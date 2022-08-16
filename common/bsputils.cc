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
#include <cstddef>

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
        return (contents_or_surf_flags & Q2_SURF_TRANSLUCENT) &&
               ((contents_or_surf_flags & Q2_SURF_TRANSLUCENT) !=
                   Q2_SURF_TRANSLUCENT); // Don't count KMQ2 fence flags combo as translucent
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
    // fast bounds check
    for (int i = 0; i < 3; ++i) {
        if (point[i] < model->mins[i])
            return false;
        if (point[i] > model->maxs[i])
            return false;
    }

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

static clipnode_info_t BSP_FindClipnodeAtPoint_r(const mbsp_t *bsp, const int parent_clipnodenum, const planeside_t parent_side, const int clipnodenum, const qvec3d &point)
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
        return BSP_FindClipnodeAtPoint_r(bsp, clipnodenum, SIDE_FRONT, node->children[SIDE_FRONT],  point);
    } else {
        return BSP_FindClipnodeAtPoint_r(bsp, clipnodenum, SIDE_BACK, node->children[SIDE_BACK], point);
    }
}

clipnode_info_t BSP_FindClipnodeAtPoint(const mbsp_t *bsp, hull_index_t hullnum, const dmodelh2_t *model, const qvec3d &point)
{
    Q_assert(hullnum.value() > 0);
    return BSP_FindClipnodeAtPoint_r(bsp, 0, static_cast<planeside_t>(-1), model->headnode.at(hullnum.value()), point);
}

int BSP_FindContentsAtPoint(const mbsp_t *bsp, hull_index_t hullnum, const dmodelh2_t *model, const qvec3d &point)
{
    if (!hullnum.value_or(0)) {
        return BSP_FindLeafAtPoint_r(bsp, model->headnode[0], point)->contents;
    }
    auto info = BSP_FindClipnodeAtPoint_r(bsp, 0, static_cast<planeside_t>(-1), model->headnode.at(hullnum.value()), point);
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

// glm stuff
std::vector<qvec3f> GLM_FacePoints(const mbsp_t *bsp, const mface_t *face)
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
    auto points = GLM_FacePoints(bsp, face);
    return qv::PolyCentroid(points.begin(), points.end());
}

void Face_DebugPrint(const mbsp_t *bsp, const mface_t *face)
{
    const mtexinfo_t *tex = &bsp->texinfo[face->texinfo];
    const char *texname = Face_TextureName(bsp, face);

    logging::print("face {}, texture '{}', {} edges; vectors:\n"
                   "{: 3.3}\n",
        Face_GetNum(bsp, face), texname, face->numedges, tex->vecs);

    for (int i = 0; i < face->numedges; i++) {
        int edge = bsp->dsurfedges[face->firstedge + i];
        int vert = Face_VertexAtIndex(bsp, face, i);
        const qvec3f &point = GetSurfaceVertexPoint(bsp, face, i);
        logging::print("{} {:3} ({:3.3}, {:3.3}, {:3.3}) :: edge {}\n", i ? "          " : "    verts ", vert, point[0],
            point[1], point[2], edge);
    }
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

/*
===================
DecompressRow
===================
*/
void DecompressRow(const uint8_t *in, const int numbytes, uint8_t *decompressed)
{
    int c;
    uint8_t *out;
    int row;

    row = numbytes;
    out = decompressed;

    do {
        if (*in) {
            *out++ = *in++;
            continue;
        }

        c = in[1];
        if (!c)
            FError("0 repeat");
        in += 2;
        while (c) {
            *out++ = 0;
            c--;
        }
    } while (out - decompressed < row);
}
