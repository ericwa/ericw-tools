// vis.c

#include <climits>
#include <cstddef>
#include <cstdint>

#include <vis/leafbits.hh>
#include <vis/vis.hh>
#include <common/log.hh>
#include <common/threads.hh>
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

/* Options - TODO: collect these in a struct */
bool fastvis;
static int verbose = 0;
int testlevel = 4;
bool ambientsky = true;
bool ambientwater = true;
bool ambientslime = true;
bool ambientlava = true;
int visdist = 0;
bool nostate = false;

std::filesystem::path sourcefile, portalfile, statefile, statetmpfile;

#if 0
void
NormalizePlane(plane_t *dp)
{
    vec_t ax, ay, az;

    if (dp->normal[0] == -1.0) {
        dp->normal[0] = 1.0;
        dp->dist = -dp->dist;
        return;
    }
    if (dp->normal[1] == -1.0) {
        dp->normal[1] = 1.0;
        dp->dist = -dp->dist;
        return;
    }
    if (dp->normal[2] == -1.0) {
        dp->normal[2] = 1.0;
        dp->dist = -dp->dist;
        return;
    }

    ax = fabs(dp->normal[0]);
    ay = fabs(dp->normal[1]);
    az = fabs(dp->normal[2]);

    if (ax >= ay && ax >= az) {
        if (dp->normal[0] < 0) {
            VectorSubtract(vec3_origin, dp->normal, dp->normal);
            dp->dist = -dp->dist;
        }
        return;
    }

    if (ay >= ax && ay >= az) {
        if (dp->normal[1] < 0) {
            VectorSubtract(vec3_origin, dp->normal, dp->normal);
            dp->dist = -dp->dist;
        }
        return;
    }

    if (dp->normal[2] < 0) {
        VectorSubtract(vec3_origin, dp->normal, dp->normal);
        dp->dist = -dp->dist;
    }
}
#endif

void PlaneFromWinding(const winding_t *w, plane_t *plane)
{
    vec3_t v1, v2;

    // calc plane
    VectorSubtract(w->points[2], w->points[1], v1);
    VectorSubtract(w->points[0], w->points[1], v2);
    CrossProduct(v2, v1, plane->normal);
    VectorNormalize(plane->normal);
    plane->dist = DotProduct(w->points[0], plane->normal);
}

//============================================================================

/*
  ==================
  NewWinding
  ==================
*/
winding_t *NewWinding(int points)
{
    winding_t *w;
    int size;

    if (points > MAX_WINDING)
        FError("{} points", points);

    // size = offsetof(winding_t, points[points]);
    size = offsetof(winding_t, points[0]);
    size += points * sizeof(w->points[0]);

    w = static_cast<winding_t *>(malloc(size));
    memset(w, 0, size);

    return w;
}

void LogWinding(const winding_t *w)
{
    int i;

    if (!verbose)
        return;

    for (i = 0; i < w->numpoints; i++)
        LogPrint("({:5.1}, {:5.1}, {:5.1})\n", w->points[i][0], w->points[i][1], w->points[i][2]);
}

void LogLeaf(const leaf_t *leaf)
{
    const portal_t *portal;
    const plane_t *plane;
    int i;

    if (!verbose)
        return;

    for (i = 0; i < leaf->numportals; i++) {
        portal = leaf->portals[i];
        plane = &portal->plane;
        LogPrint("portal {:4} to leaf {:4} : {:7.1} : ({:4.2}, {:4.2}, {:4.2})\n", (ptrdiff_t)(portal - portals), portal->leaf,
            plane->dist, plane->normal[0], plane->normal[1], plane->normal[2]);
    }
}

/*
  ==================
  CopyWinding
  ==================
*/
winding_t *CopyWinding(const winding_t *w)
{
    int size;
    winding_t *c;

    // size = offsetof(winding_t, points[w->numpoints]);
    size = offsetof(winding_t, points[0]);
    size += w->numpoints * sizeof(w->points[0]);

    c = static_cast<winding_t *>(malloc(size));
    memcpy(c, w, size);
    return c;
}

/*
  ==================
  AllocStackWinding

  Return a pointer to a free fixed winding on the stack
  ==================
*/
winding_t *AllocStackWinding(pstack_t *stack)
{
    int i;

    for (i = 0; i < STACK_WINDINGS; i++) {
        if (stack->freewindings[i]) {
            stack->freewindings[i] = 0;
            return &stack->windings[i];
        }
    }

    FError("failed");

    return NULL;
}

