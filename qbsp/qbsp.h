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

#include "warnerr.h"
#include "file.h"

//===== cmdlib.h

// Current BSP version
#define BSPVERSION	29

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
// TODO: fix MAX_FACES??
#define	MAX_FACES		128	// (was 16)
#define MAX_POINTS_ON_WINDING	64
#define	MAXEDGES		32
#define	MAXPOINTS		28	// don't let a base face get past this
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

// flag for textures, sky or slime, no lightmap or 256 subdivision
#define	TEX_SPECIAL	1

#define	MAXLIGHTMAPS	4

// Ambient sounds
#define	AMBIENT_WATER	0
#define	AMBIENT_SKY	1
#define	AMBIENT_SLIME	2
#define	AMBIENT_LAVA	3
#define	NUM_AMBIENTS	4	// automatic ambient sounds

/*
 * The quality of the bsp output is highly sensitive to these epsilon values.
 * Notes:
 * - CONTINUOUS_EPSILON needs to be slightly larger than EQUAL_EPSILON,
 *   otherwise this messes with t-junctions
 */
#define NORMAL_EPSILON		0.000001
#define ANGLEEPSILON		0.000001
#define DIST_EPSILON		0.0001
#define ZERO_EPSILON		0.0001
#define DISTEPSILON		0.0001
#define POINT_EPSILON		0.0001
#define T_EPSILON		0.0001
#define ON_EPSILON		0.0001
#define EQUAL_EPSILON		0.0001
#define CONTINUOUS_EPSILON	0.0005

#define BOGUS_RANGE	18000

// the exact bounding box of the brushes is expanded some for the headnode
// volume.  is this still needed?
#define	SIDESPACE	24

// First 15 must be in same order as BSP file is in.
// Up through BSPMODEL must remain UNALTERED
// must also alter rgcMemSize and PrintMem
enum {
    BSPENT,
    BSPPLANE,
    BSPTEX,
    BSPVERTEX,
    BSPVIS,
    BSPNODE,
    BSPTEXINFO,
    BSPFACE,
    BSPLIGHT,
    BSPCLIPNODE,
    BSPLEAF,
    BSPMARKSURF,
    BSPEDGE,
    BSPSURFEDGE,
    BSPMODEL,

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

#define BSP_LUMPS	15

typedef unsigned char byte;

double I_FloatTime(void);

void DefaultExtension(char *path, char *extension);
void StripExtension(char *path);
void StripFilename(char *path);
int IsAbsolutePath(const char *path);

char *copystring(char *s);

//===== mathlib.h

#ifdef DOUBLEVEC_T
typedef double vec_t;
#define VECT_MAX DBL_MAX
#else
typedef float vec_t;
#define VECT_MAX FLT_MAX
#endif
typedef vec_t vec3_t[3];

extern vec3_t vec3_origin;

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

//====== bspfile.h



//=============================================================================


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
    int dataofs[4];		// [nummiptex]
} dmiptexlump_t;

typedef char miptex_t[16];

typedef struct {
    float point[3];
} dvertex_t;


