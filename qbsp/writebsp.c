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
// writebsp.c

#include "qbsp.h"
#include "wad.h"

static int firstface;
static int *planemapping;

static void
ExportNodePlanes_r(node_t *node)
{
    plane_t *plane;
    dplane_t *dplane;
    int i;
    vec3_t tmp;
    struct lumpdata *planes = &pWorldEnt->lumps[BSPPLANE];

    if (node->planenum == -1)
	return;
    if (planemapping[node->planenum] == -1) {
	plane = &pPlanes[node->planenum];
	dplane = (dplane_t *)planes->data;

	// search for an equivalent plane
	for (i = 0; i < planes->index; i++, dplane++) {
	    tmp[0] = dplane->normal[0];
	    tmp[1] = dplane->normal[1];
	    tmp[2] = dplane->normal[2];
	    if (DotProduct(tmp, plane->normal) > 1 - 0.00001 &&
		fabs(dplane->dist - plane->dist) < 0.01 &&
		dplane->type == plane->type)
		break;
	}

	// a new plane
	planemapping[node->planenum] = i;

	if (i == planes->index) {
	    if (planes->index >= planes->count)
		Message(msgError, errLowPlaneCount);
	    plane = &pPlanes[node->planenum];
	    dplane = (dplane_t *)planes->data + planes->index;
	    dplane->normal[0] = plane->normal[0];
	    dplane->normal[1] = plane->normal[1];
	    dplane->normal[2] = plane->normal[2];
	    dplane->dist = plane->dist;
	    dplane->type = plane->type;

	    planes->index++;
	    map.cTotal[BSPPLANE]++;
	}
    }

    node->outputplanenum = planemapping[node->planenum];

    ExportNodePlanes_r(node->children[0]);
    ExportNodePlanes_r(node->children[1]);
}

/*
==================
ExportNodePlanes
==================
*/
void
ExportNodePlanes(node_t *nodes)
{
    struct lumpdata *planes = &pWorldEnt->lumps[BSPPLANE];

    // OK just need one plane array, stick it in worldmodel
    if (!planes->data) {
	// I'd like to use numbrushplanes here but we haven't seen every entity yet...
	planes->count = cPlanes;
	planes->data = AllocMem(BSPPLANE, planes->count, true);
    }
    // TODO: make one-time allocation?
    planemapping = AllocMem(OTHER, sizeof(int) * planes->count, true);
    memset(planemapping, -1, sizeof(int *) * planes->count);
    ExportNodePlanes_r(nodes);
    FreeMem(planemapping, OTHER, sizeof(int) * planes->count);
}

//===========================================================================


/*
==================
CountClipNodes_r
==================
*/
static void
CountClipNodes_r(node_t *node)
{
    if (node->planenum == -1)
	return;

    pCurEnt->lumps[BSPCLIPNODE].count++;

    CountClipNodes_r(node->children[0]);
    CountClipNodes_r(node->children[1]);
}

/*
==================
ExportClipNodes_r
==================
*/
static int
ExportClipNodes_r(node_t *node)
{
    int i, c;
    dclipnode_t *cn;
    face_t *f, *next;
    struct lumpdata *clipnodes = &pCurEnt->lumps[BSPCLIPNODE];

    // FIXME: free more stuff?
    if (node->planenum == -1) {
	c = node->contents;
	FreeMem(node, NODE, 1);
	return c;
    }
    // emit a clipnode
    c = map.cTotal[BSPCLIPNODE];
    cn = (dclipnode_t *)clipnodes->data + clipnodes->index;
    clipnodes->index++;
    map.cTotal[BSPCLIPNODE]++;

    cn->planenum = node->outputplanenum;
    for (i = 0; i < 2; i++)
	cn->children[i] = ExportClipNodes_r(node->children[i]);

    for (f = node->faces; f; f = next) {
	next = f->next;
	memset(f, 0, sizeof(face_t));
	FreeMem(f, FACE, 1);
    }
    FreeMem(node, NODE, 1);
    return c;
}

