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
#include <qbsp/csg4.hh>
#include <qbsp/map.hh>
#include <qbsp/merge.hh>
#include <qbsp/portals.hh>
#include <qbsp/solidbsp.hh>
#include <qbsp/surfaces.hh>
#include <qbsp/qbsp.hh>
#include <qbsp/writebsp.hh>
#include <qbsp/outside.hh>

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
    if (options.target_version) {
        FError("BSP version was set by multiple flags; currently {}, tried to change to {}\n",
            options.target_version->name, version->name);
    }

    options.target_version = version;
}

void qbsp_settings::initialize(int argc, const char **argv)
{
    if (auto file = fs::load("qbsp.ini")) {
        logging::print("Loading options from qbsp.ini\n");
        parser_t p(file->data(), file->size());
        parse(p);
    }
    
    try
    {
        token_parser_t p(argc - 1, argv + 1);
        auto remainder = parse(p);

        if (remainder.size() <= 0 || remainder.size() > 2) {
            printHelp();
        }

        options.map_path = remainder[0];

        if (remainder.size() == 2) {
            options.bsp_path = remainder[1];
        }
    }
    catch (parse_exception ex)
    {
        logging::print(ex.what());
        printHelp();
    }
}

void qbsp_settings::postinitialize(int argc, const char **argv)
{
    // side effects from common
    if (logging::mask & logging::flag::VERBOSE) {
        options.fAllverbose = true;
    }

    if ((logging::mask & (bitflags<logging::flag>(logging::flag::PERCENT) | logging::flag::STAT | logging::flag::PROGRESS)) == logging::flag::NONE) {
        options.fNoverbose = true;
    }

    // set target BSP type
    if (hlbsp.value()) {
        set_target_version(&bspver_hl);
    }

    if (q2bsp.value() || 
        (q2rtx.value() && !q2bsp.isChanged() && !qbism.isChanged())) {
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

    if (!options.target_version) {
        set_target_version(&bspver_q1);
    }

    // if we wanted hexen2, update it now
    if (hexen2.value()) {
        if (options.target_version == &bspver_bsp2) {
            options.target_version = &bspver_h2bsp2;
        } else if (options.target_version == &bspver_bsp2rmq) {
            options.target_version = &bspver_h2bsp2rmq;
        } else {
            options.target_version = &bspver_h2;
        }
    } else {
        if (!options.target_version) {
            options.target_version = &bspver_q1;
        }
    }

    // update target game
    options.target_game = options.target_version->game;

    /* If no wadpath given, default to the map directory */
    if (wadpaths.pathsValue().empty()) {
        wadpath wp{options.map_path.parent_path(), false};

        // If options.map_path is a relative path, StrippedFilename will return the empty string.
        // In that case, don't add it as a wad path.
        if (!wp.path.empty()) {
            wadpaths.addPath(wp);
        }
    }

    // side effects from q2rtx
    if (q2rtx.value()) {
        if (!subdivide.isChanged()) {
            subdivide.setValueLocked(0);
        }

        if (!includeskip.isChanged()) {
            includeskip.setValueLocked(true);
        }
    }

    common_settings::postinitialize(argc, argv);
}
}; // namespace settings

settings::qbsp_settings options;

bool node_t::opaque() const
{
    return contents.is_sky(options.target_game) || contents.is_solid(options.target_game);
}

// a simple tree structure used for leaf brush
// compression.
struct leafbrush_entry_t
{
    uint32_t offset;
    std::map<uint32_t, leafbrush_entry_t> entries;
};

// per-entity
static struct
{
    uint32_t total_brushes, total_brush_sides;
    uint32_t total_leaf_brushes;
} brush_state;

