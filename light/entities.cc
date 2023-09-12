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

#include <light/entities.hh>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <common/imglib.hh> // for img::find
#include <common/log.hh>
#include <common/cmdlib.hh>
#include <common/parser.hh>

#include <light/litfile.hh>
#include <light/trace.hh>
#include <light/trace_embree.hh>
#include <light/light.hh>
#include <common/bsputils.hh>
#include <common/parallel.hh>

static std::vector<std::unique_ptr<light_t>> all_lights;
static std::vector<sun_t> all_suns;
static std::vector<entdict_t> entdicts;
static std::vector<entdict_t> radlights;
static std::vector<std::pair<std::string, int>> lightstyleForTargetname;
static std::vector<std::unique_ptr<light_t>> surfacelight_templates;
static std::ofstream surflights_dump_file;
static fs::path surflights_dump_filename;

/**
 * Resets global data in this file
 */
void ResetLightEntities()
{
    all_lights.clear();
    all_suns.clear();
    entdicts.clear();
    radlights.clear();

    lightstyleForTargetname.clear();

    surfacelight_templates.clear();
    surflights_dump_file = {};
    surflights_dump_filename.clear();
}

std::vector<std::unique_ptr<light_t>> &GetLights()
{
    return all_lights;
}

const std::vector<entdict_t> &GetEntdicts()
{
    return entdicts;
}

std::vector<sun_t> &GetSuns()
{
    return all_suns;
}

std::vector<entdict_t> &GetRadLights()
{
    return radlights;
}

/* surface lights */
static void MakeSurfaceLights(const mbsp_t *bsp);

// light_t
light_t::light_t()
    : light{this, "light", DEFAULTLIGHTLEVEL},
      atten{this, "wait", 1.0, 0.0, std::numeric_limits<vec_t>::max()},
      formula{this, "delay", LF_LINEAR,
          {{"linear", LF_LINEAR}, {"inverse", LF_INVERSE}, {"inverse2", LF_INVERSE2}, {"infinite", LF_INFINITE},
              {"localmin", LF_LOCALMIN}, {"inverse2a", LF_INVERSE2A}}},
      cone{this, "cone", 10.f},
      spotangle{this, "angle", 40.0},
      spotangle2{this, "softangle", 0.0},
      style{this, "style", 0, 0, INVALID_LIGHTSTYLE - 1},
      anglescale{this, {"anglesense", "anglescale"}, -1.0},
      dirtscale{this, "dirtscale", 0.0},
      dirtgain{this, "dirtgain", 0},
      dirt{this, "dirt", 0},
      deviance{this, "deviance", 0},
      samples{this, "samples", 16, 0, std::numeric_limits<int32_t>::max()},
      projfov{this, "project_fov", 90},
      bouncescale{this, "bouncescale", 1.0},
      dirt_off_radius{this, "dirt_off_radius", 0.0},
      dirt_on_radius{this, "dirt_on_radius", 0.0},
      sun{this, "sun", false},
      sunlight2{this, "sunlight2", 0},
      sunlight3{this, "sunlight3", 0},
      falloff{this, "falloff", 0.0, 0.0, std::numeric_limits<vec_t>::max()},
      bleed{this, "bleed", false},
      origin{this, "origin", 0, 0, 0},
      color{this, "color", 255.0, 255.0, 255.0},
      mangle{this, "mangle", 0, 0, 0},
      projangle{this, "project_mangle", 20, 0, 0},
      project_texture{this, "project_texture", ""},
      suntexture{this, "suntexture", ""},
      nostaticlight{this, "nostaticlight", false},
      surflight_group{this, "surflight_group", 0},
      surflight_minlight_scale{this, "surflight_minlight_scale", 1.f},
      light_channel_mask{this, "light_channel_mask", CHANNEL_MASK_DEFAULT},
      shadow_channel_mask{this, "shadow_channel_mask", CHANNEL_MASK_DEFAULT},
      nonudge{this, "nonudge", false}
{
}

