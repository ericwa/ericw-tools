/*  Copyright (C) 1996-1997  Id Software, Inc.

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

#include <common/qvec.hh>

#include <atomic>
#include <memory>

struct mface_t;
struct mbsp_t;

namespace settings
{
class worldspawn_keys;
}
struct lightsurf_t;
struct bspx_decoupled_lm_perface;
class faceextents_t;
class light_t;
struct facesup_t;

extern std::atomic<uint32_t> total_light_rays, total_light_ray_hits, total_samplepoints;
extern std::atomic<uint32_t> total_bounce_rays, total_bounce_ray_hits;
extern std::atomic<uint32_t> total_surflight_rays, total_surflight_ray_hits; // mxd
extern std::atomic<uint32_t> fully_transparent_lightmaps;

void PrintFaceInfo(const mface_t *face, const mbsp_t *bsp);
// FIXME: remove light param. add normal param and dir params.
vec_t GetLightValue(const settings::worldspawn_keys &cfg, const light_t *entity, vec_t dist);
void SetupDirt(settings::worldspawn_keys &cfg);
std::unique_ptr<lightsurf_t> CreateLightmapSurface(const mbsp_t *bsp, const mface_t *face, const facesup_t *facesup,
    const bspx_decoupled_lm_perface *facesup_decoupled, const settings::worldspawn_keys &cfg);
bool Face_IsLightmapped(const mbsp_t *bsp, const mface_t *face);
bool Face_IsEmissive(const mbsp_t *bsp, const mface_t *face);
void DirectLightFace(const mbsp_t *bsp, lightsurf_t &lightsurf, const settings::worldspawn_keys &cfg);
void IndirectLightFace(const mbsp_t *bsp, lightsurf_t &lightsurf, const settings::worldspawn_keys &cfg, size_t bounce_depth);
void PostProcessLightFace(const mbsp_t *bsp, lightsurf_t &lightsurf, const settings::worldspawn_keys &cfg);
void FinishLightmapSurface(const mbsp_t *bsp, lightsurf_t *lightsurf);
void SaveLightmapSurface(const mbsp_t *bsp, mface_t *face, facesup_t *facesup,
    bspx_decoupled_lm_perface *facesup_decoupled, lightsurf_t *lightsurf, const faceextents_t &extents,
    const faceextents_t &output_extents);

struct lightgrid_sample_t
{
    bool used = false;
    int style = 0;
    qvec3d color{};

    qvec3b round_to_int() const;
    float brightness() const;

    /**
     * - if !used, style and color are ignored for equality
     * - if a color component is nan, nan is considered equal to nan for the purposes of this comparison
     */
    bool operator==(const lightgrid_sample_t &other) const;
    bool operator!=(const lightgrid_sample_t &other) const; //gcc9 workaround
};

struct lightgrid_samples_t
{
    std::array<lightgrid_sample_t, 4> samples_by_style;

    lightgrid_samples_t &operator+=(const lightgrid_samples_t &other) noexcept;
    lightgrid_samples_t &operator/=(float scale) noexcept;

    void add(const qvec3d &color, int style);
    int used_styles() const;

    bool operator==(const lightgrid_samples_t &other) const;
};

lightgrid_samples_t CalcLightgridAtPoint(const mbsp_t *bsp, const qvec3d &world_point);
void ResetLtFace();
