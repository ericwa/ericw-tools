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

// sin_dheader_t

void sin_dheader_t::stream_write(std::ostream &s) const
{
    s <= std::tie(ident, version, lumps);
}

void sin_dheader_t::stream_read(std::istream &s)
{
    s >= std::tie(ident, version, lumps);
}

// sin_lightvalue_t

void sin_lightinfo_t::stream_write(std::ostream &s) const
{
    s <= std::tie(value, color, direct, directangle, directstyle, directstylename);
}

void sin_lightinfo_t::stream_read(std::istream &s)
{
    s >= std::tie(value, color, direct, directangle, directstyle, directstylename);
}

// sin_texinfo_t

// convert from mbsp_t
sin_texinfo_t::sin_texinfo_t(const mtexinfo_t &model)
    : vecs(model.vecs),
      flags(model.flags.native_q2),
      texture(model.texture),
      nexttexinfo(model.nexttexinfo),
      trans_mag(model.trans_mag),
      trans_angle(model.trans_angle),
      base_angle(model.base_angle),
      animtime(model.animtime),
      nonlit(model.nonlit),
      translucence(model.translucence),
      friction(model.friction),
      restitution(model.restitution),
      color(model.color),
      groupname(model.groupname)
{
}

sin_texinfo_t::operator mtexinfo_t() const
{
    return {vecs, {.native_q2 = static_cast<q2_surf_flags_t>(flags)}, 0, 0, texture, nexttexinfo, trans_mag, trans_angle, base_angle, animtime, nonlit, translucence,
            friction, restitution, color, groupname};
}

void sin_texinfo_t::stream_write(std::ostream &s) const
{
    s <= std::tie(vecs, flags, texture, nexttexinfo, trans_mag, trans_angle, base_angle, animtime, nonlit, translucence, friction, restitution, color, groupname);
}

void sin_texinfo_t::stream_read(std::istream &s)
{
    s >= std::tie(vecs, flags, texture, nexttexinfo, trans_mag, trans_angle, base_angle, animtime, nonlit, translucence, friction, restitution, color, groupname);
}

// sin_dface_t

sin_dface_t::sin_dface_t(const mface_t &face)
    : planenum(numeric_cast<uint16_t>(face.planenum, "dface_t::planenum")),
      side(numeric_cast<int16_t>(face.side, "dface_t::side")),
      firstedge(face.firstedge),
      numedges(numeric_cast<int16_t>(face.numedges, "dface_t::numedges")),
      texinfo(numeric_cast<int16_t>(face.texinfo, "dface_t::texinfo")),
      styles(face.styles),
      lightofs(face.lightofs),
      lightinfo(face.lightinfo)
{
}

sin_dface_t::operator mface_t() const
{
    return {planenum, side, firstedge, numedges, texinfo, styles, lightofs, lightinfo};
}

void sin_dface_t::stream_write(std::ostream &s) const
{
    s <= std::tie(planenum, side, firstedge, numedges, texinfo, styles, lightofs, lightinfo);
}

void sin_dface_t::stream_read(std::istream &s)
{
    s >= std::tie(planenum, side, firstedge, numedges, texinfo, styles, lightofs, lightinfo);
}

// sin_dbrushside_t

sin_dbrushside_t::sin_dbrushside_t(const q2_dbrushside_qbism_t &model)
    : q2_dbrushside_t(model),
      lightinfo(model.lightinfo)
{
}

sin_dbrushside_t::operator q2_dbrushside_qbism_t() const
{
    return {planenum, texinfo, lightinfo};
}

void sin_dbrushside_t::stream_write(std::ostream &s) const
{
    s <= std::tie(planenum, texinfo, lightinfo);
}

void sin_dbrushside_t::stream_read(std::istream &s)
{
    s >= std::tie(planenum, texinfo, lightinfo);
}