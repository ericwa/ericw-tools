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

#ifndef __COMMON_BSPUTILS_HH__
#define __COMMON_BSPUTILS_HH__

#include <common/bspfile.hh>
#include <common/mathlib.hh>
#include <string>

int Face_GetNum(const bsp2_t *bsp, const bsp2_dface_t *f);
int Face_VertexAtIndex(const bsp2_t *bsp, const bsp2_dface_t *f, int v);
void Face_Normal(const bsp2_t *bsp, const bsp2_dface_t *f, vec3_t norm);
plane_t Face_Plane(const bsp2_t *bsp, const bsp2_dface_t *f);
const miptex_t *Face_Miptex(const bsp2_t *bsp, const bsp2_dface_t *face);
const char *Face_TextureName(const bsp2_t *bsp, const bsp2_dface_t *face);
const float *GetSurfaceVertexPoint(const bsp2_t *bsp, const bsp2_dface_t *f, int v);
int TextureName_Contents(const char *texname);
int Face_Contents(const bsp2_t *bsp, const bsp2_dface_t *face);
const dmodel_t *BSP_DModelForModelString(const bsp2_t *bsp, const std::string &submodel_str);
vec_t Plane_Dist(const vec3_t point, const dplane_t *plane);
bool Light_PointInSolid(const bsp2_t *bsp, const vec3_t point );
void Face_MakeInwardFacingEdgePlanes(const bsp2_t *bsp, const bsp2_dface_t *face, plane_t *out);
bool EdgePlanes_PointInside(const bsp2_dface_t *face, const plane_t *edgeplanes, const vec3_t point);

#endif /* __COMMON_BSPUTILS_HH__ */
