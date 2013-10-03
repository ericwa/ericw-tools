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

static hashvert_t *pHashverts;

/*
===============
SubdivideFace

If the face is >256 in either texture direction, carve a valid sized
piece off and insert the remainder in the next link
===============
*/
void
SubdivideFace(face_t *f, face_t **prevptr)
{
    vec_t mins, maxs;
    vec_t v;
    int axis, i;
    plane_t plane;
    face_t *front, *back, *next;
    texinfo_t *tex;
    vec3_t tmp;

    /* special (non-surface cached) faces don't need subdivision */
    tex = (texinfo_t *)pWorldEnt->lumps[LUMP_TEXINFO].data + f->texinfo;
    if (tex->flags & (TEX_SPECIAL | TEX_SKIP | TEX_HINT))
	return;

    for (axis = 0; axis < 2; axis++) {
	while (1) {
	    mins = VECT_MAX;
	    maxs = -VECT_MAX;

	    tmp[0] = tex->vecs[axis][0];
	    tmp[1] = tex->vecs[axis][1];
	    tmp[2] = tex->vecs[axis][2];

	    for (i = 0; i < f->w.numpoints; i++) {
		v = DotProduct(f->w.points[i], tmp);
		if (v < mins)
		    mins = v;
		if (v > maxs)
		    maxs = v;
	    }

	    if (maxs - mins <= options.dxSubdivide)
		break;

	    // split it
	    VectorCopy(tmp, plane.normal);
	    v = VectorLength(plane.normal);
	    VectorNormalize(plane.normal);
	    plane.dist = (mins + options.dxSubdivide - 16) / v;
	    next = f->next;
	    SplitFace(f, &plane, &front, &back);
	    if (!front || !back)
		Error("Didn't split the polygon (%s)", __func__);
	    *prevptr = back;
	    back->next = front;
	    front->next = next;
	    f = back;
	}
    }
}


/*
=============================================================================
GatherNodeFaces

Frees the current node tree and returns a new chain of the surfaces that
have inside faces.
=============================================================================
*/

static void
GatherNodeFaces_r(node_t *node, face_t **planefaces)
{
    face_t *f, *next;

    if (node->planenum != PLANENUM_LEAF) {
	// decision node
	for (f = node->faces; f; f = next) {
	    next = f->next;
	    if (!f->w.numpoints) {	// face was removed outside
		FreeMem(f, FACE, 1);
	    } else {
		f->next = planefaces[f->planenum];
		planefaces[f->planenum] = f;
	    }
	}
	GatherNodeFaces_r(node->children[0], planefaces);
	GatherNodeFaces_r(node->children[1], planefaces);
    }
    FreeMem(node, NODE, 1);
}

/*
================
GatherNodeFaces
================
*/
surface_t *
GatherNodeFaces(node_t *headnode)
{
    face_t **planefaces;
    surface_t *surfaces;

    planefaces = AllocMem(OTHER, sizeof(face_t *) * map.maxplanes, true);
    GatherNodeFaces_r(headnode, planefaces);
    surfaces = BuildSurfaces(planefaces);
    FreeMem(planefaces, OTHER, sizeof(face_t *) * map.maxplanes);

    return surfaces;
}

//===========================================================================

static hashvert_t *hvert_p;

// This is a kludge.   Should be pEdgeFaces[2].
static const face_t **pEdgeFaces0;
static const face_t **pEdgeFaces1;
static int cStartEdge;

//============================================================================

#define	NUM_HASH	4096

static hashvert_t *hashverts[NUM_HASH];
static vec3_t hash_min, hash_scale;

static void
InitHash(void)
{
    vec3_t size;
    vec_t volume;
    vec_t scale;
    int newsize[2];
    int i;

    memset(hashverts, 0, sizeof(hashverts));

    for (i = 0; i < 3; i++) {
	hash_min[i] = -8000;
	size[i] = 16000;
    }

    volume = size[0] * size[1];

    scale = sqrt(volume / NUM_HASH);

    newsize[0] = size[0] / scale;
    newsize[1] = size[1] / scale;

    hash_scale[0] = newsize[0] / size[0];
    hash_scale[1] = newsize[1] / size[1];
    hash_scale[2] = (vec_t)newsize[1];

    hvert_p = pHashverts;
}

