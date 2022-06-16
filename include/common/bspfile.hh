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
#include <any>

#include <common/cmdlib.hh>
#include <common/log.hh>
#include <common/qvec.hh>
#include <common/aabb.hh>
#include <common/settings.hh>

struct lump_t
{
    int32_t fileofs;
    int32_t filelen;

    auto stream_data() { return std::tie(fileofs, filelen); }
};

constexpr int32_t BSPVERSION = 29;
constexpr int32_t BSP2RMQVERSION = (('B' << 24) | ('S' << 16) | ('P' << 8) | '2');
constexpr int32_t BSP2VERSION = ('B' | ('S' << 8) | ('P' << 16) | ('2' << 24));
constexpr int32_t BSPHLVERSION = 30; // 24bit lighting, and private palettes in the textures lump.

enum q1_lump_t
{
    LUMP_ENTITIES,
    LUMP_PLANES,
    LUMP_TEXTURES,
    LUMP_VERTEXES,
    LUMP_VISIBILITY,
    LUMP_NODES,
    LUMP_TEXINFO,
    LUMP_FACES,
    LUMP_LIGHTING,
    LUMP_CLIPNODES,
    LUMP_LEAFS,
    LUMP_MARKSURFACES,
    LUMP_EDGES,
    LUMP_SURFEDGES,
    LUMP_MODELS,

    BSP_LUMPS
};

constexpr int32_t Q2_BSPVERSION = 38;
constexpr int32_t Q2_BSPIDENT = (('P' << 24) + ('S' << 16) + ('B' << 8) + 'I');
constexpr int32_t Q2_QBISMIDENT = (('P' << 24) + ('S' << 16) + ('B' << 8) + 'Q');

enum q2_lump_t
{
    Q2_LUMP_ENTITIES,
    Q2_LUMP_PLANES,
    Q2_LUMP_VERTEXES,
    Q2_LUMP_VISIBILITY,
    Q2_LUMP_NODES,
    Q2_LUMP_TEXINFO,
    Q2_LUMP_FACES,
    Q2_LUMP_LIGHTING,
    Q2_LUMP_LEAFS,
    Q2_LUMP_LEAFFACES,
    Q2_LUMP_LEAFBRUSHES,
    Q2_LUMP_EDGES,
    Q2_LUMP_SURFEDGES,
    Q2_LUMP_MODELS,
    Q2_LUMP_BRUSHES,
    Q2_LUMP_BRUSHSIDES,
    Q2_LUMP_POP,
    Q2_LUMP_AREAS,
    Q2_LUMP_AREAPORTALS,

    Q2_HEADER_LUMPS
};

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

constexpr size_t MAX_MAP_HULLS_Q1 = 4;

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

// Q1 contents

enum q1_contents_t : int32_t
{
    CONTENTS_EMPTY = -1,
    CONTENTS_SOLID = -2,
    CONTENTS_WATER = -3,
    CONTENTS_SLIME = -4,
    CONTENTS_LAVA = -5,
    CONTENTS_SKY = -6,

    CONTENTS_MIN = CONTENTS_SKY
};

// Q2 contents (from qfiles.h)

// contents flags are seperate bits
// a given brush can contribute multiple content bits
// multiple brushes can be in a single leaf

// lower bits are stronger, and will eat weaker brushes completely
enum q2_contents_t : int32_t
{
    Q2_CONTENTS_EMPTY = 0,
    Q2_CONTENTS_SOLID = nth_bit(0), // an eye is never valid in a solid
    Q2_CONTENTS_WINDOW = nth_bit(1), // translucent, but not watery
    Q2_CONTENTS_AUX = nth_bit(2),
    Q2_CONTENTS_LAVA = nth_bit(3),
    Q2_CONTENTS_SLIME = nth_bit(4),
    Q2_CONTENTS_WATER = nth_bit(5),
    Q2_CONTENTS_MIST = nth_bit(6),
    Q2_LAST_VISIBLE_CONTENTS = Q2_CONTENTS_MIST,
    Q2_ALL_VISIBLE_CONTENTS = Q2_CONTENTS_SOLID | Q2_CONTENTS_WINDOW | Q2_CONTENTS_AUX | Q2_CONTENTS_LAVA |
                              Q2_CONTENTS_SLIME | Q2_CONTENTS_WATER | Q2_CONTENTS_MIST,

    Q2_CONTENTS_LIQUID = (Q2_CONTENTS_LAVA | Q2_CONTENTS_SLIME | Q2_CONTENTS_WATER), // mxd

    // remaining contents are non-visible, and don't eat brushes

    Q2_CONTENTS_AREAPORTAL = nth_bit(15),

    Q2_CONTENTS_PLAYERCLIP = nth_bit(16),
    Q2_CONTENTS_MONSTERCLIP = nth_bit(17),

