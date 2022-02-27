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

#include <cstring>
#include <string>

#include <qbsp/qbsp.hh>
#include <qbsp/wad.hh>

uint8_t thepalette[768] = // Quake palette
    {0, 0, 0, 15, 15, 15, 31, 31, 31, 47, 47, 47, 63, 63, 63, 75, 75, 75, 91, 91, 91, 107, 107, 107, 123, 123, 123, 139,
        139, 139, 155, 155, 155, 171, 171, 171, 187, 187, 187, 203, 203, 203, 219, 219, 219, 235, 235, 235, 15, 11, 7,
        23, 15, 11, 31, 23, 11, 39, 27, 15, 47, 35, 19, 55, 43, 23, 63, 47, 23, 75, 55, 27, 83, 59, 27, 91, 67, 31, 99,
        75, 31, 107, 83, 31, 115, 87, 31, 123, 95, 35, 131, 103, 35, 143, 111, 35, 11, 11, 15, 19, 19, 27, 27, 27, 39,
        39, 39, 51, 47, 47, 63, 55, 55, 75, 63, 63, 87, 71, 71, 103, 79, 79, 115, 91, 91, 127, 99, 99, 139, 107, 107,
        151, 115, 115, 163, 123, 123, 175, 131, 131, 187, 139, 139, 203, 0, 0, 0, 7, 7, 0, 11, 11, 0, 19, 19, 0, 27, 27,
        0, 35, 35, 0, 43, 43, 7, 47, 47, 7, 55, 55, 7, 63, 63, 7, 71, 71, 7, 75, 75, 11, 83, 83, 11, 91, 91, 11, 99, 99,
        11, 107, 107, 15, 7, 0, 0, 15, 0, 0, 23, 0, 0, 31, 0, 0, 39, 0, 0, 47, 0, 0, 55, 0, 0, 63, 0, 0, 71, 0, 0, 79,
        0, 0, 87, 0, 0, 95, 0, 0, 103, 0, 0, 111, 0, 0, 119, 0, 0, 127, 0, 0, 19, 19, 0, 27, 27, 0, 35, 35, 0, 47, 43,
        0, 55, 47, 0, 67, 55, 0, 75, 59, 7, 87, 67, 7, 95, 71, 7, 107, 75, 11, 119, 83, 15, 131, 87, 19, 139, 91, 19,
        151, 95, 27, 163, 99, 31, 175, 103, 35, 35, 19, 7, 47, 23, 11, 59, 31, 15, 75, 35, 19, 87, 43, 23, 99, 47, 31,
        115, 55, 35, 127, 59, 43, 143, 67, 51, 159, 79, 51, 175, 99, 47, 191, 119, 47, 207, 143, 43, 223, 171, 39, 239,
        203, 31, 255, 243, 27, 11, 7, 0, 27, 19, 0, 43, 35, 15, 55, 43, 19, 71, 51, 27, 83, 55, 35, 99, 63, 43, 111, 71,
        51, 127, 83, 63, 139, 95, 71, 155, 107, 83, 167, 123, 95, 183, 135, 107, 195, 147, 123, 211, 163, 139, 227, 179,
        151, 171, 139, 163, 159, 127, 151, 147, 115, 135, 139, 103, 123, 127, 91, 111, 119, 83, 99, 107, 75, 87, 95, 63,
        75, 87, 55, 67, 75, 47, 55, 67, 39, 47, 55, 31, 35, 43, 23, 27, 35, 19, 19, 23, 11, 11, 15, 7, 7, 187, 115, 159,
        175, 107, 143, 163, 95, 131, 151, 87, 119, 139, 79, 107, 127, 75, 95, 115, 67, 83, 107, 59, 75, 95, 51, 63, 83,
        43, 55, 71, 35, 43, 59, 31, 35, 47, 23, 27, 35, 19, 19, 23, 11, 11, 15, 7, 7, 219, 195, 187, 203, 179, 167, 191,
        163, 155, 175, 151, 139, 163, 135, 123, 151, 123, 111, 135, 111, 95, 123, 99, 83, 107, 87, 71, 95, 75, 59, 83,
        63, 51, 67, 51, 39, 55, 43, 31, 39, 31, 23, 27, 19, 15, 15, 11, 7, 111, 131, 123, 103, 123, 111, 95, 115, 103,
        87, 107, 95, 79, 99, 87, 71, 91, 79, 63, 83, 71, 55, 75, 63, 47, 67, 55, 43, 59, 47, 35, 51, 39, 31, 43, 31, 23,
        35, 23, 15, 27, 19, 11, 19, 11, 7, 11, 7, 255, 243, 27, 239, 223, 23, 219, 203, 19, 203, 183, 15, 187, 167, 15,
        171, 151, 11, 155, 131, 7, 139, 115, 7, 123, 99, 7, 107, 83, 0, 91, 71, 0, 75, 55, 0, 59, 43, 0, 43, 31, 0, 27,
        15, 0, 11, 7, 0, 0, 0, 255, 11, 11, 239, 19, 19, 223, 27, 27, 207, 35, 35, 191, 43, 43, 175, 47, 47, 159, 47,
        47, 143, 47, 47, 127, 47, 47, 111, 47, 47, 95, 43, 43, 79, 35, 35, 63, 27, 27, 47, 19, 19, 31, 11, 11, 15, 43,
        0, 0, 59, 0, 0, 75, 7, 0, 95, 7, 0, 111, 15, 0, 127, 23, 7, 147, 31, 7, 163, 39, 11, 183, 51, 15, 195, 75, 27,
        207, 99, 43, 219, 127, 59, 227, 151, 79, 231, 171, 95, 239, 191, 119, 247, 211, 139, 167, 123, 59, 183, 155, 55,
        199, 195, 55, 231, 227, 87, 127, 191, 255, 171, 231, 255, 215, 255, 255, 103, 0, 0, 139, 0, 0, 179, 0, 0, 215,
        0, 0, 255, 0, 0, 255, 243, 147, 255, 247, 199, 255, 255, 255, 159, 91, 83};

