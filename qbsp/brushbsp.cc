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

#include <common/log.hh>
#include <qbsp/brush.hh>
#include <qbsp/map.hh>
#include <qbsp/portals.hh>
#include <qbsp/qbsp.hh>
#include <qbsp/tree.hh>

#include <list>
#include <atomic>

#include "tbb/task_group.h"

// if a brush just barely pokes onto the other side,
// let it slide by without chopping
constexpr double PLANESIDE_EPSILON = 0.001;
// 0.1

constexpr int PSIDE_FRONT = 1;
constexpr int PSIDE_BACK = 2;
constexpr int PSIDE_BOTH = (PSIDE_FRONT | PSIDE_BACK);
// this gets OR'ed in in the return value of QuickTestBrushToPlanenum if one of the brush sides is on the input plane
constexpr int PSIDE_FACING = 4;

#define CHECK_PLANE_AGAINST_VOLUME 1

struct bspstats_t : logging::stat_tracker_t
{
    std::unique_ptr<content_stats_base_t> leafstats;
    // total number of nodes, includes c_nonvis
    stat &c_nodes = register_stat("nodes");
    // number of nodes created by splitting on a side_t which had !visible
    stat &c_nonvis = register_stat("non-visible nodes");
    // total number of nodes created by qbsp3 method
    stat &c_qbsp3 = register_stat("expensive split nodes");
    // total number of nodes created by midsplit
    stat &c_midsplit = register_stat("mid-split nodes");
    // total number of leafs
    stat &c_leafs = register_stat("leaves");
    // number of bogus brushes (beyond world extents)
    stat &c_bogus = register_stat("bogus brushes");
    // number of brushes entirely removed from a split
    stat &c_brushesremoved = register_stat("brushes removed from a split");
    // number of brushes half-removed from a split
    stat &c_brushesonesided = register_stat("brushes split only on one side");
    // tiny volumes after clipping
    stat &c_tinyvolumes = register_stat("tiny volumes removed after splits");
};

/*
==================
BrushFromBounds

Creates a new axial brush
==================
*/
bspbrush_t::ptr BrushFromBounds(const aabb3d &bounds)
{
    auto b = bspbrush_t::make_ptr();

    b->sides.resize(6);
    for (int i = 0; i < 3; i++) {
        {
            qplane3d plane{};
            plane.normal[i] = 1;
            plane.dist = bounds.maxs()[i];

            side_t &side = b->sides[i];
            side.planenum = map.add_or_find_plane(plane);
        }

        {
            qplane3d plane{};
            plane.normal[i] = -1;
            plane.dist = -bounds.mins()[i];

            side_t &side = b->sides[3 + i];
            side.planenum = map.add_or_find_plane(plane);
        }
    }

    CreateBrushWindings(*b.get());

    return b;
}

