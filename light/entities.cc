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

#include <cstring>
#include <sstream>
#include <common/cmdlib.h>

#include <light/light.hh>
#include <light/entities.hh>

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

using strings = std::vector<std::string>;

/* temporary storage for sunlight settings before the sun_t objects are created. */
lockable_vec_t sunlight         { "sunlight", 0.0f };                   /* main sun */
lockable_vec3_t sunlight_color  { "sunlight_color", 255.0f, 255.0f, 255.0f };
lockable_vec_t sun2             { "sun2", 0.0f };                   /* second sun */
lockable_vec3_t sun2_color      { "sun2_color", 255.0f, 255.0f, 255.0f };
lockable_vec_t sunlight2        { "sunlight2", 0.0f };                   /* top sky dome */
lockable_vec3_t sunlight2_color { strings{"sunlight2_color", "sunlight_color2"}, 255.0f, 255.0f, 255.0f };
lockable_vec_t sunlight3        { "sunlight3", 0.0f };                   /* bottom sky dome */
lockable_vec3_t sunlight3_color { strings{"sunlight3_color", "sunlight_color3"}, 255.0f, 255.0f, 255.0f };
lockable_vec_t sunlight_dirt    { "sunlight_dirt", 0.0f };
lockable_vec_t sunlight2_dirt   { "sunlight2_dirt", 0.0f };
lockable_vec3_t sunvec          { strings{"sunlight_mangle", "sun_mangle"}, 0.0f, 0.0f, -1.0f };  /* defaults to straight down */
lockable_vec3_t sun2vec         { "sun2_mangle", 0.0f, 0.0f, -1.0f };  /* defaults to straight down */
lockable_vec_t sun_deviance     { "sunlight_penumbra", 0.0f, 0.0f, 180.0f };

// entity_t

const char * entity_t::classname() const {
    return ValueForKey(this, "classname");
}

/*
 * ============================================================================
 * ENTITY FILE PARSING
 * If a light has a targetname, generate a unique style in the 32-63 range
 * ============================================================================
 */

#define MAX_LIGHT_TARGETS 32

static std::vector<std::string> lighttargetnames;

static void
SetKeyValue(entity_t *ent, const char *key, const char *value)
{
    ent->epairs[key] = value;
}

static entity_t *WorldEnt()
{
    if (0 != strcmp("worldspawn", ValueForKey(entities, "classname"))) {
        Error("WorldEnt() failed to get worldspawn");
    }
    return entities;
}

void SetWorldKeyValue(const char *key, const char *value)
{
    SetKeyValue(WorldEnt(), key, value);
}
const char *WorldValueForKey(const char *key)
{
    return ValueForKey(WorldEnt(), key);
}

static int
LightStyleForTargetname(const std::string &targetname)
{
    int i;

    for (i = 0; i < lighttargetnames.size(); i++)
        if (lighttargetnames.at(i) == targetname)
            return 32 + i;
    if (i == MAX_LIGHT_TARGETS)
        Error("%s: Too many unique light targetnames\n", __func__);

    lighttargetnames.push_back(targetname);
    return static_cast<int>(lighttargetnames.size()) - 1 + 32;
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
        std::string targetstr { ValueForKey(entity, "target") };
        if (!targetstr.length())
            continue;
        for (target = entities; target; target = target->next) {
            if (targetstr == ValueForKey(target, "targetname")) {
                entity->targetent = target;
                break;
            }
        }
        if (target == NULL) {
            logprint("WARNING: entity at (%s) (%s) has unmatched "
                     "target (%s)\n", VecStr(entity->origin),
                     entity->classname(), ValueForKey(entity, "target"));
            continue;
        }
    }
}

