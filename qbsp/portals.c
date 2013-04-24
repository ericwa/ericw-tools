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
// portals.c

#include "qbsp.h"

node_t outside_node;	// portals outside the world face this

static int num_visportals;
static int num_visleafs;	// leafs the player can be in
static int num_visclusters;	// clusters of leafs
static int iNodesDone;
static FILE *PortalFile;

/*
==============================================================================

PORTAL FILE GENERATION

==============================================================================
*/

static void PlaneFromWinding(const winding_t *w, plane_t *plane);

static void
WriteFloat(vec_t v)
{
    if (fabs(v - Q_rint(v)) < ZERO_EPSILON)
	fprintf(PortalFile, "%d ", (int)Q_rint(v));
    else
	fprintf(PortalFile, "%f ", v);
}

static int
ClusterContents(const node_t *node)
{
    int contents0, contents1;

    /* Pass the leaf contents up the stack */
    if (node->contents)
	return node->contents;

    contents0 = ClusterContents(node->children[0]);
    contents1 = ClusterContents(node->children[1]);

    if (contents0 == contents1)
	return contents0;

    /*
     * Clusters may be partially solid but still be seen into
     * ?? - Should we do something more explicit with mixed liquid contents?
     */
    if (contents0 == CONTENTS_EMPTY || contents1 == CONTENTS_EMPTY)
	return CONTENTS_EMPTY;

    if (contents0 >= CONTENTS_LAVA && contents0 <= CONTENTS_WATER)
	return contents0;
    if (contents1 >= CONTENTS_LAVA && contents1 <= CONTENTS_WATER)
	return contents1;
    if (contents0 == CONTENTS_SKY || contents1 == CONTENTS_SKY)
	return CONTENTS_SKY;

    return CONTENTS_SOLID;
}

/*
 * Return true if possible to see the through the contents of the portals nodes
 */
static bool
PortalThru(const portal_t *p)
{
    int contents0 = ClusterContents(p->nodes[0]);
    int contents1 = ClusterContents(p->nodes[1]);

    /* Can't see through solids */
    if (contents0 == CONTENTS_SOLID || contents1 == CONTENTS_SOLID)
	return false;

    /* If contents values are the same and not solid, can see through */
    if (contents0 == contents1)
	return true;

    /* If water is transparent, liquids are like empty space */
    if (options.fTranswater) {
	if (contents0 >= CONTENTS_LAVA && contents0 <= CONTENTS_WATER &&
	    contents1 == CONTENTS_EMPTY)
	    return true;
	if (contents1 >= CONTENTS_LAVA && contents1 <= CONTENTS_WATER &&
	    contents0 == CONTENTS_EMPTY)
	    return true;
    }

    /* If sky is transparent, then sky is like empty space */
    if (options.fTranssky) {
	if (contents0 == CONTENTS_SKY && contents1 == CONTENTS_EMPTY)
	    return true;
	if (contents0 == CONTENTS_EMPTY && contents1 == CONTENTS_SKY)
	    return true;
    }

    return false;
}

static void
WritePortals_r(node_t *node, bool clusters)
{
    const portal_t *p, *next;
    const winding_t *w;
    const plane_t *pl;
    int i, front, back;
    plane_t plane2;

    if (!node->contents && !node->detail_separator) {
	WritePortals_r(node->children[0], clusters);
	WritePortals_r(node->children[1], clusters);
	return;
    }
    if (node->contents == CONTENTS_SOLID)
	return;

    for (p = node->portals; p; p = next) {
	next = (p->nodes[0] == node) ? p->next[0] : p->next[1];
	if (!p->winding || p->nodes[0] != node)
	    continue;
	if (!PortalThru(p))
	    continue;

	w = p->winding;
	front = clusters ? p->nodes[0]->viscluster : p->nodes[0]->visleafnum;
	back  = clusters ? p->nodes[1]->viscluster : p->nodes[1]->visleafnum;

	/*
	 * sometimes planes get turned around when they are very near the
	 * changeover point between different axis.  interpret the plane the
	 * same way vis will, and flip the side orders if needed
	 */
	pl = &map.planes[p->planenum];
	PlaneFromWinding(w, &plane2);
	if (DotProduct(pl->normal, plane2.normal) < 1.0 - ANGLEEPSILON)
	    fprintf(PortalFile, "%d %d %d ", w->numpoints, back, front);
	else
	    fprintf(PortalFile, "%d %d %d ", w->numpoints, front, back);

	for (i = 0; i < w->numpoints; i++) {
	    fprintf(PortalFile, "(");
	    WriteFloat(w->points[i][0]);
	    WriteFloat(w->points[i][1]);
	    WriteFloat(w->points[i][2]);
	    fprintf(PortalFile, ") ");
	}
	fprintf(PortalFile, "\n");
    }
}

