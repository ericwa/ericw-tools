#include <map>
#include <vector>
#include <common/fs.hh>
#include <common/imglib.hh>
#include <common/entdata.h>
#include <common/json.hh>

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

    memstream stream(file->data(), file->size(), std::ios_base::in | std::ios_base::binary);
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

static void convert_paletted_to_32_bit(const std::vector<uint8_t> &pixels, std::vector<qvec4b> &output, const std::vector<qvec3b> &pal)
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

std::optional<texture> load_wal(const std::string_view &name, const fs::data &file, bool meta_only, const gamedef_t *game)
{
    memstream stream(file->data(), file->size(), std::ios_base::in | std::ios_base::binary);
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

std::optional<texture> load_mip(const std::string_view &name, const fs::data &file, bool meta_only, const gamedef_t *game)
{
    memstream stream(file->data(), file->size(), std::ios_base::in | std::ios_base::binary);
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

            int32_t mip3_size = (header.width >> 3) + (header.height >> 3);
            size_t palette_size = sizeof(uint16_t) + (sizeof(qvec3b) * 256);

            if (header.offsets[3] <= 0) {
                logging::funcprint("mip palette needs offset3 to work, for {}\n", name);
                valid_mip_palette = false;
            } else if (header.offsets[3] + mip3_size + palette_size > file->size()) {
                logging::funcprint("mip palette overrun for {}\n", name);
                valid_mip_palette = false;
            }

            if (valid_mip_palette) {
                stream.seekg(header.offsets[3] + mip3_size + palette_size);

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

/*
============================================================================
TARGA IMAGE
============================================================================
*/
struct targa_t
{
    uint8_t id_length, colormap_type, image_type;
    uint16_t colormap_index, colormap_length;
    uint8_t colormap_size;
    uint16_t x_origin, y_origin, width, height;
    uint8_t pixel_size, attributes;

    auto stream_data()
    {
        return std::tie(id_length, colormap_type, image_type, colormap_index, colormap_length, colormap_size, x_origin,
            y_origin, width, height, pixel_size, attributes);
    }
};

/*
=============
LoadTGA
=============
*/
std::optional<texture> load_tga(const std::string_view &name, const fs::data &file, bool meta_only, const gamedef_t *game)
{
    memstream stream(file->data(), file->size(), std::ios_base::in | std::ios_base::binary);
    stream >> endianness<std::endian::little>;

    // Parse TGA
    targa_t targa_header;
    stream >= targa_header;

    if (targa_header.image_type != 2 && targa_header.image_type != 10) {
        logging::funcprint("Failed to load {}. Only type 2 and 10 targa RGB images supported.\n", name);
        return std::nullopt;
    }

    if (targa_header.colormap_type != 0 || (targa_header.pixel_size != 32 && targa_header.pixel_size != 24)) {
        logging::funcprint("Failed to load {}. Only 32 or 24 bit images supported (no colormaps).\n", name);
        return std::nullopt;
    }

    int32_t columns = targa_header.width;
    int32_t rows = targa_header.height;
    uint32_t numPixels = columns * rows;

    texture tex;

    tex.meta.extension = ext::TGA;

    tex.meta.name = name;
    tex.meta.width = tex.width = columns;
    tex.meta.height = tex.height = rows;

    if (!meta_only) {
        tex.pixels.resize(numPixels);

        if (targa_header.id_length != 0)
            stream.seekg(targa_header.id_length, std::ios_base::cur); // skip TARGA image comment

        if (targa_header.image_type == 2) { // Uncompressed, RGB images
            for (int32_t row = rows - 1; row >= 0; row--) {
                qvec4b *pixbuf = tex.pixels.data() + row * columns;
                for (int32_t column = 0; column < columns; column++) {
                    uint8_t red, green, blue, alphabyte;
                    switch (targa_header.pixel_size) {
                        case 24:
                            stream >= blue >= green >= red;
                            *pixbuf++ = {red, green, blue, 255};
                            break;
                        case 32:
                            stream >= blue >= green >= red >= alphabyte;
                            *pixbuf++ = {red, green, blue, alphabyte};
                            break;
                        default:
                            logging::funcprint("TGA {}, unsupported pixel size: {}\n", name, targa_header.pixel_size); // mxd
                            return std::nullopt;
                    }
                }
            }
        } else if (targa_header.image_type == 10) { // Runlength encoded RGB images
            unsigned char red, green, blue, alphabyte, j;
            for (int32_t row = rows - 1; row >= 0; row--) {
                qvec4b *pixbuf = tex.pixels.data() + row * columns;
                for (int32_t column = 0; column < columns;) {
                    uint8_t packetHeader;
                    stream >= packetHeader;
                    uint8_t packetSize = 1 + (packetHeader & 0x7f);

                    if (packetHeader & 0x80) { // run-length packet
                        switch (targa_header.pixel_size) {
                            case 24:
                                stream >= blue >= green >= red;
                                alphabyte = 255;
                                break;
                            case 32: stream >= blue >= green >= red >= alphabyte; break;
                            default:
                                logging::funcprint("TGA {}, unsupported pixel size: {}\n", name, targa_header.pixel_size); // mxd
                                return std::nullopt;
                        }

                        for (j = 0; j < packetSize; j++) {
                            *pixbuf++ = {red, green, blue, alphabyte};
                            column++;
                            if (column == columns) { // run spans across rows
                                column = 0;
                                if (row > 0)
                                    row--;
                                else
                                    goto breakOut;
                                pixbuf = tex.pixels.data() + row * columns;
                            }
                        }
                    } else { // non run-length packet
                        for (j = 0; j < packetSize; j++) {
                            switch (targa_header.pixel_size) {
                                case 24:
                                    stream >= blue >= green >= red;
                                    *pixbuf++ = {red, green, blue, 255};
                                    break;
                                case 32:
                                    stream >= blue >= green >= red >= alphabyte;
                                    *pixbuf++ = {red, green, blue, alphabyte};
                                    break;
                                default:
                                    logging::funcprint("TGA {}, unsupported pixel size: {}\n", name, targa_header.pixel_size); // mxd
                                    return std::nullopt;
                            }
                            column++;
                            if (column == columns) { // pixel packet run spans across rows
                                column = 0;
                                if (row > 0)
                                    row--;
                                else
                                    goto breakOut;
                                pixbuf = tex.pixels.data() + row * columns;
                            }
                        }
                    }
                }
breakOut:;
            }
        }
    }

    return tex; // mxd
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

std::tuple<std::optional<img::texture>, fs::resolve_result, fs::data> load_texture(const std::string_view &name, bool meta_only, const gamedef_t *game, const settings::common_settings &options)
{
    fs::path prefix;

    if (game->id == GAME_QUAKE_II) {
        prefix = "textures";
    }

    for (auto &ext : img::extension_list) {
        fs::path p = (prefix / name) += ext.suffix;

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
        "height": 64
    }
*/
std::optional<texture_meta> load_wal_json_meta(const std::string_view &name, const fs::data &file, const gamedef_t *game)
{
    try
    {
        auto json = json::parse(file->begin(), file->end());

        texture_meta meta;

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

        return meta;
    }
    catch (json::exception e)
    {
        logging::funcprint("{}, invalid JSON: {}\n", name, e.what());
        return std::nullopt;
    }
}

std::tuple<std::optional<img::texture_meta>, fs::resolve_result, fs::data> load_texture_meta(const std::string_view &name, const gamedef_t *game, const settings::common_settings &options)
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
} // namespace img
