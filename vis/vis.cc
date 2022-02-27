// vis.c

#include <climits>
#include <cstddef>
#include <cstdint>

#include <vis/leafbits.hh>
#include <vis/vis.hh>
#include <common/log.hh>
#include <common/threads.hh>
#include <common/fs.hh>
#include <fmt/chrono.h>

/*
 * If the portal file is "PRT2" format, then the leafs we are dealing with are
 * really clusters of leaves. So, after the vis job is done we need to expand
 * the clusters to the real leaf numbers before writing back to the bsp file.
 */
int numportals;
int portalleafs; /* leafs (PRT1) or clusters (PRT2) */
int portalleafs_real; /* real no. of leafs after expanding PRT2 clusters. Not used for Q2. */

portal_t *portals;
leaf_t *leafs;

int c_portaltest, c_portalpass, c_portalcheck, c_mightseeupdate;
int c_noclip = 0;

bool showgetleaf = true;

static uint8_t *vismap;
static uint8_t *vismap_p;
static uint8_t *vismap_end; // past visfile

uint32_t originalvismapsize;

uint8_t *uncompressed; // [leafbytes_real*portalleafs]

uint8_t *uncompressed_q2; // [leafbytes*portalleafs]

int leafbytes; // (portalleafs+63)>>3
int leaflongs;
int leafbytes_real; // (portalleafs_real+63)>>3, not used for Q2.

namespace settings
{
setting_group output_group{"Output", 200};
setting_group advanced_group{"Advanced", 300};

void vis_settings::initialize(int argc, const char **argv)
{
    auto remainder = parse(token_parser_t(argc, argv));

    if (remainder.size() <= 0 || remainder.size() > 1) {
        printHelp();
    }

    sourceMap = DefaultExtension(remainder[0], "bsp");
}
} // namespace settings

settings::vis_settings options;

std::filesystem::path portalfile, statefile, statetmpfile;

/*
  ===============
  CompressRow
  ===============
*/
int CompressRow(const uint8_t *vis, const int numbytes, uint8_t *out)
{
    int i, rep;
    uint8_t *dst;

    dst = out;
    for (i = 0; i < numbytes; i++) {
        *dst++ = vis[i];
        if (vis[i])
            continue;

        rep = 1;
        for (i++; i < numbytes; i++)
            if (vis[i] || rep == 255)
                break;
            else
                rep++;
        *dst++ = rep;
        i--;
    }

    return dst - out;
}

/*
===================
DecompressRow
===================
*/
void DecompressRow(const uint8_t *in, const int numbytes, uint8_t *decompressed)
{
    int c;
    uint8_t *out;
    int row;

    row = numbytes;
    out = decompressed;

    do {
        if (*in) {
            *out++ = *in++;
            continue;
        }

        c = in[1];
        if (!c)
            FError("0 repeat");
        in += 2;
        while (c) {
            *out++ = 0;
            c--;
        }
    } while (out - decompressed < row);
}

/*
  ==================
  AllocStackWinding

  Return a pointer to a free fixed winding on the stack
  ==================
*/
std::shared_ptr<winding_t> &AllocStackWinding(pstack_t *stack)
{
    for (auto &winding : stack->windings) {
        if (!winding) {
            return (winding = std::make_shared<winding_t>());
        }
    }

    FError("failed");
}

/*
  ==================
  FreeStackWinding

  As long as the winding passed in is local to the stack, free it. Otherwise,
  do nothing (the winding either belongs to a portal or another stack
  structure further up the call chain).
  ==================
*/
void FreeStackWinding(std::shared_ptr<winding_t> &w, pstack_t *stack)
{
    for (auto &winding : stack->windings) {
        if (winding == w) {
            w.reset();
            winding.reset();
            return;
        }
    }
}

