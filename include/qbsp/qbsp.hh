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

#include <list>
#include <vector>
#include <map>
#include <memory>
#include <unordered_map>
#include <array>
#include <optional>
#include <string>

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

#include <qbsp/winding.hh>

#include <common/bspfile.hh>
#include <common/aabb.hh>
#include <common/settings.hh>
#include <common/fs.hh>

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

    inline bool operator<(const wadpath &other) const { return path < other.path; }
};

struct setting_wadpathset : public setting_base
{
private:
    std::set<wadpath> _paths;

public:
    inline setting_wadpathset(setting_container *dictionary, const nameset &names, const setting_group *group = nullptr,
        const char *description = "")
        : setting_base(dictionary, names, group, description)
    {
    }

    inline void addPath(const wadpath &path) { _paths.insert(path); }

    constexpr const std::set<wadpath> &pathsValue() const { return _paths; }

    inline bool copyFrom(const setting_base &other) override
    {
        if (auto *casted = dynamic_cast<const setting_wadpathset *>(&other)) {
            _paths = casted->_paths;
            _source = casted->_source;
            return true;
        }
        return false;
    }

    inline void reset() override
    {
        _paths = {};
        _source = source::DEFAULT;
    }

    bool parse(const std::string &settingName, parser_base_t &parser, source source) override
    {
        if (!parser.parse_token()) {
            return false;
        }

        if (changeSource(source)) {
            _paths.insert(wadpath{fs::path(parser.token), settingName[0] == 'x'});
        }

        return true;
    }

    std::string stringValue() const override
    {
        std::string paths;

        for (auto &path : _paths) {
            if (!paths.empty()) {
                paths += " ; ";
            }

            paths += path.path.string();

            if (path.external) {
                paths += " (external)";
            }
        }

        return paths;
    }

    std::string format() const override { return "path/to/wads"; }
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

    bool parse(const std::string &settingName, parser_base_t &parser, source source) override
    {
        if (settingName == "notjunc") {
            this->setValue(tjunclevel_t::NONE, source);
            return true;
        }

        return this->setting_enum<tjunclevel_t>::parse(settingName, parser, source);
    }
};

// like qvec3f, but integer and allows up to three values (xyz, x y, or x y z)
// defaults to 1024 if assigned, otherwise zero.
class setting_blocksize : public setting_value<qvec3i>
{
public:
    inline setting_blocksize(setting_container *dictionary, const nameset &names, qvec3i val,
        const setting_group *group = nullptr, const char *description = "")
        : setting_value(dictionary, names, val, group, description)
    {
    }

    bool parse(const std::string &settingName, parser_base_t &parser, source source) override
    {
        qvec3d vec = { 1024, 1024, 1024 };

        for (int i = 0; i < 3; i++) {
            if (!parser.parse_token(PARSE_PEEK)) {
                return false;
            }

            // don't allow negatives
            if (parser.token[0] != '-') {
                try {
                    vec[i] = std::stol(parser.token);
                    parser.parse_token();
                    continue;
                } catch (std::exception &) {
                    // intentional fall-through
                }
            }

            // if we didn't parse a valid number, fail
            if (i == 0) {
                return false;
            } else if (i == 1) {
                // we parsed one valid number; use it all the way through
                vec[1] = vec[2] = vec[0];
            }  

            // for [x, y] z will be left default
        }

        setValue(vec, source);

        return true;
    }

    std::string stringValue() const override { return qv::to_string(_value); }

    std::string format() const override { return "[x [y [z]]]"; }
};

