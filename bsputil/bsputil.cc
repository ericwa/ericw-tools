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

#include <stdint.h>

#ifndef WIN32
#include <unistd.h>
#endif

#include <common/cmdlib.hh>
#include <common/bspfile.hh>
#include <common/bsputils.hh>
#include <common/mathlib.hh>

#include "decompile.h"

#include <map>
#include <set>
#include <list>
#include <algorithm> // std::sort
#include <string>
#include <light/light.hh>

/* FIXME - share header with qbsp, etc. */
typedef struct {
    char identification[4];     // should be WAD2
    int32_t numlumps;
    int32_t infotableofs;
} wadinfo_t;

typedef struct {
    int32_t filepos;
    int32_t disksize;
    int32_t size;                       // uncompressed
    char type;
    char compression;
    char pad1, pad2;
    char name[16];              // must be null terminated
} lumpinfo_t;

static void
ExportWad(FILE *wadfile, mbsp_t *bsp)
{
    wadinfo_t header;
    lumpinfo_t lump;
    dmiptexlump_t *texdata;
    miptex_t *miptex;
    int i, j, size, filepos, numvalid;

    texdata = bsp->dtexdata;

    /* Count up the valid lumps */
    numvalid = 0;
    for (i = 0; i < texdata->nummiptex; i++)
        if (texdata->dataofs[i] >= 0)
            numvalid++;

    memcpy(&header.identification, "WAD2", 4);
    header.numlumps = numvalid;
    header.infotableofs = sizeof(header);

    /* Byte-swap header and write out */
    header.numlumps = LittleLong(header.numlumps);
    header.infotableofs = LittleLong(header.infotableofs);
    fwrite(&header, sizeof(header), 1, wadfile);

    /* Miptex data will follow the lump headers */
    filepos = sizeof(header) + numvalid * sizeof(lump);
    for (i = 0; i < texdata->nummiptex; i++) {
        if (texdata->dataofs[i] < 0)
            continue;

        miptex = (miptex_t *)((uint8_t *)texdata + texdata->dataofs[i]);

        lump.filepos = filepos;
        lump.size = sizeof(*miptex) + miptex->width * miptex->height / 64 * 85;
        lump.type = 'D';
        lump.disksize = lump.size;
        lump.compression = 0;
        lump.pad1 = lump.pad2 = 0;
        q_snprintf(lump.name, sizeof(lump.name), "%s", miptex->name);

        filepos += lump.disksize;

        /* Byte-swap lumpinfo and write out */
        lump.filepos = LittleLong(lump.filepos);
        lump.disksize = LittleLong(lump.disksize);
        lump.size = LittleLong(lump.size);
        fwrite(&lump, sizeof(lump), 1, wadfile);
    }
    for (i = 0; i < texdata->nummiptex; i++) {
        if (texdata->dataofs[i] < 0)
            continue;
        miptex = (miptex_t *)((uint8_t *)texdata + texdata->dataofs[i]);
        size = sizeof(*miptex) + miptex->width * miptex->height / 64 * 85;

        /* Byte-swap miptex info and write out */
        miptex->width = LittleLong(miptex->width);
        miptex->height = LittleLong(miptex->height);
        for (j = 0; j < MIPLEVELS; j++)
            miptex->offsets[j] = LittleLong(miptex->offsets[j]);
        fwrite(miptex, size, 1, wadfile);
    }
}

static void
PrintModelInfo(const mbsp_t *bsp)
{
    int i;

    for (i = 0; i < bsp->nummodels; i++) {
        const dmodel_t *dmodel = &bsp->dmodels[i];
        printf("model %3d: %5d faces (firstface = %d)\n",
               i, dmodel->numfaces, dmodel->firstface);
    }
}


/*
 * Quick hack to check verticies of faces lie on the correct plane
 */
#define PLANE_ON_EPSILON 0.01

