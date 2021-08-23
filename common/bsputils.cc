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

const dmodel_t *BSP_GetWorldModel(const mbsp_t *bsp)
{
    // We only support .bsp's that have a world model
    if (bsp->nummodels < 1) {
        Error("BSP has no models");
    }
    return &bsp->dmodels[0];
}

int Face_GetNum(const mbsp_t *bsp, const bsp2_dface_t *f)
{
    Q_assert(f != nullptr);
    
    const ptrdiff_t diff = f - bsp->dfaces;
    Q_assert(diff >= 0 && diff < bsp->numfaces);
    
    return static_cast<int>(diff);
}

const bsp2_dnode_t *BSP_GetNode(const mbsp_t *bsp, int nodenum)
{
    Q_assert(nodenum >= 0 && nodenum < bsp->numnodes);
    return &bsp->dnodes[nodenum];
}

const mleaf_t* BSP_GetLeaf(const mbsp_t *bsp, int leafnum)
{
    if (leafnum < 0 || leafnum >= bsp->numleafs) {
        Error("Corrupt BSP: leaf %d is out of bounds (bsp->numleafs = %d)", leafnum, bsp->numleafs);
    }
    return &bsp->dleafs[leafnum];
}

const mleaf_t* BSP_GetLeafFromNodeNum(const mbsp_t *bsp, int nodenum)
{
    const int leafnum = (-1 - nodenum);
    return BSP_GetLeaf(bsp, leafnum);
}

const dplane_t *BSP_GetPlane(const mbsp_t *bsp, int planenum)
{
    Q_assert(planenum >= 0 && planenum < bsp->numplanes);
    return &bsp->dplanes[planenum];
}

const bsp2_dface_t *BSP_GetFace(const mbsp_t *bsp, int fnum)
{
    Q_assert(fnum >= 0 && fnum < bsp->numfaces);
    return &bsp->dfaces[fnum];
}

const gtexinfo_t *BSP_GetTexinfo(const mbsp_t *bsp, int texinfo) {
    if (texinfo < 0) {
        return nullptr;
    }
    if (texinfo >= bsp->numtexinfo) {
        return nullptr;
    }
    const gtexinfo_t *tex = &bsp->texinfo[texinfo];
    return tex;
}

bsp2_dface_t *BSP_GetFace(mbsp_t *bsp, int fnum)
{
    Q_assert(fnum >= 0 && fnum < bsp->numfaces);
    return &bsp->dfaces[fnum];
}

/* small helper that just retrieves the correct vertex from face->surfedge->edge lookups */
int Face_VertexAtIndex(const mbsp_t *bsp, const bsp2_dface_t *f, int v)
{
    Q_assert(v >= 0);
    Q_assert(v < f->numedges);
    
    int edge = f->firstedge + v;
    edge = bsp->dsurfedges[edge];
    if (edge < 0)
        return bsp->dedges[-edge].v[1];
    return bsp->dedges[edge].v[0];
}

static void
Vertex_GetPos(const mbsp_t *bsp, int num, vec3_t out)
{
    Q_assert(num >= 0 && num < bsp->numvertexes);
    const dvertex_t *v = &bsp->dvertexes[num];
    
    for (int i=0; i<3; i++)
        out[i] = v->point[i];
}

void Face_PointAtIndex(const mbsp_t *bsp, const bsp2_dface_t *f, int v, vec3_t point_out)
{
    const int vertnum = Face_VertexAtIndex(bsp, f, v);
    Vertex_GetPos(bsp, vertnum, point_out);
}

void
Face_Normal(const mbsp_t *bsp, const bsp2_dface_t *f, vec3_t norm)
{
    plane_t pl = Face_Plane(bsp, f);
    VectorCopy(pl.normal, norm);
}

plane_t
Face_Plane(const mbsp_t *bsp, const bsp2_dface_t *f)
{
    Q_assert(f->planenum >= 0 && f->planenum < bsp->numplanes);
    const dplane_t *dplane = &bsp->dplanes[f->planenum];
    
    vec3_t planeNormal;
    VectorCopy(dplane->normal, planeNormal); // convert from float->double if needed

    plane_t result;
    if (f->side) {
        VectorSubtract(vec3_origin, planeNormal, result.normal);
        result.dist = -dplane->dist;
    } else {
        VectorCopy(planeNormal, result.normal);
        result.dist = dplane->dist;
    }
    return result;
}

