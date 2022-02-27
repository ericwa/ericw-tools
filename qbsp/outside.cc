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

#include <qbsp/qbsp.hh>

#include <climits>
#include <vector>
#include <set>
#include <list>
#include <unordered_set>
#include <utility>
#include <fstream>
#include <fmt/ostream.h>

/*
===========
PointInLeaf

If the point is exactly on a node plane, prefer to return the
opaque leaf.

This avoids spurious leaks if a point entity is on the outside
of the map (exactly on a brush faces) - happens in base1.map.
===========
*/
static node_t *PointInLeaf(node_t *node, const qvec3d &point)
{
    if (node->planenum == PLANENUM_LEAF || node->detail_separator) {
        return node;
    }

    auto &plane = map.planes[node->planenum];
    vec_t dist = plane.distance_to(point);

    if (dist > 0) {
        // point is on the front of the node plane
        return PointInLeaf(node->children[0], point);
    } else if (dist < 0) {
        // point is on the back of the node plane
        return PointInLeaf(node->children[1], point);
    } else {
        // point is exactly on the node plane

        node_t *front = PointInLeaf(node->children[0], point);
        node_t *back = PointInLeaf(node->children[1], point);

        // prefer the opaque one
        if (front->opaque()) {
            return front;
        }
        return back;
    }
}

static std::ofstream InitPtsFile(void)
{
    options.szBSPName.replace_extension("pts");

    std::ofstream ptsfile(options.szBSPName);

    if (!ptsfile)
        FError("Failed to open {}: {}", options.szBSPName, strerror(errno));

    return ptsfile;
}

static void ClearOccupied_r(node_t *node)
{
    if (node->planenum != PLANENUM_LEAF) {
        ClearOccupied_r(node->children[0]);
        ClearOccupied_r(node->children[1]);
        return;
    }

    /* leaf node */
    node->outside_distance = -1;
    node->occupant = nullptr;
}

/*
=============
Portal_Passable

Returns true if the portal has non-opaque leafs on both sides

from q3map
=============
*/
static bool Portal_Passable(const portal_t *p)
{
    if (p->nodes[0] == &outside_node || p->nodes[1] == &outside_node) {
        // FIXME: need this because the outside_node doesn't have PLANENUM_LEAF set
        return false;
    }

    Q_assert(p->nodes[0]->planenum == PLANENUM_LEAF);
    Q_assert(p->nodes[1]->planenum == PLANENUM_LEAF);

    if (p->nodes[0]->opaque() || p->nodes[1]->opaque())
        return false;

    return true;
}

/*
==================
FloodFillFromOutsideNode

Sets outside_distance on leafs reachable from the void

preconditions:
- all leafs have outside_distance set to -1
==================
*/
static void FloodFillFromVoid()
{
    // breadth-first search
    std::list<std::pair<node_t *, int>> queue;
    std::unordered_set<node_t *> visited_nodes;

    // push a node onto the queue which is in the void, but has a portal to outside_node
    // NOTE: remember, the headnode has no relationship to the outside of the map.
    {
        const int side = (outside_node.portals->nodes[0] == &outside_node);
        node_t *fillnode = outside_node.portals->nodes[side];

        Q_assert(fillnode != &outside_node);

        // this must be true because the map is made from closed brushes, beyion which is void
        Q_assert(!fillnode->opaque());
        queue.emplace_back(fillnode, 0);
    }

    while (!queue.empty()) {
        const auto front = queue.front();
        queue.pop_front();

        auto [node, outside_distance] = front;

        if (visited_nodes.find(node) != visited_nodes.end()) {
            // have already visited this node
            continue;
        }

        // visit node
        visited_nodes.insert(node);
        node->outside_distance = outside_distance;

        // push neighbouring nodes onto the back of the queue
        int side;
        for (portal_t *portal = node->portals; portal; portal = portal->next[!side]) {
            side = (portal->nodes[0] == node);

            if (!Portal_Passable(portal))
                continue;

            node_t *neighbour = portal->nodes[side];
            queue.emplace_back(neighbour, outside_distance + 1);
        }
    }
}

