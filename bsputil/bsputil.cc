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

#include <bsputil/bsputil.hh>

#include <cstdint>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "common/imglib.hh"

#include <common/cmdlib.hh>
#include <common/bspfile.hh>
#include <common/bsputils.hh>
#include <common/decompile.hh>
#include <common/mathlib.hh>
#include <common/fs.hh>
#include <common/settings.hh>
#include <common/ostream.hh>

#include <map>
#include <set>
#include <list>
#include <algorithm> // std::sort
#include <string>
#include <fstream>
#include <fmt/ostream.h>

// bsputil_settings

bool bsputil_settings::load_setting(const std::string &name, settings::source src)
{
    auto setting = std::make_unique<settings::setting_func>(nullptr, name, nullptr);
    operations.push_back(std::move(setting));
    return true;
}

bsputil_settings::bsputil_settings()
    : scale{this, "scale",
          [&](const std::string &name, parser_base_t &parser, settings::source src) {
              return this->load_setting<settings::setting_vec3>(name, parser, src, 0.f, 0.f, 0.f);
          },
          nullptr, "Scale the BSP by the given scalar vectors (can be negative, too)"},
      replace_entities{this, "replace-entities",
          [&](const std::string &name, parser_base_t &parser, settings::source src) {
              return this->load_setting<settings::setting_string>(name, parser, src, "");
          },
          nullptr, "Replace BSP entities with the given files' contents"},
      extract_entities{this, "extract-entities",
          [&](const std::string &name, parser_base_t &parser, settings::source src) {
              return this->load_setting<settings::setting_bool>(name, parser, src, "");
          },
          nullptr, "Extract BSP entities to the given file name"},
      extract_textures{this, "extract-textures",
          [&](const std::string &name, parser_base_t &parser, settings::source src) {
              return this->load_setting<settings::setting_bool>(name, parser, src, "");
          },
          nullptr, "Extract BSP texutres to the given wad file"},
      replace_textures{this, "replace-textures",
          [&](const std::string &name, parser_base_t &parser, settings::source src) {
              return this->load_setting<settings::setting_string>(name, parser, src, "");
          },
          nullptr, "Replace BSP textures with the given wads' contents"},
      convert{this, "convert",
          [&](const std::string &name, parser_base_t &parser, settings::source src) {
              return this->load_setting<settings::setting_string>(name, parser, src, "");
          },
          nullptr, "Convert the BSP file to a different BSP format"},
      check{this, "check",
          [&](const std::string &name, parser_base_t &parser, settings::source src) {
              return this->load_setting(name, src);
          },
          nullptr, "Check/verify BSP data"},
      modelinfo{this, "modelinfo",
          [&](const std::string &name, parser_base_t &parser, settings::source src) {
              return this->load_setting(name, src);
          },
          nullptr, "Print model info"},
      findfaces{this, "findfaces",
          [&](const std::string &name, parser_base_t &parser, settings::source src) {
              auto pos = std::make_shared<settings::setting_vec3>(nullptr, name, 0.f, 0.f, 0.f);
              if (bool parsed = pos->parse(name, parser, src); !parsed)
                  return false;
              auto norm = std::make_shared<settings::setting_vec3>(nullptr, name, 0.f, 0.f, 0.f);
              if (bool parsed = norm->parse(name, parser, src); !parsed)
                  return false;
              operations.push_back(std::make_unique<setting_combined>(
                  nullptr, name, std::initializer_list<std::shared_ptr<settings::setting_base>>{pos, norm}));
              return true;
          },
          nullptr, "Find faces with specified pos/normal"},
      findleaf{this, "findleaf",
          [&](const std::string &name, parser_base_t &parser, settings::source src) {
              return this->load_setting<settings::setting_vec3>(name, parser, src, 0.f, 0.f, 0.f);
          },
          nullptr, "Find closest leaf"},
      settexinfo{this, "settexinfo",
          [&](const std::string &name, parser_base_t &parser, settings::source src) {
              auto faceNum = std::make_shared<settings::setting_int32>(nullptr, name, 0);
              if (bool parsed = faceNum->parse(name, parser, src); !parsed)
                  return false;
              auto texInfoNum = std::make_shared<settings::setting_int32>(nullptr, name, 0);
              if (bool parsed = texInfoNum->parse(name, parser, src); !parsed)
                  return false;
              operations.push_back(std::make_unique<setting_combined>(
                  nullptr, name, std::initializer_list<std::shared_ptr<settings::setting_base>>{faceNum, texInfoNum}));
              return true;
          },
          nullptr, "Set texinfo"},
      decompile{this, "decompile",
          [&](const std::string &name, parser_base_t &parser, settings::source src) {
              return this->load_setting(name, src);
          },
          nullptr, "Decompile to the given .map file"},
      decompile_geomonly{this, "decompile-geomonly",
          [&](const std::string &name, parser_base_t &parser, settings::source src) {
              return this->load_setting(name, src);
          },
          nullptr, "Decompile"},
      decompile_ignore_brushes{this, "decompile-ignore-brushes",
          [&](const std::string &name, parser_base_t &parser, settings::source src) {
              return this->load_setting(name, src);
          },
          nullptr, "Decompile entities only"},
      decompile_hull{this, "decompile-hull",
          [&](const std::string &name, parser_base_t &parser, settings::source src) {
              return this->load_setting<settings::setting_int32>(name, parser, src, 0);
          },
          nullptr, "Decompile specific hull"},
      extract_bspx_lump{this, "extract-bspx-lump",
          [&](const std::string &name, parser_base_t &parser, settings::source src) {
              auto lump = std::make_shared<settings::setting_string>(nullptr, name, "");
              if (bool parsed = lump->parse(name, parser, src); !parsed)
                  return false;
              auto output = std::make_shared<settings::setting_string>(nullptr, name, "");
              if (bool parsed = output->parse(name, parser, src); !parsed)
                  return false;
              operations.push_back(std::make_unique<setting_combined>(
                  nullptr, name, std::initializer_list<std::shared_ptr<settings::setting_base>>{lump, output}));
              return true;
          },
          nullptr, "Extract a BSPX lump"},
      insert_bspx_lump{this, "insert-bspx-lump",
          [&](const std::string &name, parser_base_t &parser, settings::source src) {
              auto lump = std::make_shared<settings::setting_string>(nullptr, name, "");
              if (bool parsed = lump->parse(name, parser, src); !parsed)
                  return false;
              auto input = std::make_shared<settings::setting_string>(nullptr, name, "");
              if (bool parsed = input->parse(name, parser, src); !parsed)
                  return false;
              operations.push_back(std::make_unique<setting_combined>(
                  nullptr, name, std::initializer_list<std::shared_ptr<settings::setting_base>>{lump, input}));
              return true;
          },
          nullptr, "Insert a BSPX lump"},
      remove_bspx_lump{this, "remove-bspx-lump",
          [&](const std::string &name, parser_base_t &parser, settings::source src) {
              return this->load_setting<settings::setting_string>(name, parser, src, "");
          },
          nullptr, "Remove a BSPX lump"},
      svg{this, "svg",
          [&](const std::string &name, parser_base_t &parser, settings::source src) {
              return this->load_setting<settings::setting_int32>(name, parser, src, 0);
          },
          nullptr, "Create an SVG view of the input BSP"}
{
}

