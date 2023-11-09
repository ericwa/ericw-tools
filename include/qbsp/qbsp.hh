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

#pragma once

#include <list>
#include <vector>
#include <map>
#include <memory>
#include <unordered_map>
#include <array>
#include <optional>
#include <string>
#include <variant>

#include <cassert>
#include <cctype>
#include <cerrno>
#include <cfloat>
#include <cmath>
#include <cstdint>
// #include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <qbsp/winding.hh>

#include <common/bspfile.hh>
#include <common/aabb.hh>
#include <common/settings.hh>
#include <common/fs.hh>
#include <qbsp/brush.hh>

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
    none,
    quake,
    quake2,
    valve,
    bp
};

// data representation of only extended flags
// used by Q2 format; used by various systems.
struct extended_texinfo_t
{
    contentflags_t contents = {0};
    surfflags_t flags = {0};
    int value = 0;
    std::string animation;
};

namespace settings
{
struct wadpath
{
    fs::path path;
    bool external; // wads from this path are not to be embedded into the bsp, but will instead require the engine
                   // to load them from elsewhere. strongly recommended for eg halflife.wad

    bool operator<(const wadpath &other) const;
};

struct setting_wadpathset : public setting_base
{
private:
    std::set<wadpath> _paths;

public:
    setting_wadpathset(setting_container *dictionary, const nameset &names, const setting_group *group = nullptr,
        const char *description = "");
    void addPath(const wadpath &path);
    const std::set<wadpath> &pathsValue() const;
    bool copy_from(const setting_base &other) override;
    void reset() override;
    bool parse(const std::string &setting_name, parser_base_t &parser, source source) override;
    std::string string_value() const override;
    std::string format() const override;
};

extern setting_group game_target_group;
extern setting_group map_development_group;
extern setting_group common_format_group;
extern setting_group debugging_group;

enum class filltype_t
{
    AUTO,
    OUTSIDE,
    INSIDE
};

enum class tjunclevel_t
{
    NONE, // don't attempt to adjust faces at all - pass them through unchanged
    ROTATE, // allow faces' vertices to be rotated to prevent zero-area triangles
    RETOPOLOGIZE, // if a face still has zero-area triangles, allow it to be re-topologized
                  // by splitting it into multiple fans
    MWT // attempt MWT first, only falling back to the prior two steps if it fails.
};

struct setting_tjunc : public setting_enum<tjunclevel_t>
{
public:
    using setting_enum<tjunclevel_t>::setting_enum;

    bool parse(const std::string &setting_name, parser_base_t &parser, source source) override;
};

// like qvec3f, but integer and allows up to three values (xyz, x y, or x y z)
// defaults to 1024 if assigned, otherwise zero.
class setting_blocksize : public setting_value<qvec3i>
{
public:
    inline setting_blocksize(setting_container *dictionary, const nameset &names, qvec3i val,
        const setting_group *group = nullptr, const char *description = "");
    bool parse(const std::string &setting_name, parser_base_t &parser, source source) override;
    std::string string_value() const override;
    std::string format() const override;
};

class setting_debugexpand : public setting_value<std::variant<uint8_t, aabb3d>>
{
public:
    inline setting_debugexpand(setting_container *dictionary, const nameset &names,
        const setting_group *group = nullptr, const char *description = "");
    bool parse(const std::string &setting_name, parser_base_t &parser, source source) override;
    std::string string_value() const override;
    std::string format() const override;
    bool is_hull() const;
    const uint8_t &hull_index_value() const;
    const aabb3d &hull_bounds_value() const;
};

class qbsp_settings : public common_settings
{
public:
    setting_bool hexen2;
    setting_bool hlbsp;
    setting_bool q2bsp;
    setting_bool qbism;
    setting_bool bsp2;
    setting_bool bsp2rmq;
    setting_func nosubdivide;
    setting_invertible_bool software;
    setting_int32 subdivide;
    setting_bool nofill;
    setting_bool nomerge;
    setting_bool noedgereuse;
    setting_bool noclip;
    setting_bool noskip;
    setting_bool nodetail;
    setting_invertible_bool chop;
    setting_bool chopfragment;
    setting_bool onlyents;
    setting_bool splitsky;
    setting_bool splitturb;
    setting_redirect splitspecial;
    setting_invertible_bool transwater;
    setting_bool transsky;
    setting_bool notextures;
    setting_enum<conversion_t> convertmapformat;
    setting_invertible_bool oldaxis;
    setting_bool forcegoodtree;
    setting_scalar midsplitsurffraction;
    setting_int32 maxnodesize;
    setting_bool oldrottex;
    setting_scalar epsilon;
    setting_scalar microvolume;
    setting_bool leaktest;
    setting_bool outsidedebug;
    setting_bool debugchop;
    setting_bool debugleak;
    setting_bool debugbspbrushes;
    setting_bool debugleafvolumes;
    setting_debugexpand debugexpand;
    setting_bool keepprt;
    setting_bool includeskip;
    setting_scalar worldextent;
    setting_int32 leakdist;
    setting_bool forceprt1;
    setting_tjunc tjunc;
    setting_bool objexport;
    setting_bool noextendedsurfflags;
    setting_bool wrbrushes;
    setting_redirect wrbrushesonly;
    setting_bool bmodelcontents;
    setting_bool omitdetail;
    setting_bool omitdetailwall;
    setting_bool omitdetailillusionary;
    setting_bool omitdetailfence;
    setting_wadpathset wadpaths;
    setting_bool notriggermodels;
    setting_set aliasdefs;
    setting_set texturedefs;
    setting_numeric<vec_t> lmscale;
    setting_enum<filltype_t> filltype;
    setting_bool filldetail;
    setting_invertible_bool allow_upgrade;
    setting_validator<setting_int32> maxedges;
    setting_numeric<vec_t> midsplitbrushfraction;
    setting_string add;
    setting_scalar scale;
    setting_bool loghulls;
    setting_bool logbmodels;

