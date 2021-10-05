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
#include <array>
#include <tuple>

#include <common/cmdlib.hh>
#include <common/log.hh>


/* upper design bounds */

#define MAX_MAP_HULLS_Q1              4
#define MAX_MAP_HULLS_H2              8

#define MAX_MAP_MODELS           256
#define MAX_MAP_BRUSHES         4096
#define MAX_MAP_PLANES         16384
#define MAX_MAP_NODES          32767    /* negative shorts are contents */
#define MAX_MAP_CLIPNODES      65520    /* = 0xfff0; larger are contents */
#define MAX_MAP_LEAFS          32767    /* BSP file format limitation */
#define MAX_MAP_VERTS          65535
#define MAX_MAP_FACES          65535
#define MAX_MAP_MARKSURFACES   65535
#define MAX_MAP_TEXINFO         8192
#define MAX_MAP_EDGES         256000
#define MAX_MAP_SURFEDGES     512000
#define MAX_MAP_MIPTEX     0x0800000
#define MAX_MAP_LIGHTING   0x8000000
#define MAX_MAP_VISIBILITY 0x8000000

/* key / value pair sizes */
#define MAX_ENT_KEY   32
#define MAX_ENT_VALUE 1024

#define NO_VERSION      -1

#define BSPVERSION     29
#define BSP2RMQVERSION (('B' << 24) | ('S' << 16) | ('P' << 8) | '2')
#define BSP2VERSION    ('B' | ('S' << 8) | ('P' << 16) | ('2' << 24))
#define BSPHLVERSION   30 //24bit lighting, and private palettes in the textures lump.
#define Q2_BSPIDENT    (('P'<<24)+('S'<<16)+('B'<<8)+'I')
#define Q2_BSPVERSION  38
#define Q2_QBISMIDENT  (('P'<<24)+('S'<<16)+('B'<<8)+'Q')

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

#define Q2_LUMP_ENTITIES    0
#define Q2_LUMP_PLANES      1
#define Q2_LUMP_VERTEXES    2
#define Q2_LUMP_VISIBILITY  3
#define Q2_LUMP_NODES       4
#define Q2_LUMP_TEXINFO     5
#define Q2_LUMP_FACES       6
#define Q2_LUMP_LIGHTING    7
#define Q2_LUMP_LEAFS       8
#define Q2_LUMP_LEAFFACES   9
#define Q2_LUMP_LEAFBRUSHES 10
#define Q2_LUMP_EDGES       11
#define Q2_LUMP_SURFEDGES   12
#define Q2_LUMP_MODELS      13
#define Q2_LUMP_BRUSHES     14
#define Q2_LUMP_BRUSHSIDES  15
#define Q2_LUMP_POP         16
#define Q2_LUMP_AREAS       17
#define Q2_LUMP_AREAPORTALS 18
#define Q2_HEADER_LUMPS     19

typedef struct {
        char id[4]; //'BSPX'
        uint32_t numlumps;
} bspx_header_t;
typedef struct {
        char lumpname[24];
        uint32_t fileofs;
        uint32_t filelen;
} bspx_lump_t;

typedef struct {
    const char *name;
    size_t size;
} lumpspec_t;

extern const lumpspec_t lumpspec_bsp29[BSP_LUMPS];
extern const lumpspec_t lumpspec_bsp2rmq[BSP_LUMPS];
extern const lumpspec_t lumpspec_bsp2[BSP_LUMPS];
extern const lumpspec_t lumpspec_q2bsp[Q2_HEADER_LUMPS];

typedef struct {
    float mins[3];
    float maxs[3];
    float origin[3];
    int32_t headnode[MAX_MAP_HULLS_Q1]; /* 4 for backward compat, only 3 hulls exist */
    int32_t visleafs;           /* not including the solid leaf 0 */
    int32_t firstface;
    int32_t numfaces;
} dmodelq1_t;

typedef struct {
    float mins[3];
    float maxs[3];
    float origin[3];
    int32_t headnode[MAX_MAP_HULLS_H2]; /* hexen2 only uses 6 */
    int32_t visleafs;           /* not including the solid leaf 0 */
    int32_t firstface;
    int32_t numfaces;
} dmodelh2_t;

typedef struct {
    float mins[3];
    float maxs[3];
    float origin[3];         // for sounds or lights
    int32_t headnode;
    int32_t firstface;
    int32_t numfaces;    // submodels just draw faces
                         // without walking the bsp tree
} q2_dmodel_t;

