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
        } else
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
const bspversion_t bspver_generic{NO_VERSION, NO_VERSION, "mbsp", "generic BSP", &gamedef_generic};
static const gamedef_q1_like_t<GAME_QUAKE> gamedef_q1;
const bspversion_t bspver_q1{BSPVERSION, NO_VERSION, "bsp29", "Quake BSP", &gamedef_q1, &bspver_bsp2};
const bspversion_t bspver_bsp2{BSP2VERSION, NO_VERSION, "bsp2", "Quake BSP2", &gamedef_q1};
const bspversion_t bspver_bsp2rmq{BSP2RMQVERSION, NO_VERSION, "bsp2rmq", "Quake BSP2-RMQ", &gamedef_q1};
/* Hexen II doesn't use a separate version, but we can still use a separate tag/name for it */
static const gamedef_h2_t gamedef_h2;
const bspversion_t bspver_h2{BSPVERSION, NO_VERSION, "hexen2", "Hexen II BSP", &gamedef_h2, &bspver_h2bsp2};
const bspversion_t bspver_h2bsp2{BSP2VERSION, NO_VERSION, "hexen2bsp2", "Hexen II BSP2", &gamedef_h2};
const bspversion_t bspver_h2bsp2rmq{BSP2RMQVERSION, NO_VERSION, "hexen2bsp2rmq", "Hexen II BSP2-RMQ", &gamedef_h2};
static const gamedef_hl_t gamedef_hl;
const bspversion_t bspver_hl{BSPHLVERSION, NO_VERSION, "hl", "Half-Life BSP", &gamedef_hl};
static const gamedef_q2_t gamedef_q2;
const bspversion_t bspver_q2{Q2_BSPIDENT, Q2_BSPVERSION, "q2bsp", "Quake II BSP", &gamedef_q2, &bspver_qbism};
const bspversion_t bspver_qbism{Q2_QBISMIDENT, Q2_BSPVERSION, "qbism", "Quake II Qbism BSP", &gamedef_q2};

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
 * BSP BYTE SWAPPING
 * =========================================================================
 */

enum swaptype_t : bool
{
    TO_DISK,
    TO_CPU
};

static void SwapBSPVertexes(int numvertexes, dvertex_t *verticies)
{
    dvertex_t *vertex = verticies;
    int i, j;

    for (i = 0; i < numvertexes; i++, vertex++)
        for (j = 0; j < 3; j++)
            vertex->at(j) = LittleFloat(vertex->at(j));
}

static void SwapBSPPlanes(int numplanes, dplane_t *planes)
{
    dplane_t *plane = planes;
    int i, j;

    for (i = 0; i < numplanes; i++, plane++) {
        for (j = 0; j < 3; j++)
            plane->normal[j] = LittleFloat(plane->normal[j]);
        plane->dist = LittleFloat(plane->dist);
        plane->type = LittleLong(plane->type);
    }
}

static void SwapBSPTexinfo(int numtexinfo, texinfo_t *texinfos)
{
    texinfo_t *texinfo = texinfos;
    int i, j;

    for (i = 0; i < numtexinfo; i++, texinfo++) {
        for (j = 0; j < 4; j++) {
            texinfo->vecs[0][j] = LittleFloat(texinfo->vecs[0][j]);
            texinfo->vecs[1][j] = LittleFloat(texinfo->vecs[1][j]);
        }
        texinfo->miptex = LittleLong(texinfo->miptex);
        texinfo->flags = LittleLong(texinfo->flags);
    }
}

static void SwapBSP29Faces(int numfaces, bsp29_dface_t *faces)
{
    bsp29_dface_t *face = faces;
    int i;

    for (i = 0; i < numfaces; i++, face++) {
        face->texinfo = LittleShort(face->texinfo);
        face->planenum = LittleShort(face->planenum);
        face->side = LittleShort(face->side);
        face->lightofs = LittleLong(face->lightofs);
        face->firstedge = LittleLong(face->firstedge);
        face->numedges = LittleShort(face->numedges);
    }
}

static void SwapBSP2Faces(int numfaces, bsp2_dface_t *faces)
{
    bsp2_dface_t *face = faces;
    int i;

    for (i = 0; i < numfaces; i++, face++) {
        face->texinfo = LittleLong(face->texinfo);
        face->planenum = LittleLong(face->planenum);
        face->side = LittleLong(face->side);
        face->lightofs = LittleLong(face->lightofs);
        face->firstedge = LittleLong(face->firstedge);
        face->numedges = LittleLong(face->numedges);
    }
}

static void SwapBSP29Nodes(int numnodes, bsp29_dnode_t *nodes)
{
    bsp29_dnode_t *node = nodes;
    int i, j;

    /* nodes */
    for (i = 0; i < numnodes; i++, node++) {
        node->planenum = LittleLong(node->planenum);
        for (j = 0; j < 3; j++) {
            node->mins[j] = LittleShort(node->mins[j]);
            node->maxs[j] = LittleShort(node->maxs[j]);
        }
        node->children[0] = LittleShort(node->children[0]);
        node->children[1] = LittleShort(node->children[1]);
        node->firstface = LittleShort(node->firstface);
        node->numfaces = LittleShort(node->numfaces);
    }
}

static void SwapBSP2rmqNodes(int numnodes, bsp2rmq_dnode_t *nodes)
{
    bsp2rmq_dnode_t *node = nodes;
    int i, j;

    /* nodes */
    for (i = 0; i < numnodes; i++, node++) {
        node->planenum = LittleLong(node->planenum);
        for (j = 0; j < 3; j++) {
            node->mins[j] = LittleShort(node->mins[j]);
            node->maxs[j] = LittleShort(node->maxs[j]);
        }
        node->children[0] = LittleLong(node->children[0]);
        node->children[1] = LittleLong(node->children[1]);
        node->firstface = LittleLong(node->firstface);
        node->numfaces = LittleLong(node->numfaces);
    }
}

static void SwapBSP2Nodes(int numnodes, bsp2_dnode_t *nodes)
{
    bsp2_dnode_t *node = nodes;
    int i, j;

    /* nodes */
    for (i = 0; i < numnodes; i++, node++) {
        node->planenum = LittleLong(node->planenum);
        for (j = 0; j < 3; j++) {
            node->mins[j] = LittleFloat(node->mins[j]);
            node->maxs[j] = LittleFloat(node->maxs[j]);
        }
        node->children[0] = LittleLong(node->children[0]);
        node->children[1] = LittleLong(node->children[1]);
        node->firstface = LittleLong(node->firstface);
        node->numfaces = LittleLong(node->numfaces);
    }
}

static void SwapBSP29Leafs(int numleafs, bsp29_dleaf_t *leafs)
{
    bsp29_dleaf_t *leaf = leafs;
    int i, j;

    for (i = 0; i < numleafs; i++, leaf++) {
        leaf->contents = LittleLong(leaf->contents);
        for (j = 0; j < 3; j++) {
            leaf->mins[j] = LittleShort(leaf->mins[j]);
            leaf->maxs[j] = LittleShort(leaf->maxs[j]);
        }
        leaf->firstmarksurface = LittleShort(leaf->firstmarksurface);
        leaf->nummarksurfaces = LittleShort(leaf->nummarksurfaces);
        leaf->visofs = LittleLong(leaf->visofs);
    }
}

