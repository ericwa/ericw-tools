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

#include <glm/glm.hpp>

using namespace std;
using namespace glm;
using namespace polylib;

mutex radlights_lock;
map<string, vec3> texturecolors;
std::vector<bouncelight_t> radlights;
std::map<int, std::vector<bouncelight_t>> radlightsByFacenum; // duplicate of `radlights` but indexed by face

class patch_t {
public:
    winding_t *w;
    vec3_t center;
    vec3_t samplepoint; // 1 unit above center
    plane_t plane;
    vec3_t directlight;
};

static unique_ptr<patch_t>
MakePatch (const globalconfig_t &cfg, winding_t *w)
{
    unique_ptr<patch_t> p { new patch_t };
    p->w = w;
    
    // cache some stuff
    WindingCenter(p->w, p->center);
    WindingPlane(p->w, p->plane.normal, &p->plane.dist);
    
    // nudge the cernter point 1 unit off
    VectorMA(p->center, 1.0f, p->plane.normal, p->samplepoint);
    
    // calculate direct light
    
    raystream_t *rs = MakeRayStream(numDirtVectors);
    GetDirectLighting(cfg, rs, p->samplepoint, p->plane.normal, p->directlight);
    delete rs;
    
    return p;
}

struct make_bounce_lights_args_t {
    const bsp2_t *bsp;
    const globalconfig_t *cfg;
};

struct save_winding_args_t {
    vector<unique_ptr<patch_t>> *patches;
    const globalconfig_t *cfg;
};

static void SaveWindingFn(winding_t *w, void *userinfo)
{
    save_winding_args_t *args = static_cast<save_winding_args_t *>(userinfo);
    args->patches->push_back(MakePatch(*args->cfg, w));
}

static bool
Face_ShouldBounce(const bsp2_t *bsp, const bsp2_dface_t *face)
{
    // make bounce light, only if this face is shadow casting
    const modelinfo_t *mi = ModelInfoForFace(bsp, static_cast<int>(face - bsp->dfaces));
    if (!mi || !mi->shadow.boolValue()) {
        return false;
    }
    
    if (bsp->texinfo[face->texinfo].flags & TEX_SPECIAL) {
        return false;
    }
    
    const char *texname = Face_TextureName(bsp, face);
    if (!strcmp("skip", texname)) {
        return false;
    }
    
    return true;
}

static void
Face_LookupTextureColor(const bsp2_t *bsp, const bsp2_dface_t *face, vec3_t color)
{
    const char *facename = Face_TextureName(bsp, face);
    
    if (texturecolors.find(facename) != texturecolors.end()) {
        const vec3 texcolor = texturecolors.at(facename);
        VectorCopyFromGLM(texcolor, color);
    } else {
        VectorSet(color, 127, 127, 127);
    }
}

static void
AddBounceLight(const vec3_t pos, const vec3_t color, const vec3_t surfnormal, vec_t area, const bsp2_dface_t *face, const bsp2_t *bsp);

static void *
MakeBounceLightsThread (void *arg)
{
    const bsp2_t *bsp = static_cast<make_bounce_lights_args_t *>(arg)->bsp;
    const globalconfig_t &cfg = *static_cast<make_bounce_lights_args_t *>(arg)->cfg;
    
    while (1) {
        int i = GetThreadWork();
        if (i == -1)
            break;
    
        const bsp2_dface_t *face = &bsp->dfaces[i];
        
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
        args.cfg = &cfg;
        
        DiceWinding(winding, 64.0f, SaveWindingFn, &args);
        winding = nullptr; // DiceWinding frees winding
        
        // average them, area weighted
        vec3_t sum = {0,0,0};
        float totalarea = 0;
        
        for (const auto &patch : patches) {
            const float patcharea = WindingArea(patch->w);
            totalarea += patcharea;
            
            VectorMA(sum, patcharea, patch->directlight, sum);
//              printf("  %f %f %f\n", patch->directlight[0], patch->directlight[1], patch->directlight[2]);
        }
        VectorScale(sum, 1.0/totalarea, sum);
        
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
        
        // final color to emit
        vec3_t emitcolor;
        for (int k=0; k<3; k++) {
            emitcolor[k] = (sum[k] / 255.0f) * (blendedcolor[k] / 255.0f);
        }
        
        AddBounceLight(facemidpoint, emitcolor, faceplane.normal, facearea, face, bsp);
    }
    
    return NULL;
}

