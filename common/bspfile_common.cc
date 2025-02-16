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
#include <common/json.hh>

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

bool surfflags_t::is_nodraw() const
{
    return !!(native_q2 & Q2_SURF_NODRAW);
}

void surfflags_t::set_nodraw(bool nodraw)
{
    if (nodraw)
        native_q2 = static_cast<q2_surf_flags_t>(native_q2 | Q2_SURF_NODRAW);
    else
        native_q2 = static_cast<q2_surf_flags_t>(native_q2 & ~Q2_SURF_NODRAW);
}

bool surfflags_t::is_hint() const
{
    return !!(native_q2 & Q2_SURF_HINT);
}

void surfflags_t::set_hint(bool hint)
{
    if (hint)
        native_q2 = static_cast<q2_surf_flags_t>(native_q2 | Q2_SURF_HINT);
    else
        native_q2 = static_cast<q2_surf_flags_t>(native_q2 & ~Q2_SURF_HINT);
}

bool surfflags_t::is_hintskip() const
{
    return !!(native_q2 & Q2_SURF_SKIP);
}

void surfflags_t::set_hintskip(bool hintskip)
{
    if (hintskip)
        native_q2 = static_cast<q2_surf_flags_t>(native_q2 | Q2_SURF_SKIP);
    else
        native_q2 = static_cast<q2_surf_flags_t>(native_q2 & ~Q2_SURF_SKIP);
}

bool surfflags_t::needs_write() const
{
    return *this != surfflags_t();
}

bool surfflags_t::is_valid(const gamedef_t *game) const
{
    return game->surfflags_are_valid(*this);
}

nlohmann::json surfflags_t::to_json() const
{
    nlohmann::json t = nlohmann::json::object();

    if (is_nodraw()) {
        t["is_nodraw"] = is_nodraw();
    }
    if (is_hint()) {
        t["is_hint"] = is_hint();
    }
    if (no_dirt) {
        t["no_dirt"] = no_dirt;
    }
    if (no_shadow) {
        t["no_shadow"] = no_shadow;
    }
    if (no_bounce) {
        t["no_bounce"] = no_bounce;
    }
    if (no_minlight) {
        t["no_minlight"] = no_minlight;
    }
    if (no_expand) {
        t["no_expand"] = no_expand;
    }
    if (light_ignore) {
        t["light_ignore"] = light_ignore;
    }
    if (surflight_rescale) {
        t["surflight_rescale"] = surflight_rescale.value();
    }
    if (surflight_style.has_value()) {
        t["surflight_style"] = surflight_style.value();
    }
    if (surflight_targetname.has_value()) {
        t["surflight_targetname"] = surflight_targetname.value();
    }
    if (surflight_color.has_value()) {
        t["surflight_color"] = surflight_color.value();
    }
    if (surflight_minlight_scale.has_value()) {
        t["surflight_minlight_scale"] = surflight_minlight_scale.value();
    }
    if (surflight_atten.has_value()) {
        t["surflight_atten"] = surflight_atten.value();
    }
    if (phong_angle) {
        t["phong_angle"] = phong_angle;
    }
    if (phong_angle_concave) {
        t["phong_angle_concave"] = phong_angle_concave;
    }
    if (phong_group) {
        t["phong_group"] = phong_group;
    }
    if (minlight) {
        t["minlight"] = *minlight;
    }
    if (maxlight) {
        t["maxlight"] = maxlight;
    }
    if (!qv::emptyExact(minlight_color)) {
        t["minlight_color"] = minlight_color;
    }
    if (light_alpha) {
        t["light_alpha"] = *light_alpha;
    }
    if (light_twosided) {
        t["light_twosided"] = *light_twosided;
    }
    if (lightcolorscale != 1.0) {
        t["lightcolorscale"] = lightcolorscale;
    }
    if (surflight_group) {
        t["surflight_group"] = surflight_group;
    }
    if (world_units_per_luxel) {
        t["world_units_per_luxel"] = *world_units_per_luxel;
    }
    if (object_channel_mask) {
        t["object_channel_mask"] = *object_channel_mask;
    }

    return t;
}

