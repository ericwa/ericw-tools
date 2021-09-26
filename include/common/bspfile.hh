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

#pragma once

#include <cstdint>
#include <array>
#include <tuple>
#include <variant>

#include <common/cmdlib.hh>
#include <common/log.hh>
#include <common/aabb.hh>

/* upper design bounds */

#define MAX_MAP_HULLS_Q1 4
#define MAX_MAP_HULLS_H2 8

#define MAX_MAP_MODELS 256
#define MAX_MAP_BRUSHES 4096
#define MAX_MAP_PLANES 16384
#define MAX_MAP_NODES 32767 /* negative shorts are contents */
#define MAX_MAP_CLIPNODES 65520 /* = 0xfff0; larger are contents */
#define MAX_MAP_LEAFS 32767 /* BSP file format limitation */
#define MAX_MAP_VERTS 65535
#define MAX_MAP_FACES 65535
#define MAX_MAP_MARKSURFACES 65535
#define MAX_MAP_TEXINFO 8192
#define MAX_MAP_EDGES 256000
#define MAX_MAP_SURFEDGES 512000
#define MAX_MAP_MIPTEX 0x0800000
#define MAX_MAP_LIGHTING 0x8000000
#define MAX_MAP_VISIBILITY 0x8000000

/* key / value pair sizes */
#define MAX_ENT_KEY 32
#define MAX_ENT_VALUE 1024

#define NO_VERSION -1

#define BSPVERSION 29
#define BSP2RMQVERSION (('B' << 24) | ('S' << 16) | ('P' << 8) | '2')
#define BSP2VERSION ('B' | ('S' << 8) | ('P' << 16) | ('2' << 24))
#define BSPHLVERSION 30 // 24bit lighting, and private palettes in the textures lump.
#define Q2_BSPIDENT (('P' << 24) + ('S' << 16) + ('B' << 8) + 'I')
#define Q2_BSPVERSION 38
#define Q2_QBISMIDENT (('P' << 24) + ('S' << 16) + ('B' << 8) + 'Q')

struct lump_t
{
    int32_t fileofs;
    int32_t filelen;

    auto stream_data()
    {
        return std::tie(fileofs, filelen);
    }
};

#define LUMP_ENTITIES 0
#define LUMP_PLANES 1
#define LUMP_TEXTURES 2
#define LUMP_VERTEXES 3
#define LUMP_VISIBILITY 4
#define LUMP_NODES 5
#define LUMP_TEXINFO 6
#define LUMP_FACES 7
#define LUMP_LIGHTING 8
#define LUMP_CLIPNODES 9
#define LUMP_LEAFS 10
#define LUMP_MARKSURFACES 11
#define LUMP_EDGES 12
#define LUMP_SURFEDGES 13
#define LUMP_MODELS 14

#define BSP_LUMPS 15

#define Q2_LUMP_ENTITIES 0
#define Q2_LUMP_PLANES 1
#define Q2_LUMP_VERTEXES 2
#define Q2_LUMP_VISIBILITY 3
#define Q2_LUMP_NODES 4
#define Q2_LUMP_TEXINFO 5
#define Q2_LUMP_FACES 6
#define Q2_LUMP_LIGHTING 7
#define Q2_LUMP_LEAFS 8
#define Q2_LUMP_LEAFFACES 9
#define Q2_LUMP_LEAFBRUSHES 10
#define Q2_LUMP_EDGES 11
#define Q2_LUMP_SURFEDGES 12
#define Q2_LUMP_MODELS 13
#define Q2_LUMP_BRUSHES 14
#define Q2_LUMP_BRUSHSIDES 15
#define Q2_LUMP_POP 16
#define Q2_LUMP_AREAS 17
#define Q2_LUMP_AREAPORTALS 18
#define Q2_HEADER_LUMPS 19

struct bspx_header_t
{
    std::array<char, 4> id = { 'B', 'S', 'P', 'X' }; //'BSPX'
    uint32_t numlumps = 0;

    auto stream_data()
    {
        return std::tie(id, numlumps);
    }
};

struct bspx_lump_t
{
    std::array<char, 24> lumpname;
    uint32_t fileofs;
    uint32_t filelen;

    auto stream_data()
    {
        return std::tie(lumpname, fileofs, filelen);
    }
};

struct lumpspec_t
{
    const char *name;
    size_t size;
};

using bspvec3s_t = std::array<int16_t, 3>;
using bspvec3f_t = std::array<float, 3>;

// helper functions to quickly numerically cast mins/maxs
// and floor/ceil them in the case of float -> integral
template<typename T, typename F>
inline std::array<T, 3> aabb_mins_cast(const std::array<F, 3> &f)
{
    if constexpr(std::is_floating_point_v<T>)
        return { numeric_cast<T>(floor(f[0])), numeric_cast<T>(floor(f[1])), numeric_cast<T>(floor(f[2])) };
    else
        return { numeric_cast<T>(f[0]), numeric_cast<T>(f[1]), numeric_cast<T>(f[2]) };
}

template<typename T, typename F>
inline std::array<T, 3> aabb_maxs_cast(const std::array<F, 3> &f)
{
    if constexpr(std::is_floating_point_v<T>)
        return { numeric_cast<T>(ceil(f[0])), numeric_cast<T>(ceil(f[1])), numeric_cast<T>(ceil(f[2])) };
    else
        return { numeric_cast<T>(f[0]), numeric_cast<T>(f[1]), numeric_cast<T>(f[2]) };
}

struct dmodelh2_t
{
    bspvec3f_t mins;
    bspvec3f_t maxs;
    bspvec3f_t origin;
    std::array<int32_t, MAX_MAP_HULLS_H2> headnode; /* hexen2 only uses 6 */
    int32_t visleafs; /* not including the solid leaf 0 */
    int32_t firstface;
    int32_t numfaces;
};

// shortcut template to trim (& convert) std::arrays
// between two lengths
template<typename ADest, typename ASrc>
constexpr ADest array_cast(const ASrc &src)
{
    ADest dest { };

    for (size_t i = 0; i < std::min(dest.size(), src.size()); i++)
    {
        if constexpr (std::is_arithmetic_v<ADest::value_type> && std::is_arithmetic_v<ASrc::value_type>)
            dest[i] = numeric_cast<ADest::value_type>(src[i]);
        else
            dest[i] = static_cast<ADest::value_type>(src[i]);
    }

    return dest;
}

