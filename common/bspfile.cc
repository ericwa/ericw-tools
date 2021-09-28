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

#include <common/cmdlib.hh>
#include <common/mathlib.hh>
#include <common/bspfile.hh>
#include <cstdint>
#include <limits.h>

#include <fmt/format.h>

/*
 * =========================================================================
 * ...
 * =========================================================================
 */

constexpr lumpspec_t lumpspec_bsp29[] = {
    {"entities", sizeof(char)},
    {"planes", sizeof(dplane_t)},
    {"texture", sizeof(uint8_t)},
    {"vertexes", sizeof(dvertex_t)},
    {"visibility", sizeof(uint8_t)},
    {"nodes", sizeof(bsp29_dnode_t)},
    {"texinfos", sizeof(texinfo_t)},
    {"faces", sizeof(bsp29_dface_t)},
    {"lighting", sizeof(uint8_t)},
    {"clipnodes", sizeof(bsp29_dclipnode_t)},
    {"leafs", sizeof(bsp29_dleaf_t)},
    {"marksurfaces", sizeof(uint16_t)},
    {"edges", sizeof(bsp29_dedge_t)},
    {"surfedges", sizeof(int32_t)},
    {"models", sizeof(dmodelq1_t)},
};

constexpr lumpspec_t lumpspec_bsp2rmq[] = {
    {"entities", sizeof(char)},
    {"planes", sizeof(dplane_t)},
    {"texture", sizeof(uint8_t)},
    {"vertexes", sizeof(dvertex_t)},
    {"visibility", sizeof(uint8_t)},
    {"nodes", sizeof(bsp2rmq_dnode_t)},
    {"texinfos", sizeof(texinfo_t)},
    {"faces", sizeof(bsp2_dface_t)},
    {"lighting", sizeof(uint8_t)},
    {"clipnodes", sizeof(bsp2_dclipnode_t)},
    {"leafs", sizeof(bsp2rmq_dleaf_t)},
    {"marksurfaces", sizeof(uint32_t)},
    {"edges", sizeof(bsp2_dedge_t)},
    {"surfedges", sizeof(int32_t)},
    {"models", sizeof(dmodelq1_t)},
};

constexpr lumpspec_t lumpspec_bsp2[] = {
    {"entities", sizeof(char)},
    {"planes", sizeof(dplane_t)},
    {"texture", sizeof(uint8_t)},
    {"vertexes", sizeof(dvertex_t)},
    {"visibility", sizeof(uint8_t)},
    {"nodes", sizeof(bsp2_dnode_t)},
    {"texinfos", sizeof(texinfo_t)},
    {"faces", sizeof(bsp2_dface_t)},
    {"lighting", sizeof(uint8_t)},
    {"clipnodes", sizeof(bsp2_dclipnode_t)},
    {"leafs", sizeof(bsp2_dleaf_t)},
    {"marksurfaces", sizeof(uint32_t)},
    {"edges", sizeof(bsp2_dedge_t)},
    {"surfedges", sizeof(int32_t)},
    {"models", sizeof(dmodelq1_t)},
};

constexpr lumpspec_t lumpspec_bsp29_h2[] = {
    {"entities", sizeof(char)},
    {"planes", sizeof(dplane_t)},
    {"texture", sizeof(uint8_t)},
    {"vertexes", sizeof(dvertex_t)},
    {"visibility", sizeof(uint8_t)},
    {"nodes", sizeof(bsp29_dnode_t)},
    {"texinfos", sizeof(texinfo_t)},
    {"faces", sizeof(bsp29_dface_t)},
    {"lighting", sizeof(uint8_t)},
    {"clipnodes", sizeof(bsp29_dclipnode_t)},
    {"leafs", sizeof(bsp29_dleaf_t)},
    {"marksurfaces", sizeof(uint16_t)},
    {"edges", sizeof(bsp29_dedge_t)},
    {"surfedges", sizeof(int32_t)},
    {"models", sizeof(dmodelh2_t)},
};

constexpr lumpspec_t lumpspec_bsp2rmq_h2[] = {
    {"entities", sizeof(char)},
    {"planes", sizeof(dplane_t)},
    {"texture", sizeof(uint8_t)},
    {"vertexes", sizeof(dvertex_t)},
    {"visibility", sizeof(uint8_t)},
    {"nodes", sizeof(bsp2rmq_dnode_t)},
    {"texinfos", sizeof(texinfo_t)},
    {"faces", sizeof(bsp2_dface_t)},
    {"lighting", sizeof(uint8_t)},
    {"clipnodes", sizeof(bsp2_dclipnode_t)},
    {"leafs", sizeof(bsp2rmq_dleaf_t)},
    {"marksurfaces", sizeof(uint32_t)},
    {"edges", sizeof(bsp2_dedge_t)},
    {"surfedges", sizeof(int32_t)},
    {"models", sizeof(dmodelh2_t)},
};

constexpr lumpspec_t lumpspec_bsp2_h2[] = {
    {"entities", sizeof(char)},
    {"planes", sizeof(dplane_t)},
    {"texture", sizeof(uint8_t)},
    {"vertexes", sizeof(dvertex_t)},
    {"visibility", sizeof(uint8_t)},
    {"nodes", sizeof(bsp2_dnode_t)},
    {"texinfos", sizeof(texinfo_t)},
    {"faces", sizeof(bsp2_dface_t)},
    {"lighting", sizeof(uint8_t)},
    {"clipnodes", sizeof(bsp2_dclipnode_t)},
    {"leafs", sizeof(bsp2_dleaf_t)},
    {"marksurfaces", sizeof(uint32_t)},
    {"edges", sizeof(bsp2_dedge_t)},
    {"surfedges", sizeof(int32_t)},
    {"models", sizeof(dmodelh2_t)},
};

constexpr lumpspec_t lumpspec_q2bsp[] = {
    {"entities", sizeof(char)},
    {"planes", sizeof(dplane_t)},
    {"vertexes", sizeof(dvertex_t)},
    {"visibility", sizeof(uint8_t)},
    {"nodes", sizeof(q2_dnode_t)},
    {"texinfos", sizeof(q2_texinfo_t)},
    {"faces", sizeof(q2_dface_t)},
    {"lighting", sizeof(uint8_t)},
    {"leafs", sizeof(q2_dleaf_t)},
    {"leaffaces", sizeof(uint16_t)},
    {"leafbrushes", sizeof(uint16_t)},
    {"edges", sizeof(bsp29_dedge_t)},
    {"surfedges", sizeof(int32_t)},
    {"models", sizeof(q2_dmodel_t)},
    {"brushes", sizeof(dbrush_t)},
    {"brushsides", sizeof(q2_dbrushside_t)},
    {"pop", sizeof(uint8_t)},
    {"areas", sizeof(darea_t)},
    {"areaportals", sizeof(dareaportal_t)},
};

constexpr lumpspec_t lumpspec_qbism[] = {
    {"entities", sizeof(char)},
    {"planes", sizeof(dplane_t)},
    {"vertexes", sizeof(dvertex_t)},
    {"visibility", sizeof(uint8_t)},
    {"nodes", sizeof(q2_dnode_qbism_t)},
    {"texinfos", sizeof(q2_texinfo_t)},
    {"faces", sizeof(q2_dface_qbism_t)},
    {"lighting", sizeof(uint8_t)},
    {"leafs", sizeof(q2_dleaf_qbism_t)},
    {"leaffaces", sizeof(uint32_t)},
    {"leafbrushes", sizeof(uint32_t)},
    {"edges", sizeof(bsp2_dedge_t)},
    {"surfedges", sizeof(int32_t)},
    {"models", sizeof(q2_dmodel_t)},
    {"brushes", sizeof(dbrush_t)},
    {"brushsides", sizeof(q2_dbrushside_qbism_t)},
    {"pop", sizeof(uint8_t)},
    {"areas", sizeof(darea_t)},
    {"areaportals", sizeof(dareaportal_t)},
};

struct gamedef_generic_t : public gamedef_t
{
    gamedef_generic_t() :
        gamedef_t("")
    {
        id = GAME_UNKNOWN;
    }

    bool surf_is_lightmapped(const surfflags_t &) const { throw std::bad_cast(); }

    bool surf_is_subdivided(const surfflags_t &) const { throw std::bad_cast(); }

    contentflags_t cluster_contents(const contentflags_t &, const contentflags_t &) const { throw std::bad_cast(); }

    int32_t get_content_type(const contentflags_t &) const { throw std::bad_cast(); }

    int32_t contents_priority(const contentflags_t &) const { throw std::bad_cast(); }

    contentflags_t create_empty_contents(const int32_t &) const { throw std::bad_cast(); }

    contentflags_t create_solid_contents(const int32_t &) const { throw std::bad_cast(); }

    contentflags_t create_sky_contents(const int32_t &) const { throw std::bad_cast(); }

    contentflags_t create_liquid_contents(const int32_t &, const int32_t &) const { throw std::bad_cast(); }

    bool contents_are_liquid(const contentflags_t &) const { throw std::bad_cast(); }

    bool contents_are_valid(const contentflags_t &, bool) const { throw std::bad_cast(); }

    bool portal_can_see_through(const contentflags_t &, const contentflags_t &) const { throw std::bad_cast(); }

    std::string get_contents_display(const contentflags_t &contents) const { throw std::bad_cast(); }

    const std::initializer_list<aabb3d> &get_hull_sizes() const { throw std::bad_cast(); }
};

template<gameid_t ID>
struct gamedef_q1_like_t : public gamedef_t
{
    gamedef_q1_like_t(const char *base_dir = "ID1") :
        gamedef_t(base_dir)
    {
        this->id = ID;
        has_rgb_lightmap = false;
    }

    bool surf_is_lightmapped(const surfflags_t &flags) const { return !(flags.native & TEX_SPECIAL); }

    bool surf_is_subdivided(const surfflags_t &flags) const { return !(flags.native & TEX_SPECIAL); }

    contentflags_t cluster_contents(const contentflags_t &contents0, const contentflags_t &contents1) const
    {
        if (contents0 == contents1)
            return contents0;

        const int32_t merged_flags = contents0.extended | contents1.extended;

        /*
         * Clusters may be partially solid but still be seen into
         * ?? - Should we do something more explicit with mixed liquid contents?
         */
        if (contents0.native == CONTENTS_EMPTY || contents1.native == CONTENTS_EMPTY)
            return create_empty_contents(merged_flags);

        if (contents0.native >= CONTENTS_LAVA && contents0.native <= CONTENTS_WATER)
            return create_liquid_contents(contents0.native, merged_flags);
        if (contents1.native >= CONTENTS_LAVA && contents1.native <= CONTENTS_WATER)
            return create_liquid_contents(contents1.native, merged_flags);
        if (contents0.native == CONTENTS_SKY || contents1.native == CONTENTS_SKY)
            return create_sky_contents(merged_flags);

        return create_solid_contents(merged_flags);
    }

    int32_t get_content_type(const contentflags_t &contents) const { return contents.native; }

    int32_t contents_priority(const contentflags_t &contents) const
    {
        if (contents.extended & CFLAGS_DETAIL) {
            return 5;
        } else if (contents.extended & CFLAGS_DETAIL_ILLUSIONARY) {
            return 3;
        } else if (contents.extended & CFLAGS_DETAIL_FENCE) {
            return 4;
        } else if (contents.extended & CFLAGS_ILLUSIONARY_VISBLOCKER) {
            return 2;
        } else {
            switch (contents.native) {
                case CONTENTS_SOLID: return 7;

                case CONTENTS_SKY: return 6;

                case CONTENTS_WATER: return 2;
                case CONTENTS_SLIME: return 2;
                case CONTENTS_LAVA: return 2;

                case CONTENTS_EMPTY: return 1;
                case 0: return 0;

                default: FError("Bad contents in face"); return 0;
            }
        }
    }

    contentflags_t create_empty_contents(const int32_t &cflags) const { return {CONTENTS_EMPTY, cflags}; }

    contentflags_t create_solid_contents(const int32_t &cflags) const { return {CONTENTS_SOLID, cflags}; }

    contentflags_t create_sky_contents(const int32_t &cflags) const { return {CONTENTS_SKY, cflags}; }

    contentflags_t create_liquid_contents(const int32_t &liquid_type, const int32_t &cflags) const
    {
        return {liquid_type, cflags};
    }

    bool contents_are_liquid(const contentflags_t &contents) const
    {
        return contents.native <= CONTENTS_WATER && contents.native >= CONTENTS_LAVA;
    }

    bool contents_are_valid(const contentflags_t &contents, bool strict) const { return contents.native <= 0; }

    bool portal_can_see_through(const contentflags_t &contents0, const contentflags_t &contents1) const
    {
        /* If contents values are the same and not solid, can see through */
        return !(contents0.is_structural_solid(this) || contents1.is_structural_solid(this)) && contents0 == contents1;
    }

    std::string get_contents_display(const contentflags_t &contents) const
    {
        switch (contents.native) {
            case 0: return "UNSET";
            case CONTENTS_EMPTY: return "EMPTY";
            case CONTENTS_SOLID: return "SOLID";
            case CONTENTS_SKY: return "SKY";
            case CONTENTS_WATER: return "WATER";
            case CONTENTS_SLIME: return "SLIME";
            case CONTENTS_LAVA: return "LAVA";
            default: return fmt::to_string(contents.native);
        }
    }

