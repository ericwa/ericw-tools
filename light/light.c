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
const vec3_t vec3_white = { 255, 255, 255 };

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

qboolean globalDirt = false;
qboolean minlightDirt = false;

qboolean dirtSetOnCmdline = false;
qboolean dirtModeSetOnCmdline = false;
qboolean dirtDepthSetOnCmdline = false;
qboolean dirtScaleSetOnCmdline = false;
qboolean dirtGainSetOnCmdline = false;

qboolean testFenceTextures = false;

qboolean lit2pass = false;	// currently doing the high-res lit2 light pass

byte *filebase;
byte *lit_filebase;
byte *lux_filebase;
static int lightmap_buffer_used;

static modelinfo_t *modelinfo;
const dmodel_t *const *tracelist;

int oversample = 1;
int write_litfile = 0;	/* 0 for none, 1 for .lit, 2 for bspx, 4 for .lit2 */
int write_luxfile = 0;	/* 0 for none, 1 for .lux, 2 for bspx, 3 for both */

byte *lmshifts;
int lmshift_override = -1;

void
GetFileSpace(byte **lightdata, byte **colordata, byte **deluxdata, int size)
{
    int current_dest;

    ThreadLock();

    /* align to 4 byte boudaries */
    current_dest = ((lightmap_buffer_used + 3) & ~3);
    lightmap_buffer_used = current_dest + size;

    if (lightmap_buffer_used > MAX_MAP_LIGHTING)
	Error("%s: overrun", __func__);

    *lightdata = filebase + current_dest;
    *colordata = lit_filebase + 3*current_dest;
    *deluxdata = lux_filebase + 3*current_dest;

    ThreadUnlock();
}