static void
SetupSpotlights(void)
{
    entity_t *entity;

    for (entity = entities; entity; entity = entity->next) {
        if (strncmp(entity->classname(), "light", 5))
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
void
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
        entity->anglescale = global_anglescale.floatValue();

    if (entity->formula < LF_LINEAR || entity->formula >= LF_COUNT) {
        static qboolean warned_once = true;
        if (!warned_once) {
            warned_once = true;
            logprint("WARNING: unknown formula number (%d) in delay field\n"
                     "   %s at (%s)\n"
                     "   (further formula warnings will be supressed)\n",
                     entity->formula, entity->classname(),
                     VecStr(entity->origin));
        }
        entity->formula = LF_LINEAR;
    }

    /* set up deviance and samples defaults */
    if (entity->deviance > 0 && entity->num_samples == 0) {
        entity->num_samples = 16;
    }
    if (entity->deviance <= 0.0f || entity->num_samples <= 1) {
        entity->deviance = 0.0f;
        entity->num_samples = 1;
    }
    /* For most formulas, we need to divide the light value by the number of
       samples (jittering) to keep the brightness approximately the same. */
    if (entity->formula == LF_INVERSE
        || entity->formula == LF_INVERSE2
        || entity->formula == LF_INFINITE
        || (entity->formula == LF_LOCALMIN && addminlight.boolValue())
        || entity->formula == LF_INVERSE2A) {
        entity->light.light /= entity->num_samples;
    }

    if (!VectorCompare(entity->light.color, vec3_origin)) {
        if (!write_litfile) {
            if (scaledonly) {
                write_litfile = 2;
                logprint("Colored light entities detected: "
                         "bspxlit output enabled.\n");
            } else {
                write_litfile = 1;
                logprint("Colored light entities detected: "
                         ".lit output enabled.\n");
            }
        }
    } else {
        VectorCopy(vec3_white, entity->light.color);
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
AddSun(vec3_t sunvec, vec_t light, const vec3_t color, int dirtInt)
{
    sun_t *sun = (sun_t *) malloc(sizeof(sun_t));
    memset(sun, 0, sizeof(*sun));
    VectorCopy(sunvec, sun->sunvec);
    VectorNormalize(sun->sunvec);
    VectorScale(sun->sunvec, -16384, sun->sunvec);
    sun->sunlight.light = light;
    VectorCopy(color, sun->sunlight.color);
    sun->anglescale = global_anglescale.floatValue();
    sun->dirt = Dirt_ResolveFlag(dirtInt);

    // add to list
    sun->next = suns;
    suns = sun;

    // printf( "sun is using vector %f %f %f light %f color %f %f %f anglescale %f dirt %d resolved to %d\n", 
    //  sun->sunvec[0], sun->sunvec[1], sun->sunvec[2], sun->sunlight.light,
    //  sun->sunlight.color[0], sun->sunlight.color[1], sun->sunlight.color[2],
    //  anglescale,
    //  dirtInt,
    //  (int)sun->dirt);
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
SetupSun(vec_t light, const vec3_t color, const vec3_t sunvec_in)
{
    vec3_t sunvec;
    int i;
    int sun_num_samples = sunsamples;

    if (sun_deviance.floatValue() == 0) {
        sun_num_samples = 1;
    } else {
        logprint("using _sunlight_penumbra of %f degrees from worldspawn.\n", sun_deviance.floatValue());
    }

    VectorCopy(sunvec_in, sunvec);
    VectorNormalize(sunvec);

    //printf( "input sunvec %f %f %f. deviance is %f, %d samples\n",sunvec[0],sunvec[1], sunvec[2], sun_deviance, sun_num_samples);

    /* set photons */
    light /= sun_num_samples;

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
                da = ( Random() * 2.0f - 1.0f ) * DEG2RAD(sun_deviance.floatValue());
                de = ( Random() * 2.0f - 1.0f ) * DEG2RAD(sun_deviance.floatValue());
            }
            while ( ( da * da + de * de ) > ( sun_deviance.floatValue() * sun_deviance.floatValue() ) );
            angle += da;
            elevation += de;

            /* create new vector */
            direction[ 0 ] = cos( angle ) * cos( elevation );
            direction[ 1 ] = sin( angle ) * cos( elevation );
            direction[ 2 ] = sin( elevation );
        }

        //printf( "sun %d is using vector %f %f %f\n", i, direction[0], direction[1], direction[2]);

        AddSun(direction, light, color, (int)sunlight_dirt.intValue());
    }
}

static void
SetupSuns()
{
    SetupSun(sunlight.floatValue(), *sunlight_color.vec3Value(), *sunvec.vec3Value());
    
    if (sun2.floatValue() != 0) {
        logprint("creating sun2\n");
        SetupSun(sun2.floatValue(), *sun2_color.vec3Value(), *sun2vec.vec3Value());
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
        int iterations;
        float angle, elevation;
        float angleStep, elevationStep;
        vec3_t direction;

        /* pick a value for 'iterations' so that 'numSuns' will be close to 'sunsamples' */
        iterations = rint(sqrt((sunsamples - 1) / 4)) + 1;
        iterations = qmax(iterations, 2);
    
        /* dummy check */
        if ( sunlight2.floatValue() <= 0.0f && sunlight3.floatValue() <= 0.0f ) {
                return;
        }
    
        /* setup */
        elevationSteps = iterations - 1;
        angleSteps = elevationSteps * 4;
        angle = 0.0f;
        elevationStep = DEG2RAD( 90.0f / (elevationSteps + 1) );  /* skip elevation 0 */
        angleStep = DEG2RAD( 360.0f / angleSteps );

        /* calc individual sun brightness */
        numSuns = angleSteps * elevationSteps + 1;
        if (sunlight2.floatValue() > 0) {
            logprint("using %d suns for _sunlight2. total light: %f color: %f %f %f\n", numSuns, sunlight2.floatValue(), (*sunlight2_color.vec3Value())[0], (*sunlight2_color.vec3Value())[1], (*sunlight2_color.vec3Value())[2]);
        }
        if (sunlight3.floatValue() > 0) {
            logprint("using %d suns for _sunlight3. total light: %f color: %f %f %f\n", numSuns, sunlight3.floatValue(), (*sunlight3_color.vec3Value())[0], (*sunlight3_color.vec3Value())[1], (*sunlight3_color.vec3Value())[2]);
        }
        // FIXME: Don't modify setting, make a copy
        float sunlight2value = sunlight2.floatValue() / numSuns;
        float sunlight3value = sunlight3.floatValue() / numSuns;

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

                        /* insert top hemisphere light */
                        if (sunlight2value > 0) {
                            AddSun(direction, sunlight2value, *sunlight2_color.vec3Value(), sunlight2_dirt.intValue());
                        }

                        direction[ 2 ] = -direction[ 2 ];
                    
                        /* insert bottom hemisphere light */
                        if (sunlight3value > 0) {
                            AddSun(direction, sunlight3value, *sunlight3_color.vec3Value(), sunlight2_dirt.intValue());
                        }
                    
                        /* move */
                        angle += angleStep;
                }

                /* move */
                elevation += elevationStep;
                angle += angleStep / elevationSteps;
        }

        /* create vertical sun */
        VectorSet( direction, 0.0f, 0.0f, 1.0f );

        if (sunlight2value > 0) {
            AddSun(direction, sunlight2value, *sunlight2_color.vec3Value(), sunlight2_dirt.intValue());
        }
    
        VectorSet( direction, 0.0f, 0.0f, -1.0f );
    
        if (sunlight3value > 0) {
            AddSun(direction, sunlight3value, *sunlight3_color.vec3Value(), sunlight2_dirt.intValue());
        }
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
    entity_t *entity = new entity_t(*src);

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
        if (!strncmp(entity->classname(), "light", 5)) {
            JitterEntity(entity);
        }
        
        if (entity == old_tail)
            break;
    }
}

