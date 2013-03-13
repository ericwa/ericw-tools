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
WAD_LoadInfo(wad_t *wad)
{
    wadinfo_t *hdr = &wad->header;
    int i, len, lumpinfosize, disksize;
    dmiptex_t miptex;

    len = fread(hdr, 1, sizeof(wadinfo_t), wad->file);
    if (len != sizeof(wadinfo_t))
	return false;

    wad->version = 0;
    if (!strncmp(hdr->identification, "WAD2", 4))
	wad->version = 2;
    else if (!strncmp(hdr->identification, "WAD3", 4))
	wad->version = 3;
    if (!wad->version)
	return false;

    lumpinfosize = sizeof(lumpinfo_t) * hdr->numlumps;
    fseek(wad->file, hdr->infotableofs, SEEK_SET);
    wad->lumps = AllocMem(OTHER, lumpinfosize, true);
    len = fread(wad->lumps, 1, lumpinfosize, wad->file);
    if (len != lumpinfosize)
	return false;

    if (wad->version == 2)
	return true;

    /*
     * WAD3 format includes a palette after the mipmap data.
     * Reduce the disksize in the lumpinfo so we can treat it like WAD2.
     */
    for (i = 0; i < wad->header.numlumps; i++) {
	fseek(wad->file, wad->lumps[i].filepos, SEEK_SET);
	len = fread(&miptex, 1, sizeof(miptex), wad->file);
	if (len != sizeof(miptex))
	    return false;
	disksize = miptex.width * miptex.height / 64 * 85;
	if (disksize < wad->lumps[i].disksize)
	    wad->lumps[i].disksize = disksize;
    }

    return true;
}


int
WADList_Init(wad_t **wads, const char *wadstring)
{
    int len, numwads;
    wad_t *wadlist, *wad;
    const char *fname;
    char *fpath;
    const char *pos;
    int pathlen;

    *wads = NULL;

    if (!wadstring)
	return 0;

    len = strlen(wadstring);
    if (len == 0)
	return 0;

    // Count # of wads
    numwads = 1;
    for (pos = wadstring; *pos ; pos++)
	if (pos[0] == ';' && pos[1] != ';')
	    numwads++;

    wadlist = AllocMem(OTHER, numwads * sizeof(wad_t), true);

    wad = wadlist;
    pos = wadstring;
    while (pos - wadstring < len) {
	fname = pos;
	while (*pos && *pos != ';')
	    pos++;

	if (!options.wadPath[0] || IsAbsolutePath(fname)) {
	    fpath = AllocMem(OTHER, (pos - fname) + 1, false);
	    snprintf(fpath, (pos - fname) + 1, "%s", fname);
	} else {
	    pathlen = strlen(options.wadPath) + 1 + (pos - fname);
	    fpath = AllocMem(OTHER, pathlen + 1, true);
	    snprintf(fpath, pathlen + 1, "%s/%s", options.wadPath, fname);
	}
	wad->file = fopen(fpath, "rb");
	if (wad->file) {
	    if (options.fVerbose)
		Message(msgLiteral, "Opened WAD: %s\n", fpath);
	    if (WAD_LoadInfo(wad)) {
		wad++;
	    } else {
		Message(msgWarning, warnNotWad, fpath);
		fclose(wad->file);
	    }
	}
	FreeMem(fpath, OTHER, strlen(fpath) + 1);
	pos++;
    }

    /* Re-allocate just the required amount */
    *wads = AllocMem(OTHER, (wad - wadlist) * sizeof(wad_t), false);
    memcpy(*wads, wadlist, (wad - wadlist) * sizeof(wad_t));
    FreeMem(wadlist, OTHER, numwads * sizeof(wad_t));
    numwads = wad - wadlist;

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

static lumpinfo_t *
WADList_FindTexture(const wad_t *wads, int numwads, const char *name)
{
    int i, j;
    const wad_t *wad;

    for (i = 0, wad = wads; i < numwads; i++, wad++)
	for (j = 0; j < wad->header.numlumps; j++)
	    if (!strcasecmp(name, wad->lumps[j].name))
		return &wad->lumps[j];

    return NULL;
}

void
WADList_Process(wad_t *wads, int numwads)
{
    int i;
    lumpinfo_t *texture;
    dmiptexlump_t *miptexlump;
    struct lumpdata *texdata = &pWorldEnt->lumps[BSPTEX];

    if (numwads < 1)
	return;

    WADList_AddAnimatingTextures(wads, numwads);

    // Count texture size.  Slow but saves memory.
    for (i = 0; i < map.nummiptex; i++) {
	texture = WADList_FindTexture(wads, numwads, map.miptex[i]);
	if (texture)
	    texdata->count += texture->disksize;
    }
    texdata->count += sizeof(int) * (map.nummiptex + 1);

    // Default texture data to store in worldmodel
    texdata->data = AllocMem(BSPTEX, texdata->count, true);
    miptexlump = (dmiptexlump_t *)texdata->data;
    miptexlump->nummiptex = map.nummiptex;

    WADList_LoadTextures(wads, numwads, miptexlump);

    // Last pass, mark unfound textures as such
    for (i = 0; i < map.nummiptex; i++)
	if (miptexlump->dataofs[i] == 0) {
	    miptexlump->dataofs[i] = -1;
	    Message(msgWarning, warnTextureNotFound, map.miptex[i]);
	}
}


static void
WADList_LoadTextures(wad_t *wads, int numwads, dmiptexlump_t *l)
{
    int i, j, len;
    byte *data;
    struct lumpdata *texdata = &pWorldEnt->lumps[BSPTEX];

    data = (byte *)&l->dataofs[map.nummiptex];
    for (i = 0; i < numwads; i++) {
	for (j = 0; j < map.nummiptex; j++) {
	    // Texture already found in a previous WAD
	    if (l->dataofs[j] != 0)
		continue;

	    l->dataofs[j] = data - (byte *)l;
	    len = WAD_LoadLump(wads + i, map.miptex[j], data);
	    if (data + len - (byte *)texdata->data > texdata->count)
		Error(errLowTextureCount);

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
		Error(errReadFailure);
	    return w->lumps[i].disksize;
	}
    }

    return 0;
}


static void
WADList_AddAnimatingTextures(wad_t *wads, int numwads)
{
    int base;
    int i, j;
    char name[32];

    base = map.nummiptex;

    for (i = 0; i < base; i++) {
	if (map.miptex[i][0] != '+')
	    continue;
	strcpy(name, map.miptex[i]);

	/* Search for all animations (0-9) and alt-animations (A-J) */
	for (j = 0; j < 20; j++) {
	    name[1] = (j < 10) ? '0' + j : 'A' + j - 10;
	    if (WADList_FindTexture(wads, numwads, name))
		FindMiptex(name);
	}
    }

    Message(msgStat, "%5i texture frames added", map.nummiptex - base);
}
