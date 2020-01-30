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

#include <limits.h>

#include <qbsp/qbsp.hh>

#include "tbb/task_group.h"

int splitnodes;

static int leaffaces;
static int nodefaces;
static int c_solid, c_empty, c_water, c_detail, c_detail_illusionary, c_detail_fence;
static int c_illusionary_visblocker;
static bool usemidsplit;

/**
 * Total number of surfaces in the map
 */
static int mapsurfaces;

//============================================================================

void
ConvertNodeToLeaf(node_t *node, int contents)
{
    // backup the mins/maxs
    vec3_t mins, maxs;
    VectorCopy(node->mins, mins);
    VectorCopy(node->maxs, maxs);

    // zero it
    memset(node, 0, sizeof(*node));
    
    // restore relevant fields
    VectorCopy(mins, node->mins);
    VectorCopy(maxs, node->maxs);
    
    node->planenum = PLANENUM_LEAF;
    node->contents = contents;
    node->markfaces = (face_t **)AllocMem(OTHER, sizeof(face_t *), true);

    Q_assert(node->markfaces[0] == nullptr);
}

void
DetailToSolid(node_t *node)
{
    if (node->planenum == PLANENUM_LEAF) {
        // We need to remap CONTENTS_DETAIL to a standard quake content type
        if (node->contents == CONTENTS_DETAIL) {
            node->contents = CONTENTS_SOLID;
        } else if (node->contents == CONTENTS_DETAIL_ILLUSIONARY) {
            node->contents = CONTENTS_EMPTY;
        }
        /* N.B.: CONTENTS_DETAIL_FENCE is not remapped to CONTENTS_SOLID until the very last moment,
         * because we want to generate a leaf (if we set it to CONTENTS_SOLID now it would use leaf 0).
         */
        return;
    } else {
        DetailToSolid(node->children[0]);
        DetailToSolid(node->children[1]);

		// If both children are solid, we can merge the two leafs into one.
        // DarkPlaces has an assertion that fails if both children are
        // solid.
		if (node->children[0]->contents == CONTENTS_SOLID
			&& node->children[1]->contents == CONTENTS_SOLID) {
            // This discards any faces on-node. Should be safe (?)
            ConvertNodeToLeaf(node, CONTENTS_SOLID);
		}
    }
}

/*
==================
FaceSide

For BSP hueristic
==================
*/
static int
FaceSide__(const face_t *in, const qbsp_plane_t *split)
{
    bool have_front, have_back;
    int i;

    have_front = have_back = false;

    if (split->type < 3) {
        /* shortcut for axial planes */
        const vec_t *p = in->w.points[0] + split->type;
        for (i = 0; i < in->w.numpoints; i++, p += 3) {
            if (*p > split->dist + ON_EPSILON) {
                if (have_back)
                    return SIDE_ON;
                have_front = true;
            } else if (*p < split->dist - ON_EPSILON) {
                if (have_front)
                    return SIDE_ON;
                have_back = true;
            }
        }
    } else {
        /* sloping planes take longer */
        const vec_t *p = in->w.points[0];
        for (i = 0; i < in->w.numpoints; i++, p += 3) {
            const vec_t dot = DotProduct(p, split->normal) - split->dist;
            if (dot > ON_EPSILON) {
                if (have_back)
                    return SIDE_ON;
                have_front = true;
            } else if (dot < -ON_EPSILON) {
                if (have_front)
                    return SIDE_ON;
                have_back = true;
            }
        }
    }

    if (!have_front)
        return SIDE_BACK;
    if (!have_back)
        return SIDE_FRONT;

    return SIDE_ON;
}

static int
FaceSide(const face_t *in, const qbsp_plane_t *split)
{
    vec_t dist;
    int ret;

    dist = DotProduct(in->origin, split->normal) - split->dist;
    if (dist > in->radius)
        ret = SIDE_FRONT;
    else if (dist < -in->radius)
        ret = SIDE_BACK;
    else
        ret = FaceSide__(in, split);

    return ret;
}

