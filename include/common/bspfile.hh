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

#include <cstdint>
#include <array>
#include <tuple>
#include <variant>
#include <vector>
#include <unordered_map>

#include <common/cmdlib.hh>
#include <common/log.hh>
#include <common/qvec.hh>
#include <common/aabb.hh>

/* upper design bounds */

#define MAX_MAP_HULLS_Q1 4
#define MAX_MAP_HULLS_H2 8

#define MAX_MAP_MODELS 256
#define MAX_MAP_BRUSHES 4096
#define MAX_MAP_PLANES 16384
#define MAX_MAP_NODES 32767 /* negative shorts are contents */
#define MAX_MAP_CLIPNODES 65520 /* = 0xfff0; larger are contents */
#define MAX_MAP_LEAFS 32767 /* BSP file format limitation */
#define MAX_MAP_VERTS 65535
#define MAX_MAP_FACES 65535
#define MAX_MAP_MARKSURFACES 65535
#define MAX_MAP_TEXINFO 8192
#define MAX_MAP_EDGES 256000
#define MAX_MAP_SURFEDGES 512000
#define MAX_MAP_MIPTEX 0x0800000
#define MAX_MAP_LIGHTING 0x8000000
#define MAX_MAP_VISIBILITY 0x8000000

/* key / value pair sizes */
#define MAX_ENT_KEY 32
#define MAX_ENT_VALUE 1024

#define NO_VERSION -1

#define BSPVERSION 29
#define BSP2RMQVERSION (('B' << 24) | ('S' << 16) | ('P' << 8) | '2')
#define BSP2VERSION ('B' | ('S' << 8) | ('P' << 16) | ('2' << 24))
#define BSPHLVERSION 30 // 24bit lighting, and private palettes in the textures lump.
#define Q2_BSPIDENT (('P' << 24) + ('S' << 16) + ('B' << 8) + 'I')
#define Q2_BSPVERSION 38
#define Q2_QBISMIDENT (('P' << 24) + ('S' << 16) + ('B' << 8) + 'Q')

struct lump_t
{
    int32_t fileofs;
    int32_t filelen;

    auto stream_data() { return std::tie(fileofs, filelen); }
};

#define LUMP_ENTITIES 0
#define LUMP_PLANES 1
#define LUMP_TEXTURES 2
#define LUMP_VERTEXES 3
#define LUMP_VISIBILITY 4
#define LUMP_NODES 5
#define LUMP_TEXINFO 6
#define LUMP_FACES 7
#define LUMP_LIGHTING 8
#define LUMP_CLIPNODES 9
#define LUMP_LEAFS 10
#define LUMP_MARKSURFACES 11
#define LUMP_EDGES 12
#define LUMP_SURFEDGES 13
#define LUMP_MODELS 14

#define BSP_LUMPS 15

#define Q2_LUMP_ENTITIES 0
#define Q2_LUMP_PLANES 1
#define Q2_LUMP_VERTEXES 2
#define Q2_LUMP_VISIBILITY 3
#define Q2_LUMP_NODES 4
#define Q2_LUMP_TEXINFO 5
#define Q2_LUMP_FACES 6
#define Q2_LUMP_LIGHTING 7
#define Q2_LUMP_LEAFS 8
#define Q2_LUMP_LEAFFACES 9
#define Q2_LUMP_LEAFBRUSHES 10
#define Q2_LUMP_EDGES 11
#define Q2_LUMP_SURFEDGES 12
#define Q2_LUMP_MODELS 13
#define Q2_LUMP_BRUSHES 14
#define Q2_LUMP_BRUSHSIDES 15
#define Q2_LUMP_POP 16
#define Q2_LUMP_AREAS 17
#define Q2_LUMP_AREAPORTALS 18

#define Q2_HEADER_LUMPS 19

struct bspx_header_t
{
    std::array<char, 4> id = {'B', 'S', 'P', 'X'}; //'BSPX'
    uint32_t numlumps;

    bspx_header_t(uint32_t numlumps) : numlumps(numlumps) { }

    auto stream_data() { return std::tie(id, numlumps); }
};

struct bspx_lump_t
{
    std::array<char, 24> lumpname{};
    uint32_t fileofs;
    uint32_t filelen;

    auto stream_data() { return std::tie(lumpname, fileofs, filelen); }
};

struct lumpspec_t
{
    const char *name;
    size_t size;
};

// helper functions to quickly numerically cast mins/maxs
// and floor/ceil them in the case of float -> integral
template<typename T, typename F>
inline qvec<T, 3> aabb_mins_cast(const qvec<F, 3> &f, const char *overflow_message = "mins")
{
    if constexpr (std::is_floating_point_v<F> && !std::is_floating_point_v<T>)
        return {numeric_cast<T>(floor(f[0]), overflow_message), numeric_cast<T>(floor(f[1]), overflow_message),
            numeric_cast<T>(floor(f[2]), overflow_message)};
    else
        return {numeric_cast<T>(f[0], overflow_message), numeric_cast<T>(f[1], overflow_message),
            numeric_cast<T>(f[2], overflow_message)};
}

template<typename T, typename F>
inline qvec<T, 3> aabb_maxs_cast(const qvec<F, 3> &f, const char *overflow_message = "maxs")
{
    if constexpr (std::is_floating_point_v<F> && !std::is_floating_point_v<T>)
        return {numeric_cast<T>(ceil(f[0]), overflow_message), numeric_cast<T>(ceil(f[1]), overflow_message),
            numeric_cast<T>(ceil(f[2]), overflow_message)};
    else
        return {numeric_cast<T>(f[0], overflow_message), numeric_cast<T>(f[1], overflow_message),
            numeric_cast<T>(f[2], overflow_message)};
}

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

// shortcut template to trim (& convert) std::arrays
// between two lengths
template<typename ADest, typename ASrc>
constexpr ADest array_cast(const ASrc &src, const char *overflow_message = "src")
{
    ADest dest{};

    for (size_t i = 0; i < std::min(dest.size(), src.size()); i++) {
        if constexpr (std::is_arithmetic_v<typename ADest::value_type> &&
                      std::is_arithmetic_v<typename ASrc::value_type>)
            dest[i] = numeric_cast<typename ADest::value_type>(src[i], overflow_message);
        else
            dest[i] = static_cast<typename ADest::value_type>(src[i]);
    }

    return dest;
}

struct dmodelq1_t
{
    qvec3f mins;
    qvec3f maxs;
    qvec3f origin;
    std::array<int32_t, MAX_MAP_HULLS_Q1> headnode; /* 4 for backward compat, only 3 hulls exist */
    int32_t visleafs; /* not including the solid leaf 0 */
    int32_t firstface;
    int32_t numfaces;