    void set_parameters(int argc, const char **argv) override;
    void initialize(int argc, const char **argv) override;
    void postinitialize(int argc, const char **argv) override;
    void reset() override;

    const bspversion_t *target_version = nullptr;
    const gamedef_t *target_game = nullptr;
    fs::path map_path;
    fs::path bsp_path;
    std::unordered_map<std::string, std::tuple<std::string, std::optional<extended_texinfo_t>>> loaded_texture_defs;
    std::unordered_map<std::string, entdict_t> loaded_entity_defs;

    qbsp_settings();

private:
    void load_texture_def(const std::string &pathname);
    void load_entity_def(const std::string &pathname);
};
}; // namespace settings

extern settings::qbsp_settings qbsp_options;

// the exact bounding box of the brushes is expanded some for the headnode
// volume. this is done to avoid a zero-bounded node/leaf, the particular
// value doesn't matter but it shows up in the .bsp output.
constexpr double SIDESPACE = 24.0;

#include <common/cmdlib.hh>
#include <common/mathlib.hh>

struct maptexinfo_t
{
    texvecf vecs; /* [s/t][xyz offset] */
    int32_t miptex = 0;
    surfflags_t flags = {};
    int32_t value = 0; // Q2-specific
    std::optional<int32_t> next = std::nullopt; // Q2-specific
    std::optional<size_t> outputnum = std::nullopt; // nullopt until added to bsp

    bool operator<(const maptexinfo_t &other) const;
    bool operator>(const maptexinfo_t &other) const;
};

class mapentity_t;

struct face_fragment_t
{
    std::vector<size_t> output_vertices; // filled in by TJunc
    std::vector<int64_t> edges; // only filled in MakeFaceEdges
    std::optional<size_t> outputnumber; // only valid for original faces after
                                        // write surfaces
};

struct portal_t;
struct qbsp_plane_t;
struct mapface_t;
struct node_t;

struct face_t
{
    size_t planenum;
    int texinfo;
    twosided<contentflags_t> contents; // contents on the front/back of the face
    winding_t w;
    std::vector<size_t> original_vertices; // the vertices of this face before fragmentation; filled in by EmitVertices
    std::vector<face_fragment_t> fragments; // the vertices of this face post-fragmentation; filled in by TJunc
    std::vector<node_t *> markleafs; // populated at the same time as markfaces; reverse mapping to that

    qvec3d origin;
    vec_t radius;

    portal_t *portal;
    mapface_t *original_side;

    const maptexinfo_t &get_texinfo() const;

    const qbsp_plane_t &get_plane() const;
    const qbsp_plane_t &get_positive_plane() const;
};

// a semi-mutable version of plane that automatically manages the "type"
// component, which allows for quick comparisons.
struct qbsp_plane_t
{
protected:
    qplane3d plane;
    plane_type_t type = plane_type_t::PLANE_INVALID;

