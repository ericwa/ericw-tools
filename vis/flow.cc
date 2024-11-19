#include <vis/vis.hh>
#include <vis/leafbits.hh>
#include <common/log.hh>
#include <common/parallel.hh>
#include <bit> // for std::popcount

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
static void ClipToSeparators(visstats_t &stats, const viswinding_t *source, const qplane3d src_pl,
    const viswinding_t *pass, viswinding_t *&target, unsigned int test, pstack_t &stack)
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
            double d = src_pl.distance_to(pass->at(j));
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
            const double len_sq = qv::length2(sep.normal);

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

enum class vistest_action
{
    action_continue,
    action_pass
};

static vistest_action VisTests(
    visstats_t &stats, pstack_t &stack, const pstack_t *const head, const pstack_t *const prevstack)
{
    /* TEST 0 :: source -> pass -> target */
    if (vis_options.level.value() > 0) {
        if (stack.numseparators[0]) {
            for (int j = 0; j < stack.numseparators[0]; j++) {
                stack.pass = ClipStackWinding(stats, stack.pass, stack, stack.separators[0][j]);
                if (!stack.pass)
                    break;
            }
        } else {
            /* Using prevstack source for separator cache correctness */
            ClipToSeparators(stats, prevstack->source, head->portalplane, prevstack->pass, stack.pass, 0, stack);
        }
        if (!stack.pass) {
            FreeStackWinding(stack.source, stack);
            return vistest_action::action_continue;
        }
    }

    /* TEST 1 :: pass -> source -> target */
    if (vis_options.level.value() > 1) {
        if (stack.numseparators[1]) {
            for (int j = 0; j < stack.numseparators[1]; j++) {
                stack.pass = ClipStackWinding(stats, stack.pass, stack, stack.separators[1][j]);
                if (!stack.pass)
                    break;
            }
        } else {
            /* Using prevstack source for separator cache correctness */
            ClipToSeparators(stats, prevstack->pass, prevstack->portalplane, prevstack->source, stack.pass, 1, stack);
        }
        if (!stack.pass) {
            FreeStackWinding(stack.source, stack);
            return vistest_action::action_continue;
        }
    }

    /* TEST 2 :: target -> pass -> source */
    if (vis_options.level.value() > 2) {
        ClipToSeparators(stats, stack.pass, stack.portalplane, prevstack->pass, stack.source, 2, stack);
        if (!stack.source) {
            FreeStackWinding(stack.pass, stack);
            return vistest_action::action_continue;
        }
    }

    /* TEST 3 :: pass -> target -> source */
    if (vis_options.level.value() > 3) {
        ClipToSeparators(stats, prevstack->pass, prevstack->portalplane, stack.pass, stack.source, 3, stack);
        if (!stack.source) {
            FreeStackWinding(stack.pass, stack);
            return vistest_action::action_continue;
        }
    }

    return vistest_action::action_pass;
}

/*
  ==================
  TargetChecks

  Filter mightsee by clipping against all portals
  ==================
*/
static unsigned TargetChecks(visstats_t &stats, const pstack_t *const head, const pstack_t *const prevstack,
    leafbits_t &prevportalbits, leafbits_t &portalbits)
{
    pstack_t stack;
    visportal_t *p, *q;
    qplane3d backplane;
    int i, j, numchecks, numremain;

    if (prevstack->pass == NULL) {
        portalbits = std::move(prevportalbits);
        return 0;
    }

    numchecks = 0;
    numremain = 0;

    stack.next = NULL;
    stack.leaf = NULL;
    stack.portal = NULL;
    stack.numseparators[0] = 0;
    stack.numseparators[1] = 0;

    for (i = 0; i < STACK_WINDINGS; i++)
        stack.windings_used[i] = false;

    leafbits_t local(portalleafs);
    local.clear();
    stack.mightsee = &local;

    // check all portals for flowing into other leafs
    for (i = 0, p = portals.data(); i < numportals * 2; i++, p++) {

        if ((*stack.mightsee)[p->leaf])
            continue; // target check already done and passed

        if (!(*prevstack->mightsee)[p->leaf])
            continue; // can't possibly see it

        if (!prevportalbits[i])
            continue; // can't possibly see it

        // get plane of portal, point normal into the neighbor leaf
        stack.portalplane = p->plane;
        backplane = -p->plane;

        if (qv::epsilonEqual(prevstack->portalplane.normal, backplane.normal, VIS_EQUAL_EPSILON))
            continue; // can't go out a coplanar face

        numchecks++;

        stack.portal = p;

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
        stack.pass = ClipStackWinding(stats, p->winding.get(), stack, head->portalplane);
        if (!stack.pass)
            continue;

        /* Clip any part of the target portal behind the pass portal */
        stack.pass = ClipStackWinding(stats, stack.pass, stack, prevstack->portalplane);
        if (!stack.pass)
            continue;

        /* Clip any part of the source portal in front of the target portal */
        stack.source = ClipStackWinding(stats, prevstack->source, stack, backplane);
        if (!stack.source) {
            FreeStackWinding(stack.pass, stack);
            continue;
        }

        if (VisTests(stats, stack, head, prevstack) == vistest_action::action_continue)
            continue;

        // mark leaf visible
        (*stack.mightsee)[p->leaf] = true;

        // mark portal visible
        portalbits[i] = true;
        numremain++;

        // inherit remaining portal visibilities
        leaf_t *l = &leafs[p->leaf];
        for (int k = 0; k < l->portals.size(); k++) {
            q = l->portals[k];
            j = (q - portals.data()) ^ 1; // another portal leading into the same leaf
            if (i < j) // is it upcoming in iteration order?
                portalbits[j] = bool(prevportalbits[j]);
        }

        FreeStackWinding(stack.source, stack);
        FreeStackWinding(stack.pass, stack);
    }

    // transfer results back to prevstack
    *prevstack->mightsee = std::move(local);

    return numchecks;
}