bsputil_settings bsputil_options;

/* FIXME - share header with qbsp, etc. */
struct wadinfo_t
{
    std::array<char, 4> identification = {'W', 'A', 'D', '2'}; // should be WAD2
    int32_t numlumps;
    int32_t infotableofs = sizeof(wadinfo_t);

    auto stream_data() { return std::tie(identification, numlumps, infotableofs); }
};

struct lumpinfo_t
{
    int32_t filepos;
    int32_t disksize;
    int32_t size; // uncompressed
    char type;
    char compression;
    char pad1, pad2;
    std::array<char, 16> name; // must be null terminated

    auto stream_data() { return std::tie(filepos, disksize, size, type, compression, pad1, pad2, name); }
};

void ExportWad(std::ofstream &wadfile, const mbsp_t *bsp)
{
    int filepos, numvalid;
    const auto &texdata = bsp->dtex;

    /* Count up the valid lumps */
    numvalid = 0;
    for (auto &texture : texdata.textures) {
        if (texture.data.size() > sizeof(dmiptex_t)) {
            numvalid++;
        }
    }

    // Write out
    wadinfo_t header;
    header.numlumps = numvalid;
    wadfile <= header;

    lumpinfo_t lump{};
    lump.type = 'D';

    /* Miptex data will follow the lump headers */
    filepos = sizeof(header) + numvalid * sizeof(lump);
    for (auto &miptex : texdata.textures) {
        if (miptex.data.size() <= sizeof(dmiptex_t))
            continue;

        lump.filepos = filepos;
        lump.size = sizeof(dmiptex_t) + miptex.width * miptex.height / 64 * 85;
        lump.disksize = lump.size;
        snprintf(lump.name.data(), sizeof(lump.name), "%s", miptex.name.data());

        filepos += lump.disksize;

        // Write it out
        wadfile <= lump;
    }
    for (auto &miptex : texdata.textures) {
        if (miptex.data.size() > sizeof(dmiptex_t)) {
            miptex.stream_write(wadfile);
        }
    }
}

static void ReplaceTexturesFromWad(mbsp_t &bsp)
{
    auto &texdata = bsp.dtex;

    for (miptex_t &tex : texdata.textures) {
        logging::print("bsp texture: {}\n", tex.name);

        // see if this texture in the .bsp is in the wad?
        if (auto [wadtex_opt, _0, mipdata] =
                img::load_texture(tex.name, false, bsp.loadversion->game, bsputil_options, false, true);
            wadtex_opt) {
            const img::texture &wadtex = *wadtex_opt;

            if (tex.width != wadtex.width || tex.height != wadtex.height) {
                logging::print("    size {}x{} in bsp does not match replacement texture {}x{}\n", tex.width,
                    tex.height, wadtex.width, wadtex.height);
                continue;
            }

            // update the bsp miptex
            tex.null_texture = false;
            tex.data = *mipdata;
            logging::print("    replaced with {} from wad\n", wadtex.meta.name);
        }
    }
}

static void PrintModelInfo(const mbsp_t *bsp)
{
    // TODO: remove, bspinfo .json export is more useful
    for (size_t i = 0; i < bsp->dmodels.size(); i++) {
        const dmodelh2_t *dmodel = &bsp->dmodels[i];
        logging::print("model {:3}: {:5} faces (firstface = {})\n", i, dmodel->numfaces, dmodel->firstface);
    }
}

/*
 * Quick hack to check verticies of faces lie on the correct plane
 */
constexpr double PLANE_ON_EPSILON = 0.01;

static void CheckBSPFacesPlanar(const mbsp_t *bsp)
{
    for (size_t i = 0; i < bsp->dfaces.size(); i++) {
        const mface_t *face = BSP_GetFace(bsp, i);
        dplane_t plane = bsp->dplanes[face->planenum];

        if (face->side) {
            plane = -plane;
        }

        for (size_t j = 0; j < face->numedges; j++) {
            const int edgenum = bsp->dsurfedges[face->firstedge + j];
            const int vertnum = (edgenum >= 0) ? bsp->dedges[edgenum][0] : bsp->dedges[-edgenum][1];
            const qvec3f &point = bsp->dvertexes[vertnum];
            const float dist = plane.distance_to(point);

            if (dist < -PLANE_ON_EPSILON || dist > PLANE_ON_EPSILON)
                logging::print("WARNING: face {}, point {} off plane by {}\n", i, j, dist);
        }
    }
}

static int Node_Height(const mbsp_t *bsp, const bsp2_dnode_t *node, std::map<const bsp2_dnode_t *, int> *cache)
{
    // leafs have a height of 0
    twosided<int32_t> child_heights = {0, 0};

    for (int i = 0; i < 2; i++) {
        const int child = node->children[i];
        if (child >= 0) {
            child_heights[i] = Node_Height(bsp, &bsp->dnodes[child], cache);
        }
    }

    const int height = std::max(child_heights[0], child_heights[1]) + 1;
    if (cache)
        (*cache)[node] = height;
    return height;
}

static void PrintNodeHeights(const mbsp_t *bsp)
{
    // get all the heights in one go.
    const bsp2_dnode_t *headnode = &bsp->dnodes[bsp->dmodels[0].headnode[0]];
    std::map<const bsp2_dnode_t *, int> cache;
    Node_Height(bsp, headnode, &cache);

    const int maxlevel = 3;

    using level_t = int;
    using visit_t = std::pair<const bsp2_dnode_t *, level_t>;

    int current_level = -1;

    std::list<visit_t> tovisit{std::make_pair(headnode, 0)};
    while (!tovisit.empty()) {
        const auto n = tovisit.front();
        tovisit.pop_front();

        const bsp2_dnode_t *node = n.first;
        const int level = n.second;

        Q_assert(level <= maxlevel);

        // handle this node
        if (level != current_level) {
            current_level = level;
            logging::print("\nNode heights at level {}: ", level);
        }

        // print the level of this node
        logging::print("{}, ", cache.at(node));

        // add child nodes to the bfs
        if (level < maxlevel) {
            for (int i = 0; i < 2; i++) {
                const int child = node->children[i];
                if (child >= 0) {
                    tovisit.emplace_back(&bsp->dnodes[child], level + 1);
                }
            }
        }
    }
    printf("\n");
}

