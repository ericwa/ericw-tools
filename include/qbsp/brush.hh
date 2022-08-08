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
#include <qbsp/qbsp.hh>
#include <common/aabb.hh>
#include <optional>

class mapentity_t;
struct maptexinfo_t;
struct mapface_t;

struct side_t
{
    winding_t w;
    size_t planenum;
    int texinfo;

    int16_t lmshift;

    bool onnode; // has this face been used as a BSP node plane yet?
    bool visible = true; // can any part of this side be seen from non-void parts of the level?
                         // non-visible means we can discard the brush side
                         // (avoiding generating a BSP spit, so expanding it outwards)
    bool bevel; // don't ever use for bsp splitting
    const mapface_t *source; // the mapface we were generated from

    bool tested;

    const maptexinfo_t &get_texinfo() const;
    const qbsp_plane_t &get_plane() const;
    const qbsp_plane_t &get_positive_plane() const;
};

class mapbrush_t;

struct bspbrush_t
{
    /**
     * The brushes in main brush vectors are considered originals. Brush fragments created during
     * the BrushBSP will have this pointing back to the original brush in the list.
     */
    bspbrush_t *original;
    const mapbrush_t *mapbrush;
    uint32_t file_order;
    aabb3d bounds;
    int side, testside; // side of node during construction
    std::vector<side_t> sides;
    contentflags_t contents; /* BSP contents */
    short lmshift; /* lightmap scaling (qu/lightmap pixel), passed to the light util */
    mapentity_t *func_areaportal;

    qvec3d sphere_origin;
    double sphere_radius;

    bool update_bounds();

    std::unique_ptr<bspbrush_t> copy_unique() const;
};

using bspbrush_vector_t = std::vector<std::unique_ptr<bspbrush_t>>;

std::optional<bspbrush_t> LoadBrush(const mapentity_t *src, const mapbrush_t *mapbrush, const contentflags_t &contents, const int hullnum);
bool CreateBrushWindings(bspbrush_t *brush);