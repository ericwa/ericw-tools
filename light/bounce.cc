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
#include <cstdio>
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
map<string, qvec3f> texturecolors;
static std::vector<bouncelight_t> radlights;
std::map<int, std::vector<int>> radlightsByFacenum;

class patch_t {
public:
    winding_t *w;
    vec3_t center;
    vec3_t samplepoint; // 1 unit above center
    plane_t plane;
    std::map<int, qvec3f> lightByStyle;
};

static unique_ptr<patch_t>
MakePatch (const mbsp_t *bsp, const globalconfig_t &cfg, winding_t *w)
{
    unique_ptr<patch_t> p { new patch_t };
    p->w = w;
    
    // cache some stuff
    WindingCenter(p->w, p->center);
    WindingPlane(p->w, p->plane.normal, &p->plane.dist);
    
    // nudge the cernter point 1 unit off
    VectorMA(p->center, 1.0f, p->plane.normal, p->samplepoint);
    
    // calculate direct light
    
    p->lightByStyle = GetDirectLighting(bsp, cfg, p->samplepoint, p->plane.normal);
    
    return p;
}

struct make_bounce_lights_args_t {
    const mbsp_t *bsp;
    const globalconfig_t *cfg;
};

struct save_winding_args_t {
    vector<unique_ptr<patch_t>> *patches;
    const mbsp_t *bsp;
    const globalconfig_t *cfg;
};

static void SaveWindingFn(winding_t *w, void *userinfo)
{
    save_winding_args_t *args = static_cast<save_winding_args_t *>(userinfo);
    args->patches->push_back(MakePatch(args->bsp, *args->cfg, w));
}

static bool
Face_ShouldBounce(const mbsp_t *bsp, const bsp2_dface_t *face)
{
    // make bounce light, only if this face is shadow casting
    const modelinfo_t *mi = ModelInfoForFace(bsp, static_cast<int>(face - bsp->dfaces));
    if (!mi || !mi->shadow.boolValue()) {
        return false;
    }
    
    if (!Face_IsLightmapped(bsp, face)) {
        return false;
    }
    
    const char *texname = Face_TextureName(bsp, face);
    if (!strcmp("skip", texname)) {
        return false;
    }

    // check for "_bounce" "-1"
    if (extended_texinfo_flags[face->texinfo] & TEX_NOBOUNCE) {
        return false;
    }

    return true;
}

void
Face_LookupTextureColor(const mbsp_t *bsp, const bsp2_dface_t *face, vec3_t color)
{
    const char *facename = Face_TextureName(bsp, face);
    
    if (texturecolors.find(facename) != texturecolors.end()) {
        const qvec3f texcolor = texturecolors.at(facename);
        VectorCopyFromGLM(texcolor, color);
    } else {
        VectorSet(color, 127, 127, 127);
    }
}

static void
AddBounceLight(const vec3_t pos, const std::map<int, qvec3f> &colorByStyle, const vec3_t surfnormal, vec_t area, const bsp2_dface_t *face, const mbsp_t *bsp);

static void *
MakeBounceLightsThread (void *arg)
{
    const mbsp_t *bsp = static_cast<make_bounce_lights_args_t *>(arg)->bsp;
    const globalconfig_t &cfg = *static_cast<make_bounce_lights_args_t *>(arg)->cfg;
    
    while (1) {
        int i = GetThreadWork();
        if (i == -1)
            break;
    
        const bsp2_dface_t *face = BSP_GetFace(bsp, i);
        
        if (!Face_ShouldBounce(bsp, face)) {
            continue;
        }
        
        vector<unique_ptr<patch_t>> patches;
        
        winding_t *winding = WindingFromFace(bsp, face);
        // grab some info about the face winding
        const float facearea = WindingArea(winding);
        
        plane_t faceplane;
        WindingPlane(winding, faceplane.normal, &faceplane.dist);
        
        vec3_t facemidpoint;
        WindingCenter(winding, facemidpoint);
        VectorMA(facemidpoint, 1, faceplane.normal, facemidpoint); // lift 1 unit
        
        save_winding_args_t args;
        args.patches = &patches;
        args.bsp = bsp;
        args.cfg = &cfg;
        
        DiceWinding(winding, 64.0f, SaveWindingFn, &args);
        winding = nullptr; // DiceWinding frees winding
        
        // average them, area weighted
        map<int, qvec3f> sum;
        float totalarea = 0;
        
        for (const auto &patch : patches) {
            const float patcharea = WindingArea(patch->w);
            totalarea += patcharea;
            
            for (const auto &styleColor : patch->lightByStyle) {
                sum[styleColor.first] = sum[styleColor.first] + (styleColor.second * patcharea);
            }
//              printf("  %f %f %f\n", patch->directlight[0], patch->directlight[1], patch->directlight[2]);
        }
        
        for (auto &styleColor : sum) {
            styleColor.second *= (1.0/totalarea);
        }
        
        // avoid small, or zero-area patches ("sum" would be nan)
        if (totalarea < 1) {
            continue;
        }
    
        vec3_t texturecolor;
        Face_LookupTextureColor(bsp, face, texturecolor);
        
        // lerp between gray and the texture color according to `bouncecolorscale`
        const vec3_t gray = {127, 127, 127};
        vec3_t blendedcolor = {0, 0, 0};
        VectorMA(blendedcolor, cfg.bouncecolorscale.floatValue(), texturecolor, blendedcolor);
        VectorMA(blendedcolor, 1-cfg.bouncecolorscale.floatValue(), gray, blendedcolor);
        
        // final colors to emit
        map<int, qvec3f> emitcolors;
        for (const auto &styleColor : sum) {
            qvec3f emitcolor(0);
            for (int k=0; k<3; k++) {
                emitcolor[k] = (styleColor.second[k] / 255.0f) * (blendedcolor[k] / 255.0f);
            }
            emitcolors[styleColor.first] = emitcolor;
        }

        AddBounceLight(facemidpoint, emitcolors, faceplane.normal, facearea, face, bsp);
    }
    
    return NULL;
}

