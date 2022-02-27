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
#include <embree3/rtcore.h>
#include <embree3/rtcore_ray.h>
#include <vector>
#include <cassert>
#include <climits>
#include <cstdlib>
#include <limits>

using namespace std;
using namespace polylib;

class sceneinfo
{
public:
    unsigned geomID;

    std::vector<const mface_t *> triToFace;
    std::vector<const modelinfo_t *> triToModelinfo;
};

class raystream_embree_common_t;

struct ray_source_info : public RTCIntersectContext
{
    raystream_embree_common_t *raystream; // may be null if this ray is not from a ray stream
    const modelinfo_t *self;
    /// only used if raystream == null
    int singleRayShadowStyle;

    ray_source_info(raystream_embree_common_t *raystream_, const modelinfo_t *self_)
        : raystream(raystream_), self(self_), singleRayShadowStyle(0)
    {
        rtcInitIntersectContext(this);

        flags = RTC_INTERSECT_CONTEXT_FLAG_COHERENT;
    }
};

/**
 * Returns 1.0 unless a custom alpha value is set.
 * The priority is: "_light_alpha" (read from extended_texinfo_flags), then "alpha"
 */
static float Face_Alpha(const modelinfo_t *modelinfo, const mface_t *face)
{
    const surfflags_t &extended_flags = extended_texinfo_flags[face->texinfo];

    // for _light_alpha, 0 is considered unset
    if (extended_flags.light_alpha) {
        return extended_flags.light_alpha;
    }

    // next check modelinfo alpha (defaults to 1.0)
    return modelinfo->alpha.value();
}

sceneinfo CreateGeometry(
    const mbsp_t *bsp, RTCDevice g_device, RTCScene scene, const std::vector<const mface_t *> &faces)
{
    // count triangles
    int numtris = 0;
    for (const mface_t *face : faces) {
        if (face->numedges < 3)
            continue;
        numtris += (face->numedges - 2);
    }

    unsigned int geomID;
    RTCGeometry geom_0 = rtcNewGeometry(g_device, RTC_GEOMETRY_TYPE_TRIANGLE);
    // we're not using masks, but they need to be set to something or else all rays miss
    // if embree is compiled with them
    rtcSetGeometryMask(geom_0, 1);
    rtcSetGeometryBuildQuality(geom_0, RTC_BUILD_QUALITY_MEDIUM);
    rtcSetGeometryTimeStepCount(geom_0, 1);
    geomID = rtcAttachGeometry(scene, geom_0);
    rtcReleaseGeometry(geom_0);

    struct Vertex
    {
        float point[4];
    }; // 4th element is padding
    struct Triangle
    {
        int v0, v1, v2;
    };

    // fill in vertices
    Vertex *vertices = (Vertex *)rtcSetNewGeometryBuffer(
        geom_0, RTC_BUFFER_TYPE_VERTEX, 0, RTC_FORMAT_FLOAT3, 4 * sizeof(float), bsp->dvertexes.size());

    size_t i = 0;
    for (auto &dvertex : bsp->dvertexes) {
        Vertex *vert = &vertices[i++];
        for (int j = 0; j < 3; j++) {
            vert->point[j] = dvertex[j];
        }
    }

    sceneinfo s;
    s.geomID = geomID;

    // fill in triangles
    Triangle *triangles = (Triangle *)rtcSetNewGeometryBuffer(
        geom_0, RTC_BUFFER_TYPE_INDEX, 0, RTC_FORMAT_UINT3, 3 * sizeof(int), numtris);
    int tri_index = 0;
    for (const mface_t *face : faces) {
        if (face->numedges < 3)
            continue;

        // NOTE: can be null for "skip" faces
        const modelinfo_t *modelinfo = ModelInfoForFace(bsp, Face_GetNum(bsp, face));

        for (int j = 2; j < face->numedges; j++) {
            Triangle *tri = &triangles[tri_index];
            tri->v0 = Face_VertexAtIndex(bsp, face, j - 1);
            tri->v1 = Face_VertexAtIndex(bsp, face, j);
            tri->v2 = Face_VertexAtIndex(bsp, face, 0);
            tri_index++;

            s.triToFace.push_back(face);
            s.triToModelinfo.push_back(modelinfo);
        }
    }

    rtcCommitGeometry(geom_0);
    return s;
}

