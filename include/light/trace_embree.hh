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

#include "trace.hh"

void Embree_TraceInit(const mbsp_t *bsp);
hitresult_t Embree_TestSky(const qvec3d &start, const qvec3d &dirn, const modelinfo_t *self, const mface_t **face_out);
hitresult_t Embree_TestLight(const qvec3d &start, const qvec3d &stop, const modelinfo_t *self);
hittype_t Embree_DirtTrace(const qvec3d &start, const qvec3d &dirn, vec_t dist, const modelinfo_t *self,
    vec_t *hitdist_out, qplane3d *hitplane_out, const mface_t **face_out);

raystream_occlusion_t *Embree_MakeOcclusionRayStream(int maxrays);
raystream_intersection_t *Embree_MakeIntersectionRayStream(int maxrays);
