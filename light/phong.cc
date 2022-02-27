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
//#include <cstdio>
#include <iostream>

#include <light/phong.hh>
#include <light/ltface.hh>

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

#include <common/qvec.hh>

using namespace std;

static neighbour_t FaceOverlapsEdge(const qvec3f &p0, const qvec3f &p1, const mbsp_t *bsp, const mface_t *f)
{
    for (int edgeindex = 0; edgeindex < f->numedges; edgeindex++) {
        const int v0 = Face_VertexAtIndex(bsp, f, edgeindex);
        const int v1 = Face_VertexAtIndex(bsp, f, (edgeindex + 1) % f->numedges);

        const qvec3f &v0point = Vertex_GetPos(bsp, v0);
        const qvec3f &v1point = Vertex_GetPos(bsp, v1);
        if (LinesOverlap(p0, p1, v0point, v1point)) {
            return neighbour_t{f, v0point, v1point};
        }
    }
    return neighbour_t{nullptr, qvec3f{}, qvec3f{}};
}

static void FacesOverlappingEdge_r(
    const qvec3f &p0, const qvec3f &p1, const mbsp_t *bsp, int nodenum, vector<neighbour_t> *result)
{
    if (nodenum < 0) {
        // we don't do anything for leafs.
        // faces are handled on nodes.
        return;
    }

    const bsp2_dnode_t *node = BSP_GetNode(bsp, nodenum);
    const dplane_t *plane = BSP_GetPlane(bsp, node->planenum);
    const vec_t p0dist = plane->distance_to_fast(p0);
    const vec_t p1dist = plane->distance_to_fast(p1);

    if (fabs(p0dist) < 0.1 && fabs(p1dist) < 0.1) {
        // check all faces on this node.
        for (int i = 0; i < node->numfaces; i++) {
            const mface_t *face = BSP_GetFace(bsp, node->firstface + i);
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
inline vector<neighbour_t> FacesOverlappingEdge(
    const qvec3f &p0, const qvec3f &p1, const mbsp_t *bsp, const dmodelh2_t *model)
{
    vector<neighbour_t> result;
    FacesOverlappingEdge_r(p0, p1, bsp, model->headnode[0], &result);
    return result;
}

std::vector<neighbour_t> NeighbouringFaces_new(const mbsp_t *bsp, const mface_t *face)
{
    std::vector<neighbour_t> result;
    std::set<const mface_t *> used_faces;

    for (int i = 0; i < face->numedges; i++) {
        const qvec3f &p0 = Face_PointAtIndex(bsp, face, i);
        const qvec3f &p1 = Face_PointAtIndex(bsp, face, (i + 1) % face->numedges);

        std::vector<neighbour_t> tmp = FacesOverlappingEdge(p0, p1, bsp, &bsp->dmodels[0]);

        // ensure the neighbour_t edges are pointing the same direction as the p0->p1 edge
        // (modifies them inplace)
        const qvec3f p0p1dir = qv::normalize(p1 - p0);
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
static float AngleBetweenVectors(const qvec3f &d1, const qvec3f &d2)
{
    float length_product = (qv::length(d1) * qv::length(d2));
    if (length_product == 0)
        return 0;
    float cosangle = qv::dot(d1, d2) / length_product;
    if (cosangle < -1)
        cosangle = -1;
    if (cosangle > 1)
        cosangle = 1;

    float angle = acos(cosangle);
    return angle;
}

/* returns the angle between vectors p2->p1 and p2->p3 */
static float AngleBetweenPoints(const qvec3f &p1, const qvec3f &p2, const qvec3f &p3)
{
    const qvec3f d1 = p1 - p2;
    const qvec3f d2 = p3 - p2;
    float result = AngleBetweenVectors(d1, d2);
    return result;
}

static bool s_builtPhongCaches;
static std::map<const mface_t *, std::vector<face_normal_t>> vertex_normals;
static std::set<int> interior_verts;
static map<const mface_t *, set<const mface_t *>> smoothFaces;
static map<int, vector<const mface_t *>> vertsToFaces;
static map<int, vector<const mface_t *>> planesToFaces;
static edgeToFaceMap_t EdgeToFaceMap;
static vector<face_cache_t> FaceCache;

vector<const mface_t *> FacesUsingVert(int vertnum)
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
bool FacesSmoothed(const mface_t *f1, const mface_t *f2)
{
    Q_assert(s_builtPhongCaches);

    const auto &facesIt = smoothFaces.find(f1);
    if (facesIt == smoothFaces.end())
        return false;

    const set<const mface_t *> &faceSet = facesIt->second;
    if (faceSet.find(f2) == faceSet.end())
        return false;

    return true;
}

const std::set<const mface_t *> &GetSmoothFaces(const mface_t *face)
{
    Q_assert(s_builtPhongCaches);

    static std::set<const mface_t *> empty;
    const auto it = smoothFaces.find(face);

    if (it == smoothFaces.end())
        return empty;

    return it->second;
}

const std::vector<const mface_t *> &GetPlaneFaces(const mface_t *face)
{
    Q_assert(s_builtPhongCaches);

    static std::vector<const mface_t *> empty;
    const auto it = planesToFaces.find(face->planenum);

    if (it == planesToFaces.end())
        return empty;

    return it->second;
}

// Adapted from https://github.com/NVIDIAGameWorks/donut/blob/main/src/engine/GltfImporter.cpp#L684
std::tuple<qvec3f, qvec3f> compute_tangents(
    const std::array<qvec3f, 3> &positions, const std::array<qvec2f, 3> &tex_coords)
{
    qvec3f dPds = positions[1] - positions[0];
    qvec3f dPdt = positions[2] - positions[0];

    qvec3f dTds = tex_coords[1] - tex_coords[0];
    qvec3f dTdt = tex_coords[2] - tex_coords[0];

    float r = 1.0f / (dTds[0] * dTdt[1] - dTds[1] * dTdt[0]);
    qvec3f tangent = (dPds * dTdt[1] - dPdt * dTds[1]) * r;
    qvec3f bitangent = (dPdt * dTds[0] - dPds * dTdt[0]) * r;

    return {qv::normalize(tangent), qv::normalize(bitangent)};
}

/* access the final phong-shaded vertex normal */
const face_normal_t &GetSurfaceVertexNormal(const mbsp_t *bsp, const mface_t *f, const int vertindex)
{
    Q_assert(s_builtPhongCaches);

    // handle degenerate faces
    const auto it = vertex_normals.find(f);
    if (it == vertex_normals.end()) {
        static const face_normal_t empty{};
        return empty;
    }
    const auto &face_normals_vec = it->second;
    return face_normals_vec.at(vertindex);
}

static bool FacesOnSamePlane(const std::vector<const mface_t *> &faces)
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

const mface_t *Face_EdgeIndexSmoothed(const mbsp_t *bsp, const mface_t *f, const int edgeindex)
{
    Q_assert(s_builtPhongCaches);

    const int v0 = Face_VertexAtIndex(bsp, f, edgeindex);
    const int v1 = Face_VertexAtIndex(bsp, f, (edgeindex + 1) % f->numedges);

    auto it = EdgeToFaceMap.find(make_pair(v1, v0));
    if (it != EdgeToFaceMap.end()) {
        for (const mface_t *neighbour : it->second) {
            if (neighbour == f) {
                // Invalid face, e.g. with vertex numbers: [0, 1, 0, 2]
                continue;
            }

            const bool sameplane = (neighbour->planenum == f->planenum && neighbour->side == f->side);

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

    for (auto &f : bsp->dfaces) {
        // walk edges
        for (int j = 0; j < f.numedges; j++) {
            const int v0 = Face_VertexAtIndex(bsp, &f, j);
            const int v1 = Face_VertexAtIndex(bsp, &f, (j + 1) % f.numedges);

            if (v0 == v1) {
                // ad_swampy.bsp has faces with repeated verts...
                continue;
            }

            const auto edge = make_pair(v0, v1);
            auto &edgeFacesRef = result[edge];

            if (find(begin(edgeFacesRef), end(edgeFacesRef), &f) != end(edgeFacesRef)) {
                // another sort of degenerate face where the same edge A->B appears more than once on the face
                continue;
            }
            edgeFacesRef.push_back(&f);
        }
    }

    return result;
}

static vector<face_normal_t> Face_VertexNormals(const mbsp_t *bsp, const mface_t *face)
{
    vector<face_normal_t> normals;
    for (int i = 0; i < face->numedges; i++) {
        normals.emplace_back(GetSurfaceVertexNormal(bsp, face, i));
    }
    return normals;
}

static vector<face_cache_t> MakeFaceCache(const mbsp_t *bsp)
{
    vector<face_cache_t> result;
    for (auto &face : bsp->dfaces) {
        result.emplace_back(bsp, &face, Face_VertexNormals(bsp, &face));
    }
    return result;
}

/**
 * Q2: Returns nonzero if phong is requested on this face, in which case that is
 * the face tag to smooth with. Otherwise returns 0.
 */
static int Q2_FacePhongValue(const mbsp_t *bsp, const mface_t *face)
{
    const gtexinfo_t *texinfo = BSP_GetTexinfo(bsp, face->texinfo);
    if (texinfo != nullptr) {
        if (texinfo->value != 0 && ((texinfo->flags.native & Q2_SURF_LIGHT) == 0)) {
            return texinfo->value;
        }
    }
    return 0;
}

inline bool isDegenerate(const qvec3f &a, const qvec3f &b, const qvec3f &c)
{
    float lengths[] = {qv::distance(a, b), qv::distance(b, c), qv::distance(c, a)};

    for (size_t i = 0; i < 3; i++) {
        if (lengths[i] == lengths[(i + 1) % 3] + lengths[(i + 2) % 3]) {
            return true;
        }
    }

    return false;
}

void CalculateVertexNormals(const mbsp_t *bsp)
{
    LogPrint("--- {} ---\n", __func__);

    Q_assert(!s_builtPhongCaches);
    s_builtPhongCaches = true;

    EdgeToFaceMap = MakeEdgeToFaceMap(bsp);

    // read _phong and _phong_angle from entities for compatiblity with other qbsp's, at the expense of no
    // support on func_detail/func_group
    for (size_t i = 0; i < bsp->dmodels.size(); i++) {
        const modelinfo_t *info = ModelInfoForModel(bsp, i);
        const uint8_t phongangle_byte = (uint8_t)clamp((int)rint(info->getResolvedPhongAngle()), 0, 255);

        if (!phongangle_byte)
            continue;

        for (int j = info->model->firstface; j < info->model->firstface + info->model->numfaces; j++) {
            const mface_t *f = BSP_GetFace(bsp, j);

            extended_texinfo_flags[f->texinfo].phong_angle = phongangle_byte;
        }
    }

    for (auto &f : bsp->dfaces) {
        // build "plane -> faces" map
        planesToFaces[f.planenum].push_back(&f);

        // build "vert index -> faces" map
        for (size_t j = 0; j < f.numedges; j++) {
            const int v = Face_VertexAtIndex(bsp, &f, j);
            vertsToFaces[v].push_back(&f);
        }
    }

    // track "interior" verts, these are in the middle of a face, and mess up normal interpolation
    for (size_t i = 0; i < bsp->dvertexes.size(); i++) {
        auto &faces = vertsToFaces[i];
        if (faces.size() > 1 && FacesOnSamePlane(faces)) {
            interior_verts.insert(i);
        }
    }

    // fmt::print("CalculateVertexNormals: {} interior verts\n", interior_verts.size());

    // build the "face -> faces to smooth with" map
    for (auto &f : bsp->dfaces) {
        const auto f_points = GLM_FacePoints(bsp, &f);
        const qvec3d f_norm = Face_Normal(bsp, &f);
        const qplane3d f_plane = Face_Plane(bsp, &f);

        // any face normal within this many degrees can be smoothed with this face
        const vec_t &f_phong_angle = extended_texinfo_flags[f.texinfo].phong_angle;
        vec_t f_phong_angle_concave = extended_texinfo_flags[f.texinfo].phong_angle_concave;
        if (f_phong_angle_concave == 0) {
            f_phong_angle_concave = f_phong_angle;
        }
        const bool f_wants_phong = (f_phong_angle || f_phong_angle_concave);

        if (!f_wants_phong)
            continue;

        for (int j = 0; j < f.numedges; j++) {
            const int v = Face_VertexAtIndex(bsp, &f, j);
            // walk over all faces incident to f (we will walk over neighbours multiple times, doesn't matter)
            for (const mface_t *f2 : vertsToFaces[v]) {
                if (f2 == &f)
                    continue;

                // FIXME: factor out and share with above?
                const vec_t &f2_phong_angle = extended_texinfo_flags[f2->texinfo].phong_angle;
                vec_t f2_phong_angle_concave = extended_texinfo_flags[f2->texinfo].phong_angle_concave;
                if (f2_phong_angle_concave == 0) {
                    f2_phong_angle_concave = f2_phong_angle;
                }
                const bool f2_wants_phong = (f2_phong_angle || f2_phong_angle_concave);

                if (!f2_wants_phong)
                    continue;

                const auto f2_points = GLM_FacePoints(bsp, f2);
                const qvec3f f2_centroid = qv::PolyCentroid(f2_points.begin(), f2_points.end());
                const qvec3d f2_norm = Face_Normal(bsp, f2);

                const vec_t cosangle = qv::dot(f_norm, f2_norm);

                const bool concave = f_plane.distAbove(f2_centroid) > 0.1;
                const vec_t f_threshold = concave ? f_phong_angle_concave : f_phong_angle;
                const vec_t f2_threshold = concave ? f2_phong_angle_concave : f2_phong_angle;
                const vec_t min_threshold = min(f_threshold, f2_threshold);
                const vec_t cosmaxangle = cos(DEG2RAD(min_threshold));

                // check the angle between the face normals
                if (cosangle >= cosmaxangle) {
                    smoothFaces[&f].insert(f2);
                }
            }
        }
    }

    // Q2: build the "face -> faces to smooth with" map
    // FIXME: merge this into the above loop
    for (auto &f : bsp->dfaces) {
        const int f_phongValue = Q2_FacePhongValue(bsp, &f);
        if (f_phongValue == 0)
            continue;

        for (int j = 0; j < f.numedges; j++) {
            const int v = Face_VertexAtIndex(bsp, &f, j);
            // walk over all faces incident to f (we will walk over neighbours multiple times, doesn't matter)
            for (const mface_t *f2 : vertsToFaces[v]) {
                if (f2 == &f)
                    continue;

                const int f2_phongValue = Q2_FacePhongValue(bsp, f2);
                if (f_phongValue != f2_phongValue)
                    continue;

                // we've already checked f_phongValue is nonzero, so smooth these two faces.
                smoothFaces[&f].insert(f2);
            }
        }
    }

    LogPrint(LOG_VERBOSE, "        {} faces for smoothing\n", smoothFaces.size());

    // finally do the smoothing for each face
    for (auto &f : bsp->dfaces) {
        if (f.numedges < 3) {
            FLogPrint("face {} is degenerate with {} edges\n", Face_GetNum(bsp, &f), f.numedges);
            for (int j = 0; j < f.numedges; j++) {
                LogPrint("                         vert at {}\n", Face_PointAtIndex(bsp, &f, j));
            }
            continue;
        }

        const auto &neighboursToSmooth = smoothFaces[&f];
        const qvec3f f_norm = Face_Normal(bsp, &f); // get the face normal

        // face tangent
        auto t1 = TexSpaceToWorld(bsp, &f);
        std::tuple<qvec3f, qvec3f> tangents(t1.col(0).xyz(), qv::normalize(t1.col(1).xyz()));

        // gather up f and neighboursToSmooth
        std::vector<const mface_t *> fPlusNeighbours;
        fPlusNeighbours.push_back(&f);
        std::copy(neighboursToSmooth.begin(), neighboursToSmooth.end(), std::back_inserter(fPlusNeighbours));

        // global vertex index -> smoothed normal
        std::map<int, face_normal_t> smoothedNormals;

        // walk fPlusNeighbours
        for (auto f2 : fPlusNeighbours) {
            const auto f2_poly = GLM_FacePoints(bsp, f2);
            const float f2_area = qv::PolyArea(f2_poly.begin(), f2_poly.end());
            const qvec3f f2_norm = Face_Normal(bsp, f2);

            // f2 face tangent
            auto t2 = TexSpaceToWorld(bsp, f2);
            std::tuple<qvec3f, qvec3f> f2_tangents(t2.col(0).xyz(), qv::normalize(t2.col(1).xyz()));

            // walk the vertices of f2, and add their contribution to smoothedNormals
            for (int j = 0; j < f2->numedges; j++) {
                const int prev_vert_num = Face_VertexAtIndex(bsp, f2, ((j - 1) + f2->numedges) % f2->numedges);
                const int curr_vert_num = Face_VertexAtIndex(bsp, f2, j);
                const int next_vert_num = Face_VertexAtIndex(bsp, f2, (j + 1) % f2->numedges);

                const qvec3f &prev_vert_pos = Vertex_GetPos(bsp, prev_vert_num);
                const qvec3f &curr_vert_pos = Vertex_GetPos(bsp, curr_vert_num);
                const qvec3f &next_vert_pos = Vertex_GetPos(bsp, next_vert_num);

                const float angle_radians = AngleBetweenPoints(prev_vert_pos, curr_vert_pos, next_vert_pos);

                float weight = f2_area * angle_radians;
                if (!std::isfinite(weight)) {
                    // TODO: not sure if needed?
                    weight = 0;
                }

                auto &n = smoothedNormals[curr_vert_num];
                n.normal += f2_norm * weight;
                n.tangent += std::get<0>(f2_tangents) * weight;
                n.bitangent += std::get<1>(f2_tangents) * weight;
            }
        }

        // normalize vertex normals (NOTE: updates smoothedNormals map)
        for (auto &pair : smoothedNormals) {
            face_normal_t &vertNormal = pair.second;
            if (0 == qv::length(vertNormal.normal)) {
                // this happens when there are colinear vertices, which give zero-area triangles,
                // so there is no contribution to the normal of the triangle in the middle of the
                // line. Not really an error, just set it to use the face normal.
#if 0
                const int vertIndex = pair.first;
                LogPrint("Failed to calculate normal for vertex {} at ({} {} {})\n",
                         vertIndex,
                         bsp->dvertexes[vertIndex].point[0],
                         bsp->dvertexes[vertIndex].point[1],
                         bsp->dvertexes[vertIndex].point[2]);
#endif
                vertNormal = {f_norm, std::get<0>(tangents), std::get<1>(tangents)};
            } else {
                vertNormal = {qv::normalize(vertNormal.normal), qv::normalize(vertNormal.tangent),
                    qv::normalize(vertNormal.bitangent)};
            }

            // FIXME: why
            if (std::isnan(vertNormal.tangent[0])) {
                vertNormal.tangent = std::get<0>(tangents);
                if (std::isnan(vertNormal.tangent[0])) {
                    vertNormal.tangent = {0, 0, 0};
                }
            }
            if (std::isnan(vertNormal.bitangent[0])) {
                vertNormal.bitangent = std::get<1>(tangents);
                if (std::isnan(vertNormal.bitangent[0])) {
                    vertNormal.bitangent = {0, 0, 0};
                }
            }
        }

        // sanity check
        if (!neighboursToSmooth.size()) {
            for (auto vertIndexNormalPair : smoothedNormals) {
                Q_assert(qv::epsilonEqual(vertIndexNormalPair.second.normal, f_norm, (float)EQUAL_EPSILON));
            }
        }

        // now, record all of the smoothed normals that are actually part of `f`
        for (int j = 0; j < f.numedges; j++) {
            int v = Face_VertexAtIndex(bsp, &f, j);
            Q_assert(smoothedNormals.find(v) != smoothedNormals.end());

            vertex_normals[&f].push_back(smoothedNormals[v]);
        }
    }

    FaceCache = MakeFaceCache(bsp);
}

const face_cache_t &FaceCacheForFNum(int fnum)
{
    Q_assert(s_builtPhongCaches);
    return FaceCache.at(fnum);
}
