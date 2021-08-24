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

/*
=============
LoadBSPFile
=============
*/
void
LoadBSPFile(void)
{
}

void BSPX_AddLump(const char *xname, const void *xdata, size_t xsize)
{
}

template <class C>
static void CopyVector(const std::vector<C>& vec, int* elementCountOut, C** arrayCopyOut)
{
    const size_t numBytes = sizeof(C) * vec.size();
    void* data = (void*)malloc(numBytes);
    memcpy(data, vec.data(), numBytes);

    *elementCountOut = vec.size();
    *arrayCopyOut = (C*)data;
}

static void CopyString(const std::string& string, bool addNullTermination, int* elementCountOut, void** arrayCopyOut)
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
void
WriteBSPFile(void)
{
    bspdata_t bspdata{};
    
    bspdata.version = &bspver_generic;
    bspdata.hullcount = MAX_MAP_HULLS_Q1;

    CopyVector(map.exported_planes, &bspdata.data.mbsp.numplanes, &bspdata.data.mbsp.dplanes);
    CopyVector(map.exported_leafs_bsp29, &bspdata.data.mbsp.numleafs, &bspdata.data.mbsp.dleafs);
    CopyVector(map.exported_vertexes, &bspdata.data.mbsp.numvertexes, &bspdata.data.mbsp.dvertexes);
    CopyVector(map.exported_nodes_bsp29, &bspdata.data.mbsp.numnodes, &bspdata.data.mbsp.dnodes);
    CopyVector(map.exported_texinfos, &bspdata.data.mbsp.numtexinfo, &bspdata.data.mbsp.texinfo);
    CopyVector(map.exported_faces, &bspdata.data.mbsp.numfaces, &bspdata.data.mbsp.dfaces);
    CopyVector(map.exported_clipnodes, &bspdata.data.mbsp.numclipnodes, &bspdata.data.mbsp.dclipnodes);
    CopyVector(map.exported_marksurfaces, &bspdata.data.mbsp.numleaffaces, &bspdata.data.mbsp.dleaffaces);
    CopyVector(map.exported_surfedges, &bspdata.data.mbsp.numsurfedges, &bspdata.data.mbsp.dsurfedges);
    CopyVector(map.exported_edges, &bspdata.data.mbsp.numedges, &bspdata.data.mbsp.dedges);
    CopyVector(map.exported_models, &bspdata.data.mbsp.nummodels, &bspdata.data.mbsp.dmodels);

    CopyString(map.exported_entities, true, &bspdata.data.mbsp.entdatasize, (void**)&bspdata.data.mbsp.dentdata);
    CopyString(map.exported_texdata, false, &bspdata.data.mbsp.texdatasize, (void**)&bspdata.data.mbsp.dtexdata);

    // TODO: pass bspx lumps to generic bsp code so they are written

    //GenLump("LMSHIFT", BSPX_LMSHIFT, 1);

    ConvertBSPFormat(&bspdata, &bspver_q1); // assume q1 for now

    StripExtension(options.szBSPName);
    strcat(options.szBSPName, ".bsp");

    WriteBSPFile(options.szBSPName, &bspdata);
    logprint("Wrote %s\n", options.szBSPName);

    PrintBSPFileSizes(&bspdata);
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
}
