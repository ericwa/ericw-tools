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

#include <common/entdata.h>
#include <common/parser.hh>
#include <common/log.hh>
#include <common/mapfile.hh>
#include <common/settings.hh>
#include <common/imglib.hh>
#include <common/bsputils.hh>
#include <common/json.hh>

#ifdef USE_LUA
extern "C"
{
    #include <lua.h>
    #include <lualib.h>
    #include <lauxlib.h>
}
#endif

// global map file state
map_file_t map_file;
const gamedef_t *current_game = nullptr;
settings::common_settings common_options;

map_file_t LoadMapOrEntFile(const fs::path &source)
{
    logging::funcheader();

    auto file = fs::load(source);
    map_file_t map;

    if (!file) {
        FError("Couldn't load map/entity file \"{}\".", source);
        return map;
    }

    parser_t parser(file, {source.string()});

    map.parse(parser);

    return map;
}

constexpr const char *usage = "\
usage: maputil [operations...]\
\
--script \"<path to Lua script file\"\
  execute the given Lua script.\
valid operations:\
--query \"<Lua expression>\"\
  perform a query on entities and print out matching results.\
  see docs for more details on globals.\
  note that query has the same access as script\
  but is more suitable for small read-only operations.\
--strip_extended_info\
  removes extended Quake II/III information on faces.\
--convert <quake | valve | etp | bp>\
  convert the current map to the given format.\
--save \"<output path>\"\
  save the current map to the given output path.\
--game <quake | quake2 | hexen2 | halflife>\
  set the current game; used for certain conversions\
  or operations.\
";

#ifdef USE_LUA
using array_iterate_callback = std::function<bool(lua_State *state, size_t index)>;

void lua_iterate_array(lua_State *state, array_iterate_callback cb)
{
    size_t n = 0;

    bool keep_iterating = false;

    do {
        lua_rawgeti(state, -1, n + 1);

        if (lua_type(state, -1) != LUA_TNIL) {
            keep_iterating = cb(state, n);
        } else {
            keep_iterating = false;
        }

        lua_pop(state, 1);

        n++;
    } while (keep_iterating);
}

size_t lua_count_array(lua_State *state)
{
    size_t num = 0;

    lua_iterate_array(state, [&num](auto state, auto index) {
        num++;
        return true;
    });

    return num;
}

// pushes value onto stack
static void json_to_lua(lua_State *state, const json &value)
{
    switch (value.type()) {
    case json::value_t::object: {
        lua_newtable(state);

        for (auto it = value.begin(); it != value.end(); ++it) {
            lua_pushstring(state, it.key().c_str());
            json_to_lua(state, it.value());
            lua_settable(state, -3);
        }
        return;
    }
    case json::value_t::array: {
        lua_newtable(state);

        size_t i = 1;

        for (auto &v : value) {
            json_to_lua(state, v);
            lua_rawseti(state, -2, i++);
        }
        return;
    }
    case json::value_t::string: {
        lua_pushstring(state, value.get<std::string>().c_str());
        return;
    }
    case json::value_t::number_unsigned: {
        lua_pushnumber(state, value.get<uint64_t>());
        return;
    }
    case json::value_t::number_integer: {
        lua_pushnumber(state, value.get<int64_t>());
        return;
    }
    case json::value_t::number_float: {
        lua_pushnumber(state, value.get<double>());
        return;
    }
    case json::value_t::boolean: {
        lua_pushboolean(state, value.get<bool>());
        return;
    }
    case json::value_t::null: {
        lua_pushnil(state);
        return;
    }
    default: {
        luaL_error(state, "invalid JSON object type\n");
        return;
    }
    }
}

static int l_load_json(lua_State *state)
{
    const char *path = lua_tostring(state, -1);
    lua_pop(state, 1);

    auto result = fs::load(path);

    if (!result) {
        return luaL_error(state, "can't load JSON file: %s\n", path);
    }

    try
    {
        auto json = json::parse(result->begin(), result->end());

        json_to_lua(state, json);
    }
    catch(std::exception &e)
    {
        return luaL_error(state, "JSON load exception for %s: %s\n", path, e.what());
    }

    return 1;
}

