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

#include <glm/vec3.hpp>

int Face_GetNum(const bsp2_t *bsp, const bsp2_dface_t *f);
int Face_VertexAtIndex(const bsp2_t *bsp, const bsp2_dface_t *f, int v);
plane_t Face_Plane(const bsp2_t *bsp, const bsp2_dface_t *f);
const texinfo_t *Face_Texinfo(const bsp2_t *bsp, const bsp2_dface_t *face);
const miptex_t *Face_Miptex(const bsp2_t *bsp, const bsp2_dface_t *face);
const char *Face_TextureName(const bsp2_t *bsp, const bsp2_dface_t *face);
bool Face_IsLightmapped(const bsp2_t *bsp, const bsp2_dface_t *face);
const float *GetSurfaceVertexPoint(const bsp2_t *bsp, const bsp2_dface_t *f, int v);
int TextureName_Contents(const char *texname);
int Face_Contents(const bsp2_t *bsp, const bsp2_dface_t *face);
const dmodel_t *BSP_DModelForModelString(const bsp2_t *bsp, const std::string &submodel_str);
vec_t Plane_Dist(const vec3_t point, const dplane_t *plane);
bool Light_PointInSolid(const bsp2_t *bsp, const dmodel_t *model, const vec3_t point);
bool Light_PointInWorld(const bsp2_t *bsp, const vec3_t point);
plane_t *Face_AllocInwardFacingEdgePlanes(const bsp2_t *bsp, const bsp2_dface_t *face);
bool EdgePlanes_PointInside(const bsp2_dface_t *face, const plane_t *edgeplanes, const vec3_t point);

glm::vec4 Face_Plane_E(const bsp2_t *bsp, const bsp2_dface_t *f);
glm::vec3 Face_PointAtIndex_E(const bsp2_t *bsp, const bsp2_dface_t *f, int v);
glm::vec3 Vertex_GetPos_E(const bsp2_t *bsp, int num);
glm::vec3 Face_Normal_E(const bsp2_t *bsp, const bsp2_dface_t *f);
std::vector<glm::vec3> GLM_FacePoints(const bsp2_t *bsp, const bsp2_dface_t *face);
glm::vec3 Face_Centroid(const bsp2_t *bsp, const bsp2_dface_t *face);

#endif /* __COMMON_BSPUTILS_HH__ */
