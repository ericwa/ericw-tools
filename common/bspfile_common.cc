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

bool contentflags_t::types_equal(contentflags_t other) const
{
    return get_content_type() == other.get_content_type();
}

bool contentflags_t::is_any_detail() const
{
    return (flags & EWT_CFLAG_DETAIL) != 0;
}

bool contentflags_t::is_detail_solid() const
{
    return (flags & EWT_CFLAG_DETAIL) && (flags & EWT_VISCONTENTS_SOLID);
}

bool contentflags_t::is_detail_wall() const
{
    // FIXME: this seems off, should be a visible contents check?
    if (flags & EWT_VISCONTENTS_SOLID) {
        return false;
    }

    return (flags & EWT_CFLAG_DETAIL) && (flags & EWT_VISCONTENTS_DETAIL_WALL);
}

bool contentflags_t::is_detail_fence() const
{
    // FIXME: this seems off, should be a visible contents check?
    if (flags & EWT_VISCONTENTS_SOLID) {
        return false;
    }

    return (flags & EWT_CFLAG_DETAIL) && (flags & EWT_VISCONTENTS_WINDOW);
}

bool contentflags_t::is_detail_illusionary() const
{
    // FIXME: this seems off, should be a visible contents check?
    if (flags & EWT_VISCONTENTS_SOLID) {
        return false;
    }

    return (flags & EWT_CFLAG_DETAIL) && (flags & (EWT_VISCONTENTS_MIST | EWT_VISCONTENTS_AUX));
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

contentflags_t &contentflags_t::set_clips_same_type(const std::optional<bool> &clips_same_type_value)
{
    if (clips_same_type_value) {
        if (!*clips_same_type_value) {
            *this = contentflags_t::make(flags | EWT_CFLAG_SUPPRESS_CLIPPING_SAME_TYPE);
        }
    }
    return *this;
}

bool contentflags_t::is_empty() const
{
    return !get_content_type();
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

bool contentflags_t::is_liquid() const
{
    contents_int_t visibleflags = visible_contents().flags;

    if (visibleflags & EWT_INVISCONTENTS_AREAPORTAL)
        return true; // HACK: treat areaportal as a liquid for the purposes of the CSG code

    return (visibleflags & EWT_ALL_LIQUIDS) != 0;
}

bool contentflags_t::is_clip() const
{
    return (flags & (EWT_INVISCONTENTS_PLAYERCLIP | EWT_INVISCONTENTS_MONSTERCLIP)) != 0;
}

bool contentflags_t::is_origin() const
{
    return (flags & EWT_INVISCONTENTS_ORIGIN) != 0;
}

bool contentflags_t::is_opaque(const gamedef_t *game, bool transwater) const
{
    if (visible_contents().flags == EWT_VISCONTENTS_EMPTY)
        return false;

    // it's visible..

    if (flags & EWT_CFLAG_TRANSLUCENT) {
        return false;
    }

    return true;
}

bool contentflags_t::is_fence() const
{
    return is_detail_fence() || is_detail_illusionary();
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

contentflags_t contentflags_t::combine_contents(contentflags_t a, contentflags_t b)
{
    contents_int_t bits_a = a.flags;
    contents_int_t bits_b = b.flags;

    // structural solid eats detail flags
    if (a.is_solid() || b.is_solid()) {
        bits_a &= ~(EWT_CFLAG_DETAIL | EWT_CFLAG_TRANSLUCENT);
        bits_b &= ~(EWT_CFLAG_DETAIL | EWT_CFLAG_TRANSLUCENT);
    }
    if (a.is_sky() || b.is_sky()) {
        bits_a &= ~(EWT_CFLAG_DETAIL | EWT_CFLAG_TRANSLUCENT);
        bits_b &= ~(EWT_CFLAG_DETAIL | EWT_CFLAG_TRANSLUCENT);
    }
    if ((a.flags & EWT_VISCONTENTS_ILLUSIONARY_VISBLOCKER) || (b.flags & EWT_VISCONTENTS_ILLUSIONARY_VISBLOCKER)) {
        // strip out detail flag, otherwise it breaks the visblocker feature
        bits_a &= ~(EWT_CFLAG_DETAIL | EWT_CFLAG_TRANSLUCENT);
        bits_b &= ~(EWT_CFLAG_DETAIL | EWT_CFLAG_TRANSLUCENT);
    }

    return contentflags_t::make(bits_a | bits_b);
}

bool contentflags_t::portal_can_see_through(contentflags_t contents0, contentflags_t contents1)
{
    contents_int_t c0 = contents0.flags, c1 = contents1.flags;

    // can't see through solid
    if ((c0 & EWT_VISCONTENTS_SOLID) || (c1 & EWT_VISCONTENTS_SOLID)) {
        return false;
    }

    if (((c0 ^ c1) & EWT_ALL_VISIBLE_CONTENTS) == 0)
        return true;

    if ((c0 & EWT_CFLAG_TRANSLUCENT) || (c0 & EWT_CFLAG_DETAIL)) {
        c0 = 0;
    }
    if ((c1 & EWT_CFLAG_TRANSLUCENT) || (c1 & EWT_CFLAG_DETAIL)) {
        c1 = 0;
    }

    // identical on both sides
    if (!(c0 ^ c1))
        return true;

    return (((c0 ^ c1) & EWT_ALL_VISIBLE_CONTENTS) == 0);
}

contentflags_t contentflags_t::portal_visible_contents(contentflags_t a, contentflags_t b)
{
    auto bits_a = a.flags;
    auto bits_b = b.flags;

    // aviods spamming "sides not found" warning on Q1 maps with sky
    if ((bits_a & (EWT_VISCONTENTS_SOLID | EWT_VISCONTENTS_SKY)) &&
        (bits_b & (EWT_VISCONTENTS_SOLID | EWT_VISCONTENTS_SKY)))
        return contentflags_t::make(EWT_VISCONTENTS_EMPTY);

    contents_int_t result;

    if ((bits_a & EWT_CFLAG_SUPPRESS_CLIPPING_SAME_TYPE) || (bits_b & EWT_CFLAG_SUPPRESS_CLIPPING_SAME_TYPE)) {
        result = bits_a | bits_b;
    } else {
        result = bits_a ^ bits_b;
    }

    auto strongest_contents_change = contentflags_t::make(result).visible_contents();

    return strongest_contents_change;
}

bool contentflags_t::portal_generates_face(
    contentflags_t portal_visible_contents, contentflags_t brushcontents, planeside_t brushside_side)
{
    auto bits_portal = portal_visible_contents.flags;
    auto bits_brush = brushcontents.flags;

    // find the highest visible content bit set in portal
    int32_t index = portal_visible_contents.visible_contents_index();
    if (index == -1) {
        return false;
    }

    // check if it's not set in the brush
    if (!(bits_brush & nth_bit(index))) {
        return false;
    }

    if (brushside_side == SIDE_BACK) {
        // explicit override?
        if (bits_brush & EWT_CFLAG_MIRROR_INSIDE_SET) {
            return (bits_brush & EWT_CFLAG_MIRROR_INSIDE) != 0;
        }
        if (portal_visible_contents.flags &
            (EWT_VISCONTENTS_WINDOW | EWT_VISCONTENTS_AUX | EWT_VISCONTENTS_DETAIL_WALL)) {
            // windows or aux don't generate inside faces
            return false;
        }
        // other types get mirrored by default
        return true;
    }
    return true;
}

std::string contentflags_t::to_string() const
{
    std::string s = get_contents_display(flags);

    return s;
}

Json::Value contentflags_t::to_json() const
{
    return get_contents_json(flags);
}

contentflags_t contentflags_t::from_json(const Json::Value &json)
{
    return contentflags_t::make(set_contents_json(json));
}

std::ostream &operator<<(std::ostream &os, contents_t flags)
{
    os << get_contents_display(flags);
    return os;
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

Json::Value surfflags_t::to_json() const
{
    Json::Value t = Json::Value(Json::objectValue);

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
        t["surflight_color"] = ::to_json(*surflight_color);
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
        t["minlight_color"] = ::to_json(minlight_color);
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

surfflags_t surfflags_t::from_json(const Json::Value &val)
{
    surfflags_t flags;

    // native q2 flags
    if (val.isMember("is_light") && val["is_light"].asBool()) {
        flags.set_native_q2_bits(Q2_SURF_LIGHT);
    }
    if (val.isMember("is_slick") && val["is_slick"].asBool()) {
        flags.set_native_q2_bits(Q2_SURF_SLICK);
    }
    if (val.isMember("is_sky") && val["is_sky"].asBool()) {
        flags.set_native_q2_bits(Q2_SURF_SKY);
    }
    if (val.isMember("is_warp") && val["is_warp"].asBool()) {
        flags.set_native_q2_bits(Q2_SURF_WARP);
    }
    if (val.isMember("is_trans33") && val["is_trans33"].asBool()) {
        flags.set_native_q2_bits(Q2_SURF_TRANS33);
    }
    if (val.isMember("is_trans66") && val["is_trans66"].asBool()) {
        flags.set_native_q2_bits(Q2_SURF_TRANS66);
    }
    if (val.isMember("is_flowing") && val["is_flowing"].asBool()) {
        flags.set_native_q2_bits(Q2_SURF_FLOWING);
    }
    if (val.isMember("is_nodraw") && val["is_nodraw"].asBool()) {
        flags.set_native_q2_bits(Q2_SURF_NODRAW);
    }
    if (val.isMember("is_hint") && val["is_hint"].asBool()) {
        flags.set_native_q2_bits(Q2_SURF_HINT);
    }
    if (val.isMember("is_skip") && val["is_skip"].asBool()) {
        flags.set_native_q2_bits(Q2_SURF_SKIP);
    }
    if (val.isMember("is_alphatest") && val["is_alphatest"].asBool()) {
        flags.set_native_q2_bits(Q2_SURF_ALPHATEST);
    }

    // native q1 flags
    if (val.isMember("is_special") && val["is_special"].asBool()) {
        flags.set_native_q1_bits(TEX_SPECIAL);
    }

    // extended flags
    if (val.isMember("is_nodraw")) {
        flags.set_nodraw(val["is_nodraw"].asBool());
    }
    if (val.isMember("is_hint")) {
        flags.set_hint(val["is_hint"].asBool());
    }
    if (val.isMember("is_hintskip")) {
        flags.set_hintskip(val["is_hintskip"].asBool());
    }
    if (val.isMember("no_dirt")) {
        flags.no_dirt = val["no_dirt"].asBool();
    }
    if (val.isMember("no_shadow")) {
        flags.no_shadow = val["no_shadow"].asBool();
    }
    if (val.isMember("no_bounce")) {
        flags.no_bounce = val["no_bounce"].asBool();
    }
    if (val.isMember("no_minlight")) {
        flags.no_minlight = val["no_minlight"].asBool();
    }
    if (val.isMember("no_expand")) {
        flags.no_expand = val["no_expand"].asBool();
    }
    if (val.isMember("light_ignore")) {
        flags.light_ignore = val["light_ignore"].asBool();
    }
    if (val.isMember("noambient")) {
        flags.noambient = val["noambient"].asBool();
    }
    if (val.isMember("surflight_rescale")) {
        flags.surflight_rescale = val["surflight_rescale"].asBool();
    }
    if (val.isMember("surflight_style")) {
        flags.surflight_style = val["surflight_style"].asInt();
    }
    if (val.isMember("surflight_targetname")) {
        flags.surflight_targetname = val["surflight_targetname"].asString();
    }
    if (val.isMember("surflight_color")) {
        flags.surflight_color = to_qvec3b(val["surflight_color"]);
    }
    if (val.isMember("surflight_minlight_scale")) {
        flags.surflight_minlight_scale = val["surflight_minlight_scale"].asFloat();
    }
    if (val.isMember("surflight_atten")) {
        flags.surflight_atten = val["surflight_atten"].asFloat();
    }
    if (val.isMember("phong_angle")) {
        flags.phong_angle = val["phong_angle"].asFloat();
    }
    if (val.isMember("phong_angle_concave")) {
        flags.phong_angle_concave = val["phong_angle_concave"].asFloat();
    }
    if (val.isMember("phong_group")) {
        flags.phong_group = val["phong_group"].asInt();
    }
    if (val.isMember("minlight")) {
        flags.minlight = val["minlight"].asFloat();
    }
    if (val.isMember("maxlight")) {
        flags.maxlight = val["maxlight"].asFloat();
    }
    if (val.isMember("minlight_color")) {
        flags.minlight_color = to_qvec3b(val["minlight_color"]);
    }
    if (val.isMember("light_alpha")) {
        flags.light_alpha = val["light_alpha"].asFloat();
    }
    if (val.isMember("light_twosided")) {
        flags.light_twosided = val["light_twosided"].asBool();
    }
    if (val.isMember("lightcolorscale")) {
        flags.lightcolorscale = val["lightcolorscale"].asFloat();
    }
    if (val.isMember("surflight_group")) {
        flags.surflight_group = val["surflight_group"].asInt();
    }
    if (val.isMember("world_units_per_luxel")) {
        flags.world_units_per_luxel = val["world_units_per_luxel"].asFloat();
    }
    if (val.isMember("object_channel_mask")) {
        flags.object_channel_mask = val["object_channel_mask"].asInt();
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

    Json::Value j;

    texinfofile >> j;

    Json::Value::Members keys = j.getMemberNames();
    for (const std::string &key : keys) {
        size_t index = std::stoull(key);

        if (index >= bsp->texinfo.size()) {
            logging::print("WARNING: Extended texinfo flags in {} does not match bsp, ignoring\n", filename);
            memset(result.data(), 0, bsp->texinfo.size() * sizeof(surfflags_t));
            return result;
        }

        const Json::Value &val = j[key];
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

    Json::Value j;
    texinfofile >> j;

    if (!j.isArray() || j.size() != bsp->dleafs.size()) {
        logging::print("ERROR: malformed extended content flags file\n");
        return result;
    }

    for (Json::Value::ArrayIndex i = 0; i < bsp->dleafs.size(); ++i) {
        const auto &elem = j[i];

        result[i] = contentflags_t::from_json(elem);
    }

    return result;
}

// content_stats_t

void content_stats_t::count_contents_in_stats(contentflags_t contents)
{
    {
        std::unique_lock lock(stat_mutex);
        native_types[contents.flags]++;
    }

    ++total_brushes;
}

void content_stats_t::print_content_stats(const char *what) const
{
    logging::stat_tracker_t stat_print;

    for (auto [bits, count] : native_types) {
        auto c = contentflags_t{.flags = bits};
        stat_print.register_stat(fmt::format("{} {}", get_contents_display(c.flags), what)).count += count;
    }

    stat_print.register_stat(fmt::format("{} total", what)).count += total_brushes;
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
