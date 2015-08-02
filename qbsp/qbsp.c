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

#include "common/log.h"
#include "qbsp.h"
#include "wad.h"

static const char *IntroString =
    "---- qbsp / TyrUtils " stringify(TYRUTILS_VERSION) " ----\n";

// command line flags
options_t options;

bool
IsObjBModelEntity(const mapentity_t *entity);

bool
IsObjDetailEntity(const mapentity_t *entity);

void ExportObjMap(node_t *headnode);

/*
===============
ProcessEntity
===============
*/
static void
ProcessEntity(mapentity_t *entity, const int hullnum)
{
    int i, numportals, firstface;
    surface_t *surfs;
    node_t *nodes;
    const char *classname;

    /*
     * func_group and func_detail entities get their brushes added to the
     * worldspawn
     */
    classname = ValueForKey(entity, "classname");
    if (!strcasecmp(classname, "func_group"))
	return;
    if (!strcasecmp(classname, "func_detail"))
	return;

    bool isObjBModel = IsObjBModelEntity(entity);

    /* No map brushes means non-bmodel entity */
    if (!entity->nummapbrushes && !isObjBModel)
	return;

    /* OBJs only exist in hull 0 for now */
//    if (isObjBModel && hullnum != 0)
//    	return;

    if (entity != pWorldEnt) {
	char mod[20];

	if (entity == pWorldEnt + 1)
	    Message(msgProgress, "Internal Entities");
	snprintf(mod, sizeof(mod), "*%d", map.cTotal[LUMP_MODELS]);
	if (options.fVerbose)
	    PrintEntity(entity);

	if (hullnum == 0)
	    Message(msgStat, "MODEL: %s", mod);
	SetKeyValue(entity, "model", mod);
    }

    /*
     * Init the entity
     */
    entity->brushes = NULL;
    entity->numbrushes = 0;
    for (i = 0; i < 3; i++) {
	entity->mins[i] = VECT_MAX;
	entity->maxs[i] = -VECT_MAX;
    }

    /*
     * Convert the map brushes (planes) into BSP brushes (polygons)
     */
    

    if (isObjBModel)
    {
    	Message(msgProgress, "Brush_LoadObj (BModel case)");
    	Brush_LoadObj(entity, entity, hullnum, true);
    }
    else
    {
    	Message(msgProgress, "Brush_LoadEntity");
	    Brush_LoadEntity(entity, entity, hullnum);

	    if (!entity->brushes && hullnum) {
		PrintEntity(entity);
		Error("Entity with no valid brushes");
	    }
    }

    /*
     * If this is the world entity, find all func_group and func_detail
     * entities and add their brushes with the appropriate contents flag set.
     */
    if (entity == pWorldEnt) {
	const mapentity_t *source;
	int detailcount;

	/* Add func_group brushes first */
	source = map.entities + 1;
	for (i = 1; i < map.numentities; i++, source++) {
	    classname = ValueForKey(source, "classname");
	    if (!strcasecmp(classname, "func_group"))
		Brush_LoadEntity(entity, source, hullnum);
	}

	/* Add detail brushes next */
	detailcount = 0;
	source = map.entities + 1;
	for (i = 1; i < map.numentities; i++, source++) {
	    classname = ValueForKey(source, "classname");
	    if (!strcasecmp(classname, "func_detail")) {
		int detailstart = entity->numbrushes;
		Brush_LoadEntity(entity, source, hullnum);
		detailcount += entity->numbrushes - detailstart;
	    }
	}

	/* Add .OBJ entities */
	source = map.entities + 1;
	for (i = 1; i < map.numentities; i++, source++) {
	    if (IsObjDetailEntity(source)) {
		Brush_LoadObj(entity, source, hullnum, false);
	    }
	}

	Message(msgStat, "%8d brushes", entity->numbrushes - detailcount);
	if (detailcount)
	    Message(msgStat, "%8d detail", detailcount);
    } else {
	Message(msgStat, "%8d brushes", entity->numbrushes);
    }

    /*
     * Take the brush_t's and clip off all overlapping and contained faces,
     * leaving a perfect skin of the model with no hidden faces
     */
    surfs = CSGFaces(entity);
    FreeBrushes(entity->brushes);

    if (hullnum != 0) {
	nodes = SolidBSP(entity, surfs, true);
	if (entity == pWorldEnt && !options.fNofill) {
	    // assume non-world bmodels are simple
	    numportals = PortalizeWorld(entity, nodes, hullnum);
	    if (FillOutside(nodes, hullnum, numportals)) {
		// Free portals before regenerating new nodes
		FreeAllPortals(nodes);
		surfs = GatherNodeFaces(nodes);
		// make a really good tree
		nodes = SolidBSP(entity, surfs, false);
	    }
	}
	ExportNodePlanes(nodes);
	ExportClipNodes(entity, nodes, hullnum);
    } else {
	/*
	 * SolidBSP generates a node tree
	 *
	 * if not the world, make a good tree first the world is just
	 * going to make a bad tree because the outside filling will
	 * force a regeneration later.
	 *
	 * Forcing the good tree for the first pass on the world can
	 * sometimes result in reduced marksurfaces at the expense of
	 * longer processing time.
	 */
	if (options.forceGoodTree)
	    nodes = SolidBSP(entity, surfs, false);
	else
	    nodes = SolidBSP(entity, surfs, entity == pWorldEnt);

//        if (isObjBModel)
//        {
//            printf("ExportObjToMap\n");
//            ExportObjMap(nodes);
//        }
	
	// build all the portals in the bsp tree
	// some portals are solid polygons, and some are paths to other leafs
	if (entity == pWorldEnt && !options.fNofill) {
	    // assume non-world bmodels are simple
	    numportals = PortalizeWorld(entity, nodes, hullnum);
	    if (FillOutside(nodes, hullnum, numportals)) {
		FreeAllPortals(nodes);

		// get the remaining faces together into surfaces again
		surfs = GatherNodeFaces(nodes);

		// merge polygons
		MergeAll(surfs);

		// make a really good tree
		nodes = SolidBSP(entity, surfs, false);

		// make the real portals for vis tracing
		numportals = PortalizeWorld(entity, nodes, hullnum);

		TJunc(entity, nodes);
	    }
	    FreeAllPortals(nodes);
	}

	ExportNodePlanes(nodes);

	firstface = MakeFaceEdges(entity, nodes);
	ExportDrawNodes(entity, nodes, firstface);
    }

    map.cTotal[LUMP_MODELS]++;
}

