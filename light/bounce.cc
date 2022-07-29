/*  Copyright (C) 1996-1997  Id Software, Inc.
    Copyright (C) 2017 Eric Wasylishen

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
#include <cassert>
//#include <cstdio>
#include <iostream>

#include <light/light.hh>
#include <light/bounce.hh>
#include <light/ltface.hh>
#include <light/surflight.hh>

#include <common/polylib.hh>
#include <common/bsputils.hh>

#include <memory>
#include <vector>
#include <map>
#include <unordered_map>
#include <set>
#include <algorithm>
#include <mutex>
#include <string>

#include <common/qvec.hh>
#include <common/parallel.hh>

using namespace std;
using namespace polylib;

mutex bouncelights_lock;
static std::forward_list<bouncelight_t> bouncelights;
static size_t lastBounceLightIndex;
static std::map<size_t, std::vector<std::reference_wrapper<bouncelight_t>>> bouncelightsByFacenum;

static bool Face_ShouldBounce(const mbsp_t *bsp, const mface_t *face)
{
    // make bounce light, only if this face is shadow casting
    const modelinfo_t *mi = ModelInfoForFace(bsp, Face_GetNum(bsp, face));
    if (!mi || !mi->shadow.boolValue()) {
        return false;
    }

    if (!Face_IsLightmapped(bsp, face)) {
        return false;
    }

    const char *texname = Face_TextureName(bsp, face);
    if (!Q_strcasecmp("skip", texname)) {
        return false;
    }

    // check for "_bounce" "-1"
    if (extended_texinfo_flags[face->texinfo].no_bounce) {
        return false;
    }

    // don't bounce *from* emission surfaces
    if (IsSurfaceLitFace(bsp, face)) {
        return false;
    }

    return true;
}

qvec3b Face_LookupTextureColor(const mbsp_t *bsp, const mface_t *face)
{
    auto it = img::find(Face_TextureName(bsp, face));

    if (it) {
        return it->averageColor;
    }

    return {127};
}

inline bouncelight_t &CreateBounceLight(const mface_t *face, const mbsp_t *bsp)
{
    unique_lock<mutex> lck{bouncelights_lock};
    bouncelight_t &l = bouncelights.emplace_front();

    bouncelightsByFacenum[Face_GetNum(bsp, face)].push_back(l);

    return l;
}

static void AddBounceLight(const qvec3d &pos, std::map<int, qvec3d> &&colorByStyle,
    const qvec3d &surfnormal, vec_t area, const mface_t *face, const mbsp_t *bsp)
{
    for (const auto &styleColor : colorByStyle) {
        Q_assert(styleColor.second[0] >= 0);
        Q_assert(styleColor.second[1] >= 0);
        Q_assert(styleColor.second[2] >= 0);
    }
    Q_assert(area > 0);

    bouncelight_t &l = CreateBounceLight(face, bsp);
    l.poly = GLM_FacePoints(bsp, face);
    l.poly_edgeplanes = GLM_MakeInwardFacingEdgePlanes(l.poly);
    l.pos = pos;
    l.colorByStyle = colorByStyle;

    for (const auto &styleColor : l.colorByStyle) {
        for (int i = 0; i < 3; i++) {
            l.componentwiseMaxColor[i] = max(l.componentwiseMaxColor[i], styleColor.second[i]);
            }
        }
    l.surfnormal = surfnormal;
    l.area = area;

    if (options.visapprox.value() == visapprox_t::VIS) {
        l.leaf = Light_PointInLeaf(bsp, pos);
    } else if (options.visapprox.value() == visapprox_t::RAYS) {
        l.bounds = EstimateVisibleBoundsAtPoint(pos);
    }
}

const std::forward_list<bouncelight_t> &BounceLights()
{
    return bouncelights;
}

const std::vector<std::reference_wrapper<bouncelight_t>> &BounceLightsForFaceNum(int facenum)
{
    const auto &vec = bouncelightsByFacenum.find(facenum);
    if (vec != bouncelightsByFacenum.end()) {
        return vec->second;
    }

    static std::vector<std::reference_wrapper<bouncelight_t>> empty;
    return empty;
}

static void MakeBounceLightsThread(const settings::worldspawn_keys &cfg, const mbsp_t *bsp, const mface_t &face)
{
    if (!Face_ShouldBounce(bsp, &face)) {
        return;
    }

    auto &surf_ptr = LightSurfaces()[&face - bsp->dfaces.data()];

    if (!surf_ptr) {
        return;
    }

    auto &surf = *surf_ptr.get();

    winding_t winding = winding_t::from_face(bsp, &face);
    const vec_t area = winding.area();

    if (!area) {
        return;
    }

    const vec_t area_divisor = sqrt(area);
    const vec_t sample_divisor = surf.points.size() / (surf.vanilla_extents.width() * surf.vanilla_extents.height());

    qplane3d faceplane = winding.plane();

    qvec3d facemidpoint = winding.center();
    facemidpoint += faceplane.normal; // lift 1 unit

    // average them, area weighted
    map<int, qvec3d> sum;

    for (const auto &lightmap : surf.lightmapsByStyle) {
        for (const auto &sample : lightmap.samples) {
            sum[lightmap.style] += sample.color / sample_divisor;
        }
    }

    qvec3d total = {};

    for (auto &styleColor : sum) {
        styleColor.second /= area_divisor;
        styleColor.second *= cfg.bouncescale.value();
        total += styleColor.second;
    }
    
    // no bounced color, we can leave early
    if (qv::emptyExact(total)) {
        return;
    }

    // lerp between gray and the texture color according to `bouncecolorscale` (0 = use gray, 1 = use texture color)
    qvec3d texturecolor = qvec3d(Face_LookupTextureColor(bsp, &face)) / 255.0f;
    qvec3d blendedcolor = mix(qvec3d{127. / 255.}, texturecolor, cfg.bouncecolorscale.value());

    // final colors to emit
    map<int, qvec3d> emitcolors;

    for (const auto &styleColor : sum) {
        emitcolors[styleColor.first] = styleColor.second * blendedcolor;
    }

    AddBounceLight(facemidpoint, std::move(emitcolors), faceplane.normal, area, &face, bsp);
}

void MakeBounceLights(const settings::worldspawn_keys &cfg, const mbsp_t *bsp)
{
    logging::print("--- MakeBounceLights ---\n");

    logging::parallel_for_each(bsp->dfaces, [&](const mface_t &face) {
        MakeBounceLightsThread(cfg, bsp, face);
    });

    logging::print("{} bounce lights created\n", lastBounceLightIndex);
}
