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

#include <common/cmdlib.hh>
#include <common/mathlib.hh>
#include <common/bspfile.hh>
#include <common/log.hh>
#include <common/threads.hh>
#include <common/polylib.hh>

#include <light/litfile.hh>
#include <light/trace.hh>
#include <light/settings.hh>

#include <vector>
#include <map>
#include <set>
#include <string>
#include <cassert>
#include <limits>
#include <sstream>

#include <common/qvec.hh>

constexpr vec_t ON_EPSILON = 0.1;
constexpr vec_t ANGLE_EPSILON = 0.001;
constexpr vec_t EQUAL_EPSILON = 0.001;

// FIXME: use maximum dimension of level
constexpr vec_t MAX_SKY_DIST = 1000000;

struct lightsample_t
{
    qvec3d color, direction;
};

// CHECK: isn't average a bad algorithm for color brightness?
template<typename T>
constexpr float LightSample_Brightness(const T &color)
{
    return ((color[0] + color[1] + color[2]) / 3.0);
}

/**
 * A directional light, emitted from "sky*" textured faces.
 */
class sun_t
{
public:
    vec3_t sunvec;
    vec_t sunlight;
    vec3_t sunlight_color;
    bool dirt;
    float anglescale;
    int style;
    std::string suntexture;
};

/* for vanilla this would be 18. some engines allow higher limits though, which will be needed if we're scaling lightmap
 * resolution. */
/*with extra sampling, lit+lux etc, we need at least 46mb stack space per thread. yes, that's a lot. on the plus side,
 * it doesn't affect bsp complexity (actually, can simplify it a little)*/
#define MAXDIMENSION (255 + 1)

struct texorg_t
{
    qmat4x4f texSpaceToWorld;
    const gtexinfo_t *texinfo;
    vec_t planedist;
};

class modelinfo_t;
class globalconfig_t;

class lightmap_t
{
public:
    int style;
    lightsample_t *samples; // new'ed array of numpoints   //FIXME: this is stupid, we shouldn't need to allocate
                            // extra data here for -extra4
};

using lightmapdict_t = std::vector<lightmap_t>;

/*Warning: this stuff needs explicit initialisation*/
struct lightsurf_t
{
    const globalconfig_t *cfg;
    const modelinfo_t *modelinfo;
    const mbsp_t *bsp;
    const mface_t *face;
    /* these take precedence the values in modelinfo */
    vec_t minlight;
    vec3_t minlight_color;
    bool nodirt;

    plane_t plane;
    vec3_t snormal;
    vec3_t tnormal;

    /* 16 in vanilla. engines will hate you if this is not power-of-two-and-at-least-one */
    float lightmapscale;
    bool curved; /*normals are interpolated for smooth lighting*/

    int texmins[2];
    int texsize[2];
    vec_t exactmid[2];
    qvec3d midpoint;

    int numpoints;
    qvec3d *points; // new'ed array of numpoints
    vec3_t *normals; // new'ed array of numpoints
    bool *occluded; // new'ed array of numpoints
    int *realfacenums; // new'ed array of numpoints

    /*
     raw ambient occlusion amount per sample point, 0-1, where 1 is
     fully occluded. dirtgain/dirtscale are not applied yet
     */
    float *occlusion; // new'ed array of numpoints

    /* for sphere culling */
    vec3_t origin;
    vec_t radius;
    /* for AABB culling */
    aabb3d bounds = qvec3d(0);

    // for radiosity
    vec3_t radiosity;
    vec3_t texturecolor;

    /* stuff used by CalcPoint */
    texorg_t texorg;
    int width, height;

    /* for lit water. receive light from either front or back. */
    bool twosided;

    // ray batch stuff
    raystream_occlusion_t *occlusion_stream;
    raystream_intersection_t *intersection_stream;

    lightmapdict_t lightmapsByStyle;
};

/* debug */

extern bool debug_highlightseams;

enum debugmode_t
{
    debugmode_none = 0,
    debugmode_phong,
    debugmode_phong_obj,
    debugmode_dirt,
    debugmode_bounce,
    debugmode_bouncelights,
    debugmode_debugoccluded,
    debugmode_debugneighbours
};

extern debugmode_t debugmode;
extern bool verbose_log;

/* tracelist is a std::vector of pointers to modelinfo_t to use for LOS tests */
extern std::vector<const modelinfo_t *> tracelist;
extern std::vector<const modelinfo_t *> selfshadowlist;
extern std::vector<const modelinfo_t *> shadowworldonlylist;
extern std::vector<const modelinfo_t *> switchableshadowlist;

extern int numDirtVectors;

// other flags

extern bool dirt_in_use; // should any dirtmapping take place? set in SetupDirt