static void ExportBrushList_r(const mapentity_t *entity, node_t *node, const uint32_t &brush_offset)
{
    if (node->planenum == PLANENUM_LEAF) {
        if (node->contents.native) {
            uint32_t b_id = brush_offset;
            std::vector<uint32_t> brushes;

            for (auto &b : entity->brushes) {
                if (entity != map.world_entity() || !node->bounds.disjoint_or_touching(b.bounds)) {
                    brushes.push_back(b_id);
                }
                b_id++;
            }

            if (brushes.size()) {
                node->numleafbrushes = brushes.size();
                brush_state.total_leaf_brushes += node->numleafbrushes;
                node->firstleafbrush = map.bsp.dleafbrushes.size();
                map.bsp.dleafbrushes.insert(map.bsp.dleafbrushes.end(), brushes.begin(), brushes.end());
            }
        }

        return;
    }

    ExportBrushList_r(entity, node->children[0], brush_offset);
    ExportBrushList_r(entity, node->children[1], brush_offset);
}

/*
=================
AddBrushBevels

Adds any additional planes necessary to allow the brush to be expanded
against axial bounding boxes
=================
*/
static std::vector<std::tuple<size_t, const face_t *>> AddBrushBevels(const brush_t &b)
{
    // add already-present planes
    std::vector<std::tuple<size_t, const face_t *>> planes;

    for (auto &f : b.faces) {
        int32_t planenum = f.planenum;

        if (f.planeside) {
            planenum = FindPlane(-map.planes[f.planenum], nullptr);
        }

        int32_t outputplanenum = ExportMapPlane(planenum);
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

                int32_t planenum = FindPlane(new_plane, nullptr);
                int32_t outputplanenum = ExportMapPlane(planenum);
                planes.emplace_back(outputplanenum, &b.faces.front());
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

                    auto it = b.faces.begin();

                    // if all the points on all the sides are
                    // behind this plane, it is a proper edge bevel
                    for (; it != b.faces.end(); it++) {
                        auto &f = *it;
                        auto &plane = map.planes[f.planenum];
                        qplane3d temp = f.planeside ? -plane : plane;

                        // if this plane has allready been used, skip it
                        if (qv::epsilonEqual(current, temp))
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

                    if (it != b.faces.end())
                        continue; // wasn't part of the outer hull

                    // add this plane
                    int32_t planenum = FindPlane(current, nullptr);
                    int32_t outputplanenum = ExportMapPlane(planenum);
                    planes.emplace_back(outputplanenum, &b.faces.front());
                }
            }
        }
    }

    return planes;
}

static void ExportBrushList(const mapentity_t *entity, node_t *node, uint32_t &brush_offset)
{
    logging::print(logging::flag::PROGRESS, "---- {} ----\n", __func__);

    brush_state = {};

    for (auto &b : entity->brushes) {
        dbrush_t &brush = map.bsp.dbrushes.emplace_back(
            dbrush_t{static_cast<int32_t>(map.bsp.dbrushsides.size()), 0, b.contents.native});

        auto bevels = AddBrushBevels(b);

        for (auto &plane : bevels) {
            map.bsp.dbrushsides.push_back(
                {(uint32_t)std::get<0>(plane), (int32_t)ExportMapTexinfo(std::get<1>(plane)->texinfo)});
            brush.numsides++;
            brush_state.total_brush_sides++;
        }

        brush_state.total_brushes++;
    }

    ExportBrushList_r(entity, node, brush_offset);

    brush_offset += brush_state.total_brushes;

    logging::print(logging::flag::STAT, "     {:8} total brushes\n", brush_state.total_brushes);
    logging::print(logging::flag::STAT, "     {:8} total brush sides\n", brush_state.total_brush_sides);
    logging::print(logging::flag::STAT, "     {:8} total leaf brushes\n", brush_state.total_leaf_brushes);
}

/*
=========================================================

FLOOD AREAS

=========================================================
*/

/*
===============
Portal_EntityFlood

The entity flood determines which areas are
"outside" on the map, which are then filled in.
Flowing from side s to side !s
===============
*/
static bool Portal_EntityFlood(const portal_t *p, int32_t s)
{
    auto contents0 = ClusterContents(p->nodes[0]);
    auto contents1 = ClusterContents(p->nodes[1]);

    // can never cross to a solid
    if (contents0.is_solid(options.target_game))
        return false;
    if (contents1.is_solid(options.target_game))
        return false;

    // can flood through everything else
    return true;
}

