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
#include <qbsp/brush.hh>
#include <qbsp/qbsp.hh>

#include <atomic>
#include <memory>

struct side_t;
struct tree_t;

struct portal_t
{
    qbsp_plane_t plane;
    node_t *onnode; // nullptr = portal to the outside of the world (one of six sides of a box)
    node_t *nodes[2]; // [0] = front side of planenum
    portal_t *next[2]; // [0] = next portal in nodes[0]'s list of portals
    std::unique_ptr<winding_t> winding;

    bool sidefound; // false if ->side hasn't been checked
    side_t *sides[2]; // [0] = the brush side visible on nodes[0] - it could come from a brush in nodes[1]. NULL =
                      // non-visible
    face_t *face[2]; // output face in bsp file
};

// helper used for building the portals in paralllel.
struct buildportal_t
{
    qbsp_plane_t plane;
    node_t *onnode = nullptr; // nullptr = portal to the outside of the world (one of six sides of a box)
    node_t *nodes[2] = {nullptr, nullptr}; // [0] = front side of planenum
    std::unique_ptr<winding_t> winding;

    inline void set_nodes(node_t *front, node_t *back) {
        nodes[0] = front;
        nodes[1] = back;
    }
};

struct portalstats_t
{
    std::atomic<int> c_tinyportals;
};

contentflags_t ClusterContents(const node_t *node);
bool Portal_VisFlood(const portal_t *p);
bool Portal_EntityFlood(const portal_t *p, int32_t s);
std::list<std::unique_ptr<buildportal_t>> MakeNodePortal(tree_t *tree, node_t *node, std::list<std::unique_ptr<buildportal_t>> boundary_portals, portalstats_t &stats);
twosided<std::list<std::unique_ptr<buildportal_t>>> SplitNodePortals(const node_t *node, std::list<std::unique_ptr<buildportal_t>> boundary_portals, portalstats_t &stats);
enum class portaltype_t {
    TREE, VIS
};
std::list<std::unique_ptr<buildportal_t>> MakeTreePortals_r(tree_t *tree, node_t *node, portaltype_t type, std::list<std::unique_ptr<buildportal_t>> boundary_portals, portalstats_t &stats);
void MakeTreePortals(tree_t *tree);
std::list<std::unique_ptr<buildportal_t>> MakeHeadnodePortals(tree_t *tree);
void MakePortalsFromBuildportals(tree_t *tree, std::list<std::unique_ptr<buildportal_t>> buildportals);
void EmitAreaPortals(node_t *headnode);
void FloodAreas(mapentity_t *entity, node_t *headnode);
void MarkVisibleSides(tree_t *tree, mapentity_t *entity, bspbrush_vector_t &brushes);
