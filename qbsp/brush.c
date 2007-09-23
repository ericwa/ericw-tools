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

#include <string.h>

#include "qbsp.h"

int numbrushplanes;

/* beveled clipping hull can generate many extra faces */
static mapface_t faces[128];
static int numbrushfaces;

/*
=================
CheckFace

Note: this will not catch 0 area polygons
=================
*/
static void
CheckFace(face_t *f)
{
    int i, j;
    vec_t *p1, *p2;
    vec_t d, edgedist;
    vec3_t dir, edgenormal, facenormal;

    if (f->w.numpoints < 3)
	Message(msgError, errTooFewPoints, f->w.numpoints);

    VectorCopy(pPlanes[f->planenum].normal, facenormal);
    if (f->planeside) {
	VectorSubtract(vec3_origin, facenormal, facenormal);
    }

    for (i = 0; i < f->w.numpoints; i++) {
	p1 = f->w.points[i];

	for (j = 0; j < 3; j++)
	    if (p1[j] > BOGUS_RANGE || p1[j] < -BOGUS_RANGE)
		Message(msgError, errBogusRange, p1[j]);

	j = i + 1 == f->w.numpoints ? 0 : i + 1;

	// check the point is on the face plane
	d = DotProduct(p1,
		       pPlanes[f->planenum].normal) -
	    pPlanes[f->planenum].dist;
	if (d < -ON_EPSILON || d > ON_EPSILON)
	    // This used to be an error
	    Message(msgWarning, warnPointOffPlane, p1[0], p1[1], p1[2], d);

	// check the edge isn't degenerate
	p2 = f->w.points[j];
	VectorSubtract(p2, p1, dir);

	if (VectorLength(dir) < ON_EPSILON) {
	    Message(msgWarning, warnDegenerateEdge, p1[0], p1[1], p1[2]);
	    for (j = i + 1; j < f->w.numpoints; j++)
		VectorCopy(f->w.points[j], f->w.points[j - 1]);
	    f->w.numpoints--;
	    CheckFace(f);
	    break;
	}

	CrossProduct(facenormal, dir, edgenormal);
	VectorNormalize(edgenormal);
	edgedist = DotProduct(p1, edgenormal);
	edgedist += ON_EPSILON;

	// all other points must be on front side
	for (j = 0; j < f->w.numpoints; j++) {
	    if (j == i)
		continue;
	    d = DotProduct(f->w.points[j], edgenormal);
	    if (d > edgedist)
		Message(msgError, errConcaveFace);
	}
    }
}


//===========================================================================

/*
=================
AddToBounds
=================
*/
static void
AddToBounds(vec3_t v)
{
    int i;

    for (i = 0; i < 3; i++) {
	if (v[i] < pCurEnt->mins[i])
	    pCurEnt->mins[i] = v[i];
	if (v[i] > pCurEnt->maxs[i])
	    pCurEnt->maxs[i] = v[i];
    }
}

//===========================================================================

static int
NormalizePlane(plane_t *p)
{
    int i;
    vec_t ax, ay, az;

    for (i = 0; i < 3; i++) {
	if (p->normal[i] == 1.0) {
	    p->normal[(i + 1) % 3] = 0;
	    p->normal[(i + 2) % 3] = 0;
	    p->type = PLANE_X + i;
	    return 0; /* no flip */
	}
	if (p->normal[i] == -1.0) {
	    p->normal[i] = 1.0;
	    p->normal[(i + 1) % 3] = 0;
	    p->normal[(i + 2) % 3] = 0;
	    p->dist = -p->dist;
	    p->type = PLANE_X + i;
	    return 1; /* plane flipped */
	}
    }

    ax = fabs(p->normal[0]);
    ay = fabs(p->normal[1]);
    az = fabs(p->normal[2]);

    if (ax >= ay && ax >= az)
	p->type = PLANE_ANYX;
    else if (ay >= ax && ay >= az)
	p->type = PLANE_ANYY;
    else
	p->type = PLANE_ANYZ;

    if (p->normal[p->type - PLANE_ANYX] < 0) {
	VectorSubtract(vec3_origin, p->normal, p->normal);
	p->dist = -p->dist;
	return 1; /* plane flipped */
    }
    return 0; /* no flip */
}


