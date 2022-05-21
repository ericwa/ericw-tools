/*
    Copyright (C) 1996-1997  Id Software, Inc.
    Copyright (C) 1997       Greg Lewis

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

    See file, 'COPYING', for details.
*/
// csg4.c

#include <qbsp/brush.hh>
#include <qbsp/csg4.hh>
#include <qbsp/map.hh>
#include <qbsp/solidbsp.hh>
#include <qbsp/qbsp.hh>

#include <atomic>
#include <mutex>

#include "tbb/parallel_for.h"

/*

NOTES
-----
Brushes that touch still need to be split at the cut point to make a tjunction

*/

// acquire this for anything that can't run in parallel during CSGFaces
std::mutex csgfaces_lock;

/*
==================
MakeSkipTexinfo
==================
*/
int MakeSkipTexinfo()
{
    // FindMiptex, FindTexinfo not threadsafe
    std::unique_lock<std::mutex> lck{csgfaces_lock};

    mtexinfo_t mt{};

    mt.miptex = FindMiptex("skip", true);
    mt.flags = {};
    mt.flags.is_skip = true;

    return FindTexinfo(mt);
}

/*
==================
NewFaceFromFace

Duplicates the non point information of a face, used by SplitFace and
MergeFace.
==================
*/
face_t *NewFaceFromFace(const face_t *in)
{
    face_t *newf = new face_t{};

    newf->planenum = in->planenum;
    newf->texinfo = in->texinfo;
    newf->planeside = in->planeside;
    newf->contents = in->contents;
    newf->lmshift = in->lmshift;

    newf->origin = in->origin;
    newf->radius = in->radius;

    return newf;
}

face_t* CopyFace(const face_t* in)
{
    face_t *temp = NewFaceFromFace(in);
    temp->w = in->w;
    return temp;
}

void UpdateFaceSphere(face_t *in)
{
    in->origin = in->w.center();
    in->radius = 0;
    for (size_t i = 0; i < in->w.size(); i++) {
        in->radius = max(in->radius, qv::distance2(in->w[i], in->origin));
    }
    in->radius = sqrt(in->radius);
}

/*
==================
SplitFace

Frees in. Returns {front, back}
==================
*/
std::tuple<face_t *, face_t *> SplitFace(face_t *in, const qplane3d &split)
{
    if (in->w.size() < 0)
        Error("Attempting to split freed face");

    /* Fast test */
    double dot = split.distance_to(in->origin);
    if (dot > in->radius) {
        // all in front
        return {in, nullptr};
    } else if (dot < -in->radius) {
        // all behind
        return {nullptr, in};
    }

    auto [front_winding, back_winding] = in->w.clip(split, ON_EPSILON, true);

    if (front_winding && !back_winding) {
        // all in front
        return {in, nullptr};
    } else if (back_winding && !front_winding) {
        // all behind
        return {nullptr, in};
    }

    face_t *new_front = NewFaceFromFace(in);
    new_front->w = std::move(front_winding.value());

    face_t *new_back = NewFaceFromFace(in);
    new_back->w = std::move(back_winding.value());

    if (new_front->w.size() > MAXEDGES || new_back->w.size() > MAXEDGES)
        FError("Internal error: numpoints > MAXEDGES");

    /* free the original face now that it is represented by the fragments */
    delete in;

    return {new_front, new_back};
}

/*
=================
RemoveOutsideFaces

Quick test before running ClipInside; move any faces that are completely
outside the brush to the outside list, without splitting them. This saves us
time in mergefaces later on (and sometimes a lot of memory)

Input is a list of faces in the param `inside`.
On return, the ones touching `brush` remain in `inside`, the rest are added to `outside`.
=================
*/
static void RemoveOutsideFaces(const brush_t &brush, std::list<face_t *> *inside, std::list<face_t *> *outside)
{
    std::list<face_t *> oldinside;

    // clear `inside`, transfer it to `oldinside`
    std::swap(*inside, oldinside);

    for (face_t *face : oldinside) {
        std::optional<winding_t> w = face->w;
        for (auto &clipface : brush.faces) {
            w = w->clip(Face_Plane(&clipface), ON_EPSILON, false)[SIDE_BACK];
            if (!w)
                break;
        }
        if (!w) {
            /* The face is completely outside this brush */
            outside->push_front(face);
        } else {
            inside->push_front(face);
        }
    }
}

