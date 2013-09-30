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

#include <common/cmdlib.h>
#include <common/mathlib.h>
#include <common/bspfile.h>

static const char *
BSPVersionString(int32_t version)
{
    static char buffers[2][20];
    static int index;
    char *buffer;

    switch (version) {
    case BSP2RMQVERSION:
	return "BSP2rmq";
    case BSP2VERSION:
	return "BSP2";
    default:
	buffer = buffers[1 & ++index];
	snprintf(buffer, sizeof(buffers[0]), "%d", version);
	return buffer;
    }
}

static qboolean
BSPVersionSupported(int32_t version)
{
    switch (version) {
    case BSPVERSION:
    case BSP2VERSION:
    case BSP2RMQVERSION:
	return true;
    default:
	return false;
    }
}

/*
 * =========================================================================
 * BSP BYTE SWAPPING
 * =========================================================================
 */

typedef enum { TO_DISK, TO_CPU } swaptype_t;

static void
SwapBSPVertexes(int numvertexes, dvertex_t *verticies)
{
    dvertex_t *vertex = verticies;
    int i, j;

    for (i = 0; i < numvertexes; i++, vertex++)
	for (j = 0; j < 3; j++)
	    vertex->point[j] = LittleFloat(vertex->point[j]);
}

static void
SwapBSPPlanes(int numplanes, dplane_t *planes)
{
    dplane_t *plane = planes;
    int i, j;

    for (i = 0; i < numplanes; i++, plane++) {
	for (j = 0; j < 3; j++)
	    plane->normal[j] = LittleFloat(plane->normal[j]);
	plane->dist = LittleFloat(plane->dist);
	plane->type = LittleLong(plane->type);
    }
}

static void
SwapBSPTexinfo(int numtexinfo, texinfo_t *texinfos)
{
    texinfo_t *texinfo = texinfos;
    int i, j;

    for (i = 0; i < numtexinfo; i++, texinfo++) {
	for (j = 0; j < 4; j++) {
	    texinfo->vecs[0][j] = LittleFloat(texinfo->vecs[0][j]);
	    texinfo->vecs[1][j] = LittleFloat(texinfo->vecs[1][j]);
	}
	texinfo->miptex = LittleLong(texinfo->miptex);
	texinfo->flags = LittleLong(texinfo->flags);
    }
}

static void
SwapBSP29Faces(int numfaces, bsp29_dface_t *faces)
{
    bsp29_dface_t *face = faces;
    int i;

    for (i = 0; i < numfaces; i++, face++) {
	face->texinfo = LittleShort(face->texinfo);
	face->planenum = LittleShort(face->planenum);
	face->side = LittleShort(face->side);
	face->lightofs = LittleLong(face->lightofs);
	face->firstedge = LittleLong(face->firstedge);
	face->numedges = LittleShort(face->numedges);
    }
}

static void
SwapBSP2Faces(int numfaces, bsp2_dface_t *faces)
{
    bsp2_dface_t *face = faces;
    int i;

    for (i = 0; i < numfaces; i++, face++) {
	face->texinfo = LittleLong(face->texinfo);
	face->planenum = LittleLong(face->planenum);
	face->side = LittleLong(face->side);
	face->lightofs = LittleLong(face->lightofs);
	face->firstedge = LittleLong(face->firstedge);
	face->numedges = LittleLong(face->numedges);
    }
}

static void
SwapBSP29Nodes(int numnodes, bsp29_dnode_t *nodes)
{
    bsp29_dnode_t *node = nodes;
    int i, j;

    /* nodes */
    for (i = 0; i < numnodes; i++, node++) {
	node->planenum = LittleLong(node->planenum);
	for (j = 0; j < 3; j++) {
	    node->mins[j] = LittleShort(node->mins[j]);
	    node->maxs[j] = LittleShort(node->maxs[j]);
	}
	node->children[0] = LittleShort(node->children[0]);
	node->children[1] = LittleShort(node->children[1]);
	node->firstface = LittleShort(node->firstface);
	node->numfaces = LittleShort(node->numfaces);
    }
}

static void
SwapBSP2rmqNodes(int numnodes, bsp2rmq_dnode_t *nodes)
{
    bsp2rmq_dnode_t *node = nodes;
    int i, j;

    /* nodes */
    for (i = 0; i < numnodes; i++, node++) {
	node->planenum = LittleLong(node->planenum);
	for (j = 0; j < 3; j++) {
	    node->mins[j] = LittleShort(node->mins[j]);
	    node->maxs[j] = LittleShort(node->maxs[j]);
	}
	node->children[0] = LittleLong(node->children[0]);
	node->children[1] = LittleLong(node->children[1]);
	node->firstface = LittleLong(node->firstface);
	node->numfaces = LittleLong(node->numfaces);
    }
}

static void
SwapBSP2Nodes(int numnodes, bsp2_dnode_t *nodes)
{
    bsp2_dnode_t *node = nodes;
    int i, j;

    /* nodes */
    for (i = 0; i < numnodes; i++, node++) {
	node->planenum = LittleLong(node->planenum);
	for (j = 0; j < 3; j++) {
	    node->mins[j] = LittleFloat(node->mins[j]);
	    node->maxs[j] = LittleFloat(node->maxs[j]);
	}
	node->children[0] = LittleLong(node->children[0]);
	node->children[1] = LittleLong(node->children[1]);
	node->firstface = LittleLong(node->firstface);
	node->numfaces = LittleLong(node->numfaces);
    }
}

static void
SwapBSP29Leafs(int numleafs, bsp29_dleaf_t *leafs)
{
    bsp29_dleaf_t *leaf = leafs;
    int i, j;

    for (i = 0; i < numleafs; i++, leaf++) {
	leaf->contents = LittleLong(leaf->contents);
	for (j = 0; j < 3; j++) {
	    leaf->mins[j] = LittleShort(leaf->mins[j]);
	    leaf->maxs[j] = LittleShort(leaf->maxs[j]);
	}
	leaf->firstmarksurface = LittleShort(leaf->firstmarksurface);
	leaf->nummarksurfaces = LittleShort(leaf->nummarksurfaces);
	leaf->visofs = LittleLong(leaf->visofs);
    }
}

static void
SwapBSP2rmqLeafs(int numleafs, bsp2rmq_dleaf_t *leafs)
{
    bsp2rmq_dleaf_t *leaf = leafs;
    int i, j;

    for (i = 0; i < numleafs; i++, leaf++) {
	leaf->contents = LittleLong(leaf->contents);
	for (j = 0; j < 3; j++) {
	    leaf->mins[j] = LittleShort(leaf->mins[j]);
	    leaf->maxs[j] = LittleShort(leaf->maxs[j]);
	}
	leaf->firstmarksurface = LittleLong(leaf->firstmarksurface);
	leaf->nummarksurfaces = LittleLong(leaf->nummarksurfaces);
	leaf->visofs = LittleLong(leaf->visofs);
    }
}

