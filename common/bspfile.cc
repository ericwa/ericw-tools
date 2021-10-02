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
    {"vertexes", sizeof(qvec3f)},
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
    {"vertexes", sizeof(qvec3f)},
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
    {"vertexes", sizeof(qvec3f)},
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
    {"vertexes", sizeof(qvec3f)},
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
    {"vertexes", sizeof(qvec3f)},
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
    {"vertexes", sizeof(qvec3f)},
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
    {"vertexes", sizeof(qvec3f)},
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
    {"vertexes", sizeof(qvec3f)},
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
template<typename T, typename F, typename = std::enable_if_t<!std::is_same_v<T, F>>>
inline void CopyOrMoveArray(std::vector<F> &from, std::vector<T> &to)
{
    to.reserve(from.size());

    for (auto &v : from)
    {
        if constexpr(std::is_arithmetic_v<T> && std::is_arithmetic_v<F>)
            to.emplace_back(numeric_cast<T>(v));
        else
            to.emplace_back(std::move(v));
    }
}

// move structured data if the input and output
// are of the same type
template<typename T, typename F>
inline void CopyOrMoveArray(F &in, T &out)
{
    out = std::move(in);
}

// convert structured data if we're different types
// with numeric casting for arrays
template<typename T, typename F, size_t N, typename = std::enable_if_t<!std::is_same_v<T, F>>>
inline void CopyOrMoveArray(std::vector<std::array<F, N>> &from, std::vector<std::array<T, N>> &to)
{
    to.reserve(from.size());

    for (auto &v : from)
    {
        if constexpr(std::is_arithmetic_v<T> && std::is_arithmetic_v<F>)
            to.emplace_back(array_cast<std::array<T, N>>(v));
        else
            to.emplace_back(std::move(v));
    }
}

// Convert from a Q1-esque format to Generic
template<typename T>
inline void ConvertQ1BSPToGeneric(T &bsp, mbsp_t &mbsp)
{
    CopyOrMoveArray(bsp.dentdata, mbsp.dentdata);
    CopyOrMoveArray(bsp.dplanes, mbsp.dplanes);
    if (std::holds_alternative<miptexhl_lump>(bsp.dtex)) {
        CopyOrMoveArray(std::get<miptexhl_lump>(bsp.dtex), mbsp.dtex);
    } else {
        CopyOrMoveArray(std::get<miptexq1_lump>(bsp.dtex), mbsp.dtex);
    }
    CopyOrMoveArray(bsp.dvertexes, mbsp.dvertexes);
    CopyOrMoveArray(bsp.dvisdata, mbsp.dvis.bits);
    CopyOrMoveArray(bsp.dnodes, mbsp.dnodes);
    CopyOrMoveArray(bsp.texinfo, mbsp.texinfo);
    CopyOrMoveArray(bsp.dfaces, mbsp.dfaces);
    CopyOrMoveArray(bsp.dlightdata, mbsp.dlightdata);
    CopyOrMoveArray(bsp.dclipnodes, mbsp.dclipnodes);
    CopyOrMoveArray(bsp.dleafs, mbsp.dleafs);
    CopyOrMoveArray(bsp.dmarksurfaces, mbsp.dleaffaces);
    CopyOrMoveArray(bsp.dedges, mbsp.dedges);
    CopyOrMoveArray(bsp.dsurfedges, mbsp.dsurfedges);
    if (std::holds_alternative<dmodelh2_vector>(bsp.dmodels)) {
        CopyOrMoveArray(std::get<dmodelh2_vector>(bsp.dmodels), mbsp.dmodels);
    } else {
        CopyOrMoveArray(std::get<dmodelq1_vector>(bsp.dmodels), mbsp.dmodels);
    }
}

