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
#include <light/entities.hh>
#include <light/trace.hh>
#include <light/ltface.hh>

#include <common/bsputils.hh>

#include <cassert>
#include <algorithm>

std::atomic<uint32_t> total_light_rays, total_light_ray_hits, total_samplepoints;
std::atomic<uint32_t> total_bounce_rays, total_bounce_ray_hits;

static void
PrintFaceInfo(const bsp2_dface_t *face, const bsp2_t *bsp);

/* ======================================================================== */


/*
 * To do arbitrary transformation of texture coordinates to world
 * coordinates requires solving for three simultaneous equations. We
 * set up the LU decomposed form of the transform matrix here.
 */
#define ZERO_EPSILON (0.001)
static qboolean
PMatrix3_LU_Decompose(pmatrix3_t *matrix)
{
    int i, j, k, tmp;
    vec_t max;
    int max_r, max_c;

    /* Do gauss elimination */
    for (i = 0; i < 3; ++i) {
        max = 0;
        max_r = max_c = i;
        for (j = i; j < 3; ++j) {
            for (k = i; k < 3; ++k) {
                if (fabs(matrix->data[j][k]) > max) {
                    max = fabs(matrix->data[j][k]);
                    max_r = j;
                    max_c = k;
                }
            }
        }

        /* Check for parallel planes */
        if (max < ZERO_EPSILON)
            return false;

        /* Swap rows/columns if necessary */
        if (max_r != i) {
            for (j = 0; j < 3; ++j) {
                max = matrix->data[i][j];
                matrix->data[i][j] = matrix->data[max_r][j];
                matrix->data[max_r][j] = max;
            }
            tmp = matrix->row[i];
            matrix->row[i] = matrix->row[max_r];
            matrix->row[max_r] = tmp;
        }
        if (max_c != i) {
            for (j = 0; j < 3; ++j) {
                max = matrix->data[j][i];
                matrix->data[j][i] = matrix->data[j][max_c];
                matrix->data[j][max_c] = max;
            }
            tmp = matrix->col[i];
            matrix->col[i] = matrix->col[max_c];
            matrix->col[max_c] = tmp;
        }

        /* Do pivot */
        for (j = i + 1; j < 3; ++j) {
            matrix->data[j][i] /= matrix->data[i][i];
            for (k = i + 1; k < 3; ++k)
                matrix->data[j][k] -= matrix->data[j][i] * matrix->data[i][k];
        }
    }

    return true;
}

static void
Solve3(const pmatrix3_t *matrix, const vec3_t rhs, vec3_t out)
{
    /* Use local short names just for readability (should optimize away) */
    const vec3_t *data = matrix->data;
    const int *r = matrix->row;
    const int *c = matrix->col;
    vec3_t tmp;

    /* forward-substitution */
    tmp[0] = rhs[r[0]];
    tmp[1] = rhs[r[1]] - data[1][0] * tmp[0];
    tmp[2] = rhs[r[2]] - data[2][0] * tmp[0] - data[2][1] * tmp[1];

    /* back-substitution */
    out[c[2]] = tmp[2] / data[2][2];
    out[c[1]] = (tmp[1] - data[1][2] * out[c[2]]) / data[1][1];
    out[c[0]] = (tmp[0] - data[0][1] * out[c[1]] - data[0][2] * out[c[2]])
        / data[0][0];
}

/*
 * ============================================================================
 * SAMPLE POINT DETERMINATION
 * void SetupBlock (bsp2_dface_t *f) Returns with surfpt[] set
 *
 * This is a little tricky because the lightmap covers more area than the face.
 * If done in the straightforward fashion, some of the sample points will be
 * inside walls or on the other side of walls, causing false shadows and light
 * bleeds.
 *
 * To solve this, I only consider a sample point valid if a line can be drawn
 * between it and the exact midpoint of the face.  If invalid, it is adjusted
 * towards the center until it is valid.
 *
 * FIXME: This doesn't completely work; I think what we really want is to move
 *        the light point to the nearst sample point that is on the polygon;
 * ============================================================================
 */

/*
 * Functions to aid in calculation of polygon centroid
 */
static void
TriCentroid(const dvertex_t *v0, const dvertex_t *v1, const dvertex_t *v2,
            vec3_t out)
{
    for (int i = 0; i < 3; i++)
        out[i] = (v0->point[i] + v1->point[i] + v2->point[i]) / 3.0;
}

static vec_t
TriArea(const dvertex_t *v0, const dvertex_t *v1, const dvertex_t *v2)
{
    vec3_t edge0, edge1, cross;

    for (int i =0; i < 3; i++) {
        edge0[i] = v1->point[i] - v0->point[i];
        edge1[i] = v2->point[i] - v0->point[i];
    }
    CrossProduct(edge0, edge1, cross);

    return VectorLength(cross) * 0.5;
}