static unsigned
HashVec(vec3_t vec)
{
    unsigned h;

    h = (unsigned)(hash_scale[0] * (vec[0] - hash_min[0]) * hash_scale[2] +
		   hash_scale[1] * (vec[1] - hash_min[1]));
    if (h >= NUM_HASH)
	return NUM_HASH - 1;
    return h;
}


/*
=============
GetVertex
=============
*/
static int
GetVertex(mapentity_t *entity, const vec3_t in)
{
    int h;
    int i;
    hashvert_t *hv;
    vec3_t vert;
    struct lumpdata *vertices = &entity->lumps[LUMP_VERTEXES];
    dvertex_t *dvertex;

    for (i = 0; i < 3; i++) {
	if (fabs(in[i] - Q_rint(in[i])) < ZERO_EPSILON)
	    vert[i] = Q_rint(in[i]);
	else
	    vert[i] = in[i];
    }

    h = HashVec(vert);
    for (hv = hashverts[h]; hv; hv = hv->next) {
	if (fabs(hv->point[0] - vert[0]) < POINT_EPSILON &&
	    fabs(hv->point[1] - vert[1]) < POINT_EPSILON &&
	    fabs(hv->point[2] - vert[2]) < POINT_EPSILON) {
	    hv->numedges++;
	    return hv->num;
	}
    }

    hv = hvert_p++;
    hv->num = map.cTotal[LUMP_VERTEXES]++;
    hv->numedges = 1;
    hv->next = hashverts[h];
    hashverts[h] = hv;
    VectorCopy(vert, hv->point);

    if (vertices->index == vertices->count)
	Error("Internal error: didn't allocate enough vertices?");

    /* emit a vertex */
    dvertex = (dvertex_t *)vertices->data + vertices->index;
    dvertex->point[0] = vert[0];
    dvertex->point[1] = vert[1];
    dvertex->point[2] = vert[2];
    vertices->index++;

    return hv->num;
}

//===========================================================================

/*
==================
GetEdge

Don't allow four way edges
==================
*/
static int
GetEdge(mapentity_t *entity, const vec3_t p1, const vec3_t p2,
	const face_t *face)
{
    struct lumpdata *edges = &entity->lumps[LUMP_EDGES];
    int v1, v2;
    int i;

    if (!face->contents[0])
	Error("Face with 0 contents (%s)", __func__);

    v1 = GetVertex(entity, p1);
    v2 = GetVertex(entity, p2);

    if (options.BSPVersion == BSPVERSION) {
	bsp29_dedge_t *edge = edges->data;

	for (i = 0; i < edges->index; i++, edge++) {
	    if (v1 == edge->v[1] && v2 == edge->v[0]
		&& pEdgeFaces1[i] == NULL
		&& pEdgeFaces0[i]->contents[0] == face->contents[0]) {
		pEdgeFaces1[i] = face;
		return -(i + cStartEdge);
	    }
	}

	/* emit an edge */
	if (edges->index >= edges->count)
	    Error("Internal error: didn't allocate enough edges?");
	edge->v[0] = v1;
	edge->v[1] = v2;
    } else {
	bsp2_dedge_t *edge = edges->data;

	for (i = 0; i < edges->index; i++, edge++) {
	    if (v1 == edge->v[1] && v2 == edge->v[0]
		&& pEdgeFaces1[i] == NULL
		&& pEdgeFaces0[i]->contents[0] == face->contents[0]) {
		pEdgeFaces1[i] = face;
		return -(i + cStartEdge);
	    }
	}

	/* emit an edge */
	if (edges->index >= edges->count)
	    Error("Internal error: didn't allocate enough edges?");
	edge->v[0] = v1;
	edge->v[1] = v2;
    }

    edges->index++;
    map.cTotal[LUMP_EDGES]++;
    pEdgeFaces0[i] = face;
    return i + cStartEdge;
}


/*
==================
FindFaceEdges
==================
*/
static void
FindFaceEdges(mapentity_t *entity, face_t *face)
{
    int i, memsize;

    face->outputnumber = -1;
    if (face->w.numpoints > MAXEDGES)
	Error("Internal error: face->numpoints > MAXEDGES (%s)", __func__);

    memsize = face->w.numpoints * sizeof(face->edges[0]);
    face->edges = AllocMem(OTHER, memsize, true);
    for (i = 0; i < face->w.numpoints; i++) {
	const vec_t *p1 = face->w.points[i];
	const vec_t *p2 = face->w.points[(i + 1) % face->w.numpoints];
	face->edges[i] = GetEdge(entity, p1, p2, face);
    }
}


