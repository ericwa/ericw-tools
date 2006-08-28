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

static const float scalecos = 0.5;
static const vec3_t bsp_origin = { 0, 0, 0 };

/* ======================================================================== */

// Solve three simultaneous equations
// mtx is modified by the function...
#define ZERO_EPSILON (0.001)
static qboolean
LU_Decompose(vec3_t mtx[3], int r[3], int c[2])
{
    int i, j, k;		// loop variables
    vec_t max;
    int max_r, max_c;

    // Do gauss elimination
    for (i = 0; i < 3; ++i) {
	max = 0;
	max_r = max_c = i;
	for (j = i; j < 3; ++j) {
	    for (k = i; k < 3; ++k) {
		if (fabs(mtx[j][k]) > max) {
		    max = fabs(mtx[j][k]);
		    max_r = j;
		    max_c = k;
		}
	    }
	}

	// Check for parallel planes
	if (max < ZERO_EPSILON)
	    return false;

	// Swap rows/columns if necessary
	if (max_r != i) {
	    for (j = 0; j < 3; ++j) {
		max = mtx[i][j];
		mtx[i][j] = mtx[max_r][j];
		mtx[max_r][j] = max;
	    }
	    k = r[i];
	    r[i] = r[max_r];
	    r[max_r] = k;
	}
	if (max_c != i) {
	    for (j = 0; j < 3; ++j) {
		max = mtx[j][i];
		mtx[j][i] = mtx[j][max_c];
		mtx[j][max_c] = max;
	    }
	    k = c[i];
	    c[i] = c[max_c];
	    c[max_c] = k;
	}
	// Do pivot
	for (j = i + 1; j < 3; ++j) {
	    mtx[j][i] /= mtx[i][i];
	    for (k = i + 1; k < 3; ++k)
		mtx[j][k] -= mtx[j][i] * mtx[i][k];
	}
    }

    return true;
}

#if __GNUC__ == 4 && __GNUC_MINOR__ == 1
#define CONST_BUG
#else
#define CONST_BUG const
#endif
static void
solve3(CONST_BUG vec3_t mtx[3], const int r[3], const int c[3],
       const vec3_t rhs, vec3_t soln)
{
    vec3_t y;

    // forward-substitution
    y[0] = rhs[r[0]];
    y[1] = rhs[r[1]] - mtx[1][0] * y[0];
    y[2] = rhs[r[2]] - mtx[2][0] * y[0] - mtx[2][1] * y[1];

    // back-substitution
    soln[c[2]] = y[2] / mtx[2][2];
    soln[c[1]] = (y[1] - mtx[1][2] * soln[c[2]]) / mtx[1][1];
    soln[c[0]] = (y[0] - mtx[0][1] * soln[c[1]] - mtx[0][2] * soln[c[2]])
	/ mtx[0][0];
}

/* ======================================================================== */


/*
 * ============
 * CastRay
 * Returns the distance between the points, or -1 if blocked
 * =============
 */
static vec_t
CastRay(const vec3_t p1, const vec3_t p2)
{
    int i;
    vec_t t;
    qboolean trace;

    trace = TestLine(p1, p2);
    if (!trace)
	return -1;		/* ray was blocked */

    t = 0;
    for (i = 0; i < 3; i++)
	t += (p2[i] - p1[i]) * (p2[i] - p1[i]);

    if (t == 0)
	t = 1;			/* don't blow up... */
    return sqrt(t);
}

/*
 * ============================================================================
 * SAMPLE POINT DETERMINATION
 * void SetupBlock (dface_t *f) Returns with surfpt[] set
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

#define SINGLEMAP (18*18*4)

typedef struct {
    vec_t lightmaps[MAXLIGHTMAPS][SINGLEMAP];
    int numlightstyles;
    vec_t *light;
    vec_t facedist;
    vec3_t facenormal;

    int numsurfpt;
    vec3_t surfpt[SINGLEMAP];

    /* FIXME - Comment this properly */
    vec_t worldtotex[2][4];	// Copy of face->texinfo->vecs
    vec3_t LU[3];
    int row_p[3];
    int col_p[3];

    vec_t exactmid[2];

    int texmins[2], texsize[2];
    int lightstyles[256];
    int surfnum;
    dface_t *face;

    /* Colored lighting */
    vec3_t lightmapcolors[MAXLIGHTMAPS][SINGLEMAP];
} lightinfo_t;

