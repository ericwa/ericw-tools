/*
    Copyright (C) 1996-1997  Id Software, Inc.
    Copyright (C) 1997       Greg Lewis

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
// writebsp.c

#include <qbsp/map.hh>

#include <common/log.hh>
#include <qbsp/qbsp.hh>

#include <vector>
#include <algorithm>
#include <cstdint>
#include <common/json.hh>
#include <fstream>

#include <stdexcept>
using nlohmann::json;

/**
 * Returns the output plane number
 */
size_t ExportMapPlane(size_t planenum)
{
    mapplane_t &plane = map.planes[planenum];

    if (plane.outputnum.has_value()) {
        return plane.outputnum.value(); // already output.
    }

    plane.outputnum = map.bsp.dplanes.size();
    dplane_t &dplane = map.bsp.dplanes.emplace_back();
    dplane.normal = plane.get_normal();
    dplane.dist = plane.get_dist();
    dplane.type = static_cast<int32_t>(plane.get_type());
    return plane.outputnum.value();
}

size_t ExportMapTexinfo(size_t texinfonum)
{
    maptexinfo_t &src = map.mtexinfos.at(texinfonum);

    if (src.outputnum.has_value())
        return src.outputnum.value();
    else if (!qbsp_options.includeskip.value() && src.flags.is_nodraw) {
        // TODO: move to game specific
        // always include LIGHT
        if (qbsp_options.target_game->id != GAME_QUAKE_II || !(src.flags.native & Q2_SURF_LIGHT))
            return -1;
    }

    // this will be the index of the exported texinfo in the BSP lump
    const size_t i = map.bsp.texinfo.size();

    mtexinfo_t &dest = map.bsp.texinfo.emplace_back();

    // make sure we don't write any non-native flags.
    // e.g. Quake only accepts 0 or TEX_SPECIAL.
    if (!src.flags.is_valid(qbsp_options.target_game)) {
        FError("Internal error: Texinfo {} has invalid surface flags {}", texinfonum, src.flags.native);
    }

    dest.flags = src.flags;
    dest.miptex = src.miptex;
    dest.vecs = src.vecs;

    const std::string &src_name = map.texinfoTextureName(texinfonum);
    if (src_name.size() > (dest.texture.size() - 1)) {
        logging::print("WARNING: texture name '{}' exceeds maximum length {} and will be truncated\n",
            src_name, dest.texture.size() - 1);
    }
    for (size_t i = 0; i < (dest.texture.size() - 1); ++i) {
        if (i < src_name.size())
            dest.texture[i] = src_name[i];
        else
            dest.texture[i] = '\0';
    }
    dest.texture[dest.texture.size() - 1] = '\0';
    dest.value = map.miptex[src.miptex].value;

    src.outputnum = i;

    if (src.next.has_value()) {
        map.bsp.texinfo[i].nexttexinfo = ExportMapTexinfo(src.next.value());
    }

    return i;
}

//===========================================================================

/*
==================
ExportClipNodes
==================
*/
static size_t ExportClipNodes(node_t *node)
{
    if (node->is_leaf) {
        return node->contents.native;
    }

    /* emit a clipnode */
    const size_t nodenum = map.bsp.dclipnodes.size();
    map.bsp.dclipnodes.emplace_back();

    const int child0 = ExportClipNodes(node->children[0]);
    const int child1 = ExportClipNodes(node->children[1]);

    // Careful not to modify the vector while using this clipnode pointer
    bsp2_dclipnode_t &clipnode = map.bsp.dclipnodes[nodenum];
    clipnode.planenum = ExportMapPlane(node->planenum);
    clipnode.children[0] = child0;
    clipnode.children[1] = child1;

    return nodenum;
}

/*
==================
ExportClipNodes

Called after the clipping hull is completed.  Generates a disk format
representation.

This gets real ugly.  Gets called twice per entity, once for each clip hull.
First time just store away data, second time fix up reference points to
accomodate new data interleaved with old.
==================
*/
void ExportClipNodes(mapentity_t &entity, node_t *nodes, hull_index_t::value_type hullnum)
{
    auto &model = map.bsp.dmodels.at(entity.outputmodelnumber.value());
    model.headnode[hullnum] = ExportClipNodes(nodes);
}

//===========================================================================

