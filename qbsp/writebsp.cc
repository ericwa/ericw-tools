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

#include <qbsp/qbsp.hh>
#include <qbsp/wad.hh>

#include <vector>
#include <algorithm>
#include <cstdint>

static void
AssertVanillaContentType(int content)
{
    switch (content) {
    case CONTENTS_EMPTY:
    case CONTENTS_SOLID:
    case CONTENTS_WATER:
    case CONTENTS_SLIME:
    case CONTENTS_LAVA:
    case CONTENTS_SKY:
        break;
    default:
        Error("Internal error: Tried to save compiler-internal contents type %s\n", GetContentsName(content));
    }
}

static int
RemapContentsForExport(int content)
{
    if (content == CONTENTS_DETAIL_FENCE) {
        /*
         * This is for func_detail_wall.. we want to write a solid leaf that has faces,
         * because it may be possible to see inside (fence textures).
         *
         * Normally solid leafs are not written and just referenced as leaf 0.
         */
        return CONTENTS_SOLID;
    }
    if (content == CONTENTS_ILLUSIONARY_VISBLOCKER) {
        return CONTENTS_EMPTY;
    }
    return content;
}

/**
 * Returns the output plane number
 */
int
ExportMapPlane(int planenum)
{
    qbsp_plane_t *plane = &map.planes.at(planenum);

    if (plane->outputplanenum != -1)
        return plane->outputplanenum; // already output.
    
    struct lumpdata *planes = &pWorldEnt()->lumps[LUMP_PLANES];
    
    if (planes->index >= planes->count)
        Error("Internal error: plane count mismatch (%s)", __func__);
    
    const int newIndex = planes->index;
    
    dplane_t *dplane = &((dplane_t *)planes->data)[newIndex];
    dplane->normal[0] = plane->normal[0];
    dplane->normal[1] = plane->normal[1];
    dplane->normal[2] = plane->normal[2];
    dplane->dist = plane->dist;
    dplane->type = plane->type;
    
    planes->index++;
    map.cTotal[LUMP_PLANES]++;

    Q_assert(planes->index == map.cTotal[LUMP_PLANES]);
    
    plane->outputplanenum = newIndex;
    return newIndex;
}

int
ExportMapTexinfo(int texinfonum)
{
    mtexinfo_t *src = &map.mtexinfos.at(texinfonum);
    if (src->outputnum != -1)
        return src->outputnum;
    
    struct lumpdata *texinfo = &pWorldEnt()->lumps[LUMP_TEXINFO];
    
    if (texinfo->index >= texinfo->count)
        Error("Internal error: texinfo count mismatch (%s)", __func__);
    
    const int i = texinfo->index;
    
    texinfo_t *dest = &(static_cast<texinfo_t *>(texinfo->data)[i]);
    dest->flags = static_cast<int32_t>(src->flags & TEX_SPECIAL);
    dest->miptex = src->miptex;
    for (int j=0; j<2; j++) {
        for (int k=0; k<4; k++) {
            dest->vecs[j][k] = src->vecs[j][k];
        }
    }
    
    texinfo->index++;
    map.cTotal[LUMP_TEXINFO]++;
    
    Q_assert(texinfo->index == map.cTotal[LUMP_TEXINFO]);
    
    src->outputnum = i;
    return i;
}

/*
==================
AllocBSPPlanes
==================
*/
void
AllocBSPPlanes()
{
    struct lumpdata *planes = &pWorldEnt()->lumps[LUMP_PLANES];

    // OK just need one plane array, stick it in worldmodel
    if (map.numplanes() > planes->count) {
        int newcount = map.numplanes();
        struct lumpdata *newplanes = (struct lumpdata *)AllocMem(BSP_PLANE, newcount, true);
        
        memcpy(newplanes, planes->data, MemSize[BSP_PLANE] * planes->count);
        FreeMem(planes->data, BSP_PLANE, planes->count);
        
        planes->count = newcount;
        planes->data = newplanes;
    }
}

