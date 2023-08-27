#include <vector>
#include <common/fs.hh>
#include <common/imglib.hh>
#include <common/entdata.h>
#include <common/json.hh>
#include <common/log.hh>
#include <common/settings.hh>

#define STB_IMAGE_IMPLEMENTATION
#include "../3rdparty/stb_image.h"

/*
============================================================================
PALETTE
============================================================================
*/

namespace img
{
// current palette
std::vector<qvec3b> palette;

/*
============================================================================
PCX IMAGE
Only used for palette here.
============================================================================
*/
struct pcx_t
{
    int8_t manufacturer;
    int8_t version;
    int8_t encoding;
    int8_t bits_per_pixel;
    uint16_t xmin, ymin, xmax, ymax;
    uint16_t hres, vres;
    padding<49> palette_reserved;
    int8_t color_planes;
    uint16_t bytes_per_line;
    uint16_t palette_type;
    padding<58> filler;

    auto stream_data()
    {
        return std::tie(manufacturer, version, encoding, bits_per_pixel, xmin, ymin, xmax, ymax, hres, vres,
            palette_reserved, color_planes, bytes_per_line, palette_type, filler);
    }
};

static bool LoadPCXPalette(const fs::path &filename, std::vector<qvec3b> &palette)
{
    auto file = fs::load(filename);

    if (!file || !file->size()) {
        logging::funcprint("Failed to load '{}'.\n", filename);
        return false;
    }

    imemstream stream(file->data(), file->size(), std::ios_base::in | std::ios_base::binary);
    stream >> endianness<std::endian::little>;

    // Parse the PCX file
    pcx_t pcx;
    stream >= pcx;

    if (pcx.manufacturer != 0x0a || pcx.version != 5 || pcx.encoding != 1 || pcx.bits_per_pixel != 8) {
        logging::funcprint("Failed to load '{}'. Unsupported PCX file.\n", filename);
        return false;
    }

    palette.resize(256);

    stream.seekg(file->size() - 768);
    stream.read(reinterpret_cast<char *>(palette.data()), 768);

    return true;
}

void init_palette(const gamedef_t *game)
{
    palette.clear();

    // Load game-specific palette palette
    if (game->id == GAME_QUAKE_II) {
        constexpr const char *colormap = "pics/colormap.pcx";

        if (LoadPCXPalette(colormap, palette)) {
            return;
        }
    }

    logging::print("INFO: using built-in palette.\n");

    auto &pal = game->get_default_palette();

    std::copy(pal.begin(), pal.end(), std::back_inserter(palette));
}

static void convert_paletted_to_32_bit(
    const std::vector<uint8_t> &pixels, std::vector<qvec4b> &output, const std::vector<qvec3b> &pal)
{
    output.resize(pixels.size());

    for (size_t i = 0; i < pixels.size(); i++) {
        // Last palette index is transparent color
        output[i] = qvec4b(pal[pixels[i]], pixels[i] == 255 ? 0 : 255);
    }
}

/*
============================================================================
WAL IMAGE
============================================================================
*/
struct q2_miptex_t
{
    std::array<char, 32> name;
    uint32_t width, height;
    std::array<uint32_t, MIPLEVELS> offsets; // four mip maps stored
    std::array<char, 32> animname; // next frame in animation chain
    int32_t flags;
    int32_t contents;
    int32_t value;

