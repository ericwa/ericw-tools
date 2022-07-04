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

#include <qbsp/outside.hh>
#include <qbsp/brush.hh>
#include <qbsp/map.hh>
#include <qbsp/portals.hh>
#include <qbsp/qbsp.hh>
#include <qbsp/tree.hh>

#include <climits>
#include <vector>
#include <set>
#include <list>
#include <unordered_set>
#include <utility>
#include <fstream>
#include <fmt/ostream.h>

static bool LeafSealsMap(const node_t *node)
{
    Q_assert(node->planenum == PLANENUM_LEAF);

    return options.target_game->contents_seals_map(node->contents);
}

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
    if (node->planenum == PLANENUM_LEAF) {
        return node;
    }

    const auto lock = std::lock_guard(map_planes_lock);
    const auto &plane = map.planes.at(node->planenum);
    vec_t dist = plane.distance_to(point);

    if (dist > 0) {
        // point is on the front of the node plane
        return PointInLeaf(node->children[0].get(), point);
    } else if (dist < 0) {
        // point is on the back of the node plane
        return PointInLeaf(node->children[1].get(), point);
    } else {
        // point is exactly on the node plane

        node_t *front = PointInLeaf(node->children[0].get(), point);
        node_t *back = PointInLeaf(node->children[1].get(), point);

        // prefer the opaque one
        if (LeafSealsMap(front)) {
            return front;
        }
        return back;
    }
}

static void ClearOccupied_r(node_t *node)
{
    // we need to clear this on leaf nodes.. just clear it on everything
    node->outside_distance = -1;
    node->occupied = 0;
    node->occupant = nullptr;

    if (node->planenum != PLANENUM_LEAF) {
        ClearOccupied_r(node->children[0].get());
        ClearOccupied_r(node->children[1].get());
    }
}

static bool OutsideFill_Passable(const portal_t *p)
{
    if (!p->onnode) {
        // portal to outside_node
        return false;
    }

    return !LeafSealsMap(p->nodes[0]) && !LeafSealsMap(p->nodes[1]);
}

/*
==================
MarkClusterOutsideDistance_R

Given that the cluster is reachable from the void, sets outside_distance
to the given value on this cluster and all desdcent leafs (if it's a detail cluster).
==================
*/
static void MarkClusterOutsideDistance_R(node_t* node, int outside_distance)
{
    node->outside_distance = outside_distance;

    if (node->planenum != PLANENUM_LEAF) {
        MarkClusterOutsideDistance_R(node->children[0].get(), outside_distance);
        MarkClusterOutsideDistance_R(node->children[1].get(), outside_distance);
    }    
}