typedef struct {
    float normal[3];
    float dist;
    int type;			// PLANE_X - PLANE_ANYZ ?remove? trivial to regenerate
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

//============================================================================

void LoadBSPFile(void);
void WriteBSPFile(void);
void PrintBSPFileSizes(void);

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

winding_t *BaseWindingForPlane(plane_t *p);
void CheckWinding(winding_t *w);
winding_t *NewWinding(int points);
void FreeWinding(winding_t *w);
winding_t *CopyWinding(winding_t *w);
winding_t *ClipWinding(winding_t *in, plane_t *split, bool keepon);
void DivideWinding(winding_t *in, plane_t *split, winding_t **front,
		   winding_t **back);
void MidpointWinding(winding_t *w, vec3_t v);

/* Helper function for ClipWinding and it's variants */
void CalcSides(const winding_t *in, const plane_t *split, int *sides,
	       vec_t *dists, int counts[3]);

//============================================================================


typedef struct visfacet_s {
    struct visfacet_s *next;

    int planenum;
    int planeside;		// which side is the front of the face
    int texturenum;
    int contents[2];		// 0 = front side

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
    // as a splitting node
    face_t *faces;		// links to all the faces on either side of the surf
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
    int valid;			// for flood filling
    int occupied;		// entity number in leaf for outside filling
} node_t;

//=============================================================================

// brush.c

typedef struct brush_s {
    struct brush_s *next;
    vec3_t mins, maxs;
    face_t *faces;
    int contents;
} brush_t;

extern int numbrushplanes;

void FixRotateOrigin(int iEntity, vec3_t offset);
void Brush_LoadEntity(void);
void FreeBrushsetBrushes(void);

void PlaneHash_Init(void);
int FindPlane(plane_t *dplane, int *side);

//=============================================================================

// csg4.c

// build surfaces is also used by GatherNodeFaces
extern face_t **validfaces;
extern int csgmergefaces;

surface_t *BuildSurfaces(void);
face_t *NewFaceFromFace(face_t *in);
surface_t *CSGFaces(void);
void SplitFace(face_t *in, plane_t *split, face_t **front, face_t **back);
void UpdateFaceSphere(face_t *in);

//=============================================================================

// solidbsp.c

extern int splitnodes;

void DivideFacet(face_t *in, plane_t *split, face_t **front, face_t **back);
void CalcSurfaceInfo(surface_t *surf);
void SubdivideFace(face_t *f, face_t **prevptr);
node_t *SolidBSP(surface_t *surfhead, bool midsplit);

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
void MakeFaceEdges(node_t *headnode);

//=============================================================================

// portals.c

typedef struct portal_s {
    int planenum;
    node_t *nodes[2];		// [0] = front side of planenum
    struct portal_s *next[2];
    winding_t *winding;
} portal_t;

extern node_t outside_node;	// portals outside the world face this
extern int num_visportals;

void PortalizeWorld(node_t *headnode);
void FreeAllPortals(node_t *node);

//=============================================================================

// region.c

void GrowNodeRegions(node_t *headnode);

//=============================================================================

// tjunc.c

typedef struct wvert_s {
    vec_t t;
    struct wvert_s *prev, *next;
} wvert_t;

typedef struct wedge_s {
    struct wedge_s *next;
    vec3_t dir;
    vec3_t origin;
    wvert_t head;
} wedge_t;

void tjunc(node_t *headnode);

//=============================================================================

// writebsp.c

void ExportNodePlanes(node_t *headnode);
void ExportClipNodes(node_t *headnode);
void ExportDrawNodes(node_t *headnode);

void BeginBSPFile(void);
void FinishBSPFile(void);

//=============================================================================

// outside.c

bool FillOutside(node_t *node);

//=============================================================================

typedef struct options_s {
    bool fNofill;
    bool fNoclip;
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
    int dxSubdivide;
    int dxLeakDist;
    char szMapName[512];
    char szBSPName[512];
    char wadPath[512];
} options_t;

extern options_t options;

extern int hullnum;

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
} mapface_t;

typedef struct mapbrush_s {
    int iFaceStart;
    int iFaceEnd;
} mapbrush_t;

struct lumpdata {
    int count;
    int index;
    void *data;
};

typedef struct mapentity_s {
    vec3_t origin;
    int iBrushStart;
    int iBrushEnd;
    epair_t *epairs;
    vec3_t mins, maxs;
    brush_t *pBrushes;		/* NULL terminated list */
    int cBrushes;
    struct lumpdata lumps[BSP_LUMPS];
} mapentity_t;

typedef struct mapdata_s {
    // c for (total) count of items
    int cFaces;
    int cBrushes;
    int cEntities;

    // i for (current) index of items
    int iFaces;
    int iBrushes;
    int iEntities;

    // rg of array (range) of actual items
    mapface_t *rgFaces;
    mapbrush_t *rgBrushes;
    mapentity_t *rgEntities;

    // Totals for BSP data items
    int cTotal[BSP_LUMPS];
} mapdata_t;

extern mapdata_t map;
extern mapentity_t *pCurEnt;
extern mapentity_t *pWorldEnt;

extern int cMiptex;
extern int cPlanes;
extern miptex_t *rgszMiptex;
extern plane_t *pPlanes;

void LoadMapFile(void);

int FindMiptex(char *name);

void PrintEntity(int iEntity);
char *ValueForKey(int iEntity, char *key);
void SetKeyValue(int iEntity, char *key, char *value);
void GetVectorForKey(int iEntity, char *szKey, vec3_t vec);

void WriteEntitiesToString(void);


// util.c

#define msgError	0
#define msgWarning	1
#define msgStat		2
#define msgProgress	3
#define msgLiteral	4
#define msgFile		5
#define msgScreen	6
#define msgPercent	7

extern char *rgszWarnings[cWarnings];
extern char *rgszErrors[cErrors];
extern const int rgcMemSize[];

extern void *AllocMem(int Type, int cSize, bool fZero);
extern void FreeMem(void *pMem, int Type, int cSize);
extern void FreeAllMem(void);
extern void PrintMem(void);

extern void Message(int MsgType, ...);

extern FILE *logfile;

#endif