class qbsp_settings : public common_settings
{
public:
    setting_bool hexen2{this, "hexen2", false, &game_target_group, "target Hexen II's BSP format"};
    setting_bool hlbsp{this, "hlbsp", false, &game_target_group, "target Half Life's BSP format"};
    setting_bool q2bsp{this, "q2bsp", false, &game_target_group, "target Quake II's BSP format"};
    setting_bool qbism{this, "qbism", false, &game_target_group, "target Qbism's extended Quake II BSP format"};
    setting_bool bsp2{this, "bsp2", false, &game_target_group, "target Quake's extended BSP2 format"};
    setting_bool bsp2rmq{
        this, "2psb", false, &game_target_group, "target Quake's extended 2PSB format (RMQ compatible)"};
    setting_func nosubdivide{this, "nosubdivide", [&](source src) { subdivide.setValue(0, src); }, &common_format_group,
        "disable subdivision"};
    setting_invertible_bool software{this, "software", true, &common_format_group,
        "change settings to allow for (or make adjustments to optimize for the lack of) software support"};
    setting_int32 subdivide{this, "subdivide", 240, &common_format_group,
        "change the subdivide threshold, in luxels. 0 will disable subdivision entirely"};
    setting_bool nofill{this, "nofill", false, &debugging_group, "don't perform outside filling"};
    setting_bool nomerge{this, "nomerge", false, &debugging_group, "don't perform face merging"};
    setting_bool noclip{this, "noclip", false, &common_format_group, "don't write clip nodes (Q1-like BSP formats)"};
    setting_bool noskip{this, "noskip", false, &debugging_group, "don't remove faces with the 'skip' texture"};
    setting_bool nodetail{this, "nodetail", false, &debugging_group, "treat all detail brushes to structural"};
    setting_bool onlyents{this, "onlyents", false, &map_development_group, "only updates .MAP entities"};
    setting_bool splitsky{this, "splitsky", false, &debugging_group, "doesn't combine sky faces into one large face"};
    setting_bool splitturb{
        this, {"litwater", "splitturb"}, true, &common_format_group, "doesn't combine water faces into one large face"};
    setting_redirect splitspecial{this, "splitspecial", {&splitsky, &splitturb}, &debugging_group,
        "doesn't combine sky and water faces into one large face (splitturb + splitsky)"};
    setting_invertible_bool transwater{
        this, "transwater", true, &common_format_group, "compute portal information for transparent water"};
    setting_bool transsky{
        this, "transsky", false, &map_development_group, "compute portal information for transparent sky"};
    setting_bool notextures{this, "notex", false, &common_format_group,
        "write only placeholder textures to depend upon replacements, keep file sizes down, or to skirt copyrights"};
    setting_enum<conversion_t> convertmapformat{this, "convert", conversion_t::none,
        {{"quake", conversion_t::quake}, {"quake2", conversion_t::quake2}, {"valve", conversion_t::valve},
            {"bp", conversion_t::bp}},
        &common_format_group, "convert a .MAP to a different .MAP format"};
    setting_invertible_bool oldaxis{this, "oldaxis", true, &debugging_group,
        "uses alternate texture alignment which was default in tyrutils-ericw v0.15.1 and older"};
    setting_bool forcegoodtree{
        this, "forcegoodtree", false, &debugging_group, "force use of expensive processing for BrushBSP stage"};
    setting_scalar midsplitsurffraction{this, "midsplitsurffraction", 0.f, 0.f, 1.f, &debugging_group,
        "if 0 (default), use `maxnodesize` for deciding when to switch to midsplit bsp heuristic.\nif 0 < midsplitSurfFraction <= 1, switch to midsplit if the node contains more than this fraction of the model's\ntotal surfaces. Try 0.15 to 0.5. Works better than maxNodeSize for maps with a 3D skybox (e.g. +-128K unit maps)"};
    setting_int32 maxnodesize{this, "maxnodesize", 1024, &debugging_group,
        "triggers simpler BSP Splitting when node exceeds size (default 1024, 0 to disable)"};
    setting_bool oldrottex{
        this, "oldrottex", false, &debugging_group, "use old rotate_ brush texturing aligned at (0 0 0)"};
    setting_scalar epsilon{
        this, "epsilon", 0.0001, 0.0, 1.0, &debugging_group, "customize epsilon value for point-on-plane checks"};
    setting_scalar microvolume{this, "microvolume", 1.0, 0.0, 1000.0, &debugging_group, "microbrush volume"};
    setting_bool contenthack{this, "contenthack", false, &debugging_group,
        "hack to fix leaks through solids. causes missing faces in some cases so disabled by default"};
    setting_bool leaktest{this, "leaktest", false, &map_development_group, "make compilation fail if the map leaks"};
    setting_bool outsidedebug{this, "outsidedebug", false, &debugging_group,
        "write a .map after outside filling showing non-visible brush sides"};
    setting_bool debugchop{this, "debugchop", false, &debugging_group, "write a .map after ChopBrushes"};
    setting_bool keepprt{this, "keepprt", false, &debugging_group, "avoid deleting the .prt file on leaking maps"};
    setting_bool includeskip{this, "includeskip", false, &common_format_group,
        "don't cull skip faces from the list of renderable surfaces (Q2RTX)"};
    setting_scalar worldextent{
        this, "worldextent", 0.0, &debugging_group, "explicitly provide world extents; 0 will auto-detect"};
    setting_int32 leakdist{this, "leakdist", 2, &debugging_group, "space between leakfile points"};
    setting_bool forceprt1{
        this, "forceprt1", false, &debugging_group, "force a PRT1 output file even if PRT2 is required for vis"};
    setting_tjunc tjunc{this, {"tjunc", "notjunc"}, tjunclevel_t::MWT,
        {{"none", tjunclevel_t::NONE}, {"rotate", tjunclevel_t::ROTATE}, {"retopologize", tjunclevel_t::RETOPOLOGIZE},
            {"mwt", tjunclevel_t::MWT}},
        &debugging_group, "T-junction fix level"};
    setting_bool objexport{
        this, "objexport", false, &debugging_group, "export the map file as .OBJ models during various CSG phases"};
    setting_bool wrbrushes{this, {"wrbrushes", "bspx"}, false, &common_format_group,
        "includes a list of brushes for brush-based collision"};
    setting_redirect wrbrushesonly{this, {"wrbrushesonly", "bspxonly"}, {&wrbrushes, &noclip}, &common_format_group,
        "includes BSPX brushes and does not output clipping hulls (wrbrushes + noclip)"};
    setting_bool omitdetail{
        this, "omitdetail", false, &map_development_group, "omit *all* detail brushes from the compile"};
    setting_bool omitdetailwall{
        this, "omitdetailwall", false, &map_development_group, "func_detail_wall brushes are omitted from the compile"};
    setting_bool omitdetailillusionary{this, "omitdetailillusionary", false, &map_development_group,
        "func_detail_illusionary brushes are omitted from the compile"};
    setting_bool omitdetailfence{this, "omitdetailfence", false, &map_development_group,
        "func_detail_fence brushes are omitted from the compile"};
    setting_bool expand{
        this, "expand", false, &common_format_group, "write hull 1 expanded brushes to expanded.map for debugging"};
    setting_wadpathset wadpaths{this, {"wadpath", "xwadpath"}, &map_development_group,
        "add a path to the wad search paths; wads found in xwadpath's will not be embedded, otherwise they will be embedded (if not -notex)"};
    setting_bool notriggermodels{this, "notriggermodels", false, &common_format_group,
        "for supported game code only: triggers will not write a model\nout, and will instead just write out their mins/maxs."};
    setting_set aliasdefs{this, "aliasdef", "\"path/to/file.def\" <multiple allowed>", &map_development_group,
        "path to an alias definition file, which can transform entities in the .map into other entities."};
    setting_set texturedefs{this, "texturedefs", "\"path/to/file.def\" <multiple allowed>", &map_development_group,
        "path to a texture definition file, which can transform textures in the .map into other textures."};
    setting_numeric<vec_t> lmscale{this, "lmscale", 1.0, &common_format_group,
        "change global lmscale (force _lmscale key on all entities). outputs the LMSCALE BSPX lump."};
    setting_enum<filltype_t> filltype{this, "filltype", filltype_t::AUTO,
        {{"auto", filltype_t::AUTO}, {"inside", filltype_t::INSIDE}, {"outside", filltype_t::OUTSIDE}},
        &common_format_group,
        "whether to fill the map from the outside in (lenient), from the inside out (aggressive), or to automatically decide based on the hull being used."};
    setting_invertible_bool allow_upgrade{this, "allowupgrade", true, &common_format_group,
        "allow formats to \"upgrade\" to compatible extended formats when a limit is exceeded (ie Quake BSP to BSP2)"};
    setting_validator<setting_int32> maxedges{
        [](setting_int32 &setting) { return setting.value() == 0 || setting.value() >= 3; }, this, "maxedges", 64,
        &map_development_group,
        "the max number of edges/vertices on a single face before it is split into another face"};
    // FIXME: this block size default is from Q3, and is basically derived from having 128x128x128 chunks of the world
    // since the max world size in Q3 is {-65536, -65536, -65536, 65536, 65536, 65536}. should we dynamically change this?
    // should we automatically turn this on if the world gets too big but leave it off for smaller worlds?
    setting_blocksize blocksize{this, "blocksize", { 0, 0, 0 }, &common_format_group, "from q3map2; split the world by x/y/z sized chunks, speeding up split decisions"};
    setting_numeric<vec_t> midsplitbrushfraction{this, "midsplitbrushfraction", 0.0, &common_format_group, "switch to cheaper partitioning if a node contains this % of brushes in the map"};
    setting_string add{this, "add", "", "", &common_format_group, "the given map file will be appended to the base map"};

