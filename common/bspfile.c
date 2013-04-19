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

/* ========================================================================= */

/*
 * =============
 * SwapBSPFile
 * Byte swaps all data in a bsp file.
 * =============
 */
static void
SwapBSPFile(qboolean todisk)
{
    int i, j;

    /* vertexes */
    for (i = 0; i < numvertexes; i++) {
	for (j = 0; j < 3; j++)
	    dvertexes[i].point[j] = LittleFloat(dvertexes[i].point[j]);
    }

    /* planes */
    for (i = 0; i < numplanes; i++) {
	for (j = 0; j < 3; j++)
	    dplanes[i].normal[j] = LittleFloat(dplanes[i].normal[j]);
	dplanes[i].dist = LittleFloat(dplanes[i].dist);
	dplanes[i].type = LittleLong(dplanes[i].type);
    }

    /* texinfos */
    for (i = 0; i < numtexinfo; i++) {
	for (j = 0; j < 4; j++) {
	    texinfo[i].vecs[0][j] = LittleFloat(texinfo[i].vecs[0][j]);
	    texinfo[i].vecs[1][j] = LittleFloat(texinfo[i].vecs[1][j]);
	}
	texinfo[i].miptex = LittleLong(texinfo[i].miptex);
	texinfo[i].flags = LittleLong(texinfo[i].flags);
    }

    /* faces */
    for (i = 0; i < numfaces; i++) {
	dfaces[i].texinfo = LittleShort(dfaces[i].texinfo);
	dfaces[i].planenum = LittleShort(dfaces[i].planenum);
	dfaces[i].side = LittleShort(dfaces[i].side);
	dfaces[i].lightofs = LittleLong(dfaces[i].lightofs);
	dfaces[i].firstedge = LittleLong(dfaces[i].firstedge);
	dfaces[i].numedges = LittleShort(dfaces[i].numedges);
    }

    /* nodes */
    for (i = 0; i < numnodes; i++) {
	dnodes[i].planenum = LittleLong(dnodes[i].planenum);
	for (j = 0; j < 3; j++) {
	    dnodes[i].mins[j] = LittleShort(dnodes[i].mins[j]);
	    dnodes[i].maxs[j] = LittleShort(dnodes[i].maxs[j]);
	}
	dnodes[i].children[0] = LittleShort(dnodes[i].children[0]);
	dnodes[i].children[1] = LittleShort(dnodes[i].children[1]);
	dnodes[i].firstface = LittleShort(dnodes[i].firstface);
	dnodes[i].numfaces = LittleShort(dnodes[i].numfaces);
    }

    /* leafs */
    for (i = 0; i < numleafs; i++) {
	dleafs[i].contents = LittleLong(dleafs[i].contents);
	for (j = 0; j < 3; j++) {
	    dleafs[i].mins[j] = LittleShort(dleafs[i].mins[j]);
	    dleafs[i].maxs[j] = LittleShort(dleafs[i].maxs[j]);
	}
	dleafs[i].firstmarksurface = LittleShort(dleafs[i].firstmarksurface);
	dleafs[i].nummarksurfaces = LittleShort(dleafs[i].nummarksurfaces);
	dleafs[i].visofs = LittleLong(dleafs[i].visofs);
    }

    /* clipnodes */
    for (i = 0; i < numclipnodes; i++) {
	dclipnodes[i].planenum = LittleLong(dclipnodes[i].planenum);
	dclipnodes[i].children[0] = LittleShort(dclipnodes[i].children[0]);
	dclipnodes[i].children[1] = LittleShort(dclipnodes[i].children[1]);
    }

    /* miptex */
    if (texdatasize) {
	dmiptexlump_t *lump = (dmiptexlump_t *)dtexdata;
	int count = todisk ? lump->nummiptex : LittleLong(lump->nummiptex);

	lump->nummiptex = LittleLong(lump->nummiptex);
	for (i = 0; i < count; i++)
	    lump->dataofs[i] = LittleLong(lump->dataofs[i]);
    }

    /* marksurfaces */
    for (i = 0; i < nummarksurfaces; i++)
	dmarksurfaces[i] = LittleShort(dmarksurfaces[i]);

    /* surfedges */
    for (i = 0; i < numsurfedges; i++)
	dsurfedges[i] = LittleLong(dsurfedges[i]);

    /* edges */
    for (i = 0; i < numedges; i++) {
	dedges[i].v[0] = LittleShort(dedges[i].v[0]);
	dedges[i].v[1] = LittleShort(dedges[i].v[1]);
    }

    /* models */
    for (i = 0; i < nummodels; i++) {
	dmodel_t *dmodel = &dmodels[i];

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

static dheader_t *header;

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
    int i, bsp_version;

    /* load the file header */
    LoadFile(filename, (void *)&header);

    /* swap the header */
    for (i = 0; i < sizeof(dheader_t) / 4; i++)
	((int *)header)[i] = LittleLong(((int *)header)[i]);

    bsp_version = (int)header->version;
    logprint("BSP is version %i\n", bsp_version);

    if (bsp_version != 29)
	Error("Sorry, only bsp version 29 supported.");

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

    free(header);		/* everything has been copied out */

    /* swap everything */
    SwapBSPFile(false);

    /* Return the version */
    return bsp_version;
}

/* ========================================================================= */

static FILE *wadfile;

static void
AddLump(int lumpnum, const void *data, int len)
{
    lump_t *lump;
    byte pad[4] = {0};

    lump = &header->lumps[lumpnum];

    lump->fileofs = LittleLong(ftell(wadfile));
    lump->filelen = LittleLong(len);
    SafeWrite(wadfile, data, (len + 3) & ~3);
    if (len % 4)
	SafeWrite(wadfile, pad, len % 4);
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
    dheader_t outheader;

    header = &outheader;
    memset(header, 0, sizeof(dheader_t));

    SwapBSPFile(true);

    header->version = LittleLong(version);
    logprint("Writing BSP version %i\n", (int)header->version);
    wadfile = SafeOpenWrite(filename);
    SafeWrite(wadfile, header, sizeof(dheader_t));	/* overwritten later */

    AddLump(LUMP_PLANES, dplanes, numplanes * sizeof(dplane_t));
    AddLump(LUMP_LEAFS, dleafs, numleafs * sizeof(dleaf_t));
    AddLump(LUMP_VERTEXES, dvertexes, numvertexes * sizeof(dvertex_t));
    AddLump(LUMP_NODES, dnodes, numnodes * sizeof(dnode_t));
    AddLump(LUMP_TEXINFO, texinfo, numtexinfo * sizeof(texinfo_t));
    AddLump(LUMP_FACES, dfaces, numfaces * sizeof(dface_t));
    AddLump(LUMP_CLIPNODES, dclipnodes, numclipnodes * sizeof(dclipnode_t));
    AddLump(LUMP_MARKSURFACES, dmarksurfaces,
	    nummarksurfaces * sizeof(dmarksurfaces[0]));
    AddLump(LUMP_SURFEDGES, dsurfedges, numsurfedges * sizeof(dsurfedges[0]));
    AddLump(LUMP_EDGES, dedges, numedges * sizeof(dedge_t));
    AddLump(LUMP_MODELS, dmodels, nummodels * sizeof(dmodel_t));

    AddLump(LUMP_LIGHTING, dlightdata, lightdatasize);
    AddLump(LUMP_VISIBILITY, dvisdata, visdatasize);
    AddLump(LUMP_ENTITIES, dentdata, entdatasize);
    AddLump(LUMP_TEXTURES, dtexdata, texdatasize);

    fseek(wadfile, 0, SEEK_SET);
    SafeWrite(wadfile, header, sizeof(dheader_t));

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
