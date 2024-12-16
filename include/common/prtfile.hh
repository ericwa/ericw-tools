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

#pragma once

#include <vector>

#include <common/polylib.hh>
#include <common/fs.hh>

constexpr size_t PRT_MAX_WINDING_FIXED = 24;

using prtfile_winding_storage_t = polylib::winding_storage_hybrid_t<double, PRT_MAX_WINDING_FIXED>;
using prtfile_winding_t = polylib::winding_base_t<prtfile_winding_storage_t>;

struct prtfile_portal_t
{
    prtfile_winding_t winding;
    int leafnums[2];
};

struct prtfile_dleafinfo_t
{
    int cluster;
};

struct prtfile_t
{
    int portalleafs; // leafs (PRT1) or clusters (PRT2)
    int portalleafs_real; // real no. of leafs after expanding PRT2 clusters. Not used for Q2.

    std::vector<prtfile_portal_t> portals;
    std::vector<prtfile_dleafinfo_t> dleafinfos; // not used for Q2
};

struct bspversion_t;
prtfile_t LoadPrtFile(const fs::path &name, const bspversion_t *loadversion);
void WritePortalfile(
    const fs::path &name, const prtfile_t &prtfile, const bspversion_t *loadversion, bool uses_detail, bool forceprt1);

void WriteDebugPortals(const std::vector<polylib::winding_t> &portals, fs::path name);