static int
PlaneEqual(const plane_t *p, const vec3_t normal, const vec_t dist)
{
    return (fabs(p->normal[0] - normal[0]) < NORMAL_EPSILON &&
	    fabs(p->normal[1] - normal[1]) < NORMAL_EPSILON &&
	    fabs(p->normal[2] - normal[2]) < NORMAL_EPSILON &&
	    fabs(p->dist - dist) < DIST_EPSILON);
}

static int
PlaneInvEqual(const plane_t *p, const vec3_t normal, const vec_t dist)
{
    return (fabs(p->normal[0] + normal[0]) < NORMAL_EPSILON &&
	    fabs(p->normal[1] + normal[1]) < NORMAL_EPSILON &&
	    fabs(p->normal[2] + normal[2]) < NORMAL_EPSILON &&
	    fabs(p->dist + dist) < DIST_EPSILON);
}

/* Plane Hashing */
#define	PLANE_HASHES (1<<10)
static struct plane *plane_hash[PLANE_HASHES];

/*
 * Choice of hash function:
 * - Begin with abs(dist), very rarely > 4096
 * - Many maps probably won't go beyond 2048 units
 * - Low 3 bits also very commonly zero (axial planes on multiples of 8 units)
 */
static inline int
plane_hash_fn(const struct plane *p)
{
    const int dist = floor(fabs(p->dist) + 0.5);

    return (dist ^ (dist >> 3)) & (PLANE_HASHES - 1);
}

static void
PlaneHash_Add(struct plane *p)
{
    const int hash = plane_hash_fn(p);

    p->hash_chain = plane_hash[hash];
    plane_hash[hash] = p;
}

void
PlaneHash_Init(void)
{
    int i;

    for (i = 0; i < PLANE_HASHES; ++i)
	plane_hash[i] = NULL;
}

/*
 * NewPlane
 * - Returns a global plane number and the side that will be the front
 */
static int
NewPlane(vec3_t normal, vec_t dist, int *side)
{
    plane_t *p;
    vec_t len;

    len = VectorLength(normal);
    if (len < 1 - ON_EPSILON || len > 1 + ON_EPSILON)
	Message(msgError, errInvalidNormal, len);
    if (numbrushplanes == cPlanes)
	Message(msgError, errLowBrushPlaneCount);

    p = &pPlanes[numbrushplanes];
    VectorCopy(normal, p->normal);
    p->dist = dist;
    *side = NormalizePlane(p) ? SIDE_BACK : SIDE_FRONT;
    PlaneHash_Add(p);

    return numbrushplanes++;
}

/*
 * FindPlane
 * - Returns a global plane number and the side that will be the front
 */
int
FindPlane(plane_t *plane, int *side)
{
    const int bins[] = { 0, 1, -1 };
    plane_t *p;
    int hash, h;
    int i;

    /* search the border bins as well */
    hash = plane_hash_fn(plane);
    for (i = 0; i < 3; ++i) {
	h = (hash + bins[i]) & (PLANE_HASHES - 1);
	for (p = plane_hash[h]; p; p = p->hash_chain) {
	    if (PlaneEqual(p, plane->normal, plane->dist)) {
		*side = SIDE_FRONT;
		return p - pPlanes;
	    } else if (PlaneInvEqual(p, plane->normal, plane->dist)) {
		*side = SIDE_BACK;
		return p - pPlanes;
	    }
	}
    }

    return NewPlane(plane->normal, plane->dist, side);
}


/*
=============================================================================

			TURN BRUSHES INTO GROUPS OF FACES

=============================================================================
*/

static vec3_t brush_mins, brush_maxs;

/*
=================
FindTargetEntity
=================
*/
static int
FindTargetEntity(char *szTarget)
{
    int iEntity;
    char *szName;

    for (iEntity = 0; iEntity < map.cEntities; iEntity++) {
	szName = ValueForKey(iEntity, "targetname");
	if (szName && !strcasecmp(szTarget, szName))
	    return iEntity;
    }

    return -1;
}


