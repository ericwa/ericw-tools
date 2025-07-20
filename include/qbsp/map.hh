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

#include <qbsp/qbsp.hh>
#include <qbsp/brush.hh>

#include <common/bspfile.hh>
#include <common/parser.hh>
#include "common/cmdlib.hh"

#include <optional>
#include <vector>
#include <utility>
#include <unordered_map>
#include <list>
#include <mutex>
#include <shared_mutex>
#include <string_view>

struct mapface_t
{
    size_t planenum;
    std::array<qvec3d, 3> planepts{};
    int texinfo = 0;
    parser_source_location line;
    // the lmshift value of the brush. stored here because
    // mapfaces don't link back to the mapbrush_t
    int16_t lmshift = 0;
    // the raw texture name of this face. this is technically
    // duplicated information, as the miptex stores the name too,
    // but it is also here for quicker lookups.
    std::string texname{};

    // brushes can technically have different contents on each side;
    // in Q1's case, consider `*water` on one and `brick` on another.
    // in Q2's case, contents are per-face (probably since brushes didn't have
    // any data on them other than faces), but only the first valid contents
    // end up being used. This stores the per-side contents, but be careful
    // about using this since it is often merged into a single contents
    // value on mapbrush_t.
    contentflags_t contents{};

    // winding used to calculate bevels; this is not valid after
    // brush processing.
    winding_t winding;

    // the raw info that we pulled from the .map file
    // with no transformations; this is for conversions only.
    std::optional<extended_texinfo_t> raw_info;

    bool visible; // can any part of this side be seen from non-void parts of the level?
                  // non-visible means we can discard the brush side
                  // (avoiding generating a BSP spit, so expanding it outwards)

    // this face is a bevel added by AddBrushBevels, and shouldn't be used as a splitter
    // for the main hull.
    bool bevel = false;

    bool set_planepts(const std::array<qvec3d, 3> &pts);

    const maptexinfo_t &get_texinfo() const;

    const texvecf &get_texvecs() const;
    void set_texvecs(const texvecf &vecs);

    const qbsp_plane_t &get_plane() const;
    const qbsp_plane_t &get_positive_plane() const;
};

class mapentity_t;

class mapbrush_t
{
public:
    std::vector<mapface_t> faces;
    aabb3d bounds{};
    std::optional<uint32_t> outputnumber; /* only set for original brushes */
    parser_source_location line;
    contentflags_t contents{};
    int16_t lmshift = 0; /* lightmap scaling (qu/lightmap pixel), passed to the light util */
    mapentity_t *func_areaportal = nullptr;
    bool is_hint = false; // whether we are a hint brush or not (at least one side is "hint" or SURF_HINT)
    int32_t chop_index = 0; // chopping order; higher numbers chop lower numbers

    std::tuple<int32_t, std::optional<size_t>> sort_key() const;
};

enum class rotation_t
{
    none,
    hipnotic,
    origin_brush
};

class mapentity_t
{
public:
    qvec3f origin{};
    rotation_t rotation;

    std::vector<mapbrush_t> mapbrushes;

    size_t numboxbevels = 0;
    size_t numedgebevels = 0;

    // key/value pairs in the order they were parsed
    entdict_t epairs;

    aabb3d bounds;

    std::optional<size_t> firstoutputfacenumber = std::nullopt;
    std::optional<size_t> outputmodelnumber = std::nullopt;

    int32_t areaportalnum = 0;
    std::array<int32_t, 2> portalareas = {};

    parser_source_location location;

    // warnings
    bool wrote_doesnt_touch_two_areas_warning = false;
};

struct maptexdata_t
{
    std::string name;
    surfflags_t flags;
    int32_t value;
    std::string animation;
    std::optional<int32_t> animation_miptex = std::nullopt;
};

#include <common/imglib.hh>

struct mapplane_t : qbsp_plane_t
{
    std::optional<size_t> outputnum;

    mapplane_t(const qbsp_plane_t &copy);
};

struct planehash_t;
struct vertexhash_t;

struct hashedge_t
{
    size_t v1;
    size_t v2;

    int64_t edge_index;

    /**
     * the face that edge v1 -> v2 belongs to
     */
    const face_t *face;

    /**
     * Has v2 -> v1 been referenced by another face yet, by using -edge_index?
     * This is only allowed to happen once (software renderer limitation).
     */
    bool has_been_reused;
};

struct mapdata_t
{
    /* Arrays of actual items */
    std::vector<mapentity_t> entities;

    // total number of brushes in the map
    size_t total_brushes;

    // this vector stores all of the planes that can potentially be
    // output in the BSP, from the map's own sides. The positive planes
    // come first (are even-numbered, with 0 being even) and the negative
    // planes are odd-numbered.
    std::vector<mapplane_t> planes;

    // planes indices (into the `planes` vector)
    std::unique_ptr<planehash_t> plane_hash;

    mapdata_t();

    // add the specified plane to the list
    size_t add_plane(const qplane3d &plane);

    std::optional<size_t> find_plane_nonfatal(const qplane3d &plane);

    // find the specified plane in the list if it exists. throws
    // if not.
    size_t find_plane(const qplane3d &plane);

    // find the specified plane in the list if it exists, or
    // return a new one
    size_t add_or_find_plane(const qplane3d &plane);

