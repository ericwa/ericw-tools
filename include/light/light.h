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

#ifndef __LIGHT_LIGHT_H__
#define __LIGHT_LIGHT_H__

#include <common/cmdlib.h>
#include <common/mathlib.h>
#include <common/bspfile.h>
#include <common/log.h>
#include <common/threads.h>
#include <light/litfile.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ON_EPSILON    0.1
#define ANGLE_EPSILON 0.001

#define TRACE_HIT_NONE  0
#define TRACE_HIT_SOLID (1 << 0)
#define TRACE_HIT_WATER (1 << 1)
#define TRACE_HIT_SLIME (1 << 2)
#define TRACE_HIT_LAVA  (1 << 3)
#define TRACE_HIT_SKY   (1 << 4)


typedef struct traceinfo_s {
    vec3_t			point;
    const bsp2_dface_t          *face;
    /* returns true if sky was hit. */
    bool hitsky;
    bool hitback;
    
    // internal
    vec3_t dir;
} traceinfo_t;

typedef struct {
    const dplane_t *dplane;
    int side;
    vec3_t point;
} tracepoint_t;

/*
 * ---------
 * TraceLine
 * ---------
 * Generic BSP model ray tracing function. Traces a ray from start towards
 * stop. If the trace line hits one of the flagged contents along the way, the
 * corresponding TRACE flag will be returned. Furthermore, if hitpoint is
 * non-null, information about the point the ray hit will be filled in.
 *
 *  model    - The bsp model to trace against
 *  flags    - contents which will stop the trace (must be > 0)
 *  start    - coordinates to start trace
 *  stop     - coordinates to end the trace
 *  hitpoint - filled in if result > 0 and hitpoint is non-NULL
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
int TraceLine(const dmodel_t *model, const int traceflags,
              const vec3_t start, const vec3_t end, tracepoint_t *hitpoint);

/*
 * Convenience functions TestLight and TestSky will test against all shadow
 * casting bmodels and self-shadow the model 'self' if self != NULL. Returns
 * true if sky or light is visible, respectively.
 */
qboolean TestSky(const vec3_t start, const vec3_t dirn, const dmodel_t *self);
qboolean TestLight(const vec3_t start, const vec3_t stop, const dmodel_t *self);
qboolean DirtTrace(const vec3_t start, const vec3_t stop, const dmodel_t *self, vec3_t hitpoint_out);

typedef struct {
    vec_t light;
    vec3_t color;
    vec3_t direction;
} lightsample_t;

typedef struct {
    const dmodel_t *model;
    qboolean shadowself;
    lightsample_t minlight;
    float lightmapscale;
    vec3_t offset;
    qboolean nodirt;
    vec_t phongangle;
} modelinfo_t;
    
void MakeTnodes_embree(const bsp2_t *bsp);
    
// returns true if un-occluded. dir is the direction to trace in (doesn't need to be normalized)
qboolean
TestLight_embree(const vec3_t start, const vec3_t dir, vec_t dist, const modelinfo_t *model);

// returns true if sky is visible. dirn must be the normalized direction _away_ from the sun
qboolean
TestSky_embree(const vec3_t start, const vec3_t dirn, const modelinfo_t *model);

// returns true if occluded
qboolean
DirtTrace_embree(const vec3_t start, const vec3_t dir, vec_t dist, vec_t *hitdist, vec_t *normal, const modelinfo_t *model);

qboolean
FaceTrace_embree(const vec3_t start, const vec3_t dir, vec3_t hitpoint, const bsp2_dface_t **hitface);

    
// returns true if the trace from start to stop hits something solid.
// only tests the selfshadow model.
qboolean
CalcPointsTrace_embree(const vec3_t start, const vec3_t dir, vec_t dist, vec_t *hitdist, vec_t *normal, const modelinfo_t *model);

    
typedef struct sun_s {
    vec3_t sunvec;
    lightsample_t sunlight;
    struct sun_s *next;
    qboolean dirt;
    float anglescale;
} sun_t;

typedef struct {
    vec3_t normal;
    vec_t dist;
} plane_t;

/* for vanilla this would be 18. some engines allow higher limits though, which will be needed if we're scaling lightmap resolution. */
/*with extra sampling, lit+lux etc, we need at least 46mb stack space per thread. yes, that's a lot. on the plus side, it doesn't affect bsp complexity (actually, can simplify it a little)*/
#define MAXDIMENSION (255+1)

/* Allow space for 4x4 oversampling */
//#define SINGLEMAP (MAXDIMENSION*MAXDIMENSION*4*4)

typedef struct {
    vec3_t data[3];     /* permuted 3x3 matrix */
    int row[3];         /* row permutations */
    int col[3];         /* column permutations */
} pmatrix3_t;
    
