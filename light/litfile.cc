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

#include <fstream>

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

    header.v1.version = version;
    header.v2.numsurfs = bsp->dfaces.size();
    header.v2.lmsamples = bsp->dlightdata.size();

    logging::print("Writing {}\n", litname);
    std::ofstream litfile(litname, std::ios_base::out | std::ios_base::binary);
    litfile <= header.v1;
    if (version == 2) {
        unsigned int i, j;
        litfile <= header.v2;
        for (i = 0; i < bsp->dfaces.size(); i++) {
            litfile <= facesup[i].lightofs;
            for (int j = 0; j < 4; j++) {
                litfile <= facesup[i].styles[j];
            }
            for (int j = 0; j < 2; j++) {
                litfile <= facesup[i].extent[j];
            }
            j = 0;
            while (nth_bit(j) < facesup[i].lmscale)
                j++;
            litfile <= (uint8_t) j;
        }
        litfile.write((const char *) lit_filebase.data(), bsp->dlightdata.size() * 3);
        litfile.write((const char *) lux_filebase.data(), bsp->dlightdata.size() * 3);
    }
    else
        litfile.write((const char *) lit_filebase.data(), bsp->dlightdata.size() * 3);
}

#include <fstream>

void WriteLuxFile(const mbsp_t *bsp, const fs::path &filename, int version)
{
    litheader_t header;

    fs::path luxname = filename;
    luxname.replace_extension("lux");

    header.v1.version = version;

    std::ofstream luxfile(luxname, std::ios_base::out | std::ios_base::binary);
    luxfile <= header.v1;
    luxfile.write((const char *) lux_filebase.data(), bsp->dlightdata.size() * 3);
}