static void CheckBSPFile(const mbsp_t *bsp)
{
    int i;

    // FIXME: Should do a better reachability check where we traverse the
    // nodes/leafs to find reachable faces.
    std::set<int32_t> referenced_texinfos;
    std::set<int32_t> referenced_planenums;
    std::set<uint32_t> referenced_vertexes;
    std::set<uint8_t> used_lightstyles;

    /* faces */
    for (i = 0; i < bsp->dfaces.size(); i++) {
        const mface_t *face = BSP_GetFace(bsp, i);

        /* texinfo bounds check */
        if (face->texinfo < 0)
            logging::print("warning: face {} has negative texinfo ({})\n", i, face->texinfo);
        if (face->texinfo >= bsp->texinfo.size())
            logging::print(
                "warning: face {} has texinfo out of range ({} >= {})\n", i, face->texinfo, bsp->texinfo.size());
        referenced_texinfos.insert(face->texinfo);

        /* planenum bounds check */
        if (face->planenum < 0)
            logging::print("warning: face {} has negative planenum ({})\n", i, face->planenum);
        if (face->planenum >= bsp->dplanes.size())
            fmt::print(
                "warning: face {} has planenum out of range ({} >= {})\n", i, face->planenum, bsp->dplanes.size());
        referenced_planenums.insert(face->planenum);

        /* lightofs check */
        if (face->lightofs < -1)
            logging::print("warning: face {} has negative light offset ({})\n", i, face->lightofs);
        if (face->lightofs >= bsp->dlightdata.size())
            logging::print("warning: face {} has light offset out of range "
                           "({} >= {})\n",
                i, face->lightofs, bsp->dlightdata.size());

        /* edge check */
        if (face->firstedge < 0)
            logging::print("warning: face {} has negative firstedge ({})\n", i, face->firstedge);
        if (face->numedges < 3)
            logging::print("warning: face {} has < 3 edges ({})\n", i, face->numedges);
        if (face->firstedge + face->numedges > bsp->dsurfedges.size())
            logging::print("warning: face {} has edges out of range ({}..{} >= {})\n", i, face->firstedge,
                face->firstedge + face->numedges - 1, bsp->dsurfedges.size());

        for (int j = 0; j < 4; j++) {
            used_lightstyles.insert(face->styles[j]);
        }
    }

    /* edges */
    for (i = 0; i < bsp->dedges.size(); i++) {
        const bsp2_dedge_t *edge = &bsp->dedges[i];
        int j;

        for (j = 0; j < 2; j++) {
            const uint32_t vertex = (*edge)[j];
            if (vertex > bsp->dvertexes.size())
                logging::print("warning: edge {} has vertex {} out range "
                               "({} >= {})\n",
                    i, j, vertex, bsp->dvertexes.size());
            referenced_vertexes.insert(vertex);
        }
    }

    /* surfedges */
    for (i = 0; i < bsp->dsurfedges.size(); i++) {
        const int edgenum = bsp->dsurfedges[i];
        if (!edgenum)
            logging::print("warning: surfedge {} has zero value!\n", i);
        if (std::abs(edgenum) >= bsp->dedges.size())
            logging::print("warning: surfedge {} is out of range (abs({}) >= {})\n", i, edgenum, bsp->dedges.size());
    }

    /* marksurfaces */
    for (i = 0; i < bsp->dleaffaces.size(); i++) {
        const uint32_t surfnum = bsp->dleaffaces[i];
        if (surfnum >= bsp->dfaces.size())
            logging::print("warning: marksurface {} is out of range ({} >= {})\n", i, surfnum, bsp->dfaces.size());
    }

    /* leafs */
    for (i = 0; i < bsp->dleafs.size(); i++) {
        const mleaf_t *leaf = &bsp->dleafs[i];
        const uint32_t endmarksurface = leaf->firstmarksurface + leaf->nummarksurfaces;
        if (endmarksurface > bsp->dleaffaces.size())
            logging::print("warning: leaf {} has marksurfaces out of range "
                           "({}..{} >= {})\n",
                i, leaf->firstmarksurface, endmarksurface - 1, bsp->dleaffaces.size());
        if (leaf->visofs < -1)
            logging::print("warning: leaf {} has negative visdata offset ({})\n", i, leaf->visofs);
        if (leaf->visofs >= bsp->dvis.bits.size())
            logging::print("warning: leaf {} has visdata offset out of range "
                           "({} >= {})\n",
                i, leaf->visofs, bsp->dvis.bits.size());
    }

    /* nodes */
    for (i = 0; i < bsp->dnodes.size(); i++) {
        const bsp2_dnode_t *node = &bsp->dnodes[i];
        int j;

        for (j = 0; j < 2; j++) {
            const int32_t child = node->children[j];
            if (child >= 0 && child >= bsp->dnodes.size())
                logging::print("warning: node {} has child {} (node) out of range "
                               "({} >= {})\n",
                    i, j, child, bsp->dnodes.size());
            if (child < 0 && -child - 1 >= bsp->dleafs.size())
                logging::print("warning: node {} has child {} (leaf) out of range "
                               "({} >= {})\n",
                    i, j, -child - 1, bsp->dleafs.size());
        }

        if (node->children[0] == node->children[1]) {
            logging::print("warning: node {} has both children {}\n", i, node->children[0]);
        }

        referenced_planenums.insert(node->planenum);
    }

    /* clipnodes */
    for (i = 0; i < bsp->dclipnodes.size(); i++) {
        const bsp2_dclipnode_t *clipnode = &bsp->dclipnodes[i];

        for (int j = 0; j < 2; j++) {
            const int32_t child = clipnode->children[j];
            if (child >= 0 && child >= bsp->dclipnodes.size())
                logging::print("warning: clipnode {} has child {} (clipnode) out of range "
                               "({} >= {})\n",
                    i, j, child, bsp->dclipnodes.size());
            if (child < 0 && child < CONTENTS_MIN)
                logging::print("warning: clipnode {} has invalid contents ({}) for child {}\n", i, child, j);
        }

        if (clipnode->children[0] == clipnode->children[1]) {
            logging::print("warning: clipnode {} has both children {}\n", i, clipnode->children[0]);
        }

        referenced_planenums.insert(clipnode->planenum);
    }

    /* TODO: finish range checks, add "unreferenced" checks... */

    /* unreferenced texinfo */
    {
        int num_unreferenced_texinfo = 0;
        for (i = 0; i < bsp->texinfo.size(); i++) {
            if (referenced_texinfos.find(i) == referenced_texinfos.end()) {
                num_unreferenced_texinfo++;
            }
        }
        if (num_unreferenced_texinfo)
            logging::print("warning: {} texinfos are unreferenced\n", num_unreferenced_texinfo);
    }

    /* unreferenced planes */
    {
        int num_unreferenced_planes = 0;
        for (i = 0; i < bsp->dplanes.size(); i++) {
            if (referenced_planenums.find(i) == referenced_planenums.end()) {
                num_unreferenced_planes++;
            }
        }
        if (num_unreferenced_planes)
            logging::print("warning: {} planes are unreferenced\n", num_unreferenced_planes);
    }

    /* unreferenced vertices */
    {
        int num_unreferenced_vertexes = 0;
        for (i = 0; i < bsp->dvertexes.size(); i++) {
            if (referenced_vertexes.find(i) == referenced_vertexes.end()) {
                num_unreferenced_vertexes++;
            }
        }
        if (num_unreferenced_vertexes)
            logging::print("warning: {} vertexes are unreferenced\n", num_unreferenced_vertexes);
    }

    /* tree balance */
    PrintNodeHeights(bsp);

    /* unique visofs's */
    std::set<int32_t> visofs_set;
    for (i = 0; i < bsp->dleafs.size(); i++) {
        const mleaf_t *leaf = &bsp->dleafs[i];
        if (leaf->visofs >= 0) {
            visofs_set.insert(leaf->visofs);
        }
    }
    logging::print("{} unique visdata offsets for {} leafs\n", visofs_set.size(), bsp->dleafs.size());
    logging::print("{} visleafs in world model\n", bsp->dmodels[0].visleafs);

    /* unique lightstyles */
    logging::print("{} lightstyles used:\n", used_lightstyles.size());
    {
        std::vector<uint8_t> v(used_lightstyles.begin(), used_lightstyles.end());
        std::sort(v.begin(), v.end());
        for (uint8_t style : v) {
            logging::print("\t{}\n", style);
        }
    }

    logging::print("world mins: {} maxs: {}\n", bsp->dmodels[0].mins, bsp->dmodels[0].maxs);
}

