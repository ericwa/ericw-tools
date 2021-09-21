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

#include <common/cmdlib.hh>
#include <common/mathlib.hh>
#include <common/bspfile.hh>

#include <cstdint>
#include <limits.h>

/*                                                                                                           hexen2, quake2 */
const bspversion_t bspver_generic   { NO_VERSION,     NO_VERSION,    "mbsp",          "generic BSP",         false,  false };
const bspversion_t bspver_q1        { BSPVERSION,     NO_VERSION,    "bsp29",         "Quake BSP",           false,  false };
const bspversion_t bspver_bsp2      { BSP2VERSION,    NO_VERSION,    "bsp2",          "Quake BSP2",          false,  false };
const bspversion_t bspver_bsp2rmq   { BSP2RMQVERSION, NO_VERSION,    "bsp2rmq",       "Quake BSP2-RMQ",      false,  false };
/* Hexen II doesn't use a separate version, but we can still use a separate tag/name for it */               
const bspversion_t bspver_h2        { BSPVERSION,     NO_VERSION,    "hexen2",        "Hexen II BSP",        true,   false };
const bspversion_t bspver_h2bsp2    { BSP2VERSION,    NO_VERSION,    "hexen2bsp2",    "Hexen II BSP2",       true,   false };
const bspversion_t bspver_h2bsp2rmq { BSP2RMQVERSION, NO_VERSION,    "hexen2bsp2rmq", "Hexen II BSP2-RMQ",   true,   false };
const bspversion_t bspver_hl        { BSPHLVERSION,   NO_VERSION,    "hl",            "Half-Life BSP",       false,  false };
const bspversion_t bspver_q2        { Q2_BSPIDENT,    Q2_BSPVERSION, "q2bsp",         "Quake II BSP",        false,  true  };
const bspversion_t bspver_qbism     { Q2_QBISMIDENT,  Q2_BSPVERSION, "qbism",         "Quake II Qbism BSP",  false,  true  };

static const char *
BSPVersionString(const bspversion_t *version)
{
    if (version->name) {
        return version->name;
    }

    static char buffers[2][20];
    static int index;
    char *buffer = buffers[1 & ++index];
    if (version->version != NO_VERSION) {
        q_snprintf(buffer, sizeof(buffers[0]), "%d:%d", version->version, version->ident);
    } else {
        q_snprintf(buffer, sizeof(buffers[0]), "%d", version->version);
    }
    return buffer;
}

