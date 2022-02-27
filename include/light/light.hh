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
#include <common/imglib.hh>
#include <common/settings.hh>

#include <light/litfile.hh>
#include <light/trace.hh>

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
    qvec3d sunvec;
    vec_t sunlight;
    qvec3d sunlight_color;
    bool dirt;
    float anglescale;
    int style;
    std::string suntexture;
};

/* for vanilla this would be 18. some engines allow higher limits though, which will be needed if we're scaling lightmap
 * resolution. */
/*with extra sampling, lit+lux etc, we need at least 46mb space per thread. yes, that's a lot. on the plus side,
 * it doesn't affect bsp complexity (actually, can simplify it a little)*/
constexpr size_t MAXDIMENSION = 255 + 1;

struct texorg_t
{
    qmat4x4f texSpaceToWorld;
    const gtexinfo_t *texinfo;
    vec_t planedist;
};

class modelinfo_t;
namespace settings
{
class worldspawn_keys;
};

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
    const settings::worldspawn_keys *cfg;
    const modelinfo_t *modelinfo;
    const mbsp_t *bsp;
    const mface_t *face;
    /* these take precedence the values in modelinfo */
    vec_t minlight;
    qvec3d minlight_color;
    bool nodirt;

    qplane3d plane;
    qvec3d snormal;
    qvec3d tnormal;

    /* 16 in vanilla. engines will hate you if this is not power-of-two-and-at-least-one */
    float lightmapscale;
    bool curved; /*normals are interpolated for smooth lighting*/

    int texmins[2];
    int texsize[2];
    qvec2d exactmid;
    qvec3d midpoint;

    int numpoints;
    qvec3d *points; // new'ed array of numpoints
    qvec3d *normals; // new'ed array of numpoints
    bool *occluded; // new'ed array of numpoints
    int *realfacenums; // new'ed array of numpoints

    /*
     raw ambient occlusion amount per sample point, 0-1, where 1 is
     fully occluded. dirtgain/dirtscale are not applied yet
     */
    float *occlusion; // new'ed array of numpoints

    /* for sphere culling */
    qvec3d origin;
    vec_t radius;
    /* for AABB culling */
    aabb3d bounds = qvec3d(0);

    // for radiosity
    qvec3d radiosity;
    qvec3d texturecolor;

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

enum class debugmodes
{
    none = 0,
    phong,
    phong_obj,
    dirt,
    bounce,
    bouncelights,
    debugoccluded,
    debugneighbours,
    phong_tangents,
    phong_bitangents
};

enum class lightfile
{
    none = 0,
    external = 1,
    bspx = 2,
    both = external | bspx,
    lit2 = 4
};

/* tracelist is a std::vector of pointers to modelinfo_t to use for LOS tests */
extern std::vector<const modelinfo_t *> tracelist;
extern std::vector<const modelinfo_t *> selfshadowlist;
extern std::vector<const modelinfo_t *> shadowworldonlylist;
extern std::vector<const modelinfo_t *> switchableshadowlist;

extern int numDirtVectors;

// other flags

extern bool dirt_in_use; // should any dirtmapping take place? set in SetupDirt

constexpr qvec3d vec3_white{255};

extern int dump_facenum;
extern int dump_vertnum;

class modelinfo_t : public settings::setting_container
{
    static constexpr vec_t DEFAULT_PHONG_ANGLE = 89.0;

public:
    const mbsp_t *bsp;
    const dmodelh2_t *model;
    float lightmapscale;
    qvec3d offset{};

    settings::setting_scalar minlight{this, "minlight", 0};
    settings::setting_scalar shadow{this, "shadow", 0};
    settings::setting_scalar shadowself{this, {"shadowself", "selfshadow"}, 0};
    settings::setting_scalar shadowworldonly{this, "shadowworldonly", 0};
    settings::setting_scalar switchableshadow{this, "switchableshadow", 0};
    settings::setting_int32 switchshadstyle{this, "switchshadstyle", 0};
    settings::setting_scalar dirt{this, "dirt", 0};
    settings::setting_scalar phong{this, "phong", 0};
    settings::setting_scalar phong_angle{this, "phong_angle", 0};
    settings::setting_scalar alpha{this, "alpha", 1.0};
    settings::setting_color minlight_color{this, {"minlight_color", "mincolor"}, 255.0, 255.0, 255.0};
    settings::setting_bool lightignore{this, "lightignore", false};

    float getResolvedPhongAngle() const
    {
        const float s = phong_angle.value();
        if (s != 0) {
            return s;
        }
        if (phong.value() > 0) {
            return DEFAULT_PHONG_ANGLE;
        }
        return 0;
    }

    bool isWorld() const { return &bsp->dmodels[0] == model; }