struct dmodelq1_t
{
    bspvec3f_t mins;
    bspvec3f_t maxs;
    bspvec3f_t origin;
    std::array<int32_t, MAX_MAP_HULLS_Q1> headnode; /* 4 for backward compat, only 3 hulls exist */
    int32_t visleafs; /* not including the solid leaf 0 */
    int32_t firstface;
    int32_t numfaces;

    dmodelq1_t() = default;

    // convert from mbsp_t
    dmodelq1_t(const dmodelh2_t &model) :
        mins(model.mins),
        maxs(model.maxs),
        origin(model.origin),
        headnode(array_cast<decltype(headnode)>(model.headnode)),
        visleafs(model.visleafs),
        firstface(model.firstface),
        numfaces(model.numfaces)
    {
    }

    // convert to mbsp_t
    operator dmodelh2_t() const
    {
        return {
            mins,
            maxs,
            origin,
            array_cast<decltype(dmodelh2_t::headnode)>(headnode),
            visleafs,
            firstface,
            numfaces
        };
    }
};

struct q2_dmodel_t
{
    bspvec3f_t mins;
    bspvec3f_t maxs;
    bspvec3f_t origin; // for sounds or lights
    int32_t headnode;
    int32_t firstface;
    int32_t numfaces; // submodels just draw faces
                      // without walking the bsp tree

    q2_dmodel_t() = default;

    // convert from mbsp_t
    q2_dmodel_t(const dmodelh2_t &model) :
        mins(model.mins),
        maxs(model.maxs),
        origin(model.origin),
        headnode(model.headnode[0]),
        firstface(model.firstface),
        numfaces(model.numfaces)
    {
    }

    // convert to mbsp_t
    operator dmodelh2_t() const
    {
        return {
            mins,
            maxs,
            origin,
            { headnode },
            0, // visleafs
            firstface,
            numfaces
        };
    }
    
    // serialize for streams
    auto stream_data()
    {
        return std::tie(mins, maxs, origin, headnode, firstface, numfaces);
    }
};

// FIXME: remove
using dmodel_t = dmodelh2_t;

struct dmiptexlump_t
{
    int32_t nummiptex;
    int32_t dataofs[4]; /* [nummiptex] */
};

#define MIPLEVELS 4
struct miptex_t
{
    char name[16];
    uint32_t width, height;
    uint32_t offsets[MIPLEVELS]; /* four mip maps stored */
};

// mxd. Used to store RGBA data in mbsp->drgbatexdata
struct rgba_miptex_t
{
    char name[32]; // Same as in Q2
    unsigned width, height;
    unsigned offset; // Offset to RGBA texture pixels
};

using dvertex_t = bspvec3f_t;

/* 0-2 are axial planes */
#define PLANE_X 0
#define PLANE_Y 1
#define PLANE_Z 2

/* 3-5 are non-axial planes snapped to the nearest */
#define PLANE_ANYX 3
#define PLANE_ANYY 4
#define PLANE_ANYZ 5

struct dplane_t
{
    bspvec3f_t normal;
    float dist;
    int32_t type;

    // serialize for streams
    auto stream_data()
    {
        return std::tie(normal, dist, type);
    }
};

// Q1 contents

#define CONTENTS_EMPTY -1
#define CONTENTS_SOLID -2
#define CONTENTS_WATER -3
#define CONTENTS_SLIME -4
#define CONTENTS_LAVA -5
#define CONTENTS_SKY -6
#define CONTENTS_MIN CONTENTS_SKY

// Q2 contents (from qfiles.h)

// contents flags are seperate bits
// a given brush can contribute multiple content bits
// multiple brushes can be in a single leaf

// these definitions also need to be in q_shared.h!

// lower bits are stronger, and will eat weaker brushes completely
#define Q2_CONTENTS_SOLID 1 // an eye is never valid in a solid
#define Q2_CONTENTS_WINDOW 2 // translucent, but not watery
#define Q2_CONTENTS_AUX 4
#define Q2_CONTENTS_LAVA 8
#define Q2_CONTENTS_SLIME 16
#define Q2_CONTENTS_WATER 32
#define Q2_CONTENTS_MIST 64
#define Q2_LAST_VISIBLE_CONTENTS 64

#define Q2_CONTENTS_LIQUID (Q2_CONTENTS_LAVA | Q2_CONTENTS_SLIME | Q2_CONTENTS_WATER) // mxd

// remaining contents are non-visible, and don't eat brushes

#define Q2_CONTENTS_AREAPORTAL 0x8000

#define Q2_CONTENTS_PLAYERCLIP 0x10000
#define Q2_CONTENTS_MONSTERCLIP 0x20000

// currents can be added to any other contents, and may be mixed
#define Q2_CONTENTS_CURRENT_0 0x40000
#define Q2_CONTENTS_CURRENT_90 0x80000
#define Q2_CONTENTS_CURRENT_180 0x100000
#define Q2_CONTENTS_CURRENT_270 0x200000
#define Q2_CONTENTS_CURRENT_UP 0x400000
#define Q2_CONTENTS_CURRENT_DOWN 0x800000

#define Q2_CONTENTS_ORIGIN 0x1000000 // removed before bsping an entity

#define Q2_CONTENTS_MONSTER 0x2000000 // should never be on a brush, only in game
#define Q2_CONTENTS_DEADMONSTER 0x4000000
#define Q2_CONTENTS_DETAIL 0x8000000 // brushes to be added after vis leafs
#define Q2_CONTENTS_TRANSLUCENT 0x10000000 // auto set if any surface has trans
#define Q2_CONTENTS_LADDER 0x20000000

// Special contents flags for the compiler only
#define CFLAGS_STRUCTURAL_COVERED_BY_DETAIL (1 << 0)
#define CFLAGS_WAS_ILLUSIONARY (1 << 1) /* was illusionary, got changed to something else */
#define CFLAGS_DETAIL_WALL (1 << 2) /* don't clip world for func_detail_wall entities */
#define CFLAGS_BMODEL_MIRROR_INSIDE                                                                                    \
    (1 << 3) /* set "_mirrorinside" "1" on a bmodel to mirror faces for when the player is inside. */
#define CFLAGS_NO_CLIPPING_SAME_TYPE                                                                                   \
    (1 << 4) /* Don't clip the same content type. mostly intended for CONTENTS_DETAIL_ILLUSIONARY */
