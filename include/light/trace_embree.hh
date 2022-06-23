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

void Embree_TraceInit(const mbsp_t *bsp);
hitresult_t Embree_TestSky(const qvec3d &start, const qvec3d &dirn, const modelinfo_t *self, const mface_t **face_out);
hitresult_t Embree_TestLight(const qvec3d &start, const qvec3d &stop, const modelinfo_t *self);

class modelinfo_t;

class raystream_embree_common_t
{
public:
    std::vector<float> _rays_maxdist;
    std::vector<int> _point_indices;
    std::vector<qvec3d> _ray_colors;
    std::vector<qvec3d> _ray_normalcontribs;

    // This is set to the modelinfo's switchshadstyle if the ray hit
    // a dynamic shadow caster. (note that for rays that hit dynamic
    // shadow casters, all of the other hit data is assuming the ray went
    // straight through).
    std::vector<int> _ray_dynamic_styles;

    int _numrays = 0;
    int _maxrays = 0;

public:
    inline raystream_embree_common_t() = default;

    inline raystream_embree_common_t(size_t maxRays)
        : _rays_maxdist(maxRays), _point_indices(maxRays), _ray_colors(maxRays),
          _ray_normalcontribs(maxRays), _ray_dynamic_styles(maxRays), _maxrays(maxRays)
    {
    }

    virtual ~raystream_embree_common_t() = default;

    constexpr size_t numPushedRays() { return _numrays; }

    inline int &getPushedRayPointIndex(size_t j)
    {
        Q_assert(j < _maxrays);
        return _point_indices[j];
    }

    inline qvec3d &getPushedRayColor(size_t j)
    {
        Q_assert(j < _maxrays);
        return _ray_colors[j];
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

    inline void clearPushedRays()
    {
        _numrays = 0;
    }
};

#include <embree3/rtcore.h>
#include <embree3/rtcore_ray.h>

extern RTCScene scene;

inline void *q_aligned_malloc(size_t align, size_t size)
{
#ifdef _MSC_VER
    return _aligned_malloc(size, align);
#else
    void *ptr;
    if (0 != posix_memalign(&ptr, align, size)) {
        return nullptr;
    }
    return ptr;
#endif
}

inline void q_aligned_free(void *ptr)
{
#ifdef _MSC_VER
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

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

struct ray_source_info : public RTCIntersectContext
{
    raystream_embree_common_t *raystream; // may be null if this ray is not from a ray stream
    const modelinfo_t *self;
    /// only used if raystream == null
    int singleRayShadowStyle;

    ray_source_info(raystream_embree_common_t *raystream_, const modelinfo_t *self_)
        : raystream(raystream_), self(self_), singleRayShadowStyle(0)
    {
        rtcInitIntersectContext(this);

        flags = RTC_INTERSECT_CONTEXT_FLAG_COHERENT;
    }
};

class sceneinfo
{
public:
    unsigned geomID;

    std::vector<const mface_t *> triToFace;
    std::vector<const modelinfo_t *> triToModelinfo;
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
public:
    RTCRayHit *_rays = nullptr;

public:
    inline raystream_intersection_t() = default;

    inline raystream_intersection_t(int maxRays)
        : raystream_embree_common_t(maxRays), _rays{static_cast<RTCRayHit *>(
                                                  q_aligned_malloc(16, sizeof(RTCRayHit) * maxRays))}
    {
    }

    inline ~raystream_intersection_t() { q_aligned_free(_rays); _rays = nullptr; }

    inline void pushRay(int i, const qvec3d &origin, const qvec3d &dir, float dist, const qvec3d *color = nullptr,
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
        _ray_dynamic_styles[_numrays] = 0;
        _numrays++;
    }

    inline void tracePushedRaysIntersection(const modelinfo_t *self)
    {
        if (!_numrays)
            return;

        ray_source_info ctx2(this, self);
        rtcIntersect1M(scene, &ctx2, _rays, _numrays, sizeof(_rays[0]));
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

    inline const mface_t *getPushedRayHitFace(size_t j)
    {
        Q_assert(j < _maxrays);

        const RTCRayHit &ray = _rays[j];

        if (ray.hit.geomID == RTC_INVALID_GEOMETRY_ID)
            return nullptr;

        const sceneinfo &si = Embree_SceneinfoForGeomID(ray.hit.geomID);
        const mface_t *face = si.triToFace.at(ray.hit.primID);
        Q_assert(face != nullptr);

        return face;
    }
};

class raystream_occlusion_t : public raystream_embree_common_t
{
public:
    RTCRay *_rays = nullptr;

public:
    inline raystream_occlusion_t() = default;

    inline raystream_occlusion_t(int maxRays)
        : raystream_embree_common_t(maxRays), _rays{
                                                  static_cast<RTCRay *>(q_aligned_malloc(16, sizeof(RTCRay) * maxRays))}
    {
    }

    inline ~raystream_occlusion_t() { q_aligned_free(_rays); }

    inline void pushRay(int i, const qvec3d &origin, const qvec3d &dir, float dist, const qvec3d *color = nullptr,
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
        _ray_dynamic_styles[_numrays] = 0;
        _numrays++;
    }

    inline void tracePushedRaysOcclusion(const modelinfo_t *self)
    {
        // Q_assert(_state == streamstate_t::READY);

        if (!_numrays)
            return;

        ray_source_info ctx2(this, self);
        rtcOccluded1M(scene, &ctx2, _rays, _numrays, sizeof(_rays[0]));
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