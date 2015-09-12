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

#include <light/light.h>
#include <light/entities.h>

/* ======================================================================== */

typedef struct {
    vec3_t data[3];	/* permuted 3x3 matrix */
    int row[3];		/* row permutations */
    int col[3];		/* column permutations */
} pmatrix3_t;

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

typedef struct {
    pmatrix3_t transform;
    const texinfo_t *texinfo;
    vec_t planedist;
} texorg_t;

typedef struct {
    vec3_t normal;
    vec_t dist;
} plane_t;

/* Allow space for 4x4 oversampling */
#define SINGLEMAP (18*18*4*4)

typedef struct {
    const modelinfo_t *modelinfo;
    plane_t plane;
    vec3_t snormal;
    vec3_t tnormal;

    int texmins[2];
    int texsize[2];
    vec_t exactmid[2];

    int numpoints;
    vec3_t points[SINGLEMAP];

    /*
    raw ambient occlusion amount per sample point, 0-1, where 1 is 
    fully occluded. dirtgain/dirtscale are not applied yet
    */
    vec_t occlusion[SINGLEMAP];
} lightsurf_t;

typedef struct {
    int style;
    lightsample_t samples[SINGLEMAP];
} lightmap_t;

/*
 * Functions to aid in calculation of polygon centroid
 */
static void
TriCentroid(const dvertex_t *v0, const dvertex_t *v1, const dvertex_t *v2,
	    vec3_t out)
{
    int i;

    for (i = 0; i < 3; i++)
	out[i] = (v0->point[i] + v1->point[i] + v2->point[i]) / 3.0;
}

static vec_t
TriArea(const dvertex_t *v0, const dvertex_t *v1, const dvertex_t *v2)
{
    int i;
    vec3_t edge0, edge1, cross;

    for (i =0; i < 3; i++) {
	edge0[i] = v1->point[i] - v0->point[i];
	edge1[i] = v2->point[i] - v0->point[i];
    }
    CrossProduct(edge0, edge1, cross);

    return VectorLength(cross) * 0.5;
}

static vec_t
FaceArea(const bsp2_dface_t *face, const bsp2_t *bsp)
{
    int i, edgenum;
    dvertex_t *v0, *v1, *v2;
    vec_t poly_area = 0;

    edgenum = bsp->dsurfedges[face->firstedge];
    if (edgenum >= 0)
	v0 = bsp->dvertexes + bsp->dedges[edgenum].v[0];
    else
	v0 = bsp->dvertexes + bsp->dedges[-edgenum].v[1];

    for (i = 1; i < face->numedges - 1; i++) {
	edgenum = bsp->dsurfedges[face->firstedge + i];
	if (edgenum >= 0) {
	    v1 = bsp->dvertexes + bsp->dedges[edgenum].v[0];
	    v2 = bsp->dvertexes + bsp->dedges[edgenum].v[1];
	} else {
	    v1 = bsp->dvertexes + bsp->dedges[-edgenum].v[1];
	    v2 = bsp->dvertexes + bsp->dedges[-edgenum].v[0];
	}
	poly_area += TriArea(v0, v1, v2);
    }

    return poly_area;
}

