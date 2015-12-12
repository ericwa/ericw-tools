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

#define ON_EPSILON    0.1
#define ANGLE_EPSILON 0.001

#define TRACE_HIT_NONE  0
#define TRACE_HIT_SOLID (1 << 0)
#define TRACE_HIT_WATER (1 << 1)
#define TRACE_HIT_SLIME (1 << 2)
#define TRACE_HIT_LAVA  (1 << 3)
#define TRACE_HIT_SKY   (1 << 4)

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
} modelinfo_t;

typedef struct sun_s {
    vec3_t sunvec;
    lightsample_t sunlight;
    struct sun_s *next;
    qboolean dirt;
    float anglescale;
} sun_t;

/* tracelist is a null terminated array of BSP models to use for LOS tests */
extern const dmodel_t *const *tracelist;

struct ltface_ctx;
struct ltface_ctx *LightFaceInit(const bsp2_t *bsp);
void LightFaceShutdown(struct ltface_ctx *ctx);
void LightFace(bsp2_dface_t *face, facesup_t *facesup, const modelinfo_t *modelinfo, struct ltface_ctx *ctx);
void MakeTnodes(const bsp2_t *bsp);

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

void SetupDirt();

/* Used by fence texture sampling */
void WorldToTexCoord(const vec3_t world, const texinfo_t *tex, vec_t coord[2]);

extern qboolean testFenceTextures;

#endif /* __LIGHT_LIGHT_H__ */