static qboolean
BSPVersionSupported(int32_t ident, int32_t version, const bspversion_t **out_version)
{
    for (const bspversion_t *bspver : bspversions) {
        if (bspver->ident == ident && bspver->version == version) {
            if (bspver->hexen2) {
                // HACK: don't detect as Hexen II here, it's done later (isHexen2).
                // Since the Hexen II bspversion_t's have the same ident/version as Quake
                // we need to assume Quake.
                continue;
            }
            *out_version = bspver;
            return true;
        }
    }

    return false;
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
SwapBSPModels(int nummodels, dmodelh2_t *dmodels)
{
    dmodelh2_t *dmodel = dmodels;
    int i, j;

    for (i = 0; i < nummodels; i++, dmodel++) {
        for (j = 0; j < MAX_MAP_HULLS_H2; j++)
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
SwapBSPModels(int nummodels, dmodelq1_t *dmodels)
{
    dmodelq1_t *dmodel = dmodels;
    int i, j;

    for (i = 0; i < nummodels; i++, dmodel++) {
        for (j = 0; j < MAX_MAP_HULLS_Q1; j++)
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
=============
Q2_SwapBSPFile

Byte swaps all data in a bsp file.
=============
*/
void Q2_SwapBSPFile (q2bsp_t *bsp, qboolean todisk)
{
    int                i, j;
    q2_dmodel_t        *d;
    
    
    // models
    for (i=0 ; i<bsp->nummodels ; i++)
    {
        d = &bsp->dmodels[i];
        
        d->firstface = LittleLong (d->firstface);
        d->numfaces = LittleLong (d->numfaces);
        d->headnode = LittleLong (d->headnode);
        
        for (j=0 ; j<3 ; j++)
        {
            d->mins[j] = LittleFloat(d->mins[j]);
            d->maxs[j] = LittleFloat(d->maxs[j]);
            d->origin[j] = LittleFloat(d->origin[j]);
        }
    }
    
    //
    // vertexes
    //
    for (i=0 ; i<bsp->numvertexes ; i++)
    {
        for (j=0 ; j<3 ; j++)
            bsp->dvertexes[i].point[j] = LittleFloat (bsp->dvertexes[i].point[j]);
    }
    
    //
    // planes
    //
    for (i=0 ; i<bsp->numplanes ; i++)
    {
        for (j=0 ; j<3 ; j++)
            bsp->dplanes[i].normal[j] = LittleFloat (bsp->dplanes[i].normal[j]);
        bsp->dplanes[i].dist = LittleFloat (bsp->dplanes[i].dist);
        bsp->dplanes[i].type = LittleLong (bsp->dplanes[i].type);
    }
    
    //
    // texinfos
    //
    for (i=0 ; i<bsp->numtexinfo ; i++)
    {
        for (j=0 ; j<4 ; j++)
        {
            bsp->texinfo[i].vecs[0][j] = LittleFloat(bsp->texinfo[i].vecs[0][j]);
            bsp->texinfo[i].vecs[1][j] = LittleFloat(bsp->texinfo[i].vecs[1][j]);
        }
        bsp->texinfo[i].flags = LittleLong (bsp->texinfo[i].flags);
        bsp->texinfo[i].value = LittleLong (bsp->texinfo[i].value);
        bsp->texinfo[i].nexttexinfo = LittleLong (bsp->texinfo[i].nexttexinfo);
    }
    
    //
    // faces
    //
    for (i=0 ; i<bsp->numfaces ; i++)
    {
        bsp->dfaces[i].texinfo = LittleShort (bsp->dfaces[i].texinfo);
        bsp->dfaces[i].planenum = LittleShort (bsp->dfaces[i].planenum);
        bsp->dfaces[i].side = LittleShort (bsp->dfaces[i].side);
        bsp->dfaces[i].lightofs = LittleLong (bsp->dfaces[i].lightofs);
        bsp->dfaces[i].firstedge = LittleLong (bsp->dfaces[i].firstedge);
        bsp->dfaces[i].numedges = LittleShort (bsp->dfaces[i].numedges);
    }
    
    //
    // nodes
    //
    for (i=0 ; i<bsp->numnodes ; i++)
    {
        bsp->dnodes[i].planenum = LittleLong (bsp->dnodes[i].planenum);
        for (j=0 ; j<3 ; j++)
        {
            bsp->dnodes[i].mins[j] = LittleShort (bsp->dnodes[i].mins[j]);
            bsp->dnodes[i].maxs[j] = LittleShort (bsp->dnodes[i].maxs[j]);
        }
        bsp->dnodes[i].children[0] = LittleLong (bsp->dnodes[i].children[0]);
        bsp->dnodes[i].children[1] = LittleLong (bsp->dnodes[i].children[1]);
        bsp->dnodes[i].firstface = LittleShort (bsp->dnodes[i].firstface);
        bsp->dnodes[i].numfaces = LittleShort (bsp->dnodes[i].numfaces);
    }
    
    //
    // leafs
    //
    for (i=0 ; i<bsp->numleafs ; i++)
    {
        bsp->dleafs[i].contents = LittleLong (bsp->dleafs[i].contents);
        bsp->dleafs[i].cluster = LittleShort (bsp->dleafs[i].cluster);
        bsp->dleafs[i].area = LittleShort (bsp->dleafs[i].area);
        for (j=0 ; j<3 ; j++)
        {
            bsp->dleafs[i].mins[j] = LittleShort (bsp->dleafs[i].mins[j]);
            bsp->dleafs[i].maxs[j] = LittleShort (bsp->dleafs[i].maxs[j]);
        }
        
        bsp->dleafs[i].firstleafface = LittleShort (bsp->dleafs[i].firstleafface);
        bsp->dleafs[i].numleaffaces = LittleShort (bsp->dleafs[i].numleaffaces);
        bsp->dleafs[i].firstleafbrush = LittleShort (bsp->dleafs[i].firstleafbrush);
        bsp->dleafs[i].numleafbrushes = LittleShort (bsp->dleafs[i].numleafbrushes);
    }
    
    //
    // leaffaces
    //
    for (i=0 ; i<bsp->numleaffaces ; i++)
        bsp->dleaffaces[i] = LittleShort (bsp->dleaffaces[i]);
    
    //
    // leafbrushes
    //
    for (i=0 ; i<bsp->numleafbrushes ; i++)
        bsp->dleafbrushes[i] = LittleShort (bsp->dleafbrushes[i]);
    
    //
    // surfedges
    //
    for (i=0 ; i<bsp->numsurfedges ; i++)
        bsp->dsurfedges[i] = LittleLong (bsp->dsurfedges[i]);
    
    //
    // edges
    //
    for (i=0 ; i<bsp->numedges ; i++)
    {
        bsp->dedges[i].v[0] = LittleShort (bsp->dedges[i].v[0]);
        bsp->dedges[i].v[1] = LittleShort (bsp->dedges[i].v[1]);
    }
    
    //
    // brushes
    //
    for (i=0 ; i<bsp->numbrushes ; i++)
    {
        bsp->dbrushes[i].firstside = LittleLong (bsp->dbrushes[i].firstside);
        bsp->dbrushes[i].numsides = LittleLong (bsp->dbrushes[i].numsides);
        bsp->dbrushes[i].contents = LittleLong (bsp->dbrushes[i].contents);
    }
    
    //
    // areas
    //
    for (i=0 ; i<bsp->numareas ; i++)
    {
        bsp->dareas[i].numareaportals = LittleLong (bsp->dareas[i].numareaportals);
        bsp->dareas[i].firstareaportal = LittleLong (bsp->dareas[i].firstareaportal);
    }
    
    //
    // areasportals
    //
    for (i=0 ; i<bsp->numareaportals ; i++)
    {
        bsp->dareaportals[i].portalnum = LittleLong (bsp->dareaportals[i].portalnum);
        bsp->dareaportals[i].otherarea = LittleLong (bsp->dareaportals[i].otherarea);
    }
    
    //
    // brushsides
    //
    for (i=0 ; i<bsp->numbrushsides ; i++)
    {
        bsp->dbrushsides[i].planenum = LittleShort (bsp->dbrushsides[i].planenum);
        bsp->dbrushsides[i].texinfo = LittleShort (bsp->dbrushsides[i].texinfo);
    }
    
    //
    // visibility
    //
    if (todisk)
        j = bsp->dvis->numclusters;
    else
        j = LittleLong(bsp->dvis->numclusters);
    bsp->dvis->numclusters = LittleLong (bsp->dvis->numclusters);
    for (i=0 ; i<j ; i++)
    {
        bsp->dvis->bitofs[i][0] = LittleLong (bsp->dvis->bitofs[i][0]);
        bsp->dvis->bitofs[i][1] = LittleLong (bsp->dvis->bitofs[i][1]);
    }
}

/*
=============
Q2_Qbism_SwapBSPFile

Byte swaps all data in a bsp file.
=============
*/
void Q2_Qbism_SwapBSPFile (q2bsp_qbism_t *bsp, qboolean todisk)
{
    int                i, j;
    q2_dmodel_t        *d;
    
    
    // models
    for (i=0 ; i<bsp->nummodels ; i++)
    {
        d = &bsp->dmodels[i];
        
        d->firstface = LittleLong (d->firstface);
        d->numfaces = LittleLong (d->numfaces);
        d->headnode = LittleLong (d->headnode);
        
        for (j=0 ; j<3 ; j++)
        {
            d->mins[j] = LittleFloat(d->mins[j]);
            d->maxs[j] = LittleFloat(d->maxs[j]);
            d->origin[j] = LittleFloat(d->origin[j]);
        }
    }
    
    //
    // vertexes
    //
    for (i=0 ; i<bsp->numvertexes ; i++)
    {
        for (j=0 ; j<3 ; j++)
            bsp->dvertexes[i].point[j] = LittleFloat (bsp->dvertexes[i].point[j]);
    }
    
    //
    // planes
    //
    for (i=0 ; i<bsp->numplanes ; i++)
    {
        for (j=0 ; j<3 ; j++)
            bsp->dplanes[i].normal[j] = LittleFloat (bsp->dplanes[i].normal[j]);
        bsp->dplanes[i].dist = LittleFloat (bsp->dplanes[i].dist);
        bsp->dplanes[i].type = LittleLong (bsp->dplanes[i].type);
    }
    
    //
    // texinfos
    //
    for (i=0 ; i<bsp->numtexinfo ; i++)
    {
        for (j=0 ; j<4 ; j++)
        {
            bsp->texinfo[i].vecs[0][j] = LittleFloat(bsp->texinfo[i].vecs[0][j]);
            bsp->texinfo[i].vecs[1][j] = LittleFloat(bsp->texinfo[i].vecs[1][j]);
        }
        bsp->texinfo[i].flags = LittleLong (bsp->texinfo[i].flags);
        bsp->texinfo[i].value = LittleLong (bsp->texinfo[i].value);
        bsp->texinfo[i].nexttexinfo = LittleLong (bsp->texinfo[i].nexttexinfo);
    }
    
    //
    // faces
    //
    for (i=0 ; i<bsp->numfaces ; i++)
    {
        bsp->dfaces[i].texinfo = LittleLong (bsp->dfaces[i].texinfo);
        bsp->dfaces[i].planenum = LittleLong (bsp->dfaces[i].planenum);
        bsp->dfaces[i].side = LittleLong (bsp->dfaces[i].side);
        bsp->dfaces[i].lightofs = LittleLong (bsp->dfaces[i].lightofs);
        bsp->dfaces[i].firstedge = LittleLong (bsp->dfaces[i].firstedge);
        bsp->dfaces[i].numedges = LittleLong (bsp->dfaces[i].numedges);
    }
    
    //
    // nodes
    //
    for (i=0 ; i<bsp->numnodes ; i++)
    {
        bsp->dnodes[i].planenum = LittleLong (bsp->dnodes[i].planenum);
        for (j=0 ; j<3 ; j++)
        {
            bsp->dnodes[i].mins[j] = LittleFloat (bsp->dnodes[i].mins[j]);
            bsp->dnodes[i].maxs[j] = LittleFloat (bsp->dnodes[i].maxs[j]);
        }
        bsp->dnodes[i].children[0] = LittleLong (bsp->dnodes[i].children[0]);
        bsp->dnodes[i].children[1] = LittleLong (bsp->dnodes[i].children[1]);
        bsp->dnodes[i].firstface = LittleLong (bsp->dnodes[i].firstface);
        bsp->dnodes[i].numfaces = LittleLong (bsp->dnodes[i].numfaces);
    }
    
    //
    // leafs
    //
    for (i=0 ; i<bsp->numleafs ; i++)
    {
        bsp->dleafs[i].contents = LittleLong (bsp->dleafs[i].contents);
        bsp->dleafs[i].cluster = LittleLong (bsp->dleafs[i].cluster);
        bsp->dleafs[i].area = LittleLong (bsp->dleafs[i].area);
        for (j=0 ; j<3 ; j++)
        {
            bsp->dleafs[i].mins[j] = LittleFloat (bsp->dleafs[i].mins[j]);
            bsp->dleafs[i].maxs[j] = LittleFloat (bsp->dleafs[i].maxs[j]);
        }
        
        bsp->dleafs[i].firstleafface = LittleLong (bsp->dleafs[i].firstleafface);
        bsp->dleafs[i].numleaffaces = LittleLong (bsp->dleafs[i].numleaffaces);
        bsp->dleafs[i].firstleafbrush = LittleLong (bsp->dleafs[i].firstleafbrush);
        bsp->dleafs[i].numleafbrushes = LittleLong (bsp->dleafs[i].numleafbrushes);
    }
    
    //
    // leaffaces
    //
    for (i=0 ; i<bsp->numleaffaces ; i++)
        bsp->dleaffaces[i] = LittleLong (bsp->dleaffaces[i]);
    
    //
    // leafbrushes
    //
    for (i=0 ; i<bsp->numleafbrushes ; i++)
        bsp->dleafbrushes[i] = LittleLong (bsp->dleafbrushes[i]);
    
    //
    // surfedges
    //
    for (i=0 ; i<bsp->numsurfedges ; i++)
        bsp->dsurfedges[i] = LittleLong (bsp->dsurfedges[i]);
    
    //
    // edges
    //
    for (i=0 ; i<bsp->numedges ; i++)
    {
        bsp->dedges[i].v[0] = LittleLong (bsp->dedges[i].v[0]);
        bsp->dedges[i].v[1] = LittleLong (bsp->dedges[i].v[1]);
    }
    
    //
    // brushes
    //
    for (i=0 ; i<bsp->numbrushes ; i++)
    {
        bsp->dbrushes[i].firstside = LittleLong (bsp->dbrushes[i].firstside);
        bsp->dbrushes[i].numsides = LittleLong (bsp->dbrushes[i].numsides);
        bsp->dbrushes[i].contents = LittleLong (bsp->dbrushes[i].contents);
    }
    
    //
    // areas
    //
    for (i=0 ; i<bsp->numareas ; i++)
    {
        bsp->dareas[i].numareaportals = LittleLong (bsp->dareas[i].numareaportals);
        bsp->dareas[i].firstareaportal = LittleLong (bsp->dareas[i].firstareaportal);
    }
    
    //
    // areasportals
    //
    for (i=0 ; i<bsp->numareaportals ; i++)
    {
        bsp->dareaportals[i].portalnum = LittleLong (bsp->dareaportals[i].portalnum);
        bsp->dareaportals[i].otherarea = LittleLong (bsp->dareaportals[i].otherarea);
    }
    
    //
    // brushsides
    //
    for (i=0 ; i<bsp->numbrushsides ; i++)
    {
        bsp->dbrushsides[i].planenum = LittleLong (bsp->dbrushsides[i].planenum);
        bsp->dbrushsides[i].texinfo = LittleLong (bsp->dbrushsides[i].texinfo);
    }
    
    //
    // visibility
    //
    if (todisk)
        j = bsp->dvis->numclusters;
    else
        j = LittleLong(bsp->dvis->numclusters);
    bsp->dvis->numclusters = LittleLong (bsp->dvis->numclusters);
    for (i=0 ; i<j ; i++)
    {
        bsp->dvis->bitofs[i][0] = LittleLong (bsp->dvis->bitofs[i][0]);
        bsp->dvis->bitofs[i][1] = LittleLong (bsp->dvis->bitofs[i][1]);
    }
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
    if (bspdata->version == &bspver_q2) {
        q2bsp_t *bsp = &bspdata->data.q2bsp;
        Q2_SwapBSPFile(bsp, swap == TO_DISK);
        return;
    } else if (bspdata->version == &bspver_qbism) {
        q2bsp_qbism_t *bsp = &bspdata->data.q2bsp_qbism;
        Q2_Qbism_SwapBSPFile(bsp, swap == TO_DISK);
        return;
    }
    
    if (bspdata->version == &bspver_q1 || bspdata->version == &bspver_h2 || bspdata->version == &bspver_hl) {
        bsp29_t *bsp = &bspdata->data.bsp29;

        SwapBSPVertexes(bsp->numvertexes, bsp->dvertexes);
        SwapBSPPlanes(bsp->numplanes, bsp->dplanes);
        SwapBSPTexinfo(bsp->numtexinfo, bsp->texinfo);
        SwapBSP29Faces(bsp->numfaces, bsp->dfaces);
        SwapBSP29Nodes(bsp->numnodes, bsp->dnodes);
        SwapBSP29Leafs(bsp->numleafs, bsp->dleafs);
        SwapBSP29Clipnodes(bsp->numclipnodes, bsp->dclipnodes);
        SwapBSPMiptex(bsp->texdatasize, bsp->dtexdata, swap);
        SwapBSP29Marksurfaces(bsp->nummarksurfaces, bsp->dmarksurfaces);
        SwapBSPSurfedges(bsp->numsurfedges, bsp->dsurfedges);
        SwapBSP29Edges(bsp->numedges, bsp->dedges);
        if (bspdata->version == &bspver_h2) {
            SwapBSPModels(bsp->nummodels, bsp->dmodels_h2);
        } else {
            SwapBSPModels(bsp->nummodels, bsp->dmodels_q);
        }

        return;
    }

    if (bspdata->version == &bspver_bsp2rmq || bspdata->version == &bspver_h2bsp2rmq) {
        bsp2rmq_t *bsp = &bspdata->data.bsp2rmq;

        SwapBSPVertexes(bsp->numvertexes, bsp->dvertexes);
        SwapBSPPlanes(bsp->numplanes, bsp->dplanes);
        SwapBSPTexinfo(bsp->numtexinfo, bsp->texinfo);
        SwapBSP2Faces(bsp->numfaces, bsp->dfaces);
        SwapBSP2rmqNodes(bsp->numnodes, bsp->dnodes);
        SwapBSP2rmqLeafs(bsp->numleafs, bsp->dleafs);
        SwapBSP2Clipnodes(bsp->numclipnodes, bsp->dclipnodes);
        SwapBSPMiptex(bsp->texdatasize, bsp->dtexdata, swap);
        SwapBSP2Marksurfaces(bsp->nummarksurfaces, bsp->dmarksurfaces);
        SwapBSPSurfedges(bsp->numsurfedges, bsp->dsurfedges);
        SwapBSP2Edges(bsp->numedges, bsp->dedges);
        if (bspdata->version == &bspver_h2bsp2rmq) {
            SwapBSPModels(bsp->nummodels, bsp->dmodels_h2);
        } else {
            SwapBSPModels(bsp->nummodels, bsp->dmodels_q);
        }

        return;
    }

    if (bspdata->version == &bspver_bsp2 || bspdata->version == &bspver_h2bsp2) {
        bsp2_t *bsp = &bspdata->data.bsp2;

        SwapBSPVertexes(bsp->numvertexes, bsp->dvertexes);
        SwapBSPPlanes(bsp->numplanes, bsp->dplanes);
        SwapBSPTexinfo(bsp->numtexinfo, bsp->texinfo);
        SwapBSP2Faces(bsp->numfaces, bsp->dfaces);
        SwapBSP2Nodes(bsp->numnodes, bsp->dnodes);
        SwapBSP2Leafs(bsp->numleafs, bsp->dleafs);
        SwapBSP2Clipnodes(bsp->numclipnodes, bsp->dclipnodes);
        SwapBSPMiptex(bsp->texdatasize, bsp->dtexdata, swap);
        SwapBSP2Marksurfaces(bsp->nummarksurfaces, bsp->dmarksurfaces);
        SwapBSPSurfedges(bsp->numsurfedges, bsp->dsurfedges);
        SwapBSP2Edges(bsp->numedges, bsp->dedges);
        if (bspdata->version == &bspver_h2bsp2) {
            SwapBSPModels(bsp->nummodels, bsp->dmodels_h2);
        } else {
            SwapBSPModels(bsp->nummodels, bsp->dmodels_q);
        }

        return;
    }

    Error("Unsupported BSP version: %d", bspdata->version);
}

/*
 * =========================================================================
 * BSP Format Conversion (ver. 29 <-> MBSP)
 * =========================================================================
 */

static dmodelh2_t *
BSPQ1toH2_Models(const dmodelq1_t *dmodelsq1, const int nummodels) {
    const dmodelq1_t *in = dmodelsq1;
    dmodelh2_t *out = static_cast<dmodelh2_t *>(calloc(nummodels, sizeof(dmodelh2_t)));
    int i, j;

    for (i = 0; i < nummodels; i++)
    {
        for (j = 0; j < 3; j++)
        {
            out[i].mins[j] = in[i].mins[j];
            out[i].maxs[j] = in[i].maxs[j];
            out[i].origin[j] = in[i].origin[j];
        }
        for (j = 0; j < MAX_MAP_HULLS_Q1; j++)
            out[i].headnode[j] = in[i].headnode[j];
        for (     ; j < MAX_MAP_HULLS_H2; j++)
            out[i].headnode[j] = 0;
        out[i].visleafs = in[i].visleafs;
        out[i].firstface = in[i].firstface;
        out[i].numfaces = in[i].numfaces;
    }
    return out;
}

static dmodelq1_t *
BSPH2toQ1_Models(const dmodelh2_t *dmodelsh2, int nummodels) {
    const dmodelh2_t *in = dmodelsh2;
    dmodelq1_t *out = static_cast<dmodelq1_t *>(calloc(nummodels, sizeof(dmodelq1_t)));
    int i, j;

    for (i = 0; i < nummodels; i++)
    {
        for (j = 0; j < 3; j++)
        {
            out[i].mins[j] = in[i].mins[j];
            out[i].maxs[j] = in[i].maxs[j];
            out[i].origin[j] = in[i].origin[j];
        }
        for (j = 0; j < MAX_MAP_HULLS_Q1; j++)
            out[i].headnode[j] = in[i].headnode[j];
        out[i].visleafs = in[i].visleafs;
        out[i].firstface = in[i].firstface;
        out[i].numfaces = in[i].numfaces;
    }
    return out;
}

static mleaf_t *
BSP29toM_Leafs(const bsp29_dleaf_t *dleafs29, int numleafs) {
    const bsp29_dleaf_t *dleaf29 = dleafs29;
    mleaf_t *newdata, *mleaf;
    int i, j;
    
    newdata = mleaf = (mleaf_t *)calloc(numleafs, sizeof(*mleaf));
    
    for (i = 0; i < numleafs; i++, dleaf29++, mleaf++) {
        mleaf->contents = dleaf29->contents;
        mleaf->visofs = dleaf29->visofs;
        for (j = 0; j < 3; j++) {
            mleaf->mins[j] = dleaf29->mins[j];
            mleaf->maxs[j] = dleaf29->maxs[j];
        }
        mleaf->firstmarksurface = dleaf29->firstmarksurface;
        mleaf->nummarksurfaces = dleaf29->nummarksurfaces;
        for (j = 0; j < NUM_AMBIENTS; j++)
            mleaf->ambient_level[j] = dleaf29->ambient_level[j];
    }
    
    return newdata;
}

static bool
OverflowsInt16(float input) {
    constexpr float minvalue = static_cast<float>(INT16_MIN);
    constexpr float maxvalue = static_cast<float>(INT16_MAX);
    
    if (input < minvalue) {
        return true;
    }
    if (input > maxvalue) {
        return true;
    }
    return false;
}

static bool
OverflowsInt16(int32_t input) {
    if (input < INT16_MIN) {
        return true;
    }
    if (input > INT16_MAX) {
        return true;
    }
    return false;
}

static constexpr bool
OverflowsUint16(uint32_t input) {
    if (input > UINT16_MAX) {
        return true;
    }
    return false;
}

static_assert(!OverflowsUint16(65535));
static_assert( OverflowsUint16(65536));

static bool
MBSPto29_Leafs_Validate(const mleaf_t *mleafs, int numleafs) {
    const mleaf_t *mleaf = mleafs;
    
    for (int i = 0; i < numleafs; i++, mleaf++) {
        for (int j = 0; j < 3; j++) {
            const float min_j = floor(mleaf->mins[j]);
            const float max_j = ceil(mleaf->maxs[j]);

            if (OverflowsInt16(min_j) || OverflowsInt16(max_j)) {
                return false;
            }
        }
        if (OverflowsUint16(mleaf->firstmarksurface)
            || OverflowsUint16(mleaf->nummarksurfaces)) {
            return false;
        }
    }    
    return true;
}

static bsp29_dleaf_t *
MBSPto29_Leafs(const mleaf_t *mleafs, int numleafs) {
    const mleaf_t *mleaf = mleafs;
    bsp29_dleaf_t *newdata, *dleaf29;
    int i, j;
    
    newdata = dleaf29 = (bsp29_dleaf_t *)calloc(numleafs, sizeof(*dleaf29));
    
    for (i = 0; i < numleafs; i++, mleaf++, dleaf29++) {
        dleaf29->contents = mleaf->contents;
        dleaf29->visofs = mleaf->visofs;
        for (j = 0; j < 3; j++) {
            dleaf29->mins[j] = floor(mleaf->mins[j]);
            dleaf29->maxs[j] = ceil(mleaf->maxs[j]);
        }
        dleaf29->firstmarksurface = mleaf->firstmarksurface;
        dleaf29->nummarksurfaces = mleaf->nummarksurfaces;
        for (j = 0; j < NUM_AMBIENTS; j++)
            dleaf29->ambient_level[j] = mleaf->ambient_level[j];
    }
    
    return newdata;
}

static gtexinfo_t *
BSP29toM_Texinfo(const texinfo_t *texinfos, int numtexinfo) {
    const texinfo_t *texinfo29 = texinfos;
    gtexinfo_t *newdata, *mtexinfo;
    int i, j, k;
    
    newdata = mtexinfo = (gtexinfo_t *)calloc(numtexinfo, sizeof(*mtexinfo));
    
    for (i = 0; i < numtexinfo; i++, texinfo29++, mtexinfo++) {
        for (j=0; j<2; j++)
            for (k=0; k<4; k++)
                mtexinfo->vecs[j][k] = texinfo29->vecs[j][k];
        mtexinfo->flags = texinfo29->flags;
        mtexinfo->miptex = texinfo29->miptex;
    }
    
    return newdata;
}

static texinfo_t *
MBSPto29_Texinfo(const gtexinfo_t *mtexinfos, int numtexinfo) {
    const gtexinfo_t *mtexinfo = mtexinfos;
    texinfo_t *newdata, *texinfo29;
    int i, j, k;
    
    newdata = texinfo29 = (texinfo_t *)calloc(numtexinfo, sizeof(*texinfo29));
    
    for (i = 0; i < numtexinfo; i++, texinfo29++, mtexinfo++) {
        for (j=0; j<2; j++)
            for (k=0; k<4; k++)
                texinfo29->vecs[j][k] = mtexinfo->vecs[j][k];
        texinfo29->flags = mtexinfo->flags;
        texinfo29->miptex = mtexinfo->miptex;
    }
    
    return newdata;
}

/*
 * =========================================================================
 * BSP Format Conversion (ver. q2 <-> MBSP)
 * =========================================================================
 */

static dmodelh2_t *
Q2BSPtoM_Models(const q2_dmodel_t *dmodelsq2, int nummodels) {
    const q2_dmodel_t *dmodelq2 = dmodelsq2;
    dmodelh2_t *newdata, *dmodelh2;
    int i, j;
    
    newdata = dmodelh2 = (dmodelh2_t *)calloc(nummodels, sizeof(*dmodelh2));
    
    for (i = 0; i < nummodels; i++, dmodelq2++, dmodelh2++) {
        for (j = 0; j<3; j++) {
            dmodelh2->mins[j] = dmodelq2->mins[j];
            dmodelh2->maxs[j] = dmodelq2->maxs[j];
            dmodelh2->origin[j] = dmodelq2->origin[j];
        }
        dmodelh2->headnode[0] = dmodelq2->headnode;
        dmodelh2->visleafs = 0;
        dmodelh2->firstface = dmodelq2->firstface;
        dmodelh2->numfaces = dmodelq2->numfaces;
    }
    
    return newdata;
}

static uint8_t *
Q2BSPtoM_CopyVisData(const dvis_t *dvisq2, int vissize, int *outvissize, mleaf_t *leafs, int numleafs) {

    if (!*outvissize) {
        return ((uint8_t *) dvisq2);
    }

    // FIXME: assumes PHS always follows PVS.
    int32_t phs_start = INT_MAX, pvs_start = INT_MAX;
    size_t header_offset = sizeof(dvis_t) + (sizeof(int32_t) * dvisq2->numclusters * 2);

    for (int32_t i = 0; i < dvisq2->numclusters; i++) {
        pvs_start = std::min(pvs_start, (int32_t) (dvisq2->bitofs[i][DVIS_PVS]));
        phs_start = std::min(phs_start, (int32_t) (dvisq2->bitofs[i][DVIS_PHS] - header_offset));

        for (int32_t l = 0; l < numleafs; l++) {
            if (leafs[l].cluster == i) {
                leafs[l].visofs = dvisq2->bitofs[i][DVIS_PVS] - header_offset;
            }
        }
    }

    // cut off the PHS and header
    *outvissize -= header_offset + ((*outvissize - header_offset) - phs_start);

    uint8_t *vis = (uint8_t *) calloc(1, *outvissize);
    memcpy(vis, ((uint8_t *) dvisq2) + pvs_start, *outvissize);
    return vis;
}

static q2_dmodel_t *
MBSPtoQ2_Models(const dmodelh2_t *dmodelsh2, int nummodels) {
    const dmodelh2_t *dmodelh2 = dmodelsh2;
    q2_dmodel_t *newdata, *dmodelq2;
    int i, j;
    
    newdata = dmodelq2 = (q2_dmodel_t *)calloc(nummodels, sizeof(*dmodelq2));
    
    for (i = 0; i < nummodels; i++, dmodelh2++, dmodelq2++) {
        for (j = 0; j<3; j++) {
            dmodelq2->mins[j] = dmodelh2->mins[j];
            dmodelq2->maxs[j] = dmodelh2->maxs[j];
            dmodelq2->origin[j] = dmodelh2->origin[j];
        }
        dmodelq2->headnode = dmodelh2->headnode[0];
        dmodelq2->firstface = dmodelh2->firstface;
        dmodelq2->numfaces = dmodelh2->numfaces;
    }
    
    return newdata;
}


/*
================
CalcPHS

Calculate the PHS (Potentially Hearable Set)
by ORing together all the PVS visible from a leaf
================
*/
static std::vector<uint8_t> CalcPHS(int32_t portalclusters, const uint8_t *visdata, int *visdatasize, int32_t bitofs[][2])
{
    const int32_t leafbytes = (portalclusters + 7) >> 3;
    const int32_t leaflongs = leafbytes / sizeof(long);
    std::vector<uint8_t> compressed_phs;
    uint8_t *uncompressed = (uint8_t *) calloc(1, leafbytes);
    uint8_t *uncompressed_2 = (uint8_t *) calloc(1, leafbytes);
    uint8_t *compressed = (uint8_t *) calloc(1, leafbytes * 2);
    uint8_t *uncompressed_orig = (uint8_t *) calloc(1, leafbytes);

    printf ("Building PHS...\n");

    int32_t count = 0;
    for (int32_t i = 0; i < portalclusters; i++)
    {
        const uint8_t *scan = &visdata[bitofs[i][DVIS_PVS]];

        DecompressRow(scan, leafbytes, uncompressed);
        memset(uncompressed_orig, 0, leafbytes);
        memcpy(uncompressed_orig, uncompressed, leafbytes);

        scan = uncompressed_orig;

        for (int32_t j = 0; j < leafbytes; j++)
        {
            uint8_t bitbyte = scan[j];
            if (!bitbyte)
                continue;
            for (int32_t k = 0; k < 8; k++)
            {
                if (! (bitbyte & (1<<k)) )
                    continue;
                // OR this pvs row into the phs
                int32_t index = ((j<<3)+k);
                if (index >= portalclusters)
                    Error ("Bad bit in PVS");	// pad bits should be 0
                const uint8_t *src_compressed = &visdata[bitofs[index][DVIS_PVS]];
                DecompressRow(src_compressed, leafbytes, uncompressed_2);
                const long *src = (long *) uncompressed_2;
                long *dest = (long *) uncompressed;
                for (int32_t l = 0; l < leaflongs; l++)
                    ((long *)uncompressed)[l] |= src[l];
            }
        }
        for (int32_t j = 0; j < portalclusters; j++)
            if (uncompressed[j>>3] & (1<<(j&7)) )
                count++;

        //
        // compress the bit string
        //
        int32_t j = CompressRow (uncompressed, leafbytes, compressed);

        bitofs[i][DVIS_PHS] = compressed_phs.size();

        compressed_phs.insert(compressed_phs.end(), compressed, compressed + j);
    }

    free(uncompressed);
    free(uncompressed_2);
    free(compressed);
    free(uncompressed_orig);

    printf ("Average clusters hearable: %i\n", count / portalclusters);

    return compressed_phs;
}

static dvis_t *
MBSPtoQ2_CopyVisData(const uint8_t *visdata, int *visdatasize, int numleafs, const mleaf_t *leafs) {
    int32_t num_clusters = 0;
    
    for (int32_t i = 0; i < numleafs; i++) {
        num_clusters = std::max(num_clusters, leafs[i].cluster + 1);
    }

    size_t vis_offset = sizeof(dvis_t) + (sizeof(int32_t) * num_clusters * 2);
    dvis_t *vis = (dvis_t *)calloc(1, vis_offset + *visdatasize);

    vis->numclusters = num_clusters;

    // the leaves are already using a per-cluster visofs, so just find one matching
    // cluster and note it down under bitofs.
    // we're also not worrying about PHS currently.
    for (int32_t i = 0; i < num_clusters; i++) {
        for (int32_t l = 0; l < numleafs; l++) {
            if (leafs[l].cluster == i) {
                // copy PVS visofs
                vis->bitofs[i][DVIS_PVS] = leafs[l].visofs;
                break;
            }
        }
    }

    std::vector<uint8_t> phs = CalcPHS(num_clusters, visdata, visdatasize, vis->bitofs);

    vis = (dvis_t *) realloc(vis, vis_offset + *visdatasize + phs.size());

    // offset the pvs/phs properly
    for (int32_t i = 0; i < num_clusters; i++) {
          vis->bitofs[i][DVIS_PVS] += vis_offset;
          vis->bitofs[i][DVIS_PHS] += vis_offset + *visdatasize;
    }

    memcpy(((uint8_t *) vis) + vis_offset, visdata, *visdatasize);
    *visdatasize += vis_offset;

    memcpy(((uint8_t *) vis) + *visdatasize, phs.data(), phs.size());
    *visdatasize += phs.size();

    return vis;
}

static mleaf_t *
Q2BSPtoM_Leafs(const q2_dleaf_t *dleafsq2, int numleafs) {
    const q2_dleaf_t *dleafq2 = dleafsq2;
    mleaf_t *newdata, *mleaf;
    int i, j;
    
    newdata = mleaf = (mleaf_t *)calloc(numleafs, sizeof(*mleaf));
    
    for (i = 0; i < numleafs; i++, dleafq2++, mleaf++) {
        mleaf->contents = dleafq2->contents;
        mleaf->cluster = dleafq2->cluster;
        mleaf->area = dleafq2->area;
        
        for (j = 0; j < 3; j++) {
            mleaf->mins[j] = dleafq2->mins[j];
            mleaf->maxs[j] = dleafq2->maxs[j];
        }
        mleaf->firstmarksurface = dleafq2->firstleafface;
        mleaf->nummarksurfaces = dleafq2->numleaffaces;
        
        mleaf->firstleafbrush = dleafq2->firstleafbrush;
        mleaf->numleafbrushes = dleafq2->numleafbrushes;
    }
    
    return newdata;
}

static mleaf_t *
Q2BSP_QBSPtoM_Leafs(const q2_dleaf_qbism_t *dleafsq2, int numleafs) {
    const q2_dleaf_qbism_t *dleafq2 = dleafsq2;
    mleaf_t *newdata, *mleaf;
    int i, j;
    
    newdata = mleaf = (mleaf_t *)calloc(numleafs, sizeof(*mleaf));
    
    for (i = 0; i < numleafs; i++, dleafq2++, mleaf++) {
        mleaf->contents = dleafq2->contents;
        mleaf->cluster = dleafq2->cluster;
        mleaf->area = dleafq2->area;
        
        for (j = 0; j < 3; j++) {
            mleaf->mins[j] = dleafq2->mins[j];
            mleaf->maxs[j] = dleafq2->maxs[j];
        }
        mleaf->firstmarksurface = dleafq2->firstleafface;
        mleaf->nummarksurfaces = dleafq2->numleaffaces;
        
        mleaf->firstleafbrush = dleafq2->firstleafbrush;
        mleaf->numleafbrushes = dleafq2->numleafbrushes;
    }
    
    return newdata;
}

static q2_dleaf_t *
MBSPtoQ2_Leafs(const mleaf_t *mleafs, int numleafs) {
    const mleaf_t *mleaf = mleafs;
    q2_dleaf_t *newdata, *dleafq2;
    int i, j;
    
    newdata = dleafq2 = (q2_dleaf_t *)calloc(numleafs, sizeof(*dleafq2));
    
    for (i = 0; i < numleafs; i++, mleaf++, dleafq2++) {
        dleafq2->contents = mleaf->contents;
        dleafq2->cluster = mleaf->cluster;
        dleafq2->area = mleaf->area;
        
        for (j = 0; j < 3; j++) {
            dleafq2->mins[j] = mleaf->mins[j];
            dleafq2->maxs[j] = mleaf->maxs[j];
        }
        dleafq2->firstleafface = mleaf->firstmarksurface;
        dleafq2->numleaffaces = mleaf->nummarksurfaces;
        
        dleafq2->firstleafbrush = mleaf->firstleafbrush;
        dleafq2->numleafbrushes = mleaf->numleafbrushes;
    }
    
    return newdata;
}

static q2_dleaf_qbism_t *
MBSPtoQ2_Qbism_Leafs(const mleaf_t *mleafs, int numleafs) {
    const mleaf_t *mleaf = mleafs;
    q2_dleaf_qbism_t *newdata, *dleafq2;
    int i, j;
    
    newdata = dleafq2 = (q2_dleaf_qbism_t *)calloc(numleafs, sizeof(*dleafq2));
    
    for (i = 0; i < numleafs; i++, mleaf++, dleafq2++) {
        dleafq2->contents = mleaf->contents;
        dleafq2->cluster = mleaf->cluster;
        dleafq2->area = mleaf->area;
        
        for (j = 0; j < 3; j++) {
            dleafq2->mins[j] = mleaf->mins[j];
            dleafq2->maxs[j] = mleaf->maxs[j];
        }
        dleafq2->firstleafface = mleaf->firstmarksurface;
        dleafq2->numleaffaces = mleaf->nummarksurfaces;
        
        dleafq2->firstleafbrush = mleaf->firstleafbrush;
        dleafq2->numleafbrushes = mleaf->numleafbrushes;
    }
    
    return newdata;
}

static bsp2_dnode_t *
Q2BSPto2_Nodes(const q2_dnode_t *dnodesq2, int numnodes) {
    const q2_dnode_t *dnodeq2 = dnodesq2;
    bsp2_dnode_t *newdata, *dnode2;
    int i, j;
    
    newdata = dnode2 = static_cast<bsp2_dnode_t *>(malloc(numnodes * sizeof(*dnode2)));
    
    for (i = 0; i < numnodes; i++, dnodeq2++, dnode2++) {
        dnode2->planenum = dnodeq2->planenum;
        dnode2->children[0] = dnodeq2->children[0];
        dnode2->children[1] = dnodeq2->children[1];
        for (j = 0; j < 3; j++) {
            dnode2->mins[j] = dnodeq2->mins[j];
            dnode2->maxs[j] = dnodeq2->maxs[j];
        }
        dnode2->firstface = dnodeq2->firstface;
        dnode2->numfaces = dnodeq2->numfaces;
    }
    
    return newdata;
}

static q2_dnode_t *
BSP2toQ2_Nodes(const bsp2_dnode_t *dnodes2, int numnodes) {
    const bsp2_dnode_t *dnode2 = dnodes2;
    q2_dnode_t *newdata, *dnodeq2;
    int i, j;
    
    newdata = dnodeq2 = static_cast<q2_dnode_t *>(malloc(numnodes * sizeof(*dnodeq2)));
    
    for (i = 0; i < numnodes; i++, dnode2++, dnodeq2++) {
        dnodeq2->planenum = dnode2->planenum;
        dnodeq2->children[0] = dnode2->children[0];
        dnodeq2->children[1] = dnode2->children[1];
        for (j = 0; j < 3; j++) {
            dnodeq2->mins[j] = dnode2->mins[j];
            dnodeq2->maxs[j] = dnode2->maxs[j];
        }
        dnodeq2->firstface = dnode2->firstface;
        dnodeq2->numfaces = dnode2->numfaces;
    }
    
    return newdata;
}

static bsp2_dface_t *
Q2BSPto2_Faces(const q2_dface_t *dfacesq2, int numfaces) {
    const q2_dface_t *dfaceq2 = dfacesq2;
    bsp2_dface_t *newdata, *dface2;
    int i, j;
    
    newdata = dface2 = static_cast<bsp2_dface_t *>(malloc(numfaces * sizeof(*dface2)));
    
    for (i = 0; i < numfaces; i++, dfaceq2++, dface2++) {
        dface2->planenum = dfaceq2->planenum;
        dface2->side = dfaceq2->side;
        dface2->firstedge = dfaceq2->firstedge;
        dface2->numedges = dfaceq2->numedges;
        dface2->texinfo = dfaceq2->texinfo;
        for (j = 0; j < MAXLIGHTMAPS; j++)
            dface2->styles[j] = dfaceq2->styles[j];
        dface2->lightofs = dfaceq2->lightofs;
    }
    
    return newdata;
}

static bsp2_dface_t *
Q2BSP_QBSPto2_Faces(const q2_dface_qbism_t *dfacesq2, int numfaces) {
    const q2_dface_qbism_t *dfaceq2 = dfacesq2;
    bsp2_dface_t *newdata, *dface2;
    int i, j;
    
    newdata = dface2 = static_cast<bsp2_dface_t *>(malloc(numfaces * sizeof(*dface2)));
    
    for (i = 0; i < numfaces; i++, dfaceq2++, dface2++) {
        dface2->planenum = dfaceq2->planenum;
        dface2->side = dfaceq2->side;
        dface2->firstedge = dfaceq2->firstedge;
        dface2->numedges = dfaceq2->numedges;
        dface2->texinfo = dfaceq2->texinfo;
        for (j = 0; j < MAXLIGHTMAPS; j++)
            dface2->styles[j] = dfaceq2->styles[j];
        dface2->lightofs = dfaceq2->lightofs;
    }
    
    return newdata;
}

static q2_dface_t *
BSP2toQ2_Faces(const bsp2_dface_t *dfaces2, int numfaces) {
    const bsp2_dface_t *dface2 = dfaces2;
    q2_dface_t *newdata, *dfaceq2;
    int i, j;
    
    newdata = dfaceq2 = static_cast<q2_dface_t *>(malloc(numfaces * sizeof(*dfaceq2)));
    
    for (i = 0; i < numfaces; i++, dface2++, dfaceq2++) {
        dfaceq2->planenum = dface2->planenum;
        dfaceq2->side = dface2->side;
        dfaceq2->firstedge = dface2->firstedge;
        dfaceq2->numedges = dface2->numedges;
        dfaceq2->texinfo = dface2->texinfo;
        for (j = 0; j < MAXLIGHTMAPS; j++)
            dfaceq2->styles[j] = dface2->styles[j];
        dfaceq2->lightofs = dface2->lightofs;
    }
    
    return newdata;
}

static q2_dface_qbism_t *
BSP2toQ2_Qbism_Faces(const bsp2_dface_t *dfaces2, int numfaces) {
    const bsp2_dface_t *dface2 = dfaces2;
    q2_dface_qbism_t *newdata, *dfaceq2;
    int i, j;
    
    newdata = dfaceq2 = static_cast<q2_dface_qbism_t *>(malloc(numfaces * sizeof(*dfaceq2)));
    
    for (i = 0; i < numfaces; i++, dface2++, dfaceq2++) {
        dfaceq2->planenum = dface2->planenum;
        dfaceq2->side = dface2->side;
        dfaceq2->firstedge = dface2->firstedge;
        dfaceq2->numedges = dface2->numedges;
        dfaceq2->texinfo = dface2->texinfo;
        for (j = 0; j < MAXLIGHTMAPS; j++)
            dfaceq2->styles[j] = dface2->styles[j];
        dfaceq2->lightofs = dface2->lightofs;
    }
    
    return newdata;
}

static gtexinfo_t *
Q2BSPtoM_Texinfo(const q2_texinfo_t *dtexinfosq2, int numtexinfos) {
    const q2_texinfo_t *dtexinfoq2 = dtexinfosq2;
    gtexinfo_t *newdata, *dtexinfo2;
    int i, j, k;
    
    newdata = dtexinfo2 = static_cast<gtexinfo_t *>(malloc(numtexinfos * sizeof(*dtexinfo2)));
    
    for (i = 0; i < numtexinfos; i++, dtexinfoq2++, dtexinfo2++) {
        for (j = 0; j < 2; j++)
            for (k = 0; k < 4; k++)
                dtexinfo2->vecs[j][k] = dtexinfoq2->vecs[j][k];
        dtexinfo2->flags = dtexinfoq2->flags;
        memcpy(dtexinfo2->texture, dtexinfoq2->texture, sizeof(dtexinfo2->texture));
        dtexinfo2->value = dtexinfoq2->value;
        dtexinfo2->nexttexinfo = dtexinfoq2->nexttexinfo;
    }
    
    return newdata;
}

static q2_texinfo_t *
MBSPtoQ2_Texinfo(const gtexinfo_t *dtexinfos2, int numtexinfos) {
    const gtexinfo_t *dtexinfo2 = dtexinfos2;
    q2_texinfo_t *newdata, *dtexinfoq2;
    int i, j, k;
    
    newdata = dtexinfoq2 = static_cast<q2_texinfo_t *>(malloc(numtexinfos * sizeof(*dtexinfoq2)));
    
    for (i = 0; i < numtexinfos; i++, dtexinfo2++, dtexinfoq2++) {
        for (j = 0; j < 2; j++)
            for (k = 0; k < 4; k++)
                dtexinfoq2->vecs[j][k] = dtexinfo2->vecs[j][k];
        dtexinfoq2->flags = dtexinfo2->flags;
        memcpy(dtexinfoq2->texture, dtexinfo2->texture, sizeof(dtexinfo2->texture));
        dtexinfoq2->value = dtexinfo2->value;
        dtexinfoq2->nexttexinfo = dtexinfo2->nexttexinfo;
    }
    
    return newdata;
}

/*
 * =========================================================================
 * BSP Format Conversion (ver. 2rmq <-> MBSP)
 * =========================================================================
 */

static mleaf_t *
BSP2rmqtoM_Leafs(const bsp2rmq_dleaf_t *dleafs2rmq, int numleafs) {
    const bsp2rmq_dleaf_t *dleaf2rmq = dleafs2rmq;
    mleaf_t *newdata, *mleaf;
    int i, j;
    
    newdata = mleaf = (mleaf_t *)calloc(numleafs, sizeof(*mleaf));
    
    for (i = 0; i < numleafs; i++, dleaf2rmq++, mleaf++) {
        mleaf->contents = dleaf2rmq->contents;
        mleaf->visofs = dleaf2rmq->visofs;
        for (j = 0; j < 3; j++) {
            mleaf->mins[j] = dleaf2rmq->mins[j];
            mleaf->maxs[j] = dleaf2rmq->maxs[j];
        }
        mleaf->firstmarksurface = dleaf2rmq->firstmarksurface;
        mleaf->nummarksurfaces = dleaf2rmq->nummarksurfaces;
        for (j = 0; j < NUM_AMBIENTS; j++)
            mleaf->ambient_level[j] = dleaf2rmq->ambient_level[j];
    }
    
    return newdata;
}

static bsp2rmq_dleaf_t *
MBSPto2rmq_Leafs(const mleaf_t *mleafs, int numleafs) {
    const mleaf_t *mleaf = mleafs;
    bsp2rmq_dleaf_t *newdata, *dleaf2rmq;
    int i, j;
    
    newdata = dleaf2rmq = (bsp2rmq_dleaf_t *)calloc(numleafs, sizeof(*dleaf2rmq));
    
    for (i = 0; i < numleafs; i++, mleaf++, dleaf2rmq++) {
        dleaf2rmq->contents = mleaf->contents;
        dleaf2rmq->visofs = mleaf->visofs;
        for (j = 0; j < 3; j++) {
            dleaf2rmq->mins[j] = mleaf->mins[j];
            dleaf2rmq->maxs[j] = mleaf->maxs[j];
        }
        dleaf2rmq->firstmarksurface = mleaf->firstmarksurface;
        dleaf2rmq->nummarksurfaces = mleaf->nummarksurfaces;
        for (j = 0; j < NUM_AMBIENTS; j++)
            dleaf2rmq->ambient_level[j] = mleaf->ambient_level[j];
    }
    
    return newdata;
}

/*
 * =========================================================================
 * BSP Format Conversion (ver. 2 <-> MBSP)
 * =========================================================================
 */

static mleaf_t *
BSP2toM_Leafs(const bsp2_dleaf_t *dleafs2, int numleafs) {
    const bsp2_dleaf_t *dleaf2 = dleafs2;
    mleaf_t *newdata, *mleaf;
    int i, j;
    
    newdata = mleaf = (mleaf_t *)calloc(numleafs, sizeof(*mleaf));
    
    for (i = 0; i < numleafs; i++, dleaf2++, mleaf++) {
        mleaf->contents = dleaf2->contents;
        mleaf->visofs = dleaf2->visofs;
        for (j = 0; j < 3; j++) {
            mleaf->mins[j] = dleaf2->mins[j];
            mleaf->maxs[j] = dleaf2->maxs[j];
        }
        mleaf->firstmarksurface = dleaf2->firstmarksurface;
        mleaf->nummarksurfaces = dleaf2->nummarksurfaces;
        for (j = 0; j < NUM_AMBIENTS; j++)
            mleaf->ambient_level[j] = dleaf2->ambient_level[j];
    }
    
    return newdata;
}

static bsp2_dleaf_t *
MBSPto2_Leafs(const mleaf_t *mleafs, int numleafs) {
    const mleaf_t *mleaf = mleafs;
    bsp2_dleaf_t *newdata, *dleaf2;
    int i, j;
    
    newdata = dleaf2 = (bsp2_dleaf_t *)calloc(numleafs, sizeof(*dleaf2));
    
    for (i = 0; i < numleafs; i++, mleaf++, dleaf2++) {
        dleaf2->contents = mleaf->contents;
        dleaf2->visofs = mleaf->visofs;
        for (j = 0; j < 3; j++) {
            dleaf2->mins[j] = mleaf->mins[j];
            dleaf2->maxs[j] = mleaf->maxs[j];
        }
        dleaf2->firstmarksurface = mleaf->firstmarksurface;
        dleaf2->nummarksurfaces = mleaf->nummarksurfaces;
        for (j = 0; j < NUM_AMBIENTS; j++)
            dleaf2->ambient_level[j] = mleaf->ambient_level[j];
    }
    
    return newdata;
}

/*
 * =========================================================================
 * BSP Format Conversion (ver. 29 <-> BSP2)
 * =========================================================================
 */

static bsp2_dnode_t *
BSP29to2_Nodes(const bsp29_dnode_t *dnodes29, int numnodes) {
    const bsp29_dnode_t *dnode29 = dnodes29;
    bsp2_dnode_t *newdata, *dnode2;
    int i, j;

    newdata = dnode2 = static_cast<bsp2_dnode_t *>(malloc(numnodes * sizeof(*dnode2)));

    for (i = 0; i < numnodes; i++, dnode29++, dnode2++) {
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

    return newdata;
}

static bool
BSP2to29_Nodes_Validate(const bsp2_dnode_t *dnodes2, int numnodes) {
    const bsp2_dnode_t *dnode2 = dnodes2;
    
    for (int i = 0; i < numnodes; i++, dnode2++) {
        if (OverflowsInt16(dnode2->children[0]) || OverflowsInt16(dnode2->children[1])) {
            return false;
        }
        for (int j = 0; j < 3; j++) {
            const float min_j = floor(dnode2->mins[j]);
            const float max_j = ceil(dnode2->maxs[j]);

            if (OverflowsInt16(min_j) || OverflowsInt16(max_j)) {
                return false;
            }
        }
        if (OverflowsUint16(dnode2->firstface)
            || OverflowsUint16(dnode2->numfaces)) {
            return false;
        }
    }

    return true;
}

static bsp29_dnode_t *
BSP2to29_Nodes(const bsp2_dnode_t *dnodes2, int numnodes) {
    const bsp2_dnode_t *dnode2 = dnodes2;
    bsp29_dnode_t *newdata, *dnode29;
    int i, j;

    newdata = dnode29 = static_cast<bsp29_dnode_t *>(malloc(numnodes * sizeof(*dnode29)));

    for (i = 0; i < numnodes; i++, dnode2++, dnode29++) {
        dnode29->planenum = dnode2->planenum;
        dnode29->children[0] = dnode2->children[0];
        dnode29->children[1] = dnode2->children[1];
        for (j = 0; j < 3; j++) {
            dnode29->mins[j] = floor(dnode2->mins[j]);
            dnode29->maxs[j] = ceil(dnode2->maxs[j]);
        }
        dnode29->firstface = dnode2->firstface;
        dnode29->numfaces = dnode2->numfaces;
    }

    return newdata;
}

static bsp2_dface_t *
BSP29to2_Faces(const bsp29_dface_t *dfaces29, int numfaces) {
    const bsp29_dface_t *dface29 = dfaces29;
    bsp2_dface_t *newdata, *dface2;
    int i, j;

    newdata = dface2 = static_cast<bsp2_dface_t *>(malloc(numfaces * sizeof(*dface2)));

    for (i = 0; i < numfaces; i++, dface29++, dface2++) {
        dface2->planenum = dface29->planenum;
        dface2->side = dface29->side;
        dface2->firstedge = dface29->firstedge;
        dface2->numedges = dface29->numedges;
        dface2->texinfo = dface29->texinfo;
        for (j = 0; j < MAXLIGHTMAPS; j++)
            dface2->styles[j] = dface29->styles[j];
        dface2->lightofs = dface29->lightofs;
    }

    return newdata;
}

static bool
BSP2to29_Faces_Validate(const bsp2_dface_t *dfaces2, int numfaces) {
    const bsp2_dface_t *dface2 = dfaces2;

    for (int i = 0; i < numfaces; i++, dface2++) {
        if (OverflowsInt16(dface2->planenum)) {
            return false;
        }
        if (OverflowsInt16(dface2->side)) {
            return false;
        }
        if (OverflowsInt16(dface2->numedges)) {
            return false;
        }
        if (OverflowsInt16(dface2->texinfo)) {
            return false;
        }
    }

    return true;
}

static bsp29_dface_t *
BSP2to29_Faces(const bsp2_dface_t *dfaces2, int numfaces) {
    const bsp2_dface_t *dface2 = dfaces2;
    bsp29_dface_t *newdata, *dface29;
    int i, j;

    newdata = dface29 = static_cast<bsp29_dface_t *>(malloc(numfaces * sizeof(*dface29)));

    for (i = 0; i < numfaces; i++, dface2++, dface29++) {
        dface29->planenum = dface2->planenum;
        dface29->side = dface2->side;
        dface29->firstedge = dface2->firstedge;
        dface29->numedges = dface2->numedges;
        dface29->texinfo = dface2->texinfo;
        for (j = 0; j < MAXLIGHTMAPS; j++)
            dface29->styles[j] = dface2->styles[j];
        dface29->lightofs = dface2->lightofs;
    }

    return newdata;
}

static bsp2_dclipnode_t *
BSP29to2_Clipnodes(const bsp29_dclipnode_t *dclipnodes29, int numclipnodes) {
    const bsp29_dclipnode_t *dclipnode29 = dclipnodes29;
    bsp2_dclipnode_t *newdata, *dclipnode2;
    int i, j;

    newdata = dclipnode2 = static_cast<bsp2_dclipnode_t *>(malloc(numclipnodes * sizeof(*dclipnode2)));

    for (i = 0; i < numclipnodes; i++, dclipnode29++, dclipnode2++) {
        dclipnode2->planenum = dclipnode29->planenum;
        for (j = 0; j < 2; j++) {
            /* Slightly tricky since we support > 32k clipnodes */
            int32_t child = (uint16_t)dclipnode29->children[j];
            dclipnode2->children[j] = child > 0xfff0 ? child - 0x10000 : child;
        }
    }

    return newdata;
}

static bool
BSP2to29_Clipnodes_Validate(const bsp2_dclipnode_t *dclipnodes2, int numclipnodes) {
    const bsp2_dclipnode_t *dclipnode2 = dclipnodes2;

    for (int i = 0; i < numclipnodes; i++, dclipnode2++) {
        for (int j = 0; j < 2; j++) {
            /* Slightly tricky since we support > 32k clipnodes */
            int32_t child = dclipnode2->children[j];
            if (child < -15 || child > 0xFFF0) {
                return false;
            }
        }
    }

    return true;
}

static bsp29_dclipnode_t *
BSP2to29_Clipnodes(const bsp2_dclipnode_t *dclipnodes2, int numclipnodes) {
    const bsp2_dclipnode_t *dclipnode2 = dclipnodes2;
    bsp29_dclipnode_t *newdata, *dclipnode29;
    int i, j;

    newdata = dclipnode29 = static_cast<bsp29_dclipnode_t *>(malloc(numclipnodes * sizeof(*dclipnode29)));

    for (i = 0; i < numclipnodes; i++, dclipnode2++, dclipnode29++) {
        dclipnode29->planenum = dclipnode2->planenum;
        for (j = 0; j < 2; j++) {
            /* Slightly tricky since we support > 32k clipnodes */
            int32_t child = dclipnode2->children[j];
            dclipnode29->children[j] = child < 0 ? child + 0x10000 : child;
        }
    }

    return newdata;
}

static bsp2_dedge_t *
BSP29to2_Edges(const bsp29_dedge_t *dedges29, int numedges)
{
    const bsp29_dedge_t *dedge29 = dedges29;
    bsp2_dedge_t *newdata, *dedge2;
    int i;

    newdata = dedge2 = static_cast<bsp2_dedge_t *>(malloc(numedges * sizeof(*dedge2)));

    for (i = 0; i < numedges; i++, dedge29++, dedge2++) {
        dedge2->v[0] = dedge29->v[0];
        dedge2->v[1] = dedge29->v[1];
    }

    return newdata;
}

static bool
BSP2to29_Edges_Validate(const bsp2_dedge_t *dedges2, int numedges)
{
    const bsp2_dedge_t *dedge2 = dedges2;
    
    for (int i = 0; i < numedges; i++, dedge2++) {
        if (OverflowsUint16(dedge2->v[0])
            || OverflowsUint16(dedge2->v[1])) {
            return false;
        }
    }

    return true;
}

static bsp29_dedge_t *
BSP2to29_Edges(const bsp2_dedge_t *dedges2, int numedges)
{
    const bsp2_dedge_t *dedge2 = dedges2;
    bsp29_dedge_t *newdata, *dedge29;
    int i;

    newdata = dedge29 = static_cast<bsp29_dedge_t *>(malloc(numedges * sizeof(*dedge29)));

    for (i = 0; i < numedges; i++, dedge2++, dedge29++) {
        dedge29->v[0] = dedge2->v[0];
        dedge29->v[1] = dedge2->v[1];
    }

    return newdata;
}

static uint32_t *
BSP29to2_Marksurfaces(const uint16_t *dmarksurfaces29, int nummarksurfaces)
{
    const uint16_t *dmarksurface29 = dmarksurfaces29;
    uint32_t *newdata, *dmarksurface2;
    int i;

    newdata = dmarksurface2 = static_cast<uint32_t *>(malloc(nummarksurfaces * sizeof(*dmarksurface2)));

    for (i = 0; i < nummarksurfaces; i++, dmarksurface29++, dmarksurface2++)
        *dmarksurface2 = *dmarksurface29;

    return newdata;
}

static bool
BSP2to29_Marksurfaces_Validate(const uint32_t *dmarksurfaces2, int nummarksurfaces)
{
    const uint32_t *dmarksurface2 = dmarksurfaces2;

    for (int i = 0; i < nummarksurfaces; i++, dmarksurface2++) {
        if (OverflowsUint16(*dmarksurface2)) {
            return false;
        }
    }
        
    return true;
}

static uint16_t *
BSP2to29_Marksurfaces(const uint32_t *dmarksurfaces2, int nummarksurfaces)
{
    const uint32_t *dmarksurface2 = dmarksurfaces2;
    uint16_t *newdata, *dmarksurface29;
    int i;

    newdata = dmarksurface29 = static_cast<uint16_t *>(malloc(nummarksurfaces * sizeof(*dmarksurface29)));

    for (i = 0; i < nummarksurfaces; i++, dmarksurface2++, dmarksurface29++)
        *dmarksurface29 = *dmarksurface2;
    
    return newdata;
}

/*
 * =========================================================================
 * BSP Format Conversion (ver. BSP2rmq <-> BSP2)
 * =========================================================================
 */

static bsp2_dnode_t *
BSP2rmqto2_Nodes(const bsp2rmq_dnode_t *dnodes2rmq, int numnodes) {
    const bsp2rmq_dnode_t *dnode2rmq = dnodes2rmq;
    bsp2_dnode_t *newdata, *dnode2;
    int i, j;

    newdata = dnode2 = static_cast<bsp2_dnode_t *>(malloc(numnodes * sizeof(*dnode2)));

    for (i = 0; i < numnodes; i++, dnode2rmq++, dnode2++) {
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

    return newdata;
}

static bsp2rmq_dnode_t *
BSP2to2rmq_Nodes(const bsp2_dnode_t *dnodes2, int numnodes) {
    const bsp2_dnode_t *dnode2 = dnodes2;
    bsp2rmq_dnode_t *newdata, *dnode2rmq;
    int i, j;

    newdata = dnode2rmq = static_cast<bsp2rmq_dnode_t *>(malloc(numnodes * sizeof(*dnode2rmq)));

    for (i = 0; i < numnodes; i++, dnode2++, dnode2rmq++) {
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

    return newdata;
}

/*
 * =========================================================================
 * BSP Format Conversion (no-ops)
 * =========================================================================
 */

static void *CopyArray(const void *in, int numelems, size_t elemsize)
{
    void *out = (void *)calloc(numelems, elemsize);
    memcpy(out, in, numelems * elemsize);
    return out;
}

static dmodelh2_t *H2_CopyModels(const dmodelh2_t *dmodels, int nummodels)
{
    return (dmodelh2_t *)CopyArray(dmodels, nummodels, sizeof(*dmodels));
}

static uint8_t *BSP29_CopyVisData(const uint8_t *dvisdata, int visdatasize)
{
    return (uint8_t *)CopyArray(dvisdata, visdatasize, 1);
}

static uint8_t *BSP29_CopyLightData(const uint8_t *dlightdata, int lightdatasize)
{
    return (uint8_t *)CopyArray(dlightdata, lightdatasize, 1);
}

static dmiptexlump_t *BSP29_CopyTexData(const dmiptexlump_t *dtexdata, int texdatasize)
{
    return (dmiptexlump_t *)CopyArray(dtexdata, texdatasize, 1);
}

static char *BSP29_CopyEntData(const char *dentdata, int entdatasize)
{
    return (char *)CopyArray(dentdata, entdatasize, 1);
}

static dplane_t *BSP29_CopyPlanes(const dplane_t *dplanes, int numplanes)
{
    return (dplane_t *)CopyArray(dplanes, numplanes, sizeof(*dplanes));
}

static dvertex_t *BSP29_CopyVertexes(const dvertex_t *dvertexes, int numvertexes)
{
    return (dvertex_t *)CopyArray(dvertexes, numvertexes, sizeof(*dvertexes));
}

static texinfo_t *BSP29_CopyTexinfo(const texinfo_t *texinfo, int numtexinfo)
{
    return (texinfo_t *)CopyArray(texinfo, numtexinfo, sizeof(*texinfo));
}

static int32_t *BSP29_CopySurfedges(const int32_t *surfedges, int numsurfedges)
{
    return (int32_t *)CopyArray(surfedges, numsurfedges, sizeof(*surfedges));
}

static bsp2_dface_t *BSP2_CopyFaces(const bsp2_dface_t *dfaces, int numfaces)
{
    return (bsp2_dface_t *)CopyArray(dfaces, numfaces, sizeof(*dfaces));
}

static bsp2_dclipnode_t *BSP2_CopyClipnodes(const bsp2_dclipnode_t *dclipnodes, int numclipnodes)
{
    return (bsp2_dclipnode_t *)CopyArray(dclipnodes, numclipnodes, sizeof(*dclipnodes));
}

static bsp2_dedge_t *BSP2_CopyEdges(const bsp2_dedge_t *dedges, int numedges)
{
    return (bsp2_dedge_t *)CopyArray(dedges, numedges, sizeof(*dedges));
}

static uint32_t *BSP2_CopyMarksurfaces(const uint32_t *marksurfaces, int nummarksurfaces)
{
    return (uint32_t *)CopyArray(marksurfaces, nummarksurfaces, sizeof(*marksurfaces));
}

static bsp2_dnode_t *BSP2_CopyNodes(const bsp2_dnode_t *dnodes, int numnodes)
{
    return (bsp2_dnode_t *)CopyArray(dnodes, numnodes, sizeof(*dnodes));
}

static uint32_t *Q2BSPtoM_CopyLeafBrushes(const uint16_t *leafbrushes, int count)
{
    const uint16_t *leafbrush = leafbrushes;
    uint32_t *newdata, *leafbrushes2;
    int i;

    newdata = leafbrushes2 = static_cast<uint32_t *>(malloc(count * sizeof(*leafbrushes2)));

    for (i = 0; i < count; i++, leafbrush++, leafbrushes2++)
        *leafbrushes2 = *leafbrush;

    return newdata;
}

static uint16_t *MBSPtoQ2_CopyLeafBrushes(const uint32_t *leafbrushes, int count)
{
    const uint32_t *leafbrush = leafbrushes;
    uint16_t *newdata, *leafbrushes2;
    int i;

    newdata = leafbrushes2 = static_cast<uint16_t *>(malloc(count * sizeof(*leafbrushes2)));

    for (i = 0; i < count; i++, leafbrush++, leafbrushes2++)
        *leafbrushes2 = *leafbrush;

    return newdata;
}

static uint32_t *Q2BSP_Qbism_CopyLeafBrushes(const uint32_t *leafbrushes, int count)
{
    return (uint32_t *)CopyArray(leafbrushes, count, sizeof(*leafbrushes));
}

static darea_t *Q2BSP_CopyAreas(const darea_t *areas, int count)
{
    return (darea_t *)CopyArray(areas, count, sizeof(*areas));
}

static dareaportal_t *Q2BSP_CopyAreaPortals(const dareaportal_t *areaportals, int count)
{
    return (dareaportal_t *)CopyArray(areaportals, count, sizeof(*areaportals));
}

static dbrush_t *Q2BSP_CopyBrushes(const dbrush_t *brushes, int count)
{
    return (dbrush_t *)CopyArray(brushes, count, sizeof(*brushes));
}

static q2_dbrushside_qbism_t *Q2BSPtoM_CopyBrushSides(const dbrushside_t *dbrushsides, int count)
{
    const dbrushside_t *brushside = dbrushsides;
    q2_dbrushside_qbism_t *newdata, *brushsides2;
    int i;

    newdata = brushsides2 = static_cast<q2_dbrushside_qbism_t *>(malloc(count * sizeof(*brushsides2)));

    for (i = 0; i < count; i++, brushside++, brushsides2++) {
        brushsides2->planenum = brushside->planenum;
        brushsides2->texinfo = brushside->texinfo;
    }

    return newdata;
}

static q2_dbrushside_qbism_t *Q2BSP_Qbism_CopyBrushSides(const q2_dbrushside_qbism_t *brushsides, int count)
{
    return (q2_dbrushside_qbism_t *)CopyArray(brushsides, count, sizeof(*brushsides));
}

static dbrushside_t *MBSPtoQ2_CopyBrushSides(const q2_dbrushside_qbism_t *dbrushsides, int count)
{
    const q2_dbrushside_qbism_t *brushside = dbrushsides;
    dbrushside_t *newdata, *brushsides2;
    int i;

    newdata = brushsides2 = static_cast<dbrushside_t *>(malloc(count * sizeof(*brushsides2)));

    for (i = 0; i < count; i++, brushside++, brushsides2++) {
        brushsides2->planenum = brushside->planenum;
        brushsides2->texinfo = brushside->texinfo;
    }

    return newdata;
}


/*
 * =========================================================================
 * Freeing BSP structs
 * =========================================================================
 */

static void FreeBSP29(bsp29_t *bsp)
{
    free(bsp->dmodels_q);
    free(bsp->dmodels_h2);
    free(bsp->dvisdata);
    free(bsp->dlightdata);
    free(bsp->dtexdata);
    free(bsp->dentdata);
    free(bsp->dleafs);
    free(bsp->dplanes);
    free(bsp->dvertexes);
    free(bsp->dnodes);
    free(bsp->texinfo);
    free(bsp->dfaces);
    free(bsp->dclipnodes);
    free(bsp->dedges);
    free(bsp->dmarksurfaces);
    free(bsp->dsurfedges);
    memset(bsp, 0, sizeof(*bsp));
}

static void FreeBSP2RMQ(bsp2rmq_t *bsp)
{
    free(bsp->dmodels_q);
    free(bsp->dmodels_h2);
    free(bsp->dvisdata);
    free(bsp->dlightdata);
    free(bsp->dtexdata);
    free(bsp->dentdata);
    free(bsp->dleafs);
    free(bsp->dplanes);
    free(bsp->dvertexes);
    free(bsp->dnodes);
    free(bsp->texinfo);
    free(bsp->dfaces);
    free(bsp->dclipnodes);
    free(bsp->dedges);
    free(bsp->dmarksurfaces);
    free(bsp->dsurfedges);
    memset(bsp, 0, sizeof(*bsp));
}

static void FreeBSP2(bsp2_t *bsp)
{
    free(bsp->dmodels_q);
    free(bsp->dmodels_h2);
    free(bsp->dvisdata);
    free(bsp->dlightdata);
    free(bsp->dtexdata);
    free(bsp->dentdata);
    free(bsp->dleafs);
    free(bsp->dplanes);
    free(bsp->dvertexes);
    free(bsp->dnodes);
    free(bsp->texinfo);
    free(bsp->dfaces);
    free(bsp->dclipnodes);
    free(bsp->dedges);
    free(bsp->dmarksurfaces);
    free(bsp->dsurfedges);
    memset(bsp, 0, sizeof(*bsp));
}

static void FreeQ2BSP(q2bsp_t *bsp)
{
    free(bsp->dmodels);
    free(bsp->dvis);
    free(bsp->dlightdata);
    free(bsp->dentdata);
    free(bsp->dleafs);
    free(bsp->dplanes);
    free(bsp->dvertexes);
    free(bsp->dnodes);
    free(bsp->texinfo);
    free(bsp->dfaces);
    free(bsp->dedges);
    free(bsp->dleaffaces);
    free(bsp->dleafbrushes);
    free(bsp->dsurfedges);
    free(bsp->dareas);
    free(bsp->dareaportals);
    free(bsp->dbrushes);
    free(bsp->dbrushsides);
    memset(bsp, 0, sizeof(*bsp));
}

static void FreeQ2BSP_QBSP(q2bsp_qbism_t *bsp)
{
    free(bsp->dmodels);
    free(bsp->dvis);
    free(bsp->dlightdata);
    free(bsp->dentdata);
    free(bsp->dleafs);
    free(bsp->dplanes);
    free(bsp->dvertexes);
    free(bsp->dnodes);
    free(bsp->texinfo);
    free(bsp->dfaces);
    free(bsp->dedges);
    free(bsp->dleaffaces);
    free(bsp->dleafbrushes);
    free(bsp->dsurfedges);
    free(bsp->dareas);
    free(bsp->dareaportals);
    free(bsp->dbrushes);
    free(bsp->dbrushsides);
    memset(bsp, 0, sizeof(*bsp));
}

static void FreeMBSP(mbsp_t *bsp)
{
    free(bsp->dmodels);
    free(bsp->dvisdata);
    free(bsp->dlightdata);
    free(bsp->dtexdata);
    free(bsp->drgbatexdata); //mxd
    free(bsp->dentdata);
    free(bsp->dleafs);
    free(bsp->dplanes);
    free(bsp->dvertexes);
    free(bsp->dnodes);
    free(bsp->texinfo);
    free(bsp->dfaces);
    free(bsp->dclipnodes);
    free(bsp->dedges);
    free(bsp->dleaffaces);
    free(bsp->dleafbrushes);
    free(bsp->dsurfedges);
    free(bsp->dareas);
    free(bsp->dareaportals);
    free(bsp->dbrushes);
    free(bsp->dbrushsides);
    memset(bsp, 0, sizeof(*bsp));
}

inline void
ConvertBSPToMFormatComplete(const bspversion_t **mbsp_loadversion, const bspversion_t *version, bspdata_t *bspdata)
{
    bspdata->loadversion = *mbsp_loadversion = bspdata->version;
    bspdata->version = version;
}

/*
 * =========================================================================
 * ConvertBSPFormat
 * - BSP is assumed to be in CPU byte order already
 * - No checks are done here (yet) for overflow of values when down-converting
 * =========================================================================
 */
bool
ConvertBSPFormat(bspdata_t *bspdata, const bspversion_t *to_version)
{
    if (bspdata->version == to_version)
        return true;

    if (to_version == &bspver_generic) {
        // Conversions to bspver_generic
        // NOTE: these always succeed

        if (bspdata->version == &bspver_q1 || bspdata->version == &bspver_h2 || bspdata->version == &bspver_hl) {
            // bspver_q1, bspver_h2, bspver_hl -> bspver_generic

            const bsp29_t *bsp29 = &bspdata->data.bsp29;
            mbsp_t *mbsp = &bspdata->data.mbsp;
        
            memset(mbsp, 0, sizeof(*mbsp));
        
            // copy counts
            mbsp->nummodels = bsp29->nummodels;
            mbsp->visdatasize = bsp29->visdatasize;
            mbsp->lightdatasize = bsp29->lightdatasize;
            mbsp->texdatasize = bsp29->texdatasize;
            mbsp->entdatasize = bsp29->entdatasize;
            mbsp->numleafs = bsp29->numleafs;
            mbsp->numplanes = bsp29->numplanes;
            mbsp->numvertexes = bsp29->numvertexes;
            mbsp->numnodes = bsp29->numnodes;
            mbsp->numtexinfo = bsp29->numtexinfo;
            mbsp->numfaces = bsp29->numfaces;
            mbsp->numclipnodes = bsp29->numclipnodes;
            mbsp->numedges = bsp29->numedges;
            mbsp->numleaffaces = bsp29->nummarksurfaces;
            mbsp->numsurfedges = bsp29->numsurfedges;
        
            // copy or convert data
            if (bspdata->version == &bspver_h2) {
                mbsp->dmodels = H2_CopyModels(bsp29->dmodels_h2, bsp29->nummodels);
            } else {
                mbsp->dmodels = BSPQ1toH2_Models(bsp29->dmodels_q, bsp29->nummodels);
            }
            mbsp->dvisdata = BSP29_CopyVisData(bsp29->dvisdata, bsp29->visdatasize);
            mbsp->dlightdata = BSP29_CopyLightData(bsp29->dlightdata, bsp29->lightdatasize);
            mbsp->dtexdata = BSP29_CopyTexData(bsp29->dtexdata, bsp29->texdatasize);
            mbsp->dentdata = BSP29_CopyEntData(bsp29->dentdata, bsp29->entdatasize);
            mbsp->dleafs = BSP29toM_Leafs(bsp29->dleafs, bsp29->numleafs);
            mbsp->dplanes = BSP29_CopyPlanes(bsp29->dplanes, bsp29->numplanes);
            mbsp->dvertexes = BSP29_CopyVertexes(bsp29->dvertexes, bsp29->numvertexes);
            mbsp->dnodes = BSP29to2_Nodes(bsp29->dnodes, bsp29->numnodes);
            mbsp->texinfo = BSP29toM_Texinfo(bsp29->texinfo, bsp29->numtexinfo);
            mbsp->dfaces = BSP29to2_Faces(bsp29->dfaces, bsp29->numfaces);
            mbsp->dclipnodes = BSP29to2_Clipnodes(bsp29->dclipnodes, bsp29->numclipnodes);
            mbsp->dedges = BSP29to2_Edges(bsp29->dedges, bsp29->numedges);
            mbsp->dleaffaces = BSP29to2_Marksurfaces(bsp29->dmarksurfaces, bsp29->nummarksurfaces);
            mbsp->dsurfedges = BSP29_CopySurfedges(bsp29->dsurfedges, bsp29->numsurfedges);
        
            /* Free old data */
            FreeBSP29((bsp29_t *)bsp29);
        
            /* Conversion complete! */
            ConvertBSPToMFormatComplete(&mbsp->loadversion, to_version, bspdata);
        
            return true;
        } else if (bspdata->version == &bspver_q2) {
            // bspver_q2 -> bspver_generic

            const q2bsp_t *q2bsp = &bspdata->data.q2bsp;
            mbsp_t *mbsp = &bspdata->data.mbsp;
        
            memset(mbsp, 0, sizeof(*mbsp));
        
            // copy counts
            mbsp->nummodels = q2bsp->nummodels;
            mbsp->visdatasize = q2bsp->visdatasize;
            mbsp->lightdatasize = q2bsp->lightdatasize;
            mbsp->entdatasize = q2bsp->entdatasize;
            mbsp->numleafs = q2bsp->numleafs;
            mbsp->numplanes = q2bsp->numplanes;
            mbsp->numvertexes = q2bsp->numvertexes;
            mbsp->numnodes = q2bsp->numnodes;
            mbsp->numtexinfo = q2bsp->numtexinfo;
            mbsp->numfaces = q2bsp->numfaces;
            mbsp->numedges = q2bsp->numedges;
            mbsp->numleaffaces = q2bsp->numleaffaces;
            mbsp->numleafbrushes = q2bsp->numleafbrushes;
            mbsp->numsurfedges = q2bsp->numsurfedges;
            mbsp->numareas = q2bsp->numareas;
            mbsp->numareaportals = q2bsp->numareaportals;
            mbsp->numbrushes = q2bsp->numbrushes;
            mbsp->numbrushsides = q2bsp->numbrushsides;
        
            // copy or convert data
            mbsp->dmodels = Q2BSPtoM_Models(q2bsp->dmodels, q2bsp->nummodels);
            mbsp->dlightdata = BSP29_CopyLightData(q2bsp->dlightdata, q2bsp->lightdatasize);
            mbsp->dentdata = BSP29_CopyEntData(q2bsp->dentdata, q2bsp->entdatasize);
            mbsp->dleafs = Q2BSPtoM_Leafs(q2bsp->dleafs, q2bsp->numleafs);
            mbsp->dplanes = BSP29_CopyPlanes(q2bsp->dplanes, q2bsp->numplanes);
            mbsp->dvertexes = BSP29_CopyVertexes(q2bsp->dvertexes, q2bsp->numvertexes);
            mbsp->dnodes = Q2BSPto2_Nodes(q2bsp->dnodes, q2bsp->numnodes);
            mbsp->texinfo = Q2BSPtoM_Texinfo(q2bsp->texinfo, q2bsp->numtexinfo);
            mbsp->dfaces = Q2BSPto2_Faces(q2bsp->dfaces, q2bsp->numfaces);
            mbsp->dedges = BSP29to2_Edges(q2bsp->dedges, q2bsp->numedges);
            mbsp->dleaffaces = BSP29to2_Marksurfaces(q2bsp->dleaffaces, q2bsp->numleaffaces);
            mbsp->dleafbrushes = Q2BSPtoM_CopyLeafBrushes(q2bsp->dleafbrushes, q2bsp->numleafbrushes);
            mbsp->dsurfedges = BSP29_CopySurfedges(q2bsp->dsurfedges, q2bsp->numsurfedges);

            mbsp->dvisdata = Q2BSPtoM_CopyVisData(q2bsp->dvis, q2bsp->visdatasize, &mbsp->visdatasize, mbsp->dleafs, mbsp->numleafs);
        
            mbsp->dareas = Q2BSP_CopyAreas(q2bsp->dareas, q2bsp->numareas);
            mbsp->dareaportals = Q2BSP_CopyAreaPortals(q2bsp->dareaportals, q2bsp->numareaportals);
        
            mbsp->dbrushes = Q2BSP_CopyBrushes(q2bsp->dbrushes, q2bsp->numbrushes);
            mbsp->dbrushsides = Q2BSPtoM_CopyBrushSides(q2bsp->dbrushsides, q2bsp->numbrushsides);
        
            /* Free old data */
            FreeQ2BSP((q2bsp_t *)q2bsp);
        
            /* Conversion complete! */
            ConvertBSPToMFormatComplete(&mbsp->loadversion, to_version, bspdata);

            return true;
        } else if (bspdata->version == &bspver_qbism) {
            // bspver_qbism -> bspver_generic

            const q2bsp_qbism_t *q2bsp = &bspdata->data.q2bsp_qbism;
            mbsp_t *mbsp = &bspdata->data.mbsp;
        
            memset(mbsp, 0, sizeof(*mbsp));
        
            // copy counts
            mbsp->nummodels = q2bsp->nummodels;
            mbsp->visdatasize = q2bsp->visdatasize;
            mbsp->lightdatasize = q2bsp->lightdatasize;
            mbsp->entdatasize = q2bsp->entdatasize;
            mbsp->numleafs = q2bsp->numleafs;
            mbsp->numplanes = q2bsp->numplanes;
            mbsp->numvertexes = q2bsp->numvertexes;
            mbsp->numnodes = q2bsp->numnodes;
            mbsp->numtexinfo = q2bsp->numtexinfo;
            mbsp->numfaces = q2bsp->numfaces;
            mbsp->numedges = q2bsp->numedges;
            mbsp->numleaffaces = q2bsp->numleaffaces;
            mbsp->numleafbrushes = q2bsp->numleafbrushes;
            mbsp->numsurfedges = q2bsp->numsurfedges;
            mbsp->numareas = q2bsp->numareas;
            mbsp->numareaportals = q2bsp->numareaportals;
            mbsp->numbrushes = q2bsp->numbrushes;
            mbsp->numbrushsides = q2bsp->numbrushsides;
        
            // copy or convert data
            mbsp->dmodels = Q2BSPtoM_Models(q2bsp->dmodels, q2bsp->nummodels);
            mbsp->dlightdata = BSP29_CopyLightData(q2bsp->dlightdata, q2bsp->lightdatasize);
            mbsp->dentdata = BSP29_CopyEntData(q2bsp->dentdata, q2bsp->entdatasize);
            mbsp->dleafs = Q2BSP_QBSPtoM_Leafs(q2bsp->dleafs, q2bsp->numleafs);
            mbsp->dplanes = BSP29_CopyPlanes(q2bsp->dplanes, q2bsp->numplanes);
            mbsp->dvertexes = BSP29_CopyVertexes(q2bsp->dvertexes, q2bsp->numvertexes);
            mbsp->dnodes = BSP2_CopyNodes(q2bsp->dnodes, q2bsp->numnodes);
            mbsp->texinfo = Q2BSPtoM_Texinfo(q2bsp->texinfo, q2bsp->numtexinfo);
            mbsp->dfaces = Q2BSP_QBSPto2_Faces(q2bsp->dfaces, q2bsp->numfaces);
            mbsp->dedges = BSP2_CopyEdges(q2bsp->dedges, q2bsp->numedges);
            mbsp->dleaffaces = BSP2_CopyMarksurfaces(q2bsp->dleaffaces, q2bsp->numleaffaces);
            mbsp->dleafbrushes = Q2BSP_Qbism_CopyLeafBrushes(q2bsp->dleafbrushes, q2bsp->numleafbrushes);
            mbsp->dsurfedges = BSP29_CopySurfedges(q2bsp->dsurfedges, q2bsp->numsurfedges);
            
            mbsp->dvisdata = Q2BSPtoM_CopyVisData(q2bsp->dvis, q2bsp->visdatasize, &mbsp->visdatasize, mbsp->dleafs, mbsp->numleafs);
        
            mbsp->dareas = Q2BSP_CopyAreas(q2bsp->dareas, q2bsp->numareas);
            mbsp->dareaportals = Q2BSP_CopyAreaPortals(q2bsp->dareaportals, q2bsp->numareaportals);
        
            mbsp->dbrushes = Q2BSP_CopyBrushes(q2bsp->dbrushes, q2bsp->numbrushes);
            mbsp->dbrushsides = Q2BSP_Qbism_CopyBrushSides(q2bsp->dbrushsides, q2bsp->numbrushsides);
        
            /* Free old data */
            FreeQ2BSP_QBSP((q2bsp_qbism_t *)q2bsp);
        
            /* Conversion complete! */
            ConvertBSPToMFormatComplete(&mbsp->loadversion, to_version, bspdata);

            return true;
        } else if (bspdata->version == &bspver_bsp2rmq || bspdata->version == &bspver_h2bsp2rmq) {
            // bspver_bsp2rmq, bspver_h2bsp2rmq -> bspver_generic

            const bsp2rmq_t *bsp2rmq = &bspdata->data.bsp2rmq;
            mbsp_t *mbsp = &bspdata->data.mbsp;
        
            memset(mbsp, 0, sizeof(*mbsp));
        
            // copy counts
            mbsp->nummodels = bsp2rmq->nummodels;
            mbsp->visdatasize = bsp2rmq->visdatasize;
            mbsp->lightdatasize = bsp2rmq->lightdatasize;
            mbsp->texdatasize = bsp2rmq->texdatasize;
            mbsp->entdatasize = bsp2rmq->entdatasize;
            mbsp->numleafs = bsp2rmq->numleafs;
            mbsp->numplanes = bsp2rmq->numplanes;
            mbsp->numvertexes = bsp2rmq->numvertexes;
            mbsp->numnodes = bsp2rmq->numnodes;
            mbsp->numtexinfo = bsp2rmq->numtexinfo;
            mbsp->numfaces = bsp2rmq->numfaces;
            mbsp->numclipnodes = bsp2rmq->numclipnodes;
            mbsp->numedges = bsp2rmq->numedges;
            mbsp->numleaffaces = bsp2rmq->nummarksurfaces;
            mbsp->numsurfedges = bsp2rmq->numsurfedges;
        
            // copy or convert data
            if (bspdata->version == &bspver_h2bsp2rmq) {
                mbsp->dmodels = H2_CopyModels(bsp2rmq->dmodels_h2, bsp2rmq->nummodels);
            } else {
                mbsp->dmodels = BSPQ1toH2_Models(bsp2rmq->dmodels_q, bsp2rmq->nummodels);
            }
            mbsp->dvisdata = BSP29_CopyVisData(bsp2rmq->dvisdata, bsp2rmq->visdatasize);
            mbsp->dlightdata = BSP29_CopyLightData(bsp2rmq->dlightdata, bsp2rmq->lightdatasize);
            mbsp->dtexdata = BSP29_CopyTexData(bsp2rmq->dtexdata, bsp2rmq->texdatasize);
            mbsp->dentdata = BSP29_CopyEntData(bsp2rmq->dentdata, bsp2rmq->entdatasize);
            mbsp->dleafs = BSP2rmqtoM_Leafs(bsp2rmq->dleafs, bsp2rmq->numleafs);
            mbsp->dplanes = BSP29_CopyPlanes(bsp2rmq->dplanes, bsp2rmq->numplanes);
            mbsp->dvertexes = BSP29_CopyVertexes(bsp2rmq->dvertexes, bsp2rmq->numvertexes);
            mbsp->dnodes = BSP2rmqto2_Nodes(bsp2rmq->dnodes, bsp2rmq->numnodes);
            mbsp->texinfo = BSP29toM_Texinfo(bsp2rmq->texinfo, bsp2rmq->numtexinfo);
            mbsp->dfaces = BSP2_CopyFaces(bsp2rmq->dfaces, bsp2rmq->numfaces);
            mbsp->dclipnodes = BSP2_CopyClipnodes(bsp2rmq->dclipnodes, bsp2rmq->numclipnodes);
            mbsp->dedges = BSP2_CopyEdges(bsp2rmq->dedges, bsp2rmq->numedges);
            mbsp->dleaffaces = BSP2_CopyMarksurfaces(bsp2rmq->dmarksurfaces, bsp2rmq->nummarksurfaces);
            mbsp->dsurfedges = BSP29_CopySurfedges(bsp2rmq->dsurfedges, bsp2rmq->numsurfedges);
        
            /* Free old data */
            FreeBSP2RMQ((bsp2rmq_t *)bsp2rmq);
        
            /* Conversion complete! */
            ConvertBSPToMFormatComplete(&mbsp->loadversion, to_version, bspdata);
        
            return true;
        } else if (bspdata->version == &bspver_bsp2 || bspdata->version == &bspver_h2bsp2) {
            // bspver_bsp2, bspver_h2bsp2 -> bspver_generic

            const bsp2_t *bsp2 = &bspdata->data.bsp2;
            mbsp_t *mbsp = &bspdata->data.mbsp;
        
            memset(mbsp, 0, sizeof(*mbsp));
        
            // copy counts
            mbsp->nummodels = bsp2->nummodels;
            mbsp->visdatasize = bsp2->visdatasize;
            mbsp->lightdatasize = bsp2->lightdatasize;
            mbsp->texdatasize = bsp2->texdatasize;
            mbsp->entdatasize = bsp2->entdatasize;
            mbsp->numleafs = bsp2->numleafs;
            mbsp->numplanes = bsp2->numplanes;
            mbsp->numvertexes = bsp2->numvertexes;
            mbsp->numnodes = bsp2->numnodes;
            mbsp->numtexinfo = bsp2->numtexinfo;
            mbsp->numfaces = bsp2->numfaces;
            mbsp->numclipnodes = bsp2->numclipnodes;
            mbsp->numedges = bsp2->numedges;
            mbsp->numleaffaces = bsp2->nummarksurfaces;
            mbsp->numsurfedges = bsp2->numsurfedges;
        
            // copy or convert data
            if (bspdata->version == &bspver_h2bsp2) {
                mbsp->dmodels = H2_CopyModels(bsp2->dmodels_h2, bsp2->nummodels);
            } else {
                mbsp->dmodels = BSPQ1toH2_Models(bsp2->dmodels_q, bsp2->nummodels);
            }
            mbsp->dvisdata = BSP29_CopyVisData(bsp2->dvisdata, bsp2->visdatasize);
            mbsp->dlightdata = BSP29_CopyLightData(bsp2->dlightdata, bsp2->lightdatasize);
            mbsp->dtexdata = BSP29_CopyTexData(bsp2->dtexdata, bsp2->texdatasize);
            mbsp->dentdata = BSP29_CopyEntData(bsp2->dentdata, bsp2->entdatasize);
            mbsp->dleafs = BSP2toM_Leafs(bsp2->dleafs, bsp2->numleafs);
            mbsp->dplanes = BSP29_CopyPlanes(bsp2->dplanes, bsp2->numplanes);
            mbsp->dvertexes = BSP29_CopyVertexes(bsp2->dvertexes, bsp2->numvertexes);
            mbsp->dnodes = BSP2_CopyNodes(bsp2->dnodes, bsp2->numnodes);
            mbsp->texinfo = BSP29toM_Texinfo(bsp2->texinfo, bsp2->numtexinfo);
            mbsp->dfaces = BSP2_CopyFaces(bsp2->dfaces, bsp2->numfaces);
            mbsp->dclipnodes = BSP2_CopyClipnodes(bsp2->dclipnodes, bsp2->numclipnodes);
            mbsp->dedges = BSP2_CopyEdges(bsp2->dedges, bsp2->numedges);
            mbsp->dleaffaces = BSP2_CopyMarksurfaces(bsp2->dmarksurfaces, bsp2->nummarksurfaces);
            mbsp->dsurfedges = BSP29_CopySurfedges(bsp2->dsurfedges, bsp2->numsurfedges);
        
            /* Free old data */
            FreeBSP2((bsp2_t *)bsp2);
        
            /* Conversion complete! */
            ConvertBSPToMFormatComplete(&mbsp->loadversion, to_version, bspdata);

            return true;
        }
    } 
    else if (bspdata->version == &bspver_generic) {
        // Conversions from bspver_generic

        if (to_version == &bspver_q1 || to_version == &bspver_h2 || to_version == &bspver_hl) {
            // bspver_generic -> bspver_q1, bspver_h2, bspver_hl

            bsp29_t *bsp29 = &bspdata->data.bsp29;
            const mbsp_t *mbsp = &bspdata->data.mbsp;
        
            // validate that the conversion is possible
            if (!MBSPto29_Leafs_Validate(mbsp->dleafs, mbsp->numleafs)) {
                return false;
            }
            if (!BSP2to29_Nodes_Validate(mbsp->dnodes, mbsp->numnodes)) {
                return false;
            }
            if (!BSP2to29_Faces_Validate(mbsp->dfaces, mbsp->numfaces)) {
                return false;
            }
            if (!BSP2to29_Clipnodes_Validate(mbsp->dclipnodes, mbsp->numclipnodes)) {
                return false;
            }
            if (!BSP2to29_Edges_Validate(mbsp->dedges, mbsp->numedges)) {
                return false;
            }
            if (!BSP2to29_Marksurfaces_Validate(mbsp->dleaffaces, mbsp->numleaffaces)) {
                return false;
            }

            // zero destination struct
            memset(bsp29, 0, sizeof(*bsp29));
        
            // copy counts
            bsp29->nummodels = mbsp->nummodels;
            bsp29->visdatasize = mbsp->visdatasize;
            bsp29->lightdatasize = mbsp->lightdatasize;
            bsp29->texdatasize = mbsp->texdatasize;
            bsp29->entdatasize = mbsp->entdatasize;
            bsp29->numleafs = mbsp->numleafs;
            bsp29->numplanes = mbsp->numplanes;
            bsp29->numvertexes = mbsp->numvertexes;
            bsp29->numnodes = mbsp->numnodes;
            bsp29->numtexinfo = mbsp->numtexinfo;
            bsp29->numfaces = mbsp->numfaces;
            bsp29->numclipnodes = mbsp->numclipnodes;
            bsp29->numedges = mbsp->numedges;
            bsp29->nummarksurfaces = mbsp->numleaffaces;
            bsp29->numsurfedges = mbsp->numsurfedges;
        
            // copy or convert data
            if (to_version == &bspver_h2) {
                bsp29->dmodels_h2 = H2_CopyModels(mbsp->dmodels, mbsp->nummodels);
            } else {
                bsp29->dmodels_q = BSPH2toQ1_Models(mbsp->dmodels, mbsp->nummodels);
            }
            bsp29->dvisdata = BSP29_CopyVisData(mbsp->dvisdata, mbsp->visdatasize);
            bsp29->dlightdata = BSP29_CopyLightData(mbsp->dlightdata, mbsp->lightdatasize);
            bsp29->dtexdata = BSP29_CopyTexData(mbsp->dtexdata, mbsp->texdatasize);
            bsp29->dentdata = BSP29_CopyEntData(mbsp->dentdata, mbsp->entdatasize);
            bsp29->dleafs = MBSPto29_Leafs(mbsp->dleafs, mbsp->numleafs);
            bsp29->dplanes = BSP29_CopyPlanes(mbsp->dplanes, mbsp->numplanes);
            bsp29->dvertexes = BSP29_CopyVertexes(mbsp->dvertexes, mbsp->numvertexes);
            bsp29->dnodes = BSP2to29_Nodes(mbsp->dnodes, mbsp->numnodes);
            bsp29->texinfo = MBSPto29_Texinfo(mbsp->texinfo, mbsp->numtexinfo);
            bsp29->dfaces = BSP2to29_Faces(mbsp->dfaces, mbsp->numfaces);
            bsp29->dclipnodes = BSP2to29_Clipnodes(mbsp->dclipnodes, mbsp->numclipnodes);
            bsp29->dedges = BSP2to29_Edges(mbsp->dedges, mbsp->numedges);
            bsp29->dmarksurfaces = BSP2to29_Marksurfaces(mbsp->dleaffaces, mbsp->numleaffaces);
            bsp29->dsurfedges = BSP29_CopySurfedges(mbsp->dsurfedges, mbsp->numsurfedges);
        
            /* Free old data */
            FreeMBSP((mbsp_t *)mbsp);
        
            /* Conversion complete! */
            bspdata->version = to_version;
        
            return true;
        } else if (to_version == &bspver_q2) {
            // bspver_generic -> bspver_q2

            const mbsp_t *mbsp = &bspdata->data.mbsp;
            q2bsp_t *q2bsp = &bspdata->data.q2bsp;
        
            // FIXME: validate that the conversion is possible without overflow
            // (see bspver_q1 case above)

            memset(q2bsp, 0, sizeof(*q2bsp));
        
            // copy counts
            q2bsp->nummodels = mbsp->nummodels;
            q2bsp->visdatasize = mbsp->visdatasize;
            q2bsp->lightdatasize = mbsp->lightdatasize;
            q2bsp->entdatasize = mbsp->entdatasize;
            q2bsp->numleafs = mbsp->numleafs;
            q2bsp->numplanes = mbsp->numplanes;
            q2bsp->numvertexes = mbsp->numvertexes;
            q2bsp->numnodes = mbsp->numnodes;
            q2bsp->numtexinfo = mbsp->numtexinfo;
            q2bsp->numfaces = mbsp->numfaces;
            q2bsp->numedges = mbsp->numedges;
            q2bsp->numleaffaces = mbsp->numleaffaces;
            q2bsp->numleafbrushes = mbsp->numleafbrushes;
            q2bsp->numsurfedges = mbsp->numsurfedges;
            q2bsp->numareas = mbsp->numareas;
            q2bsp->numareaportals = mbsp->numareaportals;
            q2bsp->numbrushes = mbsp->numbrushes;
            q2bsp->numbrushsides = mbsp->numbrushsides;
        
            // copy or convert data
            q2bsp->dmodels = MBSPtoQ2_Models(mbsp->dmodels, mbsp->nummodels);
            q2bsp->dvis = MBSPtoQ2_CopyVisData(mbsp->dvisdata, &q2bsp->visdatasize, mbsp->numleafs, mbsp->dleafs);
            q2bsp->dlightdata = BSP29_CopyLightData(mbsp->dlightdata, mbsp->lightdatasize);
            q2bsp->dentdata = BSP29_CopyEntData(mbsp->dentdata, mbsp->entdatasize);
            q2bsp->dleafs = MBSPtoQ2_Leafs(mbsp->dleafs, mbsp->numleafs);
            q2bsp->dplanes = BSP29_CopyPlanes(mbsp->dplanes, mbsp->numplanes);
            q2bsp->dvertexes = BSP29_CopyVertexes(mbsp->dvertexes, mbsp->numvertexes);
            q2bsp->dnodes = BSP2toQ2_Nodes(mbsp->dnodes, mbsp->numnodes);
            q2bsp->texinfo = MBSPtoQ2_Texinfo(mbsp->texinfo, mbsp->numtexinfo);
            q2bsp->dfaces = BSP2toQ2_Faces(mbsp->dfaces, mbsp->numfaces);
            q2bsp->dedges = BSP2to29_Edges(mbsp->dedges, mbsp->numedges);
            q2bsp->dleaffaces = BSP2to29_Marksurfaces(mbsp->dleaffaces, mbsp->numleaffaces);
            q2bsp->dleafbrushes = MBSPtoQ2_CopyLeafBrushes(mbsp->dleafbrushes, mbsp->numleafbrushes);
            q2bsp->dsurfedges = BSP29_CopySurfedges(mbsp->dsurfedges, mbsp->numsurfedges);
        
            q2bsp->dareas = Q2BSP_CopyAreas(mbsp->dareas, mbsp->numareas);
            q2bsp->dareaportals = Q2BSP_CopyAreaPortals(mbsp->dareaportals, mbsp->numareaportals);
        
            q2bsp->dbrushes = Q2BSP_CopyBrushes(mbsp->dbrushes, mbsp->numbrushes);
            q2bsp->dbrushsides = MBSPtoQ2_CopyBrushSides(mbsp->dbrushsides, mbsp->numbrushsides);
        
            /* Free old data */
            FreeMBSP((mbsp_t *)mbsp);
        
            /* Conversion complete! */
            bspdata->version = to_version;

            return true;
        } else if (to_version == &bspver_qbism) {
            // bspver_generic -> bspver_qbism

            const mbsp_t *mbsp = &bspdata->data.mbsp;
            q2bsp_qbism_t *q2bsp = &bspdata->data.q2bsp_qbism;
        
            memset(q2bsp, 0, sizeof(*q2bsp));
        
            // copy counts
            q2bsp->nummodels = mbsp->nummodels;
            q2bsp->visdatasize = mbsp->visdatasize;
            q2bsp->lightdatasize = mbsp->lightdatasize;
            q2bsp->entdatasize = mbsp->entdatasize;
            q2bsp->numleafs = mbsp->numleafs;
            q2bsp->numplanes = mbsp->numplanes;
            q2bsp->numvertexes = mbsp->numvertexes;
            q2bsp->numnodes = mbsp->numnodes;
            q2bsp->numtexinfo = mbsp->numtexinfo;
            q2bsp->numfaces = mbsp->numfaces;
            q2bsp->numedges = mbsp->numedges;
            q2bsp->numleaffaces = mbsp->numleaffaces;
            q2bsp->numleafbrushes = mbsp->numleafbrushes;
            q2bsp->numsurfedges = mbsp->numsurfedges;
            q2bsp->numareas = mbsp->numareas;
            q2bsp->numareaportals = mbsp->numareaportals;
            q2bsp->numbrushes = mbsp->numbrushes;
            q2bsp->numbrushsides = mbsp->numbrushsides;
        
            // copy or convert data
            q2bsp->dmodels = MBSPtoQ2_Models(mbsp->dmodels, mbsp->nummodels);
            q2bsp->dvis = MBSPtoQ2_CopyVisData(mbsp->dvisdata, &q2bsp->visdatasize, mbsp->numleafs, mbsp->dleafs);
            q2bsp->dlightdata = BSP29_CopyLightData(mbsp->dlightdata, mbsp->lightdatasize);
            q2bsp->dentdata = BSP29_CopyEntData(mbsp->dentdata, mbsp->entdatasize);
            q2bsp->dleafs = MBSPtoQ2_Qbism_Leafs(mbsp->dleafs, mbsp->numleafs);
            q2bsp->dplanes = BSP29_CopyPlanes(mbsp->dplanes, mbsp->numplanes);
            q2bsp->dvertexes = BSP29_CopyVertexes(mbsp->dvertexes, mbsp->numvertexes);
            q2bsp->dnodes = BSP2_CopyNodes(mbsp->dnodes, mbsp->numnodes);
            q2bsp->texinfo = MBSPtoQ2_Texinfo(mbsp->texinfo, mbsp->numtexinfo);
            q2bsp->dfaces = BSP2toQ2_Qbism_Faces(mbsp->dfaces, mbsp->numfaces);
            q2bsp->dedges = BSP2_CopyEdges(mbsp->dedges, mbsp->numedges);
            q2bsp->dleaffaces = BSP2_CopyMarksurfaces(mbsp->dleaffaces, mbsp->numleaffaces);
            q2bsp->dleafbrushes = Q2BSP_Qbism_CopyLeafBrushes(mbsp->dleafbrushes, mbsp->numleafbrushes);
            q2bsp->dsurfedges = BSP29_CopySurfedges(mbsp->dsurfedges, mbsp->numsurfedges);
        
            q2bsp->dareas = Q2BSP_CopyAreas(mbsp->dareas, mbsp->numareas);
            q2bsp->dareaportals = Q2BSP_CopyAreaPortals(mbsp->dareaportals, mbsp->numareaportals);
        
            q2bsp->dbrushes = Q2BSP_CopyBrushes(mbsp->dbrushes, mbsp->numbrushes);
            q2bsp->dbrushsides = Q2BSP_Qbism_CopyBrushSides(mbsp->dbrushsides, mbsp->numbrushsides);
        
            /* Free old data */
            FreeMBSP((mbsp_t *)mbsp);
        
            /* Conversion complete! */
            bspdata->version = to_version;

            return true;
        } else if (to_version == &bspver_bsp2rmq || to_version == &bspver_h2bsp2rmq) {
            // bspver_generic -> bspver_bsp2rmq, bspver_h2bsp2rmq

            bsp2rmq_t *bsp2rmq = &bspdata->data.bsp2rmq;
            const mbsp_t *mbsp = &bspdata->data.mbsp;

            memset(bsp2rmq, 0, sizeof(*bsp2rmq));
        
            // copy counts
            bsp2rmq->nummodels = mbsp->nummodels;
            bsp2rmq->visdatasize = mbsp->visdatasize;
            bsp2rmq->lightdatasize = mbsp->lightdatasize;
            bsp2rmq->texdatasize = mbsp->texdatasize;
            bsp2rmq->entdatasize = mbsp->entdatasize;
            bsp2rmq->numleafs = mbsp->numleafs;
            bsp2rmq->numplanes = mbsp->numplanes;
            bsp2rmq->numvertexes = mbsp->numvertexes;
            bsp2rmq->numnodes = mbsp->numnodes;
            bsp2rmq->numtexinfo = mbsp->numtexinfo;
            bsp2rmq->numfaces = mbsp->numfaces;
            bsp2rmq->numclipnodes = mbsp->numclipnodes;
            bsp2rmq->numedges = mbsp->numedges;
            bsp2rmq->nummarksurfaces = mbsp->numleaffaces;
            bsp2rmq->numsurfedges = mbsp->numsurfedges;
        
            // copy or convert data
            if (to_version == &bspver_h2bsp2rmq) {
                bsp2rmq->dmodels_h2 = H2_CopyModels(mbsp->dmodels, mbsp->nummodels);
            } else {
                bsp2rmq->dmodels_q = BSPH2toQ1_Models(mbsp->dmodels, mbsp->nummodels);
            }
            bsp2rmq->dvisdata = BSP29_CopyVisData(mbsp->dvisdata, mbsp->visdatasize);
            bsp2rmq->dlightdata = BSP29_CopyLightData(mbsp->dlightdata, mbsp->lightdatasize);
            bsp2rmq->dtexdata = BSP29_CopyTexData(mbsp->dtexdata, mbsp->texdatasize);
            bsp2rmq->dentdata = BSP29_CopyEntData(mbsp->dentdata, mbsp->entdatasize);
            bsp2rmq->dleafs = MBSPto2rmq_Leafs(mbsp->dleafs, mbsp->numleafs);
            bsp2rmq->dplanes = BSP29_CopyPlanes(mbsp->dplanes, mbsp->numplanes);
            bsp2rmq->dvertexes = BSP29_CopyVertexes(mbsp->dvertexes, mbsp->numvertexes);
            bsp2rmq->dnodes = BSP2to2rmq_Nodes(mbsp->dnodes, mbsp->numnodes);
            bsp2rmq->texinfo = MBSPto29_Texinfo(mbsp->texinfo, mbsp->numtexinfo);
            bsp2rmq->dfaces = BSP2_CopyFaces(mbsp->dfaces, mbsp->numfaces);
            bsp2rmq->dclipnodes = BSP2_CopyClipnodes(mbsp->dclipnodes, mbsp->numclipnodes);
            bsp2rmq->dedges = BSP2_CopyEdges(mbsp->dedges, mbsp->numedges);
            bsp2rmq->dmarksurfaces = BSP2_CopyMarksurfaces(mbsp->dleaffaces, mbsp->numleaffaces);
            bsp2rmq->dsurfedges = BSP29_CopySurfedges(mbsp->dsurfedges, mbsp->numsurfedges);
        
            /* Free old data */
            FreeMBSP((mbsp_t *)mbsp);
        
            /* Conversion complete! */
            bspdata->version = to_version;
        
            return true;
        } else if (to_version == &bspver_bsp2 || to_version == &bspver_h2bsp2) {
            // bspver_generic -> bspver_bsp2, bspver_h2bsp2

            bsp2_t *bsp2 = &bspdata->data.bsp2;
            const mbsp_t *mbsp = &bspdata->data.mbsp;
        
            memset(bsp2, 0, sizeof(*bsp2));
        
            // copy counts
            bsp2->nummodels = mbsp->nummodels;
            bsp2->visdatasize = mbsp->visdatasize;
            bsp2->lightdatasize = mbsp->lightdatasize;
            bsp2->texdatasize = mbsp->texdatasize;
            bsp2->entdatasize = mbsp->entdatasize;
            bsp2->numleafs = mbsp->numleafs;
            bsp2->numplanes = mbsp->numplanes;
            bsp2->numvertexes = mbsp->numvertexes;
            bsp2->numnodes = mbsp->numnodes;
            bsp2->numtexinfo = mbsp->numtexinfo;
            bsp2->numfaces = mbsp->numfaces;
            bsp2->numclipnodes = mbsp->numclipnodes;
            bsp2->numedges = mbsp->numedges;
            bsp2->nummarksurfaces = mbsp->numleaffaces;
            bsp2->numsurfedges = mbsp->numsurfedges;
        
            // copy or convert data
            if (to_version == &bspver_h2bsp2) {
                bsp2->dmodels_h2 = H2_CopyModels(mbsp->dmodels, mbsp->nummodels);
            } else {
                bsp2->dmodels_q = BSPH2toQ1_Models(mbsp->dmodels, mbsp->nummodels);
            }
            bsp2->dvisdata = BSP29_CopyVisData(mbsp->dvisdata, mbsp->visdatasize);
            bsp2->dlightdata = BSP29_CopyLightData(mbsp->dlightdata, mbsp->lightdatasize);
            bsp2->dtexdata = BSP29_CopyTexData(mbsp->dtexdata, mbsp->texdatasize);
            bsp2->dentdata = BSP29_CopyEntData(mbsp->dentdata, mbsp->entdatasize);
            bsp2->dleafs = MBSPto2_Leafs(mbsp->dleafs, mbsp->numleafs);
            bsp2->dplanes = BSP29_CopyPlanes(mbsp->dplanes, mbsp->numplanes);
            bsp2->dvertexes = BSP29_CopyVertexes(mbsp->dvertexes, mbsp->numvertexes);
            bsp2->dnodes = BSP2_CopyNodes(mbsp->dnodes, mbsp->numnodes);
            bsp2->texinfo = MBSPto29_Texinfo(mbsp->texinfo, mbsp->numtexinfo);
            bsp2->dfaces = BSP2_CopyFaces(mbsp->dfaces, mbsp->numfaces);
            bsp2->dclipnodes = BSP2_CopyClipnodes(mbsp->dclipnodes, mbsp->numclipnodes);
            bsp2->dedges = BSP2_CopyEdges(mbsp->dedges, mbsp->numedges);
            bsp2->dmarksurfaces = BSP2_CopyMarksurfaces(mbsp->dleaffaces, mbsp->numleaffaces);
            bsp2->dsurfedges = BSP29_CopySurfedges(mbsp->dsurfedges, mbsp->numsurfedges);
        
            /* Free old data */
            FreeMBSP((mbsp_t *)mbsp);
        
            /* Conversion complete! */
            bspdata->version = to_version;
        
            return true;
        }
    }
    
    Error("Don't know how to convert BSP version %s to %s",
          BSPVersionString(bspdata->version), BSPVersionString(to_version));
}

static int 
isHexen2(const dheader_t *header)
{
    /*
        the world should always have some face.
        however, if the sizes are wrong then we're actually reading headnode[6]. hexen2 only used 5 hulls, so this should be 0 in hexen2, and not in quake.
    */
    const dmodelq1_t *modelsq1 = (const dmodelq1_t*)((const uint8_t *)header + header->lumps[LUMP_MODELS].fileofs);
    return !modelsq1->numfaces;
}

/*
 * =========================================================================
 * ...
 * =========================================================================
 */

const lumpspec_t lumpspec_bsp29[] = {
    { "entities",     sizeof(char)              },
    { "planes",       sizeof(dplane_t)          },
    { "texture",      sizeof(uint8_t)              },
    { "vertexes",     sizeof(dvertex_t)         },
    { "visibility",   sizeof(uint8_t)              },
    { "nodes",        sizeof(bsp29_dnode_t)     },
    { "texinfos",     sizeof(texinfo_t)         },
    { "faces",        sizeof(bsp29_dface_t)     },
    { "lighting",     sizeof(uint8_t)              },
    { "clipnodes",    sizeof(bsp29_dclipnode_t) },
    { "leafs",        sizeof(bsp29_dleaf_t)     },
    { "marksurfaces", sizeof(uint16_t)          },
    { "edges",        sizeof(bsp29_dedge_t)     },
    { "surfedges",    sizeof(int32_t)           },
    { "models",       sizeof(dmodelq1_t)        },
};

const lumpspec_t lumpspec_bsp2rmq[] = {
    { "entities",     sizeof(char)              },
    { "planes",       sizeof(dplane_t)          },
    { "texture",      sizeof(uint8_t)              },
    { "vertexes",     sizeof(dvertex_t)         },
    { "visibility",   sizeof(uint8_t)              },
    { "nodes",        sizeof(bsp2rmq_dnode_t)   },
    { "texinfos",     sizeof(texinfo_t)         },
    { "faces",        sizeof(bsp2_dface_t)      },
    { "lighting",     sizeof(uint8_t)              },
    { "clipnodes",    sizeof(bsp2_dclipnode_t ) },
    { "leafs",        sizeof(bsp2rmq_dleaf_t)   },
    { "marksurfaces", sizeof(uint32_t)          },
    { "edges",        sizeof(bsp2_dedge_t)      },
    { "surfedges",    sizeof(int32_t)           },
    { "models",       sizeof(dmodelq1_t)        },
};

const lumpspec_t lumpspec_bsp2[] = {
    { "entities",     sizeof(char)              },
    { "planes",       sizeof(dplane_t)          },
    { "texture",      sizeof(uint8_t)              },
    { "vertexes",     sizeof(dvertex_t)         },
    { "visibility",   sizeof(uint8_t)              },
    { "nodes",        sizeof(bsp2_dnode_t)      },
    { "texinfos",     sizeof(texinfo_t)         },
    { "faces",        sizeof(bsp2_dface_t)      },
    { "lighting",     sizeof(uint8_t)              },
    { "clipnodes",    sizeof(bsp2_dclipnode_t ) },
    { "leafs",        sizeof(bsp2_dleaf_t)      },
    { "marksurfaces", sizeof(uint32_t)          },
    { "edges",        sizeof(bsp2_dedge_t)      },
    { "surfedges",    sizeof(int32_t)           },
    { "models",       sizeof(dmodelq1_t)        },
};

const lumpspec_t lumpspec_bsp29_h2[] = {
    { "entities",     sizeof(char)              },
    { "planes",       sizeof(dplane_t)          },
    { "texture",      sizeof(uint8_t)           },
    { "vertexes",     sizeof(dvertex_t)         },
    { "visibility",   sizeof(uint8_t)           },
    { "nodes",        sizeof(bsp29_dnode_t)     },
    { "texinfos",     sizeof(texinfo_t)         },
    { "faces",        sizeof(bsp29_dface_t)     },
    { "lighting",     sizeof(uint8_t)           },
    { "clipnodes",    sizeof(bsp29_dclipnode_t) },
    { "leafs",        sizeof(bsp29_dleaf_t)     },
    { "marksurfaces", sizeof(uint16_t)          },
    { "edges",        sizeof(bsp29_dedge_t)     },
    { "surfedges",    sizeof(int32_t)           },
    { "models",       sizeof(dmodelh2_t)        },
};

const lumpspec_t lumpspec_bsp2rmq_h2[] = {
    { "entities",     sizeof(char)              },
    { "planes",       sizeof(dplane_t)          },
    { "texture",      sizeof(uint8_t)           },
    { "vertexes",     sizeof(dvertex_t)         },
    { "visibility",   sizeof(uint8_t)           },
    { "nodes",        sizeof(bsp2rmq_dnode_t)   },
    { "texinfos",     sizeof(texinfo_t)         },
    { "faces",        sizeof(bsp2_dface_t)      },
    { "lighting",     sizeof(uint8_t)           },
    { "clipnodes",    sizeof(bsp2_dclipnode_t ) },
    { "leafs",        sizeof(bsp2rmq_dleaf_t)   },
    { "marksurfaces", sizeof(uint32_t)          },
    { "edges",        sizeof(bsp2_dedge_t)      },
    { "surfedges",    sizeof(int32_t)           },
    { "models",       sizeof(dmodelh2_t)        },
};

const lumpspec_t lumpspec_bsp2_h2[] = {
    { "entities",     sizeof(char)              },
    { "planes",       sizeof(dplane_t)          },
    { "texture",      sizeof(uint8_t)           },
    { "vertexes",     sizeof(dvertex_t)         },
    { "visibility",   sizeof(uint8_t)           },
    { "nodes",        sizeof(bsp2_dnode_t)      },
    { "texinfos",     sizeof(texinfo_t)         },
    { "faces",        sizeof(bsp2_dface_t)      },
    { "lighting",     sizeof(uint8_t)           },
    { "clipnodes",    sizeof(bsp2_dclipnode_t ) },
    { "leafs",        sizeof(bsp2_dleaf_t)      },
    { "marksurfaces", sizeof(uint32_t)          },
    { "edges",        sizeof(bsp2_dedge_t)      },
    { "surfedges",    sizeof(int32_t)           },
    { "models",       sizeof(dmodelh2_t)        },
};

const lumpspec_t lumpspec_q2bsp[] = {
    { "entities",     sizeof(char)              },
    { "planes",       sizeof(dplane_t)          },
    { "vertexes",     sizeof(dvertex_t)         },
    { "visibility",   sizeof(uint8_t)              },
    { "nodes",        sizeof(q2_dnode_t)        },
    { "texinfos",     sizeof(q2_texinfo_t)      },
    { "faces",        sizeof(q2_dface_t)        },
    { "lighting",     sizeof(uint8_t)              },
    { "leafs",        sizeof(q2_dleaf_t)        },
    { "leaffaces",    sizeof(uint16_t)          },
    { "leafbrushes",  sizeof(uint16_t)          },
    { "edges",        sizeof(bsp29_dedge_t)     },
    { "surfedges",    sizeof(int32_t)           },
    { "models",       sizeof(q2_dmodel_t)       },
    { "brushes",      sizeof(dbrush_t)          },
    { "brushsides",   sizeof(dbrushside_t)      },
    { "pop",          sizeof(uint8_t)              },
    { "areas",        sizeof(darea_t)           },
    { "areaportals",  sizeof(dareaportal_t)     },
};

const lumpspec_t lumpspec_qbism[] = {
    { "entities",     sizeof(char)              },
    { "planes",       sizeof(dplane_t)          },
    { "vertexes",     sizeof(dvertex_t)         },
    { "visibility",   sizeof(uint8_t)              },
    { "nodes",        sizeof(q2_dnode_qbism_t)        },
    { "texinfos",     sizeof(q2_texinfo_t)      },
    { "faces",        sizeof(q2_dface_qbism_t)        },
    { "lighting",     sizeof(uint8_t)              },
    { "leafs",        sizeof(q2_dleaf_qbism_t)        },
    { "leaffaces",    sizeof(uint32_t)          },
    { "leafbrushes",  sizeof(uint32_t)          },
    { "edges",        sizeof(q2_dedge_qbism_t)     },
    { "surfedges",    sizeof(int32_t)           },
    { "models",       sizeof(q2_dmodel_t)       },
    { "brushes",      sizeof(dbrush_t)          },
    { "brushsides",   sizeof(q2_dbrushside_qbism_t)      },
    { "pop",          sizeof(uint8_t)              },
    { "areas",        sizeof(darea_t)           },
    { "areaportals",  sizeof(dareaportal_t)     },
};

static const lumpspec_t *
LumpspecsForVersion(const bspversion_t* version) {
    const lumpspec_t* lumpspec;

    if (version == &bspver_q1 || version == &bspver_hl) {
        lumpspec = lumpspec_bsp29;
    } else if (version == &bspver_bsp2rmq) {
        lumpspec = lumpspec_bsp2rmq;
    } else if (version == &bspver_bsp2) {
        lumpspec = lumpspec_bsp2;
    } else if (version == &bspver_h2) {
        lumpspec = lumpspec_bsp29_h2;
    } else if (version == &bspver_h2bsp2rmq) {
        lumpspec = lumpspec_bsp2rmq_h2;
    } else if (version == &bspver_h2bsp2) {
        lumpspec = lumpspec_bsp2_h2;
    } else if (version == &bspver_q2) {
        lumpspec = lumpspec_q2bsp;
    } else if (version == &bspver_qbism) {
        lumpspec = lumpspec_qbism;
    } else {
        Error("Unsupported BSP version: %s", BSPVersionString(version));
    }
    return lumpspec;
}

static int
CopyLump(const void *header, const bspversion_t *version, const lump_t *lumps, int lumpnum, void *destptr)
{
    const lumpspec_t *lumpspecs = LumpspecsForVersion(version);
    const lumpspec_t *lumpspec = &lumpspecs[lumpnum];
    uint8_t **bufferptr = static_cast<uint8_t **>(destptr);
    uint8_t *buffer = *bufferptr;
    int length;
    int ofs;

    length = lumps[lumpnum].filelen;
    ofs = lumps[lumpnum].fileofs;

    if (buffer)
        free(buffer);

    {
        if (length % lumpspec->size)
            Error("%s: odd %s lump size", __func__, lumpspec->name);

        buffer = *bufferptr = static_cast<uint8_t *>(malloc(length + 1));
        if (!buffer)
            Error("%s: allocation of %i bytes failed.", __func__, length);

        memcpy(buffer, (const uint8_t *)header + ofs, length);
        buffer[length] = 0; /* In case of corrupt entity lump */

        return length / lumpspec->size;
    }
}

void BSPX_AddLump(bspdata_t *bspdata, const char *xname, const void *xdata, size_t xsize)
{
    bspxentry_t *e;
    bspxentry_t **link;
    if (!xdata)
    {
        for (link = &bspdata->bspxentries; *link; )
        {
            e = *link;
            if (!strcmp(e->lumpname, xname))
            {
                *link = e->next;
                free(e);
                break;
            }
            else
                link = &(*link)->next;
        }
        return;
    }
    for (e = bspdata->bspxentries; e; e = e->next)
    {
        if (!strcmp(e->lumpname, xname))
            break;
    }
    if (!e)
    {
        e = static_cast<bspxentry_t *>(malloc(sizeof(*e)));
        memset(e, 0, sizeof(*e));
        strncpy(e->lumpname, xname, sizeof(e->lumpname));
        e->next = bspdata->bspxentries;
        bspdata->bspxentries = e;
    }

    //ericw -- make a copy
    uint8_t *xdata_copy = (uint8_t*) malloc(xsize);
    memcpy(xdata_copy, xdata, xsize);
    
    e->lumpdata = xdata_copy;
    e->lumpsize = xsize;
}
const void *BSPX_GetLump(bspdata_t *bspdata, const char *xname, size_t *xsize)
{
    bspxentry_t *e;
    for (e = bspdata->bspxentries; e; e = e->next)
    {
        if (!strcmp(e->lumpname, xname))
            break;
    }
    if (e)
    {
        if (xsize)
            *xsize = e->lumpsize;
        return e->lumpdata;
    }
    else
    {
        if (xsize)
            *xsize = 0;
        return NULL;
    }
}

/*
 * =============
 * LoadBSPFile
 * =============
 */
void
LoadBSPFile(char *filename, bspdata_t *bspdata)
{
    int i;
    uint32_t bspxofs;
    const bspx_header_t *bspx;

    bspdata->bspxentries = NULL;
    
    logprint("LoadBSPFile: '%s'\n", filename);
    
    /* load the file header */
    uint8_t *file_data;
    uint32_t flen = LoadFilePak(filename, &file_data);

    /* transfer the header data to these variables */
    int numlumps;
    lump_t *lumps;

    /* check for IBSP */
    bspversion_t temp_version { LittleLong(((int *)file_data)[0]) };

    if (temp_version.ident == Q2_BSPIDENT ||
        temp_version.ident == Q2_QBISMIDENT) {
        q2_dheader_t *q2header = (q2_dheader_t *)file_data;
        q2header->version = LittleLong(q2header->version);
        
        numlumps = Q2_HEADER_LUMPS;
        temp_version.version = q2header->version;
        lumps = q2header->lumps;
    } else {
        dheader_t *q1header = (dheader_t *)file_data;
        q1header->version = LittleLong(q1header->version);
        
        numlumps = BSP_LUMPS;
        lumps = q1header->lumps;

        // not useful for Q1BSP, but we'll initialize it to -1
        temp_version.version = NO_VERSION;
    }
    
    /* check the file version */
    if (!BSPVersionSupported(temp_version.ident, temp_version.version, &bspdata->version)) {
        logprint("BSP is version %s\n", BSPVersionString(&temp_version));
        Error("Sorry, this bsp version is not supported.");
    } else {
        // special case handling for Hexen II
        if (isHexen2((dheader_t*)file_data)) {
            if (bspdata->version == &bspver_q1) {
                bspdata->version = &bspver_h2;
            } else if (bspdata->version == &bspver_bsp2) {
                bspdata->version = &bspver_h2bsp2;
            } else if (bspdata->version == &bspver_bsp2rmq) {
                bspdata->version = &bspver_h2bsp2rmq;
            }
        }

        logprint("BSP is version %s\n", BSPVersionString(bspdata->version));
    }

    /* swap the lump headers */
    for (i = 0; i < numlumps; i++) {
        lumps[i].fileofs = LittleLong(lumps[i].fileofs);
        lumps[i].filelen = LittleLong(lumps[i].filelen);
    }

    /* copy the data */
    if (bspdata->version == &bspver_q2) {
        q2_dheader_t *header = (q2_dheader_t *)file_data;
        q2bsp_t *bsp = &bspdata->data.q2bsp;

        memset(bsp, 0, sizeof(*bsp));

        bsp->nummodels = CopyLump (header, bspdata->version, header->lumps, Q2_LUMP_MODELS, &bsp->dmodels);
        bsp->numvertexes = CopyLump (header, bspdata->version, header->lumps, Q2_LUMP_VERTEXES, &bsp->dvertexes);
        bsp->numplanes = CopyLump (header, bspdata->version, header->lumps, Q2_LUMP_PLANES, &bsp->dplanes);
        bsp->numleafs = CopyLump (header, bspdata->version, header->lumps, Q2_LUMP_LEAFS, &bsp->dleafs);
        bsp->numnodes = CopyLump (header, bspdata->version, header->lumps, Q2_LUMP_NODES, &bsp->dnodes);
        bsp->numtexinfo = CopyLump (header, bspdata->version, header->lumps, Q2_LUMP_TEXINFO, &bsp->texinfo);
        bsp->numfaces = CopyLump (header, bspdata->version, header->lumps, Q2_LUMP_FACES, &bsp->dfaces);
        bsp->numleaffaces = CopyLump (header, bspdata->version, header->lumps, Q2_LUMP_LEAFFACES, &bsp->dleaffaces);
        bsp->numleafbrushes = CopyLump (header, bspdata->version, header->lumps, Q2_LUMP_LEAFBRUSHES, &bsp->dleafbrushes);
        bsp->numsurfedges = CopyLump (header, bspdata->version, header->lumps, Q2_LUMP_SURFEDGES, &bsp->dsurfedges);
        bsp->numedges = CopyLump (header, bspdata->version, header->lumps, Q2_LUMP_EDGES, &bsp->dedges);
        bsp->numbrushes = CopyLump (header, bspdata->version, header->lumps, Q2_LUMP_BRUSHES, &bsp->dbrushes);
        bsp->numbrushsides = CopyLump (header, bspdata->version, header->lumps, Q2_LUMP_BRUSHSIDES, &bsp->dbrushsides);
        bsp->numareas = CopyLump (header, bspdata->version, header->lumps, Q2_LUMP_AREAS, &bsp->dareas);
        bsp->numareaportals = CopyLump (header, bspdata->version, header->lumps, Q2_LUMP_AREAPORTALS, &bsp->dareaportals);
        
        bsp->visdatasize = CopyLump (header, bspdata->version, header->lumps, Q2_LUMP_VISIBILITY, &bsp->dvis);
        bsp->lightdatasize = CopyLump (header, bspdata->version, header->lumps, Q2_LUMP_LIGHTING, &bsp->dlightdata);
        bsp->entdatasize = CopyLump (header, bspdata->version, header->lumps, Q2_LUMP_ENTITIES, &bsp->dentdata);
        
        CopyLump (header, bspdata->version, header->lumps, Q2_LUMP_POP, &bsp->dpop);
    } else if (bspdata->version == &bspver_qbism) {
        q2_dheader_t *header = (q2_dheader_t *)file_data;
        q2bsp_qbism_t *bsp = &bspdata->data.q2bsp_qbism;

        memset(bsp, 0, sizeof(*bsp));

        bsp->nummodels = CopyLump (header, bspdata->version, header->lumps, Q2_LUMP_MODELS, &bsp->dmodels);
        bsp->numvertexes = CopyLump (header, bspdata->version, header->lumps, Q2_LUMP_VERTEXES, &bsp->dvertexes);
        bsp->numplanes = CopyLump (header, bspdata->version, header->lumps, Q2_LUMP_PLANES, &bsp->dplanes);
        bsp->numleafs = CopyLump (header, bspdata->version, header->lumps, Q2_LUMP_LEAFS, &bsp->dleafs);
        bsp->numnodes = CopyLump (header, bspdata->version, header->lumps, Q2_LUMP_NODES, &bsp->dnodes);
        bsp->numtexinfo = CopyLump (header, bspdata->version, header->lumps, Q2_LUMP_TEXINFO, &bsp->texinfo);
        bsp->numfaces = CopyLump (header, bspdata->version, header->lumps, Q2_LUMP_FACES, &bsp->dfaces);
        bsp->numleaffaces = CopyLump (header, bspdata->version, header->lumps, Q2_LUMP_LEAFFACES, &bsp->dleaffaces);
        bsp->numleafbrushes = CopyLump (header, bspdata->version, header->lumps, Q2_LUMP_LEAFBRUSHES, &bsp->dleafbrushes);
        bsp->numsurfedges = CopyLump (header, bspdata->version, header->lumps, Q2_LUMP_SURFEDGES, &bsp->dsurfedges);
        bsp->numedges = CopyLump (header, bspdata->version, header->lumps, Q2_LUMP_EDGES, &bsp->dedges);
        bsp->numbrushes = CopyLump (header, bspdata->version, header->lumps, Q2_LUMP_BRUSHES, &bsp->dbrushes);
        bsp->numbrushsides = CopyLump (header, bspdata->version, header->lumps, Q2_LUMP_BRUSHSIDES, &bsp->dbrushsides);
        bsp->numareas = CopyLump (header, bspdata->version, header->lumps, Q2_LUMP_AREAS, &bsp->dareas);
        bsp->numareaportals = CopyLump (header, bspdata->version, header->lumps, Q2_LUMP_AREAPORTALS, &bsp->dareaportals);
        
        bsp->visdatasize = CopyLump (header, bspdata->version, header->lumps, Q2_LUMP_VISIBILITY, &bsp->dvis);
        bsp->lightdatasize = CopyLump (header, bspdata->version, header->lumps, Q2_LUMP_LIGHTING, &bsp->dlightdata);
        bsp->entdatasize = CopyLump (header, bspdata->version, header->lumps, Q2_LUMP_ENTITIES, &bsp->dentdata);
        
        CopyLump (header, bspdata->version, header->lumps, Q2_LUMP_POP, &bsp->dpop);
    } else if (bspdata->version == &bspver_q1 || bspdata->version == &bspver_h2 || bspdata->version == &bspver_hl) {
        dheader_t *header = (dheader_t *)file_data;
        bsp29_t *bsp = &bspdata->data.bsp29;

        memset(bsp, 0, sizeof(*bsp));

        if (bspdata->version == &bspver_h2) {
            bsp->nummodels = CopyLump(header, bspdata->version, header->lumps, LUMP_MODELS, &bsp->dmodels_h2);
        } else {
            bsp->nummodels = CopyLump(header, bspdata->version, header->lumps, LUMP_MODELS, &bsp->dmodels_q);
        }
        bsp->numvertexes = CopyLump(header, bspdata->version, header->lumps, LUMP_VERTEXES, &bsp->dvertexes);
        bsp->numplanes = CopyLump(header, bspdata->version, header->lumps, LUMP_PLANES, &bsp->dplanes);
        bsp->numleafs = CopyLump(header, bspdata->version, header->lumps, LUMP_LEAFS, &bsp->dleafs);
        bsp->numnodes = CopyLump(header, bspdata->version, header->lumps, LUMP_NODES, &bsp->dnodes);
        bsp->numtexinfo = CopyLump(header, bspdata->version, header->lumps, LUMP_TEXINFO, &bsp->texinfo);
        bsp->numclipnodes = CopyLump(header, bspdata->version, header->lumps, LUMP_CLIPNODES, &bsp->dclipnodes);
        bsp->numfaces = CopyLump(header, bspdata->version, header->lumps, LUMP_FACES, &bsp->dfaces);
        bsp->nummarksurfaces = CopyLump(header, bspdata->version, header->lumps, LUMP_MARKSURFACES, &bsp->dmarksurfaces);
        bsp->numsurfedges = CopyLump(header, bspdata->version, header->lumps, LUMP_SURFEDGES, &bsp->dsurfedges);
        bsp->numedges = CopyLump(header, bspdata->version, header->lumps, LUMP_EDGES, &bsp->dedges);

        bsp->texdatasize = CopyLump(header, bspdata->version, header->lumps, LUMP_TEXTURES, &bsp->dtexdata);
        bsp->visdatasize = CopyLump(header, bspdata->version, header->lumps, LUMP_VISIBILITY, &bsp->dvisdata);
        bsp->lightdatasize = CopyLump(header, bspdata->version, header->lumps, LUMP_LIGHTING, &bsp->dlightdata);
        bsp->entdatasize = CopyLump(header, bspdata->version, header->lumps, LUMP_ENTITIES, &bsp->dentdata);
    } else if (bspdata->version == &bspver_bsp2rmq || bspdata->version == &bspver_h2bsp2rmq) {
        dheader_t *header = (dheader_t *)file_data;
        bsp2rmq_t *bsp = &bspdata->data.bsp2rmq;

        memset(bsp, 0, sizeof(*bsp));

        if (bspdata->version == &bspver_h2bsp2rmq) {
            bsp->nummodels = CopyLump(header, bspdata->version, header->lumps, LUMP_MODELS, &bsp->dmodels_h2);
        } else {
            bsp->nummodels = CopyLump(header, bspdata->version, header->lumps, LUMP_MODELS, &bsp->dmodels_q);
        }
        bsp->numvertexes = CopyLump(header, bspdata->version, header->lumps, LUMP_VERTEXES, &bsp->dvertexes);
        bsp->numplanes = CopyLump(header, bspdata->version, header->lumps, LUMP_PLANES, &bsp->dplanes);
        bsp->numleafs = CopyLump(header, bspdata->version, header->lumps, LUMP_LEAFS, &bsp->dleafs);
        bsp->numnodes = CopyLump(header, bspdata->version, header->lumps, LUMP_NODES, &bsp->dnodes);
        bsp->numtexinfo = CopyLump(header, bspdata->version, header->lumps, LUMP_TEXINFO, &bsp->texinfo);
        bsp->numclipnodes = CopyLump(header, bspdata->version, header->lumps, LUMP_CLIPNODES, &bsp->dclipnodes);
        bsp->numfaces = CopyLump(header, bspdata->version, header->lumps, LUMP_FACES, &bsp->dfaces);
        bsp->nummarksurfaces = CopyLump(header, bspdata->version, header->lumps, LUMP_MARKSURFACES, &bsp->dmarksurfaces);
        bsp->numsurfedges = CopyLump(header, bspdata->version, header->lumps, LUMP_SURFEDGES, &bsp->dsurfedges);
        bsp->numedges = CopyLump(header, bspdata->version, header->lumps, LUMP_EDGES, &bsp->dedges);

        bsp->texdatasize = CopyLump(header, bspdata->version, header->lumps, LUMP_TEXTURES, &bsp->dtexdata);
        bsp->visdatasize = CopyLump(header, bspdata->version, header->lumps, LUMP_VISIBILITY, &bsp->dvisdata);
        bsp->lightdatasize = CopyLump(header, bspdata->version, header->lumps, LUMP_LIGHTING, &bsp->dlightdata);
        bsp->entdatasize = CopyLump(header, bspdata->version, header->lumps, LUMP_ENTITIES, &bsp->dentdata);
    } else if (bspdata->version == &bspver_bsp2 || bspdata->version == &bspver_h2bsp2) {
        dheader_t *header = (dheader_t *)file_data;
        bsp2_t *bsp = &bspdata->data.bsp2;

        memset(bsp, 0, sizeof(*bsp));

        if (bspdata->version == &bspver_h2bsp2) {
            bsp->nummodels = CopyLump(header, bspdata->version, header->lumps, LUMP_MODELS, &bsp->dmodels_h2);
        } else {
            bsp->nummodels = CopyLump(header, bspdata->version, header->lumps, LUMP_MODELS, &bsp->dmodels_q);
        }
        bsp->numvertexes = CopyLump(header, bspdata->version, header->lumps, LUMP_VERTEXES, &bsp->dvertexes);
        bsp->numplanes = CopyLump(header, bspdata->version, header->lumps, LUMP_PLANES, &bsp->dplanes);
        bsp->numleafs = CopyLump(header, bspdata->version, header->lumps, LUMP_LEAFS, &bsp->dleafs);
        bsp->numnodes = CopyLump(header, bspdata->version, header->lumps, LUMP_NODES, &bsp->dnodes);
        bsp->numtexinfo = CopyLump(header, bspdata->version, header->lumps, LUMP_TEXINFO, &bsp->texinfo);
        bsp->numclipnodes = CopyLump(header, bspdata->version, header->lumps, LUMP_CLIPNODES, &bsp->dclipnodes);
        bsp->numfaces = CopyLump(header, bspdata->version, header->lumps, LUMP_FACES, &bsp->dfaces);
        bsp->nummarksurfaces = CopyLump(header, bspdata->version, header->lumps, LUMP_MARKSURFACES, &bsp->dmarksurfaces);
        bsp->numsurfedges = CopyLump(header, bspdata->version, header->lumps, LUMP_SURFEDGES, &bsp->dsurfedges);
        bsp->numedges = CopyLump(header, bspdata->version, header->lumps, LUMP_EDGES, &bsp->dedges);

        bsp->texdatasize = CopyLump(header, bspdata->version, header->lumps, LUMP_TEXTURES, &bsp->dtexdata);
        bsp->visdatasize = CopyLump(header, bspdata->version, header->lumps, LUMP_VISIBILITY, &bsp->dvisdata);
        bsp->lightdatasize = CopyLump(header, bspdata->version, header->lumps, LUMP_LIGHTING, &bsp->dlightdata);
        bsp->entdatasize = CopyLump(header, bspdata->version, header->lumps, LUMP_ENTITIES, &bsp->dentdata);
    } else {
        Error("Unknown format");
    }

    // detect BSPX
    dheader_t *header = (dheader_t *)file_data;
        
    /*bspx header is positioned exactly+4align at the end of the last lump position (regardless of order)*/
    for (i = 0, bspxofs = 0; i < BSP_LUMPS; i++)
    {
        if (bspxofs < header->lumps[i].fileofs + header->lumps[i].filelen)
        bspxofs = header->lumps[i].fileofs + header->lumps[i].filelen;
    }
    bspxofs = (bspxofs+3) & ~3;
    /*okay, so that's where it *should* be if it exists */
    if (bspxofs + sizeof(*bspx) <= flen)
    {
        int xlumps;
        const bspx_lump_t *xlump;
        bspx = (const bspx_header_t*)((const uint8_t*)header + bspxofs);
        xlump = (const bspx_lump_t*)(bspx+1);
        xlumps = LittleLong(bspx->numlumps);
        if (!memcmp(&bspx->id,"BSPX",4) && xlumps >= 0 && bspxofs+sizeof(*bspx)+sizeof(*xlump)*xlumps <= flen)
        {
            /*header seems valid so far. just add the lumps as we normally would if we were generating them, ensuring that they get written out anew*/
            while(xlumps --> 0)
            {
                uint32_t ofs = LittleLong(xlump[xlumps].fileofs);
                uint32_t len = LittleLong(xlump[xlumps].filelen);
                void *lumpdata = malloc(len);
                memcpy(lumpdata, (const uint8_t*)header + ofs, len);
                BSPX_AddLump(bspdata, xlump[xlumps].lumpname, lumpdata, len);
            }
        }
        else
        {
            if (!memcmp(&bspx->id,"BSPX",4))
                printf("invalid bspx header\n");
        }
    }
    
    /* everything has been copied out */
    free(file_data);

    /* swap everything */
    SwapBSPFile(bspdata, TO_CPU);
}

/* ========================================================================= */

typedef struct {
    const bspversion_t *version;
    
    // which one is used depends on version
    union {
        dheader_t q1header;
        q2_dheader_t q2header;
    };
    
    FILE *file;
} bspfile_t;

static void
AddLump(bspfile_t *bspfile, int lumpnum, const void *data, int count)
{
    bool q2 = false;
    size_t size;
    const lumpspec_t *lumpspecs = LumpspecsForVersion(bspfile->version);
    const lumpspec_t *lumpspec = &lumpspecs[lumpnum];
    lump_t *lumps;

    if (bspfile->version->version != NO_VERSION) {
        lumps = bspfile->q2header.lumps;
    } else {
        lumps = bspfile->q1header.lumps;
    }

    size = lumpspec->size * count;

    uint8_t pad[4] = {0};
    lump_t *lump = &lumps[lumpnum];
    
    lump->fileofs = LittleLong(ftell(bspfile->file));
    lump->filelen = LittleLong(size);
    SafeWrite(bspfile->file, data, size);
    if (size % 4)
        SafeWrite(bspfile->file, pad, 4 - (size % 4));
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
    memset(&bspfile, 0, sizeof(bspfile));

    SwapBSPFile(bspdata, TO_DISK);

    bspfile.version = bspdata->version;

    // headers are union'd, so this sets both
    bspfile.q2header.ident = LittleLong(bspfile.version->ident);

    if (bspfile.version->version != NO_VERSION) {
        bspfile.q2header.version = LittleLong(bspfile.version->version);
    }
    
    logprint("Writing %s as BSP version %s\n", filename, BSPVersionString(bspdata->version));
    bspfile.file = SafeOpenWrite(filename);

    /* Save header space, updated after adding the lumps */
    if (bspfile.version->version != NO_VERSION) {
    	SafeWrite(bspfile.file, &bspfile.q2header, sizeof(bspfile.q2header));
    } else {
        SafeWrite(bspfile.file, &bspfile.q1header, sizeof(bspfile.q1header));
    }

    if (bspdata->version == &bspver_q1 ||
        bspdata->version == &bspver_h2 ||
        bspdata->version == &bspver_hl) {
        const bsp29_t *bsp = &bspdata->data.bsp29;

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
        if (bspdata->version == &bspver_h2) {
            AddLump(&bspfile, LUMP_MODELS, bsp->dmodels_h2, bsp->nummodels);
        } else {
            AddLump(&bspfile, LUMP_MODELS, bsp->dmodels_q, bsp->nummodels);
        }

        AddLump(&bspfile, LUMP_LIGHTING, bsp->dlightdata, bsp->lightdatasize);
        AddLump(&bspfile, LUMP_VISIBILITY, bsp->dvisdata, bsp->visdatasize);
        AddLump(&bspfile, LUMP_ENTITIES, bsp->dentdata, bsp->entdatasize);
        AddLump(&bspfile, LUMP_TEXTURES, bsp->dtexdata, bsp->texdatasize);
    } else if (bspdata->version == &bspver_bsp2rmq || bspdata->version == &bspver_h2bsp2rmq) {
        const bsp2rmq_t *bsp = &bspdata->data.bsp2rmq;

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
        if (bspdata->version == &bspver_h2bsp2rmq) {
            AddLump(&bspfile, LUMP_MODELS, bsp->dmodels_h2, bsp->nummodels);
        } else {
            AddLump(&bspfile, LUMP_MODELS, bsp->dmodels_q, bsp->nummodels);
        }

        AddLump(&bspfile, LUMP_LIGHTING, bsp->dlightdata, bsp->lightdatasize);
        AddLump(&bspfile, LUMP_VISIBILITY, bsp->dvisdata, bsp->visdatasize);
        AddLump(&bspfile, LUMP_ENTITIES, bsp->dentdata, bsp->entdatasize);
        AddLump(&bspfile, LUMP_TEXTURES, bsp->dtexdata, bsp->texdatasize);
    } else if (bspdata->version == &bspver_bsp2 || bspdata->version == &bspver_h2bsp2) {
        const bsp2_t *bsp = &bspdata->data.bsp2;

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
        if (bspdata->version == &bspver_h2bsp2) {
            AddLump(&bspfile, LUMP_MODELS, bsp->dmodels_h2, bsp->nummodels);
        } else {
            AddLump(&bspfile, LUMP_MODELS, bsp->dmodels_q, bsp->nummodels);
        }

        AddLump(&bspfile, LUMP_LIGHTING, bsp->dlightdata, bsp->lightdatasize);
        AddLump(&bspfile, LUMP_VISIBILITY, bsp->dvisdata, bsp->visdatasize);
        AddLump(&bspfile, LUMP_ENTITIES, bsp->dentdata, bsp->entdatasize);
        AddLump(&bspfile, LUMP_TEXTURES, bsp->dtexdata, bsp->texdatasize);
    } else if (bspdata->version == &bspver_q2) {
        const q2bsp_t *bsp = &bspdata->data.q2bsp;

        AddLump(&bspfile, Q2_LUMP_MODELS, bsp->dmodels, bsp->nummodels);
        AddLump(&bspfile, Q2_LUMP_VERTEXES, bsp->dvertexes, bsp->numvertexes);
        AddLump(&bspfile, Q2_LUMP_PLANES, bsp->dplanes, bsp->numplanes);
        AddLump(&bspfile, Q2_LUMP_LEAFS, bsp->dleafs, bsp->numleafs);
		AddLump(&bspfile, Q2_LUMP_NODES, bsp->dnodes, bsp->numnodes);
        AddLump(&bspfile, Q2_LUMP_TEXINFO, bsp->texinfo, bsp->numtexinfo);
        AddLump(&bspfile, Q2_LUMP_FACES, bsp->dfaces, bsp->numfaces);
        AddLump(&bspfile, Q2_LUMP_LEAFFACES, bsp->dleaffaces, bsp->numleaffaces);
        AddLump(&bspfile, Q2_LUMP_LEAFBRUSHES, bsp->dleafbrushes, bsp->numleafbrushes);
        AddLump(&bspfile, Q2_LUMP_SURFEDGES, bsp->dsurfedges, bsp->numsurfedges);
        AddLump(&bspfile, Q2_LUMP_EDGES, bsp->dedges, bsp->numedges);
        AddLump(&bspfile, Q2_LUMP_BRUSHES, bsp->dbrushes, bsp->numbrushes);
        AddLump(&bspfile, Q2_LUMP_BRUSHSIDES, bsp->dbrushsides, bsp->numbrushsides);
        AddLump(&bspfile, Q2_LUMP_AREAS, bsp->dareas, bsp->numareas);
        AddLump(&bspfile, Q2_LUMP_AREAPORTALS, bsp->dareaportals, bsp->numareaportals);
        
        AddLump(&bspfile, Q2_LUMP_VISIBILITY, bsp->dvis, bsp->visdatasize);
        AddLump(&bspfile, Q2_LUMP_LIGHTING, bsp->dlightdata, bsp->lightdatasize);
        AddLump(&bspfile, Q2_LUMP_ENTITIES, bsp->dentdata, bsp->entdatasize);
        AddLump(&bspfile, Q2_LUMP_POP, bsp->dpop, sizeof(bsp->dpop));
    } else if (bspdata->version == &bspver_qbism) {
        const q2bsp_qbism_t *bsp = &bspdata->data.q2bsp_qbism;

        AddLump(&bspfile, Q2_LUMP_MODELS, bsp->dmodels, bsp->nummodels);
        AddLump(&bspfile, Q2_LUMP_VERTEXES, bsp->dvertexes, bsp->numvertexes);
        AddLump(&bspfile, Q2_LUMP_PLANES, bsp->dplanes, bsp->numplanes);
        AddLump(&bspfile, Q2_LUMP_LEAFS, bsp->dleafs, bsp->numleafs);
		AddLump(&bspfile, Q2_LUMP_NODES, bsp->dnodes, bsp->numnodes);
        AddLump(&bspfile, Q2_LUMP_TEXINFO, bsp->texinfo, bsp->numtexinfo);
        AddLump(&bspfile, Q2_LUMP_FACES, bsp->dfaces, bsp->numfaces);
        AddLump(&bspfile, Q2_LUMP_LEAFFACES, bsp->dleaffaces, bsp->numleaffaces);
        AddLump(&bspfile, Q2_LUMP_LEAFBRUSHES, bsp->dleafbrushes, bsp->numleafbrushes);
        AddLump(&bspfile, Q2_LUMP_SURFEDGES, bsp->dsurfedges, bsp->numsurfedges);
        AddLump(&bspfile, Q2_LUMP_EDGES, bsp->dedges, bsp->numedges);
        AddLump(&bspfile, Q2_LUMP_BRUSHES, bsp->dbrushes, bsp->numbrushes);
        AddLump(&bspfile, Q2_LUMP_BRUSHSIDES, bsp->dbrushsides, bsp->numbrushsides);
        AddLump(&bspfile, Q2_LUMP_AREAS, bsp->dareas, bsp->numareas);
        AddLump(&bspfile, Q2_LUMP_AREAPORTALS, bsp->dareaportals, bsp->numareaportals);
        
        AddLump(&bspfile, Q2_LUMP_VISIBILITY, bsp->dvis, bsp->visdatasize);
        AddLump(&bspfile, Q2_LUMP_LIGHTING, bsp->dlightdata, bsp->lightdatasize);
        AddLump(&bspfile, Q2_LUMP_ENTITIES, bsp->dentdata, bsp->entdatasize);
        AddLump(&bspfile, Q2_LUMP_POP, bsp->dpop, sizeof(bsp->dpop));
    } else {
        Error("Unknown format");
    }

    /*BSPX lumps are at a 4-byte alignment after the last of any official lump*/
    if (bspdata->bspxentries)
    {
        bspx_header_t xheader;
        bspxentry_t *x; 
        bspx_lump_t xlumps[64];
        uint32_t l;
        uint32_t bspxheader = ftell(bspfile.file);
        if (bspxheader & 3)
            Error("BSPX header is misaligned");
        xheader.id[0] = 'B';
        xheader.id[1] = 'S';
        xheader.id[2] = 'P';
        xheader.id[3] = 'X';
        xheader.numlumps = 0;
        for (x = bspdata->bspxentries; x; x = x->next)
            xheader.numlumps++;

        if (xheader.numlumps > sizeof(xlumps)/sizeof(xlumps[0]))        /*eep*/
            xheader.numlumps = sizeof(xlumps)/sizeof(xlumps[0]);

        SafeWrite(bspfile.file, &xheader, sizeof(xheader));
        SafeWrite(bspfile.file, xlumps, xheader.numlumps * sizeof(xlumps[0]));

        for (x = bspdata->bspxentries, l = 0; x && l < xheader.numlumps; x = x->next, l++)
        {
            uint8_t pad[4] = {0};
            xlumps[l].filelen = LittleLong(x->lumpsize);
            xlumps[l].fileofs = LittleLong(ftell(bspfile.file));
            strncpy(xlumps[l].lumpname, x->lumpname, sizeof(xlumps[l].lumpname));
            SafeWrite(bspfile.file, x->lumpdata, x->lumpsize);
            if (x->lumpsize % 4)
                SafeWrite(bspfile.file, pad, 4 - (x->lumpsize % 4));
        }

        fseek(bspfile.file, bspxheader, SEEK_SET);
        SafeWrite(bspfile.file, &xheader, sizeof(xheader));
        SafeWrite(bspfile.file, xlumps, xheader.numlumps * sizeof(xlumps[0]));
    }

    fseek(bspfile.file, 0, SEEK_SET);
	
    // write the real header
    if (bspfile.version->version != NO_VERSION) {
        SafeWrite(bspfile.file, &bspfile.q2header, sizeof(bspfile.q2header));
    } else {
        SafeWrite(bspfile.file, &bspfile.q1header, sizeof(bspfile.q1header));
    }
    
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
    const lumpspec_t *lumpspec = LumpspecsForVersion(bspdata->version);

    if (bspdata->version == &bspver_q2) {
        const q2bsp_t *bsp = &bspdata->data.q2bsp;
                
        logprint("%7i %-12s\n", bsp->nummodels, "models");
        
        PrintLumpSize(lumpspec, Q2_LUMP_PLANES, bsp->numplanes);
        PrintLumpSize(lumpspec, Q2_LUMP_VERTEXES, bsp->numvertexes);
        PrintLumpSize(lumpspec, Q2_LUMP_NODES, bsp->numnodes);
        PrintLumpSize(lumpspec, Q2_LUMP_TEXINFO, bsp->numtexinfo);
        PrintLumpSize(lumpspec, Q2_LUMP_FACES, bsp->numfaces);
        PrintLumpSize(lumpspec, Q2_LUMP_LEAFS, bsp->numleafs);
        PrintLumpSize(lumpspec, Q2_LUMP_LEAFFACES, bsp->numleaffaces);
        PrintLumpSize(lumpspec, Q2_LUMP_LEAFBRUSHES, bsp->numleafbrushes);
        PrintLumpSize(lumpspec, Q2_LUMP_EDGES, bsp->numedges);
        PrintLumpSize(lumpspec, Q2_LUMP_SURFEDGES, bsp->numsurfedges);
        PrintLumpSize(lumpspec, Q2_LUMP_BRUSHES, bsp->numbrushes);
        PrintLumpSize(lumpspec, Q2_LUMP_BRUSHSIDES, bsp->numbrushsides);
        PrintLumpSize(lumpspec, Q2_LUMP_AREAS, bsp->numareas);
        PrintLumpSize(lumpspec, Q2_LUMP_AREAPORTALS, bsp->numareaportals);
        
        logprint("%7s %-12s %10i\n", "", "lightdata", bsp->lightdatasize);
        logprint("%7s %-12s %10i\n", "", "visdata", bsp->visdatasize);
        logprint("%7s %-12s %10i\n", "", "entdata", bsp->entdatasize);
    } else if (bspdata->version == &bspver_qbism) {
        const q2bsp_qbism_t *bsp = &bspdata->data.q2bsp_qbism;

        logprint("%7i %-12s\n", bsp->nummodels, "models");
        
        PrintLumpSize(lumpspec, Q2_LUMP_PLANES, bsp->numplanes);
        PrintLumpSize(lumpspec, Q2_LUMP_VERTEXES, bsp->numvertexes);
        PrintLumpSize(lumpspec, Q2_LUMP_NODES, bsp->numnodes);
        PrintLumpSize(lumpspec, Q2_LUMP_TEXINFO, bsp->numtexinfo);
        PrintLumpSize(lumpspec, Q2_LUMP_FACES, bsp->numfaces);
        PrintLumpSize(lumpspec, Q2_LUMP_LEAFS, bsp->numleafs);
        PrintLumpSize(lumpspec, Q2_LUMP_LEAFFACES, bsp->numleaffaces);
        PrintLumpSize(lumpspec, Q2_LUMP_LEAFBRUSHES, bsp->numleafbrushes);
        PrintLumpSize(lumpspec, Q2_LUMP_EDGES, bsp->numedges);
        PrintLumpSize(lumpspec, Q2_LUMP_SURFEDGES, bsp->numsurfedges);
        PrintLumpSize(lumpspec, Q2_LUMP_BRUSHES, bsp->numbrushes);
        PrintLumpSize(lumpspec, Q2_LUMP_BRUSHSIDES, bsp->numbrushsides);
        PrintLumpSize(lumpspec, Q2_LUMP_AREAS, bsp->numareas);
        PrintLumpSize(lumpspec, Q2_LUMP_AREAPORTALS, bsp->numareaportals);
        
        logprint("%7s %-12s %10i\n", "", "lightdata", bsp->lightdatasize);
        logprint("%7s %-12s %10i\n", "", "visdata", bsp->visdatasize);
        logprint("%7s %-12s %10i\n", "", "entdata", bsp->entdatasize);
    } else if (bspdata->version == &bspver_q1 || bspdata->version == &bspver_h2 || bspdata->version == &bspver_hl) {
        const bsp29_t *bsp = &bspdata->data.bsp29;

        if (bsp->texdatasize)
            numtextures = bsp->dtexdata->nummiptex;

        logprint("%7i %-12s\n", bsp->nummodels, "models");

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

        if (bspdata->bspxentries)
        {
            bspxentry_t *x;
            for (x = bspdata->bspxentries; x; x = x->next) {
                logprint("%7s %-12s %10i\n", "BSPX", x->lumpname, (int)x->lumpsize);
            }
        }
    } else if (bspdata->version == &bspver_bsp2rmq || bspdata->version == &bspver_h2bsp2rmq) {
        const bsp2rmq_t *bsp = &bspdata->data.bsp2rmq;

        if (bsp->texdatasize)
            numtextures = bsp->dtexdata->nummiptex;

        logprint("%7i %-12s\n", bsp->nummodels, "models");

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
    } else if (bspdata->version == &bspver_bsp2 || bspdata->version == &bspver_h2bsp2) {
        const bsp2_t *bsp = &bspdata->data.bsp2;

        if (bsp->texdatasize)
            numtextures = bsp->dtexdata->nummiptex;

        logprint("%7i %-12s\n", bsp->nummodels, "models");

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
    } else {
        Error("Unsupported BSP version: %s", BSPVersionString(bspdata->version));
    }
}

/*
  ===============
  CompressRow
  ===============
*/
int
CompressRow(const uint8_t *vis, const int numbytes, uint8_t *out)
{
    int i, rep;
    uint8_t *dst;

    dst = out;
    for (i = 0; i < numbytes; i++) {
        *dst++ = vis[i];
        if (vis[i])
            continue;

        rep = 1;
        for (i++; i < numbytes; i++)
            if (vis[i] || rep == 255)
                break;
            else
                rep++;
        *dst++ = rep;
        i--;
    }

    return dst - out;
}

/*
===================
DecompressRow
===================
*/
void
DecompressRow (const uint8_t *in, const int numbytes, uint8_t *decompressed)
{
	int		c;
	uint8_t	*out;
	int		row;

	row = numbytes;
	out = decompressed;

	do
	{
		if (*in)
		{
			*out++ = *in++;
			continue;
		}

		c = in[1];
		if (!c)
			Error ("DecompressVis: 0 repeat");
		in += 2;
		while (c)
		{
			*out++ = 0;
			c--;
		}
	} while (out - decompressed < row);
}