static void
CheckBSPFacesPlanar(const mbsp_t *bsp)
{
    int i, j;

    for (i = 0; i < bsp->numfaces; i++) {
        const bsp2_dface_t *face = BSP_GetFace(bsp, i);
        dplane_t plane = bsp->dplanes[face->planenum];

        if (face->side) {
            VectorSubtract(vec3_origin, plane.normal, plane.normal);
            plane.dist = -plane.dist;
        }

        for (j = 0; j < face->numedges; j++) {
            const int edgenum = bsp->dsurfedges[face->firstedge + j];
            const int vertnum = (edgenum >= 0) ? bsp->dedges[edgenum].v[0] : bsp->dedges[-edgenum].v[1];
            const float *point = bsp->dvertexes[vertnum].point;
            const float dist = DotProduct(plane.normal, point) - plane.dist;

            if (dist < -PLANE_ON_EPSILON || dist > PLANE_ON_EPSILON)
                printf("WARNING: face %d, point %d off plane by %f\n",
                       (int)(face - bsp->dfaces), j, dist);
        }
    }
}

static int
Node_Height(const mbsp_t *bsp, const bsp2_dnode_t *node, std::map<const bsp2_dnode_t *, int> *cache)
{
    // leafs have a height of 0
    int child_heights[2] = {0, 0};
    
    for (int i=0; i<2; i++) {
        const int child = node->children[i];
        if (child >= 0) {
            child_heights[i] = Node_Height(bsp, &bsp->dnodes[child], cache);
        }
    }

    const int height = qmax(child_heights[0], child_heights[1]) + 1;
    if (cache)
        (*cache)[node] = height;
    return height;
}

static void PrintNodeHeights(const mbsp_t *bsp)
{
    // get all the heights in one go.
    const bsp2_dnode_t *headnode = &bsp->dnodes[bsp->dmodels[0].headnode[0]];
    std::map<const bsp2_dnode_t *, int> cache;
    Node_Height(bsp, headnode, &cache);
    
    const int maxlevel = 3;
    
    using level_t = int;
    using visit_t = std::pair<const bsp2_dnode_t *, level_t>;
    
    int current_level = -1;
    
    std::list<visit_t> tovisit { std::make_pair(headnode, 0) };
    while (!tovisit.empty())
    {
        const auto n = tovisit.front();
        tovisit.pop_front();
        
        const bsp2_dnode_t *node = n.first;
        const int level = n.second;
        
        Q_assert(level <= maxlevel);
        
        // handle this node
        if (level != current_level)
        {
            current_level = level;
            printf("\nNode heights at level %d: ", level);
        }
    
        // print the level of this node
        printf("%d, ", cache.at(node));
        
        // add child nodes to the bfs
        if (level < maxlevel) {
            for (int i=0; i<2; i++) {
                const int child = node->children[i];
                if (child >= 0) {
                    tovisit.push_back(std::make_pair(&bsp->dnodes[child], level + 1));
                }
            }
        }
    }
    printf("\n");
}

