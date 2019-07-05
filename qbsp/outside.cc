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

#include <qbsp/qbsp.hh>

#include <vector>
#include <set>
#include <list>
#include <utility>

/*
===========
PointInLeaf
===========
*/
node_t *
PointInLeaf(node_t *node, const vec3_t point)
{
    vec_t dist;
    const qbsp_plane_t *plane;

    while (!node->contents) {
        plane = &map.planes[node->planenum];
        dist = DotProduct(plane->normal, point) - plane->dist;
        node = (dist > 0) ? node->children[0] : node->children[1];
    }

    return node;
}

static FILE *
InitPtsFile(void)
{
    FILE *ptsfile;

    StripExtension(options.szBSPName);
    strcat(options.szBSPName, ".pts");
    ptsfile = fopen(options.szBSPName, "wt");
    if (!ptsfile)
        Error("Failed to open %s: %s", options.szBSPName, strerror(errno));

    return ptsfile;
}

// new code

static void
ClearOccupied_r(node_t *node)
{
    if (node->planenum != PLANENUM_LEAF) {
        ClearOccupied_r(node->children[0]);
        ClearOccupied_r(node->children[1]);
        return;
    }
    
    /* leaf node */
    node->occupied = 0;
    node->occupant = nullptr;
}

/*
=============
Portal_Passable

Returns true if the portal has non-opaque leafs on both sides
 
from q3map
=============
*/
static bool Portal_Passable(const portal_t *p)
{
    if (p->nodes[0] == &outside_node
        || p->nodes[1] == &outside_node) {
        // FIXME: need this because the outside_node doesn't have PLANENUM_LEAF set
        return false;
    }
    
    Q_assert(p->nodes[0]->planenum == PLANENUM_LEAF);
    Q_assert(p->nodes[1]->planenum == PLANENUM_LEAF);

    if (p->nodes[0]->opaque()
        || p->nodes[1]->opaque())
        return false;
    
    return true;
}

/*
==================
precondition: all leafs have occupied set to 0
==================
*/
static void
BFSFloodFillFromOccupiedLeafs(const std::vector<node_t *> &occupied_leafs)
{
    std::list<std::pair<node_t *, int>> queue;
    for (node_t *leaf : occupied_leafs) {
        queue.push_back(std::make_pair(leaf, 1));
    }
    
    while (!queue.empty()) {
        auto pair = queue.front();
        queue.pop_front();
        
        node_t *node = pair.first;
        const int dist = pair.second;
        
        if (node->occupied == 0) {
            // we haven't visited this node yet
            node->occupied = dist;
            
            // push neighbouring nodes onto the back of the queue
            int side;
            for (portal_t *portal = node->portals; portal; portal = portal->next[!side]) {
                side = (portal->nodes[0] == node);
                
                if (!Portal_Passable(portal))
                    continue;
                
                node_t *neighbour = portal->nodes[side];
                queue.push_back(std::make_pair(neighbour, dist + 1));
            }
        }
    }
}

static std::pair<std::vector<portal_t *>, node_t*>
MakeLeakLine(node_t *outleaf)
{
    std::vector<portal_t *> result;
    
    Q_assert(outleaf->occupied > 0);
    
    node_t *node = outleaf;
    while (1)
    {
        // exit?
        if (node->occupied == 1)
            break; // this node contains an entity
        
        // find the next node...
        
        node_t *bestneighbour = nullptr;
        portal_t *bestportal = nullptr;
        int bestoccupied = node->occupied;
        
        int side;
        for (portal_t *portal = node->portals; portal; portal = portal->next[!side]) {
            side = (portal->nodes[0] == node);
            
            if (!Portal_Passable(portal))
                continue;
            
            node_t *neighbour = portal->nodes[side];
            Q_assert(neighbour != node);
            Q_assert(neighbour->occupied > 0);
            
            if (neighbour->occupied < bestoccupied) {
                bestneighbour = neighbour;
                bestportal = portal;
                bestoccupied = neighbour->occupied;
            }
        }
        
        Q_assert(bestneighbour != nullptr);
        Q_assert(bestoccupied < node->occupied);
        
        // go through bestportal
        result.push_back(bestportal);
        node = bestneighbour;
    }
    
    Q_assert(node->occupant != nullptr);
    Q_assert(node->occupied == 1);
    return std::make_pair(result, node);
}