// FIXME: remove
typedef dmodelh2_t dmodel_t;

typedef struct {
    int32_t nummiptex;
    int32_t dataofs[4];         /* [nummiptex] */
} dmiptexlump_t;

#define MIPLEVELS 4
typedef struct miptex_s {
    char name[16];
    uint32_t width, height;
    uint32_t offsets[MIPLEVELS];        /* four mip maps stored */
} miptex_t;

//mxd. Used to store RGBA data in mbsp->drgbatexdata
typedef struct {
    char name[32]; // Same as in Q2
    unsigned width, height;
    unsigned offset; // Offset to RGBA texture pixels
} rgba_miptex_t;

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

// Q1 contents

#define CONTENTS_EMPTY -1
#define CONTENTS_SOLID -2
#define CONTENTS_WATER -3
#define CONTENTS_SLIME -4
#define CONTENTS_LAVA  -5
#define CONTENTS_SKY   -6
#define CONTENTS_MIN   CONTENTS_SKY

//#define CONTENTS_HINT   -7      /* compiler internal use only */
//#define CONTENTS_CLIP   -8      /* compiler internal use only */
//#define CONTENTS_ORIGIN -9      /* compiler internal use only */
//#define CONTENTS_DETAIL -10     /* compiler internal use only */
//#define CONTENTS_DETAIL_ILLUSIONARY -11 /* compiler internal use only */
//#define CONTENTS_DETAIL_FENCE        -12   /* compiler internal use only */
//#define CONTENTS_ILLUSIONARY_VISBLOCKER -13
//#define CONTENTS_FENCE  -15     /* compiler internal use only */
//#define CONTENTS_LADDER -16     /* reserved for engine use */

// Q2 contents (from qfiles.h)

// contents flags are seperate bits
// a given brush can contribute multiple content bits
// multiple brushes can be in a single leaf

// these definitions also need to be in q_shared.h!

// lower bits are stronger, and will eat weaker brushes completely
#define    Q2_CONTENTS_SOLID           1        // an eye is never valid in a solid
#define    Q2_CONTENTS_WINDOW          2        // translucent, but not watery
#define    Q2_CONTENTS_AUX             4
#define    Q2_CONTENTS_LAVA            8
#define    Q2_CONTENTS_SLIME           16
#define    Q2_CONTENTS_WATER           32
#define    Q2_CONTENTS_MIST            64
#define    Q2_LAST_VISIBLE_CONTENTS    64

#define    Q2_CONTENTS_LIQUID   (Q2_CONTENTS_LAVA|Q2_CONTENTS_SLIME|Q2_CONTENTS_WATER) //mxd

// remaining contents are non-visible, and don't eat brushes

#define    Q2_CONTENTS_AREAPORTAL     0x8000

#define    Q2_CONTENTS_PLAYERCLIP     0x10000
#define    Q2_CONTENTS_MONSTERCLIP    0x20000

// currents can be added to any other contents, and may be mixed
#define    Q2_CONTENTS_CURRENT_0      0x40000
#define    Q2_CONTENTS_CURRENT_90     0x80000
#define    Q2_CONTENTS_CURRENT_180    0x100000
#define    Q2_CONTENTS_CURRENT_270    0x200000
#define    Q2_CONTENTS_CURRENT_UP     0x400000
#define    Q2_CONTENTS_CURRENT_DOWN   0x800000

#define    Q2_CONTENTS_ORIGIN         0x1000000    // removed before bsping an entity

#define    Q2_CONTENTS_MONSTER        0x2000000    // should never be on a brush, only in game
#define    Q2_CONTENTS_DEADMONSTER    0x4000000
#define    Q2_CONTENTS_DETAIL         0x8000000    // brushes to be added after vis leafs
#define    Q2_CONTENTS_TRANSLUCENT    0x10000000    // auto set if any surface has trans
#define    Q2_CONTENTS_LADDER         0x20000000

