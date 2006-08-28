// vis.c

#include <vis/vis.h>
#include <common/log.h>

#ifdef USE_PTHREADS
#define MAX_THREADS 8
pthread_mutex_t *my_mutex;
#else
#define MAX_THREADS 1
#endif

int numthreads = 1;

int numportals;
int portalleafs;
int progress;

portal_t *portals;
leaf_t *leafs;

int c_portaltest, c_portalpass, c_portalcheck;
int c_noclip = 0;

qboolean showgetleaf = true;


static byte *vismap;
static byte *vismap_p;
static byte *vismap_end;	// past visfile

int originalvismapsize;

byte *uncompressed;		// [leafbytes*portalleafs]

int leafbytes;			// (portalleafs+63)>>3
int leaflongs;


qboolean fastvis;
qboolean verbose;
int testlevel = 2;

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

void
PlaneFromWinding(const winding_t * w, plane_t *plane)
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
winding_t *
NewWinding(int points)
{
    winding_t *w;
    int size;

    if (points > MAX_WINDING)
	Error("%s: %i points", __func__, points);

    size = (int)((winding_t *) 0)->points[points];
    w = malloc(size);
    memset(w, 0, size);

    return w;
}


void
pw(winding_t * w)
{
    int i;

    for (i = 0; i < w->numpoints; i++)
	logprint("(%5.1f, %5.1f, %5.1f)\n",
		 w->points[i][0], w->points[i][1], w->points[i][2]);
}

void
prl(leaf_t * l)
{
    int i;
    portal_t *p;
    plane_t pl;

    for (i = 0; i < l->numportals; i++) {
	p = l->portals[i];
	pl = p->plane;
	logprint("portal %4i to leaf %4i : %7.1f : (%4.1f, %4.1f, %4.1f)\n",
		 (int)(p - portals), p->leaf, pl.dist,
		 pl.normal[0], pl.normal[1], pl.normal[2]);
    }
}

/*
  ==================
  CopyWinding
  ==================
*/
winding_t *
CopyWinding(winding_t * w)
{
    int size;
    winding_t *c;

    size = (int)((winding_t *) 0)->points[w->numpoints];
    c = malloc(size);
    memcpy(c, w, size);
    return c;
}


