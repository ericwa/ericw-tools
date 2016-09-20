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

#ifndef __COMMON_BSPUTILS_H__
#define __COMMON_BSPUTILS_H__

#include <common/bspfile.h>
#include <common/mathlib.h>

#ifdef __cplusplus
extern "C" {
#endif

int Face_GetNum(const bsp2_t *bsp, const bsp2_dface_t *f);
int Face_VertexAtIndex(const bsp2_t *bsp, const bsp2_dface_t *f, int v);
void Face_Normal(const bsp2_t *bsp, const bsp2_dface_t *f, vec3_t norm);
plane_t Face_Plane(const bsp2_t *bsp, const bsp2_dface_t *f);
const miptex_t *Face_Miptex(const bsp2_t *bsp, const bsp2_dface_t *face);
const char *Face_TextureName(const bsp2_t *bsp, const bsp2_dface_t *face);
const float *GetSurfaceVertexPoint(const bsp2_t *bsp, const bsp2_dface_t *f, int v);
    
#ifdef __cplusplus
}
#endif

#endif /* __COMMON_BSPUTILS_H__ */
