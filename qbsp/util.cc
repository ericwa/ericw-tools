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
#include <cstddef>
#include <cstdarg>
#include <cstdlib>
#include <iostream>

#include <common/threads.hh>
#include <common/log.hh>

#include <qbsp/qbsp.hh>

/*
==========
AllocMem
==========
*/
void *AllocMem(int Type, int cElements, bool fZero)
{
    void *pTemp;
    int cSize;

    if (!(Type == WINDING || Type == OTHER))
        FError("Internal error: invalid memory type {}", Type);

    // For windings, cElements == number of points on winding
    if (Type == WINDING) {
        if (cElements > MAX_POINTS_ON_WINDING)
            FError("Too many points ({}) on winding", cElements);

        // cSize = offsetof(winding_t, points[cElements]) + sizeof(int);
        cSize = offsetof(winding_t, points[0]);
        cSize += cElements * sizeof(static_cast<winding_t *>(nullptr)->points[0]);
        cSize += sizeof(int);

        // Set cElements to 1 so bookkeeping works OK
        cElements = 1;
    } else {
        cSize = cElements;
    }
    pTemp = malloc(cSize);
    if (!pTemp)
        FError("allocation of {} bytes failed", cSize);

    if (fZero)
        memset(pTemp, 0, cSize);

    return pTemp;
}
