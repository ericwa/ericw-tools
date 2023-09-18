/*  Copyright (C) 1996-1997  Id Software, Inc.

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

#include <light/ltface.hh>

#include <light/light.hh>
#include <light/trace_embree.hh>
#include <light/phong.hh>
#include <light/surflight.hh> //mxd
#include <light/entities.hh>
#include <light/lightgrid.hh>
#include <light/trace.hh>
#include <light/litfile.hh> // for facesup_t

#include <common/imglib.hh>
#include <common/log.hh>
#include <common/bsputils.hh>
#include <common/qvec.hh>
#include <common/ostream.hh>

#include <atomic>
#include <cassert>
#include <cmath>
#include <algorithm>
#include <fstream>

std::atomic<uint32_t> total_light_rays, total_light_ray_hits, total_samplepoints;
std::atomic<uint32_t> total_bounce_rays, total_bounce_ray_hits;
std::atomic<uint32_t> total_surflight_rays, total_surflight_ray_hits; // mxd
std::atomic<uint32_t> fully_transparent_lightmaps;
static bool warned_about_light_map_overflow, warned_about_light_style_overflow;

/* Debug helper - move elsewhere? */
void PrintFaceInfo(const mface_t *face, const mbsp_t *bsp)
{
    const mtexinfo_t *tex = &bsp->texinfo[face->texinfo];
    const char *texname = Face_TextureName(bsp, face);

    logging::print("face {}, texture {}, {} edges; vectors:\n"
                   "{}\n",
        Face_GetNum(bsp, face), texname, face->numedges, tex->vecs);

    for (int i = 0; i < face->numedges; i++) {
        int edge = bsp->dsurfedges[face->firstedge + i];
        int vert = Face_VertexAtIndex(bsp, face, i);
        const qvec3f &point = GetSurfaceVertexPoint(bsp, face, i);
        const qvec3f norm = GetSurfaceVertexNormal(bsp, face, i).normal;
        logging::print("{} {:3} ({:3.3}, {:3.3}, {:3.3}) :: normal ({:3.3}, {:3.3}, {:3.3}) :: edge {}\n",
            i ? "          " : "    verts ", vert, point[0], point[1], point[2], norm[0], norm[1], norm[2], edge);
    }
}

class position_t
{
public:
    bool m_unoccluded;
    const mface_t *m_actualFace;
    qvec3f m_position;
    qvec3f m_interpolatedNormal;

    position_t(qvec3f position)
        : m_unoccluded(false),
          m_actualFace(nullptr),
          m_position(position),
          m_interpolatedNormal({})
    {
    }

    position_t(const mface_t *actualFace, const qvec3f &position, const qvec3f &interpolatedNormal)
        : m_unoccluded(true),
          m_actualFace(actualFace),
          m_position(position),
          m_interpolatedNormal(interpolatedNormal){};
};

static constexpr float sampleOffPlaneDist = 1.0f;

/*

 Why this is so complicated:

 - vanilla tools just did a trace from the face centroid to the desired location of the sample point.
   This doesn't work because we want to allow pillars blocking parts of the face. (func_detail_wall).

 - must avoid solutions that leak light through walls (e.g. having an interior wall
   pick up light from outside), and avoid light leaks e.g. in V-shaped sconces
   that have a light inside the "V".

 - it's critical to allow sample points to extend onto neighbouring faces,
   both for phong shading to look good, as well as general light quality

 */
static position_t PositionSamplePointOnFace(
    const mbsp_t *bsp, const mface_t *face, const bool phongShaded, const qvec3f &point, const qvec3f &modelOffset);

std::vector<const mface_t *> NeighbouringFaces_old(const mbsp_t *bsp, const mface_t *face)
{
    std::vector<const mface_t *> result;
    for (int i = 0; i < face->numedges; i++) {
        const mface_t *smoothed = Face_EdgeIndexSmoothed(bsp, face, i);
        if (smoothed != nullptr && smoothed != face) {
            result.push_back(smoothed);
        }
    }
    return result;
}

position_t CalcPointNormal(const mbsp_t *bsp, const mface_t *face, const qvec3f &origPoint, bool phongShaded,
    const faceextents_t &faceextents, int recursiondepth, const qvec3f &modelOffset)
{
    const auto &facecache = FaceCacheForFNum(Face_GetNum(bsp, face));
    const qvec4f &surfplane = facecache.plane();
    const auto &points = facecache.points();
    const auto &edgeplanes = facecache.edgePlanes();
    // const auto &neighbours = facecache.neighbours();

    // check for degenerate face
    if (points.empty() || edgeplanes.empty())
        return position_t(origPoint);

    // project `point` onto the surface plane, then lift it off again
    const qvec3f point = ProjectPointOntoPlane(surfplane, origPoint) + (qvec3f(surfplane) * sampleOffPlaneDist);

    // check if in face..
    if (EdgePlanes_PointInside(edgeplanes, point)) {
        return PositionSamplePointOnFace(bsp, face, phongShaded, point, modelOffset);
    }

#if 0
    // fixme: handle "not possible to compute"
    const qvec3f centroid = Face_Centroid(bsp, face);

    for (const neighbour_t &n : neighbours) {
        /*
         
         check if in XXX area:
         
   "in1"     "in2"
        \XXXX|
         \XXX|
          |--|----|
          |\ |    |
          |  *    |  * = centroid
          |------/
         
         */
        
        const qvec3f in1_normal = qv::cross(qv::normalize(n.p0 - centroid), facecache.normal());
        const qvec3f in2_normal = qv::cross(facecache.normal(), qv::normalize(n.p1 - centroid));
        const qvec4f in1 = MakePlane(in1_normal, n.p0);
        const qvec4f in2 = MakePlane(in2_normal, n.p1);
        
        const float in1_dist = DistAbovePlane(in1, point);
        const float in2_dist = DistAbovePlane(in2, point);
        if (in1_dist >= 0 && in2_dist >= 0) {
            const auto &n_facecache = FaceCacheForFNum(Face_GetNum(bsp, n.face));
            const qvec4f &n_surfplane = n_facecache.plane();
            const auto &n_edgeplanes = n_facecache.edgePlanes();
            
            // project `point` onto the surface plane, then lift it off again
            const qvec3f n_point = ProjectPointOntoPlane(n_surfplane, origPoint) + (qvec3f(n_surfplane) * sampleOffPlaneDist);
            
            // check if in face..
            if (EdgePlanes_PointInside(n_edgeplanes, n_point)) {
                return PositionSamplePointOnFace(bsp, n.face, phongShaded, n_point, modelOffset);
            }
        }
    }
#endif

    // not in any triangle. among the edges this point is _behind_,
    // search for the one that the point is least past the endpoints of the edge
    {
        int bestplane = -1;
        float bestdist = FLT_MAX;

        for (int i = 0; i < face->numedges; i++) {
            const qvec3f &v0 = points.at(i);
            const qvec3f &v1 = points.at((i + 1) % points.size());

            const auto edgeplane = MakeInwardFacingEdgePlane(v0, v1, surfplane);
            if (!edgeplane.first)
                continue; // degenerate edge

            const float planedist = DistAbovePlane(edgeplane.second, point);
            if (planedist < POINT_EQUAL_EPSILON) {
                // behind this plane. check whether we're between the endpoints.

                const qvec3f v0v1 = v1 - v0;
                const float v0v1dist = qv::length(v0v1);

                const float t = FractionOfLine(v0, v1, point); // t=0 for point=v0, t=1 for point=v1

                float edgedist;
                if (t < 0)
                    edgedist = fabs(t) * v0v1dist;
                else if (t > 1)
                    edgedist = t * v0v1dist;
                else
                    edgedist = 0;

                if (edgedist < bestdist) {
                    bestplane = i;
                    bestdist = edgedist;
                }
            }
        }

        if (bestplane != -1) {
            // FIXME: Also need to handle non-smoothed but same plane
            const mface_t *smoothed = Face_EdgeIndexSmoothed(bsp, face, bestplane);
            if (smoothed) {
                // try recursive search
                if (recursiondepth < 3) {
                    // call recursively to look up normal in the adjacent face
                    return CalcPointNormal(
                        bsp, smoothed, point, phongShaded, faceextents, recursiondepth + 1, modelOffset);
                }
            }
        }
    }

    // 2. Try snapping to poly

    const std::pair<int, qvec3f> closest = ClosestPointOnPolyBoundary(points, point);
    float luxelSpaceDist;
    {
        auto desired_point_in_lmspace = faceextents.worldToLMCoord(point);
        auto closest_point_on_face_in_lmspace = faceextents.worldToLMCoord(closest.second);

        luxelSpaceDist = qv::distance(desired_point_in_lmspace, closest_point_on_face_in_lmspace);
    }

    if (luxelSpaceDist <= 1) {
        // Snap it to the face edge. Add the 1 unit off plane.
        const qvec3f snapped = closest.second + (qvec3f(surfplane) * sampleOffPlaneDist);
        return PositionSamplePointOnFace(bsp, face, phongShaded, snapped, modelOffset);
    }

    // This point is too far from the polygon to be visible in game, so don't bother calculating lighting for it.
    // Dont contribute to interpolating.
    // We could safely colour it in pink for debugging.
    return position_t(point);
}

// Dump points to a .map file
static void CalcPoints_Debug(const lightsurf_t *surf, const mbsp_t *bsp)
{
    std::ofstream f("calcpoints.map");

    for (int t = 0; t < surf->height; t++) {
        for (int s = 0; s < surf->width; s++) {
            const int i = t * surf->width + s;
            const auto &sample = surf->samples[i];
            const qvec3d &point = sample.point;
            const qvec3d mangle = qv::mangle_from_vec(sample.normal);

            f << "{\n";
            f << "\"classname\" \"light\"\n";
            ewt::print(f, "\"origin\" \"{}\"\n", point);
            ewt::print(f, "\"mangle\" \"{}\"\n", mangle);
            ewt::print(f, "\"face\" \"{}\"\n", sample.realfacenum);
            ewt::print(f, "\"occluded\" \"{}\"\n", sample.occluded);
            ewt::print(f, "\"s\" \"{}\"\n", s);
            ewt::print(f, "\"t\" \"{}\"\n", t);
            f << "}\n";
        }
    }

    logging::print("wrote face {}'s sample points ({}x{}) to calcpoints.map\n", Face_GetNum(bsp, surf->face),
        surf->width, surf->height);

    PrintFaceInfo(surf->face, bsp);
}

/// Checks if the point is in any solid (solid or sky leaf)
/// 1. the world
/// 2. any shadow-casting bmodel
/// 3. the `self` model (regardless of whether it's selfshadowing)
///
/// This is used for marking sample points as occluded.
static bool Light_PointInAnySolid(const mbsp_t *bsp, const dmodelh2_t *self, const qvec3d &point)
{
    if (Light_PointInSolid(bsp, self, point))
        return true;

    auto *self_modelinfo = ModelInfoForModel(bsp, self - bsp->dmodels.data());
    if (self_modelinfo->object_channel_mask.value() == CHANNEL_MASK_DEFAULT) {
        if (Light_PointInWorld(bsp, point))
            return true;
    }

    for (const auto &modelinfo : tracelist) {
        if (modelinfo->object_channel_mask.value() != self_modelinfo->object_channel_mask.value())
            continue;

        if (Light_PointInSolid(bsp, modelinfo->model, point - modelinfo->offset)) {
            // Only mark occluded if the bmodel is fully opaque
            if (modelinfo->alpha.value() == 1.0f)
                return true;
        }
    }

    return false;
}

// precondition: `point` is on the same plane as `face` and within the bounds.
static position_t PositionSamplePointOnFace(
    const mbsp_t *bsp, const mface_t *face, const bool phongShaded, const qvec3f &point, const qvec3f &modelOffset)
{
    const auto &facecache = FaceCacheForFNum(Face_GetNum(bsp, face));
    const auto &points = facecache.points();
    const auto &normals = facecache.normals();
    const auto &edgeplanes = facecache.edgePlanes();
    const auto &plane = facecache.plane();

    if (edgeplanes.empty()) {
        // degenerate polygon
        return position_t(point);
    }

    const float planedist = DistAbovePlane(plane, point);
    if (!(fabs(planedist - sampleOffPlaneDist) <= 0.1)) {
        // something is wrong?
        return position_t(point);
    }

    const float insideDist = EdgePlanes_PointInsideDist(edgeplanes, point);
    if (insideDist < -POINT_EQUAL_EPSILON) {
        // Non-convex polygon
        return position_t(point);
    }

    const modelinfo_t *mi = ModelInfoForFace(bsp, Face_GetNum(bsp, face));
    if (mi == nullptr) {
        // missing model ("skip" faces) don't get lighting
        return position_t(point);
    }

    // Get the point normal
    qvec3f pointNormal;
    if (phongShaded) {
        const auto interpNormal = InterpolateNormal(points, normals, point);
        // We already know the point is in the face, so this should always succeed
        if (!interpNormal.first)
            return position_t(point);
        pointNormal = interpNormal.second;
    } else {
        pointNormal = plane;
    }

    const bool inSolid = Light_PointInAnySolid(bsp, mi->model, point + modelOffset);
    if (inSolid) {
#if 1
        // try +/- 0.5 units in X/Y/Z (8 tests)

        for (int x = -1; x <= 1; x += 2) {
            for (int y = -1; y <= 1; y += 2) {
                for (int z = -1; z <= 1; z += 2) {
                    const qvec3f jitter = qvec3f(x, y, z) * 0.5;
                    const qvec3f new_point = point + jitter;

                    if (!Light_PointInAnySolid(bsp, mi->model, new_point + modelOffset)) {
                        return position_t(face, new_point, pointNormal);
                    }
                }
            }
        }
#else
        // this has issues with narrow sliver-shaped faces moving the sample points a lot into vastly different lighting

        // Check distance to border
        const float distanceInside = EdgePlanes_PointInsideDist(edgeplanes, point);
        if (distanceInside < 1.0f) {
            // Point is too close to the border. Try nudging it inside.
            const auto &shrunk = facecache.pointsShrunkBy1Unit();
            if (!shrunk.empty()) {
                const pair<int, qvec3f> closest = ClosestPointOnPolyBoundary(shrunk, point);
                const qvec3f newPoint = closest.second + (qvec3f(plane) * sampleOffPlaneDist);
                if (!Light_PointInAnySolid(bsp, mi->model, newPoint + modelOffset))
                    return position_t(face, newPoint, pointNormal);
            }
        }
#endif
        return position_t(point);
    }

    return position_t(face, point, pointNormal);
}

/*
 * =================
 * CalcPoints
 * For each texture aligned grid point, back project onto the plane
 * to get the world xyz value of the sample point
 * =================
 */
static void CalcPoints(
    const modelinfo_t *modelinfo, const qvec3d &offset, lightsurf_t *surf, const mbsp_t *bsp, const mface_t *face)
{
    const settings::worldspawn_keys &cfg = *surf->cfg;

    surf->width = surf->extents.width() * light_options.extra.value();
    surf->height = surf->extents.height() * light_options.extra.value();

    const float starts = -0.5 + (0.5 / light_options.extra.value());
    const float startt = -0.5 + (0.5 / light_options.extra.value());
    const float st_step = 1.0f / light_options.extra.value();

    /* Allocate surf->points */
    size_t num_points = surf->width * surf->height;
    surf->samples.resize(num_points);

    const auto points = Face_Points(bsp, face);
    const auto edgeplanes = MakeInwardFacingEdgePlanes(points);

    for (int t = 0; t < surf->height; t++) {
        for (int s = 0; s < surf->width; s++) {
            const int i = t * surf->width + s;
            auto &sample = surf->samples[i];

            const vec_t us = starts + s * st_step;
            const vec_t ut = startt + t * st_step;

            sample.point =
                surf->extents.LMCoordToWorld(qvec2f(us, ut)) + surf->plane.normal; // one unit in front of face

            // do this before correcting the point, so we can wrap around the inside of pipes
            const bool phongshaded = (surf->curved && cfg.phongallowed.value());
            const auto res = CalcPointNormal(bsp, face, sample.point, phongshaded, surf->extents, 0, offset);

            sample.occluded = !res.m_unoccluded;
            sample.realfacenum = res.m_actualFace != nullptr ? Face_GetNum(bsp, res.m_actualFace) : -1;
            sample.point = res.m_position + offset;
            sample.normal = res.m_interpolatedNormal;
        }
    }

    if (dump_facenum == Face_GetNum(bsp, face)) {
        CalcPoints_Debug(surf, bsp);
    }
}