// Special contents flags for the compiler only
#define    CFLAGS_STRUCTURAL_COVERED_BY_DETAIL (1 << 0)
#define    CFLAGS_WAS_ILLUSIONARY              (1 << 1) /* was illusionary, got changed to something else */
#define    CFLAGS_BMODEL_MIRROR_INSIDE		   (1 << 3) /* set "_mirrorinside" "1" on a bmodel to mirror faces for when the player is inside. */
#define    CFLAGS_NO_CLIPPING_SAME_TYPE        (1 << 4) /* Don't clip the same content type. mostly intended for CONTENTS_DETAIL_ILLUSIONARY */
// only one of these flags below should ever be set.
#define    CFLAGS_HINT                         (1 << 5)
#define    CFLAGS_CLIP                         (1 << 6)
#define    CFLAGS_ORIGIN                       (1 << 7)
#define    CFLAGS_DETAIL                       (1 << 8)
#define    CFLAGS_DETAIL_ILLUSIONARY           (1 << 9)
#define    CFLAGS_DETAIL_FENCE                 (1 << 10)
#define    CFLAGS_ILLUSIONARY_VISBLOCKER       (1 << 11)
// all of the detail values
#define    CFLAGS_DETAIL_MASK                  (CFLAGS_DETAIL | CFLAGS_DETAIL_ILLUSIONARY | CFLAGS_DETAIL_FENCE)
// all of the special content types
#define    CFLAGS_CONTENTS_MASK                (CFLAGS_HINT | CFLAGS_CLIP | CFLAGS_ORIGIN | CFLAGS_DETAIL_MASK | CFLAGS_ILLUSIONARY_VISBLOCKER)

struct gamedef_t;

struct contentflags_t {
    // native flags value; what's written to the BSP basically
    int32_t native;

    // extra flags, specific to BSP only
    int32_t extended;

    constexpr bool operator==(const contentflags_t &other) const {
        return native == other.native && extended == other.extended;
    }

    constexpr bool operator!=(const contentflags_t &other) const {
        return !(*this == other);
    }

    // check if these contents are marked as any (or a specific kind of) detail brush.
    constexpr bool is_detail(int32_t types = CFLAGS_DETAIL_MASK) const {
        return (extended & CFLAGS_DETAIL_MASK) & types;
    }

    bool is_empty(const gamedef_t *game) const;

    // solid, not detail or any other extended content types
    bool is_solid(const gamedef_t *game) const;
    bool is_sky(const gamedef_t *game) const;
    bool is_liquid(const gamedef_t *game) const;
    bool is_valid(const gamedef_t *game, bool strict = true) const;
    
    constexpr bool is_hint() const {
        return extended & CFLAGS_HINT;
    }

    constexpr bool is_clip() const {
        return extended & CFLAGS_CLIP;
    }

    constexpr bool is_origin() const {
        return extended & CFLAGS_ORIGIN;
    }

    constexpr bool clips_same_type() const {
        return !(extended & CFLAGS_NO_CLIPPING_SAME_TYPE);
    }

    constexpr bool is_fence() const {
        return (extended & (CFLAGS_DETAIL_FENCE | CFLAGS_DETAIL_ILLUSIONARY)) != 0;
    }

    // check if this content's `type` - which is distinct from various
    // flags that turn things on/off - match. Exactly what the native
    // "type" is depends on the game, but any of the detail flags must
    // also match.
    bool types_equal(const contentflags_t &other, const gamedef_t *game) const;

    int32_t priority(const gamedef_t *game) const;

    std::string to_string(const gamedef_t *game) const;
};

struct bsp29_dnode_t {
    int32_t planenum;
    int16_t children[2];        /* negative numbers are -(leafs+1), not nodes. children[0] is front, children[1] is back */
    int16_t mins[3];            /* for sphere culling */
    int16_t maxs[3];
    uint16_t firstface;
    uint16_t numfaces;          /* counting both sides */
};

struct bsp2rmq_dnode_t {
    int32_t planenum;
    int32_t children[2];        /* negative numbers are -(leafs+1), not nodes */
    int16_t mins[3];            /* for sphere culling */
    int16_t maxs[3];
    uint32_t firstface;
    uint32_t numfaces;          /* counting both sides */
};

struct bsp2_dnode_t {
    int32_t planenum;
    int32_t children[2];        /* negative numbers are -(leafs+1), not nodes */
    float mins[3];              /* for sphere culling */
    float maxs[3];
    uint32_t firstface;
    uint32_t numfaces;          /* counting both sides */
};

