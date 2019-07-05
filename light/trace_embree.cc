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

#include <light/light.hh>
#include <light/bounce.hh>
#include <light/trace_embree.hh>
#include <light/ltface.hh>
#include <common/bsputils.hh>
#include <common/polylib.hh>
#include <embree2/rtcore.h>
#include <embree2/rtcore_ray.h>
#include <vector>
#include <cassert>
#include <cstdlib>
#include <limits>

#ifdef _MSC_VER
#include <malloc.h>
#endif

using namespace std;
using namespace polylib;

class sceneinfo {
public:
    unsigned geomID;

    std::vector<const bsp2_dface_t *> triToFace;
    std::vector<const modelinfo_t *> triToModelinfo;
};

sceneinfo
CreateGeometry(const mbsp_t *bsp, RTCScene scene, const std::vector<const bsp2_dface_t *> &faces)
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
        
        const modelinfo_t *modelinfo = ModelInfoForFace(bsp, Face_GetNum(bsp, face));
        
        for (int j = 2; j < face->numedges; j++) {
            Triangle *tri = &triangles[tri_index];
            tri->v0 = Face_VertexAtIndex(bsp, face, j-1);
            tri->v1 = Face_VertexAtIndex(bsp, face, j);
            tri->v2 = Face_VertexAtIndex(bsp, face, 0);
            tri_index++;
            
            s.triToFace.push_back(face);
            s.triToModelinfo.push_back(modelinfo);
        }
    }
    rtcUnmapBuffer(scene, geomID, RTC_INDEX_BUFFER);
    
    return s;
}

void
CreateGeometryFromWindings(RTCScene scene, const std::vector<winding_t *> &windings)
{
    if (windings.empty())
        return;
    
    // count triangles
    int numtris = 0;
    int numverts = 0;
    for (const auto &winding : windings) {
        Q_assert(winding->numpoints >= 3);
        numtris += (winding->numpoints - 2);
        numverts += winding->numpoints;
    }
    
    const unsigned geomID = rtcNewTriangleMesh(scene, RTC_GEOMETRY_STATIC, numtris, numverts);
    
    struct Vertex   { float point[4]; }; //4th element is padding
    struct Triangle { int v0, v1, v2; };
    
    // fill in vertices
    Vertex* vertices = (Vertex*) rtcMapBuffer(scene, geomID, RTC_VERTEX_BUFFER);
    {
        int vert_index = 0;
        for (const auto &winding : windings) {
            for (int j=0; j<winding->numpoints; j++) {
                for (int k=0; k<3; k++) {
                    vertices[vert_index + j].point[k] = winding->p[j][k];
                }
            }
            vert_index += winding->numpoints;
        }
    }
    rtcUnmapBuffer(scene, geomID, RTC_VERTEX_BUFFER);
    
    // fill in triangles
    Triangle* triangles = (Triangle*) rtcMapBuffer(scene, geomID, RTC_INDEX_BUFFER);
    int tri_index = 0;
    int vert_index = 0;
    for (const auto &winding : windings) {
        for (int j = 2; j < winding->numpoints; j++) {
            Triangle *tri = &triangles[tri_index];
            tri->v0 = vert_index + (j-1);
            tri->v1 = vert_index + j;
            tri->v2 = vert_index + 0;
            tri_index++;
        }
        vert_index += winding->numpoints;
    }
    Q_assert(vert_index == numverts);
    Q_assert(tri_index == numtris);
    rtcUnmapBuffer(scene, geomID, RTC_INDEX_BUFFER);
}

RTCDevice device;
RTCScene scene;

sceneinfo skygeom;    // sky. always occludes.
sceneinfo solidgeom;  // solids. always occludes.
sceneinfo filtergeom; // conditional occluders.. needs to run ray intersection filter

static const mbsp_t *bsp_static;

void ErrorCallback(void* userptr, const RTCError code, const char* str)
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
    } else if (geomID == filtergeom.geomID) {
        return filtergeom;
    } else {
        Error("unexpected geomID");
        throw; //mxd. Added to silence compiler warning
    }
}

const bsp2_dface_t *Embree_LookupFace(unsigned int geomID, unsigned int primID)
{
    const sceneinfo &info = Embree_SceneinfoForGeomID(geomID);
    return info.triToFace.at(primID);
}