std::string light_t::classname() const
{
    return epairs->get("classname");
}

const light_formula_t &light_t::getFormula() const
{
    return formula.value();
}

void light_t::initAABB()
{
    bounds = origin.value();
}

void light_t::expandAABB(const qvec3d &pt)
{
    bounds += pt;
}

/*
 * ============================================================================
 * ENTITY FILE PARSING
 * If a light has a targetname, generate a unique style in the 32-63 range
 * ============================================================================
 */

entdict_t &WorldEnt()
{
    if (entdicts.size() == 0 || entdicts.at(0).get("classname") != "worldspawn") {
        Error("WorldEnt() failed to get worldspawn");
    }
    return entdicts.at(0);
}

/**
 * Assigns a lightstyle number for the given non-empty targetname string
 * Reuses the existing lightstyle if this targetname was already assigned.
 *
 * Pass an empty string to generate a new unique lightstyle.
 */
int LightStyleForTargetname(const settings::worldspawn_keys &cfg, const std::string &targetname)
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
    if (newStylenum >= cfg.compilerstyle_max.value()) {
        FError("Too many unique light targetnames (max={})\n", cfg.compilerstyle_max.value());
    }

    lightstyleForTargetname.emplace_back(targetname, newStylenum);

    logging::print(logging::flag::VERBOSE, "Allocated lightstyle {} for targetname '{}'\n", newStylenum, targetname);

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
    for (auto &entity : all_lights) {
        const std::string &targetstr = entity->epairs->get("target");
        if (targetstr.empty()) {
            continue;
        }

        for (const entdict_t &target : entdicts) {
            if (string_iequals(targetstr, target.get("targetname"))) {
                entity->targetent = &target;
                break;
            }
        }
    }
}

static std::string EntDict_PrettyDescription(const mbsp_t *bsp, const entdict_t &entity)
{
    // get the submodel's bbox if it's a brush entity
    if (bsp != nullptr && entity.get("origin") == "" && entity.get("model") != "") {
        const std::string &submodel_str = entity.get("model");
        const dmodelh2_t *info = BSP_DModelForModelString(bsp, submodel_str);

        if (info) {
            return fmt::format(
                "brush entity with mins [{}] maxs [{}] ({})", info->mins, info->maxs, entity.get("classname"));
        }
    }

    return fmt::format("entity at ({}) ({})", entity.get("origin"), entity.get("classname"));
}

bool EntDict_CheckNoEmptyValues(const mbsp_t *bsp, const entdict_t &entdict)
{
    bool ok = true;
    // empty values warning
    for (const auto &keyval : entdict) {
        if (keyval.first.empty() || keyval.second.empty()) {
            logging::print("WARNING: {} has empty key/value \"{}\" \"{}\"\n", EntDict_PrettyDescription(bsp, entdict),
                keyval.first, keyval.second);
            ok = false;
        }
    }
    return ok;
}