struct q2_dnode_t {
    int32_t planenum;
    int32_t children[2];    // negative numbers are -(leafs+1), not nodes
    int16_t mins[3];      // for frustom culling
    int16_t maxs[3];
    uint16_t firstface;
    uint16_t numfaces;    // counting both sides
};

using q2_dnode_qbism_t = bsp2_dnode_t;

/*
 * Note that children are interpreted as unsigned values now, so that we can
 * handle > 32k clipnodes. Values > 0xFFF0 can be assumed to be CONTENTS
 * values and can be read as the signed value to be compatible with the above
 * (i.e. simply subtract 65536).
 */
typedef struct {
    int32_t planenum;
    int16_t children[2];        /* negative numbers are contents */
} bsp29_dclipnode_t;

typedef struct {
    int32_t planenum;
    int32_t children[2];        /* negative numbers are contents */
} bsp2_dclipnode_t;

typedef struct texinfo_s {
    float vecs[2][4];           /* [s/t][xyz offset] */
    int32_t miptex;
    int32_t flags;
} texinfo_t;

typedef struct {
    float vecs[2][4];     // [s/t][xyz offset]
    int32_t flags;            // miptex flags + overrides
    int32_t value;            // light emission, etc
    char texture[32];     // texture name (textures/*.wal)
    int32_t nexttexinfo;      // for animations, -1 = end of chain
} q2_texinfo_t;

// Q1 Texture flags.
#define    TEX_SPECIAL 1           /* sky or slime, no lightmap or 256 subdivision */

// Q2 Texture flags.
#define    Q2_SURF_LIGHT      0x1        // value will hold the light strength

#define    Q2_SURF_SLICK      0x2        // effects game physics

#define    Q2_SURF_SKY        0x4        // don't draw, but add to skybox
#define    Q2_SURF_WARP       0x8        // turbulent water warp
#define    Q2_SURF_TRANS33    0x10
#define    Q2_SURF_TRANS66    0x20
#define    Q2_SURF_FLOWING    0x40    // scroll towards angle
#define    Q2_SURF_NODRAW     0x80    // don't bother referencing the texture

#define    Q2_SURF_HINT       0x100    // make a primary bsp splitter
#define    Q2_SURF_SKIP       0x200    // completely ignore, allowing non-closed brushes

#define    Q2_SURF_TRANSLUCENT (Q2_SURF_TRANS33|Q2_SURF_TRANS66) //mxd

// QBSP/LIGHT flags
#define TEX_EXFLAG_SKIP    (1U << 0)   /* an invisible surface */
#define TEX_EXFLAG_HINT    (1U << 1)   /* hint surface */
#define TEX_EXFLAG_NODIRT  (1U << 2)   /* don't receive dirtmapping */
#define TEX_EXFLAG_NOSHADOW  (1U << 3)   /* don't cast a shadow */
#define TEX_EXFLAG_NOBOUNCE  (1U << 4)   /* light doesn't bounce off this face */
#define TEX_EXFLAG_NOMINLIGHT (1U << 5)   /* opt out of minlight on this face */
#define TEX_EXFLAG_NOEXPAND  (1U << 6)   /* don't expand this face for larger clip hulls */
#define TEX_EXFLAG_LIGHTIGNORE (1U << 7)  /* PLEASE DOCUMENT ME MOMMY */

struct surfflags_t {
    // native flags value; what's written to the BSP basically
    int32_t native;

    // extra flags, specific to BSP/LIGHT only
    uint8_t extended;

    // if non zero, enables phong shading and gives the angle threshold to use
    uint8_t phong_angle;

    // minlight value for this face, multiplied by 0.5, so we can store overbrights in 8 bits
    // FIXME: skip the compression and just store a float? serialize all of these to a JSON .texinfo
    // for better extensibility?
    uint8_t minlight;

    // red minlight colors for this face
    // FIXME: this probably makes it illegal to memcpy() from a surfflags_t, which is done in
    // WriteExtendedTexinfoFlags. Again, points to switching to JSON serialization.
    std::array<uint8_t, 3> minlight_color;
    
    // if non zero, overrides _phong_angle for concave joints
    uint8_t phong_angle_concave;

    // custom opacity
    uint8_t light_alpha;

    constexpr bool needs_write() const {
        return (extended & ~(TEX_EXFLAG_SKIP | TEX_EXFLAG_HINT)) || phong_angle || minlight || minlight_color[0] ||
            minlight_color[1] || minlight_color[2] || phong_angle_concave ||
            light_alpha;
    }

