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
#include <vector>
#include <string>
#include <variant>
#include "qvec.hh"

constexpr int32_t BSPVERSION = 29;
constexpr int32_t BSP2RMQVERSION = (('B' << 24) | ('S' << 16) | ('P' << 8) | '2');
constexpr int32_t BSP2VERSION = ('B' | ('S' << 8) | ('P' << 16) | ('2' << 24));
constexpr int32_t BSPHLVERSION = 30; // 24bit lighting, and private palettes in the textures lump.

enum q1_lump_t
{
    LUMP_ENTITIES,
    LUMP_PLANES,
    LUMP_TEXTURES,
    LUMP_VERTEXES,
    LUMP_VISIBILITY,
    LUMP_NODES,
    LUMP_TEXINFO,
    LUMP_FACES,
    LUMP_LIGHTING,
    LUMP_CLIPNODES,
    LUMP_LEAFS,
    LUMP_MARKSURFACES,
    LUMP_EDGES,
    LUMP_SURFEDGES,
    LUMP_MODELS,

    BSP_LUMPS
};

struct dheader_t
{
    int32_t ident;
    std::array<lump_t, BSP_LUMPS> lumps;

    auto stream_data() { return std::tie(ident, lumps); }
};

constexpr size_t MAX_MAP_HULLS_Q1 = 4;

struct dmodelq1_t
{
    qvec3f mins;
    qvec3f maxs;
    qvec3f origin;
    std::array<int32_t, MAX_MAP_HULLS_Q1> headnode; /* 4 for backward compat, only 3 hulls exist */
    int32_t visleafs; /* not including the solid leaf 0 */
    int32_t firstface;
    int32_t numfaces;

    dmodelq1_t() = default;

    // convert from mbsp_t
    dmodelq1_t(const dmodelh2_t &model)
        : mins(model.mins), maxs(model.maxs), origin(model.origin),
          headnode(array_cast<decltype(headnode)>(model.headnode, "dmodelh2_t::headnode")), visleafs(model.visleafs),
          firstface(model.firstface), numfaces(model.numfaces)
    {
    }

    // convert to mbsp_t
    operator dmodelh2_t() const
    {
        return {mins, maxs, origin, array_cast<decltype(dmodelh2_t::headnode)>(headnode, "dmodelh2_t::headnode"),
            visleafs, firstface, numfaces};
    }

    // serialize for streams
    auto stream_data() { return std::tie(mins, maxs, origin, headnode, visleafs, firstface, numfaces); }
};

// Q1 contents

enum q1_contents_t : int32_t
{
    CONTENTS_EMPTY = -1,
    CONTENTS_SOLID = -2,
    CONTENTS_WATER = -3,
    CONTENTS_SLIME = -4,
    CONTENTS_LAVA = -5,
    CONTENTS_SKY = -6,

    CONTENTS_MIN = CONTENTS_SKY
};

struct bsp29_dnode_t
{
    int32_t planenum;
    std::array<int16_t, 2>
        children; /* negative numbers are -(leafs+1), not nodes. children[0] is front, children[1] is back */
    qvec3s mins; /* for sphere culling */
    qvec3s maxs;
    uint16_t firstface;
    uint16_t numfaces; /* counting both sides */

    bsp29_dnode_t() = default;

    // convert from mbsp_t
    bsp29_dnode_t(const bsp2_dnode_t &model)
        : planenum(model.planenum), children(array_cast<decltype(children)>(model.children, "dnode_t::children")),
          mins(aabb_mins_cast<int16_t>(model.mins, "dnode_t::mins")),
          maxs(aabb_maxs_cast<int16_t>(model.maxs, "dnode_t::maxs")),
          firstface(numeric_cast<uint16_t>(model.firstface, "dnode_t::firstface")),
          numfaces(numeric_cast<uint16_t>(model.numfaces, "dnode_t::numfaces"))
    {
    }

    // convert to mbsp_t
    operator bsp2_dnode_t() const
    {
        return {planenum, array_cast<decltype(bsp2_dnode_t::children)>(children, "dnode_t::children"),
            aabb_mins_cast<float>(mins, "dnode_t::mins"), aabb_mins_cast<float>(maxs, "dnode_t::maxs"), firstface,
            numfaces};
    }

