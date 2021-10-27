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

#include <cstdint>
#include <cassert>
//#include <cstdio>
#include <iostream>
#include <fstream>
#include <fmt/ostream.h>
#include <fmt/chrono.h>

#include <light/light.hh>
#include <light/phong.hh>
#include <light/bounce.hh>
#include <light/surflight.hh> //mxd
#include <light/imglib.hh> //mxd
#include <light/entities.hh>
#include <light/ltface.hh>

#include <common/polylib.hh>
#include <common/bsputils.hh>

#ifdef HAVE_EMBREE
#include <xmmintrin.h>
//#include <pmmintrin.h>
#endif

#include <memory>
#include <vector>
#include <map>
#include <unordered_map>
#include <set>
#include <algorithm>
#include <mutex>
#include <string>

#include <common/qvec.hh>
#include <common/json.hh>

using namespace std;

globalconfig_t cfg_static{};

bool dirt_in_use = false;

float fadegate = EQUAL_EPSILON;
int softsamples = 0;

float surflight_subdivide = 128.0f;
int sunsamples = 64;
bool scaledonly = false;

bool surflight_dump = false;

static facesup_t *faces_sup; // lit2/bspx stuff

/// start of lightmap data
uint8_t *filebase;
/// offset of start of free space after data (should be kept a multiple of 4)
static int file_p;
/// offset of end of free space for lightmap data
static int file_end;

/// start of litfile data
uint8_t *lit_filebase;
/// offset of start of free space after litfile data (should be kept a multiple of 12)
static int lit_file_p;
/// offset of end of space for litfile data
static int lit_file_end;

/// start of luxfile data
uint8_t *lux_filebase;
/// offset of start of free space after luxfile data (should be kept a multiple of 12)
static int lux_file_p;
/// offset of end of space for luxfile data
static int lux_file_end;

std::vector<modelinfo_t *> modelinfo;
std::vector<const modelinfo_t *> tracelist;
std::vector<const modelinfo_t *> selfshadowlist;
std::vector<const modelinfo_t *> shadowworldonlylist;
std::vector<const modelinfo_t *> switchableshadowlist;

int oversample = 1;
int write_litfile = 0; /* 0 for none, 1 for .lit, 2 for bspx, 3 for both */
int write_luxfile = 0; /* 0 for none, 1 for .lux, 2 for bspx, 3 for both */
bool onlyents = false;
bool novisapprox = false;
bool nolights = false;
bool debug_highlightseams = false;
debugmode_t debugmode = debugmode_none;
bool verbose_log = false;
bool litonly = false;
bool skiplighting = false;
bool write_normals = false;

std::vector<surfflags_t> extended_texinfo_flags;

std::filesystem::path mapfilename;

int dump_facenum = -1;
bool dump_face;
qvec3d dump_face_point{};

int dump_vertnum = -1;
bool dump_vert;
qvec3d dump_vert_point{};

bool arghradcompat = false; // mxd

lockable_setting_t *FindSetting(std::string name)
{
    settingsdict_t sd = cfg_static.settings();
    return sd.findSetting(name);
}

void SetGlobalSetting(std::string name, std::string value, bool cmdline)
{
    settingsdict_t sd = cfg_static.settings();
    sd.setSetting(name, value, cmdline);
}

void FixupGlobalSettings()
{
    static bool once = false;
    Q_assert(!once);
    once = true;

    // NOTE: This is confusing.. Setting "dirt" "1" implies "minlight_dirt" "1"
    // (and sunlight_dir/sunlight2_dirt as well), unless those variables were
    // set by the user to "0".
    //
    // We can't just default "minlight_dirt" to "1" because that would enable
    // dirtmapping by default.

    if (cfg_static.globalDirt.boolValue()) {
        if (!cfg_static.minlightDirt.isChanged()) {
            cfg_static.minlightDirt.setBoolValue(true);
        }
        if (!cfg_static.sunlight_dirt.isChanged()) {
            cfg_static.sunlight_dirt.setFloatValue(1);
        }
        if (!cfg_static.sunlight2_dirt.isChanged()) {
            cfg_static.sunlight2_dirt.setFloatValue(1);
        }
    }
}

static void PrintOptionsSummary(void)
{
    LogPrint("--- OptionsSummary ---\n");

    settingsdict_t sd = cfg_static.settings();

    for (lockable_setting_t *setting : sd.allSettings()) {
        if (setting->isChanged()) {
            LogPrint("    \"{}\" was set to \"{}\" from {}\n", setting->primaryName(), setting->stringValue(),
                setting->sourceString());
        }
    }
}

/*
 * Return space for the lightmap and colourmap at the same time so it can
 * be done in a thread-safe manner.
 *
 * size is the number of greyscale pixels = number of bytes to allocate
 * and return in *lightdata
 */
void GetFileSpace(uint8_t **lightdata, uint8_t **colordata, uint8_t **deluxdata, int size)
{
    ThreadLock();

    *lightdata = filebase + file_p;
    *colordata = lit_filebase + lit_file_p;
    *deluxdata = lux_filebase + lux_file_p;

    // if size isn't a multiple of 4, round up to the next multiple of 4
    if ((size % 4) != 0) {
        size += (4 - (size % 4));
    }

    // increment the next writing offsets, aligning them to 4 uint8_t boundaries (file_p)
    // and 12-uint8_t boundaries (lit_file_p/lux_file_p)
    file_p += size;
    lit_file_p += 3 * size;
    lux_file_p += 3 * size;

    ThreadUnlock();

    if (file_p > file_end)
        FError("overrun");

    if (lit_file_p > lit_file_end)
        FError("overrun");
}

/**
 * Special version of GetFileSpace for when we're relighting a .bsp and can't modify it.
 * In this case the offsets are already known.
 */
