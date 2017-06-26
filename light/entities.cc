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
#include <common/cmdlib.hh>

#include <light/light.hh>
#include <light/entities.hh>
#include <light/ltface.hh>
#include <common/bsputils.hh>

using strings = std::vector<std::string>;


std::vector<light_t> all_lights;
std::vector<sun_t> all_suns;
std::vector<entdict_t> entdicts;

const std::vector<light_t>& GetLights() {
    return all_lights;
}

const std::vector<sun_t>& GetSuns() {
    return all_suns;
}

/* surface lights */
static void MakeSurfaceLights(const bsp2_t *bsp);

// light_t

const char * light_t::classname() const {
    return ValueForKey(this, "classname");
}

/*
 * ============================================================================
 * ENTITY FILE PARSING
 * If a light has a targetname, generate a unique style in the 32-63 range
 * ============================================================================
 */

static std::vector<std::pair<std::string, int>> lightstyleForTargetname;

static bool IsSwitchableLightstyle(int style) {
    return style >= LIGHT_TARGETS_START
        && style < (LIGHT_TARGETS_START + MAX_LIGHT_TARGETS);
}

static entdict_t &WorldEnt()
{
    if (entdicts.size() == 0
        || entdicts.at(0)["classname"] != "worldspawn") {
        Error("WorldEnt() failed to get worldspawn");
    }
    return entdicts.at(0);
}

void SetWorldKeyValue(const std::string &key, const std::string &value)
{
    WorldEnt()[key] = value;
}
std::string WorldValueForKey(const std::string &key)
{
    return EntDict_StringForKey(WorldEnt(), key);
}

/**
 * Assigns a lightstyle number for the given non-empty targetname string
 * Reuses the existing lightstyle if this targetname was already assigned.
 * 
 * Pass an empty string to generate a new unique lightstyle.
 */
static int
LightStyleForTargetname(const std::string &targetname)
{
    // check if already assigned
    for (const auto &pr : lightstyleForTargetname) {
        if (pr.first == targetname && targetname.size() > 0) {
            return pr.second;
        }
    }
    
    // check if full
    if (lightstyleForTargetname.size() == MAX_LIGHT_TARGETS) {
        Error("%s: Too many unique light targetnames\n", __func__);
    }
    
    // generate a new style number and return it
    const int newStylenum = LIGHT_TARGETS_START + lightstyleForTargetname.size();
    lightstyleForTargetname.push_back(std::make_pair(targetname, newStylenum));
    
    if (verbose_log) {
        logprint("%s: Allocated lightstyle %d for targetname '%s'\n", __func__, newStylenum, targetname.c_str());
    }
    
    return newStylenum;
}

std::string
TargetnameForLightStyle(int style)
{
    for (const auto &pr : lightstyleForTargetname) {
        if (pr.second == style) {
            return pr.first;
        }
    }
    return "";
}

/*
 * ==================
 * MatchTargets
 *
 * sets light_t.targetent
 *
 * entdicts should not be modified after this (saves pointers to elements)
 * ==================
 */
static void
MatchTargets(void)
{
    for (light_t &entity : all_lights) {
        std::string targetstr { ValueForKey(&entity, "target") };
        if (!targetstr.length())
            continue;
        
        bool found = false;
        for (const entdict_t &target : entdicts) {
            if (targetstr == EntDict_StringForKey(target, "targetname")) {
                entity.targetent = &target;
                found = true;
                break;
            }
        }
    }
}

static std::string
EntDict_PrettyDescription(const bsp2_t *bsp, const entdict_t &entity)
{
    // get the submodel's bbox if it's a brush entity
    if (bsp != nullptr
        && EntDict_StringForKey(entity, "origin") == ""
        && EntDict_StringForKey(entity, "model") != "") {
        
        const std::string submodel_str = EntDict_StringForKey(entity, "model");
        const dmodel_t *info = BSP_DModelForModelString(bsp, submodel_str);
        
        if (info) {
            std::stringstream s;
            s << "brush entity with mins (";
            s << VecStrf(info->mins);
            s << ") maxs (";
            s << VecStrf(info->maxs);
            s << ") (" <<
            EntDict_StringForKey(entity, "classname") << ")";
            return s.str();
        }
    }
    
    std::stringstream s;
    s << "entity at (" <<
    EntDict_StringForKey(entity, "origin") << ") (" <<
    EntDict_StringForKey(entity, "classname") << ")";
    return s.str();
}

