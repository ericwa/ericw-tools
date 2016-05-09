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

#include <stdint.h>
#include <assert.h>
#include <stdio.h>

#include <light/light.h>
#include <light/entities.h>

#include <common/polylib.h>

#include <vector>
#include <map>
#include <unordered_map>
#include <set>
#include <algorithm>
#include <mutex>
#include <string>

using namespace std;

float scaledist = 1.0;
float rangescale = 0.5;
float global_anglescale = 0.5;
float fadegate = EQUAL_EPSILON;
int softsamples = 0;
float lightmapgamma = 1.0;
const vec3_t vec3_white = { 255, 255, 255 };
float surflight_subdivide = 128.0f;
int sunsamples = 64;
qboolean scaledonly = false;

qboolean addminlight = false;
lightsample_t minlight = { 0, { 255, 255, 255 } };
sun_t *suns = NULL;

/* dirt */
qboolean dirty = false;
qboolean dirtDebug = false;
int dirtMode = 0;
float dirtDepth = 128.0f;
float dirtScale = 1.0f;
float dirtGain = 1.0f;
float dirtAngle = 88.0f;

qboolean globalDirt = false;
qboolean minlightDirt = false;

qboolean dirtSetOnCmdline = false;
qboolean dirtModeSetOnCmdline = false;
qboolean dirtDepthSetOnCmdline = false;
qboolean dirtScaleSetOnCmdline = false;
qboolean dirtGainSetOnCmdline = false;
qboolean dirtAngleSetOnCmdline = false;

/* bounce */
qboolean bounce = false;
qboolean bouncedebug = false;
vec_t bouncescale = 1.0f;

qboolean testFenceTextures = false;
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

static modelinfo_t *modelinfo;
const modelinfo_t *const *tracelist;
const modelinfo_t *const *selfshadowlist;

int oversample = 1;
int write_litfile = 0;  /* 0 for none, 1 for .lit, 2 for bspx, 3 for both */
int write_luxfile = 0;  /* 0 for none, 1 for .lux, 2 for bspx, 3 for both */
qboolean onlyents = false;
qboolean phongDebug = false;
qboolean parse_escape_sequences = true;
qboolean novis = false; /* if true, don't use vis data */

uint32_t *extended_texinfo_flags = NULL;

char mapfilename[1024];

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
    return &modelinfo[i];
}

