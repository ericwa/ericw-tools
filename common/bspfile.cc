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
#include <common/fs.hh>
#include <cstdint>
#include <limits.h>

#include <fmt/format.h>

static std::vector<qvec3b> make_palette(std::initializer_list<uint8_t> bytes)
{
    Q_assert((bytes.size() % 3) == 0);

    std::vector<qvec3b> result;
    result.reserve(bytes.size() / 3);

    for (const uint8_t *it = bytes.begin(); it < bytes.end(); it += 3) {
        result.emplace_back(it[0], it[1], it[2]);
    }

    return result;
}

struct gamedef_generic_t : public gamedef_t
{
    gamedef_generic_t() : gamedef_t("") { id = GAME_UNKNOWN; }

    bool surf_is_lightmapped(const surfflags_t &) const { throw std::bad_cast(); }

    bool surf_is_subdivided(const surfflags_t &) const { throw std::bad_cast(); }

    bool surfflags_are_valid(const surfflags_t &) const { throw std::bad_cast(); }

    bool texinfo_is_hintskip(const surfflags_t &, const std::string &) const { throw std::bad_cast(); }

    contentflags_t cluster_contents(const contentflags_t &, const contentflags_t &) const { throw std::bad_cast(); }

    int32_t get_content_type(const contentflags_t &) const { throw std::bad_cast(); }

    int32_t contents_priority(const contentflags_t &) const { throw std::bad_cast(); }

    contentflags_t create_extended_contents(const int32_t &) const { throw std::bad_cast(); }

    contentflags_t create_empty_contents(const int32_t &) const { throw std::bad_cast(); }

    contentflags_t create_solid_contents(const int32_t &) const { throw std::bad_cast(); }

    contentflags_t create_sky_contents(const int32_t &) const { throw std::bad_cast(); }

    contentflags_t create_liquid_contents(const int32_t &, const int32_t &) const { throw std::bad_cast(); }

    bool contents_are_empty(const contentflags_t &) const { throw std::bad_cast(); }

    bool contents_are_solid(const contentflags_t &) const { throw std::bad_cast(); }

    bool contents_are_sky(const contentflags_t &) const { throw std::bad_cast(); }

    bool contents_are_liquid(const contentflags_t &) const { throw std::bad_cast(); }

    bool contents_are_valid(const contentflags_t &, bool) const { throw std::bad_cast(); }

    bool portal_can_see_through(const contentflags_t &, const contentflags_t &) const { throw std::bad_cast(); }

    std::string get_contents_display(const contentflags_t &) const { throw std::bad_cast(); }

    const std::initializer_list<aabb3d> &get_hull_sizes() const { throw std::bad_cast(); }

    contentflags_t face_get_contents(const std::string &, const surfflags_t &, const contentflags_t &) const { throw std::bad_cast(); };

    void init_filesystem(const fs::path &) const { throw std::bad_cast(); };

    const std::vector<qvec3b> &get_default_palette() const { throw std::bad_cast(); };
};

template<gameid_t ID>
struct gamedef_q1_like_t : public gamedef_t
{
    gamedef_q1_like_t(const char *base_dir = "ID1") : gamedef_t(base_dir)
    {
        this->id = ID;
    }

    bool surf_is_lightmapped(const surfflags_t &flags) const { return !(flags.native & TEX_SPECIAL); }

    bool surf_is_subdivided(const surfflags_t &flags) const { return !(flags.native & TEX_SPECIAL); }

    bool surfflags_are_valid(const surfflags_t &flags) const
    {
        // Q1 only supports TEX_SPECIAL
        return (flags.native & ~TEX_SPECIAL) == 0;
    }

    bool texinfo_is_hintskip(const surfflags_t &flags, const std::string &name) const
    {
        // anything texname other than "hint" in a hint brush is treated as "hintskip", and discarded
        return !string_iequals(name, "hint");
    }

    contentflags_t cluster_contents(const contentflags_t &contents0, const contentflags_t &contents1) const
    {
        if (contents0 == contents1)
            return contents0;

        /*
         * Clusters may be partially solid but still be seen into
         * ?? - Should we do something more explicit with mixed liquid contents?
         */
        if (contents0.native == CONTENTS_EMPTY || contents1.native == CONTENTS_EMPTY)
            return create_empty_contents();

        if (contents0.native >= CONTENTS_LAVA && contents0.native <= CONTENTS_WATER)
            return create_liquid_contents(contents0.native);
        if (contents1.native >= CONTENTS_LAVA && contents1.native <= CONTENTS_WATER)
            return create_liquid_contents(contents1.native);
        if (contents0.native == CONTENTS_SKY || contents1.native == CONTENTS_SKY)
            return create_sky_contents();

        return create_solid_contents();
    }

    int32_t get_content_type(const contentflags_t &contents) const { return contents.native; }

