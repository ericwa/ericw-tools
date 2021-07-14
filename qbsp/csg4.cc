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

#include "tbb/parallel_for.h"

/*

NOTES
-----
Brushes that touch still need to be split at the cut point to make a tjunction

*/

static int brushfaces;
static int csgfaces;
int csgmergefaces;

/*
==================
MakeSkipTexinfo
==================
*/
static int
MakeSkipTexinfo()
{
    int texinfo;
    mtexinfo_t mt;
    
    mt.miptex = FindMiptex("skip");
    mt.flags = TEX_SKIP;
    memset(&mt.vecs, 0, sizeof(mt.vecs));
    
    texinfo = FindTexinfo(&mt, mt.flags);
    
    return texinfo;
}

/*
==================
NewFaceFromFace

Duplicates the non point information of a face, used by SplitFace and
MergeFace.
==================
*/
face_t *
NewFaceFromFace(face_t *in)
{
    face_t *newf;

    newf = (face_t *)AllocMem(FACE, 1, true);

    newf->planenum = in->planenum;
    newf->texinfo = in->texinfo;
    newf->planeside = in->planeside;
    newf->original = in->original;
    newf->contents[0] = in->contents[0];
    newf->contents[1] = in->contents[1];
    newf->cflags[0] = in->cflags[0];
    newf->cflags[1] = in->cflags[1];
    newf->lmshift[0] = in->lmshift[0];
    newf->lmshift[1] = in->lmshift[1];

    VectorCopy(in->origin, newf->origin);
    newf->radius = in->radius;

    return newf;
}

void
UpdateFaceSphere(face_t *in)
{
    int i;
    vec3_t radius;
    vec_t lensq;

    MidpointWinding(&in->w, in->origin);
    in->radius = 0;
    for (i = 0; i < in->w.numpoints; i++) {
        VectorSubtract(in->w.points[i], in->origin, radius);
        lensq = VectorLengthSq(radius);
        if (lensq > in->radius)
            in->radius = lensq;
    }
    in->radius = sqrt(in->radius);
}


/*
==================
SplitFace
 
Frees in.
==================
*/
void
SplitFace(face_t *in, const qbsp_plane_t *split, face_t **front, face_t **back)
{
    vec_t dists[MAXEDGES + 1];
    int sides[MAXEDGES + 1];
    int counts[3];
    vec_t dot;
    int i, j;
    face_t *newf, *new2;
    vec_t *p1, *p2;
    vec3_t mid;

    if (in->w.numpoints < 0)
        Error("Attempting to split freed face");

    /* Fast test */
    dot = DotProduct(in->origin, split->normal) - split->dist;
    if (dot > in->radius) {
        counts[SIDE_FRONT] = 1;
        counts[SIDE_BACK] = 0;
    } else if (dot < -in->radius) {
        counts[SIDE_FRONT] = 0;
        counts[SIDE_BACK] = 1;
    } else {
        CalcSides(&in->w, split, sides, dists, counts);
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
    for (i = 0; i < in->w.numpoints; i++) {
        // Note: Possible for numpoints on newf or new2 to exceed MAXEDGES if
        // in->w.numpoints == MAXEDGES and it is a really devious split.

        p1 = in->w.points[i];

        if (sides[i] == SIDE_ON) {
            VectorCopy(p1, newf->w.points[newf->w.numpoints]);
            newf->w.numpoints++;
            VectorCopy(p1, new2->w.points[new2->w.numpoints]);
            new2->w.numpoints++;
            continue;
        }

        if (sides[i] == SIDE_FRONT) {
            VectorCopy(p1, new2->w.points[new2->w.numpoints]);
            new2->w.numpoints++;
        } else {
            VectorCopy(p1, newf->w.points[newf->w.numpoints]);
            newf->w.numpoints++;
        }

        if (sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i])
            continue;

        // generate a split point
        p2 = in->w.points[(i + 1) % in->w.numpoints];

        dot = dists[i] / (dists[i] - dists[i + 1]);
        for (j = 0; j < 3; j++) {       // avoid round off error when possible
            if (split->normal[j] == 1)
                mid[j] = split->dist;
            else if (split->normal[j] == -1)
                mid[j] = -split->dist;
            else
                mid[j] = p1[j] + dot * (p2[j] - p1[j]);
        }

        VectorCopy(mid, newf->w.points[newf->w.numpoints]);
        newf->w.numpoints++;
        VectorCopy(mid, new2->w.points[new2->w.numpoints]);
        new2->w.numpoints++;
    }

    if (newf->w.numpoints > MAXEDGES || new2->w.numpoints > MAXEDGES)
        Error("Internal error: numpoints > MAXEDGES (%s)", __func__);

    /* free the original face now that it is represented by the fragments */
    FreeMem(in, FACE, 1);
}

