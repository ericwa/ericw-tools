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
#include <light/entities.hh>
#include <light/trace.hh>
#include <light/ltface.hh>

#include <common/bsputils.hh>
#include <common/qvec.hh>

#include <cassert>
#include <cmath>
#include <algorithm>

using namespace std;

std::atomic<uint32_t> total_light_rays, total_light_ray_hits, total_samplepoints;
std::atomic<uint32_t> total_bounce_rays, total_bounce_ray_hits;

/* ======================================================================== */

qvec2f WorldToTexCoord_HighPrecision(const bsp2_t *bsp, const bsp2_dface_t *face, const qvec3f &world)
{
    const texinfo_t *tex = Face_Texinfo(bsp, face);
    if (tex == nullptr)
        return qvec2f(0);
    
    qvec2f coord;
    
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
    for (int i = 0; i < 2; i++) {
        coord[i] = (long double)world[0] * tex->vecs[i][0] +
        (long double)world[1] * tex->vecs[i][1] +
        (long double)world[2] * tex->vecs[i][2] +
        tex->vecs[i][3];
    }
    return coord;
}

faceextents_t::faceextents_t(const bsp2_dface_t *face, const bsp2_t *bsp, float lmscale)
: m_lightmapscale(lmscale)
{
    m_worldToTexCoord = WorldToTexSpace(bsp, face);
    m_texCoordToWorld = TexSpaceToWorld(bsp, face);
    
    qvec2f mins(VECT_MAX, VECT_MAX);
    qvec2f maxs(-VECT_MAX, -VECT_MAX);
    
    for (int i = 0; i < face->numedges; i++) {
        const qvec3f worldpoint = Face_PointAtIndex_E(bsp, face, i);
        const qvec2f texcoord = WorldToTexCoord_HighPrecision(bsp, face, worldpoint);
        
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
            const plane_t plane = Face_Plane(bsp, face);
            const qvec3f point = Face_PointAtIndex_E(bsp, face, 0); // grab first vert
            const char *texname = Face_TextureName(bsp, face);
            
            Error("Bad surface extents:\n"
                  "   surface %d, %s extents = %d, scale = %g\n"
                  "   texture %s at (%s)\n"
                  "   surface normal (%s)\n",
                  Face_GetNum(bsp, face), i ? "t" : "s", m_texsize[i], m_lightmapscale,
                  texname, qv::to_string(point).c_str(),
                  VecStrf(plane.normal));
        }
    }
}

int faceextents_t::width() const { return m_texsize[0] + 1; }
int faceextents_t::height() const { return m_texsize[1] + 1; }
int faceextents_t::numsamples() const { return width() * height(); }
qvec2i faceextents_t::texsize() const { return qvec2i(width(), height()); }

int faceextents_t::indexOf(const qvec2i &lm) const {
    Q_assert(lm[0] >= 0 && lm[0] < width());
    Q_assert(lm[1] >= 0 && lm[1] < height());
    return lm[0] + (width() * lm[1]);
}

qvec2i faceextents_t::intCoordsFromIndex(int index) const {
    Q_assert(index >= 0);
    Q_assert(index < numsamples());
    
    qvec2i res(index % width(), index / width());
    Q_assert(indexOf(res) == index);
    return res;
}

qvec2f faceextents_t::LMCoordToTexCoord(const qvec2f &LMCoord) const {
    const qvec2f res(m_lightmapscale * (m_texmins[0] + LMCoord[0]),
                        m_lightmapscale * (m_texmins[1] + LMCoord[1]));
    return res;
}

qvec2f faceextents_t::TexCoordToLMCoord(const qvec2f &tc) const {
    const qvec2f res((tc[0] / m_lightmapscale) - m_texmins[0],
                        (tc[1] / m_lightmapscale) - m_texmins[1]);
    return res;
}

qvec2f faceextents_t::worldToTexCoord(qvec3f world) const {
    const qvec4f worldPadded(world[0], world[1], world[2], 1.0f);
    const qvec4f res = m_worldToTexCoord * worldPadded;
    
    Q_assert(res[3] == 1.0f);
    
    return qvec2f( res[0], res[1] );
}

qvec3f faceextents_t::texCoordToWorld(qvec2f tc) const {
    const qvec4f tcPadded(tc[0], tc[1], 0.0f, 1.0f);
    const qvec4f res = m_texCoordToWorld * tcPadded;
    
    Q_assert(fabs(res[3] - 1.0f) < 0.01f);
    
    return qvec3f( res[0], res[1], res[2] );
}

qvec2f faceextents_t::worldToLMCoord(qvec3f world) const {
    return TexCoordToLMCoord(worldToTexCoord(world));
}

qvec3f faceextents_t::LMCoordToWorld(qvec2f lm) const {
    return texCoordToWorld(LMCoordToTexCoord(lm));
}

qmat4x4f WorldToTexSpace(const bsp2_t *bsp, const bsp2_dface_t *f)
{
    const texinfo_t *tex = Face_Texinfo(bsp, f);
    if (tex == nullptr) {
        Q_assert_unreachable();
        return qmat4x4f();
    }
    const plane_t plane = Face_Plane(bsp, f);
    const vec_t *norm = plane.normal;
    
    //           [s]
    // T * vec = [t]
    //           [distOffPlane]
    //           [?]
    
    qmat4x4f T  { tex->vecs[0][0], tex->vecs[1][0], norm[0], 0, // col 0
                  tex->vecs[0][1], tex->vecs[1][1], norm[1], 0, // col 1
                  tex->vecs[0][2], tex->vecs[1][2], norm[2], 0, // col 2
                  tex->vecs[0][3], tex->vecs[1][3], -plane.dist, 1 // col 3
                };
    return T;
}

qmat4x4f TexSpaceToWorld(const bsp2_t *bsp, const bsp2_dface_t *f)
{
    const qmat4x4f T = WorldToTexSpace(bsp, f);
    const qmat4x4f inv = qv::inverse(T);
    
    if (std::isnan(inv.at(0,0))) {
        logprint("Bad texture axes on face:\n");
        PrintFaceInfo(f, bsp);
        Error("CreateFaceTransform");
    }
    
    return inv;
}

static void
TexCoordToWorld(vec_t s, vec_t t, const texorg_t *texorg, vec3_t world)
{
    qvec4f worldPos = texorg->texSpaceToWorld * qvec4f(s, t, /* one "unit" in front of surface */ 1.0, 1.0);
    
    glm_to_vec3_t(qvec3f(worldPos), world);
}

void
WorldToTexCoord(const vec3_t world, const texinfo_t *tex, vec_t coord[2])
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
    for (int i = 0; i < 2; i++)
        coord[i] =
            (long double)world[0] * tex->vecs[i][0] +
            (long double)world[1] * tex->vecs[i][1] +
            (long double)world[2] * tex->vecs[i][2] +
                                    tex->vecs[i][3];
}

/* Debug helper - move elsewhere? */
void PrintFaceInfo(const bsp2_dface_t *face, const bsp2_t *bsp)
{
    const texinfo_t *tex = &bsp->texinfo[face->texinfo];
    const char *texname = Face_TextureName(bsp, face);

    logprint("face %d, texture %s, %d edges...\n"
             "  vectors (%3.3f, %3.3f, %3.3f) (%3.3f)\n"
             "          (%3.3f, %3.3f, %3.3f) (%3.3f)\n",
             (int)(face - bsp->dfaces), texname, face->numedges,
             tex->vecs[0][0], tex->vecs[0][1], tex->vecs[0][2], tex->vecs[0][3],
             tex->vecs[1][0], tex->vecs[1][1], tex->vecs[1][2], tex->vecs[1][3]);

    for (int i = 0; i < face->numedges; i++) {
        int edge = bsp->dsurfedges[face->firstedge + i];
        int vert = Face_VertexAtIndex(bsp, face, i);
        const vec_t *point = GetSurfaceVertexPoint(bsp, face, i);
        const qvec3f norm = GetSurfaceVertexNormal(bsp, face, i);
        logprint("%s %3d (%3.3f, %3.3f, %3.3f) :: normal (%3.3f, %3.3f, %3.3f) :: edge %d\n",
                 i ? "          " : "    verts ", vert,
                 point[0], point[1], point[2],
                 norm[0], norm[1], norm[2],
                 edge);
    }
}

/*
 * ================
 * CalcFaceExtents
 * Fills in surf->texmins[], surf->texsize[] and sets surf->exactmid[]
 * ================
 */
static void
CalcFaceExtents(const bsp2_dface_t *face,
                const bsp2_t *bsp, lightsurf_t *surf)
{
    vec_t mins[2], maxs[2], texcoord[2];
    vec3_t worldmaxs, worldmins;

    mins[0] = mins[1] = VECT_MAX;
    maxs[0] = maxs[1] = -VECT_MAX;
    worldmaxs[0] = worldmaxs[1] = worldmaxs[2] = -VECT_MAX;
    worldmins[0] = worldmins[1] = worldmins[2] = VECT_MAX;
    const texinfo_t *tex = &bsp->texinfo[face->texinfo];

    for (int i = 0; i < face->numedges; i++) {
        int edge = bsp->dsurfedges[face->firstedge + i];
        int vert = (edge >= 0) ? bsp->dedges[edge].v[0] : bsp->dedges[-edge].v[1];
        const dvertex_t *dvertex = &bsp->dvertexes[vert];

        vec3_t worldpoint;
        VectorCopy(dvertex->point, worldpoint);
        WorldToTexCoord(worldpoint, tex, texcoord);
        for (int j = 0; j < 2; j++) {
            if (texcoord[j] < mins[j])
                mins[j] = texcoord[j];
            if (texcoord[j] > maxs[j])
                maxs[j] = texcoord[j];
        }
        
        //ericw -- also save worldmaxs/worldmins, for calculating a bounding sphere
        for (int j = 0; j < 3; j++) {
            if (worldpoint[j] > worldmaxs[j])
                worldmaxs[j] = worldpoint[j];
            if (worldpoint[j] < worldmins[j])
                worldmins[j] = worldpoint[j];
        }
    }

    vec3_t worldpoint;
    glm_to_vec3_t(Face_Centroid(bsp, face), worldpoint);
    WorldToTexCoord(worldpoint, tex, surf->exactmid);

    // calculate a bounding sphere for the face
    {
        vec3_t radius;
        
        VectorSubtract(worldmaxs, worldmins, radius);
        VectorScale(radius, 0.5, radius);
        
        VectorAdd(worldmins, radius, surf->origin);
        surf->radius = VectorLength(radius);
        
        VectorCopy(worldmaxs, surf->maxs);
        VectorCopy(worldmins, surf->mins);
    }
    
    for (int i = 0; i < 2; i++) {
        mins[i] = floor(mins[i] / surf->lightmapscale);
        maxs[i] = ceil(maxs[i] / surf->lightmapscale);
        surf->texmins[i] = mins[i];
        surf->texsize[i] = maxs[i] - mins[i];
        if (surf->texsize[i] >= MAXDIMENSION) {
            const dplane_t *plane = bsp->dplanes + face->planenum;
            const char *texname = Face_TextureName(bsp, face);
            Error("Bad surface extents:\n"              
                  "   surface %d, %s extents = %d, scale = %g\n"
                  "   texture %s at (%s)\n"
                  "   surface normal (%s)\n",
                  (int)(face - bsp->dfaces), i ? "t" : "s", surf->texsize[i], surf->lightmapscale,
                  texname, VecStr(worldpoint), VecStrf(plane->normal));
        }
    }
}

class position_t {
public:
    bool m_unoccluded;
    const bsp2_dface_t *m_actualFace;
    qvec3f m_position;
    qvec3f m_interpolatedNormal;
    
    position_t(qvec3f position)
      : m_unoccluded(false),
        m_actualFace(nullptr),
        m_position(position),
        m_interpolatedNormal(qvec3f(0,0,0)) {}
    
    position_t(const bsp2_dface_t *actualFace,
               qvec3f position,
               qvec3f interpolatedNormal)
        : m_unoccluded(true),
          m_actualFace(actualFace),
          m_position(position),
          m_interpolatedNormal(interpolatedNormal) {};
};

