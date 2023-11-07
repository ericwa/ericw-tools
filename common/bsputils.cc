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

#include <common/bsputils.hh>

#include <array>
#include <cstddef>
#include <fstream>
#include <stdexcept>
#include <common/log.hh>
#include <common/qvec.hh>

const dmodelh2_t *BSP_GetWorldModel(const mbsp_t *bsp)
{
    // We only support .bsp's that have a world model
    if (bsp->dmodels.size() < 1) {
        FError("BSP has no models");
    }
    return &bsp->dmodels[0];
}

int Face_GetNum(const mbsp_t *bsp, const mface_t *f)
{
    Q_assert(f != nullptr);

    const ptrdiff_t diff = f - bsp->dfaces.data();
    Q_assert(diff >= 0 && diff < bsp->dfaces.size());

    return static_cast<int>(diff);
}

const bsp2_dnode_t *BSP_GetNode(const mbsp_t *bsp, int nodenum)
{
    Q_assert(nodenum >= 0 && nodenum < bsp->dnodes.size());
    return &bsp->dnodes[nodenum];
}

const mleaf_t *BSP_GetLeaf(const mbsp_t *bsp, int leafnum)
{
    if (leafnum < 0 || leafnum >= bsp->dleafs.size()) {
        Error("Corrupt BSP: leaf {} is out of bounds (bsp->numleafs = {})", leafnum, bsp->dleafs.size());
    }
    return &bsp->dleafs[leafnum];
}

const mleaf_t *BSP_GetLeafFromNodeNum(const mbsp_t *bsp, int nodenum)
{
    const int leafnum = (-1 - nodenum);
    return BSP_GetLeaf(bsp, leafnum);
}

const dplane_t *BSP_GetPlane(const mbsp_t *bsp, int planenum)
{
    Q_assert(planenum >= 0 && planenum < bsp->dplanes.size());
    return &bsp->dplanes[planenum];
}

const mface_t *BSP_GetFace(const mbsp_t *bsp, int fnum)
{
    Q_assert(fnum >= 0 && fnum < bsp->dfaces.size());
    return &bsp->dfaces[fnum];
}

const mtexinfo_t *BSP_GetTexinfo(const mbsp_t *bsp, int texinfo)
{
    if (texinfo < 0) {
        return nullptr;
    }
    if (texinfo >= bsp->texinfo.size()) {
        return nullptr;
    }
    const mtexinfo_t *tex = &bsp->texinfo[texinfo];
    return tex;
}

mface_t *BSP_GetFace(mbsp_t *bsp, int fnum)
{
    Q_assert(fnum >= 0 && fnum < bsp->dfaces.size());
    return &bsp->dfaces[fnum];
}

/* small helper that just retrieves the correct vertex from face->surfedge->edge lookups */
int Face_VertexAtIndex(const mbsp_t *bsp, const mface_t *f, int v)
{
    Q_assert(v >= 0);
    Q_assert(v < f->numedges);

    int edge = f->firstedge + v;
    edge = bsp->dsurfedges[edge];
    if (edge < 0)
        return bsp->dedges[-edge][1];
    return bsp->dedges[edge][0];
}

const qvec3f &Vertex_GetPos(const mbsp_t *bsp, int num)
{
    Q_assert(num >= 0 && num < bsp->dvertexes.size());
    return bsp->dvertexes[num];
}

const qvec3f &Face_PointAtIndex(const mbsp_t *bsp, const mface_t *f, int v)
{
    const int vertnum = Face_VertexAtIndex(bsp, f, v);
    return Vertex_GetPos(bsp, vertnum);
}

qvec3d Face_Normal(const mbsp_t *bsp, const mface_t *f)
{
    return Face_Plane(bsp, f).normal;
}

qplane3d Face_Plane(const mbsp_t *bsp, const mface_t *f)
{
    Q_assert(f->planenum >= 0 && f->planenum < bsp->dplanes.size());
    qplane3d result = bsp->dplanes[f->planenum];

    if (f->side) {
        return -result;
    }

    return result;
}

const mtexinfo_t *Face_Texinfo(const mbsp_t *bsp, const mface_t *face)
{
    if (face->texinfo < 0 || face->texinfo >= bsp->texinfo.size())
        return nullptr;

    return &bsp->texinfo[face->texinfo];
}

const miptex_t *Face_Miptex(const mbsp_t *bsp, const mface_t *face)
{
    // no miptex data (Q2 maps)
    if (!bsp->dtex.textures.size())
        return nullptr;

    const mtexinfo_t *texinfo = Face_Texinfo(bsp, face);

    if (texinfo == nullptr)
        return nullptr;

    const miptex_t &miptex = bsp->dtex.textures[texinfo->miptex];

    // sometimes the texture just wasn't written. including its name.
    if (miptex.name.empty())
        return nullptr;

    return &miptex;
}

const char *Face_TextureName(const mbsp_t *bsp, const mface_t *face)
{
    const mtexinfo_t *texinfo = Face_Texinfo(bsp, face);

    if (!texinfo) {
        return "";
    }

    // Q2 has texture written directly here
    if (texinfo->texture[0]) {
        return texinfo->texture.data();
    }

    // Q1 has it on the miptex
    const auto *miptex = Face_Miptex(bsp, face);

    if (miptex) {
        return miptex->name.c_str();
    }

    return "";
}

