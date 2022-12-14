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

#include <common/cmdlib.hh>
#include <common/mathlib.hh>
#include <common/bspfile.hh>
#include <common/log.hh>
#include <common/threads.hh>
#include <common/polylib.hh>
#include <vector>

void ResetEmbree();
void Embree_TraceInit(const mbsp_t *bsp);

class modelinfo_t;

class raystream_embree_common_t
{
public:
    std::vector<float> _rays_maxdist;
    std::vector<int> _point_indices;
    std::vector<qvec3f> _ray_colors;
    std::vector<qvec3d> _ray_normalcontribs;

    std::vector<bool> _ray_hit_glass;
    std::vector<qvec3f> _ray_glass_color;
    std::vector<float> _ray_glass_opacity;

    // This is set to the modelinfo's switchshadstyle if the ray hit
    // a dynamic shadow caster. (note that for rays that hit dynamic
    // shadow casters, all of the other hit data is assuming the ray went
    // straight through).
    std::vector<int> _ray_dynamic_styles;

    int _numrays = 0;
    int _maxrays = 0;

public:
    inline raystream_embree_common_t() = default;
    virtual ~raystream_embree_common_t() = default;

    virtual void resize(size_t size)
    {
        _maxrays = size;

        _rays_maxdist.resize(size);
        _point_indices.resize(size);
        _ray_colors.resize(size);
        _ray_normalcontribs.resize(size);
        _ray_hit_glass.resize(size);
        _ray_glass_color.resize(size);
        _ray_glass_opacity.resize(size);
        _ray_dynamic_styles.resize(size);
    }

    constexpr size_t numPushedRays() { return _numrays; }

    inline int &getPushedRayPointIndex(size_t j)
    {
        Q_assert(j < _maxrays);
        return _point_indices[j];
    }

    inline qvec3f getPushedRayColor(size_t j)
    {
        Q_assert(j < _maxrays);
        qvec3f result = _ray_colors[j];

        if (_ray_hit_glass[j]) {
            const qvec3f glasscolor = _ray_glass_color[j];
            const float opacity = _ray_glass_opacity[j];

            // multiply ray color by glass color
            const qvec3f tinted = result * glasscolor;

            // lerp ray color between original ray color and fully tinted by the glass texture color, based on the glass opacity
            result = mix(result, tinted, opacity);
        }

        return result;
    }

    inline qvec3d &getPushedRayNormalContrib(size_t j)
    {
        Q_assert(j < _maxrays);
        return _ray_normalcontribs[j];
    }

    inline int &getPushedRayDynamicStyle(size_t j)
    {
        Q_assert(j < _maxrays);
        return _ray_dynamic_styles[j];
    }

    inline void clearPushedRays() { _numrays = 0; }
};

#include <embree3/rtcore.h>
#include <embree3/rtcore_ray.h>

extern RTCScene scene;

inline RTCRayHit SetupRay(unsigned rayindex, const qvec3d &start, const qvec3d &dir, vec_t dist)
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

class light_t;

struct ray_source_info : public RTCIntersectContext
{
    raystream_embree_common_t *raystream; // may be null if this ray is not from a ray stream
    const modelinfo_t *self;
    int shadowmask;

    ray_source_info(raystream_embree_common_t *raystream_, const modelinfo_t *self_, int shadowmask_);
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
private:
    aligned_vector<RTCRayHit> _rays;

public:
    inline raystream_intersection_t() = default;

    inline raystream_intersection_t(size_t maxRays) { resize(maxRays); }

    void resize(size_t size) override
    {
        _rays.resize(size);
        raystream_embree_common_t::resize(size);
    }

    inline void pushRay(int i, const qvec3d &origin, const qvec3d &dir, float dist, const qvec3f *color = nullptr,
        const qvec3d *normalcontrib = nullptr)
    {
        Q_assert(_numrays < _maxrays);
        const RTCRayHit rayHit = SetupRay(_numrays, origin, dir, dist);
        _rays[_numrays] = rayHit;
        _rays_maxdist[_numrays] = dist;
        _point_indices[_numrays] = i;
        if (color) {
            _ray_colors[_numrays] = *color;
        }
        if (normalcontrib) {
            _ray_normalcontribs[_numrays] = *normalcontrib;
        }
        _ray_hit_glass[_numrays] = false;
        _ray_dynamic_styles[_numrays] = 0;
        _numrays++;
    }

    inline void tracePushedRaysIntersection(const modelinfo_t *self, int shadowmask)
    {
        if (!_numrays)
            return;

        ray_source_info ctx2(this, self, shadowmask);
        rtcIntersect1M(scene, &ctx2, _rays.data(), _numrays, sizeof(_rays[0]));
    }

    inline qvec3d getPushedRayDir(size_t j)
    {
        Q_assert(j < _maxrays);
        return {_rays[j].ray.dir_x, _rays[j].ray.dir_y, _rays[j].ray.dir_z};
    }

    inline float getPushedRayHitDist(size_t j)
    {
        Q_assert(j < _maxrays);
        return _rays[j].ray.tfar;
    }

    inline hittype_t getPushedRayHitType(size_t j)
    {
        Q_assert(j < _maxrays);

        const unsigned id = _rays[j].hit.geomID;
        if (id == RTC_INVALID_GEOMETRY_ID) {
            return hittype_t::NONE;
        } else if (id == skygeom.geomID) {
            return hittype_t::SKY;
        } else {
            return hittype_t::SOLID;
        }
    }

    inline const triinfo *getPushedRayHitFaceInfo(size_t j)
    {
        Q_assert(j < _maxrays);

        const RTCRayHit &ray = _rays[j];

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
private:
    aligned_vector<RTCRay> _rays;

public:
    inline raystream_occlusion_t() = default;

    inline raystream_occlusion_t(size_t maxRays) { resize(maxRays); }

    void resize(size_t size) override
    {
        _rays.resize(size);
        raystream_embree_common_t::resize(size);
    }

    inline void pushRay(int i, const qvec3d &origin, const qvec3d &dir, float dist, const qvec3f *color = nullptr,
        const qvec3d *normalcontrib = nullptr)
    {
        Q_assert(_numrays < _maxrays);
        _rays[_numrays] = SetupRay(_numrays, origin, dir, dist).ray;
        _rays_maxdist[_numrays] = dist;
        _point_indices[_numrays] = i;
        if (color) {
            _ray_colors[_numrays] = *color;
        }
        if (normalcontrib) {
            _ray_normalcontribs[_numrays] = *normalcontrib;
        }
        _ray_hit_glass[_numrays] = false;
        _ray_dynamic_styles[_numrays] = 0;
        _numrays++;
    }

    inline void tracePushedRaysOcclusion(const modelinfo_t *self, int shadowmask)
    {
        // Q_assert(_state == streamstate_t::READY);

        if (!_numrays)
            return;

        ray_source_info ctx2(this, self, shadowmask);
        rtcOccluded1M(scene, &ctx2, _rays.data(), _numrays, sizeof(_rays[0]));
    }

    inline bool getPushedRayOccluded(size_t j)
    {
        Q_assert(j < _maxrays);
        return (_rays[j].tfar < 0.0f);
    }

    inline qvec3d getPushedRayDir(size_t j)
    {
        Q_assert(j < _maxrays);

        return {_rays[j].dir_x, _rays[j].dir_y, _rays[j].dir_z};
    }
};