// Convert from a Q2-esque format to Generic
template<typename T>
inline void ConvertQ2BSPToGeneric(T &bsp, mbsp_t &mbsp)
{
    CopyOrMoveArray(bsp.dentdata, mbsp.dentdata);
    CopyOrMoveArray(bsp.dplanes, mbsp.dplanes);
    CopyOrMoveArray(bsp.dvertexes, mbsp.dvertexes);
    CopyOrMoveArray(bsp.dvis, mbsp.dvis);
    CopyOrMoveArray(bsp.dnodes, mbsp.dnodes);
    CopyOrMoveArray(bsp.texinfo, mbsp.texinfo);
    CopyOrMoveArray(bsp.dfaces, mbsp.dfaces);
    CopyOrMoveArray(bsp.dlightdata, mbsp.dlightdata);
    CopyOrMoveArray(bsp.dleafs, mbsp.dleafs);
    CopyOrMoveArray(bsp.dleaffaces, mbsp.dleaffaces);
    CopyOrMoveArray(bsp.dleafbrushes, mbsp.dleafbrushes);
    CopyOrMoveArray(bsp.dedges, mbsp.dedges);
    CopyOrMoveArray(bsp.dsurfedges, mbsp.dsurfedges);
    CopyOrMoveArray(bsp.dmodels, mbsp.dmodels);
    CopyOrMoveArray(bsp.dbrushes, mbsp.dbrushes);
    CopyOrMoveArray(bsp.dbrushsides, mbsp.dbrushsides);
    CopyOrMoveArray(bsp.dareas, mbsp.dareas);
    CopyOrMoveArray(bsp.dareaportals, mbsp.dareaportals);
}

// Convert from a Q1-esque format to Generic
template<typename T>
inline T ConvertGenericToQ1BSP(mbsp_t &mbsp, const bspversion_t *to_version)
{
    T bsp { };

    // copy or convert data
    CopyOrMoveArray(mbsp.dentdata, bsp.dentdata);
    CopyOrMoveArray(mbsp.dplanes, bsp.dplanes);
    if (to_version->game->id == GAME_HALF_LIFE) {
        CopyOrMoveArray(mbsp.dtex, bsp.dtex.emplace<miptexhl_lump>());
    } else {
        CopyOrMoveArray(mbsp.dtex, bsp.dtex.emplace<miptexq1_lump>());
    }
    CopyOrMoveArray(mbsp.dvertexes, bsp.dvertexes);
    CopyOrMoveArray(mbsp.dvis.bits, bsp.dvisdata);
    CopyOrMoveArray(mbsp.dnodes, bsp.dnodes);
    CopyOrMoveArray(mbsp.texinfo, bsp.texinfo);
    CopyOrMoveArray(mbsp.dfaces, bsp.dfaces);
    CopyOrMoveArray(mbsp.dlightdata, bsp.dlightdata);
    CopyOrMoveArray(mbsp.dclipnodes, bsp.dclipnodes);
    CopyOrMoveArray(mbsp.dleafs, bsp.dleafs);
    CopyOrMoveArray(mbsp.dleaffaces, bsp.dmarksurfaces);
    CopyOrMoveArray(mbsp.dedges, bsp.dedges);
    CopyOrMoveArray(mbsp.dsurfedges, bsp.dsurfedges);
    if (to_version->game->id == GAME_HEXEN_II) {
        CopyOrMoveArray(mbsp.dmodels, bsp.dmodels.emplace<dmodelh2_vector>());
    } else {
        CopyOrMoveArray(mbsp.dmodels, bsp.dmodels.emplace<dmodelq1_vector>());
    }

    return bsp;
}

