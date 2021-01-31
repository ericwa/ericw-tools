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

#include <common/entdata.h>
#include <common/mathlib.hh>
#include <common/bspfile.hh>
#include <light/light.hh>

#define DEFAULTLIGHTLEVEL 300.0f

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

class light_t {
public:
    qboolean spotlight;
    vec3_t spotvec; // computed
    float spotfalloff;
    float spotfalloff2;
    rgba_miptex_t *projectedmip; /*projected texture*/ //mxd. miptex_t -> rgba_miptex_t
    float projectionmatrix[16]; /*matrix used to project the specified texture. already contains origin.*/

    const entdict_t *epairs;
    
    const entdict_t *targetent;

    qboolean generated;     // if true, don't write to the bsp

    const char *classname() const;
    
    vec3_t mins, maxs;
    
public:
    lockable_vec_t light, atten, formula, spotangle, spotangle2, style, anglescale;
    lockable_vec_t dirtscale, dirtgain, dirt, deviance, samples, projfov, bouncescale;
    lockable_vec_t dirt_off_radius, dirt_on_radius;
    lockable_vec_t sun; //mxd
    lockable_bool_t sunlight2, sunlight3;
    lockable_vec_t falloff; //mxd
    lockable_bool_t bleed;
    lockable_vec3_t origin, color, mangle, projangle;
    lockable_string_t project_texture;
    lockable_string_t suntexture;
    lockable_bool_t nostaticlight;

    light_formula_t getFormula() const { return static_cast<light_formula_t>(formula.intValue()); }
    
public:
    using strings = std::vector<std::string>;
    
    light_t(void) :
        spotlight { false },
        spotfalloff { 0 },
        spotfalloff2 { 0 },
        projectedmip { nullptr },
        epairs {nullptr},
        targetent {nullptr},
        generated {false},

        // settings
    
        light { "light", DEFAULTLIGHTLEVEL },
        atten { "wait", 1.0f, 0.0f, std::numeric_limits<float>::infinity() },
        formula { "delay", 0.0f },
        spotangle { "angle", 40.0f },
        spotangle2 { "softangle", 0.0f },
        style { "style", 0.0f },
        anglescale {strings{"anglesense", "anglescale"}, -1.0f }, // fallback to worldspawn
        dirtscale { "dirtscale", 0.0f },
        dirtgain { "dirtgain", 0 },
        dirt { "dirt", 0 },
        deviance { "deviance", 0 },
        samples { "samples", 16 },
        projfov { "project_fov", 90 },
        bouncescale { "bouncescale", 1.0f },
        dirt_off_radius { "dirt_off_radius", 0.0f },
        dirt_on_radius { "dirt_on_radius", 0.0f },
        sun { "sun", 0 }, //mxd
        sunlight2 { "sunlight2", 0 },
        sunlight3 { "sunlight3", 0 },
        falloff{ "falloff", 0.0f }, //mxd
        bleed { "bleed", false },
        origin { "origin", 0, 0, 0 },
        color { "color", 255.0f, 255.0f, 255.0f, vec3_transformer_t::NORMALIZE_COLOR_TO_255 },
        mangle { "mangle", 0, 0, 0 }, // not transformed to vec
        projangle { "project_mangle", 20, 0, 0 }, // not transformed to vec
        project_texture { "project_texture", "" },
        suntexture { "suntexture", "" },
        nostaticlight { "nostaticlight", false }
    {
		VectorSet(spotvec, 0, 0, 0);
		
		for (int i = 0; i < 16; i++) {
			projectionmatrix[i] = 0;
		}
	}
    
    settingsdict_t settings() {
        return {{
            &light, &atten, &formula, &spotangle, &spotangle2, &style, &bleed, &anglescale,
            &dirtscale, &dirtgain, &dirt, &deviance, &samples, &projfov, &bouncescale,
            &dirt_off_radius, &dirt_on_radius,
            &sun, //mxd
            &sunlight2, &sunlight3,
            &falloff, //mxd
            &origin, &color, &mangle, &projangle, &project_texture, &suntexture, &nostaticlight
        }};
    }
    
    void initAABB() {
        AABB_Init(mins, maxs, *origin.vec3Value());
    }
    
    void expandAABB(const vec3_t pt) {
        AABB_Expand(mins, maxs, pt);
    }
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

std::string TargetnameForLightStyle(int style);
const std::vector<light_t>& GetLights();
const std::vector<sun_t>& GetSuns();

const entdict_t *FindEntDictWithKeyPair(const std::string &key, const std::string &value);
const char *ValueForKey(const light_t *ent, const char *key);
void EntDict_VectorForKey(const entdict_t &ent, const std::string &key, vec3_t vec);

void SetWorldKeyValue(const std::string &key, const std::string &value);
std::string WorldValueForKey(const std::string &key);

void LoadEntities(const globalconfig_t &cfg, const mbsp_t *bsp);
void SetupLights(const globalconfig_t &cfg, const mbsp_t *bsp);
bool ParseLightsFile(const char *fname);
void WriteEntitiesToString(const globalconfig_t &cfg, mbsp_t *bsp);
void EstimateVisibleBoundsAtPoint(const vec3_t point, vec3_t mins, vec3_t maxs);

bool EntDict_CheckNoEmptyValues(const mbsp_t *bsp, const entdict_t &entdict);

bool EntDict_CheckTargetKeysMatched(const mbsp_t *bsp, const entdict_t &entity, const std::vector<entdict_t> &all_edicts);

bool EntDict_CheckTargetnameKeyMatched(const mbsp_t *bsp, const entdict_t &entity, const std::vector<entdict_t> &all_edicts);

#endif /* __LIGHT_ENTITIES_H__ */
