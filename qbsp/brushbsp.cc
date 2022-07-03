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

// if a brush just barely pokes onto the other side,
// let it slide by without chopping
constexpr double PLANESIDE_EPSILON = 0.001;
//0.1

constexpr int PSIDE_FRONT = 1;
constexpr int PSIDE_BACK = 2;
constexpr int PSIDE_BOTH = (PSIDE_FRONT|PSIDE_BACK);
// this gets OR'ed in in the return value of QuickTestBrushToPlanenum if one of the brush sides is on the input plane
constexpr int PSIDE_FACING = 4;

struct bspstats_t {
    std::any leafstats;
    // total number of nodes, includes c_nonvis
    std::atomic<int> c_nodes;
    // number of nodes created by splitting on a side_t which had !visible
    std::atomic<int> c_nonvis;
    // total number of leafs
    std::atomic<int> c_leafs;
};

static twosided<std::unique_ptr<bspbrush_t>> SplitBrush(std::unique_ptr<bspbrush_t> brush, int planenum);

/*
==================
CreateBrushWindings

Currently only used in BrushFromBounds
==================
*/
static void CreateBrushWindings(bspbrush_t *brush)
{
    std::optional<winding_t> w;

    for (int i = 0; i < brush->sides.size(); i++) {
        side_t *side = &brush->sides[i];
        w = BaseWindingForPlane(Face_Plane(side));
        for (int j = 0; j < brush->sides.size() && w; j++) {
            if (i == j)
                continue;
            if (brush->sides[j].bevel)
                continue;
            qplane3d plane = -Face_Plane(&brush->sides[j]);
            w = w->clip(plane, 0, false)[SIDE_FRONT]; // CLIP_EPSILON);
        }

        if (w) {
            side->w = *w;
        } else {
            side->w.clear();
        }
    }

    brush->update_bounds();
}

/*
==================
BrushFromBounds

Creates a new axial brush
==================
*/
std::unique_ptr<bspbrush_t> BrushFromBounds(const aabb3d &bounds)
{
    auto b = std::unique_ptr<bspbrush_t>(new bspbrush_t{});

    b->sides.resize(6);
    for (int i = 0; i < 3; i++)
    {
        {
            qplane3d plane{};
            plane.normal[i] = 1;
            plane.dist = bounds.maxs()[i];

            side_t &side = b->sides[i];
            side.planenum = FindPlane(plane, &side.planeside);
        }

        {
            qplane3d plane{};
            plane.normal[i] = -1;
            plane.dist = -bounds.mins()[i];

            side_t &side = b->sides[3 + i];
            side.planenum = FindPlane(plane, &side.planeside);
        }
    }

    CreateBrushWindings(b.get());

    return b;
}

/*
==================
BrushVolume

==================
*/
static vec_t BrushVolume(const bspbrush_t &brush)
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
    for (auto &side : brush.sides) {
        if (!side.w.size()) {
            continue;
        }
        auto plane = Face_Plane(&side);
        vec_t d = -(qv::dot(corner, plane.normal) - plane.dist);
        vec_t area = side.w.area();
        volume += d * area;
    }

    volume /= 3;
    return volume;
}

//========================================================

/*
==============
BoxOnPlaneSide

Returns PSIDE_FRONT, PSIDE_BACK, or PSIDE_BOTH
==============
*/
static int BoxOnPlaneSide(const aabb3d& bounds, const qbsp_plane_t &plane)
{
    // axial planes are easy
    if (static_cast<int>(plane.type) < 3)
    {
        int side = 0;
        if (bounds.maxs()[static_cast<int>(plane.type)] > plane.dist + PLANESIDE_EPSILON)
            side |= PSIDE_FRONT;
        if (bounds.mins()[static_cast<int>(plane.type)] < plane.dist - PLANESIDE_EPSILON)
            side |= PSIDE_BACK;
        return side;
    }

    // create the proper leading and trailing verts for the box
    std::array<qvec3d, 2> corners;
    for (int i = 0; i < 3; i++)
    {
        if (plane.normal[i] < 0)
        {
            corners[0][i] = bounds.mins()[i];
            corners[1][i] = bounds.maxs()[i];
        }
        else
        {
            corners[1][i] = bounds.mins()[i];
            corners[0][i] = bounds.maxs()[i];
        }
    }

    double dist1 = qv::dot(plane.normal, corners[0]) - plane.dist;
    double dist2 = qv::dot(plane.normal, corners[1]) - plane.dist;
    int side = 0;
    if (dist1 >= PLANESIDE_EPSILON)
        side = PSIDE_FRONT;
    if (dist2 < PLANESIDE_EPSILON)
        side |= PSIDE_BACK;

    return side;
}

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
        int num = side.planenum;
        if (num == planenum && side.planeside == SIDE_FRONT)
            return PSIDE_BACK|PSIDE_FACING;
        if (num == planenum && side.planeside == SIDE_BACK)
            return PSIDE_FRONT|PSIDE_FACING;
    }

    // box on plane side
    const auto lock = std::lock_guard(map_planes_lock);
    auto plane = map.planes[planenum];
    int s = BoxOnPlaneSide(brush.bounds, plane);

    // if both sides, count the visible faces split
    if (s == PSIDE_BOTH)
    {
        *numsplits += 3;
    }

    return s;
}