static void *
LightThread(void *arg)
{
    int facenum, i;
    const bsp2_t *bsp = (const bsp2_t *)arg;
    const modelinfo_t *face_modelinfo;
    struct ltface_ctx *ctx;

    while (1) {
        facenum = GetThreadWork();
        if (facenum == -1)
            break;

        ctx = LightFaceInit(bsp);
        
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
    int i, shadow, numshadowmodels, numselfshadowmodels;
    entity_t *entity;
    char modelname[20];
    const char *attribute;
    const modelinfo_t **shadowmodels;
    const modelinfo_t **selfshadowmodels;
    modelinfo_t *info;
    float lightmapscale;

    shadowmodels = (const modelinfo_t **)malloc(sizeof(modelinfo_t *) * (bsp->nummodels + 1));
    memset(shadowmodels, 0, sizeof(modelinfo_t *) * (bsp->nummodels + 1));

    selfshadowmodels = (const modelinfo_t **)malloc(sizeof(modelinfo_t *) * (bsp->nummodels + 1));
    memset(selfshadowmodels, 0, sizeof(modelinfo_t *) * (bsp->nummodels + 1));
    
    /* The world always casts shadows */
    shadowmodels[0] = &modelinfo[0];
    numshadowmodels = 1;
    numselfshadowmodels = 0;

    if (!bsp->nummodels) {
        Error("Corrupt .BSP: bsp->nummodels is 0!");
    }
    
    memset(modelinfo, 0, sizeof(*modelinfo) * bsp->nummodels);
    modelinfo[0].model = &bsp->dmodels[0];

    if (lmscaleoverride)
        SetWorldKeyValue("_lightmap_scale", lmscaleoverride);

    lightmapscale = atoi(WorldValueForKey("_lightmap_scale"));
    if (!lightmapscale)
        lightmapscale = 16; /* the default */
    if (lightmapscale <= 0)
        Error("lightmap scale is 0 or negative\n");
    if (lmscaleoverride || lightmapscale != 16)
        logprint("Forcing lightmap scale of %gqu\n", lightmapscale);
    /*I'm going to do this check in the hopes that there's a benefit to cheaper scaling in engines (especially software ones that might be able to just do some mip hacks). This tool doesn't really care.*/
    for (i = 1; i < lightmapscale;)
        i++;
    if (i != lightmapscale)
        logprint("WARNING: lightmap scale is not a power of 2\n");
    modelinfo[0].lightmapscale = lightmapscale;

    for (i = 1, info = modelinfo + 1; i < bsp->nummodels; i++, info++) {
        info->model = &bsp->dmodels[i];
        info->lightmapscale = lightmapscale;

        /* Find the entity for the model */
        snprintf(modelname, sizeof(modelname), "*%d", i);
        entity = FindEntityWithKeyPair("model", modelname);
        if (!entity)
            Error("%s: Couldn't find entity for model %s.\n", __func__,
                  modelname);

        /* Check if this model will cast shadows (shadow => shadowself) */
        shadow = atoi(ValueForKey(entity, "_shadow"));
        if (shadow) {
            shadowmodels[numshadowmodels++] = info;
        } else {
            shadow = atoi(ValueForKey(entity, "_shadowself"));
            if (shadow) {
                info->shadowself = true;
                selfshadowmodels[numselfshadowmodels++] = info;
            }
        }

        /* Set up the offset for rotate_* entities */
        attribute = ValueForKey(entity, "classname");
        if (!strncmp(attribute, "rotate_", 7))
            GetVectorForKey(entity, "origin", info->offset);

        /* Grab the bmodel minlight values, if any */
        attribute = ValueForKey(entity, "_minlight");
        if (attribute[0])
            info->minlight.light = atoi(attribute);
        const char *minlight_exclude = ValueForKey(entity, "_minlight_exclude");
        if (minlight_exclude[0] != '\0') {
            strncpy(info->minlight_exclude, minlight_exclude, 15);
            info->minlight_exclude[15] = '\0';
        }
        GetVectorForKey(entity, "_mincolor", info->minlight.color);
        normalize_color_format(info->minlight.color);
        if (!VectorCompare(info->minlight.color, vec3_origin)) {
            if (!write_litfile)
                write_litfile = scaledonly?2:1;
        } else {
            VectorCopy(vec3_white, info->minlight.color);
        }

        /* Check for disabled dirtmapping on this bmodel */
        if (atoi(ValueForKey(entity, "_dirt")) == -1) {
            info->nodirt = true;
        }
        
        /* Check for phong shading */
        // handle "_phong" and "_phong_angle"
        info->phongangle = atof(ValueForKey(entity, "_phong_angle"));
        const int phong = atoi(ValueForKey(entity, "_phong"));
        
        if (phong && (info->phongangle == 0.0)) {
            info->phongangle = 89.0; // default _phong_angle
        }
    }

    tracelist = shadowmodels;
    selfshadowlist = selfshadowmodels;
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
/* small helper that just retrieves the correct vertex from face->surfedge->edge lookups */
static int GetSurfaceVertex(const bsp2_t *bsp, const bsp2_dface_t *f, int v)
{
    int edge = f->firstedge + v;
    edge = bsp->dsurfedges[edge];
    if (edge < 0)
        return bsp->dedges[-edge].v[1];
    return bsp->dedges[edge].v[0];
}

void
Face_Normal(const bsp2_t *bsp, const bsp2_dface_t *f, vec3_t norm)
{
    if (f->side)
        VectorSubtract(vec3_origin, bsp->dplanes[f->planenum].normal, norm);
    else
        VectorCopy(bsp->dplanes[f->planenum].normal, norm);
}

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

static void
Vertex_GetPos(const bsp2_t *bsp, int num, vec3_t out)
{
    assert(num >= 0 && num < bsp->numvertexes);
    const dvertex_t *v = &bsp->dvertexes[num];
    
    for (int i=0; i<3; i++)
        out[i] = v->point[i];
}

plane_t
Face_Plane(const bsp2_t *bsp, const bsp2_dface_t *f)
{
    const int vertnum = GetSurfaceVertex(bsp, f, 0);
    vec3_t vertpos;
    Vertex_GetPos(bsp, vertnum, vertpos);
    
    plane_t res;
    Face_Normal(bsp, f, res.normal);
    res.dist = DotProduct(vertpos, res.normal);
    return res;
}

const bsp2_dface_t *
Face_EdgeIndexSmoothed(const bsp2_t *bsp, const bsp2_dface_t *f, const int edgeindex) 
{
    if (smoothFaces.find(f) == smoothFaces.end()) {
        return nullptr;
    }
    
    int v0 = GetSurfaceVertex(bsp, f, edgeindex);
    int v1 = GetSurfaceVertex(bsp, f, (edgeindex + 1) % f->numedges);
    
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
        const modelinfo_t *info = &modelinfo[i];
        const uint8_t phongangle_byte = (uint8_t) qmax(0, qmin(255, (int)rint(info->phongangle)));

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
            const int v = GetSurfaceVertex(bsp, f, j);
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
    printf("CalcualateVertexNormals: %d interior verts\n", (int)interior_verts.size());
    
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
            const int v = GetSurfaceVertex(bsp, f, j);
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
            v1 = GetSurfaceVertex(bsp, f2, 0);
            v2 = GetSurfaceVertex(bsp, f2, 1);
            for (int j = 2; j < f2->numedges; j++)
            {
                v3 = GetSurfaceVertex(bsp, f2, j);
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
            int v = GetSurfaceVertex(bsp, f, j);
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
                faces_sup[i].lmscale = modelinfo[0].lightmapscale;
        }
    }

    CalcualateVertexNormals(bsp);

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

map<string, vec3_struct_t> texturecolors;
std::vector<bouncelight_t> radlights;

// for access from C
const bouncelight_t *bouncelights;
int numbouncelights;

class patch_t {
public:
    winding_t *w;
    vec3_t center;
    vec3_t samplepoint; // 1 unit above center
    plane_t plane;
    vec3_t directlight;
    std::vector<plane_t> edgeplanes;
    
    vec3_t indirectlight;
    
    bool pointInPatch(const vec3_t point) {
        for (const auto &edgeplane : edgeplanes)
        {
            /* faces toward the center of the face */
            vec_t dist = DotProduct(point, edgeplane.normal) - edgeplane.dist;
            if (dist < 0)
                return false;
        }
        return true;
    }
};

void
GetDirectLighting(const vec3_t origin, const vec3_t normal, vec3_t colorout)
{
    const entity_t *entity;
    entity_t **lighte;
    
    VectorSet(colorout, 0, 0, 0);
    
    for (lighte = lights; (entity = *lighte); lighte++)
    {
        if (!TestLight(entity->origin, origin, NULL))
            continue;
        
        vec3_t originLightDir;
        VectorSubtract(entity->origin, origin, originLightDir);
        vec_t dist = VectorNormalize(originLightDir);
            
        vec_t cosangle = DotProduct(originLightDir, normal);
        if (cosangle < 0)
            continue;
        
        vec_t lightval = GetLightValue(&entity->light, entity, dist);
        VectorMA(colorout, lightval * cosangle / 255.0f, entity->light.color, colorout);
    }
    
    for ( sun_t *sun = suns; sun; sun = sun->next )
    {
        if (!TestSky(origin, sun->sunvec, NULL))
            continue;
        VectorMA(colorout, sun->sunlight.light / 255.0f, sun->sunlight.color, colorout);
    }
}

std::vector<patch_t *> triangleIndexToPatch;
std::unordered_map<int, std::vector<patch_t *>> facenumToPatches;
mutex facenumToPatches_mutex;

/*
=============
WindingFromFace
From q2 tools
=============
*/
winding_t *WindingFromFace (const bsp2_t *bsp, const bsp2_dface_t *f)
{
    int			i;
    int			se;
    dvertex_t	*dv;
    int			v;
    winding_t	*w;
    
    w = AllocWinding (f->numedges);
    w->numpoints = f->numedges;
    
    for (i=0 ; i<f->numedges ; i++)
    {
        se = bsp->dsurfedges[f->firstedge + i];
        if (se < 0)
            v = bsp->dedges[-se].v[1];
        else
            v = bsp->dedges[se].v[0];
        
        dv = &bsp->dvertexes[v];
        for (int j=0; j<3; j++) {
            w->p[i][j] = dv->point[j];
        }
    }
    
    RemoveColinearPoints (w);
    
    return w;
}


void SavePatch (const bsp2_t *bsp, const bsp2_dface_t *sourceface, winding_t *w)
{
    int i = sourceface - bsp->dfaces;
    
    patch_t *p = new patch_t;
    p->w = w;
    
    // cache some stuff
    WindingCenter(p->w, p->center);
    WindingPlane(p->w, p->plane.normal, &p->plane.dist);
    
    // HACK: flip the plane
    p->plane.dist = -p->plane.dist;
    VectorScale(p->plane.normal, -1, p->plane.normal);
    
    VectorMA(p->center, 1, p->plane.normal, p->samplepoint);
    
    // calculate direct light
    if (bsp->texinfo[sourceface->texinfo].flags & TEX_SPECIAL) {
        VectorSet(p->directlight, 0, 0, 0);
    } else {
        GetDirectLighting(p->center, p->plane.normal, p->directlight);
        VectorScale(p->directlight, 1/255.0, p->directlight);
    }
    
    // make edge planes
    for (int i=0; i<p->w->numpoints; i++)
    {
        plane_t dest;
        
        const vec_t *v0 = p->w->p[i];
        const vec_t *v1 = p->w->p[(i + 1) % p->w->numpoints];
        
        vec3_t edgevec;
        VectorSubtract(v1, v0, edgevec);
        VectorNormalize(edgevec);
        
        CrossProduct(edgevec, p->plane.normal, dest.normal);
        dest.dist = DotProduct(dest.normal, v0);
        
        p->edgeplanes.push_back(dest);
    }
    
    // save
    unique_lock<mutex> lck { facenumToPatches_mutex };
    facenumToPatches[i].push_back(p);
}

/*
=============
DicePatch

Chops the patch by a global grid
From q3rad
=============
*/
void	DicePatch (const bsp2_t *bsp, const bsp2_dface_t *sourceface, winding_t *w, vec_t subdiv)
{
    winding_t   *o1, *o2;
    vec3_t	mins, maxs;
    vec3_t	split;
    vec_t	dist;
    int		i;
    
    if (!w)
        return;
    
    WindingBounds (w, mins, maxs);
    for (i=0 ; i<3 ; i++)
        if (floor((mins[i]+1)/subdiv) < floor((maxs[i]-1)/subdiv))
            break;
    if (i == 3)
    {
        // no splitting needed
        SavePatch(bsp, sourceface, w);
        return;
    }
    
    //
    // split the winding
    //
    VectorCopy (vec3_origin, split);
    split[i] = 1;
    dist = subdiv*(1+floor((mins[i]+1)/subdiv));
    ClipWinding (w, split, dist, &o1, &o2);
    free(w);
    
    //
    // create a new patch
    //
    DicePatch(bsp, sourceface, o1, subdiv);
    DicePatch(bsp, sourceface, o2, subdiv);
}

static void *
MakeBounceLightsThread (void *arg)
{
    const bsp2_t *bsp = (const bsp2_t *)arg;
    
    while (1) {
        int i = GetThreadWork();
        if (i == -1)
            break;
    
        const bsp2_dface_t *face = &bsp->dfaces[i];
        if (bsp->texinfo[face->texinfo].flags & TEX_SPECIAL) {
            continue;
        }
        
        if (!strcmp("skip", Face_TextureName(bsp, face))) {
            continue;
        }
        
        winding_t *winding = WindingFromFace(bsp, face);
        DicePatch(bsp, face, winding, 1024);
    }
    
    return NULL;
}

// Returns color in [0,1]
void Texture_AvgColor (const bsp2_t *bsp, const miptex_t *miptex, vec3_t color)
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
    VectorScale(color, 1.0 / 255.0, color);
}

void MakeTextureColors (const bsp2_t *bsp)
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
        
        printf("%s has color %f %f %f\n", name.c_str(), color.v[0], color.v[1], color.v[2]);
        texturecolors[name] = color;
    }
}