/*
=================
ClipInside

Clips all of the faces in the inside list, possibly moving them to the
outside list or spliting it into a piece in each list.

Faces exactly on the plane will stay inside unless overdrawn by later brush
=================
*/
static void ClipInside(
    const face_t *clipface, bool precedence, std::list<face_t *> *inside, std::list<face_t *> *outside)
{
    std::list<face_t *> oldinside;

    // effectively make a copy of `inside`, and clear it
    std::swap(*inside, oldinside);

    const qbsp_plane_t &splitplane = map.planes[clipface->planenum];

    for (face_t *face : oldinside) {
        /* HACK: Check for on-plane but not the same planenum
          ( https://github.com/ericwa/ericw-tools/issues/174 )
         */
        bool spurious_onplane = false;
        {
            std::array<size_t, SIDE_TOTAL> counts = face->w.calc_sides(splitplane, nullptr, nullptr, ON_EPSILON);

            if (counts[SIDE_ON] && !counts[SIDE_FRONT] && !counts[SIDE_BACK]) {
                spurious_onplane = true;
            }
        }

        std::array<face_t *, 2> frags;

        /* Handle exactly on-plane faces */
        if (face->planenum == clipface->planenum || spurious_onplane) {
            const qplane3d faceplane = Face_Plane(face);
            const qplane3d clipfaceplane = Face_Plane(clipface);
            const vec_t dp = qv::dot(faceplane.normal, clipfaceplane.normal);
            const bool opposite = (dp < 0);

            if (opposite || precedence) {
                /* always clip off opposite facing */
                frags[clipface->planeside] = {};
                frags[!clipface->planeside] = {face};
            } else {
                /* leave it on the outside */
                frags[clipface->planeside] = {face};
                frags[!clipface->planeside] = {};
            }
        } else {
            /* proper split */
            std::tie(frags[0], frags[1]) = SplitFace(face, splitplane);
        }

        if (frags[clipface->planeside]) {
            outside->push_front(frags[clipface->planeside]);
        }
        if (frags[!clipface->planeside]) {
            inside->push_front(frags[!clipface->planeside]);
        }
    }
}

face_t *MirrorFace(const face_t *face)
{
    face_t *newface = NewFaceFromFace(face);
    newface->w = face->w.flip();
    newface->planeside = face->planeside ^ 1;
    newface->contents.swap();
    newface->lmshift.swap();

    return newface;
}

static void FreeFaces(std::list<face_t *> &facelist)
{
    for (face_t *face : facelist) {
        delete face;
    }
    facelist.clear();
}

//==========================================================================

static std::vector<std::unique_ptr<brush_t>> SingleBrush(std::unique_ptr<brush_t> a)
{
    std::vector<std::unique_ptr<brush_t>> res;
    res.push_back(std::move(a));
    return res;
}

static bool ShouldClipbrushEatBrush(const brush_t &brush, const brush_t &clipbrush)
{
    if (clipbrush.contents.is_empty(options.target_game)) {
        /* Ensure hint never clips anything */
        return false;
    }

    if (clipbrush.contents.is_detail(CFLAGS_DETAIL_ILLUSIONARY) &&
        !brush.contents.is_detail(CFLAGS_DETAIL_ILLUSIONARY)) {
        /* CONTENTS_DETAIL_ILLUSIONARY never clips anything but itself */
        return false;
    }

    if (clipbrush.contents.is_detail(CFLAGS_DETAIL_FENCE) && !brush.contents.is_detail(CFLAGS_DETAIL_FENCE)) {
        /* CONTENTS_DETAIL_FENCE never clips anything but itself */
        return false;
    }

    if (clipbrush.contents.types_equal(brush.contents, options.target_game) &&
        !clipbrush.contents.clips_same_type()) {
        /* _noclipfaces key */
        return false;
    }

    /*
     * If the brush is solid and the clipbrush is not, then we need to
     * keep the inside faces and set the outside contents to those of
     * the clipbrush. Otherwise, these inside surfaces are hidden and
     * should be discarded.
     *
     * FIXME: clean this up, the predicate seems to be "can you see 'brush' from inside 'clipbrush'"
     */
    if ((brush.contents.is_solid(options.target_game) && !clipbrush.contents.is_solid(options.target_game))

        ||
        (brush.contents.is_sky(options.target_game) && (!clipbrush.contents.is_solid(options.target_game) &&
                                                        !clipbrush.contents.is_sky(options.target_game)))

        || (brush.contents.is_detail(CFLAGS_DETAIL) && (!clipbrush.contents.is_solid(options.target_game) &&
                                                        !clipbrush.contents.is_sky(options.target_game) &&
                                                        !clipbrush.contents.is_detail(CFLAGS_DETAIL)))

        || (brush.contents.is_liquid(options.target_game) &&
            clipbrush.contents.is_detail(CFLAGS_DETAIL_ILLUSIONARY))

        || (brush.contents.is_fence() && clipbrush.contents.is_liquid(options.target_game))) {
        return false;
    }

    return true;
}