static void CreateGeometryFromWindings(RTCDevice g_device, RTCScene scene, const std::vector<winding_t> &windings)
{
    if (windings.empty())
        return;

    // count triangles
    int numtris = 0;
    int numverts = 0;
    for (const auto &winding : windings) {
        Q_assert(winding.size() >= 3);
        numtris += (winding.size() - 2);
        numverts += winding.size();
    }

    unsigned int geomID;
    RTCGeometry geom_1 = rtcNewGeometry(g_device, RTC_GEOMETRY_TYPE_TRIANGLE);
    rtcSetGeometryBuildQuality(geom_1, RTC_BUILD_QUALITY_MEDIUM);
    rtcSetGeometryMask(geom_1, 1);
    rtcSetGeometryTimeStepCount(geom_1, 1);
    geomID = rtcAttachGeometry(scene, geom_1);
    rtcReleaseGeometry(geom_1);

    struct Vertex
    {
        float point[4];
    }; // 4th element is padding
    struct Triangle
    {
        int v0, v1, v2;
    };

    // fill in vertices
    Vertex *vertices = (Vertex *)rtcSetNewGeometryBuffer(
        geom_1, RTC_BUFFER_TYPE_VERTEX, 0, RTC_FORMAT_FLOAT3, 4 * sizeof(float), numverts);
    {
        int vert_index = 0;
        for (const auto &winding : windings) {
            for (int j = 0; j < winding.size(); j++) {
                for (int k = 0; k < 3; k++) {
                    vertices[vert_index + j].point[k] = winding.at(j)[k];
                }
            }
            vert_index += winding.size();
        }
    }

    // fill in triangles
    Triangle *triangles = (Triangle *)rtcSetNewGeometryBuffer(
        geom_1, RTC_BUFFER_TYPE_INDEX, 0, RTC_FORMAT_UINT3, 3 * sizeof(int), numtris);
    int tri_index = 0;
    int vert_index = 0;
    for (const auto &winding : windings) {
        for (int j = 2; j < winding.size(); j++) {
            Triangle *tri = &triangles[tri_index];
            tri->v0 = vert_index + (j - 1);
            tri->v1 = vert_index + j;
            tri->v2 = vert_index + 0;
            tri_index++;
        }
        vert_index += winding.size();
    }
    Q_assert(vert_index == numverts);
    Q_assert(tri_index == numtris);

    rtcCommitGeometry(geom_1);
}

RTCDevice device;
RTCScene scene;

sceneinfo skygeom; // sky. always occludes.
sceneinfo solidgeom; // solids. always occludes.
sceneinfo filtergeom; // conditional occluders.. needs to run ray intersection filter

static const mbsp_t *bsp_static;

void ErrorCallback(void *userptr, const RTCError code, const char *str)
{
    fmt::print("RTC Error {}: {}\n", code, str);
}

static const sceneinfo &Embree_SceneinfoForGeomID(unsigned int geomID)
{
    if (geomID == skygeom.geomID) {
        return skygeom;
    } else if (geomID == solidgeom.geomID) {
        return solidgeom;
    } else if (geomID == filtergeom.geomID) {
        return filtergeom;
    } else {
        FError("unexpected geomID");
    }
}

const mface_t *Embree_LookupFace(unsigned int geomID, unsigned int primID)
{
    const sceneinfo &info = Embree_SceneinfoForGeomID(geomID);
    return info.triToFace.at(primID);
}

const modelinfo_t *Embree_LookupModelinfo(unsigned int geomID, unsigned int primID)
{
    const sceneinfo &info = Embree_SceneinfoForGeomID(geomID);
    return info.triToModelinfo.at(primID);
}

static qvec3d Embree_RayEndpoint(RTCRayN *ray, size_t N, size_t i)
{
    qvec3d dir = qv::normalize(qvec3d{RTCRayN_dir_x(ray, N, i), RTCRayN_dir_y(ray, N, i), RTCRayN_dir_z(ray, N, i)});
    qvec3d org{RTCRayN_org_x(ray, N, i), RTCRayN_org_y(ray, N, i), RTCRayN_org_z(ray, N, i)};
    float &tfar = RTCRayN_tfar(ray, N, i);

    return org + (dir * tfar);
}

