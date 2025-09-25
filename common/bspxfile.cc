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

#include "common/log.hh"

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

// bspxbrushes_perbrush

void bspxbrushes_perbrush::stream_write(std::ostream &s) const
{
    s <= bounds;
    s <= contents;
    s <= static_cast<uint16_t>(faces.size());

    for (auto &face : faces) {
        s <= face;
    }
}

void bspxbrushes_perbrush::stream_read(std::istream &s)
{
    s >= bounds;
    s >= contents;

    uint16_t numfaces = 0;
    s >= numfaces;

    faces.resize(numfaces);

    for (auto &face : faces) {
        s >= face;
    }
}

// bspxbrushes_permodel

void bspxbrushes_permodel::stream_write(std::ostream &s) const
{
    s <= ver;
    s <= modelnum;
    s <= static_cast<int32_t>(brushes.size());
    // count faces (ignore numfaces)
    int32_t faces = 0;
    for (auto &brush : brushes) {
        faces += static_cast<int32_t>(brush.faces.size());
    }
    s <= faces;

    // next serialize all of the brushes
    for (auto &brush : brushes) {
        s <= brush;
    }
}

void bspxbrushes_permodel::stream_read(std::istream &s)
{
    s >= ver;
    if (!s) {
        // we need to handle end-of-stream due to the bspx lump containing an unknown number
        // of bspxbrushes_permodel objects
        return;
    }

    s >= modelnum;

    int32_t numbrushes;
    s >= numbrushes;
    s >= numfaces;

    brushes.resize(numbrushes);
    for (auto &brush : brushes) {
        s >= brush;
    }
}

// bspxbrushes

void bspxbrushes::stream_write(std::ostream &s) const
{
    for (auto &model : models) {
        s <= model;
    }
}

