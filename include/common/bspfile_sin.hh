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

#include <cinttypes>
#include <array>
#include <iosfwd>
#include <vector>
#include <string>

#include <common/qvec.hh>
#include <common/bitflags.hh>
#include <common/bspfile_common.hh>

constexpr int32_t SIN_BSPVERSION = 1;
constexpr int32_t SIN_BSPIDENT = (('P'<<24)+('S'<<16)+('B'<<8)+'R');

enum sin_lump_t
{
    SIN_LUMP_ENTITIES,
    SIN_LUMP_PLANES,
    SIN_LUMP_VERTEXES,
    SIN_LUMP_VISIBILITY,
    SIN_LUMP_NODES,
    SIN_LUMP_TEXINFO,
    SIN_LUMP_FACES,
    SIN_LUMP_LIGHTING,
    SIN_LUMP_LEAFS,
    SIN_LUMP_LEAFFACES,
    SIN_LUMP_LEAFBRUSHES,
    SIN_LUMP_EDGES,
    SIN_LUMP_SURFEDGES,
    SIN_LUMP_MODELS,
    SIN_LUMP_BRUSHES,
    SIN_LUMP_BRUSHSIDES,
    SIN_LUMP_POP,
    SIN_LUMP_AREAS,
    SIN_LUMP_AREAPORTALS,
    SIN_LUMP_LIGHTINFO,

    SIN_HEADER_LUMPS
};

struct sin_dheader_t
{
    int32_t ident;
    int32_t version;
    std::array<lump_t, SIN_HEADER_LUMPS> lumps;

    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s);
};

// SiN contents (from qfiles.h)

// contents flags are seperate bits
// a given brush can contribute multiple content bits
// multiple brushes can be in a single leaf

// lower bits are stronger, and will eat weaker brushes completely
enum sin_contents_t : int32_t
{
    SIN_CONTENTS_EMPTY = 0,
    SIN_CONTENTS_SOLID = nth_bit(0), // an eye is never valid in a solid
    SIN_CONTENTS_WINDOW = nth_bit(1), // translucent, but not watery
    SIN_CONTENTS_FENCE = nth_bit(2),
    SIN_CONTENTS_LAVA = nth_bit(3),
    SIN_CONTENTS_SLIME = nth_bit(4),
    SIN_CONTENTS_WATER = nth_bit(5),
    SIN_CONTENTS_MIST = nth_bit(6),

    SIN_CONTENTS_UNUSED_7 = nth_bit(7),
    SIN_CONTENTS_UNUSED_8 = nth_bit(8),
    SIN_CONTENTS_UNUSED_9 = nth_bit(9),
    SIN_CONTENTS_UNUSED_10 = nth_bit(10),
    SIN_CONTENTS_UNUSED_11 = nth_bit(11),
    SIN_CONTENTS_DUMMYFENCE = nth_bit(12),
    SIN_CONTENTS_UNUSED_13 = nth_bit(13),
    SIN_CONTENTS_UNUSED_14 = nth_bit(14),
    SIN_CONTENTS_UNUSED_15 = nth_bit(15),

    SIN_LAST_VISIBLE_CONTENTS = SIN_CONTENTS_MIST,
    SIN_ALL_VISIBLE_CONTENTS = SIN_CONTENTS_SOLID | SIN_CONTENTS_WINDOW | SIN_CONTENTS_FENCE | SIN_CONTENTS_LAVA |
                               SIN_CONTENTS_SLIME | SIN_CONTENTS_WATER | SIN_CONTENTS_MIST,

    SIN_CONTENTS_LIQUID = (SIN_CONTENTS_LAVA | SIN_CONTENTS_SLIME | SIN_CONTENTS_WATER), // mxd

    // remaining contents are non-visible, and don't eat brushes

    SIN_CONTENTS_AREAPORTAL = nth_bit(15),

    SIN_CONTENTS_PLAYERCLIP = nth_bit(16),
    SIN_CONTENTS_MONSTERCLIP = nth_bit(17),

    // currents can be added to any other contents, and may be mixed
    SIN_CONTENTS_CURRENT_0 = nth_bit(18),
    SIN_CONTENTS_CURRENT_90 = nth_bit(19),
    SIN_CONTENTS_CURRENT_180 = nth_bit(20),
    SIN_CONTENTS_CURRENT_270 = nth_bit(21),
    SIN_CONTENTS_CURRENT_UP = nth_bit(22),
    SIN_CONTENTS_CURRENT_DOWN = nth_bit(23),