/*
 * ================
 * CalcFaceVectors
 * Fills in texorg, worldtotex. and textoworld
 * ================
 */
static void
CalcFaceVectors(lightinfo_t * l)
{
    texinfo_t *tex;
    int i, j;

    /* convert from float to vec_t */
    tex = &texinfo[l->face->texinfo];
    for (i = 0; i < 2; i++)
	for (j = 0; j < 4; j++)
	    l->worldtotex[i][j] = tex->vecs[i][j];

    /* Prepare LU and row, column permutations */
    for (i = 0; i < 3; ++i)
	l->row_p[i] = l->col_p[i] = i;
    VectorCopy(l->worldtotex[0], l->LU[0]);
    VectorCopy(l->worldtotex[1], l->LU[1]);
    VectorCopy(l->facenormal, l->LU[2]);

    /* Decompose the matrix. If we can't, texture axes are invalid. */
    if (!LU_Decompose(l->LU, l->row_p, l->col_p)) {
	const vec_t *p = dvertexes[dedges[l->face->firstedge].v[0]].point;

	Error("Bad texture axes on face:\n"
	      "   face point at (%5.3f, %5.3f, %5.3f)\n", p[0], p[1], p[2]);
    }
}

static void
tex_to_world(vec_t s, vec_t t, const lightinfo_t * l, vec3_t world)
{
    vec3_t rhs;

    rhs[0] = s - l->worldtotex[0][3];
    rhs[1] = t - l->worldtotex[1][3];
    rhs[2] = l->facedist + 1;	// one "unit" in front of surface

    solve3(l->LU, l->row_p, l->col_p, rhs, world);
}

/*
 * Functions to aid in calculation of polygon centroid
 */
static void
tri_centroid(const dvertex_t *v0, const dvertex_t *v1, const dvertex_t *v2,
	    vec3_t out)
{
    int i;

    for (i = 0; i < 3; i++)
	out[i] = (v0->point[i] + v1->point[i] + v2->point[i]) / 3.0;
}

static vec_t
tri_area(const dvertex_t *v0, const dvertex_t *v1, const dvertex_t *v2)
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

static void
face_centroid(const dface_t *f, vec3_t out)
{
    int i, e;
    dvertex_t *v0, *v1, *v2;
    vec3_t centroid, poly_centroid;
    vec_t area, poly_area;

    VectorCopy(vec3_origin, poly_centroid);
    poly_area = 0;

    e = dsurfedges[f->firstedge];
    if (e >= 0)
	v0 = dvertexes + dedges[e].v[0];
    else
	v0 = dvertexes + dedges[-e].v[1];

    for (i = 1; i < f->numedges - 1; i++) {
	e = dsurfedges[f->firstedge + i];
	if (e >= 0) {
	    v1 = dvertexes + dedges[e].v[0];
	    v2 = dvertexes + dedges[e].v[1];
	} else {
	    v1 = dvertexes + dedges[-e].v[1];
	    v2 = dvertexes + dedges[-e].v[0];
	}

	area = tri_area(v0, v1, v2);
	poly_area += area;

	tri_centroid(v0, v1, v2, centroid);
	VectorMA(poly_centroid, area, centroid, poly_centroid);
    }

    VectorScale(poly_centroid, 1.0 / poly_area, out);
}

/*
 * ================
 * CalcFaceExtents
 * Fills in s->texmins[], s->texsize[] and sets exactmid[]
 * ================
 */