/*
* Lua layout:
* entities = table[]
*  [E].dict = array
*      [D] = [ key, value ]
*  [E].brushes = table[]
*   [S].texture = string
*   [S].plane_points = [ [ x, y, z ] [ x, y, z ] [ x, y, z ] ]
*   [S].raw = table (can only contain ONE member:)
*       .quaked = table
*        .shift = [ x, y ]
*        .rotate = number
*        .scale = [ x, y ]
*       .valve = table
*        .axis = [ [ x, y, z ] [ x, y, z ] ]
*        .shift = [ x, y ]
*        .rotate = number
*        .scale = [ x, y ]
*       .bp = table
*        .axis = [ [ x, y, z ] [ x, y, z ] ]
*       .etp = table
*        .shift = [ x, y ]
*        .rotate = number
*        .scale = [ x, y ]
*        .tx2 = boolean
*   [S].info = table or nil
*       .contents = number
*       .value = number
*       .flags = number
*   [S].plane = [ x, y, z, d ] (read-only)
*   [S].vecs = [ [ x, y, z, d ] [ x, y, z, d ] ] (read-only)
*/

static void maputil_make_brush_side(lua_State *state, const brush_side_t &side)
{
    // make side
    lua_createtable(state, 0, 4);
    
    // make vecs
    lua_createtable(state, 2, 0);

    for (size_t i = 0; i < 2; i++) {
        lua_createtable(state, 4, 0);

        for (size_t v = 0; v < 4; v++) {
            lua_pushnumber(state, side.vecs.at(i, v));
            lua_rawseti(state, -2, v + 1);
        }

        lua_rawseti(state, -2, i + 1);
    }

    lua_setfield(state, -2, "vecs");

    // set raw
    lua_createtable(state, 0, 1);

    lua_createtable(state, 0, 4);

    if (std::holds_alternative<texdef_quake_ed_t>(side.raw) ||
        std::holds_alternative<texdef_valve_t>(side.raw) ||
        std::holds_alternative<texdef_etp_t>(side.raw)) {
        const texdef_quake_ed_t &raw = std::holds_alternative<texdef_quake_ed_t>(side.raw) ? std::get<texdef_quake_ed_t>(side.raw) :
            std::holds_alternative<texdef_valve_t>(side.raw) ? reinterpret_cast<const texdef_quake_ed_t &>(std::get<texdef_valve_t>(side.raw)) :
            reinterpret_cast<const texdef_quake_ed_t &>(std::get<texdef_etp_t>(side.raw));

        lua_createtable(state, 2, 0);
        lua_pushnumber(state, raw.shift[0]);
        lua_rawseti(state, -2, 1);
        lua_pushnumber(state, raw.shift[1]);
        lua_rawseti(state, -2, 2);
        lua_setfield(state, -2, "shift");

        lua_pushnumber(state, raw.rotate);
        lua_setfield(state, -2, "rotate");

        lua_createtable(state, 2, 0);
        lua_pushnumber(state, raw.scale[0]);
        lua_rawseti(state, -2, 1);
        lua_pushnumber(state, raw.scale[1]);
        lua_rawseti(state, -2, 2);
        lua_setfield(state, -2, "scale");

        if (std::holds_alternative<texdef_etp_t>(side.raw)) {
            const auto &raw_etp = std::get<texdef_etp_t>(side.raw);

            lua_pushboolean(state, raw_etp.tx2);
            lua_setfield(state, -2, "tx2");
        }
    }

    if (std::holds_alternative<texdef_valve_t>(side.raw) ||
        std::holds_alternative<texdef_bp_t>(side.raw)) {
        const texdef_bp_t &raw_bp =
            std::holds_alternative<texdef_valve_t>(side.raw) ? std::get<texdef_valve_t>(side.raw) :
            std::get<texdef_bp_t>(side.raw);

        lua_createtable(state, 2, 0);

        for (size_t i = 0; i < 2; i++) {
            lua_createtable(state, 3, 0);
            for (size_t v = 0; v < 3; v++) {
                lua_pushnumber(state, raw_bp.axis.at(i, v));
                lua_rawseti(state, -2, v + 1);
            }
            lua_rawseti(state, -2, i + 1);
        }

        lua_setfield(state, -2, "axis");
    }

    if (std::holds_alternative<texdef_quake_ed_t>(side.raw)) {
        lua_setfield(state, -2, "quaked");
    } if (std::holds_alternative<texdef_etp_t>(side.raw)) {
        lua_setfield(state, -2, "etp");
    } if (std::holds_alternative<texdef_valve_t>(side.raw)) {
        lua_setfield(state, -2, "valve");
    } else {
        lua_setfield(state, -2, "bp");
    }

    lua_setfield(state, -2, "raw");

    // make plane
    lua_createtable(state, 4, 0);
    lua_pushnumber(state, side.plane.normal[0]);
    lua_rawseti(state, -2, 1);
    lua_pushnumber(state, side.plane.normal[1]);
    lua_rawseti(state, -2, 2);
    lua_pushnumber(state, side.plane.normal[2]);
    lua_rawseti(state, -2, 3);
    lua_pushnumber(state, side.plane.dist);
    lua_rawseti(state, -2, 4);
    lua_setfield(state, -2, "plane");

    // make plane points
    lua_createtable(state, 3, 0);

    for (size_t i = 0; i < 3; i++) {
        lua_createtable(state, 3, 0);

        for (size_t v = 0; v < 3; v++) {
            lua_pushnumber(state, side.planepts[i][v]);
            lua_rawseti(state, -2, v + 1);
        }

        lua_rawseti(state, -2, i + 1);
    }

    lua_setfield(state, -2, "plane_points");

    // set texture
    lua_pushstring(state, side.texture.c_str());
    lua_setfield(state, -2, "texture");

    if (side.extended_info) {
        // set info
        lua_createtable(state, 0, 3);
        lua_pushnumber(state, side.extended_info->contents.native);
        lua_setfield(state, -2, "contents");
        lua_pushnumber(state, side.extended_info->value);
        lua_setfield(state, -2, "value");
        lua_pushnumber(state, side.extended_info->flags.native);
        lua_setfield(state, -2, "flags");

        lua_setfield(state, -2, "info");
    }
}