/*
  ==================
  FreeStackWinding

  As long as the winding passed in is local to the stack, free it. Otherwise,
  do nothing (the winding either belongs to a portal or another stack
  structure further up the call chain).
  ==================
*/
void FreeStackWinding(winding_t *w, pstack_t *stack)
{
    uintptr_t index = w - stack->windings;

    if (index < (uintptr_t)STACK_WINDINGS) {
        if (stack->freewindings[index])
            FError("winding already freed");
        stack->freewindings[index] = 1;
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
winding_t *ClipStackWinding(winding_t *in, pstack_t *stack, plane_t *split)
{
    vec_t dists[MAX_WINDING + 1];
    int sides[MAX_WINDING + 1];
    int counts[3];
    vec_t dot, fraction;
    int i, j;
    vec_t *p1, *p2;
    vec3_t mid;
    winding_t *neww;

    /* Fast test first */
    dot = DotProduct(in->origin, split->normal) - split->dist;
    if (dot < -in->radius) {
        FreeStackWinding(in, stack);
        return NULL;
    } else if (dot > in->radius) {
        return in;
    }

    if (in->numpoints > MAX_WINDING)
        FError("in->numpoints > MAX_WINDING ({} > {})", in->numpoints, MAX_WINDING);

    counts[0] = counts[1] = counts[2] = 0;

    /* determine sides for each point */
    for (i = 0; i < in->numpoints; i++) {
        dot = DotProduct(in->points[i], split->normal);
        dot -= split->dist;
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
    if (counts[SIDE_ON] == in->numpoints) {
        return in;
    }

    if (!counts[0]) {
        FreeStackWinding(in, stack);
        return NULL;
    }
    if (!counts[1])
        return in;

    neww = AllocStackWinding(stack);
    neww->numpoints = 0;
    VectorCopy(in->origin, neww->origin);
    neww->radius = in->radius;

    for (i = 0; i < in->numpoints; i++) {
        p1 = in->points[i];

        if (sides[i] == SIDE_ON) {
            if (neww->numpoints == MAX_WINDING_FIXED)
                goto noclip;
            VectorCopy(p1, neww->points[neww->numpoints]);
            neww->numpoints++;
            continue;
        }

        if (sides[i] == SIDE_FRONT) {
            if (neww->numpoints == MAX_WINDING_FIXED)
                goto noclip;
            VectorCopy(p1, neww->points[neww->numpoints]);
            neww->numpoints++;
        }

        if (sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i])
            continue;

        /* generate a split point */
        p2 = in->points[(i + 1) % in->numpoints];
        fraction = dists[i] / (dists[i] - dists[i + 1]);
        for (j = 0; j < 3; j++) {
            /* avoid round off error when possible */
            if (split->normal[j] == 1)
                mid[j] = split->dist;
            else if (split->normal[j] == -1)
                mid[j] = -split->dist;
            else
                mid[j] = p1[j] + fraction * (p2[j] - p1[j]);
        }

        if (neww->numpoints == MAX_WINDING_FIXED)
            goto noclip;
        VectorCopy(mid, neww->points[neww->numpoints]);
        neww->numpoints++;
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
{;
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

        if (verbose > 1) {
            LogPrint("portal:{:4}  mightsee:{:4}  cansee:{:4}\n", (ptrdiff_t)(p - portals), p->nummightsee, p->numcansee);
        }
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

static void LeafFlow(int leafnum, mleaf_t *dleaf, const mbsp_t *bsp)
{
    leaf_t *leaf;
    uint8_t *outbuffer;
    uint8_t *compressed;
    int i, j, shift, len;
    int numvis;
    uint8_t *dest;
    const portal_t *p;

    /*
     * flow through all portals, collecting visible bits
     */
    outbuffer = (bsp->loadversion->game->id == GAME_QUAKE_II ? uncompressed_q2 : uncompressed) + leafnum * leafbytes;
    leaf = &leafs[leafnum];
    for (i = 0; i < leaf->numportals; i++) {
        p = leaf->portals[i];
        if (p->status != pstat_done)
            FError("portal not done");
        for (j = 0; j < leafbytes; j++) {
            shift = (j << 3) & leafbits_t::mask;
            outbuffer[j] |= (p->visbits.data()[j >> (leafbits_t::shift - 3)] >> shift) & 0xff;
        }
    }

    if (outbuffer[leafnum >> 3] & (1 << (leafnum & 7)))
        LogPrint("WARNING: Leaf portals saw into leaf ({})\n", leafnum);
    outbuffer[leafnum >> 3] |= (1 << (leafnum & 7));

    numvis = 0;
    for (i = 0; i < portalleafs; i++)
        if (outbuffer[i >> 3] & (1 << (i & 3)))
            numvis++;

    /*
     * compress the bit string
     */
    if (verbose > 1)
        LogPrint("leaf {:4} : {:4} visible\n", leafnum, numvis);
    totalvis += numvis;

    /* Allocate for worst case where RLE might grow the data (unlikely) */
    compressed = new uint8_t[(portalleafs * 2) / 8];
    len = CompressRow(outbuffer, (portalleafs + 7) >> 3, compressed);

    dest = vismap_p;
    vismap_p += len;

    if (vismap_p > vismap_end)
        FError("Vismap expansion overflow");

    /* leaf 0 is a common solid */
    dleaf->visofs = dest - vismap;

    memcpy(dest, compressed, len);
    delete[] compressed;
}

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
    if (verbose > 1)
        LogPrint("cluster {:4} : {:4} visible\n", clusternum, numvis);

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
        compressed = new uint8_t[(portalleafs * 2) / 8];
        len = CompressRow(outbuffer, (portalleafs + 7) >> 3, compressed);
    } else {
        compressed = new uint8_t[(portalleafs_real * 2) / 8];
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
    // TODO: get rid of, we'll copy this data over from dvis
    // during conversion
    for (i = 1; i < bsp->dleafs.size(); i++) {
        if (bsp->dleafs[i].cluster == clusternum) {
            bsp->dleafs[i].visofs = visofs;
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
    if (fastvis) {
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

    if (verbose) {
        LogPrint("portalcheck: {}  portaltest: {}  portalpass: {}\n", c_portalcheck, c_portaltest, c_portalpass);
        LogPrint("c_vistest: {}  c_mighttest: {}  c_mightseeupdate {}\n", c_vistest, c_mighttest, c_mightseeupdate);
    }
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
    if (portalleafs == portalleafs_real && bsp->loadversion->game->id != GAME_QUAKE_II) {
        // Legacy, non-detail Q1 vis codepath
        // FIXME: Should be possible to remove this and just use ClusterFlow even on Q1 maps
        // with no detail.
        for (i = 0; i < portalleafs; i++)
            LeafFlow(i, &bsp->dleafs[i + 1], bsp);
    } else {
        LogPrint("Expanding clusters...\n");
        leafbits_t buffer(portalleafs);
        for (i = 0; i < portalleafs; i++) {
            ClusterFlow(i, buffer, bsp);
            buffer.clear();
        }
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

/*
  ============================================================================
  PASSAGE CALCULATION (not used yet...)
  ============================================================================
*/

int count_sep;

bool PlaneCompare(plane_t *p1, plane_t *p2)
{
    int i;

    if (fabs(p1->dist - p2->dist) > 0.01)
        return false;

    for (i = 0; i < 3; i++)
        if (fabs(p1->normal[i] - p2->normal[i]) > 0.001)
            return false;

    return true;
}

sep_t *Findpassages(winding_t *source, winding_t *pass)
{
    int i, j, k, l;
    plane_t plane;
    vec3_t v1, v2;
    float d;
    double length;
    int counts[3];
    bool fliptest;
    sep_t *sep, *list;

    list = NULL;

    // check all combinations
    for (i = 0; i < source->numpoints; i++) {
        l = (i + 1) % source->numpoints;
        VectorSubtract(source->points[l], source->points[i], v1);

        // fing a vertex of pass that makes a plane that puts all of the
        // vertexes of pass on the front side and all of the vertexes of
        // source on the back side
        for (j = 0; j < pass->numpoints; j++) {
            VectorSubtract(pass->points[j], source->points[i], v2);

            plane.normal[0] = v1[1] * v2[2] - v1[2] * v2[1];
            plane.normal[1] = v1[2] * v2[0] - v1[0] * v2[2];
            plane.normal[2] = v1[0] * v2[1] - v1[1] * v2[0];

            // if points don't make a valid plane, skip it

            length = plane.normal[0] * plane.normal[0] + plane.normal[1] * plane.normal[1] +
                     plane.normal[2] * plane.normal[2];

            if (length < ON_EPSILON)
                continue;

            length = 1 / sqrt(length);

            plane.normal[0] *= (vec_t)length;
            plane.normal[1] *= (vec_t)length;
            plane.normal[2] *= (vec_t)length;

            plane.dist = DotProduct(pass->points[j], plane.normal);

            //
            // find out which side of the generated seperating plane has the
            // source portal
            //
            fliptest = false;
            for (k = 0; k < source->numpoints; k++) {
                if (k == i || k == l)
                    continue;
                d = DotProduct(source->points[k], plane.normal) - plane.dist;
                if (d < -ON_EPSILON) { // source is on the negative side, so we want all
                    // pass and target on the positive side
                    fliptest = false;
                    break;
                } else if (d > ON_EPSILON) { // source is on the positive side, so we want all
                    // pass and target on the negative side
                    fliptest = true;
                    break;
                }
            }
            if (k == source->numpoints)
                continue; // planar with source portal

            //
            // flip the normal if the source portal is backwards
            //
            if (fliptest) {
                VectorSubtract(vec3_origin, plane.normal, plane.normal);
                plane.dist = -plane.dist;
            }
            //
            // if all of the pass portal points are now on the positive side,
            // this is the seperating plane
            //
            counts[0] = counts[1] = counts[2] = 0;
            for (k = 0; k < pass->numpoints; k++) {
                if (k == j)
                    continue;
                d = DotProduct(pass->points[k], plane.normal) - plane.dist;
                if (d < -ON_EPSILON)
                    break;
                else if (d > ON_EPSILON)
                    counts[0]++;
                else
                    counts[2]++;
            }
            if (k != pass->numpoints)
                continue; // points on negative side, not a seperating plane

            if (!counts[0])
                continue; // planar with pass portal

            //
            // save this out
            //
            count_sep++;

            sep = new sep_t;
            sep->next = list;
            list = sep;
            sep->plane = plane;
        }
    }

    return list;
}

/*
  ============
  CalcPassages
  ============
*/
void CalcPassages(void)
{
    int i, j, k;
    int count, count2;
    leaf_t *l;
    portal_t *p1, *p2;
    sep_t *sep;
    passage_t *passages;

    LogPrint("building passages...\n");

    count = count2 = 0;
    for (i = 0; i < portalleafs; i++) {
        l = &leafs[i];

        for (j = 0; j < l->numportals; j++) {
            p1 = l->portals[j];
            for (k = 0; k < l->numportals; k++) {
                if (k == j)
                    continue;

                count++;
                p2 = l->portals[k];

                // definately can't see into a coplanar portal
                if (PlaneCompare(&p1->plane, &p2->plane))
                    continue;

                count2++;

                sep = Findpassages(p1->winding, p2->winding);
                if (!sep) {
                    //                    Error ("No seperating planes found in portal pair");
                    count_sep++;
                    sep = new sep_t;
                    sep->next = NULL;
                    sep->plane = p1->plane;
                }
                passages = new passage_t;
                passages->planes = sep;
                passages->from = p1->leaf;
                passages->to = p2->leaf;
                passages->next = l->passages;
                l->passages = passages;
            }
        }
    }

    LogPrint("numpassages: {} ({})\n", count2, count);
    LogPrint("total passages: {}\n", count_sep);
}

// ===========================================================================

static void SetWindingSphere(winding_t *w)
{
    int i;
    vec3_t origin, dist;
    vec_t r, max_r;

    VectorCopy(vec3_origin, origin);
    for (i = 0; i < w->numpoints; i++)
        VectorAdd(origin, w->points[i], origin);
    VectorScale(origin, 1.0 / w->numpoints, origin);

    max_r = 0;
    for (i = 0; i < w->numpoints; i++) {
        VectorSubtract(w->points[i], origin, dist);
        r = VectorLength(dist);
        if (r > max_r)
            max_r = r;
    }

    VectorCopy(origin, w->origin);
    w->radius = max_r;
}

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
    qfile_t f { nullptr, nullptr };
    int numpoints;
    winding_t *w;
    int leafnums[2];
    plane_t plane;

    if (name == "-")
        f = { stdin, nullptr };
    else {
        f = SafeOpenRead(name, true);
    }

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
    portals = new portal_t[numportals * 2] { };

    leafs = new leaf_t[portalleafs] { };

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

        w = p->winding = NewWinding(numpoints);
        w->numpoints = numpoints;

        for (j = 0; j < numpoints; j++) {
            if (fscanf(f.get(), "(%lf %lf %lf ) ", &w->points[j][0], &w->points[j][1], &w->points[j][2]) != 3)
                FError("reading portal {}", i);
        }
        fscanf(f.get(), "\n");

        // calc plane
        PlaneFromWinding(w, &plane);

        // create forward portal
        l = &leafs[leafnums[0]];
        if (l->numportals == MAX_PORTALS_ON_LEAF)
            FError("Leaf with too many portals");
        l->portals[l->numportals] = p;
        l->numportals++;

        p->winding = w;
        VectorSubtract(vec3_origin, plane.normal, p->plane.normal);
        p->plane.dist = -plane.dist;
        p->leaf = leafnums[1];
        SetWindingSphere(p->winding);
        p++;

        // create backwards portal
        l = &leafs[leafnums[1]];
        if (l->numportals == MAX_PORTALS_ON_LEAF)
            FError("Leaf with too many portals");
        l->portals[l->numportals] = p;
        l->numportals++;

        // Create a reverse winding
        p->winding = NewWinding(numpoints);
        p->winding->numpoints = numpoints;
        for (j = 0; j < numpoints; ++j)
            VectorCopy(w->points[numpoints - (j + 1)], p->winding->points[j]);

        // p->winding = w;
        p->plane = plane;
        p->leaf = leafnums[0];
        SetWindingSphere(p->winding);
        p++;
    }

    // Q2 doesn't need this, it's PRT1 has the data we need
    if (bsp->loadversion->game->id == GAME_QUAKE_II) {
        return;
    }
    
    // No clusters
    if (portalleafs == portalleafs_real) {
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
                FError(
                    "Invalid cluster number {} in cluster map, number of clusters: {}\n", clusternum, portalleafs);
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
int main(int argc, char **argv)
{
    bspdata_t bspdata;
    const bspversion_t *loadversion;
    int i;

    InitLog("vis.log");
    LogPrint("---- vis / ericw-tools " stringify(ERICWTOOLS_VERSION) " ----\n");

    LowerProcessPriority();
    numthreads = GetDefaultThreads();

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-threads")) {
            numthreads = atoi(argv[i + 1]);
            i++;
        } else if (!strcmp(argv[i], "-fast")) {
            LogPrint("fastvis = true\n");
            fastvis = true;
        } else if (!strcmp(argv[i], "-level")) {
            testlevel = atoi(argv[i + 1]);
            i++;
        } else if (!strcmp(argv[i], "-v")) {
            LogPrint("verbose = true\n");
            verbose = 1;
        } else if (!strcmp(argv[i], "-vv")) {
            LogPrint("verbose = extra\n");
            verbose = 2;
        } else if (!strcmp(argv[i], "-noambientsky")) {
            LogPrint("ambient sky sounds disabled\n");
            ambientsky = false;
        } else if (!strcmp(argv[i], "-noambientwater")) {
            LogPrint("ambient water sounds disabled\n");
            ambientwater = false;
        } else if (!strcmp(argv[i], "-noambientslime")) {
            LogPrint("ambient slime sounds disabled\n");
            ambientslime = false;
        } else if (!strcmp(argv[i], "-noambientlava")) {
            LogPrint("ambient lava sounds disabled\n");
            ambientlava = false;
        } else if (!strcmp(argv[i], "-noambient")) {
            LogPrint("ambient sound calculation disabled\n");
            ambientsky = false;
            ambientwater = false;
            ambientslime = false;
            ambientlava = false;
        } else if (!strcmp(argv[i], "-visdist")) {
            visdist = atoi(argv[i + 1]);
            i++;
            LogPrint("visdist = {}\n", visdist);
        } else if (!strcmp(argv[i], "-nostate")) {
            LogPrint("loading from state file disabled\n");
            nostate = true;
        } else if (argv[i][0] == '-')
            FError("Unknown option \"{}\"", argv[i]);
        else
            break;
    }

    if (i != argc - 1) {
        printf("usage: vis [-threads #] [-level 0-4] [-fast] [-v|-vv] "
               "[-credits] bspfile\n");
        exit(1);
    }

    LogPrint("running with {} threads\n", numthreads);
    LogPrint("testlevel = {}\n", testlevel);

    stateinterval = std::chrono::minutes(5); /* 5 minutes */
    starttime = statetime = I_FloatTime();

    std::filesystem::path path_base(argv[i]);
    sourcefile = DefaultExtension(path_base, "bsp");

    LoadBSPFile(sourcefile, &bspdata);

    loadversion = bspdata.version;
    ConvertBSPFormat(&bspdata, &bspver_generic);

    mbsp_t &bsp = std::get<mbsp_t>(bspdata.bsp);

    portalfile = path_base.replace_extension("prt");

    LoadPortals(portalfile, &bsp);

    statefile = path_base.replace_extension("vis");
    statetmpfile = path_base.replace_extension("vi0");

    if (bsp.loadversion->game->id != GAME_QUAKE_II) {
        uncompressed = new uint8_t[portalleafs * leafbytes_real] { };
    } else {
        uncompressed_q2 = new uint8_t[portalleafs * leafbytes] { };
    }

    //    CalcPassages ();

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

    WriteBSPFile(sourcefile, &bspdata);

    endtime = I_FloatTime();
    LogPrint("{:.2} elapsed\n", (endtime - starttime));

    CloseLog();

    return 0;
}
