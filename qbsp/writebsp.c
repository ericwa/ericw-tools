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
    struct lumpdata *planes = &pWorldEnt->lumps[LUMP_PLANES];
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
		Error("Internal error: plane count mismatch (%s)", __func__);
	    plane = &map.planes[node->planenum];
	    dplane = (dplane_t *)planes->data + planes->index;
	    dplane->normal[0] = plane->normal[0];
	    dplane->normal[1] = plane->normal[1];
	    dplane->normal[2] = plane->normal[2];
	    dplane->dist = plane->dist;
	    dplane->type = plane->type;

	    planes->index++;
	    map.cTotal[LUMP_PLANES]++;
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
    struct lumpdata *planes = &pWorldEnt->lumps[LUMP_PLANES];
    int *planemap;

    // OK just need one plane array, stick it in worldmodel
    if (!planes->data) {
	// I'd like to use map.numplanes here but we haven't seen every entity yet...
	planes->count = map.maxplanes;
	planes->data = AllocMem(BSP_PLANE, planes->count, true);
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

    entity->lumps[LUMP_CLIPNODES].count++;

    CountClipNodes_r(entity, node->children[0]);
    CountClipNodes_r(entity, node->children[1]);
}

/*
==================
ExportClipNodes
==================
*/
static int
ExportClipNodes_BSP29(mapentity_t *entity, node_t *node)
{
    int nodenum;
    bsp29_dclipnode_t *clipnode;
    face_t *face, *next;
    struct lumpdata *clipnodes = &entity->lumps[LUMP_CLIPNODES];

    // FIXME: free more stuff?
    if (node->planenum == -1) {
	int contents = node->contents;
	FreeMem(node, NODE, 1);
	return contents;
    }

    /* emit a clipnode */
    clipnode = (bsp29_dclipnode_t *)clipnodes->data + clipnodes->index;
    clipnodes->index++;
    nodenum = map.cTotal[LUMP_CLIPNODES];
    map.cTotal[LUMP_CLIPNODES]++;

    clipnode->planenum = node->outputplanenum;
    clipnode->children[0] = ExportClipNodes_BSP29(entity, node->children[0]);
    clipnode->children[1] = ExportClipNodes_BSP29(entity, node->children[1]);

    for (face = node->faces; face; face = next) {
	next = face->next;
	memset(face, 0, sizeof(face_t));
	FreeMem(face, FACE, 1);
    }
    FreeMem(node, NODE, 1);

    return nodenum;
}

