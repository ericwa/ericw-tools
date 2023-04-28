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

#include <light/light.hh>
#include <light/surflight.hh>
#include <light/ltface.hh>

#include <common/polylib.hh>
#include <common/bsputils.hh>
#include <common/parallel.hh>

#include <vector>
#include <map>
#include <mutex>

#include <common/qvec.hh>

using namespace std;
using namespace polylib;

static mutex surfacelights_lock;
static std::vector<surfacelight_t> surfacelights;
static std::map<int, std::vector<int>> surfacelightsByFacenum;
static size_t total_surflight_points = 0;

void ResetSurflight()
{
    surfacelights = {};
    surfacelightsByFacenum = {};
    total_surflight_points = {};
}

std::vector<surfacelight_t> &GetSurfaceLights()
{
    return surfacelights;
}

size_t GetSurflightPoints()
{
    return total_surflight_points;
}

static void MakeSurfaceLight(const mbsp_t *bsp, const settings::worldspawn_keys &cfg, const mface_t *face,
    std::optional<qvec3f> texture_color, bool is_directional, bool is_sky, int32_t style, int32_t light_value)
{
    // Create face points...
    auto poly = GLM_FacePoints(bsp, face);
    const float facearea = qv::PolyArea(poly.begin(), poly.end());

    const surfflags_t &extended_flags = extended_texinfo_flags[face->texinfo];

    // Avoid small, or zero-area faces
    if (facearea < 1)
        return;

    // Create winding...
    winding_t winding = winding_t::from_winding_points(poly);
    winding.remove_colinear();

    // Get face normal and midpoint...
    qvec3d facenormal = Face_Normal(bsp, face);
    qvec3d facemidpoint = winding.center() + facenormal; // Lift 1 unit

    // Dice winding...
    vector<qvec3f> points;
    size_t points_before_culling = 0;
    winding.dice(cfg.surflightsubdivision.value(), [&](winding_t &w) {
        ++points_before_culling;

        qvec3f point = w.center() + facenormal;

        // optimization - cull surface lights in the void
        // also try to move them if they're slightly inside a wall
        auto [fixed_point, success] = FixLightOnFace(bsp, point, false, 0.5f);
        if (!success) {
            return;
        }

        points.push_back(fixed_point);
    });

    // Calculate emit color and intensity...

    if (extended_flags.surflight_color.has_value()) {
        texture_color = extended_flags.surflight_color.value();
    } else {
        // Handle arghrad sky light settings http://www.bspquakeeditor.com/arghrad/sunlight.html#sky
        if (!texture_color.has_value()) {
        if (cfg.sky_surface.is_changed() && is_sky) {
                // FIXME: this only handles the "_sky_surface"  "red green blue" format.
                //        There are other more complex variants we could handle documented in the link above.
                // FIXME: we require value to be nonzero, see the check above - not sure if this matches arghrad
                texture_color = cfg.sky_surface.value() * 255.0;
            } else {
                texture_color = qvec3f(Face_LookupTextureColor(bsp, face));
            }
        }
    }

    texture_color.value() /= 255.0;
    texture_color.value() *= light_value; // Scale by light value

    // Calculate intensity...
    float intensity = qv::max(texture_color.value());

    if (intensity == 0.0f)
        return;

    // Normalize color...
    if (intensity > 1.0f)
        texture_color.value() *= 1.0f / intensity;

    // Sanity checks...
    if (points.empty())
        return;

    // Add surfacelight...
    surfacelight_t l;
    l.surfnormal = facenormal;
    l.omnidirectional = !is_directional;
    l.points = std::move(points);
    if (extended_flags.surflight_style) {
        l.style = extended_flags.surflight_style.value();
    } else {
        l.style = style;
    }
    l.rescale = extended_flags.surflight_rescale;
    l.minlight_scale = extended_flags.surflight_minlight_scale;

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
    l.totalintensity = intensity * facearea;
    l.intensity = l.totalintensity / points_before_culling;
    l.color = texture_color.value();

    // Store light...
    unique_lock<mutex> lck{surfacelights_lock};
    total_surflight_points += l.points.size();
    surfacelights.push_back(l);

    const int index = static_cast<int>(surfacelights.size()) - 1;
    surfacelightsByFacenum[Face_GetNum(bsp, face)].push_back(index);
}

