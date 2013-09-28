/*  Copyright (C) 1996-1997  Id Software, Inc.

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

#ifndef __COMMON_BSPFILE_H__
#define __COMMON_BSPFILE_H__

#include <stdint.h>

#include <common/cmdlib.h>
#include <common/log.h>

/* upper design bounds */

#define MAX_MAP_HULLS              4
#define MAX_MAP_MODELS           256
#define MAX_MAP_BRUSHES         4096
#define MAX_MAP_PLANES         16384
#define MAX_MAP_NODES          32767	/* negative shorts are contents */
#define MAX_MAP_CLIPNODES      65520	/* = 0xfff0; larger are contents */
#define MAX_MAP_LEAFS          32767	/* BSP file format limitation */
#define MAX_MAP_VERTS          65535
#define MAX_MAP_FACES          65535
#define MAX_MAP_MARKSURFACES   65535
#define MAX_MAP_TEXINFO         8192
#define MAX_MAP_EDGES         256000
#define MAX_MAP_SURFEDGES     512000
#define MAX_MAP_MIPTEX     0x0800000
#define MAX_MAP_LIGHTING   0x1000000
#define MAX_MAP_VISIBILITY 0x1000000

/* key / value pair sizes */
#define MAX_ENT_KEY   32
#define MAX_ENT_VALUE 1024

#define BSPVERSION     29
#define BSP2RMQVERSION (('B' << 24) | ('S' << 16) | ('P' << 8) | '2')
#define BSP2VERSION    ('B' | ('S' << 8) | ('P' << 16) | ('2' << 24))

typedef struct {
    int32_t fileofs;
    int32_t filelen;
} lump_t;

#define LUMP_ENTITIES      0
#define LUMP_PLANES        1
#define LUMP_TEXTURES      2
#define LUMP_VERTEXES      3
#define LUMP_VISIBILITY    4
#define LUMP_NODES         5
#define LUMP_TEXINFO       6
#define LUMP_FACES         7
#define LUMP_LIGHTING      8
#define LUMP_CLIPNODES     9
#define LUMP_LEAFS        10
#define LUMP_MARKSURFACES 11
#define LUMP_EDGES        12
#define LUMP_SURFEDGES    13
#define LUMP_MODELS       14

#define BSP_LUMPS         15

typedef struct {
    const char *name;
    size_t size;
} lumpspec_t;

extern const lumpspec_t lumpspec_bsp29[BSP_LUMPS];
extern const lumpspec_t lumpspec_bsp2rmq[BSP_LUMPS];
extern const lumpspec_t lumpspec_bsp2[BSP_LUMPS];

typedef struct {
    float mins[3];
    float maxs[3];
    float origin[3];
    int32_t headnode[MAX_MAP_HULLS];
    int32_t visleafs;		/* not including the solid leaf 0 */
    int32_t firstface;
    int32_t numfaces;
} dmodel_t;

typedef struct {
    int32_t nummiptex;
    int32_t dataofs[4];		/* [nummiptex] */
} dmiptexlump_t;

#define MIPLEVELS 4
typedef struct miptex_s {
    char name[16];
    uint32_t width, height;
    uint32_t offsets[MIPLEVELS];	/* four mip maps stored */
} miptex_t;

typedef struct {
    float point[3];
} dvertex_t;


/* 0-2 are axial planes */
#define PLANE_X    0
#define PLANE_Y    1
#define PLANE_Z    2

/* 3-5 are non-axial planes snapped to the nearest */
#define PLANE_ANYX 3
#define PLANE_ANYY 4
#define PLANE_ANYZ 5

typedef struct {
    float normal[3];
    float dist;
    int32_t type;
} dplane_t;

#define CONTENTS_EMPTY -1
#define CONTENTS_SOLID -2
#define CONTENTS_WATER -3
#define CONTENTS_SLIME -4
#define CONTENTS_LAVA  -5
#define CONTENTS_SKY   -6