const qvec3f &GetSurfaceVertexPoint(const mbsp_t *bsp, const mface_t *f, int v)
{
    return bsp->dvertexes[Face_VertexAtIndex(bsp, f, v)];
}

static int TextureName_Contents(const char *texname)
{
    if (!Q_strncasecmp(texname, "sky", 3))
        return CONTENTS_SKY;
    else if (!Q_strncasecmp(texname, "*lava", 5))
        return CONTENTS_LAVA;
    else if (!Q_strncasecmp(texname, "*slime", 6))
        return CONTENTS_SLIME;
    else if (texname[0] == '*')
        return CONTENTS_WATER;

    return CONTENTS_SOLID;
}

bool // mxd
ContentsOrSurfaceFlags_IsTranslucent(const mbsp_t *bsp, const int contents_or_surf_flags)
{
    if (bsp->loadversion->game->id == GAME_QUAKE_II)
        return (contents_or_surf_flags & (Q2_SURF_TRANS33 | Q2_SURF_TRANS66));
    else
        return contents_or_surf_flags == CONTENTS_WATER || contents_or_surf_flags == CONTENTS_LAVA ||
               contents_or_surf_flags == CONTENTS_SLIME;
}

bool // mxd. Moved here from ltface.c (was Face_IsLiquid)
Face_IsTranslucent(const mbsp_t *bsp, const mface_t *face)
{
    return ContentsOrSurfaceFlags_IsTranslucent(bsp, Face_ContentsOrSurfaceFlags(bsp, face));
}

int // mxd. Returns CONTENTS_ value for Q1, Q2_SURF_ bitflags for Q2...
Face_ContentsOrSurfaceFlags(const mbsp_t *bsp, const mface_t *face)
{
    if (bsp->loadversion->game->id == GAME_QUAKE_II) {
        const mtexinfo_t *info = Face_Texinfo(bsp, face);
        return info->flags.native;
    } else {
        return TextureName_Contents(Face_TextureName(bsp, face));
    }
}

const dmodelh2_t *BSP_DModelForModelString(const mbsp_t *bsp, const std::string &submodel_str)
{
    int submodel = -1;
    if (1 == sscanf(submodel_str.c_str(), "*%d", &submodel)) {

        if (submodel < 0 || submodel >= bsp->dmodels.size()) {
            return nullptr;
        }

        return &bsp->dmodels[submodel];
    }
    return nullptr;
}

static bool Light_PointInSolid_r(const mbsp_t *bsp, const int nodenum, const qvec3d &point)
{
    if (nodenum < 0) {
        const mleaf_t *leaf = BSP_GetLeafFromNodeNum(bsp, nodenum);

        // mxd
        if (bsp->loadversion->game->id == GAME_QUAKE_II) {
            return leaf->contents & Q2_CONTENTS_SOLID;
        }

        return (leaf->contents == CONTENTS_SOLID || leaf->contents == CONTENTS_SKY);
    }

    const bsp2_dnode_t *node = &bsp->dnodes[nodenum];
    const vec_t dist = bsp->dplanes[node->planenum].distance_to_fast(point);

    if (dist > 0.1)
        return Light_PointInSolid_r(bsp, node->children[0], point);
    if (dist < -0.1)
        return Light_PointInSolid_r(bsp, node->children[1], point);

    // too close to the plane, check both sides
    return Light_PointInSolid_r(bsp, node->children[0], point) || Light_PointInSolid_r(bsp, node->children[1], point);
}

// Tests hull 0 of the given model
bool Light_PointInSolid(const mbsp_t *bsp, const dmodelh2_t *model, const qvec3d &point)
{
    return Light_PointInSolid_r(bsp, model->headnode[0], point);
}

bool Light_PointInWorld(const mbsp_t *bsp, const qvec3d &point)
{
    return Light_PointInSolid(bsp, &bsp->dmodels[0], point);
}

static std::vector<qplane3d> Face_AllocInwardFacingEdgePlanes(const mbsp_t *bsp, const mface_t *face)
{
    std::vector<qplane3d> out;
    out.reserve(face->numedges);

    const qplane3d faceplane = Face_Plane(bsp, face);
    for (int i = 0; i < face->numedges; i++) {
        const qvec3f &v0 = GetSurfaceVertexPoint(bsp, face, i);
        const qvec3f &v1 = GetSurfaceVertexPoint(bsp, face, (i + 1) % face->numedges);

        qvec3d edgevec = qv::normalize(v1 - v0);
        qvec3d normal = qv::cross(edgevec, faceplane.normal);

        out.emplace_back(normal, qv::dot(normal, v0));
    }

    return out;
}

static bool EdgePlanes_PointInside(const std::vector<qplane3d> &edgeplanes, const qvec3d &point)
{
    for (auto &plane : edgeplanes) {
        if (plane.distance_to(point) < 0) {
            return false;
        }
    }
    return true;
}

/**
 * pass 0,0,0 for wantedNormal to disable the normal check
 */
