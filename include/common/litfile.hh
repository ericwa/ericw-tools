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

#include <common/qvec.hh>
#include <common/fs.hh>

#include <array>
#include <iostream>
#include <vector>

constexpr int32_t LIT_VERSION = 1;
constexpr int32_t LIT_VERSION_E5BGR9 = (0x00010000 | LIT_VERSION);

struct litheader_t
{
    struct v1_t
    {
        std::array<char, 4> ident = {'Q', 'L', 'I', 'T'};
        int version;

        // serialize for streams
        void stream_write(std::ostream &s) const;
        void stream_read(std::istream &s);
    };
    struct v2_t
    {
        int numsurfs;
        int lmsamples;

        // serialize for streams
        void stream_write(std::ostream &s) const;
        void stream_read(std::istream &s);
    };

    v1_t v1;
    v2_t v2;
};

uint32_t HDR_PackE5BRG9(qvec3f rgb);
qvec3f HDR_UnpackE5BRG9(uint32_t packed);

std::vector<uint8_t> LoadLitFile(const fs::path &path);