static void maputil_make_brush(lua_State *state, const brush_t &brush)
{
    // make sides
    lua_createtable(state, brush.faces.size(), 0);

    for (size_t s = 0; s < brush.faces.size(); s++) {
        auto &side = brush.faces[s];

        maputil_make_brush_side(state, side);

        // put side into sides
        lua_rawseti(state, -2, s + 1);
    }
}

#define LUA_VERIFY_TOP_TYPE(type) \
    Q_assert(lua_type(state, -1) == type)

static void maputil_copy_dict(lua_State *state, map_entity_t &entity)
{
    LUA_VERIFY_TOP_TYPE(LUA_TTABLE);

    // check for dict
    if (lua_getfield(state, -1, "dict") == LUA_TTABLE) {

        // iterate kvps
        size_t n = 0;

        while (true) {
            lua_rawgeti(state, -1, n + 1);

            if (lua_type(state, -1) == LUA_TNIL) {
                lua_pop(state, 1);
                break;
            }

            LUA_VERIFY_TOP_TYPE(LUA_TTABLE);
            n++;

            lua_rawgeti(state, -1, 1);
            const char *key = lua_tostring(state, -1);
            lua_pop(state, 1);
            lua_rawgeti(state, -1, 2);
            const char *value = lua_tostring(state, -1);
            lua_pop(state, 1);
            entity.epairs.set(key, value);

            lua_pop(state, 1);
        }
    }

    lua_pop(state, 1);
}

