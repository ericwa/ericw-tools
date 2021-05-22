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
#include <common/mesh.hh>

#include <memory>
#include <vector>
#include <map>
#include <unordered_map>
#include <set>
#include <algorithm>
#include <mutex>
#include <string>

#include <common/qvec.hh>

using namespace std;

static neighbour_t
FaceOverlapsEdge(const vec3_t p0, const vec3_t p1, const mbsp_t *bsp, const bsp2_dface_t *f)
{
    for (int edgeindex = 0; edgeindex < f->numedges; edgeindex++) {
        const int v0 = Face_VertexAtIndex(bsp, f, edgeindex);
        const int v1 = Face_VertexAtIndex(bsp, f, (edgeindex + 1) % f->numedges);
        
        const qvec3f v0point = Vertex_GetPos_E(bsp, v0);
        const qvec3f v1point = Vertex_GetPos_E(bsp, v1);
        if (LinesOverlap(vec3_t_to_glm(p0), vec3_t_to_glm(p1), v0point, v1point)) {
            return  neighbour_t{f, v0point, v1point};
        }
    }
    return neighbour_t{nullptr, qvec3f{}, qvec3f{}};
}

static void
FacesOverlappingEdge_r(const vec3_t p0, const vec3_t p1, const mbsp_t *bsp, int nodenum, vector<neighbour_t> *result)
{
    if (nodenum < 0) {
        // we don't do anything for leafs.
        // faces are handled on nodes.
        return;
    }
    
    const bsp2_dnode_t *node = BSP_GetNode(bsp, nodenum);
    const dplane_t *plane = BSP_GetPlane(bsp, node->planenum);
    const vec_t p0dist = Plane_Dist(p0, plane);
    const vec_t p1dist = Plane_Dist(p1, plane);
    
    if (fabs(p0dist) < 0.1 && fabs(p1dist) < 0.1) {
    	// check all faces on this node.
        for (int i=0; i<node->numfaces; i++) {
            const bsp2_dface_t *face = BSP_GetFace(bsp, node->firstface + i);
            const auto neighbour = FaceOverlapsEdge(p0, p1, bsp, face);
            if (neighbour.face != nullptr) {
                result->push_back(neighbour);
            }
        }
    }
    
    // recurse down front.
    // NOTE: also do this if either point almost on-node.
    // It could be on this plane, but also on some other plane further down
    // the front (or back) side.
    if (p0dist > -0.1 || p1dist > -0.1) {
        FacesOverlappingEdge_r(p0, p1, bsp, node->children[0], result);
    }
    
    // recurse down back
    if (p0dist < 0.1 || p1dist < 0.1) {
        FacesOverlappingEdge_r(p0, p1, bsp, node->children[1], result);
    }
}

/**
 * Returns faces which have an edge that overlaps the given p0-p1 edge.
 * Uses hull 0.
 */
vector<neighbour_t>
FacesOverlappingEdge(const vec3_t p0, const vec3_t p1, const mbsp_t *bsp, const dmodel_t *model)
{
    vector<neighbour_t> result;
    FacesOverlappingEdge_r(p0, p1, bsp, model->headnode[0], &result);
    return result;
}

std::vector<neighbour_t> NeighbouringFaces_new(const mbsp_t *bsp, const bsp2_dface_t *face)
{
    std::vector<neighbour_t> result;
    std::set<const bsp2_dface_t *> used_faces;
    
    for (int i=0; i<face->numedges; i++) {
        vec3_t p0, p1;
        Face_PointAtIndex(bsp, face, i, p0);
        Face_PointAtIndex(bsp, face, (i + 1) % face->numedges, p1);
        
        std::vector<neighbour_t> tmp = FacesOverlappingEdge(p0, p1, bsp, &bsp->dmodels[0]);
        
        // ensure the neighbour_t edges are pointing the same direction as the p0->p1 edge
        // (modifies them inplace)
        const qvec3f p0p1dir = qv::normalize(vec3_t_to_glm(p1) - vec3_t_to_glm(p0));
        for (auto &neighbour : tmp) {
            qvec3f neighbourDir = qv::normalize(neighbour.p1 - neighbour.p0);
            float dp = qv::dot(neighbourDir, p0p1dir); // should really be 1 or -1
            if (dp < 0) {
                std::swap(neighbour.p0, neighbour.p1);
                
//                float new_dp = qv::dot(qv::normalize(neighbour.p1 - neighbour.p0), p0p1dir);
//                Q_assert(new_dp > 0);
            }
        }
        
        for (const auto &neighbour : tmp) {
            if (neighbour.face != face && used_faces.find(neighbour.face) == used_faces.end()) {
                used_faces.insert(neighbour.face);
                result.push_back(neighbour);
            }
        }
    }
    
    return result;
}

