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

#pragma once

#include <map>
#include <string>
#include <vector>

#include <common/entdata.h>
#include <common/mathlib.hh>
#include <common/bspfile.hh>
#include <light/light.hh>

constexpr vec_t DEFAULTLIGHTLEVEL = 300.0;

/*
 * Light attenuation formalae
 * (relative to distance 'x' from the light source)
 */
constexpr vec_t LF_SCALE = 128;
enum light_formula_t
{
    LF_LINEAR = 0, /* Linear (x) (DEFAULT) */
    LF_INVERSE = 1, /* Inverse (1/x), scaled by 1/128 */
    LF_INVERSE2 = 2, /* Inverse square (1/(x^2)), scaled by 1/(128^2) */
    LF_INFINITE = 3, /* No attenuation, same brightness at any distance */
    LF_LOCALMIN = 4, /* No attenuation, non-additive minlight effect within
                        line of sight of the light source. */
    LF_INVERSE2A = 5, /* Inverse square, with distance adjusted to avoid
                         exponentially bright values near the source.
                           (1/(x+128)^2), scaled by 1/(128^2) */
    LF_COUNT
};

class light_t
{
public:
    bool spotlight = false;
    qvec3d spotvec { }; // computed
    float spotfalloff = 0;
    float spotfalloff2 = 0;
    const rgba_miptex_t *projectedmip = nullptr; /*projected texture*/ // mxd. miptex_t -> rgba_miptex_t
    std::array<vec_t, 16> projectionmatrix { }; /*matrix used to project the specified texture. already contains origin.*/

    const entdict_t *epairs = nullptr;

    const entdict_t *targetent = nullptr;

    bool generated = false; // if true, don't write to the bsp

    aabb3d bounds;

    const char *classname() const;

public:
    lockable_vec_t light {"light", DEFAULTLIGHTLEVEL};
    lockable_vec_t atten {"wait", 1.0, 0.0, std::numeric_limits<vec_t>::infinity()};
    lockable_vec_t formula {"delay", 0.0};
    lockable_vec_t spotangle {"angle", 40.0};
    lockable_vec_t spotangle2 {"softangle", 0.0};
    lockable_vec_t style {"style", 0.0};
    lockable_vec_t anglescale {strings{"anglesense", "anglescale"}, -1.0}; // fallback to worldspawn
    lockable_vec_t dirtscale {"dirtscale", 0.0};
    lockable_vec_t dirtgain {"dirtgain", 0};
    lockable_vec_t dirt {"dirt", 0};
    lockable_vec_t deviance {"deviance", 0};
    lockable_vec_t samples {"samples", 16};
    lockable_vec_t projfov {"project_fov", 90};
    lockable_vec_t bouncescale {"bouncescale", 1.0};
    lockable_vec_t dirt_off_radius {"dirt_off_radius", 0.0};
    lockable_vec_t dirt_on_radius {"dirt_on_radius", 0.0};
    lockable_vec_t sun {"sun", 0}; // mxd
    lockable_bool_t sunlight2 {"sunlight2", 0};
    lockable_bool_t sunlight3 {"sunlight3", 0};
    lockable_vec_t falloff {"falloff", 0.0}; // mxd
    lockable_bool_t bleed {"bleed", false};
    lockable_vec3_t origin {"origin", 0, 0, 0};
    lockable_vec3_t color {"color", 255.0, 255.0, 255.0, vec3_transformer_t::NORMALIZE_COLOR_TO_255};
    lockable_vec3_t mangle {"mangle", 0, 0, 0}; // not transformed to vec
    lockable_vec3_t projangle {"project_mangle", 20, 0, 0}; // not transformed to vec
    lockable_string_t project_texture {"project_texture", ""};
    lockable_string_t suntexture {"suntexture", ""};
    lockable_bool_t nostaticlight {"nostaticlight", false};

    light_formula_t getFormula() const { return static_cast<light_formula_t>(formula.intValue()); }

public:
    using strings = std::vector<std::string>;

    settingsdict_t settings()
    {
        return {{&light, &atten, &formula, &spotangle, &spotangle2, &style, &bleed, &anglescale, &dirtscale, &dirtgain,
            &dirt, &deviance, &samples, &projfov, &bouncescale, &dirt_off_radius, &dirt_on_radius,
            &sun, // mxd
            &sunlight2, &sunlight3,
            &falloff, // mxd
            &origin, &color, &mangle, &projangle, &project_texture, &suntexture, &nostaticlight}};
    }

    void initAABB() { bounds = origin.vec3Value(); }

    void expandAABB(const qvec3d &pt) { bounds += pt; }
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
const std::vector<light_t> &GetLights();
const std::vector<sun_t> &GetSuns();

const entdict_t *FindEntDictWithKeyPair(const std::string &key, const std::string &value);
const char *ValueForKey(const light_t *ent, const char *key);
qvec3d EntDict_VectorForKey(const entdict_t &ent, const std::string &key);

void SetWorldKeyValue(const std::string &key, const std::string &value);
const std::string &WorldValueForKey(const std::string &key);

void LoadEntities(const globalconfig_t &cfg, const mbsp_t *bsp);
void SetupLights(const globalconfig_t &cfg, const mbsp_t *bsp);
bool ParseLightsFile(const std::filesystem::path &fname);
void WriteEntitiesToString(const globalconfig_t &cfg, mbsp_t *bsp);
aabb3d EstimateVisibleBoundsAtPoint(const qvec3d &point);

bool EntDict_CheckNoEmptyValues(const mbsp_t *bsp, const entdict_t &entdict);

bool EntDict_CheckTargetKeysMatched(
    const mbsp_t *bsp, const entdict_t &entity, const std::vector<entdict_t> &all_edicts);

bool EntDict_CheckTargetnameKeyMatched(
    const mbsp_t *bsp, const entdict_t &entity, const std::vector<entdict_t> &all_edicts);