std::optional<std::tuple<int32_t, int32_t, qvec3d, light_t *>> IsSurfaceLitFace(const mbsp_t *bsp, const mface_t *face)
{
    if (bsp->loadversion->game->id == GAME_QUAKE_II) {
        // first, check if it's a Q2 surface
        const mtexinfo_t *info = Face_Texinfo(bsp, face);

        if (info != nullptr && (info->flags.native & Q2_SURF_LIGHT) && info->value > 0) {
            return std::make_tuple(info->value, 0, qvec3d(Face_LookupTextureColor(bsp, face)), nullptr);
        }
    }

    for (const auto &surflight : GetSurfaceLightTemplates()) {
        if (FaceMatchesSurfaceLightTemplate(
                bsp, face, ModelInfoForFace(bsp, face - bsp->dfaces.data()), *surflight, SURFLIGHT_RAD)) {
            return std::make_tuple(surflight->light.value(), surflight->style.value(),
                surflight->color.is_changed() ? surflight->color.value() : qvec3d(Face_LookupTextureColor(bsp, face)),
                surflight.get());
        }
    }

    return std::nullopt;
}

static void MakeSurfaceLightsThread(const mbsp_t *bsp, const settings::worldspawn_keys &cfg, size_t i)
{
    const mface_t *face = BSP_GetFace(bsp, i);

    // Face casts light?

    if (bsp->loadversion->game->id == GAME_QUAKE_II) {
        // first, check if it's a Q2 surface
        const mtexinfo_t *info = Face_Texinfo(bsp, face);

        if (info != nullptr) {
            if (!(info->flags.native & Q2_SURF_LIGHT) || info->value == 0) {
                if (info->flags.native & Q2_SURF_LIGHT) {
                    qvec3d wc = winding_t::from_face(bsp, face).center();
                    logging::print(
                        "WARNING: surface light '{}' at [{}] has 0 intensity.\n", Face_TextureName(bsp, face), wc);
                }
            } else {
                MakeSurfaceLight(bsp, cfg, face, std::nullopt, !(info->flags.native & Q2_SURF_SKY),
                    (info->flags.native & Q2_SURF_SKY), 0, info->value);
            }
        }
    }

    // check matching templates
    for (const auto &surflight : GetSurfaceLightTemplates()) {
        if (FaceMatchesSurfaceLightTemplate(
                bsp, face, ModelInfoForFace(bsp, face - bsp->dfaces.data()), *surflight, SURFLIGHT_RAD)) {
            std::optional<qvec3f> texture_color;

            if (surflight->color.is_changed()) {
                texture_color = surflight->color.value();
            }

            MakeSurfaceLight(bsp, cfg, face, texture_color,
                !surflight->epairs->has("_surface_spotlight") ? true
                                                              : !!surflight->epairs->get_int("_surface_spotlight"),
                surflight->epairs->get_int("_surface_is_sky"), surflight->epairs->get_int("style"),
                surflight->light.value());
        }
    }
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
MakeRadiositySurfaceLights(const settings::worldspawn_keys &cfg, const mbsp_t *bsp)
{
    logging::funcheader();

    logging::parallel_for(
        static_cast<size_t>(0), bsp->dfaces.size(), [&](size_t i) { MakeSurfaceLightsThread(bsp, cfg, i); });

    if (surfacelights.size()) {
        logging::print("{} surface lights ({} light points) in use.\n", surfacelights.size(), total_surflight_points);
    }
}
