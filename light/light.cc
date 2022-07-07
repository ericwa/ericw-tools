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
#include <common/parallel.hh>

#if defined(HAVE_EMBREE) && defined (__SSE2__)
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

bool dirt_in_use = false;

// intermediate representation of lightmap surfaces
static std::vector<std::unique_ptr<lightsurf_t>> light_surfaces;

std::vector<std::unique_ptr<lightsurf_t>> &LightSurfaces()
{
    return light_surfaces;
}

static std::vector<facesup_t> faces_sup; // lit2/bspx stuff

/// start of lightmap data
std::vector<uint8_t> filebase;
/// offset of start of free space after data (should be kept a multiple of 4)
static int file_p;
/// offset of end of free space for lightmap data
static int file_end;

/// start of litfile data
std::vector<uint8_t> lit_filebase;
/// offset of start of free space after litfile data (should be kept a multiple of 12)
static int lit_file_p;
/// offset of end of space for litfile data
static int lit_file_end;

/// start of luxfile data
std::vector<uint8_t> lux_filebase;
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
    token_parser_t p(argc - 1, argv + 1);
    auto remainder = parse(p);

    if (remainder.size() <= 0 || remainder.size() > 1) {
        printHelp();
    }

    sourceMap = remainder[0];
}

void light_settings::postinitialize(int argc, const char **argv)
{
    if (gate.value() > 1) {
        logging::print("WARNING: -gate value greater than 1 may cause artifacts\n");
    }

    if (radlights.isChanged()) {
        if (!ParseLightsFile(*radlights.values().begin())) {
            logging::print("Unable to read surface lights file {}\n", *radlights.values().begin());
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
        logging::print("generating lit2 output only.\n");
    } else {
        if (write_litfile & lightfile::external)
            logging::print(".lit colored light output requested on command line.\n");
        if (write_litfile & lightfile::bspx)
            logging::print("BSPX colored light output requested on command line.\n");
        if (write_luxfile & lightfile::external)
            logging::print(".lux light directions output requested on command line.\n");
        if (write_luxfile & lightfile::bspx)
            logging::print("BSPX light directions output requested on command line.\n");
    }

    if (debugmode == debugmodes::dirt) {
        options.globalDirt.setValueLocked(true);
    } else if (debugmode == debugmodes::bounce || debugmode == debugmodes::bouncelights) {
        options.bounce.setValueLocked(true);
    } else if (debugmode == debugmodes::debugneighbours && !debugface.isChanged()) {
        FError("-debugneighbours without -debugface specified\n");
    }

    if (options.q2rtx.value()) {
        if (!options.nolighting.isChanged()) {
            options.nolighting.setValueLocked(true);
        }
    }

    // upgrade to uint16 if facestyles is specified 
    if (options.facestyles.value() > MAXLIGHTMAPS && !options.compilerstyle_max.isChanged()) {
        options.compilerstyle_max.setValue(INVALID_LIGHTSTYLE);
    }
    
    common_settings::postinitialize(argc, argv);
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

static std::mutex light_mutex;

/*
 * Return space for the lightmap and colourmap at the same time so it can
 * be done in a thread-safe manner.
 *
 * size is the number of greyscale pixels = number of bytes to allocate
 * and return in *lightdata
 */
void GetFileSpace(uint8_t **lightdata, uint8_t **colordata, uint8_t **deluxdata, int size)
{
    light_mutex.lock();

    *lightdata = *colordata = *deluxdata = nullptr;

    if (!filebase.empty()) {
        *lightdata = filebase.data() + file_p;
    }
    if (!lit_filebase.empty()) {
        *colordata = lit_filebase.data() + lit_file_p;
    }
    if (!lux_filebase.empty()) {
        *deluxdata = lux_filebase.data() + lux_file_p;
    }

    // if size isn't a multiple of 4, round up to the next multiple of 4
    if ((size % 4) != 0) {
        size += (4 - (size % 4));
    }

    // increment the next writing offsets, aligning them to 4 uint8_t boundaries (file_p)
    // and 12-uint8_t boundaries (lit_file_p/lux_file_p)
    if (!filebase.empty()) {
        file_p += size;
    }
    if (!lit_filebase.empty()) {
        lit_file_p += 3 * size;
    }
    if (!lux_filebase.empty()) {
        lux_file_p += 3 * size;
    }

    light_mutex.unlock();

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
    
    *lightdata = *colordata = *deluxdata = nullptr;

    if (!filebase.empty()) {
        *lightdata = filebase.data() + lightofs;
    }

    if (colordata && !lit_filebase.empty()) {
        *colordata = lit_filebase.data() + (lightofs * 3);
    }

    if (deluxdata && !lux_filebase.empty()) {
        *deluxdata = lux_filebase.data() + (lightofs * 3);
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

static std::vector<const img::texture *> face_textures;

const img::texture *Face_Texture(const mbsp_t *bsp, const mface_t *face)
{
    return face_textures[face - bsp->dfaces.data()];
}

static void CacheTextures(const mbsp_t &bsp)
{
    face_textures.resize(bsp.dfaces.size());
    
    for (size_t i = 0; i < bsp.dfaces.size(); i++) {
        const char *name = Face_TextureName(&bsp, &bsp.dfaces[i]);

        if (!name || !*name) {
            face_textures[i] = nullptr;
        } else {
            face_textures[i] = img::find(name);
        }
    }
}

static void CreateLightmapSurfaces(mbsp_t *bsp)
{
    light_surfaces.resize(bsp->dfaces.size());
    logging::print("--- CreateLightmapSurfaces ---\n");
    logging::parallel_for(static_cast<size_t>(0), bsp->dfaces.size(), [&bsp](size_t i) {
        auto facesup = faces_sup.empty() ? nullptr : &faces_sup[i];
        auto face = &bsp->dfaces[i];

        /* One extra lightmap is allocated to simplify handling overflow */
        if (!options.litonly.value()) {
            // if litonly is set we need to preserve the existing lightofs

            /* some surfaces don't need lightmaps */
            if (facesup) {
                facesup->lightofs = -1;
                for (size_t i = 0; i < MAXLIGHTMAPSSUP; i++) {
                    facesup->styles[i] = INVALID_LIGHTSTYLE;
                }
            } else {
                face->lightofs = -1;
                for (size_t i = 0; i < MAXLIGHTMAPS; i++) {
                    face->styles[i] = INVALID_LIGHTSTYLE_OLD;
                }
            }
        }

        light_surfaces[i] = std::move(CreateLightmapSurface(bsp, face, facesup, options));
    });
}

static void SaveLightmapSurfaces(mbsp_t *bsp)
{
    logging::print("--- SaveLightmapSurfaces ---\n");
    logging::parallel_for(static_cast<size_t>(0), bsp->dfaces.size(), [&bsp](size_t i) {
        auto &surf = light_surfaces[i];

        if (!surf) {
            return;
        }

        FinishLightmapSurface(bsp, surf.get());

        auto f = &bsp->dfaces[i];
        const modelinfo_t *face_modelinfo = ModelInfoForFace(bsp, i);

        if (faces_sup.empty()) {
            SaveLightmapSurface(bsp, f, nullptr, surf.get(), surf->extents, surf->extents);
        } else if (options.novanilla.value()) {
            f->lightofs = -1;
            f->styles[0] = INVALID_LIGHTSTYLE_OLD;
            SaveLightmapSurface(bsp, f, &faces_sup[i], surf.get(), surf->extents, surf->extents);
        } else if (faces_sup[i].lmscale == face_modelinfo->lightmapscale) {
            SaveLightmapSurface(bsp, f, &faces_sup[i], surf.get(), surf->extents, surf->extents);
            f->lightofs = faces_sup[i].lightofs;
            for (int i = 0; i < MAXLIGHTMAPS; i++) {
                f->styles[i] = faces_sup[i].styles[i];
            }
        } else {
            SaveLightmapSurface(bsp, f, nullptr, surf.get(), surf->extents, surf->vanilla_extents);
            SaveLightmapSurface(bsp, f, &faces_sup[i], surf.get(), surf->extents, surf->extents);
        }

        light_surfaces[i].reset();
    });
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

    if (options.lightmap_scale.isChanged()) {
        WorldEnt().set("_lightmap_scale", options.lightmap_scale.stringValue());
    }

    float lightmapscale = WorldEnt().get_int("_lightmap_scale");
    if (!lightmapscale)
        lightmapscale = 16; /* the default */
    if (lightmapscale <= 0)
        FError("lightmap scale is 0 or negative\n");
    if (options.lightmap_scale.isChanged() || lightmapscale != 16)
        logging::print("Forcing lightmap scale of {}qu\n", lightmapscale);
    /*I'm going to do this check in the hopes that there's a benefit to cheaper scaling in engines (especially software
     * ones that might be able to just do some mip hacks). This tool doesn't really care.*/
    {
        int i;
        for (i = 1; i < lightmapscale;) {
            i++;
        }
        if (i != lightmapscale) {
            logging::print("WARNING: lightmap scale is not a power of 2\n");
        }
    }

    /* The world always casts shadows */
    modelinfo_t *world = new modelinfo_t{bsp, &bsp->dmodels[0], lightmapscale};
    world->shadow.setValue(1.0f); /* world always casts shadows */
    world->phong_angle.copyFrom(options.phongangle);
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
        entdict->get_vector("origin", info->offset);
    }

    Q_assert(modelinfo.size() == bsp->dmodels.size());
}

// FIXME: in theory can't we calculate the exact amount of
// storage required? we'd have to expand it by 4 to account for
// lightstyles though
static constexpr size_t MAX_MAP_LIGHTING = 0x8000000;

/*
 * =============
 *  LightWorld
 * =============
 */
static void LightWorld(bspdata_t *bspdata, bool forcedscale)
{
    logging::print("--- LightWorld ---\n");

    mbsp_t &bsp = std::get<mbsp_t>(bspdata->bsp);

    light_surfaces.clear();
    filebase.clear();
    lit_filebase.clear();
    lux_filebase.clear();

    if (!bsp.loadversion->game->has_rgb_lightmap) {
        /* greyscale data stored in a separate buffer */
        filebase.resize(MAX_MAP_LIGHTING);
        file_p = 0;
        file_end = MAX_MAP_LIGHTING;
    }

    if (bsp.loadversion->game->has_rgb_lightmap || options.write_litfile) {
        /* litfile data stored in a separate buffer */
        lit_filebase.resize(MAX_MAP_LIGHTING * 3);
        lit_file_p = 0;
        lit_file_end = (MAX_MAP_LIGHTING * 3);
    }

    if (options.write_luxfile) {
        /* lux data stored in a separate buffer */
        lux_filebase.resize(MAX_MAP_LIGHTING * 3);
        lux_file_p = 0;
        lux_file_end = (MAX_MAP_LIGHTING * 3);
    }

    if (forcedscale) {
        bspdata->bspx.entries.erase("LMSHIFT");
    }

    auto lmshift_lump = bspdata->bspx.entries.find("LMSHIFT");

    if (lmshift_lump == bspdata->bspx.entries.end() && options.write_litfile != lightfile::lit2 && options.facestyles.value() <= 4) {
        faces_sup.clear(); // no scales, no lit2
    } else { // we have scales or lit2 output. yay...
        faces_sup.resize(bsp.dfaces.size());

        if (lmshift_lump != bspdata->bspx.entries.end()) {
            for (int i = 0; i < bsp.dfaces.size(); i++) {
                faces_sup[i].lmscale = nth_bit(reinterpret_cast<const char *>(lmshift_lump->second.data())[i]);
            }
        } else {
            for (int i = 0; i < bsp.dfaces.size(); i++) {
                faces_sup[i].lmscale = modelinfo.at(0)->lightmapscale;
            }
        }
    }

    CalculateVertexNormals(&bsp);

    // create lightmap surfaces
    CreateLightmapSurfaces(&bsp);

    const bool bouncerequired =
        options.bounce.value() && (options.debugmode == debugmodes::none || options.debugmode == debugmodes::bounce ||
                                      options.debugmode == debugmodes::bouncelights); // mxd

    MakeRadiositySurfaceLights(options, &bsp);

    logging::print("--- Direct Lighting ---\n"); // mxd
    logging::parallel_for(static_cast<size_t>(0), bsp.dfaces.size(), [&bsp](size_t i) {
        if (light_surfaces[i]) {
#if defined(HAVE_EMBREE) && defined (__SSE2__)
            _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
#endif

            DirectLightFace(&bsp, *light_surfaces[i].get(), options);
        }
    });

    if (bouncerequired && !options.nolighting.value()) {
        GetLights().clear();
        GetRadLights().clear();
        GetSuns().clear();
        GetSurfaceLights().clear();

        MakeBounceLights(options, &bsp);

        logging::print("--- Indirect Lighting ---\n"); // mxd
        logging::parallel_for(static_cast<size_t>(0), bsp.dfaces.size(), [&bsp](size_t i) {
            if (light_surfaces[i]) {
    #if defined(HAVE_EMBREE) && defined (__SSE2__)
                _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    #endif

                IndirectLightFace(&bsp, *light_surfaces[i].get(), options);
            }
        });
    }

    SaveLightmapSurfaces(&bsp);

    logging::print("Lighting Completed.\n\n");

    // Transfer greyscale lightmap (or color lightmap for Q2/HL) to the bsp and update lightdatasize
    if (!options.litonly.value()) {
        if (bsp.loadversion->game->has_rgb_lightmap) {
            bsp.dlightdata.resize(lit_file_p);
            memcpy(bsp.dlightdata.data(), lit_filebase.data(), bsp.dlightdata.size());
        } else {
            bsp.dlightdata.resize(file_p);
            memcpy(bsp.dlightdata.data(), filebase.data(), bsp.dlightdata.size());
        }
    } else {
        // NOTE: bsp.lightdatasize is already valid in the -litonly case
    }
    logging::print("lightdatasize: {}\n", bsp.dlightdata.size());

    // kill this stuff if its somehow found.
    bspdata->bspx.entries.erase("LMSTYLE16");
    bspdata->bspx.entries.erase("LMSTYLE");
    bspdata->bspx.entries.erase("LMOFFSET");

    if (!faces_sup.empty()) {
        bool needoffsets = false;
        bool needstyles = false;
        int maxstyle = 0;
        int stylesperface = 0;

        for (int i = 0; i < bsp.dfaces.size(); i++) {
            if (bsp.dfaces[i].lightofs != faces_sup[i].lightofs)
                needoffsets = true;
            int j = 0;
            for (; j < MAXLIGHTMAPSSUP; j++) {
                if (faces_sup[i].styles[j] == INVALID_LIGHTSTYLE)
                    break;
                if (bsp.dfaces[i].styles[j] != faces_sup[i].styles[j])
                    needstyles = true;
                if (maxstyle < faces_sup[i].styles[j])
                    maxstyle = faces_sup[i].styles[j];
            }
            if (stylesperface < j)
                stylesperface = j;
        }

        needstyles |= (stylesperface>4);

        logging::print("max {} styles per face{}\n", stylesperface, maxstyle >= INVALID_LIGHTSTYLE_OLD ? ", 16bit lightstyles" : "");
        
        if (needstyles) {
            if (maxstyle >= INVALID_LIGHTSTYLE_OLD) {
                /*needs bigger datatype*/
                std::vector<uint8_t> styles_mem(sizeof(uint16_t) * stylesperface * bsp.dfaces.size());

                omemstream styles(styles_mem.data(), styles_mem.size(), std::ios_base::out | std::ios_base::binary);
                styles << endianness<std::endian::little>;

                for (size_t i = 0; i < bsp.dfaces.size(); i++) {
                    for (size_t j = 0; j < stylesperface; j++) {
                        styles <= faces_sup[i].styles[j];
                    }
                }

                bspdata->bspx.transfer("LMSTYLE16", styles_mem);
            } else {
                /*original LMSTYLE lump was just for different lmshift info*/
                std::vector<uint8_t> styles_mem(stylesperface * bsp.dfaces.size());

                for (size_t i = 0, k = 0; i < bsp.dfaces.size(); i++) {
                    for (size_t j = 0; j < stylesperface; j++, k++) {
                        styles_mem[k] = faces_sup[i].styles[j];
                    }
                }

                bspdata->bspx.transfer("LMSTYLE", styles_mem);
            }
        }

        if (needoffsets) {
            std::vector<uint8_t> offsets_mem(bsp.dfaces.size() * sizeof(int32_t));

            omemstream offsets(offsets_mem.data(), offsets_mem.size(), std::ios_base::out | std::ios_base::binary);
            offsets << endianness<std::endian::little>;

            for (size_t i = 0; i < bsp.dfaces.size(); i++) {
                offsets <= faces_sup[i].lightofs;
            }

            bspdata->bspx.transfer("LMOFFSET", offsets_mem);
        }
    }
}

static void LoadExtendedTexinfoFlags(const fs::path &sourcefilename, const mbsp_t *bsp)
{
    // always create the zero'ed array
    extended_texinfo_flags.resize(bsp->texinfo.size());

    fs::path filename(sourcefilename);
    filename.replace_extension("texinfo.json");

    std::ifstream texinfofile(filename, std::ios_base::in | std::ios_base::binary);

    if (!texinfofile)
        return;

    logging::print("Loading extended texinfo flags from {}...\n", filename);

    json j;

    texinfofile >> j;

    for (auto it = j.begin(); it != j.end(); ++it) {
        size_t index = std::stoull(it.key());

        if (index >= bsp->texinfo.size()) {
            logging::print("WARNING: Extended texinfo flags in {} does not match bsp, ignoring\n", filename);
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

static void ExportObj(const fs::path &filename, const mbsp_t *bsp)
{
    std::ofstream objfile(filename);
    int vertcount = 0;

    const int start = bsp->dmodels[0].firstface;
    const int end = bsp->dmodels[0].firstface + bsp->dmodels[0].numfaces;

    for (int i = start; i < end; i++) {
        ExportObjFace(objfile, bsp, BSP_GetFace(bsp, i), &vertcount);
    }

    logging::print("Wrote {}\n", filename);
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
    logging::funcprint("dumping face {} (texture '{}' model {})\n", facenum, texname, modelnum);
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

    logging::funcprint("dumping vert {} at {}\n", v, bsp->dvertexes[v]);

    dump_vertnum = v;
}

static void SetLitNeeded()
{
    if (!options.write_litfile) {
        if (options.novanilla.value()) {
            options.write_litfile = lightfile::bspx;
            logging::print("Colored light entities/settings detected: "
                     "bspxlit output enabled.\n");
        } else {
            options.write_litfile = lightfile::external;
            logging::print("Colored light entities/settings detected: "
                     ".lit output enabled.\n");
        }
    }
}

static void CheckLitNeeded(const settings::worldspawn_keys &cfg)
{
    // check lights
    for (const auto &light : GetLights()) {
        if (!qv::epsilonEqual(vec3_white, light->color.value(), EQUAL_EPSILON) ||
            light->projectedmip != nullptr) { // mxd. Projected mips could also use .lit output
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
            logging::print("; ");
        } else {
            first = false;
        }

        logging::print("{}={}", setting->primaryName(), setting->stringValue());
    }
    logging::print("\n");
}

static void PrintLights(void)
{
    logging::print("===PrintLights===\n");

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
    std::vector<uint8_t> data(data_size);
    omemstream stream(data.data(), data_size);

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

    logging::print(logging::flag::VERBOSE, "Compressed {} normals down to {}\n", num_normals, unique_normals.size());

    bspdata.bspx.transfer("FACENORMALS", data);
}

/*
// Add empty to keep texture index in case of load problems...
auto &tex = img::textures.emplace(miptex.name, img::texture{}).first->second;

// try to load it externally first
auto [texture, _0, _1] = img::load_texture(miptex.name, false, bsp->loadversion->game, options);

if (texture) {
    tex = std::move(texture.value());
} else {
    if (miptex.data.size() <= sizeof(dmiptex_t)) {
        logging::funcprint("WARNING: can't find texture {}\n", miptex.name);
        continue;
    }

    auto loaded_tex = img::load_mip(miptex.name, miptex.data, false, bsp->loadversion->game);

    if (!loaded_tex) {
        logging::funcprint("WARNING: Texture {} is invalid\n", miptex.name);
        continue;
    }

    tex = std::move(loaded_tex.value());
}

tex.meta.averageColor = img::calculate_average(tex.pixels);
*/

// Load the specified texture from the BSP
static void AddTextureName(const std::string_view &textureName, const mbsp_t *bsp)
{
    if (img::find(textureName)) {
        return;
    }

    // always add entry
    auto &tex = img::textures.emplace(textureName, img::texture{}).first->second;

    // find texture & meta
    auto [ texture, _0, _1 ] = img::load_texture(textureName, false, bsp->loadversion->game, options);

    if (!texture) {
        logging::funcprint("WARNING: can't find pixel data for {}\n", textureName);
    } else {
        tex = std::move(texture.value());
    }

    auto [ texture_meta, __0, __1 ] = img::load_texture_meta(textureName, bsp->loadversion->game, options);
    
    if (!texture_meta) {
        logging::funcprint("WARNING: can't find meta data for {}\n", textureName);
    } else {
        tex.meta = std::move(texture_meta.value());
    }

    tex.averageColor = img::calculate_average(tex.pixels);
    tex.width_scale = (float) tex.width / (float) tex.meta.width;
    tex.height_scale = (float) tex.height / (float) tex.meta.height;
}

// Load all of the referenced textures from the BSP texinfos into
// the texture cache.
static void LoadTextures(const mbsp_t *bsp)
{
    // gather all loadable textures...
    for (auto &texinfo : bsp->texinfo) {
        AddTextureName(texinfo.texture.data(), bsp);
    }

    // gather textures used by _project_texture.
    // FIXME: I'm sure we can resolve this so we don't parse entdata twice.
    auto entdicts = EntData_Parse(bsp->dentdata);
    for (auto &entdict : entdicts) {
        if (entdict.get("classname").find("light") == 0) {
            const auto &tex = entdict.get("_project_texture");
            if (!tex.empty()) {
                AddTextureName(tex.c_str(), bsp);
            }
        }
    }
}

// Load all of the paletted textures from the BSP into
// the texture cache.
static void ConvertTextures(const mbsp_t *bsp)
{
    if (!bsp->dtex.textures.size()) {
        return;
    }

    for (auto &miptex : bsp->dtex.textures) {
        if (img::find(miptex.name)) {
            logging::funcprint("WARNING: Texture {} duplicated\n", miptex.name);
            continue;
        }

        // always add entry
        auto &tex = img::textures.emplace(miptex.name, img::texture{}).first->second;

        // if the miptex entry isn't a dummy, use it as our base
        if (miptex.data.size() >= sizeof(dmiptex_t)) {
            if (auto loaded_tex = img::load_mip(miptex.name, miptex.data, false, bsp->loadversion->game)) {
                tex = std::move(loaded_tex.value());
            }
        }

        // find replacement texture
        if (auto [ texture, _0, _1 ] = img::load_texture(miptex.name, false, bsp->loadversion->game, options); texture) {
            tex.width = texture->width;
            tex.height = texture->height;
            tex.pixels = std::move(texture->pixels);
        }

        if (!tex.pixels.size() || !tex.width || !tex.meta.width) {
            logging::funcprint("WARNING: invalid size data for {}\n", miptex.name);
            continue;
        }

        tex.averageColor = img::calculate_average(tex.pixels);
        tex.width_scale = (float) tex.width / (float) tex.meta.width;
        tex.height_scale = (float) tex.height / (float) tex.meta.height;
    }
}

void load_textures(const mbsp_t *bsp)
{
    logging::print("--- {} ---\n", __func__);

    if (bsp->loadversion->game->id == GAME_QUAKE_II) {
        LoadTextures(bsp);
    } else if (bsp->dtex.textures.size() > 0) {
        ConvertTextures(bsp);
    } else {
        logging::print("WARNING: failed to load or convert textures.\n");
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

    options.preinitialize(argc, argv);
    options.initialize(argc, argv);

    auto start = I_FloatTime();
    fs::path source = options.sourceMap;

    logging::init(fs::path(source).replace_filename(source.stem().string() + "-light").replace_extension("log"), options);

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

    bspdata.version->game->init_filesystem(source, options);

    ConvertBSPFormat(&bspdata, &bspver_generic);

    mbsp_t &bsp = std::get<mbsp_t>(bspdata.bsp);

    // mxd. Use 1.0 rangescale as a default to better match with qrad3/arghrad
    if (bspdata.loadversion->game->id == GAME_QUAKE_II) {
        if (!options.rangescale.isChanged()) {
            options.rangescale.setValue(1.0f);
        }
        if (!options.bouncecolorscale.isChanged()) {
            options.bouncecolorscale.setValue(0.5f);
        }
        if (!options.surflightscale.isChanged()) {
            options.surflightscale.setValue(0.65f);
        }
        if (!options.bouncescale.isChanged()) {
            options.bouncescale.setValue(1.25f);
        }
        if (!options.bounce.isChanged()) {
            options.bounce.setValue(true);
        }
    }

    // check vis approx type
    if (options.visapprox.value() == visapprox_t::AUTO) {
        options.visapprox.setValue(visapprox_t::RAYS);
    }

    load_textures(&bsp);

    CacheTextures(bsp);

    LoadExtendedTexinfoFlags(source, &bsp);
    LoadEntities(options, &bsp);

    options.postinitialize(argc, argv);

    FindModelInfo(&bsp);

    FindDebugFace(&bsp);
    FindDebugVert(&bsp);

    Embree_TraceInit(&bsp);

    if (options.debugmode == debugmodes::phong_obj) {
        CalculateVertexNormals(&bsp);
        source.replace_extension("obj");
        ExportObj(source, &bsp);

        logging::close();
        return 0;
    }

    SetupLights(options, &bsp);

    // PrintLights();

    if (!options.onlyents.value()) {
        if (!bspdata.loadversion->game->has_rgb_lightmap) {
            CheckLitNeeded(options);
        }

        SetupDirt(options);

        LightWorld(&bspdata, options.lightmap_scale.isChanged());

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
            lit_filebase.resize(bsp.dlightdata.size() * 3);
            bspdata.bspx.transfer("RGBLIGHTING", lit_filebase);
        }
        if (options.write_luxfile & lightfile::external) {
            WriteLuxFile(&bsp, source, LIT_VERSION);
        }
        if (options.write_luxfile & lightfile::bspx) {
            lux_filebase.resize(bsp.dlightdata.size() * 3);
            bspdata.bspx.transfer("LIGHTINGDIR", lux_filebase);
        }
    }

    /* -novanilla + internal lighting = no grey lightmap */
    if (options.novanilla.value() && (options.write_litfile & lightfile::bspx)) {
        bsp.dlightdata.clear();
    }

    if (options.exportobj.value()) {
        ExportObj(fs::path{source}.replace_extension(".obj"), &bsp);
    }

    WriteEntitiesToString(options, &bsp);
    /* Convert data format back if necessary */
    ConvertBSPFormat(&bspdata, bspdata.loadversion);

    if (!options.litonly.value()) {
        WriteBSPFile(source, &bspdata);
    }

    auto end = I_FloatTime();
    logging::print("{:.3} seconds elapsed\n", (end - start));
    logging::print("\n");
    logging::print("stats:\n");
    logging::print("{} lights tested, {} hits per sample point\n",
        static_cast<double>(total_light_rays) / static_cast<double>(total_samplepoints),
        static_cast<double>(total_light_ray_hits) / static_cast<double>(total_samplepoints));
    logging::print("{} surface lights tested, {} hits per sample point\n",
        static_cast<double>(total_surflight_rays) / static_cast<double>(total_samplepoints),
        static_cast<double>(total_surflight_ray_hits) / static_cast<double>(total_samplepoints)); // mxd
    logging::print("{} bounce lights tested, {} hits per sample point\n",
        static_cast<double>(total_bounce_rays) / static_cast<double>(total_samplepoints),
        static_cast<double>(total_bounce_ray_hits) / static_cast<double>(total_samplepoints));
    logging::print("{} empty lightmaps\n", static_cast<int>(fully_transparent_lightmaps));
    logging::close();

    return 0;
}
