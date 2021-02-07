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

#include <cstdint>
#include <cassert>
#include <cstdio>
#include <iostream>

#include <light/light.hh>
#include <light/phong.hh>
#include <light/bounce.hh>
#include <light/surflight.hh> //mxd
#include <light/imglib.hh> //mxd
#include <light/entities.hh>
#include <light/ltface.hh>

#include <common/polylib.hh>
#include <common/bsputils.hh>

#ifdef HAVE_EMBREE
#include <xmmintrin.h>
//#include <pmmintrin.h>
#endif

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

globalconfig_t cfg_static {};

bool dirt_in_use = false;

float fadegate = EQUAL_EPSILON;
int softsamples = 0;

const vec3_t vec3_white = { 255, 255, 255 };
float surflight_subdivide = 128.0f;
int sunsamples = 64;
qboolean scaledonly = false;

qboolean surflight_dump = false;

static facesup_t *faces_sup;    //lit2/bspx stuff

/// start of lightmap data
uint8_t *filebase;
/// offset of start of free space after data (should be kept a multiple of 4)
static int file_p;
/// offset of end of free space for lightmap data
static int file_end;

/// start of litfile data
uint8_t *lit_filebase;
/// offset of start of free space after litfile data (should be kept a multiple of 12)
static int lit_file_p;
/// offset of end of space for litfile data
static int lit_file_end;

/// start of luxfile data
uint8_t *lux_filebase;
/// offset of start of free space after luxfile data (should be kept a multiple of 12)
static int lux_file_p;
/// offset of end of space for luxfile data
static int lux_file_end;

std::vector<modelinfo_t *> modelinfo;
std::vector<const modelinfo_t *> tracelist;
std::vector<const modelinfo_t *> selfshadowlist;
std::vector<const modelinfo_t *> shadowworldonlylist;
std::vector<const modelinfo_t *> switchableshadowlist;

int oversample = 1;
int write_litfile = 0;  /* 0 for none, 1 for .lit, 2 for bspx, 3 for both */
int write_luxfile = 0;  /* 0 for none, 1 for .lux, 2 for bspx, 3 for both */
qboolean onlyents = false;
qboolean novisapprox = false;
bool nolights = false;
backend_t rtbackend = backend_embree;
bool debug_highlightseams = false;
debugmode_t debugmode = debugmode_none;
bool verbose_log = false;
bool litonly = false;

uint64_t *extended_texinfo_flags = NULL;

char mapfilename[1024];

int dump_facenum = -1;
bool dump_face;
vec3_t dump_face_point = {0,0,0};

int dump_vertnum = -1;
bool dump_vert;
vec3_t dump_vert_point = {0,0,0};

qboolean arghradcompat = false; //mxd

lockable_setting_t *FindSetting(std::string name) {
    settingsdict_t sd = cfg_static.settings();
    return sd.findSetting(name);
}

void SetGlobalSetting(std::string name, std::string value, bool cmdline) {
    settingsdict_t sd = cfg_static.settings();
    sd.setSetting(name, value, cmdline);
}

void FixupGlobalSettings() {
    static bool once = false;
    Q_assert(!once);
    once = true;
    
    // NOTE: This is confusing.. Setting "dirt" "1" implies "minlight_dirt" "1"
    // (and sunlight_dir/sunlight2_dirt as well), unless those variables were
    // set by the user to "0".
    //
    // We can't just default "minlight_dirt" to "1" because that would enable
    // dirtmapping by default.
    
    if (cfg_static.globalDirt.boolValue()) {
        if (!cfg_static.minlightDirt.isChanged()) {
            cfg_static.minlightDirt.setBoolValue(true);
        }
        if (!cfg_static.sunlight_dirt.isChanged()) {
            cfg_static.sunlight_dirt.setFloatValue(1);
        }
        if (!cfg_static.sunlight2_dirt.isChanged()) {
            cfg_static.sunlight2_dirt.setFloatValue(1);
        }
    }
}

static void
PrintOptionsSummary(void)
{
    logprint("--- OptionsSummary ---\n");
    
    settingsdict_t sd = cfg_static.settings();
    
    for (lockable_setting_t *setting : sd.allSettings()) {
        if (setting->isChanged()) {
            logprint("    \"%s\" was set to \"%s\" from %s\n",
                     setting->primaryName().c_str(),
                     setting->stringValue().c_str(),
                     setting->sourceString().c_str());
        }
    }
}

/*
 * Return space for the lightmap and colourmap at the same time so it can
 * be done in a thread-safe manner.
 *
 * size is the number of greyscale pixels = number of bytes to allocate
 * and return in *lightdata
 */
void
GetFileSpace(uint8_t **lightdata, uint8_t **colordata, uint8_t **deluxdata, int size)
{
    ThreadLock();

    *lightdata = filebase + file_p;
    *colordata = lit_filebase + lit_file_p;
    *deluxdata = lux_filebase + lux_file_p;

    // if size isn't a multiple of 4, round up to the next multiple of 4
    if ((size % 4) != 0) {
        size += (4 - (size % 4));
    }

    // increment the next writing offsets, aligning them to 4 uint8_t boundaries (file_p)
    // and 12-uint8_t boundaries (lit_file_p/lux_file_p)
    file_p += size;
    lit_file_p += 3 * size;
    lux_file_p += 3 * size;

    ThreadUnlock();

    if (file_p > file_end)
        Error("%s: overrun", __func__);

    if (lit_file_p > lit_file_end)
        Error("%s: overrun", __func__);
}

/**
 * Special version of GetFileSpace for when we're relighting a .bsp and can't modify it.
 * In this case the offsets are already known.
 */
