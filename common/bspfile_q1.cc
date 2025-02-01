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
#include <common/numeric_cast.hh>

// dheader_t

void dheader_t::stream_write(std::ostream &s) const
{
    s <= std::tie(ident, lumps);
}

void dheader_t::stream_read(std::istream &s)
{
    s >= std::tie(ident, lumps);
}

// dmodelq1_t

dmodelq1_t::dmodelq1_t(const dmodelh2_t &model)
    : mins(model.mins),
      maxs(model.maxs),
      origin(model.origin),
      headnode(array_cast<decltype(headnode)>(model.headnode, "dmodelh2_t::headnode")),
      visleafs(model.visleafs),
      firstface(model.firstface),
      numfaces(model.numfaces)
{
}

dmodelq1_t::operator dmodelh2_t() const
{
    return {mins, maxs, origin, array_cast<decltype(dmodelh2_t::headnode)>(headnode, "dmodelh2_t::headnode"), visleafs,
        firstface, numfaces};
}

void dmodelq1_t::stream_write(std::ostream &s) const
{
    s <= std::tie(mins, maxs, origin, headnode, visleafs, firstface, numfaces);
}
void dmodelq1_t::stream_read(std::istream &s)
{
    s >= std::tie(mins, maxs, origin, headnode, visleafs, firstface, numfaces);
}

// bsp29_dnode_t

bsp29_dnode_t::bsp29_dnode_t(const bsp2_dnode_t &model)
    : planenum(model.planenum),
      children(array_cast<decltype(children)>(model.children, "dnode_t::children")),
      mins(aabb_mins_cast<int16_t>(model.mins, "dnode_t::mins")),
      maxs(aabb_maxs_cast<int16_t>(model.maxs, "dnode_t::maxs")),
      firstface(numeric_cast<uint16_t>(model.firstface, "dnode_t::firstface")),
      numfaces(numeric_cast<uint16_t>(model.numfaces, "dnode_t::numfaces"))
{
}

bsp29_dnode_t::operator bsp2_dnode_t() const
{
    return {planenum, array_cast<decltype(bsp2_dnode_t::children)>(children, "dnode_t::children"),
        aabb_mins_cast<float>(mins, "dnode_t::mins"), aabb_mins_cast<float>(maxs, "dnode_t::maxs"), firstface,
        numfaces};
}

void bsp29_dnode_t::stream_write(std::ostream &s) const
{
    s <= std::tie(planenum, children, mins, maxs, firstface, numfaces);
}

void bsp29_dnode_t::stream_read(std::istream &s)
{
    s >= std::tie(planenum, children, mins, maxs, firstface, numfaces);
}

// bsp2rmq_dnode_t

bsp2rmq_dnode_t::bsp2rmq_dnode_t(const bsp2_dnode_t &model)
    : planenum(model.planenum),
      children(model.children),
      mins(aabb_mins_cast<int16_t>(model.mins, "dnode_t::mins")),
      maxs(aabb_maxs_cast<int16_t>(model.maxs, "dnode_t::maxs")),
      firstface(model.firstface),
      numfaces(model.numfaces)
{
}

bsp2rmq_dnode_t::operator bsp2_dnode_t() const
{
    return {planenum, children, aabb_mins_cast<float>(mins, "dnode_t::mins"),
        aabb_mins_cast<float>(maxs, "dnode_t::maxs"), firstface, numfaces};
}

void bsp2rmq_dnode_t::stream_write(std::ostream &s) const
{
    s <= std::tie(planenum, children, mins, maxs, firstface, numfaces);
}

void bsp2rmq_dnode_t::stream_read(std::istream &s)
{
    s >= std::tie(planenum, children, mins, maxs, firstface, numfaces);
}

// bsp29_dclipnode_t

bsp29_dclipnode_t::bsp29_dclipnode_t(const bsp2_dclipnode_t &model)
    : planenum(model.planenum),
      children({downcast(model.children[0]), downcast(model.children[1])})
{
}

bsp29_dclipnode_t::operator bsp2_dclipnode_t() const
{
    return {planenum, {upcast(children[0]), upcast(children[1])}};
}

void bsp29_dclipnode_t::stream_write(std::ostream &s) const
{
    s <= std::tie(planenum, children);
}

void bsp29_dclipnode_t::stream_read(std::istream &s)
{
    s >= std::tie(planenum, children);
}

int16_t bsp29_dclipnode_t::downcast(const int32_t &v)
{
    if (v < -15 || v > 0xFFF0) {
        throw std::overflow_error("dclipnode_t::children");
    }

    return static_cast<int16_t>(v < 0 ? v + 0x10000 : v);
}

int32_t bsp29_dclipnode_t::upcast(const int16_t &v)
{
    int32_t child = (uint16_t)v;
    return child > 0xfff0 ? child - 0x10000 : child;
}

// texinfo_t

texinfo_t::texinfo_t(const mtexinfo_t &model)
    : vecs(model.vecs),
      miptex(model.miptex),
      flags(model.flags.native_q1)
{
}

texinfo_t::operator mtexinfo_t() const
{
    return {vecs, {.native_q1 = static_cast<q1_surf_flags_t>(flags)}, miptex};
}

void texinfo_t::stream_write(std::ostream &s) const
{
    s <= std::tie(vecs, miptex, flags);
}

void texinfo_t::stream_read(std::istream &s)
{
    s >= std::tie(vecs, miptex, flags);
}

// bsp29_dface_t

