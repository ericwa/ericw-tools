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

#include <light/phong.hh>

#include <common/polylib.hh>
#include <common/bsputils.hh>

#include <memory>
#include <vector>
#include <map>
#include <unordered_map>
#include <set>
#include <algorithm>
#include <mutex>
#include <string>

#include <glm/glm.hpp>

using namespace std;
using namespace glm;

/* return 0 if either vector is zero-length */
static float
AngleBetweenVectors(const vec3 &d1, const vec3 &d2)
{
    float length_product = (length(d1)*length(d2));
    if (length_product == 0)
        return 0;
    float cosangle = dot(d1, d2)/length_product;
    if (cosangle < -1) cosangle = -1;
    if (cosangle > 1) cosangle = 1;
    
    float angle = acos(cosangle);
    return angle;
}

/* returns the angle between vectors p2->p1 and p2->p3 */
static float
AngleBetweenPoints(const vec3 &p1, const vec3 &p2, const vec3 &p3)
{
    const vec3 d1 = p1 - p2;
    const vec3 d2 = p3 - p2;
    float result = AngleBetweenVectors(d1, d2);
    return result;
}

static std::map<const bsp2_dface_t *, std::vector<vec3>> vertex_normals;
static std::set<int> interior_verts;
static map<const bsp2_dface_t *, set<const bsp2_dface_t *>> smoothFaces;
static map<int, vector<const bsp2_dface_t *>> vertsToFaces;
static map<int, vector<const bsp2_dface_t *>> planesToFaces;
static edgeToFaceMap_t EdgeToFaceMap;
static vector<face_cache_t> FaceCache;

const edgeToFaceMap_t &GetEdgeToFaceMap()
{
    return EdgeToFaceMap;
}

// Uses `smoothFaces` static var
bool FacesSmoothed(const bsp2_dface_t *f1, const bsp2_dface_t *f2)
{
    const auto &facesIt = smoothFaces.find(f1);
    if (facesIt == smoothFaces.end())
        return false;
    
    const set<const bsp2_dface_t *> &faceSet = facesIt->second;
    if (faceSet.find(f2) == faceSet.end())
        return false;
    
    return true;
}

const std::set<const bsp2_dface_t *> &GetSmoothFaces(const bsp2_dface_t *face) {
    static std::set<const bsp2_dface_t *> empty;
    const auto it = smoothFaces.find(face);
    
    if (it == smoothFaces.end())
        return empty;
    
    return it->second;
}

const std::vector<const bsp2_dface_t *> &GetPlaneFaces(const bsp2_dface_t *face) {
    static std::vector<const bsp2_dface_t *> empty;
    const auto it = planesToFaces.find(face->planenum);
    
    if (it == planesToFaces.end())
        return empty;
    
    return it->second;
}


/* given a triangle, just adds the contribution from the triangle to the given vertexes normals, based upon angles at the verts.
 * v1, v2, v3 are global vertex indices */
static void
AddTriangleNormals(std::map<int, vec3> &smoothed_normals, const vec3 &norm, const bsp2_t *bsp, int v1, int v2, int v3)
{
    const vec3 p1 = Vertex_GetPos_E(bsp, v1);
    const vec3 p2 = Vertex_GetPos_E(bsp, v2);
    const vec3 p3 = Vertex_GetPos_E(bsp, v3);
    float weight;
    
    weight = AngleBetweenPoints(p2, p1, p3);
    smoothed_normals[v1] = smoothed_normals[v1] + (weight * norm);

    weight = AngleBetweenPoints(p1, p2, p3);
    smoothed_normals[v2] = smoothed_normals[v2] + (weight * norm);

    weight = AngleBetweenPoints(p1, p3, p2);
    smoothed_normals[v3] = smoothed_normals[v3] + (weight * norm);
}

/* access the final phong-shaded vertex normal */
const glm::vec3 GetSurfaceVertexNormal(const bsp2_t *bsp, const bsp2_dface_t *f, const int vertindex)
{
    const auto &face_normals_vector = vertex_normals.at(f);
    return face_normals_vector.at(vertindex);
}

static bool
FacesOnSamePlane(const std::vector<const bsp2_dface_t *> &faces)
{
    if (faces.empty()) {
        return false;
    }
    const int32_t planenum = faces.at(0)->planenum;
    for (auto face : faces) {
        if (face->planenum != planenum) {
            return false;
        }
    }
    return true;
}

