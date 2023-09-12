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
#include <common/imglib.hh>
#include <common/log.hh>
#include <common/settings.hh>
#include <common/numeric_cast.hh>

#include <cstdint>
#include <limits.h>

#include <fmt/core.h>

#include <atomic>
#include <mutex>

void lump_t::stream_write(std::ostream &s) const
{
    s <= std::tie(fileofs, filelen);
}

void lump_t::stream_read(std::istream &s)
{
    s >= std::tie(fileofs, filelen);
}

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

template<gameid_t ID>
struct gamedef_q1_like_t : public gamedef_t
{
private:
    // extra data for contentflags_t for Quake-like
    // todo: remove this and contentflags_t::native, and just use q1_contentflags_bits
    struct q1_contentflags_data
    {
        // extended content types. can be combined with native content types
        // (e.g. a fence, or mist dipping into water needs to have
        // both water and is_fence/is_mist)
        bool is_origin = false;
        bool is_clip = false;
        bool is_wall = false;
        bool is_fence = false;
        bool is_mist = false;

        // can be combined with any content type including native ones
        bool is_detail = false;

        constexpr bool operator==(const q1_contentflags_data &other) const
        {
            return is_origin == other.is_origin && is_clip == other.is_clip && is_wall == other.is_wall &&
                   is_fence == other.is_fence && is_mist == other.is_mist && is_detail == other.is_detail;
        }

        constexpr bool operator!=(const q1_contentflags_data &other) const { return !(*this == other); }

        constexpr explicit operator bool() const
        {
            return is_origin || is_clip || is_wall || is_fence || is_mist || is_detail;
        }
    };

    // returns a blank entry if the given contents don't have
    // any game data
    inline const q1_contentflags_data &get_data(const contentflags_t &contents) const
    {
        static constexpr q1_contentflags_data blank_data;

        if (!contents.game_data.has_value()) {
            return blank_data;
        }

        return std::any_cast<const q1_contentflags_data &>(contents.game_data);
    }

    // representation of q1 native contents and compiler extended contents, as well as flags, as bit flags
    // todo: this should be the only state inside a contentflags_t in q1 mode.
    struct q1_contentflags_bits
    {
        using bitset_t = std::bitset<14>;

        // visible contents
        bool solid = false;
        bool sky = false;
        bool wall = false; // compiler-internal
        bool fence = false; // compiler-internal
        bool lava = false;
        bool slime = false;
        bool water = false;
        bool mist = false; // compiler-internal

        // non-visible contents
        bool origin = false;
        bool clip = false;
        bool illusionary_visblocker = false;

        // content flags
        bool detail = false;
        bool mirror_inside = false;
        bool suppress_clipping_same_type = false;

        constexpr size_t last_visible_contents() const { return 7; }
        constexpr bitset_t bitset() const
        {
            bitset_t result;
            result[0] = solid;
            result[1] = sky;
            result[2] = wall;
            result[3] = fence;
            result[4] = lava;
            result[5] = slime;
            result[6] = water;
            result[7] = mist;
            result[8] = origin;
            result[9] = clip;
            result[10] = illusionary_visblocker;
            result[11] = detail;
            result[12] = mirror_inside;
            result[13] = suppress_clipping_same_type;
            return result;
        }

        q1_contentflags_bits() = default;
        explicit q1_contentflags_bits(const bitset_t &bitset)
            : solid(bitset[0]),
              sky(bitset[1]),
              wall(bitset[2]),
              fence(bitset[3]),
              lava(bitset[4]),
              slime(bitset[5]),
              water(bitset[6]),
              mist(bitset[7]),
              origin(bitset[8]),
              clip(bitset[9]),
              illusionary_visblocker(bitset[10]),
              detail(bitset[11]),
              mirror_inside(bitset[12]),
              suppress_clipping_same_type(bitset[13])
        {
        }

        static constexpr const char *bitflag_names[] = {"SOLID", "SKY", "WALL", "FENCE", "LAVA", "SLIME", "WATER",
            "MIST", "ORIGIN", "CLIP", "ILLUSIONARY_VISBLOCKER", "DETAIL", "MIRROR_INSIDE",
            "SUPPRESS_CLIPPING_SAME_TYPE"};

        constexpr bool operator[](size_t index) const
        {
            switch (index) {
                case 0: return solid;
                case 1: return sky;
                case 2: return wall;
                case 3: return fence;
                case 4: return lava;
                case 5: return slime;
                case 6: return water;
                case 7: return mist;
                case 8: return origin;
                case 9: return clip;
                case 10: return illusionary_visblocker;
                case 11: return detail;
                case 12: return mirror_inside;
                case 13: return suppress_clipping_same_type;
                default: throw std::out_of_range("index");
            }
        }

        constexpr bool &operator[](size_t index)
        {
            switch (index) {
                case 0: return solid;
                case 1: return sky;
                case 2: return wall;
                case 3: return fence;
                case 4: return lava;
                case 5: return slime;
                case 6: return water;
                case 7: return mist;
                case 8: return origin;
                case 9: return clip;
                case 10: return illusionary_visblocker;
                case 11: return detail;
                case 12: return mirror_inside;
                case 13: return suppress_clipping_same_type;
                default: throw std::out_of_range("index");
            }
        }

        constexpr q1_contentflags_bits operator|(const q1_contentflags_bits &other) const
        {
            return q1_contentflags_bits(bitset() | other.bitset());
        }

        constexpr q1_contentflags_bits operator^(const q1_contentflags_bits &other) const
        {
            return q1_contentflags_bits(bitset() ^ other.bitset());
        }

        constexpr bool operator==(const q1_contentflags_bits &other) const { return bitset() == other.bitset(); }

        constexpr bool operator!=(const q1_contentflags_bits &other) const { return !(*this == other); }

        constexpr int32_t visible_contents_index() const
        {
            for (size_t i = 0; i <= last_visible_contents(); ++i) {
                if ((*this)[i]) {
                    return static_cast<int32_t>(i);
                }
            }

            return -1;
        }

        constexpr q1_contentflags_bits visible_contents() const
        {
            q1_contentflags_bits result;

            int32_t index = visible_contents_index();
            if (index != -1) {
                result[index] = true;
            }

            return result;
        }

        constexpr bool all_empty() const
        {
            q1_contentflags_bits empty_test;
            return (*this) == empty_test;
        }

        /**
         * Contents (excluding non-contents flags) are all unset
         */
        constexpr bool contents_empty() const
        {
            q1_contentflags_bits this_test = *this;

            // clear flags
            this_test.detail = false;
            this_test.mirror_inside = false;
            this_test.suppress_clipping_same_type = false;

            q1_contentflags_bits empty_test;
            return this_test == empty_test;
        }
    };

    inline q1_contentflags_bits contentflags_to_bits(const contentflags_t &contents) const
    {
        q1_contentflags_bits result;

        // set bit for native contents
        switch (contents.native) {
            case CONTENTS_SOLID: result.solid = true; break;
            case CONTENTS_WATER: result.water = true; break;
            case CONTENTS_SLIME: result.slime = true; break;
            case CONTENTS_LAVA: result.lava = true; break;
            case CONTENTS_SKY: result.sky = true; break;
        }

        // copy in extra flags
        auto &data = get_data(contents);
        result.origin = data.is_origin;
        result.clip = data.is_clip;
        result.wall = data.is_wall;
        result.fence = data.is_fence;
        result.mist = data.is_mist;
        result.detail = data.is_detail;

        result.illusionary_visblocker = contents.illusionary_visblocker;
        result.mirror_inside = contents.mirror_inside.value_or(false);
        result.suppress_clipping_same_type = !contents.clips_same_type.value_or(true);

        return result;
    }

    inline contentflags_t contentflags_from_bits(const q1_contentflags_bits &bits) const
    {
        contentflags_t result;

        // set native contents
        if (bits.solid) {
            result.native = CONTENTS_SOLID;
        } else if (bits.sky) {
            result.native = CONTENTS_SKY;
        } else if (bits.water) {
            result.native = CONTENTS_WATER;
        } else if (bits.slime) {
            result.native = CONTENTS_SLIME;
        } else if (bits.lava) {
            result.native = CONTENTS_LAVA;
        } else {
            result.native = CONTENTS_EMPTY;
        }

        // copy in extra flags
        q1_contentflags_data data;
        data.is_origin = bits.origin;
        data.is_clip = bits.clip;
        data.is_wall = bits.wall;
        data.is_fence = bits.fence;
        data.is_mist = bits.mist;
        data.is_detail = bits.detail;

        if (data) {
            result.game_data = data;
        }

        result.illusionary_visblocker = bits.illusionary_visblocker;
        result.mirror_inside = bits.mirror_inside;
        result.clips_same_type = !bits.suppress_clipping_same_type;

        return result;
    }

public:
    explicit gamedef_q1_like_t(const char *friendly_name = "quake", const char *base_dir = "ID1")
        : gamedef_t(friendly_name, base_dir)
    {
        this->id = ID;
    }

    bool surf_is_lightmapped(
        const surfflags_t &flags, const char *texname, bool light_nodraw, bool lightgrid_enabled) const override
    {
        /* don't save lightmaps for "trigger" texture */
        if (!Q_strcasecmp(texname, "trigger"))
            return false;

        /* don't save lightmaps for "skip" texture */
        if (!Q_strcasecmp(texname, "skip"))
            return false;

        return !(flags.native & TEX_SPECIAL);
    }

    bool surf_is_emissive(const surfflags_t &flags, const char *texname) const override
    {
        /* don't save lightmaps for "trigger" texture */
        if (!Q_strcasecmp(texname, "trigger"))
            return false;

        return true;
    }

    bool surf_is_subdivided(const surfflags_t &flags) const override { return !(flags.native & TEX_SPECIAL); }

    bool surfflags_are_valid(const surfflags_t &flags) const override
    {
        // Q1 only supports TEX_SPECIAL
        return (flags.native & ~TEX_SPECIAL) == 0;
    }

    bool surfflags_may_phong(const surfflags_t &a, const surfflags_t &b) const override
    {
        return (a.native & TEX_SPECIAL) == (b.native & TEX_SPECIAL);
    }

    int32_t surfflags_from_string(const std::string_view &str) const override
    {
        if (string_iequals(str, "special")) {
            return TEX_SPECIAL;
        }

        return 0;
    }

    bool texinfo_is_hintskip(const surfflags_t &flags, const std::string &name) const override
    {
        // anything texname other than "hint" in a hint brush is treated as "hintskip", and discarded
        return !string_iequals(name, "hint");
    }

