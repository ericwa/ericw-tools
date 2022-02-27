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
#include <light/entities.hh>
#include <light/ltface.hh>

#include <common/polylib.hh>
#include <common/bsputils.hh>
#include <common/fs.hh>
#include <common/imglib.hh>

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

bool dirt_in_use = false;

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

std::vector<surfflags_t> extended_texinfo_flags;

int dump_facenum = -1;
int dump_vertnum = -1;

namespace settings
{
setting_group worldspawn_group{"Overridable worldspawn keys", 500};
setting_group output_group{"Output format options", 30};
setting_group debug_group{"Debug modes", 40};
setting_group postprocessing_group{"Postprocessing options", 50};
setting_group experimental_group{"Experimental options", 60};

void light_settings::initialize(int argc, const char **argv)
{
    auto remainder = parse(token_parser_t(argc - 1, argv + 1));

    if (remainder.size() <= 0 || remainder.size() > 1) {
        printHelp();
    }

    sourceMap = remainder[0];
}

void light_settings::postinitialize(int argc, const char **argv)
{
    if (gate.value() > 1) {
        LogPrint("WARNING: -gate value greater than 1 may cause artifacts\n");
    }

    if (radlights.isChanged()) {
        if (!ParseLightsFile(radlights.value())) {
            LogPrint("Unable to read surfacelights file {}\n", radlights.value());
        }
    }

    if (soft.value() == -1) {
        switch (extra.value()) {
            case 2: soft.setValueLocked(1); break;
            case 4: soft.setValueLocked(2); break;
            default: soft.setValueLocked(0); break;
        }
    }

    if (debugmode != debugmodes::none) {
        write_litfile |= lightfile::external;
    }

    if (litonly.value()) {
        write_litfile |= lightfile::external;
    }

    if (write_litfile == lightfile::lit2) {
        LogPrint("generating lit2 output only.\n");
    } else {
        if (write_litfile & lightfile::external)
            LogPrint(".lit colored light output requested on command line.\n");
        if (write_litfile & lightfile::bspx)
            LogPrint("BSPX colored light output requested on command line.\n");
        if (write_luxfile & lightfile::external)
            LogPrint(".lux light directions output requested on command line.\n");
        if (write_luxfile & lightfile::bspx)
            LogPrint("BSPX light directions output requested on command line.\n");
    }

    if (debugmode == debugmodes::dirt) {
        options.globalDirt.setValueLocked(true);
    } else if (debugmode == debugmodes::bounce || debugmode == debugmodes::bouncelights) {
        options.bounce.setValueLocked(true);
    } else if (debugmode == debugmodes::debugneighbours && !debugface.isChanged()) {
        FError("-debugneighbours without -debugface specified\n");
    }

    options.printSummary();
}
} // namespace settings

settings::light_settings options;

void SetGlobalSetting(std::string name, std::string value, bool cmdline)
{
    options.setSetting(name, value, cmdline);
}