    const std::initializer_list<aabb3d> &get_hull_sizes() const
    {
        static std::initializer_list<aabb3d> hulls = {
            {{0, 0, 0}, {0, 0, 0}},
            {{-16, -16, -32}, {16, 16, 24}},
            {{-32, -32, -64}, {32, 32, 24}}
        };

        return hulls;
    }
};

struct gamedef_h2_t : public gamedef_q1_like_t<GAME_HEXEN_II>
{
    gamedef_h2_t() : gamedef_q1_like_t("DATA1") { }

    const std::initializer_list<aabb3d> &get_hull_sizes() const
    {
        static std::initializer_list<aabb3d> hulls = {
            {{0, 0, 0}, {0, 0, 0}},
            {{-16, -16, -32}, {16, 16, 24}},
            {{-24, -24, -20}, {24, 24, 20}},
            {{-16, -16, -12}, {16, 16, 16}},
            {{-8, -8, -8}, {8, 8, 8}}, // {{-40, -40, -42}, {40, 40, 42}} = original game
            {{-48, -48, -50}, {48, 48, 50}}
        };

        return hulls;
    }
};

struct gamedef_hl_t : public gamedef_q1_like_t<GAME_HALF_LIFE>
{
    gamedef_hl_t() :
        gamedef_q1_like_t("VALVE")
    {
        has_rgb_lightmap = true;
    }

    const std::initializer_list<aabb3d> &get_hull_sizes() const
    {
        static std::initializer_list<aabb3d> hulls = {
            {{0, 0, 0}, {0, 0, 0}},
            {{-16, -16, -36}, {16, 16, 36}},
            {{-32, -32, -32}, {32, 32, 32}},
            {{-16, -16, -18}, {16, 16, 18}}
        };

        return hulls;
    }
};

struct gamedef_q2_t : public gamedef_t
{
    gamedef_q2_t() :
        gamedef_t("BASEQ2")
    {
        this->id = GAME_QUAKE_II;
        has_rgb_lightmap = true;
    }

    bool surf_is_lightmapped(const surfflags_t &flags) const
    {
        return !(flags.native & (Q2_SURF_WARP | Q2_SURF_SKY | Q2_SURF_NODRAW)); // mxd. +Q2_SURF_NODRAW
    }

    bool surf_is_subdivided(const surfflags_t &flags) const { return !(flags.native & (Q2_SURF_WARP | Q2_SURF_SKY)); }

    contentflags_t cluster_contents(const contentflags_t &contents0, const contentflags_t &contents1) const
    {
        contentflags_t c = {contents0.native | contents1.native, contents0.extended | contents1.extended};

        // a cluster may include some solid detail areas, but
        // still be seen into
        if (!(contents0.native & Q2_CONTENTS_SOLID) || !(contents1.native & Q2_CONTENTS_SOLID))
            c.native &= ~Q2_CONTENTS_SOLID;

        return c;
    }

    int32_t get_content_type(const contentflags_t &contents) const
    {
        return (contents.native & ((Q2_LAST_VISIBLE_CONTENTS << 1) - 1)) |
               (Q2_CONTENTS_PLAYERCLIP | Q2_CONTENTS_MONSTERCLIP);
    }

    int32_t contents_priority(const contentflags_t &contents) const
    {
        if (contents.extended & CFLAGS_DETAIL) {
            return 8;
        } else if (contents.extended & CFLAGS_DETAIL_ILLUSIONARY) {
            return 6;
        } else if (contents.extended & CFLAGS_DETAIL_FENCE) {
            return 7;
        } else if (contents.extended & CFLAGS_ILLUSIONARY_VISBLOCKER) {
            return 2;
        } else
            switch (contents.native & ((Q2_LAST_VISIBLE_CONTENTS << 1) - 1)) {
                case Q2_CONTENTS_SOLID: return 10;
                case Q2_CONTENTS_WINDOW: return 9;
                case Q2_CONTENTS_AUX: return 5;
                case Q2_CONTENTS_LAVA: return 4;
                case Q2_CONTENTS_SLIME: return 3;
                case Q2_CONTENTS_WATER: return 2;
                case Q2_CONTENTS_MIST: return 1;
                default: return 0;
            }
    }

    contentflags_t create_empty_contents(const int32_t &cflags) const { return {0, cflags}; }

    contentflags_t create_solid_contents(const int32_t &cflags) const { return {Q2_CONTENTS_SOLID, cflags}; }

    contentflags_t create_sky_contents(const int32_t &cflags) const { return create_solid_contents(cflags); }

    contentflags_t create_liquid_contents(const int32_t &liquid_type, const int32_t &cflags) const
    {
        switch (liquid_type) {
            case CONTENTS_WATER: return {Q2_CONTENTS_WATER, cflags};
            case CONTENTS_SLIME: return {Q2_CONTENTS_SLIME, cflags};
            case CONTENTS_LAVA: return {Q2_CONTENTS_LAVA, cflags};
            default: FError("bad contents");
        }
    }

    bool contents_are_empty(const contentflags_t &contents) const
    {
        return !(contents.native & ((Q2_LAST_VISIBLE_CONTENTS << 1) - 1));
    }

    bool contents_are_solid(const contentflags_t &contents) const { return contents.native & Q2_CONTENTS_SOLID; }

    bool contents_are_sky(const contentflags_t &contents) const { return false; }

    bool contents_are_liquid(const contentflags_t &contents) const { return contents.native & Q2_CONTENTS_LIQUID; }

    bool contents_are_valid(const contentflags_t &contents, bool strict) const
    {
        // check that we don't have more than one visible contents type
        const int32_t x = (contents.native & ((Q2_LAST_VISIBLE_CONTENTS << 1) - 1));
        if ((x & (x - 1)) != 0) {
            return false;
        }

        // TODO: check other invalid mixes
        if (!x && strict) {
            return false;
        }

        return true;
    }

    constexpr int32_t visible_contents(const int32_t &contents) const
    {
        for (int32_t i = 1; i <= Q2_LAST_VISIBLE_CONTENTS; i <<= 1)
            if (contents & i)
                return i;

        return 0;
    }

    bool portal_can_see_through(const contentflags_t &contents0, const contentflags_t &contents1) const
    {
        int32_t c0 = contents0.native, c1 = contents1.native;

        if (!visible_contents(c0 ^ c1))
            return true;

        if ((c0 & Q2_CONTENTS_TRANSLUCENT) || contents0.is_detail())
            c0 = 0;
        if ((c1 & Q2_CONTENTS_TRANSLUCENT) || contents1.is_detail())
            c1 = 0;

        // can't see through solid
        if ((c0 | c1) & Q2_CONTENTS_SOLID)
            return false;

        // identical on both sides
        if (!(c0 ^ c1))
            return true;

        return visible_contents(c0 ^ c1);
    }

    std::string get_contents_display(const contentflags_t &contents) const
    {
        constexpr const char *bitflag_names[] = {"SOLID", "WINDOW", "AUX", "LAVA", "SLIME", "WATER", "MIST", "128",
            "256", "512", "1024", "2048", "4096", "8192", "16384", "AREAPORTAL", "PLAYERCLIP", "MONSTERCLIP",
            "CURRENT_0", "CURRENT_90", "CURRENT_180", "CURRENT_270", "CURRENT_UP", "CURRENT_DOWN", "ORIGIN", "MONSTER",
            "DEADMONSTER", "DETAIL", "TRANSLUCENT", "LADDER", "1073741824", "2147483648"};

        std::string s;

        for (int32_t i = 0; i < std::size(bitflag_names); i++) {
            if (contents.native & (1 << i)) {
                if (s.size()) {
                    s += " | " + std::string(bitflag_names[i]);
                } else {
                    s += bitflag_names[i];
                }
            }
        }

        return s;
    }

    const std::initializer_list<aabb3d> &get_hull_sizes() const
    {
        static constexpr std::initializer_list<aabb3d> hulls = {};
        return hulls;
    }
};

static const gamedef_generic_t gamedef_generic;
const bspversion_t bspver_generic{NO_VERSION, NO_VERSION, "mbsp", "generic BSP", nullptr, &gamedef_generic};
static const gamedef_q1_like_t<GAME_QUAKE> gamedef_q1;
const bspversion_t bspver_q1{BSPVERSION, NO_VERSION, "bsp29", "Quake BSP", lumpspec_bsp29, &gamedef_q1, &bspver_bsp2};
const bspversion_t bspver_bsp2{BSP2VERSION, NO_VERSION, "bsp2", "Quake BSP2", lumpspec_bsp2, &gamedef_q1};
const bspversion_t bspver_bsp2rmq{BSP2RMQVERSION, NO_VERSION, "bsp2rmq", "Quake BSP2-RMQ", lumpspec_bsp2rmq, &gamedef_q1};
/* Hexen II doesn't use a separate version, but we can still use a separate tag/name for it */
static const gamedef_h2_t gamedef_h2;
const bspversion_t bspver_h2{BSPVERSION, NO_VERSION, "hexen2", "Hexen II BSP", lumpspec_bsp29_h2, &gamedef_h2, &bspver_h2bsp2};
const bspversion_t bspver_h2bsp2{BSP2VERSION, NO_VERSION, "hexen2bsp2", "Hexen II BSP2", lumpspec_bsp2_h2, &gamedef_h2};
const bspversion_t bspver_h2bsp2rmq{BSP2RMQVERSION, NO_VERSION, "hexen2bsp2rmq", "Hexen II BSP2-RMQ", lumpspec_bsp2rmq_h2, &gamedef_h2};
static const gamedef_hl_t gamedef_hl;
const bspversion_t bspver_hl{BSPHLVERSION, NO_VERSION, "hl", "Half-Life BSP", lumpspec_bsp29, &gamedef_hl};
static const gamedef_q2_t gamedef_q2;
const bspversion_t bspver_q2{Q2_BSPIDENT, Q2_BSPVERSION, "q2bsp", "Quake II BSP", lumpspec_q2bsp, &gamedef_q2, &bspver_qbism};
const bspversion_t bspver_qbism{Q2_QBISMIDENT, Q2_BSPVERSION, "qbism", "Quake II Qbism BSP", lumpspec_qbism, &gamedef_q2};

bool contentflags_t::types_equal(const contentflags_t &other, const gamedef_t *game) const
{
    return (extended & CFLAGS_DETAIL_MASK) == (other.extended & CFLAGS_DETAIL_MASK) &&
           game->get_content_type(*this) == game->get_content_type(other);
}

int32_t contentflags_t::priority(const gamedef_t *game) const
{
    return game->contents_priority(*this);
}

bool contentflags_t::is_empty(const gamedef_t *game) const
{
    return game->contents_are_empty(*this);
}

bool contentflags_t::is_solid(const gamedef_t *game) const
{
    return game->contents_are_solid(*this);
}

bool contentflags_t::is_sky(const gamedef_t *game) const
{
    return game->contents_are_sky(*this);
}

bool contentflags_t::is_liquid(const gamedef_t *game) const
{
    return game->contents_are_liquid(*this);
}

bool contentflags_t::is_valid(const gamedef_t *game, bool strict) const
{
    return game->contents_are_valid(*this, strict);
}

std::string contentflags_t::to_string(const gamedef_t *game) const
{
    std::string s = game->get_contents_display(*this);
    return s;
}

static const char *BSPVersionString(const bspversion_t *version)
{
    if (version->name) {
        return version->name;
    }

    static char buffers[2][20];
    static int index;
    char *buffer = buffers[1 & ++index];
    if (version->version != NO_VERSION) {
        snprintf(buffer, sizeof(buffers[0]), "%d:%d", version->version, version->ident);
    } else {
        snprintf(buffer, sizeof(buffers[0]), "%d", version->version);
    }
    return buffer;
}

static bool BSPVersionSupported(int32_t ident, int32_t version, const bspversion_t **out_version)
{
    for (const bspversion_t *bspver : bspversions) {
        if (bspver->ident == ident && bspver->version == version) {
            if (bspver->game->id == GAME_HEXEN_II) {
                // HACK: don't detect as Hexen II here, it's done later (isHexen2).
                // Since the Hexen II bspversion_t's have the same ident/version as Quake
                // we need to assume Quake.
                continue;
            }
            *out_version = bspver;
            return true;
        }
    }

    return false;
}

/*
 * =========================================================================
 * BSP Format Conversion
 * =========================================================================
 */

// move structured data if the input and output
// are of the same type
template<typename T>
inline void CopyOrMoveArray(T &in, T &out)
{
    out = std::move(in);
}

// convert structured data if we're different types
template<typename T, typename F>
inline void CopyOrMoveArray(const std::vector<F> &from, std::vector<T> &to)
{
    to.reserve(from.size());

    for (auto &v : from)
    {
        if constexpr(std::is_arithmetic_v<T> && std::is_arithmetic_v<F>)
            to.emplace_back(numeric_cast<T>(v));
        else
            to.emplace_back(v);
    }
}

