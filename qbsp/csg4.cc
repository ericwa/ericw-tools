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

#include <qbsp/qbsp.hh>

#include <atomic>
#include <mutex>

#include "tbb/parallel_for.h"

/*

NOTES
-----
Brushes that touch still need to be split at the cut point to make a tjunction

*/

static std::atomic<int> brushfaces;
static int csgfaces;
int csgmergefaces;

// acquire this for anything that can't run in parallel during CSGFaces
std::mutex csgfaces_lock;

/*
==================
MakeSkipTexinfo
==================
*/
static int MakeSkipTexinfo()
{
    // FindMiptex, FindTexinfo not threadsafe
    std::unique_lock<std::mutex> lck{csgfaces_lock};

    mtexinfo_t mt{};

    mt.miptex = FindMiptex("skip", true);
    mt.flags = {};
    mt.flags.is_skip = true;

    return FindTexinfo(mt);
}

/*
==================
NewFaceFromFace

Duplicates the non point information of a face, used by SplitFace and
MergeFace.
==================
*/
face_t *NewFaceFromFace(face_t *in)
{
    face_t *newf = new face_t{};

    newf->planenum = in->planenum;
    newf->texinfo = in->texinfo;
    newf->planeside = in->planeside;
    newf->original = in->original;
    newf->contents = in->contents;
    newf->lmshift = in->lmshift;
    newf->src_entity = in->src_entity;

    newf->origin = in->origin;
    newf->radius = in->radius;

    return newf;
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

Frees in.
==================
*/
void SplitFace(face_t *in, const qplane3d &split, face_t **front, face_t **back)
{
    vec_t *dists = (vec_t *)alloca(sizeof(vec_t) * (in->w.size() + 1));
    side_t *sides = (side_t *)alloca(sizeof(side_t) * (in->w.size() + 1));
    std::array<size_t, SIDE_TOTAL> counts { };
    vec_t dot;
    size_t i, j;
    face_t *newf, *new2;
    qvec3d mid;

    if (in->w.size() < 0)
        Error("Attempting to split freed face");

    /* Fast test */
    dot = split.distance_to(in->origin);
    if (dot > in->radius) {
        counts[SIDE_FRONT] = 1;
        counts[SIDE_BACK] = 0;
    } else if (dot < -in->radius) {
        counts[SIDE_FRONT] = 0;
        counts[SIDE_BACK] = 1;
    } else {
        counts = in->w.calc_sides(split, dists, sides, ON_EPSILON);
    }

    // Plane doesn't split this face after all
    if (!counts[SIDE_FRONT]) {
        *front = NULL;
        *back = in;
        return;
    }
    if (!counts[SIDE_BACK]) {
        *front = in;
        *back = NULL;
        return;
    }

    *back = newf = NewFaceFromFace(in);
    *front = new2 = NewFaceFromFace(in);

    // distribute the points and generate splits
    for (i = 0; i < in->w.size(); i++) {
        // Note: Possible for numpoints on newf or new2 to exceed MAXEDGES if
        // in->w.numpoints == MAXEDGES and it is a really devious split.
        const qvec3d &p1 = in->w[i];

        if (sides[i] == SIDE_ON) {
            newf->w.push_back(p1);
            new2->w.push_back(p1);
            continue;
        }

        if (sides[i] == SIDE_FRONT) {
            new2->w.push_back(p1);
        } else {
            newf->w.push_back(p1);
        }

        if (sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i])
            continue;

        // generate a split point
        const qvec3d &p2 = in->w[(i + 1) % in->w.size()];

        dot = dists[i] / (dists[i] - dists[i + 1]);
        for (j = 0; j < 3; j++) { // avoid round off error when possible
            if (split.normal[j] == 1)
                mid[j] = split.dist;
            else if (split.normal[j] == -1)
                mid[j] = -split.dist;
            else
                mid[j] = p1[j] + dot * (p2[j] - p1[j]);
        }

        newf->w.push_back(mid);
        new2->w.push_back(mid);
    }

    if (newf->w.size() > MAXEDGES || new2->w.size() > MAXEDGES)
        FError("Internal error: numpoints > MAXEDGES");

    /* free the original face now that it is represented by the fragments */
    delete in;
}

