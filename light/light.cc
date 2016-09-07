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

#include <light/light.hh>
#include <light/entities.hh>
#include <light/ltface.hh>

#include <common/polylib.h>
#include <common/bsputils.h>

#ifdef HAVE_EMBREE
#include <xmmintrin.h>
#include <pmmintrin.h>
#endif

#include <memory>
#include <vector>
#include <map>
#include <unordered_map>
#include <set>
#include <algorithm>
#include <mutex>
#include <string>

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

byte *filebase;                 // start of lightmap data
static byte *file_p;            // start of free space after data
static byte *file_end;          // end of free space for lightmap data

byte *lit_filebase;             // start of litfile data
static byte *lit_file_p;        // start of free space after litfile data
static byte *lit_file_end;      // end of space for litfile data

byte *lux_buffer;               // luxfile allocation (misaligned)
byte *lux_filebase;             // start of luxfile data
static byte *lux_file_p;        // start of free space after luxfile data
static byte *lux_file_end;      // end of space for luxfile data

std::vector<modelinfo_t *> modelinfo;
std::vector<const modelinfo_t *> tracelist;
std::vector<const modelinfo_t *> selfshadowlist;

int oversample = 1;
int write_litfile = 0;  /* 0 for none, 1 for .lit, 2 for bspx, 3 for both */
int write_luxfile = 0;  /* 0 for none, 1 for .lux, 2 for bspx, 3 for both */
qboolean onlyents = false;
qboolean novisapprox = false;
bool nolights = false;
backend_t rtbackend = backend_embree;
debugmode_t debugmode = debugmode_none;

uint32_t *extended_texinfo_flags = NULL;

char mapfilename[1024];

struct ltface_ctx *ltface_ctxs;

int dump_facenum = -1;
bool dump_face;
vec3_t dump_face_point = {0,0,0};

int dump_vertnum = -1;
bool dump_vert;
vec3_t dump_vert_point = {0,0,0};

lockable_setting_t *FindSetting(std::string name) {
    settingsdict_t sd = cfg_static.settings();
    return sd.findSetting(name);
}

void SetGlobalSetting(std::string name, std::string value, bool cmdline) {
    settingsdict_t sd = cfg_static.settings();
    sd.setSetting(name, value, cmdline);
}

static void
PrintOptionsSummary(void)
{
    logprint("Options summary:\n");
    
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
 */
void
GetFileSpace(byte **lightdata, byte **colordata, byte **deluxdata, int size)
{
    ThreadLock();

    /* align to 4 byte boudaries */
    file_p = (byte *)(((uintptr_t)file_p + 3) & ~3);
    *lightdata = file_p;
    file_p += size;

    if (colordata) {
        /* align to 12 byte boundaries to match offets with 3 * lightdata */
        if ((uintptr_t)lit_file_p % 12)
            lit_file_p += 12 - ((uintptr_t)lit_file_p % 12);
        *colordata = lit_file_p;
        lit_file_p += size * 3;
    }

    if (deluxdata) {
        /* align to 12 byte boundaries to match offets with 3 * lightdata */
        if ((uintptr_t)lux_file_p % 12)
            lux_file_p += 12 - ((uintptr_t)lux_file_p % 12);
        *deluxdata = lux_file_p;
        lux_file_p += size * 3;
    }

    ThreadUnlock();

    if (file_p > file_end)
        Error("%s: overrun", __func__);

    if (lit_file_p > lit_file_end)
        Error("%s: overrun", __func__);
}

const modelinfo_t *ModelInfoForFace(const bsp2_t *bsp, int facenum)
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
    int facenum, i;
    const bsp2_t *bsp = (const bsp2_t *)arg;
    const modelinfo_t *face_modelinfo;
    struct ltface_ctx *ctx;

#ifdef HAVE_EMBREE
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#endif

    while (1) {
        facenum = GetThreadWork();
        if (facenum == -1)
            break;

        ctx = &ltface_ctxs[facenum];
        
        LightFaceInit(bsp, ctx);
        ctx->cfg = &cfg_static;
        
        /* Find the correct model offset */
        face_modelinfo = ModelInfoForFace(bsp, facenum);
        if (face_modelinfo == NULL) {
            // ericw -- silenced this warning becasue is causes spam when "skip" faces are used
            //logprint("warning: no model has face %d\n", facenum);
            continue;
        }

        if (!faces_sup)
            LightFace(bsp->dfaces + facenum, NULL, face_modelinfo, ctx);
        else if (scaledonly)
        {
            bsp->dfaces[facenum].lightofs = -1;
            bsp->dfaces[facenum].styles[0] = 255;
            LightFace(bsp->dfaces + facenum, faces_sup + facenum, face_modelinfo, ctx);
        }
        else if (faces_sup[facenum].lmscale == face_modelinfo->lightmapscale)
        {
            LightFace(bsp->dfaces + facenum, NULL, face_modelinfo, ctx);
            faces_sup[facenum].lightofs = bsp->dfaces[facenum].lightofs;
            for (i = 0; i < MAXLIGHTMAPS; i++)
                faces_sup[facenum].styles[i] = bsp->dfaces[facenum].styles[i];
        }
        else
        {
            LightFace(bsp->dfaces + facenum, NULL, face_modelinfo, ctx);
            LightFace(bsp->dfaces + facenum, faces_sup + facenum, face_modelinfo, ctx);
        }
        
        LightFaceShutdown(ctx);
    }

    return NULL;
}

