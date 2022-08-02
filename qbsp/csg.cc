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
// csg4.c

#include <qbsp/brush.hh>
#include <qbsp/csg.hh>
#include <qbsp/map.hh>
#include <qbsp/brushbsp.hh>
#include <qbsp/qbsp.hh>

#include <atomic>
#include <mutex>

#include "tbb/parallel_for.h"

/*

NOTES
-----
Brushes that touch still need to be split at the cut point to make a tjunction

*/

/*
==================
NewFaceFromFace

Duplicates the non point information of a face, used by SplitFace and
MergeFace.
==================
*/
std::unique_ptr<face_t> NewFaceFromFace(const face_t *in)
{
    auto newf = std::make_unique<face_t>();

    newf->planenum = in->planenum;
    newf->texinfo = in->texinfo;
    newf->contents = in->contents;
    newf->lmshift = in->lmshift;

    newf->origin = in->origin;
    newf->radius = in->radius;

    return newf;
}

std::unique_ptr<face_t> CopyFace(const face_t *in)
{
    auto temp = NewFaceFromFace(in);
    temp->w = in->w;
    return temp;
}

void UpdateFaceSphere(face_t *in)
{
    in->origin = in->w.center();
    in->radius = 0;
    for (size_t i = 0; i < in->w.size(); i++) {
        in->radius = max(in->radius, qv::distance2(in->w[i], in->origin));
    }
    in->radius = sqrt(in->radius);
}

/*
==================
SplitFace

Frees in. Returns {front, back}
==================
*/
std::tuple<std::unique_ptr<face_t>, std::unique_ptr<face_t>> SplitFace(
    std::unique_ptr<face_t> in, const qplane3d &split)
{
    if (in->w.size() < 0)
        Error("Attempting to split freed face");

    /* Fast test */
    double dot = split.distance_to(in->origin);
    if (dot > in->radius) {
        // all in front
        return {std::move(in), nullptr};
    } else if (dot < -in->radius) {
        // all behind
        return {nullptr, std::move(in)};
    }

    auto [front_winding, back_winding] = in->w.clip(split, qbsp_options.epsilon.value(), true);

    if (front_winding && !back_winding) {
        // all in front
        return {std::move(in), nullptr};
    } else if (back_winding && !front_winding) {
        // all behind
        return {nullptr, std::move(in)};
    }

    auto new_front = NewFaceFromFace(in.get());
    new_front->w = std::move(front_winding.value());

    auto new_back = NewFaceFromFace(in.get());
    new_back->w = std::move(back_winding.value());

    return {std::move(new_front), std::move(new_back)};
}

std::vector<std::unique_ptr<bspbrush_t>> MakeBspBrushList(mapentity_t *entity)
{
    // set the original pointers
    std::vector<std::unique_ptr<bspbrush_t>> brushcopies;
    for (const auto &original : entity->brushes) {
        auto copy = original->copy_unique();
        copy->original = original.get();
        brushcopies.push_back(std::move(copy));
    }

    return brushcopies;
}
