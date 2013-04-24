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
    bool header;		/* Flag true once header has been written */
    int backdraw;		/* Limit the length of the leak line */
    int numwritten;		/* Number of portals written to .por file */
    const mapentity_t *entity;	/* Entity that outside filling reached */
    const node_t *node;		/* Node where entity was reached */
    const portal_t **portals;	/* Portals traversed by leak line */
    int numportals;
    int maxportals;
    FILE *ptsfile;
    FILE *porfile;
} leakstate_t;

/*
===========
PointInLeaf
===========
*/
static node_t *
PointInLeaf(node_t *node, const vec3_t point)
{
    vec_t dist;
    const plane_t *plane;

    while (!node->contents) {
	plane = &map.planes[node->planenum];
	dist = DotProduct(plane->normal, point) - plane->dist;
	node = (dist > 0) ? node->children[0] : node->children[1];
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
    node_t *node;

    node = PointInLeaf(headnode, point);
    if (node->contents == CONTENTS_SOLID)
	return false;
    node->occupied = num;
    return true;
}

static FILE *
InitPorFile(void)
{
    FILE *porfile;

    StripExtension(options.szBSPName);
    strcat(options.szBSPName, ".por");
    porfile = fopen(options.szBSPName, "wt");
    if (!porfile)
	Error("Failed to open %s: %s", options.szBSPName, strerror(errno));

    fprintf(porfile, "PLACEHOLDER\r\n");

    return porfile;
}

static FILE *
InitPtsFile(void)
{
    FILE *ptsfile;

    StripExtension(options.szBSPName);
    strcat(options.szBSPName, ".pts");
    ptsfile = fopen(options.szBSPName, "wt");
    if (!ptsfile)
	Error("Failed to open %s: %s", options.szBSPName, strerror(errno));

    return ptsfile;
}

static void
WriteLeakNode(FILE *porfile, const node_t *node)
{
    const portal_t *portal;
    int i, side, count;

    if (!node)
	Error("Internal error: no leak node! (%s)", __func__);

    count = 0;
    for (portal = node->portals; portal; portal = portal->next[!side]) {
	side = (portal->nodes[0] == node);
	if (portal->nodes[side]->contents == CONTENTS_SOLID)
	    continue;
	if (portal->nodes[side]->contents == CONTENTS_SKY)
	    continue;
	count++;
    }

    if (options.fBspleak)
	fprintf(porfile, "%d\n", count);

    for (portal = node->portals; portal; portal = portal->next[!side]) {
	side = (portal->nodes[0] == node);
	if (portal->nodes[side]->contents == CONTENTS_SOLID)
	    continue;
	if (portal->nodes[side]->contents == CONTENTS_SKY)
	    continue;
	if (options.fBspleak) {
	    fprintf(porfile, "%d ", portal->winding->numpoints);
	    for (i = 0; i < portal->winding->numpoints; i++) {
		const vec_t *point = portal->winding->points[i];
		fprintf(porfile, "%f %f %f ", point[0], point[1], point[2]);
	    }
	    fprintf(porfile, "\n");
	}
    }
}


/*
===============
WriteLeakTrail
===============
*/
static void
WriteLeakTrail(FILE *leakfile, const vec3_t point1, const vec3_t point2)
{
    vec3_t vector, trail;
    vec_t dist;

    VectorSubtract(point2, point1, vector);
    dist = VectorNormalize(vector);

    VectorCopy(point1, trail);
    while (dist > options.dxLeakDist) {
	fprintf(leakfile, "%f %f %f\n", trail[0], trail[1], trail[2]);
	VectorMA(trail, options.dxLeakDist, vector, trail);
	dist -= options.dxLeakDist;
    }
}

/*
==============
MarkLeakTrail
==============
*/
__attribute__((noinline))
static void
MarkLeakTrail(leakstate_t *leak, const portal_t *portal2)
{
    int i;
    vec3_t point1, point2;
    const portal_t *portal1;

    if (leak->numportals >= leak->maxportals)
	Error("Internal error: numportals > maxportals (%s)", __func__);

    leak->portals[leak->numportals++] = portal2;

    MidpointWinding(portal2->winding, point1);

    if (options.fBspleak) {
	if (!leak->porfile)
	    leak->porfile = InitPorFile();

	/* Write the header if needed */
	if (!leak->header) {
	    const vec_t *origin = leak->entity->origin;
	    fprintf(leak->porfile, "%f %f %f\n", origin[0], origin[1], origin[2]);
	    WriteLeakNode(leak->porfile, leak->node);
	    leak->header = true;
	}

	/* Write the portal center and winding */
	fprintf(leak->porfile, "%f %f %f ", point1[0], point1[1], point1[2]);
	fprintf(leak->porfile, "%d ", portal2->winding->numpoints);
	for (i = 0; i < portal2->winding->numpoints; i++) {
	    const vec_t *point = portal2->winding->points[i];
	    fprintf(leak->porfile, "%f %f %f ", point[0], point[1], point[2]);
	}
	fprintf(leak->porfile, "\n");
	leak->numwritten++;
    }

    if (leak->numportals < 2 || !options.fOldleak)
	return;

    if (!leak->ptsfile)
	leak->ptsfile = InitPtsFile();

    portal1 = leak->portals[leak->numportals - 2];
    MidpointWinding(portal1->winding, point2);
    WriteLeakTrail(leak->ptsfile, point1, point2);
}

/*
=================
LineIntersect_r

Returns true if the line segment from point1 to point2 does not intersect any
of the faces in the node, false if it does.
=================
*/
static bool
LineIntersect_Leafnode(const node_t *node,
		       const vec3_t point1, const vec3_t point2)
{
    face_t *const *markfaces;
    const face_t *face;

    for (markfaces = node->markfaces; *markfaces; markfaces++) {
	for (face = *markfaces; face; face = face->original) {
	    const plane_t *const plane = &map.planes[face->planenum];
	    const vec_t dist1 = DotProduct(point1, plane->normal) - plane->dist;
	    const vec_t dist2 = DotProduct(point2, plane->normal) - plane->dist;
	    vec3_t mid, mins, maxs;
	    int i, j;

	    // Line segment doesn't cross the plane
	    if (dist1 < -ON_EPSILON && dist2 < -ON_EPSILON)
		continue;
	    if (dist1 > ON_EPSILON && dist2 > ON_EPSILON)
		continue;

	    if (fabs(dist1) < ON_EPSILON) {
		if (fabs(dist2) < ON_EPSILON)
		    return false; /* too short/close */
		VectorCopy(point1, mid);
	    } else if (fabs(dist2) < ON_EPSILON) {
		VectorCopy(point2, mid);
	    } else {
		/* Find the midpoint on the plane of the face */
		vec3_t pointvec;
		VectorSubtract(point2, point1, pointvec);
		VectorMA(point1, dist1 / (dist1 - dist2), pointvec, mid);
	    }

	    // Do test here for point in polygon (face)
	    // Quick hack
	    mins[0] = mins[1] = mins[2] = VECT_MAX;
	    maxs[0] = maxs[1] = maxs[2] = -VECT_MAX;
	    for (i = 0; i < face->w.numpoints; i++)
		for (j = 0; j < 3; j++) {
		    if (face->w.points[i][j] < mins[j])
			mins[j] = face->w.points[i][j];
		    if (face->w.points[i][j] > maxs[j])
			maxs[j] = face->w.points[i][j];
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

    return true;
}

static bool
LineIntersect_r(const node_t *node, const vec3_t point1, const vec3_t point2)
{
    if (node->contents)
	return LineIntersect_Leafnode(node, point1, point2);

    const plane_t *const plane = &map.planes[node->planenum];
    const vec_t dist1 = DotProduct(point1, plane->normal) - plane->dist;
    const vec_t dist2 = DotProduct(point2, plane->normal) - plane->dist;

    if (dist1 < -ON_EPSILON && dist2 < -ON_EPSILON)
	return LineIntersect_r(node->children[1], point1, point2);
    if (dist1 > ON_EPSILON && dist2 > ON_EPSILON)
	return LineIntersect_r(node->children[0], point1, point2);
    if (!LineIntersect_r(node->children[0], point1, point2))
	return false;
    if (!LineIntersect_r(node->children[1], point1, point2))
	return false;

    return true;
}


/*
=================
SimplifyLeakline
=================
*/
static void
SimplifyLeakline(leakstate_t *leak, node_t *headnode)
{
    int i, j;
    const portal_t *portal1, *portal2;
    vec3_t point1, point2;

    if (leak->numportals < 2)
	return;

    i = 0;
    portal1 = leak->portals[i];

    while (i < leak->numportals - 1) {
	MidpointWinding(portal1->winding, point1);
	j = leak->numportals - 1;
	while (j > i + 1) {
	    portal2 = leak->portals[j];
	    MidpointWinding(portal2->winding, point2);
	    if (LineIntersect_r(headnode, point1, point2))
		break;
	    else
		j--;
	}

	if (!leak->ptsfile)
	    leak->ptsfile = InitPtsFile();

	portal2 = leak->portals[j];
	MidpointWinding(portal2->winding, point2);
	WriteLeakTrail(leak->ptsfile, point1, point2);

	i = j;
	portal1 = leak->portals[i];
    }
}

static bool
FindLeaks_r(leakstate_t *leak, const int fillmark, node_t *node)
{
    portal_t *portal;
    int side;
    bool leak_found;

    if (node->contents == CONTENTS_SOLID || node->contents == CONTENTS_SKY)
	return false;
    if (node->fillmark == fillmark)
	return false;

    if (node->occupied) {
	leak->entity = &map.entities[node->occupied];
	leak->node = node;
	leak->backdraw = 4000;
	return true;
    }

    /* Mark this node so we don't visit it again */
    node->fillmark = fillmark;

    for (portal = node->portals; portal; portal = portal->next[!side]) {
	side = (portal->nodes[0] == node);
	leak_found = FindLeaks_r(leak, fillmark, portal->nodes[side]);
	if (leak_found) {
	    /* If we're already written or written too much, bail */
	    if (map.leakfile || !leak->backdraw)
		return true;
	    leak->backdraw--;
	    MarkLeakTrail(leak, portal);
	    return true;
	}
    }

    return false;
}

/*
==================
RecursiveFillOutside
Already assumed here that we have checked for leaks, so just fill
==================
*/
static int
FillOutside_r(node_t *node, const int fillmark, int outleafs)
{
    portal_t *portal;
    int side;

    if (node->contents == CONTENTS_SOLID || node->contents == CONTENTS_SKY)
	return outleafs;
    if (node->fillmark == fillmark)
	return outleafs;

    node->fillmark = fillmark;
    node->contents = CONTENTS_SOLID;
    outleafs++;

    for (portal = node->portals; portal; portal = portal->next[!side]) {
	side = (portal->nodes[0] == node);
	outleafs = FillOutside_r(portal->nodes[side], fillmark, outleafs);
    }

    return outleafs;
}

/*
==================
ClearOutFaces

==================
*/
static void
ClearOutFaces(node_t *node)
{
    face_t **markfaces;

    if (node->planenum != -1) {
	ClearOutFaces(node->children[0]);
	ClearOutFaces(node->children[1]);
	return;
    }
    if (node->contents != CONTENTS_SOLID)
	return;

    for (markfaces = node->markfaces; *markfaces; markfaces++) {
	// mark all the original faces that are removed
	(*markfaces)->w.numpoints = 0;
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
    int i, side, outleafs;
    bool inside, leak_found;
    leakstate_t leak;
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

    /* Set up state for the recursive fill */
    memset(&leak, 0, sizeof(leak));
    if (!map.leakfile) {
	leak.portals = AllocMem(OTHER, sizeof(portal_t *) * numportals, true);
	leak.maxportals = numportals;
    }

    /* first check to see if an occupied leaf is hit */
    side = (outside_node.portals->nodes[0] == &outside_node);
    fillnode = outside_node.portals->nodes[side];
    leak_found = FindLeaks_r(&leak, ++map.fillmark, fillnode);
    if (leak_found) {
	const vec_t *origin = leak.entity->origin;
	Message(msgWarning, warnMapLeak, origin[0], origin[1], origin[2]);
	if (map.leakfile)
	    return false;

	if (!options.fOldleak)
	    SimplifyLeakline(&leak, node);

	StripExtension(options.szBSPName);
	if (leak.ptsfile) {
	    fclose(leak.ptsfile);
	    Message(msgLiteral, "Leak file written to %s.pts\n",
		    options.szBSPName);
	}
	if (options.fBspleak && leak.porfile) {
	    fseek(leak.porfile, 0, SEEK_SET);
	    fprintf(leak.porfile, "%11d", leak.numwritten);
	    fclose(leak.porfile);
	    Message(msgLiteral, "BSP portal file written to %s.por\n",
		    options.szBSPName);
	}
	FreeMem(leak.portals, OTHER, sizeof(portal_t *) * numportals);
	map.leakfile = true;

	/* Get rid of the .prt file since the map has a leak */
	strcat(options.szBSPName, ".prt");
	remove(options.szBSPName);

	return false;
    }
    if (leak.portals) {
	FreeMem(leak.portals, OTHER, sizeof(portal_t *) * numportals);
	leak.portals = NULL;
    }

    /* now go back and fill outside with solid contents */
    fillnode = outside_node.portals->nodes[side];
    outleafs = FillOutside_r(fillnode, ++map.fillmark, 0);

    /* remove faces from filled in leafs */
    ClearOutFaces(node);

    Message(msgStat, "%8d outleafs", outleafs);
    return true;
}
