/* common/polylib.h */

#ifndef __COMMON_POLYLIB_H__
#define __COMMON_POLYLIB_H__

typedef struct {
    int numpoints;
    vec3_t p[4];		/* variable sized */
} winding_t;

#define MAX_POINTS_ON_WINDING 64
#define ON_EPSILON 0.1

winding_t *AllocWinding(int points);
vec_t WindingArea(winding_t * w);
void WindingCenter(winding_t * w, vec3_t center);
void ClipWinding(winding_t * in, vec3_t normal, vec_t dist,
		 winding_t ** front, winding_t ** back);
winding_t *ChopWinding(winding_t * in, vec3_t normal, vec_t dist);
winding_t *CopyWinding(winding_t * w);
winding_t *BaseWindingForPlane(vec3_t normal, float dist);
void CheckWinding(winding_t * w);
void WindingPlane(winding_t * w, vec3_t normal, vec_t *dist);
void RemoveColinearPoints(winding_t * w);

#endif /* __COMMON_POLYLIB_H__ */
