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

#include <atomic>
#include <cstdint>
#include <array>
#include <tuple>
#include <variant>
#include <vector>
#include <unordered_map>
#include <any>
#include <optional>
#include <span>
#include <mutex>

#include <json/json.h>

#include <common/bitflags.hh>
#include <common/fs.hh>
#include <common/qvec.hh>
#include <common/aabb.hh>

namespace settings
{
class common_settings;
}

struct lump_t
{
    int32_t fileofs;
    int32_t filelen;

    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s);
};

using contents_int_t = uint64_t;

/**
 * Superset of Q1- and Q2- features, plus EWT extensions.
 *
 * Update bitflag_names if this is changed.
 */
enum contents_t : contents_int_t
{
    EWT_VISCONTENTS_EMPTY = 0,
    /** an eye is never valid in a solid */
    EWT_VISCONTENTS_SOLID = nth_bit<uint64_t>(0),
    EWT_VISCONTENTS_SKY = nth_bit<uint64_t>(1),
    /** eye never valid inside. always detail, doesn't split other faces. (func_detail_wall) */
    EWT_VISCONTENTS_DETAIL_WALL = nth_bit<uint64_t>(2),
    /** translucent, but not watery. eye valid inside. (func_detail_fence) */
    EWT_VISCONTENTS_WINDOW = nth_bit<uint64_t>(3),
    /** Visblocking mist, but doesn't merge with mist/aux. (func_illusionary_visblocker) */
    EWT_VISCONTENTS_ILLUSIONARY_VISBLOCKER = nth_bit<uint64_t>(4),
    /** Mist but not mirrored by default. Doesn't merge with mist. Never visblocking / always detail. */
    EWT_VISCONTENTS_AUX = nth_bit<uint64_t>(5),
    EWT_VISCONTENTS_LAVA = nth_bit<uint64_t>(6),
    EWT_VISCONTENTS_SLIME = nth_bit<uint64_t>(7),
    EWT_VISCONTENTS_WATER = nth_bit<uint64_t>(8),
    /** Never visblocking / always detail. (func_detail_illusionary). */
    EWT_VISCONTENTS_MIST = nth_bit<uint64_t>(9),

    EWT_LAST_VISIBLE_CONTENTS_INDEX = 9,
    EWT_LAST_VISIBLE_CONTENTS = EWT_VISCONTENTS_MIST,

    /** removed before bsping an entity */
    EWT_INVISCONTENTS_ORIGIN = nth_bit<uint64_t>(10),
    /** Q1 clip */
    EWT_INVISCONTENTS_PLAYERCLIP = nth_bit<uint64_t>(11),
    EWT_INVISCONTENTS_MONSTERCLIP = nth_bit<uint64_t>(12),
    EWT_INVISCONTENTS_AREAPORTAL = nth_bit<uint64_t>(13),
    /** re-release */
    EWT_INVISCONTENTS_NO_WATERJUMP = nth_bit<uint64_t>(14),
    /** re-release */
    EWT_INVISCONTENTS_PROJECTILECLIP = nth_bit<uint64_t>(15),

    EWT_CFLAG_MIRROR_INSIDE = nth_bit<uint64_t>(16),
    EWT_CFLAG_MIRROR_INSIDE_SET = nth_bit<uint64_t>(17),
    EWT_CFLAG_SUPPRESS_CLIPPING_SAME_TYPE = nth_bit<uint64_t>(18),

    EWT_CFLAG_CURRENT_0 = nth_bit<uint64_t>(19),
    EWT_CFLAG_CURRENT_90 = nth_bit<uint64_t>(20),
    EWT_CFLAG_CURRENT_180 = nth_bit<uint64_t>(21),
    EWT_CFLAG_CURRENT_270 = nth_bit<uint64_t>(22),
    EWT_CFLAG_CURRENT_UP = nth_bit<uint64_t>(23),
    EWT_CFLAG_CURRENT_DOWN = nth_bit<uint64_t>(24),
    /** auto set if any surface has trans, */
    EWT_CFLAG_TRANSLUCENT = nth_bit<uint64_t>(25),
    EWT_CFLAG_LADDER = nth_bit<uint64_t>(26),
    /** disallowed in maps, only for gamecode use */
    EWT_CFLAG_MONSTER = nth_bit<uint64_t>(27),
    /** disallowed in maps, only for gamecode use */
    EWT_CFLAG_DEADMONSTER = nth_bit<uint64_t>(28),
    /** brushes to be added after vis leafs */
    EWT_CFLAG_DETAIL = nth_bit<uint64_t>(29),

