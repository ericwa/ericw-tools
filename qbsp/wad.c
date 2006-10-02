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

#include <string.h>

#include "qbsp.h"
#include "wad.h"

static void WADList_LoadTextures(wadlist_t *w, dmiptexlump_t *l);
static void WADList_AddAnimatingTextures(wadlist_t *w);
static int WAD_LoadLump(wad_t *w, char *name, byte *dest);


void
WADList_Init(wadlist_t *w)
{
    w->numwads = 0;
    w->wadlist = NULL;
}


void
WADList_Free(wadlist_t *w)
{
    int i;

    if (w->wadlist) {
	for (i = 0; i < w->numwads; i++) {
	    fclose(w->wadlist[i].Wad);
	    FreeMem(w->wadlist[i].lumps, OTHER,
		    sizeof(lumpinfo_t) * w->wadlist[i].header.numlumps);
	}
	FreeMem(w->wadlist, OTHER, w->numwads * sizeof(wad_t));
    }
}


bool
WADList_LoadLumpInfo(wadlist_t *w, char *list)
{
    int i, len, ret;
    FILE *fileT;
    wad_t *wad, *tmp;
    char *fname;

    if (!list)
	return false;

    len = strlen(list);
    if (len == 0)
	return false;

    // Should never happen, but just in case...
    assert(!w->wadlist);

    // Count # of wads
    w->numwads = 1;
    for (i = 0; i < len; i++)
	if (list[i] == ';' && list[i + 1] != ';')
	    w->numwads++;

    w->wadlist = AllocMem(OTHER, w->numwads * sizeof(wad_t), true);

    // Verify that at least one WAD file exists
    wad = w->wadlist;
    i = 0;
    while (i < len) {
	fname = list + i;
	while (list[i] != 0 && list[i] != ';')
	    i++;
	list[i] = 0;
	i++;

	fileT = fopen(fname, "rb");
	if (fileT) {
	    wad->Wad = fileT;
	    ret = fread(&wad->header, 1, sizeof(wadinfo_t), fileT);
	    if (ret != sizeof(wadinfo_t))
		Message(msgError, errReadFailure);
	    if (strncmp(wad->header.identification, "WAD2", 4)) {
		Message(msgWarning, warnNotWad, fname);
		fclose(fileT);
	    } else {
		fseek(fileT, wad->header.infotableofs, SEEK_SET);
		wad->lumps = AllocMem(OTHER, sizeof(lumpinfo_t) *
				      wad->header.numlumps, true);
		ret = fread(wad->lumps, 1, wad->header.numlumps *
			    sizeof(lumpinfo_t), fileT);
		if (ret != wad->header.numlumps * sizeof(lumpinfo_t))
		    Message(msgError, errReadFailure);
		wad++;
		// Note that the file is NOT closed here!
		// Also iWad is only incremented for valid files
	    }
	}
    }

    // Remove invalid wads from memory
    tmp = AllocMem(OTHER, (wad - w->wadlist) * sizeof(wad_t), true);
    memcpy(tmp, w->wadlist, (wad - w->wadlist) * sizeof(wad_t));
    FreeMem(w->wadlist, OTHER, w->numwads * sizeof(wad_t));
    w->numwads = wad - w->wadlist;
    w->wadlist = tmp;

    return w->numwads > 0;
}


void
WADList_Process(wadlist_t *w)
{
    int i, j, k;
    dmiptexlump_t *l;

    if (w->numwads < 1)
	return;

    WADList_AddAnimatingTextures(w);

    // Count texture size.  Slow but saves memory.
    for (i = 0; i < cMiptex; i++)
	for (j = 0; j < w->numwads; j++) {
	    for (k = 0; k < w->wadlist[j].header.numlumps; k++)
		if (!strcasecmp(rgszMiptex[i], w->wadlist[j].lumps[k].name)) {
		    // Found it. Add in the size and skip to outer loop.
		    pWorldEnt->cTexdata += w->wadlist[j].lumps[k].disksize;
		    j = w->numwads;
		    break;
		}
	    // If we found the texture already, break out to outer loop
	    if (k < w->wadlist[j].header.numlumps)
		break;
	}

    pWorldEnt->cTexdata += sizeof(int) * (cMiptex + 1);

    // Default texture data to store in worldmodel
    pWorldEnt->pTexdata = AllocMem(BSPTEX, pWorldEnt->cTexdata, true);
    l = (dmiptexlump_t *)pWorldEnt->pTexdata;
    l->nummiptex = cMiptex;

    WADList_LoadTextures(w, l);

    // Last pass, mark unfound textures as such
    for (i = 0; i < cMiptex; i++)
	if (l->dataofs[i] == 0) {
	    l->dataofs[i] = -1;
	    Message(msgWarning, warnTextureNotFound, rgszMiptex[i]);
	}
}


static void
WADList_LoadTextures(wadlist_t *w, dmiptexlump_t *l)
{
    int i, j, len;
    byte *data;

    data = (byte *)&l->dataofs[cMiptex];
    for (i = 0; i < w->numwads; i++) {
	for (j = 0; j < cMiptex; j++) {
	    // Texture already found in a previous WAD
	    if (l->dataofs[j] != 0)
		continue;

	    l->dataofs[j] = data - (byte *)l;
	    len = WAD_LoadLump(w->wadlist + i, rgszMiptex[j], data);
	    if (data + len - pWorldEnt->pTexdata > pWorldEnt->cTexdata)
		Message(msgError, errLowTextureCount);

	    // didn't find the texture
	    if (!len)
		l->dataofs[j] = 0;
	    data += len;
	}
    }
}


static int
WAD_LoadLump(wad_t *w, char *name, byte *dest)
{
    int i;
    int len;

    for (i = 0; i < w->header.numlumps; i++) {
	if (!strcasecmp(name, w->lumps[i].name)) {
	    fseek(w->Wad, w->lumps[i].filepos, SEEK_SET);
	    len = fread(dest, 1, w->lumps[i].disksize, w->Wad);
	    if (len != w->lumps[i].disksize)
		Message(msgError, errReadFailure);
	    return w->lumps[i].disksize;
	}
    }

    return 0;
}


static void
WADList_AddAnimatingTextures(wadlist_t *w)
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
	    for (l = 0; l < w->numwads; l++)
		for (k = 0; k < w->wadlist[l].header.numlumps; k++)
		    if (!strcasecmp(name, w->wadlist[l].lumps[k].name)) {
			FindMiptex(name);	// add to the miptex list
			break;
		    }
	}
    }

    Message(msgStat, "%5i texture frames added", cMiptex - base);
}
