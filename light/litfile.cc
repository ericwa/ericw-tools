/*  Copyright (C) 2002-2006 Kevin Shanahan

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

#include <light/litfile.hh>
#include <light/light.hh>

#include <common/bspfile.hh>
#include <common/cmdlib.hh>

void
WriteLitFile(const mbsp_t *bsp, facesup_t *facesup, const char *filename, int version)
{
    FILE *litfile;
    char litname[1024];
    litheader_t header;

    q_snprintf(litname, sizeof(litname) - 4, "%s", filename);
    StripExtension(litname);
    DefaultExtension(litname, ".lit");

    header.v1.ident[0] = 'Q';
    header.v1.ident[1] = 'L';
    header.v1.ident[2] = 'I';
    header.v1.ident[3] = 'T';
    header.v1.version = LittleLong(version);
    header.v2.numsurfs = LittleLong(bsp->numfaces);
    header.v2.lmsamples = LittleLong(bsp->lightdatasize);

    logprint("Writing %s\n", litname);
    litfile = SafeOpenWrite(litname);
    SafeWrite(litfile, &header.v1, sizeof(header.v1));
    if (version == 2)
    {
        unsigned int i, j;
        unsigned int *offsets = (unsigned int *) malloc(bsp->numfaces * sizeof(*offsets));
        unsigned short *extents = (unsigned short *) malloc(2*bsp->numfaces * sizeof(*extents));
        unsigned char *styles = (unsigned char *) malloc(4*bsp->numfaces * sizeof(*styles));
        unsigned char *shifts = (unsigned char *) malloc(bsp->numfaces * sizeof(*shifts));
        for (i = 0; i < bsp->numfaces; i++)
        {
            offsets[i] = LittleLong(facesup[i].lightofs);
            styles[i*4+0] = LittleShort(facesup[i].styles[0]);
            styles[i*4+1] = LittleShort(facesup[i].styles[1]);
            styles[i*4+2] = LittleShort(facesup[i].styles[2]);
            styles[i*4+3] = LittleShort(facesup[i].styles[3]);
            extents[i*2+0] = LittleShort(facesup[i].extent[0]);
            extents[i*2+1] = LittleShort(facesup[i].extent[1]);
            j = 0;
            while ((1u<<j) < facesup[i].lmscale)
                j++;
            shifts[i] = j;
        }
        SafeWrite(litfile, &header.v2, sizeof(header.v2));
        SafeWrite(litfile, offsets, bsp->numfaces * sizeof(*offsets));
        SafeWrite(litfile, extents, 2*bsp->numfaces * sizeof(*extents));
        SafeWrite(litfile, styles, 4*bsp->numfaces * sizeof(*styles));
        SafeWrite(litfile, shifts, bsp->numfaces * sizeof(*shifts));
        SafeWrite(litfile, lit_filebase, bsp->lightdatasize * 3);
        SafeWrite(litfile, lux_filebase, bsp->lightdatasize * 3);
    }
    else
        SafeWrite(litfile, lit_filebase, bsp->lightdatasize * 3);
    fclose(litfile);
}

void
WriteLuxFile(const mbsp_t *bsp, const char *filename, int version)
{
    FILE *luxfile;
    char luxname[1024];
    litheader_t header;

    q_snprintf(luxname, sizeof(luxname) - 4, "%s", filename);
    StripExtension(luxname);
    DefaultExtension(luxname, ".lux");

    header.v1.ident[0] = 'Q';
    header.v1.ident[1] = 'L';
    header.v1.ident[2] = 'I';
    header.v1.ident[3] = 'T';
    header.v1.version = LittleLong(version);

    luxfile = SafeOpenWrite(luxname);
    SafeWrite(luxfile, &header.v1, sizeof(header.v1));
    SafeWrite(luxfile, lux_filebase, bsp->lightdatasize * 3);
    fclose(luxfile);
}
