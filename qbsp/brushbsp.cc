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

#include <qbsp/brushbsp.hh>

#include <climits>

#include <common/vectorutils.hh>
#include <qbsp/brush.hh>
#include <qbsp/csg4.hh>
#include <qbsp/map.hh>
#include <qbsp/portals.hh>
#include <qbsp/qbsp.hh>

#include <list>
#include <atomic>

#include "tbb/task_group.h"

std::atomic<int> splitnodes;

static std::any leafstats;
static bool usemidsplit;

/**
 * Total number of brushes in the map
 */
static int mapbrushes;

/*
==================
FaceSide

For BSP hueristic
==================
*/
static int FaceSide__(const winding_t &w, const qbsp_plane_t &split)
{
    bool have_front, have_back;
    int i;

    have_front = have_back = false;

    if (split.type < plane_type_t::PLANE_ANYX) {
        /* shortcut for axial planes */
        const vec_t *p = &w[0][static_cast<size_t>(split.type)];
        for (i = 0; i < w.size(); i++, p += 3) {
            if (*p > split.dist + options.epsilon.value()) {
                if (have_back)
                    return SIDE_ON;
                have_front = true;
            } else if (*p < split.dist - options.epsilon.value()) {
                if (have_front)
                    return SIDE_ON;
                have_back = true;
            }
        }
    } else {
        /* sloping planes take longer */
        for (i = 0; i < w.size(); i++) {
            const vec_t dot = split.distance_to(w[i]);
            if (dot > options.epsilon.value()) {
                if (have_back)
                    return SIDE_ON;
                have_front = true;
            } else if (dot < -options.epsilon.value()) {
                if (have_front)
                    return SIDE_ON;
                have_back = true;
            }
        }
    }

    if (!have_front)
        return SIDE_BACK;
    if (!have_back)
        return SIDE_FRONT;

    return SIDE_ON;
}

inline int FaceSide(const face_t *in, const qbsp_plane_t &split)
{
    vec_t dist = split.distance_to(in->origin);

    if (dist > in->radius)
        return SIDE_FRONT;
    else if (dist < -in->radius)
        return SIDE_BACK;
    else
        return FaceSide__(in->w, split);
}

inline int FaceSide(const side_t *in, const qbsp_plane_t &split)
{
    vec_t dist = split.distance_to(in->origin);

    if (dist > in->radius)
        return SIDE_FRONT;
    else if (dist < -in->radius)
        return SIDE_BACK;
    else
        return FaceSide__(in->w, split);
}

/*
 * Split a bounding box by a plane; The front and back bounds returned
 * are such that they completely contain the portion of the input box
 * on that side of the plane. Therefore, if the split plane is
 * non-axial, then the returned bounds will overlap.
 */
static void DivideBounds(const aabb3d &in_bounds, const qbsp_plane_t &split, aabb3d &front_bounds, aabb3d &back_bounds)
{
    int a, b, c, i, j;
    vec_t dist1, dist2, mid, split_mins, split_maxs;
    qvec3d corner;

    front_bounds = back_bounds = in_bounds;

    if (split.type < plane_type_t::PLANE_ANYX) {
        front_bounds[0][static_cast<size_t>(split.type)] = back_bounds[1][static_cast<size_t>(split.type)] = split.dist;
        return;
    }

    /* Make proper sloping cuts... */
    for (a = 0; a < 3; ++a) {
        /* Check for parallel case... no intersection */
        if (fabs(split.normal[a]) < NORMAL_EPSILON)
            continue;

        b = (a + 1) % 3;
        c = (a + 2) % 3;

        split_mins = in_bounds.maxs()[a];
        split_maxs = in_bounds.mins()[a];
        for (i = 0; i < 2; ++i) {
            corner[b] = in_bounds[i][b];
            for (j = 0; j < 2; ++j) {
                corner[c] = in_bounds[j][c];

                corner[a] = in_bounds[0][a];
                dist1 = split.distance_to(corner);

                corner[a] = in_bounds[1][a];
                dist2 = split.distance_to(corner);

                mid = in_bounds[1][a] - in_bounds[0][a];
                mid *= (dist1 / (dist1 - dist2));
                mid += in_bounds[0][a];

                split_mins = max(min(mid, split_mins), in_bounds.mins()[a]);
                split_maxs = min(max(mid, split_maxs), in_bounds.maxs()[a]);
            }
        }
        if (split.normal[a] > 0) {
            front_bounds[0][a] = split_mins;
            back_bounds[1][a] = split_maxs;
        } else {
            back_bounds[0][a] = split_mins;
            front_bounds[1][a] = split_maxs;
        }
    }
}