static int
WriteClusters_r(node_t *node, int viscluster)
{
    if (!node->contents) {
	viscluster = WriteClusters_r(node->children[0], viscluster);
	viscluster = WriteClusters_r(node->children[1], viscluster);
	return viscluster;
    }
    if (node->contents == CONTENTS_SOLID)
	return viscluster;

    /* If we're in the next cluster, start a new line */
    if (node->viscluster != viscluster) {
	fprintf(PortalFile, "-1\n");
	viscluster++;
    }

    /* Sanity check */
    if (node->viscluster != viscluster)
	Error("Internal error: Detail cluster mismatch (%s)", __func__);

    fprintf(PortalFile, "%d ", node->visleafnum);

    return viscluster;
}


/* FIXME - bleh, incrementing global counts */
static void
CountPortals(const node_t *node)
{
    const portal_t *portal;

    for (portal = node->portals; portal;) {
	/* only write out from first leaf */
	if (portal->nodes[0] == node) {
	    if (PortalThru(portal))
		num_visportals++;
	    portal = portal->next[0];
	} else {
	    portal = portal->next[1];
	}
    }

}

/*
================
NumberLeafs_r
- Assigns leaf numbers and cluster numbers
- If cluster < 0, assign next available global cluster number and increment
- Otherwise, assign the given cluster number because parent splitter is detail
================
*/
static void
NumberLeafs_r(node_t *node, int cluster)
{
    /* decision node */
    if (!node->contents) {
	node->visleafnum = -99;
	node->viscluster = -99;
	if (cluster < 0 && node->detail_separator) {
	    cluster = num_visclusters++;
	    node->viscluster = cluster;
	    CountPortals(node);
	}
	NumberLeafs_r(node->children[0], cluster);
	NumberLeafs_r(node->children[1], cluster);
	return;
    }

    if (node->contents == CONTENTS_SOLID) {
	/* solid block, viewpoint never inside */
	node->visleafnum = -1;
	node->viscluster = -1;
	return;
    }

    node->visleafnum = num_visleafs++;
    node->viscluster = (cluster < 0) ? num_visclusters++ : cluster;
    CountPortals(node);
}


/*
================
WritePortalfile
================
*/
static void
WritePortalfile(node_t *headnode)
{
    int check;

    /*
     * Set the visleafnum and viscluster field in every leaf and count the
     * total number of portals.
     */
    num_visleafs = 0;
    num_visclusters = 0;
    num_visportals = 0;
    NumberLeafs_r(headnode, -1);

    // write the file
    StripExtension(options.szBSPName);
    strcat(options.szBSPName, ".prt");

    PortalFile = fopen(options.szBSPName, "wt");
    if (!PortalFile)
	Error("Failed to open %s: %s", options.szBSPName, strerror(errno));

    /* If no detail clusters, just use a normal PRT1 format */
    if (num_visclusters == num_visleafs) {
	fprintf(PortalFile, "PRT1\n");
	fprintf(PortalFile, "%d\n", num_visleafs);
	fprintf(PortalFile, "%d\n", num_visportals);
	WritePortals_r(headnode, false);
    } else {
	fprintf(PortalFile, "PRT2\n");
	fprintf(PortalFile, "%d\n", num_visleafs);
	fprintf(PortalFile, "%d\n", num_visclusters);
	fprintf(PortalFile, "%d\n", num_visportals);
	WritePortals_r(headnode, true);
	check = WriteClusters_r(headnode, 0);
	if (check != num_visclusters - 1)
	    Error("Internal error: Detail cluster mismatch (%s)", __func__);
	fprintf(PortalFile, "-1\n");
    }

    fclose(PortalFile);
}


//=============================================================================

/*
=============
AddPortalToNodes
=============
*/
static void
AddPortalToNodes(portal_t *p, node_t *front, node_t *back)
{
    if (p->nodes[0] || p->nodes[1])
	Error("portal already included (%s)", __func__);

    p->nodes[0] = front;
    p->next[0] = front->portals;
    front->portals = p;

    p->nodes[1] = back;
    p->next[1] = back->portals;
    back->portals = p;
}


/*
=============
RemovePortalFromNode
=============
*/
static void
RemovePortalFromNode(portal_t *portal, node_t *l)
{
    portal_t **pp, *t;

// remove reference to the current portal
    pp = &l->portals;
    while (1) {
	t = *pp;
	if (!t)
	    Error("Portal not in leaf (%s)", __func__);

	if (t == portal)
	    break;

	if (t->nodes[0] == l)
	    pp = &t->next[0];
	else if (t->nodes[1] == l)
	    pp = &t->next[1];
	else
	    Error("Portal not bounding leaf (%s)", __func__);
    }

    if (portal->nodes[0] == l) {
	*pp = portal->next[0];
	portal->nodes[0] = NULL;
    } else if (portal->nodes[1] == l) {
	*pp = portal->next[1];
	portal->nodes[1] = NULL;
    }
}