/*
===============
WriteLeakTrail
===============
*/
static void
WriteLeakTrail(FILE *leakfile, const vec3_t point1, const vec3_t point2)
{
    vec3_t vector, trail;
    vec_t dist;
    
    VectorSubtract(point2, point1, vector);
    dist = VectorNormalize(vector);
    
    VectorCopy(point1, trail);
    while (dist > options.dxLeakDist) {
        fprintf(leakfile, "%f %f %f\n", trail[0], trail[1], trail[2]);
        VectorMA(trail, options.dxLeakDist, vector, trail);
        dist -= options.dxLeakDist;
    }
}

static void
WriteLeakLine(const std::pair<std::vector<portal_t *>, node_t*> &leakline)
{
    FILE *ptsfile = InitPtsFile();
    
    vec3_t prevpt, currpt;
    VectorCopy(leakline.second->occupant->origin, prevpt);
    
    for (auto it = leakline.first.rbegin(); it != leakline.first.rend(); ++it) {
        portal_t *portal = *it;
        MidpointWinding(portal->winding, currpt);
        
        // draw dots from prevpt to currpt
        WriteLeakTrail(ptsfile, prevpt, currpt);
        
        VectorCopy(currpt, prevpt);
    }
    
    fclose(ptsfile);
    Message(msgLiteral, "Leak file written to %s\n", options.szBSPName);
}

/*
==================
FindOccupiedLeafs

sets node->occupant
==================
*/
static std::vector<node_t *>
FindOccupiedLeafs(node_t *headnode)
{
    std::vector<node_t *> result;
    
    for (int i = 1; i < map.numentities(); i++) {
        mapentity_t *entity = &map.entities.at(i);
        
        /* skip entities at (0 0 0) (bmodels) */
        if (VectorCompare(entity->origin, vec3_origin, EQUAL_EPSILON))
            continue;
        
        /* find the leaf it's in. Skip opqaue leafs */
        node_t *leaf = PointInLeaf(headnode, entity->origin);
        if (leaf->opaque())
            continue;
        
        /* did we already find an entity for this leaf? */
        if (leaf->occupant != nullptr)
            continue;
        
        leaf->occupant = entity;
        
        result.push_back(leaf);
    }
    
    return result;
}

/*
==================
ResetFacesTouchingOccupiedLeafs

Set f->touchesOccupiedLeaf=false on all faces.
==================
*/
static void
ResetFacesTouchingOccupiedLeafs(node_t *node)
{
    if (node->planenum == PLANENUM_LEAF) {
        return;
    }

    for (face_t *face = node->faces; face; face = face->next) {
        face->touchesOccupiedLeaf = false;
    }

    ResetFacesTouchingOccupiedLeafs(node->children[0]);
    ResetFacesTouchingOccupiedLeafs(node->children[1]);
}

/*
==================
MarkFacesTouchingOccupiedLeafs

Set f->touchesOccupiedLeaf=true on faces that are touching occupied leafs
==================
*/
static void
MarkFacesTouchingOccupiedLeafs(node_t *node)
{
    if (node->planenum != PLANENUM_LEAF) {
        MarkFacesTouchingOccupiedLeafs(node->children[0]);
        MarkFacesTouchingOccupiedLeafs(node->children[1]);
        return;
    }

    // visit the leaf

    if (node->occupied > 0) {
        // This is an occupied leaf, so we need to keep all of the faces touching it.
        for (face_t **markface = node->markfaces; *markface; markface++) {
            (*markface)->touchesOccupiedLeaf = true;
        }
    }
}

/*
==================
ClearOutFaces

Deletes (by setting f->w.numpoints=0) faces in solid nodes
==================
*/
static void
ClearOutFaces(node_t *node)
{
    if (node->planenum != PLANENUM_LEAF) {
        ClearOutFaces(node->children[0]);
        ClearOutFaces(node->children[1]);
        return;
    }

    // visit the leaf
    if (node->contents != CONTENTS_SOLID) {
        return;
    }

    for (face_t **markface = node->markfaces; *markface; markface++) {
        // NOTE: This is how faces are deleted here, kind of ugly
        (*markface)->w.numpoints = 0;
    }

    // FIXME: Shouldn't be needed here
    node->faces = NULL;
}

