/*
Copyright (C) 1996-1997  Id Software, Inc.
Copyright (C) 1997       Greg Lewis

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

#include "mathlib.hh"
#include "bspfile.hh"
#include "entdata.h"
#include "parser.hh"
#include <string>
#include <variant>
#include <optional>
#include <string_view>

// this file declares some names that clash with names elsewhere in the project and lead to ODR violations
// (e.g. texdef_valve_t). For now just wrap everything in a namespace to avoid issues.
namespace mapfile
{

// main brush style; technically these can be mixed
enum class texcoord_style_t
{
    quaked,
    etp,
    valve_220,
    brush_primitives
};

// raw texdef values; the unchanged
// values in the .map
struct texdef_bp_t
{
    qmat<double, 2, 3> axis;
};

struct texdef_quake_ed_t
{
    qvec2d shift;
    double rotate;
    qvec2d scale;
};

struct texdef_valve_t : texdef_quake_ed_t, texdef_bp_t
{
};

struct texdef_etp_t : texdef_quake_ed_t
{
    bool tx2 = false;
};

// extra Q2 info
struct texinfo_quake2_t
{
    int contents;
    surfflags_t flags;
    int value;
};

template<typename T>
struct name_and_flag_t
{
    const char  *name;
    T           flag;
};

// extra SiN info
// TODO: move this stuff to gamedef in some way
enum class sin_contents_t : uint32_t
{
    SOLID = 1,
    WATER = 32,
    FENCE = 4,
    SLIME = 16,
    LAVA = 8,
    WINDOW = 2,
    MIST = 64,
    ORIGIN = 0x1000000,
    PLAYERCLIP = 0x10000,
    MONSTERCLIP = 0x20000,

    CURRENT_0 = 0x40000,
    CURRENT_90 = 0x80000,
    CURRENT_180 = 0x100000,
    CURRENT_270 = 0x200000,
    CURRENT_UP = 0x400000,
    CURRENT_DOWN = 0x800000,
    MONSTER = 0x2000000,
    CORPSE = 0x4000000,
    DETAIL = 0x8000000,
    TRANSLUCENT = 0x10000000,
    LADDER = 0x20000000
};

static constexpr name_and_flag_t<sin_contents_t> sin_contents_names[] = {
    {"solid",         sin_contents_t::SOLID},
    {"water",         sin_contents_t::WATER},
    {"fence",         sin_contents_t::FENCE},
    {"slime",         sin_contents_t::SLIME},
    {"lava",          sin_contents_t::LAVA},
    {"window",        sin_contents_t::WINDOW},
    {"mist",          sin_contents_t::MIST},
    {"origin",        sin_contents_t::ORIGIN},
    {"playerclip",    sin_contents_t::PLAYERCLIP},
    {"monsterclip",   sin_contents_t::MONSTERCLIP},

    {"current_0",     sin_contents_t::CURRENT_0},
    {"current_90",    sin_contents_t::CURRENT_90},
    {"current_180",   sin_contents_t::CURRENT_180},
    {"current_270",   sin_contents_t::CURRENT_270},
    {"current_up",    sin_contents_t::CURRENT_UP},
    {"current_dn",    sin_contents_t::CURRENT_DOWN},
    {"monster",       sin_contents_t::MONSTER},
    {"corpse",        sin_contents_t::CORPSE},
    {"detail",        sin_contents_t::DETAIL},
    {"translucent",   sin_contents_t::TRANSLUCENT},
    {"ladder",        sin_contents_t::LADDER}
};

enum class sin_surfflags_t : uint32_t
{
    HINT = 0x100,
    SKIP = 0x200,
    NOMERGE = 0x4000000,

    RANDOMANIMATE = 0x400000,
    ANIMATE = 0x800000,
    RNDTIME = 0x1000000,

    CONVEYOR = 0x40,
    HARDWAREONLY = 0x10000,
    DAMAGE = 0x20000,
    WEAK = 0x40000,
    NORMAL = 0x80000,
    ADD = 0x100000,
    RICOCHET = 0x800,

    SKY = 0x4,
    WARP = 0x8,
    NODRAW = 0x80,
    MASKED = 0x2,
    WAVY = 0x400,
    NONLIT = 0x10,
    
    TYPE_BIT0 = 0x8000000,
    TYPE_BIT1 = 0x10000000,
    TYPE_BIT2 = 0x20000000,
    TYPE_BIT3 = 0x40000000,
    TRANSLATE = 0x2000000,
    LIGHT = 0x1,
    PRELIT = 0x1000,
    NOFILTER = 0x20,
    MIRROR = 0x2000,
    CONSOLE = 0x4000,
    USECOLOR = 0x8000,
    ENVMAPPED = 0x200000
};

static constexpr name_and_flag_t<sin_surfflags_t> sin_surfflag_names[] = {
    {"hint",        sin_surfflags_t::HINT},
    {"skip",        sin_surfflags_t::SKIP},
    {"nomerge",     sin_surfflags_t::NOMERGE},

    {"random",      sin_surfflags_t::RANDOMANIMATE},
    {"animate",     sin_surfflags_t::ANIMATE},
    {"rndtime",     sin_surfflags_t::RNDTIME},

    {"conveyor",    sin_surfflags_t::CONVEYOR},
    {"hardwareonly",sin_surfflags_t::HARDWAREONLY},
    {"damage",      sin_surfflags_t::DAMAGE},
    {"weak",        sin_surfflags_t::WEAK},
    {"normal",      sin_surfflags_t::NORMAL},
    {"add",         sin_surfflags_t::ADD},
    {"ricochet",    sin_surfflags_t::RICOCHET},

    {"sky",         sin_surfflags_t::SKY},
    {"warping",     sin_surfflags_t::WARP},
    {"nodraw",      sin_surfflags_t::NODRAW},
    {"masked",      sin_surfflags_t::MASKED},
    {"wavy",        sin_surfflags_t::WAVY},
    {"nonlit",      sin_surfflags_t::NONLIT},
    {"surfbit0",    sin_surfflags_t::TYPE_BIT0},
    {"surfbit1",    sin_surfflags_t::TYPE_BIT1},
    {"surfbit2",    sin_surfflags_t::TYPE_BIT2},
    {"surfbit3",    sin_surfflags_t::TYPE_BIT3},
    {"translate",   sin_surfflags_t::TRANSLATE},
    {"light",       sin_surfflags_t::LIGHT},
    {"prelit",      sin_surfflags_t::PRELIT},
    {"nofilter",    sin_surfflags_t::NOFILTER},
    {"mirror",      sin_surfflags_t::MIRROR},
    {"console",     sin_surfflags_t::CONSOLE},
    {"usecolor",    sin_surfflags_t::USECOLOR},
    {"envmapped",   sin_surfflags_t::ENVMAPPED},
};

struct sin_modify_flag_t
{
    bool                                          add;
    std::variant<sin_contents_t, sin_surfflags_t> flag;
};

struct texinfo_sin_t
{
    std::vector<std::variant<sin_modify_flag_t, keyvalue_t>> objects;
};

// convert a plane to a texture axis; used by quaked
struct texture_axis_t
{
    qvec3d xv;
    qvec3d yv;
    qvec3d snapped_normal;

    // use_new_axis = !qbsp_options.oldaxis.value()
    constexpr texture_axis_t(const qplane3d &plane, bool use_new_axis = false)
        : xv(), // gcc C++20 bug workaround
          yv(),
          snapped_normal()
    {
        constexpr qvec3d baseaxis[18] = {
            {0, 0, 1}, {1, 0, 0}, {0, -1, 0}, // floor
            {0, 0, -1}, {1, 0, 0}, {0, -1, 0}, // ceiling
            {1, 0, 0}, {0, 1, 0}, {0, 0, -1}, // west wall
            {-1, 0, 0}, {0, 1, 0}, {0, 0, -1}, // east wall
            {0, 1, 0}, {1, 0, 0}, {0, 0, -1}, // south wall
            {0, -1, 0}, {1, 0, 0}, {0, 0, -1} // north wall
        };

        double best = 0;
        size_t bestaxis = 0;

        for (size_t i = 0; i < 6; i++) {
            double dot = qv::dot(plane.normal, baseaxis[i * 3]);

            if (dot > best || (dot == best && use_new_axis)) {
                best = dot;
                bestaxis = i;
            }
        }

        xv = baseaxis[bestaxis * 3 + 1];
        yv = baseaxis[bestaxis * 3 + 2];
        snapped_normal = baseaxis[bestaxis * 3];
    }
};

// a single brush side from a .map
struct brush_side_t
{
    // source location
    parser_source_location location;

    // raw texture name
    std::string texture;
    // stores the original values that we loaded with, even if they were invalid.
    std::variant<texdef_quake_ed_t, texdef_valve_t, texdef_etp_t, texdef_bp_t> raw;
    // raw plane points
    std::array<qvec3d, 3> planepts;
    // additional game data, if available
    std::variant<std::monostate, texinfo_quake2_t, texinfo_sin_t> extended_info = {};

    // calculated texture vecs
    texvecf vecs;
    // calculated plane
    qplane3d plane;

    // TODO move to qv? keep local?
    static bool is_valid_texture_projection(const qvec3f &faceNormal, const qvec3f &s_vec, const qvec3f &t_vec);

    inline bool is_valid_texture_projection() const
    {
        return is_valid_texture_projection(plane.normal, vecs.row(0).xyz(), vecs.row(1).xyz());
    }

    void validate_texture_projection();

    // parsing
    // TODO: move to the individual texdefs?
    static texdef_bp_t parse_bp(parser_t &parser);
    static texdef_valve_t parse_valve_220(parser_t &parser);
    static texdef_quake_ed_t parse_quake_ed(parser_t &parser);

    bool parse_quark_comment(parser_t &parser);
    void parse_extended_texinfo(parser_t &parser);

    void set_texinfo(const texdef_quake_ed_t &texdef);
    void set_texinfo(const texdef_valve_t &texdef);
    void set_texinfo(const texdef_etp_t &texdef);
    void set_texinfo(const texdef_bp_t &texdef);

    void parse_texture_def(parser_t &parser, texcoord_style_t base_format);
    void parse_plane_def(parser_t &parser);

    void write_extended_info(std::ostream &stream);

    void write_texinfo(std::ostream &stream, const texdef_quake_ed_t &texdef);
    void write_texinfo(std::ostream &stream, const texdef_valve_t &texdef);
    void write_texinfo(std::ostream &stream, const texdef_etp_t &texdef);
    void write_texinfo(std::ostream &stream, const texdef_bp_t &texdef);

    void write(std::ostream &stream);

    void convert_to(texcoord_style_t style, const gamedef_t *game, const settings::common_settings &options);
};

struct brush_t
{
    parser_source_location location;
    texcoord_style_t base_format;
    std::vector<brush_side_t> faces;

    void parse_brush_face(parser_t &parser, texcoord_style_t base_format);

    void write(std::ostream &stream);

    void convert_to(texcoord_style_t style, const gamedef_t *game, const settings::common_settings &options);
};

struct map_entity_t
{
    parser_source_location location;
    entdict_t epairs;
    std::vector<brush_t> brushes;

    void parse_entity_dict(parser_t &parser);
    void parse_brush(parser_t &parser);
    bool parse(parser_t &parser);

    void write(std::ostream &stream);
};

struct map_file_t
{
    fs::path filename;

    std::vector<map_entity_t> entities;

    // if we loaded this in a specific game
    // it will be stored here.
    const gamedef_t *game = nullptr;

    void parse(parser_t &parser);

    void write(std::ostream &stream);

    void convert_to(texcoord_style_t style, const gamedef_t *game, const settings::common_settings &options);
};

map_file_t parse(std::string_view view, parser_source_location base_location);

} // namespace mapfile
