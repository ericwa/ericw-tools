#include "qbsp.h"

dheader_t	*header;

/*
=============
LoadBSPFile
=============
*/
void LoadBSPFile(void)
{
	int i;
	int cFileSize, cLumpSize, iLumpOff;
	File BSPFile;
	
	// Load the file header
	StripExtension(options.szBSPName);
	strcat(options.szBSPName, ".bsp");
	cFileSize = BSPFile.LoadFile(options.szBSPName, (void **)&header);

	if (header->version != BSPVERSION)
		Message(msgError, errBadVersion, options.szBSPName, header->version, BSPVERSION);

	// Throw all of the data into the first entity to be written out later
	for (i=0; i<BSP_LUMPS; i++)
	{
		map.cTotal[i] = cLumpSize = header->lumps[i].filelen;
		iLumpOff = header->lumps[i].fileofs;

		if (cLumpSize % rgcMemSize[i])
			Message(msgError, errDeformedBSPLump, rgcMemSize[i], cLumpSize);

		map.rgEntities[0].cData[i] = cLumpSize / rgcMemSize[i];
		map.rgEntities[0].pData[i] = AllocMem(i, map.rgEntities[0].cData[i], false);

		memcpy(map.rgEntities[0].pData[i], (byte *)header+iLumpOff, cLumpSize);
	}

	FreeMem(header, OTHER, cFileSize+1);
}

//============================================================================

// To be used for all dynamic mem data
void AddLump (File *pBSPFile, int Type)
{
	lump_t *lump;
	int cLen = 0, templen;
	int iEntity;

	lump = &header->lumps[Type];
	lump->fileofs = pBSPFile->Position();

	for (iEntity = 0; iEntity < map.cEntities; iEntity++)
	{
		if (map.rgEntities[iEntity].pData[Type] != NULL)
		{
			templen = map.rgEntities[iEntity].cData[Type] * rgcMemSize[Type];
			pBSPFile->Write(map.rgEntities[iEntity].pData[Type], templen);
			cLen += templen;
		}
	}

	// Add null terminating char for text
	if (Type == BSPENT)
	{
		pBSPFile->Write("", 1);
		cLen++;
	}
	lump->filelen = cLen;

	// Pad to 4-byte boundary
	if (cLen % 4 != 0)
		pBSPFile->Write("   ", 4-(cLen%4));
}

/*
=============
WriteBSPFile
=============
*/
void WriteBSPFile(void)
{
	File BSPFile;

	header = (dheader_t *)AllocMem(OTHER, sizeof(dheader_t));
	header->version = BSPVERSION;
	
	StripExtension(options.szBSPName);
	strcat(options.szBSPName, ".bsp");

	BSPFile.fOpen(options.szBSPName, "wb");
	BSPFile.Write(header, sizeof(dheader_t));	// overwritten later

	AddLump(&BSPFile, BSPPLANE);
	AddLump(&BSPFile, BSPLEAF);
	AddLump(&BSPFile, BSPVERTEX);
	AddLump(&BSPFile, BSPNODE);
	AddLump(&BSPFile, BSPTEXINFO);
	AddLump(&BSPFile, BSPFACE);
	AddLump(&BSPFile, BSPCLIPNODE);
	AddLump(&BSPFile, BSPMARKSURF);
	AddLump(&BSPFile, BSPSURFEDGE);
	AddLump(&BSPFile, BSPEDGE);
	AddLump(&BSPFile, BSPMODEL);

	AddLump(&BSPFile, BSPLIGHT);
	AddLump(&BSPFile, BSPVIS);
	AddLump(&BSPFile, BSPENT);
	AddLump(&BSPFile, BSPTEX);
	
	BSPFile.Seek(0, SEEK_SET);
	BSPFile.Write(header, sizeof(dheader_t));
	BSPFile.Close();

	FreeMem(header, OTHER, sizeof(dheader_t));
}

//============================================================================

/*
=============
PrintBSPFileSizes

Dumps info about current file
=============
*/
void PrintBSPFileSizes (void)
{
	Message(msgStat, "%5i planes       %6i", map.cTotal[BSPPLANE], map.cTotal[BSPPLANE]*rgcMemSize[BSPPLANE]);
	Message(msgStat, "%5i vertexes     %6i", map.cTotal[BSPVERTEX], map.cTotal[BSPVERTEX]*rgcMemSize[BSPVERTEX]);
	Message(msgStat, "%5i nodes        %6i", map.cTotal[BSPNODE], map.cTotal[BSPNODE]*rgcMemSize[BSPNODE]);
	Message(msgStat, "%5i texinfo      %6i", map.cTotal[BSPTEXINFO], map.cTotal[BSPTEXINFO]*rgcMemSize[BSPTEXINFO]);
	Message(msgStat, "%5i faces        %6i", map.cTotal[BSPFACE], map.cTotal[BSPFACE]*rgcMemSize[BSPFACE]);
	Message(msgStat, "%5i clipnodes    %6i", map.cTotal[BSPCLIPNODE], map.cTotal[BSPCLIPNODE]*rgcMemSize[BSPCLIPNODE]);
	Message(msgStat, "%5i leafs        %6i", map.cTotal[BSPLEAF], map.cTotal[BSPLEAF]*rgcMemSize[BSPLEAF]);
	Message(msgStat, "%5i marksurfaces %6i", map.cTotal[BSPMARKSURF], map.cTotal[BSPMARKSURF]*rgcMemSize[BSPMARKSURF]);
	Message(msgStat, "%5i surfedges    %6i", map.cTotal[BSPSURFEDGE], map.cTotal[BSPSURFEDGE]*rgcMemSize[BSPSURFEDGE]);
	Message(msgStat, "%5i edges        %6i", map.cTotal[BSPEDGE], map.cTotal[BSPEDGE]*rgcMemSize[BSPEDGE]);
	if (!pWorldEnt->cTexdata)
		Message(msgStat, "    0 textures          0");
	else
		Message(msgStat, "%5i textures     %6i", ((dmiptexlump_t*)pWorldEnt->pTexdata)->nummiptex, pWorldEnt->cTexdata);
	Message(msgStat, "      lightdata    %6i", map.cTotal[BSPLIGHT]);
	Message(msgStat, "      visdata      %6i", map.cTotal[BSPVIS]);
	Message(msgStat, "      entdata      %6i", map.cTotal[BSPENT]+1); // +1 for null terminator
}


