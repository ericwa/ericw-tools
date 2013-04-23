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

#include <stddef.h>

#include "qbsp.h"

/*
=================
BaseWindingForPlane
=================
*/
winding_t *
BaseWindingForPlane(const plane_t *p)
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
	Error("Internal error: no axis for winding (%s)", __func__);
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
CopyWinding(const winding_t *w)
{
    int size;
    winding_t *c;

    c = AllocMem(WINDING, w->numpoints, false);
    size = offsetof(winding_t, points[w->numpoints]);
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
CheckWinding(const winding_t *w)
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
ClipWinding(winding_t *in, const plane_t *split, bool keepon)
{
    vec_t dists[MAX_POINTS_ON_WINDING + 1];
    int sides[MAX_POINTS_ON_WINDING + 1];
    int counts[3];
    vec_t fraction;
    int i, j;
    vec_t *p1, *p2;
    vec3_t mid;
    winding_t *neww;
    int maxpts;

    if (in->numpoints > MAX_POINTS_ON_WINDING)
	Error("Internal error: in->numpoints > MAX (%s: %d > %d)",
	      __func__, in->numpoints, MAX_POINTS_ON_WINDING);

    CalcSides(in, split, sides, dists, counts);

    if (keepon && !counts[SIDE_FRONT] && !counts[SIDE_BACK])
	return in;

    if (!counts[SIDE_FRONT]) {
	FreeMem(in, WINDING, 1);
	return NULL;
    }

    if (!counts[SIDE_BACK])
	return in;

    /* 	can't use maxpoints = counts[0] + 2 because of fp grouping errors */
    maxpts = in->numpoints + 4;
    neww = AllocMem(WINDING, maxpts, true);

    for (i = 0; i < in->numpoints; i++) {
	p1 = in->points[i];

	if (sides[i] == SIDE_ON) {
	    if (neww->numpoints == maxpts)
		goto noclip;
	    VectorCopy(p1, neww->points[neww->numpoints]);
	    neww->numpoints++;
	    continue;
	}

	if (sides[i] == SIDE_FRONT) {
	    if (neww->numpoints == maxpts)
		goto noclip;
	    VectorCopy(p1, neww->points[neww->numpoints]);
	    neww->numpoints++;
	}

	if (sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i])
	    continue;

	/* generate a split point */
	p2 = in->points[(i + 1) % in->numpoints];
	fraction = dists[i] / (dists[i] - dists[i + 1]);
	for (j = 0; j < 3; j++) {
	    /* avoid round off error when possible */
	    if (split->normal[j] == 1)
		mid[j] = split->dist;
	    else if (split->normal[j] == -1)
		mid[j] = -split->dist;
	    else
		mid[j] = p1[j] + fraction * (p2[j] - p1[j]);
	}

	if (neww->numpoints == maxpts)
	    goto noclip;
	VectorCopy(mid, neww->points[neww->numpoints]);
	neww->numpoints++;
    }
    FreeMem(in, WINDING, 1);

    return neww;

 noclip:
    Error("Internal error: new->numpoints > MAX (%s: %d > %d)",
	  __func__, neww->numpoints, maxpts);
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
DivideWinding(winding_t *in, const plane_t *split, winding_t **front,
	      winding_t **back)
{
    vec_t dists[MAX_POINTS_ON_WINDING + 1];
    int sides[MAX_POINTS_ON_WINDING + 1];
    int counts[3];
    vec_t fraction;
    int i, j;
    vec_t *p1, *p2;
    vec3_t mid;
    winding_t *f, *b;
    int maxpts;

    if (in->numpoints > MAX_POINTS_ON_WINDING)
	Error("Internal error: in->numpoints > MAX (%s: %d > %d)",
	      __func__, in->numpoints, MAX_POINTS_ON_WINDING);

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

    /* 	can't use maxpoints = counts[0] + 2 because of fp grouping errors */
    maxpts = in->numpoints + 4;
    *front = f = AllocMem(WINDING, maxpts, true);
    *back = b = AllocMem(WINDING, maxpts, true);

    for (i = 0; i < in->numpoints; i++) {
	p1 = in->points[i];

	if (sides[i] == SIDE_ON) {
	    if (f->numpoints == maxpts)
		goto noclip_front;
	    VectorCopy(p1, f->points[f->numpoints]);
	    f->numpoints++;
	    if (b->numpoints == maxpts)
		goto noclip_back;
	    VectorCopy(p1, b->points[b->numpoints]);
	    b->numpoints++;
	    continue;
	}

	if (sides[i] == SIDE_FRONT) {
	    if (f->numpoints == maxpts)
		goto noclip_front;
	    VectorCopy(p1, f->points[f->numpoints]);
	    f->numpoints++;
	}
	if (sides[i] == SIDE_BACK) {
	    if (b->numpoints == maxpts)
		goto noclip_back;
	    VectorCopy(p1, b->points[b->numpoints]);
	    b->numpoints++;
	}

	if (sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i])
	    continue;

	/* generate a split point */
	p2 = in->points[(i + 1) % in->numpoints];

	fraction = dists[i] / (dists[i] - dists[i + 1]);
	for (j = 0; j < 3; j++) {
	    /* avoid round off error when possible */
	    if (split->normal[j] == 1)
		mid[j] = split->dist;
	    else if (split->normal[j] == -1)
		mid[j] = -split->dist;
	    else
		mid[j] = p1[j] + fraction * (p2[j] - p1[j]);
	}

	if (f->numpoints == maxpts)
	    goto noclip_front;
	VectorCopy(mid, f->points[f->numpoints]);
	f->numpoints++;
	if (b->numpoints == maxpts)
	    goto noclip_back;
	VectorCopy(mid, b->points[b->numpoints]);
	b->numpoints++;
    }
    return;

 noclip_front:
    Error("Internal error: front->numpoints > MAX (%s: %d > %d)",
	  __func__, f->numpoints, maxpts);
 noclip_back:
    Error("Internal error: back->numpoints > MAX (%s: %d > %d)",
	  __func__, b->numpoints, maxpts);
}


/*
==================
MidpointWinding
==================
*/
void
MidpointWinding(const winding_t *w, vec3_t v)
{
    int i;

    VectorCopy(vec3_origin, v);
    for (i = 0; i < w->numpoints; i++)
	VectorAdd(v, w->points[i], v);
    if (w->numpoints)
	VectorScale(v, 1.0 / w->numpoints, v);
}