const modelinfo_t *Embree_LookupModelinfo(unsigned int geomID, unsigned int primID)
{
    const sceneinfo &info = Embree_SceneinfoForGeomID(geomID);
    return info.triToModelinfo.at(primID);
}

static void
Embree_RayEndpoint(struct RTCRayN* ray, const struct RTCHitN* potentialHit, size_t N, size_t i, vec3_t endpoint)
{
    vec3_t dir;
    dir[0] = RTCRayN_dir_x(ray, N, i);
    dir[1] = RTCRayN_dir_y(ray, N, i);
    dir[2] = RTCRayN_dir_z(ray, N, i);
    
    VectorNormalize(dir);
    
    vec3_t org;
    org[0] = RTCRayN_org_x(ray, N, i);
    org[1] = RTCRayN_org_y(ray, N, i);
    org[2] = RTCRayN_org_z(ray, N, i);
    
    // N.B.: we want the distance to the potential hit, not RTCRayN_tfar (stopping dist?)
    float tfar = RTCHitN_t(potentialHit, N, i);
    
    VectorMA(org, tfar, dir, endpoint);
}

enum class filtertype_t {
    INTERSECTION, OCCLUSION
};

void AddGlassToRay(const RTCIntersectContext* context, unsigned rayIndex, float opacity, const vec3_t glasscolor);

void AddDynamicOccluderToRay(const RTCIntersectContext* context, unsigned rayIndex, int style);

static const unsigned RAYMASK_HASMODEL_SHIFT = 0;
static const unsigned RAYMASK_HASMODEL_MASK = (1 << RAYMASK_HASMODEL_SHIFT);

static const unsigned RAYMASK_MODELINDEX_SHIFT = 1;
static const unsigned RAYMASK_MODELINDEX_MASK = (0xffff << RAYMASK_MODELINDEX_SHIFT);

static const unsigned RAYMASK_RAYINDEX_SHIFT = 17;
static const unsigned RAYMASK_RAYINDEX_MASK = (0x7fff << RAYMASK_RAYINDEX_SHIFT);

