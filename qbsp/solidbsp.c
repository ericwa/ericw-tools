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

#include <malloc.h>

#include "qbsp.h"

int splitnodes;

static int leaffaces;
static int nodefaces;
static int c_solid, c_empty, c_water;
static bool usemidsplit;

//============================================================================

/*
==================
FaceSide

For BSP hueristic
==================
*/
static int
FaceSide(face_t *in, plane_t *split)
{
    bool frontcount, backcount;
    vec_t dot;
    int i;
    vec_t *p;

    frontcount = backcount = false;

    // axial planes are fast
    if (split->type < 3)
	for (i = 0, p = in->pts[0] + split->type; i < in->numpoints;
	     i++, p += 3) {
	    if (*p > split->dist + ON_EPSILON) {
		if (backcount)
		    return SIDE_ON;
		frontcount = true;
	    } else if (*p < split->dist - ON_EPSILON) {
		if (frontcount)
		    return SIDE_ON;
		backcount = true;
	    }
    } else
	// sloping planes take longer
	for (i = 0, p = in->pts[0]; i < in->numpoints; i++, p += 3) {
	    dot = DotProduct(p, split->normal);
	    dot -= split->dist;
	    if (dot > ON_EPSILON) {
		if (backcount)
		    return SIDE_ON;
		frontcount = true;
	    } else if (dot < -ON_EPSILON) {
		if (frontcount)
		    return SIDE_ON;
		backcount = true;
	    }
	}

    if (!frontcount)
	return SIDE_BACK;
    if (!backcount)
	return SIDE_FRONT;

    return SIDE_ON;
}

/*
==================
ChooseMidPlaneFromList

The clipping hull BSP doesn't worry about avoiding splits
==================
*/
static surface_t *
ChooseMidPlaneFromList(surface_t *surfaces, vec3_t mins, vec3_t maxs)
{
    int j, l;
    surface_t *p, *bestsurface;
    vec_t bestvalue, value, dist;
    plane_t *plane;

    // pick the plane that splits the least
    bestvalue = 6 * 8192 * 8192;
    bestsurface = NULL;

    for (p = surfaces; p; p = p->next) {
	if (p->onnode)
	    continue;

	plane = &pPlanes[p->planenum];

	// check for axis aligned surfaces
	l = plane->type;
	if (l > PLANE_Z)
	    continue;

	// calculate the split metric along axis l, smaller values are better
	value = 0;

	dist = plane->dist * plane->normal[l];
	for (j = 0; j < 3; j++) {
	    if (j == l) {
		value += (maxs[l] - dist) * (maxs[l] - dist);
		value += (dist - mins[l]) * (dist - mins[l]);
	    } else
		value += 2 * (maxs[j] - mins[j]) * (maxs[j] - mins[j]);
	}

	if (value > bestvalue)
	    continue;

	// currently the best!
	bestvalue = value;
	bestsurface = p;
    }

    if (!bestsurface) {
	for (p = surfaces; p; p = p->next)
	    if (!p->onnode)
		return p;	// first valid surface
	Message(msgError, errNoValidPlanes);
    }

    return bestsurface;
}



