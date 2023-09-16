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
#include <optional>
#include <tuple>

#include <common/qvec.hh>
#include <common/aabb.hh>

struct mleaf_t;
struct mface_t;
struct mbsp_t;
namespace settings
{
class worldspawn_keys;
}

struct surfacelight_t
{
    qvec3d pos;
    qvec3f surfnormal;
    size_t points_before_culling;

    // Estimated visible AABB culling
    aabb3d bounds;

    std::optional<vec_t> minlight_scale;

    std::vector<qvec3f> points;
    std::vector<const mleaf_t *> leaves;

    // Surface light settings...
    struct per_style_t
    {
        std::optional<size_t> bounce_level = std::nullopt; // whether this is a direct or indirect emission
        /**
         * disables use of the surfnormal. We set this to true on sky surface lights,
         * to avoid black seams on geometry meeting the sky
         */
        bool omnidirectional = false;
        // rescale faces to account for perpendicular lights
        bool rescale = false;
        int32_t style = 0; // style ID
        float intensity = 0; // Surface light strength for each point
        float totalintensity = 0; // Total surface light strength
        qvec3d color; // Surface color
    };

    // Light data per style
    std::vector<per_style_t> styles;
};

class light_t;

void ResetSurflight();
size_t GetSurflightPoints();
std::optional<std::tuple<int32_t, int32_t, qvec3d, light_t *>> IsSurfaceLitFace(const mbsp_t *bsp, const mface_t *face);
const std::vector<int> &SurfaceLightsForFaceNum(int facenum);
void MakeRadiositySurfaceLights(const settings::worldspawn_keys &cfg, const mbsp_t *bsp);
