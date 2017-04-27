/*  Copyright (C) 2017 Eric Wasylishen

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

#ifndef __COMMON_MESH_HH__
#define __COMMON_MESH_HH__

#include <vector>
#include <common/bspfile.hh>
#include <common/qvec.hh>
#include <common/aabb.hh>

using facenum_t = int;
using vertnum_t = int;
using meshface_t = std::vector<vertnum_t>;

#define TJUNC_DIST_EPSILON 0.01

class mesh_t {
public:
    std::vector<qvec3f> verts;
    std::vector<meshface_t> faces;
    // this is redundant data with the verts, but we know the planes in advance
    // and this saves having to estimate them from the verts
    std::vector<qplane3f> faceplanes;
};

// Welds vertices at exactly the same position
mesh_t buildMesh(const std::vector<std::vector<qvec3f>> &faces);
mesh_t buildMeshFromBSP(const bsp2_t *bsp);

std::vector<std::vector<qvec3f>> meshToFaces(const mesh_t &mesh);
aabb3f mesh_face_bbox(const mesh_t &mesh, facenum_t facenum);

// Preserves the number and order of faces.
// doesn't merge verts.
// adds verts to fix t-juncs
void cleanupMesh(mesh_t &mesh);

class sample_position_t {
public:
    bool m_occluded;
    facenum_t m_face;
    qvec3f m_position;
    qvec3f m_interpolatedNormal;
    
    sample_position_t(qvec3f position)
    : m_occluded(false),
      m_face(-1),
      m_position(position),
      m_interpolatedNormal(qvec3f(0,0,0)) {}
    
    sample_position_t(facenum_t actualFace,
                      qvec3f position,
                      qvec3f interpolatedNormal)
    : m_occluded(false),
      m_face(actualFace),
      m_position(position),
      m_interpolatedNormal(interpolatedNormal) {};
};

sample_position_t positionSample(const mesh_t &mesh, facenum_t startingFace, const qvec3f &startingPos);

#endif /* __COMMON_MESH_HH__ */
