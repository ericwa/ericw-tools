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

#include <vector>
#include <map>
#include <string>
#include <cassert>
#include <limits>
#include <sstream>
#include <utility> // for std::pair

enum class hittype_t : uint8_t
{
    NONE = 0,
    SOLID = 1,
    SKY = 2
};

uint32_t clamp_texcoord(vec_t in, uint32_t width);
qvec4b SampleTexture(const mface_t *face, const mbsp_t *bsp, const qvec3d &point); // mxd. Palette index -> RGBA

class modelinfo_t;

using style_t = int;

struct hitresult_t
{
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
hitresult_t TestSky(const qvec3d &start, const qvec3d &dirn, const modelinfo_t *self, const mface_t **face_out);
hitresult_t TestLight(const qvec3d &start, const qvec3d &stop, const modelinfo_t *self);

class modelinfo_t;

class raystream_common_t
{
public:
    virtual ~raystream_common_t() = default;
    virtual void pushRay(int i, const qvec3d &origin, const qvec3d &dir, float dist, const qvec3d *color = nullptr,
        const qvec3d *normalcontrib = nullptr) = 0;
    virtual size_t numPushedRays() = 0;
    virtual qvec3d getPushedRayDir(size_t j) = 0;
    virtual int getPushedRayPointIndex(size_t j) = 0;
    virtual qvec3d &getPushedRayColor(size_t j) = 0;
    virtual qvec3d &getPushedRayNormalContrib(size_t j) = 0;
    virtual int getPushedRayDynamicStyle(size_t j) = 0;
    virtual void clearPushedRays() = 0;
};

class raystream_intersection_t : public virtual raystream_common_t
{
public:
    virtual void tracePushedRaysIntersection(const modelinfo_t *self) = 0;
    virtual float getPushedRayHitDist(size_t j) = 0;
    virtual hittype_t getPushedRayHitType(size_t j) = 0;
    virtual const mface_t *getPushedRayHitFace(size_t j) = 0;

    virtual ~raystream_intersection_t() = default;
};

class raystream_occlusion_t : public virtual raystream_common_t
{
public:
    virtual void tracePushedRaysOcclusion(const modelinfo_t *self) = 0;
    virtual bool getPushedRayOccluded(size_t j) = 0;

    virtual ~raystream_occlusion_t() = default;
};

raystream_intersection_t *MakeIntersectionRayStream(int maxrays);
raystream_occlusion_t *MakeOcclusionRayStream(int maxrays);

void MakeTnodes(const mbsp_t *bsp);