/*
==================
BrushVolume

==================
*/
template<typename T>
static vec_t BrushVolume(T begin, T end)
{
    // grab the first valid point as the corner

    bool found = false;
    qvec3d corner;
    for (auto it = begin; it != end; it++) {
        auto &face = *it;

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
    for (auto it = begin; it != end; it++) {
        auto &face = *it;

        if (!face.w.size()) {
            continue;
        }

        auto &plane = face.get_plane();
        vec_t d = -(qv::dot(corner, plane.get_normal()) - plane.get_dist());
        vec_t area = face.w.area();
        volume += d * area;
    }

    volume /= 3;
    return volume;
}

vec_t BrushVolume(const bspbrush_t &brush)
{
    return BrushVolume(brush.sides.begin(), brush.sides.end());
}

//========================================================

/*
==============
BoxOnPlaneSide

Returns PSIDE_FRONT, PSIDE_BACK, or PSIDE_BOTH
==============
*/
static int BoxOnPlaneSide(const aabb3d &bounds, const qbsp_plane_t &plane)
{
    // axial planes are easy
    if (plane.get_type() < plane_type_t::PLANE_ANYX) {
        int side = 0;
        if (bounds.maxs()[static_cast<int>(plane.get_type())] > plane.get_dist() + PLANESIDE_EPSILON)
            side |= PSIDE_FRONT;
        if (bounds.mins()[static_cast<int>(plane.get_type())] < plane.get_dist() - PLANESIDE_EPSILON)
            side |= PSIDE_BACK;
        return side;
    }

    // create the proper leading and trailing verts for the box
    std::array<qvec3d, 2> corners;
    for (int i = 0; i < 3; i++) {
        if (plane.get_normal()[i] < 0) {
            corners[0][i] = bounds.mins()[i];
            corners[1][i] = bounds.maxs()[i];
        } else {
            corners[1][i] = bounds.mins()[i];
            corners[0][i] = bounds.maxs()[i];
        }
    }

    double dist1 = qv::dot(plane.get_normal(), corners[0]) - plane.get_dist();
    double dist2 = qv::dot(plane.get_normal(), corners[1]) - plane.get_dist();
    int side = 0;
    if (dist1 >= PLANESIDE_EPSILON)
        side = PSIDE_FRONT;
    if (dist2 < PLANESIDE_EPSILON)
        side |= PSIDE_BACK;

    return side;
}

#if 0
static int SphereOnPlaneSide(const qvec3d &sphere_origin, double sphere_radius, const qplane3d &plane)
{
    const double sphere_dist = plane.distance_to(sphere_origin);
    if (sphere_dist > sphere_radius) {
        return PSIDE_FRONT;
    }
    if (sphere_dist < -sphere_radius) {
        return PSIDE_BACK;
    }
    return PSIDE_BOTH;
}
#endif

#if 0
/*
============
QuickTestBrushToPlanenum

Returns PSIDE_BACK, PSIDE_FRONT, PSIDE_BOTH depending on how the brush is split by planenum
============
*/
static int QuickTestBrushToPlanenum(const bspbrush_t &brush, int planenum, int *numsplits)
{
    *numsplits = 0;

    // if the brush actually uses the planenum,
    // we can tell the side for sure
    for (auto& side : brush.sides) {
        int num = FindPlane(side.plane, nullptr);
        if (num == planenum) {
            if (side.plane_flipped == SIDE_FRONT) {
                return PSIDE_BACK|PSIDE_FACING;
            } else {
                return PSIDE_FRONT|PSIDE_FACING;
            }
        }
    }

    // box on plane side
    auto plane = map.get_plane(planenum);
    int s = BoxOnPlaneSide(brush.bounds, plane);

    // if both sides, count the visible faces split
    if (s == PSIDE_BOTH) {
        *numsplits += 3;
    }

    return s;
}
#endif

/*
============
TestBrushToPlanenum

============
*/
static int TestBrushToPlanenum(
    const bspbrush_t &brush, size_t planenum, int *numsplits, bool *hintsplit, int *epsilonbrush)
{
    if (numsplits) {
        *numsplits = 0;
    }
    if (hintsplit) {
        *hintsplit = false;
    }

    // if the brush actually uses the planenum,
    // we can tell the side for sure
    for (auto &side : brush.sides) {
        if (side.planenum == planenum) {
            return PSIDE_BACK | PSIDE_FACING;
        } else if (side.planenum == (planenum ^ 1)) {
            return PSIDE_FRONT | PSIDE_FACING;
        }
    }

    // box on plane side
    // int s = SphereOnPlaneSide(brush.sphere_origin, brush.sphere_radius, plane);
    const qbsp_plane_t &plane = map.get_plane(planenum);
    int s = BoxOnPlaneSide(brush.bounds, plane);
    if (s != PSIDE_BOTH)
        return s;

    if (numsplits && hintsplit && epsilonbrush) {
        // if both sides, count the visible faces split
        vec_t d_front = 0;
        vec_t d_back = 0;

        for (const side_t &side : brush.sides) {
            if (side.onnode)
                continue; // on node, don't worry about splits
            if (!side.is_visible())
                continue; // we don't care about non-visible
            auto &w = side.w;
            if (!w)
                continue;
            int front = 0;
            int back = 0;
            for (auto &point : w) {
                const double d = qv::dot(point, plane.get_normal()) - plane.get_dist();
                if (d > d_front)
                    d_front = d;
                if (d < d_back)
                    d_back = d;

                if (d > 0.1) // PLANESIDE_EPSILON)
                    front = 1;
                if (d < -0.1) // PLANESIDE_EPSILON)
                    back = 1;
            }
            if (front && back) {
                if (!(side.get_texinfo().flags.is_hintskip)) {
                    (*numsplits)++;
                    if (side.get_texinfo().flags.is_hint) {
                        *hintsplit = true;
                    }
                }
            }
        }

        if ((d_front > 0.0 && d_front < 1.0) || (d_back < 0.0 && d_back > -1.0)) {
            (*epsilonbrush)++;
        }
    }

    return s;
}

//========================================================

//============================================================================

/*
==================
LeafNode

Creates a leaf node.

Called in parallel.
==================
*/
static void LeafNode(node_t *leafnode, bspbrush_t::container brushes, bspstats_t &stats)
{
    leafnode->facelist.clear();
    leafnode->is_leaf = true;

    leafnode->contents = qbsp_options.target_game->create_empty_contents();
    for (auto &brush : brushes) {
        leafnode->contents = qbsp_options.target_game->combine_contents(leafnode->contents, brush->contents);
    }
    for (auto &brush : brushes) {
        leafnode->original_brushes.push_back(brush->original_brush());
    }

    qbsp_options.target_game->count_contents_in_stats(leafnode->contents, *stats.leafstats);

    if (qbsp_options.debugleak.value() || qbsp_options.debugbspbrushes.value()) {
        leafnode->bsp_brushes = brushes;
    } else {
        leafnode->volume.reset();
    }
}

//============================================================

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
================
SplitBrush

Note, it's useful to take/return std::unique_ptr so it can quickly return the
input.

https://github.com/id-Software/Quake-2-Tools/blob/master/bsp/qbsp3/brushbsp.c#L935
================
*/
static twosided<bspbrush_t::ptr> SplitBrush(
    bspbrush_t::ptr brush, size_t planenum, std::optional<std::reference_wrapper<bspstats_t>> stats)
{
    const qplane3d &split = map.planes[planenum];
    twosided<bspbrush_t::ptr> result;

    // check all points
    vec_t d_front = 0; // for points above plane, greatest distance from plane (positive)
    vec_t d_back = 0; // for points below plane, greatest distance from plane (negative)
    for (auto &face : brush->sides) {
        for (const qvec3d &p : face.w) {
            vec_t d = split.distance_to(p);
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
    std::optional<winding_t> w = BaseWindingForPlane<winding_t>(split);

    for (auto &face : brush->sides) {
        if (!w) {
            break;
        }
        w = w->clip_back(face.get_plane());
    }

    if (!w || WindingIsTiny(*w, 0.02)) { // the brush isn't really split
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

    winding_t &midwinding = *w;

    // split it for real

    // start with 2 empty brushes

    for (int i = 0; i < 2; i++) {
        result[i] = bspbrush_t::make_ptr();
        result[i]->original_ptr = brush->original_ptr ? brush->original_ptr : brush;
        result[i]->mapbrush = brush->mapbrush;
        // fixme-brushbsp: add a bspbrush_t copy constructor to make sure we get all fields
        result[i]->contents = brush->contents;
        result[i]->sides.reserve(brush->sides.size() + 1);
    }

    // split all the current windings

    for (const auto &face : brush->sides) {
        auto cw = face.w.clip(split, 0 /*PLANESIDE_EPSILON*/);
        for (size_t j = 0; j < 2; j++) {
            if (!cw[j]) {
                continue;
            }

            // add the clipped face to result[j]
            side_t &faceCopy = result[j]->sides.emplace_back(face.clone_non_winding_data());
            faceCopy.w = std::move(*cw[j]);
            faceCopy.tested = false;
            // fixme-brushbsp: configure any settings on the faceCopy?
        }
    }

    // see if we have valid polygons on both sides

    for (int i = 0; i < 2; i++) {
        bool bogus = false;

        if (result[i]->sides.size() < 3) {
            bogus = true;
        } else if (!result[i]->update_bounds(false)) {
            if (stats) {
                stats->get().c_bogus++;
            }
            bogus = true;
        } else {
            for (int j = 0; j < 3; j++) {
                if (result[i]->bounds.mins()[j] < -qbsp_options.worldextent.value() ||
                    result[i]->bounds.maxs()[j] > qbsp_options.worldextent.value()) {
                    if (stats) {
                        stats->get().c_bogus++;
                    }
                    bogus = true;
                    break;
                }
            }
        }

        if (bogus) {
            result[i] = nullptr;
        }
    }

    if (!result[0] && !result[1]) {
        if (stats) {
            stats->get().c_brushesremoved++;
        }

        return result;
    } else if (!result[0] || !result[1]) {
        if (stats) {
            stats->get().c_brushesonesided++;
        }

        if (result[0]) {
            result.front = std::move(brush);
        } else {
            result.back = std::move(brush);
        }

        return result;
    }

    // add the midwinding to both sides
    for (int i = 0; i < 2; i++) {
        side_t &cs = result[i]->sides.emplace_back();

        const bool brushOnFront = (i == 0);

        // for the brush on the front side of the plane, the `midwinding`
        // (the face that is touching the plane) should have a normal opposite the plane's normal
        cs.planenum = planenum ^ i ^ 1;
        cs.texinfo = map.skip_texinfo;
        cs.tested = false;
        cs.onnode = true;
        Q_assert(!cs.is_visible());

        if (brushOnFront) {
            cs.w = midwinding.flip();
        } else {
            cs.w = std::move(midwinding);
        }
    }

    for (int i = 0; i < 2; i++) {
        vec_t v1 = BrushVolume(*result[i]);
        if (v1 < qbsp_options.microvolume.value()) {
            result[i] = nullptr;
            if (stats) {
                stats->get().c_tinyvolumes++;
            }
        }
    }

    return result;
}

inline void CheckPlaneAgainstParents(size_t planenum, node_t *node)
{
    for (node_t *p = node->parent; p; p = p->parent) {
        if (p->planenum == planenum) {
            Error("Tried parent");
        }
    }
}

using stack_winding_storage_t = polylib::winding_storage_hybrid_t<polylib::STACK_POINTS_ON_WINDING>;
using stack_winding_t = polylib::winding_base_t<stack_winding_storage_t>;

struct stack_side_t
{
    stack_winding_t w;
    size_t planenum;

    inline const qbsp_plane_t &get_plane() const { return map.planes[planenum]; }
};

struct stack_brush_t
{
    aabb3d bounds;
    size_t num_sides;

    // stack allocated
    stack_side_t *sides;

    ~stack_brush_t()
    {
        for (size_t i = 0; i < num_sides; i++) {
            sides[i].~stack_side_t();
        }
    }

    inline bool update_bounds()
    {
        this->bounds = {};

        for (size_t i = 0; i < num_sides; i++) {
            if (sides[i].w) {
                this->bounds.unionWith_in_place(sides[i].w.bounds());
            }
        }

        for (size_t i = 0; i < 3; i++) {
            // todo: map_source_location in bspbrush_t
            if (this->bounds.mins()[0] <= -qbsp_options.worldextent.value() ||
                this->bounds.maxs()[0] >= qbsp_options.worldextent.value()) {
                return false;
            }
            if (this->bounds.mins()[0] >= qbsp_options.worldextent.value() ||
                this->bounds.maxs()[0] <= -qbsp_options.worldextent.value()) {
                return false;
            }
        }

        return true;
    }
};

/*
================
CheckSplitBrush

A special, low-allocation version of SplitBrush. Checks if a brush split
by the specified plane would result in two valid brushes.
================
*/
static bool CheckSplitBrush(const bspbrush_t::ptr &brush, size_t planenum)
{
    const qplane3d &split = map.planes[planenum];

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
    {
        return false;
    }
    if (d_back > -0.1) // PLANESIDE_EPSILON)
    { // only on front
        return false;
    }

    // create a new winding from the split plane
    std::optional<stack_winding_t> w = BaseWindingForPlane<stack_winding_t>(split);

    for (auto &face : brush->sides) {
        if (!w) {
            return false;
        }
        w = w->clip_back(face.get_plane());
    }

    if (!w || WindingIsTiny(*w, 0.02)) { // the brush isn't really split
        return false;
    }

    stack_winding_t &midwinding = *w;

    // split it for real

    // start with 2 empty brushes
    twosided<stack_brush_t> temporary_brushes;

    for (int i = 0; i < 2; i++) {
        temporary_brushes[i].sides = (stack_side_t *)alloca(sizeof(stack_side_t) * (brush->sides.size() + 1));
        temporary_brushes[i].num_sides = 0;
    }

    // split all the current windings

    for (const auto &face : brush->sides) {
        auto cw = face.w.clip<stack_winding_storage_t>(split, 0 /*PLANESIDE_EPSILON*/);

        for (size_t j = 0; j < 2; j++) {
            if (!cw[j]) {
                continue;
            }

            // add the clipped face to result[j]
            stack_side_t &faceCopy = temporary_brushes[j].sides[temporary_brushes[j].num_sides++];
            new (&faceCopy) stack_side_t;
            faceCopy.planenum = face.planenum;
            faceCopy.w = std::move(*cw[j]);
        }
    }

    // see if we have valid polygons on both sides

    for (int i = 0; i < 2; i++) {
        if (temporary_brushes[i].num_sides < 3) {
            return false;
        }

        if (!temporary_brushes[i].update_bounds()) {
            return false;
        }

        for (int j = 0; j < 3; j++) {
            if (temporary_brushes[i].bounds.mins()[j] < -qbsp_options.worldextent.value() ||
                temporary_brushes[i].bounds.maxs()[j] > qbsp_options.worldextent.value()) {
                return false;
            }
        }
    }

    // add the midwinding to both sides
    // FIXME: check if we can remove this/if BrushVolume below
    // will have the same result either way
    for (int i = 0; i < 2; i++) {
        stack_side_t &cs = temporary_brushes[i].sides[temporary_brushes[i].num_sides++];
        new (&cs) stack_side_t;

        const bool brushOnFront = (i == 0);

        cs.planenum = planenum ^ i ^ 1;

        if (brushOnFront) {
            cs.w = midwinding.flip();
        } else {
            cs.w = std::move(midwinding);
        }
    }

    for (int i = 0; i < 2; i++) {
        vec_t v1 = BrushVolume(temporary_brushes[i].sides, temporary_brushes[i].sides + temporary_brushes[i].num_sides);
        if (v1 < qbsp_options.microvolume.value()) {
            return false;
        }
    }

    return true;
}

inline bool CheckPlaneAgainstVolume(size_t planenum, const node_t *node)
{
    if (!node->volume)
        return false;

    bool valid = CheckSplitBrush(node->volume, planenum);
#ifdef PARANOID
    auto [front, back] = SplitBrush(node->volume, planenum, std::nullopt);
    Q_assert(valid == (front && back));
#endif
    return valid;
}

/*
 * Split a bounding box by a plane; The front and back bounds returned
 * are such that they completely contain the portion of the input box
 * on that side of the plane. Therefore, if the split plane is
 * non-axial, then the returned bounds will overlap.
 */
inline void DivideBounds(const aabb3d &in_bounds, const qbsp_plane_t &split, aabb3d &front_bounds, aabb3d &back_bounds)
{
    int a, b, c, i, j;
    vec_t dist1, dist2, mid, split_mins, split_maxs;
    qvec3d corner;

    front_bounds = back_bounds = in_bounds;

    if (split.get_type() < plane_type_t::PLANE_ANYX) {
        front_bounds[0][static_cast<size_t>(split.get_type())] = back_bounds[1][static_cast<size_t>(split.get_type())] =
            split.get_dist();
        return;
    }

    /* Make proper sloping cuts... */
    for (a = 0; a < 3; ++a) {
        /* Check for parallel case... no intersection */
        if (fabs(split.get_normal()[a]) < NORMAL_EPSILON)
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

                split_mins = std::max(std::min(mid, split_mins), in_bounds.mins()[a]);
                split_maxs = std::min(std::max(mid, split_maxs), in_bounds.maxs()[a]);
            }
        }
        if (split.get_normal()[a] > 0) {
            front_bounds[0][a] = split_mins;
            back_bounds[1][a] = split_maxs;
        } else {
            back_bounds[0][a] = split_mins;
            front_bounds[1][a] = split_maxs;
        }
    }
}

inline vec_t SplitPlaneMetric(const qbsp_plane_t &p, const aabb3d &bounds)
{
    aabb3d f, b;

    DivideBounds(bounds, p, f, b);

    // i.e. a good split will have equal volumes on front and back.
    // a bad split will have all of the volume on one side.
    return fabs(f.volume() - b.volume());
}

/*
==================
ChooseMidPlaneFromList

The clipping hull BSP doesn't worry about avoiding splits
==================
*/
static side_t *ChooseMidPlaneFromList(const bspbrush_t::container &brushes, const node_t *node)
{
    vec_t bestaxialmetric = VECT_MAX;
    side_t *bestaxialplane = nullptr;

    vec_t bestanymetric = VECT_MAX;
    side_t *bestanyplane = nullptr;

    for (auto &brush : brushes) {
        for (auto &side : brush->sides) {
            if (side.bevel) {
                continue; // never use a bevel as a spliter
            }
            if (side.onnode) {
                continue; // allready a node splitter
            }

            size_t positive_planenum = side.planenum & ~1;
            const qbsp_plane_t &plane = side.get_positive_plane();

#if CHECK_PLANE_AGAINST_VOLUME
            if (!CheckPlaneAgainstVolume(positive_planenum, node)) {
                continue; // would produce a tiny volume
            }
#endif

            /* calculate the split metric, smaller values are better */
            const vec_t metric = SplitPlaneMetric(plane, node->bounds);

            if (metric < bestanymetric) {
                bestanymetric = metric;
                bestanyplane = &side;
            }

            /* check for axis aligned surfaces */
            if (plane.get_type() < plane_type_t::PLANE_ANYX) {
                if (metric < bestaxialmetric) {
                    bestaxialmetric = metric;
                    bestaxialplane = &side;
                }
            }
        }
    }

    // prefer the axial split
    return bestaxialplane ? bestaxialplane : bestanyplane;
}

/*
================
SelectSplitPlane

Using heuristics, chooses a plane to partition the brushes with.
Returns nullopt if there are no valid planes to split with.
================
*/
static side_t *SelectSplitPlane(
    const bspbrush_t::container &brushes, node_t *node, tree_split_t split_type, bspstats_t &stats)
{
    // no brushes left to split, so we can't use any plane.
    if (!brushes.size()) {
        return nullptr;
    }

    if (split_type != tree_split_t::PRECISE) {
        if (split_type == tree_split_t::AUTO) {

            // decide if we should switch to the midsplit method
            if (qbsp_options.midsplitbrushfraction.value() != 0.0) {
                // new way (opt-in)
                // how much of the map are we partitioning?
                double fractionOfMap = brushes.size() / (double)map.total_brushes;
                if (fractionOfMap > qbsp_options.midsplitbrushfraction.value()) {
                    split_type = tree_split_t::FAST;
                }
            } else {
                // old way (ericw-tools 0.15.2+)
                if (qbsp_options.maxnodesize.value() >= 64) {
                    const vec_t maxnodesize = qbsp_options.maxnodesize.value() - qbsp_options.epsilon.value();

                    if ((node->bounds.maxs()[0] - node->bounds.mins()[0]) > maxnodesize ||
                        (node->bounds.maxs()[1] - node->bounds.mins()[1]) > maxnodesize ||
                        (node->bounds.maxs()[2] - node->bounds.mins()[2]) > maxnodesize) {
                        split_type = tree_split_t::FAST;
                    }
                }
            }
        }

        if (split_type == tree_split_t::FAST) {
            if (auto mid_plane = ChooseMidPlaneFromList(brushes, node)) {
                stats.c_midsplit++;

                for (auto &b : brushes) {
                    b->side = TestBrushToPlanenum(*b, mid_plane->planenum & ~1, nullptr, nullptr, nullptr);
                }

                return mid_plane;
            }
        }
    }

    side_t *bestside = nullptr;
    int bestvalue = -99999;

    // the search order goes: (changed from q2 tools - see q2_detail_leak_test.map for the issue
    // with the vanilla q2 tools method):
    //
    //   0. visible-structural
    //   1. nonvisible-structural,
    //   2. visible-detail
    //   3. nonvisible-detail.
    //
    // If any valid plane is available in a pass, no further
    // passes will be tried.
    constexpr int numpasses = 4;
    for (int pass = 0; pass < numpasses; pass++) {
        for (auto &brush : brushes) {
            if ((pass >= 2) != brush->contents.is_any_detail(qbsp_options.target_game))
                continue;
            for (auto &side : brush->sides) {
                if (side.bevel)
                    continue; // never use a bevel as a spliter
                if (!side.w)
                    continue; // nothing visible, so it can't split
                if (side.onnode)
                    continue; // allready a node splitter
                if (side.tested)
                    continue; // we allready have metrics for this plane
                if (side.get_texinfo().flags.is_hintskip)
                    continue; // skip surfaces are never chosen
                if (side.is_visible() != (pass == 0 || pass == 2))
                    continue; // only check visible faces on pass 0/2

                size_t positive_planenum = side.planenum & ~1;
                const qbsp_plane_t &plane = side.get_positive_plane(); // always use positive facing plane

                CheckPlaneAgainstParents(positive_planenum, node);

#if CHECK_PLANE_AGAINST_VOLUME
                if (!CheckPlaneAgainstVolume(positive_planenum, node))
                    continue; // would produce a tiny volume
#endif

                int front = 0;
                int back = 0;
                int facing = 0;
                int splits = 0;
                int epsilonbrush = 0;
                bool hintsplit = false;

                for (auto &test : brushes) {
                    int bsplits;
                    int s = TestBrushToPlanenum(*test, positive_planenum, &bsplits, &hintsplit, &epsilonbrush);

                    splits += bsplits;
                    if (bsplits && (s & PSIDE_FACING))
                        Error("PSIDE_FACING with splits");

                    test->testside = s;
                    // if the brush shares this face, don't bother
                    // testing that facenum as a splitter again
                    if (s & PSIDE_FACING) {
                        facing++;
                        for (auto &testside : test->sides) {
                            if ((testside.planenum & ~1) == (side.planenum & ~1)) {
                                testside.tested = true;
                            }
                        }
                    }
                    if (s & PSIDE_FRONT)
                        front++;
                    if (s & PSIDE_BACK)
                        back++;
                }

                // give a value estimate for using this plane

                int value = 5 * facing - 5 * splits - std::abs(front - back);
                //					value =  -5*splits;
                //					value =  5*facing - 5*splits;
                if (plane.get_type() < plane_type_t::PLANE_ANYX)
                    value += 5; // axial is better
                value -= epsilonbrush * 1000; // avoid!

                // never split a hint side except with another hint
                if (hintsplit && !(side.get_texinfo().flags.is_hint))
                    value = -9999999;

                // save off the side test so we don't need
                // to recalculate it when we actually seperate
                // the brushes
                if (value > bestvalue) {
                    bestvalue = value;
                    bestside = &side;
                    for (auto &test : brushes) {
                        test->side = test->testside;
                    }
                }
            }
        }

        // if we found a good plane, don't bother trying any
        // other passes
        if (bestside) {
            if (pass >= 2)
                node->detail_separator = true; // not needed for vis
            break;
        }
    }

    //
    // clear all the tested flags we set
    //
    for (auto &brush : brushes) {
        for (auto &side : brush->sides) {
            side.tested = false;
        }
    }

    if (!bestside) {
        return nullptr;
    }

    if (!bestside->is_visible()) {
        stats.c_nonvis++;
    }

    stats.c_qbsp3++;

    return bestside;
}

/*
================
SplitBrushList
================
*/
static std::array<bspbrush_t::container, 2> SplitBrushList(
    bspbrush_t::container brushes, size_t planenum, bspstats_t &stats)
{
    std::array<bspbrush_t::container, 2> result;

    for (auto &brush : brushes) {
        int sides = brush->side;

        if (sides == PSIDE_BOTH) {
            // split into two brushes (destructively)
            auto [front, back] = SplitBrush(std::move(brush), planenum, stats);

            if (front) {
                result[0].push_back(std::move(front));
            }

            if (back) {
                result[1].push_back(std::move(back));
            }
            continue;
        }

        // if the planenum is actually a part of the brush
        // find the plane and flag it as used so it won't be tried
        // as a splitter again
        if (sides & PSIDE_FACING) {
            for (auto &side : brush->sides) {
                if ((side.planenum & ~1) == planenum) {
                    side.onnode = true;
                }
            }
        }

        if (sides & PSIDE_FRONT) {
            result[0].push_back(std::move(brush));
            continue;
        }
        if (sides & PSIDE_BACK) {
            result[1].push_back(std::move(brush));
            continue;
        }
    }

    return result;
}

/*
==================
BuildTree_r

Called in parallel.
==================
*/
static void BuildTree_r(tree_t &tree, int level, node_t *node, bspbrush_t::container brushes, tree_split_t split_type,
    bspstats_t &stats, logging::percent_clock &clock)
{
    // find the best plane to use as a splitter
    auto *bestside = SelectSplitPlane(brushes, node, split_type, stats);

    if (!bestside) {
        // this is a leaf node
        clock();

        node->is_leaf = true;

        stats.c_leafs++;
        LeafNode(node, std::move(brushes), stats);

        return;
    }

    clock();

    // this is a splitplane node
    stats.c_nodes++;

    // make sure this was a positive-facing split
    size_t bestplane = bestside->planenum & ~1;
    Q_assert(!(bestplane & 1));

    node->planenum = bestplane;

    auto &plane = map.get_plane(bestplane);
    auto children = SplitBrushList(std::move(brushes), bestplane, stats);

    // allocate children before recursing
    for (int i = 0; i < 2; i++) {
        auto &newnode = node->children[i] = tree.create_node();
        newnode->parent = node;
        newnode->bounds = node->bounds;
    }

    for (int i = 0; i < 3; i++) {
        if (plane.get_normal()[i] == 1.0) {
            node->children[0]->bounds[0][i] = plane.get_dist();
            node->children[1]->bounds[1][i] = plane.get_dist();
            break;
        }
    }

    // to save time/memory we can destroy node's volume at this point
    if (node->volume) {
        auto children_volumes = SplitBrush(std::move(node->volume), bestplane, stats);
        node->volume = nullptr;
        node->children[0]->volume = std::move(children_volumes[0]);
        node->children[1]->volume = std::move(children_volumes[1]);
    }

    // recursively process children
    tbb::task_group g;
    g.run([&]() { BuildTree_r(tree, level + 1, node->children[0], std::move(children[0]), split_type, stats, clock); });
    g.run([&]() { BuildTree_r(tree, level + 1, node->children[1], std::move(children[1]), split_type, stats, clock); });
    g.wait();
}

struct brushbsp_input_stats_t : logging::stat_tracker_t
{
    stat &brushes = register_stat("brushes");
    stat &faces = register_stat("visible faces");
    stat &nonvis_faces = register_stat("non-visible faces");
    stat &clip_faces = register_stat("clip faces");
};

/*
==================
BrushBSP
==================
*/
void BrushBSP(tree_t &tree, mapentity_t &entity, const bspbrush_t::container &brushlist, tree_split_t split_type)
{
    logging::header(__func__);

    if (brushlist.empty()) {
        /*
         * We allow an entity to be constructed with no visible brushes
         * (i.e. all clip brushes), but need to construct a simple empty
         * collision hull for the engine. Probably could be done a little
         * smarter, but this works.
         */
        auto headnode = tree.create_node();
        headnode->bounds = entity.bounds;
        // The choice of plane is mostly unimportant, but having it at (0, 0, 0) affects
        // the node bounds calculation.
        headnode->planenum = 0;
        headnode->children[0] = tree.create_node();
        headnode->children[0]->is_leaf = true;
        headnode->children[0]->contents = qbsp_options.target_game->create_empty_contents();
        headnode->children[0]->parent = headnode;
        headnode->children[1] = tree.create_node();
        headnode->children[1]->is_leaf = true;
        headnode->children[1]->contents = qbsp_options.target_game->create_empty_contents();
        headnode->children[1]->parent = headnode;

        tree.bounds = headnode->bounds;
        tree.headnode = headnode;

        return;
    }

    {
        brushbsp_input_stats_t stats;
        stats.brushes += brushlist.size();

        for (const auto &b : brushlist) {

#if 0
            // fixme-brushbsp: why does this just print and do nothing? should
            // the brush be removed?
            double volume = BrushVolume(*b);
            if (volume < qbsp_options.microvolume.value()) {
                logging::print("WARNING: {}: microbrush\n",
                    b->mapbrush->line);
            }
#endif

            for (side_t &side : b->sides) {
                // since we're reusing bspbrush_t's across passes, we need to clear any data from the previous pass

                // behaviour break from qbsp3 - they would sometimes set `onnode` as a way to indicate "don't split on
                // this side". we can't do this since we're reusing brushes, and need to add a separate flag for that.
                side.onnode = false;

                if (side.bevel)
                    continue;
                if (!side.w)
                    continue;
                if (side.is_visible()) {
                    stats.faces++;
                } else {
                    stats.nonvis_faces++;
                }
            }

            tree.bounds += b->bounds;
        }
    }

    auto node = tree.create_node();

    node->bounds = tree.bounds.grow(SIDESPACE);
    node->volume = BrushFromBounds(node->bounds);

    tree.headnode = node;

    bspstats_t stats{};
    stats.leafstats = qbsp_options.target_game->create_content_stats();

    {
        logging::percent_clock clock;
        BuildTree_r(tree, 0, tree.headnode, brushlist, split_type, stats, clock);
    }

    stats.print_stats();

    CountLeafs(tree.headnode);
}

/*
==================
BrushGE

Returns true if b1 is allowed to bite b2
==================
*/
inline bool BrushGE(const bspbrush_t &b1, const bspbrush_t &b2)
{
    // detail brushes never bite structural brushes
    if ((b1.contents.is_any_detail(qbsp_options.target_game)) &&
        !(b2.contents.is_any_detail(qbsp_options.target_game))) {
        return false;
    }
    return b1.contents.is_any_solid(qbsp_options.target_game) && b2.contents.is_any_solid(qbsp_options.target_game);
}

/*
===============
BrushesDisjoint

Returns true if the two brushes definately do not intersect.
There will be false negatives for some non-axial combinations.
===============
*/
inline bool BrushesDisjoint(const bspbrush_t &a, const bspbrush_t &b)
{
    if (a.bounds.disjoint_or_touching(b.bounds)) {
        // bounding boxes don't overlap
        return true;
    }

    // check for opposing planes
    for (auto &as : a.sides) {
        for (auto &bs : b.sides) {
            if (as.planenum == (bs.planenum ^ 1)) {
                // opposite planes, so not touching
                return true;
            }
        }
    }

    return false; // might intersect
}

/*
===============
SubtractBrush

Returns a list of brushes that remain after B is subtracted from A.
May by empty if A is contained inside B.

The originals are undisturbed.
===============
*/
inline bspbrush_t::list SubtractBrush(const bspbrush_t::ptr &a, const bspbrush_t::ptr &b)
{
    bspbrush_t::list out;
    bspbrush_t::ptr in = a;

    for (auto &side : b->sides) {
        auto [front, back] = SplitBrush(in, side.planenum, std::nullopt);

        if (front) {
            // add to list
            out.push_front(front);
        }

        in = back;

        if (!in) {
            // didn't really intersect
            return {a};
        }
    }

    return out;
}

struct chopstats_t : logging::stat_tracker_t
{
    stat &c_swallowed = register_stat("brushes swallowed");
    stat &c_from_split = register_stat("brushes created from the chompening");
};

/*
=================
ChopBrushes

Carves any intersecting solid brushes into the minimum number
of non-intersecting brushes.

Modifies the input list and may free destroyed brushes.
=================
*/
void ChopBrushes(bspbrush_t::container &brushes, bool allow_fragmentation)
{
    size_t original_count = brushes.size();
    logging::funcheader();

    // convert brush container to list, so we don't lose
    // track of the original ptrs and so we can re-organize things
    bspbrush_t::list list{std::make_move_iterator(brushes.begin()), std::make_move_iterator(brushes.end())};

    // clear original list
    brushes.clear();

    logging::percent_clock clock(list.size());
    chopstats_t stats;

    decltype(list)::iterator b1_it = list.begin();

newlist:

    if (!list.size()) {
        // clear output since this is kind of an error...
        brushes.clear();
        return;
    }

    decltype(list)::iterator next;

    for (; b1_it != list.end(); b1_it = next) {
        clock.max = list.size();
        next = std::next(b1_it);

        auto &b1 = *b1_it;

        if (b1->mapbrush->no_chop) {
            continue;
        }

        for (auto b2_it = next; b2_it != list.end(); b2_it++) {
            auto &b2 = *b2_it;

            if (b2->mapbrush->no_chop) {
                continue;
            }

            if (BrushesDisjoint(*b1, *b2)) {
                continue;
            }

            bspbrush_t::list sub, sub2;
            size_t c1 = std::numeric_limits<size_t>::max(), c2 = c1;

            if (BrushGE(*b2, *b1)) {
                sub = SubtractBrush(b1, b2);
                if (sub.size() == 1 && sub.front() == b1) {
                    continue; // didn't really intersect
                }

                if (sub.empty()) { // b1 is swallowed by b2
                    b1_it = list.erase(b1_it); // continue after b1_it
                    stats.c_swallowed++;
                    goto newlist;
                }
                c1 = sub.size();
            }

            if (BrushGE(*b1, *b2)) {
                sub2 = SubtractBrush(b2, b1);
                if (sub2.size() == 1 && sub2.front() == b2) {
                    continue; // didn't really intersect
                }
                if (sub2.empty()) { // b2 is swallowed by b1
                    list.erase(b2_it);
                    // continue where b1_it was
                    stats.c_swallowed++;
                    goto newlist;
                }
                c2 = sub2.size();
            }

            if (sub.empty() && sub2.empty()) {
                continue; // neither one can bite
            }

            // only accept if it didn't fragment
            if (!allow_fragmentation && c1 > 1 && c2 > 1) {
                continue;
            }

            if (c1 < c2) {
                stats.c_from_split += sub.size();
                auto before = list.erase(b1_it); // remove the current brush, go back one
                list.splice(before, sub); // splice new list in place of where the brush was
                b1_it = before; // restart list with the new brushes
                goto newlist;
            } else {
                stats.c_from_split += sub2.size();
                list.splice(b2_it, sub2); // splice new brushes before b2_it
                list.erase(b2_it); // remove b2_it
                // continue where b1_it left off
                goto newlist;
            }
        }

        clock();
    }

    // since chopbrushes can remove stuff, exact counts are hard...
    clock.max = list.size();
    clock.print();

    brushes.insert(brushes.begin(), std::make_move_iterator(list.begin()), std::make_move_iterator(list.end()));
    logging::print(logging::flag::STAT, "chopped {} brushes into {}\n", original_count, brushes.size());

    if (qbsp_options.debugchop.value()) {
        WriteBspBrushMap("chopped", brushes);
    }
}