// called to evaluate transparency
template<filtertype_t filtertype>
static void
Embree_FilterFuncN(int* valid,
                   void* userDataPtr,
                   const RTCIntersectContext* context,
                   struct RTCRayN* ray,
                   const struct RTCHitN* potentialHit,
                   const size_t N)
{
    const int VALID = -1;
	const int INVALID = 0;
    
    for (size_t i=0; i<N; i++) {
        if (valid[i] != VALID) {
            // we only need to handle valid rays
            continue;
        }
        
        const unsigned &mask = RTCRayN_mask(ray, N, i);
        const unsigned &geomID = RTCHitN_geomID(potentialHit, N, i);
        const unsigned &primID = RTCHitN_primID(potentialHit, N, i);
        
        // unpack ray index
        const bool     hasmodel = static_cast<bool>((mask & RAYMASK_HASMODEL_MASK) >> RAYMASK_HASMODEL_SHIFT);
        const unsigned raySourceModelindex = (mask & RAYMASK_MODELINDEX_MASK) >> RAYMASK_MODELINDEX_SHIFT;
        const unsigned rayIndex = (mask & RAYMASK_RAYINDEX_MASK) >> RAYMASK_RAYINDEX_SHIFT;
        
        const modelinfo_t *source_modelinfo = hasmodel ? ModelInfoForModel(bsp_static, raySourceModelindex) : nullptr;
        const modelinfo_t *hit_modelinfo = Embree_LookupModelinfo(geomID, primID);
        Q_assert(hit_modelinfo != nullptr);
        
        if (hit_modelinfo->shadowworldonly.boolValue()) {
            // we hit "_shadowworldonly" "1" geometry. Ignore the hit unless we are from world.
            if (!source_modelinfo || !source_modelinfo->isWorld()) {
                // reject hit
                valid[i] = INVALID;
                continue;
            }
        }
        
        if (hit_modelinfo->shadowself.boolValue()) {
            // only casts shadows on itself
            if (source_modelinfo != hit_modelinfo) {
                // reject hit
                valid[i] = INVALID;
                continue;
            }
        }
        
        if (hit_modelinfo->switchableshadow.boolValue()) {
            // we hit a dynamic shadow caster. reject the hit, but store the
            // info about what we hit.
            
            const int style = hit_modelinfo->switchshadstyle.intValue();
            
            AddDynamicOccluderToRay(context, rayIndex, style);
            
            // reject hit
            valid[i] = INVALID;
            continue;
        }
        
        // test fence textures and glass
        const bsp2_dface_t *face = Embree_LookupFace(geomID, primID);
        
        
        float alpha = hit_modelinfo->alpha.floatValue();

        //mxd
        bool isFence, isGlass;
        if(bsp_static->loadversion == Q2_BSPVERSION) {
            const int contents = Face_Contents(bsp_static, face);
            isFence = ((contents & Q2_SURF_TRANSLUCENT) == Q2_SURF_TRANSLUCENT); // KMQuake 2-specific. Use texture alpha chanel when both flags are set.
            isGlass = !isFence && (contents & Q2_SURF_TRANSLUCENT);
            if(isGlass)
                alpha = (contents & Q2_SURF_TRANS33 ? 0.66f : 0.33f);
        } else {
            const char *name = Face_TextureName(bsp_static, face);
            isFence = (name[0] == '{');
            isGlass = (alpha < 1.0f);
        }
        
        if (isFence || isGlass) {
            vec3_t hitpoint;
            Embree_RayEndpoint(ray, potentialHit, N, i, hitpoint);
            const color_rgba sample = SampleTexture(face, bsp_static, hitpoint); //mxd. Palette index -> color_rgba
        
            if (isGlass) {
                // hit glass...
                
                //mxd. Adjust alpha by texture alpha?
                if (sample.a < 255)
                    alpha = sample.a / 255.0f;

                vec3_t rayDir = {
                    RTCRayN_dir_x(ray, N, i),
                    RTCRayN_dir_y(ray, N, i),
                    RTCRayN_dir_z(ray, N, i)
                };
                vec3_t potentialHitGeometryNormal = {
                    RTCHitN_Ng_x(potentialHit, N, i),
                    RTCHitN_Ng_y(potentialHit, N, i),
                    RTCHitN_Ng_z(potentialHit, N, i)
                };
                
                VectorNormalize(rayDir);
                VectorNormalize(potentialHitGeometryNormal);
                
                const vec_t raySurfaceCosAngle = DotProduct(rayDir, potentialHitGeometryNormal);
                
                // only pick up the color of the glass on the _exiting_ side of the glass.
                // (we currently trace "backwards", from surface point --> light source)
                if (raySurfaceCosAngle < 0) {
                    vec3_t samplecolor { (float)sample.r, (float)sample.g, (float)sample.b };
                    VectorScale(samplecolor, 1/255.0f, samplecolor);
                    
                    AddGlassToRay(context, rayIndex, alpha, samplecolor);
                }
                
                // reject hit
                valid[i] = INVALID;
                continue;
            }
            
            if (isFence) {
                if (sample.a < 255) {
                    // reject hit
                    valid[i] = INVALID;
                    continue;
                }
            }
        }
        
        // accept hit
        if (filtertype == filtertype_t::OCCLUSION) {
            RTCRayN_geomID(ray, N, i) = 0;
        } else {
            RTCRayN_Ng_x(ray, N, i) = RTCHitN_Ng_x(potentialHit, N, i);
            RTCRayN_Ng_y(ray, N, i) = RTCHitN_Ng_y(potentialHit, N, i);
            RTCRayN_Ng_z(ray, N, i) = RTCHitN_Ng_z(potentialHit, N, i);
            
            RTCRayN_instID(ray, N, i) = RTCHitN_instID(potentialHit, N, i);
            RTCRayN_geomID(ray, N, i) = RTCHitN_geomID(potentialHit, N, i);
            RTCRayN_primID(ray, N, i) = RTCHitN_primID(potentialHit, N, i);
            
            RTCRayN_u(ray, N, i) = RTCHitN_u(potentialHit, N, i);
            RTCRayN_v(ray, N, i) = RTCHitN_v(potentialHit, N, i);
            RTCRayN_tfar(ray, N, i) = RTCHitN_t(potentialHit, N, i);
        }
    }
}

// building faces for skip-textured bmodels

#if 0

