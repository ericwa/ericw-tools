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

#include <common/prtfile.hh>

#include <common/log.hh>
#include <common/fs.hh>
#include <common/bspfile.hh>

#include <fstream>
#include <fmt/ostream.h>

constexpr const char *PORTALFILE = "PRT1";
constexpr const char *PORTALFILE2 = "PRT2";
constexpr const char *PORTALFILEAM = "PRT1-AM";

constexpr size_t PRT_MAX_WINDING = 64;

prtfile_t LoadPrtFile(const fs::path &name, const bspversion_t *loadversion)
{
    std::ifstream f(name);

    /*
     * Parse the portal file header
     */
    std::string magic;
    std::getline(f, magic);
    if (magic.empty()) {
        FError("unknown header/empty portal file {}\n", name);
    }

    prtfile_t result{};
    int numportals;

    if (magic == PORTALFILE) {
        f >> result.portalleafs >> numportals;

        if (f.bad())
            FError("unable to parse {} header\n", PORTALFILE);

        if (loadversion->game->id == GAME_QUAKE_II) {
            // since q2bsp has native cluster support, we shouldn't look at portalleafs_real at all.
            result.portalleafs_real = 0;
        } else {
            result.portalleafs_real = result.portalleafs;
        }
    } else if (magic == PORTALFILE2) {
        if (loadversion->game->id == GAME_QUAKE_II) {
            FError("{} can not be used with Q2\n", PORTALFILE2);
        }
        f >> result.portalleafs_real >> result.portalleafs >> numportals;

        if (f.bad())
            FError("unable to parse {} header\n", PORTALFILE);
    } else if (magic == PORTALFILEAM) {
        if (loadversion->game->id == GAME_QUAKE_II) {
            FError("{} can not be used with Q2\n", PORTALFILEAM);
        }
        f >> result.portalleafs >> numportals >> result.portalleafs_real;

        if (f.bad())
            FError("unable to parse {} header\n", PORTALFILE);
    } else {
        FError("unknown header: {}\n", magic);
    }

    for (int i = 0; i < numportals; i++) {
        prtfile_portal_t p;
        int numpoints;

        f >> numpoints >> p.leafnums[0] >> p.leafnums[1];
        if (f.bad())
            FError("reading portal {}", i);
        if (numpoints > PRT_MAX_WINDING)
            FError("portal {} has too many points", i);
        if ((unsigned)p.leafnums[0] > (unsigned)result.portalleafs ||
            (unsigned)p.leafnums[1] > (unsigned)result.portalleafs)
            FError("out of bounds leaf in portal {}", i);

        auto &w = p.winding;
        w.resize(numpoints);

        for (int j = 0; j < numpoints; j++) {
            while (!f.bad() && f.get() != '(')
                ;

            f >> w[j][0] >> w[j][1] >> w[j][2];

            while (!f.bad() && f.get() != ')')
                ;

            if (f.bad())
                FError("reading portal {}", i);
        }

        result.portals.push_back(std::move(p));
    }

    // Q2 doesn't need this, it's PRT1 has the data we need
    if (loadversion->game->id == GAME_QUAKE_II) {
        return result;
    }

    // No clusters
    if (result.portalleafs == result.portalleafs_real) {
        // e.g. Quake 1, PRT1 (no func_detail).
        // Assign the identity cluster numbers for consistency

        result.dleafinfos.resize(result.portalleafs + 1);

        for (int i = 0; i < result.portalleafs; i++) {
            result.dleafinfos[i + 1].cluster = i;
        }
        return result;
    }

    if (magic == PORTALFILE2) {
        result.dleafinfos.resize(result.portalleafs_real + 1);

        int i;
        for (i = 0; i < result.portalleafs; i++) {
            while (1) {
                int leafnum;
                f >> leafnum;
                if (f.bad() || f.eof())
                    break;
                if (leafnum < 0)
                    break;
                if (leafnum >= result.portalleafs_real)
                    FError("Invalid leaf number in cluster map ({} >= {})", leafnum, result.portalleafs_real);
                result.dleafinfos[leafnum + 1].cluster = i;
            }
            if (f.bad() || f.eof())
                break;
        }
        if (i < result.portalleafs)
            FError("Couldn't read cluster map ({} / {})\n", i, result.portalleafs);
    } else if (magic == PORTALFILEAM) {
        result.dleafinfos.resize(result.portalleafs + 1);

        for (int i = 0; i < result.portalleafs_real; i++) {
            int clusternum;
            f >> clusternum;
            if (f.bad() || f.eof()) {
                Error("Unexpected end of cluster map\n");
            }
            if (clusternum < 0 || clusternum >= result.portalleafs) {
                FError("Invalid cluster number {} in cluster map, number of clusters: {}\n", clusternum,
                    result.portalleafs);
            }
            result.dleafinfos[i + 1].cluster = clusternum;
        }
    } else {
        FError("Unknown header {}\n", magic);
    }

    return result;
}

static void WriteDebugPortal(const polylib::winding_t &w, std::ofstream &portalFile)
{
    fmt::print(portalFile, "{} {} {} ", w.size(), 0, 0);
    for (int i = 0; i < w.size(); i++) {
        fmt::print(portalFile, "({} {} {}) ", w.at(i)[0], w.at(i)[1], w.at(i)[2]);
    }
    fmt::print(portalFile, "\n");
}

void WriteDebugPortals(const std::vector<polylib::winding_t> &portals, fs::path name)
{
    size_t portal_count = portals.size();

    std::ofstream portal_file(name, std::ios_base::out);
    if (!portal_file)
        FError("Failed to open {}: {}", name, strerror(errno));

    fmt::print(portal_file, "PRT1\n");
    fmt::print(portal_file, "{}\n", 0);
    fmt::print(portal_file, "{}\n", portal_count);
    for (auto &p : portals) {
        WriteDebugPortal(p, portal_file);
    }
}