    constexpr auto as_tuple() const {
        return std::tie(native, extended, phong_angle, minlight, minlight_color, phong_angle_concave, light_alpha);
    }

    constexpr bool operator<(const surfflags_t &other) const {
        return as_tuple() < other.as_tuple();
    }

    constexpr bool operator>(const surfflags_t &other) const {
        return as_tuple() > other.as_tuple();
    }
};

// header before tightly packed surfflags_t[num_texinfo]
struct extended_flags_header_t {
    uint32_t    num_texinfo;
    uint32_t    surfflags_size; // sizeof(surfflags_t)
};

typedef struct {
    float vecs[2][4];     // [s/t][xyz offset]
    surfflags_t flags;    // native miptex flags + extended flags
    
    // q1 only
    int32_t miptex;
    
    // q2 only
    int32_t value;            // light emission, etc
    char texture[32];     // texture name (textures/*.wal)
    int32_t nexttexinfo;      // for animations, -1 = end of chain
} gtexinfo_t;

/*
 * Note that edge 0 is never used, because negative edge nums are used for
 * counterclockwise use of the edge in a face
 */
typedef struct {
    uint16_t v[2];              /* vertex numbers */
} bsp29_dedge_t;

typedef struct {
    uint32_t v[2];              /* vertex numbers */
} bsp2_dedge_t;

using q2_dedge_qbism_t = bsp2_dedge_t;

#define MAXLIGHTMAPS 4
typedef struct {
    int16_t planenum;
    int16_t side;
    int32_t firstedge;          /* we must support > 64k edges */
    int16_t numedges;
    int16_t texinfo;

    /* lighting info */
    uint8_t styles[MAXLIGHTMAPS];
    int32_t lightofs;           /* start of [numstyles*surfsize] samples */
} bsp29_dface_t;

typedef struct {
    int32_t planenum;
    int32_t side;               // if true, the face is on the back side of the plane
    int32_t firstedge;          /* we must support > 64k edges */
    int32_t numedges;
    int32_t texinfo;

    /* lighting info */
    uint8_t styles[MAXLIGHTMAPS];
    int32_t lightofs;           /* start of [numstyles*surfsize] samples */
} bsp2_dface_t;

typedef struct {
    uint16_t planenum;		  // NOTE: only difference from bsp29_dface_t
    int16_t side;
    int32_t firstedge;        // we must support > 64k edges
    int16_t numedges;
    int16_t texinfo;
    
    // lighting info
    uint8_t styles[MAXLIGHTMAPS];
    int32_t lightofs;        // start of [numstyles*surfsize] samples
} q2_dface_t;

typedef struct {
    uint32_t planenum;		  // NOTE: only difference from bsp2_dface_t
    int32_t side;
    int32_t firstedge;        // we must support > 64k edges
    int32_t numedges;
    int32_t texinfo;
    
    // lighting info
    uint8_t styles[MAXLIGHTMAPS];
    int32_t lightofs;        // start of [numstyles*surfsize] samples
} q2_dface_qbism_t;

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
    int32_t visofs;             /* -1 = no visibility info */
    int16_t mins[3];            /* for frustum culling     */
    int16_t maxs[3];
    uint16_t firstmarksurface;
    uint16_t nummarksurfaces;
    uint8_t ambient_level[NUM_AMBIENTS];
} bsp29_dleaf_t;

typedef struct {
    int32_t contents;
    int32_t visofs;             /* -1 = no visibility info */
    int16_t mins[3];            /* for frustum culling     */
    int16_t maxs[3];
    uint32_t firstmarksurface;
    uint32_t nummarksurfaces;
    uint8_t ambient_level[NUM_AMBIENTS];
} bsp2rmq_dleaf_t;

typedef struct {
    int32_t contents;
    int32_t visofs;             /* -1 = no visibility info */
    float mins[3];              /* for frustum culling     */
    float maxs[3];
    uint32_t firstmarksurface;
    uint32_t nummarksurfaces;
    uint8_t ambient_level[NUM_AMBIENTS];
} bsp2_dleaf_t;

typedef struct {
    int32_t contents;            // OR of all brushes (not needed?)
    
    int16_t cluster;
    int16_t area;
    
    int16_t mins[3];            // for frustum culling
    int16_t maxs[3];
    
    uint16_t firstleafface;
    uint16_t numleaffaces;
    
    uint16_t firstleafbrush;
    uint16_t numleafbrushes;
} q2_dleaf_t;

