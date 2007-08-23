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
    mapentity_t *ent;

    // Load the file header
    StripExtension(options.szBSPName);
    strcat(options.szBSPName, ".bsp");
    cFileSize = LoadFile(options.szBSPName, (void *)&header, true);

    if (header->version != BSPVERSION)
	Message(msgError, errBadVersion, options.szBSPName, header->version,
		BSPVERSION);

    /* Throw all of the data into the first entity to be written out later */
    ent = map.rgEntities;
    for (i = 0; i < BSP_LUMPS; i++) {
	map.cTotal[i] = cLumpSize = header->lumps[i].filelen;
	iLumpOff = header->lumps[i].fileofs;
	if (cLumpSize % rgcMemSize[i])
	    Message(msgError, errDeformedBSPLump, rgcMemSize[i], cLumpSize);

	ent->lumps[i].count = cLumpSize / rgcMemSize[i];
	ent->lumps[i].data = AllocMem(i, ent->lumps[i].count, false);

	memcpy(ent->lumps[i].data, (byte *)header + iLumpOff, cLumpSize);
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
    int iEntity;
    size_t ret;
    struct lumpdata *entities;

    lump = &header->lumps[Type];
    lump->fileofs = ftell(f);

    for (iEntity = 0; iEntity < map.cEntities; iEntity++) {
	entities = &map.rgEntities[iEntity].lumps[Type];
	if (entities->data) {
	    ret = fwrite(entities->data, rgcMemSize[Type], entities->count, f);
	    if (ret != entities->count)
		Message(msgError, errWriteFailure);
	    cLen += entities->count * rgcMemSize[Type];
	}
    }

    // Add null terminating char for text
    if (Type == BSPENT) {
	ret = fwrite("", 1, 1, f);
	if (ret != 1)
	    Message(msgError, errWriteFailure);
	cLen++;
    }
    lump->filelen = cLen;

    // Pad to 4-byte boundary
    if (cLen % 4 != 0) {
	size_t pad = 4 - (cLen % 4);
	ret = fwrite("   ", 1, pad, f);
	if (ret != pad)
	    Message(msgError, errWriteFailure);
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
    header->version = BSPVERSION;

    StripExtension(options.szBSPName);
    strcat(options.szBSPName, ".bsp");

    f = fopen(options.szBSPName, "wb");
    if (f == NULL)
	Message(msgError, errOpenFailed, options.szBSPName, strerror(errno));

    /* write placeholder, header is overwritten later */
    ret = fwrite(header, sizeof(dheader_t), 1, f);
    if (ret != 1)
	Message(msgError, errWriteFailure);

    AddLump(f, BSPPLANE);
    AddLump(f, BSPLEAF);
    AddLump(f, BSPVERTEX);
    AddLump(f, BSPNODE);
    AddLump(f, BSPTEXINFO);
    AddLump(f, BSPFACE);
    AddLump(f, BSPCLIPNODE);
    AddLump(f, BSPMARKSURF);
    AddLump(f, BSPSURFEDGE);
    AddLump(f, BSPEDGE);
    AddLump(f, BSPMODEL);

    AddLump(f, BSPLIGHT);
    AddLump(f, BSPVIS);
    AddLump(f, BSPENT);
    AddLump(f, BSPTEX);

    fseek(f, 0, SEEK_SET);
    ret = fwrite(header, sizeof(dheader_t), 1, f);
    if (ret != 1)
	Message(msgError, errWriteFailure);

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

    Message(msgStat, "%6i planes       %8i", map.cTotal[BSPPLANE],
	    map.cTotal[BSPPLANE] * rgcMemSize[BSPPLANE]);
    Message(msgStat, "%6i vertexes     %8i", map.cTotal[BSPVERTEX],
	    map.cTotal[BSPVERTEX] * rgcMemSize[BSPVERTEX]);
    Message(msgStat, "%6i nodes        %8i", map.cTotal[BSPNODE],
	    map.cTotal[BSPNODE] * rgcMemSize[BSPNODE]);
    Message(msgStat, "%6i texinfo      %8i", map.cTotal[BSPTEXINFO],
	    map.cTotal[BSPTEXINFO] * rgcMemSize[BSPTEXINFO]);
    Message(msgStat, "%6i faces        %8i", map.cTotal[BSPFACE],
	    map.cTotal[BSPFACE] * rgcMemSize[BSPFACE]);
    Message(msgStat, "%6i clipnodes    %8i", map.cTotal[BSPCLIPNODE],
	    map.cTotal[BSPCLIPNODE] * rgcMemSize[BSPCLIPNODE]);
    Message(msgStat, "%6i leafs        %8i", map.cTotal[BSPLEAF],
	    map.cTotal[BSPLEAF] * rgcMemSize[BSPLEAF]);
    Message(msgStat, "%6i marksurfaces %8i", map.cTotal[BSPMARKSURF],
	    map.cTotal[BSPMARKSURF] * rgcMemSize[BSPMARKSURF]);
    Message(msgStat, "%6i surfedges    %8i", map.cTotal[BSPSURFEDGE],
	    map.cTotal[BSPSURFEDGE] * rgcMemSize[BSPSURFEDGE]);
    Message(msgStat, "%6i edges        %8i", map.cTotal[BSPEDGE],
	    map.cTotal[BSPEDGE] * rgcMemSize[BSPEDGE]);

    lump = &pWorldEnt->lumps[BSPTEX];
    if (lump->data)
	Message(msgStat, "%6i textures     %8i",
		((dmiptexlump_t *)lump->data)->nummiptex, lump->count);
    else
	Message(msgStat, "     0 textures            0");

    Message(msgStat, "       lightdata    %8i", map.cTotal[BSPLIGHT]);
    Message(msgStat, "       visdata      %8i", map.cTotal[BSPVIS]);
    Message(msgStat, "       entdata      %8i", map.cTotal[BSPENT] + 1);
}