// only one of these flags below should ever be set.
#define CFLAGS_HINT (1 << 5)
#define CFLAGS_CLIP (1 << 6)
#define CFLAGS_ORIGIN (1 << 7)
#define CFLAGS_DETAIL (1 << 8)
#define CFLAGS_DETAIL_ILLUSIONARY (1 << 9)
#define CFLAGS_DETAIL_FENCE (1 << 10)
#define CFLAGS_ILLUSIONARY_VISBLOCKER (1 << 11)
// all of the detail values
#define CFLAGS_DETAIL_MASK (CFLAGS_DETAIL | CFLAGS_DETAIL_ILLUSIONARY | CFLAGS_DETAIL_FENCE)

struct gamedef_t;

struct contentflags_t
{
    // native flags value; what's written to the BSP basically
    int32_t native;

    // extra flags, specific to BSP only
    int32_t extended;

    // merge these content flags with other, and use
    // their native contents.
    constexpr contentflags_t merge(const contentflags_t &other) const
    {
        return {other.native, extended | other.extended};
    }

    constexpr bool operator==(const contentflags_t &other) const
    {
        return native == other.native && extended == other.extended;
    }

    constexpr bool operator!=(const contentflags_t &other) const { return !(*this == other); }

    // check if these contents are marked as any (or a specific kind of) detail brush.
    constexpr bool is_detail(int32_t types = CFLAGS_DETAIL_MASK) const
    {
        return (extended & CFLAGS_DETAIL_MASK) & types;
    }

    bool is_empty(const gamedef_t *game) const;
    bool is_solid(const gamedef_t *game) const;
    bool is_sky(const gamedef_t *game) const;
    bool is_liquid(const gamedef_t *game) const;
    bool is_valid(const gamedef_t *game, bool strict = true) const;

    bool is_structural_solid(const gamedef_t *game) const { return is_solid(game) && !is_detail(); }

    bool is_structural_sky(const gamedef_t *game) const { return is_sky(game) && !is_detail(); }

    bool is_structural_sky_or_solid(const gamedef_t *game) const
    {
        return (is_sky(game) || is_solid(game)) && !is_detail();
    }

    constexpr bool is_hint() const { return extended & CFLAGS_HINT; }

    constexpr bool clips_same_type() const { return !(extended & CFLAGS_NO_CLIPPING_SAME_TYPE); }

    constexpr bool is_fence() const { return is_detail(CFLAGS_DETAIL_FENCE | CFLAGS_DETAIL_ILLUSIONARY); }

    // check if this content's `type` - which is distinct from various
    // flags that turn things on/off - match. Exactly what the native
    // "type" is depends on the game, but any of the detail flags must
    // also match.
    bool types_equal(const contentflags_t &other, const gamedef_t *game) const;

    int32_t priority(const gamedef_t *game) const;

    std::string to_string(const gamedef_t *game) const;
};

struct bsp2_dnode_t
{
    int32_t planenum;
    std::array<int32_t, 2> children; /* negative numbers are -(leafs+1), not nodes */
    bspvec3f_t mins; /* for sphere culling */
    bspvec3f_t maxs;
    uint32_t firstface;
    uint32_t numfaces; /* counting both sides */
};

struct bsp29_dnode_t
{
    int32_t planenum;
    std::array<int16_t, 2> children; /* negative numbers are -(leafs+1), not nodes. children[0] is front, children[1] is back */
    bspvec3s_t mins; /* for sphere culling */
    bspvec3s_t maxs;
    uint16_t firstface;
    uint16_t numfaces; /* counting both sides */

    bsp29_dnode_t() = default;

    // convert from mbsp_t
    bsp29_dnode_t(const bsp2_dnode_t &model) :
        planenum(model.planenum),
        children(array_cast<decltype(children)>(model.children)),
        mins(aabb_mins_cast<int16_t>(model.mins)),
        maxs(aabb_maxs_cast<int16_t>(model.maxs)),
        firstface(numeric_cast<uint16_t>(model.firstface)),
        numfaces(numeric_cast<uint16_t>(model.numfaces))
    {
    }

    // convert to mbsp_t
    operator bsp2_dnode_t() const
    {
        return {
            planenum,
            array_cast<decltype(bsp2_dnode_t::children)>(children),
            aabb_mins_cast<float>(mins),
            aabb_mins_cast<float>(maxs),
            firstface,
            numfaces
        };
    }
};

struct bsp2rmq_dnode_t
{
    int32_t planenum;
    std::array<int32_t, 2> children; /* negative numbers are -(leafs+1), not nodes */
    bspvec3s_t mins; /* for sphere culling */
    bspvec3s_t maxs;
    uint32_t firstface;
    uint32_t numfaces; /* counting both sides */

    bsp2rmq_dnode_t() = default;

    // convert from mbsp_t
    bsp2rmq_dnode_t(const bsp2_dnode_t &model) :
        planenum(model.planenum),
        children(model.children),
        mins(aabb_mins_cast<int16_t>(model.mins)),
        maxs(aabb_maxs_cast<int16_t>(model.maxs)),
        firstface(model.firstface),
        numfaces(model.numfaces)
    {
    }

    // convert to mbsp_t
    operator bsp2_dnode_t() const
    {
        return {
            planenum,
            children,
            aabb_mins_cast<float>(mins),
            aabb_mins_cast<float>(maxs),
            firstface,
            numfaces
        };
    }
};

struct q2_dnode_t
{
    int32_t planenum;
    std::array<int32_t, 2> children; // negative numbers are -(leafs+1), not nodes
    bspvec3s_t mins; // for frustom culling
    bspvec3s_t maxs;
    uint16_t firstface;
    uint16_t numfaces; // counting both sides

    q2_dnode_t() = default;

    // convert from mbsp_t
    q2_dnode_t(const bsp2_dnode_t &model) :
        planenum(model.planenum),
        children(model.children),
        mins(aabb_mins_cast<int16_t>(model.mins)),
        maxs(aabb_maxs_cast<int16_t>(model.maxs)),
        firstface(numeric_cast<uint16_t>(model.firstface)),
        numfaces(numeric_cast<uint16_t>(model.numfaces))
    {
    }

    // convert to mbsp_t
    operator bsp2_dnode_t() const
    {
        return {
            planenum,
            children,
            aabb_mins_cast<float>(mins),
            aabb_mins_cast<float>(maxs),
            firstface,
            numfaces
        };
    }