/*
 * Calculate the split plane metric for axial planes
 */
inline vec_t SplitPlaneMetric_Axial(const qbsp_plane_t &p, const aabb3d &bounds)
{
    vec_t value = 0;
    for (int i = 0; i < 3; i++) {
        if (static_cast<plane_type_t>(i) == p.type) {
            const vec_t dist = p.dist * p.normal[i];
            value += (bounds.maxs()[i] - dist) * (bounds.maxs()[i] - dist);
            value += (dist - bounds.mins()[i]) * (dist - bounds.mins()[i]);
        } else {
            value += 2 * (bounds.maxs()[i] - bounds.mins()[i]) * (bounds.maxs()[i] - bounds.mins()[i]);
        }
    }

    return value;
}

/*
 * Calculate the split plane metric for non-axial planes
 */
inline vec_t SplitPlaneMetric_NonAxial(const qbsp_plane_t &p, const aabb3d &bounds)
{
    aabb3d f, b;
    vec_t value = 0.0;

    DivideBounds(bounds, p, f, b);
    for (int i = 0; i < 3; i++) {
        value += (f.maxs()[i] - f.mins()[i]) * (f.maxs()[i] - f.mins()[i]);
        value += (b.maxs()[i] - b.mins()[i]) * (b.maxs()[i] - b.mins()[i]);
    }

    return value;
}

inline vec_t SplitPlaneMetric(const qbsp_plane_t &p, const aabb3d &bounds)
{
    if (p.type < plane_type_t::PLANE_ANYX)
        return SplitPlaneMetric_Axial(p, bounds);
    else
        return SplitPlaneMetric_NonAxial(p, bounds);
}

/*
==================
ChooseMidPlaneFromList

The clipping hull BSP doesn't worry about avoiding splits
==================
*/
static const side_t *ChooseMidPlaneFromList(const std::vector<std::unique_ptr<bspbrush_t>> &brushes, const aabb3d &bounds)
{
    /* pick the plane that splits the least */
    vec_t bestaxialmetric = VECT_MAX;
    side_t *bestaxialsurface = nullptr;
    vec_t bestanymetric = VECT_MAX;
    side_t *bestanysurface = nullptr;

    for (int pass = 0; pass < 2; pass++) {
        for (auto &brush : brushes) {
            if (brush->contents.is_any_detail(options.target_game) != (pass == 1)) {
                continue;
            }

            for (auto &face : brush->sides) {
                if (face.onnode)
                    continue;
                if (!face.visible) {
                    // never use as a bsp splitter, efffectively filling the brush outwards
                    continue;
                }

                const qbsp_plane_t &plane = map.planes.at(face.planenum);
                bool axial = false;

                /* check for axis aligned surfaces */
                if (plane.type < plane_type_t::PLANE_ANYX) {
                    axial = true;
                }

                /* calculate the split metric, smaller values are better */
                const vec_t metric = SplitPlaneMetric(plane, bounds);

                if (metric < bestanymetric) {
                    bestanymetric = metric;
                    bestanysurface = &face;
                }

                if (axial) {
                    if (metric < bestaxialmetric) {
                        bestaxialmetric = metric;
                        bestaxialsurface = &face;
                    }
                }
            }
        }

        if (bestanysurface != nullptr || bestaxialsurface != nullptr) {
            break;
        }
    }

    // prefer the axial split
    auto bestsurface = (bestaxialsurface == nullptr) ? bestanysurface : bestaxialsurface;
    return bestsurface;
}

