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
#include <string>

#include <qbsp/qbsp.hh>
#include <qbsp/wad.hh>

static void WADList_LoadTextures(const wad_t *wadlist, dmiptexlump_t *lump);
static int WAD_LoadLump(const wad_t *wad, const char *name, uint8_t *dest);
static void WADList_AddAnimationFrames(const wad_t *wadlist);

static texture_t *textures;

uint8_t thepalette[768] = // Quake palette
{
    0,0,0,15,15,15,31,31,31,47,47,47,63,63,63,75,75,75,91,91,91,107,107,107,123,123,123,139,139,139,155,155,155,171,171,171,187,187,187,203,203,203,219,219,219,235,235,235,15,11,7,23,15,11,31,23,11,39,27,15,47,35,19,55,43,23,63,47,23,75,55,27,83,59,27,91,67,31,99,75,31,107,83,31,115,87,31,123,95,35,131,103,35,143,111,35,11,11,15,19,19,27,27,27,39,39,39,51,47,47,63,55,55,75,63,63,87,71,71,103,79,79,115,91,91,127,99,99,
    139,107,107,151,115,115,163,123,123,175,131,131,187,139,139,203,0,0,0,7,7,0,11,11,0,19,19,0,27,27,0,35,35,0,43,43,7,47,47,7,55,55,7,63,63,7,71,71,7,75,75,11,83,83,11,91,91,11,99,99,11,107,107,15,7,0,0,15,0,0,23,0,0,31,0,0,39,0,0,47,0,0,55,0,0,63,0,0,71,0,0,79,0,0,87,0,0,95,0,0,103,0,0,111,0,0,119,0,0,127,0,0,19,19,0,27,27,0,35,35,0,47,43,0,55,47,0,67,
    55,0,75,59,7,87,67,7,95,71,7,107,75,11,119,83,15,131,87,19,139,91,19,151,95,27,163,99,31,175,103,35,35,19,7,47,23,11,59,31,15,75,35,19,87,43,23,99,47,31,115,55,35,127,59,43,143,67,51,159,79,51,175,99,47,191,119,47,207,143,43,223,171,39,239,203,31,255,243,27,11,7,0,27,19,0,43,35,15,55,43,19,71,51,27,83,55,35,99,63,43,111,71,51,127,83,63,139,95,71,155,107,83,167,123,95,183,135,107,195,147,123,211,163,139,227,179,151,
    171,139,163,159,127,151,147,115,135,139,103,123,127,91,111,119,83,99,107,75,87,95,63,75,87,55,67,75,47,55,67,39,47,55,31,35,43,23,27,35,19,19,23,11,11,15,7,7,187,115,159,175,107,143,163,95,131,151,87,119,139,79,107,127,75,95,115,67,83,107,59,75,95,51,63,83,43,55,71,35,43,59,31,35,47,23,27,35,19,19,23,11,11,15,7,7,219,195,187,203,179,167,191,163,155,175,151,139,163,135,123,151,123,111,135,111,95,123,99,83,107,87,71,95,75,59,83,63,
    51,67,51,39,55,43,31,39,31,23,27,19,15,15,11,7,111,131,123,103,123,111,95,115,103,87,107,95,79,99,87,71,91,79,63,83,71,55,75,63,47,67,55,43,59,47,35,51,39,31,43,31,23,35,23,15,27,19,11,19,11,7,11,7,255,243,27,239,223,23,219,203,19,203,183,15,187,167,15,171,151,11,155,131,7,139,115,7,123,99,7,107,83,0,91,71,0,75,55,0,59,43,0,43,31,0,27,15,0,11,7,0,0,0,255,11,11,239,19,19,223,27,27,207,35,35,191,43,
    43,175,47,47,159,47,47,143,47,47,127,47,47,111,47,47,95,43,43,79,35,35,63,27,27,47,19,19,31,11,11,15,43,0,0,59,0,0,75,7,0,95,7,0,111,15,0,127,23,7,147,31,7,163,39,11,183,51,15,195,75,27,207,99,43,219,127,59,227,151,79,231,171,95,239,191,119,247,211,139,167,123,59,183,155,55,199,195,55,231,227,87,127,191,255,171,231,255,215,255,255,103,0,0,139,0,0,179,0,0,215,0,0,255,0,0,255,243,147,255,247,199,255,255,255,159,91,83
};