// convert structured data if we're different types
// with numeric casting for arrays
template<typename T, typename F, size_t N>
inline void CopyOrMoveArray(const std::vector<std::array<F, N>> &from, std::vector<std::array<T, N>> &to)
{
    to.reserve(from.size());

    for (auto &v : from)
    {
        if constexpr(std::is_arithmetic_v<T> && std::is_arithmetic_v<F>)
            to.emplace_back(array_cast<std::array<T, N>>(v));
        else
            to.emplace_back(v);
    }
}

/*
 * =========================================================================
 * ConvertBSPFormat
 * - BSP is assumed to be in CPU byte order already
 * - No checks are done here (yet) for overflow of values when down-converting
 * =========================================================================
 */
bool ConvertBSPFormat(bspdata_t *bspdata, const bspversion_t *to_version)
{
    try
    {
        if (bspdata->version == to_version)
            return true;

        if (to_version == &bspver_generic) {
            // Conversions to bspver_generic
            // NOTE: these always succeed
            mbsp_t mbsp { };

            /*if (std::holds_alternative<bsp29_t>(bspdata->bsp)) {
                // bspver_q1, bspver_h2, bspver_hl -> bspver_generic
                const bsp29_t &bsp29 = std::get<bsp29_t>(bspdata->bsp);

                // copy counts
                mbsp.nummodels = bsp29.nummodels;
                mbsp.visdatasize = bsp29.visdatasize;
                mbsp.lightdatasize = bsp29.lightdatasize;
                mbsp.texdatasize = bsp29.texdatasize;
                mbsp.entdatasize = bsp29.entdatasize;
                mbsp.numleafs = bsp29.numleafs;
                mbsp.numplanes = bsp29.numplanes;
                mbsp.numvertexes = bsp29.numvertexes;
                mbsp.numnodes = bsp29.numnodes;
                mbsp.numtexinfo = bsp29.numtexinfo;
                mbsp.numfaces = bsp29.numfaces;
                mbsp.numclipnodes = bsp29.numclipnodes;
                mbsp.numedges = bsp29.numedges;
                mbsp.numleaffaces = bsp29.nummarksurfaces;
                mbsp.numsurfedges = bsp29.numsurfedges;

                // copy or convert data
                if (bspdata->version == &bspver_h2) {
                    CopyArray(bsp29.dmodels_h2, bsp29.nummodels, mbsp.dmodels);
                } else {
                    CopyArray(bsp29.dmodels_q, bsp29.nummodels, mbsp.dmodels);
                }
                mbsp.dvisdata = BSP29_CopyVisData(bsp29.dvisdata, bsp29.visdatasize);
                CopyArray(bsp29.dlightdata, bsp29.lightdatasize, mbsp.dlightdata);
                CopyArray(bsp29.dtexdata, bsp29.texdatasize, mbsp.dtexdata);
                CopyArray(bsp29.dentdata, bsp29.entdatasize, mbsp.dentdata);
                CopyArray(bsp29.dleafs, bsp29.numleafs, mbsp.dleafs);
                CopyArray(bsp29.dplanes, bsp29.numplanes, mbsp.dplanes);
                CopyArray(bsp29.dvertexes, bsp29.numvertexes, mbsp.dvertexes);
                CopyArray(bsp29.dnodes, bsp29.numnodes, mbsp.dnodes);
                CopyArray(bsp29.texinfo, bsp29.numtexinfo, mbsp.texinfo);
                CopyArray(bsp29.dfaces, bsp29.numfaces, mbsp.dfaces);
                CopyArray(bsp29.dclipnodes, bsp29.numclipnodes, mbsp.dclipnodes);
                CopyArray(bsp29.dedges, bsp29.numedges, mbsp.dedges);
                CopyArray(bsp29.dmarksurfaces, bsp29.nummarksurfaces, mbsp.dleaffaces);
                CopyArray(bsp29.dsurfedges, bsp29.numsurfedges, mbsp.dsurfedges);
            } else*/ if (std::holds_alternative<q2bsp_t>(bspdata->bsp)) {
                // bspver_q2 -> bspver_generic
                q2bsp_t &q2bsp = std::get<q2bsp_t>(bspdata->bsp);

                // copy or convert data
                CopyOrMoveArray(q2bsp.dmodels, mbsp.dmodels);
                CopyOrMoveArray(q2bsp.dlightdata, mbsp.dlightdata);
                CopyOrMoveArray(q2bsp.dentdata, mbsp.dentdata);
                CopyOrMoveArray(q2bsp.dleafs, mbsp.dleafs);
                CopyOrMoveArray(q2bsp.dplanes, mbsp.dplanes);
                CopyOrMoveArray(q2bsp.dvertexes, mbsp.dvertexes);
                CopyOrMoveArray(q2bsp.dnodes, mbsp.dnodes);
                CopyOrMoveArray(q2bsp.texinfo, mbsp.texinfo);
                CopyOrMoveArray(q2bsp.dfaces, mbsp.dfaces);
                CopyOrMoveArray(q2bsp.dedges, mbsp.dedges);
                CopyOrMoveArray(q2bsp.dleaffaces, mbsp.dleaffaces);
                CopyOrMoveArray(q2bsp.dleafbrushes, mbsp.dleafbrushes);
                CopyOrMoveArray(q2bsp.dsurfedges, mbsp.dsurfedges);

                mbsp.dvis = std::move(q2bsp.dvis);

                CopyOrMoveArray(q2bsp.dareas, mbsp.dareas);
                CopyOrMoveArray(q2bsp.dareaportals, mbsp.dareaportals);

                CopyOrMoveArray(q2bsp.dbrushes, mbsp.dbrushes);
                CopyOrMoveArray(q2bsp.dbrushsides, mbsp.dbrushsides);
            } /*else if (std::holds_alternative<q2bsp_qbism_t>(bspdata->bsp)) {
                // bspver_qbism -> bspver_generic
                const q2bsp_qbism_t &q2bsp = std::get<q2bsp_qbism_t>(bspdata->bsp);

                // copy counts
                mbsp.nummodels = q2bsp.nummodels;
                mbsp.visdatasize = q2bsp.visdatasize;
                mbsp.lightdatasize = q2bsp.lightdatasize;
                mbsp.entdatasize = q2bsp.entdatasize;
                mbsp.numleafs = q2bsp.numleafs;
                mbsp.numplanes = q2bsp.numplanes;
                mbsp.numvertexes = q2bsp.numvertexes;
                mbsp.numnodes = q2bsp.numnodes;
                mbsp.numtexinfo = q2bsp.numtexinfo;
                mbsp.numfaces = q2bsp.numfaces;
                mbsp.numedges = q2bsp.numedges;
                mbsp.numleaffaces = q2bsp.numleaffaces;
                mbsp.numleafbrushes = q2bsp.numleafbrushes;
                mbsp.numsurfedges = q2bsp.numsurfedges;
                mbsp.numareas = q2bsp.numareas;
                mbsp.numareaportals = q2bsp.numareaportals;
                mbsp.numbrushes = q2bsp.numbrushes;
                mbsp.numbrushsides = q2bsp.numbrushsides;

                // copy or convert data
                CopyArray(q2bsp.dmodels, q2bsp.nummodels, mbsp.dmodels);
                CopyArray(q2bsp.dlightdata, q2bsp.lightdatasize, mbsp.dlightdata);
                CopyArray(q2bsp.dentdata, q2bsp.entdatasize, mbsp.dentdata);
                CopyArray(q2bsp.dleafs, q2bsp.numleafs, mbsp.dleafs);
                CopyArray(q2bsp.dplanes, q2bsp.numplanes, mbsp.dplanes);
                CopyArray(q2bsp.dvertexes, q2bsp.numvertexes, mbsp.dvertexes);
                CopyArray(q2bsp.dnodes, q2bsp.numnodes, mbsp.dnodes);
                CopyArray(q2bsp.texinfo, q2bsp.numtexinfo, mbsp.texinfo);
                CopyArray(q2bsp.dfaces, q2bsp.numfaces, mbsp.dfaces);
                CopyArray(q2bsp.dedges, q2bsp.numedges, mbsp.dedges);
                CopyArray(q2bsp.dleaffaces, q2bsp.numleaffaces, mbsp.dleaffaces);
                CopyArray(q2bsp.dleafbrushes, q2bsp.numleafbrushes, mbsp.dleafbrushes);
                CopyArray(q2bsp.dsurfedges, q2bsp.numsurfedges, mbsp.dsurfedges);

                mbsp.dvisdata =
                    Q2BSPtoM_CopyVisData(q2bsp.dvis, q2bsp.visdatasize, &mbsp.visdatasize, mbsp.dleafs, mbsp.numleafs);

                CopyArray(q2bsp.dareas, q2bsp.numareas, mbsp.dareas);
                CopyArray(q2bsp.dareaportals, q2bsp.numareaportals, mbsp.dareaportals);

                CopyArray(q2bsp.dbrushes, q2bsp.numbrushes, mbsp.dbrushes);
                CopyArray(q2bsp.dbrushsides, q2bsp.numbrushsides, mbsp.dbrushsides);
            } else if (std::holds_alternative<bsp2rmq_t>(bspdata->bsp)) {
                // bspver_bsp2rmq, bspver_h2bsp2rmq -> bspver_generic
                const bsp2rmq_t &bsp2rmq = std::get<bsp2rmq_t>(bspdata->bsp);

                // copy counts
                mbsp.nummodels = bsp2rmq.nummodels;
                mbsp.visdatasize = bsp2rmq.visdatasize;
                mbsp.lightdatasize = bsp2rmq.lightdatasize;
                mbsp.texdatasize = bsp2rmq.texdatasize;
                mbsp.entdatasize = bsp2rmq.entdatasize;
                mbsp.numleafs = bsp2rmq.numleafs;
                mbsp.numplanes = bsp2rmq.numplanes;
                mbsp.numvertexes = bsp2rmq.numvertexes;
                mbsp.numnodes = bsp2rmq.numnodes;
                mbsp.numtexinfo = bsp2rmq.numtexinfo;
                mbsp.numfaces = bsp2rmq.numfaces;
                mbsp.numclipnodes = bsp2rmq.numclipnodes;
                mbsp.numedges = bsp2rmq.numedges;
                mbsp.numleaffaces = bsp2rmq.nummarksurfaces;
                mbsp.numsurfedges = bsp2rmq.numsurfedges;

                // copy or convert data
                if (bspdata->version == &bspver_h2bsp2rmq) {
                    CopyArray(bsp2rmq.dmodels_h2, bsp2rmq.nummodels, mbsp.dmodels);
                } else {
                    CopyArray(bsp2rmq.dmodels_q, bsp2rmq.nummodels, mbsp.dmodels);
                }
                mbsp.dvisdata = BSP29_CopyVisData(bsp2rmq.dvisdata, bsp2rmq.visdatasize);
                CopyArray(bsp2rmq.dlightdata, bsp2rmq.lightdatasize, mbsp.dlightdata);
                CopyArray(bsp2rmq.dtexdata, bsp2rmq.texdatasize, mbsp.dtexdata);
                CopyArray(bsp2rmq.dentdata, bsp2rmq.entdatasize,mbsp.dentdata);
                CopyArray(bsp2rmq.dleafs, bsp2rmq.numleafs, mbsp.dleafs);
                CopyArray(bsp2rmq.dplanes, bsp2rmq.numplanes, mbsp.dplanes);
                CopyArray(bsp2rmq.dvertexes, bsp2rmq.numvertexes, mbsp.dvertexes);
                CopyArray(bsp2rmq.dnodes, bsp2rmq.numnodes, mbsp.dnodes);
                CopyArray(bsp2rmq.texinfo, bsp2rmq.numtexinfo, mbsp.texinfo);
                CopyArray(bsp2rmq.dfaces, bsp2rmq.numfaces, mbsp.dfaces);
                CopyArray(bsp2rmq.dclipnodes, bsp2rmq.numclipnodes, mbsp.dclipnodes);
                CopyArray(bsp2rmq.dedges, bsp2rmq.numedges, mbsp.dedges);
                CopyArray(bsp2rmq.dmarksurfaces, bsp2rmq.nummarksurfaces, mbsp.dleaffaces);
                CopyArray(bsp2rmq.dsurfedges, bsp2rmq.numsurfedges, mbsp.dsurfedges);
            } else if (std::holds_alternative<bsp2_t>(bspdata->bsp)) {
                // bspver_bsp2, bspver_h2bsp2 -> bspver_generic
                const bsp2_t &bsp2 = std::get<bsp2_t>(bspdata->bsp);

                // copy counts
                mbsp.nummodels = bsp2.nummodels;
                mbsp.visdatasize = bsp2.visdatasize;
                mbsp.lightdatasize = bsp2.lightdatasize;
                mbsp.texdatasize = bsp2.texdatasize;
                mbsp.entdatasize = bsp2.entdatasize;
                mbsp.numleafs = bsp2.numleafs;
                mbsp.numplanes = bsp2.numplanes;
                mbsp.numvertexes = bsp2.numvertexes;
                mbsp.numnodes = bsp2.numnodes;
                mbsp.numtexinfo = bsp2.numtexinfo;
                mbsp.numfaces = bsp2.numfaces;
                mbsp.numclipnodes = bsp2.numclipnodes;
                mbsp.numedges = bsp2.numedges;
                mbsp.numleaffaces = bsp2.nummarksurfaces;
                mbsp.numsurfedges = bsp2.numsurfedges;

                // copy or convert data
                if (bspdata->version == &bspver_h2bsp2) {
                    CopyArray(bsp2.dmodels_h2, bsp2.nummodels, mbsp.dmodels);
                } else {
                    CopyArray(bsp2.dmodels_q, bsp2.nummodels, mbsp.dmodels);
                }
                mbsp.dvisdata = BSP29_CopyVisData(bsp2.dvisdata, bsp2.visdatasize);
                CopyArray(bsp2.dlightdata, bsp2.lightdatasize, mbsp.dlightdata);
                CopyArray(bsp2.dtexdata, bsp2.texdatasize, mbsp.dtexdata);
                CopyArray(bsp2.dentdata, bsp2.entdatasize, mbsp.dentdata);
                CopyArray(bsp2.dleafs, bsp2.numleafs, mbsp.dleafs);
                CopyArray(bsp2.dplanes, bsp2.numplanes, mbsp.dplanes);
                CopyArray(bsp2.dvertexes, bsp2.numvertexes, mbsp.dvertexes);
                CopyArray(bsp2.dnodes, bsp2.numnodes, mbsp.dnodes);
                CopyArray(bsp2.texinfo, bsp2.numtexinfo, mbsp.texinfo);
                CopyArray(bsp2.dfaces, bsp2.numfaces, mbsp.dfaces);
                CopyArray(bsp2.dclipnodes, bsp2.numclipnodes, mbsp.dclipnodes);
                CopyArray(bsp2.dedges, bsp2.numedges, mbsp.dedges);
                CopyArray(bsp2.dmarksurfaces, bsp2.nummarksurfaces, mbsp.dleaffaces);
                CopyArray(bsp2.dsurfedges, bsp2.numsurfedges, mbsp.dsurfedges);
            } */else {
                return false;
            }

            bspdata->loadversion = mbsp.loadversion = bspdata->version;
            bspdata->version = to_version;

            bspdata->bsp = std::move(mbsp);
            return true;
        } else if (bspdata->version == &bspver_generic) {
            // Conversions from bspver_generic
            mbsp_t &mbsp = std::get<mbsp_t>(bspdata->bsp);

            /*if (to_version == &bspver_q1 || to_version == &bspver_h2 || to_version == &bspver_hl) {
                // bspver_generic -> bspver_q1, bspver_h2, bspver_hl
                bsp29_t bsp29 { };

                // copy counts
                bsp29.nummodels = mbsp.nummodels;
                bsp29.visdatasize = mbsp.visdatasize;
                bsp29.lightdatasize = mbsp.lightdatasize;
                bsp29.texdatasize = mbsp.texdatasize;
                bsp29.entdatasize = mbsp.entdatasize;
                bsp29.numleafs = mbsp.numleafs;
                bsp29.numplanes = mbsp.numplanes;
                bsp29.numvertexes = mbsp.numvertexes;
                bsp29.numnodes = mbsp.numnodes;
                bsp29.numtexinfo = mbsp.numtexinfo;
                bsp29.numfaces = mbsp.numfaces;
                bsp29.numclipnodes = mbsp.numclipnodes;
                bsp29.numedges = mbsp.numedges;
                bsp29.nummarksurfaces = mbsp.numleaffaces;
                bsp29.numsurfedges = mbsp.numsurfedges;

                // copy or convert data
                if (to_version == &bspver_h2) {
                    CopyArray(mbsp.dmodels, mbsp.nummodels, bsp29.dmodels_h2);
                } else {
                    CopyArray(mbsp.dmodels, mbsp.nummodels, bsp29.dmodels_q);
                }
                bsp29.dvisdata = BSP29_CopyVisData(mbsp.dvisdata, mbsp.visdatasize);
                CopyArray(mbsp.dlightdata, mbsp.lightdatasize, bsp29.dlightdata);
                CopyArray(mbsp.dtexdata, mbsp.texdatasize, bsp29.dtexdata);
                CopyArray(mbsp.dentdata, mbsp.entdatasize, bsp29.dentdata);
                CopyArray(mbsp.dleafs, mbsp.numleafs, bsp29.dleafs);
                CopyArray(mbsp.dplanes, mbsp.numplanes, bsp29.dplanes);
                CopyArray(mbsp.dvertexes, mbsp.numvertexes, bsp29.dvertexes);
                CopyArray(mbsp.dnodes, mbsp.numnodes, bsp29.dnodes);
                CopyArray(mbsp.texinfo, mbsp.numtexinfo, bsp29.texinfo);
                CopyArray(mbsp.dfaces, mbsp.numfaces, bsp29.dfaces);
                CopyArray(mbsp.dclipnodes, mbsp.numclipnodes, bsp29.dclipnodes);
                CopyArray(mbsp.dedges, mbsp.numedges, bsp29.dedges);
                CopyArray(mbsp.dleaffaces, mbsp.numleaffaces, bsp29.dmarksurfaces);
                CopyArray(mbsp.dsurfedges, mbsp.numsurfedges, bsp29.dsurfedges);

                // Conversion complete!
                bspdata->version = to_version;
                bspdata->bsp = std::move(bsp29);

                return true;
            } else */if (to_version == &bspver_q2) {
                // bspver_generic -> bspver_q2
                q2bsp_t q2bsp { };

                // copy or convert data
                CopyOrMoveArray(mbsp.dmodels, q2bsp.dmodels);
                q2bsp.dvis = std::move(mbsp.dvis);
                CopyOrMoveArray(mbsp.dlightdata, q2bsp.dlightdata);
                CopyOrMoveArray(mbsp.dentdata, q2bsp.dentdata);
                CopyOrMoveArray(mbsp.dleafs, q2bsp.dleafs);
                CopyOrMoveArray(mbsp.dplanes, q2bsp.dplanes);
                CopyOrMoveArray(mbsp.dvertexes, q2bsp.dvertexes);
                CopyOrMoveArray(mbsp.dnodes, q2bsp.dnodes);
                CopyOrMoveArray(mbsp.texinfo, q2bsp.texinfo);
                CopyOrMoveArray(mbsp.dfaces, q2bsp.dfaces);
                CopyOrMoveArray(mbsp.dedges,q2bsp.dedges);
                CopyOrMoveArray(mbsp.dleaffaces, q2bsp.dleaffaces);
                CopyOrMoveArray(mbsp.dleafbrushes,q2bsp.dleafbrushes);
                CopyOrMoveArray(mbsp.dsurfedges, q2bsp.dsurfedges);

                CopyOrMoveArray(mbsp.dareas, q2bsp.dareas);
                CopyOrMoveArray(mbsp.dareaportals, q2bsp.dareaportals);

                CopyOrMoveArray(mbsp.dbrushes, q2bsp.dbrushes);
                CopyOrMoveArray(mbsp.dbrushsides, q2bsp.dbrushsides);

                /* Conversion complete! */
                bspdata->version = to_version;
                bspdata->bsp = std::move(q2bsp);

                return true;
            } /*else if (to_version == &bspver_qbism) {
                // bspver_generic -> bspver_qbism
                q2bsp_qbism_t q2bsp { };

                // copy counts
                q2bsp.nummodels = mbsp.nummodels;
                q2bsp.visdatasize = mbsp.visdatasize;
                q2bsp.lightdatasize = mbsp.lightdatasize;
                q2bsp.entdatasize = mbsp.entdatasize;
                q2bsp.numleafs = mbsp.numleafs;
                q2bsp.numplanes = mbsp.numplanes;
                q2bsp.numvertexes = mbsp.numvertexes;
                q2bsp.numnodes = mbsp.numnodes;
                q2bsp.numtexinfo = mbsp.numtexinfo;
                q2bsp.numfaces = mbsp.numfaces;
                q2bsp.numedges = mbsp.numedges;
                q2bsp.numleaffaces = mbsp.numleaffaces;
                q2bsp.numleafbrushes = mbsp.numleafbrushes;
                q2bsp.numsurfedges = mbsp.numsurfedges;
                q2bsp.numareas = mbsp.numareas;
                q2bsp.numareaportals = mbsp.numareaportals;
                q2bsp.numbrushes = mbsp.numbrushes;
                q2bsp.numbrushsides = mbsp.numbrushsides;

                // copy or convert data
                CopyArray(mbsp.dmodels, mbsp.nummodels, q2bsp.dmodels);
                q2bsp.dvis = MBSPtoQ2_CopyVisData(mbsp.dvisdata, &q2bsp.visdatasize, mbsp.numleafs, mbsp.dleafs);
                CopyArray(mbsp.dlightdata, mbsp.lightdatasize, q2bsp.dlightdata);
                CopyArray(mbsp.dentdata, mbsp.entdatasize, q2bsp.dentdata);
                CopyArray(mbsp.dleafs, mbsp.numleafs, q2bsp.dleafs);
                CopyArray(mbsp.dplanes, mbsp.numplanes, q2bsp.dplanes);
                CopyArray(mbsp.dvertexes, mbsp.numvertexes, q2bsp.dvertexes);
                CopyArray(mbsp.dnodes, mbsp.numnodes, q2bsp.dnodes);
                CopyArray(mbsp.texinfo, mbsp.numtexinfo, q2bsp.texinfo);
                CopyArray(mbsp.dfaces, mbsp.numfaces, q2bsp.dfaces);
                CopyArray(mbsp.dedges, mbsp.numedges, q2bsp.dedges);
                CopyArray(mbsp.dleaffaces, mbsp.numleaffaces, q2bsp.dleaffaces);
                CopyArray(mbsp.dleafbrushes, mbsp.numleafbrushes, q2bsp.dleafbrushes);
                CopyArray(mbsp.dsurfedges, mbsp.numsurfedges, q2bsp.dsurfedges);

                CopyArray(mbsp.dareas, mbsp.numareas, q2bsp.dareas);
                CopyArray(mbsp.dareaportals, mbsp.numareaportals, q2bsp.dareaportals);

                CopyArray(mbsp.dbrushes, mbsp.numbrushes, q2bsp.dbrushes);
                CopyArray(mbsp.dbrushsides, mbsp.numbrushsides, q2bsp.dbrushsides);

                // Conversion complete!
                bspdata->version = to_version;
                bspdata->bsp = std::move(q2bsp);

                return true;
            } else if (to_version == &bspver_bsp2rmq || to_version == &bspver_h2bsp2rmq) {
                // bspver_generic -> bspver_bsp2rmq, bspver_h2bsp2rmq
                bsp2rmq_t bsp2rmq { };

                // copy counts
                bsp2rmq.nummodels = mbsp.nummodels;
                bsp2rmq.visdatasize = mbsp.visdatasize;
                bsp2rmq.lightdatasize = mbsp.lightdatasize;
                bsp2rmq.texdatasize = mbsp.texdatasize;
                bsp2rmq.entdatasize = mbsp.entdatasize;
                bsp2rmq.numleafs = mbsp.numleafs;
                bsp2rmq.numplanes = mbsp.numplanes;
                bsp2rmq.numvertexes = mbsp.numvertexes;
                bsp2rmq.numnodes = mbsp.numnodes;
                bsp2rmq.numtexinfo = mbsp.numtexinfo;
                bsp2rmq.numfaces = mbsp.numfaces;
                bsp2rmq.numclipnodes = mbsp.numclipnodes;
                bsp2rmq.numedges = mbsp.numedges;
                bsp2rmq.nummarksurfaces = mbsp.numleaffaces;
                bsp2rmq.numsurfedges = mbsp.numsurfedges;

                // copy or convert data
                if (to_version == &bspver_h2bsp2rmq) {
                    CopyArray(mbsp.dmodels, mbsp.nummodels, bsp2rmq.dmodels_h2);
                } else {
                    CopyArray(mbsp.dmodels, mbsp.nummodels, bsp2rmq.dmodels_q);
                }
                bsp2rmq.dvisdata = BSP29_CopyVisData(mbsp.dvisdata, mbsp.visdatasize);
                CopyArray(mbsp.dlightdata, mbsp.lightdatasize, bsp2rmq.dlightdata);
                CopyArray(mbsp.dtexdata, mbsp.texdatasize, bsp2rmq.dtexdata);
                CopyArray(mbsp.dentdata, mbsp.entdatasize, bsp2rmq.dentdata);
                CopyArray(mbsp.dleafs, mbsp.numleafs, bsp2rmq.dleafs);
                CopyArray(mbsp.dplanes, mbsp.numplanes, bsp2rmq.dplanes);
                CopyArray(mbsp.dvertexes, mbsp.numvertexes, bsp2rmq.dvertexes);
                CopyArray(mbsp.dnodes, mbsp.numnodes, bsp2rmq.dnodes);
                CopyArray(mbsp.texinfo, mbsp.numtexinfo, bsp2rmq.texinfo);
                CopyArray(mbsp.dfaces, mbsp.numfaces, bsp2rmq.dfaces);
                CopyArray(mbsp.dclipnodes, mbsp.numclipnodes, bsp2rmq.dclipnodes);
                CopyArray(mbsp.dedges, mbsp.numedges, bsp2rmq.dedges);
                CopyArray(mbsp.dleaffaces, mbsp.numleaffaces, bsp2rmq.dmarksurfaces);
                CopyArray(mbsp.dsurfedges, mbsp.numsurfedges, bsp2rmq.dsurfedges);

                // Conversion complete!
                bspdata->version = to_version;
                bspdata->bsp = std::move(bsp2rmq);

                return true;
            } else if (to_version == &bspver_bsp2 || to_version == &bspver_h2bsp2) {
                // bspver_generic -> bspver_bsp2, bspver_h2bsp2
                bsp2_t bsp2 { };

                // copy counts
                bsp2.nummodels = mbsp.nummodels;
                bsp2.visdatasize = mbsp.visdatasize;
                bsp2.lightdatasize = mbsp.lightdatasize;
                bsp2.texdatasize = mbsp.texdatasize;
                bsp2.entdatasize = mbsp.entdatasize;
                bsp2.numleafs = mbsp.numleafs;
                bsp2.numplanes = mbsp.numplanes;
                bsp2.numvertexes = mbsp.numvertexes;
                bsp2.numnodes = mbsp.numnodes;
                bsp2.numtexinfo = mbsp.numtexinfo;
                bsp2.numfaces = mbsp.numfaces;
                bsp2.numclipnodes = mbsp.numclipnodes;
                bsp2.numedges = mbsp.numedges;
                bsp2.nummarksurfaces = mbsp.numleaffaces;
                bsp2.numsurfedges = mbsp.numsurfedges;

                // copy or convert data
                if (to_version == &bspver_h2bsp2) {
                    CopyArray(mbsp.dmodels, mbsp.nummodels, bsp2.dmodels_h2);
                } else {
                    CopyArray(mbsp.dmodels, mbsp.nummodels, bsp2.dmodels_q);
                }
                bsp2.dvisdata = BSP29_CopyVisData(mbsp.dvisdata, mbsp.visdatasize);
                CopyArray(mbsp.dlightdata, mbsp.lightdatasize, bsp2.dlightdata);
                CopyArray(mbsp.dtexdata, mbsp.texdatasize, bsp2.dtexdata);
                CopyArray(mbsp.dentdata, mbsp.entdatasize, bsp2.dentdata);
                CopyArray(mbsp.dleafs, mbsp.numleafs, bsp2.dleafs);
                CopyArray(mbsp.dplanes, mbsp.numplanes, bsp2.dplanes);
                CopyArray(mbsp.dvertexes, mbsp.numvertexes, bsp2.dvertexes);
                CopyArray(mbsp.dnodes, mbsp.numnodes, bsp2.dnodes);
                CopyArray(mbsp.texinfo, mbsp.numtexinfo, bsp2.texinfo);
                CopyArray(mbsp.dfaces, mbsp.numfaces, bsp2.dfaces);
                CopyArray(mbsp.dclipnodes, mbsp.numclipnodes, bsp2.dclipnodes);
                CopyArray(mbsp.dedges, mbsp.numedges, bsp2.dedges);
                CopyArray(mbsp.dleaffaces, mbsp.numleaffaces, bsp2.dmarksurfaces);
                CopyArray(mbsp.dsurfedges, mbsp.numsurfedges, bsp2.dsurfedges);

                // Conversion complete!
                bspdata->version = to_version;
                bspdata->bsp = std::move(bsp2);

                return true;
            }*/
        }

        Error("Don't know how to convert BSP version {} to {}", BSPVersionString(bspdata->version),
            BSPVersionString(to_version));
    }
    catch (std::overflow_error e)
    {
        LogPrint("LIMITS EXCEEDED ON {}\n", e.what());
        return false;
    }
}