void bspxbrushes::stream_read(std::istream &s)
{
    models.clear();

    while (true) {
        bspxbrushes_permodel model;
        s >= model;

        if (!s) {
            break;
        }

        models.push_back(std::move(model));
    }
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

// LIGHTGRID_OCTREE

// lightgrid_header_t

void lightgrid_header_t::stream_write(std::ostream &s) const
{
    s <= std::tie(grid_dist, grid_size, grid_mins, num_styles, root_node);
}

void lightgrid_header_t::stream_read(std::istream &s)
{
    s >= std::tie(grid_dist, grid_size, grid_mins, num_styles, root_node);
}

// lightgrid_node_t

void lightgrid_node_t::stream_write(std::ostream &s) const
{
    s <= std::tie(division_point, children);
}

void lightgrid_node_t::stream_read(std::istream &s)
{
    s >= std::tie(division_point, children);
}

// bspx_lightgrid_samples_t

void bspx_lightgrid_samples_t::stream_write(std::ostream &s) const
{
    if (occluded) {
        // occluded marker
        s <= static_cast<uint8_t>(0xff);
    } else {
        s <= used_samples;

        for (int j = 0; j < static_cast<int>(used_samples); ++j) {
            s <= samples_by_style[j].style;
            s <= samples_by_style[j].color;
        }
    }
}

void bspx_lightgrid_samples_t::stream_read(std::istream &s)
{
    uint8_t used_styles_in;
    s >= used_styles_in;

    if (used_styles_in == 0xff) {
        occluded = true;
        // point is occluded, no color data follows
        return;
    }

    // point is unoccluded, 0 or more style/color pairs follow
    for (int j = 0; j < used_styles_in; ++j) {
        bspx_lightgrid_sample_t sample;
        s >= sample.style;
        s >= sample.color;

        if (!insert(sample)) {
            logging::print(
                "WARNING: LIGHTGRID_OCTREE exceeds implementation limit of {} styles\n", samples_by_style.size());
        }
    }
}

// lightgrid_leaf_t

const bspx_lightgrid_samples_t &lightgrid_leaf_t::at(int x, int y, int z) const
{
    int idx = (size[0] * size[1] * z) + (size[0] * y) + x;
    Q_assert(samples.size() == (size[0] * size[1] * size[2]));
    return samples.at(idx);
}

qvec3f lightgrid_leaf_t::world_pos(const lightgrid_header_t &header, int x, int y, int z) const
{
    qvec3i grid_coord = mins + qvec3i(x, y, z);

    return header.grid_mins + (qvec3f(grid_coord) * header.grid_dist);
}

void lightgrid_leaf_t::stream_write(std::ostream &s) const
{
    s <= std::tie(mins, size);

    for (int z = 0; z < size[2]; ++z) {
        for (int y = 0; y < size[1]; ++y) {
            for (int x = 0; x < size[0]; ++x) {
                const bspx_lightgrid_samples_t &samp = at(x, y, z);

                s <= samp;
            }
        }
    }
}

void lightgrid_leaf_t::stream_read(std::istream &s)
{
    s >= std::tie(mins, size);

    for (int z = 0; z < size[2]; ++z) {
        for (int y = 0; y < size[1]; ++y) {
            for (int x = 0; x < size[0]; ++x) {
                bspx_lightgrid_samples_t &samp = samples.emplace_back();
                samp.stream_read(s);
            }
        }
    }
}

// lightgrid_octree_t

void lightgrid_octree_t::stream_write(std::ostream &s) const
{
    s <= header;

    s <= static_cast<uint32_t>(nodes.size());
    for (const auto &node : nodes)
        s <= node;

    s <= static_cast<uint32_t>(leafs.size());
    for (const auto &leaf : leafs)
        s <= leaf;
}

void lightgrid_octree_t::stream_read(std::istream &s)
{
    s >= header;

    uint32_t num_nodes;
    s >= num_nodes;
    for (int i = 0; i < num_nodes; ++i) {
        lightgrid_node_t &node = nodes.emplace_back();
        s >= node;
    }

    uint32_t num_leafs;
    s >= num_leafs;
    for (int i = 0; i < num_leafs; ++i) {
        lightgrid_leaf_t &leaf = leafs.emplace_back();
        s >= leaf;
    }
}

// LIGHTGRIDS lump

// lightgrids_sampleset_t

void lightgrids_sampleset_t::stream_write(std::ostream &s) const
{
    if (occluded) {
        // occluded marker
        s <= static_cast<uint8_t>(0xff);
        return;
    }

    s <= used_samples;

    for (int j = 0; j < used_samples; ++j) {
        const lightgrids_sample_t &sample = samples_by_style[j];
        s <= sample.style;

        // determine the flags
        uint8_t flags = 0;
        for (int side_index = 0; side_index < 6; ++side_index) {
            if (sample.colors[side_index] != qvec3b(0, 0, 0)) {
                flags |= (1 << side_index);
            }
        }

        // write the flags, then write the corresponding sides' colors out
        s <= flags;

        for (int side_index = 0; side_index < 6; ++side_index) {
            if (flags & (1 << side_index)) {
                s <= sample.colors[side_index];
            }
        }
    }
}

void lightgrids_sampleset_t::stream_read(std::istream &s)
{
    uint8_t used_styles_in;
    s >= used_styles_in;

    if (used_styles_in == 0xff) {
        occluded = true;
        // point is occluded, no color data follows
        return;
    }

    // point is unoccluded, `used_styles_in` cubes follow
    for (int j = 0; j < used_styles_in; ++j) {
        lightgrids_sample_t sample{};
        s >= sample.style;

        // there are 0 to 6 color samples, for the faces of a cube.
        // they're always given in the following order:
        //
        // index:        0,  1,  2,  3,  4,  5
        // cube normal: +x, -x, +y, -y, +z, -z
        //
        // if `flags & (1 << index)` is set, it means that index is included.
        // if they're omitted, it means the cube is black on that side.
        //
        // e.g. 0b101 means we'd read the +x color, then the +y color, and assume
        // all other faces of the cube are black.
        uint8_t flags;
        s >= flags;

        for (int side_index = 0; side_index < 6; ++side_index) {
            if (flags & (1 << side_index)) {
                s >= sample.colors[side_index];
            }
        }

        if (!insert(sample)) {
            logging::print("WARNING: LIGHTGRIDS exceeds implementation limit of {} styles\n", samples_by_style.size());
        }
    }
}

// lightgrids_leaf_t

const lightgrids_sampleset_t &lightgrids_leaf_t::at(int x, int y, int z) const
{
    int idx = (size[0] * size[1] * z) + (size[0] * y) + x;
    Q_assert(samples.size() == (size[0] * size[1] * size[2]));
    return samples.at(idx);
}

qvec3f lightgrids_leaf_t::world_pos(const lightgrid_header_t &header, int x, int y, int z) const
{
    qvec3i grid_coord = mins + qvec3i(x, y, z);

    return header.grid_mins + (qvec3f(grid_coord) * header.grid_dist);
}

void lightgrids_leaf_t::stream_write(std::ostream &s) const
{
    s <= std::tie(mins, size);

    // compute max_styles
    uint8_t max_styles = 0;
    for (auto &sampleset : samples) {
        max_styles = std::max(max_styles, sampleset.used_samples);
    }
    s <= max_styles;

    // validate number of samples
    const int expected_samples = size[0] * size[1] * size[2];
    if (expected_samples != samples.size()) {
        throw;
    }

    // write samples
    for (auto &sampleset : samples) {
        s <= sampleset;
    }
}

void lightgrids_leaf_t::stream_read(std::istream &s)
{
    s >= std::tie(mins, size);

    uint8_t max_styles; // unused
    s >= max_styles;

    samples.resize(size[0] * size[1] * size[2]);
    for (lightgrids_sampleset_t &sampleset : samples) {
        s >= sampleset;
    }
}

// subgrid_t

void subgrid_t::stream_write(std::ostream &s) const
{
    s <= header;

    s <= static_cast<uint32_t>(nodes.size());
    for (const lightgrid_node_t &node : nodes)
        s <= node;

    s <= static_cast<uint32_t>(leafs.size());
    for (const lightgrids_leaf_t &leaf : leafs)
        s <= leaf;
}

void subgrid_t::stream_read(std::istream &s)
{
    s >= header;

    uint32_t num_nodes;
    s >= num_nodes;
    for (int i = 0; i < num_nodes; ++i) {
        auto &node = nodes.emplace_back();
        s >= node;
    }

    uint32_t num_leafs;
    s >= num_leafs;
    for (int i = 0; i < num_leafs; ++i) {
        auto &leaf = leafs.emplace_back();
        s >= leaf;
    }
}

// lightgrids_t

void lightgrids_t::stream_write(std::ostream &s) const
{
    for (const auto &lightgrid : subgrids) {
        std::streampos begin_pos = s.tellp();

        // write a placeholder for the size, we'll overwrite after.
        s <= static_cast<uint32_t>(0);

        // write the lightgrid itself
        s <= lightgrid;

        std::streampos end_pos = s.tellp();
        std::streampos lightgrid_size = (end_pos - begin_pos) - 4;

        // seek back to start and overwrite the placeholder with the actual size
        s.seekp(begin_pos);
        s <= static_cast<uint32_t>(lightgrid_size);

        s.seekp(end_pos);
    }
}

void lightgrids_t::stream_read(std::istream &s)
{
    while (true) {
        uint32_t lightgrid_size_bytes;
        s >= lightgrid_size_bytes;

        if (!s) {
            // not an error, we just hit eof
            break;
        }

        std::streampos begin_pos = s.tellg();

        // read the lightgrid
        auto &lightgrid = subgrids.emplace_back();
        s >= lightgrid;

        // validate that the provided size matches what was read
        std::streampos end_pos = s.tellg();

        if ((end_pos - begin_pos) != lightgrid_size_bytes) {
            logging::print("ERROR: bad LIGHTGRIDS lump\n");
            break;
        }
    }
}
