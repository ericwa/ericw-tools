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

#pragma once

#include <set>
#include <map>
#include <vector>

#include <common/qvec.hh>

struct mbsp_t;
struct mface_t;

struct neighbour_t
{
    const mface_t *face;
    qvec3f p0, p1;
};

void CalculateVertexNormals(const mbsp_t *bsp);
const face_normal_t &GetSurfaceVertexNormal(const mbsp_t *bsp, const mface_t *f, const int vertindex);
bool FacesSmoothed(const mface_t *f1, const mface_t *f2);
const std::set<const mface_t *> &GetSmoothFaces(const mface_t *face);
const std::vector<const mface_t *> &GetPlaneFaces(const mface_t *face);
const mface_t *Face_EdgeIndexSmoothed(const mbsp_t *bsp, const mface_t *f, const int edgeindex);
int Q2_FacePhongValue(const mbsp_t *bsp, const mface_t *face);

/// a directed edge can be used by more than one face, e.g. two cube touching just along an edge
using edgeToFaceMap_t = std::map<std::pair<int, int>, std::vector<const mface_t *>>;

std::vector<neighbour_t> NeighbouringFaces_new(const mbsp_t *bsp, const mface_t *face);
std::vector<const mface_t *> FacesUsingVert(int vertnum);
const edgeToFaceMap_t &GetEdgeToFaceMap();

class face_cache_t
{
private:
    std::vector<qvec3f> m_points;
    std::vector<face_normal_t> m_normals;
    qvec4f m_plane;
    std::vector<qvec4f> m_edgePlanes;
    std::vector<qvec3f> m_pointsShrunkBy1Unit;
    std::vector<neighbour_t> m_neighbours;

public:
    face_cache_t();
    face_cache_t(const mbsp_t *bsp, const mface_t *face, const std::vector<face_normal_t> &normals);

    const std::vector<qvec3f> &points() const { return m_points; }
    const std::vector<face_normal_t> &normals() const { return m_normals; }
    const qvec4f &plane() const { return m_plane; }
    const qvec3f normal() const { return m_plane; }
    const std::vector<qvec4f> &edgePlanes() const { return m_edgePlanes; }
    const std::vector<qvec3f> &pointsShrunkBy1Unit() const { return m_pointsShrunkBy1Unit; }
    const std::vector<neighbour_t> &neighbours() const { return m_neighbours; }
};

const face_cache_t &FaceCacheForFNum(int fnum);
void ResetPhong();