    modelinfo_t(const mbsp_t *b, const dmodelh2_t *m, float lmscale) : bsp{b}, model{m}, lightmapscale{lmscale} { }
};

//
// worldspawn keys / command-line settings
//

namespace settings
{
extern setting_group worldspawn_group;

class worldspawn_keys : public virtual setting_container
{
public:
    setting_scalar scaledist{this, "dist", 1.0, 0.0, 100.0, &worldspawn_group};
    setting_scalar rangescale{this, "range", 0.5, 0.0, 100.0, &worldspawn_group};
    setting_scalar global_anglescale{this, {"anglescale", "anglesense"}, 0.5, 0.0, 1.0, &worldspawn_group};
    setting_scalar lightmapgamma{this, "gamma", 1.0, 0.0, 100.0, &worldspawn_group};
    setting_bool addminlight{this, "addmin", false, &worldspawn_group};
    setting_scalar minlight{this, {"light", "minlight"}, 0, &worldspawn_group};
    setting_color minlight_color{this, {"minlight_color", "mincolor"}, 255.0, 255.0, 255.0, &worldspawn_group};
    setting_bool spotlightautofalloff{this, "spotlightautofalloff", false, &worldspawn_group}; // mxd
    setting_int32 compilerstyle_start{
        this, "compilerstyle_start", 32, &worldspawn_group}; // start index for switchable light styles, default 32

    /* dirt */
    setting_bool globalDirt{this, {"dirt", "dirty"}, false,
        &worldspawn_group}; // apply dirt to all lights (unless they override it) + sunlight + minlight?
    setting_scalar dirtMode{this, "dirtmode", 0.0f, &worldspawn_group};
    setting_scalar dirtDepth{this, "dirtdepth", 128.0, 1.0, std::numeric_limits<vec_t>::infinity(), &worldspawn_group};
    setting_scalar dirtScale{this, "dirtscale", 1.0, 0.0, 100.0, &worldspawn_group};
    setting_scalar dirtGain{this, "dirtgain", 1.0, 0.0, 100.0, &worldspawn_group};
    setting_scalar dirtAngle{this, "dirtangle", 88.0, 1.0, 90.0, &worldspawn_group};
    setting_bool minlightDirt{this, "minlight_dirt", false, &worldspawn_group}; // apply dirt to minlight?

    /* phong */
    setting_bool phongallowed{this, "phong", true, &worldspawn_group};
    setting_scalar phongangle{this, "phong_angle", 0, &worldspawn_group};

    /* bounce */
    setting_bool bounce{this, "bounce", false, &worldspawn_group};
    setting_bool bouncestyled{this, "bouncestyled", false, &worldspawn_group};
    setting_scalar bouncescale{this, "bouncescale", 1.0, 0.0, 100.0, &worldspawn_group};
    setting_scalar bouncecolorscale{this, "bouncecolorscale", 0.0, 0.0, 1.0, &worldspawn_group};

    /* Q2 surface lights (mxd) */
    setting_scalar surflightscale{
        this, "surflightscale", 0.3, &worldspawn_group}; // Strange defaults to match arghrad3 look...
    setting_scalar surflightbouncescale{this, "surflightbouncescale", 0.1, &worldspawn_group};
    setting_scalar surflightsubdivision{this, {"surflightsubdivision", "choplight"}, 16.0, 1.0, 8192.0,
        &worldspawn_group}; // "choplight" - arghrad3 name

    /* sunlight */
    /* sun_light, sun_color, sun_angle for http://www.bspquakeeditor.com/arghrad/ compatibility */
    setting_scalar sunlight{this, {"sunlight", "sun_light"}, 0.0, &worldspawn_group}; /* main sun */
    setting_color sunlight_color{this, {"sunlight_color", "sun_color"}, 255.0, 255.0, 255.0, &worldspawn_group};
    setting_scalar sun2{this, "sun2", 0.0, &worldspawn_group}; /* second sun */
    setting_color sun2_color{this, "sun2_color", 255.0, 255.0, 255.0, &worldspawn_group};
    setting_scalar sunlight2{this, "sunlight2", 0.0, &worldspawn_group}; /* top sky dome */
    setting_color sunlight2_color{this, {"sunlight2_color", "sunlight_color2"}, 255.0, 255.0, 255.0, &worldspawn_group};
    setting_scalar sunlight3{this, "sunlight3", 0.0, &worldspawn_group}; /* bottom sky dome */
    setting_color sunlight3_color{this, {"sunlight3_color", "sunlight_color3"}, 255.0, 255.0, 255.0, &worldspawn_group};
    setting_scalar sunlight_dirt{this, "sunlight_dirt", 0.0, &worldspawn_group};
    setting_scalar sunlight2_dirt{this, "sunlight2_dirt", 0.0, &worldspawn_group};
    setting_mangle sunvec{this, {"sunlight_mangle", "sun_mangle", "sun_angle"}, 0.0, -90.0, 0.0,
        &worldspawn_group}; /* defaults to straight down */
    setting_mangle sun2vec{this, "sun2_mangle", 0.0, -90.0, 0.0, &worldspawn_group}; /* defaults to straight down */
    setting_scalar sun_deviance{this, "sunlight_penumbra", 0.0, 0.0, 180.0, &worldspawn_group};
    setting_vec3 sky_surface{
        this, {"sky_surface", "sun_surface"}, 0, 0, 0, &worldspawn_group} /* arghrad surface lights on sky faces */;
};

extern setting_group output_group;
extern setting_group debug_group;
extern setting_group postprocessing_group;
extern setting_group experimental_group;

class light_settings : public common_settings, public worldspawn_keys
{
public:
    // slight modification to setting_numeric that supports
    // a default value if a non-number is supplied after parsing
    class setting_soft : public setting_int32
    {
    public:
        using setting_int32::setting_int32;

