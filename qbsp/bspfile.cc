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
    int i;
    int cFileSize, cLumpSize, iLumpOff;

    // Load the file header
    StripExtension(options.szBSPName);
    strcat(options.szBSPName, ".bsp");
    cFileSize = LoadFile(options.szBSPName, &header, true);

    switch (header->version) {
    case BSPVERSION:
    case BSPHLVERSION:
        MemSize = MemSize_BSP29;
        break;
    case BSP2RMQVERSION:
        MemSize = MemSize_BSP2rmq;
        break;
    case BSP2VERSION:
        MemSize = MemSize_BSP2;
        break;
    default:
        Error("%s has unknown BSP version %d",
              options.szBSPName, header->version);
    }
    options.BSPVersion = header->version;

    /* Throw all of the data into the first entity to be written out later */
    if (map.entities.empty()) {
        map.entities.push_back(mapentity_t {});
    }
    
    mapentity_t *entity = &map.entities.at(0);
    for (i = 0; i < BSP_LUMPS; i++) {
        map.cTotal[i] = cLumpSize = header->lumps[i].filelen;
        iLumpOff = header->lumps[i].fileofs;
        
        if (i == LUMP_MODELS && !options.hexen2) {
            int j;

            if (cLumpSize % sizeof(dmodelq1_t))
                Error("Deformed lump in BSP file (size %d is not divisible by %d)",
                      cLumpSize, (int)sizeof(dmodelq1_t));
            
            entity->lumps[i].count = cLumpSize / sizeof(dmodelq1_t);
            entity->lumps[i].data = AllocMem(i, entity->lumps[i].count, false);
            
            map.cTotal[i] = entity->lumps[i].count * sizeof(dmodel_t);
            
            for (j=0; j<entity->lumps[i].count; j++)
            {
                int k;
                const dmodelq1_t *in = (dmodelq1_t *)((uint8_t *)header + iLumpOff) + j;
                dmodelh2_t *out = (dmodelh2_t *)entity->lumps[i].data + j;
                
                for (k = 0; k < 3; k++) {
                    out->mins[k] = in->mins[k];
                    out->maxs[k] = in->maxs[k];
                    out->origin[k] = in->origin[k];
                }
                for (k = 0; k < MAX_MAP_HULLS_Q1; k++)
                    out->headnode[k] = in->headnode[k];
                out->visleafs = in->visleafs;
                out->firstface = in->firstface;
                out->numfaces = in->numfaces;
            }
        } else {
            if (cLumpSize % MemSize[i])
                Error("Deformed lump in BSP file (size %d is not divisible by %d)",
                      cLumpSize, MemSize[i]);
            
            entity->lumps[i].count = cLumpSize / MemSize[i];
            entity->lumps[i].data = AllocMem(i, entity->lumps[i].count, false);
            
            memcpy(entity->lumps[i].data, (uint8_t *)header + iLumpOff, cLumpSize);
        }
    }

    FreeMem(header, OTHER, cFileSize + 1);
}

//============================================================================

