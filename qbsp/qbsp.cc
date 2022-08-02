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

#include <memory>
#include <cstring>
#include <algorithm>

#include <common/log.hh>
#include <common/aabb.hh>
#include <common/fs.hh>
#include <common/threads.hh>
#include <common/settings.hh>

#include <qbsp/brush.hh>
#include <qbsp/csg.hh>
#include <qbsp/map.hh>
#include <qbsp/merge.hh>
#include <qbsp/portals.hh>
#include <qbsp/prtfile.hh>
#include <qbsp/brushbsp.hh>
#include <qbsp/faces.hh>
#include <qbsp/qbsp.hh>
#include <qbsp/writebsp.hh>
#include <qbsp/outside.hh>
#include <qbsp/tree.hh>

#include <fmt/chrono.h>

#include "tbb/global_control.h"

// command line flags
namespace settings
{
setting_group game_target_group{"Game/BSP Target", -1};
setting_group map_development_group{"Map development", 1};
setting_group common_format_group{"Common format options", 2};
setting_group debugging_group{"Advanced/tool debugging", 500};

inline void set_target_version(const bspversion_t *version)
{
    if (qbsp_options.target_version) {
        FError("BSP version was set by multiple flags; currently {}, tried to change to {}\n",
            qbsp_options.target_version->name, version->name);
    }

    qbsp_options.target_version = version;
}

void qbsp_settings::initialize(int argc, const char **argv)
{
    if (auto file = fs::load("qbsp.ini")) {
        logging::print("Loading options from qbsp.ini\n");
        parser_t p(file->data(), file->size());
        parse(p);
    }

    try {
        token_parser_t p(argc - 1, argv + 1);
        auto remainder = parse(p);

        if (remainder.size() <= 0 || remainder.size() > 2) {
            printHelp();
        }

        qbsp_options.map_path = remainder[0];

        if (remainder.size() == 2) {
            qbsp_options.bsp_path = remainder[1];
        }
    } catch (parse_exception ex) {
        logging::print(ex.what());
        printHelp();
    }
}

void qbsp_settings::load_texture_def(const std::string &pathname)
{
    if (!fs::exists(pathname)) {
        FError("can't find texturedef file {}", pathname);
    }

    fs::data data = fs::load(pathname);
    parser_t parser(data);

    while (true) {
        if (!parser.parse_token() || parser.at_end()) {
            break;
        }

        std::string from = std::move(parser.token);

        if (!parser.parse_token(PARSE_SAMELINE)) {
            break;
        }

        std::string to = std::move(parser.token);
        std::optional<extended_texinfo_t> texinfo;

        // FIXME: why is this necessary? is it a trailing \0? only happens on release
        // repro with a texdef with no newline at the end
        while (std::isspace(to[to.size() - 1])) {
            to.resize(to.size() - 1);
        }

        if (parser.parse_token(PARSE_SAMELINE | PARSE_OPTIONAL)) {
            texinfo = extended_texinfo_t{std::stoi(parser.token)};

            if (parser.parse_token(PARSE_SAMELINE | PARSE_OPTIONAL)) {
                texinfo->flags.native = std::stoi(parser.token);
            }

            if (parser.parse_token(PARSE_SAMELINE | PARSE_OPTIONAL)) {
                texinfo->value = std::stoi(parser.token);
            }
        }

        loaded_texture_defs[from] = {to, texinfo};
    }
}

void qbsp_settings::load_entity_def(const std::string &pathname)
{
    if (!fs::exists(pathname)) {
        FError("can't find aliasdef file {}", pathname);
    }

    fs::data data = fs::load(pathname);
    parser_t parser(data);

    while (true) {
        if (!parser.parse_token() || parser.at_end()) {
            break;
        }

        std::string classname = std::move(parser.token);

        if (!parser.parse_token(PARSE_PEEK)) {
            FError("expected {{ in alias def {}, got end of file", pathname);
        }

        if (parser.token != "{") {
            FError("expected {{ in alias def {}, got {}", pathname, parser.token);
        }

        // parse ent dict
        loaded_entity_defs[classname] = parser;
    }
}

void qbsp_settings::postinitialize(int argc, const char **argv)
{
    // side effects from common
    if (logging::mask & logging::flag::VERBOSE) {
        qbsp_options.fAllverbose = true;
    }

    if ((logging::mask & (bitflags<logging::flag>(logging::flag::PERCENT) | logging::flag::STAT |
                             logging::flag::PROGRESS)) == logging::flag::NONE) {
        qbsp_options.fNoverbose = true;
    }

    // set target BSP type
    if (hlbsp.value()) {
        set_target_version(&bspver_hl);
    }

    if (q2bsp.value() || (q2rtx.value() && !q2bsp.isChanged() && !qbism.isChanged())) {
        set_target_version(&bspver_q2);
    }

    if (qbism.value()) {
        set_target_version(&bspver_qbism);
    }

    if (bsp2.value()) {
        set_target_version(&bspver_bsp2);
    }

    if (bsp2rmq.value()) {
        set_target_version(&bspver_bsp2rmq);
    }

    if (!qbsp_options.target_version) {
        set_target_version(&bspver_q1);
    }

    // if we wanted hexen2, update it now
    if (hexen2.value()) {
        if (qbsp_options.target_version == &bspver_bsp2) {
            qbsp_options.target_version = &bspver_h2bsp2;
        } else if (qbsp_options.target_version == &bspver_bsp2rmq) {
            qbsp_options.target_version = &bspver_h2bsp2rmq;
        } else {
            qbsp_options.target_version = &bspver_h2;
        }
    } else {
        if (!qbsp_options.target_version) {
            qbsp_options.target_version = &bspver_q1;
        }
    }

    // update target game
    qbsp_options.target_game = qbsp_options.target_version->game;

    /* If no wadpath given, default to the map directory */
    if (wadpaths.pathsValue().empty()) {
        wadpath wp{qbsp_options.map_path.parent_path(), false};

        // If options.map_path is a relative path, StrippedFilename will return the empty string.
        // In that case, don't add it as a wad path.
        if (!wp.path.empty()) {
            wadpaths.addPath(wp);
        }
    }

    // side effects from q2rtx
    if (q2rtx.value()) {
        if (!includeskip.isChanged()) {
            includeskip.setValue(true, settings::source::GAME_TARGET);
        }

        if (!software.isChanged()) {
            software.setValue(false, settings::source::GAME_TARGET);
        }
    }

    // side effects from Quake II
    if (qbsp_options.target_game->id == GAME_QUAKE_II) {
        if (!maxedges.isChanged()) {
            maxedges.setValue(0, settings::source::GAME_TARGET);
        }

        if (qbsp_options.target_version == &bspver_qbism) {
            if (!software.isChanged()) {
                software.setValue(false, settings::source::GAME_TARGET);
            }
        }

        if (!software.value() && !subdivide.isChanged()) {
            subdivide.setValue(496, settings::source::GAME_TARGET);
        }
    }

    // load texture defs
    for (auto &def : texturedefs.values()) {
        load_texture_def(def);
    }

    for (auto &def : aliasdefs.values()) {
        load_entity_def(def);
    }

    common_settings::postinitialize(argc, argv);
}
}; // namespace settings

