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

#include <common/qvec.hh>

#include <memory>
#include <vector>

struct node_t;
struct portal_t;

struct tree_t
{
    std::unique_ptr<node_t> headnode;
    node_t outside_node = {}; // portals outside the world face this
    aabb3d bounds;

    // here for ownership/memory management - not intended to be iterated directly
    std::vector<std::unique_ptr<portal_t>> portals;

    // creates a new portal owned by `this` (stored in the `portals` vector) and
    // returns a raw pointer to it
    portal_t *create_portal();
};

void DetailToSolid(node_t *node);
void PruneNodes(node_t *node);