    // serialize for streams
    auto stream_data()
    {
        return std::tie(planenum, children, mins, maxs, firstface, numfaces);
    }
};

using q2_dnode_qbism_t = bsp2_dnode_t;

/*
 * Note that children are interpreted as unsigned values now, so that we can
 * handle > 32k clipnodes. Values > 0xFFF0 can be assumed to be CONTENTS
 * values and can be read as the signed value to be compatible with the above
 * (i.e. simply subtract 65536).
 */
struct bsp2_dclipnode_t
{
    int32_t planenum;
    std::array<int32_t, 2> children; /* negative numbers are contents */
};

struct bsp29_dclipnode_t
{
    int32_t planenum;
    std::array<int16_t, 2> children; /* negative numbers are contents */

    bsp29_dclipnode_t() = default;
    
    // convert from mbsp_t
    bsp29_dclipnode_t(const bsp2_dclipnode_t &model) :
        planenum(model.planenum),
        children({ downcast(model.children[0]), downcast(model.children[1]) })
    {
    }

    // convert to mbsp_t
    operator bsp2_dclipnode_t() const
    {
        return {
            planenum,
            { upcast(children[0]), upcast(children[1]) }
        };
    }
    
    /* Slightly tricky since we support > 32k clipnodes */
private:
    static constexpr int16_t downcast(const int32_t &v)
    {
        return numeric_cast<int16_t>(v < 0 ? v + 0x10000 : v);
    }
    
    static constexpr int32_t upcast(const int16_t &v)
    {
        int32_t child = (uint16_t)v;
        return child > 0xfff0 ? child - 0x10000 : child;
    }
};

// Q1 Texture flags.
#define TEX_SPECIAL 1 /* sky or slime, no lightmap or 256 subdivision */

// Q2 Texture flags.
#define Q2_SURF_LIGHT 0x1 // value will hold the light strength

#define Q2_SURF_SLICK 0x2 // effects game physics

#define Q2_SURF_SKY 0x4 // don't draw, but add to skybox
#define Q2_SURF_WARP 0x8 // turbulent water warp
#define Q2_SURF_TRANS33 0x10
#define Q2_SURF_TRANS66 0x20
#define Q2_SURF_FLOWING 0x40 // scroll towards angle
#define Q2_SURF_NODRAW 0x80 // don't bother referencing the texture

#define Q2_SURF_HINT 0x100 // make a primary bsp splitter
#define Q2_SURF_SKIP 0x200 // completely ignore, allowing non-closed brushes

#define Q2_SURF_TRANSLUCENT (Q2_SURF_TRANS33 | Q2_SURF_TRANS66) // mxd

// QBSP/LIGHT flags
#define TEX_EXFLAG_SKIP (1U << 0) /* an invisible surface */
#define TEX_EXFLAG_HINT (1U << 1) /* hint surface */
#define TEX_EXFLAG_NODIRT (1U << 2) /* don't receive dirtmapping */
#define TEX_EXFLAG_NOSHADOW (1U << 3) /* don't cast a shadow */
#define TEX_EXFLAG_NOBOUNCE (1U << 4) /* light doesn't bounce off this face */
#define TEX_EXFLAG_NOMINLIGHT (1U << 5) /* opt out of minlight on this face */
#define TEX_EXFLAG_NOEXPAND (1U << 6) /* don't expand this face for larger clip hulls */
#define TEX_EXFLAG_LIGHTIGNORE (1U << 7) /* PLEASE DOCUMENT ME MOMMY */

struct surfflags_t
{
    // native flags value; what's written to the BSP basically
    int32_t native;

    // extra flags, specific to BSP/LIGHT only
    uint8_t extended;

    // if non zero, enables phong shading and gives the angle threshold to use
    uint8_t phong_angle;

    // minlight value for this face
    uint8_t minlight;

    // red minlight colors for this face
    std::array<uint8_t, 3> minlight_color;

    // if non zero, overrides _phong_angle for concave joints
    uint8_t phong_angle_concave;

    // custom opacity
    uint8_t light_alpha;

    constexpr bool needs_write() const
    {
        return (extended & ~(TEX_EXFLAG_SKIP | TEX_EXFLAG_HINT)) || phong_angle || minlight || minlight_color[0] ||
               minlight_color[1] || minlight_color[2] || phong_angle_concave || light_alpha;
    }

    constexpr auto as_tuple() const
    {
        return std::tie(native, extended, phong_angle, minlight, minlight_color, phong_angle_concave, light_alpha);
    }

    constexpr bool operator<(const surfflags_t &other) const { return as_tuple() < other.as_tuple(); }

    constexpr bool operator>(const surfflags_t &other) const { return as_tuple() > other.as_tuple(); }
};

// header before tightly packed surfflags_t[num_texinfo]
struct extended_flags_header_t
{
    uint32_t num_texinfo;
    uint32_t surfflags_size; // sizeof(surfflags_t)
};

using texvecf = std::array<std::array<float, 4>, 2>;

struct gtexinfo_t
{
    texvecf vecs; // [s/t][xyz offset]
    surfflags_t flags; // native miptex flags + extended flags

    // q1 only
    int32_t miptex;

    // q2 only
    int32_t value; // light emission, etc
    std::array<char, 32> texture; // texture name (textures/*.wal)
    int32_t nexttexinfo; // for animations, -1 = end of chain
};

struct texinfo_t
{
    texvecf vecs; /* [s/t][xyz offset] */
    int32_t miptex;
    int32_t flags;

    texinfo_t() = default;

    // convert from mbsp_t
    texinfo_t(const gtexinfo_t &model) :
        vecs(model.vecs),
        miptex(model.miptex),
        flags(model.flags.native)
    {
    }

    // convert to mbsp_t
    operator gtexinfo_t() const
    {
        return {
            vecs,
            { flags },
            miptex
        };
    }
};

struct q2_texinfo_t
{
    texvecf vecs; // [s/t][xyz offset]
    int32_t flags; // miptex flags + overrides
    int32_t value; // light emission, etc
    std::array<char, 32> texture; // texture name (textures/*.wal)
    int32_t nexttexinfo; // for animations, -1 = end of chain

    q2_texinfo_t() = default;

    // convert from mbsp_t
    q2_texinfo_t(const gtexinfo_t &model) :
        vecs(model.vecs),
        flags(model.flags.native),
        value(model.value),
        texture(model.texture),
        nexttexinfo(model.nexttexinfo)
    {
    }