/*
==================
ChoosePlaneFromList

The real BSP hueristic
==================
*/
static surface_t *
ChoosePlaneFromList(surface_t *surfaces, vec3_t mins, vec3_t maxs)
{
    int j, k, l;
    surface_t *p, *p2, *bestsurface;
    int bestvalue;
    vec_t bestdistribution, value, dist;
    plane_t *plane;
    face_t *f;

    // pick the plane that splits the least
    bestvalue = 99999;
    bestsurface = NULL;
    bestdistribution = 9e30f;

    for (p = surfaces; p; p = p->next) {
	if (p->onnode)
	    continue;

	plane = &pPlanes[p->planenum];
	k = 0;

	for (p2 = surfaces; p2; p2 = p2->next) {
	    if (p2 == p || p2->onnode)
		continue;

	    for (f = p2->faces; f; f = f->next) {
		if (FaceSide(f, plane) == SIDE_ON) {
		    k++;
		    if (k >= bestvalue)
			break;
		}

	    }
	    if (k > bestvalue)
		break;
	}

	if (k > bestvalue)
	    continue;

	// if equal numbers, axial planes win, then decide on spatial subdivision

	if (k < bestvalue || (k == bestvalue && plane->type < PLANE_ANYX)) {
	    // check for axis aligned surfaces
	    l = plane->type;

	    if (l <= PLANE_Z) {	// axial aligned
		// calculate the split metric along axis l
		value = 0;

		for (j = 0; j < 3; j++) {
		    if (j == l) {
			dist = plane->dist * plane->normal[l];
			value += (maxs[l] - dist) * (maxs[l] - dist);
			value += (dist - mins[l]) * (dist - mins[l]);
		    } else
			value +=
			    2 * (maxs[j] - mins[j]) * (maxs[j] - mins[j]);
		}

		if (value > bestdistribution && k == bestvalue)
		    continue;
		bestdistribution = value;
	    }
	    // currently the best!
	    bestvalue = k;
	    bestsurface = p;
	}

    }

    return bestsurface;
}


/*
==================
SelectPartition

Selects a surface from a linked list of surfaces to split the group on
returns NULL if the surface list can not be divided any more (a leaf)
==================
*/
static surface_t *
SelectPartition(surface_t *surfaces)
{
    int i, j;
    vec3_t mins, maxs;
    surface_t *p, *bestsurface;

    // count onnode surfaces
    i = 0;
    bestsurface = NULL;
    for (p = surfaces; p; p = p->next)
	if (!p->onnode) {
	    i++;
	    bestsurface = p;
	}

    if (i == 0)
	return NULL;

    if (i == 1)
	return bestsurface;	// this is a final split

    // calculate a bounding box of the entire surfaceset
    for (i = 0; i < 3; i++) {
	mins[i] = 99999;
	maxs[i] = -99999;
    }

    for (p = surfaces; p; p = p->next)
	for (j = 0; j < 3; j++) {
	    if (p->mins[j] < mins[j])
		mins[j] = p->mins[j];
	    if (p->maxs[j] > maxs[j])
		maxs[j] = p->maxs[j];
	}

    if (usemidsplit)		// do fast way for clipping hull
	return ChooseMidPlaneFromList(surfaces, mins, maxs);

    // do slow way to save poly splits for drawing hull
    return ChoosePlaneFromList(surfaces, mins, maxs);
}

//============================================================================

/*
=================
CalcSurfaceInfo

Calculates the bounding box
=================
*/
void
CalcSurfaceInfo(surface_t *surf)
{
    int i, j;
    face_t *f;

    // calculate a bounding box
    for (i = 0; i < 3; i++) {
	surf->mins[i] = 99999;
	surf->maxs[i] = -99999;
    }

    for (f = surf->faces; f; f = f->next) {
	if (f->contents[0] >= 0 || f->contents[1] >= 0)
	    Message(msgError, errBadContents);
	for (i = 0; i < f->numpoints; i++)
	    for (j = 0; j < 3; j++) {
		if (f->pts[i][j] < surf->mins[j])
		    surf->mins[j] = f->pts[i][j];
		if (f->pts[i][j] > surf->maxs[j])
		    surf->maxs[j] = f->pts[i][j];
	    }
    }
}