    auto stream_data() { return std::tie(name, width, height, offsets, animname, flags, contents, value); }
};

std::optional<texture> load_wal(
    const std::string_view &name, const fs::data &file, bool meta_only, const gamedef_t *game)
{
    imemstream stream(file->data(), file->size(), std::ios_base::in | std::ios_base::binary);
    stream >> endianness<std::endian::little>;

    // Parse WAL
    q2_miptex_t mt;
    stream >= mt;

    texture tex;

    tex.meta.extension = ext::WAL;

    // note: this is a bit of a hack, but the name stored in
    // the .wal is ignored. it's extraneous and well-formed wals
    // will all match up anyways.
    tex.meta.name = name;
    tex.meta.width = tex.width = mt.width;
    tex.meta.height = tex.height = mt.height;
    tex.meta.contents = {mt.contents};
    tex.meta.flags = {mt.flags};
    tex.meta.value = mt.value;
    tex.meta.animation = mt.animname.data();

    if (!meta_only) {
        stream.seekg(mt.offsets[0]);
        std::vector<uint8_t> pixels(mt.width * mt.height);
        stream.read(reinterpret_cast<char *>(pixels.data()), pixels.size());
        convert_paletted_to_32_bit(pixels, tex.pixels, palette);
    }

    return tex;
}

/*
============================================================================
Quake/Half Life MIP
============================================================================
*/

std::optional<texture> load_mip(
    const std::string_view &name, const fs::data &file, bool meta_only, const gamedef_t *game)
{
    imemstream stream(file->data(), file->size());
    stream >> endianness<std::endian::little>;

    // read header
    dmiptex_t header;
    stream >= header;

    // must be able to at least read the header
    if (!stream) {
        logging::funcprint("Failed to fully load mip {}. Header incomplete.\n", name);
        return std::nullopt;
    }

    texture tex;

    tex.meta.extension = ext::MIP;

    // note: this is a bit of a hack, but the name stored in
    // the mip is ignored. it's extraneous and well-formed mips
    // will all match up anyways.
    tex.meta.name = name;
    tex.meta.width = tex.width = header.width;
    tex.meta.height = tex.height = header.height;

    if (!meta_only) {
        // miptex only has meta
        if (header.offsets[0] <= 0) {
            return tex;
        }

        // convert the data into RGBA.
        // sanity check
        if (header.offsets[0] + (header.width * header.height) > file->size()) {
            logging::funcprint("mip offset0 overrun for {}\n", name);
            return tex;
        }

        // fetch the full data for the first mip
        stream.seekg(header.offsets[0]);
        std::vector<uint8_t> pixels(header.width * header.height);
        stream.read(reinterpret_cast<char *>(pixels.data()), pixels.size());

        // Half Life will have a palette of 256 colors in a specific spot
        // so use that instead of game-specific palette.
        // FIXME: to support these palettes in other games we'd need to
        // maybe pass through the archive it's loaded from. if it's a WAD3
        // we can safely make the next assumptions, but WAD2s might have wildly
        // different data after the mips...
        if (game->id == GAME_HALF_LIFE) {
            bool valid_mip_palette = true;

            int32_t mip3_size = (header.width >> 3) * (header.height >> 3);
            size_t palette_size = sizeof(uint16_t) + (sizeof(qvec3b) * 256);

            if (header.offsets[3] <= 0) {
                logging::funcprint("mip palette needs offset3 to work, for {}\n", name);
                valid_mip_palette = false;
            } else if (header.offsets[3] + mip3_size + palette_size > file->size()) {
                logging::funcprint("mip palette overrun for {}\n", name);
                valid_mip_palette = false;
            }

            if (valid_mip_palette) {
                stream.seekg(header.offsets[3] + mip3_size);

                uint16_t num_colors;
                stream >= num_colors;

                if (num_colors != 256) {
                    logging::funcprint("mip palette color num should be 256 for {}\n", name);
                    valid_mip_palette = false;
                } else {
                    std::vector<qvec3b> mip_palette(256);
                    stream.read(reinterpret_cast<char *>(mip_palette.data()), mip_palette.size() * sizeof(qvec3b));
                    convert_paletted_to_32_bit(pixels, tex.pixels, mip_palette);
                    return tex;
                }
            }
        }

        convert_paletted_to_32_bit(pixels, tex.pixels, palette);
    }

    return tex;
}

std::optional<texture> load_stb(
    const std::string_view &name, const fs::data &file, bool meta_only, const gamedef_t *game)
{
    int x, y, channels_in_file;
    stbi_uc *rgba_data = stbi_load_from_memory(file->data(), file->size(), &x, &y, &channels_in_file, 4);

    if (!rgba_data) {
        logging::funcprint("stbi error: {}\n", stbi_failure_reason());
        return {};
    }

    texture tex;
    tex.meta.extension = ext::STB;
    tex.meta.name = name;
    tex.meta.width = tex.width = x;
    tex.meta.height = tex.height = y;

    if (!meta_only) {
        int num_pixels = x * y;
        if (num_pixels < 0) {
            return {};
        }

        tex.pixels.resize(num_pixels);

        qvec4b *out = tex.pixels.data();
        for (int i = 0; i < num_pixels; ++i) {
            out[i] = {rgba_data[4 * i], rgba_data[4 * i + 1], rgba_data[4 * i + 2], rgba_data[4 * i + 3]};
        }
    }

    stbi_image_free(rgba_data);

    return tex;
}

// texture cache
std::unordered_map<std::string, texture, case_insensitive_hash, case_insensitive_equal> textures;

const texture *find(const std::string_view &str)
{
    auto it = textures.find(str.data());

    if (it == textures.end()) {
        return nullptr;
    }

    return &it->second;
}

void clear()
{
    textures.clear();
}

qvec3b calculate_average(const std::vector<qvec4b> &pixels)
{
    qvec3d avg{};
    size_t n = 0;

    for (auto &pixel : pixels) {
        // FIXME: is this valid for transparent averages?
        if (pixel[3] >= 127) {
            avg += pixel.xyz();
            n++;
        }
    }

    return avg /= n;
}

std::tuple<std::optional<img::texture>, fs::resolve_result, fs::data> load_texture(const std::string_view &name,
    bool meta_only, const gamedef_t *game, const settings::common_settings &options, bool no_prefix)
{
    fs::path prefix{};

    if (!no_prefix && game->id == GAME_QUAKE_II) {
        prefix = "textures";
    }

    for (auto &ext : img::extension_list) {
        fs::path p = (no_prefix ? fs::path(name) : (prefix / name)) += ext.suffix;

        if (auto pos = fs::where(p, options.filepriority.value() == settings::search_priority_t::LOOSE)) {
            if (auto data = fs::load(pos)) {
                if (auto texture = ext.loader(name.data(), data, meta_only, game)) {
                    return {texture, pos, data};
                }
            }
        }
    }

    return {std::nullopt, {}, {}};
}

std::optional<texture_meta> load_wal_meta(const std::string_view &name, const fs::data &file, const gamedef_t *game)
{
    if (auto tex = load_wal(name, file, true, game)) {
        return tex->meta;
    }

    return std::nullopt;
}

/*
    JSON meta format, meant to supplant .wal's metadata for external texture use.
    All of the values are optional.
    {
        // valid instances of "contents"; either:
        // - a case-insensitive string containing the textual representation
        //   of the content type
        // - a number
        // - an array of the two above, which will be OR'd together
        "contents": [ "SOLID", 8 ],
        "contents": 24,
        "contents": "SOLID",

        // valid instances of "flags"; either:
        // - a case-insensitive string containing the textual representation
        //   of the surface flags
        // - a number
        // - an array of the two above, which will be OR'd together
        "flags": [ "SKY", 16 ],
        "flags": 24,
        "flags": "SKY",

        // "value" must be an integer
        "value": 1234,

        // "animation" must be the name of the next texture in
        // the chain.
        "animation": "e1u1/comp2",

        // width/height are allowed to be supplied in order to
        // have the editor treat the surface as if its dimensions
        // are these rather than the ones pulled in from the image
        // itself. they must be integers.
        "width": 64,
        "height": 64,

        // color to use for lighting bounces. if specified, this
        // is used instead of averaging the pixels of the image.
        "color": [255, 128, 64]
    }
*/
std::optional<texture_meta> load_wal_json_meta(
    const std::string_view &name, const fs::data &file, const gamedef_t *game)
{
    try {
        auto json = json::parse(file->begin(), file->end());

        texture_meta meta{};

        meta.name = name;

        {
            fs::path wal = fs::path(name).replace_extension(".wal");

            if (auto wal_file = fs::load(wal))
                if (auto wal_meta = load_wal_meta(wal.string(), wal_file, game))
                    meta = *wal_meta;
        }

        if (json.contains("width") && json["width"].is_number_integer()) {
            meta.width = json["width"].get<int32_t>();
        }

        if (json.contains("height") && json["height"].is_number_integer()) {
            meta.height = json["height"].get<int32_t>();
        }

        if (json.contains("value") && json["value"].is_number_integer()) {
            meta.value = json["value"].get<int32_t>();
        }

        if (json.contains("contents")) {
            auto &contents = json["contents"];

            if (contents.is_number_integer()) {
                meta.contents.native = contents.get<int32_t>();
            } else if (contents.is_string()) {
                meta.contents.native = game->contents_from_string(contents.get<std::string>());
            } else if (contents.is_array()) {
                for (auto &content : contents) {
                    if (content.is_number_integer()) {
                        meta.contents.native |= content.get<int32_t>();
                    } else if (content.is_string()) {
                        meta.contents.native |= game->contents_from_string(content.get<std::string>());
                    }
                }
            }
        }

        if (json.contains("flags")) {
            auto &flags = json["flags"];

            if (flags.is_number_integer()) {
                meta.flags.native = flags.get<int32_t>();
            } else if (flags.is_string()) {
                meta.flags.native = game->surfflags_from_string(flags.get<std::string>());
            } else if (flags.is_array()) {
                for (auto &flag : flags) {
                    if (flag.is_number_integer()) {
                        meta.flags.native |= flag.get<int32_t>();
                    } else if (flag.is_string()) {
                        meta.flags.native |= game->surfflags_from_string(flag.get<std::string>());
                    }
                }
            }
        }

        if (json.contains("animation") && json["animation"].is_string()) {
            meta.animation = json["animation"].get<std::string>();
        }

        if (json.contains("color")) {
            auto &color = json["color"];

            qvec3b color_vec = {color.at(0).get<int32_t>(), color.at(1).get<int32_t>(), color.at(2).get<int32_t>()};

            meta.color_override = {color_vec};
        }

        return meta;
    } catch (json::exception e) {
        logging::funcprint("{}, invalid JSON: {}\n", name, e.what());
        return std::nullopt;
    }
}

std::tuple<std::optional<img::texture_meta>, fs::resolve_result, fs::data> load_texture_meta(
    const std::string_view &name, const gamedef_t *game, const settings::common_settings &options)
{
    fs::path prefix;

    if (game->id == GAME_QUAKE_II) {
        prefix = "textures";
    }

    for (auto &ext : img::meta_extension_list) {
        fs::path p = (prefix / name) += ext.suffix;

        if (auto pos = fs::where(p, options.filepriority.value() == settings::search_priority_t::LOOSE)) {
            if (auto data = fs::load(pos)) {
                if (auto texture = ext.loader(name.data(), data, game)) {
                    return {texture, pos, data};
                }
            }
        }
    }

    return {std::nullopt, {}, {}};
}

/*
// Add empty to keep texture index in case of load problems...
auto &tex = img::textures.emplace(miptex.name, img::texture{}).first->second;

// try to load it externally first
auto [texture, _0, _1] = img::load_texture(miptex.name, false, bsp->loadversion->game, options);

if (texture) {
    tex = std::move(texture.value());
} else {
    if (miptex.data.size() <= sizeof(dmiptex_t)) {
        logging::funcprint("WARNING: can't find texture {}\n", miptex.name);
        continue;
    }

    auto loaded_tex = img::load_mip(miptex.name, miptex.data, false, bsp->loadversion->game);

    if (!loaded_tex) {
        logging::funcprint("WARNING: Texture {} is invalid\n", miptex.name);
        continue;
    }

    tex = std::move(loaded_tex.value());
}

tex.meta.averageColor = img::calculate_average(tex.pixels);
*/

static qvec3b increase_saturation(const qvec3b &color)
{
    qvec3f color_float = qvec3f(color);
    color_float /= 255.0f;

    // square it to boost saturation
    color_float *= color_float;

    // multiply by 2, then scale back to avoid clipping if needed
    color_float *= 2.0f;

    float max_comp = qv::max(color_float);
    if (max_comp > 1.0f) {
        color_float /= max_comp;
    }

    qvec3b color_int;
    for (int i = 0; i < 3; ++i) {
        color_int[i] = static_cast<uint8_t>(std::clamp(color_float[i] * 255.0f, 0.0f, 255.0f));
    }
    return color_int;
}

// Load the specified texture from the BSP
static void AddTextureName(
    const std::string_view &textureName, const mbsp_t *bsp, const settings::common_settings &options)
{
    if (img::find(textureName)) {
        return;
    }

    // always add entry
    auto &tex = img::textures.emplace(textureName, img::texture{}).first->second;

    // find texture & meta
    auto [texture, _0, _1] = img::load_texture(textureName, false, bsp->loadversion->game, options);

    if (!texture) {
        logging::funcprint("WARNING: can't find pixel data for {}\n", textureName);
    } else {
        tex = std::move(texture.value());
    }

    auto [texture_meta, __0, __1] = img::load_texture_meta(textureName, bsp->loadversion->game, options);

    if (!texture_meta) {
        logging::funcprint("WARNING: can't find meta data for {}\n", textureName);
    } else {
        tex.meta = std::move(texture_meta.value());
    }

    if (tex.meta.color_override) {
        tex.averageColor = *tex.meta.color_override;
    } else {
        tex.averageColor = img::calculate_average(tex.pixels);

        if (options.tex_saturation_boost.value() > 0.0f) {
            tex.averageColor =
                mix(tex.averageColor, increase_saturation(tex.averageColor), options.tex_saturation_boost.value());
        }
    }

    if (tex.meta.width && tex.meta.height) {
        tex.width_scale = (float)tex.width / (float)tex.meta.width;
        tex.height_scale = (float)tex.height / (float)tex.meta.height;
    }
}

// Load all of the referenced textures from the BSP texinfos into
// the texture cache.
static void LoadTextures(const mbsp_t *bsp, const settings::common_settings &options)
{
    // gather all loadable textures...
    for (auto &texinfo : bsp->texinfo) {
        AddTextureName(texinfo.texture.data(), bsp, options);
    }

    // gather textures used by _project_texture.
    // FIXME: I'm sure we can resolve this so we don't parse entdata twice.
    auto entdicts = EntData_Parse(*bsp);
    for (auto &entdict : entdicts) {
        if (entdict.get("classname").find("light") == 0) {
            const auto &tex = entdict.get("_project_texture");
            if (!tex.empty()) {
                AddTextureName(tex.c_str(), bsp, options);
            }
        }
    }
}

// Load all of the paletted textures from the BSP into
// the texture cache.
static void ConvertTextures(const mbsp_t *bsp, const settings::common_settings &options)
{
    if (!bsp->dtex.textures.size()) {
        return;
    }

    for (auto &miptex : bsp->dtex.textures) {
        if (img::find(miptex.name)) {
            logging::funcprint("WARNING: Texture {} duplicated\n", miptex.name);
            continue;
        }

        // always add entry
        auto &tex = img::textures.emplace(miptex.name, img::texture{}).first->second;

        // if the miptex entry isn't a dummy, use it as our base
        if (miptex.data.size() >= sizeof(dmiptex_t)) {
            if (auto loaded_tex = img::load_mip(miptex.name, miptex.data, false, bsp->loadversion->game)) {
                tex = std::move(loaded_tex.value());
            }
        }

        // find replacement texture
        if (auto [texture, _0, _1] = img::load_texture(miptex.name, false, bsp->loadversion->game, options); texture) {
            tex.width = texture->width;
            tex.height = texture->height;
            tex.pixels = std::move(texture->pixels);
        }

        if (!tex.pixels.size() || !tex.width || !tex.meta.width) {
            logging::funcprint("WARNING: invalid size data for {}\n", miptex.name);
            continue;
        }

        if (tex.meta.color_override) {
            tex.averageColor = *tex.meta.color_override;
        } else {
            tex.averageColor = img::calculate_average(tex.pixels);

            if (options.tex_saturation_boost.value() > 0.0f) {
                tex.averageColor =
                    mix(tex.averageColor, increase_saturation(tex.averageColor), options.tex_saturation_boost.value());
            }
        }

        if (tex.meta.width && tex.meta.height) {
            tex.width_scale = (float)tex.width / (float)tex.meta.width;
            tex.height_scale = (float)tex.height / (float)tex.meta.height;
        }
    }
}

void load_textures(const mbsp_t *bsp, const settings::common_settings &options)
{
    logging::funcheader();

    if (bsp->loadversion->game->id == GAME_QUAKE_II) {
        LoadTextures(bsp, options);
    } else if (bsp->dtex.textures.size() > 0) {
        ConvertTextures(bsp, options);
    } else {
        logging::print("WARNING: failed to load or convert textures.\n");
    }
}
} // namespace img
