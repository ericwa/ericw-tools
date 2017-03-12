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
    const bsp2_dleaf_t *childleafs[2];
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
static const bsp2_t *bsp_static;

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
const bsp2_dleaf_t *
Light_PointInLeaf( const bsp2_t *bsp, const vec3_t point )
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
int Light_PointContents( const bsp2_t *bsp, const vec3_t point )
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
MakeTnodes_r(int nodenum, const bsp2_t *bsp)
{
    tnode_t *tnode;
    int i;
    bsp2_dnode_t *node;
    bsp2_dleaf_t *leaf;

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
MakeFaceInfo(const bsp2_t *bsp, const bsp2_dface_t *face, faceinfo_t *info)
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
Model_HasFence(const bsp2_t *bsp, const dmodel_t *model)
{
    for (int j = model->firstface; j < model->firstface + model->numfaces; j++) {
        const bsp2_dface_t *face = &bsp->dfaces[j];
        if (Face_TextureName(bsp, face)[0] == '{') {
            return true;
        }
    }
    return false;
}

static void
MakeFenceInfo(const bsp2_t *bsp)
{
    fence_dmodels = (bool *) calloc(bsp->nummodels, sizeof(bool));
    for (int i = 0; i < bsp->nummodels; i++) {
        fence_dmodels[i] = Model_HasFence(bsp, &bsp->dmodels[i]);
    }
}

static void
BSP_MakeTnodes(const bsp2_t *bsp)
{
    bsp_static = bsp;
    tnodes = (tnode_t *) malloc(bsp->numnodes * sizeof(tnode_t));
    for (int i = 0; i < bsp->nummodels; i++)
        MakeTnodes_r(bsp->dmodels[i].headnode[0], bsp);
    
    faceinfos = (faceinfo_t *) malloc(bsp->numfaces * sizeof(faceinfo_t));
    for (int i = 0; i < bsp->numfaces; i++)
        MakeFaceInfo(bsp, &bsp->dfaces[i], &faceinfos[i]);
    
    MakeFenceInfo(bsp);
}

/*
 * ============================================================================
 * FENCE TEXTURE TESTING
 * ============================================================================
 */

static uint32_t fix_coord(vec_t in, uint32_t width)
{
    if (in > 0)
    {
        return (uint32_t)in % width;
    }
    else
    {
        vec_t in_abs = fabs(in);
        uint32_t in_abs_mod = (uint32_t)in_abs % width;
        return width - in_abs_mod;
    }
}

int
SampleTexture(const bsp2_dface_t *face, const bsp2_t *bsp, const vec3_t point)
{
    vec_t texcoord[2];
    const texinfo_t *tex;
    const miptex_t *miptex;
    int x, y;
    byte *data;
    int sample;

    if (!bsp->texdatasize)
        return -1;
    
    miptex = Face_Miptex(bsp, face);
    
    if (miptex == NULL)
        return -1;
    
    tex = &bsp->texinfo[face->texinfo];

    WorldToTexCoord(point, tex, texcoord);

    x = fix_coord(texcoord[0], miptex->width);
    y = fix_coord(texcoord[1], miptex->height);
    assert (x >= 0);
    assert (y >= 0);
    
    data = (byte*)miptex + miptex->offsets[0];
    sample = data[(miptex->width * y) + x];

    return sample;
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
    const modelinfo_t *const *model;
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
            int facenum = face - bsp_static->dfaces;
            const faceinfo_t *fi = &faceinfos[facenum];
            
            // check fence
            bool passedThroughFence = false;
            if (fi->texturename[0] == '{') {
                const int sample = SampleTexture(face, bsp_static, mid);
                if (sample == 255) {
                    passedThroughFence = true;
                }
            }
            
            // only solid and sky faces stop the trace.
            if (!passedThroughFence &&
                (fi->content == CONTENTS_SOLID || fi->content == CONTENTS_SKY)) {
                ti->face = face;
                ti->hitsky = (fi->content == CONTENTS_SKY);
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

qboolean TestSky(const vec3_t start, const vec3_t dirn, const dmodel_t *self)
{
#ifdef HAVE_EMBREE
    if (rtbackend == backend_embree) {
        return Embree_TestSky(start, dirn, self);
    }
#endif
    if (rtbackend == backend_bsp) {
        return BSP_TestSky(start, dirn, self);
    }
    Error("no backend available");
}

qboolean TestLight(const vec3_t start, const vec3_t stop, const dmodel_t *self)
{
#ifdef HAVE_EMBREE
    if (rtbackend == backend_embree) {
        return Embree_TestLight(start, stop, self);
    }
#endif
    if (rtbackend == backend_bsp) {
        return BSP_TestLight(start, stop, self);
    }
    Error("no backend available");
}


hittype_t DirtTrace(const vec3_t start, const vec3_t dirn, vec_t dist, const dmodel_t *self, vec_t *hitdist_out, plane_t *hitplane_out, const bsp2_dface_t **face_out)
{
#ifdef HAVE_EMBREE
    if (rtbackend == backend_embree) {
        return Embree_DirtTrace(start, dirn, dist, self, hitdist_out, hitplane_out, face_out);
    }
#endif
    if (rtbackend == backend_bsp) {
        return BSP_DirtTrace(start, dirn, dist, self, hitdist_out, hitplane_out, face_out);
    }
    Error("no backend available");
}

class bsp_ray_t {
public:
    int _pointindex;
    vec3_t _origin, _dir;
    float _maxdist;
    const dmodel_t *_selfshadow;
    vec3_t _color;
    vec3_t _normalcontrib;
    
    // hit info
    float _hitdist;
    hittype_t _hittype;
    const bsp2_dface_t *_hitface;
    bool _hit_occluded;
    
    bsp_ray_t(int i, const vec_t *origin, const vec3_t dir, float dist, const dmodel_t *selfshadow, const vec_t *color, const vec_t *normalcontrib) :
        _pointindex{i},
        _maxdist{dist},
        _selfshadow{selfshadow},
        _hitdist{dist},
        _hittype{hittype_t::NONE},
        _hitface(nullptr),
        _hit_occluded{false} {
			VectorCopy(origin, _origin);
			VectorCopy(dir, _dir);
            if (color != nullptr) {
                VectorCopy(color, _color);
            }
            if (normalcontrib != nullptr) {
                VectorCopy(normalcontrib, _normalcontrib);
            }
        }
};

class raystream_bsp_t : public raystream_t {
private:
    std::vector<bsp_ray_t> _rays;
    int _maxrays;
    
public:
    raystream_bsp_t(int maxRays) :
    _maxrays { maxRays } {}
    
    raystream_bsp_t() {}
    
    virtual void pushRay(int i, const vec_t *origin, const vec3_t dir, float dist, const dmodel_t *selfshadow, const vec_t *color = nullptr, const vec_t *normalcontrib = nullptr) {
        bsp_ray_t r { i, origin, dir, dist, selfshadow, color, normalcontrib };
        _rays.push_back(r);
        Q_assert(_rays.size() <= _maxrays);
    }
    
    virtual size_t numPushedRays() {
        return _rays.size();
    }
    
    virtual void tracePushedRaysOcclusion() {
        if (!_rays.size())
            return;
        
        for (bsp_ray_t &ray : _rays) {
            vec3_t stop;
            VectorMA(ray._origin, ray._maxdist, ray._dir, stop);
            ray._hit_occluded = !BSP_TestLight(ray._origin, stop, ray._selfshadow);
        }
    }
    
    virtual void tracePushedRaysIntersection() {
        if (!_rays.size())
            return;
        
        for (bsp_ray_t &ray : _rays) {
            ray._hittype = BSP_DirtTrace(ray._origin, ray._dir, ray._maxdist, ray._selfshadow, &ray._hitdist, nullptr, &ray._hitface);
        }
    }
    
    virtual bool getPushedRayOccluded(size_t j) {
        return _rays.at(j)._hit_occluded;
    }
    
    virtual float getPushedRayDist(size_t j) {
        return _rays.at(j)._maxdist;
    }
    
    virtual float getPushedRayHitDist(size_t j) {
        return _rays.at(j)._hitdist;
    }
    
    virtual hittype_t getPushedRayHitType(size_t j) {
        return _rays.at(j)._hittype;
    }
    
    virtual const bsp2_dface_t *getPushedRayHitFace(size_t j) {
        return _rays.at(j)._hitface;
    }
    
    virtual void getPushedRayDir(size_t j, vec3_t out) {
        for (int i=0; i<3; i++) {
            out[i] = _rays.at(j)._dir[i];
        }
    }
    
    virtual int getPushedRayPointIndex(size_t j) {
        return _rays.at(j)._pointindex;
    }
    
    virtual void getPushedRayColor(size_t j, vec3_t out) {
        VectorCopy(_rays.at(j)._color, out);
    }
    
    virtual void getPushedRayNormalContrib(size_t j, vec3_t out) {
        VectorCopy(_rays.at(j)._normalcontrib, out);
    }
    
    virtual void clearPushedRays() {
        _rays.clear();
    }
};

raystream_t *BSP_MakeRayStream(int maxrays)
{
    return new raystream_bsp_t{maxrays};
}

raystream_t *MakeRayStream(int maxrays)
{
#ifdef HAVE_EMBREE
    if (rtbackend == backend_embree) {
        return Embree_MakeRayStream(maxrays);
    }
#endif
    if (rtbackend == backend_bsp) {
        return BSP_MakeRayStream(maxrays);
    }
    Error("no backend available");
}

void MakeTnodes(const bsp2_t *bsp)
{
#ifdef HAVE_EMBREE
    if (rtbackend == backend_embree) {
        Embree_TraceInit(bsp);
        return;
    }
#endif
    if (rtbackend == backend_bsp) {
        BSP_MakeTnodes(bsp);
        return;
    }
    Error("no backend available");
}
