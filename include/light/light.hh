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

#ifndef __LIGHT_LIGHT_H__
#define __LIGHT_LIGHT_H__

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

#define ON_EPSILON    0.1f
#define ANGLE_EPSILON 0.001f
#define EQUAL_EPSILON 0.001f

// FIXME: use maximum dimension of level
#define MAX_SKY_DIST 1000000.0f

typedef struct {
    vec3_t color;
    vec3_t direction;
} lightsample_t;

static inline float LightSample_Brightness(const vec3_t color) {
    return ((color[0] + color[1] + color[2]) / 3.0);
}

static inline float LightSample_Brightness(const qvec3f color) {
    return ((color[0] + color[1] + color[2]) / 3.0);
}

/**
 * A directional light, emitted from "sky*" textured faces.
 */
class sun_t {
public:
    vec3_t sunvec;
    vec_t sunlight;
    vec3_t sunlight_color;
    qboolean dirt;
    float anglescale;
    int style;
    std::string suntexture;
};

/* for vanilla this would be 18. some engines allow higher limits though, which will be needed if we're scaling lightmap resolution. */
/*with extra sampling, lit+lux etc, we need at least 46mb stack space per thread. yes, that's a lot. on the plus side, it doesn't affect bsp complexity (actually, can simplify it a little)*/
#define MAXDIMENSION (255+1)

typedef struct {
    qmat4x4f texSpaceToWorld;
    const gtexinfo_t *texinfo;
    vec_t planedist;
} texorg_t;

class modelinfo_t;
class globalconfig_t;

class lightmap_t {
public:
    int style;
    lightsample_t *samples; // malloc'ed array of numpoints   //FIXME: this is stupid, we shouldn't need to allocate extra data here for -extra4
};

using lightmapdict_t = std::vector<lightmap_t>;

/*Warning: this stuff needs explicit initialisation*/
typedef struct {
    const globalconfig_t *cfg;
    const modelinfo_t *modelinfo;
    const mbsp_t *bsp;
    const bsp2_dface_t *face;
    /* these take precedence the values in modelinfo */
    vec_t minlight;
    vec3_t minlight_color;
    qboolean nodirt;
    
    plane_t plane;
    vec3_t snormal;
    vec3_t tnormal;
    
    /* 16 in vanilla. engines will hate you if this is not power-of-two-and-at-least-one */
    float lightmapscale;
    qboolean curved; /*normals are interpolated for smooth lighting*/
    
    int texmins[2];
    int texsize[2];
    vec_t exactmid[2];
    vec3_t midpoint;
    
    int numpoints;
    vec3_t *points; // malloc'ed array of numpoints
    vec3_t *normals; // malloc'ed array of numpoints
    bool *occluded; // malloc'ed array of numpoints
    int *realfacenums; // malloc'ed array of numpoints
    
    /*
     raw ambient occlusion amount per sample point, 0-1, where 1 is
     fully occluded. dirtgain/dirtscale are not applied yet
     */
    vec_t *occlusion; // malloc'ed array of numpoints
    
    /* for sphere culling */
    vec3_t origin;
    vec_t radius;
    /* for AABB culling */
    vec3_t mins, maxs;

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
} lightsurf_t;

/* debug */

extern bool debug_highlightseams;

typedef enum {
    debugmode_none = 0,
    debugmode_phong,
    debugmode_phong_obj,
    debugmode_dirt,
    debugmode_bounce,
    debugmode_bouncelights,
    debugmode_debugoccluded,
    debugmode_debugneighbours
} debugmode_t;

extern debugmode_t debugmode;
extern bool verbose_log;

/* tracelist is a std::vector of pointers to modelinfo_t to use for LOS tests */
extern std::vector<const modelinfo_t *> tracelist;
extern std::vector<const modelinfo_t *> selfshadowlist;
extern std::vector<const modelinfo_t *> shadowworldonlylist;
extern std::vector<const modelinfo_t *> switchableshadowlist;

extern int numDirtVectors;

// other flags

extern bool dirt_in_use;               // should any dirtmapping take place? set in SetupDirt

extern float fadegate;
extern int softsamples;
extern const vec3_t vec3_white;
extern float surflight_subdivide;
extern int sunsamples;

extern int dump_facenum;
extern bool dump_face;

extern int dump_vertnum;
extern bool dump_vert;

extern bool arghradcompat; //mxd

class modelinfo_t {
    using strings = std::vector<std::string>;
#define DEFAULT_PHONG_ANGLE 89.0f
    
public:
    const mbsp_t *bsp;
    const dmodel_t *model;
    float lightmapscale;
    vec3_t offset;

public:
    lockable_vec_t minlight, shadow, shadowself, shadowworldonly, switchableshadow, switchshadstyle, dirt, phong, phong_angle, alpha;
    lockable_vec3_t minlight_color;
    lockable_bool_t lightignore;
    