void GetFileSpace_PreserveOffsetInBsp(uint8_t **lightdata, uint8_t **colordata, uint8_t **deluxdata, int lightofs)
{
    Q_assert(lightofs >= 0);

    *lightdata = filebase + lightofs;

    if (colordata) {
        *colordata = lit_filebase + (lightofs * 3);
    }

    if (deluxdata) {
        *deluxdata = lux_filebase + (lightofs * 3);
    }

    // NOTE: file_p et. al. are not updated, since we're not dynamically allocating the lightmaps
}

const modelinfo_t *ModelInfoForModel(const mbsp_t *bsp, int modelnum)
{
    return modelinfo.at(modelnum);
}

const modelinfo_t *ModelInfoForFace(const mbsp_t *bsp, int facenum)
{
    int i;
    const dmodelh2_t *model;

    /* Find the correct model offset */
    for (i = 0, model = bsp->dmodels.data(); i < bsp->dmodels.size(); i++, model++) {
        if (facenum < model->firstface)
            continue;
        if (facenum < model->firstface + model->numfaces)
            break;
    }
    if (i == bsp->dmodels.size()) {
        return NULL;
    }
    return modelinfo.at(i);
}

static void *LightThread(void *arg)
{
    const mbsp_t *bsp = (const mbsp_t *)arg;

#ifdef HAVE_EMBREE
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
//    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#endif

    while (1) {
        const int facenum = GetThreadWork();
        if (facenum == -1)
            break;

        mface_t *f = BSP_GetFace(const_cast<mbsp_t *>(bsp), facenum);

        /* Find the correct model offset */
        const modelinfo_t *face_modelinfo = ModelInfoForFace(bsp, facenum);
        if (face_modelinfo == NULL) {
            // ericw -- silenced this warning becasue is causes spam when "skip" faces are used
            // LogPrint("warning: no model has face {}\n", facenum);
            continue;
        }

        if (!faces_sup)
            LightFace(bsp, f, nullptr, cfg_static);
        else if (scaledonly) {
            f->lightofs = -1;
            f->styles[0] = 255;
            LightFace(bsp, f, faces_sup + facenum, cfg_static);
        } else if (faces_sup[facenum].lmscale == face_modelinfo->lightmapscale) {
            LightFace(bsp, f, nullptr, cfg_static);
            faces_sup[facenum].lightofs = f->lightofs;
            for (int i = 0; i < MAXLIGHTMAPS; i++)
                faces_sup[facenum].styles[i] = f->styles[i];
        } else {
            LightFace(bsp, f, nullptr, cfg_static);
            LightFace(bsp, f, faces_sup + facenum, cfg_static);
        }
    }

    return NULL;
}

static void FindModelInfo(const mbsp_t *bsp, const char *lmscaleoverride)
{
    Q_assert(modelinfo.size() == 0);
    Q_assert(tracelist.size() == 0);
    Q_assert(selfshadowlist.size() == 0);
    Q_assert(shadowworldonlylist.size() == 0);
    Q_assert(switchableshadowlist.size() == 0);

    if (!bsp->dmodels.size()) {
        FError("Corrupt .BSP: bsp->nummodels is 0!");
    }

    if (lmscaleoverride)
        SetWorldKeyValue("_lightmap_scale", lmscaleoverride);

    float lightmapscale = atoi(WorldValueForKey("_lightmap_scale").c_str());
    if (!lightmapscale)
        lightmapscale = 16; /* the default */
    if (lightmapscale <= 0)
        FError("lightmap scale is 0 or negative\n");
    if (lmscaleoverride || lightmapscale != 16)
        LogPrint("Forcing lightmap scale of {}qu\n", lightmapscale);
    /*I'm going to do this check in the hopes that there's a benefit to cheaper scaling in engines (especially software
     * ones that might be able to just do some mip hacks). This tool doesn't really care.*/
    {
        int i;
        for (i = 1; i < lightmapscale;) {
            i++;
        }
        if (i != lightmapscale) {
            LogPrint("WARNING: lightmap scale is not a power of 2\n");
        }
    }

    /* The world always casts shadows */
    modelinfo_t *world = new modelinfo_t{bsp, &bsp->dmodels[0], lightmapscale};
    world->shadow.setFloatValue(1.0f); /* world always casts shadows */
    world->phong_angle = cfg_static.phongangle;
    modelinfo.push_back(world);
    tracelist.push_back(world);

    for (int i = 1; i < bsp->dmodels.size(); i++) {
        modelinfo_t *info = new modelinfo_t{bsp, &bsp->dmodels[i], lightmapscale};
        modelinfo.push_back(info);

        /* Find the entity for the model */
        std::string modelname = fmt::format("*{}", i);

        const entdict_t *entdict = FindEntDictWithKeyPair("model", modelname);
        if (entdict == nullptr)
            FError("Couldn't find entity for model {}.\n", modelname);

        // apply settings
        info->settings().setSettings(*entdict, false);

        /* Check if this model will cast shadows (shadow => shadowself) */
        if (info->switchableshadow.boolValue()) {
            Q_assert(info->switchshadstyle.intValue() != 0);
            switchableshadowlist.push_back(info);
        } else if (info->shadow.boolValue()) {
            tracelist.push_back(info);
        } else if (info->shadowself.boolValue()) {
            selfshadowlist.push_back(info);
        } else if (info->shadowworldonly.boolValue()) {
            shadowworldonlylist.push_back(info);
        }

        /* Set up the offset for rotate_* entities */
        info->offset = EntDict_VectorForKey(*entdict, "origin");
    }

    Q_assert(modelinfo.size() == bsp->dmodels.size());
}

/*
 * =============
 *  LightWorld
 * =============
 */