// Convert from a Q2-esque format to Generic
template<typename T>
inline T ConvertGenericToQ2BSP(mbsp_t &mbsp, const bspversion_t *to_version)
{
    T bsp { };

    // copy or convert data
    CopyOrMoveArray(mbsp.dentdata, bsp.dentdata);
    CopyOrMoveArray(mbsp.dplanes, bsp.dplanes);
    CopyOrMoveArray(mbsp.dvertexes, bsp.dvertexes);
    CopyOrMoveArray(mbsp.dvis, bsp.dvis);
    CopyOrMoveArray(mbsp.dnodes, bsp.dnodes);
    CopyOrMoveArray(mbsp.texinfo, bsp.texinfo);
    CopyOrMoveArray(mbsp.dfaces, bsp.dfaces);
    CopyOrMoveArray(mbsp.dlightdata, bsp.dlightdata);
    CopyOrMoveArray(mbsp.dleafs, bsp.dleafs);
    CopyOrMoveArray(mbsp.dleaffaces, bsp.dleaffaces);
    CopyOrMoveArray(mbsp.dleafbrushes, bsp.dleafbrushes);
    CopyOrMoveArray(mbsp.dedges, bsp.dedges);
    CopyOrMoveArray(mbsp.dsurfedges, bsp.dsurfedges);
    CopyOrMoveArray(mbsp.dmodels, bsp.dmodels);
    CopyOrMoveArray(mbsp.dbrushes, bsp.dbrushes);
    CopyOrMoveArray(mbsp.dbrushsides, bsp.dbrushsides);
    CopyOrMoveArray(mbsp.dareas, bsp.dareas);
    CopyOrMoveArray(mbsp.dareaportals, bsp.dareaportals);

    return bsp;
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
    if (bspdata->version == to_version)
        return true;

    if (to_version == &bspver_generic) {
        // Conversions to bspver_generic
        mbsp_t mbsp { };

        if (std::holds_alternative<bsp29_t>(bspdata->bsp)) {
            ConvertQ1BSPToGeneric(std::get<bsp29_t>(bspdata->bsp), mbsp);
        } else if (std::holds_alternative<q2bsp_t>(bspdata->bsp)) {
            ConvertQ2BSPToGeneric(std::get<q2bsp_t>(bspdata->bsp), mbsp);
        } else if (std::holds_alternative<q2bsp_qbism_t>(bspdata->bsp)) {
            ConvertQ2BSPToGeneric(std::get<q2bsp_qbism_t>(bspdata->bsp), mbsp);
        } else if (std::holds_alternative<bsp2rmq_t>(bspdata->bsp)) {
            ConvertQ1BSPToGeneric(std::get<bsp2rmq_t>(bspdata->bsp), mbsp);
        } else if (std::holds_alternative<bsp2_t>(bspdata->bsp)) {
            ConvertQ1BSPToGeneric(std::get<bsp2_t>(bspdata->bsp), mbsp);
        } else {
            return false;
        }

        bspdata->loadversion = mbsp.loadversion = bspdata->version;
        bspdata->version = to_version;

        bspdata->bsp = std::move(mbsp);
        return true;
    } else if (bspdata->version == &bspver_generic) {
        // Conversions from bspver_generic
        mbsp_t &mbsp = std::get<mbsp_t>(bspdata->bsp);
            
        try
        {
            if (to_version == &bspver_q1 || to_version == &bspver_h2 || to_version == &bspver_hl) {
                bspdata->bsp = std::move(ConvertGenericToQ1BSP<bsp29_t>(mbsp, to_version));
            } else if (to_version == &bspver_q2) {
                bspdata->bsp = std::move(ConvertGenericToQ2BSP<q2bsp_t>(mbsp, to_version));
            } else if (to_version == &bspver_qbism) {
                bspdata->bsp = std::move(ConvertGenericToQ2BSP<q2bsp_qbism_t>(mbsp, to_version));
            } else if (to_version == &bspver_bsp2rmq || to_version == &bspver_h2bsp2rmq) {
                bspdata->bsp = std::move(ConvertGenericToQ1BSP<bsp2rmq_t>(mbsp, to_version));
            } else if (to_version == &bspver_bsp2 || to_version == &bspver_h2bsp2) {
                bspdata->bsp = std::move(ConvertGenericToQ1BSP<bsp2_t>(mbsp, to_version));
            } else {
                return false;
            }
        }
        catch (std::overflow_error e)
        {
            LogPrint("LIMITS EXCEEDED ON {}\n", e.what());
        }

        bspdata->version = to_version;
        return true;
    }

    return false;
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

struct lump_reader
{
    std::istream &s;
    const bspversion_t *version;
    const std::vector<lump_t> &lumps;

    // read structured lump data from stream into vector
    template<typename T>
    void read(size_t lump_num, std::vector<T> &buffer)
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
            for (size_t i = 0; i < length; i++) {
                T &val = buffer.emplace_back();
                s >= val;
            }
        } else {
            s.read(reinterpret_cast<char *>(buffer.data()), length);
        }
    }

    // read string from stream
    void read(size_t lump_num, std::string &buffer)
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
    void read(size_t lump_num, T &buffer)
    {
        const lumpspec_t &lumpspec = version->lumps[lump_num];
        const lump_t &lump = lumps[lump_num];

        if (!lump.filelen)
            return;

        Q_assert(lumpspec.size == 1);

        s.seekg(lump.fileofs);

        buffer.stream_read(s, lump);
    }
};

