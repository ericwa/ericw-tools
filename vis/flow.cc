#include <vis/vis.hh>
#include <vis/leafbits.hh>
#include <common/log.hh>
#include <common/parallel.hh>

/*
  ==============
  ClipToSeparators

  Source, pass, and target are an ordering of portals.

  Generates separating planes canidates by taking two points from source and
  one point from pass, and clips target by them.

  If target is totally clipped away, that portal can not be seen through.

  Normal clip keeps target on the same side as pass, which is correct
  if the order goes source, pass, target. If the order goes pass,
  source, target then we flip the clipping plane. Test levels 0 and 2
  use the 'normal', while 1 and 3 require the separating plane flip.

  Note that when passing in the 'source' plane, taking a copy, rather than a
  pointer, was measurably faster
  ==============
*/
static void ClipToSeparators(visstats_t &stats, const viswinding_t *source, const qplane3d src_pl, const viswinding_t *pass,
    viswinding_t *&target, unsigned int test, pstack_t &stack)
{
    // check all combinations
    for (size_t i = 0; i < source->size(); i++) {
        const size_t l = (i + 1) % source->size();
        const qvec3d v1 = source->at(l) - source->at(i);

        // find a vertex of pass that makes a plane that puts all of the
        // vertexes of pass on the front side and all of the vertexes of
        // source on the back side
        for (size_t j = 0; j < pass->size(); j++) {

            // Which side of the source portal is this point?
            // This also tells us which side of the separating plane has
            //  the source portal.
            bool fliptest;
            vec_t d = src_pl.distance_to(pass->at(j));
            if (d < -VIS_ON_EPSILON)
                fliptest = true;
            else if (d > VIS_ON_EPSILON)
                fliptest = false;
            else
                continue; // Point lies in source plane

            // Make a plane with the three points
            qplane3d sep;
            const qvec3d v2 = pass->at(j) - source->at(i);
            sep.normal = qv::cross(v1, v2);
            const vec_t len_sq = qv::length2(sep.normal);

            // If points don't make a valid plane, skip it.
            if (len_sq < VIS_ON_EPSILON)
                continue;

            sep.normal *= (1.0 / sqrt(len_sq));
            sep.dist = qv::dot(pass->at(j), sep.normal);

            //
            // flip the plane if the source portal is backwards
            //
            if (fliptest) {
                sep = -sep;
            }
            //
            // if all of the pass portal points are now on the positive side,
            // this is the separating plane
            //
            int count = 0;
            size_t k = 0;
            for (; k < pass->size(); k++) {
                if (k == j)
                    continue;
                d = sep.distance_to(pass->at(k));
                if (d < -VIS_ON_EPSILON)
                    break;
                else if (d > VIS_ON_EPSILON)
                    ++count;
            }
            if (k != pass->size())
                continue; // points on negative side, not a separating plane
            if (!count)
                continue; // planar with separating plane

            //
            // flip the normal if we want the back side (tests 1 and 3)
            //
            if (test & 1) {
                sep = -sep;
            }

            /* Cache separating planes for tests 0, 1 */
            if (test < 2) {
                if (stack.numseparators[test] == MAX_SEPARATORS)
                    FError("MAX_SEPARATORS");
                stack.separators[test][stack.numseparators[test]] = sep;
                stack.numseparators[test]++;
            }

            target = ClipStackWinding(stats, target, stack, sep);

            if (!target)
                return; // target is not visible

            break;
        }
    }
}

static int CheckStack(leaf_t *leaf, threaddata_t *thread)
{
    for (pstack_t *p = thread->pstack_head.next; p; p = p->next)
        if (p->leaf == leaf)
            return 1;
    return 0;
}

