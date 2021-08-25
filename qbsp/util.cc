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

#include <mutex>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>

#include <common/threads.hh>
#include <common/log.hh>

#include <qbsp/qbsp.hh>

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

    if (!(Type == WINDING || Type == OTHER))
        Error("Internal error: invalid memory type %d (%s)", Type, __func__);

    // For windings, cElements == number of points on winding
    if (Type == WINDING) {
        if (cElements > MAX_POINTS_ON_WINDING)
            Error("Too many points (%d) on winding (%s)", cElements, __func__);

        //cSize = offsetof(winding_t, points[cElements]) + sizeof(int);
        cSize = offsetof(winding_t, points[0]);
        cSize += cElements * sizeof(static_cast<winding_t*>(nullptr)->points[0]);
        cSize += sizeof(int);

        // Set cElements to 1 so bookkeeping works OK
        cElements = 1;
    } else {
        cSize = cElements;
    }
    pTemp = malloc(cSize);
    if (!pTemp)
        Error("allocation of %d bytes failed (%s)", cSize, __func__);

    if (fZero)
        memset(pTemp, 0, cSize);

    return pTemp;
}

/* Keep track of output state */
static bool fInPercent = false;

std::mutex messageLock;

/*
=================
Message

Generic output of warnings, stats, etc
=================
*/
void
Message(int msgType, ...)
{
    std::unique_lock<std::mutex> lck { messageLock };

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
        vsprintf(szBuffer + strlen(szBuffer), szFmt, argptr);   // Concatenate
        strcat(szBuffer, "\n");
        break;

    case msgProgress:
        // Output as-is to screen and log file
        szFmt = va_arg(argptr, char *);
        strcpy(szBuffer, "---- ");
        vsprintf(szBuffer + strlen(szBuffer), szFmt, argptr);   // Concatenate
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
