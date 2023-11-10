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

#include <common/bspfile.hh>
#include <common/cmdlib.hh>

// dmodelh2_t

void dmodelh2_t::stream_write(std::ostream &s) const
{
    s <= std::tie(mins, maxs, origin, headnode, visleafs, firstface, numfaces);
}

void dmodelh2_t::stream_read(std::istream &s)
{
    s >= std::tie(mins, maxs, origin, headnode, visleafs, firstface, numfaces);
}

// mvis_t

size_t mvis_t::header_offset() const
{
    return sizeof(int32_t) + (sizeof(int32_t) * bit_offsets.size() * 2);
}

void mvis_t::set_bit_offset(vistype_t type, size_t cluster, size_t offset)
{
    bit_offsets[cluster][type] = offset + header_offset();
}

int32_t mvis_t::get_bit_offset(vistype_t type, size_t cluster) const
{
    return bit_offsets[cluster][type] - header_offset();
}

void mvis_t::resize(size_t numclusters)
{
    bit_offsets.resize(numclusters);
}

void mvis_t::stream_read(std::istream &stream, const lump_t &lump)
{
    int32_t numclusters;

    stream >= numclusters;

    resize(numclusters);

    // read cluster -> offset tables
    for (auto &bit_offset : bit_offsets)
        stream >= bit_offset;

    // pull in final bit set
    auto remaining = lump.filelen - (static_cast<int32_t>(stream.tellg()) - lump.fileofs);
    bits.resize(remaining);
    stream.read(reinterpret_cast<char *>(bits.data()), remaining);
}

void mvis_t::stream_write(std::ostream &stream) const
{
    // no vis data
    if (!bit_offsets.size()) {
        return;
    }

    stream <= static_cast<int32_t>(bit_offsets.size());

    // write cluster -> offset tables
    for (auto &bit_offset : bit_offsets)
        stream <= bit_offset;

    // write bitset
    stream.write(reinterpret_cast<const char *>(bits.data()), bits.size());
}

// dmiptex_t

void dmiptex_t::stream_write(std::ostream &s) const
{
    s <= std::tie(name, width, height, offsets);
}

void dmiptex_t::stream_read(std::istream &s)
{
    s >= std::tie(name, width, height, offsets);
}

// miptex_t

size_t miptex_t::stream_size() const
{
    return data.size();
}

void miptex_t::stream_read(std::istream &stream, size_t len)
{
    data.resize(len);
    stream.read(reinterpret_cast<char *>(data.data()), len);

    imemstream miptex_stream(data.data(), len);

    dmiptex_t dtex;
    miptex_stream >= dtex;

    name = dtex.name.data();
    width = dtex.width;
    height = dtex.height;
    offsets = dtex.offsets;
}

void miptex_t::stream_write(std::ostream &stream) const
{
    stream.write(reinterpret_cast<const char *>(data.data()), data.size());
}

// dmiptexlump_t

void dmiptexlump_t::stream_read(std::istream &stream, const lump_t &lump)
{
    int32_t nummiptex;
    stream >= nummiptex;

    // load in all of the offsets, we need them
    // to calculate individual data sizes
    std::vector<int32_t> offsets(nummiptex);

    for (size_t i = 0; i < nummiptex; i++) {
        stream >= offsets[i];
    }

    for (size_t i = 0; i < nummiptex; i++) {
        miptex_t &tex = textures.emplace_back();

        int32_t offset = offsets[i];

        // dummy texture?
        if (offset < 0) {
            tex.null_texture = true;
            continue;
        }

        // move to miptex position (technically required
        // because there might be dummy data between the offsets
        // and the mip textures themselves...)
        stream.seekg(lump.fileofs + offset);

        // calculate the length of the data used for the individual miptex.
        int32_t next_offset = -1;

        // scan forward (skipping -1's) to find the next valid offset
        for (int j = i + 1; j < nummiptex; ++j) {
            // valid?
            if (offsets[j] >= 0) {
                next_offset = offsets[j];
                break;
            }
        }
        if (next_offset == -1) {
            // the remainder of the texures are missing, so read to the end
            // of the overall lump
            next_offset = lump.filelen;
        }

        if (next_offset > offset) {
            tex.stream_read(stream, next_offset - offset);
        }
    }
}

