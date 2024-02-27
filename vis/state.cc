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

#include <cstdint>
// #include <cstdio>

#ifdef LINUX
#include <unistd.h>
#endif

#include <vis/vis.hh>
#include <common/cmdlib.hh>
#include "common/fs.hh"
#include <common/log.hh>
#include <fstream>

constexpr uint32_t VIS_STATE_VERSION = ('T' << 24 | 'Y' << 16 | 'R' << 8 | '1');

struct dvisstate_t
{
    uint32_t version;
    uint32_t numportals;
    uint32_t numleafs;
    uint32_t testlevel;
    uint32_t time_elapsed;

    auto stream_data() { return std::tie(version, numportals, numleafs, testlevel, time_elapsed); }
};

struct dportal_t
{
    uint32_t status;
    uint32_t might;
    uint32_t vis;
    uint32_t nummightsee;
    uint32_t numcansee;

    auto stream_data() { return std::tie(status, might, vis, nummightsee, numcansee); }
};

static int CompressBits(uint8_t *out, const leafbits_t &in)
{
    int i, rep, shift, numbytes;
    uint8_t val, repval, *dst;

    dst = out;
    numbytes = (portalleafs + 7) >> 3;
    for (i = 0; i < numbytes && dst - out < numbytes; i++) {
        shift = (i << 3) & leafbits_t::mask;
        val = (in.data()[i >> (leafbits_t::shift - 3)] >> shift) & 0xff;
        *dst++ = val;
        if (val != 0 && val != 0xff)
            continue;
        if (dst - out >= numbytes)
            break;

        rep = 1;
        for (i++; i < numbytes; i++) {
            shift = (i << 3) & leafbits_t::mask;
            repval = (in.data()[i >> (leafbits_t::shift - 3)] >> shift) & 0xff;
            if (repval != val || rep == 255)
                break;
            rep++;
        }
        *dst++ = rep;
        i--;
    }

    if (dst - out < numbytes)
        return dst - out;

    /* Compression ineffective, just copy the data */
    dst = out;
    for (i = 0; i < numbytes; i++) {
        shift = (i << 3) & leafbits_t::mask;
        *dst++ = (in.data()[i >> (leafbits_t::shift - 3)] >> shift) & 0xff;
    }
    return numbytes;
}

static void DecompressBits(leafbits_t &dst, const uint8_t *src)
{
    const size_t numbytes = (portalleafs + 7) >> 3;

    dst.resize(portalleafs);

    for (size_t i = 0; i < numbytes; i++) {
        uint8_t val = *src++;
        uint32_t shift = (i << 3) & leafbits_t::mask;
        dst.data()[i >> (leafbits_t::shift - 3)] |= (uint32_t)val << shift;
        if (val != 0 && val != 0xff)
            continue;

        int32_t rep = *src++;
        if (i + rep > numbytes)
            FError("overflow");

        /* Already wrote the first byte, add (rep - 1) copies */
        while (--rep) {
            i++;
            shift = (i << 3) & leafbits_t::mask;
            dst.data()[i >> (leafbits_t::shift - 3)] |= (uint32_t)val << shift;
        }
    }
}

static void CopyLeafBits(leafbits_t &dst, const uint8_t *src, size_t numleafs)
{
    const size_t numbytes = (numleafs + 7) >> 3;
    dst.resize(numleafs);

    for (size_t i = 0; i < numbytes; i++) {
        const uint32_t shift = (i << 3) & leafbits_t::mask;
        dst.data()[i >> (leafbits_t::shift - 3)] |= (uint32_t)(*src++) << shift;
    }
}

void SaveVisState()
{
    int vis_len, might_len;
    dvisstate_t state;
    dportal_t pstate;

    std::ofstream out(statetmpfile, std::ios_base::out | std::ios_base::binary);
    out << endianness<std::endian::little>;

    /* Write out a header */
    state.version = VIS_STATE_VERSION;
    state.numportals = numportals;
    state.numleafs = portalleafs;
    state.testlevel = vis_options.visdist.value();
    state.time_elapsed = (uint32_t)(statetime - starttime).count();

    out <= state;

    /* Allocate memory for compressed bitstrings */
    std::vector<uint8_t> might((portalleafs + 7) >> 3);
    std::vector<uint8_t> vis((portalleafs + 7) >> 3);

    for (const auto &p : portals) {
        might_len = CompressBits(might.data(), p.mightsee);
        if (p.status == pstat_done) {
            vis_len = CompressBits(vis.data(), p.visbits);
        } else {
            vis_len = 0;
        }

        pstate.status = p.status;
        pstate.might = might_len;
        pstate.vis = vis_len;
        pstate.nummightsee = p.nummightsee;
        pstate.numcansee = p.numcansee;

        out <= pstate;
        out.write((const char *)might.data(), might_len);
        if (vis_len) {
            out.write((const char *)vis.data(), vis_len);
        }
    }

    out.close();

    std::error_code ec;

    fs::remove(statefile, ec);
    if (ec && ec.value() != ENOENT)
        FError("error removing old state ({})", ec.message());

    fs::rename(statetmpfile, statefile, ec);
    if (ec)
        FError("error renaming state file ({})", ec.message());
}

void CleanVisState()
{
    if (fs::exists(statefile)) {
        fs::remove(statefile);
    }
}

bool LoadVisState()
{
    fs::file_time_type prt_time, state_time;
    int numbytes;
    dvisstate_t state;
    dportal_t pstate;

    if (vis_options.nostate.value()) {
        return false;
    }

    if (!fs::exists(statefile)) {
        /* No state file, maybe temp file is there? */
        if (!fs::exists(statetmpfile))
            return false;
        state_time = fs::last_write_time(statetmpfile);

        std::error_code ec;
        fs::rename(statetmpfile, statefile, ec);

        if (ec)
            return false;
    } else {
        state_time = fs::last_write_time(statefile);
    }

    prt_time = fs::last_write_time(portalfile);
    if (prt_time > state_time) {
        logging::print("State file is out of date, will be overwritten\n");
        return false;
    }

    std::ifstream in(statefile, std::ios_base::in | std::ios_base::binary);
    in >> endianness<std::endian::little>;

    in >= state;

    /* Sanity check the headers */
    if (state.version != VIS_STATE_VERSION) {
        FError("state file version does not match");
    }
    if (state.numportals != numportals || state.numleafs != portalleafs) {
        FError("state file {} does not match portal file {}", statefile, portalfile);
    }

    /* Move back the start time to simulate already elapsed time */
    starttime -= duration(state.time_elapsed);

    numbytes = (portalleafs + 7) >> 3;
    std::vector<uint8_t> compressed(numbytes);

    /* Update the portal information */
    for (auto &p : portals) {
        in >= pstate;

        p.status = static_cast<pstatus_t>(pstate.status);
        p.nummightsee = pstate.nummightsee;
        p.numcansee = pstate.numcansee;

        in.read((char *)compressed.data(), pstate.might);
        p.mightsee.resize(portalleafs);

        if (pstate.might < numbytes) {
            DecompressBits(p.mightsee, compressed.data());
        } else {
            CopyLeafBits(p.mightsee, compressed.data(), portalleafs);
        }

        p.visbits.resize(portalleafs);

        if (pstate.vis) {
            in.read((char *)compressed.data(), pstate.vis);
            if (pstate.vis < numbytes) {
                DecompressBits(p.visbits, compressed.data());
            } else {
                CopyLeafBits(p.visbits, compressed.data(), portalleafs);
            }
        }

        /* Portals that were in progress need to be started again */
        if (p.status == pstat_working) {
            p.status = pstat_none;
        }
    }

    return true;
}