settings::qbsp_settings qbsp_options;

// per-entity
static struct
{
    uint32_t total_brushes, total_brush_sides;
    uint32_t total_leaf_brushes;
} brush_state;

static void ExportBrushList_r(const mapentity_t *entity, node_t *node)
{
    if (node->is_leaf) {
        if (node->contents.native) {
            if (node->original_brushes.size()) {
                node->numleafbrushes = node->original_brushes.size();
                brush_state.total_leaf_brushes += node->numleafbrushes;
                node->firstleafbrush = map.bsp.dleafbrushes.size();
                for (auto &b : node->original_brushes) {
                    map.bsp.dleafbrushes.push_back(b->outputnumber.value());
                }
            }
        }

        return;
    }

    ExportBrushList_r(entity, node->children[0].get());
    ExportBrushList_r(entity, node->children[1].get());
}

/*
=================
AddBrushBevels

Adds any additional planes necessary to allow the brush to be expanded
against axial bounding boxes
=================
*/
static std::vector<std::tuple<size_t, const side_t *>> AddBrushBevels(const bspbrush_t &b)
{
    // add already-present planes
    std::vector<std::tuple<size_t, const side_t *>> planes;

    for (auto &f : b.sides) {
        const qplane3d &plane = f.get_plane();
        int32_t outputplanenum = ExportMapPlane(plane);
        planes.emplace_back(outputplanenum, &f);
    }

    //
    // add the axial planes
    //
    int32_t order = 0;
    for (int32_t axis = 0; axis < 3; axis++) {
        for (int32_t dir = -1; dir <= 1; dir += 2, order++) {
            size_t i;
            // see if the plane is allready present
            for (i = 0; i < planes.size(); i++) {
                if (map.bsp.dplanes[std::get<0>(planes[i])].normal[axis] == dir)
                    break;
            }

            if (i == planes.size()) {
                // add a new side
                qplane3d new_plane{};
                new_plane.normal[axis] = dir;
                if (dir == 1)
                    new_plane.dist = b.bounds.maxs()[axis];
                else
                    new_plane.dist = -b.bounds.mins()[axis];

                int32_t outputplanenum = ExportMapPlane(new_plane);
                planes.emplace_back(outputplanenum, &b.sides.front());
            }

            // if the plane is not in it canonical order, swap it
            if (i != order)
                std::swap(planes[i], planes[order]);
        }
    }

    //
    // add the edge bevels
    //
    if (planes.size() == 6)
        return planes; // pure axial

    // test the non-axial plane edges
    size_t edges_to_test = planes.size();
    for (size_t i = 6; i < edges_to_test; i++) {
        auto &s = std::get<1>(planes[i]);
        if (!s)
            continue;
        auto &w = s->w;
        if (!w.size())
            continue;
        for (size_t j = 0; j < w.size(); j++) {
            size_t k = (j + 1) % w.size();
            qvec3d vec = w[j] - w[k];
            if (qv::normalizeInPlace(vec) < 0.5)
                continue;
            vec = qv::Snap(vec);
            for (k = 0; k < 3; k++)
                if (vec[k] == -1 || vec[k] == 1)
                    break; // axial
            if (k != 3)
                continue; // only test non-axial edges

            // try the six possible slanted axials from this edge
            for (int32_t axis = 0; axis < 3; axis++) {
                for (int32_t dir = -1; dir <= 1; dir += 2) {
                    qvec3d vec2{};
                    // construct a plane
                    vec2[axis] = dir;
                    qplane3d current;
                    current.normal = qv::cross(vec, vec2);
                    if (qv::normalizeInPlace(current.normal) < 0.5)
                        continue;
                    current.dist = qv::dot(w[j], current.normal);

                    auto it = b.sides.begin();

                    // if all the points on all the sides are
                    // behind this plane, it is a proper edge bevel
                    for (; it != b.sides.end(); it++) {
                        auto &f = *it;
                        const qplane3d &plane = f.get_plane();

                        // if this plane has allready been used, skip it
                        if (qv::epsilonEqual(current, plane))
                            break;

                        auto &w2 = f.w;
                        if (!w2.size())
                            continue;
                        size_t l;
                        for (l = 0; l < w2.size(); l++) {
                            vec_t d = current.distance_to(w2[l]);
                            if (d > 0.1)
                                break; // point in front
                        }
                        if (l != w2.size())
                            break;
                    }

                    if (it != b.sides.end())
                        continue; // wasn't part of the outer hull

                    // add this plane
                    int32_t outputplanenum = ExportMapPlane(current);
                    planes.emplace_back(outputplanenum, &b.sides.front());
                }
            }
        }
    }

    return planes;
}