/*
=================
RemoveOutsideFaces

Quick test before running ClipInside; move any faces that are completely
outside the brush to the outside list, without splitting them. This saves us
time in mergefaces later on (and sometimes a lot of memory)
=================
*/
static void RemoveOutsideFaces(const brush_t &brush, face_t **inside, face_t **outside)
{
    face_t *face = *inside;
    face_t *next = nullptr;
    *inside = NULL;
    while (face) {
        next = face->next;
        std::optional<winding_t> w = face->w;
        for (auto &clipface : brush.faces) {
            qbsp_plane_t clipplane = map.planes[clipface.planenum];
            if (!clipface.planeside) {
                clipplane = -clipplane;
            }
            w = w->clip(clipplane, ON_EPSILON, true)[SIDE_FRONT];
            if (!w)
                break;
        }
        if (!w) {
            /* The face is completely outside this brush */
            face->next = *outside;
            *outside = face;
        } else {
            face->next = *inside;
            *inside = face;
        }
        face = next;
    }
}

/*
=================
ClipInside

Clips all of the faces in the inside list, possibly moving them to the
outside list or spliting it into a piece in each list.

Faces exactly on the plane will stay inside unless overdrawn by later brush
=================
*/
static void ClipInside(const face_t *clipface, bool precedence, face_t **inside, face_t **outside)
{
    face_t *face, *next, *frags[2];

    const qbsp_plane_t &splitplane = map.planes[clipface->planenum];

    face = *inside;
    *inside = NULL;
    while (face) {
        next = face->next;

        /* HACK: Check for on-plane but not the same planenum
          ( https://github.com/ericwa/ericw-tools/issues/174 )
         */
        bool spurious_onplane = false;
        {
            std::array<size_t, SIDE_TOTAL> counts = face->w.calc_sides(splitplane, nullptr, nullptr, ON_EPSILON);

            if (counts[SIDE_ON] && !counts[SIDE_FRONT] && !counts[SIDE_BACK]) {
                spurious_onplane = true;
            }
        }

        /* Handle exactly on-plane faces */
        if (face->planenum == clipface->planenum || spurious_onplane) {
            const qplane3d faceplane = Face_Plane(face);
            const qplane3d clipfaceplane = Face_Plane(clipface);
            const vec_t dp = qv::dot(faceplane.normal, clipfaceplane.normal);
            const bool opposite = (dp < 0);

            if (opposite || precedence) {
                /* always clip off opposite facing */
                frags[clipface->planeside] = NULL;
                frags[!clipface->planeside] = face;
            } else {
                /* leave it on the outside */
                frags[clipface->planeside] = face;
                frags[!clipface->planeside] = NULL;
            }
        } else {
            /* proper split */
            SplitFace(face, splitplane, &frags[0], &frags[1]);
        }

        if (frags[clipface->planeside]) {
            frags[clipface->planeside]->next = *outside;
            *outside = frags[clipface->planeside];
        }
        if (frags[!clipface->planeside]) {
            frags[!clipface->planeside]->next = *inside;
            *inside = frags[!clipface->planeside];
        }
        face = next;
    }
}

