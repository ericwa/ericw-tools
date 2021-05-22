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

#include <light/light.hh>
#include <light/trace.hh>
#include <light/ltface.hh>
#include <common/bsputils.hh>
#ifdef HAVE_EMBREE
#include <light/trace_embree.hh>
#endif
#include <cassert>

#define TRACE_HIT_NONE  0
#define TRACE_HIT_SOLID (1 << 0)
#define TRACE_HIT_WATER (1 << 1)
#define TRACE_HIT_SLIME (1 << 2)
#define TRACE_HIT_LAVA  (1 << 3)
#define TRACE_HIT_SKY   (1 << 4)

typedef struct traceinfo_s {
    vec3_t			point;
    const bsp2_dface_t          *face;
    plane_t  hitplane;
    /* returns true if sky was hit. */
    bool hitsky;
    bool hitback;
    
    // internal
    vec3_t dir;
} traceinfo_t;

/* Stopped by solid and sky */
static bool TraceFaces (traceinfo_t *ti, int node, const vec3_t start, const vec3_t end);

/*
 * ---------
 * TraceLine
 * ---------
 * Generic BSP model ray tracing function. Traces a ray from start towards
 * stop. If the trace line hits one of the flagged contents along the way, the
 * corresponding TRACE flag will be returned.
 *
 *  model    - The bsp model to trace against
 *  flags    - contents which will stop the trace (must be > 0)
 *  start    - coordinates to start trace
 *  stop     - coordinates to end the trace
 *
 * TraceLine will return a negative traceflag if the point 'start' resides
 * inside a leaf with one of the contents types which stop the trace.
 *
 * ericw -- note, this should only be used for testing occlusion.
 * the hitpoint is not accurate, imagine a solid cube floating in a room,
 * only one of the 6 sides will be a node with a solid leaf child.
 * Yet, which side is the node with the solid leaf child determines
 * what the hit point will be.
 */
static int TraceLine(const dmodel_t *model, const int traceflags,
              const vec3_t start, const vec3_t end);

typedef struct tnode_s {
    vec3_t normal;
    vec_t dist;
    int type;
    int children[2];
    const dplane_t *plane;
    const mleaf_t *childleafs[2];
    const bsp2_dnode_t *node;
} tnode_t;

typedef struct faceinfo_s {
    int numedges;
    plane_t *edgeplanes;
    
    // sphere culling
    vec3_t origin;
    vec_t radiusSquared;
    
    int content;
    plane_t plane;
    
    const char *texturename;
    const bsp2_dface_t *face;
} faceinfo_t;

static tnode_t *tnodes;
static const mbsp_t *bsp_static;

static faceinfo_t *faceinfos;
static bool *fence_dmodels; // bsp_static->nummodels bools, true if model contains a fence texture

// from hmap2
#define PlaneDiff(point,plane) (((plane)->type < 3 ? (point)[(plane)->type] : DotProduct((point), (plane)->normal)) - (plane)->dist)

/*
==============
Light_PointInLeaf
 
from hmap2
==============
*/
const mleaf_t *
Light_PointInLeaf( const mbsp_t *bsp, const vec3_t point )
{
    int num = 0;
    
    while( num >= 0 )
        num = bsp->dnodes[num].children[PlaneDiff(point, &bsp->dplanes[bsp->dnodes[num].planenum]) < 0];
    
    return bsp->dleafs + (-1 - num);
}

/*
==============
Light_PointContents

from hmap2
==============
*/
int Light_PointContents( const mbsp_t *bsp, const vec3_t point )
{
    return Light_PointInLeaf(bsp, point)->contents;
}

/*
 * ==============
 * MakeTnodes
 * Converts the disk node structure into the efficient tracing structure
 * ==============
 */
