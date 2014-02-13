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
// qbsp.h

#ifndef QBSP_H
#define QBSP_H

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bspfile.h"
#include "file.h"
#include "warnerr.h"

#ifndef offsetof
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif

//===== cmdlib.h

/*
 * Clipnodes need to be stored as a 16-bit offset. Originally, this was a
 * signed value and only the positive values up to 32767 were available. Since
 * the negative range was unused apart from a few values reserved for flags,
 * this has been extended to allow up to 65520 (0xfff0) clipnodes (with a
 * suitably modified engine).
 */
#define MAX_BSP_CLIPNODES 0xfff0

// key / value pair sizes
#define	MAX_KEY		32
#define	MAX_VALUE	1024

// Various other geometry maximums
#define MAX_POINTS_ON_WINDING	96
#define	MAXEDGES		64
#define	MAXPOINTS		60	// don't let a base face get past this
					// because it can be split more later

// For brush.c, normal and +16 (?)
#define	NUM_HULLS	2

// 0-2 are axial planes
// 3-5 are non-axial planes snapped to the nearest
#define	PLANE_X		0
#define	PLANE_Y		1
#define	PLANE_Z		2
#define	PLANE_ANYX	3
#define	PLANE_ANYY	4
#define	PLANE_ANYZ	5

// planenum for a leaf (?)
#define	PLANENUM_LEAF	-1

// Which side of polygon a point is on
#define	SIDE_FRONT	0
#define	SIDE_BACK	1
#define	SIDE_ON		2
#define	SIDE_CROSS	-2

// Pi
#define	Q_PI	3.14159265358979323846

// Possible contents of a leaf node
#define	CONTENTS_EMPTY	-1
#define	CONTENTS_SOLID	-2
#define	CONTENTS_WATER	-3
#define	CONTENTS_SLIME	-4
#define	CONTENTS_LAVA	-5
#define	CONTENTS_SKY	-6
#define CONTENTS_CLIP	-7	/* compiler internal use only */
#define CONTENTS_HINT	-8	/* compiler internal use only */

// Special contents flags for the compiler only
#define CFLAGS_DETAIL	(1U << 0)

// Texture flags
#define	TEX_SPECIAL (1U << 0)	/* sky or liquid (no lightmap or subdivision */
#define	TEX_SKIP    (1U << 1)	/* an invisible surface */
#define TEX_HINT    (1U << 2)	/* hint surface */

/*
 * The quality of the bsp output is highly sensitive to these epsilon values.
 * Notes:
 * - T-junction calculations are sensitive to errors and need the various
 *   epsilons to be such that EQUAL_EPSILON < T_EPSILON < CONTINUOUS_EPSILON.
 *     ( TODO: re-check if CONTINUOUS_EPSILON is still directly related )
 */
#define NORMAL_EPSILON		0.000001
#define ANGLEEPSILON		0.000001
#define DIST_EPSILON		0.0001
#define ZERO_EPSILON		0.0001
#define DISTEPSILON		0.0001
#define POINT_EPSILON		0.0001
#define ON_EPSILON		0.0001
#define EQUAL_EPSILON		0.0001
#define T_EPSILON		0.0002
#define CONTINUOUS_EPSILON	0.0005

#define BOGUS_RANGE	18000

// the exact bounding box of the brushes is expanded some for the headnode
// volume.  is this still needed?
#define	SIDESPACE	24

/*
 * If this enum is changed, make sure to also update MemSize and PrintMem
 */
enum {
    BSP_ENT,
    BSP_PLANE,
    BSP_TEX,
    BSP_VERTEX,
    BSP_VIS,
    BSP_NODE,
    BSP_TEXINFO,
    BSP_FACE,
    BSP_LIGHT,
    BSP_CLIPNODE,
    BSP_LEAF,
    BSP_MARKSURF,
    BSP_EDGE,
    BSP_SURFEDGE,
    BSP_MODEL,