/*
=================
FixRotateOrigin
=================
*/
void
FixRotateOrigin(int iEntity, vec3_t offset)
{
    int iFoundEnt;
    char *szSearch;
    char szOrigin[20];

    szSearch = ValueForKey(iEntity, "target");
    if (!szSearch) {
	szSearch = ValueForKey(iEntity, "classname");
	Message(msgWarning, warnNoRotateTarget, szSearch);
    } else {
	iFoundEnt = FindTargetEntity(szSearch);
	if (iFoundEnt != -1)
	    GetVectorForKey(iFoundEnt, "origin", offset);
    }
    sprintf(szOrigin, "%d %d %d", (int)offset[0], (int)offset[1],
	    (int)offset[2]);
    SetKeyValue(iEntity, "origin", szOrigin);
}


/*
=================
CreateBrushFaces
=================
*/
static face_t *
CreateBrushFaces(void)
{
    int i, j, k;
    vec_t r;
    face_t *f;
    winding_t *w;
    plane_t plane;
    face_t *pFaceList = NULL;
    mapface_t *pFace;
    vec3_t offset;
    char *szClassname;
    vec3_t point;
    vec_t max, min;

    offset[0] = offset[1] = offset[2] = 0;
    min = brush_mins[0] = brush_mins[1] = brush_mins[2] = VECT_MAX;
    max = brush_maxs[0] = brush_maxs[1] = brush_maxs[2] = -VECT_MAX;

    // Hipnotic rotation
    szClassname = ValueForKey(map.iEntities, "classname");
    if (!strncmp(szClassname, "rotate_", 7))
	FixRotateOrigin(map.iEntities, offset);

    for (i = 0; i < numbrushfaces; i++) {
	pFace = &faces[i];

	w = BaseWindingForPlane(&pFace->plane);

	for (j = 0; j < numbrushfaces && w; j++) {
	    if (j == i)
		continue;
	    // flip the plane, because we want to keep the back side
	    VectorSubtract(vec3_origin, faces[j].plane.normal, plane.normal);
	    plane.dist = -faces[j].plane.dist;

	    w = ClipWinding(w, &plane, false);
	}

	if (!w)
	    continue;		// overcontrained plane

	// this face is a keeper
	f = AllocMem(FACE, 1, true);
	f->w.numpoints = w->numpoints;
	if (f->w.numpoints > MAXEDGES)
	    Message(msgError, errLowFacePointCount);

	for (j = 0; j < w->numpoints; j++) {
	    for (k = 0; k < 3; k++) {
		point[k] = w->points[j][k] - offset[k];
		r = Q_rint(point[k]);
		if (fabs(point[k] - r) < ZERO_EPSILON)
		    f->w.points[j][k] = r;
		else
		    f->w.points[j][k] = point[k];

		if (f->w.points[j][k] < brush_mins[k])
		    brush_mins[k] = f->w.points[j][k];
		if (f->w.points[j][k] > brush_maxs[k])
		    brush_maxs[k] = f->w.points[j][k];
		if (f->w.points[j][k] < min)
		    min = f->w.points[j][k];
		if (f->w.points[j][k] > max)
		    max = f->w.points[j][k];
	    }
	}

	VectorCopy(pFace->plane.normal, plane.normal);
	VectorScale(pFace->plane.normal, pFace->plane.dist, point);
	VectorSubtract(point, offset, point);
	plane.dist = DotProduct(plane.normal, point);

	FreeMem(w, WINDING, 1);

	f->texturenum = hullnum ? 0 : pFace->texinfo;
	f->planenum = FindPlane(&plane, &f->planeside);
	f->next = pFaceList;
	pFaceList = f;
	CheckFace(f);
	UpdateFaceSphere(f);
    }

    // Rotatable objects must have a bounding box big enough to
    // account for all its rotations
    if (!strncmp(szClassname, "rotate_", 7)) {
	vec_t delta;

	delta = fabs(max);
	if (fabs(min) > delta)
	    delta = fabs(min);

	for (k = 0; k < 3; k++) {
	    brush_mins[k] = -delta;
	    brush_maxs[k] = delta;
	}
    }

    return pFaceList;
}


