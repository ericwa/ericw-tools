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
    
    // Q2 stuff
    int contents;
    int flags;
    int value;
    
    mapface_t() :
    texinfo(0),
    linenum(0),
    contents(0),
    flags(0),
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
    int firstface;
    int numfaces;
    brushformat_t format;
    int contents;
    
    mapbrush_t() :
        firstface(0),
        numfaces(0),
        format(brushformat_t::NORMAL),
        contents(0) {}
    const mapface_t &face(int i) const;
} ;

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
    struct lumpdata lumps[BSPX_LUMPS];
    
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
    numbrushes(0) {
        VectorSet(origin,0,0,0);
        VectorSet(mins,0,0,0);
        VectorSet(maxs,0,0,0);
        memset(lumps, 0, sizeof(lumps));
    }
};

typedef struct mapdata_s {
    /* Arrays of actual items */
    std::vector<mapface_t> faces;
    std::vector<mapbrush_t> brushes;
    std::vector<mapentity_t> entities;
    std::vector<qbsp_plane_t> planes;
    std::vector<miptex_t> miptex;
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

    /* Totals for BSP data items -> TODO: move to a bspdata struct? */
    int cTotal[BSPX_LUMPS];

    /* Misc other global state for the compile process */
    bool leakfile;      /* Flag once we've written a leak (.por/.pts) file */
    
    // helpers
    std::string texinfoTextureName(int texinfo) const {
        int mt = mtexinfos.at(texinfo).miptex;
        return miptex.at(mt);
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

int FindMiptex(const char *name);
int FindTexinfo(mtexinfo_t *texinfo, uint64_t flags); //FIXME: Make this take const texinfo
int FindTexinfoEnt(mtexinfo_t *texinfo, mapentity_t *entity); //FIXME: Make this take const texinfo

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

struct bspxbrushes_s
{
        uint8_t *lumpinfo;
        size_t lumpsize;
        size_t lumpmaxsize;
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
