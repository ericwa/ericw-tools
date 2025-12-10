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

#include "common/bspxfile.hh"

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

#if 0
extern std::atomic<uint32_t> total_light_rays, total_light_ray_hits, total_samplepoints;
extern std::atomic<uint32_t> total_bounce_rays, total_bounce_ray_hits;
extern std::atomic<uint32_t> total_surflight_rays, total_surflight_ray_hits; // mxd
#endif
extern std::atomic<uint32_t> fully_transparent_lightmaps; // write.cc

void PrintFaceInfo(const mface_t *face, const mbsp_t *bsp);
void SetupDirt(settings::worldspawn_keys &cfg);
lightsurf_t CreateLightmapSurface(const mbsp_t *bsp, const mface_t *face, const facesup_t *facesup,
    const bspx_decoupled_lm_perface *facesup_decoupled, const settings::worldspawn_keys &cfg);
bool Face_IsLightmapped(const mbsp_t *bsp, const mface_t *face);
bool Face_IsEmissive(const mbsp_t *bsp, const mface_t *face);
void DirectLightFace(const mbsp_t *bsp, lightsurf_t &lightsurf, const settings::worldspawn_keys &cfg);
void IndirectLightFace(
    const mbsp_t *bsp, lightsurf_t &lightsurf, const settings::worldspawn_keys &cfg, size_t bounce_depth);
void PostProcessLightFace(const mbsp_t *bsp, lightsurf_t &lightsurf, const settings::worldspawn_keys &cfg);

qvec3b round_to_int(const qvec3f &color);

struct lightgrid_sample_t
{
    bool used = false;
    int style = 0;

    // for output to LIGHTGRIDS lump:
    // the light receivied on the 6 faces of a cube, the order of the normals given by BSPX_LIGHTGRIDS_NORMAL_ORDER
    qvec3f colors[6] = {};

    // for output to LIGHTGRID_OCTREE lump: when addding each light, whichever side of the cube
    // receives the most, we use that as the undirectional light amount and add it to this total
    qvec3f undirectional_color = {};

    qvec3b round_to_int() const;
    qvec3b round_to_int(int side) const;
    float brightness() const;

    void add(const qvec3f &color, const qvec3f &grid_to_light_dir, float anglescale);
};

struct lightgrid_samples_t
{
    std::array<lightgrid_sample_t, 4> samples_by_style;
    bool occluded = false;

    void add(const qvec3f &color, int style, const qvec3f &grid_to_light_dir, float anglescale);
    int used_styles() const;

    bspx_lightgrid_samples_t to_bspx_lightgrid_samples() const;
    lightgrids_sampleset_t to_lightgrids_sampleset_t() const;
};

lightgrid_samples_t CalcLightgridAtPoint(const mbsp_t *bsp, const qvec3f &world_point);
void ResetLtFace();
