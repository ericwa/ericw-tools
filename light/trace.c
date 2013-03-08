/*  Copyright (C) 1996-1997  Id Software, Inc.

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

#include <light/light.h>

typedef struct tnode_s {
    int type;
    vec3_t normal;
    vec_t dist;
    int children[2];
    int pad;
} tnode_t;

static tnode_t *tnodes;
static tnode_t *tnode_p;

/*
 * ==============
 * MakeTnodes
 * Converts the disk node structure into the efficient tracing structure
 * ==============
 */
static void
MakeTnodes_r(int nodenum)
{
    tnode_t *t;
    dplane_t *plane;
    int i;
    dnode_t *node;

    t = tnode_p++;

    node = dnodes + nodenum;
    plane = dplanes + node->planenum;

    t->type = plane->type;
    VectorCopy(plane->normal, t->normal);
    t->dist = plane->dist;

    for (i = 0; i < 2; i++) {
	if (node->children[i] < 0) {
	    t->children[i] = dleafs[-node->children[i] - 1].contents;
	} else {
	    t->children[i] = tnode_p - tnodes;
	    MakeTnodes_r(node->children[i]);
	}
    }
}

void
MakeTnodes(void)
{
    int i;

    tnode_p = tnodes = malloc(numnodes * sizeof(tnode_t));
    for (i = 0; i < nummodels; i++)
	MakeTnodes_r(dmodels[i].headnode[0]);
}

/*
 * ============================================================================
 * LINE TRACING
 * The major lighting operation is a point to point visibility test, performed
 * by recursive subdivision of the line by the BSP tree.
 * ============================================================================
 */

typedef struct {
    vec3_t backpt;
    int side;
    int node;
} tracestack_t;

/*
 * ==============
 * TestLineOrSky
 * ==============
 */
#define MAX_TSTACK 128
static qboolean
TestLineOrSky(const dmodel_t *model, const vec3_t start, const vec3_t stop,
	      qboolean skytest, vec3_t skypoint)
{
    int node, side;
    vec3_t front, back;
    vec_t frontdist, backdist;
    tracestack_t tracestack[MAX_TSTACK];
    tracestack_t *tstack;
    tnode_t *tnode;
    const tracestack_t *const tstack_max = tracestack + MAX_TSTACK;

    VectorCopy(start, front);
    VectorCopy(stop, back);

    tstack = tracestack;
    node = model->headnode[0];

    while (1) {
	while (node < 0 && node != CONTENTS_SOLID) {
	    if (skytest && node == CONTENTS_SKY)
		break;

	    /* If the stack is empty, not obstructions were hit */
	    if (tstack == tracestack)
		return !skytest;

	    /*
	     * pop the stack, set the hit point for this plane and
	     * go down the back side
	     */
	    tstack--;
	    VectorCopy(back, front);
	    VectorCopy(tstack->backpt, back);
	    node = tnodes[tstack->node].children[!tstack->side];
	}

	if (node == CONTENTS_SOLID)
	    return false;
	if (node == CONTENTS_SKY && skytest) {
	    VectorCopy(front, skypoint);
	    return true;
	}

	tnode = &tnodes[node];

	switch (tnode->type) {
	case PLANE_X:
	    frontdist = front[0] - tnode->dist;
	    backdist = back[0] - tnode->dist;
	    break;
	case PLANE_Y:
	    frontdist = front[1] - tnode->dist;
	    backdist = back[1] - tnode->dist;
	    break;
	case PLANE_Z:
	    frontdist = front[2] - tnode->dist;
	    backdist = back[2] - tnode->dist;
	    break;
	default:
	    frontdist = DotProduct(front, tnode->normal) - tnode->dist;
	    backdist = DotProduct(back, tnode->normal) - tnode->dist;
	    break;
	}

	if (frontdist > -ON_EPSILON && backdist > -ON_EPSILON) {
	    node = tnode->children[0];
	    continue;
	}
	if (frontdist < ON_EPSILON && backdist < ON_EPSILON) {
	    node = tnode->children[1];
	    continue;
	}

	if (tstack == tstack_max)
	    Error("%s: tstack overflow\n", __func__);

	side = frontdist < 0.0f ? 1 : 0;
	tstack->node = node;
	tstack->side = side;
	VectorCopy(back, tstack->backpt);
	tstack++;

	/* The new back is the intersection point with the node plane */
	VectorSubtract(back, front, back);
	VectorMA(front, frontdist / (frontdist - backdist), back, back);

	node = tnode->children[side];
    }
}

qboolean
TestLine(const vec3_t start, const vec3_t stop)
{
    const dmodel_t *const *model;

    for (model = tracelist; *model; model++)
	if (!TestLineModel(*model, start, stop))
	    break;

    return !*model;
}

/*
 * Wrapper functions for testing LOS between two points (TestLine)
 * and testing LOS to a sky brush along a direction vector (TestSky)
 */
qboolean
TestLineModel(const dmodel_t *model, const vec3_t start, const vec3_t stop)
{
    return TestLineOrSky(model, start, stop, false, NULL);
}

/*
 * =======
 * TestSky
 * =======
 * Returns true if the ray cast from point 'start' in the
 * direction of vector 'dirn' hits a CONTENTS_SKY node before
 * a CONTENTS_SOLID node.
 */
qboolean
TestSky(const vec3_t start, const vec3_t dirn, vec3_t skypoint)
{
    const dmodel_t *const *model;

    VectorAdd(dirn, start, skypoint);
    if (!TestLineOrSky(tracelist[0], start, skypoint, true, skypoint))
	return false;

    for (model = tracelist + 1; *model; model++)
	if (!TestLineModel(*model, start, skypoint))
	    break;

    return !*model;
}