static std::list<face_t *> CSGFace_ClipAgainstSingleBrush(std::list<face_t *> input, const mapentity_t *srcentity, const brush_t *srcbrush, const brush_t *clipbrush)
{
    if (srcbrush == clipbrush) {
        logging::print("    ignoring self-clip\n");
        return input;
    }

    const int srcindex = srcbrush->file_order;
    const int clipindex = clipbrush->file_order;

    if (!ShouldClipbrushEatBrush(*srcbrush, *clipbrush)) {
        return {input};
    }

    std::list<face_t *> inside {input};
    std::list<face_t *> outside;
    RemoveOutsideFaces(*clipbrush, &inside, &outside);

    // at this point, inside = the faces of `input` which are touching `clipbrush`
    //                outside = the other faces of `input`

    const bool overwrite = (srcindex < clipindex);

    for (auto &clipface : clipbrush->faces)
        ClipInside(&clipface, overwrite, &inside, &outside);

    // inside = parts of `brush` that are inside `clipbrush`
    // outside = parts of `brush` that are outside `clipbrush`

    return outside;
}

// fixme-brushbsp: determinism: sort `result` set by .map file order
// fixme-brushbsp: add bounds test
#if 0
static void GatherPossibleClippingBrushes_R(const node_t *node, const face_t *srcface, std::set<const brush_t *> &result)
{
    if (node->planenum == PLANENUM_LEAF) {
        for (auto *brush : node->original_brushes) {
            result.insert(brush);
        }
        return;
    }

    GatherPossibleClippingBrushes_R(node->children[0], srcface, result);
    GatherPossibleClippingBrushes_R(node->children[1], srcface, result);
}
#endif

/*
==================
GatherPossibleClippingBrushes

Starting a search at `node`, returns brushes that possibly intersect `srcface`.
==================
*/
static std::set<const brush_t *> GatherPossibleClippingBrushes(const mapentity_t* srcentity, const node_t *node, const face_t *srcface)
{
    std::set<const brush_t *> result;
    // fixme-brushbsp: implement this, need node->original_brushes working
#if 0
    GatherPossibleClippingBrushes_R(node, srcface, result);
#else
    for (auto &brush : srcentity->brushes) {
        result.insert(brush.get());
    }
#endif
    return result;
}

/*
==================
CSGFace

Given `srcface`, which was produced from `srcbrush` and lies on `srcnode`:

 - search srcnode as well as its children for brushes which might clip
   srcface.

 - clip srcface against all such brushes

Frees srcface.
==================
*/
std::list<face_t *> CSGFace(face_t *srcface, const mapentity_t *srcentity, const brush_t *srcbrush, const node_t *srcnode)
{
    const auto possible_clipbrushes = GatherPossibleClippingBrushes(srcentity, srcnode, srcface);

    logging::print("face {} has {} possible clipbrushes\n", (void *)srcface, possible_clipbrushes.size());

    std::list<face_t *> result{srcface};

    for (const brush_t *possible_clipbrush : possible_clipbrushes) {
        result = CSGFace_ClipAgainstSingleBrush(std::move(result), srcentity, srcbrush, possible_clipbrush);
    }

    return result;
}