    // convert to mbsp_t
    operator gtexinfo_t() const
    {
        return {
            vecs,
            { flags },
            -1,
            value,
            texture,
            nexttexinfo
        };
    }

    // serialize for streams
    auto stream_data()
    {
        return std::tie(vecs, flags, value, texture, nexttexinfo);
    }
};

/*
 * Note that edge 0 is never used, because negative edge nums are used for
 * counterclockwise use of the edge in a face
 */
using bsp29_dedge_t = std::array<uint16_t, 2>; /* vertex numbers */
using bsp2_dedge_t = std::array<uint32_t, 2>; /* vertex numbers */

constexpr size_t MAXLIGHTMAPS = 4;

struct mface_t
{
    int64_t planenum;
    int32_t side; // if true, the face is on the back side of the plane
    int32_t firstedge; /* we must support > 64k edges */
    int32_t numedges;
    int32_t texinfo;

    /* lighting info */
    std::array<uint8_t, MAXLIGHTMAPS> styles;
    int32_t lightofs; /* start of [numstyles*surfsize] samples */
};

struct bsp29_dface_t
{
    int16_t planenum;
    int16_t side;
    int32_t firstedge; /* we must support > 64k edges */
    int16_t numedges;
    int16_t texinfo;

    /* lighting info */
    std::array<uint8_t, MAXLIGHTMAPS> styles;
    int32_t lightofs; /* start of [numstyles*surfsize] samples */

    bsp29_dface_t() = default;

    // convert from mbsp_t
    bsp29_dface_t(const mface_t &model) :
        planenum(numeric_cast<int16_t>(model.planenum)),
        side(numeric_cast<int16_t>(model.side)),
        firstedge(model.firstedge),
        numedges(numeric_cast<int16_t>(model.numedges)),
        texinfo(numeric_cast<int16_t>(model.texinfo)),
        styles(model.styles),
        lightofs(model.lightofs)
    {
    }

    // convert to mbsp_t
    operator mface_t() const
    {
        return {
            planenum,
            side,
            firstedge,
            numedges,
            texinfo,
            styles,
            lightofs
        };
    }
};

struct bsp2_dface_t
{
    int32_t planenum;
    int32_t side; // if true, the face is on the back side of the plane
    int32_t firstedge; /* we must support > 64k edges */
    int32_t numedges;
    int32_t texinfo;

    /* lighting info */
    std::array<uint8_t, MAXLIGHTMAPS> styles;
    int32_t lightofs; /* start of [numstyles*surfsize] samples */

    bsp2_dface_t() = default;

    // convert from mbsp_t
    bsp2_dface_t(const mface_t &model) :
        planenum(numeric_cast<int32_t>(model.planenum)),
        side(model.side),
        firstedge(model.firstedge),
        numedges(model.numedges),
        texinfo(model.texinfo),
        styles(model.styles),
        lightofs(model.lightofs)
    {
    }

    // convert to mbsp_t
    operator mface_t() const
    {
        return {
            planenum,
            side,
            firstedge,
            numedges,
            texinfo,
            styles,
            lightofs
        };
    }
};

struct q2_dface_t
{
    uint16_t planenum; // NOTE: only difference from bsp29_dface_t
    int16_t side;
    int32_t firstedge; // we must support > 64k edges
    int16_t numedges;
    int16_t texinfo;

    // lighting info
    std::array<uint8_t, MAXLIGHTMAPS> styles;
    int32_t lightofs; // start of [numstyles*surfsize] samples

    q2_dface_t() = default;

    // convert from mbsp_t
    q2_dface_t(const mface_t &model) :
        planenum(numeric_cast<uint16_t>(model.planenum)),
        side(numeric_cast<int16_t>(model.side)),
        firstedge(model.firstedge),
        numedges(numeric_cast<int16_t>(model.numedges)),
        texinfo(numeric_cast<int16_t>(model.texinfo)),
        styles(model.styles),
        lightofs(model.lightofs)
    {
    }

    // convert to mbsp_t
    operator mface_t() const
    {
        return {
            planenum,
            side,
            firstedge,
            numedges,
            texinfo,
            styles,
            lightofs
        };
    }

    // serialize for streams
    auto stream_data()
    {
        return std::tie(planenum, side, firstedge, numedges, texinfo, styles, lightofs);
    }
};

struct q2_dface_qbism_t
{
    uint32_t planenum; // NOTE: only difference from bsp2_dface_t
    int32_t side;
    int32_t firstedge; // we must support > 64k edges
    int32_t numedges;
    int32_t texinfo;

    // lighting info
    std::array<uint8_t, MAXLIGHTMAPS> styles;
    int32_t lightofs; // start of [numstyles*surfsize] samples

    q2_dface_qbism_t() = default;

    // convert from mbsp_t
    q2_dface_qbism_t(const mface_t &model) :
        planenum(numeric_cast<uint32_t>(model.planenum)),
        side(model.side),
        firstedge(model.firstedge),
        numedges(model.numedges),
        texinfo(model.texinfo),
        styles(model.styles),
        lightofs(model.lightofs)
    {
    }

    // convert to mbsp_t
    operator mface_t() const
    {
        return {
            planenum,
            side,
            firstedge,
            numedges,
            texinfo,
            styles,
            lightofs
        };
    }
};

/*
 * leaf 0 is the generic CONTENTS_SOLID leaf, used for all solid areas (except Q2)
 * all other leafs need visibility info
 */
/* Ambient Sounds */
#define AMBIENT_WATER 0
#define AMBIENT_SKY 1
#define AMBIENT_SLIME 2
#define AMBIENT_LAVA 3
constexpr size_t NUM_AMBIENTS = 4;

struct mleaf_t
{
    // bsp2_dleaf_t
    int32_t contents;
    int32_t visofs; /* -1 = no visibility info */
    bspvec3f_t mins; /* for frustum culling     */
    bspvec3f_t maxs;
    uint32_t firstmarksurface;
    uint32_t nummarksurfaces;
    std::array<uint8_t, NUM_AMBIENTS> ambient_level;

    // q2 extras
    int32_t cluster;
    int32_t area;
    uint32_t firstleafbrush;
    uint32_t numleafbrushes;
};

