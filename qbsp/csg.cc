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
#include <qbsp/csg.hh>
#include <qbsp/map.hh>
#include <qbsp/qbsp.hh>

#include <common/log.hh>
#include <common/parallel.hh>
#include <atomic>
#include <mutex>

/*

NOTES
-----
Brushes that touch still need to be split at the cut point to make a tjunction

*/

/*
==================
NewFaceFromFace

Duplicates the non point information of a face, used by SplitFace and
MergeFace.
==================
*/
std::unique_ptr<face_t> NewFaceFromFace(const face_t *in)
{
    auto newf = std::make_unique<face_t>();

    newf->planenum = in->planenum;
    newf->texinfo = in->texinfo;
    newf->contents = in->contents;
    newf->original_side = in->original_side;

    newf->origin = in->origin;
    newf->radius = in->radius;

    return newf;
}

std::unique_ptr<face_t> CopyFace(const face_t *in)
{
    auto temp = NewFaceFromFace(in);
    temp->w = in->w.clone();
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
std::tuple<std::unique_ptr<face_t>, std::unique_ptr<face_t>> SplitFace(
    std::unique_ptr<face_t> in, const qplane3d &split)
{
    if (in->w.size() < 0)
        Error("Attempting to split freed face");

    /* Fast test */
    double dot = split.distance_to(in->origin);
    if (dot > in->radius) {
        // all in front
        return {std::move(in), nullptr};
    } else if (dot < -in->radius) {
        // all behind
        return {nullptr, std::move(in)};
    }

    auto [front_winding, back_winding] = in->w.clip(split, qbsp_options.epsilon.value(), true);

    if (front_winding && !back_winding) {
        // all in front
        return {std::move(in), nullptr};
    } else if (back_winding && !front_winding) {
        // all behind
        return {nullptr, std::move(in)};
    }

    auto new_front = NewFaceFromFace(in.get());
    new_front->w = std::move(front_winding.value());

    auto new_back = NewFaceFromFace(in.get());
    new_back->w = std::move(back_winding.value());

    return {std::move(new_front), std::move(new_back)};
}

// restored from csg4.cc from type-cleanup branch

/*
==================
SplitFace

Moves from `in`. Returns {front, back}
==================
*/
std::tuple<std::optional<side_t>, std::optional<side_t>> SplitFace(side_t &in, const qplane3d &split)
{
    // fixme-brushbsp: restore fast test
#if 0
    /* Fast test */
    dot = split.distance_to(in->origin);
    if (dot > in->radius) {
        counts[SIDE_FRONT] = 1;
        counts[SIDE_BACK] = 0;
    } else if (dot < -in->radius) {
        counts[SIDE_FRONT] = 0;
        counts[SIDE_BACK] = 1;
    } else
#endif
    auto [front, back] = in.w.clip(split, qbsp_options.epsilon.value(), false);

    // Plane doesn't split this face after all
    if (!front) {
        return {std::nullopt, std::move(in)};
    }
    if (!back) {
        return {std::move(in), std::nullopt};
    }

    side_t front_side = in.clone_non_winding_data();
    front_side.w = std::move(front.value());

    side_t back_side = in.clone_non_winding_data();
    back_side.w = std::move(back.value());

    return {std::move(front_side), std::move(back_side)};
}

/*
=================
RemoveOutsideFaces

Quick test before running ClipInside; move any faces that are completely
outside the clipbrush to the outside list, without splitting them. This saves us
time in mergefaces later on (and sometimes a lot of memory)

inside  (inout)     input is the starting set of faces of `brush`, output is the faces that touch `clipbrush`
outside (out)       outputs the faces of `brush` that are definitely not touching `clipbrush`
=================
*/
static void RemoveOutsideFaces(const bspbrush_t &clipbrush, std::vector<side_t> &inside, std::vector<side_t> &outside)
{
    std::vector<side_t> oldinside;

    // clear `inside`, transfer it to `oldinside`
    std::swap(inside, oldinside);

    for (side_t &face : oldinside) {
        std::optional<winding_t> w = {face.w.clone()};

        // clip `w` by all of `clipbrush`'s reversed planes,
        // which finds intersection of `w` and `clipbrush`
        for (auto &clipface : clipbrush.sides) {
            qbsp_plane_t clipplane = -clipface.get_plane();
            w = std::move(w->clip(clipplane, qbsp_options.epsilon.value(), true)[SIDE_FRONT]);
            if (!w)
                break;
        }
        if (!w) {
            /* The face is completely outside this brush */
            outside.push_back(std::move(face));
        } else {
            inside.push_back(std::move(face));
        }
    }
}

/*
=================
ClipInside

Clips all of the faces in the inside list, possibly moving them to the
outside list or spliting it into a piece in each list.

Faces exactly on the plane will stay inside unless overdrawn by later brush

clipface    a face of the clipbrush
=================
*/
static void ClipInside(
    const side_t &clipface, bool precedence, std::vector<side_t> &inside, std::vector<side_t> &outside)
{
    std::vector<side_t> oldinside;

    // effectively make a copy of `inside`, and clear it
    std::swap(inside, oldinside);

    const qbsp_plane_t &splitplane = clipface.get_plane();

    for (side_t &face : oldinside) {
        /* HACK: Check for on-plane but not the same planenum
          ( https://github.com/ericwa/ericw-tools/issues/174 )
         */
        bool spurious_onplane = false;
        {
            std::array<size_t, SIDE_TOTAL> counts =
                face.w.calc_sides(splitplane, nullptr, nullptr, qbsp_options.epsilon.value());

            if (counts[SIDE_ON] && !counts[SIDE_FRONT] && !counts[SIDE_BACK]) {
                spurious_onplane = true;
            }
        }

        std::array<std::optional<side_t>, 2> frags;

        /* Handle exactly on-plane faces (ignoring direction) */
        if ((face.planenum ^ 1) == (clipface.planenum ^ 1) || spurious_onplane) {
            const qplane3d faceplane = face.get_plane();
            const qplane3d clipfaceplane = clipface.get_plane();
            const vec_t dp = qv::dot(faceplane.normal, clipfaceplane.normal);
            const bool opposite = (dp < 0);

            if (opposite || precedence) {
                /* always clip off opposite facing */
                frags[SIDE_FRONT] = {};
                frags[SIDE_BACK] = {std::move(face)};
            } else {
                /* leave it on the outside */
                frags[SIDE_FRONT] = {std::move(face)};
                frags[SIDE_BACK] = {};
            }
        } else {
            /* proper split */
            std::tie(frags[SIDE_FRONT], frags[SIDE_BACK]) = SplitFace(face, splitplane);
        }

        if (frags[SIDE_FRONT]) {
            outside.push_back(std::move(*frags[SIDE_FRONT]));
        }
        if (frags[SIDE_BACK]) {
            inside.push_back(std::move(*frags[SIDE_BACK]));
        }
    }
}

struct csg_stats
{
    std::atomic<int> fullyeatenbrushes{};
    std::atomic<int> postcsgfaces{};
};

/*
==================
CSGFaces

Clips overlapping areas of same-content-type brushes.

Goals:

 - speed up the BSP process; there's no point in creating nodes
   that we know will be deleted later in PruneNodes

 - give better data for the BSP heuristic: avoid having it penalizing
   splits on faces (or parts of faces - since we're clipping away overlaps here)
   that won't be in the final .bsp anyway.

Note, the output brushes will be non-closed, so they can no longer be written to .map files
for debugging.

Differences from Q1 version:

 - Q1 also clipped non-solids away where they touched solids,
   and set the "front" content flag on the solid faces to record
   that they had a liquid in front

 - Q1 tools used the faces here as the final "visual" faces to write
   out in the .bsp. This doesn't work for Q2 because we need the
   leaf portals to decide whether a face gets output between 2 leafs
   (see Q2_liquids.map).
   So, we're only using these sides to construct the BSP and assign
   leaf contents.

fixme-brushbsp: lots of moving of side_t, which is slow
==================
*/
bspbrush_t::container CSGFaces(bspbrush_t::container brushes)
{
    logging::funcheader();

    {
        size_t precsgsides = 0;
        for (auto &brush : brushes) {
            precsgsides += brush->sides.size();
        }
        logging::print(logging::flag::STAT, "     {:8} pre csg sides\n", precsgsides);
    }

    csg_stats stats{};

    // output vector for the parallel_for
    bspbrush_t::container brushvec_outsides;
    brushvec_outsides.resize(brushes.size());

    /*
     * For each brush, clip away the parts that are inside other brushes.
     * Solid brushes override non-solid brushes.
     *   brush     => the brush to be clipped
     *   clipbrush => the brush we are clipping against
     *
     * The output of this is a face list for each brush called "outside"
     */
    logging::parallel_for(static_cast<size_t>(0), brushes.size(), [&](size_t i) {
        bspbrush_t::ptr &brush = brushes[i];

        bspbrush_t::ptr brush_result = bspbrush_t::make_ptr(brush->clone());

        // temporarily move brush_result's sides to the `outside` vector
        std::vector<side_t> outside;
        std::swap(outside, brush_result->sides);

        bool overwrite = false;

        for (auto &clipbrush : brushes) {
            if (&brush == &clipbrush) {
                /* Brushes further down the list override earlier ones.
                 * This is only relevant for choosing a winner when there's two
                 * overlapping faces.
                 */
                overwrite = true;
                continue;
            }
            if (!brush->contents.equals(qbsp_options.target_game, clipbrush->contents)) {
                /* Only consider clipping equal contents against each other */
                continue;
            }

            /* check bounding box first */
            // TODO: is this a disjoint check? brush->bounds.disjoint(clipbrush->bounds)?
            int j;
            for (j = 0; j < 3; j++) {
                if (brush->bounds.mins()[j] > clipbrush->bounds.maxs()[j])
                    break;
                if (brush->bounds.maxs()[j] < clipbrush->bounds.mins()[j])
                    break;
            }
            if (j < 3)
                continue;

            // divide faces by the planes of the new brush
            std::vector<side_t> inside;

            std::swap(inside, outside);

            RemoveOutsideFaces(*clipbrush, inside, outside);
            for (auto &clipface : clipbrush->sides) {
                ClipInside(clipface, overwrite, inside, outside);
            }

            // inside = parts of `brush` that are inside `clipbrush`
            // outside = parts of `brush` that are outside `clipbrush`
        }

        if (!outside.empty()) {
            stats.postcsgfaces += outside.size();

            // save the result
            brush_result->sides = std::move(outside);
            brushvec_outsides[i] = brush_result;
        } else {
            ++stats.fullyeatenbrushes;
        }
    });

    logging::print(logging::flag::STAT, "     {:8} post csg sides\n", stats.postcsgfaces.load());
    logging::print(logging::flag::STAT, "     {:8} fully eaten brushes\n", stats.fullyeatenbrushes.load());

    return brushvec_outsides;
}
