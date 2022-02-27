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

struct brush_t
{
    aabb3d bounds;
    std::vector<face_t> faces;
    contentflags_t contents; /* BSP contents */
    short lmshift; /* lightmap scaling (qu/lightmap pixel), passed to the light util */
};

class mapbrush_t;

qplane3d Face_Plane(const face_t *face);

enum class rotation_t
{
    none,
    hipnotic,
    origin_brush
};

std::optional<brush_t> LoadBrush(const mapentity_t *src, const mapbrush_t *mapbrush, const contentflags_t &contents,
    const qvec3d &rotate_offset, const rotation_t rottype, const int hullnum);
void FreeBrushes(mapentity_t *ent);

int FindPlane(const qplane3d &plane, int *side);