static texdef_quake_ed_t maputil_load_quaked(lua_State *state)
{
    texdef_quake_ed_t quaked;

    lua_getfield(state, -1, "shift");
    for (size_t i = 0; i < 2; i++) {
        lua_rawgeti(state, -1, i + 1);
        quaked.shift[i] = lua_tonumber(state, -1);
        lua_pop(state, 1);
    }
    lua_pop(state, 1);

    lua_getfield(state, -1, "rotate");
    quaked.rotate = lua_tonumber(state, -1);
    lua_pop(state, 1);

    lua_getfield(state, -1, "scale");
    for (size_t i = 0; i < 2; i++) {
        lua_rawgeti(state, -1, i + 1);
        quaked.scale[i] = lua_tonumber(state, -1);
        lua_pop(state, 1);
    }
    lua_pop(state, 1);

    return quaked;
}

static texdef_bp_t maputil_load_bp(lua_State *state)
{
    texdef_bp_t bp;

    lua_getfield(state, -1, "axis");

    for (size_t i = 0; i < 2; i++) {
        lua_rawgeti(state, -1, i + 1);

        for (size_t v = 0; v < 3; v++) {
            lua_rawgeti(state, -1, v + 1);
            bp.axis.at(i, v) = lua_tonumber(state, -1);
            lua_pop(state, 1);
        }

        lua_pop(state, 1);
    }

    lua_pop(state, 1);

    return bp;
}

static void maputil_copy_side(lua_State *state, brush_side_t &side)
{
    // texture
    lua_getfield(state, -1, "texture");
    side.texture = lua_tostring(state, -1);
    lua_pop(state, 1);

    // plane points
    lua_getfield(state, -1, "plane_points");

    for (size_t i = 0; i < 3; i++) {
        lua_rawgeti(state, -1, i + 1);

        for (size_t z = 0; z < 3; z++) {
            lua_rawgeti(state, -1, z + 1);
            side.planepts[i][z] = lua_tonumber(state, -1);
            lua_pop(state, 1);
        }

        lua_pop(state, 1);
    }

    lua_pop(state, 1);

    // raw
    lua_getfield(state, -1, "raw");

    if (lua_getfield(state, -1, "quaked") != LUA_TNIL) {
        side.raw = maputil_load_quaked(state);
    }
    lua_pop(state, 1);

    if (lua_getfield(state, -1, "valve") != LUA_TNIL) {
        texdef_bp_t bp = maputil_load_bp(state);
        texdef_quake_ed_t qed = maputil_load_quaked(state);

        side.raw = texdef_valve_t { qed, bp };
    }
    lua_pop(state, 1);

    if (lua_getfield(state, -1, "bp") != LUA_TNIL) {
        side.raw = maputil_load_bp(state);
    }
    lua_pop(state, 1);

    if (lua_getfield(state, -1, "etp") != LUA_TNIL) {
        texdef_quake_ed_t qed = maputil_load_quaked(state);

        lua_getfield(state, -1, "tx2");
        bool b = !!lua_toboolean(state, -1);
        lua_pop(state, 1);

        side.raw = texdef_etp_t { qed, b };
    }
    lua_pop(state, 1);

    lua_pop(state, 1);

    // extra info
    lua_getfield(state, -1, "info");

    if (lua_type(state, -1) == LUA_TTABLE) {
        texinfo_quake2_t q2;

        lua_getfield(state, -1, "contents");
        q2.contents.native = lua_tonumber(state, -1);
        lua_pop(state, 1);

        lua_getfield(state, -1, "value");
        q2.value = lua_tonumber(state, -1);
        lua_pop(state, 1);

        lua_getfield(state, -1, "flags");
        q2.flags.native = lua_tonumber(state, -1);
        lua_pop(state, 1);

        side.extended_info = q2;
    }

    lua_pop(state, 1);
}

static void maputil_copy_brush(lua_State *state, brush_t &brush)
{
    // count sides
    size_t num_sides = lua_count_array(state);
    brush.faces.resize(num_sides);

    // iterate brushes
    lua_iterate_array(state, [&brush](auto state, auto index) {
        brush_side_t &side = brush.faces[index];
        maputil_copy_side(state, side);
        return true;
    });
}

