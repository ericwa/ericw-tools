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

#include <light/light.hh>
#include <light/phong.hh>
#include <light/bounce.hh>
#include <light/surflight.hh> //mxd
#include <light/entities.hh>
#include <light/trace.hh>
#include <light/ltface.hh>

#include <common/bsputils.hh>
#include <common/qvec.hh>

#include <cassert>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <fmt/ostream.h>

using namespace std;

std::atomic<uint32_t> total_light_rays, total_light_ray_hits, total_samplepoints;
std::atomic<uint32_t> total_bounce_rays, total_bounce_ray_hits;
std::atomic<uint32_t> total_surflight_rays, total_surflight_ray_hits; // mxd
std::atomic<uint32_t> fully_transparent_lightmaps;

/* ======================================================================== */

qvec2d WorldToTexCoord(const qvec3d &world, const gtexinfo_t *tex)
{
    /*
     * The (long double) casts below are important: The original code
     * was written for x87 floating-point which uses 80-bit floats for
     * intermediate calculations. But if you compile it without the
     * casts for modern x86_64, the compiler will round each
     * intermediate result to a 32-bit float, which introduces extra
     * rounding error.
     *
     * This becomes a problem if the rounding error causes the light
     * utilities and the engine to disagree about the lightmap size
     * for some surfaces.
     *
     * Casting to (long double) keeps the intermediate values at at
     * least 64 bits of precision, probably 128.
     */
    return tex->vecs.uvs<long double>(world);
}

static qvec2f Face_WorldToTexCoord(const mbsp_t *bsp, const mface_t *face, const qvec3f &world)
{
    const gtexinfo_t *tex = Face_Texinfo(bsp, face);

    if (tex == nullptr)
        return {};

    return WorldToTexCoord(world, tex);
}

faceextents_t::faceextents_t(const mface_t *face, const mbsp_t *bsp, float lmscale) : m_lightmapscale(lmscale)
{
    m_worldToTexCoord = WorldToTexSpace(bsp, face);
    m_texCoordToWorld = TexSpaceToWorld(bsp, face);

    qvec2f mins(FLT_MAX, FLT_MAX);
    qvec2f maxs(-FLT_MAX, -FLT_MAX);

    for (int i = 0; i < face->numedges; i++) {
        const qvec3f &worldpoint = Face_PointAtIndex(bsp, face, i);
        const qvec2f texcoord = Face_WorldToTexCoord(bsp, face, worldpoint);

        // self test
        auto texcoordRT = this->worldToTexCoord(worldpoint);
        auto worldpointRT = this->texCoordToWorld(texcoord);
        Q_assert(qv::epsilonEqual(texcoordRT, texcoord, 0.1f));
        Q_assert(qv::epsilonEqual(worldpointRT, worldpoint, 0.1f));
        // end self test

        for (int j = 0; j < 2; j++) {
            if (texcoord[j] < mins[j])
                mins[j] = texcoord[j];
            if (texcoord[j] > maxs[j])
                maxs[j] = texcoord[j];
        }
    }

    for (int i = 0; i < 2; i++) {
        mins[i] = floor(mins[i] / m_lightmapscale);
        maxs[i] = ceil(maxs[i] / m_lightmapscale);
        m_texmins[i] = static_cast<int>(mins[i]);
        m_texsize[i] = static_cast<int>(maxs[i] - mins[i]);

        if (m_texsize[i] >= MAXDIMENSION) {
            const qplane3d plane = Face_Plane(bsp, face);
            const qvec3f &point = Face_PointAtIndex(bsp, face, 0); // grab first vert
            const char *texname = Face_TextureName(bsp, face);

            Error("Bad surface extents:\n"
                  "   surface {}, {} extents = {}, scale = {}\n"
                  "   texture {} at ({})\n"
                  "   surface normal ({})\n",
                Face_GetNum(bsp, face), i ? "t" : "s", m_texsize[i], m_lightmapscale, texname, point, plane.normal);
        }
    }
}

int faceextents_t::width() const
{
    return m_texsize[0] + 1;
}
int faceextents_t::height() const
{
    return m_texsize[1] + 1;
}
int faceextents_t::numsamples() const
{
    return width() * height();
}
qvec2i faceextents_t::texsize() const
{
    return {width(), height()};
}

int faceextents_t::indexOf(const qvec2i &lm) const
{
    Q_assert(lm[0] >= 0 && lm[0] < width());
    Q_assert(lm[1] >= 0 && lm[1] < height());
    return lm[0] + (width() * lm[1]);
}

qvec2i faceextents_t::intCoordsFromIndex(int index) const
{
    Q_assert(index >= 0);
    Q_assert(index < numsamples());

    qvec2i res(index % width(), index / width());
    Q_assert(indexOf(res) == index);
    return res;
}

qvec2f faceextents_t::LMCoordToTexCoord(const qvec2f &LMCoord) const
{
    const qvec2f res(m_lightmapscale * (m_texmins[0] + LMCoord[0]), m_lightmapscale * (m_texmins[1] + LMCoord[1]));
    return res;
}

qvec2f faceextents_t::TexCoordToLMCoord(const qvec2f &tc) const
{
    const qvec2f res((tc[0] / m_lightmapscale) - m_texmins[0], (tc[1] / m_lightmapscale) - m_texmins[1]);
    return res;
}

qvec2f faceextents_t::worldToTexCoord(qvec3f world) const
{
    const qvec4f worldPadded(world, 1.0f);
    const qvec4f res = m_worldToTexCoord * worldPadded;

    Q_assert(res[3] == 1.0f);

    return res;
}

qvec3f faceextents_t::texCoordToWorld(qvec2f tc) const
{
    const qvec4f tcPadded(tc[0], tc[1], 0.0f, 1.0f);
    const qvec4f res = m_texCoordToWorld * tcPadded;

    Q_assert(fabs(res[3] - 1.0f) < 0.01f);

    return res;
}

qvec2f faceextents_t::worldToLMCoord(qvec3f world) const
{
    return TexCoordToLMCoord(worldToTexCoord(world));
}

qvec3f faceextents_t::LMCoordToWorld(qvec2f lm) const
{
    return texCoordToWorld(LMCoordToTexCoord(lm));
}

qmat4x4f WorldToTexSpace(const mbsp_t *bsp, const mface_t *f)
{
    const gtexinfo_t *tex = Face_Texinfo(bsp, f);
    if (tex == nullptr) {
        Q_assert_unreachable();
        return qmat4x4f();
    }
    const qplane3d plane = Face_Plane(bsp, f);

    //           [s]
    // T * vec = [t]
    //           [distOffPlane]
    //           [?]

    qmat4x4f T{
        tex->vecs.at(0, 0), tex->vecs.at(1, 0), static_cast<float>(plane.normal[0]), 0, // col 0
        tex->vecs.at(0, 1), tex->vecs.at(1, 1), static_cast<float>(plane.normal[1]), 0, // col 1
        tex->vecs.at(0, 2), tex->vecs.at(1, 2), static_cast<float>(plane.normal[2]), 0, // col 2
        tex->vecs.at(0, 3), tex->vecs.at(1, 3), static_cast<float>(-plane.dist), 1 // col 3
    };
    return T;
}

constexpr qvec3d TexCoordToWorld(vec_t s, vec_t t, const texorg_t *texorg)
{
    return (texorg->texSpaceToWorld * qvec4f(s, t, /* one "unit" in front of surface */ 1.0, 1.0)).xyz();
}

/* Debug helper - move elsewhere? */
void PrintFaceInfo(const mface_t *face, const mbsp_t *bsp)
{
    const gtexinfo_t *tex = &bsp->texinfo[face->texinfo];
    const char *texname = Face_TextureName(bsp, face);

    LogPrint("face {}, texture {}, {} edges; vectors:\n"
             "{: 3.3}\n",
        Face_GetNum(bsp, face), texname, face->numedges, tex->vecs);

    for (int i = 0; i < face->numedges; i++) {
        int edge = bsp->dsurfedges[face->firstedge + i];
        int vert = Face_VertexAtIndex(bsp, face, i);
        const qvec3f &point = GetSurfaceVertexPoint(bsp, face, i);
        const qvec3f norm = GetSurfaceVertexNormal(bsp, face, i).normal;
        LogPrint("{} {:3} ({:3.3}, {:3.3}, {:3.3}) :: normal ({:3.3}, {:3.3}, {:3.3}) :: edge {}\n",
            i ? "          " : "    verts ", vert, point[0], point[1], point[2], norm[0], norm[1], norm[2], edge);
    }
}

/*
 * ================
 * CalcFaceExtents
 * Fills in surf->texmins[], surf->texsize[] and sets surf->exactmid[]
 * ================
 */