/*
 * Split a bounding box by a plane; The front and back bounds returned
 * are such that they completely contain the portion of the input box
 * on that side of the plane. Therefore, if the split plane is
 * non-axial, then the returned bounds will overlap.
 */
static void
DivideBounds(const vec3_t mins, const vec3_t maxs, const qbsp_plane_t *split,
             vec3_t front_mins, vec3_t front_maxs,
             vec3_t back_mins, vec3_t back_maxs)
{
    int a, b, c, i, j;
    vec_t dist1, dist2, mid, split_mins, split_maxs;
    vec3_t corner;
    const vec_t *bounds[2];

    VectorCopy(mins, front_mins);
    VectorCopy(mins, back_mins);
    VectorCopy(maxs, front_maxs);
    VectorCopy(maxs, back_maxs);

    if (split->type < 3) {
        front_mins[split->type] = back_maxs[split->type] = split->dist;
        return;
    }

    /* Make proper sloping cuts... */
    bounds[0] = mins;
    bounds[1] = maxs;
    for (a = 0; a < 3; ++a) {
        /* Check for parallel case... no intersection */
        if (fabs(split->normal[a]) < NORMAL_EPSILON)
            continue;

        b = (a + 1) % 3;
        c = (a + 2) % 3;

        split_mins = maxs[a];
        split_maxs = mins[a];
        for (i = 0; i < 2; ++i) {
            corner[b] = bounds[i][b];
            for (j = 0; j < 2; ++j) {
                corner[c] = bounds[j][c];

                corner[a] = bounds[0][a];
                dist1 = DotProduct(corner, split->normal) - split->dist;

                corner[a] = bounds[1][a];
                dist2 = DotProduct(corner, split->normal) - split->dist;

                mid = bounds[1][a] - bounds[0][a];
                mid *= (dist1 / (dist1 - dist2));
                mid += bounds[0][a];

                split_mins = qmax(qmin(mid, split_mins), mins[a]);
                split_maxs = qmin(qmax(mid, split_maxs), maxs[a]);
            }
        }
        if (split->normal[a] > 0) {
            front_mins[a] = split_mins;
            back_maxs[a] = split_maxs;
        } else {
            back_mins[a] = split_mins;
            front_maxs[a] = split_maxs;
        }
    }
}

/*
 * Calculate the split plane metric for axial planes
 */
static vec_t
SplitPlaneMetric_Axial(const qbsp_plane_t *p, const vec3_t mins, const vec3_t maxs)
{
    vec_t value = 0;
    for (int i = 0; i < 3; i++) {
        if (i == p->type) {
            const vec_t dist = p->dist * p->normal[i];
            value += (maxs[i] - dist) * (maxs[i] - dist);
            value += (dist - mins[i]) * (dist - mins[i]);
        } else {
            value += 2 * (maxs[i] - mins[i]) * (maxs[i] - mins[i]);
        }
    }

    return value;
}

/*
 * Calculate the split plane metric for non-axial planes
 */
static vec_t
SplitPlaneMetric_NonAxial(const qbsp_plane_t *p, const vec3_t mins, const vec3_t maxs)
{
    vec3_t fmins, fmaxs, bmins, bmaxs;
    vec_t value = 0.0;

    DivideBounds(mins, maxs, p, fmins, fmaxs, bmins, bmaxs);
    for (int i = 0; i < 3; i++) {
        value += (fmaxs[i] - fmins[i]) * (fmaxs[i] - fmins[i]);
        value += (bmaxs[i] - bmins[i]) * (bmaxs[i] - bmins[i]);
    }

    return value;
}

static inline vec_t
SplitPlaneMetric(const qbsp_plane_t *p, const vec3_t mins, const vec3_t maxs)
{
    vec_t value;

    if (p->type < 3)
        value = SplitPlaneMetric_Axial(p, mins, maxs);
    else
        value = SplitPlaneMetric_NonAxial(p, mins, maxs);

    return value;
}