/* return 0 if either vector is zero-length */
static float
AngleBetweenVectors(const qvec3f &d1, const qvec3f &d2)
{
    float length_product = (qv::length(d1)*qv::length(d2));
    if (length_product == 0)
        return 0;
    float cosangle = qv::dot(d1, d2)/length_product;
    if (cosangle < -1) cosangle = -1;
    if (cosangle > 1) cosangle = 1;
    
    float angle = acos(cosangle);
    return angle;
}

/* returns the angle between vectors p2->p1 and p2->p3 */
static float
AngleBetweenPoints(const qvec3f &p1, const qvec3f &p2, const qvec3f &p3)
{
    const qvec3f d1 = p1 - p2;
    const qvec3f d2 = p3 - p2;
    float result = AngleBetweenVectors(d1, d2);
    return result;
}

static bool s_builtPhongCaches;
static std::map<const bsp2_dface_t *, std::vector<qvec3f>> vertex_normals;
static std::set<int> interior_verts;
static map<const bsp2_dface_t *, set<const bsp2_dface_t *>> smoothFaces;
static map<int, vector<const bsp2_dface_t *>> vertsToFaces;
static map<int, vector<const bsp2_dface_t *>> planesToFaces;
static edgeToFaceMap_t EdgeToFaceMap;
static vector<face_cache_t> FaceCache;

vector<const bsp2_dface_t *> FacesUsingVert(int vertnum)
{
    const auto &vertsToFaces_const = vertsToFaces;
    
    auto it = vertsToFaces_const.find(vertnum);
    if (it != vertsToFaces_const.end())
        return it->second;
    return {};
}

const edgeToFaceMap_t &GetEdgeToFaceMap()
{
    Q_assert(s_builtPhongCaches);
    return EdgeToFaceMap;
}

// Uses `smoothFaces` static var
bool FacesSmoothed(const bsp2_dface_t *f1, const bsp2_dface_t *f2)
{
    Q_assert(s_builtPhongCaches);
    
    const auto &facesIt = smoothFaces.find(f1);
    if (facesIt == smoothFaces.end())
        return false;
    
    const set<const bsp2_dface_t *> &faceSet = facesIt->second;
    if (faceSet.find(f2) == faceSet.end())
        return false;
    
    return true;
}

const std::set<const bsp2_dface_t *> &GetSmoothFaces(const bsp2_dface_t *face)
{
    Q_assert(s_builtPhongCaches);
    
    static std::set<const bsp2_dface_t *> empty;
    const auto it = smoothFaces.find(face);
    
    if (it == smoothFaces.end())
        return empty;
    
    return it->second;
}

const std::vector<const bsp2_dface_t *> &GetPlaneFaces(const bsp2_dface_t *face)
{
    Q_assert(s_builtPhongCaches);
    
    static std::vector<const bsp2_dface_t *> empty;
    const auto it = planesToFaces.find(face->planenum);
    
    if (it == planesToFaces.end())
        return empty;
    
    return it->second;
}


/* given a triangle, just adds the contribution from the triangle to the given vertexes normals, based upon angles at the verts.
 * v1, v2, v3 are global vertex indices */
static void
AddTriangleNormals(std::map<int, qvec3f> &smoothed_normals, const qvec3f &norm, const mbsp_t *bsp, int v1, int v2, int v3)
{
    const qvec3f p1 = Vertex_GetPos_E(bsp, v1);
    const qvec3f p2 = Vertex_GetPos_E(bsp, v2);
    const qvec3f p3 = Vertex_GetPos_E(bsp, v3);
    float weight;
    
    float areaweight = GLM_TriangleArea(p1, p2, p3);
    if (!std::isfinite(areaweight)) {
        areaweight = 0;
    }
    
    weight = AngleBetweenPoints(p2, p1, p3);
    weight *= areaweight;
    smoothed_normals[v1] = smoothed_normals[v1] + (norm * weight);

    weight = AngleBetweenPoints(p1, p2, p3);
    weight *= areaweight;
    smoothed_normals[v2] = smoothed_normals[v2] + (norm * weight);

    weight = AngleBetweenPoints(p1, p3, p2);
    weight *= areaweight;
    smoothed_normals[v3] = smoothed_normals[v3] + (norm * weight);
}

