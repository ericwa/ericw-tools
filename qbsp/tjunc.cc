/*
    Copyright (C) 1996-1997  Id Software, Inc.
    Copyright (C) 1997       Greg Lewis

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
// tjunc.c

#include <qbsp/qbsp.hh>

constexpr size_t MAXPOINTS = 60;

struct wvert_t
{
    vec_t t; /* t-value for parametric equation of edge */
    wvert_t *prev, *next; /* t-ordered list of vertices on same edge */
};

struct wedge_t
{
    wedge_t *next; /* pointer for hash bucket chain */
    qvec3d dir; /* direction vector for the edge */
    qvec3d origin; /* origin (t = 0) in parametric form */
    wvert_t head; /* linked list of verticies on this edge */
};


static int numwedges, numwverts;
static int tjuncs;
static int tjuncfaces;

static int cWVerts;
static int cWEdges;

static wvert_t *pWVerts;
static wedge_t *pWEdges;

//============================================================================

#define NUM_HASH 1024

static wedge_t *wedge_hash[NUM_HASH];
static qvec3d hash_min, hash_scale;

static void InitHash(const qvec3d &mins, const qvec3d &maxs)
{
    vec_t volume;
    vec_t scale;
    int newsize[2];

    hash_min = mins;
    qvec3d size = maxs - mins;
    memset(wedge_hash, 0, sizeof(wedge_hash));

    volume = size[0] * size[1];

    scale = sqrt(volume / NUM_HASH);

    newsize[0] = (int)(size[0] / scale);
    newsize[1] = (int)(size[1] / scale);

    hash_scale[0] = newsize[0] / size[0];
    hash_scale[1] = newsize[1] / size[1];
    hash_scale[2] = (vec_t)newsize[1];
}

static unsigned HashVec(const qvec3d &vec)
{
    unsigned h;

    h = (unsigned)(hash_scale[0] * (vec[0] - hash_min[0]) * hash_scale[2] + hash_scale[1] * (vec[1] - hash_min[1]));
    if (h >= NUM_HASH)
        return NUM_HASH - 1;
    return h;
}

//============================================================================

static void CanonicalVector(const qvec3d &p1, const qvec3d &p2, qvec3d &vec)
{
    vec = p2 - p1;
    vec_t length = qv::normalizeInPlace(vec);

    for (size_t i = 0; i < 3; i++) {
        if (vec[i] > EQUAL_EPSILON)
            return;
        else if (vec[i] < -EQUAL_EPSILON) {
            vec = -vec;
            return;
        } else {
            vec[i] = 0;
        }
    }

    // FIXME: Line {}: was here but no line number can be grabbed here?
    LogPrint("WARNING: Healing degenerate edge ({}) at ({:.3})\n", length, p1);
}

static wedge_t *FindEdge(const qvec3d &p1, const qvec3d &p2, vec_t &t1, vec_t &t2)
{
    qvec3d origin, edgevec;
    wedge_t *edge;
    int h;

    CanonicalVector(p1, p2, edgevec);

    t1 = qv::dot(p1, edgevec);
    t2 = qv::dot(p2, edgevec);

    origin = p1 + (edgevec * -t1);

    if (t1 > t2) {
        std::swap(t1, t2);
    }

    h = HashVec(origin);

    for (edge = wedge_hash[h]; edge; edge = edge->next) {
        vec_t temp = edge->origin[0] - origin[0];
        if (temp < -EQUAL_EPSILON || temp > EQUAL_EPSILON)
            continue;
        temp = edge->origin[1] - origin[1];
        if (temp < -EQUAL_EPSILON || temp > EQUAL_EPSILON)
            continue;
        temp = edge->origin[2] - origin[2];
        if (temp < -EQUAL_EPSILON || temp > EQUAL_EPSILON)
            continue;

        temp = edge->dir[0] - edgevec[0];
        if (temp < -EQUAL_EPSILON || temp > EQUAL_EPSILON)
            continue;
        temp = edge->dir[1] - edgevec[1];
        if (temp < -EQUAL_EPSILON || temp > EQUAL_EPSILON)
            continue;
        temp = edge->dir[2] - edgevec[2];
        if (temp < -EQUAL_EPSILON || temp > EQUAL_EPSILON)
            continue;

        return edge;
    }

    if (numwedges >= cWEdges)
        FError("Internal error: didn't allocate enough edges for tjuncs?");
    edge = pWEdges + numwedges;
    numwedges++;

    edge->next = wedge_hash[h];
    wedge_hash[h] = edge;

    edge->origin = origin;
    edge->dir = edgevec;
    edge->head.next = edge->head.prev = &edge->head;
    edge->head.t = VECT_MAX;

    return edge;
}

