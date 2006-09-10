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
// csg4.c

#include "qbsp.h"

/*

NOTES
-----
Brushes that touch still need to be split at the cut point to make a tjunction

*/

face_t **validfaces;


face_t *inside, *outside;
int brushfaces;
int csgfaces;
int csgmergefaces;

/*
==================
NewFaceFromFace

Duplicates the non point information of a face, used by SplitFace and
MergeFace.
==================
*/
face_t *
NewFaceFromFace(face_t *in)
{
    face_t *newf;

    newf = (face_t *)AllocMem(FACE, 1, true);

    newf->planenum = in->planenum;
    newf->texturenum = in->texturenum;
    newf->planeside = in->planeside;
    newf->original = in->original;
    newf->contents[0] = in->contents[0];
    newf->contents[1] = in->contents[1];

    return newf;
}


/*
==================
SplitFace
==================
*/
void
SplitFace(face_t *in, plane_t *split, face_t **front, face_t **back)
{
    vec_t dists[MAXEDGES + 1];
    int sides[MAXEDGES + 1];
    int counts[3];
    vec_t dot;
    int i, j;
    face_t *newf, *new2;
    vec_t *p1, *p2;
    vec3_t mid;

    if (in->numpoints < 0)
	Message(msgError, errFreedFace);
    counts[0] = counts[1] = counts[2] = 0;

    // determine sides for each point
    for (i = 0; i < in->numpoints; i++) {
	dot = DotProduct(in->pts[i], split->normal);
	dot -= split->dist;
	dists[i] = dot;
	if (dot > ON_EPSILON)
	    sides[i] = SIDE_FRONT;
	else if (dot < -ON_EPSILON)
	    sides[i] = SIDE_BACK;
	else
	    sides[i] = SIDE_ON;
	counts[sides[i]]++;
    }
    sides[i] = sides[0];
    dists[i] = dists[0];

    // Plane doesn't split this face after all
    if (!counts[SIDE_FRONT]) {
	*front = NULL;
	*back = in;
	return;
    }
    if (!counts[SIDE_BACK]) {
	*front = in;
	*back = NULL;
	return;
    }

    *back = newf = NewFaceFromFace(in);
    *front = new2 = NewFaceFromFace(in);

    // distribute the points and generate splits
    for (i = 0; i < in->numpoints; i++) {
	// Note: Possible for numpoints on newf or new2 to exceed MAXEDGES if
	// in->numpoints == MAXEDGES and it is a really devious split.

	p1 = in->pts[i];

	if (sides[i] == SIDE_ON) {
	    VectorCopy(p1, newf->pts[newf->numpoints]);
	    newf->numpoints++;
	    VectorCopy(p1, new2->pts[new2->numpoints]);
	    new2->numpoints++;
	    continue;
	}

	if (sides[i] == SIDE_FRONT) {
	    VectorCopy(p1, new2->pts[new2->numpoints]);
	    new2->numpoints++;
	} else {
	    VectorCopy(p1, newf->pts[newf->numpoints]);
	    newf->numpoints++;
	}

	if (sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i])
	    continue;

	// generate a split point
	p2 = in->pts[(i + 1) % in->numpoints];

	dot = dists[i] / (dists[i] - dists[i + 1]);
	for (j = 0; j < 3; j++) {	// avoid round off error when possible
	    if (split->normal[j] == 1)
		mid[j] = split->dist;
	    else if (split->normal[j] == -1)
		mid[j] = -split->dist;
	    else
		mid[j] = p1[j] + dot * (p2[j] - p1[j]);
	}

	VectorCopy(mid, newf->pts[newf->numpoints]);
	newf->numpoints++;
	VectorCopy(mid, new2->pts[new2->numpoints]);
	new2->numpoints++;
    }

    if (newf->numpoints > MAXEDGES || new2->numpoints > MAXEDGES)
	Message(msgError, errLowSplitPointCount);

    // free the original face now that it is represented by the fragments
    FreeMem(in, FACE, 1);
}

/*
=================
ClipInside

Clips all of the faces in the inside list, possibly moving them to the
outside list or spliting it into a piece in each list.

Faces exactly on the plane will stay inside unless overdrawn by later brush

frontside is the side of the plane that holds the outside list
=================
*/
void
ClipInside(int splitplane, int frontside, bool precedence)
{
    face_t *f, *next;
    face_t *frags[2];
    face_t *insidelist;
    plane_t *split;

    split = &pPlanes[splitplane];

    insidelist = NULL;
    for (f = inside; f; f = next) {
	next = f->next;

	if (f->planenum == splitplane) {	// exactly on, handle special
	    if (frontside != f->planeside || precedence) {	// allways clip off opposite faceing
		frags[frontside] = NULL;
		frags[!frontside] = f;
	    } else {		// leave it on the outside
		frags[frontside] = f;
		frags[!frontside] = NULL;
	    }
	} else {		// proper split
	    SplitFace(f, split, &frags[0], &frags[1]);
	}

	if (frags[frontside]) {
	    frags[frontside]->next = outside;
	    outside = frags[frontside];
	}
	if (frags[!frontside]) {
	    frags[!frontside]->next = insidelist;
	    insidelist = frags[!frontside];
	}
    }

    inside = insidelist;
}