        virtual bool parse(const std::string &settingName, parser_base_t &parser, bool locked = false) override
        {
            if (!parser.parse_token()) {
                return false;
            }

            try {
                int32_t f = static_cast<int32_t>(std::stoull(parser.token));

                setValueFromParse(f, locked);

                return true;
            }
            catch (std::exception &) {
                // if we didn't provide a (valid) number, then
                // assume it's meant to be the default of -1
                if (parser.token[0] == '-') {
                    setValueFromParse(-1, locked);
                    return true;
                } else {
                    return false;
                }
            }
        }

        virtual std::string format() const { return "[n]"; }
    };

    class setting_extra : public setting_value<int32_t>
    {
    public:
        using setting_value::setting_value;

        virtual bool parse(const std::string &settingName, parser_base_t &parser, bool locked = false)
        {
            if (settingName.back() == '4') {
                setValueFromParse(4, locked);
            } else {
                setValueFromParse(2, locked);
            }

            return true;
        }

        virtual std::string stringValue() const override { return std::to_string(_value); };

        virtual std::string format() const override { return ""; };
    };

    setting_bool surflight_dump{this, "surflight_dump", false, &debug_group, "dump surface lights to a .map file"};
    setting_scalar surflight_subdivide{
        this, "surflight_subdivide", 128.0, 64.0, 2048.0, &performance_group, "surface light subdivision size"};
    setting_bool onlyents{this, "onlyents", false, &output_group, "only update entities"};
    setting_bool write_normals{
        this, "wrnormals", false, &output_group, "output normals, tangents and bitangents in a BSPX lump"};
    setting_bool novanilla{
        this, "novanilla", false, &experimental_group, "implies -bspxlit; don't write vanilla lighting"};
    setting_scalar gate{this, "gate", EQUAL_EPSILON, &performance_group, "cutoff lights at this brightness level"};
    setting_int32 sunsamples{
        this, "sunsamples", 64, 8, 2048, &performance_group, "set samples for _sunlight2, default 64"};
    setting_bool arghradcompat{
        this, "arghradcompat", false, &output_group, "enable compatibility for Arghrad-specific keys"};
    setting_bool nolighting{this, "nolighting", false, &output_group, "don't output main world lighting (Q2RTX)"};
    setting_vec3 debugface{this, "debugface", std::numeric_limits<vec_t>::quiet_NaN(),
        std::numeric_limits<vec_t>::quiet_NaN(), std::numeric_limits<vec_t>::quiet_NaN(), &debug_group, ""};
    setting_vec3 debugvert{this, "debugvert", std::numeric_limits<vec_t>::quiet_NaN(),
        std::numeric_limits<vec_t>::quiet_NaN(), std::numeric_limits<vec_t>::quiet_NaN(), &debug_group, ""};
    setting_bool highlightseams{this, "highlightseams", false, &debug_group, ""};
    setting_soft soft{this, "soft", 0, 0, std::numeric_limits<int32_t>::max(), &postprocessing_group,
        "blurs the lightmap. specify n to blur radius in samples, otherwise auto"};
    setting_string radlights{this, "radlights", "", "\"filename.rad\"", &experimental_group,
        "loads a <surfacename> <r> <g> <b> <intensity> file"};
    setting_int32 lmscale{
        this, "lmscale", 0, &experimental_group, "change lightmap scale, vanilla engines only allow 16"};
    setting_extra extra{
        this, {"extra", "extra4"}, 1, &performance_group, "supersampling; 2x2 (extra) or 4x4 (extra4) respectively"};
    setting_bool novisapprox{
        this, "novisapprox", false, &debug_group, "disable approximate visibility culling of lights"};
    setting_func lit{this, "lit", [&]() { write_litfile |= lightfile::external; }, &output_group, "write .lit file"};
    setting_func lit2{
        this, "lit2", [&]() { write_litfile = lightfile::lit2; }, &experimental_group, "write .lit2 file"};
    setting_func bspxlit{this, "bspxlit", [&]() { write_litfile |= lightfile::bspx; }, &experimental_group,
        "writes rgb data into the bsp itself"};
    setting_func lux{
        this, "lux", [&]() { write_luxfile |= lightfile::external; }, &experimental_group, "write .lux file"};
    setting_func bspxlux{this, "bspxlux", [&]() { write_luxfile |= lightfile::bspx; }, &experimental_group,
        "writes lux data into the bsp itself"};
    setting_func bspxonly{this, "bspxonly",
        [&]() {
            write_litfile = lightfile::bspx;
            write_luxfile = lightfile::bspx;
            novanilla.setValueLocked(true);
        },
        &experimental_group, "writes both rgb and directions data *only* into the bsp itself"};
    setting_func bspx{this, "bspx",
        [&]() {
            write_litfile = lightfile::bspx;
            write_luxfile = lightfile::bspx;
        },
        &experimental_group, "writes both rgb and directions data into the bsp itself"};
    setting_bool litonly{this, "litonly", false, &output_group, "only write .lit file, don't modify BSP"};
    setting_bool nolights{this, "nolights", false, &output_group, "ignore light entities (only sunlight/minlight)"};

