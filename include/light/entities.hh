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

#include <string>
#include <vector>

#include <common/aabb.hh>
#include <common/settings.hh>
#include <common/entdata.h> // for entdict_t
#include <light/light.hh> // for sun_t

struct mbsp_t;
struct mface_t;
struct mleaf_t;

namespace img
{
struct texture;
}

constexpr float DEFAULTLIGHTLEVEL = 300.0;

/*
 * Light attenuation formalae
 * (relative to distance 'x' from the light source)
 */
constexpr float LF_SCALE = 128;
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
    LF_QRAD3 = 6, /* qrad3-style surface lights; 1 / (max(x, 16) * max(x, 16)) */
    LF_COUNT
};

class light_t : public settings::setting_container
{
public:
    bool spotlight = false;
    qvec3f spotvec{}; // computed
    float spotfalloff = 0;
    float spotfalloff2 = 0;
    const img::texture *projectedmip = nullptr; /*projected texture*/ // mxd. miptex_t -> rgba_miptex_t
    std::array<float, 16> projectionmatrix{}; /*matrix used to project the specified texture. already contains origin.*/

    const entdict_t *epairs = nullptr;

    const entdict_t *targetent = nullptr;

    bool generated = false; // if true, don't write to the bsp

    const mleaf_t *leaf;

    aabb3f bounds;

    settings::setting_light light;
    settings::setting_scalar atten;
    settings::setting_enum<light_formula_t> formula;
    settings::setting_scalar cone; // Q2
    settings::setting_scalar spotangle;
    settings::setting_scalar spotangle2;
    settings::setting_numeric<int32_t> style;
    settings::setting_scalar anglescale;
    settings::setting_scalar dirtscale;
    settings::setting_scalar dirtgain;
    settings::setting_scalar dirt;
    settings::setting_scalar deviance;
    settings::setting_int32 samples;
    settings::setting_scalar projfov;
    settings::setting_scalar bouncescale;
    settings::setting_scalar dirt_off_radius;
    settings::setting_scalar dirt_on_radius;
    settings::setting_bool sun;
    settings::setting_bool sunlight2;
    settings::setting_bool sunlight3;
    settings::setting_scalar falloff;
    settings::setting_bool bleed;
    settings::setting_vec3 origin;
    settings::setting_color color;
    settings::setting_vec3 mangle;
    settings::setting_vec3 projangle;
    settings::setting_string project_texture;
    settings::setting_string suntexture;
    settings::setting_bool nostaticlight;
    settings::setting_int32 surflight_group;
    settings::setting_scalar surflight_minlight_scale;
    settings::setting_scalar surflight_atten;
    settings::setting_int32 light_channel_mask;
    settings::setting_int32 shadow_channel_mask;
    settings::setting_bool nonudge;
    settings::setting_string switchableshadow_target;

    light_t();

    std::string classname() const;
    const light_formula_t &getFormula() const;
    void initAABB();
    void expandAABB(const qvec3f &pt);
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

void ResetLightEntities();
std::string TargetnameForLightStyle(int style);
std::vector<std::unique_ptr<light_t>> &GetLights();
const std::vector<entdict_t> &GetEntdicts();
std::vector<sun_t> &GetSuns();
std::vector<entdict_t> &GetRadLights();
/**
 * Returns the light entity that has "_switchableshadow_target" set to the given value, or nullptr.
 */
light_t *LightWithSwitchableShadowTargetValue(const std::string &target);

const std::vector<std::unique_ptr<light_t>> &GetSurfaceLightTemplates();

bool FaceMatchesSurfaceLightTemplate(
    const mbsp_t *bsp, const mface_t *face, const modelinfo_t *face_modelinfo, const light_t &surflight, int surf_type);

const entdict_t *FindEntDictWithKeyPair(const std::string &key, const std::string &value);

void LoadEntities(const settings::worldspawn_keys &cfg, const mbsp_t *bsp);
void SetupLights(const settings::worldspawn_keys &cfg, const mbsp_t *bsp);
bool ParseLightsFile(const fs::path &fname);
void WriteEntitiesToString(const settings::worldspawn_keys &cfg, mbsp_t *bsp);
aabb3f EstimateVisibleBoundsAtPoint(const qvec3f &point);

bool EntDict_CheckNoEmptyValues(const mbsp_t *bsp, const entdict_t &entdict);

entdict_t &WorldEnt();
std::tuple<qvec3f, bool> FixLightOnFace(const mbsp_t *bsp, const qvec3f &point, bool warn = true, float max_dist = 2.f);