void Matrix4x4_CM_Projection_Inf(float *proj, float fovx, float fovy, float neard)
{
    float xmin, xmax, ymin, ymax;
    float nudge = 1;

    //proj
    ymax = neard * tan( fovy * Q_PI / 360.0 );
    ymin = -ymax;

    if (fovx == fovy)
    {
        xmax = ymax;
        xmin = ymin;
    }
    else
    {
        xmax = neard * tan( fovx * Q_PI / 360.0 );
        xmin = -xmax;
    }

    proj[0] = (2*neard) / (xmax - xmin);
    proj[4] = 0;
    proj[8] = (xmax + xmin) / (xmax - xmin);
    proj[12] = 0;

    proj[1] = 0;
    proj[5] = (2*neard) / (ymax - ymin);
    proj[9] = (ymax + ymin) / (ymax - ymin);
    proj[13] = 0;

    proj[2] = 0;
    proj[6] = 0;
    proj[10] = -1  * ((float)(1<<21)/(1<<22));
    proj[14] = -2*neard * nudge;

    proj[3] = 0;
    proj[7] = 0;
    proj[11] = -1;
    proj[15] = 0;
}
float *Matrix4x4_CM_NewRotation(float ret[16], float a, float x, float y, float z)
{
    float c = cos(a* Q_PI / 180.0);
    float s = sin(a* Q_PI / 180.0);

    ret[0] = x*x*(1-c)+c;
    ret[4] = x*y*(1-c)-z*s;
    ret[8] = x*z*(1-c)+y*s;
    ret[12] = 0;

    ret[1] = y*x*(1-c)+z*s;
    ret[5] = y*y*(1-c)+c;
    ret[9] = y*z*(1-c)-x*s;
    ret[13] = 0;

    ret[2] = x*z*(1-c)-y*s;
    ret[6] = y*z*(1-c)+x*s;
    ret[10] = z*z*(1-c)+c;
    ret[14] = 0;

    ret[3] = 0;
    ret[7] = 0;
    ret[11] = 0;
    ret[15] = 1;
    return ret;
}
float *Matrix4x4_CM_NewTranslation(float ret[16], float x, float y, float z)
{
    ret[0] = 1;
    ret[4] = 0;
    ret[8] = 0;
    ret[12] = x;

    ret[1] = 0;
    ret[5] = 1;
    ret[9] = 0;
    ret[13] = y;

    ret[2] = 0;
    ret[6] = 0;
    ret[10] = 1;
    ret[14] = z;

    ret[3] = 0;
    ret[7] = 0;
    ret[11] = 0;
    ret[15] = 1;
    return ret;
}
void Matrix4_Multiply(const float *a, const float *b, float *out)
{
    out[0]  = a[0] * b[0] + a[4] * b[1] + a[8] * b[2] + a[12] * b[3];
    out[1]  = a[1] * b[0] + a[5] * b[1] + a[9] * b[2] + a[13] * b[3];
    out[2]  = a[2] * b[0] + a[6] * b[1] + a[10] * b[2] + a[14] * b[3];
    out[3]  = a[3] * b[0] + a[7] * b[1] + a[11] * b[2] + a[15] * b[3];

    out[4]  = a[0] * b[4] + a[4] * b[5] + a[8] * b[6] + a[12] * b[7];
    out[5]  = a[1] * b[4] + a[5] * b[5] + a[9] * b[6] + a[13] * b[7];
    out[6]  = a[2] * b[4] + a[6] * b[5] + a[10] * b[6] + a[14] * b[7];
    out[7]  = a[3] * b[4] + a[7] * b[5] + a[11] * b[6] + a[15] * b[7];

    out[8]  = a[0] * b[8] + a[4] * b[9] + a[8] * b[10] + a[12] * b[11];
    out[9]  = a[1] * b[8] + a[5] * b[9] + a[9] * b[10] + a[13] * b[11];
    out[10] = a[2] * b[8] + a[6] * b[9] + a[10] * b[10] + a[14] * b[11];
    out[11] = a[3] * b[8] + a[7] * b[9] + a[11] * b[10] + a[15] * b[11];

    out[12] = a[0] * b[12] + a[4] * b[13] + a[8] * b[14] + a[12] * b[15];
    out[13] = a[1] * b[12] + a[5] * b[13] + a[9] * b[14] + a[13] * b[15];
    out[14] = a[2] * b[12] + a[6] * b[13] + a[10] * b[14] + a[14] * b[15];
    out[15] = a[3] * b[12] + a[7] * b[13] + a[11] * b[14] + a[15] * b[15];
}
void Matrix4x4_CM_ModelViewMatrix(float *modelview, const vec3_t viewangles, const vec3_t vieworg)
{
    float t2[16];
    float tempmat[16];
    //load identity.
    memset(modelview, 0, sizeof(*modelview)*16);
#if FULLYGL
    modelview[0] = 1;
    modelview[5] = 1;
    modelview[10] = 1;
    modelview[15] = 1;

    Matrix4_Multiply(modelview, Matrix4_CM_NewRotation(-90,  1, 0, 0), tempmat);            // put Z going up
    Matrix4_Multiply(tempmat, Matrix4_CM_NewRotation(90,  0, 0, 1), modelview);     // put Z going up
#else
    //use this lame wierd and crazy identity matrix..
    modelview[2] = -1;
    modelview[4] = -1;
    modelview[9] = 1;
    modelview[15] = 1;
#endif
    //figure out the current modelview matrix

    //I would if some of these, but then I'd still need a couple of copys
    Matrix4_Multiply(modelview, Matrix4x4_CM_NewRotation(t2, -viewangles[2],  1, 0, 0), tempmat);
    Matrix4_Multiply(tempmat, Matrix4x4_CM_NewRotation(t2, -viewangles[0],  0, 1, 0), modelview);
    Matrix4_Multiply(modelview, Matrix4x4_CM_NewRotation(t2, -viewangles[1],  0, 0, 1), tempmat);

    Matrix4_Multiply(tempmat, Matrix4x4_CM_NewTranslation(t2, -vieworg[0],  -vieworg[1],  -vieworg[2]), modelview);         // put Z going up
}
void Matrix4x4_CM_MakeModelViewProj (const vec3_t viewangles, const vec3_t vieworg, float fovx, float fovy, float *modelviewproj)
{
    float modelview[16];
    float proj[16];

    Matrix4x4_CM_ModelViewMatrix(modelview, viewangles, vieworg);
    Matrix4x4_CM_Projection_Inf(proj, fovx, fovy, 4);
    Matrix4_Multiply(proj, modelview, modelviewproj);
}
float CalcFov (float fov_x, float width, float height)
{
    float   a;
    float   x;

    if (fov_x < 1 || fov_x > 179)
        Error ("Bad fov: %f", fov_x);

    x = fov_x/360*Q_PI;
    x = tan(x);
    x = width/x;

    a = atan (height/x);

    a = a*360/Q_PI;

    return a;
}