static void SwapBSP2rmqLeafs(int numleafs, bsp2rmq_dleaf_t *leafs)
{
    bsp2rmq_dleaf_t *leaf = leafs;
    int i, j;

    for (i = 0; i < numleafs; i++, leaf++) {
        leaf->contents = LittleLong(leaf->contents);
        for (j = 0; j < 3; j++) {
            leaf->mins[j] = LittleShort(leaf->mins[j]);
            leaf->maxs[j] = LittleShort(leaf->maxs[j]);
        }
        leaf->firstmarksurface = LittleLong(leaf->firstmarksurface);
        leaf->nummarksurfaces = LittleLong(leaf->nummarksurfaces);
        leaf->visofs = LittleLong(leaf->visofs);
    }
}

static void SwapBSP2Leafs(int numleafs, bsp2_dleaf_t *leafs)
{
    bsp2_dleaf_t *leaf = leafs;
    int i, j;

    for (i = 0; i < numleafs; i++, leaf++) {
        leaf->contents = LittleLong(leaf->contents);
        for (j = 0; j < 3; j++) {
            leaf->mins[j] = LittleFloat(leaf->mins[j]);
            leaf->maxs[j] = LittleFloat(leaf->maxs[j]);
        }
        leaf->firstmarksurface = LittleLong(leaf->firstmarksurface);
        leaf->nummarksurfaces = LittleLong(leaf->nummarksurfaces);
        leaf->visofs = LittleLong(leaf->visofs);
    }
}

static void SwapBSP29Clipnodes(int numclipnodes, bsp29_dclipnode_t *clipnodes)
{
    bsp29_dclipnode_t *clipnode = clipnodes;
    int i;

    for (i = 0; i < numclipnodes; i++, clipnode++) {
        clipnode->planenum = LittleLong(clipnode->planenum);
        clipnode->children[0] = LittleShort(clipnode->children[0]);
        clipnode->children[1] = LittleShort(clipnode->children[1]);
    }
}

static void SwapBSP2Clipnodes(int numclipnodes, bsp2_dclipnode_t *clipnodes)
{
    bsp2_dclipnode_t *clipnode = clipnodes;
    int i;

    for (i = 0; i < numclipnodes; i++, clipnode++) {
        clipnode->planenum = LittleLong(clipnode->planenum);
        clipnode->children[0] = LittleLong(clipnode->children[0]);
        clipnode->children[1] = LittleLong(clipnode->children[1]);
    }
}

static void SwapBSP29Marksurfaces(int nummarksurfaces, uint16_t *dmarksurfaces)
{
    uint16_t *marksurface = dmarksurfaces;
    int i;

    for (i = 0; i < nummarksurfaces; i++, marksurface++)
        *marksurface = LittleShort(*marksurface);
}

static void SwapBSP2Marksurfaces(int nummarksurfaces, uint32_t *dmarksurfaces)
{
    uint32_t *marksurface = dmarksurfaces;
    int i;

    for (i = 0; i < nummarksurfaces; i++, marksurface++)
        *marksurface = LittleLong(*marksurface);
}

static void SwapBSPSurfedges(int numsurfedges, int32_t *dsurfedges)
{
    int32_t *surfedge = dsurfedges;
    int i;

    for (i = 0; i < numsurfedges; i++, surfedge++)
        *surfedge = LittleLong(*surfedge);
}

static void SwapBSP29Edges(int numedges, bsp29_dedge_t *dedges)
{
    bsp29_dedge_t *edge = dedges;
    int i;

    for (i = 0; i < numedges; i++, edge++) {
        (*edge)[0] = LittleShort((*edge)[0]);
        (*edge)[1] = LittleShort((*edge)[1]);
    }
}

static void SwapBSP2Edges(int numedges, bsp2_dedge_t *dedges)
{
    bsp2_dedge_t *edge = dedges;
    int i;

    for (i = 0; i < numedges; i++, edge++) {
        (*edge)[0] = LittleLong((*edge)[0]);
        (*edge)[1] = LittleLong((*edge)[1]);
    }
}

static void SwapBSPModels(int nummodels, dmodelh2_t *dmodels)
{
    dmodelh2_t *dmodel = dmodels;
    int i, j;

    for (i = 0; i < nummodels; i++, dmodel++) {
        for (j = 0; j < MAX_MAP_HULLS_H2; j++)
            dmodel->headnode[j] = LittleLong(dmodel->headnode[j]);
        dmodel->visleafs = LittleLong(dmodel->visleafs);
        dmodel->firstface = LittleLong(dmodel->firstface);
        dmodel->numfaces = LittleLong(dmodel->numfaces);
        for (j = 0; j < 3; j++) {
            dmodel->mins[j] = LittleFloat(dmodel->mins[j]);
            dmodel->maxs[j] = LittleFloat(dmodel->maxs[j]);
            dmodel->origin[j] = LittleFloat(dmodel->origin[j]);
        }
    }
}

static void SwapBSPModels(int nummodels, dmodelq1_t *dmodels)
{
    dmodelq1_t *dmodel = dmodels;
    int i, j;

    for (i = 0; i < nummodels; i++, dmodel++) {
        for (j = 0; j < MAX_MAP_HULLS_Q1; j++)
            dmodel->headnode[j] = LittleLong(dmodel->headnode[j]);
        dmodel->visleafs = LittleLong(dmodel->visleafs);
        dmodel->firstface = LittleLong(dmodel->firstface);
        dmodel->numfaces = LittleLong(dmodel->numfaces);
        for (j = 0; j < 3; j++) {
            dmodel->mins[j] = LittleFloat(dmodel->mins[j]);
            dmodel->maxs[j] = LittleFloat(dmodel->maxs[j]);
            dmodel->origin[j] = LittleFloat(dmodel->origin[j]);
        }
    }
}

static void SwapBSPMiptex(int texdatasize, dmiptexlump_t *header, const swaptype_t swap)
{
    int i, count;

    if (!texdatasize)
        return;

    count = header->nummiptex;
    if (swap == TO_CPU)
        count = LittleLong(count);

    header->nummiptex = LittleLong(header->nummiptex);
    for (i = 0; i < count; i++)
        header->dataofs[i] = LittleLong(header->dataofs[i]);
}

