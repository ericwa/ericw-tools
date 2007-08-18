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
// cmdlib.c

#include "qbsp.h"
#include <sys/types.h>
#include <sys/timeb.h>

#ifdef _WIN32
# include <direct.h>
# define timeb  _timeb
# define ftime  _ftime
#else
# include <unistd.h>
#endif

#define PATHSEPERATOR   '/'


char *
copystring(char *s)
{
    char *b;

    b = AllocMem(OTHER, strlen(s) + 1, true);
    strcpy(b, s);
    return b;
}



/*
================
I_FloatTime
================
*/
double
I_FloatTime(void)
{
    struct timeb timebuffer;

    ftime(&timebuffer);

    return (double)timebuffer.time + (timebuffer.millitm / 1000.0);
}


/*
=============================================================================

						MISC FUNCTIONS

=============================================================================
*/


void
DefaultExtension(char *path, char *extension)
{
    char *src;

//
// if path doesn't have a .EXT, append extension
// (extension should include the .)
//
    src = path + strlen(path) - 1;

    while (*src != PATHSEPERATOR && src != path) {
	if (*src == '.')
	    return;		// it has an extension
	src--;
    }

    strcat(path, extension);
}


void
StripExtension(char *path)
{
    int length;

    length = strlen(path) - 1;
    while (length > 0 && path[length] != '.') {
	length--;
	if (path[length] == '/')
	    return;		// no extension
    }
    if (length)
	path[length] = 0;
}

void
StripFilename(char *path)
{
    int length;

    length = strlen(path) - 1;
    while (length > 0 && path[length] != PATHSEPERATOR)
        length--;
    path[length] = '\0';
}

int
IsAbsolutePath(const char *path)
{
    return path[0] == '/' || (isalpha(path[0]) && path[1] == ':');
}