static void
SwapBSP2Leafs(int numleafs, bsp2_dleaf_t *leafs)
{
    bsp2_dleaf_t *leaf = leafs;
    int i, j;

    for (i = 0; i < numleafs; i++, leaf++) {
	leaf->contents = LittleLong(leaf->contents);
	for (j = 0; j < 3; j++) {
	    leaf->mins[j] = LittleFloat(leaf->mins[j]);
	    leaf->maxs[j] = LittleFloat(leaf->maxs[j]);
	}
	leaf->firstmarksurface = LittleLong(leaf->firstmarksurface);
	leaf->nummarksurfaces = LittleLong(leaf->nummarksurfaces);
	leaf->visofs = LittleLong(leaf->visofs);
    }
}

static void
SwapBSP29Clipnodes(int numclipnodes, bsp29_dclipnode_t *clipnodes)
{
    bsp29_dclipnode_t *clipnode = clipnodes;
    int i;

    for (i = 0; i < numclipnodes; i++, clipnode++) {
	clipnode->planenum = LittleLong(clipnode->planenum);
	clipnode->children[0] = LittleShort(clipnode->children[0]);
	clipnode->children[1] = LittleShort(clipnode->children[1]);
    }
}

static void
SwapBSP2Clipnodes(int numclipnodes, bsp2_dclipnode_t *clipnodes)
{
    bsp2_dclipnode_t *clipnode = clipnodes;
    int i;

    for (i = 0; i < numclipnodes; i++, clipnode++) {
	clipnode->planenum = LittleLong(clipnode->planenum);
	clipnode->children[0] = LittleLong(clipnode->children[0]);
	clipnode->children[1] = LittleLong(clipnode->children[1]);
    }
}

static void
SwapBSP29Marksurfaces(int nummarksurfaces, uint16_t *dmarksurfaces)
{
    uint16_t *marksurface = dmarksurfaces;
    int i;

    for (i = 0; i < nummarksurfaces; i++, marksurface++)
	*marksurface = LittleShort(*marksurface);
}

static void
SwapBSP2Marksurfaces(int nummarksurfaces, uint32_t *dmarksurfaces)
{
    uint32_t *marksurface = dmarksurfaces;
    int i;

    for (i = 0; i < nummarksurfaces; i++, marksurface++)
	*marksurface = LittleLong(*marksurface);
}

static void
SwapBSPSurfedges(int numsurfedges, int32_t *dsurfedges)
{
    int32_t *surfedge = dsurfedges;
    int i;

    for (i = 0; i < numsurfedges; i++, surfedge++)
	*surfedge = LittleLong(*surfedge);
}

static void
SwapBSP29Edges(int numedges, bsp29_dedge_t *dedges)
{
    bsp29_dedge_t *edge = dedges;
    int i;

    for (i = 0; i < numedges; i++, edge++) {
	edge->v[0] = LittleShort(edge->v[0]);
	edge->v[1] = LittleShort(edge->v[1]);
    }
}

static void
SwapBSP2Edges(int numedges, bsp2_dedge_t *dedges)
{
    bsp2_dedge_t *edge = dedges;
    int i;

    for (i = 0; i < numedges; i++, edge++) {
	edge->v[0] = LittleLong(edge->v[0]);
	edge->v[1] = LittleLong(edge->v[1]);
    }
}

static void
SwapBSPModels(int nummodels, dmodel_t *dmodels)
{
    dmodel_t *dmodel = dmodels;
    int i, j;

    for (i = 0; i < nummodels; i++, dmodel++) {
	for (j = 0; j < MAX_MAP_HULLS; j++)
	    dmodel->headnode[j] = LittleLong(dmodel->headnode[j]);
	dmodel->visleafs = LittleLong(dmodel->visleafs);
	dmodel->firstface = LittleLong(dmodel->firstface);
	dmodel->numfaces = LittleLong(dmodel->numfaces);
	for (j = 0; j < 3; j++) {
	    dmodel->mins[j] = LittleFloat(dmodel->mins[j]);
	    dmodel->maxs[j] = LittleFloat(dmodel->maxs[j]);
	    dmodel->origin[j] = LittleFloat(dmodel->origin[j]);
	}
    }
}

static void
SwapBSPMiptex(int texdatasize, dmiptexlump_t *header, const swaptype_t swap)
{
    int i, count;

    if (!texdatasize)
	return;

    count = header->nummiptex;
    if (swap == TO_CPU)
	count = LittleLong(count);

    header->nummiptex = LittleLong(header->nummiptex);
    for (i = 0; i < count; i++)
	header->dataofs[i] = LittleLong(header->dataofs[i]);
}

/*
 * =============
 * SwapBSPFile
 * Byte swaps all data in a bsp file.
 * =============
 */
static void
SwapBSPFile(bspdata_t *bspdata, swaptype_t swap)
{
    if (bspdata->version == BSPVERSION) {
	bsp29_t *bsp = &bspdata->data.bsp29;

	SwapBSPVertexes(bsp->numvertexes, bsp->dvertexes);
	SwapBSPPlanes(bsp->numplanes, bsp->dplanes);
	SwapBSPTexinfo(bsp->numtexinfo, bsp->texinfo);
	SwapBSP29Faces(bsp->numfaces, bsp->dfaces);
	SwapBSP29Nodes(bsp->numnodes, bsp->dnodes);
	SwapBSP29Leafs(bsp->numleafs, bsp->dleafs);
	SwapBSP29Clipnodes(bsp->numclipnodes, bsp->dclipnodes);
	SwapBSPMiptex(bsp->texdatasize, bsp->dtexdata.header, swap);
	SwapBSP29Marksurfaces(bsp->nummarksurfaces, bsp->dmarksurfaces);
	SwapBSPSurfedges(bsp->numsurfedges, bsp->dsurfedges);
	SwapBSP29Edges(bsp->numedges, bsp->dedges);
	SwapBSPModels(bsp->nummodels, bsp->dmodels);

	return;
    }

    if (bspdata->version == BSP2RMQVERSION) {
	bsp2rmq_t *bsp = &bspdata->data.bsp2rmq;

	SwapBSPVertexes(bsp->numvertexes, bsp->dvertexes);
	SwapBSPPlanes(bsp->numplanes, bsp->dplanes);
	SwapBSPTexinfo(bsp->numtexinfo, bsp->texinfo);
	SwapBSP2Faces(bsp->numfaces, bsp->dfaces);
	SwapBSP2rmqNodes(bsp->numnodes, bsp->dnodes);
	SwapBSP2rmqLeafs(bsp->numleafs, bsp->dleafs);
	SwapBSP2Clipnodes(bsp->numclipnodes, bsp->dclipnodes);
	SwapBSPMiptex(bsp->texdatasize, bsp->dtexdata.header, swap);
	SwapBSP2Marksurfaces(bsp->nummarksurfaces, bsp->dmarksurfaces);
	SwapBSPSurfedges(bsp->numsurfedges, bsp->dsurfedges);
	SwapBSP2Edges(bsp->numedges, bsp->dedges);
	SwapBSPModels(bsp->nummodels, bsp->dmodels);

	return;
    }

    if (bspdata->version == BSP2VERSION) {
	bsp2_t *bsp = &bspdata->data.bsp2;

	SwapBSPVertexes(bsp->numvertexes, bsp->dvertexes);
	SwapBSPPlanes(bsp->numplanes, bsp->dplanes);
	SwapBSPTexinfo(bsp->numtexinfo, bsp->texinfo);
	SwapBSP2Faces(bsp->numfaces, bsp->dfaces);
	SwapBSP2Nodes(bsp->numnodes, bsp->dnodes);
	SwapBSP2Leafs(bsp->numleafs, bsp->dleafs);
	SwapBSP2Clipnodes(bsp->numclipnodes, bsp->dclipnodes);
	SwapBSPMiptex(bsp->texdatasize, bsp->dtexdata.header, swap);
	SwapBSP2Marksurfaces(bsp->nummarksurfaces, bsp->dmarksurfaces);
	SwapBSPSurfedges(bsp->numsurfedges, bsp->dsurfedges);
	SwapBSP2Edges(bsp->numedges, bsp->dedges);
	SwapBSPModels(bsp->nummodels, bsp->dmodels);

	return;
    }

    Error("Unsupported BSP version: %d", bspdata->version);
}