    void setParameters(int argc, const char **argv) override
    {
        common_settings::setParameters(argc, argv);
        programDescription =
            "qbsp performs geometric level processing of Quake .MAP files to create\nQuake .BSP files.\n\n";
        remainderName = "sourcefile.map [destfile.bsp]";
    }
    void initialize(int argc, const char **argv) override;
    void postinitialize(int argc, const char **argv) override;

    bool fVerbose = true;
    bool fAllverbose = false;
    bool fNoverbose = false;
    const bspversion_t *target_version = nullptr;
    const gamedef_t *target_game = nullptr;
    fs::path map_path;
    fs::path bsp_path;
    std::unordered_map<std::string, std::tuple<std::string, std::optional<extended_texinfo_t>>> loaded_texture_defs;
    std::unordered_map<std::string, entdict_t> loaded_entity_defs;

private:
    void load_texture_def(const std::string &pathname);
    void load_entity_def(const std::string &pathname);
};
}; // namespace settings

extern settings::qbsp_settings qbsp_options;

/*
 * The quality of the bsp output is highly sensitive to these epsilon values.
 * Notes:
 * - some calculations are sensitive to errors and need the various
 *   epsilons to be such that EQUAL_EPSILON < CONTINUOUS_EPSILON.
 *     ( TODO: re-check if CONTINUOUS_EPSILON is still directly related )
 */
