/*
    Copyright (C) 1996-1997  Id Software, Inc.
    Copyright (C) 1997       Greg Lewis

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

#include <string.h>

#include "qbsp.h"
#include "wad.h"

static const char *IntroString = "TyrQBSP v" stringify(QBSP_VERSION) "\n\n";

// command line flags
options_t options;

/*
===============
ProcessEntity
===============
*/
static void
ProcessEntity(void)
{
    surface_t *surfs;
    node_t *nodes;

    // No brushes means non-bmodel entity
    if (pCurEnt->iBrushStart == pCurEnt->iBrushEnd)
	return;

    if (map.iEntities > 0) {
	char mod[8];

	if (map.iEntities == 1)
	    Message(msgProgress, "Internal Entities");
	sprintf(mod, "*%i", map.cTotal[BSPMODEL]);
	if (options.fVerbose)
	    PrintEntity(map.iEntities);

	if (hullnum == 0)
	    Message(msgStat, "MODEL: %s", mod);
	SetKeyValue(map.iEntities, "model", mod);
    }
    // take the brush_ts and clip off all overlapping and contained faces,
    // leaving a perfect skin of the model with no hidden faces
    Brush_LoadEntity();

    if (!pCurEnt->pBrushes) {
	PrintEntity(map.iEntities);
	Message(msgError, errNoValidBrushes);
    }

    surfs = CSGFaces();

    FreeBrushsetBrushes();

    if (hullnum != 0) {
	nodes = SolidBSP(surfs, true);
	if (map.iEntities == 0 && !options.fNofill)	// assume non-world bmodels are simple
	{
	    PortalizeWorld(nodes);
	    if (FillOutside(nodes)) {
		// Free portals before regenerating new nodes
		FreeAllPortals(nodes);
		surfs = GatherNodeFaces(nodes);
		nodes = SolidBSP(surfs, false);	// make a really good tree
	    }
	}
	ExportNodePlanes(nodes);
	ExportClipNodes(nodes);
    } else {
	// SolidBSP generates a node tree
	//
	// if not the world, make a good tree first
	// the world is just going to make a bad tree
	// because the outside filling will force a regeneration later
	nodes = SolidBSP(surfs, map.iEntities == 0);

	// build all the portals in the bsp tree
	// some portals are solid polygons, and some are paths to other leafs
	if (map.iEntities == 0 && !options.fNofill)	// assume non-world bmodels are simple
	{
	    PortalizeWorld(nodes);

	    if (FillOutside(nodes)) {
		FreeAllPortals(nodes);

		// get the remaining faces together into surfaces again
		surfs = GatherNodeFaces(nodes);

		// merge polygons
		MergeAll(surfs);

		// make a really good tree
		nodes = SolidBSP(surfs, false);

		// make the real portals for vis tracing
		PortalizeWorld(nodes);

		tjunc(nodes);
	    }
	    FreeAllPortals(nodes);
	}

	ExportNodePlanes(nodes);
	MakeFaceEdges(nodes);
	ExportDrawNodes(nodes);
    }

    map.cTotal[BSPMODEL]++;
}

/*
=================
UpdateEntLump

=================
*/
static void
UpdateEntLump(void)
{
    int m, iEntity;
    char szMod[80];
    char *szClassname;
    vec3_t temp;

    Message(msgStat, "Updating entities lump...");

    VectorCopy(vec3_origin, temp);
    m = 1;
    for (iEntity = 1; iEntity < map.cEntities; iEntity++) {
	if (map.rgEntities[iEntity].iBrushStart ==
	    map.rgEntities[iEntity].iBrushEnd)
	    continue;
	sprintf(szMod, "*%i", m);
	SetKeyValue(iEntity, "model", szMod);
	m++;

	// Do extra work for rotating entities if necessary
	szClassname = ValueForKey(iEntity, "classname");
	if (!strncmp(szClassname, "rotate_", 7))
	    FixRotateOrigin(iEntity, temp);
    }

    LoadBSPFile();
    WriteEntitiesToString();
    WriteBSPFile();

    if (!options.fAllverbose)
	options.fVerbose = false;
}


