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
#include <nlohmann/json.hpp>

using namespace nlohmann;

static std::string hex_string(const uint8_t* bytes, const size_t count) {
    std::stringstream str;
    
    for (size_t i = 0; i < count; ++i) {
        fmt::print(str, "{:x}", bytes[i]);
    }    

    return str.str();
}

/**
 * returns a JSON array of models
 */
static json serialize_bspxbrushlist(const bspxentry_t &lump) {
    json j = json::array();

    const uint8_t *p = reinterpret_cast<uint8_t *>(lump.lumpdata.get()), *base = p;

    while (p < (base + lump.lumpsize)) {
        bspxbrushes_permodel src_model;
        memcpy(&src_model, p, sizeof(bspxbrushes_permodel));
        p += sizeof(src_model);

        json &model = j.insert(j.end(), json::object()).value();
        model["ver"] = src_model.ver;
        model["modelnum"] = src_model.modelnum;
        model["numbrushes"] = src_model.numbrushes;
        model["numfaces"] = src_model.numfaces;
        json &brushes = (model.emplace("brushes", json::array())).first.value();

        for (int32_t i = 0; i < src_model.numbrushes; ++i) {
            bspxbrushes_perbrush src_brush;
            memcpy(&src_brush, p, sizeof(bspxbrushes_perbrush));
            p += sizeof(src_brush);

            json &brush = brushes.insert(brushes.end(), json::object()).value();
            brush.push_back({ "mins", json::array({ src_brush.mins[0], src_brush.mins[1], src_brush.mins[2] }) });
            brush.push_back({ "maxs", json::array({ src_brush.maxs[0], src_brush.maxs[1], src_brush.maxs[2] }) });
            brush.push_back({ "contents", src_brush.contents });
            json &faces = (brush.emplace("faces", json::array())).first.value();

            for (int32_t j = 0; j < src_brush.numfaces; ++j) {
                bspxbrushes_perface src_face;
                memcpy(&src_face, p, sizeof(bspxbrushes_perface));
                p += sizeof(src_face);

                json &face = faces.insert(faces.end(), json::object()).value();
                face.push_back({ "normal", json::array({ src_face.normal[0], src_face.normal[1], src_face.normal[2] }) });
                face.push_back({ "dist", src_face.dist });
            }
        }
    }

    return j;
}

