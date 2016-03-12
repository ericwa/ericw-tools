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

static void WADList_LoadTextures(const wad_t *wadlist, dmiptexlump_t *lump);
static int WAD_LoadLump(const wad_t *wad, const char *name, byte *dest);
static void WADList_AddAnimationFrames(const wad_t *wadlist);

static texture_t *textures;

static bool
WAD_LoadInfo(wad_t *wad)
{
    wadinfo_t *hdr = &wad->header;
    int i, len, lumpinfosize, disksize;
    dmiptex_t miptex;
    texture_t *tex;

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
    wad->lumps = (lumpinfo_t *)AllocMem(OTHER, lumpinfosize, true);
    len = fread(wad->lumps, 1, lumpinfosize, wad->file);
    if (len != lumpinfosize)
        return false;

    /* Get the dimensions and make a texture_t */
    for (i = 0; i < wad->header.numlumps; i++) {
        fseek(wad->file, wad->lumps[i].filepos, SEEK_SET);
        len = fread(&miptex, 1, sizeof(miptex), wad->file);
        if (len == sizeof(miptex))
        {
            tex = (texture_t *)AllocMem(OTHER, sizeof(texture_t), true);
            tex->next = textures;
            textures = tex;
            memcpy(tex->name, miptex.name, 16);
            tex->name[15] = '\0';
            tex->width = miptex.width;
            tex->height = miptex.height;

            //printf("Created texture_t %s %d %d\n", tex->name, tex->width, tex->height);
        }
    }

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
        disksize = sizeof(miptex) + (miptex.width * miptex.height / 64 * 85);
        if (disksize < wad->lumps[i].disksize)
            wad->lumps[i].disksize = disksize;
    }

    return true;
}


wad_t *
WADList_Init(const char *wadstring)
{
    int len;
    wad_t wad, *wadlist, *newwad;
    const char *fname;
    char *fpath;
    const char *pos;
    int pathlen;

    if (!wadstring || !wadstring[0])
        return NULL;

    wadlist = NULL;
    len = strlen(wadstring);
    pos = wadstring;
    while (pos - wadstring < len) {
        fname = pos;
        while (*pos && *pos != ';')
            pos++;

        if (!options.wadPath[0] || IsAbsolutePath(fname)) {
            fpath = (char *)AllocMem(OTHER, (pos - fname) + 1, false);
            snprintf(fpath, (pos - fname) + 1, "%s", fname);
        } else {
            pathlen = strlen(options.wadPath) + 1 + (pos - fname);
            fpath = (char *)AllocMem(OTHER, pathlen + 1, true);
            snprintf(fpath, pathlen + 1, "%s/%s", options.wadPath, fname);
        }
        wad.file = fopen(fpath, "rb");
        if (wad.file) {
            if (options.fVerbose)
                Message(msgLiteral, "Opened WAD: %s\n", fpath);
            if (WAD_LoadInfo(&wad)) {
                newwad = (wad_t *)AllocMem(OTHER, sizeof(wad), true);
                memcpy(newwad, &wad, sizeof(wad));
                newwad->next = wadlist;
                wadlist = newwad;
            } else {
                Message(msgWarning, warnNotWad, fpath);
                fclose(wad.file);
            }
        }
        FreeMem(fpath, OTHER, strlen(fpath) + 1);
        pos++;
    }

    return wadlist;
}


void
WADList_Free(wad_t *wadlist)
{
    wad_t *wad, *next;

    for (wad = wadlist; wad; wad = next) {
        next = wad->next;
        fclose(wad->file);
        FreeMem(wad->lumps, OTHER, sizeof(lumpinfo_t) * wad->header.numlumps);
        FreeMem(wad, OTHER, sizeof(*wad));
    }
}

static lumpinfo_t *
WADList_FindTexture(const wad_t *wadlist, const char *name)
{
    int i;
    const wad_t *wad;

    for (wad = wadlist; wad; wad = wad->next)
        for (i = 0; i < wad->header.numlumps; i++)
            if (!Q_strcasecmp(name, wad->lumps[i].name))
                return &wad->lumps[i];

    return NULL;
}

