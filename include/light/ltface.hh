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

#include <common/cmdlib.hh>
#include <common/mathlib.hh>
#include <common/bspfile.hh>
#include <common/log.hh>
#include <common/threads.hh>
#include <common/polylib.hh>
#include <common/qvec.hh>

#include <light/litfile.hh>
#include <light/trace.hh>
#include <light/entities.hh>

#include <vector>
#include <map>
#include <string>
#include <cassert>
#include <limits>
#include <sstream>
#include <atomic>

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
void DirectLightFace(const mbsp_t *bsp, lightsurf_t &lightsurf, const settings::worldspawn_keys &cfg);
void IndirectLightFace(const mbsp_t *bsp, lightsurf_t &lightsurf, const settings::worldspawn_keys &cfg);
void FinishLightmapSurface(const mbsp_t *bsp, lightsurf_t *lightsurf);
void SaveLightmapSurface(const mbsp_t *bsp, mface_t *face, facesup_t *facesup,
    bspx_decoupled_lm_perface *facesup_decoupled, lightsurf_t *lightsurf, const faceextents_t &extents,
    const faceextents_t &output_extents);

struct lightgrid_sample_t {
    bool used = false;
    int style = 0;
    qvec3d color {};

    qvec3b round_to_int() const;
};

struct lightgrid_samples_t {
    std::array<lightgrid_sample_t, 4> samples_by_style;

    void add(const qvec3d &color, int style);
    int used_styles() const;
};

lightgrid_samples_t CalcLightgridAtPoint(const mbsp_t *bsp, const qvec3d &world_point);
void ResetLtFace();
