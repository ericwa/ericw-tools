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

#pragma once

#include <unordered_map>
#include <vector>
#include <list>
#include <fstream>
#include "common/cmdlib.hh"
#include "common/fs.hh"

// Texture data stored for quick searching
struct texture_t
{
    std::string name;
    int width, height;
};

// WAD Format
struct wadinfo_t
{
    std::array<char, 4> identification; // should be WAD2
    int numlumps;
    int infotableofs;

    auto stream_data() { return std::tie(identification, numlumps, infotableofs); }
};

struct lumpinfo_t
{
    int filepos;
    int disksize;
    int size; // uncompressed
    char type;
    char compression;
    char pad1, pad2;
    std::array<char, 16> name; // must be null terminated

    auto stream_data() { return std::tie(filepos, disksize, size, type, compression, pad1, pad2, name); }
};

struct wad_t
{
    wadinfo_t header;
    int version;
    std::unordered_map<std::string, lumpinfo_t, case_insensitive_hash, case_insensitive_equal> lumps;
    std::unordered_map<std::string, texture_t, case_insensitive_hash, case_insensitive_equal> textures;
    std::ifstream file;
};

void WADList_Init(const std::string_view &wadstring);
void WADList_Process();
const texture_t *WADList_GetTexture(const char *name);
// for getting a texture width/height
