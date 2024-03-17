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
#include <common/log.hh>
#include <common/cmdlib.hh>
#include <common/bspfile.hh>
#include <common/ostream.hh>

#include <fstream>
#include <iomanip>
#include <fmt/core.h>
#include <common/json.hh>
#include "common/fs.hh"
#include "common/imglib.hh"

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "../3rdparty/stb_image_write.h"

static std::string hex_string(const uint8_t *bytes, const size_t count)
{
    std::string str;

    for (size_t i = 0; i < count; ++i) {
        fmt::format_to(std::back_inserter(str), "{:x}", bytes[i]);
    }

    return str;
}

/**
 * returns a JSON array of models
 */
static Json::Value serialize_bspxbrushlist(const std::vector<uint8_t> &lump)
{
    Json::Value j = Json::Value(Json::arrayValue);

    imemstream p(lump.data(), lump.size(), std::ios_base::in | std::ios_base::binary);

    p >> endianness<std::endian::little>;
    bspxbrushes structured;
    p >= structured;

    for (const bspxbrushes_permodel &src_model : structured.models) {
        auto &model = j.append(Json::Value(Json::objectValue));
        model["ver"] = src_model.ver;
        model["modelnum"] = src_model.modelnum;
        model["numbrushes"] = static_cast<Json::UInt64>(src_model.brushes.size());
        model["numfaces"] = src_model.numfaces;
        auto &brushes = (model["brushes"] = Json::Value(Json::arrayValue));

        for (const bspxbrushes_perbrush &src_brush : src_model.brushes) {
            auto &brush = brushes.append(Json::Value(Json::objectValue));
            brush["mins"] = to_json(src_brush.bounds.mins());
            brush["maxs"] = to_json(src_brush.bounds.maxs());
            brush["contents"] = src_brush.contents;
            auto &faces = (brush["faces"] = Json::Value(Json::arrayValue));

            for (const bspxbrushes_perface &src_face : src_brush.faces) {
                auto &face = faces.append(Json::Value(Json::objectValue));
                face["normal"] = to_json(src_face.normal);
                face["dist"] = src_face.dist;
            }
        }
    }

    return j;
}