static void LightWorld(bspdata_t *bspdata, bool forcedscale)
{
    LogPrint("--- LightWorld ---\n");

    mbsp_t &bsp = std::get<mbsp_t>(bspdata->bsp);

    delete[] filebase;
    delete[] lit_filebase;
    delete[] lux_filebase;

    /* greyscale data stored in a separate buffer */
    filebase = new uint8_t[MAX_MAP_LIGHTING]{};
    if (!filebase)
        FError("allocation of {} bytes failed.", MAX_MAP_LIGHTING);
    file_p = 0;
    file_end = MAX_MAP_LIGHTING;

    /* litfile data stored in a separate buffer */
    lit_filebase = new uint8_t[MAX_MAP_LIGHTING * 3]{};
    if (!lit_filebase)
        FError("allocation of {} bytes failed.", MAX_MAP_LIGHTING * 3);
    lit_file_p = 0;
    lit_file_end = (MAX_MAP_LIGHTING * 3);

    /* lux data stored in a separate buffer */
    lux_filebase = new uint8_t[MAX_MAP_LIGHTING * 3]{};
    if (!lux_filebase)
        FError("allocation of {} bytes failed.", MAX_MAP_LIGHTING * 3);
    lux_file_p = 0;
    lux_file_end = (MAX_MAP_LIGHTING * 3);

    if (forcedscale)
        bspdata->bspx.entries.erase("LMSHIFT");

    auto lmshift_lump = bspdata->bspx.entries.find("LMSHIFT");

    if (lmshift_lump == bspdata->bspx.entries.end() && write_litfile != ~0)
        faces_sup = nullptr; // no scales, no lit2
    else { // we have scales or lit2 output. yay...
        faces_sup = new facesup_t[bsp.dfaces.size()]{};

        if (lmshift_lump != bspdata->bspx.entries.end()) {
            for (int i = 0; i < bsp.dfaces.size(); i++)
                faces_sup[i].lmscale = 1 << reinterpret_cast<const char *>(lmshift_lump->second.lumpdata.get())[i];
        } else {
            for (int i = 0; i < bsp.dfaces.size(); i++)
                faces_sup[i].lmscale = modelinfo.at(0)->lightmapscale;
        }
    }

    CalculateVertexNormals(&bsp);

    const bool bouncerequired =
        cfg_static.bounce.boolValue() &&
        (debugmode == debugmode_none || debugmode == debugmode_bounce || debugmode == debugmode_bouncelights); // mxd
    const bool isQuake2map = bsp.loadversion->game->id == GAME_QUAKE_II; // mxd

    if ((bouncerequired || isQuake2map) && !skiplighting) {
        MakeTextureColors(&bsp);
        if (isQuake2map)
            MakeSurfaceLights(cfg_static, &bsp);
        if (bouncerequired)
            MakeBounceLights(cfg_static, &bsp);
    }

#if 0
    lightbatchthread_info_t info;
    info.all_batches = MakeLightingBatches(bsp);
    info.all_contribFaces = MakeContributingFaces(bsp);
    info.bsp = bsp;
    RunThreadsOn(0, info.all_batches.size(), LightBatchThread, &info);
#else
    LogPrint("--- LightThread ---\n"); // mxd
    RunThreadsOn(0, bsp.dfaces.size(), LightThread, &bsp);
#endif

    if ((bouncerequired || isQuake2map) && !skiplighting) { // mxd. Print some extra stats...
        LogPrint("Indirect lights: {} bounce lights, {} surface lights ({} light points) in use.\n",
            BounceLights().size(), SurfaceLights().size(), TotalSurfacelightPoints());
    }

    LogPrint("Lighting Completed.\n\n");

    // Transfer greyscale lightmap (or color lightmap for Q2/HL) to the bsp and update lightdatasize
    if (!litonly) {
        if (bsp.loadversion->game->has_rgb_lightmap) {
            bsp.dlightdata.resize(lit_file_p);
            memcpy(bsp.dlightdata.data(), lit_filebase, bsp.dlightdata.size());
        } else {
            bsp.dlightdata.resize(file_p);
            memcpy(bsp.dlightdata.data(), filebase, bsp.dlightdata.size());
        }
    } else {
        // NOTE: bsp.lightdatasize is already valid in the -litonly case
    }
    LogPrint("lightdatasize: {}\n", bsp.dlightdata.size());

    // kill this stuff if its somehow found.
    bspdata->bspx.entries.erase("LMSTYLE");
    bspdata->bspx.entries.erase("LMOFFSET");

    if (faces_sup) {
        uint8_t *styles = new uint8_t[4 * bsp.dfaces.size()];
        int32_t *offsets = new int32_t[bsp.dfaces.size()];
        for (int i = 0; i < bsp.dfaces.size(); i++) {
            offsets[i] = faces_sup[i].lightofs;
            for (int j = 0; j < MAXLIGHTMAPS; j++)
                styles[i * 4 + j] = faces_sup[i].styles[j];
        }
        bspdata->bspx.transfer("LMSTYLE", styles, sizeof(*styles) * 4 * bsp.dfaces.size());
        bspdata->bspx.transfer("LMOFFSET", (uint8_t *&)offsets, sizeof(*offsets) * bsp.dfaces.size());
    }
}