/*
============
TestBrushToPlanenum

============
*/
static int TestBrushToPlanenum(const bspbrush_t &brush, int planenum, int *numsplits, bool *hintsplit, int *epsilonbrush)
{
    *numsplits = 0;
    *hintsplit = false;

    // if the brush actually uses the planenum,
    // we can tell the side for sure
    for (auto &side : brush.sides) {
        int num = side.planenum;
        if (num == planenum && side.planeside == SIDE_FRONT) {
            return PSIDE_BACK | PSIDE_FACING;
        }
        if (num == planenum && side.planeside == SIDE_BACK) {
            return PSIDE_FRONT | PSIDE_FACING;
        }
    }

    // box on plane side
    qbsp_plane_t plane;
    {
        const auto lock = std::lock_guard(map_planes_lock);
        plane = map.planes[planenum];
    }
    int s = BoxOnPlaneSide(brush.bounds, plane);

    if (s != PSIDE_BOTH)
        return s;

    // if both sides, count the visible faces split
    vec_t d_front = 0;
    vec_t d_back = 0;

    for (const side_t &side : brush.sides)
    {
        if (side.onnode)
            continue;		// on node, don't worry about splits
        if (!side.visible)
            continue;		// we don't care about non-visible
        auto &w = side.w;
        if (!w)
            continue;
        int front = 0;
        int back = 0;
        for (auto &point : w)
        {
            const double d = qv::dot(point, plane.normal) - plane.dist;
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

    if ( (d_front > 0.0 && d_front < 1.0)
        || (d_back < 0.0 && d_back > -1.0) )
        (*epsilonbrush)++;

    return s;
}

//========================================================

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

//============================================================================

/*
==================
LeafNode

Creates a leaf node.

Called in parallel.
==================
*/
static void LeafNode(node_t *leafnode, std::vector<std::unique_ptr<bspbrush_t>> brushes, bspstats_t &stats)
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

    options.target_game->count_contents_in_stats(leafnode->contents, stats.leafstats);
}

//============================================================

static void CheckPlaneAgainstParents(int pnum, node_t *node)
{
    for (node_t *p = node->parent; p; p = p->parent) {
        if (p->planenum == pnum) {
            Error("Tried parent");
        }
    }
}

static bool CheckPlaneAgainstVolume(int pnum, node_t *node)
{
    auto [front, back] = SplitBrush(node->volume->copy_unique(), pnum);

    bool good = (front && back);

    return good;
}

/*
================
SelectSplitSide

Using a hueristic, choses one of the sides out of the brushlist
to partition the brushes with.
Returns NULL if there are no valid planes to split with..
================
*/
side_t *SelectSplitSide(const std::vector<std::unique_ptr<bspbrush_t>>& brushes, node_t *node)
{
    side_t* bestside = nullptr;
    int bestvalue = -99999;
    int bestsplits = 0;

    // the search order goes: visible-structural, visible-detail,
    // nonvisible-structural, nonvisible-detail.
    // If any valid plane is available in a pass, no further
    // passes will be tried.
    constexpr int numpasses = 4;
    for (int pass = 0 ; pass < numpasses ; pass++) {
        for (auto &brush : brushes) {
            if ( (pass & 1) && !brush->original->contents.is_any_detail(options.target_game) )
                continue;
            if ( !(pass & 1) && brush->original->contents.is_any_detail(options.target_game) )
                continue;
            for (auto &side : brush->sides) {
                if (side.bevel)
                    continue;	// never use a bevel as a spliter
                if (!side.w)
                    continue;	// nothing visible, so it can't split
                if (side.onnode)
                    continue;	// allready a node splitter
                if (side.tested)
                    continue;	// we allready have metrics for this plane
                if (side.get_texinfo().flags.is_hintskip)
                    continue;	// skip surfaces are never chosen
                if ( side.visible ^ (pass<2) )
                    continue;	// only check visible faces on first pass

                int pnum = FindPositivePlane(side.planenum); // always use positive facing plane

                CheckPlaneAgainstParents (pnum, node);

                if (!CheckPlaneAgainstVolume (pnum, node))
                    continue;	// would produce a tiny volume

                int front = 0;
                int back = 0;
                int both = 0;
                int facing = 0;
                int splits = 0;
                int epsilonbrush = 0;
                bool hintsplit = false;

                for (auto &test : brushes)
                {
                    int bsplits;
                    int s = TestBrushToPlanenum(*test, pnum, &bsplits, &hintsplit, &epsilonbrush);

                    splits += bsplits;
                    if (bsplits && (s&PSIDE_FACING) )
                        Error ("PSIDE_FACING with splits");

                    test->testside = s;
                    // if the brush shares this face, don't bother
                    // testing that facenum as a splitter again
                    if (s & PSIDE_FACING)
                    {
                        facing++;
                        for (auto &testside : test->sides) {
                            if (testside.planenum == pnum) {
                                testside.tested = true;
                            }
                        }
                    }
                    if (s & PSIDE_FRONT)
                        front++;
                    if (s & PSIDE_BACK)
                        back++;
                    if (s == PSIDE_BOTH)
                        both++;
                }

                // give a value estimate for using this plane

                int value =  5*facing - 5*splits - abs(front-back);
                //					value =  -5*splits;
                //					value =  5*facing - 5*splits;
                if (static_cast<int>(map.get_plane(pnum).type) < 3)
                    value+=5;		// axial is better
                value -= epsilonbrush*1000;	// avoid!

                // never split a hint side except with another hint
                if (hintsplit && !(side.get_texinfo().flags.is_hint) )
                    value = -9999999;

                // save off the side test so we don't need
                // to recalculate it when we actually seperate
                // the brushes
                if (value > bestvalue) {
                    bestvalue = value;
                    bestside = &side;
                    bestsplits = splits;
                    for (auto &test : brushes) {
                        test->side = test->testside;
                    }
                }
            }
        }

        // if we found a good plane, don't bother trying any
        // other passes
        if (bestside) {
            if (pass > 0)
                node->detail_separator = true;	// not needed for vis
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

    return bestside;
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
================
SplitBrush

Note, it's useful to take/return std::unique_ptr so it can quickly return the
input.

https://github.com/id-Software/Quake-2-Tools/blob/master/bsp/qbsp3/brushbsp.c#L935
================
*/
static twosided<std::unique_ptr<bspbrush_t>> SplitBrush(std::unique_ptr<bspbrush_t> brush, int planenum)
{
    qplane3d split;
    {
        const auto lock = std::lock_guard(map_planes_lock);
        split = map.planes.at(planenum);
    }

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
        cs.visible = false;
        cs.tested = false;
        cs.onnode = true;
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

/*
================
SplitBrushList
================
*/
static std::array<std::vector<std::unique_ptr<bspbrush_t>>, 2> SplitBrushList(std::vector<std::unique_ptr<bspbrush_t>> brushes, const node_t *node)
{
    std::array<std::vector<std::unique_ptr<bspbrush_t>>, 2> result;

    for (auto& brush : brushes) {
        int sides = brush->side;

        if (sides == PSIDE_BOTH) {
            // split into two brushes
            auto [front, back] = SplitBrush(brush->copy_unique(), node->planenum);
            if (front)
            {
                result[0].push_back(std::move(front));
            }
            if (back)
            {
                result[1].push_back(std::move(back));
            }
            continue;
        }

        // if the planenum is actualy a part of the brush
        // find the plane and flag it as used so it won't be tried
        // as a splitter again
        if (sides & PSIDE_FACING) {
            for (auto &side : brush->sides) {
                if (side.planenum == node->planenum) {
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
static void BuildTree_r(node_t *node, std::vector<std::unique_ptr<bspbrush_t>> brushes, bspstats_t& stats)
{
    // find the best plane to use as a splitter
    auto *bestside = const_cast<side_t *>(SelectSplitSide(brushes, node));
    if (!bestside) {
        // this is a leaf node
        node->side = nullptr;
        node->planenum = PLANENUM_LEAF;

        stats.c_leafs++;
        LeafNode(node, std::move(brushes), stats);

        return;
    }

    // this is a splitplane node
    stats.c_nodes++;
    if (!bestside->visible) {
        stats.c_nonvis++;
    }

    node->side = bestside;
    node->planenum = FindPositivePlane(bestside->planenum);	// always use front facing

    auto children = SplitBrushList(std::move(brushes), node);

    // allocate children before recursing
    for (int i = 0; i < 2; i++)
    {
        auto* newnode = new node_t{};
        newnode->parent = node;
        node->children[i] = std::unique_ptr<node_t>(newnode);
    }

    auto children_volumes = SplitBrush(node->volume->copy_unique(), node->planenum);
    node->children[0]->volume = std::move(children_volumes[0]);
    node->children[1]->volume = std::move(children_volumes[1]);

    // recursively process children
    tbb::task_group g;
    g.run([&]() { BuildTree_r(node->children[0].get(), std::move(children[0]), stats); });
    g.run([&]() { BuildTree_r(node->children[1].get(), std::move(children[1]), stats); });
    g.wait();
}

/*
==================
BrushBSP
==================
*/
static tree_t *BrushBSP(mapentity_t *entity, std::vector<std::unique_ptr<bspbrush_t>> brushlist)
{
    auto *tree = new tree_t{};

    logging::print(logging::flag::PROGRESS, "---- {} ----\n", __func__);

    size_t c_faces = 0;
    size_t c_nonvisfaces = 0;
    size_t c_brushes = 0;
    for (const auto &b : brushlist)
    {
        c_brushes++;

        double volume = BrushVolume(*b);
        if (volume < options.microvolume.value())
        {
            logging::print("WARNING: microbrush");
            // fixme-brushbsp: add entitynum, brushnum in mapbrush_t
//            printf ("WARNING: entity %i, brush %i: microbrush\n",
//                b->original->entitynum, b->original->brushnum);
        }

        for (side_t &side : b->sides)
        {
            if (side.bevel)
                continue;
            if (!side.w)
                continue;
            if (side.onnode)
                continue;
            if (side.visible)
                c_faces++;
            else
                c_nonvisfaces++;
        }

        tree->bounds += b->bounds;
    }

    if (brushlist.empty()) {
        /*
         * We allow an entity to be constructed with no visible brushes
         * (i.e. all clip brushes), but need to construct a simple empty
         * collision hull for the engine. Probably could be done a little
         * smarter, but this works.
         */
        auto headnode = std::unique_ptr<node_t>(new node_t{});
        headnode->bounds = entity->bounds;
        headnode->children[0] = std::unique_ptr<node_t>(new node_t{});
        headnode->children[0]->planenum = PLANENUM_LEAF;
        headnode->children[0]->contents = options.target_game->create_empty_contents();
        headnode->children[0]->parent = headnode.get();
        headnode->children[1] = std::unique_ptr<node_t>(new node_t{});
        headnode->children[1]->planenum = PLANENUM_LEAF;
        headnode->children[1]->contents = options.target_game->create_empty_contents();
        headnode->children[1]->parent = headnode.get();

        tree->bounds = headnode->bounds;
        tree->headnode = std::move(headnode);

        return tree;
    }

    logging::print("{:5} brushes\n", c_brushes);
    logging::print("{:5} visible faces\n", c_faces);
    logging::print("{:5} nonvisible faces\n", c_nonvisfaces);


    auto node = std::unique_ptr<node_t>(new node_t{});

    node->volume = BrushFromBounds(tree->bounds.grow(SIDESPACE));

    tree->headnode = std::move(node);

    bspstats_t stats{};
    stats.leafstats = options.target_game->create_content_stats();
    BuildTree_r(tree->headnode.get(), std::move(brushlist), stats);

    logging::print("{:5} visible nodes\n", stats.c_nodes - stats.c_nonvis);
    logging::print("{:5} nonvis nodes\n", stats.c_nonvis);
    logging::print("{:5} leafs\n", stats.c_leafs);

    return tree;
}

tree_t *BrushBSP(mapentity_t *entity, bool midsplit)
{
    // set the original pointers
    std::vector<std::unique_ptr<bspbrush_t>> brushcopies;
    for (const auto &original : entity->brushes) {
        auto copy = original->copy_unique();
        copy->original = original.get();
        brushcopies.push_back(std::move(copy));
    }

    tree_t *tree = BrushBSP(entity, std::move(brushcopies));

    return tree;
}
