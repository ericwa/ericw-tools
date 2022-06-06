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

#include <common/bspfile.hh>
#include <common/parser.hh>
#include "common/cmdlib.hh"

#include <optional>
#include <vector>
#include <utility>
#include <unordered_map>
#include <list>

struct brush_t;

struct qbsp_plane_t : qplane3d
{
    int type = 0;
    std::optional<size_t> outputplanenum = std::nullopt; // only valid after ExportNodePlanes

    [[nodiscard]] constexpr qbsp_plane_t operator-() const { return {qplane3d::operator-(), type}; }
};

struct extended_texinfo_t
{
    contentflags_t contents = {0};
    surfflags_t flags = {0};
    int value = 0;
    std::string animation;
};

struct mapface_t
{
    qbsp_plane_t plane{};
    std::array<qvec3d, 3> planepts{};
    std::string texname{};
    int texinfo = 0;
    int linenum = 0;

    surfflags_t flags{};

    // Q2 stuff
    contentflags_t contents{};
    int value = 0;

    // for convert
    std::optional<extended_texinfo_t> raw_info;

    bool set_planepts(const std::array<qvec3d, 3> &pts);

    const texvecf &get_texvecs() const;
    void set_texvecs(const texvecf &vecs);
};

enum class brushformat_t
{
    NORMAL,
    BRUSH_PRIMITIVES
};

class mapbrush_t
{
public:
    int firstface = 0;
    int numfaces = 0;
    brushformat_t format = brushformat_t::NORMAL;
    int contents = 0;

    const mapface_t &face(int i) const;
};

struct lumpdata
{
    int count;
    int index;
    void *data;
};

class mapentity_t
{
public:
    qvec3d origin{};

    int firstmapbrush = 0;
    int nummapbrushes = 0;

    // key/value pairs in the order they were parsed
    std::vector<std::pair<std::string, std::string>> epairs;

    aabb3d bounds;
    std::vector<std::unique_ptr<brush_t>> brushes;

    int firstoutputfacenumber = -1;
    std::optional<size_t> outputmodelnumber = std::nullopt;

    int32_t areaportalnum = 0;
    std::array<int32_t, 2> portalareas = {};

    const mapbrush_t &mapbrush(int i) const;
};

struct texdata_t
{
    std::string name;
    surfflags_t flags;
    int32_t value;
    std::string animation;
    int32_t animation_miptex = -1;
};

#include <common/imglib.hh>
#include <qbsp/wad.hh>

struct mapdata_t
{
private:
    // protected by read/write lock
    std::vector<qbsp_plane_t> planes;
public:
    size_t plane_size() const;
    qbsp_plane_t plane(size_t i) const;
    void emplace_plane(const qbsp_plane_t &plane, size_t &out_index);

    // only use for output/when it's safe; not locked
    inline qbsp_plane_t &mapdata_t::plane_ref(size_t i)
    {
        return planes[i];
    }

    /* Arrays of actual items */
    std::vector<mapface_t> faces;
    std::vector<mapbrush_t> brushes;
    std::vector<mapentity_t> entities;
    std::vector<texdata_t> miptex;
    std::vector<mtexinfo_t> mtexinfos;

    /* quick lookup for texinfo */
    std::map<mtexinfo_t, int> mtexinfo_lookup;

    /* map from plane hash code to list of indicies in `planes` vector */
    std::unordered_map<int, std::vector<int>> planehash;

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
    // Small cache for .wals
    std::unordered_map<std::string, std::optional<img::texture_meta>> wal_cache;

    // misc
    int start_spots = 0;
    bool wadlist_tried_loading = false;
    std::list<wad_t> wadlist;

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

void EnsureTexturesLoaded();
void ProcessExternalMapEntity(mapentity_t *entity);
void ProcessAreaPortal(mapentity_t *entity);
bool IsWorldBrushEntity(const mapentity_t *entity);
bool IsNonRemoveWorldBrushEntity(const mapentity_t *entity);
void LoadMapFile(void);
mapentity_t LoadExternalMap(const char *filename);
void ConvertMapFile(void);

struct quark_tx_info_t
{
    bool quark_tx1 = false;
    bool quark_tx2 = false;

    std::optional<extended_texinfo_t> info;
};

int FindMiptex(const char *name, std::optional<extended_texinfo_t> &extended_info, bool internal = false, bool recursive = true);
inline int FindMiptex(const char *name, bool internal = false, bool recursive = true)
{
    std::optional<extended_texinfo_t> extended_info;
    return FindMiptex(name, extended_info, internal, recursive);
}
int FindTexinfo(const mtexinfo_t &texinfo);

void PrintEntity(const mapentity_t *entity);
const char *ValueForKey(const mapentity_t *entity, const char *key);
void SetKeyValue(mapentity_t *entity, const char *key, const char *value);
int GetVectorForKey(const mapentity_t *entity, const char *szKey, qvec3d &vec);

void WriteEntitiesToString();

void FixRotateOrigin(mapentity_t *entity);

struct brush_stats_t
{
    size_t detail_illusionary;
    size_t liquid;
    size_t detail_fence;
    size_t detail;
    size_t sky;
    size_t solid;
};

/** Special ID for the collision-only hull; used for wrbrushes/Q2 */
constexpr int HULL_COLLISION = -1;

/* Create BSP brushes from map brushes */
brush_stats_t Brush_LoadEntity(mapentity_t *entity, const int hullnum);

std::list<face_t *> CSGFace(face_t *srcface, const mapentity_t* srcentity, const brush_t *srcbrush, const node_t *srcnode);
void TJunc(const mapentity_t *entity, node_t *headnode);
int MakeFaceEdges(mapentity_t *entity, node_t *headnode);
void ExportClipNodes(mapentity_t *entity, node_t *headnode, const int hullnum);
void ExportDrawNodes(mapentity_t *entity, node_t *headnode, int firstface);

struct bspxbrushes_s
{
    std::vector<uint8_t> lumpdata;
};
void BSPX_Brushes_Finalize(struct bspxbrushes_s *ctx);
void BSPX_Brushes_Init(struct bspxbrushes_s *ctx);

void ExportObj_Faces(const std::string &filesuffix, const std::vector<const face_t *> &faces);
void ExportObj_Brushes(const std::string &filesuffix, const std::vector<const brush_t *> &brushes);
void ExportObj_Nodes(const std::string &filesuffix, const node_t *nodes);
void ExportObj_Marksurfaces(const std::string &filesuffix, const node_t *nodes);

void WriteBspBrushMap(const fs::path &name, const std::vector<std::unique_ptr<brush_t>> &list);

bool IsValidTextureProjection(const qvec3f &faceNormal, const qvec3f &s_vec, const qvec3f &t_vec);
