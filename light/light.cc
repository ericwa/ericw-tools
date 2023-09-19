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

#include <light/light.hh>

#include <cstdint>
#include <iostream>
#include <fmt/chrono.h>

#include <light/lightgrid.hh>
#include <light/phong.hh>
#include <light/bounce.hh>
#include <light/surflight.hh> //mxd
#include <light/entities.hh>
#include <light/ltface.hh>
#include <light/write.hh> // for facesup_t
#include <light/trace_embree.hh>

#include <common/log.hh>
#include <common/bsputils.hh>
#include <common/numeric_cast.hh>
#include <common/fs.hh>
#include <common/imglib.hh>
#include <common/parallel.hh>
#include <common/ostream.hh>

#if defined(HAVE_EMBREE) && defined(__SSE2__)
#include <xmmintrin.h>
// #include <pmmintrin.h>
#endif

#include <memory>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <mutex>
#include <string>

#include <common/qvec.hh>
#include <common/json.hh>

bool dirt_in_use = false;

// intermediate representation of lightmap surfaces
static std::unique_ptr<lightsurf_t[]> light_surfaces;
static std::span<lightsurf_t> light_surfaces_span;

// light_surfaces filtered down to just the emissive ones
static std::vector<lightsurf_t*> emissive_light_surfaces;

std::span<lightsurf_t> &LightSurfaces()
{
    return light_surfaces_span;
}

std::vector<lightsurf_t*> &EmissiveLightSurfaces()
{
    return emissive_light_surfaces;
}

static void UpdateEmissiveLightSurfacesList()
{
    emissive_light_surfaces.clear();

    for (auto &surf_ptr : light_surfaces_span) {
        if (surf_ptr.vpl) {
            emissive_light_surfaces.push_back(&surf_ptr);
        }
    }
}

std::vector<facesup_t> faces_sup; // lit2/bspx stuff
std::vector<bspx_decoupled_lm_perface> facesup_decoupled_global;

bool IsOutputtingSupplementaryData()
{
    return !faces_sup.empty();
}

static std::unordered_map<int, std::vector<uint8_t>> all_uncompressed_vis;

const std::unordered_map<int, std::vector<uint8_t>> &UncompressedVis()
{
    return all_uncompressed_vis;
}

std::vector<modelinfo_t *> modelinfo;
std::vector<const modelinfo_t *> tracelist;
std::vector<const modelinfo_t *> selfshadowlist;
std::vector<const modelinfo_t *> shadowworldonlylist;
std::vector<const modelinfo_t *> switchableshadowlist;

std::vector<surfflags_t> extended_texinfo_flags;

int dump_facenum = -1;
int dump_vertnum = -1;

// modelinfo_t

float modelinfo_t::getResolvedPhongAngle() const
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

bool modelinfo_t::isWorld() const
{
    return &bsp->dmodels[0] == model;
}

modelinfo_t::modelinfo_t(const mbsp_t *b, const dmodelh2_t *m, float lmscale)
    : bsp{b},
      model{m},
      lightmapscale{lmscale},
      offset{},
      minlight{this, "minlight", 0},
      maxlight{this, "maxlight", 0},
      minlightMottle{this, {"minlight_mottle", "minlightMottle"}, false},
      shadow{this, "shadow", 0},
      shadowself{this, {"shadowself", "selfshadow"}, 0},
      shadowworldonly{this, "shadowworldonly", 0},
      switchableshadow{this, "switchableshadow", 0},
      switchshadstyle{this, "switchshadstyle", 0},
      dirt{this, "dirt", 0},
      phong{this, "phong", 0},
      phong_angle{this, "phong_angle", 0},
      alpha{this, "alpha", 1.0},
      minlight_color{this, {"minlight_color", "mincolor"}, 255.0, 255.0, 255.0},
      lightignore{this, "lightignore", false},
      lightcolorscale{this, "lightcolorscale", 1},
      object_channel_mask{this, "object_channel_mask", CHANNEL_MASK_DEFAULT},
      surflight_minlight_scale{this, "surflight_minlight_scale", 1.f},
      autominlight{this, "autominlight", false},
      autominlight_target{this, "autominlight_target", ""}
{
}

