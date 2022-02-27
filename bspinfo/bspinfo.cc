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

#include <common/cmdlib.hh>
#include <common/bspfile.hh>

#include <sstream>
#include <fstream>
#include <iomanip>
#include <fmt/ostream.h>
#include <common/json.hh>
#include "common/fs.hh"

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
static json serialize_bspxbrushlist(const bspxentry_t &lump)
{
    json j = json::array();

    memstream p(lump.lumpdata.get(), lump.lumpsize, std::ios_base::in | std::ios_base::binary);

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

static std::string serialize_image(const qvec3b *palette, const uint8_t *image, int32_t width, int32_t height)
{
    size_t bufsize = 122 + (width * height * 4);
    uint8_t *buf = new uint8_t[bufsize];

    memstream s(buf, bufsize, std::ios_base::out | std::ios_base::binary);

    s << endianness<std::endian::little>;

    s <= std::array<char, 2>{'B', 'M'};
    s <= (int32_t)bufsize;
    s <= (int16_t)0;
    s <= (int16_t)0;
    s <= (int32_t)122;
    s <= (int32_t)108;
    s <= width;
    s <= height;
    s <= (int16_t)1;
    s <= (int16_t)32;
    s <= (int32_t)3;
    s <= (int32_t)(width * height * 4);
    s <= (int32_t)2835;
    s <= (int32_t)2835;
    s <= (int32_t)0;
    s <= (int32_t)0;
    s <= (int32_t)0x00FF0000;
    s <= (int32_t)0x0000FF00;
    s <= (int32_t)0x000000FF;
    s <= (int32_t)0xFF000000;
    s <= std::array<char, 4>{'W', 'i', 'n', ' '};
    s <= std::array<char, 36>{};
    s <= (int32_t)0;
    s <= (int32_t)0;
    s <= (int32_t)0;

    for (size_t y = 0; y < height; y++) {
        for (size_t x = 0; x < width; x++) {
            const uint8_t *pixel = image + ((height - y - 1) * width) + x;

            if (*pixel == 255) {
                s <= (int32_t)0;
            } else {
                const qvec3b &color = palette[*pixel];
                s <= color[2];
                s <= color[1];
                s <= color[0];
                s <= (uint8_t)255;
            }
        }
    }

    std::string str{"data:image/bmp;base64,"};

    Base64EncodeTo(buf, bufsize, std::back_inserter(str));

    delete[] buf;

    return str;
}

static void serialize_bsp(const bspdata_t &bspdata, const mbsp_t &bsp, const std::filesystem::path &name)
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

            json &mips = tex["mips"] = json::array();

            const qvec3b *pal = src_tex.palette.empty() ? bspdata.loadversion->game->get_default_palette().data()
                                                        : src_tex.palette.data();

            for (size_t i = 0; i < src_tex.data.size(); i++) {
                mips.emplace_back(serialize_image(pal, src_tex.data[i].get(), src_tex.width >> i, src_tex.height >> i));
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
                    hex_string(reinterpret_cast<uint8_t *>(lump.second.lumpdata.get()), lump.second.lumpsize);
            }
        }
    }

    std::ofstream(name, std::fstream::out | std::fstream::trunc) << std::setw(4) << j;
}

#include "common/polylib.hh"
#include "common/bsputils.hh"

static void PrintBSPTextureUsage(const mbsp_t &bsp)
{
    std::unordered_map<std::string, vec_t> areas;

    for (auto &face : bsp.dfaces) {
        const char *name = Face_TextureName(&bsp, &face);

        if (!name || !*name) {
            continue;
        }

        auto points = GLM_FacePoints(&bsp, &face);
        polylib::winding_t w(points.begin(), points.end());
        vec_t area = w.area();

        areas[name] += area;
    }

    std::vector<std::tuple<std::string, vec_t>> areasVec;

    for (auto &area : areas) {
        areasVec.push_back(std::make_tuple(area.first, area.second));
    }

    std::sort(areasVec.begin(), areasVec.end(), [](auto &l, auto &r) { return std::get<1>(r) < std::get<1>(l); });

    printf("\n");

    for (auto &area : areasVec) {
        fmt::print("{},{:.0f}\n", std::get<0>(area), std::get<1>(area));
    }
}

int main(int argc, char **argv)
{
    printf("---- bspinfo / ericw-tools " stringify(ERICWTOOLS_VERSION) " ----\n");
    if (argc == 1) {
        printf("usage: bspinfo bspfile [bspfiles]\n");
        exit(1);
    }

    for (int32_t i = 1; i < argc; i++) {
        printf("---------------------\n");
        std::filesystem::path source = DefaultExtension(argv[i], ".bsp");
        fmt::print("{}\n", source);

        bspdata_t bsp;
        LoadBSPFile(source, &bsp);

        bsp.version->game->init_filesystem(source);

        PrintBSPFileSizes(&bsp);

        // WriteBSPFile(std::filesystem::path(source).replace_extension("bsp.rewrite"), &bsp);

        ConvertBSPFormat(&bsp, &bspver_generic);

        serialize_bsp(bsp, std::get<mbsp_t>(bsp.bsp), std::filesystem::path(source).replace_extension("bsp.json"));

        PrintBSPTextureUsage(std::get<mbsp_t>(bsp.bsp));

        printf("---------------------\n");

        fs::clear();
    }

    return 0;
}