    int32_t contents_priority(const contentflags_t &contents) const
    {
        if (contents.extended & CFLAGS_DETAIL) {
            return 5;
        } else if (contents.extended & CFLAGS_DETAIL_FENCE) {
            return 4;
        } else if (contents.extended & CFLAGS_DETAIL_ILLUSIONARY) {
            return 3;
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

    contentflags_t create_extended_contents(const int32_t &cflags) const { return {0, cflags}; }

    contentflags_t create_empty_contents(const int32_t &cflags = 0) const
    {
        Q_assert(!(cflags & CFLAGS_CONTENTS_MASK));

        return {CONTENTS_EMPTY, cflags};
    }

    contentflags_t create_solid_contents(const int32_t &cflags = 0) const
    {
        Q_assert(!(cflags & CFLAGS_CONTENTS_MASK));

        return {CONTENTS_SOLID, cflags};
    }

    contentflags_t create_sky_contents(const int32_t &cflags = 0) const
    {
        Q_assert(!(cflags & CFLAGS_CONTENTS_MASK));

        return {CONTENTS_SKY, cflags};
    }

    contentflags_t create_liquid_contents(const int32_t &liquid_type, const int32_t &cflags = 0) const
    {
        Q_assert(!(cflags & CFLAGS_CONTENTS_MASK));

        return {liquid_type, cflags};
    }

    bool contents_are_empty(const contentflags_t &contents) const
    {
        if (contents.extended & CFLAGS_CONTENTS_MASK)
            return false;

        return contents.native == CONTENTS_EMPTY;
    }

    bool contents_are_solid(const contentflags_t& contents) const 
    {
        if (contents.extended & CFLAGS_CONTENTS_MASK)
            return false;

        return contents.native == CONTENTS_SOLID;
    }

    bool contents_are_sky(const contentflags_t& contents) const
    {
        if (contents.extended & CFLAGS_CONTENTS_MASK)
            return false;

        return contents.native == CONTENTS_SKY;
    }

    bool contents_are_liquid(const contentflags_t &contents) const
    {
        if (contents.extended & CFLAGS_CONTENTS_MASK)
            return false;

        return contents.native <= CONTENTS_WATER && contents.native >= CONTENTS_LAVA;
    }

    bool contents_are_valid(const contentflags_t &contents, bool strict) const
    {
        if (!contents.native && !strict) {
            return true;
        }

        switch (contents.native) {
            case CONTENTS_EMPTY:
            case CONTENTS_SOLID:
            case CONTENTS_WATER:
            case CONTENTS_SLIME:
            case CONTENTS_LAVA:
            case CONTENTS_SKY: return true;
            default: return false;
        }
    }

    bool portal_can_see_through(const contentflags_t &contents0, const contentflags_t &contents1) const
    {
        /* If contents values are the same and not solid, can see through */
        return !(contents0.is_solid(this) || contents1.is_solid(this)) && contents0 == contents1;
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
            {{0, 0, 0}, {0, 0, 0}}, {{-16, -16, -32}, {16, 16, 24}}, {{-32, -32, -64}, {32, 32, 24}}};

        return hulls;
    }
    
    contentflags_t face_get_contents(const std::string &texname, const surfflags_t &flags, const contentflags_t &) const
    {
        // check for strong content indicators
        if (!Q_strcasecmp(texname.data(), "origin")) {
            return create_extended_contents(CFLAGS_ORIGIN);
        } else if (!Q_strcasecmp(texname.data(), "hint") || !Q_strcasecmp(texname.data(), "hintskip")) {
            return create_extended_contents(CFLAGS_HINT);
        } else if (!Q_strcasecmp(texname.data(), "clip")) {
            return create_extended_contents(CFLAGS_CLIP);
        } else if (texname[0] == '*') {
            if (!Q_strncasecmp(texname.data() + 1, "lava", 4)) {
                return create_liquid_contents(CONTENTS_LAVA);
            } else if (!Q_strncasecmp(texname.data() + 1, "slime", 5)) {
                return create_liquid_contents(CONTENTS_SLIME);
            } else {
                return create_liquid_contents(CONTENTS_WATER);
            }
        } else if (!Q_strncasecmp(texname.data(), "sky", 3)) {
            return create_sky_contents();
        } else {
            // and anything else is assumed to be a regular solid.
            return create_solid_contents();
        }
    }

    void init_filesystem(const fs::path &) const
    {
        // Q1-like games don't care about the local
        // filesystem.
    }

    const std::vector<qvec3b> &get_default_palette() const
    {
        static constexpr std::initializer_list<uint8_t> palette_bytes
        {
            0, 0, 0, 15, 15, 15, 31, 31, 31, 47, 47, 47, 63, 63, 63, 75, 75, 75, 91, 91, 91, 107, 107, 107, 123, 123, 123, 139,
            139, 139, 155, 155, 155, 171, 171, 171, 187, 187, 187, 203, 203, 203, 219, 219, 219, 235, 235, 235, 15, 11, 7,
            23, 15, 11, 31, 23, 11, 39, 27, 15, 47, 35, 19, 55, 43, 23, 63, 47, 23, 75, 55, 27, 83, 59, 27, 91, 67, 31, 99,
            75, 31, 107, 83, 31, 115, 87, 31, 123, 95, 35, 131, 103, 35, 143, 111, 35, 11, 11, 15, 19, 19, 27, 27, 27, 39,
            39, 39, 51, 47, 47, 63, 55, 55, 75, 63, 63, 87, 71, 71, 103, 79, 79, 115, 91, 91, 127, 99, 99, 139, 107, 107,
            151, 115, 115, 163, 123, 123, 175, 131, 131, 187, 139, 139, 203, 0, 0, 0, 7, 7, 0, 11, 11, 0, 19, 19, 0, 27, 27,
            0, 35, 35, 0, 43, 43, 7, 47, 47, 7, 55, 55, 7, 63, 63, 7, 71, 71, 7, 75, 75, 11, 83, 83, 11, 91, 91, 11, 99, 99,
            11, 107, 107, 15, 7, 0, 0, 15, 0, 0, 23, 0, 0, 31, 0, 0, 39, 0, 0, 47, 0, 0, 55, 0, 0, 63, 0, 0, 71, 0, 0, 79,
            0, 0, 87, 0, 0, 95, 0, 0, 103, 0, 0, 111, 0, 0, 119, 0, 0, 127, 0, 0, 19, 19, 0, 27, 27, 0, 35, 35, 0, 47, 43,
            0, 55, 47, 0, 67, 55, 0, 75, 59, 7, 87, 67, 7, 95, 71, 7, 107, 75, 11, 119, 83, 15, 131, 87, 19, 139, 91, 19,
            151, 95, 27, 163, 99, 31, 175, 103, 35, 35, 19, 7, 47, 23, 11, 59, 31, 15, 75, 35, 19, 87, 43, 23, 99, 47, 31,
            115, 55, 35, 127, 59, 43, 143, 67, 51, 159, 79, 51, 175, 99, 47, 191, 119, 47, 207, 143, 43, 223, 171, 39, 239,
            203, 31, 255, 243, 27, 11, 7, 0, 27, 19, 0, 43, 35, 15, 55, 43, 19, 71, 51, 27, 83, 55, 35, 99, 63, 43, 111, 71,
            51, 127, 83, 63, 139, 95, 71, 155, 107, 83, 167, 123, 95, 183, 135, 107, 195, 147, 123, 211, 163, 139, 227, 179,
            151, 171, 139, 163, 159, 127, 151, 147, 115, 135, 139, 103, 123, 127, 91, 111, 119, 83, 99, 107, 75, 87, 95, 63,
            75, 87, 55, 67, 75, 47, 55, 67, 39, 47, 55, 31, 35, 43, 23, 27, 35, 19, 19, 23, 11, 11, 15, 7, 7, 187, 115, 159,
            175, 107, 143, 163, 95, 131, 151, 87, 119, 139, 79, 107, 127, 75, 95, 115, 67, 83, 107, 59, 75, 95, 51, 63, 83,
            43, 55, 71, 35, 43, 59, 31, 35, 47, 23, 27, 35, 19, 19, 23, 11, 11, 15, 7, 7, 219, 195, 187, 203, 179, 167, 191,
            163, 155, 175, 151, 139, 163, 135, 123, 151, 123, 111, 135, 111, 95, 123, 99, 83, 107, 87, 71, 95, 75, 59, 83,
            63, 51, 67, 51, 39, 55, 43, 31, 39, 31, 23, 27, 19, 15, 15, 11, 7, 111, 131, 123, 103, 123, 111, 95, 115, 103,
            87, 107, 95, 79, 99, 87, 71, 91, 79, 63, 83, 71, 55, 75, 63, 47, 67, 55, 43, 59, 47, 35, 51, 39, 31, 43, 31, 23,
            35, 23, 15, 27, 19, 11, 19, 11, 7, 11, 7, 255, 243, 27, 239, 223, 23, 219, 203, 19, 203, 183, 15, 187, 167, 15,
            171, 151, 11, 155, 131, 7, 139, 115, 7, 123, 99, 7, 107, 83, 0, 91, 71, 0, 75, 55, 0, 59, 43, 0, 43, 31, 0, 27,
            15, 0, 11, 7, 0, 0, 0, 255, 11, 11, 239, 19, 19, 223, 27, 27, 207, 35, 35, 191, 43, 43, 175, 47, 47, 159, 47,
            47, 143, 47, 47, 127, 47, 47, 111, 47, 47, 95, 43, 43, 79, 35, 35, 63, 27, 27, 47, 19, 19, 31, 11, 11, 15, 43,
            0, 0, 59, 0, 0, 75, 7, 0, 95, 7, 0, 111, 15, 0, 127, 23, 7, 147, 31, 7, 163, 39, 11, 183, 51, 15, 195, 75, 27,
            207, 99, 43, 219, 127, 59, 227, 151, 79, 231, 171, 95, 239, 191, 119, 247, 211, 139, 167, 123, 59, 183, 155, 55,
            199, 195, 55, 231, 227, 87, 127, 191, 255, 171, 231, 255, 215, 255, 255, 103, 0, 0, 139, 0, 0, 179, 0, 0, 215,
            0, 0, 255, 0, 0, 255, 243, 147, 255, 247, 199, 255, 255, 255, 159, 91, 83
        };

        static const auto palette = make_palette(palette_bytes);
        return palette;
    }
};

struct gamedef_h2_t : public gamedef_q1_like_t<GAME_HEXEN_II>
{
    gamedef_h2_t() : gamedef_q1_like_t("DATA1") { }

    const std::initializer_list<aabb3d> &get_hull_sizes() const
    {
        static std::initializer_list<aabb3d> hulls = {{{0, 0, 0}, {0, 0, 0}}, {{-16, -16, -32}, {16, 16, 24}},
            {{-24, -24, -20}, {24, 24, 20}}, {{-16, -16, -16}, {16, 16, 12}},
            {{-8, -8, -8}, {8, 8, 8}}, // {{-40, -40, -42}, {40, 40, 42}} = original game
            {{-28, -28, -40}, {28, 28, 40}}};

        return hulls;
    }

