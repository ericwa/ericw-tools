/*
    Copyright (C) 2016       Eric Wasylishen

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

#include "qbsp.hh"
#include "wad.hh"

static FILE *
InitObjFile(void)
{
    FILE *objfile;
    
    StripExtension(options.szBSPName);
    strcat(options.szBSPName, ".obj");
    objfile = fopen(options.szBSPName, "wt");
    if (!objfile)
        Error("Failed to open %s: %s", options.szBSPName, strerror(errno));
    
    return objfile;
}

static void
GetUV(const texinfo_t *texinfo, const vec_t *pos, const int width, const int height, vec_t *u, vec_t *v)
{
    *u = (pos[0]*texinfo->vecs[0][0] + pos[1]*texinfo->vecs[0][1] + pos[2]*texinfo->vecs[0][2] + texinfo->vecs[0][3]) / width;
    *v = (pos[0]*texinfo->vecs[1][0] + pos[1]*texinfo->vecs[1][1] + pos[2]*texinfo->vecs[1][2] + texinfo->vecs[1][3]) / height;
}

static void
ExportObjFace(FILE *f, const face_t *face, int *vertcount)
{
    const texinfo_t *texinfos = (const texinfo_t *)pWorldEnt()->lumps[LUMP_TEXINFO].data;
    const texinfo_t *texinfo = &texinfos[face->texinfo];
    const char *texname = map.miptex[texinfo->miptex].c_str();
    
    const texture_t *texture = WADList_GetTexture(texname);
    const int width = texture ? texture->width : 64;
    const int height = texture ? texture->height : 64;
    
    // export the vertices and uvs
    for (int i=0; i<face->w.numpoints; i++)
    {
        const vec_t *pos = face->w.points[i];
        fprintf(f, "v %.9g %.9g %.9g\n", pos[0], pos[1], pos[2]);
        
        vec_t u, v;
        GetUV(texinfo, pos, width, height, &u, &v);
        
        // not sure why -v is needed, .obj uses (0, 0) in the top left apparently?
        fprintf(f, "vt %.9g %.9g\n", u, -v);
    }
    
    fprintf(f, "usemtl %s\n", texname);
    fprintf(f, "f");
    for (int i=0; i<face->w.numpoints; i++) {
        // .obj vertexes start from 1
        // .obj faces are CCW, quake is CW, so reverse the order
        const int vertindex = *vertcount + (face->w.numpoints - 1 - i) + 1;
        fprintf(f, " %d/%d", vertindex, vertindex);
    }
    fprintf(f, "\n");
    
    *vertcount += face->w.numpoints;
}

void
ExportObj(const surface_t *surfaces)
{
    FILE *objfile = InitObjFile();
    int vertcount = 0;
    
    const surface_t *surf;
    for (surf = surfaces; surf; surf = surf->next) {
        const face_t *face;
        for (face = surf->faces; face; face = face->next) {
            ExportObjFace(objfile, face, &vertcount);
        }
    }
    
    fclose(objfile);
}
