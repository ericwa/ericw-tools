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


#include <common/cmdlib.hh>
#include <common/bspfile.hh>

inline void PrintBSPInfo(const bspdata_t &bsp) {
    printf("brushes:\n");
    for (int32_t i = 0; i < bsp.data.q2bsp.numbrushes; i++) {
        printf(" %i: contents: %i, num sides: %i, first side: %i\n", i, bsp.data.q2bsp.dbrushes[i].contents, bsp.data.q2bsp.dbrushes[i].numsides, bsp.data.q2bsp.dbrushes[i].firstside);
    }

    printf("brush sides:\n");
    for (int32_t i = 0; i < bsp.data.q2bsp.numbrushsides; i++) {
        auto &plane = bsp.data.q2bsp.dplanes[bsp.data.q2bsp.dbrushsides[i].planenum];
        printf(" %i: { %i: %f %f %f -> %f }\n", i, plane.type, plane.normal[0], plane.normal[1], plane.normal[2], plane.dist);
    }

    printf("leaves:\n");
    for (int32_t i = 0; i < bsp.data.q2bsp.numleafs; i++) {
        auto &leaf = bsp.data.q2bsp.dleafs[i];

        printf(" %i: contents %i, leafbrushes first %i -> count %i\n", i, leaf.contents, leaf.firstleafbrush, leaf.numleafbrushes);
    }

    printf("nodes:\n");
    for (int32_t i = 0; i < bsp.data.q2bsp.numnodes; i++) {
        auto &node = bsp.data.q2bsp.dnodes[i];
        auto &plane = bsp.data.q2bsp.dplanes[node.planenum];
        printf(" %i: { %i: %f %f %f -> %f }\n", i, plane.type, plane.normal[0], plane.normal[1], plane.normal[2], plane.dist);
    }

    printf("models:\n");
    for (int32_t i = 0; i < bsp.data.q2bsp.nummodels; i++) {
        auto &model = bsp.data.q2bsp.dmodels[i];
        printf(" %i: headnode %i (%f %f %f -> %f %f %f)\n", i, model.headnode, model.mins[0], model.mins[1], model.mins[2], model.maxs[0], model.maxs[1], model.maxs[2]);
    }
}

int
main(int argc, char **argv)
{
    bspdata_t bsp;
    char source[1024];
    int i;

    printf("---- bspinfo / ericw-tools " stringify(ERICWTOOLS_VERSION) " ----\n");
    if (argc == 1) {
        printf("usage: bspinfo bspfile [bspfiles]\n");
        exit(1);
    }

    for (i = 1; i < argc; i++) {
        printf("---------------------\n");
        strcpy(source, argv[i]);
        DefaultExtension(source, ".bsp");
        printf("%s\n", source);

        LoadBSPFile(source, &bsp);
        PrintBSPFileSizes(&bsp);

        PrintBSPInfo(bsp);

        printf("---------------------\n");
    }

    return 0;
}
