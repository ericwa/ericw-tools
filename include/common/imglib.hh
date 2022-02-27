/*  Copyright (C) 1996-1997  Id Software, Inc.
Copyright (C) 2017 Eric Wasylishen

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

#include <common/cmdlib.hh>
#include <common/bspfile.hh>
#include <common/qvec.hh>
#include <common/fs.hh>

namespace img
{
extern std::vector<qvec3b> palette;

// Palette
void init_palette(const gamedef_t *game);

struct texture_meta
{
    std::string name;
    uint32_t width, height;

    // This member is only set before insertion into the table
    // and not calculated by individual load functions.
    qvec3b averageColor;

    // Q2/WAL only
    surfflags_t flags;
    contentflags_t contents;
    int32_t value;
    std::string animation;
};

struct texture
{
    texture_meta meta{};
    std::vector<qvec4b> pixels;
};

extern std::unordered_map<std::string, texture, case_insensitive_hash, case_insensitive_equal> textures;

qvec3b calculate_average(const std::vector<qvec4b> &pixels);

const texture *find(const std::string &str);

// Load wal
std::optional<texture> load_wal(const std::string &name, const fs::data &file, bool metaOnly);

// Pull in texture data from the BSP into the textures map
void load_textures(const mbsp_t *bsp);
}; // namespace img
