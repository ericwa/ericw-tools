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

static int outleafs;
static int valid;
static int hit_occupied;
static int backdraw;
static int numports;
static bool firstone = true;
static FILE *LeakFile;
static FILE *PorFile;
static node_t *leakNode = NULL;

static int numleaks;
static portal_t **pLeaks;

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
MarkLeakTrail(portal_t *n2, const int hullnum, const int numportals)
{
    int i;
    vec3_t p1, p2;
    portal_t *n1;
    vec_t *v;

    if (hullnum != 2)
	return;

    if (numleaks > numportals)
	Error(errLowLeakCount);

    pLeaks[numleaks] = n2;
    numleaks++;

    MidpointWinding(n2->winding, p1);

    if (options.fBspleak) {
	if (firstone) {
	    firstone = false;
	    v = map.entities[hit_occupied].origin;
	    fprintf(PorFile, "%f %f %f\n", v[0], v[1], v[2]);
	    WriteLeakNode(leakNode);
	}
	numports++;

	// write the center...
	fprintf(PorFile, "%f %f %f ", p1[0], p1[1], p1[2]);
	fprintf(PorFile, "%i ", n2->winding->numpoints);

	for (i = 0; i < n2->winding->numpoints; i++)
	    fprintf(PorFile, "%f %f %f ", n2->winding->points[i][0],
		    n2->winding->points[i][1], n2->winding->points[i][2]);

	fprintf(PorFile, "\n");
    }

    if (numleaks < 2 || !options.fOldleak)
	return;

    n1 = pLeaks[numleaks - 2];
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
SimplifyLeakline(node_t *headnode)
{
    int i, j;
    portal_t *p1, *p2;

    if (numleaks < 2)
	return;

    i = 0;
    p1 = pLeaks[i];

    while (i < numleaks - 1) {
	MidpointWinding(p1->winding, v1);
	j = numleaks - 1;
	while (j > i + 1) {
	    p2 = pLeaks[j];
	    MidpointWinding(p2->winding, v2);
	    if (LineIntersect_r(headnode))
		break;
	    else
		j--;
	}

	p2 = pLeaks[j];
	MidpointWinding(p2->winding, v2);
	PrintLeakTrail(v1, v2);

	i = j;
	p1 = pLeaks[i];
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
RecursiveFillOutside(node_t *node, bool fill, const int hullnum, const int numportals)
{
    portal_t *p;
    int side;

    if (node->contents == CONTENTS_SOLID || node->contents == CONTENTS_SKY)
	return false;

    if (node->valid == valid)
	return false;

    if (node->occupied) {
	hit_occupied = node->occupied;
	leakNode = node;
	backdraw = 4000;
	return true;
    }

    node->valid = valid;

    // fill it and it's neighbors
    if (fill) {
	node->contents = CONTENTS_SOLID;
	outleafs++;
    }

    for (p = node->portals; p; p = p->next[!side]) {
	side = (p->nodes[0] == node);
	if (RecursiveFillOutside(p->nodes[side], fill, hullnum, numportals)) {
	    // leaked, so stop filling
	    if (backdraw-- > 0)
		MarkLeakTrail(p, hullnum, numportals);
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
    bool inside;
    const mapentity_t *ent;
    node_t *fillnode;

    Message(msgProgress, "FillOutside");

    if (options.fNofill) {
	Message(msgStat, "skipped");
	return false;
    }

    inside = false;
    for (i = 1, ent = map.entities + 1; i < map.numentities; i++, ent++) {
	if (!VectorCompare(ent->origin, vec3_origin)) {
	    if (PlaceOccupant(i, ent->origin, node))
		inside = true;
	}
    }

    if (!inside) {
	Message(msgWarning, warnNoFilling, hullnum);
	return false;
    }

    // first check to see if an occupied leaf is hit
    outleafs = 0;
    numleaks = 0;
    valid++;

    if (hullnum == 2) {
	pLeaks = AllocMem(OTHER, sizeof(portal_t *) * numportals, true);
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

    side = !(outside_node.portals->nodes[1] == &outside_node);
    fillnode = outside_node.portals->nodes[side];
    if (RecursiveFillOutside(fillnode, false, hullnum, numportals)) {
	v = map.entities[hit_occupied].origin;
	Message(msgWarning, warnMapLeak, v[0], v[1], v[2]);
	if (hullnum == 2) {
	    if (!options.fOldleak)
		SimplifyLeakline(node);

	    // heh slight little kludge thing
	    StripExtension(options.szBSPName);
	    Message(msgLiteral, "Leak file written to %s.pts\n",
		    options.szBSPName);
	    fclose(LeakFile);

	    // Get rid of .prt file if .pts file is generated
	    strcat(options.szBSPName, ".prt");
	    remove(options.szBSPName);

	    if (options.fBspleak) {
		Message(msgLiteral, "BSP portal file written to %s.por\n",
			options.szBSPName);
		fseek(PorFile, 0, SEEK_SET);
		fprintf(PorFile, "%11i", numports);
		fclose(PorFile);
	    }
	}
	return false;
    }

    if (hullnum == 2) {
	FreeMem(pLeaks, OTHER, sizeof(portal_t *) * numportals);
	fclose(LeakFile);

	// Get rid of 0-byte .pts file
	StripExtension(options.szBSPName);
	strcat(options.szBSPName, ".pts");
	remove(options.szBSPName);

	if (options.fBspleak) {
	    fseek(PorFile, 0, SEEK_SET);
	    fprintf(PorFile, "%11i", numports);
	    fclose(PorFile);
	}
    }
    // now go back and fill things in
    valid++;
    fillnode = outside_node.portals->nodes[side];
    RecursiveFillOutside(fillnode, true, hullnum, numportals);

    // remove faces from filled in leafs
    ClearOutFaces(node);

    Message(msgStat, "%4i outleafs", outleafs);
    return true;
}
