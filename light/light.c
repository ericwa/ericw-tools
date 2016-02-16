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

#include <light/light.h>
#include <light/entities.h>

float scaledist = 1.0;
float rangescale = 0.5;
float anglescale = 0.5;
float sun_anglescale = 0.5;
float fadegate = EQUAL_EPSILON;
int softsamples = 0;
float lightmapgamma = 1.0;
const vec3_t vec3_white = { 255, 255, 255 };
float surflight_subdivide = 128.0f;
int sunsamples = 64;
qboolean scaledonly = false;
unsigned int lightturb; //water, slime, lava, tele

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

qboolean testFenceTextures = false;
qboolean surflight_dump = false;

static facesup_t *faces_sup;    //lit2/bspx stuff
vec3_t *vertex_normals;

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
const dmodel_t *const *tracelist;

int oversample = 1;
int write_litfile = 0;  /* 0 for none, 1 for .lit, 2 for bspx, 3 for both */
int write_luxfile = 0;  /* 0 for none, 1 for .lux, 2 for bspx, 3 for both */
qboolean onlyents = false;
qboolean phongDebug = false;
qboolean parse_escape_sequences = false;

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
    const bsp2_t *bsp = arg;
    const modelinfo_t *face_modelinfo;
    struct ltface_ctx *ctx;

    ctx = LightFaceInit(bsp);
    if (!ctx)
    {
        logprint("warning: not enough memory for thread context\n");
        return NULL;
    }

    while (1) {
        facenum = GetThreadWork();
        if (facenum == -1)
            break;

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
    }
    LightFaceShutdown(ctx);

    return NULL;
}

static void
FindModelInfo(const bsp2_t *bsp, const char *lmscaleoverride)
{
    int i, shadow, numshadowmodels;
    entity_t *entity;
    char modelname[20];
    const char *attribute;
    const dmodel_t **shadowmodels;
    modelinfo_t *info;
    float lightmapscale;

    shadowmodels = malloc(sizeof(dmodel_t *) * (bsp->nummodels + 1));
    memset(shadowmodels, 0, sizeof(dmodel_t *) * (bsp->nummodels + 1));

    /* The world always casts shadows */
    shadowmodels[0] = &bsp->dmodels[0];
    numshadowmodels = 1;

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
            shadowmodels[numshadowmodels++] = &bsp->dmodels[i];
        } else {
            shadow = atoi(ValueForKey(entity, "_shadowself"));
            if (shadow)
                info->shadowself = true;
        }

        /* Set up the offset for rotate_* entities */
        attribute = ValueForKey(entity, "classname");
        if (!strncmp(attribute, "rotate_", 7))
            GetVectorForKey(entity, "origin", info->offset);

        /* Grab the bmodel minlight values, if any */
        attribute = ValueForKey(entity, "_minlight");
        if (attribute[0])
            info->minlight.light = atoi(attribute);
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
    }

    tracelist = shadowmodels;
}