/*
 * =========================================================================
 * BSP Format Conversion (ver. 29 <-> BSP2)
 * =========================================================================
 */

static bsp2_dleaf_t *
BSP29to2_Leafs(const bsp29_t *bsp) {
    const bsp29_dleaf_t *dleaf29 = bsp->dleafs;
    bsp2_dleaf_t *newdata, *dleaf2;
    int i, j;

    newdata = dleaf2 = malloc(bsp->numleafs * sizeof(*dleaf2));

    for (i = 0; i < bsp->numleafs; i++, dleaf29++, dleaf2++) {
	dleaf2->contents = dleaf29->contents;
	dleaf2->visofs = dleaf29->visofs;
	for (j = 0; j < 3; j++) {
	    dleaf2->mins[j] = dleaf29->mins[j];
	    dleaf2->maxs[j] = dleaf29->maxs[j];
	}
	dleaf2->firstmarksurface = dleaf29->firstmarksurface;
	dleaf2->nummarksurfaces = dleaf29->nummarksurfaces;
	for (j = 0; j < NUM_AMBIENTS; j++)
	    dleaf2->ambient_level[j] = dleaf29->ambient_level[j];
    }

    free(bsp->dleafs);

    return newdata;
}

static bsp29_dleaf_t *
BSP2to29_Leafs(const bsp2_t *bsp) {
    const bsp2_dleaf_t *dleaf2 = bsp->dleafs;
    bsp29_dleaf_t *newdata, *dleaf29;
    int i, j;

    newdata = dleaf29 = malloc(bsp->numleafs * sizeof(*dleaf29));

    for (i = 0; i < bsp->numleafs; i++, dleaf2++, dleaf29++) {
	dleaf29->contents = dleaf2->contents;
	dleaf29->visofs = dleaf2->visofs;
	for (j = 0; j < 3; j++) {
	    dleaf29->mins[j] = dleaf2->mins[j];
	    dleaf29->maxs[j] = dleaf2->maxs[j];
	}
	dleaf29->firstmarksurface = dleaf2->firstmarksurface;
	dleaf29->nummarksurfaces = dleaf2->nummarksurfaces;
	for (j = 0; j < NUM_AMBIENTS; j++)
	    dleaf29->ambient_level[j] = dleaf2->ambient_level[j];
    }

    free(bsp->dleafs);

    return newdata;
}

static bsp2_dnode_t *
BSP29to2_Nodes(const bsp29_t *bsp) {
    const bsp29_dnode_t *dnode29 = bsp->dnodes;
    bsp2_dnode_t *newdata, *dnode2;
    int i, j;

    newdata = dnode2 = malloc(bsp->numnodes * sizeof(*dnode2));

    for (i = 0; i < bsp->numnodes; i++, dnode29++, dnode2++) {
	dnode2->planenum = dnode29->planenum;
	dnode2->children[0] = dnode29->children[0];
	dnode2->children[1] = dnode29->children[1];
	for (j = 0; j < 3; j++) {
	    dnode2->mins[j] = dnode29->mins[j];
	    dnode2->maxs[j] = dnode29->maxs[j];
	}
	dnode2->firstface = dnode29->firstface;
	dnode2->numfaces = dnode29->numfaces;
    }

    free(bsp->dnodes);

    return newdata;
}

static bsp29_dnode_t *
BSP2to29_Nodes(const bsp2_t *bsp) {
    const bsp2_dnode_t *dnode2 = bsp->dnodes;
    bsp29_dnode_t *newdata, *dnode29;
    int i, j;

    newdata = dnode29 = malloc(bsp->numnodes * sizeof(*dnode29));

    for (i = 0; i < bsp->numnodes; i++, dnode2++, dnode29++) {
	dnode29->planenum = dnode2->planenum;
	dnode29->children[0] = dnode2->children[0];
	dnode29->children[1] = dnode2->children[1];
	for (j = 0; j < 3; j++) {
	    dnode29->mins[j] = dnode2->mins[j];
	    dnode29->maxs[j] = dnode2->maxs[j];
	}
	dnode29->firstface = dnode2->firstface;
	dnode29->numfaces = dnode2->numfaces;
    }

    free(bsp->dnodes);

    return newdata;
}

static bsp2_dface_t *
BSP29to2_Faces(const bsp29_t *bsp) {
    const bsp29_dface_t *dface29 = bsp->dfaces;
    bsp2_dface_t *newdata, *dface2;
    int i, j;

    newdata = dface2 = malloc(bsp->numfaces * sizeof(*dface2));

    for (i = 0; i < bsp->numfaces; i++, dface29++, dface2++) {
	dface2->planenum = dface29->planenum;
	dface2->side = dface29->side;
	dface2->firstedge = dface29->firstedge;
	dface2->numedges = dface29->numedges;
	dface2->texinfo = dface29->texinfo;
	for (j = 0; j < MAXLIGHTMAPS; j++)
	    dface2->styles[j] = dface29->styles[j];
	dface2->lightofs = dface29->lightofs;
    }

    free(bsp->dfaces);

    return newdata;
}

static bsp29_dface_t *
BSP2to29_Faces(const bsp2_t *bsp) {
    const bsp2_dface_t *dface2 = bsp->dfaces;
    bsp29_dface_t *newdata, *dface29;
    int i, j;

    newdata = dface29 = malloc(bsp->numfaces * sizeof(*dface29));

    for (i = 0; i < bsp->numfaces; i++, dface2++, dface29++) {
	dface29->planenum = dface2->planenum;
	dface29->side = dface2->side;
	dface29->firstedge = dface2->firstedge;
	dface29->numedges = dface2->numedges;
	dface29->texinfo = dface2->texinfo;
	for (j = 0; j < MAXLIGHTMAPS; j++)
	    dface29->styles[j] = dface2->styles[j];
	dface29->lightofs = dface2->lightofs;
    }

    free(bsp->dfaces);

    return newdata;
}