static void
CalcFaceExtents(lightinfo_t * l, const vec3_t faceoffset)
{
    dface_t *s;
    vec_t mins[2], maxs[2], val;
    vec3_t centroid;
    int i, j, e;
    dvertex_t *v;
    texinfo_t *tex;

    s = l->face;

    mins[0] = mins[1] = 999999;
    maxs[0] = maxs[1] = -99999;
    tex = &texinfo[s->texinfo];

    for (i = 0; i < s->numedges; i++) {
	e = dsurfedges[s->firstedge + i];
	if (e >= 0)
	    v = dvertexes + dedges[e].v[0];
	else
	    v = dvertexes + dedges[-e].v[1];

	for (j = 0; j < 2; j++) {
	    // This is world->tex with world offset...
	    val = (v->point[0] + faceoffset[0]) * tex->vecs[j][0]
		+ (v->point[1] + faceoffset[1]) * tex->vecs[j][1]
		+ (v->point[2] + faceoffset[2]) * tex->vecs[j][2]
		+ tex->vecs[j][3];
	    if (val < mins[j])
		mins[j] = val;
	    if (val > maxs[j])
		maxs[j] = val;
	}
    }

    face_centroid(s, centroid);

    for (i = 0; i < 2; i++) {
	l->exactmid[i] =
	      (centroid[0] + faceoffset[0]) * tex->vecs[i][0]
	    + (centroid[1] + faceoffset[1]) * tex->vecs[i][1]
	    + (centroid[2] + faceoffset[2]) * tex->vecs[i][2]
	    + tex->vecs[i][3];


	mins[i] = floor(mins[i] / 16);
	maxs[i] = ceil(maxs[i] / 16);

	l->texmins[i] = mins[i];
	l->texsize[i] = maxs[i] - mins[i];
	if (l->texsize[i] > 17)
	    Error("Bad surface extents");
    }
}

/*
 * =================
 * CalcPoints
 * For each texture aligned grid point, back project onto the plane
 * to get the world xyz value of the sample point
 * =================
 */
static int c_bad;		// FIXME - what is this??

static void
CalcPoints(lightinfo_t * l)
{
    int i;
    int s, t;
    int w, h, step;
    vec_t starts, startt, us, ut;
    vec_t *surf;
    vec_t mids, midt;
    vec3_t facemid, move;

    /* fill in surforg                                         */
    /* the points are biased towards the center of the surface */
    /* to help avoid edge cases just inside walls              */

    surf = l->surfpt[0];
    mids = l->exactmid[0];
    midt = l->exactmid[1];

    tex_to_world(mids, midt, l, facemid);

    if (extrasamples) {		/* extra filtering */
	h = (l->texsize[1] + 1) * 2;
	w = (l->texsize[0] + 1) * 2;
	starts = (l->texmins[0] - 0.5) * 16;
	startt = (l->texmins[1] - 0.5) * 16;
	step = 8;
    } else {
	h = l->texsize[1] + 1;
	w = l->texsize[0] + 1;
	starts = l->texmins[0] * 16;
	startt = l->texmins[1] * 16;
	step = 16;
    }

    l->numsurfpt = w * h;
    for (t = 0; t < h; t++) {
	for (s = 0; s < w; s++, surf += 3) {
	    us = starts + s * step;
	    ut = startt + t * step;

	    /* if a line can be traced from surf to facemid, point is good */
	    for (i = 0; i < 6; i++) {
		tex_to_world(us, ut, l, surf);

		if (CastRay(facemid, surf) != -1)
		    break;	/* got it */
		if (i & 1) {	// i is odd
		    if (us > mids) {
			us -= 8;
			if (us < mids)
			    us = mids;
		    } else {
			us += 8;
			if (us > mids)
			    us = mids;
		    }
		} else {
		    if (ut > midt) {
			ut -= 8;
			if (ut < midt)
			    ut = midt;
		    } else {
			ut += 8;
			if (ut > midt)
			    ut = midt;
		    }
		}

		/* move surf 8 pixels towards the center */
		VectorSubtract(facemid, surf, move);
		VectorNormalize(move);
		VectorMA(surf, 8, move, surf);
	    }
	    if (i == 2)
		c_bad++;	// FIXME - what?
	}
    }
}


/*
 * ============================================================================
 * FACE LIGHTING
 * ============================================================================
 */

static int c_culldistplane;
static int c_proper;