/*
  ==================
  ClipStackWinding

  Clips the winding to the plane, returning the new winding on the positive
  side. Frees the input winding (if on stack). If the resulting winding would
  have too many points, the clip operation is aborted and the original winding
  is returned.
  ==================
*/
std::shared_ptr<winding_t> ClipStackWinding(std::shared_ptr<winding_t> &in, pstack_t *stack, qplane3d *split)
{
    vec_t *dists = (vec_t *)alloca(sizeof(vec_t) * (in->size() + 1));
    int *sides = (int *)alloca(sizeof(int) * (in->size() + 1));
    int counts[3];
    int i, j;

    /* Fast test first */
    vec_t dot = split->distance_to(in->origin);
    if (dot < -in->radius) {
        FreeStackWinding(in, stack);
        return NULL;
    } else if (dot > in->radius) {
        return in;
    }

    if (in->size() > MAX_WINDING)
        FError("in->numpoints > MAX_WINDING ({} > {})", in->size(), MAX_WINDING);

    counts[0] = counts[1] = counts[2] = 0;

    /* determine sides for each point */
    for (i = 0; i < in->size(); i++) {
        dot = split->distance_to((*in)[i]);
        dists[i] = dot;
        if (dot > ON_EPSILON)
            sides[i] = SIDE_FRONT;
        else if (dot < -ON_EPSILON)
            sides[i] = SIDE_BACK;
        else {
            sides[i] = SIDE_ON;
        }
        counts[sides[i]]++;
    }
    sides[i] = sides[0];
    dists[i] = dists[0];

    // ericw -- coplanar portals: return without clipping. Otherwise when two portals are less than ON_EPSILON apart,
    // one will get fully clipped away and we can't see through it causing
    // https://github.com/ericwa/ericw-tools/issues/261
    if (counts[SIDE_ON] == in->size()) {
        return in;
    }

    if (!counts[0]) {
        FreeStackWinding(in, stack);
        return NULL;
    }
    if (!counts[1])
        return in;

    auto neww = AllocStackWinding(stack);
    neww->origin = in->origin;
    neww->radius = in->radius;

    for (i = 0; i < in->size(); i++) {
        const qvec3d &p1 = (*in)[i];

        if (sides[i] == SIDE_ON) {
            if (neww->size() == MAX_WINDING_FIXED)
                goto noclip;
            neww->push_back(p1);
            continue;
        }

        if (sides[i] == SIDE_FRONT) {
            if (neww->size() == MAX_WINDING_FIXED)
                goto noclip;
            neww->push_back(p1);
        }

        if (sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i])
            continue;

        /* generate a split point */
        const qvec3d &p2 = (*in)[(i + 1) % in->size()];
        qvec3d mid;
        vec_t fraction = dists[i] / (dists[i] - dists[i + 1]);
        for (j = 0; j < 3; j++) {
            /* avoid round off error when possible */
            if (split->normal[j] == 1)
                mid[j] = split->dist;
            else if (split->normal[j] == -1)
                mid[j] = -split->dist;
            else
                mid[j] = p1[j] + fraction * (p2[j] - p1[j]);
        }

        if (neww->size() == MAX_WINDING_FIXED)
            goto noclip;

        neww->push_back(mid);
    }

    FreeStackWinding(in, stack);
    return neww;

noclip:
    FreeStackWinding(neww, stack);
    c_noclip++;
    return in;
}

//============================================================================

/*
  =============
  GetNextPortal

  Returns the next portal for a thread to work on
  Returns the portals from the least complex, so the later ones can reuse
  the earlier information.
  =============
*/
portal_t *GetNextPortal(void)
{
    int i;
    portal_t *p, *ret;
    unsigned min;

    ThreadLock();

    min = INT_MAX;
    ret = NULL;

    for (i = 0, p = portals; i < numportals * 2; i++, p++) {
        if (p->nummightsee < min && p->status == pstat_none) {
            min = p->nummightsee;
            ret = p;
        }
    }

    if (ret) {
        ret->status = pstat_working;
        GetThreadWork_Locked__();
    }

    ThreadUnlock();

    return ret;
}

/*
  =============
  UpdateMightSee

  Called after completing a portal and finding that the source leaf is no
  longer visible from the dest leaf. Visibility is symetrical, so the reverse
  must also be true. Update mightsee for any portals on the source leaf which
  haven't yet started processing.

  Called with the lock held.
  =============
*/
static void UpdateMightsee(const leaf_t *source, const leaf_t *dest)
{
    int i, leafnum;
    portal_t *p;

    leafnum = dest - leafs;
    for (i = 0; i < source->numportals; i++) {
        p = source->portals[i];
        if (p->status != pstat_none)
            continue;
        if (p->mightsee[leafnum]) {
            p->mightsee[leafnum] = false;
            p->nummightsee--;
            c_mightseeupdate++;
        }
    }
}