/*
==================
ChoosePlaneFromList

The real BSP heuristic

fixme-brushbsp: prefer splits that include a lot of brush sides?
==================
*/
static const side_t *ChoosePlaneFromList(const std::vector<std::unique_ptr<bspbrush_t>> &brushes, const aabb3d &bounds)
{
    /* pick the plane that splits the least */
    int minsplits = INT_MAX - 1;
    vec_t bestdistribution = VECT_MAX;
    side_t *bestsurface = nullptr;

    /* passes:
     * 0: structural visible
     * 1: structural non-visible
     * 2: detail visible
     * 3: detail non-visible
     * */
    for (int pass = 0; pass < 4; pass++) {
        const bool pass_wants_detail = (pass >= 2);
        const bool pass_wants_visible = (pass == 0 || pass == 2);

        for (auto &brush : brushes) {
            if (brush->contents.is_any_detail(options.target_game) != pass_wants_detail) {
                continue;
            }

            for (auto &face : brush->sides) {
                if (face.onnode) {
                    continue;
                }
                if (face.visible != pass_wants_visible) {
                    continue;
                }

                const bool hintsplit = map.mtexinfos.at(face.texinfo).flags.is_hint;

                const qbsp_plane_t &plane = map.planes.at(face.planenum);
                int splits = 0;

                // now check all of the other faces in `brushes` and count how many
                // would get split if we used `face` as the splitting plane
                for (auto &brush2 : brushes) {
                    for (auto &face2 : brush2->sides) {
                        if (face2.planenum == face.planenum || face2.onnode)
                            continue;
                        if (!face2.visible)
                            continue; // don't penalize for splitting non-visible

                        const surfflags_t &flags = map.mtexinfos.at(face2.texinfo).flags;
                        /* Don't penalize for splitting skip faces */
                        if (flags.is_skip)
                            continue;
                        if (FaceSide(&face2, plane) == SIDE_ON) {
                            /* Never split a hint face except with a hint */
                            if (!hintsplit && flags.is_hint) {
                                splits = INT_MAX;
                                break;
                            }
                            splits++;
                            if (splits >= minsplits)
                                break;
                        }
                    }
                    if (splits > minsplits)
                        break;
                }
                if (splits > minsplits)
                    continue;

                /*
                 * if equal numbers axial planes win, otherwise decide on spatial
                 * subdivision
                 */
                if (splits < minsplits || (splits == minsplits && plane.type < plane_type_t::PLANE_ANYX)) {
                    if (plane.type < plane_type_t::PLANE_ANYX) {
                        const vec_t distribution = SplitPlaneMetric(plane, bounds);
                        if (distribution > bestdistribution && splits == minsplits)
                            continue;
                        bestdistribution = distribution;
                    }
                    /* currently the best! */
                    minsplits = splits;
                    bestsurface = &face;
                }
            }
        }

        /* If we found a candidate on first pass, don't do a second pass */
        if (bestsurface != nullptr) {
            return bestsurface;
        }
    }

    return bestsurface;
}

/*
==================
SelectPartition

Selects a surface from a linked list of surfaces to split the group on
returns NULL if the surface list can not be divided any more (a leaf)

Called in parallel.
==================
*/
static const side_t *SelectPartition(const std::vector<std::unique_ptr<bspbrush_t>> &brushes)
{
    // calculate a bounding box of the entire surfaceset
    aabb3d bounds;

    for (auto &brush : brushes) {
        bounds += brush->bounds;
    }

    // how much of the map are we partitioning?
    double fractionOfMap = brushes.size() / (double)mapbrushes;

    bool largenode = false;

    // decide if we should switch to the midsplit method
    if (options.midsplitsurffraction.value() != 0.0) {
        // new way (opt-in)
        largenode = (fractionOfMap > options.midsplitsurffraction.value());
    } else {
        // old way (ericw-tools 0.15.2+)
        if (options.maxnodesize.value() >= 64) {
            const vec_t maxnodesize = options.maxnodesize.value() - options.epsilon.value();

            largenode = (bounds.maxs()[0] - bounds.mins()[0]) > maxnodesize ||
                        (bounds.maxs()[1] - bounds.mins()[1]) > maxnodesize ||
                        (bounds.maxs()[2] - bounds.mins()[2]) > maxnodesize;
        }
    }

#if 0
    if (usemidsplit || largenode) // do fast way for clipping hull
        return ChooseMidPlaneFromList(brushes, bounds);
#endif

    // do slow way to save poly splits for drawing hull
    return ChoosePlaneFromList(brushes, bounds);
}

//============================================================================

