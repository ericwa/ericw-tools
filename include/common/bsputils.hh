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

#include <common/bspfile.hh>
#include <common/bspxfile.hh>
#include <common/mathlib.hh>
#include <common/qvec.hh>
#include <common/aabb.hh>
#include <common/polylib.hh>

#include <iterator>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>

const dmodelh2_t *BSP_GetWorldModel(const mbsp_t *bsp);
int Face_GetNum(const mbsp_t *bsp, const mface_t *f);

// bounds-checked array access (assertion failure on out-of-bounds)
const bsp2_dnode_t *BSP_GetNode(const mbsp_t *bsp, int nodenum);
const mleaf_t *BSP_GetLeaf(const mbsp_t *bsp, int leafnum);
const mleaf_t *BSP_GetLeafFromNodeNum(const mbsp_t *bsp, int nodenum);
const dplane_t *BSP_GetPlane(const mbsp_t *bsp, int planenum);
const mface_t *BSP_GetFace(const mbsp_t *bsp, int fnum);
const mtexinfo_t *BSP_GetTexinfo(const mbsp_t *bsp, int texinfo);
mface_t *BSP_GetFace(mbsp_t *bsp, int fnum);

int Face_VertexAtIndex(const mbsp_t *bsp, const mface_t *f, int v);
const qvec3f &Face_PointAtIndex(const mbsp_t *bsp, const mface_t *f, int v);
qplane3d Face_Plane(const mbsp_t *bsp, const mface_t *f);
const mtexinfo_t *Face_Texinfo(const mbsp_t *bsp, const mface_t *face);
const miptex_t *Face_Miptex(const mbsp_t *bsp, const mface_t *face);
const char *Face_TextureName(const mbsp_t *bsp, const mface_t *face);
const qvec3f &GetSurfaceVertexPoint(const mbsp_t *bsp, const mface_t *f, int v);
bool ContentsOrSurfaceFlags_IsTranslucent(const mbsp_t *bsp, int contents_or_surf_flags); // mxd
bool Face_IsTranslucent(const mbsp_t *bsp, const mface_t *face); // mxd
int Face_ContentsOrSurfaceFlags(
    const mbsp_t *bsp, const mface_t *face); // mxd. Returns CONTENTS_ value for Q1, Q2_SURF_ bitflags for Q2...
const dmodelh2_t *BSP_DModelForModelString(const mbsp_t *bsp, const std::string &submodel_str);
bool Light_PointInSolid(const mbsp_t *bsp, const dmodelh2_t *model, const qvec3d &point);
bool Light_PointInWorld(const mbsp_t *bsp, const qvec3d &point);

std::vector<const mface_t *> BSP_FindFacesAtPoint(
    const mbsp_t *bsp, const dmodelh2_t *model, const qvec3d &point, const qvec3d &wantedNormal = qvec3d(0, 0, 0));
/**
 * Searches for a face touching a point and facing a certain way.
 * Sometimes (water, sky?) there will be 2 overlapping candidates facing opposite ways, the provided normal
 * is used to disambiguate these.
 */
const mface_t *BSP_FindFaceAtPoint(
    const mbsp_t *bsp, const dmodelh2_t *model, const qvec3d &point, const qvec3d &wantedNormal = qvec3d(0, 0, 0));
/**
 * Searches for a decision node in hull0 that contains `point`, and has a plane normal of either
 * wanted_normal or -wanted_normal.
 */
const bsp2_dnode_t *BSP_FindNodeAtPoint(
    const mbsp_t *bsp, const dmodelh2_t *model, const qvec3d &point, const qvec3d &wanted_normal);

const mleaf_t *BSP_FindLeafAtPoint(const mbsp_t *bsp, const dmodelh2_t *model, const qvec3d &point);

/**
 * Leaf nodes in the clipnode tree don't have an identity like hull0 leaf nodes,
 * so this struct helps tests determine if two clipnodes are the same.
 */
struct clipnode_info_t
{
    /**
     * Index into bsp->dclipnodes
     */
    int parent_clipnode;
    /**
     * Which side of `parent_clipnode` are we on
     */
    planeside_t side;
    int contents;

    bool operator==(const clipnode_info_t &other) const;
};
clipnode_info_t BSP_FindClipnodeAtPoint(
    const mbsp_t *bsp, hull_index_t hullnum, const dmodelh2_t *model, const qvec3d &point);