static void serialize_bsp(const bspdata_t &bspdata, const mbsp_t &bsp, const std::filesystem::path &name)
{
    json j = json::object();

    if (!bsp.dmodels.empty()) {
        json &models = (j.emplace("models", json::array())).first.value();

         for (auto &src_model : bsp.dmodels) {
           json &model = models.insert(models.end(), json::object()).value();

            model.push_back({"mins", json::array({src_model.mins[0], src_model.mins[1], src_model.mins[2]})});
            model.push_back({"maxs", json::array({src_model.maxs[0], src_model.maxs[1], src_model.maxs[2]})});
            model.push_back({"origin", json::array({src_model.origin[0], src_model.origin[1], src_model.origin[2]})});
            model.push_back({"headnode",
                json::array({src_model.headnode[0], src_model.headnode[1], src_model.headnode[2], src_model.headnode[3],
                    src_model.headnode[4], src_model.headnode[5], src_model.headnode[6], src_model.headnode[7]})});
            model.push_back({"visleafs", src_model.visleafs});
            model.push_back({"firstface", src_model.firstface});
            model.push_back({"numfaces", src_model.numfaces});
        }
    }

    if (bsp.dvis.bits.size()) {
        j["visdata"] = hex_string(bsp.dvis.bits.data(), bsp.dvis.bits.size());
    }

    if (bsp.dlightdata.size()) {
        j["lightdata"] = hex_string(bsp.dlightdata.data(), bsp.dlightdata.size());
    }

    if (!bsp.dentdata.empty()) {
        j["entdata"] = bsp.dentdata;
    }

    if (!bsp.dleafs.empty()) {
        json &leafs = (j.emplace("leafs", json::array())).first.value();

        for (auto &src_leaf : bsp.dleafs) {
            json &leaf = leafs.insert(leafs.end(), json::object()).value();

            leaf.push_back({"contents", src_leaf.contents});
            leaf.push_back({"visofs", src_leaf.visofs});
            leaf.push_back({"mins", json::array({src_leaf.mins[0], src_leaf.mins[1], src_leaf.mins[2]})});
            leaf.push_back({"maxs", json::array({src_leaf.maxs[0], src_leaf.maxs[1], src_leaf.maxs[2]})});
            leaf.push_back({"firstmarksurface", src_leaf.firstmarksurface});
            leaf.push_back({"nummarksurfaces", src_leaf.nummarksurfaces});
            leaf.push_back({"ambient_level", json::array({src_leaf.ambient_level[0], src_leaf.ambient_level[1],
                                                 src_leaf.ambient_level[2], src_leaf.ambient_level[3]})});
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

            plane.push_back({"normal", json::array({src_plane.normal[0], src_plane.normal[1], src_plane.normal[2]})});
            plane.push_back({"dist", src_plane.dist});
            plane.push_back({"type", src_plane.type});
        }
    }
    
    if (!bsp.dvertexes.empty()) {
        json &vertexes = (j.emplace("vertexes", json::array())).first.value();

        for (auto &src_vertex : bsp.dvertexes) {            
            vertexes.insert(vertexes.end(), json::array({src_vertex[0], src_vertex[1], src_vertex[2]}));
        }
    }

    if (!bsp.dnodes.empty()) {
        json &nodes = (j.emplace("nodes", json::array())).first.value();

        for (auto &src_node : bsp.dnodes) {
            json &node = nodes.insert(nodes.end(), json::object()).value();

            node.push_back({"planenum", src_node.planenum});
            node.push_back({"children", json::array({src_node.children[0], src_node.children[1]})});
            node.push_back({"mins", json::array({src_node.mins[0], src_node.mins[1], src_node.mins[2]})});
            node.push_back({"maxs", json::array({src_node.maxs[0], src_node.maxs[1], src_node.maxs[2]})});
            node.push_back({"firstface", src_node.firstface});
            node.push_back({"numfaces", src_node.numfaces});
        }
    }

    if (!bsp.texinfo.empty()) {
        json &texinfos = (j.emplace("texinfo", json::array())).first.value();

        for (auto &src_texinfo : bsp.texinfo) {
            json &texinfo = texinfos.insert(texinfos.end(), json::object()).value();
            
            texinfo.push_back({ "vecs", json::array({
                json::array({ src_texinfo.vecs[0][0], src_texinfo.vecs[0][1], src_texinfo.vecs[0][2], src_texinfo.vecs[0][3] }),
                json::array({ src_texinfo.vecs[1][0], src_texinfo.vecs[1][1], src_texinfo.vecs[1][2], src_texinfo.vecs[1][3] })
            })});
            texinfo.push_back({ "flags", src_texinfo.flags.native });
            texinfo.push_back({ "miptex", src_texinfo.miptex });
            texinfo.push_back({ "value", src_texinfo.value });
            texinfo.push_back({ "texture", std::string(src_texinfo.texture.data()) });
            texinfo.push_back({ "nexttexinfo", src_texinfo.nexttexinfo });
        }
    }
    
    if (bsp.numfaces) {
        json &faces = (j.emplace("faces", json::array())).first.value();

        for (int32_t i = 0; i < bsp.numfaces; i++) {
            json &face = faces.insert(faces.end(), json::object()).value();
            auto &src_face = bsp.dfaces[i];
            
            face.push_back({ "planenum", src_face.planenum });
            face.push_back({ "side", src_face.side });
            face.push_back({ "firstedge", src_face.firstedge });
            face.push_back({ "numedges", src_face.numedges });
            face.push_back({ "texinfo", src_face.texinfo });
            face.push_back({ "styles", json::array({ src_face.styles[0], src_face.styles[1], src_face.styles[2], src_face.styles[3] }) });
            face.push_back({ "lightofs", src_face.lightofs });

            // for readibility, also output the actual vertices
            auto verts = json::array();
            for (int32_t k = 0; k < src_face.numedges; ++k) {
                auto se = bsp.dsurfedges[src_face.firstedge + k];
                uint32_t v = (se < 0) ? bsp.dedges[-se].v[1] : bsp.dedges[se].v[0];
                auto dv = bsp.dvertexes[v];
                verts.push_back(json::array({ dv.point[0], dv.point[1], dv.point[2] }));
            }
            face.push_back({ "vertices", verts });
        }
    }

    if (!bsp.dclipnodes.empty()) {
        json &clipnodes = (j.emplace("clipnodes", json::array())).first.value();

        for (auto &src_clipnodes : bsp.dclipnodes) {
            json &clipnode = clipnodes.insert(clipnodes.end(), json::object()).value();

            clipnode.push_back({ "planenum", src_clipnodes.planenum });
            clipnode.push_back({ "children", json::array({ src_clipnodes.children[0], src_clipnodes.children[1] })});
        }
    }

    if (bsp.numedges) {
        json &edges = (j.emplace("edges", json::array())).first.value();

        for (int32_t i = 0; i < bsp.numedges; i++) {
            auto &src_edge = bsp.dedges[i];

            edges.insert(edges.end(), json::array({src_edge.v[0], src_edge.v[1]}));
        }
    }

    if (bsp.numleaffaces) {
        json &leaffaces = (j.emplace("leaffaces", json::array())).first.value();

        for (int32_t i = 0; i < bsp.numleaffaces; i++) {
            leaffaces.insert(leaffaces.end(), bsp.dleaffaces[i]);
        }
    }

    if (bsp.numsurfedges) {
        json &surfedges = (j.emplace("surfedges", json::array())).first.value();

        for (int32_t i = 0; i < bsp.numsurfedges; i++) {
            surfedges.insert(surfedges.end(), bsp.dsurfedges[i]);
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

    if (!bspdata.bspx.entries.empty()) {
        json &bspxentries = (j.emplace("bspxentries", json::array())).first.value();

        for (auto& lump : bspdata.bspx.entries) {
            json &entry = bspxentries.insert(bspxentries.end(), json::object()).value();
            entry["lumpname"] = lump.first;

            if (lump.first == "BRUSHLIST") {
                entry["models"] = serialize_bspxbrushlist(lump.second);
            } else {
                // unhandled BSPX lump, just write the raw data
                entry["lumpdata"] = hex_string(reinterpret_cast<uint8_t *>(lump.second.lumpdata.get()), lump.second.lumpsize);
            }
        }
    }

    std::ofstream(name, std::fstream::out | std::fstream::trunc) << std::setw(4) << j;
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
        PrintBSPFileSizes(&bsp);

        source.replace_extension("json");
        ConvertBSPFormat(&bsp, &bspver_generic);

        serialize_bsp(bsp, std::get<mbsp_t>(bsp.bsp), source);

        printf("---------------------\n");
    }

    return 0;
}
