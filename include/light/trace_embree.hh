/*  Copyright (C) 2016 Eric Wasylishen

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

#include <common/aligned_allocator.hh>
#include <common/qvec.hh>
#include <common/log.hh> // for FError

#include <vector>
#include <set>

#ifdef HAVE_EMBREE4
#include <embree4/rtcore.h>
#include <embree4/rtcore_ray.h>
#else
#include <embree3/rtcore.h>
#include <embree3/rtcore_ray.h>
#endif

struct mbsp_t;
class modelinfo_t;
struct mface_t;

class light_t;
struct mtexinfo_t;
namespace img
{
struct texture;
}

void ResetEmbree();
void Embree_TraceInit(const mbsp_t *bsp);
const std::set<const mface_t *> &ShadowCastingSolidFacesSet();

struct ray_io
{
    RTCRayHit ray;
    float maxdist;
    int index;
    qvec3f color;
    qvec3f normalcontrib;

    bool hit_glass = false;
    qvec3f glass_color;
    float glass_opacity;

    // This is set to the modelinfo's switchshadstyle if the ray hit
    // a dynamic shadow caster. (note that for rays that hit dynamic
    // shadow casters, all of the other hit data is assuming the ray went
    // straight through).
    int dynamic_style = 0;
};

struct alignas(16) aligned_vec3
{
    float x, y, z, w;
};

class raystream_embree_common_t
{
protected:
    aligned_vector<ray_io> _rays;

public:
    inline raystream_embree_common_t() = default;
    inline raystream_embree_common_t(size_t capacity) { _rays.reserve(capacity); }
    virtual ~raystream_embree_common_t() = default;

    void resize(size_t size) { _rays.resize(size); }

    ray_io &getRay(size_t index) { return _rays[index]; }
    const ray_io &getRay(size_t index) const { return _rays[index]; };

    const size_t numPushedRays() const { return _rays.size(); }

    void clearPushedRays() { _rays.clear(); }

    inline qvec3f getPushedRayColor(size_t j) const
    {
        const ray_io &ray = getRay(j);
        qvec3f result = ray.color;

        if (ray.hit_glass) {
            const qvec3f glasscolor = ray.glass_color;
            const float opacity = ray.glass_opacity;

            // multiply ray color by glass color
            const qvec3f tinted = result * glasscolor;

            // lerp ray color between original ray color and fully tinted by the glass texture color, based on the glass
            // opacity
            result = mix(result, tinted, opacity);
        }

        return result;
    }

protected:
    static inline RTCRayHit SetupRay(
        unsigned int rayindex, const aligned_vec3 &start, const aligned_vec3 &dir, float dist)
    {
        RTCRayHit ray;
        ray.ray.org_x = start.x;
        ray.ray.org_y = start.y;
        ray.ray.org_z = start.z;
        ray.ray.tnear = start.w;

        ray.ray.dir_x = dir.x; // can be un-normalized
        ray.ray.dir_y = dir.y;
        ray.ray.dir_z = dir.z;
        ray.ray.time = dir.w; // not using

        ray.ray.tfar = dist;
        ray.ray.mask = 1; // we're not using, but needs to be set if embree is compiled with masks
        ray.ray.id = rayindex;
        ray.ray.flags = 0; // reserved

        ray.hit.geomID = RTC_INVALID_GEOMETRY_ID;
        ray.hit.primID = RTC_INVALID_GEOMETRY_ID;
        ray.hit.instID[0] = RTC_INVALID_GEOMETRY_ID;
        return ray;
    }
};

extern RTCScene scene;

struct ray_source_info : public
#ifdef HAVE_EMBREE4
                         RTCRayQueryContext
#else
                         RTCIntersectContext
#endif
{
    raystream_embree_common_t *raystream; // may be null if this ray is not from a ray stream
    const modelinfo_t *self;
    int shadowmask;

    ray_source_info(raystream_embree_common_t *raystream_, const modelinfo_t *self_, int shadowmask_);
#ifdef HAVE_EMBREE4
    RTCIntersectArguments setup_intersection_arguments();
    RTCOccludedArguments setup_occluded_arguments();
#endif
};

struct triinfo
{
    const modelinfo_t *modelinfo;
    const mface_t *face;
    const mtexinfo_t *texinfo;

    const img::texture *texture;
    float alpha;
    bool is_fence, is_glass;

    // cached from modelinfo for faster access
    bool shadowworldonly;
    bool shadowself;
    bool switchableshadow;
    int32_t switchshadstyle;

    int channelmask;
};

struct sceneinfo
{
    unsigned geomID;

    std::vector<triinfo> triInfo;
};

extern sceneinfo skygeom; // sky. always occludes.
extern sceneinfo solidgeom; // solids. always occludes.
extern sceneinfo filtergeom; // conditional occluders.. needs to run ray intersection filter

enum class hittype_t : uint8_t
{
    NONE = 0,
    SOLID = 1,
    SKY = 2
};

inline const sceneinfo &Embree_SceneinfoForGeomID(unsigned int geomID)
{
    if (geomID == skygeom.geomID) {
        return skygeom;
    } else if (geomID == solidgeom.geomID) {
        return solidgeom;
    } else if (geomID == filtergeom.geomID) {
        return filtergeom;
    } else {
        FError("unexpected geomID");
    }
}

class raystream_intersection_t : public raystream_embree_common_t
{
public:
    using raystream_embree_common_t::raystream_embree_common_t;

    inline void pushRay(int i, const qvec3f &origin, const qvec3f &dir, float dist, const qvec3f *color = nullptr,
        const qvec3f *normalcontrib = nullptr)
    {
        const RTCRayHit rayHit =
            SetupRay(_rays.size(), {origin[0], origin[1], origin[2], 0.f}, {dir[0], dir[1], dir[2], 0.f}, dist);
        _rays.push_back(ray_io{.ray = rayHit,
            .maxdist = dist,
            .index = i,
            .color = color ? *color : qvec3f{},
            .normalcontrib = normalcontrib ? *normalcontrib : qvec3f{}});
    }

    inline void tracePushedRaysIntersection(const modelinfo_t *self, int shadowmask)
    {
        if (!_rays.size())
            return;

        ray_source_info ctx2(this, self, shadowmask);

#ifdef HAVE_EMBREE4
        RTCIntersectArguments embree4_args = ctx2.setup_intersection_arguments();
        for (auto &ray : _rays)
            rtcIntersect1(scene, &ray.ray, &embree4_args);
#else
        rtcIntersect1M(scene, &ctx2, &_rays.data()->ray, _rays.size(), sizeof(_rays[0]));
#endif
    }

    inline const qvec3f &getPushedRayDir(size_t j) const { return *((qvec3f *)&_rays[j].ray.ray.dir_x); }

    inline const float getPushedRayHitDist(size_t j) const { return _rays[j].ray.ray.tfar; }

    inline hittype_t getPushedRayHitType(size_t j) const
    {
        const unsigned id = _rays[j].ray.hit.geomID;
        if (id == RTC_INVALID_GEOMETRY_ID) {
            return hittype_t::NONE;
        } else if (id == skygeom.geomID) {
            return hittype_t::SKY;
        } else {
            return hittype_t::SOLID;
        }
    }

    inline const triinfo *getPushedRayHitFaceInfo(size_t j) const
    {
        const RTCRayHit &ray = _rays[j].ray;

        if (ray.hit.geomID == RTC_INVALID_GEOMETRY_ID) {
            return nullptr;
        }

        const sceneinfo &si = Embree_SceneinfoForGeomID(ray.hit.geomID);
        const triinfo *face = &si.triInfo.at(ray.hit.primID);
        Q_assert(face != nullptr);

        return face;
    }
};

class raystream_occlusion_t : public raystream_embree_common_t
{
public:
    using raystream_embree_common_t::raystream_embree_common_t;

    inline void pushRay(int i, const qvec3f &origin, const qvec3f &dir, float dist, const qvec3f *color = nullptr,
        const qvec3f *normalcontrib = nullptr)
    {
        const RTCRay ray =
            SetupRay(_rays.size(), {origin[0], origin[1], origin[2], 0.f}, {dir[0], dir[1], dir[2], 0.f}, dist).ray;
        _rays.push_back(ray_io{.ray = {ray},
            .maxdist = dist,
            .index = i,
            .color = color ? *color : qvec3f{},
            .normalcontrib = normalcontrib ? *normalcontrib : qvec3f{}});
    }

    inline void tracePushedRaysOcclusion(const modelinfo_t *self, int shadowmask)
    {
        if (!_rays.size())
            return;

        ray_source_info ctx2(this, self, shadowmask);
#ifdef HAVE_EMBREE4
        RTCOccludedArguments embree4_args = ctx2.setup_occluded_arguments();
        for (auto &ray : _rays)
            rtcOccluded1(scene, &ray.ray.ray, &embree4_args);
#else
        rtcOccluded1M(scene, &ctx2, &_rays.data()->ray.ray, _rays.size(), sizeof(_rays[0]));
#endif
    }

    inline bool getPushedRayOccluded(size_t j) const { return (_rays[j].ray.ray.tfar < 0.0f); }

    inline const qvec3f &getPushedRayDir(size_t j) const { return *((qvec3f *)&_rays[j].ray.ray.dir_x); }
};