    MAPFACE,
    MAPBRUSH,
    MAPENTITY,
    WINDING,
    FACE,
    PLANE,
    PORTAL,
    SURFACE,
    NODE,
    BRUSH,
    MIPTEX,
    WVERT,
    WEDGE,
    HASHVERT,
    OTHER,
    GLOBAL
};

double I_FloatTime(void);

void DefaultExtension(char *path, const char *extension);
void StripExtension(char *path);
void StripFilename(char *path);
int IsAbsolutePath(const char *path);

char *copystring(const char *s);

//===== mathlib.h

#ifdef DOUBLEVEC_T
typedef double vec_t;
#define VECT_MAX DBL_MAX
#else
typedef float vec_t;
#define VECT_MAX FLT_MAX
#endif
typedef vec_t vec3_t[3];

extern const vec3_t vec3_origin;

bool VectorCompare(const vec3_t v1, const vec3_t v2);

vec_t Q_rint(vec_t in);
extern vec_t DotProduct(const vec3_t v1, const vec3_t v2);
extern void VectorSubtract(const vec3_t va, const vec3_t vb, vec3_t out);
extern void VectorAdd(const vec3_t va, const vec3_t vb, vec3_t out);
extern void VectorCopy(const vec3_t in, vec3_t out);

vec_t VectorLengthSq(const vec3_t v);
vec_t VectorLength(const vec3_t v);

void VectorMA(const vec3_t va, const double scale, const vec3_t vb, vec3_t vc);

void CrossProduct(const vec3_t v1, const vec3_t v2, vec3_t cross);
vec_t VectorNormalize(vec3_t v);
void VectorInverse(vec3_t v);
void VectorScale(const vec3_t v, const vec_t scale, vec3_t out);

#define min(a,b) ({		\
	typeof(a) a_ = (a);	\
	typeof(b) b_ = (b);	\
	(void)(&a_ == &b_);	\
	(a_ < b_) ? a_ : b_;	\
})
#define max(a,b) ({		\
	typeof(a) a_ = (a);	\
	typeof(b) b_ = (b);	\
	(void)(&a_ == &b_);	\
	(a_ > b_) ? a_ : b_;	\
})

#define stringify__(x) #x
#define stringify(x) stringify__(x)

//====== bsp5.h

typedef struct plane {
    vec3_t normal;
    vec_t dist;
    int type;
    struct plane *hash_chain;
} plane_t;


//============================================================================


typedef struct {
    int numpoints;
    vec3_t points[MAXEDGES];		// variable sized
} winding_t;

winding_t *BaseWindingForPlane(const plane_t *p);
void CheckWinding(const winding_t *w);
winding_t *NewWinding(int points);
void FreeWinding(winding_t *w);
winding_t *CopyWinding(const winding_t *w);
winding_t *ClipWinding(winding_t *in, const plane_t *split, bool keepon);
void DivideWinding(winding_t *in, const plane_t *split, winding_t **front,
		   winding_t **back);
void MidpointWinding(const winding_t *w, vec3_t v);

/* Helper function for ClipWinding and it's variants */
void CalcSides(const winding_t *in, const plane_t *split, int *sides,
	       vec_t *dists, int counts[3]);

//============================================================================


typedef struct visfacet_s {
    struct visfacet_s *next;

    int planenum;
    int planeside;		// which side is the front of the face
    int texinfo;
    short contents[2];		// 0 = front side
    short cflags[2];		// contents flags

    struct visfacet_s *original;	// face on node
    int outputnumber;		// only valid for original faces after
				// write surfaces
    vec3_t origin;
    vec_t radius;

    int *edges;
    winding_t w;
} face_t;

typedef struct surface_s {
    struct surface_s *next;
    struct surface_s *original;	// before BSP cuts it up
    int planenum;
    int outputplanenum;		// only valid after WriteSurfacePlanes
    vec3_t mins, maxs;
    bool onnode;		// true if surface has already been used
				//   as a splitting node
    bool detail_separator;	// true if split generated by a detail brush
    face_t *faces;		// links to all faces on either side of the surf
} surface_t;


// there is a node_t structure for every node and leaf in the bsp tree

