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

#include <qbsp/brush.hh>
#include <qbsp/portals.hh>

#include <qbsp/map.hh>
#include <qbsp/solidbsp.hh>
#include <qbsp/qbsp.hh>

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
void MakeHeadnodePortals(tree_t *tree)
{
    int i, j, n;
    portal_t *p, *portals[6];
    qbsp_plane_t bplanes[6];
    side_t side;

    // pad with some space so there will never be null volume leafs
    aabb3d bounds = tree->bounds.grow(SIDESPACE);

    tree->outside_node.planenum = PLANENUM_LEAF;
    tree->outside_node.contents = options.target_game->create_solid_contents();
    tree->outside_node.portals = NULL;

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
                AddPortalToNodes(p, &tree->outside_node, tree->headnode);
            else
                AddPortalToNodes(p, tree->headnode, &tree->outside_node);
        }

    // clip the basewindings by all the other planes
    for (i = 0; i < 6; i++) {
        for (j = 0; j < 6; j++) {
            if (j == i)
                continue;
            portals[i]->winding = portals[i]->winding->clip(bplanes[j], options.epsilon.value(), true)[SIDE_FRONT];
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
                logging::print("WARNING: Winding outside node\n");
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
        logging::print("WARNING: Winding with area {}\n", total);
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
                    logging::print("WARNING: Portal siding direction is wrong\n");
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
BaseWindingForNode

Creates a winding from the given node plane, clipped by all parent nodes.
================
*/
#define	BASE_WINDING_EPSILON	0.001
#define	SPLIT_WINDING_EPSILON	0.001

std::optional<winding_t> BaseWindingForNode(node_t *node)
{
    auto plane = map.planes.at(node->planenum);

    std::optional<winding_t> w = BaseWindingForPlane(plane);

    // clip by all the parents
    for (node_t *np = node->parent; np && w; )
    {
        plane = map.planes.at(np->planenum);

        const side_t keep = (np->children[0] == node) ?
            SIDE_FRONT : SIDE_BACK;

        w = w->clip(plane, BASE_WINDING_EPSILON, false)[keep];

        node = np;
        np = np->parent;
    }

    return w;
}

/*
================
CutNodePortals_r
================
*/
void CutNodePortals_r(node_t *node, portal_state_t *state)
{
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

    const qbsp_plane_t &plane = map.planes.at(node->planenum);
    front = node->children[SIDE_FRONT];
    back = node->children[SIDE_BACK];

    /*
     * create the new portal by taking the full plane winding for the cutting
     * plane and clipping it by all of the planes from the other portals
     */
    new_portal = new portal_t{};
    new_portal->planenum = node->planenum;
    new_portal->onnode = node;

    std::optional<winding_t> winding = BaseWindingForPlane(plane);
    for (portal = node->portals; portal; portal = portal->next[side]) {
        const auto &clipplane = map.planes.at(portal->planenum);
        if (portal->nodes[0] == node)
            side = SIDE_FRONT;
        else if (portal->nodes[1] == node) {
            side = SIDE_BACK;
        } else
            FError("Mislinked portal");

        winding = winding->clip(side == SIDE_FRONT ? clipplane : -clipplane, options.epsilon.value(), true)[SIDE_FRONT];
        if (winding && WindingIsTiny(*winding, 0.5)) {
            winding = std::nullopt;
        }
        if (!winding) {
            //logging::funcprint("WARNING: New portal was clipped away near ({:.3} {:.3} {:.3})\n", portal->winding->at(0)[0],
            //    portal->winding->at(0)[1], portal->winding->at(0)[2]);
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
        auto windings = portal->winding->clip(plane, options.epsilon.value());

        if (windings[SIDE_BACK] && WindingIsTiny(*windings[SIDE_BACK], 0.5)) {
            windings[SIDE_BACK] = std::nullopt;
        }
        if (windings[SIDE_FRONT] && WindingIsTiny(*windings[SIDE_FRONT], 0.5)) {
            windings[SIDE_FRONT] = std::nullopt;
        }

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
    logging::percent(state->iNodesDone++, splitnodes);

    CutNodePortals_r(front, state);
    CutNodePortals_r(back, state);
}

void AssertNoPortals(node_t *node)
{
    Q_assert(!node->portals);

    if (node->planenum != PLANENUM_LEAF) {
        AssertNoPortals(node->children[0]);
        AssertNoPortals(node->children[1]);
    }
}

/*
==================
PortalizeWorld

Builds the exact polyhedrons for the nodes and leafs
==================
*/
void MakeTreePortals(tree_t *tree)
{
    portal_state_t state{};

    state.iNodesDone = 0;

    FreeTreePortals_r(tree->headnode);

    AssertNoPortals(tree->headnode);
    MakeHeadnodePortals(tree);
    CutNodePortals_r(tree->headnode, &state);
}

/*
==================
FreeTreePortals_r

==================
*/
void FreeTreePortals_r(node_t *node)
{
    portal_t *p, *nextp;

    if (node->planenum != PLANENUM_LEAF) {
        FreeTreePortals_r(node->children[0]);
        FreeTreePortals_r(node->children[1]);
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
    node->portals = nullptr;
}