/*
 * ==============================================
 * LIGHT: Attenuation formulae setup functions
 * ==============================================
 */
static vec_t
scaledDistance(vec_t distance, const entity_t *light)
{
    switch (light->formula) {
    case 1:
    case 2:
    case 3:
	/* Return a small distance to prevent culling these lights, since we */
	/* know these formulae won't fade to nothing.                        */
	return (distance <= 0.0) ? -0.25 : 0.25;
    case 0:
	return scaledist * light->atten * distance;
    default:
	return 1;		/* shut up compiler warnings */
    }
}

static vec_t
scaledLight(vec_t distance, const entity_t *light)
{
    const vec_t tmp = scaledist * light->atten * distance;

    switch (light->formula) {
    case 3:
	return light->light;
    case 1:
	return light->light / (tmp / 128);
    case 2:
	return light->light / ((tmp * tmp) / 16384);
    case 0:
	if (light->light > 0)
	    return (light->light - tmp > 0) ? light->light - tmp : 0;
	else
	    return (light->light + tmp < 0) ? light->light + tmp : 0;
    default:
	return 1;		/* shut up compiler warnings */
    }
}


/*
 * ================
 * SingleLightFace
 * ================
 */
static void
SingleLightFace(const entity_t *light, lightinfo_t * l,
		const vec3_t faceoffset)
{
    vec_t dist;
    vec3_t incoming;
    vec_t angle;
    vec_t add;
    vec_t *surf;
    qboolean hit;
    int mapnum;
    int size;
    int c, i;
    vec3_t rel;
    vec3_t spotvec;
    vec_t falloff;
    vec_t *lightsamp;

    /* Colored lighting */
    vec3_t *lightcolorsamp;

    VectorSubtract(light->origin, bsp_origin, rel);
    dist = scaledDistance((DotProduct(rel, l->facenormal) - l->facedist),
			  light);

    /* don't bother with lights behind the surface */
    if (dist < 0)
	return;

    /* don't bother with light too far away */
    if (dist > abs(light->light)) {
	c_culldistplane++;
	return;
    }

    if (light->targetent) {
	VectorSubtract(light->targetent->origin, light->origin, spotvec);
	VectorNormalize(spotvec);
	if (!light->angle)
	    falloff = -cos(20 * Q_PI / 180);
	else
	    falloff = -cos(light->angle / 2 * Q_PI / 180);
    } else if (light->use_mangle) {
	VectorCopy(light->mangle, spotvec);
	if (!light->angle)
	    falloff = -cos(20 * Q_PI / 180);
	else
	    falloff = -cos(light->angle / 2 * Q_PI / 180);
    } else {
	falloff = 0;		/* shut up compiler warnings */
    }

    mapnum = 0;
    for (mapnum = 0; mapnum < l->numlightstyles; mapnum++)
	if (l->lightstyles[mapnum] == light->style)
	    break;

    lightsamp = l->lightmaps[mapnum];
    lightcolorsamp = l->lightmapcolors[mapnum];

    /* TYR: The original way of doing things will generate "Too many light
     *      styles on a face" errors that aren't necessarily true.
     *      This is especially likely to happen if you make styled
     *      lights with delay != 0 as they are seen to be within
     *      lighting distance, even though they may be on totally
     *      opposite sides of the map (or separated by solid wall(s))
     *
     *      We should only generate the error when if find out that the
     *      face actually gets hit by the light
     *
     *      I think maybe MAXLIGHTMAPS is actually supposed to be 3.
     *      Have to make some more test maps (or check the engine source)
     *      for this.
     */

    if (mapnum == l->numlightstyles) {	/* init a new light map */
	size = (l->texsize[1] + 1) * (l->texsize[0] + 1);
	for (i = 0; i < size; i++) {
	    if (colored) {
		lightcolorsamp[i][0] = 0;
		lightcolorsamp[i][1] = 0;
		lightcolorsamp[i][2] = 0;
	    }
	    lightsamp[i] = 0;
	}
    }

    /* check it for real */

    hit = false;
    c_proper++;

    surf = l->surfpt[0];
    for (c = 0; c < l->numsurfpt; c++, surf += 3) {
	dist = scaledDistance(CastRay(light->origin, surf), light);

	if (dist < 0)
	    continue;		/* light doesn't reach */

	VectorSubtract(light->origin, surf, incoming);
	VectorNormalize(incoming);
	angle = DotProduct(incoming, l->facenormal);
	if (light->targetent || light->use_mangle) {	/* spotlight cutoff */
	    if (DotProduct(spotvec, incoming) > falloff)
		continue;
	}

	angle = (1.0 - scalecos) + scalecos * angle;
	add = scaledLight(CastRay(light->origin, surf), light);
	add *= angle;
	lightsamp[c] += add;

	if (colored) {
	    add /= (vec_t)255.0;
	    lightcolorsamp[c][0] += add * light->lightcolor[0];
	    lightcolorsamp[c][1] += add * light->lightcolor[1];
	    lightcolorsamp[c][2] += add * light->lightcolor[2];
	}

	if (abs(lightsamp[c]) > 1)	/* ignore really tiny lights */
	    hit = true;
    }

    if (mapnum == l->numlightstyles && hit) {
	if (mapnum == MAXLIGHTMAPS - 1) {
	    logprint("WARNING: Too many light styles on a face\n");
	    logprint("   lightmap point near (%0.0f, %0.0f, %0.0f)\n",
		     l->surfpt[0][0], l->surfpt[0][1], l->surfpt[0][2]);
	    logprint("   light->origin (%0.0f, %0.0f, %0.0f)\n",
		     light->origin[0], light->origin[1], light->origin[2]);
	    return;
	}

	l->lightstyles[mapnum] = light->style;
	l->numlightstyles++;	/* the style has some real data now */
    }
}

