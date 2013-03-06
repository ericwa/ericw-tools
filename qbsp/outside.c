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

#include "qbsp.h"

typedef struct {
    bool header;	/* Flag true once header has been written */
    int numportals;	/* Number of portals written to .por file */
    int entity;		/* Entity number that outside filling reached */
    portal_t **leaks;
    int numleaks;
} bspleak_t;

typedef struct {
    int outleafs;	/* Count of outside leafs removed by fill process */
    int backdraw;	/* Limit the length of the leak line */
    bspleak_t bspleak;	/* State for writing the .por file */
} fillstate_t;

typedef struct {
    bool fill;
    int fillmark;
    int numportals;
} fillparms_t;

static FILE *LeakFile;
static FILE *PorFile;
static node_t *leakNode = NULL;

/*
===========
PointInLeaf
===========
*/
static node_t *
PointInLeaf(node_t *node, const vec3_t point)
{
    vec_t d;
    const plane_t *plane;

    while (!node->contents) {
	plane = &map.planes[node->planenum];
	d = DotProduct(plane->normal, point) - plane->dist;
	node = (d > 0) ? node->children[0] : node->children[1];
    }

    return node;
}

/*
===========
PlaceOccupant
===========
*/
static bool
PlaceOccupant(int num, const vec3_t point, node_t *headnode)
{
    node_t *n;

    n = PointInLeaf(headnode, point);
    if (n->contents == CONTENTS_SOLID)
	return false;
    n->occupied = num;
    return true;
}


static void
WriteLeakNode(node_t *n)
{
    portal_t *p;
    int side;
    int i;
    int count = 0;

    if (!n)
	Error(errNoLeakNode);

    count = 0;

    for (p = n->portals; p;) {
	side = (p->nodes[0] == n);
	if ((p->nodes[side]->contents != CONTENTS_SOLID) &&
	    (p->nodes[side]->contents != CONTENTS_SKY))
	    count++;
	p = p->next[!side];
    }

    if (options.fBspleak)
	fprintf(PorFile, "%i\n", count);

    for (p = n->portals; p;) {
	side = (p->nodes[0] == n);
	if ((p->nodes[side]->contents != CONTENTS_SOLID) &&
	    (p->nodes[side]->contents != CONTENTS_SKY)) {
	    if (options.fBspleak) {
		fprintf(PorFile, "%i ", p->winding->numpoints);
		for (i = 0; i < p->winding->numpoints; i++)
		    fprintf(PorFile, "%f %f %f ", p->winding->points[i][0],
			    p->winding->points[i][1],
			    p->winding->points[i][2]);
		fprintf(PorFile, "\n");
	    }
	}
	p = p->next[!side];
    }
}


/*
===============
PrintLeakTrail
===============
*/
static void
PrintLeakTrail(vec3_t p1, vec3_t p2)
{
    vec3_t dir;
    vec_t len;

    VectorSubtract(p2, p1, dir);
    len = VectorNormalize(dir);

    while (len > options.dxLeakDist) {
	fprintf(LeakFile, "%f %f %f\n", p1[0], p1[1], p1[2]);
	VectorMA(p1, options.dxLeakDist, dir, p1);
	len -= options.dxLeakDist;
    }
}

/*
==============
MarkLeakTrail
==============
*/
__attribute__((noinline))
static void
MarkLeakTrail(bspleak_t *bspleak, const fillparms_t *parms, portal_t *n2)
{
    int i;
    vec3_t p1, p2;
    portal_t *n1;
    vec_t *v;

    if (bspleak->numleaks > parms->numportals)
	Error(errLowLeakCount);

    bspleak->leaks[bspleak->numleaks++] = n2;

    MidpointWinding(n2->winding, p1);

    if (options.fBspleak) {
	/* Write the header if needed */
	if (!bspleak->header) {
	    v = map.entities[bspleak->entity].origin;
	    fprintf(PorFile, "%f %f %f\n", v[0], v[1], v[2]);
	    WriteLeakNode(leakNode);
	    bspleak->header = true;
	}

	/* Write the portal center and winding */
	fprintf(PorFile, "%f %f %f ", p1[0], p1[1], p1[2]);
	fprintf(PorFile, "%i ", n2->winding->numpoints);
	for (i = 0; i < n2->winding->numpoints; i++)
	    fprintf(PorFile, "%f %f %f ", n2->winding->points[i][0],
		    n2->winding->points[i][1], n2->winding->points[i][2]);
	fprintf(PorFile, "\n");
	bspleak->numportals++;
    }

    if (bspleak->numleaks < 2 || !options.fOldleak)
	return;

    n1 = bspleak->leaks[bspleak->numleaks - 2];
    MidpointWinding(n1->winding, p2);
    PrintLeakTrail(p1, p2);
}

static vec3_t v1, v2;