    dmodelq1_t() = default;

    // convert from mbsp_t
    dmodelq1_t(const dmodelh2_t &model)
        : mins(model.mins), maxs(model.maxs), origin(model.origin),
          headnode(array_cast<decltype(headnode)>(model.headnode, "dmodelh2_t::headnode")), visleafs(model.visleafs),
          firstface(model.firstface), numfaces(model.numfaces)
    {
    }

    // convert to mbsp_t
    operator dmodelh2_t() const
    {
        return {mins, maxs, origin, array_cast<decltype(dmodelh2_t::headnode)>(headnode, "dmodelh2_t::headnode"),
            visleafs, firstface, numfaces};
    }

    // serialize for streams
    auto stream_data() { return std::tie(mins, maxs, origin, headnode, visleafs, firstface, numfaces); }
};

struct q2_dmodel_t
{
    qvec3f mins;
    qvec3f maxs;
    qvec3f origin; // for sounds or lights
    int32_t headnode;
    int32_t firstface;
    int32_t numfaces; // submodels just draw faces
                      // without walking the bsp tree

    q2_dmodel_t() = default;

    // convert from mbsp_t
    q2_dmodel_t(const dmodelh2_t &model)
        : mins(model.mins), maxs(model.maxs), origin(model.origin), headnode(model.headnode[0]),
          firstface(model.firstface), numfaces(model.numfaces)
    {
    }

    // convert to mbsp_t
    operator dmodelh2_t() const
    {
        return {mins, maxs, origin, {headnode},
            0, // visleafs
            firstface, numfaces};
    }

    // serialize for streams
    auto stream_data() { return std::tie(mins, maxs, origin, headnode, firstface, numfaces); }
};

// structured data from BSP
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
                    constexpr const char pad[4]{};
                    stream.write(pad, 4 - (stream.tellp() % 4));
                }
                texture.stream_write(stream);
            }
        }
    }
};

/* 0-2 are axial planes */
#define PLANE_X 0
#define PLANE_Y 1
#define PLANE_Z 2

/* 3-5 are non-axial planes snapped to the nearest */
#define PLANE_ANYX 3
#define PLANE_ANYY 4
#define PLANE_ANYZ 5

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
        switch (type) {
            case PLANE_X: return point[0] - dist;
            case PLANE_Y: return point[1] - dist;
            case PLANE_Z: return point[2] - dist;
            default: {
                return qplane3f::distance_to(point);
            }
        }
    }
};

// Q1 contents

#define CONTENTS_EMPTY -1
#define CONTENTS_SOLID -2
#define CONTENTS_WATER -3
#define CONTENTS_SLIME -4
#define CONTENTS_LAVA -5
#define CONTENTS_SKY -6
#define CONTENTS_MIN CONTENTS_SKY

// Q2 contents (from qfiles.h)

// contents flags are seperate bits
// a given brush can contribute multiple content bits
// multiple brushes can be in a single leaf

// these definitions also need to be in q_shared.h!

// lower bits are stronger, and will eat weaker brushes completely
#define Q2_CONTENTS_SOLID 1 // an eye is never valid in a solid
#define Q2_CONTENTS_WINDOW 2 // translucent, but not watery
#define Q2_CONTENTS_AUX 4
#define Q2_CONTENTS_LAVA 8
#define Q2_CONTENTS_SLIME 16
#define Q2_CONTENTS_WATER 32
#define Q2_CONTENTS_MIST 64
#define Q2_LAST_VISIBLE_CONTENTS 64

#define Q2_CONTENTS_LIQUID (Q2_CONTENTS_LAVA | Q2_CONTENTS_SLIME | Q2_CONTENTS_WATER) // mxd

// remaining contents are non-visible, and don't eat brushes

#define Q2_CONTENTS_AREAPORTAL 0x8000

#define Q2_CONTENTS_PLAYERCLIP 0x10000
#define Q2_CONTENTS_MONSTERCLIP 0x20000

// currents can be added to any other contents, and may be mixed
#define Q2_CONTENTS_CURRENT_0 0x40000
#define Q2_CONTENTS_CURRENT_90 0x80000
#define Q2_CONTENTS_CURRENT_180 0x100000
#define Q2_CONTENTS_CURRENT_270 0x200000
#define Q2_CONTENTS_CURRENT_UP 0x400000
#define Q2_CONTENTS_CURRENT_DOWN 0x800000

#define Q2_CONTENTS_ORIGIN 0x1000000 // removed before bsping an entity

#define Q2_CONTENTS_MONSTER 0x2000000 // should never be on a brush, only in game
#define Q2_CONTENTS_DEADMONSTER 0x4000000
#define Q2_CONTENTS_DETAIL 0x8000000 // brushes to be added after vis leafs
#define Q2_CONTENTS_TRANSLUCENT 0x10000000 // auto set if any surface has trans
#define Q2_CONTENTS_LADDER 0x20000000

// Special contents flags for the compiler only
#define CFLAGS_STRUCTURAL_COVERED_BY_DETAIL (1 << 0)
#define CFLAGS_WAS_ILLUSIONARY (1 << 1) /* was illusionary, got changed to something else */
#define CFLAGS_BMODEL_MIRROR_INSIDE                                                                                    \
    (1 << 3) /* set "_mirrorinside" "1" on a bmodel to mirror faces for when the player is inside. */
#define CFLAGS_NO_CLIPPING_SAME_TYPE                                                                                   \
    (1 << 4) /* Don't clip the same content type. mostly intended for CONTENTS_DETAIL_ILLUSIONARY */
// only one of these flags below should ever be set.
#define CFLAGS_HINT (1 << 5)
#define CFLAGS_CLIP (1 << 6)
#define CFLAGS_ORIGIN (1 << 7)
#define CFLAGS_DETAIL (1 << 8)
#define CFLAGS_DETAIL_ILLUSIONARY (1 << 9)
#define CFLAGS_DETAIL_FENCE (1 << 10)
#define CFLAGS_ILLUSIONARY_VISBLOCKER (1 << 11)
// all of the detail values
#define CFLAGS_DETAIL_MASK (CFLAGS_DETAIL | CFLAGS_DETAIL_ILLUSIONARY | CFLAGS_DETAIL_FENCE)
// all of the special content types
#define CFLAGS_CONTENTS_MASK                                                                                           \
    (CFLAGS_HINT | CFLAGS_CLIP | CFLAGS_ORIGIN | CFLAGS_DETAIL_MASK | CFLAGS_ILLUSIONARY_VISBLOCKER)

