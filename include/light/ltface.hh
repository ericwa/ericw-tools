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

#ifndef __LIGHT_LTFACE_H__
#define __LIGHT_LTFACE_H__

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
extern std::atomic<uint32_t> total_surflight_rays, total_surflight_ray_hits; //mxd
extern std::atomic<uint32_t> fully_transparent_lightmaps;

class faceextents_t {
private:
    qvec2i m_texmins;
    qvec2i m_texsize;
    float m_lightmapscale;
    qmat4x4f m_worldToTexCoord;
    qmat4x4f m_texCoordToWorld;
    
public:
    faceextents_t() = default;
    faceextents_t(const bsp2_dface_t *face, const mbsp_t *bsp, float lmscale);
    int width() const;
    int height() const;
    int numsamples() const;
    qvec2i texsize() const;
    int indexOf(const qvec2i &lm) const;
    qvec2i intCoordsFromIndex(int index) const;
    qvec2f LMCoordToTexCoord(const qvec2f &LMCoord) const;
    qvec2f TexCoordToLMCoord(const qvec2f &tc) const;
    qvec2f worldToTexCoord(qvec3f world) const;
    qvec3f texCoordToWorld(qvec2f tc) const;
    qvec2f worldToLMCoord(qvec3f world) const;
    qvec3f LMCoordToWorld(qvec2f lm) const;
};

qvec2f WorldToTexCoord_HighPrecision(const mbsp_t *bsp, const bsp2_dface_t *face, const qvec3f &world);
qmat4x4f WorldToTexSpace(const mbsp_t *bsp, const bsp2_dface_t *f);
qmat4x4f TexSpaceToWorld(const mbsp_t *bsp, const bsp2_dface_t *f);
void WorldToTexCoord(const vec3_t world, const gtexinfo_t *tex, vec_t coord[2]);
void PrintFaceInfo(const bsp2_dface_t *face, const mbsp_t *bsp);
// FIXME: remove light param. add normal param and dir params.
vec_t GetLightValue(const globalconfig_t &cfg, const light_t *entity, vec_t dist);
std::map<int, qvec3f> GetDirectLighting(const mbsp_t *bsp, const globalconfig_t &cfg, const vec3_t origin, const vec3_t normal);
void SetupDirt(globalconfig_t &cfg);
float DirtAtPoint(const globalconfig_t &cfg, raystream_intersection_t *rs, const vec3_t point, const vec3_t normal, const modelinfo_t *selfshadow);
void LightFace(const mbsp_t *bsp, bsp2_dface_t *face, facesup_t *facesup, const globalconfig_t &cfg);

#endif /* __LIGHT_LTFACE_H__ */