    // unused Q2 contents bits - just present here so we can roundtrip all 32-bit Q2 contents
    EWT_CFLAG_Q2_UNUSED_7 = nth_bit<uint64_t>(30),
    EWT_CFLAG_Q2_UNUSED_8 = nth_bit<uint64_t>(31),
    EWT_CFLAG_Q2_UNUSED_9 = nth_bit<uint64_t>(32),
    EWT_CFLAG_Q2_UNUSED_10 = nth_bit<uint64_t>(33),
    EWT_CFLAG_Q2_UNUSED_11 = nth_bit<uint64_t>(34),
    EWT_CFLAG_Q2_UNUSED_12 = nth_bit<uint64_t>(35),
    EWT_CFLAG_Q2_UNUSED_30 = nth_bit<uint64_t>(36),
    EWT_CFLAG_Q2_UNUSED_31 = nth_bit<uint64_t>(37),

    // masks
    EWT_ALL_LIQUIDS = EWT_VISCONTENTS_LAVA | EWT_VISCONTENTS_SLIME | EWT_VISCONTENTS_WATER,

    EWT_ALL_VISIBLE_CONTENTS = EWT_VISCONTENTS_SOLID | EWT_VISCONTENTS_SKY | EWT_VISCONTENTS_DETAIL_WALL |
                               EWT_VISCONTENTS_WINDOW | EWT_VISCONTENTS_ILLUSIONARY_VISBLOCKER | EWT_VISCONTENTS_AUX |
                               EWT_VISCONTENTS_LAVA | EWT_VISCONTENTS_SLIME | EWT_VISCONTENTS_WATER |
                               EWT_VISCONTENTS_MIST,

    // FIXME: out of date
    EWT_ALL_INVISCONTENTS = EWT_INVISCONTENTS_ORIGIN | EWT_INVISCONTENTS_PLAYERCLIP | EWT_INVISCONTENTS_MONSTERCLIP |
                            EWT_INVISCONTENTS_AREAPORTAL | EWT_INVISCONTENTS_PROJECTILECLIP,
};

struct gamedef_t;

struct contentflags_t
{
    contents_t flags;

    static contentflags_t make(contents_int_t f) { return contentflags_t{.flags = static_cast<contents_t>(f)}; }

    static contentflags_t create_detail_illusionary_contents(contentflags_t original)
    {
        contents_int_t flags = original.flags;
        flags &= ~(EWT_VISCONTENTS_SOLID | EWT_CFLAG_MIRROR_INSIDE);
        flags |= EWT_VISCONTENTS_MIST | EWT_CFLAG_TRANSLUCENT | EWT_CFLAG_DETAIL | EWT_CFLAG_MIRROR_INSIDE_SET;
        // start with mirror_inside off; we'll turn it back on if the mapper requested it
        return contentflags_t::make(flags);
    }

    static contentflags_t create_detail_fence_contents(contentflags_t original)
    {
        contents_int_t flags = original.flags;
        flags &= ~EWT_VISCONTENTS_SOLID;
        flags |= (EWT_VISCONTENTS_WINDOW | EWT_CFLAG_TRANSLUCENT | EWT_CFLAG_DETAIL | EWT_CFLAG_MIRROR_INSIDE_SET);
        // start with mirror_inside off; we'll turn it back on if the mapper requested it
        return contentflags_t::make(flags);
    }

    static contentflags_t create_detail_wall_contents(contentflags_t original)
    {
        contents_int_t flags = original.flags;
        flags &= ~EWT_VISCONTENTS_SOLID;
        flags |= (EWT_VISCONTENTS_DETAIL_WALL | EWT_CFLAG_DETAIL);
        return contentflags_t::make(flags);
    }