void GetFileSpace_PreserveOffsetInBsp(uint8_t **lightdata, uint8_t **colordata, uint8_t **deluxdata, int lightofs) {
    Q_assert(lightofs >= 0);

    *lightdata = filebase + lightofs;

    if (colordata) {
        *colordata = lit_filebase + (lightofs * 3);
    }

    if (deluxdata) {
        *deluxdata = lux_filebase + (lightofs * 3);
    }

    // NOTE: file_p et. al. are not updated, since we're not dynamically allocating the lightmaps
}

const modelinfo_t *ModelInfoForModel(const mbsp_t *bsp, int modelnum)
{
    return modelinfo.at(modelnum);
}

const modelinfo_t *ModelInfoForFace(const mbsp_t *bsp, int facenum)
{
    int i;
    dmodel_t *model;
    
    /* Find the correct model offset */
    for (i = 0, model = bsp->dmodels; i < bsp->nummodels; i++, model++) {
        if (facenum < model->firstface)
            continue;
        if (facenum < model->firstface + model->numfaces)
            break;
    }
    if (i == bsp->nummodels) {
        return NULL;
    }
    return modelinfo.at(i);
}

static void *
LightThread(void *arg)
{
    const mbsp_t *bsp = (const mbsp_t *)arg;

#ifdef HAVE_EMBREE
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
//    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#endif

    while (1) {
        const int facenum = GetThreadWork();
        if (facenum == -1)
            break;

        bsp2_dface_t *f = const_cast<bsp2_dface_t*>(BSP_GetFace(const_cast<mbsp_t *>(bsp), facenum));
        
        /* Find the correct model offset */
        const modelinfo_t *face_modelinfo = ModelInfoForFace(bsp, facenum);
        if (face_modelinfo == NULL) {
            // ericw -- silenced this warning becasue is causes spam when "skip" faces are used
            //logprint("warning: no model has face %d\n", facenum);
            continue;
        }
        
        if (!faces_sup)
            LightFace(bsp, f, nullptr, cfg_static);
        else if (scaledonly)
        {
            f->lightofs = -1;
            f->styles[0] = 255;
            LightFace(bsp, f, faces_sup + facenum, cfg_static);
        }
        else if (faces_sup[facenum].lmscale == face_modelinfo->lightmapscale)
        {
            LightFace(bsp, f, nullptr, cfg_static);
            faces_sup[facenum].lightofs = f->lightofs;
            for (int i = 0; i < MAXLIGHTMAPS; i++)
                faces_sup[facenum].styles[i] = f->styles[i];
        }
        else
        {
            LightFace(bsp, f, nullptr, cfg_static);
            LightFace(bsp, f, faces_sup + facenum, cfg_static);
        }
    }

    return NULL;
}

static void
FindModelInfo(const mbsp_t *bsp, const char *lmscaleoverride)
{
    Q_assert(modelinfo.size() == 0);
    Q_assert(tracelist.size() == 0);
    Q_assert(selfshadowlist.size() == 0);
    Q_assert(shadowworldonlylist.size() == 0);
    Q_assert(switchableshadowlist.size() == 0);
    
    if (!bsp->nummodels) {
        Error("Corrupt .BSP: bsp->nummodels is 0!");
    }
    
    if (lmscaleoverride)
        SetWorldKeyValue("_lightmap_scale", lmscaleoverride);

    float lightmapscale = atoi(WorldValueForKey("_lightmap_scale").c_str());
    if (!lightmapscale)
        lightmapscale = 16; /* the default */
    if (lightmapscale <= 0)
        Error("lightmap scale is 0 or negative\n");
    if (lmscaleoverride || lightmapscale != 16)
        logprint("Forcing lightmap scale of %gqu\n", lightmapscale);
    /*I'm going to do this check in the hopes that there's a benefit to cheaper scaling in engines (especially software ones that might be able to just do some mip hacks). This tool doesn't really care.*/
    {
        int i;
        for (i = 1; i < lightmapscale;) {
            i++;
        }
        if (i != lightmapscale) {
            logprint("WARNING: lightmap scale is not a power of 2\n");
        }
    }
    
    /* The world always casts shadows */
    modelinfo_t *world = new modelinfo_t { bsp, &bsp->dmodels[0], lightmapscale };
    world->shadow.setFloatValue(1.0f); /* world always casts shadows */
    world->phong_angle = cfg_static.phongangle;
    modelinfo.push_back(world);
    tracelist.push_back(world);
    
    for (int i = 1; i < bsp->nummodels; i++) {
        modelinfo_t *info = new modelinfo_t { bsp, &bsp->dmodels[i], lightmapscale };
        modelinfo.push_back(info);
        
        /* Find the entity for the model */
        std::stringstream ss;
        ss << "*" << i;
        std::string modelname = ss.str();
        
        const entdict_t *entdict = FindEntDictWithKeyPair("model", modelname);
        if (entdict == nullptr)
            Error("%s: Couldn't find entity for model %s.\n", __func__,
                  modelname.c_str());

        // apply settings
        info->settings().setSettings(*entdict, false);
        
        /* Check if this model will cast shadows (shadow => shadowself) */
        if (info->switchableshadow.boolValue()) {
            Q_assert(info->switchshadstyle.intValue() != 0);
            switchableshadowlist.push_back(info);
        } else if (info->shadow.boolValue()) {
            tracelist.push_back(info);
        } else if (info->shadowself.boolValue()){
            selfshadowlist.push_back(info);
        } else if (info->shadowworldonly.boolValue()){
            shadowworldonlylist.push_back(info);
        }

        /* Set up the offset for rotate_* entities */
        EntDict_VectorForKey(*entdict, "origin", info->offset);
    }

    Q_assert(modelinfo.size() == bsp->nummodels);
}