void
FaceCentroid(const bsp2_dface_t *face, const bsp2_t *bsp, vec3_t out)
{
    int edgenum;
    dvertex_t *v0, *v1, *v2;
    vec3_t centroid, poly_centroid;
    vec_t area, poly_area;

    VectorCopy(vec3_origin, poly_centroid);
    poly_area = 0;

    edgenum = bsp->dsurfedges[face->firstedge];
    if (edgenum >= 0)
        v0 = bsp->dvertexes + bsp->dedges[edgenum].v[0];
    else
        v0 = bsp->dvertexes + bsp->dedges[-edgenum].v[1];

    for (int i = 1; i < face->numedges - 1; i++) {
        edgenum = bsp->dsurfedges[face->firstedge + i];
        if (edgenum >= 0) {
            v1 = bsp->dvertexes + bsp->dedges[edgenum].v[0];
            v2 = bsp->dvertexes + bsp->dedges[edgenum].v[1];
        } else {
            v1 = bsp->dvertexes + bsp->dedges[-edgenum].v[1];
            v2 = bsp->dvertexes + bsp->dedges[-edgenum].v[0];
        }

        area = TriArea(v0, v1, v2);
        poly_area += area;

        TriCentroid(v0, v1, v2, centroid);
        VectorMA(poly_centroid, area, centroid, poly_centroid);
    }

    VectorScale(poly_centroid, 1.0 / poly_area, out);
}

/*
 * ================
 * CreateFaceTransform
 * Fills in the transform matrix for converting tex coord <-> world coord
 * ================
 */
static void
CreateFaceTransform(const bsp2_dface_t *face, const bsp2_t *bsp,
                    pmatrix3_t *transform)
{
    /* Prepare the transform matrix and init row/column permutations */
    const dplane_t *plane = &bsp->dplanes[face->planenum];
    const texinfo_t *tex = &bsp->texinfo[face->texinfo];
    for (int i = 0; i < 3; i++) {
        transform->data[0][i] = tex->vecs[0][i];
        transform->data[1][i] = tex->vecs[1][i];
        transform->data[2][i] = plane->normal[i];
        transform->row[i] = transform->col[i] = i;
    }
    if (face->side)
        VectorSubtract(vec3_origin, transform->data[2], transform->data[2]);

    /* Decompose the matrix. If we can't, texture axes are invalid. */
    if (!PMatrix3_LU_Decompose(transform)) {
        logprint("Bad texture axes on face:\n");
        PrintFaceInfo(face, bsp);
        Error("CreateFaceTransform");
    }
}

