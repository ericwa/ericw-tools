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

#include <light/litfile.h>
#include <light/light.h>
#include <common/bspfile.h>
#include <common/cmdlib.h>

void
WriteLitFile(const bsp2_t *bsp, const char *filename, int version)
{
    FILE *litfile;
    char litname[1024];
    litheader_t header;

    snprintf(litname, sizeof(litname) - 4, "%s", filename);
    StripExtension(litname);
    DefaultExtension(litname, ".lit");

    header.ident[0] = 'Q';
    header.ident[1] = 'L';
    header.ident[2] = 'I';
    header.ident[3] = 'T';
    header.version = LittleLong(version);

    litfile = SafeOpenWrite(litname);
    SafeWrite(litfile, &header, sizeof(header));
    SafeWrite(litfile, lit_filebase, bsp->lightdatasize * 3);
    fclose(litfile);
}