static void SetupSpotlights(const mbsp_t *bsp, const settings::worldspawn_keys &cfg)
{
    for (auto &entity : all_lights) {
        vec_t targetdist = 0.0; // mxd
        if (entity->targetent) {
            qvec3d targetOrigin;
            entity->targetent->get_vector("origin", targetOrigin);
            entity->spotvec = targetOrigin - entity->origin.value();
            targetdist = qv::normalizeInPlace(entity->spotvec); // mxd
            entity->spotlight = true;
        }
        if (entity->spotlight) {
            vec_t base_angle = 0.0; // spotlight cone "diameter" in degrees

            if (entity->cone.is_changed()) {
                // q2 style: "_cone" key specifies cone radius in degrees
                base_angle = entity->cone.value() * 2.f;
            } else if (entity->spotangle.is_changed()) {
                // q1 style: "angle" key specifies cone diameter in degrees
                base_angle = entity->spotangle.value();
            }

            if (!base_angle) {
                // if we don't have a valid cone angle, the default depends on the target game
                if (bsp->loadversion->game->id == GAME_QUAKE_II) {
                    base_angle = entity->cone.default_value() * 2.f;
                } else {
                    base_angle = entity->spotangle.default_value();
                }
            }

            entity->spotfalloff = -cos(base_angle / 2 * Q_PI / 180);

            vec_t angle2 = entity->spotangle2.value();
            if (angle2 <= 0 || angle2 > base_angle)
                angle2 = base_angle;
            entity->spotfalloff2 = -cos(angle2 / 2 * Q_PI / 180);

            // mxd. Apply autofalloff?
            if (targetdist > 0.0f && entity->falloff.value() == 0 && cfg.spotlightautofalloff.value()) {
                const vec_t coneradius = targetdist * tan(base_angle / 2 * Q_PI / 180);
                entity->falloff.set_value(targetdist + coneradius, settings::source::MAP);
            }
        }
    }
}