static void
TexCoordToWorld(vec_t s, vec_t t, const texorg_t *texorg, vec3_t world)
{
    vec3_t rhs;

    rhs[0] = s - texorg->texinfo->vecs[0][3];
    rhs[1] = t - texorg->texinfo->vecs[1][3];
    // FIXME: This could be more or less than one unit in world space?
    rhs[2] = texorg->planedist + 1; /* one "unit" in front of surface */

    Solve3(&texorg->transform, rhs, world);
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
static void
PrintFaceInfo(const bsp2_dface_t *face, const bsp2_t *bsp)
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
        const vec_t *norm = GetSurfaceVertexNormal(bsp, face, i);
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
    FaceCentroid(face, bsp, worldpoint);
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

/*
 * Print warning for CalcPoint where the midpoint of a polygon, one
 * unit above the surface is covered by a solid brush.
 */
static void
WarnBadMidpoint(const vec3_t point)
{
#if 0
    static qboolean warned = false;

    if (warned)
        return;

    warned = true;
    logprint("WARNING: unable to lightmap surface near (%s)\n"
             "   This is usually caused by an unintentional tiny gap between\n"
             "   two solid brushes which doesn't leave enough room for the\n"
             "   lightmap to fit (one world unit). Further instances of this\n"
             "   warning during this compile will be supressed.\n",
             VecStr(point));
#endif
}

static vec_t
TriangleArea(const vec3_t v0, const vec3_t v1, const vec3_t v2)
{
    vec3_t edge0, edge1, cross;
    VectorSubtract(v2, v0, edge0);
    VectorSubtract(v1, v0, edge1);
    CrossProduct(edge0, edge1, cross);
    
    return VectorLength(cross) * 0.5;
}


static void CalcBarycentric(const vec_t *p, const vec_t *a, const vec_t *b, const vec_t *c, vec_t *res)
{
    vec3_t v0,v1,v2;
    VectorSubtract(b, a, v0);
    VectorSubtract(c, a, v1);
    VectorSubtract(p, a, v2);
    float d00 = DotProduct(v0, v0);
    float d01 = DotProduct(v0, v1);
    float d11 = DotProduct(v1, v1);
    float d20 = DotProduct(v2, v0);
    float d21 = DotProduct(v2, v1);
    float invDenom = (d00 * d11 - d01 * d01);
    invDenom = 1.0/invDenom;
    res[1] = (d11 * d20 - d01 * d21) * invDenom;
    res[2] = (d00 * d21 - d01 * d20) * invDenom;
    res[0] = 1.0f - res[1] - res[2];
}

// from: http://stackoverflow.com/a/1501725
// see also: http://mathworld.wolfram.com/Projection.html
static vec_t
FractionOfLine(const vec3_t v, const vec3_t w, const vec3_t p) {
    vec3_t vp, vw;
    VectorSubtract(p, v, vp);
    VectorSubtract(w, v, vw);
    
    const float l2 = DotProduct(vw, vw);
    if (l2 == 0) {
        return 0;
    }
    
    const vec_t t = DotProduct(vp, vw) / l2;
    return t;
}

static void CalcPointNormal(const bsp2_t *bsp, const bsp2_dface_t *face, vec_t *norm, const vec_t *point, int inside)
{
    plane_t surfplane = Face_Plane(bsp, face);
    
    // project `point` onto the surface plane (it's hovering 1 unit above)
    vec3_t pointOnPlane;
    {
        vec_t dist = DotProduct(point, surfplane.normal) - surfplane.dist;
        VectorMA(point, -dist, surfplane.normal, pointOnPlane);
    }
    
    const vec_t *v1, *v2, *v3;

    /* now just walk around the surface as a triangle fan */
    v1 = GetSurfaceVertexPoint(bsp, face, 0);
    v2 = GetSurfaceVertexPoint(bsp, face, 1);
    for (int j = 2; j < face->numedges; j++)
    {
        v3 = GetSurfaceVertexPoint(bsp, face, j);
  
        vec3_t bary;
        CalcBarycentric(pointOnPlane, v1, v2, v3, bary);
        
        // N.B. need a small epsilon here because the barycentric coordinates are normalized to 0-1
        const vec_t BARY_EPSILON = 0.001;
        if ((bary[0] > -BARY_EPSILON) && (bary[1] > -BARY_EPSILON) && (bary[0] + bary[1] < 1+BARY_EPSILON))
        {
            // area test rejects the case when v1, v2, v3 are colinear
            if (TriangleArea(v1, v2, v3) >= 1) {
                
                v1 = GetSurfaceVertexNormal(bsp, face, 0);
                v2 = GetSurfaceVertexNormal(bsp, face, j-1);
                v3 = GetSurfaceVertexNormal(bsp, face, j);
                VectorScale(v1, bary[0], norm);
                VectorMA(norm, bary[1], v2, norm);
                VectorMA(norm, bary[2], v3, norm);
                VectorNormalize(norm);
                return;
            }
        }
        v2 = v3;
    }

    // not in any triangle. among the edges this point is _behind_,
    // search for the one that the point is least past the endpoints of the edge
    {
        plane_t *edgeplanes = (plane_t *)calloc(face->numedges, sizeof(plane_t));
        Face_MakeInwardFacingEdgePlanes(bsp, face, edgeplanes);
        
        int bestplane = -1;
        vec_t bestdist = VECT_MAX;
        
        for (int i=0; i<face->numedges; i++) {
            vec_t planedist = DotProduct(point, edgeplanes[i].normal) - edgeplanes[i].dist;
            if (planedist < ON_EPSILON) {
                // behind this plane. check whether we're between the endpoints.
                
                v1 = GetSurfaceVertexPoint(bsp, face, i);
                v2 = GetSurfaceVertexPoint(bsp, face, (i+1)%face->numedges);
                
                vec3_t v1v2;
                VectorSubtract(v2, v1, v1v2);
                const vec_t v1v2dist = VectorLength(v1v2);
                
                const vec_t t = FractionOfLine(v1, v2, point); // t=0 for point=v1, t=1 for point=v2.
                
                vec_t edgedist;
                if (t < 0) edgedist = fabs(t) * v1v2dist;
                else if (t > 1) edgedist = t * v1v2dist;
                else edgedist = 0;
                
                if (edgedist < bestdist) {
                    bestplane = i;
                    bestdist = edgedist;
                }
            }
        }
        
        
        if (bestplane != -1) {
            const bsp2_dface_t *smoothed = Face_EdgeIndexSmoothed(bsp, face, bestplane);
            if (smoothed) {
                // try recursive search
                if (inside < 3) {
                    free(edgeplanes);
                    
                    // call recursively to look up normal in the adjacent face
                    CalcPointNormal(bsp, smoothed, norm, point, inside + 1);
                    return;
                }
            }

            v1 = GetSurfaceVertexPoint(bsp, face, bestplane);
            v2 = GetSurfaceVertexPoint(bsp, face, (bestplane+1)%face->numedges);
            
            vec_t t = FractionOfLine(v1, v2, point);
            t = qmax(qmin(t, 1.0f), 0.0f);
            
            v1 = GetSurfaceVertexNormal(bsp, face, bestplane);
            v2 = GetSurfaceVertexNormal(bsp, face, (bestplane+1)%face->numedges);
            
            VectorScale(v2, t, norm);
            VectorMA(norm, 1-t, v1, norm);
            VectorNormalize(norm);
            
            free(edgeplanes);
            return;
        }
        
        free(edgeplanes);
    }

    /*utterly crap, just for testing. just grab closest vertex*/
    vec_t bestd = VECT_MAX;
    int bestv = -1;
    VectorSet(norm, 0, 0, 0);
    for (int i = 0; i < face->numedges; i++)
    {
        vec3_t t;
        int v = Face_VertexAtIndex(bsp, face, i);
        VectorSubtract(point, bsp->dvertexes[v].point, t);
        const vec_t dist = VectorLength(t);
        if (dist < bestd)
        {
            bestd = dist;
            bestv = v;
            VectorCopy(GetSurfaceVertexNormal(bsp, face, i), norm);
        }
    }
    VectorNormalize(norm);
}

static bool
CheckObstructed(const lightsurf_t *surf, const vec3_t offset, const vec_t us, const vec_t ut, vec3_t corrected)
{
    for (int x = -1; x <= 1; x += 2) {
        for (int y = -1; y <= 1; y += 2) {
            vec3_t testpoint;
            TexCoordToWorld(us + (x/10.0), ut + (y/10.0), &surf->texorg, testpoint);
            VectorAdd(testpoint, offset, testpoint);
            
            vec3_t dirn;
            VectorSubtract(testpoint, surf->midpoint, dirn);
            vec_t dist = VectorNormalize(dirn);
            if (dist == 0.0f) {
                continue; // testpoint == surf->midpoint
            }
            
            // trace from surf->midpoint to testpoint
            vec_t hitdist = 0;
            if (IntersectSingleModel(surf->midpoint, dirn, dist, surf->modelinfo->model, &hitdist)) {
                // make a corrected point
                VectorMA(surf->midpoint, qmax(0.0f, hitdist - 0.25f), dirn, corrected);
                return true;
            }
        }
    }
    return false;
}

// Dump points to a .map file
static void
CalcPoints_Debug(const lightsurf_t *surf, const bsp2_t *bsp)
{
    const int facenum = surf->face - bsp->dfaces;
    FILE *f = fopen("calcpoints.map", "w");
    
    for (int t = 0; t < surf->height; t++) {
        for (int s = 0; s < surf->width; s++) {
            const int i = t*surf->width + s;
            const vec_t *point = surf->points[i];
            
            fprintf(f, "{\n");
            fprintf(f, "\"classname\" \"light\"\n");
            fprintf(f, "\"origin\" \"%f %f %f\"\n", point[0], point[1], point[2]);
            fprintf(f, "\"face\" \"%d\"\n", facenum);
            fprintf(f, "\"s\" \"%d\"\n", s);
            fprintf(f, "\"t\" \"%d\"\n", t);
            fprintf(f, "}\n");
        }
    }
    
    fclose(f);
    
    logprint("wrote face %d's sample points (%dx%d) to calcpoints.map\n",
             facenum, surf->width, surf->height);

    PrintFaceInfo(surf->face, bsp);
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
    surf->starts = (surf->texmins[0] - 0.5 + (0.5 / oversample)) * surf->lightmapscale;
    surf->startt = (surf->texmins[1] - 0.5 + (0.5 / oversample)) * surf->lightmapscale;
    surf->st_step = surf->lightmapscale / oversample;

    /* Allocate surf->points */
    surf->numpoints = surf->width * surf->height;
    surf->points = (vec3_t *) calloc(surf->numpoints, sizeof(vec3_t));
    surf->normals = (vec3_t *) calloc(surf->numpoints, sizeof(vec3_t));
    surf->occluded = (bool *)calloc(surf->numpoints, sizeof(bool));
    
    for (int t = 0; t < surf->height; t++) {
        for (int s = 0; s < surf->width; s++) {
            const int i = t*surf->width + s;
            vec_t *point = surf->points[i];
            vec_t *norm = surf->normals[i];
            
            const vec_t us = surf->starts + s * surf->st_step;
            const vec_t ut = surf->startt + t * surf->st_step;

            TexCoordToWorld(us, ut, &surf->texorg, point);

            // do this before correcting the point, so we can wrap around the inside of pipes
            if (surf->curved && cfg.phongallowed.boolValue())
            {
                CalcPointNormal(bsp, face, norm, point, 0);
            }
            else
            {
                VectorCopy(surf->plane.normal, norm);
            }
            
            // apply model offset after calling CalcPointNormal
            VectorAdd(point, offset, point);
            
            // corrects point
            CheckObstructed(surf, offset, us, ut, point);
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
        if (lightsurf->minlight > 0 && VectorCompare(extended_mincolor, vec3_origin)) {
            VectorSet(extended_mincolor, 255, 255, 255);
        }
        VectorCopy(extended_mincolor, lightsurf->minlight_color);
    }
    
    /* never receive dirtmapping on lit liquids */
    if (Face_IsLiquid(bsp, face)) {
        lightsurf->nodirt = true;
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
    CreateFaceTransform(face, bsp, &lightsurf->texorg.transform);
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
 * Average adjacent points on the grid to soften shadow edges
 */
static void
Lightmap_Soften(lightmap_t *lightmap, const lightsurf_t *lightsurf)
{
    const int width = (lightsurf->texsize[0] + 1) * oversample;
    const int height = (lightsurf->texsize[1] + 1) * oversample;
    const int fullsamples = (2 * softsamples + 1) * (2 * softsamples + 1);

    lightsample_t *softmap = (lightsample_t *) calloc(lightsurf->numpoints, sizeof(lightsample_t));
    
    lightsample_t *dst = softmap;
    for (int i = 0; i < lightsurf->numpoints; i++, dst++) {
        const int startt = qmax((i / width) - softsamples, 0);
        const int endt = qmin((i / width) + softsamples + 1, height);
        const int starts = qmax((i % width) - softsamples, 0);
        const int ends = qmin((i % width) + softsamples + 1, width);

        for (int t = startt; t < endt; t++) {
            for (int s = starts; s < ends; s++) {
                const lightsample_t *src = &lightmap->samples[t * width + s];
                VectorAdd(dst->color, src->color, dst->color);
                VectorAdd(dst->direction, src->direction, dst->direction);
            }
        }
        /*
         * For cases where we are softening near the edge of the lightmap,
         * take extra samples from the centre point (follows old bjp tools
         * behaviour)
         */
        int samples = (endt - startt) * (ends - starts);
        if (samples < fullsamples) {
            const int extraweight = 2 * (fullsamples - samples);
            const lightsample_t *src = &lightmap->samples[i];
            VectorMA(dst->color, extraweight, src->color, dst->color);
            VectorMA(dst->direction, extraweight, src->direction, dst->direction);
            samples += extraweight;
        }
        VectorScale(dst->color, 1.0 / samples, dst->color);
        VectorScale(dst->direction, 1.0 / samples, dst->direction);
    }

    memcpy(lightmap->samples, softmap, lightsurf->numpoints * sizeof(lightsample_t));
    free(softmap);
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

/*
 * ============
 * Dirt_GetScaleFactor
 *
 * returns scale factor for dirt/ambient occlusion
 * ============
 */
static inline vec_t
Dirt_GetScaleFactor(const globalconfig_t &cfg, vec_t occlusion, const light_t *entity, const lightsurf_t *surf)
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

static void
ProjectPointOntoPlane(const vec3_t point, const plane_t *plane, vec3_t out)
{
    vec_t dist = DotProduct(point, plane->normal) - plane->dist;
    VectorMA(point, -dist, plane->normal, out);
}

// FIXME: factor out / merge with LightFace
void
GetDirectLighting(const globalconfig_t &cfg, raystream_t *rs, const vec3_t origin, const vec3_t normal, vec3_t colorout)
{
    const float occlusion = DirtAtPoint(cfg, rs, origin, normal, /* FIXME: pass selfshadow? */ nullptr);
    VectorSet(colorout, 0, 0, 0);
    
    for (const light_t &entity : GetLights()) {
        vec3_t surfpointToLightDir;
        float surfpointToLightDist;
        vec3_t color, normalcontrib;

        // NOTE: skip styled lights
        if (entity.style.intValue() != 0) {
            continue;
        }
        
        GetLightContrib(cfg, &entity, normal, origin, false, color, surfpointToLightDir, normalcontrib, &surfpointToLightDist);
        
        const float dirt = Dirt_GetScaleFactor(cfg, occlusion, &entity, /* FIXME: pass */ nullptr);
        VectorScale(color, dirt, color);
        
        // NOTE: Skip negative lights, which would make no sense to bounce!
        if (LightSample_Brightness(color) <= fadegate) {
            continue;
        }
        
        if (!TestLight(*entity.origin.vec3Value(), origin, NULL)) {
            continue;
        }
        
        VectorAdd(colorout, color, colorout);
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
            dirt = Dirt_GetScaleFactor(cfg, occlusion, nullptr, /* FIXME: pass */ nullptr);
        }
        
        VectorMA(colorout, dirt * cosangle * sun.sunlight / 255.0f, sun.sunlight_color, colorout);
    }
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
    bool hit = false;
    lightmap_t *lightmap = Lightmap_ForStyle(lightmaps, entity->style.intValue(), lightsurf);
    const dmodel_t *shadowself = modelinfo->shadowself.boolValue() ? modelinfo->model : NULL;
    
    raystream_t *rs = lightsurf->stream;
    rs->clearPushedRays();
    
    for (int i = 0; i < lightsurf->numpoints; i++) {
        const vec_t *surfpoint = lightsurf->points[i];
        const vec_t *surfnorm = lightsurf->normals[i];
        
        vec3_t surfpointToLightDir;
        float surfpointToLightDist;
        vec3_t color, normalcontrib;
        
        GetLightContrib(cfg, entity, surfnorm, surfpoint, lightsurf->twosided, color, surfpointToLightDir, normalcontrib, &surfpointToLightDist);
 
        const float occlusion = Dirt_GetScaleFactor(cfg, lightsurf->occlusion[i], entity, lightsurf);
        VectorScale(color, occlusion, color);
        
        /* Quick distance check first */
        if (fabs(LightSample_Brightness(color)) <= fadegate) {
            continue;
        }
        
        rs->pushRay(i, surfpoint, surfpointToLightDir, surfpointToLightDist, shadowself, color, normalcontrib);
    }
    
    rs->tracePushedRaysOcclusion();
    total_light_rays += rs->numPushedRays();
    
    const int N = rs->numPushedRays();
    for (int j = 0; j < N; j++) {
        if (rs->getPushedRayOccluded(j)) {
            continue;
        }

        total_light_ray_hits++;
        
        int i = rs->getPushedRayPointIndex(j);
        lightsample_t *sample = &lightmap->samples[i];
        
        vec3_t color, normalcontrib;
        rs->getPushedRayColor(j, color);
        rs->getPushedRayNormalContrib(j, normalcontrib);

        VectorAdd(sample->color, color, sample->color);
        VectorAdd(sample->direction, normalcontrib, sample->direction);
        
        hit = true;
    }
    
    if (hit)
        Lightmap_Save(lightmaps, lightsurf, lightmap, entity->style.intValue());
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

    /* if sunlight is set, use a style 0 light map */
    lightmap_t *lightmap = Lightmap_ForStyle(lightmaps, 0, lightsurf);

    vec3_t incoming;
    VectorCopy(sun->sunvec, incoming);
    VectorNormalize(incoming);
    
    /* Check each point... */
    bool hit = false;
    const dmodel_t *shadowself = modelinfo->shadowself.boolValue() ? modelinfo->model : NULL;

    raystream_t *rs = lightsurf->stream;
    rs->clearPushedRays();
    
    for (int i = 0; i < lightsurf->numpoints; i++) {
        const vec_t *surfpoint = lightsurf->points[i];
        const vec_t *surfnorm = lightsurf->normals[i];
        
        float angle = DotProduct(incoming, surfnorm);
        if (lightsurf->twosided) {
            if (angle < 0) {
                angle = -angle;
            }
        }
        
        if (angle < 0) {
            continue;
        }
        
        rs->pushRay(i, surfpoint, incoming, MAX_SKY_DIST, shadowself);
    }
    
    rs->tracePushedRaysIntersection();
    
    const int N = rs->numPushedRays();
    for (int j = 0; j < N; j++) {
        if (rs->getPushedRayHitType(j) != hittype_t::SKY) {
            continue;
        }
        
        const int i = rs->getPushedRayPointIndex(j);
        const vec_t *surfnorm = lightsurf->normals[i];
        
        // FIXME: don't recompute this: compute before tracing, check gate, and store color in ray
        float angle = DotProduct(incoming, surfnorm);
        if (lightsurf->twosided) {
            if (angle < 0) {
                angle = -angle;
            }
        }
        
        angle = (1.0 - sun->anglescale) + sun->anglescale * angle;
        float value = angle * sun->sunlight;
        if (sun->dirt) {
            value *= Dirt_GetScaleFactor(cfg, lightsurf->occlusion[i], NULL, lightsurf);
        }
        
        lightsample_t *sample = &lightmap->samples[i];
        Light_Add(sample, value, sun->sunlight_color, sun->sunvec);

        hit = true;
    }

    if (hit)
        Lightmap_Save(lightmaps, lightsurf, lightmap, 0);
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
            value *= Dirt_GetScaleFactor(cfg, lightsurf->occlusion[i], NULL, lightsurf);
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
    const dmodel_t *shadowself = modelinfo->shadowself.boolValue() ? modelinfo->model : NULL;
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
            const lightsample_t *sample = &lightmap->samples[i];
            const vec_t *surfpoint = lightsurf->points[i];
            if (cfg.addminlight.boolValue() || LightSample_Brightness(sample->color) < entity.light.floatValue()) {
                vec3_t surfpointToLightDir;
                vec_t surfpointToLightDist = GetDir(surfpoint, *entity.origin.vec3Value(), surfpointToLightDir);
                
                rs->pushRay(i, surfpoint, surfpointToLightDir, surfpointToLightDist, shadowself);
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
            
            value *= Dirt_GetScaleFactor(cfg, lightsurf->occlusion[i], &entity, lightsurf);
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
        const float light = 255 * Dirt_GetScaleFactor(cfg, lightsurf->occlusion[i], NULL, lightsurf);
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
    
    /* use a style 0 light map */
    lightmap_t *lightmap = Lightmap_ForStyle(lightmaps, 0, lightsurf);
    
    vec3_t patch_color = {0,0,0};
    std::vector<bouncelight_t> vpls = BounceLightsForFaceNum(Face_GetNum(lightsurf->bsp, lightsurf->face));
    if (vpls.size()) {
        Q_assert(vpls.size() == 1); // for now only 1 vpl per face
        
        const auto &vpl = vpls.at(0);
        VectorScale(vpl.color, 255, patch_color);
    }
    
    /* Overwrite each point with the emitted color... */
    for (int i = 0; i < lightsurf->numpoints; i++) {
        lightsample_t *sample = &lightmap->samples[i];
        VectorCopy(patch_color, sample->color);
    }
    
    Lightmap_Save(lightmaps, lightsurf, lightmap, 0);
}

// returns color in [0,255]
static inline void
BounceLight_ColorAtDist(const globalconfig_t &cfg, const bouncelight_t *vpl, vec_t dist, vec3_t color)
{
    // get light contribution
    VectorScale(vpl->color, vpl->area, color);
    
    // clamp away hotspots
    if (dist < 128) {
        dist = 128;
    }
    
    const vec_t dist2 = (dist * dist);
    const vec_t scale = (1.0/dist2) * cfg.bouncescale.floatValue();
    
    VectorScale(color, 255 * scale, color);
}

// dir: vpl -> sample point direction
// returns color in [0,255]
static inline void
GetIndirectLighting (const globalconfig_t &cfg, const bouncelight_t *vpl, const vec3_t dir, vec_t dist, const vec3_t origin, const vec3_t normal, vec3_t color)
{
    VectorSet(color, 0, 0, 0);
    
#if 0
    vec3_t dir;
    VectorSubtract(origin, vpl->pos, dir); // vpl -> sample point
    vec_t dist = VectorNormalize(dir);
#endif
    
    const vec_t dp1 = DotProduct(vpl->surfnormal, dir);
    if (dp1 < 0)
        return; // sample point behind vpl
    
    vec3_t sp_vpl;
    VectorScale(dir, -1, sp_vpl);
    
    const vec_t dp2 = DotProduct(sp_vpl, normal);
    if (dp2 < 0)
        return; // vpl behind sample face
    
    // get light contribution
    BounceLight_ColorAtDist(cfg, vpl, dist, color);
    
    // apply angle scale
    VectorScale(color, dp1 * dp2, color);
}

static inline bool
BounceLight_SphereCull(const bsp2_t *bsp, const bouncelight_t *vpl, const lightsurf_t *lightsurf)
{
    const globalconfig_t &cfg = *lightsurf->cfg;
    
    if (!novisapprox && AABBsDisjoint(vpl->mins, vpl->maxs, lightsurf->mins, lightsurf->maxs))
        return true;
    
    vec3_t color = {0};
    //GetIndirectLighting(bsp, vpl, lightsurf->face, lightsurf->pvs, lightsurf->origin, lightsurf->plane.normal, color);
    
    vec3_t dir;
    VectorSubtract(lightsurf->origin, vpl->pos, dir); // vpl -> sample point
    vec_t dist = VectorLength(dir) + lightsurf->radius;
    
    // get light contribution
    BounceLight_ColorAtDist(cfg, vpl, dist, color);
    
    if (LightSample_Brightness(color) < 0.25)
        return true;
    
    return false;
}

static void
LightFace_Bounce(const bsp2_t *bsp, const bsp2_dface_t *face, const lightsurf_t *lightsurf, lightmapdict_t *lightmaps)
{
    const globalconfig_t &cfg = *lightsurf->cfg;
    //const dmodel_t *shadowself = lightsurf->modelinfo->shadowself.boolValue() ? lightsurf->modelinfo->model : NULL;
    lightmap_t *lightmap;
    
    if (!cfg.bounce.boolValue())
        return;
    
    if (!(debugmode == debugmode_bounce
          || debugmode == debugmode_none))
        return;
    
    /* use a style 0 light map */
    lightmap = Lightmap_ForStyle(lightmaps, 0, lightsurf);
    
    bool hit = false;
    
    for (const bouncelight_t &vpl : BounceLights()) {
        if (BounceLight_SphereCull(bsp, &vpl, lightsurf))
            continue;
        
        raystream_t *rs = lightsurf->stream;
        rs->clearPushedRays();
        
        for (int i = 0; i < lightsurf->numpoints; i++) {
            vec3_t dir; // vpl -> sample point
            VectorSubtract(lightsurf->points[i], vpl.pos, dir);
            vec_t dist = VectorNormalize(dir);
            
            vec3_t indirect = {0};
            GetIndirectLighting(cfg, &vpl, dir, dist, lightsurf->points[i], lightsurf->normals[i], indirect);
            
            if (LightSample_Brightness(indirect) < 0.25)
                continue;
            
            rs->pushRay(i, vpl.pos, dir, dist, /*shadowself*/ nullptr, indirect);
        }
        
        total_bounce_rays += rs->numPushedRays();
        rs->tracePushedRaysOcclusion();
        
        const int N = rs->numPushedRays();
        for (int j = 0; j < N; j++) {
            if (rs->getPushedRayOccluded(j))
                continue;
            
            const int i = rs->getPushedRayPointIndex(j);
            vec3_t indirect = {0};
            rs->getPushedRayColor(j, indirect);
            Q_assert(LightSample_Brightness(indirect) >= 0.25);
            
            /* Use dirt scaling on the indirect lighting.
             * Except, not in bouncedebug mode.
             */
            if (debugmode != debugmode_bounce) {
                const vec_t dirtscale = Dirt_GetScaleFactor(cfg, lightsurf->occlusion[i], NULL, lightsurf);
                VectorScale(indirect, dirtscale, indirect);
            }
            
            lightsample_t *sample = &lightmap->samples[i];
            VectorAdd(sample->color, indirect, sample->color);
            
            hit = true;
            total_bounce_ray_hits++;
        }
    }
    
    if (hit)
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

#if 0
static const lightmap_t *
Lightmap_ForStyle_ReadOnly(const struct ltface_ctx *ctx, const int style)
{
    const lightmap_t *lightmap = ctx->lightsurf->lightmaps;
    
    for (int i = 0; i < MAXLIGHTMAPS; i++, lightmap++) {
        if (lightmap->style == style)
            return lightmap;
        if (lightmap->style == 255)
            break;
    }
    return NULL;
}
#endif

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
DirtAtPoint(const globalconfig_t &cfg, raystream_t *rs, const vec3_t point, const vec3_t normal, const dmodel_t *selfshadow)
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
        
        rs->pushRay(j, point, dir, cfg.dirtDepth.floatValue(), selfshadow);
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
    
    const dmodel_t *selfshadow = lightsurf->modelinfo->shadowself.boolValue() ? lightsurf->modelinfo->model : NULL;
    
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
            vec3_t dirtvec;
            GetDirtVector(cfg, j, dirtvec);
            
            vec3_t dir;
            TransformToTangentSpace(lightsurf->normals[i], myUps[i], myRts[i], dirtvec, dir);
            
            rs->pushRay(i, lightsurf->points[i], dir, cfg.dirtDepth.floatValue(), selfshadow);
        }
        
        Q_assert(rs->numPushedRays() == lightsurf->numpoints);
        
        // trace the batch
        rs->tracePushedRaysIntersection();
        
        // accumulate hitdists
        for (int i = 0; i < lightsurf->numpoints; i++) {
            if (rs->getPushedRayHitType(i) == hittype_t::SOLID) {
                float dist = rs->getPushedRayHitDist(i);
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

// applies gamma and rangescale. clamps values over 255
// N.B. we want to do this before smoothing / downscaling, so huge values don't mess up the averaging.
static void
LightFace_ScaleAndClamp(const lightsurf_t *lightsurf, lightmapdict_t *lightmaps)
{
    const globalconfig_t &cfg = *lightsurf->cfg;
    
    for (lightmap_t &lightmap : *lightmaps) {
        for (int i = 0; i < lightsurf->numpoints; i++) {
            vec_t *color = lightmap.samples[i].color;
            
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
    
    int width = (lightsurf->texsize[0] + 1) * oversample;
    for (int mapnum = 0; mapnum < numstyles; mapnum++) {
        for (int t = 0; t <= lightsurf->texsize[1]; t++) {
            for (int s = 0; s <= lightsurf->texsize[0]; s++) {

                /* Take the average of any oversampling */
                vec3_t color, direction;

                VectorCopy(vec3_origin, color);
                VectorCopy(vec3_origin, direction);
                for (int i = 0; i < oversample; i++) {
                    for (int j = 0; j < oversample; j++) {
                        const int col = (s*oversample) + j;
                        const int row = (t*oversample) + i;

                        const lightsample_t *sample = sorted.at(mapnum)->samples + (row * width) + col;

                        VectorAdd(color, sample->color, color);
                        VectorAdd(direction, sample->direction, direction);
                    }
                }
                VectorScale(color, 1.0 / oversample / oversample, color);
                
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
                    temp[0] = DotProduct(direction, lightsurf->snormal);
                    temp[1] = DotProduct(direction, lightsurf->tnormal);
                    temp[2] = DotProduct(direction, lightsurf->plane.normal);
            
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

void LightFaceInit(const bsp2_t *bsp, struct ltface_ctx *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    
    ctx->bsp = bsp;
}

void LightFaceShutdown(struct ltface_ctx *ctx)
{
    if (!ctx->lightsurf)
        return;
    
    for (auto &lm : ctx->lightsurf->lightmapsByStyle) {
        free(lm.samples);
    }
    
    free(ctx->lightsurf->points);
    free(ctx->lightsurf->normals);
    free(ctx->lightsurf->occlusion);
    free(ctx->lightsurf->occluded);
    
    delete ctx->lightsurf->stream;
    
    delete ctx->lightsurf;
}

/*
 * ============
 * LightFace
 * ============
 */
void
LightFace(bsp2_dface_t *face, facesup_t *facesup, const modelinfo_t *modelinfo, struct ltface_ctx *ctx)
{
    const bsp2_t *bsp = ctx->bsp;
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
    
    /* all good, this face is going to be lightmapped. */
    ctx->lightsurf = new lightsurf_t {};
    lightsurf_t *lightsurf = ctx->lightsurf;
    lightsurf->cfg = ctx->cfg;
    
    const globalconfig_t &cfg = *lightsurf->cfg;
    
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
            LightFace_Bounce(ctx->bsp, face, lightsurf, lightmaps);
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
        LightFace_Bounce(ctx->bsp, face, lightsurf, lightmaps);
    
    /* replace lightmaps with AO for debugging */
    if (debugmode == debugmode_dirt)
        LightFace_DirtDebug(lightsurf, lightmaps);

    if (debugmode == debugmode_phong)
        LightFace_PhongDebug(lightsurf, lightmaps);
    
    if (debugmode == debugmode_bouncelights)
        LightFace_BounceLightsDebug(lightsurf, lightmaps);
    
    /* Fix any negative values */
    for (lightmap_t &lightmap : *lightmaps) {
        for (int j = 0; j < lightsurf->numpoints; j++) {
            lightsample_t *sample = &lightmap.samples[j];
            for (int k = 0; k < 3; k++) {
                if (sample->color[k] < 0) {
                    sample->color[k] = 0;
                }
            }
        }
    }

    /* Apply gamma, rangescale, and clamp */
    LightFace_ScaleAndClamp(lightsurf, lightmaps);
    
    /* Perform post-processing if requested */
    if (softsamples > 0) {
        for (lightmap_t &lightmap : *lightmaps) {
            Lightmap_Soften(&lightmap, lightsurf);
        }
    }
    
    WriteLightmaps(bsp, face, facesup, lightsurf, lightmaps);
}
