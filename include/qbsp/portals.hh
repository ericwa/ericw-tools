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

#include <common/bspfile.hh>
#include <qbsp/qbsp.hh>

#include <atomic>
#include <memory>

struct side_t;

struct portal_t
{
    int planenum;
    node_t *onnode; // nullptr = portal to the outside of the world (one of six sides of a box)
    node_t *nodes[2]; // [0] = front side of planenum
    portal_t *next[2]; // [0] = next portal in nodes[0]'s list of portals
    std::optional<winding_t> winding;

    bool sidefound; // false if ->side hasn't been checked
    side_t *side; // NULL = non-visible
    face_t *face[2]; // output face in bsp file
};

struct tree_t
{
    std::unique_ptr<node_t> headnode;
    node_t outside_node = {}; // portals outside the world face this
    aabb3d bounds;
};

struct portalstats_t {
    std::atomic<int> c_tinyportals;
};

contentflags_t ClusterContents(const node_t *node);
bool Portal_VisFlood(const portal_t *p);
bool Portal_EntityFlood(const portal_t *p, int32_t s);
void MakeNodePortal(node_t *node, portalstats_t &stats);
void SplitNodePortals(node_t *node, portalstats_t &stats);
void MakeTreePortals(tree_t *tree);
void FreeTreePortals_r(node_t *node);
void AssertNoPortals(node_t *node);
void MakeHeadnodePortals(tree_t *tree);
void EmitAreaPortals(node_t *headnode);
void FloodAreas(mapentity_t *entity, node_t *headnode);
void MarkVisibleSides(tree_t *tree, mapentity_t* entity);