typedef struct node_s {
    vec3_t mins, maxs;		// bounding volume, not just points inside

    // information for decision nodes
    int planenum;		// -1 = leaf node
    int outputplanenum;		// only valid after WriteNodePlanes
    int firstface;		// decision node only
    int numfaces;		// decision node only
    struct node_s *children[2];	// only valid for decision nodes
    face_t *faces;		// decision nodes only, list for both sides

    // information for leafs
    int contents;		// leaf nodes (0 for decision nodes)
    face_t **markfaces;		// leaf nodes only, point to node faces
    struct portal_s *portals;
    int visleafnum;		// -1 = solid
    int viscluster;		// detail cluster for faster vis
    int fillmark;		// for flood filling
    int occupied;		// entity number in leaf for outside filling
    bool detail_separator;	// for vis portal generation
} node_t;

//=============================================================================

// brush.c

typedef struct brush_s {
    struct brush_s *next;
    vec3_t mins, maxs;
    face_t *faces;
    short contents;		/* BSP contents */
    short cflags;		/* Compiler internal contents flags */
} brush_t;

void FreeBrushes(brush_t *brushlist);

void PlaneHash_Init(void);
int FindPlane(const plane_t *plane, int *side);
int PlaneEqual(const plane_t *p1, const plane_t *p2);
int PlaneInvEqual(const plane_t *p1, const plane_t *p2);

//=============================================================================

// csg4.c

extern int csgmergefaces;

// build surfaces is also used by GatherNodeFaces
surface_t *BuildSurfaces(face_t **planefaces);
face_t *NewFaceFromFace(face_t *in);
void SplitFace(face_t *in, const plane_t *split, face_t **front, face_t **back);
void UpdateFaceSphere(face_t *in);

//=============================================================================

// solidbsp.c

extern int splitnodes;

void DivideFacet(face_t *in, plane_t *split, face_t **front, face_t **back);
void CalcSurfaceInfo(surface_t *surf);
void SubdivideFace(face_t *f, face_t **prevptr);

//=============================================================================

// merge.c

void MergePlaneFaces(surface_t *plane);
face_t *MergeFaceToList(face_t *face, face_t *list);
face_t *FreeMergeListScraps(face_t *merged);
void MergeAll(surface_t *surfhead);

//=============================================================================

// surfaces.c

typedef struct hashvert_s {
    struct hashvert_s *next;
    vec3_t point;
    int num;
    int numedges;
} hashvert_t;

surface_t *GatherNodeFaces(node_t *headnode);

//=============================================================================

// portals.c

typedef struct portal_s {
    int planenum;
    node_t *nodes[2];		// [0] = front side of planenum
    struct portal_s *next[2];
    winding_t *winding;
} portal_t;

extern node_t outside_node;	// portals outside the world face this

void FreeAllPortals(node_t *node);

//=============================================================================

// region.c

void GrowNodeRegions(node_t *headnode);

//=============================================================================

// tjunc.c

typedef struct wvert_s {
    vec_t t;                    /* t-value for parametric equation of edge */
    struct wvert_s *prev, *next; /* t-ordered list of vertices on same edge */
} wvert_t;

typedef struct wedge_s {
    struct wedge_s *next;       /* pointer for hash bucket chain */
    vec3_t dir;                 /* direction vector for the edge */
    vec3_t origin;              /* origin (t = 0) in parametric form */
    wvert_t head;               /* linked list of verticies on this edge */
} wedge_t;

//=============================================================================

// writebsp.c

void ExportNodePlanes(node_t *headnode);

void BeginBSPFile(void);
void FinishBSPFile(void);

//=============================================================================

// outside.c

bool FillOutside(node_t *node, const int hullnum, const int numportals);

//=============================================================================