static bsp2_dclipnode_t *
BSP29to2_Clipnodes(const bsp29_t *bsp) {
    const bsp29_dclipnode_t *dclipnode29 = bsp->dclipnodes;
    bsp2_dclipnode_t *newdata, *dclipnode2;
    int i, j;

    newdata = dclipnode2 = malloc(bsp->numclipnodes * sizeof(*dclipnode2));

    for (i = 0; i < bsp->numclipnodes; i++, dclipnode29++, dclipnode2++) {
	dclipnode2->planenum = dclipnode29->planenum;
	for (j = 0; j < 2; j++) {
	    /* Slightly tricky since we support > 32k clipnodes */
	    int32_t child = (uint16_t)dclipnode29->children[j];
	    dclipnode2->children[j] = child > 0xfff0 ? child - 0x10000 : child;
	}
    }

    free(bsp->dclipnodes);

    return newdata;
}

static bsp29_dclipnode_t *
BSP2to29_Clipnodes(const bsp2_t *bsp) {
    const bsp2_dclipnode_t *dclipnode2 = bsp->dclipnodes;
    bsp29_dclipnode_t *newdata, *dclipnode29;
    int i, j;

    newdata = dclipnode29 = malloc(bsp->numclipnodes * sizeof(*dclipnode29));

    for (i = 0; i < bsp->numclipnodes; i++, dclipnode2++, dclipnode29++) {
	dclipnode29->planenum = dclipnode2->planenum;
	for (j = 0; j < 2; j++) {
	    /* Slightly tricky since we support > 32k clipnodes */
	    int32_t child = dclipnode2->children[j];
	    dclipnode29->children[j] = child < 0 ? child + 0x10000 : child;
	}
    }

    free(bsp->dclipnodes);

    return newdata;
}

static bsp2_dedge_t *
BSP29to2_Edges(const bsp29_t *bsp)
{
    const bsp29_dedge_t *dedge29 = bsp->dedges;
    bsp2_dedge_t *newdata, *dedge2;
    int i;

    newdata = dedge2 = malloc(bsp->numedges * sizeof(*dedge2));

    for (i = 0; i < bsp->numedges; i++, dedge29++, dedge2++) {
	dedge2->v[0] = dedge29->v[0];
	dedge2->v[1] = dedge29->v[1];
    }

    free(bsp->dedges);

    return newdata;
}

static bsp29_dedge_t *
BSP2to29_Edges(const bsp2_t *bsp)
{
    const bsp2_dedge_t *dedge2 = bsp->dedges;
    bsp29_dedge_t *newdata, *dedge29;
    int i;

    newdata = dedge29 = malloc(bsp->numedges * sizeof(*dedge29));

    for (i = 0; i < bsp->numedges; i++, dedge2++, dedge29++) {
	dedge29->v[0] = dedge2->v[0];
	dedge29->v[1] = dedge2->v[1];
    }

    free(bsp->dedges);

    return newdata;
}

static uint32_t *
BSP29to2_Marksurfaces(const bsp29_t *bsp)
{
    const uint16_t *dmarksurface29 = bsp->dmarksurfaces;
    uint32_t *newdata, *dmarksurface2;
    int i;

    newdata = dmarksurface2 = malloc(bsp->nummarksurfaces * sizeof(*dmarksurface2));

    for (i = 0; i < bsp->nummarksurfaces; i++, dmarksurface29++, dmarksurface2++)
	*dmarksurface2 = *dmarksurface29;

    free(bsp->dmarksurfaces);

    return newdata;
}

static uint16_t *
BSP2to29_Marksurfaces(const bsp2_t *bsp)
{
    const uint32_t *dmarksurface2 = bsp->dmarksurfaces;
    uint16_t *newdata, *dmarksurface29;
    int i;

    newdata = dmarksurface29 = malloc(bsp->nummarksurfaces * sizeof(*dmarksurface29));

    for (i = 0; i < bsp->nummarksurfaces; i++, dmarksurface2++, dmarksurface29++)
	*dmarksurface29 = *dmarksurface2;

    free(bsp->dmarksurfaces);

    return newdata;
}

/*
 * =========================================================================
 * BSP Format Conversion (ver. BSP2rmq <-> BSP2)
 * =========================================================================
 */

static bsp2_dleaf_t *
BSP2rmqto2_Leafs(const bsp2rmq_t *bsp) {
    const bsp2rmq_dleaf_t *dleaf2rmq = bsp->dleafs;
    bsp2_dleaf_t *newdata, *dleaf2;
    int i, j;

    newdata = dleaf2 = malloc(bsp->numleafs * sizeof(*dleaf2));

    for (i = 0; i < bsp->numleafs; i++, dleaf2rmq++, dleaf2++) {
	dleaf2->contents = dleaf2rmq->contents;
	dleaf2->visofs = dleaf2rmq->visofs;
	for (j = 0; j < 3; j++) {
	    dleaf2->mins[j] = dleaf2rmq->mins[j];
	    dleaf2->maxs[j] = dleaf2rmq->maxs[j];
	}
	dleaf2->firstmarksurface = dleaf2rmq->firstmarksurface;
	dleaf2->nummarksurfaces = dleaf2rmq->nummarksurfaces;
	for (j = 0; j < NUM_AMBIENTS; j++)
	    dleaf2->ambient_level[j] = dleaf2rmq->ambient_level[j];
    }

    free(bsp->dleafs);

    return newdata;
}

static bsp2rmq_dleaf_t *
BSP2to2rmq_Leafs(const bsp2_t *bsp) {
    const bsp2_dleaf_t *dleaf2 = bsp->dleafs;
    bsp2rmq_dleaf_t *newdata, *dleaf2rmq;
    int i, j;

    newdata = dleaf2rmq = malloc(bsp->numleafs * sizeof(*dleaf2rmq));

    for (i = 0; i < bsp->numleafs; i++, dleaf2++, dleaf2rmq++) {
	dleaf2rmq->contents = dleaf2->contents;
	dleaf2rmq->visofs = dleaf2->visofs;
	for (j = 0; j < 3; j++) {
	    dleaf2rmq->mins[j] = dleaf2->mins[j];
	    dleaf2rmq->maxs[j] = dleaf2->maxs[j];
	}
	dleaf2rmq->firstmarksurface = dleaf2->firstmarksurface;
	dleaf2rmq->nummarksurfaces = dleaf2->nummarksurfaces;
	for (j = 0; j < NUM_AMBIENTS; j++)
	    dleaf2rmq->ambient_level[j] = dleaf2->ambient_level[j];
    }

    free(bsp->dleafs);

    return newdata;
}

