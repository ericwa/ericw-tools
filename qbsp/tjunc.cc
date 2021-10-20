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

// don't let a base face get past this
// because it can be split more later
constexpr size_t MAXPOINTS = 60;

namespace qv
{
    template<typename T>
    [[nodiscard]] constexpr int32_t compareEpsilon(const T &v1, const T &v2, const T &epsilon)
    {
        T diff = v1 - v2;
        return (diff > epsilon || diff < -epsilon) ? (diff < 0 ? -1 : 1) : 0;
    }

    template<typename T, size_t N>
    [[nodiscard]] inline int32_t compareEpsilon(const qvec<T, N> &v1, const qvec<T, N> &v2, const T &epsilon)
    {
        for (size_t i = 0; i < N; i++) {
            int32_t diff = compareEpsilon(v1[i], v2[i], epsilon);

            if (diff) {
                return diff;
            }
        }

        return 0;
    }
}

struct wedge_key_t
{
    qvec3d dir; /* direction vector for the edge */
    qvec3d origin; /* origin (t = 0) in parametric form */

    inline bool operator<(const wedge_key_t &other) const
    {
        int32_t diff = qv::compareEpsilon(dir, other.dir, EQUAL_EPSILON);

        if (diff) {
            return diff < 0;
        }

        diff = qv::compareEpsilon(origin, other.origin, EQUAL_EPSILON);

        if (diff) {
            return diff < 0;
        }

        return false;
    }
};

using wedge_t = std::list<vec_t>; /* linked list of vertices on this edge */

static int numwverts;
static int tjuncs;
static int tjuncfaces;

static std::map<wedge_key_t, wedge_t> pWEdges;

//============================================================================

static qvec3d CanonicalVector(const qvec3d &p1, const qvec3d &p2)
{
    qvec3d vec = p2 - p1;
    vec_t length = VectorNormalize(vec);

    for (size_t i = 0; i < 3; i++) {
        if (vec[i] > EQUAL_EPSILON) {
            return vec;
        } else if (vec[i] < -EQUAL_EPSILON) {
            VectorInverse(vec);
            return vec;
        } else {
            vec[i] = 0;
        }
    }

    LogPrint("WARNING: Line {}: Healing degenerate edge ({}) at ({:.3}\n", length, vec);
    return vec;
}

static std::pair<const wedge_key_t, wedge_t> &FindEdge(const qvec3d &p1, const qvec3d &p2, vec_t &t1, vec_t &t2)
{
    qvec3d edgevec = CanonicalVector(p1, p2);

    t1 = DotProduct(p1, edgevec);
    t2 = DotProduct(p2, edgevec);

    qvec3d origin = p1 + (edgevec * -t1);

    if (t1 > t2) {
        std::swap(t1, t2);
    }

    wedge_key_t key { edgevec, origin };
    auto it = pWEdges.find(key);

    if (it != pWEdges.end()) {
        return *it;
    }

    auto &edge = pWEdges.emplace(key, wedge_t { }).first;

    edge->second.emplace_front(VECT_MAX);

    return *edge;
}

/*
===============
AddVert

===============
*/
static void AddVert(wedge_t &edge, vec_t t)
{
    auto it = edge.begin();

    for (; it != edge.end(); it++) {
        if (fabs(*it - t) < T_EPSILON) {
            return;
        } else if (*it > t) {
            break;
        }
    }

    // insert a new wvert before v
    edge.insert(it, t);
    numwverts++;
}

