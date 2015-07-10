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
static entity_t *entities_tail;
static int num_entities;
static int num_lights;

entity_t *lights[MAX_LIGHTS];

/* surface lights */
#define MAX_SURFLIGHT_TEMPLATES 256
entity_t *surfacelight_templates[MAX_SURFLIGHT_TEMPLATES];
int num_surfacelight_templates;
static void MakeSurfaceLights(const bsp2_t *bsp);

/* temporary storage for sunlight settings before the sun_t objects are
   created. */
static lightsample_t sunlight = { 0, { 255, 255, 255 } };
static lightsample_t sunlight2 = { 0, { 255, 255, 255 } };
static int sunlight_dirt = 0;
static int sunlight2_dirt = 0;
static vec3_t sunvec = { 0, 0, -1 };		/* defaults to straight down */
static vec_t sun_deviance = 0;

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
    entity_t *entity;
    const entity_t *target;

    for (entity = entities; entity; entity = entity->next) {
	if (!entity->target[0])
	    continue;
	for (target = entities; target; target = target->next) {
	    if (!strcmp(target->targetname, entity->target)) {
		entity->targetent = target;
		break;
	    }
	}
	if (target == NULL) {
	    logprint("WARNING: entity at (%s) (%s) has unmatched "
		     "target (%s)\n", VecStr(entity->origin),
		     entity->classname, entity->target);
	    continue;
	}

	/* set the style on the source ent for switchable lights */

	// ericw -- this seems completely useless, why would the
	// triggering entity need to have the light's style key?
	//
	// disabling because it can cause problems, e.g. if the
	// triggering entity is a monster, and the style key is used
	// by the mod.
#if 0
	if (target->style) {
	    char style[10];
	    entity->style = target->style;
	    snprintf(style, sizeof(style), "%d", entity->style);
	    SetKeyValue(entity, "style", style);
	}
#endif
    }
}

