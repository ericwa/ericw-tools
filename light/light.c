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

#include <light/light.h>

float scaledist = 1.0;
float rangescale = 0.5;
int worldminlight = 0;
vec3_t minlight_color = { 255, 255, 255 };	/* defaults to white light   */
int sunlight = 0;
vec3_t sunlight_color = { 255, 255, 255 };	/* defaults to white light   */
vec3_t sunmangle = { 0, 0, 16384 };	/* defaults to straight down */

byte *filebase;			// start of lightmap data
static byte *file_p;		// start of free space after data
static byte *file_end;		// end of free space for lightmap data

byte *lit_filebase;		// start of litfile data
static byte *lit_file_p;	// start of free space after litfile data
static byte *lit_file_end;	// end of space for litfile data

static int bspfileface;		/* next surface to dispatch */

qboolean extrasamples;
qboolean compress_ents;
qboolean facecounter;
qboolean colored;
qboolean bsp30;
qboolean litfile;
qboolean nominlimit;

qboolean nolightface[MAX_MAP_FACES];
vec3_t faceoffset[MAX_MAP_FACES];

byte *
GetFileSpace(int size)
{
    byte *buf;

    LOCK;
    /* align to 4 byte boudaries */
    file_p = (byte *)(((long)file_p + 3) & ~3);
    buf = file_p;
    file_p += size;
    UNLOCK;
    if (file_p > file_end)
	Error("%s: overrun", __func__);
    return buf;
}

byte *
GetLitFileSpace(int size)
{
    byte *buf;

    LOCK;
    /* align to 12 byte boundaries (match offets with 3 * GetFileSpace) */
    if ((long)lit_file_p % 12)
	lit_file_p += 12 - ((long)lit_file_p % 12);
    buf = lit_file_p;
    lit_file_p += size;
    UNLOCK;
    if (lit_file_p > lit_file_end)
	Error("%s: overrun", __func__);
    return buf;
}

static void
LightThread(void *junk)
{
    int i;

    while (1) {
	LOCK;
	i = bspfileface++;
	UNLOCK;
	if (!facecounter) {
	    printf("Lighting face %i of %i\r", i, numfaces);
	    fflush(stdout);
	}
	if (i >= numfaces) {
	    logprint("\nLighting Completed.\n\n");
	    return;
	}
	LightFace(i, nolightface[i], faceoffset[i]);
    }
}

static void
FindFaceOffsets(void)
{
    int i, j;
    entity_t *ent;
    char name[20];
    char *classname;
    vec3_t org;

    memset(nolightface, 0, sizeof(nolightface));

    for (j = dmodels[0].firstface; j < dmodels[0].numfaces; j++)
	nolightface[j] = 0;

    for (i = 1; i < nummodels; i++) {
	sprintf(name, "*%d", i);
	ent = FindEntityWithKeyPair("model", name);
	if (!ent)
	    Error("%s: Couldn't find entity for model %s.\n", __func__, name);

	classname = ValueForKey(ent, "classname");
	if (!strncmp(classname, "rotate_", 7)) {
	    int start;
	    int end;

	    GetVectorForKey(ent, "origin", org);

	    start = dmodels[i].firstface;
	    end = start + dmodels[i].numfaces;
	    for (j = start; j < end; j++) {
		nolightface[j] = 300;
		faceoffset[j][0] = org[0];
		faceoffset[j][1] = org[1];
		faceoffset[j][2] = org[2];
	    }
	}
    }
}

/*
 * =============
 *  LightWorld
 * =============
 */