/*
================
MakeHeadnodePortals

The created portals will face the global outside_node
================
*/
static void
MakeHeadnodePortals(const mapentity_t *entity, node_t *node)
{
    vec3_t bounds[2];
    int i, j, n;
    portal_t *p, *portals[6];
    plane_t bplanes[6], *pl;
    int side;

    // pad with some space so there will never be null volume leafs
    for (i = 0; i < 3; i++) {
	bounds[0][i] = entity->mins[i] - SIDESPACE;
	bounds[1][i] = entity->maxs[i] + SIDESPACE;
    }

    outside_node.contents = CONTENTS_SOLID;
    outside_node.portals = NULL;

    for (i = 0; i < 3; i++)
	for (j = 0; j < 2; j++) {
	    n = j * 3 + i;

	    p = AllocMem(PORTAL, 1, true);
	    portals[n] = p;

	    pl = &bplanes[n];
	    memset(pl, 0, sizeof(*pl));
	    if (j) {
		pl->normal[i] = -1;
		pl->dist = -bounds[j][i];
	    } else {
		pl->normal[i] = 1;
		pl->dist = bounds[j][i];
	    }
	    p->planenum = FindPlane(pl, &side);

	    p->winding = BaseWindingForPlane(pl);
	    if (side)
		AddPortalToNodes(p, &outside_node, node);
	    else
		AddPortalToNodes(p, node, &outside_node);
	}

    // clip the basewindings by all the other planes
    for (i = 0; i < 6; i++) {
	for (j = 0; j < 6; j++) {
	    if (j == i)
		continue;
	    portals[i]->winding =
		ClipWinding(portals[i]->winding, &bplanes[j], true);
	}
    }
}

static void
PlaneFromWinding(const winding_t *w, plane_t *plane)
{
    vec3_t v1, v2;

    // calc plane
    VectorSubtract(w->points[2], w->points[1], v1);
    VectorSubtract(w->points[0], w->points[1], v2);
    CrossProduct(v2, v1, plane->normal);
    VectorNormalize(plane->normal);
    plane->dist = DotProduct(w->points[0], plane->normal);
}

//============================================================================

#ifdef PARANOID

static void
CheckWindingInNode(winding_t *w, node_t *node)
{
    int i, j;

    for (i = 0; i < w->numpoints; i++) {
	for (j = 0; j < 3; j++)
	    if (w->points[i][j] < node->mins[j] - 1
		|| w->points[i][j] > node->maxs[j] + 1) {
		Message(msgWarning, warnWindingOutside);
		return;
	    }
    }
}

static void
CheckWindingArea(winding_t *w)
{
    int i;
    vec_t total, add;
    vec3_t v1, v2, cross;

    total = 0;
    for (i = 1; i < w->numpoints; i++) {
	VectorSubtract(w->points[i], w->points[0], v1);
	VectorSubtract(w->points[i + 1], w->points[0], v2);
	CrossProduct(v1, v2, cross);
	add = VectorLength(cross);
	total += add * 0.5;
    }
    if (total < 16)
	Message(msgWarning, warnLowWindingArea, total);
}


static void
CheckLeafPortalConsistancy(node_t *node)
{
    int side, side2;
    portal_t *p, *p2;
    plane_t plane, plane2;
    int i;
    winding_t *w;
    vec_t dist;

    side = side2 = 0;		// quiet compiler warning

    for (p = node->portals; p; p = p->next[side]) {
	if (p->nodes[0] == node)
	    side = 0;
	else if (p->nodes[1] == node)
	    side = 1;
	else
	    Error("Mislinked portal (%s)", __func__);

	CheckWindingInNode(p->winding, node);
	CheckWindingArea(p->winding);

	// check that the side orders are correct
	plane = map.planes[p->planenum];
	PlaneFromWinding(p->winding, &plane2);

	for (p2 = node->portals; p2; p2 = p2->next[side2]) {
	    if (p2->nodes[0] == node)
		side2 = 0;
	    else if (p2->nodes[1] == node)
		side2 = 1;
	    else
		Error("Mislinked portal (%s)", __func__);

	    w = p2->winding;
	    for (i = 0; i < w->numpoints; i++) {
		dist = DotProduct(w->points[i], plane.normal) - plane.dist;
		if ((side == 0 && dist < -1) || (side == 1 && dist > 1)) {
		    Message(msgWarning, warnBadPortalDirection);
		    return;
		}
	    }
	}
    }
}

#endif /* PARANOID */

//============================================================================