struct bsp29_dleaf_t
{
    int32_t contents;
    int32_t visofs; /* -1 = no visibility info */
    bspvec3s_t mins; /* for frustum culling     */
    bspvec3s_t maxs;
    uint16_t firstmarksurface;
    uint16_t nummarksurfaces;
    std::array<uint8_t, NUM_AMBIENTS> ambient_level;

    bsp29_dleaf_t() = default;

    // convert from mbsp_t
    bsp29_dleaf_t(const mleaf_t &model) :
        contents(model.contents),
        visofs(model.visofs),
        mins(aabb_mins_cast<int16_t>(model.mins)),
        maxs(aabb_maxs_cast<int16_t>(model.maxs)),
        firstmarksurface(numeric_cast<uint16_t>(model.firstmarksurface)),
        nummarksurfaces(numeric_cast<uint16_t>(model.nummarksurfaces)),
        ambient_level(model.ambient_level)
    {
    }

    // convert to mbsp_t
    operator mleaf_t() const
    {
        return {
            contents,
            visofs,
            aabb_mins_cast<float>(mins),
            aabb_mins_cast<float>(maxs),
            firstmarksurface,
            nummarksurfaces,
            ambient_level
        };
    }
};

struct bsp2rmq_dleaf_t
{
    int32_t contents;
    int32_t visofs; /* -1 = no visibility info */
    bspvec3s_t mins; /* for frustum culling     */
    bspvec3s_t maxs;
    uint32_t firstmarksurface;
    uint32_t nummarksurfaces;
    std::array<uint8_t, NUM_AMBIENTS> ambient_level;

    bsp2rmq_dleaf_t() = default;

    // convert from mbsp_t
    bsp2rmq_dleaf_t(const mleaf_t &model) :
        contents(model.contents),
        visofs(model.visofs),
        mins(aabb_mins_cast<int16_t>(model.mins)),
        maxs(aabb_maxs_cast<int16_t>(model.maxs)),
        firstmarksurface(model.firstmarksurface),
        nummarksurfaces(model.nummarksurfaces),
        ambient_level(model.ambient_level)
    {
    }

    // convert to mbsp_t
    operator mleaf_t() const
    {
        return {
            contents,
            visofs,
            aabb_mins_cast<float>(mins),
            aabb_mins_cast<float>(maxs),
            firstmarksurface,
            nummarksurfaces,
            ambient_level
        };
    }
};

struct bsp2_dleaf_t
{
    int32_t contents;
    int32_t visofs; /* -1 = no visibility info */
    bspvec3f_t mins; /* for frustum culling     */
    bspvec3f_t maxs;
    uint32_t firstmarksurface;
    uint32_t nummarksurfaces;
    std::array<uint8_t, NUM_AMBIENTS> ambient_level;

    bsp2_dleaf_t() = default;

    // convert from mbsp_t
    bsp2_dleaf_t(const mleaf_t &model) :
        contents(model.contents),
        visofs(model.visofs),
        mins(model.mins),
        maxs(model.maxs),
        firstmarksurface(model.firstmarksurface),
        nummarksurfaces(model.nummarksurfaces),
        ambient_level(model.ambient_level)
    {
    }

    // convert to mbsp_t
    operator mleaf_t() const
    {
        return {
            contents,
            visofs,
            mins,
            maxs,
            firstmarksurface,
            nummarksurfaces,
            ambient_level
        };
    }
};

struct q2_dleaf_t
{
    int32_t contents; // OR of all brushes (not needed?)

    int16_t cluster;
    int16_t area;

    bspvec3s_t mins; // for frustum culling
    bspvec3s_t maxs;

    uint16_t firstleafface;
    uint16_t numleaffaces;

    uint16_t firstleafbrush;
    uint16_t numleafbrushes;

    q2_dleaf_t() = default;

    // convert from mbsp_t
    q2_dleaf_t(const mleaf_t &model) :
        contents(model.contents),
        cluster(numeric_cast<int16_t>(model.cluster)),
        area(numeric_cast<int16_t>(model.area)),
        mins(aabb_mins_cast<int16_t>(model.mins)),
        maxs(aabb_mins_cast<int16_t>(model.maxs)),
        firstleafface(numeric_cast<uint16_t>(model.firstmarksurface)),
        numleaffaces(numeric_cast<uint16_t>(model.nummarksurfaces)),
        firstleafbrush(numeric_cast<uint16_t>(model.firstleafbrush)),
        numleafbrushes(numeric_cast<uint16_t>(model.numleafbrushes))
    {
    }

    // convert to mbsp_t
    operator mleaf_t() const
    {
        return {
            contents,
            -1,
            aabb_mins_cast<float>(mins),
            aabb_mins_cast<float>(maxs),
            firstleafface,
            numleaffaces,
            {},
            cluster,
            area,
            firstleafbrush,
            numleafbrushes
        };
    }

    // serialize for streams
    auto stream_data()
    {
        return std::tie(contents, cluster, area, mins, maxs, firstleafface, numleaffaces, firstleafbrush, numleafbrushes);
    }
};

struct q2_dleaf_qbism_t
{
    int32_t contents; // OR of all brushes (not needed?)

    int32_t cluster;
    int32_t area;

    bspvec3f_t mins; // for frustum culling
    bspvec3f_t maxs;

    uint32_t firstleafface;
    uint32_t numleaffaces;

    uint32_t firstleafbrush;
    uint32_t numleafbrushes;

    q2_dleaf_qbism_t() = default;

    // convert from mbsp_t
    q2_dleaf_qbism_t(const mleaf_t &model) :
        contents(model.contents),
        cluster(model.cluster),
        area(model.area),
        mins(model.mins),
        maxs(model.maxs),
        firstleafface(model.firstmarksurface),
        numleaffaces(model.nummarksurfaces),
        firstleafbrush(model.firstleafbrush),
        numleafbrushes(model.numleafbrushes)
    {
    }

    // convert to mbsp_t
    operator mleaf_t() const
    {
        return {
            contents,
            -1,
            mins,
            maxs,
            firstleafface,
            numleaffaces,
            {},
            cluster,
            area,
            firstleafbrush,
            numleafbrushes
        };
    }
};

struct q2_dbrushside_qbism_t
{
    uint32_t planenum; // facing out of the leaf
    int32_t texinfo;
};

struct q2_dbrushside_t
{
    uint16_t planenum; // facing out of the leaf
    int16_t texinfo;

    q2_dbrushside_t() = default;