constexpr vec_t ANGLEEPSILON = 0.000001;
constexpr vec_t ZERO_EPSILON = 0.0001;
constexpr vec_t EQUAL_EPSILON = 0.0001;
constexpr vec_t CONTINUOUS_EPSILON = 0.0005;

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
    int32_t next = -1; // Q2-specific
    std::optional<size_t> outputnum = std::nullopt; // nullopt until added to bsp

    constexpr auto as_tuple() const { return std::tie(vecs, miptex, flags, value, next); }

    constexpr bool operator<(const maptexinfo_t &other) const { return as_tuple() < other.as_tuple(); }

    constexpr bool operator>(const maptexinfo_t &other) const { return as_tuple() > other.as_tuple(); }
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

struct face_t
{
    size_t planenum;
    int texinfo;
    contentflags_t contents; // contents on the front of the face
    int16_t lmshift;
    winding_t w;
    std::vector<size_t> original_vertices; // the vertices of this face before fragmentation; filled in by EmitVertices
    std::vector<face_fragment_t> fragments; // the vertices of this face post-fragmentation; filled in by TJunc

    qvec3d origin;
    vec_t radius;

    portal_t *portal;

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

    static inline plane_type_t calculate_type(const qplane3d &p)
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

public:
    qbsp_plane_t() = default;
    qbsp_plane_t(const qbsp_plane_t &) = default;
    inline qbsp_plane_t(const qplane3d &plane, bool flip) noexcept : plane(plane) { normalize(flip); }

    inline qbsp_plane_t(const qplane3d &plane) noexcept : qbsp_plane_t(plane, false) { }

    qbsp_plane_t &operator=(const qbsp_plane_t &) = default;
    inline qbsp_plane_t &operator=(const qplane3d &plane) noexcept
    {
        this->plane = plane;
        normalize(false);
        return *this;
    }

    [[nodiscard]] inline qbsp_plane_t operator-() const
    {
        qbsp_plane_t copy = *this;
        copy.plane = -copy.plane;
        return copy;
    }

    [[nodiscard]] constexpr const plane_type_t &get_type() const { return type; }
    [[nodiscard]] constexpr const vec_t &get_dist() const { return plane.dist; }
    [[nodiscard]] constexpr vec_t &get_dist() { return plane.dist; }
    [[nodiscard]] constexpr const qvec3d &get_normal() const { return plane.normal; }
    inline bool set_normal(const qvec3d &vec, bool flip = false)
    {
        plane.normal = vec;
        return normalize(flip);
    }

    inline bool set_plane(const qplane3d &plane, bool flip = false)
    {
        this->plane = plane;
        return normalize(flip);
    }

    [[nodiscard]] constexpr const qplane3d &get_plane() const { return plane; }
    [[nodiscard]] constexpr operator const qplane3d &() const { return plane; }

