// vis.c

#include <climits>
#include <cstddef>
#include <cstdint>

#include <vis/leafbits.hh>
#include <vis/vis.hh>
#include <common/log.hh>
#include <common/bsputils.hh>
#include <common/threads.hh>
#include <common/fs.hh>
#include <common/parallel.hh>
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

static std::vector<uint8_t> vismap;

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
    token_parser_t p(argc - 1, argv + 1);
    auto remainder = parse(p);

    if (remainder.size() <= 0 || remainder.size() > 1) {
        printHelp();
    }

    sourceMap = DefaultExtension(remainder[0], "bsp");
}
} // namespace settings

settings::vis_settings options;

fs::path portalfile, statefile, statetmpfile;

/*
  ==================
  AllocStackWinding

  Return a pointer to a free fixed winding on the stack
  ==================
*/
winding_t *AllocStackWinding(pstack_t &stack)
{
    for (size_t i = 0; i < STACK_WINDINGS; i++) {
        if (!stack.windings_used[i]) {
            stack.windings[i].clear();
            stack.windings_used[i] = true;
            return &stack.windings[i];
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
void FreeStackWinding(winding_t *&w, pstack_t &stack)
{
    if (w >= stack.windings && w <= &stack.windings[STACK_WINDINGS]) {
        stack.windings_used[w - stack.windings] = false;
        w = nullptr;
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
winding_t *ClipStackWinding(winding_t *in, pstack_t &stack, const qplane3d &split)
{
    vec_t *dists = (vec_t *)alloca(sizeof(vec_t) * (in->size() + 1));
    int *sides = (int *)alloca(sizeof(int) * (in->size() + 1));
    int counts[3];
    int i, j;

    /* Fast test first */
    vec_t dot = split.distance_to(in->origin);
    if (dot < -in->radius) {
        FreeStackWinding(in, stack);
        return nullptr;
    } else if (dot > in->radius) {
        return in;
    }

    if (in->size() > MAX_WINDING)
        FError("in->numpoints > MAX_WINDING ({} > {})", in->size(), MAX_WINDING);

    counts[0] = counts[1] = counts[2] = 0;

    /* determine sides for each point */
    for (i = 0; i < in->size(); i++) {
        dot = split.distance_to((*in)[i]);
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
        return nullptr;
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
            if (split.normal[j] == 1)
                mid[j] = split.dist;
            else if (split.normal[j] == -1)
                mid[j] = -split.dist;
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

#include <mutex>

static std::mutex portal_mutex;
static std::atomic_int64_t portalIndex;

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

    portal_mutex.lock();

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
    }

    portal_mutex.unlock();

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
static void UpdateMightsee(const leaf_t &source, const leaf_t &dest)
{
    size_t leafnum = &dest - leafs;
    for (size_t i = 0; i < source.numportals; i++) {
        portal_t *p = source.portals[i];
        if (p->status != pstat_none) {
            continue;
        }
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
    const uint32_t *might, *vis;
    uint32_t changed;

    portal_mutex.lock();

    completed->status = pstat_done;

    /*
     * For each portal on the leaf, check the leafs we eliminated from
     * mightsee during the full vis so far.
     */
    const leaf_t &myleaf = leafs[completed->leaf];
    for (i = 0; i < myleaf.numportals; i++) {
        p = myleaf.portals[i];
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
            for (k = 0; k < myleaf.numportals; k++) {
                if (k == i)
                    continue;
                p2 = myleaf.portals[k];
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
                changed &= ~nth_bit(bit);
                leafnum = (j << leafbits_t::shift) + bit;
                UpdateMightsee(leafs[leafnum], myleaf);
            }
        }
    }

    portal_mutex.unlock();
}

time_point starttime, endtime, statetime;
static duration stateinterval;

/*
  ==============
  LeafThread
  ==============
*/
void LeafThread(size_t)
{
    portal_t *p;

    portal_mutex.lock();
    /* Save state if sufficient time has elapsed */
    auto now = I_FloatTime();
    if (now > statetime + stateinterval) {
        statetime = now;
        SaveVisState();
    }
    portal_mutex.unlock();

    p = GetNextPortal();
    if (!p)
        return;

    PortalFlow(p);

    PortalCompleted(p);

    logging::print(logging::flag::VERBOSE, "portal:{:4}  mightsee:{:4}  cansee:{:4}\n", (ptrdiff_t)(p - portals), p->nummightsee,
        p->numcansee);
}

/*
  ===============
  LeafFlow

  Builds the entire visibility list for a leaf
  ===============
*/
int64_t totalvis;

static std::vector<uint8_t> compressed;

static void ClusterFlow(int clusternum, leafbits_t &buffer, mbsp_t *bsp)
{
    leaf_t *leaf;
    uint8_t *outbuffer;
    int i, j;
    int numvis, numblocks;
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
    //    logging::print("WARNING: Leaf portals saw into cluster ({})\n", clusternum);

    buffer[clusternum] = true;

    /*
     * Now expand the clusters into the full leaf visibility map
     */
    numvis = 0;

    if (bsp->loadversion->game->id == GAME_QUAKE_II) {
        outbuffer = uncompressed_q2 + clusternum * leafbytes;
        for (i = 0; i < portalleafs; i++) {
            if (buffer[i]) {
                outbuffer[i >> 3] |= nth_bit(i & 7);
                numvis++;
            }
        }
    } else {
        outbuffer = uncompressed + clusternum * leafbytes_real;
        for (i = 0; i < portalleafs_real; i++) {
            if (buffer[bsp->dleafs[i + 1].cluster]) {
                outbuffer[i >> 3] |= nth_bit(i & 7);
                numvis++;
            }
        }
    }

    /*
     * compress the bit string
     */
    logging::print(logging::flag::VERBOSE, "cluster {:4} : {:4} visible\n", clusternum, numvis);

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

    compressed.clear();

    /* Allocate for worst case where RLE might grow the data (unlikely) */
    if (bsp->loadversion->game->id == GAME_QUAKE_II) {
        CompressRow(outbuffer, (portalleafs + 7) >> 3, std::back_inserter(compressed));
    } else {
        CompressRow(outbuffer, (portalleafs_real + 7) >> 3, std::back_inserter(compressed));
    }

    /* leaf 0 is a common solid */
    int32_t visofs = vismap.size();

    bsp->dvis.set_bit_offset(VIS_PVS, clusternum, visofs);

    // Set pointers
    if (bsp->loadversion->game->id != GAME_QUAKE_II) {
        for (i = 0; i < portalleafs_real; i++) {
            if (bsp->dleafs[i + 1].cluster == clusternum) {
                bsp->dleafs[i + 1].visofs = visofs;
            }
        }
    }

    std::copy(compressed.begin(), compressed.end(), std::back_inserter(vismap));
}

/*
  ==================
  CalcPortalVis
  ==================
*/
void CalcPortalVis(const mbsp_t *bsp)
{
    int i;
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
    int32_t startcount = 0;
    for (i = 0, p = portals; i < numportals * 2; i++, p++) {
        if (p->status == pstat_done)
            startcount++;
    }
    portalIndex = startcount;
    logging::parallel_for(startcount, numportals * 2, LeafThread);

    SaveVisState();

    logging::print(
        logging::flag::VERBOSE, "portalcheck: {}  portaltest: {}  portalpass: {}\n", c_portalcheck, c_portaltest, c_portalpass);
    logging::print(
        logging::flag::VERBOSE, "c_vistest: {}  c_mighttest: {}  c_mightseeupdate {}\n", c_vistest, c_mighttest, c_mightseeupdate);
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
        logging::print("Loaded previous state. Resuming progress...\n");
    } else {
        logging::print("Calculating Base Vis:\n");
        BasePortalVis();
    }

    logging::print("Calculating Full Vis:\n");
    CalcPortalVis(bsp);

    //
    // assemble the leaf vis lists by oring and compressing the portal lists
    //
    logging::print("Expanding clusters...\n");
    leafbits_t buffer(portalleafs);
    for (i = 0; i < portalleafs; i++) {
        ClusterFlow(i, buffer, bsp);
        buffer.clear();
    }

    int64_t avg = totalvis;

    if (bsp->loadversion->game->id == GAME_QUAKE_II) {
        avg /= static_cast<int64_t>(portalleafs);

        logging::print("average clusters visible: {}\n", avg);
    } else {
        avg /= static_cast<int64_t>(portalleafs_real);

        logging::print("average leafs visible: {}\n", avg);
    }
}

// ===========================================================================

#include <fstream>
#include <common/prtfile.hh>

/*
  ============
  LoadPortals
  ============
*/
static void LoadPortals(const fs::path &name, mbsp_t *bsp)
{
    const prtfile_t prtfile = LoadPrtFile(name, bsp->loadversion);

    int i;
    portal_t *p;
    leaf_t *l;
    qplane3d plane;

    portalleafs = prtfile.portalleafs;
    portalleafs_real = prtfile.portalleafs_real;

    /* Allocate for worst case where RLE might grow the data (unlikely) */
    if (bsp->loadversion->game->id == GAME_QUAKE_II) {
        compressed.reserve(max(1, (portalleafs * 2) / 8));
    } else {
        compressed.reserve(max(1, (portalleafs_real * 2) / 8));
    }

    numportals = prtfile.portals.size();

    if (bsp->loadversion->game->id != GAME_QUAKE_II) {
        // since q2bsp has native cluster support, we shouldn't look at portalleafs_real at all.
        logging::print("{:6} leafs\n", portalleafs_real);
    }
    logging::print("{:6} clusters\n", portalleafs);
    logging::print("{:6} portals\n", numportals);

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

    vismap.reserve(originalvismapsize * 2);

    for (i = 0, p = portals; i < numportals; i++) {
        const auto &sourceportal = prtfile.portals[i];
        p->winding = winding_t{sourceportal.winding.begin(), sourceportal.winding.end()};

        // calc plane
        plane = p->winding.plane();

        // create forward portal
        l = &leafs[sourceportal.leafnums[0]];
        if (l->numportals == MAX_PORTALS_ON_LEAF)
            FError("Leaf with too many portals");
        l->portals[l->numportals] = p;
        l->numportals++;

        p->plane = -plane;
        p->leaf = sourceportal.leafnums[1];
        p->winding.SetWindingSphere();
        p++;

        // create backwards portal
        l = &leafs[sourceportal.leafnums[1]];
        if (l->numportals == MAX_PORTALS_ON_LEAF)
            FError("Leaf with too many portals");
        l->portals[l->numportals] = p;
        l->numportals++;

        // Create a reverse winding
        const auto flipped = sourceportal.winding.flip();
        p->winding = winding_t{flipped.begin(), flipped.end()};
        p->plane = plane;
        p->leaf = sourceportal.leafnums[0];
        p->winding.SetWindingSphere();
        p++;
    }

    // Q2 doesn't need this, it's PRT1 has the data we need
    if (bsp->loadversion->game->id == GAME_QUAKE_II) {
        return;
    }

    // Copy cluster mapping from .prt file
    for (int i = 1; i < prtfile.dleafinfos.size(); ++i) {
        bsp->dleafs[i].cluster = prtfile.dleafinfos[i].cluster;
    }
}

int vis_main(int argc, const char **argv)
{
    bspdata_t bspdata;
    const bspversion_t *loadversion;

    options.run(argc, argv);

    logging::init(fs::path(options.sourceMap).replace_filename(options.sourceMap.stem().string() + "-vis").replace_extension("log"), options);

    stateinterval = std::chrono::minutes(5); /* 5 minutes */
    starttime = statetime = I_FloatTime();

    LoadBSPFile(options.sourceMap, &bspdata);

    bspdata.version->game->init_filesystem(options.sourceMap, options);

    loadversion = bspdata.version;
    ConvertBSPFormat(&bspdata, &bspver_generic);

    mbsp_t &bsp = std::get<mbsp_t>(bspdata.bsp);

    if (options.phsonly.value())
    {
        if (bsp.loadversion->game->id != GAME_QUAKE_II)
        {
            logging::print("error: need a Q2-esque BSP for -phsonly");
        }
        else
        {
            portalleafs = bsp.dvis.bit_offsets.size();
            leafbytes = ((portalleafs + 63) & ~63) >> 3;
            leaflongs = leafbytes / sizeof(long);

            if (bsp.loadversion->game->id == GAME_QUAKE_II) {
                originalvismapsize = portalleafs * ((portalleafs + 7) / 8);
            }
        }
    }
    else
    {
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

        logging::print("c_noclip: {}\n", c_noclip);
        logging::print("c_chains: {}\n", c_chains);

        bsp.dvis.bits = std::move(vismap);
        bsp.dvis.bits.shrink_to_fit();
        logging::print("visdatasize:{}  compressed from {}\n", bsp.dvis.bits.size(), originalvismapsize);
    }

    // no ambient sounds for Q2
    if (bsp.loadversion->game->id != GAME_QUAKE_II) {
        logging::print("---- CalcAmbientSounds ----\n");
        CalcAmbientSounds(&bsp);
    } else {
        logging::print("---- CalcPHS ----\n");
        CalcPHS(&bsp);
    }

    /* Convert data format back if necessary */
    ConvertBSPFormat(&bspdata, loadversion);

    WriteBSPFile(options.sourceMap, &bspdata);

    endtime = I_FloatTime();
    logging::print("{:.2} elapsed\n", (endtime - starttime));

    logging::close();

    return 0;
}