enum class filtertype_t
{
    INTERSECTION,
    OCCLUSION
};

static void AddGlassToRay(RTCIntersectContext *context, unsigned rayIndex, float opacity, const qvec3d &glasscolor);
static void AddDynamicOccluderToRay(RTCIntersectContext *context, unsigned rayIndex, int style);

// called to evaluate transparency
template<filtertype_t filtertype>
static void Embree_FilterFuncN(const struct RTCFilterFunctionNArguments *args)
{
    int *const valid = args->valid;
    RTCIntersectContext *const context = args->context;
    struct RTCRayN *const ray = args->ray;
    struct RTCHitN *const potentialHit = args->hit;
    const unsigned int N = args->N;

    const int VALID = -1;
    const int INVALID = 0;

    const ray_source_info *rsi = static_cast<const ray_source_info *>(context);

    for (size_t i = 0; i < N; i++) {
        if (valid[i] != VALID) {
            // we only need to handle valid rays
            continue;
        }

        const unsigned &rayID = RTCRayN_id(ray, N, i);
        const unsigned &geomID = RTCHitN_geomID(potentialHit, N, i);
        const unsigned &primID = RTCHitN_primID(potentialHit, N, i);

        // unpack ray index
        const unsigned rayIndex = rayID;

        const modelinfo_t *source_modelinfo = rsi->self;
        const modelinfo_t *hit_modelinfo = Embree_LookupModelinfo(geomID, primID);
        if (!hit_modelinfo) {
            // we hit a "skip" face with no associated model
            // reject hit (???)
            valid[i] = INVALID;
            continue;
        }

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

            const int style = hit_modelinfo->switchshadstyle.value();

            AddDynamicOccluderToRay(context, rayIndex, style);

            // reject hit
            valid[i] = INVALID;
            continue;
        }

        // test fence textures and glass
        const mface_t *face = Embree_LookupFace(geomID, primID);
        float alpha = Face_Alpha(hit_modelinfo, face);

        // mxd
        bool isFence, isGlass;
        if (bsp_static->loadversion->game->id == GAME_QUAKE_II) {
            const int surf_flags = Face_ContentsOrSurfaceFlags(bsp_static, face);
            isFence = ((surf_flags & Q2_SURF_TRANSLUCENT) ==
                       Q2_SURF_TRANSLUCENT); // KMQuake 2-specific. Use texture alpha chanel when both flags are set.
            isGlass = !isFence && (surf_flags & Q2_SURF_TRANSLUCENT);
            if (isGlass)
                alpha = (surf_flags & Q2_SURF_TRANS33 ? 0.33f : 0.66f);
        } else {
            const char *name = Face_TextureName(bsp_static, face);
            isFence = (name[0] == '{');
            isGlass = (alpha < 1.0f);
        }

        if (isFence || isGlass) {
            qvec3d hitpoint = Embree_RayEndpoint(ray, N, i);
            const qvec4b sample = SampleTexture(face, bsp_static, hitpoint); // mxd. Palette index -> color_rgba

            if (isGlass) {
                // hit glass...

                // mxd. Adjust alpha by texture alpha?
                if (sample[3] < 255)
                    alpha = sample[3] / 255.0f;

                qvec3d rayDir =
                    qv::normalize(qvec3d{RTCRayN_dir_x(ray, N, i), RTCRayN_dir_y(ray, N, i), RTCRayN_dir_z(ray, N, i)});
                qvec3d potentialHitGeometryNormal = qv::normalize(qvec3d{RTCHitN_Ng_x(potentialHit, N, i),
                    RTCHitN_Ng_y(potentialHit, N, i), RTCHitN_Ng_z(potentialHit, N, i)});

                const vec_t raySurfaceCosAngle = qv::dot(rayDir, potentialHitGeometryNormal);

                // only pick up the color of the glass on the _exiting_ side of the glass.
                // (we currently trace "backwards", from surface point --> light source)
                if (raySurfaceCosAngle < 0) {
                    AddGlassToRay(context, rayIndex, alpha, sample.xyz() * (1.0 / 255.0));
                }

                // reject hit
                valid[i] = INVALID;
                continue;
            }

            if (isFence) {
                if (sample[3] < 255) {
                    // reject hit
                    valid[i] = INVALID;
                    continue;
                }
            }
        }

        // accept hit
        // (just need to leave the `valid` value set to VALID)
    }
}

