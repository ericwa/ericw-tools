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
    qmat<vec_t, 2, 3> axis;
};

struct texdef_quake_ed_t
{
    qvec2d shift;
    vec_t rotate;
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
    contentflags_t contents;
    surfflags_t flags;
    int value;
};

// convert a plane to a texture axis; used by quaked
struct texture_axis_t
{
    qvec3d xv;
    qvec3d yv;
    qvec3d snapped_normal;

    // use_new_axis = !qbsp_options.oldaxis.value()
    constexpr texture_axis_t(const qplane3d &plane, bool use_new_axis = false) :
        xv(), // gcc C++20 bug workaround
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

        vec_t best = 0;
        size_t bestaxis = 0;

        for (size_t i = 0; i < 6; i++) {
            vec_t dot = qv::dot(plane.normal, baseaxis[i * 3]);

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
    parser_source_location              location;

    // raw texture name
    std::string                                                                  texture;
    // stores the original values that we loaded with, even if they were invalid.
    std::variant<texdef_quake_ed_t, texdef_valve_t, texdef_etp_t, texdef_bp_t>   raw;
    // raw plane points
    std::array<qvec3d, 3>                                                        planepts;
    // Q2/Q3 data, if available
    std::optional<texinfo_quake2_t>                                              extended_info = std::nullopt;

    // calculated texture vecs
    texvecf                             vecs;
    // calculated plane
    qplane3d                            plane;

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
    std::vector<map_entity_t> entities;

    void parse(parser_t &parser);

    void write(std::ostream &stream);

    void convert_to(texcoord_style_t style, const gamedef_t *game, const settings::common_settings &options);
};