/*
==================
SaveFacesToPlaneList

Links the given list of faces into a mapping from plane number to faces.
This plane map is later used to build up the surfaces for creating the BSP.

Not parallel.
==================
*/
static void SaveFacesToPlaneList(face_t *facelist, bool mirror, std::map<int, std::list<face_t *>> &planefaces)
{
    face_t *face, *next;

    for (face = facelist; face; face = next) {
        const int plane = face->planenum;
        next = face->next;

        // Handy for debugging CSGFaces issues, throw out detail faces and export obj.
#if 0
        if (face->contents[1] == CONTENTS_DETAIL)
            continue;
#endif

        // returns empty (and inserts it) if plane is not in the map yet
        std::list<face_t *> &plane_current_facelist = planefaces[plane];

        if (mirror) {
            face_t *newface = NewFaceFromFace(face);
            newface->w = face->w.flip();
            newface->planeside = face->planeside ^ 1;
            newface->contents.swap();
            newface->lmshift.swap();

            // e.g. for a water volume:
            // the face facing the air:
            //   - face->contents[0] is CONTENTS_EMPTY
            //   - face->contents[1] is CONTENTS_WATER
            // the face facing the water:
            //   - newface->contents[0] is CONTENTS_WATER
            //   - newface->contents[1] is CONTENTS_EMPTY

            // HACK: We only want this mirrored face for CONTENTS_DETAIL
            // to force the right content type for the leaf, but we don't actually
            // want the face. So just set the texinfo to "skip" so it gets deleted.
            if ((face->contents[1].is_detail() || (face->contents[1].extended & CFLAGS_WAS_ILLUSIONARY)) ||
                (options.fContentHack && face->contents[1].is_solid(options.target_game))) {

                // if CFLAGS_BMODEL_MIRROR_INSIDE is set, never change to skip
                if (!(face->contents[1].extended & CFLAGS_BMODEL_MIRROR_INSIDE)) {
                    newface->texinfo = MakeSkipTexinfo();
                }
            }

            MergeFaceToList(newface, plane_current_facelist);
        }
        MergeFaceToList(face, plane_current_facelist);

        csgfaces++;
    }
}

static void FreeFaces(face_t *face)
{
    face_t *next;

    while (face) {
        next = face->next;
        delete face;
        face = next;
    }
}

/*
==================
SaveInsideFaces

`face` is the list of faces from `brush` (solid) that are inside `clipbrush` (nonsolid)

Save the list of faces onto the output list, modifying the outside contents to
match given brush. If the inside contents are empty, the given brush's
contents override the face inside contents.
==================
*/
static void SaveInsideFaces(face_t *face, const brush_t &clipbrush, face_t **savelist)
{
    Q_assert(!clipbrush.contents.is_solid(options.target_game));

    face_t *next;

    while (face) {
        // the back side of `face` is a solid
        // Q_assert(face->contents[1] == CONTENTS_SOLID);

        next = face->next;
        face->contents[0] = clipbrush.contents;

        if ((face->contents[1].is_solid(options.target_game) || face->contents[1].is_sky(options.target_game)) &&
            clipbrush.contents.is_detail(CFLAGS_DETAIL)) {
            // This case is when a structural and detail brush are touching,
            // and we want to save the structural face that is
            // touching detail.
            //
            // We just marked face->contents[0] as CONTENTS_DETAIL which will
            // break things, because this is turning a structural face into
            // detail.
            //
            // As a sort-of-hack, mark it as empty. Example:
            // a detail light fixture touching a structural wall.
            // The covered-up structural face on the wall has it's "front"
            // marked as empty here, and the detail faces have their "back"
            // marked as detail.

            int32_t old_native = face->contents[0].native;
            face->contents[0] = options.target_game->create_empty_contents(CFLAGS_STRUCTURAL_COVERED_BY_DETAIL);
            face->contents[0].covered_native = old_native;
            face->texinfo = MakeSkipTexinfo();
        }

        // N.B.: We don't need a hack like above for when clipbrush->contents == CONTENTS_DETAIL_ILLUSIONARY.

        // These would create leaks
        Q_assert(!(face->contents[1].is_solid(options.target_game) && face->contents[0].is_detail(CFLAGS_DETAIL)));
        Q_assert(!(face->contents[1].is_sky(options.target_game) && face->contents[0].is_detail(CFLAGS_DETAIL)));

        /*
         * If the inside brush is empty space, inherit the outside contents.
         * The only brushes with empty contents currently are hint brushes.
         */
        if (face->contents[1].is_empty(options.target_game)) {
            face->contents[1] = clipbrush.contents;
        }
        if (face->contents[1].is_detail(CFLAGS_DETAIL_ILLUSIONARY)) {
            bool wasMirrorInside = !!(face->contents[1].extended & CFLAGS_BMODEL_MIRROR_INSIDE);
            face->contents[1] = clipbrush.contents;
            face->contents[1].extended |= CFLAGS_WAS_ILLUSIONARY;
            if (wasMirrorInside) {
                face->contents[1].extended |= CFLAGS_BMODEL_MIRROR_INSIDE;
            }
        }

        face->next = *savelist;
        *savelist = face;
        face = next;
    }
}