struct gamedef_t;

struct contentflags_t
{
    // native flags value; what's written to the BSP basically
    int32_t native;

    // extra flags, specific to BSP only
    int32_t extended;

    // for CFLAGS_STRUCTURAL_COVERED_BY_DETAIL
    int32_t covered_native;

    constexpr bool operator==(const contentflags_t &other) const
    {
        return native == other.native && extended == other.extended;
    }

    constexpr bool operator!=(const contentflags_t &other) const { return !(*this == other); }

    // check if these contents are marked as any (or a specific kind of) detail brush.
    constexpr bool is_detail(int32_t types = CFLAGS_DETAIL_MASK) const
    {
        return (extended & CFLAGS_DETAIL_MASK) & types;
    }

    bool is_empty(const gamedef_t *game) const;

    // solid, not detail or any other extended content types
    bool is_solid(const gamedef_t *game) const;
    bool is_sky(const gamedef_t *game) const;
    bool is_liquid(const gamedef_t *game) const;
    bool is_valid(const gamedef_t *game, bool strict = true) const;

    constexpr bool is_hint() const { return extended & CFLAGS_HINT; }

    constexpr bool is_clip() const { return extended & CFLAGS_CLIP; }

    constexpr bool is_origin() const { return extended & CFLAGS_ORIGIN; }

    constexpr bool clips_same_type() const { return !(extended & CFLAGS_NO_CLIPPING_SAME_TYPE); }

    constexpr bool is_fence() const { return (extended & (CFLAGS_DETAIL_FENCE | CFLAGS_DETAIL_ILLUSIONARY)) != 0; }

    // check if this content's `type` - which is distinct from various
    // flags that turn things on/off - match. Exactly what the native
    // "type" is depends on the game, but any of the detail flags must
    // also match.
    bool types_equal(const contentflags_t &other, const gamedef_t *game) const;

    int32_t priority(const gamedef_t *game) const;

    std::string to_string(const gamedef_t *game) const;
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

struct bsp29_dnode_t
{
    int32_t planenum;
    std::array<int16_t, 2>
        children; /* negative numbers are -(leafs+1), not nodes. children[0] is front, children[1] is back */
    qvec3s mins; /* for sphere culling */
    qvec3s maxs;
    uint16_t firstface;
    uint16_t numfaces; /* counting both sides */

    bsp29_dnode_t() = default;

    // convert from mbsp_t
    bsp29_dnode_t(const bsp2_dnode_t &model)
        : planenum(model.planenum), children(array_cast<decltype(children)>(model.children, "dnode_t::children")),
          mins(aabb_mins_cast<int16_t>(model.mins, "dnode_t::mins")),
          maxs(aabb_maxs_cast<int16_t>(model.maxs, "dnode_t::maxs")),
          firstface(numeric_cast<uint16_t>(model.firstface, "dnode_t::firstface")),
          numfaces(numeric_cast<uint16_t>(model.numfaces, "dnode_t::numfaces"))
    {
    }

    // convert to mbsp_t
    operator bsp2_dnode_t() const
    {
        return {planenum, array_cast<decltype(bsp2_dnode_t::children)>(children, "dnode_t::children"),
            aabb_mins_cast<float>(mins, "dnode_t::mins"), aabb_mins_cast<float>(maxs, "dnode_t::maxs"), firstface,
            numfaces};
    }

    // serialize for streams
    auto stream_data() { return std::tie(planenum, children, mins, maxs, firstface, numfaces); }
};

struct bsp2rmq_dnode_t
{
    int32_t planenum;
    std::array<int32_t, 2> children; /* negative numbers are -(leafs+1), not nodes */
    qvec3s mins; /* for sphere culling */
    qvec3s maxs;
    uint32_t firstface;
    uint32_t numfaces; /* counting both sides */

    bsp2rmq_dnode_t() = default;

    // convert from mbsp_t
    bsp2rmq_dnode_t(const bsp2_dnode_t &model)
        : planenum(model.planenum), children(model.children),
          mins(aabb_mins_cast<int16_t>(model.mins, "dnode_t::mins")),
          maxs(aabb_maxs_cast<int16_t>(model.maxs, "dnode_t::maxs")), firstface(model.firstface),
          numfaces(model.numfaces)
    {
    }

    // convert to mbsp_t
    operator bsp2_dnode_t() const
    {
        return {planenum, children, aabb_mins_cast<float>(mins, "dnode_t::mins"),
            aabb_mins_cast<float>(maxs, "dnode_t::maxs"), firstface, numfaces};
    }

    // serialize for streams
    auto stream_data() { return std::tie(planenum, children, mins, maxs, firstface, numfaces); }
};

struct q2_dnode_t
{
    int32_t planenum;
    std::array<int32_t, 2> children; // negative numbers are -(leafs+1), not nodes
    qvec3s mins; // for frustom culling
    qvec3s maxs;
    uint16_t firstface;
    uint16_t numfaces; // counting both sides

    q2_dnode_t() = default;

    // convert from mbsp_t
    q2_dnode_t(const bsp2_dnode_t &model)
        : planenum(model.planenum), children(model.children),
          mins(aabb_mins_cast<int16_t>(model.mins, "dnode_t::mins")),
          maxs(aabb_maxs_cast<int16_t>(model.maxs, "dnode_t::maxs")),
          firstface(numeric_cast<uint16_t>(model.firstface, "dnode_t::firstface")),
          numfaces(numeric_cast<uint16_t>(model.numfaces, "dnode_t::numfaces"))
    {
    }

    // convert to mbsp_t
    operator bsp2_dnode_t() const
    {
        return {planenum, children, aabb_mins_cast<float>(mins, "dnode_t::mins"),
            aabb_mins_cast<float>(maxs, "dnode_t::maxs"), firstface, numfaces};
    }

    // serialize for streams
    auto stream_data() { return std::tie(planenum, children, mins, maxs, firstface, numfaces); }
};

using q2_dnode_qbism_t = bsp2_dnode_t;

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

struct bsp29_dclipnode_t
{
    int32_t planenum;
    std::array<int16_t, 2> children; /* negative numbers are contents */

    bsp29_dclipnode_t() = default;

    // convert from mbsp_t
    bsp29_dclipnode_t(const bsp2_dclipnode_t &model)
        : planenum(model.planenum), children({downcast(model.children[0]), downcast(model.children[1])})
    {
    }

    // convert to mbsp_t
    operator bsp2_dclipnode_t() const { return {planenum, {upcast(children[0]), upcast(children[1])}}; }