    const std::vector<qvec3b> &get_default_palette() const
    {
        static constexpr std::initializer_list<uint8_t> palette_bytes
        {
            0, 0, 0, 0, 0, 0, 8, 8, 8, 16, 16, 16, 24, 24, 24, 32, 32, 32, 40, 40, 40, 48, 48, 48, 56, 56, 56, 64, 64, 64, 72,
            72, 72, 80, 80, 80, 84, 84, 84, 88, 88, 88, 96, 96, 96, 104, 104, 104, 112, 112, 112, 120, 120, 120, 128, 128,
            128, 136, 136, 136, 148, 148, 148, 156, 156, 156, 168, 168, 168, 180, 180, 180, 184, 184, 184, 196, 196, 196,
            204, 204, 204, 212, 212, 212, 224, 224, 224, 232, 232, 232, 240, 240, 240, 252, 252, 252, 8, 8, 12, 16, 16, 20,
            24, 24, 28, 28, 32, 36, 36, 36, 44, 44, 44, 52, 48, 52, 60, 56, 56, 68, 64, 64, 72, 76, 76, 88, 92, 92, 104,
            108, 112, 128, 128, 132, 152, 152, 156, 176, 168, 172, 196, 188, 196, 220, 32, 24, 20, 40, 32, 28, 48, 36, 32,
            52, 44, 40, 60, 52, 44, 68, 56, 52, 76, 64, 56, 84, 72, 64, 92, 76, 72, 100, 84, 76, 108, 92, 84, 112, 96, 88,
            120, 104, 96, 128, 112, 100, 136, 116, 108, 144, 124, 112, 20, 24, 20, 28, 32, 28, 32, 36, 32, 40, 44, 40, 44,
            48, 44, 48, 56, 48, 56, 64, 56, 64, 68, 64, 68, 76, 68, 84, 92, 84, 104, 112, 104, 120, 128, 120, 140, 148, 136,
            156, 164, 152, 172, 180, 168, 188, 196, 184, 48, 32, 8, 60, 40, 8, 72, 48, 16, 84, 56, 20, 92, 64, 28, 100, 72,
            36, 108, 80, 44, 120, 92, 52, 136, 104, 60, 148, 116, 72, 160, 128, 84, 168, 136, 92, 180, 144, 100, 188, 152,
            108, 196, 160, 116, 204, 168, 124, 16, 20, 16, 20, 28, 20, 24, 32, 24, 28, 36, 28, 32, 44, 32, 36, 48, 36, 40,
            56, 40, 44, 60, 44, 48, 68, 48, 52, 76, 52, 60, 84, 60, 68, 92, 64, 76, 100, 72, 84, 108, 76, 92, 116, 84, 100,
            128, 92, 24, 12, 8, 32, 16, 8, 40, 20, 8, 52, 24, 12, 60, 28, 12, 68, 32, 12, 76, 36, 16, 84, 44, 20, 92, 48,
            24, 100, 56, 28, 112, 64, 32, 120, 72, 36, 128, 80, 44, 144, 92, 56, 168, 112, 72, 192, 132, 88, 24, 4, 4, 36,
            4, 4, 48, 0, 0, 60, 0, 0, 68, 0, 0, 80, 0, 0, 88, 0, 0, 100, 0, 0, 112, 0, 0, 132, 0, 0, 152, 0, 0, 172, 0, 0,
            192, 0, 0, 212, 0, 0, 232, 0, 0, 252, 0, 0, 16, 12, 32, 28, 20, 48, 32, 28, 56, 40, 36, 68, 52, 44, 80, 60, 56,
            92, 68, 64, 104, 80, 72, 116, 88, 84, 128, 100, 96, 140, 108, 108, 152, 120, 116, 164, 132, 132, 176, 144, 144,
            188, 156, 156, 200, 172, 172, 212, 36, 20, 4, 52, 24, 4, 68, 32, 4, 80, 40, 0, 100, 48, 4, 124, 60, 4, 140, 72,
            4, 156, 88, 8, 172, 100, 8, 188, 116, 12, 204, 128, 12, 220, 144, 16, 236, 160, 20, 252, 184, 56, 248, 200, 80,
            248, 220, 120, 20, 16, 4, 28, 24, 8, 36, 32, 8, 44, 40, 12, 52, 48, 16, 56, 56, 16, 64, 64, 20, 68, 72, 24, 72,
            80, 28, 80, 92, 32, 84, 104, 40, 88, 116, 44, 92, 128, 52, 92, 140, 52, 92, 148, 56, 96, 160, 64, 60, 16, 16,
            72, 24, 24, 84, 28, 28, 100, 36, 36, 112, 44, 44, 124, 52, 48, 140, 64, 56, 152, 76, 64, 44, 20, 8, 56, 28, 12,
            72, 32, 16, 84, 40, 20, 96, 44, 28, 112, 52, 32, 124, 56, 40, 140, 64, 48, 24, 20, 16, 36, 28, 20, 44, 36, 28,
            56, 44, 32, 64, 52, 36, 72, 60, 44, 80, 68, 48, 92, 76, 52, 100, 84, 60, 112, 92, 68, 120, 100, 72, 132, 112,
            80, 144, 120, 88, 152, 128, 96, 160, 136, 104, 168, 148, 112, 36, 24, 12, 44, 32, 16, 52, 40, 20, 60, 44, 20,
            72, 52, 24, 80, 60, 28, 88, 68, 28, 104, 76, 32, 148, 96, 56, 160, 108, 64, 172, 116, 72, 180, 124, 80, 192,
            132, 88, 204, 140, 92, 216, 156, 108, 60, 20, 92, 100, 36, 116, 168, 72, 164, 204, 108, 192, 4, 84, 4, 4, 132,
            4, 0, 180, 0, 0, 216, 0, 4, 4, 144, 16, 68, 204, 36, 132, 224, 88, 168, 232, 216, 4, 4, 244, 72, 0, 252, 128, 0,
            252, 172, 24, 252, 252, 252
        };
        
        static const auto palette = make_palette(palette_bytes);
        return palette;
    }
};

struct gamedef_hl_t : public gamedef_q1_like_t<GAME_HALF_LIFE>
{
    gamedef_hl_t() : gamedef_q1_like_t("VALVE") { has_rgb_lightmap = true; }

    const std::initializer_list<aabb3d> &get_hull_sizes() const
    {
        static std::initializer_list<aabb3d> hulls = {{{0, 0, 0}, {0, 0, 0}}, {{-16, -16, -36}, {16, 16, 36}},
            {{-32, -32, -32}, {32, 32, 32}}, {{-16, -16, -18}, {16, 16, 18}}};

        return hulls;
    }

    const std::vector<qvec3b> &get_default_palette() const
    {
        static const std::vector<qvec3b> palette;
        return palette;
    }
};

struct gamedef_q2_t : public gamedef_t
{
    gamedef_q2_t() : gamedef_t("BASEQ2")
    {
        this->id = GAME_QUAKE_II;
        has_rgb_lightmap = true;
        allow_contented_bmodels = true;
        max_entity_key = 256;
    }

    bool surf_is_lightmapped(const surfflags_t &flags) const
    {
        return !(flags.native & (Q2_SURF_WARP | Q2_SURF_SKY | Q2_SURF_NODRAW)); // mxd. +Q2_SURF_NODRAW
    }

    bool surf_is_subdivided(const surfflags_t &flags) const { return !(flags.native & (Q2_SURF_WARP | Q2_SURF_SKY)); }

    bool surfflags_are_valid(const surfflags_t &flags) const
    {
        // no rules in Quake II baby
        return true;
    }

    bool texinfo_is_hintskip(const surfflags_t &flags, const std::string &name) const
    {
        // any face in a hint brush that isn't HINT are treated as "hintskip", and discarded
        return !(flags.native & Q2_SURF_HINT);
    }

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
        return contents.native & (((Q2_LAST_VISIBLE_CONTENTS << 1) - 1) |
               (Q2_CONTENTS_PLAYERCLIP | Q2_CONTENTS_MONSTERCLIP | Q2_CONTENTS_ORIGIN | Q2_CONTENTS_TRANSLUCENT | Q2_CONTENTS_AREAPORTAL));
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
        } else {
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
    }

    contentflags_t create_extended_contents(const int32_t &cflags) const { return {0, cflags}; }

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
        if (contents.extended & CFLAGS_CONTENTS_MASK)
            return false;

        if (contents.native & Q2_CONTENTS_AREAPORTAL)
            return false; // HACK: needs to return false in order for LinkConvexFaces to assign Q2_CONTENTS_AREAPORTAL to the leaf

