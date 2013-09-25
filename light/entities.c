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

#include <string.h>
#include <common/cmdlib.h>

#include <light/light.h>
#include <light/entities.h>

entity_t *entities;
int num_entities;
static int max_entities;

/*
 * ============================================================================
 * ENTITY FILE PARSING
 * If a light has a targetname, generate a unique style in the 32-63 range
 * ============================================================================
 */

#define MAX_LIGHT_TARGETS 32

static int numlighttargets;
static char lighttargets[MAX_LIGHT_TARGETS][MAX_ENT_VALUE];

static void
SetKeyValue(entity_t *ent, const char *key, const char *value)
{
    epair_t *ep;

    for (ep = ent->epairs; ep; ep = ep->next)
	if (!strcmp(ep->key, key)) {
	    strcpy(ep->value, value);
	    return;
	}
    ep = malloc(sizeof(*ep));
    ep->next = ent->epairs;
    ent->epairs = ep;
    strcpy(ep->key, key);
    strcpy(ep->value, value);
}

static int
LightStyleForTargetname(const char *targetname)
{
    int i;

    for (i = 0; i < numlighttargets; i++)
	if (!strcmp(lighttargets[i], targetname))
	    return 32 + i;
    if (i == MAX_LIGHT_TARGETS)
	Error("%s: Too many unique light targetnames\n", __func__);

    strcpy(lighttargets[i], targetname);
    numlighttargets++;

    return numlighttargets - 1 + 32;
}

/*
 * ==================
 * MatchTargets
 * ==================
 */
static void
MatchTargets(void)
{
    int i, j;
    entity_t *entity;
    const entity_t *target;

    for (i = 0, entity = entities; i < num_entities; i++, entity++) {
	if (!entity->target[0])
	    continue;
	for (j = 0, target = entities; j < num_entities; j++, target++) {
	    if (!strcmp(target->targetname, entity->target)) {
		entity->targetent = target;
		break;
	    }
	}
	if (j == num_entities) {
	    logprint("WARNING: entity at (%s) (%s) has unmatched "
		     "target (%s)\n", VecStr(entity->origin),
		     entity->classname, entity->target);
	    continue;
	}

	/* set the style on the source ent for switchable lights */
	if (target->style) {
	    char style[10];
	    entity->style = target->style;
	    snprintf(style, sizeof(style), "%d", entity->style);
	    SetKeyValue(entity, "style", style);
	}
    }
}

static void
SetupSpotlights(void)
{
    int i;
    entity_t *entity;

    for (i = 0, entity = entities; i < num_entities; i++, entity++) {
	if (strncmp(entity->classname, "light", 5))
	    continue;
	if (entity->targetent) {
	    VectorSubtract(entity->targetent->origin, entity->origin,
			   entity->spotvec);
	    VectorNormalize(entity->spotvec);
	    entity->spotlight = true;
	}
	if (entity->spotlight) {
	    vec_t angle, angle2;

	    angle = (entity->spotangle > 0) ? entity->spotangle : 40;
	    entity->spotfalloff = -cos(angle / 2 * Q_PI / 180);

	    angle2 = entity->spotangle2;
	    if (angle2 <= 0 || angle2 > angle)
		angle2 = angle;
	    entity->spotfalloff2 = -cos(angle2 / 2 * Q_PI / 180);
	}
    }
}

/* helper function */
static void
scan_vec3(vec3_t dest, const char *buf, const char *name)
{
    int i;
    double vec[3] = { 0.0, 0.0, 0.0 };

    if (sscanf(buf, "%lf %lf %lf", &vec[0], &vec[1], &vec[2]) != 3)
	logprint("WARNING: Not 3 values for %s\n", name);
    for (i = 0; i < 3; ++i)
	dest[i] = vec[i];
}

static void
vec_from_mangle(vec3_t v, const vec3_t m)
{
    vec3_t tmp;

    VectorScale(m, Q_PI / 180, tmp);
    v[0] = cos(tmp[0]) * cos(tmp[1]);
    v[1] = sin(tmp[0]) * cos(tmp[1]);
    v[2] = sin(tmp[1]);
}

