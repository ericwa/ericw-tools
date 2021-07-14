/* common/polylib.c */

#include <stddef.h>
#include <float.h>

#include <common/cmdlib.hh>
#include <common/mathlib.hh>
#include <common/polylib.hh>

#define BOGUS_RANGE 65536

/*
 * =============
 * AllocWinding
 * =============
 */
polylib::winding_t *
polylib::AllocWinding(int points)
{
    winding_t *w;
    int s;

    s = sizeof(vec_t) * 3 * points + sizeof(int);
    w = static_cast<winding_t *>(malloc(s));
    memset(w, 0, s);
    return w;
}

/*
 * ============
 * RemoveColinearPoints
 * ============
 */
static int c_removed;

void
polylib::RemoveColinearPoints(winding_t * w)
{
    int i, j, k;
    vec3_t v1, v2;
    int nump;
    vec3_t p[MAX_POINTS_ON_WINDING];

    nump = 0;
    for (i = 0; i < w->numpoints; i++) {
        j = (i + 1) % w->numpoints;
        k = (i + w->numpoints - 1) % w->numpoints;
        VectorSubtract(w->p[j], w->p[i], v1);
        VectorSubtract(w->p[i], w->p[k], v2);
        VectorNormalize(v1);
        VectorNormalize(v2);
        if (DotProduct(v1, v2) < 0.999) {
            VectorCopy(w->p[i], p[nump]);
            nump++;
        }
    }

    if (nump == w->numpoints)
        return;

    c_removed += w->numpoints - nump;
    w->numpoints = nump;
    memcpy(w->p, p, nump * sizeof(p[0]));
}

/*
 * ============
 * WindingPlane
 * ============
 */
void
polylib::WindingPlane(const winding_t * w, vec3_t normal, vec_t *dist)
{
    vec3_t v1, v2;

    VectorSubtract(w->p[0], w->p[1], v1);
    VectorSubtract(w->p[2], w->p[1], v2);
    CrossProduct(v1, v2, normal);
    VectorNormalize(normal);
    *dist = DotProduct(w->p[0], normal);
}

/*
 * =============
 * WindingArea
 * =============
 */
vec_t
polylib::WindingArea(const winding_t * w)
{
    int i;
    vec3_t d1, d2, cross;
    vec_t total;

    total = 0;
    for (i = 2; i < w->numpoints; i++) {
        VectorSubtract(w->p[i - 1], w->p[0], d1);
        VectorSubtract(w->p[i], w->p[0], d2);
        CrossProduct(d1, d2, cross);
        total += 0.5 * VectorLength(cross);
    }
    return total;
}

/*
 * =============
 * WindingCenter
 * =============
 */
void
polylib::WindingCenter(const winding_t * w, vec3_t center)
{
    int i;
    float scale;

    VectorCopy(vec3_origin, center);
    for (i = 0; i < w->numpoints; i++)
        VectorAdd(w->p[i], center, center);

    scale = 1.0 / w->numpoints;
    VectorScale(center, scale, center);
}

/*
 * =============
 * WindingBounds
 * =============
 */
void
polylib::WindingBounds (const winding_t *w, vec3_t mins, vec3_t maxs)
{
    vec_t	v;
    int		i,j;
    
    mins[0] = mins[1] = mins[2] = FLT_MAX;
    maxs[0] = maxs[1] = maxs[2] = -FLT_MAX;
    
    for (i=0 ; i<w->numpoints ; i++)
    {
        for (j=0 ; j<3 ; j++)
        {
            v = w->p[i][j];
            if (v < mins[j])
                mins[j] = v;
            if (v > maxs[j])
                maxs[j] = v;
        }
    }
}

/*
 * =================
 * BaseWindingForPlane
 * =================
 */
polylib::winding_t *
polylib::BaseWindingForPlane(const vec3_t normal, const float dist)
{
    int i, x;
    vec_t max, v;
    vec3_t org, vright, vup;
    winding_t *w;

    /* find the major axis */
    max = -VECT_MAX;
    x = -1;
    for (i = 0; i < 3; i++) {
        v = fabs(normal[i]);
        if (v > max) {
            x = i;
            max = v;
        }
    }
    if (x == -1)
        Error("%s: no axis found", __func__);

    VectorCopy(vec3_origin, vup);
    switch (x) {
    case 0:
    case 1:
        vup[2] = 1;
        break;
    case 2:
        vup[0] = 1;
        break;
    }

    v = DotProduct(vup, normal);
    VectorMA(vup, -v, normal, vup);
    VectorNormalize(vup);

    VectorScale(normal, dist, org);

    CrossProduct(vup, normal, vright);

    VectorScale(vup, 10e6, vup);
    VectorScale(vright, 10e6, vright);

    /* project a really big axis aligned box onto the plane */
    w = AllocWinding(4);

    VectorSubtract(org, vright, w->p[0]);
    VectorAdd(w->p[0], vup, w->p[0]);

    VectorAdd(org, vright, w->p[1]);
    VectorAdd(w->p[1], vup, w->p[1]);

    VectorAdd(org, vright, w->p[2]);
    VectorSubtract(w->p[2], vup, w->p[2]);

    VectorSubtract(org, vright, w->p[3]);
    VectorSubtract(w->p[3], vup, w->p[3]);

    w->numpoints = 4;

    return w;
}