/*
  ==================
  AllocStackWinding

  Return a pointer to a free fixed winding on the stack
  ==================
*/
winding_t *
AllocStackWinding(pstack_t *stack)
{
    int i;

    for (i = 0; i < STACK_WINDINGS; i++) {
	if (stack->freewindings[i]) {
	    stack->freewindings[i] = 0;
	    return &stack->windings[i];
	}
    }

    Error("%s: failed", __func__);

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
void
FreeStackWinding(winding_t *w, pstack_t *stack)
{
    unsigned long i = w - stack->windings;

    if (i < STACK_WINDINGS) {
	if (stack->freewindings[i])
	    Error("%s: winding already freed", __func__);
	stack->freewindings[i] = 1;
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
winding_t *
ClipStackWinding(winding_t *in, pstack_t *stack, plane_t *split)
{
    vec_t dists[MAX_WINDING];
    int sides[MAX_WINDING];
    int counts[3];
    vec_t dot;
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

    counts[0] = counts[1] = counts[2] = 0;

// determine sides for each point
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

	if (neww->numpoints == MAX_WINDING_FIXED) {
	    /* Can't clip, fall back to original */
	    FreeStackWinding(neww, stack);
	    c_noclip++;
	    return in;
	}

	if (sides[i] == SIDE_ON) {
	    VectorCopy(p1, neww->points[neww->numpoints]);
	    neww->numpoints++;
	    continue;
	}

	if (sides[i] == SIDE_FRONT) {
	    VectorCopy(p1, neww->points[neww->numpoints]);
	    neww->numpoints++;
	}

	if (sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i])
	    continue;

	// generate a split point
	p2 = in->points[(i + 1) % in->numpoints];

	dot = dists[i] / (dists[i] - dists[i + 1]);
	for (j = 0; j < 3; j++) {	// avoid round off error when possible
	    if (split->normal[j] == 1)
		mid[j] = split->dist;
	    else if (split->normal[j] == -1)
		mid[j] = -split->dist;
	    else
		mid[j] = p1[j] + dot * (p2[j] - p1[j]);
	}

	VectorCopy(mid, neww->points[neww->numpoints]);
	neww->numpoints++;
    }
    FreeStackWinding(in, stack);

    return neww;
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
portal_t *
GetNextPortal(void)
{
    int j;
    portal_t *p, *tp;
    int min;

    LOCK;

    min = 99999999;
    p = NULL;

    for (j = 0, tp = portals; j < numportals * 2; j++, tp++) {
	if (tp->nummightsee < min && tp->status == stat_none) {
	    min = tp->nummightsee;
	    p = tp;
	}
    }

    if (p) {
	p->status = stat_working;
	progress++;
	printf("\r%i of %i: %i%%", progress, 2 * numportals,
	       50 * progress / numportals);
	fflush(stdout);
    }

    UNLOCK;

    return p;
}

/*
  ==============
  LeafThread
  ==============
*/
void *
LeafThread(void *unused)
{
    portal_t *p;
    int cnt, total;

    cnt = 1;
    total = numportals * 2;
    do {
	p = GetNextPortal();
	if (!p)
	    break;

	PortalFlow(p);

	if (verbose) {
	    printf("\r");
	    logprint("portal:%4i  mightsee:%4i  cansee:%4i\n",
		     (int)(p - portals), p->nummightsee, p->numcansee);
	}
    } while (1);

    printf("\r");

    return NULL;
}

/*
  ===============
  CompressRow
  ===============
*/
int
CompressRow(byte *vis, byte *dest)
{
    int j;
    int rep;
    int visrow;
    byte *dest_p;

    dest_p = dest;
    visrow = (portalleafs + 7) >> 3;

    for (j = 0; j < visrow; j++) {
	*dest_p++ = vis[j];
	if (vis[j])
	    continue;

	rep = 1;
	for (j++; j < visrow; j++)
	    if (vis[j] || rep == 255)
		break;
	    else
		rep++;
	*dest_p++ = rep;
	j--;
    }

    return dest_p - dest;
}


/*
  ===============
  LeafFlow

  Builds the entire visibility list for a leaf
  ===============
*/
int totalvis;

void
LeafFlow(int leafnum)
{
    leaf_t *leaf;
    byte *outbuffer;
    byte compressed[MAX_MAP_LEAFS / 8];
    int i, j;
    int numvis;
    byte *dest;
    portal_t *p;

//
// flow through all portals, collecting visible bits
//
    outbuffer = uncompressed + leafnum * leafbytes;
    leaf = &leafs[leafnum];
    for (i = 0; i < leaf->numportals; i++) {
	p = leaf->portals[i];
	if (p->status != stat_done)
	    Error("portal not done");
	for (j = 0; j < leafbytes; j++)
	    outbuffer[j] |= p->visbits[j];
    }

    if (outbuffer[leafnum >> 3] & (1 << (leafnum & 7)))
	logprint("WARNING: Leaf portals saw into leaf (%i)\n", leafnum);

    outbuffer[leafnum >> 3] |= (1 << (leafnum & 7));

    numvis = 0;
    for (i = 0; i < portalleafs; i++)
	if (outbuffer[i >> 3] & (1 << (i & 3)))
	    numvis++;

//
// compress the bit string
//
    if (verbose)
	logprint("leaf %4i : %4i visible\n", leafnum, numvis);
    totalvis += numvis;

#if 0
    i = (portalleafs + 7) >> 3;
    memcpy(compressed, outbuffer, i);
#else
    i = CompressRow(outbuffer, compressed);
#endif

    dest = vismap_p;
    vismap_p += i;

    if (vismap_p > vismap_end)
	Error("Vismap expansion overflow");

    dleafs[leafnum + 1].visofs = dest - vismap;	// leaf 0 is a common solid

    memcpy(dest, compressed, i);
}


/*
  ==================
  CalcPortalVis
  ==================
*/
void
CalcPortalVis(void)
{
    int i;

// fastvis just uses mightsee for a very loose bound
    if (fastvis) {
	for (i = 0; i < numportals * 2; i++) {
	    portals[i].visbits = portals[i].mightsee;
	    portals[i].status = stat_done;
	}
	return;
    }

#ifdef USE_PTHREADS
    {
	int status;
	pthread_t threads[MAX_THREADS];
	pthread_attr_t attrib;
	pthread_mutexattr_t mattrib;

	my_mutex = malloc(sizeof(*my_mutex));
	pthread_mutexattr_init(&mattrib);

#if 0
	status = pthread_mutexattr_settype(&mattrib, PTHREAD_MUTEX_FAST_NP);
	if (status)
	    Error("pthread_mutexattr_settype failed");
#endif
	pthread_mutex_init(my_mutex, &mattrib);

	status = pthread_attr_init(&attrib);
	if (status)
	    Error("pthread_attr_init failed");

	for (i = 0; i < numthreads; i++) {
	    status = pthread_create(&threads[i], &attrib, LeafThread, NULL);
	    if (status)
		Error("pthread_create failed");
	}

	for (i = 0; i < numthreads; i++) {
	    status = pthread_join(threads[i], NULL);
	    if (status)
		Error("pthread_join failed");
	}

	status = pthread_mutex_destroy(my_mutex);
	if (status)
	    Error("pthread_mutex_destroy failed");
    }
#else
    LeafThread(NULL);
#endif /* USE_PTHREADS */

    if (verbose) {
	logprint("portalcheck: %i  portaltest: %i  portalpass: %i\n",
		 c_portalcheck, c_portaltest, c_portalpass);
	logprint("c_vistest: %i  c_mighttest: %i\n", c_vistest, c_mighttest);
    }
}


/*
  ==================
  CalcVis
  ==================
*/
void
CalcVis(void)
{
    int i;

    logprint("Calculating Base Vis:\n");
    BasePortalVis();

    progress = 0;
    printf("\r");
    logprint("Calculating Full Vis:\n");
    CalcPortalVis();

//
// assemble the leaf vis lists by oring and compressing the portal lists
//
    for (i = 0; i < portalleafs; i++)
	LeafFlow(i);

    logprint("average leafs visible: %i\n", totalvis / portalleafs);
}

/*
  ============================================================================
  PASSAGE CALCULATION (not used yet...)
  ============================================================================
*/

int count_sep;

qboolean
PlaneCompare(plane_t *p1, plane_t *p2)
{
    int i;

    if (fabs(p1->dist - p2->dist) > 0.01)
	return false;

    for (i = 0; i < 3; i++)
	if (fabs(p1->normal[i] - p2->normal[i]) > 0.001)
	    return false;

    return true;
}

sep_t *
Findpassages(winding_t * source, winding_t * pass)
{
    int i, j, k, l;
    plane_t plane;
    vec3_t v1, v2;
    float d;
    double length;
    int counts[3];
    qboolean fliptest;
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

	    length = plane.normal[0] * plane.normal[0]
		+ plane.normal[1] * plane.normal[1]
		+ plane.normal[2] * plane.normal[2];

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
		if (d < -ON_EPSILON) {	// source is on the negative side, so we want all
		    // pass and target on the positive side
		    fliptest = false;
		    break;
		} else if (d > ON_EPSILON) {	// source is on the positive side, so we want all
		    // pass and target on the negative side
		    fliptest = true;
		    break;
		}
	    }
	    if (k == source->numpoints)
		continue;	// planar with source portal

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
		continue;	// points on negative side, not a seperating plane

	    if (!counts[0])
		continue;	// planar with pass portal

	    //
	    // save this out
	    //
	    count_sep++;

	    sep = malloc(sizeof(*sep));
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
void
CalcPassages(void)
{
    int i, j, k;
    int count, count2;
    leaf_t *l;
    portal_t *p1, *p2;
    sep_t *sep;
    passage_t *passages;

    logprint("building passages...\n");

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
		    sep = malloc(sizeof(*sep));
		    sep->next = NULL;
		    sep->plane = p1->plane;
		}
		passages = malloc(sizeof(*passages));
		passages->planes = sep;
		passages->from = p1->leaf;
		passages->to = p2->leaf;
		passages->next = l->passages;
		l->passages = passages;
	    }
	}
    }

    logprint("numpassages: %i (%i)\n", count2, count);
    logprint("total passages: %i\n", count_sep);
}