/*
==================
ExportLeaf
==================
*/
static void ExportLeaf(node_t *node)
{
    mleaf_t &dleaf = map.bsp.dleafs.emplace_back();

    const contentflags_t remapped =
        qbsp_options.target_game->contents_remap_for_export(node->contents, gamedef_t::remap_type_t::leaf);

    if (!remapped.is_valid(qbsp_options.target_game, false)) {
        FError("Internal error: On leaf {}, tried to save invalid contents type {}", map.bsp.dleafs.size() - 1,
            remapped.to_string(qbsp_options.target_game));
    }

    dleaf.contents = remapped.native;

    if (node->bounds.maxs()[0] < node->bounds.mins()[0]) {
        throw std::runtime_error("leaf bounds was unassigned");
    }

    /*
     * write bounding box info
     */
    dleaf.mins = qv::floor(node->bounds.mins());
    dleaf.maxs = qv::ceil(node->bounds.maxs());

    dleaf.visofs = -1; // no vis info yet

    // write the marksurfaces
    dleaf.firstmarksurface = static_cast<int>(map.bsp.dleaffaces.size());

    for (auto &face : node->markfaces) {
        if (!qbsp_options.includeskip.value() && face->get_texinfo().flags.is_nodraw) {

            // TODO: move to game specific
            // always include LIGHT
            if (qbsp_options.target_game->id != GAME_QUAKE_II || !(face->get_texinfo().flags.native & Q2_SURF_LIGHT))
                continue;
        }

        /* grab final output faces */
        for (auto &fragment : face->fragments) {
            if (fragment.outputnumber.has_value()) {
                map.bsp.dleaffaces.push_back(fragment.outputnumber.value());
            }
        }
    }
    dleaf.nummarksurfaces = static_cast<int>(map.bsp.dleaffaces.size()) - dleaf.firstmarksurface;

    if (dleaf.contents & Q2_CONTENTS_SOLID) {
        dleaf.area = AREA_INVALID;
    } else {
        if (map.leakfile || map.region || map.antiregions.size()) {
            dleaf.area = 1;
        } else {
            dleaf.area = node->area;
        }
    }

    dleaf.cluster = node->viscluster;
    dleaf.firstleafbrush = node->firstleafbrush;
    dleaf.numleafbrushes = node->numleafbrushes;
}

// only used for Q1
constexpr int32_t PLANENUM_LEAF = -1;

/*
==================
ExportDrawNodes
==================
*/
static void ExportDrawNodes(node_t *node)
{
    const size_t ourNodeIndex = map.bsp.dnodes.size();
    bsp2_dnode_t *dnode = &map.bsp.dnodes.emplace_back();

    dnode->mins = qv::floor(node->bounds.mins());
    dnode->maxs = qv::ceil(node->bounds.maxs());

    dnode->planenum = ExportMapPlane(node->planenum);
    dnode->firstface = node->firstface;
    dnode->numfaces = node->numfaces;

    // recursively output the other nodes
    for (size_t i = 0; i < 2; i++) {
        if (node->children[i]->is_leaf) {
            // In Q2, all leaves must have their own ID even if they share solidity.
            if (qbsp_options.target_game->id != GAME_QUAKE_II &&
                node->children[i]->contents.is_any_solid(qbsp_options.target_game)) {
                dnode->children[i] = PLANENUM_LEAF;
            } else {
                int32_t nextLeafIndex = static_cast<int32_t>(map.bsp.dleafs.size());
                const int32_t childnum = -(nextLeafIndex + 1);
                dnode->children[i] = childnum;
                ExportLeaf(node->children[i]);
            }
        } else {
            const int32_t childnum = static_cast<int32_t>(map.bsp.dnodes.size());
            dnode->children[i] = childnum;
            ExportDrawNodes(node->children[i]);

            // Important: our dnode pointer may be invalid after the recursive call, if the vector got resized.
            // So re-set the pointer.
            dnode = &map.bsp.dnodes[ourNodeIndex];
        }
    }

    // DarkPlaces asserts that the leaf numbers are different
    // if mod_bsp_portalize is 1 (default)
    // The most likely way it could fail is if both sides are the
    // shared CONTENTS_SOLID leaf (-1)
    Q_assert(!(dnode->children[0] == -1 && dnode->children[1] == -1));
    Q_assert(dnode->children[0] != dnode->children[1]);
}

/*
==================
ExportDrawNodes
==================
*/
void ExportDrawNodes(mapentity_t &entity, node_t *headnode, int firstface)
{
    // populate model struct (which was emitted previously)
    dmodelh2_t &dmodel = map.bsp.dmodels.at(entity.outputmodelnumber.value());
    dmodel.headnode[0] = static_cast<int32_t>(map.bsp.dnodes.size());
    dmodel.firstface = firstface;
    dmodel.numfaces = static_cast<int32_t>(map.bsp.dfaces.size()) - firstface;

    const size_t mapleafsAtStart = map.bsp.dleafs.size();

    if (headnode->is_leaf) {
        ExportLeaf(headnode);
    } else {
        ExportDrawNodes(headnode);
    }

    // count how many leafs were exported by the above calls
    dmodel.visleafs = static_cast<int32_t>(map.bsp.dleafs.size() - mapleafsAtStart);

    /* remove the headnode padding */
    for (size_t i = 0; i < 3; i++) {
        dmodel.mins[i] = headnode->bounds.mins()[i] + SIDESPACE;
        dmodel.maxs[i] = headnode->bounds.maxs()[i] - SIDESPACE;
    }

    // shrink the bounds in Q1 based games (Q1 engine compensates for this in Mod_LoadSubmodels)
    if (qbsp_options.target_game->id != GAME_QUAKE_II) {
        dmodel.mins += qvec3d(1, 1, 1);
        dmodel.maxs -= qvec3d(1, 1, 1);
    }
}