extern float fadegate;
extern int softsamples;
extern const vec3_t vec3_white;
extern float surflight_subdivide;
extern int sunsamples;

extern int dump_facenum;
extern bool dump_face;

extern int dump_vertnum;
extern bool dump_vert;

extern bool arghradcompat; // mxd

class modelinfo_t
{
    static constexpr vec_t DEFAULT_PHONG_ANGLE = 89.0;
    using strings = std::vector<std::string>;

public:
    const mbsp_t *bsp;
    const dmodelh2_t *model;
    float lightmapscale;
    qvec3d offset { };

    lockable_vec_t minlight {"minlight", 0};
    lockable_vec_t shadow {"shadow", 0};
    lockable_vec_t shadowself {strings{"shadowself", "selfshadow"}, 0};
    lockable_vec_t shadowworldonly {"shadowworldonly", 0};
    lockable_vec_t switchableshadow {"switchableshadow", 0};
    lockable_vec_t switchshadstyle {"switchshadstyle", 0};
    lockable_vec_t dirt {"dirt", 0};
    lockable_vec_t phong {"phong", 0};
    lockable_vec_t phong_angle {"phong_angle", 0};
    lockable_vec_t alpha {"alpha", 1.0};
    lockable_vec3_t minlight_color {strings{"minlight_color", "mincolor"}, 255, 255, 255, vec3_transformer_t::NORMALIZE_COLOR_TO_255};
    lockable_bool_t lightignore {"lightignore", false};

    float getResolvedPhongAngle() const
    {
        const float s = phong_angle.floatValue();
        if (s != 0) {
            return s;
        }
        if (phong.intValue() > 0) {
            return DEFAULT_PHONG_ANGLE;
        }
        return 0;
    }

    bool isWorld() const { return &bsp->dmodels[0] == model; }

    modelinfo_t(const mbsp_t *b, const dmodelh2_t *m, float lmscale)
        : bsp{b}, model{m}, lightmapscale{lmscale}
    {
    }

    settingsdict_t settings()
    {
        return {{&minlight, &shadow, &shadowself, &shadowworldonly, &switchableshadow, &switchshadstyle, &dirt, &phong,
            &phong_angle, &alpha, &minlight_color, &lightignore}};
    }
};

//
// worldspawn keys / command-line settings
//

class globalconfig_t
{
    using strings = std::vector<std::string>;

public:
    lockable_vec_t scaledist {"dist", 1.0, 0.0, 100.0};
    lockable_vec_t rangescale {"range", 0.5, 0.0, 100.0};
    lockable_vec_t global_anglescale {strings{"anglescale", "anglesense"}, 0.5, 0.0, 1.0};
    lockable_vec_t lightmapgamma {"gamma", 1.0, 0.0, 100.0};
    lockable_bool_t addminlight {"addmin", false};
    lockable_vec_t minlight {strings{"light", "minlight"}, 0};
    lockable_vec3_t minlight_color {strings{"minlight_color", "mincolor"}, 255.0, 255.0, 255.0, vec3_transformer_t::NORMALIZE_COLOR_TO_255};
    lockable_bool_t spotlightautofalloff {"spotlightautofalloff", false}; // mxd
    lockable_vec_t compilerstyle_start {"compilerstyle_start", 32}; // start index for switchable light styles, default 32 (FIXME: should be int)

    /* dirt */
    lockable_bool_t globalDirt {strings{"dirt", "dirty"}, false}; // apply dirt to all lights (unless they override it) + sunlight + minlight?
    lockable_vec_t dirtMode {"dirtmode", 0.0f};
    lockable_vec_t dirtDepth {"dirtdepth", 128.0, 1.0, std::numeric_limits<vec_t>::infinity()};
    lockable_vec_t dirtScale {"dirtscale", 1.0, 0.0, 100.0};
    lockable_vec_t dirtGain {"dirtgain", 1.0, 0.0, 100.0};
    lockable_vec_t dirtAngle {"dirtangle", 88.0, 0.0, 90.0};
    lockable_bool_t minlightDirt {"minlight_dirt", false}; // apply dirt to minlight?

    /* phong */
    lockable_bool_t phongallowed {"phong", true};
    lockable_vec_t phongangle {"phong_angle", 0};

    /* bounce */
    lockable_bool_t bounce {"bounce", false};
    lockable_bool_t bouncestyled {"bouncestyled", false};
    lockable_vec_t bouncescale {"bouncescale", 1.0, 0.0, 100.0};
    lockable_vec_t bouncecolorscale {"bouncecolorscale", 0.0, 0.0, 1.0};

