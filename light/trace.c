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
    vec3_t normal;
    vec_t dist;
    int type;
    int children[2];
    const dplane_t *plane;
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
MakeTnodes_r(int nodenum, const bsp2_t *bsp)
{
    tnode_t *tnode;
    int i;
    bsp2_dnode_t *node;

    tnode = tnode_p++;

    node = bsp->dnodes + nodenum;
    tnode->plane = bsp->dplanes + node->planenum;

    tnode->type = tnode->plane->type;
    VectorCopy(tnode->plane->normal, tnode->normal);
    tnode->dist = tnode->plane->dist;

    for (i = 0; i < 2; i++) {
	if (node->children[i] < 0) {
	    tnode->children[i] = bsp->dleafs[-node->children[i] - 1].contents;
	} else {
	    tnode->children[i] = tnode_p - tnodes;
	    MakeTnodes_r(node->children[i], bsp);
	}
    }
}

void
MakeTnodes(const bsp2_t *bsp)
{
    int i;

    tnode_p = tnodes = malloc(bsp->numnodes * sizeof(tnode_t));
    for (i = 0; i < bsp->nummodels; i++)
	MakeTnodes_r(bsp->dmodels[i].headnode[0], bsp);
}

/*
 * ============================================================================
 * LINE TRACING
 * The major lighting operation is a point to point visibility test, performed
 * by recursive subdivision of the line by the BSP tree.
 * ============================================================================
 */

typedef struct {
    vec3_t back;
    vec3_t front;
    int node;
    int side;
    const dplane_t *plane;
} tracestack_t;

/*
 * ==============
 * TraceLine
 * ==============
 */