static void CheckEntityFields(const mbsp_t *bsp, const settings::worldspawn_keys &cfg, light_t *entity)
{
    if (!entity->light.is_changed())
        entity->light.set_value(DEFAULTLIGHTLEVEL, settings::source::MAP);

    if (entity->atten.value() <= 0.0)
        entity->atten.set_value(1.0, settings::source::MAP);
    if (entity->anglescale.value() < 0 || entity->anglescale.value() > 1.0)
        entity->anglescale.set_value(cfg.global_anglescale.value(), settings::source::MAP);

    // mxd. Warn about unsupported _falloff / delay combos...
    if (entity->falloff.value() > 0.0f && entity->getFormula() != LF_LINEAR) {
        logging::print("WARNING: _falloff is currently only supported on linear (delay 0) lights\n"
                       "   {} at [{}]\n",
            entity->classname(), entity->origin.value());
        entity->falloff.set_value(0.0f, settings::source::MAP);
    }

    /* set up deviance and samples defaults */
    if (entity->deviance.value() > 0 && entity->samples.value() == 0) {
        entity->samples.set_value(16, settings::source::MAP);
    }

    if (entity->deviance.value() <= 0.0f || entity->samples.value() <= 1) {
        entity->deviance.set_value(0.0f, settings::source::MAP);
        entity->samples.set_value(1, settings::source::MAP);
    }

    /* For most formulas, we need to divide the light value by the number of
       samples (jittering) to keep the brightness approximately the same. */
    if (entity->getFormula() == LF_INVERSE || entity->getFormula() == LF_INVERSE2 ||
        entity->getFormula() == LF_INFINITE || (entity->getFormula() == LF_LOCALMIN && cfg.addminlight.value()) ||
        entity->getFormula() == LF_INVERSE2A) {
        entity->light.set_value(entity->light.value() / entity->samples.value(), settings::source::MAP);
    }

    // shadow_channel_mask defaults to light_channel_mask
    if (!entity->shadow_channel_mask.is_changed()) {
        entity->shadow_channel_mask.set_value(entity->light_channel_mask.value(), settings::source::DEFAULT);
    }

    if (!entity->surflight_minlight_scale.is_changed()) {
        if (cfg.surflight_minlight_scale.is_changed()) {
            entity->surflight_minlight_scale.set_value(cfg.surflight_minlight_scale.value(), settings::source::DEFAULT);
        } else if (bsp->loadversion->game->id == GAME_QUAKE_II) {
            // this default value mimicks the fullbright-ish nature of emissive surfaces
            // in Q2.
            entity->surflight_minlight_scale.set_value(64.0f, settings::source::DEFAULT);
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
static bool Dirt_ResolveFlag(const settings::worldspawn_keys &cfg, int dirtInt)
{
    if (dirtInt == 1)
        return true;
    else if (dirtInt == -1)
        return false;
    else
        return cfg.dirt.value();
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
    if (!suntexture.empty())
        sun.suntexture_value = img::find(suntexture);
    else
        sun.suntexture_value = nullptr;
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
    int sun_num_samples = (sun_deviance == 0 ? 1 : light_options.sunsamples.value()); // mxd
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
    for (auto &entity : all_lights) {
        // mxd. Arghrad-style sun setup
        if (entity->sun.value() && entity->light.value() > 0) {
            // Set sun vector
            qvec3d sunvec;
            if (entity->targetent) {
                qvec3d target_pos;
                entity->targetent->get_vector("origin", target_pos);
                sunvec = target_pos - entity->origin.value();
            } else if (qv::length2(entity->mangle.value()) > 0) {
                sunvec = entity->mangle.value();
            } else { // Use { 0, 0, 0 } as sun target...
                logging::print("WARNING: sun missing target, entity origin used.\n");
                sunvec = -entity->origin.value();
            }

            // Add the sun
            SetupSun(cfg, entity->light.value(), entity->color.value(), sunvec, entity->anglescale.value(),
                entity->deviance.value(), entity->dirt.value(), entity->style.value(), entity->suntexture.value());

            // Disable the light itself...
            entity->light.set_value(0.0f, settings::source::MAP);
        }
    }

    SetupSun(cfg, cfg.sunlight.value(), cfg.sunlight_color.value(), cfg.sunvec.value(), cfg.global_anglescale.value(),
        cfg.sun_deviance.value(), cfg.sunlight_dirt.value(), 0, "");

    if (cfg.sun2.value() != 0) {
        logging::print("creating sun2\n");
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
    iterations = rint(sqrt((light_options.sunsamples.value() - 1) / 4)) + 1;
    iterations = std::max(iterations, 2);

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
    for (auto &entity : all_lights) {
        if ((entity->sunlight2.value() || entity->sunlight3.value()) && entity->light.value() > 0) {
            if (entity->sunlight2.value()) {
                // Add the upper dome, like sunlight2 (pointing down)
                SetupSkyDome(cfg, entity->light.value(), entity->color.value(), entity->dirt.value(),
                    entity->anglescale.value(), entity->style.value(), entity->suntexture.value(), 0, {}, 0, 0, 0, "");
            } else {
                // Add the lower dome, like sunlight3 (pointing up)
                SetupSkyDome(cfg, 0, {}, 0, 0, 0, "", entity->light.value(), entity->color.value(),
                    entity->dirt.value(), entity->anglescale.value(), entity->style.value(),
                    entity->suntexture.value());
            }

            // Disable the light itself...
            entity->light.set_value(0.0f, settings::source::MAP);
        }
    }
}

/*
 * =============
 * DuplicateEntity
 * =============
 */
static std::unique_ptr<light_t> DuplicateEntity(const light_t &src)
{
    std::unique_ptr<light_t> entity = std::make_unique<light_t>();

    // copy settings::setting_container members
    entity->copy_from(src);

    // copy other members
    entity->spotlight = src.spotlight;
    entity->spotvec = src.spotvec;
    entity->spotfalloff = src.spotfalloff;
    entity->spotfalloff2 = src.spotfalloff2;
    entity->projectedmip = src.projectedmip;
    entity->projectionmatrix = src.projectionmatrix;
    entity->epairs = src.epairs;
    entity->targetent = src.targetent;
    entity->generated = src.generated;
    entity->bounds = src.bounds;

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
static void JitterEntity(const light_t &entity)
{
    // don't jitter suns
    if (entity.sun.value()) {
        return;
    }

    std::vector<std::unique_ptr<light_t>> new_lights;

    /* jitter the light */
    for (int j = 1; j < entity.samples.value(); j++) {
        /* create a light */
        auto &light2 = new_lights.emplace_back(DuplicateEntity(entity));
        light2->generated = true; // don't write generated light to bsp

        /* jitter it */
        qvec3d neworigin = {(entity.origin.value())[0] + (Random() * 2.0f - 1.0f) * entity.deviance.value(),
            (entity.origin.value())[1] + (Random() * 2.0f - 1.0f) * entity.deviance.value(),
            (entity.origin.value())[2] + (Random() * 2.0f - 1.0f) * entity.deviance.value()};
        light2->origin.set_value(neworigin, settings::source::MAP);
    }

    // move the new lights into all_lights
    // (don't modify the all_lights vector in the loop above, because it could invalidate the passed in `entity`
    // reference)
    for (auto &new_light : new_lights) {
        all_lights.push_back(std::move(new_light));
    }
}

static void JitterEntities()
{
    // We will append to the list during iteration.
    const size_t starting_size = all_lights.size();
    for (size_t i = 0; i < starting_size; i++) {
        JitterEntity(*all_lights.at(i));
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
    proj[10] = -1 * 0.5;
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
    logging::funcheader();

    entdicts = EntData_Parse(*bsp);

    // Make warnings
    for (auto &entdict : entdicts) {
        EntDict_CheckNoEmptyValues(bsp, entdict);
    }

    /* handle worldspawn */
    for (const auto &epair : WorldEnt()) {
        if (light_options.set_setting(epair.first, epair.second, settings::source::MAP) ==
            settings::setting_error::INVALID) {
            logging::print("WARNING: worldspawn key {} has invalid value of \"{}\"\n", epair.first, epair.second);
        }
    }

    /* apply side effects of settings (in particular "dirt") */
    FixupGlobalSettings();
    // NOTE: cfg is not valid until now.

    // First pass: make permanent changes to the bsp entdata that we will write out
    // at the end of the light process.
    for (auto &entdict : entdicts) {

        // fix "lightmap_scale"
        const std::string &lmscale = entdict.get("lightmap_scale");
        if (!lmscale.empty()) {
            // FIXME: line number
            logging::print("WARNING: lightmap_scale should be _lightmap_scale\n");

            entdict.remove("lightmap_scale");
            entdict.set("_lightmap_scale", lmscale);
        }

        // setup light styles for switchable lights
        // NOTE: this also handles "_sun" "1" entities without any extra work.
        const std::string &classname = entdict.get("classname");
        if (classname.find("light") == 0) {
            const std::string &targetname = entdict.get("targetname");
            if (!targetname.empty()) {
                const int style = LightStyleForTargetname(cfg, targetname);
                entdict.set("style", std::to_string(style));
            }
        }

        // setup light styles for dynamic shadow entities
        if (entdict.get_int("_switchableshadow") == 1) {
            const std::string &targetname = entdict.get("targetname");
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
    if (light_options.nolights.value()) {
        return;
    }

    /* go through all the entities */
    for (auto &entdict : entdicts) {

        /*
         * Check light entity fields and any global settings in worldspawn.
         */
        if (entdict.get("classname").find("light") == 0) {
            // mxd. Convert some Arghrad3 settings...
            if (light_options.arghradcompat.value()) {
                entdict.rename("_falloff", "delay"); // _falloff -> delay
                entdict.rename("_distance", "_falloff"); // _distance -> _falloff
                entdict.rename("_fade", "wait"); // _fade -> wait

                // _angfade or _angwait -> _anglescale
                entdict.rename("_angfade", "_anglescale");
                entdict.rename("_angwait", "_anglescale");
                const auto anglescale = entdict.find("_anglescale");
                if (anglescale != entdict.end()) {
                    // Convert from 0..2 to 0..1 range...
                    const vec_t val = std::min(1.0, std::max(0.0, entdict.get_float("_anglescale") * 0.5));
                    entdict.set("_anglescale", std::to_string(val));
                }
            }

            // Skip non-switchable lights if we're skipping world lighting
            if (light_options.nolighting.value() && entdict.get("style").empty() &&
                entdict.get("switchshadstyle").empty()) {
                continue;
            }

            /* Allocate a new entity */
            auto &entity = all_lights.emplace_back(std::make_unique<light_t>());

            // save pointer to the entdict
            entity->epairs = &entdict;

            // populate settings
            entity->set_settings(*entity->epairs, settings::source::MAP);

            if (entity->mangle.is_changed()) {
                entity->spotvec = qv::vec_from_mangle(entity->mangle.value());
                entity->spotlight = true;

                if (!entity->projangle.is_changed()) {
                    // copy from mangle
                    entity->projangle.set_value(entity->mangle.value(), settings::source::MAP);
                }
            }

            if (!entity->project_texture.value().empty()) {
                auto texname = entity->project_texture.value();
                entity->projectedmip = img::find(texname);
                if (entity->projectedmip == nullptr ||
                    entity->projectedmip->pixels.empty()) {
                    logging::print(
                        "WARNING: light has \"_project_texture\" \"{}\", but this texture was not found\n", texname);
                    entity->projectedmip = nullptr;
                }

                if (!entity->projangle.is_changed()) { // mxd
                    // Copy from angles
                    qvec3d angles;
                    entdict.get_vector("angles", angles);
                    qvec3d mangle{angles[1], -angles[0], angles[2]}; // -pitch yaw roll -> yaw pitch roll
                    entity->projangle.set_value(mangle, settings::source::MAP);

                    entity->spotlight = true;
                }
            }

            if (entity->projectedmip) {
                if (entity->projectedmip->meta.width > entity->projectedmip->meta.height)
                    Matrix4x4_CM_MakeModelViewProj(entity->projangle.value(), entity->origin.value(),
                        entity->projfov.value(),
                        CalcFov(entity->projfov.value(), entity->projectedmip->meta.width,
                            entity->projectedmip->meta.height),
                        entity->projectionmatrix);
                else
                    Matrix4x4_CM_MakeModelViewProj(entity->projangle.value(), entity->origin.value(),
                        CalcFov(entity->projfov.value(), entity->projectedmip->meta.height,
                            entity->projectedmip->meta.width),
                        entity->projfov.value(), entity->projectionmatrix);
            }

            CheckEntityFields(bsp, cfg, entity.get());
        }
    }

    logging::print("{} entities read, {} are lights.\n", entdicts.size(), all_lights.size());
}

std::tuple<qvec3d, bool> FixLightOnFace(const mbsp_t *bsp, const qvec3d &point, bool warn, float max_dist)
{
    // FIXME: Check all shadow casters
    if (!Light_PointInWorld(bsp, point)) {
        return {point, true};
    }

    for (int i = 0; i < 6; i++) {
        qvec3d testpoint = point;

        int axis = i / 2;
        bool add = i % 2;
        // sample points are 1 unit off faces. so nudge by 2 units, so the lights are
        // above the sample points
        testpoint[axis] += (add ? max_dist : -max_dist);

        // FIXME: Check all shadow casters
        if (!Light_PointInWorld(bsp, testpoint)) {
            return {testpoint, true};
        }
    }

    if (warn)
        logging::print("WARNING: couldn't nudge light out of solid at {}\n", point);
    return {point, false};
}

void FixLightsOnFaces(const mbsp_t *bsp)
{
    for (auto &entity : all_lights) {
        if (entity->light.value() != 0 && !entity->nonudge.value() &&
            entity->light_channel_mask.value() == CHANNEL_MASK_DEFAULT &&
            entity->shadow_channel_mask.value() == CHANNEL_MASK_DEFAULT) {
            auto [fixed_pos, success] = FixLightOnFace(bsp, entity->origin.value());
            entity->origin.set_value(fixed_pos, settings::source::MAP);
        }
    }
}

static void SetupLightLeafnums(const mbsp_t *bsp)
{
    for (auto &entity : all_lights) {
        entity->leaf = Light_PointInLeaf(bsp, entity->origin.value());
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
    constexpr size_t N = 32;
    constexpr size_t N2 = N * N;

    raystream_intersection_t rs{N2};

    aabb3d bounds = point;

    for (size_t x = 0; x < N; x++) {
        for (size_t y = 0; y < N; y++) {
            const vec_t u1 = static_cast<vec_t>(x) / static_cast<vec_t>(N - 1);
            const vec_t u2 = static_cast<vec_t>(y) / static_cast<vec_t>(N - 1);

            rs.pushRay(0, point, UniformPointOnSphere(u1, u2), 65536.0);
        }
    }

    rs.tracePushedRaysIntersection(nullptr, CHANNEL_MASK_DEFAULT);

    for (int i = 0; i < N2; i++) {
        const vec_t &dist = rs.getPushedRayHitDist(i);
        const qvec3d &dir = rs.getPushedRayDir(i);

        // get the intersection point
        qvec3d stop = point + (dir * dist);

        bounds += stop;
    }

    // grow it by 25% in each direction
    return bounds.grow(bounds.size() * 0.25);

    /*
    logging::print("light at {} {} {} has mins {} {} {} maxs {} {} {}\n",
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

inline void EstimateLightAABB(const std::unique_ptr<light_t> &light)
{
    light->bounds = EstimateVisibleBoundsAtPoint(light->origin.value());
}

void EstimateLightVisibility(void)
{
    logging::funcheader();

    logging::parallel_for_each(all_lights, EstimateLightAABB);
}

void SetupLights(const settings::worldspawn_keys &cfg, const mbsp_t *bsp)
{
    logging::print("SetupLights: {} initial lights\n", all_lights.size());

    // Creates more light entities, needs to be done before the rest
    MakeSurfaceLights(bsp);

    logging::print("SetupLights: {} after surface lights\n", all_lights.size());

    JitterEntities();

    logging::print("SetupLights: {} after jittering\n", all_lights.size());

    const size_t final_lightcount = all_lights.size();

    MatchTargets();
    SetupSpotlights(bsp, cfg);
    SetupSuns(cfg);
    SetupSkyDomes(cfg);
    FixLightsOnFaces(bsp);
    if (light_options.visapprox.value() == visapprox_t::RAYS) {
        EstimateLightVisibility();
    } else if (light_options.visapprox.value() == visapprox_t::VIS) {
        SetupLightLeafnums(bsp);
    }

    logging::print("Final count: {} lights, {} suns in use.\n", all_lights.size(), all_suns.size());

    Q_assert(final_lightcount == all_lights.size());
}

const entdict_t *FindEntDictWithKeyPair(const std::string &key, const std::string &value)
{
    for (const auto &entdict : entdicts) {
        if (entdict.get(key) == value) {
            return &entdict;
        }
    }
    return nullptr;
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
    logging::print("{} switchable light styles ({} max)\n", lightstyleForTargetname.size(),
        cfg.compilerstyle_max.value() - cfg.compilerstyle_start.value());
}

/*
 * =======================================================================
 *                            SURFACE LIGHTS
 * =======================================================================
 */

const std::vector<std::unique_ptr<light_t>> &GetSurfaceLightTemplates()
{
    return surfacelight_templates;
}

static void SurfLights_WriteEntityToFile(light_t *entity, const qvec3d &pos)
{
    Q_assert(entity->epairs != nullptr);

    entdict_t epairs{*entity->epairs};
    epairs.remove("_surface");
    epairs.set("origin", qv::to_string(pos));

    surflights_dump_file << EntData_Write({epairs});
}

static void CreateSurfaceLight(const qvec3d &origin, const qvec3d &normal, const light_t *surflight_template)
{
    auto &entity = all_lights.emplace_back(DuplicateEntity(*surflight_template));

    entity->origin.set_value(origin, settings::source::MAP);

    /* don't write to bsp */
    entity->generated = true;

    /* set spotlight vector based on face normal */
    if (surflight_template->epairs->get_int("_surface_spotlight")) {
        entity->spotlight = true;
        entity->spotvec = normal;
    }

    /* export it to a map file for debugging */
    if (light_options.surflight_dump.value()) {
        SurfLights_WriteEntityToFile(entity.get(), origin);
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

    vec_t offset = surflight_template->epairs->get_float("_surface_offset");
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

bool FaceMatchesSurfaceLightTemplate(
    const mbsp_t *bsp, const mface_t *face, const modelinfo_t *face_modelinfo, const light_t &surflight, int surf_type)
{
    const char *texname = Face_TextureName(bsp, face);

    int32_t radiosity_type;

    if (surflight.epairs->has("_surface_radiosity")) {
        radiosity_type = surflight.epairs->get_int("_surface_radiosity");
    } else {
        radiosity_type = light_options.surflight_radiosity.value();
    }

    if (radiosity_type != surf_type) {
        return false;
    }

    const surfflags_t &extended_flags = extended_texinfo_flags[face->texinfo];

    if (extended_flags.surflight_group) {
        if (surflight.surflight_group.value() && surflight.surflight_group.value() != extended_flags.surflight_group) {
            return false;
        }
    }

    return !Q_strcasecmp(texname, surflight.epairs->get("_surface"));
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
        if (FaceMatchesSurfaceLightTemplate(bsp, face, face_modelinfo, *surflight, SURFLIGHT_Q1)) {
            CreateSurfaceLightOnFaceSubdivision(face, face_modelinfo, surflight.get(), bsp, numverts, verts);
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

    SubdividePolygon(face, face_modelinfo, bsp, face->numedges, verts, light_options.surflight_subdivide.value());
}

static bool ParseEntityLights(std::ifstream &f, const fs::path &fname)
{
    std::string str{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
    parser_t p(str, {fname.string()});
    EntData_ParseInto(p, radlights);
    return true;
}

bool ParseLightsFile(const fs::path &fname)
{
    std::ifstream f(fname);

    if (!f)
        return false;

    // use entity-style format
    if (fname.extension() == ".ent") {
        return ParseEntityLights(f, fname);
    }

    while (!f.eof()) {
        std::string buf;
        std::getline(f, buf);

        parser_t parser(buf, {fname.string()});

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
    logging::funcheader();

    Q_assert(surfacelight_templates.empty());

    for (entdict_t &l : radlights) {
        auto &entity = surfacelight_templates.emplace_back(std::make_unique<light_t>());
        entity->epairs = &l;
        entity->set_settings(*entity->epairs, settings::source::MAP);
    }

    for (auto &entity : all_lights) {
        std::string tex = entity->epairs->get("_surface");
        if (!tex.empty()) {
            surfacelight_templates.push_back(DuplicateEntity(*entity)); // makes a copy

            // Hack: clear templates light value to 0 so they don't cast light
            entity->light.set_value(0, settings::source::MAP);

            logging::print("Creating surface lights for texture \"{}\" from template at ({})\n", tex,
                entity->epairs->get("origin"));
        }
    }

    if (surfacelight_templates.empty())
        return;

    if (light_options.surflight_dump.value()) {
        surflights_dump_filename = light_options.sourceMap;
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
            if (!std::any_of(surfacelight_templates.begin(), surfacelight_templates.end(), [&](const auto &surflight) {
                    return FaceMatchesSurfaceLightTemplate(bsp, surf, face_modelinfo, *surflight, SURFLIGHT_Q1);
                }))
                continue;

            /* Generate the lights */
            GL_SubdivideSurface(surf, face_modelinfo, bsp);
        }
    }

    if (surflights_dump_file.is_open()) {
        surflights_dump_file.close();
        fmt::print("wrote surface lights to '{}'\n", surflights_dump_filename);
    }
}