//==========================================================================

/*
==================
BuildSurfaces

Returns a chain of all the surfaces for all the planes with one or more
visible face.

Not parallel.
==================
*/
std::vector<surface_t> BuildSurfaces(std::map<int, std::list<face_t *>> &planefaces)
{
    std::vector<surface_t> surfaces;

    for (int i = 0; i < map.numplanes(); i++) {
        const auto entry = planefaces.find(i);

        if (entry == planefaces.end())
            continue;

        Q_assert(!entry->second.empty());

        /* create a new surface to hold the faces on this plane */
        surface_t &surf = surfaces.emplace_back();
        surf.planenum = entry->first;
        surf.faces = std::move(entry->second);
        csgmergefaces += surf.faces.size();

        /* Calculate bounding box and flags */
        surf.calculateInfo();
    }

    return surfaces;
}

//==========================================================================

/*
==================
CopyBrushFaces
==================
*/
static face_t *CopyBrushFaces(const brush_t &brush)
{
    face_t *facelist, *newface;

    facelist = NULL;
    for (auto &face : brush.faces) {
        brushfaces++;
        newface = new face_t(face);
        newface->contents = { options.target_game->create_empty_contents(), brush.contents };
        newface->lmshift = { brush.lmshift, brush.lmshift };
        newface->next = facelist;
        facelist = newface;
    }

    return facelist;
}

