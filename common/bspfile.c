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

/* ========================================================================= */

int nummodels;
dmodel_t *dmodels;

int visdatasize;
byte *dvisdata;

int lightdatasize;
byte *dlightdata;

int texdatasize;
byte *dtexdata; /* (dmiptexlump_t) */

int entdatasize;
char *dentdata;

int numleafs;
dleaf_t *dleafs;

int numplanes;
dplane_t *dplanes;

int numvertexes;
dvertex_t *dvertexes;

int numnodes;
dnode_t *dnodes;

int numtexinfo;
texinfo_t *texinfo;

int numfaces;
dface_t *dfaces;

int numclipnodes;
dclipnode_t *dclipnodes;

int numedges;
dedge_t *dedges;

int nummarksurfaces;
unsigned short *dmarksurfaces;

int numsurfedges;
int *dsurfedges;

/* Transitional helper functions */
void
GetBSPGlobals(bspdata_t *bspdata)
{
    bspdata->nummodels = nummodels;
    bspdata->dmodels = dmodels;

    bspdata->visdatasize = visdatasize;
    bspdata->dvisdata = dvisdata;

    bspdata->lightdatasize = lightdatasize;
    bspdata->dlightdata = dlightdata;

    bspdata->texdatasize = texdatasize;
    bspdata->dtexdata = dtexdata;

    bspdata->entdatasize = entdatasize;
    bspdata->dentdata = dentdata;

    bspdata->numleafs = numleafs;
    bspdata->dleafs = dleafs;

    bspdata->numplanes = numplanes;
    bspdata->dplanes = dplanes;

    bspdata->numvertexes = numvertexes;
    bspdata->dvertexes = dvertexes;

    bspdata->numnodes = numnodes;
    bspdata->dnodes = dnodes;

    bspdata->numtexinfo = numtexinfo;
    bspdata->texinfo = texinfo;

    bspdata->numfaces = numfaces;
    bspdata->dfaces = dfaces;

    bspdata->numclipnodes = numclipnodes;
    bspdata->dclipnodes = dclipnodes;

    bspdata->numedges = numedges;
    bspdata->dedges = dedges;

    bspdata->nummarksurfaces = nummarksurfaces;
    bspdata->dmarksurfaces = dmarksurfaces;

    bspdata->numsurfedges = numsurfedges;
    bspdata->dsurfedges = dsurfedges;
}

void
SetBSPGlobals(const bspdata_t *bspdata)
{
    nummodels = bspdata->nummodels;
    dmodels = bspdata->dmodels;

    visdatasize = bspdata->visdatasize;
    dvisdata = bspdata->dvisdata;

    lightdatasize = bspdata->lightdatasize;
    dlightdata = bspdata->dlightdata;

    texdatasize = bspdata->texdatasize;
    dtexdata = bspdata->dtexdata;

    entdatasize = bspdata->entdatasize;
    dentdata = bspdata->dentdata;

    numleafs = bspdata->numleafs;
    dleafs = bspdata->dleafs;

    numplanes = bspdata->numplanes;
    dplanes = bspdata->dplanes;

    numvertexes = bspdata->numvertexes;
    dvertexes = bspdata->dvertexes;

    numnodes = bspdata->numnodes;
    dnodes = bspdata->dnodes;

    numtexinfo = bspdata->numtexinfo;
    texinfo = bspdata->texinfo;

    numfaces = bspdata->numfaces;
    dfaces = bspdata->dfaces;

    numclipnodes = bspdata->numclipnodes;
    dclipnodes = bspdata->dclipnodes;

    numedges = bspdata->numedges;
    dedges = bspdata->dedges;

    nummarksurfaces = bspdata->nummarksurfaces;
    dmarksurfaces = bspdata->dmarksurfaces;

    numsurfedges = bspdata->numsurfedges;
    dsurfedges = bspdata->dsurfedges;
}

/* ========================================================================= */

typedef enum { TO_DISK, TO_CPU } swaptype_t;

/*
 * =============
 * SwapBSPFile
 * Byte swaps all data in a bsp file.
 * =============
 */
