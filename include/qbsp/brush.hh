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
#include <qbsp/map.hh>
#include <common/aabb.hh>
#include <optional>

class mapentity_t;
struct maptexinfo_t;

struct side_t
{
    winding_t w;
    qbsp_plane_t plane;
    bool plane_flipped; // whether `plane` is flipped or not
    int texinfo;

    int16_t lmshift;

    bool onnode; // has this face been used as a BSP node plane yet?
    bool visible = true; // can any part of this side be seen from non-void parts of the level?
                         // non-visible means we can discard the brush side
                         // (avoiding generating a BSP spit, so expanding it outwards)
    bool bevel;          // don't ever use for bsp splitting

    bool tested;

    const maptexinfo_t& get_texinfo() const;
};

class mapbrush_t;

struct bspbrush_t {
    /**
     * The brushes in the mapentity_t::brushes vector are considered originals. Brush fragments created during
     * the BrushBSP will have this pointing back to the original brush in mapentity_t::brushes.
     *
     * fixme-brushbsp: this is supposed to be a mapbrush_t
     */
    bspbrush_t *original;
    uint32_t file_order;
    aabb3d bounds;
    int	side, testside; // side of node during construction
    std::vector<side_t> sides;
    contentflags_t contents; /* BSP contents */
    short lmshift; /* lightmap scaling (qu/lightmap pixel), passed to the light util */
    std::optional<uint32_t> outputnumber; /* only set for original brushes */
    mapentity_t *func_areaportal;

    qvec3d sphere_origin;
    double sphere_radius;

    void update_bounds();

    std::unique_ptr<bspbrush_t> copy_unique() const;
};

qplane3d Face_Plane(const face_t *face);
qplane3d Face_Plane(const side_t *face);

enum class rotation_t
{
    none,
    hipnotic,
    origin_brush
};

std::optional<bspbrush_t> LoadBrush(const mapentity_t *src, const mapbrush_t *mapbrush, const contentflags_t &contents,
    const qvec3d &rotate_offset, const rotation_t rottype, const int hullnum);
void FreeBrushes(mapentity_t *ent);