static void BSP_FindFaceAtPoint_r(const mbsp_t *bsp, const int nodenum, const qvec3d &point, const qvec3d &wantedNormal,
    std::vector<const mface_t *> &result)
{
    if (nodenum < 0) {
        // we're only interested in nodes, since faces are owned by nodes.
        return;
    }

    const bsp2_dnode_t *node = &bsp->dnodes[nodenum];
    const vec_t dist = bsp->dplanes[node->planenum].distance_to_fast(point);

    if (dist > 0.1) {
        BSP_FindFaceAtPoint_r(bsp, node->children[0], point, wantedNormal, result);
        return;
    }
    if (dist < -0.1) {
        BSP_FindFaceAtPoint_r(bsp, node->children[1], point, wantedNormal, result);
        return;
    }

    // Point is close to this node plane. Check all faces on the plane.
    for (int i = 0; i < node->numfaces; i++) {
        const mface_t *face = BSP_GetFace(bsp, node->firstface + i);
        // First check if it's facing the right way
        qvec3d faceNormal = Face_Normal(bsp, face);

        if (wantedNormal != qvec3d{0, 0, 0}) {
            if (qv::dot(faceNormal, wantedNormal) < 0) {
                // Opposite, so not the right face.
                continue;
            }
        }

        // Next test if it's within the boundaries of the face
        auto edgeplanes = Face_AllocInwardFacingEdgePlanes(bsp, face);
        const bool insideFace = EdgePlanes_PointInside(edgeplanes, point);

        // Found a match?
        if (insideFace) {
            result.push_back(face);
        }
    }

    // No match found on this plane. Check both sides of the tree.
    BSP_FindFaceAtPoint_r(bsp, node->children[0], point, wantedNormal, result);
    BSP_FindFaceAtPoint_r(bsp, node->children[1], point, wantedNormal, result);
}

std::vector<const mface_t *> BSP_FindFacesAtPoint(
    const mbsp_t *bsp, const dmodelh2_t *model, const qvec3d &point, const qvec3d &wantedNormal)
{
    std::vector<const mface_t *> result;
    BSP_FindFaceAtPoint_r(bsp, model->headnode[0], point, wantedNormal, result);
    return result;
}

const mface_t *BSP_FindFaceAtPoint(
    const mbsp_t *bsp, const dmodelh2_t *model, const qvec3d &point, const qvec3d &wantedNormal)
{
    std::vector<const mface_t *> result;
    BSP_FindFaceAtPoint_r(bsp, model->headnode[0], point, wantedNormal, result);

    if (result.empty()) {
        return nullptr;
    }
    return result[0];
}

static const bsp2_dnode_t *BSP_FindNodeAtPoint_r(
    const mbsp_t *bsp, const int nodenum, const qvec3d &point, const qvec3d &wantedNormal)
{
    if (nodenum < 0) {
        // we're only interested in nodes
        return nullptr;
    }

    const bsp2_dnode_t *node = &bsp->dnodes[nodenum];
    const vec_t dist = bsp->dplanes[node->planenum].distance_to_fast(point);

    if (dist > 0.1)
        return BSP_FindNodeAtPoint_r(bsp, node->children[0], point, wantedNormal);
    if (dist < -0.1)
        return BSP_FindNodeAtPoint_r(bsp, node->children[1], point, wantedNormal);

    // Point is close to this node plane. Check normal
    if (qv::epsilonEqual(1.0, fabs(qv::dot(bsp->dplanes[node->planenum].normal, wantedNormal)), 0.01)) {
        return node;
    }

    // No match found on this plane. Check both sides of the tree.
    const bsp2_dnode_t *side0Match = BSP_FindNodeAtPoint_r(bsp, node->children[0], point, wantedNormal);
    if (side0Match != nullptr) {
        return side0Match;
    } else {
        return BSP_FindNodeAtPoint_r(bsp, node->children[1], point, wantedNormal);
    }
}

const bsp2_dnode_t *BSP_FindNodeAtPoint(
    const mbsp_t *bsp, const dmodelh2_t *model, const qvec3d &point, const qvec3d &wanted_normal)
{
    return BSP_FindNodeAtPoint_r(bsp, model->headnode[0], point, wanted_normal);
}

static const mleaf_t *BSP_FindLeafAtPoint_r(const mbsp_t *bsp, const int nodenum, const qvec3d &point)
{
    if (nodenum < 0) {
        return BSP_GetLeafFromNodeNum(bsp, nodenum);
    }

    const bsp2_dnode_t *node = &bsp->dnodes[nodenum];
    const vec_t dist = bsp->dplanes[node->planenum].distance_to_fast(point);

    if (dist >= 0) {
        return BSP_FindLeafAtPoint_r(bsp, node->children[0], point);
    } else {
        return BSP_FindLeafAtPoint_r(bsp, node->children[1], point);
    }
}

const mleaf_t *BSP_FindLeafAtPoint(const mbsp_t *bsp, const dmodelh2_t *model, const qvec3d &point)
{
    return BSP_FindLeafAtPoint_r(bsp, model->headnode[0], point);
}