static void ExportBrushList(mapentity_t *entity, node_t *node)
{
    logging::funcheader();

    brush_state = {};

    for (auto &b : entity->brushes) {
        b->outputnumber = {static_cast<uint32_t>(map.bsp.dbrushes.size())};

        dbrush_t &brush = map.bsp.dbrushes.emplace_back(
            dbrush_t{static_cast<int32_t>(map.bsp.dbrushsides.size()), 0, b->contents.native});

        auto bevels = AddBrushBevels(*b);

        for (auto &plane : bevels) {
            map.bsp.dbrushsides.push_back(
                {(uint32_t)std::get<0>(plane), (int32_t)ExportMapTexinfo(std::get<1>(plane)->texinfo)});
            brush.numsides++;
            brush_state.total_brush_sides++;
        }

        brush_state.total_brushes++;
    }

    ExportBrushList_r(entity, node);

    logging::print(logging::flag::STAT, "     {:8} total brushes\n", brush_state.total_brushes);
    logging::print(logging::flag::STAT, "     {:8} total brush sides\n", brush_state.total_brush_sides);
    logging::print(logging::flag::STAT, "     {:8} total leaf brushes\n", brush_state.total_leaf_brushes);
}

winding_t BaseWindingForPlane(const qplane3d &p)
{
    return winding_t::from_plane(p, qbsp_options.worldextent.value());
}