static void
FindModelInfo(const bsp2_t *bsp, const char *lmscaleoverride)
{
    assert(modelinfo.size() == 0);
    assert(tracelist.size() == 0);
    assert(selfshadowlist.size() == 0);
    
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
    modelinfo_t *world = new modelinfo_t { &bsp->dmodels[0], lightmapscale };
    world->shadow.setFloatValue(1.0f); /* world always casts shadows */
    modelinfo.push_back(world);
    tracelist.push_back(world);
    
    for (int i = 1; i < bsp->nummodels; i++) {
        modelinfo_t *info = new modelinfo_t { &bsp->dmodels[i], lightmapscale };
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
        if (info->shadow.boolValue()) {
            tracelist.push_back(info);
        } else if (info->shadowself.boolValue()){
            selfshadowlist.push_back(info);
        }

        /* Set up the offset for rotate_* entities */
        if (EntDict_StringForKey(*entdict, "classname").find("rotate_") == 0) {
            EntDict_VectorForKey(*entdict, "origin", info->offset);
        } else {
            assert(info->offset[0] == 0);
            assert(info->offset[1] == 0);
            assert(info->offset[2] == 0);
        }
    }

    assert(modelinfo.size() == bsp->nummodels);
}

/* return 0 if either vector is zero-length */
static float
AngleBetweenVectors(const vec3_t d1, const vec3_t d2)
{
    float length_product = (VectorLength(d1)*VectorLength(d2));
    if (length_product == 0)
        return 0;
    float cosangle = DotProduct(d1, d2)/length_product;
    if (cosangle < -1) cosangle = -1;
    if (cosangle > 1) cosangle = 1;
    
    float angle = acos(cosangle);
    return angle;
}

/* returns the angle between vectors p2->p1 and p2->p3 */
static float
AngleBetweenPoints(const vec3_t p1, const vec3_t p2, const vec3_t p3)
{
    vec3_t d1, d2;
    VectorSubtract(p1, p2, d1);
    VectorSubtract(p3, p2, d2);
    float result = AngleBetweenVectors(d1, d2);
    return result;
}

class vec3_struct_t {
public:
    vec3_t v;
    vec3_struct_t() {
        VectorSet(v, 0, 0, 0);
    }
};

std::map<const bsp2_dface_t *, std::vector<vec3_struct_t>> vertex_normals;
std::set<int> interior_verts;
map<const bsp2_dface_t *, set<const bsp2_dface_t *>> smoothFaces;
map<int, vector<const bsp2_dface_t *>> vertsToFaces;

/* given a triangle, just adds the contribution from the triangle to the given vertexes normals, based upon angles at the verts.
 * v1, v2, v3 are global vertex indices */
