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

#ifndef __LIGHT_ENTITIES_H__
#define __LIGHT_ENTITIES_H__

#include <common/mathlib.h>
#include <common/bspfile.h>
#include <light/light.h>

#define DEFAULTLIGHTLEVEL 300

typedef struct epair_s {
    struct epair_s *next;
    char key[MAX_ENT_KEY];
    char value[MAX_ENT_VALUE];
} epair_t;

/*
 * Light attenuation formalae
 * (relative to distance 'x' from the light source)
 */
#define LF_SCALE 128
typedef enum {
    LF_LINEAR    = 0,	/* Linear (x) (DEFAULT) */
    LF_INVERSE   = 1,	/* Inverse (1/x), scaled by 1/128 */
    LF_INVERSE2  = 2,	/* Inverse square (1/(x^2)), scaled by 1/(128^2) */
    LF_INFINITE  = 3,	/* No attenuation, same brightness at any distance */
    LF_LOCALMIN  = 4,	/* No attenuation, non-additive minlight effect within
			   line of sight of the light source. */
    LF_INVERSE2A = 5,	/* Inverse square, with distance adjusted to avoid
			   exponentially bright values near the source.
			     (1/(x+128)^2), scaled by 1/(128^2) */
    LF_COUNT
} light_formula_t;

typedef struct entity_s {
    char classname[MAX_ENT_VALUE];
    vec3_t origin;

    qboolean spotlight;
    vec3_t spotvec;
    float spotangle;
    float spotfalloff;
    float spotangle2;
    float spotfalloff2;

    lightsample_t light;
    light_formula_t formula;
    vec_t fadedist;
    float atten;
    float anglescale;
    int style;

    char target[MAX_ENT_VALUE];
    char targetname[MAX_ENT_VALUE];
    struct epair_s *epairs;
    const struct entity_s *targetent;
} entity_t;

/*
 * atten:
 *    Takes a float as a value (default 1.0).
 *    This reflects how fast a light fades with distance.
 *    For example a value of 2 will fade twice as fast, and a value of 0.5
 *      will fade half as fast. (Just like arghlite)
 *
 *  mangle:
 *    If the entity is a light, then point the spotlight in this direction.
 *    If it is the worldspawn, then this is the sunlight mangle
 *
 *  lightcolor:
 *    Stores the RGB values to determine the light color
 */

extern entity_t *entities;
extern int num_entities;

entity_t *FindEntityWithKeyPair(const char *key, const char *value);
const char *ValueForKey(const entity_t *ent, const char *key);
void GetVectorForKey(const entity_t *ent, const char *key, vec3_t vec);

void LoadEntities(const bsp2_t *bsp);
void WriteEntitiesToString(bsp2_t *bsp);

#endif /* __LIGHT_ENTITIES_H__ */