/*
=================
FreeBrushFaces
=================
*/
static void
FreeBrushFaces(face_t *pFaceList)
{
    face_t *pFace, *pNext;

    for (pFace = pFaceList; pFace; pFace = pNext) {
	pNext = pFace->next;
	FreeMem(pFace, FACE, 1);
    }
}


/*
=====================
FreeBrushsetBrushes
=====================
*/
void
FreeBrushsetBrushes(void)
{
    brush_t *pBrush, *pNext;

    for (pBrush = pCurEnt->pBrushes; pBrush; pBrush = pNext) {
	pNext = pBrush->next;
	FreeBrushFaces(pBrush->faces);
	FreeMem(pBrush, BRUSH, 1);
    }
}


/*
==============================================================================

BEVELED CLIPPING HULL GENERATION

This is done by brute force, and could easily get a lot faster if anyone cares.
==============================================================================
*/

// TODO: fix this whole thing
#define	MAX_HULL_POINTS	512
#define	MAX_HULL_EDGES	1024

static int num_hull_points;
static vec3_t hull_points[MAX_HULL_POINTS];
static vec3_t hull_corners[MAX_HULL_POINTS * 8];
static int num_hull_edges;
static int hull_edges[MAX_HULL_EDGES][2];

/*
============
AddBrushPlane
=============
*/
static void
AddBrushPlane(plane_t *plane)
{
    int i;
    plane_t *pl;
    vec_t l;

    l = VectorLength(plane->normal);
    if (l < 1.0 - NORMAL_EPSILON || l > 1.0 + NORMAL_EPSILON)
	Message(msgError, errInvalidNormal, l);

    for (i = 0; i < numbrushfaces; i++) {
	pl = &faces[i].plane;
	if (VectorCompare(pl->normal, plane->normal) &&
	    fabs(pl->dist - plane->dist) < ON_EPSILON)
	    return;
    }
    if (numbrushfaces >= MAX_FACES)
	Message(msgError, errLowBrushFaceCount);
    faces[i].plane = *plane;
    faces[i].texinfo = 0;
    numbrushfaces++;
}


/*
============
TestAddPlane

Adds the given plane to the brush description if all of the original brush
vertexes can be put on the front side
=============
*/
static void
TestAddPlane(plane_t *plane)
{
    int i, c;
    vec_t d;
    vec_t *corner;
    plane_t flip;
    int points_front, points_back;
    plane_t *pl;

    /* see if the plane has already been added */
    for (i = 0; i < numbrushfaces; i++) {
	pl = &faces[i].plane;
	if (PlaneEqual(plane, pl->normal, pl->dist))
	    return;
	if (PlaneInvEqual(plane, pl->normal, pl->dist))
	    return;
    }

    /* check all the corner points */
    points_front = 0;
    points_back = 0;

    corner = hull_corners[0];
    c = num_hull_points * 8;

    for (i = 0; i < c; i++, corner += 3) {
	d = DotProduct(corner, plane->normal) - plane->dist;
	if (d < -ON_EPSILON) {
	    if (points_front)
		return;
	    points_back = 1;
	} else if (d > ON_EPSILON) {
	    if (points_back)
		return;
	    points_front = 1;
	}
    }

    // the plane is a seperator
    if (points_front) {
	VectorSubtract(vec3_origin, plane->normal, flip.normal);
	flip.dist = -plane->dist;
	plane = &flip;
    }

    AddBrushPlane(plane);
}

/*
============
AddHullPoint

Doesn't add if duplicated
=============
*/
static int
AddHullPoint(vec3_t p, vec3_t hull_size[2])
{
    int i;
    vec_t *c;
    int x, y, z;

    for (i = 0; i < num_hull_points; i++)
	if (VectorCompare(p, hull_points[i]))
	    return i;

    if (num_hull_points == MAX_HULL_POINTS)
	Message(msgError, errLowHullPointCount);

    VectorCopy(p, hull_points[num_hull_points]);

    c = hull_corners[i * 8];

    for (x = 0; x < 2; x++)
	for (y = 0; y < 2; y++)
	    for (z = 0; z < 2; z++) {
		c[0] = p[0] + hull_size[x][0];
		c[1] = p[1] + hull_size[y][1];
		c[2] = p[2] + hull_size[z][2];
		c += 3;
	    }

    num_hull_points++;

    return i;
}