static void
AddTriangleNormals(std::map<int, vec3_struct_t> &smoothed_normals, const vec_t *norm, const dvertex_t *verts, int v1, int v2, int v3)
{
    const vec_t *p1 = verts[v1].point;
    const vec_t *p2 = verts[v2].point;
    const vec_t *p3 = verts[v3].point;
    float weight;
    
    weight = AngleBetweenPoints(p2, p1, p3);
    VectorMA(smoothed_normals[v1].v, weight, norm, smoothed_normals[v1].v);

    weight = AngleBetweenPoints(p1, p2, p3);
    VectorMA(smoothed_normals[v2].v, weight, norm, smoothed_normals[v2].v);

    weight = AngleBetweenPoints(p1, p3, p2);
    VectorMA(smoothed_normals[v3].v, weight, norm, smoothed_normals[v3].v);
}

/* access the final phong-shaded vertex normal */
const vec_t *GetSurfaceVertexNormal(const bsp2_t *bsp, const bsp2_dface_t *f, const int vertindex)
{
    const auto &face_normals_vector = vertex_normals.at(f);
    return face_normals_vector.at(vertindex).v;
}

static bool
FacesOnSamePlane(const std::vector<const bsp2_dface_t *> &faces)
{
    if (faces.empty()) {
        return false;
    }
    const int32_t planenum = faces.at(0)->planenum;
    for (auto face : faces) {
        if (face->planenum != planenum) {
            return false;
        }
    }
    return true;
}

const bsp2_dface_t *
Face_EdgeIndexSmoothed(const bsp2_t *bsp, const bsp2_dface_t *f, const int edgeindex) 
{
    if (smoothFaces.find(f) == smoothFaces.end()) {
        return nullptr;
    }
    
    int v0 = Face_VertexAtIndex(bsp, f, edgeindex);
    int v1 = Face_VertexAtIndex(bsp, f, (edgeindex + 1) % f->numedges);
    
    const auto &v0_faces = vertsToFaces.at(v0);
    const auto &v1_faces = vertsToFaces.at(v1);
    
    // find a face f2 that has both verts v0 and v1
    for (auto f2 : v0_faces) {
        if (f2 == f)
            continue;
        if (find(v1_faces.begin(), v1_faces.end(), f2) != v1_faces.end()) {
            const auto &f_smoothfaces = smoothFaces.at(f);
            bool smoothed = (f_smoothfaces.find(f2) != f_smoothfaces.end());
            return smoothed ? f2 : nullptr;
        }
    }
    return nullptr;
}