static bool Mod_LeafPvs(const mbsp_t *bsp, const mleaf_t *leaf, uint8_t *out)
{
    const size_t num_pvsclusterbytes = DecompressedVisSize(bsp);

    // init to all visible
    memset(out, 0xFF, num_pvsclusterbytes);

    if (bsp->loadversion->game->id == GAME_QUAKE_II) {
        auto it = UncompressedVis().find(leaf->cluster);
        if (it == UncompressedVis().end()) {
            return false;
        }

        memcpy(out, it->second.data(), num_pvsclusterbytes);
    } else {
        auto it = UncompressedVis().find(leaf->visofs);
        if (it == UncompressedVis().end()) {
            return false;
        }

        memcpy(out, it->second.data(), num_pvsclusterbytes);
    }

    return true;
}

static const std::vector<uint8_t> *Mod_LeafPvs(const mbsp_t *bsp, const mleaf_t *leaf)
{
    if (bsp->loadversion->game->contents_are_liquid({leaf->contents})) {
        // the liquid case is because leaf->contents might be in an opaque liquid,
        // which we typically want light to pass through, but visdata would report that
        // there's no visibility across the opaque liquid. so, skip culling and do the raytracing.
        return nullptr;
    }

    const int key = (bsp->loadversion->game->id == GAME_QUAKE_II) ? leaf->cluster : leaf->visofs;
    if (auto it = UncompressedVis().find(leaf->cluster); it != UncompressedVis().end()) {
        return &it->second;
    }
    return nullptr;
}

// returns true if pvs can see leaf
static bool Pvs_LeafVisible(const mbsp_t *bsp, const std::vector<uint8_t> &pvs, const mleaf_t *leaf)
{
    if (bsp->loadversion->game->id == GAME_QUAKE_II) {
        if (leaf->cluster < 0) {
            return false;
        }

        if (leaf->cluster >= bsp->dvis.bit_offsets.size() ||
            bsp->dvis.get_bit_offset(VIS_PVS, leaf->cluster) >= bsp->dvis.bits.size()) {
            logging::print("Pvs_LeafVisible: invalid visofs for cluster {}\n", leaf->cluster);
            return false;
        }

        return !!(pvs[leaf->cluster >> 3] & (1 << (leaf->cluster & 7)));
    } else {
        const int leafnum = (leaf - bsp->dleafs.data());
        const int visleaf = leafnum - 1;

        if (visleaf < 0 || visleaf >= bsp->dmodels[0].visleafs) {
            logging::print("WARNING: bad/empty vis data on leaf?");
            return false;
        }

        return !!(pvs[visleaf >> 3] & (1 << (visleaf & 7)));
    }
}

static void CalcPvs(const mbsp_t *bsp, lightsurf_t *lightsurf)
{
    const int pvssize = DecompressedVisSize(bsp);
    const mleaf_t *lastleaf = nullptr;

    // set defaults
    lightsurf->pvs.clear();

    if (!bsp->dvis.bits.size()) {
        return;
    }

    // set lightsurf->pvs
    uint8_t *pointpvs = (uint8_t *)alloca(pvssize);
    lightsurf->pvs.resize(pvssize);

    for (auto &sample : lightsurf->samples) {
        const mleaf_t *leaf = Light_PointInLeaf(bsp, sample.point);

        /* most/all of the surface points are probably in the same leaf */
        if (leaf == lastleaf)
            continue;

        lastleaf = leaf;

        /* copy the pvs for this leaf into pointpvs */
        Mod_LeafPvs(bsp, leaf, pointpvs);

        if (bsp->loadversion->game->contents_are_liquid({leaf->contents})) {
            // hack for when the sample point might be in an opaque liquid, blocking vis,
            // but we typically want light to pass through these.
            // see also VisCullEntity() which handles the case when the light emitter is in liquid.
            for (int j = 0; j < pvssize; j++) {
                lightsurf->pvs[j] |= 0xff;
            }
            break;
        }

        /* merge the pvs for this sample point into lightsurf->pvs */
        for (int j = 0; j < pvssize; j++) {
            lightsurf->pvs[j] |= pointpvs[j];
        }
    }
}

static std::unique_ptr<lightsurf_t> Lightsurf_Init(const modelinfo_t *modelinfo, const settings::worldspawn_keys &cfg,
    const mface_t *face, const mbsp_t *bsp, const facesup_t *facesup,
    const bspx_decoupled_lm_perface *facesup_decoupled)
{
    auto spaceToWorld = TexSpaceToWorld(bsp, face);

    /* Check for invalid texture axes */
    if (std::isnan(spaceToWorld.at(0, 0))) {
        logging::print("Bad texture axes on face:\n");
        PrintFaceInfo(face, bsp);
        return nullptr;
    }

    auto lightsurf = std::make_unique<lightsurf_t>();
    lightsurf->cfg = &cfg;
    lightsurf->modelinfo = modelinfo;
    lightsurf->bsp = bsp;
    lightsurf->face = face;
    lightsurf->occlusion_stream = std::make_unique<raystream_occlusion_t>();
    lightsurf->intersection_stream = std::make_unique<raystream_intersection_t>();

    if (Face_IsLightmapped(bsp, face)) {
        /* if liquid doesn't have the TEX_SPECIAL flag set, the map was qbsp'ed with
         * lit water in mind. In that case receive light from both top and bottom.
         * (lit will only be rendered in compatible engines, but degrades gracefully.)
         */
        lightsurf->twosided = Face_IsTranslucent(bsp, face);

        // pick the larger of the two scales
        lightsurf->lightmapscale =
            (facesup && facesup->lmscale < modelinfo->lightmapscale) ? facesup->lmscale : modelinfo->lightmapscale;

        const surfflags_t &extended_flags = extended_texinfo_flags[face->texinfo];
        lightsurf->curved = extended_flags.phong_angle != 0 || Q2_FacePhongValue(bsp, face);

        // override the autodetected twosided setting?
        if (extended_flags.light_twosided) {
            lightsurf->twosided = *extended_flags.light_twosided;
        }

        // nodirt
        if (modelinfo->dirt.is_changed()) {
            lightsurf->nodirt = (modelinfo->dirt.value() == -1);
        } else {
            lightsurf->nodirt = extended_flags.no_dirt;
        }

        // minlight
        if (modelinfo->minlight.is_changed()) {
            lightsurf->minlight = modelinfo->minlight.value();
        } else if (extended_flags.minlight) {
            lightsurf->minlight = *extended_flags.minlight;
        } else {
            lightsurf->minlight = light_options.minlight.value();
        }

        // minlightMottle
        if (modelinfo->minlightMottle.is_changed()) {
            lightsurf->minlightMottle = modelinfo->minlightMottle.value();
        } else if (light_options.minlightMottle.is_changed()) {
            lightsurf->minlightMottle = light_options.minlightMottle.value();
        } else {
            lightsurf->minlightMottle = false;
        }

        // Q2 uses a 0-1 range for minlight
        if (bsp->loadversion->game->id == GAME_QUAKE_II) {
            lightsurf->minlight *= 128.f;
        }

        // maxlight
        if (modelinfo->maxlight.is_changed()) {
            lightsurf->maxlight = modelinfo->maxlight.value();
        } else {
            lightsurf->maxlight = extended_flags.maxlight;
        }

        // lightcolorscale
        if (modelinfo->lightcolorscale.is_changed()) {
            lightsurf->lightcolorscale = modelinfo->lightcolorscale.value();
        } else {
            lightsurf->lightcolorscale = extended_flags.lightcolorscale;
        }

        // Q2 uses a 0-1 range for minlight
        if (bsp->loadversion->game->id == GAME_QUAKE_II) {
            lightsurf->maxlight *= 128.f;
        }

        // minlight_color
        if (modelinfo->minlight_color.is_changed()) {
            lightsurf->minlight_color = modelinfo->minlight_color.value();
        } else if (!qv::emptyExact(extended_flags.minlight_color)) {
            lightsurf->minlight_color = extended_flags.minlight_color;
        } else {
            lightsurf->minlight_color = light_options.minlight_color.value();
        }

        /* never receive dirtmapping on lit liquids */
        if (Face_IsTranslucent(bsp, face)) {
            lightsurf->nodirt = true;
        }

        /* handle glass alpha */
        if (modelinfo->alpha.value() < 1) {
            /* skip culling of rays coming from the back side of the face */
            lightsurf->twosided = true;
        }

        /* object channel mask */
        if (extended_flags.object_channel_mask) {
            lightsurf->object_channel_mask = *extended_flags.object_channel_mask;
        } else {
            lightsurf->object_channel_mask = modelinfo->object_channel_mask.value();
        }

        if (extended_flags.surflight_minlight_scale) {
            lightsurf->surflight_minlight_scale = *extended_flags.surflight_minlight_scale;
        } else {
            lightsurf->surflight_minlight_scale = 1.0f;
        }

        /* Set up the plane, not including model offset */
        qplane3d &plane = lightsurf->plane;
        if (face->side) {
            plane = -bsp->dplanes[face->planenum];
        } else {
            plane = bsp->dplanes[face->planenum];
        }

        const mtexinfo_t *tex = &bsp->texinfo[face->texinfo];
        lightsurf->snormal = qv::normalize(tex->vecs.row(0).xyz());
        lightsurf->tnormal = -qv::normalize(tex->vecs.row(1).xyz());

        /* Set up the surface points */
        if (light_options.world_units_per_luxel.is_changed()) {
            if (bsp->loadversion->game->id == GAME_QUAKE_II && (Face_Texinfo(bsp, face)->flags.native & Q2_SURF_SKY)) {
                lightsurf->extents = faceextents_t(*face, *bsp, world_units_per_luxel_t{}, 512.f);
            } else if (extended_flags.world_units_per_luxel) {
                lightsurf->extents =
                    faceextents_t(*face, *bsp, world_units_per_luxel_t{}, *extended_flags.world_units_per_luxel);
            } else {
                lightsurf->extents =
                    faceextents_t(*face, *bsp, world_units_per_luxel_t{}, light_options.world_units_per_luxel.value());
            }
        } else {
            lightsurf->extents = faceextents_t(*face, *bsp, lightsurf->lightmapscale);
        }
        lightsurf->vanilla_extents = faceextents_t(*face, *bsp, LMSCALE_DEFAULT);

        CalcPoints(modelinfo, modelinfo->offset, lightsurf.get(), bsp, face);

        /* Correct the plane for the model offset (must be done last,
           calculation of face extents / points needs the uncorrected plane) */
        qvec3d planepoint = (plane.normal * plane.dist) + modelinfo->offset;
        plane.dist = qv::dot(plane.normal, planepoint);

        /* Correct bounding sphere */
        lightsurf->extents.origin += modelinfo->offset;
        lightsurf->extents.bounds = lightsurf->extents.bounds.translate(modelinfo->offset);

        lightsurf->intersection_stream->resize(lightsurf->samples.size());
        lightsurf->occlusion_stream->resize(lightsurf->samples.size());

        /* Setup vis data */
        CalcPvs(bsp, lightsurf.get());
    }

    // emissiveness is handled later and allocated only if necessary

    return lightsurf;
}

static void Lightmap_AllocOrClear(lightmap_t *lightmap, const lightsurf_t *lightsurf)
{
    if (!lightmap->samples.size()) {
        /* first use of this lightmap, allocate the storage for it. */
        lightmap->samples.resize(lightsurf->samples.size());
    } else if (lightmap->style != INVALID_LIGHTSTYLE) {
        /* clear only the data that is going to be merged to it. there's no point clearing more */
        std::fill_n(lightmap->samples.begin(), lightsurf->samples.size(), lightsample_t{});
        lightmap->bounce_color = {};
    }
}

#if 0
static const lightmap_t *Lightmap_ForStyle_ReadOnly(const lightsurf_t *lightsurf, const int style)
{
    for (const auto &lm : lightsurf->lightmapsByStyle) {
        if (lm.style == style)
            return &lm;
    }
    return nullptr;
}
#endif

/*
 * Lightmap_ForStyle
 *
 * If lightmap with given style has already been allocated, return it.
 * Otherwise, return the next available map.  A new map is not marked as
 * allocated since it may not be kept if no lights hit.
 */
static lightmap_t *Lightmap_ForStyle(lightmapdict_t *lightmaps, const int style, const lightsurf_t *lightsurf)
{
    for (auto &lm : *lightmaps) {
        if (lm.style == style)
            return &lm;
    }

    // no exact match, check for an unsaved one
    for (auto &lm : *lightmaps) {
        if (lm.style == INVALID_LIGHTSTYLE) {
            Lightmap_AllocOrClear(&lm, lightsurf);
            return &lm;
        }
    }

    // add a new one to the vector (invalidates existing lightmap_t pointers)
    lightmap_t &newLightmap = lightmaps->emplace_back();
    newLightmap.style = INVALID_LIGHTSTYLE;
    Lightmap_AllocOrClear(&newLightmap, lightsurf);
    return &newLightmap;
}

/*
 * Lightmap_Save
 *
 * As long as we have space for the style, mark as allocated,
 * otherwise emit a warning.
 */
static void Lightmap_Save(
    const mbsp_t *bsp, lightmapdict_t *lightmaps, const lightsurf_t *lightsurf, lightmap_t *lightmap, const int style)
{
    Q_assert(Face_IsLightmapped(bsp, lightsurf->face));

    if (lightmap->style == INVALID_LIGHTSTYLE) {
        lightmap->style = style;
    }
}

/*
 * ============================================================================
 * FACE LIGHTING
 * ============================================================================
 */

// returns the light contribution at a given distance, without regard for angle
vec_t GetLightValue(const settings::worldspawn_keys &cfg, const light_t *entity, vec_t dist)
{
    const float light = entity->light.value();

    // mxd. Apply falloff?
    const float lightdistance = entity->falloff.value();
    if (lightdistance > 0.0f) {
        if (entity->getFormula() == LF_LINEAR) {
            // Light can affect surface?
            if (lightdistance > dist)
                return light * (1.0f - (dist / lightdistance));
            else
                return 0.0f; // Surface is unaffected
        }
    }

    if (entity->getFormula() == LF_INFINITE || entity->getFormula() == LF_LOCALMIN)
        return light;

    vec_t value = cfg.scaledist.value() * entity->atten.value() * dist;

    switch (entity->getFormula()) {
        case LF_INVERSE: return light / (value / LF_SCALE);
        case LF_INVERSE2A:
            value += LF_SCALE;
            /* Fall through */
        case LF_INVERSE2: return light / ((value * value) / (LF_SCALE * LF_SCALE));
        case LF_LINEAR:
            if (light > 0)
                return (light - value > 0) ? light - value : 0;
            else
                return (light + value < 0) ? light + value : 0;
        default: Error("Internal error: unknown light formula");
    }
}

static float GetLightValueWithAngle(const settings::worldspawn_keys &cfg, const light_t *entity, const qvec3d &surfnorm,
    bool use_surfnorm, const qvec3d &surfpointToLightDir, float dist, bool twosided)
{
    vec_t angle;
    if (use_surfnorm) {
        angle = qv::dot(surfpointToLightDir, surfnorm);
    } else {
        angle = 1.0f;
    }

    if (entity->bleed.value() || twosided) {
        if (angle < 0) {
            angle = -angle; // ericw -- support "_bleed" option
        }
    }

    /* Light behind sample point? Zero contribution, period.
       see: https://github.com/ericwa/ericw-tools/issues/181 */
    if (angle < 0) {
        return 0;
    }

    /* Apply anglescale */
    angle = (1.0 - entity->anglescale.value()) + (entity->anglescale.value() * angle);

    /* Check spotlight cone */
    float spotscale = 1;
    if (entity->spotlight) {
        const vec_t falloff = qv::dot(entity->spotvec, surfpointToLightDir);
        if (falloff > entity->spotfalloff) {
            return 0;
        }
        if (falloff > entity->spotfalloff2) {
            /* Interpolate between the two spotlight falloffs */
            spotscale = falloff - entity->spotfalloff2;
            spotscale /= entity->spotfalloff - entity->spotfalloff2;
            spotscale = 1.0 - spotscale;
        }
    }

    float add = GetLightValue(cfg, entity, dist) * angle * spotscale;
    return add;
}

