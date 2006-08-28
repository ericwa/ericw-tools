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
WriteLitFile(const char *filename, int version)
{
    FILE *l;
    char f[1024];
    litheader_t h;

    strncpy(f, filename, 1019);	/* 1024 - space for extension - '\0' */
    f[1023] = '\0';
    StripExtension(f);
    DefaultExtension(f, ".lit");

    h.ident[0] = 'Q';
    h.ident[1] = 'L';
    h.ident[2] = 'I';
    h.ident[3] = 'T';
    h.version = LittleLong(version);

    l = SafeOpenWrite(f);
    SafeWrite(l, &h, sizeof(litheader_t));
    SafeWrite(l, lit_filebase, lightdatasize * 3);
    fclose(l);
}