/*
 * ==================
 * CopyWinding
 * ==================
 */
polylib::winding_t *
polylib::CopyWinding(const winding_t * w)
{
    if (w == nullptr) {
        return nullptr;
    }
    
    int size;
    winding_t *c;

    //size = offsetof(winding_t, p[w->numpoints]);
    size = offsetof(winding_t, p[0]);
    size += w->numpoints * sizeof(w->p[0]);

    c = static_cast<winding_t *>(malloc(size));
    memcpy(c, w, size);
    return c;
}


/*
 * =============
 * ClipWinding
 * =============
 */
void
polylib::ClipWinding(const winding_t * in, const vec3_t normal, vec_t dist,
            winding_t ** front, winding_t ** back)
{
    vec_t dists[MAX_POINTS_ON_WINDING + 4];
    int sides[MAX_POINTS_ON_WINDING + 4];
    int counts[3];
    vec_t dot;
    int i, j;
    const vec_t *p1, *p2;
    vec3_t mid;
    winding_t *f, *b;
    int maxpts;

    counts[0] = counts[1] = counts[2] = 0;

    /* determine sides for each point */
    for (i = 0; i < in->numpoints; i++) {
        dot = DotProduct(in->p[i], normal);
        dot -= dist;
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

    *front = *back = NULL;

    if (!counts[0]) {
        *back = CopyWinding(in);
        return;
    }
    if (!counts[1]) {
        *front = CopyWinding(in);
        return;
    }

    maxpts = in->numpoints + 4; /* can't use counts[0]+2 because */
    /* of fp grouping errors         */

    *front = f = AllocWinding(maxpts);
    *back = b = AllocWinding(maxpts);

    for (i = 0; i < in->numpoints; i++) {
        p1 = in->p[i];

        if (sides[i] == SIDE_ON) {
            VectorCopy(p1, f->p[f->numpoints]);
            f->numpoints++;
            VectorCopy(p1, b->p[b->numpoints]);
            b->numpoints++;
            continue;
        }

        if (sides[i] == SIDE_FRONT) {
            VectorCopy(p1, f->p[f->numpoints]);
            f->numpoints++;
        }
        if (sides[i] == SIDE_BACK) {
            VectorCopy(p1, b->p[b->numpoints]);
            b->numpoints++;
        }

        if (sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i])
            continue;

        /* generate a split point */
        p2 = in->p[(i + 1) % in->numpoints];

        dot = dists[i] / (dists[i] - dists[i + 1]);
        for (j = 0; j < 3; j++) {       /* avoid round off error when possible */
            if (normal[j] == 1)
                mid[j] = dist;
            else if (normal[j] == -1)
                mid[j] = -dist;
            else
                mid[j] = p1[j] + dot * (p2[j] - p1[j]);
        }

        VectorCopy(mid, f->p[f->numpoints]);
        f->numpoints++;
        VectorCopy(mid, b->p[b->numpoints]);
        b->numpoints++;
    }

    if (f->numpoints > maxpts || b->numpoints > maxpts)
        Error("%s: points exceeded estimate", __func__);
    if (f->numpoints > MAX_POINTS_ON_WINDING
        || b->numpoints > MAX_POINTS_ON_WINDING)
        Error("%s: MAX_POINTS_ON_WINDING", __func__);
}


/*
 * =================
 * ChopWinding
 * Returns the fragment of in that is on the front side
 * of the cliping plane.  The original is freed.
 * =================
 */
polylib::winding_t *
polylib::ChopWinding(winding_t * in, vec3_t normal, vec_t dist)
{
    winding_t *f, *b;

    ClipWinding(in, normal, dist, &f, &b);
    free(in);
    if (b)
        free(b);
    return f;
}

/*
 * =================
 * CheckWinding
 * =================
 */