/*
=============
FindPortalsToVoid

Given an occupied leaf, returns a list of porals leading to the void
=============
*/
static std::vector<portal_t *> FindPortalsToVoid(node_t *occupied_leaf)
{
    Q_assert(occupied_leaf->occupant != nullptr);
    Q_assert(occupied_leaf->outside_distance >= 0);

    std::vector<portal_t *> result;

    node_t *node = occupied_leaf;
    while (1) {
        // exit?
        if (node->outside_distance == 0)
            break; // this is the void leaf where we started the flood fill in FloodFillFromVoid()

        // find the next node...

        node_t *bestneighbour = nullptr;
        portal_t *bestportal = nullptr;
        int bestdist = node->outside_distance;

        int side;
        for (portal_t *portal = node->portals; portal; portal = portal->next[!side]) {
            side = (portal->nodes[0] == node);

            if (!Portal_Passable(portal))
                continue;

            node_t *neighbour = portal->nodes[side];
            Q_assert(neighbour != node);
            Q_assert(neighbour->outside_distance >= 0);

            if (neighbour->outside_distance < bestdist) {
                bestneighbour = neighbour;
                bestportal = portal;
                bestdist = neighbour->outside_distance;
            }
        }

        Q_assert(bestneighbour != nullptr);
        Q_assert(bestdist < node->outside_distance);

        // go through bestportal
        result.push_back(bestportal);
        node = bestneighbour;
    }

    return result;
}

/*
===============
WriteLeakTrail
===============
*/
static void WriteLeakTrail(std::ofstream &leakfile, qvec3d point1, const qvec3d &point2)
{
    qvec3d vector = point2 - point1;
    vec_t dist = qv::normalizeInPlace(vector);

    while (dist > options.leakdist.value()) {
        fmt::print(leakfile, "{}\n", point1);
        point1 += vector * options.leakdist.value();
        dist -= options.leakdist.value();
    }
}

/*
===============
WriteLeakLine

leakline should be a sequence of portals leading from leakentity to the void
===============
*/
static void WriteLeakLine(const mapentity_t *leakentity, const std::vector<portal_t *> &leakline)
{
    std::ofstream ptsfile = InitPtsFile();

    qvec3d prevpt = leakentity->origin;

    for (portal_t *portal : leakline) {
        qvec3d currpt = portal->winding->center();

        // draw dots from prevpt to currpt
        WriteLeakTrail(ptsfile, prevpt, currpt);

        prevpt = currpt;
    }

    LogPrint("Leak file written to {}\n", options.szBSPName);
}

/*
==================
FindOccupiedLeafs

sets node->occupant
==================
*/
std::vector<node_t *> FindOccupiedClusters(node_t *headnode)
{
    std::vector<node_t *> result;

    for (int i = 1; i < map.numentities(); i++) {
        mapentity_t *entity = &map.entities.at(i);

        /* skip entities at (0 0 0) (bmodels) */
        if (qv::epsilonEmpty(entity->origin, EQUAL_EPSILON))
            continue;

        /* find the leaf it's in. Skip opqaue leafs */
        node_t *leaf = PointInLeaf(headnode, entity->origin);

        if (leaf->opaque())
            continue;

        /* did we already find an entity for this leaf? */
        if (leaf->occupant != nullptr)
            continue;

        leaf->occupant = entity;

        result.push_back(leaf);
    }

    return result;
}

/*
==================
ResetFacesTouchingOccupiedLeafs

Set f->touchesOccupiedLeaf=false on all faces.
==================
*/
static void ResetFacesTouchingOccupiedLeafs(node_t *node)
{
    if (node->planenum == PLANENUM_LEAF) {
        return;
    }

    for (face_t *face : node->facelist) {
        face->touchesOccupiedLeaf = false;
    }

    ResetFacesTouchingOccupiedLeafs(node->children[0]);
    ResetFacesTouchingOccupiedLeafs(node->children[1]);
}

/*
==================
MarkFacesTouchingOccupiedLeafs

Set f->touchesOccupiedLeaf=true on faces that are touching occupied leafs
==================
*/
static void MarkFacesTouchingOccupiedLeafs(node_t *node)
{
    if (node->planenum != PLANENUM_LEAF) {
        MarkFacesTouchingOccupiedLeafs(node->children[0]);
        MarkFacesTouchingOccupiedLeafs(node->children[1]);
        return;
    }

    // visit the leaf

    if (node->outside_distance == -1) {
        // This is an occupied leaf, so we need to keep all of the faces touching it.
        for (auto &markface : node->markfaces) {
            markface->touchesOccupiedLeaf = true;
        }
    }
}

/*
==================
ClearOutFaces

Deletes (by setting f->w.numpoints=0) faces in solid nodes
==================
*/
static void ClearOutFaces(node_t *node)
{
    if (node->planenum != PLANENUM_LEAF) {
        ClearOutFaces(node->children[0]);
        ClearOutFaces(node->children[1]);
        return;
    }

    // visit the leaf
    if (!node->contents.is_solid(options.target_game)) {
        return;
    }

    for (auto &markface : node->markfaces) {
        // NOTE: This is how faces are deleted here, kind of ugly
        markface->w.clear();
    }

    // FIXME: Shouldn't be needed here
    node->facelist = {};
}

