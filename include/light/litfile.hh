/*  Copyright (C) 2002 Kevin Shanahan

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

#pragma once

#include <common/bspfile.hh>

#define LIT_VERSION 1

struct litheader_t
{
    struct
    {
        char ident[4];
        int version;
    } v1;
    struct
    {
        int numsurfs;
        int lmsamples;
    } v2;
};

/* internal representation for bspx/lit2 */
struct facesup_t
{
    float lmscale;
    uint8_t styles[MAXLIGHTMAPS]; /* scaled styles */
    int32_t lightofs; /* scaled lighting */
    unsigned short extent[2];
};

void WriteLitFile(const mbsp_t *bsp, facesup_t *facesup, const char *filename, int version);
void WriteLuxFile(const mbsp_t *bsp, const char *filename, int version);
