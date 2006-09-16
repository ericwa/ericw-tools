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
// divide.h

#include "qbsp.h"

static hashvert_t *pHashverts;
static int iNodes;

/*
a surface has all of the faces that could be drawn on a given plane
the outside filling stage can remove some of them so a better bsp can be generated
*/

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

    // special (non-surface cached) faces don't need subdivision
    tex = &pWorldEnt->pTexinfo[f->texturenum];

    if (tex->flags & TEX_SPECIAL)
	return;

    for (axis = 0; axis < 2; axis++) {
	while (1) {
	    mins = 9999;
	    maxs = -9999;

	    tmp[0] = tex->vecs[axis][0];
	    tmp[1] = tex->vecs[axis][1];
	    tmp[2] = tex->vecs[axis][2];

	    for (i = 0; i < f->numpoints; i++) {
		v = DotProduct(f->pts[i], tmp);
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
		Message(msgError, errNoPolygonSplit);
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
GatherNodeFaces_r(node_t *node)
{
    face_t *f, *next;

    if (node->planenum != PLANENUM_LEAF) {
	// decision node
	for (f = node->faces; f; f = next) {
	    next = f->next;
	    if (!f->numpoints) {	// face was removed outside
		FreeMem(f, FACE, 1);
	    } else {
		f->next = validfaces[f->planenum];
		validfaces[f->planenum] = f;
	    }
	}

	GatherNodeFaces_r(node->children[0]);
	GatherNodeFaces_r(node->children[1]);

	FreeMem(node, NODE, 1);
    } else {
	// leaf node
	FreeMem(node, NODE, 1);
    }
}

/*
================
GatherNodeFaces
================
*/
surface_t *
GatherNodeFaces(node_t *headnode)
{
    memset(validfaces, 0, sizeof(face_t *) * cPlanes);
    GatherNodeFaces_r(headnode);
    return BuildSurfaces();
}

//===========================================================================

#define	POINT_EPSILON	0.01

static hashvert_t *hvert_p;

// This is a kludge.   Should be pEdgeFaces[2].
static face_t **pEdgeFaces0;
static face_t **pEdgeFaces1;
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
GetVertex(vec3_t in)
{
    int h;
    int i;
    hashvert_t *hv;
    vec3_t vert;

    for (i = 0; i < 3; i++) {
	if (fabs(in[i] - Q_rint(in[i])) < 0.001)
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

    hv = hvert_p;
    hv->numedges = 1;
    hv->next = hashverts[h];
    hashverts[h] = hv;
    VectorCopy(vert, hv->point);
    hv->num = map.cTotal[BSPVERTEX];
    hvert_p++;

    // emit a vertex
    pCurEnt->pVertices[pCurEnt->iVertices].point[0] = vert[0];
    pCurEnt->pVertices[pCurEnt->iVertices].point[1] = vert[1];
    pCurEnt->pVertices[pCurEnt->iVertices].point[2] = vert[2];
    pCurEnt->iVertices++;
    map.cTotal[BSPVERTEX]++;

    if (pCurEnt->iVertices > pCurEnt->cVertices)
	Message(msgError, errLowVertexCount);

    return hv->num;
}

//===========================================================================

/*
==================
GetEdge

Don't allow four way edges
==================
*/
static int c_tryedges;

static int
GetEdge(vec3_t p1, vec3_t p2, face_t *f)
{
    int v1, v2;
    dedge_t *edge;
    int i;

    if (!f->contents[0])
	Message(msgError, errZeroContents);

    c_tryedges++;
    v1 = GetVertex(p1);
    v2 = GetVertex(p2);
    for (i = 0; i < pCurEnt->iEdges; i++) {
	edge = pCurEnt->pEdges + i;
	if (v1 == edge->v[1] && v2 == edge->v[0]
	    && pEdgeFaces1[i] == NULL
	    && pEdgeFaces0[i]->contents[0] == f->contents[0]) {
	    pEdgeFaces1[i] = f;
	    return -(i + cStartEdge);
	}
    }

    // emit an edge
    if (pCurEnt->iEdges >= pCurEnt->cEdges)
	Message(msgError, errLowEdgeCount);

    edge = pCurEnt->pEdges + pCurEnt->iEdges;
    pCurEnt->iEdges++;
    map.cTotal[BSPEDGE]++;
    edge->v[0] = v1;
    edge->v[1] = v2;
    pEdgeFaces0[i] = f;
    return i + cStartEdge;
}


/*
==================
FindFaceEdges
==================
*/
static void
FindFaceEdges(face_t *face)
{
    int i;

    face->outputnumber = -1;
    if (face->numpoints > MAXEDGES)
	Message(msgError, errLowFacePointCount);

    face->edges = AllocMem(OTHER, face->numpoints * sizeof(int), true);
    for (i = 0; i < face->numpoints; i++)
	face->edges[i] = GetEdge
	    (face->pts[i], face->pts[(i + 1) % face->numpoints], face);
}


/*
================
MakeFaceEdges_r
================
*/
static void
MakeFaceEdges_r(node_t *node)
{
    face_t *f;

    if (node->planenum == PLANENUM_LEAF)
	return;

    for (f = node->faces; f; f = f->next)
	FindFaceEdges(f);

    // Print progress
    iNodes++;
    Message(msgPercent, iNodes, splitnodes);

    MakeFaceEdges_r(node->children[0]);
    MakeFaceEdges_r(node->children[1]);
}

/*
==============
GrowNodeRegion_r
==============
*/
static void
GrowNodeRegion_r(node_t *node)
{
    dface_t *r;
    face_t *f;
    int i;

    if (node->planenum == PLANENUM_LEAF)
	return;

    node->firstface = map.cTotal[BSPFACE];

    for (f = node->faces; f; f = f->next) {
//              if (f->outputnumber != -1)
//                      continue;       // allready grown into an earlier region

	// emit a region
	f->outputnumber = map.cTotal[BSPFACE];
	r = pCurEnt->pFaces + pCurEnt->iFaces;

	r->planenum = node->outputplanenum;
	r->side = f->planeside;
	r->texinfo = f->texturenum;
	for (i = 0; i < MAXLIGHTMAPS; i++)
	    r->styles[i] = 255;
	r->lightofs = -1;

	r->firstedge = map.cTotal[BSPSURFEDGE];
	for (i = 0; i < f->numpoints; i++) {
	    pCurEnt->pSurfedges[pCurEnt->iSurfedges] = f->edges[i];
	    pCurEnt->iSurfedges++;
	    map.cTotal[BSPSURFEDGE]++;
	}
	FreeMem(f->edges, OTHER, f->numpoints * sizeof(int));

	r->numedges = map.cTotal[BSPSURFEDGE] - r->firstedge;

	map.cTotal[BSPFACE]++;
	pCurEnt->iFaces++;
    }

    node->numfaces = map.cTotal[BSPFACE] - node->firstface;

    GrowNodeRegion_r(node->children[0]);
    GrowNodeRegion_r(node->children[1]);
}

/*
==============
CountData_r
==============
*/
static void
CountData_r(node_t *node)
{
    face_t *f;

    if (node->planenum == PLANENUM_LEAF)
	return;

    for (f = node->faces; f; f = f->next) {
	pCurEnt->cFaces++;
	pCurEnt->cVertices += f->numpoints;
    }

    CountData_r(node->children[0]);
    CountData_r(node->children[1]);
}


/*
================
MakeFaceEdges
================
*/
void
MakeFaceEdges(node_t *headnode)
{
    int i;
    void *pTemp;

    Message(msgProgress, "MakeFaceEdges");

    cStartEdge = 0;
    for (i = 0; i < map.iEntities; i++)
	cStartEdge += map.rgEntities[i].cEdges;

    CountData_r(headnode);

    // Guess: less than half vertices actually are unique.  Add one to round up odd
    // values.  Remember edges are +1 in BeginBSPFile.
    pCurEnt->cSurfedges = pCurEnt->cVertices;
    pCurEnt->cVertices++;
    pCurEnt->cVertices /= 2;
//      pCurEnt->cEdges = pCurEnt->cVertices;
    pCurEnt->cEdges += pCurEnt->cSurfedges;

    pCurEnt->pVertices = AllocMem(BSPVERTEX, pCurEnt->cVertices, true);
    pCurEnt->pEdges = AllocMem(BSPEDGE, pCurEnt->cEdges, true);

    // Accessory data
    pHashverts = AllocMem(HASHVERT, pCurEnt->cVertices, true);
    pEdgeFaces0 = AllocMem(OTHER, sizeof(face_t *) * pCurEnt->cEdges, true);
    pEdgeFaces1 = AllocMem(OTHER, sizeof(face_t *) * pCurEnt->cEdges, true);

    InitHash();
    c_tryedges = 0;
    iNodes = 0;

    MakeFaceEdges_r(headnode);

    FreeMem(pHashverts, HASHVERT, pCurEnt->cVertices);
    FreeMem(pEdgeFaces0, OTHER, sizeof(face_t *) * pCurEnt->cEdges);
    FreeMem(pEdgeFaces1, OTHER, sizeof(face_t *) * pCurEnt->cEdges);

    // Swap these...
    if (pCurEnt->iVertices < pCurEnt->cVertices) {
	pTemp = AllocMem(BSPVERTEX, pCurEnt->iVertices, true);
	memcpy(pTemp, pCurEnt->pVertices,
	       rgcMemSize[BSPVERTEX] * pCurEnt->iVertices);
	FreeMem(pCurEnt->pVertices, BSPVERTEX, pCurEnt->cVertices);
	pCurEnt->pVertices = (dvertex_t *)pTemp;
	pCurEnt->cVertices = pCurEnt->iVertices;
    }
    if (pCurEnt->iEdges < pCurEnt->cEdges) {
	pTemp = AllocMem(BSPEDGE, pCurEnt->iEdges, true);
	memcpy(pTemp, pCurEnt->pEdges, rgcMemSize[BSPEDGE] * pCurEnt->iEdges);
	FreeMem(pCurEnt->pEdges, BSPEDGE, pCurEnt->cEdges);
	pCurEnt->pEdges = (dedge_t *)pTemp;
	pCurEnt->cEdges = pCurEnt->iEdges;
    }

    pCurEnt->pSurfedges = AllocMem(BSPSURFEDGE, pCurEnt->cSurfedges, true);
    pCurEnt->pFaces = AllocMem(BSPFACE, pCurEnt->cFaces, true);

    Message(msgProgress, "GrowRegions");
    GrowNodeRegion_r(headnode);
}