/*
 * =============
 * SkyLightFace
 * =============
 */
static void
SkyLightFace(lightinfo_t * l, const vec3_t faceoffset)
{
    int i, j;
    vec_t *surf;
    vec3_t incoming;
    vec_t angle;

    /* Don't bother if surface facing away from sun */
    //if (DotProduct (sunmangle, l->facenormal) <= 0)
    if (DotProduct(sunmangle, l->facenormal) < -ANGLE_EPSILON)
	return;

    /* if sunlight is set, use a style 0 light map */
    for (i = 0; i < l->numlightstyles; i++)
	if (l->lightstyles[i] == 0)
	    break;
    if (i == l->numlightstyles) {
	if (l->numlightstyles == MAXLIGHTMAPS)
	    return;		/* oh well, too many lightmaps... */
	l->lightstyles[i] = 0;
	l->numlightstyles++;
    }

    /* Check each point... */
    VectorCopy(sunmangle, incoming);
    VectorNormalize(incoming);
    angle = DotProduct(incoming, l->facenormal);
    angle = (1.0 - scalecos) + scalecos * angle;

#if 0
    /* Experimental - lighting of faces parallel to sunlight*/
    {
	int a, b, c, k;
	vec_t oldangle, offset;
	vec3_t sun_vectors[5];

	// Try to hit parallel surfaces?
	oldangle = DotProduct(incoming, l->facenormal);
	if (oldangle < ANGLE_EPSILON) {
	    printf("real small angle! (%f)\n", oldangle);
	    angle = (1.0 - scalecos) + scalecos * ANGLE_EPSILON;
	}

	a = fabs(sunmangle[0]) > fabs(sunmangle[1]) ?
	    (fabs(sunmangle[0]) > fabs(sunmangle[2]) ? 0 : 2) :
	    (fabs(sunmangle[1]) > fabs(sunmangle[2]) ? 1 : 2);
	b = (a + 1) % 3;
	c = (a + 2) % 3;

	offset = sunmangle[a] * ANGLE_EPSILON * 2.0;	// approx...
	for (j = 0; j < 5; ++j)
	    VectorCopy(sunmangle, sun_vectors[j]);
	sun_vectors[1][b] += offset;
	sun_vectors[2][b] -= offset;
	sun_vectors[3][c] += offset;
	sun_vectors[4][c] -= offset;

	surf = l->surfpt[0];
	for (j = 0; j < l->numsurfpt; j++, surf += 3) {
	    for (k = 0; k < 1 || (oldangle < ANGLE_EPSILON && k < 5); ++k) {
		if (TestSky(surf, sun_vectors[k])) {
		    l->lightmaps[i][j] += (angle * sunlight);
		    if (colored)
			VectorMA(l->lightmapcolors[i][j], angle * sunlight /
				 (vec_t)255, sunlight_color,
				 l->lightmapcolors[i][j]);
		    break;
		}
	    }
	}
    }
#else
    surf = l->surfpt[0];
    for (j = 0; j < l->numsurfpt; j++, surf += 3) {
	if (TestSky(surf, sunmangle)) {
	    l->lightmaps[i][j] += (angle * sunlight);
	    if (colored)
		VectorMA(l->lightmapcolors[i][j], angle * sunlight /
			 (vec_t)255, sunlight_color, l->lightmapcolors[i][j]);
	}
    }
#endif
}