static bool IsTrigger(const mapentity_t *entity)
{
    auto &tex = entity->mapbrush(0).face(0).texname;

    if (tex.length() < 6) {
        return false;
    }

    size_t trigger_pos = tex.rfind("trigger");

    if (trigger_pos == std::string::npos) {
        return false;
    }

    return trigger_pos == (tex.size() - strlen("trigger"));
}

static void CountLeafs_r(node_t *node, content_stats_base_t &stats)
{
    if (node->is_leaf) {
        qbsp_options.target_game->count_contents_in_stats(node->contents, stats);
        return;
    }
    CountLeafs_r(node->children[0].get(), stats);
    CountLeafs_r(node->children[1].get(), stats);
}

static void CountLeafs(node_t *headnode)
{
    logging::funcheader();

    auto stats = qbsp_options.target_game->create_content_stats();
    CountLeafs_r(headnode, *stats);
    qbsp_options.target_game->print_content_stats(*stats, "leafs");
}

/*
===============
ProcessEntity
===============
*/
static void ProcessEntity(mapentity_t *entity, const int hullnum)
{
    /* No map brushes means non-bmodel entity.
       We need to handle worldspawn containing no brushes, though. */
    if (!entity->nummapbrushes && entity != map.world_entity())
        return;

    /*
     * func_group and func_detail entities get their brushes added to the
     * worldspawn
     */
    if (IsWorldBrushEntity(entity) || IsNonRemoveWorldBrushEntity(entity))
        return;

    // for notriggermodels: if we have at least one trigger-like texture, do special trigger stuff
    bool discarded_trigger = entity != map.world_entity() && qbsp_options.notriggermodels.value() && IsTrigger(entity);

    // Export a blank model struct, and reserve the index (only do this once, for all hulls)
    if (!discarded_trigger) {
        if (!entity->outputmodelnumber.has_value()) {
            entity->outputmodelnumber = map.bsp.dmodels.size();
            map.bsp.dmodels.emplace_back();
        }

        if (entity != map.world_entity()) {
            if (entity == map.world_entity() + 1)
                logging::header("Internal Entities");

            std::string mod = fmt::format("*{}", entity->outputmodelnumber.value());

            if (qbsp_options.fVerbose)
                PrintEntity(entity);

            if (hullnum <= 0)
                logging::print(logging::flag::STAT, "     MODEL: {}\n", mod);
            entity->epairs.set("model", mod);
        }
    }

    if (qbsp_options.lmscale.isChanged() && !entity->epairs.has("_lmscale")) {
        entity->epairs.set("_lmscale", std::to_string(qbsp_options.lmscale.value()));
    }

    /*
     * Init the entity
     */
    entity->brushes.clear();
    entity->bounds = {};

    /*
     * Convert the map brushes (planes) into BSP brushes (polygons)
     */
    Brush_LoadEntity(entity, hullnum);

    // assign brush file order
    for (size_t i = 0; i < entity->brushes.size(); ++i) {
        entity->brushes[i]->file_order = i;
    }

    // entity->brushes = ChopBrushes(entity->brushes);

    //    if (entity == map.world_entity() && hullnum <= 0) {
    //        if (options.debugchop.value()) {
    //            fs::path path = options.bsp_path;
    //            path.replace_extension(".chop.map");
    //
    //            WriteBspBrushMap(path, entity->brushes);
    //        }
    //    }

    // we're discarding the brush
    if (discarded_trigger) {
        entity->brushes.clear();
        entity->epairs.set("mins", fmt::to_string(entity->bounds.mins()));
        entity->epairs.set("maxs", fmt::to_string(entity->bounds.maxs()));
        return;
    }

    std::unique_ptr<tree_t> tree = nullptr;
    if (hullnum > 0) {
        tree = BrushBSP(entity, true);
        if (entity == map.world_entity() && !qbsp_options.nofill.value()) {
            // assume non-world bmodels are simple
            MakeTreePortals(tree.get());
            if (FillOutside(entity, tree.get(), hullnum)) {
                // make a really good tree
                tree = BrushBSP(entity, false);

                // fill again so PruneNodes works
                MakeTreePortals(tree.get());
                FillOutside(entity, tree.get(), hullnum);
                PruneNodes(tree->headnode.get());
            }
        }
        ExportClipNodes(entity, tree->headnode.get(), hullnum);

        // fixme-brushbsp: return here?
    } else {
        if (qbsp_options.forcegoodtree.value()) {
            tree = BrushBSP(entity, false);
        } else {
            tree = BrushBSP(entity, entity == map.world_entity() ? std::nullopt : std::optional<bool>(false));
        }

        // build all the portals in the bsp tree
        // some portals are solid polygons, and some are paths to other leafs
        MakeTreePortals(tree.get());

        if (entity == map.world_entity()) {
            // flood fills from the void.
            // marks brush sides which are *only* touching void;
            // we can skip using them as BSP splitters on the "really good tree"
            // (effectively expanding those brush sides outwards).
            if (!qbsp_options.nofill.value() && FillOutside(entity, tree.get(), hullnum)) {
                // make a really good tree
                tree = BrushBSP(entity, false);

                // make the real portals for vis tracing
                MakeTreePortals(tree.get());

                // fill again so PruneNodes works
                FillOutside(entity, tree.get(), hullnum);
            }

            // Area portals
            if (qbsp_options.target_game->id == GAME_QUAKE_II) {
                FloodAreas(entity, tree->headnode.get());
                EmitAreaPortals(tree->headnode.get());
            }
        } else {
            FillBrushEntity(entity, tree.get(), hullnum);

            // rebuild BSP now that we've marked invisible brush sides
            tree = BrushBSP(entity, false);
        }

        MakeTreePortals(tree.get());

        MarkVisibleSides(tree.get(), entity);
        MakeFaces(tree->headnode.get());

        FreeTreePortals(tree.get());
        PruneNodes(tree->headnode.get());

        if (hullnum <= 0 && entity == map.world_entity() && (!map.leakfile || qbsp_options.keepprt.value())) {
            WritePortalFile(tree.get());
        }

        // needs to come after any face creation
        MakeMarkFaces(tree->headnode.get());

        CountLeafs(tree->headnode.get());

        // output vertices first, since TJunc needs it
        EmitVertices(tree->headnode.get());

        TJunc(tree->headnode.get());

        if (qbsp_options.objexport.value() && entity == map.world_entity()) {
            ExportObj_Nodes("pre_makefaceedges_plane_faces", tree->headnode.get());
            ExportObj_Marksurfaces("pre_makefaceedges_marksurfaces", tree->headnode.get());
        }

        Q_assert(entity->firstoutputfacenumber == -1);

        entity->firstoutputfacenumber = MakeFaceEdges(tree->headnode.get());

        if (qbsp_options.target_game->id == GAME_QUAKE_II) {
            ExportBrushList(entity, tree->headnode.get());
        }

        ExportDrawNodes(entity, tree->headnode.get(), entity->firstoutputfacenumber);
    }

    FreeBrushes(entity);
}