const bsp2_dface_t *
Face_EdgeIndexSmoothed(const bsp2_t *bsp, const bsp2_dface_t *f, const int edgeindex) 
{
    const int v0 = Face_VertexAtIndex(bsp, f, edgeindex);
    const int v1 = Face_VertexAtIndex(bsp, f, (edgeindex + 1) % f->numedges);

    auto it = EdgeToFaceMap.find(make_pair(v1, v0));
    if (it != EdgeToFaceMap.end()) {
        for (const bsp2_dface_t *neighbour : it->second) {
            if (neighbour == f) {
                // Invalid face, e.g. with vertex numbers: [0, 1, 0, 2]
                continue;
            }
            
            // Check if these faces are smoothed or on the same plane
            if (!(FacesSmoothed(f, neighbour) || neighbour->planenum == f->planenum)) {
                continue;
            }

            return neighbour;
        }
    }
    return nullptr;
    
#if 0
    if (smoothFaces.find(f) == smoothFaces.end()) {
        return nullptr;
    }
    
    int v0 = Face_VertexAtIndex(bsp, f, edgeindex);
    int v1 = Face_VertexAtIndex(bsp, f, (edgeindex + 1) % f->numedges);
    
    const auto &v0_faces = vertsToFaces.at(v0);
    const auto &v1_faces = vertsToFaces.at(v1);
    
    // find a face f2 that has both verts v0 and v1
    for (auto f2 : v0_faces) {
        if (f2 == f)
            continue;
        if (find(v1_faces.begin(), v1_faces.end(), f2) != v1_faces.end()) {
            const auto &f_smoothfaces = smoothFaces.at(f);
            bool smoothed = (f_smoothfaces.find(f2) != f_smoothfaces.end());
            return smoothed ? f2 : nullptr;
        }
    }
    return nullptr;
#endif
}

static edgeToFaceMap_t MakeEdgeToFaceMap(const bsp2_t *bsp)
{
    edgeToFaceMap_t result;
    
    for (int i = 0; i < bsp->numfaces; i++) {
        const bsp2_dface_t *f = BSP_GetFace(bsp, i);
        
        // walk edges
        for (int j = 0; j < f->numedges; j++) {
            const int v0 = Face_VertexAtIndex(bsp, f, j);
            const int v1 = Face_VertexAtIndex(bsp, f, (j + 1) % f->numedges);
            
            if (v0 == v1) {
                // ad_swampy.bsp has faces with repeated verts...
                continue;
            }
            
            const auto edge = make_pair(v0, v1);
            auto &edgeFacesRef = result[edge];
            
            Q_assert(find(begin(edgeFacesRef), end(edgeFacesRef), f) == end(edgeFacesRef));
            edgeFacesRef.push_back(f);
        }
    }
    
    return result;
}

static vector<vec3> Face_VertexNormals(const bsp2_t *bsp, const bsp2_dface_t *face)
{
    vector<vec3> normals;
    for (int i=0; i<face->numedges; i++) {
        const glm::vec3 n = GetSurfaceVertexNormal(bsp, face, i);
        normals.push_back(n);
    }
    return normals;
}

static vector<face_cache_t> MakeFaceCache(const bsp2_t *bsp)
{
    vector<face_cache_t> result;
    for (int i=0; i<bsp->numfaces; i++) {
        const bsp2_dface_t *face = BSP_GetFace(bsp, i);
        result.push_back(face_cache_t{bsp, face, Face_VertexNormals(bsp, face)});
    }
    return result;
}