/*
================
MakeFaceEdges_r
================
*/
static int
MakeFaceEdges_r(mapentity_t *entity, node_t *node, int progress)
{
    const texinfo_t *texinfo = pWorldEnt->lumps[LUMP_TEXINFO].data;
    face_t *f;

    if (node->planenum == PLANENUM_LEAF)
	return progress;

    for (f = node->faces; f; f = f->next) {
	if (texinfo[f->texinfo].flags & (TEX_SKIP | TEX_HINT))
	    continue;
	FindFaceEdges(entity, f);
    }

    Message(msgPercent, ++progress, splitnodes);
    progress = MakeFaceEdges_r(entity, node->children[0], progress);
    progress = MakeFaceEdges_r(entity, node->children[1], progress);

    return progress;
}

/*
==============
GrowNodeRegion
==============
*/
static void
GrowNodeRegion_BSP29(mapentity_t *entity, node_t *node)
{
    const texinfo_t *texinfo = pWorldEnt->lumps[LUMP_TEXINFO].data;
    struct lumpdata *surfedges = &entity->lumps[LUMP_SURFEDGES];
    struct lumpdata *faces = &entity->lumps[LUMP_FACES];
    bsp29_dface_t *out;
    face_t *face;
    int i;

    if (node->planenum == PLANENUM_LEAF)
	return;

    node->firstface = map.cTotal[LUMP_FACES];

    for (face = node->faces; face; face = face->next) {
	if (texinfo[face->texinfo].flags & (TEX_SKIP | TEX_HINT))
	    continue;

	// emit a region
	face->outputnumber = map.cTotal[LUMP_FACES];
	out = (bsp29_dface_t *)faces->data + faces->index;
	out->planenum = node->outputplanenum;
	out->side = face->planeside;
	out->texinfo = face->texinfo;
	for (i = 0; i < MAXLIGHTMAPS; i++)
	    out->styles[i] = 255;
	out->lightofs = -1;

	out->firstedge = map.cTotal[LUMP_SURFEDGES];
	for (i = 0; i < face->w.numpoints; i++) {
	    ((int *)surfedges->data)[surfedges->index] = face->edges[i];
	    surfedges->index++;
	    map.cTotal[LUMP_SURFEDGES]++;
	}
	FreeMem(face->edges, OTHER, face->w.numpoints * sizeof(int));

	out->numedges = map.cTotal[LUMP_SURFEDGES] - out->firstedge;

	map.cTotal[LUMP_FACES]++;
	faces->index++;
    }

    node->numfaces = map.cTotal[LUMP_FACES] - node->firstface;

    GrowNodeRegion_BSP29(entity, node->children[0]);
    GrowNodeRegion_BSP29(entity, node->children[1]);
}

static void
GrowNodeRegion_BSP2(mapentity_t *entity, node_t *node)
{
    const texinfo_t *texinfo = pWorldEnt->lumps[LUMP_TEXINFO].data;
    struct lumpdata *surfedges = &entity->lumps[LUMP_SURFEDGES];
    struct lumpdata *faces = &entity->lumps[LUMP_FACES];
    bsp2_dface_t *out;
    face_t *face;
    int i;

    if (node->planenum == PLANENUM_LEAF)
	return;

    node->firstface = map.cTotal[LUMP_FACES];

    for (face = node->faces; face; face = face->next) {
	if (texinfo[face->texinfo].flags & (TEX_SKIP | TEX_HINT))
	    continue;

	// emit a region
	face->outputnumber = map.cTotal[LUMP_FACES];
	out = (bsp2_dface_t *)faces->data + faces->index;
	out->planenum = node->outputplanenum;
	out->side = face->planeside;
	out->texinfo = face->texinfo;
	for (i = 0; i < MAXLIGHTMAPS; i++)
	    out->styles[i] = 255;
	out->lightofs = -1;

	out->firstedge = map.cTotal[LUMP_SURFEDGES];
	for (i = 0; i < face->w.numpoints; i++) {
	    ((int *)surfedges->data)[surfedges->index] = face->edges[i];
	    surfedges->index++;
	    map.cTotal[LUMP_SURFEDGES]++;
	}
	FreeMem(face->edges, OTHER, face->w.numpoints * sizeof(int));

	out->numedges = map.cTotal[LUMP_SURFEDGES] - out->firstedge;

	map.cTotal[LUMP_FACES]++;
	faces->index++;
    }

    node->numfaces = map.cTotal[LUMP_FACES] - node->firstface;

    GrowNodeRegion_BSP2(entity, node->children[0]);
    GrowNodeRegion_BSP2(entity, node->children[1]);
}

