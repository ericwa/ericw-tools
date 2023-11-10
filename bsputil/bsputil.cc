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

// TODO
settings::common_settings bsputil_options;

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
        if (auto [wadtex_opt, _0, mipdata] = img::load_texture(tex.name, false, bsp.loadversion->game, bsputil_options); wadtex_opt) {
            const img::texture &wadtex = *wadtex_opt;

            if  (tex.width != wadtex.width || tex.height != wadtex.height) {
                logging::print("    size {}x{} in bsp does not match replacement texture {}x{}\n",
                    tex.width, tex.height, wadtex.width, wadtex.height);
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
    for (size_t i = 0; i < bsp->dmodels.size(); i++) {
        const dmodelh2_t *dmodel = &bsp->dmodels[i];
        logging::print("model {:3}: {:5} faces (firstface = {})\n", i, dmodel->numfaces, dmodel->firstface);
    }
}

/*
 * Quick hack to check verticies of faces lie on the correct plane
 */
constexpr vec_t PLANE_ON_EPSILON = 0.01;

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
                fmt::print("WARNING: face {}, point {} off plane by {}\n", i, j, dist);
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
            fmt::print("\nNode heights at level {}: ", level);
        }

        // print the level of this node
        fmt::print("{}, ", cache.at(node));

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
            fmt::print("warning: face {} has negative texinfo ({})\n", i, face->texinfo);
        if (face->texinfo >= bsp->texinfo.size())
            fmt::print("warning: face {} has texinfo out of range ({} >= {})\n", i, face->texinfo, bsp->texinfo.size());
        referenced_texinfos.insert(face->texinfo);

        /* planenum bounds check */
        if (face->planenum < 0)
            fmt::print("warning: face {} has negative planenum ({})\n", i, face->planenum);
        if (face->planenum >= bsp->dplanes.size())
            fmt::print(
                "warning: face {} has planenum out of range ({} >= {})\n", i, face->planenum, bsp->dplanes.size());
        referenced_planenums.insert(face->planenum);

        /* lightofs check */
        if (face->lightofs < -1)
            fmt::print("warning: face {} has negative light offset ({})\n", i, face->lightofs);
        if (face->lightofs >= bsp->dlightdata.size())
            fmt::print("warning: face {} has light offset out of range "
                       "({} >= {})\n",
                i, face->lightofs, bsp->dlightdata.size());

        /* edge check */
        if (face->firstedge < 0)
            fmt::print("warning: face {} has negative firstedge ({})\n", i, face->firstedge);
        if (face->numedges < 3)
            fmt::print("warning: face {} has < 3 edges ({})\n", i, face->numedges);
        if (face->firstedge + face->numedges > bsp->dsurfedges.size())
            fmt::print("warning: face {} has edges out of range ({}..{} >= {})\n", i, face->firstedge,
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
                fmt::print("warning: edge {} has vertex {} out range "
                           "({} >= {})\n",
                    i, j, vertex, bsp->dvertexes.size());
            referenced_vertexes.insert(vertex);
        }
    }

    /* surfedges */
    for (i = 0; i < bsp->dsurfedges.size(); i++) {
        const int edgenum = bsp->dsurfedges[i];
        if (!edgenum)
            fmt::print("warning: surfedge {} has zero value!\n", i);
        if (std::abs(edgenum) >= bsp->dedges.size())
            fmt::print("warning: surfedge {} is out of range (abs({}) >= {})\n", i, edgenum, bsp->dedges.size());
    }

    /* marksurfaces */
    for (i = 0; i < bsp->dleaffaces.size(); i++) {
        const uint32_t surfnum = bsp->dleaffaces[i];
        if (surfnum >= bsp->dfaces.size())
            fmt::print("warning: marksurface {} is out of range ({} >= {})\n", i, surfnum, bsp->dfaces.size());
    }

    /* leafs */
    for (i = 0; i < bsp->dleafs.size(); i++) {
        const mleaf_t *leaf = &bsp->dleafs[i];
        const uint32_t endmarksurface = leaf->firstmarksurface + leaf->nummarksurfaces;
        if (endmarksurface > bsp->dleaffaces.size())
            fmt::print("warning: leaf {} has marksurfaces out of range "
                       "({}..{} >= {})\n",
                i, leaf->firstmarksurface, endmarksurface - 1, bsp->dleaffaces.size());
        if (leaf->visofs < -1)
            fmt::print("warning: leaf {} has negative visdata offset ({})\n", i, leaf->visofs);
        if (leaf->visofs >= bsp->dvis.bits.size())
            fmt::print("warning: leaf {} has visdata offset out of range "
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
                fmt::print("warning: node {} has child {} (node) out of range "
                           "({} >= {})\n",
                    i, j, child, bsp->dnodes.size());
            if (child < 0 && -child - 1 >= bsp->dleafs.size())
                fmt::print("warning: node {} has child {} (leaf) out of range "
                           "({} >= {})\n",
                    i, j, -child - 1, bsp->dleafs.size());
        }

        if (node->children[0] == node->children[1]) {
            fmt::print("warning: node {} has both children {}\n", i, node->children[0]);
        }

        referenced_planenums.insert(node->planenum);
    }

    /* clipnodes */
    for (i = 0; i < bsp->dclipnodes.size(); i++) {
        const bsp2_dclipnode_t *clipnode = &bsp->dclipnodes[i];

        for (int j = 0; j < 2; j++) {
            const int32_t child = clipnode->children[j];
            if (child >= 0 && child >= bsp->dclipnodes.size())
                fmt::print("warning: clipnode {} has child {} (clipnode) out of range "
                           "({} >= {})\n",
                    i, j, child, bsp->dclipnodes.size());
            if (child < 0 && child < CONTENTS_MIN)
                fmt::print("warning: clipnode {} has invalid contents ({}) for child {}\n", i, child, j);
        }

        if (clipnode->children[0] == clipnode->children[1]) {
            fmt::print("warning: clipnode {} has both children {}\n", i, clipnode->children[0]);
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
            fmt::print("warning: {} texinfos are unreferenced\n", num_unreferenced_texinfo);
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
            fmt::print("warning: {} planes are unreferenced\n", num_unreferenced_planes);
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
            fmt::print("warning: {} vertexes are unreferenced\n", num_unreferenced_vertexes);
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
    fmt::print("{} unique visdata offsets for {} leafs\n", visofs_set.size(), bsp->dleafs.size());
    fmt::print("{} visleafs in world model\n", bsp->dmodels[0].visleafs);

    /* unique lightstyles */
    fmt::print("{} lightstyles used:\n", used_lightstyles.size());
    {
        std::vector<uint8_t> v(used_lightstyles.begin(), used_lightstyles.end());
        std::sort(v.begin(), v.end());
        for (uint8_t style : v) {
            fmt::print("\t{}\n", style);
        }
    }

    fmt::print("world mins: {} maxs: {}\n", bsp->dmodels[0].mins, bsp->dmodels[0].maxs);
}

static void CompareBSPFiles(const mbsp_t &refBsp, const mbsp_t &bsp)
{
    fmt::print("comparing {} with {} faces\n", refBsp.dfaces.size(), bsp.dfaces.size());

    const dmodelh2_t *world = BSP_GetWorldModel(&bsp);
    const dmodelh2_t *refWorld = BSP_GetWorldModel(&refBsp);

    // iterate through the refBsp world faces
    for (int i = 0; i < refWorld->numfaces; i++) {
        auto *refFace = BSP_GetFace(&refBsp, refWorld->firstface + i);
        qvec3f refFaceCentroid = Face_Centroid(&refBsp, refFace);
        qvec3d wantedNormal = Face_Normal(&refBsp, refFace);

        // Search for a face in bsp touching refFaceCentroid.
        auto *matchedFace = BSP_FindFaceAtPoint(&bsp, world, refFaceCentroid, wantedNormal);
        if (matchedFace == nullptr) {
            fmt::print("couldn't find a face at {} normal {}\n", refFaceCentroid, wantedNormal);
        }

        // TODO: run on some more complex maps
        //        auto* refFaceSelfCheck = BSP_FindFaceAtPoint(refBsp, refWorld, wantedPoint, wantedNormal);
        //        if (refFaceSelfCheck == refFace) {
        //            matches ++;
        //        } else {
        //            fmt::print("not match at {} {} {} wanted {} got {}\n", wantedPoint[0], wantedPoint[1],
        //            wantedPoint[2], refFace, refFaceSelfCheck); Face_DebugPrint(refBsp, refFace);
        //            Face_DebugPrint(refBsp, refFaceSelfCheck); notmat++;
        //        }
    }
}

static void FindFaces(const mbsp_t *bsp, const qvec3d &pos, const qvec3d &normal)
{
    for (int i = 0; i < bsp->dmodels.size(); ++i) {
        const dmodelh2_t *model = &bsp->dmodels[i];
        const mface_t *face = BSP_FindFaceAtPoint(bsp, model, pos, normal);

        if (face != nullptr) {
            fmt::print("model {} face {}: texture '{}' texinfo {}\n", i, Face_GetNum(bsp, face),
                Face_TextureName(bsp, face), face->texinfo);
        }
    }
}

static void FindLeaf(const mbsp_t *bsp, const qvec3d &pos)
{
    const mleaf_t *leaf = BSP_FindLeafAtPoint(bsp, &bsp->dmodels[0], pos);

    fmt::print("leaf {}: contents {} ({})\n", (leaf - bsp->dleafs.data()), leaf->contents,
        contentflags_t{leaf->contents}.to_string(bsp->loadversion->game));
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
    pareto::spatial_map<vec_t, 4, size_t> plane_hash;
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
            pareto::point<vec_t, 4>{positive.normal[0], positive.normal[1], positive.normal[2], positive.dist},
            positive_index);
        plane_hash.emplace(
            pareto::point<vec_t, 4>{negative.normal[0], negative.normal[1], negative.normal[2], negative.dist},
            negative_index);

        return result;
    }

    std::optional<size_t> find_plane_nonfatal(const dplane_t &plane)
    {
        constexpr vec_t HALF_NORMAL_EPSILON = NORMAL_EPSILON * 0.5;
        constexpr vec_t HALF_DIST_EPSILON = DIST_EPSILON * 0.5;

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

int bsputil_main(int argc, char **argv)
{
    logging::preinitialize();

    bspdata_t bspdata;

    fmt::print("---- bsputil / ericw-tools {} ----\n", ERICWTOOLS_VERSION);
    if (argc == 1) {
        printf(
            "usage: bsputil [--replace-entities] [--extract-entities] [--extract-textures] [--replace-textures f]\n"
            "[--convert bsp29|bsp2|bsp2rmq|q2bsp] [--check] [--modelinfo]\n"
            "[--check] [--compare otherbsp] [--findfaces x y z nx ny nz] [--findleaf x y z] [--settexinfo facenum texinfonum]\n"
            "[--decompile] [--decompile-geomonly] [--decompile-hull n]\n"
            "[--extract-bspx-lump lump_name output_file_name]\n"
            "[--insert-bspx-lump lump_name input_file_name]\n"
            "[--remove-bspx-lump lump_name] bspfile/mapfile\n");
        exit(1);
    }

    fs::path source = argv[argc - 1];

    if (!fs::exists(source)) {
        source = DefaultExtension(argv[argc - 1], "bsp");
    }

    printf("---------------------\n");
    fmt::print("{}\n", source);

    map_file_t map_file;

    if (string_iequals(source.extension().string(), ".bsp")) {
        LoadBSPFile(source, &bspdata);

        bspdata.version->game->init_filesystem(source, bsputil_options);

        ConvertBSPFormat(&bspdata, &bspver_generic);
    } else {
        map_file = LoadMapOrEntFile(source);
    }

    for (int32_t i = 1; i < argc - 1; i++) {
        if (!strcmp(argv[i], "--scale")) {

            i++;
            if (i == argc - 1) {
                Error("--scale requires three arguments; x y z");
            }

            qvec3d scalar{atof(argv[i]), atof(argv[i + 1]), atof(argv[i + 2])};

            i += 2;

            fmt::print("scaling {} by {}\n", source, scalar);

            mbsp_t &bsp = std::get<mbsp_t>(bspdata.bsp);

            // adjust entity origins
            {
                auto ents = EntData_Parse(bsp);

                for (auto &ent : ents) {
                    if (ent.has("origin")) {
                        qvec3d origin;
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

                    auto scaled = dplane_t{pts.plane(), p.type};

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

        } else if (!strcmp(argv[i], "--replace-entities")) {
            i++;
            if (i == argc - 1) {
                Error("--replace-entities requires two arguments");
            }

            fmt::print("updating {} with {}\n", source, argv[i]);

            // Load the .ent
            if (std::holds_alternative<mbsp_t>(bspdata.bsp)) {
                fs::data ent = fs::load(argv[i]);

                if (!ent) {
                    Error("couldn't load ent file {}", argv[i]);
                }

                mbsp_t &bsp = std::get<mbsp_t>(bspdata.bsp);

                bsp.dentdata = std::string(reinterpret_cast<char *>(ent->data()), ent->size());

                ConvertBSPFormat(&bspdata, bspdata.loadversion);

                WriteBSPFile(source, &bspdata);
            } else {
                map_file_t ents = LoadMapOrEntFile(argv[i]);

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
        } else if (!strcmp(argv[i], "--compare")) {
            i++;
            if (i == argc - 1) {
                Error("--compare requires two arguments");
            }
            // Load the reference BSP

            fs::path refbspname = DefaultExtension(argv[i], "bsp");

            bspdata_t refbspdata;
            LoadBSPFile(refbspname, &refbspdata);
            ConvertBSPFormat(&refbspdata, &bspver_generic);

            fmt::print("comparing reference bsp {} with test bsp {}\n", refbspname, source);

            mbsp_t &bsp = std::get<mbsp_t>(bspdata.bsp);

            CompareBSPFiles(std::get<mbsp_t>(refbspdata.bsp), bsp);

            break;
        } else if (!strcmp(argv[i], "--convert")) {
            i++;
            if (!(i < argc - 1)) {
                Error("--convert requires an argument");
            }

            const bspversion_t *fmt = nullptr;

            for (const bspversion_t *bspver : bspversions) {
                if (!strcmp(argv[i], bspver->short_name)) {
                    fmt = bspver;
                    break;
                }
            }

            if (!fmt) {
                Error("Unsupported format {}", argv[i]);
            }

            ConvertBSPFormat(&bspdata, fmt);

            WriteBSPFile(source.replace_filename(source.stem().string() + "-" + argv[i]), &bspdata);

        } else if (!strcmp(argv[i], "--extract-entities")) {

            mbsp_t &bsp = std::get<mbsp_t>(bspdata.bsp);

            uint32_t crc = CRC_Block((unsigned char *)bsp.dentdata.data(), bsp.dentdata.size() - 1);

            source.replace_extension(".ent");
            fmt::print("-> writing {} [CRC: {:04x}]... ", source, crc);

            std::ofstream f(source, std::ios_base::out | std::ios_base::binary);
            if (!f)
                Error("couldn't open {} for writing\n", source);

            f << bsp.dentdata;

            if (!f)
                Error("{}", strerror(errno));

            f.close();

            printf("done.\n");
        } else if (!strcmp(argv[i], "--extract-textures")) {

            mbsp_t &bsp = std::get<mbsp_t>(bspdata.bsp);

            source.replace_extension(".wad");
            fmt::print("-> writing {}... ", source);

            std::ofstream f(source, std::ios_base::binary);

            if (!f)
                Error("couldn't open {} for writing\n", source);

            ExportWad(f, &bsp);

            printf("done.\n");
        } else if (!strcmp(argv[i], "--replace-textures")) {
            if (i + 1 >= argc) {
                Error("--replace-textures requires 1 argument");
            }

            fs::path wad_source = argv[i + 1];

            if (auto wad = fs::addArchive(wad_source, false)) {
                logging::print("loaded wad file: {}\n", wad_source);

                mbsp_t &bsp = std::get<mbsp_t>(bspdata.bsp);
                ReplaceTexturesFromWad(bsp);
                ConvertBSPFormat(&bspdata, bspdata.loadversion);
                WriteBSPFile(source, &bspdata);
            } else {
                Error("couldn't load .wad file {}\n", wad_source);
            }

            printf("done.\n");
        } else if (!strcmp(argv[i], "--check")) {
            printf("Beginning BSP data check...\n");
            mbsp_t &bsp = std::get<mbsp_t>(bspdata.bsp);
            CheckBSPFile(&bsp);
            CheckBSPFacesPlanar(&bsp);
            printf("Done.\n");
        } else if (!strcmp(argv[i], "--modelinfo")) {
            mbsp_t &bsp = std::get<mbsp_t>(bspdata.bsp);
            PrintModelInfo(&bsp);
        } else if (!strcmp(argv[i], "--findfaces")) {
            // (i + 1) ... (i + 6) = x y z nx ny nz
            // i + 7 = bsp file

            if (i + 7 >= argc) {
                Error("--findfaces requires 6 arguments");
            }

            mbsp_t &bsp = std::get<mbsp_t>(bspdata.bsp);

            try {
                const qvec3d pos{std::stof(argv[i + 1]), std::stof(argv[i + 2]), std::stof(argv[i + 3])};
                const qvec3d normal{std::stof(argv[i + 4]), std::stof(argv[i + 5]), std::stof(argv[i + 6])};
                FindFaces(&bsp, pos, normal);
            } catch (const std::exception &) {
                Error("Error reading position/normal\n");
            }
            return 0;
        } else if (!strcmp(argv[i], "--findleaf")) {
            // (i + 1) ... (i + 3) = x y z
            // i + 4 = bsp file

            if (i + 4 >= argc) {
                Error("--findleaf requires 3 arguments");
            }

            mbsp_t &bsp = std::get<mbsp_t>(bspdata.bsp);

            try {
                const qvec3d pos{std::stof(argv[i + 1]), std::stof(argv[i + 2]), std::stof(argv[i + 3])};
                FindLeaf(&bsp, pos);
            } catch (const std::exception &) {
                Error("Error reading position/normal\n");
            }
            return 0;
        } else if (!strcmp(argv[i], "--settexinfo")) {
            // (i + 1) facenum
            // (i + 2) texinfonum

            if (i + 2 >= argc) {
                Error("--settexinfo requires 2 arguments");
            }

            mbsp_t &bsp = std::get<mbsp_t>(bspdata.bsp);

            const int fnum = std::stoi(argv[i + 1]);
            const int texinfonum = std::stoi(argv[i + 2]);

            mface_t *face = BSP_GetFace(&bsp, fnum);
            face->texinfo = texinfonum;

            ConvertBSPFormat(&bspdata, bspdata.loadversion);

            // Overwrite source bsp!
            WriteBSPFile(source, &bspdata);

            return 0;
        } else if (!strcmp(argv[i], "--decompile") || !strcmp(argv[i], "--decompile-geomonly") ||
                   !strcmp(argv[i], "--decompile-ignore-brushes") || !strcmp(argv[i], "--decompile-hull")) {
            const bool geomOnly = !strcmp(argv[i], "--decompile-geomonly");
            const bool ignoreBrushes = !strcmp(argv[i], "--decompile-ignore-brushes");
            const bool hull = !strcmp(argv[i], "--decompile-hull");

            int hullnum = 0;
            if (hull) {
                hullnum = std::stoi(argv[i + 1]);
            }

            // generate output filename
            if (hull) {
                source.replace_extension(fmt::format(".decompile.hull{}.map", hullnum));
            } else {
                source.replace_extension(".decompile.map");
            }

            fmt::print("-> writing {}...\n", source);

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

            printf("done!\n");
            return 0;
        } else if (!strcmp(argv[i], "--extract-bspx-lump")) {
            std::string lump_name = argv[i + 1];
            fs::path output_file_name = argv[i + 2];
            // argv[i + 3] == input bsp

            if (i + 3 >= argc) {
                Error("--extract-bspx-lump requires 3 arguments");
            }

            const auto &entries = bspdata.bspx.entries;
            if (entries.find(lump_name) == entries.end()) {
                FError("couldn't find bspx lump {}", lump_name);
            }

            const std::vector<uint8_t> &entry = entries.at(lump_name);

            fmt::print("-> writing {} BSPX lump data to {}... ", lump_name, output_file_name);
            std::ofstream f(output_file_name, std::ios_base::out | std::ios_base::binary);
            if (!f)
                FError("couldn't open {} for writing\n", output_file_name);

            f.write(reinterpret_cast<const char *>(entry.data()), entry.size());

            if (!f)
                FError("{}", strerror(errno));
            f.close();

            fmt::print("done.\n");
            return 0;
        } else if (!strcmp(argv[i], "--insert-bspx-lump")) {
            std::string lump_name = argv[i + 1];
            fs::path input_file_name = argv[i + 2];
            // argv[i + 3] == input bsp

            if (i + 3 >= argc) {
                Error("--insert-bspx-lump requires 3 arguments");
            }

            // read entire input
            auto data = fs::load(input_file_name);
            if (!data)
                FError("couldn't open {} for reading\n", input_file_name);

            // put bspx lump
            fmt::print("-> inserting BSPX lump {} from {} ({} bytes)...", lump_name, input_file_name, data->size());
            auto &entries = bspdata.bspx.entries;
            entries[lump_name] = std::move(*data);

            // Overwrite source bsp!
            ConvertBSPFormat(&bspdata, bspdata.loadversion);
            WriteBSPFile(source, &bspdata);

            fmt::print("done.\n");
            return 0;
        } else if (!strcmp(argv[i], "--remove-bspx-lump")) {
            std::string lump_name = argv[i + 1];
            // argv[i + 2] == input bsp

            if (i + 2 >= argc) {
                Error("--remove-bspx-lump requires 2 arguments");
            }

            // remove bspx lump
            fmt::print("-> removing bspx lump {}\n", lump_name);

            auto &entries = bspdata.bspx.entries;
            auto it = entries.find(lump_name);
            if (it == entries.end()) {
                FError("couldn't find bspx lump {}", lump_name);
            }
            entries.erase(it);

            // Overwrite source bsp!
            ConvertBSPFormat(&bspdata, bspdata.loadversion);
            WriteBSPFile(source, &bspdata);

            fmt::print("done.\n");
            return 0;
        } else {
            fmt::print("unknown command {}\n", argv[i]);
        }
    }

    printf("---------------------\n");

    return 0;
}