static void Matrix4x4_CM_Transform4(const std::array<vec_t, 16> &matrix, const qvec4d &vector, qvec4d &product)
{
    product[0] = matrix[0] * vector[0] + matrix[4] * vector[1] + matrix[8] * vector[2] + matrix[12] * vector[3];
    product[1] = matrix[1] * vector[0] + matrix[5] * vector[1] + matrix[9] * vector[2] + matrix[13] * vector[3];
    product[2] = matrix[2] * vector[0] + matrix[6] * vector[1] + matrix[10] * vector[2] + matrix[14] * vector[3];
    product[3] = matrix[3] * vector[0] + matrix[7] * vector[1] + matrix[11] * vector[2] + matrix[15] * vector[3];
}
static bool Matrix4x4_CM_Project(const qvec3d &in, qvec3d &out, const std::array<vec_t, 16> &modelviewproj)
{
    bool result = true;

    qvec4d v;
    Matrix4x4_CM_Transform4(modelviewproj, {in, 1}, v);

    v[0] /= v[3];
    v[1] /= v[3];
    if (v[2] < 0)
        result = false; // too close to the view
    v[2] /= v[3];

    out[0] = (1 + v[0]) / 2;
    out[1] = (1 + v[1]) / 2;
    out[2] = (1 + v[2]) / 2;
    if (out[2] > 1)
        result = false; // beyond far clip plane
    return result;
}

static bool LightFace_SampleMipTex(
    const img::texture *tex, const std::array<vec_t, 16> &projectionmatrix, const qvec3d &point, qvec3f &result)
{
    // okay, yes, this is weird, yes we're using a vec3_t for a coord...
    // this is because we're treating it like a cubemap. why? no idea.
    float weight[4];
    qvec4b pi[4];

    qvec3d coord;
    if (!Matrix4x4_CM_Project(point, coord, projectionmatrix) || coord[0] <= 0 || coord[0] >= 1 || coord[1] <= 0 ||
        coord[1] >= 1) {
        result = {};
        return false; // mxd
    }

    float sfrac = (coord[0]) * (tex->meta.width - 1); // mxd. We are sampling sbase+1 pixels, so multiplying by
                                                      // tex->width will result in an 1px overdraw, same for tbase
    const int sbase = sfrac;
    sfrac -= sbase;
    float tfrac = (1 - coord[1]) * (tex->meta.height - 1);
    const int tbase = tfrac;
    tfrac -= tbase;

    pi[0] = tex->pixels[((sbase + 0) % tex->meta.width) + (tex->meta.width * ((tbase + 0) % tex->meta.height))];
    weight[0] = (1 - sfrac) * (1 - tfrac);
    pi[1] = tex->pixels[((sbase + 1) % tex->meta.width) + (tex->meta.width * ((tbase + 0) % tex->meta.height))];
    weight[1] = (sfrac) * (1 - tfrac);
    pi[2] = tex->pixels[((sbase + 0) % tex->meta.width) + (tex->meta.width * ((tbase + 1) % tex->meta.height))];
    weight[2] = (1 - sfrac) * (tfrac);
    pi[3] = tex->pixels[((sbase + 1) % tex->meta.width) + (tex->meta.width * ((tbase + 1) % tex->meta.height))];
    weight[3] = (sfrac) * (tfrac);
    result = pi[0].xyz() * weight[0] + pi[1].xyz() * weight[1] + pi[2].xyz() * weight[2] + pi[3].xyz() * weight[3];
    result *= 2;

    return true;
}

static void GetLightContrib(const settings::worldspawn_keys &cfg, const light_t *entity, const qvec3d &surfnorm,
    bool use_surfnorm, const qvec3d &surfpoint, bool twosided, qvec3f &color_out, qvec3d &surfpointToLightDir_out,
    qvec3d &normalmap_addition_out, float *dist_out)
{
    float dist = GetDir(surfpoint, entity->origin.value(), surfpointToLightDir_out);
    if (dist < 0.1) {
        // Catch 0 distance between sample point and light (produces infinite brightness / nan's) and causes
        // problems later
        dist = 0.1f;
        surfpointToLightDir_out = {0, 0, 1};
    }
    const float add =
        GetLightValueWithAngle(cfg, entity, surfnorm, use_surfnorm, surfpointToLightDir_out, dist, twosided);

    /* write out the final color */
    if (entity->projectedmip) {
        qvec3f col;
        if (LightFace_SampleMipTex(entity->projectedmip, entity->projectionmatrix, surfpoint, col)) {
            // mxd. Modulate by light color...
            const auto &entcol = entity->color.value();
            for (int i = 0; i < 3; i++)
                col[i] *= entcol[i] * (1.0f / 255.0f);
        }

        color_out = col * add * (1.0f / 255.0f);
    } else {
        color_out = entity->color.value() * add * (1.0f / 255.0f);
    }

    // write normalmap contrib
    normalmap_addition_out = surfpointToLightDir_out * add;

    *dist_out = dist;
}

constexpr vec_t SQR(vec_t x)
{
    return x * x;
}

// CHECK: naming? why clamp*min*?
constexpr bool Light_ClampMin(lightsample_t &sample, const vec_t light, const qvec3d &color)
{
    bool changed = false;

    for (int i = 0; i < 3; i++) {
        float c = (float)(color[i] * (light / 255.0f));

        if (c > sample.color[i]) {
            sample.color[i] = c;
            changed = true;
        }
    }

    return changed;
}

constexpr vec_t fraction(const vec_t &min, const vec_t &val, const vec_t &max)
{
    if (val >= max)
        return 1.0;
    if (val <= min)
        return 0.0;

    return (val - min) / (max - min);
}

/*
 * ============
 * Dirt_GetScaleFactor
 *
 * returns scale factor for dirt/ambient occlusion
 * ============
 */
inline vec_t Dirt_GetScaleFactor(const settings::worldspawn_keys &cfg, vec_t occlusion, const light_t *entity,
    const vec_t entitydist, const lightsurf_t *surf)
{
    vec_t light_dirtgain = cfg.dirtgain.value();
    vec_t light_dirtscale = cfg.dirtscale.value();
    bool usedirt;

    /* is dirt processing disabled entirely? */
    if (!dirt_in_use)
        return 1.0f;
    if (surf && surf->nodirt)
        return 1.0f;

    /* should this light be affected by dirt? */
    if (entity) {
        if (entity->dirt.value() == -1) {
            usedirt = false;
        } else if (entity->dirt.value() == 1) {
            usedirt = true;
        } else {
            usedirt = cfg.dirt.value();
        }
    } else {
        /* no entity is provided, assume the caller wants dirt */
        usedirt = true;
    }

    /* if not, quit */
    if (!usedirt)
        return 1.0;

    /* override the global scale and gain values with the light-specific
       values, if present */
    if (entity) {
        if (entity->dirtgain.value())
            light_dirtgain = entity->dirtgain.value();
        if (entity->dirtscale.value())
            light_dirtscale = entity->dirtscale.value();
    }

    /* early out */
    if (occlusion <= 0.0f) {
        return 1.0f;
    }

    /* apply gain (does this even do much? heh) */
    float outDirt = pow(occlusion, light_dirtgain);
    if (outDirt > 1.0f) {
        outDirt = 1.0f;
    }

    /* apply scale */
    outDirt *= light_dirtscale;
    if (outDirt > 1.0f) {
        outDirt = 1.0f;
    }

    /* lerp based on distance to light */
    if (entity) {
        // From 0 to _dirt_off_radius units, no dirt.
        // From _dirt_off_radius to _dirt_on_radius, the dirt linearly ramps from 0 to full, and after _dirt_on_radius,
        // it's full dirt.

        if (entity->dirt_on_radius.is_changed() && entity->dirt_off_radius.is_changed()) {

            const float onRadius = entity->dirt_on_radius.value();
            const float offRadius = entity->dirt_off_radius.value();

            if (entitydist < offRadius) {
                outDirt = 0.0;
            } else if (entitydist >= offRadius && entitydist < onRadius) {
                const float frac = fraction(offRadius, entitydist, onRadius);
                outDirt = frac * outDirt;
            }
        }
    }

    /* return to sender */
    return 1.0f - outDirt;
}

/*
 * ================
 * CullLight
 *
 * Returns true if the given light doesn't reach lightsurf.
 * ================
 */
inline bool CullLight(const light_t *entity, const lightsurf_t *lightsurf)
{
    const settings::worldspawn_keys &cfg = *lightsurf->cfg;

    if (light_options.visapprox.value() == visapprox_t::RAYS &&
        entity->bounds.disjoint(lightsurf->extents.bounds, 0.001) &&
        entity->light_channel_mask.value() == CHANNEL_MASK_DEFAULT &&
        entity->shadow_channel_mask.value() == CHANNEL_MASK_DEFAULT) {
        // EstimateVisibleBoundsAtPoint uses CHANNEL_MASK_DEFAULT
        // for its rays, so only cull lights that are also going to be using
        // CHANNEL_MASK_DEFAULT for rendering
        return true;
    }

    qvec3d distvec = entity->origin.value() - lightsurf->extents.origin;
    const float dist = qv::length(distvec) - lightsurf->extents.radius;

    /* light is inside surface bounding sphere => can't cull */
    if (dist < 0) {
        return false;
    }

    /* return true if the light level at the closest point on the
     surface bounding sphere to the light source is <= fadegate.
     need fabs to handle antilights. */
    return fabs(GetLightValue(cfg, entity, dist)) <= light_options.gate.value();
}

static bool VisCullEntity(const mbsp_t *bsp, const std::vector<uint8_t> &pvs, const mleaf_t *entleaf)
{
    if (pvs.empty()) {
        return false;
    }
    if (entleaf == nullptr) {
        return false;
    }

    if (bsp->loadversion->game->contents_are_solid({entleaf->contents}) ||
        bsp->loadversion->game->contents_are_sky({entleaf->contents}) ||
        bsp->loadversion->game->contents_are_liquid({entleaf->contents})) {
        // the liquid case is because entleaf->contents might be in an opaque liquid,
        // which we typically want light to pass through, but visdata would report that
        // there's no visibility across the opaque liquid. so, skip culling and do the raytracing.
        return false;
    }

    return !Pvs_LeafVisible(bsp, pvs, entleaf);
}

/*
 * ================
 * LightFace_Entity
 * ================
 */
static void LightFace_Entity(
    const mbsp_t *bsp, const light_t *entity, lightsurf_t *lightsurf, lightmapdict_t *lightmaps)
{
    const settings::worldspawn_keys &cfg = *lightsurf->cfg;
    const modelinfo_t *modelinfo = lightsurf->modelinfo;
    const qplane3d *plane = &lightsurf->plane;

    /* vis cull */
    if (light_options.visapprox.value() == visapprox_t::VIS &&
        entity->light_channel_mask.value() == CHANNEL_MASK_DEFAULT &&
        entity->shadow_channel_mask.value() == CHANNEL_MASK_DEFAULT &&
        VisCullEntity(bsp, lightsurf->pvs, entity->leaf)) {
        return;
    }

    const vec_t planedist = plane->distance_to(entity->origin.value());

    /* don't bother with lights behind the surface.

       if the surface is curved, the light may be behind the surface, but it may
       still have a line of sight to a samplepoint, and that sample point's
       normal may be facing such that it receives some light, so we can't use this
       test in the curved case.
    */
    if (planedist < 0 && !entity->bleed.value() && !lightsurf->curved && !lightsurf->twosided) {
        return;
    }

    /* sphere cull surface and light */
    if (CullLight(entity, lightsurf)) {
        return;
    }

    // check lighting channels
    if (!(entity->light_channel_mask.value() & lightsurf->object_channel_mask)) {
        return;
    }

    /*
     * Check it for real
     */
    raystream_occlusion_t &rs = *lightsurf->occlusion_stream;
    rs.clearPushedRays();

    for (int i = 0; i < lightsurf->samples.size(); i++) {
        const auto &sample = lightsurf->samples[i];

        if (sample.occluded)
            continue;

        const qvec3d &surfpoint = sample.point;
        const qvec3d &surfnorm = sample.normal;

        qvec3d surfpointToLightDir;
        float surfpointToLightDist;
        qvec3f color;
        qvec3d normalcontrib;

        GetLightContrib(cfg, entity, surfnorm, true, surfpoint, lightsurf->twosided, color, surfpointToLightDir,
            normalcontrib, &surfpointToLightDist);

        const float occlusion = Dirt_GetScaleFactor(cfg, sample.occlusion, entity, surfpointToLightDist, lightsurf);
        color *= occlusion;

        /* Quick distance check first */
        if (fabs(LightSample_Brightness(color)) <= light_options.gate.value()) {
            continue;
        }

        rs.pushRay(i, surfpoint, surfpointToLightDir, surfpointToLightDist, &color, &normalcontrib);
    }

    // don't need closest hit, just checking for occlusion between light and surface point
    rs.tracePushedRaysOcclusion(modelinfo, entity->shadow_channel_mask.value());
    total_light_rays += rs.numPushedRays();

    int cached_style = entity->style.value();
    lightmap_t *cached_lightmap = Lightmap_ForStyle(lightmaps, cached_style, lightsurf);

    const int N = rs.numPushedRays();
    for (int j = 0; j < N; j++) {
        if (rs.getPushedRayOccluded(j)) {
            continue;
        }

        total_light_ray_hits++;

        int i = rs.getPushedRayPointIndex(j);

        // check if we hit a dynamic shadow caster (only applies to style 0 lights)
        //
        // note, this still works even though we're doing an occlusion trace - closest
        // hit doesn't matter. All that matters is whether there is a real (solid) occluder
        // between the ray start and end.
        //
        // If there is, the light is fully blocked and we bail out above, regardless of any
        // dynamic shadow casters that also might be along the ray.
        //
        // If not, then we are guaranteed to detect the dynamic shadow caster in the ray filter
        // (if any), and handle it here.
        int desired_style = entity->style.value();
        if (desired_style == 0) {
            desired_style = rs.getPushedRayDynamicStyle(j);
        }

        // if necessary, switch which lightmap we are writing to.
        if (desired_style != cached_style) {
            cached_style = desired_style;
            cached_lightmap = Lightmap_ForStyle(lightmaps, cached_style, lightsurf);
        }

        lightsample_t &sample = cached_lightmap->samples[i];

        sample.color += rs.getPushedRayColor(j);
        cached_lightmap->bounce_color += rs.getPushedRayColor(j);
        sample.direction += rs.getPushedRayNormalContrib(j);

        Lightmap_Save(bsp, lightmaps, lightsurf, cached_lightmap, cached_style);
    }
}

#define LIGHTPOINT_TAKE_MAX

/**
 * Calculates light at a given point from an entity
 */
static void LightPoint_Entity(const mbsp_t *bsp, raystream_occlusion_t &rs, const light_t *entity,
    const qvec3d &surfpoint, lightgrid_samples_t &result)
{
    rs.clearPushedRays();

    qvec3d surfpointToLightDir;
    float surfpointToLightDist;
    qvec3f color{};

    for (int axis = 0; axis < 3; ++axis) {
        for (int sign = -1; sign <= +1; sign += 2) {

            qvec3f cube_color;

            qvec3f cube_normal{};
            cube_normal[axis] = sign;

            qvec3d normalcontrib_unused;

            GetLightContrib(light_options, entity, cube_normal, true, surfpoint, false, cube_color, surfpointToLightDir,
                normalcontrib_unused, &surfpointToLightDist);

#ifdef LIGHTPOINT_TAKE_MAX
            if (qv::length2(cube_color) > qv::length2(color)) {
                color = cube_color;
            }
#else
            color += cube_color / 6.0;
#endif
        }
    }

    /* Quick distance check first */
    if (fabs(LightSample_Brightness(color)) <= light_options.gate.value()) {
        return;
    }

    rs.pushRay(0, surfpoint, surfpointToLightDir, surfpointToLightDist, &color);

    rs.tracePushedRaysOcclusion(nullptr, CHANNEL_MASK_DEFAULT);

    // add result
    const int N = rs.numPushedRays();
    for (int j = 0; j < N; j++) {
        if (rs.getPushedRayOccluded(j)) {
            continue;
        }

        result.add(rs.getPushedRayColor(j), entity->style.value());
    }
}

/*
 * =============
 * LightFace_Sky
 * =============
 */
