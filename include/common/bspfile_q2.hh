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

constexpr int32_t Q2_BSPVERSION = 38;
constexpr int32_t Q2_BSPIDENT = (('P' << 24) + ('S' << 16) + ('B' << 8) + 'I');
constexpr int32_t Q2_QBISMIDENT = (('P' << 24) + ('S' << 16) + ('B' << 8) + 'Q');

enum q2_lump_t
{
    Q2_LUMP_ENTITIES,
    Q2_LUMP_PLANES,
    Q2_LUMP_VERTEXES,
    Q2_LUMP_VISIBILITY,
    Q2_LUMP_NODES,
    Q2_LUMP_TEXINFO,
    Q2_LUMP_FACES,
    Q2_LUMP_LIGHTING,
    Q2_LUMP_LEAFS,
    Q2_LUMP_LEAFFACES,
    Q2_LUMP_LEAFBRUSHES,
    Q2_LUMP_EDGES,
    Q2_LUMP_SURFEDGES,
    Q2_LUMP_MODELS,
    Q2_LUMP_BRUSHES,
    Q2_LUMP_BRUSHSIDES,
    Q2_LUMP_POP,
    Q2_LUMP_AREAS,
    Q2_LUMP_AREAPORTALS,

    Q2_HEADER_LUMPS
};

struct q2_dheader_t
{
    int32_t ident;
    int32_t version;
    std::array<lump_t, Q2_HEADER_LUMPS> lumps;

    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s);
};

struct q2_dmodel_t
{
    qvec3f mins;
    qvec3f maxs;
    qvec3f origin; // for sounds or lights
    int32_t headnode;
    int32_t firstface;
    int32_t numfaces; // submodels just draw faces
                      // without walking the bsp tree

    q2_dmodel_t() = default;

    // convert from mbsp_t
    explicit q2_dmodel_t(const dmodelh2_t &model);

    // convert to mbsp_t
    explicit operator dmodelh2_t() const;

    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s);
};

// Q2 contents (from qfiles.h)

// contents flags are seperate bits
// a given brush can contribute multiple content bits
// multiple brushes can be in a single leaf

// lower bits are stronger, and will eat weaker brushes completely
enum q2_contents_t : int32_t
{
    Q2_CONTENTS_EMPTY = 0,
    Q2_CONTENTS_SOLID = nth_bit(0), // an eye is never valid in a solid
    Q2_CONTENTS_WINDOW = nth_bit(1), // translucent, but not watery
    Q2_CONTENTS_AUX = nth_bit(2),
    Q2_CONTENTS_LAVA = nth_bit(3),
    Q2_CONTENTS_SLIME = nth_bit(4),
    Q2_CONTENTS_WATER = nth_bit(5),
    Q2_CONTENTS_MIST = nth_bit(6),

    Q2_CONTENTS_UNUSED_7 = nth_bit(7),
    Q2_CONTENTS_UNUSED_8 = nth_bit(8),
    Q2_CONTENTS_UNUSED_9 = nth_bit(9),
    Q2_CONTENTS_UNUSED_10 = nth_bit(10),
    Q2_CONTENTS_UNUSED_11 = nth_bit(11),
    Q2_CONTENTS_UNUSED_12 = nth_bit(12),
    Q2_CONTENTS_NO_WATERJUMP = nth_bit(13), // liquid-only; re-release
    Q2_CONTENTS_PROJECTILECLIP = nth_bit(14), // like playerclip/monsterclip, but only affects projectiles

    Q2_LAST_VISIBLE_CONTENTS = Q2_CONTENTS_MIST,
    Q2_ALL_VISIBLE_CONTENTS = Q2_CONTENTS_SOLID | Q2_CONTENTS_WINDOW | Q2_CONTENTS_AUX | Q2_CONTENTS_LAVA |
                              Q2_CONTENTS_SLIME | Q2_CONTENTS_WATER | Q2_CONTENTS_MIST,

    Q2_CONTENTS_LIQUID = (Q2_CONTENTS_LAVA | Q2_CONTENTS_SLIME | Q2_CONTENTS_WATER), // mxd

    // remaining contents are non-visible, and don't eat brushes

    Q2_CONTENTS_AREAPORTAL = nth_bit(15),

    Q2_CONTENTS_PLAYERCLIP = nth_bit(16),
    Q2_CONTENTS_MONSTERCLIP = nth_bit(17),

    // currents can be added to any other contents, and may be mixed
    Q2_CONTENTS_CURRENT_0 = nth_bit(18),
    Q2_CONTENTS_CURRENT_90 = nth_bit(19),
    Q2_CONTENTS_CURRENT_180 = nth_bit(20),
    Q2_CONTENTS_CURRENT_270 = nth_bit(21),
    Q2_CONTENTS_CURRENT_UP = nth_bit(22),
    Q2_CONTENTS_CURRENT_DOWN = nth_bit(23),

    Q2_CONTENTS_ORIGIN = nth_bit(24), // removed before bsping an entity

    Q2_CONTENTS_MONSTER = nth_bit(25), // should never be on a brush, only in game
    Q2_CONTENTS_DEADMONSTER = nth_bit(26),
    Q2_CONTENTS_DETAIL = nth_bit(27), // brushes to be added after vis leafs
    Q2_CONTENTS_TRANSLUCENT = nth_bit(28), // auto set if any surface has trans
    Q2_CONTENTS_LADDER = nth_bit(29),

    Q2_CONTENTS_UNUSED_30 = nth_bit(30),
    Q2_CONTENTS_UNUSED_31 = nth_bit(31)
};

struct q2_dnode_t
{
    int32_t planenum;
    twosided<int32_t> children; // negative numbers are -(leafs+1), not nodes
    qvec3s mins; // for frustom culling
    qvec3s maxs;
    uint16_t firstface;
    uint16_t numfaces; // counting both sides