static void
MakeTnodes_r(int nodenum, const mbsp_t *bsp)
{
    tnode_t *tnode;
    int i;
    bsp2_dnode_t *node;
    mleaf_t *leaf;

    Q_assert(nodenum >= 0);
    Q_assert(nodenum < bsp->numnodes);
    tnode = &tnodes[nodenum];

    node = bsp->dnodes + nodenum;
    tnode->plane = bsp->dplanes + node->planenum;
    tnode->node = node;

    tnode->type = tnode->plane->type;
    VectorCopy(tnode->plane->normal, tnode->normal);
    tnode->dist = tnode->plane->dist;

    for (i = 0; i < 2; i++) {
        int childnum = node->children[i];
        if (childnum < 0) {
            leaf = &bsp->dleafs[-childnum - 1];
            tnode->children[i] = leaf->contents;
            tnode->childleafs[i] = leaf;
        } else {
            tnode->children[i] = childnum;
            MakeTnodes_r(childnum, bsp);
        }
    }
}

static inline bool SphereCullPoint(const faceinfo_t *info, const vec3_t point)
{
    vec3_t delta;
    vec_t deltaLengthSquared;
    VectorSubtract(point, info->origin, delta);
    deltaLengthSquared = DotProduct(delta, delta);
    return deltaLengthSquared > info->radiusSquared;
}

static void
MakeFaceInfo(const mbsp_t *bsp, const bsp2_dface_t *face, faceinfo_t *info)
{
    info->face = face;
    info->numedges = face->numedges;
    info->edgeplanes = Face_AllocInwardFacingEdgePlanes(bsp, face);
    
    info->plane = Face_Plane(bsp, face);
    
    // make sphere that bounds the face
    vec3_t centroid = {0,0,0};
    for (int i=0; i<face->numedges; i++)
    {
        const vec_t *v = GetSurfaceVertexPoint(bsp, face, i);
        VectorAdd(centroid, v, centroid);
    }
    VectorScale(centroid, 1.0f/face->numedges, centroid);
    VectorCopy(centroid, info->origin);
    
    // calculate radius
    vec_t maxRadiusSq = 0;
    for (int i=0; i<face->numedges; i++)
    {
        vec3_t delta;
        vec_t radiusSq;
        const vec_t *v = GetSurfaceVertexPoint(bsp, face, i);
        VectorSubtract(v, centroid, delta);
        radiusSq = DotProduct(delta, delta);
        if (radiusSq > maxRadiusSq)
            maxRadiusSq = radiusSq;
    }
    info->radiusSquared = maxRadiusSq;
    
    info->content = Face_Contents(bsp, face);
    
    info->texturename = Face_TextureName(bsp, face);
    
#if 0
    //test
    for (int i=0; i<face->numedges; i++)
    {
        const vec_t *v = GetSurfaceVertexPoint(bsp, face, i);
        Q_assert(!SphereCullPoint(info, v));
    }
    //test
    {
        vec_t radius = sqrt(maxRadiusSq);
        radius ++;
        
        vec3_t test;
        vec3_t n = {1, 0, 0};
        VectorMA(centroid, radius, n, test);
        
        Q_assert(SphereCullPoint(info, test));
    }
#endif
}

static bool
Model_HasFence(const mbsp_t *bsp, const dmodel_t *model)
{
    for (int j = model->firstface; j < model->firstface + model->numfaces; j++) {
        const bsp2_dface_t *face = BSP_GetFace(bsp, j);
        if (Face_TextureName(bsp, face)[0] == '{') {
            return true;
        }
    }
    return false;
}

static void
MakeFenceInfo(const mbsp_t *bsp)
{
    fence_dmodels = (bool *) calloc(bsp->nummodels, sizeof(bool));
    for (int i = 0; i < bsp->nummodels; i++) {
        fence_dmodels[i] = Model_HasFence(bsp, &bsp->dmodels[i]);
    }
}

static void
BSP_MakeTnodes(const mbsp_t *bsp)
{
    bsp_static = bsp;
    tnodes = (tnode_t *) malloc(bsp->numnodes * sizeof(tnode_t));
    for (int i = 0; i < bsp->nummodels; i++)
        MakeTnodes_r(bsp->dmodels[i].headnode[0], bsp);
    
    faceinfos = (faceinfo_t *) malloc(bsp->numfaces * sizeof(faceinfo_t));
    for (int i = 0; i < bsp->numfaces; i++)
        MakeFaceInfo(bsp, BSP_GetFace(bsp, i), &faceinfos[i]);
    
    MakeFenceInfo(bsp);
}