/*
==================
DividePlane
==================
*/
static void
DividePlane(surface_t *in, plane_t *split, surface_t **front,
	    surface_t **back)
{
    face_t *facet, *next;
    face_t *frontlist, *backlist;
    face_t *frontfrag, *backfrag;
    surface_t *news;
    plane_t *inplane;

    inplane = &pPlanes[in->planenum];
    *front = *back = NULL;

    // parallel case is easy
    if (VectorCompare(inplane->normal, split->normal)) {
	// check for exactly on node
	if (inplane->dist == split->dist) {
	    facet = in->faces;
	    in->faces = NULL;
	    in->onnode = true;

	    // divide the facets to the front and back sides
	    news = AllocMem(SURFACE, 1, true);
	    *news = *in;

	    // Prepend each face in facet list to either in or news lists
	    for (; facet; facet = next) {
		next = facet->next;
		if (facet->planeside == 1) {
		    facet->next = news->faces;
		    news->faces = facet;
		} else {
		    facet->next = in->faces;
		    in->faces = facet;
		}
	    }

	    if (in->faces)
		*front = in;
	    if (news->faces)
		*back = news;
	    return;
	}

	if (inplane->dist > split->dist)
	    *front = in;
	else
	    *back = in;
	return;
    }
// do a real split.  may still end up entirely on one side
// OPTIMIZE: use bounding box for fast test
    frontlist = NULL;
    backlist = NULL;

    for (facet = in->faces; facet; facet = next) {
	next = facet->next;
	SplitFace(facet, split, &frontfrag, &backfrag);
	if (frontfrag) {
	    frontfrag->next = frontlist;
	    frontlist = frontfrag;
	}
	if (backfrag) {
	    backfrag->next = backlist;
	    backlist = backfrag;
	}
    }

    // if nothing actually got split, just move the in plane
    if (frontlist == NULL) {
	*back = in;
	in->faces = backlist;
	return;
    }

    if (backlist == NULL) {
	*front = in;
	in->faces = frontlist;
	return;
    }

    // stuff got split, so allocate one new plane and reuse in
    news = AllocMem(SURFACE, 1, true);
    *news = *in;
    news->faces = backlist;
    *back = news;

    in->faces = frontlist;
    *front = in;

    // recalc bboxes and flags
    CalcSurfaceInfo(news);
    CalcSurfaceInfo(in);
}

/*
==================
DivideNodeBounds
==================
*/
static void
DivideNodeBounds(node_t *node, plane_t *split)
{
    VectorCopy(node->mins, node->children[0]->mins);
    VectorCopy(node->mins, node->children[1]->mins);
    VectorCopy(node->maxs, node->children[0]->maxs);
    VectorCopy(node->maxs, node->children[1]->maxs);

    // OPTIMIZE: sloping cuts can give a better bbox than this...
    if (split->type > 2)
	return;

    node->children[0]->mins[split->type] =
	node->children[1]->maxs[split->type] = split->dist;
}

/*
==================
LinkConvexFaces

Determines the contents of the leaf and creates the final list of
original faces that have some fragment inside this leaf
==================
*/
static void
LinkConvexFaces(surface_t *planelist, node_t *leafnode)
{
    face_t *f, *next;
    surface_t *surf, *pnext;
    int i, count;

    leafnode->faces = NULL;
    leafnode->contents = 0;
    leafnode->planenum = -1;

    count = 0;
    for (surf = planelist; surf; surf = surf->next) {
	for (f = surf->faces; f; f = f->next) {
	    count++;
	    if (!leafnode->contents)
		leafnode->contents = f->contents[0];
	    else if (leafnode->contents != f->contents[0])
		Message(msgError, errMixedFaceContents, f->pts[0][0],
			f->pts[0][1], f->pts[0][2]);
	}
    }

    if (!leafnode->contents)
	leafnode->contents = CONTENTS_SOLID;

    switch (leafnode->contents) {
    case CONTENTS_EMPTY:
	c_empty++;
	break;
    case CONTENTS_SOLID:
	c_solid++;
	break;
    case CONTENTS_WATER:
    case CONTENTS_SLIME:
    case CONTENTS_LAVA:
    case CONTENTS_SKY:
	c_water++;
	break;
    default:
	Message(msgError, errBadContents);
    }

    // write the list of faces, and free the originals
    leaffaces += count;
    leafnode->markfaces = malloc(sizeof(face_t *) * (count + 1)); /* FIXME */

    i = 0;
    for (surf = planelist; surf; surf = pnext) {
	pnext = surf->next;
	for (f = surf->faces; f; f = next) {
	    next = f->next;
	    leafnode->markfaces[i] = f->original;
	    i++;
	    FreeMem(f, FACE, 1);
	}
	FreeMem(surf, SURFACE, 1);
    }
    leafnode->markfaces[i] = NULL;	// sentinal
}