    float getResolvedPhongAngle() const {
        const float s = phong_angle.floatValue();
        if (s != 0) {
            return s;
        }
        if (phong.intValue() > 0) {
            return DEFAULT_PHONG_ANGLE;
        }
        return 0;
    }
    
    bool isWorld() const {
        return &bsp->dmodels[0] == model;
    }
    
public:
    modelinfo_t(const mbsp_t *b, const dmodel_t *m, float lmscale) :
        bsp { b },
        model { m },
        lightmapscale { lmscale },
        minlight { "minlight", 0 },
        shadow { "shadow", 0 },
        shadowself { strings{"shadowself", "selfshadow"}, 0 },
        shadowworldonly { "shadowworldonly", 0 },
        switchableshadow { "switchableshadow", 0 },
        switchshadstyle { "switchshadstyle", 0},
        dirt { "dirt", 0 },
        phong { "phong", 0 },
        phong_angle { "phong_angle", 0 },
        alpha { "alpha", 1.0f },
        minlight_color { strings{"minlight_color", "mincolor"}, 255, 255, 255, vec3_transformer_t::NORMALIZE_COLOR_TO_255 },
        lightignore { "lightignore", false }
    {
		VectorSet(offset, 0, 0, 0);
	}
    
    settingsdict_t settings() {
        return {{
            &minlight, &shadow, &shadowself, &shadowworldonly, &switchableshadow, &switchshadstyle, &dirt, &phong, &phong_angle, &alpha,
            &minlight_color, &lightignore
        }};
    }
};

//
// worldspawn keys / command-line settings
//

class globalconfig_t {
    using strings = std::vector<std::string>;
    
public:
    lockable_vec_t scaledist, rangescale, global_anglescale, lightmapgamma;
    lockable_bool_t addminlight;
    lockable_vec_t minlight;
    lockable_vec3_t minlight_color;
    lockable_bool_t spotlightautofalloff; //mxd
    lockable_vec_t compilerstyle_start; // start index for switchable light styles, default 32 (FIXME: should be int)

    /* dirt */
    lockable_bool_t globalDirt;          // apply dirt to all lights (unless they override it) + sunlight + minlight?
    lockable_vec_t dirtMode, dirtDepth, dirtScale, dirtGain, dirtAngle;
    
    lockable_bool_t minlightDirt;   // apply dirt to minlight?
    
    /* phong */
    lockable_bool_t phongallowed;
    lockable_vec_t phongangle;
    
    /* bounce */
    lockable_bool_t bounce;
    lockable_bool_t bouncestyled;
    lockable_vec_t bouncescale, bouncecolorscale;
    
    /* Q2 surface lights (mxd) */
    lockable_vec_t surflightscale;
    lockable_vec_t surflightbouncescale;
    lockable_vec_t surflightsubdivision;
    
    /* sunlight */
    lockable_vec_t sunlight;
    lockable_vec3_t sunlight_color;
    lockable_vec_t sun2;
    lockable_vec3_t sun2_color;
    lockable_vec_t sunlight2;
    lockable_vec3_t sunlight2_color;
    lockable_vec_t sunlight3;
    lockable_vec3_t sunlight3_color;
    lockable_vec_t sunlight_dirt;
    lockable_vec_t sunlight2_dirt;
    lockable_vec3_t sunvec;
    lockable_vec3_t sun2vec;
    lockable_vec_t sun_deviance;
    lockable_vec3_t sky_surface;
    
    globalconfig_t() :
        scaledist {"dist", 1.0, 0.0f, 100.0f},
        rangescale {"range", 0.5f, 0.0f, 100.0f},
        global_anglescale {strings{"anglescale", "anglesense"}, 0.5, 0.0f, 1.0f},
        lightmapgamma {"gamma", 1.0, 0.0f, 100.0f},

        addminlight {"addmin", false},
        minlight {strings{"light", "minlight"}, 0},
        minlight_color {strings{"minlight_color", "mincolor"}, 255.0f, 255.0f, 255.0f, vec3_transformer_t::NORMALIZE_COLOR_TO_255},
        spotlightautofalloff { "spotlightautofalloff", false }, //mxd
        compilerstyle_start { "compilerstyle_start", 32 },

        /* dirt */
        globalDirt {strings{"dirt", "dirty"}, false},
        dirtMode {"dirtmode", 0.0f},
        dirtDepth {"dirtdepth", 128.0f, 1.0f, std::numeric_limits<float>::infinity()},
        dirtScale {"dirtscale", 1.0f, 0.0f, 100.0f},
        dirtGain {"dirtgain", 1.0f, 0.0f, 100.0f},
        dirtAngle {"dirtangle", 88.0f, 0.0f, 90.0f},
        minlightDirt {"minlight_dirt", false},

        /* phong */
        phongallowed {"phong", true},
        phongangle { "phong_angle", 0}, 

