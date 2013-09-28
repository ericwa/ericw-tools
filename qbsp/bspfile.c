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

#include <stddef.h>

#include "file.h"
#include "qbsp.h"

static dheader_t *header;

/*
=============
LoadBSPFile
=============
*/
void
LoadBSPFile(void)
{
    int i;
    int cFileSize, cLumpSize, iLumpOff;
    mapentity_t *entity;

    // Load the file header
    StripExtension(options.szBSPName);
    strcat(options.szBSPName, ".bsp");
    cFileSize = LoadFile(options.szBSPName, &header, true);

    switch (header->version) {
    case BSPVERSION:
	MemSize = MemSize_BSP29;
	break;
    case BSP2RMQVERSION:
	MemSize = MemSize_BSP2rmq;
	break;
    case BSP2VERSION:
	MemSize = MemSize_BSP2;
	break;
    default:
	Error("%s has unknown BSP version %d",
	      options.szBSPName, header->version);
    }
    options.BSPVersion = header->version;

    /* Throw all of the data into the first entity to be written out later */
    entity = map.entities;
    for (i = 0; i < BSP_LUMPS; i++) {
	map.cTotal[i] = cLumpSize = header->lumps[i].filelen;
	iLumpOff = header->lumps[i].fileofs;
	if (cLumpSize % MemSize[i])
	    Error("Deformed lump in BSP file (size %d is not divisible by %d)",
		  cLumpSize, MemSize[i]);

	entity->lumps[i].count = cLumpSize / MemSize[i];
	entity->lumps[i].data = AllocMem(i, entity->lumps[i].count, false);

	memcpy(entity->lumps[i].data, (byte *)header + iLumpOff, cLumpSize);
    }

    FreeMem(header, OTHER, cFileSize + 1);
}

//============================================================================

// To be used for all dynamic mem data
static void
AddLump(FILE *f, int Type)
{
    lump_t *lump;
    int cLen = 0;
    int i;
    size_t ret;
    const struct lumpdata *entities;
    const mapentity_t *entity;

    lump = &header->lumps[Type];
    lump->fileofs = ftell(f);

    for (i = 0, entity = map.entities; i < map.numentities; i++, entity++) {
	entities = &entity->lumps[Type];
	if (entities->data) {
	    ret = fwrite(entities->data, MemSize[Type], entities->count, f);
	    if (ret != entities->count)
		Error("Failure writing to file");
	    cLen += entities->count * MemSize[Type];
	}
    }

    // Add null terminating char for text
    if (Type == LUMP_ENTITIES) {
	ret = fwrite("", 1, 1, f);
	if (ret != 1)
	    Error("Failure writing to file");
	cLen++;
    }
    lump->filelen = cLen;

    // Pad to 4-byte boundary
    if (cLen % 4 != 0) {
	size_t pad = 4 - (cLen % 4);
	ret = fwrite("   ", 1, pad, f);
	if (ret != pad)
	    Error("Failure writing to file");
    }
}

/*
=============
WriteBSPFile
=============
*/
void
WriteBSPFile(void)
{
    FILE *f;
    size_t ret;

    header = AllocMem(OTHER, sizeof(dheader_t), true);
    header->version = options.BSPVersion;

    StripExtension(options.szBSPName);
    strcat(options.szBSPName, ".bsp");

    f = fopen(options.szBSPName, "wb");
    if (!f)
	Error("Failed to open %s: %s", options.szBSPName, strerror(errno));

    /* write placeholder, header is overwritten later */
    ret = fwrite(header, sizeof(dheader_t), 1, f);
    if (ret != 1)
	Error("Failure writing to file");

    AddLump(f, LUMP_PLANES);
    AddLump(f, LUMP_LEAFS);
    AddLump(f, LUMP_VERTEXES);
    AddLump(f, LUMP_NODES);
    AddLump(f, LUMP_TEXINFO);
    AddLump(f, LUMP_FACES);
    AddLump(f, LUMP_CLIPNODES);
    AddLump(f, LUMP_MARKSURFACES);
    AddLump(f, LUMP_SURFEDGES);
    AddLump(f, LUMP_EDGES);
    AddLump(f, LUMP_MODELS);

    AddLump(f, LUMP_LIGHTING);
    AddLump(f, LUMP_VISIBILITY);
    AddLump(f, LUMP_ENTITIES);
    AddLump(f, LUMP_TEXTURES);

    fseek(f, 0, SEEK_SET);
    ret = fwrite(header, sizeof(dheader_t), 1, f);
    if (ret != 1)
	Error("Failure writing to file");

    fclose(f);
    FreeMem(header, OTHER, sizeof(dheader_t));
}

//============================================================================

/*
=============
PrintBSPFileSizes

Dumps info about current file
=============
*/
void
PrintBSPFileSizes(void)
{
    struct lumpdata *lump;

    Message(msgStat, "%8d planes       %10d", map.cTotal[LUMP_PLANES],
	    map.cTotal[LUMP_PLANES] * MemSize[BSP_PLANE]);
    Message(msgStat, "%8d vertexes     %10d", map.cTotal[LUMP_VERTEXES],
	    map.cTotal[LUMP_VERTEXES] * MemSize[BSP_VERTEX]);
    Message(msgStat, "%8d nodes        %10d", map.cTotal[LUMP_NODES],
	    map.cTotal[LUMP_NODES] * MemSize[BSP_NODE]);
    Message(msgStat, "%8d texinfo      %10d", map.cTotal[LUMP_TEXINFO],
	    map.cTotal[LUMP_TEXINFO] * MemSize[BSP_TEXINFO]);
    Message(msgStat, "%8d faces        %10d", map.cTotal[LUMP_FACES],
	    map.cTotal[LUMP_FACES] * MemSize[BSP_FACE]);
    Message(msgStat, "%8d clipnodes    %10d", map.cTotal[LUMP_CLIPNODES],
	    map.cTotal[LUMP_CLIPNODES] * MemSize[BSP_CLIPNODE]);
    Message(msgStat, "%8d leafs        %10d", map.cTotal[LUMP_LEAFS],
	    map.cTotal[LUMP_LEAFS] * MemSize[BSP_LEAF]);
    Message(msgStat, "%8d marksurfaces %10d", map.cTotal[LUMP_MARKSURFACES],
	    map.cTotal[LUMP_MARKSURFACES] * MemSize[BSP_MARKSURF]);
    Message(msgStat, "%8d surfedges    %10d", map.cTotal[LUMP_SURFEDGES],
	    map.cTotal[LUMP_SURFEDGES] * MemSize[BSP_SURFEDGE]);
    Message(msgStat, "%8d edges        %10d", map.cTotal[LUMP_EDGES],
	    map.cTotal[LUMP_EDGES] * MemSize[BSP_EDGE]);

    lump = &pWorldEnt->lumps[LUMP_TEXTURES];
    if (lump->data)
	Message(msgStat, "%8d textures     %10d",
		((dmiptexlump_t *)lump->data)->nummiptex, lump->count);
    else
	Message(msgStat, "       0 textures              0");

    Message(msgStat, "         lightdata    %10d", map.cTotal[LUMP_LIGHTING]);
    Message(msgStat, "         visdata      %10d", map.cTotal[LUMP_VISIBILITY]);
    Message(msgStat, "         entdata      %10d", map.cTotal[LUMP_ENTITIES] + 1);
}