void MakeBounceLights (const bsp2_t *bsp)
{
    logprint("--- MakeBounceLights ---\n");
    
    const dmodel_t *model = &bsp->dmodels[0];
    RunThreadsOn(model->firstface, model->firstface + model->numfaces, MakeBounceLightsThread, (void *)bsp);
    
    int patches  = 0;
    
    //FILE *f = fopen("bounce.map", "w");
    
    for (auto mapentry : facenumToPatches) {
        for (auto patch : mapentry.second) {
            patches++;

            // create VPL
            if (patch->directlight[0] > 0
                && patch->directlight[1] > 0
                && patch->directlight[2] > 0) {
                bouncelight_t l;
                VectorCopy(patch->samplepoint, l.pos);
                VectorCopy(patch->directlight, l.color);
                VectorCopy(patch->plane.normal, l.surfnormal);
                l.area = WindingArea(patch->w);
                l.leaf = Light_PointInLeaf(bsp, l.pos);
                
                // scale by texture color
                const bsp2_dface_t *f = &bsp->dfaces[mapentry.first];
                const char *facename = Face_TextureName(bsp, f);
                if (texturecolors.find(facename) != texturecolors.end()) {
                    vec3_struct_t texcolor = texturecolors.at(facename);
                    for (int i=0; i<3; i++)
                        l.color[i] *= texcolor.v[i];
                }
                
                radlights.push_back(l);
                //fprintf(f, "{\n\"classname\" \"light\"\n\"origin\" \"%f %f %f\"\n}\n", l.pos[0], l.pos[1], l.pos[2]);
            }
        }
    }
    //fclose(f);
    
    logprint("created %d patches\n", patches);
    logprint("created %d bounce lights\n", (int)radlights.size());
    
    bouncelights = radlights.data();
    numbouncelights = radlights.size();
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
        int vertnum = GetSurfaceVertex(bsp, face, i);
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

void
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

vector<vector<const bsp2_dleaf_t *>> faceleafs;
vector<bool> leafhassky;

// index some stuff from the bsp
void BuildPvsIndex(const bsp2_t *bsp)
{
    // build leafsForFace
    faceleafs.resize(bsp->numfaces);
    for (int i = 0; i < bsp->numleafs; i++) {
        const bsp2_dleaf_t *leaf = &bsp->dleafs[i];
        for (int k = 0; k < leaf->nummarksurfaces; k++) {
            const int facenum = bsp->dmarksurfaces[leaf->firstmarksurface + k];
            faceleafs.at(facenum).push_back(leaf);
        }
    }
    
    // build leafhassky
    leafhassky.resize(bsp->numleafs, false);
    for (int i = 0; i < bsp->numleafs; i++) {
        const bsp2_dleaf_t *leaf = &bsp->dleafs[i];
        
        // search for sky faces in it
        for (int k = 0; k < leaf->nummarksurfaces; k++) {
            const bsp2_dface_t *surf = &bsp->dfaces[bsp->dmarksurfaces[leaf->firstmarksurface + k]];
            const char *texname = Face_TextureName(bsp, surf);
            if (!strncmp("sky", texname, 3)) {
                leafhassky.at(i) = true;
                break;
            }
        }
    }
}

bool Leaf_HasSky(const bsp2_t *bsp, const bsp2_dleaf_t *leaf)
{
    const int leafnum = leaf - bsp->dleafs;
    return leafhassky.at(leafnum);
}

const bsp2_dleaf_t **Face_CopyLeafList(const bsp2_t *bsp, const bsp2_dface_t *face)
{
    const int facenum = face - bsp->dfaces;
    auto &leafs = faceleafs.at(facenum);
    
    const bsp2_dleaf_t **result = (const bsp2_dleaf_t **) calloc(leafs.size() + 1, sizeof(const bsp2_dleaf_t *));
    for (int i = 0; i<leafs.size(); i++) {
        result[i] = leafs.at(i);
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
main(int argc, const char **argv)
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

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-threads")) {
            numthreads = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-extra")) {
            oversample = 2;
            logprint("extra 2x2 sampling enabled\n");
        } else if (!strcmp(argv[i], "-extra4")) {
            oversample = 4;
            logprint("extra 4x4 sampling enabled\n");
        } else if (!strcmp(argv[i], "-dist")) {
            scaledist = atof(argv[++i]);
        } else if (!strcmp(argv[i], "-range")) {
            rangescale = atof(argv[++i]);
        } else if (!strcmp(argv[i], "-gate")) {
            fadegate = atof(argv[++i]);
        } else if (!strcmp(argv[i], "-light")) {
            minlight.light = atof(argv[++i]);
        } else if (!strcmp(argv[i], "-addmin")) {
            addminlight = true;
        } else if (!strcmp(argv[i], "-gamma")) {
            lightmapgamma = atof(argv[++i]);
            logprint( "Lightmap gamma %f specified on command-line.\n", lightmapgamma );
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
            if (i < argc - 2 && isdigit(argv[i + 1][0]))
                softsamples = atoi(argv[++i]);
            else
                softsamples = -1; /* auto, based on oversampling */
        } else if (!strcmp(argv[i], "-anglescale") || !strcmp(argv[i], "-anglesense")) {
            if (i < argc - 2 && isdigit(argv[i + 1][0])) {
                global_anglescale = atof(argv[++i]);
                logprint("Using global anglescale value of %f from command line.\n", global_anglescale);
            } else
                Error("-anglesense requires a numeric argument (0.0 - 1.0)");
        } else if ( !strcmp( argv[ i ], "-dirt" ) || !strcmp( argv[ i ], "-dirty" ) ) {
            dirty = true;
            globalDirt = true;
            minlightDirt = true;
            logprint( "Dirtmapping enabled globally\n" );
        } else if ( !strcmp( argv[ i ], "-dirtdebug" ) || !strcmp( argv[ i ], "-debugdirt" ) ) {
            dirty = true;
            globalDirt = true;
            dirtDebug = true;
            logprint( "Dirtmap debugging enabled\n" );
        } else if ( !strcmp( argv[ i ], "-dirtmode" ) ) {
            dirtModeSetOnCmdline = true;
            dirtMode = atoi( argv[ ++i ] );
            if ( dirtMode != 0 && dirtMode != 1 ) {
                dirtMode = 0;
            }
            if ( dirtMode == 1 ) {
                logprint( "Enabling randomized dirtmapping\n" );
            }
            else{
                logprint( "Enabling ordered dirtmapping\n" );
            }
        } else if ( !strcmp( argv[ i ], "-dirtdepth" ) ) {
            dirtDepthSetOnCmdline = true;
            dirtDepth = atof( argv[ ++i ] );
            if ( dirtDepth <= 0.0f ) {
                dirtDepth = 128.0f;
            }
            logprint( "Dirtmapping depth set to %.1f\n", dirtDepth );
        } else if ( !strcmp( argv[ i ], "-dirtscale" ) ) {
            dirtScaleSetOnCmdline = true;
            dirtScale = atof( argv[ ++i ] );
            if ( dirtScale <= 0.0f ) {
                dirtScale = 1.0f;
            }
            logprint( "Dirtmapping scale set to %.1f\n", dirtScale );
        } else if ( !strcmp( argv[ i ], "-dirtgain" ) ) {
            dirtGainSetOnCmdline = true;
            dirtGain = atof( argv[ ++i ] );
            if ( dirtGain <= 0.0f ) {
                dirtGain = 1.0f;
            }
            logprint( "Dirtmapping gain set to %.1f\n", dirtGain );
        } else if ( !strcmp( argv[ i ], "-dirtangle" ) ) {
            dirtAngleSetOnCmdline = true;
            dirtAngle = atof( argv[ ++i ] );
            logprint( "Dirtmapping cone angle set to %.1f\n", dirtAngle );
        } else if ( !strcmp( argv[ i ], "-bounce" ) ) {
            bounce = true;
            logprint( "Bounce enabled on command line\n" );
        } else if ( !strcmp( argv[ i ], "-bouncedebug" ) ) {
            bounce = true;
            bouncedebug = true;
            logprint( "Bounce debugging mode enabled on command line\n" );
        } else if ( !strcmp( argv[ i ], "-bouncescale" ) ) {
            bounce = true;
            bouncescale = atof( argv[ ++i ] );
            logprint( "Bounce scale factor set to %f on command line\n", bouncescale );
        } else if ( !strcmp( argv[ i ], "-fence" ) ) {
            testFenceTextures = true;
            logprint( "Fence texture tracing enabled on command line\n" );
        } else if ( !strcmp( argv[ i ], "-surflight_subdivide" ) ) {
            surflight_subdivide = atof( argv[ ++i ] );
            surflight_subdivide = qmin(qmax(surflight_subdivide, 64.0f), 2048.0f);
            logprint( "Using surface light subdivision size of %f\n", surflight_subdivide);
        } else if ( !strcmp( argv[ i ], "-surflight_dump" ) ) {
            surflight_dump = true;
        } else if ( !strcmp( argv[ i ], "-sunsamples" ) ) {
            sunsamples = atof( argv[ ++i ] );
            sunsamples = qmin(qmax(sunsamples, 8), 2048);
            logprint( "Using sunsamples of %d\n", sunsamples);
        } else if ( !strcmp( argv[ i ], "-onlyents" ) ) {
            onlyents = true;
            logprint( "Onlyents mode enabled\n" );
        } else if ( !strcmp( argv[ i ], "-no_parse_escape_sequences" ) ) {
            parse_escape_sequences = false;
            logprint( "Parsing escape sequences disabled\n" );
        } else if ( !strcmp( argv[ i ], "-phongdebug" ) ) {
            phongDebug = true;
            write_litfile |= 1;
            logprint( "Phong shading debug mode enabled\n" );
        } else if ( !strcmp( argv[ i ], "-novis" ) ) {
            novis = true;
            logprint( "Skipping use of vis data to optimize lighting\n" );
        } else if (argv[i][0] == '-')
            Error("Unknown option \"%s\"", argv[i]);
        else
            break;
    }

    if (i != argc - 1) {
        printf("usage: light [-threads num] [-extra|-extra4]\n"
               "             [-light num] [-addmin] [-anglescale|-anglesense]\n"
               "             [-dist n] [-range n] [-gate n] [-lit|-lit2] [-lux] [-bspx] [-lmscale n]\n"
               "             [-dirt] [-dirtdebug] [-dirtmode n] [-dirtdepth n] [-dirtscale n] [-dirtgain n] [-dirtangle n]\n"
               "             [-soft [n]] [-fence] [-gamma n] [-surflight_subdivide n] [-surflight_dump] [-onlyents] [-sunsamples n] [-no_parse_escape_sequences] [-phongdebug] bspfile\n");
        exit(1);
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
    StripExtension(source);
    DefaultExtension(source, ".lit");
    remove(source);
    
    StripExtension(source);
    DefaultExtension(source, ".bsp");
    LoadBSPFile(source, &bspdata);

    loadversion = bspdata.version;
    if (bspdata.version != BSP2VERSION)
        ConvertBSPFormat(BSP2VERSION, &bspdata);

    BuildPvsIndex(bsp);
    LoadExtendedTexinfoFlags(source, bsp);
    LoadEntities(bsp);

    modelinfo = (modelinfo_t *)malloc(bsp->nummodels * sizeof(*modelinfo));
    FindModelInfo(bsp, lmscaleoverride);
    SetupLights(bsp);
    
    if (!onlyents)
    {
        if (dirty)
            SetupDirt();

        MakeTnodes(bsp);
        
        if (bounce) {
            MakeTextureColors(bsp);
            MakeBounceLights(bsp);
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
    logprint("%5.1f seconds elapsed\n", end - start);

    close_log();

    free(modelinfo);
    
    return 0;
}