/*
  ==================
  IterativeTargetChecks

  Retrace the path and reduce mightsee by clipping the targets directly
  ==================
*/
static unsigned IterativeTargetChecks(visstats_t &stats, pstack_t *const head)
{
    unsigned numchecks, numblocks;

    numchecks = 0;
    numblocks = (portalleafs + leafbits_t::mask) >> leafbits_t::shift;

    leafbits_t portalbits(numportals * 2); // in contradiction to the typename, I know
    portalbits.setall();

    for (pstack_t *stack = head; stack; stack = stack->next) {
        if (stack->did_targetchecks)
            continue;

        leafbits_t nextportalbits(numportals * 2);
        nextportalbits.clear();
        numchecks += TargetChecks(stats, head, stack, portalbits, nextportalbits);
        portalbits = std::move(nextportalbits);

        if (stack->next) {
            pstack_t *next = stack->next;
            uint32_t *nextsee = next->mightsee->data();
            uint32_t *mightsee = stack->mightsee->data();
            for (int i = 0; i < numblocks; i++)
                nextsee[i] &= mightsee[i];
        }

        // mark done
        stack->did_targetchecks = true;
        stack->num_expected_targetchecks = 0;
    }

    return numchecks;
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

    // check all target portals instead of just neighbor portals, if the time is right
    if (vis_options.targetratio.value() > 0.0 && prevstack.num_expected_targetchecks > 0 &&
        thread->numsteps * vis_options.targetratio.value() >=
            thread->numtargetchecks + prevstack.num_expected_targetchecks) {
        unsigned num_actual_targetchecks = IterativeTargetChecks(thread->stats, &thread->pstack_head);
        thread->stats.c_targetcheck += num_actual_targetchecks;
        thread->numtargetchecks += num_actual_targetchecks;
        // prevstack.num_expected_targetchecks is zero now
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

        const auto might = stack.mightsee->data(); // buffer of stack.mightsee can change between iterations
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

        stack.did_targetchecks = false;
        stack.num_expected_targetchecks = 0;

        // calculate num_expected_targetchecks only if we're using it, since it's somewhat expensive to compute
        if (vis_options.targetratio.value() > 0.0) {
            int nummightsee = 0;
            for (int j = 0; j < numblocks; j++) {
                nummightsee += std::popcount(might[j]);
            }
            stack.num_expected_targetchecks = prevstack.num_expected_targetchecks + nummightsee;
        }

        // get plane of portal, point normal into the neighbor leaf
        stack.portalplane = p->plane;
        const qplane3d backplane = -p->plane;

        if (qv::epsilonEqual(prevstack.portalplane.normal, backplane.normal, VIS_EQUAL_EPSILON))
            continue; // can't go out a coplanar face

        thread->numsteps++;
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

        if (VisTests(thread->stats, stack, &thread->pstack_head, &prevstack) == vistest_action::action_continue)
            continue;

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
    data.numsteps = 0;
    data.numtargetchecks = 0;

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
void BasePortalVis()
{
    logging::parallel_for(0, numportals * 2, BasePortalThread);
}