    contentflags_t cluster_contents(const contentflags_t &contents0, const contentflags_t &contents1) const override
    {
        const auto bits0 = contentflags_to_bits(contents0);
        const auto bits1 = contentflags_to_bits(contents1);

        auto combined = bits0 | bits1;

        // a cluster may include some solid detail areas, but
        // still be seen into
        if (!bits0.solid || !bits1.solid) {
            combined.solid = false;
        }

        return contentflags_from_bits(combined);
    }

    contentflags_t create_empty_contents() const override
    {
        q1_contentflags_bits result;
        return contentflags_from_bits(result);
    }

    contentflags_t create_solid_contents() const override
    {
        q1_contentflags_bits result;
        result.solid = true;
        return contentflags_from_bits(result);
    }

    contentflags_t create_detail_illusionary_contents(const contentflags_t &original) const override
    {
        q1_contentflags_bits result;
        result.mist = true;
        result.detail = true;
        return contentflags_from_bits(result);
    }

    contentflags_t create_detail_fence_contents(const contentflags_t &original) const override
    {
        q1_contentflags_bits result;
        result.fence = true;
        result.detail = true;
        return contentflags_from_bits(result);
    }

    contentflags_t create_detail_wall_contents(const contentflags_t &original) const override
    {
        q1_contentflags_bits result;
        result.wall = true;
        result.detail = true;
        return contentflags_from_bits(result);
    }

    contentflags_t create_detail_solid_contents(const contentflags_t &original) const override
    {
        q1_contentflags_bits result;
        result.solid = true;
        result.detail = true;
        return contentflags_from_bits(result);
    }

    contentflags_t clear_detail(const contentflags_t &original) const override
    {
        auto bits = contentflags_to_bits(original);
        bits.detail = false;
        return contentflags_from_bits(bits);
    }

    bool contents_are_type_equal(const contentflags_t &self, const contentflags_t &other) const override
    {
        // fixme-brushbsp: document what this is supposed to do, remove if unneeded?
        // is it checking for equality of visible content bits (in q2 terminology)?
        // same highest-priority visible content bit?

        return contentflags_to_bits(self) == contentflags_to_bits(other);
    }

    bool contents_are_equal(const contentflags_t &self, const contentflags_t &other) const override
    {
        // fixme-brushbsp: document what this is supposed to do, remove if unneeded?
        return contents_are_type_equal(self, other);
    }

    bool contents_are_any_detail(const contentflags_t &contents) const override
    {
        return contentflags_to_bits(contents).detail;
    }

    bool contents_are_detail_solid(const contentflags_t &contents) const override
    {
        // fixme-brushbsp: document whether this is an exclusive test (i.e. what does it return for water|solid|detail)
        const auto bits = contentflags_to_bits(contents);
        return bits.detail && bits.solid;
    }

    bool contents_are_detail_wall(const contentflags_t &contents) const override
    {
        // fixme-brushbsp: document whether this is an exclusive test (i.e. what does it return for water|fence|detail)
        const auto bits = contentflags_to_bits(contents);
        return bits.detail && bits.wall;
    }

    bool contents_are_detail_fence(const contentflags_t &contents) const override
    {
        // fixme-brushbsp: document whether this is an exclusive test (i.e. what does it return for water|fence|detail)
        const auto bits = contentflags_to_bits(contents);
        return bits.detail && bits.fence;
    }

    bool contents_are_detail_illusionary(const contentflags_t &contents) const override
    {
        // fixme-brushbsp: document whether this is an exclusive test (i.e. what does it return for water|mist|detail)
        const auto bits = contentflags_to_bits(contents);
        return bits.detail && bits.mist;
    }

    bool contents_are_origin(const contentflags_t &contents) const override
    {
        // fixme-brushbsp: document whether this is an exclusive test (i.e. what does it return for water|origin)
        const auto bits = contentflags_to_bits(contents);
        return bits.origin;
    }

    bool contents_are_clip(const contentflags_t &contents) const override
    {
        // fixme-brushbsp: document whether this is an exclusive test (i.e. what does it return for water|clip)
        const auto bits = contentflags_to_bits(contents);
        return bits.clip;
    }

    bool contents_clip_same_type(const contentflags_t &self, const contentflags_t &other) const override
    {
        return self.equals(this, other) && self.clips_same_type.value_or(true);
    }

    bool contents_are_empty(const contentflags_t &contents) const override
    {
        const auto bits = contentflags_to_bits(contents);
        return bits.contents_empty();
    }

    bool contents_are_any_solid(const contentflags_t &contents) const override
    {
        const auto bits = contentflags_to_bits(contents);
        return bits.solid;
    }

    // fixme-brushbsp: this is a leftover from q1 tools, and not really used in qbsp3, remove if possible
    bool contents_are_solid(const contentflags_t &contents) const override
    {
        const auto bits = contentflags_to_bits(contents);
        return bits.solid && !bits.detail;
    }

    bool contents_are_sky(const contentflags_t &contents) const override
    {
        const auto bits = contentflags_to_bits(contents);
        return bits.sky;
    }

    bool contents_are_liquid(const contentflags_t &contents) const override
    {
        const auto bits = contentflags_to_bits(contents);
        return bits.water || bits.lava || bits.slime;
    }

