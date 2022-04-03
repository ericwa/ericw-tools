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

#include <qbsp/qbsp.hh>

#include <atomic>
#include <mutex>

#include "tbb/parallel_for.h"

/*

NOTES
-----
Brushes that touch still need to be split at the cut point to make a tjunction

*/

static std::atomic<int> brushfaces;
static int csgfaces;
int csgmergefaces;

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
    newf->original = in->original;
    newf->contents = in->contents;
    newf->lmshift = in->lmshift;
    newf->src_entity = in->src_entity;

    newf->origin = in->origin;
    newf->radius = in->radius;

    return newf;
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
    vec_t *dists = (vec_t *)alloca(sizeof(vec_t) * (in->w.size() + 1));
    side_t *sides = (side_t *)alloca(sizeof(side_t) * (in->w.size() + 1));
    std::array<size_t, SIDE_TOTAL> counts{};
    vec_t dot;
    size_t i, j;
    face_t *newf, *new2;
    qvec3d mid;

    if (in->w.size() < 0)
        Error("Attempting to split freed face");

    /* Fast test */
    dot = split.distance_to(in->origin);
    if (dot > in->radius) {
        counts[SIDE_FRONT] = 1;
        counts[SIDE_BACK] = 0;
    } else if (dot < -in->radius) {
        counts[SIDE_FRONT] = 0;
        counts[SIDE_BACK] = 1;
    } else {
        counts = in->w.calc_sides(split, dists, sides, ON_EPSILON);
    }

    // Plane doesn't split this face after all
    if (!counts[SIDE_FRONT]) {
        return {nullptr, in};
    }
    if (!counts[SIDE_BACK]) {
        return {in, nullptr};
    }

    newf = NewFaceFromFace(in);
    new2 = NewFaceFromFace(in);

    // distribute the points and generate splits
    for (i = 0; i < in->w.size(); i++) {
        // Note: Possible for numpoints on newf or new2 to exceed MAXEDGES if
        // in->w.numpoints == MAXEDGES and it is a really devious split.
        const qvec3d &p1 = in->w[i];

        if (sides[i] == SIDE_ON) {
            newf->w.push_back(p1);
            new2->w.push_back(p1);
            continue;
        }

        if (sides[i] == SIDE_FRONT) {
            new2->w.push_back(p1);
        } else {
            newf->w.push_back(p1);
        }

        if (sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i])
            continue;

        // generate a split point
        const qvec3d &p2 = in->w[(i + 1) % in->w.size()];

        dot = dists[i] / (dists[i] - dists[i + 1]);
        for (j = 0; j < 3; j++) { // avoid round off error when possible
            if (split.normal[j] == 1)
                mid[j] = split.dist;
            else if (split.normal[j] == -1)
                mid[j] = -split.dist;
            else
                mid[j] = p1[j] + dot * (p2[j] - p1[j]);
        }

        newf->w.push_back(mid);
        new2->w.push_back(mid);
    }

    if (newf->w.size() > MAXEDGES || new2->w.size() > MAXEDGES)
        FError("Internal error: numpoints > MAXEDGES");

    /* free the original face now that it is represented by the fragments */
    delete in;

    // {front, back}
    return {new2, newf};
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

/*
==================
BrushIndexInMap

Returns the index of the brush in the .map files.
Only call with an "original" brush (from entity->brushes).
Used for clipping priority.
==================
*/
static int BrushIndexInMap(const mapentity_t *entity, const brush_t *brush)
{
    Q_assert(brush >= entity->brushes.data());
    Q_assert(brush < (entity->brushes.data() + entity->brushes.size()));

    return static_cast<int>(brush - entity->brushes.data());
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

    const int srcindex = BrushIndexInMap(srcentity, srcbrush);
    const int clipindex = BrushIndexInMap(srcentity, clipbrush);

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
        result.insert(&brush);
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