/*
==================
ExportClipNodes

Called after the clipping hull is completed.  Generates a disk format
representation and frees the original memory.

This gets real ugly.  Gets called twice per entity, once for each clip hull.
First time just store away data, second time fix up reference points to
accomodate new data interleaved with old.
==================
*/
void
ExportClipNodes(node_t *nodes)
{
    int oldcount, i, diff;
    int clipcount = 0;
    dclipnode_t *pTemp;
    struct lumpdata *clipnodes = &pCurEnt->lumps[BSPCLIPNODE];
    dmodel_t *model = (dmodel_t *)pCurEnt->lumps[BSPMODEL].data;

    oldcount = clipnodes->count;

    /* Count nodes before this one */
    for (i = 0; i < map.iEntities; i++)
	clipcount += map.rgEntities[i].lumps[BSPCLIPNODE].count;
    model->headnode[hullnum] = clipcount + oldcount;

    CountClipNodes_r(nodes);
    if (clipnodes->count > MAX_BSP_CLIPNODES)
	Message(msgError, errTooManyClipnodes);

    pTemp = clipnodes->data;
    clipnodes->data = AllocMem(BSPCLIPNODE, clipnodes->count, true);
    if (pTemp) {
	memcpy(clipnodes->data, pTemp, oldcount * rgcMemSize[BSPCLIPNODE]);
	FreeMem(pTemp, BSPCLIPNODE, oldcount);

	/* Worth special-casing for entity 0 (no modification needed) */
	diff = clipcount - model->headnode[1];
	if (diff != 0) {
	    model->headnode[1] += diff;
	    for (i = 0; i < oldcount; i++) {
		pTemp = (dclipnode_t *)clipnodes->data + i;
		if (pTemp->children[0] < MAX_BSP_CLIPNODES)
		    pTemp->children[0] += diff;
		if (pTemp->children[1] < MAX_BSP_CLIPNODES)
		    pTemp->children[1] += diff;
	    }
	}
    }

    map.cTotal[BSPCLIPNODE] = clipcount + oldcount;
    ExportClipNodes_r(nodes);
}

//===========================================================================


/*
==================
CountLeaves
==================
*/
static void
CountLeaves(node_t *node)
{
    face_t **fp, *f;

    pCurEnt->lumps[BSPLEAF].count++;
    for (fp = node->markfaces; *fp; fp++)
	for (f = *fp; f; f = f->original)
	    pCurEnt->lumps[BSPMARKSURF].count++;
}

/*
==================
CountNodes_r
==================
*/
static void
CountNodes_r(node_t *node)
{
    int i;

    pCurEnt->lumps[BSPNODE].count++;

    for (i = 0; i < 2; i++) {
	if (node->children[i]->planenum == -1) {
	    if (node->children[i]->contents != CONTENTS_SOLID)
		CountLeaves(node->children[i]);
	} else
	    CountNodes_r(node->children[i]);
    }
}

/*
==================
CountNodes
==================
*/
static void
CountNodes(node_t *headnode)
{
    if (headnode->contents < 0)
	CountLeaves(headnode);
    else
	CountNodes_r(headnode);
}

/*
==================
ExportLeaf
==================
*/
static void
ExportLeaf(node_t *node)
{
    face_t **fp, *f;
    dleaf_t *leaf_p;
    struct lumpdata *leaves = &pCurEnt->lumps[BSPLEAF];
    struct lumpdata *marksurfs = &pCurEnt->lumps[BSPMARKSURF];

    // ptr arithmetic to get correct leaf in memory
    leaf_p = (dleaf_t *)leaves->data + leaves->index;
    leaves->index++;
    map.cTotal[BSPLEAF]++;

    leaf_p->contents = node->contents;

//
// write bounding box info
//
    // VectorCopy don't work since dest are shorts
    leaf_p->mins[0] = (short)node->mins[0];
    leaf_p->mins[1] = (short)node->mins[1];
    leaf_p->mins[2] = (short)node->mins[2];
    leaf_p->maxs[0] = (short)node->maxs[0];
    leaf_p->maxs[1] = (short)node->maxs[1];
    leaf_p->maxs[2] = (short)node->maxs[2];

    leaf_p->visofs = -1;	// no vis info yet

    // write the marksurfaces
    leaf_p->firstmarksurface = map.cTotal[BSPMARKSURF];

    for (fp = node->markfaces; *fp; fp++) {
	// emit a marksurface
	f = *fp;
	do {
	    *((unsigned short *)marksurfs->data + marksurfs->index) =
		f->outputnumber;
	    marksurfs->index++;
	    map.cTotal[BSPMARKSURF]++;
	    f = f->original;	// grab tjunction split faces
	} while (f);
    }

    leaf_p->nummarksurfaces =
	map.cTotal[BSPMARKSURF] - leaf_p->firstmarksurface;
}