static void
CalcualateVertexNormals(const bsp2_t *bsp)
{
    // clear in case we are run twice
    vertex_normals.clear();
    interior_verts.clear();
    smoothFaces.clear();
    vertsToFaces.clear();
    
    // read _phong and _phong_angle from entities for compatiblity with other qbsp's, at the expense of no
    // support on func_detail/func_group
    for (int i=0; i<bsp->nummodels; i++) {
        const modelinfo_t *info = modelinfo.at(i);
        const uint8_t phongangle_byte = (uint8_t) qmax(0, qmin(255, (int)rint(info->getResolvedPhongAngle())));

        if (!phongangle_byte)
            continue;
        
        for (int j=info->model->firstface; j < info->model->firstface + info->model->numfaces; j++) {
            const bsp2_dface_t *f = &bsp->dfaces[j];
            
            extended_texinfo_flags[f->texinfo] &= ~(TEX_PHONG_ANGLE_MASK);
            extended_texinfo_flags[f->texinfo] |= (phongangle_byte << TEX_PHONG_ANGLE_SHIFT);
        }
    }
    
    // build "vert index -> faces" map
    for (int i = 0; i < bsp->numfaces; i++) {
        const bsp2_dface_t *f = &bsp->dfaces[i];
        for (int j = 0; j < f->numedges; j++) {
            const int v = Face_VertexAtIndex(bsp, f, j);
            vertsToFaces[v].push_back(f);
        }
    }
    
    // track "interior" verts, these are in the middle of a face, and mess up normal interpolation
    for (int i=0; i<bsp->numvertexes; i++) {
        auto &faces = vertsToFaces[i];
        if (faces.size() > 1 && FacesOnSamePlane(faces)) {
            interior_verts.insert(i);
        }
    }
    //printf("CalcualateVertexNormals: %d interior verts\n", (int)interior_verts.size());
    
    // build the "face -> faces to smooth with" map
    for (int i = 0; i < bsp->numfaces; i++) {
        bsp2_dface_t *f = &bsp->dfaces[i];
        
        vec3_t f_norm;
        Face_Normal(bsp, f, f_norm);
        
        // any face normal within this many degrees can be smoothed with this face
        const int f_smoothangle = (extended_texinfo_flags[f->texinfo] & TEX_PHONG_ANGLE_MASK) >> TEX_PHONG_ANGLE_SHIFT;
        if (!f_smoothangle)
            continue;
        
        for (int j = 0; j < f->numedges; j++) {
            const int v = Face_VertexAtIndex(bsp, f, j);
            // walk over all faces incident to f (we will walk over neighbours multiple times, doesn't matter)
            for (const bsp2_dface_t *f2 : vertsToFaces[v]) {
                if (f2 == f)
                    continue;
                
                const int f2_smoothangle = (extended_texinfo_flags[f2->texinfo] & TEX_PHONG_ANGLE_MASK) >> TEX_PHONG_ANGLE_SHIFT;
                if (!f2_smoothangle)
                    continue;
                
                vec3_t f2_norm;
                Face_Normal(bsp, f2, f2_norm);

                const vec_t cosangle = DotProduct(f_norm, f2_norm);
                const vec_t cosmaxangle = cos(DEG2RAD(qmin(f_smoothangle, f2_smoothangle)));
                
                // check the angle between the face normals
                if (cosangle >= cosmaxangle) {
                    smoothFaces[f].insert(f2);
                }
            }
        }
    }
    
    // finally do the smoothing for each face
    for (int i = 0; i < bsp->numfaces; i++)
    {
        const bsp2_dface_t *f = &bsp->dfaces[i];
        if (f->numedges < 3) {
            logprint("CalcualateVertexNormals: face %d is degenerate with %d edges\n", i, f->numedges);
            continue;
        }
        
        const auto &neighboursToSmooth = smoothFaces[f];
        vec3_t f_norm;
        
        // get the face normal
        Face_Normal(bsp, f, f_norm);
        
        // gather up f and neighboursToSmooth
        std::vector<const bsp2_dface_t *> fPlusNeighbours;
        fPlusNeighbours.push_back(f);
        for (auto neighbour : neighboursToSmooth) {
            fPlusNeighbours.push_back(neighbour);
        }
        
        // global vertex index -> smoothed normal
        std::map<int, vec3_struct_t> smoothedNormals;

        // walk fPlusNeighbours
        for (auto f2 : fPlusNeighbours) {
            vec3_t f2_norm;
            Face_Normal(bsp, f2, f2_norm);
            
            /* now just walk around the surface as a triangle fan */
            int v1, v2, v3;
            v1 = Face_VertexAtIndex(bsp, f2, 0);
            v2 = Face_VertexAtIndex(bsp, f2, 1);
            for (int j = 2; j < f2->numedges; j++)
            {
                v3 = Face_VertexAtIndex(bsp, f2, j);
                AddTriangleNormals(smoothedNormals, f2_norm, bsp->dvertexes, v1, v2, v3);
                v2 = v3;
            }
        }
        
        // normalize vertex normals
        for (auto &pair : smoothedNormals) {
            const int vertIndex = pair.first;
            vec_t *vertNormal = pair.second.v;
            if (0 == VectorNormalize(vertNormal)) {
                // this happens when there are colinear vertices, which give zero-area triangles,
                // so there is no contribution to the normal of the triangle in the middle of the
                // line. Not really an error, just set it to use the face normal.
#if 0
                logprint("Failed to calculate normal for vertex %d at (%f %f %f)\n",
                         vertIndex,
                         bsp->dvertexes[vertIndex].point[0],
                         bsp->dvertexes[vertIndex].point[1],
                         bsp->dvertexes[vertIndex].point[2]);
#endif
                VectorCopy(f_norm, vertNormal);
            }
        }
        
        // sanity check
        if (!neighboursToSmooth.size()) {
            for (auto vertIndexNormalPair : smoothedNormals) {
                assert(VectorCompare(vertIndexNormalPair.second.v, f_norm));
            }
        }
        
        // now, record all of the smoothed normals that are actually part of `f`
        for (int j=0; j<f->numedges; j++) {
            int v = Face_VertexAtIndex(bsp, f, j);
            assert(smoothedNormals.find(v) != smoothedNormals.end());
            
            vertex_normals[f].push_back(smoothedNormals[v]);
        }
    }
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
    
    bsp2_t *const bsp = &bspdata->data.bsp2;
    const unsigned char *lmshift_lump;
    int i, j;
    if (bsp->dlightdata)
        free(bsp->dlightdata);
    if (lux_buffer)
        free(lux_buffer);

    /* FIXME - remove this limit */
    bsp->lightdatasize = MAX_MAP_LIGHTING;
    bsp->dlightdata = (byte *)malloc(bsp->lightdatasize + 16); /* for alignment */
    if (!bsp->dlightdata)
        Error("%s: allocation of %i bytes failed.",
              __func__, bsp->lightdatasize);
    memset(bsp->dlightdata, 0, bsp->lightdatasize + 16);
    bsp->lightdatasize /= 4;

    /* align filebase to a 4 byte boundary */
    filebase = file_p = (byte *)(((uintptr_t)bsp->dlightdata + 3) & ~3);
    file_end = filebase + bsp->lightdatasize;

    /* litfile data stored in dlightdata, after the white light */
    lit_filebase = file_end + 12 - ((uintptr_t)file_end % 12);
    lit_file_p = lit_filebase;
    lit_file_end = lit_filebase + 3 * (MAX_MAP_LIGHTING / 4);

    /* lux data stored in a separate buffer */
    lux_buffer = (byte *)malloc(bsp->lightdatasize*3);
    lux_filebase = lux_buffer + 12 - ((uintptr_t)lux_buffer % 12);
    lux_file_p = lux_filebase;
    lux_file_end = lux_filebase + 3 * (MAX_MAP_LIGHTING / 4);


    if (forcedscale)
        BSPX_AddLump(bspdata, "LMSHIFT", NULL, 0);

    lmshift_lump = (const unsigned char *)BSPX_GetLump(bspdata, "LMSHIFT", NULL);
    if (!lmshift_lump && write_litfile != ~0)
        faces_sup = NULL; //no scales, no lit2
    else
    {   //we have scales or lit2 output. yay...
        faces_sup = (facesup_t *)malloc(sizeof(*faces_sup) * bsp->numfaces);
        memset(faces_sup, 0, sizeof(*faces_sup) * bsp->numfaces);
        if (lmshift_lump)
        {
            for (i = 0; i < bsp->numfaces; i++)
                faces_sup[i].lmscale = 1<<lmshift_lump[i];
        }
        else
        {
            for (i = 0; i < bsp->numfaces; i++)
                faces_sup[i].lmscale = modelinfo.at(0)->lightmapscale;
        }
    }

    CalcualateVertexNormals(bsp);

    /* ericw -- alloc memory */
    ltface_ctxs = (struct ltface_ctx *)calloc(bsp->numfaces, sizeof(struct ltface_ctx));
    
    RunThreadsOn(0, bsp->numfaces, LightThread, bsp);

    logprint("Lighting Completed.\n\n");
    bsp->lightdatasize = file_p - filebase;
    logprint("lightdatasize: %i\n", bsp->lightdatasize);


    if (faces_sup)
    {
        uint8_t *styles = (uint8_t *)malloc(sizeof(*styles)*4*bsp->numfaces);
        int32_t *offsets = (int32_t *)malloc(sizeof(*offsets)*bsp->numfaces);
        for (i = 0; i < bsp->numfaces; i++)
        {
            offsets[i] = faces_sup[i].lightofs;
            for (j = 0; j < MAXLIGHTMAPS; j++)
                styles[i*4+j] = faces_sup[i].styles[j];
        }
        BSPX_AddLump(bspdata, "LMSTYLE", styles, sizeof(*styles)*4*bsp->numfaces);
        BSPX_AddLump(bspdata, "LMOFFSET", offsets, sizeof(*offsets)*bsp->numfaces);
    }
    else
    { //kill this stuff if its somehow found.
        BSPX_AddLump(bspdata, "LMSTYLE", NULL, 0);
        BSPX_AddLump(bspdata, "LMOFFSET", NULL, 0);
    }
}