/*
==================
FloodFillFromOutsideNode

Sets outside_distance on clusters reachable from the void

preconditions:
- all leafs have outside_distance set to -1
==================
*/
static void FloodFillClustersFromVoid(tree_t *tree)
{
    // breadth-first search
    std::list<std::pair<node_t *, int>> queue;
    std::unordered_set<node_t *> visited_nodes;

    // push a node onto the queue which is in the void, but has a portal to outside_node
    // NOTE: remember, the headnode has no relationship to the outside of the map.
    {
        const int side = (tree->outside_node.portals->nodes[0] == &tree->outside_node);
        node_t *fillnode = tree->outside_node.portals->nodes[side];

        Q_assert(fillnode != &tree->outside_node);

        // this must be true because the map is made from closed brushes, beyond which is void
        Q_assert(!LeafSealsMap(fillnode));
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
        MarkClusterOutsideDistance_R(node, outside_distance);

        // push neighbouring nodes onto the back of the queue
        int side;
        for (portal_t *portal = node->portals; portal; portal = portal->next[!side]) {
            side = (portal->nodes[0] == node);

            if (!OutsideFill_Passable(portal))
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

            if (!OutsideFill_Passable(portal))
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
static void MarkOccupiedClusters(node_t *headnode)
{
    for (int i = 1; i < map.entities.size(); i++) {
        mapentity_t *entity = &map.entities.at(i);

        /* skip entities at (0 0 0) (bmodels) */
        if (qv::epsilonEmpty(entity->origin, EQUAL_EPSILON))
            continue;

#if 0
        /* skip lights */
        if (strcmp(ValueForKey(entity, "classname"), "light") == 0)
            continue;
#endif

        /* find the leaf it's in. Skip opqaue leafs */
        node_t *cluster = PointInLeaf(headnode, entity->origin);

        if (LeafSealsMap(cluster)) {
            continue;
        }

        /* did we already find an entity for this leaf? */
        if (cluster->occupant != nullptr) {
            continue;
        }

        cluster->occupant = entity;
    }
}

static void FindOccupiedClusters_R(node_t *node, std::vector<node_t *>& result)
{
    if (node->occupant) {
        result.push_back(node);
    }

    if (node->planenum != PLANENUM_LEAF) {
        FindOccupiedClusters_R(node->children[0].get(), result);
        FindOccupiedClusters_R(node->children[1].get(), result);
    }
}

/*
==================
FindOccupiedClusters

Requires that FillOutside has run
==================
*/
std::vector<node_t *> FindOccupiedClusters(node_t *headnode)
{
    std::vector<node_t *> result;
    FindOccupiedClusters_R(headnode, result);
    return result;
}

//=============================================================================

static void MarkBrushSidesInvisible(mapentity_t *entity)
{
    for (auto &brush : entity->brushes) {
        for (auto &face : brush->sides) {
            face.visible = false;
        }
    }
}

static void MarkAllBrushSidesVisible_R(node_t *node)
{
    // descend to leafs
    if (node->planenum != PLANENUM_LEAF) {
        MarkAllBrushSidesVisible_R(node->children[0].get());
        MarkAllBrushSidesVisible_R(node->children[1].get());
        return;
    }

    for (auto *brush : node->original_brushes) {
        for (auto &side : brush->sides) {
            side.visible = true;
        }
    }
}

/*
==================
MarkVisibleBrushSides

Set f->touchesOccupiedLeaf=true on faces that are touching occupied leafs
==================
*/
static void MarkVisibleBrushSides_R(node_t *node)
{
    // descent to leafs
    if (!(node->planenum == PLANENUM_LEAF)) {
        MarkVisibleBrushSides_R(node->children[0].get());
        MarkVisibleBrushSides_R(node->children[1].get());
        return;
    }

    if (LeafSealsMap(node)) {
        // this cluster is opaque
        return;
    }

    Q_assert(!node->detail_separator);

    // we also want to mark brush sides in the neighbouring cluster
    // as visible

    int side;
    for (portal_t *portal = node->portals; portal; portal = portal->next[!side]) {
        side = (portal->nodes[0] == node);

        node_t *neighbour_leaf = portal->nodes[side];

        if (neighbour_leaf->planenum == PLANENUM_LEAF) {
            // optimized case: just mark the brush sides in the neighbouring
            // leaf that are coplanar
            for (auto *brush : neighbour_leaf->original_brushes) {
                for (auto &side : brush->sides) {
                    if (side.planenum == portal->planenum) {
                        // we've found a brush side in an original brush in the neighbouring
                        // leaf, on a portal to this (non-opaque) leaf, so mark it as visible.
                        side.visible = true;
                    }
                }
            }
        } else {
            Q_assert(false);
        }
    }
}

//=============================================================================

static void OutLeafsToSolid_r(node_t *node, int *outleafs_count, settings::filltype_t filltype)
{
    if (node->planenum != PLANENUM_LEAF) {
        OutLeafsToSolid_r(node->children[0].get(), outleafs_count, filltype);
        OutLeafsToSolid_r(node->children[1].get(), outleafs_count, filltype);
        return;
    }

    // skip leafs reachable from entities
    if (filltype == settings::filltype_t::INSIDE) {
        if (node->occupied > 0) {
            return;
        }
    } else {
        if (node->outside_distance == -1) {
            return;
        }
    }

    // Don't fill sky, or count solids as outleafs
    if (options.target_game->contents_seals_map(node->contents)) {
        return;
    }

     // Finally, we can fill it in as void.
    node->contents = options.target_game->create_solid_contents();
    *outleafs_count += 1;
}

static int OutLeafsToSolid(node_t *node, settings::filltype_t filltype)
{
    int count = 0;
    OutLeafsToSolid_r(node, &count, filltype);
    return count;
}

//=============================================================================

static void SetOccupied_R(node_t *node, int dist)
{
    if (node->planenum != PLANENUM_LEAF) {
        SetOccupied_R(node->children[0].get(), dist);
        SetOccupied_R(node->children[1].get(), dist);
    }

    node->occupied = dist;
}

/*
==================
precondition: all leafs have occupied set to 0
==================
*/
static void BFSFloodFillFromOccupiedLeafs(const std::vector<node_t *> &occupied_leafs)
{
    std::list<std::pair<node_t *, int>> queue;
    for (node_t *leaf : occupied_leafs) {
        queue.push_back(std::make_pair(leaf, 1));
    }

    while (!queue.empty()) {
        auto pair = queue.front();
        queue.pop_front();

        node_t *node = pair.first;
        const int dist = pair.second;

        if (node->occupied == 0) {
            // we haven't visited this node yet
            Q_assert(!node->detail_separator);
            node->occupied = dist;

            // push neighbouring nodes onto the back of the queue
            int side;
            for (portal_t *portal = node->portals; portal; portal = portal->next[!side]) {
                side = (portal->nodes[0] == node);

                if (!OutsideFill_Passable(portal))
                    continue;

                node_t *neighbour = portal->nodes[side];
                queue.push_back(std::make_pair(neighbour, dist + 1));
            }
        }
    }
}

static std::vector<portal_t *> MakeLeakLine(node_t *outleaf, mapentity_t *&leakentity)
{
    std::vector<portal_t *> result;

    Q_assert(outleaf->occupied > 0);

    node_t *node = outleaf;
    while (1) {
        // exit?
        if (node->occupied == 1)
            break; // this node contains an entity

        // find the next node...

        node_t *bestneighbour = nullptr;
        portal_t *bestportal = nullptr;
        int bestoccupied = node->occupied;

        int side;
        for (portal_t *portal = node->portals; portal; portal = portal->next[!side]) {
            side = (portal->nodes[0] == node);

            if (!OutsideFill_Passable(portal))
                continue;

            node_t *neighbour = portal->nodes[side];
            Q_assert(neighbour != node);
            Q_assert(neighbour->occupied > 0);

            if (neighbour->occupied < bestoccupied) {
                bestneighbour = neighbour;
                bestportal = portal;
                bestoccupied = neighbour->occupied;
            }
        }

        Q_assert(bestneighbour != nullptr);
        Q_assert(bestoccupied < node->occupied);

        // go through bestportal
        result.push_back(bestportal);
        node = bestneighbour;
    }

    Q_assert(node->occupant != nullptr);
    Q_assert(node->occupied == 1);

    leakentity = node->occupant;
    return result;
}

/*
===========
FillOutside

Goal is to mark brush sides which are only seen from the void as `visible = false`.
They will still lbe used as BSP splitters, but the idea of q2bsp tools is to
use them as splitters after all of the `visible = true` splitters have been used.

This should allow PruneNodes to prune most void nodes like Q1 was able to.

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

Special cases: structural fully covered by detail still needs to be marked "visible".
===========
*/
bool FillOutside(mapentity_t *entity, tree_t *tree, const int hullnum)
{
    node_t *node = tree->headnode.get();

    logging::print(logging::flag::PROGRESS, "---- {} ----\n", __func__);

    /* Clear the outside filling state on all nodes */
    ClearOccupied_r(node);

    // Sets leaf->occupant
    MarkOccupiedClusters(node);
    const std::vector<node_t *> occupied_clusters = FindOccupiedClusters(node);

    for (auto *occupied_cluster : occupied_clusters) {
        Q_assert(occupied_cluster->outside_distance == -1);
        Q_assert(occupied_cluster->occupied == 0);
    }

    if (occupied_clusters.empty()) {
        logging::print("WARNING: No entities in empty space -- no filling performed (hull {})\n", hullnum);
        return false;
    }

    mapentity_t *leakentity = nullptr;
    std::vector<portal_t *> leakline;

    settings::filltype_t filltype = options.filltype.value();
    
    if (filltype == settings::filltype_t::AUTO) {
        filltype = hullnum > 0 ? settings::filltype_t::OUTSIDE : settings::filltype_t::INSIDE;
    }

    if (filltype == settings::filltype_t::INSIDE) {
        BFSFloodFillFromOccupiedLeafs(occupied_clusters);

        /* first check to see if an occupied leaf is hit */
        const int side = (tree->outside_node.portals->nodes[0] == &tree->outside_node);
        node_t *fillnode = tree->outside_node.portals->nodes[side];

        if (fillnode->occupied > 0) {
            leakline = MakeLeakLine(fillnode, leakentity);
        }
    } else {
        // Flood fill from outside -> in.
        //
        // We tried inside -> out and it leads to things like monster boxes getting inadvertently sealed,
        // or even whole sections of the map with no point entities - problems compounded by hull expansion.
        FloodFillClustersFromVoid(tree);

        // check for the occupied leaf closest to the void
        int best_leak_dist = INT_MAX;
        node_t *best_leak = nullptr;

        for (node_t *leaf : occupied_clusters) {
            if (leaf->outside_distance == -1)
                continue;

            if (leaf->outside_distance < best_leak_dist) {
                best_leak_dist = leaf->outside_distance;
                best_leak = leaf;
            }
        }

        if (best_leak) {
            leakentity = best_leak->occupant;
            Q_assert(leakentity != nullptr);
            leakline = FindPortalsToVoid(best_leak);
        }
    }

    if (leakentity) {
        logging::print("WARNING: Reached occupant \"{}\" at ({}), no filling performed.\n",
            leakentity->epairs.get("classname"), leakentity->origin);
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
    const int outleafs = OutLeafsToSolid(node, filltype);

    // See missing_face_simple.map for a test case with a brush that straddles between void and non-void
    
    MarkBrushSidesInvisible(entity);

    MarkVisibleBrushSides_R(node);

    if (options.outsidedebug.value() && (options.target_game->get_hull_sizes().size() == 0 || hullnum == 0)) {
        fs::path path = options.bsp_path;
        path.replace_extension(".outside.map");

        WriteBspBrushMap(path, map.entities[0].brushes);
    }

    logging::print(logging::flag::STAT, "     {:8} outleafs\n", outleafs);
    return true;
}

void FillBrushEntity(mapentity_t* entity, tree_t *tree, const int hullnum)
{
    logging::print(logging::flag::PROGRESS, "---- {} ----\n", __func__);

    // Clear the outside filling state on all nodes
    ClearOccupied_r(tree->headnode.get());

    MarkBrushSidesInvisible(entity);

    MarkVisibleBrushSides_R(tree->headnode.get());
}