void FixupGlobalSettings()
{
    static bool once = false;
    Q_assert(!once);
    once = true;

    // NOTE: This is confusing.. Setting "dirt" "1" implies "minlight_dirt" "1"
    // (and sunlight_dir/sunlight2_dirt as well), unless those variables were
    // set by the user to "0".
    //
    // We can't just default "minlight_dirt" to "1" because that would enable
    // dirtmapping by default.

    if (options.globalDirt.value()) {
        if (!options.minlightDirt.isChanged()) {
            options.minlightDirt.setValue(true);
        }
        if (!options.sunlight_dirt.isChanged()) {
            options.sunlight_dirt.setValue(1);
        }
        if (!options.sunlight2_dirt.isChanged()) {
            options.sunlight2_dirt.setValue(1);
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

const img::texture *Face_Texture(const mbsp_t *bsp, const mface_t *face)
{
    const char *name = Face_TextureName(bsp, face);

    if (!name || !*name) {
        return nullptr;
    }

    return img::find(name);
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
            LightFace(bsp, f, nullptr, options);
        else if (options.novanilla.value()) {
            f->lightofs = -1;
            f->styles[0] = 255;
            LightFace(bsp, f, faces_sup + facenum, options);
        } else if (faces_sup[facenum].lmscale == face_modelinfo->lightmapscale) {
            LightFace(bsp, f, nullptr, options);
            faces_sup[facenum].lightofs = f->lightofs;
            for (int i = 0; i < MAXLIGHTMAPS; i++)
                faces_sup[facenum].styles[i] = f->styles[i];
        } else {
            LightFace(bsp, f, nullptr, options);
            LightFace(bsp, f, faces_sup + facenum, options);
        }
    }

    return NULL;
}

static void FindModelInfo(const mbsp_t *bsp)
{
    Q_assert(modelinfo.size() == 0);
    Q_assert(tracelist.size() == 0);
    Q_assert(selfshadowlist.size() == 0);
    Q_assert(shadowworldonlylist.size() == 0);
    Q_assert(switchableshadowlist.size() == 0);

    if (!bsp->dmodels.size()) {
        FError("Corrupt .BSP: bsp->nummodels is 0!");
    }

    if (options.lmscale.isChanged()) {
        SetWorldKeyValue("_lightmap_scale", options.lmscale.stringValue());
    }

    float lightmapscale = atoi(WorldValueForKey("_lightmap_scale").c_str());
    if (!lightmapscale)
        lightmapscale = 16; /* the default */
    if (lightmapscale <= 0)
        FError("lightmap scale is 0 or negative\n");
    if (options.lmscale.isChanged() || lightmapscale != 16)
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
    world->shadow.setValue(1.0f); /* world always casts shadows */
    world->phong_angle = options.phongangle;
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
        info->setSettings(*entdict, false);

        /* Check if this model will cast shadows (shadow => shadowself) */
        if (info->switchableshadow.boolValue()) {
            Q_assert(info->switchshadstyle.value() != 0);
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

    if (lmshift_lump == bspdata->bspx.entries.end() && options.write_litfile != lightfile::lit2)
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
        options.bounce.value() && (options.debugmode == debugmodes::none || options.debugmode == debugmodes::bounce ||
                                      options.debugmode == debugmodes::bouncelights); // mxd
    const bool isQuake2map = bsp.loadversion->game->id == GAME_QUAKE_II; // mxd

    if ((bouncerequired || isQuake2map) && !options.nolighting.value()) {
        if (isQuake2map)
            MakeSurfaceLights(options, &bsp);
        if (bouncerequired)
            MakeBounceLights(options, &bsp);
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

    if ((bouncerequired || isQuake2map) && !options.nolighting.value()) { // mxd. Print some extra stats...
        LogPrint("Indirect lights: {} bounce lights, {} surface lights ({} light points) in use.\n",
            BounceLights().size(), SurfaceLights().size(), TotalSurfacelightPoints());
    }

    LogPrint("Lighting Completed.\n\n");

    // Transfer greyscale lightmap (or color lightmap for Q2/HL) to the bsp and update lightdatasize
    if (!options.litonly.value()) {
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
    if (!options.debugface.isChanged())
        return;

    const mface_t *f = Face_NearestCentroid(bsp, options.debugface.value());
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
static int Vertex_NearestPoint(const mbsp_t *bsp, const qvec3d &point)
{
    int nearest_vert = -1;
    float nearest_dist = std::numeric_limits<vec_t>::infinity();

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
    if (!options.debugvert.isChanged())
        return;

    int v = Vertex_NearestPoint(bsp, options.debugvert.value());

    FLogPrint("dumping vert {} at {}\n", v, bsp->dvertexes[v]);

    dump_vertnum = v;
}

static void SetLitNeeded()
{
    if (!options.write_litfile) {
        if (options.novanilla.value()) {
            options.write_litfile = lightfile::bspx;
            LogPrint("Colored light entities/settings detected: "
                     "bspxlit output enabled.\n");
        } else {
            options.write_litfile = lightfile::external;
            LogPrint("Colored light entities/settings detected: "
                     ".lit output enabled.\n");
        }
    }
}

static void CheckLitNeeded(const settings::worldspawn_keys &cfg)
{
    // check lights
    for (const auto &light : GetLights()) {
        if (!qv::epsilonEqual(vec3_white, light.color.value(), EQUAL_EPSILON) ||
            light.projectedmip != nullptr) { // mxd. Projected mips could also use .lit output
            SetLitNeeded();
            return;
        }
    }

    // check global settings
    if (cfg.bouncecolorscale.value() != 0 || !qv::epsilonEqual(cfg.minlight_color.value(), vec3_white, EQUAL_EPSILON) ||
        !qv::epsilonEqual(cfg.sunlight_color.value(), vec3_white, EQUAL_EPSILON) ||
        !qv::epsilonEqual(cfg.sun2_color.value(), vec3_white, EQUAL_EPSILON) ||
        !qv::epsilonEqual(cfg.sunlight2_color.value(), vec3_white, EQUAL_EPSILON) ||
        !qv::epsilonEqual(cfg.sunlight3_color.value(), vec3_white, EQUAL_EPSILON)) {
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

    LogPrint(LOG_VERBOSE, "Compressed {} normals down to {}\n", num_normals, unique_normals.size());

    bspdata.bspx.transfer("FACENORMALS", data, data_size);

    ofstream obj("test.obj");

    size_t index_id = 1;

    for (auto &face : bsp.dfaces) {
        auto &cache = FaceCacheForFNum(&face - bsp.dfaces.data());

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

    InitLog("light.log");
    LogPrint("---- light / ericw-tools " stringify(ERICWTOOLS_VERSION) " ----\n");

    options.preinitialize(argc, argv);
    options.initialize(argc, argv);

    LowerProcessPriority();

    auto start = I_FloatTime();
    fs::path source = options.sourceMap;

    // delete previous litfile
    if (!options.onlyents.value()) {
        source.replace_extension("lit");
        remove(source);
    }

    source.replace_extension("rad");
    if (source != "lights.rad")
        ParseLightsFile("lights.rad"); // generic/default name
    ParseLightsFile(source); // map-specific file name

    source.replace_extension("bsp");
    LoadBSPFile(source, &bspdata);

    bspdata.version->game->init_filesystem(source);

    ConvertBSPFormat(&bspdata, &bspver_generic);

    mbsp_t &bsp = std::get<mbsp_t>(bspdata.bsp);

    // mxd. Use 1.0 rangescale as a default to better match with qrad3/arghrad
    if ((bspdata.loadversion->game->id == GAME_QUAKE_II) && !options.rangescale.isChanged()) {
        options.rangescale =
            std::move(settings::setting_scalar(nullptr, options.rangescale.primaryName().c_str(), 1.0f, 0.0f,
                100.0f)); // Gross hacks to avoid displaying this in OptionsSummary...
    }

    img::init_palette(bspdata.loadversion->game);
    img::load_textures(&bsp);

    LoadExtendedTexinfoFlags(source, &bsp);
    LoadEntities(options, &bsp);

    options.postinitialize(argc, argv);

    FindModelInfo(&bsp);

    FindDebugFace(&bsp);
    FindDebugVert(&bsp);

    MakeTnodes(&bsp);

    if (options.debugmode == debugmodes::phong_obj) {
        CalculateVertexNormals(&bsp);
        source.replace_extension("obj");
        ExportObj(source, &bsp);

        CloseLog();
        return 0;
    }

    SetupLights(options, &bsp);

    // PrintLights();

    if (!options.onlyents.value()) {
        if (!bspdata.loadversion->game->has_rgb_lightmap) {
            CheckLitNeeded(options);
        }

        SetupDirt(options);

        LightWorld(&bspdata, options.lmscale.isChanged());

        // invalidate normals
        bspdata.bspx.entries.erase("FACENORMALS");

        if (options.write_normals.value()) {
            WriteNormals(bsp, bspdata);
        }

        /*invalidate any bspx lighting info early*/
        bspdata.bspx.entries.erase("RGBLIGHTING");
        bspdata.bspx.entries.erase("LIGHTINGDIR");

        if (options.write_litfile == lightfile::lit2) {
            WriteLitFile(&bsp, faces_sup, source, 2);
            return 0; // run away before any files are written
        }

        /*fixme: add a new per-surface offset+lmscale lump for compat/versitility?*/
        if (options.write_litfile & lightfile::external) {
            WriteLitFile(&bsp, faces_sup, source, LIT_VERSION);
        }
        if (options.write_litfile & lightfile::bspx) {
            bspdata.bspx.transfer("RGBLIGHTING", lit_filebase, bsp.dlightdata.size() * 3);
        }
        if (options.write_luxfile & lightfile::external) {
            WriteLuxFile(&bsp, source, LIT_VERSION);
        }
        if (options.write_luxfile & lightfile::bspx) {
            bspdata.bspx.transfer("LIGHTINGDIR", lux_filebase, bsp.dlightdata.size() * 3);
        }
    }

    /* -novanilla + internal lighting = no grey lightmap */
    if (options.novanilla.value() && (options.write_litfile & lightfile::bspx)) {
        bsp.dlightdata.clear();
    }

#if 0
    ExportObj(source, bsp);
#endif

    WriteEntitiesToString(options, &bsp);
    /* Convert data format back if necessary */
    ConvertBSPFormat(&bspdata, bspdata.loadversion);

    if (!options.litonly.value()) {
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