//=============================================================================

/*
==================
BeginBSPFile
==================
*/
void BeginBSPFile(void)
{
    // First edge must remain unused because 0 can't be negated
    map.bsp.dedges.emplace_back();
    Q_assert(map.bsp.dedges.size() == 1);

    // Leave room for leaf 0 (must be solid)
    auto &solid_leaf = map.bsp.dleafs.emplace_back();
    solid_leaf.contents = qbsp_options.target_game->create_solid_contents().native;
    solid_leaf.cluster = CLUSTER_INVALID;
    Q_assert(map.bsp.dleafs.size() == 1);
}

/*
 * Writes extended texinfo flags to a file so they can be read by the light tool.
 * Used for phong shading and other lighting settings on func_detail.
 */
static void WriteExtendedTexinfoFlags(void)
{
    auto file = fs::path(qbsp_options.bsp_path).replace_extension("texinfo.json");
    bool needwrite = false;

    if (fs::exists(file)) {
        fs::remove(file);
    }

    for (auto &texinfo : map.mtexinfos) {
        if (texinfo.flags.needs_write()) {
            // this texinfo uses some extended flags, write them to a file
            needwrite = true;
            break;
        }
    }

    if (!needwrite || qbsp_options.noextendedsurfflags.value())
        return;

    // sort by output texinfo number
    std::vector<maptexinfo_t> texinfos_sorted(map.mtexinfos);
    std::sort(texinfos_sorted.begin(), texinfos_sorted.end(),
        [](const maptexinfo_t &a, const maptexinfo_t &b) { return a.outputnum < b.outputnum; });

    json texinfofile = json::object();

    for (const auto &tx : texinfos_sorted) {
        if (!tx.outputnum.has_value() || !tx.flags.needs_write())
            continue;

        json t = json::object();

        if (tx.flags.is_nodraw) {
            t["is_nodraw"] = tx.flags.is_nodraw;
        }
        if (tx.flags.is_hint) {
            t["is_hint"] = tx.flags.is_hint;
        }
        if (tx.flags.no_dirt) {
            t["no_dirt"] = tx.flags.no_dirt;
        }
        if (tx.flags.no_shadow) {
            t["no_shadow"] = tx.flags.no_shadow;
        }
        if (tx.flags.no_bounce) {
            t["no_bounce"] = tx.flags.no_bounce;
        }
        if (tx.flags.no_minlight) {
            t["no_minlight"] = tx.flags.no_minlight;
        }
        if (tx.flags.no_expand) {
            t["no_expand"] = tx.flags.no_expand;
        }
        if (tx.flags.no_phong) {
            t["no_phong"] = tx.flags.no_phong;
        }
        if (tx.flags.light_ignore) {
            t["light_ignore"] = tx.flags.light_ignore;
        }
        if (tx.flags.surflight_rescale) {
            t["surflight_rescale"] = tx.flags.surflight_rescale.value();
        }
        if (tx.flags.surflight_style.has_value()) {
            t["surflight_style"] = tx.flags.surflight_style.value();
        }
        if (tx.flags.surflight_targetname.has_value()) {
            t["surflight_targetname"] = tx.flags.surflight_targetname.value();
        }
        if (tx.flags.surflight_color.has_value()) {
            t["surflight_color"] = tx.flags.surflight_color.value();
        }
        if (tx.flags.surflight_minlight_scale.has_value()) {
            t["surflight_minlight_scale"] = tx.flags.surflight_minlight_scale.value();
        }
        if (tx.flags.phong_angle) {
            t["phong_angle"] = tx.flags.phong_angle;
        }
        if (tx.flags.phong_angle_concave) {
            t["phong_angle_concave"] = tx.flags.phong_angle_concave;
        }
        if (tx.flags.phong_group) {
            t["phong_group"] = tx.flags.phong_group;
        }
        if (tx.flags.minlight) {
            t["minlight"] = *tx.flags.minlight;
        }
        if (tx.flags.maxlight) {
            t["maxlight"] = tx.flags.maxlight;
        }
        if (!qv::emptyExact(tx.flags.minlight_color)) {
            t["minlight_color"] = tx.flags.minlight_color;
        }
        if (tx.flags.light_alpha) {
            t["light_alpha"] = *tx.flags.light_alpha;
        }
        if (tx.flags.light_twosided) {
            t["light_twosided"] = *tx.flags.light_twosided;
        }
        if (tx.flags.lightcolorscale != 1.0) {
            t["lightcolorscale"] = tx.flags.lightcolorscale;
        }
        if (tx.flags.surflight_group) {
            t["surflight_group"] = tx.flags.surflight_group;
        }
        if (tx.flags.world_units_per_luxel) {
            t["world_units_per_luxel"] = *tx.flags.world_units_per_luxel;
        }
        if (tx.flags.object_channel_mask) {
            t["object_channel_mask"] = *tx.flags.object_channel_mask;
        }

        texinfofile[std::to_string(*tx.outputnum)].swap(t);
    }

    std::ofstream(file, std::ios_base::out | std::ios_base::binary) << texinfofile;
}