/*
  =============
  PortalCompleted

  Mark the portal completed and propogate new vis information across
  to the complementry portals.

  Called with the lock held.
  =============
*/
static void PortalCompleted(portal_t *completed)
{
    int i, j, k, bit, numblocks;
    int leafnum;
    const portal_t *p, *p2;
    const leaf_t *myleaf;
    const uint32_t *might, *vis;
    uint32_t changed;

    ThreadLock();

    completed->status = pstat_done;

    /*
     * For each portal on the leaf, check the leafs we eliminated from
     * mightsee during the full vis so far.
     */
    myleaf = &leafs[completed->leaf];
    for (i = 0; i < myleaf->numportals; i++) {
        p = myleaf->portals[i];
        if (p->status != pstat_done)
            continue;

        might = p->mightsee.data();
        vis = p->visbits.data();
        numblocks = (portalleafs + leafbits_t::mask) >> leafbits_t::shift;
        for (j = 0; j < numblocks; j++) {
            changed = might[j] & ~vis[j];
            if (!changed)
                continue;

            /*
             * If any of these changed bits are still visible from another
             * portal, we can't update yet.
             */
            for (k = 0; k < myleaf->numportals; k++) {
                if (k == i)
                    continue;
                p2 = myleaf->portals[k];
                if (p2->status == pstat_done)
                    changed &= ~p2->visbits.data()[j];
                else
                    changed &= ~p2->mightsee.data()[j];
                if (!changed)
                    break;
            }

            /*
             * Update mightsee for any of the changed bits that survived
             */
            while (changed) {
                bit = ffsl(changed) - 1;
                changed &= ~(1UL << bit);
                leafnum = (j << leafbits_t::shift) + bit;
                UpdateMightsee(leafs + leafnum, myleaf);
            }
        }
    }

    ThreadUnlock();
}

time_point starttime, endtime, statetime;
static duration stateinterval;

/*
  ==============
  LeafThread
  ==============
*/
void *LeafThread(void *arg)
{
    portal_t *p;

    do {
        ThreadLock();
        /* Save state if sufficient time has elapsed */
        auto now = I_FloatTime();
        if (now > statetime + stateinterval) {
            statetime = now;
            SaveVisState();
        }
        ThreadUnlock();

        p = GetNextPortal();
        if (!p)
            break;

        PortalFlow(p);

        PortalCompleted(p);

        LogPrint(LOG_VERBOSE, "portal:{:4}  mightsee:{:4}  cansee:{:4}\n", (ptrdiff_t)(p - portals), p->nummightsee,
            p->numcansee);
    } while (1);

    return NULL;
}

/*
  ===============
  LeafFlow

  Builds the entire visibility list for a leaf
  ===============
*/
int64_t totalvis;

static void ClusterFlow(int clusternum, leafbits_t &buffer, mbsp_t *bsp)
{
    leaf_t *leaf;
    uint8_t *outbuffer;
    uint8_t *compressed;
    int i, j, len;
    int numvis, numblocks;
    uint8_t *dest;
    const portal_t *p;

    /*
     * Collect visible bits from all portals into buffer
     */
    leaf = &leafs[clusternum];
    numblocks = (portalleafs + leafbits_t::mask) >> leafbits_t::shift;
    for (i = 0; i < leaf->numportals; i++) {
        p = leaf->portals[i];
        if (p->status != pstat_done)
            FError("portal not done");
        for (j = 0; j < numblocks; j++)
            buffer.data()[j] |= p->visbits.data()[j];
    }

    // ericw -- this seems harmless and the fix for https://github.com/ericwa/ericw-tools/issues/261
    // causes it to happen a lot.
    // if (TestLeafBit(buffer, clusternum))
    //    LogPrint("WARNING: Leaf portals saw into cluster ({})\n", clusternum);

    buffer[clusternum] = true;

    /*
     * Now expand the clusters into the full leaf visibility map
     */
    numvis = 0;

    if (bsp->loadversion->game->id == GAME_QUAKE_II) {
        outbuffer = uncompressed_q2 + clusternum * leafbytes;
        for (i = 0; i < portalleafs; i++) {
            if (buffer[i]) {
                outbuffer[i >> 3] |= (1 << (i & 7));
                numvis++;
            }
        }
    } else {
        outbuffer = uncompressed + clusternum * leafbytes_real;
        for (i = 0; i < portalleafs_real; i++) {
            if (buffer[bsp->dleafs[i + 1].cluster]) {
                outbuffer[i >> 3] |= (1 << (i & 7));
                numvis++;
            }
        }
    }

    /*
     * compress the bit string
     */
    LogPrint(LOG_VERBOSE, "cluster {:4} : {:4} visible\n", clusternum, numvis);

    /*
     * increment totalvis by
     * (# of real leafs in this cluster) x (# of real leafs visible from this cluster)
     */
    if (bsp->loadversion->game->id == GAME_QUAKE_II) {
        // FIXME: not sure what this is supposed to be?
        totalvis += numvis;
    } else {
        for (i = 0; i < portalleafs_real; i++) {
            if (bsp->dleafs[i + 1].cluster == clusternum) {
                totalvis += numvis;
            }
        }
    }

    /* Allocate for worst case where RLE might grow the data (unlikely) */
    if (bsp->loadversion->game->id == GAME_QUAKE_II) {
        compressed = new uint8_t[max(1, (portalleafs * 2) / 8)];
        len = CompressRow(outbuffer, (portalleafs + 7) >> 3, compressed);
    } else {
        compressed = new uint8_t[max(1, (portalleafs_real * 2) / 8)];
        len = CompressRow(outbuffer, (portalleafs_real + 7) >> 3, compressed);
    }

    dest = vismap_p;
    vismap_p += len;

    if (vismap_p > vismap_end)
        FError("Vismap expansion overflow");

    /* leaf 0 is a common solid */
    int32_t visofs = dest - vismap;

    bsp->dvis.set_bit_offset(VIS_PVS, clusternum, visofs);

    // Set pointers
    if (bsp->loadversion->game->id == GAME_QUAKE_II) {
        for (i = 1; i < bsp->dleafs.size(); i++) {
            if (bsp->dleafs[i].cluster == clusternum) {
                bsp->dleafs[i].visofs = visofs;
            }
        }
    } else {
        for (i = 0; i < portalleafs_real; i++) {
            if (bsp->dleafs[i + 1].cluster == clusternum) {
                bsp->dleafs[i + 1].visofs = visofs;
            }
        }
    }

    memcpy(dest, compressed, len);
    delete[] compressed;
}