static clipnode_info_t BSP_FindClipnodeAtPoint_r(const mbsp_t *bsp, const int parent_clipnodenum,
    const planeside_t parent_side, const int clipnodenum, const qvec3d &point)
{
    if (clipnodenum < 0) {
        // actually contents
        clipnode_info_t info;
        info.parent_clipnode = parent_clipnodenum;
        info.contents = clipnodenum;
        info.side = parent_side;
        return info;
    }

    const auto *node = &bsp->dclipnodes.at(clipnodenum);
    const vec_t dist = bsp->dplanes[node->planenum].distance_to_fast(point);

    if (dist >= 0) {
        return BSP_FindClipnodeAtPoint_r(bsp, clipnodenum, SIDE_FRONT, node->children[SIDE_FRONT], point);
    } else {
        return BSP_FindClipnodeAtPoint_r(bsp, clipnodenum, SIDE_BACK, node->children[SIDE_BACK], point);
    }
}

bool clipnode_info_t::operator==(const clipnode_info_t &other) const
{
    return this->parent_clipnode == other.parent_clipnode && this->side == other.side &&
           this->contents == other.contents;
}

clipnode_info_t BSP_FindClipnodeAtPoint(
    const mbsp_t *bsp, hull_index_t hullnum, const dmodelh2_t *model, const qvec3d &point)
{
    Q_assert(hullnum.value() > 0);
    return BSP_FindClipnodeAtPoint_r(bsp, 0, static_cast<planeside_t>(-1), model->headnode.at(hullnum.value()), point);
}

int BSP_FindContentsAtPoint(const mbsp_t *bsp, hull_index_t hullnum, const dmodelh2_t *model, const qvec3d &point)
{
    if (!hullnum.value_or(0)) {
        return BSP_FindLeafAtPoint_r(bsp, model->headnode[0], point)->contents;
    }
    auto info =
        BSP_FindClipnodeAtPoint_r(bsp, 0, static_cast<planeside_t>(-1), model->headnode.at(hullnum.value()), point);
    return info.contents;
}

std::vector<const mface_t *> Leaf_Markfaces(const mbsp_t *bsp, const mleaf_t *leaf)
{
    std::vector<const mface_t *> result;
    result.reserve(leaf->nummarksurfaces);

    for (uint32_t i = 0; i < leaf->nummarksurfaces; ++i) {
        uint32_t face_index = bsp->dleaffaces.at(leaf->firstmarksurface + i);
        result.push_back(BSP_GetFace(bsp, face_index));
    }

    return result;
}

std::vector<const dbrush_t *> Leaf_Brushes(const mbsp_t *bsp, const mleaf_t *leaf)
{
    std::vector<const dbrush_t *> result;
    result.reserve(leaf->numleafbrushes);

    for (uint32_t i = 0; i < leaf->numleafbrushes; ++i) {
        uint32_t brush_index = bsp->dleafbrushes.at(leaf->firstleafbrush + i);
        result.push_back(&bsp->dbrushes.at(brush_index));
    }

    return result;
}

std::vector<qvec3f> Face_Points(const mbsp_t *bsp, const mface_t *face)
{
    std::vector<qvec3f> points;

    points.reserve(face->numedges);

    for (int j = 0; j < face->numedges; j++) {
        points.push_back(Face_PointAtIndex(bsp, face, j));
    }

    return points;
}

polylib::winding_t Face_Winding(const mbsp_t *bsp, const mface_t *face)
{
    polylib::winding_t w{};

    for (int j = 0; j < face->numedges; j++) {
        w.push_back(Face_PointAtIndex(bsp, face, j));
    }

    return w;
}

qvec3f Face_Centroid(const mbsp_t *bsp, const mface_t *face)
{
    auto points = Face_Points(bsp, face);
    return qv::PolyCentroid(points.begin(), points.end());
}

void Face_DebugPrint(const mbsp_t *bsp, const mface_t *face)
{
    const mtexinfo_t *tex = &bsp->texinfo[face->texinfo];
    const char *texname = Face_TextureName(bsp, face);

    logging::print("face {}, texture '{}', {} edges; vectors:\n"
                   "{}\n",
        Face_GetNum(bsp, face), texname, face->numedges, tex->vecs);

    for (int i = 0; i < face->numedges; i++) {
        int edge = bsp->dsurfedges[face->firstedge + i];
        int vert = Face_VertexAtIndex(bsp, face, i);
        const qvec3f &point = GetSurfaceVertexPoint(bsp, face, i);
        logging::print("{} {:3} ({:3.3}, {:3.3}, {:3.3}) :: edge {}\n", i ? "          " : "    verts ", vert, point[0],
            point[1], point[2], edge);
    }
}

aabb3f Model_BoundsOfFaces(const mbsp_t &bsp, const dmodelh2_t &model)
{
    aabb3f result;
    for (int i = model.firstface; i < model.firstface + model.numfaces; ++i) {
        auto &face = bsp.dfaces[i];
        for (int j = 0; j < face.numedges; ++j) {
            result += Face_PointAtIndex(&bsp, &face, j);
        }
    }
    return result;
}

/*
===============
CompressRow
===============
*/
void CompressRow(const uint8_t *vis, const size_t numbytes, std::back_insert_iterator<std::vector<uint8_t>> it)
{
    for (size_t i = 0; i < numbytes; i++) {
        it++ = vis[i];

        if (vis[i]) {
            continue;
        }

        int32_t rep = 1;

        for (i++; i < numbytes; i++) {
            if (vis[i] || rep == 255) {
                break;
            }
            rep++;
        }

        it++ = rep;
        i--;
    }
}