#define MAX_TSTACK 128
int
TraceLine(const dmodel_t *model, const int traceflags,
	  const vec3_t start, const vec3_t stop, tracepoint_t *hitpoint)
{
    int node, side, tracehit;
    vec3_t front, back;
    vec_t frontdist, backdist;
    tracestack_t tracestack[MAX_TSTACK];
    tracestack_t *tstack, *crossnode;
    tnode_t *tnode;
    const tracestack_t *const tstack_max = tracestack + MAX_TSTACK;

    if (traceflags <= 0)
	Error("Internal error: %s - bad traceflags (%d)",
	      __func__, traceflags);

    VectorCopy(start, front);
    VectorCopy(stop, back);

    tstack = tracestack;
    node = model->headnode[0];
    crossnode = NULL;
    tracehit = TRACE_HIT_NONE;

    while (1) {
	while (node < 0) {
	    switch (node) {
	    case CONTENTS_SOLID:
		if (traceflags & TRACE_HIT_SOLID)
		    tracehit = TRACE_HIT_SOLID;
		break;
	    case CONTENTS_WATER:
		if (traceflags & TRACE_HIT_WATER)
		    tracehit = TRACE_HIT_WATER;
		break;
	    case CONTENTS_SLIME:
		if (traceflags & TRACE_HIT_SLIME)
		    tracehit = TRACE_HIT_SLIME;
		break;
	    case CONTENTS_LAVA:
		if (traceflags & TRACE_HIT_LAVA)
		    tracehit = TRACE_HIT_LAVA;
		break;
	    case CONTENTS_SKY:
		if (traceflags & TRACE_HIT_SKY)
		    tracehit = TRACE_HIT_SKY;
		break;
	    default:
		break;
	    }
	    if (tracehit != TRACE_HIT_NONE) {
		/* If we haven't crossed, start was inside flagged contents */
		if (!crossnode)
		    return -tracehit;
		if (hitpoint) {
		    hitpoint->dplane = crossnode->plane;
		    hitpoint->side = crossnode->side;
		    VectorCopy(crossnode->back, hitpoint->point);
		}
		return tracehit;
	    }

	    /* If the stack is empty, no obstructions were hit */
	    if (tstack == tracestack)
		return TRACE_HIT_NONE;

	    /* Pop the stack and go down the back side */
	    crossnode = --tstack;
	    VectorCopy(tstack->front, front);
	    VectorCopy(tstack->back, back);
	    node = tnodes[tstack->node].children[!tstack->side];
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

	if (frontdist > ON_EPSILON && backdist > ON_EPSILON) {
	    node = tnode->children[0];
	    continue;
	}
	if (frontdist < -ON_EPSILON && backdist < -ON_EPSILON) {
	    node = tnode->children[1];
	    continue;
	}

	if (frontdist >= -ON_EPSILON && frontdist <= ON_EPSILON) {
	    if (backdist >= -ON_EPSILON && backdist <= ON_EPSILON) {
		/* Front and back on-node, go down both sides */
		if (tstack == tstack_max)
		    Error("%s: tstack overflow\n", __func__);
		tstack->node = node;
		tstack->side = 0;
		tstack->plane = tnode->plane;
		VectorCopy(front, tstack->front);
		VectorCopy(back, tstack->back);
		crossnode = tstack++;
		node = tnode->children[0];
		continue;
	    }

	    /* If only front is on-node, go down the side containing back */
	    side = back < 0;
	    node = tnode->children[side];
	    continue;
	}

	if (backdist >= -ON_EPSILON && backdist <= ON_EPSILON) {
	    /* If only back is on-node, record a cross point but continue */
	    if (tstack == tstack_max)
		Error("%s: tstack overflow\n", __func__);
	    side = frontdist < 0;
	    tstack->node = node;
	    tstack->side = side;
	    tstack->plane = tnode->plane;
	    VectorCopy(front, tstack->front);
	    VectorCopy(back, tstack->back);
	    crossnode = tstack;
	    node = tnode->children[side];
	    continue;
	}

	/*
	 * If we get here, we have a clean split with front and back on
	 * opposite sides. The new back is the intersection point with the
	 * node plane. Push the other segment onto the stack and continue.
	 */
	side = frontdist < 0;
	tstack->node = node;
	tstack->side = side;
	tstack->plane = tnode->plane;
	VectorCopy(back, tstack->back);
	VectorSubtract(back, front, back);
	VectorMA(front, frontdist / (frontdist - backdist), back, back);
	VectorCopy(back, tstack->front);
	crossnode = tstack++;
	node = tnode->children[side];
    }
}

qboolean
TestLight(const vec3_t start, const vec3_t stop, const dmodel_t *self)
{
    const dmodel_t *const *model;
    const int traceflags = TRACE_HIT_SOLID;
    int result = TRACE_HIT_NONE;

    /* Check against the list of global shadow casters */
    for (model = tracelist; *model; model++) {
	result = TraceLine(*model, traceflags, start, stop, NULL);
	if (result != TRACE_HIT_NONE)
	    break;
    }

    /* If not yet obscured, check against the self-shadow model */
    if (result == TRACE_HIT_NONE && self)
	result = TraceLine(self, traceflags, start, stop, NULL);

    return (result == TRACE_HIT_NONE);
}

qboolean
TestSky(const vec3_t start, const vec3_t dirn, const dmodel_t *self)
{
    const dmodel_t *const *model;
    int traceflags = TRACE_HIT_SKY | TRACE_HIT_SOLID;
    int result = TRACE_HIT_NONE;
    vec3_t stop;
    tracepoint_t hit;

    /* Trace towards the sunlight for a sky brush */
    VectorAdd(dirn, start, stop);
    result = TraceLine(tracelist[0], traceflags, start, stop, &hit);
    if (result != TRACE_HIT_SKY)
	return false;

    /* If good, check it isn't shadowed by another model */
    traceflags = TRACE_HIT_SOLID;
    for (model = tracelist + 1; *model; model++) {
	result = TraceLine(*model, traceflags, start, hit.point, NULL);
	if (result != TRACE_HIT_NONE)
	    return false;
    }

    /* Check for self-shadowing */
    if (self) {
	result = TraceLine(self, traceflags, start, hit.point, NULL);
	if (result != TRACE_HIT_NONE)
	    return false;
    }

    return true;
}
