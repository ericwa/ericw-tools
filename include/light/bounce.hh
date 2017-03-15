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

#ifndef __LIGHT_BOUNCE_H__
#define __LIGHT_BOUNCE_H__

#include <common/cmdlib.hh>
#include <common/mathlib.hh>

#include <vector>
#include <map>

#include <glm/vec3.hpp>

typedef struct {
    std::vector<glm::vec3> poly;
    std::vector<glm::vec4> poly_edgeplanes;
    glm::vec3 pos;
    std::map<int, glm::vec3> colorByStyle;
    glm::vec3 componentwiseMaxColor; // cached maximum color in the colorByStyle, used for culling so we don't need to loop through colorByStyle
    glm::vec3 surfnormal;
    float area;
    
    /* estimated visible AABB culling */
    vec3_t mins;
    vec3_t maxs;
} bouncelight_t;

// public functions

const std::vector<bouncelight_t> &BounceLights();
const std::vector<int> &BounceLightsForFaceNum(int facenum);
void MakeTextureColors (const bsp2_t *bsp);
void MakeBounceLights (const globalconfig_t &cfg, const bsp2_t *bsp);
/** Returns color components in [0, 255] */
glm::vec3 Palette_GetColor(int i);

#endif /* __LIGHT_BOUNCe_H__ */