/*
================
WindingIsTiny

Returns true if the winding would be crunched out of
existance by the vertex snapping.
================
*/
#define EDGE_LENGTH 0.2
bool WindingIsTiny(const winding_t &w, double size)
{
#if 0
    return w.area() < size;
#else
    int edges = 0;
    for (size_t i = 0; i < w.size(); i++) {
        size_t j = (i + 1) % w.size();
        const qvec3d delta = w[j] - w[i];
        const double len = qv::length(delta);
        if (len > size) {
            if (++edges == 3)
                return false;
        }
    }
    return true;
#endif
}

/*
================
WindingIsHuge

Returns true if the winding still has one of the points
from basewinding for plane
================
*/
bool WindingIsHuge(const winding_t &w)
{
    for (size_t i = 0; i < w.size(); i++) {
        for (size_t  j = 0; j < 3; j++)
            if (fabs(w[i][j]) > options.worldextent.value())
                return true;
    }
    return false;
}

/*
==================
BrushMostlyOnSide

==================
*/
planeside_t BrushMostlyOnSide(const bspbrush_t &brush, const qplane3d &plane)
{
    vec_t max = 0;
    planeside_t side = SIDE_FRONT;
    for (auto &face : brush.sides) {
        for (size_t j = 0; j < face.w.size(); j++) {
            vec_t d = qv::dot(face.w[j], plane.normal) - plane.dist;
            if (d > max) {
                max = d;
                side = SIDE_FRONT;
            }
            if (-d > max) {
                max = -d;
                side = SIDE_BACK;
            }
        }
    }
    return side;
}

/*
==================
BrushVolume

==================
*/
vec_t BrushVolume(const bspbrush_t &brush)
{
    // grab the first valid point as the corner
    
    bool found = false;
    qvec3d corner;
    for (auto &face : brush.sides) {
        if (face.w.size() > 0) {
            corner = face.w[0];
            found = true;
        }
    }
    if (!found) {
        return 0;
    }

    // make tetrahedrons to all other faces

    vec_t volume = 0;
    for (auto &face : brush.sides) {
        auto plane = Face_Plane(&face);
        vec_t d = -(qv::dot(corner, plane.normal) - plane.dist);
        vec_t area = face.w.area();
        volume += d * area;
    }

    volume /= 3;
    return volume;
}