static FILE *
InitObjFile(const char *filename)
{
    FILE *objfile;
    char objfilename[1024];
    strcpy(objfilename, filename);
    StripExtension(objfilename);
    DefaultExtension(objfilename, ".obj");
    
    objfile = fopen(objfilename, "wt");
    if (!objfile)
        Error("Failed to open %s: %s", objfilename, strerror(errno));
    
    return objfile;
}

static void
ExportObjFace(FILE *f, const winding_t *winding, int *vertcount)
{
//    plane_t plane;
//    WindingPlane(winding, plane.normal, &plane.dist);
    
    // export the vertices and uvs
    for (int i=0; i<winding->numpoints; i++)
    {
        fprintf(f, "v %.9g %.9g %.9g\n", winding->p[i][0], winding->p[i][1], winding->p[i][2]);
//        fprintf(f, "vn %.9g %.9g %.9g\n", plane.normal[0], plane.normal[1], plane.normal[2]);
    }
    
    fprintf(f, "f");
    for (int i=0; i<winding->numpoints; i++) {
        // .obj vertexes start from 1
        // .obj faces are CCW, quake is CW, so reverse the order
        const int vertindex = *vertcount + (winding->numpoints - 1 - i) + 1;
        fprintf(f, " %d//%d", vertindex, vertindex);
    }
    fprintf(f, "\n");
    
    *vertcount += winding->numpoints;
}

static void
ExportObj(const char *filename, const vector<winding_t *> &windings)
{
    FILE *objfile = InitObjFile(filename);
    int vertcount = 0;
    
    for (const auto &winding : windings) {
        ExportObjFace(objfile, winding, &vertcount);
    }
    
    fclose(objfile);
}

#endif

plane_t Node_Plane(const mbsp_t *bsp, const bsp2_dnode_t *node, bool side)
{
    const dplane_t *dplane = &bsp->dplanes[node->planenum];
    plane_t plane;
    
    VectorCopy(dplane->normal, plane.normal);
    plane.dist = dplane->dist;
    
    if (side) {
        VectorScale(plane.normal, -1, plane.normal);
        plane.dist *= -1.0f;
    }
    
    return plane;
}

/**
 * `planes` all of the node planes that bound this leaf, facing inward.
 */
std::vector<winding_t *>
Leaf_MakeFaces(const mbsp_t *bsp, const mleaf_t *leaf, const std::vector<plane_t> &planes)
{
    std::vector<winding_t *> result;
    
    for (const plane_t &plane : planes) {
        // flip the inward-facing split plane to get the outward-facing plane of the face we're constructing
        plane_t faceplane;
        VectorScale(plane.normal, -1, faceplane.normal);
        faceplane.dist = -plane.dist;
        
        winding_t *winding = BaseWindingForPlane(faceplane.normal, faceplane.dist);
        
        // clip `winding` by all of the other planes
        for (const plane_t &plane2 : planes) {
            if (&plane2 == &plane)
                continue;
            
            winding_t *front = nullptr;
            winding_t *back = nullptr;
            
            // frees winding.
            ClipWinding(winding, plane2.normal, plane2.dist, &front, &back);
            
            // discard the back, continue clipping the front part
            free(back);
            winding = front;
            
            // check if everything was clipped away
            if (winding == nullptr)
                break;
        }
        
        if (winding == nullptr) {
            //logprint("WARNING: winding clipped away\n");
        } else {
            result.push_back(winding);
        }
    }
    
    return result;
}

void FreeWindings(std::vector<winding_t *> &windings)
{
    for (winding_t *winding : windings) {
        free(winding);
    }
    windings.clear();
}

void
MakeFaces_r(const mbsp_t *bsp, const int nodenum, std::vector<plane_t> *planes, std::vector<winding_t *> *result)
{
    if (nodenum < 0) {
        const int leafnum = -nodenum - 1;
        const mleaf_t *leaf = &bsp->dleafs[leafnum];
        
        if (bsp->loadversion == Q2_BSPVERSION ? leaf->contents & Q2_CONTENTS_SOLID : leaf->contents == CONTENTS_SOLID) {
            std::vector<winding_t *> leaf_windings = Leaf_MakeFaces(bsp, leaf, *planes);
            for (winding_t *w : leaf_windings) {
                result->push_back(w);
            }
        }
        return;
    }
 
    const bsp2_dnode_t *node = &bsp->dnodes[nodenum];

    // go down the front side
    const plane_t front = Node_Plane(bsp, node, false);
    planes->push_back(front);
    MakeFaces_r(bsp, node->children[0], planes, result);
    planes->pop_back();
    
    // go down the back side
    const plane_t back = Node_Plane(bsp, node, true);
    planes->push_back(back);
    MakeFaces_r(bsp, node->children[1], planes, result);
    planes->pop_back();
}