/*
finds the texture that is meant to be projected.
*/
static miptex_t *FindProjectionTexture(const bsp2_t *bsp, const char *texname)
{
    if (!bsp->texdatasize)
        return NULL;
    
    dmiptexlump_t *miplump = bsp->dtexdata.header;
    miptex_t *miptex;
    int texnum;
    /*outer loop finds the textures*/
    for (texnum = 0; texnum< miplump->nummiptex; texnum++)
    {
        int offset = miplump->dataofs[texnum];
        if (offset < 0)
            continue;
        
        miptex = (miptex_t*)(bsp->dtexdata.base + offset);
        if (!Q_strcasecmp(miptex->name, texname))
            return miptex;
    }
    return NULL;
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
        if (!strcmp(entity->classname(), "worldspawn")) {
            // HACK: workaround https://github.com/ericwa/tyrutils-ericw/issues/67
            // LoadEntities and FindLights need to be completely rewritten.
            continue;
        }
        if (entity->light.light != 0) {
            lights[totallights++] = entity;
        }
    }

    logprint("FindLights: %d total lights\n", totallights);
}

static void
SetupLightLeafnums(const bsp2_t *bsp)
{
    entity_t *entity;

    for (entity = entities; entity; entity = entity->next) {
        entity->leaf = Light_PointInLeaf(bsp, entity->origin);
    }
}

using entdict_t = std::map<std::string, std::string>;

/*
 * ==================
 * EntData_Parse
 * ==================
 */
std::vector<entdict_t>
EntData_Parse(const char *entdata)
{
    std::vector<entdict_t> result;
    const char *data = entdata;
    
    /* go through all the entities */
    while (1) {
        /* parse the opening brace */
        data = COM_Parse(data);
        if (!data)
            break;
        if (com_token[0] != '{')
            Error("%s: found %s when expecting {", __func__, com_token);
        
        /* Allocate a new entity */
        entdict_t entity;
        
        /* go through all the keys in this entity */
        while (1) {
            /* parse key */
            data = COM_Parse(data);
            if (!data)
                Error("%s: EOF without closing brace", __func__);
            
            std::string keystr { com_token };
            
            if (keystr == "}")
                break;
            if (keystr.length() > MAX_ENT_KEY - 1)
                Error("%s: Key length > %i", __func__, MAX_ENT_KEY - 1);
            
            /* parse value */
            data = COM_Parse(data);
            if (!data)
                Error("%s: EOF without closing brace", __func__);
            
            std::string valstring { com_token };
            
            if (valstring[0] == '}')
                Error("%s: closing brace without data", __func__);
            if (valstring.length() > MAX_ENT_VALUE - 1)
                Error("%s: Value length > %i", __func__, MAX_ENT_VALUE - 1);
            
            entity[keystr] = valstring;
        }
        
        result.push_back(entity);
    }
    
    logprint("%d entities read", static_cast<int>(result.size()));
    return result;
}

/*
 * ================
 * EntData_Write
 * ================
 */
std::string
EntData_Write(const std::vector<entdict_t> &ents)
{
    std::stringstream out;
    for (const auto &ent : ents) {
        out << "{\n";
        for (const auto &epair : ent) {
            out << "\"" << epair.first << "\" \"" << epair.second << "\"\n";
        }
        out << "}\n";
    }
    return out.str();
}

/*
 * ==================
 * LoadEntities
 * ==================
 */