static void
CheckBSPFile(const mbsp_t *bsp)
{
    int i;

    // FIXME: Should do a better reachability check where we traverse the
    // nodes/leafs to find reachable faces.
    std::set<int32_t> referenced_texinfos;
    std::set<int32_t> referenced_planenums;
    std::set<uint32_t> referenced_vertexes;
    std::set<uint8_t> used_lightstyles;
    
    /* faces */
    for (i = 0; i < bsp->numfaces; i++) {
        const bsp2_dface_t *face = BSP_GetFace(bsp, i);

        /* texinfo bounds check */
        if (face->texinfo < 0)
            printf("warning: face %d has negative texinfo (%d)\n",
                   i, face->texinfo);
        if (face->texinfo >= bsp->numtexinfo)
            printf("warning: face %d has texinfo out of range (%d >= %d)\n",
                   i, face->texinfo, bsp->numtexinfo);
        referenced_texinfos.insert(face->texinfo);
        
        /* planenum bounds check */
        if (face->planenum < 0)
            printf("warning: face %d has negative planenum (%d)\n",
                   i, face->planenum);
        if (face->planenum >= bsp->numplanes)
            printf("warning: face %d has planenum out of range (%d >= %d)\n",
                   i, face->planenum, bsp->numplanes);
        referenced_planenums.insert(face->planenum);

        /* lightofs check */
        if (face->lightofs < -1)
            printf("warning: face %d has negative light offset (%d)\n",
                   i, face->lightofs);
        if (face->lightofs >= bsp->lightdatasize)
            printf("warning: face %d has light offset out of range "
                   "(%d >= %d)\n", i, face->lightofs, bsp->lightdatasize);

        /* edge check */
        if (face->firstedge < 0)
            printf("warning: face %d has negative firstedge (%d)\n",
                   i, face->firstedge);
        if (face->numedges < 3)
            printf("warning: face %d has < 3 edges (%d)\n",
                   i, face->numedges);
        if (face->firstedge + face->numedges > bsp->numsurfedges)
            printf("warning: face %d has edges out of range (%d..%d >= %d)\n",
                   i, face->firstedge, face->firstedge + face->numedges - 1,
                   bsp->numsurfedges);
        
        for (int j=0; j<4; j++) {
            used_lightstyles.insert(face->styles[j]);
        }
    }

    /* edges */
    for (i = 0; i < bsp->numedges; i++) {
        const bsp2_dedge_t *edge = &bsp->dedges[i];
        int j;

        for (j = 0; j < 2; j++) {
            const uint32_t vertex = edge->v[j];
            if (vertex > bsp->numvertexes)
                printf("warning: edge %d has vertex %d out range "
                       "(%d >= %d)\n", i, j, vertex, bsp->numvertexes);
            referenced_vertexes.insert(vertex);
        }
    }

    /* surfedges */
    for (i = 0; i < bsp->numsurfedges; i++) {
        const int edgenum = bsp->dsurfedges[i];
        if (!edgenum)
            printf("warning: surfedge %d has zero value!\n", i);
        if (abs(edgenum) >= bsp->numedges)
            printf("warning: surfedge %d is out of range (abs(%d) >= %d)\n",
                   i, edgenum, bsp->numedges);
    }

    /* marksurfaces */
    for (i = 0; i < bsp->numleaffaces; i++) {
        const uint32_t surfnum = bsp->dleaffaces[i];
        if (surfnum >= bsp->numfaces)
            printf("warning: marksurface %d is out of range (%d >= %d)\n",
                   i, surfnum, bsp->numfaces);
    }

    /* leafs */
    for (i = 0; i < bsp->numleafs; i++) {
        const mleaf_t *leaf = &bsp->dleafs[i];
        const uint32_t endmarksurface =
            leaf->firstmarksurface + leaf->nummarksurfaces;
        if (endmarksurface > bsp->numleaffaces)
            printf("warning: leaf %d has marksurfaces out of range "
                   "(%d..%d >= %d)\n", i, leaf->firstmarksurface,
                   endmarksurface - 1, bsp->numleaffaces);
        if (leaf->visofs < -1)
            printf("warning: leaf %d has negative visdata offset (%d)\n",
                   i, leaf->visofs);
        if (leaf->visofs >= bsp->visdatasize)
            printf("warning: leaf %d has visdata offset out of range "
                   "(%d >= %d)\n", i, leaf->visofs, bsp->visdatasize);
    }

    /* nodes */
    for (i = 0; i < bsp->numnodes; i++) {
        const bsp2_dnode_t *node = &bsp->dnodes[i];
        int j;

        for (j = 0; j < 2; j++) {
            const int32_t child = node->children[j];
            if (child >= 0 && child >= bsp->numnodes)
                printf("warning: node %d has child %d (node) out of range "
                       "(%d >= %d)\n", i, j, child, bsp->numnodes);
            if (child < 0 && -child - 1 >= bsp->numleafs)
                printf("warning: node %d has child %d (leaf) out of range "
                       "(%d >= %d)\n", i, j, -child - 1, bsp->numleafs);
        }
        
        if (node->children[0] == node->children[1]) {
            printf("warning: node %d has both children %d\n", i, node->children[0]);
        }
        
        referenced_planenums.insert(node->planenum);
    }
    
    /* clipnodes */
    for (i = 0; i < bsp->numclipnodes; i++) {
        const bsp2_dclipnode_t *clipnode = &bsp->dclipnodes[i];
        
        for (int j = 0; j < 2; j++) {
            const int32_t child = clipnode->children[j];
            if (child >= 0 && child >= bsp->numclipnodes)
                printf("warning: clipnode %d has child %d (clipnode) out of range "
                       "(%d >= %d)\n", i, j, child, bsp->numclipnodes);
            if (child < 0 && child < CONTENTS_MIN)
                printf("warning: clipnode %d has invalid contents (%d) for child %d\n",
                       i, child, j);
        }
        
        if (clipnode->children[0] == clipnode->children[1]) {
            printf("warning: clipnode %d has both children %d\n", i, clipnode->children[0]);
        }
        
        referenced_planenums.insert(clipnode->planenum);
    }

    /* TODO: finish range checks, add "unreferenced" checks... */
    
    /* unreferenced texinfo */
    {
        int num_unreferenced_texinfo = 0;
        for (i = 0; i < bsp->numtexinfo; i++) {
            if (referenced_texinfos.find(i) == referenced_texinfos.end()) {
                num_unreferenced_texinfo++;
            }
        }
        if (num_unreferenced_texinfo)
            printf("warning: %d texinfos are unreferenced\n", num_unreferenced_texinfo);
    }
    
    /* unreferenced planes */
    {
        int num_unreferenced_planes = 0;
        for (i = 0; i < bsp->numplanes; i++) {
            if (referenced_planenums.find(i) == referenced_planenums.end()) {
                num_unreferenced_planes++;
            }
        }
        if (num_unreferenced_planes)
            printf("warning: %d planes are unreferenced\n", num_unreferenced_planes);
    }
    
    /* unreferenced vertices */
    {
        int num_unreferenced_vertexes = 0;
        for (i = 0; i < bsp->numvertexes; i++) {
            if (referenced_vertexes.find(i) == referenced_vertexes.end()) {
                num_unreferenced_vertexes++;
            }
        }
        if (num_unreferenced_vertexes)
            printf("warning: %d vertexes are unreferenced\n", num_unreferenced_vertexes);
    }
    
    /* tree balance */
    PrintNodeHeights(bsp);
    
    /* unique visofs's */
    std::set<int32_t> visofs_set;
    for (i = 0; i < bsp->numleafs; i++) {
        const mleaf_t *leaf = &bsp->dleafs[i];
        if (leaf->visofs >= 0) {
            visofs_set.insert(leaf->visofs);
        }
    }
    printf("%d unique visdata offsets for %d leafs\n",
           static_cast<int>(visofs_set.size()), bsp->numleafs);
    printf("%d visleafs in world model\n", bsp->dmodels[0].visleafs);
    
    /* unique lightstyles */
    printf("%d lightstyles used:\n", static_cast<int>(used_lightstyles.size()));
    {
        std::vector<int> v;
        for (uint8_t style : used_lightstyles) {
            v.push_back(static_cast<int>(style));
        }
        std::sort(v.begin(), v.end());
        for (int style : v) {
            printf("\t%d\n", style);
        }
    }
    
    printf("world mins: %f %f %f maxs: %f %f %f\n",
           bsp->dmodels[0].mins[0],
           bsp->dmodels[0].mins[1],
           bsp->dmodels[0].mins[2],
           bsp->dmodels[0].maxs[0],
           bsp->dmodels[0].maxs[1],
           bsp->dmodels[0].maxs[2]);
}