    template<typename T>
    [[nodiscard]] inline T distance_to(const qvec<T, 3> &pt) const
    {
        return plane.distance_to(pt);
    }

    // normalize the given plane, optionally flipping it to face
    // the positive direction. returns whether the plane was flipped or not.
    inline bool normalize(bool flip) noexcept
    {
        for (size_t i = 0; i < 3; i++) {
            if (plane.normal[i] == 1.0) {
                plane.normal[(i + 1) % 3] = 0;
                plane.normal[(i + 2) % 3] = 0;
                type = (i == 0 ? plane_type_t::PLANE_X : i == 1 ? plane_type_t::PLANE_Y : plane_type_t::PLANE_Z);
                return false;
            }
            if (plane.normal[i] == -1.0) {
                if (flip) {
                    plane.normal[i] = 1.0;
                    plane.dist = -plane.dist;
                }
                plane.normal[(i + 1) % 3] = 0;
                plane.normal[(i + 2) % 3] = 0;
                type = (i == 0 ? plane_type_t::PLANE_X : i == 1 ? plane_type_t::PLANE_Y : plane_type_t::PLANE_Z);
                return flip;
            }
        }

        vec_t ax = fabs(plane.normal[0]);
        vec_t ay = fabs(plane.normal[1]);
        vec_t az = fabs(plane.normal[2]);

        size_t nearest;

        if (ax >= ay && ax >= az) {
            nearest = 0;
            type = plane_type_t::PLANE_ANYX;
        } else if (ay >= ax && ay >= az) {
            nearest = 1;
            type = plane_type_t::PLANE_ANYY;
        } else {
            nearest = 2;
            type = plane_type_t::PLANE_ANYZ;
        }

        if (flip && plane.normal[nearest] < 0) {
            plane = -plane;
            return true;
        }

        return false;
    }
};

namespace qv
{
// faster version of epsilonEqual for BSP planes
// which have a bit more info in them
[[nodiscard]] inline bool epsilonEqual(const qbsp_plane_t &p1, const qbsp_plane_t &p2,
    vec_t normalEpsilon = NORMAL_EPSILON, vec_t distEpsilon = DIST_EPSILON)
{
    // axial planes will never match on normal, so we can skip that check entirely
    if (p1.get_type() < plane_type_t::PLANE_ANYX && p2.get_type() < plane_type_t::PLANE_ANYX) {
        // if we aren't the same type, we definitely aren't equal
        if (p1.get_type() != p2.get_type()) {
            return false;
        } else if (p1.get_normal()[static_cast<int32_t>(p1.get_type())] !=
                   p2.get_normal()[static_cast<int32_t>(p2.get_type())]) {
            // axials will always be only 1 or -1
            return false;
        }

        // check dist
        return epsilonEqual(p1.get_dist(), p2.get_dist(), distEpsilon);
    }

    // check dist
    if (!epsilonEqual(p1.get_dist(), p2.get_dist(), distEpsilon)) {
        return false;
    }

    // check normal
    return epsilonEqual(p1.get_normal(), p2.get_normal(), normalEpsilon);
}
}; // namespace qv

// there is a node_t structure for every node and leaf in the bsp tree

#include <set>

struct bspbrush_t;
struct side_t;

struct bspbrush_t_less
{
    bool operator()(const bspbrush_t *a, const bspbrush_t *b) const;
};

struct node_t
{
    // both leafs and nodes
    aabb3d bounds; // bounding volume, not just points inside
    node_t *parent;
    // this is also a bounding volume like `bounds`
    std::unique_ptr<bspbrush_t> volume; // one for each leaf/node
    bool is_leaf = false;

    // information for decision nodes
    size_t planenum; // decision node only

    const qbsp_plane_t &get_plane() const;

    int firstface; // decision node only
    int numfaces; // decision node only
    twosided<std::unique_ptr<node_t>>
        children; // children[0] = front side, children[1] = back side of plane. only valid for decision nodes
    std::list<std::unique_ptr<face_t>> facelist; // decision nodes only, list for both sides

    // information for leafs
    contentflags_t contents; // leaf nodes (0 for decision nodes)
    std::set<bspbrush_t *, bspbrush_t_less> original_brushes;
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
};

void InitQBSP(int argc, const char **argv);
void InitQBSP(const std::vector<std::string> &args);
void ProcessFile();

int qbsp_main(int argc, const char **argv);