static void LightFace_Sky(const mbsp_t *bsp, const sun_t *sun, lightsurf_t *lightsurf, lightmapdict_t *lightmaps)
{
    const settings::worldspawn_keys &cfg = *lightsurf->cfg;
    const modelinfo_t *modelinfo = lightsurf->modelinfo;
    const qplane3d *plane = &lightsurf->plane;

    // FIXME: Normalized sun vector should be stored in the sun_t. Also clarify which way the vector points (towards or
    // away..)
    // FIXME: Much of this is copied/pasted from LightFace_Entity, should probably be merged
    qvec3d incoming = qv::normalize(sun->sunvec);

    /* Don't bother if surface facing away from sun */
    const vec_t dp = qv::dot(incoming, plane->normal);
    if (dp < -LIGHT_ANGLE_EPSILON && !lightsurf->curved && !lightsurf->twosided) {
        return;
    }

    // check lighting channels (currently sunlight is always on CHANNEL_MASK_DEFAULT)
    if (!(lightsurf->object_channel_mask & CHANNEL_MASK_DEFAULT)) {
        return;
    }

    /* Check each point... */
    raystream_intersection_t &rs = *lightsurf->intersection_stream;
    rs.clearPushedRays();

    for (int i = 0; i < lightsurf->samples.size(); i++) {
        const auto &sample = lightsurf->samples[i];

        if (sample.occluded)
            continue;

        const qvec3d &surfpoint = sample.point;
        const qvec3d &surfnorm = sample.normal;

        vec_t angle = qv::dot(incoming, surfnorm);
        if (lightsurf->twosided) {
            if (angle < 0) {
                angle = -angle;
            }
        }

        angle = std::max(0.0, angle);

        angle = (1.0 - sun->anglescale) + sun->anglescale * angle;
        vec_t value = angle * sun->sunlight;

        if (sun->dirt) {
            value *= Dirt_GetScaleFactor(cfg, sample.occlusion, NULL, 0.0, lightsurf);
        }

        qvec3f color = sun->sunlight_color * (value / 255.0);

        /* Quick distance check first */
        if (fabs(LightSample_Brightness(color)) <= light_options.gate.value()) {
            continue;
        }

        qvec3d normalcontrib = incoming * value;

        rs.pushRay(i, surfpoint, incoming, MAX_SKY_DIST, &color, &normalcontrib);
    }

    // We need to check if the first hit face is a sky face, so we need
    // to test intersection (not occlusion)
    rs.tracePushedRaysIntersection(modelinfo, CHANNEL_MASK_DEFAULT);

    /* if sunlight is set, use a style 0 light map */
    int cached_style = sun->style;
    lightmap_t *cached_lightmap = Lightmap_ForStyle(lightmaps, cached_style, lightsurf);

    const int N = rs.numPushedRays();
    total_light_rays += N;

    for (int j = 0; j < N; j++) {
        if (rs.getPushedRayHitType(j) != hittype_t::SKY) {
            continue;
        }

        // check if we hit the wrong texture
        if (sun->suntexture_value) {
            const triinfo *face = rs.getPushedRayHitFaceInfo(j);
            if (sun->suntexture_value != face->texture) {
                continue;
            }
        }

        const int i = rs.getPushedRayPointIndex(j);

        // check if we hit a dynamic shadow caster
        int desired_style = sun->style;
        if (desired_style == 0) {
            desired_style = rs.getPushedRayDynamicStyle(j);
        }

        // if necessary, switch which lightmap we are writing to.
        if (desired_style != cached_style) {
            cached_style = desired_style;
            cached_lightmap = Lightmap_ForStyle(lightmaps, cached_style, lightsurf);
        }

        lightsample_t &sample = cached_lightmap->samples[i];

        sample.color += rs.getPushedRayColor(j);
        cached_lightmap->bounce_color += rs.getPushedRayColor(j);
        sample.direction += rs.getPushedRayNormalContrib(j);
        total_light_ray_hits++;

        Lightmap_Save(bsp, lightmaps, lightsurf, cached_lightmap, cached_style);
    }
}

static void LightPoint_Sky(const mbsp_t *bsp, raystream_intersection_t &rs, const sun_t *sun, const qvec3d &surfpoint,
    lightgrid_samples_t &result)
{
    // FIXME: Normalized sun vector should be stored in the sun_t. Also clarify which way the vector points (towards or
    // away..)
    // FIXME: Much of this is copied/pasted from LightFace_Entity, should probably be merged
    qvec3d incoming = qv::normalize(sun->sunvec);

    rs.clearPushedRays();

    // only 1 ray
    {
        qvec3f color{};

        for (int axis = 0; axis < 3; ++axis) {
            for (int sign = -1; sign <= +1; sign += 2) {

                qvec3f cube_color;

                qvec3f cube_normal{};
                cube_normal[axis] = sign;

                vec_t angle = qv::dot(incoming, cube_normal);
                angle = std::max(0.0, angle);
                angle = (1.0 - sun->anglescale) + sun->anglescale * angle;

                float value = angle * sun->sunlight;
                cube_color = sun->sunlight_color * (value / 255.0);

#ifdef LIGHTPOINT_TAKE_MAX
                if (qv::length2(cube_color) > qv::length2(color)) {
                    color = cube_color;
                }
#else
                color += cube_color / 6;
#endif
            }
        }

        /* Quick distance check first */
        if (fabs(LightSample_Brightness(color)) <= light_options.gate.value()) {
            return;
        }

        qvec3d normalcontrib{}; // unused

        rs.pushRay(0, surfpoint, incoming, MAX_SKY_DIST, &color, &normalcontrib);
    }

    // We need to check if the first hit face is a sky face, so we need
    // to test intersection (not occlusion)
    rs.tracePushedRaysIntersection(nullptr, CHANNEL_MASK_DEFAULT);

    // add result
    const int N = rs.numPushedRays();
    for (int j = 0; j < N; j++) {
        if (rs.getPushedRayHitType(j) != hittype_t::SKY) {
            continue;
        }

        result.add(rs.getPushedRayColor(j), sun->style);
    }
}

// Mottle

static int mod_round_to_neg_inf(int x, int y)
{
    assert(y > 0);
    if (x >= 0) {
        return x % y;
    }
    // e.g. with mod_round_to_neg_inf(-7, 3) we want +2
    const int temp = (-x) % y;
    return y - temp;
}

constexpr int mottle_texsize = 256;

/**
 * integers 0 through 255 shuffled with Python:
 *
 * import random
 * a = list(range(0, 256))
 * random.shuffle(a)
 */
static constexpr uint8_t MottlePermutation256[] = {11, 255, 250, 82, 217, 9, 144, 93, 136, 153, 55, 71, 73, 204, 96,
    180, 126, 8, 50, 46, 113, 91, 238, 143, 30, 215, 191, 243, 65, 58, 208, 33, 86, 1, 182, 118, 83, 115, 207, 52, 94,
    112, 205, 48, 99, 254, 117, 101, 157, 140, 72, 242, 244, 154, 10, 135, 155, 168, 125, 183, 148, 116, 187, 166, 25,
    156, 177, 231, 165, 57, 221, 105, 28, 211, 127, 41, 142, 253, 146, 87, 122, 229, 162, 137, 194, 174, 167, 15, 220,
    26, 235, 3, 39, 80, 88, 42, 202, 12, 97, 53, 70, 123, 170, 110, 214, 192, 173, 84, 169, 188, 64, 102, 147, 158, 100,
    69, 213, 193, 43, 20, 13, 237, 171, 103, 32, 190, 223, 150, 131, 206, 85, 124, 163, 18, 139, 132, 79, 29, 216, 232,
    178, 74, 24, 141, 201, 181, 152, 4, 7, 159, 134, 212, 226, 245, 164, 239, 47, 66, 27, 40, 197, 81, 78, 219, 228,
    241, 121, 23, 120, 230, 76, 252, 199, 184, 45, 203, 161, 89, 16, 21, 119, 5, 209, 196, 68, 130, 195, 176, 225, 233,
    128, 22, 248, 179, 249, 61, 108, 138, 145, 31, 49, 107, 56, 172, 224, 210, 6, 160, 189, 104, 200, 44, 175, 133, 77,
    62, 106, 92, 186, 227, 14, 38, 247, 37, 17, 222, 36, 75, 129, 185, 251, 240, 54, 151, 2, 98, 149, 0, 63, 218, 60,
    198, 19, 59, 90, 246, 234, 67, 51, 109, 95, 236, 35, 34, 114, 111};

/**
 * Return a noise texture value from 0-47.
 *
 * Vanilla Q2 tools just called (rand() % 48) per-luxel, which generates seams
 * and scales in size with lightmap scale.
 *
 * Replacement code uses "value noise", generating a tiling 3D texture
 * in world space.
 */
static float Mottle(const qvec3d &position)
{
#if 0
    return rand() % 48;
#else
    const float world_to_tex = 1 / 16.0f;

    qvec3d texspace_pos = position * world_to_tex;

    int coord_floor_x = static_cast<int>(floor(texspace_pos[0]));
    int coord_floor_y = static_cast<int>(floor(texspace_pos[1]));
    int coord_floor_z = static_cast<int>(floor(texspace_pos[2]));

    float coord_frac_x = static_cast<float>(texspace_pos[0] - coord_floor_x);
    float coord_frac_y = static_cast<float>(texspace_pos[1] - coord_floor_y);
    float coord_frac_z = static_cast<float>(texspace_pos[2] - coord_floor_z);

    assert(coord_frac_x >= 0 && coord_frac_x <= 1);
    assert(coord_frac_y >= 0 && coord_frac_y <= 1);
    assert(coord_frac_z >= 0 && coord_frac_z <= 1);

    coord_floor_x = mod_round_to_neg_inf(coord_floor_x, mottle_texsize);
    coord_floor_y = mod_round_to_neg_inf(coord_floor_y, mottle_texsize);
    coord_floor_z = mod_round_to_neg_inf(coord_floor_z, mottle_texsize);

    assert(coord_floor_x >= 0 && coord_floor_x < mottle_texsize);
    assert(coord_floor_y >= 0 && coord_floor_y < mottle_texsize);
    assert(coord_floor_z >= 0 && coord_floor_z < mottle_texsize);

    // look up sample in the 3d texture at an integer coordinate
    auto tex = [](int x, int y, int z) -> uint8_t {
        int v;
        v = MottlePermutation256[x % 256];
        v = MottlePermutation256[(v + y) % 256];
        v = MottlePermutation256[(v + z) % 256];
        return v;
    };

    // 3D bilinear interpolation
    float res = mix(mix(mix(tex(coord_floor_x, coord_floor_y, coord_floor_z),
                            tex(coord_floor_x + 1, coord_floor_y, coord_floor_z), coord_frac_x),
                        mix(tex(coord_floor_x, coord_floor_y + 1, coord_floor_z),
                            tex(coord_floor_x + 1, coord_floor_y + 1, coord_floor_z), coord_frac_x),
                        coord_frac_y),
        mix(mix(tex(coord_floor_x, coord_floor_y, coord_floor_z + 1),
                tex(coord_floor_x + 1, coord_floor_y, coord_floor_z + 1), coord_frac_x),
            mix(tex(coord_floor_x, coord_floor_y + 1, coord_floor_z + 1),
                tex(coord_floor_x + 1, coord_floor_y + 1, coord_floor_z + 1), coord_frac_x),
            coord_frac_y),
        coord_frac_z);

    return (res / 255.0f) * 48.0f;
#endif
}

/*
 * ============
 * LightFace_Min
 * ============
 */
static void LightFace_Min(const mbsp_t *bsp, const mface_t *face, const qvec3d &color, vec_t light,
    lightsurf_t *lightsurf, lightmapdict_t *lightmaps, int32_t style)
{
    const settings::worldspawn_keys &cfg = *lightsurf->cfg;

    const surfflags_t &extended_flags = extended_texinfo_flags[face->texinfo];
    if (extended_flags.no_minlight) {
        return; /* this face is excluded from minlight */
    }

    /* Find the lightmap */
    lightmap_t *lightmap = Lightmap_ForStyle(lightmaps, style, lightsurf);

    bool hit = false;
    for (int i = 0; i < lightsurf->samples.size(); i++) {
        const auto &surf_sample = lightsurf->samples[i];
        lightsample_t &sample = lightmap->samples[i];

        vec_t value = light;
        if (cfg.minlight_dirt.value()) {
            value *= Dirt_GetScaleFactor(cfg, surf_sample.occlusion, NULL, 0.0, lightsurf);
        }
        if (cfg.addminlight.value()) {
            sample.color += color * (value / 255.0);
            hit = true;
        } else {
            if (lightsurf->minlightMottle) {
                value += Mottle(surf_sample.point);
            }
            hit = Light_ClampMin(sample, value, color) || hit;
        }
    }

    if (hit) {
        Lightmap_Save(bsp, lightmaps, lightsurf, lightmap, style);
    }
}

static void LightFace_LocalMin(
    const mbsp_t *bsp, const mface_t *face, lightsurf_t *lightsurf, lightmapdict_t *lightmaps)
{
    const settings::worldspawn_keys &cfg = *lightsurf->cfg;
    const modelinfo_t *modelinfo = lightsurf->modelinfo;

    const surfflags_t &extended_flags = extended_texinfo_flags[face->texinfo];
    if (extended_flags.no_minlight) {
        return; /* this face is excluded from minlight */
    }

    // FIXME: Refactor this?
    if (lightsurf->modelinfo->lightignore.value() || extended_flags.light_ignore)
        return;

    /* Cast rays for local minlight entities */
    for (const auto &entity : GetLights()) {
        if (entity->getFormula() != LF_LOCALMIN) {
            continue;
        }
        if (entity->nostaticlight.value()) {
            continue;
        }

        if (CullLight(entity.get(), lightsurf)) {
            continue;
        }

        raystream_occlusion_t &rs = *lightsurf->occlusion_stream;
        rs.clearPushedRays();

        lightmap_t *lightmap = Lightmap_ForStyle(lightmaps, entity->style.value(), lightsurf);

        bool hit = false;
        for (int i = 0; i < lightsurf->samples.size(); i++) {
            const auto &surf_sample = lightsurf->samples[i];

            if (surf_sample.occluded)
                continue;

            const lightsample_t &sample = lightmap->samples[i];
            const qvec3d &surfpoint = surf_sample.point;
            if (cfg.addminlight.value() || LightSample_Brightness(sample.color) < entity->light.value()) {
                qvec3d surfpointToLightDir;
                const vec_t surfpointToLightDist = GetDir(surfpoint, entity->origin.value(), surfpointToLightDir);

                rs.pushRay(i, surfpoint, surfpointToLightDir, surfpointToLightDist);
            }
        }

        // local minlight just needs occlusion, not closest hit
        rs.tracePushedRaysOcclusion(modelinfo, CHANNEL_MASK_DEFAULT);
        total_light_rays += rs.numPushedRays();

        const int N = rs.numPushedRays();
        for (int j = 0; j < N; j++) {
            if (rs.getPushedRayOccluded(j)) {
                continue;
            }

            int i = rs.getPushedRayPointIndex(j);
            vec_t value = entity->light.value();
            lightsample_t &sample = lightmap->samples[i];

            value *= Dirt_GetScaleFactor(
                cfg, lightsurf->samples[i].occlusion, entity.get(), 0.0 /* TODO: pass distance */, lightsurf);
            if (cfg.addminlight.value()) {
                sample.color += entity->color.value() * (value / 255.0);
                hit = true;
            } else {
                hit = Light_ClampMin(sample, value, entity->color.value()) || hit;
            }

            total_light_ray_hits++;
        }

        if (hit) {
            Lightmap_Save(bsp, lightmaps, lightsurf, lightmap, entity->style.value());
        }
    }
}

static void LightFace_AutoMin(const mbsp_t *bsp, const mface_t *face, lightsurf_t *lightsurf, lightmapdict_t *lightmaps)
{
    const settings::worldspawn_keys &cfg = *lightsurf->cfg;
    const modelinfo_t *modelinfo = lightsurf->modelinfo;

    if (!modelinfo)
        return;

    /**
     * if true, apply to all luxels, not just occluded ones
     */
    bool apply_to_all = false;

    const bool any_occluded = std::any_of(lightsurf->samples.begin(), lightsurf->samples.end(),
        [](const lightsurf_t::sample_data_t &v) { return v.occluded; });

    if (!modelinfo->autominlight.is_changed()) {
        // default: apply autominlight to occluded luxels only
        if (!any_occluded)
            return;
    } else if (!modelinfo->autominlight.value()) {
        // force off
        return;
    } else {
        // force on
        apply_to_all = true;
    }

    // we are going to apply at least some

    qvec3f center = (modelinfo->model->mins + modelinfo->model->maxs) / 2;
    if (!modelinfo->autominlight_target.value().empty()) {
        for (auto &entity : GetEntdicts()) {
            if (entity.get("targetname") == modelinfo->autominlight_target.value()) {
                qvec3d point{};
                entity.get_vector("origin", point);
                center = point;
                break;
            }
        }
    }

    auto [grid_samples, occluded] = FixPointAndCalcLightgrid(bsp, center);

    if (!occluded) {
        // process each of the captured styles
        for (const auto &grid_sample : grid_samples.samples_by_style) {
            if (!grid_sample.used)
                break;

            lightmap_t *lightmap = Lightmap_ForStyle(lightmaps, grid_sample.style, lightsurf);

            // for each luxel (or only occluded luxels, depending on the setting),
            // apply the minlight
            for (int i = 0; i < lightsurf->samples.size(); i++) {
                if (apply_to_all || lightsurf->samples[i].occluded) {
                    lightmap->samples[i].color = qv::max(qvec3f{grid_sample.color}, lightmap->samples[i].color);
                }
            }

            Lightmap_Save(bsp, lightmaps, lightsurf, lightmap, grid_sample.style);
        }

        // clear occluded state, since we filled in all occluded samples with a color
        for (int i = 0; i < lightsurf->samples.size(); i++) {
            lightsurf->samples[i].occluded = false;
        }
    }
}

