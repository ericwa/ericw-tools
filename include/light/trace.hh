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

#ifndef __LIGHT_TRACE_H__
#define __LIGHT_TRACE_H__

#include <light/imglib.hh> //mxd

#include <common/cmdlib.hh>
#include <common/mathlib.hh>
#include <common/bspfile.hh>
#include <common/log.hh>
#include <common/threads.hh>
#include <common/polylib.hh>

#include <vector>
#include <map>
#include <string>
#include <cassert>
#include <limits>
#include <sstream>
#include <utility> // for std::pair

enum class hittype_t : uint8_t {
    NONE = 0,
    SOLID = 1,
    SKY = 2
};

const mleaf_t *Light_PointInLeaf( const mbsp_t *bsp, const vec3_t point );
int Light_PointContents( const mbsp_t *bsp, const vec3_t point );
uint32_t clamp_texcoord(vec_t in, uint32_t width);
color_rgba SampleTexture(const bsp2_dface_t *face, const mbsp_t *bsp, const vec3_t point); //mxd. Palette index -> RGBA

class modelinfo_t;

using style_t = int;

struct hitresult_t {
    bool blocked;

    /**
     * non-zero means light passed through a shadow-casting bmodel with the given style.
     * only valid if blocked == false.
     */
    style_t passedSwitchableShadowStyle;
};

/**
 * Convenience functions TestLight and TestSky will test against all shadow
 * casting bmodels and self-shadow the model 'self' if self != NULL.
 */
hitresult_t TestSky(const vec3_t start, const vec3_t dirn, const modelinfo_t *self, const bsp2_dface_t **face_out);
hitresult_t TestLight(const vec3_t start, const vec3_t stop, const modelinfo_t *self);
#if 0
hittype_t DirtTrace(const vec3_t start, const vec3_t dirn, vec_t dist, const modelinfo_t *self, vec_t *hitdist_out, plane_t *hitplane_out, const bsp2_dface_t **face_out);
#endif

class modelinfo_t;

class raystream_common_t {
public:
    virtual ~raystream_common_t() = default;
    virtual void pushRay(int i, const vec_t *origin, const vec3_t dir, float dist, const vec_t *color = nullptr, const vec_t *normalcontrib = nullptr) = 0;
    virtual size_t numPushedRays() = 0;
    virtual void getPushedRayDir(size_t j, vec3_t out) = 0;
    virtual int getPushedRayPointIndex(size_t j) = 0;
    virtual void getPushedRayColor(size_t j, vec3_t out) = 0;
    virtual void getPushedRayNormalContrib(size_t j, vec3_t out) = 0;
    virtual int getPushedRayDynamicStyle(size_t j) = 0;
    virtual void clearPushedRays() = 0;

public:
    void pushRay(int i, const qvec3f &origin, const qvec3f &dir, float dist) {
        vec3_t originTemp, dirTemp;
        glm_to_vec3_t(origin, originTemp);
        glm_to_vec3_t(dir, dirTemp);
        this->pushRay(i, originTemp, dirTemp, dist);
    }

    qvec3f getPushedRayDir(size_t j) {
        vec3_t temp;
        this->getPushedRayDir(j, temp);
        return vec3_t_to_glm(temp);
    }
};

class raystream_intersection_t : public virtual raystream_common_t {
public:
    virtual void tracePushedRaysIntersection(const modelinfo_t *self) = 0;
    virtual float getPushedRayHitDist(size_t j) = 0;
    virtual hittype_t getPushedRayHitType(size_t j) = 0;
    virtual const bsp2_dface_t *getPushedRayHitFace(size_t j) = 0;

    virtual ~raystream_intersection_t() = default;
};

class raystream_occlusion_t : public virtual raystream_common_t {
public:
    virtual void tracePushedRaysOcclusion(const modelinfo_t *self) = 0;
    virtual bool getPushedRayOccluded(size_t j) = 0;

    virtual ~raystream_occlusion_t() = default;
};

raystream_intersection_t *MakeIntersectionRayStream(int maxrays);
raystream_occlusion_t *MakeOcclusionRayStream(int maxrays);

void MakeTnodes(const mbsp_t *bsp);

#endif /* __LIGHT_TRACE_H__ */
