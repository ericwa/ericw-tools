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

#include <common/vectorutils.hh>
#include <qbsp/qbsp.hh>
#include <qbsp/brush.hh>
#include <qbsp/portals.hh>

//============================================================================

portal_t *tree_t::create_portal()
{
    return portals.emplace_back(std::make_unique<portal_t>()).get();
}

/*
==================
FreeTreePortals_r

==================
*/
static void ClearNodePortals_r(node_t *node)
{
    if (!node->is_leaf) {
        ClearNodePortals_r(node->children[0].get());
        ClearNodePortals_r(node->children[1].get());
    }

    node->portals = nullptr;
}

void FreeTreePortals(tree_t *tree)
{
    ClearNodePortals_r(tree->headnode.get());
    tree->outside_node.portals = nullptr;

    tree->portals.clear();
}

//============================================================================

static void ConvertNodeToLeaf(node_t *node, const contentflags_t &contents)
{
    // merge the children's brush lists
    node->original_brushes = concat(node->children[0]->original_brushes, node->children[1]->original_brushes);
    sort_and_remove_duplicates(node->original_brushes);

    node->is_leaf = true;

    for (int i = 0; i < 2; ++i) {
        node->children[i] = nullptr;
    }
    node->facelist.clear();

    node->contents = contents;

    Q_assert(node->markfaces.empty());
}

static void PruneNodes_R(node_t *node, int &count_pruned)
{
    if (node->is_leaf) {
        return;
    }

    PruneNodes_R(node->children[0].get(), count_pruned);
    PruneNodes_R(node->children[1].get(), count_pruned);

    if (node->children[0]->is_leaf && node->children[0]->contents.is_any_solid(qbsp_options.target_game) &&
        node->children[1]->is_leaf && node->children[1]->contents.is_any_solid(qbsp_options.target_game)) {
        // This discards any faces on-node. Should be safe (?)
        ConvertNodeToLeaf(node, qbsp_options.target_game->create_solid_contents());
        ++count_pruned;
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

    int count_pruned = 0;
    PruneNodes_R(node, count_pruned);

    logging::print(logging::flag::STAT, "     {:8} pruned nodes\n", count_pruned);
}