static void
AddBounceLight(const vec3_t pos, const std::map<int, qvec3f> &colorByStyle, const vec3_t surfnormal, vec_t area, const bsp2_dface_t *face, const mbsp_t *bsp)
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
    l.pos = vec3_t_to_glm(pos);
    l.colorByStyle = colorByStyle;
    
    qvec3f componentwiseMaxColor(0);
    for (const auto &styleColor : colorByStyle) {
        for (int i=0; i<3; i++) {
            if (styleColor.second[i] > componentwiseMaxColor[i]) {
                componentwiseMaxColor[i] = styleColor.second[i];
            }
        }
    }
    l.componentwiseMaxColor = componentwiseMaxColor;
    l.surfnormal = vec3_t_to_glm(surfnormal);
    l.area = area;
    VectorSet(l.mins, 0, 0, 0);
    VectorSet(l.maxs, 0, 0, 0);
    
    if (!novisapprox) {
        EstimateVisibleBoundsAtPoint(pos, l.mins, l.maxs);
    }
    
    unique_lock<mutex> lck { radlights_lock };
    radlights.push_back(l);
    
    const int lastBounceLightIndex = static_cast<int>(radlights.size()) - 1;
    radlightsByFacenum[Face_GetNum(bsp, face)].push_back(lastBounceLightIndex);
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

// Returns color in [0,255]
static qvec3f
Texture_AvgColor (const mbsp_t *bsp, const rgba_miptex_t *miptex)
{
    qvec4f color(0);

    if (!bsp->rgbatexdatasize)
        return color;
    
    //mxd
    const auto miptexsize = miptex->width * miptex->height;
    for(auto i = 0; i < miptexsize; i++)
    {
        auto c = Texture_GetColor(miptex, i);
        if(c[3] < 128) continue; // Skip transparent pixels...
        color += c;
    }

    return color / miptexsize;
}

void
MakeTextureColors (const mbsp_t *bsp)
{
    logprint("--- MakeTextureColors ---\n");
 
    if (!bsp->rgbatexdatasize) //mxd. dtexdata -> drgbatexdata
        return;
    
    for (int i = 0; i<bsp->drgbatexdata->nummiptex; i++) {
        const int ofs = bsp->drgbatexdata->dataofs[i];
        if (ofs < 0)
            continue;
        
        const rgba_miptex_t *miptex = (rgba_miptex_t *)((uint8_t *)bsp->drgbatexdata + ofs);
        const string name { miptex->name };
        const qvec3f color = Texture_AvgColor(bsp, miptex);
        
//      printf("%s has color %s\n", name.c_str(), VecStr(color));
        texturecolors[name] = color;
    }
}

void
MakeBounceLights (const globalconfig_t &cfg, const mbsp_t *bsp)
{
    logprint("--- MakeBounceLights ---\n");
    
    make_bounce_lights_args_t args { bsp, &cfg }; //mxd. https://clang.llvm.org/extra/clang-tidy/checks/cppcoreguidelines-pro-type-member-init.html
    
    RunThreadsOn(0, bsp->numfaces, MakeBounceLightsThread, (void *)&args);

    logprint("%d bounce lights created\n", static_cast<int>(radlights.size()));
}