    // serialize for streams
    auto stream_data() { return std::tie(planenum, children); }

    /* Slightly tricky since we support > 32k clipnodes */
private:
    static constexpr int16_t downcast(const int32_t &v)
    {
        if (v < -15 || v > 0xFFF0)
            throw std::overflow_error("dclipmode_t::children");

        return static_cast<int16_t>(v < 0 ? v + 0x10000 : v);
    }

    static constexpr int32_t upcast(const int16_t &v)
    {
        int32_t child = (uint16_t)v;
        return child > 0xfff0 ? child - 0x10000 : child;
    }
};

// Q1 Texture flags.
#define TEX_SPECIAL 1 /* sky or slime, no lightmap or 256 subdivision */

// Q2 Texture flags.
#define Q2_SURF_LIGHT 0x1 // value will hold the light strength

#define Q2_SURF_SLICK 0x2 // effects game physics

#define Q2_SURF_SKY 0x4 // don't draw, but add to skybox
#define Q2_SURF_WARP 0x8 // turbulent water warp
#define Q2_SURF_TRANS33 0x10
#define Q2_SURF_TRANS66 0x20
#define Q2_SURF_FLOWING 0x40 // scroll towards angle
#define Q2_SURF_NODRAW 0x80 // don't bother referencing the texture

#define Q2_SURF_HINT 0x100 // make a primary bsp splitter
#define Q2_SURF_SKIP 0x200 // completely ignore, allowing non-closed brushes

#define Q2_SURF_TRANSLUCENT (Q2_SURF_TRANS33 | Q2_SURF_TRANS66) // mxd

struct surfflags_t
{
    // native flags value; what's written to the BSP basically
    int32_t native;

    // an invisible surface
    bool is_skip;

    // hint surface
    bool is_hint;

    // don't receive dirtmapping
    bool no_dirt;

    // don't cast a shadow
    bool no_shadow;

    // light doesn't bounce off this face
    bool no_bounce;

    // opt out of minlight on this face
    bool no_minlight;

    // don't expand this face for larger clip hulls
    bool no_expand;

    // this face doesn't receive light
    bool light_ignore;

    // if non zero, enables phong shading and gives the angle threshold to use
    vec_t phong_angle;

    // if non zero, overrides _phong_angle for concave joints
    vec_t phong_angle_concave;

    // minlight value for this face
    vec_t minlight;

    // red minlight colors for this face
    qvec3b minlight_color;

    // custom opacity
    vec_t light_alpha;

    constexpr bool needs_write() const
    {
        return no_dirt || no_shadow || no_bounce || no_minlight || no_expand || light_ignore || phong_angle ||
               phong_angle_concave || minlight || !qv::emptyExact(minlight_color) || light_alpha;
    }

private:
    constexpr auto as_tuple() const
    {
        return std::tie(native, is_skip, is_hint, no_dirt, no_shadow, no_bounce, no_minlight, no_expand, light_ignore,
            phong_angle, phong_angle_concave, minlight, minlight_color, light_alpha);
    }

public:
    // sort support
    constexpr bool operator<(const surfflags_t &other) const { return as_tuple() < other.as_tuple(); }

    constexpr bool operator>(const surfflags_t &other) const { return as_tuple() > other.as_tuple(); }

    bool is_valid(const gamedef_t *game) const;
};

// header before tightly packed surfflags_t[num_texinfo]
struct extended_flags_header_t
{
    uint32_t num_texinfo;
    uint32_t surfflags_size; // sizeof(surfflags_t)
};

template<typename T>
struct texvec : qmat<T, 2, 4>
{
    using qmat<T, 2, 4>::qmat;

    template<typename T2>
    constexpr qvec<T2, 2> uvs(const qvec<T2, 3> &pos) const
    {
        return {(pos[0] * this->at(0, 0) + pos[1] * this->at(0, 1) + pos[2] * this->at(0, 2) + this->at(0, 3)),
            (pos[0] * this->at(1, 0) + pos[1] * this->at(1, 1) + pos[2] * this->at(1, 2) + this->at(1, 3))};
    }

    template<typename T2>
    constexpr qvec<T2, 2> uvs(const qvec<T2, 3> &pos, const int32_t &width, const int32_t &height) const
    {
        return uvs(pos) / qvec<T2, 2>{width, height};
    }

    // Not blit compatible because qmat is column-major but
    // texvecs are row-major

    void stream_read(std::istream &stream)
    {
        for (size_t i = 0; i < 2; i++)
            for (size_t x = 0; x < 4; x++) {
                stream >= this->at(i, x);
            }
    }

    void stream_write(std::ostream &stream) const
    {
        for (size_t i = 0; i < 2; i++)
            for (size_t x = 0; x < 4; x++) {
                stream <= this->at(i, x);
            }
    }
};

// Fmt support
template<class T>
struct fmt::formatter<texvec<T>> : formatter<qmat<T, 2, 4>>
{
};

using texvecf = texvec<float>;

struct gtexinfo_t
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

struct texinfo_t
{
    texvecf vecs; /* [s/t][xyz offset] */
    int32_t miptex;
    int32_t flags;

    texinfo_t() = default;

    // convert from mbsp_t
    texinfo_t(const gtexinfo_t &model) : vecs(model.vecs), miptex(model.miptex), flags(model.flags.native) { }

    // convert to mbsp_t
    operator gtexinfo_t() const { return {vecs, {flags}, miptex}; }

    // serialize for streams
    auto stream_data() { return std::tie(vecs, miptex, flags); }
};

struct q2_texinfo_t
{
    texvecf vecs; // [s/t][xyz offset]
    int32_t flags; // miptex flags + overrides
    int32_t value; // light emission, etc
    std::array<char, 32> texture; // texture name (textures/*.wal)
    int32_t nexttexinfo; // for animations, -1 = end of chain

    q2_texinfo_t() = default;

    // convert from mbsp_t
    q2_texinfo_t(const gtexinfo_t &model)
        : vecs(model.vecs), flags(model.flags.native), value(model.value), texture(model.texture),
          nexttexinfo(model.nexttexinfo)
    {
    }

    // convert to mbsp_t
    operator gtexinfo_t() const { return {vecs, {flags}, -1, value, texture, nexttexinfo}; }

    // serialize for streams
    auto stream_data() { return std::tie(vecs, flags, value, texture, nexttexinfo); }
};

/*
 * Note that edge 0 is never used, because negative edge nums are used for
 * counterclockwise use of the edge in a face
 */
using bsp29_dedge_t = std::array<uint16_t, 2>; /* vertex numbers */
using bsp2_dedge_t = std::array<uint32_t, 2>; /* vertex numbers */

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

struct bsp29_dface_t
{
    int16_t planenum;
    int16_t side;
    int32_t firstedge; /* we must support > 64k edges */
    int16_t numedges;
    int16_t texinfo;