/*
===============
AddEdge

===============
*/
static void AddEdge(const qvec3d &p1, const qvec3d &p2)
{
    vec_t t1, t2;
    auto &edge = FindEdge(p1, p2, t1, t2);
    AddVert(edge.second, t1);
    AddVert(edge.second, t2);
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

/*
 * superface is a large face used as intermediate stage in tjunc fixes,
 * can hold hundreds of edges if needed
 */
#define MAX_SUPERFACE_POINTS 8192

static void SplitFaceForTjunc(face_t *face, face_t *original, face_t **facelist)
{
    winding_t &w = face->w;
    face_t *newf, *chain;
    vec3_t edgevec[2];
    vec_t angle;
    int i, firstcorner, lastcorner;

    chain = NULL;
    do {
        if (w.size() <= MAXPOINTS) {
            /*
             * the face is now small enough without more cutting so
             * copy it back to the original
             */
            *original = *face;
            original->original = chain;
            original->next = *facelist;
            *facelist = original;
            return;
        }

        tjuncfaces++;

restart:
        /* find the last corner */
        VectorSubtract(w[w.size() - 1], w[0], edgevec[0]);
        VectorNormalize(edgevec[0]);
        for (lastcorner = w.size() - 1; lastcorner > 0; lastcorner--) {
            const qvec3d &p0 = w[lastcorner - 1];
            const qvec3d &p1 = w[lastcorner];
            VectorSubtract(p0, p1, edgevec[1]);
            VectorNormalize(edgevec[1]);
            angle = DotProduct(edgevec[0], edgevec[1]);
            if (angle < 1 - ANGLEEPSILON || angle > 1 + ANGLEEPSILON)
                break;
        }

        /* find the first corner */
        VectorSubtract(w[1], w[0], edgevec[0]);
        VectorNormalize(edgevec[0]);
        for (firstcorner = 1; firstcorner < w.size() - 1; firstcorner++) {
            const qvec3d &p0 = w[firstcorner + 1];
            const qvec3d &p1 = w[firstcorner];
            VectorSubtract(p0, p1, edgevec[1]);
            VectorNormalize(edgevec[1]);
            angle = DotProduct(edgevec[0], edgevec[1]);
            if (angle < 1 - ANGLEEPSILON || angle > 1 + ANGLEEPSILON)
                break;
        }

        if (firstcorner + 2 >= MAXPOINTS) {
            /* rotate the point winding */
            vec3_t point0;

            VectorCopy(w[0], point0);
            for (i = 1; i < w.size(); i++)
                VectorCopy(w[i], w[i - 1]);
            VectorCopy(point0, w[w.size() - 1]);
            goto restart;
        }

        /*
         * cut off as big a piece as possible, less than MAXPOINTS, and not
         * past lastcorner
         */
        newf = NewFaceFromFace(face);
        if (face->original)
            FError("original face still exists");

        newf->original = chain;
        chain = newf;
        newf->next = *facelist;
        *facelist = newf;
        if (w.size() - firstcorner <= MAXPOINTS)
            newf->w.resize(firstcorner + 2);
        else if (lastcorner + 2 < MAXPOINTS && w.size() - lastcorner <= MAXPOINTS)
            newf->w.resize(lastcorner + 2);
        else
            newf->w.resize(MAXPOINTS);

        for (i = 0; i < newf->w.size(); i++)
            newf->w[i] = w[i];
        for (i = newf->w.size() - 1; i < w.size(); i++)
            w[i - (newf->w.size() - 2)] = w[i];

        w.resize(w.size() - (newf->w.size() - 2));
    } while (1);
}

/*
===============
FixFaceEdges

===============
*/
static void FixFaceEdges(face_t *face, face_t *superface, face_t **facelist)
{
    *superface = *face;

restart:
    for (size_t i = 0; i < superface->w.size(); i++) {
        size_t j = (i + 1) % superface->w.size();
        
        vec_t t1, t2;
        auto &edge = FindEdge(superface->w[i], superface->w[j], t1, t2);

        auto it = edge.second.begin();
        while (*it < t1 + T_EPSILON)
            it++;

        if (*it < t2 - T_EPSILON) {
            /* insert a new vertex here */
            if (superface->w.size() == MAX_SUPERFACE_POINTS)
                FError("tjunc fixups generated too many edges (max {})", MAX_SUPERFACE_POINTS);

            tjuncs++;

            // FIXME: a bit of a silly way of handling this
            superface->w.push_back({});

            for (int32_t k = superface->w.size() - 1; k > j; k--)
                VectorCopy(superface->w[k - 1], superface->w[k]);

            superface->w[j] = edge.first.origin + (edge.first.dir * *it);
            goto restart;
        }
    }

    if (superface->w.size() <= MAXPOINTS) {
        *face = *superface;
        face->next = *facelist;
        *facelist = face;
        return;
    }

    /* Too many edges - needs to be split into multiple faces */
    SplitFaceForTjunc(superface, face, facelist);
}

//============================================================================

static void tjunc_find_r(node_t *node)
{
    if (node->planenum == PLANENUM_LEAF)
        return;

    for (face_t *f = node->faces; f; f = f->next)
        AddFaceEdges(f);

    tjunc_find_r(node->children[0]);
    tjunc_find_r(node->children[1]);
}

static void tjunc_fix_r(node_t *node, face_t *superface)
{
    if (node->planenum == PLANENUM_LEAF)
        return;

    face_t *facelist = nullptr;

    for (face_t *face = node->faces, *next = nullptr; face; face = next) {
        next = face->next;
        FixFaceEdges(face, superface, &facelist);
    }

    node->faces = facelist;

    tjunc_fix_r(node->children[0], superface);
    tjunc_fix_r(node->children[1], superface);
}

/*
===========
tjunc
===========
*/
void TJunc(const mapentity_t *entity, node_t *headnode)
{
    LogPrint(LOG_PROGRESS, "---- {} ----\n", __func__);

    pWEdges.clear();

    numwverts = 0;

    tjunc_find_r(headnode);

    LogPrint(LOG_STAT, "     {:8} world edges\n", pWEdges.size());
    LogPrint(LOG_STAT, "     {:8} edge points\n", numwverts);

    face_t superface;

    /* add extra vertexes on edges where needed */
    tjuncs = tjuncfaces = 0;
    tjunc_fix_r(headnode, &superface);

    LogPrint(LOG_STAT, "     {:8} edges added by tjunctions\n", tjuncs);
    LogPrint(LOG_STAT, "     {:8} faces added by tjunctions\n", tjuncfaces);
}
