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

#include <common/cmdlib.h>
#include <common/mathlib.h>
#include <common/bspfile.h>
#include <common/log.h>
#include <common/threads.h>
#include <common/polylib.h>

#include <light/litfile.hh>
#include <light/trace.hh>
#include <light/entities.hh>

#include <vector>
#include <map>
#include <string>
#include <cassert>
#include <limits>
#include <sstream>

void FaceCentroid(const bsp2_dface_t *face, const bsp2_t *bsp, vec3_t out);
void WorldToTexCoord(const vec3_t world, const texinfo_t *tex, vec_t coord[2]);
vec_t GetLightValue(const light_t *entity, vec_t dist);
void GetDirectLighting(raystream_t *rs, const vec3_t origin, const vec3_t normal, vec3_t colorout);
void SetupDirt();
void LightFaceInit(const bsp2_t *bsp, struct ltface_ctx *ctx);
void LightFaceShutdown(struct ltface_ctx *ctx);
void LightFace(bsp2_dface_t *face, facesup_t *facesup, const modelinfo_t *modelinfo, struct ltface_ctx *ctx);

#endif /* __LIGHT_LTFACE_H__ */