/*
==================
ChooseMidPlaneFromList

The clipping hull BSP doesn't worry about avoiding splits
==================
*/
static surface_t *
ChooseMidPlaneFromList(surface_t *surfaces, const vec3_t mins, const vec3_t maxs)
{
    /* pick the plane that splits the least */
    vec_t bestmetric = VECT_MAX;
    surface_t *bestsurface = nullptr;

    for (int pass = 0; pass < 2; pass++) {
        for (surface_t *surf = surfaces; surf; surf = surf->next) {
            if (surf->onnode)
                continue;
            
            if( surf->has_struct && pass )
                continue;
            if( !surf->has_struct && !pass )
                continue;

            /* check for axis aligned surfaces */
            const qbsp_plane_t *plane = &map.planes[surf->planenum];
            if (!(plane->type < 3))
                continue;

            /* calculate the split metric, smaller values are better */
            const vec_t metric = SplitPlaneMetric(plane, mins, maxs);
            if (metric < bestmetric) {
                bestmetric = metric;
                bestsurface = surf;
            }
        }

        if (!bestsurface) {
            /* Choose based on spatial subdivision only */
            for (surface_t *surf = surfaces; surf; surf = surf->next) {
                if (surf->onnode)
                    continue;

                if( surf->has_struct && pass )
                    continue;
                if( !surf->has_struct && !pass )
                    continue;
                
                const qbsp_plane_t *plane = &map.planes[surf->planenum];
                const vec_t metric = SplitPlaneMetric(plane, mins, maxs);
                if (metric < bestmetric) {
                    bestmetric = metric;
                    bestsurface = surf;
                }
            }
        }
        
        if (bestsurface)
            break;
    }
    if (!bestsurface)
        Error("No valid planes in surface list (%s)", __func__);

    // ericw -- (!usemidsplit) is true on the final SolidBSP phase for the world.
    // !bestsurface->has_struct means all surfaces in this node are detail, so
    // mark the surface as a detail separator.
    //
    // TODO: investigate dropping the maxNodeSize feature (dynamically choosing
    // between ChooseMidPlaneFromList and ChoosePlaneFromList) and use Q2's
    // chopping on a uniform grid?
    if (!usemidsplit && !bestsurface->has_struct) {
        bestsurface->detail_separator = true;
    }
    
    return bestsurface;
}



/*
==================
ChoosePlaneFromList

The real BSP hueristic
==================
*/
static surface_t *
ChoosePlaneFromList(surface_t *surfaces, vec3_t mins, vec3_t maxs)
{
    /* pick the plane that splits the least */
    int minsplits = INT_MAX - 1;
    vec_t bestdistribution = VECT_MAX;
    surface_t *bestsurface = nullptr;

    /* Two passes - exhaust all non-detail faces before details */
    for (int pass = 0; pass < 2; pass++) {
        for (surface_t *surf = surfaces; surf; surf = surf->next) {
            if (surf->onnode)
                continue;

            /*
             * Check that the surface has a suitable face for the current pass
             * and check whether this is a hint split.
             */
            bool hintsplit = false;
            for (const face_t *face = surf->faces; face; face = face->next) {
                if (map.mtexinfos.at(face->texinfo).flags & TEX_HINT)
                    hintsplit = true;
            }

            if( surf->has_struct && pass )
                continue;
            if( !surf->has_struct && !pass )
                continue;
            

            const qbsp_plane_t *plane = &map.planes[surf->planenum];
            int splits = 0;

            for (surface_t *surf2 = surfaces; surf2; surf2 = surf2->next) {
                if (surf2 == surf || surf2->onnode)
                    continue;
                const qbsp_plane_t *plane2 = &map.planes[surf2->planenum];
                if (plane->type < 3 && plane->type == plane2->type)
                    continue;
                for (const face_t *face = surf2->faces; face; face = face->next) {
                    const uint64_t flags = map.mtexinfos.at(face->texinfo).flags;
                    /* Don't penalize for splitting skip faces */
                    if (flags & TEX_SKIP)
                        continue;
                    if (FaceSide(face, plane) == SIDE_ON) {
                        /* Never split a hint face except with a hint */
                        if (!hintsplit && (flags & TEX_HINT)) {
                            splits = INT_MAX;
                            break;
                        }
                        splits++;
                        if (splits >= minsplits)
                            break;
                    }
                }
                if (splits > minsplits)
                    break;
            }
            if (splits > minsplits)
                continue;

            /*
             * if equal numbers axial planes win, otherwise decide on spatial
             * subdivision
             */
            if (splits < minsplits || (splits == minsplits && plane->type < 3)) {
                if (plane->type < 3) {
                    const vec_t distribution = SplitPlaneMetric(plane, mins, maxs);
                    if (distribution > bestdistribution && splits == minsplits)
                        continue;
                    bestdistribution = distribution;
                }
                /* currently the best! */
                minsplits = splits;
                bestsurface = surf;
            }
        }

        /* If we found a candidate on first pass, don't do a second pass */
        if (bestsurface) {
            bestsurface->detail_separator = (pass > 0);
            break;
        }
    }

    return bestsurface;
}


