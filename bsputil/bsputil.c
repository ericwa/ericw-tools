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

void
ExportWad(FILE *f)
{
    wadinfo_t header;
    dmiptexlump_t *m;
    miptex_t *mt;
    int i, j, datalen;
    lumpinfo_t l;

    m = (dmiptexlump_t *)dtexdata;
    memcpy(&header.identification, "WAD2", 4);
    header.numlumps = m->nummiptex;
    header.infotableofs = sizeof(wadinfo_t);

    /* Byte-swap header and write out */
    header.numlumps = LittleLong(header.numlumps);
    header.infotableofs = LittleLong(header.infotableofs);
    fwrite(&header, sizeof(wadinfo_t), 1, f);

    datalen = sizeof(wadinfo_t) + sizeof(lumpinfo_t) * m->nummiptex;

    for (i = 0; i < m->nummiptex; i++) {
	mt = (miptex_t *)((byte *)m + m->dataofs[i]);

	l.filepos = datalen;
	l.disksize = sizeof(miptex_t) + mt->width * mt->height / 64 * 85;
	l.size = l.disksize;
	l.type = 'D';
	l.compression = 0;
	l.pad1 = l.pad2 = 0;
	memcpy(l.name, mt->name, 15);
	l.name[15] = 0;

	datalen += l.disksize;

	/* Byte-swap lumpinfo and write out */
	l.filepos = LittleLong(l.filepos);
	l.disksize = LittleLong(l.disksize);
	l.size = LittleLong(l.size);
	fwrite(&l, sizeof(lumpinfo_t), 1, f);
    }
    for (i = 0; i < m->nummiptex; i++) {
	mt = (miptex_t *)((byte *)m + m->dataofs[i]);
	datalen = sizeof(miptex_t) + mt->width * mt->height / 64 * 85;

	/* Byte-swap miptex info and write out */
	mt->width = LittleLong(mt->width);
	mt->height = LittleLong(mt->height);
	for (j = 0; j < MIPLEVELS; j++)
	    mt->offsets[j] = LittleLong(mt->offsets[j]);
	fwrite(mt, datalen, 1, f);
    }
}

int
main(int argc, char **argv)
{
    int i;
    int err;
    char source[1024];
    FILE *f;

    if (argc == 1)
	Error("usage: bsputil [--extract-entities] [--extract-textures] "
	      "bspfile");

    strcpy(source, argv[argc - 1]);
    DefaultExtension(source, ".bsp");
    printf("---------------------\n");
    printf("%s\n", source);

    LoadBSPFile(source);

    for (i = 0; i < argc - 1; i++) {
	if (!strcmp(argv[i], "--extract-entities")) {
	    StripExtension(source);
	    DefaultExtension(source, ".ent");
	    printf("-> writing %s... ", source);

	    f = fopen(source, "w");
	    if (!f)
		Error("couldn't open %s for writing\n", source);

	    err = fwrite(dentdata, sizeof(char), entdatasize - 1, f);
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

	    ExportWad(f);

	    err = fclose(f);
	    if (err)
		Error("%s", strerror(errno));

	    printf("done.\n");
	}
    }

    printf("---------------------\n");

    return 0;
}