/*
=============
Q2_SwapBSPFile

Byte swaps all data in a bsp file.
=============
*/
void Q2_SwapBSPFile(q2bsp_t &bsp, bool todisk)
{
    int i, j;
    q2_dmodel_t *d;

    // models
    for (i = 0; i < bsp.nummodels; i++) {
        d = &bsp.dmodels[i];

        d->firstface = LittleLong(d->firstface);
        d->numfaces = LittleLong(d->numfaces);
        d->headnode = LittleLong(d->headnode);

        for (j = 0; j < 3; j++) {
            d->mins[j] = LittleFloat(d->mins[j]);
            d->maxs[j] = LittleFloat(d->maxs[j]);
            d->origin[j] = LittleFloat(d->origin[j]);
        }
    }

    //
    // vertexes
    //
    for (i = 0; i < bsp.numvertexes; i++) {
        for (j = 0; j < 3; j++)
            bsp.dvertexes[i][j] = LittleFloat(bsp.dvertexes[i][j]);
    }

    //
    // planes
    //
    for (i = 0; i < bsp.numplanes; i++) {
        for (j = 0; j < 3; j++)
            bsp.dplanes[i].normal[j] = LittleFloat(bsp.dplanes[i].normal[j]);
        bsp.dplanes[i].dist = LittleFloat(bsp.dplanes[i].dist);
        bsp.dplanes[i].type = LittleLong(bsp.dplanes[i].type);
    }

    //
    // texinfos
    //
    for (i = 0; i < bsp.numtexinfo; i++) {
        for (j = 0; j < 4; j++) {
            bsp.texinfo[i].vecs[0][j] = LittleFloat(bsp.texinfo[i].vecs[0][j]);
            bsp.texinfo[i].vecs[1][j] = LittleFloat(bsp.texinfo[i].vecs[1][j]);
        }
        bsp.texinfo[i].flags = LittleLong(bsp.texinfo[i].flags);
        bsp.texinfo[i].value = LittleLong(bsp.texinfo[i].value);
        bsp.texinfo[i].nexttexinfo = LittleLong(bsp.texinfo[i].nexttexinfo);
    }

    //
    // faces
    //
    for (i = 0; i < bsp.numfaces; i++) {
        bsp.dfaces[i].texinfo = LittleShort(bsp.dfaces[i].texinfo);
        bsp.dfaces[i].planenum = LittleShort(bsp.dfaces[i].planenum);
        bsp.dfaces[i].side = LittleShort(bsp.dfaces[i].side);
        bsp.dfaces[i].lightofs = LittleLong(bsp.dfaces[i].lightofs);
        bsp.dfaces[i].firstedge = LittleLong(bsp.dfaces[i].firstedge);
        bsp.dfaces[i].numedges = LittleShort(bsp.dfaces[i].numedges);
    }

    //
    // nodes
    //
    for (i = 0; i < bsp.numnodes; i++) {
        bsp.dnodes[i].planenum = LittleLong(bsp.dnodes[i].planenum);
        for (j = 0; j < 3; j++) {
            bsp.dnodes[i].mins[j] = LittleShort(bsp.dnodes[i].mins[j]);
            bsp.dnodes[i].maxs[j] = LittleShort(bsp.dnodes[i].maxs[j]);
        }
        bsp.dnodes[i].children[0] = LittleLong(bsp.dnodes[i].children[0]);
        bsp.dnodes[i].children[1] = LittleLong(bsp.dnodes[i].children[1]);
        bsp.dnodes[i].firstface = LittleShort(bsp.dnodes[i].firstface);
        bsp.dnodes[i].numfaces = LittleShort(bsp.dnodes[i].numfaces);
    }

    //
    // leafs
    //
    for (i = 0; i < bsp.numleafs; i++) {
        bsp.dleafs[i].contents = LittleLong(bsp.dleafs[i].contents);
        bsp.dleafs[i].cluster = LittleShort(bsp.dleafs[i].cluster);
        bsp.dleafs[i].area = LittleShort(bsp.dleafs[i].area);
        for (j = 0; j < 3; j++) {
            bsp.dleafs[i].mins[j] = LittleShort(bsp.dleafs[i].mins[j]);
            bsp.dleafs[i].maxs[j] = LittleShort(bsp.dleafs[i].maxs[j]);
        }

        bsp.dleafs[i].firstleafface = LittleShort(bsp.dleafs[i].firstleafface);
        bsp.dleafs[i].numleaffaces = LittleShort(bsp.dleafs[i].numleaffaces);
        bsp.dleafs[i].firstleafbrush = LittleShort(bsp.dleafs[i].firstleafbrush);
        bsp.dleafs[i].numleafbrushes = LittleShort(bsp.dleafs[i].numleafbrushes);
    }

    //
    // leaffaces
    //
    for (i = 0; i < bsp.numleaffaces; i++)
        bsp.dleaffaces[i] = LittleShort(bsp.dleaffaces[i]);

    //
    // leafbrushes
    //
    for (i = 0; i < bsp.numleafbrushes; i++)
        bsp.dleafbrushes[i] = LittleShort(bsp.dleafbrushes[i]);

    //
    // surfedges
    //
    for (i = 0; i < bsp.numsurfedges; i++)
        bsp.dsurfedges[i] = LittleLong(bsp.dsurfedges[i]);

    //
    // edges
    //
    for (i = 0; i < bsp.numedges; i++) {
        bsp.dedges[i][0] = LittleShort(bsp.dedges[i][0]);
        bsp.dedges[i][1] = LittleShort(bsp.dedges[i][1]);
    }

    //
    // brushes
    //
    for (i = 0; i < bsp.numbrushes; i++) {
        bsp.dbrushes[i].firstside = LittleLong(bsp.dbrushes[i].firstside);
        bsp.dbrushes[i].numsides = LittleLong(bsp.dbrushes[i].numsides);
        bsp.dbrushes[i].contents = LittleLong(bsp.dbrushes[i].contents);
    }

    //
    // areas
    //
    for (i = 0; i < bsp.numareas; i++) {
        bsp.dareas[i].numareaportals = LittleLong(bsp.dareas[i].numareaportals);
        bsp.dareas[i].firstareaportal = LittleLong(bsp.dareas[i].firstareaportal);
    }

    //
    // areasportals
    //
    for (i = 0; i < bsp.numareaportals; i++) {
        bsp.dareaportals[i].portalnum = LittleLong(bsp.dareaportals[i].portalnum);
        bsp.dareaportals[i].otherarea = LittleLong(bsp.dareaportals[i].otherarea);
    }

    //
    // brushsides
    //
    for (i = 0; i < bsp.numbrushsides; i++) {
        bsp.dbrushsides[i].planenum = LittleShort(bsp.dbrushsides[i].planenum);
        bsp.dbrushsides[i].texinfo = LittleShort(bsp.dbrushsides[i].texinfo);
    }

    //
    // visibility
    //
    if (bsp.dvis) {
        if (todisk)
            j = bsp.dvis->numclusters;
        else
            j = LittleLong(bsp.dvis->numclusters);
        bsp.dvis->numclusters = LittleLong(bsp.dvis->numclusters);
        for (i = 0; i < j; i++) {
            bsp.dvis->bitofs[i][0] = LittleLong(bsp.dvis->bitofs[i][0]);
            bsp.dvis->bitofs[i][1] = LittleLong(bsp.dvis->bitofs[i][1]);
        }
    }
}