    bool contents_are_valid(const contentflags_t &contents, bool strict) const override
    {
        // fixme-brushbsp: document exactly what this is supposed to do
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

    int32_t contents_from_string(const std::string_view &str) const override
    {
        // Q1 doesn't get contents from files
        return 0;
    }

    bool portal_can_see_through(
        const contentflags_t &contents0, const contentflags_t &contents1, bool transwater, bool transsky) const override
    {
        auto bits_a = contentflags_to_bits(contents0);
        auto bits_b = contentflags_to_bits(contents1);

        // can't see through solid
        if (bits_a.solid || bits_b.solid) {
            return false;
        }

        bool a_translucent = transwater ? (bits_a.water || bits_a.slime || bits_a.lava) : false;
        bool b_translucent = transwater ? (bits_b.water || bits_b.slime || bits_b.lava) : false;

        if ((bits_a ^ bits_b).visible_contents().all_empty())
            return true;

        if (bits_a.detail || a_translucent)
            bits_a = q1_contentflags_bits();
        if (bits_b.detail || b_translucent)
            bits_b = q1_contentflags_bits();

        if ((bits_a ^ bits_b).all_empty())
            return true; // identical on both sides

        if ((bits_a ^ bits_b).visible_contents().all_empty())
            return true;
        return false;
    }

    bool contents_seals_map(const contentflags_t &contents) const override
    {
        return contents_are_solid(contents) || contents_are_sky(contents);
    }

    contentflags_t contents_remap_for_export(const contentflags_t &contents, remap_type_t type) const override
    {
        /*
         * This is for func_detail_wall.. we want to write a solid leaf that has faces,
         * because it may be possible to see inside (fence textures).
         *
         * Normally solid leafs are not written and just referenced as leaf 0.
         */
        if (contents_are_detail_fence(contents) || contents_are_detail_wall(contents)) {
            return create_solid_contents();
        }

        return contents;
    }

    contentflags_t combine_contents(const contentflags_t &a, const contentflags_t &b) const override
    {
        auto bits_a = contentflags_to_bits(a);
        auto bits_b = contentflags_to_bits(b);

        if (contents_are_solid(a) || contents_are_solid(b)) {
            return create_solid_contents();
        }
        if (contents_are_sky(a) || contents_are_sky(b)) {
            return contentflags_t{CONTENTS_SKY};
        }

        return contentflags_from_bits(bits_a | bits_b);
    }

    contentflags_t portal_visible_contents(const contentflags_t &a, const contentflags_t &b) const override
    {
        auto bits_a = contentflags_to_bits(a);
        auto bits_b = contentflags_to_bits(b);

        q1_contentflags_bits result;

        if (bits_a.suppress_clipping_same_type || bits_b.suppress_clipping_same_type) {
            result = bits_a | bits_b;
        } else {
            result = bits_a ^ bits_b;
        }

        auto strongest_contents_change = result.visible_contents();

        return contentflags_from_bits(strongest_contents_change);
    }

    bool portal_generates_face(const contentflags_t &portal_visible_contents, const contentflags_t &brushcontents,
        planeside_t brushside_side) const override
    {
        auto bits_portal = contentflags_to_bits(portal_visible_contents);
        auto bits_brush = contentflags_to_bits(brushcontents);

        // find the highest visible content bit set in portal
        int32_t index = bits_portal.visible_contents_index();
        if (index == -1) {
            return false;
        }

        // check if it's also set in the brush
        if (!bits_brush[index]) {
            return false;
        }

        if (brushside_side == SIDE_BACK) {
            // explicit override?
            if (brushcontents.mirror_inside) {
                return *brushcontents.mirror_inside;
            }

            return bits_brush.mirror_inside || bits_brush.water || bits_brush.slime || bits_brush.lava;
        }
        return true;
    }

    inline std::string get_contents_display(const q1_contentflags_bits &bits) const
    {
        if (bits.all_empty()) {
            return "EMPTY";
        }

        std::string s;

        for (int32_t i = 0; i < std::size(q1_contentflags_bits::bitflag_names); i++) {
            if (bits[i]) {
                if (!s.empty()) {
                    s += " | ";
                }

                s += q1_contentflags_bits::bitflag_names[i];
            }
        }

        return s;
    }

    std::string get_contents_display(const contentflags_t &contents) const override
    {
        const auto bits = contentflags_to_bits(contents);
        return get_contents_display(bits);
    }

    void contents_make_valid(contentflags_t &contents) const override
    {
        // fixme-brushbsp: probably wrong?
        // todo: anything smarter we can do here?
        // think this can't even happen in Q1 anyways
        if (!contents_are_valid(contents, false)) {
            contents = {CONTENTS_SOLID};
        }
    }

    const std::initializer_list<aabb3d> &get_hull_sizes() const override
    {
        static std::initializer_list<aabb3d> hulls = {
            {{0, 0, 0}, {0, 0, 0}}, {{-16, -16, -32}, {16, 16, 24}}, {{-32, -32, -64}, {32, 32, 24}}};

        return hulls;
    }

    contentflags_t face_get_contents(
        const std::string &texname, const surfflags_t &flags, const contentflags_t &) const override
    {
        // check for strong content indicators
        if (!Q_strcasecmp(texname.data(), "origin")) {
            q1_contentflags_bits result;
            result.origin = true;
            return contentflags_from_bits(result);
        } else if (!Q_strcasecmp(texname.data(), "hint") || !Q_strcasecmp(texname.data(), "hintskip")) {
            return create_empty_contents();
        } else if (!Q_strcasecmp(texname.data(), "clip")) {
            q1_contentflags_bits result;
            result.clip = true;
            return contentflags_from_bits(result);
        } else if (texname[0] == '*') {
            if (!Q_strncasecmp(texname.data() + 1, "lava", 4)) {
                q1_contentflags_bits result;
                result.lava = true;
                return contentflags_from_bits(result);
            } else if (!Q_strncasecmp(texname.data() + 1, "slime", 5)) {
                q1_contentflags_bits result;
                result.slime = true;
                return contentflags_from_bits(result);
            } else {
                q1_contentflags_bits result;
                result.water = true;
                return contentflags_from_bits(result);
            }
        } else if (!Q_strncasecmp(texname.data(), "sky", 3)) {
            q1_contentflags_bits result;
            result.sky = true;
            return contentflags_from_bits(result);
        }

        // and anything else is assumed to be a regular solid.
        return create_solid_contents();
    }

    void init_filesystem(const fs::path &map_or_bsp, const settings::common_settings &options) const override
    {
        img::clear();
        // Q1-like games don't care about the local
        // filesystem.
        // they do care about the palette though.
        fs::clear();

        for (auto &path : options.paths.values()) {
            fs::addArchive(path, true);
        }

        // certain features like '-add additional.map' search relative to the map we're compiling
        // so add the map directory to the search path
        auto map_or_bsp_dir = map_or_bsp.parent_path();
        if (!map_or_bsp_dir.empty()) {
            fs::addArchive(map_or_bsp_dir);
        }

        img::init_palette(this);
    }

    const std::vector<qvec3b> &get_default_palette() const override
    {
        static constexpr std::initializer_list<uint8_t> palette_bytes{0, 0, 0, 15, 15, 15, 31, 31, 31, 47, 47, 47, 63,
            63, 63, 75, 75, 75, 91, 91, 91, 107, 107, 107, 123, 123, 123, 139, 139, 139, 155, 155, 155, 171, 171, 171,
            187, 187, 187, 203, 203, 203, 219, 219, 219, 235, 235, 235, 15, 11, 7, 23, 15, 11, 31, 23, 11, 39, 27, 15,
            47, 35, 19, 55, 43, 23, 63, 47, 23, 75, 55, 27, 83, 59, 27, 91, 67, 31, 99, 75, 31, 107, 83, 31, 115, 87,
            31, 123, 95, 35, 131, 103, 35, 143, 111, 35, 11, 11, 15, 19, 19, 27, 27, 27, 39, 39, 39, 51, 47, 47, 63, 55,
            55, 75, 63, 63, 87, 71, 71, 103, 79, 79, 115, 91, 91, 127, 99, 99, 139, 107, 107, 151, 115, 115, 163, 123,
            123, 175, 131, 131, 187, 139, 139, 203, 0, 0, 0, 7, 7, 0, 11, 11, 0, 19, 19, 0, 27, 27, 0, 35, 35, 0, 43,
            43, 7, 47, 47, 7, 55, 55, 7, 63, 63, 7, 71, 71, 7, 75, 75, 11, 83, 83, 11, 91, 91, 11, 99, 99, 11, 107, 107,
            15, 7, 0, 0, 15, 0, 0, 23, 0, 0, 31, 0, 0, 39, 0, 0, 47, 0, 0, 55, 0, 0, 63, 0, 0, 71, 0, 0, 79, 0, 0, 87,
            0, 0, 95, 0, 0, 103, 0, 0, 111, 0, 0, 119, 0, 0, 127, 0, 0, 19, 19, 0, 27, 27, 0, 35, 35, 0, 47, 43, 0, 55,
            47, 0, 67, 55, 0, 75, 59, 7, 87, 67, 7, 95, 71, 7, 107, 75, 11, 119, 83, 15, 131, 87, 19, 139, 91, 19, 151,
            95, 27, 163, 99, 31, 175, 103, 35, 35, 19, 7, 47, 23, 11, 59, 31, 15, 75, 35, 19, 87, 43, 23, 99, 47, 31,
            115, 55, 35, 127, 59, 43, 143, 67, 51, 159, 79, 51, 175, 99, 47, 191, 119, 47, 207, 143, 43, 223, 171, 39,
            239, 203, 31, 255, 243, 27, 11, 7, 0, 27, 19, 0, 43, 35, 15, 55, 43, 19, 71, 51, 27, 83, 55, 35, 99, 63, 43,
            111, 71, 51, 127, 83, 63, 139, 95, 71, 155, 107, 83, 167, 123, 95, 183, 135, 107, 195, 147, 123, 211, 163,
            139, 227, 179, 151, 171, 139, 163, 159, 127, 151, 147, 115, 135, 139, 103, 123, 127, 91, 111, 119, 83, 99,
            107, 75, 87, 95, 63, 75, 87, 55, 67, 75, 47, 55, 67, 39, 47, 55, 31, 35, 43, 23, 27, 35, 19, 19, 23, 11, 11,
            15, 7, 7, 187, 115, 159, 175, 107, 143, 163, 95, 131, 151, 87, 119, 139, 79, 107, 127, 75, 95, 115, 67, 83,
            107, 59, 75, 95, 51, 63, 83, 43, 55, 71, 35, 43, 59, 31, 35, 47, 23, 27, 35, 19, 19, 23, 11, 11, 15, 7, 7,
            219, 195, 187, 203, 179, 167, 191, 163, 155, 175, 151, 139, 163, 135, 123, 151, 123, 111, 135, 111, 95, 123,
            99, 83, 107, 87, 71, 95, 75, 59, 83, 63, 51, 67, 51, 39, 55, 43, 31, 39, 31, 23, 27, 19, 15, 15, 11, 7, 111,
            131, 123, 103, 123, 111, 95, 115, 103, 87, 107, 95, 79, 99, 87, 71, 91, 79, 63, 83, 71, 55, 75, 63, 47, 67,
            55, 43, 59, 47, 35, 51, 39, 31, 43, 31, 23, 35, 23, 15, 27, 19, 11, 19, 11, 7, 11, 7, 255, 243, 27, 239,
            223, 23, 219, 203, 19, 203, 183, 15, 187, 167, 15, 171, 151, 11, 155, 131, 7, 139, 115, 7, 123, 99, 7, 107,
            83, 0, 91, 71, 0, 75, 55, 0, 59, 43, 0, 43, 31, 0, 27, 15, 0, 11, 7, 0, 0, 0, 255, 11, 11, 239, 19, 19, 223,
            27, 27, 207, 35, 35, 191, 43, 43, 175, 47, 47, 159, 47, 47, 143, 47, 47, 127, 47, 47, 111, 47, 47, 95, 43,
            43, 79, 35, 35, 63, 27, 27, 47, 19, 19, 31, 11, 11, 15, 43, 0, 0, 59, 0, 0, 75, 7, 0, 95, 7, 0, 111, 15, 0,
            127, 23, 7, 147, 31, 7, 163, 39, 11, 183, 51, 15, 195, 75, 27, 207, 99, 43, 219, 127, 59, 227, 151, 79, 231,
            171, 95, 239, 191, 119, 247, 211, 139, 167, 123, 59, 183, 155, 55, 199, 195, 55, 231, 227, 87, 127, 191,
            255, 171, 231, 255, 215, 255, 255, 103, 0, 0, 139, 0, 0, 179, 0, 0, 215, 0, 0, 255, 0, 0, 255, 243, 147,
            255, 247, 199, 255, 255, 255, 159, 91, 83};

        static const auto palette = make_palette(palette_bytes);
        return palette;
    }

private:
    struct content_stats_t : public content_stats_base_t
    {
        std::mutex stat_mutex;
        std::unordered_map<typename q1_contentflags_bits::bitset_t, size_t> native_types;

        std::atomic<size_t> total_brushes;
    };

public:
    std::unique_ptr<content_stats_base_t> create_content_stats() const override
    {
        return std::unique_ptr<content_stats_base_t>(new content_stats_t{});
    }

    void count_contents_in_stats(const contentflags_t &contents, content_stats_base_t &stats_any) const override
    {
        content_stats_t &stats = dynamic_cast<content_stats_t &>(stats_any);

        // convert to std::bitset so we can use it as an unordered_map key
        const auto bitset = contentflags_to_bits(contents).bitset();

        {
            std::unique_lock lock(stats.stat_mutex);
            stats.native_types[bitset]++;
        }

        stats.total_brushes++;
    }

    void print_content_stats(const content_stats_base_t &stats_any, const char *what) const override
    {
        const content_stats_t &stats = dynamic_cast<const content_stats_t &>(stats_any);
        logging::stat_tracker_t stat_print;

        for (auto [bits, count] : stats.native_types) {
            stat_print.register_stat(fmt::format("{} {}", get_contents_display(q1_contentflags_bits(bits)), what))
                .count += count;
        }

        stat_print.register_stat(fmt::format("{} total", what)).count += stats.total_brushes;
    }
};

struct gamedef_h2_t : public gamedef_q1_like_t<GAME_HEXEN_II>
{
    gamedef_h2_t()
        : gamedef_q1_like_t("hexen2", "DATA1")
    {
    }

    const std::initializer_list<aabb3d> &get_hull_sizes() const override
    {
        static std::initializer_list<aabb3d> hulls = {{{0, 0, 0}, {0, 0, 0}}, {{-16, -16, -32}, {16, 16, 24}},
            {{-24, -24, -20}, {24, 24, 20}}, {{-16, -16, -16}, {16, 16, 12}},
            {{-8, -8, -8}, {8, 8, 8}}, // {{-40, -40, -42}, {40, 40, 42}} = original game
            {{-28, -28, -40}, {28, 28, 40}}};

        return hulls;
    }