/*
=================
UpdateEntLump

=================
*/
static void UpdateEntLump(void)
{
    int modnum;
    mapentity_t *entity;

    logging::print(logging::flag::STAT, "     Updating entities lump...\n");

    modnum = 1;
    for (int i = 1; i < map.entities.size(); i++) {
        entity = &map.entities.at(i);

        /* Special handling for misc_external_map.
           Duplicates some logic from ProcessExternalMapEntity. */
        bool is_misc_external_map = false;
        if (!Q_strcasecmp(entity->epairs.get("classname"), "misc_external_map")) {
            const std::string &new_classname = entity->epairs.get("_external_map_classname");

            entity->epairs.set("classname", new_classname);
            entity->epairs.set("origin", "0 0 0");

            /* Note: the classname could have switched to
             * a IsWorldBrushEntity entity (func_group, func_detail),
             * or a bmodel entity (func_wall
             */
            is_misc_external_map = true;
        }

        bool isBrushEnt = (entity->nummapbrushes > 0) || is_misc_external_map;
        if (!isBrushEnt)
            continue;

        if (IsWorldBrushEntity(entity) || IsNonRemoveWorldBrushEntity(entity))
            continue;

        entity->epairs.set("model", fmt::format("*{}", modnum));
        modnum++;

        /* Do extra work for rotating entities if necessary */
        const std::string &classname = entity->epairs.get("classname");
        if (!classname.compare(0, 7, "rotate_")) {
            FixRotateOrigin(entity);
        }
    }

    WriteEntitiesToString();
    UpdateBSPFileEntitiesLump();

    if (!qbsp_options.fAllverbose) {
        qbsp_options.fVerbose = false;
        logging::mask &=
            ~(bitflags<logging::flag>(logging::flag::STAT) | logging::flag::PROGRESS | logging::flag::PERCENT);
    }
}

