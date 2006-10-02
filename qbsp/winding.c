/*
    Copyright (C) 1996-1997  Id Software, Inc.
    Copyright (C) 1997       Greg Lewis

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
// winding.c

#include "qbsp.h"

/*
=================
BaseWindingForPlane
=================
*/
winding_t *
BaseWindingForPlane(plane_t *p)
{
    int i, x;
    vec_t max, v;
    vec3_t org, vright, vup;
    winding_t *w;

    // find the major axis
    max = -BOGUS_RANGE;
    x = -1;
    for (i = 0; i < 3; i++) {
	v = fabs(p->normal[i]);
	if (v > max) {
	    x = i;
	    max = v;
	}
    }

    VectorCopy(vec3_origin, vup);
    switch (x) {
    case 0:
    case 1:
	vup[2] = 1;
	break;
    case 2:
	vup[0] = 1;
	break;
    default:
	Message(msgError, errNoWindingAxis);
    }

    v = DotProduct(vup, p->normal);
    VectorMA(vup, -v, p->normal, vup);
    VectorNormalize(vup);

    VectorScale(p->normal, p->dist, org);

    CrossProduct(vup, p->normal, vright);

    VectorScale(vup, 8192, vup);
    VectorScale(vright, 8192, vright);

    // project a really big axis aligned box onto the plane
    w = AllocMem(WINDING, 4, true);

    VectorSubtract(org, vright, w->points[0]);
    VectorAdd(w->points[0], vup, w->points[0]);

    VectorAdd(org, vright, w->points[1]);
    VectorAdd(w->points[1], vup, w->points[1]);

    VectorAdd(org, vright, w->points[2]);
    VectorSubtract(w->points[2], vup, w->points[2]);

    VectorSubtract(org, vright, w->points[3]);
    VectorSubtract(w->points[3], vup, w->points[3]);

    w->numpoints = 4;

    return w;
}



/*
==================
CopyWinding
==================
*/
winding_t *
CopyWinding(winding_t *w)
{
    int size;
    winding_t *c;

    c = AllocMem(WINDING, w->numpoints, false);
    size = (int)((winding_t *)0)->points[w->numpoints];
    memcpy(c, w, size);

    return c;
}



/*
==================
CheckWinding

Check for possible errors
==================
*/
void
CheckWinding(winding_t *w)
{
}


void
CalcSides(const winding_t *in, const plane_t *split, int *sides, vec_t *dists,
	  int counts[3])
{
    int i;
    const vec_t *p;

    counts[0] = counts[1] = counts[2] = 0;
    p = in->points[0];
    for (i = 0; i < in->numpoints; i++, p += 3) {
	const vec_t dist = DotProduct(split->normal, p) - split->dist;
	dists[i] = dist;
	if (dist > ON_EPSILON)
	    sides[i] = SIDE_FRONT;
	else if (dist < -ON_EPSILON)
	    sides[i] = SIDE_BACK;
	else
	    sides[i] = SIDE_ON;
	counts[sides[i]]++;
    }
    sides[i] = sides[0];
    dists[i] = dists[0];
}