/*
==================
CSGFaces

Returns a list of surfaces containing all of the faces
==================
*/
std::vector<surface_t> CSGFaces(const mapentity_t *entity)
{
    LogPrint(LOG_PROGRESS, "---- {} ----\n", __func__);

    csgfaces = 0;
    brushfaces = 0;
    csgmergefaces = 0;

#if 0
    LogPrint("CSGFaces brush order:\n");
    for (brush = entity->brushes; brush; brush = brush->next) {
        LogPrint("    {} ({})\n", map.texinfoTextureName(brush->faces->texinfo), brush->contents.to_string(options.target_game));
    }
#endif

    // output vector for the parallel_for
    std::vector<face_t *> brushvec_outsides;
    brushvec_outsides.resize(entity->brushes.size());

    /*
     * For each brush, clip away the parts that are inside other brushes.
     * Solid brushes override non-solid brushes.
     *   brush     => the brush to be clipped
     *   clipbrush => the brush we are clipping against
     *
     * The output of this is a face list for each brush called "outside"
     */
    tbb::parallel_for(static_cast<size_t>(0), entity->brushes.size(), [entity, &brushvec_outsides](const size_t i) {
        auto &brush = entity->brushes[i];
        face_t *outside = CopyBrushFaces(brush);
        bool overwrite = false;

        for (auto &clipbrush : entity->brushes) {
            if (&brush == &clipbrush) {
                /* Brushes further down the list overried earlier ones */
                overwrite = true;
                continue;
            }
            if (clipbrush.contents.is_empty(options.target_game)) {
                /* Ensure hint never clips anything */
                continue;
            }

            if (clipbrush.contents.is_detail(CFLAGS_DETAIL_ILLUSIONARY) &&
                !brush.contents.is_detail(CFLAGS_DETAIL_ILLUSIONARY)) {
                /* CONTENTS_DETAIL_ILLUSIONARY never clips anything but itself */
                continue;
            }

            if (clipbrush.contents.is_detail(CFLAGS_DETAIL_FENCE) && !brush.contents.is_detail(CFLAGS_DETAIL_FENCE)) {
                /* CONTENTS_DETAIL_FENCE never clips anything but itself */
                continue;
            }

            if (clipbrush.contents.types_equal(brush.contents, options.target_game) &&
                !clipbrush.contents.clips_same_type()) {
                /* _noclipfaces key */
                continue;
            }

            /* check bounding box first */
            // TODO: is this a disjoint check? brush->bounds.disjoint(clipbrush->bounds)?
            int i;
            for (i = 0; i < 3; i++) {
                if (brush.bounds.mins()[i] > clipbrush.bounds.maxs()[i])
                    break;
                if (brush.bounds.maxs()[i] < clipbrush.bounds.mins()[i])
                    break;
            }
            if (i < 3)
                continue;

            /*
             * TODO - optimise by checking for opposing planes?
             *  => brushes can't intersect
             */

            // divide faces by the planes of the new brush
            face_t *inside = outside;
            outside = NULL;

            RemoveOutsideFaces(clipbrush, &inside, &outside);
            for (auto &clipface : clipbrush.faces)
                ClipInside(&clipface, overwrite, &inside, &outside);

            // inside = parts of `brush` that are inside `clipbrush`
            // outside = parts of `brush` that are outside `clipbrush`

            /*
             * If the brush is solid and the clipbrush is not, then we need to
             * keep the inside faces and set the outside contents to those of
             * the clipbrush. Otherwise, these inside surfaces are hidden and
             * should be discarded.
             *
             * FIXME: clean this up, the predicate seems to be "can you see 'brush' from inside 'clipbrush'"
             */
            if ((brush.contents.is_solid(options.target_game) && !clipbrush.contents.is_solid(options.target_game))

                ||
                (brush.contents.is_sky(options.target_game) && (!clipbrush.contents.is_solid(options.target_game) &&
                                                                    !clipbrush.contents.is_sky(options.target_game)))

                || (brush.contents.is_detail(CFLAGS_DETAIL) && (!clipbrush.contents.is_solid(options.target_game) &&
                                                                    !clipbrush.contents.is_sky(options.target_game) &&
                                                                    !clipbrush.contents.is_detail(CFLAGS_DETAIL)))

                || (brush.contents.is_liquid(options.target_game) &&
                       clipbrush.contents.is_detail(CFLAGS_DETAIL_ILLUSIONARY))

                || (brush.contents.is_fence() && clipbrush.contents.is_liquid(options.target_game))) {
                SaveInsideFaces(inside, clipbrush, &outside);
            } else {
                FreeFaces(inside);
            }
        }

        // save the result
        brushvec_outsides[i] = outside;
    });

    // Non parallel part:
    std::map<int, std::list<face_t *>> planefaces;
    for (size_t i = 0; i < entity->brushes.size(); ++i) {
        const brush_t &brush = entity->brushes[i];
        face_t *outside = brushvec_outsides[i];

        /*
         * All of the faces left on the outside list are real surface faces
         * If the brush is non-solid, mirror faces for the inside view
         */
        const bool mirror = options.fContentHack ? true : !brush.contents.is_solid(options.target_game);
        SaveFacesToPlaneList(outside, mirror, planefaces);
    }

    std::vector<surface_t> surfaces = BuildSurfaces(planefaces);

    LogPrint(LOG_STAT, "     {:8} brushfaces\n", brushfaces.load());
    LogPrint(LOG_STAT, "     {:8} csgfaces\n", csgfaces);
    LogPrint(LOG_STAT, "     {:8} mergedfaces\n", csgmergefaces);
    LogPrint(LOG_STAT, "     {:8} surfaces\n", surfaces.size());

    return surfaces;
}