    static contentflags_t create_detail_solid_contents(contentflags_t original)
    {
        contents_int_t flags = original.flags;
        flags |= (EWT_VISCONTENTS_SOLID | EWT_CFLAG_DETAIL);
        return contentflags_t::make(flags);
    }

    bool equals(const gamedef_t *game, contentflags_t other) const;

    // is any kind of detail? (solid, liquid, etc.)
    bool is_any_detail() const;
    // is detail and is solid
    bool is_detail_solid() const;
    bool is_detail_wall() const;
    bool is_detail_fence() const;
    bool is_detail_illusionary() const;

    std::optional<bool> mirror_inside() const
    {
        if (flags & EWT_CFLAG_MIRROR_INSIDE_SET) {
            return {(flags & EWT_CFLAG_MIRROR_INSIDE) != 0};
        }
        return std::nullopt;
    }
    contentflags_t &set_mirrored(const std::optional<bool> &mirror_inside_value);

    std::optional<bool> clips_same_type() const
    {
        if (flags & EWT_CFLAG_SUPPRESS_CLIPPING_SAME_TYPE) {
            return {false};
        }
        return std::nullopt;
    }
    contentflags_t &set_clips_same_type(const std::optional<bool> &clips_same_type_value);

    bool is_empty() const;
    bool is_any_solid() const;
    // solid, not detail
    bool is_solid() const;
    bool has_structural_solid() const { return (flags & EWT_VISCONTENTS_SOLID) && !(flags & EWT_CFLAG_DETAIL); }
    // FIXME: checks for "sky" bit, but sky might not be the visible contents so "is_sky()" is a misonomer
    bool is_sky() const;
    // NOTE: unlike the other is_*() checks, this one checks the visible contents
    bool is_liquid() const;
    // FIXME: checks for "clip" bits (player or monster), but is_clip() makes it sound like an exclusive check.
    bool is_clip() const;
    bool is_origin() const;
    bool is_opaque(const gamedef_t *game, bool transwater) const;

    bool is_fence() const;

    // check if this content's `type` - which is distinct from various
    // flags that turn things on/off - match. Exactly what the native
    // "type" is depends on the game, but any of the detail flags must
    // also match.
    bool types_equal(contentflags_t other) const;

    inline contents_int_t get_content_type() const
    {
        return flags & (EWT_ALL_VISIBLE_CONTENTS | EWT_ALL_INVISCONTENTS);
    }

    contentflags_t cluster_contents(contentflags_t other) const;
    static contentflags_t combine_contents(contentflags_t a, contentflags_t b);
    static bool portal_can_see_through(contentflags_t contents0, contentflags_t contents1);
    // for a portal with contents from `a` to `b`, returns what type of face should be rendered facing `a` and `b`
    static contentflags_t portal_visible_contents(contentflags_t a, contentflags_t b);
    // for a brush with the given contents touching a portal with the required `portal_visible_contents`, as determined
    // by portal_visible_contents, should the `brushside_side` of the brushside generate a face? e.g. liquids generate
    // front and back sides by default, but for q1 detail_wall/detail_illusionary the back side is opt-in with
    // _mirrorinside
    static bool portal_generates_face(
        contentflags_t portal_visible_contents, contentflags_t brushcontents, planeside_t brushside_side);

    std::string to_string() const;

    Json::Value to_json() const;
    static contentflags_t from_json(const Json::Value &json);

    // returns the bit index (starting from 0) of the strongest visible content type
    // set, or -1 if no visible content bits are set (i.e. EWT_VISCONTENTS_EMPTY)
    int visible_contents_index() const
    {
        for (uint32_t index = 0; nth_bit(index) <= EWT_LAST_VISIBLE_CONTENTS; ++index) {
            if (flags & nth_bit(index)) {
                return index;
            }
        }

        if (flags & EWT_INVISCONTENTS_PLAYERCLIP) {
            return 10;
        } else if (flags & EWT_INVISCONTENTS_MONSTERCLIP) {
            return 11;
        } else if (flags & EWT_INVISCONTENTS_PROJECTILECLIP) {
            return 14;
        }

        return -1;
    }