    /* lighting info */
    std::array<uint8_t, MAXLIGHTMAPS> styles;
    int32_t lightofs; /* start of [numstyles*surfsize] samples */

    bsp29_dface_t() = default;

    // convert from mbsp_t
    bsp29_dface_t(const mface_t &model)
        : planenum(numeric_cast<int16_t>(model.planenum, "dface_t::planenum")),
          side(numeric_cast<int16_t>(model.side, "dface_t::side")), firstedge(model.firstedge),
          numedges(numeric_cast<int16_t>(model.numedges, "dface_t::numedges")),
          texinfo(numeric_cast<int16_t>(model.texinfo, "dface_t::texinfo")), styles(model.styles),
          lightofs(model.lightofs)
    {
    }

    // convert to mbsp_t
    operator mface_t() const { return {planenum, side, firstedge, numedges, texinfo, styles, lightofs}; }

    // serialize for streams
    auto stream_data() { return std::tie(planenum, side, firstedge, numedges, texinfo, styles, lightofs); }
};

struct bsp2_dface_t
{
    int32_t planenum;
    int32_t side; // if true, the face is on the back side of the plane
    int32_t firstedge; /* we must support > 64k edges */
    int32_t numedges;
    int32_t texinfo;

    /* lighting info */
    std::array<uint8_t, MAXLIGHTMAPS> styles;
    int32_t lightofs; /* start of [numstyles*surfsize] samples */

    bsp2_dface_t() = default;

    // convert from mbsp_t
    bsp2_dface_t(const mface_t &model)
        : planenum(numeric_cast<int32_t>(model.planenum, "dface_t::planenum")), side(model.side),
          firstedge(model.firstedge), numedges(model.numedges), texinfo(model.texinfo), styles(model.styles),
          lightofs(model.lightofs)
    {
    }

    // convert to mbsp_t
    operator mface_t() const { return {planenum, side, firstedge, numedges, texinfo, styles, lightofs}; }

    // serialize for streams
    auto stream_data() { return std::tie(planenum, side, firstedge, numedges, texinfo, styles, lightofs); }
};

struct q2_dface_t
{
    uint16_t planenum; // NOTE: only difference from bsp29_dface_t
    int16_t side;
    int32_t firstedge; // we must support > 64k edges
    int16_t numedges;
    int16_t texinfo;

    // lighting info
    std::array<uint8_t, MAXLIGHTMAPS> styles;
    int32_t lightofs; // start of [numstyles*surfsize] samples

    q2_dface_t() = default;

    // convert from mbsp_t
    q2_dface_t(const mface_t &model)
        : planenum(numeric_cast<uint16_t>(model.planenum, "dface_t::planenum")),
          side(numeric_cast<int16_t>(model.side, "dface_t::side")), firstedge(model.firstedge),
          numedges(numeric_cast<int16_t>(model.numedges, "dface_t::numedges")),
          texinfo(numeric_cast<int16_t>(model.texinfo, "dface_t::texinfo")), styles(model.styles),
          lightofs(model.lightofs)
    {
    }

    // convert to mbsp_t
    operator mface_t() const { return {planenum, side, firstedge, numedges, texinfo, styles, lightofs}; }

    // serialize for streams
    auto stream_data() { return std::tie(planenum, side, firstedge, numedges, texinfo, styles, lightofs); }
};

struct q2_dface_qbism_t
{
    uint32_t planenum; // NOTE: only difference from bsp2_dface_t
    int32_t side;
    int32_t firstedge; // we must support > 64k edges
    int32_t numedges;
    int32_t texinfo;

    // lighting info
    std::array<uint8_t, MAXLIGHTMAPS> styles;
    int32_t lightofs; // start of [numstyles*surfsize] samples

    q2_dface_qbism_t() = default;

    // convert from mbsp_t
    q2_dface_qbism_t(const mface_t &model)
        : planenum(numeric_cast<uint32_t>(model.planenum, "dface_t::planenum")), side(model.side),
          firstedge(model.firstedge), numedges(model.numedges), texinfo(model.texinfo), styles(model.styles),
          lightofs(model.lightofs)
    {
    }

    // convert to mbsp_t
    operator mface_t() const { return {planenum, side, firstedge, numedges, texinfo, styles, lightofs}; }

    // serialize for streams
    auto stream_data() { return std::tie(planenum, side, firstedge, numedges, texinfo, styles, lightofs); }
};

/*
 * leaf 0 is the generic CONTENTS_SOLID leaf, used for all solid areas (except Q2)
 * all other leafs need visibility info
 */
/* Ambient Sounds */
#define AMBIENT_WATER 0
#define AMBIENT_SKY 1
#define AMBIENT_SLIME 2
#define AMBIENT_LAVA 3
constexpr size_t NUM_AMBIENTS = 4;

struct mleaf_t
{
    // bsp2_dleaf_t
    int32_t contents;
    int32_t visofs; /* -1 = no visibility info */
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

struct bsp29_dleaf_t
{
    int32_t contents;
    int32_t visofs; /* -1 = no visibility info */
    qvec3s mins; /* for frustum culling     */
    qvec3s maxs;
    uint16_t firstmarksurface;
    uint16_t nummarksurfaces;
    std::array<uint8_t, NUM_AMBIENTS> ambient_level;

    bsp29_dleaf_t() = default;

    // convert from mbsp_t
    bsp29_dleaf_t(const mleaf_t &model)
        : contents(model.contents), visofs(model.visofs), mins(aabb_mins_cast<int16_t>(model.mins, "dleaf_t::mins")),
          maxs(aabb_maxs_cast<int16_t>(model.maxs, "dleaf_t::maxs")),
          firstmarksurface(numeric_cast<uint16_t>(model.firstmarksurface, "dleaf_t::firstmarksurface")),
          nummarksurfaces(numeric_cast<uint16_t>(model.nummarksurfaces, "dleaf_t::nummarksurfaces")),
          ambient_level(model.ambient_level)
    {
    }

    // convert to mbsp_t
    operator mleaf_t() const
    {
        return {contents, visofs, aabb_mins_cast<float>(mins, "dleaf_t::mins"),
            aabb_mins_cast<float>(maxs, "dleaf_t::maxs"), firstmarksurface, nummarksurfaces, ambient_level};
    }

