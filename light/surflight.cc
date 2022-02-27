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
//#include <cstdio>
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
int total_surflight_points = 0;

struct make_surface_lights_args_t
{
    const mbsp_t *bsp;
    const settings::worldspawn_keys *cfg;
};

struct save_winding_points_args_t
{
    vector<qvec3f> *points;
};

static void SaveWindingCenterFn(winding_t &w, void *userinfo)
{
    auto *args = static_cast<save_winding_points_args_t *>(userinfo);

    args->points->push_back(w.center());
}

static void *MakeSurfaceLightsThread(void *arg)
{
    const mbsp_t *bsp = static_cast<make_surface_lights_args_t *>(arg)->bsp;
    const settings::worldspawn_keys &cfg = *static_cast<make_surface_lights_args_t *>(arg)->cfg;

    while (true) {
        const int i = GetThreadWork();
        if (i == -1)
            break;

        const mface_t *face = BSP_GetFace(bsp, i);

        // Face casts light?
        const gtexinfo_t *info = Face_Texinfo(bsp, face);
        if (info == nullptr)
            continue;
        if (!(info->flags.native & Q2_SURF_LIGHT) || info->value == 0) {
            if (info->flags.native & Q2_SURF_LIGHT) {
                qvec3d wc = winding_t::from_face(bsp, face).center();
                LogPrint("WARNING: surface light '{}' at [{}] has 0 intensity.\n", Face_TextureName(bsp, face), wc);
            }
            continue;
        }

        // Create face points...
        auto poly = GLM_FacePoints(bsp, face);
        const float facearea = qv::PolyArea(poly.begin(), poly.end());

        // Avoid small, or zero-area faces
        if (facearea < 1)
            continue;

        // Create winding...
        winding_t winding = winding_t::from_winding_points(poly);
        winding.remove_colinear();

        // Get face normal and midpoint...
        qvec3d facenormal = Face_Normal(bsp, face);
        qvec3d facemidpoint = winding.center() + facenormal; // Lift 1 unit

        // Dice winding...
        vector<qvec3f> points;
        save_winding_points_args_t args{};
        args.points = &points;

        winding.dice(cfg.surflightsubdivision.value(), SaveWindingCenterFn, &args);
        total_surflight_points += points.size();

        // Get texture color
        qvec3f texturecolor = qvec3f(Face_LookupTextureColor(bsp, face)) / 255.f;

        // Calculate emit color and intensity...

        // Handle arghrad sky light settings http://www.bspquakeeditor.com/arghrad/sunlight.html#sky
        if (info->flags.native & Q2_SURF_SKY) {
            // FIXME: this only handles the "_sky_surface"  "red green blue" format.
            //        There are other more complex variants we could handle documented in the link above.
            // FIXME: we require value to be nonzero, see the check above - not sure if this matches arghrad
            if (cfg.sky_surface.isChanged()) {
                texturecolor = cfg.sky_surface.value();
            }
        }

        texturecolor *= info->value; // Scale by light value

        // Calculate intensity...
        float intensity = qv::max(texturecolor);

        if (intensity == 0.0f)
            continue;

        // Normalize color...
        if (intensity > 1.0f)
            texturecolor *= 1.0f / intensity;

        // Sanity checks...
        Q_assert(!points.empty());

        // Add surfacelight...
        surfacelight_t l;
        l.surfnormal = facenormal;
        l.omnidirectional = (info->flags.native & Q2_SURF_SKY) ? true : false;
        l.points = points;
        l.pos = facemidpoint;

        // Store surfacelight settings...
        l.totalintensity = intensity * facearea;
        l.intensity = l.totalintensity / points.size();
        l.color = texturecolor;

        // Init bbox...
        l.bounds = qvec3d(0);

        if (!options.novisapprox.value())
            l.bounds = EstimateVisibleBoundsAtPoint(facemidpoint);

        // Store light...
        unique_lock<mutex> lck{surfacelights_lock};
        surfacelights.push_back(l);

        const int index = static_cast<int>(surfacelights.size()) - 1;
        surfacelightsByFacenum[Face_GetNum(bsp, face)].push_back(index);
    }

    return nullptr;
}

const std::vector<surfacelight_t> &SurfaceLights()
{
    return surfacelights;
}

int TotalSurfacelightPoints()
{
    return total_surflight_points;
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
MakeSurfaceLights(const settings::worldspawn_keys &cfg, const mbsp_t *bsp)
{
    LogPrint("--- MakeSurfaceLights ---\n");

    make_surface_lights_args_t args{bsp, &cfg};
    RunThreadsOn(0, bsp->dfaces.size(), MakeSurfaceLightsThread, static_cast<void *>(&args));
}