/*
=================
LineIntersect_r

Returns true if the line segment v1, v2 does not intersect any of the faces
in the node, false if it does.
=================
*/
static bool
LineIntersect_r(node_t *n)
{
    // Process this node's faces if leaf node
    if (n->contents) {
	face_t *f, **fp;
	vec_t dist1, dist2;
	vec3_t dir;
	vec3_t mid, mins, maxs;
	plane_t *p;
	int i, j;

	for (fp = n->markfaces; *fp; fp++) {
	    for (f = *fp; f; f = f->original) {
		p = &map.planes[f->planenum];
		dist1 = DotProduct(v1, p->normal) - p->dist;
		dist2 = DotProduct(v2, p->normal) - p->dist;

		// Line segment doesn't cross the plane
		if (dist1 < -ON_EPSILON && dist2 < -ON_EPSILON)
		    continue;
		if (dist1 > ON_EPSILON && dist2 > ON_EPSILON)
		    continue;

		if (fabs(dist1) < ON_EPSILON) {
		    if (fabs(dist2) < ON_EPSILON)
			return false; /* too short/close */
		    VectorCopy(v1, mid);
		} else if (fabs(dist2) < ON_EPSILON) {
		    VectorCopy(v2, mid);
		} else {
		    // Find the midpoint on the plane of the face
		    VectorSubtract(v2, v1, dir);
		    VectorMA(v1, dist1 / (dist1 - dist2), dir, mid);
		}

		// Do test here for point in polygon (face)
		// Quick hack
		mins[0] = mins[1] = mins[2] = VECT_MAX;
		maxs[0] = maxs[1] = maxs[2] = -VECT_MAX;
		for (i = 0; i < f->w.numpoints; i++)
		    for (j = 0; j < 3; j++) {
			if (f->w.points[i][j] < mins[j])
			    mins[j] = f->w.points[i][j];
			if (f->w.points[i][j] > maxs[j])
			    maxs[j] = f->w.points[i][j];
		    }

		if (mid[0] < mins[0] - ON_EPSILON ||
		    mid[1] < mins[1] - ON_EPSILON ||
		    mid[2] < mins[2] - ON_EPSILON ||
		    mid[0] > maxs[0] + ON_EPSILON ||
		    mid[1] > maxs[1] + ON_EPSILON ||
		    mid[2] > maxs[2] + ON_EPSILON)
		    continue;

		return false;
	    }
	}
    } else {
	const plane_t *p = &map.planes[n->planenum];
	const vec_t dist1 = DotProduct(v1, p->normal) - p->dist;
	const vec_t dist2 = DotProduct(v2, p->normal) - p->dist;

	if (dist1 < -ON_EPSILON && dist2 < -ON_EPSILON)
	    return LineIntersect_r(n->children[1]);
	if (dist1 > ON_EPSILON && dist2 > ON_EPSILON)
	    return LineIntersect_r(n->children[0]);
	if (!LineIntersect_r(n->children[0]))
	    return false;
	if (!LineIntersect_r(n->children[1]))
	    return false;
    }

    return true;
}


/*
=================
SimplifyLeakline
=================
*/
static void
SimplifyLeakline(const bspleak_t *bspleak, node_t *headnode)
{
    int i, j;
    const portal_t *p1, *p2;

    if (bspleak->numleaks < 2)
	return;

    i = 0;
    p1 = bspleak->leaks[i];

    while (i < bspleak->numleaks - 1) {
	MidpointWinding(p1->winding, v1);
	j = bspleak->numleaks - 1;
	while (j > i + 1) {
	    p2 = bspleak->leaks[j];
	    MidpointWinding(p2->winding, v2);
	    if (LineIntersect_r(headnode))
		break;
	    else
		j--;
	}

	p2 = bspleak->leaks[j];
	MidpointWinding(p2->winding, v2);
	PrintLeakTrail(v1, v2);

	i = j;
	p1 = bspleak->leaks[i];
    }
}


/*
==================
RecursiveFillOutside

If fill is false, just check, don't fill
Returns true if an occupied leaf is reached
==================
*/
static bool
RecursiveFillOutside(fillstate_t *state, const fillparms_t *parms, node_t *node)
{
    portal_t *p;
    int side;
    bool leak;

    if (node->contents == CONTENTS_SOLID || node->contents == CONTENTS_SKY)
	return false;

    if (node->fillmark == parms->fillmark)
	return false;

    if (node->occupied) {
	state->bspleak.entity = node->occupied;
	leakNode = node;
	state->backdraw = 4000;
	return true;
    }

    node->fillmark = parms->fillmark;

    // fill it and it's neighbors
    if (parms->fill) {
	node->contents = CONTENTS_SOLID;
	state->outleafs++;
    }

    for (p = node->portals; p; p = p->next[!side]) {
	side = (p->nodes[0] == node);
	leak = RecursiveFillOutside(state, parms, p->nodes[side]);
	if (leak) {
	    if (map.leakfile || !state->backdraw)
		return true;
	    state->backdraw--;
	    MarkLeakTrail(&state->bspleak, parms, p);
	    return true;
	}
    }

    return false;
}