size_t DecompressedVisSize(const mbsp_t *bsp)
{
    if (bsp->loadversion->game->id == GAME_QUAKE_II) {
        return (bsp->dvis.bit_offsets.size() + 7) / 8;
    }

    return (bsp->dmodels[0].visleafs + 7) / 8;
}

int VisleafToLeafnum(int visleaf)
{
    return visleaf + 1;
}

int LeafnumToVisleaf(int leafnum)
{
    return leafnum - 1;
}

// returns true if pvs can see leaf
bool Pvs_LeafVisible(const mbsp_t *bsp, const std::vector<uint8_t> &pvs, const mleaf_t *leaf)
{
    if (bsp->loadversion->game->id == GAME_QUAKE_II) {
        if (leaf->cluster < 0) {
            return false;
        }

        if (leaf->cluster >= bsp->dvis.bit_offsets.size() ||
            bsp->dvis.get_bit_offset(VIS_PVS, leaf->cluster) >= bsp->dvis.bits.size()) {
            logging::print("Pvs_LeafVisible: invalid visofs for cluster {}\n", leaf->cluster);
            return false;
        }

        return !!(pvs[leaf->cluster >> 3] & (1 << (leaf->cluster & 7)));
    } else {
        const int leafnum = (leaf - bsp->dleafs.data());
        const int visleaf = LeafnumToVisleaf(leafnum);

        if (leafnum == 0) {
            // can't see into the shared solid leaf
            return false;
        }
        if (visleaf < -1 || visleaf >= bsp->dmodels[0].visleafs) {
            logging::print("WARNING: bad/empty vis data on leaf?");
            return false;
        }

        return !!(pvs[visleaf >> 3] & (1 << (visleaf & 7)));
    }
}

// from DarkPlaces (Mod_Q1BSP_DecompressVis)
void DecompressVis(const uint8_t *in, const uint8_t *inend, uint8_t *out, uint8_t *outend)
{
    int c;
    uint8_t *outstart = out;
    while (out < outend) {
        if (in == inend) {
            logging::print("DecompressVis: input underrun (decompressed {} of {} output bytes)\n", (out - outstart),
                (outend - outstart));
            return;
        }

        c = *in++;
        if (c) {
            *out++ = c;
            continue;
        }

        if (in == inend) {
            logging::print("DecompressVis: input underrun (during zero-run) (decompressed {} of {} output bytes)\n",
                (out - outstart), (outend - outstart));
            return;
        }

        const int run_length = *in++;
        if (!run_length) {
            logging::print("DecompressVis: 0 repeat\n");
            return;
        }

        for (c = run_length; c > 0; c--) {
            if (out == outend) {
                logging::print("DecompressVis: output overrun (decompressed {} of {} output bytes)\n", (out - outstart),
                    (outend - outstart));
                return;
            }
            *out++ = 0;
        }
    }
}

/**
 * Decompress visdata for the entire map, and returns a map of:
 *
 *  - Q2: cluster number to decompressed visdata
 *  - Q1/others: visofs to decompressed visdata
 *
 * Q1 uses visofs as the map key, rather than e.g. visleaf number or leaf number, because if func_detail is in use,
 * many leafs will share the same visofs. This avoids storing the same visdata redundantly.
 */
std::unordered_map<int, std::vector<uint8_t>> DecompressAllVis(const mbsp_t *bsp, bool trans_water)
{
    std::unordered_map<int, std::vector<uint8_t>> result;

    const size_t decompressed_size = DecompressedVisSize(bsp);

    if (bsp->loadversion->game->id == GAME_QUAKE_II) {
        const int num_clusters = bsp->dvis.bit_offsets.size();

        for (int cluster = 0; cluster < num_clusters; ++cluster) {
            if (bsp->dvis.get_bit_offset(VIS_PVS, cluster) >= bsp->dvis.bits.size()) {
                logging::print("DecompressAllVis: invalid visofs for cluster {}\n", cluster);
                continue;
            }

            std::vector<uint8_t> decompressed(decompressed_size);
            DecompressVis(bsp->dvis.bits.data() + bsp->dvis.get_bit_offset(VIS_PVS, cluster),
                bsp->dvis.bits.data() + bsp->dvis.bits.size(), decompressed.data(),
                decompressed.data() + decompressed.size());
            result[cluster] = std::move(decompressed);
        }
    } else {
        for (int leafnum = 0; leafnum < bsp->dleafs.size(); ++leafnum) {
            auto &leaf = bsp->dleafs[leafnum];
            if (leaf.visofs < 0) {
                continue;
            }

            const int map_key = leaf.visofs;

            if (result.find(map_key) != result.end()) {
                // already decompressed this cluster
                continue;
            }

            if (leaf.visofs >= bsp->dvis.bits.size()) {
                logging::print("DecompressAllVis: invalid visofs for leaf {}\n", leafnum);
                continue;
            }

            std::vector<uint8_t> decompressed(decompressed_size);
            DecompressVis(bsp->dvis.bits.data() + leaf.visofs, bsp->dvis.bits.data() + bsp->dvis.bits.size(),
                decompressed.data(), decompressed.data() + decompressed.size());
            result[map_key] = std::move(decompressed);
        }
    }

    return result;
}

