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

#include <qbsp/prtfile.hh>

#include <common/log.hh>
#include <common/ostream.hh>
#include <common/prtfile.hh>
#include <qbsp/map.hh>
#include <qbsp/portals.hh>
#include <qbsp/qbsp.hh>
#include <qbsp/tree.hh>

#include <fstream>

/*
==============================================================================

PORTAL FILE GENERATION

==============================================================================
*/

static void WritePortals_r(node_t *node, prtfile_t &portalFile, bool clusters)
{
    const portal_t *p, *next;
    const winding_t *w;
    int front, back;
    qplane3d plane2;

    if (auto *nodedata = node->get_nodedata(); nodedata && !nodedata->detail_separator) {
        WritePortals_r(nodedata->children[0], portalFile, clusters);
        WritePortals_r(nodedata->children[1], portalFile, clusters);
        return;
    }
    // at this point, `node` may be a leaf or a cluster
    if (auto *leafdata = node->get_leafdata(); leafdata && leafdata->contents.is_any_solid())
        return;

    for (p = node->portals; p; p = next) {
        next = (p->nodes[0] == node) ? p->next[0] : p->next[1];
        if (!p->winding || p->nodes[0] != node)
            continue;
        if (!Portal_VisFlood(p))
            continue;

        w = &p->winding;
        front = clusters ? p->nodes[0]->viscluster : p->nodes[0]->visleafnum;
        back = clusters ? p->nodes[1]->viscluster : p->nodes[1]->visleafnum;

        if (front == -1 || back == -1) {
            auto front_contents = ClusterContents(p->nodes.front);
            auto back_contents = ClusterContents(p->nodes.back);

            FError("front {}, cluster contents: {}. back {}, cluster contents: {}. portal: {}", front,
                front_contents.to_string(), back, back_contents.to_string(), w->center());
        }

        prtfile_portal_t result_portal;

        /*
         * sometimes planes get turned around when they are very near the
         * changeover point between different axis.  interpret the plane the
         * same way vis will, and flip the side orders if needed
         */
        plane2 = w->plane();
        if (qv::dot(p->plane.get_normal(), plane2.normal) < 1.0 - ANGLEEPSILON) {
            result_portal.leafnums[0] = back;
            result_portal.leafnums[1] = front;
        } else {
            result_portal.leafnums[0] = front;
            result_portal.leafnums[1] = back;
        }

        result_portal.winding = w->clone<prtfile_winding_storage_t>();

        // push
        portalFile.portals.push_back(std::move(result_portal));
    }
}

static void WritePTR2ClusterMapping_r(node_t *node, prtfile_t &portalFile)
{
    if (auto *nodedata = node->get_nodedata()) {
        WritePTR2ClusterMapping_r(nodedata->children[0], portalFile);
        WritePTR2ClusterMapping_r(nodedata->children[1], portalFile);
        return;
    }

    auto *leafdata = node->get_leafdata();
    if (leafdata->contents.is_any_solid())
        return;

    portalFile.dleafinfos[node->visleafnum + 1].cluster = node->viscluster;
}

struct portal_state_t : logging::stat_tracker_t
{
    stat &num_visleafs = register_stat("player-occupiable leaves");
    stat &num_visclusters = register_stat("clusters of leaves");
    stat &num_visportals = register_stat("vis portals");
    bool uses_detail;
};

static void CountPortals(const node_t *node, portal_state_t &state)
{
    const portal_t *portal;

    for (portal = node->portals; portal;) {
        /* only write out from first leaf */
        if (portal->nodes[0] == node) {
            if (Portal_VisFlood(portal)) {
                state.num_visportals++;
            }
            portal = portal->next[0];
        } else {
            portal = portal->next[1];
        }
    }
}

/*
================
NumberLeafs_r
- Assigns leaf numbers and cluster numbers
- If cluster < 0, assign next available global cluster number and increment
- Otherwise, assign the given cluster number because parent splitter is detail
================
*/
static void NumberLeafs_r(node_t *node, portal_state_t &state, int cluster)
{
    /* decision node */
    if (auto *nodedata = node->get_nodedata()) {
        if (cluster < 0 && nodedata->detail_separator) {
            state.uses_detail = true;
            cluster = state.num_visclusters++;
            node->viscluster = cluster;
            CountPortals(node, state);
        }
        NumberLeafs_r(nodedata->children[0], state, cluster);
        NumberLeafs_r(nodedata->children[1], state, cluster);
        return;
    }

    if (auto *leafdata = node->get_leafdata(); leafdata && leafdata->contents.is_any_solid()) {
        /* solid block, viewpoint never inside */
        node->visleafnum = -1;
        node->viscluster = -1;
        return;
    }

    node->visleafnum = state.num_visleafs++;
    node->viscluster = (cluster < 0) ? state.num_visclusters++ : cluster;
    CountPortals(node, state);
}