static void
OutLeafsToSolid_r(node_t *node, int *outleafs_count)
{
    if (node->planenum != PLANENUM_LEAF) {
        OutLeafsToSolid_r(node->children[0], outleafs_count);
        OutLeafsToSolid_r(node->children[1], outleafs_count);
        return;
    }
    
    // skip leafs reachable from entities
    if (node->occupied > 0)
        return;
    
    // Don't fill sky, or count solids as outleafs
    if (node->contents == CONTENTS_SKY
        || node->contents == CONTENTS_SOLID)
        return;

    // Now check all faces touching the leaf. If any of them are partially going into the occupied part of the map,
    // don't fill the leaf (see comment in FillOutside).
    bool skipFill = false;
    for (face_t **markface = node->markfaces; *markface; markface++) {
        if ((*markface)->touchesOccupiedLeaf) {
            skipFill = true;
            break;
        }
    }
    if (skipFill) {
        return;
    }

    // Finally, we can fill it in as void.
    node->contents = CONTENTS_SOLID;
    *outleafs_count += 1;
}

static int
OutLeafsToSolid(node_t *node)
{
    int count = 0;
    OutLeafsToSolid_r(node, &count);
    return count;
}

//=============================================================================

/*
===========
FillOutside

===========
*/
bool
FillOutside(node_t *node, const int hullnum)
{
    Message(msgProgress, "FillOutside");
    
    if (options.fNofill) {
        Message(msgStat, "skipped");
        return false;
    }
    
    /* Clear the node->occupied on all leafs to 0 */
    ClearOccupied_r(node);
    
    const std::vector<node_t *> occupied_leafs = FindOccupiedLeafs(node);

    if (occupied_leafs.empty()) {
        Message(msgWarning, warnNoFilling, hullnum);
        return false;
    }

    BFSFloodFillFromOccupiedLeafs(occupied_leafs);

    /* first check to see if an occupied leaf is hit */
    const int side = (outside_node.portals->nodes[0] == &outside_node);
    node_t *fillnode = outside_node.portals->nodes[side];
    
    if (fillnode->occupied > 0) {
        const auto leakline = MakeLeakLine(fillnode);
        
        mapentity_t *leakentity = leakline.second->occupant;
        Q_assert(leakentity != nullptr);
        
        const vec_t *origin = leakentity->origin;
        Message(msgWarning, warnMapLeak, ValueForKey(leakentity, "classname"), origin[0], origin[1], origin[2]);
        if (map.leakfile)
            return false;
        
        WriteLeakLine(leakline);
        map.leakfile = true;

        /* Get rid of the .prt file since the map has a leak */
        StripExtension(options.szBSPName);
        strcat(options.szBSPName, ".prt");
        remove(options.szBSPName);
        
        if (options.fLeakTest) {
            logprint("Aborting because -leaktest was used.\n");
            exit(1);
        }
        
        return false;
    }

    // At this point, leafs not reachable from entities have (node->occupied == 0).
    // The two final tasks are:
    // 1. Mark the leafs that are not reachable as CONTENTS_SOLID (i.e. filling them in as the void).
    // 2. Delete faces in those leafs


    // An annoying wrinkle here: there may be leafs with (node->occupied == 0), which means they should be filled in as void,
    // but they have faces straddling between them and occupied leafs (i.e. leafs which will be CONTENTS_EMPTY because
    // they're in playable space). See missing_face_simple.map for an example.
    //
    // The subtlety is, if we fill these leafs in as solid and delete the inward-facing faces, the only face left
    // will be the void-and-non-void-straddling face. This face will mess up LinkConvexFaces, since we need to rebuild the
    // BSP and recalculate the leaf contents, unaware of the fact that we wanted this leaf to be void (CONTENTS_SOLID),
    // and this face will cause it to be marked as CONTENTS_EMPTY which will manifest as messed up hull0 collision in game
    // (weapons shoot through the leaf.)
    //
    // In order to avoid this scenario, we need to detect those "void-and-non-void-straddling" faces and not fill those leafs
    // in as solid. This will keep some extra faces around but keep the content types consistent.

    ResetFacesTouchingOccupiedLeafs(node);
    MarkFacesTouchingOccupiedLeafs(node);

    /* now go back and fill outside with solid contents */
    const int outleafs = OutLeafsToSolid(node);

    /* remove faces from filled in leafs */
    ClearOutFaces(node);

    Message(msgStat, "%8d outleafs", outleafs);
    return true;
}
