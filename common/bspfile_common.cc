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

#include <fstream>
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

bool contentflags_t::is_any_detail() const
{
    return (flags & EWT_CFLAG_DETAIL) != 0;
}

bool contentflags_t::is_detail_solid() const
{
    return (flags & EWT_CFLAG_DETAIL) && (flags & EWT_VISCONTENTS_SOLID);
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

bool contentflags_t::is_any_solid() const
{
    return (flags & EWT_VISCONTENTS_SOLID) != 0;
}

bool contentflags_t::is_solid() const
{
    return (flags & EWT_VISCONTENTS_SOLID) && !(flags & EWT_CFLAG_DETAIL);
}

bool contentflags_t::is_sky() const
{
    return (flags & EWT_VISCONTENTS_SKY) != 0;
}

bool contentflags_t::is_liquid(const gamedef_t *game) const
{
    return game->contents_are_liquid(*this);
}

bool contentflags_t::is_valid(const gamedef_t *game, bool strict) const
{
    return game->contents_are_valid(*this, strict);
}

bool contentflags_t::is_clip() const
{
    return (flags & (EWT_INVISCONTENTS_PLAYERCLIP | EWT_INVISCONTENTS_MONSTERCLIP)) != 0;
}

bool contentflags_t::is_origin() const
{
    return (flags & EWT_INVISCONTENTS_ORIGIN) != 0;
}

void contentflags_t::make_valid(const gamedef_t *game)
{
    game->contents_make_valid(*this);
}

bool contentflags_t::is_fence(const gamedef_t *game) const
{
    return is_detail_fence(game) || is_detail_illusionary(game);
}

contentflags_t contentflags_t::cluster_contents(contentflags_t other) const
{
    contents_int_t combined = this->flags | other.flags;

    // a cluster may include some solid detail areas, but
    // still be seen into
    if (!(this->flags & EWT_VISCONTENTS_SOLID) || !(other.flags & EWT_VISCONTENTS_SOLID)) {
        combined &= ~EWT_VISCONTENTS_SOLID;
    }

    return contentflags_t::make(combined);
}


std::string contentflags_t::to_string() const
{
    std::string s = get_contents_display(flags);

    return s;
}

nlohmann::json contentflags_t::to_json() const
{
    return get_contents_json(flags);
}

contentflags_t contentflags_t::from_json(const nlohmann::json &json)
{
    return contentflags_t::make(set_contents_json(json));
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

bool surfflags_t::is_valid(const gamedef_t *game) const
{
    return game->surfflags_are_valid(*this);
}

void surfflags_t::set_native_q1_bits(q1_surf_flags_t bits)
{
    native_q1 = static_cast<q1_surf_flags_t>(native_q1 | bits);
}

void surfflags_t::set_native_q2_bits(q2_surf_flags_t bits)
{
    native_q2 = static_cast<q2_surf_flags_t>(native_q2 | bits);
}

nlohmann::json surfflags_t::to_json() const
{
    nlohmann::json t = nlohmann::json::object();

    // native q2 flags
    if (native_q2 & Q2_SURF_LIGHT) {
        t["is_light"] = true;
    }
    if (native_q2 & Q2_SURF_SLICK) {
        t["is_slick"] = true;
    }
    if (native_q2 & Q2_SURF_SKY) {
        t["is_sky"] = true;
    }
    if (native_q2 & Q2_SURF_WARP) {
        t["is_warp"] = true;
    }
    if (native_q2 & Q2_SURF_TRANS33) {
        t["is_trans33"] = true;
    }
    if (native_q2 & Q2_SURF_TRANS66) {
        t["is_trans66"] = true;
    }
    if (native_q2 & Q2_SURF_FLOWING) {
        t["is_flowing"] = true;
    }
    if (native_q2 & Q2_SURF_NODRAW) {
        t["is_nodraw"] = true;
    }
    if (native_q2 & Q2_SURF_HINT) {
        t["is_hint"] = true;
    }
    if (native_q2 & Q2_SURF_SKIP) {
        t["is_skip"] = true;
    }
    if (native_q2 & Q2_SURF_ALPHATEST) {
        t["is_alphatest"] = true;
    }

    // native q1 flags
    if (native_q1 & TEX_SPECIAL) {
        t["is_special"] = true;
    }

    // extended flags
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
    if (noambient) {
        t["noambient"] = noambient;
    }
    if (surflight_rescale) {
        t["surflight_rescale"] = *surflight_rescale;
    }
    if (surflight_style) {
        t["surflight_style"] = *surflight_style;
    }
    if (surflight_targetname) {
        t["surflight_targetname"] = *surflight_targetname;
    }
    if (surflight_color) {
        t["surflight_color"] = *surflight_color;
    }
    if (surflight_minlight_scale) {
        t["surflight_minlight_scale"] = *surflight_minlight_scale;
    }
    if (surflight_atten) {
        t["surflight_atten"] = *surflight_atten;
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
    if (!qv::emptyExact(minlight_color)) {
        t["minlight_color"] = minlight_color;
    }
    if (light_alpha) {
        t["light_alpha"] = *light_alpha;
    }
    if (light_twosided) {
        t["light_twosided"] = *light_twosided;
    }
    if (maxlight) {
        t["maxlight"] = maxlight;
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

    // native q2 flags
    if (val.contains("is_light") && val.at("is_light").get<bool>()) {
        flags.set_native_q2_bits(Q2_SURF_LIGHT);
    }
    if (val.contains("is_slick") && val.at("is_slick").get<bool>()) {
        flags.set_native_q2_bits(Q2_SURF_SLICK);
    }
    if (val.contains("is_sky") && val.at("is_sky").get<bool>()) {
        flags.set_native_q2_bits(Q2_SURF_SKY);
    }
    if (val.contains("is_warp") && val.at("is_warp").get<bool>()) {
        flags.set_native_q2_bits(Q2_SURF_WARP);
    }
    if (val.contains("is_trans33") && val.at("is_trans33").get<bool>()) {
        flags.set_native_q2_bits(Q2_SURF_TRANS33);
    }
    if (val.contains("is_trans66") && val.at("is_trans66").get<bool>()) {
        flags.set_native_q2_bits(Q2_SURF_TRANS66);
    }
    if (val.contains("is_flowing") && val.at("is_flowing").get<bool>()) {
        flags.set_native_q2_bits(Q2_SURF_FLOWING);
    }
    if (val.contains("is_nodraw") && val.at("is_nodraw").get<bool>()) {
        flags.set_native_q2_bits(Q2_SURF_NODRAW);
    }
    if (val.contains("is_hint") && val.at("is_hint").get<bool>()) {
        flags.set_native_q2_bits(Q2_SURF_HINT);
    }
    if (val.contains("is_skip") && val.at("is_skip").get<bool>()) {
        flags.set_native_q2_bits(Q2_SURF_SKIP);
    }
    if (val.contains("is_alphatest") && val.at("is_alphatest").get<bool>()) {
        flags.set_native_q2_bits(Q2_SURF_ALPHATEST);
    }

    // native q1 flags
    if (val.contains("is_special") && val.at("is_special").get<bool>()) {
        flags.set_native_q1_bits(TEX_SPECIAL);
    }

    // extended flags
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
    if (val.contains("noambient")) {
        flags.noambient = val.at("noambient").get<bool>();
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

std::vector<surfflags_t> LoadExtendedTexinfoFlags(const fs::path &sourcefilename, const mbsp_t *bsp)
{
    std::vector<surfflags_t> result;

    // always create the zero'ed array
    result.resize(bsp->texinfo.size());

    fs::path filename(sourcefilename);
    filename.replace_extension("texinfo.json");

    std::ifstream texinfofile(filename, std::ios_base::in | std::ios_base::binary);

    if (!texinfofile)
        return result;

    logging::print("Loading extended texinfo flags from {}...\n", filename);

    json j;

    texinfofile >> j;

    for (auto it = j.begin(); it != j.end(); ++it) {
        size_t index = std::stoull(it.key());

        if (index >= bsp->texinfo.size()) {
            logging::print("WARNING: Extended texinfo flags in {} does not match bsp, ignoring\n", filename);
            memset(result.data(), 0, bsp->texinfo.size() * sizeof(surfflags_t));
            return result;
        }

        auto &val = it.value();
        result[index] = surfflags_t::from_json(val);
    }

    return result;
}

std::vector<contentflags_t> LoadExtendedContentFlags(const fs::path &sourcefilename, const mbsp_t *bsp)
{
    std::vector<contentflags_t> result;

    // initialize with the contents from the .bsp, in case the .json file is missing
    result.resize(bsp->dleafs.size());
    for (size_t i = 0; i < bsp->dleafs.size(); ++i) {
        result[i] = bsp->loadversion->game->create_contents_from_native(bsp->dleafs[i].contents);
    }

    fs::path filename(sourcefilename);
    filename.replace_extension("content.json");

    std::ifstream texinfofile(filename, std::ios_base::in | std::ios_base::binary);

    if (!texinfofile)
        return result;

    logging::print("Loading extended content flags from {}...\n", filename);

    json j;
    texinfofile >> j;

    if (!j.is_array() || j.size() != bsp->dleafs.size()) {
        logging::print("ERROR: malformed extended content flags file\n");
        return result;
    }

    for (size_t i = 0; i < bsp->dleafs.size(); ++i) {
        const auto &elem = j[i];

        result[i] = contentflags_t::from_json(elem);
    }

    return result;
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
