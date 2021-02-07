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
#include <map>
#include <list>

static int needlmshifts;

/*
===============
SubdivideFace

If the face is >256 in either texture direction, carve a valid sized
piece off and insert the remainder in the next link
===============
*/
void
SubdivideFace(face_t *f, face_t **prevptr)
{
    vec_t mins, maxs;
    vec_t v;
    int axis, i;
    qbsp_plane_t plane;
    face_t *front, *back, *next;
    const mtexinfo_t *tex;
    vec3_t tmp;
    vec_t subdiv;
    vec_t extent;
    int lmshift;

    /* special (non-surface cached) faces don't need subdivision */
    tex = &map.mtexinfos.at(f->texinfo);
    if (tex->flags & (TEX_SPECIAL | TEX_SKIP | TEX_HINT))
        return;

//subdivision is pretty much pointless other than because of lightmap block limits
//one lightmap block will always be added at the end, for smooth interpolation

    //engines that do support scaling will support 256*256 blocks (at whatever scale).
    lmshift = f->lmshift[0];
    if (lmshift > 4)
        lmshift = 4;    //no bugging out with legacy lighting
    subdiv = 255<<lmshift;

//legacy engines support 18*18 max blocks (at 1:16 scale).
//the 18*18 limit can be relaxed in certain engines, and doing so will generally give a performance boost.
    if (subdiv >= options.dxSubdivide)
        subdiv = options.dxSubdivide;

//      subdiv += 8;

//floating point precision from clipping means we should err on the low side
//the bsp is possibly going to be used in both engines that support scaling and those that do not. this means we always over-estimate by 16 rathern than 1<<lmscale

    for (axis = 0; axis < 2; axis++) {
        while (1) {
            mins = VECT_MAX;
            maxs = -VECT_MAX;

            tmp[0] = tex->vecs[axis][0];
            tmp[1] = tex->vecs[axis][1];
            tmp[2] = tex->vecs[axis][2];

            for (i = 0; i < f->w.numpoints; i++) {
                v = DotProduct(f->w.points[i], tmp);
                if (v < mins)
                    mins = v;
                if (v > maxs)
                    maxs = v;
            }

            extent = ceil(maxs) - floor(mins);
//          extent = maxs - mins;
            if (extent <= subdiv)
                break;

            // split it
            VectorCopy(tmp, plane.normal);
            v = VectorLength(plane.normal);
            VectorNormalize(plane.normal);
            
            // ericw -- reverted this, was causing https://github.com/ericwa/ericw-tools/issues/160
//            if (subdiv > extent/2)      /* if we're near a boundary, just split the difference, this should balance the load slightly */
//                plane.dist = (mins + subdiv/2) / v;
//            else
//                plane.dist = (mins + subdiv) / v;
            plane.dist = (mins + subdiv - 16) / v;
            
            next = f->next;
            SplitFace(f, &plane, &front, &back);
            if (!front || !back)
            {
                printf("didn't split\n");
                break;
//              Error("Didn't split the polygon (%s)", __func__);
            }
            *prevptr = back;
            back->next = front;
            front->next = next;
            f = back;
        }
    }
}


/*
=============================================================================
GatherNodeFaces

Frees the current node tree and returns a new chain of the surfaces that
have inside faces.
=============================================================================
*/

static void
GatherNodeFaces_r(node_t *node, std::map<int, face_t *> &planefaces)
{
    face_t *f, *next;

    if (node->planenum != PLANENUM_LEAF) {
        // decision node
        for (f = node->faces; f; f = next) {
            next = f->next;
            if (!f->w.numpoints) {      // face was removed outside
                FreeMem(f, FACE, 1);
            } else {
                f->next = planefaces[f->planenum];
                planefaces[f->planenum] = f;
            }
        }
        GatherNodeFaces_r(node->children[0], planefaces);
        GatherNodeFaces_r(node->children[1], planefaces);
    }
    FreeMem(node, NODE, 1);
}

/*
================
GatherNodeFaces
================
*/
surface_t *
GatherNodeFaces(node_t *headnode)
{
    surface_t *surfaces;

    std::map<int, face_t *> planefaces;
    GatherNodeFaces_r(headnode, planefaces);
    surfaces = BuildSurfaces(planefaces);

    return surfaces;
}

//===========================================================================

// This is a kludge.   Should be pEdgeFaces[2].
static const face_t **pEdgeFaces0;
static const face_t **pEdgeFaces1;
static int cStartEdge;

//============================================================================

using vertidx_t = int;
using edgeidx_t = int;
static std::map<std::pair<vertidx_t, vertidx_t>, std::list<edgeidx_t>> hashedges;
static std::map<std::tuple<int,int,int>, std::list<hashvert_t>> hashverts;

