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
#include <vector>
#include <cassert>
#include <limits>

static constexpr float MAX_SKY_RAY_DEPTH = 8192.0f;

/**
 * i is between 0 and face->numedges - 1
 */
// TODO: move elsewhere
static int VertAtIndex(const bsp2_t *bsp, const bsp2_dface_t *face, const int i)
{
        int edge = bsp->dsurfedges[face->firstedge + i];
        int vert = (edge >= 0) ? bsp->dedges[edge].v[0] : bsp->dedges[-edge].v[1];
        return vert;
}

class sceneinfo {
public:
    unsigned geomID;

    std::vector<const bsp2_dface_t *> triToFace;
};

sceneinfo
CreateGeometry(const bsp2_t *bsp, RTCScene scene, const std::vector<const bsp2_dface_t *> &faces)
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

RTCDevice device;
RTCScene scene;
/* global shadow casters */
sceneinfo skygeom, solidgeom, fencegeom, selfshadowgeom;

static const bsp2_t *bsp_static;

void ErrorCallback(const RTCError code, const char* str)
{
    printf("RTC Error %d: %s\n", code, str);
}

static const sceneinfo &
Embree_SceneinfoForGeomID(unsigned int geomID)
{
    if (geomID == skygeom.geomID) {
        return skygeom;
    } else if (geomID == solidgeom.geomID) {
        return solidgeom;
    } else if (geomID == fencegeom.geomID) {
        return fencegeom;
    } else if (geomID == selfshadowgeom.geomID) {
        return selfshadowgeom;
    } else {
        Error("unexpected geomID");
    }
}

const bsp2_dface_t *Embree_LookupFace(unsigned int geomID, unsigned int primID)
{
    const sceneinfo &info = Embree_SceneinfoForGeomID(geomID);
    return info.triToFace.at(primID);
}

void Embree_RayEndpoint(const RTCRay& ray, vec3_t endpoint)
{
    vec3_t dir;
    VectorCopy(ray.dir, dir);
    VectorNormalize(dir);
    
    VectorMA(ray.org, ray.tfar, dir, endpoint);
}

// called to evaluate transparency
static void
Embree_FilterFunc(void* userDataPtr, RTCRay&   ray)
{
    // bail if we hit a selfshadow face, but the ray is not coming from within that model
    if (ray.mask == 0 && ray.geomID == selfshadowgeom.geomID) {
        // reject hit
        ray.geomID = RTC_INVALID_GEOMETRY_ID;
        return;
    }
    
    // test fence texture
    const bsp2_dface_t *face = Embree_LookupFace(ray.geomID, ray.primID);
    
    // bail if it's not a fence
    const char *name = Face_TextureName(bsp_static, face);
    if (name[0] != '{')
        return;
    
    vec3_t hitpoint;
    Embree_RayEndpoint(ray, hitpoint);
    const int sample = SampleTexture(face, bsp_static, hitpoint);
    
    if (sample == 255) {
        // reject hit
        ray.geomID = RTC_INVALID_GEOMETRY_ID;
    }
}


void
Embree_TraceInit(const bsp2_t *bsp)
{
    bsp_static = bsp;
    assert(tracelist != NULL);
    assert(device == nullptr);
    
    std::vector<const bsp2_dface_t *> skyfaces, solidfaces, fencefaces, selfshadowfaces;
    
    /* Check against the list of global shadow casters */
    for (const modelinfo_t *const *model = tracelist; *model; model++) {
        for (int i=0; i<(*model)->model->numfaces; i++) {
            const bsp2_dface_t *face = &bsp->dfaces[(*model)->model->firstface + i];
            const char *texname = Face_TextureName(bsp, face);
            
            if (!strncmp("sky", texname, 3)) {
                skyfaces.push_back(face);
            } else if (texname[0] == '{') {
                fencefaces.push_back(face);
            } else {
                solidfaces.push_back(face);
            }
        }
    }
    
    /* Self-shadow models */
    for (const modelinfo_t *const *model = selfshadowlist; *model; model++) {
        for (int i=0; i<(*model)->model->numfaces; i++) {
            const bsp2_dface_t *face = &bsp->dfaces[(*model)->model->firstface + i];
            selfshadowfaces.push_back(face);
        }
    }
    
    device = rtcNewDevice();
    rtcDeviceSetErrorFunction(device, ErrorCallback);
    
    // we use the ray mask field to store the dmodel index of the self-shadow model
    if (0 != rtcDeviceGetParameter1i(device, RTC_CONFIG_RAY_MASK)) {
        Error("embree must be built with ray masks disabled");
    }

    scene = rtcDeviceNewScene(device, RTC_SCENE_STATIC | RTC_SCENE_COHERENT, RTC_INTERSECT1);
    skygeom = CreateGeometry(bsp, scene, skyfaces);
    solidgeom = CreateGeometry(bsp, scene, solidfaces);
    fencegeom = CreateGeometry(bsp, scene, fencefaces);
    selfshadowgeom = CreateGeometry(bsp, scene, selfshadowfaces);
    
    rtcSetIntersectionFilterFunction(scene, fencegeom.geomID, Embree_FilterFunc);
    rtcSetOcclusionFilterFunction(scene, fencegeom.geomID, Embree_FilterFunc);

    rtcSetIntersectionFilterFunction(scene, selfshadowgeom.geomID, Embree_FilterFunc);
    rtcSetOcclusionFilterFunction(scene, selfshadowgeom.geomID, Embree_FilterFunc);
    
    rtcCommit (scene);
    
    logprint("Embree_TraceInit: %d skyfaces %d solidfaces %d fencefaces %d selfshadowfaces\n",
           (int)skyfaces.size(),
           (int)solidfaces.size(),
           (int)fencefaces.size(),
           (int)selfshadowfaces.size());
}

