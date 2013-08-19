/*
    Copyright (C) 1996-1997  Id Software, Inc.

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

#ifndef __BSPFILE_H__
#define __BSPFILE_H__

/* FIXME - put this typedef elsewhere */
typedef unsigned char byte;

#define BSPVERSION 29

#define MAXLIGHTMAPS 4

/* Ambient sounds */
#define AMBIENT_WATER   0
#define AMBIENT_SKY     1
#define AMBIENT_SLIME   2
#define AMBIENT_LAVA    3
#define NUM_AMBIENTS    4

#define BSP_LUMPS 15
typedef struct {
    int fileofs, filelen;
} lump_t;

typedef struct {
    float mins[3], maxs[3];
    float origin[3];
    int headnode[4];		// 4 for backward compat, only 3 hulls exist
    int visleafs;		// not including the solid leaf 0
    int firstface, numfaces;
} dmodel_t;

typedef struct {
    int version;
    lump_t lumps[BSP_LUMPS];
} dheader_t;

typedef struct {
    int nummiptex;
    int dataofs[];		/* [nummiptex] */
} dmiptexlump_t;

typedef char miptex_t[16];

typedef struct {
    float point[3];
} dvertex_t;


typedef struct {
    float normal[3];
    float dist;
    int type;
} dplane_t;

// !!! if this is changed, it must be changed in asm_i386.h too !!!
typedef struct {
    int planenum;
    short children[2];		// negative numbers are -(leafs+1), not nodes
    short mins[3];		// for sphere culling
    short maxs[3];
    unsigned short firstface;
    unsigned short numfaces;	// counting both sides
} dnode_t;

typedef struct {
    int planenum;
    uint16_t children[2];		// negative numbers are contents
} dclipnode_t;


typedef struct texinfo_s {
    float vecs[2][4];		// [s/t][xyz offset]
    int miptex;
    int flags;
} texinfo_t;

// note that edge 0 is never used, because negative edge nums are used for
// counterclockwise use of the edge in a face
typedef struct {
    unsigned short v[2];	// vertex numbers
} dedge_t;

typedef struct {
    short planenum;
    short side;

    int firstedge;		// we must support > 64k edges
    short numedges;
    short texinfo;

    // lighting info
    byte styles[MAXLIGHTMAPS];
    int lightofs;		// start of [numstyles*surfsize] samples
} dface_t;

// leaf 0 is the generic CONTENTS_SOLID leaf, used for all solid areas
// all other leafs need visibility info
typedef struct {
    int contents;
    int visofs;			// -1 = no visibility info

    short mins[3];		// for frustum culling
    short maxs[3];

    unsigned short firstmarksurface;
    unsigned short nummarksurfaces;

    byte ambient_level[NUM_AMBIENTS];
} dleaf_t;

void LoadBSPFile(void);
void WriteBSPFile(void);
void PrintBSPFileSizes(void);

#endif /* __BSPFILE_H__ */
