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

#include <algorithm>
#include <cstring>
#include <fstream>
#include <common/cmdlib.hh>
#include <common/parser.hh>

#include <light/light.hh>
#include <light/entities.hh>
#include <light/ltface.hh>
#include <common/bsputils.hh>

std::vector<light_t> all_lights;
std::vector<sun_t> all_suns;
std::vector<entdict_t> entdicts;
static std::vector<entdict_t> radlights;

const std::vector<light_t> &GetLights()
{
    return all_lights;
}

const std::vector<sun_t> &GetSuns()
{
    return all_suns;
}

/* surface lights */
static void MakeSurfaceLights(const mbsp_t *bsp);

// light_t

const char *light_t::classname() const
{
    return ValueForKey(this, "classname");
}

/*
 * ============================================================================
 * ENTITY FILE PARSING
 * If a light has a targetname, generate a unique style in the 32-63 range
 * ============================================================================
 */

static std::vector<std::pair<std::string, int>> lightstyleForTargetname;

#define MAX_SWITCHABLE_STYLES 64

static entdict_t &WorldEnt()
{
    if (entdicts.size() == 0 || entdicts.at(0).get("classname") != "worldspawn") {
        Error("WorldEnt() failed to get worldspawn");
    }
    return entdicts.at(0);
}

void SetWorldKeyValue(const std::string &key, const std::string &value)
{
    WorldEnt().set(key, value);
}
const std::string &WorldValueForKey(const std::string &key)
{
    return EntDict_StringForKey(WorldEnt(), key);
}

/**
 * Assigns a lightstyle number for the given non-empty targetname string
 * Reuses the existing lightstyle if this targetname was already assigned.
 *
 * Pass an empty string to generate a new unique lightstyle.
 */
static int LightStyleForTargetname(const settings::worldspawn_keys &cfg, const std::string &targetname)
{
    // check if already assigned
    for (const auto &pr : lightstyleForTargetname) {
        if (pr.first == targetname && targetname.size() > 0) {
            return pr.second;
        }
    }

    // generate a new style number and return it
    const int newStylenum = cfg.compilerstyle_start.value() + lightstyleForTargetname.size();

    // check if full
    if (newStylenum >= MAX_SWITCHABLE_STYLES) {
        FError("Too many unique light targetnames (max={})\n", MAX_SWITCHABLE_STYLES);
    }

    lightstyleForTargetname.emplace_back(
        targetname, newStylenum); // mxd. https://clang.llvm.org/extra/clang-tidy/checks/modernize-use-emplace.html

    LogPrint(LOG_VERBOSE, "Allocated lightstyle {} for targetname '{}'\n", newStylenum, targetname);

    return newStylenum;
}

std::string TargetnameForLightStyle(int style)
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
static void MatchTargets(void)
{
    for (light_t &entity : all_lights) {
        std::string targetstr{ValueForKey(&entity, "target")};
        if (!targetstr.length())
            continue;

        for (const entdict_t &target : entdicts) {
            if (string_iequals(targetstr, EntDict_StringForKey(target, "targetname"))) {
                entity.targetent = &target;
                break;
            }
        }
    }
}

static std::string EntDict_PrettyDescription(const mbsp_t *bsp, const entdict_t &entity)
{
    // get the submodel's bbox if it's a brush entity
    if (bsp != nullptr && EntDict_StringForKey(entity, "origin") == "" && EntDict_StringForKey(entity, "model") != "") {
        const std::string &submodel_str = EntDict_StringForKey(entity, "model");
        const dmodelh2_t *info = BSP_DModelForModelString(bsp, submodel_str);

        if (info) {
            return fmt::format("brush entity with mins [{}] maxs [{}] ({})", info->mins, info->maxs,
                EntDict_StringForKey(entity, "classname"));
        }
    }

    return fmt::format(
        "entity at ({}) ({})", EntDict_StringForKey(entity, "origin"), EntDict_StringForKey(entity, "classname"));
}

bool EntDict_CheckNoEmptyValues(const mbsp_t *bsp, const entdict_t &entdict)
{
    bool ok = true;
    // empty values warning
    for (const auto &keyval : entdict) {
        if (keyval.first.empty() || keyval.second.empty()) {
            LogPrint("WARNING: {} has empty key/value \"{}\" \"{}\"\n", EntDict_PrettyDescription(bsp, entdict),
                keyval.first, keyval.second);
            ok = false;
        }
    }
    return ok;
}

/**
 * Checks `edicts` for unmatched targets/targetnames and prints warnings
 */
bool EntDict_CheckTargetKeysMatched(
    const mbsp_t *bsp, const entdict_t &entity, const std::vector<entdict_t> &all_edicts)
{
    bool ok = true;

    // TODO: what if we just do this for any key that contains `target` not immediately followed by `name`?
    const std::vector<std::string> targetKeys{
        "target", "killtarget", "target2", "angrytarget", "deathtarget" // from AD
    };

    const std::string &targetname = EntDict_StringForKey(entity, "targetname");

    // search for "target" values such that no entity has a matching "targetname"

    for (const auto &targetKey : targetKeys) {
        const auto &targetVal = EntDict_StringForKey(entity, targetKey);
        if (!targetVal.length())
            continue;

        if (targetVal == targetname) {
            LogPrint("WARNING: {} has \"{}\" set to itself\n", EntDict_PrettyDescription(bsp, entity), targetKey);
            ok = false;
            continue;
        }

        bool found = false;
        for (const entdict_t &target : all_edicts) {
            if (&target == &entity) {
                continue;
            }

            if (string_iequals(targetVal, EntDict_StringForKey(target, "targetname"))) {
                found = true;
                break;
            }
        }

        if (!found) {
            LogPrint("WARNING: {} has unmatched \"{}\" ({})\n", EntDict_PrettyDescription(bsp, entity), targetKey,
                targetVal);
            ok = false;
        }
    }

    return ok;
}

