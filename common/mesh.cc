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

#include <common/cmdlib.hh>
#include <common/mesh.hh>
#include <common/octree.hh>

#include <map>

using namespace std;

mesh_t buildMesh(const vector<vector<qvec3f>> &faces)
{
    // FIXME: this is ugly
    using pos_t = tuple<float, float, float>;
    
    int nextVert = 0;
    map<pos_t, int> posToVertIndex;
    
    vector<vector<int>> facesWithIndices;
    for (const auto &face : faces) {
        vector<int> vertIndices;
        
        for (const auto &vert : face) {
            const pos_t pos = make_tuple(vert[0], vert[1], vert[2]);
            const auto it = posToVertIndex.find(pos);
            
            if (it == posToVertIndex.end()) {
                posToVertIndex[pos] = nextVert;
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
        const pos_t &pos = posIndex.first;
        vertsVec.at(posIndex.second) = qvec3f(std::get<0>(pos), std::get<1>(pos), std::get<2>(pos));
    }
    
    mesh_t res;
    res.verts = vertsVec;
    res.faces = facesWithIndices;
    return res;
}

std::vector<std::vector<qvec3f>> meshToFaces(const mesh_t &mesh)
{
    std::vector<std::vector<qvec3f>> res;
    for (const auto &meshFace : mesh.faces) {
        std::vector<qvec3f> points;
        for (int vertIndex : meshFace) {
            const qvec3f point = mesh.verts.at(vertIndex);
            points.push_back(point);
        }
        res.push_back(points);
    }
    Q_assert(res.size() == mesh.faces.size());
    return res;
}


static aabb3f mesh_face_bbox(const mesh_t &mesh, int facenum)
{
    const std::vector<int> &face = mesh.faces.at(facenum);
    
}

void cleanupMesh(mesh_t &mesh)
{
    using facenum_t = int;
    
    std::vector<std::pair<aabb3f, facenum_t>> faces;
    
    octree_t<facenum_t> octree = makeOctree(faces);
    
}