static Json::Value serialize_bspx_decoupled_lm(const std::vector<uint8_t> &lump)
{
    auto j = Json::Value(Json::arrayValue);

    imemstream p(lump.data(), lump.size(), std::ios_base::in | std::ios_base::binary);

    p >> endianness<std::endian::little>;

    while (true) {
        bspx_decoupled_lm_perface src_face;
        p >= src_face;

        if (!p) {
            break;
        }

        auto &model = j.append(Json::objectValue);
        model["lmwidth"] = src_face.lmwidth;
        model["lmheight"] = src_face.lmheight;
        model["offset"] = src_face.offset;
        model["world_to_lm_space"] = json_array({
            to_json(src_face.world_to_lm_space.row(0)),
            to_json(src_face.world_to_lm_space.row(1))
        });
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
    stbi_write_png_to_func(
        [](void *context, void *data, int size) {
            std::copy(reinterpret_cast<uint8_t *>(data), reinterpret_cast<uint8_t *>(data) + size,
                std::back_inserter(*reinterpret_cast<decltype(buf) *>(context)));
        },
        &buf, texture.meta.width, texture.meta.height, 4, texture.pixels.data(), texture.width * 4);

    std::string str{"data:image/png;base64,"};

    Base64EncodeTo(buf.data(), buf.size(), std::back_inserter(str));

    return str;
}

#include "common/bsputils.hh"

static faceextents_t get_face_extents(const mbsp_t &bsp, const bspxentries_t &bspx,
    const std::vector<bspx_decoupled_lm_perface> &bspx_decoupled, const mface_t &face, bool use_bspx,
    bool use_decoupled)
{
    if (use_decoupled) {
        ptrdiff_t face_idx = &face - bsp.dfaces.data();
        auto &bspx = bspx_decoupled[face_idx];
        return {face, bsp, bspx.lmwidth, bspx.lmheight, bspx.world_to_lm_space};
    }
    if (!use_bspx) {
        return {face, bsp, LMSCALE_DEFAULT};
    }

    return {face, bsp,
        (float)nth_bit(reinterpret_cast<const char *>(bspx.at("LMSHIFT").data())[&face - bsp.dfaces.data()])};
}

full_atlas_t build_lightmap_atlas(const mbsp_t &bsp, const bspxentries_t &bspx, const std::vector<uint8_t> &litdata, bool use_bspx, bool use_decoupled)
{
    struct face_rect
    {
        const mface_t *face;
        faceextents_t extents;
        int32_t lightofs;
        std::optional<img::texture> texture = std::nullopt;
        size_t atlas = 0;
        size_t x = 0, y = 0;
    };

    constexpr size_t atlas_size = 512;
    const uint8_t *lightdata_source;
    bool is_rgb;
    bool is_lit;

    if (!litdata.empty()) {
        is_lit = true;
        is_rgb = true;
        lightdata_source = litdata.data();
    } else {
        is_lit = false;
        is_rgb = bsp.loadversion->game->has_rgb_lightmap;
        lightdata_source = bsp.dlightdata.data();
    }

    struct atlas
    {
        size_t current_x = 0, current_y = 0;
        size_t tallest = 0;
    };

    std::vector<atlas> atlasses;
    std::vector<face_rect> rectangles;
    size_t current_atlas = 0;
    rectangles.reserve(bsp.dfaces.size());

    imemstream bspx_lmoffset(nullptr, 0);

    if (use_bspx) {
        auto &lmoffset = bspx.at("LMOFFSET");
        bspx_lmoffset = imemstream(lmoffset.data(), lmoffset.size());
        bspx_lmoffset >> endianness<std::endian::little>;
    }

    std::vector<bspx_decoupled_lm_perface> bspx_decoupled;
    if (use_decoupled && (bspx.find("DECOUPLED_LM") != bspx.end())) {
        bspx_decoupled.resize(bsp.dfaces.size());

        imemstream stream(nullptr, 0);

        auto &decoupled_lm = bspx.at("DECOUPLED_LM");
        stream = imemstream(decoupled_lm.data(), decoupled_lm.size());
        stream >> endianness<std::endian::little>;

        for (size_t i = 0; i < bsp.dfaces.size(); ++i) {
            stream >= bspx_decoupled[i];
        }
    } else {
        use_decoupled = false;
    }

    // make rectangles
    for (auto &face : bsp.dfaces) {
        const ptrdiff_t face_idx = (&face - bsp.dfaces.data());
        int32_t faceofs;

        if (use_decoupled) {
            faceofs = bspx_decoupled[face_idx].offset;
        } else if (!use_bspx) {
            faceofs = face.lightofs;
        } else {
            bspx_lmoffset.seekg(face_idx * sizeof(int32_t));
            bspx_lmoffset >= faceofs;
        }

        rectangles.emplace_back(
            face_rect{&face, get_face_extents(bsp, bspx, bspx_decoupled, face, use_bspx, use_decoupled), faceofs});
    }

    if (!rectangles.size()) {
        return {};
    }

    // sort faces
    std::sort(rectangles.begin(), rectangles.end(), [](const face_rect &a, const face_rect &b) -> bool {
        int32_t a_height = a.extents.height();
        int32_t b_height = b.extents.height();

        if (a_height == b_height) {
            return b.face > a.face;
        }

        return a_height > b_height;
    });

    // pack
    for (auto &rect : rectangles) {
        while (true) {
            if (current_atlas == atlasses.size()) {
                atlasses.emplace_back();
            }

            atlas &atl = atlasses[current_atlas];

            if (atl.current_x + rect.extents.width() >= atlas_size) {
                atl.current_x = 0;
                atl.current_y += atl.tallest;
                atl.tallest = 0;
            }

            if (atl.current_y + rect.extents.height() >= atlas_size) {
                current_atlas++;
                continue;
            }

            atl.tallest = std::max(atl.tallest, (size_t)rect.extents.height());
            rect.x = atl.current_x;
            rect.y = atl.current_y;
            rect.atlas = current_atlas;

            atl.current_x += rect.extents.width();
            break;
        }
    }

    // calculate final atlas texture size
    img::texture full_atlas;
    size_t sqrt_count = ceil(sqrt(atlasses.size()));
    size_t trimmed_width = 0, trimmed_height = 0;

    for (size_t i = 0; i < atlasses.size(); i++) {
        size_t atlas_x = (i % sqrt_count) * atlas_size;
        size_t atlas_y = (i / sqrt_count) * atlas_size;

        for (auto &rect : rectangles) {
            if (rect.atlas == i) {
                rect.x += atlas_x;
                rect.y += atlas_y;
                trimmed_width = std::max(trimmed_width, rect.x + rect.extents.width());
                trimmed_height = std::max(trimmed_height, rect.y + rect.extents.height());
            }
#if 0
            for (size_t x = 0; x < rect.texture->width; x++) {
                for (size_t y = 0; y < rect.texture->height; y++) {
                    auto &src_pixel = rect.texture->pixels[(y * rect.texture->width) + x];
                    auto &dst_pixel = full_atlas.pixels[((atlas_y + y + rect.y) * full_atlas.width) + (atlas_x + x + rect.x)];
                    dst_pixel = src_pixel;
                }
            }
#endif
        }
    }

    full_atlas.width = full_atlas.meta.width = trimmed_width;
    full_atlas.height = full_atlas.meta.height = trimmed_height;
    full_atlas.pixels.resize(full_atlas.width * full_atlas.height);

    full_atlas_t result;

    // compile all of the styles that are available
    // TODO: LMSTYLE16
    for (size_t i = 0; i < INVALID_LIGHTSTYLE_OLD - 1; i++) {
        bool any_written = false;

        for (auto &rect : rectangles) {
            int32_t style_index = -1;

            for (size_t s = 0; s < MAXLIGHTMAPS; s++) {
                if (rect.face->styles[s] == i) {
                    style_index = s;
                    break;
                }
            }

            if (style_index == -1) {
                continue;
            }

            if (bsp.dlightdata.empty()) {
                continue;
            }

            auto in_pixel =
                lightdata_source + ((is_lit ? 3 : 1) * rect.lightofs) + (rect.extents.numsamples() * (is_rgb ? 3 : 1) * style_index);

            for (size_t y = 0; y < rect.extents.height(); y++) {
                for (size_t x = 0; x < rect.extents.width(); x++) {
                    size_t ox = rect.x + x;
                    size_t oy = rect.y + y;

                    auto &out_pixel = full_atlas.pixels[(oy * full_atlas.width) + ox];
                    out_pixel[3] = 255;

                    if (is_rgb) {
                        out_pixel[0] = *in_pixel++;
                        out_pixel[1] = *in_pixel++;
                        out_pixel[2] = *in_pixel++;
                    } else {
                        out_pixel[0] = out_pixel[1] = out_pixel[2] = *in_pixel++;
                    }
                }
            }

            any_written = true;
        }

        if (!any_written) {
            continue;
        }

        // copy out the atlas texture
        result.style_to_lightmap_atlas[i] = full_atlas;

        memset(full_atlas.pixels.data(), 0, sizeof(*full_atlas.pixels.data()) * full_atlas.pixels.size());
    }

    auto ExportLightmapUVs = [&full_atlas, &result](const mbsp_t *bsp, const face_rect &face) {
        std::vector<qvec2f> face_lightmap_uvs;

        for (int i = 0; i < face.face->numedges; i++) {
            const int vertnum = Face_VertexAtIndex(bsp, face.face, i);
            const qvec3f &pos = bsp->dvertexes[vertnum];

            auto tc = face.extents.worldToLMCoord(pos);
            tc[0] += face.x;
            tc[1] += face.y;

            // add a half-texel offset (see BuildSurfaceDisplayList() in Quakespasm)
            tc[0] += 0.5;
            tc[1] += 0.5;

            tc[0] /= full_atlas.width;
            tc[1] /= full_atlas.height;

            face_lightmap_uvs.push_back(tc);
        }

        result.facenum_to_lightmap_uvs[Face_GetNum(bsp, face.face)] = std::move(face_lightmap_uvs);
    };

    for (auto &rect : rectangles) {
        ExportLightmapUVs(&bsp, rect);
    }

    return result;
}

static void export_obj_and_lightmaps(const mbsp_t &bsp, const bspxentries_t &bspx, bool use_bspx, bool use_decoupled,
    fs::path obj_path, const fs::path &lightmaps_path_base)
{
    // FIXME: pass in .lit
    const auto atlas = build_lightmap_atlas(bsp, bspx, {}, use_bspx, use_decoupled);

    if (atlas.facenum_to_lightmap_uvs.empty()) {
        return;
    }

    // e.g. mapname.bsp.lm
    const std::string stem = lightmaps_path_base.stem().string();

    // write .png's, one per style
    for (const auto &[i, full_atlas] : atlas.style_to_lightmap_atlas) {
        auto lightmaps_path = lightmaps_path_base;
        lightmaps_path.replace_filename(stem + "_" + std::to_string(i) + ".png");
        std::ofstream strm(lightmaps_path, std::ofstream::out | std::ofstream::binary);
        stbi_write_png_to_func(
            [](void *context, void *data, int size) {
                std::ofstream &strm = *((std::ofstream *)context);
                strm.write((const char *)data, size);
            },
            &strm, full_atlas.width, full_atlas.height, 4, full_atlas.pixels.data(), full_atlas.width * 4);
        logging::print("wrote {}\n", lightmaps_path);
    }

    auto ExportObjFace = [&atlas](std::ostream &f, const mbsp_t *bsp, int face_num, int &vertcount) {
        const auto *face = BSP_GetFace(bsp, face_num);

        const auto &tcs = atlas.facenum_to_lightmap_uvs.at(face_num);

        // export the vertices and uvs
        for (int i = 0; i < face->numedges; i++) {
            const int vertnum = Face_VertexAtIndex(bsp, face, i);
            const qvec3f normal = bsp->dplanes[face->planenum].normal;
            const qvec3f &pos = bsp->dvertexes[vertnum];
            ewt::print(f, "v {:.9} {:.9} {:.9}\n", pos[0], pos[1], pos[2]);
            ewt::print(f, "vn {:.9} {:.9} {:.9}\n", normal[0], normal[1], normal[2]);

            qvec2f tc = tcs[i];

            tc[1] = 1.0 - tc[1];

            ewt::print(f, "vt {:.9} {:.9}\n", tc[0], tc[1]);
        }

        f << "f";
        for (int i = 0; i < face->numedges; i++) {
            // .obj vertexes start from 1
            // .obj faces are CCW, quake is CW, so reverse the order
            const int vertindex = vertcount + (face->numedges - 1 - i) + 1;
            ewt::print(f, " {0}/{0}/{0}", vertindex);
        }
        f << '\n';

        vertcount += face->numedges;
    };

    auto ExportObj = [&ExportObjFace, &obj_path](const mbsp_t *bsp) {
        std::ofstream objstream(obj_path, std::ofstream::out);
        int vertcount = 0;

        for (int i = 0; i < bsp->dfaces.size(); ++i) {
            ExportObjFace(objstream, bsp, i, vertcount);
        }
    };

    ExportObj(&bsp);

    logging::print("wrote {}\n", obj_path);
}

void serialize_bsp(const bspdata_t &bspdata, const mbsp_t &bsp, const fs::path &name)
{
    auto j = Json::Value(Json::objectValue);

    if (!bsp.dmodels.empty()) {
        auto &models = (j["models"] = Json::Value(Json::arrayValue));

        for (auto &src_model : bsp.dmodels) {
            auto &model = models.append(Json::Value(Json::objectValue));

            model["mins"] = to_json(src_model.mins);
            model["maxs"] = to_json(src_model.maxs);
            model["origin"] = to_json(src_model.origin);
            model["headnode"] = to_json(src_model.headnode);
            model["visleafs"] = src_model.visleafs;
            model["firstface"] = src_model.firstface;
            model["numfaces"] = src_model.numfaces;
        }
    }

    if (bsp.dvis.bits.size()) {

        if (bsp.dvis.bit_offsets.size()) {
            auto &visdata = j["visdata"];
            visdata = Json::Value(Json::objectValue);

            auto &pvs = (visdata["pvs"] = Json::Value(Json::arrayValue));
            auto &phs = (visdata["pvs"] = Json::Value(Json::arrayValue));

            for (auto &offset : bsp.dvis.bit_offsets) {
                pvs.append(offset[VIS_PVS]);
                phs.append(offset[VIS_PHS]);
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
        auto &leafs = (j["leafs"] = Json::Value(Json::arrayValue));

        for (auto &src_leaf : bsp.dleafs) {
            auto &leaf = leafs.append(Json::Value(Json::objectValue));

            leaf["contents"] = src_leaf.contents;
            leaf["visofs"] = src_leaf.visofs;
            leaf["mins"] = to_json(src_leaf.mins);
            leaf["maxs"] = to_json(src_leaf.maxs);
            leaf["firstmarksurface"] = src_leaf.firstmarksurface;
            leaf["nummarksurfaces"] = src_leaf.nummarksurfaces;
            leaf["ambient_level"] = to_json(src_leaf.ambient_level);
            leaf["cluster"] = src_leaf.cluster;
            leaf["area"] = src_leaf.area;
            leaf["firstleafbrush"] = src_leaf.firstleafbrush;
            leaf["numleafbrushes"] = src_leaf.numleafbrushes;
        }
    }

    if (!bsp.dplanes.empty()) {
        auto &planes = (j["planes"] = Json::Value(Json::arrayValue));

        for (auto &src_plane : bsp.dplanes) {
            auto &plane = planes.append(Json::Value(Json::objectValue));

            plane["normal"] = to_json(src_plane.normal);
            plane["dist"] = src_plane.dist;
            plane["type"] = src_plane.type;
        }
    }

    if (!bsp.dvertexes.empty()) {
        auto &vertexes = (j["vertexes"] = Json::Value(Json::arrayValue));

        for (auto &src_vertex : bsp.dvertexes) {
            vertexes.append(to_json(src_vertex));
        }
    }

    if (!bsp.dnodes.empty()) {
        auto &nodes = (j["nodes"] = Json::Value(Json::arrayValue));

        for (auto &src_node : bsp.dnodes) {
            auto &node = nodes.append(Json::Value(Json::objectValue));

            node["planenum"] = src_node.planenum;
            node["children"] = to_json(src_node.children);
            node["mins"] = to_json(src_node.mins);
            node["maxs"] = to_json(src_node.maxs);
            node["firstface"] = src_node.firstface;
            node["numfaces"] = src_node.numfaces;

            // human-readable plane
            auto &plane = bsp.dplanes.at(src_node.planenum);
            node["plane"] = json_array({plane.normal[0], plane.normal[1], plane.normal[2], plane.dist});
        }
    }

    if (!bsp.texinfo.empty()) {
        auto &texinfos = (j["texinfo"] = Json::Value(Json::arrayValue));

        for (auto &src_texinfo : bsp.texinfo) {
            auto &texinfo = texinfos.append(Json::Value(Json::objectValue));

            texinfo["vecs"] = json_array({json_array({src_texinfo.vecs.at(0, 0), src_texinfo.vecs.at(0, 1),
                                                        src_texinfo.vecs.at(0, 2), src_texinfo.vecs.at(0, 3)}),
                json_array({src_texinfo.vecs.at(1, 0), src_texinfo.vecs.at(1, 1),
                                               src_texinfo.vecs.at(1, 2), src_texinfo.vecs.at(1, 3)})});
            texinfo["flags"] = src_texinfo.flags.native;
            texinfo["miptex"] = src_texinfo.miptex;
            texinfo["value"] = src_texinfo.value;
            texinfo["texture"] = std::string(src_texinfo.texture.data());
            texinfo["nexttexinfo"] = src_texinfo.nexttexinfo;
        }
    }

    if (!bsp.dfaces.empty()) {
        auto &faces = (j["faces"] = Json::Value(Json::arrayValue));

        for (auto &src_face : bsp.dfaces) {
            auto &face = faces.append(Json::Value(Json::objectValue));

            face["planenum"] = src_face.planenum;
            face["side"] = src_face.side;
            face["firstedge"] = src_face.firstedge;
            face["numedges"] = src_face.numedges;
            face["texinfo"] = src_face.texinfo;
            face["styles"] = to_json(src_face.styles);
            face["lightofs"] = src_face.lightofs;

            // for readibility, also output the actual vertices
            auto verts = Json::Value(Json::arrayValue);
            for (int32_t k = 0; k < src_face.numedges; ++k) {
                auto se = bsp.dsurfedges[src_face.firstedge + k];
                uint32_t v = (se < 0) ? bsp.dedges[-se][1] : bsp.dedges[se][0];
                verts.append(to_json(bsp.dvertexes[v]));
            }
            face["vertices"] = verts;

#if 0
            if (auto lm = get_lightmap_face(bsp, src_face, false)) {
                face["lightmap", serialize_image(lm)});
            }
#endif
        }
    }

    if (!bsp.dclipnodes.empty()) {
        auto &clipnodes = (j["clipnodes"] = Json::Value(Json::arrayValue));

        for (auto &src_clipnodes : bsp.dclipnodes) {
            auto &clipnode = clipnodes.append(Json::Value(Json::objectValue));

            clipnode["planenum"] = src_clipnodes.planenum;
            clipnode["children"] = to_json(src_clipnodes.children);
        }
    }

    if (!bsp.dedges.empty()) {
        auto &edges = (j["edges"] = Json::Value(Json::arrayValue));

        for (auto &src_edge : bsp.dedges) {
            edges.append(to_json(src_edge));
        }
    }

    if (!bsp.dleaffaces.empty()) {
        auto &leaffaces = (j["leaffaces"] = Json::Value(Json::arrayValue));

        for (auto &src_leafface : bsp.dleaffaces) {
            leaffaces.append(src_leafface);
        }
    }

    if (!bsp.dsurfedges.empty()) {
        auto &surfedges = (j["surfedges"] = Json::Value(Json::arrayValue));

        for (auto &src_surfedges : bsp.dsurfedges) {
            surfedges.append(src_surfedges);
        }
    }

    if (!bsp.dbrushsides.empty()) {
        auto &brushsides = (j["brushsides"] = Json::Value(Json::arrayValue));

        for (auto &src_brushside : bsp.dbrushsides) {
            auto &brushside = brushsides.append(Json::Value(Json::objectValue));

            brushside["planenum"] = src_brushside.planenum;
            brushside["texinfo"] = src_brushside.texinfo;
        }
    }

    if (!bsp.dbrushes.empty()) {
        auto &brushes = (j["brushes"] = Json::Value(Json::arrayValue));

        for (auto &src_brush : bsp.dbrushes) {
            auto &brush = brushes.append(Json::Value(Json::objectValue));

            brush["firstside"] = src_brush.firstside;
            brush["numsides"] = src_brush.numsides;
            brush["contents"] = src_brush.contents;
        }
    }

    if (!bsp.dleafbrushes.empty()) {
        auto &leafbrushes = (j["leafbrushes"] = Json::Value(Json::arrayValue));

        for (auto &src_leafbrush : bsp.dleafbrushes) {
            leafbrushes.append(src_leafbrush);
        }
    }

    if (bsp.dtex.textures.size()) {
        auto &textures = (j["textures"] = Json::Value(Json::arrayValue));

        for (auto &src_tex : bsp.dtex.textures) {
            if (src_tex.null_texture) {
                // use json null to indicate offset -1
                textures.append(Json::Value(Json::nullValue));
                continue;
            }
            auto &tex = textures.append(Json::Value(Json::objectValue));

            tex["name"] = src_tex.name;
            tex["width"] = src_tex.width;
            tex["height"] = src_tex.height;

            if (src_tex.data.size() > sizeof(dmiptex_t)) {
                auto &mips = tex["mips"] = Json::Value(Json::arrayValue);
                mips.append(
                    serialize_image(img::load_mip(src_tex.name, src_tex.data, false, bspdata.loadversion->game)));
            }
        }
    }

    if (!bspdata.bspx.entries.empty()) {
        auto &bspxentries = (j["bspxentries"] = Json::Value(Json::arrayValue));

        for (auto &lump : bspdata.bspx.entries) {
            auto &entry = bspxentries.append(Json::Value(Json::objectValue));
            entry["lumpname"] = lump.first;

            if (lump.first == "BRUSHLIST") {
                entry["models"] = serialize_bspxbrushlist(lump.second);
            } else if (lump.first == "DECOUPLED_LM") {
                entry["faces"] = serialize_bspx_decoupled_lm(lump.second);
            } else {
                // unhandled BSPX lump, just write the raw data
                entry["lumpdata"] = hex_string(lump.second.data(), lump.second.size());
            }
        }
    }

    // lightmap atlas
#if 0
    for (int32_t i = 0; i < MAXLIGHTMAPS; i++) {
        if (auto lm = generate_lightmap_atlases(bsp, bspdata.bspx.entries, false); !lm.empty()) {
            j.emplace("lightmaps", std::move(lm));
        }

        if (bspdata.bspx.entries.find("LMOFFSET") != bspdata.bspx.entries.end()) {
            if (auto lm = generate_lightmap_atlases(bsp, bspdata.bspx.entries, true); !lm.empty()) {
                j.emplace("bspx_lightmaps", std::move(lm));
            }
        }
    }
#endif
    export_obj_and_lightmaps(bsp, bspdata.bspx.entries, false, true, fs::path(name).replace_extension(".geometry.obj"),
        fs::path(name).replace_extension(".lm.png"));

    std::ofstream(name, std::fstream::out | std::fstream::trunc) << std::setw(4) << j;

    logging::print("wrote {}\n", name);
}