    const std::vector<qvec3b> &get_default_palette() const override
    {
        static constexpr std::initializer_list<uint8_t> palette_bytes{0, 0, 0, 0, 0, 0, 8, 8, 8, 16, 16, 16, 24, 24, 24,
            32, 32, 32, 40, 40, 40, 48, 48, 48, 56, 56, 56, 64, 64, 64, 72, 72, 72, 80, 80, 80, 84, 84, 84, 88, 88, 88,
            96, 96, 96, 104, 104, 104, 112, 112, 112, 120, 120, 120, 128, 128, 128, 136, 136, 136, 148, 148, 148, 156,
            156, 156, 168, 168, 168, 180, 180, 180, 184, 184, 184, 196, 196, 196, 204, 204, 204, 212, 212, 212, 224,
            224, 224, 232, 232, 232, 240, 240, 240, 252, 252, 252, 8, 8, 12, 16, 16, 20, 24, 24, 28, 28, 32, 36, 36, 36,
            44, 44, 44, 52, 48, 52, 60, 56, 56, 68, 64, 64, 72, 76, 76, 88, 92, 92, 104, 108, 112, 128, 128, 132, 152,
            152, 156, 176, 168, 172, 196, 188, 196, 220, 32, 24, 20, 40, 32, 28, 48, 36, 32, 52, 44, 40, 60, 52, 44, 68,
            56, 52, 76, 64, 56, 84, 72, 64, 92, 76, 72, 100, 84, 76, 108, 92, 84, 112, 96, 88, 120, 104, 96, 128, 112,
            100, 136, 116, 108, 144, 124, 112, 20, 24, 20, 28, 32, 28, 32, 36, 32, 40, 44, 40, 44, 48, 44, 48, 56, 48,
            56, 64, 56, 64, 68, 64, 68, 76, 68, 84, 92, 84, 104, 112, 104, 120, 128, 120, 140, 148, 136, 156, 164, 152,
            172, 180, 168, 188, 196, 184, 48, 32, 8, 60, 40, 8, 72, 48, 16, 84, 56, 20, 92, 64, 28, 100, 72, 36, 108,
            80, 44, 120, 92, 52, 136, 104, 60, 148, 116, 72, 160, 128, 84, 168, 136, 92, 180, 144, 100, 188, 152, 108,
            196, 160, 116, 204, 168, 124, 16, 20, 16, 20, 28, 20, 24, 32, 24, 28, 36, 28, 32, 44, 32, 36, 48, 36, 40,
            56, 40, 44, 60, 44, 48, 68, 48, 52, 76, 52, 60, 84, 60, 68, 92, 64, 76, 100, 72, 84, 108, 76, 92, 116, 84,
            100, 128, 92, 24, 12, 8, 32, 16, 8, 40, 20, 8, 52, 24, 12, 60, 28, 12, 68, 32, 12, 76, 36, 16, 84, 44, 20,
            92, 48, 24, 100, 56, 28, 112, 64, 32, 120, 72, 36, 128, 80, 44, 144, 92, 56, 168, 112, 72, 192, 132, 88, 24,
            4, 4, 36, 4, 4, 48, 0, 0, 60, 0, 0, 68, 0, 0, 80, 0, 0, 88, 0, 0, 100, 0, 0, 112, 0, 0, 132, 0, 0, 152, 0,
            0, 172, 0, 0, 192, 0, 0, 212, 0, 0, 232, 0, 0, 252, 0, 0, 16, 12, 32, 28, 20, 48, 32, 28, 56, 40, 36, 68,
            52, 44, 80, 60, 56, 92, 68, 64, 104, 80, 72, 116, 88, 84, 128, 100, 96, 140, 108, 108, 152, 120, 116, 164,
            132, 132, 176, 144, 144, 188, 156, 156, 200, 172, 172, 212, 36, 20, 4, 52, 24, 4, 68, 32, 4, 80, 40, 0, 100,
            48, 4, 124, 60, 4, 140, 72, 4, 156, 88, 8, 172, 100, 8, 188, 116, 12, 204, 128, 12, 220, 144, 16, 236, 160,
            20, 252, 184, 56, 248, 200, 80, 248, 220, 120, 20, 16, 4, 28, 24, 8, 36, 32, 8, 44, 40, 12, 52, 48, 16, 56,
            56, 16, 64, 64, 20, 68, 72, 24, 72, 80, 28, 80, 92, 32, 84, 104, 40, 88, 116, 44, 92, 128, 52, 92, 140, 52,
            92, 148, 56, 96, 160, 64, 60, 16, 16, 72, 24, 24, 84, 28, 28, 100, 36, 36, 112, 44, 44, 124, 52, 48, 140,
            64, 56, 152, 76, 64, 44, 20, 8, 56, 28, 12, 72, 32, 16, 84, 40, 20, 96, 44, 28, 112, 52, 32, 124, 56, 40,
            140, 64, 48, 24, 20, 16, 36, 28, 20, 44, 36, 28, 56, 44, 32, 64, 52, 36, 72, 60, 44, 80, 68, 48, 92, 76, 52,
            100, 84, 60, 112, 92, 68, 120, 100, 72, 132, 112, 80, 144, 120, 88, 152, 128, 96, 160, 136, 104, 168, 148,
            112, 36, 24, 12, 44, 32, 16, 52, 40, 20, 60, 44, 20, 72, 52, 24, 80, 60, 28, 88, 68, 28, 104, 76, 32, 148,
            96, 56, 160, 108, 64, 172, 116, 72, 180, 124, 80, 192, 132, 88, 204, 140, 92, 216, 156, 108, 60, 20, 92,
            100, 36, 116, 168, 72, 164, 204, 108, 192, 4, 84, 4, 4, 132, 4, 0, 180, 0, 0, 216, 0, 4, 4, 144, 16, 68,
            204, 36, 132, 224, 88, 168, 232, 216, 4, 4, 244, 72, 0, 252, 128, 0, 252, 172, 24, 252, 252, 252};

        static const auto palette = make_palette(palette_bytes);
        return palette;
    }
};

struct gamedef_hl_t : public gamedef_q1_like_t<GAME_HALF_LIFE>
{
    gamedef_hl_t()
        : gamedef_q1_like_t("halflife", "VALVE")
    {
        has_rgb_lightmap = true;
    }

    const std::initializer_list<aabb3d> &get_hull_sizes() const override
    {
        static std::initializer_list<aabb3d> hulls = {{{0, 0, 0}, {0, 0, 0}}, {{-16, -16, -36}, {16, 16, 36}},
            {{-32, -32, -32}, {32, 32, 32}}, {{-16, -16, -18}, {16, 16, 18}}};

        return hulls;
    }

    const std::vector<qvec3b> &get_default_palette() const override
    {
        static const std::vector<qvec3b> palette;
        return palette;
    }
};

struct gamedef_q2_t : public gamedef_t
{
    gamedef_q2_t()
        : gamedef_t("quake2", "baseq2")
    {
        this->id = GAME_QUAKE_II;
        has_rgb_lightmap = true;
        allow_contented_bmodels = true;
        max_entity_key = 256;
    }

    bool surf_is_lightmapped(
        const surfflags_t &flags, const char *texname, bool light_nodraw, bool lightgrid_enabled) const override
    {
        /* don't save lightmaps for "trigger" texture even if light_nodraw is set */
        if (std::string_view(texname).ends_with("/trigger"))
            return false;

        // Q2RTX should light nodraw faces
        if (light_nodraw && (flags.native & Q2_SURF_NODRAW)) {
            return true;
        }

        // The only reason to lightmap sky faces in Q2 is to light models floating over sky.
        // If lightgrid is in use, this reason is no longer relevant, so skip lightmapping.
        if (lightgrid_enabled && (flags.native & Q2_SURF_SKY)) {
            return false;
        }

        return !(flags.native & (Q2_SURF_NODRAW | Q2_SURF_SKIP));
    }

    bool surf_is_emissive(const surfflags_t &flags, const char *texname) const override { return true; }

    bool surf_is_subdivided(const surfflags_t &flags) const override { return !(flags.native & Q2_SURF_SKY); }

    bool surfflags_are_valid(const surfflags_t &flags) const override
    {
        // no rules in Quake II baby
        return true;
    }

    bool surfflags_may_phong(const surfflags_t &a, const surfflags_t &b) const override
    {
        // these are the bits we'll require to match in order to allow phonging `a` and `b`
        auto mask = [](const surfflags_t &flags) {
            return flags.native &
                   (Q2_SURF_SKY | Q2_SURF_WARP | Q2_SURF_TRANS33 | Q2_SURF_TRANS66 | Q2_SURF_FLOWING | Q2_SURF_NODRAW);
        };

        return mask(a) == mask(b);
    }

    static constexpr const char *surf_bitflag_names[] = {"LIGHT", "SLICK", "SKY", "WARP", "TRANS33", "TRANS66",
        "FLOWING", "NODRAW", "HINT", "512", "1024", "2048", "4096", "8192", "16384", "32768", "65536", "131072",
        "262144", "524288", "1048576", "2097152", "4194304", "8388608", "16777216", "ALPHATEST"};

    int32_t surfflags_from_string(const std::string_view &str) const override
    {
        for (size_t i = 0; i < std::size(surf_bitflag_names); i++) {
            if (string_iequals(str, surf_bitflag_names[i])) {
                return nth_bit(i);
            }
        }

        return 0;
    }

    bool texinfo_is_hintskip(const surfflags_t &flags, const std::string &name) const override
    {
        // any face in a hint brush that isn't HINT are treated as "hintskip", and discarded
        return !(flags.native & Q2_SURF_HINT);
    }

    contentflags_t cluster_contents(const contentflags_t &contents0, const contentflags_t &contents1) const override
    {
        contentflags_t c = {contents0.native | contents1.native};

        c.illusionary_visblocker = contents0.illusionary_visblocker || contents1.illusionary_visblocker;

        // a cluster may include some solid detail areas, but
        // still be seen into
        if (!(contents0.native & Q2_CONTENTS_SOLID) || !(contents1.native & Q2_CONTENTS_SOLID)) {
            c.native &= ~Q2_CONTENTS_SOLID;
        }

        return c;
    }

    inline int32_t get_content_type(const contentflags_t &contents) const
    {
        // HACK: Q2_CONTENTS_MONSTER is only here for func_detail_wall
        return contents.native & (Q2_ALL_VISIBLE_CONTENTS_PLUS_MONSTER |
                                     (Q2_CONTENTS_PLAYERCLIP | Q2_CONTENTS_MONSTERCLIP | Q2_CONTENTS_ORIGIN |
                                         Q2_CONTENTS_TRANSLUCENT | Q2_CONTENTS_AREAPORTAL));
    }

    contentflags_t create_empty_contents() const override { return {Q2_CONTENTS_EMPTY}; }

    contentflags_t create_solid_contents() const override { return {Q2_CONTENTS_SOLID}; }

    contentflags_t create_detail_illusionary_contents(const contentflags_t &original) const override
    {
        contentflags_t result = original;
        result.native &= ~Q2_CONTENTS_SOLID;
        result.native |= Q2_CONTENTS_MIST | Q2_CONTENTS_DETAIL;
        return result;
    }

