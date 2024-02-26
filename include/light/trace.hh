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

#include <common/qvec.hh>

namespace img
{
struct texture;
}
struct mtexinfo_t;
struct mface_t;
struct mbsp_t;

uint32_t clamp_texcoord(float in, uint32_t width);
qvec4b SampleTexture(const mface_t *face, const mtexinfo_t *tex, const img::texture *texture, const mbsp_t *bsp,
    const qvec3f &point); // mxd. Palette index -> RGBA

class modelinfo_t;
struct mleaf_t;
const mleaf_t *Light_PointInLeaf(const mbsp_t *bsp, const qvec3f &point);