static void BSP_VisitAllLeafs_R(
    const mbsp_t &bsp, const int nodenum, const std::function<void(const mleaf_t &)> &visitor)
{
    if (nodenum < 0) {
        auto *leaf = BSP_GetLeafFromNodeNum(&bsp, nodenum);
        visitor(*leaf);
        return;
    }

    const bsp2_dnode_t &node = bsp.dnodes.at(nodenum);
    BSP_VisitAllLeafs_R(bsp, node.children[0], visitor);
    BSP_VisitAllLeafs_R(bsp, node.children[1], visitor);
}

void BSP_VisitAllLeafs(const mbsp_t &bsp, const dmodelh2_t &model, const std::function<void(const mleaf_t &)> &visitor)
{
    BSP_VisitAllLeafs_R(bsp, model.headnode[0], visitor);
}

bspx_decoupled_lm_perface BSPX_DecoupledLM(const bspxentries_t &entries, int face_num)
{
    auto &lump_bytes = entries.at("DECOUPLED_LM");

    auto stream = imemstream(lump_bytes.data(), lump_bytes.size());

    stream.seekg(face_num * sizeof(bspx_decoupled_lm_perface));
    stream >> endianness<std::endian::little>;

    bspx_decoupled_lm_perface result;
    stream >= result;
    return result;
}

std::optional<bspxfacenormals> BSPX_FaceNormals(const mbsp_t &bsp, const bspxentries_t &entries)
{
    auto it = entries.find("FACENORMALS");
    if (it == entries.end()) {
        return std::nullopt;
    }

    auto stream = imemstream(it->second.data(), it->second.size());
    stream >> endianness<std::endian::little>;

    bspxfacenormals result;
    result.stream_read(stream, bsp);
    return result;
}

qvec2d WorldToTexCoord(const qvec3d &world, const mtexinfo_t *tex)
{
    /*
     * The (long double) casts below are important: The original code
     * was written for x87 floating-point which uses 80-bit floats for
     * intermediate calculations. But if you compile it without the
     * casts for modern x86_64, the compiler will round each
     * intermediate result to a 32-bit float, which introduces extra
     * rounding error.
     *
     * This becomes a problem if the rounding error causes the light
     * utilities and the engine to disagree about the lightmap size
     * for some surfaces.
     *
     * Casting to (long double) keeps the intermediate values at at
     * least 64 bits of precision, probably 128.
     */
    return tex->vecs.uvs<long double>(world);
}

qvec2f Face_WorldToTexCoord(const mbsp_t *bsp, const mface_t *face, const qvec3f &world)
{
    const mtexinfo_t *tex = Face_Texinfo(bsp, face);

    if (tex == nullptr)
        return {};

    return WorldToTexCoord(world, tex);
}

qmat4x4f WorldToTexSpace(const mbsp_t *bsp, const mface_t *f)
{
    const mtexinfo_t *tex = Face_Texinfo(bsp, f);
    if (tex == nullptr) {
        Q_assert_unreachable();
        return qmat4x4f();
    }
    const qplane3d plane = Face_Plane(bsp, f);

    //           [s]
    // T * vec = [t]
    //           [distOffPlane]
    //           [?]

    qmat4x4f T{
        tex->vecs.at(0, 0), tex->vecs.at(1, 0), static_cast<float>(plane.normal[0]), 0, // col 0
        tex->vecs.at(0, 1), tex->vecs.at(1, 1), static_cast<float>(plane.normal[1]), 0, // col 1
        tex->vecs.at(0, 2), tex->vecs.at(1, 2), static_cast<float>(plane.normal[2]), 0, // col 2
        tex->vecs.at(0, 3), tex->vecs.at(1, 3), static_cast<float>(-plane.dist), 1 // col 3
    };
    return T;
}

qmat4x4f TexSpaceToWorld(const mbsp_t *bsp, const mface_t *f)
{
    return qv::inverse(WorldToTexSpace(bsp, f));
}

// faceextents_t

faceextents_t::faceextents_t(const mface_t &face, const mbsp_t &bsp, float lightmapshift)
{
    worldToTexCoordMatrix = WorldToTexSpace(&bsp, &face);
    texCoordToWorldMatrix = TexSpaceToWorld(&bsp, &face);

    aabb2d tex_bounds;

    for (int i = 0; i < face.numedges; i++) {
        const qvec3f &worldpoint = Face_PointAtIndex(&bsp, &face, i);
        const qvec2f texcoord = Face_WorldToTexCoord(&bsp, &face, worldpoint);

#ifdef PARANOID
        // self test
        auto texcoordRT = this->worldToTexCoord(worldpoint);
        auto worldpointRT = this->texCoordToWorld(texcoord);
        Q_assert(qv::epsilonEqual(texcoordRT, texcoord, 0.1f));
        Q_assert(qv::epsilonEqual(worldpointRT, worldpoint, 0.1f));
        // end self test
#endif

        tex_bounds += texcoord;
        bounds += worldpoint;
    }

    qvec2i lm_mins;
    for (int i = 0; i < 2; i++) {
        tex_bounds[0][i] = floor(tex_bounds[0][i] / lightmapshift);
        tex_bounds[1][i] = ceil(tex_bounds[1][i] / lightmapshift);
        lm_mins[i] = static_cast<int>(tex_bounds[0][i]);
        lm_extents[i] = static_cast<int>(tex_bounds[1][i] - tex_bounds[0][i]);

        if (lm_extents[i] >= MAXDIMENSION * (16.0 / lightmapshift)) {
            const qplane3d plane = Face_Plane(&bsp, &face);
            const qvec3f &point = Face_PointAtIndex(&bsp, &face, 0); // grab first vert
            const char *texname = Face_TextureName(&bsp, &face);

            logging::print("WARNING: Bad surface extents (may not load in vanilla Q1 engines):\n"
                           "   surface {}, {} extents = {}, shift = {}\n"
                           "   texture {} at ({})\n"
                           "   surface normal ({})\n",
                Face_GetNum(&bsp, &face), i ? "t" : "s", lm_extents[i], lightmapshift, texname, point, plane.normal);
        }
    }

    // calculate a bounding sphere for the face
    qvec3d radius = (bounds.maxs() - bounds.mins()) * 0.5;

    origin = bounds.mins() + radius;
    this->radius = qv::length(radius);

    qmat4x4f LMToTexCoordMatrix = qmat4x4f::row_major({lightmapshift, 0, 0, lm_mins[0] * lightmapshift, 0,
        lightmapshift, 0, lm_mins[1] * lightmapshift, 0, 0, 1, 0, 0, 0, 0, 1});
    qmat4x4f TexCoordToLMMatrix = qv::inverse(LMToTexCoordMatrix);

    lmToWorldMatrix = texCoordToWorldMatrix * LMToTexCoordMatrix;
    worldToLMMatrix = TexCoordToLMMatrix * worldToTexCoordMatrix;
}