        return !(contents.native & ((Q2_LAST_VISIBLE_CONTENTS << 1) - 1));
    }

    bool contents_are_solid(const contentflags_t &contents) const
    {
        if (contents.extended & CFLAGS_CONTENTS_MASK)
            return false;
        
        return contents.native & Q2_CONTENTS_SOLID;
    }

    bool contents_are_sky(const contentflags_t &contents) const { return false; }

    bool contents_are_liquid(const contentflags_t &contents) const
    {
        if (contents.extended & CFLAGS_CONTENTS_MASK)
            return false;
        
        if (contents.native & Q2_CONTENTS_AREAPORTAL)
            return true; // HACK: treat areaportal as a liquid for the purposes of the CSG code

        return contents.native & Q2_CONTENTS_LIQUID;
    }

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
        
        // can't see through solid
        if ((c0 | c1) & Q2_CONTENTS_SOLID)
            return false;

        if (!visible_contents(c0 ^ c1))
            return true;

        if ((c0 & Q2_CONTENTS_TRANSLUCENT) || contents0.is_detail())
            c0 = 0;
        if ((c1 & Q2_CONTENTS_TRANSLUCENT) || contents1.is_detail())
            c1 = 0;

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
    
    contentflags_t face_get_contents(const std::string &texname, const surfflags_t &flags, const contentflags_t &contents) const
    {
        // hints and skips are never detail, and have no content
        if (flags.native & Q2_SURF_HINT) {
            return { 0, CFLAGS_HINT };
        } else if (flags.native & Q2_SURF_SKIP) {
            return { 0, 0 };
        }

        contentflags_t surf_contents = contents;

        // if we don't have a declared content type, assume solid.
        if (!get_content_type(surf_contents)) {
            surf_contents.native |= Q2_CONTENTS_SOLID;
        }

        // if we have TRANS33 or TRANS66, we have to be marked as WINDOW,
        // so unset SOLID, give us WINDOW, and give us TRANSLUCENT
        if (flags.native & (Q2_SURF_TRANS33 | Q2_SURF_TRANS66)) {
            surf_contents.native |= Q2_CONTENTS_TRANSLUCENT;

            if (surf_contents.native & Q2_CONTENTS_SOLID) {
                surf_contents.native = (surf_contents.native & ~Q2_CONTENTS_SOLID) | Q2_CONTENTS_WINDOW;
            }
        }

        // translucent objects are automatically classified as detail
        if (surf_contents.native & Q2_CONTENTS_WINDOW) {
            surf_contents.extended |= CFLAGS_DETAIL_FENCE;
        } else if (surf_contents.native & (Q2_CONTENTS_MIST | Q2_CONTENTS_AUX)) {
            surf_contents.extended |= CFLAGS_DETAIL_ILLUSIONARY;
        // if we used the DETAIL contents flag, copy over DETAIL
        } else if (surf_contents.native & Q2_CONTENTS_DETAIL) {
            surf_contents.extended |= CFLAGS_DETAIL;
        }

        if (surf_contents.native & (Q2_CONTENTS_MONSTERCLIP | Q2_CONTENTS_PLAYERCLIP)) {
            surf_contents.extended |= CFLAGS_CLIP;
        }

        if (surf_contents.native & Q2_CONTENTS_ORIGIN) {
            surf_contents.extended |= CFLAGS_ORIGIN;
        }

        // FIXME: this is a bit of a hack, but this is because clip
        // and liquids and stuff are already handled *like* detail by
        // the compiler.
        if (surf_contents.extended & CFLAGS_DETAIL) {
            if (!(surf_contents.native & Q2_CONTENTS_SOLID)) {
                surf_contents.extended &= ~CFLAGS_DETAIL;
            }
        }

        return surf_contents;
    }

private:
    void discoverArchives(const fs::path &base) const
    {
        fs::directory_iterator it(base);

        // TODO: natsort
        std::set<std::string, case_insensitive_less> packs;

        for (auto &entry : it) {
            if (string_iequals(entry.path().extension().generic_string(), ".pak")) {
                packs.insert(entry.path().generic_string());
            }
        }

        for (auto &pak : packs) {
            fs::addArchive(pak);
        }
    }

public:
    void init_filesystem(const fs::path &source) const
    {
        constexpr const char *MAPS_FOLDER = "maps";

        // detect gamedir (mod directory path)
        fs::path gamedir;

        // expand canonicals, and fetch parent of source file
        if (auto paths = fs::splitArchivePath(source)) {
            // if the source is an archive, use the parent
            // of that folder as the mod directory
            // pak0.pak/maps/source.map -> C:/Quake/ID1
            gamedir = fs::canonical(paths.archive).parent_path();
        } else {
            // maps/source.map -> C:/Quake/ID1/maps
            // this is weak because the source may not exist yet
            gamedir = fs::weakly_canonical(source).parent_path();

            if (!string_iequals(gamedir.filename().generic_string(), "maps")) {
                FLogPrint("WARNING: '{}' is not directly inside '{}'. This may throw off automated path detection!\n", source, MAPS_FOLDER);
                return;
            }
            
            // C:/Quake/ID1/maps -> C:/Quake/ID1
            gamedir = gamedir.parent_path();
        }

        // C:/Quake/ID1 -> C:/Quake
        fs::path qdir = gamedir.parent_path();

        // Set base dir and make sure it exists
        fs::path basedir = qdir / default_base_dir;

        if (!exists(basedir)) {
            FLogPrint("WARNING: failed to find '{}' in '{}'\n", default_base_dir, qdir);
        } else if (!equivalent(gamedir, basedir)) {
            fs::addArchive(basedir);
            discoverArchives(basedir);
        }

        fs::addArchive(gamedir);
        discoverArchives(gamedir);
    }

    const std::vector<qvec3b> &get_default_palette() const
    {
        static constexpr std::initializer_list<uint8_t> palette_bytes
        {
            0, 0, 0, 15, 15, 15, 31, 31, 31, 47, 47, 47, 63, 63, 63, 75, 75, 75, 91, 91, 91, 107, 107, 107, 123, 123, 123, 139,
            139, 139, 155, 155, 155, 171, 171, 171, 187, 187, 187, 203, 203, 203, 219, 219, 219, 235, 235, 235, 99, 75, 35,
            91, 67, 31, 83, 63, 31, 79, 59, 27, 71, 55, 27, 63, 47, 23, 59, 43, 23, 51, 39, 19, 47, 35, 19, 43, 31, 19, 39,
            27, 15, 35, 23, 15, 27, 19, 11, 23, 15, 11, 19, 15, 7, 15, 11, 7, 95, 95, 111, 91, 91, 103, 91, 83, 95, 87, 79,
            91, 83, 75, 83, 79, 71, 75, 71, 63, 67, 63, 59, 59, 59, 55, 55, 51, 47, 47, 47, 43, 43, 39, 39, 39, 35, 35, 35,
            27, 27, 27, 23, 23, 23, 19, 19, 19, 143, 119, 83, 123, 99, 67, 115, 91, 59, 103, 79, 47, 207, 151, 75, 167, 123,
            59, 139, 103, 47, 111, 83, 39, 235, 159, 39, 203, 139, 35, 175, 119, 31, 147, 99, 27, 119, 79, 23, 91, 59, 15,
            63, 39, 11, 35, 23, 7, 167, 59, 43, 159, 47, 35, 151, 43, 27, 139, 39, 19, 127, 31, 15, 115, 23, 11, 103, 23, 7,
            87, 19, 0, 75, 15, 0, 67, 15, 0, 59, 15, 0, 51, 11, 0, 43, 11, 0, 35, 11, 0, 27, 7, 0, 19, 7, 0, 123, 95, 75,
            115, 87, 67, 107, 83, 63, 103, 79, 59, 95, 71, 55, 87, 67, 51, 83, 63, 47, 75, 55, 43, 67, 51, 39, 63, 47, 35,
            55, 39, 27, 47, 35, 23, 39, 27, 19, 31, 23, 15, 23, 15, 11, 15, 11, 7, 111, 59, 23, 95, 55, 23, 83, 47, 23, 67,
            43, 23, 55, 35, 19, 39, 27, 15, 27, 19, 11, 15, 11, 7, 179, 91, 79, 191, 123, 111, 203, 155, 147, 215, 187, 183,
            203, 215, 223, 179, 199, 211, 159, 183, 195, 135, 167, 183, 115, 151, 167, 91, 135, 155, 71, 119, 139, 47, 103,
            127, 23, 83, 111, 19, 75, 103, 15, 67, 91, 11, 63, 83, 7, 55, 75, 7, 47, 63, 7, 39, 51, 0, 31, 43, 0, 23, 31, 0,
            15, 19, 0, 7, 11, 0, 0, 0, 139, 87, 87, 131, 79, 79, 123, 71, 71, 115, 67, 67, 107, 59, 59, 99, 51, 51, 91, 47,
            47, 87, 43, 43, 75, 35, 35, 63, 31, 31, 51, 27, 27, 43, 19, 19, 31, 15, 15, 19, 11, 11, 11, 7, 7, 0, 0, 0, 151,
            159, 123, 143, 151, 115, 135, 139, 107, 127, 131, 99, 119, 123, 95, 115, 115, 87, 107, 107, 79, 99, 99, 71, 91,
            91, 67, 79, 79, 59, 67, 67, 51, 55, 55, 43, 47, 47, 35, 35, 35, 27, 23, 23, 19, 15, 15, 11, 159, 75, 63, 147,
            67, 55, 139, 59, 47, 127, 55, 39, 119, 47, 35, 107, 43, 27, 99, 35, 23, 87, 31, 19, 79, 27, 15, 67, 23, 11, 55,
            19, 11, 43, 15, 7, 31, 11, 7, 23, 7, 0, 11, 0, 0, 0, 0, 0, 119, 123, 207, 111, 115, 195, 103, 107, 183, 99, 99,
            167, 91, 91, 155, 83, 87, 143, 75, 79, 127, 71, 71, 115, 63, 63, 103, 55, 55, 87, 47, 47, 75, 39, 39, 63, 35,
            31, 47, 27, 23, 35, 19, 15, 23, 11, 7, 7, 155, 171, 123, 143, 159, 111, 135, 151, 99, 123, 139, 87, 115, 131,
            75, 103, 119, 67, 95, 111, 59, 87, 103, 51, 75, 91, 39, 63, 79, 27, 55, 67, 19, 47, 59, 11, 35, 47, 7, 27, 35,
            0, 19, 23, 0, 11, 15, 0, 0, 255, 0, 35, 231, 15, 63, 211, 27, 83, 187, 39, 95, 167, 47, 95, 143, 51, 95, 123,
            51, 255, 255, 255, 255, 255, 211, 255, 255, 167, 255, 255, 127, 255, 255, 83, 255, 255, 39, 255, 235, 31, 255,
            215, 23, 255, 191, 15, 255, 171, 7, 255, 147, 0, 239, 127, 0, 227, 107, 0, 211, 87, 0, 199, 71, 0, 183, 59, 0,
            171, 43, 0, 155, 31, 0, 143, 23, 0, 127, 15, 0, 115, 7, 0, 95, 0, 0, 71, 0, 0, 47, 0, 0, 27, 0, 0, 239, 0, 0,
            55, 55, 255, 255, 0, 0, 0, 0, 255, 43, 43, 35, 27, 27, 23, 19, 19, 15, 235, 151, 127, 195, 115, 83, 159, 87, 51,
            123, 63, 27, 235, 211, 199, 199, 171, 155, 167, 139, 119, 135, 107, 87, 159, 91, 83
        };
        static const auto palette = make_palette(palette_bytes);
        return palette;
    }
};

