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

#include <cinttypes>
#include <array>
#include <vector>
#include <string>
#include <memory>
#include "qvec.hh"

constexpr int32_t MBSPIDENT = -1;

constexpr size_t MAX_MAP_HULLS_H2 = 8;

struct dmodelh2_t
{
    qvec3f mins;
    qvec3f maxs;
    qvec3f origin;
    std::array<int32_t, MAX_MAP_HULLS_H2> headnode; /* hexen2 only uses 6 */
    int32_t visleafs; /* not including the solid leaf 0 */
    int32_t firstface;
    int32_t numfaces;

    // serialize for streams
    auto stream_data() { return std::tie(mins, maxs, origin, headnode, visleafs, firstface, numfaces); }
};

enum vistype_t
{
    VIS_PVS,
    VIS_PHS
};

// the visibility lump consists of a header with a count, then
// byte offsets for the PVS and PHS of each cluster, then the raw
// compressed bit vectors.
struct mvis_t
{
    std::vector<std::array<int32_t, 2>> bit_offsets;
    std::vector<uint8_t> bits;

    inline size_t header_offset() const { return sizeof(int32_t) + (sizeof(int32_t) * bit_offsets.size() * 2); }

    // set a bit offset of the specified cluster/vistype *relative to the start of the bits array*
    // (after the header)
    inline void set_bit_offset(vistype_t type, size_t cluster, size_t offset)
    {
        bit_offsets[cluster][type] = offset + header_offset();
    }

    // fetch the bit offset of the specified cluster/vistype
    // relative to the start of the bits array
    inline int32_t get_bit_offset(vistype_t type, size_t cluster) const
    {
        return bit_offsets[cluster][type] - header_offset();
    }

    void resize(size_t numclusters) { bit_offsets.resize(numclusters); }

    void stream_read(std::istream &stream, const lump_t &lump)
    {
        int32_t numclusters;

        stream >= numclusters;

        resize(numclusters);

        // read cluster -> offset tables
        for (auto &bit_offset : bit_offsets)
            stream >= bit_offset;

        // pull in final bit set
        auto remaining = lump.filelen - (static_cast<int32_t>(stream.tellg()) - lump.fileofs);
        bits.resize(remaining);
        stream.read(reinterpret_cast<char *>(bits.data()), remaining);
    }

    void stream_write(std::ostream &stream) const
    {
        // no vis data
        if (!bit_offsets.size()) {
            return;
        }

        stream <= static_cast<int32_t>(bit_offsets.size());

        // write cluster -> offset tables
        for (auto &bit_offset : bit_offsets)
            stream <= bit_offset;

        // write bitset
        stream.write(reinterpret_cast<const char *>(bits.data()), bits.size());
    }
};

constexpr size_t MIPLEVELS = 4;

struct dmiptex_t
{
    std::array<char, 16> name;
    uint32_t width, height;
    std::array<int32_t, MIPLEVELS> offsets; /* four mip maps stored */

    auto stream_data() { return std::tie(name, width, height, offsets); }
};

// miptex in memory
struct miptex_t
{
    std::string name;
    uint32_t width, height;
    std::array<std::unique_ptr<uint8_t[]>, MIPLEVELS> data;

    static inline uint8_t *copy_bytes(const uint8_t *in, size_t size)
    {
        uint8_t *bytes = new uint8_t[size];
        memcpy(bytes, in, size);
        return bytes;
    }

    miptex_t() = default;
    miptex_t(const miptex_t &copy) : name(copy.name), width(copy.width), height(copy.height)
    {
        for (int32_t i = 0; i < data.size(); i++) {
            if (copy.data[i]) {
                data[i] = std::unique_ptr<uint8_t[]>(copy_bytes(copy.data[i].get(), (width >> i) * (height >> i)));
            }
        }
    }

    inline miptex_t &operator=(const miptex_t &copy)
    {
        name = copy.name;
        width = copy.width;
        height = copy.height;

        for (int32_t i = 0; i < data.size(); i++) {
            if (copy.data[i]) {
                data[i] = std::unique_ptr<uint8_t[]>(copy_bytes(copy.data[i].get(), (width >> i) * (height >> i)));
            }
        }

        return *this;
    }

    virtual ~miptex_t() { }

    virtual size_t stream_size() const { return sizeof(dmiptex_t) + width * height / 64 * 85; }

    virtual void stream_read(std::istream &stream)
    {
        auto start = stream.tellg();

        dmiptex_t dtex;
        stream >= dtex;

        name = dtex.name.data();

        width = dtex.width;
        height = dtex.height;

        for (size_t g = 0; g < MIPLEVELS; g++) {
            if (dtex.offsets[g] <= 0) {
                continue;
            }

            stream.seekg(static_cast<uint32_t>(start) + dtex.offsets[g]);
            const size_t num_bytes = (dtex.width >> g) * (dtex.height >> g);
            uint8_t *bytes = new uint8_t[num_bytes];
            stream.read(reinterpret_cast<char *>(bytes), num_bytes);
            data[g] = std::unique_ptr<uint8_t[]>(bytes);
        }
    }