// FIXME: duped by faceextents_t?
static void CalcFaceExtents(const mface_t *face, const mbsp_t *bsp, lightsurf_t *surf)
{
    vec_t mins[2], maxs[2];
    qvec3d worldmaxs, worldmins;

    mins[0] = mins[1] = VECT_MAX;
    maxs[0] = maxs[1] = -VECT_MAX;
    worldmaxs[0] = worldmaxs[1] = worldmaxs[2] = -VECT_MAX;
    worldmins[0] = worldmins[1] = worldmins[2] = VECT_MAX;
    const gtexinfo_t *tex = &bsp->texinfo[face->texinfo];

    for (int i = 0; i < face->numedges; i++) {
        const int edge = bsp->dsurfedges[face->firstedge + i];
        const int vert = (edge >= 0) ? bsp->dedges[edge][0] : bsp->dedges[-edge][1];
        const qvec3d worldpoint = bsp->dvertexes[vert];

        qvec2d texcoord = WorldToTexCoord(worldpoint, tex);
        for (int j = 0; j < 2; j++) {
            if (texcoord[j] < mins[j])
                mins[j] = texcoord[j];
            if (texcoord[j] > maxs[j])
                maxs[j] = texcoord[j];
        }

        // ericw -- also save worldmaxs/worldmins, for calculating a bounding sphere
        for (int j = 0; j < 3; j++) {
            if (worldpoint[j] > worldmaxs[j])
                worldmaxs[j] = worldpoint[j];
            if (worldpoint[j] < worldmins[j])
                worldmins[j] = worldpoint[j];
        }
    }

    qvec3d worldpoint = Face_Centroid(bsp, face);
    surf->exactmid = WorldToTexCoord(worldpoint, tex);

    // calculate a bounding sphere for the face
    {
        qvec3d radius = (worldmaxs - worldmins) * 0.5;

        surf->origin = worldmins + radius;
        surf->radius = qv::length(radius);
        surf->bounds = {worldmins, worldmaxs};
    }

    for (int i = 0; i < 2; i++) {
        mins[i] = floor(mins[i] / surf->lightmapscale);
        maxs[i] = ceil(maxs[i] / surf->lightmapscale);
        surf->texmins[i] = mins[i];
        surf->texsize[i] = maxs[i] - mins[i];
        if (surf->texsize[i] >= MAXDIMENSION) {
            const dplane_t &plane = bsp->dplanes[face->planenum];
            const char *texname = Face_TextureName(bsp, face);
            Error("Bad surface extents:\n"
                  "   face {}, {} extents = {}, scale = {}\n"
                  "   texture {} at [{}]\n"
                  "   surface normal [{}]\n",
                Face_GetNum(bsp, face), i ? "t" : "s", surf->texsize[i], surf->lightmapscale, texname, worldpoint,
                plane.normal);
        }
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
        : m_unoccluded(false), m_actualFace(nullptr), m_position(position), m_interpolatedNormal({})
    {
    }

    position_t(const mface_t *actualFace, const qvec3f &position, const qvec3f &interpolatedNormal)
        : m_unoccluded(true), m_actualFace(actualFace), m_position(position),
          m_interpolatedNormal(interpolatedNormal){};
};

static constexpr float sampleOffPlaneDist = 1.0f;

static float TexSpaceDist(const mbsp_t *bsp, const mface_t *face, const qvec3f &p0, const qvec3f &p1)
{
    const qvec2f p0_tex = Face_WorldToTexCoord(bsp, face, p0);
    const qvec2f p1_tex = Face_WorldToTexCoord(bsp, face, p1);

    return qv::length(p1_tex - p0_tex);
}

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
    float face_lmscale, int recursiondepth, const qvec3f &modelOffset)
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
    const qvec3f point = GLM_ProjectPointOntoPlane(surfplane, origPoint) + (qvec3f(surfplane) * sampleOffPlaneDist);

    // check if in face..
    if (GLM_EdgePlanes_PointInside(edgeplanes, point)) {
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
        const qvec4f in1 = GLM_MakePlane(in1_normal, n.p0);
        const qvec4f in2 = GLM_MakePlane(in2_normal, n.p1);
        
        const float in1_dist = GLM_DistAbovePlane(in1, point);
        const float in2_dist = GLM_DistAbovePlane(in2, point);
        if (in1_dist >= 0 && in2_dist >= 0) {
            const auto &n_facecache = FaceCacheForFNum(Face_GetNum(bsp, n.face));
            const qvec4f &n_surfplane = n_facecache.plane();
            const auto &n_edgeplanes = n_facecache.edgePlanes();
            
            // project `point` onto the surface plane, then lift it off again
            const qvec3f n_point = GLM_ProjectPointOntoPlane(n_surfplane, origPoint) + (qvec3f(n_surfplane) * sampleOffPlaneDist);
            
            // check if in face..
            if (GLM_EdgePlanes_PointInside(n_edgeplanes, n_point)) {
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

            const auto edgeplane = GLM_MakeInwardFacingEdgePlane(v0, v1, surfplane);
            if (!edgeplane.first)
                continue; // degenerate edge

            const float planedist = GLM_DistAbovePlane(edgeplane.second, point);
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
                        bsp, smoothed, point, phongShaded, face_lmscale, recursiondepth + 1, modelOffset);
                }
            }
        }
    }

    // 2. Try snapping to poly

    const pair<int, qvec3f> closest = GLM_ClosestPointOnPolyBoundary(points, point);
    const float texSpaceDist = TexSpaceDist(bsp, face, closest.second, point);

    if (texSpaceDist <= face_lmscale) {
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
            const qvec3d &point = surf->points[i];
            const qvec3d mangle = qv::mangle_from_vec(qvec3d(surf->normals[i]));

            f << "{\n";
            f << "\"classname\" \"light\"\n";
            fmt::print(f, "\"origin\" \"{}\"\n", point);
            fmt::print(f, "\"mangle\" \"{}\"\n", mangle);
            fmt::print(f, "\"face\" \"{}\"\n", surf->realfacenums[i]);
            fmt::print(f, "\"occluded\" \"{}\"\n", (int)surf->occluded[i]);
            fmt::print(f, "\"s\" \"{}\"\n", s);
            fmt::print(f, "\"t\" \"{}\"\n", t);
            f << "}\n";
        }
    }

    LogPrint("wrote face {}'s sample points ({}x{}) to calcpoints.map\n", Face_GetNum(bsp, surf->face), surf->width,
        surf->height);

    PrintFaceInfo(surf->face, bsp);
}