static void
FaceCentroid(const bsp2_dface_t *face, const bsp2_t *bsp, vec3_t out)
{
    int i, edgenum;
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

    for (i = 1; i < face->numedges - 1; i++) {
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

static void
PrintFaceInfo(const bsp2_dface_t *face, const bsp2_t *bsp);

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
    const dplane_t *plane;
    const texinfo_t *tex;
    int i;

    /* Prepare the transform matrix and init row/column permutations */
    plane = &bsp->dplanes[face->planenum];
    tex = &bsp->texinfo[face->texinfo];
    for (i = 0; i < 3; i++) {
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
	Error("");
    }
}

static void
TexCoordToWorld(vec_t s, vec_t t, const texorg_t *texorg, vec3_t world)
{
    vec3_t rhs;

    rhs[0] = s - texorg->texinfo->vecs[0][3];
    rhs[1] = t - texorg->texinfo->vecs[1][3];
    rhs[2] = texorg->planedist + 1; /* one "unit" in front of surface */

    Solve3(&texorg->transform, rhs, world);
}

void
WorldToTexCoord(const vec3_t world, const texinfo_t *tex, vec_t coord[2])
{
    int i;

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
    for (i = 0; i < 2; i++)
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
    const int offset = bsp->dtexdata.header->dataofs[tex->miptex];
    const miptex_t *miptex = (const miptex_t *)(bsp->dtexdata.base + offset);
    int i;

    logprint("face %d, texture %s, %d edges...\n"
	     "  vectors (%3.3f, %3.3f, %3.3f) (%3.3f)\n"
	     "          (%3.3f, %3.3f, %3.3f) (%3.3f)\n",
	     (int)(face - bsp->dfaces), miptex->name, face->numedges,
	     tex->vecs[0][0], tex->vecs[0][1], tex->vecs[0][2], tex->vecs[0][3],
	     tex->vecs[1][0], tex->vecs[1][1], tex->vecs[1][2], tex->vecs[1][3]);

    for (i = 0; i < face->numedges; i++) {
	int edge = bsp->dsurfedges[face->firstedge + i];
	int vert = (edge >= 0) ? bsp->dedges[edge].v[0] : bsp->dedges[-edge].v[1];
	const float *point = bsp->dvertexes[vert].point;

	logprint("%s %3d (%3.3f, %3.3f, %3.3f) :: edge %d\n",
		 i ? "          " : "    verts ", vert,
		 point[0], point[1], point[2], edge);
    }
}

/*
 * ================
 * CalcFaceExtents
 * Fills in surf->texmins[], surf->texsize[] and sets surf->exactmid[]
 * ================
 */
__attribute__((noinline))
static void
CalcFaceExtents(const bsp2_dface_t *face,
		const bsp2_t *bsp, lightsurf_t *surf)
{
    vec_t mins[2], maxs[2], texcoord[2];
    vec3_t worldpoint;
    int i, j, edge, vert;
    const dvertex_t *dvertex;
    const texinfo_t *tex;

    mins[0] = mins[1] = VECT_MAX;
    maxs[0] = maxs[1] = -VECT_MAX;
    tex = &bsp->texinfo[face->texinfo];

    for (i = 0; i < face->numedges; i++) {
	edge = bsp->dsurfedges[face->firstedge + i];
	vert = (edge >= 0) ? bsp->dedges[edge].v[0] : bsp->dedges[-edge].v[1];
	dvertex = &bsp->dvertexes[vert];

	VectorCopy(dvertex->point, worldpoint);
	WorldToTexCoord(worldpoint, tex, texcoord);
	for (j = 0; j < 2; j++) {
	    if (texcoord[j] < mins[j])
		mins[j] = texcoord[j];
	    if (texcoord[j] > maxs[j])
		maxs[j] = texcoord[j];
	}
    }

    FaceCentroid(face, bsp, worldpoint);
    WorldToTexCoord(worldpoint, tex, surf->exactmid);

    for (i = 0; i < 2; i++) {
	mins[i] = floor(mins[i] / 16);
	maxs[i] = ceil(maxs[i] / 16);
	surf->texmins[i] = mins[i];
	surf->texsize[i] = maxs[i] - mins[i];
	if (surf->texsize[i] > 17) {
	    const dplane_t *plane = bsp->dplanes + face->planenum;
	    const int offset = bsp->dtexdata.header->dataofs[tex->miptex];
	    const miptex_t *miptex = (const miptex_t *)(bsp->dtexdata.base + offset);
	    Error("Bad surface extents:\n"
		  "   surface %d, %s extents = %d\n"
		  "   texture %s at (%s)\n"
		  "   surface normal (%s)\n",
		  (int)(face - bsp->dfaces), i ? "t" : "s", surf->texsize[i],
		  miptex->name, VecStr(worldpoint), VecStrf(plane->normal));
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

/*
 * =================
 * CalcPoints
 * For each texture aligned grid point, back project onto the plane
 * to get the world xyz value of the sample point
 * =================
 */
__attribute__((noinline))
static void
CalcPoints(const dmodel_t *model, const vec3_t offset, const texorg_t *texorg, lightsurf_t *surf)
{
    int i;
    int s, t;
    int width, height, step;
    vec_t starts, startt, us, ut;
    vec_t *point;
    vec3_t midpoint, move;

    /*
     * Fill in the surface points. The points are biased towards the center of
     * the surface to help avoid edge cases just inside walls
     */
    TexCoordToWorld(surf->exactmid[0], surf->exactmid[1], texorg, midpoint);
    VectorAdd(midpoint, offset, midpoint);

    width  = (surf->texsize[0] + 1) * oversample;
    height = (surf->texsize[1] + 1) * oversample;
    starts = (surf->texmins[0] - 0.5 + (0.5 / oversample)) * 16;
    startt = (surf->texmins[1] - 0.5 + (0.5 / oversample)) * 16;
    step = 16 / oversample;

    point = surf->points[0];
    surf->numpoints = width * height;
    for (t = 0; t < height; t++) {
	for (s = 0; s < width; s++, point += 3) {
	    us = starts + s * step;
	    ut = startt + t * step;

	    TexCoordToWorld(us, ut, texorg, point);
	    VectorAdd(point, offset, point);
	    for (i = 0; i < 6; i++) {
		const int flags = TRACE_HIT_SOLID;
		tracepoint_t hit;
		int result;
		vec_t dist;

		result = TraceLine(model, flags, midpoint, point, &hit);
		if (result == TRACE_HIT_NONE)
		    break;
		if (result != TRACE_HIT_SOLID) {
		    WarnBadMidpoint(midpoint);
		    break;
		}

		/* Move the point 1 unit above the obstructing surface */
		dist = DotProduct(point, hit.dplane->normal) - hit.dplane->dist;
		dist = hit.side ? -dist - 1 : -dist + 1;
		VectorScale(hit.dplane->normal, dist, move);
		VectorAdd(point, move, point);
	    }
	}
    }
}

__attribute__((noinline))
static void
Lightsurf_Init(const modelinfo_t *modelinfo, const bsp2_dface_t *face,
	       const bsp2_t *bsp, lightsurf_t *lightsurf)
{
    plane_t *plane;
    const texinfo_t *tex;
    vec3_t planepoint;
    texorg_t texorg;

    memset(lightsurf, 0, sizeof(*lightsurf));
    lightsurf->modelinfo = modelinfo;

    /* Set up the plane, not including model offset */
    plane = &lightsurf->plane;
    VectorCopy(bsp->dplanes[face->planenum].normal, plane->normal);
    plane->dist = bsp->dplanes[face->planenum].dist;
    if (face->side) {
	VectorSubtract(vec3_origin, plane->normal, plane->normal);
	plane->dist = -plane->dist;
    }

    /* Set up the texorg for coordinate transformation */
    CreateFaceTransform(face, bsp, &texorg.transform);
    texorg.texinfo = &bsp->texinfo[face->texinfo];
    texorg.planedist = plane->dist;

    tex = &bsp->texinfo[face->texinfo];
    VectorCopy(tex->vecs[0], lightsurf->snormal);
    VectorSubtract(vec3_origin, tex->vecs[1], lightsurf->tnormal);
    VectorNormalize(lightsurf->snormal);
    VectorNormalize(lightsurf->tnormal);

    /* Set up the surface points */
    CalcFaceExtents(face, bsp, lightsurf);
    CalcPoints(modelinfo->model, modelinfo->offset, &texorg, lightsurf);
    
    /* Correct the plane for the model offset (must be done last, 
       calculation of face extents / points needs the uncorrected plane) */
    VectorScale(plane->normal, plane->dist, planepoint);
    VectorAdd(planepoint, modelinfo->offset, planepoint);
    plane->dist = DotProduct(plane->normal, planepoint);
}

static void
Lightmaps_Init(lightmap_t *lightmaps, const int count)
{
    int i;

    /*these are cleared on demand, there's no point clearing them twice. most of these are unused anyway,
     memset(lightmaps, 0, sizeof(lightmap_t) * count); */

    for (i = 0; i < count; i++)
	lightmaps[i].style = 255;
}

/*
 * Lightmap_ForStyle
 *
 * If lightmap with given style has already been allocated, return it.
 * Otherwise, return the next available map.  A new map is not marked as
 * allocated since it may not be kept if no lights hit.
 */
static lightmap_t *
Lightmap_ForStyle(lightmap_t *lightmaps, const int style, const lightsurf_t *lightsurf)
{
    lightmap_t *lightmap = lightmaps;
    int i;

    for (i = 0; i < MAXLIGHTMAPS; i++, lightmap++) {
	if (lightmap->style == style)
	    return lightmap;
	if (lightmap->style == 255)
	    break;
    }
    
    /*clear only the data that is going to be merged to it. there's no point clearing more*/
    memset(lightmap->samples, 0, sizeof(lightsample_t) * lightsurf->numpoints);
    lightmap->style = 255;

    return lightmap;
}

/*
 * Lightmap_Save
 *
 * As long as we have space for the style, mark as allocated,
 * otherwise emit a warning.
 */
static void
Lightmap_Save(lightmap_t *lightmaps, const lightsurf_t *lightsurf,
	      lightmap_t *lightmap, const int style)
{
    if (lightmap - lightmaps < MAXLIGHTMAPS) {
	if (lightmap->style == 255)
	    lightmap->style = style;
	return;
    }

    logprint("WARNING: Too many light styles on a face\n"
	     "         lightmap point near (%s)\n",
	     VecStr(lightsurf->points[0]));
}

/*
 * Average adjacent points on the grid to soften shadow edges
 */
__attribute__((noinline))
static void
Lightmap_Soften(lightmap_t *lightmap, const lightsurf_t *lightsurf)
{
    int i, samples;
    int s, t, starts, startt, ends, endt;
    const lightsample_t *src;
    lightsample_t *dst;
    lightmap_t softmap;

    const int width = (lightsurf->texsize[0] + 1) * oversample;
    const int height = (lightsurf->texsize[1] + 1) * oversample;
    const int fullsamples = (2 * softsamples + 1) * (2 * softsamples + 1);

    memset(&softmap, 0, sizeof(softmap));
    dst = softmap.samples;
    for (i = 0; i < lightsurf->numpoints; i++, dst++) {
	startt = qmax((i / width) - softsamples, 0);
	endt = qmin((i / width) + softsamples + 1, height);
	starts = qmax((i % width) - softsamples, 0);
	ends = qmin((i % width) + softsamples + 1, width);

	for (t = startt; t < endt; t++) {
	    src = &lightmap->samples[t * width + starts];
	    for (s = starts; s < ends; s++) {
		dst->light += src->light;
		VectorAdd(dst->color, src->color, dst->color);
		VectorAdd(dst->direction, src->direction, dst->direction);
		src++;
	    }
	}
	/*
	 * For cases where we are softening near the edge of the lightmap,
	 * take extra samples from the centre point (follows old bjp tools
	 * behaviour)
	 */
	samples = (endt - startt) * (ends - starts);
	if (samples < fullsamples) {
	    const int extraweight = 2 * (fullsamples - samples);
	    src = &lightmap->samples[i];
	    dst->light += src->light * extraweight;
	    VectorMA(dst->color, extraweight, src->color, dst->color);
	    VectorMA(dst->direction, extraweight, src->direction, dst->direction);
	    samples += extraweight;
	}
	dst->light /= samples;
	VectorScale(dst->color, 1.0 / samples, dst->color);
	VectorScale(dst->direction, 1.0 / samples, dst->direction);
    }

    softmap.style = lightmap->style;
    memcpy(lightmap, &softmap, sizeof(softmap));
}



/*
 * ============================================================================
 * FACE LIGHTING
 * ============================================================================
 */

static vec_t
GetLightValue(const lightsample_t *light, const entity_t *entity, vec_t dist)
{
    vec_t value;

    if (entity->formula == LF_INFINITE || entity->formula == LF_LOCALMIN)
	return light->light;

    value = scaledist * entity->atten * dist;
    switch (entity->formula) {
    case LF_INVERSE:
	return light->light / (value / LF_SCALE);
    case LF_INVERSE2A:
	value += LF_SCALE;
	/* Fall through */
    case LF_INVERSE2:
	return light->light / ((value * value) / (LF_SCALE * LF_SCALE));
    case LF_LINEAR:
	if (light->light > 0)
	    return (light->light - value > 0) ? light->light - value : 0;
	else
	    return (light->light + value < 0) ? light->light + value : 0;
    default:
	Error("Internal error: unknown light formula");
    }
}

static inline void
Light_Add(lightsample_t *sample, const vec_t light, const vec3_t color, const vec3_t direction)
{
    sample->light += light;
    VectorMA(sample->color, light / 255.0f, color, sample->color);
    VectorMA(sample->direction, light, direction, sample->direction);
}

static inline void
Light_ClampMin(lightsample_t *sample, const vec_t light, const vec3_t color)
{
    int i;

    if (sample->light < light) {
	sample->light = light;
	for (i = 0; i < 3; i++)
	    if (sample->color[i] < color[i] * light / 255.0f)
		sample->color[i] = color[i] * light / 255.0f;
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
Dirt_GetScaleFactor(vec_t occlusion, const entity_t *entity, const modelinfo_t *modelinfo)
{
    vec_t light_dirtgain = dirtGain;
    vec_t light_dirtscale = dirtScale;
    vec_t outDirt;
    qboolean usedirt;

    /* is dirt processing disabled entirely? */
    if (!dirty)
	return 1.0f;
    if (modelinfo != NULL && modelinfo->nodirt)
        return 1.0f;

    /* should this light be affected by dirt? */
    if (entity) {
        if (entity->dirt == -1) {
	    usedirt = false;
        } else if (entity->dirt == 1) {
	    usedirt = true;
        } else {
	    usedirt = globalDirt;
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
	if (entity->dirtgain)
	    light_dirtgain = entity->dirtgain;
	if (entity->dirtscale)
	    light_dirtscale = entity->dirtscale;
    }

    /* early out */
    if ( occlusion <= 0.0f ) {
	return 1.0f;
    }

    /* apply gain (does this even do much? heh) */
    outDirt = pow( occlusion, light_dirtgain );
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
 * LightFace_Entity
 * ================
 */
static void
LightFace_Entity(const entity_t *entity, const lightsample_t *light,
		 const lightsurf_t *lightsurf, lightmap_t *lightmaps)
{
    const modelinfo_t *modelinfo = lightsurf->modelinfo;
    const plane_t *plane = &lightsurf->plane;
    const dmodel_t *shadowself;
    const vec_t *surfpoint;
    int i;
    qboolean hit;
    vec_t dist, add, angle, spotscale;
    lightsample_t *sample;
    lightmap_t *lightmap;

    dist = DotProduct(entity->origin, plane->normal) - plane->dist;

    /* don't bother with lights behind the surface */
    if (dist < 0)
	return;

    /* don't bother with light too far away */
    if (dist > entity->fadedist)
	return;

    /*
     * Check it for real
     */
    hit = false;
    lightmap = Lightmap_ForStyle(lightmaps, entity->style, lightsurf);
    shadowself = modelinfo->shadowself ? modelinfo->model : NULL;
    sample = lightmap->samples;
    surfpoint = lightsurf->points[0];
    for (i = 0; i < lightsurf->numpoints; i++, sample++, surfpoint += 3) {
	vec3_t ray;

	VectorSubtract(entity->origin, surfpoint, ray);
	dist = VectorLength(ray);

	/* Quick distance check first */
	if (dist > entity->fadedist)
	    continue;

	/* Check spotlight cone */
	VectorScale(ray, 1.0 / dist, ray);
	angle = DotProduct(ray, plane->normal);
	spotscale = 1;
	if (entity->spotlight) {
	    vec_t falloff = DotProduct(entity->spotvec, ray);
	    if (falloff > entity->spotfalloff)
		continue;
	    if (falloff > entity->spotfalloff2) {
		/* Interpolate between the two spotlight falloffs */
		spotscale = falloff - entity->spotfalloff2;
		spotscale /= entity->spotfalloff - entity->spotfalloff2;
		spotscale = 1.0 - spotscale;
	    }
	}

	if (!TestLight(entity->origin, surfpoint, shadowself))
	    continue;

	angle = (1.0 - entity->anglescale) + entity->anglescale * angle;
	add = GetLightValue(light, entity, dist) * angle * spotscale;
	add *= Dirt_GetScaleFactor(lightsurf->occlusion[i], entity, modelinfo);

	Light_Add(sample, add, light->color, ray);

	/* Check if we really hit, ignore tiny lights */
	/* ericw -- never ignore generated lights, which can be tiny and need
           the additive effect of lots hitting */
	if (!hit && (sample->light >= 1 || entity->generated))
	    hit = true;
    }

    if (hit)
	Lightmap_Save(lightmaps, lightsurf, lightmap, entity->style);
}

/*
 * =============
 * LightFace_Sky
 * =============
 */
static void
LightFace_Sky(const sun_t *sun, const lightsurf_t *lightsurf, lightmap_t *lightmaps)
{
    const modelinfo_t *modelinfo = lightsurf->modelinfo;
    const plane_t *plane = &lightsurf->plane;
    const dmodel_t *shadowself;
    const vec_t *surfpoint;
    int i;
    qboolean hit;
    vec3_t incoming;
    vec_t angle;
    lightsample_t *sample;
    lightmap_t *lightmap;

    /* Don't bother if surface facing away from sun */
    if (DotProduct(sun->sunvec, plane->normal) < -ANGLE_EPSILON)
	return;

    /* if sunlight is set, use a style 0 light map */
    lightmap = Lightmap_ForStyle(lightmaps, 0, lightsurf);

    VectorCopy(sun->sunvec, incoming);
    VectorNormalize(incoming);
    angle = DotProduct(incoming, plane->normal);
    angle = (1.0 - sun->anglescale) + sun->anglescale * angle;

    /* Check each point... */
    hit = false;
    shadowself = modelinfo->shadowself ? modelinfo->model : NULL;
    sample = lightmap->samples;
    surfpoint = lightsurf->points[0];
    for (i = 0; i < lightsurf->numpoints; i++, sample++, surfpoint += 3) {
    	vec_t value;
	if (!TestSky(surfpoint, sun->sunvec, shadowself))
	    continue;
	value = angle * sun->sunlight.light;
	if (sun->dirt)
	    value *= Dirt_GetScaleFactor(lightsurf->occlusion[i], NULL, modelinfo);
	Light_Add(sample, value, sun->sunlight.color, sun->sunvec);
	if (!hit/* && (sample->light >= 1)*/)
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
LightFace_Min(const lightsample_t *light,
	      const lightsurf_t *lightsurf, lightmap_t *lightmaps)
{
    const modelinfo_t *modelinfo = lightsurf->modelinfo;
    const dmodel_t *shadowself;
    entity_t **entity;
    const vec_t *surfpoint;
    qboolean hit, trace;
    int i, j;
    lightsample_t *sample;
    lightmap_t *lightmap;

    /* Find a style 0 lightmap */
    lightmap = Lightmap_ForStyle(lightmaps, 0, lightsurf);

    hit = false;
    sample = lightmap->samples;
    for (i = 0; i < lightsurf->numpoints; i++, sample++) {
    	vec_t value = light->light;
        if (minlightDirt)
	    value *= Dirt_GetScaleFactor(lightsurf->occlusion[i], NULL, modelinfo);
	if (addminlight)
	    Light_Add(sample, value, light->color, vec3_origin);
	else
	    Light_ClampMin(sample, value, light->color);
	if (!hit && sample->light >= 1)
	    hit = true;
    }

    /* Cast rays for local minlight entities */
    shadowself = modelinfo->shadowself ? modelinfo->model : NULL;
    for (entity = lights; *entity; entity++) {
        if ((*entity)->formula != LF_LOCALMIN)
            continue;

	sample = lightmap->samples;
	surfpoint = lightsurf->points[0];
	for (j = 0; j < lightsurf->numpoints; j++, sample++, surfpoint += 3) {
	    if (addminlight || sample->light < (*entity)->light.light) {
	    	vec_t value = (*entity)->light.light;
		trace = TestLight((*entity)->origin, surfpoint, shadowself);
		if (!trace)
		    continue;
		value *= Dirt_GetScaleFactor(lightsurf->occlusion[j], (*entity), modelinfo);
		if (addminlight)
		    Light_Add(sample, value, (*entity)->light.color, vec3_origin);
		else
		    Light_ClampMin(sample, value, (*entity)->light.color);
	    }
	    if (!hit && sample->light >= 1)
		hit = true;
	}
    }

    if (hit)
	Lightmap_Save(lightmaps, lightsurf, lightmap, 0);
}

/*
 * =============
 * LightFace_DirtDebug
 * =============
 */
static void
LightFace_DirtDebug(const lightsurf_t *lightsurf, lightmap_t *lightmaps)
{
    const modelinfo_t *modelinfo = lightsurf->modelinfo;
    int i;
    lightsample_t *sample;
    lightmap_t *lightmap;

    /* use a style 0 light map */
    lightmap = Lightmap_ForStyle(lightmaps, 0, lightsurf);

    /* Overwrite each point with the dirt value for that sample... */
    sample = lightmap->samples;
    for (i = 0; i < lightsurf->numpoints; i++, sample++) {
	sample->light = 255 * Dirt_GetScaleFactor(lightsurf->occlusion[i], NULL, modelinfo);
	VectorSet(sample->color, sample->light, sample->light, sample->light);
    }

    Lightmap_Save(lightmaps, lightsurf, lightmap, 0);
}

/* Dirtmapping borrowed from q3map2, originally by RaP7oR */

#define DIRT_NUM_ANGLE_STEPS        16
#define DIRT_NUM_ELEVATION_STEPS    3
#define DIRT_NUM_VECTORS            ( DIRT_NUM_ANGLE_STEPS * DIRT_NUM_ELEVATION_STEPS )

static vec3_t dirtVectors[ DIRT_NUM_VECTORS ];
static int numDirtVectors = 0;

/*
 * ============
 * SetupDirt
 *
 * sets up dirtmap (ambient occlusion)
 * ============
 */
void SetupDirt( void ) {
    int i, j;
    float angle, elevation, angleStep, elevationStep;

    /* note it */
    logprint("--- SetupDirt ---\n" );

    /* clamp dirtAngle */
    if ( dirtAngle <= 1.0f ) {
	dirtAngle = 1.0f;
    }
    if ( dirtAngle >= 90.0f) {
	dirtAngle = 90.0f;
    }
    
    /* calculate angular steps */
    angleStep = DEG2RAD( 360.0f / DIRT_NUM_ANGLE_STEPS );
    elevationStep = DEG2RAD( dirtAngle / DIRT_NUM_ELEVATION_STEPS );

    /* iterate angle */
    angle = 0.0f;
    for ( i = 0, angle = 0.0f; i < DIRT_NUM_ANGLE_STEPS; i++, angle += angleStep ) {
	/* iterate elevation */
	for ( j = 0, elevation = elevationStep * 0.5f; j < DIRT_NUM_ELEVATION_STEPS; j++, elevation += elevationStep ) {
	    dirtVectors[ numDirtVectors ][ 0 ] = sin( elevation ) * cos( angle );
	    dirtVectors[ numDirtVectors ][ 1 ] = sin( elevation ) * sin( angle );
	    dirtVectors[ numDirtVectors ][ 2 ] = cos( elevation );
	    numDirtVectors++;
	}
    }

    /* emit some statistics */
    logprint("%9d dirtmap vectors\n", numDirtVectors );
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
DirtTrace(const vec3_t start, const vec3_t stop, const dmodel_t *self, vec3_t hitpoint_out)
{
    const dmodel_t *const *model;
    const int traceflags = TRACE_HIT_SOLID | TRACE_HIT_SKY;
    int result = TRACE_HIT_NONE;
    tracepoint_t hitpoint;

    if (self) {
	result = TraceLine(self, traceflags, start, stop, &hitpoint);
	if (result == -TRACE_HIT_SOLID) {
	    /* We started in the void, which ideally wouldn't happen, 
	       but does (say on e1m1). Return the start point as the hitpoint,
	       which will make fully black dirt.
	     */
	    VectorCopy(start, hitpoint_out);
	    return true;
	} else if (result == TRACE_HIT_SOLID) {
	    VectorCopy(hitpoint.point, hitpoint_out);
	    return true;
	}
    }

    /* Check against the list of global shadow casters */
    for (model = tracelist; *model; model++) {
	if (*model == self)
	    continue;
	result = TraceLine(*model, traceflags, start, stop, &hitpoint);
	if (result == -TRACE_HIT_SOLID) {
	    VectorCopy(start, hitpoint_out);
	    return true;
	} else if (result == TRACE_HIT_SOLID) {
	    VectorCopy(hitpoint.point, hitpoint_out);
	    return true;
	}
    }

    return false;
}

/*
 * ============
 * DirtForSample
 * ============
 */
static vec_t
DirtForSample(const dmodel_t *model, const vec3_t origin, const vec3_t normal){
    int i;
    float gatherDirt, angle, elevation, ooDepth;
    vec3_t worldUp, myUp, myRt, temp, direction, displacement;
    vec3_t traceEnd, traceHitpoint;

    /* dummy check */
    if ( !dirty ) {
	return 1.0f;
    }
    
    /* setup */
    gatherDirt = 0.0f;
    ooDepth = 1.0f / dirtDepth;

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
	VectorSet( worldUp, 0.0f, 0.0f, 1.0f );
	CrossProduct( normal, worldUp, myRt );
	VectorNormalize( myRt );
	CrossProduct( myRt, normal, myUp );
	VectorNormalize( myUp );
    }

    /* 1 = random mode, 0 (well everything else) = non-random mode */
    if ( dirtMode == 1 ) {
	/* iterate */
	for ( i = 0; i < numDirtVectors; i++ ) {
	    /* get random vector */
	    angle = Random() * DEG2RAD( 360.0f );
	    elevation = Random() * DEG2RAD( dirtAngle );
	    temp[ 0 ] = cos( angle ) * sin( elevation );
	    temp[ 1 ] = sin( angle ) * sin( elevation );
	    temp[ 2 ] = cos( elevation );

	    /* transform into tangent space */
	    direction[ 0 ] = myRt[ 0 ] * temp[ 0 ] + myUp[ 0 ] * temp[ 1 ] + normal[ 0 ] * temp[ 2 ];
	    direction[ 1 ] = myRt[ 1 ] * temp[ 0 ] + myUp[ 1 ] * temp[ 1 ] + normal[ 1 ] * temp[ 2 ];
	    direction[ 2 ] = myRt[ 2 ] * temp[ 0 ] + myUp[ 2 ] * temp[ 1 ] + normal[ 2 ] * temp[ 2 ];

	    /* set endpoint */
	    VectorMA( origin, dirtDepth, direction, traceEnd );

	    /* trace */
	    if (DirtTrace(origin, traceEnd, model, traceHitpoint)) {
		VectorSubtract( traceHitpoint, origin, displacement );
		gatherDirt += 1.0f - ooDepth * VectorLength( displacement );
	    }
	}
    } else {
	/* iterate through ordered vectors */
	for ( i = 0; i < numDirtVectors; i++ ) {
	    /* transform vector into tangent space */
	    direction[ 0 ] = myRt[ 0 ] * dirtVectors[ i ][ 0 ] + myUp[ 0 ] * dirtVectors[ i ][ 1 ] + normal[ 0 ] * dirtVectors[ i ][ 2 ];
	    direction[ 1 ] = myRt[ 1 ] * dirtVectors[ i ][ 0 ] + myUp[ 1 ] * dirtVectors[ i ][ 1 ] + normal[ 1 ] * dirtVectors[ i ][ 2 ];
	    direction[ 2 ] = myRt[ 2 ] * dirtVectors[ i ][ 0 ] + myUp[ 2 ] * dirtVectors[ i ][ 1 ] + normal[ 2 ] * dirtVectors[ i ][ 2 ];

	    /* set endpoint */
	    VectorMA( origin, dirtDepth, direction, traceEnd );
	    
	    /* trace */
	    if (DirtTrace(origin, traceEnd, model, traceHitpoint)) {
		VectorSubtract( traceHitpoint, origin, displacement );
		gatherDirt += 1.0f - ooDepth * VectorLength( displacement );
	    }
	}
    }

    /* direct ray */
    VectorMA( origin, dirtDepth, normal, traceEnd );
    
    /* trace */
    if (DirtTrace(origin, traceEnd, model, traceHitpoint)) {
	VectorSubtract( traceHitpoint, origin, displacement );
	gatherDirt += 1.0f - ooDepth * VectorLength( displacement );
    }

    /* save gatherDirt, the rest of the scaling of the dirt value is done
       per-light */

    return gatherDirt / ( numDirtVectors + 1 );
}


/*
 * ============
 * LightFace_CalculateDirt
 * ============
 */
static void
LightFace_CalculateDirt(lightsurf_t *lightsurf)
{
    const modelinfo_t *modelinfo = lightsurf->modelinfo;
    const plane_t *plane = &lightsurf->plane;
    const vec_t *surfpoint;
    int i;

    /* Check each point... */
    surfpoint = lightsurf->points[0];
    for (i = 0; i < lightsurf->numpoints; i++, surfpoint += 3) {
	lightsurf->occlusion[i] = DirtForSample(modelinfo->model, surfpoint, plane->normal);
    }
}


static void
WriteLightmaps(bsp2_dface_t *face, const lightsurf_t *lightsurf,
	       const lightmap_t *lightmaps)
{
    int numstyles, size, mapnum, width, s, t, i, j;
    const lightsample_t *sample;
    vec_t light, maxcolor;
    vec3_t color, direction;
    byte *out, *lit, *lux;

    numstyles = 0;
    for (mapnum = 0; mapnum < MAXLIGHTMAPS; mapnum++) {
	face->styles[mapnum] = lightmaps[mapnum].style;
	if (lightmaps[mapnum].style != 255)
	    numstyles++;
    }
    if (!numstyles)
	return;

    size = (lightsurf->texsize[0] + 1) * (lightsurf->texsize[1] + 1);
    GetFileSpace(&out, &lit, &lux, size * numstyles);
    face->lightofs = out - filebase;

    width = (lightsurf->texsize[0] + 1) * oversample;
    for (mapnum = 0; mapnum < MAXLIGHTMAPS; mapnum++) {
	if (lightmaps[mapnum].style == 255)
	    break;

	for (t = 0; t <= lightsurf->texsize[1]; t++) {
	    for (s = 0; s <= lightsurf->texsize[0]; s++) {

		/* Take the average of any oversampling */
		VectorCopy(vec3_origin, color);
		VectorCopy(vec3_origin, direction);
		for (i = 0; i < oversample; i++) {
		    for (j = 0; j < oversample; j++) {
			const int col = (s*oversample) + j;
			const int row = (t*oversample) + i;

			sample = lightmaps[mapnum].samples + (row * width) + col;
			
			VectorAdd(color, sample->color, color);
			VectorAdd(direction, sample->direction, direction);
		    }
		}
		VectorScale(color, 1.0 / oversample / oversample, color);

		/* Scale and clamp any out-of-range samples */
		maxcolor = 0;
		VectorScale(color, rangescale, color);
		for (i = 0; i < 3; i++)
		    color[i] = pow( color[i] / 255.0f, 1.0 / lightmapgamma ) * 255.0f;
		for (i = 0; i < 3; i++)
		    if (color[i] > maxcolor)
			maxcolor = color[i];
		if (maxcolor > 255)
		    VectorScale(color, 255.0f / maxcolor, color);

		*lit++ = color[0];
		*lit++ = color[1];
		*lit++ = color[2];
		
		/* Average the color to get the value to write to the
		   .bsp lightmap. this avoids issues with some engines
		   that require the lit and internal lightmap to have the same
		   intensity. (MarkV, some QW engines)
		 */
		light = (color[0] + color[1] + color[2]) / 3.0;
		if (light < 0) light = 0;
		if (light > 255) light = 255;
		*out++ = light;

		if (lux)
		{
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

/*
 * ============
 * LightFace
 * ============
 */
void
LightFace(bsp2_dface_t *face, const modelinfo_t *modelinfo,
	  const bsp2_t *bsp)
{
    int i, j, k;
    entity_t **entity;
    lightsample_t *sample;
    lightsurf_t lightsurf;
    sun_t *sun;

    /* One extra lightmap is allocated to simplify handling overflow */
    lightmap_t lightmaps[MAXLIGHTMAPS + 1];

    /* some surfaces don't need lightmaps */
    face->lightofs = -1;
    for (i = 0; i < MAXLIGHTMAPS; i++)
	face->styles[i] = 255;
    if (bsp->texinfo[face->texinfo].flags & TEX_SPECIAL)
	return;

    Lightsurf_Init(modelinfo, face, bsp, &lightsurf);
    Lightmaps_Init(lightmaps, MAXLIGHTMAPS + 1);

    /* calculate dirt (ambient occlusion) but don't use it yet */
    if (dirty)
        LightFace_CalculateDirt(&lightsurf);

    /*
     * The lighting procedure is: cast all positive lights, fix
     * minlight levels, then cast all negative lights. Finally, we
     * clamp any values that may have gone negative.
     */

    /* positive lights */
    for (entity = lights; *entity; entity++) {
        if ((*entity)->formula == LF_LOCALMIN)
            continue;
        if ((*entity)->light.light > 0)
            LightFace_Entity(*entity, &(*entity)->light, &lightsurf, lightmaps);
    }
    for ( sun = suns; sun; sun = sun->next )
        if (sun->sunlight.light > 0)
            LightFace_Sky (sun, &lightsurf, lightmaps);

    /* minlight - Use the greater of global or model minlight. */
    if (modelinfo->minlight.light > minlight.light)
	LightFace_Min(&modelinfo->minlight, &lightsurf, lightmaps);
    else
	LightFace_Min(&minlight, &lightsurf, lightmaps);

    /* negative lights */
    for (entity = lights; *entity; entity++) {
        if ((*entity)->formula == LF_LOCALMIN)
            continue;
        if ((*entity)->light.light < 0)
            LightFace_Entity(*entity, &(*entity)->light, &lightsurf, lightmaps);
    }
    for ( sun = suns; sun; sun = sun->next )
        if (sun->sunlight.light < 0)
            LightFace_Sky (sun, &lightsurf, lightmaps);

    /* replace lightmaps with AO for debugging */
    if (dirtDebug)
    	LightFace_DirtDebug(&lightsurf, lightmaps);

    /* Fix any negative values */
    for (i = 0; i < MAXLIGHTMAPS; i++) {
	if (lightmaps[i].style == 255)
	    break;
	sample = lightmaps[i].samples;
	for (j = 0; j < lightsurf.numpoints; j++, sample++) {
	    if (sample->light < 0)
		sample->light = 0;
	    for (k = 0; k < 3; k++) {
		if (sample->color[k] < 0) {
		    sample->color[k] = 0;
		}
	    }
	}
    }

    /* Perform post-processing if requested */
    if (softsamples > 0) {
	for (i = 0; i < MAXLIGHTMAPS; i++) {
	    if (lightmaps[i].style == 255)
		break;
	    Lightmap_Soften(&lightmaps[i], &lightsurf);
	}
    }

    WriteLightmaps(face, &lightsurf, lightmaps);
}