void
LoadEntities(const bsp2_t *bsp)
{
    const char *data;
    entity_t *entity;
    char key[MAX_ENT_KEY];
    vec3_t vec, projangle;
    vec_t projfov;
    qboolean projangleknown;

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
        entity = new entity_t {};
        
        Entities_Insert(entity);

        /* Init some fields... */
        entity->anglescale = -1;

        projangle[0] = 20;
        projangle[1] = 0;
        projangle[2] = 0;
        projfov = 90;
        projangleknown = false;

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

            if (!strcmp(key, "lightmap_scale"))
            { /*this is parsed by the engine. make sure we're consistent*/
                strcpy(key, "_lightmap_scale");
                logprint("lightmap_scale should be _lightmap_scale\n");
            }

            entity->epairs[key] = com_token;

            if (!strcmp(key, "origin"))
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
                entity->formula = static_cast<light_formula_t>(atoi(com_token));
            else if (!strcmp(key, "mangle")) {
                if (!projangleknown)
                        scan_vec3(projangle, com_token, key);
                scan_vec3(vec, com_token, "mangle");
                vec_from_mangle(entity->spotvec, vec);
                entity->spotlight = true;
            } else if (!strcmp(key, "_color") || !strcmp(key, "color")) {
                scan_vec3(entity->light.color, com_token, "color");
                normalize_color_format(entity->light.color);
            } else if (!strcmp(key, "_sunlight")) {
                sunlight.setFloatValue(atof(com_token));
            } else if (!strcmp(key, "_sunlight_mangle") || !strcmp(key, "_sun_mangle")) {
                vec3_t tmp;
                scan_vec3(vec, com_token, "_sun_mangle");
                vec_from_mangle(tmp, vec);
                sunvec.setVec3Value(tmp);
            } else if (!strcmp(key, "_sunlight_color")) {
                vec3_t tmp;
                scan_vec3(tmp, com_token, "_sunlight_color");
                normalize_color_format(tmp);
                sunlight_color.setVec3Value(tmp);
            } else if (!strcmp(key, "_sun2")) {
                sun2.setFloatValue(atof(com_token));
            } else if (!strcmp(key, "_sun2_mangle")) {
                scan_vec3(vec, com_token, "_sun2_mangle");
                vec3_t tmp;
                vec_from_mangle(tmp, vec);
                sun2vec.setVec3Value(tmp);
            } else if (!strcmp(key, "_sun2_color")) {
                vec3_t tmp;
                scan_vec3(tmp, com_token, "_sun2_color");
                normalize_color_format(tmp);
                sun2_color.setVec3Value(tmp);
            } else if (!strcmp(key, "_sunlight2")) {
                sunlight2.setFloatValue(atof(com_token));
            } else if (!strcmp(key, "_sunlight3")) {
                sunlight3.setFloatValue(atof(com_token));
            } else if (!strcmp(key, "_sunlight2_color") || !strcmp(key, "_sunlight_color2")) {
                vec3_t tmp;
                scan_vec3(tmp, com_token, key);
                normalize_color_format(tmp);
                sunlight2_color.setVec3Value(tmp);
            } else if (!strcmp(key, "_sunlight3_color") || !strcmp(key, "_sunlight_color3")) {
                vec3_t tmp;
                scan_vec3(tmp, com_token, key);
                normalize_color_format(tmp);
                sunlight3_color.setVec3Value(tmp);
            } else if (!strcmp(key, "_minlight_color")) {
                vec3_t tmp;
                scan_vec3(tmp, com_token, "_minlight_color");
                normalize_color_format(tmp);
                minlight_color.setVec3Value(tmp);
            } else if (!strcmp(key, "_anglesense") || !strcmp(key, "_anglescale"))
                entity->anglescale = atof(com_token);
            else if (!strcmp(key, "_dirtdepth"))
                entity->dirtdepth = atof(com_token);
            else if (!strcmp(key, "_dirtmode"))
                entity->dirtmode = atoi(com_token);
            else if (!strcmp(key, "_dirtangle"))
                entity->dirtangle = atoi(com_token);
            else if (!strcmp(key, "_sunlight_dirt")) {
                sunlight_dirt.setFloatValue(atoi(com_token));
            } else if (!strcmp(key, "_sunlight2_dirt")) {
                sunlight2_dirt.setFloatValue(atoi(com_token));
            } else if (!strcmp(key, "_minlight_dirt"))
                entity->minlight_dirt = atoi(com_token);
            else if (!strcmp(key, "_dirtscale"))
                entity->dirtscale = atof(com_token);
            else if (!strcmp(key, "_dirtgain"))
                entity->dirtgain = atof(com_token);
            else if (!strcmp(key, "_dirt")) {
                entity->dirt = atoi(com_token);
                if (entity->dirt == 1 && !dirty.boolValue()) {
                    logprint("entity with \"_dirt\" \"1\" detected, enabling "
                        "dirtmapping.\n");
                    dirty.setFloatValue(1.0f);
                }
            }
            else if (!strcmp(key, "_project_texture")) {
                entity->projectedmip = FindProjectionTexture(bsp, com_token);
                if (entity->projectedmip == NULL) {
                    logprint("WARNING: light has \"_project_texture\" \"%s\", but this texture is not present in the bsp\n", com_token);
                }
            } else if (!strcmp(key, "_project_mangle"))
            {
                projangleknown = true;
                scan_vec3(projangle, com_token, key);
            }
            else if (!strcmp(key, "_project_fov"))
            {
                projfov = atof(com_token);
            }
            else if (!strcmp(key, "_bleed")) {
                entity->bleed = atoi(com_token);
            }
            else if (!strcmp(key, "_sunlight_penumbra")) {
                sun_deviance.setFloatValue(atof(com_token));
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
                lightmapgamma.setFloatValue(atof(com_token));
                logprint("using lightmap gamma value %f\n", lightmapgamma.floatValue());
            }
            else if (!strcmp(key, "_bounce")) {
                bounce.setFloatValue(atoi(com_token));
                logprint("_bounce set to %d\n", bounce.intValue());
            }
            else if (!strcmp(key, "_bouncescale")) {
                bouncescale.setFloatValue(atof(com_token));
                logprint("_bouncescale set to %f\n", bouncescale.floatValue());
            }
            else if (!strcmp(key, "_bouncecolorscale")) {
                float tmp = atof(com_token);
                tmp = qmin(qmax(tmp, 0.0f), 1.0f);
                bouncecolorscale.setFloatValue(tmp);
                logprint("_bouncecolorscale set to %f\n", bouncecolorscale.floatValue());
            }
        }

        /*
         * Check light entity fields and any global settings in worldspawn.
         */
        if (!strncmp(entity->classname(), "light", 5)) {
            if (entity->projectedmip) {
                if (entity->projectedmip->width > entity->projectedmip->height)
                    Matrix4x4_CM_MakeModelViewProj (projangle, entity->origin, projfov, CalcFov(projfov, entity->projectedmip->width, entity->projectedmip->height), entity->projectionmatrix);
                else
                    Matrix4x4_CM_MakeModelViewProj (projangle, entity->origin, CalcFov(projfov, entity->projectedmip->height, entity->projectedmip->width), projfov, entity->projectionmatrix);
            }

            CheckEntityFields(entity);
            num_lights++;
        }
        if (!strncmp(entity->classname(), "light", 5)) {
            if (ValueForKey(entity, "targetname")[0] && !entity->style) {
                char style[16];
                entity->style = LightStyleForTargetname(ValueForKey(entity, "targetname"));
                snprintf(style, sizeof(style), "%i", entity->style);
                SetKeyValue(entity, "style", style);
            }
        }
        if (!strcmp(entity->classname(), "worldspawn")) {
            if (entity->light.light > 0 && !minlight.floatValue()) {
                minlight.setFloatValue(entity->light.light);
                logprint("using minlight value %i from worldspawn.\n",
                         (int)minlight.floatValue());
            } else if (minlight.floatValue()) {
                logprint("Using minlight value %i from command line.\n",
                         (int)minlight.floatValue());
            }
            if (entity->anglescale >= 0 && entity->anglescale <= 1.0) {
                global_anglescale.setFloatValue(entity->anglescale);
                logprint("using global anglescale value %f from worldspawn.\n",
                         global_anglescale.floatValue());
            }

            if (entity->dist != 0.0) {
                scaledist.setFloatValue(entity->dist);
                logprint("using _dist value %f from worldspawn.\n",
                         scaledist.floatValue());
            }

            if (entity->range != 0.0) {
                rangescale.setFloatValue(entity->range);
                logprint("using _range value %f from worldspawn.\n",
                         rangescale.floatValue());
            }

            if (entity->dirtdepth) {
                dirtDepth.setFloatValue(entity->dirtdepth);
                logprint("Using dirtdepth value %f from worldspawn.\n", 
                        dirtDepth.floatValue());
            }
            if (entity->dirtmode) {
                dirtMode.setFloatValue(entity->dirtmode);
                logprint("Using dirtmode value %i from worldspawn.\n", 
                        (int)dirtMode.intValue());
            }
            if (entity->dirtscale) {
                dirtScale.setFloatValue(entity->dirtscale);
                logprint("Using dirtscale value %f from worldspawn.\n", 
                        dirtScale.floatValue());
            }
            if (entity->dirtgain) {
                dirtGain.setFloatValue(entity->dirtgain);
                logprint("Using dirtgain value %f from worldspawn.\n", 
                        dirtGain.floatValue());
            }
            if (entity->dirtangle) {
                dirtAngle.setFloatValue(entity->dirtangle);
                logprint("Using dirtangle value %f from worldspawn.\n",
                         dirtAngle.floatValue());
            }
            if (entity->dirt == 1) {
                globalDirt = true;
                dirty.setFloatValue(true);
                logprint("Global dirtmapping enabled in worldspawn.\n");
            }

            if (sunlight_dirt.intValue() == 1) {
                dirty.setFloatValue(true);
                logprint("Sunlight dirtmapping enabled in worldspawn.\n");
            } else if (sunlight_dirt.intValue() == -1) {
                logprint("Sunlight dirtmapping disabled in worldspawn.\n");
            }

            if (sunlight2_dirt.intValue() == 1) {
                dirty.setFloatValue(true);
                logprint("Sunlight2 dirtmapping enabled in worldspawn.\n");
            } else if (sunlight2_dirt.intValue() == -1) {
                logprint("Sunlight2 dirtmapping disabled in worldspawn.\n");
            }

            if (entity->minlight_dirt == 1) {
                minlightDirt.setFloatValue(true);
                dirty.setFloatValue(true);
                logprint("Minlight dirtmapping enabled in worldspawn.\n");
            } else if (entity->minlight_dirt == -1) {
                minlightDirt.setFloatValue(false);
                logprint("Minlight dirtmapping disabled in worldspawn.\n");
            } else {
                minlightDirt.setFloatValue(globalDirt);
            }
        }
    }

    if (!VectorCompare(*sunlight_color.vec3Value(), vec3_white) ||
        !VectorCompare(*minlight_color.vec3Value(), vec3_white) ||
        !VectorCompare(*sunlight2_color.vec3Value(), vec3_white) ||
        !VectorCompare(*sunlight3_color.vec3Value(), vec3_white)) {
        if (!write_litfile) {
            write_litfile = true;
            logprint("Colored light entities detected: "
                     ".lit output enabled.\n");
        }
    }

    logprint("%d entities read, %d are lights.\n", num_entities, num_lights);
}

