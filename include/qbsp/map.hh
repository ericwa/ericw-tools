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

struct bspbrush_t;

struct mapface_t
{
    size_t planenum;
    std::array<qvec3d, 3> planepts{};
    std::string texname{};
    int texinfo = 0;
    parser_source_location line;
    bool bevel = false;
    bool visible = false;
    winding_t winding; // winding used to calculate bevels

    surfflags_t flags{};

    // Q2 stuff
    contentflags_t contents{};
    int value = 0;

    // for convert
    std::optional<extended_texinfo_t> raw_info;

    bool set_planepts(const std::array<qvec3d, 3> &pts);

    const texvecf &get_texvecs() const;
    void set_texvecs(const texvecf &vecs);

    const qbsp_plane_t &get_plane() const;
};

enum class brushformat_t
{
    NORMAL,
    BRUSH_PRIMITIVES
};

class mapbrush_t
{
public:
    std::vector<mapface_t> faces;
    brushformat_t format = brushformat_t::NORMAL;
    aabb3d bounds {};
    std::optional<uint32_t> outputnumber; /* only set for original brushes */
    parser_source_location line;
    contentflags_t contents {};
};

struct lumpdata
{
    int count;
    int index;
    void *data;
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
    qvec3d origin{};
    rotation_t rotation;

    std::list<mapbrush_t> mapbrushes;

    size_t numboxbevels = 0;
    size_t numedgebevels = 0;

    // key/value pairs in the order they were parsed
    entdict_t epairs;

    aabb3d bounds;

    int firstoutputfacenumber = -1;
    std::optional<size_t> outputmodelnumber = std::nullopt;

    int32_t areaportalnum = 0;
    std::array<int32_t, 2> portalareas = {};

    parser_source_location location;
};

struct maptexdata_t
{
    std::string name;
    surfflags_t flags;
    int32_t value;
    std::string animation;
    int32_t animation_miptex = -1;
};

#include <common/imglib.hh>

extern std::shared_mutex map_planes_lock;

struct mapplane_t : qbsp_plane_t
{
    std::optional<size_t> outputnum;

    inline mapplane_t(const qbsp_plane_t &copy) : qbsp_plane_t(copy) { }
};

constexpr size_t hash_combine(size_t lhs, size_t rhs)
{
    return lhs ^ rhs + 0x9e3779b9 + (lhs << 6) + (lhs >> 2);
}

struct qbsp_plane_hash
{
    size_t operator()(const qbsp_plane_t &plane) const
    {
        return hash_combine(
            std::hash<vec_t>()((int32_t) (fabs(plane.get_dist()) / 2.0)),
            std::hash<plane_type_t>()(plane.get_type())
        );
    }
};

struct qbsp_plane_eq
{
    bool operator()(const qbsp_plane_t &a, const qbsp_plane_t &b) const
    {
        return qv::epsilonEqual(a, b);
    }
};

