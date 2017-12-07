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

#include <common/qvec.hh>

class neighbour_t {
public:
    const bsp2_dface_t *face;
    qvec3f p0, p1;
    
    neighbour_t(const bsp2_dface_t *f, const qvec3f p0in, const qvec3f p1in)
    : face(f),
    p0(p0in),
    p1(p1in) {
    }
};

std::vector<neighbour_t> FacesOverlappingEdge(const vec3_t p0, const vec3_t p1, const mbsp_t *bsp, const dmodel_t *model);

void CalcualateVertexNormals(const mbsp_t *bsp);
const qvec3f GetSurfaceVertexNormal(const mbsp_t *bsp, const bsp2_dface_t *f, const int vertindex);
bool FacesSmoothed(const bsp2_dface_t *f1, const bsp2_dface_t *f2);
const std::set<const bsp2_dface_t *> &GetSmoothFaces(const bsp2_dface_t *face);
const std::vector<const bsp2_dface_t *> &GetPlaneFaces(const bsp2_dface_t *face);
const qvec3f GetSurfaceVertexNormal(const mbsp_t *bsp, const bsp2_dface_t *f, const int v);
const bsp2_dface_t *Face_EdgeIndexSmoothed(const mbsp_t *bsp, const bsp2_dface_t *f, const int edgeindex);

/// a directed edge can be used by more than one face, e.g. two cube touching just along an edge
using edgeToFaceMap_t = std::map<std::pair<int,int>, std::vector<const bsp2_dface_t *>>;

std::vector<neighbour_t> NeighbouringFaces_new(const mbsp_t *bsp, const bsp2_dface_t *face);
std::vector<const bsp2_dface_t *> FacesUsingVert(int vertnum);
const edgeToFaceMap_t &GetEdgeToFaceMap();

class face_cache_t {
private:
    std::vector<qvec3f> m_points;
    std::vector<qvec3f> m_normals;
    qvec4f m_plane;
    std::vector<qvec4f> m_edgePlanes;
    std::vector<qvec3f> m_pointsShrunkBy1Unit;
    std::vector<neighbour_t> m_neighbours;
    
public:
    face_cache_t(const mbsp_t *bsp, const bsp2_dface_t *face, const std::vector<qvec3f> &normals) :
        m_points(GLM_FacePoints(bsp, face)),
        m_normals(normals),
        m_plane(Face_Plane_E(bsp, face).vec4()),
        m_edgePlanes(GLM_MakeInwardFacingEdgePlanes(m_points)),
        m_pointsShrunkBy1Unit(GLM_ShrinkPoly(m_points, 1.0f)),
    	m_neighbours(NeighbouringFaces_new(bsp, face))
    { }
    
    const std::vector<qvec3f> &points() const {
        return m_points;
    }
    const std::vector<qvec3f> &normals() const {
        return m_normals;
    }
    const qvec4f &plane() const {
        return m_plane;
    }
    const qvec3f normal() const {
        return qvec3f(m_plane);
    }
    const std::vector<qvec4f> &edgePlanes() const {
        return m_edgePlanes;
    }
    const std::vector<qvec3f> &pointsShrunkBy1Unit() const {
        return m_pointsShrunkBy1Unit;
    }
    const std::vector<neighbour_t> &neighbours() const {
        return m_neighbours;
    }
};

const face_cache_t &FaceCacheForFNum(int fnum);

#endif /* __LIGHT_PHONG_H__ */
