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

#include <fmt/core.h>

#include <cinttypes>
#include <iosfwd>
#include <array>
#include <vector>
#include <string>
#include <string_view>
#include <memory>
#include "qvec.hh"

constexpr int32_t MBSPIDENT = -1;

constexpr size_t MAX_MAP_HULLS_H2 = 8;

struct dmodelh2_t
{
    qvec3f mins;
    qvec3f maxs;
    qvec3f origin;
    std::array<int32_t, MAX_MAP_HULLS_H2> headnode; /* hexen2 only uses 6 */
    int32_t visleafs; /* not including the solid leaf 0 */
    int32_t firstface;
    int32_t numfaces;

    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s);
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
    std::vector<std::array<int32_t, 2>> bit_offsets;
    std::vector<uint8_t> bits;

    size_t header_offset() const;

    // set a bit offset of the specified cluster/vistype *relative to the start of the bits array*
    // (after the header)
    void set_bit_offset(vistype_t type, size_t cluster, size_t offset);

    // fetch the bit offset of the specified cluster/vistype
    // relative to the start of the bits array
    int32_t get_bit_offset(vistype_t type, size_t cluster) const;

    void resize(size_t numclusters);

    void stream_read(std::istream &stream, const lump_t &lump);
    void stream_write(std::ostream &stream) const;
};

// structured data from BSP. this is the header of the miptex used
// in Quake-like formats.
constexpr size_t MIPLEVELS = 4;
struct dmiptex_t
{
    std::array<char, 16> name;
    uint32_t width, height;
    std::array<int32_t, MIPLEVELS> offsets; /* four mip maps stored */

    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s);
};

// semi-structured miptex data; we don't directly care about
// the contents of the miptex beyond the header. we store
// some of the data from the miptex (name, width, height) but
// the full, raw miptex is also stored in `data`.
struct miptex_t
{
    std::string name;
    uint32_t width, height;
    std::vector<uint8_t> data;
    /**
     * set at read time if the offset is -1
     */
    bool null_texture = false;
    /**
     * exposed for testing -notex
     */
    std::array<int32_t, MIPLEVELS> offsets;

    size_t stream_size() const;

    void stream_read(std::istream &stream, size_t len);
    void stream_write(std::ostream &stream) const;
};

// structured miptex container lump
struct dmiptexlump_t
{
    std::vector<miptex_t> textures;

    void stream_read(std::istream &stream, const lump_t &lump);
    void stream_write(std::ostream &stream) const;

    size_t stream_size() const;
};

// 0-2 are axial planes
// 3-5 are non-axial planes snapped to the nearest
enum class plane_type_t
{
    PLANE_INVALID = -1,
    PLANE_X = 0,
    PLANE_Y = 1,
    PLANE_Z = 2,
    PLANE_ANYX = 3,
    PLANE_ANYY = 4,
    PLANE_ANYZ = 5,
};

template<typename T>
inline plane_type_t calculate_plane_type(const qplane3<T> &p)
{
    for (size_t i = 0; i < 3; i++) {
        if (p.normal[i] == 1.0 || p.normal[i] == -1.0) {
            return (i == 0 ? plane_type_t::PLANE_X : i == 1 ? plane_type_t::PLANE_Y : plane_type_t::PLANE_Z);
        }
    }

    vec_t ax = fabs(p.normal[0]);
    vec_t ay = fabs(p.normal[1]);
    vec_t az = fabs(p.normal[2]);

    if (ax >= ay && ax >= az) {
        return plane_type_t::PLANE_ANYX;
    } else if (ay >= ax && ay >= az) {
        return plane_type_t::PLANE_ANYY;
    } else {
        return plane_type_t::PLANE_ANYZ;
    }
}

// Fmt support
template<>
struct fmt::formatter<plane_type_t>
{
    constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) { return ctx.end(); }

    template<typename FormatContext>
    auto format(plane_type_t t, FormatContext &ctx)
    {
        string_view name = "unknown";
        switch (t) {
            case plane_type_t::PLANE_INVALID: name = "PLANE_INVALID"; break;
            case plane_type_t::PLANE_X: name = "PLANE_X"; break;
            case plane_type_t::PLANE_Y: name = "PLANE_Y"; break;
            case plane_type_t::PLANE_Z: name = "PLANE_Z"; break;
            case plane_type_t::PLANE_ANYX: name = "PLANE_ANYX"; break;
            case plane_type_t::PLANE_ANYY: name = "PLANE_ANYY"; break;
            case plane_type_t::PLANE_ANYZ: name = "PLANE_ANYZ"; break;
        }
        return format_to(ctx.out(), "{}", name);
    }
};

