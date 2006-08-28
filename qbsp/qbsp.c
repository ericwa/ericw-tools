// qbsp.c

#include "qbsp.h"
#include "wad.h"

#define IntroString "TreeQBSP v1.62 by Greg Lewis.  Source supplied by John Carmack.\n\n"

// command line flags
options_t options;

/*
===============
ProcessEntity
===============
*/
void ProcessEntity (void)
{
	surface_t	*surfs;
	node_t		*nodes;
	
	// No brushes means non-bmodel entity
	if (pCurEnt->iBrushStart == pCurEnt->iBrushEnd)
		return;

	if (map.iEntities > 0)
	{
		char mod[8];

		if (map.iEntities == 1)
			Message(msgProgress, "Internal Entities");
		sprintf (mod, "*%i", map.cTotal[BSPMODEL]);
		if (options.fVerbose)
			PrintEntity (map.iEntities);

		if (hullnum == 0)
			Message(msgStat, "MODEL: %s", mod);
		SetKeyValue (map.iEntities, "model", mod);
	}

	// take the brush_ts and clip off all overlapping and contained faces,
	// leaving a perfect skin of the model with no hidden faces
	Brush_LoadEntity();
	
	if (!pCurEnt->pBrushes)
	{
		PrintEntity (map.iEntities);
		Message(msgError, errNoValidBrushes);
	}
	
	surfs = CSGFaces();
	
	FreeBrushsetBrushes();

	if (hullnum != 0)
	{
		nodes = SolidBSP (surfs, true);
		if (map.iEntities == 0 && !options.fNofill)	// assume non-world bmodels are simple
		{
			PortalizeWorld (nodes);
			if (FillOutside (nodes))
			{
				// Free portals before regenerating new nodes
				FreeAllPortals (nodes);
				surfs = GatherNodeFaces (nodes);
				nodes = SolidBSP (surfs, false);	// make a really good tree
			}
		}
		ExportNodePlanes(nodes);
		ExportClipNodes(nodes);
	}
	else
	{
		// SolidBSP generates a node tree
		//
		// if not the world, make a good tree first
		// the world is just going to make a bad tree
		// because the outside filling will force a regeneration later
		nodes = SolidBSP (surfs, map.iEntities == 0);	
		
		// build all the portals in the bsp tree
		// some portals are solid polygons, and some are paths to other leafs
		if (map.iEntities == 0 && !options.fNofill)	// assume non-world bmodels are simple
		{
			PortalizeWorld (nodes);
		
			if (FillOutside (nodes))
			{
				FreeAllPortals (nodes);

				// get the remaining faces together into surfaces again
				surfs = GatherNodeFaces (nodes);

				// merge polygons
				MergeAll (surfs);

				// make a really good tree
				nodes = SolidBSP (surfs, false);

				// make the real portals for vis tracing
				PortalizeWorld (nodes);

				// fix tjunctions
				if (options.fTjunc)
					tjunc (nodes);
			}
			FreeAllPortals (nodes);
		}

		ExportNodePlanes (nodes);
		MakeFaceEdges (nodes);
		ExportDrawNodes (nodes);
	}

	map.cTotal[BSPMODEL]++;
}