static RTCRay SetupRay(const vec3_t start, const vec3_t dir, vec_t dist, const dmodel_t *self)
{
    RTCRay ray;
    VectorCopy(start, ray.org);
    VectorCopy(dir, ray.dir); // can be un-normalized
    ray.tnear = 0.f;
    ray.tfar = dist;
    ray.geomID = RTC_INVALID_GEOMETRY_ID;
    ray.primID = RTC_INVALID_GEOMETRY_ID;
    ray.instID = RTC_INVALID_GEOMETRY_ID;
    
    // NOTE: we are not using the ray masking feature of embree, but just using
    // this field to store whether the ray is coming from self-shadow geometry
    ray.mask = (self == nullptr) ? 0 : 1;
    ray.time = 0.f;
    return ray;
}

static RTCRay SetupRay_StartStop(const vec3_t start, const vec3_t stop, const dmodel_t *self)
{
    vec3_t dir;
    VectorSubtract(stop, start, dir);
    vec_t dist = VectorNormalize(dir);
    
    return SetupRay(start, dir, dist, self);
}

//public
qboolean Embree_TestLight(const vec3_t start, const vec3_t stop, const dmodel_t *self)
{
    RTCRay ray = SetupRay_StartStop(start, stop, self);
    rtcOccluded(scene, ray);
    
    if (ray.geomID != RTC_INVALID_GEOMETRY_ID)
        return false; //hit
    
    // no obstruction
    return true;
}

//public
qboolean Embree_TestSky(const vec3_t start, const vec3_t dirn, const dmodel_t *self)
{
    // trace from the sample point towards the sun, and
    // return true if we hit a sky poly.
    
    vec3_t dir_normalized;
    VectorCopy(dirn, dir_normalized);
    VectorNormalize(dir_normalized);
    
    RTCRay ray = SetupRay(start, dir_normalized, MAX_SKY_RAY_DEPTH, self);
    rtcIntersect(scene, ray);

    qboolean hit_sky = (ray.geomID == skygeom.geomID);
    return hit_sky;
}

//public
qboolean Embree_DirtTrace(const vec3_t start, const vec3_t dirn, vec_t dist, const dmodel_t *self, vec_t *hitdist_out, plane_t *hitplane_out, const bsp2_dface_t **face_out)
{
    RTCRay ray = SetupRay(start, dirn, dist, self);
    rtcIntersect(scene, ray);
    
    if (ray.geomID == RTC_INVALID_GEOMETRY_ID
        || ray.geomID == skygeom.geomID)
        return false;
    
    if (hitdist_out) {
        *hitdist_out = ray.tfar;
    }
    if (hitplane_out) {
        for (int i=0; i<3; i++) {
            hitplane_out->normal[i] = ray.Ng[i];
        }
        VectorNormalize(hitplane_out->normal);
        
        vec3_t hitpoint;
        VectorMA(start, ray.tfar, dirn, hitpoint);
        
        hitplane_out->dist = DotProduct(hitplane_out->normal, hitpoint);
    }
    if (face_out) {
        const sceneinfo &si = Embree_SceneinfoForGeomID(ray.geomID);
        *face_out = si.triToFace.at(ray.primID);
    }
    
    return true;
}