/*
==================
SelectPartition

Selects a surface from a linked list of surfaces to split the group on
returns NULL if the surface list can not be divided any more (a leaf)
==================
*/
static surface_t *
SelectPartition(surface_t *surfaces)
{
    // count onnode surfaces
    int surfcount = 0;
    surface_t *bestsurface = nullptr;
    for (surface_t *surf = surfaces; surf; surf = surf->next)
        if (!surf->onnode) {
            surfcount++;
            bestsurface = surf;
        }

    if (surfcount == 0)
        return NULL;

    if (surfcount == 1)
        return bestsurface;     // this is a final split

    // calculate a bounding box of the entire surfaceset
    vec3_t mins, maxs;
    for (int i = 0; i < 3; i++) {
        mins[i] = VECT_MAX;
        maxs[i] = -VECT_MAX;
    }
    for (surface_t *surf = surfaces; surf; surf = surf->next)
        for (int i = 0; i < 3; i++) {
            if (surf->mins[i] < mins[i])
                mins[i] = surf->mins[i];
            if (surf->maxs[i] > maxs[i])
                maxs[i] = surf->maxs[i];
        }

    // how much of the map are we partitioning?
    double fractionOfMap = surfcount / (double)mapsurfaces;

    bool largenode = false;

    // decide if we should switch to the midsplit method
    if (options.midsplitSurfFraction != 0.0) {
        // new way (opt-in)
        largenode = (fractionOfMap > options.midsplitSurfFraction);
    } else {
        // old way (ericw-tools 0.15.2+)
        if (options.maxNodeSize >= 64) {
            const vec_t maxnodesize = options.maxNodeSize - ON_EPSILON;

            largenode = (maxs[0] - mins[0]) > maxnodesize
                        || (maxs[1] - mins[1]) > maxnodesize
                        || (maxs[2] - mins[2]) > maxnodesize;
        }
    }

    if (usemidsplit || largenode) // do fast way for clipping hull
        return ChooseMidPlaneFromList(surfaces, mins, maxs);

    // do slow way to save poly splits for drawing hull
    return ChoosePlaneFromList(surfaces, mins, maxs);
}

//============================================================================