static void
InitHash(void)
{
    hashverts.clear();
    hashedges.clear();
}

static void
AddHashEdge(int v1, int v2, int i)
{
    hashedges[std::make_pair(v1, v2)].push_front(i);
}

static std::tuple<int,int,int>
HashVec(const vec3_t vec)
{
    return std::make_tuple(static_cast<int>(floor(vec[0])),
                           static_cast<int>(floor(vec[1])),
                           static_cast<int>(floor(vec[2])));
}

static void
AddHashVert(const vec3_t vert, const int global_vert_num)
{
    hashvert_t hv;
    VectorCopy(vert, hv.point);
    hv.num = global_vert_num;
    
    // insert each vert at floor(pos[axis]) and floor(pos[axis]) + 1 (for each axis)
    // so e.g. a vert at (0.99, 0.99, 0.99) shows up if we search at (1.01, 1.01, 1.01)
    // this is a bit wasteful, since it inserts 8 copies of each vert.
    
    for (int x=0; x<=1; x++) {
        for (int y=0; y<=1; y++) {
            for (int z=0; z<=1; z++) {
                const auto h = std::make_tuple(static_cast<int>(floor(vert[0])) + x,
                                               static_cast<int>(floor(vert[1])) + y,
                                               static_cast<int>(floor(vert[2])) + z);
                hashverts[h].push_front(hv);
            }
        }
    }
}

/*
=============
GetVertex
=============
*/
static int
GetVertex(mapentity_t *entity, const vec3_t in)
{
    int i;
    vec3_t vert;
    struct lumpdata *vertices = &entity->lumps[LUMP_VERTEXES];
    dvertex_t *dvertex;

    for (i = 0; i < 3; i++) {
        if (fabs(in[i] - Q_rint(in[i])) < ZERO_EPSILON)
            vert[i] = Q_rint(in[i]);
        else
            vert[i] = in[i];
    }

    const auto h = HashVec(vert);
    auto it = hashverts.find(h);
    if (it != hashverts.end()) {
        for (hashvert_t &hv : it->second) {
            if (fabs(hv.point[0] - vert[0]) < POINT_EPSILON &&
                fabs(hv.point[1] - vert[1]) < POINT_EPSILON &&
                fabs(hv.point[2] - vert[2]) < POINT_EPSILON) {

                return hv.num;
            }
        }
    }

    const int global_vert_num = map.cTotal[LUMP_VERTEXES]++;

    AddHashVert(vert, global_vert_num);
    
    if (vertices->index == vertices->count)
        Error("Internal error: didn't allocate enough vertices?");

    /* emit a vertex */
    dvertex = (dvertex_t *)vertices->data + vertices->index;
    dvertex->point[0] = vert[0];
    dvertex->point[1] = vert[1];
    dvertex->point[2] = vert[2];
    vertices->index++;

    return global_vert_num;
}

//===========================================================================

/*
==================
GetEdge

Don't allow four way edges (FIXME: What is this?)
 
Returns a global edge number, possibly negative to indicate a backwards edge.
==================
*/
static int
GetEdge(mapentity_t *entity, const vec3_t p1, const vec3_t p2,
        const face_t *face)
{
    struct lumpdata *edges = &entity->lumps[LUMP_EDGES];
    int v1, v2;
    int i;

    if (!face->contents[0])
        Error("Face with 0 contents (%s)", __func__);

    v1 = GetVertex(entity, p1);
    v2 = GetVertex(entity, p2);

    // search for an existing edge from v2->v1
    const std::pair<int,int> edge_hash_key = std::make_pair(v2, v1);

    if (options.BSPVersion == BSPVERSION || options.BSPVersion == BSPHLVERSION) {
        bsp29_dedge_t *edge;

        auto it = hashedges.find(edge_hash_key);
        if (it != hashedges.end()) {
            for (const int i : it->second) {
                edge = (bsp29_dedge_t *)edges->data + i;
                if (!(v1 == edge->v[1] && v2 == edge->v[0])) {
                    Error("Too many edges for standard BSP format. Try compiling with -bsp2");
                }
                if (pEdgeFaces1[i] == NULL
                    && pEdgeFaces0[i]->contents[0] == face->contents[0]) {
                    pEdgeFaces1[i] = face;
                    return -(i + cStartEdge);
                }
            }
        }

        /* emit an edge */
        i = edges->index;
        edge = (bsp29_dedge_t *)edges->data + i;
        if (edges->index >= edges->count)
            Error("Internal error: didn't allocate enough edges?");
        edge->v[0] = v1;
        edge->v[1] = v2;
    } else {
        bsp2_dedge_t *edge;

        auto it = hashedges.find(edge_hash_key);
        if (it != hashedges.end()) {
            for (const int i : it->second) {
                edge = (bsp2_dedge_t *)edges->data + i;
                Q_assert(v1 == edge->v[1] && v2 == edge->v[0]);
                if (pEdgeFaces1[i] == NULL
                    && pEdgeFaces0[i]->contents[0] == face->contents[0]) {
                    pEdgeFaces1[i] = face;
                    return -(i + cStartEdge);
                }
            }
        }

        /* emit an edge */
        i = edges->index;
        edge = (bsp2_dedge_t *)edges->data + i;
        if (edges->index >= edges->count)
            Error("Internal error: didn't allocate enough edges?");
        edge->v[0] = v1;
        edge->v[1] = v2;
    }

    AddHashEdge(v1, v2, edges->index);

    edges->index++;
    map.cTotal[LUMP_EDGES]++;
    pEdgeFaces0[i] = face;
    return i + cStartEdge;
}


