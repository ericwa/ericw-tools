#include <common/threads.h>
#include <vis/vis.h>
#include <vis/leafbits.h>

unsigned long c_chains;
int c_vistest, c_mighttest;

static int c_portalskip;
static int c_leafskip;

/*
  ==============
  ClipToSeperators

  Source, pass, and target are an ordering of portals.

  Generates seperating planes canidates by taking two points from source and
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
static winding_t *
ClipToSeperators(const winding_t *source,
		 const plane_t src_pl,
		 const winding_t *pass,
		 winding_t *target, unsigned int test,
		 pstack_t *stack)
{
    int i, j, k, l;
    plane_t sep;
    vec3_t v1, v2;
    vec_t d;
    int count;
    qboolean fliptest;
    vec_t len_sq;

    // check all combinations
    for (i = 0; i < source->numpoints; i++) {
	l = (i + 1) % source->numpoints;
	VectorSubtract(source->points[l], source->points[i], v1);

	// find a vertex of pass that makes a plane that puts all of the
	// vertexes of pass on the front side and all of the vertexes of
	// source on the back side
	for (j = 0; j < pass->numpoints; j++) {

	    // Which side of the source portal is this point?
	    // This also tells us which side of the seperating plane has
	    //  the source portal.
	    d = DotProduct(pass->points[j], src_pl.normal) - src_pl.dist;
	    if (d < -ON_EPSILON)
		fliptest = true;
	    else if (d > ON_EPSILON)
		fliptest = false;
	    else
		continue;	// Point lies in source plane

	    // Make a plane with the three points
	    VectorSubtract(pass->points[j], source->points[i], v2);
	    CrossProduct(v1, v2, sep.normal);
	    len_sq = sep.normal[0] * sep.normal[0]
		+ sep.normal[1] * sep.normal[1]
		+ sep.normal[2] * sep.normal[2];

	    // If points don't make a valid plane, skip it.
	    if (len_sq < ON_EPSILON)
		continue;

	    VectorScale(sep.normal, 1.0 / sqrt(len_sq), sep.normal);
	    sep.dist = DotProduct(pass->points[j], sep.normal);

	    //
	    // flip the plane if the source portal is backwards
	    //
	    if (fliptest) {
		VectorSubtract(vec3_origin, sep.normal, sep.normal);
		sep.dist = -sep.dist;
	    }
	    //
	    // if all of the pass portal points are now on the positive side,
	    // this is the seperating plane
	    //
	    count = 0;
	    for (k = 0; k < pass->numpoints; k++) {
		if (k == j)
		    continue;
		d = DotProduct(pass->points[k], sep.normal) - sep.dist;
		if (d < -ON_EPSILON)
		    break;
		else if (d > ON_EPSILON)
		    ++count;
	    }
	    if (k != pass->numpoints)
		continue;	// points on negative side, not a seperating plane
	    if (!count)
		continue;	// planar with seperating plane

	    //
	    // flip the normal if we want the back side (tests 1 and 3)
	    //
	    if (test & 1) {
		VectorSubtract(vec3_origin, sep.normal, sep.normal);
		sep.dist = -sep.dist;
	    }

	    /* Cache separating planes for tests 0, 1 */
	    if (test < 2) {
		if (stack->numseparators[test] == MAX_SEPARATORS)
		    Error("MAX_SEPARATORS");
		stack->separators[test][stack->numseparators[test]] = sep;
		stack->numseparators[test]++;
	    }

	    target = ClipStackWinding(target, stack, &sep);
	    if (!target)
		return NULL;	// target is not visible

	    break;
	}
    }
    return target;
}