/*
================
CutNodePortals_r
================
*/
static void
CutNodePortals_r(node_t *node)
{
    const plane_t *plane;
    plane_t clipplane;
    node_t *front, *back, *other_node;
    portal_t *portal, *new_portal, *next_portal;
    winding_t *winding, *frontwinding, *backwinding;
    int side;

#ifdef PARANOID
    CheckLeafPortalConsistancy(node);
#endif

    /* If a leaf, no more dividing */
    if (node->contents)
	return;

    /* No portals on detail separators */
    if (node->detail_separator)
	return;

    plane = &map.planes[node->planenum];
    front = node->children[0];
    back = node->children[1];

    /*
     * create the new portal by taking the full plane winding for the cutting
     * plane and clipping it by all of the planes from the other portals
     */
    new_portal = AllocMem(PORTAL, 1, true);
    new_portal->planenum = node->planenum;

    winding = BaseWindingForPlane(plane);
    for (portal = node->portals; portal; portal = portal->next[side]) {
	clipplane = map.planes[portal->planenum];
	if (portal->nodes[0] == node)
	    side = 0;
	else if (portal->nodes[1] == node) {
	    clipplane.dist = -clipplane.dist;
	    VectorSubtract(vec3_origin, clipplane.normal, clipplane.normal);
	    side = 1;
	} else
	    Error("Mislinked portal (%s)", __func__);

	winding = ClipWinding(winding, &clipplane, true);
	if (!winding) {
	    Message(msgWarning, warnPortalClippedAway);
	    break;
	}
    }

    /* If the plane was not clipped on all sides, there was an error */
    if (winding) {
	new_portal->winding = winding;
	AddPortalToNodes(new_portal, front, back);
    }

    /* partition the portals */
    for (portal = node->portals; portal; portal = next_portal) {
	if (portal->nodes[0] == node)
	    side = 0;
	else if (portal->nodes[1] == node)
	    side = 1;
	else
	    Error("Mislinked portal (%s)", __func__);
	next_portal = portal->next[side];

	other_node = portal->nodes[!side];
	RemovePortalFromNode(portal, portal->nodes[0]);
	RemovePortalFromNode(portal, portal->nodes[1]);

	/* cut the portal into two portals, one on each side of the cut plane */
	DivideWinding(portal->winding, plane, &frontwinding, &backwinding);

	if (!frontwinding) {
	    if (side == 0)
		AddPortalToNodes(portal, back, other_node);
	    else
		AddPortalToNodes(portal, other_node, back);
	    continue;
	}
	if (!backwinding) {
	    if (side == 0)
		AddPortalToNodes(portal, front, other_node);
	    else
		AddPortalToNodes(portal, other_node, front);
	    continue;
	}

	/* the winding is split */
	new_portal = AllocMem(PORTAL, 1, true);
	*new_portal = *portal;
	new_portal->winding = backwinding;
	FreeMem(portal->winding, WINDING, 1);
	portal->winding = frontwinding;

	if (side == 0) {
	    AddPortalToNodes(portal, front, other_node);
	    AddPortalToNodes(new_portal, back, other_node);
	} else {
	    AddPortalToNodes(portal, other_node, front);
	    AddPortalToNodes(new_portal, other_node, back);
	}
    }

    /* Display progress */
    iNodesDone++;
    Message(msgPercent, iNodesDone, splitnodes);

    CutNodePortals_r(front);
    CutNodePortals_r(back);
}


/*
==================
PortalizeWorld

Builds the exact polyhedrons for the nodes and leafs
==================
*/
int
PortalizeWorld(const mapentity_t *entity, node_t *headnode, const int hullnum)
{
    Message(msgProgress, "Portalize");

    iNodesDone = 0;

    MakeHeadnodePortals(entity, headnode);
    CutNodePortals_r(headnode);

    if (!hullnum) {
	/* save portal file for vis tracing */
	WritePortalfile(headnode);

	Message(msgStat, "%8d vis leafs", num_visleafs);
	Message(msgStat, "%8d vis clusters", num_visclusters);
	Message(msgStat, "%8d vis portals", num_visportals);
    }

    return num_visportals;
}


/*
==================
FreeAllPortals

==================
*/
void
FreeAllPortals(node_t *node)
{
    portal_t *p, *nextp;

    if (!node->contents) {
	FreeAllPortals(node->children[0]);
	FreeAllPortals(node->children[1]);
    }

    for (p = node->portals; p; p = nextp) {
	if (p->nodes[0] == node)
	    nextp = p->next[0];
	else
	    nextp = p->next[1];
	RemovePortalFromNode(p, p->nodes[0]);
	RemovePortalFromNode(p, p->nodes[1]);
	FreeMem(p->winding, WINDING, 1);
	FreeMem(p, PORTAL, 1);
    }
    node->portals = NULL;
}