static void ApplyArea_r(node_t *node)
{
    node->area = map.c_areas;

    if (node->planenum != PLANENUM_LEAF) {
        ApplyArea_r(node->children[0]);
        ApplyArea_r(node->children[1]);
    }
}

static mapentity_t *AreanodeEntityForLeaf(node_t *node)
{
    // if detail cluster, search the children recursively
    if (node->planenum != PLANENUM_LEAF) {
        if (auto *child0result = AreanodeEntityForLeaf(node->children[0]); child0result) {
            return child0result;
        }
        return AreanodeEntityForLeaf(node->children[1]);
    }

    for (auto *face : node->markfaces) {
        const std::string &classname = face->src_entity->epairs.get("classname");
        if (0 == Q_strcasecmp(classname, "func_areaportal")) {
            return face->src_entity;
        }
    }
    return nullptr;
}

/*
=============
FloodAreas_r
=============
*/
static void FloodAreas_r(node_t *node)
{
    if ((node->planenum == PLANENUM_LEAF || node->detail_separator) && (ClusterContents(node).native & Q2_CONTENTS_AREAPORTAL)) {
        // grab the func_areanode entity
        mapentity_t *entity = AreanodeEntityForLeaf(node);

        if (entity == nullptr)
        {
            logging::print("WARNING: areaportal contents in node, but no entity found {} -> {}\n",
                node->bounds.mins(),
                node->bounds.maxs());
            return;
        }

        // this node is part of an area portal;
        // if the current area has allready touched this
        // portal, we are done
        if (entity->portalareas[0] == map.c_areas || entity->portalareas[1] == map.c_areas)
            return;

        // note the current area as bounding the portal
        if (entity->portalareas[1]) {
            logging::print("WARNING: areaportal entity {} touches > 2 areas\n  Entity Bounds: {} -> {}\n",
                entity - map.entities.data(), entity->bounds.mins(),
                entity->bounds.maxs());
            return;
        }

        if (entity->portalareas[0])
            entity->portalareas[1] = map.c_areas;
        else
            entity->portalareas[0] = map.c_areas;

        return;
    }

    if (node->area)
        return; // already got it

    node->area = map.c_areas;

    // propagate area assignment to descendants if we're a cluster
    if (!(node->planenum == PLANENUM_LEAF)) {
        ApplyArea_r(node);
    }

    int32_t s;

    for (portal_t *p = node->portals; p; p = p->next[s]) {
        s = (p->nodes[1] == node);
#if 0
		if (p->nodes[!s]->occupied)
			continue;
#endif
        if (!Portal_EntityFlood(p, s))
            continue;

        FloodAreas_r(p->nodes[!s]);
    }
}

/*
=============
FindAreas_r

Just decend the tree, and for each node that hasn't had an
area set, flood fill out from there
=============
*/
static void FindAreas(node_t *node)
{
    auto leafs = FindOccupiedClusters(node);
    for (auto *leaf : leafs) {
        if (leaf->area)
            continue;

        // area portals are always only flooded into, never
        // out of
        if (ClusterContents(leaf).native & Q2_CONTENTS_AREAPORTAL)
            continue;

        map.c_areas++;
        FloodAreas_r(leaf);
    }
}

/*
=============
SetAreaPortalAreas_r

Just decend the tree, and for each node that hasn't had an
area set, flood fill out from there
=============
*/
static void SetAreaPortalAreas_r(node_t *node)
{
    if (node->planenum != PLANENUM_LEAF) {
        SetAreaPortalAreas_r(node->children[0]);
        SetAreaPortalAreas_r(node->children[1]);
        return;
    }

    if (node->contents.native != Q2_CONTENTS_AREAPORTAL)
        return;

    if (node->area)
        return; // already set

    // grab the func_areanode entity
    mapentity_t *entity = AreanodeEntityForLeaf(node);

    if (!entity)
    {
        logging::print("WARNING: areaportal missing for node: {} -> {}\n",
            node->bounds.mins(), node->bounds.maxs());
        return;
    }

    node->area = entity->portalareas[0];
    if (!entity->portalareas[1]) {
        logging::print("WARNING: areaportal entity {} doesn't touch two areas\n  Entity Bounds: {} -> {}\n",
            entity - map.entities.data(),
            entity->bounds.mins(), entity->bounds.maxs());
        return;
    }
}