static void
CompareBSPFiles(const mbsp_t *refBsp, const mbsp_t *bsp)
{
    printf("comparing %d with %d faces\n", refBsp->numfaces, bsp->numfaces);

    const dmodel_t *world = BSP_GetWorldModel(bsp);
    const dmodel_t *refWorld = BSP_GetWorldModel(refBsp);

    // iterate through the refBsp world faces
    for (int i=0; i<refWorld->numfaces; i++) {
        auto* refFace = BSP_GetFace(refBsp, refWorld->firstface + i);
        qvec3f refFaceCentroid = Face_Centroid(refBsp, refFace);

        // FIXME:   
        vec3_t wantedPoint;
        glm_to_vec3_t(refFaceCentroid, wantedPoint);

        vec3_t wantedNormal;
        Face_Normal(refBsp, refFace, wantedNormal);

        // Search for a face in bsp touching refFaceCentroid.
        auto* matchedFace = BSP_FindFaceAtPoint(bsp, world, wantedPoint, wantedNormal);
        if (matchedFace == nullptr) {
            printf("couldn't find a face at %f %f %f normal %f %f %f\n",
                    wantedPoint[0], wantedPoint[1], wantedPoint[2],
                    wantedNormal[0], wantedNormal[1], wantedNormal[2]);
        }

        // TODO: run on some more complex maps
//        auto* refFaceSelfCheck = BSP_FindFaceAtPoint(refBsp, refWorld, wantedPoint, wantedNormal);
//        if (refFaceSelfCheck == refFace) {
//            matches ++;
//        } else {
//            printf("not match at %f %f %f wanted %p got %p\n", wantedPoint[0], wantedPoint[1], wantedPoint[2], refFace, refFaceSelfCheck);
//            Face_DebugPrint(refBsp, refFace);
//            Face_DebugPrint(refBsp, refFaceSelfCheck);
//            notmat++;
//        }
    }


}