    // returns the strongest EWT_VISCONTENTS_ bit, discarding all other flags
    contentflags_t visible_contents() const
    {
        int index = visible_contents_index();
        if (index >= 0) {
            return contentflags_t::make(static_cast<contents_t>(nth_bit(index)));
        }
        return contentflags_t::make(EWT_VISCONTENTS_EMPTY);
    }

    bool seals_map() const { return is_solid() || is_sky(); }

    auto operator<=>(const contentflags_t &other) const = default;
};

// gtest support
std::ostream &operator<<(std::ostream &os, contents_t flags);

enum q1_surf_flags_t : int32_t;
enum q2_surf_flags_t : int32_t;

/**
 * Superset of all surface flags for all supported games, plus extended EWT-specific flags
 */
struct surfflags_t
{
    // native flags value; what's written to the BSP for a Q2 map basically
    // when compiling Q1 maps, we can use these internally but obviously not write them out
    q2_surf_flags_t native_q2 = static_cast<q2_surf_flags_t>(0);

    // native q1 flags
    q1_surf_flags_t native_q1 = static_cast<q1_surf_flags_t>(0);

    // an invisible surface (Q1 "skip" texture, Q2 SURF_NODRAW)
    bool is_nodraw() const;

    void set_nodraw(bool nodraw);

    // hint surface
    bool is_hint() const;

    void set_hint(bool hint);

    // is a skip surface from a hint brush
    bool is_hintskip() const;

    void set_hintskip(bool hintskip);

    // don't receive dirtmapping
    bool no_dirt : 1 = false;

    // don't cast a shadow
    bool no_shadow : 1 = false;

    // light doesn't bounce off this face
    bool no_bounce : 1 = false;

    // opt out of minlight on this face (including opting out of local minlight, so
    // not the same as just setting minlight to 0).
    bool no_minlight : 1 = false;

    // don't expand this face for larger clip hulls
    bool no_expand : 1 = false;

    // this face doesn't receive light
    bool light_ignore : 1 = false;

    bool noambient : 1 = false;

    // if true, rescales any surface light emitted by these brushes to emit 50% light at 90 degrees from the surface
    // normal if false, use a more natural angle falloff of 0% at 90 degrees
    std::optional<bool> surflight_rescale;

    // override surface lighting style
    std::optional<int32_t> surflight_style;

    // override surface lighting targetname
    std::optional<std::string> surflight_targetname;

    // override the textures' surflight color
    std::optional<qvec3b> surflight_color;

    // surface light rescaling
    std::optional<float> surflight_minlight_scale;

    // surface light attenuation
    std::optional<float> surflight_atten;

    // if non zero, enables phong shading and gives the angle threshold to use
    float phong_angle = 0.0f;

    // if non zero, overrides _phong_angle for concave joints
    float phong_angle_concave = 0.0f;

    // _phong_group key, equivalent q2 map format's use of the "value" field
    int phong_group = 0;

    // minlight value for this face. empty = inherit from worldspawn.
    std::optional<float> minlight;

    // red minlight colors for this face
    qvec3b minlight_color = qvec3b(0, 0, 0);

    // custom opacity
    std::optional<float> light_alpha;

    // two-sided lighting
    std::optional<bool> light_twosided;

    // maxlight value for this face
    float maxlight = 0.0;

    // light color scale
    float lightcolorscale = 1.0;

    // surface light group
    int32_t surflight_group = 0;

    // custom world_units_per_luxel for this geometry
    std::optional<float> world_units_per_luxel;

    std::optional<int32_t> object_channel_mask;

public:
    // sort support
    auto operator<=>(const surfflags_t &other) const = default;

    bool is_valid(const gamedef_t *game) const;

public:
    // to/from json

    Json::Value to_json() const;
    static surfflags_t from_json(const Json::Value &json);

private:
    void set_native_q1_bits(q1_surf_flags_t bits);
    void set_native_q2_bits(q2_surf_flags_t bits);
};

struct mbsp_t;
std::vector<surfflags_t> LoadExtendedTexinfoFlags(const fs::path &sourcefilename, const mbsp_t *bsp);
std::vector<contentflags_t> LoadExtendedContentFlags(const fs::path &sourcefilename, const mbsp_t *bsp);

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