static void
AddBounceLight(const vec3_t pos, const vec3_t color, const vec3_t surfnormal, vec_t area, const bsp2_dface_t *face, const bsp2_t *bsp)
{
    Q_assert(color[0] >= 0);
    Q_assert(color[1] >= 0);
    Q_assert(color[2] >= 0);
    Q_assert(area > 0);
    
    bouncelight_t l = {0};
    VectorCopy(pos, l.pos);
    VectorCopy(color, l.color);
    VectorCopy(surfnormal, l.surfnormal);
    l.area = area;
    
    if (!novisapprox) {
        EstimateVisibleBoundsAtPoint(pos, l.mins, l.maxs);
    }
    
    unique_lock<mutex> lck { radlights_lock };
    radlights.push_back(l);
    radlightsByFacenum[Face_GetNum(bsp, face)].push_back(l);
}

const std::vector<bouncelight_t> &BounceLights()
{
    return radlights;
}

std::vector<bouncelight_t> BounceLightsForFaceNum(int facenum)
{
    const auto &vec = radlightsByFacenum.find(facenum);
    if (vec != radlightsByFacenum.end()) {
        return vec->second;
    }
    return {};
}

void Palette_GetColor(int i, vec3_t samplecolor)
{
    samplecolor[0] = (float)thepalette[3*i];
    samplecolor[1] = (float)thepalette[3*i + 1];
    samplecolor[2] = (float)thepalette[3*i + 2];
}

// Returns color in [0,1]
static void
Texture_AvgColor (const bsp2_t *bsp, const miptex_t *miptex, vec3_t color)
{
    VectorSet(color, 0, 0, 0);
    if (!bsp->texdatasize)
        return;
    
    const byte *data = (byte*)miptex + miptex->offsets[0];
    for (int y=0; y<miptex->height; y++) {
        for (int x=0; x<miptex->width; x++) {
            const int i = data[(miptex->width * y) + x];
            
            vec3_t samplecolor;
            Palette_GetColor(i, samplecolor);
            VectorAdd(color, samplecolor, color);
        }
    }
    VectorScale(color, 1.0 / (miptex->width * miptex->height), color);
}

void
MakeTextureColors (const bsp2_t *bsp)
{
    logprint("--- MakeTextureColors ---\n");
 
    if (!bsp->texdatasize)
        return;
    
    for (int i=0; i<bsp->dtexdata.header->nummiptex; i++) {
        const int ofs = bsp->dtexdata.header->dataofs[i];
        if (ofs < 0)
            continue;
        
        const miptex_t *miptex = (miptex_t *)(bsp->dtexdata.base + ofs);
        
        string name { miptex->name };
        vec3_t color;
        Texture_AvgColor(bsp, miptex, color);
        
//        printf("%s has color %f %f %f\n", name.c_str(), color.v[0], color.v[1], color.v[2]);
        texturecolors[name] = VectorToGLM(color);
    }
}

void
MakeBounceLights (const globalconfig_t &cfg, const bsp2_t *bsp)
{
    logprint("--- MakeBounceLights ---\n");
    
    const dmodel_t *model = &bsp->dmodels[0];
    
    make_bounce_lights_args_t args;
    args.bsp = bsp;
    args.cfg = &cfg;
    
    RunThreadsOn(model->firstface, model->firstface + model->numfaces, MakeBounceLightsThread, (void *)&args);
}
