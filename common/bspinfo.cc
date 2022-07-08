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

#include <common/bspinfo.hh>
#include <common/cmdlib.hh>
#include <common/bspfile.hh>

#include <sstream>
#include <fstream>
#include <iomanip>
#include <fmt/ostream.h>
#include <common/json.hh>
#include "common/fs.hh"
#include "common/imglib.hh"

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "../3rdparty/stb_image_write.h"

static std::string hex_string(const uint8_t *bytes, const size_t count)
{
    std::stringstream str;

    for (size_t i = 0; i < count; ++i) {
        fmt::print(str, "{:x}", bytes[i]);
    }

    return str.str();
}

/**
 * returns a JSON array of models
 */
static json serialize_bspxbrushlist(const std::vector<uint8_t> &lump)
{
    json j = json::array();

    imemstream p(lump.data(), lump.size(), std::ios_base::in | std::ios_base::binary);

    p >> endianness<std::endian::little>;

    while (true) {
        bspxbrushes_permodel src_model;
        p >= src_model;

        if (!p) {
            break;
        }

        json &model = j.insert(j.end(), json::object()).value();
        model["ver"] = src_model.ver;
        model["modelnum"] = src_model.modelnum;
        model["numbrushes"] = src_model.numbrushes;
        model["numfaces"] = src_model.numfaces;
        json &brushes = (model.emplace("brushes", json::array())).first.value();

        for (int32_t i = 0; i < src_model.numbrushes; ++i) {
            bspxbrushes_perbrush src_brush;
            p >= src_brush;

            json &brush = brushes.insert(brushes.end(), json::object()).value();
            brush.push_back({"mins", src_brush.bounds.mins()});
            brush.push_back({"maxs", src_brush.bounds.maxs()});
            brush.push_back({"contents", src_brush.contents});
            json &faces = (brush.emplace("faces", json::array())).first.value();

            for (int32_t j = 0; j < src_brush.numfaces; ++j) {
                bspxbrushes_perface src_face;
                p >= std::tie(src_face.normal, src_face.dist);

                json &face = faces.insert(faces.end(), json::object()).value();
                face.push_back({"normal", src_face.normal});
                face.push_back({"dist", src_face.dist});
            }
        }
    }

    return j;
}