/*
 * ============================================================================
 * FENCE TEXTURE TESTING
 * ============================================================================
 */

/**
 * Given a float texture coordinate, returns a pixel index to sample in [0, width-1].
 * This assumes the texture repeats and nearest filtering
 */
uint32_t clamp_texcoord(vec_t in, uint32_t width)
{
    if (in >= 0.0f)
    {
        return (uint32_t)in % width;
    }
    else
    {
        vec_t in_abs = ceil(fabs(in));
        uint32_t in_abs_mod = (uint32_t)in_abs % width;
        return (width - in_abs_mod) % width;
    }
}

color_rgba //mxd. int -> color_rgba
SampleTexture(const bsp2_dface_t *face, const mbsp_t *bsp, const vec3_t point)
{
    color_rgba sample{};
    if (!bsp->rgbatexdatasize)
        return sample;

    // FIXME: re-enable the following code
    return sample;
#if 0
    const auto *miptex = Face_Miptex(bsp, face);
    
    if (miptex == nullptr)
        return sample;
    
    const gtexinfo_t *tex = &bsp->texinfo[face->texinfo];

    vec_t texcoord[2];
    WorldToTexCoord(point, tex, texcoord);

    const int x = clamp_texcoord(texcoord[0], miptex->width);
    const int y = clamp_texcoord(texcoord[1], miptex->height);
    assert (x >= 0);
    assert (y >= 0);
    
    // FIXME: this is broken - palette index? color?
    // see: https://github.com/ericwa/ericw-tools/commit/0661098bc57d09b9961aa8314c52545a8f89a1e1#diff-dff5fe3d0288e49cabf1e7bc8fb28819c513be54cb7bbcdbea8b52ee0efd6bf5
    color_rgba *data = (color_rgba*)((uint8_t*)miptex + miptex->offset);
    sample = data[(miptex->width * y) + x];

    return sample;
#endif
}

/* assumes point is on the same plane as face */
static inline qboolean
TestHitFace(const faceinfo_t *fi, const vec3_t point)
{
    return EdgePlanes_PointInside(fi->face, fi->edgeplanes, point);
}

static inline bsp2_dface_t *
SearchNodeForHitFace(const bsp2_dnode_t *bspnode, const vec3_t point)
{
    // search the faces on this node
    int i;
    for (i=0; i<bspnode->numfaces; i++)
    {
        int facenum = bspnode->firstface + i;
        const faceinfo_t *fi = &faceinfos[facenum];
        
        if (SphereCullPoint(fi, point))
            continue;
        
        if (TestHitFace(fi, point)) {
            return &bsp_static->dfaces[facenum];
        }
    }
    return NULL;
}

/*
 * ============================================================================
 * LINE TRACING
 * The major lighting operation is a point to point visibility test, performed
 * by recursive subdivision of the line by the BSP tree.
 * ============================================================================
 */

typedef struct {
    vec3_t back;
    vec3_t front;
    int node;
    int side;
    const dplane_t *plane;
} tracestack_t;

/*
 * ==============
 * TraceLine
 * ==============
 */