struct content_stats_t
{
private:
    std::mutex stat_mutex;
    std::unordered_map<contents_t, size_t> native_types;
    std::atomic<size_t> total_brushes;

public:
    void count_contents_in_stats(contentflags_t contents);
    void print_content_stats(const char *what) const;
};

// Game definition, which contains data specific to
// the game a BSP version is being compiled for.
struct gamedef_t
{
    virtual ~gamedef_t() = default;

    // friendly name, used for commands
    const char *friendly_name;

    // ID, used for quick comparisons
    gameid_t id = GAME_UNKNOWN;

    // whether the game uses an RGB lightmap or not
    bool has_rgb_lightmap = false;

    // whether the game supports content flags on brush models
    bool allow_contented_bmodels = false;

    // base dir for searching for paths, in case we are in a mod dir
    // note: we need this to be able to be overridden via options
    const std::string default_base_dir = {};

    // max values of entity key & value pairs, only used for
    // printing warnings.
    size_t max_entity_key = 32;
    size_t max_entity_value = 128;

    gamedef_t(const char *friendly_name, const char *default_base_dir);

    // surface stores lightmap/luxel color data
    virtual bool surf_is_lightmapped(
        const surfflags_t &flags, const char *texname, bool light_nodraw, bool lightgrid_enabled) const = 0;
    // surface can be emissive
    virtual bool surf_is_emissive(const surfflags_t &flags, const char *texname) const = 0;
    virtual bool surf_is_subdivided(const surfflags_t &flags) const = 0;
    virtual bool surfflags_are_valid(const surfflags_t &flags) const = 0;
    /**
     * We block certain surface flag combinations from ever smoothing together
     * e.g. warping and non-warping
     */
    virtual bool surfflags_may_phong(const surfflags_t &a, const surfflags_t &b) const = 0;
    virtual int32_t surfflags_from_string(std::string_view str) const = 0;
    virtual contentflags_t create_contents_from_native(int32_t native) const = 0;
    virtual int32_t contents_to_native(contentflags_t contents) const = 0;
    virtual int32_t contents_from_string(std::string_view str) const = 0;
    enum class remap_type_t
    {
        brush,
        leaf
    };
    virtual contentflags_t contents_remap_for_export(contentflags_t contents, remap_type_t type) const = 0;
    virtual std::span<const aabb3d> get_hull_sizes() const = 0;
    virtual contentflags_t face_get_contents(
        const std::string &texname, const surfflags_t &flags, contentflags_t contents, bool transwater) const = 0;
    virtual void init_filesystem(const fs::path &source, const settings::common_settings &settings) const = 0;
    virtual const std::vector<qvec3b> &get_default_palette() const = 0;
};

// Lump specification; stores the name and size
// of an individual entry in the lump. Count is
// calculated as (lump_size / size)
struct lumpspec_t
{
    const char *name;
    size_t size;
};

// BSP version struct & instances
struct bspversion_t
{
    /* identifier value, the first int32_t in the header */
    int32_t ident;
    /* version value, if supported */
    std::optional<int32_t> version;
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
            fmt::format_to(ctx.out(), "{} ", v.name);
        }

        // Q2-esque BSPs are printed as, ex, IBSP:38
        if (v.version.has_value()) {
            char ident[5] = {(char)(v.ident & 0xFF), (char)((v.ident >> 8) & 0xFF), (char)((v.ident >> 16) & 0xFF),
                (char)((v.ident >> 24) & 0xFF), '\0'};
            return fmt::format_to(ctx.out(), "{}:{}", ident, v.version.value());
        }

        // Q1-esque BSPs are printed as, ex, bsp29
        return fmt::format_to(ctx.out(), "{}", v.short_name);
    }
};

struct texvecf : qmat<float, 2, 4>
{
    using qmat<float, 2, 4>::qmat;

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

    void stream_read(std::istream &stream);
    void stream_write(std::ostream &stream) const;
};

// Fmt support
template<>
struct fmt::formatter<texvecf> : fmt::formatter<qmat<float, 2, 4>>
{
};

// type to store a hull index; max 256 hulls, zero is valid.
using hull_index_t = std::optional<uint8_t>;