/*
 * =============
 *  LightWorld
 * =============
 */
static void
LightWorld(bspdata_t *bspdata, qboolean forcedscale)
{
    logprint("--- LightWorld ---\n" );
    
    mbsp_t *const bsp = &bspdata->data.mbsp;
    free(filebase);
    free(lit_filebase);
    free(lux_filebase);

    /* greyscale data stored in a separate buffer */
    filebase = (uint8_t *)calloc(MAX_MAP_LIGHTING, 1);
    if (!filebase)
        Error("%s: allocation of %i bytes failed.", __func__, MAX_MAP_LIGHTING);
    file_p = 0;
    file_end = MAX_MAP_LIGHTING;

    /* litfile data stored in a separate buffer */
    lit_filebase = (uint8_t *)calloc(MAX_MAP_LIGHTING*3, 1);
    if (!lit_filebase)
        Error("%s: allocation of %i bytes failed.", __func__, MAX_MAP_LIGHTING*3);
    lit_file_p = 0;
    lit_file_end = (MAX_MAP_LIGHTING*3);

    /* lux data stored in a separate buffer */
    lux_filebase = (uint8_t *)calloc(MAX_MAP_LIGHTING*3, 1);
    if (!lux_filebase)
        Error("%s: allocation of %i bytes failed.", __func__, MAX_MAP_LIGHTING*3);
    lux_file_p = 0;
    lux_file_end = (MAX_MAP_LIGHTING*3);

    if (forcedscale)
        BSPX_AddLump(bspdata, "LMSHIFT", NULL, 0);

    const unsigned char *lmshift_lump = (const unsigned char *)BSPX_GetLump(bspdata, "LMSHIFT", NULL);
    if (!lmshift_lump && write_litfile != ~0)
        faces_sup = NULL; //no scales, no lit2
    else
    {   //we have scales or lit2 output. yay...
        faces_sup = (facesup_t *)malloc(sizeof(*faces_sup) * bsp->numfaces);
        memset(faces_sup, 0, sizeof(*faces_sup) * bsp->numfaces);
        if (lmshift_lump)
        {
            for (int i = 0; i < bsp->numfaces; i++)
                faces_sup[i].lmscale = 1<<lmshift_lump[i];
        }
        else
        {
            for (int i = 0; i < bsp->numfaces; i++)
                faces_sup[i].lmscale = modelinfo.at(0)->lightmapscale;
        }
    }

    CalcualateVertexNormals(bsp);
    
    const qboolean bouncerequired = cfg_static.bounce.boolValue() && (debugmode == debugmode_none || debugmode == debugmode_bounce || debugmode == debugmode_bouncelights); //mxd
    const qboolean isQuake2map = (bsp->loadversion == Q2_BSPVERSION); //mxd

    if (bouncerequired || isQuake2map) {
        MakeTextureColors(bsp);
        if (isQuake2map)   MakeSurfaceLights(cfg_static, bsp);
        if (bouncerequired) MakeBounceLights(cfg_static, bsp);
    }
    
#if 0
    lightbatchthread_info_t info;
    info.all_batches = MakeLightingBatches(bsp);
    info.all_contribFaces = MakeContributingFaces(bsp);
    info.bsp = bsp;
    RunThreadsOn(0, info.all_batches.size(), LightBatchThread, &info);
#else
    logprint("--- LightThread ---\n"); //mxd
    RunThreadsOn(0, bsp->numfaces, LightThread, bsp);
#endif

    if (bouncerequired || isQuake2map) { //mxd. Print some extra stats...
        logprint("Indirect lights: %i bounce lights, %i surface lights (%i light points) in use.\n",
                 static_cast<int>(BounceLights().size()),
                 static_cast<int>(SurfaceLights().size()),
                 static_cast<int>(TotalSurfacelightPoints()));
    }

    logprint("Lighting Completed.\n\n");

    // Transfer greyscale lightmap (or color lightmap for Q2/HL) to the bsp and update lightdatasize
    if (!litonly) {
        free(bsp->dlightdata);
        if (bsp->loadversion == Q2_BSPVERSION || bsp->loadversion == BSPHLVERSION) {
            bsp->lightdatasize = lit_file_p;
            bsp->dlightdata = (uint8_t *)malloc(bsp->lightdatasize);
            memcpy(bsp->dlightdata, lit_filebase, bsp->lightdatasize);
        } else {
            bsp->lightdatasize = file_p;
            bsp->dlightdata = (uint8_t *)malloc(bsp->lightdatasize);
            memcpy(bsp->dlightdata, filebase, bsp->lightdatasize);
        }
    } else {
        // NOTE: bsp->lightdatasize is already valid in the -litonly case
    }
    logprint("lightdatasize: %i\n", bsp->lightdatasize);

    if (faces_sup) {
        uint8_t *styles = (uint8_t *)malloc(sizeof(*styles)*4*bsp->numfaces);
        int32_t *offsets = (int32_t *)malloc(sizeof(*offsets)*bsp->numfaces);
        for (int i = 0; i < bsp->numfaces; i++) {
            offsets[i] = faces_sup[i].lightofs;
            for (int j = 0; j < MAXLIGHTMAPS; j++)
                styles[i*4+j] = faces_sup[i].styles[j];
        }
        BSPX_AddLump(bspdata, "LMSTYLE", styles, sizeof(*styles)*4*bsp->numfaces);
        BSPX_AddLump(bspdata, "LMOFFSET", offsets, sizeof(*offsets)*bsp->numfaces);
    } else { 
        //kill this stuff if its somehow found.
        BSPX_AddLump(bspdata, "LMSTYLE", NULL, 0);
        BSPX_AddLump(bspdata, "LMOFFSET", NULL, 0);
    }
}