/*
=================
RemoveOutsideFaces

Quick test before running ClipInside; move any faces that are completely
outside the brush to the outside list, without splitting them. This saves us
time in mergefaces later on (and sometimes a lot of memory)
=================
*/
static void
RemoveOutsideFaces(const brush_t *brush, face_t **inside, face_t **outside)
{
    face_t *face = *inside;
    face_t *next = nullptr;
    *inside = NULL;
    while (face) {
        next = face->next;
        winding_t *w = CopyWinding(&face->w);
        for (const face_t *clipface = brush->faces; clipface; clipface = clipface->next) {
            qbsp_plane_t clipplane = map.planes[clipface->planenum];
            if (!clipface->planeside) {
                VectorSubtract(vec3_origin, clipplane.normal, clipplane.normal);
                clipplane.dist = -clipplane.dist;
            }
            w = ClipWinding(w, &clipplane, true);
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
            FreeMem(w, WINDING, 1);
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
static void
ClipInside(const face_t *clipface, bool precedence,
           face_t **inside, face_t **outside)
{
    face_t *face, *next, *frags[2];
    const qbsp_plane_t *splitplane;

    splitplane = &map.planes[clipface->planenum];

    face = *inside;
    *inside = NULL;
    while (face) {
        next = face->next;

        /* HACK: Check for on-plane but not the same planenum
          ( https://github.com/ericwa/ericw-tools/issues/174 )
         */
        bool spurious_onplane = false;
        {
            vec_t dists[MAXEDGES + 1];
            int sides[MAXEDGES + 1];
            int counts[3];

            CalcSides(&face->w, splitplane, sides, dists, counts);
            if (counts[SIDE_ON] && !counts[SIDE_FRONT] && !counts[SIDE_BACK]) {
                spurious_onplane = true;
            }
        }
        
        /* Handle exactly on-plane faces */
        if (face->planenum == clipface->planenum || spurious_onplane) {
            const plane_t faceplane = Face_Plane(face);
            const plane_t clipfaceplane = Face_Plane(clipface);
            const vec_t dp = DotProduct(faceplane.normal, clipfaceplane.normal);
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
==================
*/
void
SaveFacesToPlaneList(face_t *facelist, bool mirror, std::map<int, face_t *> &planefaces)
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
        
        face_t *plane_current_facelist = planefaces[plane]; // returns `null` (and inserts it) if plane is not in the map yet

        if (mirror) {
            face_t *newface = NewFaceFromFace(face);
            newface->w.numpoints = face->w.numpoints;
            newface->planeside = face->planeside ^ 1;
            newface->contents[0] = face->contents[1];
            newface->contents[1] = face->contents[0];
            newface->cflags[0] = face->cflags[1];
            newface->cflags[1] = face->cflags[0];
            newface->lmshift[0] = face->lmshift[1];
            newface->lmshift[1] = face->lmshift[0];

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
            if (face->contents[1] == CONTENTS_DETAIL
                || face->contents[1] == CONTENTS_DETAIL_ILLUSIONARY
                || face->contents[1] == CONTENTS_DETAIL_FENCE
                || (face->cflags[1] & CFLAGS_WAS_ILLUSIONARY)
                || (options.fContentHack && face->contents[1] == CONTENTS_SOLID)) {
                
                // if CFLAGS_BMODEL_MIRROR_INSIDE is set, never change to skip
                if (!(face->cflags[1] & CFLAGS_BMODEL_MIRROR_INSIDE)) {
                	newface->texinfo = MakeSkipTexinfo();
                }
            }
            
            for (int i = 0; i < face->w.numpoints; i++)
                VectorCopy(face->w.points[face->w.numpoints - 1 - i], newface->w.points[i]);

            plane_current_facelist = MergeFaceToList(newface, plane_current_facelist);
        }
        plane_current_facelist = MergeFaceToList(face, plane_current_facelist);
        plane_current_facelist = FreeMergeListScraps(plane_current_facelist);

        // save the new list back in the map
        planefaces[plane] = plane_current_facelist;
        
        csgfaces++;
    }
}

static void
FreeFaces(face_t *face)
{
    face_t *next;

    while (face) {
        next = face->next;
        FreeMem(face, FACE, 1);
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
static void
SaveInsideFaces(face_t *face, const brush_t *clipbrush, face_t **savelist)
{
    Q_assert(clipbrush->contents != CONTENTS_SOLID);
    
    face_t *next;

    while (face) {
        // the back side of `face` is a solid
        //Q_assert(face->contents[1] == CONTENTS_SOLID);
        
        next = face->next;
        face->contents[0] = clipbrush->contents;
        face->cflags[0] = clipbrush->cflags;
        
        if ((face->contents[1] == CONTENTS_SOLID || face->contents[1] == CONTENTS_SKY)
             && clipbrush->contents == CONTENTS_DETAIL) {
            // This case is when a structural and detail brush are touching,
            // and we want to save the sturctural face that is
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
            
            face->contents[0] = CONTENTS_EMPTY;
            face->cflags[0] = CFLAGS_STRUCTURAL_COVERED_BY_DETAIL;
            face->texinfo = MakeSkipTexinfo();
        }
        
        // N.B.: We don't need a hack like above for when clipbrush->contents == CONTENTS_DETAIL_ILLUSIONARY.
        
        // These would create leaks
        Q_assert(!(face->contents[1] == CONTENTS_SOLID && face->contents[0] == CONTENTS_DETAIL));
        Q_assert(!(face->contents[1] == CONTENTS_SKY && face->contents[0] == CONTENTS_DETAIL));
        
        /*
         * If the inside brush is empty space, inherit the outside contents.
         * The only brushes with empty contents currently are hint brushes.
         */
        if (face->contents[1] == CONTENTS_EMPTY)
            face->contents[1] = clipbrush->contents;
        
        if (face->contents[1] == CONTENTS_DETAIL_ILLUSIONARY) {
            face->contents[1] = clipbrush->contents;
            face->cflags[1] |= CFLAGS_WAS_ILLUSIONARY;
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
==================
*/
surface_t *
BuildSurfaces(const std::map<int, face_t *> &planefaces)
{
    surface_t *surfaces = NULL;
    
    for (int i = 0; i < map.numplanes(); i++) {
        const auto entry = planefaces.find(i);
        if (entry == planefaces.end() || entry->second == nullptr) // FIXME: entry->second == nullptr should never happen, turn into Q_assert
            continue;
        
        /* create a new surface to hold the faces on this plane */
        surface_t *surf = (surface_t *)AllocMem(SURFACE, 1, true);
        surf->planenum = entry->first;
        surf->next = surfaces;
        surfaces = surf;
        surf->faces = entry->second;
        for (const face_t *face = surf->faces; face; face = face->next)
            csgmergefaces++;
        
        /* Calculate bounding box and flags */
        CalcSurfaceInfo(surf);
    }

    return surfaces;
}

//==========================================================================

/*
==================
CopyBrushFaces
==================
*/
static face_t *
CopyBrushFaces(const brush_t *brush)
{
    face_t *facelist, *face, *newface;

    facelist = NULL;
    for (face = brush->faces; face; face = face->next) {
        brushfaces++;
        newface = (face_t *)AllocMem(FACE, 1, true);
        *newface = *face;
        newface->contents[0] = CONTENTS_EMPTY;
        newface->contents[1] = brush->contents;
        newface->cflags[0] = 0;
        newface->cflags[1] = brush->cflags;
        newface->lmshift[0] = brush->lmshift;
        newface->lmshift[1] = brush->lmshift;
        newface->next = facelist;
        facelist = newface;
    }

    return facelist;
}

static bool IsLiquid(int contents)
{
    return contents == CONTENTS_WATER
        || contents == CONTENTS_LAVA
        || contents == CONTENTS_SLIME;
}

static bool IsFence(int contents) {
    return contents == CONTENTS_DETAIL_FENCE
        || contents == CONTENTS_DETAIL_ILLUSIONARY;
}

/*
==================
CSGFaces

Returns a list of surfaces containing all of the faces
==================
*/
surface_t *
CSGFaces(const mapentity_t *entity)
{
    Message(msgProgress, "CSGFaces");

    csgfaces = brushfaces = csgmergefaces = 0;

#if 0
    logprint("CSGFaces brush order:\n");
    for (brush = entity->brushes; brush; brush = brush->next) {
        logprint("    %s (%s)\n", map.texinfoTextureName(brush->faces->texinfo).c_str(), GetContentsName(brush->contents));
    }
#endif

    // copy to vector
    // TODO: change brush list in mapentity_t to a vector so we can skip this
    std::vector<const brush_t*> brushvec;
    for (const brush_t* brush = entity->brushes; brush; brush = brush->next) {
        brushvec.push_back(brush);
    }

    // output vector for the parallel_for
    std::vector<face_t*> brushvec_outsides;
    brushvec_outsides.resize(brushvec.size());

    /*
     * For each brush, clip away the parts that are inside other brushes.
     * Solid brushes override non-solid brushes.
     *   brush     => the brush to be clipped
     *   clipbrush => the brush we are clipping against
     *
     * The output of this is a face list for each brush called "outside"
     */
    tbb::parallel_for(static_cast<size_t>(0), brushvec.size(),
                      [entity, &brushvec, &brushvec_outsides](const size_t i) {
        const brush_t* brush = brushvec[i];
        face_t *outside = CopyBrushFaces(brush);
        bool overwrite = false;
        const brush_t *clipbrush = entity->brushes;

        for (; clipbrush; clipbrush = clipbrush->next) {
            if (brush == clipbrush) {
                /* Brushes further down the list overried earlier ones */
                overwrite = true;
                continue;
            }
            if (clipbrush->contents == CONTENTS_EMPTY) {
                /* Ensure hint never clips anything */
                continue;
            }
            
            if (clipbrush->contents == CONTENTS_DETAIL_ILLUSIONARY
                && brush->contents != CONTENTS_DETAIL_ILLUSIONARY) {
                /* CONTENTS_DETAIL_ILLUSIONARY never clips anything but itself */
                continue;
            }
            
            if (clipbrush->contents == CONTENTS_DETAIL && (clipbrush->cflags & CFLAGS_DETAIL_WALL)
                && !(brush->contents == CONTENTS_DETAIL && (brush->cflags & CFLAGS_DETAIL_WALL))) {
                /* if clipbrush has CONTENTS_DETAIL and CFLAGS_DETAIL_WALL are set,
                   only clip other brushes with both CONTENTS_DETAIL and CFLAGS_DETAIL_WALL.
                 */
                continue;
            }
            
            if (clipbrush->contents == CONTENTS_DETAIL_FENCE
                && brush->contents != CONTENTS_DETAIL_FENCE) {
                /* CONTENTS_DETAIL_FENCE never clips anything but itself */
                continue;
            }
            
            if (clipbrush->contents == brush->contents
                && (clipbrush->cflags & CFLAGS_NO_CLIPPING_SAME_TYPE)) {
                /* _noclipfaces key */
                continue;
            }

            /* check bounding box first */
            int i;
            for (i = 0; i < 3; i++) {
                if (brush->mins[i] > clipbrush->maxs[i])
                    break;
                if (brush->maxs[i] < clipbrush->mins[i])
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
            const face_t *clipface = clipbrush->faces;
            for (; clipface; clipface = clipface->next)
                ClipInside(clipface, overwrite, &inside, &outside);
            
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
            if ((brush->contents == CONTENTS_SOLID && clipbrush->contents != CONTENTS_SOLID)
                || (brush->contents == CONTENTS_SKY && (clipbrush->contents != CONTENTS_SOLID
                                                        && clipbrush->contents != CONTENTS_SKY))
                || (brush->contents == CONTENTS_DETAIL && (clipbrush->contents != CONTENTS_SOLID
                                                           && clipbrush->contents != CONTENTS_SKY
                                                           && clipbrush->contents != CONTENTS_DETAIL))
                || (IsLiquid(brush->contents)          && clipbrush->contents == CONTENTS_DETAIL_ILLUSIONARY)
                || (IsFence(brush->contents)           && (IsLiquid(clipbrush->contents) || IsFence(clipbrush->contents))))
            {
                SaveInsideFaces(inside, clipbrush, &outside);
            } else {
                FreeFaces(inside);
            }
        }

        // save the result
        brushvec_outsides[i] = outside;
    });

    // Non parallel part:
    std::map<int, face_t *> planefaces;
    for (size_t i = 0; i < brushvec.size(); ++i) {
        const brush_t* brush = brushvec[i];
        face_t* outside = brushvec_outsides[i];

        /*
         * All of the faces left on the outside list are real surface faces
         * If the brush is non-solid, mirror faces for the inside view
         */
        const bool mirror = options.fContentHack ? true : (brush->contents != CONTENTS_SOLID);
        SaveFacesToPlaneList(outside, mirror, planefaces);
    }
    surface_t *surfaces = BuildSurfaces(planefaces);

    Message(msgStat, "%8d brushfaces", brushfaces);
    Message(msgStat, "%8d csgfaces", csgfaces);
    Message(msgStat, "%8d mergedfaces", csgmergefaces);

    return surfaces;
}