/*
Actually writes out the final bspx BRUSHLIST lump
This lump replaces the clipnodes stuff for custom collision sizes.
*/
void BSPX_Brushes_Finalize(struct bspxbrushes_s *ctx)
{
    // Actually written in WriteBSPFile()
    map.exported_bspxbrushes = std::move(ctx->lumpdata);
}
void BSPX_Brushes_Init(struct bspxbrushes_s *ctx)
{
    ctx->lumpdata.clear();
}

/*
WriteBrushes
Generates a submodel's direct brush information to a separate file, so the engine doesn't need to depend upon specific
hull sizes
*/

static void BSPX_Brushes_AddModel(
    struct bspxbrushes_s *ctx, int modelnum, std::vector<std::unique_ptr<bspbrush_t>> &brushes)
{
    std::shared_lock lock(map_planes_lock);

    bspxbrushes_permodel permodel{1, modelnum};

    for (auto &b : brushes) {
        permodel.numbrushes++;
        for (auto &f : b->sides) {
            /*skip axial*/
            const auto &plane = f.get_plane();
            if (plane.get_type() < plane_type_t::PLANE_ANYX)
                continue;
            permodel.numfaces++;
        }
    }

    std::ostringstream str(std::ios_base::out | std::ios_base::binary);

    str << endianness<std::endian::little>;

    str <= permodel;

    for (auto &b : brushes) {
        bspxbrushes_perbrush perbrush{};

        for (auto &f : b->sides) {
            /*skip axial*/
            const auto &plane = f.get_plane();
            if (plane.get_type() < plane_type_t::PLANE_ANYX)
                continue;
            perbrush.numfaces++;
        }

        perbrush.bounds = b->bounds;

        switch (b->contents.native) {
            // contents should match the engine.
            case CONTENTS_EMPTY: // really an error, but whatever
            case CONTENTS_SOLID: // these are okay
            case CONTENTS_WATER:
            case CONTENTS_SLIME:
            case CONTENTS_LAVA:
            case CONTENTS_SKY:
                if (b->contents.is_clip(qbsp_options.target_game)) {
                    perbrush.contents = -8;
                } else {
                    perbrush.contents = b->contents.native;
                }
                break;
            //              case CONTENTS_LADDER:
            //                      perbrush.contents = -16;
            //                      break;
            default: {
                if (b->contents.is_clip(qbsp_options.target_game)) {
                    perbrush.contents = -8;
                } else {
                    logging::print("WARNING: Unknown contents: {}. Translating to solid.\n",
                        b->contents.to_string(qbsp_options.target_game));
                    perbrush.contents = CONTENTS_SOLID;
                }
                break;
            }
        }

        str <= perbrush;

        for (auto &f : b->sides) {
            /*skip axial*/
            const auto &plane = f.get_plane();
            if (plane.get_type() < plane_type_t::PLANE_ANYX)
                continue;

            bspxbrushes_perface perface = qplane3f(plane.get_normal(), plane.get_dist());
            str <= std::tie(perface.normal, perface.dist);
        }
    }

    std::string data = str.str();
    ctx->lumpdata.insert(ctx->lumpdata.end(), (uint8_t *)data.data(), ((uint8_t *)data.data()) + data.size());
}

/* for generating BRUSHLIST bspx lump */
static void BSPX_CreateBrushList(void)
{
    struct bspxbrushes_s ctx;

    if (!qbsp_options.wrbrushes.value())
        return;

    BSPX_Brushes_Init(&ctx);

    for (int entnum = 0; entnum < map.entities.size(); ++entnum) {
        mapentity_t *ent = &map.entities.at(entnum);
        int modelnum;
        if (ent == map.world_entity()) {
            modelnum = 0;
        } else {
            const std::string &mod = ent->epairs.get("model");
            if (mod[0] != '*')
                continue;
            modelnum = std::stoi(mod.substr(1));
        }

        ent->brushes.clear();

        Brush_LoadEntity(ent, HULL_COLLISION);

        if (ent->brushes.empty())
            continue; // non-bmodel entity

        BSPX_Brushes_AddModel(&ctx, modelnum, ent->brushes);
        FreeBrushes(ent);
    }

    BSPX_Brushes_Finalize(&ctx);
}