void
CalcualateVertexNormals(const bsp2_t *bsp)
{
    EdgeToFaceMap = MakeEdgeToFaceMap(bsp);
    
    // clear in case we are run twice
    vertex_normals.clear();
    interior_verts.clear();
    smoothFaces.clear();
    vertsToFaces.clear();
    
    // read _phong and _phong_angle from entities for compatiblity with other qbsp's, at the expense of no
    // support on func_detail/func_group
    for (int i=0; i<bsp->nummodels; i++) {
        const modelinfo_t *info = ModelInfoForModel(bsp, i);
        const uint8_t phongangle_byte = (uint8_t) qmax(0, qmin(255, (int)rint(info->getResolvedPhongAngle())));

        if (!phongangle_byte)
            continue;
        
        for (int j=info->model->firstface; j < info->model->firstface + info->model->numfaces; j++) {
            const bsp2_dface_t *f = BSP_GetFace(bsp, j);
            
            extended_texinfo_flags[f->texinfo] &= ~(TEX_PHONG_ANGLE_MASK);
            extended_texinfo_flags[f->texinfo] |= (phongangle_byte << TEX_PHONG_ANGLE_SHIFT);
        }
    }
    
    // build "plane -> faces" map
    for (int i = 0; i < bsp->numfaces; i++) {
        const bsp2_dface_t *f = BSP_GetFace(bsp, i);
        planesToFaces[f->planenum].push_back(f);
    }
    
    // build "vert index -> faces" map
    for (int i = 0; i < bsp->numfaces; i++) {
        const bsp2_dface_t *f = BSP_GetFace(bsp, i);
        for (int j = 0; j < f->numedges; j++) {
            const int v = Face_VertexAtIndex(bsp, f, j);
            vertsToFaces[v].push_back(f);
        }
    }
    
    // track "interior" verts, these are in the middle of a face, and mess up normal interpolation
    for (int i=0; i<bsp->numvertexes; i++) {
        auto &faces = vertsToFaces[i];
        if (faces.size() > 1 && FacesOnSamePlane(faces)) {
            interior_verts.insert(i);
        }
    }
    //printf("CalcualateVertexNormals: %d interior verts\n", (int)interior_verts.size());
    
    // build the "face -> faces to smooth with" map
    for (int i = 0; i < bsp->numfaces; i++) {
        bsp2_dface_t *f = BSP_GetFace(const_cast<bsp2_t *>(bsp), i);
        
        const vec3 f_norm = Face_Normal_E(bsp, f);
        
        // any face normal within this many degrees can be smoothed with this face
        const int f_smoothangle = (extended_texinfo_flags[f->texinfo] & TEX_PHONG_ANGLE_MASK) >> TEX_PHONG_ANGLE_SHIFT;
        if (!f_smoothangle)
            continue;
        
        for (int j = 0; j < f->numedges; j++) {
            const int v = Face_VertexAtIndex(bsp, f, j);
            // walk over all faces incident to f (we will walk over neighbours multiple times, doesn't matter)
            for (const bsp2_dface_t *f2 : vertsToFaces[v]) {
                if (f2 == f)
                    continue;
                
                const int f2_smoothangle = (extended_texinfo_flags[f2->texinfo] & TEX_PHONG_ANGLE_MASK) >> TEX_PHONG_ANGLE_SHIFT;
                if (!f2_smoothangle)
                    continue;
                
                const vec3 f2_norm = Face_Normal_E(bsp, f2);

                const vec_t cosangle = dot(f_norm, f2_norm);
                const vec_t cosmaxangle = cos(DEG2RAD(qmin(f_smoothangle, f2_smoothangle)));
                
                // check the angle between the face normals
                if (cosangle >= cosmaxangle) {
                    smoothFaces[f].insert(f2);
                }
            }
        }
    }
    
    // finally do the smoothing for each face
    for (int i = 0; i < bsp->numfaces; i++)
    {
        const bsp2_dface_t *f = BSP_GetFace(bsp, i);
        if (f->numedges < 3) {
            logprint("CalcualateVertexNormals: face %d is degenerate with %d edges\n", i, f->numedges);
            continue;
        }
        
        const auto &neighboursToSmooth = smoothFaces[f];
        const vec3 f_norm = Face_Normal_E(bsp, f); // get the face normal
        
        // gather up f and neighboursToSmooth
        std::vector<const bsp2_dface_t *> fPlusNeighbours;
        fPlusNeighbours.push_back(f);
        for (auto neighbour : neighboursToSmooth) {
            fPlusNeighbours.push_back(neighbour);
        }
        
        // global vertex index -> smoothed normal
        std::map<int, vec3> smoothedNormals;

        // walk fPlusNeighbours
        for (auto f2 : fPlusNeighbours) {
            const vec3 f2_norm = Face_Normal_E(bsp, f2);
            
            /* now just walk around the surface as a triangle fan */
            int v1, v2, v3;
            v1 = Face_VertexAtIndex(bsp, f2, 0);
            v2 = Face_VertexAtIndex(bsp, f2, 1);
            for (int j = 2; j < f2->numedges; j++)
            {
                v3 = Face_VertexAtIndex(bsp, f2, j);
                AddTriangleNormals(smoothedNormals, f2_norm, bsp, v1, v2, v3);
                v2 = v3;
            }
        }
        
        // normalize vertex normals (NOTE: updates smoothedNormals map)
        for (auto &pair : smoothedNormals) {
            const int vertIndex = pair.first;
            const vec3 vertNormal = pair.second;
            if (0 == length(vertNormal)) {
                // this happens when there are colinear vertices, which give zero-area triangles,
                // so there is no contribution to the normal of the triangle in the middle of the
                // line. Not really an error, just set it to use the face normal.
#if 0
                logprint("Failed to calculate normal for vertex %d at (%f %f %f)\n",
                         vertIndex,
                         bsp->dvertexes[vertIndex].point[0],
                         bsp->dvertexes[vertIndex].point[1],
                         bsp->dvertexes[vertIndex].point[2]);
#endif
                pair.second = f_norm;
            }
            else
            {
                pair.second = normalize(vertNormal);
            }
        }
        
        // sanity check
        if (!neighboursToSmooth.size()) {
            for (auto vertIndexNormalPair : smoothedNormals) {
                Q_assert(GLMVectorCompare(vertIndexNormalPair.second, f_norm));
            }
        }
        
        // now, record all of the smoothed normals that are actually part of `f`
        for (int j=0; j<f->numedges; j++) {
            int v = Face_VertexAtIndex(bsp, f, j);
            Q_assert(smoothedNormals.find(v) != smoothedNormals.end());
            
            vertex_normals[f].push_back(smoothedNormals[v]);
        }
    }
    
    FaceCache = MakeFaceCache(bsp);
}

const face_cache_t &FaceCacheForFNum(int fnum) {
    return FaceCache.at(fnum);
}
