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

#include <common/bspfile_common.hh>

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

// lump_t

void lump_t::stream_write(std::ostream &s) const
{
    s <= std::tie(fileofs, filelen);
}

void lump_t::stream_read(std::istream &s)
{
    s >= std::tie(fileofs, filelen);
}

// contentflags_t

bool contentflags_t::equals(const gamedef_t *game, contentflags_t other) const
{
    return flags == other.flags;
}

bool contentflags_t::types_equal(contentflags_t other, const gamedef_t *game) const
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
    if (mirror_inside_value.has_value()) {
        if (*mirror_inside_value) {
            // set to true
            flags = static_cast<contents_t>(flags | EWT_CFLAG_MIRROR_INSIDE_SET | EWT_CFLAG_MIRROR_INSIDE);
        } else {
            // set to false
            flags = static_cast<contents_t>(flags | EWT_CFLAG_MIRROR_INSIDE_SET);
            flags = static_cast<contents_t>(flags & ~(EWT_CFLAG_MIRROR_INSIDE));
        }
    } else {
        // unset
        flags = static_cast<contents_t>(flags & ~(EWT_CFLAG_MIRROR_INSIDE_SET | EWT_CFLAG_MIRROR_INSIDE));
    }
    return *this;
}

bool contentflags_t::will_clip_same_type(const gamedef_t *game, contentflags_t other) const
{
    return game->contents_clip_same_type(*this, other);
}

contentflags_t &contentflags_t::set_clips_same_type(const std::optional<bool> &clips_same_type_value)
{
    if (clips_same_type_value) {
        if (!*clips_same_type_value) {
            *this = contentflags_t::make(flags | EWT_CFLAG_SUPPRESS_CLIPPING_SAME_TYPE);
        }
    }
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

std::string contentflags_t::to_string() const
{
    std::string s = get_contents_display(flags);

    return s;
}

// surfflags_t

static auto as_tuple(const surfflags_t &flags)
{
    return std::tie(flags.native, flags.is_nodraw, flags.is_hintskip, flags.is_hint, flags.no_dirt, flags.no_shadow,
        flags.no_bounce, flags.no_minlight, flags.no_expand, flags.no_phong, flags.light_ignore,
        flags.surflight_rescale, flags.surflight_style, flags.surflight_color, flags.surflight_minlight_scale,
        flags.surflight_atten, flags.surflight_targetname, flags.phong_angle, flags.phong_angle_concave,
        flags.phong_group, flags.minlight, flags.minlight_color, flags.light_alpha, flags.light_twosided,
        flags.maxlight, flags.lightcolorscale, flags.surflight_group, flags.world_units_per_luxel,
        flags.object_channel_mask);
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

// gamedef_t

gamedef_t::gamedef_t(const char *friendly_name, const char *default_base_dir)
    : friendly_name(friendly_name),
      default_base_dir(default_base_dir)
{
}

std::unique_ptr<content_stats_base_t> gamedef_t::create_content_stats() const
{
    return std::unique_ptr<content_stats_base_t>(new content_stats_t{});
}

void gamedef_t::count_contents_in_stats(contentflags_t contents, content_stats_base_t &stats_any) const
{
    content_stats_t &stats = dynamic_cast<content_stats_t &>(stats_any);

    {
        std::unique_lock lock(stats.stat_mutex);
        stats.native_types[contents.flags]++;
    }

    stats.total_brushes++;
}

void gamedef_t::print_content_stats(const content_stats_base_t &stats_any, const char *what) const
{
    const content_stats_t &stats = dynamic_cast<const content_stats_t &>(stats_any);
    logging::stat_tracker_t stat_print;

    for (auto [bits, count] : stats.native_types) {
        auto c = contentflags_t{.flags = bits};
        stat_print.register_stat(fmt::format("{} {}", get_contents_display(c.flags), what)).count += count;
    }

    stat_print.register_stat(fmt::format("{} total", what)).count += stats.total_brushes;
}

// texvecf

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