static void
LoadExtendedTexinfoFlags(const char *sourcefilename, const mbsp_t *bsp)
{
    char filename[1024];
    
    // always create the zero'ed array
    extended_texinfo_flags = static_cast<uint64_t *>(calloc(bsp->numtexinfo, sizeof(uint64_t)));
    
    strcpy(filename, sourcefilename);
    StripExtension(filename);
    DefaultExtension(filename, ".texinfo");

    FILE *texinfofile = fopen(filename, "rt");
    if (!texinfofile)
        return;
    
    logprint("Loaded extended texinfo flags from %s\n", filename);
    
    for (int i = 0; i < bsp->numtexinfo; i++) {
        long long unsigned int flags = 0;
        const int cnt = fscanf(texinfofile, "%llu\n", &flags);
        if (cnt != 1) {
            logprint("WARNING: Extended texinfo flags in %s does not match bsp, ignoring\n", filename);
            fclose(texinfofile);
            memset(extended_texinfo_flags, 0, bsp->numtexinfo * sizeof(uint32_t));
            return;
        }
        extended_texinfo_flags[i] = flags;
    }
    
    // fail if there are more lines in the file
    if (fgetc(texinfofile) != EOF) {
        logprint("WARNING: Extended texinfo flags in %s does not match bsp, ignoring\n", filename);
        fclose(texinfofile);
        memset(extended_texinfo_flags, 0, bsp->numtexinfo * sizeof(uint32_t));
        return;
    }
    
    fclose(texinfofile);
}

static const char* //mxd
GetBaseDirName(bspdata_t *bspdata)
{
    if (bspdata->loadversion == Q2_BSPVERSION)
        return "BASEQ2";
    if (bspdata->hullcount == MAX_MAP_HULLS_H2)
        return "DATA1";
    return "ID1";
}

//obj

static FILE *
InitObjFile(const char *filename)
{
    char objfilename[1024];
    strcpy(objfilename, filename);
    StripExtension(objfilename);
    DefaultExtension(objfilename, ".obj");
    
    FILE *objfile = fopen(objfilename, "wt");
    if (!objfile)
        Error("Failed to open %s: %s", objfilename, strerror(errno));
    
    return objfile;
}

static void
ExportObjFace(FILE *f, const mbsp_t *bsp, const bsp2_dface_t *face, int *vertcount)
{
    // export the vertices and uvs
    for (int i=0; i<face->numedges; i++)
    {
        const int vertnum = Face_VertexAtIndex(bsp, face, i);
        const qvec3f normal = GetSurfaceVertexNormal(bsp, face, i);
        const float *pos = bsp->dvertexes[vertnum].point;
        fprintf(f, "v %.9g %.9g %.9g\n", pos[0], pos[1], pos[2]);
        fprintf(f, "vn %.9g %.9g %.9g\n", normal[0], normal[1], normal[2]);
    }
    
    fprintf(f, "f");
    for (int i=0; i<face->numedges; i++) {
        // .obj vertexes start from 1
        // .obj faces are CCW, quake is CW, so reverse the order
        const int vertindex = *vertcount + (face->numedges - 1 - i) + 1;
        fprintf(f, " %d//%d", vertindex, vertindex);
    }
    fprintf(f, "\n");
    
    *vertcount += face->numedges;
}

static void
ExportObj(const char *filename, const mbsp_t *bsp)
{
    FILE *objfile = InitObjFile(filename);
    int vertcount = 0;
    
    const int start = bsp->dmodels[0].firstface;
    const int end = bsp->dmodels[0].firstface + bsp->dmodels[0].numfaces;
    
    for (int i=start; i<end; i++) {
        ExportObjFace(objfile, bsp, BSP_GetFace(bsp, i), &vertcount);
    }
    
    fclose(objfile);
    
    logprint("Wrote %s\n", filename);
}


//obj

static void
CheckNoDebugModeSet()
{
    if (debugmode != debugmode_none) {
        Error("Only one debug mode is allowed at a time");
    }
}

// returns the face with a centroid nearest the given point.
static const bsp2_dface_t *
Face_NearestCentroid(const mbsp_t *bsp, const qvec3f &point)
{
    const bsp2_dface_t *nearest_face = NULL;
    float nearest_dist = VECT_MAX;
    
    for (int i=0; i<bsp->numfaces; i++) {
        const bsp2_dface_t *f = BSP_GetFace(bsp, i);
        
        const qvec3f fc = Face_Centroid(bsp, f);
        
        const qvec3f distvec = fc - point;
        const float dist = qv::length(distvec);
        
        if (dist < nearest_dist) {
            nearest_dist = dist;
            nearest_face = f;
        }
    }
    
    return nearest_face;
}

static void
FindDebugFace(const mbsp_t *bsp)
{
    if (!dump_face)
        return;
    
    const bsp2_dface_t *f = Face_NearestCentroid(bsp, vec3_t_to_glm(dump_face_point));
    if (f == NULL)
        Error("FindDebugFace: f == NULL\n");

    const int facenum = f - bsp->dfaces;
    
    dump_facenum = facenum;
    
    const modelinfo_t *mi = ModelInfoForFace(bsp, facenum);
    const int modelnum = mi ? (mi->model - bsp->dmodels) : -1;
    
    const char *texname = Face_TextureName(bsp, f);
    
    logprint("FindDebugFace: dumping face %d (texture '%s' model %d)\n", facenum, texname, modelnum);
}