/*
================
SplitBrush

Note, it's useful to take/return std::unique_ptr so it can quickly return the
input.

https://github.com/id-Software/Quake-2-Tools/blob/master/bsp/qbsp3/brushbsp.c#L935
================
*/
twosided<std::unique_ptr<bspbrush_t>> SplitBrush(std::unique_ptr<bspbrush_t> brush, const qplane3d &split)
{
    twosided<std::unique_ptr<bspbrush_t>> result;
    
    // check all points
    vec_t d_front = 0;
    vec_t d_back = 0;
    for (auto &face : brush->sides) {
        for (int j = 0; j < face.w.size(); j++) {
            vec_t d = qv::dot(face.w[j], split.normal) - split.dist;
            if (d > 0 && d > d_front)
                d_front = d;
            if (d < 0 && d < d_back)
                d_back = d;
        }
    }
    if (d_front < 0.1) // PLANESIDE_EPSILON)
    { // only on back
        result.back = std::move(brush);
        return result;
    }
    if (d_back > -0.1) // PLANESIDE_EPSILON)
    { // only on front
        result.front = std::move(brush);
        return result;
    }

    // create a new winding from the split plane
    auto w = std::optional<winding_t>{BaseWindingForPlane(split)};
    for (auto &face : brush->sides) {
        if (!w) {
            break;
        }
        auto [frontOpt, backOpt] = w->clip(Face_Plane(&face));
        w = backOpt;
    }

    if (!w || WindingIsTiny(*w)) { // the brush isn't really split
        planeside_t side = BrushMostlyOnSide(*brush, split);
        if (side == SIDE_FRONT)
            result.front = std::move(brush);
        else
            result.back = std::move(brush);
        return result;
    }

    if (WindingIsHuge(*w)) {
        logging::print("WARNING: huge winding\n");
    }

    winding_t midwinding = *w;

    // split it for real

    // start with 2 empty brushes

    for (int i = 0; i < 2; i++) {
        result[i] = std::make_unique<bspbrush_t>();
        result[i]->original = brush->original;
        // fixme-brushbsp: add a bspbrush_t copy constructor to make sure we get all fields
        result[i]->contents = brush->contents;
        result[i]->lmshift = brush->lmshift;
        result[i]->func_areaportal = brush->func_areaportal;
    }

    // split all the current windings

    for (const auto &face : brush->sides) {
        auto cw = face.w.clip(split, 0 /*PLANESIDE_EPSILON*/);
        for (size_t j = 0; j < 2; j++) {
            if (!cw[j])
                continue;
#if 0
			if (WindingIsTiny (cw[j]))
			{
				FreeWinding (cw[j]);
				continue;
			}
#endif

            // add the clipped face to result[j]
            side_t faceCopy = face;
            faceCopy.w = *cw[j];
            
            // fixme-brushbsp: configure any settings on the faceCopy?
            // Q2 does `cs->tested = false;`, why?

            result[j]->sides.push_back(std::move(faceCopy));
        }
    }

    // see if we have valid polygons on both sides

    for (int i = 0; i < 2; i++) {
        result[i]->update_bounds();

        bool bogus = false;
        for (int j = 0; j < 3; j++) {
            if (result[i]->bounds.mins()[j] < -4096 || result[i]->bounds.maxs()[j] > 4096) {
                logging::print("bogus brush after clip\n");
                bogus = true;
                break;
            }
        }

        if (result[i]->sides.size() < 3 || bogus) {
            result[i] = nullptr;
        }
    }

    if (!(result[0] && result[1])) {
        if (!result[0] && !result[1])
            logging::print("split removed brush\n");
        else
            logging::print("split not on both sides\n");
        if (result[0]) {
            result.front = std::move(brush);
        }
        // fixme: use of move here, might move twice. should it be `else`?
        if (result[1]) {
            result.back = std::move(brush);
        }
        return result;
    }

    // add the midwinding to both sides
    for (int i = 0; i < 2; i++) {
        side_t cs{};
        
        const bool brushOnFront = (i == 0);
        
        // for the brush on the front side of the plane, the `midwinding`
        // (the face that is touching the plane) should have a normal opposite the plane's normal
        cs.planenum = FindPlane(brushOnFront ? -split : split, &cs.planeside);
        cs.texinfo = map.skip_texinfo;

        // fixme-brushbsp: configure any other settings on the face?

        cs.w = brushOnFront ? midwinding.flip() : midwinding;

        result[i]->sides.push_back(std::move(cs));
    }

    {
        vec_t v1;
        int i;

        for (i = 0; i < 2; i++) {
            v1 = BrushVolume(*result[i]);
            if (v1 < 1.0) {
                result[i] = nullptr;
                //			qprintf ("tiny volume after clip\n");
            }
        }
    }

    return result;
}

//============================================================================

/*
==================
DivideNodeBounds
==================
*/
inline void DivideNodeBounds(node_t *node, const qbsp_plane_t &split)
{
    DivideBounds(node->bounds, split, node->children[0]->bounds, node->children[1]->bounds);
}

static bool AllDetail(const std::vector<std::unique_ptr<bspbrush_t>> &brushes)
{
    for (auto &brush : brushes) {
        if (!brush->contents.is_any_detail(options.target_game)) {
            return false;
        }
    }
    return true;
}

/*
==================
CreateLeaf

Determines the contents of the leaf and creates the final list of
original faces that have some fragment inside this leaf.

Called in parallel.
==================
*/
static void CreateLeaf(std::vector<std::unique_ptr<bspbrush_t>> brushes, node_t *leafnode)
{
    leafnode->facelist.clear();
    leafnode->planenum = PLANENUM_LEAF;

    leafnode->contents = options.target_game->create_empty_contents();
    for (auto &brush : brushes) {
        leafnode->contents = options.target_game->combine_contents(leafnode->contents, brush->contents);
    }
    for (auto &brush : brushes) {
        Q_assert(brush->original != nullptr);
        leafnode->original_brushes.push_back(brush->original);
    }

    options.target_game->count_contents_in_stats(leafnode->contents, leafstats);
}