/*
 * ============
 * FixMinlight
 * ============
 */
static void
FixMinlight(lightinfo_t * l)
{
    int i, j, k;
    vec_t tmp;

    /* if minlight is set, there must be a style 0 light map */
    for (i = 0; i < l->numlightstyles; i++) {
	if (l->lightstyles[i] == 0) {
	    // Set the minimum light level...
	    break;
	} else {
	    // Set zero minimum...
	}
    }
    if (i == l->numlightstyles) {
	if (l->numlightstyles == MAXLIGHTMAPS)
	    return;		/* oh well.. */
	for (j = 0; j < l->numsurfpt; j++)
	    l->lightmaps[i][j] = worldminlight;
	if (colored)
	    for (j = 0; j < l->numsurfpt; j++)
		VectorScale(minlight_color, worldminlight / (vec_t)255,
			    l->lightmapcolors[i][j]);
	l->lightstyles[i] = 0;
	l->numlightstyles++;
    } else {
	for (j = 0; j < l->numsurfpt; j++) {
	    if (l->lightmaps[i][j] < worldminlight)
		l->lightmaps[i][j] = worldminlight;
	    if (colored) {
		for (k = 0; k < 3; k++) {
		    tmp = (vec_t)worldminlight *minlight_color[k];

		    tmp /= (vec_t)255.0;
		    if (l->lightmapcolors[i][j][k] < tmp)
			l->lightmapcolors[i][j][k] = tmp;
		}
	    }
	}
    }
}


/*
 * light is the light intensity, needed to check if +ve or -ve.
 * src and dest are the source and destination color vectors (vec3_t).
 * dest becomes a copy of src where
 *    MakePosColoredLight zeros negative light components.
 *    MakeNegColoredLight zeros positive light components.
 */
static void
MakePosColoredLight(int light, vec3_t dest, const vec3_t src)
{
    int i;

    if (light >= 0) {
	for (i = 0; i < 3; i++)
	    if (src[i] < 0)
		dest[i] = 0;
	    else
		dest[i] = src[i];
    } else {
	for (i = 0; i < 3; i++)
	    if (src[i] > 0)
		dest[i] = 0;
	    else
		dest[i] = src[i];
    }
}

static void
MakeNegColoredLight(int light, vec3_t dest, const vec3_t src)
{
    int i;

    if (light >= 0) {
	for (i = 0; i < 3; i++)
	    if (src[i] > 0)
		dest[i] = 0;
	    else
		dest[i] = src[i];
    } else {
	for (i = 0; i < 3; i++)
	    if (src[i] < 0)
		dest[i] = 0;
	    else
		dest[i] = src[i];
    }
}

/*
 * ============
 * LightFace
 * ============
 */