/*
==================
ExportDrawNodes_r
==================
*/
static void
ExportDrawNodes_r(node_t *node)
{
    dnode_t *n;
    int i;
    struct lumpdata *nodes = &pCurEnt->lumps[BSPNODE];

    // ptr arithmetic to get correct node in memory
    n = (dnode_t *)nodes->data + nodes->index;
    nodes->index++;
    map.cTotal[BSPNODE]++;

    // VectorCopy doesn't work since dest are shorts
    n->mins[0] = (short)node->mins[0];
    n->mins[1] = (short)node->mins[1];
    n->mins[2] = (short)node->mins[2];
    n->maxs[0] = (short)node->maxs[0];
    n->maxs[1] = (short)node->maxs[1];
    n->maxs[2] = (short)node->maxs[2];

    n->planenum = node->outputplanenum;
    n->firstface = node->firstface;
    n->numfaces = node->numfaces;

    // recursively output the other nodes
    for (i = 0; i < 2; i++) {
	if (node->children[i]->planenum == -1) {
	    if (node->children[i]->contents == CONTENTS_SOLID)
		n->children[i] = -1;
	    else {
		n->children[i] = -(map.cTotal[BSPLEAF] + 1);
		ExportLeaf(node->children[i]);
	    }
	} else {
	    n->children[i] = map.cTotal[BSPNODE];
	    ExportDrawNodes_r(node->children[i]);
	}
    }
}

/*
==================
ExportDrawNodes
==================
*/
void
ExportDrawNodes(node_t *headnode)
{
    int i;
    dmodel_t *bm;
    struct lumpdata *nodes = &pCurEnt->lumps[BSPNODE];
    struct lumpdata *leaves = &pCurEnt->lumps[BSPLEAF];
    struct lumpdata *marksurfs = &pCurEnt->lumps[BSPMARKSURF];

    // Get a feel for how many of these things there are.
    CountNodes(headnode);

    // emit a model
    nodes->data = AllocMem(BSPNODE, nodes->count, true);
    leaves->data = AllocMem(BSPLEAF, leaves->count, true);
    marksurfs->data = AllocMem(BSPMARKSURF, marksurfs->count, true);

    /*
     * Set leaf 0 properly (must be solid). cLeaves etc incremented in
     * BeginBSPFile.
     */
    ((dleaf_t *)pWorldEnt->lumps[BSPLEAF].data)->contents = CONTENTS_SOLID;

    bm = (dmodel_t *)pCurEnt->lumps[BSPMODEL].data;
    bm->headnode[0] = map.cTotal[BSPNODE];
    bm->firstface = firstface;
    bm->numfaces = map.cTotal[BSPFACE] - firstface;
    firstface = map.cTotal[BSPFACE];

    if (headnode->contents < 0)
	ExportLeaf(headnode);
    else
	ExportDrawNodes_r(headnode);

    // Not counting initial vis leaf
    bm->visleafs = leaves->count;
    if (map.iEntities == 0)
	bm->visleafs--;

    for (i = 0; i < 3; i++) {
	bm->mins[i] = headnode->mins[i] + SIDESPACE + 1;	// remove the padding
	bm->maxs[i] = headnode->maxs[i] - SIDESPACE - 1;
    }
}

//=============================================================================

/*
==================
BeginBSPFile
==================
*/
void
BeginBSPFile(void)
{
    firstface = 0;

    // First edge must remain unused because 0 can't be negated
    pWorldEnt->lumps[BSPEDGE].count++;
    pWorldEnt->lumps[BSPEDGE].index++;
    map.cTotal[BSPEDGE]++;

    // Leave room for leaf 0 (must be solid)
    pWorldEnt->lumps[BSPLEAF].count++;
    pWorldEnt->lumps[BSPLEAF].index++;
    map.cTotal[BSPLEAF]++;
}


/*
==================
FinishBSPFile
==================
*/
void
FinishBSPFile(void)
{
    dplane_t *pTemp;
    struct lumpdata *planes = &pWorldEnt->lumps[BSPPLANE];

    options.fVerbose = true;
    Message(msgProgress, "WriteBSPFile");

    // TODO: Fix this somewhere else?
    pTemp = AllocMem(BSPPLANE, map.cTotal[BSPPLANE], true);
    memcpy(pTemp, planes->data, map.cTotal[BSPPLANE] * rgcMemSize[BSPPLANE]);
    FreeMem(planes->data, BSPPLANE, planes->count);
    planes->data = pTemp;
    planes->count = map.cTotal[BSPPLANE];

    PrintBSPFileSizes();
    WriteBSPFile();

    options.fVerbose = options.fAllverbose;
}