typedef struct {
    int32_t planenum;
    int16_t children[2];	/* negative numbers are -(leafs+1), not nodes */
    int16_t mins[3];		/* for sphere culling */
    int16_t maxs[3];
    uint16_t firstface;
    uint16_t numfaces;		/* counting both sides */
} bsp29_dnode_t;

typedef struct {
    int32_t planenum;
    int32_t children[2];	/* negative numbers are -(leafs+1), not nodes */
    int16_t mins[3];		/* for sphere culling */
    int16_t maxs[3];
    uint32_t firstface;
    uint32_t numfaces;		/* counting both sides */
} bsp2rmq_dnode_t;

typedef struct {
    int32_t planenum;
    int32_t children[2];	/* negative numbers are -(leafs+1), not nodes */
    float mins[3];		/* for sphere culling */
    float maxs[3];
    uint32_t firstface;
    uint32_t numfaces;		/* counting both sides */
} bsp2_dnode_t;

/*
 * Note that children are interpreted as unsigned values now, so that we can
 * handle > 32k clipnodes. Values > 0xFFF0 can be assumed to be CONTENTS
 * values and can be read as the signed value to be compatible with the above
 * (i.e. simply subtract 65536).
 */
typedef struct {
    int32_t planenum;
    int16_t children[2];	/* negative numbers are contents */
} bsp29_dclipnode_t;

typedef struct {
    int32_t planenum;
    int32_t children[2];	/* negative numbers are contents */
} bsp2_dclipnode_t;

typedef struct texinfo_s {
    float vecs[2][4];		/* [s/t][xyz offset] */
    int32_t miptex;
    int32_t flags;
} texinfo_t;

#define TEX_SPECIAL 1		/* sky or slime, no lightmap or 256 subdivision */

/*
 * Note that edge 0 is never used, because negative edge nums are used for
 * counterclockwise use of the edge in a face
 */
typedef struct {
    uint16_t v[2];		/* vertex numbers */
} bsp29_dedge_t;

typedef struct {
    uint32_t v[2];		/* vertex numbers */
} bsp2_dedge_t;

#define MAXLIGHTMAPS 4
typedef struct {
    int16_t planenum;
    int16_t side;
    int32_t firstedge;		/* we must support > 64k edges */
    int16_t numedges;
    int16_t texinfo;

    /* lighting info */
    uint8_t styles[MAXLIGHTMAPS];
    int32_t lightofs;		/* start of [numstyles*surfsize] samples */
} bsp29_dface_t;

typedef struct {
    int32_t planenum;
    int32_t side;
    int32_t firstedge;		/* we must support > 64k edges */
    int32_t numedges;
    int32_t texinfo;

    /* lighting info */
    uint8_t styles[MAXLIGHTMAPS];
    int32_t lightofs;		/* start of [numstyles*surfsize] samples */
} bsp2_dface_t;

/* Ambient Sounds */
#define AMBIENT_WATER   0
#define AMBIENT_SKY     1
#define AMBIENT_SLIME   2
#define AMBIENT_LAVA    3
#define NUM_AMBIENTS    4

/*
 * leaf 0 is the generic CONTENTS_SOLID leaf, used for all solid areas
 * all other leafs need visibility info
 */
typedef struct {
    int32_t contents;
    int32_t visofs;		/* -1 = no visibility info */
    int16_t mins[3];		/* for frustum culling     */
    int16_t maxs[3];
    uint16_t firstmarksurface;
    uint16_t nummarksurfaces;
    uint8_t ambient_level[NUM_AMBIENTS];
} bsp29_dleaf_t;

typedef struct {
    int32_t contents;
    int32_t visofs;		/* -1 = no visibility info */
    int16_t mins[3];		/* for frustum culling     */
    int16_t maxs[3];
    uint32_t firstmarksurface;
    uint32_t nummarksurfaces;
    uint8_t ambient_level[NUM_AMBIENTS];
} bsp2rmq_dleaf_t;