surfflags_t surfflags_t::from_json(const nlohmann::json &val)
{
    surfflags_t flags;

    if (val.contains("is_nodraw")) {
        flags.set_nodraw(val.at("is_nodraw").get<bool>());
    }
    if (val.contains("is_hint")) {
        flags.set_hint(val.at("is_hint").get<bool>());
    }
    if (val.contains("is_hintskip")) {
        flags.set_hintskip(val.at("is_hintskip").get<bool>());
    }
    if (val.contains("no_dirt")) {
        flags.no_dirt = val.at("no_dirt").get<bool>();
    }
    if (val.contains("no_shadow")) {
        flags.no_shadow = val.at("no_shadow").get<bool>();
    }
    if (val.contains("no_bounce")) {
        flags.no_bounce = val.at("no_bounce").get<bool>();
    }
    if (val.contains("no_minlight")) {
        flags.no_minlight = val.at("no_minlight").get<bool>();
    }
    if (val.contains("no_expand")) {
        flags.no_expand = val.at("no_expand").get<bool>();
    }
    if (val.contains("light_ignore")) {
        flags.light_ignore = val.at("light_ignore").get<bool>();
    }
    if (val.contains("surflight_rescale")) {
        flags.surflight_rescale = val.at("surflight_rescale").get<bool>();
    }
    if (val.contains("surflight_style")) {
        flags.surflight_style = val.at("surflight_style").get<int32_t>();
    }
    if (val.contains("surflight_targetname")) {
        flags.surflight_targetname = val.at("surflight_targetname").get<std::string>();
    }
    if (val.contains("surflight_color")) {
        flags.surflight_color = val.at("surflight_color").get<qvec3b>();
    }
    if (val.contains("surflight_minlight_scale")) {
        flags.surflight_minlight_scale = val.at("surflight_minlight_scale").get<float>();
    }
    if (val.contains("surflight_atten")) {
        flags.surflight_atten = val.at("surflight_atten").get<float>();
    }
    if (val.contains("phong_angle")) {
        flags.phong_angle = val.at("phong_angle").get<float>();
    }
    if (val.contains("phong_angle_concave")) {
        flags.phong_angle_concave = val.at("phong_angle_concave").get<float>();
    }
    if (val.contains("phong_group")) {
        flags.phong_group = val.at("phong_group").get<int>();
    }
    if (val.contains("minlight")) {
        flags.minlight = val.at("minlight").get<float>();
    }
    if (val.contains("maxlight")) {
        flags.maxlight = val.at("maxlight").get<float>();
    }
    if (val.contains("minlight_color")) {
        flags.minlight_color = val.at("minlight_color").get<qvec3b>();
    }
    if (val.contains("light_alpha")) {
        flags.light_alpha = val.at("light_alpha").get<float>();
    }
    if (val.contains("light_twosided")) {
        flags.light_twosided = val.at("light_twosided").get<bool>();
    }
    if (val.contains("lightcolorscale")) {
        flags.lightcolorscale = val.at("lightcolorscale").get<float>();
    }
    if (val.contains("surflight_group")) {
        flags.surflight_group = val.at("surflight_group").get<int32_t>();
    }
    if (val.contains("world_units_per_luxel")) {
        flags.world_units_per_luxel = val.at("world_units_per_luxel").get<float>();
    }
    if (val.contains("object_channel_mask")) {
        flags.object_channel_mask = val.at("object_channel_mask").get<int32_t>();
    }

    return flags;
}

// gamedef_t

gamedef_t::gamedef_t(const char *friendly_name, const char *default_base_dir)
    : friendly_name(friendly_name),
      default_base_dir(default_base_dir)
{
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