bool
EntDict_CheckNoEmptyValues(const bsp2_t *bsp, const entdict_t &entdict)
{
    bool ok = true;
    // empty values warning
    for (const auto &keyval : entdict) {
        if (keyval.first.empty() || keyval.second.empty()) {
            logprint("WARNING: %s has empty key/value \"%s\" \"%s\"\n",
                     EntDict_PrettyDescription(bsp, entdict).c_str(),
                     keyval.first.c_str(), keyval.second.c_str());
            ok = false;
        }
    }
    return ok;
}

/**
 * Checks `edicts` for unmatched targets/targetnames and prints warnings
 */
bool
EntDict_CheckTargetKeysMatched(const bsp2_t *bsp, const entdict_t &entity, const std::vector<entdict_t> &all_edicts)
{
    bool ok = true;
    
    const std::vector<std::string> targetKeys {
        "target", "killtarget",
        "target2", "angrytarget", "deathtarget" // from AD
    };
    
    std::string targetname = EntDict_StringForKey(entity, "targetname");
    
    // search for "target" values such that no entity has a matching "targetname"
    
    for (const auto &targetKey : targetKeys) {
        const auto targetVal = EntDict_StringForKey(entity, targetKey);
        if (!targetVal.length())
            continue;
        
        if (targetVal == targetname) {
            logprint("WARNING: %s has \"%s\" set to itself\n",
                     EntDict_PrettyDescription(bsp, entity).c_str(),
                     targetKey.c_str());
            ok = false;
            continue;
        }
        
        bool found = false;
        for (const entdict_t &target : all_edicts) {
            if (&target == &entity) {
                continue;
            }
            
            if (targetVal == EntDict_StringForKey(target, "targetname")) {
                found = true;
                break;
            }
        }
        
        if (!found) {
            logprint("WARNING: %s has unmatched \"%s\" (%s)\n",
                     EntDict_PrettyDescription(bsp, entity).c_str(),
                     targetKey.c_str(),
                     targetVal.c_str());
            ok = false;
        }
    }
    
    return ok;
}

bool
EntDict_CheckTargetnameKeyMatched(const bsp2_t *bsp, const entdict_t &entity, const std::vector<entdict_t> &all_edicts)
{
    // search for "targetname" values such that no entity has a matching "target"
    // accept any key name as a target, so we don't print false positive
    // if the map has "some_mod_specific_target" "foo"

    bool ok = true;
    
    const auto targetnameVal = EntDict_StringForKey(entity, "targetname");
    if (targetnameVal.length()) {
        bool found = false;
        for (const entdict_t &targetter : all_edicts) {
            if (&targetter == &entity) {
                continue;
            }
            
            for (const auto &targetter_keyval : targetter) {
                if (targetnameVal == targetter_keyval.second) {
                    found = true;
                    break;
                }
            }
            
            if (found) {
                break;
            }
        }
        
        if (!found) {
            logprint("WARNING: %s has targetname \"%s\" which is not targetted by anything.\n",
                     EntDict_PrettyDescription(bsp, entity).c_str(),
                     targetnameVal.c_str());
            ok = false;
        }
    }
    
    return ok;
}

static void
SetupSpotlights(void)
{
    for (light_t &entity : all_lights) {
        if (entity.targetent) {
            vec3_t targetOrigin;
            EntDict_VectorForKey(*entity.targetent, "origin", targetOrigin);
            VectorSubtract(targetOrigin, *entity.origin.vec3Value(), entity.spotvec);
            VectorNormalize(entity.spotvec);
            entity.spotlight = true;
        }
        if (entity.spotlight) {
            vec_t angle, angle2;

            angle = (entity.spotangle.floatValue() > 0) ? entity.spotangle.floatValue() : 40;
            entity.spotfalloff = -cos(angle / 2 * Q_PI / 180);

            angle2 = entity.spotangle2.floatValue();
            if (angle2 <= 0 || angle2 > angle)
                angle2 = angle;
            entity.spotfalloff2 = -cos(angle2 / 2 * Q_PI / 180);
        }
    }
}

