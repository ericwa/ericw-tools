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

static void WADList_LoadTextures(wad_t *wads, int numwads, dmiptexlump_t *l);
static void WADList_AddAnimatingTextures(wad_t *wads, int numwads);
static int WAD_LoadLump(wad_t *w, char *name, byte *dest);


static bool
WAD_LoadInfo(wad_t *w)
{
    wadinfo_t *hdr = &w->header;
    bool ret = false;
    int len;

    len = fread(hdr, 1, sizeof(wadinfo_t), w->file);
    if (len == sizeof(wadinfo_t))
	if (!strncmp(hdr->identification, "WAD2", 4)) {
	    const int lumpinfo = sizeof(lumpinfo_t) * hdr->numlumps;
	    fseek(w->file, hdr->infotableofs, SEEK_SET);
	    w->lumps = AllocMem(OTHER, lumpinfo, true);
	    len = fread(w->lumps, 1, lumpinfo, w->file);
	    if (len == lumpinfo)
		ret = true;
	}

    return ret;
}


int
WADList_Init(wad_t **wads, char *wadstring)
{
    int i, len, numwads;
    wad_t *tmp, *w;
    char *fname;
    char *fpath;
    int pathlen;

    numwads = 0;
    *wads = NULL;

    if (!wadstring)
	return 0;

    len = strlen(wadstring);
    if (len == 0)
	return 0;

    // Count # of wads
    numwads = 1;
    for (i = 0; i < len; i++)
	if (wadstring[i] == ';' && wadstring[i + 1] != ';')
	    numwads++;

    tmp = AllocMem(OTHER, numwads * sizeof(wad_t), true);

    w = tmp;
    i = 0;
    while (i < len) {
	fname = wadstring + i;
	while (wadstring[i] != 0 && wadstring[i] != ';')
	    i++;
	wadstring[i++] = 0;

	if (!options.wadPath[0] || IsAbsolutePath(fname))
	    fpath = copystring(fname);
	else {
	    pathlen = strlen(options.wadPath) + strlen(fname) + 1;
	    fpath = AllocMem(OTHER, pathlen + 1, false);
	    sprintf(fpath, "%s/%s", options.wadPath, fname);
	}
	w->file = fopen(fpath, "rb");
	if (w->file) {
	    if (options.fVerbose)
		Message(msgLiteral, "Opened WAD: %s\n", fpath);
	    if (!WAD_LoadInfo(w)) {
		Message(msgWarning, warnNotWad, fpath);
		fclose(w->file);
	    } else
		w++;
	}
	FreeMem(fpath, OTHER, strlen(fpath) + 1);
    }

    /* Re-allocate just the required amount */
    *wads = AllocMem(OTHER, (w - tmp) * sizeof(wad_t), false);
    memcpy(*wads, tmp, (w - tmp) * sizeof(wad_t));
    FreeMem(tmp, OTHER, numwads * sizeof(wad_t));
    numwads = w - tmp;

    return numwads;
}


void
WADList_Free(wad_t *wads, int numwads)
{
    int i;

    if (wads) {
	for (i = 0; i < numwads; i++) {
	    fclose(wads[i].file);
	    FreeMem(wads[i].lumps, OTHER,
		    sizeof(lumpinfo_t) * wads[i].header.numlumps);
	}
	FreeMem(wads, OTHER, numwads * sizeof(wad_t));
    }
}


void
WADList_Process(wad_t *wads, int numwads)
{
    int i, j, k;
    dmiptexlump_t *miptex;
    struct lumpdata *texdata = &pWorldEnt->lumps[BSPTEX];

    if (numwads < 1)
	return;

    WADList_AddAnimatingTextures(wads, numwads);

    // Count texture size.  Slow but saves memory.
    for (i = 0; i < cMiptex; i++)
	for (j = 0; j < numwads; j++) {
	    for (k = 0; k < wads[j].header.numlumps; k++)
		if (!strcasecmp(rgszMiptex[i], wads[j].lumps[k].name)) {
		    // Found it. Add in the size and skip to outer loop.
		    texdata->count += wads[j].lumps[k].disksize;
		    j = numwads;
		    break;
		}
	    // If we found the texture already, break out to outer loop
	    if (k < wads[j].header.numlumps)
		break;
	}

    texdata->count += sizeof(int) * (cMiptex + 1);

    // Default texture data to store in worldmodel
    texdata->data = AllocMem(BSPTEX, texdata->count, true);
    miptex = (dmiptexlump_t *)texdata->data;
    miptex->nummiptex = cMiptex;

    WADList_LoadTextures(wads, numwads, miptex);

    // Last pass, mark unfound textures as such
    for (i = 0; i < cMiptex; i++)
	if (miptex->dataofs[i] == 0) {
	    miptex->dataofs[i] = -1;
	    Message(msgWarning, warnTextureNotFound, rgszMiptex[i]);
	}
}


static void
WADList_LoadTextures(wad_t *wads, int numwads, dmiptexlump_t *l)
{
    int i, j, len;
    byte *data;
    struct lumpdata *texdata = &pWorldEnt->lumps[BSPTEX];

    data = (byte *)&l->dataofs[cMiptex];
    for (i = 0; i < numwads; i++) {
	for (j = 0; j < cMiptex; j++) {
	    // Texture already found in a previous WAD
	    if (l->dataofs[j] != 0)
		continue;

	    l->dataofs[j] = data - (byte *)l;
	    len = WAD_LoadLump(wads + i, rgszMiptex[j], data);
	    if (data + len - (byte *)texdata->data > texdata->count)
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
	    fseek(w->file, w->lumps[i].filepos, SEEK_SET);
	    len = fread(dest, 1, w->lumps[i].disksize, w->file);
	    if (len != w->lumps[i].disksize)
		Message(msgError, errReadFailure);
	    return w->lumps[i].disksize;
	}
    }

    return 0;
}


static void
WADList_AddAnimatingTextures(wad_t *wads, int numwads)
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
	    for (l = 0; l < numwads; l++)
		for (k = 0; k < wads[l].header.numlumps; k++)
		    if (!strcasecmp(name, wads[l].lumps[k].name)) {
			FindMiptex(name);	// add to the miptex list
			break;
		    }
	}
    }

    Message(msgStat, "%5i texture frames added", cMiptex - base);
}