/*
 * =============
 * LightFace_DirtDebug
 * =============
 */
static void LightFace_DirtDebug(const mbsp_t *bsp, const lightsurf_t *lightsurf, lightmapdict_t *lightmaps)
{
    const settings::worldspawn_keys &cfg = *lightsurf->cfg;
    /* use a style 0 light map */
    lightmap_t *lightmap = Lightmap_ForStyle(lightmaps, 0, lightsurf);

    /* Overwrite each point with the dirt value for that sample... */
    for (int i = 0; i < lightsurf->samples.size(); i++) {
        lightsample_t &sample = lightmap->samples[i];
        const float light = 255 * Dirt_GetScaleFactor(cfg, lightsurf->samples[i].occlusion, nullptr, 0.0, lightsurf);
        sample.color = {light};
    }

    Lightmap_Save(bsp, lightmaps, lightsurf, lightmap, 0);
}

/*
 * =============
 * LightFace_PhongDebug
 * =============
 */
static void LightFace_PhongDebug(const mbsp_t *bsp, const lightsurf_t *lightsurf, lightmapdict_t *lightmaps)
{
    /* use a style 0 light map */
    lightmap_t *lightmap = Lightmap_ForStyle(lightmaps, 0, lightsurf);

    /* Overwrite each point with the normal for that sample... */
    for (int i = 0; i < lightsurf->samples.size(); i++) {
        lightsample_t &sample = lightmap->samples[i];
        // scale from [-1..1] to [0..1], then multiply by 255
        sample.color = lightsurf->samples[i].normal;

        for (auto &v : sample.color) {
            v = std::abs(v) * 255;
        }
    }

    Lightmap_Save(bsp, lightmaps, lightsurf, lightmap, 0);
}

static void LightFace_DebugMottle(const mbsp_t *bsp, const lightsurf_t *lightsurf, lightmapdict_t *lightmaps)
{
    /* use a style 0 light map */
    lightmap_t *lightmap = Lightmap_ForStyle(lightmaps, 0, lightsurf);

    /* Overwrite each point with the mottle noise for that sample... */
    for (int i = 0; i < lightsurf->samples.size(); i++) {
        lightsample_t &sample = lightmap->samples[i];
        // mottle is meant to be applied on top of minlight, so add some here
        // for preview purposes.
        const float minlight = 20.0f;
        sample.color = qvec3f(minlight + Mottle(lightsurf->samples[i].point));
    }

    Lightmap_Save(bsp, lightmaps, lightsurf, lightmap, 0);
}

// mxd. Surface light falloff. Returns color in [0,255]
constexpr qvec3f SurfaceLight_ColorAtDist(const settings::worldspawn_keys &cfg, const float &surf_scale,
    const float &intensity, const qvec3d &color, const float &dist, const float &hotspot_clamp)
{
    // Exponential falloff
    const float d = std::max(dist, hotspot_clamp); // Clamp away hotspots, also avoid division by 0...
    const float scaledintensity = intensity * surf_scale;
    const float scale = (1.0f / (d * d));

    return color * scaledintensity * scale;
}

// dir: vpl -> sample point direction
// mxd. returns color in [0,255]
inline qvec3f GetSurfaceLighting(const settings::worldspawn_keys &cfg, const surfacelight_t &vpl,
    const surfacelight_t::per_style_t &vpl_settings, const qvec3f &dir, float dist, const qvec3f &normal,
    bool use_normal, const vec_t &standard_scale, const vec_t &sky_scale, const float &hotspot_clamp)
{
    qvec3f result;
    float dotProductFactor = 1.0f;

    float dp1 = qv::dot(vpl.surfnormal, dir);
    const qvec3f sp_vpl = dir * -1.0f;
    float dp2 = use_normal ? qv::dot(sp_vpl, normal) : 1.0f;

    if (!vpl_settings.omnidirectional) {
        if (dp1 < -LIGHT_ANGLE_EPSILON)
            return {0}; // sample point behind vpl
        if (dp2 < -LIGHT_ANGLE_EPSILON)
            return {0}; // vpl behind sample face

        // Rescale a bit to brighten the faces nearly-perpendicular to the surface light plane...
        if (vpl_settings.rescale) {
            dp1 = 0.5f + dp1 * 0.5f;
            dp2 = 0.5f + dp2 * 0.5f;
        }

        dotProductFactor = dp1 * dp2;
    } else {
        // used for sky face surface lights
        dotProductFactor = dp2 * 0.5f;
    }

    dotProductFactor = std::max(0.0f, dotProductFactor);

    // quick exit
    if (!dotProductFactor) {
        return {0};
    }

    if (vpl_settings.omnidirectional) {
        dist += cfg.surflightskydist.value();
    }

    // Get light contribution
    result = SurfaceLight_ColorAtDist(cfg, vpl_settings.omnidirectional ? sky_scale : standard_scale,
        vpl_settings.intensity, vpl_settings.color, dist, hotspot_clamp);

    // Apply angle scale
    const qvec3f resultscaled = result * dotProductFactor;

    //Q_assert(!std::isnan(resultscaled[0]) && !std::isnan(resultscaled[1]) && !std::isnan(resultscaled[2]));
    return resultscaled;
}

static bool // mxd
SurfaceLight_SphereCull(const surfacelight_t *vpl, const lightsurf_t *lightsurf,
    const surfacelight_t::per_style_t &vpl_settings, const vec_t &bouncelight_gate, const float &hotspot_clamp)
{
    if (light_options.visapprox.value() == visapprox_t::RAYS &&
        vpl->bounds.disjoint(lightsurf->extents.bounds, 0.001)) {
        return true;
    }

    const settings::worldspawn_keys &cfg = *lightsurf->cfg;
    const qvec3f dir = qvec3f(lightsurf->extents.origin) - qvec3f(vpl->pos); // vpl -> sample point
    const float dist = qv::length(dir) + lightsurf->extents.radius;

    // Get light contribution
    const qvec3f color = SurfaceLight_ColorAtDist(cfg,
        vpl_settings.omnidirectional ? cfg.surflightskyscale.value() : cfg.surflightscale.value(),
        vpl_settings.totalintensity, vpl_settings.color, dist, hotspot_clamp);

    return qv::gate(color, (float)bouncelight_gate);
}

static void // mxd
LightFace_SurfaceLight(const mbsp_t *bsp, lightsurf_t *lightsurf, lightmapdict_t *lightmaps, std::optional<size_t> bounce_depth,
    const vec_t &standard_scale, const vec_t &sky_scale, const float &hotspot_clamp)
{
    const settings::worldspawn_keys &cfg = *lightsurf->cfg;
    const float surflight_gate = 0.01f;

    // check lighting channels (currently surface lights are always on CHANNEL_MASK_DEFAULT)
    if (!(lightsurf->object_channel_mask & CHANNEL_MASK_DEFAULT)) {
        return;
    }

    for (const auto &surf_ptr : LightSurfaces()) {

        if (!surf_ptr || !surf_ptr->vpl) {
            // didn't emit anthing
            continue;
        }

        auto &vpl = *surf_ptr->vpl.get();

        for (const auto &vpl_setting : surf_ptr->vpl->styles) {

            if (vpl_setting.bounce_level != bounce_depth)
                continue;
            else if (SurfaceLight_SphereCull(&vpl, lightsurf, vpl_setting, surflight_gate, hotspot_clamp))
                continue;

            raystream_occlusion_t &rs = *lightsurf->occlusion_stream;

            for (int c = 0; c < vpl.points.size(); c++) {
                if (light_options.visapprox.value() == visapprox_t::VIS &&
                    VisCullEntity(bsp, lightsurf->pvs, vpl.leaves[c])) {
                    continue;
                }

                rs.clearPushedRays();

                for (int i = 0; i < lightsurf->samples.size(); i++) {
                    const auto &sample = lightsurf->samples[i];

                    if (sample.occluded)
                        continue;

                    const qvec3d &lightsurf_pos = sample.point;
                    const qvec3d &lightsurf_normal = sample.normal;

                    const qvec3f &pos = vpl.points[c];
                    qvec3f dir = lightsurf_pos - pos;
                    float dist = std::max(0.01f, qv::length(dir));
                    bool use_normal = true;

                    if (lightsurf->twosided) {
                        use_normal = false;
                        dir /= dist;
                    } else if (dist == 0.0f) {
                        dir = lightsurf_normal;
                        use_normal = false;
                    } else {
                        dir /= dist;
                    }

                    const qvec3f indirect = GetSurfaceLighting(cfg, vpl, vpl_setting, dir, dist, lightsurf_normal,
                        use_normal, standard_scale, sky_scale, hotspot_clamp);
                    if (!qv::gate(indirect, surflight_gate)) { // Each point contributes very little to the final result
                        rs.pushRay(i, pos, dir, dist, &indirect);
                    }
                }

                if (!rs.numPushedRays())
                    continue;

                total_surflight_rays += rs.numPushedRays();
                rs.tracePushedRaysOcclusion(lightsurf->modelinfo, CHANNEL_MASK_DEFAULT);

                const int lightmapstyle = vpl_setting.style;
                lightmap_t *lightmap = Lightmap_ForStyle(lightmaps, lightmapstyle, lightsurf);

                bool hit = false;
                const int numrays = rs.numPushedRays();
                for (int j = 0; j < numrays; j++) {
                    if (rs.getPushedRayOccluded(j))
                        continue;

                    const int i = rs.getPushedRayPointIndex(j);
                    qvec3f indirect = rs.getPushedRayColor(j);

                    //Q_assert(!std::isnan(indirect[0]));

                    // Use dirt scaling on the surface lighting.
                    const vec_t dirtscale =
                        Dirt_GetScaleFactor(cfg, lightsurf->samples[i].occlusion, nullptr, 0.0, lightsurf);
                    indirect *= dirtscale;

                    lightsample_t &sample = lightmap->samples[i];
                    sample.color += indirect;
                    lightmap->bounce_color += indirect;

                    hit = true;
                    ++total_surflight_ray_hits;
                }

                // If surface light contributed anything, save.
                if (hit)
                    Lightmap_Save(bsp, lightmaps, lightsurf, lightmap, lightmapstyle);
            }
        }
    }
}

static void // mxd
LightPoint_SurfaceLight(const mbsp_t *bsp, const std::vector<uint8_t> *pvs, raystream_occlusion_t &rs, bool bounce,
    const vec_t &standard_scale, const vec_t &sky_scale, const float &hotspot_clamp, const qvec3d &surfpoint,
    lightgrid_samples_t &result)
{
    const settings::worldspawn_keys &cfg = light_options;
    const float surflight_gate = 0.01f;

    for (const auto &surf : LightSurfaces()) {
        if (!surf || !surf->vpl) {
            continue;
        }

        const surfacelight_t &vpl = *surf->vpl;

        for (int c = 0; c < vpl.points.size(); c++) {
            if (light_options.visapprox.value() == visapprox_t::VIS && pvs && VisCullEntity(bsp, *pvs, vpl.leaves[c])) {
                continue;
            }

            // 1 ray
            for (auto &vpl_settings : vpl.styles) {
                if (vpl_settings.bounce_level.has_value() != bounce)
                    continue;

                qvec3f pos = vpl.points[c];
                qvec3f dir = surfpoint - pos;
                float dist = qv::length(dir);

                if (dist == 0.0f)
                    dir = {0, 0, 1};
                else
                    dir /= dist;

                qvec3f indirect{};

                rs.clearPushedRays();

                for (int axis = 0; axis < 3; ++axis) {
                    for (int sign = -1; sign <= +1; sign += 2) {

                        qvec3f cube_color;

                        qvec3f cube_normal{};
                        cube_normal[axis] = sign;

                        cube_color = GetSurfaceLighting(cfg, vpl, vpl_settings, dir, dist, cube_normal, true,
                            standard_scale, sky_scale, hotspot_clamp);

#ifdef LIGHTPOINT_TAKE_MAX
                        if (qv::length2(cube_color) > qv::length2(indirect)) {
                            indirect = cube_color;
                        }
#else
                        indirect += cube_color / 6.0;
#endif
                    }
                }

                if (!qv::gate(indirect, surflight_gate)) { // Each point contributes very little to the final result
                    rs.pushRay(0, pos, dir, dist, &indirect);
                }

                if (!rs.numPushedRays())
                    continue;

                rs.tracePushedRaysOcclusion(nullptr, CHANNEL_MASK_DEFAULT);

                const int numrays = rs.numPushedRays();
                for (int j = 0; j < numrays; j++) {
                    if (rs.getPushedRayOccluded(j))
                        continue;

                    qvec3f indirect = rs.getPushedRayColor(j);

                    //Q_assert(!std::isnan(indirect[0]));

                    result.add(indirect, vpl_settings.style);
                }
            }
        }
    }
}

static void LightFace_OccludedDebug(const mbsp_t *bsp, lightsurf_t *lightsurf, lightmapdict_t *lightmaps)
{
    Q_assert(light_options.debugmode == debugmodes::debugoccluded);

    /* use a style 0 light map */
    lightmap_t *lightmap = Lightmap_ForStyle(lightmaps, 0, lightsurf);

    /* Overwrite each point, red=occluded, green=ok */
    for (int i = 0; i < lightsurf->samples.size(); i++) {
        auto &surf_sample = lightsurf->samples[i];
        lightsample_t &sample = lightmap->samples[i];
        if (surf_sample.occluded) {
            sample.color = {255, 0, 0};
        } else {
            sample.color = {0, 255, 0};
        }
        // N.B.: Mark it as un-occluded now, to disable special handling later in the -extra/-extra4 downscaling code
        surf_sample.occluded = false;
    }

    Lightmap_Save(bsp, lightmaps, lightsurf, lightmap, 0);
}

static void LightFace_DebugNeighbours(const mbsp_t *bsp, lightsurf_t *lightsurf, lightmapdict_t *lightmaps)
{
    Q_assert(light_options.debugmode == debugmodes::debugneighbours);

    /* use a style 0 light map */
    lightmap_t *lightmap = Lightmap_ForStyle(lightmaps, 0, lightsurf);

    //    std::vector<neighbour_t> neighbours = NeighbouringFaces_new(lightsurf->bsp, BSP_GetFace(lightsurf->bsp,
    //    dump_facenum)); bool found = false; for (auto &f : neighbours) {
    //        if (f.face == lightsurf->face)
    //            found = true;
    //    }

    bool has_sample_on_dumpface = false;
    for (int i = 0; i < lightsurf->samples.size(); i++) {
        if (lightsurf->samples[i].realfacenum == dump_facenum) {
            has_sample_on_dumpface = true;
            break;
        }
    }

    /* Overwrite each point */
    for (int i = 0; i < lightsurf->samples.size(); i++) {
        lightsample_t &sample = lightmap->samples[i];
        const int &sample_face = lightsurf->samples[i].realfacenum;

        if (sample_face == dump_facenum) {
            /* Red - the sample is on the selected face */
            sample.color = {255, 0, 0};
        } else if (has_sample_on_dumpface) {
            /* Green - the face has some samples on the selected face */
            sample.color = {0, 255, 0};
        } else {
            sample.color = {};
        }
        // N.B.: Mark it as un-occluded now, to disable special handling later in the -extra/-extra4 downscaling code
        lightsurf->samples[i].occluded = false;
    }

    Lightmap_Save(bsp, lightmaps, lightsurf, lightmap, 0);
}

/* Dirtmapping borrowed from q3map2, originally by RaP7oR */

constexpr size_t DIRT_NUM_ANGLE_STEPS = 16;
constexpr size_t DIRT_NUM_ELEVATION_STEPS = 3;
constexpr size_t DIRT_NUM_VECTORS = (DIRT_NUM_ANGLE_STEPS * DIRT_NUM_ELEVATION_STEPS);