static void
CheckEntityFields(const globalconfig_t &cfg, light_t *entity)
{
    if (entity->light.floatValue() == 0.0f)
        entity->light.setFloatValue(DEFAULTLIGHTLEVEL);

    if (entity->atten.floatValue() <= 0.0)
        entity->atten.setFloatValue(1.0);
    if (entity->anglescale.floatValue() < 0 || entity->anglescale.floatValue() > 1.0)
        entity->anglescale.setFloatValue(cfg.global_anglescale.floatValue());

    if (entity->getFormula() < LF_LINEAR || entity->getFormula() >= LF_COUNT) {
        static qboolean warned_once = true;
        if (!warned_once) {
            warned_once = true;
            logprint("WARNING: unknown formula number (%d) in delay field\n"
                     "   %s at (%s)\n"
                     "   (further formula warnings will be supressed)\n",
                     entity->getFormula(), entity->classname(),
                     VecStr(*entity->origin.vec3Value()));
        }
        entity->formula.setFloatValue(LF_LINEAR);
    }

    /* set up deviance and samples defaults */
    if (entity->deviance.floatValue() > 0 && entity->samples.intValue() == 0) {
        entity->samples.setFloatValue(16);
    }
    if (entity->deviance.floatValue() <= 0.0f || entity->samples.intValue() <= 1) {
        entity->deviance.setFloatValue(0.0f);
        entity->samples.setFloatValue(1);
    }
    /* For most formulas, we need to divide the light value by the number of
       samples (jittering) to keep the brightness approximately the same. */
    if (entity->getFormula() == LF_INVERSE
        || entity->getFormula() == LF_INVERSE2
        || entity->getFormula() == LF_INFINITE
        || (entity->getFormula() == LF_LOCALMIN && cfg.addminlight.boolValue())
        || entity->getFormula() == LF_INVERSE2A) {
        entity->light.setFloatValue(entity->light.floatValue() / entity->samples.intValue());
    }

    if (entity->style.intValue() < 0 || entity->style.intValue() > 254) {
        Error("Bad light style %i (must be 0-254)", entity->style.intValue());
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
Dirt_ResolveFlag(const globalconfig_t &cfg, int dirtInt)
{
        if (dirtInt == 1) return true;
        else if (dirtInt == -1) return false;
        else return cfg.globalDirt.boolValue();
}

/*
 * =============
 * AddSun
 * =============
 */
static void
AddSun(const globalconfig_t &cfg, vec3_t sunvec, vec_t light, const vec3_t color, int dirtInt)
{
    if (light == 0.0f)
        return;
    
    sun_t sun {};
    VectorCopy(sunvec, sun.sunvec);
    VectorNormalize(sun.sunvec);
    VectorScale(sun.sunvec, -16384, sun.sunvec);
    sun.sunlight = light;
    VectorCopy(color, sun.sunlight_color);
    sun.anglescale = cfg.global_anglescale.floatValue();
    sun.dirt = Dirt_ResolveFlag(cfg, dirtInt);

    // add to list
    all_suns.push_back(sun);

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
SetupSun(const globalconfig_t &cfg, vec_t light, const vec3_t color, const vec3_t sunvec_in)
{
    vec3_t sunvec;
    int i;
    int sun_num_samples = sunsamples;

    if (cfg.sun_deviance.floatValue() == 0) {
        sun_num_samples = 1;
    } else {
        logprint("using _sunlight_penumbra of %f degrees from worldspawn.\n", cfg.sun_deviance.floatValue());
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
                da = ( Random() * 2.0f - 1.0f ) * DEG2RAD(cfg.sun_deviance.floatValue());
                de = ( Random() * 2.0f - 1.0f ) * DEG2RAD(cfg.sun_deviance.floatValue());
            }
            while ( ( da * da + de * de ) > ( cfg.sun_deviance.floatValue() * cfg.sun_deviance.floatValue() ) );
            angle += da;
            elevation += de;

            /* create new vector */
            direction[ 0 ] = cos( angle ) * cos( elevation );
            direction[ 1 ] = sin( angle ) * cos( elevation );
            direction[ 2 ] = sin( elevation );
        }

        //printf( "sun %d is using vector %f %f %f\n", i, direction[0], direction[1], direction[2]);

        AddSun(cfg, direction, light, color, cfg.sunlight_dirt.intValue());
    }
}

static void
SetupSuns(const globalconfig_t &cfg)
{
    SetupSun(cfg, cfg.sunlight.floatValue(), *cfg.sunlight_color.vec3Value(), *cfg.sunvec.vec3Value());
    
    if (cfg.sun2.floatValue() != 0) {
        logprint("creating sun2\n");
        SetupSun(cfg, cfg.sun2.floatValue(), *cfg.sun2_color.vec3Value(), *cfg.sun2vec.vec3Value());
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
SetupSkyDome(const globalconfig_t &cfg)
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
        if ( cfg.sunlight2.floatValue() <= 0.0f && cfg.sunlight3.floatValue() <= 0.0f ) {
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
        if (cfg.sunlight2.floatValue() > 0) {
            logprint("using %d suns for _sunlight2. total light: %f color: %f %f %f\n", numSuns, cfg.sunlight2.floatValue(), (*cfg.sunlight2_color.vec3Value())[0], (*cfg.sunlight2_color.vec3Value())[1], (*cfg.sunlight2_color.vec3Value())[2]);
        }
        if (cfg.sunlight3.floatValue() > 0) {
            logprint("using %d suns for _sunlight3. total light: %f color: %f %f %f\n", numSuns, cfg.sunlight3.floatValue(), (*cfg.sunlight3_color.vec3Value())[0], (*cfg.sunlight3_color.vec3Value())[1], (*cfg.sunlight3_color.vec3Value())[2]);
        }
    
        const float sunlight2value = cfg.sunlight2.floatValue() / numSuns;
        const float sunlight3value = cfg.sunlight3.floatValue() / numSuns;

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
                            AddSun(cfg, direction, sunlight2value, *cfg.sunlight2_color.vec3Value(), cfg.sunlight2_dirt.intValue());
                        }

                        direction[ 2 ] = -direction[ 2 ];
                    
                        /* insert bottom hemisphere light */
                        if (sunlight3value > 0) {
                            AddSun(cfg, direction, sunlight3value, *cfg.sunlight3_color.vec3Value(), cfg.sunlight2_dirt.intValue());
                        }
                    
                        /* move */
                        angle += angleStep;
                }

                /* move */
                elevation += elevationStep;
                angle += angleStep / elevationSteps;
        }

        /* create vertical sun */
        VectorSet( direction, 0.0f, 0.0f, -1.0f );

        if (sunlight2value > 0) {
            AddSun(cfg, direction, sunlight2value, *cfg.sunlight2_color.vec3Value(), cfg.sunlight2_dirt.intValue());
        }
    
        VectorSet( direction, 0.0f, 0.0f, 1.0f );
    
        if (sunlight3value > 0) {
            AddSun(cfg, direction, sunlight3value, *cfg.sunlight3_color.vec3Value(), cfg.sunlight2_dirt.intValue());
        }
}