/*
=================
CalcSurfaceInfo

Calculates the bounding box
=================
*/
void
CalcSurfaceInfo(surface_t *surf)
{
    // calculate a bounding box
    for (int i = 0; i < 3; i++) {
        surf->mins[i] = VECT_MAX;
        surf->maxs[i] = -VECT_MAX;
    }

    surf->has_detail = false;
    surf->has_struct = false;
    
    for (const face_t *f = surf->faces; f; f = f->next) {
        if (f->contents[0] >= 0 || f->contents[1] >= 0)
            Error("Bad contents in face (%s)", __func__);

        surf->lmshift = (f->lmshift[0]<f->lmshift[1])?f->lmshift[0]:f->lmshift[1];
        
        bool faceIsDetail = false;
        if ((f->contents[0] == CONTENTS_DETAIL)
            || (f->contents[1] == CONTENTS_DETAIL))
            faceIsDetail = true;
        
        if ((f->contents[0] == CONTENTS_DETAIL_ILLUSIONARY)
            || (f->contents[1] == CONTENTS_DETAIL_ILLUSIONARY))
            faceIsDetail = true;
        
        if ((f->contents[0] == CONTENTS_DETAIL_FENCE)
            || (f->contents[1] == CONTENTS_DETAIL_FENCE))
            faceIsDetail = true;
        
        if ((f->cflags[0] & CFLAGS_WAS_ILLUSIONARY)
            || (f->cflags[1] & CFLAGS_WAS_ILLUSIONARY))
            faceIsDetail = true;

        if (faceIsDetail)
            surf->has_detail = true;
        else
            surf->has_struct = true;

        for (int i = 0; i < f->w.numpoints; i++)
            for (int j = 0; j < 3; j++) {
                if (f->w.points[i][j] < surf->mins[j])
                    surf->mins[j] = f->w.points[i][j];
                if (f->w.points[i][j] > surf->maxs[j])
                    surf->maxs[j] = f->w.points[i][j];
            }
    }
}



/*
==================
DividePlane
==================
*/
static void
DividePlane(surface_t *in, const qbsp_plane_t *split, surface_t **front,
            surface_t **back)
{
    const qbsp_plane_t *inplane = &map.planes[in->planenum];
    *front = *back = NULL;

    // parallel case is easy
    if (VectorCompare(inplane->normal, split->normal, EQUAL_EPSILON)) {
        // check for exactly on node
        if (inplane->dist == split->dist) {
            face_t *facet = in->faces;
            in->faces = NULL;
            in->onnode = true;

            // divide the facets to the front and back sides
            surface_t *newsurf = (surface_t *)AllocMem(SURFACE, 1, true);
            *newsurf = *in;

            // Prepend each face in facet list to either in or newsurf lists
            face_t* next;
            for (; facet; facet = next) {
                next = facet->next;
                if (facet->planeside == 1) {
                    facet->next = newsurf->faces;
                    newsurf->faces = facet;
                } else {
                    facet->next = in->faces;
                    in->faces = facet;
                }
            }

            // ericw -- added these CalcSurfaceInfo to recalculate the surf bbox.
            // pretty sure their omission here was a bug.
            CalcSurfaceInfo(newsurf);
            CalcSurfaceInfo(in);

            if (in->faces)
                *front = in;
            else
                FreeMem(in, SURFACE, 1);

            if (newsurf->faces)
                *back = newsurf;
            else
                FreeMem(newsurf, SURFACE, 1);

            return;
        }

        if (inplane->dist > split->dist)
            *front = in;
        else
            *back = in;
        return;
    }
// do a real split.  may still end up entirely on one side
// OPTIMIZE: use bounding box for fast test
    face_t *frontlist = NULL;
    face_t *backlist = NULL;

    face_t *next;
    for (face_t *facet = in->faces; facet; facet = next) {
        next = facet->next;

        face_t *frontfrag = NULL;
        face_t *backfrag = NULL;
        SplitFace(facet, split, &frontfrag, &backfrag);
        if (frontfrag) {
            frontfrag->next = frontlist;
            frontlist = frontfrag;
        }
        if (backfrag) {
            backfrag->next = backlist;
            backlist = backfrag;
        }
    }

    // if nothing actually got split, just move the in plane
    if (frontlist == NULL) {
        *back = in;
        in->faces = backlist;
        return;
    }

    if (backlist == NULL) {
        *front = in;
        in->faces = frontlist;
        return;
    }

    // stuff got split, so allocate one new plane and reuse in
    surface_t *newsurf = (surface_t *)AllocMem(SURFACE, 1, true);
    *newsurf = *in;
    newsurf->faces = backlist;
    *back = newsurf;

    in->faces = frontlist;
    *front = in;

    // recalc bboxes and flags
    CalcSurfaceInfo(newsurf);
    CalcSurfaceInfo(in);
}

