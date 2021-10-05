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

#include <common/bsputils.hh>
#include <common/cmdlib.hh>
#include <common/mesh.hh>
#include <common/octree.hh>

#include <common/mathlib.hh>

#include <iterator>
#include <set>
#include <map>

using namespace std;

mesh_t buildMesh(const vector<vector<qvec3f>> &faces)
{
    int nextVert = 0;
    map<qvec3f, int> posToVertIndex;

    vector<qplane3f> faceplanes;
    vector<vector<int>> facesWithIndices;
    facesWithIndices.reserve(faces.size());

    for (const auto &face : faces) {
        vector<int> vertIndices;
        vertIndices.reserve(face.size());

        // compute face plane
        qvec4f gp = GLM_PolyPlane(face);
        qplane3f &qp = faceplanes.emplace_back(qvec3f(gp[0], gp[1], gp[2]), gp[3]);

        for (const auto &vert : face) {
            float distOff = qp.distAbove(vert);
            Q_assert(fabs(distOff) < 0.001);

            const auto it = posToVertIndex.find(vert);

            if (it == posToVertIndex.end()) {
                posToVertIndex[vert] = nextVert;
                vertIndices.push_back(nextVert);
                nextVert++;
            } else {
                int vertIndex = it->second;
                vertIndices.push_back(vertIndex);
            }
        }

        facesWithIndices.push_back(vertIndices);
    }

    // convert posToVertIndex to a vector
    vector<qvec3f> vertsVec;
    vertsVec.resize(posToVertIndex.size());

    for (const auto &posIndex : posToVertIndex) {
        vertsVec.at(posIndex.second) = posIndex.first;
    }

    mesh_t res;
    res.verts = vertsVec;
    res.faces = facesWithIndices;
    res.faceplanes = faceplanes;
    return res;
}

mesh_t buildMeshFromBSP(const mbsp_t *bsp)
{
    mesh_t res;
    std::copy(bsp->dvertexes.begin(), bsp->dvertexes.end(), std::back_inserter(res.verts));

    for (auto &f : bsp->dfaces) {
        // grab face verts
        std::vector<vertnum_t> face;
        face.reserve(f.numedges);
        for (int j = 0; j < f.numedges; j++) {
            face.push_back(Face_VertexAtIndex(bsp, &f, j));
        }
        res.faces.push_back(face);

        // grab exact plane
        res.faceplanes.push_back(Face_Plane(bsp, &f));
    }

    return res;
}

std::vector<std::vector<qvec3f>> meshToFaces(const mesh_t &mesh)
{
    std::vector<std::vector<qvec3f>> res;
    res.reserve(mesh.faces.size());
    for (const auto &meshFace : mesh.faces) {
        std::vector<qvec3f> &points = res.emplace_back();
        points.reserve(meshFace.size());
        for (int vertIndex : meshFace) {
            points.push_back(mesh.verts.at(vertIndex));
        }
    }
    Q_assert(res.size() == mesh.faces.size());
    return res;
}

aabb3f mesh_face_bbox(const mesh_t &mesh, facenum_t facenum)
{
    const std::vector<int> &face = mesh.faces.at(facenum);

    const qvec3f &vert0 = mesh.verts.at(face.at(0));
    aabb3f bbox(vert0, vert0);

    for (int vert_i : face) {
        const qvec3f &vert = mesh.verts.at(vert_i);
        bbox = bbox.expand(vert);
    }
    return bbox;
}

static octree_t<vertnum_t> build_vert_octree(const mesh_t &mesh)
{
    std::vector<std::pair<aabb3f, vertnum_t>> vertBboxNumPairs;

    for (int i = 0; i < mesh.verts.size(); i++) {
        const qvec3f &vert = mesh.verts[i];
        const aabb3f bbox(vert, vert);

        vertBboxNumPairs.emplace_back(bbox, i);
    }

    return makeOctree(vertBboxNumPairs);
}

/**
 * Possibly insert vert `vnum` on one of the edges of face `fnum`, if it happens
 * to lie on one of the edges.
 */
void face_InsertVertIfNeeded(mesh_t &mesh, facenum_t fnum, vertnum_t vnum)
{
    meshface_t &face = mesh.faces.at(fnum);
    const qplane3f &faceplane = mesh.faceplanes.at(fnum);
    const qvec3f potentialVertPos = mesh.verts.at(vnum);

    const float distOff = faceplane.distAbove(potentialVertPos);
    if (fabs(distOff) > TJUNC_DIST_EPSILON)
        return; // not on the face plane

    // N.B. we will modify the `face` std::vector within this loop
    for (int i = 0; i < face.size(); i++) {
        const qvec3f &v0 = mesh.verts.at(i);
        const qvec3f &v1 = mesh.verts.at((i + 1) % face.size());

        // does `potentialVertPos` lie on the line between `v0` and `v1`?
        float distToLine = DistToLine(v0, v1, potentialVertPos);
        if (distToLine > TJUNC_DIST_EPSILON)
            continue;

        // N.B.: not a distance
        float fracOfLine = FractionOfLine(v0, v1, potentialVertPos);
        if (fracOfLine < 0 || fracOfLine > 1)
            continue;

        // do it
        auto it = face.begin();
        std::advance(it, i + 1);
        face.insert(it, vnum);
        Q_assert(face.at(i + 1) == vnum);
        return;
    }

    // didn't do it
    return;
}

template<class T>
static set<T> vecToSet(const vector<T> &vec)
{
    set<T> res;
    for (const auto &item : vec) {
        res.insert(item);
    }
    return res;
}

void cleanupFace(mesh_t &mesh, facenum_t i, const octree_t<vertnum_t> &vertoctree)
{
    aabb3f facebbox = mesh_face_bbox(mesh, i);
    facebbox = facebbox.grow(qvec3f(1));

    const set<vertnum_t> face_vert_set = vecToSet(mesh.faces.at(i));
    const vector<vertnum_t> nearbyverts = vertoctree.queryTouchingBBox(facebbox);

    for (vertnum_t vnum : nearbyverts) {
        // skip verts that are already on the face
        if (face_vert_set.find(vnum) != face_vert_set.end()) {
            continue;
        }

        // possibly add this vert
        face_InsertVertIfNeeded(mesh, i, vnum);
    }
}

void cleanupMesh(mesh_t &mesh)
{
    const octree_t<vertnum_t> vertoctree = build_vert_octree(mesh);

    for (size_t i = 0; i < mesh.faces.size(); i++) {
        cleanupFace(mesh, i, vertoctree);
    }
}