/*
=================
CreateSingleHull
=================
*/
static void CreateSingleHull(const int hullnum)
{
    logging::print("Processing hull {}...\n", hullnum);

    // for each entity in the map file that has geometry
    for (auto &entity : map.entities) {
        ProcessEntity(&entity, hullnum);
        if (!qbsp_options.fAllverbose) {
            qbsp_options.fVerbose = false; // don't print rest of entities
            logging::mask &=
                ~(bitflags<logging::flag>(logging::flag::STAT) | logging::flag::PROGRESS | logging::flag::PERCENT);
        }
    }
}

/*
=================
CreateHulls
=================
*/
static void CreateHulls(void)
{
    /* create the hulls sequentially */
    if (!qbsp_options.fNoverbose) {
        qbsp_options.fVerbose = true;
        logging::mask |=
            (bitflags<logging::flag>(logging::flag::STAT) | logging::flag::PROGRESS | logging::flag::PERCENT);
    }

    auto &hulls = qbsp_options.target_game->get_hull_sizes();

    // game has no hulls, so we have to export brush lists and stuff.
    if (!hulls.size()) {
        CreateSingleHull(HULL_COLLISION);
        // only create hull 0 if fNoclip is set
    } else if (qbsp_options.noclip.value()) {
        CreateSingleHull(0);
        // do all the hulls
    } else {
        for (size_t i = 0; i < hulls.size(); i++) {
            CreateSingleHull(i);
        }
    }
}

// Fill the BSP's `dtex` data
static void LoadTextureData()
{
    for (size_t i = 0; i < map.miptex.size(); i++) {
        // always fill the name even if we can't find it
        auto &miptex = map.bsp.dtex.textures[i];
        miptex.name = map.miptex[i].name;

        {
            auto [tex, pos, file] = img::load_texture(map.miptex[i].name, true, qbsp_options.target_game, qbsp_options);

            if (!tex) {
                if (pos.archive) {
                    logging::print("WARNING: unable to load texture {} in archive {}\n", map.miptex[i].name,
                        pos.archive->pathname);
                } else {
                    logging::print("WARNING: unable to find texture {}\n", map.miptex[i].name);
                }
            } else {
                miptex.width = tex->meta.width;
                miptex.height = tex->meta.height;

                // only mips can be embedded directly
                if (!pos.archive->external && tex->meta.extension == img::ext::MIP) {
                    miptex.data = std::move(file.value());
                    continue;
                }
            }
        }

        // fall back to when we can't load the image.
        // construct fake data that solely contains the header.
        miptex.data.resize(sizeof(dmiptex_t));

        dmiptex_t header{};
        if (miptex.name.size() >= 16) {
            logging::print("WARNING: texture {} name too long for Quake miptex\n", miptex.name);
            std::copy_n(miptex.name.begin(), 15, header.name.begin());
        } else {
            std::copy(miptex.name.begin(), miptex.name.end(), header.name.begin());
        }

        header.width = miptex.width;
        header.height = miptex.height;
        header.offsets = {-1, -1, -1, -1};

        omemstream stream(miptex.data.data(), miptex.data.size());
        stream <= header;
    }
}

static void AddAnimationFrames()
{
    size_t oldcount = map.miptex.size();

    for (size_t i = 0; i < oldcount; i++) {
        const std::string &existing_name = map.miptexTextureName(i);

        if (existing_name[0] != '+' && (qbsp_options.target_game->id != GAME_HALF_LIFE || existing_name[0] != '-')) {
            continue;
        }

        std::string name = map.miptexTextureName(i);

        /* Search for all animations (0-9) and alt-animations (A-J) */
        for (size_t j = 0; j < 20; j++) {
            name[1] = (j < 10) ? '0' + j : 'a' + j - 10;
            if (fs::where(name)) {
                FindMiptex(name.c_str());
            }
        }
    }

    logging::print(logging::flag::STAT, "     {:8} texture frames added\n", map.miptex.size() - oldcount);
}

