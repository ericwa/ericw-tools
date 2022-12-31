/*  Copyright (C) 1996-1997  Id Software, Inc.
Copyright (C) 2018 MaxED

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

#include <vector>
#include <tuple>

struct surfacelight_t
{
    qvec3d pos;
    qvec3f surfnormal;
    /**
     * disables use of the surfnormal. We set this to true on sky surface lights,
     * to avoid black seams on geometry meeting the sky
     */
    bool omnidirectional;
    std::vector<qvec3f> points;
    std::vector<const mleaf_t *> leaves;

    // Surface light settings...
    float intensity; // Surface light strength for each point
    float totalintensity; // Total surface light strength
    qvec3d color; // Surface color

    // Estimated visible AABB culling
    aabb3d bounds;

    int32_t style;

    // rescale faces to account for perpendicular lights
    bool rescale;
};

class light_t;

void ResetSurflight();
std::vector<surfacelight_t> &GetSurfaceLights();
std::optional<std::tuple<int32_t, int32_t, qvec3d, light_t *>> IsSurfaceLitFace(const mbsp_t *bsp, const mface_t *face);
const std::vector<int> &SurfaceLightsForFaceNum(int facenum);
void MakeRadiositySurfaceLights(const settings::worldspawn_keys &cfg, const mbsp_t *bsp);
