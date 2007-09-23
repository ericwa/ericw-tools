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
// merge.c

#include "qbsp.h"

#ifdef PARANOID
static void
CheckColinear(face_t *f)
{
    int i, j;
    vec3_t v1, v2;

    for (i = 0; i < f->w.numpoints; i++) {
	// skip the point if the vector from the previous point is the same
	// as the vector to the next point
	j = (i - 1 < 0) ? f->w.numpoints - 1 : i - 1;
	VectorSubtract(f->w.points[i], f->w.points[j], v1);
	VectorNormalize(v1);

	j = (i + 1 == f->w.numpoints) ? 0 : i + 1;
	VectorSubtract(f->w.points[j], f->w.points[i], v2);
	VectorNormalize(v2);

	if (VectorCompare(v1, v2))
	    Message(msgError, errColinearEdge);
    }
}
#endif /* PARANOID */

/*
=============
TryMerge

If two polygons share a common edge and the edges that meet at the
common points are both inside the other polygons, merge them

Returns NULL if the faces couldn't be merged, or the new face.
The originals will NOT be freed.
=============
*/
static face_t *
TryMerge(face_t *f1, face_t *f2)
{
    vec_t *p1, *p2, *p3, *p4, *back;
    face_t *newf;
    int i, j, k, l;
    vec3_t normal, delta, planenormal;
    vec_t dot;
    plane_t *plane;
    bool keep1, keep2;

    if (f1->w.numpoints == -1 ||
	f2->w.numpoints == -1 ||
	f1->planeside != f2->planeside ||
	f1->texturenum != f2->texturenum ||
	f1->contents[0] != f2->contents[0] ||
	f1->contents[1] != f2->contents[1])
	return NULL;

    // find a common edge
    p1 = p2 = NULL;		// stop compiler warning
    j = 0;			//

    for (i = 0; i < f1->w.numpoints; i++) {
	p1 = f1->w.points[i];
	p2 = f1->w.points[(i + 1) % f1->w.numpoints];
	for (j = 0; j < f2->w.numpoints; j++) {
	    p3 = f2->w.points[j];
	    p4 = f2->w.points[(j + 1) % f2->w.numpoints];
	    for (k = 0; k < 3; k++) {
		if (fabs(p1[k] - p4[k]) > EQUAL_EPSILON ||
		    fabs(p2[k] - p3[k]) > EQUAL_EPSILON)
		    break;
	    }
	    if (k == 3)
		break;
	}
	if (j < f2->w.numpoints)
	    break;
    }

    if (i == f1->w.numpoints)
	return NULL;		// no matching edges

    // check slope of connected lines
    // if the slopes are colinear, the point can be removed
    plane = &pPlanes[f1->planenum];
    VectorCopy(plane->normal, planenormal);
    if (f1->planeside)
	VectorSubtract(vec3_origin, planenormal, planenormal);

    back = f1->w.points[(i + f1->w.numpoints - 1) % f1->w.numpoints];
    VectorSubtract(p1, back, delta);
    CrossProduct(planenormal, delta, normal);
    VectorNormalize(normal);

    back = f2->w.points[(j + 2) % f2->w.numpoints];
    VectorSubtract(back, p1, delta);
    dot = DotProduct(delta, normal);
    if (dot > CONTINUOUS_EPSILON)
	return NULL;		// not a convex polygon
    keep1 = dot < -CONTINUOUS_EPSILON;

    back = f1->w.points[(i + 2) % f1->w.numpoints];
    VectorSubtract(back, p2, delta);
    CrossProduct(planenormal, delta, normal);
    VectorNormalize(normal);

    back = f2->w.points[(j + f2->w.numpoints - 1) % f2->w.numpoints];
    VectorSubtract(back, p2, delta);
    dot = DotProduct(delta, normal);
    if (dot > CONTINUOUS_EPSILON)
	return NULL;		// not a convex polygon
    keep2 = dot < -CONTINUOUS_EPSILON;

    // build the new polygon
    if (f1->w.numpoints + f2->w.numpoints > MAXEDGES) {
	Message(msgWarning, warnTooManyMergePoints);
	return NULL;
    }

    newf = NewFaceFromFace(f1);

    // copy first polygon
    if (keep2)
	k = (i + 1) % f1->w.numpoints;
    else
	k = (i + 2) % f1->w.numpoints;
    for (; k != i; k = (k + 1) % f1->w.numpoints) {
	VectorCopy(f1->w.points[k], newf->w.points[newf->w.numpoints]);
	newf->w.numpoints++;
    }

    // copy second polygon
    if (keep1)
	l = (j + 1) % f2->w.numpoints;
    else
	l = (j + 2) % f2->w.numpoints;
    for (; l != j; l = (l + 1) % f2->w.numpoints) {
	VectorCopy(f2->w.points[l], newf->w.points[newf->w.numpoints]);
	newf->w.numpoints++;
    }

    UpdateFaceSphere(newf);

    return newf;
}


/*
===============
MergeFaceToList
===============
*/
face_t *
MergeFaceToList(face_t *face, face_t *list)
{
    face_t *newf, *f;

    f = list;
    while (f) {
#ifdef PARANOID
	CheckColinear (f);
#endif
	newf = TryMerge(face, f);
	if (newf) {
	    FreeMem(face, FACE, 1);
	    f->w.numpoints = -1;	// merged out, remove later
	    face = newf;
	    f = list;
	} else
	    f = f->next;
    }

    // didn't merge, so add at start
    face->next = list;
    return face;
}


/*
===============
FreeMergeListScraps
===============
*/
face_t *
FreeMergeListScraps(face_t *merged)
{
    face_t *head, *next;

    head = NULL;
    for (; merged; merged = next) {
	next = merged->next;
	if (merged->w.numpoints == -1)
	    FreeMem(merged, FACE, 1);
	else {
	    merged->next = head;
	    head = merged;
	}
    }

    return head;
}


/*
===============
MergePlaneFaces
===============
*/
void
MergePlaneFaces(surface_t *plane)
{
    face_t *f, *next;
    face_t *merged;

    merged = NULL;

    for (f = plane->faces; f; f = next) {
	next = f->next;
	merged = MergeFaceToList(f, merged);
    }

    // Remove all empty faces (numpoints == -1) and add the remaining
    // faces to the plane
    plane->faces = FreeMergeListScraps(merged);
}


/*
============
MergeAll
============
*/
void
MergeAll(surface_t *surfhead)
{
    surface_t *surf;
    int mergefaces = 0;
    face_t *f;

    Message(msgProgress, "MergeAll");

    for (surf = surfhead; surf; surf = surf->next) {
	MergePlaneFaces(surf);
	for (f = surf->faces; f; f = f->next)
	    mergefaces++;
    }

    Message(msgStat, "%5i mergefaces", mergefaces);

    // Quick hack to let solidbsp print out progress %
    csgmergefaces = mergefaces;
}
