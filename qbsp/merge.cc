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
// merge.c

#include <qbsp/merge.hh>

#include <common/log.hh>
#include <qbsp/qbsp.hh>
#include <qbsp/csg.hh>
#include <qbsp/map.hh>
#include <qbsp/faces.hh>

#ifdef PARANOID
static void CheckColinear(face_t *f)
{
    int i, j;
    qvec3d v1, v2;

    for (i = 0; i < f->w.numpoints; i++) {
        // skip the point if the vector from the previous point is the same
        // as the vector to the next point
        j = (i - 1 < 0) ? f->w.numpoints - 1 : i - 1;
        v1 = qv::normalize(f->w.points[i] - f->w.points[j]);

        j = (i + 1 == f->w.numpoints) ? 0 : i + 1;
        v2 = qv::normalize(f->w.points[j] - f->w.points[i]);

        if (qv::epsilonEqual(v1, v2, QBSP_EQUAL_EPSILON))
            FError("Colinear edge");
    }
}
#endif /* PARANOID */

/*
=============
TryMerge

If two polygons share a common edge and the edges that meet at the
common points are both inside the other polygons, merge them

Returns NULL if the faces couldn't be merged, or the new face.
The originals will NOT be freed.
=============
*/
static std::unique_ptr<face_t> TryMerge(const face_t *f1, const face_t *f2)
{
    qvec3d p1, p2, p3, p4, back;
    int i, j, k, l;
    qvec3d normal, delta;
    double dot;
    bool keep1, keep2;

    if (!f1->w.size() || !f2->w.size() || f1->planenum != f2->planenum || f1->texinfo != f2->texinfo ||
        f1->original_side->lmshift != f2->original_side->lmshift)
        return NULL;

    // block merging across water boundaries;
    // ezQuake/nQuake (Q1) and Paintball2 (Q2) water caustics will leak onto
    // above-water faces.
    if (f1->contents[0].is_liquid() != f2->contents[0].is_liquid())
        return nullptr;

    // Q1: don't merge across sky boundary - we delete faces inside sky
    if (f1->contents[0].is_sky() != f2->contents[0].is_sky())
        return nullptr;

    // find a common edge
    p1 = p2 = NULL; // stop compiler warning
    j = 0; //

    for (i = 0; i < f1->w.size(); i++) {
        p1 = f1->w[i];
        p2 = f1->w[(i + 1) % f1->w.size()];
        for (j = 0; j < f2->w.size(); j++) {
            p3 = f2->w[j];
            p4 = f2->w[(j + 1) % f2->w.size()];
            for (k = 0; k < 3; k++) {
                if (fabs(p1[k] - p4[k]) > QBSP_EQUAL_EPSILON || fabs(p2[k] - p3[k]) > QBSP_EQUAL_EPSILON)
                    break;
            }
            if (k == 3)
                break;
        }
        if (j < f2->w.size())
            break;
    }

    if (i == f1->w.size())
        return NULL; // no matching edges

    // check slope of connected lines
    // if the slopes are colinear, the point can be removed
    const qvec3d &planenormal = f1->get_plane().get_normal();

    back = f1->w[(i + f1->w.size() - 1) % f1->w.size()];
    delta = p1 - back;
    normal = qv::normalize(qv::cross(planenormal, delta));

    back = f2->w[(j + 2) % f2->w.size()];
    delta = back - p1;
    dot = qv::dot(delta, normal);
    if (dot > CONTINUOUS_EPSILON)
        return NULL; // not a convex polygon
    keep1 = dot < -CONTINUOUS_EPSILON;

    back = f1->w[(i + 2) % f1->w.size()];
    delta = back - p2;
    normal = qv::normalize(qv::cross(planenormal, delta));

    back = f2->w[(j + f2->w.size() - 1) % f2->w.size()];
    delta = back - p2;
    dot = qv::dot(delta, normal);
    if (dot > CONTINUOUS_EPSILON)
        return NULL; // not a convex polygon
    keep2 = dot < -CONTINUOUS_EPSILON;

    // build the new polygon
    std::unique_ptr<face_t> newf = NewFaceFromFace(f1);

    // copy first polygon
    if (keep2)
        k = (i + 1) % f1->w.size();
    else
        k = (i + 2) % f1->w.size();
    for (; k != i; k = (k + 1) % f1->w.size()) {
        newf->w.push_back(f1->w[k]);
    }

    // copy second polygon
    if (keep1)
        l = (j + 1) % f2->w.size();
    else
        l = (j + 2) % f2->w.size();
    for (; l != j; l = (l + 1) % f2->w.size()) {
        newf->w.push_back(f2->w[l]);
    }

    UpdateFaceSphere(newf.get());

    return newf;
}

/*
===============
MergeFaceToList
===============
*/
void MergeFaceToList(
    std::unique_ptr<face_t> face, std::list<std::unique_ptr<face_t>> &list, logging::stat_tracker_t::stat &num_merged)
{
    for (auto it = list.begin(); it != list.end();) {
#ifdef PARANOID
        CheckColinear(f);
#endif
        std::unique_ptr<face_t> newf = TryMerge(face.get(), it->get());

        if (newf) {
            list.erase(it);
            // restart, now trying to merge `newf` into the list
            face = std::move(newf);
            it = list.begin();
            num_merged++;
        } else {
            it++;
        }
    }

    list.push_back(std::move(face));
}

/*
===============
MergeFaceList
===============
*/
std::list<std::unique_ptr<face_t>> MergeFaceList(
    std::list<std::unique_ptr<face_t>> input, logging::stat_tracker_t::stat &num_merged)
{
    std::list<std::unique_ptr<face_t>> result;

    for (auto &face : input) {
        MergeFaceToList(std::move(face), result, num_merged);
    }

    return result;
}