static bsp2_dnode_t *
BSP2rmqto2_Nodes(const bsp2rmq_t *bsp) {
    const bsp2rmq_dnode_t *dnode2rmq = bsp->dnodes;
    bsp2_dnode_t *newdata, *dnode2;
    int i, j;

    newdata = dnode2 = malloc(bsp->numnodes * sizeof(*dnode2));

    for (i = 0; i < bsp->numnodes; i++, dnode2rmq++, dnode2++) {
	dnode2->planenum = dnode2rmq->planenum;
	dnode2->children[0] = dnode2rmq->children[0];
	dnode2->children[1] = dnode2rmq->children[1];
	for (j = 0; j < 3; j++) {
	    dnode2->mins[j] = dnode2rmq->mins[j];
	    dnode2->maxs[j] = dnode2rmq->maxs[j];
	}
	dnode2->firstface = dnode2rmq->firstface;
	dnode2->numfaces = dnode2rmq->numfaces;
    }

    free(bsp->dnodes);

    return newdata;
}

static bsp2rmq_dnode_t *
BSP2to2rmq_Nodes(const bsp2_t *bsp) {
    const bsp2_dnode_t *dnode2 = bsp->dnodes;
    bsp2rmq_dnode_t *newdata, *dnode2rmq;
    int i, j;

    newdata = dnode2rmq = malloc(bsp->numnodes * sizeof(*dnode2rmq));

    for (i = 0; i < bsp->numnodes; i++, dnode2++, dnode2rmq++) {
	dnode2rmq->planenum = dnode2->planenum;
	dnode2rmq->children[0] = dnode2->children[0];
	dnode2rmq->children[1] = dnode2->children[1];
	for (j = 0; j < 3; j++) {
	    dnode2rmq->mins[j] = dnode2->mins[j];
	    dnode2rmq->maxs[j] = dnode2->maxs[j];
	}
	dnode2rmq->firstface = dnode2->firstface;
	dnode2rmq->numfaces = dnode2->numfaces;
    }

    free(bsp->dnodes);

    return newdata;
}

/*
 * =========================================================================
 * ConvertBSPFormat
 * - BSP is assumed to be in CPU byte order already
 * - No checks are done here (yet) for overflow of values when down-converting
 * =========================================================================
 */
void
ConvertBSPFormat(int32_t version, bspdata_t *bspdata)
{
    if (bspdata->version == version)
	return;

    if (bspdata->version == BSPVERSION && version == BSP2VERSION) {
	bsp29_t *bsp29 = &bspdata->data.bsp29;
	bsp2_t *bsp2 = &bspdata->data.bsp2;

	bsp2->dleafs = BSP29to2_Leafs(bsp29);
	bsp2->dnodes = BSP29to2_Nodes(bsp29);
	bsp2->dfaces = BSP29to2_Faces(bsp29);
	bsp2->dclipnodes = BSP29to2_Clipnodes(bsp29);
	bsp2->dedges = BSP29to2_Edges(bsp29);
	bsp2->dmarksurfaces = BSP29to2_Marksurfaces(bsp29);

	/* Conversion complete! */
	bspdata->version = BSP2VERSION;

	return;
    }

    if (bspdata->version == BSP2RMQVERSION && version == BSP2VERSION) {
	bsp2rmq_t *bsp2rmq = &bspdata->data.bsp2rmq;
	bsp2_t *bsp2 = &bspdata->data.bsp2;

	bsp2->dleafs = BSP2rmqto2_Leafs(bsp2rmq);
	bsp2->dnodes = BSP2rmqto2_Nodes(bsp2rmq);

	/* Conversion complete! */
	bspdata->version = BSP2VERSION;

	return;
    }

    if (bspdata->version == BSP2VERSION && version == BSPVERSION) {
	bsp29_t *bsp29 = &bspdata->data.bsp29;
	bsp2_t *bsp2 = &bspdata->data.bsp2;

	bsp29->dleafs = BSP2to29_Leafs(bsp2);
	bsp29->dnodes = BSP2to29_Nodes(bsp2);
	bsp29->dfaces = BSP2to29_Faces(bsp2);
	bsp29->dclipnodes = BSP2to29_Clipnodes(bsp2);
	bsp29->dedges = BSP2to29_Edges(bsp2);
	bsp29->dmarksurfaces = BSP2to29_Marksurfaces(bsp2);

	/* Conversion complete! */
	bspdata->version = BSPVERSION;

	return;
    }

    if (bspdata->version == BSP2VERSION && version == BSP2RMQVERSION) {
	bsp2rmq_t *bsp2rmq = &bspdata->data.bsp2rmq;
	bsp2_t *bsp2 = &bspdata->data.bsp2;

	bsp2rmq->dleafs = BSP2to2rmq_Leafs(bsp2);
	bsp2rmq->dnodes = BSP2to2rmq_Nodes(bsp2);

	/* Conversion complete! */
	bspdata->version = BSP2RMQVERSION;

	return;
    }

    if (bspdata->version == BSPVERSION && version == BSP2RMQVERSION) {
	ConvertBSPFormat(BSP2VERSION, bspdata);
	ConvertBSPFormat(BSP2RMQVERSION, bspdata);
	return;
    }

    if (bspdata->version == BSP2RMQVERSION && version == BSPVERSION) {
	ConvertBSPFormat(BSP2VERSION, bspdata);
	ConvertBSPFormat(BSPVERSION, bspdata);
	return;
    }


    Error("Don't know how to convert BSP version %s to %s",
	  BSPVersionString(bspdata->version), BSPVersionString(version));
}

/*
 * =========================================================================
 * ...
 * =========================================================================
 */

const lumpspec_t lumpspec_bsp29[] = {
    { "entities",     sizeof(char)              },
    { "planes",       sizeof(dplane_t)          },
    { "texture",      sizeof(byte)              },
    { "vertexes",     sizeof(dvertex_t)         },
    { "visibility",   sizeof(byte)              },
    { "nodes",        sizeof(bsp29_dnode_t)     },
    { "texinfos",     sizeof(texinfo_t)         },
    { "faces",        sizeof(bsp29_dface_t)     },
    { "lighting",     sizeof(byte)              },
    { "clipnodes",    sizeof(bsp29_dclipnode_t) },
    { "leafs",        sizeof(bsp29_dleaf_t)     },
    { "marksurfaces", sizeof(uint16_t)          },
    { "edges",        sizeof(bsp29_dedge_t)     },
    { "surfedges",    sizeof(int32_t)           },
    { "models",       sizeof(dmodel_t)          },
};

const lumpspec_t lumpspec_bsp2rmq[] = {
    { "entities",     sizeof(char)              },
    { "planes",       sizeof(dplane_t)          },
    { "texture",      sizeof(byte)              },
    { "vertexes",     sizeof(dvertex_t)         },
    { "visibility",   sizeof(byte)              },
    { "nodes",        sizeof(bsp2rmq_dnode_t)   },
    { "texinfos",     sizeof(texinfo_t)         },
    { "faces",        sizeof(bsp2_dface_t)      },
    { "lighting",     sizeof(byte)              },
    { "clipnodes",    sizeof(bsp2_dclipnode_t ) },
    { "leafs",        sizeof(bsp2rmq_dleaf_t)   },
    { "marksurfaces", sizeof(uint32_t)          },
    { "edges",        sizeof(bsp2_dedge_t)      },
    { "surfedges",    sizeof(int32_t)           },
    { "models",       sizeof(dmodel_t)          },
};