/*
==================
AllocBSPTexinfo
==================
*/
void
AllocBSPTexinfo()
{
    struct lumpdata *texinfo = &pWorldEnt()->lumps[LUMP_TEXINFO];
    
    // OK just need one plane array, stick it in worldmodel
    if (map.numtexinfo() > texinfo->count) {
        int newcount = map.numtexinfo();
        struct lumpdata *newtexinfo = (struct lumpdata *)AllocMem(BSP_TEXINFO, newcount, true);
        
        memcpy(newtexinfo, texinfo->data, MemSize[BSP_TEXINFO] * texinfo->count);
        FreeMem(texinfo->data, BSP_TEXINFO, texinfo->count);
        
        texinfo->count = newcount;
        texinfo->data = newtexinfo;
    }
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

    clipnode->planenum = ExportMapPlane(node->planenum);
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

    clipnode->planenum = ExportMapPlane(node->planenum);
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
    const int entnum = entity - &map.entities.at(0);
    for (i = 0; i < entnum; i++)
        clipcount += map.entities.at(i).lumps[LUMP_CLIPNODES].count;
    model->headnode[hullnum] = clipcount + oldcount;

    CountClipNodes_r(entity, nodes);
    if (clipnodes->count > MAX_BSP_CLIPNODES && (options.BSPVersion == BSPVERSION || options.BSPVersion == BSPHLVERSION))
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
            for (i = 1; i < hullnum; i++)
                model->headnode[i] += diff;
            if (options.BSPVersion == BSPVERSION || options.BSPVersion == BSPHLVERSION) {
                bsp29_dclipnode_t *clipnode = (bsp29_dclipnode_t *)clipnodes->data;
                for (i = 0; i < oldcount; i++, clipnode++) {
                    if (clipnode->children[0] < MAX_BSP_CLIPNODES)
                        clipnode->children[0] += diff;
                    if (clipnode->children[1] < MAX_BSP_CLIPNODES)
                        clipnode->children[1] += diff;
                }
            } else {
                bsp2_dclipnode_t *clipnode = (bsp2_dclipnode_t *)clipnodes->data;
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
    if (options.BSPVersion == BSPVERSION || options.BSPVersion == BSPHLVERSION)
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
    
    entity->lumps[LUMP_LEAFS].count++;
    for (markfaces = node->markfaces; *markfaces; markfaces++) {
        if (map.mtexinfos.at((*markfaces)->texinfo).flags & TEX_SKIP)
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
    struct lumpdata *leaves = &entity->lumps[LUMP_LEAFS];
    struct lumpdata *marksurfs = &entity->lumps[LUMP_MARKSURFACES];
    uint16_t *marksurfnums = (uint16_t *)marksurfs->data;
    face_t **markfaces, *face;
    bsp29_dleaf_t *dleaf;

    // ptr arithmetic to get correct leaf in memory
    dleaf = (bsp29_dleaf_t *)leaves->data + leaves->index;
    leaves->index++;
    map.cTotal[LUMP_LEAFS]++;

    dleaf->contents = RemapContentsForExport(node->contents);
    AssertVanillaContentType(dleaf->contents);

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

    dleaf->visofs = -1; // no vis info yet

    // write the marksurfaces
    dleaf->firstmarksurface = map.cTotal[LUMP_MARKSURFACES];

    for (markfaces = node->markfaces; *markfaces; markfaces++) {
        face = *markfaces;
        if (map.mtexinfos.at(face->texinfo).flags & TEX_SKIP)
            continue;

        /* emit a marksurface */
        do {
            marksurfnums[marksurfs->index] = face->outputnumber;
            marksurfs->index++;
            map.cTotal[LUMP_MARKSURFACES]++;
            face = face->original;      /* grab tjunction split faces */
        } while (face);
    }
    dleaf->nummarksurfaces =
        map.cTotal[LUMP_MARKSURFACES] - dleaf->firstmarksurface;
}

static void
ExportLeaf_BSP2(mapentity_t *entity, node_t *node)
{
    struct lumpdata *leaves = &entity->lumps[LUMP_LEAFS];
    struct lumpdata *marksurfs = &entity->lumps[LUMP_MARKSURFACES];
    uint32_t *marksurfnums = (uint32_t *)marksurfs->data;
    face_t **markfaces, *face;
    bsp2_dleaf_t *dleaf;

    // ptr arithmetic to get correct leaf in memory
    dleaf = (bsp2_dleaf_t *)leaves->data + leaves->index;
    leaves->index++;
    map.cTotal[LUMP_LEAFS]++;

    dleaf->contents = RemapContentsForExport(node->contents);
    AssertVanillaContentType(dleaf->contents);
    
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

    dleaf->visofs = -1; // no vis info yet

    // write the marksurfaces
    dleaf->firstmarksurface = map.cTotal[LUMP_MARKSURFACES];

    for (markfaces = node->markfaces; *markfaces; markfaces++) {
        face = *markfaces;
        if (map.mtexinfos.at(face->texinfo).flags & TEX_SKIP)
            continue;

        /* emit a marksurface */
        do {
            marksurfnums[marksurfs->index] = face->outputnumber;
            marksurfs->index++;
            map.cTotal[LUMP_MARKSURFACES]++;
            face = face->original;      /* grab tjunction split faces */
        } while (face);
    }
    dleaf->nummarksurfaces =
        map.cTotal[LUMP_MARKSURFACES] - dleaf->firstmarksurface;
}

static void
ExportLeaf_BSP2rmq(mapentity_t *entity, node_t *node)
{
    struct lumpdata *leaves = &entity->lumps[LUMP_LEAFS];
    struct lumpdata *marksurfs = &entity->lumps[LUMP_MARKSURFACES];
    uint32_t *marksurfnums = (uint32_t *)marksurfs->data;
    face_t **markfaces, *face;
    bsp2rmq_dleaf_t *dleaf;

    // ptr arithmetic to get correct leaf in memory
    dleaf = (bsp2rmq_dleaf_t *)leaves->data + leaves->index;
    leaves->index++;
    map.cTotal[LUMP_LEAFS]++;

    dleaf->contents = RemapContentsForExport(node->contents);
    AssertVanillaContentType(dleaf->contents);

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

    dleaf->visofs = -1; // no vis info yet

    // write the marksurfaces
    dleaf->firstmarksurface = map.cTotal[LUMP_MARKSURFACES];

    for (markfaces = node->markfaces; *markfaces; markfaces++) {
        face = *markfaces;
        if (map.mtexinfos.at(face->texinfo).flags & TEX_SKIP)
            continue;

        /* emit a marksurface */
        do {
            marksurfnums[marksurfs->index] = face->outputnumber;
            marksurfs->index++;
            map.cTotal[LUMP_MARKSURFACES]++;
            face = face->original;      /* grab tjunction split faces */
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

    dnode->planenum = ExportMapPlane(node->planenum);
    dnode->firstface = node->firstface;
    dnode->numfaces = node->numfaces;

    // recursively output the other nodes
    for (i = 0; i < 2; i++) {
        if (node->children[i]->planenum == -1) {
            if (node->children[i]->contents == CONTENTS_SOLID)
                dnode->children[i] = -1;
            else {
                int childnum = -(map.cTotal[LUMP_LEAFS] + 1);
                if (childnum < INT16_MIN) {
                    Error("Map exceeds BSP29 node/leaf limit. Recompile with -bsp2 flag.");
                }
                dnode->children[i] = childnum;
                ExportLeaf_BSP29(entity, node->children[i]);
            }
        } else {
            int childnum = map.cTotal[LUMP_NODES];
            if (childnum > INT16_MAX) {
                Error("Map exceeds BSP29 node/leaf limit. Recompile with -bsp2 flag.");
            }
            dnode->children[i] = childnum;
            ExportDrawNodes_BSP29(entity, node->children[i]);
        }
    }

    // DarkPlaces asserts that the leaf numbers are different
    // if mod_bsp_portalize is 1 (default)
    // The most likely way it could fail is if both sides are the
    // shared CONTENTS_SOLID leaf (-1)
    Q_assert(!(dnode->children[0] == -1 && dnode->children[1] == -1));
	Q_assert(dnode->children[0] != dnode->children[1]);
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

    dnode->planenum = ExportMapPlane(node->planenum);
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
    
    Q_assert(!(dnode->children[0] == -1 && dnode->children[1] == -1));
    Q_assert(dnode->children[0] != dnode->children[1]);
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

    dnode->planenum = ExportMapPlane(node->planenum);
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
    
    Q_assert(!(dnode->children[0] == -1 && dnode->children[1] == -1));
    Q_assert(dnode->children[0] != dnode->children[1]);
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

    // Allocate a model
    entity->lumps[LUMP_MODELS].data = AllocMem(BSP_MODEL, 1, true);
    entity->lumps[LUMP_MODELS].count = 1;
    
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
        bsp2_dleaf_t *leaf = (bsp2_dleaf_t *)pWorldEnt()->lumps[LUMP_LEAFS].data;
        leaf->contents = CONTENTS_SOLID;
    } else if (options.BSPVersion == BSP2RMQVERSION) {
        bsp2rmq_dleaf_t *leaf = (bsp2rmq_dleaf_t *)pWorldEnt()->lumps[LUMP_LEAFS].data;
        leaf->contents = CONTENTS_SOLID;
    } else {
        bsp29_dleaf_t *leaf = (bsp29_dleaf_t *)pWorldEnt()->lumps[LUMP_LEAFS].data;
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
    if (entity == pWorldEnt())
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
    pWorldEnt()->lumps[LUMP_EDGES].count++;
    pWorldEnt()->lumps[LUMP_EDGES].index++;
    map.cTotal[LUMP_EDGES]++;

    // Leave room for leaf 0 (must be solid)
    pWorldEnt()->lumps[LUMP_LEAFS].count++;
    pWorldEnt()->lumps[LUMP_LEAFS].index++;
    map.cTotal[LUMP_LEAFS]++;
}

/*
 * Writes extended texinfo flags to a file so they can be read by the light tool.
 * Used for phong shading and other lighting settings on func_detail.
 */
static void
WriteExtendedTexinfoFlags(void)
{
    bool needwrite = false;
    const int num_texinfo = map.numtexinfo();
    
    for (int i = 0; i < num_texinfo; i++) {
        if (map.mtexinfos.at(i).flags & ~(TEX_SPECIAL | TEX_SKIP | TEX_HINT)) {
            // this texinfo uses some extended flags, write them to a file
            needwrite = true;
            break;
        }
    }

    if (!needwrite)
        return;
    
    // sort by output texinfo number
    std::vector<mtexinfo_t> texinfos_sorted(map.mtexinfos);
    std::sort(texinfos_sorted.begin(), texinfos_sorted.end(), [](const mtexinfo_t &a, const mtexinfo_t &b) {
        return a.outputnum < b.outputnum;
    });
    
    FILE *texinfofile;
    StripExtension(options.szBSPName);
    strcat(options.szBSPName, ".texinfo");
    texinfofile = fopen(options.szBSPName, "wt");
    if (!texinfofile)
        Error("Failed to open %s: %s", options.szBSPName, strerror(errno));
    
    int count = 0;
    for (const auto &tx : texinfos_sorted) {
        if (tx.outputnum == -1)
            continue;
        
        Q_assert(count == tx.outputnum); // check we are outputting them in the proper sequence
        
        fprintf(texinfofile, "%llu\n", static_cast<unsigned long long>(tx.flags));
        count++;
    }
    Q_assert(count == map.cTotal[LUMP_TEXINFO]);
    
    fclose(texinfofile);
}

/*
==================
FinishBSPFile
==================
*/
void
FinishBSPFile(void)
{
    options.fVerbose = true;
    Message(msgProgress, "WriteBSPFile");

    // TODO: Fix this somewhere else?
    struct lumpdata *planes = &pWorldEnt()->lumps[LUMP_PLANES];    
    planes->count = map.cTotal[LUMP_PLANES];

    struct lumpdata *texinfo = &pWorldEnt()->lumps[LUMP_TEXINFO];
    texinfo->count = map.cTotal[LUMP_TEXINFO];
    
    WriteExtendedTexinfoFlags();
    WriteBSPFile();
    PrintBSPFileSizes();

    options.fVerbose = options.fAllverbose;
}
