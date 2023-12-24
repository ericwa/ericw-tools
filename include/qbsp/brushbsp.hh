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

#include <qbsp/winding.hh>
#include <common/qvec.hh>
#include <qbsp/brush.hh>
#include <qbsp/qbsp.hh>

#include <atomic>
#include <list>
#include <optional>
#include <memory>

struct bspbrush_t;
struct node_t;
struct face_t;
class mapentity_t;
struct tree_t;

constexpr double EDGE_LENGTH_EPSILON = 0.2;

/*
================
WindingIsTiny

Returns true if the winding would be crunched out of
existance by the vertex snapping.
================
*/
template<typename T>
bool WindingIsTiny(const T &w, double size = EDGE_LENGTH_EPSILON)
{
    size_t edges = 0;
    for (size_t i = 0; i < w.size(); i++) {
        size_t j = (i + 1) % w.size();
        const qvec3d delta = w[j] - w[i];
        const double len = qv::length(delta);
        if (len > size) {
            if (++edges == 3) {
                return false;
            }
        }
    }
    return true;
}

/*
================
WindingIsHuge

Returns true if the winding still has one of the points
from basewinding for plane
================
*/
template<typename T>
bool WindingIsHuge(const T &w)
{
    for (size_t i = 0; i < w.size(); i++) {
        for (size_t j = 0; j < 3; j++) {
            if (fabs(w[i][j]) > qbsp_options.worldextent.value()) {
                return true;
            }
        }
    }
    return false;
}

enum tree_split_t
{
    // change the split type depending on node size,
    // brush count, etc
    AUTO,
    // always use the precise/expensive split method
    // to make a good BSP tree
    PRECISE,
    // always use faster methods to create the tree
    FAST
};

double BrushVolume(const bspbrush_t &brush);
bspbrush_t::ptr BrushFromBounds(const aabb3d &bounds);
void BrushBSP(tree_t &tree, mapentity_t &entity, const bspbrush_t::container &brushes, tree_split_t split_type);
void ChopBrushes(bspbrush_t::container &brushes, bool allow_fragmentation);