#include <map>
#include <vector>
#include <common/fs.hh>
#include <common/imglib.hh>
#include <common/entdata.h>

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

static bool LoadPCXPalette(const std::filesystem::path &filename, std::vector<qvec3b> &palette)
{
    auto file = fs::load(filename);

    if (!file || !file->size()) {
        FLogPrint("Failed to load '{}'.\n", filename);
        return false;
    }

    memstream stream(file->data(), file->size(), std::ios_base::in | std::ios_base::binary);
    stream >> endianness<std::endian::little>;

    // Parse the PCX file
    pcx_t pcx;
    stream >= pcx;

    if (pcx.manufacturer != 0x0a || pcx.version != 5 || pcx.encoding != 1 || pcx.bits_per_pixel != 8) {
        FLogPrint("Failed to load '{}'. Unsupported PCX file.\n", filename);
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

        LogPrint("INFO: Falling back to built-in palette.\n");
    }

    auto &pal = game->get_default_palette();

    std::copy(pal.begin(), pal.end(), std::back_inserter(palette));
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

std::optional<texture> load_wal(const std::string &name, const fs::data &file, bool metaOnly)
{
    memstream stream(file->data(), file->size(), std::ios_base::in | std::ios_base::binary);
    stream >> endianness<std::endian::little>;

    // Parse WAL
    q2_miptex_t mt;
    stream >= mt;

    size_t numPixels = mt.width * mt.height;

    texture tex;

    tex.meta.name = name;
    tex.meta.width = mt.width;
    tex.meta.height = mt.height;
    tex.meta.contents = {mt.contents};
    tex.meta.flags = {mt.flags};
    tex.meta.value = mt.value;
    tex.meta.animation = mt.animname.data();

    if (!metaOnly) {
        tex.pixels.resize(numPixels);

        stream.seekg(mt.offsets[0]);

        for (size_t i = 0; i < numPixels; i++) {
            uint8_t pixel;
            stream >= pixel;

            // Last palette index is transparent color
            tex.pixels[i] = qvec4b(palette[pixel], pixel == 255 ? 0 : 255);
        }
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
std::optional<texture> load_tga(const std::string &name, const fs::data &file, bool metaOnly)
{
    memstream stream(file->data(), file->size(), std::ios_base::in | std::ios_base::binary);
    stream >> endianness<std::endian::little>;

    // Parse TGA
    targa_t targa_header;
    stream >= targa_header;

    if (targa_header.image_type != 2 && targa_header.image_type != 10) {
        FLogPrint("Failed to load TGA. Only type 2 and 10 targa RGB images supported.\n");
        return std::nullopt;
    }

    if (targa_header.colormap_type != 0 || (targa_header.pixel_size != 32 && targa_header.pixel_size != 24)) {
        FLogPrint("Failed to load TGA. Only 32 or 24 bit images supported (no colormaps).\n");
        return std::nullopt;
    }

    int32_t columns = targa_header.width;
    int32_t rows = targa_header.height;
    uint32_t numPixels = columns * rows;

    texture tex;

    tex.meta.name = name;
    tex.meta.width = columns;
    tex.meta.height = rows;

    if (!metaOnly) {
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
                            FLogPrint("unsupported pixel size: {}\n", targa_header.pixel_size); // mxd
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
                                FLogPrint("unsupported pixel size: {}\n", targa_header.pixel_size); // mxd
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
                                    FLogPrint("unsupported pixel size: {}\n", targa_header.pixel_size); // mxd
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

const texture *find(const std::string &str)
{
    auto it = textures.find(str);

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

/*
==============================================================================
Load (Quake 2) / Convert (Quake, Hexen 2) textures from paletted to RGBA (mxd)
==============================================================================
*/
static void AddTextureName(const char *textureName)
{
    if (textures.find(textureName) != textures.end()) {
        return;
    }

    auto &tex = textures.emplace(textureName, texture{}).first->second;

    static constexpr struct
    {
        const char *name;
        decltype(load_wal) *loader;
    } supportedExtensions[] = {{"tga", load_tga}};

    // find wal first, since we'll use it for metadata
    auto wal = fs::load("textures" / fs::path(textureName) += ".wal");

    if (!wal) {
        FLogPrint("WARNING: can't find .wal for {}\n", textureName);
    } else {
        auto walTex = load_wal(textureName, wal, false);

        if (walTex) {
            tex = std::move(*walTex);
        }
    }

    // now check for replacements
    for (auto &ext : supportedExtensions) {
        auto replacement = fs::load(("textures" / fs::path(textureName) += ".") += ext.name);

        if (!replacement) {
            continue;
        }

        auto replacementTex = ext.loader(textureName, replacement, false);

        if (replacementTex) {
            tex.meta.width = replacementTex->meta.width;
            tex.meta.height = replacementTex->meta.height;
            tex.pixels = std::move(replacementTex->pixels);
            break;
        }
    }

    tex.meta.averageColor = calculate_average(tex.pixels);
}

// Load all of the referenced textures from the BSP texinfos into
// the texture cache.
static void LoadTextures(const mbsp_t *bsp)
{
    // gather all loadable textures...
    for (auto &texinfo : bsp->texinfo) {
        AddTextureName(texinfo.texture.data());
    }

    // gather textures used by _project_texture.
    // FIXME: I'm sure we can resolve this so we don't parse entdata twice.
    auto entdicts = EntData_Parse(bsp->dentdata);
    for (auto &entdict : entdicts) {
        if (EntDict_StringForKey(entdict, "classname").find("light") == 0) {
            const auto &tex = EntDict_StringForKey(entdict, "_project_texture");
            if (!tex.empty()) {
                AddTextureName(tex.c_str());
            }
        }
    }
}

// Load all of the paletted textures from the BSP into
// the texture cache.
// TODO: doesn't handle external wads...
static void ConvertTextures(const mbsp_t *bsp)
{
    if (!bsp->dtex.textures.size()) {
        return;
    }

    for (auto &miptex : bsp->dtex.textures) {
        if (textures.find(miptex.name) != textures.end()) {
            FLogPrint("WARNING: Texture {} duplicated\n", miptex.name);
            continue;
        }

        // Add empty to keep texture index in case of load problems...
        auto &tex = textures.emplace(miptex.name, texture{}).first->second;

        if (!miptex.data[0]) {
            FLogPrint("WARNING: Texture {} is external\n", miptex.name);
            continue;
        }

        // Create rgba_miptex_t...
        tex.meta.name = miptex.name;
        tex.meta.width = miptex.width;
        tex.meta.height = miptex.height;

        // Convert to RGBA
        size_t numPixels = miptex.width * miptex.height;

        tex.pixels.resize(numPixels);

        const uint8_t *data = miptex.data[0].get();
        auto &pal = miptex.palette.empty() ? palette : miptex.palette;

        for (size_t c = 0; c < numPixels; c++) {
            const uint8_t palindex = data[c];
            tex.pixels[c] = {pal[palindex], static_cast<uint8_t>(palindex == 255 ? 0 : 255)};
        }

        tex.meta.averageColor = calculate_average(tex.pixels);
    }
}

void load_textures(const mbsp_t *bsp)
{
    LogPrint("--- {} ---\n", __func__);

    if (bsp->loadversion->game->id == GAME_QUAKE_II) {
        LoadTextures(bsp);
    } else if (bsp->dtex.textures.size() > 0) {
        ConvertTextures(bsp);
    } else {
        LogPrint("WARNING: failed to load or convert textures.\n");
    }
}
} // namespace img
