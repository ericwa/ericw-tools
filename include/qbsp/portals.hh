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
    // nullptr = portal to the outside of the world (one of six sides of a box)
    node_t *onnode;
    // .front/.back side of planenum
    twosided<node_t *> nodes;
    // front = next portal in nodes[0]'s list of portals
    twosided<portal_t *> next;
    winding_t winding;

    // front = the brush side visible on nodes.front - it could come from a brush in nodes.back
    // nullptr = non-visible
    twosided<side_t *> sides;

    // false if ->side hasn't been checked
    bool sidefound;
};

// helper used for building the portals in paralllel.
struct buildportal_t
{
    qbsp_plane_t plane;
    // nullptr = portal to the outside of the world (one of six sides of a box)
    node_t *onnode = nullptr;
    // .front/.back side of planenum
    twosided<node_t *> nodes = {nullptr, nullptr};
    winding_t winding;
};

struct portalstats_t : logging::stat_tracker_t
{
    stat &c_tinyportals = register_stat("tiny portals");
};

contentflags_t ClusterContents(const node_t *node);
bool Portal_VisFlood(const portal_t *p);
bool Portal_EntityFlood(const portal_t *p, int32_t s);
enum class portaltype_t
{
    NONE,
    TREE,
    VIS
};
std::list<buildportal_t> MakeTreePortals_r(node_t *node, portaltype_t type, std::list<buildportal_t> boundary_portals,
    portalstats_t &stats, logging::percent_clock &clock);
void MakeTreePortals(tree_t &tree);
std::list<buildportal_t> MakeHeadnodePortals(tree_t &tree);
void MakePortalsFromBuildportals(tree_t &tree, std::list<buildportal_t> &buildportals);
void EmitAreaPortals(tree_t &tree);
void MarkVisibleSides(tree_t &tree, bspbrush_t::container &brushes);