bsp29_dface_t::bsp29_dface_t(const mface_t &model)
    : planenum(numeric_cast<int16_t>(model.planenum, "dface_t::planenum")),
      side(numeric_cast<int16_t>(model.side, "dface_t::side")),
      firstedge(model.firstedge),
      numedges(numeric_cast<int16_t>(model.numedges, "dface_t::numedges")),
      texinfo(numeric_cast<int16_t>(model.texinfo, "dface_t::texinfo")),
      styles(model.styles),
      lightofs(model.lightofs)
{
}

bsp29_dface_t::operator mface_t() const
{
    return {planenum, side, firstedge, numedges, texinfo, styles, lightofs};
}

void bsp29_dface_t::stream_write(std::ostream &s) const
{
    s <= std::tie(planenum, side, firstedge, numedges, texinfo, styles, lightofs);
}

void bsp29_dface_t::stream_read(std::istream &s)
{
    s >= std::tie(planenum, side, firstedge, numedges, texinfo, styles, lightofs);
}

// bsp2_dface_t

bsp2_dface_t::bsp2_dface_t(const mface_t &model)
    : planenum(numeric_cast<int32_t>(model.planenum, "dface_t::planenum")),
      side(model.side),
      firstedge(model.firstedge),
      numedges(model.numedges),
      texinfo(model.texinfo),
      styles(model.styles),
      lightofs(model.lightofs)
{
}

bsp2_dface_t::operator mface_t() const
{
    return {planenum, side, firstedge, numedges, texinfo, styles, lightofs};
}

void bsp2_dface_t::stream_write(std::ostream &s) const
{
    s <= std::tie(planenum, side, firstedge, numedges, texinfo, styles, lightofs);
}

void bsp2_dface_t::stream_read(std::istream &s)
{
    s >= std::tie(planenum, side, firstedge, numedges, texinfo, styles, lightofs);
}

// bsp29_dleaf_t

bsp29_dleaf_t::bsp29_dleaf_t(const mleaf_t &model)
    : contents(model.contents),
      visofs(model.visofs),
      mins(aabb_mins_cast<int16_t>(model.mins, "dleaf_t::mins")),
      maxs(aabb_maxs_cast<int16_t>(model.maxs, "dleaf_t::maxs")),
      firstmarksurface(numeric_cast<uint16_t>(model.firstmarksurface, "dleaf_t::firstmarksurface")),
      nummarksurfaces(numeric_cast<uint16_t>(model.nummarksurfaces, "dleaf_t::nummarksurfaces")),
      ambient_level(model.ambient_level)
{
}

bsp29_dleaf_t::operator mleaf_t() const
{
    return {contents, visofs, aabb_mins_cast<float>(mins, "dleaf_t::mins"),
        aabb_mins_cast<float>(maxs, "dleaf_t::maxs"), firstmarksurface, nummarksurfaces, ambient_level};
}

void bsp29_dleaf_t::stream_write(std::ostream &s) const
{
    s <= std::tie(contents, visofs, mins, maxs, firstmarksurface, nummarksurfaces, ambient_level);
}

void bsp29_dleaf_t::stream_read(std::istream &s)
{
    s >= std::tie(contents, visofs, mins, maxs, firstmarksurface, nummarksurfaces, ambient_level);
}

// bsp2rmq_dleaf_t

bsp2rmq_dleaf_t::bsp2rmq_dleaf_t(const mleaf_t &model)
    : contents(model.contents),
      visofs(model.visofs),
      mins(aabb_mins_cast<int16_t>(model.mins, "dleaf_t::mins")),
      maxs(aabb_maxs_cast<int16_t>(model.maxs, "dleaf_t::maxs")),
      firstmarksurface(model.firstmarksurface),
      nummarksurfaces(model.nummarksurfaces),
      ambient_level(model.ambient_level)
{
}

bsp2rmq_dleaf_t::operator mleaf_t() const
{
    return {contents, visofs, aabb_mins_cast<float>(mins, "dleaf_t::mins"),
        aabb_mins_cast<float>(maxs, "dleaf_t::maxs"), firstmarksurface, nummarksurfaces, ambient_level};
}

void bsp2rmq_dleaf_t::stream_write(std::ostream &s) const
{
    s <= std::tie(contents, visofs, mins, maxs, firstmarksurface, nummarksurfaces, ambient_level);
}

void bsp2rmq_dleaf_t::stream_read(std::istream &s)
{
    s >= std::tie(contents, visofs, mins, maxs, firstmarksurface, nummarksurfaces, ambient_level);
}

// bsp2_dleaf_t

bsp2_dleaf_t::bsp2_dleaf_t(const mleaf_t &model)
    : contents(model.contents),
      visofs(model.visofs),
      mins(model.mins),
      maxs(model.maxs),
      firstmarksurface(model.firstmarksurface),
      nummarksurfaces(model.nummarksurfaces),
      ambient_level(model.ambient_level)
{
}

bsp2_dleaf_t::operator mleaf_t() const
{
    return {contents, visofs, mins, maxs, firstmarksurface, nummarksurfaces, ambient_level};
}

void bsp2_dleaf_t::stream_write(std::ostream &s) const
{
    s <= std::tie(contents, visofs, mins, maxs, firstmarksurface, nummarksurfaces, ambient_level);
}

void bsp2_dleaf_t::stream_read(std::istream &s)
{
    s >= std::tie(contents, visofs, mins, maxs, firstmarksurface, nummarksurfaces, ambient_level);
}