static qvec3d dirtVectors[DIRT_NUM_VECTORS];
int numDirtVectors = 0;

/*
 * ============
 * SetupDirt
 *
 * sets up dirtmap (ambient occlusion)
 * ============
 */
void SetupDirt(settings::worldspawn_keys &cfg)
{
    // check if needed

    if (!cfg.dirt.value() && cfg.dirt.get_source() == settings::source::COMMANDLINE) {
        // HACK: "-dirt 0" disables all dirtmapping even if we would otherwise use it.
        dirt_in_use = false;
        return;
    }

    if (cfg.dirt.value() || cfg.minlight_dirt.value() || cfg.sunlight_dirt.boolValue() ||
        cfg.sunlight2_dirt.boolValue()) {
        dirt_in_use = true;
    }

    if (!dirt_in_use) {
        // check entities, maybe only a few lights use it
        for (const auto &light : GetLights()) {
            if (light->dirt.boolValue()) {
                dirt_in_use = true;
                break;
            }
        }
    }

    if (!dirt_in_use) {
        // dirtmapping is not used by this map.
        return;
    }

    /* note it */
    logging::funcheader();

    /* calculate angular steps */
    constexpr float angleStep = (float)DEG2RAD(360.0f / DIRT_NUM_ANGLE_STEPS);
    const float elevationStep = (float)DEG2RAD(cfg.dirtangle.value() / DIRT_NUM_ELEVATION_STEPS);

    /* iterate angle */
    float angle = 0.0f;
    numDirtVectors = 0;
    for (int i = 0; i < DIRT_NUM_ANGLE_STEPS; i++, angle += angleStep) {
        /* iterate elevation */
        float elevation = elevationStep * 0.5f;
        for (int j = 0; j < DIRT_NUM_ELEVATION_STEPS; j++, elevation += elevationStep) {
            dirtVectors[numDirtVectors][0] = sin(elevation) * cos(angle);
            dirtVectors[numDirtVectors][1] = sin(elevation) * sin(angle);
            dirtVectors[numDirtVectors][2] = cos(elevation);
            numDirtVectors++;
        }
    }

    /* emit some statistics */
    logging::print("{:9} dirtmap vectors\n", numDirtVectors);
}

// from q3map2
inline qvec3d TransformToTangentSpace(
    const qvec3d &normal, const qvec3d &myUp, const qvec3d &myRt, const qvec3d &inputvec)
{
    return myRt * inputvec[0] + myUp * inputvec[1] + normal * inputvec[2];
}

// from q3map2
inline qvec3d GetDirtVector(const settings::worldspawn_keys &cfg, int i)
{
    Q_assert(i < numDirtVectors);

    if (cfg.dirtmode.value() == 1) {
        /* get random vector */
        float angle = Random() * DEG2RAD(360.0f);
        float elevation = Random() * DEG2RAD(cfg.dirtangle.value());
        return {cos(angle) * sin(elevation), sin(angle) * sin(elevation), cos(elevation)};
    }

    return dirtVectors[i];
}

/*
 * ============
 * LightFace_CalculateDirt
 * ============
 */
static void LightFace_CalculateDirt(lightsurf_t *lightsurf)
{
    const settings::worldspawn_keys &cfg = *lightsurf->cfg;

    Q_assert(dirt_in_use);

    // batch implementation:

    thread_local static std::vector<qvec3d> myUps, myRts;

    myUps.resize(lightsurf->samples.size());
    myRts.resize(lightsurf->samples.size());

    // init
    for (int i = 0; i < lightsurf->samples.size(); i++) {
        lightsurf->samples[i].occlusion = 0;
    }

    // this stuff is just per-point
    for (int i = 0; i < lightsurf->samples.size(); i++) {
        const auto [tangent, bitangent] = qv::MakeTangentAndBitangentUnnormalized(lightsurf->samples[i].normal);

        myUps[i] = qv::normalize(tangent);
        myRts[i] = qv::normalize(bitangent);
    }

    for (int j = 0; j < numDirtVectors; j++) {
        raystream_intersection_t &rs = *lightsurf->intersection_stream;
        rs.clearPushedRays();

        // fill in input buffers

        for (int i = 0; i < lightsurf->samples.size(); i++) {
            const auto &sample = lightsurf->samples[i];

            if (sample.occluded)
                continue;

            qvec3d dirtvec = GetDirtVector(cfg, j);
            qvec3d dir = TransformToTangentSpace(sample.normal, myUps[i], myRts[i], dirtvec);

            rs.pushRay(i, sample.point, dir, cfg.dirtdepth.value());
        }

        // trace the batch. need closest hit for dirt, so intersection.
        //
        // use the model's own channel mask as the shadow mask, e.g. so a model in channel 2's AO rays will only hit
        // other things in channel 2
        rs.tracePushedRaysIntersection(lightsurf->modelinfo, lightsurf->object_channel_mask);

        // accumulate hitdists
        for (int k = 0; k < rs.numPushedRays(); k++) {
            const int i = rs.getPushedRayPointIndex(k);
            if (rs.getPushedRayHitType(k) == hittype_t::SOLID) {
                vec_t dist = rs.getPushedRayHitDist(k);
                lightsurf->samples[i].occlusion += std::min(cfg.dirtdepth.value(), dist);
            } else {
                lightsurf->samples[i].occlusion += cfg.dirtdepth.value();
            }
        }
    }

    // process the results.
    for (int i = 0; i < lightsurf->samples.size(); i++) {
        vec_t avgHitdist = lightsurf->samples[i].occlusion / (float)numDirtVectors;
        lightsurf->samples[i].occlusion = 1.0 - (avgHitdist / cfg.dirtdepth.value());
    }
}

// clamps negative values. applies gamma and rangescale. clamps values over 255
// N.B. we want to do this before smoothing / downscaling, so huge values don't mess up the averaging.
inline void LightFace_ScaleAndClamp(lightsurf_t *lightsurf)
{
    const settings::worldspawn_keys &cfg = *lightsurf->cfg;

    for (lightmap_t &lightmap : lightsurf->lightmapsByStyle) {
        for (int i = 0; i < lightsurf->samples.size(); i++) {
            qvec3f &color = lightmap.samples[i].color;

            /* Fix any negative values */
            color = qv::max(color, {0});

            // before any other scaling, apply maxlight
            if (lightsurf->maxlight || cfg.maxlight.value()) {
                vec_t maxcolor = qv::max(color);
                // FIXME: for colored lighting, this doesn't seem to generate the right values...
                vec_t maxval = (lightsurf->maxlight ? lightsurf->maxlight : cfg.maxlight.value()) * 2.0;

                if (maxcolor > maxval) {
                    color *= (maxval / maxcolor);
                }
            }

            // color scaling
            if (lightsurf->lightcolorscale != 1.0) {
                qvec3f grayscale{qv::max(color)};
                color = mix(grayscale, color, lightsurf->lightcolorscale);
            }

            /* Scale and handle gamma adjustment */
            color *= cfg.rangescale.value();

            if (cfg.lightmapgamma.value() != 1.0) {
                for (auto &c : color) {
                    c = pow(c / 255.0f, 1.0 / cfg.lightmapgamma.value()) * 255.0f;
                }
            }

            // clamp
            // FIXME: should this be a brightness clamp?
            vec_t maxcolor = qv::max(color);

            if (maxcolor > 255.0) {
                color *= (255.0 / maxcolor);
            }
        }
    }
}

/**
 * Same as above LightFace_ScaleAndClamp, but for lightgrid
 * TODO: share code with above
 */
static void LightPoint_ScaleAndClamp(qvec3d &color)
{
    const settings::worldspawn_keys &cfg = light_options;

    /* Fix any negative values */
    color = qv::max(color, {0});

    // before any other scaling, apply maxlight
    if (cfg.maxlight.value()) {
        vec_t maxcolor = qv::max(color);
        // FIXME: for colored lighting, this doesn't seem to generate the right values...
        vec_t maxval = cfg.maxlight.value() * 2.0;

        if (maxcolor > maxval) {
            color *= (maxval / maxcolor);
        }
    }

    /* Scale and handle gamma adjustment */
    color *= cfg.rangescale.value();

    if (cfg.lightmapgamma.value() != 1.0) {
        for (auto &c : color) {
            c = pow(c / 255.0f, 1.0 / cfg.lightmapgamma.value()) * 255.0f;
        }
    }

    // clamp
    // FIXME: should this be a brightness clamp?
    vec_t maxcolor = qv::max(color);

    if (maxcolor > 255.0) {
        color *= (255.0 / maxcolor);
    }
}

static void LightPoint_ScaleAndClamp(lightgrid_samples_t &result)
{
    for (auto &sample : result.samples_by_style) {
        if (sample.used) {
            LightPoint_ScaleAndClamp(sample.color);
        }
    }
}

void FinishLightmapSurface(const mbsp_t *bsp, lightsurf_t *lightsurf)
{
    /* Apply gamma, rangescale, and clamp */
    LightFace_ScaleAndClamp(lightsurf);
}

static float Lightmap_AvgBrightness(const lightmap_t *lm, const lightsurf_t *lightsurf)
{
    float avgb = 0;
    for (int j = 0; j < lightsurf->samples.size(); j++) {
        avgb += LightSample_Brightness(lm->samples[j].color);
    }
    avgb /= lightsurf->samples.size();
    return avgb;
}

static float Lightmap_MaxBrightness(const lightmap_t *lm, const lightsurf_t *lightsurf)
{
    float maxb = 0;
    for (int j = 0; j < lightsurf->samples.size(); j++) {
        const float b = LightSample_Brightness(lm->samples[j].color);
        if (b > maxb) {
            maxb = b;
        }
    }
    return maxb;
}

#if 0
static void WritePPM(const fs::path &fname, int width, int height, const uint8_t *rgbdata)
{
    qfile_t file = SafeOpenWrite(fname);

    // see: http://netpbm.sourceforge.net/doc/ppm.html
    ewt::print(file.get(), "P6 {} {} 255 ", width, height);
    int bytes = width * height * 3;
    Q_assert(bytes == SafeWrite(file, rgbdata, bytes));
}

static void DumpFullSizeLightmap(const mbsp_t *bsp, const lightsurf_t *lightsurf)
{
    const lightmap_t *lm = Lightmap_ForStyle_ReadOnly(lightsurf, 0);
    if (lm != nullptr) {
        int fnum = Face_GetNum(bsp, lightsurf->face);

        std::vector<uint8_t> rgbdata;
        for (int i = 0; i < lightsurf->numpoints; i++) {
            const qvec3d &color = lm->samples[i].color;
            for (int j = 0; j < 3; j++) {
                int intval = static_cast<int>(clamp(color[j], 0.0, 255.0));
                rgbdata.push_back(static_cast<uint8_t>(intval));
            }
        }

        const int oversampled_width = (lightsurf->texsize[0] + 1) * settings::extra.value();
        const int oversampled_height = (lightsurf->texsize[1] + 1) * settings::extra.value();

        Q_assert(lightsurf->numpoints == (oversampled_height * oversampled_width));

        WritePPM(fmt::format("face{:04}.ppm", fnum), oversampled_width, oversampled_height, rgbdata.data());
    }
}

static void DumpGLMVector(std::string fname, std::vector<qvec3f> vec, int width, int height)
{
    std::vector<uint8_t> rgbdata;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const qvec3f &sample = vec.at((y * width) + x);
            for (int j = 0; j < 3; j++) {
                int intval = static_cast<int>(clamp(sample[j], 0.0f, 255.0f));
                rgbdata.push_back(static_cast<uint8_t>(intval));
            }
        }
    }
    Q_assert(rgbdata.size() == (width * height * 3));
    WritePPM(fname, width, height, rgbdata.data());
}

static void DumpDownscaledLightmap(const mbsp_t *bsp, const mface_t *face, int w, int h, const qvec3d *colors)
{
    int fnum = Face_GetNum(bsp, face);

    std::vector<uint8_t> rgbdata;
    for (int i = 0; i < (w * h); i++) {
        for (int j = 0; j < 3; j++) {
            int intval = static_cast<int>(clamp(colors[i][j], 0.0, 255.0));
            rgbdata.push_back(static_cast<uint8_t>(intval));
        }
    }

    WritePPM(fmt::format("face-small{:04}.ppm", fnum), w, h, rgbdata.data());
}
#endif

static std::vector<qvec4f> LightmapColorsToGLMVector(const lightsurf_t *lightsurf, const lightmap_t *lm)
{
    std::vector<qvec4f> res;
    for (int i = 0; i < lightsurf->samples.size(); i++) {
        const qvec3d &color = lm->samples[i].color;
        const float alpha = lightsurf->samples[i].occluded ? 0.0f : 1.0f;
        res.emplace_back(color[0], color[1], color[2], alpha);
    }
    return res;
}

static std::vector<qvec4f> LightmapNormalsToGLMVector(const lightsurf_t *lightsurf, const lightmap_t *lm)
{
    std::vector<qvec4f> res;
    for (int i = 0; i < lightsurf->samples.size(); i++) {
        const qvec3d &color = lm->samples[i].direction;
        const float alpha = lightsurf->samples[i].occluded ? 0.0f : 1.0f;
        res.emplace_back(color[0], color[1], color[2], alpha);
    }
    return res;
}

// Special handling of alpha channel:
// - "alpha channel" is expected to be 0 or 1. This gets set to 0 if the sample
// point is occluded (bmodel sticking outside of the world, or inside a shadow-
// casting bmodel that is overlapping a world face), otherwise it's 1.
//
// - If alpha is 0 the sample doesn't contribute to the filter kernel.
// - If all the samples in the filter kernel have alpha=0, write a sample with alpha=0
//   (but still average the colors, important so that minlight still works properly
//    for bmodels that go outside of the world).
static std::vector<qvec4f> IntegerDownsampleImage(const std::vector<qvec4f> &input, int w, int h, int factor)
{
    Q_assert(factor >= 1);
    if (factor == 1)
        return input;

    const int outw = w / factor;
    const int outh = h / factor;

    std::vector<qvec4f> res(static_cast<size_t>(outw * outh));

    for (int y = 0; y < outh; y++) {
        for (int x = 0; x < outw; x++) {

            float totalWeight = 0.0f;
            qvec3f totalColor{};

            // These are only used if all the samples in the kernel have alpha = 0
            float totalWeightIgnoringOcclusion = 0.0f;
            qvec3f totalColorIgnoringOcclusion{};

            const int extraradius = 0;
            const int kernelextent = factor + (2 * extraradius);

            for (int y0 = 0; y0 < kernelextent; y0++) {
                for (int x0 = 0; x0 < kernelextent; x0++) {
                    const int x1 = (x * factor) - extraradius + x0;
                    const int y1 = (y * factor) - extraradius + y0;

                    // check if the kernel goes outside of the source image
                    if (x1 < 0 || x1 >= w)
                        continue;
                    if (y1 < 0 || y1 >= h)
                        continue;

                    // read the input sample
                    const float weight = 1.0f;
                    const qvec4f &inSample = input.at((y1 * w) + x1);

                    totalColorIgnoringOcclusion += qvec3f(inSample) * weight;
                    totalWeightIgnoringOcclusion += weight;

                    // Occluded sample points don't contribute to the filter
                    if (inSample[3] == 0.0f)
                        continue;

                    totalColor += qvec3f(inSample) * weight;
                    totalWeight += weight;
                }
            }

            const int outIndex = (y * outw) + x;
            if (totalWeight > 0.0f) {
                const qvec3f tmp = totalColor / totalWeight;
                const qvec4f resultColor = qvec4f(tmp[0], tmp[1], tmp[2], 1.0f);
                res[outIndex] = resultColor;
            } else {
                const qvec3f tmp = totalColorIgnoringOcclusion / totalWeightIgnoringOcclusion;
                const qvec4f resultColor = qvec4f(tmp[0], tmp[1], tmp[2], 0.0f);
                res[outIndex] = resultColor;
            }
        }
    }

    return res;
}