namespace settings
{
// worldspawn_keys

worldspawn_keys::worldspawn_keys()
    : scaledist{this, "dist", 1.0, 0.0, 100.0, &worldspawn_group},
      rangescale{this, "range", 0.5, 0.0, 100.0, &worldspawn_group},
      global_anglescale{this, {"anglescale", "anglesense"}, 0.5, 0.0, 1.0, &worldspawn_group},
      lightmapgamma{this, "gamma", 1.0, 0.0, 100.0, &worldspawn_group},
      addminlight{this, "addmin", false, &worldspawn_group},
      minlight{this, {"light", "minlight"}, 0, &worldspawn_group},
      minlightMottle{this, {"minlight_mottle", "minlightMottle"}, false, &worldspawn_group},
      maxlight{this, "maxlight", 0, &worldspawn_group},
      minlight_color{this, {"minlight_color", "mincolor"}, 255.0, 255.0, 255.0, &worldspawn_group},
      spotlightautofalloff{this, "spotlightautofalloff", false, &worldspawn_group},
      compilerstyle_start{this, "compilerstyle_start", 32, &worldspawn_group},
      compilerstyle_max{this, "compilerstyle_max", 64, &worldspawn_group},
      dirt{this, {"dirt", "dirty"}, false, &worldspawn_group,
          "apply dirt to all lights (unless they override it) + sunlight + minlight"},
      dirtmode{this, "dirtmode", 0.0f, &worldspawn_group},
      dirtdepth{this, "dirtdepth", 128.0, 1.0, std::numeric_limits<float>::infinity(), &worldspawn_group},
      dirtscale{this, "dirtscale", 1.0, 0.0, 100.0, &worldspawn_group},
      dirtgain{this, "dirtgain", 1.0, 0.0, 100.0, &worldspawn_group},
      dirtangle{this, "dirtangle", 88.0, 1.0, 90.0, &worldspawn_group},
      minlight_dirt{this, "minlight_dirt", false, &worldspawn_group},
      phongallowed{this, "phong", true, &worldspawn_group},
      phongangle{this, "phong_angle", 0, &worldspawn_group},
      bounce{this, "bounce", 0, 0, 100, settings::can_omit_argument_tag(), 1, &worldspawn_group},
      bouncestyled{this, "bouncestyled", false, &worldspawn_group},
      bouncescale{this, "bouncescale", 1.0, 0.0, 100.0, &worldspawn_group},
      bouncecolorscale{this, "bouncecolorscale", 0.0, 0.0, 1.0, &worldspawn_group},
      bouncelightsubdivision{this, "bouncelightsubdivision", 64.0, 1.0, 8192.0, &worldspawn_group},
      surflightscale{this, "surflightscale", 1.0, &worldspawn_group},
      surflightskyscale{this, "surflightskyscale", 1.0, &worldspawn_group},
      surflightskydist{this, "surflightskydist", 0.0, &worldspawn_group},
      surflightsubdivision{this, {"surflightsubdivision", "choplight"}, 16.0, 1.0, 8192.0, &worldspawn_group},
      surflight_minlight_scale{this, "surflight_minlight_scale", 1.0f, 0.f, 510.f, &worldspawn_group},
      sunlight{this, {"sunlight", "sun_light"}, 0.0, &worldspawn_group},
      sunlight_color{this, {"sunlight_color", "sun_color"}, 255.0, 255.0, 255.0, &worldspawn_group},
      sun2{this, "sun2", 0.0, &worldspawn_group},
      sun2_color{this, "sun2_color", 255.0, 255.0, 255.0, &worldspawn_group},
      sunlight2{this, "sunlight2", 0.0, &worldspawn_group},
      sunlight2_color{this, {"sunlight2_color", "sunlight_color2"}, 255.0, 255.0, 255.0, &worldspawn_group},
      sunlight3{this, "sunlight3", 0.0, &worldspawn_group},
      sunlight3_color{this, {"sunlight3_color", "sunlight_color3"}, 255.0, 255.0, 255.0, &worldspawn_group},
      sunlight_dirt{this, "sunlight_dirt", 0.0, &worldspawn_group},
      sunlight2_dirt{this, "sunlight2_dirt", 0.0, &worldspawn_group},
      // NOTE: the default mangle needs to be in direction vector form, not euler angle
      sunvec{this, {"sunlight_mangle", "sun_mangle", "sun_angle"}, 0.0, 0.0, -1.0, &worldspawn_group},
      sun2vec{this, "sun2_mangle", 0.0, 0.0, -1.0, &worldspawn_group},
      sun_deviance{this, "sunlight_penumbra", 0.0, 0.0, 180.0, &worldspawn_group},
      sky_surface{this, {"sky_surface", "sun_surface"}, 0, 0, 0, &worldspawn_group},
      surflight_radiosity{this, "surflight_radiosity", SURFLIGHT_Q1, &worldspawn_group,
          "whether to use Q1-style surface subdivision (0) or Q2-style surface radiosity"}
{
}

// light_settings::setting_soft

bool light_settings::setting_soft::parse(const std::string &setting_name, parser_base_t &parser, source source)
{
    if (!parser.parse_token(PARSE_PEEK)) {
        return false;
    }

    try {
        int32_t f = static_cast<int32_t>(std::stoull(parser.token));

        set_value(f, source);

        parser.parse_token();

        return true;
    } catch (std::exception &) {
        // if we didn't provide a (valid) number, then
        // assume it's meant to be the default of -1
        set_value(-1, source);
        return true;
    }
}

std::string light_settings::setting_soft::format() const
{
    return "[n]";
}

// light_settings::setting_extra

bool light_settings::setting_extra::parse(const std::string &setting_name, parser_base_t &parser, source source)
{
    if (setting_name.back() == '4') {
        set_value(4, source);
    } else {
        set_value(2, source);
    }

    return true;
}

std::string light_settings::setting_extra::string_value() const
{
    return std::to_string(_value);
};

std::string light_settings::setting_extra::format() const
{
    return "";
};

void light_settings::CheckNoDebugModeSet()
{
    if (debugmode != debugmodes::none) {
        Error("Only one debug mode is allowed at a time");
    }
}

setting_group worldspawn_group{"Overridable worldspawn keys", 500, expected_source::worldspawn};
setting_group output_group{"Output format options", 30, expected_source::commandline};
setting_group debug_group{"Debug modes", 40, expected_source::commandline};
setting_group postprocessing_group{"Postprocessing options", 50, expected_source::commandline};
setting_group experimental_group{"Experimental options", 60, expected_source::commandline};

light_settings::light_settings()
    : surflight_dump{this, "surflight_dump", false, &debug_group, "dump surface lights to a .map file"},
      surflight_subdivide{
          this, "surflight_subdivide", 128.0, 1.0, 2048.0, &performance_group, "surface light subdivision size"},
      onlyents{this, "onlyents", false, &output_group, "only update entities"},
      write_normals{this, "wrnormals", false, &output_group, "output normals, tangents and bitangents in a BSPX lump"},
      novanilla{this, "novanilla", false, &experimental_group, "implies -bspxlit; don't write vanilla lighting"},
      gate{this, "gate", LIGHT_EQUAL_EPSILON, &performance_group, "cutoff lights at this brightness level"},
      sunsamples{this, "sunsamples", 64, 8, 2048, &performance_group, "set samples for _sunlight2, default 64"},
      arghradcompat{this, "arghradcompat", false, &output_group, "enable compatibility for Arghrad-specific keys"},
      nolighting{this, "nolighting", false, &output_group, "don't output main world lighting (Q2RTX)"},
      debugface{this, "debugface", std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN(),
          std::numeric_limits<float>::quiet_NaN(), &debug_group, ""},
      debugvert{this, "debugvert", std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN(),
          std::numeric_limits<float>::quiet_NaN(), &debug_group, ""},
      highlightseams{this, "highlightseams", false, &debug_group, ""},
      soft{this, "soft", 0, -1, std::numeric_limits<int32_t>::max(), &postprocessing_group,
          "blurs the lightmap. specify n to blur radius in samples, otherwise auto"},
      radlights{this, "radlights", "\"filename.rad\"", &experimental_group,
          "loads a <surfacename> <r> <g> <b> <intensity> file"},
      lightmap_scale{
          this, "lightmap_scale", 0, &experimental_group, "force change lightmap scale; vanilla engines only allow 16"},
      extra{
          this, {"extra", "extra4"}, 1, &performance_group, "supersampling; 2x2 (extra) or 4x4 (extra4) respectively"},
      emissivequality{this, "emissivequality", emissivequality_t::LOW,
          {{"LOW", emissivequality_t::LOW}, {"MEDIUM", emissivequality_t::MEDIUM}, {"HIGH", emissivequality_t::HIGH}},
          &performance_group,
          "low = one point in the center of the face, med = center + all verts, high = spread points out for antialiasing"},
      visapprox{this, "visapprox", visapprox_t::AUTO,
          {{"auto", visapprox_t::AUTO}, {"none", visapprox_t::NONE}, {"vis", visapprox_t::VIS},
              {"rays", visapprox_t::RAYS}},
          &debug_group,
          "change approximate visibility algorithm. auto = choose default based on format. vis = use BSP vis data (slow but precise). rays = use sphere culling with fired rays (fast but may miss faces)"},
      lit{this, "lit", [&](source) { write_litfile |= lightfile::external; }, &output_group, "write .lit file"},
      lit2{this, "lit2", [&](source) { write_litfile = lightfile::lit2; }, &experimental_group, "write .lit2 file"},
      bspxlit{this, "bspxlit", [&](source) { write_litfile |= lightfile::bspx; }, &experimental_group,
          "writes rgb data into the bsp itself"},
      lux{this, "lux", [&](source) { write_luxfile |= lightfile::external; }, &experimental_group, "write .lux file"},
      bspxlux{this, "bspxlux", [&](source) { write_luxfile |= lightfile::bspx; }, &experimental_group,
          "writes lux data into the bsp itself"},
      bspxonly{this, "bspxonly",
          [&](source source) {
              write_litfile = lightfile::bspx;
              write_luxfile = lightfile::bspx;
              novanilla.set_value(true, source);
          },
          &experimental_group, "writes both rgb and directions data *only* into the bsp itself"},
      bspx{this, "bspx",
          [&](source source) {
              write_litfile = lightfile::bspx;
              write_luxfile = lightfile::bspx;
          },
          &experimental_group, "writes both rgb and directions data into the bsp itself"},
      world_units_per_luxel{
          this, "world_units_per_luxel", 0, 0, 1024, &output_group, "enables output of DECOUPLED_LM BSPX lump"},
      litonly{this, "litonly", false, &output_group, "only write .lit file, don't modify BSP"},
      nolights{this, "nolights", false, &output_group, "ignore light entities (only sunlight/minlight)"},
      facestyles{this, "facestyles", 4, &output_group, "max amount of styles per face; requires BSPX lump if > 4"},
      exportobj{this, "exportobj", false, &output_group, "export an .OBJ for inspection"},
      lmshift{this, "lmshift", 4, &output_group,
          "force a specified lmshift to be applied to the entire map; this is useful if you want to re-light a map with higher quality BSPX lighting without the sources. Will add the LMSHIFT lump to the BSP."},
      lightgrid{this, "lightgrid", false, &experimental_group,
          "generates a lightgrid and writes it to a bspx lump (LIGHTGRID_OCTREE)"},
      lightgrid_dist{this, "lightgrid_dist", 32.f, 32.f, 32.f, &experimental_group,
          "distance between lightgrid sample points, in world units. controls lightgrid size."},
      lightgrid_format{this, "lightgrid_format", lightgrid_format_t::OCTREE, {{"octree", lightgrid_format_t::OCTREE}},
          &experimental_group, "lightgrid BSPX lump to use"},

