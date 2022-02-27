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

#include <list>
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

    Q_assert(node->markfaces.empty());
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
static int FaceSide__(const face_t *in, const qbsp_plane_t &split)
{
    bool have_front, have_back;
    int i;

    have_front = have_back = false;

    if (split.type < 3) {
        /* shortcut for axial planes */
        const vec_t *p = &in->w[0][split.type];
        for (i = 0; i < in->w.size(); i++, p += 3) {
            if (*p > split.dist + ON_EPSILON) {
                if (have_back)
                    return SIDE_ON;
                have_front = true;
            } else if (*p < split.dist - ON_EPSILON) {
                if (have_front)
                    return SIDE_ON;
                have_back = true;
            }
        }
    } else {
        /* sloping planes take longer */
        for (i = 0; i < in->w.size(); i++) {
            const vec_t dot = split.distance_to(in->w[i]);
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

inline int FaceSide(const face_t *in, const qbsp_plane_t &split)
{
    vec_t dist = split.distance_to(in->origin);

    if (dist > in->radius)
        return SIDE_FRONT;
    else if (dist < -in->radius)
        return SIDE_BACK;
    else
        return FaceSide__(in, split);
}

/*
 * Split a bounding box by a plane; The front and back bounds returned
 * are such that they completely contain the portion of the input box
 * on that side of the plane. Therefore, if the split plane is
 * non-axial, then the returned bounds will overlap.
 */
static void DivideBounds(const aabb3d &in_bounds, const qbsp_plane_t &split, aabb3d &front_bounds, aabb3d &back_bounds)
{
    int a, b, c, i, j;
    vec_t dist1, dist2, mid, split_mins, split_maxs;
    qvec3d corner;

    front_bounds = back_bounds = in_bounds;

    if (split.type < 3) {
        front_bounds[0][split.type] = back_bounds[1][split.type] = split.dist;
        return;
    }

    /* Make proper sloping cuts... */
    for (a = 0; a < 3; ++a) {
        /* Check for parallel case... no intersection */
        if (fabs(split.normal[a]) < NORMAL_EPSILON)
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
                dist1 = split.distance_to(corner);

                corner[a] = in_bounds[1][a];
                dist2 = split.distance_to(corner);

                mid = in_bounds[1][a] - in_bounds[0][a];
                mid *= (dist1 / (dist1 - dist2));
                mid += in_bounds[0][a];

                split_mins = max(min(mid, split_mins), in_bounds.mins()[a]);
                split_maxs = min(max(mid, split_maxs), in_bounds.maxs()[a]);
            }
        }
        if (split.normal[a] > 0) {
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
inline vec_t SplitPlaneMetric_Axial(const qbsp_plane_t &p, const aabb3d &bounds)
{
    vec_t value = 0;
    for (int i = 0; i < 3; i++) {
        if (i == p.type) {
            const vec_t dist = p.dist * p.normal[i];
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
inline vec_t SplitPlaneMetric_NonAxial(const qbsp_plane_t &p, const aabb3d &bounds)
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

inline vec_t SplitPlaneMetric(const qbsp_plane_t &p, const aabb3d &bounds)
{
    if (p.type < 3)
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
static std::vector<surface_t>::iterator ChooseMidPlaneFromList(std::vector<surface_t> &surfaces, const aabb3d &bounds)
{
    /* pick the plane that splits the least */
    vec_t bestaxialmetric = VECT_MAX;
    std::vector<surface_t>::iterator bestaxialsurface = surfaces.end();
    vec_t bestanymetric = VECT_MAX;
    std::vector<surface_t>::iterator bestanysurface = surfaces.end();

    for (int pass = 0; pass < 2; pass++) {
        for (auto surf = surfaces.begin(); surf != surfaces.end(); surf++) {
            if (surf->onnode)
                continue;

            if (surf->has_struct && pass)
                continue;
            if (!surf->has_struct && !pass)
                continue;

            const qbsp_plane_t &plane = map.planes[surf->planenum];
            bool axial = false;

            /* check for axis aligned surfaces */
            if (plane.type < 3) {
                axial = true;
            }

            /* calculate the split metric, smaller values are better */
            const vec_t metric = SplitPlaneMetric(plane, bounds);

            if (metric < bestanymetric) {
                bestanymetric = metric;
                bestanysurface = surf;
            }

            if (axial) {
                if (metric < bestaxialmetric) {
                    bestaxialmetric = metric;
                    bestaxialsurface = surf;
                }
            }
        }

        if (bestanysurface != surfaces.end() || bestaxialsurface != surfaces.end()) {
            break;
        }
    }

    // prefer the axial split
    auto bestsurface = (bestaxialsurface == surfaces.end()) ? bestanysurface : bestaxialsurface;

    if (bestsurface == surfaces.end())
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

The real BSP heuristic
==================
*/
static std::vector<surface_t>::iterator ChoosePlaneFromList(std::vector<surface_t> &surfaces, const aabb3d &bounds)
{
    /* pick the plane that splits the least */
    int minsplits = INT_MAX - 1;
    vec_t bestdistribution = VECT_MAX;
    std::vector<surface_t>::iterator bestsurface = surfaces.end();

    /* Two passes - exhaust all non-detail faces before details */
    for (int pass = 0; pass < 2; pass++) {
        for (auto surf = surfaces.begin(); surf != surfaces.end(); surf++) {
            if (surf->onnode)
                continue;

            /*
             * Check that the surface has a suitable face for the current pass
             * and check whether this is a hint split.
             */
            bool hintsplit = false;
            for (auto &face : surf->faces) {
                if (map.mtexinfos.at(face->texinfo).flags.is_hint)
                    hintsplit = true;
            }

            if (surf->has_struct && pass)
                continue;
            if (!surf->has_struct && !pass)
                continue;

            const qbsp_plane_t &plane = map.planes[surf->planenum];
            int splits = 0;

            for (auto surf2 = surfaces.begin(); surf2 != surfaces.end(); surf2++) {
                if (surf2 == surf || surf2->onnode)
                    continue;
                const qbsp_plane_t &plane2 = map.planes[surf2->planenum];
                if (plane.type < 3 && plane.type == plane2.type)
                    continue;
                for (auto &face : surf2->faces) {
                    const surfflags_t &flags = map.mtexinfos.at(face->texinfo).flags;
                    /* Don't penalize for splitting skip faces */
                    if (flags.is_skip)
                        continue;
                    if (FaceSide(face, plane) == SIDE_ON) {
                        /* Never split a hint face except with a hint */
                        if (!hintsplit && flags.is_hint) {
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
            if (splits < minsplits || (splits == minsplits && plane.type < 3)) {
                if (plane.type < 3) {
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
        if (bestsurface != surfaces.end()) {
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
static std::vector<surface_t>::iterator SelectPartition(std::vector<surface_t> &surfaces)
{
    // count onnode surfaces
    int surfcount = 0;
    std::vector<surface_t>::iterator bestsurface = surfaces.end();

    for (auto surf = surfaces.begin(); surf != surfaces.end(); surf++) {
        if (!surf->onnode) {
            surfcount++;
            bestsurface = surf;
        }
    }

    if (surfcount == 0)
        return surfaces.end();

    if (surfcount == 1)
        return bestsurface; // this is a final split

    // calculate a bounding box of the entire surfaceset
    aabb3d bounds;

    for (auto &surf : surfaces) {
        bounds += surf.bounds;
    }

    // how much of the map are we partitioning?
    double fractionOfMap = surfcount / (double)mapsurfaces;

    bool largenode = false;

    // decide if we should switch to the midsplit method
    if (options.midsplitsurffraction.value() != 0.0) {
        // new way (opt-in)
        largenode = (fractionOfMap > options.midsplitsurffraction.value());
    } else {
        // old way (ericw-tools 0.15.2+)
        if (options.maxnodesize.value() >= 64) {
            const vec_t maxnodesize = options.maxnodesize.value() - ON_EPSILON;

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
==================
DividePlane
==================
*/
static twosided<std::optional<surface_t>> DividePlane(surface_t &in, const qplane3d &split)
{
    const qbsp_plane_t *inplane = &map.planes[in.planenum];
    std::optional<surface_t> front, back;

    // parallel case is easy
    if (qv::epsilonEqual(inplane->normal, split.normal, EQUAL_EPSILON)) {
        // check for exactly on node
        if (inplane->dist == split.dist) {
            in.onnode = true;

            // divide the facets to the front and back sides
            surface_t newsurf = in.shallowCopy();

            // Prepend each face in facet list to either in or newsurf lists
            for (auto it = in.faces.begin(); it != in.faces.end();) {
                if ((*it)->planeside == 1) {
                    auto next = std::next(it);
                    newsurf.faces.splice(newsurf.faces.begin(), in.faces, it);
                    it = next;
                } else {
                    it++;
                }
            }

            if (!in.faces.empty()) {
                in.calculateInfo();
                front = std::move(in);
            }

            if (!newsurf.faces.empty()) {
                newsurf.calculateInfo();
                back = std::move(newsurf);
            }

            return {front, back};
        }

        if (inplane->dist > split.dist) {
            front = std::move(in);
        } else {
            back = std::move(in);
        }

        return {front, back};
    }

    // do a real split.  may still end up entirely on one side
    // OPTIMIZE: use bounding box for fast test
    std::list<face_t *> frontlist, backlist;

    for (auto face : in.faces) {
        auto [frontfrag, backfrag] = SplitFace(face, split);

        if (frontfrag) {
            frontlist.push_back(frontfrag);
        }
        if (backfrag) {
            backlist.push_back(backfrag);
        }
    }

    // if nothing actually got split, just move the in plane
    if (frontlist.empty()) {
        in.faces = std::move(backlist);
        return {std::nullopt, std::move(in)};
    }

    if (backlist.empty()) {
        in.faces = std::move(frontlist);
        return {std::move(in), std::nullopt};
    }

    // stuff got split, so allocate one new plane and reuse in
    surface_t newsurf = in.shallowCopy();

    newsurf.faces = std::move(backlist);
    newsurf.calculateInfo();

    in.faces = std::move(frontlist);
    in.calculateInfo();

    return {std::move(in), std::move(newsurf)};
}

/*
==================
DivideNodeBounds
==================
*/
inline void DivideNodeBounds(node_t *node, const qbsp_plane_t &split)
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
static void LinkConvexFaces(std::vector<surface_t> &planelist, node_t *leafnode)
{
    leafnode->facelist.clear();
    leafnode->planenum = PLANENUM_LEAF;

    int count = 0;
    std::optional<contentflags_t> contents;

    for (auto &surf : planelist) {
        count += surf.faces.size();

        for (auto &f : surf.faces) {
            const int currentpri = contents.has_value() ? contents->priority(options.target_game) : -1;
            const int fpri = f->contents[0].priority(options.target_game);

            if (fpri > currentpri) {
                contents = f->contents[0];
            }

            // HACK: Handle structural covered by detail.
            if (f->contents[0].extended & CFLAGS_STRUCTURAL_COVERED_BY_DETAIL) {
                Q_assert(f->contents[0].is_empty(options.target_game));

                contentflags_t solid_detail = options.target_game->create_extended_contents(CFLAGS_DETAIL);
                solid_detail.native = f->contents[0].covered_native;

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
    leafnode->markfaces.reserve(count);

    for (auto &surf : planelist) {
        for (auto &f : surf.faces) {
            leafnode->markfaces.push_back(f->original);
            delete f;
        }
    }
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
static std::list<face_t *> LinkNodeFaces(surface_t &surface)
{
    // subdivide large faces if requested
    if (options.subdivide.value()) {
        for (auto it = surface.faces.begin(); it != surface.faces.end(); it++) {
            it = SubdivideFace(it, surface.faces);
        }
    }

    surface.faces.reverse();

    nodefaces += surface.faces.size();

    std::list<face_t *> list;

    // copy
    for (auto &f : surface.faces) {
        face_t *newf = new face_t(*f);
        Q_assert(newf->original == nullptr);

        list.push_front(newf);
        f->original = newf;
    }

    return list;
}

/*
==================
PartitionSurfaces

Called in parallel.
==================
*/
static void PartitionSurfaces(std::vector<surface_t> &surfaces, node_t *node)
{
    std::vector<surface_t>::iterator split = SelectPartition(surfaces);

    if (split == surfaces.end()) { // this is a leaf node
        node->planenum = PLANENUM_LEAF;

        // frees `surfaces` and the faces on it.
        // saves pointers to face->original in the leaf's markfaces list.
        LinkConvexFaces(surfaces, node);
        return;
    }

    splitnodes++;
    LogPercent(splitnodes.load(), csgmergefaces);

    node->facelist = LinkNodeFaces(*split);
    node->children[0] = new node_t{};
    node->children[1] = new node_t{};
    node->planenum = split->planenum;
    node->detail_separator = split->detail_separator;

    const qbsp_plane_t &splitplane = map.planes[split->planenum];

    DivideNodeBounds(node, splitplane);

    // multiple surfaces, so split all the polysurfaces into front and back lists
    std::vector<surface_t> frontlist, backlist;

    for (auto &surf : surfaces) {
        auto frags = DividePlane(surf, splitplane);

        if (frags.front && frags.back) {
            // the plane was split, which may expose oportunities to merge
            // adjacent faces into a single face
            //                      MergePlaneFaces (frontfrag);
            //                      MergePlaneFaces (backfrag);
        }

        if (frags.front) {
            if (frags.front->faces.empty()) {
                FError("Surface with no faces");
            }
            frontlist.emplace_back(std::move(*frags.front));
        }
        if (frags.back) {
            if (frags.back->faces.empty()) {
                FError("Surface with no faces");
            }
            backlist.emplace_back(std::move(*frags.back));
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
node_t *SolidBSP(const mapentity_t *entity, std::vector<surface_t> &surfhead, bool midsplit)
{
    if (surfhead.empty()) {
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
        headnode->children[1] = new node_t{};
        headnode->children[1]->planenum = PLANENUM_LEAF;
        headnode->children[1]->contents = options.target_game->create_empty_contents();

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
    mapsurfaces = surfhead.size();

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
