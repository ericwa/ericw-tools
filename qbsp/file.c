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

#include <stdarg.h>
#include <stdio.h>

#include "qbsp.h"
#include "file.h"

/*
==============
LoadFile
==============
*/
size_t
LoadFile(const char *filename, void *bufptr, bool nofail)
{
    char **buf = bufptr;
    size_t len;
    FILE *f;

    f = fopen(filename, "rb");
    if (!f) {
	if (nofail)
	    Error("Failed to open %s: %s", filename, strerror(errno));
	return 0;
    }

    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);

    *buf = AllocMem(OTHER, len + 1, false);
    ((char *)*buf)[len] = 0;

    if (fread(*buf, 1, len, f) != len)
	Error("Failure reading from file");

    fclose(f);

    return len;
}
