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

#include <common/bspxfile.hh>

#include <common/cmdlib.hh>

// bspx_header_t

bspx_header_t::bspx_header_t(uint32_t numlumps)
    : numlumps(numlumps)
{
}

void bspx_header_t::stream_write(std::ostream &s) const
{
    s <= std::tie(id, numlumps);
}

void bspx_header_t::stream_read(std::istream &s)
{
    s >= std::tie(id, numlumps);
}

// bspx_lump_t

void bspx_lump_t::stream_write(std::ostream &s) const
{
    s <= std::tie(lumpname, fileofs, filelen);
}

void bspx_lump_t::stream_read(std::istream &s)
{
    s >= std::tie(lumpname, fileofs, filelen);
}

// bspxbrushes_permodel

void bspxbrushes_permodel::stream_write(std::ostream &s) const
{
    s <= std::tie(ver, modelnum, numbrushes, numfaces);
}

void bspxbrushes_permodel::stream_read(std::istream &s)
{
    s >= std::tie(ver, modelnum, numbrushes, numfaces);
}

// bspxbrushes_perbrush

void bspxbrushes_perbrush::stream_write(std::ostream &s) const
{
    s <= std::tie(bounds, contents, numfaces);
}

void bspxbrushes_perbrush::stream_read(std::istream &s)
{
    s >= std::tie(bounds, contents, numfaces);
}

// bspxfacenormals_per_vert

void bspxfacenormals_per_vert::stream_write(std::ostream &s) const
{
    s <= std::tie(normal, tangent, bitangent);
}

void bspxfacenormals_per_vert::stream_read(std::istream &s)
{
    s >= std::tie(normal, tangent, bitangent);
}

// bspxfacenormals_per_face

void bspxfacenormals_per_face::stream_write(std::ostream &s) const
{
    for (const auto &v : per_vert) {
        s <= v;
    }
}

void bspxfacenormals_per_face::stream_read(std::istream &s, const mface_t &f)
{
    for (int i = 0; i < f.numedges; ++i) {
        bspxfacenormals_per_vert v;
        s >= v;
        per_vert.push_back(v);
    }
}

// bspxfacenormals

void bspxfacenormals::stream_write(std::ostream &s) const
{
    // write the table of normals
    s <= static_cast<uint32_t>(normals.size());

    for (const qvec3f &v : normals) {
        s <= v;
    }

    // write the per-face, per-vertex indices into the prior table
    for (const auto &f : per_face) {
        s <= f;
    }
}

void bspxfacenormals::stream_read(std::istream &s, const mbsp_t &bsp)
{
    normals.clear();
    per_face.clear();

    // read normals table
    uint32_t size;
    s >= size;

    for (uint32_t i = 0; i < size; ++i) {
        qvec3f v;
        s >= v;
        normals.push_back(v);
    }

    // read, based on the faces in the provided bsp
    for (const auto &f : bsp.dfaces) {
        bspxfacenormals_per_face pf;
        pf.stream_read(s, f);
        per_face.push_back(pf);
    }
}

// bspx_decoupled_lm_perface

void bspx_decoupled_lm_perface::stream_write(std::ostream &s) const
{
    s <= std::tie(lmwidth, lmheight, offset, world_to_lm_space);
}

void bspx_decoupled_lm_perface::stream_read(std::istream &s)
{
    s >= std::tie(lmwidth, lmheight, offset, world_to_lm_space);
}
