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
#include <light/entities.h>
#include <light/litfile.h>

#define ON_EPSILON    0.1
#define ANGLE_EPSILON 0.001

#define MAXLIGHTS 1024

qboolean TestSky(const vec3_t start, const vec3_t dirn, vec3_t skypoint);
qboolean TestLine(const vec3_t start, const vec3_t stop);
qboolean TestLineModel(const dmodel_t *model,
		       const vec3_t start, const vec3_t stop);

typedef struct {
    const dmodel_t *model;
    qboolean shadowself;
    int minlight;
    vec3_t mincolor;
    vec3_t offset;
} modelinfo_t;

/* tracelist is a null terminated array of BSP models to use for LOS tests */
extern const dmodel_t *const *tracelist;

void LightFace(int surfnum, const modelinfo_t *modelinfo);
void LightLeaf(dleaf_t * leaf);

void MakeTnodes(void);

extern float scaledist;
extern float rangescale;
extern float fadegate;
extern int worldminlight;
extern const vec3_t vec3_white;
extern vec3_t minlight_color;
extern int sunlight;
extern vec3_t sunlight_color;
extern vec3_t sunvec;

/*
 * Return space for the lightmap and colourmap at the same time so it can
 * be done in a thread-safe manner.
 */
void GetFileSpace(byte **lightdata, byte **colordata, int size);

extern byte *filebase;
extern byte *lit_filebase;

void TransformSample(vec3_t in, vec3_t out);
void RotateSample(vec3_t in, vec3_t out);

extern int oversample;
extern qboolean compress_ents;
extern qboolean facecounter;
extern qboolean colored;
extern qboolean bsp30;
extern qboolean litfile;
extern qboolean nominlimit;

#endif /* __LIGHT_LIGHT_H__ */