static vec_t Plane_Dist(const vec3_t point, const dplane_t *plane)
{
    switch (plane->type)
    {
        case PLANE_X: return point[0] - plane->dist;
        case PLANE_Y: return point[1] - plane->dist;
        case PLANE_Z: return point[2] - plane->dist;
        default: return DotProduct(point, plane->normal) - plane->dist;
    }
}

static bool Light_PointInSolid_r(const bsp2_t *bsp, int nodenum, const vec3_t point )
{
    if (nodenum < 0) {
        bsp2_dleaf_t *leaf = bsp->dleafs + (-1 - nodenum);
        
        return leaf->contents == CONTENTS_SOLID
        || leaf->contents == CONTENTS_SKY;
    }
    
    const bsp2_dnode_t *node = &bsp->dnodes[nodenum];
    vec_t dist = Plane_Dist(point, &bsp->dplanes[node->planenum]);
    
    if (dist > 0.1)
        return Light_PointInSolid_r(bsp, node->children[0], point);
    else if (dist < -0.1)
        return Light_PointInSolid_r(bsp, node->children[1], point);
    else {
        // too close to the plane, check both sides
        return Light_PointInSolid_r(bsp, node->children[0], point)
        || Light_PointInSolid_r(bsp, node->children[1], point);
    }
}