static bool Is16BitMarkfsurfaceFormat(const bspversion_t *version)
{
    for (auto &lumpspec : version->lumps) {
        if ((!strcmp("marksurfaces", lumpspec.name) || !strcmp("leaffaces", lumpspec.name)) && lumpspec.size == 2) {
            return true;
        }
    }
    return false;
}

/*
=============
WriteBSPFile
=============
*/
static void WriteBSPFile()
{
    bspdata_t bspdata{};

    bspdata.bsp = std::move(map.bsp);

    bspdata.version = &bspver_generic;

    if (map.needslmshifts) {
        bspdata.bspx.transfer("LMSHIFT", map.exported_lmshifts);
    }
    if (!map.exported_bspxbrushes.empty()) {
        bspdata.bspx.transfer("BRUSHLIST", map.exported_bspxbrushes);
    }

    const size_t num_faces = std::get<mbsp_t>(bspdata.bsp).dfaces.size();

    // convert to output format
    if (!ConvertBSPFormat(&bspdata, qbsp_options.target_version)) {
        const bspversion_t *extendedLimitsFormat = qbsp_options.target_version->extended_limits;

        if (!extendedLimitsFormat) {
            FError("No extended limits version of {} available", qbsp_options.target_version->name);
        } else if (!qbsp_options.allow_upgrade.value()) {
            FError("Limits exceeded for {} and allow_upgrade was disabled", qbsp_options.target_version->name);
        }

        logging::print("NOTE: limits exceeded for {} - switching to {}\n", qbsp_options.target_version->name,
            extendedLimitsFormat->name);

        Q_assert(ConvertBSPFormat(&bspdata, extendedLimitsFormat));
    }

    // Formats with 16-bit marksurfaces/leaffaces have two subformats:
    //  - the vanilla format with int16_t face indices (imposing a limit of 32768 faces)
    //  - an extended format with uint6_t face indices
    //
    // We don't model these as separate bspversion_t's, but this check allows -noallowupgrade
    // to force the vanilla format.
    if (Is16BitMarkfsurfaceFormat(bspdata.version) && num_faces > 32768) {
        if (!qbsp_options.allow_upgrade.value()) {
            FError("{} faces requires an extended-limits BSP, but allow_upgrade was disabled", num_faces);
        } else {
            logging::print("WARNING: {} faces requires unsigned marksurfaces, which is not supported by all "
                           "engines. Recompile with -bsp2 if targeting ezQuake.\n",
                num_faces);
        }
    }

    qbsp_options.bsp_path.replace_extension("bsp");

    WriteBSPFile(qbsp_options.bsp_path, &bspdata);
    logging::print("Wrote {}\n", qbsp_options.bsp_path);

    PrintBSPFileSizes(&bspdata);
}

/*
==================
FinishBSPFile
==================
*/
void FinishBSPFile(void)
{
    logging::funcheader();

    if (map.bsp.dvertexes.empty()) {
        // First vertex must remain unused because edge references it
        map.bsp.dvertexes.emplace_back();
        Q_assert(map.bsp.dvertexes.size() == 1);
    }

    WriteExtendedTexinfoFlags();
    WriteBSPFile();
}

/*
==================
UpdateBSPFileEntitiesLump
==================
*/
void UpdateBSPFileEntitiesLump()
{
    bspdata_t bspdata;

    qbsp_options.bsp_path.replace_extension("bsp");

    // load the .bsp
    LoadBSPFile(qbsp_options.bsp_path, &bspdata);

    bspdata.version->game->init_filesystem(qbsp_options.bsp_path, qbsp_options);

    ConvertBSPFormat(&bspdata, &bspver_generic);

    mbsp_t &bsp = std::get<mbsp_t>(bspdata.bsp);

    // replace the existing entities lump with map's exported entities
    bsp.dentdata = std::move(map.bsp.dentdata);

    // write the .bsp back to disk
    ConvertBSPFormat(&bspdata, bspdata.loadversion);
    WriteBSPFile(qbsp_options.bsp_path, &bspdata);

    logging::print("Wrote {}\n", qbsp_options.bsp_path);
}