/*
  ==================
  RecursiveLeafFlow

  Flood fill through the leafs
  If src_portal is NULL, this is the originating leaf
  ==================
*/
static void RecursiveLeafFlow(int leafnum, threaddata_t *thread, pstack_t &prevstack)
{
    pstack_t stack;

    ++thread->stats.c_chains;

    leaf_t *leaf = &leafs[leafnum];

    /*
     * Check we haven't recursed into a leaf already on the stack
     */
    if (CheckStack(leaf, thread)) {
        logging::funcprint("WARNING: recursion on leaf {}\n", leafnum);
        return;
    }

    // mark the leaf as visible
    if (!thread->leafvis[leafnum]) {
        thread->leafvis[leafnum] = true;
        thread->base->numcansee++;
    }

    prevstack.next = &stack;

    stack.next = nullptr;
    stack.leaf = leaf;
    stack.portal = nullptr;
    stack.numseparators[0] = 0;
    stack.numseparators[1] = 0;

    for (int i = 0; i < STACK_WINDINGS; i++)
        stack.windings_used[i] = false;

    leafbits_t local(portalleafs);
    stack.mightsee = &local;

    const auto might = stack.mightsee->data();
    const auto vis = thread->leafvis.data();

    // check all portals for flowing into other leafs
    for (visportal_t *p : leaf->portals) {
        if (!(*prevstack.mightsee)[p->leaf]) {
            thread->stats.c_leafskip++;
            continue; // can't possibly see it
        }

        uint32_t *test;

        // if the portal can't see anything we haven't allready seen, skip it
        if (p->status == pstat_done) {
            thread->stats.c_vistest++;
            test = p->visbits.data();
        } else {
            thread->stats.c_mighttest++;
            test = p->mightsee.data();
        }

        uint32_t more = 0;
        const int numblocks = (portalleafs + leafbits_t::mask) >> leafbits_t::shift;
        for (int j = 0; j < numblocks; j++) {
            might[j] = prevstack.mightsee->data()[j] & test[j];
            more |= (might[j] & ~vis[j]);
        }

        if (!more) {
            // can't see anything new
            thread->stats.c_portalskip++;
            continue;
        }
        // get plane of portal, point normal into the neighbor leaf
        stack.portalplane = p->plane;
        const qplane3d backplane = -p->plane;

        if (qv::epsilonEqual(prevstack.portalplane.normal, backplane.normal, VIS_EQUAL_EPSILON))
            continue; // can't go out a coplanar face

        thread->stats.c_portalcheck++;

        stack.portal = p;
        stack.next = nullptr;

        /*
         * Testing visibility of a target portal, from a source portal,
         * looking through a pass portal.
         *
         *    source portal  =>  pass portal      =>  target portal
         *    stack.source   =>  prevstack->pass  =>  stack.pass
         *
         * If we can see part of the target portal, we use that clipped portal
         * as the pass portal into the next leaf.
         */

        /* Clip any part of the target portal behind the source portal */
        stack.pass = ClipStackWinding(thread->stats, p->winding.get(), stack, thread->pstack_head.portalplane);
        if (!stack.pass)
            continue;

        if (!prevstack.pass) {
            // the second leaf can only be blocked if coplanar
            stack.source = prevstack.source;
            RecursiveLeafFlow(p->leaf, thread, stack);
            FreeStackWinding(stack.pass, stack);
            continue;
        }

        /* Clip any part of the target portal behind the pass portal */
        stack.pass = ClipStackWinding(thread->stats, stack.pass, stack, prevstack.portalplane);
        if (!stack.pass)
            continue;

        /* Clip any part of the source portal in front of the target portal */
        stack.source = ClipStackWinding(thread->stats, prevstack.source, stack, backplane);
        if (!stack.source) {
            FreeStackWinding(stack.pass, stack);
            continue;
        }

        thread->stats.c_portaltest++;

        /* TEST 0 :: source -> pass -> target */
        if (vis_options.level.value() > 0) {
            if (stack.numseparators[0]) {
                for (int j = 0; j < stack.numseparators[0]; j++) {
                    stack.pass = ClipStackWinding(thread->stats, stack.pass, stack, stack.separators[0][j]);
                    if (!stack.pass)
                        break;
                }
            } else {
                /* Using prevstack source for separator cache correctness */
                ClipToSeparators(
                    thread->stats, prevstack.source, thread->pstack_head.portalplane, prevstack.pass, stack.pass, 0, stack);
            }
            if (!stack.pass) {
                FreeStackWinding(stack.source, stack);
                continue;
            }
        }

        /* TEST 1 :: pass -> source -> target */
        if (vis_options.level.value() > 1) {
            if (stack.numseparators[1]) {
                for (int j = 0; j < stack.numseparators[1]; j++) {
                    stack.pass = ClipStackWinding(thread->stats, stack.pass, stack, stack.separators[1][j]);
                    if (!stack.pass)
                        break;
                }
            } else {
                /* Using prevstack source for separator cache correctness */
                ClipToSeparators(thread->stats, prevstack.pass, prevstack.portalplane, prevstack.source, stack.pass, 1, stack);
            }
            if (!stack.pass) {
                FreeStackWinding(stack.source, stack);
                continue;
            }
        }

        /* TEST 2 :: target -> pass -> source */
        if (vis_options.level.value() > 2) {
            ClipToSeparators(thread->stats, stack.pass, stack.portalplane, prevstack.pass, stack.source, 2, stack);
            if (!stack.source) {
                FreeStackWinding(stack.pass, stack);
                continue;
            }
        }

        /* TEST 3 :: pass -> target -> source */
        if (vis_options.level.value() > 3) {
            ClipToSeparators(thread->stats, prevstack.pass, prevstack.portalplane, stack.pass, stack.source, 3, stack);
            if (!stack.source) {
                FreeStackWinding(stack.pass, stack);
                continue;
            }
        }

        thread->stats.c_portalpass++;

        // flow through it for real
        RecursiveLeafFlow(p->leaf, thread, stack);

        FreeStackWinding(stack.source, stack);
        FreeStackWinding(stack.pass, stack);
    }
}