    // serialize for streams
    auto stream_data()
    {
        return std::tie(contents, visofs, mins, maxs, firstmarksurface, nummarksurfaces, ambient_level);
    }
};

struct bsp2rmq_dleaf_t
{
    int32_t contents;
    int32_t visofs; /* -1 = no visibility info */
    qvec3s mins; /* for frustum culling     */
    qvec3s maxs;
    uint32_t firstmarksurface;
    uint32_t nummarksurfaces;
    std::array<uint8_t, NUM_AMBIENTS> ambient_level;

    bsp2rmq_dleaf_t() = default;

    // convert from mbsp_t
    bsp2rmq_dleaf_t(const mleaf_t &model)
        : contents(model.contents), visofs(model.visofs), mins(aabb_mins_cast<int16_t>(model.mins, "dleaf_t::mins")),
          maxs(aabb_maxs_cast<int16_t>(model.maxs, "dleaf_t::maxs")), firstmarksurface(model.firstmarksurface),
          nummarksurfaces(model.nummarksurfaces), ambient_level(model.ambient_level)
    {
    }

    // convert to mbsp_t
    operator mleaf_t() const
    {
        return {contents, visofs, aabb_mins_cast<float>(mins, "dleaf_t::mins"),
            aabb_mins_cast<float>(maxs, "dleaf_t::maxs"), firstmarksurface, nummarksurfaces, ambient_level};
    }

    // serialize for streams
    auto stream_data()
    {
        return std::tie(contents, visofs, mins, maxs, firstmarksurface, nummarksurfaces, ambient_level);
    }
};

struct bsp2_dleaf_t
{
    int32_t contents;
    int32_t visofs; /* -1 = no visibility info */
    qvec3f mins; /* for frustum culling     */
    qvec3f maxs;
    uint32_t firstmarksurface;
    uint32_t nummarksurfaces;
    std::array<uint8_t, NUM_AMBIENTS> ambient_level;

    bsp2_dleaf_t() = default;

    // convert from mbsp_t
    bsp2_dleaf_t(const mleaf_t &model)
        : contents(model.contents), visofs(model.visofs), mins(model.mins), maxs(model.maxs),
          firstmarksurface(model.firstmarksurface), nummarksurfaces(model.nummarksurfaces),
          ambient_level(model.ambient_level)
    {
    }

    // convert to mbsp_t
    operator mleaf_t() const
    {
        return {contents, visofs, mins, maxs, firstmarksurface, nummarksurfaces, ambient_level};
    }

    // serialize for streams
    auto stream_data()
    {
        return std::tie(contents, visofs, mins, maxs, firstmarksurface, nummarksurfaces, ambient_level);
    }
};

struct q2_dleaf_t
{
    int32_t contents; // OR of all brushes (not needed?)

    int16_t cluster;
    int16_t area;

    qvec3s mins; // for frustum culling
    qvec3s maxs;

    uint16_t firstleafface;
    uint16_t numleaffaces;

    uint16_t firstleafbrush;
    uint16_t numleafbrushes;

    q2_dleaf_t() = default;

    // convert from mbsp_t
    q2_dleaf_t(const mleaf_t &model)
        : contents(model.contents), cluster(numeric_cast<int16_t>(model.cluster, "dleaf_t::cluster")),
          area(numeric_cast<int16_t>(model.area, "dleaf_t::area")),
          mins(aabb_mins_cast<int16_t>(model.mins, "dleaf_t::mins")),
          maxs(aabb_mins_cast<int16_t>(model.maxs, "dleaf_t::maxs")),
          firstleafface(numeric_cast<uint16_t>(model.firstmarksurface, "dleaf_t::firstmarksurface")),
          numleaffaces(numeric_cast<uint16_t>(model.nummarksurfaces, "dleaf_t::nummarksurfaces")),
          firstleafbrush(numeric_cast<uint16_t>(model.firstleafbrush, "dleaf_t::firstleafbrush")),
          numleafbrushes(numeric_cast<uint16_t>(model.numleafbrushes, "dleaf_t::numleafbrushes"))
    {
    }

    // convert to mbsp_t
    operator mleaf_t() const
    {
        return {contents, -1, aabb_mins_cast<float>(mins, "dleaf_t::mins"),
            aabb_mins_cast<float>(maxs, "dleaf_t::maxs"), firstleafface, numleaffaces, {}, cluster, area,
            firstleafbrush, numleafbrushes};
    }

    // serialize for streams
    auto stream_data()
    {
        return std::tie(
            contents, cluster, area, mins, maxs, firstleafface, numleaffaces, firstleafbrush, numleafbrushes);
    }
};

struct q2_dleaf_qbism_t
{
    int32_t contents; // OR of all brushes (not needed?)

    int32_t cluster;
    int32_t area;

    qvec3f mins; // for frustum culling
    qvec3f maxs;

    uint32_t firstleafface;
    uint32_t numleaffaces;

    uint32_t firstleafbrush;
    uint32_t numleafbrushes;

    q2_dleaf_qbism_t() = default;

    // convert from mbsp_t
    q2_dleaf_qbism_t(const mleaf_t &model)
        : contents(model.contents), cluster(model.cluster), area(model.area), mins(model.mins), maxs(model.maxs),
          firstleafface(model.firstmarksurface), numleaffaces(model.nummarksurfaces),
          firstleafbrush(model.firstleafbrush), numleafbrushes(model.numleafbrushes)
    {
    }

    // convert to mbsp_t
    operator mleaf_t() const
    {
        return {
            contents, -1, mins, maxs, firstleafface, numleaffaces, {}, cluster, area, firstleafbrush, numleafbrushes};
    }

    // serialize for streams
    auto stream_data()
    {
        return std::tie(
            contents, cluster, area, mins, maxs, firstleafface, numleaffaces, firstleafbrush, numleafbrushes);
    }
};

struct q2_dbrushside_qbism_t
{
    uint32_t planenum; // facing out of the leaf
    int32_t texinfo;

    // serialize for streams
    auto stream_data() { return std::tie(planenum, texinfo); }
};

struct q2_dbrushside_t
{
    uint16_t planenum; // facing out of the leaf
    int16_t texinfo;

    q2_dbrushside_t() = default;

    // convert from mbsp_t
    q2_dbrushside_t(const q2_dbrushside_qbism_t &model)
        : planenum(numeric_cast<uint16_t>(model.planenum, "dbrushside_t::planenum")),
          texinfo(numeric_cast<int16_t>(model.texinfo, "dbrushside_t::texinfo"))
    {
    }

    // convert to mbsp_t
    operator q2_dbrushside_qbism_t() const { return {planenum, texinfo}; }