int BSP_FindContentsAtPoint(const mbsp_t *bsp, hull_index_t hullnum, const dmodelh2_t *model, const qvec3d &point);

std::vector<const mface_t *> Leaf_Markfaces(const mbsp_t *bsp, const mleaf_t *leaf);
std::vector<const dbrush_t *> Leaf_Brushes(const mbsp_t *bsp, const mleaf_t *leaf);
const qvec3f &Vertex_GetPos(const mbsp_t *bsp, int num);
qvec3d Face_Normal(const mbsp_t *bsp, const mface_t *f);
std::vector<qvec3f> Face_Points(const mbsp_t *bsp, const mface_t *face);
polylib::winding_t Face_Winding(const mbsp_t *bsp, const mface_t *face);
qvec3f Face_Centroid(const mbsp_t *bsp, const mface_t *face);
void Face_DebugPrint(const mbsp_t *bsp, const mface_t *face);
aabb3f Model_BoundsOfFaces(const mbsp_t &bsp, const dmodelh2_t &model);

void CompressRow(const uint8_t *vis, const size_t numbytes, std::back_insert_iterator<std::vector<uint8_t>> it);
size_t DecompressedVisSize(const mbsp_t *bsp);
int VisleafToLeafnum(int visleaf);
int LeafnumToVisleaf(int leafnum);
bool Pvs_LeafVisible(const mbsp_t *bsp, const std::vector<uint8_t> &pvs, const mleaf_t *leaf);
void DecompressVis(const uint8_t *in, const uint8_t *inend, uint8_t *out, uint8_t *outend);
std::unordered_map<int, std::vector<uint8_t>> DecompressAllVis(const mbsp_t *bsp, bool trans_water = false);

void BSP_VisitAllLeafs(const mbsp_t &bsp, const dmodelh2_t &model, const std::function<void(const mleaf_t &)> &visitor);

bspx_decoupled_lm_perface BSPX_DecoupledLM(const bspxentries_t &entries, int face_num);
std::optional<bspxfacenormals> BSPX_FaceNormals(const mbsp_t &bsp, const bspxentries_t &entries);

/* ======================================================================== */

qvec2d WorldToTexCoord(const qvec3d &world, const mtexinfo_t *tex);
qvec2f Face_WorldToTexCoord(const mbsp_t *bsp, const mface_t *face, const qvec3f &world);
qmat4x4f WorldToTexSpace(const mbsp_t *bsp, const mface_t *f);
qmat4x4f TexSpaceToWorld(const mbsp_t *bsp, const mface_t *f);

/* for vanilla this would be 18. some engines allow higher limits though, which will be needed if we're scaling lightmap
 * resolution. */
/*with extra sampling, lit+lux etc, we need at least 46mb space per thread. yes, that's a lot. on the plus side,
 * it doesn't affect bsp complexity (actually, can simplify it a little)*/
constexpr size_t MAXDIMENSION = 255 + 1;

struct world_units_per_luxel_t
{
};

constexpr float LMSCALE_DEFAULT = 16.0f;

class faceextents_t
{
public:
    qvec2i lm_extents;
    qmat4x4f worldToTexCoordMatrix;
    qmat4x4f texCoordToWorldMatrix;
    qmat4x4f lmToWorldMatrix;
    qmat4x4f worldToLMMatrix;

    qvec3d origin;
    vec_t radius;
    aabb3d bounds;

    faceextents_t() = default;

    faceextents_t(const mface_t &face, const mbsp_t &bsp, float lmshift);
    faceextents_t(
        const mface_t &face, const mbsp_t &bsp, uint16_t lmwidth, uint16_t lmheight, texvecf world_to_lm_space);
    faceextents_t(const mface_t &face, const mbsp_t &bsp, world_units_per_luxel_t tag, float world_units_per_luxel);

    int width() const;
    int height() const;
    int numsamples() const;
    qvec2i lmsize() const;
    qvec2f worldToTexCoord(qvec3f world) const;
    qvec3f texCoordToWorld(qvec2f tc) const;
    qvec2f worldToLMCoord(qvec3f world) const;
    qvec3f LMCoordToWorld(qvec2f lm) const;
};

qvec3b LM_Sample(const mbsp_t *bsp, const std::vector<uint8_t> *lit, const faceextents_t &faceextents,
    int byte_offset_of_face, qvec2i coord);
std::vector<uint8_t> LoadLitFile(const fs::path &path);

std::map<int, std::vector<int>> ClusterToLeafnumsMap(const mbsp_t *bsp);