static int
ExportClipNodes_BSP2(mapentity_t *entity, node_t *node)
{
    int nodenum;
    bsp2_dclipnode_t *clipnode;
    face_t *face, *next;
    struct lumpdata *clipnodes = &entity->lumps[LUMP_CLIPNODES];

    // FIXME: free more stuff?
    if (node->planenum == -1) {
	int contents = node->contents;
	FreeMem(node, NODE, 1);
	return contents;
    }

    /* emit a clipnode */
    clipnode = (bsp2_dclipnode_t *)clipnodes->data + clipnodes->index;
    clipnodes->index++;
    nodenum = map.cTotal[LUMP_CLIPNODES];
    map.cTotal[LUMP_CLIPNODES]++;

    clipnode->planenum = node->outputplanenum;
    clipnode->children[0] = ExportClipNodes_BSP2(entity, node->children[0]);
    clipnode->children[1] = ExportClipNodes_BSP2(entity, node->children[1]);

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
    void *olddata;
    struct lumpdata *clipnodes = &entity->lumps[LUMP_CLIPNODES];
    dmodel_t *model = (dmodel_t *)entity->lumps[LUMP_MODELS].data;

    oldcount = clipnodes->count;

    /* Count nodes before this one */
    for (i = 0; i < entity - map.entities; i++)
	clipcount += map.entities[i].lumps[LUMP_CLIPNODES].count;
    model->headnode[hullnum] = clipcount + oldcount;

    CountClipNodes_r(entity, nodes);
    if (clipnodes->count > MAX_BSP_CLIPNODES && options.BSPVersion == BSPVERSION)
	Error("Clipnode count exceeds bsp 29 max (%d > %d)",
	      clipnodes->count, MAX_BSP_CLIPNODES);

    olddata = clipnodes->data;
    clipnodes->data = AllocMem(BSP_CLIPNODE, clipnodes->count, true);
    if (olddata) {
	memcpy(clipnodes->data, olddata, oldcount * MemSize[BSP_CLIPNODE]);
	FreeMem(olddata, BSP_CLIPNODE, oldcount);

	/* Worth special-casing for entity 0 (no modification needed) */
	diff = clipcount - model->headnode[1];
	if (diff != 0) {
	    model->headnode[1] += diff;
	    if (options.BSPVersion == BSPVERSION) {
		bsp29_dclipnode_t *clipnode = clipnodes->data;
		for (i = 0; i < oldcount; i++, clipnode++) {
		    if (clipnode->children[0] < MAX_BSP_CLIPNODES)
			clipnode->children[0] += diff;
		    if (clipnode->children[1] < MAX_BSP_CLIPNODES)
			clipnode->children[1] += diff;
		}
	    } else {
		bsp2_dclipnode_t *clipnode = clipnodes->data;
		for (i = 0; i < oldcount; i++, clipnode++) {
		    if (clipnode->children[0] >= 0)
			clipnode->children[0] += diff;
		    if (clipnode->children[1] >= 0)
			clipnode->children[1] += diff;
		}
	    }
	}
    }

    map.cTotal[LUMP_CLIPNODES] = clipcount + oldcount;
    if (options.BSPVersion == BSPVERSION)
	ExportClipNodes_BSP29(entity, nodes);
    else
	ExportClipNodes_BSP2(entity, nodes);
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
    const texinfo_t *texinfo = pWorldEnt->lumps[LUMP_TEXINFO].data;

    entity->lumps[LUMP_LEAFS].count++;
    for (markfaces = node->markfaces; *markfaces; markfaces++) {
	if (texinfo[(*markfaces)->texinfo].flags & TEX_SKIP)
	    continue;
	for (face = *markfaces; face; face = face->original)
	    entity->lumps[LUMP_MARKSURFACES].count++;
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

    entity->lumps[LUMP_NODES].count++;

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
ExportLeaf_BSP29(mapentity_t *entity, node_t *node)
{
    const texinfo_t *texinfo = pWorldEnt->lumps[LUMP_TEXINFO].data;
    struct lumpdata *leaves = &entity->lumps[LUMP_LEAFS];
    struct lumpdata *marksurfs = &entity->lumps[LUMP_MARKSURFACES];
    uint16_t *marksurfnums = marksurfs->data;
    face_t **markfaces, *face;
    bsp29_dleaf_t *dleaf;

    // ptr arithmetic to get correct leaf in memory
    dleaf = (bsp29_dleaf_t *)leaves->data + leaves->index;
    leaves->index++;
    map.cTotal[LUMP_LEAFS]++;

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
    dleaf->firstmarksurface = map.cTotal[LUMP_MARKSURFACES];

    for (markfaces = node->markfaces; *markfaces; markfaces++) {
	face = *markfaces;
	if (texinfo[face->texinfo].flags & TEX_SKIP)
	    continue;

	/* emit a marksurface */
	do {
	    marksurfnums[marksurfs->index] = face->outputnumber;
	    marksurfs->index++;
	    map.cTotal[LUMP_MARKSURFACES]++;
	    face = face->original;	/* grab tjunction split faces */
	} while (face);
    }
    dleaf->nummarksurfaces =
	map.cTotal[LUMP_MARKSURFACES] - dleaf->firstmarksurface;
}

static void
ExportLeaf_BSP2(mapentity_t *entity, node_t *node)
{
    const texinfo_t *texinfo = pWorldEnt->lumps[LUMP_TEXINFO].data;
    struct lumpdata *leaves = &entity->lumps[LUMP_LEAFS];
    struct lumpdata *marksurfs = &entity->lumps[LUMP_MARKSURFACES];
    uint32_t *marksurfnums = marksurfs->data;
    face_t **markfaces, *face;
    bsp2_dleaf_t *dleaf;

    // ptr arithmetic to get correct leaf in memory
    dleaf = (bsp2_dleaf_t *)leaves->data + leaves->index;
    leaves->index++;
    map.cTotal[LUMP_LEAFS]++;

    dleaf->contents = node->contents;

    /*
     * write bounding box info
     * (VectorCopy doesn't work double->float)
     */
    dleaf->mins[0] = node->mins[0];
    dleaf->mins[1] = node->mins[1];
    dleaf->mins[2] = node->mins[2];
    dleaf->maxs[0] = node->maxs[0];
    dleaf->maxs[1] = node->maxs[1];
    dleaf->maxs[2] = node->maxs[2];

    dleaf->visofs = -1;	// no vis info yet

    // write the marksurfaces
    dleaf->firstmarksurface = map.cTotal[LUMP_MARKSURFACES];

    for (markfaces = node->markfaces; *markfaces; markfaces++) {
	face = *markfaces;
	if (texinfo[face->texinfo].flags & TEX_SKIP)
	    continue;

	/* emit a marksurface */
	do {
	    marksurfnums[marksurfs->index] = face->outputnumber;
	    marksurfs->index++;
	    map.cTotal[LUMP_MARKSURFACES]++;
	    face = face->original;	/* grab tjunction split faces */
	} while (face);
    }
    dleaf->nummarksurfaces =
	map.cTotal[LUMP_MARKSURFACES] - dleaf->firstmarksurface;
}

static void
ExportLeaf_BSP2rmq(mapentity_t *entity, node_t *node)
{
    const texinfo_t *texinfo = pWorldEnt->lumps[LUMP_TEXINFO].data;
    struct lumpdata *leaves = &entity->lumps[LUMP_LEAFS];
    struct lumpdata *marksurfs = &entity->lumps[LUMP_MARKSURFACES];
    uint32_t *marksurfnums = marksurfs->data;
    face_t **markfaces, *face;
    bsp2rmq_dleaf_t *dleaf;

    // ptr arithmetic to get correct leaf in memory
    dleaf = (bsp2rmq_dleaf_t *)leaves->data + leaves->index;
    leaves->index++;
    map.cTotal[LUMP_LEAFS]++;

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
    dleaf->firstmarksurface = map.cTotal[LUMP_MARKSURFACES];

    for (markfaces = node->markfaces; *markfaces; markfaces++) {
	face = *markfaces;
	if (texinfo[face->texinfo].flags & TEX_SKIP)
	    continue;

	/* emit a marksurface */
	do {
	    marksurfnums[marksurfs->index] = face->outputnumber;
	    marksurfs->index++;
	    map.cTotal[LUMP_MARKSURFACES]++;
	    face = face->original;	/* grab tjunction split faces */
	} while (face);
    }
    dleaf->nummarksurfaces =
	map.cTotal[LUMP_MARKSURFACES] - dleaf->firstmarksurface;
}

/*
==================
ExportDrawNodes
==================
*/
static void
ExportDrawNodes_BSP29(mapentity_t *entity, node_t *node)
{
    struct lumpdata *nodes = &entity->lumps[LUMP_NODES];
    bsp29_dnode_t *dnode;
    int i;

    dnode = (bsp29_dnode_t *)nodes->data + nodes->index;
    nodes->index++;
    map.cTotal[LUMP_NODES]++;

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
		dnode->children[i] = -(map.cTotal[LUMP_LEAFS] + 1);
		ExportLeaf_BSP29(entity, node->children[i]);
	    }
	} else {
	    dnode->children[i] = map.cTotal[LUMP_NODES];
	    ExportDrawNodes_BSP29(entity, node->children[i]);
	}
    }
}