/*
=================
CreateSingleHull

=================
*/
static void
CreateSingleHull(void)
{
    Message(msgLiteral, "Processing hull %d...\n", hullnum);
    map.cTotal[BSPMODEL] = 0;

    // for each entity in the map file that has geometry
    for (map.iEntities = 0, pCurEnt = &map.rgEntities[0];
	 map.iEntities < map.cEntities; map.iEntities++, pCurEnt++) {
	ProcessEntity();
	if (!options.fAllverbose)
	    options.fVerbose = false;	// don't print rest of entities
    }
}

/*
=================
CreateHulls

=================
*/
static void
CreateHulls(void)
{
    PlaneHash_Init();

    // commanded to create a single hull only
    if (hullnum) {
	CreateSingleHull();
	exit(0);
    }
    // commanded to ignore the hulls altogether
    if (options.fNoclip) {
	CreateSingleHull();
	return;
    }
    // create the hulls sequentially
    if (!options.fNoverbose)
	options.fVerbose = true;
    hullnum = 0;
    CreateSingleHull();

    hullnum = 1;
    CreateSingleHull();

    hullnum = 2;
    CreateSingleHull();

}


/*
=================
ProcessFile
=================
*/
static void
ProcessFile(void)
{
    char *wadstring;
    wad_t *wads;
    int numwads = 0;

    // load brushes and entities
    LoadMapFile();
    if (options.fOnlyents) {
	UpdateEntLump();
	return;
    }

    wadstring = ValueForKey(0, "_wad");
    if (!wadstring)
	wadstring = ValueForKey(0, "wad");
    if (!wadstring)
	Message(msgWarning, warnNoWadKey);
    else
	numwads = WADList_Init(&wads, wadstring);

    if (!numwads) {
	if (wadstring)
	    Message(msgWarning, warnNoValidWads);
	/* Try the default wad name */
	wadstring = AllocMem(OTHER, strlen(options.szMapName) + 5, false);
	strcpy(wadstring, options.szMapName);
	StripExtension(wadstring);
	DefaultExtension(wadstring, ".wad");
	numwads = WADList_Init(&wads, wadstring);
	if (numwads)
	    Message(msgLiteral, "Using default WAD: %s\n", wadstring);
	FreeMem(wadstring, OTHER, strlen(options.szMapName) + 5);
    }

    // init the tables to be shared by all models
    BeginBSPFile();

    // the clipping hulls will not be written out to text files by forked processes :)
    if (!options.fAllverbose)
	options.fVerbose = false;
    CreateHulls();

    WriteEntitiesToString();
    WADList_Process(wads, numwads);
    FinishBSPFile();

    WADList_Free(wads, numwads);
}


/*
==============
PrintOptions
==============
*/
static void
PrintOptions(void)
{
    printf("QBSP performs geometric level processing of Quake .MAP files to create\n");
    printf("Quake .BSP files.\n\n");
    printf("qbsp [options] sourcefile [destfile]\n\n");
    printf("Options:\n");
    printf("   -nofill         Doesn't perform outside filling\n");
    printf("   -noclip         Doesn't build clip hulls\n");
    printf("   -onlyents       Only updates .MAP entities\n");
    printf("   -verbose        Print out more .MAP information\n");
    printf("   -noverbose      Print out almost no information at all\n");
    printf("   -splitspecial   Doesn't combine sky and water faces into one large face\n");
    printf("   -transwater     Computes portal information for transparent water\n");
    printf("   -transsky       Computes portal information for transparent sky\n");
    printf("   -oldaxis        Uses original QBSP texture alignment algorithm\n");
    printf("   -bspleak        Creates a .POR file, used in the BSP editor for leaky maps\n");
    printf("   -oldleak        Create an old-style QBSP .PTS file (default is new style)\n");
    printf("   -nopercent      Prevents output of percent completion information\n");
    printf("   -leakdist  [n]  Space between leakfile points (default 2)\n");
    printf("   -subdivide [n]  Use different texture subdivision (default 240)\n");
    printf("   -wadpath <dir>  Search this directory for wad files\n");
    printf("   sourcefile      .MAP file to process\n");
    printf("   destfile        .BSP file to output\n");

    exit(1);
}