static void FindFaces(const mbsp_t *bsp, const qvec3d &pos, const qvec3d &normal)
{
    for (int i = 0; i < bsp->dmodels.size(); ++i) {
        const dmodelh2_t *model = &bsp->dmodels[i];
        const mface_t *face = BSP_FindFaceAtPoint(bsp, model, pos, normal);

        if (face != nullptr) {
            logging::print("model {} face {}: texture '{}' texinfo {}\n", i, Face_GetNum(bsp, face),
                Face_TextureName(bsp, face), face->texinfo);
        }
    }
}

static void FindLeaf(const mbsp_t *bsp, const qvec3d &pos)
{
    const mleaf_t *leaf = BSP_FindLeafAtPoint(bsp, &bsp->dmodels[0], pos);

    logging::print("leaf {}: contents {} ({})\n", (leaf - bsp->dleafs.data()), leaf->contents,
        bsp->loadversion->game->create_contents_from_native(leaf->contents).to_string());
}

// map file stuff
struct map_entity_t
{
    entdict_t epairs;
    parser_source_location location;
    std::string map_brushes; // raw brush data
};

struct map_file_t
{
    std::vector<map_entity_t> entities;
};

static void ParseEpair(parser_t &parser, map_entity_t &entity)
{
    std::string key = parser.token;

    // trim whitespace from start/end
    while (std::isspace(key.front())) {
        key.erase(key.begin());
    }
    while (std::isspace(key.back())) {
        key.erase(key.end() - 1);
    }

    parser.parse_token(PARSE_SAMELINE);

    entity.epairs.set(key, parser.token);
}

bool ParseEntity(parser_t &parser, map_entity_t &entity)
{
    entity.location = parser.location;

    if (!parser.parse_token()) {
        return false;
    }

    if (parser.token != "{") {
        FError("{}: Invalid entity format, {{ not found", parser.location);
    }

    do {
        if (!parser.parse_token())
            FError("Unexpected EOF (no closing brace)");
        if (parser.token == "}")
            break;
        else if (parser.token == "{") {
            auto start = parser.pos - 1;

            // skip until a }
            do {
                if (!parser.parse_token()) {
                    FError("Unexpected EOF (no closing brace)");
                }
            } while (parser.token != "}");

            auto end = parser.pos;
            entity.map_brushes += std::string(start, end) + "\n";
        } else {
            ParseEpair(parser, entity);
        }
    } while (1);

    return true;
}

map_file_t LoadMapOrEntFile(const fs::path &source)
{
    logging::funcheader();

    auto file = fs::load(source);
    map_file_t map;

    if (!file) {
        FError("Couldn't load map/entity file \"{}\".\n", source);
        return map;
    }

    parser_t parser(file, {source.string()});

    for (;;) {
        map_entity_t &entity = map.entities.emplace_back();

        if (!ParseEntity(parser, entity)) {
            break;
        }
    }

    // Remove dummy entity inserted above
    assert(!map.entities.back().epairs.size());
    map.entities.pop_back();

    return map;
}

struct planepoints : std::array<qvec3d, 3>
{
    qplane3d plane() const
    {
        /* calculate the normal/dist plane equation */
        qvec3d ab = at(0) - at(1);
        qvec3d cb = at(2) - at(1);
        qvec3d normal = qv::normalize(qv::cross(ab, cb));
        return {normal, qv::dot(at(1), normal)};
    }
};

template<typename T>
static planepoints NormalDistanceToThreePoints(const qplane3<T> &plane)
{
    std::tuple<qvec3d, qvec3d> tanBitan = qv::MakeTangentAndBitangentUnnormalized(plane.normal);

    qvec3d point0 = plane.normal * plane.dist;

    return {point0, point0 + std::get<1>(tanBitan), point0 + std::get<0>(tanBitan)};
}

#include <pareto/spatial_map.h>

struct planelist_t
{
    // planes indices (into the `planes` vector)
    pareto::spatial_map<double, 4, size_t> plane_hash;
    std::vector<dplane_t> planes;

    // add the specified plane to the list
    size_t add_plane(const dplane_t &plane)
    {
        planes.emplace_back(plane);
        planes.emplace_back(-plane);

        size_t positive_index = planes.size() - 2;
        size_t negative_index = planes.size() - 1;

        auto &positive = planes[positive_index];
        auto &negative = planes[negative_index];

        size_t result;

        if (positive.normal[static_cast<int32_t>(positive.type) % 3] < 0.0) {
            std::swap(positive, negative);
            result = negative_index;
        } else {
            result = positive_index;
        }

        plane_hash.emplace(
            pareto::point<double, 4>{positive.normal[0], positive.normal[1], positive.normal[2], positive.dist},
            positive_index);
        plane_hash.emplace(
            pareto::point<double, 4>{negative.normal[0], negative.normal[1], negative.normal[2], negative.dist},
            negative_index);

        return result;
    }