static void
SetupSpotlights(void)
{
    entity_t *entity;

    for (entity = entities; entity; entity = entity->next) {
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

/* detect colors with components in 0-1 and scale them to 0-255 */
static void
normalize_color_format(vec3_t color)
{
    if (color[0] >= 0 && color[0] <= 1 &&
	color[1] >= 0 && color[1] <= 1 &&
	color[2] >= 0 && color[2] <= 1)
    {
	VectorScale(color, 255, color);
    }
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
    
    /* set up deviance and samples defaults */
    if (entity->deviance > 0 && entity->num_samples == 0) {
        entity->num_samples = 16;
    }
    if (entity->deviance < 0.0f || entity->num_samples < 1) {
        entity->deviance = 0.0f;
        entity->num_samples = 1;
    }
    /* For most formulas, we need to divide the light value by the number of
       samples (jittering) to keep the brightness approximately the same. */
    if (entity->formula == LF_INVERSE
        || entity->formula == LF_INVERSE2
        || entity->formula == LF_INFINITE
        || (entity->formula == LF_LOCALMIN && addminlight)
        || entity->formula == LF_INVERSE2A) {
        entity->light.light /= entity->num_samples;
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
 * =============
 * Dirt_ResolveFlag
 *
 * Resolves a dirt flag (0=default, 1=enable, -1=disable) to a boolean
 * =============
 */
static qboolean 
Dirt_ResolveFlag(int dirtInt)
{
	if (dirtInt == 1) return true;
	else if (dirtInt == -1) return false;
	else return globalDirt;
}

/*
 * =============
 * AddSun
 * =============
 */
static void
AddSun(vec3_t sunvec, lightsample_t sunlight, float anglescale, int dirtInt)
{
    sun_t *sun = malloc(sizeof(sun_t));
    memset(sun, 0, sizeof(*sun));
    VectorCopy(sunvec, sun->sunvec);
    VectorNormalize(sun->sunvec);
    VectorScale(sun->sunvec, -16384, sun->sunvec);
    sun->sunlight = sunlight;    
    sun->anglescale = anglescale;
    sun->dirt = Dirt_ResolveFlag(dirtInt);

    // add to list
    sun->next = suns;
    suns = sun;

    // printf( "sun is using vector %f %f %f light %f color %f %f %f anglescale %f dirt %d resolved to %d\n", 
    // 	sun->sunvec[0], sun->sunvec[1], sun->sunvec[2], sun->sunlight.light,
    // 	sun->sunlight.color[0], sun->sunlight.color[1], sun->sunlight.color[2],
    // 	anglescale,
    // 	dirtInt,
    // 	(int)sun->dirt);
}

/*
 * =============
 * SetupSuns
 *
 * Creates a sun_t object for the "_sunlight" worldspawn key,
 * optionall many suns if the "_sunlight_penumbra" key is used.
 *
 * From q3map2
 * =============
 */
static void
SetupSuns()
{
    int i;
    int sun_num_samples = 100;

    if (sun_deviance == 0) {
    	sun_num_samples = 1;
    } else {
	logprint("using _sunlight_penumbra of %f degrees from worldspawn.\n", sun_deviance);
    }

    VectorNormalize(sunvec);

    //printf( "input sunvec %f %f %f. deviance is %f, %d samples\n",sunvec[0],sunvec[1], sunvec[2], sun_deviance, sun_num_samples);

    /* set photons */
    sunlight.light /= sun_num_samples;

    for ( i = 0; i < sun_num_samples; i++ )
    {
    	vec3_t direction;

        /* calculate sun direction */
        if ( i == 0 ) {
            VectorCopy( sunvec, direction );
        }
        else
        {
            vec_t da, de;
            vec_t d = sqrt( sunvec[ 0 ] * sunvec[ 0 ] + sunvec[ 1 ] * sunvec[ 1 ] );
            vec_t angle = atan2( sunvec[ 1 ], sunvec[ 0 ] );
            vec_t elevation = atan2( sunvec[ 2 ], d );

            /* jitter the angles (loop to keep random sample within sun->deviance steridians) */
            do
            {
                da = ( Random() * 2.0f - 1.0f ) * DEG2RAD(sun_deviance);
                de = ( Random() * 2.0f - 1.0f ) * DEG2RAD(sun_deviance);
            }
            while ( ( da * da + de * de ) > ( sun_deviance * sun_deviance ) );
            angle += da;
            elevation += de;

            /* create new vector */
            direction[ 0 ] = cos( angle ) * cos( elevation );
            direction[ 1 ] = sin( angle ) * cos( elevation );
            direction[ 2 ] = sin( elevation );
        }

        //printf( "sun %d is using vector %f %f %f\n", i, direction[0], direction[1], direction[2]);

        AddSun(direction, sunlight, sun_anglescale, sunlight_dirt);
    }
}

/*
 * =============
 * SetupSkyDome
 *
 * Setup a dome of suns for the "_sunlight2" worldspawn key.
 *
 * From q3map2
 * =============
 */
static void
SetupSkyDome()
{
	int i, j, numSuns;
	int angleSteps, elevationSteps;
	float angle, elevation;
	float angleStep, elevationStep;
	float step, start;
	vec3_t direction;
	const int iterations = 8;

	/* dummy check */
	if ( sunlight2.light <= 0.0f || iterations < 2 ) {
		return;
	}

	/* calculate some stuff */
	step = 2.0f / ( iterations - 1 );
	start = -1.0f;

	/* setup */
	elevationSteps = iterations - 1;
	angleSteps = elevationSteps * 4;
	angle = 0.0f;
	elevationStep = DEG2RAD( 90.0f / iterations );  /* skip elevation 0 */
	angleStep = DEG2RAD( 360.0f / angleSteps );

	/* calc individual sun brightness */
	numSuns = angleSteps * elevationSteps + 1;
	logprint("using %d suns for _sunlight2. total light: %f color: %f %f %f\n", numSuns, sunlight2.light, sunlight2.color[0], sunlight2.color[1], sunlight2.color[2]);
	sunlight2.light /= numSuns;

	/* iterate elevation */
	elevation = elevationStep * 0.5f;
	angle = 0.0f;
	for ( i = 0, elevation = elevationStep * 0.5f; i < elevationSteps; i++ )
	{
		/* iterate angle */
		for ( j = 0; j < angleSteps; j++ )
		{
			/* create sun */
			direction[ 0 ] = cos( angle ) * cos( elevation );
			direction[ 1 ] = sin( angle ) * cos( elevation );
			direction[ 2 ] = -sin( elevation );

			AddSun(direction, sunlight2, 0.0, sunlight2_dirt);

			/* move */
			angle += angleStep;
		}

		/* move */
		elevation += elevationStep;
		angle += angleStep / elevationSteps;
	}

	/* create vertical sun */
	VectorSet( direction, 0.0f, 0.0f, 1.0f );

	AddSun(direction, sunlight2, 0.0, sunlight2_dirt);
}

#if 0
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
#endif

/*
 * =============
 * Entities_Insert
 *
 * Adds the entity to the linked list
 * =============
 */
static void
Entities_Insert(entity_t *entity)
{
	/* Insert it into the tail end of the list */
	if (num_entities == 0) {
	    entities = entity;
	    entities_tail = entity;
	} else {
	    entities_tail->next = entity;
	    entities_tail = entity;
	}
	entity->next = NULL;
	num_entities++;
}

/*
 * =============
 * DuplicateEntity
 * =============
 */
static entity_t *
DuplicateEntity(const entity_t *src)
{
    epair_t *ep;
    entity_t *entity = (entity_t *)malloc(sizeof(entity_t));
    memcpy(entity, src, sizeof(entity_t));
    
    /* also copy epairs */
    entity->epairs = NULL;
    for (ep = src->epairs; ep; ep = ep->next)
        SetKeyValue(entity, ep->key, ep->value);
    
    /* also insert into the entity list */
    entity->next = NULL;
    Entities_Insert(entity);
    
    return entity;
}

/*
 * =============
 * JitterEntity
 *
 * Creates jittered copies of the light if specified using the "_samples" and "_deviance" keys. 
 *
 * From q3map2
 * =============
 */
static void
JitterEntity(entity_t *entity)
{
	int j;

	/* jitter the light */
	for ( j = 1; j < entity->num_samples; j++ )
	{
		/* create a light */
                entity_t *light2 = DuplicateEntity(entity);
		light2->generated = true; // don't write generated light to bsp

		/* jitter it */
		light2->origin[ 0 ] = entity->origin[ 0 ] + ( Random() * 2.0f - 1.0f ) * entity->deviance;
		light2->origin[ 1 ] = entity->origin[ 1 ] + ( Random() * 2.0f - 1.0f ) * entity->deviance;
		light2->origin[ 2 ] = entity->origin[ 2 ] + ( Random() * 2.0f - 1.0f ) * entity->deviance;
	}
}

static void
JitterEntities()
{
    entity_t *old_tail;
    entity_t *entity;

    // We will append to the list during iteration. This is the entity
    // to stop at.
    old_tail = entities_tail;
    
    for (entity = entities; entity; entity = entity->next) {
        if (!strncmp(entity->classname, "light", 5)) {
            JitterEntity(entity);
        }
        
        if (entity == old_tail)
            break;
    }
}

static void
FindLights()
{
    int totallights;
    entity_t *entity;

    totallights = 0;
    for (entity = entities; entity; entity = entity->next) {
        if (totallights == MAX_LIGHTS) {
            Error("totallights == MAX_LIGHTS");
        }
        if (entity->light.light != 0) {
            lights[totallights++] = entity;
        }
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

    /* start parsing */
    num_entities = 0;
    entities = NULL;
    entities_tail = NULL;
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

	/* Allocate a new entity */
	entity = (entity_t *)malloc(sizeof(entity_t));
	memset(entity, 0, sizeof(*entity));
	Entities_Insert(entity);

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
	    } else if (!strcmp(key, "_color") || !strcmp(key, "color")) {
		scan_vec3(entity->light.color, com_token, "color");
		normalize_color_format(entity->light.color);
	    } else if (!strcmp(key, "_sunlight"))
		sunlight.light = atof(com_token);
	    else if (!strcmp(key, "_sunlight_mangle") || !strcmp(key, "_sun_mangle")) {
		scan_vec3(vec, com_token, "_sun_mangle");
		vec_from_mangle(sunvec, vec);
	    } else if (!strcmp(key, "_sunlight_color")) {
		scan_vec3(sunlight.color, com_token, "_sunlight_color");
		normalize_color_format(sunlight.color);
	    } else if (!strcmp(key, "_sunlight2"))
		sunlight2.light = atof(com_token);
	    else if (!strcmp(key, "_sunlight2_color") || !strcmp(key, "_sunlight_color2")) {
		scan_vec3(sunlight2.color, com_token, key);
		normalize_color_format(sunlight2.color);
	    } else if (!strcmp(key, "_minlight_color")) {
		scan_vec3(minlight.color, com_token, "_minlight_color");
		normalize_color_format(minlight.color);
	    } else if (!strcmp(key, "_anglesense") || !strcmp(key, "_anglescale"))
		entity->anglescale = atof(com_token);
	    else if (!strcmp(key, "_dirtdepth"))
		entity->dirtdepth = atof(com_token);
	    else if (!strcmp(key, "_dirtmode"))
		entity->dirtmode = atoi(com_token);
	    else if (!strcmp(key, "_sunlight_dirt"))
		sunlight_dirt = atoi(com_token);
	    else if (!strcmp(key, "_sunlight2_dirt"))
		sunlight2_dirt = atoi(com_token);
	    else if (!strcmp(key, "_minlight_dirt"))
		entity->minlight_dirt = atoi(com_token);
	    else if (!strcmp(key, "_dirtscale"))
		entity->dirtscale = atof(com_token);
	    else if (!strcmp(key, "_dirtgain"))
		entity->dirtgain = atof(com_token);
	    else if (!strcmp(key, "_dirt")) {
		entity->dirt = atoi(com_token);
		if (entity->dirt == 1 && !dirty) {
		    logprint("entity with \"_dirt\" \"1\" detected, enabling "
			"dirtmapping.\n");
		    dirty = true;
		}
	    }
	    else if (!strcmp(key, "_sunlight_penumbra")) {
		sun_deviance = atof(com_token);
	    }
	    else if (!strcmp(key, "_deviance")) {
		entity->deviance = atof(com_token);
	    }
	    else if (!strcmp(key, "_samples")) {
		entity->num_samples = atoi(com_token);
	    }
	    else if (!strcmp(key, "_dist")) {
		entity->dist = atof(com_token);
	    }
	    else if (!strcmp(key, "_range")) {
		entity->range = atof(com_token);
	    }
	    else if (!strcmp(key, "_gamma")) {
		lightmapgamma = atof(com_token);
		logprint("using lightmap gamma value %f\n", lightmapgamma);
	    }
	}

	/*
	 * Check light entity fields and any global settings in worldspawn.
	 */
	if (!strncmp(entity->classname, "light", 5)) {
	    CheckEntityFields(entity);
	    num_lights++;
	}
	if (!strncmp(entity->classname, "light", 5)) {
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
	    if (entity->anglescale >= 0 && entity->anglescale <= 1.0) {
		sun_anglescale = entity->anglescale;
		logprint("using sunlight anglescale value %f from worldspawn.\n",
			 sun_anglescale);
	    }

	    if (entity->dist != 0.0) {
		scaledist = entity->dist;
		logprint("using _dist value %f from worldspawn.\n",
			 scaledist);
	    }

	    if (entity->range != 0.0) {
		rangescale = entity->range;
		logprint("using _range value %f from worldspawn.\n",
			 rangescale);
	    }

	    if (entity->dirtdepth && !dirtDepthSetOnCmdline) {
		dirtDepth = entity->dirtdepth;
		logprint("Using dirtdepth value %f from worldspawn.\n", 
			dirtDepth);
	    }
	    if (entity->dirtmode && !dirtModeSetOnCmdline) {
		dirtMode = entity->dirtmode;
		logprint("Using dirtmode value %i from worldspawn.\n", 
			dirtMode);
	    }
	    if (entity->dirtscale && !dirtScaleSetOnCmdline) {
		dirtScale = entity->dirtscale;
		logprint("Using dirtscale value %f from worldspawn.\n", 
			dirtScale);
	    }
	    if (entity->dirtgain && !dirtGainSetOnCmdline) {
		dirtGain = entity->dirtgain;
		logprint("Using dirtgain value %f from worldspawn.\n", 
			dirtGain);
	    }
	    if (entity->dirt == 1) {
		globalDirt = true;
		dirty = true;
		logprint("Global dirtmapping enabled in worldspawn.\n");
	    }

	    if (sunlight_dirt == 1) {
		dirty = true;
		logprint("Sunlight dirtmapping enabled in worldspawn.\n");
	    } else if (sunlight_dirt == -1) {
		logprint("Sunlight dirtmapping disabled in worldspawn.\n");
	    }

	    if (sunlight2_dirt == 1) {
		dirty = true;
		logprint("Sunlight2 dirtmapping enabled in worldspawn.\n");
	    } else if (sunlight2_dirt == -1) {
		logprint("Sunlight2 dirtmapping disabled in worldspawn.\n");
	    }

	    if (entity->minlight_dirt == 1) {
		minlightDirt = true;
		dirty = true;
		logprint("Minlight dirtmapping enabled in worldspawn.\n");
	    } else if (entity->minlight_dirt == -1) {
		minlightDirt = false;
		logprint("Minlight dirtmapping disabled in worldspawn.\n");
	    } else {
		minlightDirt = globalDirt;
	    }
	}
    }

    if (!VectorCompare(sunlight.color, vec3_white) ||
	!VectorCompare(minlight.color, vec3_white) ||
	!VectorCompare(sunlight2.color, vec3_white)) {
	if (!write_litfile) {
	    write_litfile = true;
	    logprint("Colored light entities detected: "
		     ".lit output enabled.\n");
	}
    }

    logprint("%d entities read, %d are lights.\n", num_entities, num_lights);

    // Creates more light entities, needs to be done before the rest
    MakeSurfaceLights(bsp);

    MatchTargets();
    JitterEntities();
    SetupSpotlights();
    SetupSuns();
    SetupSkyDome();
    FindLights();
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

    for (ent = entities; ent; ent = ent->next) {
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
Get_EntityStringSize(const entity_t *entities)
{
    const entity_t *entity;
    const epair_t *epair;
    size_t size;

    size = 0;
    for (entity = entities; entity; entity = entity->next) {
	if (!entity->epairs)
	    continue;
	if (entity->generated)
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
 * FIXME - why even bother re-writing the string? Switchable lights need styles set.
 * ================
 */
void
WriteEntitiesToString(bsp2_t *bsp)
{
    const entity_t *entity;
    const epair_t *epair;
    size_t space, length;
    char *pos;

    if (bsp->dentdata)
	free(bsp->dentdata);

    /* FIXME - why are we printing this here? */
    logprint("%i switchable light styles\n", numlighttargets);

    bsp->entdatasize = Get_EntityStringSize(entities);
    bsp->dentdata = malloc(bsp->entdatasize);
    if (!bsp->dentdata)
	Error("%s: allocation of %d bytes failed\n", __func__,
	      bsp->entdatasize);

    space = bsp->entdatasize;
    pos = bsp->dentdata;
    for (entity = entities; entity; entity = entity->next) {
	if (!entity->epairs)
	    continue;
	if (entity->generated)
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


/*
 * =======================================================================
 *                            SURFACE LIGHTS
 * =======================================================================
 */

static void CreateSurfaceLight(const vec3_t origin, const entity_t *surflight_template)
{
    entity_t *entity = DuplicateEntity(surflight_template);

    VectorCopy(origin, entity->origin);

    /* don't write to bsp */
    entity->generated = true;

    num_lights++;
}

static void CreateSurfaceLightOnFaceSubdivision(const bsp2_dface_t *face, const entity_t *surflight_template, const bsp2_t *bsp, int numverts, const vec_t *verts)
{
    int i;
    vec3_t midpoint = {0, 0, 0};
    vec3_t normal;
    vec_t offset;

    for (i=0; i<numverts; i++)
    {
        VectorAdd(midpoint, verts + (i * 3), midpoint);
    }
    midpoint[0] /= numverts;
    midpoint[1] /= numverts;
    midpoint[2] /= numverts;
    VectorCopy(bsp->dplanes[face->planenum].normal, normal);
    vec_t dist = bsp->dplanes[face->planenum].dist;

    /* Nudge 2 units (by default) along face normal */
    if (face->side) {
        dist = -dist;
        VectorSubtract(vec3_origin, normal, normal);
    }

    offset = atof(ValueForKey(surflight_template, "_surface_offset"));
    if (offset <= 0)
        offset = 2.0;
    
    VectorMA(midpoint, offset, normal, midpoint);

    CreateSurfaceLight(midpoint, surflight_template);
}

static void BoundPoly (int numverts, float *verts, vec3_t mins, vec3_t maxs)
{
    int             i, j;
    float   *v;

    mins[0] = mins[1] = mins[2] = 9999;
    maxs[0] = maxs[1] = maxs[2] = -9999;
    v = verts;
    for (i=0 ; i<numverts ; i++)
        for (j=0 ; j<3 ; j++, v++)
        {
            if (*v < mins[j])
                mins[j] = *v;
            if (*v > maxs[j])
                maxs[j] = *v;
        }
}

/*
 ================
 SubdividePolygon - from GLQuake
 ================
 */
static void SubdividePolygon (const bsp2_dface_t *face, const bsp2_t *bsp, int numverts, vec_t *verts, float subdivide_size)
{
    int             i, j, k;
    vec3_t  mins, maxs;
    float   m;
    float   *v;
    vec3_t  front[64], back[64];
    int             f, b;
    float   dist[64];
    float   frac;
    //glpoly_t        *poly;
    //float   s, t;

    if (numverts > 60)
        Error ("numverts = %i", numverts);

    BoundPoly (numverts, verts, mins, maxs);

    for (i=0 ; i<3 ; i++)
    {
        m = (mins[i] + maxs[i]) * 0.5;
        m = subdivide_size * floor (m/subdivide_size + 0.5);
        if (maxs[i] - m < 8)
            continue;
        if (m - mins[i] < 8)
            continue;

        // cut it
        v = verts + i;
        for (j=0 ; j<numverts ; j++, v+= 3)
            dist[j] = *v - m;

        // wrap cases
        dist[j] = dist[0];
        v-=i;
        VectorCopy (verts, v);

        f = b = 0;
        v = verts;
        for (j=0 ; j<numverts ; j++, v+= 3)
        {
            if (dist[j] >= 0)
            {
                VectorCopy (v, front[f]);
                f++;
            }
            if (dist[j] <= 0)
            {
                VectorCopy (v, back[b]);
                b++;
            }
            if (dist[j] == 0 || dist[j+1] == 0)
                continue;
            if ( (dist[j] > 0) != (dist[j+1] > 0) )
            {
                // clip point
                frac = dist[j] / (dist[j] - dist[j+1]);
                for (k=0 ; k<3 ; k++)
                    front[f][k] = back[b][k] = v[k] + frac*(v[3+k] - v[k]);
                f++;
                b++;
            }
        }

        SubdividePolygon (face, bsp, f, front[0], subdivide_size);
        SubdividePolygon (face, bsp, b, back[0], subdivide_size);
        return;
    }

    const texinfo_t *tex = &bsp->texinfo[face->texinfo];
    const int offset = bsp->dtexdata.header->dataofs[tex->miptex];
    const miptex_t *miptex = (const miptex_t *)(bsp->dtexdata.base + offset);
    const char *texname = miptex->name;

    for (i=0; i<num_surfacelight_templates; i++) {
        if (!strcasecmp(texname, ValueForKey(surfacelight_templates[i], "_surface"))) {
            CreateSurfaceLightOnFaceSubdivision(face, surfacelight_templates[i], bsp, numverts, verts);
        }
    }
}

/*
 ================
 GL_SubdivideSurface - from GLQuake
 ================
 */
static void GL_SubdivideSurface (const bsp2_dface_t *face, const bsp2_t *bsp)
{
    int i;
    vec3_t  verts[64];

    for (i = 0; i < face->numedges; i++) {
        dvertex_t *v;
        int edgenum = bsp->dsurfedges[face->firstedge + i];
        if (edgenum >= 0) {
            v = bsp->dvertexes + bsp->dedges[edgenum].v[0];
        } else {
            v = bsp->dvertexes + bsp->dedges[-edgenum].v[1];
        }
        VectorCopy(v->point, verts[i]);
    }

    SubdividePolygon (face, bsp, face->numedges, verts[0], 128);
}

static void MakeSurfaceLights(const bsp2_t *bsp)
{
    entity_t *entity;
    int i, k;

    for (entity = entities; entity; entity = entity->next) {
        const char *tex = ValueForKey(entity, "_surface");
        if (strcmp(tex, "") != 0) {
            /* Add to template list */
            if (num_surfacelight_templates == MAX_SURFLIGHT_TEMPLATES)
                Error("num_surfacelight_templates == MAX_SURFLIGHT_TEMPLATES");
            surfacelight_templates[num_surfacelight_templates++] = entity;
            
            printf("Creating surface lights for texture \"%s\" from template at (%s)\n",
                   tex, ValueForKey(entity, "origin"));
        }
    }

    if (!num_surfacelight_templates)
        return;

    /* Create the surface lights */
    for (i=0; i<bsp->numleafs; i++) {
        const bsp2_dleaf_t *leaf = bsp->dleafs + i;
        const bsp2_dface_t *surf;
        int ofs;
        qboolean underwater = leaf->contents != CONTENTS_EMPTY;

        for (k = 0; k < leaf->nummarksurfaces; k++) {
            const texinfo_t *info;
            const miptex_t *miptex;

            surf = &bsp->dfaces[bsp->dmarksurfaces[leaf->firstmarksurface + k]];
            info = &bsp->texinfo[surf->texinfo];
            ofs = bsp->dtexdata.header->dataofs[info->miptex];
            miptex = (const miptex_t *)(bsp->dtexdata.base + ofs);
            
            /* Ignore the underwater side of liquid surfaces */
            if (miptex->name[0] == '*' && underwater)
                continue;

            GL_SubdivideSurface(surf, bsp);
        }
    }
    
    /* Hack: clear templates light value to 0 so they don't cast light */
    for (i=0;i<num_surfacelight_templates;i++) {
        surfacelight_templates[i]->light.light = 0;
    }
}