// To be used for all dynamic mem data
static void
AddLump(FILE *f, int Type)
{
    lump_t *lump;
    int cLen = 0;
    int i;
    size_t ret;
    const struct lumpdata *entities;
    const mapentity_t *entity;

    lump = &header->lumps[Type];
    lump->fileofs = ftell(f);

    for (i = 0; i < map.numentities(); i++) {
        entity = &map.entities.at(i);
        entities = &entity->lumps[Type];
        if (entities->data) {
            if (Type == LUMP_MODELS && !options.hexen2) {
                const dmodel_t *in = (const dmodel_t *)entities->data;
                dmodelq1_t out;
                int j, k;
                for (j = 0; j < entities->count; j++)
                {
                    for (k = 0; k < 3; k++) {
                        out.mins[k] = in[j].mins[k];
                        out.maxs[k] = in[j].maxs[k];
                        out.origin[k] = in[j].origin[k];
                    }
                    for (k = 0; k < MAX_MAP_HULLS_Q1; k++)
                        out.headnode[k] = in[j].headnode[k];
                    out.visleafs = in[j].visleafs;
                    out.firstface = in[j].firstface;
                    out.numfaces = in[j].numfaces;
                    ret = fwrite(&out, sizeof(out), 1, f);
                    if (ret != 1)
                        Error("Failure writing to file");
                }
                cLen += entities->count * sizeof(out);
            } else {
                ret = fwrite(entities->data, MemSize[Type], entities->count, f);
                if (ret != entities->count)
                    Error("Failure writing to file");
                cLen += entities->count * MemSize[Type];
            }
        }
    }

    // Add null terminating char for text
    if (Type == LUMP_ENTITIES) {
        ret = fwrite("", 1, 1, f);
        if (ret != 1)
            Error("Failure writing to file");
        cLen++;
    }
    lump->filelen = cLen;

    // Pad to 4-byte boundary
    if (cLen % 4 != 0) {
        size_t pad = 4 - (cLen % 4);
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

    AddLump(f, LUMP_PLANES);
    AddLump(f, LUMP_LEAFS);
    AddLump(f, LUMP_VERTEXES);
    AddLump(f, LUMP_NODES);
    AddLump(f, LUMP_TEXINFO);
    AddLump(f, LUMP_FACES);
    AddLump(f, LUMP_CLIPNODES);
    AddLump(f, LUMP_MARKSURFACES);
    AddLump(f, LUMP_SURFEDGES);
    AddLump(f, LUMP_EDGES);
    AddLump(f, LUMP_MODELS);

    AddLump(f, LUMP_LIGHTING);
    AddLump(f, LUMP_VISIBILITY);
    AddLump(f, LUMP_ENTITIES);
    AddLump(f, LUMP_TEXTURES);

    GenLump("LMSHIFT", BSPX_LMSHIFT, 1);

    /*BSPX lumps are at a 4-byte alignment after the last of any official lump*/
    if (bspxentries)
    {
#define LittleLong(x) x
#define SafeWrite(f,p,l) fwrite(p, 1, l, f);
        bspx_header_t xheader;
        bspxentry_t *x; 
        bspx_lump_t xlumps[64];
        uint32_t l;
        uint32_t bspxheader = ftell(f);
        if (bspxheader & 3)
            Error("BSPX header is misaligned");
        xheader.id[0] = 'B';
        xheader.id[1] = 'S';
        xheader.id[2] = 'P';
        xheader.id[3] = 'X';
        xheader.numlumps = 0;
        for (x = bspxentries; x; x = x->next)
            xheader.numlumps++;

        if (xheader.numlumps > sizeof(xlumps)/sizeof(xlumps[0]))        /*eep*/
            xheader.numlumps = sizeof(xlumps)/sizeof(xlumps[0]);

        SafeWrite(f, &xheader, sizeof(xheader));
        SafeWrite(f, xlumps, xheader.numlumps * sizeof(xlumps[0]));

        for (x = bspxentries, l = 0; x && l < xheader.numlumps; x = x->next, l++)
        {
            uint8_t pad[4] = {0};
            xlumps[l].filelen = LittleLong(x->lumpsize);
            xlumps[l].fileofs = LittleLong(ftell(f));
            strncpy(xlumps[l].lumpname, x->lumpname, sizeof(xlumps[l].lumpname));
            SafeWrite(f, x->lumpdata, x->lumpsize);
            if (x->lumpsize % 4)
                SafeWrite(f, pad, 4 - (x->lumpsize % 4));
        }

        fseek(f, bspxheader, SEEK_SET);
        SafeWrite(f, &xheader, sizeof(xheader));
        SafeWrite(f, xlumps, xheader.numlumps * sizeof(xlumps[0]));
    }

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

    Message(msgStat, "%8d planes       %10d", map.cTotal[LUMP_PLANES],       map.cTotal[LUMP_PLANES] * MemSize[BSP_PLANE]);
    Message(msgStat, "%8d vertexes     %10d", map.cTotal[LUMP_VERTEXES],     map.cTotal[LUMP_VERTEXES] * MemSize[BSP_VERTEX]);
    Message(msgStat, "%8d nodes        %10d", map.cTotal[LUMP_NODES],        map.cTotal[LUMP_NODES] * MemSize[BSP_NODE]);
    Message(msgStat, "%8d texinfo      %10d", map.cTotal[LUMP_TEXINFO],      map.cTotal[LUMP_TEXINFO] * MemSize[BSP_TEXINFO]);
    Message(msgStat, "%8d faces        %10d", map.cTotal[LUMP_FACES],        map.cTotal[LUMP_FACES] * MemSize[BSP_FACE]);
    Message(msgStat, "%8d clipnodes    %10d", map.cTotal[LUMP_CLIPNODES],    map.cTotal[LUMP_CLIPNODES] * MemSize[BSP_CLIPNODE]);
    Message(msgStat, "%8d leafs        %10d", map.cTotal[LUMP_LEAFS],        map.cTotal[LUMP_LEAFS] * MemSize[BSP_LEAF]);
    Message(msgStat, "%8d marksurfaces %10d", map.cTotal[LUMP_MARKSURFACES], map.cTotal[LUMP_MARKSURFACES] * MemSize[BSP_MARKSURF]);
    Message(msgStat, "%8d surfedges    %10d", map.cTotal[LUMP_SURFEDGES],    map.cTotal[LUMP_SURFEDGES] * MemSize[BSP_SURFEDGE]);
    Message(msgStat, "%8d edges        %10d", map.cTotal[LUMP_EDGES],        map.cTotal[LUMP_EDGES] * MemSize[BSP_EDGE]);

    lump = &pWorldEnt()->lumps[LUMP_TEXTURES];
    if (lump->data)
        Message(msgStat, "%8d textures     %10d",
                ((dmiptexlump_t *)lump->data)->nummiptex, lump->count);
    else
        Message(msgStat, "       0 textures              0");

    Message(msgStat, "         lightdata    %10d", map.cTotal[LUMP_LIGHTING]);
    Message(msgStat, "         visdata      %10d", map.cTotal[LUMP_VISIBILITY]);
    Message(msgStat, "         entdata      %10d", map.cTotal[LUMP_ENTITIES] + 1);

    if (bspxentries) {
        bspxentry_t *x;
        for (x = bspxentries; x; x = x->next) {
            Message(msgStat, "%8s %-12s %10i", "BSPX", x->lumpname, x->lumpsize);
        }
    }
}