    const qbsp_plane_t &get_plane(size_t pnum);

    std::vector<maptexdata_t> miptex;
    std::vector<maptexinfo_t> mtexinfos;

    /* quick lookup for texinfo */
    std::map<maptexinfo_t, int> mtexinfo_lookup;

    // hashed vertices; generated by EmitVertices
    std::unique_ptr<vertexhash_t> hashverts;

    // find output index for specified already-output vector.
    std::optional<size_t> find_emitted_hash_vector(const qvec3d &vert);

    // add vector to hash
    void add_hash_vector(const qvec3d &point, size_t num);

    // hashed edges; generated by EmitEdges
    std::map<std::pair<size_t, size_t>, hashedge_t> hashedges;

    void add_hash_edge(size_t v1, size_t v2, int64_t edge_index, const face_t *face);

    /* Misc other global state for the compile process */
    bool leakfile = false; /* Flag once we've written a leak (.por/.pts) file */

    // Final, exported BSP
    mbsp_t bsp;

    // bspx data
    std::vector<uint8_t> exported_lmshifts;
    bool needslmshifts = false;
    std::vector<uint8_t> exported_bspxbrushes;

    // contents flags to write to content.json
    std::vector<contentflags_t> exported_extended_contentflags;

    // Q2 stuff
    int32_t c_areas = 0;
    int32_t numareaportals = 0;
    int32_t numareaportal_leaks = 0;
    // running total
    uint32_t brush_offset = 0;
    // Small cache for image meta in the current map
    std::unordered_map<std::string, std::optional<img::texture_meta>> meta_cache;
    // load or fetch image meta associated with the specified name
    const std::optional<img::texture_meta> &load_image_meta(std::string_view name);
    // whether we had attempted loading texture stuff
    bool textures_loaded = false;

    // map compile region
    std::optional<mapbrush_t> region = std::nullopt;
    std::vector<mapbrush_t> antiregions;

    // helpers
    const std::string &miptexTextureName(int mt) const;
    const std::string &texinfoTextureName(int texinfo) const;

    int skip_texinfo;

    mapentity_t &world_entity();
    bool is_world_entity(const mapentity_t &entity);

    void reset();
};

extern mapdata_t map;

void CalculateWorldExtent();

struct texture_def_issues_t : logging::stat_tracker_t
{
    // number of faces that have SKY | NODRAW mixed. this is a Q2-specific issue
    // that is a bit weird, because NODRAW indicates that the face should not be
    // emitted at all in Q1 compilers, whereas in qbsp3 it only left out a texinfo
    // reference (in theory...); this meant that sky brushes would disappear. It
    // doesn't really make sense to have these two mixed, because sky is drawn in-game
    // and the texture is still referenced on them.
    stat &num_sky_nodraw = register_stat(
        "faces have SKY | NODRAW flags mixed; NODRAW removed as this combo makes no sense. Use -verbose to display affected faces.",
        false, true);

    // Q2 specific: TRANSLUCENT is an internal compiler flag and should never
    // be set directly. In older tools, the only side effect this has is to
    // turn it into DETAIL effectively.
    stat &num_translucent = register_stat(
        "faces have TRANSLUCENT flag swapped to DETAIL; TRANSLUCENT is an internal flag. Use -verbose to display affected faces.",
        false, true);

    stat &num_repaired = register_stat(
        "faces have invalid texture projections and were repaired. Use -verbose to display affected faces.", false,
        true);
};

namespace mapfile
{
struct map_entity_t;
}
void ParseEntity(const mapfile::map_entity_t &in_entity, mapentity_t &entity, texture_def_issues_t &issue_stats);

void ProcessExternalMapEntity(mapentity_t &entity);
void ProcessAreaPortal(mapentity_t &entity);
bool IsWorldBrushEntity(const mapentity_t &entity);
bool IsNonRemoveWorldBrushEntity(const mapentity_t &entity);
void LoadMapFile();
void ConvertMapFile();
void ProcessMapBrushes();

struct quark_tx_info_t
{
    std::optional<extended_texinfo_t> info;
};

int FindMiptex(
    const char *name, std::optional<extended_texinfo_t> &extended_info, bool internal = false, bool recursive = true);
int FindMiptex(const char *name, bool internal = false, bool recursive = true);
int FindTexinfo(const maptexinfo_t &texinfo, const qplane3d &plane, bool add = true);

void PrintEntity(const mapentity_t &entity);

void WriteEntitiesToString();

qvec3d FixRotateOrigin(mapentity_t &entity);

/* Create BSP brushes from map brushes */
void Brush_LoadEntity(mapentity_t &entity, hull_index_t hullnum, bspbrush_t::container &brushes, size_t &num_clipped);

size_t EmitFaces(node_t *headnode);
void EmitVertices(node_t *headnode);
void ExportClipNodes(mapentity_t &entity, node_t *headnode, hull_index_t::value_type hullnum);
void ExportDrawNodes(mapentity_t &entity, node_t *headnode, int firstface);
void WriteBspBrushMap(std::string_view filename_suffix, const bspbrush_t::container &list);

bool IsValidTextureProjection(const qvec3f &faceNormal, const qvec3f &s_vec, const qvec3f &t_vec);