void
SwapBSPFile(bspdata_t *bspdata, swaptype_t swap)
{
    int i, j;

    /* vertexes */
    for (i = 0; i < bspdata->numvertexes; i++) {
	dvertex_t *vertex = &bspdata->dvertexes[i];
	for (j = 0; j < 3; j++)
	    vertex->point[j] = LittleFloat(vertex->point[j]);
    }

    /* planes */
    for (i = 0; i < bspdata->numplanes; i++) {
	dplane_t *plane = &dplanes[i];
	for (j = 0; j < 3; j++)
	    plane->normal[j] = LittleFloat(plane->normal[j]);
	plane->dist = LittleFloat(plane->dist);
	plane->type = LittleLong(plane->type);
    }

    /* texinfos */
    for (i = 0; i < bspdata->numtexinfo; i++) {
	texinfo_t *texinfo = &bspdata->texinfo[i];
	for (j = 0; j < 4; j++) {
	    texinfo->vecs[0][j] = LittleFloat(texinfo->vecs[0][j]);
	    texinfo->vecs[1][j] = LittleFloat(texinfo->vecs[1][j]);
	}
	texinfo->miptex = LittleLong(texinfo->miptex);
	texinfo->flags = LittleLong(texinfo->flags);
    }

    /* faces */
    for (i = 0; i < bspdata->numfaces; i++) {
	dface_t *face = &bspdata->dfaces[i];
	face->texinfo = LittleShort(face->texinfo);
	face->planenum = LittleShort(face->planenum);
	face->side = LittleShort(face->side);
	face->lightofs = LittleLong(face->lightofs);
	face->firstedge = LittleLong(face->firstedge);
	face->numedges = LittleShort(face->numedges);
    }

    /* nodes */
    for (i = 0; i < bspdata->numnodes; i++) {
	dnode_t *node = &bspdata->dnodes[i];
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

    /* leafs */
    for (i = 0; i < bspdata->numleafs; i++) {
	dleaf_t *leaf = &bspdata->dleafs[i];
	leaf->contents = LittleLong(leaf->contents);
	for (j = 0; j < 3; j++) {
	    leaf->mins[j] = LittleShort(leaf->mins[j]);
	    leaf->maxs[j] = LittleShort(leaf->maxs[j]);
	}
	leaf->firstmarksurface = LittleShort(leaf->firstmarksurface);
	leaf->nummarksurfaces = LittleShort(leaf->nummarksurfaces);
	leaf->visofs = LittleLong(leaf->visofs);
    }

    /* clipnodes */
    for (i = 0; i < bspdata->numclipnodes; i++) {
	dclipnode_t *clipnode = &bspdata->dclipnodes[i];
	clipnode->planenum = LittleLong(clipnode->planenum);
	clipnode->children[0] = LittleShort(clipnode->children[0]);
	clipnode->children[1] = LittleShort(clipnode->children[1]);
    }

    /* miptex */
    if (bspdata->texdatasize) {
	dmiptexlump_t *lump = (dmiptexlump_t *)bspdata->dtexdata;
	int count = lump->nummiptex;
	if (swap == TO_CPU)
	    count = LittleLong(count);

	lump->nummiptex = LittleLong(lump->nummiptex);
	for (i = 0; i < count; i++)
	    lump->dataofs[i] = LittleLong(lump->dataofs[i]);
    }

    /* marksurfaces */
    for (i = 0; i < bspdata->nummarksurfaces; i++) {
	unsigned short *marksurface = &bspdata->dmarksurfaces[i];
	*marksurface = LittleShort(*marksurface);
    }

    /* surfedges */
    for (i = 0; i < bspdata->numsurfedges; i++) {
	int *surfedge = &bspdata->dsurfedges[i];
	*surfedge = LittleLong(*surfedge);
    }

    /* edges */
    for (i = 0; i < bspdata->numedges; i++) {
	dedge_t *edge = &bspdata->dedges[i];
	edge->v[0] = LittleShort(edge->v[0]);
	edge->v[1] = LittleShort(edge->v[1]);
    }

    /* models */
    for (i = 0; i < bspdata->nummodels; i++) {
	dmodel_t *dmodel = &bspdata->dmodels[i];
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

const lumpspec_t lumpspec[] = {
    { "entity",      sizeof(char)           },
    { "plane",       sizeof(dplane_t)       },
    { "texture",     sizeof(byte)           },
    { "vertex",      sizeof(dvertex_t)      },
    { "visibility",  sizeof(byte)           },
    { "node",        sizeof(dnode_t)        },
    { "texinfo",     sizeof(texinfo_t)      },
    { "face",        sizeof(dface_t)        },
    { "lighting",    sizeof(byte)           },
    { "clipnode",    sizeof(dclipnode_t)    },
    { "leaf",        sizeof(dleaf_t)        },
    { "marksurface", sizeof(unsigned short) },
    { "edge",        sizeof(dedge_t)        },
    { "surfedge",    sizeof(int)            },
    { "model",       sizeof(dmodel_t)       },
};

static int
CopyLump(const dheader_t *header, int lumpnum, void *destptr)
{
    const size_t size = lumpspec[lumpnum].size;
    byte **bufferptr = destptr;
    byte *buffer = *bufferptr;
    int length;
    int ofs;

    length = header->lumps[lumpnum].filelen;
    ofs = header->lumps[lumpnum].fileofs;

    if (length % size)
	Error("%s: odd %s lump size", __func__, lumpspec[lumpnum].name);

    if (buffer)
	free(buffer);

    buffer = *bufferptr = malloc(length + 1);
    if (!buffer)
	Error("%s: allocation of %i bytes failed.", __func__, length);

    memcpy(buffer, (const byte *)header + ofs, length);
    buffer[length] = 0; /* In case of corrupt entity lump */

    return length / size;
}

/*
 * =============
 * LoadBSPFile
 * =============
 */
int
LoadBSPFile(const char *filename)
{
    bspdata_t bspdata;
    dheader_t *header;
    int i, bsp_version;

    /* load the file header */
    LoadFile(filename, &header);

    /* check the file version */
    header->version = LittleLong(header->version);
    bsp_version = (int)header->version;
    logprint("BSP is version %i\n", bsp_version);
    if (bsp_version != 29)
	Error("Sorry, only bsp version 29 supported.");

    /* swap the lump headers */
    for (i = 0; i < HEADER_LUMPS; i++) {
	header->lumps[i].fileofs = LittleLong(header->lumps[i].fileofs);
	header->lumps[i].filelen = LittleLong(header->lumps[i].filelen);
    }

    /* copy the data */
    nummodels = CopyLump(header, LUMP_MODELS, &dmodels);
    numvertexes = CopyLump(header, LUMP_VERTEXES, &dvertexes);
    numplanes = CopyLump(header, LUMP_PLANES, &dplanes);
    numleafs = CopyLump(header, LUMP_LEAFS, &dleafs);
    numnodes = CopyLump(header, LUMP_NODES, &dnodes);
    numtexinfo = CopyLump(header, LUMP_TEXINFO, &texinfo);
    numclipnodes = CopyLump(header, LUMP_CLIPNODES, &dclipnodes);
    numfaces = CopyLump(header, LUMP_FACES, &dfaces);
    nummarksurfaces = CopyLump(header, LUMP_MARKSURFACES, &dmarksurfaces);
    numsurfedges = CopyLump(header, LUMP_SURFEDGES, &dsurfedges);
    numedges = CopyLump(header, LUMP_EDGES, &dedges);

    texdatasize = CopyLump(header, LUMP_TEXTURES, &dtexdata);
    visdatasize = CopyLump(header, LUMP_VISIBILITY, &dvisdata);
    lightdatasize = CopyLump(header, LUMP_LIGHTING, &dlightdata);
    entdatasize = CopyLump(header, LUMP_ENTITIES, &dentdata);

    /* everything has been copied out */
    free(header);

    /* swap everything */
    GetBSPGlobals(&bspdata);
    SwapBSPFile(&bspdata, TO_CPU);

    /* Return the version */
    return bsp_version;
}

/* ========================================================================= */

static void
AddLump(FILE *wadfile, dheader_t *header, int lumpnum,
	const void *data, int count)
{
    const size_t size = lumpspec[lumpnum].size * count;
    lump_t *lump = &header->lumps[lumpnum];
    byte pad[4] = {0};

    lump->fileofs = LittleLong(ftell(wadfile));
    lump->filelen = LittleLong(size);
    SafeWrite(wadfile, data, (size + 3) & ~3);
    if (size % 4)
	SafeWrite(wadfile, pad, size % 4);
}

/*
 * =============
 * WriteBSPFile
 * Swaps the bsp file in place, so it should not be referenced again
 * =============
 */
void
WriteBSPFile(const char *filename, int version)
{
    bspdata_t bspdata;
    dheader_t header;
    FILE *wadfile;

    memset(&header, 0, sizeof(header));

    GetBSPGlobals(&bspdata);
    SwapBSPFile(&bspdata, TO_DISK);

    header.version = LittleLong(version);
    logprint("Writing BSP version %i\n", header.version);
    wadfile = SafeOpenWrite(filename);

    /* Save header space, updated after adding the lumps */
    SafeWrite(wadfile, &header, sizeof(header));

    AddLump(wadfile, &header, LUMP_PLANES, dplanes, numplanes);
    AddLump(wadfile, &header, LUMP_LEAFS, dleafs, numleafs);
    AddLump(wadfile, &header, LUMP_VERTEXES, dvertexes, numvertexes);
    AddLump(wadfile, &header, LUMP_NODES, dnodes, numnodes);
    AddLump(wadfile, &header, LUMP_TEXINFO, texinfo, numtexinfo);
    AddLump(wadfile, &header, LUMP_FACES, dfaces, numfaces);
    AddLump(wadfile, &header, LUMP_CLIPNODES, dclipnodes, numclipnodes);
    AddLump(wadfile, &header, LUMP_MARKSURFACES, dmarksurfaces, nummarksurfaces);
    AddLump(wadfile, &header, LUMP_SURFEDGES, dsurfedges, numsurfedges);
    AddLump(wadfile, &header, LUMP_EDGES, dedges, numedges);
    AddLump(wadfile, &header, LUMP_MODELS, dmodels, nummodels);

    AddLump(wadfile, &header, LUMP_LIGHTING, dlightdata, lightdatasize);
    AddLump(wadfile, &header, LUMP_VISIBILITY, dvisdata, visdatasize);
    AddLump(wadfile, &header, LUMP_ENTITIES, dentdata, entdatasize);
    AddLump(wadfile, &header, LUMP_TEXTURES, dtexdata, texdatasize);

    fseek(wadfile, 0, SEEK_SET);
    SafeWrite(wadfile, &header, sizeof(header));

    fclose(wadfile);
}

/* ========================================================================= */

/*
 * =============
 * PrintBSPFileSizes
 * Dumps info about current file
 * =============
 */
void
PrintBSPFileSizes(void)
{
    logprint("%6i planes       %8i\n",
	     numplanes, (int)(numplanes * sizeof(dplane_t)));
    logprint("%6i vertexes     %8i\n",
	     numvertexes, (int)(numvertexes * sizeof(dvertex_t)));
    logprint("%6i nodes        %8i\n",
	     numnodes, (int)(numnodes * sizeof(dnode_t)));
    logprint("%6i texinfo      %8i\n",
	     numtexinfo, (int)(numtexinfo * sizeof(texinfo_t)));
    logprint("%6i faces        %8i\n",
	     numfaces, (int)(numfaces * sizeof(dface_t)));
    logprint("%6i clipnodes    %8i\n",
	     numclipnodes, (int)(numclipnodes * sizeof(dclipnode_t)));
    logprint("%6i leafs        %8i\n",
	     numleafs, (int)(numleafs * sizeof(dleaf_t)));
    logprint("%6i marksurfaces %8i\n",
	     nummarksurfaces,
	     (int)(nummarksurfaces * sizeof(dmarksurfaces[0])));
    logprint("%6i surfedges    %8i\n", numsurfedges,
	     (int)(numsurfedges * sizeof(dmarksurfaces[0])));
    logprint("%6i edges        %8i\n", numedges,
	     (int)(numedges * sizeof(dedge_t)));
    if (!texdatasize) {
	logprint("     0 textures            0\n");
    } else {
	logprint("%6i textures     %8i\n",
		 ((dmiptexlump_t *)dtexdata)->nummiptex, texdatasize);
    }
    logprint("       lightdata    %8i\n", lightdatasize);
    logprint("       visdata      %8i\n", visdatasize);
    logprint("       entdata      %8i\n", entdatasize);
}