static const float sampleOffPlaneDist = 1.0f;

static float
TexSpaceDist(const bsp2_t *bsp, const bsp2_dface_t *face, const qvec3f &p0, const qvec3f &p1)
{
    const qvec2f p0_tex = WorldToTexCoord_HighPrecision(bsp, face, p0);
    const qvec2f p1_tex = WorldToTexCoord_HighPrecision(bsp, face, p1);
    
    return qv::length(p1_tex - p0_tex);
}

static position_t
PositionSamplePointOnFace(const bsp2_t *bsp,
                          const bsp2_dface_t *face,
                          const bool phongShaded,
                          const qvec3f &point,
                          const qvec3f &modelOffset);

position_t CalcPointNormal(const bsp2_t *bsp, const bsp2_dface_t *face, const qvec3f &origPoint, bool phongShaded, float face_lmscale, int recursiondepth,
                           const qvec3f &modelOffset)
{
    const auto &facecache = FaceCacheForFNum(Face_GetNum(bsp, face));
    const qvec4f &surfplane = facecache.plane();
    const auto &points = facecache.points();
    const auto &edgeplanes = facecache.edgePlanes();
    
    // check for degenerate face
    if (points.empty() || edgeplanes.empty())
        return position_t(origPoint);
    
    // project `point` onto the surface plane, then lift it off again
    const qvec3f point = GLM_ProjectPointOntoPlane(surfplane, origPoint) + (qvec3f(surfplane) * sampleOffPlaneDist);
    
    // check if in face..
    if (GLM_EdgePlanes_PointInside(edgeplanes, point)) {
        return PositionSamplePointOnFace(bsp, face, phongShaded, point, modelOffset);
    }

    // not in any triangle. among the edges this point is _behind_,
    // search for the one that the point is least past the endpoints of the edge
    {
        int bestplane = -1;
        float bestdist = FLT_MAX;
        
        for (int i=0; i<face->numedges; i++) {
            const qvec3f v0 = points.at(i);
            const qvec3f v1 = points.at((i+1) % points.size());
            
            const auto edgeplane = GLM_MakeInwardFacingEdgePlane(v0, v1, qvec3f(surfplane));
            if (!edgeplane.first)
                continue; // degenerate edge
            
            float planedist = GLM_DistAbovePlane(edgeplane.second, point);
            if (planedist < POINT_EQUAL_EPSILON) {
                // behind this plane. check whether we're between the endpoints.
                
                const qvec3f v0v1 = v1 - v0;
                const float v0v1dist = qv::length(v0v1);
                
                const float t = FractionOfLine(v0, v1, point); // t=0 for point=v0, t=1 for point=v1
                
                float edgedist;
                if (t < 0) edgedist = fabs(t) * v0v1dist;
                else if (t > 1) edgedist = t * v0v1dist;
                else edgedist = 0;
                
                if (edgedist < bestdist) {
                    bestplane = i;
                    bestdist = edgedist;
                }
            }
        }
        
        if (bestplane != -1) {
            // FIXME: Also need to handle non-smoothed but same plane
            const bsp2_dface_t *smoothed = Face_EdgeIndexSmoothed(bsp, face, bestplane);
            if (smoothed) {
                // try recursive search
                if (recursiondepth < 3) {
                    // call recursively to look up normal in the adjacent face
                    return CalcPointNormal(bsp, smoothed, point, phongShaded, face_lmscale, recursiondepth + 1, modelOffset);
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
static void
CalcPoints_Debug(const lightsurf_t *surf, const bsp2_t *bsp)
{
    FILE *f = fopen("calcpoints.map", "w");
    
    for (int t = 0; t < surf->height; t++) {
        for (int s = 0; s < surf->width; s++) {
            const int i = t*surf->width + s;
            const vec_t *point = surf->points[i];
            const qvec3f mangle = mangle_from_vec(vec3_t_to_glm(surf->normals[i]));
            
            fprintf(f, "{\n");
            fprintf(f, "\"classname\" \"light\"\n");
            fprintf(f, "\"origin\" \"%f %f %f\"\n", point[0], point[1], point[2]);
            fprintf(f, "\"mangle\" \"%f %f %f\"\n", mangle[0], mangle[1], mangle[2]);
            fprintf(f, "\"face\" \"%d\"\n", surf->realfacenums[i]);
            fprintf(f, "\"occluded\" \"%d\"\n", (int)surf->occluded[i]);
            fprintf(f, "\"s\" \"%d\"\n", s);
            fprintf(f, "\"t\" \"%d\"\n", t);
            fprintf(f, "}\n");
        }
    }
    
    fclose(f);
    
    logprint("wrote face %d's sample points (%dx%d) to calcpoints.map\n",
             Face_GetNum(bsp, surf->face), surf->width, surf->height);

    PrintFaceInfo(surf->face, bsp);
}

/// Checks if the point is in any solid (solid or sky leaf)
/// 1. the world
/// 2. any shadow-casting bmodel
/// 3. the `self` model (regardless of whether it's selfshadowing)
bool
Light_PointInAnySolid(const bsp2_t *bsp, const dmodel_t *self, const qvec3f &point)
{
    vec3_t v3;
    glm_to_vec3_t(point, v3);
    
    if (Light_PointInSolid(bsp, self, v3))
        return true;
    
    if (Light_PointInWorld(bsp, v3))
        return true;
    
    for (const auto &modelinfo : tracelist) {
        if (Light_PointInSolid(bsp, modelinfo->model, v3))
            return true;
    }
    
    return false;
}

// precondition: `point` is on the same plane as `face` and within the bounds.
static position_t
PositionSamplePointOnFace(const bsp2_t *bsp,
                          const bsp2_dface_t *face,
                          const bool phongShaded,
                          const qvec3f &point,
                          const qvec3f &modelOffset)
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
    
    // Get the point normal
    qvec3f pointNormal;
    if (phongShaded) {
        const auto interpNormal = GLM_InterpolateNormal(points, normals, point);
        // We already know the point is in the face, so this should always succeed
        if(!interpNormal.first)
            return position_t(point);
        pointNormal = interpNormal.second;
    } else {
        pointNormal = qvec3f(plane);
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
static void
CalcPoints(const modelinfo_t *modelinfo, const vec3_t offset, lightsurf_t *surf, const bsp2_t *bsp, const bsp2_dface_t *face)
{
    const globalconfig_t &cfg = *surf->cfg;
    
    /*
     * Fill in the surface points. The points are biased towards the center of
     * the surface to help avoid edge cases just inside walls
     */
    TexCoordToWorld(surf->exactmid[0], surf->exactmid[1], &surf->texorg, surf->midpoint);
    VectorAdd(surf->midpoint, offset, surf->midpoint);
    
    surf->width  = (surf->texsize[0] + 1) * oversample;
    surf->height = (surf->texsize[1] + 1) * oversample;
    const float starts = (surf->texmins[0] - 0.5 + (0.5 / oversample)) * surf->lightmapscale;
    const float startt = (surf->texmins[1] - 0.5 + (0.5 / oversample)) * surf->lightmapscale;
    const float st_step = surf->lightmapscale / oversample;

    /* Allocate surf->points */
    surf->numpoints = surf->width * surf->height;
    surf->points = (vec3_t *) calloc(surf->numpoints, sizeof(vec3_t));
    surf->normals = (vec3_t *) calloc(surf->numpoints, sizeof(vec3_t));
    surf->occluded = (bool *)calloc(surf->numpoints, sizeof(bool));
    surf->realfacenums = (int *)calloc(surf->numpoints, sizeof(int));
    
    const auto points = GLM_FacePoints(bsp, face);
    const auto edgeplanes = GLM_MakeInwardFacingEdgePlanes(points);
    
    for (int t = 0; t < surf->height; t++) {
        for (int s = 0; s < surf->width; s++) {
            const int i = t*surf->width + s;
            vec_t *point = surf->points[i];
            vec_t *norm = surf->normals[i];
            int *realfacenum = &surf->realfacenums[i];
            
            const vec_t us = starts + s * st_step;
            const vec_t ut = startt + t * st_step;

            TexCoordToWorld(us, ut, &surf->texorg, point);

            // do this before correcting the point, so we can wrap around the inside of pipes
            const bool phongshaded = (surf->curved && cfg.phongallowed.boolValue());
            const auto res = CalcPointNormal(bsp, face, vec3_t_to_glm(point), phongshaded, surf->lightmapscale, 0, vec3_t_to_glm(offset));
            
            surf->occluded[i] = !res.m_unoccluded;
            *realfacenum = res.m_actualFace != nullptr ? Face_GetNum(bsp, res.m_actualFace) : -1;
            glm_to_vec3_t(res.m_position, point);
            glm_to_vec3_t(res.m_interpolatedNormal, norm);
            
            // apply model offset after calling CalcPointNormal
            VectorAdd(point, offset, point);
        }
    }
    
    const int facenum = (face - bsp->dfaces);
    if (dump_facenum == facenum) {
        CalcPoints_Debug(surf, bsp);
    }
}

static bool
Face_IsLiquid(const bsp2_t *bsp, const bsp2_dface_t *face)
{
    const char *name = Face_TextureName(bsp, face);
    return name[0] == '*';
}

static void
Lightsurf_Init(const modelinfo_t *modelinfo, const bsp2_dface_t *face,
               const bsp2_t *bsp, lightsurf_t *lightsurf, facesup_t *facesup)
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

    const uint64_t extended_flags = extended_texinfo_flags[face->texinfo];
    lightsurf->curved = !!(extended_flags & TEX_PHONG_ANGLE_MASK);
    
    // nodirt
    if (modelinfo->dirt.isChanged()) {
        lightsurf->nodirt = (modelinfo->dirt.intValue() == -1);
    } else {
        lightsurf->nodirt = !!(extended_flags & TEX_NODIRT);
    }
    
    // minlight
    if (modelinfo->minlight.isChanged()) {
        lightsurf->minlight = modelinfo->minlight.floatValue();
    } else {
        lightsurf->minlight = static_cast<vec_t>((extended_flags & TEX_MINLIGHT_MASK) >> TEX_MINLIGHT_SHIFT);
    }
    
    // minlight_color
    if (modelinfo->minlight_color.isChanged()) {
        VectorCopy(*modelinfo->minlight_color.vec3Value(), lightsurf->minlight_color);    
    } else {
        // if modelinfo mincolor not set, use the one from the .texinfo file
        vec3_t extended_mincolor {
            static_cast<float>((extended_flags & TEX_MINLIGHT_COLOR_R_MASK) >> TEX_MINLIGHT_COLOR_R_SHIFT),
            static_cast<float>((extended_flags & TEX_MINLIGHT_COLOR_G_MASK) >> TEX_MINLIGHT_COLOR_G_SHIFT),
            static_cast<float>((extended_flags & TEX_MINLIGHT_COLOR_B_MASK) >> TEX_MINLIGHT_COLOR_B_SHIFT)};
        if (lightsurf->minlight > 0 && VectorCompare(extended_mincolor, vec3_origin, EQUAL_EPSILON)) {
            VectorSet(extended_mincolor, 255, 255, 255);
        }
        VectorCopy(extended_mincolor, lightsurf->minlight_color);
    }
    
    /* never receive dirtmapping on lit liquids */
    if (Face_IsLiquid(bsp, face)) {
        lightsurf->nodirt = true;
    }
    
    /* handle glass alpha */
    if (modelinfo->alpha.floatValue() < 1) {
        /* skip culling of rays coming from the back side of the face */
        lightsurf->twosided = true;
    }
    
    /* Set up the plane, not including model offset */
    plane_t *plane = &lightsurf->plane;
    VectorCopy(bsp->dplanes[face->planenum].normal, plane->normal);
    plane->dist = bsp->dplanes[face->planenum].dist;
    if (face->side) {
        VectorSubtract(vec3_origin, plane->normal, plane->normal);
        plane->dist = -plane->dist;
    }

    /* Set up the texorg for coordinate transformation */
    lightsurf->texorg.texSpaceToWorld = TexSpaceToWorld(bsp, face);
    lightsurf->texorg.texinfo = &bsp->texinfo[face->texinfo];
    lightsurf->texorg.planedist = plane->dist;

    const texinfo_t *tex = &bsp->texinfo[face->texinfo];
    VectorCopy(tex->vecs[0], lightsurf->snormal);
    VectorSubtract(vec3_origin, tex->vecs[1], lightsurf->tnormal);
    VectorNormalize(lightsurf->snormal);
    VectorNormalize(lightsurf->tnormal);

    /* Set up the surface points */
    CalcFaceExtents(face, bsp, lightsurf);
    CalcPoints(modelinfo, modelinfo->offset, lightsurf, bsp, face);
    
    /* Correct the plane for the model offset (must be done last, 
       calculation of face extents / points needs the uncorrected plane) */
    vec3_t planepoint;
    VectorScale(plane->normal, plane->dist, planepoint);
    VectorAdd(planepoint, modelinfo->offset, planepoint);
    plane->dist = DotProduct(plane->normal, planepoint);
    
    /* Correct bounding sphere */
    VectorAdd(lightsurf->origin, modelinfo->offset, lightsurf->origin);
    VectorAdd(lightsurf->mins, modelinfo->offset, lightsurf->mins);
    VectorAdd(lightsurf->maxs, modelinfo->offset, lightsurf->maxs);
    
    /* Allocate occlusion array */
    lightsurf->occlusion = (float *) calloc(lightsurf->numpoints, sizeof(float));
    
    lightsurf->stream = MakeRayStream(lightsurf->numpoints);
}

static void
Lightmap_AllocOrClear(lightmap_t *lightmap, const lightsurf_t *lightsurf)
{
    if (lightmap->samples == NULL) {
        /* first use of this lightmap, allocate the storage for it. */
        lightmap->samples = (lightsample_t *) calloc(lightsurf->numpoints, sizeof(lightsample_t));
    } else {
        /* clear only the data that is going to be merged to it. there's no point clearing more */
        memset(lightmap->samples, 0, sizeof(*lightmap->samples)*lightsurf->numpoints);
    }
}

static const lightmap_t *
Lightmap_ForStyle_ReadOnly(const lightsurf_t *lightsurf, const int style)
{
    for (const auto &lm : lightsurf->lightmapsByStyle) {
        if (lm.style == style)
            return &lm;
    }
    return nullptr;
}

/*
 * Lightmap_ForStyle
 *
 * If lightmap with given style has already been allocated, return it.
 * Otherwise, return the next available map.  A new map is not marked as
 * allocated since it may not be kept if no lights hit.
 */
static lightmap_t *
Lightmap_ForStyle(lightmapdict_t *lightmaps, const int style, const lightsurf_t *lightsurf)
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
    lightmap_t newLightmap {};
    newLightmap.style = 255;
    Lightmap_AllocOrClear(&newLightmap, lightsurf);
    lightmaps->push_back(newLightmap);
    
    return &lightmaps->back();
}

static void
Lightmap_ClearAll(lightmapdict_t *lightmaps)
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
static void
Lightmap_Save(lightmapdict_t *lightmaps, const lightsurf_t *lightsurf,
              lightmap_t *lightmap, const int style)
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
vec_t
GetLightValue(const globalconfig_t &cfg, const light_t *entity, vec_t dist)
{
    const float light = entity->light.floatValue();
    vec_t value;

    if (entity->getFormula() == LF_INFINITE || entity->getFormula() == LF_LOCALMIN)
        return light;

    value = cfg.scaledist.floatValue() * entity->atten.floatValue() * dist;
    switch (entity->getFormula()) {
    case LF_INVERSE:
        return light / (value / LF_SCALE);
    case LF_INVERSE2A:
        value += LF_SCALE;
        /* Fall through */
    case LF_INVERSE2:
        return light / ((value * value) / (LF_SCALE * LF_SCALE));
    case LF_LINEAR:
        if (light > 0)
            return (light - value > 0) ? light - value : 0;
        else
            return (light + value < 0) ? light + value : 0;
    default:
        Error("Internal error: unknown light formula");
    }
}

float
GetLightValueWithAngle(const globalconfig_t &cfg, const light_t *entity, const vec3_t surfnorm, const vec3_t surfpointToLightDir, float dist, bool twosided)
{
    float angle = DotProduct(surfpointToLightDir, surfnorm);
    if (entity->bleed.boolValue() || twosided) {
        if (angle < 0) {
            angle = -angle; // ericw -- support "_bleed" option
        }
    }
    
    /* Light behind sample point? Zero contribution, period. */
    if (angle < 0) {
        return 0;
    }
    
    /* Apply anglescale */
    angle = (1.0 - entity->anglescale.floatValue()) + (entity->anglescale.floatValue() * angle);
    
    /* Check spotlight cone */
    float spotscale = 1;
    if (entity->spotlight) {
        vec_t falloff = DotProduct(entity->spotvec, surfpointToLightDir);
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

static void LightFace_SampleMipTex(miptex_t *tex, const float *projectionmatrix, const vec3_t point, float *result);

void
GetLightContrib(const globalconfig_t &cfg, const light_t *entity, const vec3_t surfnorm, const vec3_t surfpoint, bool twosided,
                vec3_t color_out, vec3_t surfpointToLightDir_out, vec3_t normalmap_addition_out, float *dist_out)
{
    float dist = GetDir(surfpoint, *entity->origin.vec3Value(), surfpointToLightDir_out);
    float add = GetLightValueWithAngle(cfg, entity, surfnorm, surfpointToLightDir_out, dist, twosided);
    
    /* write out the final color */
    if (entity->projectedmip) {
        vec3_t col;
        LightFace_SampleMipTex(entity->projectedmip, entity->projectionmatrix, surfpoint, col);
        VectorScale(col, add * (1.0f / 255.0f), color_out);
    } else {
        VectorScale(*entity->color.vec3Value(), add * (1.0f / 255.0f), color_out);
    }
    
    // write normalmap contrib
    VectorScale(surfpointToLightDir_out, add, normalmap_addition_out);
    
    *dist_out = dist;
}

#define SQR(x) ((x)*(x))

// this is the inverse of GetLightValue
float
GetLightDist(const globalconfig_t &cfg, const light_t *entity, vec_t desiredLight)
{
    float fadedist;
    if (entity->getFormula() == LF_LINEAR) {
        /* Linear formula always has a falloff point */
        fadedist = fabs(entity->light.floatValue()) - desiredLight;
        fadedist = fadedist / entity->atten.floatValue() / cfg.scaledist.floatValue();
        fadedist = qmax(0.0f, fadedist);
    } else {
        /* Calculate the distance at which brightness falls to desiredLight */
        switch (entity->getFormula()) {
            case LF_INFINITE:
            case LF_LOCALMIN:
                fadedist = VECT_MAX;
                break;
            case LF_INVERSE:
                fadedist = (LF_SCALE * fabs(entity->light.floatValue())) / (cfg.scaledist.floatValue() * entity->atten.floatValue() * desiredLight);
                break;
            case LF_INVERSE2:
            case LF_INVERSE2A:
                fadedist = sqrt(fabs(entity->light.floatValue() * SQR(LF_SCALE) / (SQR(cfg.scaledist.floatValue()) * SQR(entity->atten.floatValue()) * desiredLight)));
                if (entity->getFormula() == LF_INVERSE2A) {
                    fadedist -= (LF_SCALE / (cfg.scaledist.floatValue() * entity->atten.floatValue()));
                }
                fadedist = qmax(0.0f, fadedist);
                break;
            default:
                Error("Internal error: formula not handled in %s", __func__);
        }
    }
    return fadedist;
}

static inline void
Light_Add(lightsample_t *sample, const vec_t light, const vec3_t color, const vec3_t direction)
{
    VectorMA(sample->color, light / 255.0f, color, sample->color);
    VectorMA(sample->direction, light, direction, sample->direction);
}

static inline void
Light_ClampMin(lightsample_t *sample, const vec_t light, const vec3_t color)
{
    for (int i = 0; i < 3; i++) {
        if (sample->color[i] < color[i] * light / 255.0f) {
            sample->color[i] = color[i] * light / 255.0f;
        }
    }
}

static float fraction(float min, float val, float max) {
    if (val >= max) return 1.0;
    if (val <= min) return 0.0;
    
    return (val - min) / (max - min);
}

/*
 * ============
 * Dirt_GetScaleFactor
 *
 * returns scale factor for dirt/ambient occlusion
 * ============
 */
static inline vec_t
Dirt_GetScaleFactor(const globalconfig_t &cfg, vec_t occlusion, const light_t *entity, const vec_t entitydist, const lightsurf_t *surf)
{
    vec_t light_dirtgain = cfg.dirtGain.floatValue();
    vec_t light_dirtscale = cfg.dirtScale.floatValue();
    bool usedirt;

    /* is dirt processing disabled entirely? */
    if (!dirt_in_use)
        return 1.0f;
    if (surf && surf->nodirt)
        return 1.0f;

    /* should this light be affected by dirt? */
    if (entity) {
        if (entity->dirt.intValue() == -1) {
            usedirt = false;
        } else if (entity->dirt.intValue() == 1) {
            usedirt = true;
        } else {
            usedirt = cfg.globalDirt.boolValue();
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
        if (entity->dirtgain.floatValue())
            light_dirtgain = entity->dirtgain.floatValue();
        if (entity->dirtscale.floatValue())
            light_dirtscale = entity->dirtscale.floatValue();
    }

    /* early out */
    if ( occlusion <= 0.0f ) {
        return 1.0f;
    }

    /* apply gain (does this even do much? heh) */
    float outDirt = pow( occlusion, light_dirtgain );
    if ( outDirt > 1.0f ) {
        outDirt = 1.0f;
    }

    /* apply scale */
    outDirt *= light_dirtscale;
    if ( outDirt > 1.0f ) {
        outDirt = 1.0f;
    }

    /* lerp based on distance to light */
    if (entity) {
        // From 0 to _dirt_off_radius units, no dirt.
        // From _dirt_off_radius to _dirt_on_radius, the dirt linearly ramps from 0 to full, and after _dirt_on_radius, it's full dirt.
        
        if (entity->dirt_on_radius.isChanged()
            && entity->dirt_off_radius.isChanged()) {
            
            const float onRadius = entity->dirt_on_radius.floatValue();
            const float offRadius = entity->dirt_off_radius.floatValue();
            
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
static inline qboolean
CullLight(const light_t *entity, const lightsurf_t *lightsurf)
{
    const globalconfig_t &cfg = *lightsurf->cfg;
    
    if (!novisapprox && AABBsDisjoint(entity->mins, entity->maxs, lightsurf->mins, lightsurf->maxs)) {
        return true;
    }
    
    vec3_t distvec;
    VectorSubtract(*entity->origin.vec3Value(), lightsurf->origin, distvec);
    float dist = VectorLength(distvec) - lightsurf->radius;
    
    /* light is inside surface bounding sphere => can't cull */
    if (dist < 0) {
        return false;
    }
    
    /* return true if the light level at the closest point on the
     surface bounding sphere to the light source is <= fadegate.
     need fabs to handle antilights. */
    return fabs(GetLightValue(cfg, entity, dist)) <= fadegate;
}

byte thepalette[768] =
{
0,0,0,15,15,15,31,31,31,47,47,47,63,63,63,75,75,75,91,91,91,107,107,107,123,123,123,139,139,139,155,155,155,171,171,171,187,187,187,203,203,203,219,219,219,235,235,235,15,11,7,23,15,11,31,23,11,39,27,15,47,35,19,55,43,23,63,47,23,75,55,27,83,59,27,91,67,31,99,75,31,107,83,31,115,87,31,123,95,35,131,103,35,143,111,35,11,11,15,19,19,27,27,27,39,39,39,51,47,47,63,55,55,75,63,63,87,71,71,103,79,79,115,91,91,127,99,99,
139,107,107,151,115,115,163,123,123,175,131,131,187,139,139,203,0,0,0,7,7,0,11,11,0,19,19,0,27,27,0,35,35,0,43,43,7,47,47,7,55,55,7,63,63,7,71,71,7,75,75,11,83,83,11,91,91,11,99,99,11,107,107,15,7,0,0,15,0,0,23,0,0,31,0,0,39,0,0,47,0,0,55,0,0,63,0,0,71,0,0,79,0,0,87,0,0,95,0,0,103,0,0,111,0,0,119,0,0,127,0,0,19,19,0,27,27,0,35,35,0,47,43,0,55,47,0,67,
55,0,75,59,7,87,67,7,95,71,7,107,75,11,119,83,15,131,87,19,139,91,19,151,95,27,163,99,31,175,103,35,35,19,7,47,23,11,59,31,15,75,35,19,87,43,23,99,47,31,115,55,35,127,59,43,143,67,51,159,79,51,175,99,47,191,119,47,207,143,43,223,171,39,239,203,31,255,243,27,11,7,0,27,19,0,43,35,15,55,43,19,71,51,27,83,55,35,99,63,43,111,71,51,127,83,63,139,95,71,155,107,83,167,123,95,183,135,107,195,147,123,211,163,139,227,179,151,
171,139,163,159,127,151,147,115,135,139,103,123,127,91,111,119,83,99,107,75,87,95,63,75,87,55,67,75,47,55,67,39,47,55,31,35,43,23,27,35,19,19,23,11,11,15,7,7,187,115,159,175,107,143,163,95,131,151,87,119,139,79,107,127,75,95,115,67,83,107,59,75,95,51,63,83,43,55,71,35,43,59,31,35,47,23,27,35,19,19,23,11,11,15,7,7,219,195,187,203,179,167,191,163,155,175,151,139,163,135,123,151,123,111,135,111,95,123,99,83,107,87,71,95,75,59,83,63,
51,67,51,39,55,43,31,39,31,23,27,19,15,15,11,7,111,131,123,103,123,111,95,115,103,87,107,95,79,99,87,71,91,79,63,83,71,55,75,63,47,67,55,43,59,47,35,51,39,31,43,31,23,35,23,15,27,19,11,19,11,7,11,7,255,243,27,239,223,23,219,203,19,203,183,15,187,167,15,171,151,11,155,131,7,139,115,7,123,99,7,107,83,0,91,71,0,75,55,0,59,43,0,43,31,0,27,15,0,11,7,0,0,0,255,11,11,239,19,19,223,27,27,207,35,35,191,43,
43,175,47,47,159,47,47,143,47,47,127,47,47,111,47,47,95,43,43,79,35,35,63,27,27,47,19,19,31,11,11,15,43,0,0,59,0,0,75,7,0,95,7,0,111,15,0,127,23,7,147,31,7,163,39,11,183,51,15,195,75,27,207,99,43,219,127,59,227,151,79,231,171,95,239,191,119,247,211,139,167,123,59,183,155,55,199,195,55,231,227,87,127,191,255,171,231,255,215,255,255,103,0,0,139,0,0,179,0,0,215,0,0,255,0,0,255,243,147,255,247,199,255,255,255,159,91,83
};
static void Matrix4x4_CM_Transform4(const float *matrix, const float *vector, float *product)
{
    product[0] = matrix[0]*vector[0] + matrix[4]*vector[1] + matrix[8]*vector[2] + matrix[12]*vector[3];
    product[1] = matrix[1]*vector[0] + matrix[5]*vector[1] + matrix[9]*vector[2] + matrix[13]*vector[3];
    product[2] = matrix[2]*vector[0] + matrix[6]*vector[1] + matrix[10]*vector[2] + matrix[14]*vector[3];
    product[3] = matrix[3]*vector[0] + matrix[7]*vector[1] + matrix[11]*vector[2] + matrix[15]*vector[3];
}
static qboolean Matrix4x4_CM_Project (const vec3_t in, vec3_t out, const float *modelviewproj)
{
    qboolean result = true;

    float v[4], tempv[4];
    tempv[0] = in[0];
    tempv[1] = in[1];
    tempv[2] = in[2];
    tempv[3] = 1;

    Matrix4x4_CM_Transform4(modelviewproj, tempv, v);

    v[0] /= v[3];
    v[1] /= v[3];
    if (v[2] < 0)
        result = false; //too close to the view
    v[2] /= v[3];

    out[0] = (1+v[0])/2;
    out[1] = (1+v[1])/2;
    out[2] = (1+v[2])/2;
    if (out[2] > 1)
        result = false; //beyond far clip plane
    return result;
}
static void LightFace_SampleMipTex(miptex_t *tex, const float *projectionmatrix, const vec3_t point, float *result)
{
    //okay, yes, this is weird, yes we're using a vec3_t for a coord...
    //this is because we're treating it like a cubemap. why? no idea.
    float sfrac, tfrac, weight[4];
    int sbase, tbase;
    byte *data = (byte*)tex + tex->offsets[0], *pi[4];

    vec3_t coord;
    if (!Matrix4x4_CM_Project(point, coord, projectionmatrix) || coord[0] <= 0 || coord[0] >= 1 || coord[1] <= 0 || coord[1] >= 1)
        VectorSet(result, 0, 0, 0);
    else
    {
        sfrac = (coord[0]) * tex->width;
        sbase = sfrac;
        sfrac -= sbase;
        tfrac = (1-coord[1]) * tex->height;
        tbase = tfrac;
        tfrac -= tbase;

        pi[0] = thepalette + 3*data[((sbase+0)%tex->width) + (tex->width*((tbase+0)%tex->height))];     weight[0] = (1-sfrac)*(1-tfrac);
        pi[1] = thepalette + 3*data[((sbase+1)%tex->width) + (tex->width*((tbase+0)%tex->height))];     weight[1] = (sfrac)*(1-tfrac);
        pi[2] = thepalette + 3*data[((sbase+0)%tex->width) + (tex->width*((tbase+1)%tex->height))];     weight[2] = (1-sfrac)*(tfrac);
        pi[3] = thepalette + 3*data[((sbase+1)%tex->width) + (tex->width*((tbase+1)%tex->height))];     weight[3] = (sfrac)*(tfrac);
        VectorSet(result, 0, 0, 0);
        result[0]  = weight[0] * pi[0][0];
        result[1]  = weight[0] * pi[0][1];
        result[2]  = weight[0] * pi[0][2];
        result[0] += weight[1] * pi[1][0];
        result[1] += weight[1] * pi[1][1];
        result[2] += weight[1] * pi[1][2];
        result[0] += weight[2] * pi[2][0];
        result[1] += weight[2] * pi[2][1];
        result[2] += weight[2] * pi[2][2];
        result[0] += weight[3] * pi[3][0];
        result[1] += weight[3] * pi[3][1];
        result[2] += weight[3] * pi[3][2];
        VectorScale(result, 2, result);
    }
}

/*
 * ================
 * GetDirectLighting
 *
 * Mesaures direct lighting at a point, currently only used for bounce lighting.
 * FIXME: factor out / merge with LightFace
 * ================
 */
std::map<int, qvec3f>
GetDirectLighting(const globalconfig_t &cfg, raystream_t *rs, const vec3_t origin, const vec3_t normal)
{
    std::map<int, qvec3f> result;

    float occlusion = DirtAtPoint(cfg, rs, origin, normal, /* FIXME: pass selfshadow? */ nullptr);
    if (std::isnan(occlusion)) {
        // HACK: getting an invalid normal of (0, 0, 0).
        occlusion = 0.0f;
    }
    
    for (const light_t &entity : GetLights()) {
        vec3_t surfpointToLightDir;
        float surfpointToLightDist;
        vec3_t color, normalcontrib;
        
        // Skip styled lights if "bouncestyled" setting is off.
        if (entity.style.intValue() != 0 && !cfg.bouncestyled.boolValue()) {
            continue;
        }
        
        GetLightContrib(cfg, &entity, normal, origin, false, color, surfpointToLightDir, normalcontrib, &surfpointToLightDist);
        
        const float dirt = Dirt_GetScaleFactor(cfg, occlusion, &entity, surfpointToLightDist, /* FIXME: pass */ nullptr);
        VectorScale(color, dirt, color);
        VectorScale(color, entity.bouncescale.floatValue(), color);
        
        // NOTE: Skip negative lights, which would make no sense to bounce!
        if (LightSample_Brightness(color) <= fadegate) {
            continue;
        }
        
        if (!TestLight(*entity.origin.vec3Value(), origin, NULL)) {
            continue;
        }
        
        result[entity.style.intValue()] += vec3_t_to_glm(color);
    }
    
    for (const sun_t &sun : GetSuns()) {
        
        // NOTE: Skip negative lights, which would make no sense to bounce!
        if (sun.sunlight < 0)
            continue;
            
        vec3_t originLightDir;
        VectorCopy(sun.sunvec, originLightDir);
        VectorNormalize(originLightDir);
        
        vec_t cosangle = DotProduct(originLightDir, normal);
        if (cosangle < 0) {
            continue;
        }
        
        // apply anglescale
        cosangle = (1.0 - sun.anglescale) + sun.anglescale * cosangle;
        
        if (!TestSky(origin, sun.sunvec, NULL)) {
            continue;
        }
        
        float dirt = 1;
        if (sun.dirt) {
            dirt = Dirt_GetScaleFactor(cfg, occlusion, nullptr, 0.0, /* FIXME: pass */ nullptr);
        }
        
        const int sunstyle = 0;
        const qvec3f sunContrib = vec3_t_to_glm(sun.sunlight_color) * (dirt * cosangle * sun.sunlight / 255.0f);
        result[sunstyle] += sunContrib;
    }
    
    return result;
}


/*
 * ================
 * LightFace_Entity
 * ================
 */
static void
LightFace_Entity(const bsp2_t *bsp,
                 const light_t *entity,
                lightsurf_t *lightsurf, lightmapdict_t *lightmaps)
{
    const globalconfig_t &cfg = *lightsurf->cfg;
    const modelinfo_t *modelinfo = lightsurf->modelinfo;
    const plane_t *plane = &lightsurf->plane;

    const float planedist = DotProduct(*entity->origin.vec3Value(), plane->normal) - plane->dist;

    /* don't bother with lights behind the surface.
     
       if the surface is curved, the light may be behind the surface, but it may
       still have a line of sight to a samplepoint, and that sample point's 
       normal may be facing such that it receives some light, so we can't use this 
       test in the curved case.
    */
    if (planedist < 0 && !entity->bleed.boolValue() && !lightsurf->curved && !lightsurf->twosided) {
        return;
    }

    /* sphere cull surface and light */
    if (CullLight(entity, lightsurf)) {
        return;
    }

    /*
     * Check it for real
     */
    raystream_t *rs = lightsurf->stream;
    rs->clearPushedRays();
    
    for (int i = 0; i < lightsurf->numpoints; i++) {
        const vec_t *surfpoint = lightsurf->points[i];
        const vec_t *surfnorm = lightsurf->normals[i];
        
        if (lightsurf->occluded[i])
            continue;
        
        vec3_t surfpointToLightDir;
        float surfpointToLightDist;
        vec3_t color, normalcontrib;
        
        GetLightContrib(cfg, entity, surfnorm, surfpoint, lightsurf->twosided, color, surfpointToLightDir, normalcontrib, &surfpointToLightDist);
 
        const float occlusion = Dirt_GetScaleFactor(cfg, lightsurf->occlusion[i], entity, surfpointToLightDist, lightsurf);
        VectorScale(color, occlusion, color);
        
        /* Quick distance check first */
        if (fabs(LightSample_Brightness(color)) <= fadegate) {
            continue;
        }
        
        rs->pushRay(i, surfpoint, surfpointToLightDir, surfpointToLightDist, modelinfo, color, normalcontrib);
    }
    
    rs->tracePushedRaysOcclusion();
    total_light_rays += rs->numPushedRays();
    
    int cached_style = entity->style.intValue();
    lightmap_t *cached_lightmap = Lightmap_ForStyle(lightmaps, cached_style, lightsurf);
    
    const int N = rs->numPushedRays();
    for (int j = 0; j < N; j++) {
        if (rs->getPushedRayOccluded(j)) {
            continue;
        }

        total_light_ray_hits++;
        
        int i = rs->getPushedRayPointIndex(j);
        
        // check if we hit a dynamic shadow caster
        int desired_style = entity->style.intValue();
        if (rs->getPushedRayDynamicStyle(j) != 0) {
            desired_style = rs->getPushedRayDynamicStyle(j);
        }
        
        // if necessary, switch which lightmap we are writing to.
        if (desired_style != cached_style) {
            cached_style = desired_style;
            cached_lightmap = Lightmap_ForStyle(lightmaps, cached_style, lightsurf);
        }
        
        lightsample_t *sample = &cached_lightmap->samples[i];
        
        vec3_t color, normalcontrib;
        rs->getPushedRayColor(j, color);
        rs->getPushedRayNormalContrib(j, normalcontrib);

        VectorAdd(sample->color, color, sample->color);
        VectorAdd(sample->direction, normalcontrib, sample->direction);
        
        Lightmap_Save(lightmaps, lightsurf, cached_lightmap, cached_style);
    }
}

/*
 * =============
 * LightFace_Sky
 * =============
 */
static void
LightFace_Sky(const sun_t *sun, const lightsurf_t *lightsurf, lightmapdict_t *lightmaps)
{
    const globalconfig_t &cfg = *lightsurf->cfg;
    const float MAX_SKY_DIST = 65536.0f;
    const modelinfo_t *modelinfo = lightsurf->modelinfo;
    const plane_t *plane = &lightsurf->plane;
    
    /* Don't bother if surface facing away from sun */
    if (DotProduct(sun->sunvec, plane->normal) < -ANGLE_EPSILON && !lightsurf->curved && !lightsurf->twosided) {
        return;
    }
    
    vec3_t incoming;
    VectorCopy(sun->sunvec, incoming);
    VectorNormalize(incoming);
    
    /* Check each point... */
    raystream_t *rs = lightsurf->stream;
    rs->clearPushedRays();
    
    for (int i = 0; i < lightsurf->numpoints; i++) {
        const vec_t *surfpoint = lightsurf->points[i];
        const vec_t *surfnorm = lightsurf->normals[i];
        
        if (lightsurf->occluded[i])
            continue;
        
        float angle = DotProduct(incoming, surfnorm);
        if (lightsurf->twosided) {
            if (angle < 0) {
                angle = -angle;
            }
        }
        
        if (angle < 0) {
            continue;
        }
        
        angle = (1.0 - sun->anglescale) + sun->anglescale * angle;
        float value = angle * sun->sunlight;
        if (sun->dirt) {
            value *= Dirt_GetScaleFactor(cfg, lightsurf->occlusion[i], NULL, 0.0, lightsurf);
        }
        
        vec3_t color, normalcontrib;
        VectorScale(sun->sunlight_color, value / 255.0, color);
        VectorScale(sun->sunvec, value, normalcontrib);
        
        /* Quick distance check first */
        if (fabs(LightSample_Brightness(color)) <= fadegate) {
            continue;
        }
        
        rs->pushRay(i, surfpoint, incoming, MAX_SKY_DIST, modelinfo, color, normalcontrib);
    }
    
    rs->tracePushedRaysIntersection();
    
    /* if sunlight is set, use a style 0 light map */
    int cached_style = 0;
    lightmap_t *cached_lightmap = Lightmap_ForStyle(lightmaps, cached_style, lightsurf);
    
    const int N = rs->numPushedRays();
    for (int j = 0; j < N; j++) {
        if (rs->getPushedRayHitType(j) != hittype_t::SKY) {
            continue;
        }
        
        const int i = rs->getPushedRayPointIndex(j);
        
        // check if we hit a dynamic shadow caster
        int desired_style = 0;
        if (rs->getPushedRayDynamicStyle(j) != 0) {
            desired_style = rs->getPushedRayDynamicStyle(j);
        }
        
        // if necessary, switch which lightmap we are writing to.
        if (desired_style != cached_style) {
            cached_style = desired_style;
            cached_lightmap = Lightmap_ForStyle(lightmaps, cached_style, lightsurf);
        }

        lightsample_t *sample = &cached_lightmap->samples[i];
        
        vec3_t color, normalcontrib;
        rs->getPushedRayColor(j, color);
        rs->getPushedRayNormalContrib(j, normalcontrib);
        
        VectorAdd(sample->color, color, sample->color);
        VectorAdd(sample->direction, normalcontrib, sample->direction);
        
        Lightmap_Save(lightmaps, lightsurf, cached_lightmap, cached_style);
    }
}

/*
 * ============
 * LightFace_Min
 * ============
 */
static void
LightFace_Min(const bsp2_t *bsp, const bsp2_dface_t *face,
              const vec3_t color, vec_t light,
              const lightsurf_t *lightsurf, lightmapdict_t *lightmaps)
{
    const globalconfig_t &cfg = *lightsurf->cfg;
    const modelinfo_t *modelinfo = lightsurf->modelinfo;

    const char *texname = Face_TextureName(bsp, face);
    if (texname[0] != '\0' && modelinfo->minlight_exclude.stringValue() == std::string{ texname }) {
        return; /* this texture is excluded from minlight */
    }
    
    /* Find a style 0 lightmap */
    lightmap_t *lightmap = Lightmap_ForStyle(lightmaps, 0, lightsurf);

    bool hit = false;
    for (int i = 0; i < lightsurf->numpoints; i++) {
        lightsample_t *sample = &lightmap->samples[i];
        
        vec_t value = light;
        if (cfg.minlightDirt.boolValue()) {
            value *= Dirt_GetScaleFactor(cfg, lightsurf->occlusion[i], NULL, 0.0, lightsurf);
        }
        if (cfg.addminlight.boolValue()) {
            Light_Add(sample, value, color, vec3_origin);
        } else {
            Light_ClampMin(sample, value, color);
        }

        hit = true;
    }

    if (hit) {
        Lightmap_Save(lightmaps, lightsurf, lightmap, 0);
    }
    
    // FIXME: Refactor this?
    if (lightsurf->modelinfo->lightignore.boolValue())
        return;
    
    /* Cast rays for local minlight entities */
    for (const auto &entity : GetLights()) {
        if (entity.getFormula() != LF_LOCALMIN) {
            continue;
        }

        if (CullLight(&entity, lightsurf)) {
            continue;
        }
        
        raystream_t *rs = lightsurf->stream;
        rs->clearPushedRays();
        
        lightmap = Lightmap_ForStyle(lightmaps, entity.style.intValue(), lightsurf);

        hit = false;
        for (int i = 0; i < lightsurf->numpoints; i++) {
            if (lightsurf->occluded[i])
                continue;
            
            const lightsample_t *sample = &lightmap->samples[i];
            const vec_t *surfpoint = lightsurf->points[i];
            if (cfg.addminlight.boolValue() || LightSample_Brightness(sample->color) < entity.light.floatValue()) {
                vec3_t surfpointToLightDir;
                vec_t surfpointToLightDist = GetDir(surfpoint, *entity.origin.vec3Value(), surfpointToLightDir);
                
                rs->pushRay(i, surfpoint, surfpointToLightDir, surfpointToLightDist, modelinfo);
            }
        }
        
        rs->tracePushedRaysOcclusion();
        total_light_rays += rs->numPushedRays();
        
        const int N = rs->numPushedRays();
        for (int j = 0; j < N; j++) {
            if (rs->getPushedRayOccluded(j)) {
                continue;
            }
            
            int i = rs->getPushedRayPointIndex(j);
            vec_t value = entity.light.floatValue();
            lightsample_t *sample = &lightmap->samples[i];
            
            value *= Dirt_GetScaleFactor(cfg, lightsurf->occlusion[i], &entity, 0.0 /* TODO: pass distance */, lightsurf);
            if (cfg.addminlight.boolValue()) {
                Light_Add(sample, value, *entity.color.vec3Value(), vec3_origin);
            } else {
                Light_ClampMin(sample, value, *entity.color.vec3Value());
            }

            hit = true;
            total_light_ray_hits++;
        }
        
        if (hit) {
            Lightmap_Save(lightmaps, lightsurf, lightmap, entity.style.intValue());
        }
    }
}

/*
 * =============
 * LightFace_DirtDebug
 * =============
 */
static void
LightFace_DirtDebug(const lightsurf_t *lightsurf, lightmapdict_t *lightmaps)
{
    const globalconfig_t &cfg = *lightsurf->cfg;
    /* use a style 0 light map */
    lightmap_t *lightmap = Lightmap_ForStyle(lightmaps, 0, lightsurf);

    /* Overwrite each point with the dirt value for that sample... */
    for (int i = 0; i < lightsurf->numpoints; i++) {
        lightsample_t *sample = &lightmap->samples[i];
        const float light = 255 * Dirt_GetScaleFactor(cfg, lightsurf->occlusion[i], NULL, 0.0, lightsurf);
        VectorSet(sample->color, light, light, light);
    }

    Lightmap_Save(lightmaps, lightsurf, lightmap, 0);
}

/*
 * =============
 * LightFace_PhongDebug
 * =============
 */
static void
LightFace_PhongDebug(const lightsurf_t *lightsurf, lightmapdict_t *lightmaps)
{
    /* use a style 0 light map */
    lightmap_t *lightmap = Lightmap_ForStyle(lightmaps, 0, lightsurf);
    
    /* Overwrite each point with the normal for that sample... */
    for (int i = 0; i < lightsurf->numpoints; i++) {
        lightsample_t *sample = &lightmap->samples[i];
        const vec3_t vec3_one = { 1.0f, 1.0f, 1.0f };
        vec3_t normal_as_color;
        // scale from [-1..1] to [0..1], then multiply by 255
        VectorCopy(lightsurf->normals[i], normal_as_color);
        VectorAdd(normal_as_color, vec3_one, normal_as_color);
        VectorScale(normal_as_color, 0.5, normal_as_color);
        VectorScale(normal_as_color, 255, normal_as_color);
        
        VectorCopy(normal_as_color, sample->color);
    }
    
    Lightmap_Save(lightmaps, lightsurf, lightmap, 0);
}

static void
LightFace_BounceLightsDebug(const lightsurf_t *lightsurf, lightmapdict_t *lightmaps)
{
    Q_assert(debugmode == debugmode_bouncelights);
 
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
            if (!GLM_EdgePlanes_PointInside(vpl.poly_edgeplanes, vec3_t_to_glm(lightsurf->points[i])))
                continue;
            
            for (const auto &styleColor : vpl.colorByStyle) {
                const qvec3f patch_color = styleColor.second * 255.0f;
                
                lightmap_t *lightmap = Lightmap_ForStyle(lightmaps, styleColor.first, lightsurf);
                lightsample_t *sample = &lightmap->samples[i];
                glm_to_vec3_t(patch_color, sample->color);
                Lightmap_Save(lightmaps, lightsurf, lightmap, styleColor.first);
            }
        }
    }
}

// returns color in [0,255]
static inline qvec3f
BounceLight_ColorAtDist(const globalconfig_t &cfg, float area, const qvec3f &bounceLightColor, float dist)
{
    // clamp away hotspots
    if (dist < 128.0f) {
        dist = 128.0f;
    }
    
    const float dist2 = (dist * dist);
    const float scale = (1.0f/dist2) * cfg.bouncescale.floatValue();
    
    // get light contribution
    const qvec3f result = bounceLightColor * area * (255.0f * scale);
    return result;
}

// dir: vpl -> sample point direction
// returns color in [0,255]
static inline qvec3f
GetIndirectLighting (const globalconfig_t &cfg, const bouncelight_t *vpl, const qvec3f &bounceLightColor, const qvec3f &dir, float dist, const qvec3f &origin, const qvec3f &normal)
{
    const float dp1 = qv::dot(vpl->surfnormal, dir);
    if (dp1 < 0.0f)
        return qvec3f(0); // sample point behind vpl
    
    const qvec3f sp_vpl = dir * -1.0f;
    const float dp2 = qv::dot(sp_vpl, normal);
    if (dp2 < 0.0f)
        return qvec3f(0); // vpl behind sample face
    
    // get light contribution
    const qvec3f result = BounceLight_ColorAtDist(cfg, vpl->area, bounceLightColor, dist);
    
    // apply angle scale
    const qvec3f resultscaled = result * dp1 * dp2;
    
    if (dp1 < 0) {
        Q_assert(!std::isnan(dp1));
    }
    
    Q_assert(!std::isnan(resultscaled[0]));
    Q_assert(!std::isnan(resultscaled[1]));
    Q_assert(!std::isnan(resultscaled[2]));
    
    return resultscaled;
}

static inline bool
BounceLight_SphereCull(const bsp2_t *bsp, const bouncelight_t *vpl, const lightsurf_t *lightsurf)
{
    const globalconfig_t &cfg = *lightsurf->cfg;
    
    if (!novisapprox && AABBsDisjoint(vpl->mins, vpl->maxs, lightsurf->mins, lightsurf->maxs))
        return true;
    
    const qvec3f dir = vec3_t_to_glm(lightsurf->origin) - vpl->pos; // vpl -> sample point
    const float dist = qv::length(dir) + lightsurf->radius;
    
    // get light contribution
    const qvec3f color = BounceLight_ColorAtDist(cfg, vpl->area, vpl->componentwiseMaxColor, dist);
    
    if (LightSample_Brightness(color) < 0.25f)
        return true;
    
    return false;
}

static void
LightFace_Bounce(const bsp2_t *bsp, const bsp2_dface_t *face, const lightsurf_t *lightsurf, lightmapdict_t *lightmaps)
{
    const globalconfig_t &cfg = *lightsurf->cfg;
    //const dmodel_t *shadowself = lightsurf->modelinfo->shadowself.boolValue() ? lightsurf->modelinfo->model : NULL;
    
    if (!cfg.bounce.boolValue())
        return;
    
    if (!(debugmode == debugmode_bounce
          || debugmode == debugmode_none))
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
            
            raystream_t *rs = lightsurf->stream;
            rs->clearPushedRays();
            
            for (int i = 0; i < lightsurf->numpoints; i++) {
                if (lightsurf->occluded[i])
                    continue;
                
                qvec3f dir = vec3_t_to_glm(lightsurf->points[i]) - vpl.pos; // vpl -> sample point
                const float dist = qv::length(dir);
                if (dist == 0.0f)
                    continue; // FIXME: nudge or something
                dir /= dist;
                
                const qvec3f indirect = GetIndirectLighting(cfg, &vpl, color, dir, dist, vec3_t_to_glm(lightsurf->points[i]), vec3_t_to_glm(lightsurf->normals[i]));
                
                if (LightSample_Brightness(indirect) < 0.25)
                    continue;
                
                vec3_t vplPos, vplDir, vplColor;
                glm_to_vec3_t(vpl.pos, vplPos);
                glm_to_vec3_t(dir, vplDir);
                glm_to_vec3_t(indirect, vplColor);
                
                rs->pushRay(i, vplPos, vplDir, dist, lightsurf->modelinfo, vplColor);
            }
            
            if (!rs->numPushedRays())
                continue;
            
            total_bounce_rays += rs->numPushedRays();
            rs->tracePushedRaysOcclusion();
            
            lightmap_t *lightmap = Lightmap_ForStyle(lightmaps, style, lightsurf);
            
            const int N = rs->numPushedRays();
            for (int j = 0; j < N; j++) {
                if (rs->getPushedRayOccluded(j))
                    continue;
                
                const int i = rs->getPushedRayPointIndex(j);
                vec3_t indirect = {0};
                rs->getPushedRayColor(j, indirect);
                
                Q_assert(!std::isnan(indirect[0]));
                
                /* Use dirt scaling on the indirect lighting.
                 * Except, not in bouncedebug mode.
                 */
                if (debugmode != debugmode_bounce) {
                    const vec_t dirtscale = Dirt_GetScaleFactor(cfg, lightsurf->occlusion[i], NULL, 0.0, lightsurf);
                    VectorScale(indirect, dirtscale, indirect);
                }
                
                lightsample_t *sample = &lightmap->samples[i];
                VectorAdd(sample->color, indirect, sample->color);
                
                hit = true;
                total_bounce_ray_hits++;
            }
            
            // If this style of this bounce light contributed anything, save.
            if (hit)
                Lightmap_Save(lightmaps, lightsurf, lightmap, style);
        }
    }

#else

    lightmap_t *lightmap = Lightmap_ForStyle(lightmaps, 0, lightsurf);

    const int N=1024;
    raystream_t *rs = MakeRayStream(N);

    for (int i = 0; i < lightsurf->numpoints; i++) {
        if (lightsurf->occluded[i])
            continue;
        
        const qvec3f surfpoint = vec3_t_to_glm(lightsurf->points[i]);
        const qvec3f surfnormal = vec3_t_to_glm(lightsurf->normals[i]);
        
        const qmat3x3f rotationMatrix = RotateFromUpToSurfaceNormal(surfnormal);
        
        rs->clearPushedRays();
        
        
        for (int j=0; j<N; j++) {
            const qvec3f randomDirInUpCoordSystem = CosineWeightedHemisphereSample(Random(), Random());
            const qvec3f rayDir = rotationMatrix * randomDirInUpCoordSystem;
            
            if (!(qv::dot(rayDir, surfnormal) > -0.01)) {
                //printf("bad dot\n");
            }
            
            rs->pushRay(i, surfpoint, rayDir, 8192, lightsurf->modelinfo);
        }
        
        rs->tracePushedRaysIntersection();
        
        qvec3f colorAvg(0);
        int Nhits = 0;
        
        for (int j=0; j<N; j++) {
            if (rs->getPushedRayHitType(j) == hittype_t::SKY)
                continue;
        
            const bsp2_dface_t *face = rs->getPushedRayHitFace(j);
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
                //printf("bad dot\n");
                continue;
            }
            
            if (!(fabs(1.0f - qv::length(raydir)) < 0.1)) {
                //printf("bad raydir: %f %f %f (len %f)\n", raydir.x, raydir.y, raydir.z, qv::length(raydir));
                continue;
            }
            
            const float dist = rs->getPushedRayHitDist(j);
            if (dist <= 0) {
                //printf("bad dist\n");
                continue;
            }
            
            const qplane3f plane = Face_Plane_E(bsp, face);
            float scale = qv::dot(plane.normal(), -raydir);
            if (scale < 0)
                scale = 0;
            
            //const qvec3f indirect = GetIndirectLighting(cfg, &vpl, color, -raydir, dist, surfpoint, surfnormal);
            
            Q_assert(!std::isnan(color.x));
            Q_assert(!std::isnan(color.y));
            Q_assert(!std::isnan(color.z));
            
            colorAvg += (color * scale * 255.0f);
            Nhits++;
        }
        
        if (Nhits) {
            colorAvg /= Nhits;
        
            lightsample_t *sample = &lightmap->samples[i];
        
            vec3_t indirectTmp;
            glm_to_vec3_t(colorAvg, indirectTmp);
            VectorAdd(sample->color, indirectTmp, sample->color);
        }
    }
    
    Lightmap_Save(lightmaps, lightsurf, lightmap, 0);
    delete rs;

#endif
}

static void
LightFace_OccludedDebug(lightsurf_t *lightsurf, lightmapdict_t *lightmaps)
{
    Q_assert(debugmode == debugmode_debugoccluded);
    
    /* use a style 0 light map */
    lightmap_t *lightmap = Lightmap_ForStyle(lightmaps, 0, lightsurf);
    
    /* Overwrite each point, red=occluded, green=ok */
    for (int i = 0; i < lightsurf->numpoints; i++) {
        lightsample_t *sample = &lightmap->samples[i];
        if (lightsurf->occluded[i]) {
            glm_to_vec3_t(qvec3f(255,0,0), sample->color);
        } else {
            glm_to_vec3_t(qvec3f(0,255,0), sample->color);
        }
        // N.B.: Mark it as un-occluded now, to disable special handling later in the -extra/-extra4 downscaling code
        lightsurf->occluded[i] = false;
    }
    
    Lightmap_Save(lightmaps, lightsurf, lightmap, 0);
}


/* Dirtmapping borrowed from q3map2, originally by RaP7oR */

#define DIRT_NUM_ANGLE_STEPS        16
#define DIRT_NUM_ELEVATION_STEPS    3
#define DIRT_NUM_VECTORS            ( DIRT_NUM_ANGLE_STEPS * DIRT_NUM_ELEVATION_STEPS )

static vec3_t dirtVectors[ DIRT_NUM_VECTORS ];
int numDirtVectors = 0;

/*
 * ============
 * SetupDirt
 *
 * sets up dirtmap (ambient occlusion)
 * ============
 */
void SetupDirt(globalconfig_t &cfg) {
    // check if needed
    
    if (!cfg.globalDirt.boolValue()
        && cfg.globalDirt.isLocked()) {
        // HACK: "-dirt 0" disables all dirtmapping even if we would otherwise use it.
        dirt_in_use = false;
        return;
    }
    
    if (cfg.globalDirt.boolValue()
        || cfg.minlightDirt.boolValue()
        || cfg.sunlight_dirt.boolValue()
        || cfg.sunlight2_dirt.boolValue()) {
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
    logprint("--- SetupDirt ---\n" );

    /* clamp dirtAngle */
    if ( cfg.dirtAngle.floatValue() <= 1.0f ) {
        cfg.dirtAngle.setFloatValueLocked(1.0f); // FIXME: add clamping API
    }
    if ( cfg.dirtAngle.floatValue() >= 90.0f) {
        cfg.dirtAngle.setFloatValueLocked(90.0f);
    }
    
    /* calculate angular steps */
    float angleStep = DEG2RAD( 360.0f / DIRT_NUM_ANGLE_STEPS );
    float elevationStep = DEG2RAD( cfg.dirtAngle.floatValue() / DIRT_NUM_ELEVATION_STEPS );

    /* iterate angle */
    float angle = 0.0f;
    for ( int i = 0; i < DIRT_NUM_ANGLE_STEPS; i++, angle += angleStep ) {
        /* iterate elevation */
        float elevation = elevationStep * 0.5f;
        for ( int j = 0; j < DIRT_NUM_ELEVATION_STEPS; j++, elevation += elevationStep ) {
            dirtVectors[ numDirtVectors ][ 0 ] = sin( elevation ) * cos( angle );
            dirtVectors[ numDirtVectors ][ 1 ] = sin( elevation ) * sin( angle );
            dirtVectors[ numDirtVectors ][ 2 ] = cos( elevation );
            numDirtVectors++;
        }
    }

    /* emit some statistics */
    logprint("%9d dirtmap vectors\n", numDirtVectors );
}

// from q3map2
static void
GetUpRtVecs(const vec3_t normal, vec3_t myUp, vec3_t myRt)
{
    /* check if the normal is aligned to the world-up */
    if ( normal[ 0 ] == 0.0f && normal[ 1 ] == 0.0f ) {
        if ( normal[ 2 ] == 1.0f ) {
            VectorSet( myRt, 1.0f, 0.0f, 0.0f );
            VectorSet( myUp, 0.0f, 1.0f, 0.0f );
        } else if ( normal[ 2 ] == -1.0f ) {
            VectorSet( myRt, -1.0f, 0.0f, 0.0f );
            VectorSet( myUp,  0.0f, 1.0f, 0.0f );
        }
    } else {
        vec3_t worldUp;
        VectorSet( worldUp, 0.0f, 0.0f, 1.0f );
        CrossProduct( normal, worldUp, myRt );
        VectorNormalize( myRt );
        CrossProduct( myRt, normal, myUp );
        VectorNormalize( myUp );
    }
}

// from q3map2
static void
TransformToTangentSpace(const vec3_t normal, const vec3_t myUp, const vec3_t myRt, const vec3_t inputvec, vec3_t outputvec)
{
    for (int i=0; i<3; i++)
        outputvec[i] = myRt[i] * inputvec[0] + myUp[i] * inputvec[1] + normal[i] * inputvec[2];
}

// from q3map2
static inline void
GetDirtVector(const globalconfig_t &cfg, int i, vec3_t out)
{
    Q_assert(i < numDirtVectors);
    
    if (cfg.dirtMode.intValue() == 1) {
        /* get random vector */
        float angle = Random() * DEG2RAD( 360.0f );
        float elevation = Random() * DEG2RAD( cfg.dirtAngle.floatValue() );
        out[ 0 ] = cos( angle ) * sin( elevation );
        out[ 1 ] = sin( angle ) * sin( elevation );
        out[ 2 ] = cos( elevation );
    } else {
        VectorCopy(dirtVectors[i], out);
    }
}

float
DirtAtPoint(const globalconfig_t &cfg, raystream_t *rs, const vec3_t point, const vec3_t normal, const modelinfo_t *modelinfo)
{
    if (!dirt_in_use) {
        return 0.0f;
    }
    
    vec3_t myUp, myRt;
    float occlusion = 0;
    
    // this stuff is just per-point
    
    GetUpRtVecs(normal, myUp, myRt);
    
    rs->clearPushedRays();
    
    for (int j=0; j<numDirtVectors; j++) {
        
        // fill in input buffers
    
        vec3_t dirtvec;
        GetDirtVector(cfg, j, dirtvec);
        
        vec3_t dir;
        TransformToTangentSpace(normal, myUp, myRt, dirtvec, dir);
        
        rs->pushRay(j, point, dir, cfg.dirtDepth.floatValue(), modelinfo);
    }
    
    Q_assert(rs->numPushedRays() == numDirtVectors);
    
    // trace the batch
    rs->tracePushedRaysIntersection();
    
    // accumulate hitdists
    for (int j=0; j<numDirtVectors; j++) {
        if (rs->getPushedRayHitType(j) == hittype_t::SOLID) {
            float dist = rs->getPushedRayHitDist(j);
            occlusion += qmin(cfg.dirtDepth.floatValue(), dist);
        } else {
            occlusion += cfg.dirtDepth.floatValue();
        }
    }
    
    // process the results.
    
    vec_t avgHitdist = occlusion / (float)numDirtVectors;
    occlusion = 1 - (avgHitdist / cfg.dirtDepth.floatValue());
    return occlusion;
}

/*
 * ============
 * LightFace_CalculateDirt
 * ============
 */
static void
LightFace_CalculateDirt(lightsurf_t *lightsurf)
{
    const globalconfig_t &cfg = *lightsurf->cfg;
    
    Q_assert(dirt_in_use);

    // batch implementation:

    vec3_t *myUps = (vec3_t *) calloc(lightsurf->numpoints, sizeof(vec3_t));
    vec3_t *myRts = (vec3_t *) calloc(lightsurf->numpoints, sizeof(vec3_t));
    
    // init
    for (int i = 0; i < lightsurf->numpoints; i++) {
        lightsurf->occlusion[i] = 0;
    }
    
    // this stuff is just per-point
    for (int i = 0; i < lightsurf->numpoints; i++) {
        GetUpRtVecs(lightsurf->normals[i], myUps[i], myRts[i]);
    }

    for (int j=0; j<numDirtVectors; j++) {
        raystream_t *rs = lightsurf->stream;
        rs->clearPushedRays();
        
        // fill in input buffers
        
        for (int i = 0; i < lightsurf->numpoints; i++) {
            if (lightsurf->occluded[i])
                continue;
            
            vec3_t dirtvec;
            GetDirtVector(cfg, j, dirtvec);
            
            vec3_t dir;
            TransformToTangentSpace(lightsurf->normals[i], myUps[i], myRts[i], dirtvec, dir);
            
            rs->pushRay(i, lightsurf->points[i], dir, cfg.dirtDepth.floatValue(), lightsurf->modelinfo);
        }
        
        // trace the batch
        rs->tracePushedRaysIntersection();
        
        // accumulate hitdists
        for (int k = 0; k < rs->numPushedRays(); k++) {
            const int i = rs->getPushedRayPointIndex(k);
            if (rs->getPushedRayHitType(k) == hittype_t::SOLID) {
                float dist = rs->getPushedRayHitDist(k);
                lightsurf->occlusion[i] += qmin(cfg.dirtDepth.floatValue(), dist);
            } else {
                lightsurf->occlusion[i] += cfg.dirtDepth.floatValue();
            }
        }
    }
    
    // process the results.
    for (int i = 0; i < lightsurf->numpoints; i++) {
        vec_t avgHitdist = lightsurf->occlusion[i] / (float)numDirtVectors;
        lightsurf->occlusion[i] = 1 - (avgHitdist / cfg.dirtDepth.floatValue());
    }

    free(myUps);
    free(myRts);
}

// clamps negative values. applies gamma and rangescale. clamps values over 255
// N.B. we want to do this before smoothing / downscaling, so huge values don't mess up the averaging.
static void
LightFace_ScaleAndClamp(const lightsurf_t *lightsurf, lightmapdict_t *lightmaps)
{
    const globalconfig_t &cfg = *lightsurf->cfg;
    
    for (lightmap_t &lightmap : *lightmaps) {
        for (int i = 0; i < lightsurf->numpoints; i++) {
            vec_t *color = lightmap.samples[i].color;
            
            /* Fix any negative values */
            for (int k = 0; k < 3; k++) {
                if (color[k] < 0) {
                    color[k] = 0;
                }
            }
            
            /* Scale and clamp any out-of-range samples */
            vec_t maxcolor = 0;
            VectorScale(color, cfg.rangescale.floatValue(), color);
            for (int i = 0; i < 3; i++) {
                color[i] = pow( color[i] / 255.0f, 1.0 / cfg.lightmapgamma.floatValue() ) * 255.0f;
            }
            for (int i = 0; i < 3; i++) {
                if (color[i] > maxcolor) {
                    maxcolor = color[i];
                }
            }
            if (maxcolor > 255) {
                VectorScale(color, 255.0f / maxcolor, color);
            }
        }
    }
}

static float
Lightmap_AvgBrightness(const lightmap_t *lm, const lightsurf_t *lightsurf) {
    float avgb = 0;
    for (int j=0; j<lightsurf->numpoints; j++) {
        avgb += LightSample_Brightness(lm->samples[j].color);
    }
    avgb /= lightsurf->numpoints;
    return avgb;
}

static float
Lightmap_MaxBrightness(const lightmap_t *lm, const lightsurf_t *lightsurf) {
    float maxb = 0;
    for (int j=0; j<lightsurf->numpoints; j++) {
        const float b = LightSample_Brightness(lm->samples[j].color);
        if (b > maxb) {
            maxb = b;
        }
    }
    return maxb;
}

static void
WritePPM(std::string fname, int width, int height, const uint8_t *rgbdata)
{
    FILE *file = fopen(fname.c_str(), "wb");
    
    // see: http://netpbm.sourceforge.net/doc/ppm.html
    fprintf(file, "P6 %d %d 255 ", width, height);
    int bytes = width*height*3;
    Q_assert(bytes == fwrite(rgbdata, 1, bytes, file));
    
    fclose(file);
}

static void
DumpFullSizeLightmap(const bsp2_t *bsp, const lightsurf_t *lightsurf)
{
    const lightmap_t *lm = Lightmap_ForStyle_ReadOnly(lightsurf, 0);
    if (lm != nullptr) {
        int fnum = Face_GetNum(bsp, lightsurf->face);
        
        char fname[1024];
        sprintf(fname, "face%04d.ppm", fnum);
        
        std::vector<uint8_t> rgbdata;
        for (int i=0; i<lightsurf->numpoints; i++) {
            const vec_t *color = lm->samples[i].color;
            for (int j=0; j<3; j++) {
                int intval = static_cast<int>(qclamp(color[j], 0.0f, 255.0f));
                rgbdata.push_back(static_cast<uint8_t>(intval));
            }
        }
        
        const int oversampled_width = (lightsurf->texsize[0] + 1) * oversample;
        const int oversampled_height = (lightsurf->texsize[1] + 1) * oversample;
        
        Q_assert(lightsurf->numpoints == (oversampled_height * oversampled_width));

        WritePPM(std::string{fname}, oversampled_width, oversampled_height, rgbdata.data());
    }
}

static void
DumpGLMVector(std::string fname, std::vector<qvec3f> vec, int width, int height)
{
    std::vector<uint8_t> rgbdata;
    for (int y=0; y<height; y++) {
        for (int x=0; x<width; x++) {
            const qvec3f sample = vec.at((y * width) + x);
            for (int j=0; j<3; j++) {
                int intval = static_cast<int>(qclamp(sample[j], 0.0f, 255.0f));
                rgbdata.push_back(static_cast<uint8_t>(intval));
            }
        }
    }
    Q_assert(rgbdata.size() == (width * height * 3));
    WritePPM(fname, width, height, rgbdata.data());
}

static void
DumpDownscaledLightmap(const bsp2_t *bsp, const bsp2_dface_t *face, int w, int h, const vec3_t *colors)
{
    int fnum = Face_GetNum(bsp, face);
    char fname[1024];
    sprintf(fname, "face-small%04d.ppm", fnum);
        
    std::vector<uint8_t> rgbdata;
    for (int i=0; i<(w*h); i++) {
        for (int j=0; j<3; j++) {
            int intval = static_cast<int>(qclamp(colors[i][j], 0.0f, 255.0f));
            rgbdata.push_back(static_cast<uint8_t>(intval));
        }
    }
    
    WritePPM(std::string{fname}, w, h, rgbdata.data());
}

static std::vector<qvec4f>
LightmapColorsToGLMVector(const lightsurf_t *lightsurf, const lightmap_t *lm)
{
    std::vector<qvec4f> res;
    for (int i=0; i<lightsurf->numpoints; i++) {
        const vec_t *color = lm->samples[i].color;
        const float alpha = lightsurf->occluded[i] ? 0.0f : 1.0f;
        res.push_back(qvec4f(color[0], color[1], color[2], alpha));
    }
    return res;
}

static std::vector<qvec4f>
LightmapNormalsToGLMVector(const lightsurf_t *lightsurf, const lightmap_t *lm)
{
    std::vector<qvec4f> res;
    for (int i=0; i<lightsurf->numpoints; i++) {
        const vec_t *color = lm->samples[i].direction;
        const float alpha = lightsurf->occluded[i] ? 0.0f : 1.0f;
        res.push_back(qvec4f(color[0], color[1], color[2], alpha));
    }
    return res;
}

static std::vector<qvec4f>
LightmapToGLMVector(const bsp2_t *bsp, const lightsurf_t *lightsurf)
{
    const lightmap_t *lm = Lightmap_ForStyle_ReadOnly(lightsurf, 0);
    if (lm != nullptr) {
        return LightmapColorsToGLMVector(lightsurf, lm);
    }
    return std::vector<qvec4f>();
}

static qvec3f
LinearToGamma22(const qvec3f &c) {
    return qv::pow(c, qvec3f(1/2.2f));
}

static qvec3f
Gamma22ToLinear(const qvec3f &c) {
    return qv::pow(c, qvec3f(2.2f));
}

void GLMVector_GammaToLinear(std::vector<qvec3f> &vec) {
    for (auto &v : vec) {
        v = Gamma22ToLinear(v);
    }
}

void GLMVector_LinearToGamma(std::vector<qvec3f> &vec) {
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
static std::vector<qvec4f>
IntegerDownsampleImage(const std::vector<qvec4f> &input, int w, int h, int factor)
{
    Q_assert(factor >= 1);
    if (factor == 1)
        return input;
    
    int outw = w/factor;
    int outh = h/factor;
    
    std::vector<qvec4f> res(static_cast<size_t>(outw * outh));
    
    for (int y=0; y<outh; y++) {
        for (int x=0; x<outw; x++) {

            float totalWeight = 0.0f;
            qvec3f totalColor(0);
            
            // These are only used if all the samples in the kernel have alpha = 0
            float totalWeightIgnoringOcclusion = 0.0f;
            qvec3f totalColorIgnoringOcclusion(0);
            
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
                    const qvec4f inSample = input.at((y1 * w) + x1);
                    
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

static std::vector<qvec4f>
BoxBlurImage(const std::vector<qvec4f> &input, int w, int h, int radius)
{
    std::vector<qvec4f> res(input.size());
    
    for (int y=0; y<h; y++) {
        for (int x=0; x<w; x++) {
            
            float totalWeight = 0.0f;
            qvec3f totalColor(0);
            
            // These are only used if all the samples in the kernel have alpha = 0
            float totalWeightIgnoringOcclusion = 0.0f;
            qvec3f totalColorIgnoringOcclusion(0);
            
            for (int y0 = -radius; y0 <= radius; y0++) {
                for (int x0 = -radius; x0 <= radius; x0++) {
                    const int x1 = x + x0;
                    const int y1 = y + y0;
                    
                    // check if the kernel goes outside of the source image
                    if (x1 < 0 || x1 >= w)
                        continue;
                    if (y1 < 0 || y1 >= h)
                        continue;
                    
                    // read the input sample
                    const float weight = 1.0f;
                    const qvec4f inSample = input.at((y1 * w) + x1);
                    
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

static void
WriteLightmaps(const bsp2_t *bsp, bsp2_dface_t *face, facesup_t *facesup, const lightsurf_t *lightsurf,
               const lightmapdict_t *lightmaps)
{
    // intermediate collection for sorting lightmaps
    std::vector<std::pair<float, const lightmap_t *>> sortable;
    
    for (const lightmap_t &lightmap : *lightmaps) {
        // skip un-saved lightmaps
        if (lightmap.style == 255)
            continue;
        
        // skip lightmaps where all samples have brightness below 1
        const float maxb = Lightmap_MaxBrightness(&lightmap, lightsurf);
        if (maxb < 1)
            continue;
        
        const float avgb = Lightmap_AvgBrightness(&lightmap, lightsurf);
        sortable.push_back({ avgb, &lightmap });
    }
    
    // sort in descending order of average brightness
    std::sort(sortable.begin(), sortable.end());
    std::reverse(sortable.begin(), sortable.end());
    
    std::vector<const lightmap_t *> sorted;
    for (const auto &pair : sortable) {
        if (sorted.size() == MAXLIGHTMAPS) {
            logprint("WARNING: Too many light styles on a face\n"
                     "         lightmap point near (%s)\n",
                     VecStr(lightsurf->points[0]));
            break;
        }
        
        sorted.push_back(pair.second);
    }
    
    /* final number of lightmaps */
    const int numstyles = static_cast<int>(sorted.size());
    Q_assert(numstyles <= MAXLIGHTMAPS);

    /* update face info (either core data or supplementary stuff) */
    if (facesup)
    {
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
    }
    else
    {
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

    int size = (lightsurf->texsize[0] + 1) * (lightsurf->texsize[1] + 1);
    byte *out, *lit, *lux;
    GetFileSpace(&out, &lit, &lux, size * numstyles);
    if (facesup) {
        facesup->lightofs = out - filebase;
    } else {
        face->lightofs = out - filebase;
    }

    // sanity check that we don't save a lightmap for a non-lightmapped face
    {
        const char *texname = Face_TextureName(bsp, face);
        Q_assert(!(bsp->texinfo[face->texinfo].flags & TEX_SPECIAL));
        Q_assert(Q_strcasecmp(texname, "skip") != 0);
        Q_assert(Q_strcasecmp(texname, "trigger") != 0);
    }
    
    const int actual_width = lightsurf->texsize[0] + 1;
    const int actual_height = lightsurf->texsize[1] + 1;
    
    const int oversampled_width = (lightsurf->texsize[0] + 1) * oversample;
    const int oversampled_height = (lightsurf->texsize[1] + 1) * oversample;
    
    for (int mapnum = 0; mapnum < numstyles; mapnum++) {
        const lightmap_t *lm = sorted.at(mapnum);
        
        // allocate new float buffers for the output colors and directions
        // these are the actual output width*height, without oversampling.
        
        std::vector<qvec4f> fullres = LightmapColorsToGLMVector(lightsurf, lm);
        if (softsamples > 0) {
            fullres = BoxBlurImage(fullres, oversampled_width, oversampled_height, softsamples);
        }
        
        const std::vector<qvec4f> output_color = IntegerDownsampleImage(fullres, oversampled_width, oversampled_height, oversample);
        const std::vector<qvec4f> output_dir = IntegerDownsampleImage(LightmapNormalsToGLMVector(lightsurf, lm), oversampled_width, oversampled_height, oversample);
        
        // copy from the float buffers to byte buffers in .bsp / .lit / .lux
        
        for (int t = 0; t < actual_height; t++) {
            for (int s = 0; s < actual_width; s++) {
                const int sampleindex = (t * actual_width) + s;
                const qvec4f &color = output_color.at(sampleindex);
                const qvec4f &direction = output_dir.at(sampleindex);
                
                *lit++ = color[0];
                *lit++ = color[1];
                *lit++ = color[2];
                
                /* Average the color to get the value to write to the
                 .bsp lightmap. this avoids issues with some engines
                 that require the lit and internal lightmap to have the same
                 intensity. (MarkV, some QW engines)
                 */
                vec_t light = LightSample_Brightness(color);
                if (light < 0) light = 0;
                if (light > 255) light = 255;
                *out++ = light;
                
                if (lux) {
                    vec3_t temp;
                    int v;
                    temp[0] = qv::dot(qvec3f(direction), vec3_t_to_glm(lightsurf->snormal));
                    temp[1] = qv::dot(qvec3f(direction), vec3_t_to_glm(lightsurf->tnormal));
                    temp[2] = qv::dot(qvec3f(direction), vec3_t_to_glm(lightsurf->plane.normal));
                    
                    if (!temp[0] && !temp[1] && !temp[2])
                        VectorSet(temp, 0, 0, 1);
                    else
                        VectorNormalize(temp);
                    
                    v = (temp[0]+1)*128;  *lux++ = (v>255)?255:v;
                    v = (temp[1]+1)*128;  *lux++ = (v>255)?255:v;
                    v = (temp[2]+1)*128;  *lux++ = (v>255)?255:v;
                }
            }
        }
    }
}

static void LightFaceShutdown(lightsurf_t *lightsurf)
{
    for (auto &lm : lightsurf->lightmapsByStyle) {
        free(lm.samples);
    }
    
    free(lightsurf->points);
    free(lightsurf->normals);
    free(lightsurf->occlusion);
    free(lightsurf->occluded);
    free(lightsurf->realfacenums);
    
    delete lightsurf->stream;
    
    delete lightsurf;
}

/*
 * ============
 * LightFace
 * ============
 */
void
LightFace(const bsp2_t *bsp, bsp2_dface_t *face, facesup_t *facesup, const globalconfig_t &cfg)
{
    /* Find the correct model offset */
    const modelinfo_t *modelinfo = ModelInfoForFace(bsp, Face_GetNum(bsp, face));
    if (modelinfo == nullptr) {
        return;
    }    
    
    const char *texname = Face_TextureName(bsp, face);
    
    /* One extra lightmap is allocated to simplify handling overflow */

    /* some surfaces don't need lightmaps */
    if (facesup)
    {
        facesup->lightofs = -1;
        for (int i = 0; i < MAXLIGHTMAPS; i++)
            facesup->styles[i] = 255;
    }
    else
    {
        face->lightofs = -1;
        for (int i = 0; i < MAXLIGHTMAPS; i++)
            face->styles[i] = 255;
    }
    if (bsp->texinfo[face->texinfo].flags & TEX_SPECIAL)
        return;
    
    /* don't save lightmaps for "trigger" texture */
    if (!Q_strcasecmp(texname, "trigger"))
        return;
    
    /* don't save lightmaps for "skip" texture */
    if (!Q_strcasecmp(texname, "skip"))
        return;
    
    /* don't bother with degenerate faces */
    if (face->numedges < 3)
        return;
    
    /* all good, this face is going to be lightmapped. */
    lightsurf_t *lightsurf = new lightsurf_t {};
    lightsurf->cfg = &cfg;
    
    /* if liquid doesn't have the TEX_SPECIAL flag set, the map was qbsp'ed with
     * lit water in mind. In that case receive light from both top and bottom.
     * (lit will only be rendered in compatible engines, but degrades gracefully.)
     */
    if (texname[0] == '*') {
        lightsurf->twosided = true;
    }
    
    Lightsurf_Init(modelinfo, face, bsp, lightsurf, facesup);
    lightmapdict_t *lightmaps = &lightsurf->lightmapsByStyle;

    /* calculate dirt (ambient occlusion) but don't use it yet */
    if (dirt_in_use && (debugmode != debugmode_phong))
        LightFace_CalculateDirt(lightsurf);

    /*
     * The lighting procedure is: cast all positive lights, fix
     * minlight levels, then cast all negative lights. Finally, we
     * clamp any values that may have gone negative.
     */

    if (debugmode == debugmode_none) {
        
        total_samplepoints += lightsurf->numpoints;
        
        /* positive lights */
        if (!modelinfo->lightignore.boolValue()) {
            for (const auto &entity : GetLights())
            {
                if (entity.getFormula() == LF_LOCALMIN)
                    continue;
                if (entity.light.floatValue() > 0)
                    LightFace_Entity(bsp, &entity, lightsurf, lightmaps);
            }
            for ( const sun_t &sun : GetSuns() )
                if (sun.sunlight > 0)
                    LightFace_Sky (&sun, lightsurf, lightmaps);

            /* add indirect lighting */
            LightFace_Bounce(bsp, face, lightsurf, lightmaps);
        }
        
        /* minlight - Use the greater of global or model minlight. */
        if (lightsurf->minlight > cfg.minlight.floatValue())
            LightFace_Min(bsp, face, lightsurf->minlight_color, lightsurf->minlight, lightsurf, lightmaps);
        else {
            const float light = cfg.minlight.floatValue();
            vec3_t color;
            VectorCopy(*cfg.minlight_color.vec3Value(), color);
            
            LightFace_Min(bsp, face, color, light, lightsurf, lightmaps);
        }

        /* negative lights */
        if (!modelinfo->lightignore.boolValue()) {
            for (const auto &entity : GetLights())
            {
                if (entity.getFormula() == LF_LOCALMIN)
                    continue;
                if (entity.light.floatValue() < 0)
                    LightFace_Entity(bsp, &entity, lightsurf, lightmaps);
            }
            for (const sun_t &sun : GetSuns())
                if (sun.sunlight < 0)
                    LightFace_Sky (&sun, lightsurf, lightmaps);
        }
    }
    
    /* bounce debug */
    // TODO: add a BounceDebug function that clear the lightmap to make the code more clear
    if (debugmode == debugmode_bounce)
        LightFace_Bounce(bsp, face, lightsurf, lightmaps);
    
    /* replace lightmaps with AO for debugging */
    if (debugmode == debugmode_dirt)
        LightFace_DirtDebug(lightsurf, lightmaps);

    if (debugmode == debugmode_phong)
        LightFace_PhongDebug(lightsurf, lightmaps);
    
    if (debugmode == debugmode_bouncelights)
        LightFace_BounceLightsDebug(lightsurf, lightmaps);
    
    if (debugmode == debugmode_debugoccluded)
        LightFace_OccludedDebug(lightsurf, lightmaps);
    
    /* Apply gamma, rangescale, and clamp */
    LightFace_ScaleAndClamp(lightsurf, lightmaps);
    
    WriteLightmaps(bsp, face, facesup, lightsurf, lightmaps);
    
    LightFaceShutdown(lightsurf);
}