bool EntDict_CheckTargetnameKeyMatched(
    const mbsp_t *bsp, const entdict_t &entity, const std::vector<entdict_t> &all_edicts)
{
    // search for "targetname" values such that no entity has a matching "target"
    // accept any key name as a target, so we don't print false positive
    // if the map has "some_mod_specific_target" "foo"

    bool ok = true;

    const auto &targetnameVal = EntDict_StringForKey(entity, "targetname");
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
            LogPrint("WARNING: {} has targetname \"{}\", which is not targeted by anything.\n",
                EntDict_PrettyDescription(bsp, entity), targetnameVal);
            ok = false;
        }
    }

    return ok;
}

static void SetupSpotlights(const settings::worldspawn_keys &cfg)
{
    for (light_t &entity : all_lights) {
        vec_t targetdist = 0.0; // mxd
        if (entity.targetent) {
            qvec3d targetOrigin = EntDict_VectorForKey(*entity.targetent, "origin");
            entity.spotvec = targetOrigin - entity.origin.value();
            targetdist = qv::normalizeInPlace(entity.spotvec); // mxd
            entity.spotlight = true;
        }
        if (entity.spotlight) {
            const vec_t angle = (entity.spotangle.value() > 0) ? entity.spotangle.value() : 40;
            entity.spotfalloff = -cos(angle / 2 * Q_PI / 180);

            vec_t angle2 = entity.spotangle2.value();
            if (angle2 <= 0 || angle2 > angle)
                angle2 = angle;
            entity.spotfalloff2 = -cos(angle2 / 2 * Q_PI / 180);

            // mxd. Apply autofalloff?
            if (targetdist > 0.0f && entity.falloff.value() == 0 && cfg.spotlightautofalloff.value()) {
                const vec_t coneradius = targetdist * tan(angle / 2 * Q_PI / 180);
                entity.falloff.setValue(targetdist + coneradius);
            }
        }
    }
}

static void CheckEntityFields(const settings::worldspawn_keys &cfg, light_t *entity)
{
    if (entity->light.value() == 0.0f)
        entity->light.setValue(DEFAULTLIGHTLEVEL);

    if (entity->atten.value() <= 0.0)
        entity->atten.setValue(1.0);
    if (entity->anglescale.value() < 0 || entity->anglescale.value() > 1.0)
        entity->anglescale.setValue(cfg.global_anglescale.value());

    // mxd. Warn about unsupported _falloff / delay combos...
    if (entity->falloff.value() > 0.0f && entity->getFormula() != LF_LINEAR) {
        LogPrint("WARNING: _falloff is currently only supported on linear (delay 0) lights\n"
                 "   {} at [{}]\n",
            entity->classname(), entity->origin.value());
        entity->falloff.setValue(0.0f);
    }

    /* set up deviance and samples defaults */
    if (entity->deviance.value() > 0 && entity->samples.value() == 0) {
        entity->samples.setValue(16);
    }

    if (entity->deviance.value() <= 0.0f || entity->samples.value() <= 1) {
        entity->deviance.setValue(0.0f);
        entity->samples.setValue(1);
    }

    /* For most formulas, we need to divide the light value by the number of
       samples (jittering) to keep the brightness approximately the same. */
    if (entity->getFormula() == LF_INVERSE || entity->getFormula() == LF_INVERSE2 ||
        entity->getFormula() == LF_INFINITE || (entity->getFormula() == LF_LOCALMIN && cfg.addminlight.value()) ||
        entity->getFormula() == LF_INVERSE2A) {
        entity->light.setValue(entity->light.value() / entity->samples.value());
    }
}

/*
 * =============
 * Dirt_ResolveFlag
 *
 * Resolves a dirt flag (0=default, 1=enable, -1=disable) to a boolean
 * =============
 */
static bool Dirt_ResolveFlag(const settings::worldspawn_keys &cfg, int dirtInt)
{
    if (dirtInt == 1)
        return true;
    else if (dirtInt == -1)
        return false;
    else
        return cfg.globalDirt.value();
}

/*
 * =============
 * AddSun
 * =============
 */