static void
LoadExtendedTexinfoFlags(const char *sourcefilename, const bsp2_t *bsp)
{
    char filename[1024];
    
    // always create the zero'ed array
    extended_texinfo_flags = (uint32_t *) calloc(bsp->numtexinfo, sizeof(uint32_t));
    
    strcpy(filename, sourcefilename);
    StripExtension(filename);
    DefaultExtension(filename, ".texinfo");

    FILE *texinfofile = fopen(filename, "rt");
    if (!texinfofile)
        return;
    
    logprint("Loaded extended texinfo flags from %s\n", filename);
    
    for (int i = 0; i < bsp->numtexinfo; i++) {
        int cnt = fscanf(texinfofile, "%u\n", &extended_texinfo_flags[i]);
        if (cnt != 1) {
            logprint("WARNING: Extended texinfo flags in %s does not match bsp, ignoring\n", filename);
            fclose(texinfofile);
            memset(extended_texinfo_flags, 0, bsp->numtexinfo * sizeof(uint32_t));
            return;
        }
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

// radiosity

mutex radlights_lock;
map<string, vec3_struct_t> texturecolors;
std::vector<bouncelight_t> radlights;

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
        vec3_struct_t texcolor = texturecolors.at(facename);
        VectorCopy(texcolor.v, color);
    } else {
        VectorSet(color, 127, 127, 127);
    }
}

