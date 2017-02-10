/*  Copyright (C) 1996-1997  Id Software, Inc.
    Copyright (C) 2017 Eric Wasylishen
 
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

#ifndef __LIGHT_PHONG_H__
#define __LIGHT_PHONG_H__

#include <common/cmdlib.hh>
#include <common/mathlib.hh>
#include <common/bspfile.hh>
#include <common/log.hh>

#include <light/light.hh>

#include <glm/vec3.hpp>

void CalcualateVertexNormals(const bsp2_t *bsp);
const glm::vec3 GetSurfaceVertexNormal(const bsp2_t *bsp, const bsp2_dface_t *f, const int vertindex);
bool FacesSmoothed(const bsp2_dface_t *f1, const bsp2_dface_t *f2);
const std::set<const bsp2_dface_t *> &GetSmoothFaces(const bsp2_dface_t *face);
const std::vector<const bsp2_dface_t *> &GetPlaneFaces(const bsp2_dface_t *face);
const glm::vec3 GetSurfaceVertexNormal(const bsp2_t *bsp, const bsp2_dface_t *f, const int v);
const bsp2_dface_t *Face_EdgeIndexSmoothed(const bsp2_t *bsp, const bsp2_dface_t *f, const int edgeindex);

#endif /* __LIGHT_PHONG_H__ */