    contentflags_t create_detail_fence_contents(const contentflags_t &original) const override
    {
        contentflags_t result = original;
        result.native &= ~Q2_CONTENTS_SOLID;
        result.native |= (Q2_CONTENTS_WINDOW | Q2_CONTENTS_TRANSLUCENT | Q2_CONTENTS_DETAIL);
        return result;
    }

    contentflags_t create_detail_wall_contents(const contentflags_t &original) const override
    {
        contentflags_t result = original;
        // HACK: borrowing Q2_CONTENTS_MONSTER as a compiler internal flag
        result.native &= ~Q2_CONTENTS_SOLID;
        result.native |= (Q2_CONTENTS_MONSTER | Q2_CONTENTS_DETAIL);
        return result;
    }

    contentflags_t create_detail_solid_contents(const contentflags_t &original) const override
    {
        contentflags_t result = original;
        result.native |= (Q2_CONTENTS_SOLID | Q2_CONTENTS_DETAIL);
        return result;
    }

    contentflags_t clear_detail(const contentflags_t &original) const override
    {
        contentflags_t result = original;
        result.native &= ~Q2_CONTENTS_DETAIL;
        return result;
    }

    bool contents_are_type_equal(const contentflags_t &self, const contentflags_t &other) const override
    {
        return self.illusionary_visblocker == other.illusionary_visblocker &&
               get_content_type(self) == get_content_type(other);
    }

    bool contents_are_equal(const contentflags_t &self, const contentflags_t &other) const override
    {
        return self.illusionary_visblocker == other.illusionary_visblocker && self.native == other.native;
    }

    bool contents_are_any_detail(const contentflags_t &contents) const override
    {
        return ((contents.native & Q2_CONTENTS_DETAIL) != 0);
    }

    bool contents_are_detail_solid(const contentflags_t &contents) const override
    {
        int32_t test = (Q2_CONTENTS_DETAIL | Q2_CONTENTS_SOLID);

        return ((contents.native & test) == test);
    }

    bool contents_are_detail_wall(const contentflags_t &contents) const override
    {
        if (contents.native & Q2_CONTENTS_SOLID) {
            return false;
        }

        int32_t test = (Q2_CONTENTS_DETAIL | Q2_CONTENTS_MONSTER);
        return ((contents.native & test) == test);
    }

    bool contents_are_detail_fence(const contentflags_t &contents) const override
    {
        if (contents.native & Q2_CONTENTS_SOLID) {
            return false;
        }

        int32_t test = (Q2_CONTENTS_DETAIL | Q2_CONTENTS_WINDOW);
        return ((contents.native & test) == test);
    }

    bool contents_are_detail_illusionary(const contentflags_t &contents) const override
    {
        if (contents.native & Q2_CONTENTS_SOLID) {
            return false;
        }

        int32_t mist1_type = (Q2_CONTENTS_DETAIL | Q2_CONTENTS_MIST);
        int32_t mist2_type = (Q2_CONTENTS_DETAIL | Q2_CONTENTS_AUX);

        return ((contents.native & mist1_type) == mist1_type) || ((contents.native & mist2_type) == mist2_type);
    }

    bool contents_are_origin(const contentflags_t &contents) const override
    {
        return contents.native & Q2_CONTENTS_ORIGIN;
    }

    bool contents_are_clip(const contentflags_t &contents) const override
    {
        return contents.native & (Q2_CONTENTS_PLAYERCLIP | Q2_CONTENTS_MONSTERCLIP);
    }

    bool contents_clip_same_type(const contentflags_t &self, const contentflags_t &other) const override
    {
        return (self.native & Q2_ALL_VISIBLE_CONTENTS_PLUS_MONSTER) ==
                   (other.native & Q2_ALL_VISIBLE_CONTENTS_PLUS_MONSTER) &&
               self.clips_same_type.value_or(true);
    }

    inline bool contents_has_extended(const contentflags_t &contents) const { return contents.illusionary_visblocker; }

    bool contents_are_empty(const contentflags_t &contents) const override
    {
        return !contents_has_extended(contents) && !get_content_type(contents);
    }

    bool contents_are_any_solid(const contentflags_t &contents) const override
    {
        return (contents.native & Q2_CONTENTS_SOLID) != 0;
    }

    bool contents_are_solid(const contentflags_t &contents) const override
    {
        return !contents_has_extended(contents) && (contents.native & Q2_CONTENTS_SOLID) &&
               !(contents.native & Q2_CONTENTS_DETAIL);
    }

    bool contents_are_sky(const contentflags_t &contents) const override { return false; }

    bool contents_are_liquid(const contentflags_t &contents) const override
    {
        if (contents_has_extended(contents))
            return false;

        if (contents.native & Q2_CONTENTS_AREAPORTAL)
            return true; // HACK: treat areaportal as a liquid for the purposes of the CSG code

        return contents.native & Q2_CONTENTS_LIQUID;
    }

    bool contents_are_valid(const contentflags_t &contents, bool strict) const override
    {
        // check that we don't have more than one visible contents type
        const int32_t x = contents.native & Q2_ALL_VISIBLE_CONTENTS_PLUS_MONSTER;

        // TODO: check other invalid mixes
        if (!x && strict) {
            return false;
        }

        return true;
    }

    static constexpr const char *bitflag_names[] = {"SOLID", "WINDOW", "AUX", "LAVA", "SLIME", "WATER", "MIST", "128",
        "256", "512", "1024", "2048", "4096", "8192", "16384", "AREAPORTAL", "PLAYERCLIP", "MONSTERCLIP", "CURRENT_0",
        "CURRENT_90", "CURRENT_180", "CURRENT_270", "CURRENT_UP", "CURRENT_DOWN", "ORIGIN", "MONSTER", "DEADMONSTER",
        "DETAIL", "TRANSLUCENT", "LADDER", "1073741824", "2147483648"};

    int32_t contents_from_string(const std::string_view &str) const override
    {
        for (size_t i = 0; i < std::size(bitflag_names); i++) {
            if (string_iequals(str, bitflag_names[i])) {
                return nth_bit(i);
            }
        }

        return 0;
    }

    /**
     * Returns the single content bit of the strongest visible content present
     */
    constexpr int32_t visible_contents(const int32_t &contents) const
    {
        // HACK: func_detail_wall (Q2_CONTENTS_MONSTER) fits between Q2_CONTENTS_SOLID and
        // Q2_CONTENTS_WINDOW

        if (contents & Q2_CONTENTS_SOLID)
            return Q2_CONTENTS_SOLID;

        if (contents & Q2_CONTENTS_MONSTER)
            return Q2_CONTENTS_MONSTER;

        for (int32_t i = Q2_CONTENTS_WINDOW; i <= Q2_LAST_VISIBLE_CONTENTS; i <<= 1) {
            if (contents & i) {
                return i;
            }
        }

        return 0;
    }

    contentflags_t portal_visible_contents(const contentflags_t &a, const contentflags_t &b) const override
    {
        contentflags_t result;

        if (!a.clips_same_type.value_or(true) || !b.clips_same_type.value_or(true)) {
            result.native = visible_contents(a.native | b.native);
        } else {
            result.native = visible_contents(a.native ^ b.native);
        }
        return result;
    }

    bool portal_can_see_through(
        const contentflags_t &contents0, const contentflags_t &contents1, bool, bool) const override
    {
        int32_t c0 = contents0.native, c1 = contents1.native;

        // can't see through solid
        if ((c0 | c1) & Q2_CONTENTS_SOLID) {
            return false;
        }

        if (!visible_contents(c0 ^ c1)) {
            return true;
        }

        if ((c0 & Q2_CONTENTS_TRANSLUCENT) || contents0.is_any_detail(this)) {
            c0 = 0;
        }
        if ((c1 & Q2_CONTENTS_TRANSLUCENT) || contents1.is_any_detail(this)) {
            c1 = 0;
        }

        // identical on both sides
        if (!(c0 ^ c1))
            return true;

        return !visible_contents(c0 ^ c1);
    }

    bool contents_seals_map(const contentflags_t &contents) const override
    {
        return contents_are_solid(contents) || contents_are_sky(contents);
    }

    contentflags_t contents_remap_for_export(const contentflags_t &contents, remap_type_t type) const override
    {
        // HACK: borrowing Q2_CONTENTS_MONSTER for func_detail_wall
        if (contents.native & Q2_CONTENTS_MONSTER) {
            return {Q2_CONTENTS_SOLID};
        }
        // Solid wipes out any other contents
        // Previously, this was done in LeafNode but we've changed to detail-solid being
        // non-sealing.
        if (type == remap_type_t::leaf) {
            if (contents.native & Q2_CONTENTS_SOLID) {
                return {Q2_CONTENTS_SOLID};
            }
        }

        return contents;
    }

    contentflags_t combine_contents(const contentflags_t &a, const contentflags_t &b) const override
    {
        // structural solid (but not detail solid) eats any other contents
        if (contents_are_solid(a) || contents_are_solid(b)) {
            return {Q2_CONTENTS_SOLID};
        }

        contentflags_t result;
        result.native = a.native | b.native;
        result.clips_same_type = (a.clips_same_type.value_or(true) && b.clips_same_type.value_or(true));
        result.mirror_inside = (a.mirror_inside.value_or(true) && b.mirror_inside.value_or(true));
        result.illusionary_visblocker = a.illusionary_visblocker || b.illusionary_visblocker;
        return result;
    }

    bool portal_generates_face(const contentflags_t &portal_visible_contents, const contentflags_t &brushcontents,
        planeside_t brushside_side) const override
    {
        if ((portal_visible_contents.native & brushcontents.native) == 0) {
            return false;
        }

        if (brushside_side == SIDE_BACK) {
            // explicit override?
            if (brushcontents.mirror_inside) {
                return *brushcontents.mirror_inside;
            }
            if (portal_visible_contents.native & (Q2_CONTENTS_WINDOW | Q2_CONTENTS_AUX)) {
                // windows or aux don't generate inside faces
                return false;
            }
            // other types get mirrored by default
            return true;
        }
        return true;
    }

    std::string get_contents_display(const contentflags_t &contents) const override
    {
        if (!contents.native) {
            return "EMPTY";
        }

        std::string s;

        for (int32_t i = 0; i < std::size(bitflag_names); i++) {
            if (contents.native & nth_bit(i)) {
                if (s.size()) {
                    s += " | ";
                }

                s += bitflag_names[i];
            }
        }

        return s;
    }

    void contents_make_valid(contentflags_t &contents) const override
    {
        if (contents_are_valid(contents, false)) {
            return;
        }

        bool got = false;

        for (int32_t i = 0; i < 8; i++) {
            if (!got) {
                if (contents.native & nth_bit(i)) {
                    got = true;
                    continue;
                }
            } else {
                contents.native &= ~nth_bit(i);
            }
        }
    }