    std::optional<size_t> find_plane_nonfatal(const dplane_t &plane)
    {
        constexpr double HALF_NORMAL_EPSILON = NORMAL_EPSILON * 0.5;
        constexpr double HALF_DIST_EPSILON = DIST_EPSILON * 0.5;

        if (auto it = plane_hash.find_intersection(
                {plane.normal[0] - HALF_NORMAL_EPSILON, plane.normal[1] - HALF_NORMAL_EPSILON,
                    plane.normal[2] - HALF_NORMAL_EPSILON, plane.dist - HALF_DIST_EPSILON},
                {plane.normal[0] + HALF_NORMAL_EPSILON, plane.normal[1] + HALF_NORMAL_EPSILON,
                    plane.normal[2] + HALF_NORMAL_EPSILON, plane.dist + HALF_DIST_EPSILON});
            it != plane_hash.end()) {
            return it->second;
        }

        return std::nullopt;
    }

    // find the specified plane in the list if it exists. throws
    // if not.
    size_t find_plane(const dplane_t &plane)
    {
        if (auto index = find_plane_nonfatal(plane)) {
            return *index;
        }

        throw std::bad_function_call();
    }

    // find the specified plane in the list if it exists, or
    // return a new one
    size_t add_or_find_plane(const dplane_t &plane)
    {
        if (auto index = find_plane_nonfatal(plane)) {
            return *index;
        }

        return add_plane(plane);
    }
};

