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

#include <qbsp/map.hh>
#include <qbsp/portals.hh>
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

static void ClearOccupied_r(node_t *node)
{
    // we need to clear this on leaf nodes and detail separators (clusters).. just clear it on everything
    node->outside_distance = -1;
    node->occupant = nullptr;

    if (node->planenum != PLANENUM_LEAF) {
        ClearOccupied_r(node->children[0]);
        ClearOccupied_r(node->children[1]);
    }
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

    auto leafOpaque = [](const node_t *l) {
        Q_assert(l->planenum == PLANENUM_LEAF || l->detail_separator);

        // fixme-brushbsp: confirm, why was this not needed before?
        // detail separators are treated as non-opaque because detail doesn't block vis
        // fixme-brushbsp: should probably move to node_t::opaque()
        if (l->detail_separator) {
            return false;
        }

        return l->opaque();
    };

    if (leafOpaque(p->nodes[0]) || leafOpaque(p->nodes[1]))
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
    fs::path name = options.bsp_path;
    name.replace_extension("pts");

    std::ofstream ptsfile(name);

    if (!ptsfile)
        FError("Failed to open {}: {}", name, strerror(errno));

    qvec3d prevpt = leakentity->origin;

    for (portal_t *portal : leakline) {
        qvec3d currpt = portal->winding->center();

        // draw dots from prevpt to currpt
        WriteLeakTrail(ptsfile, prevpt, currpt);

        prevpt = currpt;
    }

    logging::print("Leak file written to {}\n", name);
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

    for (int i = 1; i < map.entities.size(); i++) {
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

//=============================================================================

static void MarkBrushSidesInvisible(mapentity_t *entity)
{
    for (auto &brush : entity->brushes) {
        for (auto &face : brush.faces) {
            face.visible = false;
        }
    }
}

/*
==================
MarkVisibleBrushSides

Set f->touchesOccupiedLeaf=true on faces that are touching occupied leafs
==================
*/
static void MarkVisibleBrushSides(node_t *node)
{
    if (node->planenum != PLANENUM_LEAF) {
        MarkVisibleBrushSides(node->children[0]);
        MarkVisibleBrushSides(node->children[1]);
        return;
    }

    if (node->opaque()) {
        return;
    }

    // visit the non-opaque leaf: check all portals to neighbouring leafs.

    int side;
    for (portal_t *portal = node->portals; portal; portal = portal->next[!side]) {
        side = (portal->nodes[0] == node);

        node_t *neighbour_leaf = portal->nodes[side];

        for (auto *brush : neighbour_leaf->original_brushes) {
            for (auto &side : brush->faces) {
                if (side.planenum == portal->planenum) {
                    // we've found a brush side in an original brush in the neighbouring
                    // leaf, on a portal to this (non-opaque) leaf, so mark it as visible.
                    side.visible = true;
                }
            }
        }
    }
}

//=============================================================================

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

Goal is to mark brush sides which only touch void as "unbounded" (aka Q2 skip),
so they're not used as BSP splitters in the next phase and basically expand outwards.

Brush sides which cross between void and non-void are problematic for this, so
the process looks like:

1) flood outside -> in from beyond the map bounds and mark these leafs as solid

Now all leafs marked "empty" are actually empty, not void.

2) initialize all original brush sides to "invisible"

2) flood from all empty leafs, mark original brush sides as "visible"

This will handle partially-void, partially-in-bounds sides (they'll be marked visible).

(doing it the opposite way, defaulting brushes to "visible" and flood-filling
from the void wouldn't work, because brush sides that cross into the map would
get incorrectly marked as "invisible").

fixme-brushbsp: we'll want to do this for detail as well, which means building another set of
portals for everything (not just structural).

fixme-brushbsp: remember, structural covered by detail still gets marked 'visible'.
===========
*/
bool FillOutside(mapentity_t *entity, node_t *node, const int hullnum)
{
    logging::print(logging::flag::PROGRESS, "---- {} ----\n", __func__);

    /* Clear the outside filling state on all nodes */
    ClearOccupied_r(node);

    // Sets leaf->occupant
    const std::vector<node_t *> occupied_leafs = FindOccupiedClusters(node);

    for (auto *occupied_leaf : occupied_leafs) {
        Q_assert(occupied_leaf->outside_distance == -1);
    }

    if (occupied_leafs.empty()) {
        logging::print("WARNING: No entities in empty space -- no filling performed (hull {})\n", hullnum);
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

        logging::print("WARNING: Reached occupant \"{}\" at ({}), no filling performed.\n",
            ValueForKey(leakentity, "classname"), leakentity->origin);
        if (map.leakfile)
            return false;

        WriteLeakLine(leakentity, leakline);
        map.leakfile = true;

        /* Get rid of the .prt file since the map has a leak */
        if (!options.keepprt.value()) {
            fs::path name = options.bsp_path;
            name.replace_extension("prt");
            remove(name);
        }

        if (options.leaktest.value()) {
            logging::print("Aborting because -leaktest was used.\n");
            exit(1);
        }

        // clear occupied state, so areas can be flooded in Q2
        ClearOccupied_r(node);

        return false;
    }

    // change the leaf contents
    const int outleafs = OutLeafsToSolid(node);

    // See missing_face_simple.map for a test case with a brush that straddles between void and non-void
    
    MarkBrushSidesInvisible(entity);

    MarkVisibleBrushSides(node);

    logging::print(logging::flag::STAT, "     {:8} outleafs\n", outleafs);
    return true;
}