faceextents_t::faceextents_t(
    const mface_t &face, const mbsp_t &bsp, uint16_t lmwidth, uint16_t lmheight, texvecf world_to_lm_space)
{
    const qplane3f plane = Face_Plane(&bsp, &face);

    if (lmwidth > 0 && lmheight > 0) {
        lm_extents = {lmwidth - 1, lmheight - 1};
    }

    worldToTexCoordMatrix = WorldToTexSpace(&bsp, &face);
    texCoordToWorldMatrix = TexSpaceToWorld(&bsp, &face);

    worldToLMMatrix.set_row(0, world_to_lm_space.row(0));
    worldToLMMatrix.set_row(1, world_to_lm_space.row(1));
    worldToLMMatrix.set_row(2, qvec4f(plane.normal[0], plane.normal[1], plane.normal[2], -plane.dist));
    worldToLMMatrix.set_row(3, {0, 0, 0, 1});

    lmToWorldMatrix = qv::inverse(worldToLMMatrix);

    // bounds
    for (int i = 0; i < face.numedges; i++) {
        const qvec3f &worldpoint = Face_PointAtIndex(&bsp, &face, i);
        bounds += worldpoint;
    }

    // calculate a bounding sphere for the face
    qvec3d radius = (bounds.maxs() - bounds.mins()) * 0.5;

    origin = bounds.mins() + radius;
    this->radius = qv::length(radius);
}

faceextents_t::faceextents_t(
    const mface_t &face, const mbsp_t &bsp, world_units_per_luxel_t tag, float world_units_per_luxel)
{
    const qplane3f plane = Face_Plane(&bsp, &face);
    auto orig_normal = Face_Normal(&bsp, &face);
    size_t axis = qv::indexOfLargestMagnitudeComponent(orig_normal);

#if 0
    if (orig_normal == qvec3f(-1, 0, 0)) {
        logging::print("-x\n");
    }
#endif

    qvec3f snapped_normal{};
    if (orig_normal[axis] > 0) {
        snapped_normal[axis] = 1;
    } else {
        snapped_normal[axis] = -1;
    }

    auto [t, b] = qv::MakeTangentAndBitangentUnnormalized(snapped_normal);
    t = t * (1 / world_units_per_luxel);
    b = b * (1 / world_units_per_luxel);

    qmat<float, 2, 3> world_to_lm;
    world_to_lm.set_row(0, t);
    world_to_lm.set_row(1, b);

    aabb2f lm_bounds;
    for (int i = 0; i < face.numedges; i++) {
        const qvec3f &worldpoint = Face_PointAtIndex(&bsp, &face, i);
        const qvec2f lmcoord = world_to_lm * worldpoint;
        lm_bounds += lmcoord;
    }

    qvec2i lm_mins;
    for (int i = 0; i < 2; i++) {
        lm_bounds[0][i] = floor(lm_bounds[0][i]);
        lm_bounds[1][i] = ceil(lm_bounds[1][i]);
        lm_mins[i] = static_cast<int>(lm_bounds[0][i]);
        lm_extents[i] = static_cast<int>(lm_bounds[1][i] - lm_bounds[0][i]);
    }

    worldToLMMatrix.set_row(0, qvec4f(world_to_lm.row(0), -lm_mins[0]));
    worldToLMMatrix.set_row(1, qvec4f(world_to_lm.row(1), -lm_mins[1]));
    worldToLMMatrix.set_row(2, qvec4f(plane.normal[0], plane.normal[1], plane.normal[2], -plane.dist));
    worldToLMMatrix.set_row(3, qvec4f(0, 0, 0, 1));

    lmToWorldMatrix = qv::inverse(worldToLMMatrix);

    // world <-> tex conversions
    worldToTexCoordMatrix = WorldToTexSpace(&bsp, &face);
    texCoordToWorldMatrix = TexSpaceToWorld(&bsp, &face);

    // bounds
    for (int i = 0; i < face.numedges; i++) {
        const qvec3f &worldpoint = Face_PointAtIndex(&bsp, &face, i);
        bounds += worldpoint;

#if 0
        auto lm = worldToLMMatrix * qvec4f(worldpoint, 1.0f);

        logging::print("testing world {} -> lm {}\n",
            worldpoint,
            lm);
#endif
    }

    // calculate a bounding sphere for the face
    qvec3d radius = (bounds.maxs() - bounds.mins()) * 0.5;

    origin = bounds.mins() + radius;
    this->radius = qv::length(radius);
}