static bool WAD_LoadInfo(wad_t &wad, bool external)
{
    wadinfo_t *hdr = &wad.header;
    int i, len;
    dmiptex_t miptex;

    external |= options.notextures.value();

    len = SafeRead(wad.file, hdr, sizeof(wadinfo_t));
    if (len != sizeof(wadinfo_t))
        return false;

    wad.version = 0;
    if (!strncmp(hdr->identification, "WAD2", 4))
        wad.version = 2;
    else if (!strncmp(hdr->identification, "WAD3", 4))
        wad.version = 3;
    if (!wad.version)
        return false;

    SafeSeek(wad.file, hdr->infotableofs, SEEK_SET);
    wad.lumps.reserve(wad.header.numlumps);

    /* Get the dimensions and make a texture_t */
    for (i = 0; i < wad.header.numlumps; i++) {
        lumpinfo_t lump;

        len = SafeRead(wad.file, &lump, sizeof(lump));
        if (len != sizeof(lump))
            return false;

        auto restore_pos = SafeTell(wad.file);

        SafeSeek(wad.file, lump.filepos, SEEK_SET);
        len = SafeRead(wad.file, &miptex, sizeof(miptex));

        if (len == sizeof(miptex)) {
            int w = LittleLong(miptex.width);
            int h = LittleLong(miptex.height);
            lump.size =
                sizeof(miptex) + (w >> 0) * (h >> 0) + (w >> 1) * (h >> 1) + (w >> 2) * (h >> 2) + (w >> 3) * (h >> 3);
            if (options.target_game->id == GAME_HALF_LIFE)
                lump.size += 2 + 3 * 256; // palette size+palette data
            lump.size = (lump.size + 3) & ~3; // keep things aligned if we can.

            texture_t tex;
            memcpy(tex.name, miptex.name.data(), 16);
            tex.name[15] = '\0';
            tex.width = miptex.width;
            tex.height = miptex.height;
            wad.textures.insert({tex.name, tex});

            // if we're not going to embed it into the bsp, set its size now so we know how much to actually store.
            if (external)
                lump.size = lump.disksize = sizeof(dmiptex_t);

            // fmt::print("Created texture_t {} {} {}\n", tex->name, tex->width, tex->height);
            wad.lumps.insert({tex.name, lump});
        } else {
            lump.size = 0;
            wad.lumps.insert({lump.name, lump});
        }

        SafeSeek(wad.file, restore_pos, SEEK_SET);
    }

    return true;
}

static void WADList_OpenWad(const std::filesystem::path &fpath, bool external)
{
    wad_t wad;

    wad.file = SafeOpenRead(fpath);

    if (wad.file) {
        if (options.fVerbose)
            LogPrint("Opened WAD: {}\n", fpath);

        if (WAD_LoadInfo(wad, external)) {
            wadlist.emplace_front(std::move(wad));
            return;
        }

        LogPrint("WARNING: {} isn't a wadfile\n", fpath);
        wad.file.reset();
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
        std::string fpathstr;
        fpathstr.resize(fpathLen);
        memcpy(&fpathstr[0], fname, fpathLen);
        std::filesystem::path fpath = fpathstr;

        if (options.wadpaths.pathsValue().empty() || fpath.is_absolute()) {
            WADList_OpenWad(fpath, false);
        } else {
            for (auto &wadpath : options.wadpaths.pathsValue()) {
                WADList_OpenWad(wadpath.path / fpath, wadpath.external);
            }
        }

        pos++;
    }
}