/*
============
AddHullEdge

Creates all of the hull planes around the given edge, if not done allready
=============
*/
static void
AddHullEdge(vec3_t p1, vec3_t p2, vec3_t hull_size[2])
{
    int pt1, pt2;
    int i;
    int a, b, c, d, e;
    vec3_t edgevec, planeorg, planevec;
    plane_t plane;
    vec_t length;

    pt1 = AddHullPoint(p1, hull_size);
    pt2 = AddHullPoint(p2, hull_size);

    for (i = 0; i < num_hull_edges; i++)
	if ((hull_edges[i][0] == pt1 && hull_edges[i][1] == pt2)
	    || (hull_edges[i][0] == pt2 && hull_edges[i][1] == pt1))
	    return;

    if (num_hull_edges == MAX_HULL_EDGES)
	Message(msgError, errLowHullEdgeCount);

    hull_edges[i][0] = pt1;
    hull_edges[i][1] = pt2;
    num_hull_edges++;

    VectorSubtract(p1, p2, edgevec);
    VectorNormalize(edgevec);

    for (a = 0; a < 3; a++) {
	b = (a + 1) % 3;
	c = (a + 2) % 3;

	planevec[a] = 1;
	planevec[b] = 0;
	planevec[c] = 0;
	CrossProduct(planevec, edgevec, plane.normal);
	length = VectorLength(plane.normal);

	/* If this edge is almost parallel to the hull edge, skip it. */
	if (length < ANGLEEPSILON)
	    continue;

	VectorScale(plane.normal, 1.0 / length, plane.normal);
	for (d = 0; d <= 1; d++) {
	    for (e = 0; e <= 1; e++) {
		VectorCopy(p1, planeorg);
		planeorg[b] += hull_size[d][b];
		planeorg[c] += hull_size[e][c];
		plane.dist = DotProduct(planeorg, plane.normal);
		TestAddPlane(&plane);
	    }
	}
    }
}


/*
============
ExpandBrush
=============
*/
static void
ExpandBrush(vec3_t hull_size[2], face_t *pFaceList)
{
    int i, x, s;
    vec3_t corner;
    face_t *f;
    plane_t plane, *p;
    int cBevEdge = 0;

    num_hull_points = 0;
    num_hull_edges = 0;

    // create all the hull points
    for (f = pFaceList; f; f = f->next)
	for (i = 0; i < f->w.numpoints; i++) {
	    AddHullPoint(f->w.points[i], hull_size);
	    cBevEdge++;
	}

    // expand all of the planes
    for (i = 0; i < numbrushfaces; i++) {
	p = &faces[i].plane;
	VectorCopy(vec3_origin, corner);
	for (x = 0; x < 3; x++) {
	    if (p->normal[x] > 0)
		corner[x] = hull_size[1][x];
	    else if (p->normal[x] < 0)
		corner[x] = hull_size[0][x];
	}
	p->dist += DotProduct(corner, p->normal);
    }

    // add any axis planes not contained in the brush to bevel off corners
    for (x = 0; x < 3; x++)
	for (s = -1; s <= 1; s += 2) {
	    // add the plane
	    VectorCopy(vec3_origin, plane.normal);
	    plane.normal[x] = (vec_t)s;
	    if (s == -1)
		plane.dist = -brush_mins[x] + -hull_size[0][x];
	    else
		plane.dist = brush_maxs[x] + hull_size[1][x];
	    AddBrushPlane(&plane);
	}

    // add all of the edge bevels
    for (f = pFaceList; f; f = f->next)
	for (i = 0; i < f->w.numpoints; i++)
	    AddHullEdge(f->w.points[i], f->w.points[(i + 1) % f->w.numpoints], hull_size);
}