/*
==================
SubtractBrush

Returns the fragments from a - b
==================
*/
static std::vector<std::unique_ptr<brush_t>> SubtractBrush(std::unique_ptr<brush_t> a, const brush_t& b)
{
    // first, check if `a` is fully in front of _any_ of b's planes
    for (const auto &side : b.faces) {
        // is `a` fully in front of `side`?
        bool fully_infront = true;

        // fixme-brushbsp: factor this out somewhere
        for (const auto &a_face : a->faces) {
            for (const auto &a_point : a_face.w) {
                if (Face_Plane(&side).distance_to(a_point) < 0) {
                    fully_infront = false;
                    break;
                }
            }
            if (!fully_infront) {
                break;
            }
        }

        if (fully_infront) {
            // `a` is fully in front of this side of b, so they don't actually intersect
            return SingleBrush(std::move(a));
        }
    }

    std::vector<std::unique_ptr<brush_t>> frontlist;
    std::vector<std::unique_ptr<brush_t>> unclassified = SingleBrush(std::move(a));

    for (const auto &side : b.faces) {
        std::vector<std::unique_ptr<brush_t>> new_unclassified;

        for (auto &fragment : unclassified) {
            // destructively processing `unclassified` here
            auto [front, back] = SplitBrush(std::move(fragment), Face_Plane(&side));
            if (front) {
                frontlist.push_back(std::move(front));
            }
            if (back) {
                new_unclassified.push_back(std::move(back));
            }
        }

        unclassified = std::move(new_unclassified);
    }

    return frontlist;
}

/*
==================
BrushGE

Returns a >= b as far as brush clipping
==================
*/
bool BrushGE(const brush_t& a, const brush_t& b)
{
    // same contents clip each other
    if (a.contents == b.contents && a.contents.clips_same_type()) {
        // map file order
        return a.file_order > b.file_order;
    }

    // only chop if at least one of the two contents is
    // opaque (solid, sky, or detail)
    if (!(a.contents.chops(options.target_game) || b.contents.chops(options.target_game))) {
        return false;
    }

    int32_t a_pri = a.contents.priority(options.target_game);
    int32_t b_pri = b.contents.priority(options.target_game);

    if (a_pri == b_pri) {
        // map file order
        return a.file_order > b.file_order;
    }

    return a_pri >= b_pri;
}

/*
==================
ChopBrushes

Clips off any overlapping portions of brushes
==================
*/
std::vector<std::unique_ptr<brush_t>> ChopBrushes(const std::vector<std::unique_ptr<brush_t>>& input)
{
    logging::print(logging::flag::PROGRESS, "---- {} ----\n", __func__);

    // each inner vector corresponds to a brush in `input`
    // (set up this way for thread safety)
    std::vector<std::vector<std::unique_ptr<brush_t>>> brush_fragments;
    brush_fragments.resize(input.size());

    /*
     * For each brush, clip away the parts that are inside other brushes.
     * Solid brushes override non-solid brushes.
     *   brush     => the brush to be clipped
     *   clipbrush => the brush we are clipping against
     *
     * The output of this is a face list for each brush called "outside"
     */
    tbb::parallel_for(static_cast<size_t>(0), input.size(), [&](const size_t i) {
        const auto& brush = input[i];

        // the fragments `brush` is chopped into
        std::vector<std::unique_ptr<brush_t>> brush_result = SingleBrush(
            // start with a copy of brush
            std::make_unique<brush_t>(*brush)
        );

        for (auto &clipbrush : input) {
            if (brush == clipbrush) {
                continue;
            }
            if (brush->bounds.disjoint_or_touching(clipbrush->bounds)) {
                continue;
            }

            if (BrushGE(*clipbrush, *brush)) {
                std::vector<std::unique_ptr<brush_t>> new_result;

                // clipbrush is stronger. 
                // rebuild existing fragments in brush_result, cliping them to clipbrush
                for (auto &current_fragment : brush_result) {
                    for (auto &new_fragment : SubtractBrush(std::move(current_fragment), *clipbrush)) {
                        new_result.push_back(std::move(new_fragment));
                    }
                }
                
                brush_result = std::move(new_result);
            }
        }

        // save the result
        brush_fragments[i] = std::move(brush_result);
    });

    // Non parallel part:
    std::vector<std::unique_ptr<brush_t>> result;
    for (auto &fragment_list : brush_fragments) {
        for (auto &fragment : fragment_list) {
            result.push_back(std::move(fragment));
        }
    }
    
    logging::print(logging::flag::STAT, "     {:8} brushes\n", input.size());
    logging::print(logging::flag::STAT, "     {:8} chopped brushes\n", result.size());

    return result;
}
