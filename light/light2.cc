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

#include <cstdint>
#include <cassert>
#include <cstdio>
#include <iostream>

#include <light/light2.hh>
#include <light/phong.hh>
#include <light/entities.hh>
#include <light/ltface.hh>
#include <light/ltface2.hh>

#include <common/polylib.hh>
#include <common/bsputils.hh>

#ifdef HAVE_EMBREE
#include <xmmintrin.h>
//#include <pmmintrin.h>
#endif

#include <memory>
#include <vector>
#include <list>
#include <map>
#include <unordered_map>
#include <set>
#include <algorithm>
#include <mutex>
#include <string>

#include <glm/vec2.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>

using namespace std;

glm::mat4x4 WorldToTexSpace(const bsp2_t *bsp, const bsp2_dface_t *f)
{
    const texinfo_t *tex = Face_Texinfo(bsp, f);
    if (tex == nullptr) {
        Q_assert_unreachable();
        return glm::mat4x4();
    }
    const plane_t plane = Face_Plane(bsp, f);
    const vec_t *norm = plane.normal;
    
    //           [s]
    // T * vec = [t]
    //           [distOffPlane]
    //           [?]
    
    glm::mat4x4 T(tex->vecs[0][0], tex->vecs[1][0], norm[0], 0, // col 0
                  tex->vecs[0][1], tex->vecs[1][1], norm[1], 0, // col 1
                  tex->vecs[0][2], tex->vecs[1][2], norm[2], 0, // col 2
                  tex->vecs[0][3], tex->vecs[1][3], -plane.dist, 1 // col 3
                  );
    return T;
}

glm::mat4x4 TexSpaceToWorld(const bsp2_t *bsp, const bsp2_dface_t *f)
{
    const glm::mat4x4 T = WorldToTexSpace(bsp, f);
    
    if (glm::determinant(T) == 0) {
        logprint("Bad texture axes on face:\n");
        PrintFaceInfo(f, bsp);
        Error("CreateFaceTransform");
    }
    
    return glm::inverse(T);
}