// returns the vert nearest the given point
static int
Vertex_NearestPoint(const mbsp_t *bsp, const vec3_t point)
{
    int nearest_vert = -1;
    vec_t nearest_dist = VECT_MAX;
    
    for (int i=0; i<bsp->numvertexes; i++) {
        const dvertex_t *vertex = &bsp->dvertexes[i];
        
        vec3_t distvec;
        VectorSubtract(vertex->point, point, distvec);
        vec_t dist = VectorLength(distvec);
        
        if (dist < nearest_dist) {
            nearest_dist = dist;
            nearest_vert = i;
        }
    }
    
    return nearest_vert;
}

static void
FindDebugVert(const mbsp_t *bsp)
{
    if (!dump_vert)
        return;
    
    int v = Vertex_NearestPoint(bsp, dump_vert_point);
    const dvertex_t *vertex = &bsp->dvertexes[v];
    
    logprint("FindDebugVert: dumping vert %d at %f %f %f\n", v,
             vertex->point[0],
             vertex->point[1],
             vertex->point[2]);
    dump_vertnum = v;
}

static void SetLitNeeded()
{
    if (!write_litfile) {
        if (scaledonly) {
            write_litfile = 2;
            logprint("Colored light entities/settings detected: "
                     "bspxlit output enabled.\n");
        } else {
            write_litfile = 1;
            logprint("Colored light entities/settings detected: "
                     ".lit output enabled.\n");
        }
    }
}

static void CheckLitNeeded(const globalconfig_t &cfg)
{
    const vec3_t white = {255,255,255};
    
    // check lights
    for (const auto &light : GetLights()) {
        if (!VectorCompare(white, *light.color.vec3Value(), EQUAL_EPSILON) || light.projectedmip != nullptr) { //mxd. Projected mips could also use .lit output
            SetLitNeeded();
            return;
        }
    }
    
    // check global settings
    if (cfg.bouncecolorscale.floatValue() != 0 ||
        !VectorCompare(*cfg.minlight_color.vec3Value(), white, EQUAL_EPSILON) ||
        !VectorCompare(*cfg.sunlight_color.vec3Value(), white, EQUAL_EPSILON) ||
        !VectorCompare(*cfg.sun2_color.vec3Value(), white, EQUAL_EPSILON) ||
        !VectorCompare(*cfg.sunlight2_color.vec3Value(), white, EQUAL_EPSILON) ||
        !VectorCompare(*cfg.sunlight3_color.vec3Value(), white, EQUAL_EPSILON)) {
        SetLitNeeded();
        return;
    }
}

static void PrintLight(const light_t &light)
{
    bool first = true;
    
    auto settings = const_cast<light_t&>(light).settings();
    for (const auto &setting : settings.allSettings()) {
        if (!setting->isChanged())
            continue; // don't spam default values
        
        // print separator
        if (!first) {
            logprint("; ");
        } else {
            first = false;
        }
        
        logprint("%s=%s", setting->primaryName().c_str(), setting->stringValue().c_str());
    }
    logprint("\n");
}

static void PrintLights(void)
{
    logprint("===PrintLights===\n");
    
    for (const auto &light: GetLights()) {
        PrintLight(light);
    }
}

static void PrintUsage()
{
    printf("usage: light [options] mapname.bsp\n"
"\n"
"Performance options:\n"
"  -threads n          set the number of threads\n"
"  -extra              2x supersampling\n"
"  -extra4             4x supersampling, slowest, use for final compile\n"
"  -gate n             cutoff lights at this brightness level\n"
"  -sunsamples n       set samples for _sunlight2, default 64\n"
"  -surflight_subdivide  surface light subdivision size\n"
"\n"
"Output format options:\n"
"  -lit                write .lit file\n"
"  -onlyents           only update entities\n"
"\n"
"Postprocessing options:\n"
"  -soft [n]           blurs the lightmap, n=blur radius in samples\n"
"\n"
"Debug modes:\n"
"  -dirtdebug          only save the AO values to the lightmap\n"
"  -phongdebug         only save the normals to the lightmap\n"
"  -bouncedebug        only save bounced lighting to the lightmap\n"
"  -surflight_dump     dump surface lights to a .map file\n"
"  -novisapprox        disable approximate visibility culling of lights\n"
"\n"
"Experimental options:\n"
"  -lit2               write .lit2 file\n"
"  -lmscale n          change lightmap scale, vanilla engines only allow 16\n"
"  -lux                write .lux file\n"
"  -bspxlit            writes rgb data into the bsp itself\n"
"  -bspx               writes both rgb and directions data into the bsp itself\n"
"  -novanilla          implies -bspxlit. don't write vanilla lighting\n"
"  -radlights filename.rad loads a <surfacename> <r> <g> <b> <intensity> file\n");
    
    printf("\n");
    printf("Overridable worldspawn keys:\n");
    settingsdict_t dict = cfg_static.settings();
    for (const auto &s : dict.allSettings()) {
        printf("  ");
        for (int i=0; i<s->names().size(); i++) {
            const auto &name = s->names().at(i);
            
            printf("-%s ", name.c_str());
            
            if (dynamic_cast<lockable_vec_t *>(s)) {
                printf("[n] ");
            } else if (dynamic_cast<lockable_bool_t *>(s)) {
                printf("[0,1] ");
            } else if (dynamic_cast<lockable_vec3_t *>(s)) {
                printf("[n n n] ");
            } else if (dynamic_cast<lockable_string_t *>(s)) {
                printf("\"str\" ");
            } else {
                Q_assert_unreachable();
            }
            
            if ((i+1) < s->names().size()) {
                printf("| ");
            }
        }
        printf("\n");
    }
}