typedef struct {
    int32_t contents;            // OR of all brushes (not needed?)
    
    int32_t cluster;
    int32_t area;
    
    float mins[3];            // for frustum culling
    float maxs[3];
    
    uint32_t firstleafface;
    uint32_t numleaffaces;
    
    uint32_t firstleafbrush;
    uint32_t numleafbrushes;
} q2_dleaf_qbism_t;

typedef struct {
    // bsp2_dleaf_t
    int32_t contents;
    int32_t visofs;             /* -1 = no visibility info */
    float mins[3];              /* for frustum culling     */
    float maxs[3];
    uint32_t firstmarksurface;
    uint32_t nummarksurfaces;
    uint8_t ambient_level[NUM_AMBIENTS];
    
    // q2 extras
    int32_t cluster;
    int32_t area;
    uint32_t firstleafbrush;
    uint32_t numleafbrushes;
} mleaf_t;

typedef struct {
    uint16_t planenum;        // facing out of the leaf
    int16_t texinfo;
} dbrushside_t;

typedef struct {
    uint32_t planenum;        // facing out of the leaf
    int32_t texinfo;
} q2_dbrushside_qbism_t;

typedef struct {
    int32_t firstside;
    int32_t numsides;
    int32_t contents;
} dbrush_t;

// the visibility lump consists of a header with a count, then
// byte offsets for the PVS and PHS of each cluster, then the raw
// compressed bit vectors
#define    DVIS_PVS    0
#define    DVIS_PHS    1
struct dvis_t {
    int32_t numclusters;
    int32_t bitofs[][2];    // bitofs[numclusters][2]
};

// each area has a list of portals that lead into other areas
// when portals are closed, other areas may not be visible or
// hearable even if the vis info says that it should be
typedef struct {
    int32_t portalnum;
    int32_t otherarea;
} dareaportal_t;

typedef struct {
    int32_t numareaportals;
    int32_t firstareaportal;
} darea_t;

/* ========================================================================= */

typedef struct bspxentry_s
{
    char lumpname[24];
    const uint8_t *lumpdata;
    size_t lumpsize;

    struct bspxentry_s *next;
} bspxentry_t;

// BRUSHLIST BSPX lump

struct bspxbrushes_permodel {
        int32_t ver;
        int32_t modelnum;
        int32_t numbrushes;
        int32_t numfaces;
};
struct bspxbrushes_perbrush {
        float mins[3];
        float maxs[3];
        int16_t contents;
        uint16_t numfaces;
};
struct bspxbrushes_perface {
        float normal[3];
        float dist;
};

/* ========================================================================= */

struct bsp29_t {
    int nummodels;
    dmodelq1_t *dmodels_q;
    dmodelh2_t *dmodels_h2;

    int visdatasize;
    uint8_t *dvisdata;

    int lightdatasize;
    uint8_t *dlightdata;

    int texdatasize;
    dmiptexlump_t *dtexdata;

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
};

struct bsp2rmq_t {
    int nummodels;
    dmodelq1_t *dmodels_q;
    dmodelh2_t *dmodels_h2;

    int visdatasize;
    uint8_t *dvisdata;

    int lightdatasize;
    uint8_t *dlightdata;

    int texdatasize;
    dmiptexlump_t *dtexdata;

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
};

struct bsp2_t {
    int nummodels;
    dmodelq1_t *dmodels_q;
    dmodelh2_t *dmodels_h2;

    int visdatasize;
    uint8_t *dvisdata;

    int lightdatasize;
    uint8_t *dlightdata;

    int texdatasize;
    dmiptexlump_t *dtexdata;

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
};

struct q2bsp_t {
    int nummodels;
    q2_dmodel_t *dmodels;
    
    int visdatasize;
    dvis_t *dvis;
    
    int lightdatasize;
    uint8_t *dlightdata;
    
    int entdatasize;
    char *dentdata;
    
    int numleafs;
    q2_dleaf_t *dleafs;
    
    int numplanes;
    dplane_t *dplanes;
    
    int numvertexes;
    dvertex_t *dvertexes;
    
    int numnodes;
    q2_dnode_t *dnodes;
    
