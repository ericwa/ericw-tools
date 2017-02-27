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

#include <light/light.hh>
#include <light/light2.hh>
#include <light/entities.hh>
#include <light/trace.hh>
#include <light/ltface2.hh>

#include <common/bsputils.hh>

#include <iostream>
#include <cassert>
#include <cmath>
#include <algorithm>
#include <array>

#include <glm/gtc/epsilon.hpp>
#include <glm/gtx/string_cast.hpp>

using namespace std;
using namespace glm;
using namespace polylib;

glm::vec2 WorldToTexCoord_HighPrecision(const bsp2_t *bsp, const bsp2_dface_t *face, const glm::vec3 &world)
{
    const texinfo_t *tex = Face_Texinfo(bsp, face);
    if (tex == nullptr)
        return glm::vec2(0);
    
    glm::vec2 coord;
    
    /*
     * The (long double) casts below are important: The original code
     * was written for x87 floating-point which uses 80-bit floats for
     * intermediate calculations. But if you compile it without the
     * casts for modern x86_64, the compiler will round each
     * intermediate result to a 32-bit float, which introduces extra
     * rounding error.
     *
     * This becomes a problem if the rounding error causes the light
     * utilities and the engine to disagree about the lightmap size
     * for some surfaces.
     *
     * Casting to (long double) keeps the intermediate values at at
     * least 64 bits of precision, probably 128.
     */
    for (int i = 0; i < 2; i++) {
        coord[i] = (long double)world[0] * tex->vecs[i][0] +
        (long double)world[1] * tex->vecs[i][1] +
        (long double)world[2] * tex->vecs[i][2] +
        tex->vecs[i][3];
    }
    return coord;
}

faceextents_t::faceextents_t(const bsp2_dface_t *face, const bsp2_t *bsp, float lmscale)
    : m_lightmapscale(lmscale)
{
    m_worldToTexCoord = WorldToTexSpace(bsp, face);
    m_texCoordToWorld = TexSpaceToWorld(bsp, face);
    
    glm::vec2 mins(VECT_MAX, VECT_MAX);
    glm::vec2 maxs(-VECT_MAX, -VECT_MAX);
    
    for (int i = 0; i < face->numedges; i++) {
        const glm::vec3 worldpoint = Face_PointAtIndex_E(bsp, face, i);
        const glm::vec2 texcoord = WorldToTexCoord_HighPrecision(bsp, face, worldpoint);
        
        // self test
        auto texcoordRT = this->worldToTexCoord(worldpoint);
        auto worldpointRT = this->texCoordToWorld(texcoord);
        Q_assert(glm::bvec2(true, true) == glm::epsilonEqual(texcoordRT, texcoord, 0.1f));
        Q_assert(glm::bvec3(true, true, true) == glm::epsilonEqual(worldpointRT, worldpoint, 0.1f));
        // end self test
        
        for (int j = 0; j < 2; j++) {
            if (texcoord[j] < mins[j])
                mins[j] = texcoord[j];
            if (texcoord[j] > maxs[j])
                maxs[j] = texcoord[j];
        }
    }
    
    for (int i = 0; i < 2; i++) {
        mins[i] = floor(mins[i] / m_lightmapscale);
        maxs[i] = ceil(maxs[i] / m_lightmapscale);
        m_texmins[i] = static_cast<int>(mins[i]);
        m_texsize[i] = static_cast<int>(maxs[i] - mins[i]);
        
        if (m_texsize[i] >= MAXDIMENSION) {
            const plane_t plane = Face_Plane(bsp, face);
            const glm::vec3 point = Face_PointAtIndex_E(bsp, face, 0); // grab first vert
            const char *texname = Face_TextureName(bsp, face);
            
            Error("Bad surface extents:\n"
                  "   surface %d, %s extents = %d, scale = %g\n"
                  "   texture %s at (%s)\n"
                  "   surface normal (%s)\n",
                  Face_GetNum(bsp, face), i ? "t" : "s", m_texsize[i], m_lightmapscale,
                  texname, glm::to_string(point).c_str(),
                  VecStrf(plane.normal));
        }
    }
}

int faceextents_t::width() const { return m_texsize[0] + 1; }
int faceextents_t::height() const { return m_texsize[1] + 1; }
int faceextents_t::numsamples() const { return width() * height(); }
glm::ivec2 faceextents_t::texsize() const { return glm::ivec2(width(), height()); }

int faceextents_t::indexOf(const glm::ivec2 &lm) const {
    Q_assert(lm.x >= 0 && lm.x < width());
    Q_assert(lm.y >= 0 && lm.y < height());
    return lm.x + (width() * lm.y);
}

glm::ivec2 faceextents_t::intCoordsFromIndex(int index) const {
    Q_assert(index >= 0);
    Q_assert(index < numsamples());
    
    glm::ivec2 res(index % width(), index / width());
    Q_assert(indexOf(res) == index);
    return res;
}

glm::vec2 faceextents_t::LMCoordToTexCoord(const glm::vec2 &LMCoord) const {
    const glm::vec2 res(m_lightmapscale * (m_texmins[0] + LMCoord.x),
                        m_lightmapscale * (m_texmins[1] + LMCoord.y));
    return res;
}

glm::vec2 faceextents_t::TexCoordToLMCoord(const glm::vec2 &tc) const {
    const glm::vec2 res((tc.x / m_lightmapscale) - m_texmins[0],
                        (tc.y / m_lightmapscale) - m_texmins[1]);
    return res;
}

glm::vec2 faceextents_t::worldToTexCoord(glm::vec3 world) const {
    const glm::vec4 worldPadded(world[0], world[1], world[2], 1.0f);
    const glm::vec4 res = m_worldToTexCoord * worldPadded;
    
    Q_assert(res[3] == 1.0f);
    
    return glm::vec2( res[0], res[1] );
}

glm::vec3 faceextents_t::texCoordToWorld(glm::vec2 tc) const {
    const glm::vec4 tcPadded(tc[0], tc[1], 0.0f, 1.0f);
    const glm::vec4 res = m_texCoordToWorld * tcPadded;
    
    Q_assert(fabs(res[3] - 1.0f) < 0.01f);
    
    return glm::vec3( res[0], res[1], res[2] );
}

glm::vec2 faceextents_t::worldToLMCoord(glm::vec3 world) const {
    return TexCoordToLMCoord(worldToTexCoord(world));
}

glm::vec3 faceextents_t::LMCoordToWorld(glm::vec2 lm) const {
    return texCoordToWorld(LMCoordToTexCoord(lm));
}