    virtual void stream_write(std::ostream &stream) const
    {
        std::array<char, 16> as_array{};
        memcpy(as_array.data(), name.c_str(), name.size());

        stream <= as_array <= width <= height;

        uint32_t header_end = sizeof(dmiptex_t);

        for (size_t i = 0; i < MIPLEVELS; i++) {
            if (data[i] <= 0) {
                stream <= (uint32_t)0;
            } else {
                stream <= header_end;
                header_end += (width >> i) * (height >> i);
            }
        }

        for (size_t i = 0; i < MIPLEVELS; i++) {
            if (data[i]) {
                stream.write(reinterpret_cast<char *>(data[i].get()), (width >> i) * (height >> i));
            }
        }
    }
};

// Half Life miptex, which includes a palette
struct miptexhl_t : miptex_t
{
    std::vector<qvec3b> palette;

    miptexhl_t() = default;

    // convert miptex_t to miptexhl_t
    miptexhl_t(const miptex_t &copy) : miptex_t(copy) { }

    virtual size_t stream_size() const
    {
        return miptex_t::stream_size() + sizeof(uint16_t) + (palette.size() * sizeof(qvec3b));
    }

    virtual void stream_read(std::istream &stream)
    {
        miptex_t::stream_read(stream);

        uint16_t num_colors;
        stream >= num_colors;

        palette.resize(num_colors);
        stream.read(reinterpret_cast<char *>(palette.data()), palette.size());
    }

    virtual void stream_write(std::ostream &stream) const
    {
        miptex_t::stream_write(stream);

        stream <= static_cast<uint16_t>(palette.size());

        stream.write(reinterpret_cast<const char *>(palette.data()), palette.size());
    }
};

// structured miptex container lump
template<typename T>
struct dmiptexlump_t
{
    std::vector<T> textures;

    dmiptexlump_t() = default;

    // copy from a different lump type
    template<typename T2>
    dmiptexlump_t(const dmiptexlump_t<T2> &copy)
    {
        textures.reserve(copy.textures.size());

        for (auto &m : copy.textures) {
            textures.emplace_back(m);
        }
    }

    void stream_read(std::istream &stream, const lump_t &lump)
    {
        int32_t nummiptex;
        stream >= nummiptex;

        for (size_t i = 0; i < nummiptex; i++) {
            int32_t mipofs;

            stream >= mipofs;

            miptex_t &tex = textures.emplace_back();

            if (mipofs < 0)
                continue;

            auto pos = stream.tellg();

            stream.seekg(lump.fileofs + mipofs);

            tex.stream_read(stream);

            stream.seekg(pos);
        }
    }

    void stream_write(std::ostream &stream) const
    {
        auto p = (size_t)stream.tellp();

        stream <= static_cast<int32_t>(textures.size());

        const size_t header_size = sizeof(int32_t) + (sizeof(int32_t) * textures.size());

        size_t miptex_offset = 0;

        for (auto &texture : textures) {
            if (!texture.name[0]) {
                stream <= static_cast<int32_t>(-1);
                continue;
            }
            stream <= static_cast<int32_t>(header_size + miptex_offset);

            miptex_offset += texture.stream_size();

            if ((p + miptex_offset) % 4) {
                miptex_offset += 4 - ((p + miptex_offset) % 4);
            }
        }

        for (auto &texture : textures) {
            if (texture.name[0]) {
                if (stream.tellp() % 4) {
                    stream <= padding_n(4 - (stream.tellp() % 4));
                }
                texture.stream_write(stream);
            }
        }
    }
};

// 0-2 are axial planes
// 3-5 are non-axial planes snapped to the nearest
enum class plane_type_t
{
    PLANE_INVALID = -1,
    PLANE_X = 0,
    PLANE_Y = 1,
    PLANE_Z = 2,
    PLANE_ANYX = 3,
    PLANE_ANYY = 4,
    PLANE_ANYZ = 5,
};

struct dplane_t : qplane3f
{
    int32_t type;

    [[nodiscard]] constexpr dplane_t operator-() const { return {qplane3f::operator-(), type}; }

    // serialize for streams
    auto stream_data() { return std::tie(normal, dist, type); }

    // optimized case
    template<typename T>
    inline T distance_to_fast(const qvec<T, 3> &point) const
    {
        switch (static_cast<plane_type_t>(type)) {
            case plane_type_t::PLANE_X: return point[0] - dist;
            case plane_type_t::PLANE_Y: return point[1] - dist;
            case plane_type_t::PLANE_Z: return point[2] - dist;
            default: {
                return qplane3f::distance_to(point);
            }
        }
    }
};

struct bsp2_dnode_t
{
    int32_t planenum;
    std::array<int32_t, 2> children; /* negative numbers are -(leafs+1), not nodes */
    qvec3f mins; /* for sphere culling */
    qvec3f maxs;
    uint32_t firstface;
    uint32_t numfaces; /* counting both sides */

    // serialize for streams
    auto stream_data() { return std::tie(planenum, children, mins, maxs, firstface, numfaces); }
};

