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

#include <cstring>
#include <algorithm>

#include <common/log.hh>
#include <common/aabb.hh>
#include <common/fs.hh>
#include <common/settings.hh>

#include <qbsp/brush.hh>
#include <qbsp/exportobj.hh>
#include <qbsp/map.hh>
#include <qbsp/portals.hh>
#include <qbsp/prtfile.hh>
#include <qbsp/brushbsp.hh>
#include <qbsp/faces.hh>
#include <qbsp/qbsp.hh>
#include <qbsp/writebsp.hh>
#include <qbsp/outside.hh>
#include <qbsp/tjunc.hh>
#include <qbsp/tree.hh>
#include <qbsp/csg.hh>

#include <fmt/chrono.h>

namespace settings
{
bool wadpath::operator<(const wadpath &other) const
{
    return path < other.path;
}

// setting_wadpathset

setting_wadpathset::setting_wadpathset(
    setting_container *dictionary, const nameset &names, const setting_group *group, const char *description)
    : setting_base(dictionary, names, group, description)
{
}

void setting_wadpathset::addPath(const wadpath &path)
{
    _paths.insert(path);
}

const std::set<wadpath> &setting_wadpathset::pathsValue() const
{
    return _paths;
}

bool setting_wadpathset::copy_from(const setting_base &other)
{
    if (auto *casted = dynamic_cast<const setting_wadpathset *>(&other)) {
        _paths = casted->_paths;
        _source = casted->_source;
        return true;
    }
    return false;
}

void setting_wadpathset::reset()
{
    _paths = {};
    _source = source::DEFAULT;
}

bool setting_wadpathset::parse(const std::string &setting_name, parser_base_t &parser, source source)
{
    if (!parser.parse_token()) {
        return false;
    }

    if (change_source(source)) {
        _paths.insert(wadpath{fs::path(parser.token), setting_name[0] == 'x'});
    }

    return true;
}

std::string setting_wadpathset::string_value() const
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

std::string setting_wadpathset::format() const
{
    return "path/to/wads";
}

// setting_tjunc

bool setting_tjunc::parse(const std::string &setting_name, parser_base_t &parser, source source)
{
    if (setting_name == "notjunc") {
        this->set_value(tjunclevel_t::NONE, source);
        return true;
    }

    return this->setting_enum<tjunclevel_t>::parse(setting_name, parser, source);
}

// setting_blocksize

setting_blocksize::setting_blocksize(setting_container *dictionary, const nameset &names, qvec3i val,
    const setting_group *group, const char *description)
    : setting_value(dictionary, names, val, group, description)
{
}

bool setting_blocksize::parse(const std::string &setting_name, parser_base_t &parser, source source)
{
    qvec3d vec = {1024, 1024, 1024};

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

    set_value(vec, source);

    return true;
}

std::string setting_blocksize::string_value() const
{
    return qv::to_string(_value);
}

std::string setting_blocksize::format() const
{
    return "[x [y [z]]]";
}

// setting_debugexpand

setting_debugexpand::setting_debugexpand(
    setting_container *dictionary, const nameset &names, const setting_group *group, const char *description)
    : setting_value(dictionary, names, {}, group, description)
{
}

bool setting_debugexpand::parse(const std::string &setting_name, parser_base_t &parser, source source)
{
    std::array<vec_t, 6> values;
    size_t i = 0;

    try {
        for (; i < 6; i++) {
            if (!parser.parse_token(PARSE_PEEK)) {
                throw std::exception();
            }

            values[i] = std::stod(parser.token);

            parser.parse_token();
        }

        this->set_value(aabb3d{{values[0], values[1], values[2]}, {values[3], values[4], values[5]}}, source);

        return true;
    } catch (std::exception &) {
        // single hull value
        if (i == 1) {
            set_value(static_cast<uint8_t>(values[0]), source);
            return true;
        }

        return false;
    }
}

std::string setting_debugexpand::string_value() const
{
    return is_hull() ? std::to_string(hull_index_value()) : fmt::format("{}", hull_bounds_value());
}

std::string setting_debugexpand::format() const
{
    return "[single hull index] or [mins_x mins_y mins_z maxs_x maxs_y maxs_z]";
}

bool setting_debugexpand::is_hull() const
{
    return std::holds_alternative<uint8_t>(_value);
}

const uint8_t &setting_debugexpand::hull_index_value() const
{
    return std::get<uint8_t>(_value);
}

const aabb3d &setting_debugexpand::hull_bounds_value() const
{
    return std::get<aabb3d>(_value);
}
} // namespace settings

static auto as_tuple(const maptexinfo_t &info)
{
    return std::tie(info.vecs, info.miptex, info.flags, info.value, info.next);
}

bool maptexinfo_t::operator<(const maptexinfo_t &other) const
{
    return as_tuple(*this) < as_tuple(other);
}

bool maptexinfo_t::operator>(const maptexinfo_t &other) const
{
    return as_tuple(*this) > as_tuple(other);
}

const maptexinfo_t &face_t::get_texinfo() const
{
    return map.mtexinfos[texinfo];
}

const qbsp_plane_t &face_t::get_plane() const
{
    return map.get_plane(planenum);
}

const qbsp_plane_t &face_t::get_positive_plane() const
{
    return map.get_plane(planenum & ~1);
}

const qbsp_plane_t &node_t::get_plane() const
{
    return map.get_plane(planenum);
}

plane_type_t qbsp_plane_t::calculate_type(const qplane3d &p)
{
    return calculate_plane_type(p);
}

qbsp_plane_t::qbsp_plane_t(const qplane3d &plane, bool flip) noexcept
    : plane(plane)
{
    normalize(flip);
}

qbsp_plane_t::qbsp_plane_t(const qplane3d &plane) noexcept
    : qbsp_plane_t(plane, false)
{
}

qbsp_plane_t &qbsp_plane_t::operator=(const qplane3d &plane) noexcept
{
    this->plane = plane;
    normalize(false);
    return *this;
}

[[nodiscard]] qbsp_plane_t qbsp_plane_t::operator-() const
{
    qbsp_plane_t copy = *this;
    copy.plane = -copy.plane;
    return copy;
}

[[nodiscard]] const plane_type_t &qbsp_plane_t::get_type() const
{
    return type;
}
[[nodiscard]] const vec_t &qbsp_plane_t::get_dist() const
{
    return plane.dist;
}
[[nodiscard]] vec_t &qbsp_plane_t::get_dist()
{
    return plane.dist;
}
[[nodiscard]] const qvec3d &qbsp_plane_t::get_normal() const
{
    return plane.normal;
}
bool qbsp_plane_t::set_normal(const qvec3d &vec, bool flip)
{
    plane.normal = vec;
    return normalize(flip);
}

