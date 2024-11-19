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

#include <light/bounce.hh>

#include <cstdint>
#include <atomic>

#include <light/light.hh>
#include <light/entities.hh> // for EstimateVisibleBoundsAtPoint
#include <light/ltface.hh>
#include <light/surflight.hh>
#include <light/trace.hh> // for Light_PointInLeaf
#include <light/trace_embree.hh> // for ShadowCastingSolidFacesSet

#include <common/polylib.hh>
#include <common/bsputils.hh>

#include <vector>
#include <unordered_map>
#include <mutex>

#include <common/qvec.hh>
#include <common/parallel.hh>

static bool Face_ShouldBounce(const mbsp_t *bsp, const mface_t *face)
{
    // make bounce light, only if this face is shadow casting

    // NOTE: this should be the same set of faces that are unconditionally shadow casting
    // (so exclude fences, water, etc.) which is why we're currently fetching it from the embree
    // code, because the condition is quite complex and we don't want to try to repeat it here.
    auto &face_set = ShadowCastingSolidFacesSet();
    if (face_set.find(face) == face_set.end())
        return false;

    if (!Face_IsLightmapped(bsp, face)) {
        return false;
    }

    const char *texname = Face_TextureName(bsp, face);
    if (!Q_strcasecmp("skip", texname)) {
        return false;
    }

    // check for "_bounce" "-1"
    const auto &ext_info = extended_texinfo_flags[face->texinfo];
    if (ext_info.no_bounce) {
        return false;
    }

    // don't bounce *from* emission surfaces
    if (IsSurfaceLitFace(bsp, face)) {
        return false;
    }

    return true;
}

static void MakeBounceLight(const mbsp_t *bsp, const settings::worldspawn_keys &cfg, lightsurf_t &surf,
    qvec3f texture_color, int32_t style, std::vector<qvec3f> &points, float area, const qvec3f &facenormal,
    const qvec3f &facemidpoint, size_t depth)
{
    if (!Face_IsEmissive(bsp, surf.face)) {
        return;
    }

    // Calculate emit color and intensity...

    // Calculate intensity...
    float intensity = qv::max(texture_color);

    if (intensity <= 0.0f) {
        return;
    }

    // Normalize color...
    if (intensity > 1.0f) {
        texture_color *= 1.0f / intensity;
    }

    if (!surf.vpl) {
        auto &l = surf.vpl = std::make_unique<surfacelight_t>();

        // Sanity checks...
        Q_assert(!points.empty());

        // Add surfacelight...
        l->surfnormal = facenormal;
        l->points = std::move(points);

        // Init bbox...
        if (light_options.visapprox.value() == visapprox_t::RAYS) {
            l->bounds = EstimateVisibleBoundsAtPoint(facemidpoint);

            for (auto &pt : l->points) {
                l->bounds += EstimateVisibleBoundsAtPoint(pt);
            }
        }

        l->pos = facemidpoint;
    }

    // Store surfacelight settings...
    {
        auto &l = surf.vpl;
        auto &setting = l->styles.emplace_back();
        setting.bounce_level = depth;
        setting.style = style;
        setting.totalintensity = intensity * area;
        setting.intensity = setting.totalintensity / l->points.size();
        setting.color = texture_color;
    }
}

static bool MakeBounceLightsThread(
    const settings::worldspawn_keys &cfg, const mbsp_t *bsp, const mface_t &face, size_t depth)
{
    if (!Face_ShouldBounce(bsp, &face)) {
        return false;
    }

    auto &surf = LightSurfaces()[&face - bsp->dfaces.data()];

    // no lights
    if (!surf.lightmapsByStyle.size()) {
        return false;
    }

    auto winding = polylib::winding3f_t::from_face(bsp, &face);
    float area = winding.area();

    if (area < 1.f) {
        return false;
    }

    // Create winding...
    winding.remove_colinear();

    // grab the average color across the whole set of lightmaps for this face.
    // this doesn't change regardless of the above settings.
    std::unordered_map<int, qvec3f> sum;
    float sample_divisor = surf.lightmapsByStyle.front().samples.size();

    bool has_any_color = false;

    for (auto &lightmap : surf.lightmapsByStyle) {

        if (lightmap.style && !cfg.bouncestyled.value()) {
            continue;
        }

        if (!qv::emptyExact(lightmap.bounce_color)) {
            sum[lightmap.style] = lightmap.bounce_color / sample_divisor;
            has_any_color = true;
        }

        // clear bounced color from lightmap since we
        // have "counted" it
        lightmap.bounce_color = {};
    }

    // no bounced color, we can leave early
    if (!has_any_color) {
        return false;
    }

    // lerp between gray and the texture color according to `bouncecolorscale` (0 = use gray, 1 = use texture color)
    const qvec3f &blendedcolor = Face_LookupTextureBounceColor(bsp, &face);

    // final colors to emit
    std::unordered_map<int, qvec3f> emitcolors;

    for (const auto &styleColor : sum) {
        emitcolors[styleColor.first] = styleColor.second * blendedcolor;
    }

    qplane3d faceplane = winding.plane();

    // Get face normal and midpoint...
    qvec3f facenormal = faceplane.normal;
    qvec3f facemidpoint = winding.center() + facenormal; // Lift 1 unit

    std::vector<qvec3f> points;

    if (light_options.emissivequality.value() == emissivequality_t::LOW ||
        light_options.emissivequality.value() == emissivequality_t::MEDIUM) {
        points = {facemidpoint};

        if (light_options.emissivequality.value() == emissivequality_t::MEDIUM) {

            for (auto &pt : winding) {
                points.push_back(pt + faceplane.normal);
            }
        }
    } else {
        winding.dice(cfg.bouncelightsubdivision.value(),
            [&points, &faceplane](polylib::winding3f_t &w) { points.push_back(w.center() + faceplane.normal); });
    }

    for (auto &style : emitcolors) {
        MakeBounceLight(bsp, cfg, surf, style.second, style.first, points, area, facenormal, facemidpoint, depth);
    }

    return true;
}

bool MakeBounceLights(const settings::worldspawn_keys &cfg, const mbsp_t *bsp, size_t depth)
{
    logging::funcheader();

    std::atomic_bool any_to_bounce = false;

    logging::parallel_for_each(bsp->dfaces,
        [&](const mface_t &face) { any_to_bounce = MakeBounceLightsThread(cfg, bsp, face, depth) || any_to_bounce; });

    return any_to_bounce.load();
}