static std::vector<qvec4f> FloodFillTransparent(const std::vector<qvec4f> &input, int w, int h)
{
    // transparent pixels take the average of their neighbours.

    std::vector<qvec4f> res(input);

    while (1) {
        int unhandled_pixels = 0;

        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                const int i = (y * w) + x;
                const qvec4f &inSample = res.at(i);

                if (inSample[3] == 0) {
                    // average the neighbouring non-transparent samples

                    int opaque_neighbours = 0;
                    qvec3f neighbours_sum{};
                    for (int y0 = -1; y0 <= 1; y0++) {
                        for (int x0 = -1; x0 <= 1; x0++) {
                            const int x1 = x + x0;
                            const int y1 = y + y0;

                            if (x1 < 0 || x1 >= w)
                                continue;
                            if (y1 < 0 || y1 >= h)
                                continue;

                            const qvec4f neighbourSample = res.at((y1 * w) + x1);
                            if (neighbourSample[3] == 1) {
                                opaque_neighbours++;
                                neighbours_sum += qvec3f(neighbourSample);
                            }
                        }
                    }

                    if (opaque_neighbours > 0) {
                        neighbours_sum *= (1.0f / (float)opaque_neighbours);
                        res.at(i) = qvec4f(neighbours_sum[0], neighbours_sum[1], neighbours_sum[2], 1.0f);

                        // this sample is now opaque
                    } else {
                        unhandled_pixels++;

                        // all neighbours are transparent. need to perform more iterations (or the whole lightmap is
                        // transparent).
                    }
                }
            }
        }

        if (unhandled_pixels == input.size()) {
            // logging::funcprint("warning, fully transparent lightmap\n");
            fully_transparent_lightmaps++;
            break;
        }

        if (unhandled_pixels == 0)
            break; // all done
    }

    return res;
}

static std::vector<qvec4f> HighlightSeams(const std::vector<qvec4f> &input, int w, int h)
{
    std::vector<qvec4f> res(input);

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const int i = (y * w) + x;
            const qvec4f &inSample = res.at(i);

            if (inSample[3] == 0) {
                res.at(i) = qvec4f(255, 0, 0, 1);
            }
        }
    }

    return res;
}

static std::vector<qvec4f> BoxBlurImage(const std::vector<qvec4f> &input, int w, int h, int radius)
{
    std::vector<qvec4f> res(input.size());

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {

            float totalWeight = 0.0f;
            qvec3f totalColor{};

            // These are only used if all the samples in the kernel have alpha = 0
            float totalWeightIgnoringOcclusion = 0.0f;
            qvec3f totalColorIgnoringOcclusion{};

            for (int y0 = -radius; y0 <= radius; y0++) {
                for (int x0 = -radius; x0 <= radius; x0++) {
                    const int x1 = std::clamp(x + x0, 0, w - 1);
                    const int y1 = std::clamp(y + y0, 0, h - 1);

                    // check if the kernel goes outside of the source image

                    // 2017-09-16: this is a hack, but clamping the
                    // x/y instead of discarding the samples outside of the
                    // kernel looks better in some cases:
                    // https://github.com/ericwa/ericw-tools/issues/171
#if 0
                    if (x1 < 0 || x1 >= w)
                        continue;
                    if (y1 < 0 || y1 >= h)
                        continue;
#endif

                    // read the input sample
                    const float weight = 1.0f;
                    const qvec4f &inSample = input.at((y1 * w) + x1);

                    totalColorIgnoringOcclusion += qvec3f(inSample) * weight;
                    totalWeightIgnoringOcclusion += weight;

                    // Occluded sample points don't contribute to the filter
                    if (inSample[3] == 0.0f)
                        continue;

                    totalColor += qvec3f(inSample) * weight;
                    totalWeight += weight;
                }
            }

            const int outIndex = (y * w) + x;
            if (totalWeight > 0.0f) {
                const qvec3f tmp = totalColor / totalWeight;
                const qvec4f resultColor = qvec4f(tmp[0], tmp[1], tmp[2], 1.0f);
                res[outIndex] = resultColor;
            } else {
                const qvec3f tmp = totalColorIgnoringOcclusion / totalWeightIgnoringOcclusion;
                const qvec4f resultColor = qvec4f(tmp[0], tmp[1], tmp[2], 0.0f);
                res[outIndex] = resultColor;
            }
        }
    }

    return res;
}

// check if the given face can actually store luxel data
bool Face_IsLightmapped(const mbsp_t *bsp, const mface_t *face)
{
    const mtexinfo_t *texinfo = Face_Texinfo(bsp, face);

    if (texinfo == nullptr) {
        return false;
    }

    const char *texname = Face_TextureName(bsp, face);

    return bsp->loadversion->game->surf_is_lightmapped(texinfo->flags, texname,
        light_options.q2rtx.value() &&
            bsp->loadversion->game->id == GAME_QUAKE_II, // FIXME: move to own config option. -light_nodraw?
        light_options.lightgrid.value());
}

// check if the given face can actually emit light
bool Face_IsEmissive(const mbsp_t *bsp, const mface_t *face)
{
    const mtexinfo_t *texinfo = Face_Texinfo(bsp, face);

    if (texinfo == nullptr) {
        return false;
    }

    const char *texname = Face_TextureName(bsp, face);

    return bsp->loadversion->game->surf_is_emissive(texinfo->flags, texname);
}

/**
 * - Writes (actual_width * actual_height) bytes to `out`
 * - Writes (actual_width * actual_height * 3) bytes to `lit`
 * - Writes (actual_width * actual_height * 3) bytes to `lux`
 */
static void WriteSingleLightmap(const mbsp_t *bsp, const mface_t *face, const lightsurf_t *lightsurf,
    const lightmap_t *lm, const int actual_width, const int actual_height, uint8_t *out, uint8_t *lit, uint8_t *lux,
    const faceextents_t &output_extents)
{
    const int oversampled_width = actual_width * light_options.extra.value();
    const int oversampled_height = actual_height * light_options.extra.value();

    // allocate new float buffers for the output colors and directions
    // these are the actual output width*height, without oversampling.

    std::vector<qvec4f> fullres = LightmapColorsToGLMVector(lightsurf, lm);

    if (light_options.highlightseams.value()) {
        fullres = HighlightSeams(fullres, oversampled_width, oversampled_height);
    }

    // removes all transparent pixels by averaging from adjacent pixels
    fullres = FloodFillTransparent(fullres, oversampled_width, oversampled_height);

    if (light_options.soft.value() > 0) {
        fullres = BoxBlurImage(fullres, oversampled_width, oversampled_height, light_options.soft.value());
    }

    const std::vector<qvec4f> output_color =
        IntegerDownsampleImage(fullres, oversampled_width, oversampled_height, light_options.extra.value());
    std::optional<std::vector<qvec4f>> output_dir;

    if (lux) {
        output_dir = IntegerDownsampleImage(LightmapNormalsToGLMVector(lightsurf, lm), oversampled_width,
            oversampled_height, light_options.extra.value());
    }

    // copy from the float buffers to byte buffers in .bsp / .lit / .lux
    const int output_width = output_extents.width();
    const int output_height = output_extents.height();

    for (int t = 0; t < output_height; t++) {
        for (int s = 0; s < output_width; s++) {
            const int input_sample_s = (s / (float)output_width) * actual_width;
            const int input_sample_t = (t / (float)output_height) * actual_height;
            const int sampleindex = (input_sample_t * actual_width) + input_sample_s;

            if (lit || out) {
                const qvec4f &color = output_color.at(sampleindex);

                if (lit) {
                    *lit++ = color[0];
                    *lit++ = color[1];
                    *lit++ = color[2];
                }

                if (out) {
                    /* Take the max() of the 3 components to get the value to write to the
                    .bsp lightmap. this avoids issues with some engines
                    that require the lit and internal lightmap to have the same
                    intensity. (MarkV, some QW engines)

                    This must be max(), see LightNormalize in MarkV 1036.
                    */
                    float light = std::max({color[0], color[1], color[2]});
                    if (light < 0)
                        light = 0;
                    if (light > 255)
                        light = 255;
                    *out++ = light;
                }
            }

            if (lux) {
                qvec3d direction = output_dir->at(sampleindex).xyz();
                qvec3d temp = {qv::dot(direction, lightsurf->snormal), qv::dot(direction, lightsurf->tnormal),
                    qv::dot(direction, lightsurf->plane.normal)};

                if (qv::emptyExact(temp))
                    temp = {0, 0, 1};
                else
                    qv::normalizeInPlace(temp);

                int v = (temp[0] + 1) * 128;
                *lux++ = (v > 255) ? 255 : v;
                v = (temp[1] + 1) * 128;
                *lux++ = (v > 255) ? 255 : v;
                v = (temp[2] + 1) * 128;
                *lux++ = (v > 255) ? 255 : v;
            }
        }
    }
}

/**
 * - Writes (output_width * output_height) bytes to `out`
 * - Writes (output_width * output_height * 3) bytes to `lit`
 * - Writes (output_width * output_height * 3) bytes to `lux`
 */
static void WriteSingleLightmap_FromDecoupled(const mbsp_t *bsp, const mface_t *face, const lightsurf_t *lightsurf,
    const lightmap_t *lm, const int output_width, const int output_height, uint8_t *out, uint8_t *lit, uint8_t *lux)
{
    // this is the lightmap data in the "decoupled" coordinate system
    std::vector<qvec4f> fullres = LightmapColorsToGLMVector(lightsurf, lm);

    // maps a luxel in the vanilla lightmap to the corresponding position in the decoupled lightmap
    const qmat4x4f vanillaLMToDecoupled =
        lightsurf->extents.worldToLMMatrix * lightsurf->vanilla_extents.lmToWorldMatrix;

    // samples the "decoupled" lightmap at an integer coordinate, with clamping
    auto tex = [&lightsurf, &fullres](int x, int y) -> qvec4f {
        const int x_clamped = std::clamp(x, 0, lightsurf->width - 1);
        const int y_clamped = std::clamp(y, 0, lightsurf->height - 1);

        const int sampleindex = (y_clamped * lightsurf->width) + x_clamped;
        assert(sampleindex >= 0);
        assert(sampleindex < fullres.size());

        return fullres[sampleindex];
    };

    for (int t = 0; t < output_height; t++) {
        for (int s = 0; s < output_width; s++) {
            // convert from vanilla lm coord to decoupled lm coord
            qvec2f decoupled_lm_coord = vanillaLMToDecoupled * qvec4f(s, t, 0, 1);

            decoupled_lm_coord = decoupled_lm_coord * light_options.extra.value();

            // split into integer/fractional part for bilinear interpolation
            const int coord_floor_x = (int)decoupled_lm_coord[0];
            const int coord_floor_y = (int)decoupled_lm_coord[1];

            const float coord_frac_x = decoupled_lm_coord[0] - coord_floor_x;
            const float coord_frac_y = decoupled_lm_coord[1] - coord_floor_y;

            // 2D bilinear interpolation
            const qvec4f color =
                mix(mix(tex(coord_floor_x, coord_floor_y), tex(coord_floor_x + 1, coord_floor_y), coord_frac_x),
                    mix(tex(coord_floor_x, coord_floor_y + 1), tex(coord_floor_x + 1, coord_floor_y + 1), coord_frac_x),
                    coord_frac_y);

            if (lit || out) {
                if (lit) {
                    *lit++ = color[0];
                    *lit++ = color[1];
                    *lit++ = color[2];
                }

                if (out) {
                    // FIXME: implement
                    *out++ = 0;
                }
            }

            if (lux) {
                // FIXME: implement
                *lux++ = 0;
                *lux++ = 0;
                *lux++ = 0;
            }
        }
    }
}

void SaveLightmapSurface(const mbsp_t *bsp, mface_t *face, facesup_t *facesup,
    bspx_decoupled_lm_perface *facesup_decoupled, lightsurf_t *lightsurf, const faceextents_t &extents,
    const faceextents_t &output_extents)
{
    lightmapdict_t &lightmaps = lightsurf->lightmapsByStyle;
    const int actual_width = extents.width();
    const int actual_height = extents.height();
    const int output_width = output_extents.width();
    const int output_height = output_extents.height();
    const int size = output_extents.numsamples();

    if (light_options.litonly.value()) {
        // special case for writing a .lit for a bsp without modifying the bsp.
        // involves looking at which styles were written to the bsp in the previous lighting run, and then
        // writing the same styles to the same offsets in the .lit file.

        if (face->lightofs == -1) {
            // nothing to write for this face
            return;
        }

        uint8_t *out, *lit, *lux;
        GetFileSpace_PreserveOffsetInBsp(&out, &lit, &lux, face->lightofs);

        for (int mapnum = 0; mapnum < MAXLIGHTMAPS; mapnum++) {
            const int style = face->styles[mapnum];

            if (style == 255) {
                break; // all done for this face
            }

            // see if we have computed lighting for this style
            for (const lightmap_t &lm : lightmaps) {
                if (lm.style == style) {
                    WriteSingleLightmap(
                        bsp, face, lightsurf, &lm, actual_width, actual_height, out, lit, lux, output_extents);
                    break;
                }
            }
            // if we didn't find a matching lightmap, just don't write anything

            if (out) {
                out += size;
            }
            if (lit) {
                lit += (size * 3);
            }
            if (lux) {
                lux += (size * 3);
            }
        }

        return;
    }

    size_t maxfstyles = std::min((size_t)light_options.facestyles.value(), facesup ? MAXLIGHTMAPSSUP : MAXLIGHTMAPS);
    int maxstyle = facesup ? INVALID_LIGHTSTYLE : INVALID_LIGHTSTYLE_OLD;

    // intermediate collection for sorting lightmaps
    std::vector<std::pair<float, const lightmap_t *>> sortable;

    for (const lightmap_t &lightmap : lightmaps) {
        // skip un-saved lightmaps
        if (lightmap.style == INVALID_LIGHTSTYLE)
            continue;
        if (lightmap.style > maxstyle || (facesup && lightmap.style > INVALID_LIGHTSTYLE_OLD)) {
            if (!warned_about_light_style_overflow) {
                if (IsOutputtingSupplementaryData()) {
                    logging::print(
                        "INFO: a face has exceeded max light style id ({});\n LMSTYLE16 will be output to hold the non-truncated data.\n Use -verbose to find which faces.\n",
                        maxstyle, lightsurf->samples[0].point);
                } else {
                    logging::print(
                        "WARNING: a face has exceeded max light style id ({}). Use -verbose to find which faces.\n",
                        maxstyle, lightsurf->samples[0].point);
                }
                warned_about_light_style_overflow = true;
            }
            logging::print(logging::flag::VERBOSE, "WARNING: Style {} too high on face near {}\n", lightmap.style,
                lightsurf->samples[0].point);
            continue;
        }

        // skip lightmaps where all samples have brightness below 1
        if (bsp->loadversion->game->id != GAME_QUAKE_II) { // HACK: don't do this on Q2. seems if all styles are 0xff,
                                                           // the face is drawn fullbright instead of black (Q1)
            const float maxb = Lightmap_MaxBrightness(&lightmap, lightsurf);
            if (maxb < 1)
                continue;
        }

        const float avgb = Lightmap_AvgBrightness(&lightmap, lightsurf);
        sortable.emplace_back(avgb, &lightmap);
    }

    // HACK: in Q2, if lightofs is -1, then it's drawn fullbright,
    // so we can't optimize away unused portions of the lightmap.
    if (bsp->loadversion->game->id == GAME_QUAKE_II) {
        if (!sortable.size()) {
            lightmap_t *lm = Lightmap_ForStyle(&lightmaps, 0, lightsurf);
            lm->style = 0;
            for (auto &sample : lightsurf->samples) {
                sample.occluded = false;
            }
            sortable.emplace_back(0, lm);
        }
    }

    // sort in descending order of average brightness
    std::sort(sortable.begin(), sortable.end());
    std::reverse(sortable.begin(), sortable.end());

    std::vector<const lightmap_t *> sorted;
    for (const auto &pair : sortable) {
        if (sorted.size() == maxfstyles) {
            if (!warned_about_light_map_overflow) {
                if (IsOutputtingSupplementaryData()) {
                    logging::print(
                        "INFO: a face has exceeded max light styles ({});\n LMSTYLE/LMSTYLE16 will be output to hold the non-truncated data.\n Use -verbose to find which faces.\n",
                        maxfstyles, lightsurf->samples[0].point);
                } else {
                    logging::print(
                        "WARNING: a face has exceeded max light styles ({}). Use -verbose to find which faces.\n",
                        maxfstyles, lightsurf->samples[0].point);
                }
                warned_about_light_map_overflow = true;
            }
            logging::print(logging::flag::VERBOSE,
                "WARNING: {} light styles (max {}) on face near {}; styles: ", sortable.size(), maxfstyles,
                lightsurf->samples[0].point);
            for (auto &p : sortable) {
                logging::print(logging::flag::VERBOSE, "{} ", p.second->style);
            }
            logging::print(logging::flag::VERBOSE, "\n");
            break;
        }

        sorted.push_back(pair.second);
    }

    /* final number of lightmaps */
    const int numstyles = static_cast<int>(sorted.size());
    Q_assert(numstyles <= MAXLIGHTMAPSSUP);

    if (bsp->loadversion->game->id == GAME_QUAKE_II) {
        Q_assert(numstyles > 0);
    }

    /* update face info (either core data or supplementary stuff) */
    if (facesup) {
        facesup->extent[0] = output_width;
        facesup->extent[1] = output_height;
        int mapnum;
        for (mapnum = 0; mapnum < numstyles && mapnum < MAXLIGHTMAPSSUP; mapnum++) {
            facesup->styles[mapnum] = sorted.at(mapnum)->style;
        }
        for (; mapnum < MAXLIGHTMAPSSUP; mapnum++) {
            facesup->styles[mapnum] = INVALID_LIGHTSTYLE;
        }
        facesup->lmscale = lightsurf->lightmapscale;
    } else {
        int mapnum;
        for (mapnum = 0; mapnum < numstyles && mapnum < MAXLIGHTMAPS; mapnum++) {
            face->styles[mapnum] = sorted.at(mapnum)->style;
        }
        for (; mapnum < MAXLIGHTMAPS; mapnum++) {
            face->styles[mapnum] = INVALID_LIGHTSTYLE_OLD;
        }

        if (facesup_decoupled) {
            facesup_decoupled->lmwidth = output_width;
            facesup_decoupled->lmheight = output_height;
            for (size_t i = 0; i < 2; ++i) {
                facesup_decoupled->world_to_lm_space.set_row(i, output_extents.worldToLMMatrix.row(i));
            }
        }
    }

    if (!numstyles)
        return;

    uint8_t *out, *lit, *lux;
    GetFileSpace(&out, &lit, &lux, size * numstyles);

    int lightofs;

    // Q2/HL native colored lightmaps
    if (bsp->loadversion->game->has_rgb_lightmap) {
        lightofs = lit - lit_filebase.data();
    } else {
        lightofs = out - filebase.data();
    }

    if (facesup_decoupled) {
        facesup_decoupled->offset = lightofs;
        face->lightofs = -1;
    } else if (facesup) {
        facesup->lightofs = lightofs;
    } else {
        face->lightofs = lightofs;
    }

    // sanity check that we don't save a lightmap for a non-lightmapped face
    {
        const char *texname = Face_TextureName(bsp, face);
        Q_assert(Face_IsLightmapped(bsp, face));
    }

    for (int mapnum = 0; mapnum < numstyles; mapnum++) {
        const lightmap_t *lm = sorted.at(mapnum);

        WriteSingleLightmap(bsp, face, lightsurf, lm, actual_width, actual_height, out, lit, lux, output_extents);

        if (out) {
            out += size;
        }
        if (lit) {
            lit += (size * 3);
        }
        if (lux) {
            lux += (size * 3);
        }
    }

    // write vanilla lightmap if -world_units_per_luxel is in use but not -novanilla
    if (facesup_decoupled && !light_options.novanilla.value()) {
        // FIXME: duplicates some code from above
        GetFileSpace(&out, &lit, &lux, lightsurf->vanilla_extents.numsamples() * numstyles);

        // Q2/HL native colored lightmaps
        if (bsp->loadversion->game->has_rgb_lightmap) {
            lightofs = lit - lit_filebase.data();
        } else {
            lightofs = out - filebase.data();
        }
        face->lightofs = lightofs;

        for (int mapnum = 0; mapnum < numstyles; mapnum++) {
            const lightmap_t *lm = sorted.at(mapnum);

            WriteSingleLightmap_FromDecoupled(bsp, face, lightsurf, lm, lightsurf->vanilla_extents.width(),
                lightsurf->vanilla_extents.height(), out, lit, lux);

            if (out) {
                out += lightsurf->vanilla_extents.numsamples();
            }
            if (lit) {
                lit += (lightsurf->vanilla_extents.numsamples() * 3);
            }
            if (lux) {
                lux += (lightsurf->vanilla_extents.numsamples() * 3);
            }
        }
    }
}

