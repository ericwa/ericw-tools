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
#include <common/aabb.hh>
#include <optional>
#include <list>
#include <vector>
#include <memory>

class mapentity_t;
struct maptexinfo_t;
struct mapface_t;
struct qbsp_plane_t;

struct side_t
{
    winding_t w;
    size_t planenum;
    int texinfo;

    bool onnode; // has this face been used as a BSP node plane yet?
    bool bevel; // don't ever use for bsp splitting
    mapface_t *source; // the mapface we were generated from

    bool tested;

    side_t clone_non_winding_data() const;
    side_t clone() const;

    bool is_visible() const;
    const maptexinfo_t &get_texinfo() const;
    const qbsp_plane_t &get_plane() const;
    const qbsp_plane_t &get_positive_plane() const;
};

class mapbrush_t;

struct bspbrush_t
{
    using ptr = std::shared_ptr<bspbrush_t>;
    using container = std::vector<ptr>;
    using list = std::list<ptr>;

    template<typename... Args>
    static inline ptr make_ptr(Args&& ...args)
    {
        return std::make_shared<bspbrush_t>(std::forward<Args>(args)...);
    }

    /**
     * The brushes in main brush vectors are considered originals. Brush fragments created during
     * the BrushBSP will have this pointing back to the original brush in the list.
     */
    ptr original_ptr;
    mapbrush_t *mapbrush;

    bspbrush_t *original_brush() { return original_ptr ? original_ptr.get() : this; }
    const bspbrush_t *original_brush() const { return original_ptr ? original_ptr.get() : this; }

    aabb3d bounds;
    int side, testside; // side of node during construction
    std::vector<side_t> sides;
    contentflags_t contents; /* BSP contents */

    qvec3d sphere_origin;
    double sphere_radius;

    bool update_bounds(bool warn_on_failures);

    ptr copy_unique() const;

    bspbrush_t clone() const;
};

std::optional<bspbrush_t> LoadBrush(const mapentity_t *src, mapbrush_t *mapbrush, const contentflags_t &contents, const int hullnum);
bool CreateBrushWindings(bspbrush_t *brush);