    // convert from mbsp_t
    q2_dbrushside_t(const q2_dbrushside_qbism_t &model) :
        planenum(numeric_cast<uint16_t>(model.planenum)),
        texinfo(numeric_cast<int16_t>(model.texinfo))
    {
    }

    // convert to mbsp_t
    operator q2_dbrushside_qbism_t() const
    {
        return {
            planenum,
            texinfo
        };
    }

    // serialize for streams
    auto stream_data()
    {
        return std::tie(planenum, texinfo);
    }
};

struct dbrush_t
{
    int32_t firstside;
    int32_t numsides;
    int32_t contents;

    // serialize for streams
    auto stream_data()
    {
        return std::tie(firstside, numsides, contents);
    }
};

enum vistype_t
{
    VIS_PVS,
    VIS_PHS
};

// the visibility lump consists of a header with a count, then
// byte offsets for the PVS and PHS of each cluster, then the raw
// compressed bit vectors.
struct mvis_t
{
    int32_t numclusters;
    std::vector<std::array<int32_t, 2>> bit_offsets;
    std::vector<uint8_t> bits;

    inline size_t header_offset() const
    {
        return sizeof(numclusters) + (sizeof(int32_t) * numclusters * 2);
    }

    // set a bit offset of the specified cluster/vistype *relative to the start of the bits array*
    // (after the header)
    inline void set_bit_offset(vistype_t type, size_t cluster, size_t offset)
    {
        bit_offsets[cluster][type] = offset + header_offset();
    }

    // fetch the bit offset of the specified cluster/vistype
    // relative to the start of the bits array
    inline int32_t get_bit_offset(vistype_t type, size_t cluster)
    {
        return bit_offsets[cluster][type] - header_offset();
    }

    void resize(size_t numclusters)
    {
        this->numclusters = numclusters;
        bit_offsets.resize(numclusters);
    }

    void stream_read(std::istream &stream, size_t lump_size)
    {
        auto start = stream.tellg();
        stream >= numclusters;

        bit_offsets.resize(numclusters);

        // read cluster -> offset tables
        for (auto &bit_offset : bit_offsets)
            stream >= bit_offset;

        // pull in final bit set
        auto remaining = lump_size - (stream.tellg() - start);
        bits.resize(remaining);
        stream.read(reinterpret_cast<char *>(bits.data()), remaining);
    }

    void stream_write(std::ostream &stream) const
    {
        stream <= numclusters;

        // write cluster -> offset tables
        for (auto &bit_offset : bit_offsets)
            stream <= bit_offset;

        // write bitset
        stream.write(reinterpret_cast<const char *>(bits.data()), bits.size());
    }
};

// each area has a list of portals that lead into other areas
// when portals are closed, other areas may not be visible or
// hearable even if the vis info says that it should be
struct dareaportal_t
{
    int32_t portalnum;
    int32_t otherarea;

    // serialize for streams
    auto stream_data()
    {
        return std::tie(portalnum, otherarea);
    }
};

struct darea_t
{
    int32_t numareaportals;
    int32_t firstareaportal;

    // serialize for streams
    auto stream_data()
    {
        return std::tie(numareaportals, firstareaportal);
    }
};

/* ========================================================================= */

struct bspxentry_t
{
    std::array<char, 24> lumpname;
    const void *lumpdata;
    size_t lumpsize;

    bspxentry_t *next;
};

// this is just temporary until the types use typesafe
// containers.
#define IMPL_MOVE_COPY(T) \
    T() { memset(this, 0, sizeof(*this)); } \
    T(T &&move) { memcpy(this, &move, sizeof(move)); memset(&move, 0, sizeof(move)); } \
    T(const T &copy) { memcpy(this, &copy, sizeof(copy)); } \
    T &operator=(T &&move) { memcpy(this, &move, sizeof(move)); memset(&move, 0, sizeof(move)); return *this; } \
    T &operator=(const T &copy) { memcpy(this, &copy, sizeof(copy)); return *this; }

struct bsp29_t
{
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

    IMPL_MOVE_COPY(bsp29_t);

    ~bsp29_t()
    {
        delete[] dmodels_q;
        delete[] dmodels_h2;
        delete[] dvisdata;
        delete[] dlightdata;
        delete[] dtexdata;
        delete[] dentdata;
        delete[] dleafs;
        delete[] dplanes;
        delete[] dvertexes;
        delete[] dnodes;
        delete[] texinfo;
        delete[] dfaces;
        delete[] dclipnodes;
        delete[] dedges;
        delete[] dmarksurfaces;
        delete[] dsurfedges;
    }
};

struct bsp2rmq_t
{
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

    IMPL_MOVE_COPY(bsp2rmq_t);

    ~bsp2rmq_t()
    {
        delete[] dmodels_q;
        delete[] dmodels_h2;
        delete[] dvisdata;
        delete[] dlightdata;
        delete[] dtexdata;
        delete[] dentdata;
        delete[] dleafs;
        delete[] dplanes;
        delete[] dvertexes;
        delete[] dnodes;
        delete[] texinfo;
        delete[] dfaces;
        delete[] dclipnodes;
        delete[] dedges;
        delete[] dmarksurfaces;
        delete[] dsurfedges;
    }
};

struct bsp2_t
{
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

    IMPL_MOVE_COPY(bsp2_t);

    ~bsp2_t()
    {
        delete[] dmodels_q;
        delete[] dmodels_h2;
        delete[] dvisdata;
        delete[] dlightdata;
        delete[] dtexdata;
        delete[] dentdata;
        delete[] dleafs;
        delete[] dplanes;
        delete[] dvertexes;
        delete[] dnodes;
        delete[] texinfo;
        delete[] dfaces;
        delete[] dclipnodes;
        delete[] dedges;
        delete[] dmarksurfaces;
        delete[] dsurfedges;
    }
};

struct q2bsp_t
{
    std::vector<q2_dmodel_t> dmodels;

    mvis_t dvis;

    std::vector<uint8_t> dlightdata;
    std::string dentdata;
    std::vector<q2_dleaf_t> dleafs;
    std::vector<dplane_t> dplanes;
    std::vector<dvertex_t> dvertexes;
    std::vector<q2_dnode_t> dnodes;
    std::vector<q2_texinfo_t> texinfo;
    std::vector<q2_dface_t> dfaces;
    std::vector<bsp29_dedge_t> dedges;
    std::vector<uint16_t> dleaffaces;
    std::vector<uint16_t> dleafbrushes;
    std::vector<int32_t> dsurfedges;
    std::vector<darea_t> dareas;
    std::vector<dareaportal_t> dareaportals;
    std::vector<dbrush_t> dbrushes;
    std::vector<q2_dbrushside_t> dbrushsides;
};