    SIN_CONTENTS_ORIGIN = nth_bit(24), // removed before bsping an entity

    SIN_CONTENTS_MONSTER = nth_bit(25), // should never be on a brush, only in game
    SIN_CONTENTS_DEADMONSTER = nth_bit(26),
    SIN_CONTENTS_DETAIL = nth_bit(27), // brushes to be added after vis leafs
    SIN_CONTENTS_TRANSLUCENT = nth_bit(28), // auto set if any surface has trans
    SIN_CONTENTS_LADDER = nth_bit(29),

    SIN_CONTENTS_UNUSED_30 = nth_bit(30),
    SIN_CONTENTS_UNUSED_31 = nth_bit(31)
};

// Q2 Texture flags.
enum sin_surf_flags_t : int32_t
{
    SIN_SURF_LIGHT = nth_bit(0), // value will hold the light strength

    SIN_SURF_MASKED = nth_bit(1), // surface texture is masked

    SIN_SURF_SKY = nth_bit(2), // don't draw, but add to skybox
    SIN_SURF_WARP = nth_bit(3), // turbulent water warp
    SIN_SURF_NONLIT = nth_bit(4), // surface is not lit
    SIN_SURF_NOFILTER = nth_bit(5), // surface is not bi-linear filtered
    SIN_SURF_CONVEYOR = nth_bit(6), // scroll towards angle
    SIN_SURF_NODRAW = nth_bit(7), // don't bother referencing the texture

    SIN_SURF_HINT = nth_bit(8), // make a primary bsp splitter
    SIN_SURF_SKIP = nth_bit(9), // ONLY FOR HINT! "nodraw" = Q1 "skip"

    SIN_SURF_WAVY = nth_bit(10), // surface has waves
    SIN_SURF_RICOCHET = nth_bit(11), // projectiles bounce literally bounce off this surface
    SIN_SURF_PRELIT		     = nth_bit(12), // surface has intensity information for pre-lighting
    SIN_SURF_MIRROR		     = nth_bit(13), // surface is a mirror
    SIN_SURF_CONSOLE         = nth_bit(14), // surface is a console
    SIN_SURF_USECOLOR        = nth_bit(15), // surface is lit with non-lit * color
    SIN_SURF_HARDWAREONLY    = nth_bit(16), // surface has been damaged
    SIN_SURF_DAMAGE          = nth_bit(17), // surface can be damaged
    SIN_SURF_WEAK            = nth_bit(18), // surface has weak hit points
    SIN_SURF_NORMAL          = nth_bit(19), // surface has normal hit points
    SIN_SURF_ADD             = nth_bit(20), // surface will be additive
    SIN_SURF_ENVMAPPED       = nth_bit(21), // surface is envmapped
    SIN_SURF_RANDOMANIMATE   = nth_bit(22), // surface start animating on a random frame
    SIN_SURF_ANIMATE         = nth_bit(23), // surface animates
    SIN_SURF_RNDTIME         = nth_bit(24), // time between animations is random
    SIN_SURF_TRANSLATE       = nth_bit(25), // surface translates
    SIN_SURF_NOMERGE         = nth_bit(26), // surface is not merged in csg phase
    SIN_SURF_TYPE_BIT0       = nth_bit(27), // 0 bit of surface type
    SIN_SURF_TYPE_BIT1       = nth_bit(28), // 1 bit of surface type
    SIN_SURF_TYPE_BIT2       = nth_bit(29), // 2 bit of surface type
    SIN_SURF_TYPE_BIT3       = nth_bit(30), // 3 bit of surface type

    SIN_SURF_START_BIT = 27
};

#define SIN_SURFACETYPE_FROM_FLAGS(x) (((x) >> (SIN_SURF_START_BIT)) & 0xf )
#define SIN_SURF_TYPE_SHIFT(x)         ((x) << (SIN_SURF_START_BIT)) // macro for getting proper bit mask