    inline void CheckNoDebugModeSet()
    {
        if (debugmode != debugmodes::none) {
            Error("Only one debug mode is allowed at a time");
        }
    }

    setting_func dirtdebug{this, {"dirtdebug", "debugdirt"},
        [&]() {
            CheckNoDebugModeSet();
            debugmode = debugmodes::dirt;
        },
        &debug_group, "only save the AO values to the lightmap"};

    setting_func bouncedebug{this, "bouncedebug",
        [&]() {
            CheckNoDebugModeSet();
            debugmode = debugmodes::bounce;
        },
        &debug_group, "only save bounced lighting to the lightmap"};

    setting_func bouncelightsdebug{this, "bouncelightsdebug",
        [&]() {
            CheckNoDebugModeSet();
            debugmode = debugmodes::bouncelights;
        },
        &debug_group, "only save bounced emitters lighting to the lightmap"};

    setting_func phongdebug{this, "phongdebug",
        [&]() {
            CheckNoDebugModeSet();
            debugmode = debugmodes::phong;
        },
        &debug_group, "only save phong normals to the lightmap"};

    setting_func phongdebug_obj{this, "phongdebug_obj",
        [&]() {
            CheckNoDebugModeSet();
            debugmode = debugmodes::phong_obj;
        },
        &debug_group, "save map as .obj with phonged normals"};

    setting_func debugoccluded{this, "debugoccluded",
        [&]() {
            CheckNoDebugModeSet();
            debugmode = debugmodes::debugoccluded;
        },
        &debug_group, "save light occlusion data to lightmap"};

    setting_func debugneighbours{this, "debugneighbours",
        [&]() {
            CheckNoDebugModeSet();
            debugmode = debugmodes::debugneighbours;
        },
        &debug_group, "save neighboring faces data to lightmap (requires -debugface)"};

    fs::path sourceMap;

    bitflags<lightfile> write_litfile = lightfile::none;
    bitflags<lightfile> write_luxfile = lightfile::none;
    debugmodes debugmode = debugmodes::none;

    virtual void setParameters(int argc, const char **argv) override
    {
        common_settings::setParameters(argc, argv);
        usage = "light compiles lightmap data for BSPs\n\n";
        remainderName = "mapname.bsp";
    }

    virtual void initialize(int argc, const char **argv) override;
    virtual void postinitialize(int argc, const char **argv) override;
};
}; // namespace settings

extern settings::light_settings options;

extern uint8_t *filebase;
extern uint8_t *lit_filebase;
extern uint8_t *lux_filebase;

extern std::vector<surfflags_t> extended_texinfo_flags;

// public functions

void SetGlobalSetting(std::string name, std::string value, bool cmdline);
void FixupGlobalSettings(void);
void GetFileSpace(uint8_t **lightdata, uint8_t **colordata, uint8_t **deluxdata, int size);
void GetFileSpace_PreserveOffsetInBsp(uint8_t **lightdata, uint8_t **colordata, uint8_t **deluxdata, int lightofs);
const modelinfo_t *ModelInfoForModel(const mbsp_t *bsp, int modelnum);
/**
 * returns nullptr for "skip" faces
 */
const modelinfo_t *ModelInfoForFace(const mbsp_t *bsp, int facenum);
const img::texture *Face_Texture(const mbsp_t *bsp, const mface_t *face);
int light_main(int argc, const char **argv);