struct q2bsp_qbism_t
{
    int nummodels;
    q2_dmodel_t *dmodels;

    mvis_t dvis;

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

    IMPL_MOVE_COPY(q2bsp_qbism_t);

    ~q2bsp_qbism_t()
    {
        delete[] dmodels;
        delete[] dlightdata;
        delete[] dentdata;
        delete[] dleafs;
        delete[] dplanes;
        delete[] dvertexes;
        delete[] dnodes;
        delete[] texinfo;
        delete[] dfaces;
        delete[] dedges;
        delete[] dleaffaces;
        delete[] dleafbrushes;
        delete[] dsurfedges;
        delete[] dareas;
        delete[] dareaportals;
        delete[] dbrushes;
        delete[] dbrushsides;
    }
};

struct bspversion_t;

struct mbsp_t
{
    const bspversion_t *loadversion;

    std::vector<dmodelh2_t> dmodels;

    mvis_t dvis;

    std::vector<uint8_t> dlightdata;

    int texdatasize;
    dmiptexlump_t *dtexdata;

    int rgbatexdatasize; // mxd
    dmiptexlump_t *drgbatexdata; // mxd. Followed by rgba_miptex_t structs

    std::string dentdata;
    std::vector<mleaf_t> dleafs;
    std::vector<dplane_t> dplanes;
    std::vector<dvertex_t> dvertexes;
    std::vector<bsp2_dnode_t> dnodes;
    std::vector<gtexinfo_t> texinfo;
    std::vector<mface_t> dfaces;
    std::vector<bsp2_dclipnode_t> dclipnodes;
    std::vector<bsp2_dedge_t> dedges;
    std::vector<uint32_t> dleaffaces;
    std::vector<uint32_t> dleafbrushes;
    std::vector<int32_t> dsurfedges;
    std::vector<darea_t> dareas;
    std::vector<dareaportal_t> dareaportals;
    std::vector<dbrush_t> dbrushes;
    std::vector<q2_dbrushside_qbism_t> dbrushsides;
}; // "generic" bsp - superset of all other supported types

struct dheader_t
{
    int32_t version;
    std::array<lump_t, BSP_LUMPS> lumps;

    auto stream_data()
    {
        return std::tie(version, lumps);
    }
};

struct q2_dheader_t
{
    int32_t ident;
    int32_t version;
    std::array<lump_t, Q2_HEADER_LUMPS> lumps;

    auto stream_data()
    {
        return std::tie(ident, version, lumps);
    }
};

struct bspdata_t
{
    const bspversion_t *version, *loadversion;

    // Stay in monostate until a BSP type is requested.
    std::variant<std::monostate, mbsp_t, bsp29_t, bsp2rmq_t, bsp2_t, q2bsp_t, q2bsp_qbism_t> bsp;

    bspxentry_t *bspxentries;
};

// native game target ID
enum gameid_t
{
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
    gameid_t id;

    bool has_rgb_lightmap;

    const std::string base_dir;

    gamedef_t(const char *base_dir) :
        base_dir(base_dir)
    {
    }

    virtual bool surf_is_lightmapped(const surfflags_t &flags) const = 0;
    virtual bool surf_is_subdivided(const surfflags_t &flags) const = 0;
    virtual contentflags_t cluster_contents(const contentflags_t &contents0, const contentflags_t &contents1) const = 0;
    virtual int32_t get_content_type(const contentflags_t &contents) const = 0;
    virtual int32_t contents_priority(const contentflags_t &contents) const = 0;
    virtual contentflags_t create_empty_contents(const int32_t &cflags = 0) const = 0;
    virtual contentflags_t create_solid_contents(const int32_t &cflags = 0) const = 0;
    virtual contentflags_t create_sky_contents(const int32_t &cflags = 0) const = 0;
    virtual contentflags_t create_liquid_contents(const int32_t &liquid_type, const int32_t &cflags = 0) const = 0;
    virtual bool contents_are_empty(const contentflags_t &contents) const
    {
        return contents.native == create_empty_contents().native;
    }
    virtual bool contents_are_solid(const contentflags_t &contents) const
    {
        return contents.native == create_solid_contents().native;
    }
    virtual bool contents_are_sky(const contentflags_t &contents) const
    {
        return contents.native == create_sky_contents().native;
    }
    virtual bool contents_are_liquid(const contentflags_t &contents) const = 0;
    virtual bool contents_are_valid(const contentflags_t &contents, bool strict = true) const = 0;
    virtual bool portal_can_see_through(const contentflags_t &contents0, const contentflags_t &contents1) const = 0;
    virtual std::string get_contents_display(const contentflags_t &contents) const = 0;
    virtual const std::initializer_list<aabb3d> &get_hull_sizes() const = 0;
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
    /* lump specification */
    const lumpspec_t *lumps;
    /* game ptr */
    const gamedef_t *game;
    /* if we surpass the limits of this format, upgrade to this one */
    const bspversion_t *extended_limits;
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
constexpr const bspversion_t *const bspversions[] = {&bspver_generic, &bspver_q1, &bspver_h2, &bspver_h2bsp2,
    &bspver_h2bsp2rmq, &bspver_bsp2, &bspver_bsp2rmq, &bspver_hl, &bspver_q2, &bspver_qbism};

void LoadBSPFile(std::filesystem::path &filename, bspdata_t *bspdata); // returns the filename as contained inside a bsp
void WriteBSPFile(const std::filesystem::path &filename, bspdata_t *bspdata);
void PrintBSPFileSizes(const bspdata_t *bspdata);
/**
 * Returns false if the conversion failed.
 */
bool ConvertBSPFormat(bspdata_t *bspdata, const bspversion_t *to_version);
void BSPX_AddLump(bspdata_t *bspdata, const char *xname, const void *xdata, size_t xsize);
const void *BSPX_GetLump(bspdata_t *bspdata, const char *xname, size_t *xsize);

void DecompressRow(const uint8_t *in, const int numbytes, uint8_t *decompressed);

int CompressRow(const uint8_t *vis, const int numbytes, uint8_t *out);
