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

#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>

#include "common/threads.h"
#include "common/log.h"

#include "qbsp.h"

static int rgMemTotal[GLOBAL + 1];
static int rgMemActive[GLOBAL + 1];
static int rgMemPeak[GLOBAL + 1];
static int rgMemActiveBytes[GLOBAL + 1];
static int rgMemPeakBytes[GLOBAL + 1];

/*
==========
AllocMem
==========
*/
void *
AllocMem(int Type, int cElements, bool fZero)
{
    void *pTemp;
    int cSize;

    if (Type < 0 || Type > OTHER)
	Error("Internal error: invalid memory type %d (%s)", Type, __func__);

    // For windings, cElements == number of points on winding
    if (Type == WINDING) {
	if (cElements > MAX_POINTS_ON_WINDING)
	    Error("Too many points (%d) on winding (%s)", cElements, __func__);

	cSize = offsetof(winding_t, points[cElements]) + sizeof(int);

	// Set cElements to 1 so bookkeeping works OK
	cElements = 1;
    } else
	cSize = cElements * MemSize[Type];

    pTemp = malloc(cSize);
    if (!pTemp)
	Error("allocation of %d bytes failed (%s)", cSize, __func__);

    if (fZero)
	memset(pTemp, 0, cSize);

    // Special stuff for face_t
    if (Type == FACE && cElements == 1)
	((face_t *)pTemp)->planenum = -1;
    if (Type == WINDING) {
	*(int *)pTemp = cSize;
	pTemp = (char *)pTemp + sizeof(int);
    }

    rgMemTotal[Type] += cElements;
    rgMemActive[Type] += cElements;
    rgMemActiveBytes[Type] += cSize;
    if (rgMemActive[Type] > rgMemPeak[Type])
	rgMemPeak[Type] = rgMemActive[Type];
    if (rgMemActiveBytes[Type] > rgMemPeakBytes[Type])
	rgMemPeakBytes[Type] = rgMemActiveBytes[Type];

    // Also keep global statistics
    rgMemTotal[GLOBAL] += cSize;
    rgMemActive[GLOBAL] += cSize;
    if (rgMemActive[GLOBAL] > rgMemPeak[GLOBAL])
	rgMemPeak[GLOBAL] = rgMemActive[GLOBAL];

    return pTemp;
}


/*
==========
FreeMem
==========
*/
void
FreeMem(void *pMem, int Type, int cElements)
{
    rgMemActive[Type] -= cElements;
    if (Type == WINDING) {
	pMem = (char *)pMem - sizeof(int);
	rgMemActiveBytes[Type] -= *(int *)pMem;
	rgMemActive[GLOBAL] -= *(int *)pMem;
    } else {
	rgMemActiveBytes[Type] -= cElements * MemSize[Type];
	rgMemActive[GLOBAL] -= cElements * MemSize[Type];
    }

    free(pMem);
}


static const char *
MemString(int bytes)
{
    static char buf[20];

    if (bytes > 1024 * 1024 * 1024 / 2)
	snprintf(buf, 20, "%0.1fG", (float)bytes / (1024 * 1024 * 1024));
    else if (bytes > 1024 * 1024 / 2)
	snprintf(buf, 20, "%0.1fM", (float)bytes / (1024 * 1024));
    else if (bytes > 1024 / 2)
	snprintf(buf, 20, "%0.1fk", (float)bytes / 1024);
    else
	buf[0] = 0;

    return buf;
}

/*
==========
PrintMem
==========
*/
void
PrintMem(void)
{
    const char *MemTypes[] = {
	"BSPEntity", "BSPPlane", "BSPTex", "BSPVertex", "BSPVis", "BSPNode",
	"BSPTexinfo", "BSPFace", "BSPLight", "BSPClipnode", "BSPLeaf",
	"BSPMarksurface", "BSPEdge", "BSPSurfedge", "BSPModel",

	"Mapface", "Mapbrush", "Mapentity", "Winding", "Face", "Plane",
	"Portal", "Surface", "Node", "Brush", "Miptex", "World verts",
	"World edges", "Hash verts", "Other", "Total"
    };
    int i;

    if (options.fVerbose) {
	Message(msgLiteral,
		"\nData type        CurrentNum    PeakNum      PeakMem\n");
	for (i = 0; i <= OTHER; i++)
	    Message(msgLiteral, "%-16s  %9d  %9d %12d %8s\n",
		    MemTypes[i], rgMemActive[i], rgMemPeak[i],
		    rgMemPeakBytes[i], MemString(rgMemPeakBytes[i]));
	Message(msgLiteral, "%-16s                       %12d %8s\n",
		MemTypes[GLOBAL], rgMemPeak[GLOBAL],
		MemString(rgMemPeak[GLOBAL]));
    } else
	Message(msgLiteral, "Peak memory usage: %d (%s)\n", rgMemPeak[GLOBAL],
		MemString(rgMemPeak[GLOBAL]));
}

