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
WAD_LoadInfo(wad_t &wad, bool external)
{
    wadinfo_t *hdr = &wad.header;
    int i, len;
    dmiptex_t miptex;

    external |= options.fNoTextures;

    len = fread(hdr, 1, sizeof(wadinfo_t), wad.file);
    if (len != sizeof(wadinfo_t))
        return false;

    wad.version = 0;
    if (!strncmp(hdr->identification, "WAD2", 4))
        wad.version = 2;
    else if (!strncmp(hdr->identification, "WAD3", 4))
        wad.version = 3;
    if (!wad.version)
        return false;

    fseek(wad.file, hdr->infotableofs, SEEK_SET);
    wad.lumps.reserve(wad.header.numlumps);

    /* Get the dimensions and make a texture_t */
    for (i = 0; i < wad.header.numlumps; i++) {
        lumpinfo_t lump;

        len = fread(&lump, 1, sizeof(lump), wad.file);
        if (len != sizeof(lump))
            return false;

        auto restore_pos = ftell(wad.file);

        fseek(wad.file, lump.filepos, SEEK_SET);
        len = fread(&miptex, 1, sizeof(miptex), wad.file);

        if (len == sizeof(miptex))
        {
            int w = LittleLong(miptex.width);
            int h = LittleLong(miptex.height);
            lump.size = sizeof(miptex) + (w>>0)*(h>>0) + (w>>1)*(h>>1) + (w>>2)*(h>>2) + (w>>3)*(h>>3);
            if (options.target_game->id == GAME_HALF_LIFE)
                lump.size += 2+3*256;    //palette size+palette data
            lump.size = (lump.size+3) & ~3;    //keep things aligned if we can.

            texture_t tex;
            memcpy(tex.name, miptex.name, 16);
            tex.name[15] = '\0';
            tex.width = miptex.width;
            tex.height = miptex.height;
            wad.textures.insert({ tex.name, tex });

            //if we're not going to embed it into the bsp, set its size now so we know how much to actually store.
            if (external)
                lump.size = lump.disksize = sizeof(dmiptex_t);

            //printf("Created texture_t %s %d %d\n", tex->name, tex->width, tex->height);
        }
        else
            lump.size = 0;

        fseek(wad.file, restore_pos, SEEK_SET);

        wad.lumps.insert({ lump.name, lump });
    }

    return true;
}

static void WADList_OpenWad(const char *fpath, bool external)
{
    wad_t wad;
    
    wad.file = fopen(fpath, "rb");

    if (wad.file) {
        if (options.fVerbose)
            Message(msgLiteral, "Opened WAD: %s\n", fpath);

        if (WAD_LoadInfo(wad, external)) {
            wadlist.push_front(wad);
            wad.file = nullptr; // wadlist now owns this file handle
            return;
        }

        Message(msgWarning, warnNotWad, fpath);
        fclose(wad.file);
    } else {
        // Message?
    }
}

void WADList_Init(const char *wadstring)
{
    if (!wadstring || !wadstring[0])
        return;

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
            WADList_OpenWad(fpath.c_str(), false);
        } else {
            for (const options_t::wadpath& wadpath : options.wadPathsVec) {
                const std::string fullPath = wadpath.path + "/" + fpath;
                WADList_OpenWad(fullPath.c_str(), wadpath.external);
            }
        }

        pos++;
    }
}

static const lumpinfo_t *
WADList_FindTexture(const char *name)
{
    for (auto &wad : wadlist) {
        auto it = wad.lumps.find(name);

        if (it == wad.lumps.end()) {
            continue;
        }

        return &it->second;
    }

    return NULL;
}

static void WAD_SanitizeName(dmiptex_t* miptex)
{
    bool reached_null = false;

    for (int i = 0; i < sizeof(miptex->name); ++i) {
        if (!miptex->name[i]) {
            reached_null = true;
        }

        // fill the remainder of the buffer with 0 bytes
        if (reached_null) {
            miptex->name[i] = '\0';
        }
    }
}