void dmiptexlump_t::stream_write(std::ostream &stream) const
{
    auto p = (size_t)stream.tellp();

    stream <= static_cast<int32_t>(textures.size());

    const size_t header_size = sizeof(int32_t) + (sizeof(int32_t) * textures.size());

    size_t miptex_offset = 0;

    // write out the miptex offsets
    for (auto &texture : textures) {
        if (!texture.name[0] || texture.width == 0 || texture.height == 0) {
            // dummy texture
            stream <= static_cast<int32_t>(-1);
            continue;
        }

        stream <= static_cast<int32_t>(header_size + miptex_offset);

        miptex_offset += texture.stream_size();

        // Half Life requires the padding, but it's also a good idea
        // in general to keep them padded to 4s
        if ((p + miptex_offset) % 4) {
            miptex_offset += 4 - ((p + miptex_offset) % 4);
        }
    }

    for (auto &texture : textures) {
        if (texture.name[0] && texture.width && texture.height) {
            // fix up the padding to match the above conditions
            if (stream.tellp() % 4) {
                constexpr const char pad[4]{};
                stream.write(pad, 4 - (stream.tellp() % 4));
            }
            texture.stream_write(stream);
        }
    }
}

size_t dmiptexlump_t::stream_size() const
{
    omemsizestream stream;
    stream_write(stream);
    return stream.tellp();
}

// dplane_t

void dplane_t::stream_write(std::ostream &s) const
{
    s <= std::tie(normal, dist, type);
}

void dplane_t::stream_read(std::istream &s)
{
    s >= std::tie(normal, dist, type);
}

// bsp2_dnode_t

void bsp2_dnode_t::stream_write(std::ostream &s) const
{
    s <= std::tie(planenum, children, mins, maxs, firstface, numfaces);
}
void bsp2_dnode_t::stream_read(std::istream &s)
{
    s >= std::tie(planenum, children, mins, maxs, firstface, numfaces);
}

// mface_t

void mface_t::stream_write(std::ostream &s) const
{
    s <= std::tie(planenum, side, firstedge, numedges, texinfo, styles, lightofs);
}
void mface_t::stream_read(std::istream &s)
{
    s >= std::tie(planenum, side, firstedge, numedges, texinfo, styles, lightofs);
}

// bsp2_dclipnode_t

void bsp2_dclipnode_t::stream_write(std::ostream &s) const
{
    s <= std::tie(planenum, children);
}

void bsp2_dclipnode_t::stream_read(std::istream &s)
{
    s >= std::tie(planenum, children);
}

// mleaf_t

static auto tuple(const mleaf_t &l)
{
    return std::tie(l.contents, l.visofs, l.mins, l.maxs, l.firstmarksurface, l.nummarksurfaces, l.ambient_level,
        l.cluster, l.area, l.firstleafbrush, l.numleafbrushes);
}

bool mleaf_t::operator==(const mleaf_t &other) const
{
    return tuple(*this) == tuple(other);
}

// darea_t

void darea_t::stream_write(std::ostream &s) const
{
    s <= std::tie(numareaportals, firstareaportal);
}

void darea_t::stream_read(std::istream &s)
{
    s >= std::tie(numareaportals, firstareaportal);
}

bool darea_t::operator==(const darea_t &other) const
{
    return std::tie(numareaportals, firstareaportal) == std::tie(other.numareaportals, other.firstareaportal);
}

// dareaportal_t

void dareaportal_t::stream_write(std::ostream &s) const
{
    s <= std::tie(portalnum, otherarea);
}

void dareaportal_t::stream_read(std::istream &s)
{
    s >= std::tie(portalnum, otherarea);
}

bool dareaportal_t::operator==(const dareaportal_t &other) const
{
    return std::tie(portalnum, otherarea) == std::tie(other.portalnum, other.otherarea);
}

// dbrush_t

void dbrush_t::stream_write(std::ostream &s) const
{
    s <= std::tie(firstside, numsides, contents);
}

void dbrush_t::stream_read(std::istream &s)
{
    s >= std::tie(firstside, numsides, contents);
}

// q2_dbrushside_qbism_t

void q2_dbrushside_qbism_t::stream_write(std::ostream &s) const
{
    s <= std::tie(planenum, texinfo);
}

void q2_dbrushside_qbism_t::stream_read(std::istream &s)
{
    s >= std::tie(planenum, texinfo);
}