/*
  ==================
  CalcPortalVis
  ==================
*/
void CalcPortalVis(const mbsp_t *bsp)
{
    int i, startcount;
    portal_t *p;

    // fastvis just uses mightsee for a very loose bound
    if (options.fast.value()) {
        for (i = 0; i < numportals * 2; i++) {
            portals[i].visbits = portals[i].mightsee;
            portals[i].status = pstat_done;
        }
        return;
    }

    /*
     * Count the already completed portals in case we loaded previous state
     */
    startcount = 0;
    for (i = 0, p = portals; i < numportals * 2; i++, p++) {
        if (p->status == pstat_done)
            startcount++;
    }
    RunThreadsOn(startcount, numportals * 2, LeafThread, NULL);

    SaveVisState();

    LogPrint(
        LOG_VERBOSE, "portalcheck: {}  portaltest: {}  portalpass: {}\n", c_portalcheck, c_portaltest, c_portalpass);
    LogPrint(
        LOG_VERBOSE, "c_vistest: {}  c_mighttest: {}  c_mightseeupdate {}\n", c_vistest, c_mighttest, c_mightseeupdate);
}

/*
  ==================
  CalcVis
  ==================
*/
void CalcVis(mbsp_t *bsp)
{
    int i;

    if (LoadVisState()) {
        LogPrint("Loaded previous state. Resuming progress...\n");
    } else {
        LogPrint("Calculating Base Vis:\n");
        BasePortalVis();
    }

    LogPrint("Calculating Full Vis:\n");
    CalcPortalVis(bsp);

    //
    // assemble the leaf vis lists by oring and compressing the portal lists
    //
    LogPrint("Expanding clusters...\n");
    leafbits_t buffer(portalleafs);
    for (i = 0; i < portalleafs; i++) {
        ClusterFlow(i, buffer, bsp);
        buffer.clear();
    }

    int64_t avg = totalvis;

    if (bsp->loadversion->game->id == GAME_QUAKE_II) {
        avg /= static_cast<int64_t>(portalleafs);

        LogPrint("average clusters visible: {}\n", avg);
    } else {
        avg /= static_cast<int64_t>(portalleafs_real);

        LogPrint("average leafs visible: {}\n", avg);
    }
}

// ===========================================================================

