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
// writebsp.c

#include <qbsp/qbsp.hh>
#include <qbsp/wad.hh>

#include <vector>
#include <algorithm>
#include <cstdint>

static void
AssertVanillaContentType(int content)
{
    switch (content) {
    case CONTENTS_EMPTY:
    case CONTENTS_SOLID:
    case CONTENTS_WATER:
    case CONTENTS_SLIME:
    case CONTENTS_LAVA:
    case CONTENTS_SKY:
        break;
    default:
        Error("Internal error: Tried to save compiler-internal contents type %s\n", GetContentsName(content));
    }
}

static int
RemapContentsForExport(int content)
{
    if (content == CONTENTS_DETAIL_FENCE) {
        /*
         * This is for func_detail_wall.. we want to write a solid leaf that has faces,
         * because it may be possible to see inside (fence textures).
         *
         * Normally solid leafs are not written and just referenced as leaf 0.
         */
        return CONTENTS_SOLID;
    }
    if (content == CONTENTS_ILLUSIONARY_VISBLOCKER) {
        return CONTENTS_EMPTY;
    }
    return content;
}

/**
 * Returns the output plane number
 */
int
ExportMapPlane(int planenum)
{
    qbsp_plane_t *plane = &map.planes.at(planenum);

    if (plane->outputplanenum != -1)
        return plane->outputplanenum; // already output.
    
    const int newIndex = static_cast<int>(map.exported_planes.size());
    map.exported_planes.push_back({});

    dplane_t *dplane = &map.exported_planes.back();
    dplane->normal[0] = plane->normal[0];
    dplane->normal[1] = plane->normal[1];
    dplane->normal[2] = plane->normal[2];
    dplane->dist = plane->dist;
    dplane->type = plane->type;
    
    plane->outputplanenum = newIndex;
    return newIndex;
}

int
ExportMapTexinfo(int texinfonum)
{
    mtexinfo_t *src = &map.mtexinfos.at(texinfonum);
    if (src->outputnum != -1)
        return src->outputnum;
    
    // this will be the index of the exported texinfo in the BSP lump
    const int i = static_cast<int>(map.exported_texinfos.size());
    
    map.exported_texinfos.push_back({});
    gtexinfo_t* dest = &map.exported_texinfos.back();
    memset(dest, 0, sizeof(dest));

    dest->flags = static_cast<int32_t>(src->flags & TEX_SPECIAL);
    dest->miptex = src->miptex;
    for (int j=0; j<2; j++) {
        for (int k=0; k<4; k++) {
            dest->vecs[j][k] = src->vecs[j][k];
        }
    }
    // FIXME-Q2: fill in other attributes

    src->outputnum = i;    
    return i;
}

//===========================================================================

/*
==================
ExportClipNodes
==================
*/
static int
ExportClipNodes_BSP29(mapentity_t *entity, node_t *node)
{
    bsp2_dclipnode_t *clipnode;
    face_t *face, *next;

    // FIXME: free more stuff?
    if (node->planenum == -1) {
        int contents = node->contents;
        FreeMem(node, NODE, 1);
        return contents;
    }

    /* emit a clipnode */
    const int nodenum = static_cast<int>(map.exported_clipnodes.size());
    map.exported_clipnodes.push_back({});

    const int child0 = ExportClipNodes_BSP29(entity, node->children[0]);
    const int child1 = ExportClipNodes_BSP29(entity, node->children[1]);

    // Careful not to modify the vector while using this clipnode pointer
    clipnode = &map.exported_clipnodes.at(nodenum);
    clipnode->planenum = ExportMapPlane(node->planenum);
    clipnode->children[0] = child0;
    clipnode->children[1] = child1;

    for (face = node->faces; face; face = next) {
        next = face->next;
        memset(face, 0, sizeof(face_t));
        FreeMem(face, FACE, 1);
    }
    FreeMem(node, NODE, 1);

    return nodenum;
}