/*
==================
FindFaceEdges
==================
*/
static void
FindFaceEdges(mapentity_t *entity, face_t *face)
{
    int i, memsize;

    if (map.mtexinfos.at(face->texinfo).flags & (TEX_SKIP | TEX_HINT))
        return;
    
    face->outputnumber = -1;
    if (face->w.numpoints > MAXEDGES)
        Error("Internal error: face->numpoints > MAXEDGES (%s)", __func__);

    memsize = face->w.numpoints * sizeof(face->edges[0]);
    face->edges = (int *)AllocMem(OTHER, memsize, true);
    for (i = 0; i < face->w.numpoints; i++) {
        const vec_t *p1 = face->w.points[i];
        const vec_t *p2 = face->w.points[(i + 1) % face->w.numpoints];
        face->edges[i] = GetEdge(entity, p1, p2, face);
    }
}


/*
================
MakeFaceEdges_r
================
*/
static int
MakeFaceEdges_r(mapentity_t *entity, node_t *node, int progress)
{
    face_t *f;

    if (node->planenum == PLANENUM_LEAF)
        return progress;

    for (f = node->faces; f; f = f->next) {
        FindFaceEdges(entity, f);
    }

    Message(msgPercent, ++progress, splitnodes);
    progress = MakeFaceEdges_r(entity, node->children[0], progress);
    progress = MakeFaceEdges_r(entity, node->children[1], progress);

    return progress;
}

/*
==============
EmitFace
==============
*/
template <class DFACE>
static void
EmitFace_Internal(mapentity_t *entity, face_t *face)
{
    struct lumpdata *surfedges = &entity->lumps[LUMP_SURFEDGES];
    struct lumpdata *faces = &entity->lumps[LUMP_FACES];
    struct lumpdata *lmshifts = &entity->lumps[BSPX_LMSHIFT];
    DFACE *out;
    int i;

    if (map.mtexinfos.at(face->texinfo).flags & (TEX_SKIP | TEX_HINT))
        return;
    
    // emit a region
    Q_assert(face->outputnumber == -1);
    face->outputnumber = map.cTotal[LUMP_FACES];
    if (lmshifts->data)
        ((unsigned char*)lmshifts->data)[faces->index] = face->lmshift[1];
    out = (DFACE *)faces->data + faces->index;
    out->planenum = ExportMapPlane(face->planenum);
    out->side = face->planeside;
    out->texinfo = ExportMapTexinfo(face->texinfo);
    for (i = 0; i < MAXLIGHTMAPS; i++)
        out->styles[i] = 255;
    out->lightofs = -1;
    
    out->firstedge = map.cTotal[LUMP_SURFEDGES];
    for (i = 0; i < face->w.numpoints; i++) {
        ((int *)surfedges->data)[surfedges->index] = face->edges[i];
        surfedges->index++;
        map.cTotal[LUMP_SURFEDGES]++;
    }
    FreeMem(face->edges, OTHER, face->w.numpoints * sizeof(int));
    
    out->numedges = map.cTotal[LUMP_SURFEDGES] - out->firstedge;
    
    map.cTotal[LUMP_FACES]++;
    faces->index++;
}

static void
EmitFace(mapentity_t *entity, face_t *face)
{
    if (options.BSPVersion == BSPVERSION || options.BSPVersion == BSPHLVERSION)
        EmitFace_Internal<bsp29_dface_t>(entity, face);
    else
        EmitFace_Internal<bsp2_dface_t>(entity, face);
}