static bool isHexen2(const dheader_t *header)
{
    /*
        the world should always have some face.
        however, if the sizes are wrong then we're actually reading headnode[6]. hexen2 only used 5 hulls, so this
       should be 0 in hexen2, and not in quake.
    */
    const dmodelq1_t *modelsq1 = (const dmodelq1_t *)((const uint8_t *)header + header->lumps[LUMP_MODELS].fileofs);
    return !modelsq1->numfaces;
}

// read structured lump data from stream into vector
template<typename T>
static void ReadLump(std::istream &s, const bspversion_t *version, const std::vector<lump_t> &lumps, size_t lump_num, std::vector<T> &buffer)
{
    const lumpspec_t &lumpspec = version->lumps[lump_num];
    const lump_t &lump = lumps[lump_num];
    size_t length;

    Q_assert(!buffer.size());

    if (lumpspec.size > 1) {
        if (sizeof(T) != lumpspec.size)
            FError("odd {} value size ({} != {})", lumpspec.name, sizeof(T), lumpspec.size);
        else if (lump.filelen % lumpspec.size)
            FError("odd {} lump size ({} not multiple of {})", lumpspec.name, lump.filelen, lumpspec.size);

        buffer.reserve(length = (lump.filelen / lumpspec.size));
    } else {
        buffer.reserve(length = lump.filelen);
    }

    if (!lump.filelen)
        return;

    s.seekg(lump.fileofs);
    
    if (lumpspec.size > 1) {
        T val;

        for (size_t i = 0; i < length; i++) {
            s >= val;
            buffer.push_back(val);
        }
    } else {
        s.read(reinterpret_cast<char *>(buffer.data()), length);
    }
}