/*
=============
Q2_Qbism_SwapBSPFile

Byte swaps all data in a bsp file.
=============
*/
void Q2_Qbism_SwapBSPFile(q2bsp_qbism_t &bsp, bool todisk)
{
    int i, j;
    q2_dmodel_t *d;

    // models
    for (i = 0; i < bsp.nummodels; i++) {
        d = &bsp.dmodels[i];

        d->firstface = LittleLong(d->firstface);
        d->numfaces = LittleLong(d->numfaces);
        d->headnode = LittleLong(d->headnode);

        for (j = 0; j < 3; j++) {
            d->mins[j] = LittleFloat(d->mins[j]);
            d->maxs[j] = LittleFloat(d->maxs[j]);
            d->origin[j] = LittleFloat(d->origin[j]);
        }
    }

    //
    // vertexes
    //
    for (i = 0; i < bsp.numvertexes; i++) {
        for (j = 0; j < 3; j++)
            bsp.dvertexes[i][j] = LittleFloat(bsp.dvertexes[i][j]);
    }

    //
    // planes
    //
    for (i = 0; i < bsp.numplanes; i++) {
        for (j = 0; j < 3; j++)
            bsp.dplanes[i].normal[j] = LittleFloat(bsp.dplanes[i].normal[j]);
        bsp.dplanes[i].dist = LittleFloat(bsp.dplanes[i].dist);
        bsp.dplanes[i].type = LittleLong(bsp.dplanes[i].type);
    }

    //
    // texinfos
    //
    for (i = 0; i < bsp.numtexinfo; i++) {
        for (j = 0; j < 4; j++) {
            bsp.texinfo[i].vecs[0][j] = LittleFloat(bsp.texinfo[i].vecs[0][j]);
            bsp.texinfo[i].vecs[1][j] = LittleFloat(bsp.texinfo[i].vecs[1][j]);
        }
        bsp.texinfo[i].flags = LittleLong(bsp.texinfo[i].flags);
        bsp.texinfo[i].value = LittleLong(bsp.texinfo[i].value);
        bsp.texinfo[i].nexttexinfo = LittleLong(bsp.texinfo[i].nexttexinfo);
    }

    //
    // faces
    //
    for (i = 0; i < bsp.numfaces; i++) {
        bsp.dfaces[i].texinfo = LittleLong(bsp.dfaces[i].texinfo);
        bsp.dfaces[i].planenum = LittleLong(bsp.dfaces[i].planenum);
        bsp.dfaces[i].side = LittleLong(bsp.dfaces[i].side);
        bsp.dfaces[i].lightofs = LittleLong(bsp.dfaces[i].lightofs);
        bsp.dfaces[i].firstedge = LittleLong(bsp.dfaces[i].firstedge);
        bsp.dfaces[i].numedges = LittleLong(bsp.dfaces[i].numedges);
    }

    //
    // nodes
    //
    for (i = 0; i < bsp.numnodes; i++) {
        bsp.dnodes[i].planenum = LittleLong(bsp.dnodes[i].planenum);
        for (j = 0; j < 3; j++) {
            bsp.dnodes[i].mins[j] = LittleFloat(bsp.dnodes[i].mins[j]);
            bsp.dnodes[i].maxs[j] = LittleFloat(bsp.dnodes[i].maxs[j]);
        }
        bsp.dnodes[i].children[0] = LittleLong(bsp.dnodes[i].children[0]);
        bsp.dnodes[i].children[1] = LittleLong(bsp.dnodes[i].children[1]);
        bsp.dnodes[i].firstface = LittleLong(bsp.dnodes[i].firstface);
        bsp.dnodes[i].numfaces = LittleLong(bsp.dnodes[i].numfaces);
    }

    //
    // leafs
    //
    for (i = 0; i < bsp.numleafs; i++) {
        bsp.dleafs[i].contents = LittleLong(bsp.dleafs[i].contents);
        bsp.dleafs[i].cluster = LittleLong(bsp.dleafs[i].cluster);
        bsp.dleafs[i].area = LittleLong(bsp.dleafs[i].area);
        for (j = 0; j < 3; j++) {
            bsp.dleafs[i].mins[j] = LittleFloat(bsp.dleafs[i].mins[j]);
            bsp.dleafs[i].maxs[j] = LittleFloat(bsp.dleafs[i].maxs[j]);
        }

        bsp.dleafs[i].firstleafface = LittleLong(bsp.dleafs[i].firstleafface);
        bsp.dleafs[i].numleaffaces = LittleLong(bsp.dleafs[i].numleaffaces);
        bsp.dleafs[i].firstleafbrush = LittleLong(bsp.dleafs[i].firstleafbrush);
        bsp.dleafs[i].numleafbrushes = LittleLong(bsp.dleafs[i].numleafbrushes);
    }

    //
    // leaffaces
    //
    for (i = 0; i < bsp.numleaffaces; i++)
        bsp.dleaffaces[i] = LittleLong(bsp.dleaffaces[i]);

    //
    // leafbrushes
    //
    for (i = 0; i < bsp.numleafbrushes; i++)
        bsp.dleafbrushes[i] = LittleLong(bsp.dleafbrushes[i]);

    //
    // surfedges
    //
    for (i = 0; i < bsp.numsurfedges; i++)
        bsp.dsurfedges[i] = LittleLong(bsp.dsurfedges[i]);

    //
    // edges
    //
    for (i = 0; i < bsp.numedges; i++) {
        bsp.dedges[i][0] = LittleLong(bsp.dedges[i][0]);
        bsp.dedges[i][1] = LittleLong(bsp.dedges[i][1]);
    }

    //
    // brushes
    //
    for (i = 0; i < bsp.numbrushes; i++) {
        bsp.dbrushes[i].firstside = LittleLong(bsp.dbrushes[i].firstside);
        bsp.dbrushes[i].numsides = LittleLong(bsp.dbrushes[i].numsides);
        bsp.dbrushes[i].contents = LittleLong(bsp.dbrushes[i].contents);
    }

    //
    // areas
    //
    for (i = 0; i < bsp.numareas; i++) {
        bsp.dareas[i].numareaportals = LittleLong(bsp.dareas[i].numareaportals);
        bsp.dareas[i].firstareaportal = LittleLong(bsp.dareas[i].firstareaportal);
    }

    //
    // areasportals
    //
    for (i = 0; i < bsp.numareaportals; i++) {
        bsp.dareaportals[i].portalnum = LittleLong(bsp.dareaportals[i].portalnum);
        bsp.dareaportals[i].otherarea = LittleLong(bsp.dareaportals[i].otherarea);
    }

    //
    // brushsides
    //
    for (i = 0; i < bsp.numbrushsides; i++) {
        bsp.dbrushsides[i].planenum = LittleLong(bsp.dbrushsides[i].planenum);
        bsp.dbrushsides[i].texinfo = LittleLong(bsp.dbrushsides[i].texinfo);
    }

    //
    // visibility
    //
    if (bsp.dvis) {
        if (todisk)
            j = bsp.dvis->numclusters;
        else
            j = LittleLong(bsp.dvis->numclusters);
        bsp.dvis->numclusters = LittleLong(bsp.dvis->numclusters);
        for (i = 0; i < j; i++) {
            bsp.dvis->bitofs[i][0] = LittleLong(bsp.dvis->bitofs[i][0]);
            bsp.dvis->bitofs[i][1] = LittleLong(bsp.dvis->bitofs[i][1]);
        }
    }
}