/*
==================
ExportClipNodes

Called after the clipping hull is completed.  Generates a disk format
representation and frees the original memory.

This gets real ugly.  Gets called twice per entity, once for each clip hull.
First time just store away data, second time fix up reference points to
accomodate new data interleaved with old.
==================
*/
void
ExportClipNodes(mapentity_t *entity, node_t *nodes, const int hullnum)
{
    auto *model = &map.exported_models.at(static_cast<size_t>(entity->outputmodelnumber));

    model->headnode[hullnum] = ExportClipNodes_BSP29(entity, nodes);
}

//===========================================================================

/*
==================
ExportLeaf
==================
*/
static void
ExportLeaf_BSP29(mapentity_t *entity, node_t *node)
{
    map.exported_leafs_bsp29.push_back({});
    mleaf_t *dleaf = &map.exported_leafs_bsp29.back();

    dleaf->contents = RemapContentsForExport(node->contents);
    AssertVanillaContentType(dleaf->contents);

    /*
     * write bounding box info
     * (VectorCopy doesn't work since dest are shorts)
     */
    dleaf->mins[0] = (short)node->mins[0];
    dleaf->mins[1] = (short)node->mins[1];
    dleaf->mins[2] = (short)node->mins[2];
    dleaf->maxs[0] = (short)node->maxs[0];
    dleaf->maxs[1] = (short)node->maxs[1];
    dleaf->maxs[2] = (short)node->maxs[2];

    dleaf->visofs = -1; // no vis info yet

    // write the marksurfaces
    dleaf->firstmarksurface = static_cast<int>(map.exported_marksurfaces.size());

    for (face_t **markfaces = node->markfaces; *markfaces; markfaces++) {
        face_t *face = *markfaces;
        if (map.mtexinfos.at(face->texinfo).flags & TEX_SKIP)
            continue;

        /* emit a marksurface */
        do {
            map.exported_marksurfaces.push_back(face->outputnumber);
            face = face->original;      /* grab tjunction split faces */
        } while (face);
    }
    dleaf->nummarksurfaces =
        static_cast<int>(map.exported_marksurfaces.size()) - dleaf->firstmarksurface;

    // FIXME-Q2: fill in other things
}

/*
==================
ExportDrawNodes
==================
*/
static void
ExportDrawNodes_BSP29(mapentity_t *entity, node_t *node)
{
    bsp2_dnode_t *dnode;
    int i;

    const size_t ourNodeIndex = map.exported_nodes_bsp29.size();
    map.exported_nodes_bsp29.push_back({});

    dnode = &map.exported_nodes_bsp29[ourNodeIndex];

    // VectorCopy doesn't work since dest are shorts
    dnode->mins[0] = (short)node->mins[0];
    dnode->mins[1] = (short)node->mins[1];
    dnode->mins[2] = (short)node->mins[2];
    dnode->maxs[0] = (short)node->maxs[0];
    dnode->maxs[1] = (short)node->maxs[1];
    dnode->maxs[2] = (short)node->maxs[2];

    dnode->planenum = ExportMapPlane(node->planenum);
    dnode->firstface = node->firstface;
    dnode->numfaces = node->numfaces;

    // recursively output the other nodes
    for (i = 0; i < 2; i++) {
        if (node->children[i]->planenum == -1) {
            if (node->children[i]->contents == CONTENTS_SOLID)
                dnode->children[i] = -1;
            else {
                int nextLeafIndex = static_cast<int>(map.exported_leafs_bsp29.size());
                int childnum = -(nextLeafIndex + 1);
                if (childnum < INT16_MIN) {
                    Error("Map exceeds BSP29 node/leaf limit. Recompile with -bsp2 flag.");
                }
                dnode->children[i] = childnum;
                ExportLeaf_BSP29(entity, node->children[i]);
            }
        } else {
            int childnum = static_cast<int>(map.exported_nodes_bsp29.size());
            if (childnum > INT16_MAX) {
                Error("Map exceeds BSP29 node/leaf limit. Recompile with -bsp2 flag.");
            }
            dnode->children[i] = childnum;
            ExportDrawNodes_BSP29(entity, node->children[i]);

            // Important: our dnode pointer may be invalid after the recursive call, if the vector got resized.
            // So re-set the pointer.
            dnode = &map.exported_nodes_bsp29[ourNodeIndex];
        }
    }

    // DarkPlaces asserts that the leaf numbers are different
    // if mod_bsp_portalize is 1 (default)
    // The most likely way it could fail is if both sides are the
    // shared CONTENTS_SOLID leaf (-1)
    Q_assert(!(dnode->children[0] == -1 && dnode->children[1] == -1));
	Q_assert(dnode->children[0] != dnode->children[1]);
}