static void LoadSecondaryTextures()
{
    // Q2 doesn't use any secondary textures
    if (qbsp_options.target_game->id == GAME_QUAKE_II) {
        return;
    }

    AddAnimationFrames();

    /* Default texture data to store in worldmodel */
    map.bsp.dtex.textures.resize(map.miptex.size());

    LoadTextureData();
}

/*
=================
ProcessFile
=================
*/
void ProcessFile()
{
    // load brushes and entities
    LoadMapFile();

    if (qbsp_options.convertmapformat.value() != conversion_t::none) {
        ConvertMapFile();
        return;
    }
    if (qbsp_options.onlyents.value()) {
        UpdateEntLump();
        return;
    }

    // initialize secondary textures
    LoadSecondaryTextures();

    // init the tables to be shared by all models
    BeginBSPFile();

    if (!qbsp_options.fAllverbose) {
        qbsp_options.fVerbose = false;
        logging::mask &=
            ~(bitflags<logging::flag>(logging::flag::STAT) | logging::flag::PROGRESS | logging::flag::PERCENT);
    }

    // calculate extents, if required
    if (!qbsp_options.worldextent.value()) {
        CalculateWorldExtent();
    }

    // create hulls!
    CreateHulls();
    
    // undo logging changes
    if (!qbsp_options.fNoverbose) {
        qbsp_options.fVerbose = true;
        logging::mask |=
            (bitflags<logging::flag>(logging::flag::STAT) | logging::flag::PROGRESS | logging::flag::PERCENT);
    }

    WriteEntitiesToString();
    BSPX_CreateBrushList();
    FinishBSPFile();
}

/*
==================
MakeSkipTexinfo
==================
*/
static int MakeSkipTexinfo()
{
    maptexinfo_t mt{};

    mt.miptex = FindMiptex("skip", true);
    mt.flags.is_skip = true;

    return FindTexinfo(mt);
}

/*
==================
InitQBSP
==================
*/
void InitQBSP(int argc, const char **argv)
{
    // In case we're launched more than once, in testqbsp
    map.reset();
    qbsp_options.reset();
    // fixme-brushbsp: clear any other members of qbsp_settings
    qbsp_options.target_game = nullptr;
    qbsp_options.target_version = nullptr;

    qbsp_options.run(argc, argv);

    qbsp_options.map_path.replace_extension("map");

    // The .map extension gets removed right away anyways...
    if (qbsp_options.bsp_path.empty())
        qbsp_options.bsp_path = qbsp_options.map_path;

    /* Start logging to <bspname>.log */
    logging::init(fs::path(qbsp_options.bsp_path).replace_extension("log"), qbsp_options);

    // Remove already existing files
    if (!qbsp_options.onlyents.value() && qbsp_options.convertmapformat.value() == conversion_t::none) {
        qbsp_options.bsp_path.replace_extension("bsp");
        remove(qbsp_options.bsp_path);

        // Probably not the best place to do this
        logging::print("Input file: {}\n", qbsp_options.map_path);
        logging::print("Output file: {}\n\n", qbsp_options.bsp_path);

        fs::path prtfile = qbsp_options.bsp_path;
        prtfile.replace_extension("prt");
        remove(prtfile);

        fs::path ptsfile = qbsp_options.bsp_path;
        ptsfile.replace_extension("pts");
        remove(ptsfile);

        fs::path porfile = qbsp_options.bsp_path;
        porfile.replace_extension("por");
        remove(porfile);
    }

    // onlyents might not load this yet
    if (qbsp_options.target_game) {
        qbsp_options.target_game->init_filesystem(qbsp_options.map_path, qbsp_options);
    }

    // make skip texinfo, in case map needs it (it'll get culled out if not)
    map.skip_texinfo = MakeSkipTexinfo();
}

void InitQBSP(const std::vector<std::string> &args)
{
    std::vector<const char *> argPtrs;
    for (const std::string &arg : args) {
        argPtrs.push_back(arg.data());
    }

    InitQBSP(argPtrs.size(), argPtrs.data());
}

#include <fstream>

/*
==================
main
==================
*/
int qbsp_main(int argc, const char **argv)
{
    InitQBSP(argc, argv);

    // do it!
    auto start = I_FloatTime();
    ProcessFile();
    auto end = I_FloatTime();

    logging::print("\n{:.3} seconds elapsed\n", (end - start));

    logging::close();

    return 0;
}
