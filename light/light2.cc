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

// Rotates face1Norm about the line segment p0 <-> p1 so it is aligned with face0Norm
pair<bool, glm::mat4x4> RotationAboutLineSegment(glm::vec3 p0, glm::vec3 p1,
                                                 glm::vec3 face0Norm, glm::vec3 face1Norm)
{
    // Get rotation angle. Quaternion rotates face1Norm to face0Norm
    const glm::quat rotationQuat = glm::rotation(face1Norm, face0Norm);
    
    // Any point on the line p0-p1 will work, so just use p0
    const glm::mat4x4 toOrigin = glm::translate(-p0);
    const glm::mat4x4 fromOrigin = glm::translate(p0);
    
    const glm::mat4x4 composed = fromOrigin * glm::toMat4(rotationQuat) * toOrigin;
    return make_pair(true, composed);
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

edgeToFaceMap_t MakeEdgeToFaceMap(const bsp2_t *bsp) {
    edgeToFaceMap_t result;
    
    for (int i = 0; i < bsp->numfaces; i++) {
        const bsp2_dface_t *f = &bsp->dfaces[i];
        
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

using bbox2 = pair<glm::vec2, glm::vec2>;
using bbox3 = pair<glm::vec3, glm::vec3>;

using contribface_stackframe_t = pair<const bsp2_dface_t *, glm::mat4x4>;

// Returns the next stack frames to process
vector<contribface_stackframe_t> SetupContributingFaces_R(const contribface_stackframe_t &frame,
                                                          const bsp2_t *bsp,
                                                          const edgeToFaceMap_t &edgeToFaceMap,
                                                          const bsp2_dface_t *refFace,
                                                          const aabb2 &refFaceTexBounds,
                                                          const aabb3 &refFaceWorldBounds,
                                                          vector<bool> *faceidx_handled,
                                                          vector<contributing_face_t> *result)
{
    const bsp2_dface_t *f = frame.first;
    const glm::mat4x4 &FWorldToRefWorld = frame.second;
    
    const int currFnum = Face_GetNum(bsp, f);
    if (faceidx_handled->at(currFnum))
        return {};
    
    // mark currentFace as handled
    faceidx_handled->at(currFnum) = true;
    
    if (!Face_IsLightmapped(bsp, f))
        return {};
    
    // Check angle between reference face and this face
    const glm::vec3 refNormal = Face_Normal_E(bsp, refFace);
    const glm::vec3 fNormal = Face_Normal_E(bsp, f);
    if (dot(fNormal, refNormal) <= 0)
        return {};
    
    
    //printf("%s\n", Face_TextureName(bsp, f));
    
    // transformFromRefFace will rotate `f` so it lies on the same plane as the reference face
    
    // convert `f` texture space to world space, apply transformFromRefFace
    // to rotate `f` onto the same plane as `f`,
    // then convert that from refFace's world space to texture space
    
    const glm::mat4x4 RefWorldToRefTex = WorldToTexSpace(bsp, refFace);
    
    // now check each vertex's position in refFace's texture space.
    // if no verts are within the range that could contribute to a sample in refFace
    // we can stop recursion and skip adding `f` to the result vector.
    const glm::mat4x4 FWorldToRefTex = RefWorldToRefTex * FWorldToRefWorld;
    bool foundNearVert = false;
    for (int j = 0; j < f->numedges; j++) {
        const int v0 = Face_VertexAtIndex(bsp, f, j);
        const glm::vec3 v0_position = Vertex_GetPos_E(bsp, v0);
        
        const glm::vec4 v0InRefTex = FWorldToRefTex * glm::vec4(v0_position[0], v0_position[1], v0_position[2], 1.0);
        const glm::vec2 v0InRefTex2f = glm::vec2(v0InRefTex);
        
        // check distance to box
        const bool near = refFaceTexBounds.grow(glm::vec2(16, 16)).contains(v0InRefTex2f);
        if (near) {
            
            const glm::vec3 v0InRefWorld = glm::vec3(FWorldToRefWorld * glm::vec4(v0_position[0], v0_position[1], v0_position[2], 1.0));
            //const float worldDist = refFaceWorldBounds.exteriorDistance(v0InRefWorld);
            
            //printf ("world distance: %f, tex dist: %f\n", worldDist, dist);
            
            foundNearVert = true;
            break;
        }
    }
    if (!foundNearVert) {
        return {};
    }
    
    const glm::mat4x4 FTexToFWorld = TexSpaceToWorld(bsp, f);
    const glm::mat4x4 FTexToRefTex = RefWorldToRefTex * FWorldToRefWorld * FTexToFWorld;
    
    // add to result (don't add the starting face though)
    if (f != refFace) {
        contributing_face_t resAddition;
        resAddition.contribFace = f;
        resAddition.refFace = refFace;
        resAddition.contribWorldToRefWorld = FWorldToRefWorld;
        resAddition.refWorldToContribWorld = glm::inverse(FWorldToRefWorld);
        resAddition.contribTexToRefTex = FTexToRefTex;
        resAddition.contribWorldToRefTex = FWorldToRefTex;
        resAddition.contribFaceEdgePlanes = GLM_MakeInwardFacingEdgePlanes(GLM_FacePoints(bsp, f));
        result->push_back(resAddition);
    }
    
    // walk edges
    
    vector<contribface_stackframe_t> nextframes;
    
    for (int j = 0; j < f->numedges; j++) {
        const int v0 = Face_VertexAtIndex(bsp, f, j);
        const int v1 = Face_VertexAtIndex(bsp, f, (j + 1) % f->numedges);
        
        const glm::vec3 v0pos = Vertex_GetPos_E(bsp, v0);
        const glm::vec3 v1pos = Vertex_GetPos_E(bsp, v1);
        
        auto it = edgeToFaceMap.find(make_pair(v1, v0));
        if (it != edgeToFaceMap.end()) {
            for (const bsp2_dface_t *neighbour : it->second) {
                const int neighbourFNum = Face_GetNum(bsp, neighbour);
                if (neighbour == f) {
                    // Invalid face, e.g. with vertex numbers: [0, 1, 0, 2]
                    continue;
                }
                
                // Check if these faces are smoothed or on the same plane
                if (!(FacesSmoothed(f, neighbour) || neighbour->planenum == f->planenum)) {
                    // Never visit this face. Since we are exploring breadth-first from the reference face,
                    // once we have a non-smoothed edge, we don't want to "loop around" and include it later.
                    faceidx_handled->at(neighbourFNum) = true;
                    continue;
                }
                
                const glm::vec3 neighbourNormal = Face_Normal_E(bsp, neighbour);
                
                const auto success = RotationAboutLineSegment(v0pos, v1pos, fNormal, neighbourNormal);
                Q_assert(success.first);
                const glm::mat4x4 NeighbourWorldToFWorld = success.second;
                const glm::mat4x4 NeighbourWorldToRefWorld = FWorldToRefWorld * NeighbourWorldToFWorld;
                
                nextframes.push_back(make_pair(neighbour, NeighbourWorldToRefWorld));
            }
        }
    }
    
    return nextframes;
}

aabb2 FaceTexBounds(const bsp2_t *bsp, const bsp2_dface_t *f)
{
    aabb2 result;
    const glm::mat4x4 T = WorldToTexSpace(bsp, f);
    
    for (int j = 0; j < f->numedges; j++) {
        const int v0 = Face_VertexAtIndex(bsp, f, j);
        
        const glm::vec3 v0_position = Vertex_GetPos_E(bsp, v0);
        
        const glm::vec4 v0Tex = T * glm::vec4(v0_position[0], v0_position[1], v0_position[2], 1.0);
        const glm::vec2 v0Tex2f = glm::vec2(v0Tex);
        
        result = result.expand(v0Tex2f);
    }
    
    return result;
}

aabb3 FaceWorldBounds(const bsp2_t *bsp, const bsp2_dface_t *f)
{
    aabb3 result;
    
    for (int j = 0; j < f->numedges; j++) {
        const int v0 = Face_VertexAtIndex(bsp, f, j);
        const glm::vec3 v0_position = Vertex_GetPos_E(bsp, v0);
        
        result = result.expand(v0_position);
    }
    
    return result;
}

/**
 * For the given face, find all other faces that can contribute samples.
 *
 * For each face that can contribute samples, makes a transformation matrix
 *   from that face's texture space to this face's.
 *
 *
 *
 */
vector<contributing_face_t> SetupContributingFaces(const bsp2_t *bsp, const bsp2_dface_t *face, const edgeToFaceMap_t &edgeToFaceMap)
{
    vector<bool> faceidx_handled(static_cast<size_t>(bsp->numfaces), false);
    vector<contributing_face_t> result;
    
    const aabb2 refFaceTexBounds = FaceTexBounds(bsp, face);
    const aabb3 refFaceWorldBounds = FaceWorldBounds(bsp, face);
    
    //    std::cout << "Face " << Face_GetNum(bsp, face)
    //        << " has tex bounds: "
    //        << refFaceTexBounds.min() << ", max:"
    //        << refFaceTexBounds.max() << std::endl
    //        << " has world bounds: "
    //        << refFaceWorldBounds.min() << ", max:"
    //        << refFaceWorldBounds.max() << std::endl;
    
    // Breadth-first search, starting with `face`.
    list<contribface_stackframe_t> queue { make_pair(face, glm::mat4x4()) };
    
    while (!queue.empty()) {
        const contribface_stackframe_t frame = queue.front();
        queue.pop_front();
        
        vector<contribface_stackframe_t> next = SetupContributingFaces_R(frame, bsp, edgeToFaceMap, face,
                                                                         refFaceTexBounds, refFaceWorldBounds, &faceidx_handled, &result);
        
        // Push the next frames on the back of the queue to get a BFS
        for (const auto &frame : next) {
            queue.push_back(frame);
        }
    }
    
    return result;
}

all_contrib_faces_t MakeContributingFaces(const bsp2_t *bsp)
{
    const edgeToFaceMap_t edgeToFaceMap = MakeEdgeToFaceMap(bsp);
    
    logprint("--- MakeContributingFaces ---\n");
    
    all_contrib_faces_t result;
    
    for (int i = 0; i < bsp->numfaces; i++) {
        const bsp2_dface_t *f = &bsp->dfaces[i];
        
        auto contrib = SetupContributingFaces(bsp, f, edgeToFaceMap);
        
        result[f] = contrib;
        //        printf("face %d (%s) has %d contributing faces\n",
        //               (int)i,
        //               Face_TextureName(bsp, f),
        //               (int)contrib.size());
    }
    
    return result;
}

static void AddFaceToBatch_R(const bsp2_t *bsp, const bsp2_dface_t *f, std::vector<bool> *faceidx_handled, std::vector<int> *batch)
{
    const int fnum = Face_GetNum(bsp, f);
    if (faceidx_handled->at(fnum))
        return;
    
    // add this face to the batch
    faceidx_handled->at(fnum) = true;
    batch->push_back(fnum);
    
    // get touching faces either on the same plane, or phong shaded
    for (const bsp2_dface_t *f2 : GetSmoothFaces(f)) {
        AddFaceToBatch_R(bsp, f2, faceidx_handled, batch);
    }
    for (const bsp2_dface_t *f2 : GetPlaneFaces(f)) {
        AddFaceToBatch_R(bsp, f2, faceidx_handled, batch);
    }
}

/**
 * Batch of faces that need to be lit together, on the same thread
 *
 * - If 2 faces are phong shaded across an edge, or line on the same plane and share an edge, they need to be lit together
 *
 * Light samples taken on one face might also contribute to other faces in a lightingbatch_t, but
 * never to faces in another lightingbatch_t.
 */

batches_t MakeLightingBatches(const bsp2_t *bsp)
{
    std::vector<bool> faceidx_handled(static_cast<size_t>(bsp->numfaces), false);
    
    batches_t batches;
    
    for (int i = 0; i < bsp->numfaces; i++) {
        if (faceidx_handled.at(i))
            continue;
        
        std::vector<int> batch;
        AddFaceToBatch_R(bsp, &bsp->dfaces[i], &faceidx_handled, &batch);
        Q_assert(batch.size() > 0);
        batches.push_back(batch);
    }
    
    //stats
    
    int sum = 0;
    int max = -1;
    for (const auto &batch : batches) {
        const int bs = static_cast<int>(batch.size());
        
        sum += bs;
        if (bs > max) {
            max = bs;
        }
    }
    Q_assert(sum == bsp->numfaces);
    
    std::cout << "batches: " << batches.size() << " largest batch: " << max << std::endl;
    
    return batches;
}

void *LightBatchThread(void *arg)
{
    lightbatchthread_info_t *info = (lightbatchthread_info_t *)arg;
    
#ifdef HAVE_EMBREE
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    //    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#endif
    
    while (1) {
        const int batchnum = GetThreadWork();
        if (batchnum == -1)
            break;
        
        const auto &batch = info->all_batches.at(batchnum);
        LightBatch(info->bsp, batch, info->all_contribFaces);
    }
    
    return NULL;
}
