/*  Copyright (C) 1996-1997  Id Software, Inc.

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

#include <array>
#include <cstdint>
#include <common/aabb.hh>
#include <memory>

/* ========================================================================= */

struct bspx_header_t
{
    std::array<char, 4> id = {'B', 'S', 'P', 'X'}; //'BSPX'
    uint32_t numlumps;

    constexpr bspx_header_t(uint32_t numlumps) : numlumps(numlumps) { }

    auto stream_data() { return std::tie(id, numlumps); }
};

struct bspx_lump_t
{
    std::array<char, 24> lumpname{};
    uint32_t fileofs;
    uint32_t filelen;

    auto stream_data() { return std::tie(lumpname, fileofs, filelen); }
};

// BRUSHLIST BSPX lump
struct bspxbrushes_permodel
{
    int32_t ver;
    int32_t modelnum;
    int32_t numbrushes;
    int32_t numfaces;

    auto stream_data() { return std::tie(ver, modelnum, numbrushes, numfaces); }
};

struct bspxbrushes_perbrush
{
    aabb3f bounds;
    int16_t contents;
    uint16_t numfaces;

    auto stream_data() { return std::tie(bounds, contents, numfaces); }
};

using bspxbrushes_perface = qplane3f;

// BSPX data

struct bspxentry_t
{
    std::unique_ptr<uint8_t[]> lumpdata;
    size_t lumpsize;

    // bspxentry_t takes ownership over the pointer and will
    // free it automatically.
    inline bspxentry_t(void *lumpdata, size_t lumpsize) : lumpdata(reinterpret_cast<uint8_t *>(lumpdata)), lumpsize(lumpsize)
    {
    }
};