// read string from stream
static void ReadLump(std::istream &s, const bspversion_t *version, const std::vector<lump_t> &lumps, size_t lump_num, std::string &buffer)
{
    const lumpspec_t &lumpspec = version->lumps[lump_num];
    const lump_t &lump = lumps[lump_num];

    Q_assert(lumpspec.size == 1);
    Q_assert(!buffer.size());

    buffer.resize(lump.filelen);

    if (!lump.filelen)
        return;

    s.seekg(lump.fileofs);
    
    s.read(reinterpret_cast<char *>(buffer.data()), lump.filelen);

    // in case of bad BSPs, we'll fix it by growing the lump
    if (buffer[lump.filelen]) {
        buffer += '\0';
    }
}

// read structured lump data from stream into struct
template<typename T, typename = std::enable_if_t<std::is_member_function_pointer_v<decltype(&T::stream_read)>>>
static void ReadLump(std::istream &s, const bspversion_t *version, const std::vector<lump_t> &lumps, size_t lump_num, T &buffer)
{
    const lumpspec_t &lumpspec = version->lumps[lump_num];
    const lump_t &lump = lumps[lump_num];

    if (!lump.filelen)
        return;

    Q_assert(lumpspec.size == 1);

    s.seekg(lump.fileofs);

    buffer.stream_read(s, lump.filelen);
}

void BSPX_AddLump(bspdata_t *bspdata, const char *xname, const void *xdata, size_t xsize)
{
    bspxentry_t *e;
    bspxentry_t **link;
    if (!xdata) {
        for (link = &bspdata->bspxentries; *link;) {
            e = *link;
            if (!strcmp(e->lumpname.data(), xname)) {
                *link = e->next;
                delete e;
                break;
            } else
                link = &(*link)->next;
        }
        return;
    }
    for (e = bspdata->bspxentries; e; e = e->next) {
        if (!strcmp(e->lumpname.data(), xname))
            break;
    }
    if (!e) {
        e = new bspxentry_t { };
        strncpy(e->lumpname.data(), xname, sizeof(e->lumpname));
        e->next = bspdata->bspxentries;
        bspdata->bspxentries = e;
    }

    // ericw -- make a copy
    uint8_t *xdata_copy = new uint8_t[xsize];
    memcpy(xdata_copy, xdata, xsize);

    e->lumpdata = xdata_copy;
    e->lumpsize = xsize;
}

const void *BSPX_GetLump(bspdata_t *bspdata, const char *xname, size_t *xsize)
{
    bspxentry_t *e;
    for (e = bspdata->bspxentries; e; e = e->next) {
        if (!strcmp(e->lumpname.data(), xname))
            break;
    }
    if (e) {
        if (xsize)
            *xsize = e->lumpsize;
        return e->lumpdata;
    } else {
        if (xsize)
            *xsize = 0;
        return NULL;
    }
}

/*
 * =============
 * LoadBSPFile
 * =============
 */