    int numtexinfo;
    q2_texinfo_t *texinfo;
    
    int numfaces;
    q2_dface_t *dfaces;
    
    int numedges;
    bsp29_dedge_t *dedges;
    
    int numleaffaces;
    uint16_t *dleaffaces;
    
    int numleafbrushes;
    uint16_t *dleafbrushes;
    
    int numsurfedges;
    int32_t *dsurfedges;
    
    int numareas;
    darea_t *dareas;
    
    int numareaportals;
    dareaportal_t *dareaportals;
    
    int numbrushes;
    dbrush_t *dbrushes;
    
    int numbrushsides;
    dbrushside_t *dbrushsides;
    
    uint8_t dpop[256];
};

struct q2bsp_qbism_t {
    int nummodels;
    q2_dmodel_t *dmodels;
    
    int visdatasize;
    dvis_t *dvis;
    
    int lightdatasize;
    uint8_t *dlightdata;
    
    int entdatasize;
    char *dentdata;
    
    int numleafs;
    q2_dleaf_qbism_t *dleafs;
    
    int numplanes;
    dplane_t *dplanes;
    
    int numvertexes;
    dvertex_t *dvertexes;
    
    int numnodes;
    q2_dnode_qbism_t *dnodes;
    
    int numtexinfo;
    q2_texinfo_t *texinfo;
    
    int numfaces;
    q2_dface_qbism_t *dfaces;
    
    int numedges;
    q2_dedge_qbism_t *dedges;
    
    int numleaffaces;
    uint32_t *dleaffaces;
    
    int numleafbrushes;
    uint32_t *dleafbrushes;
    
    int numsurfedges;
    int32_t *dsurfedges;
    
    int numareas;
    darea_t *dareas;
    
    int numareaportals;
    dareaportal_t *dareaportals;
    
    int numbrushes;
    dbrush_t *dbrushes;
    
    int numbrushsides;
    q2_dbrushside_qbism_t *dbrushsides;
    
    uint8_t dpop[256];
};

struct bspversion_t;

struct mbsp_t {
    const bspversion_t *loadversion;
    
    int nummodels;
    dmodelh2_t *dmodels;
    
    // FIXME: split this into q1 and q2 members, since the format is different
    int visdatasize;
    uint8_t *dvisdata;
    
    int lightdatasize;
    uint8_t *dlightdata;
    
    int texdatasize;
    dmiptexlump_t *dtexdata;

    int rgbatexdatasize; //mxd
    dmiptexlump_t *drgbatexdata; //mxd. Followed by rgba_miptex_t structs
    
    int entdatasize;
    char *dentdata;
    
    int numleafs;
    mleaf_t *dleafs;
    
    int numplanes;
    dplane_t *dplanes;
    
    int numvertexes;
    dvertex_t *dvertexes;
    
    int numnodes;
    bsp2_dnode_t *dnodes;
    
    int numtexinfo;
    gtexinfo_t *texinfo;
    
    int numfaces;
    bsp2_dface_t *dfaces;
    
    int numclipnodes;
    bsp2_dclipnode_t *dclipnodes;

    int numedges;
    bsp2_dedge_t *dedges;
    
    int numleaffaces;
    uint32_t *dleaffaces;
    
    int numleafbrushes;
    uint32_t *dleafbrushes;
    
    int numsurfedges;
    int32_t *dsurfedges;
    
    int numareas;
    darea_t *dareas;
    
    int numareaportals;
    dareaportal_t *dareaportals;
    
    int numbrushes;
    dbrush_t *dbrushes;
    
    int numbrushsides;
    q2_dbrushside_qbism_t *dbrushsides;
    
    uint8_t dpop[256];
}; // "generic" bsp - superset of all other supported types

typedef struct {
    int32_t version;
    lump_t lumps[BSP_LUMPS];
} dheader_t;

typedef struct {
    int32_t ident;
    int32_t version;
    lump_t lumps[Q2_HEADER_LUMPS];
} q2_dheader_t;

typedef struct {
    const bspversion_t *version, *loadversion;
    
    struct {
        bsp29_t bsp29;
        bsp2rmq_t bsp2rmq;
        bsp2_t bsp2;
        q2bsp_t q2bsp;
        mbsp_t mbsp;
        q2bsp_qbism_t q2bsp_qbism;
    } data;

    bspxentry_t *bspxentries;
} bspdata_t;

