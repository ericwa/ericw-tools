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

#pragma once

#include <vector>
#include <map>
#include <unordered_map>
#include <array>
#include <optional>

#include <cassert>
#include <cctype>
#include <cerrno>
#include <cfloat>
#include <cmath>
#include <cstdint>
//#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <common/bspfile.hh>
#include <common/aabb.hh>

#include "file.hh"

/*
 * Clipnodes need to be stored as a 16-bit offset. Originally, this was a
 * signed value and only the positive values up to 32767 were available. Since
 * the negative range was unused apart from a few values reserved for flags,
 * this has been extended to allow up to 65520 (0xfff0) clipnodes (with a
 * suitably modified engine).
 */
#define MAX_BSP_CLIPNODES 0xfff0

// key / value pair sizes
#define MAX_KEY 32
#define MAX_VALUE 1024

// Various other geometry maximums
#define MAX_POINTS_ON_WINDING 96
#define MAXEDGES 64
#define MAXPOINTS                                                                                                      \
    60 // don't let a base face get past this
       // because it can be split more later

// For brush.c, normal and +16 (?)
#define NUM_HULLS 2

// 0-2 are axial planes
// 3-5 are non-axial planes snapped to the nearest
#define PLANE_X 0
#define PLANE_Y 1
#define PLANE_Z 2
#define PLANE_ANYX 3
#define PLANE_ANYY 4
#define PLANE_ANYZ 5

// planenum for a leaf (?)
#define PLANENUM_LEAF -1

/*
 * The quality of the bsp output is highly sensitive to these epsilon values.
 * Notes:
 * - T-junction calculations are sensitive to errors and need the various
 *   epsilons to be such that EQUAL_EPSILON < T_EPSILON < CONTINUOUS_EPSILON.
 *     ( TODO: re-check if CONTINUOUS_EPSILON is still directly related )
 */
#define ANGLEEPSILON 0.000001
#define DIST_EPSILON 0.0001
#define ZERO_EPSILON 0.0001
#define DISTEPSILON 0.0001
#define POINT_EPSILON 0.0001
#define ON_EPSILON options.on_epsilon
#define EQUAL_EPSILON 0.0001
#define T_EPSILON 0.0002
#define CONTINUOUS_EPSILON 0.0005

// from q3map
#define MAX_WORLD_COORD (128 * 1024)
#define MIN_WORLD_COORD (-128 * 1024)
#define WORLD_SIZE (MAX_WORLD_COORD - MIN_WORLD_COORD)

// the exact bounding box of the brushes is expanded some for the headnode
// volume.  is this still needed?
#define SIDESPACE 24

// AllocMem types
enum
{
    WINDING,
    OTHER
};

#include <common/cmdlib.hh>
#include <common/mathlib.hh>
#include <qbsp/winding.hh>

struct mtexinfo_t
{
    texvecf vecs; /* [s/t][xyz offset] */
    int32_t miptex = 0;
    surfflags_t flags = {};
    int32_t value = 0; // Q2-specific
    std::optional<int> outputnum = std::nullopt; // nullopt until added to bsp

    constexpr auto as_tuple() const { return std::tie(vecs, miptex, flags, value); }

    constexpr bool operator<(const mtexinfo_t &other) const { return as_tuple() < other.as_tuple(); }

    constexpr bool operator>(const mtexinfo_t &other) const { return as_tuple() > other.as_tuple(); }
};

struct face_t
{
    face_t *next;

    int planenum;
    int planeside; // which side is the front of the face
    int texinfo;
    contentflags_t contents[2]; // 0 = front side
    short lmshift[2]; // lightmap scale.

    face_t *original; // face on node
    int outputnumber; // only valid for original faces after
                      // write surfaces
    bool touchesOccupiedLeaf; // internal use in outside.cc
    vec3_t origin;
    vec_t radius;

    int *edges;
    winding_t w;
};

struct surface_t
{
    surface_t *next;
    surface_t *original; // before BSP cuts it up
    int planenum;
    int outputplanenum; // only valid after WriteSurfacePlanes
    aabb3d bounds;
    bool onnode; // true if surface has already been used
                 //   as a splitting node
    bool detail_separator; // true if ALL faces are detail
    face_t *faces; // links to all faces on either side of the surf
    bool has_detail; // 1 if the surface has detail brushes
    bool has_struct; // 1 if the surface has non-detail brushes
    short lmshift;
};

// there is a node_t structure for every node and leaf in the bsp tree

class mapentity_t;
struct brush_t;
struct portal_t;

struct node_t
{
    aabb3d bounds; // bounding volume, not just points inside