/*
  ============
  LoadPortals
  ============
*/
static void LoadPortals(const std::filesystem::path &name, mbsp_t *bsp)
{
    int i, j, count;
    portal_t *p;
    leaf_t *l;
    char magic[80];
    int numpoints;
    int leafnums[2];
    qplane3d plane;
    qfile_t f = SafeOpenRead(name, true);

    /*
     * Parse the portal file header
     */
    count = fscanf(f.get(), "%79s\n", magic);
    if (count != 1)
        FError("unknown header: {}\n", magic);

    if (!strcmp(magic, PORTALFILE)) {
        count = fscanf(f.get(), "%i\n%i\n", &portalleafs, &numportals);
        if (count != 2)
            FError("unable to parse {} HEADER\n", PORTALFILE);

        if (bsp->loadversion->game->id == GAME_QUAKE_II) {
            // since q2bsp has native cluster support, we shouldn't look at portalleafs_real at all.
            portalleafs_real = 0;
            LogPrint("{:6} clusters\n", portalleafs);
            LogPrint("{:6} portals\n", numportals);
        } else {
            portalleafs_real = portalleafs;
            LogPrint("{:6} leafs\n", portalleafs);
            LogPrint("{:6} portals\n", numportals);
        }
    } else if (!strcmp(magic, PORTALFILE2)) {
        count = fscanf(f.get(), "%i\n%i\n%i\n", &portalleafs_real, &portalleafs, &numportals);
        if (count != 3)
            FError("unable to parse {} HEADER\n", PORTALFILE);
        if (bsp->loadversion->game->id == GAME_QUAKE_II) {
            FError("{} can not be used with Q2\n", PORTALFILE2);
        }
        LogPrint("{:6} leafs\n", portalleafs_real);
        LogPrint("{:6} clusters\n", portalleafs);
        LogPrint("{:6} portals\n", numportals);
    } else if (!strcmp(magic, PORTALFILEAM)) {
        count = fscanf(f.get(), "%i\n%i\n%i\n", &portalleafs, &numportals, &portalleafs_real);
        if (count != 3)
            FError("unable to parse {} HEADER\n", PORTALFILE);
        if (bsp->loadversion->game->id == GAME_QUAKE_II) {
            FError("{} can not be used with Q2\n", PORTALFILEAM);
        }
        LogPrint("{:6} leafs\n", portalleafs_real);
        LogPrint("{:6} clusters\n", portalleafs);
        LogPrint("{:6} portals\n", numportals);
    } else {
        FError("unknown header: {}\n", magic);
    }

    leafbytes = ((portalleafs + 63) & ~63) >> 3;
    leaflongs = leafbytes / sizeof(long);
    if (bsp->loadversion->game->id == GAME_QUAKE_II) {
        // not used in Q2
        leafbytes_real = 0;
    } else {
        leafbytes_real = ((portalleafs_real + 63) & ~63) >> 3;
    }

    // each file portal is split into two memory portals
    portals = new portal_t[numportals * 2]{};

    leafs = new leaf_t[portalleafs]{};

    if (bsp->loadversion->game->id == GAME_QUAKE_II) {
        originalvismapsize = portalleafs * ((portalleafs + 7) / 8);
    } else {
        originalvismapsize = portalleafs_real * ((portalleafs_real + 7) / 8);
    }

    bsp->dvis.resize(portalleafs);

    bsp->dvis.bits.resize(originalvismapsize * 2);

    vismap = vismap_p = bsp->dvis.bits.data();
    vismap_end = vismap + bsp->dvis.bits.size();

    for (i = 0, p = portals; i < numportals; i++) {
        if (fscanf(f.get(), "%i %i %i ", &numpoints, &leafnums[0], &leafnums[1]) != 3)
            FError("reading portal {}", i);
        if (numpoints > MAX_WINDING)
            FError("portal {} has too many points", i);
        if ((unsigned)leafnums[0] > (unsigned)portalleafs || (unsigned)leafnums[1] > (unsigned)portalleafs)
            FError("out of bounds leaf in portal {}", i);

        winding_t &w = *(p->winding = std::make_shared<winding_t>(numpoints));

        for (j = 0; j < numpoints; j++) {
            if (fscanf(f.get(), "(%lf %lf %lf ) ", &w[j][0], &w[j][1], &w[j][2]) != 3)
                FError("reading portal {}", i);
        }
        fscanf(f.get(), "\n");

        // calc plane
        plane = w.plane();

        // create forward portal
        l = &leafs[leafnums[0]];
        if (l->numportals == MAX_PORTALS_ON_LEAF)
            FError("Leaf with too many portals");
        l->portals[l->numportals] = p;
        l->numportals++;

        p->plane = -plane;
        p->leaf = leafnums[1];
        p->winding->SetWindingSphere();
        p++;

        // create backwards portal
        l = &leafs[leafnums[1]];
        if (l->numportals == MAX_PORTALS_ON_LEAF)
            FError("Leaf with too many portals");
        l->portals[l->numportals] = p;
        l->numportals++;

        // Create a reverse winding
        p->winding = std::make_shared<winding_t>(numpoints);

        for (j = 0; j < numpoints; ++j)
            p->winding->at(j) = w[numpoints - (j + 1)];

        p->plane = plane;
        p->leaf = leafnums[0];
        p->winding->SetWindingSphere();
        p++;
    }

    // Q2 doesn't need this, it's PRT1 has the data we need
    if (bsp->loadversion->game->id == GAME_QUAKE_II) {
        return;
    }

    // No clusters
    if (portalleafs == portalleafs_real) {
        // e.g. Quake 1, PRT1 (no func_detail).
        // Assign the identity cluster numbers for consistency
        for (i = 0; i < portalleafs; i++) {
            bsp->dleafs[i + 1].cluster = i;
        }
        return;
    }

    if (!strcmp(magic, PORTALFILE2)) {
        for (i = 0; i < portalleafs; i++) {
            while (1) {
                int leafnum;
                count = fscanf(f.get(), "%i", &leafnum);
                if (!count || count == EOF)
                    break;
                if (leafnum < 0)
                    break;
                if (leafnum >= portalleafs_real)
                    FError("Invalid leaf number in cluster map ({} >= {})", leafnum, portalleafs_real);
                bsp->dleafs[leafnum + 1].cluster = i;
            }
            if (count == EOF)
                break;
        }
        if (i < portalleafs)
            FError("Couldn't read cluster map ({} / {})\n", i, portalleafs);
    } else if (!strcmp(magic, PORTALFILEAM)) {
        for (i = 0; i < portalleafs_real; i++) {
            int clusternum;
            count = fscanf(f.get(), "%i", &clusternum);
            if (!count || count == EOF) {
                Error("Unexpected end of cluster map\n");
            }
            if (clusternum < 0 || clusternum >= portalleafs) {
                FError("Invalid cluster number {} in cluster map, number of clusters: {}\n", clusternum, portalleafs);
            }
            bsp->dleafs[i + 1].cluster = clusternum;
        }
    } else {
        FError("Unknown header {}\n", magic);
    }
}