    static plane_type_t calculate_type(const qplane3d &p);

public:
    qbsp_plane_t() = default;
    qbsp_plane_t(const qbsp_plane_t &) = default;
    qbsp_plane_t(const qplane3d &plane, bool flip) noexcept;

    qbsp_plane_t(const qplane3d &plane) noexcept;

    qbsp_plane_t &operator=(const qbsp_plane_t &) = default;
    qbsp_plane_t &operator=(const qplane3d &plane) noexcept;

    [[nodiscard]] qbsp_plane_t operator-() const;

    [[nodiscard]] const plane_type_t &get_type() const;
    [[nodiscard]] const vec_t &get_dist() const;
    [[nodiscard]] vec_t &get_dist();
    [[nodiscard]] const qvec3d &get_normal() const;
    bool set_normal(const qvec3d &vec, bool flip = false);
    bool set_plane(const qplane3d &plane, bool flip = false);

    [[nodiscard]] const qplane3d &get_plane() const;
    [[nodiscard]] operator const qplane3d &() const;

    template<typename T>
    [[nodiscard]] inline T distance_to(const qvec<T, 3> &pt) const
    {
        return plane.distance_to(pt);
    }

    // normalize the given plane, optionally flipping it to face
    // the positive direction. returns whether the plane was flipped or not.
    bool normalize(bool flip) noexcept;
};

// Fmt support
template<>
struct fmt::formatter<qbsp_plane_t> : formatter<qplane3d>
{
    template<typename FormatContext>
    auto format(const qbsp_plane_t &p, FormatContext &ctx) -> decltype(ctx.out())
    {
        fmt::format_to(ctx.out(), "<");
        fmt::formatter<qplane3d>::format(p.get_plane(), ctx);
        fmt::format_to(ctx.out(), ", type: {}>", p.get_type());
        return ctx.out();
    }
};

namespace qv
{
// faster version of epsilonEqual for BSP planes
// which have a bit more info in them
[[nodiscard]] bool epsilonEqual(const qbsp_plane_t &p1, const qbsp_plane_t &p2, vec_t normalEpsilon = NORMAL_EPSILON,
    vec_t distEpsilon = DIST_EPSILON);
}; // namespace qv

template<typename T>
T BaseWindingForPlane(const qplane3d &p)
{
    return T::from_plane(p, qbsp_options.worldextent.value());
}

// there is a node_t structure for every node and leaf in the bsp tree

#include <set>

struct bspbrush_t;
struct side_t;
class mapbrush_t;

struct node_t
{
    // both leafs and nodes
    aabb3d bounds; // bounding volume, not just points inside
    node_t *parent;
    // this is also a bounding volume like `bounds`
    bspbrush_t::ptr volume; // one for each leaf/node
    bool is_leaf = false;

    // information for decision nodes
    size_t planenum; // decision node only

    const qbsp_plane_t &get_plane() const;

    int firstface; // decision node only
    int numfaces; // decision node only
    twosided<node_t *>
        children; // children[0] = front side, children[1] = back side of plane. only valid for decision nodes
    std::list<std::unique_ptr<face_t>> facelist; // decision nodes only, list for both sides

    // information for leafs
    contentflags_t contents; // leaf nodes (0 for decision nodes)
    std::vector<face_t *> markfaces; // leaf nodes only, point to node faces
    portal_t *portals;
    int visleafnum; // -1 = solid
    int viscluster; // detail cluster for faster vis
    int outside_distance; // -1 = can't reach outside, 0 = first void node, >0 = distance from void, in number of
                          // portals used to write leak lines that take the shortest path to the void
    int occupied; // 0=can't reach entity, 1 = has entity, >1 = distance from leaf with entity
    mapentity_t *occupant; // example occupant, for leak hunting
    bool detail_separator; // for vis portal generation. true if ALL faces on node, and on all descendant nodes/leafs,
                           // are detail.
    uint32_t firstleafbrush; // Q2
    uint32_t numleafbrushes;
    int32_t area;
    std::vector<bspbrush_t *> original_brushes;
    bspbrush_t::container bsp_brushes;
};

void InitQBSP(int argc, const char **argv);
void InitQBSP(const std::vector<std::string> &args);
void CountLeafs(node_t *headnode);
void ProcessFile();

int qbsp_main(int argc, const char **argv);
