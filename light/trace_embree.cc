/*  Copyright (C) 2016 Eric Wasylishen

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

#include <light/light.h>
#include <embree2/rtcore.h>
#include <embree2/rtcore_ray.h>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <cassert>
#include <limits>

/**
 * i is between 0 and face->numedges - 1
 */
static int VertAtIndex(const bsp2_t *bsp, const bsp2_dface_t *face, const int i)
{
        int edge = bsp->dsurfedges[face->firstedge + i];
        int vert = (edge >= 0) ? bsp->dedges[edge].v[0] : bsp->dedges[-edge].v[1];
        return vert;
}

static const miptex_t *
MiptexForFace(const bsp2_t *bsp, const bsp2_dface_t *face)
{
    const texinfo_t *tex;
    dmiptexlump_t *miplump = bsp->dtexdata.header;
    const miptex_t *miptex;
    tex = &bsp->texinfo[face->texinfo];
    
    int dataofs = miplump->dataofs[tex->miptex];
    if (dataofs == -1)
        return NULL;
    
    miptex = (miptex_t*)(bsp->dtexdata.base + dataofs);
    return miptex;
}

class sceneinfo {
public:
    unsigned geomID;

    std::vector<const bsp2_dface_t *> triToFace;
};

sceneinfo
CreateGeometry(const bsp2_t *bsp, RTCScene scene, const std::unordered_set<const bsp2_dface_t *> &faces)
{
    // count triangles
    int numtris = 0;
    for (const bsp2_dface_t *face : faces) {
        if (face->numedges < 3)
            continue;
        numtris += (face->numedges - 2);
    }
    
    unsigned geomID = rtcNewTriangleMesh(scene, RTC_GEOMETRY_STATIC, numtris, bsp->numvertexes);
    
    struct Vertex   { float point[4]; }; //4th element is padding
    struct Triangle { int v0, v1, v2; };
    
    // fill in vertices
    Vertex* vertices = (Vertex*) rtcMapBuffer(scene, geomID, RTC_VERTEX_BUFFER);
    for (int i=0; i<bsp->numvertexes; i++) {
        const dvertex_t *dvertex = &bsp->dvertexes[i];
        Vertex *vert = &vertices[i];
        for (int j=0; j<3; j++) {
            vert->point[j] = dvertex->point[j];
        }
    }
    rtcUnmapBuffer(scene, geomID, RTC_VERTEX_BUFFER);
    
    sceneinfo s;
    s.geomID = geomID;
    
    // fill in triangles
    Triangle* triangles = (Triangle*) rtcMapBuffer(scene, geomID, RTC_INDEX_BUFFER);
    int tri_index = 0;
    for (const bsp2_dface_t *face : faces) {
        if (face->numedges < 3)
            continue;
        
        for (int j = 2; j < face->numedges; j++) {
            Triangle *tri = &triangles[tri_index];
            tri->v0 = VertAtIndex(bsp, face, j-1);
            tri->v1 = VertAtIndex(bsp, face, j);
            tri->v2 = VertAtIndex(bsp, face, 0);
            tri_index++;
            
            s.triToFace.push_back(face);
        }
    }
    rtcUnmapBuffer(scene, geomID, RTC_INDEX_BUFFER);
    
    return s;
}

RTCScene scene;
/* global shadow casters */
sceneinfo skygeom, solidgeom;

/* self-shadow models for all bmodels which are NOT global shadow casters.
 * these are only used for: 
 *  - dirtmapping
 *  - making bmodels self-shadow if it's explicitly set with the _shadow flag
 */
std::unordered_map<const dmodel_t *, RTCScene> selfshadowSceneForDModel;

void ErrorCallback(const RTCError code, const char* str)
{
    printf("RTC Error %d: %s\n", code, str);
}

void
MakeTnodes_embree(const bsp2_t *bsp)
{
    assert(tracelist != NULL);
    
    std::unordered_set<const bsp2_dface_t *> skyfaces, solidfaces;
    
    /* Check against the list of global shadow casters */
    for (const dmodel_t *const *model = tracelist; *model; model++) {
        for (int i=0; i<(*model)->numfaces; i++) {
            const bsp2_dface_t *face = &bsp->dfaces[(*model)->firstface + i];
            const miptex_t *miptex = MiptexForFace(bsp, face);
            
            if (miptex != NULL && !strncmp("sky", miptex->name, 3)) {
                skyfaces.insert(face);
            } else {
                solidfaces.insert(face);
            }
        }
    }
    
    rtcInit(NULL);
    rtcSetErrorFunction(ErrorCallback);

    scene = rtcNewScene(RTC_SCENE_STATIC, RTC_INTERSECT1);
    skygeom = CreateGeometry(bsp, scene, skyfaces);
    solidgeom = CreateGeometry(bsp, scene, solidfaces);
    
    rtcCommit (scene);
    
    printf("MakeTnodes: created sky geometry with %d faces and solid with %d faces\n",
           (int)skyfaces.size(),
           (int)solidfaces.size());
    
    /* Create self-shadow models */
    
    for (int i=0; i<bsp->nummodels; i++) {
        const dmodel_t *model = &bsp->dmodels[i];

        // N.B. All faces are considered opaque for the selfshadow models
        
        std::unordered_set<const bsp2_dface_t *> faces;
        for (int j=0; j<model->numfaces; j++) {
            const bsp2_dface_t *face = &bsp->dfaces[model->firstface + j];
            faces.insert(face);
        }
        
        RTCScene selfshadowscene = rtcNewScene(RTC_SCENE_STATIC | RTC_SCENE_HIGH_QUALITY | RTC_SCENE_ROBUST, RTC_INTERSECT1);
        CreateGeometry(bsp, selfshadowscene, faces);
        rtcCommit (selfshadowscene);
        selfshadowSceneForDModel[model] = selfshadowscene;
    }
}