/*
 * =============
 * SwapBSPFile
 * Byte swaps all data in a bsp file.
 * =============
 */
static void SwapBSPFile(bspdata_t *bspdata, swaptype_t swap)
{
    if (bspdata->version == &bspver_q2) {
        Q2_SwapBSPFile(std::get<q2bsp_t>(bspdata->bsp), swap == TO_DISK);
        return;
    } else if (bspdata->version == &bspver_qbism) {
        Q2_Qbism_SwapBSPFile(std::get<q2bsp_qbism_t>(bspdata->bsp), swap == TO_DISK);
        return;
    }

    if (bspdata->version == &bspver_q1 || bspdata->version == &bspver_h2 || bspdata->version == &bspver_hl) {
        bsp29_t &bsp = std::get<bsp29_t>(bspdata->bsp);

        SwapBSPVertexes(bsp.numvertexes, bsp.dvertexes);
        SwapBSPPlanes(bsp.numplanes, bsp.dplanes);
        SwapBSPTexinfo(bsp.numtexinfo, bsp.texinfo);
        SwapBSP29Faces(bsp.numfaces, bsp.dfaces);
        SwapBSP29Nodes(bsp.numnodes, bsp.dnodes);
        SwapBSP29Leafs(bsp.numleafs, bsp.dleafs);
        SwapBSP29Clipnodes(bsp.numclipnodes, bsp.dclipnodes);
        SwapBSPMiptex(bsp.texdatasize, bsp.dtexdata, swap);
        SwapBSP29Marksurfaces(bsp.nummarksurfaces, bsp.dmarksurfaces);
        SwapBSPSurfedges(bsp.numsurfedges, bsp.dsurfedges);
        SwapBSP29Edges(bsp.numedges, bsp.dedges);
        if (bspdata->version == &bspver_h2) {
            SwapBSPModels(bsp.nummodels, bsp.dmodels_h2);
        } else {
            SwapBSPModels(bsp.nummodels, bsp.dmodels_q);
        }

        return;
    }

    if (bspdata->version == &bspver_bsp2rmq || bspdata->version == &bspver_h2bsp2rmq) {
        bsp2rmq_t &bsp = std::get<bsp2rmq_t>(bspdata->bsp);

        SwapBSPVertexes(bsp.numvertexes, bsp.dvertexes);
        SwapBSPPlanes(bsp.numplanes, bsp.dplanes);
        SwapBSPTexinfo(bsp.numtexinfo, bsp.texinfo);
        SwapBSP2Faces(bsp.numfaces, bsp.dfaces);
        SwapBSP2rmqNodes(bsp.numnodes, bsp.dnodes);
        SwapBSP2rmqLeafs(bsp.numleafs, bsp.dleafs);
        SwapBSP2Clipnodes(bsp.numclipnodes, bsp.dclipnodes);
        SwapBSPMiptex(bsp.texdatasize, bsp.dtexdata, swap);
        SwapBSP2Marksurfaces(bsp.nummarksurfaces, bsp.dmarksurfaces);
        SwapBSPSurfedges(bsp.numsurfedges, bsp.dsurfedges);
        SwapBSP2Edges(bsp.numedges, bsp.dedges);
        if (bspdata->version == &bspver_h2bsp2rmq) {
            SwapBSPModels(bsp.nummodels, bsp.dmodels_h2);
        } else {
            SwapBSPModels(bsp.nummodels, bsp.dmodels_q);
        }

        return;
    }

    if (bspdata->version == &bspver_bsp2 || bspdata->version == &bspver_h2bsp2) {
        bsp2_t &bsp = std::get<bsp2_t>(bspdata->bsp);

        SwapBSPVertexes(bsp.numvertexes, bsp.dvertexes);
        SwapBSPPlanes(bsp.numplanes, bsp.dplanes);
        SwapBSPTexinfo(bsp.numtexinfo, bsp.texinfo);
        SwapBSP2Faces(bsp.numfaces, bsp.dfaces);
        SwapBSP2Nodes(bsp.numnodes, bsp.dnodes);
        SwapBSP2Leafs(bsp.numleafs, bsp.dleafs);
        SwapBSP2Clipnodes(bsp.numclipnodes, bsp.dclipnodes);
        SwapBSPMiptex(bsp.texdatasize, bsp.dtexdata, swap);
        SwapBSP2Marksurfaces(bsp.nummarksurfaces, bsp.dmarksurfaces);
        SwapBSPSurfedges(bsp.numsurfedges, bsp.dsurfedges);
        SwapBSP2Edges(bsp.numedges, bsp.dedges);
        if (bspdata->version == &bspver_h2bsp2) {
            SwapBSPModels(bsp.nummodels, bsp.dmodels_h2);
        } else {
            SwapBSPModels(bsp.nummodels, bsp.dmodels_q);
        }

        return;
    }

    FError("Unsupported BSP version: {}", BSPVersionString(bspdata->version));
}

/*
 * =========================================================================
 * BSP Format Conversion (ver. q2 <-> MBSP)
 * =========================================================================
 */

static uint8_t *Q2BSPtoM_CopyVisData(const dvis_t *dvisq2, int vissize, int *outvissize, mleaf_t *leafs, int numleafs)
{
    if (!*outvissize) {
        return nullptr;
    }

    // FIXME: assumes PHS always follows PVS.
    int32_t phs_start = INT_MAX, pvs_start = INT_MAX;
    size_t header_offset = sizeof(dvis_t) + (sizeof(int32_t) * dvisq2->numclusters * 2);

    for (int32_t i = 0; i < dvisq2->numclusters; i++) {
        pvs_start = std::min(pvs_start, (int32_t)(dvisq2->bitofs[i][DVIS_PVS]));
        phs_start = std::min(phs_start, (int32_t)(dvisq2->bitofs[i][DVIS_PHS] - header_offset));

        for (int32_t l = 0; l < numleafs; l++) {
            if (leafs[l].cluster == i) {
                leafs[l].visofs = dvisq2->bitofs[i][DVIS_PVS] - header_offset;
            }
        }
    }

    // cut off the PHS and header
    *outvissize -= header_offset + ((*outvissize - header_offset) - phs_start);

    uint8_t *vis = new uint8_t[*outvissize];
    memcpy(vis, ((uint8_t *)dvisq2) + pvs_start, *outvissize);
    return vis;
}