    // currents can be added to any other contents, and may be mixed
    Q2_CONTENTS_CURRENT_0 = nth_bit(18),
    Q2_CONTENTS_CURRENT_90 = nth_bit(19),
    Q2_CONTENTS_CURRENT_180 = nth_bit(20),
    Q2_CONTENTS_CURRENT_270 = nth_bit(21),
    Q2_CONTENTS_CURRENT_UP = nth_bit(22),
    Q2_CONTENTS_CURRENT_DOWN = nth_bit(23),

    Q2_CONTENTS_ORIGIN = nth_bit(24), // removed before bsping an entity

    Q2_CONTENTS_MONSTER = nth_bit(25), // should never be on a brush, only in game
    Q2_CONTENTS_DEADMONSTER = nth_bit(26),
    Q2_CONTENTS_DETAIL = nth_bit(27), // brushes to be added after vis leafs
    Q2_CONTENTS_TRANSLUCENT = nth_bit(28), // auto set if any surface has trans
    Q2_CONTENTS_LADDER = nth_bit(29)
};

struct gamedef_t;

struct contentflags_t
{
    // native flags value; what's written to the BSP basically
    int32_t native = 0;

    // extra data supplied by the game
    std::any game_data;

    // the value set directly from `_mirrorinside` on the brush, if available.
    // don't check this directly, use `is_mirror_inside` to allow the game to decide.
    std::optional<bool> mirror_inside = std::nullopt;

    // don't clip the same content type. mostly intended for CONTENTS_DETAIL_ILLUSIONARY.
    // don't check this directly, use `will_clip_same_type` to allow the game to decide.
    std::optional<bool> clips_same_type = std::nullopt;

    // always blocks vis, even if it normally wouldn't
    bool illusionary_visblocker = false;

    bool equals(const gamedef_t *game, const contentflags_t &other) const;

    // is any kind of detail? (solid, liquid, etc.)
    bool is_any_detail(const gamedef_t *game) const;
    bool is_detail_solid(const gamedef_t *game) const;
    bool is_detail_fence(const gamedef_t *game) const;
    bool is_detail_illusionary(const gamedef_t *game) const;
    
    bool is_mirrored(const gamedef_t *game) const;
    contentflags_t &set_mirrored(const std::optional<bool> &mirror_inside_value) { mirror_inside = mirror_inside_value; return *this; }
    
    inline bool will_clip_same_type(const gamedef_t *game) const { return will_clip_same_type(game, *this); }
    bool will_clip_same_type(const gamedef_t *game, const contentflags_t &other) const;
    contentflags_t &set_clips_same_type(const std::optional<bool> &clips_same_type_value) { clips_same_type = clips_same_type_value; return *this; }

    bool is_empty(const gamedef_t *game) const;

    // detail solid or structural solid
    inline bool is_any_solid(const gamedef_t *game) const {
        return is_solid(game)
            || is_detail_solid(game);
    }

    // solid, not detail or any other extended content types
    bool is_solid(const gamedef_t *game) const;
    bool is_sky(const gamedef_t *game) const;
    bool is_liquid(const gamedef_t *game) const;
    bool is_valid(const gamedef_t *game, bool strict = true) const;
    bool is_clip(const gamedef_t *game) const;
    bool is_origin(const gamedef_t *game) const;

    void make_valid(const gamedef_t *game);

    inline bool is_fence(const gamedef_t *game) const {
        return is_detail_fence(game) || is_detail_illusionary(game);
    }

    // check if this content's `type` - which is distinct from various
    // flags that turn things on/off - match. Exactly what the native
    // "type" is depends on the game, but any of the detail flags must
    // also match.
    bool types_equal(const contentflags_t &other, const gamedef_t *game) const;

    // when multiple brushes contribute to a leaf, the higher priority
    // one determines the leaf contents
    int32_t priority(const gamedef_t *game) const;

    // whether this should chop (if so, only lower priority content brushes get chopped)
    // should return true only for solid / opaque content types
    bool chops(const gamedef_t *game) const;

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

/*
* Clipnodes need to be stored as a 16-bit offset. Originally, this was a
* signed value and only the positive values up to 32767 were available. Since
* the negative range was unused apart from a few values reserved for flags,
* this has been extended to allow up to 65520 (0xfff0) clipnodes (with a
* suitably modified engine).
*/
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
enum q1_surf_flags_t : int32_t
{
    TEX_SPECIAL = nth_bit(0) /* sky or slime, no lightmap or 256 subdivision */
};

// Q2 Texture flags.
enum q2_surf_flags_t : int32_t
{
    Q2_SURF_LIGHT = nth_bit(0), // value will hold the light strength

    Q2_SURF_SLICK = nth_bit(1), // effects game physics

    Q2_SURF_SKY = nth_bit(2), // don't draw, but add to skybox
    Q2_SURF_WARP = nth_bit(3), // turbulent water warp
    Q2_SURF_TRANS33 = nth_bit(4),
    Q2_SURF_TRANS66 = nth_bit(5),
    Q2_SURF_FLOWING = nth_bit(6), // scroll towards angle
    Q2_SURF_NODRAW = nth_bit(7), // don't bother referencing the texture