      dirtdebug{this, {"dirtdebug", "debugdirt"},
          [&](source) {
              CheckNoDebugModeSet();
              debugmode = debugmodes::dirt;
          },
          &debug_group, "only save the AO values to the lightmap"},

      bouncedebug{this, "bouncedebug",
          [&](source) {
              CheckNoDebugModeSet();
              debugmode = debugmodes::bounce;
          },
          &debug_group, "only save bounced lighting to the lightmap"},

      bouncelightsdebug{this, "bouncelightsdebug",
          [&](source) {
              CheckNoDebugModeSet();
              debugmode = debugmodes::bouncelights;
          },
          &debug_group, "only save bounced emitters lighting to the lightmap"},

      phongdebug{this, "phongdebug",
          [&](source) {
              CheckNoDebugModeSet();
              debugmode = debugmodes::phong;
          },
          &debug_group, "only save phong normals to the lightmap"},

      phongdebug_obj{this, "phongdebug_obj",
          [&](source) {
              CheckNoDebugModeSet();
              debugmode = debugmodes::phong_obj;
          },
          &debug_group, "save map as .obj with phonged normals"},

      debugoccluded{this, "debugoccluded",
          [&](source) {
              CheckNoDebugModeSet();
              debugmode = debugmodes::debugoccluded;
          },
          &debug_group, "save light occlusion data to lightmap"},