static void maputil_copy_brushes(lua_State *state, map_entity_t &entity)
{
    LUA_VERIFY_TOP_TYPE(LUA_TTABLE);

    // check for dict
    if (lua_getfield(state, -1, "brushes") == LUA_TTABLE) {

        // count brushes
        size_t num_brushes = lua_count_array(state);
        entity.brushes.resize(num_brushes);

        // iterate brushes
        lua_iterate_array(state, [&entity](auto state, auto index) {
            brush_t &brush = entity.brushes[index];
            maputil_copy_brush(state, brush);
            return true;
        });
    }

    lua_pop(state, 1);
}

static int l_commit_map(lua_State *state)
{
    map_file.entities.clear();

    // verify entities global
    lua_getglobal(state, "entities");

    LUA_VERIFY_TOP_TYPE(LUA_TTABLE);

    // count entities
    size_t num_entities = lua_count_array(state);

    // create entities
    map_file.entities.resize(num_entities);

    for (size_t i = 0; i < num_entities; i++) {
        auto &entity = map_file.entities[i];

        lua_rawgeti(state, -1, i + 1);

        maputil_copy_dict(state, entity);

        maputil_copy_brushes(state, entity);

        lua_pop(state, 1);
    }

    lua_pop(state, 1);

    return 0;
}

static inline qplane3d pop_plane_from_side(lua_State *state)
{
    qplane3d plane;

    lua_getfield(state, -1, "plane");

    for (size_t i = 0; i < 3; i++) {
        lua_rawgeti(state, -1, i + 1);
        plane.normal[i] = lua_tonumber(state, -1);
        lua_pop(state, 1);
    }
    lua_rawgeti(state, -1, 4);
    plane.dist = lua_tonumber(state, -1);
    lua_pop(state, 1);

    lua_pop(state, 1);

    return plane;
}

static int l_create_winding(lua_State *state)
{
    // -3 = face
    // -2 = brush
    // -1 = extents
    bool found_face = false;

    vec_t extents = lua_tonumber(state, -1);
    lua_pop(state, 1);

    lua_pushvalue(state, -2);
    qplane3d side_plane = pop_plane_from_side(state);
    lua_pop(state, 1);

    using winding_t = polylib::winding_base_t<polylib::winding_storage_hybrid_t<16>>;
    std::optional<winding_t> winding = winding_t::from_plane(side_plane, extents);

    // loop through sides on brush
    {
        if (lua_type(state, -1) == LUA_TTABLE) {
            lua_iterate_array(state, [&found_face, &winding](auto state, auto index) {

                LUA_VERIFY_TOP_TYPE(LUA_TTABLE);

                // check that the face is part of the brush
                if (lua_rawequal(state, -1, -3)) {
                    found_face = true;
                } else if (winding) {
                    qplane3d plane = pop_plane_from_side(state);
                    winding = winding->clip_front(-plane, 0.0f);
                }

                return true;
            });
        }

        lua_pop(state, 1);
    }

    if (!winding) {
        lua_pushnil(state);
    } else {
        lua_createtable(state, winding->size(), 0);

        for (size_t i = 0; i < winding->size(); i++) {
            lua_createtable(state, 3, 0);

            auto &p = winding->at(i);
            for (size_t v = 0; v < 3; v++) {
                lua_pushnumber(state, p[v]);
                lua_rawseti(state, -2, v + 1);
            }

            lua_rawseti(state, -2, i + 1);
        }
    }

    return 1;
}

static int l_load_texture_meta(lua_State *state)
{
    const char *path = lua_tostring(state, 1);  /* get argument */
    lua_pop(state, 1);

    if (!current_game) {
        luaL_error(state, "need a game loaded with -game for this function");
    }

    auto result = std::get<0>(img::load_texture_meta(path, current_game, common_options)).value_or(img::texture_meta {});

    lua_createtable(state, 0, 5);

    lua_pushnumber(state, result.contents.native);
    lua_setfield(state, -2, "contents");
    lua_pushnumber(state, result.flags.native);
    lua_setfield(state, -2, "flags");
    lua_pushnumber(state, result.value);
    lua_setfield(state, -2, "value");
    lua_pushnumber(state, result.width);
    lua_setfield(state, -2, "width");
    lua_pushnumber(state, result.height);
    lua_setfield(state, -2, "height");

    return 1;
}