/*
  ===========
  main
  ===========
*/
int main(int argc, const char **argv)
{
    bspdata_t bspdata;
    const bspversion_t *loadversion;

    InitLog("vis.log");
    LogPrint("---- vis / ericw-tools " stringify(ERICWTOOLS_VERSION) " ----\n");

    options.run(argc, argv);

    LowerProcessPriority();

    stateinterval = std::chrono::minutes(5); /* 5 minutes */
    starttime = statetime = I_FloatTime();

    LoadBSPFile(options.sourceMap, &bspdata);

    bspdata.version->game->init_filesystem(options.sourceMap);

    loadversion = bspdata.version;
    ConvertBSPFormat(&bspdata, &bspver_generic);

    mbsp_t &bsp = std::get<mbsp_t>(bspdata.bsp);

    portalfile = fs::path(options.sourceMap).replace_extension("prt");
    LoadPortals(portalfile, &bsp);

    statefile = fs::path(options.sourceMap).replace_extension("vis");
    statetmpfile = fs::path(options.sourceMap).replace_extension("vi0");

    if (bsp.loadversion->game->id != GAME_QUAKE_II) {
        uncompressed = new uint8_t[portalleafs * leafbytes_real]{};
    } else {
        uncompressed_q2 = new uint8_t[portalleafs * leafbytes]{};
    }

    CalcVis(&bsp);

    LogPrint("c_noclip: {}\n", c_noclip);
    LogPrint("c_chains: {}\n", c_chains);

    bsp.dvis.bits.resize(vismap_p - bsp.dvis.bits.data());
    bsp.dvis.bits.shrink_to_fit();
    LogPrint("visdatasize:{}  compressed from {}\n", bsp.dvis.bits.size(), originalvismapsize);

    // no ambient sounds for Q2
    if (bsp.loadversion->game->id != GAME_QUAKE_II) {
        LogPrint("---- CalcAmbientSounds ----\n");
        CalcAmbientSounds(&bsp);
    } else {
        LogPrint("---- CalcPHS ----\n");
        CalcPHS(&bsp);
    }

    /* Convert data format back if necessary */
    ConvertBSPFormat(&bspdata, loadversion);

    WriteBSPFile(options.sourceMap, &bspdata);

    endtime = I_FloatTime();
    LogPrint("{:.2} elapsed\n", (endtime - starttime));

    CloseLog();

    return 0;
}