/*
==================
DivideNodeBounds
==================
*/
static void
DivideNodeBounds(node_t *node, const qbsp_plane_t *split)
{
    DivideBounds(node->mins, node->maxs, split,
                 node->children[0]->mins, node->children[0]->maxs,
                 node->children[1]->mins, node->children[1]->maxs);
}

/*
==================
GetContentsName
==================
*/
const char *
GetContentsName( int Contents ) {
    switch( Contents ) {
        case CONTENTS_EMPTY:
            return "Empty";
            
        case CONTENTS_SOLID:
            return "Solid";
            
        case CONTENTS_WATER:
            return "Water";
            
        case CONTENTS_SLIME:
            return "Slime";
            
        case CONTENTS_LAVA:
            return "Lava";
            
        case CONTENTS_SKY:
            return "Sky";
        
        case CONTENTS_DETAIL:
            return "Detail";
            
        case CONTENTS_DETAIL_ILLUSIONARY:
            return "DetailIllusionary";

        case CONTENTS_DETAIL_FENCE:
            return "DetailFence";

        case CONTENTS_ILLUSIONARY_VISBLOCKER:
            return "IllusionaryVisblocker";

        default:
            return "Error";
    }
}

int Contents_Priority(int contents)
{
    switch (contents) {
        case CONTENTS_SOLID:  return 7;
            
        case CONTENTS_SKY:    return 6;
            
        case CONTENTS_DETAIL: return 5;
    
        case CONTENTS_DETAIL_FENCE: return 4;
            
        case CONTENTS_DETAIL_ILLUSIONARY: return 3;
            
        case CONTENTS_WATER:  return 2;
        case CONTENTS_SLIME:  return 2;
        case CONTENTS_LAVA:   return 2;
        case CONTENTS_ILLUSIONARY_VISBLOCKER: return 2;

        case CONTENTS_EMPTY:  return 1;
        case 0:               return 0;
        
        default:
            Error("Bad contents in face (%s)", __func__);
            return 0;
    }
}