// building faces for skip-textured bmodels

qplane3d Node_Plane(const mbsp_t *bsp, const bsp2_dnode_t *node, bool side)
{
    qplane3d plane = bsp->dplanes[node->planenum];

    if (side) {
        return -plane;
    }

    return plane;
}

/**
 * `planes` all of the node planes that bound this leaf, facing inward.
 */
static void Leaf_MakeFaces(
    const mbsp_t *bsp, const mleaf_t *leaf, const std::vector<qplane3d> &planes, std::vector<winding_t> &result)
{
    for (const qplane3d &plane : planes) {
        // flip the inward-facing split plane to get the outward-facing plane of the face we're constructing
        qplane3d faceplane = -plane;

        std::optional<winding_t> winding = winding_t::from_plane(faceplane, 10e6);

        // clip `winding` by all of the other planes
        for (const qplane3d &plane2 : planes) {
            if (&plane2 == &plane)
                continue;

            auto clipped = winding->clip(plane2);

            // discard the back, continue clipping the front part
            winding = clipped[0];

            // check if everything was clipped away
            if (!winding)
                break;
        }

        if (winding) {
            // LogPrint("WARNING: winding clipped away\n");
        } else {
            result.push_back(std::move(*winding));
        }
    }
}

void MakeFaces_r(const mbsp_t *bsp, const int nodenum, std::vector<qplane3d> *planes, std::vector<winding_t> &result)
{
    if (nodenum < 0) {
        const int leafnum = -nodenum - 1;
        const mleaf_t *leaf = &bsp->dleafs[leafnum];

        if ((bsp->loadversion->game->id == GAME_QUAKE_II) ? (leaf->contents & Q2_CONTENTS_SOLID)
                                                          : leaf->contents == CONTENTS_SOLID) {
            Leaf_MakeFaces(bsp, leaf, *planes, result);
        }
        return;
    }

    const bsp2_dnode_t *node = &bsp->dnodes[nodenum];

    // go down the front side
    planes->push_back(Node_Plane(bsp, node, false));
    MakeFaces_r(bsp, node->children[0], planes, result);
    planes->pop_back();

    // go down the back side
    planes->push_back(Node_Plane(bsp, node, true));
    MakeFaces_r(bsp, node->children[1], planes, result);
    planes->pop_back();
}

static void MakeFaces(const mbsp_t *bsp, const dmodelh2_t *model, std::vector<winding_t> &result)
{
    std::vector<qplane3d> planes;
    MakeFaces_r(bsp, model->headnode[0], &planes, result);
    Q_assert(planes.empty());
}