/*
==================
ClearOutFaces

==================
*/
static void
ClearOutFaces(node_t *node)
{
    face_t **fp;

    if (node->planenum != -1) {
	ClearOutFaces(node->children[0]);
	ClearOutFaces(node->children[1]);
	return;
    }
    if (node->contents != CONTENTS_SOLID)
	return;

    for (fp = node->markfaces; *fp; fp++) {
	// mark all the original faces that are removed
	(*fp)->w.numpoints = 0;
    }
    node->faces = NULL;
}


//=============================================================================

/*
===========
FillOutside

===========
*/
bool
FillOutside(node_t *node, const int hullnum, const int numportals)
{
    int side;
    vec_t *v;
    int i;
    bool inside, leak;
    fillstate_t fillstate;
    fillparms_t fillparms;
    const mapentity_t *entity;
    node_t *fillnode;

    Message(msgProgress, "FillOutside");

    if (options.fNofill) {
	Message(msgStat, "skipped");
	return false;
    }

    inside = false;
    for (i = 1, entity = map.entities + 1; i < map.numentities; i++, entity++) {
	if (!VectorCompare(entity->origin, vec3_origin)) {
	    if (PlaceOccupant(i, entity->origin, node))
		inside = true;
	}
    }

    if (!inside) {
	Message(msgWarning, warnNoFilling, hullnum);
	return false;
    }

    if (!map.leakfile) {
	fillstate.bspleak.leaks =
	    AllocMem(OTHER, sizeof(portal_t *) * numportals, true);
	StripExtension(options.szBSPName);
	strcat(options.szBSPName, ".pts");

	LeakFile = fopen(options.szBSPName, "wt");
	if (LeakFile == NULL)
	    Error(errOpenFailed, options.szBSPName, strerror(errno));

	if (options.fBspleak) {
	    StripExtension(options.szBSPName);
	    strcat(options.szBSPName, ".por");

	    PorFile = fopen(options.szBSPName, "wt");
	    if (PorFile == NULL)
		Error(errOpenFailed, options.szBSPName, strerror(errno));

	    /* ??? "make room for the count" */
	    fprintf(PorFile, "PLACEHOLDER\r\n");
	}
    }

    /* Set up state and parameters for the recursive fill */
    fillparms.numportals = numportals;
    fillstate.outleafs = 0;
    fillstate.backdraw = 0;
    fillstate.bspleak.header = false;
    fillstate.bspleak.numportals = 0;
    fillstate.bspleak.entity = 0;
    fillstate.bspleak.numleaks = 0;

    /* first check to see if an occupied leaf is hit */
    fillparms.fill = false;
    fillparms.fillmark = ++map.fillmark;
    side = !(outside_node.portals->nodes[1] == &outside_node);
    fillnode = outside_node.portals->nodes[side];

    leak = RecursiveFillOutside(&fillstate, &fillparms, fillnode);
    if (leak) {
	v = map.entities[fillstate.bspleak.entity].origin;
	Message(msgWarning, warnMapLeak, v[0], v[1], v[2]);
	if (map.leakfile)
	    return false;

	if (!options.fOldleak)
	    SimplifyLeakline(&fillstate.bspleak, node);

	// heh slight little kludge thing
	StripExtension(options.szBSPName);
	Message(msgLiteral, "Leak file written to %s.pts\n", options.szBSPName);
	fclose(LeakFile);

	if (options.fBspleak) {
	    Message(msgLiteral, "BSP portal file written to %s.por\n",
		    options.szBSPName);
	    fseek(PorFile, 0, SEEK_SET);
	    fprintf(PorFile, "%11i", fillstate.bspleak.numportals);
	    fclose(PorFile);
	}
	map.leakfile = true;

	// Get rid of .prt file if .pts file is generated
	strcat(options.szBSPName, ".prt");
	remove(options.szBSPName);

	return false;
    }

    if (!map.leakfile) {
	FreeMem(fillstate.bspleak.leaks, OTHER,	sizeof(portal_t *) * numportals);
	fclose(LeakFile);

	// Get rid of 0-byte .pts file
	StripExtension(options.szBSPName);
	strcat(options.szBSPName, ".pts");
	remove(options.szBSPName);

	if (options.fBspleak) {
	    fseek(PorFile, 0, SEEK_SET);
	    fprintf(PorFile, "%11i", fillstate.bspleak.numportals);
	    fclose(PorFile);
	}
    }

    /* now go back and fill things in */
    fillparms.fill = true;
    fillparms.fillmark = ++map.fillmark;
    fillnode = outside_node.portals->nodes[side];
    RecursiveFillOutside(&fillstate, &fillparms, fillnode);

    /* remove faces from filled in leafs */
    ClearOutFaces(node);

    Message(msgStat, "%4i outleafs", fillstate.outleafs);
    return true;
}