/*
  ===============
  PortalFlow
  ===============
*/
visstats_t PortalFlow(visportal_t *p)
{
    threaddata_t data{p->visbits};

    if (p->status != pstat_working)
        FError("reflowed");

    data.leafvis.resize(portalleafs);

    data.base = p;

    data.pstack_head.portal = p;
    data.pstack_head.source = p->winding.get();
    data.pstack_head.portalplane = p->plane;
    data.pstack_head.mightsee = &p->mightsee;

    RecursiveLeafFlow(p->leaf, &data, data.pstack_head);

    return data.stats;
}

/*
  ============================================================================
  This is a rough first-order aproximation that is used to trivially reject
  some of the final calculations.
  ============================================================================
*/

static void SimpleFlood(visportal_t &srcportal, int leafnum, const leafbits_t &portalsee)
{
    if (srcportal.mightsee[leafnum])
        return;

    srcportal.mightsee[leafnum] = true;
    srcportal.nummightsee++;

    leaf_t &leaf = leafs[leafnum];
    for (const visportal_t *p : leaf.portals) {
        if (portalsee[p - portals.data()]) {
            SimpleFlood(srcportal, p->leaf, portalsee);
        }
    }
}

/*
  ==============
  BasePortalVis
  ==============
*/
static void BasePortalThread(size_t portalnum)
{
    leafbits_t portalsee(numportals * 2);

    visportal_t &p = portals[portalnum];
    viswinding_t &w = *p.winding;

    p.mightsee.resize(portalleafs);

    for (size_t i = 0; i < numportals * 2; i++) {
        if (i == portalnum) {
            continue;
        }

        visportal_t &tp = portals[i];
        viswinding_t &tw = *tp.winding;

        // Quick test - completely at the back?
        float d = p.plane.distance_to(tw.origin);
        if (d < -tw.radius)
            continue;

        int cctp = 0;
        size_t j;
        for (j = 0; j < tw.size(); j++) {
            d = p.plane.distance_to(tw[j]);
            cctp += d > -VIS_ON_EPSILON;
            if (d > VIS_ON_EPSILON)
                break;
        }
        if (j == tw.size()) {
            if (cctp != tw.size())
                continue; // no points on front
        } else
            cctp = 0;

        // Quick test - completely on front?
        d = tp.plane.distance_to(w.origin);
        if (d > w.radius)
            continue;

        int ccp = 0;
        for (j = 0; j < w.size(); j++) {
            d = tp.plane.distance_to(w[j]);
            ccp += d < VIS_ON_EPSILON;
            if (d < -VIS_ON_EPSILON)
                break;
        }
        if (j == w.size()) {
            if (ccp != w.size())
                continue; // no points on back
        } else
            ccp = 0;

        // coplanarity check
        if (cctp != 0 || ccp != 0)
            if (qv::dot(p.plane.normal, tp.plane.normal) < -0.99)
                continue;

        if (vis_options.visdist.value() > 0) {
            if (tp.winding->distFromPortal(p) > vis_options.visdist.value() ||
                p.winding->distFromPortal(tp) > vis_options.visdist.value())
                continue;
        }

        portalsee[i] = 1;
    }

    p.nummightsee = 0;
    SimpleFlood(p, p.leaf, portalsee);

    portalsee.clear();
}

/*
  ==============
  BasePortalVis
  ==============
*/
void BasePortalVis(void)
{
    logging::parallel_for(0, numportals * 2, BasePortalThread);
}