std::vector<winding_t *>
MakeFaces(const mbsp_t *bsp, const dmodel_t *model)
{
    std::vector<winding_t *> result;
    std::vector<plane_t> planes;
    MakeFaces_r(bsp, model->headnode[0], &planes, &result);
    Q_assert(planes.empty());
    
    return result;
}

void
Embree_TraceInit(const mbsp_t *bsp)
{
    bsp_static = bsp;
    Q_assert(device == nullptr);
    
    std::vector<const bsp2_dface_t *> skyfaces, solidfaces, filterfaces;
    
    // check all modelinfos
    for (int mi = 0; mi<bsp->nummodels; mi++) {
        const modelinfo_t *model = ModelInfoForModel(bsp, mi);
        
        const bool isWorld = model->isWorld();
        const bool shadow = model->shadow.boolValue();
        const bool shadowself = model->shadowself.boolValue();
        const bool shadowworldonly = model->shadowworldonly.boolValue();
        const bool switchableshadow = model->switchableshadow.boolValue();

        if (!(isWorld || shadow || shadowself || shadowworldonly || switchableshadow))
            continue;
        
        for (int i=0; i<model->model->numfaces; i++) {
            const bsp2_dface_t *face = BSP_GetFace(bsp, model->model->firstface + i);
            
            // check for TEX_NOSHADOW
            const uint64_t extended_flags = extended_texinfo_flags[face->texinfo];
            if (extended_flags & TEX_NOSHADOW)
                continue;
            
            // handle switchableshadow
            if (switchableshadow) {
                filterfaces.push_back(face);
                continue;
            }
            
            const int contents = Face_Contents(bsp, face); //mxd

            //mxd. Skip NODRAW faces, but not SKY ones (Q2's sky01.wal has both flags set)
            if(bsp->loadversion == Q2_BSPVERSION && (contents & Q2_SURF_NODRAW) && !(contents & Q2_SURF_SKY))
                continue;
            
            // handle glass
            if (model->alpha.floatValue() < 1.0f 
                || (bsp->loadversion == Q2_BSPVERSION && (contents & Q2_SURF_TRANSLUCENT))) { //mxd. Both fence and transparent textures are done using SURF_TRANS flags in Q2
                filterfaces.push_back(face);
                continue;
            }
            
            // fence
            const char *texname = Face_TextureName(bsp, face);
            if (texname[0] == '{') {
                filterfaces.push_back(face);
                continue;
            }
            
            // handle sky
            if (/* !Q_strncasecmp("sky", texname, 3) */ bsp->loadversion == Q2_BSPVERSION ? contents & Q2_SURF_SKY : contents == CONTENTS_SKY) { //mxd
                skyfaces.push_back(face);
                continue;
            }
            
            // liquids
            if (/* texname[0] == '*' */ Contents_IsTranslucent(bsp, contents)) { //mxd
                if (!isWorld) {
                    // world liquids never cast shadows; shadow casting bmodel liquids do
                    solidfaces.push_back(face);
                }
                continue;
            }
            
            // solid faces
            
            if (isWorld || shadow){
                solidfaces.push_back(face);
            } else {
                // shadowself or shadowworldonly
                Q_assert(shadowself || shadowworldonly);
                filterfaces.push_back(face);
            }
        }
    }

    /* Special handling of skip-textured bmodels */
    std::vector<winding_t *> skipwindings;
    for (const modelinfo_t *model : tracelist) {
        if (model->model->numfaces == 0) {
            std::vector<winding_t *> windings = MakeFaces(bsp, model->model);
            for (auto &w : windings) {
                skipwindings.push_back(w);
            }
        }
    }
    
    device = rtcNewDevice();
    rtcDeviceSetErrorFunction2(device, ErrorCallback, nullptr); //mxd. Changed from rtcDeviceSetErrorFunction to silence compiler warning...
    
    // log version
    const size_t ver_maj = rtcDeviceGetParameter1i(device, RTC_CONFIG_VERSION_MAJOR);
    const size_t ver_min = rtcDeviceGetParameter1i(device, RTC_CONFIG_VERSION_MINOR);
    const size_t ver_pat = rtcDeviceGetParameter1i(device, RTC_CONFIG_VERSION_PATCH);
    logprint("Embree_TraceInit: Embree version: %d.%d.%d\n",
             static_cast<int>(ver_maj), static_cast<int>(ver_min), static_cast<int>(ver_pat));
    
    // we use the ray mask field to store the dmodel index of the self-shadow model
    if (0 != rtcDeviceGetParameter1i(device, RTC_CONFIG_RAY_MASK)) {
        Error("embree must be built with ray masks disabled");
    }

    scene = rtcDeviceNewScene(device, RTC_SCENE_STATIC | RTC_SCENE_COHERENT, RTC_INTERSECT1 | RTC_INTERSECT_STREAM);
    skygeom = CreateGeometry(bsp, scene, skyfaces);
    solidgeom = CreateGeometry(bsp, scene, solidfaces);
    filtergeom = CreateGeometry(bsp, scene, filterfaces);
    CreateGeometryFromWindings(scene, skipwindings);
    
    rtcSetIntersectionFilterFunctionN(scene, filtergeom.geomID, Embree_FilterFuncN<filtertype_t::INTERSECTION>);
    rtcSetOcclusionFilterFunctionN(scene, filtergeom.geomID, Embree_FilterFuncN<filtertype_t::OCCLUSION>);
    
    rtcCommit (scene);
    
    logprint("Embree_TraceInit:\n");
    logprint("\t%d sky faces\n", (int)skyfaces.size());
    logprint("\t%d solid faces\n", (int)solidfaces.size());
    logprint("\t%d filtered faces\n", (int)filterfaces.size());
    logprint("\t%d shadow-casting skip faces\n", (int)skipwindings.size());
    
    FreeWindings(skipwindings);
}

