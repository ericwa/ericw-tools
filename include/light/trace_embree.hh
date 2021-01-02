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

#ifndef __LIGHT_TRACE_EMBREE_H__
#define __LIGHT_TRACE_EMBREE_H__

#include <common/cmdlib.hh>
#include <common/mathlib.hh>
#include <common/bspfile.hh>
#include <common/log.hh>
#include <common/threads.hh>
#include <common/polylib.hh>

#include "trace.hh"

void Embree_TraceInit(const mbsp_t *bsp);
hitresult_t Embree_TestSky(const vec3_t start, const vec3_t dirn, const modelinfo_t *self, const bsp2_dface_t **face_out);
hitresult_t Embree_TestLight(const vec3_t start, const vec3_t stop, const modelinfo_t *self);
hittype_t Embree_DirtTrace(const vec3_t start, const vec3_t dirn, vec_t dist, const modelinfo_t *self, vec_t *hitdist_out, plane_t *hitplane_out, const bsp2_dface_t **face_out);

raystream_occlusion_t *Embree_MakeOcclusionRayStream(int maxrays);
raystream_intersection_t *Embree_MakeIntersectionRayStream(int maxrays);

#endif /* __LIGHT_TRACE_EMBREE_H__ */
