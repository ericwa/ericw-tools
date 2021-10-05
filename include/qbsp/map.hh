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

#ifndef QBSP_MAP_HH
#define QBSP_MAP_HH

#include <qbsp/parser.hh>

#include <optional>
#include <vector>

typedef struct epair_s {
    struct epair_s *next;
    char *key;
    char *value;
} epair_t;

struct mapface_t {
    qbsp_plane_t plane;
    vec3_t planepts[3];
    std::string texname;
    int texinfo;
    int linenum;

    surfflags_t flags;
    
    // Q2 stuff
    int contents;
    int value;
    
    mapface_t() :
        texinfo(0),
        linenum(0),
        contents(0),
        flags({}),
        value(0) {
        memset(&plane, 0, sizeof(plane));
        for (int i=0; i<3; i++) {
            VectorSet(planepts[i], 0, 0, 0);
        }
    }
    
    bool set_planepts(const vec3_t *pts);
    
    std::array<qvec4f, 2> get_texvecs(void) const;
    void set_texvecs(const std::array<qvec4f, 2> &vecs);
};

enum class brushformat_t {
    NORMAL, BRUSH_PRIMITIVES
};
    
class mapbrush_t {
public:
    int firstface = 0;
    int numfaces = 0;
    brushformat_t format = brushformat_t::NORMAL;
    int contents = 0;

    const mapface_t &face(int i) const;
};

struct lumpdata {
    int count;
    int index;
    void *data;
};

class mapentity_t {
public:
    vec3_t origin;

    int firstmapbrush;
    int nummapbrushes;
    
    // Temporary lists used to build `brushes` in the correct order.
    brush_t *solid, *sky, *detail, *detail_illusionary, *detail_fence, *liquid;
    
    epair_t *epairs;
    vec3_t mins, maxs;
    brush_t *brushes;           /* NULL terminated list */
    int numbrushes;
    
    int firstoutputfacenumber;
    int outputmodelnumber;

    const mapbrush_t &mapbrush(int i) const;
    
    mapentity_t() :
    firstmapbrush(0),
    nummapbrushes(0),
    solid(nullptr),
    sky(nullptr),
    detail(nullptr),
    detail_illusionary(nullptr),
    detail_fence(nullptr),
    liquid(nullptr),
    epairs(nullptr),
    brushes(nullptr),
    numbrushes(0),
    firstoutputfacenumber(-1),
    outputmodelnumber(-1) {
        VectorSet(origin,0,0,0);
        VectorSet(mins,0,0,0);
        VectorSet(maxs,0,0,0);
    }
};

struct texdata_t {
    std::string     name;
    int32_t         flags, value;
};

typedef struct mapdata_s {
    /* Arrays of actual items */
    std::vector<mapface_t> faces;
    std::vector<mapbrush_t> brushes;
    std::vector<mapentity_t> entities;
    std::vector<qbsp_plane_t> planes;
    std::vector<texdata_t> miptex;
    std::vector<mtexinfo_t> mtexinfos;
    
    /* quick lookup for texinfo */
    std::map<mtexinfo_t, int> mtexinfo_lookup;
    
    /* map from plane hash code to list of indicies in `planes` vector */
    std::unordered_map<int, std::vector<int>> planehash;
    
    /* Number of items currently used */
    int numfaces() const { return faces.size(); };
    int numbrushes() const { return brushes.size(); };
    int numentities() const { return entities.size(); };
    int numplanes() const { return planes.size(); };
    int nummiptex() const { return miptex.size(); };
    int numtexinfo() const { return static_cast<int>(mtexinfos.size()); };

    /* Misc other global state for the compile process */
    bool leakfile;      /* Flag once we've written a leak (.por/.pts) file */
    
    // Final, exported data
    std::vector<gtexinfo_t> exported_texinfos;
    std::vector<dplane_t> exported_planes;
    std::vector<mleaf_t> exported_leafs;
    std::vector<bsp2_dnode_t> exported_nodes;
    std::vector<uint32_t> exported_marksurfaces;
    std::vector<bsp2_dclipnode_t> exported_clipnodes;
    std::vector<bsp2_dedge_t> exported_edges;
    std::vector<dvertex_t> exported_vertexes;
    std::vector<int32_t> exported_surfedges;
    std::vector<bsp2_dface_t> exported_faces;
    std::vector<dmodelh2_t> exported_models;
    std::vector<uint32_t> exported_leafbrushes;
    std::vector<q2_dbrushside_qbism_t> exported_brushsides;
    std::vector<dbrush_t> exported_brushes;

    std::string exported_entities;
    std::string exported_texdata;

    // bspx data
    std::vector<uint8_t> exported_lmshifts;
    bool needslmshifts = false;
    std::vector<uint8_t> exported_bspxbrushes;

    // helpers
    const std::string &miptexTextureName(int mt) const {
        return miptex.at(mt).name;
    }

    const std::string &texinfoTextureName(int texinfo) const {
        return miptexTextureName(mtexinfos.at(texinfo).miptex);
    }
} mapdata_t;

