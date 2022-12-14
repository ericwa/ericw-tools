/*  Copyright (C) 1996-1997  Id Software, Inc.
    Copyright (C) 2017 Eric Wasylishen

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

#include <cstdint>
//#include <cstdio>
#include <atomic>

#include <light/light.hh>
#include <light/bounce.hh>
#include <light/ltface.hh>
#include <light/surflight.hh>

#include <common/polylib.hh>
#include <common/bsputils.hh>

#include <vector>
#include <unordered_map>
#include <mutex>

#include <common/qvec.hh>
#include <common/parallel.hh>

using namespace std;
using namespace polylib;

mutex bouncelights_lock;
static std::vector<surfacelight_t> bouncelights;
static std::atomic_size_t bouncelightpoints;

void ResetBounce()
{
    bouncelights.clear();
    bouncelightpoints = 0;
}

static bool Face_ShouldBounce(const mbsp_t *bsp, const mface_t *face)
{
    // make bounce light, only if this face is shadow casting
    const modelinfo_t *mi = ModelInfoForFace(bsp, Face_GetNum(bsp, face));
    if (!mi || !mi->shadow.boolValue()) {
        return false;
    }

    if (!Face_IsLightmapped(bsp, face)) {
        return false;
    }

    const char *texname = Face_TextureName(bsp, face);
    if (!Q_strcasecmp("skip", texname)) {
        return false;
    }

    // check for "_bounce" "-1"
    if (extended_texinfo_flags[face->texinfo].no_bounce) {
        return false;
    }

    // don't bounce *from* emission surfaces
    if (IsSurfaceLitFace(bsp, face)) {
        return false;
    }

    if (mi->object_channel_mask.value() != CHANNEL_MASK_DEFAULT) {
        return false;
    }

    return true;
}

static void MakeBounceLight(const mbsp_t *bsp, const settings::worldspawn_keys &cfg, const mface_t *face,
    qvec3d texture_color, int32_t style, const std::vector<qvec3f> &points, const winding_t &winding, const vec_t &area,
    const qvec3d &facenormal, const qvec3d &facemidpoint)
{
    bouncelightpoints += points.size();

    // Calculate emit color and intensity...

    // Calculate intensity...
    vec_t intensity = qv::max(texture_color);

    if (intensity <= 0.0) {
        return;
    }

    // Normalize color...
    if (intensity > 1.0) {
        texture_color *= 1.0 / intensity;
    }

    // Sanity checks...
    Q_assert(!points.empty());

    // Add surfacelight...
    surfacelight_t l;
    l.surfnormal = facenormal;
    l.omnidirectional = false;
    l.points = points;
    l.style = style;

    // Init bbox...
    if (light_options.visapprox.value() == visapprox_t::RAYS) {
        l.bounds = EstimateVisibleBoundsAtPoint(facemidpoint);
    }

    for (auto &pt : l.points) {
        if (light_options.visapprox.value() == visapprox_t::VIS) {
            l.leaves.push_back(Light_PointInLeaf(bsp, pt));
        } else if (light_options.visapprox.value() == visapprox_t::RAYS) {
            l.bounds += EstimateVisibleBoundsAtPoint(pt);
        }
    }

    l.pos = facemidpoint;

    // Store surfacelight settings...
    l.totalintensity = intensity * area;
    l.intensity = l.totalintensity / l.points.size();
    l.color = texture_color;

    // Store light...
    unique_lock<mutex> lck{bouncelights_lock};
    bouncelights.push_back(l);
}

const std::vector<surfacelight_t> &BounceLights()
{
    return bouncelights;
}

static void MakeBounceLightsThread(const settings::worldspawn_keys &cfg, const mbsp_t *bsp, const mface_t &face)
{
    if (!Face_ShouldBounce(bsp, &face)) {
        return;
    }

    auto &surf_ptr = LightSurfaces()[&face - bsp->dfaces.data()];

    if (!surf_ptr) {
        return;
    }

    auto &surf = *surf_ptr.get();

    // no lights
    if (!surf.lightmapsByStyle.size()) {
        return;
    }

    winding_t winding = winding_t::from_face(bsp, &face);
    vec_t area = winding.area();

    if (area < 1.f) {
        return;
    }

    // Create winding...
    winding.remove_colinear();

    // grab the average color across the whole set of lightmaps for this face.
    // this doesn't change regardless of the above settings.
    std::unordered_map<int, qvec3d> sum;
    vec_t sample_divisor = surf.lightmapsByStyle.front().samples.size();

    bool has_any_color = false;

    for (const auto &lightmap : surf.lightmapsByStyle) {

        if (lightmap.style && !cfg.bouncestyled.value()) {
            continue;
        }

        for (auto &sample : lightmap.samples) {
            sum[lightmap.style] += sample.color;
        }
    }

    for (auto &sample : sum) {
        if (!qv::emptyExact(sample.second)) {
            sample.second /= sample_divisor;
            has_any_color = true;
        }
    }

    // no bounced color, we can leave early
    if (!has_any_color) {
        return;
    }

    // lerp between gray and the texture color according to `bouncecolorscale` (0 = use gray, 1 = use texture color)
    const qvec3d &blendedcolor = Face_LookupTextureBounceColor(bsp, &face);

    // final colors to emit
    std::unordered_map<int, qvec3d> emitcolors;

    for (const auto &styleColor : sum) {
        emitcolors[styleColor.first] = styleColor.second * blendedcolor;
    }
    
    qplane3d faceplane = winding.plane();

    // Get face normal and midpoint...
    qvec3d facenormal = faceplane.normal;
    qvec3d facemidpoint = winding.center() + facenormal; // Lift 1 unit

    if (light_options.fastbounce.value()) {
        vector<qvec3f> points { facemidpoint };

        for (auto &style : emitcolors) {
            MakeBounceLight(bsp, cfg, &face, style.second, style.first, points, winding, area, facenormal, facemidpoint);
        }
    } else {
        vector<qvec3f> points;
        winding.dice(cfg.bouncelightsubdivision.value(),
            [&points, &faceplane](winding_t &w) { points.push_back(w.center() + faceplane.normal); });

        for (auto &style : emitcolors) {
            MakeBounceLight(bsp, cfg, &face, style.second, style.first, points, winding, area, facenormal, facemidpoint);
        }
    }
}

void MakeBounceLights(const settings::worldspawn_keys &cfg, const mbsp_t *bsp)
{
    logging::funcheader();

    logging::parallel_for_each(bsp->dfaces, [&](const mface_t &face) { MakeBounceLightsThread(cfg, bsp, face); });

    logging::print("{} bounce lights created, with {} points\n", bouncelights.size(), bouncelightpoints);
}