const gtexinfo_t *Face_Texinfo(const mbsp_t *bsp, const bsp2_dface_t *face)
{
    if (face->texinfo < 0 || face->texinfo >= bsp->numtexinfo)
        return nullptr;

    return &bsp->texinfo[face->texinfo];
}

const miptex_t *
Face_Miptex(const mbsp_t *bsp, const bsp2_dface_t *face)
{
    if (!bsp->texdatasize)
        return nullptr;
    
    const gtexinfo_t *texinfo = Face_Texinfo(bsp, face);
    if (texinfo == nullptr)
        return nullptr;
    
    const int texnum = texinfo->miptex;
    const dmiptexlump_t *miplump = bsp->dtexdata;
    
    const int offset = miplump->dataofs[texnum];
    if (offset < 0)
        return nullptr; //sometimes the texture just wasn't written. including its name.
    
    const miptex_t *miptex = (const miptex_t*)((const uint8_t *)bsp->dtexdata + offset);
    return miptex;
}

const char *
Face_TextureName(const mbsp_t *bsp, const bsp2_dface_t *face)
{
    const auto *miptex = Face_Miptex(bsp, face);
    return (miptex ? miptex->name : "");
}

bool Face_IsLightmapped(const mbsp_t *bsp, const bsp2_dface_t *face)
{
    const gtexinfo_t *texinfo = Face_Texinfo(bsp, face);
    if (texinfo == nullptr)
        return false;
    
    if (bsp->loadversion == &bspver_q2 || bsp->loadversion == &bspver_qbism) {
        if (texinfo->flags & (Q2_SURF_WARP|Q2_SURF_SKY|Q2_SURF_NODRAW)) { //mxd. +Q2_SURF_NODRAW
            return false;
        }
    } else {
        if (texinfo->flags & TEX_SPECIAL)
            return false;
    }
    
    return true;
}

const float *GetSurfaceVertexPoint(const mbsp_t *bsp, const bsp2_dface_t *f, int v)
{
    return bsp->dvertexes[Face_VertexAtIndex(bsp, f, v)].point;
}

int
TextureName_Contents(const char *texname)
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

bool //mxd
Contents_IsTranslucent(const mbsp_t *bsp, const int contents)
{
    if (bsp->loadversion == &bspver_q2 || bsp->loadversion == &bspver_qbism)
        return (contents & Q2_SURF_TRANSLUCENT) && ((contents & Q2_SURF_TRANSLUCENT) != Q2_SURF_TRANSLUCENT); // Don't count KMQ2 fence flags combo as translucent
    else
        return contents == CONTENTS_WATER || contents == CONTENTS_LAVA || contents == CONTENTS_SLIME;
}

bool //mxd. Moved here from ltface.c (was Face_IsLiquid)
Face_IsTranslucent(const mbsp_t *bsp, const bsp2_dface_t *face)
{
    return Contents_IsTranslucent(bsp, Face_Contents(bsp, face));
}

int //mxd. Returns CONTENTS_ value for Q1, Q2_SURF_ bitflags for Q2...
Face_Contents(const mbsp_t *bsp, const bsp2_dface_t *face)
{
    if (bsp->loadversion == &bspver_q2 || bsp->loadversion == &bspver_qbism) {
        const gtexinfo_t *info = Face_Texinfo(bsp, face);
        return info->flags;
    } else {
        const char *texname = Face_TextureName(bsp, face);
        return TextureName_Contents(texname);
    }
}

const dmodel_t *BSP_DModelForModelString(const mbsp_t *bsp, const std::string &submodel_str)
{
    int submodel = -1;
    if (1 == sscanf(submodel_str.c_str(), "*%d", &submodel)) {
        
        if (submodel < 0 || submodel >= bsp->nummodels) {
            return nullptr;
        }
        
        return &bsp->dmodels[submodel];
        
    }
    return nullptr;
}

vec_t Plane_Dist(const vec3_t point, const dplane_t *plane)
{
    switch (plane->type)
    {
        case PLANE_X: return point[0] - plane->dist;
        case PLANE_Y: return point[1] - plane->dist;
        case PLANE_Z: return point[2] - plane->dist;
        default: {
            vec3_t planeNormal;
            VectorCopy(plane->normal, planeNormal); // convert from float->double if needed
            return DotProduct(point, planeNormal) - plane->dist;
        }
    }
}