std::unique_ptr<lightsurf_t> CreateLightmapSurface(const mbsp_t *bsp, const mface_t *face, const facesup_t *facesup,
    const bspx_decoupled_lm_perface *facesup_decoupled, const settings::worldspawn_keys &cfg)
{
    /* Find the correct model offset */
    const modelinfo_t *modelinfo = ModelInfoForFace(bsp, Face_GetNum(bsp, face));
    if (modelinfo == nullptr) {
        return nullptr;
    }

    /* don't bother with degenerate faces */
    if (face->numedges < 3)
        return nullptr;

    // if we don't have a lightmap or emissive, we're done
    if (!Face_IsEmissive(bsp, face) && !Face_IsLightmapped(bsp, face)) {
        return nullptr;
    }

    return Lightsurf_Init(modelinfo, cfg, face, bsp, facesup, facesup_decoupled);
}

/*
 * ============
 * LightFace
 * ============
 */
void DirectLightFace(const mbsp_t *bsp, lightsurf_t &lightsurf, const settings::worldspawn_keys &cfg)
{
    auto face = lightsurf.face;
    const modelinfo_t *modelinfo = ModelInfoForFace(bsp, Face_GetNum(bsp, face));

    lightmapdict_t *lightmaps = &lightsurf.lightmapsByStyle;

    /* calculate dirt (ambient occlusion) but don't use it yet */
    if (dirt_in_use && (light_options.debugmode != debugmodes::phong))
        LightFace_CalculateDirt(&lightsurf);

    /*
     * The lighting procedure is: cast all positive lights, fix
     * minlight levels, then cast all negative lights. Finally, we
     * clamp any values that may have gone negative.
     */

    if (light_options.debugmode == debugmodes::none) {

        total_samplepoints += lightsurf.samples.size();

        const surfflags_t &extended_flags = extended_texinfo_flags[face->texinfo];

        /* positive lights */
        if (!(modelinfo->lightignore.value() || extended_flags.light_ignore)) {
            for (const auto &entity : GetLights()) {
                if (entity->getFormula() == LF_LOCALMIN)
                    continue;
                if (entity->nostaticlight.value())
                    continue;
                if (entity->light.value() > 0)
                    LightFace_Entity(bsp, entity.get(), &lightsurf, lightmaps);
            }
            for (const sun_t &sun : GetSuns())
                if (sun.sunlight > 0)
                    LightFace_Sky(bsp, &sun, &lightsurf, lightmaps);

            // mxd. Add surface lights...
            // FIXME: negative surface lights
            LightFace_SurfaceLight(
                bsp, &lightsurf, lightmaps, std::nullopt, cfg.surflightscale.value(), cfg.surflightskyscale.value(), 16.0f);
        }

        LightFace_LocalMin(bsp, face, &lightsurf, lightmaps);
    }

    /* replace lightmaps with AO for debugging */
    if (light_options.debugmode == debugmodes::dirt)
        LightFace_DirtDebug(bsp, &lightsurf, lightmaps);

    if (light_options.debugmode == debugmodes::phong)
        LightFace_PhongDebug(bsp, &lightsurf, lightmaps);

    if (light_options.debugmode == debugmodes::debugoccluded)
        LightFace_OccludedDebug(bsp, &lightsurf, lightmaps);

    if (light_options.debugmode == debugmodes::debugneighbours)
        LightFace_DebugNeighbours(bsp, &lightsurf, lightmaps);
}

/*
 * ============
 * IndirectLightFace
 * ============
 */
void IndirectLightFace(const mbsp_t *bsp, lightsurf_t &lightsurf, const settings::worldspawn_keys &cfg, size_t bounce_depth)
{
    auto face = lightsurf.face;
    const modelinfo_t *modelinfo = ModelInfoForFace(bsp, Face_GetNum(bsp, face));
    lightmapdict_t *lightmaps = &lightsurf.lightmapsByStyle;

    if (light_options.debugmode == debugmodes::none) {
        const surfflags_t &extended_flags = extended_texinfo_flags[face->texinfo];

        /* positive lights */
        if (!(modelinfo->lightignore.value() || extended_flags.light_ignore)) {

            /* add bounce lighting */
            // note: scale here is just to keep it close-ish to the old code
            LightFace_SurfaceLight(
                bsp, &lightsurf, lightmaps, bounce_depth, cfg.bouncescale.value() * 0.5, cfg.bouncescale.value(), 128.0f);
        }
    }
}

/*
 * ============
 * PostProcessLightFace
 * ============
 */
void PostProcessLightFace(const mbsp_t *bsp, lightsurf_t &lightsurf, const settings::worldspawn_keys &cfg)
{
    auto face = lightsurf.face;
    const modelinfo_t *modelinfo = ModelInfoForFace(bsp, Face_GetNum(bsp, face));

    lightmapdict_t *lightmaps = &lightsurf.lightmapsByStyle;

    if (light_options.debugmode == debugmodes::none) {

        total_samplepoints += lightsurf.samples.size();

        const surfflags_t &extended_flags = extended_texinfo_flags[face->texinfo];

        float minlight = 0;
        qvec3d minlight_color;

        // first, check for global minlight; this only affects style 0
        if (lightsurf.minlight > cfg.minlight.value()) {
            minlight = lightsurf.minlight;
            minlight_color = lightsurf.minlight_color;
        } else {
            minlight = cfg.minlight.value();
            minlight_color = cfg.minlight_color.value();
        }

        if (minlight) {
            LightFace_Min(bsp, face, minlight_color, minlight, &lightsurf, lightmaps, 0);
        }

        if (lightsurf.vpl) {
            if (auto value = IsSurfaceLitFace(bsp, face)) {
                auto *entity = std::get<3>(value.value());
                float surface_minlight_scale = entity ? entity->surflight_minlight_scale.value() : 1.f;
                surface_minlight_scale *= lightsurf.surflight_minlight_scale;

                if (surface_minlight_scale > 0) {
                    minlight = std::get<0>(value.value()) * surface_minlight_scale;
                    minlight_color = std::get<2>(value.value());
                    LightFace_Min(
                        bsp, face, minlight_color, minlight, &lightsurf, lightmaps, std::get<1>(value.value()));
                }
            }
        }

        if (!modelinfo->isWorld()) {
            LightFace_AutoMin(bsp, face, &lightsurf, lightmaps);
        }

        /* negative lights */
        if (!(modelinfo->lightignore.value() || extended_flags.light_ignore)) {
            for (const auto &entity : GetLights()) {
                if (entity->getFormula() == LF_LOCALMIN)
                    continue;
                if (entity->nostaticlight.value())
                    continue;
                if (entity->light.value() < 0)
                    LightFace_Entity(bsp, entity.get(), &lightsurf, lightmaps);
            }
            for (const sun_t &sun : GetSuns())
                if (sun.sunlight < 0)
                    LightFace_Sky(bsp, &sun, &lightsurf, lightmaps);
        }
    }

    if (light_options.debugmode == debugmodes::mottle)
        LightFace_DebugMottle(bsp, &lightsurf, lightmaps);
}
// lightgrid

lightgrid_samples_t &lightgrid_samples_t::operator+=(const lightgrid_samples_t &other) noexcept
{
    for (auto &other_sample : other.samples_by_style) {
        if (!other_sample.used) {
            break;
        }
        add(other_sample.color, other_sample.style);
    }
    return *this;
}

lightgrid_samples_t &lightgrid_samples_t::operator/=(float scale) noexcept
{
    for (auto &sample : samples_by_style) {
        if (!sample.used) {
            break;
        }
        sample.color /= scale;
    }
    return *this;
}

void lightgrid_samples_t::add(const qvec3d &color, int style)
{
    for (auto &sample : samples_by_style) {
        if (!sample.used) {
            // allocate new style
            sample.used = true;
            sample.style = style;
            sample.color = color;
            return;
        }

        if (sample.style == style) {
            // found matching style
            sample.color += color;
            return;
        }
    }

    // uncommon case: all slots are used, but we didn't find a matching style
    // drop the style with the lowest brightness

    int min_brightness_index = 0;
    float min_brightness = FLT_MAX;

    for (int i = 0; i < samples_by_style.size(); ++i) {
        float i_brightness = samples_by_style[i].brightness();
        if (i_brightness < min_brightness) {
            min_brightness_index = i;
            min_brightness = i_brightness;
        }
    }

    // overwrite the sample at min_brightness_index
    auto &target = samples_by_style[min_brightness_index];
    target.style = style;
    target.color = color;
}

qvec3b lightgrid_sample_t::round_to_int() const
{
    return qvec3b{std::clamp((int)round(color[0]), 0, 255), std::clamp((int)round(color[1]), 0, 255),
        std::clamp((int)round(color[2]), 0, 255)};
}

float lightgrid_sample_t::brightness() const
{
    return (color[0] + color[1] + color[2]) / 3.0;
}

bool lightgrid_sample_t::operator==(const lightgrid_sample_t &other) const
{
    if (used != other.used)
        return false;

    if (!used) {
        // if unused, style and color don't matter
        return true;
    }

    if (style != other.style)
        return false;

    // color check requires special handling for nan
    for (int i=0; i<3; ++i) {
        if (std::isnan(color[i])) {
            if (!std::isnan(other.color[i]))
                return false;
        } else {
            if (color[i] != other.color[i])
                return false;
        }
    }

    return true;
}

bool lightgrid_sample_t::operator!=(const lightgrid_sample_t &other) const
{
    return !(*this == other);
}

int lightgrid_samples_t::used_styles() const
{
    int used = 0;
    for (auto &sample : samples_by_style) {
        if (sample.used) {
            used++;
        } else {
            break;
        }
    }
    return used;
}

bool lightgrid_samples_t::operator==(const lightgrid_samples_t &other) const
{
    return samples_by_style == other.samples_by_style;
}

lightgrid_samples_t CalcLightgridAtPoint(const mbsp_t *bsp, const qvec3d &world_point)
{
    // TODO: use more than 1 ray for better performance
    raystream_occlusion_t rs(1);
    raystream_intersection_t rsi(1);

    const auto *pvs = Mod_LeafPvs(bsp, BSP_FindLeafAtPoint(bsp, &bsp->dmodels[0], world_point));

    auto &cfg = light_options;

    lightgrid_samples_t result;

    // from DirectLightFace

    /*
     * The lighting procedure is: cast all positive lights, fix
     * minlight levels, then cast all negative lights. Finally, we
     * clamp any values that may have gone negative.
     */

    /* positive lights */
    for (const auto &entity : GetLights()) {
        if (entity->getFormula() == LF_LOCALMIN)
            continue;
        if (entity->nostaticlight.value())
            continue;
        if (entity->light.value() > 0)
            LightPoint_Entity(bsp, rs, entity.get(), world_point, result);
    }

    for (const sun_t &sun : GetSuns())
        if (sun.sunlight > 0)
            LightPoint_Sky(bsp, rsi, &sun, world_point, result);

    // mxd. Add surface lights...
    // FIXME: negative surface lights
    LightPoint_SurfaceLight(
        bsp, pvs, rs, false, cfg.surflightscale.value(), cfg.surflightskyscale.value(), 16.0f, world_point, result);

#if 0
    // FIXME: port to lightgrid
    float minlight = cfg.minlight.value();
    qvec3d minlight_color = cfg.minlight_color.value();

    if (minlight) {
        LightFace_Min(bsp, face, minlight_color, minlight, &lightsurf, lightmaps, 0);
    }

    LightFace_LocalMin(bsp, face, &lightsurf, lightmaps);
#endif

    /* negative lights */
    for (const auto &entity : GetLights()) {
        if (entity->getFormula() == LF_LOCALMIN)
            continue;
        if (entity->nostaticlight.value())
            continue;
        if (entity->light.value() < 0)
            LightPoint_Entity(bsp, rs, entity.get(), world_point, result);
    }
    for (const sun_t &sun : GetSuns())
        if (sun.sunlight < 0)
            LightPoint_Sky(bsp, rsi, &sun, world_point, result);

    // from IndirectLightFace

    /* add bounce lighting */
    // note: scale here is just to keep it close-ish to the old code
    LightPoint_SurfaceLight(
        bsp, pvs, rs, true, cfg.bouncescale.value() * 0.5, cfg.bouncescale.value(), 128.0f, world_point, result);

    LightPoint_ScaleAndClamp(result);

    return result;
}

void ResetLtFace()
{
    total_light_rays = 0;
    total_light_ray_hits = 0;
    total_samplepoints = 0;

    total_bounce_rays = 0;
    total_bounce_ray_hits = 0;
    total_surflight_rays = 0;
    total_surflight_ray_hits = 0;

    fully_transparent_lightmaps = 0;

    warned_about_light_map_overflow = false;
    warned_about_light_style_overflow = false;
}
