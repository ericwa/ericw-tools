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

#include <qbsp/tree.hh>

#include <qbsp/qbsp.hh>
#include <qbsp/brush.hh>
#include <qbsp/portals.hh>
#include <tbb/task_group.h>

#include <atomic>

//============================================================================

portal_t *tree_t::create_portal()
{
    return portals.emplace_back(std::make_unique<portal_t>()).get();
}

node_t *tree_t::create_node()
{
    auto it = nodes.grow_by(1);

    return &(*it);
}

void tree_t::clear()
{
    headnode = nullptr;
    outside_node = {};
    bounds = {};

    FreeTreePortals(*this);
    nodes.clear();
}

/*
==================
FreeTreePortals_r

==================
*/
static void ClearNodePortals_r(node_t *node)
{
    if (!node->is_leaf) {
        ClearNodePortals_r(node->children[0]);
        ClearNodePortals_r(node->children[1]);
    }

    node->portals = nullptr;
}

#include <tbb/parallel_for_each.h>

void FreeTreePortals(tree_t &tree)
{
    if (tree.headnode) {
        ClearNodePortals_r(tree.headnode);
        tree.outside_node.portals = nullptr;
    }

    tbb::parallel_for_each(tree.portals, [](std::unique_ptr<portal_t> &portal) { portal.reset(); });

    tree.portals.clear();
    tree.portaltype = portaltype_t::NONE;
}

//============================================================================

static void ConvertNodeToLeaf(node_t *node, const contentflags_t &contents)
{
    // merge the children's brush lists
    size_t base = node->children[0]->original_brushes.size() > node->children[1]->original_brushes.size() ? 0 : 1;
    node->original_brushes = std::move(node->children[base]->original_brushes);
    node->original_brushes.insert(node->original_brushes.end(), node->children[base ^ 1]->original_brushes.begin(),
        node->children[base ^ 1]->original_brushes.end());

    std::sort(node->original_brushes.begin(), node->original_brushes.end(),
        [](const bspbrush_t *a, const bspbrush_t *b) { return a->mapbrush < b->mapbrush; });
    auto unique = std::unique(node->original_brushes.begin(), node->original_brushes.end());
    node->original_brushes.erase(unique, node->original_brushes.end());

    node->is_leaf = true;

    for (auto &child : node->children) {
        *child = {}; // clear everything in the node
        child = nullptr;
    }

    node->facelist.clear();

    node->contents = contents;

    Q_assert(node->markfaces.empty());
}

struct prune_stats_t : logging::stat_tracker_t
{
    stat &nodes_pruned = register_stat("nodes pruned");
};

static bool IsAnySolidLeaf(const node_t *node)
{
    return node->is_leaf && node->contents.is_any_solid(qbsp_options.target_game);
}

static void PruneNodes_R(node_t *node, prune_stats_t &stats)
{
    if (node->is_leaf) {
        // remap any contents
        if (qbsp_options.target_game->id != GAME_QUAKE_II && node->contents.is_detail_wall(qbsp_options.target_game)) {
            node->contents = qbsp_options.target_game->create_solid_contents();
        }
        return;
    }

    tbb::task_group g;
    g.run([&]() { PruneNodes_R(node->children[0], stats); });
    g.run([&]() { PruneNodes_R(node->children[1], stats); });
    g.wait();

    // fixme-brushbsp: is it correct to strip off detail flags here?
    if (IsAnySolidLeaf(node->children[0]) && IsAnySolidLeaf(node->children[1])) {
        // This discards any faces on-node. Should be safe (?)
        ConvertNodeToLeaf(node, qbsp_options.target_game->create_solid_contents());
        stats.nodes_pruned++;
    }

    // DarkPlaces has an assertion that fails if both children are
    // solid.

    /* N.B.: CONTENTS_DETAIL_FENCE is not remapped to CONTENTS_SOLID until the very last moment,
     * because we want to generate a leaf (if we set it to CONTENTS_SOLID now it would use leaf 0).
     */

    // fixme-brushbsp: corner case where two solid leafs shouldn't merge is two noclipfaces fence brushes touching
    // fixme-brushbsp: also merge other content types
    // fixme-brushbsp: maybe merge if same content type, and all faces on node are invisible?
}

void PruneNodes(node_t *node)
{
    logging::funcheader();

    prune_stats_t stats;

    PruneNodes_R(node, stats);
}