/*
=============
GetTok

Gets tokens from command line string.
=============
*/
static char *
GetTok(char *szBuf, char *szEnd)
{
    char *szTok;

    if (szBuf >= szEnd)
	return NULL;

    // Eliminate leading whitespace
    while (*szBuf == ' ' || *szBuf == '\n' || *szBuf == '\t' ||
	   *szBuf == '\r')
	szBuf++;

    if (szBuf >= szEnd)
	return NULL;

    // Three cases: strings, options, and none-of-the-above.
    if (*szBuf == '\"') {
	szBuf++;
	szTok = szBuf;
	while (*szBuf != 0 && *szBuf != '\"' && *szBuf != '\n'
	       && *szBuf != '\r')
	    szBuf++;
    } else if (*szBuf == '-' || *szBuf == '/') {
	szTok = szBuf;
	while (*szBuf != ' ' && *szBuf != '\n' && *szBuf != '\t' &&
	       *szBuf != '\r' && *szBuf != 0)
	    szBuf++;
    } else {
	szTok = szBuf;
	while (*szBuf != ' ' && *szBuf != '\n' && *szBuf != '\t' &&
	       *szBuf != '\r' && *szBuf != 0)
	    szBuf++;
    }

    if (*szBuf != 0)
	*szBuf = 0;
    return szTok;
}

/*
==================
ParseOptions
==================
*/
static void
ParseOptions(char *szOptions)
{
    char *szTok, *szTok2;
    char *szEnd;
    int NameCount = 0;

    szEnd = szOptions + strlen(szOptions);
    szTok = GetTok(szOptions, szEnd);
    while (szTok) {
	if (szTok[0] != '-' && szTok[0] != '/') {
	    // Treat as filename
	    if (NameCount == 0)
		strcpy(options.szMapName, szTok);
	    else if (NameCount == 1)
		strcpy(options.szBSPName, szTok);
	    else
		Message(msgError, errUnknownOption, szTok);
	    NameCount++;
	} else {
	    szTok++;
	    if (!strcasecmp(szTok, "nofill"))
		options.fNofill = true;
	    else if (!strcasecmp(szTok, "noclip"))
		options.fNoclip = true;
	    else if (!strcasecmp(szTok, "onlyents"))
		options.fOnlyents = true;
	    else if (!strcasecmp(szTok, "verbose"))
		options.fAllverbose = true;
	    else if (!strcasecmp(szTok, "splitspecial"))
		options.fSplitspecial = true;
	    else if (!strcasecmp(szTok, "transwater"))
		options.fTranswater = true;
	    else if (!strcasecmp(szTok, "transsky"))
		options.fTranssky = true;
	    else if (!strcasecmp(szTok, "oldaxis"))
		options.fOldaxis = true;
	    else if (!strcasecmp(szTok, "bspleak"))
		options.fBspleak = true;
	    else if (!strcasecmp(szTok, "noverbose"))
		options.fNoverbose = true;
	    else if (!strcasecmp(szTok, "oldleak"))
		options.fOldleak = true;
	    else if (!strcasecmp(szTok, "nopercent"))
		options.fNopercent = true;
	    else if (!strcasecmp(szTok, "leakdist")) {
		szTok2 = GetTok(szTok + strlen(szTok) + 1, szEnd);
		if (!szTok2)
		    Message(msgError, errInvalidOption, szTok);
		options.dxLeakDist = atoi(szTok2);
		szTok = szTok2;
	    } else if (!strcasecmp(szTok, "subdivide")) {
		szTok2 = GetTok(szTok + strlen(szTok) + 1, szEnd);
		if (!szTok2)
		    Message(msgError, errInvalidOption, szTok);
		options.dxSubdivide = atoi(szTok2);
		szTok = szTok2;
	    } else if (!strcasecmp(szTok, "wadpath")) {
		szTok2 = GetTok(szTok + strlen(szTok) + 1, szEnd);
		if (!szTok2)
		    Message(msgError, errInvalidOption, szTok);
		strcpy(options.wadPath, szTok2);
		szTok = szTok2;
		/* Remove trailing /, if any */
		if (options.wadPath[strlen(options.wadPath) - 1] == '/')
		    options.wadPath[strlen(options.wadPath) - 1] = 0;
	    } else if (!strcasecmp(szTok, "?") || !strcasecmp(szTok, "help"))
		PrintOptions();
	    else
		Message(msgError, errUnknownOption, szTok);
	}

	szTok = GetTok(szTok + strlen(szTok) + 1, szEnd);
    }
}