#define MAX_TSTACK 256
static int
TraceLine(const dmodel_t *model, const int traceflags,
          const vec3_t start, const vec3_t stop)
{
    int node, side, tracehit;
    vec3_t front, back;
    vec_t frontdist, backdist;
    tracestack_t tracestack[MAX_TSTACK];
    tracestack_t *tstack, *crossnode;
    tnode_t *tnode;
    
    // Special case for bmodels with fence textures
    const int modelnum = model - &bsp_static->dmodels[0];
    if (modelnum != 0 && fence_dmodels[modelnum]) {
        traceinfo_t ti = {0};
        bool hit = TraceFaces(&ti, model->headnode[0], start, stop);
        if (hit && ti.hitsky && (traceflags & TRACE_HIT_SKY)) {
            return TRACE_HIT_SKY;
        } else if (hit && !ti.hitsky && (traceflags & TRACE_HIT_SOLID)) {
            return TRACE_HIT_SOLID;
        }
        return TRACE_HIT_NONE;
    }
    
    // FIXME: check for stack overflow
//    const tracestack_t *const tstack_max = tracestack + MAX_TSTACK;
    
    if (traceflags <= 0)
        Error("Internal error: %s - bad traceflags (%d)",
              __func__, traceflags);

    VectorCopy(start, front);
    VectorCopy(stop, back);

    tstack = tracestack;
    node = model->headnode[0];
    crossnode = NULL;
    tracehit = TRACE_HIT_NONE;

    while (1) {
        while (node < 0) {
            switch (node) {
            case CONTENTS_SOLID:
                if (traceflags & TRACE_HIT_SOLID)
                    tracehit = TRACE_HIT_SOLID;
                break;
            case CONTENTS_WATER:
                if (traceflags & TRACE_HIT_WATER)
                    tracehit = TRACE_HIT_WATER;
                break;
            case CONTENTS_SLIME:
                if (traceflags & TRACE_HIT_SLIME)
                    tracehit = TRACE_HIT_SLIME;
                break;
            case CONTENTS_LAVA:
                if (traceflags & TRACE_HIT_LAVA)
                    tracehit = TRACE_HIT_LAVA;
                break;
            case CONTENTS_SKY:
                if (traceflags & TRACE_HIT_SKY)
                    tracehit = TRACE_HIT_SKY;
                break;
            default:
                break;
            }
            if (tracehit != TRACE_HIT_NONE) {
                /* If we haven't crossed, start was inside flagged contents */
                if (!crossnode)
                    return -tracehit;

                return tracehit;
            }

            /* If the stack is empty, no obstructions were hit */
            if (tstack == tracestack)
                return TRACE_HIT_NONE;

            /* Pop the stack and go down the back side */
            crossnode = --tstack;
            VectorCopy(tstack->front, front);
            VectorCopy(tstack->back, back);
            node = tnodes[tstack->node].children[!tstack->side];
        }

        tnode = &tnodes[node];
        switch (tnode->type) {
        case PLANE_X:
            frontdist = front[0] - tnode->dist;
            backdist = back[0] - tnode->dist;
            break;
        case PLANE_Y:
            frontdist = front[1] - tnode->dist;
            backdist = back[1] - tnode->dist;
            break;
        case PLANE_Z:
            frontdist = front[2] - tnode->dist;
            backdist = back[2] - tnode->dist;
            break;
        default:
            frontdist = DotProduct(front, tnode->normal) - tnode->dist;
            backdist = DotProduct(back, tnode->normal) - tnode->dist;
            break;
        }

        if (frontdist >= -ON_EPSILON && backdist >= -ON_EPSILON) {
            node = tnode->children[0];
            continue;
        }
        if (frontdist < ON_EPSILON && backdist < ON_EPSILON) {
            node = tnode->children[1];
            continue;
        }

        /*
         * If we get here, we have a clean split with front and back on
         * opposite sides. The new back is the intersection point with the
         * node plane. Push the other segment onto the stack and continue.
         */
        side = frontdist < 0;
        tstack->node = node;
        tstack->side = side;
        tstack->plane = tnode->plane;
        VectorCopy(back, tstack->back);
        VectorSubtract(back, front, back);
        VectorMA(front, frontdist / (frontdist - backdist), back, back);
        VectorCopy(back, tstack->front);
        crossnode = tstack++;
        node = tnode->children[side];
    }
}

static qboolean
BSP_TestLight(const vec3_t start, const vec3_t stop, const dmodel_t *self)
{
    const int traceflags = TRACE_HIT_SOLID;
    int result = TRACE_HIT_NONE;

    /* Check against the list of global shadow casters */
    for (const modelinfo_t *model : tracelist) {
        if (model->model == self)
            continue;
        result = TraceLine(model->model, traceflags, start, stop);
        if (result != TRACE_HIT_NONE)
            break;
    }

    /* If not yet obscured, check against the self-shadow model */
    if (result == TRACE_HIT_NONE && self)
        result = TraceLine(self, traceflags, start, stop);

    return (result == TRACE_HIT_NONE);
}

