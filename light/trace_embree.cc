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

#include <light/trace_embree.hh>

#include <light/light.hh>
#include <light/trace.hh> // for SampleTexture

#include <common/bsputils.hh>
#include <common/polylib.hh>
#include <vector>
#include <climits>

sceneinfo skygeom; // sky. always occludes.
sceneinfo solidgeom; // solids. always occludes.
sceneinfo filtergeom; // conditional occluders.. needs to run ray intersection filter

static RTCDevice device;
RTCScene scene;

static const mbsp_t *bsp_static;

void ResetEmbree()
{
    skygeom = {};
    solidgeom = {};
    filtergeom = {};

    if (scene) {
        rtcReleaseScene(scene);
        scene = nullptr;
    }

    if (device) {
        rtcReleaseDevice(device);
        device = nullptr;
    }

    bsp_static = nullptr;
}

/**
 * Returns 1.0 unless a custom alpha value is set.
 * The priority is: "_light_alpha" (read from extended_texinfo_flags), then "alpha", then Q2 surface flags
 */
static float Face_Alpha(const mbsp_t *bsp, const modelinfo_t *modelinfo, const mface_t *face)
{
    const surfflags_t &extended_flags = extended_texinfo_flags[face->texinfo];
    const int surf_flags = Face_ContentsOrSurfaceFlags(bsp, face);
    const bool is_q2 = bsp->loadversion->game->id == GAME_QUAKE_II;

    if (extended_flags.light_alpha) {
        return *extended_flags.light_alpha;
    }

    // next check "alpha" key (q1)
    if (modelinfo->alpha.is_changed()) {
        return modelinfo->alpha.value();
    }

    // next handle q2 surface flags
    if (is_q2) {
        if (surf_flags & Q2_SURF_TRANS33) {
            return 0.33f;
        }
        if (surf_flags & Q2_SURF_TRANS66) {
            return 0.66f;
        }
    }

    // no alpha requested
    return 1.0f;
}

