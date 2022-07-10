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
static std::vector<bouncelight_t> bouncelights;
std::map<int, std::vector<int>> bouncelightsByFacenum;

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

static void AddBounceLight(const qvec3d &pos, const std::map<int, qvec3d> &colorByStyle, const qvec3d &surfnormal,
    vec_t area, const mface_t *face, const mbsp_t *bsp)
{
    for (const auto &styleColor : colorByStyle) {
        Q_assert(styleColor.second[0] >= 0);
        Q_assert(styleColor.second[1] >= 0);
        Q_assert(styleColor.second[2] >= 0);
    }
    Q_assert(area > 0);

    bouncelight_t l;
    l.poly = GLM_FacePoints(bsp, face);
    l.poly_edgeplanes = GLM_MakeInwardFacingEdgePlanes(l.poly);
    l.pos = pos;
    l.colorByStyle = colorByStyle;

    qvec3f componentwiseMaxColor{};
    for (const auto &styleColor : colorByStyle) {
        for (int i = 0; i < 3; i++) {
            if (styleColor.second[i] > componentwiseMaxColor[i]) {
                componentwiseMaxColor[i] = styleColor.second[i];
            }
        }
    }
    l.componentwiseMaxColor = componentwiseMaxColor;
    l.surfnormal = surfnormal;
    l.area = area;

    if (options.visapprox.value() == visapprox_t::VIS) {
        l.leaf = Light_PointInLeaf(bsp, pos);
    } else if (options.visapprox.value() == visapprox_t::RAYS) {
        l.bounds = EstimateVisibleBoundsAtPoint(pos);
    }

    unique_lock<mutex> lck{bouncelights_lock};
    bouncelights.push_back(l);

    const int lastBounceLightIndex = static_cast<int>(bouncelights.size()) - 1;
    bouncelightsByFacenum[Face_GetNum(bsp, face)].push_back(lastBounceLightIndex);
}

const std::vector<bouncelight_t> &BounceLights()
{
    return bouncelights;
}

const std::vector<int> &BounceLightsForFaceNum(int facenum)
{
    const auto &vec = bouncelightsByFacenum.find(facenum);
    if (vec != bouncelightsByFacenum.end()) {
        return vec->second;
    }

    static std::vector<int> empty;
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
            sum[lightmap.style] += sample.color;
        }
    }

    qvec3d total = {};

    for (auto &styleColor : sum) {
        styleColor.second /= area_divisor;
        styleColor.second /= sample_divisor;
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

    AddBounceLight(facemidpoint, emitcolors, faceplane.normal, area, &face, bsp);
}

void MakeBounceLights(const settings::worldspawn_keys &cfg, const mbsp_t *bsp)
{
    logging::print("--- MakeBounceLights ---\n");

    logging::parallel_for_each(bsp->dfaces, [&](const mface_t &face) {
        MakeBounceLightsThread(cfg, bsp, face);
    });

    logging::print("{} bounce lights created\n", bouncelights.size());
}