static RTCRay SetupRay(const vec3_t start, const vec3_t dir, vec_t dist)
{
    RTCRay ray;
    VectorCopy(start, ray.org);
    VectorCopy(dir, ray.dir); // can be un-normalized
    ray.tnear = 0.f;
    ray.tfar = dist;
    ray.geomID = RTC_INVALID_GEOMETRY_ID;
    ray.primID = RTC_INVALID_GEOMETRY_ID;
    ray.instID = RTC_INVALID_GEOMETRY_ID;
    ray.mask = 0xFFFFFFFF;
    ray.time = 0.f;
    return ray;
}

qboolean
TestLight_embree(const vec3_t start, const vec3_t dir, vec_t dist, const modelinfo_t *model)
{
    RTCRay ray = SetupRay(start, dir, dist);
    rtcOccluded(scene, ray);
    
    if (ray.geomID != RTC_INVALID_GEOMETRY_ID)
        return false; //hit
    
    if (model->shadowself) {
        // check the selfshadow model
        RTCScene selfshadowscene = selfshadowSceneForDModel[model->model];
        ray = SetupRay(start, dir, dist);
        rtcOccluded(selfshadowscene, ray);
        
        if (ray.geomID != RTC_INVALID_GEOMETRY_ID)
            return false; //hit
    }
    
    // no obstruction
    return true;
}

qboolean
TestSky_embree(const vec3_t start, const vec3_t dirn, const modelinfo_t *model)
{
    // trace from the sample point towards the sun, and
    // return true if we hit a sky poly.
    
    RTCRay ray = SetupRay(start, dirn, std::numeric_limits<float>::infinity());
    rtcIntersect(scene, ray);

    qboolean hit_sky = (ray.geomID == skygeom.geomID);
    
    if (hit_sky && model->shadowself) {
        // sky is visible, check for an obstruction from the selfshadow
        RTCScene selfshadowscene = selfshadowSceneForDModel[model->model];
        ray = SetupRay(start, dirn, std::numeric_limits<float>::infinity());
        rtcIntersect(selfshadowscene, ray);
        
        hit_sky = (ray.geomID == RTC_INVALID_GEOMETRY_ID);
    }
    
    return hit_sky;
}

/*
 * ============
 * DirtTrace
 *
 * returns true if the trace from start to stop hits something solid,
 * or if it started in the void.
 * ============
 */
qboolean
DirtTrace_embree(const vec3_t start, const vec3_t dir, vec_t dist, vec_t *hitdist, vec_t *normal, const modelinfo_t *model)
{
    RTCRay ray = SetupRay(start, dir, dist);
    rtcIntersect(scene, ray);
    
    if (ray.geomID != solidgeom.geomID) {
        // don't re-check the world's self-shadow model because it's already part of 'scene'
        if (model->model != tracelist[0]) {
            RTCScene selfshadowscene = selfshadowSceneForDModel[model->model];
            ray = SetupRay(start, dir, dist);
            rtcIntersect(selfshadowscene, ray);
            
            if (ray.geomID == RTC_INVALID_GEOMETRY_ID) {
                return false; //no hit
            }
        } else {
            // no self shadow model. no hit
            return false;
        }
    }
    
    // check if the ray was coming from behind the hit surface (it started in the void)
    {
        VectorNormalize(ray.Ng);
        vec_t rayHitPlaneAngle = DotProduct(ray.Ng, dir);
        if (rayHitPlaneAngle > 0) { // pointing in the same direction
            ray.tfar = 0;
        }
    }
    
    if (hitdist)
        *hitdist = ray.tfar;
    if (normal)
    {
        VectorCopy(ray.Ng, normal);
    }
    return true;
}

qboolean
FaceTrace_embree(const vec3_t start, const vec3_t dir, vec3_t hitpoint, const bsp2_dface_t **hitface)
{
    RTCRay ray = SetupRay(start, dir, 512);
    rtcIntersect(scene, ray);
    
    if (ray.geomID != solidgeom.geomID) {
        return false;
    }
    
    VectorMA(start, ray.tfar, dir, hitpoint);
    *hitface = solidgeom.triToFace[ray.primID];
    
    return true;
}

/*
 * ============
 * CalcPointsTrace
 *
 * returns true if the trace from start to stop hits something solid.
 * ============
 */
qboolean
CalcPointsTrace_embree(const vec3_t start, const vec3_t dir, vec_t dist, vec_t *hitdist, vec_t *normal, const modelinfo_t *model)
{
    RTCScene selfshadowscene = selfshadowSceneForDModel[model->model];
    RTCRay ray = SetupRay(start, dir, dist);
    rtcIntersect(selfshadowscene, ray);
    
    // if there is no hit, but we were tracning on a submodel, also test against the world.
    if (ray.geomID == RTC_INVALID_GEOMETRY_ID
        && model->model != tracelist[0]) {
        ray = SetupRay(start, dir, dist);
        rtcIntersect(scene, ray);
    }
    
    if (hitdist)
        *hitdist = ray.tfar;
    if (normal)
        VectorCopy(ray.Ng, normal);

    return ray.geomID != RTC_INVALID_GEOMETRY_ID;
}