/*
==================
ExportDrawNodes
==================
*/
void
ExportDrawNodes(mapentity_t *entity, node_t *headnode, int firstface)
{
    int i;
    dmodelh2_t *dmodel;

    // populate model struct (which was emitted previously)
    dmodel = &map.exported_models.at(static_cast<size_t>(entity->outputmodelnumber));
    dmodel->headnode[0] = static_cast<int>(map.exported_nodes_bsp29.size());
    dmodel->firstface = firstface;
    dmodel->numfaces = static_cast<int>(map.exported_faces.size()) - firstface;

    const size_t mapleafsAtStart = map.exported_leafs_bsp29.size();

    {
        if (headnode->contents < 0)
            ExportLeaf_BSP29(entity, headnode);
        else
            ExportDrawNodes_BSP29(entity, headnode);
    }

    // count how many leafs were exported by the above calls
    dmodel->visleafs = static_cast<int>(map.exported_leafs_bsp29.size() - mapleafsAtStart);

    /* remove the headnode padding */
    for (i = 0; i < 3; i++) {
        dmodel->mins[i] = headnode->mins[i] + SIDESPACE + 1;
        dmodel->maxs[i] = headnode->maxs[i] - SIDESPACE - 1;
    }
}

//=============================================================================

/*
==================
BeginBSPFile
==================
*/
void
BeginBSPFile(void)
{
    // First edge must remain unused because 0 can't be negated
    map.exported_edges.push_back({});
    Q_assert(map.exported_edges.size() == 1);

    // Leave room for leaf 0 (must be solid)
    map.exported_leafs_bsp29.push_back({});
    map.exported_leafs_bsp29.back().contents = CONTENTS_SOLID; // FIXME-Q2: use Q2_CONTENTS_SOLID
    Q_assert(map.exported_leafs_bsp29.size() == 1);
}

/*
 * Writes extended texinfo flags to a file so they can be read by the light tool.
 * Used for phong shading and other lighting settings on func_detail.
 */
static void
WriteExtendedTexinfoFlags(void)
{
    bool needwrite = false;
    const int num_texinfo = map.numtexinfo();
    
    for (int i = 0; i < num_texinfo; i++) {
        if (map.mtexinfos.at(i).flags & ~(TEX_SPECIAL | TEX_SKIP | TEX_HINT)) {
            // this texinfo uses some extended flags, write them to a file
            needwrite = true;
            break;
        }
    }

    if (!needwrite)
        return;
    
    // sort by output texinfo number
    std::vector<mtexinfo_t> texinfos_sorted(map.mtexinfos);
    std::sort(texinfos_sorted.begin(), texinfos_sorted.end(), [](const mtexinfo_t &a, const mtexinfo_t &b) {
        return a.outputnum < b.outputnum;
    });
    
    FILE *texinfofile;
    StripExtension(options.szBSPName);
    strcat(options.szBSPName, ".texinfo");
    texinfofile = fopen(options.szBSPName, "wt");
    if (!texinfofile)
        Error("Failed to open %s: %s", options.szBSPName, strerror(errno));
    
    int count = 0;
    for (const auto &tx : texinfos_sorted) {
        if (tx.outputnum == -1)
            continue;
        
        Q_assert(count == tx.outputnum); // check we are outputting them in the proper sequence
        
        fprintf(texinfofile, "%llu\n", static_cast<unsigned long long>(tx.flags));
        count++;
    }
    Q_assert(count == static_cast<int>(map.exported_texinfos.size()));
    
    fclose(texinfofile);
}

/*
==================
FinishBSPFile
==================
*/
void
FinishBSPFile(void)
{
    options.fVerbose = true;
    Message(msgProgress, "WriteBSPFile");

    WriteExtendedTexinfoFlags();
    WriteBSPFile();
    PrintBSPFileSizes();

    options.fVerbose = options.fAllverbose;
}