    q2_dnode_t() = default;

    // convert from mbsp_t
    explicit q2_dnode_t(const bsp2_dnode_t &model);

    // convert to mbsp_t
    explicit operator bsp2_dnode_t() const;

    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s);
};

using q2_dnode_qbism_t = bsp2_dnode_t;

// Q2 Texture flags.
enum q2_surf_flags_t : int32_t
{
    Q2_SURF_LIGHT = nth_bit(0), // value will hold the light strength

    Q2_SURF_SLICK = nth_bit(1), // effects game physics

    Q2_SURF_SKY = nth_bit(2), // don't draw, but add to skybox
    Q2_SURF_WARP = nth_bit(3), // turbulent water warp
    Q2_SURF_TRANS33 = nth_bit(4),
    Q2_SURF_TRANS66 = nth_bit(5),
    Q2_SURF_FLOWING = nth_bit(6), // scroll towards angle
    Q2_SURF_NODRAW = nth_bit(7), // don't bother referencing the texture

    Q2_SURF_HINT = nth_bit(8), // make a primary bsp splitter
    Q2_SURF_SKIP = nth_bit(9), // ONLY FOR HINT! "nodraw" = Q1 "skip"

    Q2_SURF_ALPHATEST = nth_bit(25), // Paril, KMQ2 flag
};

static constexpr int32_t Q2_SURF_ALL = Q2_SURF_LIGHT | Q2_SURF_SLICK | Q2_SURF_SKY | Q2_SURF_WARP | Q2_SURF_TRANS33 |
                                       Q2_SURF_TRANS66 | Q2_SURF_FLOWING | Q2_SURF_NODRAW | Q2_SURF_HINT |
                                       Q2_SURF_SKIP | Q2_SURF_ALPHATEST;

struct q2_texinfo_t
{
    texvecf vecs; // [s/t][xyz offset]
    int32_t flags; // miptex flags + overrides
    int32_t value; // light emission, etc
    std::array<char, 32> texture; // texture name (textures/*.wal)
    int32_t nexttexinfo; // for animations, -1 = end of chain

    q2_texinfo_t() = default;

    // convert from mbsp_t
    explicit q2_texinfo_t(const mtexinfo_t &model);

    // convert to mbsp_t
    explicit operator mtexinfo_t() const;

    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s);
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
    explicit q2_dface_t(const mface_t &model);

    // convert to mbsp_t
    explicit operator mface_t() const;

    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s);
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
    explicit q2_dface_qbism_t(const mface_t &model);

    // convert to mbsp_t
    explicit operator mface_t() const;

    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s);
};

struct q2_dleaf_t
{
    int32_t contents; // OR of all brushes (not needed?)

    int16_t cluster;
    int16_t area;

    qvec3s mins; // for frustum culling
    qvec3s maxs;

    uint16_t firstleafface;
    uint16_t numleaffaces;

    uint16_t firstleafbrush;
    uint16_t numleafbrushes;

    q2_dleaf_t() = default;

    // convert from mbsp_t
    explicit q2_dleaf_t(const mleaf_t &model);

    // convert to mbsp_t
    explicit operator mleaf_t() const;

    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s);
};

struct q2_dleaf_qbism_t
{
    int32_t contents; // OR of all brushes (not needed?)

    int32_t cluster;
    int32_t area;

    qvec3f mins; // for frustum culling
    qvec3f maxs;

    uint32_t firstleafface;
    uint32_t numleaffaces;

    uint32_t firstleafbrush;
    uint32_t numleafbrushes;

    q2_dleaf_qbism_t() = default;

    // convert from mbsp_t
    explicit q2_dleaf_qbism_t(const mleaf_t &model);

    // convert to mbsp_t
    explicit operator mleaf_t() const;

    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s);
};

struct q2_dbrushside_t
{
    uint16_t planenum; // facing out of the leaf
    int16_t texinfo;

    q2_dbrushside_t() = default;

    // convert from mbsp_t
    explicit q2_dbrushside_t(const q2_dbrushside_qbism_t &model);

    // convert to mbsp_t
    explicit operator q2_dbrushside_qbism_t() const;

    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s);
};

// type tag used for type inference
struct q2bsp_tag_t
{
};

struct q2bsp_t : q2bsp_tag_t
{
    std::vector<q2_dmodel_t> dmodels;

    mvis_t dvis;

    std::vector<uint8_t> dlightdata;
    std::string dentdata;
    std::vector<q2_dleaf_t> dleafs;
    std::vector<dplane_t> dplanes;
    std::vector<qvec3f> dvertexes;
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

struct q2bsp_qbism_t : q2bsp_tag_t
{
    std::vector<q2_dmodel_t> dmodels;
    mvis_t dvis;
    std::vector<uint8_t> dlightdata;
    std::string dentdata;
    std::vector<q2_dleaf_qbism_t> dleafs;
    std::vector<dplane_t> dplanes;
    std::vector<qvec3f> dvertexes;
    std::vector<q2_dnode_qbism_t> dnodes;
    std::vector<q2_texinfo_t> texinfo;
    std::vector<q2_dface_qbism_t> dfaces;
    std::vector<bsp2_dedge_t> dedges;
    std::vector<uint32_t> dleaffaces;
    std::vector<uint32_t> dleafbrushes;
    std::vector<int32_t> dsurfedges;
    std::vector<darea_t> dareas;
    std::vector<dareaportal_t> dareaportals;
    std::vector<dbrush_t> dbrushes;
    std::vector<q2_dbrushside_qbism_t> dbrushsides;
};

extern const bspversion_t bspver_q2;
extern const bspversion_t bspver_qbism;