const lumpspec_t lumpspec_bsp2[] = {
    { "entities",     sizeof(char)              },
    { "planes",       sizeof(dplane_t)          },
    { "texture",      sizeof(byte)              },
    { "vertexes",     sizeof(dvertex_t)         },
    { "visibility",   sizeof(byte)              },
    { "nodes",        sizeof(bsp2_dnode_t)      },
    { "texinfos",     sizeof(texinfo_t)         },
    { "faces",        sizeof(bsp2_dface_t)      },
    { "lighting",     sizeof(byte)              },
    { "clipnodes",    sizeof(bsp2_dclipnode_t ) },
    { "leafs",        sizeof(bsp2_dleaf_t)      },
    { "marksurfaces", sizeof(uint32_t)          },
    { "edges",        sizeof(bsp2_dedge_t)      },
    { "surfedges",    sizeof(int32_t)           },
    { "models",       sizeof(dmodel_t)          },
};

static int
CopyLump(const dheader_t *header, int lumpnum, void *destptr)
{
    const lumpspec_t *lumpspec;
    byte **bufferptr = destptr;
    byte *buffer = *bufferptr;
    int length;
    int ofs;

    switch (header->version) {
    case BSPVERSION:
	lumpspec = &lumpspec_bsp29[lumpnum];
	break;
    case BSP2RMQVERSION:
	lumpspec = &lumpspec_bsp2rmq[lumpnum];
	break;
    case BSP2VERSION:
	lumpspec = &lumpspec_bsp2[lumpnum];
	break;
    default:
	Error("Unsupported BSP version: %d", header->version);
    }

    length = header->lumps[lumpnum].filelen;
    ofs = header->lumps[lumpnum].fileofs;

    if (length % lumpspec->size)
	Error("%s: odd %s lump size", __func__, lumpspec->name);

    if (buffer)
	free(buffer);

    buffer = *bufferptr = malloc(length + 1);
    if (!buffer)
	Error("%s: allocation of %i bytes failed.", __func__, length);

    memcpy(buffer, (const byte *)header + ofs, length);
    buffer[length] = 0; /* In case of corrupt entity lump */

    return length / lumpspec->size;
}

/*
 * =============
 * LoadBSPFile
 * =============
 */
void
LoadBSPFile(const char *filename, bspdata_t *bspdata)
{
    dheader_t *header;
    int i;

    /* load the file header */
    LoadFile(filename, &header);

    /* check the file version */
    header->version = LittleLong(header->version);
    logprint("BSP is version %s\n", BSPVersionString(header->version));
    if (!BSPVersionSupported(header->version))
	Error("Sorry, this bsp version is not supported.");

    /* swap the lump headers */
    for (i = 0; i < BSP_LUMPS; i++) {
	header->lumps[i].fileofs = LittleLong(header->lumps[i].fileofs);
	header->lumps[i].filelen = LittleLong(header->lumps[i].filelen);
    }

    /* copy the data */
    if (header->version == BSPVERSION) {
	bsp29_t *bsp = &bspdata->data.bsp29;

	memset(bsp, 0, sizeof(*bsp));
	bspdata->version = header->version;
	bsp->nummodels = CopyLump(header, LUMP_MODELS, &bsp->dmodels);
	bsp->numvertexes = CopyLump(header, LUMP_VERTEXES, &bsp->dvertexes);
	bsp->numplanes = CopyLump(header, LUMP_PLANES, &bsp->dplanes);
	bsp->numleafs = CopyLump(header, LUMP_LEAFS, &bsp->dleafs);
	bsp->numnodes = CopyLump(header, LUMP_NODES, &bsp->dnodes);
	bsp->numtexinfo = CopyLump(header, LUMP_TEXINFO, &bsp->texinfo);
	bsp->numclipnodes = CopyLump(header, LUMP_CLIPNODES, &bsp->dclipnodes);
	bsp->numfaces = CopyLump(header, LUMP_FACES, &bsp->dfaces);
	bsp->nummarksurfaces = CopyLump(header, LUMP_MARKSURFACES, &bsp->dmarksurfaces);
	bsp->numsurfedges = CopyLump(header, LUMP_SURFEDGES, &bsp->dsurfedges);
	bsp->numedges = CopyLump(header, LUMP_EDGES, &bsp->dedges);

	bsp->texdatasize = CopyLump(header, LUMP_TEXTURES, &bsp->dtexdata.base);
	bsp->visdatasize = CopyLump(header, LUMP_VISIBILITY, &bsp->dvisdata);
	bsp->lightdatasize = CopyLump(header, LUMP_LIGHTING, &bsp->dlightdata);
	bsp->entdatasize = CopyLump(header, LUMP_ENTITIES, &bsp->dentdata);
    }

    if (header->version == BSP2RMQVERSION) {
	bsp2rmq_t *bsp = &bspdata->data.bsp2rmq;

	memset(bsp, 0, sizeof(*bsp));
	bspdata->version = header->version;
	bsp->nummodels = CopyLump(header, LUMP_MODELS, &bsp->dmodels);
	bsp->numvertexes = CopyLump(header, LUMP_VERTEXES, &bsp->dvertexes);
	bsp->numplanes = CopyLump(header, LUMP_PLANES, &bsp->dplanes);
	bsp->numleafs = CopyLump(header, LUMP_LEAFS, &bsp->dleafs);
	bsp->numnodes = CopyLump(header, LUMP_NODES, &bsp->dnodes);
	bsp->numtexinfo = CopyLump(header, LUMP_TEXINFO, &bsp->texinfo);
	bsp->numclipnodes = CopyLump(header, LUMP_CLIPNODES, &bsp->dclipnodes);
	bsp->numfaces = CopyLump(header, LUMP_FACES, &bsp->dfaces);
	bsp->nummarksurfaces = CopyLump(header, LUMP_MARKSURFACES, &bsp->dmarksurfaces);
	bsp->numsurfedges = CopyLump(header, LUMP_SURFEDGES, &bsp->dsurfedges);
	bsp->numedges = CopyLump(header, LUMP_EDGES, &bsp->dedges);

	bsp->texdatasize = CopyLump(header, LUMP_TEXTURES, &bsp->dtexdata.base);
	bsp->visdatasize = CopyLump(header, LUMP_VISIBILITY, &bsp->dvisdata);
	bsp->lightdatasize = CopyLump(header, LUMP_LIGHTING, &bsp->dlightdata);
	bsp->entdatasize = CopyLump(header, LUMP_ENTITIES, &bsp->dentdata);
    }

    if (header->version == BSP2VERSION) {
	bsp2_t *bsp = &bspdata->data.bsp2;

	memset(bsp, 0, sizeof(*bsp));
	bspdata->version = header->version;
	bsp->nummodels = CopyLump(header, LUMP_MODELS, &bsp->dmodels);
	bsp->numvertexes = CopyLump(header, LUMP_VERTEXES, &bsp->dvertexes);
	bsp->numplanes = CopyLump(header, LUMP_PLANES, &bsp->dplanes);
	bsp->numleafs = CopyLump(header, LUMP_LEAFS, &bsp->dleafs);
	bsp->numnodes = CopyLump(header, LUMP_NODES, &bsp->dnodes);
	bsp->numtexinfo = CopyLump(header, LUMP_TEXINFO, &bsp->texinfo);
	bsp->numclipnodes = CopyLump(header, LUMP_CLIPNODES, &bsp->dclipnodes);
	bsp->numfaces = CopyLump(header, LUMP_FACES, &bsp->dfaces);
	bsp->nummarksurfaces = CopyLump(header, LUMP_MARKSURFACES, &bsp->dmarksurfaces);
	bsp->numsurfedges = CopyLump(header, LUMP_SURFEDGES, &bsp->dsurfedges);
	bsp->numedges = CopyLump(header, LUMP_EDGES, &bsp->dedges);

	bsp->texdatasize = CopyLump(header, LUMP_TEXTURES, &bsp->dtexdata.base);
	bsp->visdatasize = CopyLump(header, LUMP_VISIBILITY, &bsp->dvisdata);
	bsp->lightdatasize = CopyLump(header, LUMP_LIGHTING, &bsp->dlightdata);
	bsp->entdatasize = CopyLump(header, LUMP_ENTITIES, &bsp->dentdata);
    }

    /* everything has been copied out */
    free(header);

    /* swap everything */
    SwapBSPFile(bspdata, TO_CPU);
}