/*
================
WritePortalfile
================
*/
static void WritePortalfile(node_t *headnode, portal_state_t &state)
{
    /*
     * Set the visleafnum and viscluster field in every leaf and count the
     * total number of portals.
     */
    NumberLeafs_r(headnode, state, -1);

    // write the file
    fs::path name = qbsp_options.bsp_path;
    name.replace_extension("prt");

    prtfile_t portalFile{};

    // q2 uses a PRT1 file, but with clusters.
    // (Since q2bsp natively supports clusters, we don't need PRT2.)
    if (qbsp_options.target_game->id == GAME_QUAKE_II) {
        portalFile.portalleafs = state.num_visclusters.count.load();
        WritePortals_r(headnode, portalFile, true);
    } else if (!state.uses_detail) {
        /* If no detail clusters, just use a normal PRT1 format */
        portalFile.portalleafs = state.num_visleafs.count.load();
        WritePortals_r(headnode, portalFile, false);
    } else if (qbsp_options.forceprt1.value()) {
        /* Write a PRT1 file for loading in the map editor. Vis will reject it. */
        portalFile.portalleafs = state.num_visclusters.count.load();
        WritePortals_r(headnode, portalFile, true);
    } else {
        /* Write a PRT2 */
        portalFile.portalleafs_real = state.num_visleafs.count.load();
        portalFile.portalleafs = state.num_visclusters.count.load();
        WritePortals_r(headnode, portalFile, true);
        portalFile.dleafinfos.resize(state.num_visleafs.count.load() + 1);
        WritePTR2ClusterMapping_r(headnode, portalFile);
    }

    WritePortalfile(name, portalFile, qbsp_options.target_version, state.uses_detail, qbsp_options.forceprt1.value());
}

/*
==================
WritePortalFile
==================
*/
void WritePortalFile(tree_t &tree)
{
    logging::funcheader();

    FreeTreePortals(tree);

    MakeHeadnodePortals(tree);

    {
        logging::percent_clock clock;

        // vis portal generation doesn't use headnode portals
        portalstats_t stats{};
        auto buildportals = MakeTreePortals_r(tree.headnode, portaltype_t::VIS, {}, stats, clock);

        MakePortalsFromBuildportals(tree, buildportals);

        tree.portaltype = portaltype_t::VIS;
    }

    portal_state_t state{};

    /* save portal file for vis tracing */
    WritePortalfile(tree.headnode, state);
}

/*
==============================================================================

DEBUG PORTAL FILE GENERATION

==============================================================================
*/

static void WriteDebugPortal(const portal_t *p, std::ofstream &portalFile)
{
    const winding_t *w = &p->winding;

    ewt::print(portalFile, "{} {} {} ", w->size(), 0, 0);

    for (int i = 0; i < w->size(); i++) {
        ewt::print(portalFile, "({} {} {}) ", w->at(i)[0], w->at(i)[1], w->at(i)[2]);
    }
    ewt::print(portalFile, "\n");
}

static void WriteTreePortals_r(node_t *node, std::ofstream &portalFile)
{
    if (auto *nodedata = node->get_nodedata()) {
        WriteTreePortals_r(nodedata->children[0], portalFile);
        WriteTreePortals_r(nodedata->children[1], portalFile);
        return;
    }

    const portal_t *p, *next;
    for (p = node->portals; p; p = next) {
        next = (p->nodes[0] == node) ? p->next[0] : p->next[1];
        if (!p->winding || p->nodes[0] != node)
            continue;

        WriteDebugPortal(p, portalFile);
    }
}

static void CountTreePortals_r(node_t *node, size_t &count)
{
    if (auto *nodedata = node->get_nodedata()) {
        CountTreePortals_r(nodedata->children[0], count);
        CountTreePortals_r(nodedata->children[1], count);
        return;
    }

    const portal_t *p, *next;
    for (p = node->portals; p; p = next) {
        next = (p->nodes[0] == node) ? p->next[0] : p->next[1];
        if (!p->winding || p->nodes[0] != node)
            continue;

        ++count;
    }
}

/*
==================
WritePortalFile
==================
*/
void WriteDebugTreePortalFile(tree_t &tree, std::string_view filename_suffix)
{
    logging::funcheader();

    size_t portal_count = 0;
    CountTreePortals_r(tree.headnode, portal_count);

    // write the file
    fs::path name = qbsp_options.bsp_path;
    name.replace_extension(std::string(filename_suffix) + ".prt");

    std::ofstream portalFile(name, std::ios_base::out); // .prt files are intentionally text mode
    if (!portalFile)
        FError("Failed to open {}: {}", name, strerror(errno));

    ewt::print(portalFile, "PRT1\n");
    ewt::print(portalFile, "{}\n", 0);
    ewt::print(portalFile, "{}\n", portal_count);
    WriteTreePortals_r(tree.headnode, portalFile);

    logging::print(logging::flag::STAT, "     {:8} tree portals written to {}\n", portal_count, name);
}

void WriteDebugPortals(std::vector<portal_t *> portals, std::string_view filename_suffix)
{
    logging::funcheader();

    // count how many are nonemtpy
    size_t portal_count = 0;
    for (auto &p : portals) {
        if (p->winding) {
            ++portal_count;
        }
    }

    // write the file
    fs::path name = qbsp_options.bsp_path;
    name.replace_extension(std::string(filename_suffix) + ".prt");

    std::ofstream portal_file(name, std::ios_base::out); // .prt files are intentionally text mode
    if (!portal_file)
        FError("Failed to open {}: {}", name, strerror(errno));

    ewt::print(portal_file, "PRT1\n");
    ewt::print(portal_file, "{}\n", 0);
    ewt::print(portal_file, "{}\n", portal_count);
    for (auto &p : portals) {
        if (p->winding) {
            WriteDebugPortal(p, portal_file);
        }
    }

    logging::print(logging::flag::STAT, "     {:8} portals written to {}\n", portal_count, name);
}