#include <pareto/spatial_map.h>

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
    // planes hashed by dist
    std::unordered_multimap<qbsp_plane_t, size_t, qbsp_plane_hash, qbsp_plane_eq> plane_hash;

    // add the specified plane to the list
    inline size_t add_plane(const qplane3d &plane)
    {
        planes.emplace_back(plane);
        planes.emplace_back(-plane);

        auto &positive = planes[planes.size() - 2];
        auto &negative = planes[planes.size() - 1];

        size_t result;

        if (positive.get_normal()[static_cast<int32_t>(positive.get_type()) % 3] < 0.0) {
            std::swap(positive, negative);
            result = planes.size() - 1;
        } else {
            result = planes.size() - 2;
        }
        
        plane_hash.emplace(positive, planes.size() - 2);
        plane_hash.emplace(negative, planes.size() - 1);

        return result;
    }
    
    // find the specified plane in the list if it exists. throws
    // if not.
    inline size_t find_plane(const qplane3d &plane)
    {
        auto range = plane_hash.equal_range(plane);
        for (auto it = range.first; it != range.second; ++it) {
            if (qv::epsilonEqual(it->first, plane)) {
                return it->second;
            }
        }

        throw std::bad_function_call();
    }

    // find the specified plane in the list if it exists, or
    // return a new one
    inline size_t add_or_find_plane(const qplane3d &plane)
    {
        auto range = plane_hash.equal_range(plane);
        for (auto it = range.first; it != range.second; ++it) {
            if (qv::epsilonEqual(it->first, plane)) {
                return it->second;
            }
        }

        return add_plane(plane);
    }

    inline const qbsp_plane_t &get_plane(size_t pnum)
    {
        return planes[pnum];
    }

    std::vector<maptexdata_t> miptex;
    std::vector<maptexinfo_t> mtexinfos;

    /* quick lookup for texinfo */
    std::map<maptexinfo_t, int> mtexinfo_lookup;

    /* map from plane hash code to list of indicies in `planes` vector */
    std::unordered_map<int, std::vector<int>> planehash;

    // hashed vertices; generated by EmitVertices
    pareto::spatial_map<vec_t, 3, size_t> hashverts {};

    // find vector of points in hash closest to vec
    inline auto find_hash_vector(const qvec3d &vec)
    {
        return hashverts.find({floor(vec[0]), floor(vec[1]), floor(vec[2])});
    }

    // find output index for specified already-output vector.
    inline std::optional<size_t> find_emitted_hash_vector(const qvec3d &vert)
    {
        static const vec_t point_epsilon_with_border = std::nextafter(POINT_EQUAL_EPSILON, 1.0);

        if (auto it = hashverts.find_intersection({{ vert[0] - (POINT_EQUAL_EPSILON * 0.5), vert[1] - (POINT_EQUAL_EPSILON * 0.5), vert[2] - (POINT_EQUAL_EPSILON * 0.5) }},
            {{ vert[0] + (POINT_EQUAL_EPSILON * 0.5) }, { vert[1] + (POINT_EQUAL_EPSILON * 0.5) }, { vert[2] + (POINT_EQUAL_EPSILON * 0.5) }}); it != hashverts.end()) {
            return it->second;
        }

        return std::nullopt;
    }

    // add vector to hash
    inline void add_hash_vector(const qvec3d &point, const size_t &num)
    {
        hashverts.emplace(pareto::point<vec_t, 3>({ point[0], point[1], point[2] }), num);
    }

    // hashed edges; generated by EmitEdges
    std::map<std::pair<size_t, size_t>, int64_t> hashedges;

    inline void add_hash_edge(size_t v1, size_t v2, int64_t i) { hashedges.emplace(std::make_pair(v1, v2), i); }

    /* Misc other global state for the compile process */
    bool leakfile = false; /* Flag once we've written a leak (.por/.pts) file */

    // Final, exported BSP
    mbsp_t bsp;

    // bspx data
    std::vector<uint8_t> exported_lmshifts;
    bool needslmshifts = false;
    std::vector<uint8_t> exported_bspxbrushes;

    // Q2 stuff
    int32_t c_areas = 0;
    int32_t numareaportals = 0;
    // running total
    uint32_t brush_offset = 0;
    // Small cache for image meta in the current map
    std::unordered_map<std::string, std::optional<img::texture_meta>> meta_cache;
    // load or fetch image meta associated with the specified name
    const std::optional<img::texture_meta> &load_image_meta(const std::string_view &name);
    // whether we had attempted loading texture stuff
    bool textures_loaded = false;

    // helpers
    const std::string &miptexTextureName(int mt) const { return miptex.at(mt).name; }

    const std::string &texinfoTextureName(int texinfo) const { return miptexTextureName(mtexinfos.at(texinfo).miptex); }

    int skip_texinfo;

    mapentity_t *world_entity();

    void reset();
};

extern mapdata_t map;

void CalculateWorldExtent(void);

bool ParseEntity(parser_t &parser, mapentity_t *entity);

void ProcessExternalMapEntity(mapentity_t *entity);
void ProcessAreaPortal(mapentity_t *entity);
bool IsWorldBrushEntity(const mapentity_t *entity);
bool IsNonRemoveWorldBrushEntity(const mapentity_t *entity);
void LoadMapFile(void);
void ConvertMapFile(void);
void ProcessMapBrushes();

struct quark_tx_info_t
{
    bool quark_tx1 = false;
    bool quark_tx2 = false;

    std::optional<extended_texinfo_t> info;
};

int FindMiptex(
    const char *name, std::optional<extended_texinfo_t> &extended_info, bool internal = false, bool recursive = true);
inline int FindMiptex(const char *name, bool internal = false, bool recursive = true)
{
    std::optional<extended_texinfo_t> extended_info;
    return FindMiptex(name, extended_info, internal, recursive);
}
int FindTexinfo(const maptexinfo_t &texinfo);

void PrintEntity(const mapentity_t *entity);

void WriteEntitiesToString();

qvec3d FixRotateOrigin(mapentity_t *entity);

/** Special ID for the collision-only hull; used for wrbrushes/Q2 */
constexpr int HULL_COLLISION = -1;

/* Create BSP brushes from map brushes */
void Brush_LoadEntity(mapentity_t *entity, const int hullnum, bspbrush_vector_t &brushes);

std::list<face_t *> CSGFace(
    face_t *srcface, const mapentity_t *srcentity, const bspbrush_t *srcbrush, const node_t *srcnode);
void TJunc(node_t *headnode);
int MakeFaceEdges(node_t *headnode);
void EmitVertices(node_t *headnode);
void ExportClipNodes(mapentity_t *entity, node_t *headnode, const int hullnum);
void ExportDrawNodes(mapentity_t *entity, node_t *headnode, int firstface);

struct bspxbrushes_s
{
    std::vector<uint8_t> lumpdata;
};
void BSPX_Brushes_Finalize(struct bspxbrushes_s *ctx);
void BSPX_Brushes_Init(struct bspxbrushes_s *ctx);

void ExportObj_Faces(const std::string &filesuffix, const std::vector<const face_t *> &faces);
void ExportObj_Brushes(const std::string &filesuffix, const std::vector<const bspbrush_t *> &brushes);
void ExportObj_Nodes(const std::string &filesuffix, const node_t *nodes);
void ExportObj_Marksurfaces(const std::string &filesuffix, const node_t *nodes);

void WriteBspBrushMap(const fs::path &name, const std::vector<std::unique_ptr<bspbrush_t>> &list);

bool IsValidTextureProjection(const qvec3f &faceNormal, const qvec3f &s_vec, const qvec3f &t_vec);
