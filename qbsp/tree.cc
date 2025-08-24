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
    if (auto *nodedata = node->get_nodedata()) {
        ClearNodePortals_r(nodedata->children[0]);
        ClearNodePortals_r(nodedata->children[1]);
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

static void ConvertNodeToLeaf(node_t *node, contentflags_t contents)
{
    auto *nodedata = node->get_nodedata();

    // merge the children's brush lists
    size_t base = nodedata->children[0]->get_leafdata()->original_brushes.size() >
                          nodedata->children[1]->get_leafdata()->original_brushes.size()
                      ? 0
                      : 1;
    std::vector<bspbrush_t *> original_brushes = std::move(nodedata->children[base]->get_leafdata()->original_brushes);
    original_brushes.insert(original_brushes.end(),
        nodedata->children[base ^ 1]->get_leafdata()->original_brushes.begin(),
        nodedata->children[base ^ 1]->get_leafdata()->original_brushes.end());

    std::sort(original_brushes.begin(), original_brushes.end(),
        [](const bspbrush_t *a, const bspbrush_t *b) { return a->mapbrush < b->mapbrush; });
    auto unique = std::unique(original_brushes.begin(), original_brushes.end());
    original_brushes.erase(unique, original_brushes.end());

    auto *leafdata = node->make_leaf();
    leafdata->contents = contents;
    leafdata->original_brushes = original_brushes;
}

struct prune_stats_t : logging::stat_tracker_t
{
    stat &nodes_pruned = register_stat("nodes pruned");
};

static bool IsAnySolidLeaf(const node_t *node)
{
    auto *leafdata = node->get_leafdata();
    return leafdata && leafdata->contents.is_any_solid();
}

static void PruneNodes_R(node_t *node, prune_stats_t &stats)
{
    if (auto *leafdata = node->get_leafdata()) {
        // remap any contents
        if (qbsp_options.target_game->id != GAME_QUAKE_II && leafdata->contents.is_detail_wall()) {
            leafdata->contents = contentflags_t::make(EWT_VISCONTENTS_SOLID);
        }
        return;
    }

    auto *nodedata = node->get_nodedata();

    tbb::task_group g;
    g.run([&]() { PruneNodes_R(nodedata->children[0], stats); });
    g.run([&]() { PruneNodes_R(nodedata->children[1], stats); });
    g.wait();

    // fixme-brushbsp: is it correct to strip off detail flags here?
    if (IsAnySolidLeaf(nodedata->children[0]) && IsAnySolidLeaf(nodedata->children[1])) {
        contentflags_t merged_contents = contentflags_t::combine_contents(
            nodedata->children[0]->get_leafdata()->contents, nodedata->children[1]->get_leafdata()->contents);

        // This discards any faces on-node. Should be safe (?)
        ConvertNodeToLeaf(node, merged_contents);
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
