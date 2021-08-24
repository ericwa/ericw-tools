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

#include <stddef.h>

#include <qbsp/file.hh>
#include <qbsp/qbsp.hh>

static dheader_t *header;

typedef struct bspxentry_s
{
    char lumpname[24];
    const void *lumpdata;
    size_t lumpsize;

    struct bspxentry_s *next;
} bspxentry_t;
static bspxentry_t *bspxentries;

/*
=============
LoadBSPFile
=============
*/
void
LoadBSPFile(void)
{
}

//============================================================================

// TODO: remove this once we switch to common
static void
AddLumpFromBuffer(FILE *f, int Type, void* src, size_t srcbytes)
{
    lump_t *lump;
    size_t ret;
    const mapentity_t *entity;

    lump = &header->lumps[Type];
    lump->fileofs = ftell(f);

    if (srcbytes) {
        ret = fwrite(src, 1, srcbytes, f);
        if (ret != srcbytes)
            Error("Failure writing to file");
    }

    lump->filelen = srcbytes;

    // Pad to 4-byte boundary
    if (srcbytes % 4 != 0) {
        size_t pad = 4 - (srcbytes % 4);
        ret = fwrite("   ", 1, pad, f);
        if (ret != pad)
            Error("Failure writing to file");
    }
}

static void
GenLump(const char *bspxlump, int Type, size_t sz)
{
    int cLen = 0;
    int i;
    const struct lumpdata *entities;
    const mapentity_t *entity;
    char *out;

    for (i = 0; i < map.numentities(); i++) {
        entity = &map.entities.at(i);
        entities = &entity->lumps[Type];
        cLen += entities->count*sz; 
    }
    if (!cLen)
        return;
    out = (char *)malloc(cLen);
    cLen = 0;
    for (i = 0; i < map.numentities(); i++) {
        entity = &map.entities.at(i);
        entities = &entity->lumps[Type];
        memcpy(out+cLen, entities->data, entities->count*sz);
        cLen += entities->count*sz;
    }
    BSPX_AddLump(bspxlump, out, cLen);
}

void BSPX_AddLump(const char *xname, const void *xdata, size_t xsize)
{
    bspxentry_t *e;
    for (e = bspxentries; e; e = e->next)
    {
        if (!strcmp(e->lumpname, xname))
            break;
    }
    if (!e)
    {
        e = (bspxentry_t *)malloc(sizeof(*e));
        memset(e, 0, sizeof(*e));
        strncpy(e->lumpname, xname, sizeof(e->lumpname));
        e->next = bspxentries;
        bspxentries = e;
    }

    e->lumpdata = xdata;
    e->lumpsize = xsize;
}

/*
=============
WriteBSPFile
=============
*/
void
WriteBSPFile(void)
{
    FILE *f;
    size_t ret;

    header = (dheader_t *)AllocMem(OTHER, sizeof(dheader_t), true);
    header->version = options.BSPVersion;

    StripExtension(options.szBSPName);
    strcat(options.szBSPName, ".bsp");

    f = fopen(options.szBSPName, "wb");
    if (!f)
        Error("Failed to open %s: %s", options.szBSPName, strerror(errno));

    /* write placeholder, header is overwritten later */
    ret = fwrite(header, sizeof(dheader_t), 1, f);
    if (ret != 1)
        Error("Failure writing to file");

    AddLumpFromBuffer(f, LUMP_PLANES, map.exported_planes.data(), map.exported_planes.size() * sizeof(map.exported_planes[0]));
    AddLumpFromBuffer(f, LUMP_LEAFS, map.exported_leafs_bsp29.data(), map.exported_leafs_bsp29.size() * sizeof(map.exported_leafs_bsp29[0]));
    AddLumpFromBuffer(f, LUMP_VERTEXES, map.exported_vertexes.data(), map.exported_vertexes.size() * sizeof(map.exported_vertexes[0]));
    AddLumpFromBuffer(f, LUMP_NODES, map.exported_nodes_bsp29.data(), map.exported_nodes_bsp29.size() * sizeof(map.exported_nodes_bsp29[0]));
    AddLumpFromBuffer(f, LUMP_TEXINFO, map.exported_texinfos.data(), map.exported_texinfos.size() * sizeof(map.exported_texinfos[0]));
    AddLumpFromBuffer(f, LUMP_FACES, map.exported_faces.data(), map.exported_faces.size() * sizeof(map.exported_faces[0]));
    AddLumpFromBuffer(f, LUMP_CLIPNODES, map.exported_clipnodes.data(), map.exported_clipnodes.size() * sizeof(map.exported_clipnodes[0]));
    AddLumpFromBuffer(f, LUMP_MARKSURFACES, map.exported_marksurfaces.data(), map.exported_marksurfaces.size() * sizeof(map.exported_marksurfaces[0]));
    AddLumpFromBuffer(f, LUMP_SURFEDGES, map.exported_surfedges.data(), map.exported_surfedges.size() * sizeof(map.exported_surfedges[0]));
    AddLumpFromBuffer(f, LUMP_EDGES, map.exported_edges.data(), map.exported_edges.size() * sizeof(map.exported_edges[0]));
    AddLumpFromBuffer(f, LUMP_MODELS, map.exported_models.data(), map.exported_models.size() * sizeof(map.exported_models[0]));

    AddLumpFromBuffer(f, LUMP_LIGHTING, nullptr, 0);
    AddLumpFromBuffer(f, LUMP_VISIBILITY, nullptr, 0);
    AddLumpFromBuffer(f, LUMP_ENTITIES, map.exported_entities.data(), map.exported_entities.size() + 1); // +1 to write the terminating null (safe in C++11)
    AddLumpFromBuffer(f, LUMP_TEXTURES, map.exported_texdata.data(), map.exported_texdata.size());

    GenLump("LMSHIFT", BSPX_LMSHIFT, 1);

    // TODO: pass bspx lumps to generic bsp code so they are written

    fseek(f, 0, SEEK_SET);
    ret = fwrite(header, sizeof(dheader_t), 1, f);
    if (ret != 1)
        Error("Failure writing to file");

    if (fclose(f) != 0)
        Error("Failure closing file");
    
    logprint("Wrote %s\n", options.szBSPName);
    
    FreeMem(header, OTHER, sizeof(dheader_t));
}