static float
AngleBetweenVectors(const vec3_t d1, const vec3_t d2)
{
    float cosangle = DotProduct(d1, d2)/(VectorLength(d1)*VectorLength(d2));
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

/* given a triangle, just adds the contribution from the triangle to the given vertexes normals, based upon angles at the verts */
static void
AddTriangleNormals(const vec_t *norm, const dvertex_t *verts, int v1, int v2, int v3)
{
    const vec_t *p1 = verts[v1].point;
    const vec_t *p2 = verts[v2].point;
    const vec_t *p3 = verts[v3].point;
    float weight;
    
    weight = AngleBetweenPoints(p2, p1, p3);
    VectorMA(vertex_normals[v1], weight, norm, vertex_normals[v1]);

    weight = AngleBetweenPoints(p1, p2, p3);
    VectorMA(vertex_normals[v2], weight, norm, vertex_normals[v2]);

    weight = AngleBetweenPoints(p1, p3, p2);
    VectorMA(vertex_normals[v3], weight, norm, vertex_normals[v3]);
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

static void
CalcualateVertexNormals(const bsp2_t *bsp)
{
    int i, j, v1, v2, v3;
    bsp2_dface_t *f;
    vec3_t norm;

    vertex_normals = malloc(sizeof(vec3_t) * bsp->numvertexes);
    memset(vertex_normals, 0, sizeof(vec3_t) * bsp->numvertexes);

    for (i = 0; i < bsp->numfaces; i++)
    {
        f = &bsp->dfaces[i];
        if (!(bsp->texinfo[f->texinfo].flags & TEX_CURVED))     /* we only care about smoothed faces. unsmoothed stuff shouldn't blend. */
            continue;

        if (f->side)
            VectorSubtract(vec3_origin, bsp->dplanes[f->planenum].normal, norm);
        else
            VectorCopy(bsp->dplanes[f->planenum].normal, norm);

        /* now just walk around the surface as a triangle fan */
        v1 = GetSurfaceVertex(bsp, f, 0);
        v2 = GetSurfaceVertex(bsp, f, 1);
        for (j = 2; j < f->numedges; j++)
        {
            v3 = GetSurfaceVertex(bsp, f, j);
            AddTriangleNormals(norm, bsp->dvertexes, v1, v2, v3);
            v2 = v3;
        }
    }

    for (i = 0; i < bsp->numvertexes; i++)
    {
        VectorNormalize(vertex_normals[i]);
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
    bsp2_t *const bsp = &bspdata->data.bsp2;
    const unsigned char *lmshift_lump;
    int i, j;
    if (bsp->dlightdata)
        free(bsp->dlightdata);
    if (lux_buffer)
        free(lux_buffer);

    /* FIXME - remove this limit */
    bsp->lightdatasize = MAX_MAP_LIGHTING;
    bsp->dlightdata = malloc(bsp->lightdatasize + 16); /* for alignment */
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
    lux_buffer = malloc(bsp->lightdatasize*3);
    lux_filebase = lux_buffer + 12 - ((uintptr_t)lux_buffer % 12);
    lux_file_p = lux_filebase;
    lux_file_end = lux_filebase + 3 * (MAX_MAP_LIGHTING / 4);


    if (forcedscale)
        BSPX_AddLump(bspdata, "LMSHIFT", NULL, 0);

    lmshift_lump = BSPX_GetLump(bspdata, "LMSHIFT", NULL);
    if (!lmshift_lump && write_litfile != ~0)
        faces_sup = NULL; //no scales, no lit2
    else
    {   //we have scales or lit2 output. yay...
        faces_sup = malloc(sizeof(*faces_sup) * bsp->numfaces);
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
        uint8_t *styles = malloc(sizeof(*styles)*4*bsp->numfaces);
        int32_t *offsets = malloc(sizeof(*offsets)*bsp->numfaces);
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
        } else if ( !strcmp( argv[ i ], "-lightturb" ) ) {
            lightturb |= 15;
        } else if ( !strcmp( argv[ i ], "-lightwater" ) ) {
            lightturb |= 1;
        } else if ( !strcmp( argv[ i ], "-lightslime" ) ) {
            lightturb |= 2;
        } else if ( !strcmp( argv[ i ], "-lightlava" ) ) {
            lightturb |= 4;
        } else if ( !strcmp( argv[ i ], "-lighttele" ) ) {
            lightturb |= 8;
        } else if (!strcmp(argv[i], "-soft")) {
            if (i < argc - 2 && isdigit(argv[i + 1][0]))
                softsamples = atoi(argv[++i]);
            else
                softsamples = -1; /* auto, based on oversampling */
        } else if (!strcmp(argv[i], "-anglescale") || !strcmp(argv[i], "-anglesense")) {
            if (i < argc - 2 && isdigit(argv[i + 1][0]))
                anglescale = sun_anglescale = atoi(argv[++i]);
            else
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
        } else if ( !strcmp( argv[ i ], "-parse_escape_sequences" ) ) {
            parse_escape_sequences = true;
            logprint( "Parsing escape sequences enabled\n" );
        } else if ( !strcmp( argv[ i ], "-phongdebug" ) ) {
            phongDebug = true;
            write_litfile |= 1;
            logprint( "Phong shading debug mode enabled\n" );
        } else if (argv[i][0] == '-')
            Error("Unknown option \"%s\"", argv[i]);
        else
            break;
    }

    if (i != argc - 1) {
        printf("usage: light [-threads num] [-extra|-extra4]\n"
               "             [-light num] [-addmin] [-anglescale|-anglesense]\n"
               "             [-lightturb] [-lightwater] [-lightslime] [-lightlava] [-lighttele]\n"
               "             [-dist n] [-range n] [-gate n] [-lit|-lit2] [-lux] [-bspx] [-lmscale n]\n"
               "             [-dirt] [-dirtdebug] [-dirtmode n] [-dirtdepth n] [-dirtscale n] [-dirtgain n] [-dirtangle n]\n"
               "             [-soft [n]] [-fence] [-gamma n] [-surflight_subdivide n] [-surflight_dump] [-onlyents] [-sunsamples n] [-parse_escape_sequences] [-phongdebug] bspfile\n");
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
    StripExtension(source);
    DefaultExtension(source, ".bsp");
    LoadBSPFile(source, &bspdata);

    loadversion = bspdata.version;
    if (bspdata.version != BSP2VERSION)
        ConvertBSPFormat(BSP2VERSION, &bspdata);

    LoadEntities(bsp);

    modelinfo = malloc(bsp->nummodels * sizeof(*modelinfo));
    FindModelInfo(bsp, lmscaleoverride);
    SetupLights(bsp);
    
    if (!onlyents)
    {
        if (dirty)
            SetupDirt();

        MakeTnodes(bsp);
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