typedef struct options_s {
    bool fNofill;
    bool fNoclip;
    bool fNoskip;
    bool fOnlyents;
    bool fVerbose;
    bool fAllverbose;
    bool fSplitspecial;
    bool fTranswater;
    bool fTranssky;
    bool fOldaxis;
    bool fBspleak;
    bool fNoverbose;
    bool fOldleak;
    bool fNopercent;
    bool forceGoodTree;
    int BSPVersion;
    int dxSubdivide;
    int dxLeakDist;
    char szMapName[512];
    char szBSPName[512];
    char wadPath[512];
} options_t;

extern options_t options;

//=============================================================================

// map.c

typedef struct epair_s {
    struct epair_s *next;
    char *key;
    char *value;
} epair_t;

typedef struct mapface_s {
    plane_t plane;
    int texinfo;
    int fUnique;
    int linenum;
} mapface_t;

typedef struct mapbrush_s {
    mapface_t *faces;
    int numfaces;
} mapbrush_t;

struct lumpdata {
    int count;
    int index;
    void *data;
};

typedef struct mapentity_s {
    vec3_t origin;
    mapbrush_t *mapbrushes;	/* Array */
    int nummapbrushes;
    epair_t *epairs;
    vec3_t mins, maxs;
    brush_t *brushes;		/* NULL terminated list */
    int numbrushes;
    struct lumpdata lumps[BSP_LUMPS];
} mapentity_t;

typedef struct mapdata_s {
    /* Maximum space available for items */
    int maxfaces;
    int maxbrushes;
    int maxentities;
    int maxplanes;
    int maxmiptex;

    /* Number of items currently used */
    int numfaces;
    int numbrushes;
    int numentities;
    int numplanes;
    int nummiptex;

    /* Arrays of actual items */
    mapface_t *faces;
    mapbrush_t *brushes;
    mapentity_t *entities;
    plane_t *planes;
    miptex_t *miptex;

    /* Totals for BSP data items -> TODO: move to a bspdata struct? */
    int cTotal[BSP_LUMPS];

    /* Misc other global state for the compile process */
    int fillmark;	/* For marking leaves while outside filling */
    bool leakfile;	/* Flag once we've written a leak (.por/.pts) file */
} mapdata_t;

extern mapdata_t map;
extern mapentity_t *pWorldEnt;

void LoadMapFile(void);

int FindMiptex(const char *name);

void PrintEntity(const mapentity_t *entity);
const char *ValueForKey(const mapentity_t *entity, const char *key);
void SetKeyValue(mapentity_t *entity, const char *key, const char *value);
void GetVectorForKey(const mapentity_t *entity, const char *szKey, vec3_t vec);

void WriteEntitiesToString(void);

void FixRotateOrigin(mapentity_t *entity);

/* Create BSP brushes from map brushes in src and save into dst */
void Brush_LoadEntity(mapentity_t *dst, const mapentity_t *src,
		      const int hullnum);

surface_t *CSGFaces(const mapentity_t *entity);
int PortalizeWorld(const mapentity_t *entity, node_t *headnode, const int hullnum);
void TJunc(const mapentity_t *entity, node_t *headnode);
node_t *SolidBSP(const mapentity_t *entity, surface_t *surfhead, bool midsplit);
int MakeFaceEdges(mapentity_t *entity, node_t *headnode);
void ExportClipNodes(mapentity_t *entity, node_t *headnode, const int hullnum);
void ExportDrawNodes(mapentity_t *entity, node_t *headnode, int firstface);

// util.c

#define msgWarning	1
#define msgStat		2
#define msgProgress	3
#define msgLiteral	4
#define msgFile		5
#define msgScreen	6
#define msgPercent	7

extern const char *rgszWarnings[cWarnings];
extern const int *MemSize;
extern const int MemSize_BSP29[GLOBAL + 1];
extern const int MemSize_BSP2rmq[GLOBAL + 1];
extern const int MemSize_BSP2[GLOBAL + 1];

void *AllocMem(int Type, int cSize, bool fZero);
void FreeMem(void *pMem, int Type, int cSize);
void FreeAllMem(void);
void PrintMem(void);

void Message(int MsgType, ...);
void Error(const char *error, ...)
    __attribute__((format(printf,1,2),noreturn));

#endif