//============================================================================

/*
=============
PrintBSPFileSizes

Dumps info about current file
=============
*/
void
PrintBSPFileSizes(void)
{
    struct lumpdata *lump;

    Message(msgStat, "%8d planes       %10d", static_cast<int>(map.exported_planes.size()), static_cast<int>(map.exported_planes.size()) * MemSize[BSP_PLANE]);
    Message(msgStat, "%8d vertexes     %10d", static_cast<int>(map.exported_vertexes.size()), static_cast<int>(map.exported_vertexes.size()) * MemSize[BSP_VERTEX]);
    Message(msgStat, "%8d nodes        %10d", static_cast<int>(map.exported_nodes_bsp29.size()), static_cast<int>(map.exported_nodes_bsp29.size()) * MemSize[BSP_NODE]);
    Message(msgStat, "%8d texinfo      %10d", static_cast<int>(map.exported_texinfos.size()), static_cast<int>(map.exported_texinfos.size()) * MemSize[BSP_TEXINFO]);
    Message(msgStat, "%8d faces        %10d", static_cast<int>(map.exported_faces.size()), static_cast<int>(map.exported_faces.size()) * MemSize[BSP_FACE]);
    Message(msgStat, "%8d clipnodes    %10d", static_cast<int>(map.exported_clipnodes.size()), static_cast<int>(map.exported_clipnodes.size()) * MemSize[BSP_CLIPNODE]);
    Message(msgStat, "%8d leafs        %10d", static_cast<int>(map.exported_leafs_bsp29.size()), static_cast<int>(map.exported_leafs_bsp29.size()) * MemSize[BSP_LEAF]);
    Message(msgStat, "%8d marksurfaces %10d", static_cast<int>(map.exported_marksurfaces.size()), static_cast<int>(map.exported_marksurfaces.size()) * MemSize[BSP_MARKSURF]);
    Message(msgStat, "%8d surfedges    %10d", static_cast<int>(map.exported_surfedges.size()), static_cast<int>(map.exported_surfedges.size()) * MemSize[BSP_SURFEDGE]);
    Message(msgStat, "%8d edges        %10d", static_cast<int>(map.exported_edges.size()), static_cast<int>(map.exported_edges.size()) * MemSize[BSP_EDGE]);

    if (!map.exported_texdata.empty())
        Message(msgStat, "%8d textures     %10d",
                ((dmiptexlump_t *)map.exported_texdata.data())->nummiptex, map.exported_texdata.size());
    else
        Message(msgStat, "       0 textures              0");

    Message(msgStat, "         lightdata    %10d", 0);
    Message(msgStat, "         visdata      %10d", 0);
    Message(msgStat, "         entdata      %10d", static_cast<int>(map.exported_entities.size()) + 1);

    if (bspxentries) {
        bspxentry_t *x;
        for (x = bspxentries; x; x = x->next) {
            Message(msgStat, "%8s %-12s %10i", "BSPX", x->lumpname, x->lumpsize);
        }
    }
}
