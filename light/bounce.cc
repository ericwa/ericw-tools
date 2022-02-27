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

using namespace std;
using namespace polylib;

mutex radlights_lock;
static std::vector<bouncelight_t> radlights;
std::map<int, std::vector<int>> radlightsByFacenum;

class patch_t
{
public:
    winding_t w;
    qvec3d center;
    qvec3d samplepoint; // 1 unit above center
    qplane3d plane;
    std::map<int, qvec3f> lightByStyle;
};

static unique_ptr<patch_t> MakePatch(const mbsp_t *bsp, const settings::worldspawn_keys &cfg, winding_t &w)
{
    unique_ptr<patch_t> p{new patch_t};
    p->w = std::move(w);

    // cache some stuff
    p->center = p->w.center();
    p->plane = p->w.plane();

    // nudge the cernter point 1 unit off
    p->samplepoint = p->center + p->plane.normal;

    // calculate direct light

    p->lightByStyle = GetDirectLighting(bsp, cfg, p->samplepoint, p->plane.normal);

    return p;
}

struct make_bounce_lights_args_t
{
    const mbsp_t *bsp;
    const settings::worldspawn_keys *cfg;
};

struct save_winding_args_t
{
    vector<unique_ptr<patch_t>> *patches;
    const mbsp_t *bsp;
    const settings::worldspawn_keys *cfg;
};

static void SaveWindingFn(winding_t &w, void *userinfo)
{
    save_winding_args_t *args = static_cast<save_winding_args_t *>(userinfo);
    args->patches->push_back(MakePatch(args->bsp, *args->cfg, w));
}

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

    return true;
}

qvec3b Face_LookupTextureColor(const mbsp_t *bsp, const mface_t *face)
{
    auto it = img::find(Face_TextureName(bsp, face));

    if (it) {
        return it->meta.averageColor;
    }

    return {127};
}

template<typename T>
static void AddBounceLight(const T &pos, const std::map<int, qvec3f> &colorByStyle, const qvec3d &surfnormal,
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
    l.bounds = qvec3d(0);

    if (!options.novisapprox.value()) {
        l.bounds = EstimateVisibleBoundsAtPoint(pos);
    }

    unique_lock<mutex> lck{radlights_lock};
    radlights.push_back(l);

    const int lastBounceLightIndex = static_cast<int>(radlights.size()) - 1;
    radlightsByFacenum[Face_GetNum(bsp, face)].push_back(lastBounceLightIndex);
}

static void *MakeBounceLightsThread(void *arg)
{
    const mbsp_t *bsp = static_cast<make_bounce_lights_args_t *>(arg)->bsp;
    const settings::worldspawn_keys &cfg = *static_cast<make_bounce_lights_args_t *>(arg)->cfg;

    while (1) {
        int i = GetThreadWork();
        if (i == -1)
            break;

        const mface_t *face = BSP_GetFace(bsp, i);

        if (!Face_ShouldBounce(bsp, face)) {
            continue;
        }

        vector<unique_ptr<patch_t>> patches;

        winding_t winding = winding_t::from_face(bsp, face);

        // grab some info about the face winding
        const vec_t facearea = winding.area();

        // degenerate face
        if (!facearea) {
            continue;
        }

        qplane3d faceplane = winding.plane();

        qvec3d facemidpoint = winding.center();
        facemidpoint += faceplane.normal; // lift 1 unit

        save_winding_args_t args;
        args.patches = &patches;
        args.bsp = bsp;
        args.cfg = &cfg;

        winding.dice(64.0f, SaveWindingFn, &args);

        // average them, area weighted
        map<int, qvec3f> sum;
        float totalarea = 0;

        for (const auto &patch : patches) {
            const float patcharea = patch->w.area();
            totalarea += patcharea;

            for (const auto &styleColor : patch->lightByStyle) {
                sum[styleColor.first] = sum[styleColor.first] + (styleColor.second * patcharea);
            }
            // fmt::print("  {} {} {}\n", patch->directlight[0], patch->directlight[1], patch->directlight[2]);
        }

        // avoid small, or zero-area patches ("sum" would be nan)
        if (totalarea < 1) {
            continue;
        }

        for (auto &styleColor : sum) {
            styleColor.second *= (1.0f / totalarea);
            styleColor.second /= 255.0f;
        }

        // lerp between gray and the texture color according to `bouncecolorscale` (0 = use gray, 1 = use texture color)
        qvec3f texturecolor = qvec3f(Face_LookupTextureColor(bsp, face)) / 255.0f;
        qvec3f blendedcolor = mix(qvec3f{127.f / 255.f}, texturecolor, cfg.bouncecolorscale.value());

        // final colors to emit
        map<int, qvec3f> emitcolors;
        for (const auto &styleColor : sum) {
            qvec3f emitcolor{};
            for (int k = 0; k < 3; k++) {
                emitcolor[k] = styleColor.second[k] * blendedcolor[k];
            }
            emitcolors[styleColor.first] = emitcolor;
        }

        AddBounceLight(facemidpoint, emitcolors, faceplane.normal, facearea, face, bsp);
    }

    return NULL;
}

const std::vector<bouncelight_t> &BounceLights()
{
    return radlights;
}

const std::vector<int> &BounceLightsForFaceNum(int facenum)
{
    const auto &vec = radlightsByFacenum.find(facenum);
    if (vec != radlightsByFacenum.end()) {
        return vec->second;
    }

    static std::vector<int> empty;
    return empty;
}

void MakeBounceLights(const settings::worldspawn_keys &cfg, const mbsp_t *bsp)
{
    LogPrint("--- MakeBounceLights ---\n");

    make_bounce_lights_args_t args{
        bsp, &cfg}; // mxd. https://clang.llvm.org/extra/clang-tidy/checks/cppcoreguidelines-pro-type-member-init.html

    RunThreadsOn(0, bsp->dfaces.size(), MakeBounceLightsThread, (void *)&args);

    LogPrint("{} bounce lights created\n", radlights.size());
}