    // serialize for streams
    auto stream_data() { return std::tie(planenum, children, mins, maxs, firstface, numfaces); }
};

struct bsp2rmq_dnode_t
{
    int32_t planenum;
    std::array<int32_t, 2> children; /* negative numbers are -(leafs+1), not nodes */
    qvec3s mins; /* for sphere culling */
    qvec3s maxs;
    uint32_t firstface;
    uint32_t numfaces; /* counting both sides */

    bsp2rmq_dnode_t() = default;

    // convert from mbsp_t
    bsp2rmq_dnode_t(const bsp2_dnode_t &model)
        : planenum(model.planenum), children(model.children),
          mins(aabb_mins_cast<int16_t>(model.mins, "dnode_t::mins")),
          maxs(aabb_maxs_cast<int16_t>(model.maxs, "dnode_t::maxs")), firstface(model.firstface),
          numfaces(model.numfaces)
    {
    }

    // convert to mbsp_t
    operator bsp2_dnode_t() const
    {
        return {planenum, children, aabb_mins_cast<float>(mins, "dnode_t::mins"),
            aabb_mins_cast<float>(maxs, "dnode_t::maxs"), firstface, numfaces};
    }

    // serialize for streams
    auto stream_data() { return std::tie(planenum, children, mins, maxs, firstface, numfaces); }
};

/*
 * Clipnodes need to be stored as a 16-bit offset. Originally, this was a
 * signed value and only the positive values up to 32767 were available. Since
 * the negative range was unused apart from a few values reserved for flags,
 * this has been extended to allow up to 65520 (0xfff0) clipnodes (with a
 * suitably modified engine).
 */
struct bsp29_dclipnode_t
{
    int32_t planenum;
    std::array<int16_t, 2> children; /* negative numbers are contents */

    bsp29_dclipnode_t() = default;

    // convert from mbsp_t
    bsp29_dclipnode_t(const bsp2_dclipnode_t &model)
        : planenum(model.planenum), children({downcast(model.children[0]), downcast(model.children[1])})
    {
    }

    // convert to mbsp_t
    operator bsp2_dclipnode_t() const { return {planenum, {upcast(children[0]), upcast(children[1])}}; }

    // serialize for streams
    auto stream_data() { return std::tie(planenum, children); }

    /* Slightly tricky since we support > 32k clipnodes */
private:
    static constexpr int16_t downcast(const int32_t &v)
    {
        if (v < -15 || v > 0xFFF0)
            throw std::overflow_error("dclipmode_t::children");

        return static_cast<int16_t>(v < 0 ? v + 0x10000 : v);
    }

    static constexpr int32_t upcast(const int16_t &v)
    {
        int32_t child = (uint16_t)v;
        return child > 0xfff0 ? child - 0x10000 : child;
    }
};

// Q1 Texture flags.
enum q1_surf_flags_t : int32_t
{
    TEX_SPECIAL = nth_bit(0) /* sky or slime, no lightmap or 256 subdivision */
};

struct texinfo_t
{
    texvecf vecs; /* [s/t][xyz offset] */
    int32_t miptex;
    int32_t flags;

    texinfo_t() = default;

    // convert from mbsp_t
    texinfo_t(const mtexinfo_t &model) : vecs(model.vecs), miptex(model.miptex), flags(model.flags.native) { }

    // convert to mbsp_t
    operator mtexinfo_t() const { return {vecs, {flags}, miptex}; }

    // serialize for streams
    auto stream_data() { return std::tie(vecs, miptex, flags); }
};

/*
 * Note that edge 0 is never used, because negative edge nums are used for
 * counterclockwise use of the edge in a face
 */
