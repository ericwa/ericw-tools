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

struct bspxbrushes
{
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

// structs shared between LIGHTGRID_OCTREE and LIGHTGRIDS

// helper container for different types of sample, shared by LIGHTGRID_OCTREE and LIGHTGRIDS.
// essentially an optional vector of 0..4 elements.
//
// each sample can either be:
// - occluded
// - or, have 0..4 styles
//
// note that "occluded" is distinct from no styles (which indicates black);
// occluded means "no sample available, don't interpolate from here"
template<class T>
struct sampleset_t
{
    // guaranteed to be zeroed beyond used_samples
    std::array<T, 4> samples_by_style{};

    // how many elements are inserted in samples_by_style.
    // can be 0 which means no contribution in any light styles (i.e., black).
    uint8_t used_samples = 0;

    // if true, all other fields are ignored. the sample is missing and shouldn't be interpolated with.
    bool occluded = false;

    // returns false if unsuccessful
    bool insert(const T &sample)
    {
        if (used_samples >= samples_by_style.size())
            return false;

        // store
        samples_by_style[used_samples++] = sample;
        return true;
    }

    const T *find_style(int style) const
    {
        if (occluded)
            return nullptr;

        for (int i = 0; i < used_samples; ++i) {
            if (samples_by_style[i].style == style) {
                return &samples_by_style[i];
            }
        }
        return nullptr;
    }
};

// header of LIGHTGRID_OCTREE, also used in LIGHTGRIDS
struct lightgrid_header_t
{
    qvec3f grid_dist;
    qvec3i grid_size;
    qvec3f grid_mins;
    uint8_t num_styles;
    uint32_t root_node;

    auto operator<=>(const lightgrid_header_t &) const = default;

    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s);
};

// a node in a lightgrid lump
struct lightgrid_node_t
{
    qvec3i division_point;
    std::array<uint32_t, 8> children;

    auto operator<=>(const lightgrid_node_t &) const = default;

    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s);
};

// LIGHTGRID_OCTREE BSPX lump

struct bspx_lightgrid_sample_t
{
    qvec3b color{};
    uint8_t style = 0;

    auto operator<=>(const bspx_lightgrid_sample_t &) const = default;
};

struct bspx_lightgrid_samples_t : public sampleset_t<bspx_lightgrid_sample_t>
{
    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s);
};

// a single leaf within LIGHTGRID_OCTREE, a literal block of 3D texture.
struct lightgrid_leaf_t
{
    qvec3i mins, size;
    std::vector<bspx_lightgrid_samples_t> samples; // [z][y][x]

    // x/y/z expected to be in [0 .. size[0/1/2])
    const bspx_lightgrid_samples_t &at(int x, int y, int z) const;
    qvec3f world_pos(const lightgrid_header_t &header, int x, int y, int z) const;

    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s);
};

// the overall LIGHTGRID_OCTREE bspx lump
struct lightgrid_octree_t
{
    lightgrid_header_t header;
    std::vector<lightgrid_node_t> nodes;
    std::vector<lightgrid_leaf_t> leafs;

    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s);
};

// LIGHTGRIDS lump (wip, non-final)

// clang-format off
static inline constexpr qvec3f BSPX_LIGHTGRIDS_NORMAL_ORDER[] = {
    { 1,  0,  0 },
    {-1,  0,  0 },

    { 0,  1,  0 },
    { 0, -1,  0 },

    { 0,  0,  1 },
    { 0,  0, -1 },
};
// clang-format on

// stores the colors on 6 sides of a cube, for a particular lightstyle
struct lightgrids_sample_t
{
    // order given by BSPX_LIGHTGRIDS_NORMAL_ORDER
    qvec3b colors[6] = {};
    // all styles 0-255 are allowed
    uint8_t style = {};

    auto operator<=>(const lightgrids_sample_t &) const = default;
};

// information for a single sample point in LIGHTGRIDS.
// contains incoming colors on a six-sided cube, for up to 4 lightstyles
struct lightgrids_sampleset_t : public sampleset_t<lightgrids_sample_t>
{
    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s);
};

// a single leaf within LIGHTGRIDS, a literal block of 3D texture.
struct lightgrids_leaf_t
{
    qvec3i mins = {};
    qvec3i size = {};
    std::vector<lightgrids_sampleset_t> samples; // [z][y][x]

    // x/y/z expected to be in [0 .. size[0/1/2])
    const lightgrids_sampleset_t &at(int x, int y, int z) const;
    qvec3f world_pos(const lightgrid_header_t &header, int x, int y, int z) const;

    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s);
};

// one lightgrid in a LIGHTGRIDS lump.
// similar to LIGHTGRID_OCTREE, but with directionality on a cube
struct subgrid_t
{
    lightgrid_header_t header;
    std::vector<lightgrid_node_t> nodes;
    std::vector<lightgrids_leaf_t> leafs;

    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s);
};

// the overall LIGHTGRIDS bspx lump
struct lightgrids_t
{
    std::vector<subgrid_t> subgrids;

    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s);
};

// BSPX data
