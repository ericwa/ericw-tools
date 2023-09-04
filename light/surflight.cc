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

#include <light/surflight.hh>

#include <cassert>

#include <light/entities.hh> // for FixLightOnFace
#include <light/trace.hh> // for Light_PointInLeaf
#include <light/light.hh>
#include <light/ltface.hh>

#include <common/polylib.hh>
#include <common/bsputils.hh>
#include <common/parallel.hh>

#include <vector>
#include <map>
#include <mutex>

#include <common/qvec.hh>

static std::atomic_size_t total_surflight_points;

void ResetSurflight()
{
    total_surflight_points = {};
}

size_t GetSurflightPoints()
{
    return total_surflight_points;
}

int LightStyleForTargetname(const settings::worldspawn_keys &cfg, const std::string &targetname);

static void MakeSurfaceLight(const mbsp_t *bsp, const settings::worldspawn_keys &cfg, const mface_t *face,
    std::optional<qvec3f> texture_color, bool is_directional, bool is_sky, int32_t style, int32_t light_value)
{
    auto &surf_ptr = LightSurfaces()[face - bsp->dfaces.data()];

    if (!surf_ptr || !Face_IsEmissive(bsp, face)) {
        return;
    }

    auto &surf = *surf_ptr.get();

    // Create face points...
    auto poly = Face_Points(bsp, face);
    const float facearea = qv::PolyArea(poly.begin(), poly.end());

    const surfflags_t &extended_flags = extended_texinfo_flags[face->texinfo];

    // Avoid small, or zero-area faces
    if (facearea < 1)
        return;

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

    if (!surf.vpl) {
        auto &l = surf.vpl = std::make_unique<surfacelight_t>();

        // Create winding...
        auto winding = polylib::winding_t::from_winding_points(poly);
        auto face_modelinfo = ModelInfoForFace(bsp, face - bsp->dfaces.data());

        for (auto &pt : winding) {
            pt += face_modelinfo->offset;
        }

        winding.remove_colinear();

        // Get face normal and midpoint...
        l->surfnormal = Face_Normal(bsp, face);
        l->pos = winding.center() + l->surfnormal; // Lift 1 unit

        // Dice winding...
        l->points_before_culling = 0;

        if (light_options.emissivequality.value() == emissivequality_t::LOW ||
            light_options.emissivequality.value() == emissivequality_t::MEDIUM) {
            l->points = {l->pos};
            l->points_before_culling++;
            total_surflight_points++;

            if (light_options.emissivequality.value() == emissivequality_t::MEDIUM) {

                for (auto &pt : winding) {
                    l->points_before_culling++;
                    auto point = pt + l->surfnormal;
                    auto diff = qv::normalize(l->pos - pt);

                    point += diff;

                    // optimization - cull surface lights in the void
                    // also try to move them if they're slightly inside a wall
                    auto [fixed_point, success] = FixLightOnFace(bsp, point, false, 0.5f);
                    if (!success) {
                        continue;
                    }

                    l->points.push_back(fixed_point);
                    total_surflight_points++;
                }
            }
        } else {
            winding.dice(cfg.surflightsubdivision.value(), [&](polylib::winding_t &w) {
                ++l->points_before_culling;

                qvec3f point = w.center() + l->surfnormal;

                // optimization - cull surface lights in the void
                // also try to move them if they're slightly inside a wall
                auto [fixed_point, success] = FixLightOnFace(bsp, point, false, 0.5f);
                if (!success) {
                    return;
                }

                l->points.push_back(fixed_point);
                ++total_surflight_points;
            });
        }

        l->minlight_scale = extended_flags.surflight_minlight_scale;

        // Init bbox...
        if (light_options.visapprox.value() == visapprox_t::RAYS) {
            l->bounds = EstimateVisibleBoundsAtPoint(l->pos);
        }

        for (auto &pt : l->points) {
            if (light_options.visapprox.value() == visapprox_t::VIS) {
                l->leaves.push_back(Light_PointInLeaf(bsp, pt));
            } else if (light_options.visapprox.value() == visapprox_t::RAYS) {
                l->bounds += EstimateVisibleBoundsAtPoint(pt);
            }
        }
    }

    auto &l = surf.vpl;

    // Sanity checks...
    if (l->points.empty()) {
        return;
    }

    // Add surfacelight...
    auto &setting = l->styles.emplace_back();
    setting.omnidirectional = !is_directional;
    if (extended_flags.surflight_targetname) {
        setting.style = LightStyleForTargetname(cfg, extended_flags.surflight_targetname.value());
    } else if (extended_flags.surflight_style) {
        setting.style = extended_flags.surflight_style.value();
    } else {
        setting.style = style;
    }
    if (extended_flags.surflight_rescale) {
        setting.rescale = extended_flags.surflight_rescale.value();
    } else {
        setting.rescale = is_sky ? false : true;
    }

    // Store surfacelight settings...
    setting.totalintensity = intensity * facearea;
    setting.intensity = setting.totalintensity / l->points_before_culling;
    setting.color = texture_color.value();
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
                    qvec3d wc = polylib::winding_t::from_face(bsp, face).center();
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

void // Quake 2 surface lights
MakeRadiositySurfaceLights(const settings::worldspawn_keys &cfg, const mbsp_t *bsp)
{
    logging::funcheader();

    logging::parallel_for(
        static_cast<size_t>(0), bsp->dfaces.size(), [&](size_t i) { MakeSurfaceLightsThread(bsp, cfg, i); });

    /*if (surfacelights.size()) {
        logging::print("{} surface lights ({} light points) in use.\n", surfacelights.size(), total_surflight_points);
    }*/
}