/*
==================
LinkNodeFaces

Returns a duplicated list of all faces on surface
==================
*/
static face_t *
LinkNodeFaces(surface_t *surface)
{
    face_t *f, *newf, **prevptr;
    face_t *list = NULL;

    // subdivide large faces
    prevptr = &surface->faces;
    f = *prevptr;
    while (f) {
	SubdivideFace(f, prevptr);
	prevptr = &(*prevptr)->next;
	f = *prevptr;
    }

    // copy
    for (f = surface->faces; f; f = f->next) {
	nodefaces++;
	newf = AllocMem(FACE, 1, true);
	*newf = *f;
	f->original = newf;
	newf->next = list;
	list = newf;
    }

    return list;
}


/*
==================
PartitionSurfaces
==================
*/
static void
PartitionSurfaces(surface_t *surfaces, node_t *node)
{
    surface_t *split, *p, *next;
    surface_t *frontlist, *backlist;
    surface_t *frontfrag, *backfrag;
    plane_t *splitplane;

    split = SelectPartition(surfaces);
    if (!split) {		// this is a leaf node
	node->planenum = PLANENUM_LEAF;
	LinkConvexFaces(surfaces, node);
	return;
    }

    splitnodes++;
    Message(msgPercent, splitnodes, csgmergefaces);

    node->faces = LinkNodeFaces(split);
    node->children[0] = AllocMem(NODE, 1, true);
    node->children[1] = AllocMem(NODE, 1, true);
    node->planenum = split->planenum;

    splitplane = &pPlanes[split->planenum];

    DivideNodeBounds(node, splitplane);

    // multiple surfaces, so split all the polysurfaces into front and back lists
    frontlist = NULL;
    backlist = NULL;

    for (p = surfaces; p; p = next) {
	next = p->next;
	DividePlane(p, splitplane, &frontfrag, &backfrag);
	if (frontfrag && backfrag) {
	    // the plane was split, which may expose oportunities to merge
	    // adjacent faces into a single face
//                      MergePlaneFaces (frontfrag);
//                      MergePlaneFaces (backfrag);
	}

	if (frontfrag) {
	    if (!frontfrag->faces)
		Message(msgError, errNoSurfaceFaces);
	    frontfrag->next = frontlist;
	    frontlist = frontfrag;
	}
	if (backfrag) {
	    if (!backfrag->faces)
		Message(msgError, errNoSurfaceFaces);
	    backfrag->next = backlist;
	    backlist = backfrag;
	}
    }

    PartitionSurfaces(frontlist, node->children[0]);
    PartitionSurfaces(backlist, node->children[1]);
}


/*
==================
SolidBSP
==================
*/
node_t *
SolidBSP(surface_t *surfhead, bool midsplit)
{
    int i;
    node_t *headnode;

    Message(msgProgress, "SolidBSP");

    headnode = AllocMem(NODE, 1, true);
    usemidsplit = midsplit;

    // calculate a bounding box for the entire model
    for (i = 0; i < 3; i++) {
	headnode->mins[i] = pCurEnt->mins[i] - SIDESPACE;
	headnode->maxs[i] = pCurEnt->maxs[i] + SIDESPACE;
    }

    // recursively partition everything
    splitnodes = 0;
    leaffaces = 0;
    nodefaces = 0;
    c_solid = c_empty = c_water = 0;

    PartitionSurfaces(surfhead, headnode);

    Message(msgStat, "%5i split nodes", splitnodes);
    Message(msgStat, "%5i solid leafs", c_solid);
    Message(msgStat, "%5i empty leafs", c_empty);
    Message(msgStat, "%5i water leafs", c_water);
    Message(msgStat, "%5i leaffaces", leaffaces);
    Message(msgStat, "%5i nodefaces", nodefaces);

    return headnode;
}