/*
=============
FloodAreas

Mark each leaf with an area, bounded by CONTENTS_AREAPORTAL
=============
*/
static void FloodAreas(mapentity_t *entity, node_t *headnode)
{
    logging::print(logging::flag::PROGRESS, "---- {} ----\n", __func__);
    FindAreas(headnode);
    SetAreaPortalAreas_r(headnode);
    logging::print(logging::flag::STAT, "{:5} areas\n", map.c_areas);
}

/*
=============
EmitAreaPortals

=============
*/
static void EmitAreaPortals(node_t *headnode)
{
    logging::print(logging::flag::PROGRESS, "---- {} ----\n", __func__);

    map.bsp.dareaportals.emplace_back();
    map.bsp.dareas.emplace_back();

    for (size_t i = 1; i <= map.c_areas; i++) {
        darea_t &area = map.bsp.dareas.emplace_back();
        area.firstareaportal = map.bsp.dareaportals.size();

        for (auto &e : map.entities) {

            if (!e.areaportalnum)
                continue;
            dareaportal_t dp = {};

            if (e.portalareas[0] == i) {
                dp.portalnum = e.areaportalnum;
                dp.otherarea = e.portalareas[1];
            } else if (e.portalareas[1] == i) {
                dp.portalnum = e.areaportalnum;
                dp.otherarea = e.portalareas[0];
            }

            size_t j = 0;

            for (; j < map.bsp.dareaportals.size(); j++)
            {
                if (map.bsp.dareaportals[j] == dp)
                    break;
            }

            if (j == map.bsp.dareaportals.size())
                map.bsp.dareaportals.push_back(dp);
        }

        area.numareaportals = map.bsp.dareaportals.size() - area.firstareaportal;
    }

    logging::print(logging::flag::STAT, "{:5} numareas\n", map.bsp.dareas.size());
    logging::print(logging::flag::STAT, "{:5} numareaportals\n", map.bsp.dareaportals.size());
}