typedef struct {
    pmatrix3_t transform;
    const texinfo_t *texinfo;
    vec_t planedist;
} texorg_t;
    
/*Warning: this stuff needs explicit initialisation*/
typedef struct {
    const modelinfo_t *modelinfo;
    /* these take precedence the values in modelinfo */
    lightsample_t minlight;
    qboolean nodirt;
    
    plane_t plane;
    vec3_t snormal;
    vec3_t tnormal;
    
    /* 16 in vanilla. engines will hate you if this is not power-of-two-and-at-least-one */
    float lightmapscale;
    qboolean curved; /*normals are interpolated for smooth lighting*/
    
    int texmins[2];
    int texsize[2];
    vec_t exactmid[2];
    vec3_t midpoint;
    
    int numpoints;
    vec3_t *points; // malloc'ed array of numpoints
    vec3_t *normals; // malloc'ed array of numpoints
    bool *occluded; // malloc'ed array of numpoints
    
    /*
     raw ambient occlusion amount per sample point, 0-1, where 1 is
     fully occluded. dirtgain/dirtscale are not applied yet
     */
    vec_t *occlusion; // malloc'ed array of numpoints
    
    /* for sphere culling */
    vec3_t origin;
    vec_t radius;
    
    /* stuff used by CalcPoint */
    vec_t starts, startt, st_step;
    texorg_t texorg;
} lightsurf_t;

typedef struct {
    int style;
    lightsample_t *samples; // malloc'ed array of numpoints   //FIXME: this is stupid, we shouldn't need to allocate extra data here for -extra4
} lightmap_t;

struct ltface_ctx
{
    const bsp2_t *bsp;
    lightsurf_t lightsurf;
    lightmap_t lightmaps[MAXLIGHTMAPS + 1];
};

/* tracelist is a null terminated array of BSP models to use for LOS tests */
extern const modelinfo_t *const *tracelist;
extern const modelinfo_t *const *selfshadowlist;

struct ltface_ctx;
struct ltface_ctx *LightFaceInit(const bsp2_t *bsp);
void LightFaceShutdown(struct ltface_ctx *ctx);
const modelinfo_t *ModelInfoForFace(const bsp2_t *bsp, int facenum);
void LightFace(bsp2_dface_t *face, facesup_t *facesup, const modelinfo_t *modelinfo, struct ltface_ctx *ctx);
void MakeTnodes(const bsp2_t *bsp);

/* access the final phong-shaded vertex normal */
const vec_t *GetSurfaceVertexNormal(const bsp2_t *bsp, const bsp2_dface_t *f, const int v);
    
extern float scaledist;
extern float rangescale;
extern float anglescale;
extern float sun_anglescale;
extern float fadegate;
extern int softsamples;
extern float lightmapgamma;
extern const vec3_t vec3_white;
extern float surflight_subdivide;
extern int sunsamples;

extern qboolean addminlight;
extern lightsample_t minlight;

extern sun_t *suns;

/* dirt */

extern qboolean dirty;          // should any dirtmapping take place?
extern qboolean dirtDebug;
extern int dirtMode;
extern float dirtDepth;
extern float dirtScale;
extern float dirtGain;
extern float dirtAngle;

extern qboolean globalDirt;     // apply dirt to all lights (unless they override it)?
extern qboolean minlightDirt;   // apply dirt to minlight?

extern qboolean dirtModeSetOnCmdline;
extern qboolean dirtDepthSetOnCmdline;
extern qboolean dirtScaleSetOnCmdline;
extern qboolean dirtGainSetOnCmdline;
extern qboolean dirtAngleSetOnCmdline;

/*
 * Return space for the lightmap and colourmap at the same time so it can
 * be done in a thread-safe manner.
 */
void GetFileSpace(byte **lightdata, byte **colordata, byte **deluxdata, int size);

extern byte *filebase;
extern byte *lit_filebase;
extern byte *lux_filebase;

extern int oversample;
extern int write_litfile;
extern int write_luxfile;
extern qboolean onlyents;
extern qboolean parse_escape_sequences;
extern qboolean scaledonly;
extern unsigned int lightturb;
extern uint32_t *extended_texinfo_flags;

void SetupDirt();

/* Used by fence texture sampling */
void WorldToTexCoord(const vec3_t world, const texinfo_t *tex, vec_t coord[2]);

extern qboolean testFenceTextures;
extern qboolean surflight_dump;
extern qboolean phongDebug;

extern char mapfilename[1024];

#ifdef __cplusplus
}
#endif

#endif /* __LIGHT_LIGHT_H__ */