static void
AddBounceLight(const vec3_t pos, const vec3_t color, const vec3_t surfnormal, vec_t area, const bsp2_t *bsp);

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
        
        // average them
        vec3_t sum = {0,0,0};
        if (patches.size()) {
            for (const auto &patch : patches) {
                VectorAdd(sum, patch->directlight, sum);
//              printf("  %f %f %f\n", patch->directlight[0], patch->directlight[1], patch->directlight[2]);
            }
            VectorScale(sum, 1.0/patches.size(), sum);
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
        
        AddBounceLight(facemidpoint, emitcolor, faceplane.normal, facearea, bsp);
    }
    
    return NULL;
}

static void
AddBounceLight(const vec3_t pos, const vec3_t color, const vec3_t surfnormal, vec_t area, const bsp2_t *bsp)
{
    bouncelight_t l = {0};
    VectorCopy(pos, l.pos);
    VectorCopy(color, l.color);
    VectorCopy(surfnormal, l.surfnormal);
    l.area = area;
    l.leaf = Light_PointInLeaf(bsp, pos);
    
    if (!novisapprox) {
        EstimateVisibleBoundsAtPoint(pos, l.mins, l.maxs);
    }
    
    unique_lock<mutex> lck { radlights_lock };
    radlights.push_back(l);
}

const std::vector<bouncelight_t> &BounceLights()
{
    return radlights;
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
            
            vec3_t samplecolor = { (float)thepalette[3*i], (float)thepalette[3*i + 1], (float)thepalette[3*i + 2] };
            VectorAdd(color, samplecolor, color);
        }
    }
    VectorScale(color, 1.0 / (miptex->width * miptex->height), color);
}

static void
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
        vec3_struct_t color;
        Texture_AvgColor(bsp, miptex, color.v);
        
//        printf("%s has color %f %f %f\n", name.c_str(), color.v[0], color.v[1], color.v[2]);
        texturecolors[name] = color;
    }
}

static void
MakeBounceLights (const globalconfig_t &cfg, const bsp2_t *bsp)
{
    logprint("--- MakeBounceLights ---\n");
    
    const dmodel_t *model = &bsp->dmodels[0];
    
    make_bounce_lights_args_t args;
    args.bsp = bsp;
    args.cfg = &cfg;
    
    RunThreadsOn(model->firstface, model->firstface + model->numfaces, MakeBounceLightsThread, (void *)&args);
}