// Game definitions, used for the bsp versions below
static const gamedef_generic_t gamedef_generic;
static const gamedef_q1_like_t<GAME_QUAKE> gamedef_q1;
static const gamedef_h2_t gamedef_h2;
static const gamedef_hl_t gamedef_hl;
static const gamedef_q2_t gamedef_q2;

const bspversion_t bspver_generic{NO_VERSION, NO_VERSION, "mbsp", "generic BSP", {}, &gamedef_generic};
const bspversion_t bspver_q1{BSPVERSION, NO_VERSION, "bsp29", "Quake BSP", {
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
}, &gamedef_q1, &bspver_bsp2};
const bspversion_t bspver_bsp2{BSP2VERSION, NO_VERSION, "bsp2", "Quake BSP2", {
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
}, &gamedef_q1};
const bspversion_t bspver_bsp2rmq{
    BSP2RMQVERSION, NO_VERSION, "bsp2rmq", "Quake BSP2-RMQ", {
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
}, &gamedef_q1};
/* Hexen II doesn't use a separate version, but we can still use a separate tag/name for it */
const bspversion_t bspver_h2{
    BSPVERSION, NO_VERSION, "hexen2", "Hexen II BSP", {
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
}, &gamedef_h2, &bspver_h2bsp2};
const bspversion_t bspver_h2bsp2{BSP2VERSION, NO_VERSION, "hexen2bsp2", "Hexen II BSP2", {
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
}, &gamedef_h2};
const bspversion_t bspver_h2bsp2rmq{
    BSP2RMQVERSION, NO_VERSION, "hexen2bsp2rmq", "Hexen II BSP2-RMQ", {
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
}, &gamedef_h2};
const bspversion_t bspver_hl{BSPHLVERSION, NO_VERSION, "hl", "Half-Life BSP", bspver_q1.lumps, &gamedef_hl};
const bspversion_t bspver_q2{
    Q2_BSPIDENT, Q2_BSPVERSION, "q2bsp", "Quake II BSP", {
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
}, &gamedef_q2, &bspver_qbism};
const bspversion_t bspver_qbism{
    Q2_QBISMIDENT, Q2_BSPVERSION, "qbism", "Quake II Qbism BSP", {
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
}, &gamedef_q2};

bool surfflags_t::is_valid(const gamedef_t *game) const
{
    return game->surfflags_are_valid(*this);
}