    /* Q2 surface lights (mxd) */
    lockable_vec_t surflightscale {"surflightscale", 0.3}; // Strange defaults to match arghrad3 look...
    lockable_vec_t surflightbouncescale {"surflightbouncescale", 0.1};
    lockable_vec_t surflightsubdivision {strings{"surflightsubdivision", "choplight"}, 16.0, 1.0, 8192.0}; // "choplight" - arghrad3 name

    /* sunlight */
    /* sun_light, sun_color, sun_angle for http://www.bspquakeeditor.com/arghrad/ compatibility */
    lockable_vec_t sunlight {strings{"sunlight", "sun_light"}, 0.0}; /* main sun */
    lockable_vec3_t sunlight_color {strings{"sunlight_color", "sun_color"}, 255.0, 255.0, 255.0, vec3_transformer_t::NORMALIZE_COLOR_TO_255};
    lockable_vec_t sun2 {"sun2", 0.0}; /* second sun */
    lockable_vec3_t sun2_color {"sun2_color", 255.0, 255.0, 255.0, vec3_transformer_t::NORMALIZE_COLOR_TO_255};
    lockable_vec_t sunlight2 {"sunlight2", 0.0}; /* top sky dome */
    lockable_vec3_t sunlight2_color {strings{"sunlight2_color", "sunlight_color2"}, 255.0, 255.0, 255.0, vec3_transformer_t::NORMALIZE_COLOR_TO_255};
    lockable_vec_t sunlight3 {"sunlight3", 0.0}; /* bottom sky dome */
    lockable_vec3_t sunlight3_color {strings{"sunlight3_color", "sunlight_color3"}, 255.0, 255.0, 255.0, vec3_transformer_t::NORMALIZE_COLOR_TO_255};
    lockable_vec_t sunlight_dirt {"sunlight_dirt", 0.0};
    lockable_vec_t sunlight2_dirt {"sunlight2_dirt", 0.0};
    lockable_vec3_t sunvec {strings{"sunlight_mangle", "sun_mangle", "sun_angle"}, 0.0, -90.0, 0.0, vec3_transformer_t::MANGLE_TO_VEC}; /* defaults to straight down */
    lockable_vec3_t sun2vec {"sun2_mangle", 0.0, -90.0, 0.0, vec3_transformer_t::MANGLE_TO_VEC}; /* defaults to straight down */
    lockable_vec_t sun_deviance {"sunlight_penumbra", 0.0, 0.0, 180.0};
    lockable_vec3_t sky_surface {strings{"sky_surface", "sun_surface"}, 0, 0, 0} /* arghrad surface lights on sky faces */;

    settingsdict_t settings()
    {
        return {{&scaledist, &rangescale, &global_anglescale, &lightmapgamma, &addminlight, &minlight, &minlight_color,
            &spotlightautofalloff, // mxd
            &compilerstyle_start, &globalDirt, &dirtMode, &dirtDepth, &dirtScale, &dirtGain, &dirtAngle, &minlightDirt,
            &phongallowed, &bounce, &bouncestyled, &bouncescale, &bouncecolorscale, &surflightscale,
            &surflightbouncescale, &surflightsubdivision, // mxd
            &sunlight, &sunlight_color, &sun2, &sun2_color, &sunlight2, &sunlight2_color, &sunlight3, &sunlight3_color,
            &sunlight_dirt, &sunlight2_dirt, &sunvec, &sun2vec, &sun_deviance, &sky_surface}};
    }
};

extern uint8_t *filebase;
extern uint8_t *lit_filebase;
extern uint8_t *lux_filebase;

extern int oversample;
extern int write_litfile;
extern int write_luxfile;
extern bool onlyents;
extern bool scaledonly;
extern surfflags_t *extended_texinfo_flags;
extern bool novisapprox;
extern bool nolights;
extern bool litonly;
extern bool write_normals;

enum backend_t
{
    backend_bsp,
    backend_embree
};

extern backend_t rtbackend;
extern bool surflight_dump;
extern std::filesystem::path mapfilename;

// public functions

lockable_setting_t *FindSetting(std::string name);
void SetGlobalSetting(std::string name, std::string value, bool cmdline);
void FixupGlobalSettings(void);
void GetFileSpace(uint8_t **lightdata, uint8_t **colordata, uint8_t **deluxdata, int size);
void GetFileSpace_PreserveOffsetInBsp(uint8_t **lightdata, uint8_t **colordata, uint8_t **deluxdata, int lightofs);
const modelinfo_t *ModelInfoForModel(const mbsp_t *bsp, int modelnum);
/**
 * returns nullptr for "skip" faces
 */
const modelinfo_t *ModelInfoForFace(const mbsp_t *bsp, int facenum);
// bool Leaf_HasSky(const mbsp_t *bsp, const mleaf_t *leaf); //mxd. Missing definition
int light_main(int argc, const char **argv);
