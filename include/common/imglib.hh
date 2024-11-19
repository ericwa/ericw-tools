/*  Copyright (C) 1996-1997  Id Software, Inc.
Copyright (C) 2017 Eric Wasylishen

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

#include <common/cmdlib.hh>
#include <common/bspfile.hh>
#include <common/qvec.hh>
#include <common/fs.hh>

namespace img
{
enum class ext
{
    TGA,
    WAL,
    MIP,
    /**
     * Anything loadable by stb_image.h
     */
    STB
};

extern std::vector<qvec3b> palette;

// Palette
void init_palette(const gamedef_t *game);

struct texture_meta
{
    std::string name;
    uint32_t width = 0, height = 0;

    // extension that we pulled the pixels in from.
    std::optional<ext> extension;

    // so .json metadata can set an emissive color when we don't have
    // texture data. Also useful to override the emissive color
    std::optional<qvec3b> color_override;

    // Q2/WAL only
    surfflags_t flags{};
    uint32_t contents_native = 0;
    int32_t value = 0;
    std::string animation;
};

struct texture
{
    texture_meta meta{};

    // in the case of replacement textures, these may not
    // the width/height of the metadata.
    uint32_t width = 0, height = 0;

    // RGBA order
    std::vector<qvec4b> pixels;

    // the scale required to map a pixel from the
    // meta data onto the real size (16x16 onto 32x32 -> 2)
    float width_scale = 1, height_scale = 1;

    // This member is only set before insertion into the table
    // and not calculated by individual load functions.
    qvec3b averageColor{0};
};

extern std::unordered_map<std::string, texture, case_insensitive_hash, case_insensitive_equal> textures;

// clears the texture cache
void clear();

qvec3b calculate_average(const std::vector<qvec4b> &pixels);

const texture *find(std::string_view str);

// Load wal
std::optional<texture> load_wal(std::string_view name, const fs::data &file, bool meta_only, const gamedef_t *game);

// Load Quake/Half Life mip (raw data)
std::optional<texture> load_mip(std::string_view name, const fs::data &file, bool meta_only, const gamedef_t *game);

// stb_image.h loaders
std::optional<texture> load_stb(std::string_view name, const fs::data &file, bool meta_only, const gamedef_t *game);

// list of supported extensions and their loaders
struct extension_info_t
{
    const char *suffix;
    ext id;
    decltype(load_wal) *loader;
};

constexpr extension_info_t extension_list[] = {{".png", ext::STB, load_stb}, {".jpg", ext::STB, load_stb},
    {".tga", ext::TGA, load_stb}, {".wal", ext::WAL, load_wal}, {".mip", ext::MIP, load_mip}, {"", ext::MIP, load_mip}};

// Attempt to load a texture from the specified name.
std::tuple<std::optional<texture>, fs::resolve_result, fs::data> load_texture(std::string_view name, bool meta_only,
    const gamedef_t *game, const settings::common_settings &options, bool no_prefix = false, bool mip_only = false);

enum class meta_ext
{
    WAL,
    WAL_JSON
};

// Load wal
std::optional<texture_meta> load_wal_meta(std::string_view name, const fs::data &file, const gamedef_t *game);

std::optional<texture_meta> load_wal_json_meta(std::string_view name, const fs::data &file, const gamedef_t *game);

// list of supported meta extensions and their loaders
constexpr struct
{
    const char *suffix;
    meta_ext id;
    decltype(load_wal_meta) *loader;
} meta_extension_list[] = {
    {".wal_json", meta_ext::WAL_JSON, load_wal_json_meta}, {".wal", meta_ext::WAL, load_wal_meta}};

// Attempt to load a texture meta from the specified name.
std::tuple<std::optional<texture_meta>, fs::resolve_result, fs::data> load_texture_meta(
    std::string_view name, const gamedef_t *game, const settings::common_settings &options);

// Loads textures referenced by the bsp into the texture cache.
void load_textures(const mbsp_t *bsp, const settings::common_settings &options);
}; // namespace img