/*
===============
AddVert

===============
*/
static void AddVert(wedge_t *edge, vec_t t)
{
    wvert_t *v, *newv;

    v = edge->head.next;
    do {
        if (fabs(v->t - t) < T_EPSILON)
            return;
        if (v->t > t)
            break;
        v = v->next;
    } while (1);

    // insert a new wvert before v
    if (numwverts >= cWVerts)
        FError("Internal error: didn't allocate enough vertices for tjuncs?");

    newv = pWVerts + numwverts;
    numwverts++;

    newv->t = t;
    newv->next = v;
    newv->prev = v->prev;
    v->prev->next = newv;
    v->prev = newv;
}

/*
===============
AddEdge

===============
*/
static void AddEdge(const qvec3d &p1, const qvec3d &p2)
{
    vec_t t1, t2;
    wedge_t *edge = FindEdge(p1, p2, t1, t2);
    AddVert(edge, t1);
    AddVert(edge, t2);
}

/*
===============
AddFaceEdges

===============
*/
static void AddFaceEdges(face_t *f)
{
    for (size_t i = 0; i < f->w.size(); i++) {
        size_t j = (i + 1) % f->w.size();
        AddEdge(f->w[i], f->w[j]);
    }
}

//============================================================================

// If we hit this amount of points, there's probably an issue
// in the algorithm that is generating endless vertices.
constexpr size_t MAX_TJUNC_POINTS = 8192;

static void SplitFaceForTjunc(face_t *face)
{
    winding_t &w = face->w;
    qvec3d edgevec[2];
    vec_t angle;
    int i, firstcorner, lastcorner;

    do {
        if (w.size() <= MAXPOINTS) {
            // the face is now small enough without more cutting
            return;
        }

        tjuncfaces++;

restart:
        /* find the last corner */
        edgevec[0] = qv::normalize(w[w.size() - 1] - w[0]);
        for (lastcorner = w.size() - 1; lastcorner > 0; lastcorner--) {
            const qvec3d &p0 = w[lastcorner - 1];
            const qvec3d &p1 = w[lastcorner];
            edgevec[1] = qv::normalize(p0 - p1);
            angle = qv::dot(edgevec[0], edgevec[1]);
            if (angle < 1 - ANGLEEPSILON || angle > 1 + ANGLEEPSILON)
                break;
        }

        /* find the first corner */
        edgevec[0] = qv::normalize(w[1] - w[0]);
        for (firstcorner = 1; firstcorner < w.size() - 1; firstcorner++) {
            const qvec3d &p0 = w[firstcorner + 1];
            const qvec3d &p1 = w[firstcorner];
            edgevec[1] = qv::normalize(p0 - p1);
            angle = qv::dot(edgevec[0], edgevec[1]);
            if (angle < 1 - ANGLEEPSILON || angle > 1 + ANGLEEPSILON)
                break;
        }

        if (firstcorner + 2 >= MAXPOINTS) {
            /* rotate the point winding */
            qvec3d point0 = w[0];
            for (i = 1; i < w.size(); i++)
                w[i - 1] = w[i];
            w[w.size() - 1] = point0;
            goto restart;
        }

        /*
         * cut off as big a piece as possible, less than MAXPOINTS, and not
         * past lastcorner
         */
        winding_t neww(face->w);

        if (w.size() - firstcorner <= MAXPOINTS)
            neww.resize(firstcorner + 2);
        else if (lastcorner + 2 < MAXPOINTS && w.size() - lastcorner <= MAXPOINTS)
            neww.resize(lastcorner + 2);
        else
            neww.resize(MAXPOINTS);

        for (i = 0; i < neww.size(); i++)
            Q_assert(qv::equalExact(neww[i], w[i]));
        for (i = neww.size() - 1; i < w.size(); i++)
            w[i - (neww.size() - 2)] = w[i];

        w.resize(w.size() - (neww.size() - 2));

        face->fragments.push_back(face_fragment_t { std::move(neww) });
    } while (1);
}