void
LightFace(int surfnum, qboolean nolight, const vec3_t faceoffset)
{
    dface_t *f;
    lightinfo_t l;
    int s, t;
    int i, j, k, c;

    vec_t max;
    int x1, x2, x3, x4;

    vec_t total;
    int size;
    int lightmapwidth;
    int lightmapsize;
    byte *out;
    byte *lit_out;
    vec_t *light;

    vec3_t *lightcolor;
    vec3_t totalcolors;

    int w, h;
    vec3_t point;


    f = dfaces + surfnum;

    /* some surfaces don't need lightmaps */
    f->lightofs = -1;
    for (j = 0; j < MAXLIGHTMAPS; j++)
	f->styles[j] = 255;

    if (texinfo[f->texinfo].flags & TEX_SPECIAL)
	return;			/* non-lit texture */

    memset(&l, 0, sizeof(l));
    l.surfnum = surfnum;
    l.face = f;

    /* rotate plane */

    VectorCopy(dplanes[f->planenum].normal, l.facenormal);
    l.facedist = dplanes[f->planenum].dist;
    VectorScale(l.facenormal, l.facedist, point);
    VectorAdd(point, faceoffset, point);
    l.facedist = DotProduct(point, l.facenormal);

    if (f->side) {
	VectorSubtract(vec3_origin, l.facenormal, l.facenormal);
	l.facedist = -l.facedist;
    }

    CalcFaceVectors(&l);
    CalcFaceExtents(&l, faceoffset);
    CalcPoints(&l);

    lightmapwidth = l.texsize[0] + 1;

    size = lightmapwidth * (l.texsize[1] + 1);
    if (size > SINGLEMAP)
	Error("Bad lightmap size");

    for (i = 0; i < MAXLIGHTMAPS; i++)
	l.lightstyles[i] = 255;

    /* Under normal circumstances, the lighting procedure is:
     * - cast all light entities
     * - cast sky lighting
     * - do minlighting.
     *
     * However, if nominlimit is enabled then we need to do the following:
     * - cast _positive_ lights
     * - cast _positive_ skylight (if any)
     * - do minlighting
     * - cast _negative_ lights
     * - cast _negative_ sky light (if any)
     */

    l.numlightstyles = 0;
    if (nominlimit) {
	/* cast only positive lights */
	entity_t lt;

	for (i = 0; i < num_entities; i++) {
	    if (colored) {
		if (entities[i].light) {
		    memcpy(&lt, &entities[i], sizeof(entity_t));
		    MakePosColoredLight(entities[i].light, lt.lightcolor,
					entities[i].lightcolor);
		    SingleLightFace(&lt, &l, faceoffset);
		}
	    } else if (entities[i].light > 0) {
		SingleLightFace(&entities[i], &l, faceoffset);
	    }
	}
	/* cast positive sky light */
	if (sunlight) {
	    if (colored) {
		vec3_t suncol_save;

		VectorCopy(sunlight_color, suncol_save);
		MakePosColoredLight(sunlight, sunlight_color, suncol_save);
		SkyLightFace(&l, faceoffset);
		VectorCopy(suncol_save, sunlight_color);
	    } else if (sunlight > 0) {
		SkyLightFace(&l, faceoffset);
	    }
	}
    } else {
	/* (!nominlimit) => cast all lights */
	for (i = 0; i < num_entities; i++)
	    if (entities[i].light)
		SingleLightFace(&entities[i], &l, faceoffset);

	/* cast sky light */
	if (sunlight)
	    SkyLightFace(&l, faceoffset);
    }

    /* Minimum lighting */
    FixMinlight(&l);

    if (nominlimit) {
	/* cast only negative lights */
	entity_t lt;

	for (i = 0; i < num_entities; i++) {
	    if (colored) {
		if (entities[i].light) {
		    memcpy(&lt, &entities[i], sizeof(entity_t));
		    MakeNegColoredLight(entities[i].light, lt.lightcolor,
					entities[i].lightcolor);
		    SingleLightFace(&lt, &l, faceoffset);
		}
	    } else if (entities[i].light < 0) {
		SingleLightFace(&entities[i], &l, faceoffset);
	    }
	}
	/* cast negative sky light */
	if (sunlight) {
	    if (colored) {
		vec3_t suncol_save;

		VectorCopy(sunlight_color, suncol_save);
		MakeNegColoredLight(sunlight, sunlight_color, suncol_save);
		SkyLightFace(&l, faceoffset);
		VectorCopy(suncol_save, sunlight_color);
	    } else if (sunlight < 0) {
		SkyLightFace(&l, faceoffset);
	    }
	}

	/* Fix any negative values */
	for (i = 0; i < l.numlightstyles; i++) {
	    for (j = 0; j < l.numsurfpt; j++) {
		if (l.lightmaps[i][j] < 0) {
		    l.lightmaps[i][j] = 0;
		}
		if (colored) {
		    for (k = 0; k < 3; k++) {
			if (l.lightmapcolors[i][j][k] < 0) {
			    l.lightmapcolors[i][j][k] = 0;
			}
		    }
		}
	    }
	}
    }

    if (!l.numlightstyles)
	return;			/* no light hitting it */

    /* save out the values */

    for (i = 0; i < MAXLIGHTMAPS; i++)
	f->styles[i] = l.lightstyles[i];

    /* Extra room for BSP30 lightmaps */
    if (colored && bsp30)
	lightmapsize = size * l.numlightstyles * 4;
    else
	lightmapsize = size * l.numlightstyles;

    out = GetFileSpace(lightmapsize);

    if (litfile)
	lit_out = GetLitFileSpace(lightmapsize * 3);
    else
	lit_out = NULL;		/* Fix compiler warning... */

    f->lightofs = out - filebase;

    /* extra filtering */
    h = (l.texsize[1] + 1) * 2;
    w = (l.texsize[0] + 1) * 2;

    for (i = 0; i < l.numlightstyles; i++) {
	if (l.lightstyles[i] == 0xff)
	    Error("Wrote empty lightmap");

	light = l.lightmaps[i];
	lightcolor = l.lightmapcolors[i];
	c = 0;

	for (t = 0; t <= l.texsize[1]; t++) {
	    for (s = 0; s <= l.texsize[0]; s++, c++) {
		if (extrasamples) {
		    x1 = t * 2 * w + s * 2;
		    x2 = x1 + 1;
		    x3 = (t * 2 + 1) * w + s * 2;
		    x4 = x3 + 1;

		    /* filtered sample */
		    total = light[x1] + light[x2] + light[x3] + light[x4];
		    total *= 0.25;

		    /* Calculate the color */
		    if (colored) {
			totalcolors[0] =
			    lightcolor[x1][0] + lightcolor[x2][0]
			    + lightcolor[x3][0] + lightcolor[x4][0];
			totalcolors[0] *= 0.25;
			totalcolors[1] =
			    lightcolor[x1][1] + lightcolor[x2][1]
			    + lightcolor[x3][1] + lightcolor[x4][1];
			totalcolors[1] *= 0.25;
			totalcolors[2] =
			    lightcolor[x1][2] + lightcolor[x2][2]
			    + lightcolor[x3][2] + lightcolor[x4][2];
			totalcolors[2] *= 0.25;
		    }
		} else {
		    total = light[c];
		    if (colored)
			VectorCopy(lightcolor[c], totalcolors);
		}
		total *= rangescale;	/* scale before clamping */

		if (colored) {
		    /* Scale back intensity, instead of capping individual
		     * colors
		     */
		    VectorScale(totalcolors, rangescale, totalcolors);
		    max = 0.0;
		    for (j = 0; j < 3; j++)
			if (totalcolors[j] > max) {
			    max = totalcolors[j];
			} else if (totalcolors[j] < 0.0f) {
			    Error("color %i < 0", j);
			}
		    if (max > 255.0f)
			VectorScale(totalcolors, 255.0f / max, totalcolors);
		}

		if (total > 255.0f)
		    total = 255.0f;
		else if (total < 0) {
		    //Error ("light < 0");
		    total = 0;
		}

		/* Write out the lightmap in the appropriate format */
		if (colored && bsp30) {
		    *out++ = totalcolors[0];
		    *out++ = totalcolors[1];
		    *out++ = totalcolors[2];
		}
		if (colored && litfile) {
		    *lit_out++ = totalcolors[0];
		    *lit_out++ = totalcolors[1];
		    *lit_out++ = totalcolors[2];
		}
		*out++ = total;
	    }
	}
    }
}