void LoadBSPFile(std::filesystem::path &filename, bspdata_t *bspdata)
{
    int i;
    uint32_t bspxofs;
    const bspx_header_t *bspx;

    bspdata->bspxentries = NULL;

    FLogPrint("'{}'\n", filename);

    /* load the file header */
    uint8_t *file_data;
    uint32_t flen = LoadFilePak(filename, &file_data);

    imemstream stream(file_data, flen);
    stream >> endianness<std::endian::little>;

    /* transfer the header data to this */
    std::vector<lump_t> lumps;

    /* check for IBSP */
    bspversion_t temp_version{};
    stream >= temp_version.ident;
    stream.seekg(0);

    if (temp_version.ident == Q2_BSPIDENT || temp_version.ident == Q2_QBISMIDENT) {
        q2_dheader_t q2header;
        stream >= q2header;

        temp_version.version = q2header.version;
        std::copy(q2header.lumps.begin(), q2header.lumps.end(), std::back_inserter(lumps));
    } else {
        dheader_t q1header;
        stream >= q1header;
        
        temp_version.version = NO_VERSION;
        std::copy(q1header.lumps.begin(), q1header.lumps.end(), std::back_inserter(lumps));
    }

    /* check the file version */
    if (!BSPVersionSupported(temp_version.ident, temp_version.version, &bspdata->version)) {
        LogPrint("BSP is version {}\n", BSPVersionString(&temp_version));
        Error("Sorry, this bsp version is not supported.");
    } else {
        // special case handling for Hexen II
        if (bspdata->version->game->id == GAME_QUAKE && isHexen2((dheader_t *)file_data)) {
            if (bspdata->version == &bspver_q1) {
                bspdata->version = &bspver_h2;
            } else if (bspdata->version == &bspver_bsp2) {
                bspdata->version = &bspver_h2bsp2;
            } else if (bspdata->version == &bspver_bsp2rmq) {
                bspdata->version = &bspver_h2bsp2rmq;
            }
        }

        LogPrint("BSP is version {}\n", BSPVersionString(bspdata->version));
    }

    /* copy the data */
    if (bspdata->version == &bspver_q2) {
        q2bsp_t &bsp = bspdata->bsp.emplace<q2bsp_t>();

        ReadLump(stream, bspdata->version, lumps, Q2_LUMP_MODELS, bsp.dmodels);
        ReadLump(stream, bspdata->version, lumps, Q2_LUMP_VERTEXES, bsp.dvertexes);
        ReadLump(stream, bspdata->version, lumps, Q2_LUMP_PLANES, bsp.dplanes);
        ReadLump(stream, bspdata->version, lumps, Q2_LUMP_LEAFS, bsp.dleafs);
        ReadLump(stream, bspdata->version, lumps, Q2_LUMP_NODES, bsp.dnodes);
        ReadLump(stream, bspdata->version, lumps, Q2_LUMP_TEXINFO, bsp.texinfo);
        ReadLump(stream, bspdata->version, lumps, Q2_LUMP_FACES, bsp.dfaces);
        ReadLump(stream, bspdata->version, lumps, Q2_LUMP_LEAFFACES, bsp.dleaffaces);
        ReadLump(stream, bspdata->version, lumps, Q2_LUMP_LEAFBRUSHES, bsp.dleafbrushes);
        ReadLump(stream, bspdata->version, lumps, Q2_LUMP_SURFEDGES, bsp.dsurfedges);
        ReadLump(stream, bspdata->version, lumps, Q2_LUMP_EDGES, bsp.dedges);
        ReadLump(stream, bspdata->version, lumps, Q2_LUMP_BRUSHES, bsp.dbrushes);
        ReadLump(stream, bspdata->version, lumps, Q2_LUMP_BRUSHSIDES, bsp.dbrushsides);
        ReadLump(stream, bspdata->version, lumps, Q2_LUMP_AREAS, bsp.dareas);
        ReadLump(stream, bspdata->version, lumps, Q2_LUMP_AREAPORTALS, bsp.dareaportals);

        ReadLump(stream, bspdata->version, lumps, Q2_LUMP_VISIBILITY, bsp.dvis);
        ReadLump(stream, bspdata->version, lumps, Q2_LUMP_LIGHTING, bsp.dlightdata);
        ReadLump(stream, bspdata->version, lumps, Q2_LUMP_ENTITIES, bsp.dentdata);
    } /*else if (bspdata->version == &bspver_qbism) {
        q2_dheader_t *header = (q2_dheader_t *)file_data;
        q2bsp_qbism_t &bsp = bspdata->bsp.emplace<q2bsp_qbism_t>();

        bsp.nummodels = CopyLump(header, bspdata->version, header->lumps, Q2_LUMP_MODELS, &bsp.dmodels);
        bsp.numvertexes = CopyLump(header, bspdata->version, header->lumps, Q2_LUMP_VERTEXES, &bsp.dvertexes);
        bsp.numplanes = CopyLump(header, bspdata->version, header->lumps, Q2_LUMP_PLANES, &bsp.dplanes);
        bsp.numleafs = CopyLump(header, bspdata->version, header->lumps, Q2_LUMP_LEAFS, &bsp.dleafs);
        bsp.numnodes = CopyLump(header, bspdata->version, header->lumps, Q2_LUMP_NODES, &bsp.dnodes);
        bsp.numtexinfo = CopyLump(header, bspdata->version, header->lumps, Q2_LUMP_TEXINFO, &bsp.texinfo);
        bsp.numfaces = CopyLump(header, bspdata->version, header->lumps, Q2_LUMP_FACES, &bsp.dfaces);
        bsp.numleaffaces = CopyLump(header, bspdata->version, header->lumps, Q2_LUMP_LEAFFACES, &bsp.dleaffaces);
        bsp.numleafbrushes =
            CopyLump(header, bspdata->version, header->lumps, Q2_LUMP_LEAFBRUSHES, &bsp.dleafbrushes);
        bsp.numsurfedges = CopyLump(header, bspdata->version, header->lumps, Q2_LUMP_SURFEDGES, &bsp.dsurfedges);
        bsp.numedges = CopyLump(header, bspdata->version, header->lumps, Q2_LUMP_EDGES, &bsp.dedges);
        bsp.numbrushes = CopyLump(header, bspdata->version, header->lumps, Q2_LUMP_BRUSHES, &bsp.dbrushes);
        bsp.numbrushsides = CopyLump(header, bspdata->version, header->lumps, Q2_LUMP_BRUSHSIDES, &bsp.dbrushsides);
        bsp.numareas = CopyLump(header, bspdata->version, header->lumps, Q2_LUMP_AREAS, &bsp.dareas);
        bsp.numareaportals =
            CopyLump(header, bspdata->version, header->lumps, Q2_LUMP_AREAPORTALS, &bsp.dareaportals);

        bsp.visdatasize = CopyLump(header, bspdata->version, header->lumps, Q2_LUMP_VISIBILITY, &bsp.dvisdata);
        bsp.lightdatasize = CopyLump(header, bspdata->version, header->lumps, Q2_LUMP_LIGHTING, &bsp.dlightdata);
        bsp.entdatasize = CopyLump(header, bspdata->version, header->lumps, Q2_LUMP_ENTITIES, &bsp.dentdata);
    } else if (bspdata->version == &bspver_q1 || bspdata->version == &bspver_h2 || bspdata->version == &bspver_hl) {
        dheader_t *header = (dheader_t *)file_data;
        bsp29_t &bsp = bspdata->bsp.emplace<bsp29_t>();

        if (bspdata->version == &bspver_h2) {
            bsp.nummodels = CopyLump(header, bspdata->version, header->lumps, LUMP_MODELS, &bsp.dmodels_h2);
        } else {
            bsp.nummodels = CopyLump(header, bspdata->version, header->lumps, LUMP_MODELS, &bsp.dmodels_q);
        }
        bsp.numvertexes = CopyLump(header, bspdata->version, header->lumps, LUMP_VERTEXES, &bsp.dvertexes);
        bsp.numplanes = CopyLump(header, bspdata->version, header->lumps, LUMP_PLANES, &bsp.dplanes);
        bsp.numleafs = CopyLump(header, bspdata->version, header->lumps, LUMP_LEAFS, &bsp.dleafs);
        bsp.numnodes = CopyLump(header, bspdata->version, header->lumps, LUMP_NODES, &bsp.dnodes);
        bsp.numtexinfo = CopyLump(header, bspdata->version, header->lumps, LUMP_TEXINFO, &bsp.texinfo);
        bsp.numclipnodes = CopyLump(header, bspdata->version, header->lumps, LUMP_CLIPNODES, &bsp.dclipnodes);
        bsp.numfaces = CopyLump(header, bspdata->version, header->lumps, LUMP_FACES, &bsp.dfaces);
        bsp.nummarksurfaces =
            CopyLump(header, bspdata->version, header->lumps, LUMP_MARKSURFACES, &bsp.dmarksurfaces);
        bsp.numsurfedges = CopyLump(header, bspdata->version, header->lumps, LUMP_SURFEDGES, &bsp.dsurfedges);
        bsp.numedges = CopyLump(header, bspdata->version, header->lumps, LUMP_EDGES, &bsp.dedges);

        bsp.texdatasize = CopyLump(header, bspdata->version, header->lumps, LUMP_TEXTURES, &bsp.dtexdata);
        bsp.visdatasize = CopyLump(header, bspdata->version, header->lumps, LUMP_VISIBILITY, &bsp.dvisdata);
        bsp.lightdatasize = CopyLump(header, bspdata->version, header->lumps, LUMP_LIGHTING, &bsp.dlightdata);
        bsp.entdatasize = CopyLump(header, bspdata->version, header->lumps, LUMP_ENTITIES, &bsp.dentdata);
    } else if (bspdata->version == &bspver_bsp2rmq || bspdata->version == &bspver_h2bsp2rmq) {
        dheader_t *header = (dheader_t *)file_data;
        bsp2rmq_t &bsp = bspdata->bsp.emplace<bsp2rmq_t>();

        if (bspdata->version == &bspver_h2bsp2rmq) {
            bsp.nummodels = CopyLump(header, bspdata->version, header->lumps, LUMP_MODELS, &bsp.dmodels_h2);
        } else {
            bsp.nummodels = CopyLump(header, bspdata->version, header->lumps, LUMP_MODELS, &bsp.dmodels_q);
        }
        bsp.numvertexes = CopyLump(header, bspdata->version, header->lumps, LUMP_VERTEXES, &bsp.dvertexes);
        bsp.numplanes = CopyLump(header, bspdata->version, header->lumps, LUMP_PLANES, &bsp.dplanes);
        bsp.numleafs = CopyLump(header, bspdata->version, header->lumps, LUMP_LEAFS, &bsp.dleafs);
        bsp.numnodes = CopyLump(header, bspdata->version, header->lumps, LUMP_NODES, &bsp.dnodes);
        bsp.numtexinfo = CopyLump(header, bspdata->version, header->lumps, LUMP_TEXINFO, &bsp.texinfo);
        bsp.numclipnodes = CopyLump(header, bspdata->version, header->lumps, LUMP_CLIPNODES, &bsp.dclipnodes);
        bsp.numfaces = CopyLump(header, bspdata->version, header->lumps, LUMP_FACES, &bsp.dfaces);
        bsp.nummarksurfaces =
            CopyLump(header, bspdata->version, header->lumps, LUMP_MARKSURFACES, &bsp.dmarksurfaces);
        bsp.numsurfedges = CopyLump(header, bspdata->version, header->lumps, LUMP_SURFEDGES, &bsp.dsurfedges);
        bsp.numedges = CopyLump(header, bspdata->version, header->lumps, LUMP_EDGES, &bsp.dedges);

        bsp.texdatasize = CopyLump(header, bspdata->version, header->lumps, LUMP_TEXTURES, &bsp.dtexdata);
        bsp.visdatasize = CopyLump(header, bspdata->version, header->lumps, LUMP_VISIBILITY, &bsp.dvisdata);
        bsp.lightdatasize = CopyLump(header, bspdata->version, header->lumps, LUMP_LIGHTING, &bsp.dlightdata);
        bsp.entdatasize = CopyLump(header, bspdata->version, header->lumps, LUMP_ENTITIES, &bsp.dentdata);
    } else if (bspdata->version == &bspver_bsp2 || bspdata->version == &bspver_h2bsp2) {
        dheader_t *header = (dheader_t *)file_data;
        bsp2_t &bsp = bspdata->bsp.emplace<bsp2_t>();

        if (bspdata->version == &bspver_h2bsp2) {
            bsp.nummodels = CopyLump(header, bspdata->version, header->lumps, LUMP_MODELS, &bsp.dmodels_h2);
        } else {
            bsp.nummodels = CopyLump(header, bspdata->version, header->lumps, LUMP_MODELS, &bsp.dmodels_q);
        }
        bsp.numvertexes = CopyLump(header, bspdata->version, header->lumps, LUMP_VERTEXES, &bsp.dvertexes);
        bsp.numplanes = CopyLump(header, bspdata->version, header->lumps, LUMP_PLANES, &bsp.dplanes);
        bsp.numleafs = CopyLump(header, bspdata->version, header->lumps, LUMP_LEAFS, &bsp.dleafs);
        bsp.numnodes = CopyLump(header, bspdata->version, header->lumps, LUMP_NODES, &bsp.dnodes);
        bsp.numtexinfo = CopyLump(header, bspdata->version, header->lumps, LUMP_TEXINFO, &bsp.texinfo);
        bsp.numclipnodes = CopyLump(header, bspdata->version, header->lumps, LUMP_CLIPNODES, &bsp.dclipnodes);
        bsp.numfaces = CopyLump(header, bspdata->version, header->lumps, LUMP_FACES, &bsp.dfaces);
        bsp.nummarksurfaces =
            CopyLump(header, bspdata->version, header->lumps, LUMP_MARKSURFACES, &bsp.dmarksurfaces);
        bsp.numsurfedges = CopyLump(header, bspdata->version, header->lumps, LUMP_SURFEDGES, &bsp.dsurfedges);
        bsp.numedges = CopyLump(header, bspdata->version, header->lumps, LUMP_EDGES, &bsp.dedges);

        bsp.texdatasize = CopyLump(header, bspdata->version, header->lumps, LUMP_TEXTURES, &bsp.dtexdata);
        bsp.visdatasize = CopyLump(header, bspdata->version, header->lumps, LUMP_VISIBILITY, &bsp.dvisdata);
        bsp.lightdatasize = CopyLump(header, bspdata->version, header->lumps, LUMP_LIGHTING, &bsp.dlightdata);
        bsp.entdatasize = CopyLump(header, bspdata->version, header->lumps, LUMP_ENTITIES, &bsp.dentdata);
    }*/ else {
        FError("Unknown format");
    }

    // detect BSPX
    dheader_t *header = (dheader_t *)file_data;

    /*bspx header is positioned exactly+4align at the end of the last lump position (regardless of order)*/
    for (i = 0, bspxofs = 0; i < BSP_LUMPS; i++) {
        if (bspxofs < header->lumps[i].fileofs + header->lumps[i].filelen)
            bspxofs = header->lumps[i].fileofs + header->lumps[i].filelen;
    }
    bspxofs = (bspxofs + 3) & ~3;
    /*okay, so that's where it *should* be if it exists */
    if (bspxofs + sizeof(*bspx) <= flen) {
        int xlumps;
        const bspx_lump_t *xlump;
        bspx = (const bspx_header_t *)((const uint8_t *)header + bspxofs);
        xlump = (const bspx_lump_t *)(bspx + 1);
        xlumps = LittleLong(bspx->numlumps);
        if (!memcmp(&bspx->id, "BSPX", 4) && xlumps >= 0 && bspxofs + sizeof(*bspx) + sizeof(*xlump) * xlumps <= flen) {
            /*header seems valid so far. just add the lumps as we normally would if we were generating them, ensuring
             * that they get written out anew*/
            while (xlumps-- > 0) {
                uint32_t ofs = LittleLong(xlump[xlumps].fileofs);
                uint32_t len = LittleLong(xlump[xlumps].filelen);
                void *lumpdata = new uint8_t[len];
                memcpy(lumpdata, (const uint8_t *)header + ofs, len);
                BSPX_AddLump(bspdata, xlump[xlumps].lumpname.data(), lumpdata, len);
            }
        } else {
            if (!memcmp(&bspx->id, "BSPX", 4))
                printf("invalid bspx header\n");
        }
    }

    /* everything has been copied out */
    delete[] file_data;
}

/* ========================================================================= */
#include <fstream>

struct bspfile_t
{
    const bspversion_t *version;

    // which one is used depends on version
    union
    {
        dheader_t q1header;
        q2_dheader_t q2header;
    };

    std::ofstream stream;
};

/*static void AddLump(bspfile_t *bspfile, int lumpnum, const void *data, int count)
{
    bool q2 = false;
    size_t size;
    const lumpspec_t &lumpspec = bspfile->version->lumps[lumpnum];
    lump_t *lumps;

    if (bspfile->version->version != NO_VERSION) {
        lumps = bspfile->q2header.lumps.data();
    } else {
        lumps = bspfile->q1header.lumps.data();
    }

    size = lumpspec.size * count;

    uint8_t pad[4] = {0};
    lump_t *lump = &lumps[lumpnum];

    lump->fileofs = LittleLong(SafeTell(bspfile->file));
    lump->filelen = LittleLong(size);
    SafeWrite(bspfile->file, data, size);
    if (size % 4)
        SafeWrite(bspfile->file, pad, 4 - (size % 4));
}*/

// write structured lump data from vector
template<typename T>
static void WriteLump(bspfile_t &bspfile, size_t lump_num, const std::vector<T> &data)
{
    static constexpr char pad[4] { };
    const lumpspec_t &lumpspec = bspfile.version->lumps[lump_num];
    lump_t *lumps;

    if (bspfile.version->version != NO_VERSION) {
        lumps = bspfile.q2header.lumps.data();
    } else {
        lumps = bspfile.q1header.lumps.data();
    }

    lump_t &lump = lumps[lump_num];

    lump.fileofs = bspfile.stream.tellp();

    for (auto &v : data)
        bspfile.stream <= v;

    auto written = static_cast<int32_t>(bspfile.stream.tellp()) - lump.fileofs;

    if (sizeof(T) == 1 || lumpspec.size > 1)
        Q_assert(written == (lumpspec.size * data.size()));

    lump.filelen = written;

    if (written % 4)
        bspfile.stream.write(pad, 4 - (written % 4));
}