enum sin_surf_type_t : int32_t
{
    SIN_SURF_TYPE_NONE       = SIN_SURF_TYPE_SHIFT(0),
    SIN_SURF_TYPE_WOOD       = SIN_SURF_TYPE_SHIFT(1),
    SIN_SURF_TYPE_METAL      = SIN_SURF_TYPE_SHIFT(2),
    SIN_SURF_TYPE_STONE      = SIN_SURF_TYPE_SHIFT(3),
    SIN_SURF_TYPE_CONCRETE   = SIN_SURF_TYPE_SHIFT(4),
    SIN_SURF_TYPE_DIRT       = SIN_SURF_TYPE_SHIFT(5),
    SIN_SURF_TYPE_FLESH      = SIN_SURF_TYPE_SHIFT(6),
    SIN_SURF_TYPE_GRILL      = SIN_SURF_TYPE_SHIFT(7),
    SIN_SURF_TYPE_GLASS      = SIN_SURF_TYPE_SHIFT(8),
    SIN_SURF_TYPE_FABRIC     = SIN_SURF_TYPE_SHIFT(9),
    SIN_SURF_TYPE_MONITOR    = SIN_SURF_TYPE_SHIFT(10),
    SIN_SURF_TYPE_GRAVEL     = SIN_SURF_TYPE_SHIFT(11),
    SIN_SURF_TYPE_VEGETATION = SIN_SURF_TYPE_SHIFT(12),
    SIN_SURF_TYPE_PAPER      = SIN_SURF_TYPE_SHIFT(13),
    SIN_SURF_TYPE_DUCT       = SIN_SURF_TYPE_SHIFT(14),
    SIN_SURF_TYPE_WATER      = SIN_SURF_TYPE_SHIFT(15)
};

struct sin_texinfo_t
{
    texvecf vecs; // [s/t][xyz offset]
    int32_t flags; // miptex flags + overrides
    std::array<char, 64> texture; // texture name (textures/*.wal)
    int32_t nexttexinfo; // for animations, -1 = end of chain
    float trans_mag;
    int trans_angle;
    int base_angle;
    float animtime;
    float nonlit;
    float translucence;
    float friction;
    float restitution;
    qvec3f color;
    std::array<char, 32> groupname;

    sin_texinfo_t() = default;

    // convert from mbsp_t
    sin_texinfo_t(const mtexinfo_t &model);

    // convert to mbsp_t
    operator mtexinfo_t() const;

    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s);
};

struct sin_dface_t
{
    uint16_t planenum; // NOTE: only difference from bsp29_dface_t
    int16_t side;
    int32_t firstedge; // we must support > 64k edges
    int16_t numedges;
    int16_t texinfo;

    // lighting info
    std::array<uint8_t, MAXLIGHTMAPS> styles;
    int32_t lightofs; // start of [numstyles*surfsize] samples
    int32_t lightinfo;

    sin_dface_t() = default;

    // convert from mbsp_t
    sin_dface_t(const mface_t &model);

    // convert to mbsp_t
    operator mface_t() const;

    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s);
};

struct sin_dbrushside_t : q2_dbrushside_t
{
    int32_t lightinfo;

    sin_dbrushside_t() = default;

    // convert from mbsp_t
    sin_dbrushside_t(const q2_dbrushside_qbism_t &model);

    // convert to mbsp_t
    operator q2_dbrushside_qbism_t() const;

    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s);
};

// type tag used for type inference
struct sinbsp_tag_t
{
};

struct sinbsp_t : sinbsp_tag_t
{
    std::vector<q2_dmodel_t> dmodels;

    mvis_t dvis;

    std::vector<uint8_t> dlightdata;
    std::string dentdata;
    std::vector<q2_dleaf_t> dleafs;
    std::vector<dplane_t> dplanes;
    std::vector<qvec3f> dvertexes;
    std::vector<q2_dnode_t> dnodes;
    std::vector<sin_texinfo_t> texinfo;
    std::vector<sin_dface_t> dfaces;
    std::vector<bsp29_dedge_t> dedges;
    std::vector<uint16_t> dleaffaces;
    std::vector<uint16_t> dleafbrushes;
    std::vector<int32_t> dsurfedges;
    std::vector<darea_t> dareas;
    std::vector<dareaportal_t> dareaportals;
    std::vector<dbrush_t> dbrushes;
    std::vector<sin_dbrushside_t> dbrushsides;
    std::vector<sin_lightinfo_t> dlightinfo;
};

extern const bspversion_t bspver_sin;