// only check hull 0 of model 0 (world)
bool Light_PointInSolid(const bsp2_t *bsp, const vec3_t point )
{
    return Light_PointInSolid_r(bsp, bsp->dmodels[0].headnode[0], point);
}

static void
FixLightOnFace(const bsp2_t *bsp, const vec3_t point, vec3_t point_out)
{
    if (!Light_PointInSolid(bsp, point)) {
        VectorCopy(point, point_out);
        return;
    }
    
    for (int i = 0; i < 6; i++) {
        vec3_t testpoint;
        VectorCopy(point, testpoint);
        
        int axis = i/2;
        bool add = i%2;
        testpoint[axis] += (add ? 2 : -2); // sample points are 1 unit off faces. so nudge by 2 units, so the lights are above the sample points
        
        if (!Light_PointInSolid(bsp, testpoint)) {
            VectorCopy(testpoint, point_out);
            return;
        }
    }
    
    logprint("WARNING: couldn't nudge light in solid at %f %f %f\n",
             point[0], point[1], point[2]);
    VectorCopy(point, point_out);
    return;
}

void
FixLightsOnFaces(const bsp2_t *bsp)
{
    entity_t *entity;
    
    for (entity = entities; entity; entity = entity->next) {
        if (entity->light.light != 0) {
            FixLightOnFace(bsp, entity->origin, entity->origin);
        }
    }
}

void
SetupLights(const bsp2_t *bsp)
{
    // Creates more light entities, needs to be done before the rest
    MakeSurfaceLights(bsp);

    MatchTargets();
    JitterEntities();
    SetupSpotlights();
    SetupSuns();
    SetupSkyDome();
    FindLights();
    FixLightsOnFaces(bsp);
    SetupLightLeafnums(bsp);
}

const char *
ValueForKey(const entity_t *ent, const char *key)
{
    auto iter = ent->epairs.find(key);
    if (iter != ent->epairs.end()) {
        return (*iter).second.c_str();
    } else {
        return "";
    }
}