    // serialize for streams
    auto stream_data() { return std::tie(planenum, texinfo); }
};

struct dbrush_t
{
    int32_t firstside;
    int32_t numsides;
    int32_t contents;

    // serialize for streams
    auto stream_data() { return std::tie(firstside, numsides, contents); }
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
    inline int32_t get_bit_offset(vistype_t type, size_t cluster)
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

// each area has a list of portals that lead into other areas
// when portals are closed, other areas may not be visible or
// hearable even if the vis info says that it should be
struct dareaportal_t
{
    int32_t portalnum;
    int32_t otherarea;

    // serialize for streams
    auto stream_data() { return std::tie(portalnum, otherarea); }
};

struct darea_t
{
    int32_t numareaportals;
    int32_t firstareaportal;

    // serialize for streams
    auto stream_data() { return std::tie(numareaportals, firstareaportal); }
};

// Q1-esque maps can use one of these two.
using dmodelq1_vector = std::vector<dmodelq1_t>;
using dmodelh2_vector = std::vector<dmodelh2_t>;

// Q1-esque maps can use one of these two.
using miptexq1_lump = dmiptexlump_t<miptex_t>;
using miptexhl_lump = dmiptexlump_t<miptexhl_t>;

// type tag used for type inference
struct q1bsp_tag_t
{
};

struct bsp29_t : q1bsp_tag_t
{
    std::variant<std::monostate, dmodelq1_vector, dmodelh2_vector> dmodels;
    std::vector<uint8_t> dvisdata;
    std::vector<uint8_t> dlightdata;
    std::variant<std::monostate, miptexq1_lump, miptexhl_lump> dtex;
    std::string dentdata;
    std::vector<bsp29_dleaf_t> dleafs;
    std::vector<dplane_t> dplanes;
    std::vector<qvec3f> dvertexes;
    std::vector<bsp29_dnode_t> dnodes;
    std::vector<texinfo_t> texinfo;
    std::vector<bsp29_dface_t> dfaces;
    std::vector<bsp29_dclipnode_t> dclipnodes;
    std::vector<bsp29_dedge_t> dedges;
    std::vector<uint16_t> dmarksurfaces;
    std::vector<int32_t> dsurfedges;
};

struct bsp2rmq_t : q1bsp_tag_t
{
    std::variant<std::monostate, dmodelq1_vector, dmodelh2_vector> dmodels;
    std::vector<uint8_t> dvisdata;
    std::vector<uint8_t> dlightdata;
    std::variant<std::monostate, miptexq1_lump, miptexhl_lump> dtex;
    std::string dentdata;
    std::vector<bsp2rmq_dleaf_t> dleafs;
    std::vector<dplane_t> dplanes;
    std::vector<qvec3f> dvertexes;
    std::vector<bsp2rmq_dnode_t> dnodes;
    std::vector<texinfo_t> texinfo;
    std::vector<bsp2_dface_t> dfaces;
    std::vector<bsp2_dclipnode_t> dclipnodes;
    std::vector<bsp2_dedge_t> dedges;
    std::vector<uint32_t> dmarksurfaces;
    std::vector<int32_t> dsurfedges;
};

struct bsp2_t : q1bsp_tag_t
{
    std::variant<std::monostate, dmodelq1_vector, dmodelh2_vector> dmodels;
    std::vector<uint8_t> dvisdata;
    std::vector<uint8_t> dlightdata;
    std::variant<std::monostate, miptexq1_lump, miptexhl_lump> dtex;
    std::string dentdata;
    std::vector<bsp2_dleaf_t> dleafs;
    std::vector<dplane_t> dplanes;
    std::vector<qvec3f> dvertexes;
    std::vector<bsp2_dnode_t> dnodes;
    std::vector<texinfo_t> texinfo;
    std::vector<bsp2_dface_t> dfaces;
    std::vector<bsp2_dclipnode_t> dclipnodes;
    std::vector<bsp2_dedge_t> dedges;
    std::vector<uint32_t> dmarksurfaces;
    std::vector<int32_t> dsurfedges;
};

// type tag used for type inference
struct q2bsp_tag_t
{
};

struct q2bsp_t : q2bsp_tag_t
{
    std::vector<q2_dmodel_t> dmodels;

    mvis_t dvis;

    std::vector<uint8_t> dlightdata;
    std::string dentdata;
    std::vector<q2_dleaf_t> dleafs;
    std::vector<dplane_t> dplanes;
    std::vector<qvec3f> dvertexes;
    std::vector<q2_dnode_t> dnodes;
    std::vector<q2_texinfo_t> texinfo;
    std::vector<q2_dface_t> dfaces;
    std::vector<bsp29_dedge_t> dedges;
    std::vector<uint16_t> dleaffaces;
    std::vector<uint16_t> dleafbrushes;
    std::vector<int32_t> dsurfedges;
    std::vector<darea_t> dareas;
    std::vector<dareaportal_t> dareaportals;
    std::vector<dbrush_t> dbrushes;
    std::vector<q2_dbrushside_t> dbrushsides;
};

struct q2bsp_qbism_t : q2bsp_tag_t
{
    std::vector<q2_dmodel_t> dmodels;
    mvis_t dvis;
    std::vector<uint8_t> dlightdata;
    std::string dentdata;
    std::vector<q2_dleaf_qbism_t> dleafs;
    std::vector<dplane_t> dplanes;
    std::vector<qvec3f> dvertexes;
    std::vector<q2_dnode_qbism_t> dnodes;
    std::vector<q2_texinfo_t> texinfo;
    std::vector<q2_dface_qbism_t> dfaces;
    std::vector<bsp2_dedge_t> dedges;
    std::vector<uint32_t> dleaffaces;
    std::vector<uint32_t> dleafbrushes;
    std::vector<int32_t> dsurfedges;
    std::vector<darea_t> dareas;
    std::vector<dareaportal_t> dareaportals;
    std::vector<dbrush_t> dbrushes;
    std::vector<q2_dbrushside_qbism_t> dbrushsides;
};

struct bspversion_t;

struct mbsp_t
{
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
    std::vector<gtexinfo_t> texinfo;
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
}; // "generic" bsp - superset of all other supported types

struct dheader_t
{
    int32_t ident;
    std::array<lump_t, BSP_LUMPS> lumps;

    auto stream_data() { return std::tie(ident, lumps); }
};

struct q2_dheader_t
{
    int32_t ident;
    int32_t version;
    std::array<lump_t, Q2_HEADER_LUMPS> lumps;