    Q2_SURF_HINT = nth_bit(8), // make a primary bsp splitter
    Q2_SURF_SKIP = nth_bit(9), // ONLY FOR HINT! "nodraw" = Q1 "skip"

    Q2_SURF_TRANSLUCENT = (Q2_SURF_TRANS33 | Q2_SURF_TRANS66), // mxd
};

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

struct texinfo_t
{
    texvecf vecs; /* [s/t][xyz offset] */
    int32_t miptex;
    int32_t flags;

    texinfo_t() = default;

    // convert from mbsp_t
    texinfo_t(const mtexinfo_t &model) : vecs(model.vecs), miptex(model.miptex), flags(model.flags.native) { }

    // convert to mbsp_t
    operator mtexinfo_t() const { return {vecs, {flags}, miptex}; }

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
    q2_texinfo_t(const mtexinfo_t &model)
        : vecs(model.vecs), flags(model.flags.native), value(model.value), texture(model.texture),
          nexttexinfo(model.nexttexinfo)
    {
    }

    // convert to mbsp_t
    operator mtexinfo_t() const { return {vecs, {flags}, -1, value, texture, nexttexinfo}; }

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
    auto tuple() const { return std::tie(portalnum, otherarea); }

    // comparison operator for tests
    bool operator==(const dareaportal_t &other) const { return tuple() == other.tuple(); }
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
    virtual int32_t contents_priority(const contentflags_t &contents) const = 0;
    virtual bool chops(const contentflags_t &) const = 0;
    virtual contentflags_t create_empty_contents() const = 0;
    virtual contentflags_t create_solid_contents() const = 0;
    virtual contentflags_t create_detail_illusionary_contents(const contentflags_t &original) const = 0;
    virtual contentflags_t create_detail_fence_contents(const contentflags_t &original) const = 0;
    virtual contentflags_t create_detail_solid_contents(const contentflags_t &original) const = 0;
    virtual bool contents_are_type_equal(const contentflags_t &self, const contentflags_t &other) const = 0;
    virtual bool contents_are_equal(const contentflags_t &self, const contentflags_t &other) const = 0;
    virtual bool contents_are_any_detail(const contentflags_t &contents) const = 0;
    virtual bool contents_are_detail_solid(const contentflags_t &contents) const = 0;
    virtual bool contents_are_detail_fence(const contentflags_t &contents) const = 0;
    virtual bool contents_are_detail_illusionary(const contentflags_t &contents) const = 0;
    virtual bool contents_are_mirrored(const contentflags_t &contents) const = 0;
    virtual bool contents_are_origin(const contentflags_t &contents) const = 0;
    virtual bool contents_are_clip(const contentflags_t &contents) const = 0;
    virtual bool contents_are_empty(const contentflags_t &contents) const = 0;
    virtual bool contents_clip_same_type(const contentflags_t &self, const contentflags_t &other) const = 0;
    virtual bool contents_are_solid(const contentflags_t &contents) const = 0;
    virtual bool contents_are_sky(const contentflags_t &contents) const = 0;
    virtual bool contents_are_liquid(const contentflags_t &contents) const = 0;
    virtual bool contents_are_valid(const contentflags_t &contents, bool strict = true) const = 0;
    virtual bool portal_can_see_through(const contentflags_t &contents0, const contentflags_t &contents1, bool transwater, bool transsky) const = 0;
    virtual bool contents_seals_map(const contentflags_t &contents) const = 0;
    virtual contentflags_t contents_remap_for_export(const contentflags_t &contents) const = 0;
    virtual contentflags_t combine_contents(const contentflags_t &a, const contentflags_t &b) const = 0;
    virtual std::string get_contents_display(const contentflags_t &contents) const = 0;
    virtual void contents_make_valid(contentflags_t &contents) const = 0;
    virtual const std::initializer_list<aabb3d> &get_hull_sizes() const = 0;
    virtual contentflags_t face_get_contents(
        const std::string &texname, const surfflags_t &flags, const contentflags_t &contents) const = 0;
    virtual void init_filesystem(const fs::path &source, const settings::common_settings &settings) const = 0;
    virtual const std::vector<qvec3b> &get_default_palette() const = 0;
    virtual std::any create_content_stats() const = 0;
    virtual void count_contents_in_stats(const contentflags_t &contents, std::any &stats) const = 0;
    virtual void print_content_stats(const std::any &stats, const char *what) const = 0;
};

constexpr int32_t NO_VERSION = -1;

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

void LoadBSPFile(fs::path &filename, bspdata_t *bspdata); // returns the filename as contained inside a bsp
void WriteBSPFile(const fs::path &filename, bspdata_t *bspdata);
void PrintBSPFileSizes(const bspdata_t *bspdata);
/**
 * Returns false if the conversion failed.
 */
bool ConvertBSPFormat(bspdata_t *bspdata, const bspversion_t *to_version);