// end radiosity

//obj

static FILE *
InitObjFile(const char *filename)
{
    FILE *objfile;
    char objfilename[1024];
    strcpy(objfilename, filename);
    StripExtension(objfilename);
    DefaultExtension(objfilename, ".obj");
    
    objfile = fopen(objfilename, "wt");
    if (!objfile)
        Error("Failed to open %s: %s", objfilename, strerror(errno));
    
    return objfile;
}

static void
ExportObjFace(FILE *f, const bsp2_t *bsp, const bsp2_dface_t *face, int *vertcount)
{
    // export the vertices and uvs
    for (int i=0; i<face->numedges; i++)
    {
        int vertnum = Face_VertexAtIndex(bsp, face, i);
        const vec_t *normal = GetSurfaceVertexNormal(bsp, face, i);
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
ExportObj(const char *filename, const bsp2_t *bsp)
{
    FILE *objfile = InitObjFile(filename);
    int vertcount = 0;
    
    const int start = bsp->dmodels[0].firstface;
    const int end = bsp->dmodels[0].firstface + bsp->dmodels[0].numfaces;
    
    for (int i=start; i<end; i++) {
        ExportObjFace(objfile, bsp, &bsp->dfaces[i], &vertcount);
    }
    
    fclose(objfile);
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
Face_NearestCentroid(const bsp2_t *bsp, const vec3_t point)
{
    const bsp2_dface_t *nearest_face = NULL;
    vec_t nearest_dist = VECT_MAX;
    
    for (int i=0; i<bsp->numfaces; i++) {
        const bsp2_dface_t *f = &bsp->dfaces[i];
        
        vec3_t fc;
        FaceCentroid(f, bsp, fc);
        
        vec3_t distvec;
        VectorSubtract(fc, point, distvec);
        vec_t dist = VectorLength(distvec);
        
        if (dist < nearest_dist) {
            nearest_dist = dist;
            nearest_face = f;
        }
    }
    
    return nearest_face;
}

static void
FindDebugFace(const bsp2_t *bsp)
{
    if (!dump_face)
        return;
    
    const bsp2_dface_t *f = Face_NearestCentroid(bsp, dump_face_point);
    if (f == NULL)
        Error("FindDebugFace: f == NULL\n");

    const int facenum = f - bsp->dfaces;
    
    dump_facenum = facenum;
    
    const modelinfo_t *mi = ModelInfoForFace(bsp, facenum);
    int modelnum = (mi->model - bsp->dmodels);
    
    const char *texname = Face_TextureName(bsp, f);
    
    logprint("FindDebugFace: dumping face %d (texture '%s' model %d)\n", facenum, texname, modelnum);
}

// returns the vert nearest the given point
static int
Vertex_NearestPoint(const bsp2_t *bsp, const vec3_t point)
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
FindDebugVert(const bsp2_t *bsp)
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
        if (!VectorCompare(white, *light.color.vec3Value())) {
            SetLitNeeded();
            return;
        }
    }
    
    // check global settings
    if (cfg.bouncecolorscale.floatValue() != 0 ||
        !VectorCompare(*cfg.minlight_color.vec3Value(), white) ||
        !VectorCompare(*cfg.sunlight_color.vec3Value(), white) ||
        !VectorCompare(*cfg.sun2_color.vec3Value(), white) ||
        !VectorCompare(*cfg.sunlight2_color.vec3Value(), white) ||
        !VectorCompare(*cfg.sunlight3_color.vec3Value(), white)) {
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
"* = also a worldspawn key with underscore prefix; -light becomes \"_light\"\n"
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
"Global options:\n"
"* -light n            sets global minlight level n\n"
"  -addmin             additive minlight\n"
"* -anglescale n       set weight of cosine term, default 0.5, 1=realistic\n"
"  -anglesense n       same as -anglescale n\n"
"* -dist n             scale fade distance of all lights, default 1\n"
"* -range n            scale brightness of all lights, default 0.5\n"
"  -phong n            0=disable phong shading\n"
"\n"
"Dirtmapping (ambient occlusion) options:\n"
"* -dirt [n]           enable global AO, 0=disable even if set in worldspawn\n"
"* -dirtmode n         0=ordered (default), 1=random AO\n"
"* -dirtdepth n        distance for occlusion test, default 128\n"
"* -dirtscale n        scale factor for AO, default 1, higher values are darker\n"
"* -dirtgain n         exponent for AO, default 1, lower values are darker\n"
"* -dirtangle n        maximum angle for AO rays, default 88\n"
"\n"
"Bounce options:\n"
"* -bounce [n]         enables 1 bounce, 0=disable even if set in worldspawn\n"
"* -bouncescale n      scales brightness of bounce lighting, default 1\n"
"* -bouncecolorscale n how much to use texture colors, 0=none (default), 1=full\n"
"\n"
"Postprocessing options:\n"
"* -gamma n            gamma correct final lightmap, default 1.0\n"
"  -soft [n]           blurs the lightmap, n=blur radius in samples\n"
"\n"
"Debug modes:\n"
"  -dirtdebug          only save the AO values to the lightmap\n"
"  -phongdebug         only save the normals to the lightmap\n"
"  -bouncedebug        only save bounced lighting to the lightmap\n"
"  -surflight_dump     dump surface lights to a .map file\n"
"  -novis              disable vis acceleration\n"
"\n"
"Experimental options:\n"
"  -lit2               write .lit2 file\n"
"* -lmscale n          change lightmap scale, vanilla engines only allow 16\n"
"  -lux                write .lux file\n"
"  -bspxlit            writes rgb data into the bsp itself\n"
"  -bspx               writes both rgb and directions data into the bsp itself\n"
"  -novanilla          implies -bspxlit. don't write vanilla lighting\n");
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
    bsp2_t *const bsp = &bspdata.data.bsp2;
    int32_t loadversion;
    int i;
    double start;
    double end;
    char source[1024];
    const char *lmscaleoverride = NULL;
    
    init_log("light.log");
    logprint("---- light / TyrUtils " stringify(TYRUTILS_VERSION) " ----\n");

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
    
    StripExtension(source);
    DefaultExtension(source, ".bsp");
    LoadBSPFile(source, &bspdata);

    loadversion = bspdata.version;
    if (bspdata.version != BSP2VERSION)
        ConvertBSPFormat(BSP2VERSION, &bspdata);

    LoadExtendedTexinfoFlags(source, bsp);
    LoadEntities(cfg, bsp);

    PrintOptionsSummary();
    
    FindModelInfo(bsp, lmscaleoverride);
    
    FindDebugFace(bsp);
    FindDebugVert(bsp);

    MakeTnodes(bsp);
    
    SetupLights(cfg, bsp);
    
    //PrintLights();
    
    if (!onlyents)
    {
        CheckLitNeeded(cfg);
        SetupDirt(cfg);
        
        if (cfg.bounce.boolValue()) {
            MakeTextureColors(bsp);
            MakeBounceLights(cfg, bsp);
        }
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
    
    WriteEntitiesToString(bsp);
    /* Convert data format back if necessary */
    if (loadversion != BSP2VERSION)
        ConvertBSPFormat(loadversion, &bspdata);
    WriteBSPFile(source, &bspdata);
    end = I_FloatTime();
    logprint("%5.3f seconds elapsed\n", end - start);
    logprint("\n");
    logprint("stats:\n");
    logprint("%f lights tested, %f hits per sample point\n",
             static_cast<double>(total_light_rays) / static_cast<double>(total_samplepoints),
             static_cast<double>(total_light_ray_hits) / static_cast<double>(total_samplepoints));
    logprint("%f bounce lights tested, %f hits per sample point\n",
             static_cast<double>(total_bounce_rays) / static_cast<double>(total_samplepoints),
             static_cast<double>(total_bounce_ray_hits) / static_cast<double>(total_samplepoints));
    
    close_log();
    
    return 0;
}