static void
ExportDrawNodes_BSP2(mapentity_t *entity, node_t *node)
{
    struct lumpdata *nodes = &entity->lumps[LUMP_NODES];
    bsp2_dnode_t *dnode;
    int i;

    dnode = (bsp2_dnode_t *)nodes->data + nodes->index;
    nodes->index++;
    map.cTotal[LUMP_NODES]++;

    // VectorCopy doesn't work double->float
    dnode->mins[0] = node->mins[0];
    dnode->mins[1] = node->mins[1];
    dnode->mins[2] = node->mins[2];
    dnode->maxs[0] = node->maxs[0];
    dnode->maxs[1] = node->maxs[1];
    dnode->maxs[2] = node->maxs[2];

    dnode->planenum = node->outputplanenum;
    dnode->firstface = node->firstface;
    dnode->numfaces = node->numfaces;

    // recursively output the other nodes
    for (i = 0; i < 2; i++) {
	if (node->children[i]->planenum == -1) {
	    if (node->children[i]->contents == CONTENTS_SOLID)
		dnode->children[i] = -1;
	    else {
		dnode->children[i] = -(map.cTotal[LUMP_LEAFS] + 1);
		ExportLeaf_BSP2(entity, node->children[i]);
	    }
	} else {
	    dnode->children[i] = map.cTotal[LUMP_NODES];
	    ExportDrawNodes_BSP2(entity, node->children[i]);
	}
    }
}