/*
=================
UpdateEntLump

=================
*/
void UpdateEntLump (void)
{
	int m, iEntity;
	char szMod[80];
	char *szClassname;
	vec3_t temp;
		
	Message(msgStat, "Updating entities lump...");
	
	VectorCopy(vec3_origin, temp);
	m = 1;
	for (iEntity = 1; iEntity < map.cEntities; iEntity++)
	{
		if (map.rgEntities[iEntity].iBrushStart == map.rgEntities[iEntity].iBrushEnd)
			continue;
		sprintf (szMod, "*%i", m);
		SetKeyValue (iEntity, "model", szMod);
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
void CreateSingleHull (void)
{
	Message(msgLiteral, "Processing hull %d...\n", hullnum);
	map.cTotal[BSPMODEL] = 0;

	// for each entity in the map file that has geometry
	for (map.iEntities=0, pCurEnt = &map.rgEntities[0];
		 map.iEntities < map.cEntities;
		 map.iEntities++, pCurEnt++)
	{
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
void CreateHulls (void)
{
	// commanded to create a single hull only
	if (hullnum)
	{
		CreateSingleHull ();
		exit(0);
	}
	
	// commanded to ignore the hulls altogether
	if (options.fNoclip)
	{
		CreateSingleHull ();
		return;
	}

	// create the hulls sequentially
	if (!options.fNoverbose)
		options.fVerbose = true;
	hullnum = 0;
	CreateSingleHull ();

	hullnum = 1;
	CreateSingleHull ();
	
	hullnum = 2;
	CreateSingleHull ();

}


/*
=================
ProcessFile
=================
*/
void ProcessFile (void)
{
	char *szWad;
	WAD wad;

	// load brushes and entities
	LoadMapFile();
	if (options.fOnlyents)
	{
		UpdateEntLump ();
		return;
	}

	szWad = ValueForKey(0, "_wad");
	if (!szWad)
	{
		szWad = ValueForKey(0, "wad");
		if (!szWad)
		{
			Message(msgWarning, warnNoWadKey);
			pWorldEnt->cTexdata = 0;
		}
		else if (!wad.InitWADList(szWad))
		{
			Message(msgWarning, warnNoValidWads);
			pWorldEnt->cTexdata = 0;
		}
	}

	// init the tables to be shared by all models
	BeginBSPFile ();

	// the clipping hulls will not be written out to text files by forked processes :)
	if (!options.fAllverbose)
		options.fVerbose = false;
	CreateHulls ();

	WriteEntitiesToString();
	wad.fProcessWAD();
	FinishBSPFile ();
}


/*
==============
PrintOptions
==============
*/
void PrintOptions(void)
{
	printf("QBSP performs geometric level processing of Quake .MAP files to create\n");
	printf("Quake .BSP files.\n\n");
	printf("qbsp [options] sourcefile [destfile]\n\n");
	printf("Options:\n");
	printf("   -tjunc          Enables tjunc calculations (default is disabled)\n");
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
char *GetTok(char *szBuf, char *szEnd)
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
	if (*szBuf == '\"')
	{
		szBuf++;
		szTok = szBuf;
		while (*szBuf != 0 && *szBuf != '\"' && *szBuf != '\n' && *szBuf != '\r')
			szBuf++;
	}
	else if (*szBuf == '-' || *szBuf == '/')
	{
		szTok = szBuf;
		while (*szBuf != ' ' && *szBuf != '\n' && *szBuf != '\t' &&
			   *szBuf != '\r' && *szBuf != 0)
			szBuf++;
	}
	else
	{
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
void ParseOptions(char *szOptions)
{
	char *szTok, *szTok2;
	char *szEnd;
	char szFilename[256] = "";
	int NameCount = 0;

	szEnd = szOptions + strlen(szOptions);
	szTok = GetTok(szOptions, szEnd);
	while (szTok)
	{
		if (szTok[0] != '-' && szTok[0] != '/')
		{
			// Treat as filename
			if (NameCount == 0)
				strcpy(options.szMapName, szTok);
			else if (NameCount == 1)
				strcpy(options.szBSPName, szTok);
			else
				Message(msgError, errUnknownOption, szTok);
			NameCount++;
		}
		else
		{
			szTok++;
			if (!stricmp (szTok, "tjunc"))
				options.fTjunc = true;
			else if (!stricmp (szTok, "nofill"))
				options.fNofill = true;
			else if (!stricmp (szTok, "noclip"))
				options.fNoclip = true;
			else if (!stricmp (szTok, "onlyents"))
				options.fOnlyents = true;
			else if (!stricmp (szTok, "verbose"))
				options.fAllverbose = true;
			else if (!stricmp (szTok, "splitspecial"))
				options.fSplitspecial = true;
			else if (!stricmp (szTok, "transwater"))
				options.fTranswater = true;
			else if (!stricmp (szTok, "transsky"))
				options.fTranssky = true;
			else if (!stricmp (szTok, "oldaxis"))
				options.fOldaxis = true;
			else if (!stricmp (szTok, "bspleak"))
				options.fBspleak = true;
			else if (!stricmp (szTok, "noverbose"))
				options.fNoverbose = true;
			else if (!stricmp (szTok, "oldleak"))
				options.fOldleak = true;
			else if (!stricmp (szTok, "nopercent"))
				options.fNopercent = true;
			else if (!stricmp (szTok, "leakdist"))
			{
				szTok2 = GetTok(szTok+strlen(szTok)+1, szEnd);
				if (!szTok2)
					Message(msgError, errInvalidOption, szTok);
				options.dxLeakDist = atoi(szTok2);
				szTok = szTok2;
			}
			else if (!stricmp (szTok, "subdivide"))
			{
				szTok2 = GetTok(szTok+strlen(szTok), szEnd);
				if (!szTok2)
					Message(msgError, errInvalidOption, szTok);
				options.dxSubdivide = atoi(szTok2);
				szTok = szTok2;
			}
			else if (!stricmp(szTok, "?") ||
					 !stricmp(szTok, "help"))
				PrintOptions();
			else
				Message(msgError, errUnknownOption, szTok);
		}

		szTok = GetTok(szTok+strlen(szTok)+1, szEnd);
	}
}


/*
==================
InitQBSP
==================
*/
void InitQBSP(int argc, char **argv)
{
	int i;
	char szArgs[512];
	char *szBuf;
	int length;
	File IniFile;
		
	// Start logging to qbsp.log
	if (!LogFile.fOpen("qbsp.log", "wt", false))
		Message(msgWarning, warnNoLogFile);
	else
		// Kinda dumb, but hey...
		Message(msgFile, IntroString);

	// Initial values
	options.dxLeakDist = 2;
	options.dxSubdivide = 240;
	options.fVerbose = true;
	options.szMapName[0] = options.szBSPName[0] = 0;

	length = IniFile.LoadFile("qbsp.ini", (void **)&szBuf, false);
	if (length)
	{
		Message(msgLiteral, "Loading options from qbsp.ini\n");
		ParseOptions(szBuf);

		FreeMem(szBuf, OTHER, length+1);
	}

	// Concatenate command line args
	szArgs[0] = 0;
	for (i=1; i<argc; i++)
	{
		// Toss " around filenames to preserve LFNs in the Parsing function
		if (argv[i][0] != '-')
			strcat(szArgs, "\"");
		strcat(szArgs, argv[i]);
		if (argv[i][0] != '-')
			strcat(szArgs, "\" ");
		else
			strcat(szArgs, " ");
	}

	ParseOptions(szArgs);

	if (options.szMapName[0] == 0)
		PrintOptions();
	
	// create destination name if not specified
	DefaultExtension (options.szMapName, ".map");

	// The .map extension gets removed right away anyways...
	if (options.szBSPName[0] == 0)
		strcpy(options.szBSPName, options.szMapName);

	// Remove already existing files
	if (!options.fOnlyents)
	{
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
int main (int argc, char **argv)
{
	double	start, end;

	Message(msgScreen, IntroString);

	InitQBSP(argc, argv);

	// do it!
	start = I_FloatTime ();
	ProcessFile();
	end = I_FloatTime ();

	Message(msgLiteral, "\n%5.3f seconds elapsed\n", end-start);
	PrintMem();
//	FreeAllMem();
//	PrintMem();

	LogFile.Close();

	return 0;
}