static bool
WAD_LoadInfo(wad_t *wad, bool external)
{
    wadinfo_t *hdr = &wad->header;
    int i, len, lumpinfosize;
    dmiptex_t miptex;
    texture_t *tex;

    external |= options.fNoTextures;

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
            int w = LittleLong(miptex.width);
            int h = LittleLong(miptex.height);
            wad->lumps[i].size = sizeof(miptex) + (w>>0)*(h>>0) + (w>>1)*(h>>1) + (w>>2)*(h>>2) + (w>>3)*(h>>3);
            if (options.BSPVersion == BSPHLVERSION)
                wad->lumps[i].size += 2+3*256;    //palette size+palette data
            wad->lumps[i].size = (wad->lumps[i].size+3) & ~3;    //keep things aligned if we can.

            tex = (texture_t *)AllocMem(OTHER, sizeof(texture_t), true);
            tex->next = textures;
            textures = tex;
            memcpy(tex->name, miptex.name, 16);
            tex->name[15] = '\0';
            tex->width = miptex.width;
            tex->height = miptex.height;

            //if we're not going to embed it into the bsp, set its size now so we know how much to actually store.
            if (external)
                wad->lumps[i].size = wad->lumps[i].disksize = sizeof(dmiptex_t);

            //printf("Created texture_t %s %d %d\n", tex->name, tex->width, tex->height);
        }
        else
            wad->lumps[i].size = 0;
    }

    return true;
}

wad_t *WADList_AddWad(const char *fpath, bool external, wad_t *current_wadlist)
{
    wad_t wad = {0};
    
    wad.file = fopen(fpath, "rb");
    if (wad.file) {
        if (options.fVerbose)
            Message(msgLiteral, "Opened WAD: %s\n", fpath);
        if (WAD_LoadInfo(&wad, external)) {
            wad_t *newwad = (wad_t *)AllocMem(OTHER, sizeof(wad), true);
            memcpy(newwad, &wad, sizeof(wad));
            newwad->next = current_wadlist;
            
            // FIXME: leaves file open?
            // (currently needed so that mips can be loaded later, as needed)
            
            return newwad;
        } else {
            Message(msgWarning, warnNotWad, fpath);
            fclose(wad.file);
        }
    }
    return current_wadlist;
}