static bool Light_PointInSolid_r(const mbsp_t *bsp, const int nodenum, const vec3_t point)
{
    if (nodenum < 0) {
        const mleaf_t *leaf = BSP_GetLeafFromNodeNum(bsp, nodenum);

        //mxd
        if (bsp->loadversion == &bspver_q2 || bsp->loadversion == &bspver_qbism) {
            return leaf->contents & Q2_CONTENTS_SOLID;
        }
        
        return (leaf->contents == CONTENTS_SOLID || leaf->contents == CONTENTS_SKY);
    }
    
    const bsp2_dnode_t *node = &bsp->dnodes[nodenum];
    const vec_t dist = Plane_Dist(point, &bsp->dplanes[node->planenum]);
    
    if (dist > 0.1)
        return Light_PointInSolid_r(bsp, node->children[0], point);
    if (dist < -0.1)
        return Light_PointInSolid_r(bsp, node->children[1], point);

    // too close to the plane, check both sides
    return Light_PointInSolid_r(bsp, node->children[0], point)
        || Light_PointInSolid_r(bsp, node->children[1], point);
}

// Tests hull 0 of the given model
bool Light_PointInSolid(const mbsp_t *bsp, const dmodel_t *model, const vec3_t point)
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

bool Light_PointInWorld(const mbsp_t *bsp, const vec3_t point)
{
    return Light_PointInSolid(bsp, &bsp->dmodels[0], point);
}

static const bsp2_dface_t *BSP_FindFaceAtPoint_r(const mbsp_t *bsp, const int nodenum, const vec3_t point, const vec3_t wantedNormal)
{
    if (nodenum < 0) {
        // we're only interested in nodes, since faces are owned by nodes.
        return nullptr;
    }
    
    const bsp2_dnode_t *node = &bsp->dnodes[nodenum];
    const vec_t dist = Plane_Dist(point, &bsp->dplanes[node->planenum]);
    
    if (dist > 0.1)
        return BSP_FindFaceAtPoint_r(bsp, node->children[0], point, wantedNormal);
    if (dist < -0.1)
        return BSP_FindFaceAtPoint_r(bsp, node->children[1], point, wantedNormal);

    // Point is close to this node plane. Check all faces on the plane.
    for (int i=0; i<node->numfaces; i++) {
        const bsp2_dface_t *face = BSP_GetFace(bsp, node->firstface + i);
        // First check if it's facing the right way
        vec3_t faceNormal;
        Face_Normal(bsp, face, faceNormal);

        if (DotProduct(faceNormal, wantedNormal) < 0) {
            // Opposite, so not the right face.
            continue;
        }

        // Next test if it's within the boundaries of the face
        plane_t *edgeplanes = Face_AllocInwardFacingEdgePlanes(bsp, face);
        const bool insideFace = EdgePlanes_PointInside(face, edgeplanes, point);
        free(edgeplanes);

        // Found a match?
        if (insideFace) {
            return face;
        }
    }

    // No match found on this plane. Check both sides of the tree.
    const bsp2_dface_t *side0Match = BSP_FindFaceAtPoint_r(bsp, node->children[0], point, wantedNormal);
    if (side0Match != nullptr) {
        return side0Match;
    } else {
        return BSP_FindFaceAtPoint_r(bsp, node->children[1], point, wantedNormal);
    }
}

const bsp2_dface_t * BSP_FindFaceAtPoint(const mbsp_t *bsp, const dmodel_t *model, const vec3_t point, const vec3_t wantedNormal)
{
    return BSP_FindFaceAtPoint_r(bsp, model->headnode[0], point, wantedNormal);
}

const bsp2_dface_t * BSP_FindFaceAtPoint_InWorld(const mbsp_t *bsp, const vec3_t point, const vec3_t wantedNormal)
{
    return BSP_FindFaceAtPoint(bsp, &bsp->dmodels[0], point, wantedNormal);
}

