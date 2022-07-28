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

#include <common/cmdlib.hh>
#include <common/mathlib.hh>
#include <common/bspfile.hh>
#include <common/log.hh>
#include <common/threads.hh>
#include <common/polylib.hh>
#include <common/imglib.hh>

#include <vector>
#include <map>
#include <string>
#include <cassert>
#include <limits>
#include <sstream>
#include <utility> // for std::pair

enum class hittype_t : uint8_t
{
    NONE = 0,
    SOLID = 1,
    SKY = 2
};

uint32_t clamp_texcoord(vec_t in, uint32_t width);
qvec4b SampleTexture(const mface_t *face, const mtexinfo_t *tex, const img::texture *texture, const mbsp_t *bsp,
    const qvec3d &point); // mxd. Palette index -> RGBA

class modelinfo_t;

using style_t = int;

struct hitresult_t
{
    bool blocked;

    /**
     * non-zero means light passed through a shadow-casting bmodel with the given style.
     * only valid if blocked == false.
     */
    style_t passedSwitchableShadowStyle;
};

/**
 * Convenience functions TestLight and TestSky will test against all shadow
 * casting bmodels and self-shadow the model 'self' if self != NULL.
 */
hitresult_t TestSky(const qvec3d &start, const qvec3d &dirn, const modelinfo_t *self, const mface_t **face_out);
hitresult_t TestLight(const qvec3d &start, const qvec3d &stop, const modelinfo_t *self);

const mleaf_t *Light_PointInLeaf(const mbsp_t *bsp, const qvec3d &point);
int Light_PointContents(const mbsp_t *bsp, const qvec3d &point);

#include "trace_embree.hh"