static void LoadExtendedTexinfoFlags(const std::filesystem::path &sourcefilename, const mbsp_t *bsp)
{
    // always create the zero'ed array
    extended_texinfo_flags.resize(bsp->texinfo.size());

    std::filesystem::path filename(sourcefilename);
    filename.replace_extension("texinfo.json");

    std::ifstream texinfofile(filename, std::ios_base::in | std::ios_base::binary);

    if (!texinfofile)
        return;

    LogPrint("Loading extended texinfo flags from {}...\n", filename);

    json j;

    texinfofile >> j;

    for (auto it = j.begin(); it != j.end(); ++it) {
        size_t index = std::stoull(it.key());

        if (index >= bsp->texinfo.size()) {
            LogPrint("WARNING: Extended texinfo flags in {} does not match bsp, ignoring\n", filename);
            memset(extended_texinfo_flags.data(), 0, bsp->texinfo.size() * sizeof(surfflags_t));
            return;
        }

        auto &val = it.value();
        auto &flags = extended_texinfo_flags[index];
        
        if (val.contains("is_skip")) {
            flags.is_skip = val.at("is_skip").get<bool>();
        }
        if (val.contains("is_hint")) {
            flags.is_hint = val.at("is_hint").get<bool>();
        }
        if (val.contains("no_dirt")) {
            flags.no_dirt = val.at("no_dirt").get<bool>();
        }
        if (val.contains("no_shadow")) {
            flags.no_shadow = val.at("no_shadow").get<bool>();
        }
        if (val.contains("no_bounce")) {
            flags.no_bounce = val.at("no_bounce").get<bool>();
        }
        if (val.contains("no_minlight")) {
            flags.no_minlight = val.at("no_minlight").get<bool>();
        }
        if (val.contains("no_expand")) {
            flags.no_expand = val.at("no_expand").get<bool>();
        }
        if (val.contains("light_ignore")) {
            flags.light_ignore = val.at("light_ignore").get<bool>();
        }
        if (val.contains("phong_angle")) {
            flags.phong_angle = val.at("phong_angle").get<vec_t>();
        }
        if (val.contains("phong_angle_concave")) {
            flags.phong_angle_concave = val.at("phong_angle_concave").get<vec_t>();
        }
        if (val.contains("minlight")) {
            flags.minlight = val.at("minlight").get<vec_t>();
        }
        if (val.contains("minlight_color")) {
            flags.minlight_color = val.at("minlight_color").get<qvec3b>();
        }
        if (val.contains("light_alpha")) {
            flags.light_alpha = val.at("light_alpha").get<vec_t>();
        }
    }
}

// obj

static void ExportObjFace(std::ofstream &f, const mbsp_t *bsp, const mface_t *face, int *vertcount)
{
    // export the vertices and uvs
    for (int i = 0; i < face->numedges; i++) {
        const int vertnum = Face_VertexAtIndex(bsp, face, i);
        const qvec3f normal = GetSurfaceVertexNormal(bsp, face, i).normal;
        const qvec3f &pos = bsp->dvertexes[vertnum];
        fmt::print(f, "v {:.9} {:.9} {:.9}\n", pos[0], pos[1], pos[2]);
        fmt::print(f, "vn {:.9} {:.9} {:.9}\n", normal[0], normal[1], normal[2]);
    }

    f << "f";
    for (int i = 0; i < face->numedges; i++) {
        // .obj vertexes start from 1
        // .obj faces are CCW, quake is CW, so reverse the order
        const int vertindex = *vertcount + (face->numedges - 1 - i) + 1;
        fmt::print(f, " {}//{}", vertindex, vertindex);
    }
    f << '\n';

    *vertcount += face->numedges;
}

static void ExportObj(const std::filesystem::path &filename, const mbsp_t *bsp)
{
    std::ofstream objfile(filename);
    int vertcount = 0;

    const int start = bsp->dmodels[0].firstface;
    const int end = bsp->dmodels[0].firstface + bsp->dmodels[0].numfaces;

    for (int i = start; i < end; i++) {
        ExportObjFace(objfile, bsp, BSP_GetFace(bsp, i), &vertcount);
    }

    LogPrint("Wrote {}\n", filename);
}

// obj

static void CheckNoDebugModeSet()
{
    if (debugmode != debugmode_none) {
        Error("Only one debug mode is allowed at a time");
    }
}

// returns the face with a centroid nearest the given point.
static const mface_t *Face_NearestCentroid(const mbsp_t *bsp, const qvec3f &point)
{
    const mface_t *nearest_face = NULL;
    float nearest_dist = FLT_MAX;

    for (int i = 0; i < bsp->dfaces.size(); i++) {
        const mface_t *f = BSP_GetFace(bsp, i);

        const qvec3f fc = Face_Centroid(bsp, f);

        const qvec3f distvec = fc - point;
        const float dist = qv::length(distvec);

        if (dist < nearest_dist) {
            nearest_dist = dist;
            nearest_face = f;
        }
    }

    return nearest_face;
}

static void FindDebugFace(const mbsp_t *bsp)
{
    if (!dump_face)
        return;

    const mface_t *f = Face_NearestCentroid(bsp, dump_face_point);
    if (f == NULL)
        FError("f == NULL\n");

    const int facenum = f - bsp->dfaces.data();

    dump_facenum = facenum;

    const modelinfo_t *mi = ModelInfoForFace(bsp, facenum);
    const int modelnum = mi ? (mi->model - bsp->dmodels.data()) : -1;

    const char *texname = Face_TextureName(bsp, f);
    FLogPrint("dumping face {} (texture '{}' model {})\n", facenum, texname, modelnum);
}

// returns the vert nearest the given point
// FIXME: qv distance double
static int Vertex_NearestPoint(const mbsp_t *bsp, const qvec3f &point)
{
    int nearest_vert = -1;
    float nearest_dist = std::numeric_limits<float>::infinity();

    for (int i = 0; i < bsp->dvertexes.size(); i++) {
        const qvec3f &vertex = bsp->dvertexes[i];

        float dist = qv::distance(vertex, point);

        if (dist < nearest_dist) {
            nearest_dist = dist;
            nearest_vert = i;
        }
    }

    return nearest_vert;
}

static void FindDebugVert(const mbsp_t *bsp)
{
    if (!dump_vert)
        return;

    int v = Vertex_NearestPoint(bsp, dump_vert_point);

    FLogPrint("dumping vert {} at {}\n", v, bsp->dvertexes[v]);

    dump_vertnum = v;
}

static void SetLitNeeded()
{
    if (!write_litfile) {
        if (scaledonly) {
            write_litfile = 2;
            LogPrint("Colored light entities/settings detected: "
                     "bspxlit output enabled.\n");
        } else {
            write_litfile = 1;
            LogPrint("Colored light entities/settings detected: "
                     ".lit output enabled.\n");
        }
    }
}

