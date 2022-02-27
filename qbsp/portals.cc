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
// portals.c

#include <fstream>
#include <fmt/ostream.h>
#include <qbsp/qbsp.hh>

node_t outside_node; // portals outside the world face this

class portal_state_t
{
public:
    int num_visportals;
    int num_visleafs; // leafs the player can be in
    int num_visclusters; // clusters of leafs
    int iNodesDone;
    bool uses_detail;
};

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

contentflags_t ClusterContents(const node_t *node)
{
    /* Pass the leaf contents up the stack */
    if (node->planenum == PLANENUM_LEAF)
        return node->contents;

    return options.target_game->cluster_contents(
        ClusterContents(node->children[0]), ClusterContents(node->children[1]));
}

/* Return true if possible to see the through the contents of the portals nodes */
static bool PortalThru(const portal_t *p)
{
    contentflags_t contents0 = ClusterContents(p->nodes[0]);
    contentflags_t contents1 = ClusterContents(p->nodes[1]);

    /* Can't see through func_illusionary_visblocker */
    if ((contents0.extended | contents1.extended) & CFLAGS_ILLUSIONARY_VISBLOCKER)
        return false;

    // FIXME: we can't move this directly to portal_can_see_through because
    // "options" isn't exposed there.
    if (options.target_game->id != GAME_QUAKE_II) {
        /* If water is transparent, liquids are like empty space */
        if (options.transwater.value()) {
            if (contents0.is_liquid(options.target_game) && contents1.is_empty(options.target_game))
                return true;
            if (contents1.is_liquid(options.target_game) && contents0.is_empty(options.target_game))
                return true;
        }

        /* If sky is transparent, then sky is like empty space */
        if (options.transsky.value()) {
            if (contents0.is_sky(options.target_game) && contents1.is_empty(options.target_game))
                return true;
            if (contents0.is_empty(options.target_game) && contents1.is_sky(options.target_game))
                return true;
        }
    }

    // Check per-game visibility
    return options.target_game->portal_can_see_through(contents0, contents1);
}

