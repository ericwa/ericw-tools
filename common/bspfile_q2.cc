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

// q2_dheader_t

void q2_dheader_t::stream_write(std::ostream &s) const
{
    s <= std::tie(ident, version, lumps);
}

void q2_dheader_t::stream_read(std::istream &s)
{
    s >= std::tie(ident, version, lumps);
}

// q2_dmodel_t

q2_dmodel_t::q2_dmodel_t(const dmodelh2_t &model)
    : mins(model.mins),
      maxs(model.maxs),
      origin(model.origin),
      headnode(model.headnode[0]),
      firstface(model.firstface),
      numfaces(model.numfaces)
{
}

q2_dmodel_t::operator dmodelh2_t() const
{
    return {mins, maxs, origin, {headnode},
        0, // visleafs
        firstface, numfaces};
}

void q2_dmodel_t::stream_write(std::ostream &s) const
{
    s <= std::tie(mins, maxs, origin, headnode, firstface, numfaces);
}

void q2_dmodel_t::stream_read(std::istream &s)
{
    s >= std::tie(mins, maxs, origin, headnode, firstface, numfaces);
}

// q2_dnode_t

q2_dnode_t::q2_dnode_t(const bsp2_dnode_t &model)
    : planenum(model.planenum),
      children(model.children),
      mins(aabb_mins_cast<int16_t>(model.mins, "dnode_t::mins")),
      maxs(aabb_maxs_cast<int16_t>(model.maxs, "dnode_t::maxs")),
      firstface(numeric_cast<uint16_t>(model.firstface, "dnode_t::firstface")),
      numfaces(numeric_cast<uint16_t>(model.numfaces, "dnode_t::numfaces"))
{
}

q2_dnode_t::operator bsp2_dnode_t() const
{
    return {planenum, children, aabb_mins_cast<float>(mins, "dnode_t::mins"),
        aabb_mins_cast<float>(maxs, "dnode_t::maxs"), firstface, numfaces};
}

void q2_dnode_t::stream_write(std::ostream &s) const
{
    s <= std::tie(planenum, children, mins, maxs, firstface, numfaces);
}

void q2_dnode_t::stream_read(std::istream &s)
{
    s >= std::tie(planenum, children, mins, maxs, firstface, numfaces);
}

// q2_texinfo_t

q2_texinfo_t::q2_texinfo_t(const mtexinfo_t &model)
    : vecs(model.vecs),
      flags(model.flags.native_q2),
      value(model.value),
      texture(model.texture),
      nexttexinfo(model.nexttexinfo)
{
}

// convert to mbsp_t
q2_texinfo_t::operator mtexinfo_t() const
{
    return {vecs, {.native_q2 = static_cast<q2_surf_flags_t>(flags)}, -1, value, texture, nexttexinfo};
}

void q2_texinfo_t::stream_write(std::ostream &s) const
{
    s <= std::tie(vecs, flags, value, texture, nexttexinfo);
}

void q2_texinfo_t::stream_read(std::istream &s)
{
    s >= std::tie(vecs, flags, value, texture, nexttexinfo);
}

// q2_dface_t

q2_dface_t::q2_dface_t(const mface_t &model)
    : planenum(numeric_cast<uint16_t>(model.planenum, "dface_t::planenum")),
      side(numeric_cast<int16_t>(model.side, "dface_t::side")),
      firstedge(model.firstedge),
      numedges(numeric_cast<int16_t>(model.numedges, "dface_t::numedges")),
      texinfo(numeric_cast<int16_t>(model.texinfo, "dface_t::texinfo")),
      styles(model.styles),
      lightofs(model.lightofs)
{
}

q2_dface_t::operator mface_t() const
{
    return {planenum, side, firstedge, numedges, texinfo, styles, lightofs};
}

void q2_dface_t::stream_write(std::ostream &s) const
{
    s <= std::tie(planenum, side, firstedge, numedges, texinfo, styles, lightofs);
}

void q2_dface_t::stream_read(std::istream &s)
{
    s >= std::tie(planenum, side, firstedge, numedges, texinfo, styles, lightofs);
}

// q2_dface_qbism_t

q2_dface_qbism_t::q2_dface_qbism_t(const mface_t &model)
    : planenum(numeric_cast<uint32_t>(model.planenum, "dface_t::planenum")),
      side(model.side),
      firstedge(model.firstedge),
      numedges(model.numedges),
      texinfo(model.texinfo),
      styles(model.styles),
      lightofs(model.lightofs)
{
}