    const std::initializer_list<aabb3d> &get_hull_sizes() const override
    {
        static constexpr std::initializer_list<aabb3d> hulls = {};
        return hulls;
    }

    contentflags_t face_get_contents(
        const std::string &texname, const surfflags_t &flags, const contentflags_t &contents) const override
    {
        // hints and skips are never detail, and have no content
        if (flags.native & (Q2_SURF_HINT | Q2_SURF_SKIP)) {
            return {Q2_CONTENTS_EMPTY};
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
        if (surf_contents.native & Q2_CONTENTS_TRANSLUCENT) {
            surf_contents.native |= Q2_CONTENTS_DETAIL;
        } else if (surf_contents.native & (Q2_CONTENTS_MIST | Q2_CONTENTS_AUX)) {
            surf_contents.native |= Q2_CONTENTS_DETAIL;
        }

        if (surf_contents.native & (Q2_CONTENTS_MONSTERCLIP | Q2_CONTENTS_PLAYERCLIP)) {
            surf_contents.native |= Q2_CONTENTS_DETAIL;
        }

        return surf_contents;
    }

private:
    void discoverArchives(const fs::path &base) const
    {
        fs::directory_iterator it(base);

        std::set<std::string, natural_case_insensitive_less> packs;

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
    inline void addArchive(const fs::path &path) const
    {
        fs::addArchive(path, true);
        discoverArchives(path);
    }

    void init_filesystem(const fs::path &source, const settings::common_settings &options) const override
    {
        img::clear();
        fs::clear();

        if (options.defaultpaths.value()) {
            constexpr const char *MAPS_FOLDER = "maps";

            // detect gamedir (mod directory path)
            fs::path gamedir, basedir;

            // pull in from settings
            if (options.gamedir.is_changed()) {
                gamedir = options.gamedir.value();
            }
            if (options.basedir.is_changed()) {
                basedir = options.basedir.value();
            }

            // figure out the gamedir first
            if (!gamedir.is_absolute()) {
                if (!gamedir.empty() && basedir.is_absolute()) {
                    // we passed in a relative gamedir. probably meant to
                    // be derived from basedir.
                    gamedir = basedir.parent_path() / gamedir;
                }

                // no gamedir, so calculate it from the input
                if (gamedir.empty()) {
                    // expand canonicals, and fetch parent of source file
                    if (auto paths = fs::splitArchivePath(source)) {
                        // if the source is an archive, use the parent
                        // of that folder as the mod directory
                        // pak0.pak/maps/source.map -> C:/Quake/ID1
                        gamedir = fs::canonical(paths.archive).parent_path();
                    } else {
                        // maps/*/source.map -> C:/Quake/ID1/maps
                        // this is weak because the source may not exist yet
                        bool found_maps_folder = false;
                        fs::path olddir = gamedir = source;

                        // NOTE: parent_path() of C:/ is C:/ and this is considered non-empty
                        // its relative_path() (the part after the drive letter) is empty, though
                        while (!gamedir.relative_path().empty()) {
                            gamedir = fs::weakly_canonical(gamedir).parent_path();

                            if (string_iequals(gamedir.filename().generic_string(), MAPS_FOLDER)) {
                                found_maps_folder = true;
                                break;
                            }
                        }

                        if (!found_maps_folder) {
                            logging::print(
                                "WARNING: '{}' is not a child of '{}'; gamedir can't be automatically determined.\n",
                                source, MAPS_FOLDER);

                            gamedir = olddir;
                        }

                        // C:/Quake/ID1/maps -> C:/Quake/ID1
                        gamedir = gamedir.parent_path();
                    }
                }
            }

            if (!exists(gamedir)) {
                logging::print("WARNING: failed to find gamedir '{}'\n", gamedir);
            } else {
                logging::print("using gamedir: '{}'\n", gamedir);
            }

            // now find base dir, if we haven't set it yet
            if (!basedir.is_absolute()) {
                if (!basedir.empty() && gamedir.is_absolute()) {
                    // we passed in a relative basedir. probably meant to
                    // be derived from gamedir.
                    basedir = gamedir.parent_path() / basedir;
                }

                // no basedir, so calculate it from gamedir
                if (basedir.empty()) {
                    basedir = gamedir.parent_path() / default_base_dir;
                }
            }

            if (!exists(basedir)) {
                logging::print("WARNING: failed to find basedir '{}'\n", basedir);
            } else if (!equivalent(gamedir, basedir)) {
                addArchive(basedir);
                logging::print("using basedir: '{}'\n", basedir);
            }

            if (exists(gamedir)) {
                addArchive(gamedir);
            }
        }

        // add secondary paths
        for (auto &path : options.paths.values()) {
            addArchive(path);
        }

        // load palette
        img::init_palette(this);
    }

    const std::vector<qvec3b> &get_default_palette() const override
    {
        static constexpr std::initializer_list<uint8_t> palette_bytes{0, 0, 0, 15, 15, 15, 31, 31, 31, 47, 47, 47, 63,
            63, 63, 75, 75, 75, 91, 91, 91, 107, 107, 107, 123, 123, 123, 139, 139, 139, 155, 155, 155, 171, 171, 171,
            187, 187, 187, 203, 203, 203, 219, 219, 219, 235, 235, 235, 99, 75, 35, 91, 67, 31, 83, 63, 31, 79, 59, 27,
            71, 55, 27, 63, 47, 23, 59, 43, 23, 51, 39, 19, 47, 35, 19, 43, 31, 19, 39, 27, 15, 35, 23, 15, 27, 19, 11,
            23, 15, 11, 19, 15, 7, 15, 11, 7, 95, 95, 111, 91, 91, 103, 91, 83, 95, 87, 79, 91, 83, 75, 83, 79, 71, 75,
            71, 63, 67, 63, 59, 59, 59, 55, 55, 51, 47, 47, 47, 43, 43, 39, 39, 39, 35, 35, 35, 27, 27, 27, 23, 23, 23,
            19, 19, 19, 143, 119, 83, 123, 99, 67, 115, 91, 59, 103, 79, 47, 207, 151, 75, 167, 123, 59, 139, 103, 47,
            111, 83, 39, 235, 159, 39, 203, 139, 35, 175, 119, 31, 147, 99, 27, 119, 79, 23, 91, 59, 15, 63, 39, 11, 35,
            23, 7, 167, 59, 43, 159, 47, 35, 151, 43, 27, 139, 39, 19, 127, 31, 15, 115, 23, 11, 103, 23, 7, 87, 19, 0,
            75, 15, 0, 67, 15, 0, 59, 15, 0, 51, 11, 0, 43, 11, 0, 35, 11, 0, 27, 7, 0, 19, 7, 0, 123, 95, 75, 115, 87,
            67, 107, 83, 63, 103, 79, 59, 95, 71, 55, 87, 67, 51, 83, 63, 47, 75, 55, 43, 67, 51, 39, 63, 47, 35, 55,
            39, 27, 47, 35, 23, 39, 27, 19, 31, 23, 15, 23, 15, 11, 15, 11, 7, 111, 59, 23, 95, 55, 23, 83, 47, 23, 67,
            43, 23, 55, 35, 19, 39, 27, 15, 27, 19, 11, 15, 11, 7, 179, 91, 79, 191, 123, 111, 203, 155, 147, 215, 187,
            183, 203, 215, 223, 179, 199, 211, 159, 183, 195, 135, 167, 183, 115, 151, 167, 91, 135, 155, 71, 119, 139,
            47, 103, 127, 23, 83, 111, 19, 75, 103, 15, 67, 91, 11, 63, 83, 7, 55, 75, 7, 47, 63, 7, 39, 51, 0, 31, 43,
            0, 23, 31, 0, 15, 19, 0, 7, 11, 0, 0, 0, 139, 87, 87, 131, 79, 79, 123, 71, 71, 115, 67, 67, 107, 59, 59,
            99, 51, 51, 91, 47, 47, 87, 43, 43, 75, 35, 35, 63, 31, 31, 51, 27, 27, 43, 19, 19, 31, 15, 15, 19, 11, 11,
            11, 7, 7, 0, 0, 0, 151, 159, 123, 143, 151, 115, 135, 139, 107, 127, 131, 99, 119, 123, 95, 115, 115, 87,
            107, 107, 79, 99, 99, 71, 91, 91, 67, 79, 79, 59, 67, 67, 51, 55, 55, 43, 47, 47, 35, 35, 35, 27, 23, 23,
            19, 15, 15, 11, 159, 75, 63, 147, 67, 55, 139, 59, 47, 127, 55, 39, 119, 47, 35, 107, 43, 27, 99, 35, 23,
            87, 31, 19, 79, 27, 15, 67, 23, 11, 55, 19, 11, 43, 15, 7, 31, 11, 7, 23, 7, 0, 11, 0, 0, 0, 0, 0, 119, 123,
            207, 111, 115, 195, 103, 107, 183, 99, 99, 167, 91, 91, 155, 83, 87, 143, 75, 79, 127, 71, 71, 115, 63, 63,
            103, 55, 55, 87, 47, 47, 75, 39, 39, 63, 35, 31, 47, 27, 23, 35, 19, 15, 23, 11, 7, 7, 155, 171, 123, 143,
            159, 111, 135, 151, 99, 123, 139, 87, 115, 131, 75, 103, 119, 67, 95, 111, 59, 87, 103, 51, 75, 91, 39, 63,
            79, 27, 55, 67, 19, 47, 59, 11, 35, 47, 7, 27, 35, 0, 19, 23, 0, 11, 15, 0, 0, 255, 0, 35, 231, 15, 63, 211,
            27, 83, 187, 39, 95, 167, 47, 95, 143, 51, 95, 123, 51, 255, 255, 255, 255, 255, 211, 255, 255, 167, 255,
            255, 127, 255, 255, 83, 255, 255, 39, 255, 235, 31, 255, 215, 23, 255, 191, 15, 255, 171, 7, 255, 147, 0,
            239, 127, 0, 227, 107, 0, 211, 87, 0, 199, 71, 0, 183, 59, 0, 171, 43, 0, 155, 31, 0, 143, 23, 0, 127, 15,
            0, 115, 7, 0, 95, 0, 0, 71, 0, 0, 47, 0, 0, 27, 0, 0, 239, 0, 0, 55, 55, 255, 255, 0, 0, 0, 0, 255, 43, 43,
            35, 27, 27, 23, 19, 19, 15, 235, 151, 127, 195, 115, 83, 159, 87, 51, 123, 63, 27, 235, 211, 199, 199, 171,
            155, 167, 139, 119, 135, 107, 87, 159, 91, 83};
        static const auto palette = make_palette(palette_bytes);
        return palette;
    }

private:
    struct content_stats_t : public content_stats_base_t
    {
        std::mutex stat_mutex;
        std::unordered_map<int32_t, size_t> native_types;
        std::atomic<size_t> total_brushes;
        std::atomic<size_t> visblocker_brushes;
    };

public:
    std::unique_ptr<content_stats_base_t> create_content_stats() const override
    {
        return std::unique_ptr<content_stats_base_t>(new content_stats_t{});
    }

    void count_contents_in_stats(const contentflags_t &contents, content_stats_base_t &stats_any) const override
    {
        content_stats_t &stats = dynamic_cast<content_stats_t &>(stats_any);

        {
            std::unique_lock lock(stats.stat_mutex);
            stats.native_types[contents.native]++;
        }

        if (contents.illusionary_visblocker) {
            stats.visblocker_brushes++;
        }

        stats.total_brushes++;
    }

    void print_content_stats(const content_stats_base_t &stats_any, const char *what) const override
    {
        const content_stats_t &stats = dynamic_cast<const content_stats_t &>(stats_any);
        logging::stat_tracker_t stat_print;

        for (auto &it : stats.native_types) {
            stat_print.register_stat(fmt::format("{} {}", get_contents_display({it.first}), what)).count += it.second;
        }

        if (stats.visblocker_brushes) {
            stat_print.register_stat(fmt::format("VISBLOCKER {}", what)).count += stats.visblocker_brushes;
        }

        stat_print.register_stat(fmt::format("{} total", what)).count += stats.total_brushes;
    }
};

// Game definitions, used for the bsp versions below
static const gamedef_q1_like_t<GAME_QUAKE> gamedef_q1;
static const gamedef_h2_t gamedef_h2;
static const gamedef_hl_t gamedef_hl;
static const gamedef_q2_t gamedef_q2;

const std::initializer_list<const gamedef_t *> &gamedef_list()
{
    static constexpr std::initializer_list<const gamedef_t *> gamedefs {
        &gamedef_q1,
        &gamedef_h2,
        &gamedef_hl,
        &gamedef_q2
    };
    return gamedefs;
}

const bspversion_t bspver_generic{MBSPIDENT, std::nullopt, "mbsp", "generic BSP", {}};
const bspversion_t bspver_q1{BSPVERSION, std::nullopt, "bsp29", "Quake BSP",
    {
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
    },
    &gamedef_q1, &bspver_bsp2};
const bspversion_t bspver_bsp2{BSP2VERSION, std::nullopt, "bsp2", "Quake BSP2",
    {
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
    },
    &gamedef_q1};
const bspversion_t bspver_bsp2rmq{BSP2RMQVERSION, std::nullopt, "bsp2rmq", "Quake BSP2-RMQ",
    {
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
    },
    &gamedef_q1};
/* Hexen II doesn't use a separate version, but we can still use a separate tag/name for it */
const bspversion_t bspver_h2{BSPVERSION, std::nullopt, "hexen2", "Hexen II BSP",
    {
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
    },
    &gamedef_h2, &bspver_h2bsp2};
const bspversion_t bspver_h2bsp2{BSP2VERSION, std::nullopt, "hexen2bsp2", "Hexen II BSP2",
    {
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
    },
    &gamedef_h2};
const bspversion_t bspver_h2bsp2rmq{BSP2RMQVERSION, std::nullopt, "hexen2bsp2rmq", "Hexen II BSP2-RMQ",
    {
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
    },
    &gamedef_h2};
const bspversion_t bspver_hl{BSPHLVERSION, std::nullopt, "hl", "Half-Life BSP", bspver_q1.lumps, &gamedef_hl};
const bspversion_t bspver_q2{Q2_BSPIDENT, Q2_BSPVERSION, "q2bsp", "Quake II BSP",
    {
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
    },
    &gamedef_q2, &bspver_qbism};
const bspversion_t bspver_qbism{Q2_QBISMIDENT, Q2_BSPVERSION, "qbism", "Quake II Qbism BSP",
    {
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
    },
    &gamedef_q2};

static auto as_tuple(const surfflags_t &flags)
{
    return std::tie(flags.native, flags.is_nodraw, flags.is_hintskip, flags.is_hint, flags.no_dirt, flags.no_shadow,
        flags.no_bounce, flags.no_minlight, flags.no_expand, flags.no_phong, flags.light_ignore,
        flags.surflight_rescale, flags.surflight_style, flags.surflight_color, flags.surflight_minlight_scale,
        flags.surflight_targetname, flags.phong_angle, flags.phong_angle_concave, flags.phong_group, flags.minlight,
        flags.minlight_color, flags.light_alpha, flags.light_twosided, flags.maxlight, flags.lightcolorscale, flags.surflight_group,
        flags.world_units_per_luxel, flags.object_channel_mask);
}

bool surfflags_t::needs_write() const
{
    return as_tuple(*this) != as_tuple(surfflags_t());
}

bool surfflags_t::operator<(const surfflags_t &other) const
{
    return as_tuple(*this) < as_tuple(other);
}

bool surfflags_t::operator>(const surfflags_t &other) const
{
    return as_tuple(*this) > as_tuple(other);
}

bool surfflags_t::is_valid(const gamedef_t *game) const
{
    return game->surfflags_are_valid(*this);
}

bool contentflags_t::equals(const gamedef_t *game, const contentflags_t &other) const
{
    return game->contents_are_equal(*this, other) && mirror_inside == other.mirror_inside &&
           clips_same_type == other.clips_same_type;
}

bool contentflags_t::types_equal(const contentflags_t &other, const gamedef_t *game) const
{
    return game->contents_are_type_equal(*this, other);
}

bool contentflags_t::is_any_detail(const gamedef_t *game) const
{
    return game->contents_are_any_detail(*this);
}

bool contentflags_t::is_detail_solid(const gamedef_t *game) const
{
    return game->contents_are_detail_solid(*this);
}

bool contentflags_t::is_detail_wall(const gamedef_t *game) const
{
    return game->contents_are_detail_wall(*this);
}

bool contentflags_t::is_detail_fence(const gamedef_t *game) const
{
    return game->contents_are_detail_fence(*this);
}

bool contentflags_t::is_detail_illusionary(const gamedef_t *game) const
{
    return game->contents_are_detail_illusionary(*this);
}

contentflags_t &contentflags_t::set_mirrored(const std::optional<bool> &mirror_inside_value)
{
    mirror_inside = mirror_inside_value;
    return *this;
}

bool contentflags_t::will_clip_same_type(const gamedef_t *game, const contentflags_t &other) const
{
    return game->contents_clip_same_type(*this, other);
}

contentflags_t &contentflags_t::set_clips_same_type(const std::optional<bool> &clips_same_type_value)
{
    clips_same_type = clips_same_type_value;
    return *this;
}

bool contentflags_t::is_empty(const gamedef_t *game) const
{
    return game->contents_are_empty(*this);
}

bool contentflags_t::is_any_solid(const gamedef_t *game) const
{
    return game->contents_are_any_solid(*this);
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

bool contentflags_t::is_clip(const gamedef_t *game) const
{
    return game->contents_are_clip(*this);
}

bool contentflags_t::is_origin(const gamedef_t *game) const
{
    return game->contents_are_origin(*this);
}

void contentflags_t::make_valid(const gamedef_t *game)
{
    game->contents_make_valid(*this);
}

bool contentflags_t::is_fence(const gamedef_t *game) const
{
    return is_detail_fence(game) || is_detail_illusionary(game);
}

std::string contentflags_t::to_string(const gamedef_t *game) const
{
    std::string s = game->get_contents_display(*this);

    if (mirror_inside != std::nullopt) {
        s += fmt::format(" | MIRROR_INSIDE[{}]", mirror_inside.value() ? "true" : "false");
    }

    if (contentflags_t{native}.will_clip_same_type(game) != will_clip_same_type(game)) {
        s += fmt::format(" | CLIPS_SAME_TYPE[{}]",
            clips_same_type.has_value() ? (clips_same_type.value() ? "true" : "false") : "nullopt");
    }

    if (illusionary_visblocker) {
        s += " | ILLUSIONARY_VISBLOCKER";
    }

    return s;
}

gamedef_t::gamedef_t(const char *friendly_name, const char *default_base_dir)
    : friendly_name(friendly_name), default_base_dir(default_base_dir)
{
}

static bool BSPVersionSupported(int32_t ident, std::optional<int32_t> version, const bspversion_t **out_version)
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
    CopyArray(bsp.dtex, mbsp.dtex);
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
    CopyArray(mbsp.dtex, bsp.dtex);
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

        mbsp.file = bspdata->file;

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
        } catch (std::overflow_error e) {
            logging::print("LIMITS EXCEEDED ON {}\n", e.what());
            return false;
        }

        bspdata->version = to_version;
        return true;
    }