// write structured string data
static void WriteLump(bspfile_t &bspfile, size_t lump_num, const std::string &data)
{
    static constexpr char pad[4] { };
    const lumpspec_t &lumpspec = bspfile.version->lumps[lump_num];
    lump_t *lumps;

    Q_assert(lumpspec.size == 1);

    if (bspfile.version->version != NO_VERSION) {
        lumps = bspfile.q2header.lumps.data();
    } else {
        lumps = bspfile.q1header.lumps.data();
    }

    lump_t &lump = lumps[lump_num];

    lump.fileofs = bspfile.stream.tellp();

    bspfile.stream.write(data.c_str(), data.size() + 1); // null terminator

    auto written = static_cast<int32_t>(bspfile.stream.tellp()) - lump.fileofs;

    Q_assert(written == data.size() + 1);

    lump.filelen = written;

    if (written % 4)
        bspfile.stream.write(pad, 4 - (written % 4));
}

// write structured lump data
template<typename T, typename = std::enable_if_t<std::is_member_function_pointer_v<decltype(&T::stream_write)>>>
static void WriteLump(bspfile_t &bspfile, size_t lump_num, const T &data)
{
    static constexpr char pad[4] { };
    const lumpspec_t &lumpspec = bspfile.version->lumps[lump_num];
    lump_t *lumps;

    Q_assert(lumpspec.size == 1);

    if (bspfile.version->version != NO_VERSION) {
        lumps = bspfile.q2header.lumps.data();
    } else {
        lumps = bspfile.q1header.lumps.data();
    }

    lump_t &lump = lumps[lump_num];

    lump.fileofs = bspfile.stream.tellp();

    data.stream_write(bspfile.stream);

    auto written = static_cast<int32_t>(bspfile.stream.tellp()) - lump.fileofs;

    lump.filelen = written;

    if (written % 4)
        bspfile.stream.write(pad, 4 - (written % 4));
}

/*
 * =============
 * WriteBSPFile
 * Swaps the bsp file in place, so it should not be referenced again
 * =============
 */
void WriteBSPFile(const std::filesystem::path &filename, bspdata_t *bspdata)
{
    bspfile_t bspfile { };

    bspfile.version = bspdata->version;

    // headers are union'd, so this sets both
    bspfile.q2header.ident = bspfile.version->ident;

    if (bspfile.version->version != NO_VERSION) {
        bspfile.q2header.version = bspfile.version->version;
    }

    LogPrint("Writing {} as BSP version {}\n", filename, BSPVersionString(bspdata->version));
    bspfile.stream.open(filename, std::ios_base::out | std::ios_base::trunc | std::ios_base::binary);

    if (!bspfile.stream)
        FError("unable to open {} for writing", filename);

    bspfile.stream << endianness<std::endian::little>;

    /* Save header space, updated after adding the lumps */
    if (bspfile.version->version != NO_VERSION) {
        bspfile.stream <= bspfile.q2header;
    } else {
        bspfile.stream <= bspfile.q1header;
    }

    /*if (std::holds_alternative<bsp29_t>(bspdata->bsp)) {
        const bsp29_t &bsp = std::get<bsp29_t>(bspdata->bsp);

        AddLump(&bspfile, LUMP_PLANES, bsp.dplanes, bsp.numplanes);
        AddLump(&bspfile, LUMP_LEAFS, bsp.dleafs, bsp.numleafs);
        AddLump(&bspfile, LUMP_VERTEXES, bsp.dvertexes, bsp.numvertexes);
        AddLump(&bspfile, LUMP_NODES, bsp.dnodes, bsp.numnodes);
        AddLump(&bspfile, LUMP_TEXINFO, bsp.texinfo, bsp.numtexinfo);
        AddLump(&bspfile, LUMP_FACES, bsp.dfaces, bsp.numfaces);
        AddLump(&bspfile, LUMP_CLIPNODES, bsp.dclipnodes, bsp.numclipnodes);
        AddLump(&bspfile, LUMP_MARKSURFACES, bsp.dmarksurfaces, bsp.nummarksurfaces);
        AddLump(&bspfile, LUMP_SURFEDGES, bsp.dsurfedges, bsp.numsurfedges);
        AddLump(&bspfile, LUMP_EDGES, bsp.dedges, bsp.numedges);
        if (bspdata->version == &bspver_h2) {
            AddLump(&bspfile, LUMP_MODELS, bsp.dmodels_h2, bsp.nummodels);
        } else {
            AddLump(&bspfile, LUMP_MODELS, bsp.dmodels_q, bsp.nummodels);
        }

        AddLump(&bspfile, LUMP_LIGHTING, bsp.dlightdata, bsp.lightdatasize);
        AddLump(&bspfile, LUMP_VISIBILITY, bsp.dvisdata, bsp.visdatasize);
        AddLump(&bspfile, LUMP_ENTITIES, bsp.dentdata, bsp.entdatasize);
        AddLump(&bspfile, LUMP_TEXTURES, bsp.dtexdata, bsp.texdatasize);
    } else if (std::holds_alternative<bsp2rmq_t>(bspdata->bsp)) {
        const bsp2rmq_t &bsp = std::get<bsp2rmq_t>(bspdata->bsp);

        AddLump(&bspfile, LUMP_PLANES, bsp.dplanes, bsp.numplanes);
        AddLump(&bspfile, LUMP_LEAFS, bsp.dleafs, bsp.numleafs);
        AddLump(&bspfile, LUMP_VERTEXES, bsp.dvertexes, bsp.numvertexes);
        AddLump(&bspfile, LUMP_NODES, bsp.dnodes, bsp.numnodes);
        AddLump(&bspfile, LUMP_TEXINFO, bsp.texinfo, bsp.numtexinfo);
        AddLump(&bspfile, LUMP_FACES, bsp.dfaces, bsp.numfaces);
        AddLump(&bspfile, LUMP_CLIPNODES, bsp.dclipnodes, bsp.numclipnodes);
        AddLump(&bspfile, LUMP_MARKSURFACES, bsp.dmarksurfaces, bsp.nummarksurfaces);
        AddLump(&bspfile, LUMP_SURFEDGES, bsp.dsurfedges, bsp.numsurfedges);
        AddLump(&bspfile, LUMP_EDGES, bsp.dedges, bsp.numedges);
        if (bspdata->version == &bspver_h2bsp2rmq) {
            AddLump(&bspfile, LUMP_MODELS, bsp.dmodels_h2, bsp.nummodels);
        } else {
            AddLump(&bspfile, LUMP_MODELS, bsp.dmodels_q, bsp.nummodels);
        }

        AddLump(&bspfile, LUMP_LIGHTING, bsp.dlightdata, bsp.lightdatasize);
        AddLump(&bspfile, LUMP_VISIBILITY, bsp.dvisdata, bsp.visdatasize);
        AddLump(&bspfile, LUMP_ENTITIES, bsp.dentdata, bsp.entdatasize);
        AddLump(&bspfile, LUMP_TEXTURES, bsp.dtexdata, bsp.texdatasize);
    } else if (std::holds_alternative<bsp2_t>(bspdata->bsp)) {
        const bsp2_t &bsp = std::get<bsp2_t>(bspdata->bsp);

        AddLump(&bspfile, LUMP_PLANES, bsp.dplanes, bsp.numplanes);
        AddLump(&bspfile, LUMP_LEAFS, bsp.dleafs, bsp.numleafs);
        AddLump(&bspfile, LUMP_VERTEXES, bsp.dvertexes, bsp.numvertexes);
        AddLump(&bspfile, LUMP_NODES, bsp.dnodes, bsp.numnodes);
        AddLump(&bspfile, LUMP_TEXINFO, bsp.texinfo, bsp.numtexinfo);
        AddLump(&bspfile, LUMP_FACES, bsp.dfaces, bsp.numfaces);
        AddLump(&bspfile, LUMP_CLIPNODES, bsp.dclipnodes, bsp.numclipnodes);
        AddLump(&bspfile, LUMP_MARKSURFACES, bsp.dmarksurfaces, bsp.nummarksurfaces);
        AddLump(&bspfile, LUMP_SURFEDGES, bsp.dsurfedges, bsp.numsurfedges);
        AddLump(&bspfile, LUMP_EDGES, bsp.dedges, bsp.numedges);
        if (bspdata->version == &bspver_h2bsp2) {
            AddLump(&bspfile, LUMP_MODELS, bsp.dmodels_h2, bsp.nummodels);
        } else {
            AddLump(&bspfile, LUMP_MODELS, bsp.dmodels_q, bsp.nummodels);
        }

        AddLump(&bspfile, LUMP_LIGHTING, bsp.dlightdata, bsp.lightdatasize);
        AddLump(&bspfile, LUMP_VISIBILITY, bsp.dvisdata, bsp.visdatasize);
        AddLump(&bspfile, LUMP_ENTITIES, bsp.dentdata, bsp.entdatasize);
        AddLump(&bspfile, LUMP_TEXTURES, bsp.dtexdata, bsp.texdatasize);
    } else */if (std::holds_alternative<q2bsp_t>(bspdata->bsp)) {
        const q2bsp_t &bsp = std::get<q2bsp_t>(bspdata->bsp);

        WriteLump(bspfile, Q2_LUMP_MODELS, bsp.dmodels);
        WriteLump(bspfile, Q2_LUMP_VERTEXES, bsp.dvertexes);
        WriteLump(bspfile, Q2_LUMP_PLANES, bsp.dplanes);
        WriteLump(bspfile, Q2_LUMP_LEAFS, bsp.dleafs);
        WriteLump(bspfile, Q2_LUMP_NODES, bsp.dnodes);
        WriteLump(bspfile, Q2_LUMP_TEXINFO, bsp.texinfo);
        WriteLump(bspfile, Q2_LUMP_FACES, bsp.dfaces);
        WriteLump(bspfile, Q2_LUMP_LEAFFACES, bsp.dleaffaces);
        WriteLump(bspfile, Q2_LUMP_LEAFBRUSHES, bsp.dleafbrushes);
        WriteLump(bspfile, Q2_LUMP_SURFEDGES, bsp.dsurfedges);
        WriteLump(bspfile, Q2_LUMP_EDGES, bsp.dedges);
        WriteLump(bspfile, Q2_LUMP_BRUSHES, bsp.dbrushes);
        WriteLump(bspfile, Q2_LUMP_BRUSHSIDES, bsp.dbrushsides);
        WriteLump(bspfile, Q2_LUMP_AREAS, bsp.dareas);
        WriteLump(bspfile, Q2_LUMP_AREAPORTALS, bsp.dareaportals);

        WriteLump(bspfile, Q2_LUMP_VISIBILITY, bsp.dvis);
        WriteLump(bspfile, Q2_LUMP_LIGHTING, bsp.dlightdata);
        WriteLump(bspfile, Q2_LUMP_ENTITIES, bsp.dentdata);
    } /*else if (std::holds_alternative<q2bsp_qbism_t>(bspdata->bsp)) {
        const q2bsp_qbism_t &bsp = std::get<q2bsp_qbism_t>(bspdata->bsp);

        AddLump(&bspfile, Q2_LUMP_MODELS, bsp.dmodels, bsp.nummodels);
        AddLump(&bspfile, Q2_LUMP_VERTEXES, bsp.dvertexes, bsp.numvertexes);
        AddLump(&bspfile, Q2_LUMP_PLANES, bsp.dplanes, bsp.numplanes);
        AddLump(&bspfile, Q2_LUMP_LEAFS, bsp.dleafs, bsp.numleafs);
        AddLump(&bspfile, Q2_LUMP_NODES, bsp.dnodes, bsp.numnodes);
        AddLump(&bspfile, Q2_LUMP_TEXINFO, bsp.texinfo, bsp.numtexinfo);
        AddLump(&bspfile, Q2_LUMP_FACES, bsp.dfaces, bsp.numfaces);
        AddLump(&bspfile, Q2_LUMP_LEAFFACES, bsp.dleaffaces, bsp.numleaffaces);
        AddLump(&bspfile, Q2_LUMP_LEAFBRUSHES, bsp.dleafbrushes, bsp.numleafbrushes);
        AddLump(&bspfile, Q2_LUMP_SURFEDGES, bsp.dsurfedges, bsp.numsurfedges);
        AddLump(&bspfile, Q2_LUMP_EDGES, bsp.dedges, bsp.numedges);
        AddLump(&bspfile, Q2_LUMP_BRUSHES, bsp.dbrushes, bsp.numbrushes);
        AddLump(&bspfile, Q2_LUMP_BRUSHSIDES, bsp.dbrushsides, bsp.numbrushsides);
        AddLump(&bspfile, Q2_LUMP_AREAS, bsp.dareas, bsp.numareas);
        AddLump(&bspfile, Q2_LUMP_AREAPORTALS, bsp.dareaportals, bsp.numareaportals);

        AddLump(&bspfile, Q2_LUMP_VISIBILITY, bsp.dvis, bsp.visdatasize);
        AddLump(&bspfile, Q2_LUMP_LIGHTING, bsp.dlightdata, bsp.lightdatasize);
        AddLump(&bspfile, Q2_LUMP_ENTITIES, bsp.dentdata, bsp.entdatasize);
        AddLump(&bspfile, Q2_LUMP_POP, bsp.dpop, sizeof(bsp.dpop));
    } */else {
        FError("Unknown format");
    }

    /*BSPX lumps are at a 4-byte alignment after the last of any official lump*/
    if (bspdata->bspxentries) {
        bspx_header_t xheader;
        bspxentry_t *x;
        bspx_lump_t xlumps[64];
        uint32_t l;

        if (bspfile.stream.tellp() & 3)
            FError("BSPX header is misaligned");

        for (x = bspdata->bspxentries; x; x = x->next)
            xheader.numlumps++;

        xheader.numlumps = std::min(xheader.numlumps, (uint32_t) std::size(xlumps)); // FIXME

        bspfile.stream <= xheader;

        auto bspxheader = bspfile.stream.tellp();

        for (size_t i = 0; i < xheader.numlumps; i++)
            bspfile.stream <= xlumps[i];

        for (x = bspdata->bspxentries, l = 0; x && l < xheader.numlumps; x = x->next, l++) {
            static constexpr char pad[4] {};

            xlumps[l].filelen = x->lumpsize;
            xlumps[l].fileofs = bspfile.stream.tellp();
            xlumps[l].lumpname = x->lumpname;

            bspfile.stream.write(reinterpret_cast<const char *>(x->lumpdata), x->lumpsize);

            if (x->lumpsize % 4)
                bspfile.stream.write(pad, 4 - (x->lumpsize % 4));
        }

        bspfile.stream.seekp(bspxheader);

        for (size_t i = 0; i < xheader.numlumps; i++)
            bspfile.stream <= xlumps[i];
    }

    bspfile.stream.seekp(0);

    // write the real header
    if (bspfile.version->version != NO_VERSION) {
        bspfile.stream <= bspfile.q2header;
    } else {
        bspfile.stream <= bspfile.q1header;
    }
}