template<typename T>
inline void ReadQ1BSP(lump_reader &reader, T &bsp)
{
    reader.read(LUMP_ENTITIES, bsp.dentdata);
    reader.read(LUMP_PLANES, bsp.dplanes);
    if (reader.version->game->id == GAME_HALF_LIFE) {
        reader.read(LUMP_TEXTURES, bsp.dtex.emplace<miptexhl_lump>());
    } else {
        reader.read(LUMP_TEXTURES, bsp.dtex.emplace<miptexq1_lump>());
    }
    reader.read(LUMP_VERTEXES, bsp.dvertexes);
    reader.read(LUMP_VISIBILITY, bsp.dvisdata);
    reader.read(LUMP_NODES, bsp.dnodes);
    reader.read(LUMP_TEXINFO, bsp.texinfo);
    reader.read(LUMP_FACES, bsp.dfaces);
    reader.read(LUMP_LIGHTING, bsp.dlightdata);
    reader.read(LUMP_CLIPNODES, bsp.dclipnodes);
    reader.read(LUMP_LEAFS, bsp.dleafs);
    reader.read(LUMP_MARKSURFACES, bsp.dmarksurfaces);
    reader.read(LUMP_EDGES, bsp.dedges);
    reader.read(LUMP_SURFEDGES, bsp.dsurfedges);
    if (reader.version->game->id == GAME_HEXEN_II) {
        reader.read(LUMP_MODELS, bsp.dmodels.emplace<dmodelh2_vector>());
    } else {
        reader.read(LUMP_MODELS, bsp.dmodels.emplace<dmodelq1_vector>());
    }
}