void
WADList_Process(const wad_t *wadlist)
{
    int i;
    lumpinfo_t *texture;
    dmiptexlump_t *miptexlump;
    struct lumpdata *texdata = &pWorldEnt->lumps[LUMP_TEXTURES];

    WADList_AddAnimationFrames(wadlist);

    /* Count space for miptex header/offsets */
    texdata->count = offsetof(dmiptexlump_t, dataofs[map.nummiptex()]);

    /* Count texture size.  Slower, but saves memory. */
    for (i = 0; i < map.nummiptex(); i++) {
        texture = WADList_FindTexture(wadlist, map.miptex[i]);
        if (texture) {
            if (options.fNoTextures)
                texdata->count += sizeof(dmiptex_t);
            else
                texdata->count += texture->disksize;
        }
    }

    /* Default texture data to store in worldmodel */
    texdata->data = AllocMem(BSP_TEX, texdata->count, true);
    miptexlump = (dmiptexlump_t *)texdata->data;
    miptexlump->nummiptex = map.nummiptex();

    WADList_LoadTextures(wadlist, miptexlump);

    /* Last pass, mark unfound textures as such */
    for (i = 0; i < map.nummiptex(); i++) {
        if (miptexlump->dataofs[i] == 0) {
            miptexlump->dataofs[i] = -1;
            Message(msgWarning, warnTextureNotFound, map.miptex[i]);
        }
    }
}

static void
WADList_LoadTextures(const wad_t *wadlist, dmiptexlump_t *lump)
{
    int i, size;
    byte *data;
    const wad_t *wad;
    struct lumpdata *texdata = &pWorldEnt->lumps[LUMP_TEXTURES];

    data = (byte *)&lump->dataofs[map.nummiptex()];

    for (i = 0; i < map.nummiptex(); i++) {
        if (lump->dataofs[i])
            continue;
        size = 0;
        for (wad = wadlist; wad; wad = wad->next) {
            size = WAD_LoadLump(wad, map.miptex[i], data);
            if (size)
                break;
        }
        if (!size)
            continue;
        if (data + size - (byte *)texdata->data > texdata->count)
            Error("Internal error: not enough texture memory allocated");
        lump->dataofs[i] = data - (byte *)lump;
        data += size;
    }
}


static int
WAD_LoadLump(const wad_t *wad, const char *name, byte *dest)
{
    int i;
    int size;

    for (i = 0; i < wad->header.numlumps; i++) {
        if (!Q_strcasecmp(name, wad->lumps[i].name)) {
            fseek(wad->file, wad->lumps[i].filepos, SEEK_SET);
            if (options.fNoTextures)
            {
                size = fread(dest, 1, sizeof(dmiptex_t), wad->file);
                if (size != sizeof(dmiptex_t))
                    Error("Failure reading from file");
                for (i = 0; i < MIPLEVELS; i++)
                    ((dmiptex_t*)dest)->offsets[i] = 0;
                return sizeof(dmiptex_t);
            }
            size = fread(dest, 1, wad->lumps[i].disksize, wad->file);
            if (size != wad->lumps[i].disksize)
                Error("Failure reading from file");
            return wad->lumps[i].disksize;
        }
    }

    return 0;
}

static void
WADList_AddAnimationFrames(const wad_t *wadlist)
{
    int oldcount, i, j;
    miptex_t name;

    oldcount = map.nummiptex();

    for (i = 0; i < oldcount; i++) {
        if (map.miptex[i][0] != '+')
            continue;
        snprintf(name, sizeof(name), "%s", map.miptex[i]);

        /* Search for all animations (0-9) and alt-animations (A-J) */
        for (j = 0; j < 20; j++) {
            name[1] = (j < 10) ? '0' + j : 'a' + j - 10;
            if (WADList_FindTexture(wadlist, name))
                FindMiptex(name);
        }
    }

    Message(msgStat, "%8d texture frames added", map.nummiptex() - oldcount);
}

const texture_t *WADList_GetTexture(const char *name)
{
    texture_t *tex;
    for (tex = textures; tex; tex = tex->next)
    {
        if (!strcmp(name, tex->name))
            return tex;
    }
    return NULL;
}