static bool ParseVec3Optional(vec3_t vec3_out, int *i_inout, int argc, const char **argv)
{
    if ((*i_inout + 3) < argc) {
        const int start = (*i_inout + 1);
        const int end = (*i_inout + 3);
        
        // validate that there are 3 numbers
        for (int j=start; j <= end; j++) {
            if (argv[j][0] == '-' && isdigit(argv[j][1])) {
                continue; // accept '-' followed by a digit for negative numbers
            }
            
            // otherwise, reject if the first character is not a digit
            if (!isdigit(argv[j][0])) {
                return false;
            }
        }

        vec3_out[0] = atof( argv[ ++(*i_inout) ] );
        vec3_out[1] = atof( argv[ ++(*i_inout) ] );
        vec3_out[2] = atof( argv[ ++(*i_inout) ] );
        return true;
    } else {
        return false;
    }
}

static bool ParseVecOptional(vec_t *result, int *i_inout, int argc, const char **argv)
{
    if ((*i_inout + 1) < argc) {
        if (!isdigit(argv[*i_inout + 1][0])) {
            return false;
        }
        *result = atof( argv[ ++(*i_inout) ] );
        return true;
    } else {
        return false;
    }
}

static bool ParseIntOptional(int *result, int *i_inout, int argc, const char **argv)
{
    if ((*i_inout + 1) < argc) {
        if (!isdigit(argv[*i_inout + 1][0])) {
            return false;
        }
        *result = atoi( argv[ ++(*i_inout) ] );
        return true;
    } else {
        return false;
    }
}

static const char *ParseStringOptional(int *i_inout, int argc, const char **argv)
{
    if ((*i_inout + 1) < argc) {
        return argv[ ++(*i_inout) ];
    } else {
        return NULL;
    }
}

static void ParseVec3(vec3_t vec3_out, int *i_inout, int argc, const char **argv)
{
    if (!ParseVec3Optional(vec3_out, i_inout, argc, argv)) {
        Error("%s requires 3 numberic arguments\n", argv[ *i_inout ]);
    }
}

static vec_t ParseVec(int *i_inout, int argc, const char **argv)
{
    vec_t result = 0;
    if (!ParseVecOptional(&result, i_inout, argc, argv)) {
        Error("%s requires 1 numeric argument\n", argv[ *i_inout ]);
        return 0;
    }
    return result;
}

static int ParseInt(int *i_inout, int argc, const char **argv)
{
    int result = 0;
    if (!ParseIntOptional(&result, i_inout, argc, argv)) {
        Error("%s requires 1 integer argument\n", argv[ *i_inout ]);
        return 0;
    }
    return result;
}

static const char *ParseString(int *i_inout, int argc, const char **argv)
{
    const char *result = NULL;
    if (!(result = ParseStringOptional(i_inout, argc, argv))) {
        Error("%s requires 1 string argument\n", argv[ *i_inout ]);
    }
    return result;
}

/*
 * ==================
 * main
 * light modelfile
 * ==================
 */