static int
CheckStack(leaf_t *leaf, threaddata_t *thread)
{
    pstack_t *p;

    for (p = thread->pstack_head.next; p; p = p->next)
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
static void
RecursiveLeafFlow(int leafnum, threaddata_t *thread, pstack_t *prevstack)
{
    pstack_t stack;
    portal_t *p;
    plane_t backplane;
    leaf_t *leaf;
    int i, j, err, numblocks;
    leafblock_t *test, *might, *vis, more;

    ++c_chains;

    leaf = &leafs[leafnum];

    /*
     * Check we haven't recursed into a leaf already on the stack
     */
    err = CheckStack(leaf, thread);
    if (err) {
	logprint("WARNING: %s: recursion on leaf %d\n", __func__, leafnum);
	LogLeaf(leaf);
	return;
    }

    // mark the leaf as visible
    if (!TestLeafBit(thread->leafvis, leafnum)) {
	SetLeafBit(thread->leafvis, leafnum);
	thread->base->numcansee++;
    }

    prevstack->next = &stack;

    stack.next = NULL;
    stack.leaf = leaf;
    stack.portal = NULL;
    stack.numseparators[0] = 0;
    stack.numseparators[1] = 0;

    for (i = 0; i < STACK_WINDINGS; i++)
	stack.freewindings[i] = 1;

    stack.mightsee = malloc(LeafbitsSize(portalleafs));
    might = stack.mightsee->bits;
    vis = thread->leafvis->bits;

    // check all portals for flowing into other leafs
    for (i = 0; i < leaf->numportals; i++) {
	p = leaf->portals[i];

	if (!TestLeafBit(prevstack->mightsee, p->leaf)) {
	    c_leafskip++;
	    continue;		// can't possibly see it
	}
	// if the portal can't see anything we haven't allready seen, skip it
	if (p->status == pstat_done) {
	    c_vistest++;
	    test = p->visbits->bits;
	} else {
	    c_mighttest++;
	    test = p->mightsee->bits;
	}

	more = 0;
	numblocks = (portalleafs + LEAFMASK) >> LEAFSHIFT;
	for (j = 0; j < numblocks; j++) {
	    might[j] = prevstack->mightsee->bits[j] & test[j];
	    more |= (might[j] & ~vis[j]);
	}

	if (!more) {
	    // can't see anything new
	    c_portalskip++;
	    continue;
	}
	// get plane of portal, point normal into the neighbor leaf
	stack.portalplane = p->plane;
	VectorSubtract(vec3_origin, p->plane.normal, backplane.normal);
	backplane.dist = -p->plane.dist;

	if (VectorCompare(prevstack->portalplane.normal, backplane.normal))
	    continue;		// can't go out a coplanar face

	c_portalcheck++;

	stack.portal = p;
	stack.next = NULL;

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
	stack.pass = ClipStackWinding(p->winding, &stack,
				      &thread->pstack_head.portalplane);
	if (!stack.pass)
	    continue;

	if (!prevstack->pass) {
	    // the second leaf can only be blocked if coplanar
	    stack.source = prevstack->source;
	    RecursiveLeafFlow(p->leaf, thread, &stack);
	    FreeStackWinding(stack.pass, &stack);
	    continue;
	}

	/* Clip any part of the target portal behind the pass portal */
	stack.pass = ClipStackWinding(stack.pass, &stack,
				      &prevstack->portalplane);
	if (!stack.pass)
	    continue;

	/* Clip any part of the source portal in front of the target portal */
	stack.source = ClipStackWinding(prevstack->source, &stack,
					&backplane);
	if (!stack.source) {
	    FreeStackWinding(stack.pass, &stack);
	    continue;
	}

	c_portaltest++;

	/* TEST 0 :: source -> pass -> target */
	if (testlevel > 0) {
	    if (stack.numseparators[0]) {
		for (j = 0; j < stack.numseparators[0]; j++) {
		    stack.pass = ClipStackWinding(stack.pass, &stack,
						  &stack.separators[0][j]);
		    if (!stack.pass)
			break;
		}
	    } else {
		/* Using prevstack source for separator cache correctness */
		stack.pass = ClipToSeperators(prevstack->source,
					      thread->pstack_head.portalplane,
					      prevstack->pass, stack.pass, 0,
					      &stack);
	    }
	    if (!stack.pass) {
		FreeStackWinding(stack.source, &stack);
		continue;
	    }
	}

	/* TEST 1 :: pass -> source -> target */
	if (testlevel > 1) {
	    if (stack.numseparators[1]) {
		for (j = 0; j < stack.numseparators[1]; j++) {
		    stack.pass = ClipStackWinding(stack.pass, &stack,
						  &stack.separators[1][j]);
		    if (!stack.pass)
			break;
		}
	    } else {
		/* Using prevstack source for separator cache correctness */
		stack.pass = ClipToSeperators(prevstack->pass,
					      prevstack->portalplane,
					      prevstack->source, stack.pass, 1,
					      &stack);
	    }
	    if (!stack.pass) {
		FreeStackWinding(stack.source, &stack);
		continue;
	    }
	}

	/* TEST 2 :: target -> pass -> source */
	if (testlevel > 2) {
	    stack.source = ClipToSeperators(stack.pass, stack.portalplane,
					    prevstack->pass, stack.source, 2,
					    &stack);
	    if (!stack.source) {
		FreeStackWinding(stack.pass, &stack);
		continue;
	    }
	}

	/* TEST 3 :: pass -> target -> source */
	if (testlevel > 3) {
	    stack.source = ClipToSeperators(prevstack->pass,
					    prevstack->portalplane, stack.pass,
					    stack.source, 3, &stack);
	    if (!stack.source) {
		FreeStackWinding(stack.pass, &stack);
		continue;
	    }
	}

	c_portalpass++;

	// flow through it for real
	RecursiveLeafFlow(p->leaf, thread, &stack);

	FreeStackWinding(stack.source, &stack);
	FreeStackWinding(stack.pass, &stack);
    }

    free(stack.mightsee);
}


/*
  ===============
  PortalFlow
  ===============
*/
void
PortalFlow(portal_t *p)
{
    threaddata_t data;

    if (p->status != pstat_working)
	Error("%s: reflowed", __func__);

    p->visbits = malloc(LeafbitsSize(portalleafs));
    memset(p->visbits, 0, LeafbitsSize(portalleafs));

    memset(&data, 0, sizeof(data));
    data.leafvis = p->visbits;
    data.base = p;

    data.pstack_head.portal = p;
    data.pstack_head.source = p->winding;
    data.pstack_head.portalplane = p->plane;
    data.pstack_head.mightsee = p->mightsee;

    RecursiveLeafFlow(p->leaf, &data, &data.pstack_head);
}


/*
  ============================================================================
  This is a rough first-order aproximation that is used to trivially reject
  some of the final calculations.
  ============================================================================
*/

static void
SimpleFlood(portal_t *srcportal, int leafnum, byte *portalsee)
{
    int i;
    leaf_t *leaf;
    portal_t *p;

    if (TestLeafBit(srcportal->mightsee, leafnum))
	return;

    SetLeafBit(srcportal->mightsee, leafnum);
    srcportal->nummightsee++;

    leaf = &leafs[leafnum];
    for (i = 0; i < leaf->numportals; i++) {
	p = leaf->portals[i];
	if (!portalsee[p - portals])
	    continue;
	SimpleFlood(srcportal, p->leaf, portalsee);
    }
}

/*
  ==============
  BasePortalVis
  ==============
*/
static void *
BasePortalThread(void *dummy)
{
    int i, j, portalnum;
    portal_t *p, *tp;
    winding_t *w, *tw;
    float d;
    byte *portalsee;

    portalsee = malloc(sizeof(*portalsee) * numportals * 2);
    if (!portalsee)
	Error("%s: Out of Memory", __func__);

    while (1) {
	portalnum = GetThreadWork();
	if (portalnum == -1)
	    break;

	p = portals + portalnum;
	w = p->winding;

	p->mightsee = malloc(LeafbitsSize(portalleafs));
	memset(p->mightsee, 0, LeafbitsSize(portalleafs));

	memset(portalsee, 0, numportals * 2);

	for (i = 0, tp = portals; i < numportals * 2; i++, tp++) {
	    tw = tp->winding;
	    if (tp == p)
		continue;

	    // Quick test - completely at the back?
	    d = DotProduct(tw->origin, p->plane.normal) - p->plane.dist;
	    if (d < -tw->radius)
		continue;

	    for (j = 0; j < tw->numpoints; j++) {
		d = DotProduct(tw->points[j], p->plane.normal) - p->plane.dist;
		if (d > ON_EPSILON)
		    break;
	    }
	    if (j == tw->numpoints)
		continue;	// no points on front

	    // Quick test - completely on front?
	    d = DotProduct(w->origin, tp->plane.normal) - tp->plane.dist;
	    if (d > w->radius)
		continue;

	    for (j = 0; j < w->numpoints; j++) {
		d = DotProduct(w->points[j], tp->plane.normal) - tp->plane.dist;
		if (d < -ON_EPSILON)
		    break;
	    }
	    if (j == w->numpoints)
		continue;	// no points on back

	    portalsee[i] = 1;
	}

	p->nummightsee = 0;
	SimpleFlood(p, p->leaf, portalsee);
    }

    free(portalsee);

    return NULL;
}


/*
  ==============
  BasePortalVis
  ==============
*/
void
BasePortalVis(void)
{
    RunThreadsOn(0, numportals * 2, BasePortalThread, NULL);
}
