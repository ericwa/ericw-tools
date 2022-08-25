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

// Special contents flags for the compiler only
enum extended_cflags_t : uint16_t
{
    CFLAGS_STRUCTURAL_COVERED_BY_DETAIL = nth_bit(0u),
    CFLAGS_WAS_ILLUSIONARY = nth_bit(1), /* was illusionary, got changed to something else */
    CFLAGS_BMODEL_MIRROR_INSIDE = nth_bit(3), /* set "_mirrorinside" "1" on a bmodel to mirror faces for when the player is inside. */
    CFLAGS_NO_CLIPPING_SAME_TYPE = nth_bit(4), /* Don't clip the same content type. mostly intended for CONTENTS_DETAIL_ILLUSIONARY */
    // only one of these flags below should ever be set.
    CFLAGS_HINT = nth_bit(5),
    CFLAGS_CLIP = nth_bit(6),
    CFLAGS_ORIGIN = nth_bit(7),
    CFLAGS_DETAIL = nth_bit(8),
    CFLAGS_DETAIL_ILLUSIONARY = nth_bit(9),
    CFLAGS_DETAIL_FENCE = nth_bit(10),
    CFLAGS_ILLUSIONARY_VISBLOCKER = nth_bit(11),
    // all of the detail values
    CFLAGS_DETAIL_MASK = (CFLAGS_DETAIL | CFLAGS_DETAIL_ILLUSIONARY | CFLAGS_DETAIL_FENCE),
    // all of the special content types
    CFLAGS_CONTENTS_MASK =
        (CFLAGS_HINT | CFLAGS_CLIP | CFLAGS_ORIGIN | CFLAGS_DETAIL_MASK | CFLAGS_ILLUSIONARY_VISBLOCKER)
};

struct gamedef_t;

struct contentflags_t
{
    // native flags value; what's written to the BSP basically
    int32_t native;

    // extra flags, specific to BSP only
    uint16_t extended;

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

    // maxlight value for this face
    vec_t maxlight;

    constexpr bool needs_write() const
    {
        return no_dirt || no_shadow || no_bounce || no_minlight || no_expand || light_ignore || phong_angle ||
               phong_angle_concave || minlight || !qv::emptyExact(minlight_color) || light_alpha || maxlight;
    }

private:
    constexpr auto as_tuple() const
    {
        return std::tie(native, is_skip, is_hint, no_dirt, no_shadow, no_bounce, no_minlight, no_expand, light_ignore,
            phong_angle, phong_angle_concave, minlight, minlight_color, light_alpha, maxlight);
    }

public:
    // sort support
    constexpr bool operator<(const surfflags_t &other) const { return as_tuple() < other.as_tuple(); }

    constexpr bool operator>(const surfflags_t &other) const { return as_tuple() > other.as_tuple(); }

    bool is_valid(const gamedef_t *game) const;
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

    gamedef_t(const char *default_base_dir) : default_base_dir(default_base_dir) { }

    virtual bool surf_is_lightmapped(const surfflags_t &flags) const = 0;
    virtual bool surf_is_subdivided(const surfflags_t &flags) const = 0;
    virtual bool surfflags_are_valid(const surfflags_t &flags) const = 0;
    virtual int32_t surfflags_from_string(const std::string_view &str) const = 0;
    // FIXME: fix so that we don't have to pass a name here
    virtual bool texinfo_is_hintskip(const surfflags_t &flags, const std::string &name) const = 0;
    virtual contentflags_t cluster_contents(const contentflags_t &contents0, const contentflags_t &contents1) const = 0;
    virtual int32_t get_content_type(const contentflags_t &contents) const = 0;
    virtual int32_t contents_priority(const contentflags_t &contents) const = 0;
    virtual contentflags_t create_extended_contents(const uint16_t &cflags = 0) const = 0;
    virtual contentflags_t create_empty_contents(const uint16_t &cflags = 0) const = 0;
    virtual contentflags_t create_solid_contents(const uint16_t &cflags = 0) const = 0;
    virtual contentflags_t create_sky_contents(const uint16_t &cflags = 0) const = 0;
    virtual contentflags_t create_liquid_contents(const int32_t &liquid_type, const uint16_t &cflags = 0) const = 0;
    virtual bool contents_are_empty(const contentflags_t &contents) const = 0;
    virtual bool contents_are_solid(const contentflags_t &contents) const = 0;
    virtual bool contents_are_sky(const contentflags_t &contents) const = 0;
    virtual bool contents_are_liquid(const contentflags_t &contents) const = 0;
    virtual bool contents_are_valid(const contentflags_t &contents, bool strict = true) const = 0;
    virtual int32_t contents_from_string(const std::string_view &str) const = 0;
    virtual bool portal_can_see_through(const contentflags_t &contents0, const contentflags_t &contents1) const = 0;
    virtual std::string get_contents_display(const contentflags_t &contents) const = 0;
    virtual const std::initializer_list<aabb3d> &get_hull_sizes() const = 0;
    virtual contentflags_t face_get_contents(
        const std::string &texname, const surfflags_t &flags, const contentflags_t &contents) const = 0;
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
            format_to(ctx.out(), "{} ", v.name);
        }

        // Q2-esque BSPs are printed as, ex, IBSP:38
        if (v.version.has_value()) {
            char ident[5] = { (char) (v.ident & 0xFF), (char) ((v.ident >> 8) & 0xFF), (char) ((v.ident >> 16) & 0xFF), (char) ((v.ident >> 24) & 0xFF), '\0' };
            return format_to(ctx.out(), "{}:{}", ident, v.version.value());
        }

        // Q1-esque BSPs are printed as, ex, bsp29
        return format_to(ctx.out(), "{}", v.short_name);
    }
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

#include "bspfile_generic.hh"
#include "bspfile_q1.hh"
#include "bspfile_q2.hh"
#include "bspxfile.hh"

using bspxentries_t = std::unordered_map<std::string, std::vector<uint8_t>>;

struct bspdata_t
{
    const bspversion_t *version, *loadversion;

    // Stay in monostate until a BSP type is requested.
    std::variant<std::monostate, mbsp_t, bsp29_t, bsp2rmq_t, bsp2_t, q2bsp_t, q2bsp_qbism_t> bsp;

    // This can be used with any BSP format.
    struct
    {
        bspxentries_t entries;
        
        // transfer ownership of the vector into a BSPX lump
        inline void transfer(const char *xname, std::vector<uint8_t> &xdata)
        {
            entries.insert_or_assign(xname, std::move(xdata));
        }
        
        // transfer ownership of the vector into a BSPX lump
        inline void transfer(const char *xname, std::vector<uint8_t> &&xdata)
        {
            entries.insert_or_assign(xname, xdata);
        }
    } bspx;
};

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
