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

#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>

using namespace nlohmann;

static void SerializeBSP(const mbsp_t &bsp, const std::filesystem::path &name)
{
    json j = json::object();

    if (bsp.nummodels) {
        json &models = (j.emplace("models", json::array())).first.value();

        for (int32_t i = 0; i < bsp.nummodels; i++) {
            json &model = models.insert(models.end(), json::object()).value();
            auto &src_model = bsp.dmodels[i];

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

    if (bsp.numleafs) {
        json &leafs = (j.emplace("leafs", json::array())).first.value();

        for (int32_t i = 0; i < bsp.numleafs; i++) {
            json &leaf = leafs.insert(leafs.end(), json::object()).value();
            auto &src_leaf = bsp.dleafs[i];

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

    if (bsp.numplanes) {
        json &planes = (j.emplace("planes", json::array())).first.value();

        for (int32_t i = 0; i < bsp.numplanes; i++) {
            json &plane = planes.insert(planes.end(), json::object()).value();
            auto &src_plane = bsp.dplanes[i];

            plane.push_back({"normal", json::array({src_plane.normal[0], src_plane.normal[1], src_plane.normal[2]})});
            plane.push_back({"dist", src_plane.dist});
            plane.push_back({"type", src_plane.type});
        }
    }

    if (bsp.numnodes) {
        json &nodes = (j.emplace("nodes", json::array())).first.value();

        for (int32_t i = 0; i < bsp.numnodes; i++) {
            json &node = nodes.insert(nodes.end(), json::object()).value();
            auto &src_node = bsp.dnodes[i];

            node.push_back({"planenum", src_node.planenum});
            node.push_back({"children", json::array({src_node.children[0], src_node.children[1]})});
            node.push_back({"mins", json::array({src_node.mins[0], src_node.mins[1], src_node.mins[2]})});
            node.push_back({"maxs", json::array({src_node.maxs[0], src_node.maxs[1], src_node.maxs[2]})});
            node.push_back({"firstface", src_node.firstface});
            node.push_back({"numfaces", src_node.numfaces});
        }
    }

    if (bsp.numbrushsides) {
        json &brushsides = (j.emplace("brushsides", json::array())).first.value();

        for (int32_t i = 0; i < bsp.numbrushsides; i++) {
            json &brushside = brushsides.insert(brushsides.end(), json::object()).value();
            auto &src_brushside = bsp.dbrushsides[i];

            brushside.push_back({"planenum", src_brushside.planenum});
            brushside.push_back({"texinfo", src_brushside.texinfo});
        }
    }

    if (bsp.numbrushes) {
        json &brushes = (j.emplace("brushes", json::array())).first.value();

        for (int32_t i = 0; i < bsp.numbrushes; i++) {
            json &brush = brushes.insert(brushes.end(), json::object()).value();
            auto &src_brush = bsp.dbrushes[i];

            brush.push_back({"firstside", src_brush.firstside});
            brush.push_back({"numsides", src_brush.numsides});
            brush.push_back({"contents", src_brush.contents});
        }
    }

    if (bsp.numleafbrushes) {
        json &leafbrushes = (j.emplace("leafbrushes", json::array())).first.value();

        for (int32_t i = 0; i < bsp.numleafbrushes; i++) {
            leafbrushes.push_back(bsp.dleafbrushes[i]);
        }
    }

    std::ofstream(name, std::fstream::out | std::fstream::trunc | std::fstream::binary) << std::setw(4) << j;
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

        SerializeBSP(std::get<mbsp_t>(bsp.bsp), source);

        printf("---------------------\n");
    }

    return 0;
}