void Embree_TraceInit(const mbsp_t *bsp)
{
    bsp_static = bsp;
    Q_assert(device == nullptr);

    std::vector<const mface_t *> skyfaces, solidfaces, filterfaces;

    // check all modelinfos
    for (size_t mi = 0; mi < bsp->dmodels.size(); mi++) {
        const modelinfo_t *model = ModelInfoForModel(bsp, mi);

        const bool isWorld = model->isWorld();
        const bool shadow = model->shadow.boolValue();
        const bool shadowself = model->shadowself.boolValue();
        const bool shadowworldonly = model->shadowworldonly.boolValue();
        const bool switchableshadow = model->switchableshadow.boolValue();

        if (!(isWorld || shadow || shadowself || shadowworldonly || switchableshadow))
            continue;

        for (int i = 0; i < model->model->numfaces; i++) {
            const mface_t *face = BSP_GetFace(bsp, model->model->firstface + i);

            // check for TEX_NOSHADOW
            const surfflags_t &extended_flags = extended_texinfo_flags[face->texinfo];
            if (extended_flags.no_shadow)
                continue;

            // handle switchableshadow
            if (switchableshadow) {
                filterfaces.push_back(face);
                continue;
            }

            const int contents_or_surf_flags = Face_ContentsOrSurfaceFlags(bsp, face); // mxd
            const gtexinfo_t *texinfo = Face_Texinfo(bsp, face);
            const bool is_q2 = bsp->loadversion->game->id == GAME_QUAKE_II;

            // mxd. Skip NODRAW faces, but not SKY ones (Q2's sky01.wal has both flags set)
            if (is_q2 && (contents_or_surf_flags & Q2_SURF_NODRAW) && !(contents_or_surf_flags & Q2_SURF_SKY))
                continue;

            // handle glass / water
            const float alpha = Face_Alpha(model, face);
            if (alpha < 1.0f ||
                (is_q2 && (contents_or_surf_flags & Q2_SURF_TRANSLUCENT))) { // mxd. Both fence and transparent textures
                                                                             // are done using SURF_TRANS flags in Q2
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
            if (is_q2) {
                // Q2: arghrad compat: sky faces only emit sunlight if:
                // sky flag set, light flag set, value nonzero
                if ((contents_or_surf_flags & Q2_SURF_SKY) != 0 &&
                    (!options.arghradcompat.value() ||
                        ((contents_or_surf_flags & Q2_SURF_LIGHT) != 0 && texinfo->value != 0))) {
                    skyfaces.push_back(face);
                    continue;
                }
            } else {
                // Q1
                if (!Q_strncasecmp("sky", texname, 3)) {
                    skyfaces.push_back(face);
                    continue;
                }
            }

            // liquids
            if (/* texname[0] == '*' */ ContentsOrSurfaceFlags_IsTranslucent(bsp, contents_or_surf_flags)) { // mxd
                if (!isWorld) {
                    // world liquids never cast shadows; shadow casting bmodel liquids do
                    solidfaces.push_back(face);
                }
                continue;
            }

            // solid faces

            if (isWorld || shadow) {
                solidfaces.push_back(face);
            } else {
                // shadowself or shadowworldonly
                Q_assert(shadowself || shadowworldonly);
                filterfaces.push_back(face);
            }
        }
    }

    /* Special handling of skip-textured bmodels */
    std::vector<winding_t> skipwindings;
    for (const modelinfo_t *model : tracelist) {
        if (model->model->numfaces == 0) {
            MakeFaces(bsp, model->model, skipwindings);
        }
    }

    device = rtcNewDevice(NULL);
    rtcSetDeviceErrorFunction(
        device, ErrorCallback, nullptr); // mxd. Changed from rtcDeviceSetErrorFunction to silence compiler warning...

    // log version
    const size_t ver_maj = rtcGetDeviceProperty(device, RTC_DEVICE_PROPERTY_VERSION_MAJOR);
    const size_t ver_min = rtcGetDeviceProperty(device, RTC_DEVICE_PROPERTY_VERSION_MINOR);
    const size_t ver_pat = rtcGetDeviceProperty(device, RTC_DEVICE_PROPERTY_VERSION_PATCH);
    FLogPrint("Embree version: {}.{}.{}\n", ver_maj, ver_min, ver_pat);

    scene = rtcNewScene(device);
    rtcSetSceneFlags(scene, RTC_SCENE_FLAG_NONE);
    rtcSetSceneBuildQuality(scene, RTC_BUILD_QUALITY_HIGH);
    skygeom = CreateGeometry(bsp, device, scene, skyfaces);
    solidgeom = CreateGeometry(bsp, device, scene, solidfaces);
    filtergeom = CreateGeometry(bsp, device, scene, filterfaces);
    CreateGeometryFromWindings(device, scene, skipwindings);

    rtcSetGeometryIntersectFilterFunction(
        rtcGetGeometry(scene, filtergeom.geomID), Embree_FilterFuncN<filtertype_t::INTERSECTION>);
    rtcSetGeometryOccludedFilterFunction(
        rtcGetGeometry(scene, filtergeom.geomID), Embree_FilterFuncN<filtertype_t::OCCLUSION>);

    rtcCommitScene(scene);

    FLogPrint("\n");
    LogPrint("\t{} sky faces\n", skyfaces.size());
    LogPrint("\t{} solid faces\n", solidfaces.size());
    LogPrint("\t{} filtered faces\n", filterfaces.size());
    LogPrint("\t{} shadow-casting skip faces\n", skipwindings.size());
}

static RTCRayHit SetupRay(unsigned rayindex, const qvec3d &start, const qvec3d &dir, vec_t dist)
{
    RTCRayHit ray;
    ray.ray.org_x = start[0];
    ray.ray.org_y = start[1];
    ray.ray.org_z = start[2];
    ray.ray.tnear = 0.f;

    ray.ray.dir_x = dir[0]; // can be un-normalized
    ray.ray.dir_y = dir[1];
    ray.ray.dir_z = dir[2];
    ray.ray.time = 0.f; // not using

    ray.ray.tfar = dist;
    ray.ray.mask = 1; // we're not using, but needs to be set if embree is compiled with masks
    ray.ray.id = rayindex;
    ray.ray.flags = 0; // reserved

    ray.hit.geomID = RTC_INVALID_GEOMETRY_ID;
    ray.hit.primID = RTC_INVALID_GEOMETRY_ID;
    ray.hit.instID[0] = RTC_INVALID_GEOMETRY_ID;
    return ray;
}

static RTCRayHit SetupRay_StartStop(const qvec3d &start, const qvec3d &stop)
{
    qvec3d dir = stop - start;
    vec_t dist = qv::normalizeInPlace(dir);

    return SetupRay(0, start, dir, dist);
}

// public
hitresult_t Embree_TestLight(const qvec3d &start, const qvec3d &stop, const modelinfo_t *self)
{
    RTCRay ray = SetupRay_StartStop(start, stop).ray;

    ray_source_info ctx2(nullptr, self);
    rtcOccluded1(scene, &ctx2, &ray);

    if (ray.tfar < 0.0f)
        return {false, 0}; // fully occluded

    // no obstruction (or a switchable shadow obstruction only)
    return {true, ctx2.singleRayShadowStyle};
}

// public
hitresult_t Embree_TestSky(const qvec3d &start, const qvec3d &dirn, const modelinfo_t *self, const mface_t **face_out)
{
    // trace from the sample point towards the sun, and
    // return true if we hit a sky poly.

    qvec3d dir_normalized = qv::normalize(dirn);

    RTCRayHit ray = SetupRay(0, start, dir_normalized, MAX_SKY_DIST);

    ray_source_info ctx2(nullptr, self);
    rtcIntersect1(scene, &ctx2, &ray);

    bool hit_sky = (ray.hit.geomID == skygeom.geomID);

    if (face_out) {
        if (hit_sky) {
            const sceneinfo &si = Embree_SceneinfoForGeomID(ray.hit.geomID);
            *face_out = si.triToFace.at(ray.hit.primID);
        } else {
            *face_out = nullptr;
        }
    }

    return {hit_sky, ctx2.singleRayShadowStyle};
}

// public
hittype_t Embree_DirtTrace(const qvec3d &start, const qvec3d &dirn, vec_t dist, const modelinfo_t *self,
    vec_t *hitdist_out, qplane3d *hitplane_out, const mface_t **face_out)
{
    RTCRayHit ray = SetupRay(0, start, dirn, dist);
    ray_source_info ctx2(nullptr, self);
    rtcIntersect1(scene, &ctx2, &ray);
    ray.hit.Ng_x = -ray.hit.Ng_x;
    ray.hit.Ng_y = -ray.hit.Ng_y;
    ray.hit.Ng_z = -ray.hit.Ng_z;

    if (ray.hit.geomID == RTC_INVALID_GEOMETRY_ID)
        return hittype_t::NONE;

    if (hitdist_out) {
        *hitdist_out = ray.ray.tfar;
    }
    if (hitplane_out) {
        hitplane_out->normal = qv::normalize(qvec3d{ray.hit.Ng_x, ray.hit.Ng_y, ray.hit.Ng_z});

        qvec3d hitpoint = start + (dirn * ray.ray.tfar);

        hitplane_out->dist = qv::dot(hitplane_out->normal, hitpoint);
    }
    if (face_out) {
        const sceneinfo &si = Embree_SceneinfoForGeomID(ray.hit.geomID);
        *face_out = si.triToFace.at(ray.hit.primID);
    }

    if (ray.hit.geomID == skygeom.geomID) {
        return hittype_t::SKY;
    } else {
        return hittype_t::SOLID;
    }
}

// enum class streamstate_t {
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

class raystream_embree_common_t : public virtual raystream_common_t
{
public:
    float *_rays_maxdist;
    int *_point_indices;
    qvec3d *_ray_colors;
    qvec3d *_ray_normalcontribs;

    // This is set to the modelinfo's switchshadstyle if the ray hit
    // a dynamic shadow caster. (note that for rays that hit dynamic
    // shadow casters, all of the other hit data is assuming the ray went
    // straight through).
    int *_ray_dynamic_styles;

    int _numrays;
    int _maxrays;
    //    streamstate_t _state;

public:
    raystream_embree_common_t(int maxRays)
        : _rays_maxdist{new float[maxRays]}, _point_indices{new int[maxRays]}, _ray_colors{new qvec3d[maxRays]{}},
          _ray_normalcontribs{new qvec3d[maxRays]{}}, _ray_dynamic_styles{new int[maxRays]}, _numrays{0}, _maxrays{
                                                                                                              maxRays}
    {
    }
    //,
    //_state { streamstate_t::READY } {}

    ~raystream_embree_common_t()
    {
        delete[] _rays_maxdist;
        delete[] _point_indices;
        delete[] _ray_colors;
        delete[] _ray_normalcontribs;
        delete[] _ray_dynamic_styles;
    }

    size_t numPushedRays() override { return _numrays; }

    int getPushedRayPointIndex(size_t j) override
    {
        // Q_assert(_state != streamstate_t::READY);
        Q_assert(j < _maxrays);
        return _point_indices[j];
    }

    qvec3d &getPushedRayColor(size_t j) override
    {
        Q_assert(j < _maxrays);
        return _ray_colors[j];
    }

    qvec3d &getPushedRayNormalContrib(size_t j) override
    {
        Q_assert(j < _maxrays);
        return _ray_normalcontribs[j];
    }

    int getPushedRayDynamicStyle(size_t j) override
    {
        Q_assert(j < _maxrays);
        return _ray_dynamic_styles[j];
    }

    void clearPushedRays() override
    {
        _numrays = 0;
        //_state = streamstate_t::READY;
    }
};

class raystream_embree_intersection_t : public raystream_embree_common_t, public raystream_intersection_t
{
public:
    RTCRayHit *_rays;

public:
    raystream_embree_intersection_t(int maxRays)
        : raystream_embree_common_t(maxRays), _rays{static_cast<RTCRayHit *>(
                                                  q_aligned_malloc(16, sizeof(RTCRayHit) * maxRays))}
    {
    }

    ~raystream_embree_intersection_t() { q_aligned_free(_rays); }

    void pushRay(int i, const qvec3d &origin, const qvec3d &dir, float dist, const qvec3d *color = nullptr,
        const qvec3d *normalcontrib = nullptr) override
    {
        Q_assert(_numrays < _maxrays);
        const RTCRayHit rayHit = SetupRay(_numrays, origin, dir, dist);
        _rays[_numrays] = rayHit;
        _rays_maxdist[_numrays] = dist;
        _point_indices[_numrays] = i;
        if (color) {
            _ray_colors[_numrays] = *color;
        }
        if (normalcontrib) {
            _ray_normalcontribs[_numrays] = *normalcontrib;
        }
        _ray_dynamic_styles[_numrays] = 0;
        _numrays++;
    }

    void tracePushedRaysIntersection(const modelinfo_t *self) override
    {
        if (!_numrays)
            return;

        ray_source_info ctx2(this, self);
        rtcIntersect1M(scene, &ctx2, _rays, _numrays, sizeof(_rays[0]));
    }

    qvec3d getPushedRayDir(size_t j) override
    {
        Q_assert(j < _maxrays);
        return {_rays[j].ray.dir_x, _rays[j].ray.dir_y, _rays[j].ray.dir_z};
    }

    float getPushedRayHitDist(size_t j) override
    {
        Q_assert(j < _maxrays);
        return _rays[j].ray.tfar;
    }

    hittype_t getPushedRayHitType(size_t j) override
    {
        Q_assert(j < _maxrays);

        const unsigned id = _rays[j].hit.geomID;
        if (id == RTC_INVALID_GEOMETRY_ID) {
            return hittype_t::NONE;
        } else if (id == skygeom.geomID) {
            return hittype_t::SKY;
        } else {
            return hittype_t::SOLID;
        }
    }

    const mface_t *getPushedRayHitFace(size_t j) override
    {
        Q_assert(j < _maxrays);

        const RTCRayHit &ray = _rays[j];

        if (ray.hit.geomID == RTC_INVALID_GEOMETRY_ID)
            return nullptr;

        const sceneinfo &si = Embree_SceneinfoForGeomID(ray.hit.geomID);
        const mface_t *face = si.triToFace.at(ray.hit.primID);
        Q_assert(face != nullptr);

        return face;
    }
};

class raystream_embree_occlusion_t : public raystream_embree_common_t, public raystream_occlusion_t
{
public:
    RTCRay *_rays;

public:
    raystream_embree_occlusion_t(int maxRays)
        : raystream_embree_common_t(maxRays), _rays{
                                                  static_cast<RTCRay *>(q_aligned_malloc(16, sizeof(RTCRay) * maxRays))}
    {
    }

    ~raystream_embree_occlusion_t() { q_aligned_free(_rays); }

    void pushRay(int i, const qvec3d &origin, const qvec3d &dir, float dist, const qvec3d *color = nullptr,
        const qvec3d *normalcontrib = nullptr) override
    {
        Q_assert(_numrays < _maxrays);
        _rays[_numrays] = SetupRay(_numrays, origin, dir, dist).ray;
        _rays_maxdist[_numrays] = dist;
        _point_indices[_numrays] = i;
        if (color) {
            _ray_colors[_numrays] = *color;
        }
        if (normalcontrib) {
            _ray_normalcontribs[_numrays] = *normalcontrib;
        }
        _ray_dynamic_styles[_numrays] = 0;
        _numrays++;
    }

    void tracePushedRaysOcclusion(const modelinfo_t *self) override
    {
        // Q_assert(_state == streamstate_t::READY);

        if (!_numrays)
            return;

        ray_source_info ctx2(this, self);
        rtcOccluded1M(scene, &ctx2, _rays, _numrays, sizeof(_rays[0]));
    }

    bool getPushedRayOccluded(size_t j) override
    {
        Q_assert(j < _maxrays);
        return (_rays[j].tfar < 0.0f);
    }

    qvec3d getPushedRayDir(size_t j) override
    {
        Q_assert(j < _maxrays);

        return {_rays[j].dir_x, _rays[j].dir_y, _rays[j].dir_z};
    }
};

raystream_occlusion_t *Embree_MakeOcclusionRayStream(int maxrays)
{
    return new raystream_embree_occlusion_t{maxrays};
}

raystream_intersection_t *Embree_MakeIntersectionRayStream(int maxrays)
{
    return new raystream_embree_intersection_t{maxrays};
}

static void AddGlassToRay(RTCIntersectContext *context, unsigned rayIndex, float opacity, const qvec3d &glasscolor)
{
    ray_source_info *ctx = static_cast<ray_source_info *>(context);
    raystream_embree_common_t *rs = ctx->raystream;

    if (rs == nullptr) {
        // FIXME: remove this.. once all ray casts use raystreams
        // happens for bounce lights, e.g. Embree_TestSky
        return;
    }

    // clamp opacity
    opacity = clamp(opacity, 0.0f, 1.0f);

    Q_assert(rayIndex < rs->_numrays);

    // multiply ray color by glass color
    qvec3d tinted = rs->_ray_colors[rayIndex] * glasscolor;

    // use the lerped color between original ray color and fully tinted, based on opacity
    rs->_ray_colors[rayIndex] = mix(tinted, rs->_ray_colors[rayIndex], opacity);
}

static void AddDynamicOccluderToRay(RTCIntersectContext *context, unsigned rayIndex, int style)
{
    ray_source_info *ctx = static_cast<ray_source_info *>(context);
    raystream_embree_common_t *rs = ctx->raystream;

    if (rs != nullptr) {
        rs->_ray_dynamic_styles[rayIndex] = style;
    } else {
        // TestLight case
        ctx->singleRayShadowStyle = style;
    }
}