static void
FindFaces(const mbsp_t *bsp, const vec3_t pos, const vec3_t normal)
{
    for (int i = 0; i < bsp->nummodels; ++i) {
        const dmodelh2_t* model = &bsp->dmodels[i];
        const bsp2_dface_t* face = BSP_FindFaceAtPoint(bsp, model, pos, normal);

        if (face != nullptr) {
            printf("model %d face %d: texture '%s' texinfo %d\n",
                i, static_cast<int>(face - bsp->dfaces), 
                Face_TextureName(bsp, face),
                face->texinfo);
        }
    }
}


int
main(int argc, char **argv)
{
    bspdata_t bspdata;
    mbsp_t *const bsp = &bspdata.data.mbsp;
    char source[1024];
    FILE *f;
    int i, err;

    printf("---- bsputil / ericw-tools " stringify(ERICWTOOLS_VERSION) " ----\n");
    if (argc == 1) {
        printf("usage: bsputil [--extract-entities] [--extract-textures] [--convert bsp29|bsp2|bsp2rmq|q2bsp] [--check] [--modelinfo]\n"
               "[--check] [--compare otherbsp] [--findfaces x y z nx ny nz] [--settexinfo facenum texinfonum]\n"
               "[--decompile] [--decompile-geomonly] bspfile\n");
        exit(1);
    }

    strcpy(source, argv[argc - 1]);
    DefaultExtension(source, ".bsp");
    printf("---------------------\n");
    printf("%s\n", source);

    LoadBSPFile(source, &bspdata);

    ConvertBSPFormat(&bspdata, &bspver_generic);

    for (i = 0; i < argc - 1; i++) {
        if (!strcmp(argv[i], "--compare")) {
            i++;
            if (i == argc - 1) {
                Error("--compare requires two arguments");
            }
            // Load the reference BSP

            char refbspname[1024];
            bspdata_t refbspdata;
            strcpy(refbspname, argv[i]);
            DefaultExtension(refbspname, ".bsp");
            LoadBSPFile(refbspname, &refbspdata);
            ConvertBSPFormat(&refbspdata, &bspver_generic);

            printf("comparing reference bsp %s with test bsp %s\n", refbspname, source);

            CompareBSPFiles(&refbspdata.data.mbsp,
                            &bspdata.data.mbsp);

            break;
        } else if (!strcmp(argv[i], "--convert")) {
            i++;
            if (!(i < argc - 1)) {
                Error("--convert requires an argument");
            }

            const bspversion_t *fmt = nullptr;

            for (const bspversion_t *bspver : bspversions) {
                if (!strcmp(argv[i], bspver->short_name)) {
                    fmt = bspver;
                    break;
                }
            }

            if (!fmt) {
                Error("Unsupported format %s", argv[i]);
            }

            ConvertBSPFormat(&bspdata, fmt);
            
            StripExtension(source);
            strcat(source, "-");
            strcat(source, argv[i]);
            strcat(source, ".bsp");
            
            WriteBSPFile(source, &bspdata);
            
        } else if (!strcmp(argv[i], "--extract-entities")) {
            unsigned int crc = CRC_Block((unsigned char *)bsp->dentdata, bsp->entdatasize - 1);
            StripExtension(source);
            DefaultExtension(source, ".ent");
            printf("-> writing %s [CRC: %04x]... ", source, crc);

            f = fopen(source, "wb");
            if (!f)
                Error("couldn't open %s for writing\n", source);

            err = fwrite(bsp->dentdata, sizeof(char), bsp->entdatasize - 1, f);
            if (err != bsp->entdatasize - 1)
                Error("%s", strerror(errno));

            err = fclose(f);
            if (err)
                Error("%s", strerror(errno));

            printf("done.\n");
        } else if (!strcmp(argv[i], "--extract-textures")) {
            StripExtension(source);
            DefaultExtension(source, ".wad");
            printf("-> writing %s... ", source);

            f = fopen(source, "wb");
            if (!f)
                Error("couldn't open %s for writing\n", source);

            ExportWad(f, bsp);

            err = fclose(f);
            if (err)
                Error("%s", strerror(errno));

            printf("done.\n");
        } else if (!strcmp(argv[i], "--check")) {
            printf("Beginning BSP data check...\n");
            CheckBSPFile(bsp);
            CheckBSPFacesPlanar(bsp);
            printf("Done.\n");
        } else if (!strcmp(argv[i], "--modelinfo")) {
            PrintModelInfo(bsp);
        } else if (!strcmp(argv[i], "--findfaces")) {
            // (i + 1) ... (i + 6) = x y z nx ny nz
            // i + 7 = bsp file

            if (i + 7 >= argc) {
               Error("--findfaces requires 6 arguments");
            }

            try {
                const vec3_t pos = {
                    std::stof(argv[i + 1]),
                    std::stof(argv[i + 2]),
                    std::stof(argv[i + 3])
                };
                const vec3_t normal = {
                    std::stof(argv[i + 4]),
                    std::stof(argv[i + 5]),
                    std::stof(argv[i + 6])
                };
                FindFaces(bsp, pos, normal);
            } catch (const std::exception&) {
                printf("Error reading position/normal\n");
            }
            return 0;
        } else if (!strcmp(argv[i], "--settexinfo")) {
            // (i + 1) facenum
            // (i + 2) texinfonum

            if (i + 2 >= argc) {
               Error("--settexinfo requires 2 arguments");
            }

            const int fnum       = std::stoi(argv[i + 1]);
            const int texinfonum = std::stoi(argv[i + 2]);

            bsp2_dface_t* face = BSP_GetFace(bsp, fnum);
            face->texinfo = texinfonum;

            ConvertBSPFormat(&bspdata, bspdata.loadversion);            

            // Overwrite source bsp!
            WriteBSPFile(source, &bspdata);

            return 0;
        } else if (!strcmp(argv[i], "--decompile") || !strcmp(argv[i], "--decompile-geomonly")) {
            const bool geomOnly = !strcmp(argv[i], "--decompile-geomonly");

            StripExtension(source);
            DefaultExtension(source, "-decompile.map");
            printf("-> writing %s... ", source);

            f = fopen(source, "w");
            if (!f)
                Error("couldn't open %s for writing\n", source);

            decomp_options options;
            options.geometryOnly = geomOnly;

            DecompileBSP(bsp, options, f);

            fclose(f);
            printf("done.\n");
            return 0;
        }
    }

    printf("---------------------\n");

    return 0;
}