/*
 * =============
 * DuplicateEntity
 * =============
 */
static light_t
DuplicateEntity(const light_t &src)
{
    light_t entity { src };
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
JitterEntity(const light_t entity)
{
    /* jitter the light */
    for ( int j = 1; j < entity.samples.intValue(); j++ )
    {
        /* create a light */
        light_t light2 = DuplicateEntity(entity);
        light2.generated = true; // don't write generated light to bsp

        /* jitter it */
        vec3_t neworigin = {
            (*entity.origin.vec3Value())[ 0 ] + ( Random() * 2.0f - 1.0f ) * entity.deviance.floatValue(),
            (*entity.origin.vec3Value())[ 1 ] + ( Random() * 2.0f - 1.0f ) * entity.deviance.floatValue(),
            (*entity.origin.vec3Value())[ 2 ] + ( Random() * 2.0f - 1.0f ) * entity.deviance.floatValue()
        };
        light2.origin.setVec3Value(neworigin);
        
        all_lights.push_back(light2);
    }
}

static void
JitterEntities()
{
    // We will append to the list during iteration.
    const size_t starting_size = all_lights.size();
    for (size_t i=0; i<starting_size; i++) {
        JitterEntity(all_lights.at(i));
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
    Matrix4_Multiply(modelview, Matrix4x4_CM_NewRotation(t2, -viewangles[2],  1, 0, 0), tempmat); //roll
    Matrix4_Multiply(tempmat, Matrix4x4_CM_NewRotation(t2, viewangles[1],  0, 1, 0), modelview); //pitch
    Matrix4_Multiply(modelview, Matrix4x4_CM_NewRotation(t2, -viewangles[0],  0, 0, 1), tempmat); //yaw

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

std::string
EntDict_StringForKey(const entdict_t &dict, const std::string key)
{
    auto it = dict.find(key);
    if (it != dict.end()) {
        return it->second;
    }
    return "";
}

float
EntDict_FloatForKey(const entdict_t &dict, const std::string key)
{
    auto s = EntDict_StringForKey(dict, key);
    if (s == "")
        return 0;
    
    try {
        return std::stof(s);
    } catch (std::exception &e) {
        return 0.0f;
    }
}

void
EntDict_RemoveValueForKey(entdict_t &dict, const std::string key)
{
    auto it = dict.find(key);
    if (it != dict.end()) {
        dict.erase(it);
    }
    Q_assert(dict.find(key) == dict.end());
}

static std::string
ParseEscapeSequences(const std::string &input)
{
    std::stringstream ss;
    bool bold = false;
    
    for (size_t i=0; i<input.length(); i++) {
        if (input.at(i) == '\\'
            && (i+1) < input.length()
            && input.at(i+1) == 'b')
        {
            bold = !bold;
            i++;
        } else {
            uint8_t c = static_cast<uint8_t>(input.at(i));
            if (bold) {
                c |= 128;
            }
            ss.put(static_cast<char>(c));
        }
    }
    return ss.str();
}

/*
 * ==================
 * LoadEntities
 * ==================
 */
void
LoadEntities(const globalconfig_t &cfg, const bsp2_t *bsp)
{
    logprint("--- LoadEntities ---\n");
    
    entdicts = EntData_Parse(bsp->dentdata);
    
    // Make warnings
    for (auto &entdict : entdicts) {
        EntDict_CheckNoEmptyValues(bsp, entdict);
        EntDict_CheckTargetKeysMatched(bsp, entdict, entdicts);
        EntDict_CheckTargetnameKeyMatched(bsp, entdict, entdicts);
    }
    
    // First pass: make permanent changes to the bsp entdata that we will write out
    // at the end of the light process.
    for (auto &entdict : entdicts) {
        
        // fix "lightmap_scale"
        std::string lmscale = EntDict_StringForKey(entdict, "lightmap_scale");
        if (lmscale != "") {
            logprint("lightmap_scale should be _lightmap_scale\n");
            
            entdict.erase(entdict.find("lightmap_scale"));
            entdict["_lightmap_scale"] = lmscale;
        }
        
        // setup light styles for switchable lights
        std::string classname = EntDict_StringForKey(entdict, "classname");
        if (classname.find("light") == 0) {
            std::string targetname = EntDict_StringForKey(entdict, "targetname");
            if (targetname != "") {
                const int style = LightStyleForTargetname(targetname);
                entdict["style"] = std::to_string(style);
            }
        }
        
        // setup light styles for dynamic shadow entities
        if (EntDict_StringForKey(entdict, "_switchableshadow") == "1") {
            std::string targetname = EntDict_StringForKey(entdict, "targetname");
            // if targetname is "", generates a new unique lightstyle
            const int style = LightStyleForTargetname(targetname);
            // TODO: Configurable key?
            entdict["switchshadstyle"] = std::to_string(style);
        }
        
        // parse escape sequences
        for (auto &epair : entdict) {
            epair.second = ParseEscapeSequences(epair.second);
        }
    }
    
    /* handle worldspawn */
    for (const auto &epair : WorldEnt()) {
        SetGlobalSetting(epair.first, epair.second, false);
    }
    /* apply side effects of settings (in particular "dirt") */
    FixupGlobalSettings();
    
    Q_assert(all_lights.size() == 0);
    if (nolights) {
        return;
    }
    
    /* go through all the entities */
    for (const auto &entdict : entdicts) {
        
        /*
         * Check light entity fields and any global settings in worldspawn.
         */
        if (EntDict_StringForKey(entdict, "classname").find("light") == 0) {
            /* Allocate a new entity */
            light_t entity {};
            
            // save pointer to the entdict
            entity.epairs = &entdict;
            
            // populate settings
            entity.settings().setSettings(*entity.epairs, false);
            
            if (entity.mangle.isChanged()) {
                const qvec3f temp = vec_from_mangle(vec3_t_to_glm(*entity.mangle.vec3Value()));
                glm_to_vec3_t(temp, entity.spotvec);
                entity.spotlight = true;
                
                if (!entity.projangle.isChanged()) {
                    // copy from mangle
                    entity.projangle.setVec3Value(*entity.mangle.vec3Value());
                }
            }
            
            if (entity.project_texture.stringValue() != "") {
                auto texname = entity.project_texture.stringValue();
                entity.projectedmip = FindProjectionTexture(bsp, texname.c_str());
                if (entity.projectedmip == NULL) {
                    logprint("WARNING: light has \"_project_texture\" \"%s\", but this texture is not present in the bsp\n", texname.c_str());
                }
            }
            
            if (entity.projectedmip) {
                if (entity.projectedmip->width > entity.projectedmip->height)
                    Matrix4x4_CM_MakeModelViewProj (*entity.projangle.vec3Value(), *entity.origin.vec3Value(), entity.projfov.floatValue(), CalcFov(entity.projfov.floatValue(), entity.projectedmip->width, entity.projectedmip->height), entity.projectionmatrix);
                else
                    Matrix4x4_CM_MakeModelViewProj (*entity.projangle.vec3Value(), *entity.origin.vec3Value(), CalcFov(entity.projfov.floatValue(), entity.projectedmip->height, entity.projectedmip->width), entity.projfov.floatValue(), entity.projectionmatrix);
            }

            CheckEntityFields(cfg, &entity);
            
            all_lights.push_back(entity);
        }
    }

    logprint("%d entities read, %d are lights.\n",
             static_cast<int>(entdicts.size()),
             static_cast<int>(all_lights.size()));
}

static void
FixLightOnFace(const bsp2_t *bsp, const vec3_t point, vec3_t point_out)
{
    // FIXME: Check all shadow casters
    if (!Light_PointInWorld(bsp, point)) {
        VectorCopy(point, point_out);
        return;
    }
    
    for (int i = 0; i < 6; i++) {
        vec3_t testpoint;
        VectorCopy(point, testpoint);
        
        int axis = i/2;
        bool add = i%2;
        testpoint[axis] += (add ? 2 : -2); // sample points are 1 unit off faces. so nudge by 2 units, so the lights are above the sample points
    
        // FIXME: Check all shadow casters
        if (!Light_PointInWorld(bsp, testpoint)) {
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
    for (light_t &entity : all_lights) {
        if (entity.light.floatValue() != 0) {
            vec3_t tmp;
            FixLightOnFace(bsp, *entity.origin.vec3Value(), tmp);
            entity.origin.setVec3Value(tmp);
        }
    }
}

void EstimateVisibleBoundsAtPoint(const vec3_t point, vec3_t mins, vec3_t maxs)
{
    const int N = 32;
    const int N2 = N*N;
    
    raystream_t *rs = MakeRayStream(N2);
    
    AABB_Init(mins, maxs, point);
    for (int x=0; x<N; x++) {
        for (int y=0; y<N; y++) {
            const vec_t u1 = static_cast<float>(x) / static_cast<float>(N - 1);
            const vec_t u2 = static_cast<float>(y) / static_cast<float>(N - 1);
            
            vec3_t dir;
            UniformPointOnSphere(dir, u1, u2);
        
            rs->pushRay(0, point, dir, 65536.0f, nullptr);
        }
    }
    
    rs->tracePushedRaysIntersection();
    
    for (int i=0; i<N2; i++) {
        const float dist = rs->getPushedRayHitDist(i);
        vec3_t dir;
        rs->getPushedRayDir(i, dir);
        
        // get the intersection point
        vec3_t stop;
        VectorMA(point, dist, dir, stop);
        
        AABB_Expand(mins, maxs, stop);
    }
    
    // grow it by 25% in each direction
    vec3_t size;
    AABB_Size(mins, maxs, size);
    VectorScale(size, 0.25, size);
    AABB_Grow(mins, maxs, size);
    
    /*
    logprint("light at %f %f %f has mins %f %f %f maxs %f %f %f\n",
           point[0],
           point[1],
           point[2],
           mins[0],
           mins[1],
           mins[2],
           maxs[0],
           maxs[1],
           maxs[2]);
    */
    
    delete rs;
}

static void EstimateLightAABB(light_t *light)
{
    EstimateVisibleBoundsAtPoint(*light->origin.vec3Value(), light->mins, light->maxs);
}

static void *EstimateLightAABBThread(void *arg)
{
    while (1) {
        const int i = GetThreadWork();
        if (i == -1)
            break;
        
        EstimateLightAABB(&all_lights.at(i));
    }
    return nullptr;
}

void EstimateLightVisibility(void)
{
    if (novisapprox)
        return;
    
    logprint("--- EstimateLightVisibility ---\n");
    
    RunThreadsOn(0, static_cast<int>(all_lights.size()), EstimateLightAABBThread, nullptr);
}

void
SetupLights(const globalconfig_t &cfg, const bsp2_t *bsp)
{
    logprint("SetupLights: %d initial lights\n", static_cast<int>(all_lights.size()));
    
    // Creates more light entities, needs to be done before the rest
    MakeSurfaceLights(bsp);
             
    logprint("SetupLights: %d after surface lights\n", static_cast<int>(all_lights.size()));
    
    JitterEntities();
                      
    logprint("SetupLights: %d after jittering\n", static_cast<int>(all_lights.size()));
    
    const size_t final_lightcount = all_lights.size();
    
    MatchTargets();
    SetupSpotlights();
    SetupSuns(cfg);
    SetupSkyDome(cfg);
    FixLightsOnFaces(bsp);
    EstimateLightVisibility();
    
    logprint("Final count: %d lights %d suns in use.\n",
             static_cast<int>(all_lights.size()),
             static_cast<int>(all_suns.size()));
    
    
    Q_assert(final_lightcount == all_lights.size());
}

const char *
ValueForKey(const light_t *ent, const char *key)
{
    auto iter = ent->epairs->find(key);
    if (iter != ent->epairs->end()) {
        return (*iter).second.c_str();
    } else {
        return "";
    }
}

const entdict_t *FindEntDictWithKeyPair(const std::string &key, const std::string &value)
{
    for (const auto &entdict : entdicts) {
        if (EntDict_StringForKey(entdict, key) == value) {
            return &entdict;
        }
    }
    return nullptr;
}

void
EntDict_VectorForKey(const entdict_t &ent, const std::string &key, vec3_t vec)
{
    std::string value = EntDict_StringForKey(ent, key);
    
    VectorSet(vec, 0, 0, 0);
    sscanf(value.c_str(), "%f %f %f", &vec[0], &vec[1], &vec[2]);
}

/*
 * ================
 * WriteEntitiesToString
 * 
 * Re-write the entdata BSP lump because switchable lights need styles set.
 * ================
 */
void
WriteEntitiesToString(bsp2_t *bsp)
{
    std::string entdata = EntData_Write(entdicts);
    
    if (bsp->dentdata)
        free(bsp->dentdata);

    /* FIXME - why are we printing this here? */
    logprint("%i switchable light styles (%d max)\n",
             static_cast<int>(lightstyleForTargetname.size()),
             MAX_LIGHT_TARGETS);

    bsp->entdatasize = entdata.size() + 1; // +1 for a null byte at the end
    bsp->dentdata = (char *) calloc(bsp->entdatasize, 1);
    if (!bsp->dentdata)
        Error("%s: allocation of %d bytes failed\n", __func__,
              bsp->entdatasize);

    memcpy(bsp->dentdata, entdata.data(), entdata.size());
    
    Q_assert(0 == bsp->dentdata[bsp->entdatasize - 1]);
}


/*
 * =======================================================================
 *                            SURFACE LIGHTS
 * =======================================================================
 */

std::vector<light_t> surfacelight_templates;

FILE *surflights_dump_file;
char surflights_dump_filename[1024];

static void
SurfLights_WriteEntityToFile(FILE *f, light_t *entity, const vec3_t pos)
{
    Q_assert(entity->epairs != nullptr);
    
    entdict_t epairs { *entity->epairs };
    EntDict_RemoveValueForKey(epairs, "_surface");
    epairs["origin"] = VecStr(pos);
    
    std::string entstring = EntData_Write({ epairs });
    fwrite(entstring.data(), 1, entstring.size(), f);
}

static void CreateSurfaceLight(const vec3_t origin, const vec3_t normal, const light_t *surflight_template)
{
    light_t entity = DuplicateEntity(*surflight_template);

    entity.origin.setVec3Value(origin);

    /* don't write to bsp */
    entity.generated = true;

    /* set spotlight vector based on face normal */
    if (atoi(ValueForKey(surflight_template, "_surface_spotlight"))) {
        entity.spotlight = true;
        VectorCopy(normal, entity.spotvec);
    }
    
    /* export it to a map file for debugging */
    if (surflight_dump) {
        SurfLights_WriteEntityToFile(surflights_dump_file, &entity, origin);
    }
    
    all_lights.push_back(entity);
}

static void CreateSurfaceLightOnFaceSubdivision(const bsp2_dface_t *face, const modelinfo_t *face_modelinfo, const light_t *surflight_template, const bsp2_t *bsp, int numverts, const vec_t *verts)
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

    for (const auto &surflight : surfacelight_templates) {
        if (!Q_strcasecmp(texname, ValueForKey(&surflight, "_surface"))) {
            CreateSurfaceLightOnFaceSubdivision(face, face_modelinfo, &surflight, bsp, numverts, verts);
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
    int i, k;

    for (light_t &entity : all_lights) {
        std::string tex = ValueForKey(&entity, "_surface");
        if (tex != "") {
            surfacelight_templates.push_back(entity); // makes a copy

            // Hack: clear templates light value to 0 so they don't cast light
            entity.light.setFloatValue(0);
            
            logprint("Creating surface lights for texture \"%s\" from template at (%s)\n",
                   tex.c_str(), ValueForKey(&entity, "origin"));
        }
    }

    if (!surfacelight_templates.size())
        return;

    if (surflight_dump) {
        strcpy(surflights_dump_filename, mapfilename);
        StripExtension(surflights_dump_filename);
        strcat(surflights_dump_filename, "-surflights.map");
        surflights_dump_file = fopen(surflights_dump_filename, "w");
    }
    
    /* Create the surface lights */
    std::vector<bool> face_visited(static_cast<size_t>(bsp->numfaces), false);
    
    for (i=0; i<bsp->numleafs; i++) {
        const bsp2_dleaf_t *leaf = bsp->dleafs + i;
        const bsp2_dface_t *surf;
        qboolean underwater = leaf->contents != CONTENTS_EMPTY;

        for (k = 0; k < leaf->nummarksurfaces; k++) {
            const modelinfo_t *face_modelinfo;
            int facenum = bsp->dmarksurfaces[leaf->firstmarksurface + k];

            surf = BSP_GetFace(bsp, facenum);
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
            if (face_visited.at(facenum))
                continue;
            
            /* Mark as handled */
            face_visited.at(facenum) = true;

            /* Generate the lights */
            GL_SubdivideSurface(surf, face_modelinfo, bsp);
        }
    }
    
    if (surflights_dump_file) {
        fclose(surflights_dump_file);
        printf("wrote surface lights to '%s'\n", surflights_dump_filename);
    }
}
