/* common/polylib.h */

#ifndef __COMMON_POLYLIB_H__
#define __COMMON_POLYLIB_H__

#include <common/mathlib.hh>
#include <common/bspfile.hh>

namespace polylib {

typedef struct {
    int numpoints;
    vec3_t p[4];                /* variable sized */
} winding_t;

typedef struct {
    int numedges;
    plane_t *planes;
} winding_edges_t;
    
#define MAX_POINTS_ON_WINDING 64
#define ON_EPSILON 0.1f //mxd. Changed from 0.1 to silence compiler warning

winding_t *AllocWinding(int points);
vec_t WindingArea(const winding_t * w);
void WindingCenter(const winding_t * w, vec3_t center);
void WindingBounds (const winding_t *w, vec3_t mins, vec3_t maxs);
void ClipWinding(const winding_t * in, const vec3_t normal, vec_t dist,
                 winding_t ** front, winding_t ** back);
winding_t *ChopWinding(winding_t * in, vec3_t normal, vec_t dist);
winding_t *CopyWinding(const winding_t * w);
winding_t *BaseWindingForPlane(const vec3_t normal, float dist);
void CheckWinding(const winding_t * w);
void WindingPlane(const winding_t * w, vec3_t normal, vec_t *dist);
void RemoveColinearPoints(winding_t * w);

typedef void (*save_winding_fn_t)(winding_t *w, void *userinfo);
void DiceWinding (winding_t *w, vec_t subdiv, save_winding_fn_t save_fn, void *userinfo);
    
winding_t *WindingFromFace (const mbsp_t *bsp, const bsp2_dface_t *f);

winding_edges_t *AllocWindingEdges(const winding_t *w);
void FreeWindingEdges(winding_edges_t *wi);
bool PointInWindingEdges(const winding_edges_t *wi, const vec3_t point);

std::vector<qvec3f> GLM_WindingPoints(const winding_t *w);
    
};

#endif /* __COMMON_POLYLIB_H__ */