void
polylib::CheckWinding(const winding_t * w)
{
    int i, j;
    const vec_t *p1, *p2;
    vec_t d, edgedist;
    vec3_t dir, edgenormal, facenormal;
    vec_t area;
    vec_t facedist;

    if (w->numpoints < 3)
        Error("%s: %i points", __func__, w->numpoints);

    area = WindingArea(w);
    if (area < 1)
        Error("%s: %f area", __func__, area);

    WindingPlane(w, facenormal, &facedist);

    for (i = 0; i < w->numpoints; i++) {
        p1 = w->p[i];

        for (j = 0; j < 3; j++)
            if (p1[j] > BOGUS_RANGE || p1[j] < -BOGUS_RANGE)
                Error("%s: BUGUS_RANGE: %f", __func__, p1[j]);

        j = i + 1 == w->numpoints ? 0 : i + 1;

        /* check the point is on the face plane */
        d = DotProduct(p1, facenormal) - facedist;
        if (d < -ON_EPSILON || d > ON_EPSILON)
            Error("%s: point off plane", __func__);

        /* check the edge isn't degenerate */
        p2 = w->p[j];
        VectorSubtract(p2, p1, dir);

        if (VectorLength(dir) < ON_EPSILON)
            Error("%s: degenerate edge", __func__);

        CrossProduct(facenormal, dir, edgenormal);
        VectorNormalize(edgenormal);
        edgedist = DotProduct(p1, edgenormal);
        edgedist += ON_EPSILON;

        /* all other points must be on front side */
        for (j = 0; j < w->numpoints; j++) {
            if (j == i)
                continue;
            d = DotProduct(w->p[j], edgenormal);
            if (d > edgedist)
                Error("%s: non-convex", __func__);
        }
    }
}

/*
 =============
 DiceWinding
 
 Chops the winding by a global grid.
 Calls save_fn on each subdivided chunk.
 Frees w.
 
 From q3rad (DicePatch)
 =============
 */
void	polylib::DiceWinding (winding_t *w, vec_t subdiv, save_winding_fn_t save_fn, void *userinfo)
{
    winding_t   *o1, *o2;
    vec3_t	mins, maxs;
    vec3_t	split;
    vec_t	dist;
    int		i;
    
    if (!w)
        return;
    
    WindingBounds (w, mins, maxs);
    for (i=0 ; i<3 ; i++)
        if (floor((mins[i]+1)/subdiv) < floor((maxs[i]-1)/subdiv))
            break;
    if (i == 3)
    {
        // no splitting needed
        save_fn(w, userinfo);
        return;
    }
    
    //
    // split the winding
    //
    VectorCopy (vec3_origin, split);
    split[i] = 1;
    dist = subdiv*(1+floor((mins[i]+1)/subdiv));
    ClipWinding (w, split, dist, &o1, &o2);
    free(w);
    
    //
    // create a new patch
    //
    DiceWinding(o1, subdiv, save_fn, userinfo);
    DiceWinding(o2, subdiv, save_fn, userinfo);
}

/*
 =============
 WindingFromFace
 From q2 tools
 =============
 */
polylib::winding_t *polylib::WindingFromFace (const mbsp_t *bsp, const bsp2_dface_t *f)
{
    int			i;
    int			se;
    dvertex_t	*dv;
    int			v;
    winding_t	*w;
    
    w = AllocWinding (f->numedges);
    w->numpoints = f->numedges;
    
    for (i=0 ; i<f->numedges ; i++)
    {
        se = bsp->dsurfedges[f->firstedge + i];
        if (se < 0)
            v = bsp->dedges[-se].v[1];
        else
            v = bsp->dedges[se].v[0];
        
        dv = &bsp->dvertexes[v];
        for (int j=0; j<3; j++) {
            w->p[i][j] = dv->point[j];
        }
    }
    
    RemoveColinearPoints (w);
    
    return w;
}

polylib::winding_edges_t *
polylib::AllocWindingEdges(const winding_t *w)
{
    plane_t p;
    WindingPlane(w, p.normal, &p.dist);
    
    winding_edges_t *result = (winding_edges_t *) calloc(1, sizeof(winding_edges_t));
    result->numedges = w->numpoints;
    result->planes = (plane_t *) calloc(w->numpoints, sizeof(plane_t));
    
    for (int i=0; i<w->numpoints; i++)
    {
        plane_t *dest = &result->planes[i];
        
        const vec_t *v0 = w->p[i];
        const vec_t *v1 = w->p[(i+1)%w->numpoints];
        
        vec3_t edgevec;
        VectorSubtract(v1, v0, edgevec);
        VectorNormalize(edgevec);
        
        CrossProduct(edgevec, p.normal, dest->normal);
        dest->dist = DotProduct(dest->normal, v0);
    }
    
    return result;
}

void
polylib::FreeWindingEdges(winding_edges_t *wi)
{
    free(wi->planes);
    free(wi);
}

bool
polylib::PointInWindingEdges(const winding_edges_t *wi, const vec3_t point)
{
    for (int i=0; i<wi->numedges; i++)
    {
        /* faces toward the center of the face */
        const plane_t *edgeplane = &wi->planes[i];
        
        vec_t dist = DotProduct(point, edgeplane->normal) - edgeplane->dist;
        if (dist < 0)
            return false;
    }
    return true;
}

std::vector<qvec3f> polylib::GLM_WindingPoints(const winding_t *w)
{
    std::vector<qvec3f> points;
    points.reserve(w->numpoints); //mxd. https://clang.llvm.org/extra/clang-tidy/checks/performance-inefficient-vector-operation.html
    for (int j = 0; j < w->numpoints; j++) {
        points.push_back(vec3_t_to_glm(w->p[j]));
    }
    return points;
}
