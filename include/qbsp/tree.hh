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

#pragma once

#include <qbsp/qbsp.hh>
#include <qbsp/brush.hh>
#include <qbsp/portals.hh>
#include <common/qvec.hh>

#include <memory>
#include <vector>

#include <tbb/concurrent_vector.h>

struct portal_t;
struct tree_t;

void FreeTreePortals(tree_t &tree);

struct tree_t
{
    node_t *headnode = nullptr;
    node_t outside_node = {}; // portals outside the world face this
    aabb3d bounds;

    // here for ownership/memory management - not intended to be iterated directly
    std::vector<std::unique_ptr<portal_t>> portals;

    // which kind of portals (cluster portals or leaf portals) are currently built?
    portaltype_t portaltype = portaltype_t::NONE;

    // here for ownership/memory management - not intended to be iterated directly
    //
    // concurrent_vector allows BrushBSP to insert nodes in parallel, and also
    // promises not to move elements so we can omit the std::unique_ptr wrapper.
    tbb::concurrent_vector<node_t> nodes;

    // creates a new portal owned by `this` (stored in the `portals` vector) and
    // returns a raw pointer to it
    portal_t *create_portal();

    // creates a new node owned by `this` (stored in the `nodes` vector) and
    // returns a raw pointer to it
    node_t *create_node();

    // reset the tree without clearing allocated vector space
    void clear();
};

void PruneNodes(node_t *node);