/*
==================
LinkConvexFaces

Determines the contents of the leaf and creates the final list of
original faces that have some fragment inside this leaf
==================
*/
static void
LinkConvexFaces(surface_t *planelist, node_t *leafnode)
{
    leafnode->faces = NULL;
    leafnode->contents = 0;
    leafnode->planenum = PLANENUM_LEAF;

    int count = 0;
    for (surface_t *surf = planelist; surf; surf = surf->next) {
        for (face_t *f = surf->faces; f; f = f->next) {
            count++;
            
            const int currentpri = Contents_Priority(leafnode->contents);
            const int fpri = Contents_Priority(f->contents[0]);
            if (fpri > currentpri) {
                leafnode->contents = f->contents[0];
            }
            
            // HACK: Handle structural covered by detail.
            if (f->cflags[0] & CFLAGS_STRUCTURAL_COVERED_BY_DETAIL) {
                Q_assert(f->contents[0] == CONTENTS_EMPTY);

                if (Contents_Priority(CONTENTS_DETAIL) > currentpri) {
                    leafnode->contents = CONTENTS_DETAIL;
                }
            }
        }
    }

    // NOTE: This is crazy..
    // Liquid leafs get assigned liquid content types because of the
    // "cosmetic" mirrored faces.
    if (!leafnode->contents)
        leafnode->contents = CONTENTS_SOLID; // FIXME: Need to create CONTENTS_DETAIL sometimes?
    
    switch (leafnode->contents) {
    case CONTENTS_EMPTY:
        c_empty++;
        break;
    case CONTENTS_SOLID:
        c_solid++;
        break;
    case CONTENTS_WATER:
    case CONTENTS_SLIME:
    case CONTENTS_LAVA:
    case CONTENTS_SKY:
        c_water++;
        break;
    case CONTENTS_DETAIL:
        c_detail++;
        break;
    case CONTENTS_DETAIL_ILLUSIONARY:
        c_detail_illusionary++;
        break;
    case CONTENTS_DETAIL_FENCE:
        c_detail_fence++;
        break;
    case CONTENTS_ILLUSIONARY_VISBLOCKER:
        c_illusionary_visblocker++;
        break;
    default:
        Error("Bad contents in face (%s)", __func__);
    }

    // write the list of the original faces to the leaf's markfaces
    // free surf and the surf->faces list.
    leaffaces += count;
    leafnode->markfaces = (face_t **)AllocMem(OTHER, sizeof(face_t *) * (count + 1), true);

    int i = 0;
    surface_t *pnext;
    for (surface_t *surf = planelist; surf; surf = pnext) {
        pnext = surf->next;
        face_t* next;
        for (face_t *f = surf->faces; f; f = next) {
            next = f->next;
            leafnode->markfaces[i] = f->original;
            i++;
            FreeMem(f, FACE, 1);
        }
        FreeMem(surf, SURFACE, 1);
    }
    leafnode->markfaces[i] = NULL;      // sentinal
}


/*
==================
LinkNodeFaces

First subdivides surface->faces.
Then, duplicates the list of subdivided faces and returns it.

For each surface->faces, ->original is set to the respective duplicate that 
is returned here (why?)
==================
*/
static face_t *
LinkNodeFaces(surface_t *surface)
{
    face_t *list = NULL;

    // subdivide large faces
    face_t** prevptr = &surface->faces;
    face_t *f = *prevptr;
    while (f) {
        SubdivideFace(f, prevptr);
        prevptr = &(*prevptr)->next;
        f = *prevptr;
    }

    // copy
    for (face_t *f = surface->faces; f; f = f->next) {
        nodefaces++;
        face_t *newf = (face_t *)AllocMem(FACE, 1, true);
        *newf = *f;
        f->original = newf;
        newf->next = list;
        list = newf;
    }

    return list;
}


/*
==================
PartitionSurfaces
==================
*/
static void
PartitionSurfaces(surface_t *surfaces, node_t *node)
{
    surface_t *split = SelectPartition(surfaces);
    if (!split) {               // this is a leaf node
        node->planenum = PLANENUM_LEAF;
        
        // frees `surfaces` and the faces on it.
        // saves pointers to face->original in the leaf's markfaces list.
        LinkConvexFaces(surfaces, node);
        return;
    }

    splitnodes++;
    Message(msgPercent, splitnodes, csgmergefaces);

    node->faces = LinkNodeFaces(split);
    node->children[0] = (node_t *)AllocMem(NODE, 1, true);
    node->children[1] = (node_t *)AllocMem(NODE, 1, true);
    node->planenum = split->planenum;
    node->detail_separator = split->detail_separator;

    const qbsp_plane_t *splitplane = &map.planes[split->planenum];

    DivideNodeBounds(node, splitplane);

    // multiple surfaces, so split all the polysurfaces into front and back lists
    surface_t *frontlist = NULL;
    surface_t *backlist = NULL;

    surface_t *next;
    for (surface_t *surf = surfaces; surf; surf = next) {
        next = surf->next;

        surface_t *frontfrag, *backfrag;
        DividePlane(surf, splitplane, &frontfrag, &backfrag);
        if (frontfrag && backfrag) {
            // the plane was split, which may expose oportunities to merge
            // adjacent faces into a single face
//                      MergePlaneFaces (frontfrag);
//                      MergePlaneFaces (backfrag);
        }

        if (frontfrag) {
            if (!frontfrag->faces)
                Error("Surface with no faces (%s)", __func__);
            frontfrag->next = frontlist;
            frontlist = frontfrag;
        }
        if (backfrag) {
            if (!backfrag->faces)
                Error("Surface with no faces (%s)", __func__);
            backfrag->next = backlist;
            backlist = backfrag;
        }
    }

    tbb::task_group g;
    g.run([&](){ PartitionSurfaces(frontlist, node->children[0]); });
    g.run([&](){ PartitionSurfaces(backlist, node->children[1]); });
    g.wait();
}


