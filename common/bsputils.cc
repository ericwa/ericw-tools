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
#include <assert.h>
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

const bsp2_dface_t *BSP_GetFace(const mbsp_t *bsp, int fnum)
{
    Q_assert(fnum >= 0 && fnum < bsp->numfaces);
    return &bsp->dfaces[fnum];
}

bsp2_dface_t *BSP_GetFace(bsp2_t *bsp, int fnum)
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
    
    plane_t result;
    if (f->side) {
        VectorSubtract(vec3_origin, dplane->normal, result.normal);
        result.dist = -dplane->dist;
    } else {
        VectorCopy(dplane->normal, result.normal);
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
    
    int offset = miplump->dataofs[texnum];
    if (offset < 0)
        return NULL; //sometimes the texture just wasn't written. including its name.
    
    const miptex_t *miptex = (const miptex_t*)((const byte *)bsp->dtexdata + offset);
    return miptex;
}

const char *
Face_TextureName(const mbsp_t *bsp, const bsp2_dface_t *face)
{
    const miptex_t *miptex = Face_Miptex(bsp, face);
    if (miptex)
        return miptex->name;
    else
        return "";
}

bool Face_IsLightmapped(const mbsp_t *bsp, const bsp2_dface_t *face)
{
    const gtexinfo_t *texinfo = Face_Texinfo(bsp, face);
    if (texinfo == nullptr)
        return false;
    
    if (bsp->loadversion == Q2_BSPVERSION) {
        if (texinfo->flags & (Q2_SURF_WARP|Q2_SURF_SKY)) {
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

int
Face_Contents(const mbsp_t *bsp, const bsp2_dface_t *face)
{
    const char *texname = Face_TextureName(bsp, face);
    return TextureName_Contents(texname);
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
        default: return DotProduct(point, plane->normal) - plane->dist;
    }
}

static bool Light_PointInSolid_r(const mbsp_t *bsp, int nodenum, const vec3_t point )
{
    if (nodenum < 0) {
		// FIXME: Factor out into bounds-checked getter
		int leafnum = (-1 - nodenum);
		if (leafnum < 0 || leafnum >= bsp->numleafs) {
			Error("Corrupt BSP: leaf %d is out of bounds (bsp->numleafs = %d)", leafnum, bsp->numleafs);
		}
        mleaf_t *leaf = &bsp->dleafs[leafnum];
        
        return leaf->contents == CONTENTS_SOLID
        || leaf->contents == CONTENTS_SKY;
    }
    
    const bsp2_dnode_t *node = &bsp->dnodes[nodenum];
    vec_t dist = Plane_Dist(point, &bsp->dplanes[node->planenum]);
    
    if (dist > 0.1)
        return Light_PointInSolid_r(bsp, node->children[0], point);
    else if (dist < -0.1)
        return Light_PointInSolid_r(bsp, node->children[1], point);
    else {
        // too close to the plane, check both sides
        return Light_PointInSolid_r(bsp, node->children[0], point)
        || Light_PointInSolid_r(bsp, node->children[1], point);
    }
}

// Tests model 0 of the given model
bool Light_PointInSolid(const mbsp_t *bsp, const dmodel_t *model, const vec3_t point)
{
    return Light_PointInSolid_r(bsp, model->headnode[0], point);
}

bool Light_PointInWorld(const mbsp_t *bsp, const vec3_t point)
{
    return Light_PointInSolid(bsp, &bsp->dmodels[0], point);
}

plane_t *
Face_AllocInwardFacingEdgePlanes(const mbsp_t *bsp, const bsp2_dface_t *face)
{
    plane_t *out = (plane_t *)calloc(face->numedges, sizeof(plane_t));
    
    const plane_t faceplane = Face_Plane(bsp, face);
    for (int i=0; i<face->numedges; i++)
    {
        plane_t *dest = &out[i];
        
        const vec_t *v0 = GetSurfaceVertexPoint(bsp, face, i);
        const vec_t *v1 = GetSurfaceVertexPoint(bsp, face, (i+1)%face->numedges);
        
        vec3_t edgevec;
        VectorSubtract(v1, v0, edgevec);
        VectorNormalize(edgevec);
        
        CrossProduct(edgevec, faceplane.normal, dest->normal);
        dest->dist = DotProduct(dest->normal, v0);
    }
    
    return out;
}

bool
EdgePlanes_PointInside(const bsp2_dface_t *face, const plane_t *edgeplanes, const vec3_t point)
{
    for (int i=0; i<face->numedges; i++) {
        vec_t planedist = DotProduct(point, edgeplanes[i].normal) - edgeplanes[i].dist;
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

std::vector<qvec3f> GLM_FacePoints(const mbsp_t *bsp, const bsp2_dface_t *f)
{
    std::vector<qvec3f> points;
    for (int j = 0; j < f->numedges; j++) {
        points.push_back(Face_PointAtIndex_E(bsp, f, j));
    }
    return points;
}

qvec3f Face_Centroid(const mbsp_t *bsp, const bsp2_dface_t *face)
{
    return GLM_PolyCentroid(GLM_FacePoints(bsp, face));
}
