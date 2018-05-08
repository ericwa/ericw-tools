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

#include <common/qvec.hh>

typedef struct {
    std::vector<qvec3f> poly;
    std::vector<qvec4f> poly_edgeplanes;
    qvec3f pos;
    std::map<int, qvec3f> colorByStyle;
    qvec3f componentwiseMaxColor; // cached maximum color in the colorByStyle, used for culling so we don't need to loop through colorByStyle
    qvec3f surfnormal;
    float area;
    
    /* estimated visible AABB culling */
    vec3_t mins;
    vec3_t maxs;
} bouncelight_t;

// public functions

const std::vector<bouncelight_t> &BounceLights();
const std::vector<int> &BounceLightsForFaceNum(int facenum);
void MakeTextureColors (const mbsp_t *bsp);
void MakeBounceLights (const globalconfig_t &cfg, const mbsp_t *bsp);
void Face_LookupTextureColor (const mbsp_t *bsp, const bsp2_dface_t *face, vec3_t color); //mxd

#endif /* __LIGHT_BOUNCE_H__ */