static void CheckLitNeeded(const globalconfig_t &cfg)
{
    // check lights
    for (const auto &light : GetLights()) {
        if (!qv::epsilonEqual(vec3_white, light.color.vec3Value(), EQUAL_EPSILON) ||
            light.projectedmip != nullptr) { // mxd. Projected mips could also use .lit output
            SetLitNeeded();
            return;
        }
    }

    // check global settings
    if (cfg.bouncecolorscale.floatValue() != 0 ||
        !qv::epsilonEqual(cfg.minlight_color.vec3Value(), vec3_white, EQUAL_EPSILON) ||
        !qv::epsilonEqual(cfg.sunlight_color.vec3Value(), vec3_white, EQUAL_EPSILON) ||
        !qv::epsilonEqual(cfg.sun2_color.vec3Value(), vec3_white, EQUAL_EPSILON) ||
        !qv::epsilonEqual(cfg.sunlight2_color.vec3Value(), vec3_white, EQUAL_EPSILON) ||
        !qv::epsilonEqual(cfg.sunlight3_color.vec3Value(), vec3_white, EQUAL_EPSILON)) {
        SetLitNeeded();
        return;
    }
}

#if 0
static void PrintLight(const light_t &light)
{
    bool first = true;

    auto settings = const_cast<light_t &>(light).settings();
    for (const auto &setting : settings.allSettings()) {
        if (!setting->isChanged())
            continue; // don't spam default values

        // print separator
        if (!first) {
            LogPrint("; ");
        } else {
            first = false;
        }

        LogPrint("{}={}", setting->primaryName(), setting->stringValue());
    }
    LogPrint("\n");
}

static void PrintLights(void)
{
    LogPrint("===PrintLights===\n");

    for (const auto &light : GetLights()) {
        PrintLight(light);
    }
}
#endif

static void PrintUsage()
{
    printf("usage: light [options] mapname.bsp\n"
           "\n"
           "Performance options:\n"
           "  -threads n          set the number of threads\n"
           "  -extra              2x supersampling\n"
           "  -extra4             4x supersampling, slowest, use for final compile\n"
           "  -gate n             cutoff lights at this brightness level\n"
           "  -sunsamples n       set samples for _sunlight2, default 64\n"
           "  -surflight_subdivide  surface light subdivision size\n"
           "\n"
           "Output format options:\n"
           "  -lit                write .lit file\n"
           "  -onlyents           only update entities\n"
           "\n"
           "Postprocessing options:\n"
           "  -soft [n]           blurs the lightmap, n=blur radius in samples\n"
           "\n"
           "Debug modes:\n"
           "  -dirtdebug          only save the AO values to the lightmap\n"
           "  -phongdebug         only save the normals to the lightmap\n"
           "  -bouncedebug        only save bounced lighting to the lightmap\n"
           "  -surflight_dump     dump surface lights to a .map file\n"
           "  -novisapprox        disable approximate visibility culling of lights\n"
           "\n"
           "Experimental options:\n"
           "  -lit2               write .lit2 file\n"
           "  -lmscale n          change lightmap scale, vanilla engines only allow 16\n"
           "  -lux                write .lux file\n"
           "  -bspxlit            writes rgb data into the bsp itself\n"
           "  -bspx               writes both rgb and directions data into the bsp itself\n"
           "  -novanilla          implies -bspxlit. don't write vanilla lighting\n"
           "  -radlights filename.rad loads a <surfacename> <r> <g> <b> <intensity> file\n"
           "  -wrnormals          write normals into the bsp itself\n");

    printf("\n");
    printf("Overridable worldspawn keys:\n");
    settingsdict_t dict = cfg_static.settings();
    for (const auto &s : dict.allSettings()) {
        printf("  ");
        for (int i = 0; i < s->names().size(); i++) {
            const auto &name = s->names().at(i);

            fmt::print("-{} ", name);

            if (dynamic_cast<lockable_vec_t *>(s)) {
                printf("[n] ");
            } else if (dynamic_cast<lockable_bool_t *>(s)) {
                printf("[0,1] ");
            } else if (dynamic_cast<lockable_vec3_t *>(s)) {
                printf("[n n n] ");
            } else if (dynamic_cast<lockable_string_t *>(s)) {
                printf("\"str\" ");
            } else {
                Q_assert_unreachable();
            }

            if ((i + 1) < s->names().size()) {
                printf("| ");
            }
        }
        printf("\n");
    }
}

static bool ParseVec3Optional(qvec3d &vec3_out, int *i_inout, int argc, const char **argv)
{
    if ((*i_inout + 3) < argc) {
        const int start = (*i_inout + 1);
        const int end = (*i_inout + 3);

        // validate that there are 3 numbers
        for (int j = start; j <= end; j++) {
            if (argv[j][0] == '-' && isdigit(argv[j][1])) {
                continue; // accept '-' followed by a digit for negative numbers
            }

            // otherwise, reject if the first character is not a digit
            if (!isdigit(argv[j][0])) {
                return false;
            }
        }

        vec3_out[0] = atof(argv[++(*i_inout)]);
        vec3_out[1] = atof(argv[++(*i_inout)]);
        vec3_out[2] = atof(argv[++(*i_inout)]);
        return true;
    } else {
        return false;
    }
}

static bool ParseVecOptional(vec_t *result, int *i_inout, int argc, const char **argv)
{
    if ((*i_inout + 1) < argc) {
        if (!isdigit(argv[*i_inout + 1][0])) {
            return false;
        }
        *result = atof(argv[++(*i_inout)]);
        return true;
    } else {
        return false;
    }
}

static bool ParseIntOptional(int *result, int *i_inout, int argc, const char **argv)
{
    if ((*i_inout + 1) < argc) {
        if (!isdigit(argv[*i_inout + 1][0])) {
            return false;
        }
        *result = atoi(argv[++(*i_inout)]);
        return true;
    } else {
        return false;
    }
}

#if 0
static const char *ParseStringOptional(int *i_inout, int argc, const char **argv)
{
    if ((*i_inout + 1) < argc) {
        return argv[++(*i_inout)];
    } else {
        return NULL;
    }
}
#endif