static void maputil_setup_globals(lua_State *state)
{
    lua_pushcfunction(state, l_load_json);
    lua_setglobal(state, "load_json");

    lua_pushcfunction(state, l_commit_map);
    lua_setglobal(state, "commit_map");

    lua_pushcfunction(state, l_create_winding);
    lua_setglobal(state, "create_winding");

    lua_pushcfunction(state, l_load_texture_meta);
    lua_setglobal(state, "load_texture_meta");

    // constants
    lua_pushnumber(state, (int32_t) texcoord_style_t::quaked);
    lua_setglobal(state, "TEXCOORD_QUAKED");
    lua_pushnumber(state, (int32_t) texcoord_style_t::etp);
    lua_setglobal(state, "TEXCOORD_ETP");
    lua_pushnumber(state, (int32_t) texcoord_style_t::valve_220);
    lua_setglobal(state, "TEXCOORD_VALVE");
    lua_pushnumber(state, (int32_t) texcoord_style_t::brush_primitives);
    lua_setglobal(state, "TEXCOORD_BP");

    // convert map to a Lua representation.
    lua_createtable(state, map_file.entities.size(), 0);
    
    for (size_t i = 0; i < map_file.entities.size(); i++) {
        auto &entity = map_file.entities[i];

        // make entity table
        lua_createtable(state, 0, 2);

        // make dict
        if (entity.epairs.size()) {
            lua_createtable(state, entity.epairs.size(), 0);

            size_t ent = 0;

            for (auto &pair : entity.epairs) {
                lua_createtable(state, 2, 0);
                lua_pushstring(state, pair.first.c_str());
                lua_rawseti(state, -2, 1);
                lua_pushstring(state, pair.second.c_str());
                lua_rawseti(state, -2, 2);

                lua_rawseti(state, -2, ent + 1);
                ent++;
            }

            // push dict to entity
            lua_setfield(state, -2, "dict");
        }

        // make brushes
        if (!entity.brushes.empty()) {
            lua_createtable(state, entity.brushes.size(), 0);

            for (size_t b = 0; b < entity.brushes.size(); b++) {
                auto &brush = entity.brushes[b];

                maputil_make_brush(state, brush);

                // put brush into brushes
                lua_rawseti(state, -2, b + 1);
            }

            // push dict to entity
            lua_setfield(state, -2, "brushes");
        }

        // put entity into entities
        lua_rawseti(state, -2, i + 1);
    }

    lua_setglobal(state, "entities");
}

static lua_State *maputil_setup_lua()
{
    lua_State *state = luaL_newstate();

    luaL_openlibs(state);

    return state;
}

static void maputil_free_lua(lua_State *state)
{
    lua_close(state);
}

static int maputil_lua_error(lua_State *state)
{
    luaL_traceback(state, state, NULL, 1);

    logging::print("can't execute script: {}\n{}\n", lua_tostring(state, -1), lua_tostring(state, -2));

    return 0;
}
#endif

static void maputil_exec_script(const fs::path &file)
{
#ifdef USE_LUA
    lua_State *state = maputil_setup_lua();

    lua_pushcfunction(state, maputil_lua_error);

    int err = luaL_loadfile(state, file.string().c_str());

    if (err != LUA_OK) {
        logging::print("can't load script: {}\n", lua_tostring(state, -1));
    } else {
        maputil_setup_globals(state);

        err = lua_pcall(state, 0, 0, -2);

        if (err != LUA_OK) {

            lua_pop(state, 1);
        }
    }

    maputil_free_lua(state);
#else
    logging::print("maputil not compiled with Lua support\n");
#endif
}