/*
==================
PartitionBrushes

Called in parallel.
==================
*/
static void PartitionBrushes(std::vector<std::unique_ptr<bspbrush_t>> brushes, node_t *node)
{
    auto *split = const_cast<side_t *>(SelectPartition(brushes));

    if (split == nullptr) { // this is a leaf node
        node->planenum = PLANENUM_LEAF;

        CreateLeaf(std::move(brushes), node);
        return;
    }

    splitnodes++;

    node->children[0] = new node_t{};
    node->children[0]->parent = node;
    node->children[1] = new node_t{};
    node->children[1]->parent = node;
    node->planenum = FindPositivePlane(split->planenum);

    node->detail_separator = AllDetail(brushes);

    const qbsp_plane_t &splitplane = map.planes.at(node->planenum);

    DivideNodeBounds(node, splitplane);

    // multiple surfaces, so split all the polysurfaces into front and back lists
    std::vector<std::unique_ptr<bspbrush_t>> frontlist, backlist;

    for (auto &brush : brushes) {
        // NOTE: we're destroying `brushes` here with the std::move()
        auto frags = SplitBrush(std::move(brush), splitplane);

        // mark faces which were used as a splitter
        for (auto &brushMaybe : frags) {
            if (brushMaybe) {
                for (auto &face : brushMaybe->sides) {
                    if (face.planenum == split->planenum) {
                        face.onnode = true;
                    }
                }
            }
        }

        if (frags.front) {
            if (frags.front->sides.empty()) {
                FError("Surface with no faces");
            }
            frontlist.emplace_back(std::move(frags.front));
        }
        if (frags.back) {
            if (frags.back->sides.empty()) {
                FError("Surface with no faces");
            }
            backlist.emplace_back(std::move(frags.back));
        }
    }

    tbb::task_group g;
    g.run([&]() { PartitionBrushes(std::move(frontlist), node->children[0]); });
    g.run([&]() { PartitionBrushes(std::move(backlist), node->children[1]); });
    g.wait();
}

/*
==================
SolidBSP
==================
*/
tree_t *BrushBSP(mapentity_t *entity, bool midsplit)
{
    tree_t *tree = new tree_t{};

    if (entity->brushes.empty()) {
        /*
         * We allow an entity to be constructed with no visible brushes
         * (i.e. all clip brushes), but need to construct a simple empty
         * collision hull for the engine. Probably could be done a little
         * smarter, but this works.
         */
        node_t *headnode = new node_t{};
        headnode->bounds = entity->bounds;
        headnode->children[0] = new node_t{};
        headnode->children[0]->planenum = PLANENUM_LEAF;
        headnode->children[0]->contents = options.target_game->create_empty_contents();
        headnode->children[0]->parent = headnode;
        headnode->children[1] = new node_t{};
        headnode->children[1]->planenum = PLANENUM_LEAF;
        headnode->children[1]->contents = options.target_game->create_empty_contents();
        headnode->children[1]->parent = headnode;

        tree->headnode = headnode;
        tree->bounds = headnode->bounds;

        return tree;
    }

    logging::print(logging::flag::PROGRESS, "---- {} ----\n", __func__);

    // Count visible/nonvisible brush sides (this is the side effect of FillOutside)
    int visible_brush_sides = 0;
    int invisible_brush_sides = 0;
    for (const auto &brush : entity->brushes) {
        for (auto &side : brush->sides) {
            if (side.visible) {
                ++visible_brush_sides;
            } else {
                ++invisible_brush_sides;
            }
        }
    }

    logging::print(logging::flag::STAT, "     {:8} visible brush sides\n", visible_brush_sides);
    logging::print(logging::flag::STAT, "     {:8} invisible brush sides\n", invisible_brush_sides);

    node_t *headnode = new node_t{};
    usemidsplit = midsplit;

    // calculate a bounding box for the entire model
    headnode->bounds = entity->bounds;

    // recursively partition everything
    splitnodes = 0;
    leafstats = options.target_game->create_content_stats();
    // count map surfaces; this is used when deciding to switch between midsplit and the expensive partitioning
    mapbrushes = entity->brushes.size();

    // set the original pointers
    std::vector<std::unique_ptr<bspbrush_t>> brushcopies;
    for (const auto &original : entity->brushes) {
        auto copy = std::make_unique<bspbrush_t>(*original);
        copy->original = original.get();
        brushcopies.push_back(std::move(copy));
    }
    PartitionBrushes(std::move(brushcopies), headnode);

    //logging::percent(csgmergefaces, csgmergefaces, entity == map.pWorldEnt());

    logging::print(logging::flag::STAT, "     {:8} split nodes\n", splitnodes.load());
    options.target_game->print_content_stats(leafstats, "leaves");

    tree->bounds = headnode->bounds;
    tree->headnode = headnode;

    return tree;
}