static RTCRay SetupRay(unsigned rayindex, const vec3_t start, const vec3_t dir, vec_t dist, const modelinfo_t *modelinfo)
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
    ray.mask = 0;
    
    if (modelinfo) {
        ray.mask |= RAYMASK_HASMODEL_MASK;
        
        // Hacky..
        const int modelindex = (modelinfo->model - bsp_static->dmodels);
        Q_assert(modelindex >= 0 && modelindex < bsp_static->nummodels);
        Q_assert(modelindex <= 65535);
        
        ray.mask |= (static_cast<unsigned>(modelindex) << RAYMASK_MODELINDEX_SHIFT);
    }
    
    // pack the ray index into the rest of the mask
    Q_assert(rayindex <= 32767);
    ray.mask |= (rayindex << RAYMASK_RAYINDEX_SHIFT);
    
    ray.time = 0.f;
    return ray;
}

static RTCRay SetupRay_StartStop(const vec3_t start, const vec3_t stop, const modelinfo_t *self)
{
    vec3_t dir;
    VectorSubtract(stop, start, dir);
    vec_t dist = VectorNormalize(dir);
    
    return SetupRay(0, start, dir, dist, self);
}

//public
qboolean Embree_TestLight(const vec3_t start, const vec3_t stop, const modelinfo_t *self)
{
    RTCRay ray = SetupRay_StartStop(start, stop, self);
    rtcOccluded(scene, ray);
    
    if (ray.geomID != RTC_INVALID_GEOMETRY_ID)
        return false; //hit
    
    // no obstruction
    return true;
}

//public
qboolean Embree_TestSky(const vec3_t start, const vec3_t dirn, const modelinfo_t *self)
{
    // trace from the sample point towards the sun, and
    // return true if we hit a sky poly.
    
    vec3_t dir_normalized;
    VectorCopy(dirn, dir_normalized);
    VectorNormalize(dir_normalized);
    
    RTCRay ray = SetupRay(0, start, dir_normalized, MAX_SKY_DIST, self);
    rtcIntersect(scene, ray);

    qboolean hit_sky = (ray.geomID == skygeom.geomID);
    return hit_sky;
}