static void
CheckEntityFields(entity_t *entity)
{
    if (!entity->light.light)
	entity->light.light = DEFAULTLIGHTLEVEL;
    if (entity->atten <= 0.0)
	entity->atten = 1.0;
    if (entity->anglescale < 0 || entity->anglescale > 1.0)
	entity->anglescale = anglescale;

    if (entity->formula < LF_LINEAR || entity->formula >= LF_COUNT) {
	static qboolean warned_once = true;
	if (!warned_once) {
	    warned_once = true;
	    logprint("WARNING: unknown formula number (%d) in delay field\n"
		     "   %s at (%s)\n"
		     "   (further formula warnings will be supressed)\n",
		     entity->formula, entity->classname,
		     VecStr(entity->origin));
	}
	entity->formula = LF_LINEAR;
    }

    if (!VectorCompare(entity->light.color, vec3_origin)) {
	if (!write_litfile) {
	    write_litfile = true;
	    logprint("Colored light entities detected: "
		     ".lit output enabled.\n");
	}
    } else {
	VectorCopy(vec3_white, entity->light.color);
    }

    if (entity->formula == LF_LINEAR) {
	/* Linear formula always has a falloff point */
	entity->fadedist = fabs(entity->light.light) - fadegate;
	entity->fadedist = entity->fadedist / entity->atten / scaledist;
    } else if (fadegate < EQUAL_EPSILON) {
	/* If fadegate is tiny, other lights have effectively infinite reach */
	entity->fadedist = VECT_MAX;
    } else {
	/* Calculate the distance at which brightness falls to zero */
	switch (entity->formula) {
	case LF_INFINITE:
	case LF_LOCALMIN:
	    entity->fadedist = VECT_MAX;
	    break;
	case LF_INVERSE:
	    entity->fadedist = entity->light.light * entity->atten * scaledist;
	    entity->fadedist *= LF_SCALE / fadegate;
	    entity->fadedist = fabs(entity->fadedist);
	    break;
	case LF_INVERSE2:
	    entity->fadedist = entity->light.light * entity->atten * scaledist;
	    entity->fadedist *= LF_SCALE / sqrt(fadegate);
	    entity->fadedist = fabs(entity->fadedist);
	    break;
	case LF_INVERSE2A:
	    entity->fadedist = entity->light.light * entity->atten * scaledist;
	    entity->fadedist -= LF_SCALE;
	    entity->fadedist *= LF_SCALE / sqrt(fadegate);
	    entity->fadedist = fabs(entity->fadedist);
	    break;
	default:
	    Error("Internal error: formula not handled in %s", __func__);
	}
    }
}

/*
 * Quick count of entities.
 * Assumes correct syntax, etc.
 */
static int
CountEntities(const char *entitystring)
{
    const char *pos = entitystring;
    int count = 0;

    while (1) {
	pos += strcspn(pos, "/{");
	if (!*pos)
	    return count;

	/* It's probably overkill to consider comments, but... */
	if (*pos == '/') {
	    pos++;
	    if (*pos == '*') {
		pos++;
		while (1) {
		    pos = strchr(pos, '*');
		    if (!pos)
			return count;
		    if (pos[1] == '/') {
			pos += 2;
			break;
		    }
		}
	    } else if (*pos == '/') {
		pos = strchr(pos, '\n');
		if (!pos)
		    return count;
	    }
	    continue;
	}

	/* Add one entity for every opening brace */
	count++;
	pos++;
    }
}

/*
 * ==================
 * LoadEntities
 * ==================
 */