static void ParseVec3(qvec3d &vec3_out, int *i_inout, int argc, const char **argv)
{
    if (!ParseVec3Optional(vec3_out, i_inout, argc, argv)) {
        Error("{} requires 3 numberic arguments\n", argv[*i_inout]);
    }
}

static vec_t ParseVec(int *i_inout, int argc, const char **argv)
{
    vec_t result = 0;
    if (!ParseVecOptional(&result, i_inout, argc, argv)) {
        Error("{} requires 1 numeric argument\n", argv[*i_inout]);
        return 0;
    }
    return result;
}

static int ParseInt(int *i_inout, int argc, const char **argv)
{
    int result = 0;
    if (!ParseIntOptional(&result, i_inout, argc, argv)) {
        Error("{} requires 1 integer argument\n", argv[*i_inout]);
        return 0;
    }
    return result;
}

#if 0
static const char *ParseString(int *i_inout, int argc, const char **argv)
{
    const char *result = NULL;
    if (!(result = ParseStringOptional(i_inout, argc, argv))) {
        Error("{} requires 1 string argument\n", argv[*i_inout]);
    }
    return result;
}
#endif

static inline void WriteNormals(const mbsp_t &bsp, bspdata_t &bspdata)
{
    std::set<qvec3f> unique_normals;
    size_t num_normals = 0;

    for (auto &face : bsp.dfaces) {
        auto &cache = FaceCacheForFNum(&face - bsp.dfaces.data());
        for (auto &normals : cache.normals()) {
            unique_normals.insert(qv::Snap(normals.normal));
            unique_normals.insert(qv::Snap(normals.tangent));
            unique_normals.insert(qv::Snap(normals.bitangent));
            num_normals += 3;
        }
    }

    size_t data_size = sizeof(uint32_t) + (sizeof(qvec3f) * unique_normals.size()) + (sizeof(uint32_t) * num_normals);
    uint8_t *data = new uint8_t[data_size];
    memstream stream(data, data_size);

    stream << endianness<std::endian::little>;
    stream <= numeric_cast<uint32_t>(unique_normals.size());

    std::map<qvec3f, size_t> mapped_normals;

    for (auto &n : unique_normals) {
        stream <= std::tie(n[0], n[1], n[2]);
        mapped_normals.emplace(n, mapped_normals.size());
    }

    for (auto &face : bsp.dfaces) {
        auto &cache = FaceCacheForFNum(&face - bsp.dfaces.data());

        for (auto &n : cache.normals()) {
            stream <= numeric_cast<uint32_t>(mapped_normals[qv::Snap(n.normal)]);
            stream <= numeric_cast<uint32_t>(mapped_normals[qv::Snap(n.tangent)]);
            stream <= numeric_cast<uint32_t>(mapped_normals[qv::Snap(n.bitangent)]);
        }
    }

    Q_assert(stream.tellp() == data_size);

    if (verbose_log) {
        LogPrint("Compressed {} normals down to {}\n", num_normals, unique_normals.size());
    }

    bspdata.bspx.transfer("FACENORMALS", data, data_size);

    ofstream obj("test.obj");

    size_t index_id = 1;

    for (auto &face : bsp.dfaces) {
        auto &cache = FaceCacheForFNum(&face - bsp.dfaces.data());
        /*bool keep = true;
        
        for (size_t i = 0; i < cache.points().size(); i++) {
            auto &pt = cache.points()[i];

            if (qv::distance(pt, { -208, 6, 21 }) > 256) {
                keep = false;
                break;
            }
        }

        if (!keep) {
            continue;
        }*/

        for (size_t i = 0; i < cache.points().size(); i++) {
            auto &pt = cache.points()[i];
            auto &n = cache.normals()[i];
            
            fmt::print(obj, "v {}\n", pt);
            fmt::print(obj, "vn {}\n", n.normal);
        }
        
        for (size_t i = 1; i < cache.points().size() - 1; i++) {
            size_t n1 = 0;
            size_t n2 = i;
            size_t n3 = (i + 1) % cache.points().size();

            fmt::print(obj, "f {0}//{0} {1}//{1} {2}//{2}\n", index_id + n1, index_id + n2, index_id + n3);
        }

        index_id += cache.points().size();
    }
}

/*
 * ==================
 * main
 * light modelfile
 * ==================
 */