wad_t *
WADList_Init(const char *wadstring)
{
    if (!wadstring || !wadstring[0])
        return nullptr;

    wad_t *wadlist = nullptr;
    const int len = strlen(wadstring);
    const char *pos = wadstring;
    while (pos - wadstring < len) {
        // split string by ';' and copy the current component into fpath
        const char *const fname = pos;
        while (*pos && *pos != ';')
            pos++;

        const size_t fpathLen = pos - fname;
        std::string fpath;
        fpath.resize(fpathLen);
        memcpy(&fpath[0], fname, fpathLen);

        if (options.wadPathsVec.empty() || IsAbsolutePath(fpath.c_str())) {
            wadlist = WADList_AddWad(fpath.c_str(), false, wadlist);
        } else {
            for (const options_t::wadpath& wadpath : options.wadPathsVec) {
                const std::string fullPath = wadpath.path + "/" + fpath;
                wadlist = WADList_AddWad(fullPath.c_str(), wadpath.external, wadlist);
            }
        }

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
    struct lumpdata *texdata = &pWorldEnt()->lumps[LUMP_TEXTURES];

    WADList_AddAnimationFrames(wadlist);

    /* Count space for miptex header/offsets */
    texdata->count = offsetof(dmiptexlump_t, dataofs[0]) + (map.nummiptex() * sizeof(uint32_t));

    /* Count texture size.  Slower, but saves memory. */
    for (i = 0; i < map.nummiptex(); i++) {
        texture = WADList_FindTexture(wadlist, map.miptex.at(i).c_str());
        if (texture) {
            texdata->count += texture->size;
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
            Message(msgWarning, warnTextureNotFound, map.miptex.at(i).c_str());
        }
    }
}

static void
WADList_LoadTextures(const wad_t *wadlist, dmiptexlump_t *lump)
{
    int i, size;
    uint8_t *data;
    const wad_t *wad;
    struct lumpdata *texdata = &pWorldEnt()->lumps[LUMP_TEXTURES];

    data = (uint8_t *)&lump->dataofs[map.nummiptex()];

    for (i = 0; i < map.nummiptex(); i++) {
        if (lump->dataofs[i])
            continue;
        size = 0;
        for (wad = wadlist; wad; wad = wad->next) {
            size = WAD_LoadLump(wad, map.miptex.at(i).c_str(), data);
            if (size)
                break;
        }
        if (!size)
            continue;
        if (data + size - (uint8_t *)texdata->data > texdata->count)
            Error("Internal error: not enough texture memory allocated");
        lump->dataofs[i] = data - (uint8_t *)lump;
        data += size;
    }
}


static int
WAD_LoadLump(const wad_t *wad, const char *name, uint8_t *dest)
{
    int i;
    int size;

    for (i = 0; i < wad->header.numlumps; i++) {
        if (!Q_strcasecmp(name, wad->lumps[i].name)) {
            fseek(wad->file, wad->lumps[i].filepos, SEEK_SET);
            if (wad->lumps[i].disksize == sizeof(dmiptex_t))
            {
                size = fread(dest, 1, sizeof(dmiptex_t), wad->file);
                if (size != sizeof(dmiptex_t))
                    Error("Failure reading from file");
                for (i = 0; i < MIPLEVELS; i++)
                    ((dmiptex_t*)dest)->offsets[i] = 0;
                return sizeof(dmiptex_t);
            }

            if (wad->lumps[i].size != wad->lumps[i].disksize)
            {
            logprint("Texture %s is %i bytes in wad, packed to %i bytes in bsp\n", name, wad->lumps[i].disksize, wad->lumps[i].size);
                std::vector<uint8_t> data(wad->lumps[i].disksize);
                size = fread(data.data(), 1, wad->lumps[i].disksize, wad->file);
                if (size != wad->lumps[i].disksize)
                    Error("Failure reading from file");
                auto out = (dmiptex_t *)dest;
                auto in = (dmiptex_t *)data.data();
                *out = *in;
                out->offsets[0] = sizeof(*out);
                out->offsets[1] = out->offsets[0] + (in->width>>0)*(in->height>>0);
                out->offsets[2] = out->offsets[1] + (in->width>>1)*(in->height>>1);
                out->offsets[3] = out->offsets[2] + (in->width>>2)*(in->height>>2);
                auto palofs     = out->offsets[3] + (in->width>>3)*(in->height>>3);
                memcpy(dest+out->offsets[0], data.data()+(in->offsets[0]), (in->width>>0)*(in->height>>0));
                memcpy(dest+out->offsets[1], data.data()+(in->offsets[1]), (in->width>>1)*(in->height>>1));
                memcpy(dest+out->offsets[2], data.data()+(in->offsets[2]), (in->width>>2)*(in->height>>2));
                memcpy(dest+out->offsets[3], data.data()+(in->offsets[3]), (in->width>>3)*(in->height>>3));

                if (options.BSPVersion == BSPHLVERSION)
                {    //palette size. 256 in little endian.
                    dest[palofs+0] = ((256>>0)&0xff);
                    dest[palofs+1] = ((256>>8)&0xff);

                    //now the palette
                    if (wad->version == 3)
                        memcpy(dest+palofs+2, data.data()+(in->offsets[3]+(in->width>>3)*(in->height>>3)+2), 3*256);
                    else
                        memcpy(dest+palofs+2, thepalette, 3*256);    //FIXME: quake palette or something.
                }
            }
            else
            {
                size = fread(dest, 1, wad->lumps[i].disksize, wad->file);
                if (size != wad->lumps[i].disksize)
                    Error("Failure reading from file");
            }
            return wad->lumps[i].size;
        }
    }

    return 0;
}

static void
WADList_AddAnimationFrames(const wad_t *wadlist)
{
    int oldcount, i, j;

    oldcount = map.nummiptex();

    for (i = 0; i < oldcount; i++) {
        if (map.miptex.at(i)[0] != '+' && (options.BSPVersion!=BSPHLVERSION||map.miptex.at(i)[0] != '-'))
            continue;
        std::string name = map.miptex.at(i);

        /* Search for all animations (0-9) and alt-animations (A-J) */
        for (j = 0; j < 20; j++) {
            name[1] = (j < 10) ? '0' + j : 'a' + j - 10;
            if (WADList_FindTexture(wadlist, name.c_str()))
                FindMiptex(name.c_str());
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
