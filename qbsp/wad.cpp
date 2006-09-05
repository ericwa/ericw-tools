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
/*

WAD source file

*/

#include "qbsp.h"
#include "wad.h"

/*
==================
WAD
==================
*/
WAD::WAD (void) {
    cWads = 0;
    wadlist = NULL;
}

/*
==================
~WAD
==================
*/
WAD::~WAD (void) {
    if (wadlist) {
	for (iWad = 0; iWad < cWads; iWad++) {
	    wadlist[iWad].Wad.Close();
	    FreeMem(wadlist[iWad].lumps, OTHER,
		    sizeof(lumpinfo_t) * wadlist[iWad].header.numlumps);
	}
	FreeMem(wadlist, OTHER, cWads * sizeof(wadlist_t));
    }
}

/*
==================
InitWADList
==================
*/
bool WAD::InitWADList(char *szWadList)
{
    int i, len;
    void *pTemp;
    File *fileT;

    if (!szWadList)
	return false;

    len = strlen(szWadList);
    if (len == 0)
	return false;

    // Should never happen, but just in case...
    assert(!wadlist);

    // Count # of wads
    cWads = 1;
    for (i = 0; i < len; i++)
	if (szWadList[i] == ';' && szWadList[i + 1] != ';')
	    cWads++;

    wadlist = (wadlist_t *) AllocMem(OTHER, cWads * sizeof(wadlist_t));

    // Verify that at least one WAD file exists
    i = iWad = 0;
    while (i < len) {
	szWadName = szWadList + i;
	while (szWadList[i] != 0 && szWadList[i] != ';')
	    i++;
	szWadList[i] = 0;
	i++;

	fileT = &wadlist[iWad].Wad;
	if (fileT->fOpen(szWadName, "rb", false)) {
	    fileT->Read(&wadlist[iWad].header, sizeof(wadinfo_t));
	    if (strncmp(wadlist[iWad].header.identification, "WAD2", 4)) {
		Message(msgWarning, warnNotWad, szWadName);
		fileT->Close();
	    } else {
//                              strcpy(wadlist[iWad].szName, szWadName);
		fileT->Seek(wadlist[iWad].header.infotableofs, SEEK_SET);
		wadlist[iWad].lumps =
		    (lumpinfo_t *) AllocMem(OTHER,
					    sizeof(lumpinfo_t) *
					    wadlist[iWad].header.numlumps);
		fileT->Read(wadlist[iWad].lumps,
			    wadlist[iWad].header.numlumps *
			    sizeof(lumpinfo_t));
		iWad++;
		// Note that the file is NOT closed here!
		// Also iWad is only incremented for valid files
	    }
	}
    }

    // Remove invalid wads from memory
    pTemp = AllocMem(OTHER, iWad * sizeof(wadlist_t));
    memcpy(pTemp, wadlist, iWad * sizeof(wadlist_t));
    FreeMem(wadlist, OTHER, cWads * sizeof(wadlist_t));
    wadlist = (wadlist_t *) pTemp;
    cWads = iWad;

    return iWad > 0;
}


/*
==================
fProcessWAD
==================
*/
void WAD::fProcessWAD(void)
{
    int i, j;
    dmiptexlump_t *l;

    if (cWads < 1)
	return;

    AddAnimatingTextures();

    // Count texture size.  Slow but saves memory.
    for (i = 0; i < cMiptex; i++)
	for (iWad = 0; iWad < cWads; iWad++) {
	    for (j = 0; j < wadlist[iWad].header.numlumps; j++)
		if (!stricmp(rgszMiptex[i], wadlist[iWad].lumps[j].name)) {
		    // Found it. Add in the size and skip to outer loop.
// For EvlG                     printf("Texture: %9s in wad: %s\n", rgszMiptex[i], wadlist[iWad].szName);
		    pWorldEnt->cTexdata += wadlist[iWad].lumps[j].disksize;
		    iWad = cWads;
		    break;
		}
	    // If we found the texture already, break out to outer loop
	    if (j < wadlist[iWad].header.numlumps)
		break;
	}

    pWorldEnt->cTexdata += sizeof(int) * (cMiptex + 1);

    // Default texture data to store in worldmodel
    pWorldEnt->pTexdata = (byte *)AllocMem(BSPTEX, pWorldEnt->cTexdata);
    l = (dmiptexlump_t *)pWorldEnt->pTexdata;
    l->nummiptex = cMiptex;

    LoadTextures(l);

    // Last pass, mark unfound textures as such
    for (i = 0; i < cMiptex; i++)
	if (l->dataofs[i] == 0) {
	    l->dataofs[i] = -1;
	    Message(msgWarning, warnTextureNotFound, rgszMiptex[i]);
	}
}

/*
==================
LoadTextures
==================
*/
void WAD::LoadTextures(dmiptexlump_t *l)
{
    int i, len;
    byte *data;

    data = (byte *)&l->dataofs[cMiptex];
    for (iWad = 0; iWad < cWads; iWad++) {
	for (i = 0; i < cMiptex; i++) {
	    // Texture already found in a previous WAD
	    if (l->dataofs[i] != 0)
		continue;

	    l->dataofs[i] = data - (byte *)l;
	    len = LoadLump(rgszMiptex[i], data);
	    if (data + len - pWorldEnt->pTexdata > pWorldEnt->cTexdata)
		Message(msgError, errLowTextureCount);

	    // didn't find the texture
	    if (!len)
		l->dataofs[i] = 0;
	    data += len;
	}
    }
}

/*
==================
LoadLump
==================
*/
int WAD::LoadLump(char *szName, byte *pDest)
{
    int i;

    for (i = 0; i < wadlist[iWad].header.numlumps; i++) {
	if (!stricmp(szName, wadlist[iWad].lumps[i].name)) {
	    wadlist[iWad].Wad.Seek(wadlist[iWad].lumps[i].filepos, SEEK_SET);
	    wadlist[iWad].Wad.Read(pDest, wadlist[iWad].lumps[i].disksize);
	    return wadlist[iWad].lumps[i].disksize;
	}
    }

    return 0;
}


/*
==================
AddAnimatingTextures
==================
*/
void WAD::AddAnimatingTextures(void)
{
    int base;
    int i, j, k, l;
    char name[32];

    base = cMiptex;

    for (i = 0; i < base; i++) {
	if (rgszMiptex[i][0] != '+')
	    continue;
	strcpy(name, rgszMiptex[i]);

	for (j = 0; j < 20; j++) {
	    if (j < 10)
		name[1] = '0' + j;
	    else
		name[1] = 'A' + j - 10;	// alternate animation


	    // see if this name exists in the wadfiles
	    for (l = 0; l < cWads; l++)
		for (k = 0; k < wadlist[l].header.numlumps; k++)
		    if (!stricmp(name, wadlist[l].lumps[k].name)) {
			FindMiptex(name);	// add to the miptex list
			break;
		    }
	}
    }

    Message(msgStat, "%5i texture frames added", cMiptex - base);
}