/*
==================
InitQBSP
==================
*/
static void
InitQBSP(int argc, char **argv)
{
    int i;
    char *szBuf;
    int length;

    logfile = NULL;

    // Initial values
    options.dxLeakDist = 2;
    options.dxSubdivide = 240;
    options.fVerbose = true;
    options.szMapName[0] = options.szBSPName[0] = options.wadPath[0] = 0;

    length = LoadFile("qbsp.ini", (void *)&szBuf, false);
    if (length) {
	Message(msgLiteral, "Loading options from qbsp.ini\n");
	ParseOptions(szBuf);

	FreeMem(szBuf, OTHER, length + 1);
    }

    // Concatenate command line args
    length = 1;
    for (i = 1; i < argc; i++) {
	length += strlen(argv[i]) + 1;
	if (argv[i][0] != '-')
	    length += 2; /* quotes */
    }
    szBuf = AllocMem(OTHER, length, true);
    for (i = 1; i < argc; i++) {
	/* Quote filenames for the parsing function */
	if (argv[i][0] != '-')
	    strcat(szBuf, "\"");
	strcat(szBuf, argv[i]);
	if (argv[i][0] != '-')
	    strcat(szBuf, "\" ");
	else
	    strcat(szBuf, " ");
    }
    szBuf[length - 1] = 0;
    ParseOptions(szBuf);
    FreeMem(szBuf, OTHER, length);

    if (options.szMapName[0] == 0)
	PrintOptions();

    StripExtension(options.szMapName);
    strcat(options.szMapName, ".map");

    // The .map extension gets removed right away anyways...
    if (options.szBSPName[0] == 0)
	strcpy(options.szBSPName, options.szMapName);

    /* Start logging to <bspname>.log */
    StripExtension(options.szBSPName);
    strcat(options.szBSPName, ".log");
    logfile = fopen(options.szBSPName, "wt");
    if (!logfile)
	Message(msgWarning, warnNoLogFile);
    else
	Message(msgFile, IntroString);

    /* If no wadpath given, default to the map directory */
    if (options.wadPath[0] == 0) {
	strcpy(options.wadPath, options.szMapName);
	StripFilename(options.wadPath);
    }

    // Remove already existing files
    if (!options.fOnlyents) {
	StripExtension(options.szBSPName);
	strcat(options.szBSPName, ".bsp");
	remove(options.szBSPName);

	// Probably not the best place to do this
	Message(msgLiteral, "Input file: %s\n", options.szMapName);
	Message(msgLiteral, "Output file: %s\n\n", options.szBSPName);

	StripExtension(options.szBSPName);
	strcat(options.szBSPName, ".prt");
	remove(options.szBSPName);

	StripExtension(options.szBSPName);
	strcat(options.szBSPName, ".pts");
	remove(options.szBSPName);

	StripExtension(options.szBSPName);
	strcat(options.szBSPName, ".por");
	remove(options.szBSPName);
    }
}


/*
==================
main
==================
*/
int
main(int argc, char **argv)
{
    double start, end;

    Message(msgScreen, IntroString);

    InitQBSP(argc, argv);

    // do it!
    start = I_FloatTime();
    ProcessFile();
    end = I_FloatTime();

    Message(msgLiteral, "\n%5.3f seconds elapsed\n", end - start);
    PrintMem();
//      FreeAllMem();
//      PrintMem();

    fclose(logfile);

    return 0;
}