static void AddSun(const settings::worldspawn_keys &cfg, const qvec3d &sunvec, vec_t light, const qvec3d &color,
    int dirtInt, vec_t sun_anglescale, const int style, const std::string &suntexture)
{
    if (light == 0.0f)
        return;

    // add to list
    sun_t &sun = all_suns.emplace_back();
    sun.sunvec = qv::normalize(sunvec) * -16384;
    sun.sunlight = light;
    sun.sunlight_color = color;
    sun.anglescale = sun_anglescale;
    sun.dirt = Dirt_ResolveFlag(cfg, dirtInt);
    sun.style = style;
    sun.suntexture = suntexture;

    // fmt::print( "sun is using vector {} {} {} light {} color {} {} {} anglescale {} dirt {} resolved to {}\n",
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
static void SetupSun(const settings::worldspawn_keys &cfg, vec_t light, const qvec3d &color, const qvec3d &sunvec_in,
    const vec_t sun_anglescale, const vec_t sun_deviance, const int sunlight_dirt, const int style,
    const std::string &suntexture)
{
    int i;
    int sun_num_samples = (sun_deviance == 0 ? 1 : options.sunsamples.value()); // mxd
    vec_t sun_deviance_rad = DEG2RAD(sun_deviance); // mxd
    vec_t sun_deviance_sq = sun_deviance * sun_deviance; // mxd

    qvec3d sunvec = qv::normalize(sunvec_in);

    // fmt::print( "input sunvec {} {} {}. deviance is {}, {} samples\n",sunvec[0],sunvec[1], sunvec[2], sun_deviance,
    // sun_num_samples);

    /* set photons */
    light /= sun_num_samples;

    for (i = 0; i < sun_num_samples; i++) {
        qvec3d direction;

        /* calculate sun direction */
        if (i == 0) {
            direction = sunvec;
        } else {
            vec_t da, de;
            vec_t d = sqrt(sunvec[0] * sunvec[0] + sunvec[1] * sunvec[1]);
            vec_t angle = atan2(sunvec[1], sunvec[0]);
            vec_t elevation = atan2(sunvec[2], d);

            /* jitter the angles (loop to keep random sample within sun->deviance steridians) */
            do {
                da = (Random() * 2.0f - 1.0f) * sun_deviance_rad;
                de = (Random() * 2.0f - 1.0f) * sun_deviance_rad;
            } while ((da * da + de * de) > sun_deviance_sq);
            angle += da;
            elevation += de;

            /* create new vector */
            direction[0] = cos(angle) * cos(elevation);
            direction[1] = sin(angle) * cos(elevation);
            direction[2] = sin(elevation);
        }

        // fmt::print( "sun {} is using vector {} {} {}\n", i, direction[0], direction[1], direction[2]);

        AddSun(cfg, direction, light, color, sunlight_dirt, sun_anglescale, style, suntexture);
    }
}

static void SetupSuns(const settings::worldspawn_keys &cfg)
{
    for (light_t &entity : all_lights) {
        // mxd. Arghrad-style sun setup
        if (entity.sun.value() && entity.light.value() > 0) {
            // Set sun vector
            qvec3d sunvec;
            if (entity.targetent) {
                qvec3d target_pos = EntDict_VectorForKey(*entity.targetent, "origin");
                sunvec = target_pos - entity.origin.value();
            } else if (qv::length2(entity.mangle.value()) > 0) {
                sunvec = entity.mangle.value();
            } else { // Use { 0, 0, 0 } as sun target...
                LogPrint("WARNING: sun missing target, entity origin used.\n");
                sunvec = -entity.origin.value();
            }

            // Add the sun
            SetupSun(cfg, entity.light.value(), entity.color.value(), sunvec, entity.anglescale.value(),
                entity.deviance.value(), entity.dirt.value(), entity.style.value(), entity.suntexture.value());

            // Disable the light itself...
            entity.light.setValue(0.0f);
        }
    }

    SetupSun(cfg, cfg.sunlight.value(), cfg.sunlight_color.value(), cfg.sunvec.value(), cfg.global_anglescale.value(),
        cfg.sun_deviance.value(), cfg.sunlight_dirt.value(), 0, "");

    if (cfg.sun2.value() != 0) {
        LogPrint("creating sun2\n");
        SetupSun(cfg, cfg.sun2.value(), cfg.sun2_color.value(), cfg.sun2vec.value(), cfg.global_anglescale.value(),
            cfg.sun_deviance.value(), cfg.sunlight_dirt.value(), 0, "");
    }
}

/*
 * =============
 * SetupSkyDome
 *
 * Setup a dome of suns for the "_sunlight2" worldspawn key.
 *
 * From q3map2
 *
 * FIXME: this is becoming a mess
 * =============
 */
static void SetupSkyDome(const settings::worldspawn_keys &cfg, vec_t upperLight, const qvec3d &upperColor,
    const int upperDirt, const vec_t upperAnglescale, const int upperStyle, const std::string &upperSuntexture,
    vec_t lowerLight, const qvec3d &lowerColor, const int lowerDirt, const vec_t lowerAnglescale, const int lowerStyle,
    const std::string &lowerSuntexture)
{
    int i, j, numSuns;
    int angleSteps, elevationSteps;
    int iterations;
    vec_t angle, elevation;
    vec_t angleStep, elevationStep;
    qvec3d direction;

    /* pick a value for 'iterations' so that 'numSuns' will be close to 'sunsamples' */
    iterations = rint(sqrt((options.sunsamples.value() - 1) / 4)) + 1;
    iterations = max(iterations, 2);

    /* dummy check */
    if (upperLight <= 0.0f && lowerLight <= 0.0f) {
        return;
    }

    /* setup */
    elevationSteps = iterations - 1;
    angleSteps = elevationSteps * 4;
    angle = 0.0f;
    elevationStep = DEG2RAD(90.0f / (elevationSteps + 1)); /* skip elevation 0 */
    angleStep = DEG2RAD(360.0f / angleSteps);

    /* calc individual sun brightness */
    numSuns = angleSteps * elevationSteps + 1;

    const vec_t sunlight2value = upperLight / numSuns;
    const vec_t sunlight3value = lowerLight / numSuns;

    /* iterate elevation */
    elevation = elevationStep * 0.5f;
    angle = 0.0f;
    for (i = 0, elevation = elevationStep * 0.5f; i < elevationSteps; i++) {
        /* iterate angle */
        for (j = 0; j < angleSteps; j++) {
            /* create sun */
            direction[0] = cos(angle) * cos(elevation);
            direction[1] = sin(angle) * cos(elevation);
            direction[2] = -sin(elevation);

            /* insert top hemisphere light */
            if (sunlight2value > 0) {
                AddSun(cfg, direction, sunlight2value, upperColor, upperDirt, upperAnglescale, upperStyle,
                    upperSuntexture);
            }

            direction[2] = -direction[2];

            /* insert bottom hemisphere light */
            if (sunlight3value > 0) {
                AddSun(cfg, direction, sunlight3value, lowerColor, lowerDirt, lowerAnglescale, lowerStyle,
                    lowerSuntexture);
            }

            /* move */
            angle += angleStep;
        }

        /* move */
        elevation += elevationStep;
        angle += angleStep / elevationSteps;
    }

    /* create vertical sun */
    if (sunlight2value > 0) {
        AddSun(
            cfg, {0.0, 0.0, -1.0}, sunlight2value, upperColor, upperDirt, upperAnglescale, upperStyle, upperSuntexture);
    }

    if (sunlight3value > 0) {
        AddSun(
            cfg, {0.0, 0.0, 1.0}, sunlight3value, lowerColor, lowerDirt, lowerAnglescale, lowerStyle, lowerSuntexture);
    }
}

static void SetupSkyDomes(const settings::worldspawn_keys &cfg)
{
    // worldspawn "legacy" skydomes
    SetupSkyDome(cfg, cfg.sunlight2.value(), cfg.sunlight2_color.value(), cfg.sunlight2_dirt.value(),
        cfg.global_anglescale.value(), 0, "", cfg.sunlight3.value(), cfg.sunlight3_color.value(),
        cfg.sunlight2_dirt.value(), cfg.global_anglescale.value(), 0, "");

    // new per-entity sunlight2/3 skydomes
    for (light_t &entity : all_lights) {
        if ((entity.sunlight2.value() || entity.sunlight3.value()) && entity.light.value() > 0) {
            if (entity.sunlight2.value()) {
                // Add the upper dome, like sunlight2 (pointing down)
                SetupSkyDome(cfg, entity.light.value(), entity.color.value(), entity.dirt.value(),
                    entity.anglescale.value(), entity.style.value(), entity.suntexture.value(), 0, {}, 0, 0, 0, "");
            } else {
                // Add the lower dome, like sunlight3 (pointing up)
                SetupSkyDome(cfg, 0, {}, 0, 0, 0, "", entity.light.value(), entity.color.value(), entity.dirt.value(),
                    entity.anglescale.value(), entity.style.value(), entity.suntexture.value());
            }

            // Disable the light itself...
            entity.light.setValue(0.0f);
        }
    }
}

/*
 * =============
 * DuplicateEntity
 * =============
 */
static light_t DuplicateEntity(const light_t &src)
{
    light_t entity{src};
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
static void JitterEntity(const light_t entity)
{
    /* jitter the light */
    for (int j = 1; j < entity.samples.value(); j++) {
        /* create a light */
        light_t &light2 = all_lights.emplace_back(DuplicateEntity(entity));
        light2.generated = true; // don't write generated light to bsp

        /* jitter it */
        qvec3d neworigin = {(entity.origin.value())[0] + (Random() * 2.0f - 1.0f) * entity.deviance.value(),
            (entity.origin.value())[1] + (Random() * 2.0f - 1.0f) * entity.deviance.value(),
            (entity.origin.value())[2] + (Random() * 2.0f - 1.0f) * entity.deviance.value()};
        light2.origin.setValue(neworigin);
    }
}

static void JitterEntities()
{
    // We will append to the list during iteration.
    const size_t starting_size = all_lights.size();
    for (size_t i = 0; i < starting_size; i++) {
        JitterEntity(all_lights.at(i));
    }
}

void Matrix4x4_CM_Projection_Inf(std::array<vec_t, 16> &proj, vec_t fovx, vec_t fovy, vec_t neard)
{
    vec_t xmin, xmax, ymin, ymax;
    constexpr vec_t nudge = 1;

    // proj
    ymax = neard * tan(fovy * Q_PI / 360.0);
    ymin = -ymax;

    if (fovx == fovy) {
        xmax = ymax;
        xmin = ymin;
    } else {
        xmax = neard * tan(fovx * Q_PI / 360.0);
        xmin = -xmax;
    }

    proj[0] = (2 * neard) / (xmax - xmin);
    proj[4] = 0;
    proj[8] = (xmax + xmin) / (xmax - xmin);
    proj[12] = 0;

    proj[1] = 0;
    proj[5] = (2 * neard) / (ymax - ymin);
    proj[9] = (ymax + ymin) / (ymax - ymin);
    proj[13] = 0;

    proj[2] = 0;
    proj[6] = 0;
    proj[10] = -1 * ((vec_t)(1 << 21) / (1 << 22));
    proj[14] = -2 * neard * nudge;

    proj[3] = 0;
    proj[7] = 0;
    proj[11] = -1;
    proj[15] = 0;
}
std::array<vec_t, 16> &Matrix4x4_CM_NewRotation(std::array<vec_t, 16> &ret, vec_t a, vec_t x, vec_t y, vec_t z)
{
    vec_t c = cos(a * Q_PI / 180.0);
    vec_t s = sin(a * Q_PI / 180.0);

    ret[0] = x * x * (1 - c) + c;
    ret[4] = x * y * (1 - c) - z * s;
    ret[8] = x * z * (1 - c) + y * s;
    ret[12] = 0;

    ret[1] = y * x * (1 - c) + z * s;
    ret[5] = y * y * (1 - c) + c;
    ret[9] = y * z * (1 - c) - x * s;
    ret[13] = 0;

    ret[2] = x * z * (1 - c) - y * s;
    ret[6] = y * z * (1 - c) + x * s;
    ret[10] = z * z * (1 - c) + c;
    ret[14] = 0;

    ret[3] = 0;
    ret[7] = 0;
    ret[11] = 0;
    ret[15] = 1;
    return ret;
}
std::array<vec_t, 16> &Matrix4x4_CM_NewTranslation(std::array<vec_t, 16> &ret, vec_t x, vec_t y, vec_t z)
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
void Matrix4_Multiply(const std::array<vec_t, 16> &a, const std::array<vec_t, 16> &b, std::array<vec_t, 16> &out)
{
    out[0] = a[0] * b[0] + a[4] * b[1] + a[8] * b[2] + a[12] * b[3];
    out[1] = a[1] * b[0] + a[5] * b[1] + a[9] * b[2] + a[13] * b[3];
    out[2] = a[2] * b[0] + a[6] * b[1] + a[10] * b[2] + a[14] * b[3];
    out[3] = a[3] * b[0] + a[7] * b[1] + a[11] * b[2] + a[15] * b[3];

    out[4] = a[0] * b[4] + a[4] * b[5] + a[8] * b[6] + a[12] * b[7];
    out[5] = a[1] * b[4] + a[5] * b[5] + a[9] * b[6] + a[13] * b[7];
    out[6] = a[2] * b[4] + a[6] * b[5] + a[10] * b[6] + a[14] * b[7];
    out[7] = a[3] * b[4] + a[7] * b[5] + a[11] * b[6] + a[15] * b[7];

    out[8] = a[0] * b[8] + a[4] * b[9] + a[8] * b[10] + a[12] * b[11];
    out[9] = a[1] * b[8] + a[5] * b[9] + a[9] * b[10] + a[13] * b[11];
    out[10] = a[2] * b[8] + a[6] * b[9] + a[10] * b[10] + a[14] * b[11];
    out[11] = a[3] * b[8] + a[7] * b[9] + a[11] * b[10] + a[15] * b[11];

    out[12] = a[0] * b[12] + a[4] * b[13] + a[8] * b[14] + a[12] * b[15];
    out[13] = a[1] * b[12] + a[5] * b[13] + a[9] * b[14] + a[13] * b[15];
    out[14] = a[2] * b[12] + a[6] * b[13] + a[10] * b[14] + a[14] * b[15];
    out[15] = a[3] * b[12] + a[7] * b[13] + a[11] * b[14] + a[15] * b[15];
}
void Matrix4x4_CM_ModelViewMatrix(std::array<vec_t, 16> &modelview, const qvec3d &viewangles, const qvec3d &vieworg)
{
    std::array<vec_t, 16> t2;
    std::array<vec_t, 16> tempmat;
    // load identity.
    modelview = {};
#if FULLYGL
    modelview[0] = 1;
    modelview[5] = 1;
    modelview[10] = 1;
    modelview[15] = 1;

    Matrix4_Multiply(modelview, Matrix4_CM_NewRotation(-90, 1, 0, 0), tempmat); // put Z going up
    Matrix4_Multiply(tempmat, Matrix4_CM_NewRotation(90, 0, 0, 1), modelview); // put Z going up
#else
    // use this lame wierd and crazy identity matrix..
    modelview[2] = -1;
    modelview[4] = -1;
    modelview[9] = 1;
    modelview[15] = 1;
#endif
    // figure out the current modelview matrix

    // I would if some of these, but then I'd still need a couple of copys
    Matrix4_Multiply(modelview, Matrix4x4_CM_NewRotation(t2, -viewangles[2], 1, 0, 0), tempmat); // roll
    Matrix4_Multiply(tempmat, Matrix4x4_CM_NewRotation(t2, viewangles[1], 0, 1, 0), modelview); // pitch
    Matrix4_Multiply(modelview, Matrix4x4_CM_NewRotation(t2, -viewangles[0], 0, 0, 1), tempmat); // yaw

    Matrix4_Multiply(
        tempmat, Matrix4x4_CM_NewTranslation(t2, -vieworg[0], -vieworg[1], -vieworg[2]), modelview); // put Z going up
}

static void Matrix4x4_CM_MakeModelViewProj(
    const qvec3d &viewangles, const qvec3d &vieworg, vec_t fovx, vec_t fovy, std::array<vec_t, 16> &modelviewproj)
{
    std::array<vec_t, 16> modelview;
    std::array<vec_t, 16> proj;

    Matrix4x4_CM_ModelViewMatrix(modelview, viewangles, vieworg);
    Matrix4x4_CM_Projection_Inf(proj, fovx, fovy, 4);
    Matrix4_Multiply(proj, modelview, modelviewproj);
}

inline vec_t CalcFov(vec_t fov_x, vec_t width, vec_t height)
{
    if (fov_x < 1 || fov_x > 179)
        FError("Unsupported fov: {}. Expected a value in [1..179] range.", fov_x);

    vec_t x = fov_x / 360 * Q_PI;
    x = tan(x);
    x = width / x;

    vec_t a = atan(height / x);

    a = a * 360 / Q_PI;

    return a;
}

static std::string ParseEscapeSequences(const std::string &input)
{
    std::string s;
    s.reserve(input.size());

    bool bold = false;

    for (size_t i = 0; i < input.length(); i++) {
        if (input.at(i) == '\\' && (i + 1) < input.length() && input.at(i + 1) == 'b') {
            bold = !bold;
            i++;
        } else {
            uint8_t c = static_cast<uint8_t>(input.at(i));
            if (bold) {
                c |= 128;
            }
            s += static_cast<char>(c);
        }
    }
    return s;
}

/*
 * ==================
 * LoadEntities
 * ==================
 */
void LoadEntities(const settings::worldspawn_keys &cfg, const mbsp_t *bsp)
{
    LogPrint("--- LoadEntities ---\n");

    entdicts = EntData_Parse(bsp->dentdata);

    // Make warnings
    for (auto &entdict : entdicts) {
        EntDict_CheckNoEmptyValues(bsp, entdict);
        EntDict_CheckTargetKeysMatched(bsp, entdict, entdicts);
        EntDict_CheckTargetnameKeyMatched(bsp, entdict, entdicts);
    }

    /* handle worldspawn */
    for (const auto &epair : WorldEnt()) {
        SetGlobalSetting(epair.first, epair.second, false);
    }
    /* apply side effects of settings (in particular "dirt") */
    FixupGlobalSettings();
    // NOTE: cfg is not valid until now.

    // First pass: make permanent changes to the bsp entdata that we will write out
    // at the end of the light process.
    for (auto &entdict : entdicts) {

        // fix "lightmap_scale"
        const std::string &lmscale = EntDict_StringForKey(entdict, "lightmap_scale");
        if (!lmscale.empty()) {
            LogPrint("lightmap_scale should be _lightmap_scale\n");

            EntDict_RemoveValueForKey(entdict, "lightmap_scale");
            entdict.set("_lightmap_scale", lmscale);
        }

        // setup light styles for switchable lights
        // NOTE: this also handles "_sun" "1" entities without any extra work.
        const std::string &classname = EntDict_StringForKey(entdict, "classname");
        if (classname.find("light") == 0) {
            const std::string &targetname = EntDict_StringForKey(entdict, "targetname");
            if (!targetname.empty()) {
                const int style = LightStyleForTargetname(cfg, targetname);
                entdict.set("style", std::to_string(style));
            }
        }

        // setup light styles for dynamic shadow entities
        if (EntDict_StringForKey(entdict, "_switchableshadow") == "1") {
            const std::string &targetname = EntDict_StringForKey(entdict, "targetname");
            // if targetname is "", generates a new unique lightstyle
            const int style = LightStyleForTargetname(cfg, targetname);
            // TODO: Configurable key?
            entdict.set("switchshadstyle", std::to_string(style));
        }

        // parse escape sequences
        for (auto &epair : entdict) {
            epair.second = ParseEscapeSequences(epair.second);
        }
    }

    Q_assert(all_lights.empty());
    if (options.nolights.value()) {
        return;
    }

    /* go through all the entities */
    for (auto &entdict : entdicts) {

        /*
         * Check light entity fields and any global settings in worldspawn.
         */
        if (EntDict_StringForKey(entdict, "classname").find("light") == 0) {
            // mxd. Convert some Arghrad3 settings...
            if (options.arghradcompat.value()) {
                EntDict_RenameKey(entdict, "_falloff", "delay"); // _falloff -> delay
                EntDict_RenameKey(entdict, "_distance", "_falloff"); // _distance -> _falloff
                EntDict_RenameKey(entdict, "_fade", "wait"); // _fade -> wait

                // _angfade or _angwait -> _anglescale
                EntDict_RenameKey(entdict, "_angfade", "_anglescale");
                EntDict_RenameKey(entdict, "_angwait", "_anglescale");
                const auto anglescale = entdict.find("_anglescale");
                if (anglescale != entdict.end()) {
                    // Convert from 0..2 to 0..1 range...
                    const vec_t val = min(1.0, max(0.0, EntDict_FloatForKey(entdict, "_anglescale") * 0.5));
                    entdict.set("_anglescale", std::to_string(val));
                }
            }

            // Skip non-switchable lights if we're skipping world lighting
            if (options.nolighting.value() && EntDict_StringForKey(entdict, "style").empty() &&
                EntDict_StringForKey(entdict, "switchshadstyle").empty()) {
                continue;
            }

            /* Allocate a new entity */
            light_t &entity = all_lights.emplace_back();

            // save pointer to the entdict
            entity.epairs = &entdict;

            // populate settings
            entity.setSettings(*entity.epairs, false);

            if (entity.mangle.isChanged()) {
                entity.spotvec = qv::vec_from_mangle(entity.mangle.value());
                entity.spotlight = true;

                if (!entity.projangle.isChanged()) {
                    // copy from mangle
                    entity.projangle.setValue(entity.mangle.value());
                }
            }

            if (!entity.project_texture.value().empty()) {
                auto texname = entity.project_texture.value();
                entity.projectedmip = img::find(texname);
                if (entity.projectedmip == nullptr) {
                    LogPrint(
                        "WARNING: light has \"_project_texture\" \"{}\", but this texture was not found\n", texname);
                }

                if (!entity.projangle.isChanged()) { // mxd
                    // Copy from angles
                    qvec3d angles = EntDict_VectorForKey(entdict, "angles");
                    qvec3d mangle{angles[1], -angles[0], angles[2]}; // -pitch yaw roll -> yaw pitch roll
                    entity.projangle.setValue(mangle);

                    entity.spotlight = true;
                }
            }

            if (entity.projectedmip) {
                if (entity.projectedmip->meta.width > entity.projectedmip->meta.height)
                    Matrix4x4_CM_MakeModelViewProj(entity.projangle.value(), entity.origin.value(),
                        entity.projfov.value(),
                        CalcFov(
                            entity.projfov.value(), entity.projectedmip->meta.width, entity.projectedmip->meta.height),
                        entity.projectionmatrix);
                else
                    Matrix4x4_CM_MakeModelViewProj(entity.projangle.value(), entity.origin.value(),
                        CalcFov(
                            entity.projfov.value(), entity.projectedmip->meta.height, entity.projectedmip->meta.width),
                        entity.projfov.value(), entity.projectionmatrix);
            }

            CheckEntityFields(cfg, &entity);
        }
    }

    LogPrint("{} entities read, {} are lights.\n", entdicts.size(), all_lights.size());
}

static qvec3d FixLightOnFace(const mbsp_t *bsp, const qvec3d &point)
{
    // FIXME: Check all shadow casters
    if (!Light_PointInWorld(bsp, point)) {
        return point;
    }

    for (int i = 0; i < 6; i++) {
        qvec3d testpoint = point;

        int axis = i / 2;
        bool add = i % 2;
        testpoint[axis] += (add ? 2 : -2); // sample points are 1 unit off faces. so nudge by 2 units, so the lights are
                                           // above the sample points

        // FIXME: Check all shadow casters
        if (!Light_PointInWorld(bsp, testpoint)) {
            return testpoint;
        }
    }

    LogPrint("WARNING: couldn't nudge light in solid at {}\n", point);
    return point;
}

void FixLightsOnFaces(const mbsp_t *bsp)
{
    for (light_t &entity : all_lights) {
        if (entity.light.value() != 0) {
            entity.origin.setValue(FixLightOnFace(bsp, entity.origin.value()));
        }
    }
}

// Maps uniform random variables U and V in [0, 1] to uniformly distributed points on a sphere

// from http://mathworld.wolfram.com/SpherePointPicking.html
// eqns 6,7,8
inline qvec3d UniformPointOnSphere(vec_t u1, vec_t u2)
{
    Q_assert(u1 >= 0 && u1 <= 1);
    Q_assert(u2 >= 0 && u2 <= 1);

    const vec_t theta = u1 * 2.0 * Q_PI;
    const vec_t u = (2.0 * u2) - 1.0;

    const vec_t s = sqrt(1.0 - (u * u));

    qvec3d dir{s * cos(theta), s * sin(theta), u};

    for (auto &v : dir) {
        Q_assert(v >= -1.001);
        Q_assert(v <= 1.001);
    }

    return dir;
}

aabb3d EstimateVisibleBoundsAtPoint(const qvec3d &point)
{
    const int N = 32;
    const int N2 = N * N;

    raystream_intersection_t *rs = MakeIntersectionRayStream(N2);

    aabb3d bounds = point;

    for (int x = 0; x < N; x++) {
        for (int y = 0; y < N; y++) {
            const vec_t u1 = static_cast<vec_t>(x) / static_cast<vec_t>(N - 1);
            const vec_t u2 = static_cast<vec_t>(y) / static_cast<vec_t>(N - 1);

            rs->pushRay(0, point, UniformPointOnSphere(u1, u2), 65536.0);
        }
    }

    rs->tracePushedRaysIntersection(nullptr);

    for (int i = 0; i < N2; i++) {
        const vec_t dist = rs->getPushedRayHitDist(i);
        qvec3d dir = rs->getPushedRayDir(i);

        // get the intersection point
        qvec3d stop = point + (dir * dist);

        bounds += stop;
    }

    delete rs;

    // grow it by 25% in each direction
    return bounds.grow(bounds.size() * 0.25);

    /*
    LogPrint("light at {} {} {} has mins {} {} {} maxs {} {} {}\n",
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
}

inline void EstimateLightAABB(light_t *light)
{
    light->bounds = EstimateVisibleBoundsAtPoint(light->origin.value());
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
    if (options.novisapprox.value())
        return;

    LogPrint("--- EstimateLightVisibility ---\n");

    RunThreadsOn(0, static_cast<int>(all_lights.size()), EstimateLightAABBThread, nullptr);
}

void SetupLights(const settings::worldspawn_keys &cfg, const mbsp_t *bsp)
{
    LogPrint("SetupLights: {} initial lights\n", all_lights.size());

    // Creates more light entities, needs to be done before the rest
    MakeSurfaceLights(bsp);

    LogPrint("SetupLights: {} after surface lights\n", all_lights.size());

    JitterEntities();

    LogPrint("SetupLights: {} after jittering\n", all_lights.size());

    const size_t final_lightcount = all_lights.size();

    MatchTargets();
    SetupSpotlights(cfg);
    SetupSuns(cfg);
    SetupSkyDomes(cfg);
    FixLightsOnFaces(bsp);
    EstimateLightVisibility();

    LogPrint("Final count: {} lights, {} suns in use.\n", all_lights.size(), all_suns.size());

    Q_assert(final_lightcount == all_lights.size());
}

const char *ValueForKey(const light_t *ent, const char *key)
{
    const auto iter = ent->epairs->find(key);
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

qvec3d EntDict_VectorForKey(const entdict_t &ent, const std::string &key)
{
    const std::string &value = EntDict_StringForKey(ent, key);
    qvec3d vec{};
    sscanf(value.c_str(), "%lf %lf %lf", &vec[0], &vec[1], &vec[2]);
    return vec;
}

/*
 * ================
 * WriteEntitiesToString
 *
 * Re-write the entdata BSP lump because switchable lights need styles set.
 * ================
 */
void WriteEntitiesToString(const settings::worldspawn_keys &cfg, mbsp_t *bsp)
{
    bsp->dentdata = EntData_Write(entdicts);

    /* FIXME - why are we printing this here? */
    LogPrint("{} switchable light styles ({} max)\n", lightstyleForTargetname.size(),
        MAX_SWITCHABLE_STYLES - cfg.compilerstyle_start.value());
}

/*
 * =======================================================================
 *                            SURFACE LIGHTS
 * =======================================================================
 */

static std::vector<light_t> surfacelight_templates;

static std::ofstream surflights_dump_file;
static std::filesystem::path surflights_dump_filename;

static void SurfLights_WriteEntityToFile(light_t *entity, const qvec3d &pos)
{
    Q_assert(entity->epairs != nullptr);

    entdict_t epairs{*entity->epairs};
    EntDict_RemoveValueForKey(epairs, "_surface");
    epairs.set("origin", qv::to_string(pos));

    surflights_dump_file << EntData_Write({epairs});
}

static void CreateSurfaceLight(const qvec3d &origin, const qvec3d &normal, const light_t *surflight_template)
{
    light_t &entity = all_lights.emplace_back(DuplicateEntity(*surflight_template));

    entity.origin.setValue(origin);

    /* don't write to bsp */
    entity.generated = true;

    /* set spotlight vector based on face normal */
    if (atoi(ValueForKey(surflight_template, "_surface_spotlight"))) {
        entity.spotlight = true;
        entity.spotvec = normal;
    }

    /* export it to a map file for debugging */
    if (options.surflight_dump.value()) {
        SurfLights_WriteEntityToFile(&entity, origin);
    }
}

static void CreateSurfaceLightOnFaceSubdivision(const mface_t *face, const modelinfo_t *face_modelinfo,
    const light_t *surflight_template, const mbsp_t *bsp, int numverts, const qvec3d *verts)
{
    qvec3d midpoint = qv::PolyCentroid(verts, verts + numverts);
    qplane3d plane = bsp->dplanes[face->planenum];

    /* Nudge 2 units (by default) along face normal */
    if (face->side) {
        plane = -plane;
    }

    vec_t offset = atof(ValueForKey(surflight_template, "_surface_offset"));
    if (offset == 0)
        offset = 2.0;

    midpoint += plane.normal * offset;

    /* Add the model offset */
    midpoint += face_modelinfo->offset;

    CreateSurfaceLight(midpoint, plane.normal, surflight_template);
}

static aabb3d BoundPoly(int numverts, qvec3d *verts)
{
    aabb3d bounds;

    for (auto v = verts; v < verts + numverts; v++) {
        bounds += *v;
    }

    return bounds;
}

static bool FaceMatchesSurfaceLightTemplate(const mbsp_t *bsp, const mface_t *face, const light_t &surflight)
{
    const char *texname = Face_TextureName(bsp, face);
    return !Q_strcasecmp(texname, ValueForKey(&surflight, "_surface"));
}

/*
 ================
 SubdividePolygon - from GLQuake
 ================
 */
static void SubdividePolygon(const mface_t *face, const modelinfo_t *face_modelinfo, const mbsp_t *bsp, int numverts,
    qvec3d *verts, vec_t subdivide_size)
{
    int i, j;
    vec_t m;
    qvec3d front[64], back[64];
    int f, b;
    vec_t dist[64];
    vec_t frac;
    // glpoly_t        *poly;
    // float   s, t;

    if (numverts > 64)
        FError("numverts = {}", numverts);

    aabb3d bounds = BoundPoly(numverts, verts);

    for (i = 0; i < 3; i++) {
        m = (bounds.mins()[i] + bounds.maxs()[i]) * 0.5;
        m = subdivide_size * floor(m / subdivide_size + 0.5);
        if (bounds.maxs()[i] - m < 8)
            continue;
        if (m - bounds.mins()[i] < 8)
            continue;

        // cut it
        {
            vec_t *v = &verts->at(i);
            for (j = 0; j < numverts; j++, v += 3)
                dist[j] = *v - m;

            // wrap cases
            dist[j] = dist[0];
            v -= i;
            v[0] = (*verts)[0];
            v[1] = (*verts)[1];
            v[2] = (*verts)[2];
        }

        f = b = 0;
        qvec3d *v = verts;
        for (j = 0; j < numverts; j++, v++) {
            if (dist[j] >= 0) {
                front[f] = *v;
                f++;
            }
            if (dist[j] <= 0) {
                back[b] = *v;
                b++;
            }
            if (dist[j] == 0 || dist[j + 1] == 0)
                continue;
            if ((dist[j] > 0) != (dist[j + 1] > 0)) {
                // clip point
                frac = dist[j] / (dist[j] - dist[j + 1]);
                front[f] = back[b] = *v + ((*(v + 1) - *v) * frac);
                f++;
                b++;
            }
        }

        SubdividePolygon(face, face_modelinfo, bsp, f, front, subdivide_size);
        SubdividePolygon(face, face_modelinfo, bsp, b, back, subdivide_size);
        return;
    }

    for (const auto &surflight : surfacelight_templates) {
        if (FaceMatchesSurfaceLightTemplate(bsp, face, surflight)) {
            CreateSurfaceLightOnFaceSubdivision(face, face_modelinfo, &surflight, bsp, numverts, verts);
        }
    }
}

/*
 ================
 GL_SubdivideSurface - from GLQuake
 ================
 */
static void GL_SubdivideSurface(const mface_t *face, const modelinfo_t *face_modelinfo, const mbsp_t *bsp)
{
    int i;
    // TODO: is numedges ever > 64? should we use a winding_t here for
    // simplicity?
    qvec3d verts[64];

    for (i = 0; i < face->numedges; i++) {
        int edgenum = bsp->dsurfedges[face->firstedge + i];
        if (edgenum >= 0) {
            verts[i] = bsp->dvertexes[bsp->dedges[edgenum][0]];
        } else {
            verts[i] = bsp->dvertexes[bsp->dedges[-edgenum][1]];
        }
    }

    SubdividePolygon(face, face_modelinfo, bsp, face->numedges, verts, options.surflight_subdivide.value());
}

bool ParseLightsFile(const std::filesystem::path &fname)
{
    // note: this creates dupes. super bright light! (and super slow, too)
    std::string buf;
    std::ifstream f(fname);

    if (!f)
        return false;

    while (!f.eof()) {
        std::getline(f, buf);

        parser_t parser(buf);

        if (!parser.parse_token())
            continue;

        entdict_t &d = radlights.emplace_back();
        d.set("_surface", parser.token);
        parser.parse_token();
        vec_t r = std::stod(parser.token);
        parser.parse_token();
        vec_t g = std::stod(parser.token);
        parser.parse_token();
        vec_t b = std::stod(parser.token);
        d.set("_color", fmt::format("{} {} {}", r, g, b));
        parser.parse_token();
        d.set("light", parser.token);
        // might be hdr rgbi values here
    }

    return true;
}

static void MakeSurfaceLights(const mbsp_t *bsp)
{
    LogPrint("--- MakeSurfaceLights ---\n");

    Q_assert(surfacelight_templates.empty());

    for (entdict_t &l : radlights) {
        light_t &entity = surfacelight_templates.emplace_back();
        entity.epairs = &l;
        entity.setSettings(*entity.epairs, false);
    }

    for (light_t &entity : all_lights) {
        std::string tex = ValueForKey(&entity, "_surface");
        if (!tex.empty()) {
            surfacelight_templates.push_back(entity); // makes a copy

            // Hack: clear templates light value to 0 so they don't cast light
            entity.light.setValue(0);

            LogPrint("Creating surface lights for texture \"{}\" from template at ({})\n", tex,
                ValueForKey(&entity, "origin"));
        }
    }

    if (surfacelight_templates.empty())
        return;

    if (options.surflight_dump.value()) {
        surflights_dump_filename = options.sourceMap;
        surflights_dump_filename.replace_filename(surflights_dump_filename.stem().string() + "-surflights")
            .replace_extension("map");
        surflights_dump_file.open(surflights_dump_filename);
    }

    /* Create the surface lights */
    std::vector<bool> face_visited(bsp->dfaces.size(), false);

    for (auto &leaf : bsp->dleafs) {
        const bool underwater =
            ((bsp->loadversion->game->id == GAME_QUAKE_II) ? (leaf.contents & Q2_CONTENTS_LIQUID)
                                                           : leaf.contents != CONTENTS_EMPTY); // mxd

        for (int k = 0; k < leaf.nummarksurfaces; k++) {
            const int facenum = bsp->dleaffaces[leaf.firstmarksurface + k];
            const mface_t *surf = BSP_GetFace(bsp, facenum);
            const modelinfo_t *face_modelinfo = ModelInfoForFace(bsp, facenum);

            /* Skip face with no modelinfo */
            if (face_modelinfo == nullptr)
                continue;

            /* Ignore the underwater side of liquid surfaces */
            if (underwater && Face_IsTranslucent(bsp, surf)) // mxd
                continue;

            /* Skip if already handled */
            if (face_visited.at(facenum))
                continue;

            /* Mark as handled */
            face_visited.at(facenum) = true;

            /* Don't bother subdividing if it doesn't match any surface light templates */
            if (!std::any_of(surfacelight_templates.begin(), surfacelight_templates.end(),
                    [&](const auto &surflight) { return FaceMatchesSurfaceLightTemplate(bsp, surf, surflight); }))
                continue;

            /* Generate the lights */
            GL_SubdivideSurface(surf, face_modelinfo, bsp);
        }
    }

    if (surflights_dump_file) {
        surflights_dump_file.close();
        fmt::print("wrote surface lights to '{}'\n", surflights_dump_filename);
    }
}