/*
================
CalcPHS

Calculate the PHS (Potentially Hearable Set)
by ORing together all the PVS visible from a leaf
================
*/
static std::vector<uint8_t> CalcPHS(
    int32_t portalclusters, const uint8_t *visdata, int *visdatasize, int32_t bitofs[][2])
{
    const int32_t leafbytes = (portalclusters + 7) >> 3;
    const int32_t leaflongs = leafbytes / sizeof(long);
    std::vector<uint8_t> compressed_phs;
    // FIXME: should this use alloca? 
    uint8_t *uncompressed = new uint8_t[leafbytes];
    uint8_t *uncompressed_2 = new uint8_t[leafbytes];
    uint8_t *compressed = new uint8_t[leafbytes * 2];
    uint8_t *uncompressed_orig = new uint8_t[leafbytes];

    printf("Building PHS...\n");

    int32_t count = 0;
    for (int32_t i = 0; i < portalclusters; i++) {
        const uint8_t *scan = &visdata[bitofs[i][DVIS_PVS]];

        DecompressRow(scan, leafbytes, uncompressed);
        memset(uncompressed_orig, 0, leafbytes);
        memcpy(uncompressed_orig, uncompressed, leafbytes);

        scan = uncompressed_orig;

        for (int32_t j = 0; j < leafbytes; j++) {
            uint8_t bitbyte = scan[j];
            if (!bitbyte)
                continue;
            for (int32_t k = 0; k < 8; k++) {
                if (!(bitbyte & (1 << k)))
                    continue;
                // OR this pvs row into the phs
                int32_t index = ((j << 3) + k);
                if (index >= portalclusters)
                    FError("Bad bit in PVS"); // pad bits should be 0
                const uint8_t *src_compressed = &visdata[bitofs[index][DVIS_PVS]];
                DecompressRow(src_compressed, leafbytes, uncompressed_2);
                const long *src = (long *)uncompressed_2;
                long *dest = (long *)uncompressed;
                for (int32_t l = 0; l < leaflongs; l++)
                    ((long *)uncompressed)[l] |= src[l];
            }
        }
        for (int32_t j = 0; j < portalclusters; j++)
            if (uncompressed[j >> 3] & (1 << (j & 7)))
                count++;

        //
        // compress the bit string
        //
        int32_t j = CompressRow(uncompressed, leafbytes, compressed);

        bitofs[i][DVIS_PHS] = compressed_phs.size();

        compressed_phs.insert(compressed_phs.end(), compressed, compressed + j);
    }

    delete[] uncompressed;
    delete[] uncompressed_2;
    delete[] compressed;
    delete[] uncompressed_orig;

    fmt::print("Average clusters hearable: {}\n", count / portalclusters);

    return compressed_phs;
}

static dvis_t *MBSPtoQ2_CopyVisData(const uint8_t *visdata, int *visdatasize, int numleafs, const mleaf_t *leafs)
{
    if (!*visdatasize) {
        return nullptr;
    }

    int32_t num_clusters = 0;

    for (int32_t i = 0; i < numleafs; i++) {
        num_clusters = std::max(num_clusters, leafs[i].cluster + 1);
    }

    size_t vis_offset = sizeof(dvis_t) + (sizeof(int32_t) * num_clusters * 2);
    dvis_t *vis = (dvis_t *)calloc(1, vis_offset + *visdatasize);

    vis->numclusters = num_clusters;

    // the leaves are already using a per-cluster visofs, so just find one matching
    // cluster and note it down under bitofs.
    // we're also not worrying about PHS currently.
    for (int32_t i = 0; i < num_clusters; i++) {
        for (int32_t l = 0; l < numleafs; l++) {
            if (leafs[l].cluster == i) {
                // copy PVS visofs
                vis->bitofs[i][DVIS_PVS] = leafs[l].visofs;
                break;
            }
        }
    }

    std::vector<uint8_t> phs = CalcPHS(num_clusters, visdata, visdatasize, vis->bitofs);

    vis = (dvis_t *)realloc(vis, vis_offset + *visdatasize + phs.size());

    // offset the pvs/phs properly
    for (int32_t i = 0; i < num_clusters; i++) {
        vis->bitofs[i][DVIS_PVS] += vis_offset;
        vis->bitofs[i][DVIS_PHS] += vis_offset + *visdatasize;
    }

    memcpy(((uint8_t *)vis) + vis_offset, visdata, *visdatasize);
    *visdatasize += vis_offset;

    memcpy(((uint8_t *)vis) + *visdatasize, phs.data(), phs.size());
    *visdatasize += phs.size();

    return vis;
}

/*
 * =========================================================================
 * BSP Format Conversion (no-ops)
 * =========================================================================
 */

// copy structured conversion data
template<typename T, typename F, typename = std::enable_if_t<std::is_convertible_v<T, F>>>
inline void CopyArray(const F *in, int numelems, T *&out)
{
    out = new T[numelems];

    for (int i = 0; i < numelems; i++)
    {
        if constexpr(std::is_arithmetic_v<T> && std::is_arithmetic_v<F>)
            out[i] = numeric_cast<T>(in[i]);
        else
            out[i] = static_cast<T>(in[i]);
    }
}

// copy structured array
template<typename T, typename F, size_t N>
inline void CopyArray(const std::array<F, N> *in, int numelems, std::array<T, N> *&out)
{
    out = new std::array<T, N>[numelems];

    for (int i = 0; i < numelems; i++)
    {
        if constexpr(std::is_arithmetic_v<T> && std::is_arithmetic_v<F>)
            out[i] = array_cast<std::array<T, N>>(in[i]);
        else
            out[i] = static_cast<T>(in[i]);
    }
}