static void WritePortals_r(node_t *node, std::ofstream &portalFile, bool clusters)
{
    const portal_t *p, *next;
    std::optional<winding_t> w;
    const qbsp_plane_t *pl;
    int i, front, back;
    qplane3d plane2;

    if (node->planenum != PLANENUM_LEAF && !node->detail_separator) {
        WritePortals_r(node->children[0], portalFile, clusters);
        WritePortals_r(node->children[1], portalFile, clusters);
        return;
    }
    if (node->contents.is_solid(options.target_game))
        return;

    for (p = node->portals; p; p = next) {
        next = (p->nodes[0] == node) ? p->next[0] : p->next[1];
        if (!p->winding || p->nodes[0] != node)
            continue;
        if (!PortalThru(p))
            continue;

        w = p->winding;
        front = clusters ? p->nodes[0]->viscluster : p->nodes[0]->visleafnum;
        back = clusters ? p->nodes[1]->viscluster : p->nodes[1]->visleafnum;

        Q_assert(front != -1);
        Q_assert(back != -1);

        /*
         * sometimes planes get turned around when they are very near the
         * changeover point between different axis.  interpret the plane the
         * same way vis will, and flip the side orders if needed
         */
        pl = &map.planes[p->planenum];
        plane2 = w->plane();
        if (qv::dot(pl->normal, plane2.normal) < 1.0 - ANGLEEPSILON)
            fmt::print(portalFile, "{} {} {} ", w->size(), back, front);
        else
            fmt::print(portalFile, "{} {} {} ", w->size(), front, back);

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
    if (node->planenum != PLANENUM_LEAF) {
        viscluster = WriteClusters_r(node->children[0], portalFile, viscluster);
        viscluster = WriteClusters_r(node->children[1], portalFile, viscluster);
        return viscluster;
    }
    if (node->contents.is_solid(options.target_game))
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

static void CountPortals(const node_t *node, portal_state_t *state)
{
    const portal_t *portal;

    for (portal = node->portals; portal;) {
        /* only write out from first leaf */
        if (portal->nodes[0] == node) {
            if (PortalThru(portal))
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
    if (node->planenum != PLANENUM_LEAF) {
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

    if (node->contents.is_solid(options.target_game)) {
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
    options.szBSPName.replace_extension("prt");

    std::ofstream portalFile(options.szBSPName, std::ios_base::binary | std::ios_base::out);
    if (!portalFile)
        FError("Failed to open {}: {}", options.szBSPName, strerror(errno));

    // q2 uses a PRT1 file, but with clusters.
    // (Since q2bsp natively supports clusters, we don't need PRT2.)
    if (options.target_game->id == GAME_QUAKE_II) {
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
        if (options.forceprt1.value()) {
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

//=============================================================================

/*
=============
AddPortalToNodes
=============
*/
static void AddPortalToNodes(portal_t *p, node_t *front, node_t *back)
{
    if (p->nodes[0] || p->nodes[1])
        FError("portal already included");

    p->nodes[0] = front;
    p->next[0] = front->portals;
    front->portals = p;

    p->nodes[1] = back;
    p->next[1] = back->portals;
    back->portals = p;
}

/*
=============
RemovePortalFromNode
=============
*/
static void RemovePortalFromNode(portal_t *portal, node_t *l)
{
    portal_t **pp, *t;

    // remove reference to the current portal
    pp = &l->portals;
    while (1) {
        t = *pp;
        if (!t)
            FError("Portal not in leaf");

        if (t == portal)
            break;

        if (t->nodes[0] == l)
            pp = &t->next[0];
        else if (t->nodes[1] == l)
            pp = &t->next[1];
        else
            FError("Portal not bounding leaf");
    }

    if (portal->nodes[0] == l) {
        *pp = portal->next[0];
        portal->nodes[0] = NULL;
    } else if (portal->nodes[1] == l) {
        *pp = portal->next[1];
        portal->nodes[1] = NULL;
    }
}

/*
================
MakeHeadnodePortals

The created portals will face the global outside_node
================
*/
static void MakeHeadnodePortals(const mapentity_t *entity, node_t *node)
{
    int i, j, n;
    portal_t *p, *portals[6];
    qbsp_plane_t bplanes[6];
    int side;

    // pad with some space so there will never be null volume leafs
    aabb3d bounds = entity->bounds.grow(SIDESPACE);

    outside_node.planenum = PLANENUM_LEAF;
    outside_node.contents = options.target_game->create_solid_contents();
    outside_node.portals = NULL;

    // create 6 portals forming a cube around the bounds of the map.
    // these portals will have `outside_node` on one side, and headnode on the other.
    for (i = 0; i < 3; i++)
        for (j = 0; j < 2; j++) {
            n = j * 3 + i;

            p = new portal_t{};
            portals[n] = p;

            qplane3d &pl = bplanes[n] = {};

            if (j) {
                pl.normal[i] = -1;
                pl.dist = -bounds[j][i];
            } else {
                pl.normal[i] = 1;
                pl.dist = bounds[j][i];
            }
            p->planenum = FindPlane(pl, &side);

            p->winding = BaseWindingForPlane(pl);
            if (side)
                AddPortalToNodes(p, &outside_node, node);
            else
                AddPortalToNodes(p, node, &outside_node);
        }

    // clip the basewindings by all the other planes
    for (i = 0; i < 6; i++) {
        for (j = 0; j < 6; j++) {
            if (j == i)
                continue;
            portals[i]->winding = portals[i]->winding->clip(bplanes[j], ON_EPSILON, true)[SIDE_FRONT];
        }
    }
}

//============================================================================

#ifdef PARANOID

static void CheckWindingInNode(winding_t *w, node_t *node)
{
    int i, j;

    for (i = 0; i < w->numpoints; i++) {
        for (j = 0; j < 3; j++)
            if (w->points[i][j] < node->mins[j] - 1 || w->points[i][j] > node->maxs[j] + 1) {
                LogPrint("WARNING: Winding outside node\n");
                return;
            }
    }
}

static void CheckWindingArea(winding_t *w)
{
    int i;
    vec_t total, add;
    qvec3d v1, v2, cross;

    total = 0;
    for (i = 1; i < w->numpoints; i++) {
        v1 = w->points[i] - w->points[0];
        v2 = w->points[i + 1] - w->points[0];
        cross = qv::cross(v1, v2);
        add = qv::length(cross);
        total += add * 0.5;
    }
    if (total < 16)
        LogPrint("WARNING: Winding with area {}\n", total);
}

static void CheckLeafPortalConsistancy(node_t *node)
{
    int side, side2;
    portal_t *p, *p2;
    qbsp_plane_t plane, plane2;
    int i;
    winding_t *w;
    vec_t dist;

    side = side2 = 0; // quiet compiler warning

    for (p = node->portals; p; p = p->next[side]) {
        if (p->nodes[0] == node)
            side = 0;
        else if (p->nodes[1] == node)
            side = 1;
        else
            FError("Mislinked portal");

        CheckWindingInNode(p->winding, node);
        CheckWindingArea(p->winding);

        // check that the side orders are correct
        plane = map.planes[p->planenum];
        plane2 = p->winding.plane();

        for (p2 = node->portals; p2; p2 = p2->next[side2]) {
            if (p2->nodes[0] == node)
                side2 = 0;
            else if (p2->nodes[1] == node)
                side2 = 1;
            else
                FError("Mislinked portal");

            w = p2->winding;
            for (i = 0; i < w->numpoints; i++) {
                dist = plane.distance_to(w->points[i]);
                if ((side == 0 && dist < -1) || (side == 1 && dist > 1)) {
                    LogPrint("WARNING: Portal siding direction is wrong\n");
                    return;
                }
            }
        }
    }
}

#endif /* PARANOID */

//============================================================================

/*
================
CutNodePortals_r
================
*/
static void CutNodePortals_r(node_t *node, portal_state_t *state)
{
    qbsp_plane_t clipplane;
    node_t *front, *back, *other_node;
    portal_t *portal, *new_portal, *next_portal;
    int side;

#ifdef PARANOID
    CheckLeafPortalConsistancy(node);
#endif

    /* If a leaf, no more dividing */
    if (node->planenum == PLANENUM_LEAF)
        return;

    /* No portals on detail separators */
    if (node->detail_separator)
        return;

    const qbsp_plane_t &plane = map.planes[node->planenum];
    front = node->children[SIDE_FRONT];
    back = node->children[SIDE_BACK];

    /*
     * create the new portal by taking the full plane winding for the cutting
     * plane and clipping it by all of the planes from the other portals
     */
    new_portal = new portal_t{};
    new_portal->planenum = node->planenum;

    std::optional<winding_t> winding = BaseWindingForPlane(plane);
    for (portal = node->portals; portal; portal = portal->next[side]) {
        clipplane = map.planes[portal->planenum];
        if (portal->nodes[0] == node)
            side = SIDE_FRONT;
        else if (portal->nodes[1] == node) {
            clipplane = -clipplane;
            side = SIDE_BACK;
        } else
            FError("Mislinked portal");

        winding = winding->clip(clipplane, ON_EPSILON, true)[SIDE_FRONT];
        if (!winding) {
            FLogPrint("WARNING: New portal was clipped away near ({:.3} {:.3} {:.3})\n", portal->winding->at(0)[0],
                portal->winding->at(0)[1], portal->winding->at(0)[2]);
            break;
        }
    }

    /* If the plane was not clipped on all sides, there was an error */
    if (winding) {
        new_portal->winding = winding;
        AddPortalToNodes(new_portal, front, back);
    }

    /* partition the portals */
    for (portal = node->portals; portal; portal = next_portal) {
        if (portal->nodes[0] == node)
            side = SIDE_FRONT;
        else if (portal->nodes[1] == node)
            side = SIDE_BACK;
        else
            FError("Mislinked portal");
        next_portal = portal->next[side];

        other_node = portal->nodes[!side];
        RemovePortalFromNode(portal, portal->nodes[0]);
        RemovePortalFromNode(portal, portal->nodes[1]);

        /* cut the portal into two portals, one on each side of the cut plane */
        auto windings = portal->winding->clip(plane, ON_EPSILON);

        if (!windings[SIDE_FRONT]) {
            if (side == SIDE_FRONT)
                AddPortalToNodes(portal, back, other_node);
            else
                AddPortalToNodes(portal, other_node, back);
            continue;
        }
        if (!windings[SIDE_BACK]) {
            if (side == SIDE_FRONT)
                AddPortalToNodes(portal, front, other_node);
            else
                AddPortalToNodes(portal, other_node, front);
            continue;
        }

        /* the winding is split */
        new_portal = new portal_t{};
        *new_portal = *portal;
        new_portal->winding = std::move(windings[SIDE_BACK]);
        portal->winding = std::move(windings[SIDE_FRONT]);

        if (side == SIDE_FRONT) {
            AddPortalToNodes(portal, front, other_node);
            AddPortalToNodes(new_portal, back, other_node);
        } else {
            AddPortalToNodes(portal, other_node, front);
            AddPortalToNodes(new_portal, other_node, back);
        }
    }

    /* Display progress */
    state->iNodesDone++;
    LogPercent(state->iNodesDone, splitnodes.load());

    CutNodePortals_r(front, state);
    CutNodePortals_r(back, state);
}

/*
==================
PortalizeWorld

Builds the exact polyhedrons for the nodes and leafs
==================
*/
void PortalizeWorld(const mapentity_t *entity, node_t *headnode, const int hullnum)
{
    LogPrint(LOG_PROGRESS, "---- {} ----\n", __func__);

    portal_state_t state{};

    state.iNodesDone = 0;

    MakeHeadnodePortals(entity, headnode);
    CutNodePortals_r(headnode, &state);

    if (hullnum <= 0) {
        /* save portal file for vis tracing */
        WritePortalfile(headnode, &state);

        LogPrint(LOG_STAT, "     {:8} vis leafs\n", state.num_visleafs);
        LogPrint(LOG_STAT, "     {:8} vis clusters\n", state.num_visclusters);
        LogPrint(LOG_STAT, "     {:8} vis portals\n", state.num_visportals);
    }
}

/*
==================
FreeAllPortals

==================
*/
void FreeAllPortals(node_t *node)
{
    portal_t *p, *nextp;

    if (node->planenum != PLANENUM_LEAF) {
        FreeAllPortals(node->children[0]);
        FreeAllPortals(node->children[1]);
    }

    for (p = node->portals; p; p = nextp) {
        if (p->nodes[0] == node)
            nextp = p->next[0];
        else
            nextp = p->next[1];
        RemovePortalFromNode(p, p->nodes[0]);
        RemovePortalFromNode(p, p->nodes[1]);
        delete p;
    }
    node->portals = NULL;
}