using bsp29_dedge_t = std::array<uint16_t, 2>; /* vertex numbers */

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
    bsp29_dface_t(const mface_t &model)
        : planenum(numeric_cast<int16_t>(model.planenum, "dface_t::planenum")),
          side(numeric_cast<int16_t>(model.side, "dface_t::side")), firstedge(model.firstedge),
          numedges(numeric_cast<int16_t>(model.numedges, "dface_t::numedges")),
          texinfo(numeric_cast<int16_t>(model.texinfo, "dface_t::texinfo")), styles(model.styles),
          lightofs(model.lightofs)
    {
    }

    // convert to mbsp_t
    operator mface_t() const { return {planenum, side, firstedge, numedges, texinfo, styles, lightofs}; }

    // serialize for streams
    auto stream_data() { return std::tie(planenum, side, firstedge, numedges, texinfo, styles, lightofs); }
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
    bsp2_dface_t(const mface_t &model)
        : planenum(numeric_cast<int32_t>(model.planenum, "dface_t::planenum")), side(model.side),
          firstedge(model.firstedge), numedges(model.numedges), texinfo(model.texinfo), styles(model.styles),
          lightofs(model.lightofs)
    {
    }

    // convert to mbsp_t
    operator mface_t() const { return {planenum, side, firstedge, numedges, texinfo, styles, lightofs}; }

    // serialize for streams
    auto stream_data() { return std::tie(planenum, side, firstedge, numedges, texinfo, styles, lightofs); }
};

struct bsp29_dleaf_t
{
    int32_t contents;
    int32_t visofs; /* -1 = no visibility info */
    qvec3s mins; /* for frustum culling     */
    qvec3s maxs;
    uint16_t firstmarksurface;
    uint16_t nummarksurfaces;
    std::array<uint8_t, NUM_AMBIENTS> ambient_level;

    bsp29_dleaf_t() = default;

    // convert from mbsp_t
    bsp29_dleaf_t(const mleaf_t &model)
        : contents(model.contents), visofs(model.visofs), mins(aabb_mins_cast<int16_t>(model.mins, "dleaf_t::mins")),
          maxs(aabb_maxs_cast<int16_t>(model.maxs, "dleaf_t::maxs")),
          firstmarksurface(numeric_cast<uint16_t>(model.firstmarksurface, "dleaf_t::firstmarksurface")),
          nummarksurfaces(numeric_cast<uint16_t>(model.nummarksurfaces, "dleaf_t::nummarksurfaces")),
          ambient_level(model.ambient_level)
    {
    }

    // convert to mbsp_t
    operator mleaf_t() const
    {
        return {contents, visofs, aabb_mins_cast<float>(mins, "dleaf_t::mins"),
            aabb_mins_cast<float>(maxs, "dleaf_t::maxs"), firstmarksurface, nummarksurfaces, ambient_level};
    }

    // serialize for streams
    auto stream_data()
    {
        return std::tie(contents, visofs, mins, maxs, firstmarksurface, nummarksurfaces, ambient_level);
    }
};

struct bsp2rmq_dleaf_t
{
    int32_t contents;
    int32_t visofs; /* -1 = no visibility info */
    qvec3s mins; /* for frustum culling     */
    qvec3s maxs;
    uint32_t firstmarksurface;
    uint32_t nummarksurfaces;
    std::array<uint8_t, NUM_AMBIENTS> ambient_level;

    bsp2rmq_dleaf_t() = default;

    // convert from mbsp_t
    bsp2rmq_dleaf_t(const mleaf_t &model)
        : contents(model.contents), visofs(model.visofs), mins(aabb_mins_cast<int16_t>(model.mins, "dleaf_t::mins")),
          maxs(aabb_maxs_cast<int16_t>(model.maxs, "dleaf_t::maxs")), firstmarksurface(model.firstmarksurface),
          nummarksurfaces(model.nummarksurfaces), ambient_level(model.ambient_level)
    {
    }

    // convert to mbsp_t
    operator mleaf_t() const
    {
        return {contents, visofs, aabb_mins_cast<float>(mins, "dleaf_t::mins"),
            aabb_mins_cast<float>(maxs, "dleaf_t::maxs"), firstmarksurface, nummarksurfaces, ambient_level};
    }

    // serialize for streams
    auto stream_data()
    {
        return std::tie(contents, visofs, mins, maxs, firstmarksurface, nummarksurfaces, ambient_level);
    }
};