int bsputil_main(int _argc, const char **_argv)
{
    logging::preinitialize();

    bsputil_options.preinitialize(_argc, _argv);
    bsputil_options.initialize(_argc - 1, _argv + 1);
    bsputil_options.postinitialize(_argc, _argv);

    logging::init(std::nullopt, bsputil_options);

    if (bsputil_options.remainder.size() != 1 || bsputil_options.operations.empty()) {
        bsputil_options.print_help(true);
        return 1;
    }

    bspdata_t bspdata;

    fs::path source = bsputil_options.remainder[0];

    if (!fs::exists(source)) {
        source = DefaultExtension(source, "bsp");
    }

    logging::print("---------------------\n");
    logging::print("{}\n", source);

    map_file_t map_file;

    if (string_iequals(source.extension().string(), ".bsp")) {
        LoadBSPFile(source, &bspdata);

        bspdata.version->game->init_filesystem(source, bsputil_options);

        ConvertBSPFormat(&bspdata, &bspver_generic);
    } else {
        map_file = LoadMapOrEntFile(source);
    }

    for (auto &operation : bsputil_options.operations) {
        if (operation->primary_name() == "svg") {
            fs::path svg = fs::path(source).replace_extension(".svg");
            std::ofstream f(svg, std::ios_base::out);

            f << R"(<?xml version="1.0" encoding="UTF-8"?>)" << std::endl;
            f << R"(<!DOCTYPE svg PUBLIC "-//W3C//DTD SVG 1.1//EN" "http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd">)"
              << std::endl;

            auto &bsp = std::get<mbsp_t>(bspdata.bsp);

            img::load_textures(&bsp, {});

            struct rendered_faces_t
            {
                std::vector<const mface_t *> faces;
                qvec3f origin;
                aabb3f bounds;
            };

            std::vector<rendered_faces_t> faces;
            aabb3f total_bounds;
            size_t total_faces = 0;
            auto ents = EntData_Parse(bsp);

            auto addSubModel = [&bsp, &faces, &total_bounds, &total_faces](int32_t index, qvec3f origin) {
                auto &model = bsp.dmodels[index];
                rendered_faces_t f{{}, origin};

                std::vector<size_t> face_ids;
                face_ids.reserve(model.numfaces);

                for (size_t i = model.firstface; i < model.firstface + model.numfaces; i++) {
                    auto &face = bsp.dfaces[i];

                    if (face.texinfo == -1)
                        continue;

                    auto &texinfo = bsp.texinfo[face.texinfo];

                    if (texinfo.flags.is_nodraw())
                        continue;
                    // TODO
                    // else if (texinfo.flags.native & Q2_SURF_SKY)
                    //    continue;
                    else if (!Q_strcasecmp(Face_TextureName(&bsp, &face), "trigger"))
                        continue;

                    auto norm = Face_Normal(&bsp, &face);

                    if (qv::dot(qvec3d(0, 0, 1), norm) <= DEFAULT_ON_EPSILON)
                        continue;

                    face_ids.push_back(i);
                }

                std::sort(face_ids.begin(), face_ids.end(), [&bsp](size_t a, size_t b) {
                    float za = std::numeric_limits<float>::lowest();
                    float zb = za;
                    auto &facea = bsp.dfaces[a];
                    auto &faceb = bsp.dfaces[b];

                    for (size_t e = 0; e < facea.numedges; e++)
                        za = std::max(za, Face_PointAtIndex(&bsp, &facea, e)[2]);

                    for (size_t e = 0; e < faceb.numedges; e++)
                        zb = std::max(zb, Face_PointAtIndex(&bsp, &faceb, e)[2]);

                    return za < zb;
                });

                for (auto &face_index : face_ids) {
                    const auto &face = bsp.dfaces[face_index];
                    f.faces.push_back(&face);

                    for (auto pt : Face_Points(&bsp, &face))
                        f.bounds += f.origin + pt;
                }

                if (f.faces.empty())
                    return;

                total_bounds += f.bounds;
                total_faces += f.faces.size();
                faces.push_back(std::move(f));
            };

            addSubModel(0, {});

            for (auto &entity : ents) {
                if (!entity.has("model"))
                    continue;

                qvec3f origin{};
                int32_t model = atoi(entity.get("model").substr(1).c_str());

                if (entity.has("origin"))
                    entity.get_vector("origin", origin);

                addSubModel(model, origin);
            }

            total_bounds = total_bounds.grow(32);

            float xo = total_bounds.mins()[0];
            float yo = total_bounds.mins()[1];
            // float zo = total_bounds.mins()[2];

            float xs = total_bounds.maxs()[0] - xo;
            float ys = total_bounds.maxs()[1] - yo;
            // float zs = total_bounds.maxs()[2] - zo;

            fmt::print(f, R"(<svg xmlns="http://www.w3.org/2000/svg" version="1.1" width="{}" height="{}">)", xs, ys);
            f << std::endl;

            f << R"(<defs><g id="bsp">)" << std::endl;

            struct face_id_t
            {
                size_t model;
                size_t face;
            };
            std::vector<face_id_t> face_ids;
            face_ids.reserve(total_faces);

            for (size_t i = 0; i < faces.size(); i++)
                for (size_t f = 0; f < faces[i].faces.size(); f++)
                    face_ids.push_back(face_id_t{i, f});

            std::sort(face_ids.begin(), face_ids.end(), [&bsp, &faces, yo](face_id_t a, face_id_t b) {
                float za = yo;
                float zb = yo;
                auto facea = faces[a.model].faces[a.face];
                auto faceb = faces[b.model].faces[b.face];

                for (size_t e = 0; e < facea->numedges; e++)
                    za = std::max(za, Face_PointAtIndex(&bsp, facea, e)[2] + faces[a.model].origin[2]);

                for (size_t e = 0; e < faceb->numedges; e++)
                    zb = std::max(zb, Face_PointAtIndex(&bsp, faceb, e)[2] + faces[b.model].origin[2]);

                return za < zb;
            });

            float low_z = total_bounds.maxs()[2], high_z = total_bounds.mins()[2];

            for (auto &face_index : face_ids) {
                auto face = faces[face_index.model].faces[face_index.face];

                for (auto &pt : Face_Points(&bsp, face)) {
                    low_z = std::min(low_z, pt[2] + faces[face_index.model].origin[2]);
                    high_z = std::max(high_z, pt[2] + faces[face_index.model].origin[2]);
                }
            }

            for (auto &face_index : face_ids) {
                auto face = faces[face_index.model].faces[face_index.face];
                auto pts = Face_Points(&bsp, face);
                std::string pts_str;
                float nz = xo;

                for (auto &pt : pts) {
                    fmt::format_to(std::back_inserter(pts_str), "{},{} ",
                        (pt[0] + faces[face_index.model].origin[0]) - xo,
                        ys - ((pt[1] + faces[face_index.model].origin[1]) - yo));
                    nz = std::max(nz, pt[2] + faces[face_index.model].origin[2]);
                }

                float z_scale = (nz - low_z) / (high_z - low_z);
                float d = (0.5 + (z_scale * 0.5));
                qvec3b color{255, 255, 255};

                const char *tex = Face_TextureName(&bsp, face);

                if (tex) {
                    if (auto texptr = img::find(tex))
                        color = texptr->averageColor;
                }

                fmt::print(f, R"svg(<polygon points="{}" fill="rgb({}, {}, {})" />)svg", pts_str, color[0] * d,
                    color[1] * d, color[2] * d);
                f << std::endl;
            }

            f << R"(</g></defs>)" << std::endl;

            f << R"(<use href="#bsp" fill="none" stroke="black" stroke-width="15" stroke-miterlimit="0" />)"
              << std::endl;
            f << R"(<use href="#bsp" fill="white" stroke="black" stroke-width="1" />)" << std::endl;

            f << R"(</svg>)" << std::endl;
        } else if (operation->primary_name() == "scale") {
            qvec3d scalar = dynamic_cast<settings::setting_vec3 *>(operation.get())->value();
            logging::print("scaling by {}\n", scalar);

            mbsp_t &bsp = std::get<mbsp_t>(bspdata.bsp);

            // adjust entity origins
            {
                auto ents = EntData_Parse(bsp);

                for (auto &ent : ents) {
                    if (ent.has("origin")) {
                        qvec3f origin;
                        ent.get_vector("origin", origin);
                        origin *= scalar;
                        ent.set("origin", fmt::format("{} {} {}", origin[0], origin[1], origin[2]));
                    }

                    if (ent.has("lip")) {
                        float lip = ent.get_float("lip");
                        lip -= 2.0f;
                        lip *= scalar[2];
                        lip += 2.0f;
                        ent.set("lip", fmt::format("{}", lip));
                    }

                    if (ent.has("height")) {
                        // FIXME: check this
                        float height = ent.get_float("height");
                        height *= scalar[2];
                        ent.set("height", fmt::format("{}", height));
                    }
                }

                bsp.dentdata = EntData_Write(ents);
            }

            // adjust vertices
            for (auto &v : bsp.dvertexes) {
                v *= scalar;
            }

            // flip edge lists if we need to
            int32_t flip_faces = !!(scalar[0] < 0) + !!(scalar[1] < 0) + !!(scalar[2] < 0);

            if (flip_faces & 1) {
                for (auto &s : bsp.dfaces) {
                    std::reverse(
                        bsp.dsurfedges.data() + s.firstedge, bsp.dsurfedges.data() + (s.firstedge + s.numedges));
                }
            }

            std::unordered_map<size_t, size_t> plane_remap;
            auto old_planes = bsp.dplanes;

            // rebuild planes
            {
                size_t i = 0;
                planelist_t new_planes;

                for (auto &p : bsp.dplanes) {
                    auto pts = NormalDistanceToThreePoints(p);

                    for (auto &pt : pts) {
                        pt *= scalar;
                    }

                    if (flip_faces) {
                        std::reverse(pts.begin(), pts.end());
                    }

                    dplane_t scaled{qplane3f(pts.plane()), p.type};

                    plane_remap[i] = new_planes.add_or_find_plane(scaled);
                    i++;
                }

                // remap plane list
                bsp.dplanes = std::move(new_planes.planes);
            }

            // adjust node/leaf/model bounds
            for (auto &m : bsp.dmodels) {
                m.origin *= scalar;

                qvec3f scaled_mins = m.mins * scalar;
                qvec3f scaled_maxs = m.maxs * scalar;

                m.mins = qv::min(scaled_mins, scaled_maxs);
                m.maxs = qv::max(scaled_mins, scaled_maxs);
            }

            for (auto &l : bsp.dleafs) {
                qvec3f scaled_mins = l.mins * scalar;
                qvec3f scaled_maxs = l.maxs * scalar;

                l.mins = qv::min(scaled_mins, scaled_maxs);
                l.maxs = qv::max(scaled_mins, scaled_maxs);

                for (auto &v : l.mins) {
                    v = floor(v);
                }
                for (auto &v : l.maxs) {
                    v = ceil(v);
                }
            }

            for (auto &m : bsp.dnodes) {
                qvec3f scaled_mins = m.mins * scalar;
                qvec3f scaled_maxs = m.maxs * scalar;

                m.mins = qv::min(scaled_mins, scaled_maxs);
                m.maxs = qv::max(scaled_mins, scaled_maxs);

                for (auto &v : m.mins) {
                    v = floor(v);
                }
                for (auto &v : m.maxs) {
                    v = ceil(v);
                }

                m.planenum = plane_remap[m.planenum];

                if (m.planenum & 1) {
                    std::reverse(m.children.begin(), m.children.end());
                    m.planenum &= ~1;
                }
            }

            // remap planes on stuff
            for (auto &v : bsp.dbrushsides) {
                v.planenum = plane_remap[v.planenum];
            }

            for (auto &v : bsp.dfaces) {
                v.planenum = plane_remap[v.planenum];
            }

            auto scaleTexInfo = [&](mtexinfo_t &t) {
                // update texinfo

                const qmat3x3d inversescaleM{// column-major...
                    1 / scalar[0], 0.0, 0.0, 0.0, 1 / scalar[1], 0.0, 0.0, 0.0, 1 / scalar[2]};

                auto &texvecs = t.vecs;
                texvecf newtexvecs;

                for (int i = 0; i < 2; i++) {
                    const qvec4f in = texvecs.row(i);
                    const qvec3f in_first3(in);

                    const qvec3f out_first3 = inversescaleM * in_first3;
                    newtexvecs.set_row(i, {out_first3[0], out_first3[1], out_first3[2], in[3]});
                }

                texvecs = newtexvecs;
            };

            // adjust texinfo
            for (auto &t : bsp.texinfo) {
                scaleTexInfo(t);
            }

            // adjust decoupled LM
            if (bspdata.bspx.entries.contains("DECOUPLED_LM")) {

                auto &lump_bytes = bspdata.bspx.entries.at("DECOUPLED_LM");

                auto istream = imemstream(lump_bytes.data(), lump_bytes.size());
                auto ostream = omemstream(lump_bytes.data(), lump_bytes.size());

                istream >> endianness<std::endian::little>;
                ostream << endianness<std::endian::little>;

                bspx_decoupled_lm_perface result;

                for ([[maybe_unused]] auto &face : bsp.dfaces) {
                    istream >= result;

                    const qmat3x3d inversescaleM{// column-major...
                        1 / scalar[0], 0.0, 0.0, 0.0, 1 / scalar[1], 0.0, 0.0, 0.0, 1 / scalar[2]};

                    auto &texvecs = result.world_to_lm_space;
                    texvecf newtexvecs;

                    for (int i = 0; i < 2; i++) {
                        const qvec4f in = texvecs.row(i);
                        const qvec3f in_first3(in);

                        const qvec3f out_first3 = inversescaleM * in_first3;
                        newtexvecs.set_row(i, {out_first3[0], out_first3[1], out_first3[2], in[3]});
                    }

                    texvecs = newtexvecs;

                    ostream <= result;
                }
            }

            // adjust lightgrid
            if (bspdata.bspx.entries.contains("LIGHTGRID_OCTREE")) {

                auto &lump_bytes = bspdata.bspx.entries.at("LIGHTGRID_OCTREE");

                auto istream = imemstream(lump_bytes.data(), lump_bytes.size());
                auto ostream = omemstream(lump_bytes.data(), lump_bytes.size());

                istream >> endianness<std::endian::little>;
                ostream << endianness<std::endian::little>;

                qvec3f original_grid_dist;
                istream >= original_grid_dist;
                ostream <= qvec3f(original_grid_dist * scalar);

                qvec3i grid_size;
                istream >= grid_size;
                ostream.seekp(sizeof(qvec3i), std::ios_base::cur);

                {
                    qvec3f grid_mins;
                    istream >= grid_mins;

                    qvec3f scaled_mins = grid_mins * scalar;
                    qvec3f scaled_maxs = (grid_mins + original_grid_dist * (grid_size - qvec3i{1, 1, 1})) * scalar;

                    ostream <= qv::min(scaled_mins, scaled_maxs);
                }
            }

            ConvertBSPFormat(&bspdata, bspdata.loadversion);

            WriteBSPFile(source.replace_filename(source.stem().string() + "-scaled.bsp"), &bspdata);

        } else if (operation->primary_name() == "replace-entities") {
            fs::path dest = operation->string_value();
            logging::print("updating with {}\n", dest);

            // Load the .ent
            if (std::holds_alternative<mbsp_t>(bspdata.bsp)) {
                fs::data ent = fs::load(dest);

                if (!ent) {
                    Error("couldn't load ent file {}", dest);
                }

                mbsp_t &bsp = std::get<mbsp_t>(bspdata.bsp);

                bsp.dentdata = std::string(reinterpret_cast<char *>(ent->data()), ent->size());

                ConvertBSPFormat(&bspdata, bspdata.loadversion);

                WriteBSPFile(source, &bspdata);
            } else {
                map_file_t ents = LoadMapOrEntFile(dest);

                ents.entities[0].map_brushes = std::move(map_file.entities[0].map_brushes);

                // move brushes over from .map into the .ent
                for (int32_t i1 = 0, b = 1; i1 < map_file.entities.size(); i1++) {

                    // skip worldspawn though
                    if (map_file.entities[i1].map_brushes.empty() || i1 == 0) {
                        continue;
                    }

                    for (int32_t i2 = 0, b2 = 1; i2 < ents.entities.size(); i2++) {
                        if (ents.entities[i2].epairs.get("model").empty() &&
                            ents.entities[i2].epairs.get("classname") != "func_areaportal") {
                            continue;
                        }

                        if (b2 == b) {
                            ents.entities[i2].map_brushes = std::move(map_file.entities[i1].map_brushes);
                            b++;
                            break;
                        }

                        b2++;
                    }

                    if (!map_file.entities[i1].map_brushes.empty()) {
                        Error("ent files' map brushes don't match\n");
                    }
                }

                for (auto &ent : ents.entities) {
                    // remove origin key from brushed entities
                    if (!ent.map_brushes.empty() && ent.epairs.find("origin") != ent.epairs.end()) {
                        ent.epairs.remove("origin");
                    }

                    // remove style keys from areaportals and lights that
                    // have targetnames
                    if (ent.epairs.find("style") != ent.epairs.end()) {
                        if (ent.epairs.get("classname") == "light") {
                            if (ent.epairs.find("targetname") != ent.epairs.end()) {
                                ent.epairs.remove("style");
                            }
                        } else if (ent.epairs.get("classname") == "func_areaportal") {
                            ent.epairs.remove("style");
                        }
                    }
                }

                // write out .replaced.map
                fs::path output = fs::path(source).replace_extension(".replaced.map");
                std::ofstream strm(output, std::ios::binary);

                for (const auto &ent : ents.entities) {
                    strm << "{\n";
                    for (const auto &epair : ent.epairs) {
                        ewt::print(strm, "\"{}\" \"{}\"\n", epair.first, epair.second);
                    }
                    if (!ent.map_brushes.empty()) {
                        strm << ent.map_brushes;
                    }
                    strm << "}\n";
                }
            }
        } else if (operation->primary_name() == "convert") {
            std::string format = operation->string_value();
            const bspversion_t *fmt = nullptr;

            for (const bspversion_t *bspver : bspversions) {
                if (string_iequals(format, bspver->short_name)) {
                    fmt = bspver;
                    break;
                }
            }

            if (!fmt) {
                Error("Unsupported format {}", format);
            }

            ConvertBSPFormat(&bspdata, fmt);

            WriteBSPFile(source.replace_filename(source.stem().string() + "-" + fmt->short_name), &bspdata);
        } else if (operation->primary_name() == "extract-entities") {
            mbsp_t &bsp = std::get<mbsp_t>(bspdata.bsp);

            uint32_t crc = CRC_Block((unsigned char *)bsp.dentdata.data(), bsp.dentdata.size() - 1);

            source.replace_extension(".ent");
            logging::print("-> writing {} [CRC: {:04x}]... ", source, crc);

            std::ofstream f(source, std::ios_base::out | std::ios_base::binary);
            if (!f)
                Error("couldn't open {} for writing\n", source);

            f << bsp.dentdata;

            if (!f)
                Error("{}", strerror(errno));

            f.close();
        } else if (operation->primary_name() == "extract-textures") {
            mbsp_t &bsp = std::get<mbsp_t>(bspdata.bsp);

            source.replace_extension(".wad");
            logging::print("-> writing {}... ", source);

            std::ofstream f(source, std::ios_base::binary);

            if (!f)
                Error("couldn't open {} for writing\n", source);

            ExportWad(f, &bsp);
        } else if (operation->primary_name() == "replace-textures") {
            fs::path wad_source = operation->string_value();

            if (auto wad = fs::addArchive(wad_source, false)) {
                logging::print("loaded wad file: {}\n", wad_source);

                mbsp_t &bsp = std::get<mbsp_t>(bspdata.bsp);
                ReplaceTexturesFromWad(bsp);
                ConvertBSPFormat(&bspdata, bspdata.loadversion);
                WriteBSPFile(source, &bspdata);
            } else {
                Error("couldn't load .wad file {}\n", wad_source);
            }
        } else if (operation->primary_name() == "check") {
            mbsp_t &bsp = std::get<mbsp_t>(bspdata.bsp);
            CheckBSPFile(&bsp);
            CheckBSPFacesPlanar(&bsp);
        } else if (operation->primary_name() == "modelinfo") {
            mbsp_t &bsp = std::get<mbsp_t>(bspdata.bsp);
            PrintModelInfo(&bsp);
        } else if (operation->primary_name() == "findfaces") {
            auto setting = dynamic_cast<setting_combined *>(operation.get());
            mbsp_t &bsp = std::get<mbsp_t>(bspdata.bsp);

            try {
                const qvec3d pos = setting->get<settings::setting_vec3>(0)->value();
                const qvec3d normal = setting->get<settings::setting_vec3>(1)->value();
                FindFaces(&bsp, pos, normal);
            } catch (const std::exception &) {
                Error("Error reading position/normal\n");
            }
        } else if (operation->primary_name() == "findleaf") {
            qvec3f pos = dynamic_cast<settings::setting_vec3 *>(operation.get())->value();
            mbsp_t &bsp = std::get<mbsp_t>(bspdata.bsp);

            try {
                FindLeaf(&bsp, pos);
            } catch (const std::exception &) {
                Error("Error reading position/normal\n");
            }
        } else if (operation->primary_name() == "settexinfo") {
            auto setting = dynamic_cast<setting_combined *>(operation.get());
            mbsp_t &bsp = std::get<mbsp_t>(bspdata.bsp);

            const int fnum = setting->get<settings::setting_int32>(0)->value();
            const int texinfonum = setting->get<settings::setting_int32>(1)->value();

            mface_t *face = BSP_GetFace(&bsp, fnum);
            face->texinfo = texinfonum;

            ConvertBSPFormat(&bspdata, bspdata.loadversion);

            // Overwrite source bsp!
            WriteBSPFile(source, &bspdata);
        } else if (operation->primary_name().starts_with("decompile")) {
            const bool geomOnly = operation->primary_name() == "decompile-geomonly";
            const bool ignoreBrushes = operation->primary_name() == "decompile-ignore-brushes";
            const bool hull = operation->primary_name() == "decompile-hull";

            int hullnum = 0;
            if (hull) {
                hullnum = dynamic_cast<settings::setting_int32 *>(operation.get())->value();
            }

            // generate output filename
            if (hull) {
                source.replace_extension(fmt::format(".decompile.hull{}.map", hullnum));
            } else {
                source.replace_extension(".decompile.map");
            }

            logging::print("-> writing {}...\n", source);

            std::ofstream f(source);

            if (!f)
                Error("couldn't open {} for writing\n", source);

            mbsp_t &bsp = std::get<mbsp_t>(bspdata.bsp);

            decomp_options options;
            options.geometryOnly = geomOnly;
            options.ignoreBrushes = ignoreBrushes;
            options.hullnum = hullnum;

            DecompileBSP(&bsp, options, f);

            f.close();

            if (!f)
                Error("{}", strerror(errno));
        } else if (operation->primary_name() == "extract-bspx-lump") {
            auto setting = dynamic_cast<setting_combined *>(operation.get());
            std::string lump_name = setting->get<settings::setting_string>(0)->value();
            fs::path output_file_name = setting->get<settings::setting_string>(1)->value();

            const auto &entries = bspdata.bspx.entries;
            if (entries.find(lump_name) == entries.end()) {
                FError("couldn't find bspx lump {}", lump_name);
            }

            const std::vector<uint8_t> &entry = entries.at(lump_name);

            logging::print("-> writing {} BSPX lump data to {}... ", lump_name, output_file_name);
            std::ofstream f(output_file_name, std::ios_base::out | std::ios_base::binary);
            if (!f)
                FError("couldn't open {} for writing\n", output_file_name);

            f.write(reinterpret_cast<const char *>(entry.data()), entry.size());

            if (!f)
                FError("{}", strerror(errno));
            f.close();

            logging::print("done.\n");
        } else if (operation->primary_name() == "insert-bspx-lump") {
            auto setting = dynamic_cast<setting_combined *>(operation.get());
            std::string lump_name = setting->get<settings::setting_string>(0)->value();
            fs::path input_file_name = setting->get<settings::setting_string>(1)->value();

            // read entire input
            auto data = fs::load(input_file_name);
            if (!data)
                FError("couldn't open {} for reading\n", input_file_name);

            // put bspx lump
            logging::print("-> inserting BSPX lump {} from {} ({} bytes)...", lump_name, input_file_name, data->size());
            auto &entries = bspdata.bspx.entries;
            entries[lump_name] = std::move(*data);

            // Overwrite source bsp!
            ConvertBSPFormat(&bspdata, bspdata.loadversion);
            WriteBSPFile(source, &bspdata);

            logging::print("done.\n");
        } else if (operation->primary_name() == "remove-bspx-lump") {
            std::string lump_name = operation->string_value();

            // remove bspx lump
            logging::print("-> removing bspx lump {}\n", lump_name);

            auto &entries = bspdata.bspx.entries;
            auto it = entries.find(lump_name);
            if (it == entries.end()) {
                FError("couldn't find bspx lump {}", lump_name);
            }
            entries.erase(it);

            // Overwrite source bsp!
            ConvertBSPFormat(&bspdata, bspdata.loadversion);
            WriteBSPFile(source, &bspdata);

            logging::print("done.\n");
        } else {
            Error("option not implemented: {}", operation->primary_name());
        }
    }

    return 0;
}