int
light_main(int argc, const char **argv)
{
    bspdata_t bspdata;
    mbsp_t *const bsp = &bspdata.data.mbsp;
    int32_t loadversion;
    int i;
    double start;
    double end;
    char source[1024];
    const char *lmscaleoverride = NULL;
    
    init_log("light.log");
    logprint("---- light / ericw-tools " stringify(ERICWTOOLS_VERSION) " ----\n");

    LowerProcessPriority();
    numthreads = GetDefaultThreads();
    
    globalconfig_t &cfg = cfg_static;
    
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-threads")) {
            numthreads = ParseInt(&i, argc, argv);
        } else if (!strcmp(argv[i], "-extra")) {
            oversample = 2;
            logprint("extra 2x2 sampling enabled\n");
        } else if (!strcmp(argv[i], "-extra4")) {
            oversample = 4;
            logprint("extra 4x4 sampling enabled\n");
        } else if (!strcmp(argv[i], "-gate")) {
            fadegate = ParseVec(&i, argc, argv);
            if (fadegate > 1) {
                logprint( "WARNING: -gate value greater than 1 may cause artifacts\n" );
            }
        } else if (!strcmp(argv[i], "-lit")) {
            write_litfile |= 1;
        } else if (!strcmp(argv[i], "-lit2")) {
            write_litfile = ~0;
        } else if (!strcmp(argv[i], "-lux")) {
            write_luxfile |= 1;
        } else if (!strcmp(argv[i], "-bspxlit")) {
            write_litfile |= 2;
        } else if (!strcmp(argv[i], "-bspxlux")) {
            write_luxfile |= 2;
        } else if (!strcmp(argv[i], "-bspxonly")) {
            write_litfile = 2;
            write_luxfile = 2;
            scaledonly = true;
        } else if (!strcmp(argv[i], "-bspx")) {
            write_litfile |= 2;
            write_luxfile |= 2;
        } else if (!strcmp(argv[i], "-novanilla")) {
            scaledonly = true;
        } else if ( !strcmp( argv[ i ], "-radlights" ) ) {
            if (!ParseLightsFile(argv[++i]))
                logprint( "Unable to read surfacelights file %s\n", argv[i] );
        } else if ( !strcmp( argv[ i ], "-lmscale" ) ) {
            lmscaleoverride = argv[++i];
        } else if (!strcmp(argv[i], "-soft")) {
            if ((i + 1) < argc && isdigit(argv[i + 1][0]))
                softsamples = ParseInt(&i, argc, argv);
            else
                softsamples = -1; /* auto, based on oversampling */
        } else if ( !strcmp( argv[ i ], "-dirtdebug" ) || !strcmp( argv[ i ], "-debugdirt" ) ) {
            CheckNoDebugModeSet();
            
            cfg.globalDirt.setBoolValueLocked(true);
            debugmode = debugmode_dirt;
            logprint( "Dirtmap debugging enabled\n" );
        } else if ( !strcmp( argv[ i ], "-bouncedebug" ) ) {
            CheckNoDebugModeSet();
            cfg.bounce.setBoolValueLocked(true);
            debugmode = debugmode_bounce;
            logprint( "Bounce debugging mode enabled on command line\n" );
        } else if ( !strcmp( argv[ i ], "-bouncelightsdebug" ) ) {
            CheckNoDebugModeSet();
            cfg.bounce.setBoolValueLocked(true);
            debugmode = debugmode_bouncelights;
            logprint( "Bounce emitters debugging mode enabled on command line\n" );
        } else if ( !strcmp( argv[ i ], "-surflight_subdivide" ) ) {
            surflight_subdivide = ParseVec(&i, argc, argv);
            surflight_subdivide = qmin(qmax(surflight_subdivide, 64.0f), 2048.0f);
            logprint( "Using surface light subdivision size of %f\n", surflight_subdivide);
        } else if ( !strcmp( argv[ i ], "-surflight_dump" ) ) {
            surflight_dump = true;
        } else if ( !strcmp( argv[ i ], "-sunsamples" ) ) {
            sunsamples = ParseInt(&i, argc, argv);
            sunsamples = qmin(qmax(sunsamples, 8), 2048);
            logprint( "Using sunsamples of %d\n", sunsamples);
        } else if ( !strcmp( argv[ i ], "-onlyents" ) ) {
            onlyents = true;
            logprint( "Onlyents mode enabled\n" );
        } else if ( !strcmp( argv[ i ], "-phongdebug" ) ) {
            CheckNoDebugModeSet();
            debugmode = debugmode_phong;
            write_litfile |= 1;
            logprint( "Phong shading debug mode enabled\n" );
        } else if ( !strcmp( argv[ i ], "-phongdebug_obj" ) ) {
            CheckNoDebugModeSet();
            debugmode = debugmode_phong_obj;
            logprint( "Phong shading debug mode (.obj export) enabled\n" );
        } else if ( !strcmp( argv[ i ], "-novisapprox" ) ) {
            novisapprox = true;
            logprint( "Skipping approximate light visibility\n" );
        } else if ( !strcmp( argv[ i ], "-nolights" ) ) {
            nolights = true;
            logprint( "Skipping all light entities (sunlight / minlight only)\n" );
        } else if ( !strcmp( argv[ i ], "-backend" ) ) {
            const char *requested = ParseString(&i, argc, argv);
            if (!strcmp(requested, "bsp")) {
                rtbackend = backend_bsp;
            } else if (!strcmp(requested, "embree")) {
                rtbackend = backend_embree;
            } else {
                Error("unknown backend %s", requested);
            }
        } else if ( !strcmp( argv[ i ], "-debugface" ) ) {
            ParseVec3(dump_face_point, &i, argc, argv);
            dump_face = true;
        } else if ( !strcmp( argv[ i ], "-debugvert" ) ) {
            ParseVec3(dump_vert_point, &i, argc, argv);
            dump_vert = true;
        } else if ( !strcmp( argv[ i ], "-debugoccluded" ) ) {
            CheckNoDebugModeSet();
            debugmode = debugmode_debugoccluded;
        } else if ( !strcmp( argv[ i ], "-debugneighbours" ) ) {
            ParseVec3(dump_face_point, &i, argc, argv);
            dump_face = true;
            
            CheckNoDebugModeSet();
            debugmode = debugmode_debugneighbours;
        } else if ( !strcmp( argv[ i ], "-highlightseams" ) ) {
            logprint("Highlighting lightmap seams\n");
            debug_highlightseams = true;
        } else if (!strcmp(argv[i], "-arghradcompat")) { //mxd
            logprint("Arghrad entity keys conversion enabled\n");
            arghradcompat = true;
        } else if (!strcmp(argv[i], "-litonly")) {
            logprint("-litonly specified; .bsp file will not be modified\n");
            litonly = true;
            write_litfile |= 1;
        } else if ( !strcmp( argv[ i ], "-verbose" ) || !strcmp( argv[ i ], "-v" ) ) { // Quark always passes -v
            verbose_log = true;
        } else if ( !strcmp( argv[ i ], "-help" ) ) {
            PrintUsage();
            exit(0);
        } else if (argv[i][0] == '-') {
            // hand over to the settings system
            std::string settingname { &argv[i][1] };
            lockable_setting_t *setting = FindSetting(settingname);
            if (setting == nullptr) {
                Error("Unknown option \"-%s\"", settingname.c_str());
                PrintUsage();
            }
            
            if (lockable_bool_t *boolsetting = dynamic_cast<lockable_bool_t *>(setting)) {
                float v;
                if (ParseVecOptional(&v, &i, argc, argv)) {
                    boolsetting->setStringValue(std::to_string(v), true);
                } else {
                    boolsetting->setBoolValueLocked(true);
                }
            } else if (lockable_vec_t *vecsetting = dynamic_cast<lockable_vec_t *>(setting)) {
                vecsetting->setFloatValueLocked(ParseVec(&i, argc, argv));
            } else if (lockable_vec3_t *vec3setting = dynamic_cast<lockable_vec3_t *>(setting)) {
                vec3_t temp;
                ParseVec3(temp, &i, argc, argv);
                vec3setting->setVec3ValueLocked(temp);
            } else {
                Error("Internal error");
            }
        } else {
            break;
        }
    }

    if (i != argc - 1) {
        PrintUsage();
        exit(1);
    }

    if (debugmode != debugmode_none) {
        write_litfile |= 1;
    }
    
#ifndef HAVE_EMBREE
    if (rtbackend == backend_embree) {
        rtbackend = backend_bsp;
    }