static void
ExportDrawNodes_BSP2rmq(mapentity_t *entity, node_t *node)
{
    struct lumpdata *nodes = &entity->lumps[LUMP_NODES];
    bsp2rmq_dnode_t *dnode;
    int i;

    dnode = (bsp2rmq_dnode_t *)nodes->data + nodes->index;
    nodes->index++;
    map.cTotal[LUMP_NODES]++;

    // VectorCopy doesn't work since dest are shorts
    dnode->mins[0] = node->mins[0];
    dnode->mins[1] = node->mins[1];
    dnode->mins[2] = node->mins[2];
    dnode->maxs[0] = node->maxs[0];
    dnode->maxs[1] = node->maxs[1];
    dnode->maxs[2] = node->maxs[2];

    dnode->planenum = node->outputplanenum;
    dnode->firstface = node->firstface;
    dnode->numfaces = node->numfaces;

    // recursively output the other nodes
    for (i = 0; i < 2; i++) {
	if (node->children[i]->planenum == -1) {
	    if (node->children[i]->contents == CONTENTS_SOLID)
		dnode->children[i] = -1;
	    else {
		dnode->children[i] = -(map.cTotal[LUMP_LEAFS] + 1);
		ExportLeaf_BSP2rmq(entity, node->children[i]);
	    }
	} else {
	    dnode->children[i] = map.cTotal[LUMP_NODES];
	    ExportDrawNodes_BSP2rmq(entity, node->children[i]);
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
    struct lumpdata *nodes = &entity->lumps[LUMP_NODES];
    struct lumpdata *leaves = &entity->lumps[LUMP_LEAFS];
    struct lumpdata *marksurfs = &entity->lumps[LUMP_MARKSURFACES];

    // Get a feel for how many of these things there are.
    CountNodes(entity, headnode);

    // emit a model
    nodes->data = AllocMem(BSP_NODE, nodes->count, true);
    leaves->data = AllocMem(BSP_LEAF, leaves->count, true);
    marksurfs->data = AllocMem(BSP_MARKSURF, marksurfs->count, true);

    /*
     * Set leaf 0 properly (must be solid). cLeaves etc incremented in
     * BeginBSPFile.
     */
    if (options.BSPVersion == BSP2VERSION) {
	bsp2_dleaf_t *leaf = pWorldEnt->lumps[LUMP_LEAFS].data;
	leaf->contents = CONTENTS_SOLID;
    } else if (options.BSPVersion == BSP2RMQVERSION) {
	bsp2rmq_dleaf_t *leaf = pWorldEnt->lumps[LUMP_LEAFS].data;
	leaf->contents = CONTENTS_SOLID;
    } else {
	bsp29_dleaf_t *leaf = pWorldEnt->lumps[LUMP_LEAFS].data;
	leaf->contents = CONTENTS_SOLID;
    }

    dmodel = (dmodel_t *)entity->lumps[LUMP_MODELS].data;
    dmodel->headnode[0] = map.cTotal[LUMP_NODES];
    dmodel->firstface = firstface;
    dmodel->numfaces = map.cTotal[LUMP_FACES] - firstface;

    if (options.BSPVersion == BSP2VERSION) {
	if (headnode->contents < 0)
	    ExportLeaf_BSP2(entity, headnode);
	else
	    ExportDrawNodes_BSP2(entity, headnode);
    } else if (options.BSPVersion == BSP2RMQVERSION) {
	if (headnode->contents < 0)
	    ExportLeaf_BSP2rmq(entity, headnode);
	else
	    ExportDrawNodes_BSP2rmq(entity, headnode);
    } else {
	if (headnode->contents < 0)
	    ExportLeaf_BSP29(entity, headnode);
	else
	    ExportDrawNodes_BSP29(entity, headnode);
    }

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
    pWorldEnt->lumps[LUMP_EDGES].count++;
    pWorldEnt->lumps[LUMP_EDGES].index++;
    map.cTotal[LUMP_EDGES]++;

    // Leave room for leaf 0 (must be solid)
    pWorldEnt->lumps[LUMP_LEAFS].count++;
    pWorldEnt->lumps[LUMP_LEAFS].index++;
    map.cTotal[LUMP_LEAFS]++;
}

/*
 * Remove any extra texinfo flags we added that are not normally written
 * Standard quake utils only ever write the TEX_SPECIAL flag.
 */
static void
CleanBSPTexinfoFlags(void)
{
    texinfo_t *texinfo = pWorldEnt->lumps[LUMP_TEXINFO].data;
    const int num_texinfo = pWorldEnt->lumps[LUMP_TEXINFO].index;
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
    struct lumpdata *planes = &pWorldEnt->lumps[LUMP_PLANES];
    dplane_t *newdata;

    options.fVerbose = true;
    Message(msgProgress, "WriteBSPFile");

    // TODO: Fix this somewhere else?
    newdata = AllocMem(BSP_PLANE, map.cTotal[LUMP_PLANES], true);
    memcpy(newdata, planes->data, map.cTotal[LUMP_PLANES] * MemSize[BSP_PLANE]);
    FreeMem(planes->data, BSP_PLANE, planes->count);
    planes->data = newdata;
    planes->count = map.cTotal[LUMP_PLANES];

    PrintBSPFileSizes();
    CleanBSPTexinfoFlags();
    WriteBSPFile();

    options.fVerbose = options.fAllverbose;
}
