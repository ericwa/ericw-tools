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

#include <map>
#include <string>
#include <vector>

#include <common/mathlib.h>
#include <common/bspfile.h>
#include <light/light.hh>

#define DEFAULTLIGHTLEVEL 300.0f

using entdict_t = std::map<std::string, std::string>;

/*
 * Light attenuation formalae
 * (relative to distance 'x' from the light source)
 */
#define LF_SCALE 128
typedef enum {
    LF_LINEAR    = 0,   /* Linear (x) (DEFAULT) */
    LF_INVERSE   = 1,   /* Inverse (1/x), scaled by 1/128 */
    LF_INVERSE2  = 2,   /* Inverse square (1/(x^2)), scaled by 1/(128^2) */
    LF_INFINITE  = 3,   /* No attenuation, same brightness at any distance */
    LF_LOCALMIN  = 4,   /* No attenuation, non-additive minlight effect within
                           line of sight of the light source. */
    LF_INVERSE2A = 5,   /* Inverse square, with distance adjusted to avoid
                           exponentially bright values near the source.
                             (1/(x+128)^2), scaled by 1/(128^2) */
    LF_COUNT
} light_formula_t;

class entity_t {
public:
    qboolean spotlight;
    vec3_t spotvec; // computed
    float spotfalloff;
    float spotfalloff2;
    miptex_t *projectedmip; /*projected texture*/
    float projectionmatrix[16]; /*matrix used to project the specified texture. already contains origin.*/

    const entdict_t *epairs;
    
    const entdict_t *targetent;

    qboolean generated;     // if true, don't write to the bsp

    const bsp2_dleaf_t *leaf;    // for vis testing
    
    entity_t *next;
    
    const char *classname() const;
    
public:
    lockable_vec_t light, atten, formula, spotangle, spotangle2, style, bleed, anglescale;
    lockable_vec_t dirtscale, dirtgain, dirt, deviance, samples, projfov;
    lockable_vec3_t origin, color, mangle, projangle;
    lockable_string_t project_texture;
    settingsdict_t settings;

    light_formula_t getFormula() const { return static_cast<light_formula_t>(formula.intValue()); }
    
public:
    using strings = std::vector<std::string>;
    
    entity_t(void) :
        spotlight { false },
        spotvec { 0, 0, 0 },
        spotfalloff { 0 },
        spotfalloff2 { 0 },
        projectedmip { nullptr },
        projectionmatrix {
            0,0,0,0,
            0,0,0,0,
            0,0,0,0,
            0,0,0,0,
        },
        epairs {nullptr},
        targetent {nullptr},
        generated {false},
        leaf {nullptr},
        next {nullptr},

        // settings
    
        light { "light", DEFAULTLIGHTLEVEL },
        atten { "wait", 1.0f, 0.0f, std::numeric_limits<float>::infinity() },
        formula { "delay", 0.0f },
        spotangle { "angle", 40.0f },
        spotangle2 { "softangle", 0.0f },
        style { "style", 0.0f },
        bleed { "bleed", 0 },
        anglescale {strings{"anglesense", "anglescale"}, -1.0f }, // fallback to worldspawn
        dirtscale { "dirtscale", 0.0f },
        dirtgain { "dirtgain", 0 },
        dirt { "dirt", 0 },
        deviance { "deviance", 0 },
        samples { "samples", 16 },
        projfov { "project_fov", 90 },
        origin { "origin", 0, 0, 0 },
        color { "color", 255.0f, 255.0f, 255.0f, vec3_transformer_t::NORMALIZE_COLOR_TO_255 },
        mangle { "mangle", 0, 0, 0 }, // not transformed to vec
        projangle { "project_mangle", 20, 0, 0 }, // not transformed to vec
        project_texture { "project_texture", "" },
        settings {{
            &light, &atten, &formula, &spotangle, &spotangle2, &style, &bleed, &anglescale,
            &dirtscale, &dirtgain, &dirt, &deviance, &samples, &projfov,
            &origin, &color, &mangle, &projangle, &project_texture
        }}
    {}
};

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

//#define MAX_LIGHTS 65536
//extern entity_t *lights[MAX_LIGHTS];

const std::vector<entity_t>& GetLights();

const entdict_t *FindEntDictWithKeyPair(const std::string &key, const std::string &value);
const char *ValueForKey(const entity_t *ent, const char *key);
void GetVectorForKey(const entdict_t *ent, const char *key, vec3_t vec);

std::string EntDict_StringForKey(const entdict_t &dict, const std::string key);
float EntDict_FloatForKey(const entdict_t &dict, const std::string key);

void SetWorldKeyValue(const std::string &key, const std::string &value);
std::string WorldValueForKey(const std::string &key);

void LoadEntities(const bsp2_t *bsp);
void SetupLights(const bsp2_t *bsp);
void WriteEntitiesToString(bsp2_t *bsp);

vec_t GetLightValue(const lightsample_t *light, const entity_t *entity, vec_t dist);
    
bool Light_PointInSolid(const bsp2_t *bsp, const vec3_t point );

#endif /* __LIGHT_ENTITIES_H__ */