int faceextents_t::width() const
{
    return lm_extents[0] + 1;
}

int faceextents_t::height() const
{
    return lm_extents[1] + 1;
}

int faceextents_t::numsamples() const
{
    return width() * height();
}

qvec2i faceextents_t::lmsize() const
{
    return {width(), height()};
}

qvec2f faceextents_t::worldToTexCoord(qvec3f world) const
{
    const qvec4f worldPadded(world, 1.0f);
    const qvec4f res = worldToTexCoordMatrix * worldPadded;

    Q_assert(res[3] == 1.0f);

    return res;
}

qvec3f faceextents_t::texCoordToWorld(qvec2f tc) const
{
    const qvec4f tcPadded(tc[0], tc[1], 0.0f, 1.0f);
    const qvec4f res = texCoordToWorldMatrix * tcPadded;

    Q_assert(fabs(res[3] - 1.0f) < 0.01f);

    return res;
}

qvec2f faceextents_t::worldToLMCoord(qvec3f world) const
{
    const qvec4f worldPadded(world, 1.0f);
    const qvec4f res = worldToLMMatrix * worldPadded;
    return res;
}

qvec3f faceextents_t::LMCoordToWorld(qvec2f lm) const
{
    const qvec4f lmPadded(lm[0], lm[1], 0.0f, 1.0f);
    const qvec4f res = lmToWorldMatrix * lmPadded;
    return res;
}

/**
 * Samples the lightmap at an integer coordinate
 * FIXME: this doesn't deal with styles at all
 */
qvec3b LM_Sample(const mbsp_t *bsp, const std::vector<uint8_t> *lit, const faceextents_t &faceextents,
    int byte_offset_of_face, qvec2i coord)
{
    if (byte_offset_of_face == -1) {
        return {0, 0, 0};
    }

    Q_assert(coord[0] >= 0);
    Q_assert(coord[1] >= 0);
    Q_assert(coord[0] < faceextents.width());
    Q_assert(coord[1] < faceextents.height());

    int pixel = coord[0] + (coord[1] * faceextents.width());

    assert(byte_offset_of_face >= 0);

    const uint8_t *data = bsp->dlightdata.data();

    if (lit) {
        const uint8_t *lit_data = lit->data();

        return qvec3f{lit_data[(3 * byte_offset_of_face) + (pixel * 3) + 0],
            lit_data[(3 * byte_offset_of_face) + (pixel * 3) + 1],
            lit_data[(3 * byte_offset_of_face) + (pixel * 3) + 2]};
    } else if (bsp->loadversion->game->has_rgb_lightmap) {
        return qvec3f{data[byte_offset_of_face + (pixel * 3) + 0], data[byte_offset_of_face + (pixel * 3) + 1],
            data[byte_offset_of_face + (pixel * 3) + 2]};
    } else {
        return qvec3f{
            data[byte_offset_of_face + pixel], data[byte_offset_of_face + pixel], data[byte_offset_of_face + pixel]};
    }
}

std::vector<uint8_t> LoadLitFile(const fs::path &path)
{
    std::ifstream stream(path, std::ios_base::in | std::ios_base::binary);
    stream >> endianness<std::endian::little>;

    std::array<char, 4> ident;
    stream >= ident;
    if (ident != std::array<char, 4>{'Q', 'L', 'I', 'T'}) {
        throw std::runtime_error("invalid lit ident");
    }

    int version;
    stream >= version;
    if (version != 1) {
        throw std::runtime_error("invalid lit version");
    }

    std::vector<uint8_t> litdata;
    while (stream.good()) {
        uint8_t b;
        stream >= b;
        litdata.push_back(b);
    }

    return litdata;
}

static void AddLeafs(const mbsp_t *bsp, int nodenum, std::map<int, std::vector<int>> &cluster_to_leafnums)
{
    if (nodenum < 0) {
        const mleaf_t *leaf = BSP_GetLeafFromNodeNum(bsp, nodenum);

        // cluster -1 is invalid
        if (leaf->cluster != -1) {
            int leafnum = leaf - bsp->dleafs.data();
            cluster_to_leafnums[leaf->cluster].push_back(leafnum);
        }

        return;
    }

    auto *node = BSP_GetNode(bsp, nodenum);
    AddLeafs(bsp, node->children[0], cluster_to_leafnums);
    AddLeafs(bsp, node->children[1], cluster_to_leafnums);
}

std::map<int, std::vector<int>> ClusterToLeafnumsMap(const mbsp_t *bsp)
{
    std::map<int, std::vector<int>> result;
    AddLeafs(bsp, bsp->dmodels[0].headnode[0], result);
    return result;
}
