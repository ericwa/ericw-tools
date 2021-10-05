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
AssertVanillaContentType(const contentflags_t &flags)
{
    if (!flags.is_valid(options.target_game, false)) {
        Error("Internal error: Tried to save invalid contents type %s\n", GetContentsName(flags));
    }
}

static contentflags_t
RemapContentsForExport(const contentflags_t &content)
{
    if (content.extended & CFLAGS_DETAIL_FENCE) {
        /*
         * This is for func_detail_wall.. we want to write a solid leaf that has faces,
         * because it may be possible to see inside (fence textures).
         *
         * Normally solid leafs are not written and just referenced as leaf 0.
         */
        return options.target_game->create_solid_contents();
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

    if (plane->outputplanenum != PLANENUM_LEAF)
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
    if (src->outputnum.has_value())
        return src->outputnum.value();
    
    // this will be the index of the exported texinfo in the BSP lump
    const int i = static_cast<int>(map.exported_texinfos.size());
    
    map.exported_texinfos.push_back({});
    gtexinfo_t* dest = &map.exported_texinfos.back();

    // make sure we don't write any non-native flags.
    // e.g. Quake only accepts 0 or TEX_SPECIAL.
    dest->flags = options.target_game->surf_remap_for_export(src->flags);
    // TODO: warn if dest->flags.native != src->flags.native

    dest->miptex = src->miptex;
    for (int j=0; j<2; j++) {
        for (int k=0; k<4; k++) {
            dest->vecs[j][k] = src->vecs[j][k];
        }
    }

    strcpy(dest->texture, map.texinfoTextureName(texinfonum).c_str());
    //dest->flags = map.miptex[src->miptex].flags;
    dest->value = map.miptex[src->miptex].value;

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
ExportClipNodes(mapentity_t *entity, node_t *node)
{
    bsp2_dclipnode_t *clipnode;
    face_t *face, *next;

    // FIXME: free more stuff?
    if (node->planenum == PLANENUM_LEAF) {
        int contents = node->contents.native;
        free(node);
        return contents;
    }

    /* emit a clipnode */
    const int nodenum = static_cast<int>(map.exported_clipnodes.size());
    map.exported_clipnodes.push_back({});

    const int child0 = ExportClipNodes(entity, node->children[0]);
    const int child1 = ExportClipNodes(entity, node->children[1]);

    // Careful not to modify the vector while using this clipnode pointer
    clipnode = &map.exported_clipnodes.at(nodenum);
    clipnode->planenum = ExportMapPlane(node->planenum);
    clipnode->children[0] = child0;
    clipnode->children[1] = child1;

    for (face = node->faces; face; face = next) {
        next = face->next;
        memset(face, 0, sizeof(face_t));
        free(face);
    }
    free(node);

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

    model->headnode[hullnum] = ExportClipNodes(entity, nodes);
}

//===========================================================================

/*
==================
ExportLeaf
==================
*/
static void
ExportLeaf(mapentity_t *entity, node_t *node)
{
    map.exported_leafs.push_back({});
    mleaf_t *dleaf = &map.exported_leafs.back();

    const contentflags_t remapped = RemapContentsForExport(node->contents);
    AssertVanillaContentType(remapped);
    dleaf->contents = remapped.native;

    /*
     * write bounding box info
     */
    for (int32_t i = 0; i < 3; ++i) {
        dleaf->mins[i] = floor(node->mins[i]);
        dleaf->maxs[i] = ceil(node->maxs[i]);
    }

    dleaf->visofs = -1; // no vis info yet

    // write the marksurfaces
    dleaf->firstmarksurface = static_cast<int>(map.exported_marksurfaces.size());

    for (face_t **markfaces = node->markfaces; *markfaces; markfaces++) {
        face_t *face = *markfaces;
        if (map.mtexinfos.at(face->texinfo).flags.extended & TEX_EXFLAG_SKIP)
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
    dleaf->area = 1;
    dleaf->cluster = node->viscluster;
    dleaf->firstleafbrush = node->firstleafbrush;
    dleaf->numleafbrushes = node->numleafbrushes;
}

/*
==================
ExportDrawNodes
==================
*/
static void
ExportDrawNodes(mapentity_t *entity, node_t *node)
{
    bsp2_dnode_t *dnode;
    int i;

    const size_t ourNodeIndex = map.exported_nodes.size();
    map.exported_nodes.push_back({});

    dnode = &map.exported_nodes[ourNodeIndex];

    // VectorCopy doesn't work since dest are shorts
    for (int32_t i = 0; i < 3; ++i) {
        dnode->mins[i] = floor(node->mins[i]);
        dnode->maxs[i] = ceil(node->maxs[i]);
    }

    dnode->planenum = ExportMapPlane(node->planenum);
    dnode->firstface = node->firstface;
    dnode->numfaces = node->numfaces;

    // recursively output the other nodes
    for (i = 0; i < 2; i++) {
        if (node->children[i]->planenum == PLANENUM_LEAF) {
            // In Q2, all leaves must have their own ID even if they share solidity.
            // (probably for collision purposes? makes sense given they store leafbrushes)
            if (options.target_game->id != GAME_QUAKE_II && node->children[i]->contents.is_solid(options.target_game))
                dnode->children[i] = -1;
            else {
                int nextLeafIndex = static_cast<int>(map.exported_leafs.size());
                const int childnum = -(nextLeafIndex + 1);
                dnode->children[i] = childnum;
                ExportLeaf(entity, node->children[i]);
            }
        } else {
            const int childnum = static_cast<int>(map.exported_nodes.size());
            dnode->children[i] = childnum;
            ExportDrawNodes(entity, node->children[i]);

            // Important: our dnode pointer may be invalid after the recursive call, if the vector got resized.
            // So re-set the pointer.
            dnode = &map.exported_nodes[ourNodeIndex];
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
    dmodel->headnode[0] = static_cast<int>(map.exported_nodes.size());
    dmodel->firstface = firstface;
    dmodel->numfaces = static_cast<int>(map.exported_faces.size()) - firstface;

    const size_t mapleafsAtStart = map.exported_leafs.size();

    if (headnode->planenum == PLANENUM_LEAF)
        ExportLeaf(entity, headnode);
    else
        ExportDrawNodes(entity, headnode);

    // count how many leafs were exported by the above calls
    dmodel->visleafs = static_cast<int>(map.exported_leafs.size() - mapleafsAtStart);

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
    map.exported_leafs.push_back({});
    map.exported_leafs.back().contents = options.target_game->create_solid_contents().native;
    Q_assert(map.exported_leafs.size() == 1);
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
        if (map.mtexinfos.at(i).flags.needs_write()) {
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

    extended_flags_header_t header;
    header.num_texinfo = map.exported_texinfos.size();
    header.surfflags_size = sizeof(surfflags_t);

    fwrite(&header, 1, sizeof(header), texinfofile);
    
    int count = 0;
    for (const auto &tx : texinfos_sorted) {
        if (!tx.outputnum.has_value())
            continue;
        
        Q_assert(count == tx.outputnum.value()); // check we are outputting them in the proper sequence
      
        fwrite(&tx.flags, 1, sizeof(tx.flags), texinfofile);
        count++;
    }
    Q_assert(count == static_cast<int>(map.exported_texinfos.size()));
    
    fclose(texinfofile);
}

template <class C>
static void
CopyVector(const std::vector<C>& vec, int* elementCountOut, C** arrayCopyOut)
{
    const size_t numBytes = sizeof(C) * vec.size();
    void* data = (void*)malloc(numBytes);
    memcpy(data, vec.data(), numBytes);

    *elementCountOut = vec.size();
    *arrayCopyOut = (C*)data;
}

static void
CopyString(const std::string& string, bool addNullTermination, int* elementCountOut, void** arrayCopyOut)
{
    const size_t numBytes = addNullTermination ? string.size() + 1 : string.size();
    void* data = malloc(numBytes);
    memcpy(data, string.data(), numBytes); // std::string::data() has null termination, so it's safe to copy it

    *elementCountOut = numBytes;
    *arrayCopyOut = data;
}

/*
=============
WriteBSPFile
=============
*/
static void
WriteBSPFile()
{
    bspdata_t bspdata{};
    
    bspdata.version = &bspver_generic;

    CopyVector(map.exported_planes, &bspdata.data.mbsp.numplanes, &bspdata.data.mbsp.dplanes);
    CopyVector(map.exported_leafs, &bspdata.data.mbsp.numleafs, &bspdata.data.mbsp.dleafs);
    CopyVector(map.exported_vertexes, &bspdata.data.mbsp.numvertexes, &bspdata.data.mbsp.dvertexes);
    CopyVector(map.exported_nodes, &bspdata.data.mbsp.numnodes, &bspdata.data.mbsp.dnodes);
    CopyVector(map.exported_texinfos, &bspdata.data.mbsp.numtexinfo, &bspdata.data.mbsp.texinfo);
    CopyVector(map.exported_faces, &bspdata.data.mbsp.numfaces, &bspdata.data.mbsp.dfaces);
    CopyVector(map.exported_clipnodes, &bspdata.data.mbsp.numclipnodes, &bspdata.data.mbsp.dclipnodes);
    CopyVector(map.exported_marksurfaces, &bspdata.data.mbsp.numleaffaces, &bspdata.data.mbsp.dleaffaces);
    CopyVector(map.exported_surfedges, &bspdata.data.mbsp.numsurfedges, &bspdata.data.mbsp.dsurfedges);
    CopyVector(map.exported_edges, &bspdata.data.mbsp.numedges, &bspdata.data.mbsp.dedges);
    CopyVector(map.exported_models, &bspdata.data.mbsp.nummodels, &bspdata.data.mbsp.dmodels);
    CopyVector(map.exported_leafbrushes, &bspdata.data.mbsp.numleafbrushes, &bspdata.data.mbsp.dleafbrushes);
    CopyVector(map.exported_brushsides, &bspdata.data.mbsp.numbrushsides, &bspdata.data.mbsp.dbrushsides);
    CopyVector(map.exported_brushes, &bspdata.data.mbsp.numbrushes, &bspdata.data.mbsp.dbrushes);

    CopyString(map.exported_entities, true, &bspdata.data.mbsp.entdatasize, (void**)&bspdata.data.mbsp.dentdata);
    CopyString(map.exported_texdata, false, &bspdata.data.mbsp.texdatasize, (void**)&bspdata.data.mbsp.dtexdata);

    if (map.needslmshifts) {
        BSPX_AddLump(&bspdata, "LMSHIFT", map.exported_lmshifts.data(), map.exported_lmshifts.size());
    }
    if (!map.exported_bspxbrushes.empty()) {
        BSPX_AddLump(&bspdata, "BRUSHLIST", map.exported_bspxbrushes.data(), map.exported_bspxbrushes.size());
    }

    // FIXME: temp
    bspdata.data.mbsp.numareaportals = 1;
    bspdata.data.mbsp.dareaportals = (dareaportal_t *) calloc(bspdata.data.mbsp.numareaportals, sizeof(dareaportal_t));

    bspdata.data.mbsp.numareas = 2;
    bspdata.data.mbsp.dareas = (darea_t *) calloc(bspdata.data.mbsp.numareas, sizeof(darea_t));
    bspdata.data.mbsp.dareas[1].firstareaportal = 1;
    if (!ConvertBSPFormat(&bspdata, options.target_version)) {
        const bspversion_t* highLimitsFormat = nullptr;

        if (options.target_version == &bspver_q1) {
            highLimitsFormat = &bspver_bsp2;
        } else if (options.target_version == &bspver_h2) {
            highLimitsFormat = &bspver_h2bsp2;
        } else if (options.target_version == &bspver_q2) {
            highLimitsFormat = &bspver_qbism;
        } else {
            Error("No high limits version of %s available", options.target_version->name);
        }

        logprint("NOTE: limits exceeded for %s - switching to %s\n", options.target_version->name, highLimitsFormat->name);

        Q_assert(ConvertBSPFormat(&bspdata, highLimitsFormat));
    }

    StripExtension(options.szBSPName);
    strcat(options.szBSPName, ".bsp");

    WriteBSPFile(options.szBSPName, &bspdata);
    logprint("Wrote %s\n", options.szBSPName);

    PrintBSPFileSizes(&bspdata);
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

    options.fVerbose = options.fAllverbose;
}

/*
==================
UpdateBSPFileEntitiesLump
==================
*/
void
UpdateBSPFileEntitiesLump()
{
    bspdata_t bspdata;
    StripExtension(options.szBSPName);
    DefaultExtension(options.szBSPName, ".bsp");

    // load the .bsp
    LoadBSPFile(options.szBSPName, &bspdata);
    ConvertBSPFormat(&bspdata, &bspver_generic);

    // replace the existing entities lump with map.exported_entities
    CopyString(map.exported_entities, true, &bspdata.data.mbsp.entdatasize, (void**)&bspdata.data.mbsp.dentdata);

    // write the .bsp back to disk
    ConvertBSPFormat(&bspdata, bspdata.loadversion);
    WriteBSPFile(options.szBSPName, &bspdata);

    logprint("Wrote %s\n", options.szBSPName);
}