typedef struct {
    int32_t contents;
    int32_t visofs;		/* -1 = no visibility info */
    float mins[3];		/* for frustum culling     */
    float maxs[3];
    uint32_t firstmarksurface;
    uint32_t nummarksurfaces;
    uint8_t ambient_level[NUM_AMBIENTS];
} bsp2_dleaf_t;

typedef union {
    byte *base;
    dmiptexlump_t *header;
} dtexdata_t;

/* ========================================================================= */

typedef struct {
    int nummodels;
    dmodel_t *dmodels;

    int visdatasize;
    byte *dvisdata;

    int lightdatasize;
    byte *dlightdata;

    int texdatasize;
    dtexdata_t dtexdata;

    int entdatasize;
    char *dentdata;

    int numleafs;
    bsp29_dleaf_t *dleafs;

    int numplanes;
    dplane_t *dplanes;

    int numvertexes;
    dvertex_t *dvertexes;

    int numnodes;
    bsp29_dnode_t *dnodes;

    int numtexinfo;
    texinfo_t *texinfo;

    int numfaces;
    bsp29_dface_t *dfaces;

    int numclipnodes;
    bsp29_dclipnode_t *dclipnodes;

    int numedges;
    bsp29_dedge_t *dedges;

    int nummarksurfaces;
    uint16_t *dmarksurfaces;

    int numsurfedges;
    int32_t *dsurfedges;
} bsp29_t;

typedef struct {
    int nummodels;
    dmodel_t *dmodels;

    int visdatasize;
    byte *dvisdata;

    int lightdatasize;
    byte *dlightdata;

    int texdatasize;
    dtexdata_t dtexdata;

    int entdatasize;
    char *dentdata;

    int numleafs;
    bsp2rmq_dleaf_t *dleafs;

    int numplanes;
    dplane_t *dplanes;

    int numvertexes;
    dvertex_t *dvertexes;

    int numnodes;
    bsp2rmq_dnode_t *dnodes;

    int numtexinfo;
    texinfo_t *texinfo;

    int numfaces;
    bsp2_dface_t *dfaces;

    int numclipnodes;
    bsp2_dclipnode_t *dclipnodes;

    int numedges;
    bsp2_dedge_t *dedges;

    int nummarksurfaces;
    uint32_t *dmarksurfaces;

    int numsurfedges;
    int32_t *dsurfedges;
} bsp2rmq_t;

typedef struct {
    int nummodels;
    dmodel_t *dmodels;

    int visdatasize;
    byte *dvisdata;

    int lightdatasize;
    byte *dlightdata;

    int texdatasize;
    dtexdata_t dtexdata;

    int entdatasize;
    char *dentdata;

    int numleafs;
    bsp2_dleaf_t *dleafs;

    int numplanes;
    dplane_t *dplanes;

    int numvertexes;
    dvertex_t *dvertexes;

    int numnodes;
    bsp2_dnode_t *dnodes;

    int numtexinfo;
    texinfo_t *texinfo;

    int numfaces;
    bsp2_dface_t *dfaces;

    int numclipnodes;
    bsp2_dclipnode_t *dclipnodes;

    int numedges;
    bsp2_dedge_t *dedges;

    int nummarksurfaces;
    uint32_t *dmarksurfaces;

    int numsurfedges;
    int32_t *dsurfedges;
} bsp2_t;

typedef struct {
    int32_t version;
    lump_t lumps[BSP_LUMPS];
} dheader_t;

typedef struct {
    int32_t version;
    union {
	bsp29_t bsp29;
	bsp2rmq_t bsp2rmq;
	bsp2_t bsp2;
    } data;
} bspdata_t;

void LoadBSPFile(const char *filename, bspdata_t *bsp);
void WriteBSPFile(const char *filename, bspdata_t *bsp);
void PrintBSPFileSizes(const bspdata_t *bsp);
void ConvertBSPFormat(int32_t version, bspdata_t *bspdata);

#endif /* __COMMON_BSPFILE_H__ */
