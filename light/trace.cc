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
#include <light/trace.hh>
#include <light/ltface.hh>
#include <common/bsputils.hh>
#ifdef HAVE_EMBREE
#include <light/trace_embree.hh>
#endif
#include <cassert>

/*
 * ============================================================================
 * FENCE TEXTURE TESTING
 * ============================================================================
 */

/**
 * Given a float texture coordinate, returns a pixel index to sample in [0, width-1].
 * This assumes the texture repeats and nearest filtering
 */
uint32_t clamp_texcoord(vec_t in, uint32_t width)
{
    if (in >= 0.0f)
    {
        return (uint32_t)in % width;
    }
    else
    {
        vec_t in_abs = ceil(fabs(in));
        uint32_t in_abs_mod = (uint32_t)in_abs % width;
        return (width - in_abs_mod) % width;
    }
}

color_rgba //mxd. int -> color_rgba
SampleTexture(const bsp2_dface_t *face, const mbsp_t *bsp, const vec3_t point)
{
    color_rgba sample{};
    if (!bsp->rgbatexdatasize)
        return sample;

    // FIXME: re-enable the following code
    return sample;
#if 0
    const auto *miptex = Face_Miptex(bsp, face);
    
    if (miptex == nullptr)
        return sample;
    
    const gtexinfo_t *tex = &bsp->texinfo[face->texinfo];

    vec_t texcoord[2];
    WorldToTexCoord(point, tex, texcoord);

    const int x = clamp_texcoord(texcoord[0], miptex->width);
    const int y = clamp_texcoord(texcoord[1], miptex->height);
    assert (x >= 0);
    assert (y >= 0);
    
    // FIXME: this is broken - palette index? color?
    // see: https://github.com/ericwa/ericw-tools/commit/0661098bc57d09b9961aa8314c52545a8f89a1e1#diff-dff5fe3d0288e49cabf1e7bc8fb28819c513be54cb7bbcdbea8b52ee0efd6bf5
    color_rgba *data = (color_rgba*)((uint8_t*)miptex + miptex->offset);
    sample = data[(miptex->width * y) + x];

    return sample;
#endif
}

hitresult_t TestSky(const vec3_t start, const vec3_t dirn, const modelinfo_t *self, const bsp2_dface_t **face_out)
{
    return Embree_TestSky(start, dirn, self, face_out);
}

hitresult_t TestLight(const vec3_t start, const vec3_t stop, const modelinfo_t *self)
{
    return Embree_TestLight(start, stop, self);
}

raystream_intersection_t *MakeIntersectionRayStream(int maxrays) {
    return Embree_MakeIntersectionRayStream(maxrays);
}
raystream_occlusion_t* MakeOcclusionRayStream(int maxrays) {
    return Embree_MakeOcclusionRayStream(maxrays);
}

void MakeTnodes(const mbsp_t *bsp)
{
    Embree_TraceInit(bsp);
}