// ===========================================================================

static void
SetWindingSphere(winding_t *w)
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
void
LoadPortals(char *name)
{
    int i, j;
    portal_t *p;
    leaf_t *l;
    char magic[80];
    FILE *f;
    int numpoints;
    winding_t *w;
    int leafnums[2];
    plane_t plane;

    if (!strcmp(name, "-"))
	f = stdin;
    else {
	f = fopen(name, "r");
	if (!f) {
	    logprint("%s: couldn't read %s\n", __func__, name);
	    logprint("No vising performed.\n");
	    exit(1);
	}
    }

    if (fscanf(f, "%79s\n%i\n%i\n", magic, &portalleafs, &numportals) != 3)
	Error("%s: failed to read header", __func__);
    if (strcmp(magic, PORTALFILE))
	Error("%s: not a portal file", __func__);

    if (numportals * 2 > MAX_PORTALS)
	Error("Exceeded MAX_PORTALS.");

    logprint("%4i portalleafs\n", portalleafs);
    logprint("%4i numportals\n", numportals);

    leafbytes = ((portalleafs + 63) & ~63) >> 3;
    leaflongs = leafbytes / sizeof(long);

// each file portal is split into two memory portals
    portals = malloc(2 * numportals * sizeof(portal_t));
    memset(portals, 0, 2 * numportals * sizeof(portal_t));

    leafs = malloc(portalleafs * sizeof(leaf_t));
    memset(leafs, 0, portalleafs * sizeof(leaf_t));

    originalvismapsize = portalleafs * ((portalleafs + 7) / 8);

    // FIXME - more intelligent allocation?
    dvisdata = malloc(MAX_MAP_VISIBILITY);
    if (!dvisdata)
	Error("%s: dvisdata allocation failed (%i bytes)", __func__,
	      MAX_MAP_VISIBILITY);
    memset(dvisdata, 0, MAX_MAP_VISIBILITY);

    vismap = vismap_p = dvisdata;
    vismap_end = vismap + MAX_MAP_VISIBILITY;

    for (i = 0, p = portals; i < numportals; i++) {
	if (fscanf(f, "%i %i %i ", &numpoints, &leafnums[0], &leafnums[1])
	    != 3)
	    Error("%s: reading portal %i", __func__, i);
	if (numpoints > MAX_WINDING)
	    Error("%s: portal %i has too many points", __func__, i);
	if ((unsigned)leafnums[0] > (unsigned)portalleafs
	    || (unsigned)leafnums[1] > (unsigned)portalleafs)
	    Error("%s: reading portal %i", __func__, i);

	w = p->winding = NewWinding(numpoints);
	w->numpoints = numpoints;

	for (j = 0; j < numpoints; j++) {
	    double v[3];
	    int k;

	    // scanf into double, then assign to vec_t
	    if (fscanf(f, "(%lf %lf %lf ) ", &v[0], &v[1], &v[2]) != 3)
		Error("%s: reading portal %i", __func__, i);
	    for (k = 0; k < 3; k++)
		w->points[j][k] = (vec_t)v[k];
	}
	fscanf(f, "\n");

	// calc plane
	PlaneFromWinding(w, &plane);

	// create forward portal
	l = &leafs[leafnums[0]];
	if (l->numportals == MAX_PORTALS_ON_LEAF)
	    Error("Leaf with too many portals");
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
	    Error("Leaf with too many portals");
	l->portals[l->numportals] = p;
	l->numportals++;

	// Create a reverse winding
	p->winding = NewWinding(numpoints);
	p->winding->numpoints = numpoints;
	for (j = 0; j < numpoints; ++j)
	    VectorCopy(w->points[numpoints - (j + 1)], p->winding->points[j]);

	//p->winding = w;
	p->plane = plane;
	p->leaf = leafnums[0];
	SetWindingSphere(p->winding);
	p++;
    }

    fclose(f);
}


