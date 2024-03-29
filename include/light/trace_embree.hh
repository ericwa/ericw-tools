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
    float   maxdist;
    int     index;
    qvec3f  color;
    qvec3f  normalcontrib;

    bool    hit_glass = false;
    qvec3f  glass_color;
    float   glass_opacity;

    // This is set to the modelinfo's switchshadstyle if the ray hit
    // a dynamic shadow caster. (note that for rays that hit dynamic
    // shadow casters, all of the other hit data is assuming the ray went
    // straight through).
    int     dynamic_style = 0;
};

class raystream_embree_base_t
{
public:
    inline raystream_embree_base_t() = default;
    virtual ~raystream_embree_base_t() = default;

    virtual constexpr const size_t numPushedRays() const = 0;
    virtual constexpr ray_io &getRay(size_t index) = 0;
    virtual constexpr const ray_io &getRay(size_t index) const = 0;

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
};

template<typename TRay = ray_io>
class raystream_embree_common_t : public raystream_embree_base_t
{
protected:
    aligned_vector<TRay> _rays;

public:
    inline raystream_embree_common_t() = default;
    inline raystream_embree_common_t(size_t capacity)
    {
        _rays.reserve(capacity);
    }
    virtual ~raystream_embree_common_t() = default;

    constexpr void resize(size_t size)
    {
        _rays.resize(size);
    }

    constexpr ray_io &getRay(size_t index) override { return _rays[index]; }
    constexpr const ray_io &getRay(size_t index) const override { return _rays[index]; };

    constexpr const size_t numPushedRays() const override { return _rays.size(); }

    constexpr void clearPushedRays() { _rays.clear(); }

protected:
    static inline RTCRayHit SetupRay(unsigned int rayindex, const qvec3f &start, const qvec3f &dir, float dist)
    {
        RTCRayHit ray;
        ray.ray.org_x = start[0];
        ray.ray.org_y = start[1];
        ray.ray.org_z = start[2];
        ray.ray.tnear = 0.f;

        ray.ray.dir_x = dir[0]; // can be un-normalized
        ray.ray.dir_y = dir[1];
        ray.ray.dir_z = dir[2];
        ray.ray.time = 0.f; // not using

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
    raystream_embree_base_t *raystream; // may be null if this ray is not from a ray stream
    const modelinfo_t *self;
    int shadowmask;

    ray_source_info(raystream_embree_base_t *raystream_, const modelinfo_t *self_, int shadowmask_);
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

struct ray_io_intersection : public ray_io
{
    RTCRayHit   ray;

    constexpr ray_io_intersection() = default;
    constexpr ray_io_intersection(const ray_io_intersection &) = default;
    constexpr ray_io_intersection(ray_io_intersection &&) = default;

    constexpr ray_io_intersection(ray_io &&io, const RTCRayHit &ray) :
        ray_io(io),
        ray(ray)
    {
    }
};

class raystream_intersection_t : public raystream_embree_common_t<ray_io_intersection>
{
public:
    using raystream_embree_common_t::raystream_embree_common_t;

    inline void pushRay(int i, const qvec3f &origin, const qvec3f &dir, float dist, const qvec3f *color = nullptr,
        const qvec3f *normalcontrib = nullptr)
    {
        const RTCRayHit rayHit = SetupRay(_rays.size(), origin, dir, dist);
        _rays.emplace_back(
            ray_io {
                .maxdist = dist,
                .index = i,
                .color = color ? *color : qvec3f{},
                .normalcontrib = normalcontrib ? *normalcontrib : qvec3f{}
            },
            rayHit
        );
    }

    inline void tracePushedRaysIntersection(const modelinfo_t *self, int shadowmask)
    {
        if (!_rays.size())
            return;

        ray_source_info ctx2(this, self, shadowmask);

#ifdef HAVE_EMBREE4
        RTCIntersectArguments embree4_args = ctx2.setup_intersection_arguments();
        for (int i = 0; i < _numrays; ++i)
            rtcIntersect1(scene, &_rays[i], &embree4_args);
#else
        rtcIntersect1M(scene, &ctx2, &_rays.data()->ray, _rays.size(), sizeof(_rays[0]));
#endif
    }

    inline const qvec3f &getPushedRayDir(size_t j) const { return *((qvec3f *)&_rays[j].ray.ray.dir_x); }

    inline const float &getPushedRayHitDist(size_t j) const { return _rays[j].ray.ray.tfar; }

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

struct ray_io_occlusion : public ray_io
{
    RTCRay   ray;

    constexpr ray_io_occlusion() = default;
    constexpr ray_io_occlusion(const ray_io_occlusion &) = default;
    constexpr ray_io_occlusion(ray_io_occlusion &&) = default;

    constexpr ray_io_occlusion(ray_io &&io, const RTCRay &ray) :
        ray_io(io),
        ray(ray)
    {
    }
};

class raystream_occlusion_t : public raystream_embree_common_t<ray_io_occlusion>
{
public:
    using raystream_embree_common_t::raystream_embree_common_t;

    inline void pushRay(int i, const qvec3f &origin, const qvec3f &dir, float dist, const qvec3f *color = nullptr,
        const qvec3f *normalcontrib = nullptr)
    {
        const RTCRay ray = SetupRay(_rays.size(), origin, dir, dist).ray;
        _rays.emplace_back(
            ray_io {
                .maxdist = dist,
                .index = i,
                .color = color ? *color : qvec3f{},
                .normalcontrib = normalcontrib ? *normalcontrib : qvec3f{}
            },
            ray
        );
    }

    inline void tracePushedRaysOcclusion(const modelinfo_t *self, int shadowmask)
    {
        if (!_rays.size())
            return;

        ray_source_info ctx2(this, self, shadowmask);
#ifdef HAVE_EMBREE4
        RTCOccludedArguments embree4_args = ctx2.setup_occluded_arguments();
        for (int i = 0; i < _numrays; ++i)
            rtcOccluded1(scene, &_rays[i], &embree4_args);
#else
        rtcOccluded1M(scene, &ctx2, &_rays.data()->ray, _rays.size(), sizeof(_rays[0]));
#endif
    }

    inline bool getPushedRayOccluded(size_t j) const { return (_rays[j].ray.tfar < 0.0f); }

    inline const qvec3f &getPushedRayDir(size_t j) const { return *((qvec3f *)&_rays[j].ray.dir_x); }
};