extern mapdata_t map;
extern mapentity_t *pWorldEnt();

bool ParseEntity(parser_t *parser, mapentity_t *entity);

void EnsureTexturesLoaded();
void ProcessExternalMapEntity(mapentity_t *entity);
bool IsWorldBrushEntity(const mapentity_t *entity);
void LoadMapFile(void);
mapentity_t LoadExternalMap(const char *filename);
void ConvertMapFile(void);

struct extended_texinfo_t {
    int contents = 0;
    int flags = 0;
    int value = 0;
};

struct quark_tx_info_t {
    bool quark_tx1 = false;
    bool quark_tx2 = false;
    
    std::optional<extended_texinfo_t> info;
};

int FindMiptex(const char *name, std::optional<extended_texinfo_t> &extended_info, bool internal = false);
inline int FindMiptex(const char *name, bool internal = false) {
    std::optional<extended_texinfo_t> extended_info;
    return FindMiptex(name, extended_info, internal);
}
int FindTexinfo(const mtexinfo_t &texinfo);

void PrintEntity(const mapentity_t *entity);
const char *ValueForKey(const mapentity_t *entity, const char *key);
void SetKeyValue(mapentity_t *entity, const char *key, const char *value);
int GetVectorForKey(const mapentity_t *entity, const char *szKey, vec3_t vec);

void WriteEntitiesToString(void);

void FixRotateOrigin(mapentity_t *entity);

/* Create BSP brushes from map brushes in src and save into dst */
void Brush_LoadEntity(mapentity_t *dst, const mapentity_t *src,
                      const int hullnum);

/* Builds the dst->brushes list. Call after Brush_LoadEntity. */
void Entity_SortBrushes(mapentity_t *dst);


surface_t *CSGFaces(const mapentity_t *entity);
void PortalizeWorld(const mapentity_t *entity, node_t *headnode, const int hullnum);
void TJunc(const mapentity_t *entity, node_t *headnode);
node_t *SolidBSP(const mapentity_t *entity, surface_t *surfhead, bool midsplit);
int MakeFaceEdges(mapentity_t *entity, node_t *headnode);
void ExportClipNodes(mapentity_t *entity, node_t *headnode, const int hullnum);
void ExportDrawNodes(mapentity_t *entity, node_t *headnode, int firstface);

struct bspxbrushes_s {
    std::vector<uint8_t> lumpdata;
};
void BSPX_Brushes_Finalize(struct bspxbrushes_s *ctx);
void BSPX_Brushes_Init(struct bspxbrushes_s *ctx);
void BSPX_Brushes_AddModel(struct bspxbrushes_s *ctx, int modelnum, brush_t *brushes);

void ExportObj_Faces(const std::string &filesuffix, const std::vector<const face_t *> &faces);
void ExportObj_Brushes(const std::string &filesuffix, const std::vector<const brush_t *> &brushes);
void ExportObj_Surfaces(const std::string &filesuffix, const surface_t *surfaces);
void ExportObj_Nodes(const std::string &filesuffix, const node_t *nodes);
void ExportObj_Marksurfaces(const std::string &filesuffix, const node_t *nodes);

void WriteBspBrushMap(const char *name, const std::vector<const brush_t *> &list);

bool IsValidTextureProjection(const qvec3f &faceNormal, const qvec3f &s_vec, const qvec3f &t_vec);


#endif