/*
  ===========
  main
  ===========
*/
int
main(int argc, char **argv)
{
    char portalfile[1024];
    char source[1024];
    int i, bsp_version;
    double start, end;
    qboolean credits = false;

    init_log("vis.log");
    logprint("---- TyrVis v1.0 ---- "
#if 0
	     "(Beta version " __DATE__ " " __TIME__ ")"
#endif
	     "\n");
    for (i = 1; i < argc; i++) {
	if (!strcmp(argv[i], "-credits")) {
	    logprint("Original source supplied no obligation by iD Software "
		     "12th September 96\n"
		     "Modification by Antony Suter, TeamFortress Software "
		     "<antony@teamfortress.com>\n"
		     "Additional Modification by Kevin Shanahan, Aka "
		     "Tyrann <tyrann@disenchant.net>\n");
	    credits = true;
	} else if (!strcmp(argv[i], "-threads")) {
	    numthreads = atoi(argv[i + 1]);
	    i++;
	} else if (!strcmp(argv[i], "-fast")) {
	    logprint("fastvis = true\n");
	    fastvis = true;
	} else if (!strcmp(argv[i], "-level")) {
	    testlevel = atoi(argv[i + 1]);
	    logprint("testlevel = %i\n", testlevel);
	    i++;
	} else if (!strcmp(argv[i], "-v")) {
	    logprint("verbose = true\n");
	    verbose = true;
	} else if (argv[i][0] == '-')
	    Error("Unknown option \"%s\"", argv[i]);
	else
	    break;
    }

    if (i == argc && credits)
	return 0;
    else if (i != argc - 1)
	Error("usage: vis [-threads #] [-level 0-4] [-fast] [-v] "
	      "[-credits] bspfile");

    start = I_FloatTime();

    strcpy(source, argv[i]);
    StripExtension(source);
    DefaultExtension(source, ".bsp");

    bsp_version = LoadBSPFile(source);

    strcpy(portalfile, argv[i]);
    StripExtension(portalfile);
    strcat(portalfile, ".prt");

    LoadPortals(portalfile);

    uncompressed = malloc(leafbytes * portalleafs);
    memset(uncompressed, 0, leafbytes * portalleafs);

//    CalcPassages ();

    CalcVis();

    logprint("c_noclip: %i\n", c_noclip);
    logprint("c_chains: %lu\n", c_chains);

    visdatasize = vismap_p - dvisdata;
    logprint("visdatasize:%i  compressed from %i\n",
	     visdatasize, originalvismapsize);

    CalcAmbientSounds();

    WriteBSPFile(source, bsp_version);

//    unlink (portalfile);

    end = I_FloatTime();
    logprint("%5.1f seconds elapsed\n", end - start);

    close_log();

    return 0;
}