        /* bounce */
        bounce {"bounce", false},
        bouncestyled {"bouncestyled", false},
        bouncescale {"bouncescale", 1.0f, 0.0f, 100.0f},
        bouncecolorscale {"bouncecolorscale", 0.0f, 0.0f, 1.0f},

        /* Q2 surface lights (mxd) */
        surflightscale       { "surflightscale", 0.3f }, // Strange defaults to match arghrad3 look...
        surflightbouncescale { "surflightbouncescale", 0.1f },
        surflightsubdivision { strings { "surflightsubdivision", "choplight" }, 16.0f, 1.0f, 8192.0f }, // "choplight" - arghrad3 name

        /* sun */
        /* sun_light, sun_color, sun_angle for http://www.bspquakeeditor.com/arghrad/ compatibility */
        sunlight        { strings{"sunlight", "sun_light"}, 0.0f },  /* main sun */
        sunlight_color  { strings{"sunlight_color", "sun_color"}, 255.0f, 255.0f, 255.0f, vec3_transformer_t::NORMALIZE_COLOR_TO_255 },
        sun2            { "sun2", 0.0f },      /* second sun */
        sun2_color      { "sun2_color", 255.0f, 255.0f, 255.0f, vec3_transformer_t::NORMALIZE_COLOR_TO_255 },
        sunlight2       { "sunlight2", 0.0f }, /* top sky dome */
        sunlight2_color { strings{"sunlight2_color", "sunlight_color2"}, 255.0f, 255.0f, 255.0f, vec3_transformer_t::NORMALIZE_COLOR_TO_255 },
        sunlight3       { "sunlight3", 0.0f }, /* bottom sky dome */
        sunlight3_color { strings{"sunlight3_color", "sunlight_color3"}, 255.0f, 255.0f, 255.0f, vec3_transformer_t::NORMALIZE_COLOR_TO_255 },
        sunlight_dirt   { "sunlight_dirt", 0.0f },
        sunlight2_dirt  { "sunlight2_dirt", 0.0f },
        sunvec          { strings{"sunlight_mangle", "sun_mangle", "sun_angle"}, 0.0f, -90.0f, 0.0f, vec3_transformer_t::MANGLE_TO_VEC },  /* defaults to straight down */
        sun2vec         { "sun2_mangle", 0.0f, -90.0f, 0.0f, vec3_transformer_t::MANGLE_TO_VEC },  /* defaults to straight down */
        sun_deviance    { "sunlight_penumbra", 0.0f, 0.0f, 180.0f },
        sky_surface     { strings{"sky_surface", "sun_surface"}, 0, 0, 0} /* arghrad surface lights on sky faces */
    {}
    
    settingsdict_t settings() {
        return {{
            &scaledist, &rangescale, &global_anglescale, &lightmapgamma,
            &addminlight,
            &minlight,
            &minlight_color,
            &spotlightautofalloff, //mxd
            &compilerstyle_start,
            &globalDirt,
            &dirtMode, &dirtDepth, &dirtScale, &dirtGain, &dirtAngle,
            &minlightDirt,
            &phongallowed,
            &bounce, &bouncestyled, &bouncescale, &bouncecolorscale,
            &surflightscale, &surflightbouncescale, &surflightsubdivision, //mxd
            &sunlight,
            &sunlight_color,
            &sun2,
            &sun2_color,
            &sunlight2,
            &sunlight2_color,
            &sunlight3,
            &sunlight3_color,
            &sunlight_dirt,
            &sunlight2_dirt,
            &sunvec,
            &sun2vec,
            &sun_deviance,
            &sky_surface
        }};
    }
};

extern uint8_t *filebase;
extern uint8_t *lit_filebase;
extern uint8_t *lux_filebase;

extern int oversample;
extern int write_litfile;
extern int write_luxfile;
extern qboolean onlyents;
extern qboolean scaledonly;
extern surfflags_t *extended_texinfo_flags;
extern qboolean novisapprox;
extern bool nolights;
extern bool litonly;

extern qboolean surflight_dump;
extern char mapfilename[1024];

// public functions

lockable_setting_t *FindSetting(std::string name);
void SetGlobalSetting(std::string name, std::string value, bool cmdline);
void FixupGlobalSettings(void);
void GetFileSpace(uint8_t **lightdata, uint8_t **colordata, uint8_t **deluxdata, int size);
void GetFileSpace_PreserveOffsetInBsp(uint8_t **lightdata, uint8_t **colordata, uint8_t **deluxdata, int lightofs);
const modelinfo_t *ModelInfoForModel(const mbsp_t *bsp, int modelnum);
/**
 * returs nullptr for "skip" faces
 */
const modelinfo_t *ModelInfoForFace(const mbsp_t *bsp, int facenum);
//bool Leaf_HasSky(const mbsp_t *bsp, const mleaf_t *leaf); //mxd. Missing definition
int light_main(int argc, const char **argv);

#endif /* __LIGHT_LIGHT_H__ */