      debugneighbours{this, "debugneighbours",
          [&](source) {
              CheckNoDebugModeSet();
              debugmode = debugmodes::debugneighbours;
          },
          &debug_group, "save neighboring faces data to lightmap (requires -debugface)"},

      debugmottle{this, "debugmottle",
          [&](source) {
              CheckNoDebugModeSet();
              debugmode = debugmodes::mottle;
          },
          &debug_group, "save mottle pattern to lightmap"}
{
}

void light_settings::set_parameters(int argc, const char **argv)
{
    common_settings::set_parameters(argc, argv);
    program_description = "light compiles lightmap data for BSPs\n\n";
    remainder_name = "mapname.bsp";
}

void light_settings::initialize(int argc, const char **argv)
{
    try {
        token_parser_t p(argc - 1, argv + 1, {"command line"});
        auto remainder = parse(p);

        if (remainder.size() <= 0 || remainder.size() > 1) {
            print_help();
        }

        sourceMap = remainder[0];
    } catch (parse_exception &ex) {
        logging::print(ex.what());
        print_help();
    }
}

void light_settings::postinitialize(int argc, const char **argv)
{
    if (gate.value() > 1) {
        logging::print("WARNING: -gate value greater than 1 may cause artifacts\n");
    }

    if (radlights.is_changed()) {
        if (!ParseLightsFile(*radlights.values().begin())) {
            logging::print("Unable to read surface lights file {}\n", *radlights.values().begin());
        }
    }

    if (soft.value() == -1) {
        switch (extra.value()) {
            case 2: soft.set_value(1, settings::source::COMMANDLINE); break;
            case 4: soft.set_value(2, settings::source::COMMANDLINE); break;
            default: soft.set_value(0, settings::source::COMMANDLINE); break;
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
        light_options.dirt.set_value(true, settings::source::COMMANDLINE);
    } else if (debugmode == debugmodes::bounce || debugmode == debugmodes::bouncelights) {
        light_options.bounce.set_value(true, settings::source::COMMANDLINE);
    } else if (debugmode == debugmodes::debugneighbours && !debugface.is_changed()) {
        FError("-debugneighbours without -debugface specified\n");
    }

    if (light_options.q2rtx.value()) {
        if (!light_options.nolighting.is_changed()) {
            light_options.nolighting.set_value(true, settings::source::GAME_TARGET);
        }

        if (!light_options.write_normals.is_changed()) {
            light_options.write_normals.set_value(true, settings::source::GAME_TARGET);
        }
    }

    // upgrade to uint16 if facestyles is specified
    if (light_options.facestyles.value() > MAXLIGHTMAPS && !light_options.compilerstyle_max.is_changed()) {
        light_options.compilerstyle_max.set_value(INVALID_LIGHTSTYLE, settings::source::COMMANDLINE);
    }

    common_settings::postinitialize(argc, argv);
}

void light_settings::reset()
{
    common_settings::reset();

    sourceMap = fs::path();

    write_litfile = lightfile::none;
    write_luxfile = lightfile::none;
    debugmode = debugmodes::none;
}
} // namespace settings

settings::light_settings light_options;

void FixupGlobalSettings()
{
    // NOTE: This is confusing.. Setting "dirt" "1" implies "minlight_dirt" "1"
    // (and sunlight_dir/sunlight2_dirt as well), unless those variables were
    // set by the user to "0".
    //
    // We can't just default "minlight_dirt" to "1" because that would enable
    // dirtmapping by default.

    if (light_options.dirt.value()) {
        if (!light_options.minlight_dirt.is_changed()) {
            light_options.minlight_dirt.set_value(true, settings::source::COMMANDLINE);
        }
        if (!light_options.sunlight_dirt.is_changed()) {
            light_options.sunlight_dirt.set_value(1, settings::source::COMMANDLINE);
        }
        if (!light_options.sunlight2_dirt.is_changed()) {
            light_options.sunlight2_dirt.set_value(1, settings::source::COMMANDLINE);
        }
    }
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

struct face_texture_cache
{
    const img::texture *image;
    qvec3b averageColor;
    qvec3f bounceColor;
};

static std::vector<face_texture_cache> face_textures;

const img::texture *Face_Texture(const mbsp_t *bsp, const mface_t *face)
{
    return face_textures[face - bsp->dfaces.data()].image;
}

const qvec3b &Face_LookupTextureColor(const mbsp_t *bsp, const mface_t *face)
{
    return face_textures[face - bsp->dfaces.data()].averageColor;
}

const qvec3f &Face_LookupTextureBounceColor(const mbsp_t *bsp, const mface_t *face)
{
    return face_textures[face - bsp->dfaces.data()].bounceColor;
}

static void CacheTextures(const mbsp_t &bsp)
{
    face_textures.resize(bsp.dfaces.size());

    for (size_t i = 0; i < bsp.dfaces.size(); i++) {
        const char *name = Face_TextureName(&bsp, &bsp.dfaces[i]);

        if (!name || !*name) {
            face_textures[i] = {nullptr, {127}, {0.5}};
        } else {
            auto tex = img::find(name);
            auto &ext = extended_texinfo_flags[bsp.dfaces[i].texinfo];
            auto avg = ext.surflight_color.value_or(tex->averageColor);
            face_textures[i] = {tex, avg,
                // lerp between gray and the texture color according to `bouncecolorscale` (0 = use gray, 1 = use
                // texture color)
                mix(qvec3f{127}, qvec3f(avg), light_options.bouncecolorscale.value()) / 255.0};
        }
    }
}

static void CreateLightmapSurfaces(mbsp_t *bsp)
{
    light_surfaces = std::make_unique<lightsurf_t[]>(bsp->dfaces.size());
    light_surfaces_span = { light_surfaces.get(), light_surfaces.get() + bsp->dfaces.size() };
    logging::funcheader();
    logging::parallel_for(static_cast<size_t>(0), bsp->dfaces.size(), [&bsp](size_t i) {
        auto facesup = faces_sup.empty() ? nullptr : &faces_sup[i];
        auto facesup_decoupled = facesup_decoupled_global.empty() ? nullptr : &facesup_decoupled_global[i];
        auto face = &bsp->dfaces[i];

        /* One extra lightmap is allocated to simplify handling overflow */
        if (!light_options.litonly.value()) {
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

                if (facesup_decoupled) {
                    facesup_decoupled->offset = -1;
                }
            }
        }

        light_surfaces[i] = CreateLightmapSurface(bsp, face, facesup, facesup_decoupled, light_options);
    });
}

static void ClearLightmapSurfaces()
{
    logging::funcheader();
    light_surfaces.reset();
    light_surfaces_span = {};
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

    if (light_options.lightmap_scale.is_changed()) {
        WorldEnt().set("_lightmap_scale", light_options.lightmap_scale.string_value());
    }

    float lightmapscale = WorldEnt().get_int("_lightmap_scale");
    if (!lightmapscale)
        lightmapscale = LMSCALE_DEFAULT; /* the default */
    if (lightmapscale <= 0)
        FError("lightmap scale is 0 or negative\n");
    if (light_options.lightmap_scale.is_changed() || lightmapscale != LMSCALE_DEFAULT)
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
    world->shadow.set_value(1.0f, settings::source::MAP); /* world always casts shadows */
    world->phong_angle.copy_from(light_options.phongangle);
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
        info->set_settings(*entdict, settings::source::MAP);

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

/*
 * =============
 *  LightWorld
 * =============
 */
static void LightWorld(bspdata_t *bspdata, const fs::path &source, bool forcedscale)
{
    logging::funcheader();

    mbsp_t &bsp = std::get<mbsp_t>(bspdata->bsp);

    ClearLightmapSurfaces();

    if (forcedscale) {
        bspdata->bspx.entries.erase("LMSHIFT");
    } else if (light_options.lmshift.is_changed()) {
        // if we forcefully specified an lmshift lump, we have to generate one.
        bspdata->bspx.entries.erase("LMSHIFT");

        std::vector<uint8_t> shifts(bsp.dfaces.size());

        for (auto &shift : shifts) {
            shift = light_options.lmshift.value();
        }

        bspdata->bspx.transfer("LMSHIFT", shifts);
    }

    auto lmshift_lump = bspdata->bspx.entries.find("LMSHIFT");

    if (lmshift_lump == bspdata->bspx.entries.end() && light_options.write_litfile != lightfile::lit2 &&
        light_options.facestyles.value() <= 4) {
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

    // decoupled lightmaps
    facesup_decoupled_global.clear();
    if (light_options.world_units_per_luxel.is_changed()) {
        facesup_decoupled_global.resize(bsp.dfaces.size());
    }

    CalculateVertexNormals(&bsp);

    // create lightmap surfaces
    CreateLightmapSurfaces(&bsp);

    const bool bouncerequired =
        light_options.bounce.value() &&
        (light_options.debugmode == debugmodes::none || light_options.debugmode == debugmodes::bounce ||
            light_options.debugmode == debugmodes::bouncelights); // mxd

    MakeRadiositySurfaceLights(light_options, &bsp);
    UpdateEmissiveLightSurfacesList();

    logging::header("Direct Lighting"); // mxd
    logging::parallel_for(static_cast<size_t>(0), bsp.dfaces.size(), [&bsp](size_t i) {
        if (Face_IsLightmapped(&bsp, &bsp.dfaces[i])) {
#if defined(HAVE_EMBREE) && defined(__SSE2__)
            _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
#endif
            DirectLightFace(&bsp, light_surfaces[i], light_options);
        }
    });

    if (bouncerequired && !light_options.nolighting.value()) {

        for (size_t i = 0; i < light_options.bounce.value(); i++) {

            if (!MakeBounceLights(light_options, &bsp, i)) {
                logging::header("No bounces; indirect lighting halted");
                break;
            }
            UpdateEmissiveLightSurfacesList();

            logging::header(fmt::format("Indirect Lighting (pass {0})", i).c_str()); // mxd

            logging::parallel_for(static_cast<size_t>(0), bsp.dfaces.size(), [i, &bsp](size_t f) {
                if (Face_IsLightmapped(&bsp, &bsp.dfaces[f])) {
    #if defined(HAVE_EMBREE) && defined(__SSE2__)
                    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    #endif

                    IndirectLightFace(&bsp, light_surfaces[f], light_options, i);
                }
            });
        }
    }

    if (!light_options.nolighting.value()) {
        logging::header("Post-Processing"); // mxd
        logging::parallel_for(static_cast<size_t>(0), bsp.dfaces.size(), [&bsp](size_t i) {
            if (Face_IsLightmapped(&bsp, &bsp.dfaces[i])) {
#if defined(HAVE_EMBREE) && defined(__SSE2__)
                _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
#endif

                PostProcessLightFace(&bsp, light_surfaces[i], light_options);
            }
        });
    }

    SaveLightmapSurfaces(bspdata, source);

    // kill this stuff if its somehow found.
    bspdata->bspx.entries.erase("LMSTYLE16");
    bspdata->bspx.entries.erase("LMSTYLE");
    bspdata->bspx.entries.erase("LMOFFSET");
    bspdata->bspx.entries.erase("DECOUPLED_LM");

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
                if (j < MAXLIGHTMAPS && bsp.dfaces[i].styles[j] != faces_sup[i].styles[j]) {
                    needstyles = true;
                }
                if (maxstyle < faces_sup[i].styles[j])
                    maxstyle = faces_sup[i].styles[j];
            }
            if (stylesperface < j)
                stylesperface = j;
        }

        if (stylesperface >= light_options.facestyles.value()) {
            logging::print(
                "WARNING: styles per face {} exceeds compiler-set max styles {}; use `-facestyles` if you need more.\n",
                stylesperface, light_options.facestyles.value());
            stylesperface = light_options.facestyles.value();
        }

        needstyles |= (stylesperface > 4);

        logging::print("max {} styles per face, {} used{}\n", light_options.facestyles.value(), stylesperface,
            maxstyle >= INVALID_LIGHTSTYLE_OLD ? ", 16bit lightstyles" : "");

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

                logging::print("LMSTYLE16 BSPX lump written\n");
                bspdata->bspx.transfer("LMSTYLE16", styles_mem);
            } else {
                /*original LMSTYLE lump was just for different lmshift info*/
                std::vector<uint8_t> styles_mem(stylesperface * bsp.dfaces.size());

                for (size_t i = 0, k = 0; i < bsp.dfaces.size(); i++) {
                    for (size_t j = 0; j < stylesperface; j++, k++) {
                        styles_mem[k] = faces_sup[i].styles[j] == INVALID_LIGHTSTYLE ? INVALID_LIGHTSTYLE_OLD
                                                                                     : faces_sup[i].styles[j];
                    }
                }

                logging::print("LMSTYLE BSPX lump written\n");
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

            logging::print("LMOFFSET BSPX lump written\n");
            bspdata->bspx.transfer("LMOFFSET", offsets_mem);
        }
    }

    if (!facesup_decoupled_global.empty()) {
        std::vector<uint8_t> mem(sizeof(bspx_decoupled_lm_perface) * bsp.dfaces.size());

        omemstream stream(mem.data(), mem.size(), std::ios_base::out | std::ios_base::binary);
        stream << endianness<std::endian::little>;

        for (size_t i = 0; i < bsp.dfaces.size(); i++) {
            stream <= facesup_decoupled_global[i];
        }

        logging::print("DECOUPLED_LM BSPX lump written\n");
        bspdata->bspx.transfer("DECOUPLED_LM", mem);
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

    Json::Value j;

    texinfofile >> j;

    for (auto &key : j.getMemberNames()) {
        size_t index = std::stoull(key);
        if (index >= bsp->texinfo.size()) {
            logging::print("WARNING: Extended texinfo flags in {} does not match bsp, ignoring\n", filename);
            memset(extended_texinfo_flags.data(), 0, bsp->texinfo.size() * sizeof(surfflags_t));
            return;
        }

        auto &val = j[key];
        auto &flags = extended_texinfo_flags[index];

        if (val.isMember("is_nodraw")) {
            flags.is_nodraw = val["is_nodraw"].as<bool>();
        }
        if (val.isMember("is_hint")) {
            flags.is_hint = val["is_hint"].as<bool>();
        }
        if (val.isMember("is_hintskip")) {
            flags.is_hintskip = val["is_hintskip"].as<bool>();
        }
        if (val.isMember("no_dirt")) {
            flags.no_dirt = val["no_dirt"].as<bool>();
        }
        if (val.isMember("no_shadow")) {
            flags.no_shadow = val["no_shadow"].as<bool>();
        }
        if (val.isMember("no_bounce")) {
            flags.no_bounce = val["no_bounce"].as<bool>();
        }
        if (val.isMember("no_minlight")) {
            flags.no_minlight = val["no_minlight"].as<bool>();
        }
        if (val.isMember("no_expand")) {
            flags.no_expand = val["no_expand"].as<bool>();
        }
        if (val.isMember("no_phong")) {
            flags.no_expand = val["no_phong"].as<bool>();
        }
        if (val.isMember("light_ignore")) {
            flags.light_ignore = val["light_ignore"].as<bool>();
        }
        if (val.isMember("surflight_rescale")) {
            flags.surflight_rescale = val["surflight_rescale"].as<bool>();
        }
        if (val.isMember("surflight_style")) {
            flags.surflight_style = val["surflight_style"].as<int32_t>();
        }
        if (val.isMember("surflight_targetname")) {
            flags.surflight_targetname = val["surflight_targetname"].as<std::string>();
        }
        if (val.isMember("surflight_color")) {
            flags.surflight_color = from_json<int, 3>(val["surflight_color"]);
        }
        if (val.isMember("surflight_minlight_scale")) {
            flags.surflight_minlight_scale = val["surflight_minlight_scale"].as<float>();
        }
        if (val.isMember("phong_angle")) {
            flags.phong_angle = val["phong_angle"].as<float>();
        }
        if (val.isMember("phong_angle_concave")) {
            flags.phong_angle_concave = val["phong_angle_concave"].as<float>();
        }
        if (val.isMember("phong_group")) {
            flags.phong_group = val["phong_group"].as<int>();
        }
        if (val.isMember("minlight")) {
            flags.minlight = val["minlight"].as<float>();
        }
        if (val.isMember("maxlight")) {
            flags.maxlight = val["maxlight"].as<float>();
        }
        if (val.isMember("minlight_color")) {
            flags.minlight_color = from_json<int, 3>(val["minlight_color"]);
        }
        if (val.isMember("light_alpha")) {
            flags.light_alpha = val["light_alpha"].as<float>();
        }
        if (val.isMember("light_twosided")) {
            flags.light_twosided = val["light_twosided"].as<bool>();
        }
        if (val.isMember("lightcolorscale")) {
            flags.lightcolorscale = val["lightcolorscale"].as<float>();
        }
        if (val.isMember("surflight_group")) {
            flags.surflight_group = val["surflight_group"].as<int32_t>();
        }
        if (val.isMember("world_units_per_luxel")) {
            flags.world_units_per_luxel = val["world_units_per_luxel"].as<float>();
        }
        if (val.isMember("object_channel_mask")) {
            flags.object_channel_mask = val["object_channel_mask"].as<int32_t>();
        }
        if (val.isMember("surflight_minlight_scale")) {
            flags.surflight_minlight_scale = val["surflight_minlight_scale"].as<float>();
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
        ewt::print(f, "v {:.9} {:.9} {:.9}\n", pos[0], pos[1], pos[2]);
        ewt::print(f, "vn {:.9} {:.9} {:.9}\n", normal[0], normal[1], normal[2]);
    }

    f << "f";
    for (int i = 0; i < face->numedges; i++) {
        // .obj vertexes start from 1
        // .obj faces are CCW, quake is CW, so reverse the order
        const int vertindex = *vertcount + (face->numedges - 1 - i) + 1;
        ewt::print(f, " {}//{}", vertindex, vertindex);
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
    if (!light_options.debugface.is_changed())
        return;

    const mface_t *f = Face_NearestCentroid(bsp, light_options.debugface.value());
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
    if (!light_options.debugvert.is_changed())
        return;

    int v = Vertex_NearestPoint(bsp, light_options.debugvert.value());

    logging::funcprint("dumping vert {} at {}\n", v, bsp->dvertexes[v]);

    dump_vertnum = v;
}

static void SetLitNeeded()
{
    if (!light_options.write_litfile) {
        if (light_options.novanilla.value()) {
            light_options.write_litfile = lightfile::bspx;
            logging::print("Colored light entities/settings detected: "
                           "bspxlit output enabled.\n");
        } else {
            light_options.write_litfile = lightfile::external;
            logging::print("Colored light entities/settings detected: "
                           ".lit output enabled.\n");
        }
    }
}

static void CheckLitNeeded(const settings::worldspawn_keys &cfg)
{
    // check lights
    for (const auto &light : GetLights()) {
        if (!qv::epsilonEqual(vec3_white, light->color.value(), LIGHT_EQUAL_EPSILON) ||
            light->projectedmip != nullptr) { // mxd. Projected mips could also use .lit output
            SetLitNeeded();
            return;
        }
    }

    // check global settings
    if (cfg.bouncecolorscale.value() != 0 ||
        !qv::epsilonEqual(cfg.minlight_color.value(), vec3_white, LIGHT_EQUAL_EPSILON) ||
        !qv::epsilonEqual(cfg.sunlight_color.value(), vec3_white, LIGHT_EQUAL_EPSILON) ||
        !qv::epsilonEqual(cfg.sun2_color.value(), vec3_white, LIGHT_EQUAL_EPSILON) ||
        !qv::epsilonEqual(cfg.sunlight2_color.value(), vec3_white, LIGHT_EQUAL_EPSILON) ||
        !qv::epsilonEqual(cfg.sunlight3_color.value(), vec3_white, LIGHT_EQUAL_EPSILON)) {
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

static void PrintLights()
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

/**
 * Resets globals in this file
 */
static void ResetLight()
{
    dirt_in_use = false;
    ClearLightmapSurfaces();
    faces_sup.clear();
    facesup_decoupled_global.clear();

    all_uncompressed_vis.clear();
    modelinfo.clear();
    tracelist.clear();
    selfshadowlist.clear();
    shadowworldonlylist.clear();
    switchableshadowlist.clear();

    extended_texinfo_flags.clear();

    dump_facenum = -1;
    dump_vertnum = -1;
}

void light_reset()
{
    ResetLightEntities();
    ResetLight();
    ResetLtFace();
    ResetPhong();
    ResetSurflight();
    ResetEmbree();

    light_options.reset();
}

/*
 * ==================
 * main
 * light modelfile
 * ==================
 */
int light_main(int argc, const char **argv)
{
    light_reset();

    bspdata_t bspdata;

    light_options.preinitialize(argc, argv);
    light_options.initialize(argc, argv);

    auto start = I_FloatTime();
    fs::path source = light_options.sourceMap;

    logging::init(
        fs::path(source).replace_filename(source.stem().string() + "-light").replace_extension("log"), light_options);

    // delete previous litfile
    if (!light_options.onlyents.value()) {
        source.replace_extension("lit");
        remove(source);
    }

    source.replace_extension("rad");
    if (source != "lights.rad")
        ParseLightsFile("lights.rad"); // generic/default name
    ParseLightsFile(source); // map-specific file name

    source.replace_extension("bsp");
    LoadBSPFile(source, &bspdata);

    bspdata.version->game->init_filesystem(source, light_options);

    ConvertBSPFormat(&bspdata, &bspver_generic);

    mbsp_t &bsp = std::get<mbsp_t>(bspdata.bsp);

    // mxd. Use 1.0 rangescale as a default to better match with qrad3/arghrad
    if (bspdata.loadversion->game->id == GAME_QUAKE_II) {
        if (!light_options.rangescale.is_changed()) {
            light_options.rangescale.set_value(1.0, settings::source::GAME_TARGET);
        }
        if (!light_options.bouncecolorscale.is_changed()) {
            light_options.bouncecolorscale.set_value(0.5, settings::source::GAME_TARGET);
        }
        if (!light_options.surflightscale.is_changed()) {
            light_options.surflightscale.set_value(0.65f, settings::source::GAME_TARGET);
        }
        if (!light_options.surflightskyscale.is_changed()) {
            light_options.surflightskyscale.set_value(0.65f, settings::source::GAME_TARGET);
        }
        if (!light_options.bouncescale.is_changed()) {
            light_options.bouncescale.set_value(0.85f, settings::source::GAME_TARGET);
        }
        if (!light_options.bounce.is_changed()) {
            light_options.bounce.set_value(true, settings::source::GAME_TARGET);
        }
        if (!light_options.surflight_radiosity.is_changed()) {
            light_options.surflight_radiosity.set_value(SURFLIGHT_RAD, settings::source::GAME_TARGET);
        }
        if (!light_options.bouncestyled.is_changed()) {
            light_options.bouncestyled.set_value(true, settings::source::GAME_TARGET);
        }
    }

    // check vis approx type
    if (light_options.visapprox.value() == visapprox_t::AUTO) {
        if (!bsp.dvis.bits.empty()) {
            light_options.visapprox.set_value(visapprox_t::VIS, settings::source::DEFAULT);
        } else {
            light_options.visapprox.set_value(visapprox_t::RAYS, settings::source::DEFAULT);
        }
    }

    img::load_textures(&bsp, light_options);

    LoadExtendedTexinfoFlags(source, &bsp);

    CacheTextures(bsp);

    LoadEntities(light_options, &bsp);

    light_options.postinitialize(argc, argv);

    all_uncompressed_vis = DecompressAllVis(&bsp, true);
    FindModelInfo(&bsp);

    FindDebugFace(&bsp);
    FindDebugVert(&bsp);

    Embree_TraceInit(&bsp);

    if (light_options.debugmode == debugmodes::phong_obj) {
        CalculateVertexNormals(&bsp);
        source.replace_extension("obj");
        ExportObj(source, &bsp);

        logging::close();
        return 0;
    }

    SetupLights(light_options, &bsp);

    // PrintLights();

    if (!light_options.onlyents.value()) {
        if (!bspdata.loadversion->game->has_rgb_lightmap) {
            CheckLitNeeded(light_options);
        }

        SetupDirt(light_options);

        LightWorld(&bspdata, source, light_options.lightmap_scale.is_changed());

        LightGrid(&bspdata);

        ClearLightmapSurfaces();

        // invalidate normals
        bspdata.bspx.entries.erase("FACENORMALS");

        if (light_options.write_normals.value()) {
            WriteNormals(bsp, bspdata);
        }

        if (light_options.write_litfile == lightfile::lit2) {
            return 0; // run away before any files are written
        }
    }

    /* -novanilla + internal lighting = no grey lightmap */
    if (light_options.novanilla.value() && (light_options.write_litfile & lightfile::bspx)) {
        bsp.dlightdata.clear();
    }

    if (light_options.exportobj.value()) {
        ExportObj(fs::path{source}.replace_extension(".obj"), &bsp);
    }

    WriteEntitiesToString(light_options, &bsp);
    /* Convert data format back if necessary */
    ConvertBSPFormat(&bspdata, bspdata.loadversion);

    if (!light_options.litonly.value()) {
        WriteBSPFile(source, &bspdata);
    }

    auto end = I_FloatTime();
    logging::print("{:.3} seconds elapsed\n", (end - start));
#if 0
    logging::print("\n");
    logging::print("stats:\n");
    logging::print("{} lights tested, {} hits per sample point\n",
        static_cast<float>(total_light_rays) / static_cast<float>(total_samplepoints),
        static_cast<float>(total_light_ray_hits) / static_cast<float>(total_samplepoints));
    logging::print("{} surface lights tested, {} hits per sample point\n",
        static_cast<float>(total_surflight_rays) / static_cast<float>(total_samplepoints),
        static_cast<float>(total_surflight_ray_hits) / static_cast<float>(total_samplepoints)); // mxd
    logging::print("{} bounce lights tested, {} hits per sample point\n",
        static_cast<float>(total_bounce_rays) / static_cast<float>(total_samplepoints),
        static_cast<float>(total_bounce_ray_hits) / static_cast<float>(total_samplepoints));
#endif
    logging::print("{} empty lightmaps\n", static_cast<int>(fully_transparent_lightmaps));
    logging::close();

    return 0;
}

int light_main(const std::vector<std::string> &args)
{
    std::vector<const char *> argPtrs;
    for (const std::string &arg : args) {
        argPtrs.push_back(arg.data());
    }

    return light_main(argPtrs.size(), argPtrs.data());
}
