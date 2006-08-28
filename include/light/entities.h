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

#include <common/bspfile.h>

#define DEFAULTLIGHTLEVEL 300

typedef struct epair_s {
    struct epair_s *next;
    char key[MAX_ENT_KEY];
    char value[MAX_ENT_VALUE];
} epair_t;

typedef struct entity_s {
    char classname[MAX_ENT_VALUE];
    vec3_t origin;
    float angle;

    /* TYR - added fields */
    int formula;
    float atten;
    vec3_t mangle;
    qboolean use_mangle;
    vec3_t lightcolor;

    int light;
    int style;
    char target[MAX_ENT_VALUE];
    char targetname[MAX_ENT_VALUE];
    struct epair_s *epairs;
    struct entity_s *targetent;
} entity_t;

/* Explanation of values added to struct entity_s
 *
 * formula:
 *    takes a value 0-3 (default 0)
 *    0 - Standard lighting formula like original light
 *    1 - light fades as 1/x
 *    2 - light fades as 1/(x^2)
 *    3 - Light stays same brightness reguardless of distance
 *
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

extern entity_t entities[MAX_MAP_ENTITIES];
extern int num_entities;

entity_t *FindEntityWithKeyPair(const char *key, const char *value);
char *ValueForKey(const entity_t *ent, const char *key);
void GetVectorForKey(const entity_t *ent, const char *key, vec3_t vec);

void LoadEntities(void);
void WriteEntitiesToString(void);

#endif /* __LIGHT_ENTITIES_H__ */