// native game target ID
enum gameid_t {
    GAME_UNKNOWN,
    GAME_QUAKE,
    GAME_HEXEN_II,
    GAME_HALF_LIFE,
    GAME_QUAKE_II,

    GAME_TOTAL
};

// Game definition, which contains data specific to
// the game a BSP version is being compiled for.
struct gamedef_t
{
    gameid_t    id;

    bool        has_rgb_lightmap;

    const char *base_dir;

    virtual bool surf_is_lightmapped(const surfflags_t &flags) const = 0;
    virtual bool surf_is_subdivided(const surfflags_t &flags) const = 0;
    virtual surfflags_t surf_remap_for_export(const surfflags_t &flags) const = 0;
    virtual contentflags_t cluster_contents(const contentflags_t &contents0, const contentflags_t &contents1) const = 0;
    virtual int32_t get_content_type(const contentflags_t &contents) const = 0;
    virtual int32_t contents_priority(const contentflags_t &contents) const = 0;
    virtual contentflags_t create_extended_contents(const int32_t &cflags = 0) const = 0;
    virtual contentflags_t create_empty_contents(const int32_t &cflags = 0) const = 0;
    virtual contentflags_t create_solid_contents(const int32_t &cflags = 0) const = 0;
    virtual contentflags_t create_sky_contents(const int32_t &cflags = 0) const = 0;
    virtual contentflags_t create_liquid_contents(const int32_t &liquid_type, const int32_t &cflags = 0) const = 0;
    virtual bool contents_are_empty(const contentflags_t &contents) const {
        return contents.native == create_empty_contents().native;
    }
    virtual bool contents_are_solid(const contentflags_t &contents) const {
        return contents.native == create_solid_contents().native;
    }
    virtual bool contents_are_sky(const contentflags_t &contents) const {
        return contents.native == create_sky_contents().native;
    }
    virtual bool contents_are_liquid(const contentflags_t &contents) const = 0;
    virtual bool contents_are_valid(const contentflags_t &contents, bool strict = true) const = 0;
    virtual bool portal_can_see_through(const contentflags_t &contents0, const contentflags_t &contents1) const = 0;
    virtual std::string get_contents_display(const contentflags_t &contents) const = 0;
};

// BSP version struct & instances
struct bspversion_t
{
    /* identifier value, the first int32_t in the header */
    int32_t ident;
    /* version value, if supported; use NO_VERSION if a version is not required */
    int32_t version;
    /* short name used for command line args, etc */
    const char *short_name;
    /* full display name for printing */
    const char *name;
    /* game ptr */
    const gamedef_t *game;
};

extern const bspversion_t bspver_generic;
extern const bspversion_t bspver_q1;
extern const bspversion_t bspver_h2;
extern const bspversion_t bspver_h2bsp2;
extern const bspversion_t bspver_h2bsp2rmq;
extern const bspversion_t bspver_bsp2;
extern const bspversion_t bspver_bsp2rmq;
extern const bspversion_t bspver_hl;
extern const bspversion_t bspver_q2;
extern const bspversion_t bspver_qbism;

/* table of supported versions */
constexpr const bspversion_t *const bspversions[] = {
    &bspver_generic,
    &bspver_q1,
    &bspver_h2,
    &bspver_h2bsp2,
    &bspver_h2bsp2rmq,
    &bspver_bsp2,
    &bspver_bsp2rmq,
    &bspver_hl,
    &bspver_q2,
    &bspver_qbism
};

void LoadBSPFile(char *filename, bspdata_t *bspdata);       //returns the filename as contained inside a bsp
void WriteBSPFile(const char *filename, bspdata_t *bspdata);
void PrintBSPFileSizes(const bspdata_t *bspdata);
/**
 * Returns false if the conversion failed.
 */
bool ConvertBSPFormat(bspdata_t *bspdata, const bspversion_t *to_version);
void BSPX_AddLump(bspdata_t *bspdata, const char *xname, const void *xdata, size_t xsize);
const void *BSPX_GetLump(bspdata_t *bspdata, const char *xname, size_t *xsize);

void
DecompressRow (const uint8_t *in, const int numbytes, uint8_t *decompressed);

int
CompressRow(const uint8_t *vis, const int numbytes, uint8_t *out);

#endif /* __COMMON_BSPFILE_H__ */