/* access the final phong-shaded vertex normal */
const qvec3f GetSurfaceVertexNormal(const mbsp_t *bsp, const bsp2_dface_t *f, const int vertindex)
{
    Q_assert(s_builtPhongCaches);
    
    // handle degenerate faces
    const auto it = vertex_normals.find(f);
    if (it == vertex_normals.end()) {
        return qvec3f(0,0,0);
    }
    const auto &face_normals_vec = it->second;
    return face_normals_vec.at(vertindex);
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
Face_EdgeIndexSmoothed(const mbsp_t *bsp, const bsp2_dface_t *f, const int edgeindex) 
{
    Q_assert(s_builtPhongCaches);
    
    const int v0 = Face_VertexAtIndex(bsp, f, edgeindex);
    const int v1 = Face_VertexAtIndex(bsp, f, (edgeindex + 1) % f->numedges);

    auto it = EdgeToFaceMap.find(make_pair(v1, v0));
    if (it != EdgeToFaceMap.end()) {
        for (const bsp2_dface_t *neighbour : it->second) {
            if (neighbour == f) {
                // Invalid face, e.g. with vertex numbers: [0, 1, 0, 2]
                continue;
            }

            const bool sameplane = (neighbour->planenum == f->planenum
                                    && neighbour->side == f->side);

            // Check if these faces are smoothed or on the same plane
            if (!(FacesSmoothed(f, neighbour) || sameplane)) {
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

static edgeToFaceMap_t MakeEdgeToFaceMap(const mbsp_t *bsp)
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

static vector<qvec3f> Face_VertexNormals(const mbsp_t *bsp, const bsp2_dface_t *face)
{
    vector<qvec3f> normals;
    for (int i=0; i<face->numedges; i++) {
        const qvec3f n = GetSurfaceVertexNormal(bsp, face, i);
        normals.push_back(n);
    }
    return normals;
}

static vector<face_cache_t> MakeFaceCache(const mbsp_t *bsp)
{
    vector<face_cache_t> result;
    for (int i=0; i<bsp->numfaces; i++) {
        const bsp2_dface_t *face = BSP_GetFace(bsp, i);
        result.push_back(face_cache_t{bsp, face, Face_VertexNormals(bsp, face)});
    }
    return result;
}

/**
 * Q2: Returns nonzero if phong is requested on this face, in which case that is
 * the face tag to smooth with. Otherwise returns 0.
 */
static int Q2_FacePhongValue(const mbsp_t *bsp, const bsp2_dface_t *face) {
    const gtexinfo_t* texinfo = BSP_GetTexinfo(bsp, face->texinfo);
    if (texinfo != nullptr) {
        if (texinfo->value != 0
            && ((texinfo->flags & Q2_SURF_LIGHT) == 0)) {
            return texinfo->value;
        }
    }
    return 0;
}

void
CalcualateVertexNormals(const mbsp_t *bsp)
{
    logprint("--- CalcualateVertexNormals ---\n");

    Q_assert(!s_builtPhongCaches);
    s_builtPhongCaches = true;
    
    EdgeToFaceMap = MakeEdgeToFaceMap(bsp);
    
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
        const bsp2_dface_t *f = BSP_GetFace(const_cast<mbsp_t *>(bsp), i);
        
        const auto f_points = GLM_FacePoints(bsp, f);
        const qvec3f f_norm = Face_Normal_E(bsp, f);
        const qplane3f f_plane = Face_Plane_E(bsp, f);
        
        // any face normal within this many degrees can be smoothed with this face
        const int f_phong_angle = (extended_texinfo_flags[f->texinfo] & TEX_PHONG_ANGLE_MASK) >> TEX_PHONG_ANGLE_SHIFT;
        int f_phong_angle_concave = (extended_texinfo_flags[f->texinfo] & TEX_PHONG_ANGLE_CONCAVE_MASK) >> TEX_PHONG_ANGLE_CONCAVE_SHIFT;
        if (f_phong_angle_concave == 0) {
            f_phong_angle_concave = f_phong_angle;
        }
        const bool f_wants_phong = (f_phong_angle || f_phong_angle_concave);
        
        if (!f_wants_phong)
            continue;
        
        for (int j = 0; j < f->numedges; j++) {
            const int v = Face_VertexAtIndex(bsp, f, j);
            // walk over all faces incident to f (we will walk over neighbours multiple times, doesn't matter)
            for (const bsp2_dface_t *f2 : vertsToFaces[v]) {
                if (f2 == f)
                    continue;
                
                // FIXME: factor out and share with above?
                const int f2_phong_angle = (extended_texinfo_flags[f2->texinfo] & TEX_PHONG_ANGLE_MASK) >> TEX_PHONG_ANGLE_SHIFT;
                int f2_phong_angle_concave = (extended_texinfo_flags[f2->texinfo] & TEX_PHONG_ANGLE_CONCAVE_MASK) >> TEX_PHONG_ANGLE_CONCAVE_SHIFT;
                if (f2_phong_angle_concave == 0) {
                    f2_phong_angle_concave = f2_phong_angle;
                }
                const bool f2_wants_phong = (f2_phong_angle || f2_phong_angle_concave);
                
                if (!f2_wants_phong)
                    continue;
                
                const auto f2_points = GLM_FacePoints(bsp, f2);
                const qvec3f f2_centroid = GLM_PolyCentroid(f2_points);
                const qvec3f f2_norm = Face_Normal_E(bsp, f2);
                
                const vec_t cosangle = qv::dot(f_norm, f2_norm);
                
                const bool concave = f_plane.distAbove(f2_centroid) > 0.1;
                const vec_t f_threshold = concave ? f_phong_angle_concave : f_phong_angle;
                const vec_t f2_threshold = concave ? f2_phong_angle_concave : f2_phong_angle;
                const vec_t min_threshold = qmin(f_threshold, f2_threshold);
                const vec_t cosmaxangle = cos(DEG2RAD(min_threshold));

                // check the angle between the face normals
                if (cosangle >= cosmaxangle) {
                    smoothFaces[f].insert(f2);
                }
            }
        }
    }
    
    // Q2: build the "face -> faces to smooth with" map
    for (int i = 0; i < bsp->numfaces; i++) {
        const bsp2_dface_t *f = BSP_GetFace(const_cast<mbsp_t *>(bsp), i);
        const int f_phongValue = Q2_FacePhongValue(bsp, f);
        if (f_phongValue == 0)
            continue;

        for (int j = 0; j < f->numedges; j++) {
            const int v = Face_VertexAtIndex(bsp, f, j);
            // walk over all faces incident to f (we will walk over neighbours multiple times, doesn't matter)
            for (const bsp2_dface_t *f2 : vertsToFaces[v]) {
                if (f2 == f)
                    continue;

                const int f2_phongValue = Q2_FacePhongValue(bsp, f2);
                if (f_phongValue != f2_phongValue)
                    continue;

                // we've already checked f_phongValue is nonzero, so smooth these two faces.
                smoothFaces[f].insert(f2);
            }
        }
    }

    // finally do the smoothing for each face
    for (int i = 0; i < bsp->numfaces; i++)
    {
        const bsp2_dface_t *f = BSP_GetFace(bsp, i);
        if (f->numedges < 3) {
            logprint("CalcualateVertexNormals: face %d is degenerate with %d edges\n", i, f->numedges);
            for (int j = 0; j<f->numedges; j++) {
                vec3_t pt;
                Face_PointAtIndex(bsp, f, j, pt);
                logprint("                         vert at %f %f %f\n", pt[0], pt[1], pt[2]);
            }
            continue;
        }
        
        const auto &neighboursToSmooth = smoothFaces[f];
        const qvec3f f_norm = Face_Normal_E(bsp, f); // get the face normal
        
        // gather up f and neighboursToSmooth
        std::vector<const bsp2_dface_t *> fPlusNeighbours;
        fPlusNeighbours.push_back(f);
        for (auto neighbour : neighboursToSmooth) {
            fPlusNeighbours.push_back(neighbour);
        }
        
        // global vertex index -> smoothed normal
        std::map<int, qvec3f> smoothedNormals;

        // walk fPlusNeighbours
        for (auto f2 : fPlusNeighbours) {
            const qvec3f f2_norm = Face_Normal_E(bsp, f2);
            
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
            const qvec3f vertNormal = pair.second;
            if (0 == qv::length(vertNormal)) {
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
                pair.second = qv::normalize(vertNormal);
            }
        }
        
        // sanity check
        if (!neighboursToSmooth.size()) {
            for (auto vertIndexNormalPair : smoothedNormals) {
                Q_assert(GLMVectorCompare(vertIndexNormalPair.second, f_norm, EQUAL_EPSILON));
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

const face_cache_t &FaceCacheForFNum(int fnum)
{
    Q_assert(s_builtPhongCaches);
    return FaceCache.at(fnum);
}