template<typename T>
inline void ReadQ2BSP(lump_reader &reader, T &bsp)
{
    reader.read(Q2_LUMP_ENTITIES, bsp.dentdata);
    reader.read(Q2_LUMP_PLANES, bsp.dplanes);
    reader.read(Q2_LUMP_VERTEXES, bsp.dvertexes);
    reader.read(Q2_LUMP_VISIBILITY, bsp.dvis);
    reader.read(Q2_LUMP_NODES, bsp.dnodes);
    reader.read(Q2_LUMP_TEXINFO, bsp.texinfo);
    reader.read(Q2_LUMP_FACES, bsp.dfaces);
    reader.read(Q2_LUMP_LIGHTING, bsp.dlightdata);
    reader.read(Q2_LUMP_LEAFS, bsp.dleafs);
    reader.read(Q2_LUMP_LEAFFACES, bsp.dleaffaces);
    reader.read(Q2_LUMP_LEAFBRUSHES, bsp.dleafbrushes);
    reader.read(Q2_LUMP_EDGES, bsp.dedges);
    reader.read(Q2_LUMP_SURFEDGES, bsp.dsurfedges);
    reader.read(Q2_LUMP_MODELS, bsp.dmodels);
    reader.read(Q2_LUMP_BRUSHES, bsp.dbrushes);
    reader.read(Q2_LUMP_BRUSHSIDES, bsp.dbrushsides);
    reader.read(Q2_LUMP_AREAS, bsp.dareas);
    reader.read(Q2_LUMP_AREAPORTALS, bsp.dareaportals);
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

    memstream stream(file_data, flen);

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

    lump_reader reader { stream, bspdata->version, lumps };

    /* copy the data */
    if (bspdata->version == &bspver_q2) {
        ReadQ2BSP(reader, bspdata->bsp.emplace<q2bsp_t>());
    } else if (bspdata->version == &bspver_qbism) {
        ReadQ2BSP(reader, bspdata->bsp.emplace<q2bsp_qbism_t>());
    } else if (bspdata->version == &bspver_q1 || bspdata->version == &bspver_h2 || bspdata->version == &bspver_hl) {
        ReadQ1BSP(reader, bspdata->bsp.emplace<bsp29_t>());
    } else if (bspdata->version == &bspver_bsp2rmq || bspdata->version == &bspver_h2bsp2rmq) {
        ReadQ1BSP(reader, bspdata->bsp.emplace<bsp2rmq_t>());
    } else if (bspdata->version == &bspver_bsp2 || bspdata->version == &bspver_h2bsp2) {
        ReadQ1BSP(reader, bspdata->bsp.emplace<bsp2_t>());
    } else {
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

template<typename T>
inline void WriteQ1BSP(bspfile_t &bspfile, const T &bsp)
{
    WriteLump(bspfile, LUMP_ENTITIES, bsp.dentdata);
    WriteLump(bspfile, LUMP_PLANES, bsp.dplanes);
    if (std::holds_alternative<miptexhl_lump>(bsp.dtex))
        WriteLump(bspfile, LUMP_TEXTURES, std::get<miptexhl_lump>(bsp.dtex));
    else
        WriteLump(bspfile, LUMP_TEXTURES, std::get<miptexq1_lump>(bsp.dtex));
    WriteLump(bspfile, LUMP_VERTEXES, bsp.dvertexes);
    WriteLump(bspfile, LUMP_VISIBILITY, bsp.dvisdata);
    WriteLump(bspfile, LUMP_NODES, bsp.dnodes);
    WriteLump(bspfile, LUMP_TEXINFO, bsp.texinfo);
    WriteLump(bspfile, LUMP_FACES, bsp.dfaces);
    WriteLump(bspfile, LUMP_LIGHTING, bsp.dlightdata);
    WriteLump(bspfile, LUMP_CLIPNODES, bsp.dclipnodes);
    WriteLump(bspfile, LUMP_LEAFS, bsp.dleafs);
    WriteLump(bspfile, LUMP_MARKSURFACES, bsp.dmarksurfaces);
    WriteLump(bspfile, LUMP_EDGES, bsp.dedges);
    WriteLump(bspfile, LUMP_SURFEDGES, bsp.dsurfedges);
    if (std::holds_alternative<dmodelh2_vector>(bsp.dmodels))
        WriteLump(bspfile, LUMP_MODELS, std::get<dmodelh2_vector>(bsp.dmodels));
    else
        WriteLump(bspfile, LUMP_MODELS, std::get<dmodelq1_vector>(bsp.dmodels));
}

template<typename T>
inline void WriteQ2BSP(bspfile_t &bspfile, const T &bsp)
{
    WriteLump(bspfile, Q2_LUMP_ENTITIES, bsp.dentdata);
    WriteLump(bspfile, Q2_LUMP_PLANES, bsp.dplanes);
    WriteLump(bspfile, Q2_LUMP_VERTEXES, bsp.dvertexes);
    WriteLump(bspfile, Q2_LUMP_VISIBILITY, bsp.dvis);
    WriteLump(bspfile, Q2_LUMP_NODES, bsp.dnodes);
    WriteLump(bspfile, Q2_LUMP_TEXINFO, bsp.texinfo);
    WriteLump(bspfile, Q2_LUMP_FACES, bsp.dfaces);
    WriteLump(bspfile, Q2_LUMP_LIGHTING, bsp.dlightdata);
    WriteLump(bspfile, Q2_LUMP_LEAFS, bsp.dleafs);
    WriteLump(bspfile, Q2_LUMP_LEAFFACES, bsp.dleaffaces);
    WriteLump(bspfile, Q2_LUMP_LEAFBRUSHES, bsp.dleafbrushes);
    WriteLump(bspfile, Q2_LUMP_EDGES, bsp.dedges);
    WriteLump(bspfile, Q2_LUMP_SURFEDGES, bsp.dsurfedges);
    WriteLump(bspfile, Q2_LUMP_MODELS, bsp.dmodels);
    WriteLump(bspfile, Q2_LUMP_BRUSHES, bsp.dbrushes);
    WriteLump(bspfile, Q2_LUMP_BRUSHSIDES, bsp.dbrushsides);
    WriteLump(bspfile, Q2_LUMP_AREAS, bsp.dareas);
    WriteLump(bspfile, Q2_LUMP_AREAPORTALS, bsp.dareaportals);
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

    if (std::holds_alternative<bsp29_t>(bspdata->bsp)) {
        WriteQ1BSP(bspfile, std::get<bsp29_t>(bspdata->bsp));
    } else if (std::holds_alternative<bsp2rmq_t>(bspdata->bsp)) {
        WriteQ1BSP(bspfile, std::get<bsp2rmq_t>(bspdata->bsp));
    } else if (std::holds_alternative<bsp2_t>(bspdata->bsp)) {
        WriteQ1BSP(bspfile, std::get<bsp2_t>(bspdata->bsp));
    } else if (std::holds_alternative<q2bsp_t>(bspdata->bsp)) {
        WriteQ2BSP(bspfile, std::get<q2bsp_t>(bspdata->bsp));
    } else if (std::holds_alternative<q2bsp_qbism_t>(bspdata->bsp)) {
        WriteQ2BSP(bspfile, std::get<q2bsp_qbism_t>(bspdata->bsp));
    } else {
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
    LogPrint("{:7} {:<12} {:10}\n", count, lump->name, count * lump->size);
}

template<typename T>
inline void PrintQ1BSPLumps(const lumpspec_t *lumpspec, const T &bsp)
{
    if (std::holds_alternative<dmodelh2_vector>(bsp.dmodels))
        LogPrint("{:7} {:<12}\n", std::get<dmodelh2_vector>(bsp.dmodels).size(), "models");
    else
        LogPrint("{:7} {:<12}\n", std::get<dmodelq1_vector>(bsp.dmodels).size(), "models");

    PrintLumpSize(lumpspec, LUMP_PLANES, bsp.dplanes.size());
    PrintLumpSize(lumpspec, LUMP_VERTEXES, bsp.dvertexes.size());
    PrintLumpSize(lumpspec, LUMP_NODES, bsp.dnodes.size());
    PrintLumpSize(lumpspec, LUMP_TEXINFO, bsp.texinfo.size());
    PrintLumpSize(lumpspec, LUMP_FACES, bsp.dfaces.size());
    PrintLumpSize(lumpspec, LUMP_CLIPNODES, bsp.dclipnodes.size());
    PrintLumpSize(lumpspec, LUMP_LEAFS, bsp.dleafs.size());
    PrintLumpSize(lumpspec, LUMP_MARKSURFACES, bsp.dmarksurfaces.size());
    PrintLumpSize(lumpspec, LUMP_EDGES, bsp.dedges.size());
    PrintLumpSize(lumpspec, LUMP_SURFEDGES, bsp.dsurfedges.size());
    
    if (std::holds_alternative<miptexhl_lump>(bsp.dtex))
        LogPrint("{:7} {:<12} {:10}\n", "", "textures", std::get<miptexhl_lump>(bsp.dtex).textures.size());
    else
        LogPrint("{:7} {:<12} {:10}\n", "", "textures", std::get<miptexq1_lump>(bsp.dtex).textures.size());
    LogPrint("{:7} {:<12} {:10}\n", "", "lightdata", bsp.dlightdata.size());
    LogPrint("{:7} {:<12} {:10}\n", "", "visdata", bsp.dvisdata.size());
    LogPrint("{:7} {:<12} {:10}\n", "", "entdata", bsp.dentdata.size());
}

template<typename T>
inline void PrintQ2BSPLumps(const lumpspec_t *lumpspec, const T &bsp)
{
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
}

/*
 * =============
 * PrintBSPFileSizes
 * Dumps info about the bsp data
 * =============
 */
void PrintBSPFileSizes(const bspdata_t *bspdata)
{
    const lumpspec_t *lumpspec = bspdata->version->lumps;

    if (std::holds_alternative<q2bsp_t>(bspdata->bsp)) {
        PrintQ2BSPLumps(lumpspec, std::get<q2bsp_t>(bspdata->bsp));
    } else if (std::holds_alternative<q2bsp_qbism_t>(bspdata->bsp)) {
        PrintQ2BSPLumps(lumpspec, std::get<q2bsp_qbism_t>(bspdata->bsp));
    } else if (std::holds_alternative<bsp29_t>(bspdata->bsp)) {
        PrintQ1BSPLumps(lumpspec, std::get<bsp29_t>(bspdata->bsp));
    } else if (std::holds_alternative<bsp2rmq_t>(bspdata->bsp)) {
        PrintQ1BSPLumps(lumpspec, std::get<bsp2rmq_t>(bspdata->bsp));
    } else if (std::holds_alternative<bsp2_t>(bspdata->bsp)) {
        PrintQ1BSPLumps(lumpspec, std::get<bsp2_t>(bspdata->bsp));
    } else {
        Error("Unsupported BSP version: {}", BSPVersionString(bspdata->version));
    }

    if (bspdata->bspxentries) {
        bspxentry_t *x;
        for (x = bspdata->bspxentries; x; x = x->next) {
            LogPrint("{:7} {:<12} {:10}\n", "BSPX", x->lumpname.data(), x->lumpsize);
        }
    }
}