/*
==================
SaveOutside

Saves all of the faces in the outside list to the bsp plane list
==================
*/
void
SaveOutside(bool mirror)
{
    face_t *f, *next, *newf;
    int i;
    int planenum;

    for (f = outside; f; f = next) {
	next = f->next;
	csgfaces++;
	planenum = f->planenum;

	if (mirror) {
	    newf = NewFaceFromFace(f);

	    newf->numpoints = f->numpoints;
	    newf->planeside = f->planeside ^ 1;	// reverse side
	    newf->contents[0] = f->contents[1];
	    newf->contents[1] = f->contents[0];

	    for (i = 0; i < f->numpoints; i++)	// add points backwards
		VectorCopy(f->pts[f->numpoints - 1 - i], newf->pts[i]);

	    validfaces[planenum] =
		MergeFaceToList(newf, validfaces[planenum]);
	}

	validfaces[planenum] = MergeFaceToList(f, validfaces[planenum]);
	validfaces[planenum] = FreeMergeListScraps(validfaces[planenum]);
    }
}

/*
==================
FreeInside

Free all the faces that got clipped out
==================
*/
void
FreeInside(int contents)
{
    face_t *f, *next;

    for (f = inside; f; f = next) {
	next = f->next;

	if (contents != CONTENTS_SOLID) {
	    f->contents[0] = contents;
	    f->next = outside;
	    outside = f;
	} else
	    FreeMem(f, FACE, 1);
    }
}


//==========================================================================

/*
==================
BuildSurfaces

Returns a chain of all the external surfaces with one or more visible
faces.
==================
*/
surface_t *
BuildSurfaces(void)
{
    face_t **f;
    face_t *count;
    int i;
    surface_t *s;
    surface_t *surfhead;

    surfhead = NULL;

    f = validfaces;
    for (i = 0; i < numbrushplanes; i++, f++) {
	if (!*f)
	    continue;		// nothing left on this plane

	// create a new surface to hold the faces on this plane
	s = (surface_t *)AllocMem(SURFACE, 1, true);
	s->planenum = i;
	s->next = surfhead;
	surfhead = s;
	s->faces = *f;
	for (count = s->faces; count; count = count->next)
	    csgmergefaces++;
	CalcSurfaceInfo(s);	// bounding box and flags
    }

    return surfhead;
}

//==========================================================================

/*
==================
CopyFacesToOutside
==================
*/
void
CopyFacesToOutside(brush_t *b)
{
    face_t *f, *newf;

    outside = NULL;

    for (f = b->faces; f; f = f->next) {
	brushfaces++;
	newf = (face_t *)AllocMem(FACE, 1, true);
	*newf = *f;
	newf->next = outside;
	newf->contents[0] = CONTENTS_EMPTY;
	newf->contents[1] = b->contents;
	outside = newf;
    }
}


/*
==================
CSGFaces

Returns a list of surfaces containing all of the faces
==================
*/
surface_t *
CSGFaces(void)
{
    brush_t *b1, *b2;
    int i;
    bool overwrite;
    face_t *f;
    surface_t *surfhead;
    int iBrushes = 0;

    Message(msgProgress, "CSGFaces");

    if (validfaces == NULL)
	validfaces = (face_t **)AllocMem(OTHER, sizeof(face_t *) * cPlanes,
					 true);
    else
	memset(validfaces, 0, sizeof(face_t *) * cPlanes);
    csgfaces = brushfaces = csgmergefaces = 0;

    // do the solid faces
    for (b1 = pCurEnt->pBrushes; b1; b1 = b1->next) {
	// set outside to a copy of the brush's faces
	CopyFacesToOutside(b1);

	// Why is this necessary?
	overwrite = false;

	for (b2 = pCurEnt->pBrushes; b2; b2 = b2->next) {
	    // check bounding box first
	    for (i = 0; i < 3; i++)
		if (b1->mins[i] > b2->maxs[i] || b1->maxs[i] < b2->mins[i])
		    break;
	    if (i < 3)
		continue;

	    // see if b2 needs to clip a chunk out of b1
	    if (b1 == b2) {
		overwrite = true;
		continue;
	    }
	    // divide faces by the planes of the new brush
	    inside = outside;
	    outside = NULL;

	    for (f = b2->faces; f; f = f->next)
		ClipInside(f->planenum, f->planeside, overwrite);

	    // these faces are continued in another brush, so get rid of them
	    if (b1->contents == CONTENTS_SOLID
		&& b2->contents <= CONTENTS_WATER)
		FreeInside(b2->contents);
	    else
		FreeInside(CONTENTS_SOLID);
	}

	// all of the faces left in outside are real surface faces
	if (b1->contents != CONTENTS_SOLID)
	    SaveOutside(true);	// mirror faces for inside view
	else
	    SaveOutside(false);

	iBrushes++;
	Message(msgPercent, iBrushes, pCurEnt->cBrushes);
    }

    surfhead = BuildSurfaces();

    Message(msgStat, "%5i brushfaces", brushfaces);
    Message(msgStat, "%5i csgfaces", csgfaces);
    Message(msgStat, "%5i mergedfaces", csgmergefaces);

    return surfhead;
}