static void OutLeafsToSolid_r(node_t *node, int *outleafs_count)
{
    if (node->planenum != PLANENUM_LEAF) {
        OutLeafsToSolid_r(node->children[0], outleafs_count);
        OutLeafsToSolid_r(node->children[1], outleafs_count);
        return;
    }

    // skip leafs reachable from entities
    if (node->outside_distance == -1)
        return;

    // Don't fill sky, or count solids as outleafs
    if (node->contents.is_solid(options.target_game) || node->contents.is_sky(options.target_game))
        return;

    // Now check all faces touching the leaf. If any of them are partially going into the occupied part of the map,
    // don't fill the leaf (see comment in FillOutside).
    bool skipFill = false;
    for (auto &markface : node->markfaces) {
        if (markface->touchesOccupiedLeaf) {
            skipFill = true;
            break;
        }
    }
    if (skipFill) {
        return;
    }

    // Finally, we can fill it in as void.
    node->contents = options.target_game->create_solid_contents();
    *outleafs_count += 1;
}

static int OutLeafsToSolid(node_t *node)
{
    int count = 0;
    OutLeafsToSolid_r(node, &count);
    return count;
}

//=============================================================================

/*
===========
FillOutside

===========
*/
bool FillOutside(node_t *node, const int hullnum)
{
    LogPrint(LOG_PROGRESS, "---- {} ----\n", __func__);

    /* Clear the outside filling state on all nodes */
    ClearOccupied_r(node);

    // Sets leaf->occupant
    const std::vector<node_t *> occupied_leafs = FindOccupiedClusters(node);

    if (occupied_leafs.empty()) {
        LogPrint("WARNING: No entities in empty space -- no filling performed (hull {})\n", hullnum);
        return false;
    }

    // Flood fill from outside -> in.
    //
    // We tried inside -> out and it leads to things like monster boxes getting inadvertently sealed,
    // or even whole sections of the map with no point entities - problems compounded by hull expansion.
    FloodFillFromVoid();

    // check for the occupied leaf closest to the void
    int best_leak_dist = INT_MAX;
    node_t *best_leak = nullptr;

    for (node_t *leaf : occupied_leafs) {
        if (leaf->outside_distance == -1)
            continue;

        if (leaf->outside_distance < best_leak_dist) {
            best_leak_dist = leaf->outside_distance;
            best_leak = leaf;
        }
    }

    if (best_leak) {
        const auto leakline = FindPortalsToVoid(best_leak);

        mapentity_t *leakentity = best_leak->occupant;
        Q_assert(leakentity != nullptr);

        LogPrint("WARNING: Reached occupant \"{}\" at ({}), no filling performed.\n",
            ValueForKey(leakentity, "classname"), leakentity->origin);
        if (map.leakfile)
            return false;

        WriteLeakLine(leakentity, leakline);
        map.leakfile = true;

        /* Get rid of the .prt file since the map has a leak */
        options.szBSPName.replace_extension("prt");
        remove(options.szBSPName);

        if (options.leaktest.value()) {
            LogPrint("Aborting because -leaktest was used.\n");
            exit(1);
        }

        // clear occupied state, so areas can be flooded in Q2
        ClearOccupied_r(node);

        return false;
    }

    // At this point, leafs not reachable from entities have (node->occupied == 0).
    // The two final tasks are:
    // 1. Mark the leafs that are not reachable as CONTENTS_SOLID (i.e. filling them in as the void).
    // 2. Delete faces in those leafs

    // An annoying wrinkle here: there may be leafs with (node->occupied == 0), which means they should be filled in as
    // void, but they have faces straddling between them and occupied leafs (i.e. leafs which will be CONTENTS_EMPTY
    // because they're in playable space). See missing_face_simple.map for an example.
    //
    // The subtlety is, if we fill these leafs in as solid and delete the inward-facing faces, the only face left
    // will be the void-and-non-void-straddling face. This face will mess up LinkConvexFaces, since we need to rebuild
    // the BSP and recalculate the leaf contents, unaware of the fact that we wanted this leaf to be void
    // (CONTENTS_SOLID), and this face will cause it to be marked as CONTENTS_EMPTY which will manifest as messed up
    // hull0 collision in game (weapons shoot through the leaf.)
    //
    // In order to avoid this scenario, we need to detect those "void-and-non-void-straddling" faces and not fill those
    // leafs in as solid. This will keep some extra faces around but keep the content types consistent.

    ResetFacesTouchingOccupiedLeafs(node);
    MarkFacesTouchingOccupiedLeafs(node);

    /* now go back and fill outside with solid contents */
    const int outleafs = OutLeafsToSolid(node);

    /* remove faces from filled in leafs */
    ClearOutFaces(node);

    LogPrint(LOG_STAT, "     {:8} outleafs\n", outleafs);
    return true;
}