    auto stream_data() { return std::tie(ident, version, lumps); }
};

/* ========================================================================= */

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
    bspxentry_t(void *lumpdata, size_t lumpsize) : lumpdata(reinterpret_cast<uint8_t *>(lumpdata)), lumpsize(lumpsize)
    {
    }
};

struct bspdata_t
{
    const bspversion_t *version, *loadversion;

    // Stay in monostate until a BSP type is requested.
    std::variant<std::monostate, mbsp_t, bsp29_t, bsp2rmq_t, bsp2_t, q2bsp_t, q2bsp_qbism_t> bsp;

    // This can be used with any BSP format.
    struct
    {
        std::unordered_map<std::string, bspxentry_t> entries;

        // convenience function to transfer a generic pointer into
        // the entries list
        inline void transfer(const char *xname, uint8_t *&xdata, size_t xsize)
        {
            entries.insert_or_assign(xname, bspxentry_t{xdata, xsize});
            xdata = nullptr;
        }

        // copies the data over to the BSP
        void copy(const char *xname, const uint8_t *xdata, size_t xsize)
        {
            uint8_t *copy = new uint8_t[xsize];
            memcpy(copy, xdata, xsize);
            transfer(xname, copy, xsize);
        }
    } bspx;
};

// native game target ID
enum gameid_t
{
    GAME_UNKNOWN,
    GAME_QUAKE,
    GAME_HEXEN_II,
    GAME_HALF_LIFE,
    GAME_QUAKE_II,

    GAME_TOTAL
};

// Game definition, which contains data specific to
// the game a BSP version is being compiled for.
struct gamedef_t
{
    // ID, used for quick comparisons
    gameid_t id;

    // whether the game uses an RGB lightmap or not
    bool has_rgb_lightmap = false;

    // whether the game supports content flags on brush models
    bool allow_contented_bmodels = false;

    // base dir for searching for paths, in case we are in a mod dir
    // note: we need this to be able to be overridden via options
    const std::string default_base_dir;

    // max values of entity key & value pairs, only used for
    // printing warnings.
    size_t max_entity_key = 32;
    size_t max_entity_value = 128;

    gamedef_t(const char *default_base_dir) : default_base_dir(default_base_dir) { }

    virtual bool surf_is_lightmapped(const surfflags_t &flags) const = 0;
    virtual bool surf_is_subdivided(const surfflags_t &flags) const = 0;
    virtual bool surfflags_are_valid(const surfflags_t &flags) const = 0;
    // FIXME: fix so that we don't have to pass a name here
    virtual bool texinfo_is_hintskip(const surfflags_t &flags, const std::string &name) const = 0;
    virtual contentflags_t cluster_contents(const contentflags_t &contents0, const contentflags_t &contents1) const = 0;
    virtual int32_t get_content_type(const contentflags_t &contents) const = 0;
    virtual int32_t contents_priority(const contentflags_t &contents) const = 0;
    virtual contentflags_t create_extended_contents(const int32_t &cflags = 0) const = 0;
    virtual contentflags_t create_empty_contents(const int32_t &cflags = 0) const = 0;
    virtual contentflags_t create_solid_contents(const int32_t &cflags = 0) const = 0;
    virtual contentflags_t create_sky_contents(const int32_t &cflags = 0) const = 0;
    virtual contentflags_t create_liquid_contents(const int32_t &liquid_type, const int32_t &cflags = 0) const = 0;
    virtual bool contents_are_empty(const contentflags_t &contents) const = 0;
    virtual bool contents_are_solid(const contentflags_t &contents) const = 0;
    virtual bool contents_are_sky(const contentflags_t &contents) const = 0;
    virtual bool contents_are_liquid(const contentflags_t &contents) const = 0;
    virtual bool contents_are_valid(const contentflags_t &contents, bool strict = true) const = 0;
    virtual bool portal_can_see_through(const contentflags_t &contents0, const contentflags_t &contents1) const = 0;
    virtual std::string get_contents_display(const contentflags_t &contents) const = 0;
    virtual const std::initializer_list<aabb3d> &get_hull_sizes() const = 0;
    virtual contentflags_t face_get_contents(
        const std::string &texname, const surfflags_t &flags, const contentflags_t &contents) const = 0;
    virtual void init_filesystem(const std::filesystem::path &source) const = 0;
    virtual const std::vector<qvec3b> &get_default_palette() const = 0;
};

// BSP version struct & instances
struct bspversion_t
{
    /* identifier value, the first int32_t in the header */
    int32_t ident;
    /* version value, if supported; use NO_VERSION if a version is not required */
    int32_t version;
    /* short name used for command line args, etc */
    const char *short_name;
    /* full display name for printing */
    const char *name;
    /* lump specification */
    const std::initializer_list<lumpspec_t> lumps;
    /* game ptr */
    const gamedef_t *game;
    /* if we surpass the limits of this format, upgrade to this one */
    const bspversion_t *extended_limits;
};

// FMT support
template<>
struct fmt::formatter<bspversion_t>
{
    constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) { return ctx.end(); }

    template<typename FormatContext>
    auto format(const bspversion_t &v, FormatContext &ctx) -> decltype(ctx.out())
    {
        if (v.name) {
            return format_to(ctx.out(), "{}", v.name);
        }

        if (v.version != NO_VERSION) {
            return format_to(ctx.out(), "{}:{}", v.version, v.ident);
        }

        return format_to(ctx.out(), "{}", v.version, v.ident);
    }
};

extern const bspversion_t bspver_generic;
extern const bspversion_t bspver_q1;
extern const bspversion_t bspver_h2;
extern const bspversion_t bspver_h2bsp2;
extern const bspversion_t bspver_h2bsp2rmq;
extern const bspversion_t bspver_bsp2;
extern const bspversion_t bspver_bsp2rmq;
extern const bspversion_t bspver_hl;
extern const bspversion_t bspver_q2;
extern const bspversion_t bspver_qbism;

/* table of supported versions */
constexpr const bspversion_t *const bspversions[] = {&bspver_generic, &bspver_q1, &bspver_h2, &bspver_h2bsp2,
    &bspver_h2bsp2rmq, &bspver_bsp2, &bspver_bsp2rmq, &bspver_hl, &bspver_q2, &bspver_qbism};

void LoadBSPFile(std::filesystem::path &filename, bspdata_t *bspdata); // returns the filename as contained inside a bsp
void WriteBSPFile(const std::filesystem::path &filename, bspdata_t *bspdata);
void PrintBSPFileSizes(const bspdata_t *bspdata);
/**
 * Returns false if the conversion failed.
 */
bool ConvertBSPFormat(bspdata_t *bspdata, const bspversion_t *to_version);