int light_main(int argc, const char **argv)
{
    bspdata_t bspdata;
    const bspversion_t *loadversion;
    int i;
    const char *lmscaleoverride = NULL;

    InitLog("light.log");
    LogPrint("---- light / ericw-tools " stringify(ERICWTOOLS_VERSION) " ----\n");

    LowerProcessPriority();
    numthreads = GetDefaultThreads();

    globalconfig_t &cfg = cfg_static;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-threads")) {
            numthreads = ParseInt(&i, argc, argv);
        } else if (!strcmp(argv[i], "-extra")) {
            oversample = 2;
            LogPrint("extra 2x2 sampling enabled\n");
        } else if (!strcmp(argv[i], "-extra4")) {
            oversample = 4;
            LogPrint("extra 4x4 sampling enabled\n");
        } else if (!strcmp(argv[i], "-gate")) {
            fadegate = ParseVec(&i, argc, argv);
            if (fadegate > 1) {
                LogPrint("WARNING: -gate value greater than 1 may cause artifacts\n");
            }
        } else if (!strcmp(argv[i], "-lit")) {
            write_litfile |= 1;
        } else if (!strcmp(argv[i], "-lit2")) {
            write_litfile = ~0;
        } else if (!strcmp(argv[i], "-lux")) {
            write_luxfile |= 1;
        } else if (!strcmp(argv[i], "-bspxlit")) {
            write_litfile |= 2;
        } else if (!strcmp(argv[i], "-bspxlux")) {
            write_luxfile |= 2;
        } else if (!strcmp(argv[i], "-bspxonly")) {
            write_litfile = 2;
            write_luxfile = 2;
            scaledonly = true;
        } else if (!strcmp(argv[i], "-bspx")) {
            write_litfile |= 2;
            write_luxfile |= 2;
        } else if (!strcmp(argv[i], "-novanilla")) {
            scaledonly = true;
        } else if (!strcmp(argv[i], "-radlights")) {
            if (!ParseLightsFile(argv[++i]))
                LogPrint("Unable to read surfacelights file {}\n", argv[i]);
        } else if (!strcmp(argv[i], "-lmscale")) {
            lmscaleoverride = argv[++i];
        } else if (!strcmp(argv[i], "-soft")) {
            if ((i + 1) < argc && isdigit(argv[i + 1][0]))
                softsamples = ParseInt(&i, argc, argv);
            else
                softsamples = -1; /* auto, based on oversampling */
        } else if (!strcmp(argv[i], "-dirtdebug") || !strcmp(argv[i], "-debugdirt")) {
            CheckNoDebugModeSet();

            cfg.globalDirt.setBoolValueLocked(true);
            debugmode = debugmode_dirt;
            LogPrint("Dirtmap debugging enabled\n");
        } else if (!strcmp(argv[i], "-bouncedebug")) {
            CheckNoDebugModeSet();
            cfg.bounce.setBoolValueLocked(true);
            debugmode = debugmode_bounce;
            LogPrint("Bounce debugging mode enabled on command line\n");
        } else if (!strcmp(argv[i], "-bouncelightsdebug")) {
            CheckNoDebugModeSet();
            cfg.bounce.setBoolValueLocked(true);
            debugmode = debugmode_bouncelights;
            LogPrint("Bounce emitters debugging mode enabled on command line\n");
        } else if (!strcmp(argv[i], "-surflight_subdivide")) {
            surflight_subdivide = ParseVec(&i, argc, argv);
            surflight_subdivide = min(max(surflight_subdivide, 64.0f), 2048.0f);
            LogPrint("Using surface light subdivision size of {}\n", surflight_subdivide);
        } else if (!strcmp(argv[i], "-surflight_dump")) {
            surflight_dump = true;
        } else if (!strcmp(argv[i], "-sunsamples")) {
            sunsamples = ParseInt(&i, argc, argv);
            sunsamples = min(max(sunsamples, 8), 2048);
            LogPrint("Using sunsamples of {}\n", sunsamples);
        } else if (!strcmp(argv[i], "-onlyents")) {
            onlyents = true;
            LogPrint("Onlyents mode enabled\n");
        } else if (!strcmp(argv[i], "-phongdebug")) {
            CheckNoDebugModeSet();
            debugmode = debugmode_phong;
            write_litfile |= 1;
            LogPrint("Phong shading debug mode enabled\n");
        } else if (!strcmp(argv[i], "-phongdebug_obj")) {
            CheckNoDebugModeSet();
            debugmode = debugmode_phong_obj;
            LogPrint("Phong shading debug mode (.obj export) enabled\n");
        } else if (!strcmp(argv[i], "-novisapprox")) {
            novisapprox = true;
            LogPrint("Skipping approximate light visibility\n");
        } else if (!strcmp(argv[i], "-nolights")) {
            nolights = true;
            LogPrint("Skipping all light entities (sunlight / minlight only)\n");
        } else if (!strcmp(argv[i], "-debugface")) {
            ParseVec3(dump_face_point, &i, argc, argv);
            dump_face = true;
        } else if (!strcmp(argv[i], "-debugvert")) {
            ParseVec3(dump_vert_point, &i, argc, argv);
            dump_vert = true;
        } else if (!strcmp(argv[i], "-debugoccluded")) {
            CheckNoDebugModeSet();
            debugmode = debugmode_debugoccluded;
        } else if (!strcmp(argv[i], "-debugneighbours")) {
            ParseVec3(dump_face_point, &i, argc, argv);
            dump_face = true;

            CheckNoDebugModeSet();
            debugmode = debugmode_debugneighbours;
        } else if (!strcmp(argv[i], "-highlightseams")) {
            LogPrint("Highlighting lightmap seams\n");
            debug_highlightseams = true;
        } else if (!strcmp(argv[i], "-arghradcompat")) { // mxd
            LogPrint("Arghrad entity keys conversion enabled\n");
            arghradcompat = true;
        } else if (!strcmp(argv[i], "-litonly")) {
            LogPrint("-litonly specified; .bsp file will not be modified\n");
            litonly = true;
            write_litfile |= 1;
        } else if (!strcmp(argv[i], "-nolighting")) {
            LogPrint("-nolighting specified; .bsp file will not calculate lightmap data\n");
            skiplighting = true;
        } else if (!strcmp(argv[i], "-wrnormals")) {
            write_normals = true;
        } else if (!strcmp(argv[i], "-verbose") || !strcmp(argv[i], "-v")) { // Quark always passes -v
            verbose_log = true;
        } else if (!strcmp(argv[i], "-help")) {
            PrintUsage();
            exit(0);
        } else if (argv[i][0] == '-') {
            // hand over to the settings system
            std::string settingname{&argv[i][1]};
            lockable_setting_t *setting = FindSetting(settingname);
            if (setting == nullptr) {
                Error("Unknown option \"-{}\"", settingname);
                PrintUsage();
            }

            if (lockable_bool_t *boolsetting = dynamic_cast<lockable_bool_t *>(setting)) {
                vec_t v;
                if (ParseVecOptional(&v, &i, argc, argv)) {
                    boolsetting->setStringValue(std::to_string(v), true);
                } else {
                    boolsetting->setBoolValueLocked(true);
                }
            } else if (lockable_vec_t *vecsetting = dynamic_cast<lockable_vec_t *>(setting)) {
                vecsetting->setFloatValueLocked(ParseVec(&i, argc, argv));
            } else if (lockable_vec3_t *vec3setting = dynamic_cast<lockable_vec3_t *>(setting)) {
                qvec3d temp;
                ParseVec3(temp, &i, argc, argv);
                vec3setting->setVec3ValueLocked(temp);
            } else {
                Error("Internal error");
            }
        } else {
            break;
        }
    }

    if (i != argc - 1) {
        PrintUsage();
        exit(1);
    }

    if (debugmode != debugmode_none) {
        write_litfile |= 1;
    }

    if (numthreads > 1)
        LogPrint("running with {} threads\n", numthreads);

    if (write_litfile == ~0)
        LogPrint("generating lit2 output only.\n");
    else {
        if (write_litfile & 1)
            LogPrint(".lit colored light output requested on command line.\n");
        if (write_litfile & 2)
            LogPrint("BSPX colored light output requested on command line.\n");
        if (write_luxfile & 1)
            LogPrint(".lux light directions output requested on command line.\n");
        if (write_luxfile & 2)
            LogPrint("BSPX light directions output requested on command line.\n");
    }

    if (softsamples == -1) {
        switch (oversample) {
            case 2: softsamples = 1; break;
            case 4: softsamples = 2; break;
            default: softsamples = 0; break;
        }
    }

    auto start = I_FloatTime();

    std::filesystem::path source(argv[i]);
    mapfilename = source;

    // delete previous litfile
    if (!onlyents) {
        source.replace_extension("lit");
        remove(source);
    }

    {
        source.replace_extension("rad");
        if (source != "lights.rad")
            ParseLightsFile("lights.rad"); // generic/default name
        ParseLightsFile(source); // map-specific file name
    }

    source.replace_extension("bsp");
    LoadBSPFile(source, &bspdata);

    loadversion = bspdata.version;
    ConvertBSPFormat(&bspdata, &bspver_generic);

    mbsp_t &bsp = std::get<mbsp_t>(bspdata.bsp);

    // mxd. Use 1.0 rangescale as a default to better match with qrad3/arghrad
    if ((loadversion->game->id == GAME_QUAKE_II) && !cfg.rangescale.isChanged()) {
        const auto rs = new lockable_vec_t(cfg.rangescale.primaryName(), 1.0f, 0.0f, 100.0f);
        cfg.rangescale = *rs; // Gross hacks to avoid displaying this in OptionsSummary...
    }

    // mxd. Load or convert textures...
    SetQdirFromPath(bspdata.loadversion->game->base_dir, source);
    LoadPalette(&bspdata);
    LoadOrConvertTextures(&bsp);

    LoadExtendedTexinfoFlags(source, &bsp);
    LoadEntities(cfg, &bsp);

    PrintOptionsSummary();

    FindModelInfo(&bsp, lmscaleoverride);

    FindDebugFace(&bsp);
    FindDebugVert(&bsp);

    MakeTnodes(&bsp);

    if (debugmode == debugmode_phong_obj) {
        CalculateVertexNormals(&bsp);
        source.replace_extension("obj");
        ExportObj(source, &bsp);

        CloseLog();
        return 0;
    }

    SetupLights(cfg, &bsp);

    // PrintLights();

    if (!onlyents) {
        if (!loadversion->game->has_rgb_lightmap) {
            CheckLitNeeded(cfg);
        }
        SetupDirt(cfg);

        LightWorld(&bspdata, !!lmscaleoverride);

        // invalidate normals
        bspdata.bspx.entries.erase("FACENORMALS");

        if (write_normals) {
            WriteNormals(bsp, bspdata);
        }

        /*invalidate any bspx lighting info early*/
        bspdata.bspx.entries.erase("RGBLIGHTING");
        bspdata.bspx.entries.erase("LIGHTINGDIR");

        if (write_litfile == ~0) {
            WriteLitFile(&bsp, faces_sup, source, 2);
            return 0; // run away before any files are written
        } else {
            /*fixme: add a new per-surface offset+lmscale lump for compat/versitility?*/
            if (write_litfile & 1)
                WriteLitFile(&bsp, faces_sup, source, LIT_VERSION);
            if (write_litfile & 2)
                bspdata.bspx.transfer("RGBLIGHTING", lit_filebase, bsp.dlightdata.size() * 3);
            if (write_luxfile & 1)
                WriteLuxFile(&bsp, source, LIT_VERSION);
            if (write_luxfile & 2)
                bspdata.bspx.transfer("LIGHTINGDIR", lux_filebase, bsp.dlightdata.size() * 3);
        }
    }

    /* -novanilla + internal lighting = no grey lightmap */
    if (scaledonly && (write_litfile & 2))
        bsp.dlightdata.clear();