void
LoadEntities(const bsp2_t *bsp)
{
    char *data;
    entity_t *entity;
    char key[MAX_ENT_KEY];
    epair_t *epair;
    vec3_t vec;
    int memsize, num_lights;

    /* Count the entities and allocate memory */
    max_entities = CountEntities(bsp->dentdata);
    memsize = max_entities * sizeof(*entities);
    entities = malloc(memsize);
    if (!entities)
	Error("%s: allocation of %d bytes failed\n", __func__, memsize);
    memset(entities, 0, memsize);

    /* start parsing */
    num_entities = 0;
    num_lights = 0;
    data = bsp->dentdata;

    /* go through all the entities */
    while (1) {
	/* parse the opening brace */
	data = COM_Parse(data);
	if (!data)
	    break;
	if (com_token[0] != '{')
	    Error("%s: found %s when expecting {", __func__, com_token);

	if (num_entities == max_entities)
	    Error("%s: Internal Error - exceeded max_entities", __func__);
	entity = &entities[num_entities];
	num_entities++;

	/* Init some fields... */
	entity->anglescale = -1;

	/* go through all the keys in this entity */
	while (1) {
	    int c;

	    /* parse key */
	    data = COM_Parse(data);
	    if (!data)
		Error("%s: EOF without closing brace", __func__);
	    if (!strcmp(com_token, "}"))
		break;
	    if (strlen(com_token) > MAX_ENT_KEY - 1)
		Error("%s: Key length > %i", __func__, MAX_ENT_KEY - 1);
	    strcpy(key, com_token);

	    /* parse value */
	    data = COM_Parse(data);
	    if (!data)
		Error("%s: EOF without closing brace", __func__);
	    c = com_token[0];
	    if (c == '}')
		Error("%s: closing brace without data", __func__);
	    if (strlen(com_token) > MAX_ENT_VALUE - 1)
		Error("%s: Value length > %i", __func__, MAX_ENT_VALUE - 1);

	    epair = malloc(sizeof(epair_t));
	    memset(epair, 0, sizeof(epair_t));
	    strcpy(epair->key, key);
	    strcpy(epair->value, com_token);
	    epair->next = entity->epairs;
	    entity->epairs = epair;

	    if (!strcmp(key, "classname"))
		strcpy(entity->classname, com_token);
	    else if (!strcmp(key, "target"))
		strcpy(entity->target, com_token);
	    else if (!strcmp(key, "targetname"))
		strcpy(entity->targetname, com_token);
	    else if (!strcmp(key, "origin"))
		scan_vec3(entity->origin, com_token, "origin");
	    else if (!strncmp(key, "light", 5) || !strcmp(key, "_light"))
		entity->light.light = atof(com_token);
	    else if (!strcmp(key, "style")) {
		entity->style = atof(com_token);
		if (entity->style < 0 || entity->style > 254)
		    Error("Bad light style %i (must be 0-254)", entity->style);
	    } else if (!strcmp(key, "angle"))
		entity->spotangle = atof(com_token);
	    else if (!strcmp(key, "_softangle"))
		entity->spotangle2 = atof(com_token);
	    else if (!strcmp(key, "wait"))
		entity->atten = atof(com_token);
	    else if (!strcmp(key, "delay"))
		entity->formula = atoi(com_token);
	    else if (!strcmp(key, "mangle")) {
		scan_vec3(vec, com_token, "mangle");
		vec_from_mangle(entity->spotvec, vec);
		entity->spotlight = true;
	    } else if (!strcmp(key, "_color") || !strcmp(key, "color"))
		scan_vec3(entity->light.color, com_token, "color");
	    else if (!strcmp(key, "_sunlight"))
		sunlight.light = atof(com_token);
	    else if (!strcmp(key, "_sun_mangle")) {
		scan_vec3(vec, com_token, "_sun_mangle");
		vec_from_mangle(sunvec, vec);
		VectorNormalize(sunvec);
		VectorScale(sunvec, -16384, sunvec);
	    } else if (!strcmp(key, "_sunlight_color"))
		scan_vec3(sunlight.color, com_token, "_sunlight_color");
	    else if (!strcmp(key, "_minlight_color"))
		scan_vec3(minlight.color, com_token, "_minlight_color");
	    else if (!strcmp(key, "_anglesense") || !strcmp(key, "_anglescale"))
		entity->anglescale = atof(com_token);
	}

	/*
	 * Check light entity fields and any global settings in worldspawn.
	 */
	if (!strncmp(entity->classname, "light", 5)) {
	    CheckEntityFields(entity);
	    num_lights++;
	}
	if (!strcmp(entity->classname, "light")) {
	    if (entity->targetname[0] && !entity->style) {
		char style[16];
		entity->style = LightStyleForTargetname(entity->targetname);
		snprintf(style, sizeof(style), "%i", entity->style);
		SetKeyValue(entity, "style", style);
	    }
	}
	if (!strcmp(entity->classname, "worldspawn")) {
	    if (entity->light.light > 0 && !minlight.light) {
		minlight.light = entity->light.light;
		logprint("using minlight value %i from worldspawn.\n",
			 (int)minlight.light);
	    } else if (minlight.light) {
		logprint("Using minlight value %i from command line.\n",
			 (int)minlight.light);
	    }
	    if (entity->anglescale >= 0 && entity->anglescale <= 1.0)
		sun_anglescale = entity->anglescale;
	}
    }

    if (!VectorCompare(sunlight.color, vec3_white) ||
	!VectorCompare(minlight.color, vec3_white)) {
	if (!write_litfile) {
	    write_litfile = true;
	    logprint("Colored light entities detected: "
		     ".lit output enabled.\n");
	}
    }

    logprint("%d entities read, %d are lights.\n", num_entities, num_lights);
    MatchTargets();
    SetupSpotlights();
}

