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

entity_t entities[MAX_MAP_ENTITIES];
int num_entities;

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
LightStyleForTargetname(const char *targetname, qboolean alloc)
{
    int i;

    for (i = 0; i < numlighttargets; i++)
	if (!strcmp(lighttargets[i], targetname))
	    return 32 + i;
    if (!alloc)
	return -1;

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
    int i;
    int j;

    for (i = 0; i < num_entities; i++) {
	if (!entities[i].target[0])
	    continue;
	for (j = 0; j < num_entities; j++)
	    if (!strcmp(entities[j].targetname, entities[i].target)) {
		entities[i].targetent = &entities[j];
		break;
	    }
	if (j == num_entities) {
	    const entity_t *e = &entities[i];

	    logprint("WARNING: entity at (%i, %i, %i) (%s) has unmatched "
		     "target (%s)\n", (int)e->origin[0], (int)e->origin[1],
		     (int)e->origin[2], e->classname, e->target);
	    continue;
	}

	/* set the style on the source ent for switchable lights */
	if (entities[j].style) {
	    char s[16];

	    entities[i].style = entities[j].style;
	    sprintf(s, "%i", entities[i].style);
	    SetKeyValue(&entities[i], "style", s);
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

/*
 * ==================
 * LoadEntities
 * ==================
 */
void
LoadEntities(void)
{
    char *data;
    entity_t *entity;
    char key[MAX_ENT_KEY];
    epair_t *epair;
    vec3_t vec;
    int num_lights;

    data = dentdata;

    /* start parsing */
    num_entities = 0;
    num_lights = 0;

    /* go through all the entities */
    while (1) {
	/* parse the opening brace */
	data = COM_Parse(data);
	if (!data)
	    break;
	if (com_token[0] != '{')
	    Error("%s: found %s when expecting {", __func__, com_token);

	if (num_entities == MAX_MAP_ENTITIES)
	    Error("%s: MAX_MAP_ENTITIES", __func__);
	entity = &entities[num_entities];
	num_entities++;

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
		entity->light = atof(com_token);
	    else if (!strcmp(key, "style")) {
		entity->style = atof(com_token);
		if ((unsigned)entity->style > 254)
		    Error("Bad light style %i (must be 0-254)",
			  entity->style);
	    } else if (!strcmp(key, "angle"))
		entity->angle = atof(com_token);
	    else if (!strcmp(key, "wait"))
		entity->atten = atof(com_token);
	    else if (!strcmp(key, "delay"))
		entity->formula = atoi(com_token);
	    else if (!strcmp(key, "mangle")) {
		scan_vec3(vec, com_token, "mangle");
		vec_from_mangle(entity->mangle, vec);
		entity->use_mangle = true;
	    } else if (!strcmp(key, "_color") || !strcmp(key, "color"))
		scan_vec3(entity->lightcolor, com_token, "color");
	    else if (!strcmp(key, "_sunlight"))
		sunlight = atof(com_token);
	    else if (!strcmp(key, "_sun_mangle")) {
		scan_vec3(vec, com_token, "_sun_mangle");
		vec_from_mangle(sunmangle, vec);
		VectorNormalize(sunmangle);
		VectorScale(sunmangle, -16384, sunmangle);
	    } else if (!strcmp(key, "_sunlight_color"))
		scan_vec3(sunlight_color, com_token, "_sunlight_color");
	    else if (!strcmp(key, "_minlight_color"))
		scan_vec3(minlight_color, com_token, "_minlight_color");
	}

	/*
	 * All fields have been parsed. Check default settings and check for
	 * light value in worldspawn...
	 */
	if (!strncmp(entity->classname, "light", 5)) {
	    if (!entity->light)
		entity->light = DEFAULTLIGHTLEVEL;
	    if (entity->atten <= 0.0)
		entity->atten = 1.0;
	    if ((entity->formula < 0) || (entity->formula > 3))
		entity->formula = 0;
	    if (!entity->lightcolor[0] && !entity->lightcolor[1]
		&& !entity->lightcolor[2]) {
		entity->lightcolor[0] = 255;
		entity->lightcolor[1] = 255;
		entity->lightcolor[2] = 255;
	    }
	    num_lights++;
	}

	if (!strcmp(entity->classname, "light")) {
	    if (entity->targetname[0] && !entity->style) {
		char s[16];

		entity->style =
		    LightStyleForTargetname(entity->targetname, true);
		sprintf(s, "%i", entity->style);
		SetKeyValue(entity, "style", s);
	    }
	}

	if (!strcmp(entity->classname, "worldspawn")) {
	    if (entity->light > 0 && !worldminlight) {
		worldminlight = entity->light;
		logprint("using minlight value %i from worldspawn.\n",
			 worldminlight);
	    } else if (worldminlight) {
		logprint("Using minlight value %i from command line.\n",
			 worldminlight);
	    }
	}
    }
    logprint("%d entities read, %d are lights.\n", num_entities, num_lights);
    MatchTargets();
}

char *
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
    char *k;

    k = ValueForKey(ent, key);
    sscanf(k, "%f %f %f", &vec[0], &vec[1], &vec[2]);
}


/*
 * ================
 * WriteEntitiesToString
 * ================
 */
void
WriteEntitiesToString(void)
{
    char *buf;
    char *end;
    epair_t *ep;
    int i;
    char line[MAX_ENT_KEY + MAX_ENT_VALUE + 4];

    /*
     * max strlen(line) = (MAX_ENT_KEY - 1) + (MAX_ENT_VALUE - 1) +
     *                    strlen("\"\" \"\") + 1
     */

    buf = malloc(MAX_MAP_ENTSTRING);
    if (!buf)
	Error("%s: allocation of %i bytes failed.", __func__, entdatasize);

    end = buf;
    *end = 0;

    logprint("%i switchable light styles\n", numlighttargets);

    for (i = 0; i < num_entities; i++) {
	if (!entities[i].epairs)
	    continue;		/* ent got removed */

	strcat(end, "{\n");
	end += 2;

	for (ep = entities[i].epairs; ep; ep = ep->next) {
	    if (compress_ents && !strncmp(entities[i].classname, "light", 5)) {
		if (!strcmp(ep->key, "classname") ||
		    !strcmp(ep->key, "origin") ||
		    !strcmp(ep->key, "targetname") ||
		    !strcmp(ep->key, "spawnflags")) {
		    sprintf(line, "\"%s\" \"%s\"\n", ep->key, ep->value);
		    strcat(end, line);
		    end += strlen(line);
		}
	    } else {
		sprintf(line, "\"%s\" \"%s\"\n", ep->key, ep->value);
		strcat(end, line);
		end += strlen(line);
	    }
	}
	strcat(end, "}\n");
	end += 2;

	if (end > buf + MAX_MAP_ENTSTRING)
	    Error("Entity text too long");
    }
    entdatasize = end - buf + 1;

    if (dentdata)
	free(dentdata);

    dentdata = malloc(entdatasize);
    if (!dentdata)
	Error("%s: allocation of %i bytes failed.", __func__, entdatasize);

    memcpy(dentdata, buf, entdatasize);
    free(buf);
}