    return false;
}

static bool isHexen2(const dheader_t *header, const bspversion_t *bspversion)
{
    if (0 != (header->lumps[LUMP_MODELS].filelen % sizeof(dmodelh2_t))) {
        // definitely not H2
        return false;
    }
    if (0 != (header->lumps[LUMP_MODELS].filelen % sizeof(dmodelq1_t))) {
        // definitely not Q1
        return true;
    }

    // models lump is divisible by both the Q1 model size (64 bytes) and the H2 model size (80 bytes).

    const int bytes_per_face = bspversion->lumps.begin()[LUMP_FACES].size;
    const int bytes_per_node = bspversion->lumps.begin()[LUMP_NODES].size;
    const int bytes_per_leaf = bspversion->lumps.begin()[LUMP_LEAFS].size;
    const int bytes_per_clipnode = bspversion->lumps.begin()[LUMP_CLIPNODES].size;

    const int faces_count = header->lumps[LUMP_FACES].filelen / bytes_per_face;
    const int nodes_count = header->lumps[LUMP_NODES].filelen / bytes_per_node;
    const int leafs_count = header->lumps[LUMP_LEAFS].filelen / bytes_per_leaf;
    const int clipnodes_count = header->lumps[LUMP_CLIPNODES].filelen / bytes_per_clipnode;

    // assume H2, and do some basic validation
    // FIXME: this potentially does unaligned reads, convert to using streams like the rest of the loading code

    const dmodelh2_t *models_h2 = (const dmodelh2_t *)((const uint8_t *)header + header->lumps[LUMP_MODELS].fileofs);
    const int models_h2_count = header->lumps[LUMP_MODELS].filelen / sizeof(dmodelh2_t);

    for (int i = 0; i < models_h2_count; ++i) {
        const dmodelh2_t *model = &models_h2[i];

        // visleafs
        if (model->visleafs < 0 || model->visleafs > leafs_count)
            return false;

        // numfaces, firstface
        if (model->numfaces < 0)
            return false;
        if (model->firstface < 0)
            return false;
        if (model->firstface + model->numfaces > faces_count)
            return false;

        // headnode[0]
        if (model->headnode[0] >= nodes_count)
            return false;

        // clipnode headnodes
        for (int j = 1; j < 8; ++j) {
            if (model->headnode[j] >= clipnodes_count)
                return false;
        }
    }

    // passed all checks, assume H2
    return true;
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

        Q_assert((bool)s);
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

        Q_assert((bool)s);
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

        Q_assert((bool)s);
    }
};