const char *
ValueForKey(const entity_t *ent, const char *key)
{
    epair_t *ep;

    for (ep = ent->epairs; ep; ep = ep->next)
	if (!strcmp(ep->key, key))
	    return ep->value;
    return "";
}

entity_t *
FindEntityWithKeyPair(const char *key, const char *value)
{
    entity_t *ent;
    epair_t *ep;
    int i;

    for (i = 0; i < num_entities; i++) {
	ent = &entities[i];
	for (ep = ent->epairs; ep; ep = ep->next)
	    if (!strcmp(ep->key, key)) {
		if (!strcmp(ep->value, value))
		    return ent;
		break;
	    }
    }
    return NULL;
}

void
GetVectorForKey(const entity_t *ent, const char *key, vec3_t vec)
{
    const char *value;

    value = ValueForKey(ent, key);
    sscanf(value, "%f %f %f", &vec[0], &vec[1], &vec[2]);
}

static size_t
Get_EntityStringSize(const entity_t *entities, int num_entities)
{
    const entity_t *entity;
    const epair_t *epair;
    size_t size;
    int i;

    size = 0;
    for (i = 0, entity = entities; i < num_entities; i++, entity++) {
	if (!entity->epairs)
	    continue;
	size += 2; /* "{\n" */
	for (epair = entity->epairs; epair; epair = epair->next) {
	    /* 6 extra chars for quotes, space and newline */
	    size += strlen(epair->key) + strlen(epair->value) + 6;
	}
	size += 2; /* "}\n" */
    }
    size += 1; /* zero terminator */

    return size;
}

/*
 * ================
 * WriteEntitiesToString
 * FIXME - why even bother re-writing the string?
 * ================
 */
void
WriteEntitiesToString(bsp2_t *bsp)
{
    const entity_t *entity;
    const epair_t *epair;
    size_t space, length;
    char *pos;
    int i;

    if (bsp->dentdata)
	free(bsp->dentdata);

    /* FIXME - why are we printing this here? */
    logprint("%i switchable light styles\n", numlighttargets);

    bsp->entdatasize = Get_EntityStringSize(entities, num_entities);
    bsp->dentdata = malloc(bsp->entdatasize);
    if (!bsp->dentdata)
	Error("%s: allocation of %d bytes failed\n", __func__,
	      bsp->entdatasize);

    space = bsp->entdatasize;
    pos = bsp->dentdata;
    for (i = 0, entity = entities; i < num_entities; i++, entity++) {
	if (!entity->epairs)
	    continue;

	length = snprintf(pos, space, "{\n");
	pos += length;
	space -= length;

	for (epair = entity->epairs; epair; epair = epair->next) {
	    length = snprintf(pos, space, "\"%s\" \"%s\"\n",
			      epair->key, epair->value);
	    pos += length;
	    space -= length;
	}

	length = snprintf(pos, space, "}\n");
	pos += length;
	space -= length;
    }
}
