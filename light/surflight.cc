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

#include <cassert>
#include <cstdio>
#include <iostream>

#include <light/light.hh>
#include <light/bounce.hh>
#include <light/surflight.hh>
#include <light/ltface.hh>

#include <common/polylib.hh>
#include <common/bsputils.hh>

#include <vector>
#include <map>
#include <mutex>
#include <string>

#include <common/qvec.hh>

using namespace std;
using namespace polylib;

mutex surfacelights_lock;
std::vector<surfacelight_t> surfacelights;
std::map<int, std::vector<int>> surfacelightsByFacenum;

struct make_surface_lights_args_t {
    const mbsp_t *bsp;
    const globalconfig_t *cfg;
};

static void
AddSurfaceLight(const mbsp_t *bsp, const bsp2_dface_t *face, const float area, const vec3_t pos, const vec3_t surfnormal, const vec3_t color, const float lightvalue)
{
    surfacelight_t l;
    l.poly = GLM_FacePoints(bsp, face);
    l.poly_edgeplanes = GLM_MakeInwardFacingEdgePlanes(l.poly);
    l.pos = vec3_t_to_glm(pos);
    l.areascaler = ((area / 4.0f) / 128.0f);

    // Store surfacelight settings...
    l.value = lightvalue;
    VectorCopy(color, l.color);

    l.surfnormal = vec3_t_to_glm(surfnormal);
    VectorSet(l.mins, 0, 0, 0);
    VectorSet(l.maxs, 0, 0, 0);

    if (!novisapprox)
        EstimateVisibleBoundsAtPoint(pos, l.mins, l.maxs);

    unique_lock<mutex> lck{ surfacelights_lock };
    surfacelights.push_back(l);

    const int index = static_cast<int>(surfacelights.size()) - 1;
    surfacelightsByFacenum[Face_GetNum(bsp, face)].push_back(index);
}

static void *
MakeSurfaceLightsThread(void *arg)
{
    const mbsp_t *bsp = static_cast<make_surface_lights_args_t *>(arg)->bsp;

    while (true) {
        const int i = GetThreadWork();
        if (i == -1) break;

        const bsp2_dface_t *face = BSP_GetFace(bsp, i);

        // Face casts light?
        const gtexinfo_t *info = Face_Texinfo(bsp, face);
        if (info == nullptr) continue;
        if (!(info->flags & Q2_SURF_LIGHT) || info->value == 0) {
            if (info->flags & Q2_SURF_LIGHT) {
                vec3_t wc;
                WindingCenter(WindingFromFace(bsp, face), wc);
                logprint("WARNING: surface light '%s' at [%s] has 0 intensity.\n", Face_TextureName(bsp, face), VecStr(wc));
            }
            continue;
        }

        // Grab some info about the face winding
        winding_t *winding = WindingFromFace(bsp, face);
        const float facearea = WindingArea(winding);

        // Avoid small, or zero-area faces
        if (facearea < 1) continue;

        plane_t faceplane;
        WindingPlane(winding, faceplane.normal, &faceplane.dist);

        vec3_t facemidpoint;
        WindingCenter(winding, facemidpoint);
        VectorMA(facemidpoint, 1, faceplane.normal, facemidpoint); // lift 1 unit

        // Get texture color
        vec3_t blendedcolor = { 0, 0, 0 };
        vec3_t texturecolor;
        Face_LookupTextureColor(bsp, face, texturecolor);

        // Calculate Q2 surface light color and strength
        const float scaler = info->value / 256.0f; // Playing by the eye here...
        for (int k = 0; k < 3; k++)
            blendedcolor[k] = texturecolor[k] * scaler / 255.0f; // Scale by light value, convert to [0..1] range...

        AddSurfaceLight(bsp, face, facearea, facemidpoint, faceplane.normal, blendedcolor, info->value);
    }

    return nullptr;
}

const std::vector<surfacelight_t> &SurfaceLights()
{
    return surfacelights;
}

// No surflight_debug (yet?), so unused...
const std::vector<int> &SurfaceLightsForFaceNum(int facenum)
{
    const auto &vec = surfacelightsByFacenum.find(facenum);
    if (vec != surfacelightsByFacenum.end()) 
        return vec->second;

    static std::vector<int> empty;
    return empty;
}

void // Quake 2 surface lights
MakeSurfaceLights(const globalconfig_t &cfg, const mbsp_t *bsp)
{
    logprint("--- MakeSurfaceLights ---\n");

    make_surface_lights_args_t args { bsp,  &cfg };
    RunThreadsOn(0, bsp->numfaces, MakeSurfaceLightsThread, static_cast<void *>(&args));
}