static int
WAD_LoadLump(const wad_t &wad, const char *name, uint8_t *dest)
{
    int i;
    int size;

    auto it = wad.lumps.find(name);

    if (it == wad.lumps.end()) {
        return 0;
    }

    auto &lump = it->second;

    fseek(wad.file, lump.filepos, SEEK_SET);

    if (lump.disksize == sizeof(dmiptex_t)) {
        size = fread(dest, 1, sizeof(dmiptex_t), wad.file);
        if (size != sizeof(dmiptex_t))
            Error("Failure reading from file");

        WAD_SanitizeName((dmiptex_t*)dest);
        for (i = 0; i < MIPLEVELS; i++)
            ((dmiptex_t*)dest)->offsets[i] = 0;
        return sizeof(dmiptex_t);
    }

    if (lump.size != lump.disksize) {
        logprint("Texture %s is %i bytes in wad, packed to %i bytes in bsp\n", name, lump.disksize, lump.size);
        std::vector<uint8_t> data(lump.disksize);
        size = fread(data.data(), 1, lump.disksize, wad.file);
        if (size != lump.disksize)
            Error("Failure reading from file");

        WAD_SanitizeName((dmiptex_t*)dest);
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

        if (options.target_game->id == GAME_HALF_LIFE)
        {    //palette size. 256 in little endian.
            dest[palofs+0] = ((256>>0)&0xff);
            dest[palofs+1] = ((256>>8)&0xff);

            //now the palette
            if (wad.version == 3)
                memcpy(dest+palofs+2, data.data()+(in->offsets[3]+(in->width>>3)*(in->height>>3)+2), 3*256);
            else
                memcpy(dest+palofs+2, thepalette, 3*256);    //FIXME: quake palette or something.
        }
    }
    else
    {
        size = fread(dest, 1, lump.disksize, wad.file);
        if (size != lump.disksize)
            Error("Failure reading from file");

        WAD_SanitizeName((dmiptex_t*)dest);
    }
    return lump.size;
}

static void
WADList_LoadTextures(dmiptexlump_t *lump)
{
    int i, size;
    uint8_t *data;

    data = (uint8_t *)&lump->dataofs[map.nummiptex()];

    for (i = 0; i < map.nummiptex(); i++) {
        if (lump->dataofs[i])
            continue;
        size = 0;
        for (auto &wad : wadlist) {
            size = WAD_LoadLump(wad, map.miptexTextureName(i).c_str(), data);
            if (size)
                break;
        }
        if (!size)
            continue;
        if (data + size - (uint8_t *)map.exported_texdata.data() > map.exported_texdata.size())
            Error("Internal error: not enough texture memory allocated");
        lump->dataofs[i] = data - (uint8_t *)lump;
        data += size;
    }
}

static void
WADList_AddAnimationFrames()
{
    int oldcount, i, j;

    oldcount = map.nummiptex();

    for (i = 0; i < oldcount; i++) {
        const std::string &existing_name = map.miptexTextureName(i);
        if (existing_name[0] != '+' && (options.target_version != &bspver_hl || existing_name[0] != '-'))
            continue;
        std::string name = map.miptexTextureName(i);

        /* Search for all animations (0-9) and alt-animations (A-J) */
        for (j = 0; j < 20; j++) {
            name[1] = (j < 10) ? '0' + j : 'a' + j - 10;
            if (WADList_FindTexture(name.c_str()))
                FindMiptex(name.c_str());
        }
    }

    Message(msgStat, "%8d texture frames added", map.nummiptex() - oldcount);
}

void
WADList_Process()
{
    int i;
    const lumpinfo_t *texture;
    dmiptexlump_t *miptexlump;
    
    WADList_AddAnimationFrames();

    /* Count space for miptex header/offsets */
    size_t texdatasize = offsetof(dmiptexlump_t, dataofs[0]) + (map.nummiptex() * sizeof(uint32_t));

    /* Count texture size.  Slower, but saves memory. */
    for (i = 0; i < map.nummiptex(); i++) {
        texture = WADList_FindTexture(map.miptexTextureName(i).c_str());
        if (texture) {
            texdatasize += texture->size;
        }
    }

    // Q2 doesn't use texdata
    if (options.target_game->id == GAME_QUAKE_II) {
        return;
    }
    
    /* Default texture data to store in worldmodel */
    map.exported_texdata = std::string(texdatasize, '\0');
    miptexlump = (dmiptexlump_t *)map.exported_texdata.data();
    miptexlump->nummiptex = map.nummiptex();

    WADList_LoadTextures(miptexlump);

    /* Last pass, mark unfound textures as such */
    for (i = 0; i < map.nummiptex(); i++) {
        if (miptexlump->dataofs[i] == 0) {
            miptexlump->dataofs[i] = -1;
            Message(msgWarning, warnTextureNotFound, map.miptexTextureName(i).c_str());
        }
    }
}

const texture_t *WADList_GetTexture(const char *name)
{
    for (auto &wad : wadlist) {
        auto it = wad.textures.find(name);

        if (it == wad.textures.end()) {
            return nullptr;
        }

        return &it->second;
    }

    return nullptr;
}

std::list<wad_t> wadlist;