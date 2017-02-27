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
#include <common/bsputils.hh>
#include <common/bspfile.hh>
#include <common/log.hh>

#include <light/light.hh>

#include <set>
#include <map>
#include <vector>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

void CalcualateVertexNormals(const bsp2_t *bsp);
const glm::vec3 GetSurfaceVertexNormal(const bsp2_t *bsp, const bsp2_dface_t *f, const int vertindex);
bool FacesSmoothed(const bsp2_dface_t *f1, const bsp2_dface_t *f2);
const std::set<const bsp2_dface_t *> &GetSmoothFaces(const bsp2_dface_t *face);
const std::vector<const bsp2_dface_t *> &GetPlaneFaces(const bsp2_dface_t *face);
const glm::vec3 GetSurfaceVertexNormal(const bsp2_t *bsp, const bsp2_dface_t *f, const int v);
const bsp2_dface_t *Face_EdgeIndexSmoothed(const bsp2_t *bsp, const bsp2_dface_t *f, const int edgeindex);

/// a directed edge can be used by more than one face, e.g. two cube touching just along an edge
using edgeToFaceMap_t = std::map<std::pair<int,int>, std::vector<const bsp2_dface_t *>>;

const edgeToFaceMap_t &GetEdgeToFaceMap();

class face_cache_t {
private:
    std::vector<glm::vec3> m_points;
    std::vector<glm::vec3> m_normals;
    glm::vec4 m_plane;
    std::vector<glm::vec4> m_edgePlanes;
    std::vector<glm::vec3> m_pointsShrunkBy1Unit;
    
public:
    face_cache_t(const bsp2_t *bsp, const bsp2_dface_t *face, const std::vector<glm::vec3> &normals) :
        m_points(GLM_FacePoints(bsp, face)),
        m_normals(normals),
        m_plane(Face_Plane_E(bsp, face)),
        m_edgePlanes(GLM_MakeInwardFacingEdgePlanes(m_points)),
        m_pointsShrunkBy1Unit(GLM_ShrinkPoly(m_points, 1.0f))
    { }
    
    const std::vector<glm::vec3> &points() const {
        return m_points;
    }
    const std::vector<glm::vec3> &normals() const {
        return m_normals;
    }
    const glm::vec4 &plane() const {
        return m_plane;
    }
    const glm::vec3 normal() const {
        return glm::vec3(m_plane);
    }
    const std::vector<glm::vec4> &edgePlanes() const {
        return m_edgePlanes;
    }
    const std::vector<glm::vec3> &pointsShrunkBy1Unit() const {
        return m_pointsShrunkBy1Unit;
    }
};

const face_cache_t &FaceCacheForFNum(int fnum);

#endif /* __LIGHT_PHONG_H__ */