/*
==================
SolidBSP
==================
*/
node_t *
SolidBSP(const mapentity_t *entity, surface_t *surfhead, bool midsplit)
{
    if (!surfhead) {
        /*
         * We allow an entity to be constructed with no visible brushes
         * (i.e. all clip brushes), but need to construct a simple empty
         * collision hull for the engine. Probably could be done a little
         * smarter, but this works.
         */
        node_t *headnode = (node_t *)AllocMem(NODE, 1, true);
        for (int i = 0; i < 3; i++) {
            headnode->mins[i] = entity->mins[i] - SIDESPACE;
            headnode->maxs[i] = entity->maxs[i] + SIDESPACE;
        }
        headnode->children[0] = (node_t *)AllocMem(NODE, 1, true);
        headnode->children[0]->planenum = PLANENUM_LEAF;
        headnode->children[0]->contents = CONTENTS_EMPTY;
        headnode->children[0]->markfaces = (face_t **)AllocMem(OTHER, sizeof(face_t *), true);
        headnode->children[1] = (node_t *)AllocMem(NODE, 1, true);
        headnode->children[1]->planenum = PLANENUM_LEAF;
        headnode->children[1]->contents = CONTENTS_EMPTY;
        headnode->children[1]->markfaces = (face_t **)AllocMem(OTHER, sizeof(face_t *), true);

        return headnode;
    }

    Message(msgProgress, "SolidBSP");

    node_t *headnode = (node_t *)AllocMem(NODE, 1, true);
    usemidsplit = midsplit;

    // calculate a bounding box for the entire model
    for (int i = 0; i < 3; i++) {
        headnode->mins[i] = entity->mins[i] - SIDESPACE;
        headnode->maxs[i] = entity->maxs[i] + SIDESPACE;
    }

    // recursively partition everything
    splitnodes = 0;
    leaffaces = 0;
    nodefaces = 0;
    c_solid = 0;
    c_empty = 0;
    c_water = 0;
    c_detail = 0;
    c_detail_illusionary = 0;
    c_detail_fence = 0;
    c_illusionary_visblocker = 0;
    // count map surfaces; this is used when deciding to switch between midsplit and the expensive partitioning
    mapsurfaces = 0;
    for (surface_t *surf = surfhead; surf; surf = surf->next) {
        mapsurfaces++;
    }

    PartitionSurfaces(surfhead, headnode);

    Message(msgStat, "%8d split nodes", splitnodes);
    Message(msgStat, "%8d solid leafs", c_solid);
    Message(msgStat, "%8d empty leafs", c_empty);
    Message(msgStat, "%8d water leafs", c_water);
    Message(msgStat, "%8d detail leafs", c_detail);
    Message(msgStat, "%8d detail illusionary leafs", c_detail_illusionary);
    Message(msgStat, "%8d detail fence leafs", c_detail_fence);
    Message(msgStat, "%8d illusionary visblocker leafs", c_illusionary_visblocker);
    Message(msgStat, "%8d leaffaces", leaffaces);
    Message(msgStat, "%8d nodefaces", nodefaces);

    return headnode;
}