plane_t *
Face_AllocInwardFacingEdgePlanes(const mbsp_t *bsp, const bsp2_dface_t *face)
{
    plane_t *out = (plane_t *)calloc(face->numedges, sizeof(plane_t));
    
    const plane_t faceplane = Face_Plane(bsp, face);
    for (int i=0; i<face->numedges; i++)
    {
        plane_t *dest = &out[i];
        
        const float *v0 = GetSurfaceVertexPoint(bsp, face, i);
        const float *v1 = GetSurfaceVertexPoint(bsp, face, (i+1)%face->numedges);
        
        vec3_t v0_vec3t;
        vec3_t v1_vec3t;

        VectorCopy(v0, v0_vec3t);
        VectorCopy(v1, v1_vec3t); // convert float->double

        vec3_t edgevec;
        VectorSubtract(v1_vec3t, v0_vec3t, edgevec);
        VectorNormalize(edgevec);
        
        CrossProduct(edgevec, faceplane.normal, dest->normal);
        dest->dist = DotProduct(dest->normal, v0_vec3t);
    }
    
    return out;
}

bool
EdgePlanes_PointInside(const bsp2_dface_t *face, const plane_t *edgeplanes, const vec3_t point)
{
    for (int i=0; i<face->numedges; i++) {
        const vec_t planedist = DotProduct(point, edgeplanes[i].normal) - edgeplanes[i].dist;
        if (planedist < 0) {
            return false;
        }
    }
    return true;
}

// glm stuff

qplane3f Face_Plane_E(const mbsp_t *bsp, const bsp2_dface_t *f)
{
    const plane_t pl = Face_Plane(bsp, f);
    return qplane3f(qvec3f(pl.normal[0], pl.normal[1], pl.normal[2]), pl.dist);
}

qvec3f Face_PointAtIndex_E(const mbsp_t *bsp, const bsp2_dface_t *f, int v)
{
    return Vertex_GetPos_E(bsp, Face_VertexAtIndex(bsp, f, v));
}

qvec3f Vertex_GetPos_E(const mbsp_t *bsp, int num)
{
    vec3_t temp;
    Vertex_GetPos(bsp, num, temp);
    return vec3_t_to_glm(temp);
}

qvec3f Face_Normal_E(const mbsp_t *bsp, const bsp2_dface_t *f)
{
    vec3_t temp;
    Face_Normal(bsp, f, temp);
    return vec3_t_to_glm(temp);
}

std::vector<qvec3f> GLM_FacePoints(const mbsp_t *bsp, const bsp2_dface_t *face)
{
    std::vector<qvec3f> points;
    points.reserve(face->numedges); //mxd. https://clang.llvm.org/extra/clang-tidy/checks/performance-inefficient-vector-operation.html
    for (int j = 0; j < face->numedges; j++) {
        points.push_back(Face_PointAtIndex_E(bsp, face, j));
    }
    return points;
}

qvec3f Face_Centroid(const mbsp_t *bsp, const bsp2_dface_t *face)
{
    // FIXME: GLM_PolyCentroid has a assertion that there are >= 3 points
    return GLM_PolyCentroid(GLM_FacePoints(bsp, face));
}

void Face_DebugPrint(const mbsp_t *bsp, const bsp2_dface_t *face)
{
    const gtexinfo_t *tex = &bsp->texinfo[face->texinfo];
    const char *texname = Face_TextureName(bsp, face);

    logprint("face %d, texture '%s', %d edges...\n"
             "  vectors (%3.3f, %3.3f, %3.3f) (%3.3f)\n"
             "          (%3.3f, %3.3f, %3.3f) (%3.3f)\n",
             (int)(face - bsp->dfaces), texname, face->numedges,
             tex->vecs[0][0], tex->vecs[0][1], tex->vecs[0][2], tex->vecs[0][3],
             tex->vecs[1][0], tex->vecs[1][1], tex->vecs[1][2], tex->vecs[1][3]);

    for (int i = 0; i < face->numedges; i++) {
        int edge = bsp->dsurfedges[face->firstedge + i];
        int vert = Face_VertexAtIndex(bsp, face, i);
        const float *point = GetSurfaceVertexPoint(bsp, face, i);
        logprint("%s %3d (%3.3f, %3.3f, %3.3f) :: edge %d\n",
                 i ? "          " : "    verts ", vert,
                 point[0], point[1], point[2],
                 edge);
    }
}