    // information for decision nodes
    int planenum; // -1 = leaf node
    // outputplanenum moved to qbsp_plane_t
    int firstface; // decision node only
    int numfaces; // decision node only
    node_t *children[2]; // children[0] = front side, children[1] = back side of plane. only valid for decision nodes
    face_t *faces; // decision nodes only, list for both sides

    // information for leafs
    contentflags_t contents; // leaf nodes (0 for decision nodes)
    face_t **markfaces; // leaf nodes only, point to node faces
    portal_t *portals;
    int visleafnum; // -1 = solid
    int viscluster; // detail cluster for faster vis
    int occupied; // 0=can't reach entity, 1 = has entity, >1 = distance from leaf with entity
    mapentity_t *occupant; // example occupant, for leak hunting
    bool detail_separator; // for vis portal generation. true if ALL faces on node, and on all descendant nodes/leafs,
                           // are detail.
    uint32_t firstleafbrush; // Q2
    uint32_t numleafbrushes;

    bool opaque() const;
};

#include <qbsp/brush.hh>
#include <qbsp/csg4.hh>
#include <qbsp/solidbsp.hh>
#include <qbsp/merge.hh>
#include <qbsp/surfaces.hh>
#include <qbsp/portals.hh>
#include <qbsp/region.hh>
#include <qbsp/tjunc.hh>
#include <qbsp/writebsp.hh>
#include <qbsp/outside.hh>

enum texcoord_style_t
{
    TX_QUAKED = 0,
    TX_QUARK_TYPE1 = 1,
    TX_QUARK_TYPE2 = 2,
    TX_VALVE_220 = 3,
    TX_BRUSHPRIM = 4
};

enum class conversion_t
{
    quake,
    quake2,
    valve,
    bp
};

class options_t
{
public:
    bool fNofill;
    bool fNoclip;
    bool fNoskip;
    bool fNodetail;
    bool fOnlyents;
    bool fConvertMapFormat;
    conversion_t convertMapFormat;
    bool fVerbose;
    bool fAllverbose;
    bool fSplitspecial;
    bool fSplitturb;
    bool fSplitsky;
    bool fTranswater;
    bool fTranssky;
    bool fOldaxis;
    bool fNoverbose;
    bool fNopercent;
    bool forceGoodTree;
    bool fixRotateObjTexture;
    bool fbspx_brushes;
    bool fNoTextures;
    const bspversion_t *target_version = &bspver_q1;
    const gamedef_t *target_game = target_version->game;
    int dxSubdivide;
    int dxLeakDist;
    int maxNodeSize;
    /**
     * if 0 (default), use maxNodeSize for deciding when to switch to midsplit bsp heuristic.
     *
     * if 0 < midsplitSurfFraction <=1, switch to midsplit if the node contains more than this fraction of the model's
     * total surfaces. Try 0.15 to 0.5. Works better than maxNodeSize for maps with a 3D skybox (e.g. +-128K unit maps)
     */
    float midsplitSurfFraction;
    std::filesystem::path szMapName;
    std::filesystem::path szBSPName;

    struct wadpath
    {
        std::filesystem::path path;
        bool external; // wads from this path are not to be embedded into the bsp, but will instead require the engine
                       // to load them from elsewhere. strongly recommended for eg halflife.wad
    };

    std::vector<wadpath> wadPathsVec;
    vec_t on_epsilon;
    bool fObjExport;
    bool fOmitDetail;
    bool fOmitDetailWall;
    bool fOmitDetailIllusionary;
    bool fOmitDetailFence;
    bool fForcePRT1;
    bool fTestExpand;
    bool fLeakTest;
    bool fContentHack;
    vec_t worldExtent;
    bool fNoThreads;

    options_t()
        : fNofill(false), fNoclip(false), fNoskip(false), fNodetail(false), fOnlyents(false), fConvertMapFormat(false),
          convertMapFormat(conversion_t::quake), fVerbose(true), fAllverbose(false), fSplitspecial(false),
          fSplitturb(false), fSplitsky(false), fTranswater(true), fTranssky(false), fOldaxis(true), fNoverbose(false),
          fNopercent(false), forceGoodTree(false), fixRotateObjTexture(true), fbspx_brushes(false), fNoTextures(false),
          dxSubdivide(240), dxLeakDist(2), maxNodeSize(1024), midsplitSurfFraction(0.0f), szMapName{}, szBSPName{},
          on_epsilon(0.0001), fObjExport(false), fOmitDetail(false), fOmitDetailWall(false),
          fOmitDetailIllusionary(false), fOmitDetailFence(false), fForcePRT1(false), fTestExpand(false),
          fLeakTest(false), fContentHack(false), worldExtent(65536.0f), fNoThreads(false)
    {
    }
};

extern options_t options;

#include <qbsp/map.hh>
#include <qbsp/util.hh>

int qbsp_main(int argc, const char **argv);
