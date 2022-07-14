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

    inline bool copyFrom(const setting_base& other) override {
        if (auto *casted = dynamic_cast<const setting_wadpathset *>(&other)) {
            _paths = casted->_paths;
            _source = casted->_source;
            return true;
        }
        return false;
    }

    inline void reset() override {
        _paths = {};
        _source = source::DEFAULT;
    }

    bool parse(const std::string &settingName, parser_base_t &parser, bool locked = false) override
    {
        if (!parser.parse_token()) {
            return false;
        }

        if (changeSource(locked ? source::COMMANDLINE : source::MAP)) {
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
    setting_int32 subdivide{this, "subdivide", 240, &common_format_group,
        "change the subdivide threshold, in luxels. 0 will disable subdivision entirely"};
    setting_bool nofill{this, "nofill", false, &debugging_group, "don't perform outside filling"};
    setting_bool nomerge{this, "nomerge", false, &debugging_group, "don't perform face merging"};
    setting_bool noclip{this, "noclip", false, &common_format_group, "don't write clip nodes (Q1-like BSP formats)"};
    setting_bool noskip{this, "noskip", false, &debugging_group, "don't remove faces with the 'skip' texture"};
    setting_bool nodetail{this, "nodetail", false, &debugging_group, "treat all detail brushes to structural"};
    setting_bool onlyents{this, "onlyents", false, &map_development_group, "only updates .MAP entities"};
    setting_bool splitsky{this, "splitsky", false, &debugging_group, "doesn't combine sky faces into one large face"};
    setting_bool splitturb{this, {"litwater", "splitturb"}, true, &common_format_group,
        "doesn't combine water faces into one large face"};
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
        this, "forcegoodtree", false, &debugging_group, "force use of expensive processing for SolidBSP stage"};
    setting_scalar midsplitsurffraction{this, "midsplitsurffraction", 0.f, 0.f, 1.f, &debugging_group,
        "if 0 (default), use `maxnodesize` for deciding when to switch to midsplit bsp heuristic.\nif 0 < midsplitSurfFraction <= 1, switch to midsplit if the node contains more than this fraction of the model's\ntotal surfaces. Try 0.15 to 0.5. Works better than maxNodeSize for maps with a 3D skybox (e.g. +-128K unit maps)"};
    setting_int32 maxnodesize{this, "maxnodesize", 1024, &debugging_group,
        "triggers simpler BSP Splitting when node exceeds size (default 1024, 0 to disable)"};
    setting_bool oldrottex{
        this, "oldrottex", false, &debugging_group, "use old rotate_ brush texturing aligned at (0 0 0)"};
    setting_scalar epsilon{
        this, "epsilon", 0.0001, 0.0, 1.0, &debugging_group, "customize epsilon value for point-on-plane checks"};
    setting_scalar microvolume{
        this, "microvolume", 1.0, 0.0, 1000.0, &debugging_group, "microbrush volume"};
    setting_bool contenthack{this, "contenthack", false, &debugging_group,
        "hack to fix leaks through solids. causes missing faces in some cases so disabled by default"};
    setting_bool leaktest{this, "leaktest", false, &map_development_group, "make compilation fail if the map leaks"};
    setting_bool outsidedebug{this, "outsidedebug", false, &debugging_group, "write a .map after outside filling showing non-visible brush sides"};
    setting_bool debugchop{this, "debugchop", false, &debugging_group,
        "write a .map after ChopBrushes"};
    setting_bool keepprt{this, "keepprt", false, &debugging_group,
        "avoid deleting the .prt file on leaking maps"};
    setting_bool includeskip{this, "includeskip", false, &common_format_group,
        "don't cull skip faces from the list of renderable surfaces (Q2RTX)"};
    setting_scalar worldextent{
        this, "worldextent", 0.0, &debugging_group, "explicitly provide world extents; 0 will auto-detect"};
    setting_int32 leakdist{this, "leakdist", 2, &debugging_group, "space between leakfile points"};
    setting_bool forceprt1{
        this, "forceprt1", false, &debugging_group, "force a PRT1 output file even if PRT2 is required for vis"};
    setting_bool notjunc{this, "notjunc", false, &debugging_group, "don't fix T-junctions"};
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
    setting_bool notriggermodels{this, "notriggermodels", false, &common_format_group, "for supported game code only: triggers will not write a model\nout, and will instead just write out their mins/maxs."};
    setting_set aliasdefs{this, "aliasdef", "\"path/to/file.def\" <multiple allowed>", &map_development_group, "path to an alias definition file, which can transform entities in the .map into other entities."};
    setting_set texturedefs{this, "texturedefs", "\"path/to/file.def\" <multiple allowed>", &map_development_group, "path to a texture definition file, which can transform textures in the .map into other textures."};
    setting_numeric<vec_t> lmscale{this, "lmscale", 1.0, &common_format_group, "change global lmscale (force _lmscale key on all entities). outputs the LMSCALE BSPX lump." };
    setting_enum<filltype_t> filltype{this, "filltype", filltype_t::AUTO, { { "auto", filltype_t::AUTO }, { "inside", filltype_t::INSIDE }, { "outside", filltype_t::OUTSIDE } }, &common_format_group,
        "whether to fill the map from the outside in (lenient), from the inside out (aggressive), or to automatically decide based on the hull being used."};
    setting_invertible_bool allow_upgrade{this, "allowupgrade", true, &common_format_group, "allow formats to \"upgrade\" to compatible extended formats when a limit is exceeded (ie Quake BSP to BSP2)"};

    void setParameters(int argc, const char **argv) override
    {
        common_settings::setParameters(argc, argv);
        programDescription = "qbsp performs geometric level processing of Quake .MAP files to create\nQuake .BSP files.\n\n";
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

// planenum for a leaf
constexpr int32_t PLANENUM_LEAF = -1;

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
    std::vector<size_t> output_vertices, original_vertices; // filled in by EmitVertices & TJunc
    std::vector<int64_t> edges; // only filled in MakeFaceEdges
    std::optional<size_t> outputnumber; // only valid for original faces after
                                        // write surfaces
};

struct portal_t;

struct face_t : face_fragment_t
{
    int planenum;
    planeside_t planeside; // which side is the front of the face
    int texinfo;
    contentflags_t contents; // contents on the front of the face
    int16_t lmshift;
    winding_t w;

    qvec3d origin;
    vec_t radius;

    // only valid after tjunction code
    std::vector<face_fragment_t> fragments;

    portal_t *portal;
};

// there is a node_t structure for every node and leaf in the bsp tree

struct bspbrush_t;
struct side_t;

struct node_t
{
    // both leafs and nodes
    aabb3d bounds; // bounding volume, not just points inside
    node_t *parent;
    // this is also a bounding volume like `bounds`
    std::unique_ptr<bspbrush_t> volume; // one for each leaf/node

    // information for decision nodes
    int planenum; // -1 = leaf node
    int firstface; // decision node only
    int numfaces; // decision node only
    twosided<std::unique_ptr<node_t>> children; // children[0] = front side, children[1] = back side of plane. only valid for decision nodes
    std::list<std::unique_ptr<face_t>> facelist; // decision nodes only, list for both sides
    side_t *side; // the side that created the node

    // information for leafs
    contentflags_t contents; // leaf nodes (0 for decision nodes)
    std::vector<bspbrush_t *> original_brushes;
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
void InitQBSP(const std::vector<std::string>& args);
void ProcessFile();

int qbsp_main(int argc, const char **argv);