struct dplane_t : qplane3f
{
    int32_t type;

    [[nodiscard]] constexpr dplane_t operator-() const { return {qplane3f::operator-(), type}; }

    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s);

    // optimized case
    template<typename T>
    inline T distance_to_fast(const qvec<T, 3> &point) const
    {
        switch (static_cast<plane_type_t>(type)) {
            case plane_type_t::PLANE_X: return point[0] - dist;
            case plane_type_t::PLANE_Y: return point[1] - dist;
            case plane_type_t::PLANE_Z: return point[2] - dist;
            default: {
                return qplane3f::distance_to(point);
            }
        }
    }
};

struct bsp2_dnode_t
{
    int32_t planenum;
    std::array<int32_t, 2> children; /* negative numbers are -(leafs+1), not nodes */
    qvec3f mins; /* for sphere culling */
    qvec3f maxs;
    uint32_t firstface;
    uint32_t numfaces; /* counting both sides */

    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s);
};

struct mtexinfo_t
{
    texvecf vecs; // [s/t][xyz offset]
    surfflags_t flags; // native miptex flags + extended flags

    // q1 only
    int32_t miptex;

    // q2 only
    int32_t value; // light emission, etc
    std::array<char, 32> texture; // texture name (textures/*.wal)
    int32_t nexttexinfo = -1; // for animations, -1 = end of chain
};

constexpr size_t MAXLIGHTMAPS = 4;
constexpr uint16_t INVALID_LIGHTSTYLE_OLD = 0xffu;

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

    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s);
};

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

    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s);
};

using bsp2_dedge_t = std::array<uint32_t, 2>; /* vertex numbers */

/*
 * leaf 0 is the generic CONTENTS_SOLID leaf, used for all solid areas (except Q2)
 * all other leafs need visibility info
 */
/* Ambient Sounds */
enum ambient_type_t : uint8_t
{
    AMBIENT_WATER,
    AMBIENT_SKY,
    AMBIENT_SLIME,
    AMBIENT_LAVA,

    NUM_AMBIENTS = 4
};

constexpr int32_t CLUSTER_INVALID = -1;
constexpr int32_t AREA_INVALID = 0;

struct mleaf_t
{
    // bsp2_dleaf_t
    int32_t contents;
    int32_t visofs; /* -1 = no visibility info; Q1 only! */
    qvec3f mins; /* for frustum culling     */
    qvec3f maxs;
    uint32_t firstmarksurface;
    uint32_t nummarksurfaces;
    std::array<uint8_t, NUM_AMBIENTS> ambient_level;

    // q2 extras
    int32_t cluster;
    int32_t area;
    uint32_t firstleafbrush;
    uint32_t numleafbrushes;

    // comparison operator for tests
    bool operator==(const mleaf_t &other) const;
};

struct darea_t
{
    int32_t numareaportals;
    int32_t firstareaportal;

    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s);

    // comparison operator for tests
    bool operator==(const darea_t &other) const;
};

// each area has a list of portals that lead into other areas
// when portals are closed, other areas may not be visible or
// hearable even if the vis info says that it should be
struct dareaportal_t
{
    int32_t portalnum;
    int32_t otherarea;

    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s);

    // comparison operator for tests
    bool operator==(const dareaportal_t &other) const;
};

struct dbrush_t
{
    int32_t firstside;
    int32_t numsides;
    int32_t contents;

    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s);
};

struct q2_dbrushside_qbism_t
{
    uint32_t planenum; // facing out of the leaf
    int32_t texinfo;

    // serialize for streams
    void stream_write(std::ostream &s) const;
    void stream_read(std::istream &s);
};

struct bspversion_t;

// "generic" bsp - superset of all other supported types
struct mbsp_t
{
    // the BSP version that we came from, if any
    const bspversion_t *loadversion;

    // the BSP we were converted from, if any
    fs::path file;

    std::vector<dmodelh2_t> dmodels;
    mvis_t dvis;
    std::vector<uint8_t> dlightdata;
    dmiptexlump_t dtex;
    std::string dentdata;
    std::vector<mleaf_t> dleafs;
    std::vector<dplane_t> dplanes;
    std::vector<qvec3f> dvertexes;
    std::vector<bsp2_dnode_t> dnodes;
    std::vector<mtexinfo_t> texinfo;
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
};

extern const bspversion_t bspver_generic;

const std::initializer_list<const gamedef_t *> &gamedef_list();