#endif
    
    logprint("Raytracing backend: ");
    switch (rtbackend) {
        case backend_bsp: logprint("BSP\n"); break;
        case backend_embree: logprint("Embree\n"); break;
    }
    
    if (numthreads > 1)
        logprint("running with %d threads\n", numthreads);

    if (write_litfile == ~0)
        logprint("generating lit2 output only.\n");
    else
    {
        if (write_litfile & 1)
            logprint(".lit colored light output requested on command line.\n");
        if (write_litfile & 2)
            logprint("BSPX colored light output requested on command line.\n");
        if (write_luxfile & 1)
            logprint(".lux light directions output requested on command line.\n");
        if (write_luxfile & 2)
            logprint("BSPX light directions output requested on command line.\n");
    }

    if (softsamples == -1) {
        switch (oversample) {
        case 2:
            softsamples = 1;
            break;
        case 4:
            softsamples = 2;
            break;
        default:
            softsamples = 0;
            break;
        }
    }

    start = I_FloatTime();

    strcpy(source, argv[i]);
    strcpy(mapfilename, argv[i]);
    
    // delete previous litfile
    if (!onlyents) {
        StripExtension(source);
        DefaultExtension(source, ".lit");
        remove(source);
    }

    {
        StripExtension(source);
        DefaultExtension(source, ".rad");
        if (strcmp(source, "lights.rad"))
            ParseLightsFile("lights.rad");    //generic/default name
        ParseLightsFile(source);            //map-specific file name
    }
    
    StripExtension(source);
    DefaultExtension(source, ".bsp");
    LoadBSPFile(source, &bspdata);

    loadversion = bspdata.version;
    ConvertBSPFormat(GENERIC_BSP, &bspdata);

    //mxd. Use 1.0 rangescale as a default to better match with qrad3/arghrad
    if (loadversion == Q2_BSPVERSION && !cfg.rangescale.isChanged())
    {
        const auto rs = new lockable_vec_t(cfg.rangescale.primaryName(), 1.0f, 0.0f, 100.0f);
        cfg.rangescale = *rs; // Gross hacks to avoid displaying this in OptionsSummary...
    }

    //mxd. Load or convert textures...
    SetQdirFromPath(GetBaseDirName(&bspdata), source);
    LoadPalette(&bspdata);
    LoadOrConvertTextures(bsp);

    LoadExtendedTexinfoFlags(source, bsp);
    LoadEntities(cfg, bsp);

    PrintOptionsSummary();
    
    FindModelInfo(bsp, lmscaleoverride);
    
    FindDebugFace(bsp);
    FindDebugVert(bsp);

    MakeTnodes(bsp);
    
    if (debugmode == debugmode_phong_obj) {
        StripExtension(source);
        DefaultExtension(source, ".obj");
        
        CalcualateVertexNormals(bsp);
        ExportObj(source, bsp);
        
        close_log();
        return 0;
    }
    
    SetupLights(cfg, bsp);
    
    //PrintLights();
    
    if (!onlyents)
    {
        if (loadversion != Q2_BSPVERSION && bsp->loadversion != BSPHLVERSION) //mxd. No lit for Quake 2
            CheckLitNeeded(cfg);
        SetupDirt(cfg);
        
        LightWorld(&bspdata, !!lmscaleoverride);
        
        /*invalidate any bspx lighting info early*/
        BSPX_AddLump(&bspdata, "RGBLIGHTING", NULL, 0);
        BSPX_AddLump(&bspdata, "LIGHTINGDIR", NULL, 0);

        if (write_litfile == ~0)
        {
            WriteLitFile(bsp, faces_sup, source, 2);
            return 0;   //run away before any files are written
        }
        else
        {
            /*fixme: add a new per-surface offset+lmscale lump for compat/versitility?*/
            if (write_litfile & 1)
                WriteLitFile(bsp, faces_sup, source, LIT_VERSION);
            if (write_litfile & 2)
                BSPX_AddLump(&bspdata, "RGBLIGHTING", lit_filebase, bsp->lightdatasize*3);
            if (write_luxfile & 1)
                WriteLuxFile(bsp, source, LIT_VERSION);
            if (write_luxfile & 2)
                BSPX_AddLump(&bspdata, "LIGHTINGDIR", lux_filebase, bsp->lightdatasize*3);
        }
    }

    /* -novanilla + internal lighting = no grey lightmap */
    if (scaledonly && (write_litfile & 2))
        bsp->lightdatasize = 0;

#if 0
    ExportObj(source, bsp);
#endif
    
    WriteEntitiesToString(cfg, bsp);
    /* Convert data format back if necessary */
    ConvertBSPFormat(loadversion, &bspdata);

    if (!litonly) {
        WriteBSPFile(source, &bspdata);
    }

    end = I_FloatTime();
    logprint("%5.3f seconds elapsed\n", end - start);
    logprint("\n");
    logprint("stats:\n");
    logprint("%f lights tested, %f hits per sample point\n",
             static_cast<double>(total_light_rays) / static_cast<double>(total_samplepoints),
             static_cast<double>(total_light_ray_hits) / static_cast<double>(total_samplepoints));
    logprint("%f surface lights tested, %f hits per sample point\n",
        static_cast<double>(total_surflight_rays) / static_cast<double>(total_samplepoints),
        static_cast<double>(total_surflight_ray_hits) / static_cast<double>(total_samplepoints)); //mxd
    logprint("%f bounce lights tested, %f hits per sample point\n",
             static_cast<double>(total_bounce_rays) / static_cast<double>(total_samplepoints),
             static_cast<double>(total_bounce_ray_hits) / static_cast<double>(total_samplepoints));
    logprint("%d empty lightmaps\n", static_cast<int>(fully_transparent_lightmaps));
    close_log();
    
    return 0;
}