#if 0
    ExportObj(source, bsp);
#endif

    WriteEntitiesToString(cfg, &bsp);
    /* Convert data format back if necessary */
    ConvertBSPFormat(&bspdata, loadversion);

    if (!litonly) {
        WriteBSPFile(source, &bspdata);
    }

    auto end = I_FloatTime();
    LogPrint("{:.3} seconds elapsed\n", (end - start));
    LogPrint("\n");
    LogPrint("stats:\n");
    LogPrint("{} lights tested, {} hits per sample point\n",
        static_cast<double>(total_light_rays) / static_cast<double>(total_samplepoints),
        static_cast<double>(total_light_ray_hits) / static_cast<double>(total_samplepoints));
    LogPrint("{} surface lights tested, {} hits per sample point\n",
        static_cast<double>(total_surflight_rays) / static_cast<double>(total_samplepoints),
        static_cast<double>(total_surflight_ray_hits) / static_cast<double>(total_samplepoints)); // mxd
    LogPrint("{} bounce lights tested, {} hits per sample point\n",
        static_cast<double>(total_bounce_rays) / static_cast<double>(total_samplepoints),
        static_cast<double>(total_bounce_ray_hits) / static_cast<double>(total_samplepoints));
    LogPrint("{} empty lightmaps\n", static_cast<int>(fully_transparent_lightmaps));
    CloseLog();

    return 0;
}