static qboolean
BSP_TestSky(const vec3_t start, const vec3_t dirn, const dmodel_t *self)
{
    //const modelinfo_t *const *model;
    int traceflags = TRACE_HIT_SKY | TRACE_HIT_SOLID;
    int result = TRACE_HIT_NONE;
    vec3_t stop;

    /* Trace towards the sunlight for a sky brush */
    VectorAdd(dirn, start, stop);
    result = TraceLine(tracelist[0]->model, traceflags, start, stop);
    if (result != TRACE_HIT_SKY)
        return false;

    /* If good, check it isn't shadowed by another model */
    traceflags = TRACE_HIT_SOLID;
    for (const modelinfo_t *model : tracelist) {
        if (model == tracelist.at(0))
            continue;
        if (model->model == self)
            continue;
        result = TraceLine(model->model, traceflags, start, stop);
        if (result != TRACE_HIT_NONE)
            return false;
    }

    /* Check for self-shadowing */
    if (self) {
        result = TraceLine(self, traceflags, start, stop);
        if (result != TRACE_HIT_NONE)
            return false;
    }

    return true;
}

/*
 * ============
 * DirtTrace
 *
 * returns true if the trace from start to stop hits something solid,
 * or if it started in the void.
 * ============
 */
static hittype_t
BSP_DirtTrace(const vec3_t start, const vec3_t dirn, const vec_t dist, const dmodel_t *self, vec_t *hitdist_out, plane_t *hitplane_out, const bsp2_dface_t **face_out)
{
    vec3_t stop;
    VectorMA(start, dist, dirn, stop);

    traceinfo_t ti = {0};
    VectorCopy(dirn, ti.dir);
    
    if (self) {
        if (TraceFaces (&ti, self->headnode[0], start, stop)) {
            if (hitdist_out) {
                vec3_t delta;
                VectorSubtract(ti.point, start, delta);
                *hitdist_out = VectorLength(delta);
            }
            if (hitplane_out) {
                *hitplane_out = ti.hitplane;
            }
            if (face_out) {
                *face_out = ti.face;
            }
            return ti.hitsky ? hittype_t::SKY : hittype_t::SOLID;
        }
    }
    
    /* Check against the list of global shadow casters */
    for (const modelinfo_t *model : tracelist) {
        if (model->model == self)
            continue;
        if (TraceFaces (&ti, model->model->headnode[0], start, stop)) {
            if (hitdist_out) {
                vec3_t delta;
                VectorSubtract(ti.point, start, delta);
                *hitdist_out = VectorLength(delta);
            }
            if (hitplane_out) {
                *hitplane_out = ti.hitplane;
            }
            if (face_out) {
                *face_out = ti.face;
            }
            return ti.hitsky ? hittype_t::SKY : hittype_t::SOLID;
        }
    }
    
    return hittype_t::NONE;
}

static bool
BSP_IntersectSingleModel(const vec3_t start, const vec3_t dirn, vec_t dist, const dmodel_t *self, vec_t *hitdist_out)
{
    vec3_t stop;
    VectorMA(start, dist, dirn, stop);
    
    traceinfo_t ti = {0};
    VectorCopy(dirn, ti.dir);

    if (TraceFaces (&ti, self->headnode[0], start, stop)) {
        if (hitdist_out) {
            vec3_t delta;
            VectorSubtract(ti.point, start, delta);
            *hitdist_out = VectorLength(delta);
        }
        return true;
    }
    return false;
}