static uint8_t *BSP29_CopyVisData(const uint8_t *dvisdata, int visdatasize)
{
    uint8_t *out;
    CopyArray(dvisdata, visdatasize, out);
    return out;
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

            if (std::holds_alternative<bsp29_t>(bspdata->bsp)) {
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
            } else if (std::holds_alternative<q2bsp_t>(bspdata->bsp)) {
                // bspver_q2 -> bspver_generic
                const q2bsp_t &q2bsp = std::get<q2bsp_t>(bspdata->bsp);

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
            } else if (std::holds_alternative<q2bsp_qbism_t>(bspdata->bsp)) {
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
            } else {
                return false;
            }

            bspdata->loadversion = mbsp.loadversion = bspdata->version;
            bspdata->version = to_version;

            bspdata->bsp = std::move(mbsp);
            return true;
        } else if (bspdata->version == &bspver_generic) {
            // Conversions from bspver_generic
            const mbsp_t &mbsp = std::get<mbsp_t>(bspdata->bsp);

            if (to_version == &bspver_q1 || to_version == &bspver_h2 || to_version == &bspver_hl) {
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

                /* Conversion complete! */
                bspdata->version = to_version;
                bspdata->bsp = std::move(bsp29);

                return true;
            } else if (to_version == &bspver_q2) {
                // bspver_generic -> bspver_q2
                q2bsp_t q2bsp { };

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
                CopyArray(mbsp.dedges, mbsp.numedges,q2bsp.dedges);
                CopyArray(mbsp.dleaffaces, mbsp.numleaffaces, q2bsp.dleaffaces);
                CopyArray(mbsp.dleafbrushes, mbsp.numleafbrushes,q2bsp.dleafbrushes);
                CopyArray(mbsp.dsurfedges, mbsp.numsurfedges, q2bsp.dsurfedges);

                CopyArray(mbsp.dareas, mbsp.numareas, q2bsp.dareas);
                CopyArray(mbsp.dareaportals, mbsp.numareaportals, q2bsp.dareaportals);

                CopyArray(mbsp.dbrushes, mbsp.numbrushes, q2bsp.dbrushes);
                CopyArray(mbsp.dbrushsides, mbsp.numbrushsides, q2bsp.dbrushsides);

                /* Conversion complete! */
                bspdata->version = to_version;
                bspdata->bsp = std::move(q2bsp);

                return true;
            } else if (to_version == &bspver_qbism) {
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

                /* Conversion complete! */
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

                /* Conversion complete! */
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

                /* Conversion complete! */
                bspdata->version = to_version;
                bspdata->bsp = std::move(bsp2);

                return true;
            }
        }

        Error("Don't know how to convert BSP version {} to {}", BSPVersionString(bspdata->version),
            BSPVersionString(to_version));
    }
    catch (std::overflow_error)
    {
        return false;
    }
}

static int isHexen2(const dheader_t *header)
{
    /*
        the world should always have some face.
        however, if the sizes are wrong then we're actually reading headnode[6]. hexen2 only used 5 hulls, so this
       should be 0 in hexen2, and not in quake.
    */
    const dmodelq1_t *modelsq1 = (const dmodelq1_t *)((const uint8_t *)header + header->lumps[LUMP_MODELS].fileofs);
    return !modelsq1->numfaces;
}

/*
 * =========================================================================
 * ...
 * =========================================================================
 */

