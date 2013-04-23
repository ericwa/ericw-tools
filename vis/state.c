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

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <vis/vis.h>
#include <common/cmdlib.h>

#define VIS_STATE_VERSION ('T' << 24 | 'Y' << 16 | 'R' << 8 | '1')

typedef struct {
    uint32_t version;
    uint32_t numportals;
    uint32_t numleafs;
    uint32_t testlevel;
    uint32_t time_elapsed;
} dvisstate_t;

typedef struct {
    uint32_t status;
    uint32_t might;
    uint32_t vis;
    uint32_t nummightsee;
    uint32_t numcansee;
} dportal_t;

static int
CompressBits(uint8_t *out, const leafbits_t *in)
{
    int i, rep, shift, numbytes;
    uint8_t val, repval, *dst;

    dst = out;
    numbytes = (portalleafs + 7) >> 3;
    for (i = 0; i < numbytes && dst - out < numbytes; i++) {
	shift = (i << 3) & LEAFMASK;
	val = (in->bits[i >> (LEAFSHIFT - 3)] >> shift) & 0xff;
	*dst++ = val;
	if (val != 0 && val != 0xff)
	    continue;
	if (dst - out >= numbytes)
	    break;

	rep = 1;
	for (i++; i < numbytes; i++) {
	    shift = (i << 3) & LEAFMASK;
	    repval = (in->bits[i >> (LEAFSHIFT - 3)] >> shift) & 0xff;
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
	shift = (i << 3) & LEAFMASK;
	*dst++ = (in->bits[i >> (LEAFSHIFT - 3)] >> shift) & 0xff;
    }
    return numbytes;
}

static void
DecompressBits(leafbits_t *dst, const uint8_t *src)
{
    int i, rep, shift, numbytes;
    uint8_t val;

    numbytes = (portalleafs + 7) >> 3;
    memset(dst->bits, 0, numbytes);
    dst->numleafs = portalleafs;

    for (i = 0; i < numbytes; i++) {
	val = *src++;
	shift = (i << 3) & LEAFMASK;
	dst->bits[i >> (LEAFSHIFT - 3)] |= (leafblock_t)val << shift;
	if (val != 0 && val != 0xff)
	    continue;

	rep = *src++;
	if (i + rep > numbytes)
	    Error("%s: overflow", __func__);

	/* Already wrote the first byte, add (rep - 1) copies */
	while (--rep) {
	    i++;
	    shift = (i << 3) & LEAFMASK;
	    dst->bits[i >> (LEAFSHIFT - 3)] |= (leafblock_t)val << shift;
	}
    }
}

static void
CopyLeafBits(leafbits_t *dst, const byte *src, int numleafs)
{
    int i, shift;
    int numbytes;

    numbytes = (numleafs + 7) >> 3;
    memset(dst->bits, 0, numbytes);
    dst->numleafs = numleafs;

    for (i = 0; i < numbytes; i++) {
	shift = (i << 3) & LEAFMASK;
	dst->bits[i >> (LEAFSHIFT - 3)] |= (leafblock_t)(*src++) << shift;
    }
}

void
SaveVisState(void)
{
    int i, vis_len, might_len;
    const portal_t *p;
    dvisstate_t state;
    dportal_t pstate;
    uint8_t *vis;
    uint8_t *might;
    FILE *outfile;
    int err;

    outfile = SafeOpenWrite(statetmpfile);

    /* Write out a header */
    state.version = LittleLong(VIS_STATE_VERSION);
    state.numportals = LittleLong(numportals);
    state.numleafs = LittleLong(portalleafs);
    state.testlevel = LittleLong(testlevel);
    state.time_elapsed = LittleLong((uint32_t)(statetime - starttime));

    SafeWrite(outfile, &state, sizeof(state));

    /* Allocate memory for compressed bitstrings */
    might = malloc((portalleafs + 7) >> 3);
    vis = malloc((portalleafs + 7) >> 3);

    for (i = 0, p = portals; i < numportals * 2; i++, p++ ) {
	might_len = CompressBits(might, p->mightsee);
	if (p->status == pstat_done)
	    vis_len = CompressBits(vis, p->visbits);
	else
	    vis_len = 0;

	pstate.status = LittleLong(p->status);
	pstate.might = LittleLong(might_len);
	pstate.vis = LittleLong(vis_len);
	pstate.nummightsee = LittleLong(p->nummightsee);
	pstate.numcansee = LittleLong(p->numcansee);

	SafeWrite(outfile, &pstate, sizeof(pstate));
	SafeWrite(outfile, might, might_len);
	if (vis_len)
	    SafeWrite(outfile, vis, vis_len);
    }

    free(might);
    free(vis);

    err = fclose(outfile);
    if (err)
	Error("%s: error writing new state (%s)", __func__, strerror(errno));
    err = unlink(statefile);
    if (err && errno != ENOENT)
	Error("%s: error removing old state (%s)", __func__, strerror(errno));
    err = rename(statetmpfile, statefile);
    if (err)
	Error("%s: error renaming state file (%s)", __func__, strerror(errno));
}

qboolean
LoadVisState(void)
{
    FILE *infile;
    int prt_time, state_time;
    int i, numbytes, err;
    portal_t *p;
    dvisstate_t state;
    dportal_t pstate;
    byte *compressed;

    state_time = FileTime(statefile);
    if (state_time == -1) {
	/* No state file, maybe temp file is there? */
	state_time = FileTime(statetmpfile);
	if (state_time == -1)
	    return false;
	err = rename(statetmpfile, statefile);
	if (err)
	    return false;
    }

    prt_time = FileTime(portalfile);
    if (prt_time > state_time) {
	logprint("State file is out of date, will be overwritten\n");
	return false;
    }

    infile = SafeOpenRead(statefile);

    SafeRead(infile, &state, sizeof(state));
    state.version = LittleLong(state.version);
    state.numportals = LittleLong(state.numportals);
    state.numleafs = LittleLong(state.numleafs);
    state.testlevel = LittleLong(state.testlevel);
    state.time_elapsed = LittleLong(state.time_elapsed);

    /* Sanity check the headers */
    if (state.version != VIS_STATE_VERSION) {
	fclose(infile);
	Error("%s: state file version does not match", __func__);
    }
    if (state.numportals != numportals || state.numleafs != portalleafs) {
	fclose(infile);
	Error("%s: state file %s does not match portal file %s", __func__,
	      statefile, portalfile);
    }

    /* Move back the start time to simulate already elapsed time */
    starttime -= state.time_elapsed;

    numbytes = (portalleafs + 7) >> 3;
    compressed = malloc(numbytes);

    /* Update the portal information */
    for (i = 0, p = portals; i < numportals * 2; i++, p++) {
	SafeRead(infile, &pstate, sizeof(pstate));
	pstate.status = LittleLong(pstate.status);
	pstate.might = LittleLong(pstate.might);
	pstate.vis = LittleLong(pstate.vis);
	pstate.nummightsee = LittleLong(pstate.nummightsee);
	pstate.numcansee = LittleLong(pstate.numcansee);

	p->status = pstate.status;
	p->nummightsee = pstate.nummightsee;
	p->numcansee = pstate.numcansee;

	SafeRead(infile, compressed, pstate.might);
	p->mightsee = malloc(LeafbitsSize(portalleafs));
	memset(p->mightsee, 0, LeafbitsSize(portalleafs));
	if (pstate.might < numbytes)
	    DecompressBits(p->mightsee, compressed);
	else
	    CopyLeafBits(p->mightsee, compressed, portalleafs);

	p->visbits = malloc(LeafbitsSize(portalleafs));
	memset(p->visbits, 0, LeafbitsSize(portalleafs));
	if (pstate.vis) {
	    SafeRead(infile, compressed, pstate.vis);
	    if (pstate.vis < numbytes)
		DecompressBits(p->visbits, compressed);
	    else
		CopyLeafBits(p->visbits, compressed, portalleafs);
	}

	/* Portals that were in progress need to be started again */
	if (p->status == pstat_working)
	    p->status = pstat_none;
    }

    free(compressed);
    fclose(infile);

    return true;
}
