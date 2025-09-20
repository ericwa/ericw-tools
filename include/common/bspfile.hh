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

#include <common/bspfile_generic.hh>
#include <common/bspfile_q1.hh>
#include <common/bspfile_q2.hh>
#include <common/bspxfile.hh>

using bspxentries_t = std::unordered_map<std::string, std::vector<uint8_t>>;

struct bspdata_t
{
    const bspversion_t *version, *loadversion;

    // the file path that this BSP was loaded from
    fs::path file;

    // Stay in monostate until a BSP type is requested.
    std::variant<std::monostate, mbsp_t, bsp29_t, bsp2rmq_t, bsp2_t, q2bsp_t, q2bsp_qbism_t> bsp;

    // This can be used with any BSP format.
    struct bspxentries
    {
        bspxentries_t entries;

        // transfer ownership of the vector into a BSPX lump
        void transfer(const char *xname, std::vector<uint8_t> &xdata);

        // transfer ownership of the vector into a BSPX lump
        void transfer(const char *xname, std::vector<uint8_t> &&xdata);
    };

    bspxentries bspx;
};

/* table of supported versions */
constexpr const bspversion_t *const bspversions[] = {&bspver_generic, &bspver_q1, &bspver_h2, &bspver_h2bsp2,
    &bspver_h2bsp2rmq, &bspver_bsp2, &bspver_bsp2rmq, &bspver_hl, &bspver_q2, &bspver_qbism};

void LoadBSPFile(fs::path &filename, bspdata_t *bspdata); // returns the filename as contained inside a bsp
void WriteBSPFile(const fs::path &filename, bspdata_t *bspdata);
void PrintBSPFileSizes(const bspdata_t *bspdata);
/**
 * Returns false if the conversion failed.
 */
bool ConvertBSPFormat(bspdata_t *bspdata, const bspversion_t *to_version);

std::string get_contents_display(contents_t bits);
Json::Value get_contents_json(contents_t bits);
contents_int_t set_contents_json(const Json::Value &json);