static void *
LightThread(void *arg)
{
    int facenum, i;
    dmodel_t *model;
    const bsp2_t *bsp = arg;
    struct ltface_ctx *ctx;

    while (1) {
	facenum = GetThreadWork();
	if (facenum == -1)
	    break;

	/* Find the correct model offset */
	for (i = 0, model = bsp->dmodels; i < bsp->nummodels; i++, model++) {
	    if (facenum < model->firstface)
		continue;
	    if (facenum < model->firstface + model->numfaces)
		break;
	}
	if (i == bsp->nummodels) {
	    logprint("warning: no model has face %d\n", facenum);
	    continue;
	}
	ctx = LightFaceInit(bsp);
	LightFace(bsp->dfaces + facenum, bsp->dfacesup + facenum, &modelinfo[i], ctx);
	LightFaceShutdown(ctx);
    }

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

    shadowmodels = malloc(sizeof(dmodel_t *) * (bsp->nummodels + 1));
    memset(shadowmodels, 0, sizeof(dmodel_t *) * (bsp->nummodels + 1));

    /* The world always casts shadows */
    shadowmodels[0] = &bsp->dmodels[0];
    numshadowmodels = 1;

    memset(modelinfo, 0, sizeof(*modelinfo) * bsp->nummodels);
    modelinfo[0].model = &bsp->dmodels[0];


    for (i = 1, info = modelinfo + 1; i < bsp->nummodels; i++, info++) {
	info->model = &bsp->dmodels[i];

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
	if (!VectorCompare(info->minlight.color, vec3_origin)) {
	    if (!write_litfile)
		write_litfile |= WRITE_LIT;
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

/*
 ==================
 FinishBSPFile
 ==================
 */
void
LoadLMScaleFile(const bsp2_t *bsp, const char *name)
{
	int i;
	char source[1024];
	FILE *LmscaleFile;
	
	strcpy(source, name);
	StripExtension(source);
	DefaultExtension(source, ".lmscale");
	
	LmscaleFile = fopen(source, "rb");
	if (!LmscaleFile)
	    return;
	
	lmshifts = calloc(bsp->numfaces, 1);
    
	if (bsp->numfaces != fread(lmshifts, 1, bsp->numfaces, LmscaleFile)) {
	    logprint("Ignoring corrupt lmscale file: '%s'.\n", source);
	    free(lmshifts);
	    lmshifts = NULL;
	}
	
	fclose(LmscaleFile);
    
    for (i=0; i<bsp->numfaces; i++) {
	if (lmshifts[i] != 4) {
	    write_litfile = WRITE_LIT | WRITE_LIT2;
	    logprint("Non-vanilla lightmap scale detected, enabling .lit2 output\n");
	    return;
	}
    }
}

/*
 * =============
 *  LightWorld
 * =============
 */
static void
LightWorld(bsp2_t *bsp)
{
    if (filebase)
	free(filebase);
    if (lit_filebase)
	free(lit_filebase);
    if (lux_filebase)
	free(lux_filebase);

    lightmap_buffer_used = 0;

    filebase = calloc(MAX_MAP_LIGHTING, 1);
    lit_filebase = calloc(MAX_MAP_LIGHTING, 3);
    lux_filebase = calloc(MAX_MAP_LIGHTING, 3);

    if (!filebase || !lit_filebase || !lux_filebase)
	Error("%s: calloc failed", __func__);

    RunThreadsOn(0, bsp->numfaces, LightThread, bsp);
    logprint("Lighting Completed.\n\n");

    bsp->lightdatasize = lightmap_buffer_used;
    bsp->dlightdata = filebase;
    logprint("lightdatasize: %i\n", bsp->lightdatasize);
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
    char *lmscaleoverride = NULL;

    init_log("light.log");
    logprint("---- light / TyrUtils " stringify(TYRUTILS_VERSION) " ----\n");

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
	} else if (!strcmp(argv[i], "-lit")) {
	    write_litfile |= WRITE_LIT;
	} else if (!strcmp(argv[i], "-lit2")) {
	    write_litfile |= WRITE_LIT2;
	} else if (!strcmp(argv[i], "-lux")) {
	    write_luxfile |= 1;
	} else if ( !strcmp( argv[ i ], "-lmscale" ) ) {
	    int j;
	    int lightmapscale = atof(argv[++i]) * 16;
	    
	    for (j = 1, lmshift_override = 0; j < lightmapscale;) {
		j *= 2;
		lmshift_override++;
	    }
	    
	    logprint( "Overriding lightmap scale to %.3g\n", (1<<lmshift_override)/16.0 );

	    write_litfile |= WRITE_LIT2;
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
	} else if ( !strcmp( argv[ i ], "-fence" ) ) {
	    testFenceTextures = true;
	    logprint( "Fence texture tracing enabled on command line\n" );
	} else if (argv[i][0] == '-')
	    Error("Unknown option \"%s\"", argv[i]);
	else
	    break;
    }

    if (i != argc - 1) {
	printf("usage: light [-threads num] [-extra|-extra4]\n"
	       "             [-light num] [-addmin] [-anglescale|-anglesense]\n"
	       "             [-dist n] [-range n] [-gate n] [-lit] [-lit2] [-lmscale n]\n"
	       "             [-dirt] [-dirtdebug] [-dirtmode n] [-dirtdepth n] [-dirtscale n] [-dirtgain n]\n"
	       "             [-soft [n]] [-fence] bspfile\n");
	exit(1);
    }

    if (numthreads > 1)
	logprint("running with %d threads\n", numthreads);

    if (write_litfile & WRITE_LIT)
	logprint(".lit colored light output requested on command line.\n");
    if (write_litfile & WRITE_LIT2)
	logprint(".lit2 colored light output requested on command line.\n");

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
    StripExtension(source);
    DefaultExtension(source, ".bsp");
    LoadBSPFile(source, &bspdata);

    loadversion = bspdata.version;
    if (bspdata.version != BSP2VERSION)
	ConvertBSPFormat(BSP2VERSION, &bspdata);
	
    LoadLMScaleFile(bsp, source);
	
    LoadEntities(bsp);

    if (dirty)
	SetupDirt();

    MakeTnodes(bsp);
    modelinfo = malloc(bsp->nummodels * sizeof(*modelinfo));
    FindModelInfo(bsp, lmscaleoverride);
    
    if (write_litfile & WRITE_LIT2) {
	lit2pass = true;
	LightWorld(bsp);
	WriteLitFile(bsp, source, LIT2_VERSION);
	lit2pass = false;
    }
    
    LightWorld(bsp);
    free(modelinfo);

    WriteEntitiesToString(bsp);

    if (write_litfile & WRITE_LIT) {
	WriteLitFile(bsp, source, LIT_VERSION);
    }
    /* Convert data format back if necessary */
    if (loadversion != BSP2VERSION)
	ConvertBSPFormat(loadversion, &bspdata);
    WriteBSPFile(source, &bspdata);

    end = I_FloatTime();
    logprint("%5.1f seconds elapsed\n", end - start);

    close_log();

    return 0;
}
