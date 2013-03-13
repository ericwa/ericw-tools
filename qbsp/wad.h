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

#ifndef WAD_H
#define WAD_H

#include <stdbool.h>

typedef struct {
    char identification[4];	// should be WAD2
    int numlumps;
    int infotableofs;
} wadinfo_t;

typedef struct {
    int filepos;
    int disksize;
    int size;			// uncompressed
    char type;
    char compression;
    char pad1, pad2;
    char name[16];		// must be null terminated
} lumpinfo_t;

#define MIPLEVELS 4
typedef struct {
    char name[16];
    uint32_t width, height;
    uint32_t offsets[MIPLEVELS];
} dmiptex_t;

typedef struct wad_s {
    wadinfo_t header;
    int version;
    lumpinfo_t *lumps;
    FILE *file;
    struct wad_s *next;
} wad_t;

wad_t *WADList_Init(const char *wadstring);
void WADList_Process(const wad_t *wadlist);
void WADList_Free(wad_t *wadlist);

#endif /* WAD_H */
