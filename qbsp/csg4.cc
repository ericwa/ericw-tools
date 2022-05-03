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
    newf->src_entity = in->src_entity;

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
            if (brush->bounds.disjoint(clipbrush->bounds)) {
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