//public
hittype_t Embree_DirtTrace(const vec3_t start, const vec3_t dirn, vec_t dist, const modelinfo_t *self, vec_t *hitdist_out, plane_t *hitplane_out, const bsp2_dface_t **face_out)
{
    RTCRay ray = SetupRay(0, start, dirn, dist, self);
    rtcIntersect(scene, ray);
    
    if (ray.geomID == RTC_INVALID_GEOMETRY_ID)
        return hittype_t::NONE;
    
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
    
    if (ray.geomID == skygeom.geomID) {
        return hittype_t::SKY;
    } else {
        return hittype_t::SOLID;
    }
}

//enum class streamstate_t {
//    READY, DID_OCCLUDE, DID_INTERSECT
//};

static void *q_aligned_malloc(size_t align, size_t size)
{
#ifdef _MSC_VER
    return _aligned_malloc(size, align);
#else
    void *ptr;
    if (0 != posix_memalign(&ptr, align, size)) {
        return nullptr;
    }
    return ptr;
#endif
}

static void q_aligned_free(void *ptr)
{
#ifdef _MSC_VER
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

class raystream_embree_t : public raystream_t {
public:
    RTCRay *_rays;
    float *_rays_maxdist;
    int *_point_indices;
    vec3_t *_ray_colors;
    vec3_t *_ray_normalcontribs;
    
    // This is set to the modelinfo's switchshadstyle if the ray hit
    // a dynamic shadow caster. (note that for rays that hit dynamic
    // shadow casters, all of the other hit data is assuming the ray went
    // straight through).
    int *_ray_dynamic_styles;
    
    int _numrays;
    int _maxrays;
//    streamstate_t _state;
    
public:
    raystream_embree_t(int maxRays) :
        _rays { static_cast<RTCRay *>(q_aligned_malloc(16, sizeof(RTCRay) * maxRays)) },
        _rays_maxdist { new float[maxRays] },
        _point_indices { new int[maxRays] },
        _ray_colors { static_cast<vec3_t *>(calloc(maxRays, sizeof(vec3_t))) },
        _ray_normalcontribs { static_cast<vec3_t *>(calloc(maxRays, sizeof(vec3_t))) },
        _ray_dynamic_styles { new int[maxRays] },
        _numrays { 0 },
        _maxrays { maxRays } {}
        //,
        //_state { streamstate_t::READY } {}
    
    ~raystream_embree_t() {
        q_aligned_free(_rays);
        delete[] _rays_maxdist;
        delete[] _point_indices;
        free(_ray_colors);
        free(_ray_normalcontribs);
        delete[] _ray_dynamic_styles;
    }
    
    virtual void pushRay(int i, const vec_t *origin, const vec3_t dir, float dist, const modelinfo_t *modelinfo, const vec_t *color = nullptr, const vec_t *normalcontrib = nullptr) {
        Q_assert(_numrays<_maxrays);
        _rays[_numrays] = SetupRay(_numrays, origin, dir, dist, modelinfo);
        _rays_maxdist[_numrays] = dist;
        _point_indices[_numrays] = i;
        if (color) {
            VectorCopy(color, _ray_colors[_numrays]);
        }
        if (normalcontrib) {
            VectorCopy(normalcontrib, _ray_normalcontribs[_numrays]);
        }
        _ray_dynamic_styles[_numrays] = 0;
        _numrays++;
    }
    
    virtual size_t numPushedRays() {
        return _numrays;
    }
    
    virtual void tracePushedRaysOcclusion() {
        //Q_assert(_state == streamstate_t::READY);
        
        if (!_numrays)
            return;
        
        const RTCIntersectContext ctx = {
            RTC_INTERSECT_COHERENT,
            static_cast<void *>(this)
        };
        
        rtcOccluded1M(scene, &ctx, _rays, _numrays, sizeof(RTCRay));
    }
    
    virtual void tracePushedRaysIntersection() {
        if (!_numrays)
            return;
        
        const RTCIntersectContext ctx = {
            RTC_INTERSECT_COHERENT,
            static_cast<void *>(this)
        };
        
        rtcIntersect1M(scene, &ctx, _rays, _numrays, sizeof(RTCRay));
    }
    
    virtual bool getPushedRayOccluded(size_t j) {
        Q_assert(j < _maxrays);
        return (_rays[j].geomID != RTC_INVALID_GEOMETRY_ID);
    }
    
    virtual float getPushedRayDist(size_t j) {
        Q_assert(j < _maxrays);
        return _rays_maxdist[j];
    }
    
    virtual float getPushedRayHitDist(size_t j) {
        Q_assert(j < _maxrays);
        return _rays[j].tfar;
    }
    
    virtual hittype_t getPushedRayHitType(size_t j) {
        Q_assert(j < _maxrays);

        if (_rays[j].geomID == RTC_INVALID_GEOMETRY_ID) {
            return hittype_t::NONE;
        } else if (_rays[j].geomID == skygeom.geomID) {
            return hittype_t::SKY;
        } else {
            return hittype_t::SOLID;
        }
    }
    
    virtual const bsp2_dface_t *getPushedRayHitFace(size_t j) {
        Q_assert(j < _maxrays);
        
        const RTCRay &ray = _rays[j];
        
        if (ray.geomID == RTC_INVALID_GEOMETRY_ID)
            return nullptr;
        
        const sceneinfo &si = Embree_SceneinfoForGeomID(ray.geomID);
        const bsp2_dface_t *face = si.triToFace.at(ray.primID);
        Q_assert(face != nullptr);
        
        return face;
    }
    
    virtual void getPushedRayDir(size_t j, vec3_t out) {
        Q_assert(j < _maxrays);
        for (int i=0; i<3; i++) {
            out[i] = _rays[j].dir[i];
        }
    }
    
    virtual int getPushedRayPointIndex(size_t j) {
       // Q_assert(_state != streamstate_t::READY);
        Q_assert(j < _maxrays);
        return _point_indices[j];
    }
    
    virtual void getPushedRayColor(size_t j, vec3_t out) {
        Q_assert(j < _maxrays);
        VectorCopy(_ray_colors[j], out);
    }
    
    virtual void getPushedRayNormalContrib(size_t j, vec3_t out) {
        Q_assert(j < _maxrays);
        VectorCopy(_ray_normalcontribs[j], out);
    }
    
    virtual int getPushedRayDynamicStyle(size_t j) {
        Q_assert(j < _maxrays);
        return _ray_dynamic_styles[j];
    }
    
    virtual void clearPushedRays() {
        _numrays = 0;
        //_state = streamstate_t::READY;
    }
};

raystream_t *Embree_MakeRayStream(int maxrays)
{
    return new raystream_embree_t{maxrays};
}

void AddGlassToRay(const RTCIntersectContext* context, unsigned rayIndex, float opacity, const vec3_t glasscolor) {
    if (context == nullptr) {
        // FIXME: remove this..
        // happens for bounce lights, e.g. Embree_TestSky
        return;
    }
    
    raystream_embree_t *rs = static_cast<raystream_embree_t *>(context->userRayExt);
    
    // clamp opacity
    opacity = qmin(qmax(0.0f, opacity), 1.0f);
    
    Q_assert(rayIndex < rs->_numrays);
    
    Q_assert(glasscolor[0] >= 0.0 && glasscolor[0] <= 1.0);
    Q_assert(glasscolor[1] >= 0.0 && glasscolor[1] <= 1.0);
    Q_assert(glasscolor[2] >= 0.0 && glasscolor[2] <= 1.0);
    
    //multiply ray color by glass color
    vec3_t tinted;
    for (int i=0; i<3; i++) {
        tinted[i] = rs->_ray_colors[rayIndex][i] * glasscolor[i];
    }
    
    // lerp between original ray color and fully tinted, based on opacity
    vec3_t lerped = {0.0, 0.0, 0.0};
    VectorMA(lerped, opacity, tinted, lerped);
    VectorMA(lerped, 1.0-opacity, rs->_ray_colors[rayIndex], lerped);
    
    // use the lerped color, scaled by (1-opacity) as the new ray color
  //  VectorScale(lerped, (1.0f - opacity), rs->_ray_colors[rayIndex]);
    
    // use the lerped color
    VectorCopy(lerped, rs->_ray_colors[rayIndex]);
}

void AddDynamicOccluderToRay(const RTCIntersectContext* context, unsigned rayIndex, int style)
{
    raystream_embree_t *rs = static_cast<raystream_embree_t *>(context->userRayExt);
    rs->_ray_dynamic_styles[rayIndex] = style;
}