/*
===============
FixFaceEdges

===============
*/
static void FixFaceEdges(face_t *face)
{
    int i, j;
    wedge_t *edge;
    wvert_t *v;
    vec_t t1, t2;

    for (i = 0; i < face->w.size(); ) {
        j = (i + 1) % face->w.size();

        edge = FindEdge(face->w[i], face->w[j], t1, t2);

        v = edge->head.next;
        while (v->t < t1 + T_EPSILON)
            v = v->next;

        if (v->t < t2 - T_EPSILON) {
            /* insert a new vertex here */
            if (face->w.size() >= MAX_TJUNC_POINTS) {
                FError("generated too many points (max {})", MAX_TJUNC_POINTS);
            }

            tjuncs++;

            face->w.resize(face->w.size() + 1);

            std::copy_backward(face->w.begin() + j, face->w.end() - 1, face->w.end());  

            face->w[j] = edge->origin + (edge->dir * v->t);

            i = 0;
            continue;
        }

        i++;
    }

    // we're good to go!
    if (face->w.size() <= MAXPOINTS) {
        return;
    }

    /* Too many edges - needs to be split into multiple faces */
    SplitFaceForTjunc(face);
}

//============================================================================

static void tjunc_count_r(node_t *node)
{
    if (node->planenum == PLANENUM_LEAF)
        return;

    for (face_t *f : node->facelist) {
        cWVerts += f->w.size();
    }

    tjunc_count_r(node->children[0]);
    tjunc_count_r(node->children[1]);
}

static void tjunc_find_r(node_t *node)
{
    if (node->planenum == PLANENUM_LEAF)
        return;

    for (face_t *f : node->facelist) {
        AddFaceEdges(f);
    }

    tjunc_find_r(node->children[0]);
    tjunc_find_r(node->children[1]);
}

static void tjunc_fix_r(node_t *node)
{
    if (node->planenum == PLANENUM_LEAF)
        return;

    for (face_t *face : node->facelist) {
        FixFaceEdges(face);
    }

    tjunc_fix_r(node->children[0]);
    tjunc_fix_r(node->children[1]);
}

/*
===========
tjunc
===========
*/
void TJunc(const mapentity_t *entity, node_t *headnode)
{
    LogPrint(LOG_PROGRESS, "---- {} ----\n", __func__);

    /*
     * Guess edges = 1/2 verts
     * Verts are arbitrarily multiplied by 2 because there appears to
     * be a need for them to "grow" slightly.
     */
    cWVerts = 0;
    tjunc_count_r(headnode);
    cWEdges = cWVerts;
    cWVerts *= 2;

    pWVerts = new wvert_t[cWVerts]{};
    pWEdges = new wedge_t[cWEdges]{};

    qvec3d maxs;
    /*
     * identify all points on common edges
     * origin points won't allways be inside the map, so extend the hash area
     */
    for (size_t i = 0; i < 3; i++) {
        if (fabs(entity->bounds.maxs()[i]) > fabs(entity->bounds.mins()[i]))
            maxs[i] = fabs(entity->bounds.maxs()[i]);
        else
            maxs[i] = fabs(entity->bounds.mins()[i]);
    }
    qvec3d mins = -maxs;

    InitHash(mins, maxs);

    numwedges = numwverts = 0;

    tjunc_find_r(headnode);

    LogPrint(LOG_STAT, "     {:8} world edges\n", numwedges);
    LogPrint(LOG_STAT, "     {:8} edge points\n", numwverts);

    /* add extra vertexes on edges where needed */
    tjuncs = tjuncfaces = 0;
    tjunc_fix_r(headnode);

    delete[] pWVerts;
    delete[] pWEdges;

    LogPrint(LOG_STAT, "     {:8} edges added by tjunctions\n", tjuncs);
    LogPrint(LOG_STAT, "     {:8} faces added by tjunctions\n", tjuncfaces);
}