/*
==============
GrowNodeRegion
==============
*/
static void
GrowNodeRegion(mapentity_t *entity, node_t *node)
{
    if (node->planenum == PLANENUM_LEAF)
        return;

    node->firstface = map.cTotal[LUMP_FACES];

    for (face_t *face = node->faces; face; face = face->next) {
        Q_assert(face->planenum == node->planenum);
        
        // emit a region
        EmitFace(entity, face);
    }

    node->numfaces = map.cTotal[LUMP_FACES] - node->firstface;

    GrowNodeRegion(entity, node->children[0]);
    GrowNodeRegion(entity, node->children[1]);
}

static void
CountFace(mapentity_t *entity, face_t *f)
{
    if (map.mtexinfos.at(f->texinfo).flags & (TEX_SKIP | TEX_HINT))
        return;
    
    if (f->lmshift[1] != 4)
        needlmshifts = true;
    entity->lumps[LUMP_FACES].count++;
    entity->lumps[LUMP_VERTEXES].count += f->w.numpoints;
}

/*
==============
CountData_r
==============
*/
static void
CountData_r(mapentity_t *entity, node_t *node)
{
    face_t *f;

    if (node->planenum == PLANENUM_LEAF)
        return;

    for (f = node->faces; f; f = f->next) {
        CountFace(entity, f);
    }

    CountData_r(entity, node->children[0]);
    CountData_r(entity, node->children[1]);
}

/*
================
MakeFaceEdges
================
*/
int
MakeFaceEdges(mapentity_t *entity, node_t *headnode)
{
    int i, firstface;
    struct lumpdata *surfedges = &entity->lumps[LUMP_SURFEDGES];
    struct lumpdata *edges = &entity->lumps[LUMP_EDGES];
    struct lumpdata *vertices = &entity->lumps[LUMP_VERTEXES];
    struct lumpdata *faces = &entity->lumps[LUMP_FACES];
    struct lumpdata *lmshifts = &entity->lumps[BSPX_LMSHIFT];

    Message(msgProgress, "MakeFaceEdges");

    needlmshifts = false;
    cStartEdge = 0;
    const int entnum = entity - &map.entities.at(0);
    for (i = 0; i < entnum; i++)
        cStartEdge += map.entities.at(i).lumps[LUMP_EDGES].count;

    CountData_r(entity, headnode);

    /*
     * Remember edges are +1 in BeginBSPFile.  Often less than half
     * the vertices actually are unique, although heavy use of skip
     * faces will break that assumption.  2/3 should be safe most of
     * the time without wasting too much memory...
     */
    surfedges->count = vertices->count;
    edges->count += surfedges->count;

    vertices->data = AllocMem(BSP_VERTEX, vertices->count, true);
    edges->data = AllocMem(BSP_EDGE, edges->count, true);

    // Accessory data
    pEdgeFaces0 = (const face_t **)AllocMem(OTHER, sizeof(face_t *) * edges->count, true);
    pEdgeFaces1 = (const face_t **)AllocMem(OTHER, sizeof(face_t *) * edges->count, true);

    InitHash();

    firstface = map.cTotal[LUMP_FACES];
    MakeFaceEdges_r(entity, headnode, 0);
    
    FreeMem(pEdgeFaces0, OTHER, sizeof(face_t *) * edges->count);
    FreeMem(pEdgeFaces1, OTHER, sizeof(face_t *) * edges->count);

    /* Free any excess allocated memory */
    if (vertices->index < vertices->count) {
        dvertex_t *temp = (dvertex_t *)AllocMem(BSP_VERTEX, vertices->index, true);
        memcpy(temp, vertices->data, sizeof(*temp) * vertices->index);
        FreeMem(vertices->data, BSP_VERTEX, vertices->count);
        vertices->data = temp;
        vertices->count = vertices->index;
    }
    if (edges->index < edges->count) {
        void *temp = AllocMem(BSP_EDGE, edges->index, true);
        memcpy(temp, edges->data, MemSize[BSP_EDGE] * edges->index);
        FreeMem(edges->data, BSP_EDGE, edges->count);
        edges->data = temp;
        edges->count = edges->index;
    }

    if (map.cTotal[LUMP_VERTEXES] > 65535 && (options.BSPVersion == BSPVERSION || options.BSPVersion == BSPHLVERSION))
        Error("Too many vertices (%d > 65535). Recompile with the \"-bsp2\" flag to lift this restriction.", map.cTotal[LUMP_VERTEXES]);

    surfedges->data = AllocMem(BSP_SURFEDGE, surfedges->count, true);
    faces->data = AllocMem(BSP_FACE, faces->count, true);

    lmshifts->count = needlmshifts?faces->count:0;
    lmshifts->data = needlmshifts?AllocMem(OTHER, sizeof(uint8_t) * lmshifts->count, true):NULL;

    Message(msgProgress, "GrowRegions");
    GrowNodeRegion(entity, headnode);
    
    return firstface;
}