struct bsp2_dleaf_t
{
    int32_t contents;
    int32_t visofs; /* -1 = no visibility info */
    qvec3f mins; /* for frustum culling     */
    qvec3f maxs;
    uint32_t firstmarksurface;
    uint32_t nummarksurfaces;
    std::array<uint8_t, NUM_AMBIENTS> ambient_level;

    bsp2_dleaf_t() = default;

    // convert from mbsp_t
    bsp2_dleaf_t(const mleaf_t &model)
        : contents(model.contents), visofs(model.visofs), mins(model.mins), maxs(model.maxs),
          firstmarksurface(model.firstmarksurface), nummarksurfaces(model.nummarksurfaces),
          ambient_level(model.ambient_level)
    {
    }

    // convert to mbsp_t
    operator mleaf_t() const
    {
        return {contents, visofs, mins, maxs, firstmarksurface, nummarksurfaces, ambient_level};
    }

    // serialize for streams
    auto stream_data()
    {
        return std::tie(contents, visofs, mins, maxs, firstmarksurface, nummarksurfaces, ambient_level);
    }
};

// Q1-esque maps can use one of these two.
using dmodelq1_vector = std::vector<dmodelq1_t>;
using dmodelh2_vector = std::vector<dmodelh2_t>;

// type tag used for type inference
struct q1bsp_tag_t
{
};

struct bsp29_t : q1bsp_tag_t
{
    std::variant<std::monostate, dmodelq1_vector, dmodelh2_vector> dmodels;
    std::vector<uint8_t> dvisdata;
    std::vector<uint8_t> dlightdata;
    dmiptexlump_t dtex;
    std::string dentdata;
    std::vector<bsp29_dleaf_t> dleafs;
    std::vector<dplane_t> dplanes;
    std::vector<qvec3f> dvertexes;
    std::vector<bsp29_dnode_t> dnodes;
    std::vector<texinfo_t> texinfo;
    std::vector<bsp29_dface_t> dfaces;
    std::vector<bsp29_dclipnode_t> dclipnodes;
    std::vector<bsp29_dedge_t> dedges;
    std::vector<uint16_t> dmarksurfaces;
    std::vector<int32_t> dsurfedges;
};

struct bsp2rmq_t : q1bsp_tag_t
{
    std::variant<std::monostate, dmodelq1_vector, dmodelh2_vector> dmodels;
    std::vector<uint8_t> dvisdata;
    std::vector<uint8_t> dlightdata;
    dmiptexlump_t dtex;
    std::string dentdata;
    std::vector<bsp2rmq_dleaf_t> dleafs;
    std::vector<dplane_t> dplanes;
    std::vector<qvec3f> dvertexes;
    std::vector<bsp2rmq_dnode_t> dnodes;
    std::vector<texinfo_t> texinfo;
    std::vector<bsp2_dface_t> dfaces;
    std::vector<bsp2_dclipnode_t> dclipnodes;
    std::vector<bsp2_dedge_t> dedges;
    std::vector<uint32_t> dmarksurfaces;
    std::vector<int32_t> dsurfedges;
};

struct bsp2_t : q1bsp_tag_t
{
    std::variant<std::monostate, dmodelq1_vector, dmodelh2_vector> dmodels;
    std::vector<uint8_t> dvisdata;
    std::vector<uint8_t> dlightdata;
    dmiptexlump_t dtex;
    std::string dentdata;
    std::vector<bsp2_dleaf_t> dleafs;
    std::vector<dplane_t> dplanes;
    std::vector<qvec3f> dvertexes;
    std::vector<bsp2_dnode_t> dnodes;
    std::vector<texinfo_t> texinfo;
    std::vector<bsp2_dface_t> dfaces;
    std::vector<bsp2_dclipnode_t> dclipnodes;
    std::vector<bsp2_dedge_t> dedges;
    std::vector<uint32_t> dmarksurfaces;
    std::vector<int32_t> dsurfedges;
};

extern const bspversion_t bspver_q1;
extern const bspversion_t bspver_h2;
extern const bspversion_t bspver_h2bsp2;
extern const bspversion_t bspver_h2bsp2rmq;
extern const bspversion_t bspver_bsp2;
extern const bspversion_t bspver_bsp2rmq;
extern const bspversion_t bspver_hl;