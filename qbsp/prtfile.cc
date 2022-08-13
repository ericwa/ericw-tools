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

#include <qbsp/map.hh>
#include <qbsp/portals.hh>
#include <qbsp/qbsp.hh>
#include <qbsp/tree.hh>

#include <fstream>
#include <fmt/ostream.h>

#include "tbb/task_group.h"

/*
==============================================================================

PORTAL FILE GENERATION

==============================================================================
*/

static void WriteFloat(std::ofstream &portalFile, vec_t v)
{
    if (fabs(v - Q_rint(v)) < ZERO_EPSILON)
        fmt::print(portalFile, "{} ", (int)Q_rint(v));
    else
        fmt::print(portalFile, "{} ", v);
}

static void WritePortals_r(node_t *node, std::ofstream &portalFile, bool clusters)
{
    const portal_t *p, *next;
    const winding_t* w;
    int i, front, back;
    qplane3d plane2;

    if (!node->is_leaf && !node->detail_separator) {
        WritePortals_r(node->children[0], portalFile, clusters);
        WritePortals_r(node->children[1], portalFile, clusters);
        return;
    }
    if (node->contents.is_solid(qbsp_options.target_game))
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

        Q_assert(front != -1);
        Q_assert(back != -1);

        /*
         * sometimes planes get turned around when they are very near the
         * changeover point between different axis.  interpret the plane the
         * same way vis will, and flip the side orders if needed
         */
        plane2 = w->plane();
        if (qv::dot(p->plane.get_normal(), plane2.normal) < 1.0 - ANGLEEPSILON) {
            fmt::print(portalFile, "{} {} {} ", w->size(), back, front);
        } else {
            fmt::print(portalFile, "{} {} {} ", w->size(), front, back);
        }

        for (i = 0; i < w->size(); i++) {
            fmt::print(portalFile, "(");
            WriteFloat(portalFile, w->at(i)[0]);
            WriteFloat(portalFile, w->at(i)[1]);
            WriteFloat(portalFile, w->at(i)[2]);
            fmt::print(portalFile, ") ");
        }
        fmt::print(portalFile, "\n");
    }
}

static int WriteClusters_r(node_t *node, std::ofstream &portalFile, int viscluster)
{
    if (!node->is_leaf) {
        viscluster = WriteClusters_r(node->children[0], portalFile, viscluster);
        viscluster = WriteClusters_r(node->children[1], portalFile, viscluster);
        return viscluster;
    }
    if (node->contents.is_solid(qbsp_options.target_game))
        return viscluster;

    /* If we're in the next cluster, start a new line */
    if (node->viscluster != viscluster) {
        fmt::print(portalFile, "-1\n");
        viscluster++;
    }

    /* Sanity check */
    if (node->viscluster != viscluster)
        FError("Internal error: Detail cluster mismatch");

    fmt::print(portalFile, "{} ", node->visleafnum);

    return viscluster;
}

struct portal_state_t
{
    int num_visportals;
    int num_visleafs; // leafs the player can be in
    int num_visclusters; // clusters of leafs
    bool uses_detail;
};

static void CountPortals(const node_t *node, portal_state_t *state)
{
    const portal_t *portal;

    for (portal = node->portals; portal;) {
        /* only write out from first leaf */
        if (portal->nodes[0] == node) {
            if (Portal_VisFlood(portal))
                state->num_visportals++;
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
static void NumberLeafs_r(node_t *node, portal_state_t *state, int cluster)
{
    /* decision node */
    if (!node->is_leaf) {
        node->visleafnum = -99;
        node->viscluster = -99;
        if (cluster < 0 && node->detail_separator) {
            state->uses_detail = true;
            cluster = state->num_visclusters++;
            node->viscluster = cluster;
            CountPortals(node, state);
        }
        NumberLeafs_r(node->children[0], state, cluster);
        NumberLeafs_r(node->children[1], state, cluster);
        return;
    }

    if (node->contents.is_solid(qbsp_options.target_game)) {
        /* solid block, viewpoint never inside */
        node->visleafnum = -1;
        node->viscluster = -1;
        return;
    }

    node->visleafnum = state->num_visleafs++;
    node->viscluster = (cluster < 0) ? state->num_visclusters++ : cluster;
    CountPortals(node, state);
}

/*
================
WritePortalfile
================
*/
static void WritePortalfile(node_t *headnode, portal_state_t *state)
{
    int check;

    /*
     * Set the visleafnum and viscluster field in every leaf and count the
     * total number of portals.
     */
    state->num_visleafs = 0;
    state->num_visclusters = 0;
    state->num_visportals = 0;
    state->uses_detail = false;
    NumberLeafs_r(headnode, state, -1);

    // write the file
    fs::path name = qbsp_options.bsp_path;
    name.replace_extension("prt");

    std::ofstream portalFile(name, std::ios_base::binary | std::ios_base::out);
    if (!portalFile)
        FError("Failed to open {}: {}", name, strerror(errno));

    // q2 uses a PRT1 file, but with clusters.
    // (Since q2bsp natively supports clusters, we don't need PRT2.)
    if (qbsp_options.target_game->id == GAME_QUAKE_II) {
        fmt::print(portalFile, "PRT1\n");
        fmt::print(portalFile, "{}\n", state->num_visclusters);
        fmt::print(portalFile, "{}\n", state->num_visportals);
        WritePortals_r(headnode, portalFile, true);
        return;
    }

    /* If no detail clusters, just use a normal PRT1 format */
    if (!state->uses_detail) {
        fmt::print(portalFile, "PRT1\n");
        fmt::print(portalFile, "{}\n", state->num_visleafs);
        fmt::print(portalFile, "{}\n", state->num_visportals);
        WritePortals_r(headnode, portalFile, false);
    } else {
        if (qbsp_options.forceprt1.value()) {
            /* Write a PRT1 file for loading in the map editor. Vis will reject it. */
            fmt::print(portalFile, "PRT1\n");
            fmt::print(portalFile, "{}\n", state->num_visclusters);
            fmt::print(portalFile, "{}\n", state->num_visportals);
            WritePortals_r(headnode, portalFile, true);
        } else {
            /* Write a PRT2 */
            fmt::print(portalFile, "PRT2\n");
            fmt::print(portalFile, "{}\n", state->num_visleafs);
            fmt::print(portalFile, "{}\n", state->num_visclusters);
            fmt::print(portalFile, "{}\n", state->num_visportals);
            WritePortals_r(headnode, portalFile, true);
            check = WriteClusters_r(headnode, portalFile, 0);
            if (check != state->num_visclusters - 1)
                FError("Internal error: Detail cluster mismatch");
            fmt::print(portalFile, "-1\n");
        }
    }
}

/*
==================
WritePortalFile
==================
*/
void WritePortalFile(tree_t *tree)
{
    logging::funcheader();

    portal_state_t state{};

    FreeTreePortals(tree);

    MakeHeadnodePortals(tree);
    
    {
        logging::percent_clock clock;

        // vis portal generation doesn't use headnode portals
        portalstats_t stats{};
        auto buildportals = MakeTreePortals_r(tree, tree->headnode, portaltype_t::VIS, {}, stats, clock);

        MakePortalsFromBuildportals(tree, buildportals);
    }

    /* save portal file for vis tracing */
    WritePortalfile(tree->headnode, &state);

    logging::print(logging::flag::STAT, "     {:8} vis leafs\n", state.num_visleafs);
    logging::print(logging::flag::STAT, "     {:8} vis clusters\n", state.num_visclusters);
    logging::print(logging::flag::STAT, "     {:8} vis portals\n", state.num_visportals);
}
