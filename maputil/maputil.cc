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

#ifdef USE_LUA
extern "C"
{
    #include <lua.h>
    #include <lualib.h>
    #include <lauxlib.h>
}
#endif

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
valid operations:\
--query \"<Lua expression>\"\
  perform a query on entities and print out matching results.\
  see docs for more details on globals.\
--script \"<path to Lua script file\"\
  execute the given Lua script.\
--strip_extended_info\
  removes extended Quake II/III information on faces.\
--convert <quake | valve | etp | bp>\
  convert the current map to the given format.\
--save \"<output path>\"\
  save the current map to the given output path.\
";

static void maputil_query(map_file_t &map_file, const char *query)
{
#ifdef USE_LUA
    logging::print("query: {}\n", query);

    lua_State *state = luaL_newstate();

    luaL_openlibs(state);

    int err = luaL_loadstring(state, query);

    if (err != LUA_OK) {
        logging::print("can't load query: {}\n", lua_tostring(state, -1));
        lua_pop(state, 1);
    } else {
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

        lua_close(state);
    }
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

    map_file_t map_file;

    map_file = LoadMapOrEntFile(source);

    for (int32_t i = 2; i < argc - 1; i++) {

        const char *cmd = argv[i];

        if (!strcmp(cmd, "--query")) {
            i++;

            const char *query = argv[i];

            maputil_query(map_file, query);
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

            map_file.convert_to(dest_style);
        }
    }

    printf("---------------------\n");

    return 0;
}