static const lumpinfo_t *WADList_FindTexture(const std::string &name)
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

static bool WAD_LoadLump(const wad_t &wad, const char *name, miptexhl_t &dest)
{
    auto it = wad.lumps.find(name);

    if (it == wad.lumps.end()) {
        it = wad.lumps.find("*slime0");
        return false;
    }

    auto &lump = it->second;

    SafeSeek(wad.file, lump.filepos, SEEK_SET);

    if (lump.disksize < sizeof(dmiptex_t)) {
        LogPrint("Wad texture {} is invalid", name);
        return false;
    }

    std::unique_ptr<uint8_t[]> buffer = std::make_unique<uint8_t[]>(lump.disksize);
    size_t size = SafeRead(wad.file, buffer.get(), lump.disksize);

    if (size != lump.disksize)
        FError("Failure reading from file");

    memstream stream(buffer.get(), size);

    stream >> endianness<std::endian::little>;

    dmiptex_t header;
    stream >= header;

    dest.name = header.name.data();
    dest.width = header.width;
    dest.height = header.height;

    // only header, no actual texture data
    if (lump.disksize == sizeof(dmiptex_t)) {
        return true;
    }

    if (lump.size != lump.disksize) {
        LogPrint("Texture {} is {} bytes in wad, packed to {} bytes in bsp\n", name, lump.disksize, lump.size);
    }

    for (size_t i = 0; i < MIPLEVELS; i++) {
        stream.seekg(header.offsets[i]);
        size_t mipsize = (header.width >> i) * (header.height >> i);
        dest.data[i] = std::make_unique<uint8_t[]>(mipsize);
        stream.read(reinterpret_cast<char *>(dest.data[i].get()), mipsize);
    }

    // we're right after offsets[3] now, so see if palette is here
    uint16_t palette_size;

    if (stream >= palette_size) {
        dest.palette.resize(palette_size);

        if (!stream.read(reinterpret_cast<char *>(dest.palette.data()), palette_size * 3)) {
            FError("Invalid palette in wad texture {}\n", name);
        }
    } else {
        dest.palette.resize(sizeof(thepalette));
        memcpy(dest.palette.data(), thepalette, sizeof(thepalette));
    }

    return true;
}

static void WADList_LoadTextures()
{
    for (size_t i = 0; i < map.nummiptex(); i++) {
        // already loaded?
        if (map.bsp.dtex.textures[i].data[0])
            continue;

        for (auto &wad : wadlist) {
            if (WAD_LoadLump(wad, map.miptexTextureName(i).c_str(), map.bsp.dtex.textures[i]))
                break;
        }
    }
}

static void WADList_AddAnimationFrames()
{
    size_t oldcount = map.nummiptex();

    for (size_t i = 0; i < oldcount; i++) {
        const std::string &existing_name = map.miptexTextureName(i);
        if (existing_name[0] != '+' && (options.target_game->id != GAME_HALF_LIFE || existing_name[0] != '-'))
            continue;
        std::string name = map.miptexTextureName(i);

        /* Search for all animations (0-9) and alt-animations (A-J) */
        for (size_t j = 0; j < 20; j++) {
            name[1] = (j < 10) ? '0' + j : 'a' + j - 10;
            if (WADList_FindTexture(name))
                FindMiptex(name.c_str());
        }
    }

    LogPrint(LOG_STAT, "     {:8} texture frames added\n", map.nummiptex() - oldcount);
}

void WADList_Process()
{
    WADList_AddAnimationFrames();

    // Q2 doesn't use texdata
    if (options.target_game->id == GAME_QUAKE_II) {
        return;
    }

    /* Default texture data to store in worldmodel */
    map.bsp.dtex.textures.resize(map.nummiptex());

    WADList_LoadTextures();

    /* Last pass, mark unfound textures as such */
    for (size_t i = 0; i < map.nummiptex(); i++) {
        if (!map.bsp.dtex.textures[i].data[0]) {
            LogPrint("WARNING: Texture {} not found\n", map.miptexTextureName(i));
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