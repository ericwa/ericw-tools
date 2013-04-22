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

static void
ExportNodePlanes_r(node_t *node, int *planemap)
{
    struct lumpdata *planes = &pWorldEnt->lumps[BSPPLANE];
    plane_t *plane;
    dplane_t *dplane;
    int i;

    if (node->planenum == -1)
	return;
    if (planemap[node->planenum] == -1) {
	plane = &map.planes[node->planenum];
	dplane = (dplane_t *)planes->data;

	// search for an equivalent plane
	for (i = 0; i < planes->index; i++, dplane++) {
	    vec3_t normal;
	    normal[0] = dplane->normal[0];
	    normal[1] = dplane->normal[1];
	    normal[2] = dplane->normal[2];
	    if (DotProduct(normal, plane->normal) > 1 - 0.00001 &&
		fabs(dplane->dist - plane->dist) < 0.01 &&
		dplane->type == plane->type)
		break;
	}

	// a new plane
	planemap[node->planenum] = i;

	if (i == planes->index) {
	    if (planes->index >= planes->count)
		Error_("Internal error: plane count mismatch (%s)", __func__);
	    plane = &map.planes[node->planenum];
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

    node->outputplanenum = planemap[node->planenum];

    ExportNodePlanes_r(node->children[0], planemap);
    ExportNodePlanes_r(node->children[1], planemap);
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
    int *planemap;

    // OK just need one plane array, stick it in worldmodel
    if (!planes->data) {
	// I'd like to use map.numplanes here but we haven't seen every entity yet...
	planes->count = map.maxplanes;
	planes->data = AllocMem(BSPPLANE, planes->count, true);
    }
    // TODO: make one-time allocation?
    planemap = AllocMem(OTHER, sizeof(int) * planes->count, true);
    memset(planemap, -1, sizeof(int) * planes->count);
    ExportNodePlanes_r(nodes, planemap);
    FreeMem(planemap, OTHER, sizeof(int) * planes->count);
}

//===========================================================================


/*
==================
CountClipNodes_r
==================
*/
static void
CountClipNodes_r(mapentity_t *entity, node_t *node)
{
    if (node->planenum == -1)
	return;

    entity->lumps[BSPCLIPNODE].count++;

    CountClipNodes_r(entity, node->children[0]);
    CountClipNodes_r(entity, node->children[1]);
}

/*
==================
ExportClipNodes_r
==================
*/
static int
ExportClipNodes_r(mapentity_t *entity, node_t *node)
{
    int i, nodenum;
    dclipnode_t *clipnode;
    face_t *face, *next;
    struct lumpdata *clipnodes = &entity->lumps[BSPCLIPNODE];

    // FIXME: free more stuff?
    if (node->planenum == -1) {
	int contents = node->contents;
	FreeMem(node, NODE, 1);
	return contents;
    }

    /* emit a clipnode */
    clipnode = (dclipnode_t *)clipnodes->data + clipnodes->index;
    clipnodes->index++;
    nodenum = map.cTotal[BSPCLIPNODE];
    map.cTotal[BSPCLIPNODE]++;

    clipnode->planenum = node->outputplanenum;
    for (i = 0; i < 2; i++)
	clipnode->children[i] = ExportClipNodes_r(entity, node->children[i]);

    for (face = node->faces; face; face = next) {
	next = face->next;
	memset(face, 0, sizeof(face_t));
	FreeMem(face, FACE, 1);
    }
    FreeMem(node, NODE, 1);

    return nodenum;
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
ExportClipNodes(mapentity_t *entity, node_t *nodes, const int hullnum)
{
    int oldcount, i, diff;
    int clipcount = 0;
    dclipnode_t *olddata, *clipnode;
    struct lumpdata *clipnodes = &entity->lumps[BSPCLIPNODE];
    dmodel_t *model = (dmodel_t *)entity->lumps[BSPMODEL].data;

    oldcount = clipnodes->count;

    /* Count nodes before this one */
    for (i = 0; i < entity - map.entities; i++)
	clipcount += map.entities[i].lumps[BSPCLIPNODE].count;
    model->headnode[hullnum] = clipcount + oldcount;

    CountClipNodes_r(entity, nodes);
    if (clipnodes->count > MAX_BSP_CLIPNODES)
	Error_("Clipnode count exceeds bsp29 max (%d > %d)",
	       clipnodes->count, MAX_BSP_CLIPNODES);

    olddata = clipnodes->data;
    clipnodes->data = AllocMem(BSPCLIPNODE, clipnodes->count, true);
    if (olddata) {
	memcpy(clipnodes->data, olddata, oldcount * rgcMemSize[BSPCLIPNODE]);
	FreeMem(olddata, BSPCLIPNODE, oldcount);

	/* Worth special-casing for entity 0 (no modification needed) */
	diff = clipcount - model->headnode[1];
	if (diff != 0) {
	    model->headnode[1] += diff;
	    clipnode = clipnodes->data;
	    for (i = 0; i < oldcount; i++, clipnode++) {
		if (clipnode->children[0] < MAX_BSP_CLIPNODES)
		    clipnode->children[0] += diff;
		if (clipnode->children[1] < MAX_BSP_CLIPNODES)
		    clipnode->children[1] += diff;
	    }
	}
    }

    map.cTotal[BSPCLIPNODE] = clipcount + oldcount;
    ExportClipNodes_r(entity, nodes);
}

//===========================================================================


/*
==================
CountLeaves
==================
*/
static void
CountLeaves(mapentity_t *entity, node_t *node)
{
    face_t **markfaces, *face;
    const texinfo_t *texinfo = pWorldEnt->lumps[BSPTEXINFO].data;

    entity->lumps[BSPLEAF].count++;
    for (markfaces = node->markfaces; *markfaces; markfaces++) {
	if (texinfo[(*markfaces)->texinfo].flags & TEX_SKIP)
	    continue;
	for (face = *markfaces; face; face = face->original)
	    entity->lumps[BSPMARKSURF].count++;
    }
}

/*
==================
CountNodes_r
==================
*/
static void
CountNodes_r(mapentity_t *entity, node_t *node)
{
    int i;

    entity->lumps[BSPNODE].count++;

    for (i = 0; i < 2; i++) {
	if (node->children[i]->planenum == -1) {
	    if (node->children[i]->contents != CONTENTS_SOLID)
		CountLeaves(entity, node->children[i]);
	} else
	    CountNodes_r(entity, node->children[i]);
    }
}

/*
==================
CountNodes
==================
*/
static void
CountNodes(mapentity_t *entity, node_t *headnode)
{
    if (headnode->contents < 0)
	CountLeaves(entity, headnode);
    else
	CountNodes_r(entity, headnode);
}

/*
==================
ExportLeaf
==================
*/
static void
ExportLeaf(mapentity_t *entity, node_t *node)
{
    const texinfo_t *texinfo = pWorldEnt->lumps[BSPTEXINFO].data;
    struct lumpdata *leaves = &entity->lumps[BSPLEAF];
    struct lumpdata *marksurfs = &entity->lumps[BSPMARKSURF];
    unsigned short *marksurfnums = marksurfs->data;
    face_t **markfaces, *face;
    dleaf_t *dleaf;

    // ptr arithmetic to get correct leaf in memory
    dleaf = (dleaf_t *)leaves->data + leaves->index;
    leaves->index++;
    map.cTotal[BSPLEAF]++;

    dleaf->contents = node->contents;

    /*
     * write bounding box info
     * (VectorCopy doesn't work since dest are shorts)
     */
    dleaf->mins[0] = (short)node->mins[0];
    dleaf->mins[1] = (short)node->mins[1];
    dleaf->mins[2] = (short)node->mins[2];
    dleaf->maxs[0] = (short)node->maxs[0];
    dleaf->maxs[1] = (short)node->maxs[1];
    dleaf->maxs[2] = (short)node->maxs[2];

    dleaf->visofs = -1;	// no vis info yet

    // write the marksurfaces
    dleaf->firstmarksurface = map.cTotal[BSPMARKSURF];

    for (markfaces = node->markfaces; *markfaces; markfaces++) {
	face = *markfaces;
	if (texinfo[face->texinfo].flags & TEX_SKIP)
	    continue;

	/* emit a marksurface */
	do {
	    marksurfnums[marksurfs->index] = face->outputnumber;
	    marksurfs->index++;
	    map.cTotal[BSPMARKSURF]++;
	    face = face->original;	/* grab tjunction split faces */
	} while (face);
    }
    dleaf->nummarksurfaces = map.cTotal[BSPMARKSURF] - dleaf->firstmarksurface;
}


/*
==================
ExportDrawNodes_r
==================
*/
static void
ExportDrawNodes_r(mapentity_t *entity, node_t *node)
{
    struct lumpdata *nodes = &entity->lumps[BSPNODE];
    dnode_t *dnode;
    int i;

    dnode = (dnode_t *)nodes->data + nodes->index;
    nodes->index++;
    map.cTotal[BSPNODE]++;

    // VectorCopy doesn't work since dest are shorts
    dnode->mins[0] = (short)node->mins[0];
    dnode->mins[1] = (short)node->mins[1];
    dnode->mins[2] = (short)node->mins[2];
    dnode->maxs[0] = (short)node->maxs[0];
    dnode->maxs[1] = (short)node->maxs[1];
    dnode->maxs[2] = (short)node->maxs[2];

    dnode->planenum = node->outputplanenum;
    dnode->firstface = node->firstface;
    dnode->numfaces = node->numfaces;

    // recursively output the other nodes
    for (i = 0; i < 2; i++) {
	if (node->children[i]->planenum == -1) {
	    if (node->children[i]->contents == CONTENTS_SOLID)
		dnode->children[i] = -1;
	    else {
		dnode->children[i] = -(map.cTotal[BSPLEAF] + 1);
		ExportLeaf(entity, node->children[i]);
	    }
	} else {
	    dnode->children[i] = map.cTotal[BSPNODE];
	    ExportDrawNodes_r(entity, node->children[i]);
	}
    }
}

/*
==================
ExportDrawNodes
==================
*/
void
ExportDrawNodes(mapentity_t *entity, node_t *headnode, int firstface)
{
    int i;
    dmodel_t *dmodel;
    struct lumpdata *nodes = &entity->lumps[BSPNODE];
    struct lumpdata *leaves = &entity->lumps[BSPLEAF];
    struct lumpdata *marksurfs = &entity->lumps[BSPMARKSURF];

    // Get a feel for how many of these things there are.
    CountNodes(entity, headnode);

    // emit a model
    nodes->data = AllocMem(BSPNODE, nodes->count, true);
    leaves->data = AllocMem(BSPLEAF, leaves->count, true);
    marksurfs->data = AllocMem(BSPMARKSURF, marksurfs->count, true);

    /*
     * Set leaf 0 properly (must be solid). cLeaves etc incremented in
     * BeginBSPFile.
     */
    ((dleaf_t *)pWorldEnt->lumps[BSPLEAF].data)->contents = CONTENTS_SOLID;

    dmodel = (dmodel_t *)entity->lumps[BSPMODEL].data;
    dmodel->headnode[0] = map.cTotal[BSPNODE];
    dmodel->firstface = firstface;
    dmodel->numfaces = map.cTotal[BSPFACE] - firstface;

    if (headnode->contents < 0)
	ExportLeaf(entity, headnode);
    else
	ExportDrawNodes_r(entity, headnode);

    /* Not counting initial vis leaf */
    dmodel->visleafs = leaves->count;
    if (entity == pWorldEnt)
	dmodel->visleafs--;

    /* remove the headnode padding */
    for (i = 0; i < 3; i++) {
	dmodel->mins[i] = headnode->mins[i] + SIDESPACE + 1;
	dmodel->maxs[i] = headnode->maxs[i] - SIDESPACE - 1;
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
 * Remove any extra texinfo flags we added that are not normally written
 * Standard quake utils only ever write the TEX_SPECIAL flag.
 */
static void
CleanBSPTexinfoFlags(void)
{
    texinfo_t *texinfo = pWorldEnt->lumps[BSPTEXINFO].data;
    const int num_texinfo = pWorldEnt->lumps[BSPTEXINFO].index;
    int i;

    for (i = 0; i < num_texinfo; i++, texinfo++)
	texinfo->flags &= TEX_SPECIAL;
}

/*
==================
FinishBSPFile
==================
*/
void
FinishBSPFile(void)
{
    struct lumpdata *planes = &pWorldEnt->lumps[BSPPLANE];
    dplane_t *newdata;

    options.fVerbose = true;
    Message(msgProgress, "WriteBSPFile");

    // TODO: Fix this somewhere else?
    newdata = AllocMem(BSPPLANE, map.cTotal[BSPPLANE], true);
    memcpy(newdata, planes->data, map.cTotal[BSPPLANE] * rgcMemSize[BSPPLANE]);
    FreeMem(planes->data, BSPPLANE, planes->count);
    planes->data = newdata;
    planes->count = map.cTotal[BSPPLANE];

    PrintBSPFileSizes();
    CleanBSPTexinfoFlags();
    WriteBSPFile();

    options.fVerbose = options.fAllverbose;
}