bool contentflags_t::types_equal(const contentflags_t &other, const gamedef_t *game) const
{
    return (extended & CFLAGS_CONTENTS_MASK) == (other.extended & CFLAGS_CONTENTS_MASK) &&
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
inline void CopyArray(T &in, T &out)
{
    out = in;
}

// convert structured data if we're different types
template<typename T, typename F, typename = std::enable_if_t<!std::is_same_v<T, F>>>
inline void CopyArray(std::vector<F> &from, std::vector<T> &to)
{
    to.reserve(from.size());

    for (auto &v : from) {
        if constexpr (std::is_arithmetic_v<T> && std::is_arithmetic_v<F>)
            to.emplace_back(numeric_cast<T>(v));
        else
            to.emplace_back(v);
    }
}

// move structured data if the input and output
// are of the same type
template<typename T, typename F>
inline void CopyArray(F &in, T &out)
{
    out = in;
}

// convert structured data if we're different types
// with numeric casting for arrays
template<typename T, typename F, size_t N, typename = std::enable_if_t<!std::is_same_v<T, F>>>
inline void CopyArray(std::vector<std::array<F, N>> &from, std::vector<std::array<T, N>> &to)
{
    to.reserve(from.size());

    for (auto &v : from) {
        if constexpr (std::is_arithmetic_v<T> && std::is_arithmetic_v<F>)
            to.emplace_back(array_cast<std::array<T, N>>(v));
        else
            to.emplace_back(v);
    }
}

// Convert from a Q1-esque format to Generic
template<typename T>
inline void ConvertQ1BSPToGeneric(T &bsp, mbsp_t &mbsp)
{
    CopyArray(bsp.dentdata, mbsp.dentdata);
    CopyArray(bsp.dplanes, mbsp.dplanes);
    if (std::holds_alternative<miptexhl_lump>(bsp.dtex)) {
        CopyArray(std::get<miptexhl_lump>(bsp.dtex), mbsp.dtex);
    } else {
        CopyArray(std::get<miptexq1_lump>(bsp.dtex), mbsp.dtex);
    }
    CopyArray(bsp.dvertexes, mbsp.dvertexes);
    CopyArray(bsp.dvisdata, mbsp.dvis.bits);
    CopyArray(bsp.dnodes, mbsp.dnodes);
    CopyArray(bsp.texinfo, mbsp.texinfo);
    CopyArray(bsp.dfaces, mbsp.dfaces);
    CopyArray(bsp.dlightdata, mbsp.dlightdata);
    CopyArray(bsp.dclipnodes, mbsp.dclipnodes);
    CopyArray(bsp.dleafs, mbsp.dleafs);
    CopyArray(bsp.dmarksurfaces, mbsp.dleaffaces);
    CopyArray(bsp.dedges, mbsp.dedges);
    CopyArray(bsp.dsurfedges, mbsp.dsurfedges);
    if (std::holds_alternative<dmodelh2_vector>(bsp.dmodels)) {
        CopyArray(std::get<dmodelh2_vector>(bsp.dmodels), mbsp.dmodels);
    } else {
        CopyArray(std::get<dmodelq1_vector>(bsp.dmodels), mbsp.dmodels);
    }
}

// Convert from a Q2-esque format to Generic
template<typename T>
inline void ConvertQ2BSPToGeneric(T &bsp, mbsp_t &mbsp)
{
    CopyArray(bsp.dentdata, mbsp.dentdata);
    CopyArray(bsp.dplanes, mbsp.dplanes);
    CopyArray(bsp.dvertexes, mbsp.dvertexes);
    CopyArray(bsp.dvis, mbsp.dvis);
    CopyArray(bsp.dnodes, mbsp.dnodes);
    CopyArray(bsp.texinfo, mbsp.texinfo);
    CopyArray(bsp.dfaces, mbsp.dfaces);
    CopyArray(bsp.dlightdata, mbsp.dlightdata);
    CopyArray(bsp.dleafs, mbsp.dleafs);
    CopyArray(bsp.dleaffaces, mbsp.dleaffaces);
    CopyArray(bsp.dleafbrushes, mbsp.dleafbrushes);
    CopyArray(bsp.dedges, mbsp.dedges);
    CopyArray(bsp.dsurfedges, mbsp.dsurfedges);
    CopyArray(bsp.dmodels, mbsp.dmodels);
    CopyArray(bsp.dbrushes, mbsp.dbrushes);
    CopyArray(bsp.dbrushsides, mbsp.dbrushsides);
    CopyArray(bsp.dareas, mbsp.dareas);
    CopyArray(bsp.dareaportals, mbsp.dareaportals);
}

// Convert from a Q1-esque format to Generic
template<typename T>
inline T ConvertGenericToQ1BSP(mbsp_t &mbsp, const bspversion_t *to_version)
{
    T bsp{};

    // copy or convert data
    CopyArray(mbsp.dentdata, bsp.dentdata);
    CopyArray(mbsp.dplanes, bsp.dplanes);
    if (to_version->game->id == GAME_HALF_LIFE) {
        CopyArray(mbsp.dtex, bsp.dtex.template emplace<miptexhl_lump>());
    } else {
        CopyArray(mbsp.dtex, bsp.dtex.template emplace<miptexq1_lump>());
    }
    CopyArray(mbsp.dvertexes, bsp.dvertexes);
    CopyArray(mbsp.dvis.bits, bsp.dvisdata);
    CopyArray(mbsp.dnodes, bsp.dnodes);
    CopyArray(mbsp.texinfo, bsp.texinfo);
    CopyArray(mbsp.dfaces, bsp.dfaces);
    CopyArray(mbsp.dlightdata, bsp.dlightdata);
    CopyArray(mbsp.dclipnodes, bsp.dclipnodes);
    CopyArray(mbsp.dleafs, bsp.dleafs);
    CopyArray(mbsp.dleaffaces, bsp.dmarksurfaces);
    CopyArray(mbsp.dedges, bsp.dedges);
    CopyArray(mbsp.dsurfedges, bsp.dsurfedges);
    if (to_version->game->id == GAME_HEXEN_II) {
        CopyArray(mbsp.dmodels, bsp.dmodels.template emplace<dmodelh2_vector>());
    } else {
        CopyArray(mbsp.dmodels, bsp.dmodels.template emplace<dmodelq1_vector>());
    }

    return bsp;
}

// Convert from a Q2-esque format to Generic
template<typename T>
inline T ConvertGenericToQ2BSP(mbsp_t &mbsp, const bspversion_t *to_version)
{
    T bsp{};

    // copy or convert data
    CopyArray(mbsp.dentdata, bsp.dentdata);
    CopyArray(mbsp.dplanes, bsp.dplanes);
    CopyArray(mbsp.dvertexes, bsp.dvertexes);
    CopyArray(mbsp.dvis, bsp.dvis);
    CopyArray(mbsp.dnodes, bsp.dnodes);
    CopyArray(mbsp.texinfo, bsp.texinfo);
    CopyArray(mbsp.dfaces, bsp.dfaces);
    CopyArray(mbsp.dlightdata, bsp.dlightdata);
    CopyArray(mbsp.dleafs, bsp.dleafs);
    CopyArray(mbsp.dleaffaces, bsp.dleaffaces);
    CopyArray(mbsp.dleafbrushes, bsp.dleafbrushes);
    CopyArray(mbsp.dedges, bsp.dedges);
    CopyArray(mbsp.dsurfedges, bsp.dsurfedges);
    CopyArray(mbsp.dmodels, bsp.dmodels);
    CopyArray(mbsp.dbrushes, bsp.dbrushes);
    CopyArray(mbsp.dbrushsides, bsp.dbrushsides);
    CopyArray(mbsp.dareas, bsp.dareas);
    CopyArray(mbsp.dareaportals, bsp.dareaportals);

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
        mbsp_t mbsp{};

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

        try {
            if (to_version == &bspver_q1 || to_version == &bspver_h2 || to_version == &bspver_hl) {
                bspdata->bsp = ConvertGenericToQ1BSP<bsp29_t>(mbsp, to_version);
            } else if (to_version == &bspver_q2) {
                bspdata->bsp = ConvertGenericToQ2BSP<q2bsp_t>(mbsp, to_version);
            } else if (to_version == &bspver_qbism) {
                bspdata->bsp = ConvertGenericToQ2BSP<q2bsp_qbism_t>(mbsp, to_version);
            } else if (to_version == &bspver_bsp2rmq || to_version == &bspver_h2bsp2rmq) {
                bspdata->bsp = ConvertGenericToQ1BSP<bsp2rmq_t>(mbsp, to_version);
            } else if (to_version == &bspver_bsp2 || to_version == &bspver_h2bsp2) {
                bspdata->bsp = ConvertGenericToQ1BSP<bsp2_t>(mbsp, to_version);
            } else {
                return false;
            }
        }
        catch (std::overflow_error e) {
            LogPrint("LIMITS EXCEEDED ON {}\n", e.what());
            return false;
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
        Q_assert(version->lumps.size() > lump_num);
        const lumpspec_t &lumpspec = version->lumps.begin()[lump_num];
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
            buffer.resize(length = lump.filelen);
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
        Q_assert(version->lumps.size() > lump_num);
        const lumpspec_t &lumpspec = version->lumps.begin()[lump_num];
        const lump_t &lump = lumps[lump_num];

        Q_assert(lumpspec.size == 1);
        Q_assert(!buffer.size());

        buffer.resize(lump.filelen);

        if (!lump.filelen)
            return;

        s.seekg(lump.fileofs);

        s.read(reinterpret_cast<char *>(buffer.data()), lump.filelen);

        // the last byte is required to be '\0' which was added when the .bsp was
        // written. chop it off now, since we want the std::string to
        // be the logical string (we'll add the null terminator again when saving the .bsp)
        if (buffer[lump.filelen - 1] == 0) {
            buffer.resize(lump.filelen - 1);
        }
        // TODO: warn about bad .bsp if missing \0?
    }

    // read structured lump data from stream into struct
    template<typename T, typename = std::enable_if_t<std::is_member_function_pointer_v<decltype(&T::stream_read)>>>
    void read(size_t lump_num, T &buffer)
    {
        Q_assert(version->lumps.size() > lump_num);
        const lumpspec_t &lumpspec = version->lumps.begin()[lump_num];
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
        reader.read(LUMP_TEXTURES, bsp.dtex.template emplace<miptexhl_lump>());
    } else {
        reader.read(LUMP_TEXTURES, bsp.dtex.template emplace<miptexq1_lump>());
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
        reader.read(LUMP_MODELS, bsp.dmodels.template emplace<dmodelh2_vector>());
    } else {
        reader.read(LUMP_MODELS, bsp.dmodels.template emplace<dmodelq1_vector>());
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

    FLogPrint("'{}'\n", filename);

    /* load the file header */
    fs::data file_data = fs::load(filename);

    if (!file_data) {
        FError("Unable to load \"{}\"\n", filename);
    }

    filename = fs::resolveArchivePath(filename);

    memstream stream(file_data->data(), file_data->size());

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
        LogPrint("BSP is version {}\n", temp_version);
        Error("Sorry, this bsp version is not supported.");
    } else {
        // special case handling for Hexen II
        if (bspdata->version->game->id == GAME_QUAKE && isHexen2((dheader_t *)file_data->data())) {
            if (bspdata->version == &bspver_q1) {
                bspdata->version = &bspver_h2;
            } else if (bspdata->version == &bspver_bsp2) {
                bspdata->version = &bspver_h2bsp2;
            } else if (bspdata->version == &bspver_bsp2rmq) {
                bspdata->version = &bspver_h2bsp2rmq;
            }
        }

        LogPrint("BSP is version {}\n", *bspdata->version);
    }

    lump_reader reader{stream, bspdata->version, lumps};

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
    /*bspx header is positioned exactly+4align at the end of the last lump position (regardless of order)*/
    for (i = 0, bspxofs = 0; i < lumps.size(); i++) {
        if (bspxofs < lumps[i].fileofs + lumps[i].filelen)
            bspxofs = lumps[i].fileofs + lumps[i].filelen;
    }

    bspxofs = (bspxofs + 3) & ~3;
    /*okay, so that's where it *should* be if it exists */
    if (bspxofs + sizeof(*bspx) <= file_data->size()) {
        int xlumps;
        const bspx_lump_t *xlump;
        bspx = (const bspx_header_t *)((const uint8_t *)file_data->data() + bspxofs);
        xlump = (const bspx_lump_t *)(bspx + 1);
        xlumps = LittleLong(bspx->numlumps);
        if (!memcmp(&bspx->id, "BSPX", 4) && xlumps >= 0 && bspxofs + sizeof(*bspx) + sizeof(*xlump) * xlumps <= file_data->size()) {
            /*header seems valid so far. just add the lumps as we normally would if we were generating them, ensuring
             * that they get written out anew*/
            while (xlumps-- > 0) {
                uint32_t ofs = LittleLong(xlump[xlumps].fileofs);
                uint32_t len = LittleLong(xlump[xlumps].filelen);
                uint8_t *lumpdata = new uint8_t[len];
                memcpy(lumpdata, (const uint8_t *)file_data->data() + ofs, len);
                bspdata->bspx.transfer(xlump[xlumps].lumpname.data(), lumpdata, len);
            }
        } else {
            if (memcmp(&bspx->id, "BSPX", 4))
                printf("invalid bspx header\n");
        }
    }
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

private:
    // write structured lump data from vector
    template<typename T>
    inline void write_lump(size_t lump_num, const std::vector<T> &data)
    {
        static constexpr char pad[4]{};
        Q_assert(version->lumps.size() > lump_num);
        const lumpspec_t &lumpspec = version->lumps.begin()[lump_num];
        lump_t *lumps;

        if (version->version != NO_VERSION) {
            lumps = q2header.lumps.data();
        } else {
            lumps = q1header.lumps.data();
        }

        lump_t &lump = lumps[lump_num];

        lump.fileofs = stream.tellp();

        for (auto &v : data)
            stream <= v;

        auto written = static_cast<int32_t>(stream.tellp()) - lump.fileofs;

        if (sizeof(T) == 1 || lumpspec.size > 1)
            Q_assert(written == (lumpspec.size * data.size()));

        lump.filelen = written;

        if (written % 4)
            stream.write(pad, 4 - (written % 4));
    }

    // this is only here to satisfy std::visit
    constexpr void write_lump(size_t, const std::monostate &) { }

    // write structured string data
    inline void write_lump(size_t lump_num, const std::string &data)
    {
        Q_assert(version->lumps.size() > lump_num);
        static constexpr char pad[4]{};
        const lumpspec_t &lumpspec = version->lumps.begin()[lump_num];
        lump_t *lumps;

        Q_assert(lumpspec.size == 1);

        if (version->version != NO_VERSION) {
            lumps = q2header.lumps.data();
        } else {
            lumps = q1header.lumps.data();
        }

        lump_t &lump = lumps[lump_num];

        lump.fileofs = stream.tellp();

        stream.write(data.c_str(), data.size() + 1); // null terminator

        auto written = static_cast<int32_t>(stream.tellp()) - lump.fileofs;

        Q_assert(written == data.size() + 1);

        lump.filelen = written;

        if (written % 4)
            stream.write(pad, 4 - (written % 4));
    }

    // write structured lump data
    template<typename T, typename = std::enable_if_t<std::is_member_function_pointer_v<decltype(&T::stream_write)>>>
    inline void write_lump(size_t lump_num, const T &data)
    {
        static constexpr char pad[4]{};
        const lumpspec_t &lumpspec = version->lumps.begin()[lump_num];
        lump_t *lumps;

        Q_assert(lumpspec.size == 1);

        if (version->version != NO_VERSION) {
            lumps = q2header.lumps.data();
        } else {
            lumps = q1header.lumps.data();
        }

        lump_t &lump = lumps[lump_num];

        lump.fileofs = stream.tellp();

        data.stream_write(stream);

        auto written = static_cast<int32_t>(stream.tellp()) - lump.fileofs;

        lump.filelen = written;

        if (written % 4)
            stream.write(pad, 4 - (written % 4));
    }

public:
    inline void write_bsp(const mbsp_t &) { FError("Can't write generic BSP"); }
    inline void write_bsp(const std::monostate &) { FError("No BSP to write"); }
    
    template<typename T, typename std::enable_if_t<std::is_base_of_v<q1bsp_tag_t, T>, int> = 0>
    inline void write_bsp(const T &bsp)
    {
        write_lump(LUMP_PLANES, bsp.dplanes);
        write_lump(LUMP_LEAFS, bsp.dleafs);
        write_lump(LUMP_VERTEXES, bsp.dvertexes);
        write_lump(LUMP_NODES, bsp.dnodes);
        write_lump(LUMP_TEXINFO, bsp.texinfo);
        write_lump(LUMP_FACES, bsp.dfaces);
        write_lump(LUMP_CLIPNODES, bsp.dclipnodes);
        write_lump(LUMP_MARKSURFACES, bsp.dmarksurfaces);
        write_lump(LUMP_SURFEDGES, bsp.dsurfedges);
        write_lump(LUMP_EDGES, bsp.dedges);
        std::visit([this](auto&& arg) { this->write_lump(LUMP_MODELS, arg); }, bsp.dmodels);

        write_lump(LUMP_LIGHTING, bsp.dlightdata);
        write_lump(LUMP_VISIBILITY, bsp.dvisdata);
        write_lump(LUMP_ENTITIES, bsp.dentdata);
        std::visit([this](auto&& arg) { this->write_lump(LUMP_TEXTURES, arg); }, bsp.dtex);
    }

    template<typename T, typename std::enable_if_t<std::is_base_of_v<q2bsp_tag_t, T>, int> = 0>
    inline void write_bsp(const T &bsp)
    {
        write_lump(Q2_LUMP_PLANES, bsp.dplanes);
        write_lump(Q2_LUMP_LEAFS, bsp.dleafs);
        write_lump(Q2_LUMP_VERTEXES, bsp.dvertexes);
        write_lump(Q2_LUMP_NODES, bsp.dnodes);
        write_lump(Q2_LUMP_TEXINFO, bsp.texinfo);
        write_lump(Q2_LUMP_FACES, bsp.dfaces);
        write_lump(Q2_LUMP_LEAFFACES, bsp.dleaffaces);
        write_lump(Q2_LUMP_SURFEDGES, bsp.dsurfedges);
        write_lump(Q2_LUMP_EDGES, bsp.dedges);
        write_lump(Q2_LUMP_MODELS, bsp.dmodels);
        write_lump(Q2_LUMP_LEAFBRUSHES, bsp.dleafbrushes);
        write_lump(Q2_LUMP_BRUSHES, bsp.dbrushes);
        write_lump(Q2_LUMP_BRUSHSIDES, bsp.dbrushsides);
        write_lump(Q2_LUMP_AREAS, bsp.dareas);
        write_lump(Q2_LUMP_AREAPORTALS, bsp.dareaportals);

        write_lump(Q2_LUMP_LIGHTING, bsp.dlightdata);
        write_lump(Q2_LUMP_VISIBILITY, bsp.dvis);
        write_lump(Q2_LUMP_ENTITIES, bsp.dentdata);
    }

    inline void write_bspx(const bspdata_t &bspdata)
    {
        if (!bspdata.bspx.entries.size())
            return;

        if (stream.tellp() & 3)
            FError("BSPX header is misaligned");

        stream <= bspx_header_t(bspdata.bspx.entries.size());

        auto bspxheader = stream.tellp();

        // write dummy lump headers
        for ([[maybe_unused]] auto &_ : bspdata.bspx.entries) {
            stream <= bspx_lump_t{};
        }

        std::vector<bspx_lump_t> xlumps;
        xlumps.reserve(bspdata.bspx.entries.size());

        for (auto &x : bspdata.bspx.entries) {
            static constexpr char pad[4]{};

            bspx_lump_t &lump = xlumps.emplace_back();
            lump.filelen = x.second.lumpsize;
            lump.fileofs = stream.tellp();
            memcpy(lump.lumpname.data(), x.first.c_str(), std::min(x.first.size(), lump.lumpname.size() - 1));

            stream.write(reinterpret_cast<const char *>(x.second.lumpdata.get()), x.second.lumpsize);

            if (x.second.lumpsize % 4)
                stream.write(pad, 4 - (x.second.lumpsize % 4));
        }

        stream.seekp(bspxheader);

        for (auto &lump : xlumps)
            stream <= lump;
    }
};

/*
 * =============
 * WriteBSPFile
 * Swaps the bsp file in place, so it should not be referenced again
 * =============
 */
void WriteBSPFile(const std::filesystem::path &filename, bspdata_t *bspdata)
{
    bspfile_t bspfile{};

    bspfile.version = bspdata->version;

    // headers are union'd, so this sets both
    bspfile.q2header.ident = bspfile.version->ident;

    if (bspfile.version->version != NO_VERSION) {
        bspfile.q2header.version = bspfile.version->version;
    }

    LogPrint("Writing {} as BSP version {}\n", filename, *bspdata->version);
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

    std::visit([&bspfile](auto&& arg) { bspfile.write_bsp(arg); }, bspdata->bsp);

    /*BSPX lumps are at a 4-byte alignment after the last of any official lump*/
    bspfile.write_bspx(*bspdata);

    bspfile.stream.seekp(0);

    // write the real header
    if (bspfile.version->version != NO_VERSION) {
        bspfile.stream <= bspfile.q2header;
    } else {
        bspfile.stream <= bspfile.q1header;
    }
}

/* ========================================================================= */

inline void PrintLumpSize(const lumpspec_t &lump, size_t count)
{
    LogPrint("{:7} {:<12} {:10}\n", count, lump.name, count * lump.size);
}

template<typename T>
inline void PrintQ1BSPLumps(const std::initializer_list<lumpspec_t> &lumpspec, const T &bsp)
{
    if (std::holds_alternative<dmodelh2_vector>(bsp.dmodels))
        LogPrint("{:7} {:<12}\n", std::get<dmodelh2_vector>(bsp.dmodels).size(), "models");
    else
        LogPrint("{:7} {:<12}\n", std::get<dmodelq1_vector>(bsp.dmodels).size(), "models");

    PrintLumpSize(lumpspec.begin()[LUMP_PLANES], bsp.dplanes.size());
    PrintLumpSize(lumpspec.begin()[LUMP_VERTEXES], bsp.dvertexes.size());
    PrintLumpSize(lumpspec.begin()[LUMP_NODES], bsp.dnodes.size());
    PrintLumpSize(lumpspec.begin()[LUMP_TEXINFO], bsp.texinfo.size());
    PrintLumpSize(lumpspec.begin()[LUMP_FACES], bsp.dfaces.size());
    PrintLumpSize(lumpspec.begin()[LUMP_CLIPNODES], bsp.dclipnodes.size());
    PrintLumpSize(lumpspec.begin()[LUMP_LEAFS], bsp.dleafs.size());
    PrintLumpSize(lumpspec.begin()[LUMP_MARKSURFACES], bsp.dmarksurfaces.size());
    PrintLumpSize(lumpspec.begin()[LUMP_EDGES], bsp.dedges.size());
    PrintLumpSize(lumpspec.begin()[LUMP_SURFEDGES], bsp.dsurfedges.size());

    if (std::holds_alternative<miptexhl_lump>(bsp.dtex))
        LogPrint("{:7} {:<12} {:10}\n", "", "textures", std::get<miptexhl_lump>(bsp.dtex).textures.size());
    else
        LogPrint("{:7} {:<12} {:10}\n", "", "textures", std::get<miptexq1_lump>(bsp.dtex).textures.size());
    LogPrint("{:7} {:<12} {:10}\n", "", "lightdata", bsp.dlightdata.size());
    LogPrint("{:7} {:<12} {:10}\n", "", "visdata", bsp.dvisdata.size());
    LogPrint("{:7} {:<12} {:10}\n", "", "entdata", bsp.dentdata.size() + 1); // include the null terminator
}

template<typename T>
inline void PrintQ2BSPLumps(const std::initializer_list<lumpspec_t> &lumpspec, const T &bsp)
{
    LogPrint("{:7} {:<12}\n", bsp.dmodels.size(), "models");

    PrintLumpSize(lumpspec.begin()[Q2_LUMP_PLANES], bsp.dplanes.size());
    PrintLumpSize(lumpspec.begin()[Q2_LUMP_VERTEXES], bsp.dvertexes.size());
    PrintLumpSize(lumpspec.begin()[Q2_LUMP_NODES], bsp.dnodes.size());
    PrintLumpSize(lumpspec.begin()[Q2_LUMP_TEXINFO], bsp.texinfo.size());
    PrintLumpSize(lumpspec.begin()[Q2_LUMP_FACES], bsp.dfaces.size());
    PrintLumpSize(lumpspec.begin()[Q2_LUMP_LEAFS], bsp.dleafs.size());
    PrintLumpSize(lumpspec.begin()[Q2_LUMP_LEAFFACES], bsp.dleaffaces.size());
    PrintLumpSize(lumpspec.begin()[Q2_LUMP_LEAFBRUSHES], bsp.dleafbrushes.size());
    PrintLumpSize(lumpspec.begin()[Q2_LUMP_EDGES], bsp.dedges.size());
    PrintLumpSize(lumpspec.begin()[Q2_LUMP_SURFEDGES], bsp.dsurfedges.size());
    PrintLumpSize(lumpspec.begin()[Q2_LUMP_BRUSHES], bsp.dbrushes.size());
    PrintLumpSize(lumpspec.begin()[Q2_LUMP_BRUSHSIDES], bsp.dbrushsides.size());
    PrintLumpSize(lumpspec.begin()[Q2_LUMP_AREAS], bsp.dareas.size());
    PrintLumpSize(lumpspec.begin()[Q2_LUMP_AREAPORTALS], bsp.dareaportals.size());

    LogPrint("{:7} {:<12} {:10}\n", "", "lightdata", bsp.dlightdata.size());
    LogPrint("{:7} {:<12} {:10}\n", "", "visdata", bsp.dvis.bits.size());
    LogPrint("{:7} {:<12} {:10}\n", "", "entdata", bsp.dentdata.size() + 1); // include the null terminator
}

/*
 * =============
 * PrintBSPFileSizes
 * Dumps info about the bsp data
 * =============
 */
void PrintBSPFileSizes(const bspdata_t *bspdata)
{
    const auto &lumpspec = bspdata->version->lumps;

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
        Error("Unsupported BSP version: {}", *bspdata->version);
    }

    for (auto &x : bspdata->bspx.entries) {
        LogPrint("{:7} {:<12} {:10}\n", "BSPX", x.first, x.second.lumpsize);
    }
}