/// Checks if the point is in any solid (solid or sky leaf)
/// 1. the world
/// 2. any shadow-casting bmodel
/// 3. the `self` model (regardless of whether it's selfshadowing)
///
/// This is used for marking sample points as occluded.
bool Light_PointInAnySolid(const mbsp_t *bsp, const dmodelh2_t *self, const qvec3d &point)
{
    if (Light_PointInSolid(bsp, self, point))
        return true;

    if (Light_PointInWorld(bsp, point))
        return true;

    for (const auto &modelinfo : tracelist) {
        if (Light_PointInSolid(bsp, modelinfo->model, point)) {
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

    const float planedist = GLM_DistAbovePlane(plane, point);
    if (!(fabs(planedist - sampleOffPlaneDist) <= 0.1)) {
        // something is wrong?
        return position_t(point);
    }

    const float insideDist = GLM_EdgePlanes_PointInsideDist(edgeplanes, point);
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
        const auto interpNormal = GLM_InterpolateNormal(points, normals, point);
        // We already know the point is in the face, so this should always succeed
        if (!interpNormal.first)
            return position_t(point);
        pointNormal = interpNormal.second;
    } else {
        pointNormal = plane;
    }

    const bool inSolid = Light_PointInAnySolid(bsp, mi->model, point + modelOffset);
    if (inSolid) {
        // Check distance to border
        const float distanceInside = GLM_EdgePlanes_PointInsideDist(edgeplanes, point);
        if (distanceInside < 1.0f) {
            // Point is too close to the border. Try nudging it inside.
            const auto &shrunk = facecache.pointsShrunkBy1Unit();
            if (!shrunk.empty()) {
                const pair<int, qvec3f> closest = GLM_ClosestPointOnPolyBoundary(shrunk, point);
                const qvec3f newPoint = closest.second + (qvec3f(plane) * sampleOffPlaneDist);
                if (!Light_PointInAnySolid(bsp, mi->model, newPoint + modelOffset))
                    return position_t(face, newPoint, pointNormal);
            }
        }

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

    /*
     * Fill in the surface points. The points are biased towards the center of
     * the surface to help avoid edge cases just inside walls
     */
    surf->midpoint = TexCoordToWorld(surf->exactmid[0], surf->exactmid[1], &surf->texorg);
    surf->midpoint += offset;

    surf->width = (surf->texsize[0] + 1) * options.extra.value();
    surf->height = (surf->texsize[1] + 1) * options.extra.value();
    const float starts = (surf->texmins[0] - 0.5 + (0.5 / options.extra.value())) * surf->lightmapscale;
    const float startt = (surf->texmins[1] - 0.5 + (0.5 / options.extra.value())) * surf->lightmapscale;
    const float st_step = surf->lightmapscale / options.extra.value();

    /* Allocate surf->points */
    surf->numpoints = surf->width * surf->height;
    surf->points = new qvec3d[surf->numpoints];
    surf->normals = new qvec3d[surf->numpoints];
    surf->occluded = new bool[surf->numpoints];
    surf->realfacenums = new int[surf->numpoints];

    const auto points = GLM_FacePoints(bsp, face);
    const auto edgeplanes = GLM_MakeInwardFacingEdgePlanes(points);

    for (int t = 0; t < surf->height; t++) {
        for (int s = 0; s < surf->width; s++) {
            const int i = t * surf->width + s;
            qvec3d &point = surf->points[i];
            qvec3d &norm = surf->normals[i];
            int *realfacenum = &surf->realfacenums[i];

            const vec_t us = starts + s * st_step;
            const vec_t ut = startt + t * st_step;

            point = TexCoordToWorld(us, ut, &surf->texorg);

            // do this before correcting the point, so we can wrap around the inside of pipes
            const bool phongshaded = (surf->curved && cfg.phongallowed.value());
            const auto res = CalcPointNormal(bsp, face, point, phongshaded, surf->lightmapscale, 0, offset);

            surf->occluded[i] = !res.m_unoccluded;
            *realfacenum = res.m_actualFace != nullptr ? Face_GetNum(bsp, res.m_actualFace) : -1;
            point = res.m_position + offset;
            norm = res.m_interpolatedNormal;
        }
    }

    if (dump_facenum == Face_GetNum(bsp, face)) {
        CalcPoints_Debug(surf, bsp);
    }
}

static bool Lightsurf_Init(
    const modelinfo_t *modelinfo, const mface_t *face, const mbsp_t *bsp, lightsurf_t *lightsurf, facesup_t *facesup)
{
    /*FIXME: memset can be slow on large datasets*/
    //    memset(lightsurf, 0, sizeof(*lightsurf));
    lightsurf->modelinfo = modelinfo;
    lightsurf->bsp = bsp;
    lightsurf->face = face;

    if (facesup)
        lightsurf->lightmapscale = facesup->lmscale;
    else
        lightsurf->lightmapscale = modelinfo->lightmapscale;

    const surfflags_t &extended_flags = extended_texinfo_flags[face->texinfo];
    lightsurf->curved = extended_flags.phong_angle != 0;

    // nodirt
    if (modelinfo->dirt.isChanged()) {
        lightsurf->nodirt = (modelinfo->dirt.value() == -1);
    } else {
        lightsurf->nodirt = extended_flags.no_dirt;
    }

    // minlight
    if (modelinfo->minlight.isChanged()) {
        lightsurf->minlight = modelinfo->minlight.value();
    } else {
        lightsurf->minlight = extended_flags.minlight;
    }

    // minlight_color
    if (modelinfo->minlight_color.isChanged()) {
        lightsurf->minlight_color = modelinfo->minlight_color.value();
    } else {
        // if modelinfo mincolor not set, use the one from the .texinfo file
        if (lightsurf->minlight > 0 && qv::emptyExact(extended_flags.minlight_color)) {
            lightsurf->minlight_color = {255.0};
        } else {
            lightsurf->minlight_color = extended_flags.minlight_color;
        }
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

    /* Set up the plane, not including model offset */
    qplane3d &plane = lightsurf->plane;
    if (face->side) {
        plane = -bsp->dplanes[face->planenum];
    } else {
        plane = bsp->dplanes[face->planenum];
    }

    /* Set up the texorg for coordinate transformation */
    lightsurf->texorg.texSpaceToWorld = TexSpaceToWorld(bsp, face);
    lightsurf->texorg.texinfo = &bsp->texinfo[face->texinfo];
    lightsurf->texorg.planedist = plane.dist;

    /* Check for invalid texture axes */
    if (std::isnan(lightsurf->texorg.texSpaceToWorld.at(0, 0))) {
        LogPrint("Bad texture axes on face:\n");
        PrintFaceInfo(face, bsp);
        return false;
    }

    const gtexinfo_t *tex = &bsp->texinfo[face->texinfo];
    lightsurf->snormal = qv::normalize(tex->vecs.row(0).xyz());
    lightsurf->tnormal = -qv::normalize(tex->vecs.row(1).xyz());

    /* Set up the surface points */
    CalcFaceExtents(face, bsp, lightsurf);
    CalcPoints(modelinfo, modelinfo->offset, lightsurf, bsp, face);

    /* Correct the plane for the model offset (must be done last,
       calculation of face extents / points needs the uncorrected plane) */
    qvec3d planepoint = (plane.normal * plane.dist) + modelinfo->offset;
    plane.dist = qv::dot(plane.normal, planepoint);

    /* Correct bounding sphere */
    lightsurf->origin += modelinfo->offset;
    lightsurf->bounds = lightsurf->bounds.translate(modelinfo->offset);

    /* Allocate occlusion array */
    lightsurf->occlusion = new float[lightsurf->numpoints]{};

    lightsurf->intersection_stream = MakeIntersectionRayStream(lightsurf->numpoints);
    lightsurf->occlusion_stream = MakeOcclusionRayStream(lightsurf->numpoints);
    return true;
}

static void Lightmap_AllocOrClear(lightmap_t *lightmap, const lightsurf_t *lightsurf)
{
    if (lightmap->samples == NULL) {
        /* first use of this lightmap, allocate the storage for it. */
        lightmap->samples = new lightsample_t[lightsurf->numpoints]{};
    } else {
        /* clear only the data that is going to be merged to it. there's no point clearing more */
        memset(lightmap->samples, 0, sizeof(*lightmap->samples) * lightsurf->numpoints);
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
        if (lm.style == 255) {
            Lightmap_AllocOrClear(&lm, lightsurf);
            return &lm;
        }
    }

    // add a new one to the vector (invalidates existing lightmap_t pointers)
    lightmap_t &newLightmap = lightmaps->emplace_back();
    newLightmap.style = 255;
    Lightmap_AllocOrClear(&newLightmap, lightsurf);
    return &newLightmap;
}

static void Lightmap_ClearAll(lightmapdict_t *lightmaps)
{
    for (auto &lm : *lightmaps) {
        lm.style = 255;
    }
}

/*
 * Lightmap_Save
 *
 * As long as we have space for the style, mark as allocated,
 * otherwise emit a warning.
 */
static void Lightmap_Save(
    lightmapdict_t *lightmaps, const lightsurf_t *lightsurf, lightmap_t *lightmap, const int style)
{
    if (lightmap->style == 255) {
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
    const qvec3d &surfpointToLightDir, float dist, bool twosided)
{
    vec_t angle = qv::dot(surfpointToLightDir, surfnorm);
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
    const img::texture *tex, const std::array<vec_t, 16> &projectionmatrix, const qvec3d &point, qvec3d &result)
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
    const qvec3d &surfpoint, bool twosided, qvec3d &color_out, qvec3d &surfpointToLightDir_out,
    qvec3d &normalmap_addition_out, float *dist_out)
{
    float dist = GetDir(surfpoint, entity->origin.value(), surfpointToLightDir_out);
    if (dist < 0.1) {
        // Catch 0 distance between sample point and light (produces infinite brightness / nan's) and causes
        // problems later
        dist = 0.1f;
        surfpointToLightDir_out = {0, 0, 1};
    }
    const float add = GetLightValueWithAngle(cfg, entity, surfnorm, surfpointToLightDir_out, dist, twosided);

    /* write out the final color */
    if (entity->projectedmip) {
        qvec3d col;
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

// this is the inverse of GetLightValue
float GetLightDist(const settings::worldspawn_keys &cfg, const light_t *entity, vec_t desiredLight)
{
    float fadedist;
    if (entity->getFormula() == LF_LINEAR) {
        /* Linear formula always has a falloff point */
        fadedist = fabs(entity->light.value()) - desiredLight;
        fadedist = fadedist / entity->atten.value() / cfg.scaledist.value();
        fadedist = max(0.0f, fadedist);
    } else {
        /* Calculate the distance at which brightness falls to desiredLight */
        switch (entity->getFormula()) {
            case LF_INFINITE:
            case LF_LOCALMIN: fadedist = FLT_MAX; break;
            case LF_INVERSE:
                fadedist = (LF_SCALE * fabs(entity->light.value())) /
                           (cfg.scaledist.value() * entity->atten.value() * desiredLight);
                break;
            case LF_INVERSE2:
            case LF_INVERSE2A:
                fadedist = sqrt(fabs(entity->light.value() * SQR(LF_SCALE) /
                                     (SQR(cfg.scaledist.value()) * SQR(entity->atten.value()) * desiredLight)));
                if (entity->getFormula() == LF_INVERSE2A) {
                    fadedist -= (LF_SCALE / (cfg.scaledist.value() * entity->atten.value()));
                }
                fadedist = max(0.0f, fadedist);
                break;
            default: FError("Internal error: formula not handled");
        }
    }
    return fadedist;
}

// CHECK: naming? why clamp*min*?
constexpr void Light_ClampMin(lightsample_t *sample, const vec_t light, const qvec3d &color)
{
    for (int i = 0; i < 3; i++) {
        sample->color[i] = std::max(sample->color[i], color[i] * (light / 255.0f));
    }
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
    vec_t light_dirtgain = cfg.dirtGain.value();
    vec_t light_dirtscale = cfg.dirtScale.value();
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
            usedirt = cfg.globalDirt.value();
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

        if (entity->dirt_on_radius.isChanged() && entity->dirt_off_radius.isChanged()) {

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

    if (!options.novisapprox.value() && entity->bounds.disjoint(lightsurf->bounds, 0.001)) {
        return true;
    }

    qvec3d distvec = entity->origin.value() - lightsurf->origin;
    const float dist = qv::length(distvec) - lightsurf->radius;

    /* light is inside surface bounding sphere => can't cull */
    if (dist < 0) {
        return false;
    }

    /* return true if the light level at the closest point on the
     surface bounding sphere to the light source is <= fadegate.
     need fabs to handle antilights. */
    return fabs(GetLightValue(cfg, entity, dist)) <= options.gate.value();
}

/*
 * ================
 * GetDirectLighting
 *
 * Mesaures direct lighting at a point, currently only used for bounce lighting.
 * FIXME: factor out / merge with LightFace
 *
 * This gathers up how much a patch should bounce back into the level,
 * per-lightstyle.
 * ================
 */
std::map<int, qvec3f> GetDirectLighting(
    const mbsp_t *bsp, const settings::worldspawn_keys &cfg, const qvec3d &origin, const qvec3d &normal)
{
    std::map<int, qvec3f> result;

    // mxd. Surface lights...
    for (const surfacelight_t &vpl : SurfaceLights()) {
        // Bounce light falloff. Uses light surface center and intensity based on face area
        qvec3d surfpointToLightDir;
        // FIXME: this is always 128 because vpl.pos and origin are always equal it seems?
        const float surfpointToLightDist =
            max(128.0, GetDir(vpl.pos, origin,
                           surfpointToLightDir)); // Clamp away hotspots, also avoid division by 0...
        const vec_t angle = qv::dot(surfpointToLightDir, normal);
        if (angle <= 0)
            continue;

        // Exponential falloff
        const float add = (vpl.totalintensity / SQR(surfpointToLightDist)) * angle;
        if (add <= 0)
            continue;

        // Write out the final color
        // color_out is expected to be in [0..255] range, vpl->color is in [0..1] range.
        qvec3d color = (vpl.color * add) * cfg.surflightbouncescale.value();

        // NOTE: Skip negative lights, which would make no sense to bounce!
        if (LightSample_Brightness(color) <= options.gate.value())
            continue;

        if (!TestLight(vpl.pos, origin, nullptr).blocked)
            continue;

        result[0] += color;
    }

    for (const light_t &entity : GetLights()) {
        qvec3d surfpointToLightDir;
        float surfpointToLightDist;
        qvec3d color, normalcontrib;

        if (entity.nostaticlight.value()) {
            continue;
        }

        // Skip styled lights if "bouncestyled" setting is off.
        if (entity.style.value() != 0 && !cfg.bouncestyled.value()) {
            continue;
        }

        GetLightContrib(
            cfg, &entity, normal, origin, false, color, surfpointToLightDir, normalcontrib, &surfpointToLightDist);

        color *= entity.bouncescale.value();

        // NOTE: Skip negative lights, which would make no sense to bounce!
        if (LightSample_Brightness(color) <= options.gate.value()) {
            continue;
        }

        const hitresult_t hit = TestLight(entity.origin.value(), origin, NULL);
        if (!hit.blocked) {
            continue;
        }

        int lightstyle = entity.style.value();
        if (lightstyle == 0) {
            // switchable shadow only blocks style 0 lights, otherwise switchable lights become always on when shadow is
            // hidden
            lightstyle = hit.passedSwitchableShadowStyle;
        }

        result[lightstyle] += color;
    }

    for (const sun_t &sun : GetSuns()) {
        // Skip styled lights if "bouncestyled" setting is off.
        if (sun.style != 0 && !cfg.bouncestyled.value()) {
            continue;
        }

        // NOTE: Skip negative lights, which would make no sense to bounce!
        if (sun.sunlight < 0)
            continue;

        qvec3d originLightDir = qv::normalize(sun.sunvec);

        vec_t cosangle = qv::dot(originLightDir, normal);
        if (cosangle < 0) {
            continue;
        }

        // apply anglescale
        cosangle = (1.0 - sun.anglescale) + sun.anglescale * cosangle;

        const mface_t *face = nullptr;
        const hitresult_t hit = TestSky(origin, sun.sunvec, NULL, &face);
        if (!hit.blocked) {
            continue;
        }

        int lightstyle = sun.style;
        if (lightstyle == 0) {
            lightstyle = hit.passedSwitchableShadowStyle; // switchable shadow only blocks style 0 suns
        }

        // check if we hit the wrong texture
        // TODO: this could be faster!
        // TODO: deduplicate from LightFace_Sky
        if (!sun.suntexture.empty()) {
            const char *facetex = Face_TextureName(bsp, face);
            if (sun.suntexture != facetex) {
                continue;
            }
        }

        const qvec3f sunContrib = qvec3f(sun.sunlight_color) * (cosangle * sun.sunlight / 255.0f);
        result[lightstyle] += sunContrib;
    }

    return result;
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

    /*
     * Check it for real
     */
    raystream_occlusion_t *rs = lightsurf->occlusion_stream;
    rs->clearPushedRays();

    for (int i = 0; i < lightsurf->numpoints; i++) {
        const qvec3d &surfpoint = lightsurf->points[i];
        const qvec3d &surfnorm = lightsurf->normals[i];

        if (lightsurf->occluded[i])
            continue;

        qvec3d surfpointToLightDir;
        float surfpointToLightDist;
        qvec3d color, normalcontrib;

        GetLightContrib(cfg, entity, surfnorm, surfpoint, lightsurf->twosided, color, surfpointToLightDir,
            normalcontrib, &surfpointToLightDist);

        const float occlusion =
            Dirt_GetScaleFactor(cfg, lightsurf->occlusion[i], entity, surfpointToLightDist, lightsurf);
        color *= occlusion;

        /* Quick distance check first */
        if (fabs(LightSample_Brightness(color)) <= options.gate.value()) {
            continue;
        }

        rs->pushRay(i, surfpoint, surfpointToLightDir, surfpointToLightDist, &color, &normalcontrib);
    }

    // don't need closest hit, just checking for occlusion between light and surface point
    rs->tracePushedRaysOcclusion(modelinfo);
    total_light_rays += rs->numPushedRays();

    int cached_style = entity->style.value();
    lightmap_t *cached_lightmap = Lightmap_ForStyle(lightmaps, cached_style, lightsurf);

    const int N = rs->numPushedRays();
    for (int j = 0; j < N; j++) {
        if (rs->getPushedRayOccluded(j)) {
            continue;
        }

        total_light_ray_hits++;

        int i = rs->getPushedRayPointIndex(j);

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
            desired_style = rs->getPushedRayDynamicStyle(j);
        }

        // if necessary, switch which lightmap we are writing to.
        if (desired_style != cached_style) {
            cached_style = desired_style;
            cached_lightmap = Lightmap_ForStyle(lightmaps, cached_style, lightsurf);
        }

        lightsample_t *sample = &cached_lightmap->samples[i];

        sample->color += rs->getPushedRayColor(j);
        sample->direction += rs->getPushedRayNormalContrib(j);

        Lightmap_Save(lightmaps, lightsurf, cached_lightmap, cached_style);
    }
}

/*
 * =============
 * LightFace_Sky
 * =============
 */
static void LightFace_Sky(const sun_t *sun, const lightsurf_t *lightsurf, lightmapdict_t *lightmaps)
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
    if (dp < -ANGLE_EPSILON && !lightsurf->curved && !lightsurf->twosided) {
        return;
    }

    /* Check each point... */
    raystream_intersection_t *rs = lightsurf->intersection_stream;
    rs->clearPushedRays();

    for (int i = 0; i < lightsurf->numpoints; i++) {
        const qvec3d &surfpoint = lightsurf->points[i];
        const qvec3d &surfnorm = lightsurf->normals[i];

        if (lightsurf->occluded[i])
            continue;

        vec_t angle = qv::dot(incoming, surfnorm);
        if (lightsurf->twosided) {
            if (angle < 0) {
                angle = -angle;
            }
        }

        angle = max(0.0, angle);

        angle = (1.0 - sun->anglescale) + sun->anglescale * angle;
        float value = angle * sun->sunlight;
        if (sun->dirt) {
            value *= Dirt_GetScaleFactor(cfg, lightsurf->occlusion[i], NULL, 0.0, lightsurf);
        }

        qvec3d color = sun->sunlight_color * (value / 255.0);

        /* Quick distance check first */
        if (fabs(LightSample_Brightness(color)) <= options.gate.value()) {
            continue;
        }

        qvec3d normalcontrib = sun->sunvec * value;

        rs->pushRay(i, surfpoint, incoming, MAX_SKY_DIST, &color, &normalcontrib);
    }

    // We need to check if the first hit face is a sky face, so we need
    // to test intersection (not occlusion)
    rs->tracePushedRaysIntersection(modelinfo);

    /* if sunlight is set, use a style 0 light map */
    int cached_style = sun->style;
    lightmap_t *cached_lightmap = Lightmap_ForStyle(lightmaps, cached_style, lightsurf);

    const int N = rs->numPushedRays();
    for (int j = 0; j < N; j++) {
        if (rs->getPushedRayHitType(j) != hittype_t::SKY) {
            continue;
        }

        // check if we hit the wrong texture
        // TODO: this could be faster!
        if (!sun->suntexture.empty()) {
            const mface_t *face = rs->getPushedRayHitFace(j);
            const char *facetex = Face_TextureName(lightsurf->bsp, face);
            if (sun->suntexture != facetex) {
                continue;
            }
        }

        const int i = rs->getPushedRayPointIndex(j);

        // check if we hit a dynamic shadow caster
        int desired_style = sun->style;
        if (desired_style == 0) {
            desired_style = rs->getPushedRayDynamicStyle(j);
        }

        // if necessary, switch which lightmap we are writing to.
        if (desired_style != cached_style) {
            cached_style = desired_style;
            cached_lightmap = Lightmap_ForStyle(lightmaps, cached_style, lightsurf);
        }

        lightsample_t *sample = &cached_lightmap->samples[i];

        sample->color += rs->getPushedRayColor(j);
        sample->direction += rs->getPushedRayNormalContrib(j);

        Lightmap_Save(lightmaps, lightsurf, cached_lightmap, cached_style);
    }
}

/*
 * ============
 * LightFace_Min
 * ============
 */
static void LightFace_Min(const mbsp_t *bsp, const mface_t *face, const qvec3d &color, vec_t light,
    const lightsurf_t *lightsurf, lightmapdict_t *lightmaps)
{
    const settings::worldspawn_keys &cfg = *lightsurf->cfg;
    const modelinfo_t *modelinfo = lightsurf->modelinfo;

    const surfflags_t &extended_flags = extended_texinfo_flags[face->texinfo];
    if (extended_flags.no_minlight) {
        return; /* this face is excluded from minlight */
    }

    /* Find a style 0 lightmap */
    lightmap_t *lightmap = Lightmap_ForStyle(lightmaps, 0, lightsurf);

    bool hit = false;
    for (int i = 0; i < lightsurf->numpoints; i++) {
        lightsample_t *sample = &lightmap->samples[i];

        vec_t value = light;
        if (cfg.minlightDirt.value()) {
            value *= Dirt_GetScaleFactor(cfg, lightsurf->occlusion[i], NULL, 0.0, lightsurf);
        }
        if (cfg.addminlight.value()) {
            sample->color += color * (value / 255.0);
        } else {
            Light_ClampMin(sample, value, color);
        }

        hit = true;
    }

    if (hit) {
        Lightmap_Save(lightmaps, lightsurf, lightmap, 0);
    }

    // FIXME: Refactor this?
    if (lightsurf->modelinfo->lightignore.value() || extended_flags.light_ignore)
        return;

    /* Cast rays for local minlight entities */
    for (const auto &entity : GetLights()) {
        if (entity.getFormula() != LF_LOCALMIN) {
            continue;
        }
        if (entity.nostaticlight.value()) {
            continue;
        }

        if (CullLight(&entity, lightsurf)) {
            continue;
        }

        raystream_occlusion_t *rs = lightsurf->occlusion_stream;
        rs->clearPushedRays();

        lightmap = Lightmap_ForStyle(lightmaps, entity.style.value(), lightsurf);

        hit = false;
        for (int i = 0; i < lightsurf->numpoints; i++) {
            if (lightsurf->occluded[i])
                continue;

            const lightsample_t *sample = &lightmap->samples[i];
            const qvec3d &surfpoint = lightsurf->points[i];
            if (cfg.addminlight.value() || LightSample_Brightness(sample->color) < entity.light.value()) {
                qvec3d surfpointToLightDir;
                const vec_t surfpointToLightDist = GetDir(surfpoint, entity.origin.value(), surfpointToLightDir);

                rs->pushRay(i, surfpoint, surfpointToLightDir, surfpointToLightDist);
            }
        }

        // local minlight just needs occlusion, not closest hit
        rs->tracePushedRaysOcclusion(modelinfo);
        total_light_rays += rs->numPushedRays();

        const int N = rs->numPushedRays();
        for (int j = 0; j < N; j++) {
            if (rs->getPushedRayOccluded(j)) {
                continue;
            }

            int i = rs->getPushedRayPointIndex(j);
            vec_t value = entity.light.value();
            lightsample_t *sample = &lightmap->samples[i];

            value *=
                Dirt_GetScaleFactor(cfg, lightsurf->occlusion[i], &entity, 0.0 /* TODO: pass distance */, lightsurf);
            if (cfg.addminlight.value()) {
                sample->color += entity.color.value() * (value / 255.0);
            } else {
                Light_ClampMin(sample, value, entity.color.value());
            }

            hit = true;
            total_light_ray_hits++;
        }

        if (hit) {
            Lightmap_Save(lightmaps, lightsurf, lightmap, entity.style.value());
        }
    }
}

/*
 * =============
 * LightFace_DirtDebug
 * =============
 */
static void LightFace_DirtDebug(const lightsurf_t *lightsurf, lightmapdict_t *lightmaps)
{
    const settings::worldspawn_keys &cfg = *lightsurf->cfg;
    /* use a style 0 light map */
    lightmap_t *lightmap = Lightmap_ForStyle(lightmaps, 0, lightsurf);

    /* Overwrite each point with the dirt value for that sample... */
    for (int i = 0; i < lightsurf->numpoints; i++) {
        lightsample_t *sample = &lightmap->samples[i];
        const float light = 255 * Dirt_GetScaleFactor(cfg, lightsurf->occlusion[i], NULL, 0.0, lightsurf);
        sample->color = {light};
    }

    Lightmap_Save(lightmaps, lightsurf, lightmap, 0);
}

/*
 * =============
 * LightFace_PhongDebug
 * =============
 */
static void LightFace_PhongDebug(const lightsurf_t *lightsurf, lightmapdict_t *lightmaps)
{
    /* use a style 0 light map */
    lightmap_t *lightmap = Lightmap_ForStyle(lightmaps, 0, lightsurf);

    /* Overwrite each point with the normal for that sample... */
    for (int i = 0; i < lightsurf->numpoints; i++) {
        lightsample_t *sample = &lightmap->samples[i];
        // scale from [-1..1] to [0..1], then multiply by 255
        sample->color = lightsurf->normals[i];

        for (auto &v : sample->color) {
            v = abs(v) * 255;
        }
    }

    Lightmap_Save(lightmaps, lightsurf, lightmap, 0);
}

static void LightFace_BounceLightsDebug(const lightsurf_t *lightsurf, lightmapdict_t *lightmaps)
{
    Q_assert(options.debugmode == debugmodes::bouncelights);

    // reset all lightmaps to black (lazily)
    Lightmap_ClearAll(lightmaps);

    const std::vector<int> &vpls = BounceLightsForFaceNum(Face_GetNum(lightsurf->bsp, lightsurf->face));
    const std::vector<bouncelight_t> &all_vpls = BounceLights();

    /* Overwrite each point with the emitted color... */
    for (int i = 0; i < lightsurf->numpoints; i++) {
        if (lightsurf->occluded[i])
            continue;

        for (const auto &vplnum : vpls) {
            const bouncelight_t &vpl = all_vpls[vplnum];

            // check for point in polygon (note: could be on the edge of more than one)
            if (!GLM_EdgePlanes_PointInside(vpl.poly_edgeplanes, lightsurf->points[i]))
                continue;

            for (const auto &styleColor : vpl.colorByStyle) {
                const qvec3f patch_color = styleColor.second * 255.0f;

                lightmap_t *lightmap = Lightmap_ForStyle(lightmaps, styleColor.first, lightsurf);
                lightsample_t *sample = &lightmap->samples[i];
                sample->color = patch_color;
                Lightmap_Save(lightmaps, lightsurf, lightmap, styleColor.first);
            }
        }
    }
}

// returns color in [0,255]
inline qvec3f BounceLight_ColorAtDist(
    const settings::worldspawn_keys &cfg, float area, const qvec3f &bounceLightColor, float dist)
{
    // clamp away hotspots
    if (dist < 128.0f) {
        dist = 128.0f;
    }

    const float dist2 = (dist * dist);
    const float scale = (1.0f / dist2) * cfg.bouncescale.value();

    // get light contribution
    const qvec3f result = bounceLightColor * area * (255.0f * scale);
    return result;
}

// mxd. Surface light falloff. Returns color in [0,255]
static qvec3f SurfaceLight_ColorAtDist(
    const settings::worldspawn_keys &cfg, const float intensity, const qvec3f color, const float dist)
{
    // Exponential falloff
    const float d = max(dist, 16.0f); // Clamp away hotspots, also avoid division by 0...
    const float scaledintensity = intensity * cfg.surflightscale.value();
    const float scale = (1.0f / (d * d));

    return color * scaledintensity * scale;
}

// dir: vpl -> sample point direction
// returns color in [0,255]
inline qvec3f GetIndirectLighting(const settings::worldspawn_keys &cfg, const bouncelight_t *vpl,
    const qvec3f &bounceLightColor, const qvec3f &dir, const float dist, const qvec3f &origin, const qvec3f &normal)
{
    const float dp1 = qv::dot(vpl->surfnormal, dir);
    if (dp1 < 0.0f)
        return {}; // sample point behind vpl

    const qvec3f sp_vpl = dir * -1.0f;
    const float dp2 = qv::dot(sp_vpl, normal);
    if (dp2 < 0.0f)
        return {}; // vpl behind sample face

    // get light contribution
    const qvec3f result = BounceLight_ColorAtDist(cfg, vpl->area, bounceLightColor, dist);

    // apply angle scale
    const qvec3f resultscaled = result * dp1 * dp2;

    Q_assert(!std::isnan(resultscaled[0]));
    Q_assert(!std::isnan(resultscaled[1]));
    Q_assert(!std::isnan(resultscaled[2]));

    return resultscaled;
}

// dir: vpl -> sample point direction
// mxd. returns color in [0,255]
static qvec3f GetSurfaceLighting(const settings::worldspawn_keys &cfg, const surfacelight_t *vpl, const qvec3f &dir,
    const float dist, const qvec3f &normal)
{
    qvec3f result;
    float dotProductFactor = 1.0f;

    const float dp1 = qv::dot(vpl->surfnormal, dir);
    const qvec3f sp_vpl = dir * -1.0f;
    float dp2 = qv::dot(sp_vpl, normal);

    if (!vpl->omnidirectional) {
        if (dp1 < 0.0f)
            return {0}; // sample point behind vpl
        if (dp2 < 0.0f)
            return {0}; // vpl behind sample face

        dp2 =
            0.5f + dp2 * 0.5f; // Rescale a bit to brighten the faces nearly-perpendicular to the surface light plane...

        dotProductFactor = dp1 * dp2;
    } else {
        // used for sky face surface lights
        dotProductFactor = dp2;
    }

    // Get light contribution
    result = SurfaceLight_ColorAtDist(cfg, vpl->intensity, vpl->color, dist);

    // Apply angle scale
    const qvec3f resultscaled = result * dotProductFactor;

    Q_assert(!std::isnan(resultscaled[0]) && !std::isnan(resultscaled[1]) && !std::isnan(resultscaled[2]));
    return resultscaled;
}

inline bool BounceLight_SphereCull(const mbsp_t *bsp, const bouncelight_t *vpl, const lightsurf_t *lightsurf)
{
    const settings::worldspawn_keys &cfg = *lightsurf->cfg;

    if (!options.novisapprox.value() && vpl->bounds.disjoint(lightsurf->bounds, 0.001))
        return true;

    const qvec3f dir = qvec3f(lightsurf->origin) - vpl->pos; // vpl -> sample point
    const float dist = qv::length(dir) + lightsurf->radius;

    // get light contribution
    const qvec3f color = BounceLight_ColorAtDist(cfg, vpl->area, vpl->componentwiseMaxColor, dist);

    return LightSample_Brightness(color) < 0.25f;
}

static bool // mxd
SurfaceLight_SphereCull(const surfacelight_t *vpl, const lightsurf_t *lightsurf)
{
    if (!options.novisapprox.value() && vpl->bounds.disjoint(lightsurf->bounds, 0.001))
        return true;

    const settings::worldspawn_keys &cfg = *lightsurf->cfg;
    const qvec3f dir = qvec3f(lightsurf->origin) - qvec3f(vpl->pos); // vpl -> sample point
    const float dist = qv::length(dir) + lightsurf->radius;

    // Get light contribution
    const qvec3f color = SurfaceLight_ColorAtDist(cfg, vpl->totalintensity, qvec3f(vpl->color), dist);

    return LightSample_Brightness(color) < 0.25f;
}

#if 0
inline qvec3d CosineWeightedHemisphereSample(float u1, float u2)
{
    Q_assert(u1 >= 0.0 && u1 <= 1.0);
    Q_assert(u2 >= 0.0 && u2 <= 1.0);

    // Generate a uniform sample on the unit disk
    // http://mathworld.wolfram.com/DiskPointPicking.html
    const vec_t sqrt_u1 = sqrt(u1);
    const vec_t theta = 2.0 * Q_PI * u2;

    const vec_t x = sqrt_u1 * cos(theta);
    const vec_t y = sqrt_u1 * sin(theta);

    // Project it up onto the sphere (calculate z)
    //
    // We know sqrt(x^2 + y^2 + z^2) = 1
    // so      x^2 + y^2 + z^2 = 1
    //         z = sqrt(1 - x^2 - y^2)

    const vec_t temp = 1.0 - x * x - y * y;
    const vec_t z = sqrt(max(0.0, temp));

    return { x, y, z };
}
#endif

static void LightFace_Bounce(
    const mbsp_t *bsp, const mface_t *face, const lightsurf_t *lightsurf, lightmapdict_t *lightmaps)
{
    const settings::worldspawn_keys &cfg = *lightsurf->cfg;
    // const dmodelh2_t *shadowself = lightsurf->modelinfo->shadowself.boolValue() ? lightsurf->modelinfo->model : NULL;

    if (!cfg.bounce.value())
        return;

    if (!(options.debugmode == debugmodes::bounce || options.debugmode == debugmodes::none))
        return;

#if 1
    for (const bouncelight_t &vpl : BounceLights()) {
        if (BounceLight_SphereCull(bsp, &vpl, lightsurf))
            continue;

        // FIXME: This will trace the same ray multiple times, once per style,
        // if the bouncelight is hitting a face with multiple styles.
        for (const auto &styleColor : vpl.colorByStyle) {
            bool hit = false;
            const int style = styleColor.first;
            const qvec3f &color = styleColor.second;

            raystream_occlusion_t *rs = lightsurf->occlusion_stream;
            rs->clearPushedRays();

            for (int i = 0; i < lightsurf->numpoints; i++) {
                if (lightsurf->occluded[i])
                    continue;

                qvec3f dir = qvec3f(lightsurf->points[i]) - vpl.pos; // vpl -> sample point
                const float dist = qv::length(dir);
                if (dist == 0.0f)
                    continue; // FIXME: nudge or something
                dir /= dist;

                const qvec3d indirect =
                    GetIndirectLighting(cfg, &vpl, color, dir, dist, lightsurf->points[i], lightsurf->normals[i]);

                if (LightSample_Brightness(indirect) < 0.25)
                    continue;

                rs->pushRay(i, vpl.pos, dir, dist, &indirect);
            }

            if (!rs->numPushedRays())
                continue;

            total_bounce_rays += rs->numPushedRays();
            rs->tracePushedRaysOcclusion(lightsurf->modelinfo);

            lightmap_t *lightmap = Lightmap_ForStyle(lightmaps, style, lightsurf);

            const int N = rs->numPushedRays();
            for (int j = 0; j < N; j++) {
                if (rs->getPushedRayOccluded(j))
                    continue;

                const int i = rs->getPushedRayPointIndex(j);
                qvec3d indirect = rs->getPushedRayColor(j);

                Q_assert(!std::isnan(indirect[0]));

                /* Use dirt scaling on the indirect lighting.
                 * Except, not in bouncedebug mode.
                 */
                if (options.debugmode != debugmodes::bounce) {
                    const vec_t dirtscale = Dirt_GetScaleFactor(cfg, lightsurf->occlusion[i], NULL, 0.0, lightsurf);
                    indirect *= dirtscale;
                }

                lightsample_t *sample = &lightmap->samples[i];
                sample->color += indirect;

                hit = true;
                ++total_bounce_ray_hits;
            }

            // If this style of this bounce light contributed anything, save.
            if (hit)
                Lightmap_Save(lightmaps, lightsurf, lightmap, style);
        }
    }

#else

    lightmap_t *lightmap = Lightmap_ForStyle(lightmaps, 0, lightsurf);

    const int N = 1024;
    raystream_t *rs = MakeRayStream(N);

    for (int i = 0; i < lightsurf->numpoints; i++) {
        if (lightsurf->occluded[i])
            continue;

        const qvec3f surfpoint = vec3_t_to_glm(lightsurf->points[i]);
        const qvec3f surfnormal = vec3_t_to_glm(lightsurf->normals[i]);

        const qmat3x3f rotationMatrix = RotateFromUpToSurfaceNormal(surfnormal);

        rs->clearPushedRays();

        for (int j = 0; j < N; j++) {
            const qvec3f randomDirInUpCoordSystem = CosineWeightedHemisphereSample(Random(), Random());
            const qvec3f rayDir = rotationMatrix * randomDirInUpCoordSystem;

            if (!(qv::dot(rayDir, surfnormal) > -0.01)) {
                // printf("bad dot\n");
            }

            rs->pushRay(i, surfpoint, rayDir, 8192);
        }

        rs->tracePushedRaysIntersection(lightsurf->modelinfo);

        qvec3f colorAvg{};
        int Nhits = 0;

        for (int j = 0; j < N; j++) {
            if (rs->getPushedRayHitType(j) == hittype_t::SKY)
                continue;

            const mface_t *face = rs->getPushedRayHitFace(j);
            if (face == nullptr)
                continue;

            const int fnum = Face_GetNum(bsp, face);
            const auto lights = BounceLightsForFaceNum(fnum); // FIXME: Slow
            if (lights.empty())
                continue;

            Q_assert(lights.size() == 1);
            const bouncelight_t &vpl = lights[0];

            const auto it = vpl.colorByStyle.find(0);
            if (it == vpl.colorByStyle.end())
                continue;
            const qvec3f color = it->second;

            const qvec3f raydir = rs->getPushedRayDir(j);
            if (!(qv::dot(raydir, surfnormal) > -0.01)) {
                // printf("bad dot\n");
                continue;
            }

            if (!(fabs(1.0f - qv::length(raydir)) < 0.1)) {
                // fmt::print("bad raydir: {} {} {} (len {})\n", raydir.x, raydir.y, raydir.z, qv::length(raydir));
                continue;
            }

            const float dist = rs->getPushedRayHitDist(j);
            if (dist <= 0) {
                // printf("bad dist\n");
                continue;
            }

            const qplane3f plane = Face_Plane_E(bsp, face);
            float scale = qv::dot(plane.normal(), -raydir);
            if (scale < 0)
                scale = 0;

            // const qvec3f indirect = GetIndirectLighting(cfg, &vpl, color, -raydir, dist, surfpoint, surfnormal);

            Q_assert(!std::isnan(color.x));
            Q_assert(!std::isnan(color.y));
            Q_assert(!std::isnan(color.z));

            colorAvg += (color * scale * 255.0f);
            Nhits++;
        }

        if (Nhits) {
            colorAvg /= Nhits;

            lightsample_t *sample = &lightmap->samples[i];
            sample->color += colorAvg;
        }
    }

    Lightmap_Save(lightmaps, lightsurf, lightmap, 0);
    delete rs;

#endif
}

static void // mxd
LightFace_SurfaceLight(const lightsurf_t *lightsurf, lightmapdict_t *lightmaps)
{
    const settings::worldspawn_keys &cfg = *lightsurf->cfg;

    for (const surfacelight_t &vpl : SurfaceLights()) {
        if (SurfaceLight_SphereCull(&vpl, lightsurf))
            continue;

        raystream_occlusion_t *rs = lightsurf->occlusion_stream;

        for (int c = 0; c < vpl.points.size(); c++) {
            rs->clearPushedRays();

            for (int i = 0; i < lightsurf->numpoints; i++) {
                if (lightsurf->occluded[i])
                    continue;

                const qvec3f lightsurf_pos = lightsurf->points[i];
                const qvec3f lightsurf_normal = lightsurf->normals[i];

                // Push 1 unit behind the surflight (fixes darkening near surflight face on neighbouring faces)
                qvec3f pos = vpl.points[c] - vpl.surfnormal;
                qvec3f dir = lightsurf_pos - pos;
                float dist = qv::length(dir);

                if (dist == 0.0f)
                    dir = lightsurf_normal;
                else
                    dir /= dist;

                const qvec3d indirect = GetSurfaceLighting(cfg, &vpl, dir, dist, lightsurf_normal);
                if (LightSample_Brightness(indirect) < 0.01f) // Each point contributes very little to the final result
                    continue;

                // Push 1 unit in front of the surflight, so embree can properly process it ...
                pos = vpl.points[c] + vpl.surfnormal;
                dir = lightsurf_pos - pos;
                dist = qv::length(dir);

                if (dist == 0.0f)
                    dir = lightsurf_normal;
                else
                    dir /= dist;

                rs->pushRay(i, pos, dir, dist, &indirect);
            }

            if (!rs->numPushedRays())
                continue;

            total_surflight_rays += rs->numPushedRays();
            rs->tracePushedRaysOcclusion(lightsurf->modelinfo);

            const int lightmapstyle = 0;
            lightmap_t *lightmap = Lightmap_ForStyle(lightmaps, lightmapstyle, lightsurf);

            bool hit = false;
            const int numrays = rs->numPushedRays();
            for (int j = 0; j < numrays; j++) {
                if (rs->getPushedRayOccluded(j))
                    continue;

                const int i = rs->getPushedRayPointIndex(j);
                qvec3d indirect = rs->getPushedRayColor(j);

                Q_assert(!std::isnan(indirect[0]));

                // Use dirt scaling on the surface lighting.
                const vec_t dirtscale = Dirt_GetScaleFactor(cfg, lightsurf->occlusion[i], nullptr, 0.0, lightsurf);
                indirect *= dirtscale;

                lightsample_t *sample = &lightmap->samples[i];
                sample->color += indirect;

                hit = true;
                ++total_surflight_ray_hits;
            }

            // If surface light contributed anything, save.
            if (hit)
                Lightmap_Save(lightmaps, lightsurf, lightmap, lightmapstyle);
        }
    }
}

static void LightFace_OccludedDebug(lightsurf_t *lightsurf, lightmapdict_t *lightmaps)
{
    Q_assert(options.debugmode == debugmodes::debugoccluded);

    /* use a style 0 light map */
    lightmap_t *lightmap = Lightmap_ForStyle(lightmaps, 0, lightsurf);

    /* Overwrite each point, red=occluded, green=ok */
    for (int i = 0; i < lightsurf->numpoints; i++) {
        lightsample_t *sample = &lightmap->samples[i];
        if (lightsurf->occluded[i]) {
            sample->color = {255, 0, 0};
        } else {
            sample->color = {0, 255, 0};
        }
        // N.B.: Mark it as un-occluded now, to disable special handling later in the -extra/-extra4 downscaling code
        lightsurf->occluded[i] = false;
    }

    Lightmap_Save(lightmaps, lightsurf, lightmap, 0);
}

static void LightFace_DebugNeighbours(lightsurf_t *lightsurf, lightmapdict_t *lightmaps)
{
    Q_assert(options.debugmode == debugmodes::debugneighbours);

    /* use a style 0 light map */
    lightmap_t *lightmap = Lightmap_ForStyle(lightmaps, 0, lightsurf);

    //    std::vector<neighbour_t> neighbours = NeighbouringFaces_new(lightsurf->bsp, BSP_GetFace(lightsurf->bsp,
    //    dump_facenum)); bool found = false; for (auto &f : neighbours) {
    //        if (f.face == lightsurf->face)
    //            found = true;
    //    }

    bool has_sample_on_dumpface = false;
    for (int i = 0; i < lightsurf->numpoints; i++) {
        if (lightsurf->realfacenums[i] == dump_facenum) {
            has_sample_on_dumpface = true;
            break;
        }
    }

    /* Overwrite each point */
    for (int i = 0; i < lightsurf->numpoints; i++) {
        lightsample_t *sample = &lightmap->samples[i];
        const int sample_face = lightsurf->realfacenums[i];

        if (sample_face == dump_facenum) {
            /* Red - the sample is on the selected face */
            sample->color = {255, 0, 0};
        } else if (has_sample_on_dumpface) {
            /* Green - the face has some samples on the selected face */
            sample->color = {0, 255, 0};
        } else {
            sample->color = {};
        }
        // N.B.: Mark it as un-occluded now, to disable special handling later in the -extra/-extra4 downscaling code
        lightsurf->occluded[i] = false;
    }

    Lightmap_Save(lightmaps, lightsurf, lightmap, 0);
}

/* Dirtmapping borrowed from q3map2, originally by RaP7oR */

#define DIRT_NUM_ANGLE_STEPS 16
#define DIRT_NUM_ELEVATION_STEPS 3
#define DIRT_NUM_VECTORS (DIRT_NUM_ANGLE_STEPS * DIRT_NUM_ELEVATION_STEPS)

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

    if (!cfg.globalDirt.value() && cfg.globalDirt.isLocked()) {
        // HACK: "-dirt 0" disables all dirtmapping even if we would otherwise use it.
        dirt_in_use = false;
        return;
    }

    if (cfg.globalDirt.value() || cfg.minlightDirt.value() || cfg.sunlight_dirt.boolValue() ||
        cfg.sunlight2_dirt.boolValue()) {
        dirt_in_use = true;
    }

    if (!dirt_in_use) {
        // check entities, maybe only a few lights use it
        for (const auto &light : GetLights()) {
            if (light.dirt.boolValue()) {
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
    LogPrint("--- SetupDirt ---\n");

    /* calculate angular steps */
    constexpr float angleStep = (float)DEG2RAD(360.0f / DIRT_NUM_ANGLE_STEPS);
    const float elevationStep = (float)DEG2RAD(cfg.dirtAngle.value() / DIRT_NUM_ELEVATION_STEPS);

    /* iterate angle */
    float angle = 0.0f;
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
    LogPrint("{:9} dirtmap vectors\n", numDirtVectors);
}

// from q3map2
static void GetUpRtVecs(const qvec3d &normal, qvec3d &myUp, qvec3d &myRt)
{
    /* check if the normal is aligned to the world-up */
    if (normal[0] == 0.0f && normal[1] == 0.0f) {
        if (normal[2] == 1.0f) {
            myRt = {1.0, 0.0, 0.0};
            myUp = {0.0, 1.0, 0.0};
        } else if (normal[2] == -1.0f) {
            myRt = {-1.0, 0.0, 0.0};
            myUp = {0.0, 1.0, 0.0};
        }
    } else {
        constexpr qvec3d worldUp{0, 0, 1};
        myRt = qv::normalize(qv::cross(normal, worldUp));
        myUp = qv::normalize(qv::cross(myRt, normal));
    }
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

    if (cfg.dirtMode.value() == 1) {
        /* get random vector */
        float angle = Random() * DEG2RAD(360.0f);
        float elevation = Random() * DEG2RAD(cfg.dirtAngle.value());
        return {cos(angle) * sin(elevation), sin(angle) * sin(elevation), cos(elevation)};
    }

    return dirtVectors[i];
}

float DirtAtPoint(const settings::worldspawn_keys &cfg, raystream_intersection_t *rs, const qvec3d &point,
    const qvec3d &normal, const modelinfo_t *selfshadow)
{
    if (!dirt_in_use) {
        return 0.0f;
    }

    qvec3d myUp, myRt;
    float occlusion = 0;

    // this stuff is just per-point

    GetUpRtVecs(normal, myUp, myRt);

    rs->clearPushedRays();

    for (int j = 0; j < numDirtVectors; j++) {

        // fill in input buffers
        qvec3d dirtvec = GetDirtVector(cfg, j);
        qvec3d dir = TransformToTangentSpace(normal, myUp, myRt, dirtvec);

        rs->pushRay(j, point, dir, cfg.dirtDepth.value());
    }

    Q_assert(rs->numPushedRays() == numDirtVectors);

    // trace the batch
    rs->tracePushedRaysIntersection(selfshadow);

    // accumulate hitdists
    for (int j = 0; j < numDirtVectors; j++) {
        if (rs->getPushedRayHitType(j) == hittype_t::SOLID) {
            const vec_t dist = rs->getPushedRayHitDist(j);
            occlusion += min(cfg.dirtDepth.value(), dist);
        } else {
            occlusion += cfg.dirtDepth.value();
        }
    }

    // process the results.
    const vec_t avgHitdist = occlusion / numDirtVectors;
    occlusion = 1 - (avgHitdist / cfg.dirtDepth.value());
    return occlusion;
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

    qvec3d *myUps = new qvec3d[lightsurf->numpoints];
    qvec3d *myRts = new qvec3d[lightsurf->numpoints];

    // init
    for (int i = 0; i < lightsurf->numpoints; i++) {
        lightsurf->occlusion[i] = 0;
    }

    // this stuff is just per-point
    for (int i = 0; i < lightsurf->numpoints; i++) {
        GetUpRtVecs(lightsurf->normals[i], myUps[i], myRts[i]);
    }

    for (int j = 0; j < numDirtVectors; j++) {
        raystream_intersection_t *rs = lightsurf->intersection_stream;
        rs->clearPushedRays();

        // fill in input buffers

        for (int i = 0; i < lightsurf->numpoints; i++) {
            if (lightsurf->occluded[i])
                continue;

            qvec3d dirtvec = GetDirtVector(cfg, j);
            qvec3d dir = TransformToTangentSpace(lightsurf->normals[i], myUps[i], myRts[i], dirtvec);

            rs->pushRay(i, lightsurf->points[i], dir, cfg.dirtDepth.value());
        }

        // trace the batch. need closest hit for dirt, so intersection.
        rs->tracePushedRaysIntersection(lightsurf->modelinfo);

        // accumulate hitdists
        for (int k = 0; k < rs->numPushedRays(); k++) {
            const int i = rs->getPushedRayPointIndex(k);
            if (rs->getPushedRayHitType(k) == hittype_t::SOLID) {
                vec_t dist = rs->getPushedRayHitDist(k);
                lightsurf->occlusion[i] += min(cfg.dirtDepth.value(), dist);
            } else {
                lightsurf->occlusion[i] += cfg.dirtDepth.value();
            }
        }
    }

    // process the results.
    for (int i = 0; i < lightsurf->numpoints; i++) {
        vec_t avgHitdist = lightsurf->occlusion[i] / (float)numDirtVectors;
        lightsurf->occlusion[i] = 1 - (avgHitdist / cfg.dirtDepth.value());
    }

    delete[] myUps;
    delete[] myRts;
}

// clamps negative values. applies gamma and rangescale. clamps values over 255
// N.B. we want to do this before smoothing / downscaling, so huge values don't mess up the averaging.
static void LightFace_ScaleAndClamp(const lightsurf_t *lightsurf, lightmapdict_t *lightmaps)
{
    const settings::worldspawn_keys &cfg = *lightsurf->cfg;

    for (lightmap_t &lightmap : *lightmaps) {
        for (int i = 0; i < lightsurf->numpoints; i++) {
            qvec3d &color = lightmap.samples[i].color;

            /* Fix any negative values */
            color = qv::max(color, {0});

            /* Scale and clamp any out-of-range samples */
            color *= cfg.rangescale.value();

            for (auto &c : color) {
                c = pow(c / 255.0f, 1.0 / cfg.lightmapgamma.value()) * 255.0f;
            }

            vec_t maxcolor = qv::max(color);

            if (maxcolor > 255) {
                color *= (255.0f / maxcolor);
            }
        }
    }
}

static float Lightmap_AvgBrightness(const lightmap_t *lm, const lightsurf_t *lightsurf)
{
    float avgb = 0;
    for (int j = 0; j < lightsurf->numpoints; j++) {
        avgb += LightSample_Brightness(lm->samples[j].color);
    }
    avgb /= lightsurf->numpoints;
    return avgb;
}

static float Lightmap_MaxBrightness(const lightmap_t *lm, const lightsurf_t *lightsurf)
{
    float maxb = 0;
    for (int j = 0; j < lightsurf->numpoints; j++) {
        const float b = LightSample_Brightness(lm->samples[j].color);
        if (b > maxb) {
            maxb = b;
        }
    }
    return maxb;
}

#if 0
static void WritePPM(const std::filesystem::path &fname, int width, int height, const uint8_t *rgbdata)
{
    qfile_t file = SafeOpenWrite(fname);

    // see: http://netpbm.sourceforge.net/doc/ppm.html
    fmt::print(file.get(), "P6 {} {} 255 ", width, height);
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
    for (int i = 0; i < lightsurf->numpoints; i++) {
        const qvec3d &color = lm->samples[i].color;
        const float alpha = lightsurf->occluded[i] ? 0.0f : 1.0f;
        res.emplace_back(color[0], color[1], color[2], alpha);
    }
    return res;
}

static std::vector<qvec4f> LightmapNormalsToGLMVector(const lightsurf_t *lightsurf, const lightmap_t *lm)
{
    std::vector<qvec4f> res;
    for (int i = 0; i < lightsurf->numpoints; i++) {
        const qvec3d &color = lm->samples[i].direction;
        const float alpha = lightsurf->occluded[i] ? 0.0f : 1.0f;
        res.emplace_back(color[0], color[1], color[2], alpha);
    }
    return res;
}

#if 0
static std::vector<qvec4f> LightmapToGLMVector(const mbsp_t *bsp, const lightsurf_t *lightsurf)
{
    const lightmap_t *lm = Lightmap_ForStyle_ReadOnly(lightsurf, 0);
    if (lm != nullptr) {
        return LightmapColorsToGLMVector(lightsurf, lm);
    }
    return std::vector<qvec4f>();
}
#endif

static qvec3f LinearToGamma22(const qvec3f &c)
{
    return qv::pow(c, qvec3f(1 / 2.2f));
}

static qvec3f Gamma22ToLinear(const qvec3f &c)
{
    return qv::pow(c, qvec3f(2.2f));
}

void GLMVector_GammaToLinear(std::vector<qvec3f> &vec)
{
    for (auto &v : vec) {
        v = Gamma22ToLinear(v);
    }
}

void GLMVector_LinearToGamma(std::vector<qvec3f> &vec)
{
    for (auto &v : vec) {
        v = LinearToGamma22(v);
    }
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
            // FLogPrint("warning, fully transparent lightmap\n");
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
                    const int x1 = clamp(x + x0, 0, w - 1);
                    const int y1 = clamp(y + y0, 0, h - 1);

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

static void WriteSingleLightmap(const mbsp_t *bsp, const mface_t *face, const lightsurf_t *lightsurf,
    const lightmap_t *lm, const int actual_width, const int actual_height, uint8_t *out, uint8_t *lit, uint8_t *lux);

static void WriteLightmaps(
    const mbsp_t *bsp, mface_t *face, facesup_t *facesup, const lightsurf_t *lightsurf, const lightmapdict_t *lightmaps)
{
    const int actual_width = lightsurf->texsize[0] + 1;
    const int actual_height = lightsurf->texsize[1] + 1;

    if (options.litonly.value()) {
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
            for (const lightmap_t &lm : *lightmaps) {
                if (lm.style == style) {
                    WriteSingleLightmap(bsp, face, lightsurf, &lm, actual_width, actual_height, out, lit, lux);
                    break;
                }
            }
            // if we didn't find a matching lightmap, just don't write anything

            out += (actual_width * actual_height);
            lit += (actual_width * actual_height * 3);
            lux += (actual_width * actual_height * 3);
        }

        return;
    }

    // intermediate collection for sorting lightmaps
    std::vector<std::pair<float, const lightmap_t *>> sortable;

    for (const lightmap_t &lightmap : *lightmaps) {
        // skip un-saved lightmaps
        if (lightmap.style == 255)
            continue;

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

    // sort in descending order of average brightness
    std::sort(sortable.begin(), sortable.end());
    std::reverse(sortable.begin(), sortable.end());

    std::vector<const lightmap_t *> sorted;
    for (const auto &pair : sortable) {
        if (sorted.size() == MAXLIGHTMAPS) {
            LogPrint("WARNING: Too many light styles on a face\n"
                     "         lightmap point near [{}]\n",
                lightsurf->points[0]);
            break;
        }

        sorted.push_back(pair.second);
    }

    /* final number of lightmaps */
    const int numstyles = static_cast<int>(sorted.size());
    Q_assert(numstyles <= MAXLIGHTMAPS);

    /* update face info (either core data or supplementary stuff) */
    if (facesup) {
        facesup->extent[0] = lightsurf->texsize[0] + 1;
        facesup->extent[1] = lightsurf->texsize[1] + 1;
        int mapnum;
        for (mapnum = 0; mapnum < numstyles; mapnum++) {
            facesup->styles[mapnum] = sorted.at(mapnum)->style;
        }
        for (; mapnum < MAXLIGHTMAPS; mapnum++) {
            facesup->styles[mapnum] = 255;
        }
        facesup->lmscale = lightsurf->lightmapscale;
    } else {
        int mapnum;
        for (mapnum = 0; mapnum < numstyles; mapnum++) {
            face->styles[mapnum] = sorted.at(mapnum)->style;
        }
        for (; mapnum < MAXLIGHTMAPS; mapnum++) {
            face->styles[mapnum] = 255;
        }
    }

    if (!numstyles)
        return;

    const int size = (lightsurf->texsize[0] + 1) * (lightsurf->texsize[1] + 1);

    uint8_t *out, *lit, *lux;
    GetFileSpace(&out, &lit, &lux, size * numstyles);

    int lightofs;

    // Q2/HL native colored lightmaps
    if (bsp->loadversion->game->has_rgb_lightmap) {
        lightofs = lit - lit_filebase;
    } else {
        lightofs = out - filebase;
    }

    if (facesup) {
        facesup->lightofs = lightofs;
    } else {
        face->lightofs = lightofs;
    }

    // sanity check that we don't save a lightmap for a non-lightmapped face
    {
        const char *texname = Face_TextureName(bsp, face);
        Q_assert(Face_IsLightmapped(bsp, face));
        Q_assert(Q_strcasecmp(texname, "skip") != 0);
        Q_assert(Q_strcasecmp(texname, "trigger") != 0);
    }

    for (int mapnum = 0; mapnum < numstyles; mapnum++) {
        const lightmap_t *lm = sorted.at(mapnum);

        WriteSingleLightmap(bsp, face, lightsurf, lm, actual_width, actual_height, out, lit, lux);

        out += (actual_width * actual_height);
        lit += (actual_width * actual_height * 3);
        lux += (actual_width * actual_height * 3);
    }
}

/**
 * - Writes (actual_width * actual_height) bytes to `out`
 * - Writes (actual_width * actual_height * 3) bytes to `lit`
 * - Writes (actual_width * actual_height * 3) bytes to `lux`
 */
static void WriteSingleLightmap(const mbsp_t *bsp, const mface_t *face, const lightsurf_t *lightsurf,
    const lightmap_t *lm, const int actual_width, const int actual_height, uint8_t *out, uint8_t *lit, uint8_t *lux)
{
    const int oversampled_width = actual_width * options.extra.value();
    const int oversampled_height = actual_height * options.extra.value();

    // allocate new float buffers for the output colors and directions
    // these are the actual output width*height, without oversampling.

    std::vector<qvec4f> fullres = LightmapColorsToGLMVector(lightsurf, lm);

    if (options.highlightseams.value()) {
        fullres = HighlightSeams(fullres, oversampled_width, oversampled_height);
    }

    // removes all transparent pixels by averaging from adjacent pixels
    fullres = FloodFillTransparent(fullres, oversampled_width, oversampled_height);

    if (options.soft.value() > 0) {
        fullres = BoxBlurImage(fullres, oversampled_width, oversampled_height, options.soft.value());
    }

    const std::vector<qvec4f> output_color =
        IntegerDownsampleImage(fullres, oversampled_width, oversampled_height, options.extra.value());
    std::optional<std::vector<qvec4f>> output_dir;

    if (lux) {
        output_dir = IntegerDownsampleImage(
            LightmapNormalsToGLMVector(lightsurf, lm), oversampled_width, oversampled_height, options.extra.value());
    }

    // copy from the float buffers to byte buffers in .bsp / .lit / .lux

    for (int t = 0; t < actual_height; t++) {
        for (int s = 0; s < actual_width; s++) {
            const int sampleindex = (t * actual_width) + s;
            const qvec4f &color = output_color.at(sampleindex);

            *lit++ = color[0];
            *lit++ = color[1];
            *lit++ = color[2];

            /* Take the max() of the 3 components to get the value to write to the
            .bsp lightmap. this avoids issues with some engines
            that require the lit and internal lightmap to have the same
            intensity. (MarkV, some QW engines)

            This must be max(), see LightNormalize in MarkV 1036.
            */
            float light = max({color[0], color[1], color[2]});
            if (light < 0)
                light = 0;
            if (light > 255)
                light = 255;
            *out++ = light;

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

static void LightFaceShutdown(lightsurf_t *lightsurf)
{
    for (auto &lm : lightsurf->lightmapsByStyle) {
        delete[] lm.samples;
    }

    delete[] lightsurf->points;
    delete[] lightsurf->normals;
    delete[] lightsurf->occlusion;
    delete[] lightsurf->occluded;
    delete[] lightsurf->realfacenums;

    delete lightsurf->occlusion_stream;
    delete lightsurf->intersection_stream;

    delete lightsurf;
}

/*
 * ============
 * LightFace
 * ============
 */
void LightFace(const mbsp_t *bsp, mface_t *face, facesup_t *facesup, const settings::worldspawn_keys &cfg)
{
    /* Find the correct model offset */
    const modelinfo_t *modelinfo = ModelInfoForFace(bsp, Face_GetNum(bsp, face));
    if (modelinfo == nullptr) {
        return;
    }

    /* One extra lightmap is allocated to simplify handling overflow */

    if (!options.litonly.value()) {
        // if litonly is set we need to preserve the existing lightofs

        /* some surfaces don't need lightmaps */
        if (facesup) {
            facesup->lightofs = -1;
            for (int i = 0; i < MAXLIGHTMAPS; i++)
                facesup->styles[i] = 255;
        } else {
            face->lightofs = -1;
            for (int i = 0; i < MAXLIGHTMAPS; i++)
                face->styles[i] = 255;
        }
    }

    /* don't bother with degenerate faces */
    if (face->numedges < 3)
        return;

    if (!Face_IsLightmapped(bsp, face))
        return;

    const char *texname = Face_TextureName(bsp, face);

    /* don't save lightmaps for "trigger" texture */
    if (!Q_strcasecmp(texname, "trigger"))
        return;

    /* don't save lightmaps for "skip" texture */
    if (!Q_strcasecmp(texname, "skip"))
        return;

    /* all good, this face is going to be lightmapped. */
    lightsurf_t *lightsurf = new lightsurf_t{};
    lightsurf->cfg = &cfg;

    /* if liquid doesn't have the TEX_SPECIAL flag set, the map was qbsp'ed with
     * lit water in mind. In that case receive light from both top and bottom.
     * (lit will only be rendered in compatible engines, but degrades gracefully.)
     */
    if (Face_IsTranslucent(bsp, face)) {
        lightsurf->twosided = true;
    }

    if (!Lightsurf_Init(modelinfo, face, bsp, lightsurf, facesup)) {
        /* invalid texture axes */
        return;
    }
    lightmapdict_t *lightmaps = &lightsurf->lightmapsByStyle;

    /* calculate dirt (ambient occlusion) but don't use it yet */
    if (dirt_in_use && (options.debugmode != debugmodes::phong))
        LightFace_CalculateDirt(lightsurf);

    /*
     * The lighting procedure is: cast all positive lights, fix
     * minlight levels, then cast all negative lights. Finally, we
     * clamp any values that may have gone negative.
     */

    if (options.debugmode == debugmodes::none) {

        total_samplepoints += lightsurf->numpoints;

        const surfflags_t &extended_flags = extended_texinfo_flags[face->texinfo];

        /* positive lights */
        if (!(modelinfo->lightignore.value() || extended_flags.light_ignore)) {
            for (const auto &entity : GetLights()) {
                if (entity.getFormula() == LF_LOCALMIN)
                    continue;
                if (entity.nostaticlight.value())
                    continue;
                if (entity.light.value() > 0)
                    LightFace_Entity(bsp, &entity, lightsurf, lightmaps);
            }
            for (const sun_t &sun : GetSuns())
                if (sun.sunlight > 0)
                    LightFace_Sky(&sun, lightsurf, lightmaps);

            // mxd. Add surface lights...
            LightFace_SurfaceLight(lightsurf, lightmaps);

            /* add indirect lighting */
            LightFace_Bounce(bsp, face, lightsurf, lightmaps);
        }

        /* minlight - Use Q2 surface light, or the greater of global or model minlight. */
        const gtexinfo_t *texinfo = Face_Texinfo(bsp, face); // mxd. Surface lights...
        if (texinfo != nullptr && texinfo->value > 0 && (texinfo->flags.native & Q2_SURF_LIGHT)) {
            LightFace_Min(bsp, face, Face_LookupTextureColor(bsp, face), texinfo->value * 2.0f, lightsurf,
                lightmaps); // Playing by the eye here... 2.0 == 256 / 128; 128 is the light value, at which the surface
                            // is renered fullbright, when using arghrad3
        } else if (lightsurf->minlight > cfg.minlight.value()) {
            LightFace_Min(bsp, face, lightsurf->minlight_color, lightsurf->minlight, lightsurf, lightmaps);
        } else {
            const float light = cfg.minlight.value();
            const qvec3d &color = cfg.minlight_color.value();

            LightFace_Min(bsp, face, color, light, lightsurf, lightmaps);
        }

        /* negative lights */
        if (!(modelinfo->lightignore.value() || extended_flags.light_ignore)) {
            for (const auto &entity : GetLights()) {
                if (entity.getFormula() == LF_LOCALMIN)
                    continue;
                if (entity.nostaticlight.value())
                    continue;
                if (entity.light.value() < 0)
                    LightFace_Entity(bsp, &entity, lightsurf, lightmaps);
            }
            for (const sun_t &sun : GetSuns())
                if (sun.sunlight < 0)
                    LightFace_Sky(&sun, lightsurf, lightmaps);
        }
    }

    /* bounce debug */
    // TODO: add a BounceDebug function that clear the lightmap to make the code more clear
    if (options.debugmode == debugmodes::bounce)
        LightFace_Bounce(bsp, face, lightsurf, lightmaps);

    /* replace lightmaps with AO for debugging */
    if (options.debugmode == debugmodes::dirt)
        LightFace_DirtDebug(lightsurf, lightmaps);

    if (options.debugmode == debugmodes::phong)
        LightFace_PhongDebug(lightsurf, lightmaps);

    if (options.debugmode == debugmodes::bouncelights)
        LightFace_BounceLightsDebug(lightsurf, lightmaps);

    if (options.debugmode == debugmodes::debugoccluded)
        LightFace_OccludedDebug(lightsurf, lightmaps);

    if (options.debugmode == debugmodes::debugneighbours)
        LightFace_DebugNeighbours(lightsurf, lightmaps);

    /* Apply gamma, rangescale, and clamp */
    LightFace_ScaleAndClamp(lightsurf, lightmaps);

    WriteLightmaps(bsp, face, facesup, lightsurf, lightmaps);

    LightFaceShutdown(lightsurf);
}