/*
=================
UpdateEntLump

=================
*/
static void
UpdateEntLump(void)
{
    int modnum, i;
    char modname[10];
    const char *classname;
    mapentity_t *entity;

    Message(msgStat, "Updating entities lump...");

    modnum = 1;
    for (i = 1, entity = map.entities + 1; i < map.numentities; i++, entity++) {
	if (!entity->nummapbrushes)
	    continue;
	classname = ValueForKey(entity, "classname");
	if (!strcasecmp(classname, "func_detail"))
	    continue;
	if (!strcasecmp(classname, "func_group"))
	    continue;

	snprintf(modname, sizeof(modname), "*%d", modnum);
	SetKeyValue(entity, "model", modname);
	modnum++;

	/* Do extra work for rotating entities if necessary */
	if (!strncmp(classname, "rotate_", 7))
	    FixRotateOrigin(entity);
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
CreateSingleHull(const int hullnum)
{
    int i;
    mapentity_t *entity;

    Message(msgLiteral, "Processing hull %d...\n", hullnum);
    map.cTotal[LUMP_MODELS] = 0;

    // for each entity in the map file that has geometry
    for (i = 0, entity = map.entities; i < map.numentities; i++, entity++) {
	ProcessEntity(entity, hullnum);
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

    /* create the hulls sequentially */
    if (!options.fNoverbose)
	options.fVerbose = true;

    CreateSingleHull(0);

    /* ignore the clipping hulls altogether */
    if (options.fNoclip)
	return;

    CreateSingleHull(1);
    CreateSingleHull(2);
}


/*
=================
ProcessFile
=================
*/
static void
ProcessFile(void)
{
    const char *wadstring;
    char *defaultwad;
    wad_t *wadlist;
    
    // load brushes and entities
    LoadMapFile();
    if (options.fOnlyents) {
	UpdateEntLump();
	return;
    }

    wadlist = NULL;
    wadstring = ValueForKey(pWorldEnt, "_wad");
    if (!wadstring[0])
	wadstring = ValueForKey(pWorldEnt, "wad");
    if (!wadstring[0])
	Message(msgWarning, warnNoWadKey);
    else
	wadlist = WADList_Init(wadstring);

    if (!wadlist) {
	if (wadstring[0])
	    Message(msgWarning, warnNoValidWads);
	/* Try the default wad name */
	defaultwad = AllocMem(OTHER, strlen(options.szMapName) + 5, false);
	strcpy(defaultwad, options.szMapName);
	StripExtension(defaultwad);
	DefaultExtension(defaultwad, ".wad");
	wadlist = WADList_Init(defaultwad);
	if (wadlist)
	    Message(msgLiteral, "Using default WAD: %s\n", defaultwad);
	FreeMem(defaultwad, OTHER, strlen(options.szMapName) + 5);
    }

    // init the tables to be shared by all models
    BeginBSPFile();

    if (!options.fAllverbose)
	options.fVerbose = false;
    CreateHulls();

    WriteEntitiesToString();
    WADList_Process(wadlist);
    FinishBSPFile();

    WADList_Free(wadlist);
}


/*
==============
PrintOptions
==============
*/
static void
PrintOptions(void)
{
    printf("\n"
	   "qbsp performs geometric level processing of Quake .MAP files to create\n"
	   "Quake .BSP files.\n\n"
	   "qbsp [options] sourcefile [destfile]\n\n"
	   "Options:\n"
	   "   -nofill         Doesn't perform outside filling\n"
	   "   -noclip         Doesn't build clip hulls\n"
	   "   -noskip         Doesn't remove faces with the 'skip' texture\n"
	   "   -onlyents       Only updates .MAP entities\n"
	   "   -verbose        Print out more .MAP information\n"
	   "   -noverbose      Print out almost no information at all\n"
	   "   -splitspecial   Doesn't combine sky and water faces into one large face\n"
	   "   -notranswater   Computes portal information for opaque water\n"
	   "   -transsky       Computes portal information for transparent sky\n"
	   "   -oldaxis        Uses original QBSP texture alignment algorithm\n"
	   "   -forcegoodtree  Force use of expensive processing for SolidBSP stage\n"
	   "   -bspleak        Creates a .POR file, used in the BSP editor for leaky maps\n"
	   "   -oldleak        Create an old-style QBSP .PTS file (default is new style)\n"
	   "   -nopercent      Prevents output of percent completion information\n"
	   "   -bsp2           Request output in bsp2 format\n"
	   "   -2psb           Request output in 2psb format (RMQ compatible)\n"
	   "   -leakdist  [n]  Space between leakfile points (default 2)\n"
	   "   -subdivide [n]  Use different texture subdivision (default 240)\n"
	   "   -wadpath <dir>  Search this directory for wad files\n"
	   "   -oldrottex      Use old rotate_ brush texturing aligned at (0 0 0)\n"
	   "   sourcefile      .MAP file to process\n"
	   "   destfile        .BSP file to output\n");

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

    /* Default to the original Quake BSP Version... */
    options.BSPVersion = BSPVERSION;
    options.fTranswater = true;
    options.fixRotateObjTexture = true;

    szEnd = szOptions + strlen(szOptions);
    szTok = GetTok(szOptions, szEnd);
    while (szTok) {
	if (szTok[0] != '-' && szTok[0] != '/') {
	    /* Treat as filename */
	    if (NameCount == 0)
		strcpy(options.szMapName, szTok);
	    else if (NameCount == 1)
		strcpy(options.szBSPName, szTok);
	    else
		Error("Unknown option '%s'", szTok);
	    NameCount++;
	} else {
	    szTok++;
	    if (!strcasecmp(szTok, "nofill"))
		options.fNofill = true;
	    else if (!strcasecmp(szTok, "noclip"))
		options.fNoclip = true;
	    else if (!strcasecmp(szTok, "noskip"))
		options.fNoskip = true;
	    else if (!strcasecmp(szTok, "onlyents"))
		options.fOnlyents = true;
	    else if (!strcasecmp(szTok, "verbose"))
		options.fAllverbose = true;
	    else if (!strcasecmp(szTok, "splitspecial"))
		options.fSplitspecial = true;
	    else if (!strcasecmp(szTok, "notranswater"))
		options.fTranswater = false;
	    else if (!strcasecmp(szTok, "transwater"))
		options.fTranswater = true;
	    else if (!strcasecmp(szTok, "transsky"))
		options.fTranssky = true;
	    else if (!strcasecmp(szTok, "oldaxis"))
		options.fOldaxis = true;
	    else if (!strcasecmp(szTok, "forcegoodtree"))
		options.forceGoodTree = true;
	    else if (!strcasecmp(szTok, "bspleak"))
		options.fBspleak = true;
	    else if (!strcasecmp(szTok, "noverbose"))
		options.fNoverbose = true;
	    else if (!strcasecmp(szTok, "oldleak"))
		options.fOldleak = true;
	    else if (!strcasecmp(szTok, "nopercent"))
		options.fNopercent = true;
	    else if (!strcasecmp(szTok, "bsp2")) {
		options.BSPVersion = BSP2VERSION;
		MemSize = MemSize_BSP2;
	    } else if (!strcasecmp(szTok, "2psb")) {
		options.BSPVersion = BSP2RMQVERSION;
		MemSize = MemSize_BSP2rmq;
	    } else if (!strcasecmp(szTok, "leakdist")) {
		szTok2 = GetTok(szTok + strlen(szTok) + 1, szEnd);
		if (!szTok2)
		    Error("Invalid argument to option %s", szTok);
		options.dxLeakDist = atoi(szTok2);
		szTok = szTok2;
	    } else if (!strcasecmp(szTok, "subdivide")) {
		szTok2 = GetTok(szTok + strlen(szTok) + 1, szEnd);
		if (!szTok2)
		    Error("Invalid argument to option %s", szTok);
		options.dxSubdivide = atoi(szTok2);
		szTok = szTok2;
	    } else if (!strcasecmp(szTok, "wadpath")) {
		szTok2 = GetTok(szTok + strlen(szTok) + 1, szEnd);
		if (!szTok2)
		    Error("Invalid argument to option %s", szTok);
		strcpy(options.wadPath, szTok2);
		szTok = szTok2;
		/* Remove trailing /, if any */
		if (options.wadPath[strlen(options.wadPath) - 1] == '/')
		    options.wadPath[strlen(options.wadPath) - 1] = 0;
            } else if (!strcasecmp(szTok, "oldrottex")) {
                options.fixRotateObjTexture = false;
            } else if (!strcasecmp(szTok, "?") || !strcasecmp(szTok, "help"))
		PrintOptions();
	    else
		Error("Unknown option '%s'", szTok);
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

    // Initial values
    options.dxLeakDist = 2;
    options.dxSubdivide = 240;
    options.fVerbose = true;
    options.szMapName[0] = options.szBSPName[0] = options.wadPath[0] = 0;

    length = LoadFile("qbsp.ini", &szBuf, false);
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
    init_log(options.szBSPName);

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

    close_log();

    return 0;
}