#if 0
/*
============
FreeAllMem
============
*/
void
FreeAllMem(void)
{
    int i, j;
    epair_t *ep, *next;
    struct lumpdata *lump;
    mapentity_t *entity;

    for (i = 0, entity = map.entities; i < map.numentities; i++, entity++) {
	for (ep = entity->epairs; ep; ep = next) {
	    next = ep->next;
	    if (ep->key)
		FreeMem(ep->key, OTHER, strlen(ep->key) + 1);
	    if (ep->value)
		FreeMem(ep->value, OTHER, strlen(ep->value) + 1);
	    FreeMem(ep, OTHER, sizeof(epair_t));
	}
	lump = entity->lumps;
	for (j = 0; j < BSP_LUMPS; j++)
	    if (lump[j].data)
		FreeMem(lump[j].data, j, lump[j].count);
    }

    FreeMem(map.planes, PLANE, map.maxplanes);
    FreeMem(map.faces, MAPFACE, map.maxfaces);
    FreeMem(map.brushes, MAPBRUSH, map.maxbrushes);
    FreeMem(map.entities, MAPENTITY, map.maxentities);
}
#endif

/* Keep track of output state */
static bool fInPercent = false;

void
Error(const char *error, ...)
{
    va_list argptr;

    /* Using lockless prints so we can error out while holding the lock */
    InterruptThreadProgress__();
    logprint_locked__("************ ERROR ************\n");

    va_start(argptr, error);
    logvprint(error, argptr);
    va_end(argptr);
    logprint_locked__("\n");
    exit(1);
}

/*
=================
Message

Generic output of warnings, stats, etc
=================
*/
void
Message(int msgType, ...)
{
    va_list argptr;
    char szBuffer[512];
    char *szFmt;
    int WarnType;
    int Cur, Total;

    va_start(argptr, msgType);

    // Exit if necessary
    if ((msgType == msgStat || msgType == msgProgress)
	&& (!options.fVerbose || options.fNoverbose))
	return;
    else if (msgType == msgPercent
	     && (options.fNopercent || options.fNoverbose))
	return;

    if (fInPercent && msgType != msgPercent) {
	printf("\r");
	fInPercent = false;
    }

    switch (msgType) {
    case msgWarning:
	WarnType = va_arg(argptr, int);
	if (WarnType >= cWarnings)
	    printf("Internal error: unknown WarnType in Message!\n");
	sprintf(szBuffer, "*** WARNING %02d: ", WarnType);
	vsprintf(szBuffer + strlen(szBuffer), rgszWarnings[WarnType], argptr);
	strcat(szBuffer, "\n");
	break;

    case msgLiteral:
	// Output as-is to screen and log file
	szFmt = va_arg(argptr, char *);
	vsprintf(szBuffer, szFmt, argptr);
	break;

    case msgStat:
	// Output as-is to screen and log file
	szFmt = va_arg(argptr, char *);
	strcpy(szBuffer, "     ");
	vsprintf(szBuffer + strlen(szBuffer), szFmt, argptr);	// Concatenate
	strcat(szBuffer, "\n");
	break;

    case msgProgress:
	// Output as-is to screen and log file
	szFmt = va_arg(argptr, char *);
	strcpy(szBuffer, "---- ");
	vsprintf(szBuffer + strlen(szBuffer), szFmt, argptr);	// Concatenate
	strcat(szBuffer, " ----\n");
	break;

    case msgPercent:
	// Calculate the percent complete.  Only output if it changes.
	Cur = va_arg(argptr, int);
	Total = va_arg(argptr, int);
	if (((Cur + 1) * 100) / Total == (Cur * 100) / Total)
	    return;

	sprintf(szBuffer, "\r%3d%%", ((Cur + 1) * 100) / Total);

	// Handle output formatting properly
	fInPercent = true;
	msgType = msgScreen;
	break;

    case msgFile:
	// Output only to the file
	szFmt = va_arg(argptr, char *);
	vsprintf(szBuffer, szFmt, argptr);
	break;

    case msgScreen:
	// Output only to the screen
	szFmt = va_arg(argptr, char *);
	vsprintf(szBuffer, szFmt, argptr);
	break;

    default:
	printf("Unhandled msgType in message!\n");
	return;
    }

    switch (msgType) {
    case msgScreen:
	fprintf(stdout, "%s", szBuffer);
	fflush(stdout);
	break;
    case msgFile:
	logprint_silent("%s", szBuffer);
	break;
    default:
	logprint("%s", szBuffer);
    }

    va_end(argptr);
}