const lumpspec_t lumpspec_bsp29[] = {
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

const lumpspec_t lumpspec_bsp2rmq[] = {
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

const lumpspec_t lumpspec_bsp2[] = {
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

const lumpspec_t lumpspec_bsp29_h2[] = {
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

const lumpspec_t lumpspec_bsp2rmq_h2[] = {
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

const lumpspec_t lumpspec_bsp2_h2[] = {
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

const lumpspec_t lumpspec_q2bsp[] = {
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

const lumpspec_t lumpspec_qbism[] = {
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

static const lumpspec_t *LumpspecsForVersion(const bspversion_t *version)
{
    const lumpspec_t *lumpspec;

    if (version == &bspver_q1 || version == &bspver_hl) {
        lumpspec = lumpspec_bsp29;
    } else if (version == &bspver_bsp2rmq) {
        lumpspec = lumpspec_bsp2rmq;
    } else if (version == &bspver_bsp2) {
        lumpspec = lumpspec_bsp2;
    } else if (version == &bspver_h2) {
        lumpspec = lumpspec_bsp29_h2;
    } else if (version == &bspver_h2bsp2rmq) {
        lumpspec = lumpspec_bsp2rmq_h2;
    } else if (version == &bspver_h2bsp2) {
        lumpspec = lumpspec_bsp2_h2;
    } else if (version == &bspver_q2) {
        lumpspec = lumpspec_q2bsp;
    } else if (version == &bspver_qbism) {
        lumpspec = lumpspec_qbism;
    } else {
        Error("Unsupported BSP version: {}", BSPVersionString(version));
    }
    return lumpspec;
}

template<typename T>
static int CopyLump(const void *header, const bspversion_t *version, const lump_t *lumps, int lumpnum, T **bufferptr)
{
    const lumpspec_t *lumpspecs = LumpspecsForVersion(version);
    const lumpspec_t *lumpspec = &lumpspecs[lumpnum];
    int length = lumps[lumpnum].filelen;
    int ofs = lumps[lumpnum].fileofs;

    if (*bufferptr)
        delete[] *bufferptr;

    if (lumpspec->size > 1 && (sizeof(T) != lumpspec->size || length % lumpspec->size))
        FError("odd {} lump size", lumpspec->name);

    T *buffer;

    if constexpr(std::is_same_v<T, char>)
        buffer = *bufferptr = new T[length + 1];
    else
        buffer = *bufferptr = new T[length];

    if (!buffer)
        FError("allocation of {} bytes failed.", length);

    memcpy(buffer, (const uint8_t *)header + ofs, length);
    
    if constexpr(std::is_same_v<T, char>)
        buffer[length] = 0; /* In case of corrupt entity lump */

    return length / lumpspec->size;
}

void BSPX_AddLump(bspdata_t *bspdata, const char *xname, const void *xdata, size_t xsize)
{
    bspxentry_t *e;
    bspxentry_t **link;
    if (!xdata) {
        for (link = &bspdata->bspxentries; *link;) {
            e = *link;
            if (!strcmp(e->lumpname, xname)) {
                *link = e->next;
                delete e;
                break;
            } else
                link = &(*link)->next;
        }
        return;
    }
    for (e = bspdata->bspxentries; e; e = e->next) {
        if (!strcmp(e->lumpname, xname))
            break;
    }
    if (!e) {
        e = new bspxentry_t { };
        strncpy(e->lumpname, xname, sizeof(e->lumpname));
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
        if (!strcmp(e->lumpname, xname))
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

    /* transfer the header data to these variables */
    int numlumps;
    lump_t *lumps;

    /* check for IBSP */
    bspversion_t temp_version{LittleLong(((int *)file_data)[0])};

    if (temp_version.ident == Q2_BSPIDENT || temp_version.ident == Q2_QBISMIDENT) {
        q2_dheader_t *q2header = (q2_dheader_t *)file_data;
        q2header->version = LittleLong(q2header->version);

        numlumps = Q2_HEADER_LUMPS;
        temp_version.version = q2header->version;
        lumps = q2header->lumps;
    } else {
        dheader_t *q1header = (dheader_t *)file_data;
        q1header->version = LittleLong(q1header->version);

        numlumps = BSP_LUMPS;
        lumps = q1header->lumps;

        // not useful for Q1BSP, but we'll initialize it to -1
        temp_version.version = NO_VERSION;
    }

    /* check the file version */
    if (!BSPVersionSupported(temp_version.ident, temp_version.version, &bspdata->version)) {
        LogPrint("BSP is version {}\n", BSPVersionString(&temp_version));
        Error("Sorry, this bsp version is not supported.");
    } else {
        // special case handling for Hexen II
        if (isHexen2((dheader_t *)file_data)) {
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

    /* swap the lump headers */
    for (i = 0; i < numlumps; i++) {
        lumps[i].fileofs = LittleLong(lumps[i].fileofs);
        lumps[i].filelen = LittleLong(lumps[i].filelen);
    }

    /* copy the data */
    if (bspdata->version == &bspver_q2) {
        q2_dheader_t *header = (q2_dheader_t *)file_data;
        q2bsp_t bsp { };

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

        bspdata->bsp = std::move(bsp);
    } else if (bspdata->version == &bspver_qbism) {
        q2_dheader_t *header = (q2_dheader_t *)file_data;
        q2bsp_qbism_t bsp { };

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

        bspdata->bsp = std::move(bsp);
    } else if (bspdata->version == &bspver_q1 || bspdata->version == &bspver_h2 || bspdata->version == &bspver_hl) {
        dheader_t *header = (dheader_t *)file_data;
        bsp29_t bsp { };

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

        bspdata->bsp = std::move(bsp);
    } else if (bspdata->version == &bspver_bsp2rmq || bspdata->version == &bspver_h2bsp2rmq) {
        dheader_t *header = (dheader_t *)file_data;
        bsp2rmq_t bsp { };

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

        bspdata->bsp = std::move(bsp);
    } else if (bspdata->version == &bspver_bsp2 || bspdata->version == &bspver_h2bsp2) {
        dheader_t *header = (dheader_t *)file_data;
        bsp2_t bsp { };

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

        bspdata->bsp = std::move(bsp);
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
                BSPX_AddLump(bspdata, xlump[xlumps].lumpname, lumpdata, len);
            }
        } else {
            if (!memcmp(&bspx->id, "BSPX", 4))
                printf("invalid bspx header\n");
        }
    }

    /* everything has been copied out */
    delete[] file_data;

    /* swap everything */
    SwapBSPFile(bspdata, TO_CPU);
}

/* ========================================================================= */

struct bspfile_t
{
    const bspversion_t *version;

    // which one is used depends on version
    union
    {
        dheader_t q1header;
        q2_dheader_t q2header;
    };

    qfile_t file { nullptr, nullptr };
};

static void AddLump(bspfile_t *bspfile, int lumpnum, const void *data, int count)
{
    bool q2 = false;
    size_t size;
    const lumpspec_t *lumpspecs = LumpspecsForVersion(bspfile->version);
    const lumpspec_t *lumpspec = &lumpspecs[lumpnum];
    lump_t *lumps;

    if (bspfile->version->version != NO_VERSION) {
        lumps = bspfile->q2header.lumps;
    } else {
        lumps = bspfile->q1header.lumps;
    }

    size = lumpspec->size * count;

    uint8_t pad[4] = {0};
    lump_t *lump = &lumps[lumpnum];

    lump->fileofs = LittleLong(SafeTell(bspfile->file));
    lump->filelen = LittleLong(size);
    SafeWrite(bspfile->file, data, size);
    if (size % 4)
        SafeWrite(bspfile->file, pad, 4 - (size % 4));
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

    SwapBSPFile(bspdata, TO_DISK);

    bspfile.version = bspdata->version;

    // headers are union'd, so this sets both
    bspfile.q2header.ident = LittleLong(bspfile.version->ident);

    if (bspfile.version->version != NO_VERSION) {
        bspfile.q2header.version = LittleLong(bspfile.version->version);
    }

    LogPrint("Writing {} as BSP version {}\n", filename, BSPVersionString(bspdata->version));
    bspfile.file = SafeOpenWrite(filename);

    /* Save header space, updated after adding the lumps */
    if (bspfile.version->version != NO_VERSION) {
        SafeWrite(bspfile.file, &bspfile.q2header, sizeof(bspfile.q2header));
    } else {
        SafeWrite(bspfile.file, &bspfile.q1header, sizeof(bspfile.q1header));
    }

    if (std::holds_alternative<bsp29_t>(bspdata->bsp)) {
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
    } else if (std::holds_alternative<q2bsp_t>(bspdata->bsp)) {
        const q2bsp_t &bsp = std::get<q2bsp_t>(bspdata->bsp);

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
    } else if (std::holds_alternative<q2bsp_qbism_t>(bspdata->bsp)) {
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
    } else {
        FError("Unknown format");
    }

    /*BSPX lumps are at a 4-byte alignment after the last of any official lump*/
    if (bspdata->bspxentries) {
        bspx_header_t xheader;
        bspxentry_t *x;
        bspx_lump_t xlumps[64];
        uint32_t l;
        long bspxheader = SafeTell(bspfile.file);
        if (bspxheader & 3)
            FError("BSPX header is misaligned");
        xheader.id[0] = 'B';
        xheader.id[1] = 'S';
        xheader.id[2] = 'P';
        xheader.id[3] = 'X';
        xheader.numlumps = 0;
        for (x = bspdata->bspxentries; x; x = x->next)
            xheader.numlumps++;

        if (xheader.numlumps > sizeof(xlumps) / sizeof(xlumps[0])) /*eep*/
            xheader.numlumps = sizeof(xlumps) / sizeof(xlumps[0]);

        SafeWrite(bspfile.file, &xheader, sizeof(xheader));
        SafeWrite(bspfile.file, xlumps, xheader.numlumps * sizeof(xlumps[0]));

        for (x = bspdata->bspxentries, l = 0; x && l < xheader.numlumps; x = x->next, l++) {
            uint8_t pad[4] = {0};
            xlumps[l].filelen = LittleLong(x->lumpsize);
            xlumps[l].fileofs = LittleLong(SafeTell(bspfile.file));
            strncpy(xlumps[l].lumpname, x->lumpname, sizeof(xlumps[l].lumpname));
            SafeWrite(bspfile.file, x->lumpdata, x->lumpsize);
            if (x->lumpsize % 4)
                SafeWrite(bspfile.file, pad, 4 - (x->lumpsize % 4));
        }

        SafeSeek(bspfile.file, bspxheader, SEEK_SET);
        SafeWrite(bspfile.file, &xheader, sizeof(xheader));
        SafeWrite(bspfile.file, xlumps, xheader.numlumps * sizeof(xlumps[0]));
    }

    SafeSeek(bspfile.file, 0, SEEK_SET);

    // write the real header
    if (bspfile.version->version != NO_VERSION) {
        SafeWrite(bspfile.file, &bspfile.q2header, sizeof(bspfile.q2header));
    } else {
        SafeWrite(bspfile.file, &bspfile.q1header, sizeof(bspfile.q1header));
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
    const lumpspec_t *lumpspec = LumpspecsForVersion(bspdata->version);

    if (std::holds_alternative<q2bsp_t>(bspdata->bsp)) {
        const q2bsp_t &bsp = std::get<q2bsp_t>(bspdata->bsp);

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
    } else if (std::holds_alternative<q2bsp_qbism_t>(bspdata->bsp)) {
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
    } else {
        Error("Unsupported BSP version: {}", BSPVersionString(bspdata->version));
    }

    if (bspdata->bspxentries) {
        bspxentry_t *x;
        for (x = bspdata->bspxentries; x; x = x->next) {
            LogPrint("{:7} {:<12} {:10}\n", "BSPX", x->lumpname, (int)x->lumpsize);
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
