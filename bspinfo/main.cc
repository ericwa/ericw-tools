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

#include <common/bspinfo.hh>
#include <common/cmdlib.hh>
#include <common/bspfile.hh>
#include <common/log.hh>
#include <common/settings.hh>

#include <fstream>
#include <fmt/ostream.h>
#include <common/json.hh>
#include "common/fs.hh"

#include "common/polylib.hh"
#include "common/bsputils.hh"

static void PrintBSPTextureUsage(const mbsp_t &bsp)
{
    std::unordered_map<std::string, vec_t> areas;

    for (auto &face : bsp.dfaces) {
        const char *name = Face_TextureName(&bsp, &face);

        if (!name || !*name) {
            continue;
        }

        auto points = Face_Points(&bsp, &face);
        polylib::winding_t w(points.begin(), points.end());
        vec_t area = w.area();

        areas[name] += area;
    }

    std::vector<std::tuple<std::string, vec_t>> areasVec;

    for (auto &area : areas) {
        areasVec.push_back(std::make_tuple(area.first, area.second));
    }

    std::sort(areasVec.begin(), areasVec.end(), [](auto &l, auto &r) { return std::get<1>(r) < std::get<1>(l); });

    printf("\n");

    for (auto &area : areasVec) {
        fmt::print("{},{:.0f}\n", std::get<0>(area), std::get<1>(area));
    }
}

static void FindInfiniteChains(const mbsp_t &bsp)
{
    for (auto &ti : bsp.texinfo) {
        if (ti.nexttexinfo == -1)
            continue;

        int loop = 0;

        for (int i = ti.nexttexinfo; i != -1; i = bsp.texinfo[i].nexttexinfo, loop++) {
            if (loop > bsp.texinfo.size()) {
                printf("INFINITE LOOP!");
                exit(1);
            }
        }
    }
}

// TODO
settings::common_settings bspinfo_options;

int main(int argc, char **argv)
{
    try {
        logging::preinitialize();

        fmt::print("---- bspinfo / ericw-tools {} ----\n", ERICWTOOLS_VERSION);
        if (argc == 1) {
            printf("usage: bspinfo bspfile [bspfiles]\n");
            exit(1);
        }

        for (int32_t i = 1; i < argc; i++) {
            printf("---------------------\n");
            fs::path source = DefaultExtension(argv[i], ".bsp");
            fmt::print("{}\n", source);

            bspdata_t bsp;
            LoadBSPFile(source, &bsp);

            bsp.version->game->init_filesystem(source, bspinfo_options);

            PrintBSPFileSizes(&bsp);

            // WriteBSPFile(fs::path(source).replace_extension("bsp.rewrite"), &bsp);

            ConvertBSPFormat(&bsp, &bspver_generic);

            serialize_bsp(bsp, std::get<mbsp_t>(bsp.bsp), fs::path(source).replace_extension("bsp.json"));

            PrintBSPTextureUsage(std::get<mbsp_t>(bsp.bsp));

            FindInfiniteChains(std::get<mbsp_t>(bsp.bsp));

            printf("---------------------\n");

            fs::clear();
        }

        return 0;
    } catch (const std::exception &e) {
        exit_on_exception(e);
    }
}
