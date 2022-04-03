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
#include <common/fs.hh>

void WriteLitFile(const mbsp_t *bsp, facesup_t *facesup, const fs::path &filename, int version)
{
    litheader_t header;

    fs::path litname = filename;
    litname.replace_extension("lit");

    header.v1.ident[0] = 'Q';
    header.v1.ident[1] = 'L';
    header.v1.ident[2] = 'I';
    header.v1.ident[3] = 'T';
    header.v1.version = LittleLong(version);
    header.v2.numsurfs = LittleLong(bsp->dfaces.size());
    header.v2.lmsamples = LittleLong(bsp->dlightdata.size());

    logging::print("Writing {}\n", litname);
    auto litfile = SafeOpenWrite(litname);
    SafeWrite(litfile, &header.v1, sizeof(header.v1));
    if (version == 2) {
        unsigned int i, j;
        unsigned int *offsets = new unsigned int[bsp->dfaces.size()];
        unsigned short *extents = new unsigned short[2 * bsp->dfaces.size()];
        unsigned char *styles = new unsigned char[4 * bsp->dfaces.size()];
        unsigned char *shifts = new unsigned char[bsp->dfaces.size()];
        for (i = 0; i < bsp->dfaces.size(); i++) {
            offsets[i] = LittleLong(facesup[i].lightofs);
            styles[i * 4 + 0] = LittleShort(facesup[i].styles[0]);
            styles[i * 4 + 1] = LittleShort(facesup[i].styles[1]);
            styles[i * 4 + 2] = LittleShort(facesup[i].styles[2]);
            styles[i * 4 + 3] = LittleShort(facesup[i].styles[3]);
            extents[i * 2 + 0] = LittleShort(facesup[i].extent[0]);
            extents[i * 2 + 1] = LittleShort(facesup[i].extent[1]);
            j = 0;
            while ((1u << j) < facesup[i].lmscale)
                j++;
            shifts[i] = j;
        }
        SafeWrite(litfile, &header.v2, sizeof(header.v2));
        SafeWrite(litfile, offsets, bsp->dfaces.size() * sizeof(*offsets));
        SafeWrite(litfile, extents, 2 * bsp->dfaces.size() * sizeof(*extents));
        SafeWrite(litfile, styles, 4 * bsp->dfaces.size() * sizeof(*styles));
        SafeWrite(litfile, shifts, bsp->dfaces.size() * sizeof(*shifts));
        SafeWrite(litfile, lit_filebase, bsp->dlightdata.size() * 3);
        SafeWrite(litfile, lux_filebase, bsp->dlightdata.size() * 3);
    } else
        SafeWrite(litfile, lit_filebase, bsp->dlightdata.size() * 3);
}

void WriteLuxFile(const mbsp_t *bsp, const fs::path &filename, int version)
{
    litheader_t header;

    fs::path luxname = filename;
    luxname.replace_extension("lux");

    header.v1.ident[0] = 'Q';
    header.v1.ident[1] = 'L';
    header.v1.ident[2] = 'I';
    header.v1.ident[3] = 'T';
    header.v1.version = LittleLong(version);

    auto luxfile = SafeOpenWrite(luxname);
    SafeWrite(luxfile, &header.v1, sizeof(header.v1));
    SafeWrite(luxfile, lux_filebase, bsp->dlightdata.size() * 3);
}