//============================================================================


/*
===============
LoadBrush

Converts a mapbrush to a bsp brush
===============
*/
static brush_t *
LoadBrush(int iBrush)
{
    brush_t *b;
    mapbrush_t *mb;
    int contents;
    char *szName;
    face_t *pFaceList;
    mapface_t *face;
    texinfo_t *texinfo = pWorldEnt->lumps[BSPTEXINFO].data;

    /* check texture name for attributes */
    mb = &map.rgBrushes[iBrush];
    face = &map.rgFaces[mb->iFaceStart];
    szName = rgszMiptex[texinfo[face->texinfo].miptex];

    if (!strcasecmp(szName, "clip") && hullnum == 0)
	return NULL;		// "clip" brushes don't show up in the draw hull

    // entities never use water merging
    if (map.iEntities != 0)
	contents = CONTENTS_SOLID;
    else if (szName[0] == '*') {
	if (!strncasecmp(szName + 1, "lava", 4))
	    contents = CONTENTS_LAVA;
	else if (!strncasecmp(szName + 1, "slime", 5))
	    contents = CONTENTS_SLIME;
	else
	    contents = CONTENTS_WATER;
    } else if (!strncasecmp(szName, "sky", 3) && hullnum == 0)
	contents = CONTENTS_SKY;
    else
	contents = CONTENTS_SOLID;

    if (hullnum && contents != CONTENTS_SOLID && contents != CONTENTS_SKY)
	return NULL;		// water brushes don't show up in clipping hulls

    // create the faces
    numbrushfaces = mb->iFaceEnd - mb->iFaceStart;
    memcpy(faces, face, numbrushfaces * sizeof(mapface_t));

    pFaceList = CreateBrushFaces();

    if (!pFaceList) {
	Message(msgWarning, warnNoBrushFaces);
	return NULL;
    }

    if (hullnum == 1) {
	vec3_t size[2] = { {-16, -16, -32}, {16, 16, 24} };

	ExpandBrush(size, pFaceList);
	FreeBrushFaces(pFaceList);
	pFaceList = CreateBrushFaces();
    } else if (hullnum == 2) {
	vec3_t size[2] = { {-32, -32, -64}, {32, 32, 24} };

	ExpandBrush(size, pFaceList);
	FreeBrushFaces(pFaceList);
	pFaceList = CreateBrushFaces();
    }

    // create the brush
    b = AllocMem(BRUSH, 1, true);

    b->contents = contents;
    b->faces = pFaceList;
    VectorCopy(brush_mins, b->mins);
    VectorCopy(brush_maxs, b->maxs);

    return b;
}

//=============================================================================


/*
============
Brush_LoadEntity
============
*/
void
Brush_LoadEntity(void)
{
    brush_t *b, *next, *water, *other;
    int iBrush, cMapBrushes;
    int i;

    for (i = 0; i < 3; i++) {
	pCurEnt->mins[i] = VECT_MAX;
	pCurEnt->maxs[i] = -VECT_MAX;
    }

    pCurEnt->cBrushes = 0;
    other = water = NULL;

    Message(msgProgress, "Brush_LoadEntity");

    cMapBrushes = pCurEnt->iBrushEnd - pCurEnt->iBrushStart;
    for (iBrush = pCurEnt->iBrushStart; iBrush < pCurEnt->iBrushEnd; iBrush++) {
	b = LoadBrush(iBrush);
	if (!b)
	    continue;

	pCurEnt->cBrushes++;
	Message(msgPercent, pCurEnt->cBrushes, cMapBrushes);

	if (b->contents != CONTENTS_SOLID) {
	    b->next = water;
	    water = b;
	} else {
	    b->next = other;
	    other = b;
	}

	AddToBounds(b->mins);
	AddToBounds(b->maxs);
    }

    // add all of the water textures at the start
    for (b = water; b; b = next) {
	next = b->next;
	b->next = other;
	other = b;
    }

    Message(msgStat, "%5i brushes", pCurEnt->cBrushes);

    // Store the brushes away
    pCurEnt->pBrushes = other;
}