/*
==============
CountData_r
==============
*/
static void
CountData_r(mapentity_t *entity, node_t *node)
{
    const texinfo_t *texinfo = pWorldEnt->lumps[LUMP_TEXINFO].data;
    face_t *f;

    if (node->planenum == PLANENUM_LEAF)
	return;

    for (f = node->faces; f; f = f->next) {
	if (texinfo[f->texinfo].flags & (TEX_SKIP | TEX_HINT))
	    continue;
	entity->lumps[LUMP_FACES].count++;
	entity->lumps[LUMP_VERTEXES].count += f->w.numpoints;
    }

    CountData_r(entity, node->children[0]);
    CountData_r(entity, node->children[1]);
}


/*
================
MakeFaceEdges
================
*/
int
MakeFaceEdges(mapentity_t *entity, node_t *headnode)
{
    int i, firstface;
    struct lumpdata *surfedges = &entity->lumps[LUMP_SURFEDGES];
    struct lumpdata *edges = &entity->lumps[LUMP_EDGES];
    struct lumpdata *vertices = &entity->lumps[LUMP_VERTEXES];
    struct lumpdata *faces = &entity->lumps[LUMP_FACES];

    Message(msgProgress, "MakeFaceEdges");

    cStartEdge = 0;
    for (i = 0; i < entity - map.entities; i++)
	cStartEdge += map.entities[i].lumps[LUMP_EDGES].count;

    CountData_r(entity, headnode);

    /*
     * Remember edges are +1 in BeginBSPFile.  Often less than half
     * the vertices actually are unique, although heavy use of skip
     * faces will break that assumption.  2/3 should be safe most of
     * the time without wasting too much memory...
     */
    surfedges->count = vertices->count;
    edges->count += surfedges->count;

    vertices->data = AllocMem(BSP_VERTEX, vertices->count, true);
    edges->data = AllocMem(BSP_EDGE, edges->count, true);

    // Accessory data
    pHashverts = AllocMem(HASHVERT, vertices->count, true);
    pEdgeFaces0 = AllocMem(OTHER, sizeof(face_t *) * edges->count, true);
    pEdgeFaces1 = AllocMem(OTHER, sizeof(face_t *) * edges->count, true);

    InitHash();

    firstface = map.cTotal[LUMP_FACES];
    MakeFaceEdges_r(entity, headnode, 0);

    FreeMem(pHashverts, HASHVERT, vertices->count);
    FreeMem(pEdgeFaces0, OTHER, sizeof(face_t *) * edges->count);
    FreeMem(pEdgeFaces1, OTHER, sizeof(face_t *) * edges->count);

    /* Free any excess allocated memory */
    if (vertices->index < vertices->count) {
	dvertex_t *temp = AllocMem(BSP_VERTEX, vertices->index, true);
	memcpy(temp, vertices->data, sizeof(*temp) * vertices->index);
	FreeMem(vertices->data, BSP_VERTEX, vertices->count);
	vertices->data = temp;
	vertices->count = vertices->index;
    }
    if (edges->index < edges->count) {
	void *temp = AllocMem(BSP_EDGE, edges->index, true);
	memcpy(temp, edges->data, MemSize[BSP_EDGE] * edges->index);
	FreeMem(edges->data, BSP_EDGE, edges->count);
	edges->data = temp;
	edges->count = edges->index;
    }

    if (map.cTotal[LUMP_VERTEXES] > 65535 && options.BSPVersion == BSPVERSION)
	Error("Too many vertices (%d > 65535)", map.cTotal[LUMP_VERTEXES]);

    surfedges->data = AllocMem(BSP_SURFEDGE, surfedges->count, true);
    faces->data = AllocMem(BSP_FACE, faces->count, true);

    Message(msgProgress, "GrowRegions");

    if (options.BSPVersion == BSPVERSION)
	GrowNodeRegion_BSP29(entity, headnode);
    else
	GrowNodeRegion_BSP2(entity, headnode);

    return firstface;
}