entity_t *
FindEntityWithKeyPair(const char *key, const char *value)
{
    entity_t *ent;
    std::string value_stdstring { value };

    for (ent = entities; ent; ent = ent->next) {
        auto iter = ent->epairs.find(key);
        if (iter != ent->epairs.end()) {
            if ((*iter).second == value_stdstring) {
                return ent;
            }
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
    size_t size;

    size = 0;
    for (entity = entities; entity; entity = entity->next) {
        if (!entity->epairs.size())
            continue;
        if (entity->generated)
            continue;
        size += 2; /* "{\n" */
        for (auto epair : entity->epairs) {
            /* 6 extra chars for quotes, space and newline */
            size += strlen(epair.first.c_str()) + strlen(epair.second.c_str()) + 6;
        }
        size += 2; /* "}\n" */
    }
    size += 1; /* zero terminator */

    return size;
}

const char *
CopyValueWithEscapeSequencesParsed(const char *value)
{
    char *result = copystring(value);
    const char *inptr = value;
    char *outptr = result;
    qboolean bold = false;
    
    for ( ; *inptr; inptr++) {
        if (inptr[0] == '\\' && inptr[1] == 'b') {
            bold = !bold; // Toggle bold
            inptr++;
            continue;
        } else {
            int mask = bold ? 128 : 0;
            *(outptr++) = *inptr | mask;
        }
    }
    *outptr = 0;
    
    return result;
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
    size_t space, length;
    char *pos;

    if (bsp->dentdata)
        free(bsp->dentdata);

    /* FIXME - why are we printing this here? */
    logprint("%i switchable light styles\n", static_cast<int>(lighttargetnames.size()));

    bsp->entdatasize = Get_EntityStringSize(entities);
    bsp->dentdata = (char *) malloc(bsp->entdatasize);
    if (!bsp->dentdata)
        Error("%s: allocation of %d bytes failed\n", __func__,
              bsp->entdatasize);

    space = bsp->entdatasize;
    pos = bsp->dentdata;
    for (entity = entities; entity; entity = entity->next) {
        if (!entity->epairs.size())
            continue;
        if (entity->generated)
            continue;

        length = snprintf(pos, space, "{\n");
        pos += length;
        space -= length;

        for (auto epair : entity->epairs) {
            const char *value;
            const bool parse_escape_sequences = true;
            if (parse_escape_sequences) {
                value = CopyValueWithEscapeSequencesParsed(epair.second.c_str());
            } else {
                value = epair.second.c_str();
            }
            
            length = snprintf(pos, space, "\"%s\" \"%s\"\n",
                              epair.first.c_str(), value);
            
            if (parse_escape_sequences) {
                free((void *)value);
            }
            
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

FILE *surflights_dump_file;
char surflights_dump_filename[1024];

void
WriteEntityToFile(FILE *f, entity_t *entity)
{
    if (!entity->epairs.size())
        return;
    
    fprintf(f, "{\n");
    for (auto epair : entity->epairs) {
        fprintf(f, "\"%s\" \"%s\"\n", epair.first.c_str(), epair.second.c_str());
    }
    fprintf(f, "}\n");
}

static void CreateSurfaceLight(const vec3_t origin, const vec3_t normal, const entity_t *surflight_template)
{
    entity_t *entity = DuplicateEntity(surflight_template);

    VectorCopy(origin, entity->origin);

    /* don't write to bsp */
    entity->generated = true;

    /* set spotlight vector based on face normal */
    if (atoi(ValueForKey(surflight_template, "_surface_spotlight"))) {
        entity->spotlight = true;
        VectorCopy(normal, entity->spotvec);
    }
    
    /* export it to a map file for debugging */
    if (surflight_dump) {
        SetKeyValue(entity, "origin", VecStr(origin));
        WriteEntityToFile(surflights_dump_file, entity);
    }
    
    num_lights++;
}

static void CreateSurfaceLightOnFaceSubdivision(const bsp2_dface_t *face, const modelinfo_t *face_modelinfo, const entity_t *surflight_template, const bsp2_t *bsp, int numverts, const vec_t *verts)
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
    if (offset == 0)
        offset = 2.0;
    
    VectorMA(midpoint, offset, normal, midpoint);

    /* Add the model offset */
    VectorAdd(midpoint, face_modelinfo->offset, midpoint);
    
    CreateSurfaceLight(midpoint, normal, surflight_template);
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
static void SubdividePolygon (const bsp2_dface_t *face, const modelinfo_t *face_modelinfo, const bsp2_t *bsp, int numverts, vec_t *verts, float subdivide_size)
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

        SubdividePolygon (face, face_modelinfo, bsp, f, front[0], subdivide_size);
        SubdividePolygon (face, face_modelinfo, bsp, b, back[0], subdivide_size);
        return;
    }

    const char *texname = Face_TextureName(bsp, face);

    for (i=0; i<num_surfacelight_templates; i++) {
        if (!Q_strcasecmp(texname, ValueForKey(surfacelight_templates[i], "_surface"))) {
            CreateSurfaceLightOnFaceSubdivision(face, face_modelinfo, surfacelight_templates[i], bsp, numverts, verts);
        }
    }
}

/*
 ================
 GL_SubdivideSurface - from GLQuake
 ================
 */
static void GL_SubdivideSurface (const bsp2_dface_t *face, const modelinfo_t *face_modelinfo, const bsp2_t *bsp)
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

    SubdividePolygon (face, face_modelinfo, bsp, face->numedges, verts[0], surflight_subdivide);
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

    if (surflight_dump) {
        strcpy(surflights_dump_filename, mapfilename);
        StripExtension(surflights_dump_filename);
        strcat(surflights_dump_filename, "-surflights.map");
        surflights_dump_file = fopen(surflights_dump_filename, "w");
    }
    
    /* Create the surface lights */
    qboolean *face_visited = (qboolean *)calloc(bsp->numfaces, sizeof(qboolean));
    for (i=0; i<bsp->numleafs; i++) {
        const bsp2_dleaf_t *leaf = bsp->dleafs + i;
        const bsp2_dface_t *surf;
        qboolean underwater = leaf->contents != CONTENTS_EMPTY;

        for (k = 0; k < leaf->nummarksurfaces; k++) {
            const modelinfo_t *face_modelinfo;
            int facenum = bsp->dmarksurfaces[leaf->firstmarksurface + k];

            surf = &bsp->dfaces[facenum];
            const char *texname = Face_TextureName(bsp, surf);

            face_modelinfo = ModelInfoForFace(bsp, facenum);
            
            /* Skip face with no modelinfo */
            if (face_modelinfo == NULL)
                continue;
            
            /* Ignore the underwater side of liquid surfaces */
            // FIXME: Use a Face_TextureName function for this
            if (texname[0] == '*' && underwater)
                continue;

            /* Skip if already handled */
            if (face_visited[facenum])
                continue;
            
            /* Mark as handled */
            face_visited[facenum] = true;

            /* Generate the lights */
            GL_SubdivideSurface(surf, face_modelinfo, bsp);
        }
    }
    free(face_visited);
    
    /* Hack: clear templates light value to 0 so they don't cast light */
    for (i=0;i<num_surfacelight_templates;i++) {
        surfacelight_templates[i]->light.light = 0;
    }
    
    if (surflights_dump_file) {
        fclose(surflights_dump_file);
        printf("wrote surface lights to '%s'\n", surflights_dump_filename);
    }
}