/**
 * The MIT License (MIT)
 * Copyright (c) 2016 tomykaira
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
template<typename T>
static void Base64EncodeTo(const uint8_t *data, size_t in_len, T p)
{
    static constexpr char sEncodingTable[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',
        'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k',
        'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '0', '1', '2', '3', '4', '5', '6',
        '7', '8', '9', '+', '/'};

    if (in_len == 0)
        return;

    size_t i;

    if (in_len == 1) {
        *p++ = sEncodingTable[(data[0] >> 2) & 0x3F];
        *p++ = sEncodingTable[((data[0] & 0x3) << 4)];
        *p++ = '=';
        *p++ = '=';
        return;
    }

    if (in_len == 2) {
        *p++ = sEncodingTable[(data[0] >> 2) & 0x3F];
        *p++ = sEncodingTable[((data[0] & 0x3) << 4) | ((int)(data[1] & 0xF0) >> 4)];
        *p++ = sEncodingTable[((data[1] & 0xF) << 2)];
        *p++ = '=';
        return;
    }

    for (i = 0; i < in_len - 2; i += 3) {
        *p++ = sEncodingTable[(data[i] >> 2) & 0x3F];
        *p++ = sEncodingTable[((data[i] & 0x3) << 4) | ((int)(data[i + 1] & 0xF0) >> 4)];
        *p++ = sEncodingTable[((data[i + 1] & 0xF) << 2) | ((int)(data[i + 2] & 0xC0) >> 6)];
        *p++ = sEncodingTable[data[i + 2] & 0x3F];
    }
    if (i < in_len) {
        *p++ = sEncodingTable[(data[i] >> 2) & 0x3F];
        if (i == (in_len - 1)) {
            *p++ = sEncodingTable[((data[i] & 0x3) << 4)];
            *p++ = '=';
        } else {
            *p++ = sEncodingTable[((data[i] & 0x3) << 4) | ((int)(data[i + 1] & 0xF0) >> 4)];
            *p++ = sEncodingTable[((data[i + 1] & 0xF) << 2)];
        }
        *p++ = '=';
    }
}

static std::string serialize_image(const std::optional<img::texture> &texture_opt)
{
    if (!texture_opt) {
        FError("can't serialize image in BSP?");
    }

    auto &texture = texture_opt.value();
    std::vector<uint8_t> buf;
    stbi_write_png_to_func([](void *context, void *data, int size) { std::copy(reinterpret_cast<uint8_t *>(data), reinterpret_cast<uint8_t *>(data) + size, std::back_inserter(*reinterpret_cast<decltype(buf) *>(context))); },
        &buf, texture.meta.width, texture.meta.height, 4, texture.pixels.data(), texture.width * 4);

    std::string str{"data:image/png;base64,"};

    Base64EncodeTo(buf.data(), buf.size(), std::back_inserter(str));

    return str;
}

#include "common/bsputils.hh"

static faceextents_t get_face_extents(const mbsp_t &bsp, const bspxentries_t &bspx, const mface_t &face, bool use_bspx)
{
    if (!use_bspx) {
        return { face, bsp, 16.0 };
    }

    return { face, bsp, (float) nth_bit(reinterpret_cast<const char *>(bspx.at("LMSHIFT").data())[&face - bsp.dfaces.data()]) };
}

static std::optional<img::texture> get_lightmap_face(const mbsp_t &bsp, const bspxentries_t &bspx, const mface_t &face, bool use_bspx)
{
    if (face.lightofs == -1) {
        return std::nullopt;
    }

    img::texture texture;
    faceextents_t extents = get_face_extents(bsp, bspx, face, use_bspx);
    texture.meta.width = texture.width = extents.width();
    texture.meta.height = texture.height = extents.height();
    texture.pixels.resize(texture.meta.width * texture.meta.height);

    auto pixels = bsp.dlightdata.begin();
    
    if (!use_bspx) {
        pixels += face.lightofs;
    } else {
        auto &lmoffset = bspx.at("LMOFFSET");
        imemstream stream(lmoffset.data(), lmoffset.size());
        stream >> endianness<std::endian::little>;
        stream.seekg((&face - bsp.dfaces.data()) * sizeof(int32_t));
        int32_t ofs;
        stream >= ofs;
        pixels += ofs;
    }

    auto out_pixels = texture.pixels.begin();

    const bool is_rgb = bsp.loadversion->game->has_rgb_lightmap;

    for (size_t i = 0; i < texture.pixels.size(); i++) {
        if (is_rgb) {
            uint8_t r = *pixels++;
            uint8_t g = *pixels++;
            uint8_t b = *pixels++;
            *out_pixels++ = { r, g, b, 255 };
        } else {
            uint8_t l = *pixels++;
            *out_pixels++ = { l, l, l, 255 };
        }
    }

    return texture;
}

static std::optional<std::string> generate_lightmap_atlas(const mbsp_t &bsp, const bspxentries_t &bspx, int32_t style, bool use_bspx)
{
    struct face_rect
    {
        const mface_t *face;
        std::optional<img::texture> texture;
        size_t atlas = 0;
        size_t x = 0, y = 0;
    };

    constexpr size_t atlas_size = 512;

    struct atlas
    {
        size_t current_x = 0, current_y = 0;
        size_t tallest = 0;
    };

    std::vector<atlas> atlasses;
    std::vector<face_rect> rectangles;
    size_t current_atlas = 0;
    rectangles.reserve(bsp.dfaces.size());

    for (auto &face : bsp.dfaces) {
        face_rect rect { &face, get_lightmap_face(bsp, bspx, face, use_bspx) };

        if (!rect.texture) {
            continue;
        }

        rectangles.emplace_back(std::move(rect));
    }
    
    if (!rectangles.size()) {
        return std::nullopt;
    }

    std::sort(rectangles.begin(), rectangles.end(), [](const face_rect &a, const face_rect &b) -> bool {
        int32_t a_height = (a.texture ? a.texture->height : 0);
        int32_t b_height = (b.texture ? b.texture->height : 0);

        if (a_height == b_height) {
            return b.face > a.face;
        }

        return a_height > b_height;
    });

    for (auto &rect : rectangles) {
        if (!rect.texture) {
            continue;
        }

        while (true) {
            if (current_atlas == atlasses.size()) {
                atlasses.emplace_back();
            }

            atlas &atl = atlasses[current_atlas];

            if (atl.current_x + rect.texture->width >= atlas_size) {
                atl.current_x = 0;
                atl.current_y += atl.tallest;
                atl.tallest = 0;
            }

            if (atl.current_y + rect.texture->height >= atlas_size) {
                current_atlas++;
                continue;
            }

            atl.tallest = max(atl.tallest, (size_t) rect.texture->height);
            rect.x = atl.current_x;
            rect.y = atl.current_y;
            rect.atlas = current_atlas;

            atl.current_x += rect.texture->width;
            break;
        }
    }

    img::texture full_atlas;
    size_t sqrt_count = ceil(sqrt(atlasses.size()));
    full_atlas.width = full_atlas.meta.width = sqrt_count * atlas_size;
    full_atlas.height = full_atlas.meta.height = (floor((atlasses.size() * atlas_size) / full_atlas.width) + 1) * atlas_size;
    full_atlas.pixels.resize(full_atlas.width * full_atlas.height);

    size_t trimmed_width = 0, trimmed_height = 0;

    for (size_t i = 0; i < atlasses.size(); i++) {
        size_t atlas_x = (i % sqrt_count) * atlas_size;
        size_t atlas_y = (i / sqrt_count) * atlas_size;

        for (auto &rect : rectangles) {
            if (!rect.texture || rect.atlas != i) {
                continue;
            }
            
            trimmed_width = max(trimmed_width, atlas_x + rect.x + rect.texture->width);
            trimmed_height = max(trimmed_height, atlas_y + rect.y + rect.texture->height);

            for (size_t x = 0; x < rect.texture->width; x++) {
                for (size_t y = 0; y < rect.texture->height; y++) {
                    auto &src_pixel = rect.texture->pixels[(y * rect.texture->width) + x];
                    auto &dst_pixel = full_atlas.pixels[((atlas_y + y + rect.y) * full_atlas.width) + (atlas_x + x + rect.x)];
                    dst_pixel = src_pixel;
                }
            }
        }
    }
    
    // trim the edges off
    full_atlas.meta.width = trimmed_width;
    full_atlas.height = full_atlas.meta.height = trimmed_height;
    full_atlas.pixels.resize(full_atlas.width * full_atlas.height);

    return serialize_image(full_atlas);
}

void serialize_bsp(const bspdata_t &bspdata, const mbsp_t &bsp, const fs::path &name)
{
    json j = json::object();

    if (!bsp.dmodels.empty()) {
        json &models = (j.emplace("models", json::array())).first.value();

        for (auto &src_model : bsp.dmodels) {
            json &model = models.insert(models.end(), json::object()).value();

            model.push_back({"mins", src_model.mins});
            model.push_back({"maxs", src_model.maxs});
            model.push_back({"origin", src_model.origin});
            model.push_back({"headnode", src_model.headnode});
            model.push_back({"visleafs", src_model.visleafs});
            model.push_back({"firstface", src_model.firstface});
            model.push_back({"numfaces", src_model.numfaces});
        }
    }

    if (bsp.dvis.bits.size()) {

        if (bsp.dvis.bit_offsets.size()) {
            json &visdata = (j.emplace("visdata", json::object())).first.value();

            json &pvs = (visdata.emplace("pvs", json::array())).first.value();
            json &phs = (visdata.emplace("pvs", json::array())).first.value();

            for (auto &offset : bsp.dvis.bit_offsets) {
                pvs.push_back(offset[VIS_PVS]);
                phs.push_back(offset[VIS_PHS]);
            }

            visdata["bits"] = hex_string(bsp.dvis.bits.data(), bsp.dvis.bits.size());
        } else {
            j["visdata"] = hex_string(bsp.dvis.bits.data(), bsp.dvis.bits.size());
        }
    }

    if (bsp.dlightdata.size()) {
        j["lightdata"] = hex_string(bsp.dlightdata.data(), bsp.dlightdata.size());
    }

    if (!bsp.dentdata.empty()) {
        j["entdata"] = bsp.dentdata + '\0';
    }

    if (!bsp.dleafs.empty()) {
        json &leafs = (j.emplace("leafs", json::array())).first.value();

        for (auto &src_leaf : bsp.dleafs) {
            json &leaf = leafs.insert(leafs.end(), json::object()).value();

            leaf.push_back({"contents", src_leaf.contents});
            leaf.push_back({"visofs", src_leaf.visofs});
            leaf.push_back({"mins", src_leaf.mins});
            leaf.push_back({"maxs", src_leaf.maxs});
            leaf.push_back({"firstmarksurface", src_leaf.firstmarksurface});
            leaf.push_back({"nummarksurfaces", src_leaf.nummarksurfaces});
            leaf.push_back({"ambient_level", src_leaf.ambient_level});
            leaf.push_back({"cluster", src_leaf.cluster});
            leaf.push_back({"area", src_leaf.area});
            leaf.push_back({"firstleafbrush", src_leaf.firstleafbrush});
            leaf.push_back({"numleafbrushes", src_leaf.numleafbrushes});
        }
    }

    if (!bsp.dplanes.empty()) {
        json &planes = (j.emplace("planes", json::array())).first.value();

        for (auto &src_plane : bsp.dplanes) {
            json &plane = planes.insert(planes.end(), json::object()).value();

            plane.push_back({"normal", src_plane.normal});
            plane.push_back({"dist", src_plane.dist});
            plane.push_back({"type", src_plane.type});
        }
    }

    if (!bsp.dvertexes.empty()) {
        json &vertexes = (j.emplace("vertexes", json::array())).first.value();

        for (auto &src_vertex : bsp.dvertexes) {
            vertexes.insert(vertexes.end(), src_vertex);
        }
    }

    if (!bsp.dnodes.empty()) {
        json &nodes = (j.emplace("nodes", json::array())).first.value();

        for (auto &src_node : bsp.dnodes) {
            json &node = nodes.insert(nodes.end(), json::object()).value();

            node.push_back({"planenum", src_node.planenum});
            node.push_back({"children", src_node.children});
            node.push_back({"mins", src_node.mins});
            node.push_back({"maxs", src_node.maxs});
            node.push_back({"firstface", src_node.firstface});
            node.push_back({"numfaces", src_node.numfaces});

            // human-readable plane
            auto& plane = bsp.dplanes.at(src_node.planenum);
            node.push_back({"plane", json::array({plane.normal[0], plane.normal[1], plane.normal[2], plane.dist})});
        }
    }

    if (!bsp.texinfo.empty()) {
        json &texinfos = (j.emplace("texinfo", json::array())).first.value();

        for (auto &src_texinfo : bsp.texinfo) {
            json &texinfo = texinfos.insert(texinfos.end(), json::object()).value();

            texinfo.push_back({"vecs", json::array({json::array({src_texinfo.vecs.at(0, 0), src_texinfo.vecs.at(0, 1),
                                                        src_texinfo.vecs.at(0, 2), src_texinfo.vecs.at(0, 3)}),
                                           json::array({src_texinfo.vecs.at(1, 0), src_texinfo.vecs.at(1, 1),
                                               src_texinfo.vecs.at(1, 2), src_texinfo.vecs.at(1, 3)})})});
            texinfo.push_back({"flags", src_texinfo.flags.native});
            texinfo.push_back({"miptex", src_texinfo.miptex});
            texinfo.push_back({"value", src_texinfo.value});
            texinfo.push_back({"texture", std::string(src_texinfo.texture.data())});
            texinfo.push_back({"nexttexinfo", src_texinfo.nexttexinfo});
        }
    }

    if (!bsp.dfaces.empty()) {
        json &faces = (j.emplace("faces", json::array())).first.value();

        for (auto &src_face : bsp.dfaces) {
            json &face = faces.insert(faces.end(), json::object()).value();

            face.push_back({"planenum", src_face.planenum});
            face.push_back({"side", src_face.side});
            face.push_back({"firstedge", src_face.firstedge});
            face.push_back({"numedges", src_face.numedges});
            face.push_back({"texinfo", src_face.texinfo});
            face.push_back({"styles", src_face.styles});
            face.push_back({"lightofs", src_face.lightofs});

            // for readibility, also output the actual vertices
            auto verts = json::array();
            for (int32_t k = 0; k < src_face.numedges; ++k) {
                auto se = bsp.dsurfedges[src_face.firstedge + k];
                uint32_t v = (se < 0) ? bsp.dedges[-se][1] : bsp.dedges[se][0];
                verts.push_back(bsp.dvertexes[v]);
            }
            face.push_back({"vertices", verts});

#if 0
            if (auto lm = get_lightmap_face(bsp, src_face, false)) {
                face.push_back({"lightmap", serialize_image(lm)});
            }
#endif
        }
    }

    if (!bsp.dclipnodes.empty()) {
        json &clipnodes = (j.emplace("clipnodes", json::array())).first.value();

        for (auto &src_clipnodes : bsp.dclipnodes) {
            json &clipnode = clipnodes.insert(clipnodes.end(), json::object()).value();

            clipnode.push_back({"planenum", src_clipnodes.planenum});
            clipnode.push_back({"children", src_clipnodes.children});
        }
    }

    if (!bsp.dedges.empty()) {
        json &edges = (j.emplace("edges", json::array())).first.value();

        for (auto &src_edge : bsp.dedges) {
            edges.insert(edges.end(), src_edge);
        }
    }

    if (!bsp.dleaffaces.empty()) {
        json &leaffaces = (j.emplace("leaffaces", json::array())).first.value();

        for (auto &src_leafface : bsp.dleaffaces) {
            leaffaces.insert(leaffaces.end(), src_leafface);
        }
    }

    if (!bsp.dsurfedges.empty()) {
        json &surfedges = (j.emplace("surfedges", json::array())).first.value();

        for (auto &src_surfedges : bsp.dsurfedges) {
            surfedges.insert(surfedges.end(), src_surfedges);
        }
    }

    if (!bsp.dbrushsides.empty()) {
        json &brushsides = (j.emplace("brushsides", json::array())).first.value();

        for (auto &src_brushside : bsp.dbrushsides) {
            json &brushside = brushsides.insert(brushsides.end(), json::object()).value();

            brushside.push_back({"planenum", src_brushside.planenum});
            brushside.push_back({"texinfo", src_brushside.texinfo});
        }
    }

    if (!bsp.dbrushes.empty()) {
        json &brushes = (j.emplace("brushes", json::array())).first.value();

        for (auto &src_brush : bsp.dbrushes) {
            json &brush = brushes.insert(brushes.end(), json::object()).value();

            brush.push_back({"firstside", src_brush.firstside});
            brush.push_back({"numsides", src_brush.numsides});
            brush.push_back({"contents", src_brush.contents});
        }
    }

    if (!bsp.dleafbrushes.empty()) {
        json &leafbrushes = (j.emplace("leafbrushes", json::array())).first.value();

        for (auto &src_leafbrush : bsp.dleafbrushes) {
            leafbrushes.push_back(src_leafbrush);
        }
    }

    if (bsp.dtex.textures.size()) {
        json &textures = (j.emplace("textures", json::array())).first.value();

        for (auto &src_tex : bsp.dtex.textures) {
            json &tex = textures.insert(textures.end(), json::object()).value();

            tex.push_back({"name", src_tex.name});
            tex.push_back({"width", src_tex.width});
            tex.push_back({"height", src_tex.height});

            if (src_tex.data.size() > sizeof(dmiptex_t)) {
            	json &mips = tex["mips"] = json::array();
            	mips.emplace_back(serialize_image(img::load_mip(src_tex.name, src_tex.data, false, bspdata.loadversion->game)));
            }
        }
    }

    if (!bspdata.bspx.entries.empty()) {
        json &bspxentries = (j.emplace("bspxentries", json::array())).first.value();

        for (auto &lump : bspdata.bspx.entries) {
            json &entry = bspxentries.insert(bspxentries.end(), json::object()).value();
            entry["lumpname"] = lump.first;

            if (lump.first == "BRUSHLIST") {
                entry["models"] = serialize_bspxbrushlist(lump.second);
            } else {
                // unhandled BSPX lump, just write the raw data
                entry["lumpdata"] =
                    hex_string(lump.second.data(), lump.second.size());
            }
        }
    }

    // lightmap atlas
    if (auto lm = generate_lightmap_atlas(bsp, bspdata.bspx.entries, 0, false)) {
        j.emplace("lightmap", std::move(*lm));
    }

    if (bspdata.bspx.entries.find("LMOFFSET") != bspdata.bspx.entries.end()) {
        if (auto lm = generate_lightmap_atlas(bsp, bspdata.bspx.entries, 0, true)) {
            j.emplace("bspx_lightmap", std::move(*lm));
        }
    }

    std::ofstream(name, std::fstream::out | std::fstream::trunc) << std::setw(4) << j;
}