winding_t BaseWindingForPlane(const qplane3d &p)
{
    return winding_t::from_plane(p, options.worldextent.value());
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

/*
===============
ProcessEntity
===============
*/
static void ProcessEntity(mapentity_t *entity, const int hullnum)
{
    int firstface;
    node_t *nodes;

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
    bool discarded_trigger = entity != map.world_entity() &&
        options.notriggermodels.value() &&
        IsTrigger(entity);

    // Export a blank model struct, and reserve the index (only do this once, for all hulls)
    if (!discarded_trigger) {
        if (!entity->outputmodelnumber.has_value()) {
            entity->outputmodelnumber = map.bsp.dmodels.size();
            map.bsp.dmodels.emplace_back();
        }

        if (entity != map.world_entity()) {
            if (entity == map.world_entity() + 1)
                logging::print(logging::flag::PROGRESS, "---- Internal Entities ----\n");

            std::string mod = fmt::format("*{}", entity->outputmodelnumber.value());

            if (options.fVerbose)
                PrintEntity(entity);

            if (hullnum <= 0)
                logging::print(logging::flag::STAT, "     MODEL: {}\n", mod);
            entity->epairs.set("model", mod);
        }
    }

    /*
     * Init the entity
     */
    entity->brushes.clear();
    entity->bounds = {};

    /*
     * Convert the map brushes (planes) into BSP brushes (polygons)
     */
    logging::print(logging::flag::PROGRESS, "---- Brush_LoadEntity ----\n");
    auto stats = Brush_LoadEntity(entity, hullnum);

    // we're discarding the brush
    if (discarded_trigger) {
        entity->brushes.clear();
        entity->epairs.set("mins", fmt::to_string(entity->bounds.mins()));
        entity->epairs.set("maxs", fmt::to_string(entity->bounds.maxs()));
        return;
    }

    /* Print brush counts */
    if (stats.solid) {
        logging::print(logging::flag::STAT, "     {:8} solid brushes\n", stats.solid);
    }
    if (stats.sky) {
        logging::print(logging::flag::STAT, "     {:8} sky brushes\n", stats.sky);
    }
    if (stats.detail) {
        logging::print(logging::flag::STAT, "     {:8} detail brushes\n", stats.detail);
    }
    if (stats.detail_illusionary) {
        logging::print(logging::flag::STAT, "     {:8} detail illusionary brushes\n", stats.detail_illusionary);
    }
    if (stats.detail_fence) {
        logging::print(logging::flag::STAT, "     {:8} detail fence brushes\n", stats.detail_fence);
    }
    if (stats.liquid) {
        logging::print(logging::flag::STAT, "     {:8} liquid brushes\n", stats.liquid);
    }

    logging::print(logging::flag::STAT, "     {:8} planes\n", map.planes.size());

    if (entity->brushes.empty() && hullnum && entity != map.world_entity()) {
        PrintEntity(entity);
        FError("Entity with no valid brushes");
    }

    /*
     * Take the brush_t's and clip off all overlapping and contained faces,
     * leaving a perfect skin of the model with no hidden faces
     */
    std::vector<surface_t> surfs = CSGFaces(entity);

    if (options.objexport.value() && entity == map.world_entity() && hullnum <= 0) {
        ExportObj_Surfaces("post_csg", surfs);
    }

    if (hullnum > 0) {
        nodes = SolidBSP(entity, surfs, true);
        if (entity == map.world_entity() && !options.nofill.value()) {
            // assume non-world bmodels are simple
            PortalizeWorld(entity, nodes, hullnum);
            if (!options.nofill.value() && FillOutside(nodes, hullnum)) {
                // Free portals before regenerating new nodes
                FreeAllPortals(nodes);
                surfs = GatherNodeFaces(nodes);
                // make a really good tree
                nodes = SolidBSP(entity, surfs, false);

                DetailToSolid(nodes);
            }
        }
        ExportClipNodes(entity, nodes, hullnum);
    } else {
        /*
         * SolidBSP generates a node tree
         *
         * if not the world, make a good tree first the world is just
         * going to make a bad tree because the outside filling will
         * force a regeneration later.
         *
         * Forcing the good tree for the first pass on the world can
         * sometimes result in reduced marksurfaces at the expense of
         * longer processing time.
         */
        if (options.forcegoodtree.value())
            nodes = SolidBSP(entity, surfs, false);
        else
            nodes = SolidBSP(entity, surfs, entity == map.world_entity());

        // build all the portals in the bsp tree
        // some portals are solid polygons, and some are paths to other leafs
        if (entity == map.world_entity()) {
            // assume non-world bmodels are simple
            PortalizeWorld(entity, nodes, hullnum);
            if (!options.nofill.value() && FillOutside(nodes, hullnum)) {
                FreeAllPortals(nodes);

                // get the remaining faces together into surfaces again
                surfs = GatherNodeFaces(nodes);

                // merge polygons
                MergeAll(surfs);

                // make a really good tree
                nodes = SolidBSP(entity, surfs, false);

                // convert detail leafs to solid
                DetailToSolid(nodes);

                // make the real portals for vis tracing
                PortalizeWorld(entity, nodes, hullnum);

                if (!options.notjunc.value()) {
                    TJunc(entity, nodes);
                }
            }

            // Area portals
            if (options.target_game->id == GAME_QUAKE_II) {
                FloodAreas(entity, nodes);
                EmitAreaPortals(nodes);
            }

            FreeAllPortals(nodes);
        }

        // bmodels
        if (entity != map.world_entity() && !options.notjunc.value()) {
            TJunc(entity, nodes);
        }

        // convert detail leafs to solid (in case we didn't make the call above)
        DetailToSolid(nodes);

        if (options.objexport.value() && entity == map.world_entity()) {
            ExportObj_Nodes("pre_makefaceedges_plane_faces", nodes);
            ExportObj_Marksurfaces("pre_makefaceedges_marksurfaces", nodes);
        }

        firstface = MakeFaceEdges(entity, nodes);

        if (options.target_game->id == GAME_QUAKE_II) {
            ExportBrushList(entity, nodes, map.brush_offset);
        }

        ExportDrawNodes(entity, nodes, firstface);
    }

    FreeBrushes(entity);
    // FreeNodes(nodes);
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

    if (!options.fAllverbose) {
        options.fVerbose = false;
        logging::mask &= ~(bitflags<logging::flag>(logging::flag::STAT) | logging::flag::PROGRESS);
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

static void BSPX_Brushes_AddModel(struct bspxbrushes_s *ctx, int modelnum, std::vector<brush_t> &brushes)
{
    bspxbrushes_permodel permodel{1, modelnum};

    for (auto &b : brushes) {
        permodel.numbrushes++;
        for (auto &f : b.faces) {
            /*skip axial*/
            if (fabs(map.planes[f.planenum].normal[0]) == 1 || fabs(map.planes[f.planenum].normal[1]) == 1 ||
                fabs(map.planes[f.planenum].normal[2]) == 1)
                continue;
            permodel.numfaces++;
        }
    }

    permodel.numbrushes = LittleLong(permodel.numbrushes);
    permodel.numfaces = LittleLong(permodel.numfaces);

    std::ostringstream str(std::ios_base::out | std::ios_base::binary);

    str << endianness<std::endian::little>;

    str <= permodel;

    for (auto &b : brushes) {
        bspxbrushes_perbrush perbrush{};

        for (auto &f : b.faces) {
            /*skip axial*/
            if (fabs(map.planes[f.planenum].normal[0]) == 1 || fabs(map.planes[f.planenum].normal[1]) == 1 ||
                fabs(map.planes[f.planenum].normal[2]) == 1)
                continue;
            perbrush.numfaces++;
        }

        perbrush.bounds = b.bounds;

        switch (b.contents.native) {
            // contents should match the engine.
            case CONTENTS_EMPTY: // really an error, but whatever
            case CONTENTS_SOLID: // these are okay
            case CONTENTS_WATER:
            case CONTENTS_SLIME:
            case CONTENTS_LAVA:
            case CONTENTS_SKY:
                if (b.contents.is_clip()) {
                    perbrush.contents = -8;
                } else {
                    perbrush.contents = b.contents.native;
                }
                break;
            //              case CONTENTS_LADDER:
            //                      perbrush.contents = -16;
            //                      break;
            default: {
                if (b.contents.is_clip()) {
                    perbrush.contents = -8;
                } else {
                    logging::print("WARNING: Unknown contents: {}. Translating to solid.\n",
                        b.contents.to_string(options.target_game));
                    perbrush.contents = CONTENTS_SOLID;
                }
                break;
            }
        }

        str <= perbrush;

        for (auto &f : b.faces) {
            /*skip axial*/
            if (fabs(map.planes[f.planenum].normal[0]) == 1 || fabs(map.planes[f.planenum].normal[1]) == 1 ||
                fabs(map.planes[f.planenum].normal[2]) == 1)
                continue;

            bspxbrushes_perface perface;

            if (f.planeside) {
                perface = -map.planes[f.planenum];
            } else {
                perface = map.planes[f.planenum];
            }

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

    if (!options.wrbrushes.value())
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
        if (!options.fAllverbose) {
            options.fVerbose = false; // don't print rest of entities
            logging::mask &= ~(bitflags<logging::flag>(logging::flag::STAT) | logging::flag::PROGRESS);
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
    if (!options.fNoverbose) {
        options.fVerbose = true;
        logging::mask |= (bitflags<logging::flag>(logging::flag::STAT) | logging::flag::PROGRESS);
    }

    auto &hulls = options.target_game->get_hull_sizes();

    // game has no hulls, so we have to export brush lists and stuff.
    if (!hulls.size()) {
        CreateSingleHull(HULL_COLLISION);
        // only create hull 0 if fNoclip is set
    } else if (options.noclip.value()) {
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
        auto pos = fs::where(map.miptex[i].name);

        if (!pos) {
            logging::print("WARNING: Texture {} not found\n", map.miptex[i].name);
            continue;
        }

        auto file = fs::load(pos);
        auto tex = img::load_mip(map.miptex[i].name, file, true, options.target_game);

        if (!tex) {
            logging::print("WARNING: unable to load texture {} in archive {}\n", map.miptex[i].name, pos.archive->pathname);
            continue;
        }

        auto &miptex = map.bsp.dtex.textures[i];
        miptex.name = map.miptex[i].name;
        miptex.width = tex->meta.width;
        miptex.height = tex->meta.height;

        if (!pos.archive->external) {
            miptex.data = std::move(file.value());
        } else {
            // construct fake data that solely contains the header.
            miptex.data.resize(sizeof(dmiptex_t));
            
            dmiptex_t header {};
            std::copy(miptex.name.begin(), miptex.name.end(), header.name.begin());
            header.width = miptex.width;
            header.height = miptex.height;
            header.offsets = { -1, -1, -1, -1 };

            omemstream stream(miptex.data.data(), miptex.data.size());
            stream <= header;
        }
    }
}

static void AddAnimationFrames()
{
    size_t oldcount = map.miptex.size();

    for (size_t i = 0; i < oldcount; i++) {
        const std::string &existing_name = map.miptexTextureName(i);

        if (existing_name[0] != '+' && (options.target_game->id != GAME_HALF_LIFE || existing_name[0] != '-')) {
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
    if (options.target_game->id == GAME_QUAKE_II) {
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

    if (options.convertmapformat.value() != conversion_t::none) {
        ConvertMapFile();
        return;
    }
    if (options.onlyents.value()) {
        UpdateEntLump();
        return;
    }

    // initialize secondary textures
    LoadSecondaryTextures();

    // init the tables to be shared by all models
    BeginBSPFile();

    if (!options.fAllverbose) {
        options.fVerbose = false;
        logging::mask &= ~(bitflags<logging::flag>(logging::flag::STAT) | logging::flag::PROGRESS);
    }

    // calculate extents, if required
    if (!options.worldextent.value()) {
        CalculateWorldExtent();
    }

    // create hulls!
    CreateHulls();

    WriteEntitiesToString();
    BSPX_CreateBrushList();
    FinishBSPFile();
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
    options.reset();
    // fixme-brushbsp: clear any other members of qbsp_settings
    options.target_game = nullptr;
    options.target_version = nullptr;

    options.run(argc, argv);

    options.map_path.replace_extension("map");

    // The .map extension gets removed right away anyways...
    if (options.bsp_path.empty())
        options.bsp_path = options.map_path;

    /* Start logging to <bspname>.log */
    logging::init(fs::path(options.bsp_path).replace_extension("log"), options);

    // Remove already existing files
    if (!options.onlyents.value() && options.convertmapformat.value() == conversion_t::none) {
        options.bsp_path.replace_extension("bsp");
        remove(options.bsp_path);

        // Probably not the best place to do this
        logging::print("Input file: {}\n", options.map_path);
        logging::print("Output file: {}\n\n", options.bsp_path);

        fs::path prtfile = options.bsp_path;
        prtfile.replace_extension("prt");
        remove(prtfile);

        fs::path ptsfile = options.bsp_path;
        ptsfile.replace_extension("pts");
        remove(ptsfile);

        fs::path porfile = options.bsp_path;
        porfile.replace_extension("por");
        remove(porfile);
    }

    // onlyents might not load this yet
    if (options.target_game) {
        options.target_game->init_filesystem(options.map_path, options);
    }
}

void InitQBSP(const std::vector<std::string>& args)
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