bool qbsp_plane_t::set_plane(const qplane3d &plane, bool flip)
{
    this->plane = plane;
    return normalize(flip);
}

[[nodiscard]] const qplane3d &qbsp_plane_t::get_plane() const
{
    return plane;
}
[[nodiscard]] qbsp_plane_t::operator const qplane3d &() const
{
    return plane;
}

// normalize the given plane, optionally flipping it to face
// the positive direction. returns whether the plane was flipped or not.
bool qbsp_plane_t::normalize(bool flip) noexcept
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

namespace qv
{
[[nodiscard]] bool epsilonEqual(const qbsp_plane_t &p1, const qbsp_plane_t &p2, vec_t normalEpsilon, vec_t distEpsilon)
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

// command line flags
namespace settings
{
setting_group game_target_group{"Game/BSP Target", -1, expected_source::commandline};
setting_group map_development_group{"Map development", 1, expected_source::commandline};
setting_group common_format_group{"Common format options", 2, expected_source::commandline};
setting_group debugging_group{"Advanced/tool debugging", 500, expected_source::commandline};

inline void set_target_version(const bspversion_t *version)
{
    if (qbsp_options.target_version) {
        FError("BSP version was set by multiple flags; currently {}, tried to change to {}\n",
            qbsp_options.target_version->name, version->name);
    }

    qbsp_options.target_version = version;
}

qbsp_settings::qbsp_settings()
    : hexen2{this, "hexen2", false, &game_target_group, "target Hexen II's BSP format"},
      hlbsp{this, "hlbsp", false, &game_target_group, "target Half Life's BSP format"},
      q2bsp{this, "q2bsp", false, &game_target_group, "target Quake II's BSP format"},
      qbism{this, "qbism", false, &game_target_group, "target Qbism's extended Quake II BSP format"},
      bsp2{this, "bsp2", false, &game_target_group, "target Quake's extended BSP2 format"},
      bsp2rmq{this, "2psb", false, &game_target_group, "target Quake's extended 2PSB format (RMQ compatible)"},
      nosubdivide{this, "nosubdivide", [&](source src) { subdivide.set_value(0, src); }, &common_format_group,
          "disable subdivision"},
      software{this, "software", true, &common_format_group,
          "change settings to allow for (or make adjustments to optimize for the lack of) software support"},
      subdivide{this, "subdivide", 240, &common_format_group,
          "change the subdivide threshold, in luxels. 0 will disable subdivision entirely"},
      nofill{this, "nofill", false, &debugging_group, "don't perform outside filling"},
      nomerge{this, "nomerge", false, &debugging_group, "don't perform face merging"},
      noedgereuse{this, "noedgereuse", false, &debugging_group, "don't reuse edges (for debugging software rendering)"},
      noclip{this, "noclip", false, &common_format_group, "don't write clip nodes (Q1-like BSP formats)"},
      noskip{this, "noskip", false, &debugging_group, "don't remove faces with the 'skip' texture"},
      nodetail{this, "nodetail", false, &debugging_group, "treat all detail brushes to structural"},
      chop{this, "chop", false, &debugging_group, "adjust brushes to remove intersections if possible"},
      chopfragment{this, "chopfragment", false, &debugging_group, "always do full fragmentation for chop"},
      onlyents{this, "onlyents", false, &map_development_group, "only updates .MAP entities"},
      splitsky{this, "splitsky", false, &debugging_group, "doesn't combine sky faces into one large face"},
      splitturb{this, {"litwater", "splitturb"}, true, &common_format_group,
          "doesn't combine water faces into one large face"},
      splitspecial{this, "splitspecial", {&splitsky, &splitturb}, &debugging_group,
          "doesn't combine sky and water faces into one large face (splitturb + splitsky)"},
      transwater{this, "transwater", true, &common_format_group, "compute portal information for transparent water"},
      transsky{this, "transsky", false, &map_development_group, "compute portal information for transparent sky"},
      notextures{this, "notex", false, &common_format_group,
          "write only placeholder textures to depend upon replacements, keep file sizes down, or to skirt copyrights"},
      convertmapformat{this, "convert", conversion_t::none,
          {{"quake", conversion_t::quake}, {"quake2", conversion_t::quake2}, {"valve", conversion_t::valve},
              {"bp", conversion_t::bp}},
          &common_format_group, "convert a .MAP to a different .MAP format"},
      oldaxis{this, "oldaxis", true, &debugging_group,
          "uses alternate texture alignment which was default in tyrutils-ericw v0.15.1 and older"},
      forcegoodtree{
          this, "forcegoodtree", false, &debugging_group, "force use of expensive processing for BrushBSP stage"},
      midsplitsurffraction{this, "midsplitsurffraction", 0.f, 0.f, 1.f, &debugging_group,
          "if 0 (default), use `maxnodesize` for deciding when to switch to midsplit bsp heuristic.\nif 0 < midsplitSurfFraction <= 1, switch to midsplit if the node contains more than this fraction of the model's\ntotal surfaces. Try 0.15 to 0.5. Works better than maxNodeSize for maps with a 3D skybox (e.g. +-128K unit maps)"},
      maxnodesize{this, "maxnodesize", 1024, &debugging_group,
          "triggers simpler BSP Splitting when node exceeds size (default 1024, 0 to disable)"},
      oldrottex{this, "oldrottex", false, &debugging_group, "use old rotate_ brush texturing aligned at (0 0 0)"},
      epsilon{this, "epsilon", 0.0001, 0.0, 1.0, &debugging_group, "customize epsilon value for point-on-plane checks"},
      microvolume{this, "microvolume", 0.0, 0.0, 1000.0, &debugging_group, "microbrush volume"},
      leaktest{this, "leaktest", false, &map_development_group, "make compilation fail if the map leaks"},
      outsidedebug{this, "outsidedebug", false, &debugging_group,
          "write a .map after outside filling showing non-visible brush sides"},
      debugchop{this, "debugchop", false, &debugging_group, "write a .map after ChopBrushes"},
      debugleak{this, "debugleak", false, &debugging_group, "write more diagnostic files for debugging leaks"},
      debugbspbrushes{this, "debugbspbrushes", false, &debugging_group,
          "save bsp brushes after BrushBSP to a .map, for visualizing BSP splits"},
      debugleafvolumes{this, "debugleafvolumes", false, &debugging_group,
          "save bsp leaf volumes after BrushBSP to a .map, for visualizing BSP splits"},
      debugexpand{this, "debugexpand", &debugging_group,
          "write expanded hull .map for debugging/inspecting hulls/brush bevelling"},
      keepprt{this, "keepprt", false, &debugging_group, "avoid deleting the .prt file on leaking maps"},
      includeskip{this, "includeskip", false, &common_format_group,
          "don't cull skip faces from the list of renderable surfaces (Q2RTX)"},
      worldextent{this, "worldextent", 0.0, &debugging_group, "explicitly provide world extents; 0 will auto-detect"},
      leakdist{this, "leakdist", 0, &debugging_group, "space between leakfile points (default 0: no inbetween points)"},
      forceprt1{
          this, "forceprt1", false, &debugging_group, "force a PRT1 output file even if PRT2 is required for vis"},
      tjunc{this, {"tjunc", "notjunc"}, tjunclevel_t::MWT,
          {{"none", tjunclevel_t::NONE}, {"rotate", tjunclevel_t::ROTATE}, {"retopologize", tjunclevel_t::RETOPOLOGIZE},
              {"mwt", tjunclevel_t::MWT}},
          &debugging_group, "T-junction fix level"},
      objexport{
          this, "objexport", false, &debugging_group, "export the map file as .OBJ models during various CSG phases"},
      noextendedsurfflags{this, "noextendedsurfflags", false, &debugging_group, "suppress writing a .texinfo file"},
      wrbrushes{this, {"wrbrushes", "bspx"}, false, &common_format_group,
          "includes a list of brushes for brush-based collision"},
      wrbrushesonly{this, {"wrbrushesonly", "bspxonly"}, {&wrbrushes, &noclip}, &common_format_group,
          "includes BSPX brushes and does not output clipping hulls (wrbrushes + noclip)"},
      bmodelcontents{this, "bmodelcontents", false, &common_format_group,
          "allow control over brush contents in bmodels, don't force CONTENTS_SOLID"},
      omitdetail{this, "omitdetail", false, &map_development_group, "omit *all* detail brushes from the compile"},
      omitdetailwall{this, "omitdetailwall", false, &map_development_group,
          "func_detail_wall brushes are omitted from the compile"},
      omitdetailillusionary{this, "omitdetailillusionary", false, &map_development_group,
          "func_detail_illusionary brushes are omitted from the compile"},
      omitdetailfence{this, "omitdetailfence", false, &map_development_group,
          "func_detail_fence brushes are omitted from the compile"},
      wadpaths{this, {"wadpath", "xwadpath"}, &map_development_group,
          "add a path to the wad search paths; wads found in xwadpath's will not be embedded, otherwise they will be embedded (if not -notex)"},
      notriggermodels{this, "notriggermodels", false, &common_format_group,
          "for supported game code only: triggers will not write a model\nout, and will instead just write out their mins/maxs."},
      aliasdefs{this, "aliasdef", "\"path/to/file.def\" <multiple allowed>", &map_development_group,
          "path to an alias definition file, which can transform entities in the .map into other entities."},
      texturedefs{this, "texturedefs", "\"path/to/file.def\" <multiple allowed>", &map_development_group,
          "path to a texture definition file, which can transform textures in the .map into other textures."},
      lmscale{this, "lmscale", 1.0, &common_format_group,
          "change global lmscale (force _lmscale key on all entities). outputs the LMSCALE BSPX lump."},
      filltype{this, "filltype", filltype_t::INSIDE,
          {{"auto", filltype_t::AUTO}, {"inside", filltype_t::INSIDE}, {"outside", filltype_t::OUTSIDE}},
          &common_format_group,
          "whether to fill the map from the outside in (lenient), from the inside out (aggressive), or to automatically decide based on the hull being used."},
      filldetail{this, "filldetail", true, &common_format_group,
          "whether to fill in empty spaces which are fully enclosed by detail solid"},
      allow_upgrade{this, "allowupgrade", true, &common_format_group,
          "allow formats to \"upgrade\" to compatible extended formats when a limit is exceeded (ie Quake BSP to BSP2)"},
      maxedges{[](setting_int32 &setting) { return setting.value() == 0 || setting.value() >= 3; }, this, "maxedges",
          64, &map_development_group,
          "the max number of edges/vertices on a single face before it is split into another face"},
      midsplitbrushfraction{this, "midsplitbrushfraction", 0.0, &common_format_group,
          "switch to cheaper partitioning if a node contains this % of brushes in the map"},
      add{this, "add", "", "", &common_format_group, "the given map file will be appended to the base map"},
      scale{this, "scale", 1.0, &map_development_group,
          "scales the map brushes and point entity origins by a give factor"},
      loghulls{this, {"loghulls"}, false, &logging_group, "print log output for collision hulls"},
      logbmodels{this, {"logbmodels"}, false, &logging_group, "print log output for bmodels"}
{
}

void qbsp_settings::set_parameters(int argc, const char **argv)
{
    common_settings::set_parameters(argc, argv);
    program_description =
        "qbsp performs geometric level processing of Quake .MAP files to create\nQuake .BSP files.\n\n";
    remainder_name = "sourcefile.map [destfile.bsp]";
}

void qbsp_settings::initialize(int argc, const char **argv)
{
    if (auto file = fs::load("qbsp.ini")) {
        logging::print("Loading options from qbsp.ini\n");
        parser_t p(file, {"qbsp.ini"});
        parse(p);
    }

    try {
        token_parser_t p(argc - 1, argv + 1, {"command line"});
        auto remainder = parse(p);

        if (remainder.size() <= 0 || remainder.size() > 2) {
            print_help();
        }

        qbsp_options.map_path = remainder[0];

        if (remainder.size() == 2) {
            qbsp_options.bsp_path = remainder[1];
        }
    } catch (parse_exception &ex) {
        logging::print(ex.what());
        print_help();
    }
}

void qbsp_settings::load_texture_def(const std::string &pathname)
{
    if (!fs::exists(pathname)) {
        FError("can't find texturedef file {}", pathname);
    }

    fs::data data = fs::load(pathname);
    parser_t parser(data, {pathname});

    while (true) {
        if (!parser.parse_token() || parser.at_end()) {
            break;
        }

        std::string from = std::move(parser.token);

        if (!parser.parse_token(PARSE_SAMELINE)) {
            break;
        }

        std::string to = std::move(parser.token);
        std::optional<extended_texinfo_t> texinfo;

        // FIXME: why is this necessary? is it a trailing \0? only happens on release
        // repro with a texdef with no newline at the end
        while (std::isspace(to[to.size() - 1])) {
            to.resize(to.size() - 1);
        }

        if (parser.parse_token(PARSE_SAMELINE | PARSE_OPTIONAL)) {
            texinfo = extended_texinfo_t{{std::stoi(parser.token)}};

            if (parser.parse_token(PARSE_SAMELINE | PARSE_OPTIONAL)) {
                texinfo->flags.native = std::stoi(parser.token);
            }

            if (parser.parse_token(PARSE_SAMELINE | PARSE_OPTIONAL)) {
                texinfo->value = std::stoi(parser.token);
            }
        }

        loaded_texture_defs[from] = {to, texinfo};
    }
}

void qbsp_settings::load_entity_def(const std::string &pathname)
{
    if (!fs::exists(pathname)) {
        FError("can't find aliasdef file {}", pathname);
    }

    fs::data data = fs::load(pathname);
    parser_t parser(data, {pathname});

    while (true) {
        if (!parser.parse_token() || parser.at_end()) {
            break;
        }

        std::string classname = std::move(parser.token);

        if (!parser.parse_token(PARSE_PEEK)) {
            FError("expected {{ in alias def {}, got end of file", pathname);
        }

        if (parser.token != "{") {
            FError("expected {{ in alias def {}, got {}", pathname, parser.token);
        }

        // parse ent dict
        loaded_entity_defs[classname] = parser;
    }
}

void qbsp_settings::postinitialize(int argc, const char **argv)
{
    // set target BSP type
    if (hlbsp.value()) {
        set_target_version(&bspver_hl);
    }

    if (q2bsp.value() || (q2rtx.value() && !q2bsp.is_changed() && !qbism.is_changed())) {
        set_target_version(&bspver_q2);
    }

    if (qbism.value()) {
        set_target_version(&bspver_qbism);
    }

    if (bsp2.value()) {
        set_target_version(&bspver_bsp2);
    }

    if (bsp2rmq.value()) {
        set_target_version(&bspver_bsp2rmq);
    }

    if (!qbsp_options.target_version) {
        set_target_version(&bspver_q1);
    }

    // if we wanted hexen2, update it now
    if (hexen2.value()) {
        if (qbsp_options.target_version == &bspver_bsp2) {
            qbsp_options.target_version = &bspver_h2bsp2;
        } else if (qbsp_options.target_version == &bspver_bsp2rmq) {
            qbsp_options.target_version = &bspver_h2bsp2rmq;
        } else {
            qbsp_options.target_version = &bspver_h2;
        }
    } else {
        if (!qbsp_options.target_version) {
            qbsp_options.target_version = &bspver_q1;
        }
    }

    // update target game
    qbsp_options.target_game = qbsp_options.target_version->game;

    /* If no wadpath given, default to the map directory */
    if (wadpaths.pathsValue().empty()) {
        wadpath wp{qbsp_options.map_path.parent_path(), false};

        // If options.map_path is a relative path, StrippedFilename will return the empty string.
        // In that case, don't add it as a wad path.
        if (!wp.path.empty()) {
            wadpaths.addPath(wp);
        }
    }

    // side effects from q2rtx
    if (q2rtx.value()) {
        if (!includeskip.is_changed()) {
            includeskip.set_value(true, settings::source::GAME_TARGET);
        }

        if (!software.is_changed()) {
            software.set_value(false, settings::source::GAME_TARGET);
        }
    }

    // side effects from Quake II
    if (qbsp_options.target_game->id == GAME_QUAKE_II) {
        if (!maxedges.is_changed()) {
            maxedges.set_value(0, settings::source::GAME_TARGET);
        }

        if (qbsp_options.target_version == &bspver_qbism) {
            if (!software.is_changed()) {
                software.set_value(false, settings::source::GAME_TARGET);
            }
        }

        if (!software.value() && !subdivide.is_changed()) {
            subdivide.set_value(496, settings::source::GAME_TARGET);
        }

        if (!qbsp_options.chop.is_changed()) {
            qbsp_options.chop.set_value(true, settings::source::GAME_TARGET);
        }
    }

    // load texture defs
    for (auto &def : texturedefs.values()) {
        load_texture_def(def);
    }

    for (auto &def : aliasdefs.values()) {
        load_entity_def(def);
    }

    common_settings::postinitialize(argc, argv);
}

void qbsp_settings::reset()
{
    common_settings::reset();

    target_version = nullptr;
    target_game = nullptr;
    map_path = fs::path();
    bsp_path = fs::path();
    loaded_texture_defs.clear();
    loaded_entity_defs.clear();
}
}; // namespace settings

settings::qbsp_settings qbsp_options;

struct brush_list_stats_t : logging::stat_tracker_t
{
    stat &total_brushes = register_stat("total brushes");
    stat &total_brush_sides = register_stat("total brush sides");
    stat &total_leaf_brushes = register_stat("total leaf brushes");
};

static void ExportBrushList_r(const mapentity_t &entity, node_t *node, brush_list_stats_t &stats)
{
    if (node->is_leaf) {
        if (node->contents.native) {
            if (node->original_brushes.size()) {
                node->numleafbrushes = node->original_brushes.size();
                stats.total_leaf_brushes += node->numleafbrushes;
                node->firstleafbrush = map.bsp.dleafbrushes.size();
                for (auto &b : node->original_brushes) {

                    if (!b->mapbrush->outputnumber.has_value()) {
                        b->mapbrush->outputnumber = {static_cast<uint32_t>(map.bsp.dbrushes.size())};

                        dbrush_t &brush = map.bsp.dbrushes.emplace_back(
                            dbrush_t{.firstside = static_cast<int32_t>(map.bsp.dbrushsides.size()),
                                .numsides = 0,
                                .contents = qbsp_options.target_game
                                                ->contents_remap_for_export(b->contents, gamedef_t::remap_type_t::brush)
                                                .native});

                        for (auto &side : b->mapbrush->faces) {
                            map.bsp.dbrushsides.push_back(
                                {(uint32_t)ExportMapPlane(side.planenum), (int32_t)ExportMapTexinfo(side.texinfo)});
                            brush.numsides++;
                            stats.total_brush_sides++;
                        }

                        stats.total_brushes++;
                    }

                    map.bsp.dleafbrushes.push_back(b->mapbrush->outputnumber.value());
                }
            }
        }

        return;
    }

    ExportBrushList_r(entity, node->children[0], stats);
    ExportBrushList_r(entity, node->children[1], stats);
}

static void ExportBrushList(mapentity_t &entity, node_t *node)
{
    logging::funcheader();

    brush_list_stats_t stats;

    ExportBrushList_r(entity, node, stats);
}

static bool IsTrigger(const mapentity_t &entity)
{
    auto &tex = entity.mapbrushes.front().faces[0].texname;

    if (tex.length() < 6) {
        return false;
    }

    size_t trigger_pos = tex.rfind("trigger");

    if (trigger_pos == std::string::npos) {
        return false;
    }

    return trigger_pos == (tex.size() - strlen("trigger"));
}

static void CountLeafs_r(node_t *node, content_stats_base_t &stats)
{
    if (node->is_leaf) {
        qbsp_options.target_game->count_contents_in_stats(node->contents, stats);
        return;
    }
    CountLeafs_r(node->children[0], stats);
    CountLeafs_r(node->children[1], stats);
}

static int NodeHeight(node_t *node)
{
    if (node->parent) {
        return 1 + NodeHeight(node->parent);
    }
    return 1;
}

static void CountLeafHeights_r(node_t *node, std::vector<int> &heights)
{
    if (node->is_leaf) {
        heights.push_back(NodeHeight(node));
        return;
    }
    CountLeafHeights_r(node->children[0], heights);
    CountLeafHeights_r(node->children[1], heights);
}

void CountLeafs(node_t *headnode)
{
    logging::funcheader();

    auto stats = qbsp_options.target_game->create_content_stats();
    CountLeafs_r(headnode, *stats);
    qbsp_options.target_game->print_content_stats(*stats, "leafs");

    // count the heights of the tree at each leaf
    logging::stat_tracker_t stat_print;

    std::vector<int> leaf_heights;
    CountLeafHeights_r(headnode, leaf_heights);

    const int max_height = *std::max_element(leaf_heights.begin(), leaf_heights.end());
    stat_print.register_stat("max tree height").count += max_height;

    double avg_height = 0;
    for (int height : leaf_heights) {
        avg_height += (height / static_cast<double>(leaf_heights.size()));
    }
    stat_print.register_stat("avg tree height").count += static_cast<int>(avg_height);
}

static void GatherBspbrushes_r(node_t *node, bspbrush_t::container &container)
{
    if (node->is_leaf) {
        for (auto &brush : node->bsp_brushes) {
            container.push_back(brush);
        }
        return;
    }

    GatherBspbrushes_r(node->children[0], container);
    GatherBspbrushes_r(node->children[1], container);
}

static void GatherLeafVolumes_r(node_t *node, bspbrush_t::container &container)
{
    if (node->is_leaf) {
        if (!node->contents.is_empty(qbsp_options.target_game)) {
            container.push_back(node->volume);
        }
        return;
    }

    GatherLeafVolumes_r(node->children[0], container);
    GatherLeafVolumes_r(node->children[1], container);
}

/*
===============
ProcessEntity
===============
*/
static void ProcessEntity(mapentity_t &entity, hull_index_t hullnum)
{
    /* No map brushes means non-bmodel entity.
       We need to handle worldspawn containing no brushes, though. */
    if (!entity.mapbrushes.size() && !map.is_world_entity(entity)) {
        return;
    }

    /*
     * func_group and func_detail entities get their brushes added to the
     * worldspawn
     */
    if (IsWorldBrushEntity(entity) || IsNonRemoveWorldBrushEntity(entity))
        return;

    // for notriggermodels: if we have at least one trigger-like texture, do special trigger stuff
    bool discarded_trigger = !map.is_world_entity(entity) && qbsp_options.notriggermodels.value() && IsTrigger(entity);

    // Export a blank model struct, and reserve the index (only do this once, for all hulls)
    if (!discarded_trigger) {
        if (!entity.outputmodelnumber.has_value()) {
            entity.outputmodelnumber = map.bsp.dmodels.size();
            map.bsp.dmodels.emplace_back();
        }

        if (!map.is_world_entity(entity)) {
            if (&entity == &map.entities[1]) {
                logging::header("Internal Entities");
            }

            std::string mod = fmt::format("*{}", entity.outputmodelnumber.value());

            if (qbsp_options.verbose.value()) {
                PrintEntity(entity);
            }

            if (!hullnum.value_or(0) || qbsp_options.loghulls.value()) {
                logging::print(logging::flag::STAT, "     MODEL: {}\n", mod);
            }

            entity.epairs.set("model", mod);
        }
    }

    if (qbsp_options.lmscale.is_changed() && !entity.epairs.has("_lmscale")) {
        entity.epairs.set("_lmscale", std::to_string(qbsp_options.lmscale.value()));
    }

    // Init the entity
    entity.bounds = {};

    // reserve enough brushes; we would only make less,
    // never more
    bspbrush_t::container brushes;
    brushes.reserve(entity.mapbrushes.size());

    /*
     * Convert the map brushes (planes) into BSP brushes (polygons)
     */
    size_t num_clipped = 0;
    Brush_LoadEntity(entity, hullnum, brushes, num_clipped);

    if (num_clipped && !qbsp_options.verbose.value()) {
        logging::print(logging::flag::STAT,
            "WARNING: {} faces were crunched away by being too small. {}Use -verbose to see which faces were affected.\n",
            num_clipped, hullnum.value_or(0) ? "This is normal for the hulls. " : "");
    }

    size_t num_sides = 0;
    for (size_t i = 0; i < brushes.size(); ++i) {
        num_sides += brushes[i]->sides.size();
    }

    logging::print(
        logging::flag::STAT, "INFO: calculating BSP for {} brushes with {} sides\n", brushes.size(), num_sides);

    // always chop the other hulls to reduce brush tests
    if (qbsp_options.chop.value() || hullnum.value_or(0)) {
        std::sort(brushes.begin(), brushes.end(), [](const bspbrush_t::ptr &a, const bspbrush_t::ptr &b) -> bool {
            if (a->mapbrush->chop_index == b->mapbrush->chop_index) {
                return a->mapbrush->line.line_number < b->mapbrush->line.line_number;
            }

            return a->mapbrush->chop_index < b->mapbrush->chop_index;
        });

        ChopBrushes(brushes, qbsp_options.chopfragment.value());
    }

    // we're discarding the brush
    if (discarded_trigger) {
        entity.epairs.set("mins", fmt::to_string(entity.bounds.mins()));
        entity.epairs.set("maxs", fmt::to_string(entity.bounds.maxs()));
        return;
    }

    // corner case, -omitdetail with all detail in an bmodel
    if (brushes.empty() && entity.bounds == aabb3d()) {
        return;
    }

    // simpler operation for hulls
    if (hullnum.value_or(0)) {
        tree_t tree;
        BrushBSP(tree, entity, brushes, tree_split_t::FAST);
        if (map.is_world_entity(entity) && !qbsp_options.nofill.value()) {
            // assume non-world bmodels are simple
            MakeTreePortals(tree);
            if (FillOutside(tree, hullnum, brushes)) {
                if (qbsp_options.filldetail.value())
                    FillDetail(tree, hullnum, brushes);

                // make a really good tree
                tree.clear();
                BrushBSP(tree, entity, brushes, tree_split_t::PRECISE);

                // fill again so PruneNodes works
                MakeTreePortals(tree);
                FillOutside(tree, hullnum, brushes);
                if (qbsp_options.filldetail.value())
                    FillDetail(tree, hullnum, brushes);

                FreeTreePortals(tree);
                PruneNodes(tree.headnode);
            }
            CountLeafs(tree.headnode);
        }
        ExportClipNodes(entity, tree.headnode, hullnum.value());
        return;
    }

    // full operation for collision (or main hull)
    tree_t tree;

    BrushBSP(tree, entity, brushes,
        qbsp_options.forcegoodtree.value() ? tree_split_t::PRECISE : // we asked for the slow method
            !map.is_world_entity(entity) ? tree_split_t::FAST
                                         : // brush models are assumed to be simple
            tree_split_t::AUTO);

    // build all the portals in the bsp tree
    // some portals are solid polygons, and some are paths to other leafs
    MakeTreePortals(tree);

    if (map.is_world_entity(entity)) {
        // debug output of bspbrushes
        if (!hullnum.value_or(0)) {
            if (qbsp_options.debugbspbrushes.value()) {
                bspbrush_t::container all_bspbrushes;
                GatherBspbrushes_r(tree.headnode, all_bspbrushes);
                WriteBspBrushMap("first-brushbsp", all_bspbrushes);
            }
            if (qbsp_options.debugleafvolumes.value()) {
                bspbrush_t::container all_bspbrushes;
                GatherLeafVolumes_r(tree.headnode, all_bspbrushes);
                WriteBspBrushMap("first-brushbsp-volumes", all_bspbrushes);
            }
        }

        // flood fills from the void.
        // marks brush sides which are *only* touching void;
        // we can skip using them as BSP splitters on the "really good tree"
        // (effectively expanding those brush sides outwards).
        if (!qbsp_options.nofill.value() && FillOutside(tree, hullnum, brushes)) {
            if (qbsp_options.filldetail.value())
                FillDetail(tree, hullnum, brushes);

            // make a really good tree
            tree.clear();
            BrushBSP(tree, entity, brushes, tree_split_t::PRECISE);

            // debug output of bspbrushes
            if (!hullnum.value_or(0)) {
                if (qbsp_options.debugbspbrushes.value()) {
                    bspbrush_t::container all_bspbrushes;
                    GatherBspbrushes_r(tree.headnode, all_bspbrushes);
                    WriteBspBrushMap("second-brushbsp", all_bspbrushes);
                }
                if (qbsp_options.debugleafvolumes.value()) {
                    bspbrush_t::container all_bspbrushes;
                    GatherLeafVolumes_r(tree.headnode, all_bspbrushes);
                    WriteBspBrushMap("second-brushbsp-volumes", all_bspbrushes);
                }
            }

            // make the real portals for vis tracing
            MakeTreePortals(tree);

            // fill again so PruneNodes works
            FillOutside(tree, hullnum, brushes);

            if (qbsp_options.filldetail.value())
                FillDetail(tree, hullnum, brushes);
        }

        // Area portals
        if (qbsp_options.target_game->id == GAME_QUAKE_II) {
            EmitAreaPortals(tree.headnode);
        }
    } else {
        FillBrushEntity(tree, hullnum, brushes);

        // rebuild BSP now that we've marked invisible brush sides
        tree.clear();
        BrushBSP(tree, entity, brushes, tree_split_t::PRECISE);
    }

    MakeTreePortals(tree);

    MarkVisibleSides(tree, brushes);
    MakeFaces(tree.headnode);

    FreeTreePortals(tree);
    PruneNodes(tree.headnode);

    // write out .prt for main hull
    if (!hullnum.value_or(0) && map.is_world_entity(entity) && (!map.leakfile || qbsp_options.keepprt.value())) {
        WritePortalFile(tree);
    }

    auto MakeFaceFromSide = [](node_t *node, mapface_t &side) -> std::unique_ptr<face_t> {
        if (!side.winding.size()) {
            return nullptr;
        }

        auto f = std::make_unique<face_t>();

        f->texinfo = side.texinfo;
        f->planenum = side.planenum ^ 1;
        f->portal = nullptr;
        f->original_side = &side;

        f->w = side.winding.clone();
        f->contents = {side.contents, side.contents};

        UpdateFaceSphere(f.get());

        return f;
    };

    // super-detail
    if (map.is_world_entity(entity)) {
        if (!hullnum.value_or(0)) {
            for (int i = 1; i < map.entities.size(); i++) {
                mapentity_t &source = map.entities.at(i);

                if (!source.epairs.get_int("_super_detail")) {
                    continue;
                }

                for (auto &brush : source.mapbrushes) {
                    for (auto &side : brush.faces) {
                        {
                            auto face = MakeFaceFromSide(tree.headnode, side);

                            if (face) {
                                tree.headnode->facelist.push_back(std::move(face));
                            }
                        }
                    }
                }
            }
        }
    }

    // needs to come after any face creation
    MakeMarkFaces(tree.headnode);

    CountLeafs(tree.headnode);

    // output vertices first, since TJunc needs it
    EmitVertices(tree.headnode);

    TJunc(tree.headnode);

    if (qbsp_options.objexport.value() && map.is_world_entity(entity)) {
        ExportObj_Nodes("pre_makefaceedges_plane_faces", tree.headnode);
        ExportObj_Marksurfaces("pre_makefaceedges_marksurfaces", tree.headnode);
    }

    Q_assert(!entity.firstoutputfacenumber.has_value());

    entity.firstoutputfacenumber = EmitFaces(tree.headnode);

    if (qbsp_options.target_game->id == GAME_QUAKE_II) {
        ExportBrushList(entity, tree.headnode);
    }

    ExportDrawNodes(entity, tree.headnode, entity.firstoutputfacenumber.value());
    FreeTreePortals(tree);
}

/*
=================
UpdateEntLump

=================
*/
static void UpdateEntLump(void)
{
    logging::print(logging::flag::STAT, "     Updating entities lump...\n");

    if (qbsp_options.target_game->id == GAME_QUAKE_II) {
        FError("this won't work on Q2 maps; for Q2, please use bsputil --extract-entities & --replace-entities.");
        return;
    }

    size_t modnum = 1;

    for (size_t i = 1; i < map.entities.size(); i++) {
        mapentity_t &entity = map.entities.at(i);

        /* Special handling for misc_external_map.
           Duplicates some logic from ProcessExternalMapEntity. */
        bool is_misc_external_map = false;

        if (!Q_strcasecmp(entity.epairs.get("classname"), "misc_external_map")) {
            const std::string &new_classname = entity.epairs.get("_external_map_classname");

            entity.epairs.set("classname", new_classname);
            entity.epairs.set("origin", "0 0 0");

            /* Note: the classname could have switched to
             * a IsWorldBrushEntity entity (func_group, func_detail),
             * or a bmodel entity (func_wall
             */
            is_misc_external_map = true;
        }

        bool isBrushEnt = (entity.mapbrushes.size() > 0) || is_misc_external_map;
        if (!isBrushEnt) {
            continue;
        }

        if (IsWorldBrushEntity(entity) || IsNonRemoveWorldBrushEntity(entity)) {
            continue;
        }

        entity.epairs.set("model", fmt::format("*{}", modnum));
        modnum++;

        /* Do extra work for rotating entities if necessary */
        const std::string &classname = entity.epairs.get("classname");

        if (!classname.compare(0, 7, "rotate_")) {
            FixRotateOrigin(entity);
        }
    }

    WriteEntitiesToString();
    UpdateBSPFileEntitiesLump();
}

/*
Actually writes out the final bspx BRUSHLIST lump
This lump replaces the clipnodes stuff for custom collision sizes.
*/
void BSPX_Brushes_Finalize(struct bspxbrushes_s *ctx)
{
    // Actually written in WriteBSPFile()
    map.exported_bspxbrushes = std::move(ctx->lumpdata);
}
void BSPX_Brushes_Init(struct bspxbrushes_s *ctx)
{
    ctx->lumpdata.clear();
}

/*
WriteBrushes
Generates a submodel's direct brush information to a separate file, so the engine doesn't need to depend upon specific
hull sizes
*/

static void BSPX_Brushes_AddModel(struct bspxbrushes_s *ctx, int modelnum, const std::vector<mapbrush_t *> &brushes)
{
    bspxbrushes_permodel permodel{1, modelnum};

    for (auto &b : brushes) {
        permodel.numbrushes++;
        for (auto &f : b->faces) {
            /*skip axial*/
            const auto &plane = f.get_plane();
            if (plane.get_type() < plane_type_t::PLANE_ANYX)
                continue;
            permodel.numfaces++;
        }
    }

    std::ostringstream str(std::ios_base::out | std::ios_base::binary);

    str << endianness<std::endian::little>;

    str <= permodel;

    for (auto &b : brushes) {
        bspxbrushes_perbrush perbrush{};

        for (auto &f : b->faces) {
            /*skip axial*/
            const auto &plane = f.get_plane();
            if (plane.get_type() < plane_type_t::PLANE_ANYX)
                continue;
            perbrush.numfaces++;
        }

        perbrush.bounds = b->bounds;

        const auto &contents = b->contents;

        switch (contents.native) {
            // contents should match the engine.
            case CONTENTS_EMPTY: // really an error, but whatever
            case CONTENTS_SOLID: // these are okay
            case CONTENTS_WATER:
            case CONTENTS_SLIME:
            case CONTENTS_LAVA:
            case CONTENTS_SKY:
                if (contents.is_clip(qbsp_options.target_game)) {
                    perbrush.contents = -8;
                } else {
                    perbrush.contents = contents.native;
                }
                break;
            //              case CONTENTS_LADDER:
            //                      perbrush.contents = -16;
            //                      break;
            default: {
                if (contents.is_clip(qbsp_options.target_game)) {
                    perbrush.contents = -8;
                } else {
                    logging::print("WARNING: Unknown contents: {}. Translating to solid.\n",
                        contents.to_string(qbsp_options.target_game));
                    perbrush.contents = CONTENTS_SOLID;
                }
                break;
            }
        }

        str <= perbrush;

        for (auto &f : b->faces) {
            /*skip axial*/
            const auto &plane = f.get_plane();
            if (plane.get_type() < plane_type_t::PLANE_ANYX)
                continue;

            bspxbrushes_perface perface = qplane3f(plane.get_normal(), plane.get_dist());
            str <= std::tie(perface.normal, perface.dist);
        }
    }

    std::string data = str.str();
    ctx->lumpdata.insert(ctx->lumpdata.end(), (uint8_t *)data.data(), ((uint8_t *)data.data()) + data.size());
}

/* for generating BRUSHLIST bspx lump */
static void BSPX_CreateBrushList(void)
{
    struct bspxbrushes_s ctx;

    if (!qbsp_options.wrbrushes.value())
        return;

    BSPX_Brushes_Init(&ctx);

    for (size_t entnum = 0; entnum < map.entities.size(); ++entnum) {
        mapentity_t &ent = map.entities.at(entnum);
        size_t modelnum;

        if (IsWorldBrushEntity(ent)) {
            continue;
        }

        if (map.is_world_entity(ent)) {
            modelnum = 0;
        } else {
            const std::string &mod = ent.epairs.get("model");
            if (mod[0] != '*') {
                continue;
            }
            modelnum = std::stoi(mod.substr(1));
        }

        std::vector<mapbrush_t *> brushes;

        brushes.reserve(ent.mapbrushes.size());

        for (auto &b : ent.mapbrushes) {
            brushes.push_back(&b);
        }

        if (modelnum == 0) {

            for (size_t e = 1; e < map.entities.size(); ++e) {
                mapentity_t &bent = map.entities.at(e);

                brushes.reserve(brushes.size() + ent.mapbrushes.size());

                if (IsWorldBrushEntity(bent)) {

                    for (auto &b : bent.mapbrushes) {
                        brushes.push_back(&b);
                    }
                }
            }
        }

        if (!brushes.empty()) {
            BSPX_Brushes_AddModel(&ctx, modelnum, brushes);
        }
    }

    BSPX_Brushes_Finalize(&ctx);
}

/*
=================
CreateSingleHull
=================
*/
static void CreateSingleHull(hull_index_t hullnum)
{
    if (hullnum.has_value()) {
        logging::print("Processing hull {}...\n", hullnum.value());
    } else {
        logging::print("Processing map...\n");
    }

    // for each entity in the map file that has geometry
    for (auto &entity : map.entities) {
        bool wants_logging = true;

        // decide if we want to log this entity / hull combination
        if (!map.is_world_entity(entity)) {
            wants_logging = wants_logging && qbsp_options.logbmodels.value();
        }
        if (hullnum.value_or(0)) {
            wants_logging = wants_logging && qbsp_options.loghulls.value();
        }

        // update logging mask if requested
        const auto prev_logging_mask = logging::mask;
        if (!wants_logging) {
            logging::mask &= ~(
                bitflags<logging::flag>(logging::flag::STAT) | logging::flag::PROGRESS | logging::flag::CLOCK_ELAPSED);
        }

        ProcessEntity(entity, hullnum);

        // restore logging
        logging::mask = prev_logging_mask;
    }
}

/*
=================
CreateHulls
=================
*/
static void CreateHulls(void)
{
    /* create the hulls sequentially */
    auto &hulls = qbsp_options.target_game->get_hull_sizes();

    // game has no hulls, so we have to export brush lists and stuff.
    if (!hulls.size()) {
        CreateSingleHull(std::nullopt);
        return;
    }

    // all the hulls
    for (size_t i = 0; i < hulls.size(); i++) {
        CreateSingleHull(i);

        // only create hull 0 if fNoclip is set
        if (qbsp_options.noclip.value()) {
            break;
        }
    }
}

// Fill the BSP's `dtex` data
static void LoadTextureData()
{
    for (size_t i = 0; i < map.miptex.size(); i++) {
        // always fill the name even if we can't find it
        auto &miptex = map.bsp.dtex.textures[i];
        miptex.name = map.miptex[i].name;

        {
            auto [tex, pos, file] = img::load_texture(map.miptex[i].name, true, qbsp_options.target_game, qbsp_options);

            if (!tex) {
                if (pos.archive) {
                    logging::print("WARNING: unable to load texture {} in archive {}\n", map.miptex[i].name,
                        pos.archive->pathname);
                } else {
                    logging::print("WARNING: unable to find texture {}\n", map.miptex[i].name);
                }
            } else {
                miptex.width = tex->meta.width;
                miptex.height = tex->meta.height;

                // only mips can be embedded directly
                if (!qbsp_options.notextures.value() && !pos.archive->external &&
                    tex->meta.extension == img::ext::MIP) {
                    miptex.data = std::move(file.value());
                    continue;
                }
            }
        }

        // fall back to when we can't load the image.
        // construct fake data that solely contains the header.
        miptex.data.resize(sizeof(dmiptex_t));

        dmiptex_t header{};
        if (miptex.name.size() >= 16) {
            logging::print("WARNING: texture {} name too long for Quake miptex\n", miptex.name);
            std::copy_n(miptex.name.begin(), 15, header.name.begin());
        } else {
            std::copy(miptex.name.begin(), miptex.name.end(), header.name.begin());
        }

        header.width = miptex.width;
        header.height = miptex.height;
        header.offsets = {0, 0, 0, 0};

        omemstream stream(miptex.data.data(), miptex.data.size());
        stream <= header;
    }
}

static void AddAnimationFrames()
{
    size_t oldcount = map.miptex.size();

    for (size_t i = 0; i < oldcount; i++) {
        const std::string &existing_name = map.miptexTextureName(i);

        if (existing_name[0] != '+' && (qbsp_options.target_game->id != GAME_HALF_LIFE || existing_name[0] != '-')) {
            continue;
        }

        std::string name = map.miptexTextureName(i);

        /* Search for all animations (0-9) and alt-animations (A-J) */
        for (size_t j = 0; j < 20; j++) {
            name[1] = (j < 10) ? '0' + j : 'a' + j - 10;
            if (fs::where(name)) {
                FindMiptex(name.c_str());
            }
        }
    }

    logging::print(logging::flag::STAT, "     {:8} texture frames added\n", map.miptex.size() - oldcount);
}

static void LoadSecondaryTextures()
{
    // Q2 doesn't use any secondary textures
    if (qbsp_options.target_game->id == GAME_QUAKE_II) {
        return;
    }

    AddAnimationFrames();

    /* Default texture data to store in worldmodel */
    map.bsp.dtex.textures.resize(map.miptex.size());

    LoadTextureData();
}

/*
=================
ProcessFile
=================
*/
void ProcessFile()
{
    // load brushes and entities
    LoadMapFile();

    if (qbsp_options.convertmapformat.value() != conversion_t::none) {
        ConvertMapFile();
        return;
    }
    if (qbsp_options.onlyents.value()) {
        UpdateEntLump();
        return;
    }

    // handle load time operation on the .map
    ProcessMapBrushes();

    // initialize secondary textures
    LoadSecondaryTextures();

    // init the tables to be shared by all models
    BeginBSPFile();

    // create hulls!
    CreateHulls();

    WriteEntitiesToString();
    BSPX_CreateBrushList();
    FinishBSPFile();
}

/*
==================
MakeSkipTexinfo
==================
*/
static int MakeSkipTexinfo()
{
    maptexinfo_t mt{};

    mt.miptex = FindMiptex("skip", true);
    mt.flags.is_nodraw = true;

    return FindTexinfo(mt);
}

/*
==================
InitQBSP
==================
*/
void InitQBSP(int argc, const char **argv)
{
    // In case we're launched more than once, in testqbsp
    map.reset();
    qbsp_options.reset();

    qbsp_options.run(argc, argv);

    qbsp_options.map_path.replace_extension("map");

    // The .map extension gets removed right away anyways...
    if (qbsp_options.bsp_path.empty())
        qbsp_options.bsp_path = qbsp_options.map_path;

    /* Start logging to <bspname>.log */
    logging::init(fs::path(qbsp_options.bsp_path).replace_extension("log"), qbsp_options);

    // Remove already existing files
    if (!qbsp_options.onlyents.value() && qbsp_options.convertmapformat.value() == conversion_t::none) {
        qbsp_options.bsp_path.replace_extension("bsp");
        remove(qbsp_options.bsp_path);

        // Probably not the best place to do this
        logging::print("Input file: {}\n", fs::absolute(qbsp_options.map_path));
        logging::print("Output file: {}\n\n", fs::absolute(qbsp_options.bsp_path));

        fs::path prtfile = qbsp_options.bsp_path;
        prtfile.replace_extension("prt");
        remove(prtfile);

        fs::path ptsfile = qbsp_options.bsp_path;
        ptsfile.replace_extension("pts");
        remove(ptsfile);

        fs::path porfile = qbsp_options.bsp_path;
        porfile.replace_extension("por");
        remove(porfile);

        // areaportal leaks
        for (int i = 0;; i++) {
            fs::path name = qbsp_options.bsp_path;
            name.replace_extension(fmt::format("areaportal_leak{}.pts", i));

            if (!remove(name)) {
                break;
            }
        }
    }

    // onlyents might not load this yet
    if (qbsp_options.target_game) {
        qbsp_options.target_game->init_filesystem(qbsp_options.map_path, qbsp_options);
    }

    // make skip texinfo, in case map needs it (it'll get culled out if not)
    map.skip_texinfo = MakeSkipTexinfo();
}

void InitQBSP(const std::vector<std::string> &args)
{
    std::vector<const char *> argPtrs;
    for (const std::string &arg : args) {
        argPtrs.push_back(arg.data());
    }

    InitQBSP(argPtrs.size(), argPtrs.data());
}

#include <fstream>

/*
==================
main
==================
*/
int qbsp_main(int argc, const char **argv)
{
    InitQBSP(argc, argv);

    // do it!
    auto start = I_FloatTime();
    ProcessFile();
    auto end = I_FloatTime();

    logging::print("\n{:.3} seconds elapsed\n", (end - start));

    logging::close();

    return 0;
}