sceneinfo CreateGeometry(
    const mbsp_t *bsp, RTCDevice g_device, RTCScene scene, const std::vector<const mface_t *> &faces)
{
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

    // temprary buffers used while gathering faces
    std::vector<Vertex> vertices_temp;
    std::vector<Triangle> tris_temp;

    sceneinfo s;
    s.geomID = geomID;

    auto add_vert = [&](const qvec3f &pos) { vertices_temp.push_back({.point{pos[0], pos[1], pos[2], 0.0f}}); };

    // FIXME: reuse vertices
    auto add_tri = [&](const mface_t *face, int bsp_vert0, int bsp_vert1, int bsp_vert2, const modelinfo_t *modelinfo) {
        const qvec3f final_pos0 = Vertex_GetPos(bsp, bsp_vert0) + modelinfo->offset;
        const qvec3f final_pos1 = Vertex_GetPos(bsp, bsp_vert1) + modelinfo->offset;
        const qvec3f final_pos2 = Vertex_GetPos(bsp, bsp_vert2) + modelinfo->offset;

        // push the 3 vertices
        int first_vert_index = vertices_temp.size();
        add_vert(final_pos0);
        add_vert(final_pos1);
        add_vert(final_pos2);

        tris_temp.push_back({first_vert_index, first_vert_index + 1, first_vert_index + 2});

        const surfflags_t &extended_flags = extended_texinfo_flags[face->texinfo];

        triinfo info;

        info.face = face;
        info.modelinfo = modelinfo;
        info.texinfo = &bsp->texinfo[face->texinfo];

        info.texture = Face_Texture(bsp, face);

        // FIXME: don't these need to check extended_flags?
        info.shadowworldonly = modelinfo->shadowworldonly.boolValue();
        info.shadowself = modelinfo->shadowself.boolValue();
        info.switchableshadow = modelinfo->switchableshadow.boolValue();
        info.switchshadstyle = modelinfo->switchshadstyle.value();

        info.channelmask = extended_flags.object_channel_mask.value_or(modelinfo->object_channel_mask.value());

        info.alpha = Face_Alpha(bsp, modelinfo, face);

        // mxd
        if (bsp->loadversion->game->id == GAME_QUAKE_II) {
            const int surf_flags = Face_ContentsOrSurfaceFlags(bsp, face);
            info.is_fence = surf_flags & Q2_SURF_ALPHATEST;
            info.is_glass = !info.is_fence && (surf_flags & (Q2_SURF_TRANS33 | Q2_SURF_TRANS66));
        } else {
            const char *name = Face_TextureName(bsp, face);
            info.is_fence = (name[0] == '{');
            info.is_glass = (info.alpha < 1.0f);
        }

        s.triInfo.push_back(info);
    };

    auto add_face = [&](const mface_t *face, const modelinfo_t *modelinfo) {
        if (face->numedges < 3)
            return;

        for (int j = 2; j < face->numedges; j++) {
            int bsp_vert0 = Face_VertexAtIndex(bsp, face, j - 1);
            int bsp_vert1 = Face_VertexAtIndex(bsp, face, j);
            int bsp_vert2 = Face_VertexAtIndex(bsp, face, 0);

            add_tri(face, bsp_vert0, bsp_vert1, bsp_vert2, modelinfo);
        }
    };

    for (const mface_t *face : faces) {
        // NOTE: can be null for "skip" faces
        const modelinfo_t *modelinfo = ModelInfoForFace(bsp, Face_GetNum(bsp, face));

        if (modelinfo) {
            add_face(face, modelinfo);
        }
    }

    // copy vertices, triangles from temporary buffers to embree-managed memory
    Vertex *vertices = (Vertex *)rtcSetNewGeometryBuffer(
        geom_0, RTC_BUFFER_TYPE_VERTEX, 0, RTC_FORMAT_FLOAT3, 4 * sizeof(float), vertices_temp.size());

    Triangle *triangles = (Triangle *)rtcSetNewGeometryBuffer(
        geom_0, RTC_BUFFER_TYPE_INDEX, 0, RTC_FORMAT_UINT3, 3 * sizeof(int), tris_temp.size());

    memcpy(vertices, vertices_temp.data(), sizeof(Vertex) * vertices_temp.size());
    memcpy(triangles, tris_temp.data(), sizeof(Triangle) * tris_temp.size());

    rtcCommitGeometry(geom_0);
    return s;
}

static void CreateGeometryFromWindings(
    RTCDevice g_device, RTCScene scene, const std::vector<polylib::winding_t> &windings)
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

    RTCGeometry geom_1 = rtcNewGeometry(g_device, RTC_GEOMETRY_TYPE_TRIANGLE);
    rtcSetGeometryBuildQuality(geom_1, RTC_BUILD_QUALITY_MEDIUM);
    rtcSetGeometryMask(geom_1, 1);
    rtcSetGeometryTimeStepCount(geom_1, 1);
    rtcAttachGeometry(scene, geom_1);
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

void ErrorCallback(void *userptr, const RTCError code, const char *str)
{
    fmt::print("RTC Error {}: {}\n", static_cast<int>(code), str);
}

const triinfo &Embree_LookupTriangleInfo(unsigned int geomID, unsigned int primID)
{
    const sceneinfo &info = Embree_SceneinfoForGeomID(geomID);
    return info.triInfo.at(primID);
}

inline qvec3f Embree_RayEndpoint(RTCRayN *ray, const qvec3f &dir, size_t N, size_t i)
{
    qvec3f org{RTCRayN_org_x(ray, N, i), RTCRayN_org_y(ray, N, i), RTCRayN_org_z(ray, N, i)};
    float &tfar = RTCRayN_tfar(ray, N, i);

    return org + (dir * tfar);
}

static void AddGlassToRay(RTCIntersectContext *context, unsigned rayIndex, float opacity, const qvec3d &glasscolor);
static void AddDynamicOccluderToRay(RTCIntersectContext *context, unsigned rayIndex, int style);

