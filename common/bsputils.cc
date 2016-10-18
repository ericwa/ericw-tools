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

int Face_GetNum(const bsp2_t *bsp, const bsp2_dface_t *f)
{
    return f - bsp->dfaces;
}

/* small helper that just retrieves the correct vertex from face->surfedge->edge lookups */
int Face_VertexAtIndex(const bsp2_t *bsp, const bsp2_dface_t *f, int v)
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
Vertex_GetPos(const bsp2_t *bsp, int num, vec3_t out)
{
    Q_assert(num >= 0 && num < bsp->numvertexes);
    const dvertex_t *v = &bsp->dvertexes[num];
    
    for (int i=0; i<3; i++)
        out[i] = v->point[i];
}

void
Face_Normal(const bsp2_t *bsp, const bsp2_dface_t *f, vec3_t norm)
{
    if (f->side)
        VectorSubtract(vec3_origin, bsp->dplanes[f->planenum].normal, norm);
    else
        VectorCopy(bsp->dplanes[f->planenum].normal, norm);
}

plane_t
Face_Plane(const bsp2_t *bsp, const bsp2_dface_t *f)
{
    const int vertnum = Face_VertexAtIndex(bsp, f, 0);
    vec3_t vertpos;
    Vertex_GetPos(bsp, vertnum, vertpos);
    
    plane_t res;
    Face_Normal(bsp, f, res.normal);
    res.dist = DotProduct(vertpos, res.normal);
    return res;
}

const miptex_t *
Face_Miptex(const bsp2_t *bsp, const bsp2_dface_t *face)
{
    if (!bsp->texdatasize)
        return NULL;
    
    if (face->texinfo < 0)
        return NULL;
    
    int texnum = bsp->texinfo[face->texinfo].miptex;
    const dmiptexlump_t *miplump = bsp->dtexdata.header;
    
    int offset = miplump->dataofs[texnum];
    if (offset < 0)
        return NULL; //sometimes the texture just wasn't written. including its name.
    
    const miptex_t *miptex = (miptex_t*)(bsp->dtexdata.base + offset);
    return miptex;
}

const char *
Face_TextureName(const bsp2_t *bsp, const bsp2_dface_t *face)
{
    const miptex_t *miptex = Face_Miptex(bsp, face);
    if (miptex)
        return miptex->name;
    else
        return "";
}

const float *GetSurfaceVertexPoint(const bsp2_t *bsp, const bsp2_dface_t *f, int v)
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
Face_Contents(const bsp2_t *bsp, const bsp2_dface_t *face)
{
    const char *texname = Face_TextureName(bsp, face);
    return TextureName_Contents(texname);
}
