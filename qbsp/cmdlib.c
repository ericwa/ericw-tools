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
#include <string.h>

#ifdef WIN32
#include <windows.h>
#endif

#ifdef LINUX
#include <sys/time.h>
#endif

#define PATHSEPERATOR   '/'

char *
copystring(const char *s)
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
#ifdef WIN32
        FILETIME ft;
        uint64_t hundred_ns;
        GetSystemTimeAsFileTime(&ft);
        hundred_ns = (((uint64_t)ft.dwHighDateTime) << 32) + ((uint64_t)ft.dwLowDateTime);
        return (double)hundred_ns / 10000000.0;
#else
        struct timeval tv;

        gettimeofday(&tv, NULL);

        return (double)tv.tv_sec + (tv.tv_usec / 1000000.0);
#endif
}


/*
=============================================================================

                                MISC FUNCTIONS

=============================================================================
*/


void
DefaultExtension(char *path, const char *extension)
{
    char *src;

//
// if path doesn't have a .EXT, append extension
// (extension should include the .)
//
    src = path + strlen(path) - 1;

    while (*src != PATHSEPERATOR && src != path) {
        if (*src == '.')
            return;             // it has an extension
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
        if (path[length] == PATHSEPERATOR)
            return;             // no extension
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
    return path[0] == PATHSEPERATOR || (isalpha(path[0]) && path[1] == ':');
}

int
Q_strncasecmp(const char *s1, const char *s2, int n)
{
        int c1, c2;

        while (1) {
                c1 = *s1++;
                c2 = *s2++;

                if (!n--)
                        return 0;               /* strings are equal until end point */

                if (c1 != c2) {
                        if (c1 >= 'a' && c1 <= 'z')
                                c1 -= ('a' - 'A');
                        if (c2 >= 'a' && c2 <= 'z')
                                c2 -= ('a' - 'A');
                        if (c1 != c2)
                                return -1;      /* strings not equal */
                }
                if (!c1)
                        return 0;               /* strings are equal */
        }

        return -1;
}

int
Q_strcasecmp(const char *s1, const char *s2)
{
        return Q_strncasecmp(s1, s2, 99999);
}
