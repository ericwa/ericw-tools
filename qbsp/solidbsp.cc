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

#include <climits>

#include <qbsp/qbsp.hh>

#include <atomic>

#include "tbb/task_group.h"

std::atomic<int> splitnodes;

static std::atomic<int> leaffaces;
static std::atomic<int> nodefaces;
static std::atomic<int> c_solid, c_empty, c_water, c_detail, c_detail_illusionary, c_detail_fence;
static std::atomic<int> c_illusionary_visblocker;
static bool usemidsplit;

/**
 * Total number of surfaces in the map
 */
static int mapsurfaces;

//============================================================================

void ConvertNodeToLeaf(node_t *node, const contentflags_t &contents)
{
    // backup the mins/maxs
    aabb3d bounds = node->bounds;

    // zero it
    memset(node, 0, sizeof(*node));

    // restore relevant fields
    node->bounds = bounds;

    node->planenum = PLANENUM_LEAF;
    node->contents = contents;
    node->markfaces = new face_t *[1] {};

    Q_assert(node->markfaces[0] == nullptr);
}

void DetailToSolid(node_t *node)
{
    if (node->planenum == PLANENUM_LEAF) {
        if (options.target_game->id == GAME_QUAKE_II) {
            return;
        }

        // We need to remap CONTENTS_DETAIL to a standard quake content type
        if (node->contents.is_detail(CFLAGS_DETAIL)) {
            node->contents = options.target_game->create_solid_contents();
        } else if (node->contents.is_detail(CFLAGS_DETAIL_ILLUSIONARY)) {
            node->contents = options.target_game->create_empty_contents();
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
        if (node->children[0]->contents.is_solid(options.target_game) &&
            node->children[1]->contents.is_solid(options.target_game)) {
            // This discards any faces on-node. Should be safe (?)
            ConvertNodeToLeaf(node, options.target_game->create_solid_contents());
        }
    }
}

/*
==================
FaceSide

For BSP hueristic
==================
*/
static int FaceSide__(const face_t *in, const qbsp_plane_t *split)
{
    bool have_front, have_back;
    int i;

    have_front = have_back = false;

    if (split->type < 3) {
        /* shortcut for axial planes */
        const vec_t *p = &in->w[0][split->type];
        for (i = 0; i < in->w.size(); i++, p += 3) {
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
        const vec_t *p = &in->w[0][0];
        for (i = 0; i < in->w.size(); i++, p += 3) {
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

static int FaceSide(const face_t *in, const qbsp_plane_t *split)
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
static void DivideBounds(const aabb3d &in_bounds, const qbsp_plane_t *split, aabb3d &front_bounds, aabb3d &back_bounds)
{
    int a, b, c, i, j;
    vec_t dist1, dist2, mid, split_mins, split_maxs;
    vec3_t corner;

    front_bounds = back_bounds = in_bounds;

    if (split->type < 3) {
        // CHECK: this escapes the immutability "sandbox" of aabb3d, is this a good idea?
        // it'd take like 6 lines to otherwise reproduce this line.
        front_bounds[0][split->type] = back_bounds[1][split->type] = split->dist;
        return;
    }

    /* Make proper sloping cuts... */
    for (a = 0; a < 3; ++a) {
        /* Check for parallel case... no intersection */
        if (fabs(split->normal[a]) < NORMAL_EPSILON)
            continue;

        b = (a + 1) % 3;
        c = (a + 2) % 3;

        split_mins = in_bounds.maxs()[a];
        split_maxs = in_bounds.mins()[a];
        for (i = 0; i < 2; ++i) {
            corner[b] = in_bounds[i][b];
            for (j = 0; j < 2; ++j) {
                corner[c] = in_bounds[j][c];

                corner[a] = in_bounds[0][a];
                dist1 = DotProduct(corner, split->normal) - split->dist;

                corner[a] = in_bounds[1][a];
                dist2 = DotProduct(corner, split->normal) - split->dist;

                mid = in_bounds[1][a] - in_bounds[0][a];
                mid *= (dist1 / (dist1 - dist2));
                mid += in_bounds[0][a];

                split_mins = qmax(qmin(mid, split_mins), in_bounds.mins()[a]);
                split_maxs = qmin(qmax(mid, split_maxs), in_bounds.maxs()[a]);
            }
        }
        if (split->normal[a] > 0) {
            front_bounds[0][a] = split_mins;
            back_bounds[1][a] = split_maxs;
        } else {
            back_bounds[0][a] = split_mins;
            front_bounds[1][a] = split_maxs;
        }
    }
}

/*
 * Calculate the split plane metric for axial planes
 */
static vec_t SplitPlaneMetric_Axial(const qbsp_plane_t *p, const aabb3d &bounds)
{
    vec_t value = 0;
    for (int i = 0; i < 3; i++) {
        if (i == p->type) {
            const vec_t dist = p->dist * p->normal[i];
            value += (bounds.maxs()[i] - dist) * (bounds.maxs()[i] - dist);
            value += (dist - bounds.mins()[i]) * (dist - bounds.mins()[i]);
        } else {
            value += 2 * (bounds.maxs()[i] - bounds.mins()[i]) * (bounds.maxs()[i] - bounds.mins()[i]);
        }
    }

    return value;
}

/*
 * Calculate the split plane metric for non-axial planes
 */
static vec_t SplitPlaneMetric_NonAxial(const qbsp_plane_t *p, const aabb3d &bounds)
{
    aabb3d f, b;
    vec_t value = 0.0;

    DivideBounds(bounds, p, f, b);
    for (int i = 0; i < 3; i++) {
        value += (f.maxs()[i] - f.mins()[i]) * (f.maxs()[i] - f.mins()[i]);
        value += (b.maxs()[i] - b.mins()[i]) * (b.maxs()[i] - b.mins()[i]);
    }

    return value;
}

inline vec_t SplitPlaneMetric(const qbsp_plane_t *p, const aabb3d &bounds)
{
    if (p->type < 3)
        return SplitPlaneMetric_Axial(p, bounds);
    else
        return SplitPlaneMetric_NonAxial(p, bounds);
}

/*
==================
ChooseMidPlaneFromList

The clipping hull BSP doesn't worry about avoiding splits
==================
*/
static surface_t *ChooseMidPlaneFromList(surface_t *surfaces, const aabb3d &bounds)
{
    /* pick the plane that splits the least */
    vec_t bestmetric = VECT_MAX;
    surface_t *bestsurface = nullptr;

    for (int pass = 0; pass < 2; pass++) {
        for (surface_t *surf = surfaces; surf; surf = surf->next) {
            if (surf->onnode)
                continue;

            if (surf->has_struct && pass)
                continue;
            if (!surf->has_struct && !pass)
                continue;

            /* check for axis aligned surfaces */
            const qbsp_plane_t *plane = &map.planes[surf->planenum];
            if (!(plane->type < 3))
                continue;

            /* calculate the split metric, smaller values are better */
            const vec_t metric = SplitPlaneMetric(plane, bounds);
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

                if (surf->has_struct && pass)
                    continue;
                if (!surf->has_struct && !pass)
                    continue;

                const qbsp_plane_t *plane = &map.planes[surf->planenum];
                const vec_t metric = SplitPlaneMetric(plane, bounds);
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
        FError("No valid planes in surface list");

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
static surface_t *ChoosePlaneFromList(surface_t *surfaces, const aabb3d &bounds)
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
                if (map.mtexinfos.at(face->texinfo).flags.extended & TEX_EXFLAG_HINT)
                    hintsplit = true;
            }

            if (surf->has_struct && pass)
                continue;
            if (!surf->has_struct && !pass)
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
                    const surfflags_t &flags = map.mtexinfos.at(face->texinfo).flags;
                    /* Don't penalize for splitting skip faces */
                    if (flags.extended & TEX_EXFLAG_SKIP)
                        continue;
                    if (FaceSide(face, plane) == SIDE_ON) {
                        /* Never split a hint face except with a hint */
                        if (!hintsplit && (flags.extended & TEX_EXFLAG_HINT)) {
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
                    const vec_t distribution = SplitPlaneMetric(plane, bounds);
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

Called in parallel.
==================
*/
static surface_t *SelectPartition(surface_t *surfaces)
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
        return bestsurface; // this is a final split

    // calculate a bounding box of the entire surfaceset
    aabb3d bounds;

    for (surface_t *surf = surfaces; surf; surf = surf->next) {
        bounds += surf->bounds;
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

            largenode = (bounds.maxs()[0] - bounds.mins()[0]) > maxnodesize ||
                        (bounds.maxs()[1] - bounds.mins()[1]) > maxnodesize ||
                        (bounds.maxs()[2] - bounds.mins()[2]) > maxnodesize;
        }
    }

    if (usemidsplit || largenode) // do fast way for clipping hull
        return ChooseMidPlaneFromList(surfaces, bounds);

    // do slow way to save poly splits for drawing hull
    return ChoosePlaneFromList(surfaces, bounds);
}

//============================================================================

/*
=================
CalcSurfaceInfo

Calculates the bounding box
=================
*/
void CalcSurfaceInfo(surface_t *surf)
{
    // calculate a bounding box
    surf->bounds = {};

    surf->has_detail = false;
    surf->has_struct = false;

    for (const face_t *f = surf->faces; f; f = f->next) {
        for (auto &contents : f->contents)
            if (!contents.is_valid(options.target_game, false))
                FError("Bad contents in face: {}", contents.to_string(options.target_game));

        surf->lmshift = (f->lmshift[0] < f->lmshift[1]) ? f->lmshift[0] : f->lmshift[1];

        bool faceIsDetail = false;

        if ((f->contents[0].extended | f->contents[1].extended) &
            (CFLAGS_DETAIL | CFLAGS_DETAIL_ILLUSIONARY | CFLAGS_DETAIL_FENCE | CFLAGS_WAS_ILLUSIONARY))
            faceIsDetail = true;

        if (faceIsDetail)
            surf->has_detail = true;
        else
            surf->has_struct = true;

        for (int i = 0; i < f->w.size(); i++) {
            surf->bounds += f->w[i];
        }
    }
}

/*
==================
DividePlane
==================
*/
static void DividePlane(surface_t *in, const qbsp_plane_t *split, surface_t **front, surface_t **back)
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
            surface_t *newsurf = new surface_t{};
            *newsurf = *in;

            // Prepend each face in facet list to either in or newsurf lists
            face_t *next;
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
                delete in;

            if (newsurf->faces)
                *back = newsurf;
            else
                delete newsurf;

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
    surface_t *newsurf = new surface_t(*in);
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
inline void DivideNodeBounds(node_t *node, const qbsp_plane_t *split)
{
    DivideBounds(node->bounds, split, node->children[0]->bounds, node->children[1]->bounds);
}

/*
==================
LinkConvexFaces

Determines the contents of the leaf and creates the final list of
original faces that have some fragment inside this leaf.

Called in parallel.
==================
*/
static void LinkConvexFaces(surface_t *planelist, node_t *leafnode)
{
    leafnode->faces = NULL;
    leafnode->planenum = PLANENUM_LEAF;

    int count = 0;
    std::optional<contentflags_t> contents;

    for (surface_t *surf = planelist; surf; surf = surf->next) {
        for (face_t *f = surf->faces; f; f = f->next) {
            count++;

            const int currentpri = contents.has_value() ? contents->priority(options.target_game) : -1;
            const int fpri = f->contents[0].priority(options.target_game);
            if (fpri > currentpri) {
                contents = f->contents[0];
            }

            // HACK: Handle structural covered by detail.
            if (f->contents[0].extended & CFLAGS_STRUCTURAL_COVERED_BY_DETAIL) {
                Q_assert(f->contents[0].is_empty(options.target_game));

                const contentflags_t solid_detail = options.target_game->create_extended_contents(CFLAGS_DETAIL);

                if (solid_detail.priority(options.target_game) > currentpri) {
                    contents = solid_detail;
                }
            }
        }
    }

    // NOTE: This is crazy..
    // Liquid leafs get assigned liquid content types because of the
    // "cosmetic" mirrored faces.
    leafnode->contents = contents.value_or(
        options.target_game->create_solid_contents()); // FIXME: Need to create CONTENTS_DETAIL sometimes?

    if (leafnode->contents.extended & CFLAGS_ILLUSIONARY_VISBLOCKER) {
        c_illusionary_visblocker++;
    } else if (leafnode->contents.extended & CFLAGS_DETAIL_FENCE) {
        c_detail_fence++;
    } else if (leafnode->contents.extended & CFLAGS_DETAIL_ILLUSIONARY) {
        c_detail_illusionary++;
    } else if (leafnode->contents.extended & CFLAGS_DETAIL) {
        c_detail++;
    } else if (leafnode->contents.is_empty(options.target_game)) {
        c_empty++;
    } else if (leafnode->contents.is_solid(options.target_game)) {
        c_solid++;
    } else if (leafnode->contents.is_liquid(options.target_game) || leafnode->contents.is_sky(options.target_game)) {
        c_water++;
    } else {
        // FIXME: what to call here? is_valid()? this hits in Q2 a lot
        // FError("Bad contents in face: {}", leafnode->contents.to_string(options.target_game));
    }

    // write the list of the original faces to the leaf's markfaces
    // free surf and the surf->faces list.
    leaffaces += count;
    leafnode->markfaces = new face_t *[count + 1] {};

    int i = 0;
    surface_t *pnext;
    for (surface_t *surf = planelist; surf; surf = pnext) {
        pnext = surf->next;
        face_t *next;
        for (face_t *f = surf->faces; f; f = next) {
            next = f->next;
            leafnode->markfaces[i] = f->original;
            i++;
            delete f;
        }
        delete surf;
    }
    leafnode->markfaces[i] = NULL; // sentinal
}

/*
==================
LinkNodeFaces

First subdivides surface->faces.
Then, duplicates the list of subdivided faces and returns it.

For each surface->faces, ->original is set to the respective duplicate that
is returned here (why?).

Called in parallel.
==================
*/
static face_t *LinkNodeFaces(surface_t *surface)
{
    face_t *list = NULL;

    // subdivide large faces
    face_t **prevptr = &surface->faces;
    face_t *f = *prevptr;
    while (f) {
        SubdivideFace(f, prevptr);
        prevptr = &(*prevptr)->next;
        f = *prevptr;
    }

    // copy
    for (face_t *f = surface->faces; f; f = f->next) {
        nodefaces++;
        face_t *newf = new face_t(*f);
        f->original = newf;
        newf->next = list;
        list = newf;
    }

    return list;
}

/*
==================
PartitionSurfaces

Called in parallel.
==================
*/
static void PartitionSurfaces(surface_t *surfaces, node_t *node)
{
    surface_t *split = SelectPartition(surfaces);
    if (!split) { // this is a leaf node
        node->planenum = PLANENUM_LEAF;

        // frees `surfaces` and the faces on it.
        // saves pointers to face->original in the leaf's markfaces list.
        LinkConvexFaces(surfaces, node);
        return;
    }

    splitnodes++;
    LogPercent(splitnodes.load(), csgmergefaces);

    node->faces = LinkNodeFaces(split);
    node->children[0] = new node_t{};
    node->children[1] = new node_t{};
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
                FError("Surface with no faces");
            frontfrag->next = frontlist;
            frontlist = frontfrag;
        }
        if (backfrag) {
            if (!backfrag->faces)
                FError("Surface with no faces");
            backfrag->next = backlist;
            backlist = backfrag;
        }
    }

    tbb::task_group g;
    g.run([&]() { PartitionSurfaces(frontlist, node->children[0]); });
    g.run([&]() { PartitionSurfaces(backlist, node->children[1]); });
    g.wait();
}

/*
==================
SolidBSP
==================
*/
node_t *SolidBSP(const mapentity_t *entity, surface_t *surfhead, bool midsplit)
{
    if (!surfhead) {
        /*
         * We allow an entity to be constructed with no visible brushes
         * (i.e. all clip brushes), but need to construct a simple empty
         * collision hull for the engine. Probably could be done a little
         * smarter, but this works.
         */
        node_t *headnode = new node_t{};
        headnode->bounds = entity->bounds.grow(SIDESPACE);
        headnode->children[0] = new node_t{};
        headnode->children[0]->planenum = PLANENUM_LEAF;
        headnode->children[0]->contents = options.target_game->create_empty_contents();
        headnode->children[0]->markfaces = new face_t *[1] {};
        headnode->children[1] = new node_t{};
        headnode->children[1]->planenum = PLANENUM_LEAF;
        headnode->children[1]->contents = options.target_game->create_empty_contents();
        headnode->children[1]->markfaces = new face_t *[1] {};

        return headnode;
    }

    LogPrint(LOG_PROGRESS, "---- {} ----\n", __func__);

    node_t *headnode = new node_t{};
    usemidsplit = midsplit;

    // calculate a bounding box for the entire model
    headnode->bounds = entity->bounds.grow(SIDESPACE);

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

    LogPrint(LOG_STAT, "     {:8} split nodes\n", splitnodes.load());
    LogPrint(LOG_STAT, "     {:8} solid leafs\n", c_solid.load());
    LogPrint(LOG_STAT, "     {:8} empty leafs\n", c_empty.load());
    LogPrint(LOG_STAT, "     {:8} water leafs\n", c_water.load());
    LogPrint(LOG_STAT, "     {:8} detail leafs\n", c_detail.load());
    LogPrint(LOG_STAT, "     {:8} detail illusionary leafs\n", c_detail_illusionary.load());
    LogPrint(LOG_STAT, "     {:8} detail fence leafs\n", c_detail_fence.load());
    LogPrint(LOG_STAT, "     {:8} illusionary visblocker leafs\n", c_illusionary_visblocker.load());
    LogPrint(LOG_STAT, "     {:8} leaffaces\n", leaffaces.load());
    LogPrint(LOG_STAT, "     {:8} nodefaces\n", nodefaces.load());

    return headnode;
}