static void
LightWorld(void)
{
    if (dlightdata)
	free(dlightdata);

    if (colored)
	lightdatasize = MAX_MAP_LIGHTING;
    else
	lightdatasize = MAX_MAP_LIGHTING / 4;
    dlightdata = malloc(lightdatasize + 16); /* for alignment */
    if (!dlightdata)
	Error("%s: allocation of %i bytes failed.", __func__, lightdatasize);
    memset(dlightdata, 0, lightdatasize + 16);

    if (litfile)
	lightdatasize /= 4;

    /* align filebase to a 4 byte boundary */
    filebase = file_p = (byte *)(((unsigned long)dlightdata + 3) & ~3);
    file_end = filebase + lightdatasize;

    if (colored && litfile) {
	/* litfile data stored in dlightdata, after the white light */
	lit_filebase = file_end + 12 - ((unsigned long)file_end % 12);
	lit_file_p = lit_filebase;
	lit_file_end = lit_filebase + 3 * (MAX_MAP_LIGHTING / 4);
    }

    RunThreadsOn(LightThread);
    lightdatasize = file_p - filebase;
    logprint("lightdatasize: %i\n", lightdatasize);
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
    int i, bsp_version;
    double start;
    double end;
    char source[1024];

    init_log("light.log");
    logprint("----- TyrLight v0.99e -----\n"
#if 0
	     "** Beta version " __DATE__ " " __TIME__ "\n"
#endif
	);

    for (i = 1; i < argc; i++) {
	if (!strcmp(argv[i], "-threads")) {
	    numthreads = atoi(argv[i + 1]);
	    i++;
	} else if (!strcmp(argv[i], "-extra")) {
	    extrasamples = true;
	    logprint("extra sampling enabled\n");
	} else if (!strcmp(argv[i], "-dist")) {
	    scaledist = atof(argv[i + 1]);
	    i++;
	} else if (!strcmp(argv[i], "-range")) {
	    rangescale = atof(argv[i + 1]);
	    i++;
	} else if (!strcmp(argv[i], "-light")) {
	    worldminlight = atof(argv[i + 1]);
	    i++;
	} else if (!strcmp(argv[i], "-compress")) {
	    compress_ents = true;
	    logprint("light entity compression enabled\n");
	} else if (!strcmp(argv[i], "-nocount")) {
	    facecounter = true;
	} else if (!strcmp(argv[i], "-colored") ||
		   !strcmp(argv[i], "-coloured")) {
	    colored = true;
	} else if (!strcmp(argv[i], "-bsp30")) {
	    bsp30 = true;
	} else if (!strcmp(argv[i], "-lit")) {
	    litfile = true;
	} else if (!strcmp(argv[i], "-nominlimit")) {
	    nominlimit = true;
	} else if (argv[i][0] == '-')
	    Error("Unknown option \"%s\"", argv[i]);
	else
	    break;
    }

    // Switch on colored flag if specifying -lit or -bsp30
    if (bsp30 || litfile)
	colored = true;

    // Check the colored options
    if (colored) {
	if (!bsp30 && !litfile) {
	    logprint("colored output format not specified -> using bsp 30\n");
	    bsp30 = true;
	} else if (bsp30 && litfile) {
	    Error("Two colored output formats specified");
	} else if (litfile) {
	    logprint("colored output format: lit\n");
	} else if (bsp30) {
	    logprint("colored output format: bsp30\n");
	}
    }

    if (i != argc - 1)
	Error("usage: light [-threads num] [-light num] [-extra]\n"
	      "             [-colored] [-bsp30] [-lit]\n"
	      "             [-nocount] [-compress] [-nominlimit] bspfile\n");

    InitThreads();

    start = I_FloatTime();

    strcpy(source, argv[i]);
    StripExtension(source);
    DefaultExtension(source, ".bsp");
    bsp_version = LoadBSPFile(source);

    LoadEntities();
    MakeTnodes();
    FindFaceOffsets();
    LightWorld();

    WriteEntitiesToString();

    if (colored && bsp30)
	WriteBSPFile(source, 30);
    else
	WriteBSPFile(source, bsp_version);

    if (colored && litfile)
	WriteLitFile(source, LIT_VERSION);

    end = I_FloatTime();
    logprint("%5.1f seconds elapsed\n", end - start);

    close_log();

    return 0;
}
