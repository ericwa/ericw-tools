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
#include <iosfwd>
#include <cstdint>
#include <common/aabb.hh>
#include <memory>
#include <common/bspfile.hh>

/* ========================================================================= */

struct bspx_header_t
{
    std::array<char, 4> id = {'B', 'S', 'P', 'X'}; //'BSPX'
    uint32_t numlumps;

    bspx_header_t() = default;
    bspx_header_t(uint32_t numlumps);

    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s);
};

struct bspx_lump_t
{
    std::array<char, 24> lumpname{};
    uint32_t fileofs;
    uint32_t filelen;

    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s);
};

// BRUSHLIST BSPX lump
using bspxbrushes_perface = qplane3f;

struct bspxbrushes_perbrush
{
    aabb3f bounds;
    int16_t contents;
    // non-axial faces only
    std::vector<bspxbrushes_perface> faces;

    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s);
};

struct bspxbrushes_permodel
{
    int32_t ver;
    int32_t modelnum;
    std::vector<bspxbrushes_perbrush> brushes;
    // ignored when writing
    int32_t numfaces;

    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s);
};

struct bspxbrushes {
    std::vector<bspxbrushes_permodel> models;

    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s);
};

struct bspxfacenormals_per_vert
{
    // these are all indices into bspxfacenormals::normals
    uint32_t normal;
    uint32_t tangent;
    uint32_t bitangent;

    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s);
};

struct bspxfacenormals_per_face
{
    // the size of this must match the corresponding face's numedges
    std::vector<bspxfacenormals_per_vert> per_vert;

    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s, const mface_t &face);
};

// FACENORMALS BSPX lump
struct bspxfacenormals
{
    // we can't just store 1 normal per bsp.dvertexes;
    // to allow smoothing groups to work, a single vertex may have different normals
    // when it's used by different faces.

    // table of vectors referenced by the per-face data below,
    // these could be used as normals, tangents, or bitangents.
    // the size of this table doesn't correspond to bsp.dvertexes.
    std::vector<qvec3f> normals;

    // the size of this must match bsp.dfaces
    std::vector<bspxfacenormals_per_face> per_face;

    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s, const mbsp_t &bsp);
};

// DECOUPLED_LM BSPX lump
struct bspx_decoupled_lm_perface
{
    uint16_t lmwidth; // pixels
    uint16_t lmheight; // pixels
    // offset into dlightdata lump.
    // start of numstyles (from face struct) * (lmwidth * lmheight) samples
    int32_t offset;

    // 2 rows * 4 column matrix, stored in row major order
    // this is a world -> lightmap space transformation matrix
    texvecf world_to_lm_space;

    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s);
};

// BSPX data