static void maputil_exec_query(const char *query)
{
#ifdef USE_LUA
    logging::print("query: {}\n", query);

    lua_State *state = maputil_setup_lua();

    int err = luaL_loadstring(state, query);

    if (err != LUA_OK) {
        logging::print("can't load query: {}\n", lua_tostring(state, -1));
        lua_pop(state, 1);
    } else {
        maputil_setup_globals(state);

        lua_pushvalue(state, 1);

        int ref = luaL_ref(state, LUA_REGISTRYINDEX);

        lua_pop(state, 1);

        for (auto &entity : map_file.entities) {
            lua_createtable(state, 0, entity.epairs.size());

            for (auto &kvp : entity.epairs) {

                lua_pushstring(state, kvp.second.c_str());
                lua_setfield(state, -2, kvp.first.c_str());
            }

            lua_setglobal(state, "entity");

            lua_rawgeti(state, LUA_REGISTRYINDEX, ref);
            err = lua_pcall(state, 0, 1, 0);

            if (err != LUA_OK) {
                logging::print("can't execute query: {}\n", lua_tostring(state, -1));
                lua_pop(state, 1);
            } else {
                int b = lua_toboolean(state, -1);
                lua_pop(state, 1);

                if (b) {
                    logging::print("MATCHED: {} @ {}\n", entity.epairs.get("classname"), entity.location);
                }
            }

            lua_gc(state, LUA_GCCOLLECT);
        }

        luaL_unref(state, LUA_REGISTRYINDEX, ref);
    }

    maputil_free_lua(state);
#else
    logging::print("maputil not compiled with Lua support\n");
#endif
}

int maputil_main(int argc, char **argv)
{
    logging::preinitialize();

    fmt::print("---- maputil / ericw-tools {} ----\n", ERICWTOOLS_VERSION);
    if (argc == 1) {
        fmt::print("{}", usage);
        exit(1);
    }

    fs::path source = argv[1];

    if (!fs::exists(source)) {
        source = DefaultExtension(argv[1], "map");
    }

    printf("---------------------\n");
    fmt::print("{}\n", source);

    map_file = LoadMapOrEntFile(source);

    for (int32_t i = 2; i < argc - 1; i++) {

        const char *cmd = argv[i];

        if (!strcmp(cmd, "--query")) {
            i++;

            const char *query = argv[i];

            maputil_exec_query(query);
        } else if (!strcmp(cmd, "--script")) {
            i++;

            const char *file = argv[i];

            maputil_exec_script(file);
        } else if (!strcmp(cmd, "--game")) {
            i++;

            const char *gamename = argv[i];

            current_game = nullptr;

            for (auto &game : gamedef_list()) {
                if (!Q_strcasecmp(game->friendly_name, gamename)) {
                    current_game = game;
                    break;
                }
            }

            if (!current_game) {
                FError("not sure what game {} is\n", gamename);
            }

            current_game->init_filesystem(source, common_options);

        } else if (!strcmp(cmd, "--save")) {
            i++;

            const char *output = argv[i];

            fs::path dest = DefaultExtension(output, "map");
            fmt::print("saving to {}...\n", dest);

            std::ofstream stream(dest);
            map_file.write(stream);
        } else if (!strcmp(cmd, "--strip_extended_info")) {

            for (auto &entity : map_file.entities) {
                for (auto &brush : entity.brushes) {
                    for (auto &face : brush.faces) {
                        face.extended_info = std::nullopt;
                    }
                }
            }
        } else if (!strcmp(cmd, "--convert")) {

            i++;

            const char *type = argv[i];
            texcoord_style_t dest_style;

            if (!strcmp(type, "quake")) {
                dest_style = texcoord_style_t::quaked;
            } else if (!strcmp(type, "valve")) {
                dest_style = texcoord_style_t::valve_220;
            } else if (!strcmp(type, "etp")) {
                dest_style = texcoord_style_t::etp;
            } else if (!strcmp(type, "bp")) {
                dest_style = texcoord_style_t::brush_primitives;
            } else {
                FError("unknown map style {}", type);
            }

            map_file.convert_to(dest_style, current_game, common_options);
        }
    }

    printf("---------------------\n");

    return 0;
}