/* ========================================================================= */

static void PrintLumpSize(const lumpspec_t *lumpspec, int lumptype, int count)
{
    const lumpspec_t *lump = &lumpspec[lumptype];
    LogPrint("{:7} {:<12} {:10}\n", count, lump->name, count * (int)lump->size);
}

/*
 * =============
 * PrintBSPFileSizes
 * Dumps info about the bsp data
 * =============
 */
void PrintBSPFileSizes(const bspdata_t *bspdata)
{
    int numtextures = 0;
    const lumpspec_t *lumpspec = bspdata->version->lumps;

    if (std::holds_alternative<q2bsp_t>(bspdata->bsp)) {
        const q2bsp_t &bsp = std::get<q2bsp_t>(bspdata->bsp);

        LogPrint("{:7} {:<12}\n", bsp.dmodels.size(), "models");

        PrintLumpSize(lumpspec, Q2_LUMP_PLANES, bsp.dplanes.size());
        PrintLumpSize(lumpspec, Q2_LUMP_VERTEXES, bsp.dvertexes.size());
        PrintLumpSize(lumpspec, Q2_LUMP_NODES, bsp.dnodes.size());
        PrintLumpSize(lumpspec, Q2_LUMP_TEXINFO, bsp.texinfo.size());
        PrintLumpSize(lumpspec, Q2_LUMP_FACES, bsp.dfaces.size());
        PrintLumpSize(lumpspec, Q2_LUMP_LEAFS, bsp.dleafs.size());
        PrintLumpSize(lumpspec, Q2_LUMP_LEAFFACES, bsp.dleaffaces.size());
        PrintLumpSize(lumpspec, Q2_LUMP_LEAFBRUSHES, bsp.dleafbrushes.size());
        PrintLumpSize(lumpspec, Q2_LUMP_EDGES, bsp.dedges.size());
        PrintLumpSize(lumpspec, Q2_LUMP_SURFEDGES, bsp.dsurfedges.size());
        PrintLumpSize(lumpspec, Q2_LUMP_BRUSHES, bsp.dbrushes.size());
        PrintLumpSize(lumpspec, Q2_LUMP_BRUSHSIDES, bsp.dbrushsides.size());
        PrintLumpSize(lumpspec, Q2_LUMP_AREAS, bsp.dareas.size());
        PrintLumpSize(lumpspec, Q2_LUMP_AREAPORTALS, bsp.dareaportals.size());

        LogPrint("{:7} {:<12} {:10}\n", "", "lightdata", bsp.dlightdata.size());
        LogPrint("{:7} {:<12} {:10}\n", "", "visdata", bsp.dvis.bits.size());
        LogPrint("{:7} {:<12} {:10}\n", "", "entdata", bsp.dentdata.size());
    } /*else if (std::holds_alternative<q2bsp_qbism_t>(bspdata->bsp)) {
        const q2bsp_qbism_t &bsp = std::get<q2bsp_qbism_t>(bspdata->bsp);

        LogPrint("{:7} {:<12}\n", bsp.nummodels, "models");

        PrintLumpSize(lumpspec, Q2_LUMP_PLANES, bsp.numplanes);
        PrintLumpSize(lumpspec, Q2_LUMP_VERTEXES, bsp.numvertexes);
        PrintLumpSize(lumpspec, Q2_LUMP_NODES, bsp.numnodes);
        PrintLumpSize(lumpspec, Q2_LUMP_TEXINFO, bsp.numtexinfo);
        PrintLumpSize(lumpspec, Q2_LUMP_FACES, bsp.numfaces);
        PrintLumpSize(lumpspec, Q2_LUMP_LEAFS, bsp.numleafs);
        PrintLumpSize(lumpspec, Q2_LUMP_LEAFFACES, bsp.numleaffaces);
        PrintLumpSize(lumpspec, Q2_LUMP_LEAFBRUSHES, bsp.numleafbrushes);
        PrintLumpSize(lumpspec, Q2_LUMP_EDGES, bsp.numedges);
        PrintLumpSize(lumpspec, Q2_LUMP_SURFEDGES, bsp.numsurfedges);
        PrintLumpSize(lumpspec, Q2_LUMP_BRUSHES, bsp.numbrushes);
        PrintLumpSize(lumpspec, Q2_LUMP_BRUSHSIDES, bsp.numbrushsides);
        PrintLumpSize(lumpspec, Q2_LUMP_AREAS, bsp.numareas);
        PrintLumpSize(lumpspec, Q2_LUMP_AREAPORTALS, bsp.numareaportals);

        LogPrint("{:7} {:<12} {:10}\n", "", "lightdata", bsp.lightdatasize);
        LogPrint("{:7} {:<12} {:10}\n", "", "visdata", bsp.visdatasize);
        LogPrint("{:7} {:<12} {:10}\n", "", "entdata", bsp.entdatasize);
    } else if (std::holds_alternative<bsp29_t>(bspdata->bsp)) {
        const bsp29_t &bsp = std::get<bsp29_t>(bspdata->bsp);

        if (bsp.texdatasize)
            numtextures = bsp.dtexdata->nummiptex;

        LogPrint("{:7} {:<12}\n", bsp.nummodels, "models");

        PrintLumpSize(lumpspec, LUMP_PLANES, bsp.numplanes);
        PrintLumpSize(lumpspec, LUMP_VERTEXES, bsp.numvertexes);
        PrintLumpSize(lumpspec, LUMP_NODES, bsp.numnodes);
        PrintLumpSize(lumpspec, LUMP_TEXINFO, bsp.numtexinfo);
        PrintLumpSize(lumpspec, LUMP_FACES, bsp.numfaces);
        PrintLumpSize(lumpspec, LUMP_CLIPNODES, bsp.numclipnodes);
        PrintLumpSize(lumpspec, LUMP_LEAFS, bsp.numleafs);
        PrintLumpSize(lumpspec, LUMP_MARKSURFACES, bsp.nummarksurfaces);
        PrintLumpSize(lumpspec, LUMP_EDGES, bsp.numedges);
        PrintLumpSize(lumpspec, LUMP_SURFEDGES, bsp.numsurfedges);

        LogPrint("{:7} {:<12} {:10}\n", numtextures, "textures", bsp.texdatasize);
        LogPrint("{:7} {:<12} {:10}\n", "", "lightdata", bsp.lightdatasize);
        LogPrint("{:7} {:<12} {:10}\n", "", "visdata", bsp.visdatasize);
        LogPrint("{:7} {:<12} {:10}\n", "", "entdata", bsp.entdatasize);
    } else if (std::holds_alternative<bsp2rmq_t>(bspdata->bsp)) {
        const bsp2rmq_t &bsp = std::get<bsp2rmq_t>(bspdata->bsp);

        if (bsp.texdatasize)
            numtextures = bsp.dtexdata->nummiptex;

        LogPrint("{:7} {:<12}\n", bsp.nummodels, "models");

        PrintLumpSize(lumpspec, LUMP_PLANES, bsp.numplanes);
        PrintLumpSize(lumpspec, LUMP_VERTEXES, bsp.numvertexes);
        PrintLumpSize(lumpspec, LUMP_NODES, bsp.numnodes);
        PrintLumpSize(lumpspec, LUMP_TEXINFO, bsp.numtexinfo);
        PrintLumpSize(lumpspec, LUMP_FACES, bsp.numfaces);
        PrintLumpSize(lumpspec, LUMP_CLIPNODES, bsp.numclipnodes);
        PrintLumpSize(lumpspec, LUMP_LEAFS, bsp.numleafs);
        PrintLumpSize(lumpspec, LUMP_MARKSURFACES, bsp.nummarksurfaces);
        PrintLumpSize(lumpspec, LUMP_EDGES, bsp.numedges);
        PrintLumpSize(lumpspec, LUMP_SURFEDGES, bsp.numsurfedges);

        LogPrint("{:7} {:<12} {:10}\n", numtextures, "textures", bsp.texdatasize);
        LogPrint("{:7} {:<12} {:10}\n", "", "lightdata", bsp.lightdatasize);
        LogPrint("{:7} {:<12} {:10}\n", "", "visdata", bsp.visdatasize);
        LogPrint("{:7} {:<12} {:10}\n", "", "entdata", bsp.entdatasize);
    } else if (std::holds_alternative<bsp2_t>(bspdata->bsp)) {
        const bsp2_t &bsp = std::get<bsp2_t>(bspdata->bsp);

        if (bsp.texdatasize)
            numtextures = bsp.dtexdata->nummiptex;

        LogPrint("{:7} {:<12}\n", bsp.nummodels, "models");

        PrintLumpSize(lumpspec, LUMP_PLANES, bsp.numplanes);
        PrintLumpSize(lumpspec, LUMP_VERTEXES, bsp.numvertexes);
        PrintLumpSize(lumpspec, LUMP_NODES, bsp.numnodes);
        PrintLumpSize(lumpspec, LUMP_TEXINFO, bsp.numtexinfo);
        PrintLumpSize(lumpspec, LUMP_FACES, bsp.numfaces);
        PrintLumpSize(lumpspec, LUMP_CLIPNODES, bsp.numclipnodes);
        PrintLumpSize(lumpspec, LUMP_LEAFS, bsp.numleafs);
        PrintLumpSize(lumpspec, LUMP_MARKSURFACES, bsp.nummarksurfaces);
        PrintLumpSize(lumpspec, LUMP_EDGES, bsp.numedges);
        PrintLumpSize(lumpspec, LUMP_SURFEDGES, bsp.numsurfedges);

        LogPrint("{:7} {:<12} {:10}\n", numtextures, "textures", bsp.texdatasize);
        LogPrint("{:7} {:<12} {:10}\n", "", "lightdata", bsp.lightdatasize);
        LogPrint("{:7} {:<12} {:10}\n", "", "visdata", bsp.visdatasize);
        LogPrint("{:7} {:<12} {:10}\n", "", "entdata", bsp.entdatasize);
    } */else {
        Error("Unsupported BSP version: {}", BSPVersionString(bspdata->version));
    }

    if (bspdata->bspxentries) {
        bspxentry_t *x;
        for (x = bspdata->bspxentries; x; x = x->next) {
            LogPrint("{:7} {:<12} {:10}\n", "BSPX", x->lumpname.data(), x->lumpsize);
        }
    }
}

/*
  ===============
  CompressRow
  ===============
*/
int CompressRow(const uint8_t *vis, const int numbytes, uint8_t *out)
{
    int i, rep;
    uint8_t *dst;

    dst = out;
    for (i = 0; i < numbytes; i++) {
        *dst++ = vis[i];
        if (vis[i])
            continue;

        rep = 1;
        for (i++; i < numbytes; i++)
            if (vis[i] || rep == 255)
                break;
            else
                rep++;
        *dst++ = rep;
        i--;
    }

    return dst - out;
}

/*
===================
DecompressRow
===================
*/
void DecompressRow(const uint8_t *in, const int numbytes, uint8_t *decompressed)
{
    int c;
    uint8_t *out;
    int row;

    row = numbytes;
    out = decompressed;

    do {
        if (*in) {
            *out++ = *in++;
            continue;
        }

        c = in[1];
        if (!c)
            FError("0 repeat");
        in += 2;
        while (c) {
            *out++ = 0;
            c--;
        }
    } while (out - decompressed < row);
}
