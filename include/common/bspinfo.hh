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

#include "common/fs.hh"
#include "common/bsputils.hh"
#include "common/imglib.hh"
#include "common/qvec.hh"

#include <map>
#include <vector>

struct bspdata_t;
struct mbsp_t;
struct mface_t;

struct full_atlas_t
{
    /**
     * these are normalized to 0..1
     */
    std::map<int, std::vector<qvec2f>> facenum_to_lightmap_uvs;

    std::map<int, img::texture> style_to_lightmap_atlas;
};

full_atlas_t build_lightmap_atlas(const mbsp_t &bsp, const bspxentries_t &bspx, const std::vector<uint8_t> &litdata, bool use_bspx, bool use_decoupled);

void serialize_bsp(const bspdata_t &bspdata, const mbsp_t &bsp, const fs::path &name);