/* ========================================================================= */

typedef struct {
    dheader_t header;
    FILE *file;
} bspfile_t;

static void
AddLump(bspfile_t *bspfile, int lumpnum, const void *data, int count)
{
    lump_t *lump = &bspfile->header.lumps[lumpnum];
    byte pad[4] = {0};
    size_t size;

    /* FIXME - bad API, needing to byte swap back and forth... */
    switch (LittleLong(bspfile->header.version)) {
    case BSPVERSION:
	size = lumpspec_bsp29[lumpnum].size * count;
	break;
    case BSP2RMQVERSION:
	size = lumpspec_bsp2rmq[lumpnum].size * count;
	break;
    case BSP2VERSION:
	size = lumpspec_bsp2[lumpnum].size * count;
	break;
    default:
	Error("Unsupported BSP version: %d",
	      LittleLong(bspfile->header.version));

    }

    lump->fileofs = LittleLong(ftell(bspfile->file));
    lump->filelen = LittleLong(size);
    SafeWrite(bspfile->file, data, (size + 3) & ~3);
    if (size % 4)
	SafeWrite(bspfile->file, pad, size % 4);
}

/*
 * =============
 * WriteBSPFile
 * Swaps the bsp file in place, so it should not be referenced again
 * =============
 */
void
WriteBSPFile(const char *filename, bspdata_t *bspdata)
{
    bspfile_t bspfile;

    memset(&bspfile.header, 0, sizeof(bspfile.header));

    SwapBSPFile(bspdata, TO_DISK);

    bspfile.header.version = LittleLong(bspdata->version);
    logprint("Writing BSP version %s\n", BSPVersionString(bspdata->version));
    bspfile.file = SafeOpenWrite(filename);

    /* Save header space, updated after adding the lumps */
    SafeWrite(bspfile.file, &bspfile.header, sizeof(bspfile.header));

    if (bspdata->version == BSPVERSION) {
	bsp29_t *bsp = &bspdata->data.bsp29;

	AddLump(&bspfile, LUMP_PLANES, bsp->dplanes, bsp->numplanes);
	AddLump(&bspfile, LUMP_LEAFS, bsp->dleafs, bsp->numleafs);
	AddLump(&bspfile, LUMP_VERTEXES, bsp->dvertexes, bsp->numvertexes);
	AddLump(&bspfile, LUMP_NODES, bsp->dnodes, bsp->numnodes);
	AddLump(&bspfile, LUMP_TEXINFO, bsp->texinfo, bsp->numtexinfo);
	AddLump(&bspfile, LUMP_FACES, bsp->dfaces, bsp->numfaces);
	AddLump(&bspfile, LUMP_CLIPNODES, bsp->dclipnodes, bsp->numclipnodes);
	AddLump(&bspfile, LUMP_MARKSURFACES, bsp->dmarksurfaces, bsp->nummarksurfaces);
	AddLump(&bspfile, LUMP_SURFEDGES, bsp->dsurfedges, bsp->numsurfedges);
	AddLump(&bspfile, LUMP_EDGES, bsp->dedges, bsp->numedges);
	AddLump(&bspfile, LUMP_MODELS, bsp->dmodels, bsp->nummodels);

	AddLump(&bspfile, LUMP_LIGHTING, bsp->dlightdata, bsp->lightdatasize);
	AddLump(&bspfile, LUMP_VISIBILITY, bsp->dvisdata, bsp->visdatasize);
	AddLump(&bspfile, LUMP_ENTITIES, bsp->dentdata, bsp->entdatasize);
	AddLump(&bspfile, LUMP_TEXTURES, bsp->dtexdata.base, bsp->texdatasize);
    }

    if (bspdata->version == BSP2RMQVERSION) {
	bsp2rmq_t *bsp = &bspdata->data.bsp2rmq;

	AddLump(&bspfile, LUMP_PLANES, bsp->dplanes, bsp->numplanes);
	AddLump(&bspfile, LUMP_LEAFS, bsp->dleafs, bsp->numleafs);
	AddLump(&bspfile, LUMP_VERTEXES, bsp->dvertexes, bsp->numvertexes);
	AddLump(&bspfile, LUMP_NODES, bsp->dnodes, bsp->numnodes);
	AddLump(&bspfile, LUMP_TEXINFO, bsp->texinfo, bsp->numtexinfo);
	AddLump(&bspfile, LUMP_FACES, bsp->dfaces, bsp->numfaces);
	AddLump(&bspfile, LUMP_CLIPNODES, bsp->dclipnodes, bsp->numclipnodes);
	AddLump(&bspfile, LUMP_MARKSURFACES, bsp->dmarksurfaces, bsp->nummarksurfaces);
	AddLump(&bspfile, LUMP_SURFEDGES, bsp->dsurfedges, bsp->numsurfedges);
	AddLump(&bspfile, LUMP_EDGES, bsp->dedges, bsp->numedges);
	AddLump(&bspfile, LUMP_MODELS, bsp->dmodels, bsp->nummodels);

	AddLump(&bspfile, LUMP_LIGHTING, bsp->dlightdata, bsp->lightdatasize);
	AddLump(&bspfile, LUMP_VISIBILITY, bsp->dvisdata, bsp->visdatasize);
	AddLump(&bspfile, LUMP_ENTITIES, bsp->dentdata, bsp->entdatasize);
	AddLump(&bspfile, LUMP_TEXTURES, bsp->dtexdata.base, bsp->texdatasize);
    }

    if (bspdata->version == BSP2VERSION) {
	bsp2_t *bsp = &bspdata->data.bsp2;

	AddLump(&bspfile, LUMP_PLANES, bsp->dplanes, bsp->numplanes);
	AddLump(&bspfile, LUMP_LEAFS, bsp->dleafs, bsp->numleafs);
	AddLump(&bspfile, LUMP_VERTEXES, bsp->dvertexes, bsp->numvertexes);
	AddLump(&bspfile, LUMP_NODES, bsp->dnodes, bsp->numnodes);
	AddLump(&bspfile, LUMP_TEXINFO, bsp->texinfo, bsp->numtexinfo);
	AddLump(&bspfile, LUMP_FACES, bsp->dfaces, bsp->numfaces);
	AddLump(&bspfile, LUMP_CLIPNODES, bsp->dclipnodes, bsp->numclipnodes);
	AddLump(&bspfile, LUMP_MARKSURFACES, bsp->dmarksurfaces, bsp->nummarksurfaces);
	AddLump(&bspfile, LUMP_SURFEDGES, bsp->dsurfedges, bsp->numsurfedges);
	AddLump(&bspfile, LUMP_EDGES, bsp->dedges, bsp->numedges);
	AddLump(&bspfile, LUMP_MODELS, bsp->dmodels, bsp->nummodels);

	AddLump(&bspfile, LUMP_LIGHTING, bsp->dlightdata, bsp->lightdatasize);
	AddLump(&bspfile, LUMP_VISIBILITY, bsp->dvisdata, bsp->visdatasize);
	AddLump(&bspfile, LUMP_ENTITIES, bsp->dentdata, bsp->entdatasize);
	AddLump(&bspfile, LUMP_TEXTURES, bsp->dtexdata.base, bsp->texdatasize);
    }

    fseek(bspfile.file, 0, SEEK_SET);
    SafeWrite(bspfile.file, &bspfile.header, sizeof(bspfile.header));

    fclose(bspfile.file);
}