q2_dface_qbism_t::operator mface_t() const
{
    return {planenum, side, firstedge, numedges, texinfo, styles, lightofs};
}

void q2_dface_qbism_t::stream_write(std::ostream &s) const
{
    s <= std::tie(planenum, side, firstedge, numedges, texinfo, styles, lightofs);
}

void q2_dface_qbism_t::stream_read(std::istream &s)
{
    s >= std::tie(planenum, side, firstedge, numedges, texinfo, styles, lightofs);
}

// q2_dleaf_t

q2_dleaf_t::q2_dleaf_t(const mleaf_t &model)
    : contents(model.contents),
      cluster(numeric_cast<int16_t>(model.cluster, "dleaf_t::cluster")),
      area(numeric_cast<int16_t>(model.area, "dleaf_t::area")),
      mins(aabb_mins_cast<int16_t>(model.mins, "dleaf_t::mins")),
      maxs(aabb_mins_cast<int16_t>(model.maxs, "dleaf_t::maxs")),
      firstleafface(numeric_cast<uint16_t>(model.firstmarksurface, "dleaf_t::firstmarksurface")),
      numleaffaces(numeric_cast<uint16_t>(model.nummarksurfaces, "dleaf_t::nummarksurfaces")),
      firstleafbrush(numeric_cast<uint16_t>(model.firstleafbrush, "dleaf_t::firstleafbrush")),
      numleafbrushes(numeric_cast<uint16_t>(model.numleafbrushes, "dleaf_t::numleafbrushes"))
{
}

q2_dleaf_t::operator mleaf_t() const
{
    return {contents, -1, aabb_mins_cast<float>(mins, "dleaf_t::mins"), aabb_mins_cast<float>(maxs, "dleaf_t::maxs"),
        firstleafface, numleaffaces, {}, cluster, area, firstleafbrush, numleafbrushes};
}

void q2_dleaf_t::stream_write(std::ostream &s) const
{
    s <= std::tie(contents, cluster, area, mins, maxs, firstleafface, numleaffaces, firstleafbrush, numleafbrushes);
}

void q2_dleaf_t::stream_read(std::istream &s)
{
    s >= std::tie(contents, cluster, area, mins, maxs, firstleafface, numleaffaces, firstleafbrush, numleafbrushes);
}

// q2_dleaf_qbism_t

q2_dleaf_qbism_t::q2_dleaf_qbism_t(const mleaf_t &model)
    : contents(model.contents),
      cluster(model.cluster),
      area(model.area),
      mins(model.mins),
      maxs(model.maxs),
      firstleafface(model.firstmarksurface),
      numleaffaces(model.nummarksurfaces),
      firstleafbrush(model.firstleafbrush),
      numleafbrushes(model.numleafbrushes)
{
}

q2_dleaf_qbism_t::operator mleaf_t() const
{
    return {contents, -1, mins, maxs, firstleafface, numleaffaces, {}, cluster, area, firstleafbrush, numleafbrushes};
}

void q2_dleaf_qbism_t::stream_write(std::ostream &s) const
{
    s <= std::tie(contents, cluster, area, mins, maxs, firstleafface, numleaffaces, firstleafbrush, numleafbrushes);
}

void q2_dleaf_qbism_t::stream_read(std::istream &s)
{
    s >= std::tie(contents, cluster, area, mins, maxs, firstleafface, numleaffaces, firstleafbrush, numleafbrushes);
}

// q2_dbrushside_t

q2_dbrushside_t::q2_dbrushside_t(const q2_dbrushside_qbism_t &model)
    : planenum(numeric_cast<uint16_t>(model.planenum, "dbrushside_t::planenum")),
      texinfo(numeric_cast<int16_t>(model.texinfo, "dbrushside_t::texinfo"))
{
}

q2_dbrushside_t::operator q2_dbrushside_qbism_t() const
{
    return {planenum, texinfo};
}

void q2_dbrushside_t::stream_write(std::ostream &s) const
{
    s <= std::tie(planenum, texinfo);
}

void q2_dbrushside_t::stream_read(std::istream &s)
{
    s >= std::tie(planenum, texinfo);
}
