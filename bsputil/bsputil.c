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
#include <unistd.h>

#include <common/cmdlib.h>
#include <common/bspfile.h>
#include <common/mathlib.h>

/* FIXME - share header with qbsp, etc. */
typedef struct {
    char identification[4];	// should be WAD2
    int32_t numlumps;
    int32_t infotableofs;
} wadinfo_t;

typedef struct {
    int32_t filepos;
    int32_t disksize;
    int32_t size;			// uncompressed
    char type;
    char compression;
    char pad1, pad2;
    char name[16];		// must be null terminated
} lumpinfo_t;

static void
ExportWad(FILE *wadfile, bsp2_t *bsp)
{
    wadinfo_t header;
    lumpinfo_t lump;
    dtexdata_t texdata;
    miptex_t *miptex;
    int i, j, size, filepos, numvalid;

    texdata = bsp->dtexdata;

    /* Count up the valid lumps */
    numvalid = 0;
    for (i = 0; i < texdata.header->nummiptex; i++)
	if (texdata.header->dataofs[i] >= 0)
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
    for (i = 0; i < texdata.header->nummiptex; i++) {
	if (texdata.header->dataofs[i] < 0)
	    continue;

	miptex = (miptex_t *)(texdata.base + texdata.header->dataofs[i]);

	lump.filepos = filepos;
	lump.size = sizeof(*miptex) + miptex->width * miptex->height / 64 * 85;
	lump.type = 'D';
	lump.disksize = lump.size;
	lump.compression = 0;
	lump.pad1 = lump.pad2 = 0;
	snprintf(lump.name, sizeof(lump.name), "%s", miptex->name);

	filepos += lump.disksize;

	/* Byte-swap lumpinfo and write out */
	lump.filepos = LittleLong(lump.filepos);
	lump.disksize = LittleLong(lump.disksize);
	lump.size = LittleLong(lump.size);
	fwrite(&lump, sizeof(lump), 1, wadfile);
    }
    for (i = 0; i < texdata.header->nummiptex; i++) {
	if (texdata.header->dataofs[i] < 0)
	    continue;
	miptex = (miptex_t *)(texdata.base + texdata.header->dataofs[i]);
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
PrintModelInfo(const bsp2_t *bsp)
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
#define ON_EPSILON 0.01
const vec3_t vec3_origin = { 0, 0, 0 };
static void
CheckBSPFacesPlanar(const bsp2_t *bsp)
{
    int i, j;

    for (i = 0; i < bsp->numfaces; i++) {
	const bsp2_dface_t *face = &bsp->dfaces[i];
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

	    if (dist < -ON_EPSILON || dist > ON_EPSILON)
		printf("WARNING: face %d, point %d off plane by %f\n",
		       (int)(face - bsp->dfaces), j, dist);
	}
    }
}

static void
CheckBSPFile(const bsp2_t *bsp)
{
    int i;

    /* faces */
    for (i = 0; i < bsp->numfaces; i++) {
	const bsp2_dface_t *face = &bsp->dfaces[i];

	/* texinfo bounds check */
	if (face->texinfo < 0)
	    printf("warning: face %d has negative texinfo (%d)\n",
		   i, face->texinfo);
	if (face->texinfo >= bsp->numtexinfo)
	    printf("warning: face %d has texinfo out of range (%d >= %d)\n",
		   i, face->texinfo, bsp->numtexinfo);

	/* planenum bounds check */
	if (face->planenum < 0)
	    printf("warning: face %d has negative planenum (%d)\n",
		   i, face->planenum);
	if (face->planenum >= bsp->numplanes)
	    printf("warning: face %d has planenum out of range (%d >= %d)\n",
		   i, face->planenum, bsp->numplanes);

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
	if (face->firstedge + face->numedges > bsp->numsurfedges)
	    printf("warning: face %d has edges out of range (%d..%d >= %d)\n",
		   i, face->firstedge, face->firstedge + face->numedges - 1,
		   bsp->numsurfedges);
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
    for (i = 0; i < bsp->nummarksurfaces; i++) {
	const uint32_t surfnum = bsp->dmarksurfaces[i];
	if (surfnum >= bsp->numfaces)
	    printf("warning: marksurface %d is out of range (%d >= %d)\n",
		   i, surfnum, bsp->numfaces);
    }

    /* leafs */
    for (i = 0; i < bsp->numleafs; i++) {
	const bsp2_dleaf_t *leaf = &bsp->dleafs[i];
	const uint32_t endmarksurface =
	    leaf->firstmarksurface + leaf->nummarksurfaces;
	if (endmarksurface > bsp->nummarksurfaces)
	    printf("warning: leaf %d has marksurfaces out of range "
		   "(%d..%d >= %d)\n", i, leaf->firstmarksurface,
		   endmarksurface - 1, bsp->nummarksurfaces);
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
    }

    /* TODO: finish range checks, add "unreferenced" checks... */
}

int
main(int argc, char **argv)
{
    bspdata_t bspdata;
    bsp2_t *const bsp = &bspdata.data.bsp2;
    char source[1024];
    FILE *f;
    int i, err;

    printf("---- bsputil / TyrUtils " stringify(TYRUTILS_VERSION) " ----\n");
    if (argc == 1) {
	printf("usage: bsputil [--extract-entities] [--extract-textures] "
	       "[--check] bspfile\n");
	exit(1);
    }

    strcpy(source, argv[argc - 1]);
    DefaultExtension(source, ".bsp");
    printf("---------------------\n");
    printf("%s\n", source);

    LoadBSPFile(source, &bspdata);

    if (bspdata.version != BSP2VERSION)
	ConvertBSPFormat(BSP2VERSION, &bspdata);

    for (i = 0; i < argc - 1; i++) {
	if (!strcmp(argv[i], "--extract-entities")) {
	    StripExtension(source);
	    DefaultExtension(source, ".ent");
	    printf("-> writing %s... ", source);

	    f = fopen(source, "w");
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
	}
    }

    printf("---------------------\n");

    return 0;
}