// called to evaluate transparency
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
        const triinfo &hit_triinfo = Embree_LookupTriangleInfo(geomID, primID);

        if (!(hit_triinfo.channelmask & rsi->shadowmask)) {
            // reject hit
            valid[i] = INVALID;
            continue;
        }

        if (!hit_triinfo.modelinfo) {
            // we hit a "skip" face with no associated model
            // reject hit (???)
            valid[i] = INVALID;
            continue;
        }

        if (hit_triinfo.shadowworldonly) {
            // we hit "_shadowworldonly" "1" geometry. Ignore the hit unless we are from world.
            if (!source_modelinfo || !source_modelinfo->isWorld()) {
                // reject hit
                valid[i] = INVALID;
                continue;
            }
        }

        if (hit_triinfo.shadowself) {
            // only casts shadows on itself
            if (source_modelinfo != hit_triinfo.modelinfo) {
                // reject hit
                valid[i] = INVALID;
                continue;
            }
        }

        if (hit_triinfo.switchableshadow) {
            // we hit a dynamic shadow caster. reject the hit, but store the
            // info about what we hit.

            const int style = hit_triinfo.switchshadstyle;

            AddDynamicOccluderToRay(context, rayIndex, style);

            // reject hit
            valid[i] = INVALID;
            continue;
        }

        float alpha = hit_triinfo.alpha;

        // test fence textures and glass
        if (hit_triinfo.is_fence || hit_triinfo.is_glass) {
            qvec3f rayDir =
                qv::normalize(qvec3f{RTCRayN_dir_x(ray, N, i), RTCRayN_dir_y(ray, N, i), RTCRayN_dir_z(ray, N, i)});
            qvec3f hitpoint = Embree_RayEndpoint(ray, rayDir, N, i);
            const qvec4b sample = SampleTexture(hit_triinfo.face, hit_triinfo.texinfo, hit_triinfo.texture, bsp_static,
                hitpoint); // mxd. Palette index -> color_rgba

            if (hit_triinfo.is_glass) {
                // hit glass...

                // mxd. Adjust alpha by texture alpha?
                if (sample[3] < 255)
                    alpha = sample[3] / 255.0f;

                qvec3f potentialHitGeometryNormal = qv::normalize(qvec3f{RTCHitN_Ng_x(potentialHit, N, i),
                    RTCHitN_Ng_y(potentialHit, N, i), RTCHitN_Ng_z(potentialHit, N, i)});

                const float raySurfaceCosAngle = qv::dot(rayDir, potentialHitGeometryNormal);

                // only pick up the color of the glass on the _exiting_ side of the glass.
                // (we currently trace "backwards", from surface point --> light source)
                if (raySurfaceCosAngle < 0) {
                    AddGlassToRay(context, rayIndex, alpha, sample.xyz() * (1.0 / 255.0));
                }

                // reject hit
                valid[i] = INVALID;
                continue;
            }

            if (hit_triinfo.is_fence) {
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

/**
 * For use with all rays coming from a model with non-default channel mask
 */
static void PerRay_FilterFuncN(const struct RTCFilterFunctionNArguments *args)
{
    int *const valid = args->valid;
    RTCIntersectContext *const context = args->context;
    struct RTCHitN *const potentialHit = args->hit;
    const unsigned int N = args->N;

    const int VALID = -1;
    const int INVALID = 0;

    auto *rsi = static_cast<const ray_source_info *>(context);

    for (size_t i = 0; i < N; i++) {
        if (valid[i] != VALID) {
            // we only need to handle valid rays
            continue;
        }

        const unsigned &geomID = RTCHitN_geomID(potentialHit, N, i);
        const unsigned &primID = RTCHitN_primID(potentialHit, N, i);

        // unpack ray index
        const triinfo &hit_triinfo = Embree_LookupTriangleInfo(geomID, primID);

        if (!(hit_triinfo.channelmask & rsi->shadowmask)) {
            // reject hit
            valid[i] = INVALID;
            continue;
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
static void Leaf_MakeFaces(const mbsp_t *bsp, const modelinfo_t *modelinfo, const mleaf_t *leaf,
    const std::vector<qplane3d> &planes, std::vector<polylib::winding_t> &result)
{
    for (const qplane3d &plane : planes) {
        // flip the inward-facing split plane to get the outward-facing plane of the face we're constructing
        qplane3d faceplane = -plane;

        std::optional<polylib::winding_t> winding = polylib::winding_t::from_plane(faceplane, 10e6);

        // clip `winding` by all of the other planes
        for (const qplane3d &plane2 : planes) {
            if (&plane2 == &plane)
                continue;

            // discard the back, continue clipping the front part
            winding = winding->clip_front(plane2);

            // check if everything was clipped away
            if (!winding)
                break;
        }

        if (!winding) {
            // logging::print("WARNING: winding clipped away\n");
        } else {
            result.push_back(winding->translate(modelinfo->offset));
        }
    }
}

void MakeFaces_r(const mbsp_t *bsp, const modelinfo_t *modelinfo, const int nodenum, std::vector<qplane3d> *planes,
    std::vector<polylib::winding_t> &result)
{
    if (nodenum < 0) {
        const int leafnum = -nodenum - 1;
        const mleaf_t *leaf = &bsp->dleafs[leafnum];

        if ((bsp->loadversion->game->id == GAME_QUAKE_II) ? (leaf->contents & Q2_CONTENTS_SOLID)
                                                          : leaf->contents == CONTENTS_SOLID) {
            Leaf_MakeFaces(bsp, modelinfo, leaf, *planes, result);
        }
        return;
    }

    const bsp2_dnode_t *node = &bsp->dnodes[nodenum];

    // go down the front side
    planes->push_back(Node_Plane(bsp, node, false));
    MakeFaces_r(bsp, modelinfo, node->children[0], planes, result);
    planes->pop_back();

    // go down the back side
    planes->push_back(Node_Plane(bsp, node, true));
    MakeFaces_r(bsp, modelinfo, node->children[1], planes, result);
    planes->pop_back();
}

static void MakeFaces(
    const mbsp_t *bsp, const modelinfo_t *modelinfo, const dmodelh2_t *model, std::vector<polylib::winding_t> &result)
{
    std::vector<qplane3d> planes;
    MakeFaces_r(bsp, modelinfo, model->headnode[0], &planes, result);
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

        // check reasons that a bmodel can be shadow casting
        const bool isWorld = model->isWorld();
        const bool shadow = model->shadow.boolValue();
        const bool shadowself = model->shadowself.boolValue();
        const bool shadowworldonly = model->shadowworldonly.boolValue();
        const bool switchableshadow = model->switchableshadow.boolValue();
        const bool has_custom_channel_mask = (model->object_channel_mask.value() != CHANNEL_MASK_DEFAULT);

        if (!(isWorld || shadow || shadowself || shadowworldonly || switchableshadow || has_custom_channel_mask))
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

            // non-default channel mask
            if (model->object_channel_mask.value() != CHANNEL_MASK_DEFAULT ||
                extended_flags.object_channel_mask.value_or(CHANNEL_MASK_DEFAULT) != CHANNEL_MASK_DEFAULT) {
                filterfaces.push_back(face);
                continue;
            }

            const int contents_or_surf_flags = Face_ContentsOrSurfaceFlags(bsp, face); // mxd
            const mtexinfo_t *texinfo = Face_Texinfo(bsp, face);
            const bool is_q2 = bsp->loadversion->game->id == GAME_QUAKE_II;

            // mxd. Skip NODRAW faces, but not SKY ones (Q2's sky01.wal has both flags set)
            if (is_q2 && (contents_or_surf_flags & Q2_SURF_NODRAW) && !(contents_or_surf_flags & Q2_SURF_SKY))
                continue;

            // handle glass / water
            const float alpha = Face_Alpha(bsp, model, face);
            if (alpha < 1.0f ||
                (is_q2 && (contents_or_surf_flags & (Q2_SURF_ALPHATEST | Q2_SURF_TRANS33 | Q2_SURF_TRANS66)))) {
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
                    (!light_options.arghradcompat.value() ||
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
    std::vector<polylib::winding_t> skipwindings;
    for (const modelinfo_t *modelinfo : tracelist) {
        if (modelinfo->model->numfaces == 0) {
            MakeFaces(bsp, modelinfo, modelinfo->model, skipwindings);
        }
    }

    device = rtcNewDevice(NULL);
    rtcSetDeviceErrorFunction(
        device, ErrorCallback, nullptr); // mxd. Changed from rtcDeviceSetErrorFunction to silence compiler warning...

    // log version
    const size_t ver_maj = rtcGetDeviceProperty(device, RTC_DEVICE_PROPERTY_VERSION_MAJOR);
    const size_t ver_min = rtcGetDeviceProperty(device, RTC_DEVICE_PROPERTY_VERSION_MINOR);
    const size_t ver_pat = rtcGetDeviceProperty(device, RTC_DEVICE_PROPERTY_VERSION_PATCH);
    logging::funcprint("Embree version: {}.{}.{}\n", ver_maj, ver_min, ver_pat);

    scene = rtcNewScene(device);
    // we're using RTCIntersectContext::filter so it's required that we set
    // RTC_SCENE_FLAG_CONTEXT_FILTER_FUNCTION
    rtcSetSceneFlags(scene, RTC_SCENE_FLAG_CONTEXT_FILTER_FUNCTION);
    rtcSetSceneBuildQuality(scene, RTC_BUILD_QUALITY_HIGH);
    skygeom = CreateGeometry(bsp, device, scene, skyfaces);
    solidgeom = CreateGeometry(bsp, device, scene, solidfaces);
    filtergeom = CreateGeometry(bsp, device, scene, filterfaces);
    CreateGeometryFromWindings(device, scene, skipwindings);

    rtcSetGeometryIntersectFilterFunction(rtcGetGeometry(scene, filtergeom.geomID), Embree_FilterFuncN);
    rtcSetGeometryOccludedFilterFunction(rtcGetGeometry(scene, filtergeom.geomID), Embree_FilterFuncN);

    rtcCommitScene(scene);

    logging::funcprint("\n");
    logging::print("\t{} sky faces\n", skyfaces.size());
    logging::print("\t{} solid faces\n", solidfaces.size());
    logging::print("\t{} filtered faces\n", filterfaces.size());
    logging::print("\t{} shadow-casting skip faces\n", skipwindings.size());
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
    opacity = std::clamp(opacity, 0.0f, 1.0f);

    Q_assert(rayIndex < rs->_numrays);

    rs->_ray_hit_glass[rayIndex] = true;
    rs->_ray_glass_color[rayIndex] = glasscolor;
    rs->_ray_glass_opacity[rayIndex] = opacity;
}

static void AddDynamicOccluderToRay(RTCIntersectContext *context, unsigned rayIndex, int style)
{
    ray_source_info *ctx = static_cast<ray_source_info *>(context);
    raystream_embree_common_t *rs = ctx->raystream;

    if (rs != nullptr) {
        rs->_ray_dynamic_styles[rayIndex] = style;
    }
}

ray_source_info::ray_source_info(raystream_embree_common_t *raystream_, const modelinfo_t *self_, int shadowmask_)
    : raystream(raystream_),
      self(self_),
      shadowmask(shadowmask_)
{
    rtcInitIntersectContext(this);

    flags = RTC_INTERSECT_CONTEXT_FLAG_COHERENT;

    if (shadowmask != CHANNEL_MASK_DEFAULT) {
        // non-default shadow mask means we have to use the slow path
        filter = PerRay_FilterFuncN;
    }
}
