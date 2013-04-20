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

#include <unistd.h>

#include <common/cmdlib.h>
#include <common/bspfile.h>

/* FIXME - share header with qbsp, etc. */
typedef struct {
    char identification[4];	// should be WAD2
    int numlumps;
    int infotableofs;
} wadinfo_t;

typedef struct {
    int filepos;
    int disksize;
    int size;			// uncompressed
    char type;
    char compression;
    char pad1, pad2;
    char name[16];		// must be null terminated
} lumpinfo_t;

static void
ExportWad(FILE *wadfile, bspdata_t *bsp)
{
    wadinfo_t header;
    lumpinfo_t lump;
    dtexdata_t texdata;
    miptex_t *miptex;
    int i, j, size, filepos;

    texdata = bsp->dtexdata;
    memcpy(&header.identification, "WAD2", 4);
    header.numlumps = texdata.header->nummiptex;
    header.infotableofs = sizeof(header);

    /* Byte-swap header and write out */
    header.numlumps = LittleLong(header.numlumps);
    header.infotableofs = LittleLong(header.infotableofs);
    fwrite(&header, sizeof(header), 1, wadfile);

    /* miptex data will follow the lump headers */
    filepos = sizeof(header) + sizeof(lump) * texdata.header->nummiptex;
    for (i = 0; i < texdata.header->nummiptex; i++) {
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

int
main(int argc, char **argv)
{
    bspdata_t bsp;
    char source[1024];
    FILE *f;
    int i, err;

    printf("---- bsputil / TyrUtils " stringify(TYRUTILS_VERSION) " ----\n");
    if (argc == 1) {
	printf("usage: bsputil [--extract-entities] [--extract-textures] "
	       "bspfile");
	exit(1);
    }

    strcpy(source, argv[argc - 1]);
    DefaultExtension(source, ".bsp");
    printf("---------------------\n");
    printf("%s\n", source);

    LoadBSPFile(source, &bsp);

    for (i = 0; i < argc - 1; i++) {
	if (!strcmp(argv[i], "--extract-entities")) {
	    StripExtension(source);
	    DefaultExtension(source, ".ent");
	    printf("-> writing %s... ", source);

	    f = fopen(source, "w");
	    if (!f)
		Error("couldn't open %s for writing\n", source);

	    err = fwrite(bsp.dentdata, sizeof(char), bsp.entdatasize - 1, f);
	    if (err != entdatasize - 1)
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

	    ExportWad(f, &bsp);

	    err = fclose(f);
	    if (err)
		Error("%s", strerror(errno));

	    printf("done.\n");
	}
    }

    printf("---------------------\n");

    return 0;
}