struct mtexinfo_t
{
    texvecf vecs; // [s/t][xyz offset]
    surfflags_t flags; // native miptex flags + extended flags

    // q1 only
    int32_t miptex;

    // q2 only
    int32_t value; // light emission, etc
    std::array<char, 32> texture; // texture name (textures/*.wal)
    int32_t nexttexinfo = -1; // for animations, -1 = end of chain
};

constexpr size_t MAXLIGHTMAPS = 4;

struct mface_t
{
    int64_t planenum;
    int32_t side; // if true, the face is on the back side of the plane
    int32_t firstedge; /* we must support > 64k edges */
    int32_t numedges;
    int32_t texinfo;

    /* lighting info */
    std::array<uint8_t, MAXLIGHTMAPS> styles;
    int32_t lightofs; /* start of [numstyles*surfsize] samples */

    // serialize for streams
    auto stream_data() { return std::tie(planenum, side, firstedge, numedges, texinfo, styles, lightofs); }
};

/*
 * Note that children are interpreted as unsigned values now, so that we can
 * handle > 32k clipnodes. Values > 0xFFF0 can be assumed to be CONTENTS
 * values and can be read as the signed value to be compatible with the above
 * (i.e. simply subtract 65536).
 */
struct bsp2_dclipnode_t
{
    int32_t planenum;
    std::array<int32_t, 2> children; /* negative numbers are contents */

    // serialize for streams
    auto stream_data() { return std::tie(planenum, children); }
};

using bsp2_dedge_t = std::array<uint32_t, 2>; /* vertex numbers */

/*
 * leaf 0 is the generic CONTENTS_SOLID leaf, used for all solid areas (except Q2)
 * all other leafs need visibility info
 */
/* Ambient Sounds */
enum ambient_type_t : uint8_t
{
    AMBIENT_WATER,
    AMBIENT_SKY,
    AMBIENT_SLIME,
    AMBIENT_LAVA,

    NUM_AMBIENTS = 4
};

struct mleaf_t
{
    // bsp2_dleaf_t
    int32_t contents;
    int32_t visofs; /* -1 = no visibility info; Q1 only! */
    qvec3f mins; /* for frustum culling     */
    qvec3f maxs;
    uint32_t firstmarksurface;
    uint32_t nummarksurfaces;
    std::array<uint8_t, NUM_AMBIENTS> ambient_level;

    // q2 extras
    int32_t cluster;
    int32_t area;
    uint32_t firstleafbrush;
    uint32_t numleafbrushes;
};

struct darea_t
{
    int32_t numareaportals;
    int32_t firstareaportal;

    // serialize for streams
    auto stream_data() { return std::tie(numareaportals, firstareaportal); }
    auto tuple() const { return std::tie(numareaportals, firstareaportal); }

    // comparison operator for tests
    bool operator==(const darea_t &other) const { return tuple() == other.tuple(); }
};

// each area has a list of portals that lead into other areas
// when portals are closed, other areas may not be visible or
// hearable even if the vis info says that it should be
struct dareaportal_t
{
    int32_t portalnum;
    int32_t otherarea;

    // serialize for streams
    auto stream_data() { return std::tie(portalnum, otherarea); }
    auto tuple() const { return std::tie(portalnum, otherarea); }

    // comparison operator for tests
    bool operator==(const dareaportal_t &other) const { return tuple() == other.tuple(); }
};

struct dbrush_t
{
    int32_t firstside;
    int32_t numsides;
    int32_t contents;

    // serialize for streams
    auto stream_data() { return std::tie(firstside, numsides, contents); }
};

struct q2_dbrushside_qbism_t
{
    uint32_t planenum; // facing out of the leaf
    int32_t texinfo;

    // serialize for streams
    auto stream_data() { return std::tie(planenum, texinfo); }
};

struct bspversion_t;

 // "generic" bsp - superset of all other supported types
struct mbsp_t
{
    // the BSP version that we came from, if any
    const bspversion_t *loadversion;

    std::vector<dmodelh2_t> dmodels;
    mvis_t dvis;
    std::vector<uint8_t> dlightdata;
    dmiptexlump_t<miptexhl_t> dtex;
    std::string dentdata;
    std::vector<mleaf_t> dleafs;
    std::vector<dplane_t> dplanes;
    std::vector<qvec3f> dvertexes;
    std::vector<bsp2_dnode_t> dnodes;
    std::vector<mtexinfo_t> texinfo;
    std::vector<mface_t> dfaces;
    std::vector<bsp2_dclipnode_t> dclipnodes;
    std::vector<bsp2_dedge_t> dedges;
    std::vector<uint32_t> dleaffaces;
    std::vector<uint32_t> dleafbrushes;
    std::vector<int32_t> dsurfedges;
    std::vector<darea_t> dareas;
    std::vector<dareaportal_t> dareaportals;
    std::vector<dbrush_t> dbrushes;
    std::vector<q2_dbrushside_qbism_t> dbrushsides;
};

extern const bspversion_t bspver_generic;