/* ========================================================================= */

static void
PrintLumpSize(const lumpspec_t *lumpspec, int lumptype, int count)
{
    const lumpspec_t *lump = &lumpspec[lumptype];
    logprint("%7i %-12s %10i\n", count, lump->name, count * (int)lump->size);
}

/*
 * =============
 * PrintBSPFileSizes
 * Dumps info about the bsp data
 * =============
 */
void
PrintBSPFileSizes(const bspdata_t *bspdata)
{
    int numtextures = 0;

    if (bspdata->version == BSPVERSION) {
	const bsp29_t *bsp = &bspdata->data.bsp29;
	const lumpspec_t *lumpspec = lumpspec_bsp29;

	if (bsp->texdatasize)
	    numtextures = bsp->dtexdata.header->nummiptex;

	PrintLumpSize(lumpspec, LUMP_PLANES, bsp->numplanes);
	PrintLumpSize(lumpspec, LUMP_VERTEXES, bsp->numvertexes);
	PrintLumpSize(lumpspec, LUMP_NODES, bsp->numnodes);
	PrintLumpSize(lumpspec, LUMP_TEXINFO, bsp->numtexinfo);
	PrintLumpSize(lumpspec, LUMP_FACES, bsp->numfaces);
	PrintLumpSize(lumpspec, LUMP_CLIPNODES, bsp->numclipnodes);
	PrintLumpSize(lumpspec, LUMP_LEAFS, bsp->numleafs);
	PrintLumpSize(lumpspec, LUMP_MARKSURFACES, bsp->nummarksurfaces);
	PrintLumpSize(lumpspec, LUMP_EDGES, bsp->numedges);
	PrintLumpSize(lumpspec, LUMP_SURFEDGES, bsp->numsurfedges);

	logprint("%7i %-12s %10i\n", numtextures, "textures", bsp->texdatasize);
	logprint("%7s %-12s %10i\n", "", "lightdata", bsp->lightdatasize);
	logprint("%7s %-12s %10i\n", "", "visdata", bsp->visdatasize);
	logprint("%7s %-12s %10i\n", "", "entdata", bsp->entdatasize);
    }

    if (bspdata->version == BSP2RMQVERSION) {
	const bsp2rmq_t *bsp = &bspdata->data.bsp2rmq;
	const lumpspec_t *lumpspec = lumpspec_bsp2rmq;

	if (bsp->texdatasize)
	    numtextures = bsp->dtexdata.header->nummiptex;

	PrintLumpSize(lumpspec, LUMP_PLANES, bsp->numplanes);
	PrintLumpSize(lumpspec, LUMP_VERTEXES, bsp->numvertexes);
	PrintLumpSize(lumpspec, LUMP_NODES, bsp->numnodes);
	PrintLumpSize(lumpspec, LUMP_TEXINFO, bsp->numtexinfo);
	PrintLumpSize(lumpspec, LUMP_FACES, bsp->numfaces);
	PrintLumpSize(lumpspec, LUMP_CLIPNODES, bsp->numclipnodes);
	PrintLumpSize(lumpspec, LUMP_LEAFS, bsp->numleafs);
	PrintLumpSize(lumpspec, LUMP_MARKSURFACES, bsp->nummarksurfaces);
	PrintLumpSize(lumpspec, LUMP_EDGES, bsp->numedges);
	PrintLumpSize(lumpspec, LUMP_SURFEDGES, bsp->numsurfedges);

	logprint("%7i %-12s %10i\n", numtextures, "textures", bsp->texdatasize);
	logprint("%7s %-12s %10i\n", "", "lightdata", bsp->lightdatasize);
	logprint("%7s %-12s %10i\n", "", "visdata", bsp->visdatasize);
	logprint("%7s %-12s %10i\n", "", "entdata", bsp->entdatasize);
    }

    if (bspdata->version == BSP2VERSION) {
	const bsp2_t *bsp = &bspdata->data.bsp2;
	const lumpspec_t *lumpspec = lumpspec_bsp2;

	if (bsp->texdatasize)
	    numtextures = bsp->dtexdata.header->nummiptex;

	PrintLumpSize(lumpspec, LUMP_PLANES, bsp->numplanes);
	PrintLumpSize(lumpspec, LUMP_VERTEXES, bsp->numvertexes);
	PrintLumpSize(lumpspec, LUMP_NODES, bsp->numnodes);
	PrintLumpSize(lumpspec, LUMP_TEXINFO, bsp->numtexinfo);
	PrintLumpSize(lumpspec, LUMP_FACES, bsp->numfaces);
	PrintLumpSize(lumpspec, LUMP_CLIPNODES, bsp->numclipnodes);
	PrintLumpSize(lumpspec, LUMP_LEAFS, bsp->numleafs);
	PrintLumpSize(lumpspec, LUMP_MARKSURFACES, bsp->nummarksurfaces);
	PrintLumpSize(lumpspec, LUMP_EDGES, bsp->numedges);
	PrintLumpSize(lumpspec, LUMP_SURFEDGES, bsp->numsurfedges);

	logprint("%7i %-12s %10i\n", numtextures, "textures", bsp->texdatasize);
	logprint("%7s %-12s %10i\n", "", "lightdata", bsp->lightdatasize);
	logprint("%7s %-12s %10i\n", "", "visdata", bsp->visdatasize);
	logprint("%7s %-12s %10i\n", "", "entdata", bsp->entdatasize);
    }
}