/*
=============
TraceFaces
 
From lordhavoc, johnfitz (RecursiveLightPoint)
=============
*/
static bool
TraceFaces (traceinfo_t *ti, int node, const vec3_t start, const vec3_t end)
{
    float		front, back, frac;
    vec3_t		mid;
    tnode_t             *tnode;
    
    if (node < 0)
        return false;		// didn't hit anything
    
    tnode = &tnodes[node]; //ericw
    
    // calculate mid point
    if (tnode->type < 3)
    {
        front = start[tnode->type] - tnode->dist;
        back = end[tnode->type] - tnode->dist;
    }
    else
    {
        front = DotProduct(start, tnode->normal) - tnode->dist;
        back = DotProduct(end, tnode->normal) - tnode->dist;
    }
    
    if ((back < 0) == (front < 0))
        return TraceFaces (ti, tnode->children[front < 0], start, end);
    
    frac = front / (front-back);
    mid[0] = start[0] + (end[0] - start[0])*frac;
    mid[1] = start[1] + (end[1] - start[1])*frac;
    mid[2] = start[2] + (end[2] - start[2])*frac;
    
    // go down front side
    if (TraceFaces (ti, tnode->children[front < 0], start, mid))
        return true;	// hit something
    else
    {
        // check for impact on this node
        VectorCopy (mid, ti->point);
        //ti->lightplane = tnode->plane;
        
        bsp2_dface_t *face = SearchNodeForHitFace(tnode->node, mid);
        if (face) {
            const int facenum = face - bsp_static->dfaces;
            const faceinfo_t *fi = &faceinfos[facenum];
            
            // check fence
            bool passedThroughFence = false;
            if (fi->texturename[0] == '{') {
                const color_rgba sample = SampleTexture(face, bsp_static, mid); //mxd. Palette index -> RGBA
                if (sample.a < 255) {
                    passedThroughFence = true;
                }
            }
            
            // only solid and sky faces stop the trace.
            bool issolid, issky; //mxd
            if(bsp_static->loadversion == Q2_BSPVERSION) {
                issolid = !(fi->content & Q2_SURF_TRANSLUCENT);
                issky = (fi->content & Q2_SURF_SKY);
            } else {
                issolid = (fi->content == CONTENTS_SOLID);
                issky = (fi->content == CONTENTS_SKY);
            }

            if (!passedThroughFence && (issolid || issky)) {
                ti->face = face;
                ti->hitsky = issky;
                VectorCopy(fi->plane.normal, ti->hitplane.normal);
                ti->hitplane.dist = fi->plane.dist;
                
                // check if we hit the back side
                ti->hitback = (DotProduct(ti->dir, fi->plane.normal) >= 0);
                
                return true;
            }
        }

        //ericw -- no impact found on this node.
        
        // go down back side
        return TraceFaces (ti, tnode->children[front >= 0], mid, end);
    }
}

//
// Embree wrappers
//

hitresult_t TestSky(const vec3_t start, const vec3_t dirn, const modelinfo_t *self, const bsp2_dface_t **face_out)
{
#ifdef HAVE_EMBREE
    if (rtbackend == backend_embree) {
        return Embree_TestSky(start, dirn, self, face_out);
    }
#endif
#if 0
    if (rtbackend == backend_bsp) {
        return BSP_TestSky(start, dirn, self);
    }
#endif
    Error("no backend available");
    throw; //mxd. Silences compiler warning
}

hitresult_t TestLight(const vec3_t start, const vec3_t stop, const modelinfo_t *self)
{
#ifdef HAVE_EMBREE
    if (rtbackend == backend_embree) {
        return Embree_TestLight(start, stop, self);
    }
#endif
#if 0
    if (rtbackend == backend_bsp) {
        return BSP_TestLight(start, stop, self);
    }
#endif
    Error("no backend available");
    throw; //mxd. Silences compiler warning
}


hittype_t DirtTrace(const vec3_t start, const vec3_t dirn, vec_t dist, const modelinfo_t *self, vec_t *hitdist_out, plane_t *hitplane_out, const bsp2_dface_t **face_out)
{
#ifdef HAVE_EMBREE
    if (rtbackend == backend_embree) {
        return Embree_DirtTrace(start, dirn, dist, self, hitdist_out, hitplane_out, face_out);
    }
#endif
#if 0
    if (rtbackend == backend_bsp) {
        return BSP_DirtTrace(start, dirn, dist, self, hitdist_out, hitplane_out, face_out);
    }
#endif
    Error("no backend available");
    throw; //mxd. Silences compiler warning
}

raystream_intersection_t *MakeIntersectionRayStream(int maxrays) {
    return Embree_MakeIntersectionRayStream(maxrays);
}
raystream_occlusion_t* MakeOcclusionRayStream(int maxrays) {
    return Embree_MakeOcclusionRayStream(maxrays);
}

void MakeTnodes(const mbsp_t *bsp)
{
    Embree_TraceInit(bsp);
}