template<typename T>
inline void ReadQ1BSP(lump_reader &reader, T &bsp)
{
    reader.read(LUMP_ENTITIES, bsp.dentdata);
    reader.read(LUMP_PLANES, bsp.dplanes);
    reader.read(LUMP_TEXTURES, bsp.dtex);
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

void texvecf::stream_read(std::istream &stream)
{
    for (size_t i = 0; i < 2; i++)
        for (size_t x = 0; x < 4; x++) {
            stream >= this->at(i, x);
        }
}

void texvecf::stream_write(std::ostream &stream) const
{
    for (size_t i = 0; i < 2; i++)
        for (size_t x = 0; x < 4; x++) {
            stream <= this->at(i, x);
        }
}

void bspdata_t::bspxentries::transfer(const char *xname, std::vector<uint8_t> &xdata)
{
    entries.insert_or_assign(xname, std::move(xdata));
}

void bspdata_t::bspxentries::transfer(const char *xname, std::vector<uint8_t> &&xdata)
{
    entries.insert_or_assign(xname, xdata);
}

/*
 * =============
 * LoadBSPFile
 * =============
 */
void LoadBSPFile(fs::path &filename, bspdata_t *bspdata)
{
    int i;

    logging::funcprint("'{}'\n", filename);

    bspdata->file = filename;

    /* load the file header */
    fs::data file_data = fs::load(filename);

    if (!file_data) {
        FError("Unable to load \"{}\"\n", filename);
    }

    filename = fs::resolveArchivePath(filename);

    imemstream stream(file_data->data(), file_data->size());

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

        temp_version.version = std::nullopt;
        std::copy(q1header.lumps.begin(), q1header.lumps.end(), std::back_inserter(lumps));
    }

    /* check the file version */
    if (!BSPVersionSupported(temp_version.ident, temp_version.version, &bspdata->version)) {
        logging::print("BSP is version {}\n", temp_version);
        Error("Sorry, this bsp version is not supported.");
    } else {
        // special case handling for Hexen II
        if (bspdata->version->game->id == GAME_QUAKE && isHexen2((dheader_t *)file_data->data(), bspdata->version)) {
            if (bspdata->version == &bspver_q1) {
                bspdata->version = &bspver_h2;
            } else if (bspdata->version == &bspver_bsp2) {
                bspdata->version = &bspver_h2bsp2;
            } else if (bspdata->version == &bspver_bsp2rmq) {
                bspdata->version = &bspver_h2bsp2rmq;
            }
        }

        logging::print("BSP is version {}\n", *bspdata->version);
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

    size_t bspxofs;

    // detect BSPX
    /*bspx header is positioned exactly+4align at the end of the last lump position (regardless of order)*/
    for (i = 0, bspxofs = 0; i < lumps.size(); i++) {
        bspxofs = std::max(bspxofs, static_cast<size_t>(lumps[i].fileofs + lumps[i].filelen));
    }

    bspxofs = (bspxofs + 3) & ~3;

    /*okay, so that's where it *should* be if it exists */
    if (bspxofs + sizeof(bspx_header_t) <= file_data->size()) {
        stream.seekg(bspxofs);

        bspx_header_t bspx;
        stream >= bspx;

        if (!stream || memcmp(bspx.id.data(), "BSPX", 4)) {
            logging::print("WARNING: invalid BSPX header\n");
            return;
        }

        for (size_t i = 0; i < bspx.numlumps; i++) {
            bspx_lump_t xlump;

            if (!(stream >= xlump)) {
                logging::print("WARNING: invalid BSPX lump at index {}\n", i);
                return;
            }

            if (xlump.fileofs > file_data->size() || (xlump.fileofs + xlump.filelen) > file_data->size()) {
                logging::print("WARNING: invalid BSPX lump at index {}\n", i);
                return;
            }

            bspdata->bspx.transfer(xlump.lumpname.data(), std::vector<uint8_t>(file_data->begin() + xlump.fileofs,
                                                              file_data->begin() + xlump.fileofs + xlump.filelen));
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
        Q_assert(version->lumps.size() > lump_num);
        const lumpspec_t &lumpspec = version->lumps.begin()[lump_num];
        lump_t *lumps;

        if (version->version.has_value()) {
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
            stream <= padding_n(4 - (written % 4));
    }

    // this is only here to satisfy std::visit
    constexpr void write_lump(size_t, const std::monostate &) { }

    // write structured string data
    inline void write_lump(size_t lump_num, const std::string &data)
    {
        Q_assert(version->lumps.size() > lump_num);
        const lumpspec_t &lumpspec = version->lumps.begin()[lump_num];
        lump_t *lumps;

        Q_assert(lumpspec.size == 1);

        if (version->version.has_value()) {
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
            stream <= padding_n(4 - (written % 4));
    }

    // write structured lump data
    template<typename T, typename = std::enable_if_t<std::is_member_function_pointer_v<decltype(&T::stream_write)>>>
    inline void write_lump(size_t lump_num, const T &data)
    {
        const lumpspec_t &lumpspec = version->lumps.begin()[lump_num];
        lump_t *lumps;

        Q_assert(lumpspec.size == 1);

        if (version->version.has_value()) {
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
            stream <= padding_n(4 - (written % 4));
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
        std::visit([this](auto &&arg) { this->write_lump(LUMP_MODELS, arg); }, bsp.dmodels);

        write_lump(LUMP_LIGHTING, bsp.dlightdata);
        write_lump(LUMP_VISIBILITY, bsp.dvisdata);
        write_lump(LUMP_ENTITIES, bsp.dentdata);
        write_lump(LUMP_TEXTURES, bsp.dtex);
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
            bspx_lump_t &lump = xlumps.emplace_back();
            lump.filelen = x.second.size();
            lump.fileofs = stream.tellp();
            memcpy(lump.lumpname.data(), x.first.c_str(), std::min(x.first.size(), lump.lumpname.size() - 1));

            stream.write(reinterpret_cast<const char *>(x.second.data()), x.second.size());

            if (x.second.size() % 4)
                stream <= padding_n(4 - (x.second.size() % 4));
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
void WriteBSPFile(const fs::path &filename, bspdata_t *bspdata)
{
    bspfile_t bspfile{};

    bspfile.version = bspdata->version;

    // headers are union'd, so this sets both
    bspfile.q2header.ident = bspfile.version->ident;

    if (bspfile.version->version.has_value()) {
        bspfile.q2header.version = bspfile.version->version.value();
    }

    logging::print("Writing {} as {}\n", filename, *bspdata->version);
    bspfile.stream.open(filename, std::ios_base::out | std::ios_base::trunc | std::ios_base::binary);

    if (!bspfile.stream)
        FError("unable to open {} for writing", filename);

    bspfile.stream << endianness<std::endian::little>;

    /* Save header space, updated after adding the lumps */
    if (bspfile.version->version.has_value()) {
        bspfile.stream <= bspfile.q2header;
    } else {
        bspfile.stream <= bspfile.q1header;
    }

    std::visit([&bspfile](auto &&arg) { bspfile.write_bsp(arg); }, bspdata->bsp);

    /*BSPX lumps are at a 4-byte alignment after the last of any official lump*/
    bspfile.write_bspx(*bspdata);

    bspfile.stream.seekp(0);

    // write the real header
    if (bspfile.version->version.has_value()) {
        bspfile.stream <= bspfile.q2header;
    } else {
        bspfile.stream <= bspfile.q1header;
    }
}

/* ========================================================================= */

inline void PrintLumpSize(const lumpspec_t &lump, size_t count)
{
    logging::print("{:7} {:<12} {:10}\n", count, lump.name, count * lump.size);
}

template<typename T>
inline void PrintQ1BSPLumps(const std::initializer_list<lumpspec_t> &lumpspec, const T &bsp)
{
    if (std::holds_alternative<dmodelh2_vector>(bsp.dmodels))
        PrintLumpSize(lumpspec.begin()[LUMP_MODELS], std::get<dmodelh2_vector>(bsp.dmodels).size());
    else
        PrintLumpSize(lumpspec.begin()[LUMP_MODELS], std::get<dmodelq1_vector>(bsp.dmodels).size());

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

    logging::print("{:7} {:<12} {:10}\n", bsp.dtex.textures.size(), "textures", bsp.dtex.stream_size());
    logging::print("{:7} {:<12} {:10}\n", "", "lightdata", bsp.dlightdata.size());
    logging::print("{:7} {:<12} {:10}\n", "", "visdata", bsp.dvisdata.size());
    logging::print("{:7} {:<12} {:10}\n", "", "entdata", bsp.dentdata.size() + 1); // include the null terminator
}

template<typename T>
inline void PrintQ2BSPLumps(const std::initializer_list<lumpspec_t> &lumpspec, const T &bsp)
{
    PrintLumpSize(lumpspec.begin()[Q2_LUMP_MODELS], bsp.dmodels.size());
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

    logging::print("{:7} {:<12} {:10}\n", "", "lightdata", bsp.dlightdata.size());
    logging::print("{:7} {:<12} {:10}\n", "", "visdata", bsp.dvis.bits.size());
    logging::print("{:7} {:<12} {:10}\n", "", "entdata", bsp.dentdata.size() + 1); // include the null terminator
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

    logging::print("\n{:7} {:<12} {:10}\n", "count", "lump name", "byte size");

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

    if (bspdata->bspx.entries.size()) {
        logging::print("\n{:<16} {:10}\n", "BSPX lump name", "byte size");

        for (auto &x : bspdata->bspx.entries) {
            logging::print("{:<16} {:10}\n", x.first, x.second.size());
        }
    }
}