/*
==================
ClipWinding

Clips the winding to the plane, returning the new winding on the positive side
Frees the input winding.
If keepon is true, an exactly on-plane winding will be saved, otherwise
it will be clipped away.
==================
*/
winding_t *
ClipWinding(winding_t *in, plane_t *split, bool keepon)
{
    vec_t dists[MAX_POINTS_ON_WINDING];
    int sides[MAX_POINTS_ON_WINDING];
    int counts[3];
    vec_t dot;
    int i, j;
    vec_t *p1, *p2;
    vec3_t mid;
    winding_t *neww;
    int maxpts;

    CalcSides(in, split, sides, dists, counts);

    if (keepon && !counts[SIDE_FRONT] && !counts[SIDE_BACK])
	return in;

    if (!counts[SIDE_FRONT]) {
	FreeMem(in, WINDING, 1);
	return NULL;
    }

    if (!counts[SIDE_BACK])
	return in;

    maxpts = in->numpoints + 4;	// can't use counts[0]+2 because
    // of fp grouping errors
    neww = AllocMem(WINDING, maxpts, true);

    for (i = 0; i < in->numpoints; i++) {
	p1 = in->points[i];

	if (sides[i] == SIDE_ON) {
	    VectorCopy(p1, neww->points[neww->numpoints]);
	    neww->numpoints++;
	    continue;
	}

	if (sides[i] == SIDE_FRONT) {
	    VectorCopy(p1, neww->points[neww->numpoints]);
	    neww->numpoints++;
	}

	if (sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i])
	    continue;

	// generate a split point
	p2 = in->points[(i + 1) % in->numpoints];

	dot = dists[i] / (dists[i] - dists[i + 1]);
	for (j = 0; j < 3; j++) {
	    // avoid round off error when possible
	    if (split->normal[j] == 1)
		mid[j] = split->dist;
	    else if (split->normal[j] == -1)
		mid[j] = -split->dist;
	    else
		mid[j] = p1[j] + dot * (p2[j] - p1[j]);
	}

	VectorCopy(mid, neww->points[neww->numpoints]);
	neww->numpoints++;
    }

    if (neww->numpoints > maxpts)
	Message(msgError, errLowPointCount);

    // free the original winding
    FreeMem(in, WINDING, 1);

    return neww;
}


/*
==================
DivideWinding

Divides a winding by a plane, producing one or two windings.  The
original winding is not damaged or freed.  If only on one side, the
returned winding will be the input winding.  If on both sides, two
new windings will be created.
==================
*/
void
DivideWinding(winding_t *in, plane_t *split, winding_t **front,
	      winding_t **back)
{
    vec_t dists[MAX_POINTS_ON_WINDING];
    int sides[MAX_POINTS_ON_WINDING];
    int counts[3];
    vec_t dot;
    int i, j;
    vec_t *p1, *p2;
    vec3_t mid;
    winding_t *f, *b;
    int maxpts;

    counts[0] = counts[1] = counts[2] = 0;

    CalcSides(in, split, sides, dists, counts);

    *front = *back = NULL;

    if (!counts[0]) {
	*back = in;
	return;
    }
    if (!counts[1]) {
	*front = in;
	return;
    }

    maxpts = in->numpoints + 4;	// can't use counts[0]+2 because
    // of fp grouping errors

    *front = f = AllocMem(WINDING, maxpts, true);
    *back = b = AllocMem(WINDING, maxpts, true);

    for (i = 0; i < in->numpoints; i++) {
	p1 = in->points[i];

	if (sides[i] == SIDE_ON) {
	    VectorCopy(p1, f->points[f->numpoints]);
	    f->numpoints++;
	    VectorCopy(p1, b->points[b->numpoints]);
	    b->numpoints++;
	    continue;
	}

	if (sides[i] == SIDE_FRONT) {
	    VectorCopy(p1, f->points[f->numpoints]);
	    f->numpoints++;
	}
	if (sides[i] == SIDE_BACK) {
	    VectorCopy(p1, b->points[b->numpoints]);
	    b->numpoints++;
	}

	if (sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i])
	    continue;

	// generate a split point
	p2 = in->points[(i + 1) % in->numpoints];

	dot = dists[i] / (dists[i] - dists[i + 1]);
	for (j = 0; j < 3; j++) {	// avoid round off error when possible
	    if (split->normal[j] == 1)
		mid[j] = split->dist;
	    else if (split->normal[j] == -1)
		mid[j] = -split->dist;
	    else
		mid[j] = p1[j] + dot * (p2[j] - p1[j]);
	}

	VectorCopy(mid, f->points[f->numpoints]);
	f->numpoints++;
	VectorCopy(mid, b->points[b->numpoints]);
	b->numpoints++;
    }

    if (f->numpoints > maxpts || b->numpoints > maxpts)
	Message(msgError, errLowPointCount);
}


/*
==================
MidpointWinding
==================
*/
void
MidpointWinding(winding_t *w, vec3_t v)
{
    int i, j;

    VectorCopy(vec3_origin, v);
    for (i = 0; i < w->numpoints; i++)
	for (j = 0; j < 3; j++)
	    v[j] += w->points[i][j];

    if (w->numpoints > 0)
	for (j = 0; j < 3; j++)
	    v[j] /= w->numpoints;
}
