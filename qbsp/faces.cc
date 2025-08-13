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

#include <qbsp/brush.hh>

#include <common/log.hh>
#include <qbsp/portals.hh>
#include <qbsp/csg.hh>
#include <qbsp/map.hh>
#include <qbsp/merge.hh>
#include <qbsp/qbsp.hh>
#include <qbsp/tree.hh>
#include <qbsp/writebsp.hh>

#include <list>

struct makefaces_stats_t : logging::stat_tracker_t
{
    stat &c_nodefaces = register_stat("makefaces"); // FIXME: what is "makefaces" exactly
    stat &c_merge = register_stat("merged");
    stat &c_subdivide = register_stat("subdivided");
};

static bool ShouldOmitFace(face_t *f)
{
    if (!qbsp_options.includeskip.value() && f->get_texinfo().flags.is_nodraw()) {
        // TODO: move to game specific
        // always include LIGHT
        if (qbsp_options.target_game->id == GAME_QUAKE_II && (f->get_texinfo().flags.native_q2 & Q2_SURF_LIGHT))
            return false;

        return true;
    }
    if (map.mtexinfos.at(f->texinfo).flags.is_hint())
        return true;

    // HACK: to save a few faces, don't output the interior faces of sky brushes
    if (f->contents.front.is_sky()) {
        return true;
    }

    // omit faces fully covered by detail wall
    if (!f->markleafs.empty() && std::all_of(f->markleafs.begin(), f->markleafs.end(), [](node_t *l) {
            auto *leafdata = l->get_leafdata();
            return leafdata->contents.is_detail_wall();
        })) {
        return true;
    }

    return false;
}

static void MergeNodeFaces(node_t *node, makefaces_stats_t &stats)
{
    auto *nodedata = node->get_nodedata();
    nodedata->facelist = MergeFaceList(std::move(nodedata->facelist), stats.c_merge);
}

/*
=============
EmitVertex
=============
*/
inline void EmitVertex(const qvec3d &vert, size_t &vert_id)
{
    // already added
    if (auto v = map.find_emitted_hash_vector(vert)) {
        vert_id = *v;
        return;
    }

    // add new vertex!
    map.add_hash_vector(vert, vert_id = map.bsp.dvertexes.size());

    map.bsp.dvertexes.emplace_back(vert);
}

// output final vertices
static void EmitFaceVertices(face_t *f)
{
    if (ShouldOmitFace(f)) {
        return;
    }

    f->original_vertices.resize(f->w.size());

    for (size_t i = 0; i < f->w.size(); i++) {
        EmitVertex(f->w[i], f->original_vertices[i]);
    }
}

static void EmitVertices_R(node_t *node)
{
    if (node->is_leaf()) {
        return;
    }

    auto *nodedata = node->get_nodedata();
    for (auto &f : nodedata->facelist) {
        EmitFaceVertices(f.get());
    }

    EmitVertices_R(nodedata->children[0]);
    EmitVertices_R(nodedata->children[1]);
}

void EmitVertices(node_t *headnode)
{
    EmitVertices_R(headnode);
}

//===========================================================================

struct emit_faces_stats_t : logging::stat_tracker_t
{
    stat &unique_edges = register_stat("edges");
    stat &unique_faces = register_stat("faces");
};

/*
==================
GetEdge

Returns a global edge number, possibly negative to indicate a backwards edge.
==================
*/
inline int64_t GetEdge(size_t v1, size_t v2, const face_t *face, emit_faces_stats_t &stats)
{
    if (!qbsp_options.noedgereuse.value()) {
        // search for existing edges
        if (auto it = map.hashedges.find(std::make_pair(v2, v1)); it != map.hashedges.end()) {
            hashedge_t &existing = it->second;
            // this content check is required for software renderers
            // (see q1_liquid_software test case)
            if (existing.face->contents.front.equals(qbsp_options.target_game, face->contents.front)) {
                // only reusing an edge once is a separate limitation of software renderers
                // (see q1_edge_sharing_software.map test case)
                if (!existing.has_been_reused) {
                    existing.has_been_reused = true;
                    return -existing.edge_index;
                }
            }
        }
    }

    /* emit an edge */
    int64_t i = map.bsp.dedges.size();

    map.bsp.dedges.push_back(bsp2_dedge_t{static_cast<uint32_t>(v1), static_cast<uint32_t>(v2)});

    map.add_hash_edge(v1, v2, i, face);

    stats.unique_edges++;

    return i;
}

static void EmitEdges(face_t *face, face_fragment_t *fragment, emit_faces_stats_t &stats)
{
    Q_assert(fragment->outputnumber == std::nullopt);

    if (qbsp_options.maxedges.value() && fragment->output_vertices.size() > qbsp_options.maxedges.value()) {
        FError("Internal error: face->numpoints > max edges ({})", qbsp_options.maxedges.value());
    }

    fragment->edges.resize(fragment->output_vertices.size());

    for (size_t i = 0; i < fragment->output_vertices.size(); i++) {
        auto &p1 = fragment->output_vertices[i];
        auto &p2 = fragment->output_vertices[(i + 1) % fragment->output_vertices.size()];
        fragment->edges[i] = GetEdge(p1, p2, face, stats);
    }
}

/*
==============
EmitFaceFragment
==============
*/
static void EmitFaceFragment(face_t *face, face_fragment_t *fragment, emit_faces_stats_t &stats)
{
    // this can't really happen, but just in case it ever does..
    // (I use this in testing to find faces of interest)
    if (fragment->output_vertices.size() < 3) {
        logging::print("WARNING: {}-point face attempted to be emitted\n", fragment->output_vertices.size());
        return;
    }

    int i;

    // emit a region
    Q_assert(!fragment->outputnumber.has_value());
    fragment->outputnumber = map.bsp.dfaces.size();

    mface_t &out = map.bsp.dfaces.emplace_back();

    // emit lmshift
    map.exported_lmshifts.push_back(face->original_side->lmshift);
    Q_assert(map.bsp.dfaces.size() == map.exported_lmshifts.size());

    out.planenum = ExportMapPlane(face->planenum & ~1);
    out.side = face->planenum & 1;
    out.texinfo = ExportMapTexinfo(face->texinfo);
    for (i = 0; i < MAXLIGHTMAPS; i++)
        out.styles[i] = 255;
    out.lightofs = -1;

    // emit surfedges
    out.firstedge = static_cast<int32_t>(map.bsp.dsurfedges.size());
    std::copy(fragment->edges.cbegin(), fragment->edges.cbegin() + fragment->output_vertices.size(),
        std::back_inserter(map.bsp.dsurfedges));
    fragment->edges.clear();

    out.numedges = static_cast<int32_t>(map.bsp.dsurfedges.size()) - out.firstedge;

    stats.unique_faces++;
}

/*
================
MakeFaceEdges_r
================
*/
static void EmitFaces_R(node_t *node, emit_faces_stats_t &stats)
{
    if (node->is_leaf()) {
        return;
    }

    auto *nodedata = node->get_nodedata();
    nodedata->firstface = static_cast<int>(map.bsp.dfaces.size());

    for (auto &face : nodedata->facelist) {
        // emit a region
        for (auto &fragment : face->fragments) {
            EmitEdges(face.get(), &fragment, stats);
            EmitFaceFragment(face.get(), &fragment, stats);
        }
    }

    nodedata->numfaces = static_cast<int>(map.bsp.dfaces.size()) - nodedata->firstface;

    EmitFaces_R(nodedata->children[0], stats);
    EmitFaces_R(nodedata->children[1], stats);
}

/*
================
MakeFaceEdges
================
*/
size_t EmitFaces(node_t *headnode)
{
    logging::funcheader();

    Q_assert(map.hashedges.empty());

    emit_faces_stats_t stats;

    size_t firstface = map.bsp.dfaces.size();

    EmitFaces_R(headnode, stats);

    map.hashedges.clear();

    return firstface;
}

//===========================================================================

/*
================
AddMarksurfaces_r

Adds the given face to the markfaces lists of all descendant leafs of `node`.
================
*/
static void AddMarksurfaces_r(face_t *face, std::unique_ptr<face_t> face_copy, node_t *node)
{
    if (auto *leafdata = node->get_leafdata()) {
        leafdata->markfaces.push_back(face);
        face->markleafs.push_back(node);
        return;
    }

    auto *nodedata = node->get_nodedata();
    const qplane3d &splitplane = nodedata->get_plane();

    auto [frontFragment, backFragment] = SplitFace(std::move(face_copy), splitplane);
    if (frontFragment) {
        AddMarksurfaces_r(face, std::move(frontFragment), nodedata->children[0]);
    }
    if (backFragment) {
        AddMarksurfaces_r(face, std::move(backFragment), nodedata->children[1]);
    }
}

/*
================
MakeMarkFaces

Populates the `markfaces` vectors of all leafs
================
*/
void MakeMarkFaces(node_t *node)
{
    if (node->is_leaf()) {
        return;
    }

    auto *nodedata = node->get_nodedata();

    // for the faces on this splitting node..
    for (auto &face : nodedata->facelist) {
        // add this face to all descendant leafs it touches

        // make a copy we can clip
        AddMarksurfaces_r(face.get(), CopyFace(face.get()), nodedata->children[face->planenum & 1]);
    }

    // process child nodes recursively
    MakeMarkFaces(nodedata->children[0]);
    MakeMarkFaces(nodedata->children[1]);
}

//===========================================================================
// FixupDetailFence
//===========================================================================

// gathers markfaces from the node and descendants, if they're in detail fence leafs
static void FixupDetailFence_FindDetailFenceFaces(std::set<face_t *> *dest, node_t *node)
{
    // descend to leafs
    if (nodedata_t *nodedata = node->get_nodedata()) {
        FixupDetailFence_FindDetailFenceFaces(dest, nodedata->children[0]);
        FixupDetailFence_FindDetailFenceFaces(dest, nodedata->children[1]);
        return;
    }

    // add this leaf's markfaces to the set
    auto *leafdata = node->get_leafdata();

    // exit if it's not a detail_fence
    if (leafdata->contents.visible_contents().flags != EWT_VISCONTENTS_WINDOW)
        return;

    for (auto *f : leafdata->markfaces) {
        dest->insert(f);
    }
}

// does this cluster have any leafs with detail fence as their strongest content type?
static bool FixupMarkFaces_ProcessCluster_HasDetailFence(node_t *node)
{
    // descend to leafs
    if (nodedata_t *nodedata = node->get_nodedata()) {
        return FixupMarkFaces_ProcessCluster_HasDetailFence(nodedata->children[0]) ||
               FixupMarkFaces_ProcessCluster_HasDetailFence(nodedata->children[1]);
    }

    return !!(node->get_leafdata()->contents.visible_contents().flags & EWT_VISCONTENTS_WINDOW);
}

static bool FixupMarkFaces_IsUsableLeaf(node_t *node)
{
    auto flags = node->get_leafdata()->contents.visible_contents().flags;

    return (flags == EWT_VISCONTENTS_EMPTY || flags == EWT_VISCONTENTS_LAVA || flags == EWT_VISCONTENTS_SLIME ||
            flags == EWT_VISCONTENTS_WATER);
}

static node_t *FixupMarkFaces_ProcessCluster_FindStorageLeaf(node_t *node)
{
    // descend to leafs
    if (nodedata_t *nodedata = node->get_nodedata()) {
        // return the first child that is usable
        for (int i = 0; i < 2; ++i)
            if (auto *found = FixupMarkFaces_ProcessCluster_FindStorageLeaf(nodedata->children[i]))
                return found;

        return nullptr;
    }

    // make sure it's usable
    auto *leafdata = node->get_leafdata();
    if (!FixupMarkFaces_IsUsableLeaf(node))
        return nullptr;

    // it's usable, return it
    return node;
}

static void FixupMarkFaces_AddFacesToLeaf(node_t *node, const std::set<face_t *> &marfaces_to_add)
{
    Q_assert(FixupMarkFaces_IsUsableLeaf(node));
    auto *leafdata = node->get_leafdata();

    // ensure this leaf's marksurfaces list contains everything in marfaces_to_add
    auto current_markfaces = std::set<face_t *>(leafdata->markfaces.begin(), leafdata->markfaces.end());

    for (face_t *f : marfaces_to_add) {
        current_markfaces.insert(f);
    }

    auto new_markfaces = std::vector<face_t *>(current_markfaces.begin(), current_markfaces.end());
    leafdata->markfaces = new_markfaces;
}

/**
 * Does the func_detail_fence fixup process described below for this 1 cluster (if it has any detail_fence in it).
 */
static void FixupMarkFaces_ProcessCluster(node_t *node)
{
    // need to fix up?
    if (!FixupMarkFaces_ProcessCluster_HasDetailFence(node))
        return;

    logging::print("fixing up cluster at {}\n", node->bounds.centroid());

    // gather all marksurfaces of func_detail_fence containing leafs in the cluster into a std::set
    std::set<face_t *> marfaces_to_propagate;
    FixupDetailFence_FindDetailFenceFaces(&marfaces_to_propagate, node);

    // start with the cluster...
    std::vector<node_t *> queue;
    queue.push_back(node);
    int front = 0;

    std::set<node_t *> visited;

    // results of the flood fill
    std::vector<node_t *> storage_leafs;

    while (front < queue.size()) {
        // pop front, and visit it
        node_t *current_node = queue[front++];
        visited.insert(current_node);

        // to visit: either we store the marfaces_to_propagate,
        // _or_ we push all valid neighbours (unvisited, vis-visible) to the queue.
        if (auto *storage_leaf = FixupMarkFaces_ProcessCluster_FindStorageLeaf(current_node)) {
            storage_leafs.push_back(storage_leaf);

            // processing done on this cluster
            continue;
        }

        // we couldn't store the marksurfaces in current_node. so we need to push all of its
        // neighbours
        bool is_on_back;
        for (portal_t *p = current_node->portals; p; p = p->next[is_on_back]) {
            is_on_back = (p->nodes.back == current_node);

            node_t *other_cluster = is_on_back ? p->nodes.front : p->nodes.back;
            if (visited.find(other_cluster) != visited.end())
                continue;
            if (!Portal_VisFlood(p))
                continue;

            queue.push_back(other_cluster);
        }
    }

    // final part: now that we've identified the storage destinations, actually store there
    for (auto *storage_leaf : storage_leafs) {
        FixupMarkFaces_AddFacesToLeaf(storage_leaf, marfaces_to_propagate);
    }
}

// process all clusters in the tree with FixupMarkFaces_ProcessCluster
static void FixupDetailFenceMarkFaces_R(node_t *node)
{
    // visit all clusters
    if (nodedata_t *nodedata = node->get_nodedata()) {
        if (nodedata->detail_separator) {
            // process cluster
            FixupMarkFaces_ProcessCluster(node);
            return;
        }

        // non-cluster leaf.. descend
        FixupDetailFenceMarkFaces_R(nodedata->children[0]);
        FixupDetailFenceMarkFaces_R(nodedata->children[1]);
        return;
    }

    // it's a regular leaf.. process as cluster
    FixupMarkFaces_ProcessCluster(node);
}

/**
 * func_detail_fence (internally we call it WINDOW because it's identical to Q2 WINDOW)
 * does not map perfectly to any contents type in Q1 so we need to emulate it.
 *
 * We write them as solid (see `gamedef_q1_like_t::contents_remap_for_export()`) but this has some issues,
 * because players are supposed to be able to see inside, but vanilla Quake ignores marksurfaces on solid leafs.
 *
 * This is the workaround; the idea is, we take the marksurfaces that would be rendered as a part of
 * func_detail_fence leafs, and propagate them outwards to the nearest empty leafs and use that to trick the renderer
 * into drawing inside the func_detail_fence (solid in the .bsp).
 *
 * More precise description: for each cluster with _any_ detail_fence in it:
 * 1. gather _all_ marksurfaces in detail_fence leafs in that cluster
 * 2. use the cluster itself if it is "usable" as defined below
 * 3. if not, flood fill through see-through cluster portals until we find _all_ "usable" clusters - these could be
 *   several clusters away
 *
 * "usable cluster" is defined as "having 1 or more non-solid leaf in it", and the action we do with all of them is
 * add the set of marksurfaces identified in step 1.
 */
void FixupDetailFence(tree_t &tree)
{
    if (tree.portaltype != portaltype_t::VIS) {
        return;
    }

    if (qbsp_options.target_game->id == GAME_QUAKE_II) {
        // Q2 natively supports detail fence (WINDOW) so this isn't needed
        return;
    }
    if (!qbsp_options.fixupdetailfence.value()) {
        return;
    }

    FixupDetailFenceMarkFaces_R(tree.headnode);
}

//===========================================================================

/*
===============
SubdivideFace

If the face is >256 in either texture direction, carve a valid sized
piece off and insert the remainder in the next link
===============
*/
static std::list<std::unique_ptr<face_t>> SubdivideFace(std::unique_ptr<face_t> f, makefaces_stats_t &stats)
{
    double mins, maxs;
    double v;
    int axis;
    qplane3d plane;
    double subdiv;
    double extent;
    int lmshift;

    /* special (non-surface cached) faces don't need subdivision */
    const maptexinfo_t &tex = f->get_texinfo();

    if (tex.flags.is_nodraw() || tex.flags.is_hint() || !qbsp_options.target_game->surf_is_subdivided(tex.flags)) {
        std::list<std::unique_ptr<face_t>> result;
        result.push_back(std::move(f));
        return result;
    }
    // subdivision is pretty much pointless other than because of lightmap block limits
    // one lightmap block will always be added at the end, for smooth interpolation

    // engines that do support scaling will support 256*256 blocks (at whatever scale).
    lmshift = f->original_side->lmshift;
    if (lmshift > 4)
        lmshift = 4; // no bugging out with legacy lighting

    // legacy engines support 18*18 max blocks (at 1:16 scale).
    // the 18*18 limit can be relaxed in certain engines, and doing so will generally give a performance boost.
    subdiv = std::min(qbsp_options.subdivide.value(), 255 << lmshift);

    //      subdiv += 8;

    // floating point precision from clipping means we should err on the low side
    // the bsp is possibly going to be used in both engines that support scaling and those that do not. this means we
    // always over-estimate by 16 rather than 1<<lmscale

    std::list<std::unique_ptr<face_t>> surfaces;
    surfaces.push_back(std::move(f));

    for (axis = 0; axis < 2; axis++) {
        // we'll transfer faces that are chopped down to size to this list
        std::list<std::unique_ptr<face_t>> chopped;

        while (!surfaces.empty()) {
            f = std::move(surfaces.front());
            surfaces.pop_front();

            mins = VECT_MAX;
            maxs = -VECT_MAX;

            qvec3d tmp = tex.vecs.row(axis).xyz();

            for (int32_t i = 0; i < f->w.size(); i++) {
                v = qv::dot(f->w[i], tmp);
                if (v < mins)
                    mins = v;
                if (v > maxs)
                    maxs = v;
            }

            extent = ceil(maxs) - floor(mins);
            //          extent = maxs - mins;
            if (extent <= subdiv) {
                // this face is already good
                chopped.push_back(std::move(f));
                continue;
            }

            // split it
            plane.normal = tmp;
            v = qv::normalizeInPlace(plane.normal);

            // ericw -- reverted this, was causing https://github.com/ericwa/ericw-tools/issues/160
            //            if (subdiv > extent/2)      /* if we're near a boundary, just split the difference, this
            //            should balance the load slightly */
            //                plane.dist = (mins + subdiv/2) / v;
            //            else
            //                plane.dist = (mins + subdiv) / v;
            plane.dist = (mins + subdiv - 16) / v;

            std::unique_ptr<face_t> front;
            std::unique_ptr<face_t> back;
            std::tie(front, back) = SplitFace(std::move(f), plane);
            if (!front || !back) {
                // logging::print("didn't split\n");
                //  FError("Didn't split the polygon");
            }

            if (front) {
                surfaces.push_back(std::move(front));
            }
            if (back) {
                chopped.push_front(std::move(back));
            }
        }

        // we've finished chopping on this axis, but we may need to chop on other axes
        Q_assert(surfaces.empty());

        surfaces = std::move(chopped);
    }

    stats.c_subdivide += surfaces.size() - 1;

    return surfaces;
}

static void SubdivideNodeFaces(node_t *node, makefaces_stats_t &stats)
{
    std::list<std::unique_ptr<face_t>> result;

    auto *nodedata = node->get_nodedata();

    // subdivide each face and push the results onto subdivided
    for (auto &face : nodedata->facelist) {
        result.splice(result.end(), SubdivideFace(std::move(face), stats));
    }

    nodedata->facelist = std::move(result);
}

/*
============
FaceFromPortal

pside is which side of portal (equivalently, which side of the node) we're in.
Typically, we're in an empty leaf and the other side of the portal is a solid wall.

see also FindPortalSide which populates p->side
============
*/
static std::unique_ptr<face_t> FaceFromPortal(portal_t *p, bool pside)
{
    side_t *side = p->sides[pside];
    if (!side)
        return nullptr; // portal does not bridge different visible contents

    Q_assert(side->source);

    auto f = std::make_unique<face_t>();

    f->texinfo = side->texinfo;
    f->planenum = (side->planenum & ~1) | (pside ? 1 : 0);
    f->portal = p;
    f->original_side = side->source;

    if (pside) {
        f->w = p->winding.flip();
    } else {
        f->w = p->winding.clone();
    }

    f->contents = {
        .front = p->nodes[pside]->get_leafdata()->contents, .back = p->nodes[!pside]->get_leafdata()->contents};

    UpdateFaceSphere(f.get());

    return f;
}

/*
===============
MakeFaces_r

If a portal will make a visible face,
mark the side that originally created it

  solid / empty : solid
  solid / water : solid
  water / empty : water
  water / water : none
===============
*/
static void MakeFaces_r(node_t *node, makefaces_stats_t &stats)
{
    // recurse down to leafs
    if (auto *nodedata = node->get_nodedata()) {
        MakeFaces_r(nodedata->children[0], stats);
        MakeFaces_r(nodedata->children[1], stats);

        // merge together all visible faces on the node
        if (!qbsp_options.nomerge.value())
            MergeNodeFaces(node, stats);
        if (qbsp_options.subdivide.boolValue())
            SubdivideNodeFaces(node, stats);

        return;
    }

    auto *leafdata = node->get_leafdata();

    // solid leafs never have visible faces
    if (leafdata->contents.is_any_solid())
        return;

    // see which portals are valid

    // (Note, this is happening per leaf, so we can potentially generate faces
    // for the same portal once from one leaf, and once from the neighbouring one)
    bool is_on_back;

    for (portal_t *p = node->portals; p; p = p->next[is_on_back]) {
        is_on_back = (p->nodes.back == node);

        std::unique_ptr<face_t> f = FaceFromPortal(p, is_on_back);

        if (f) {
            stats.c_nodefaces++;
            p->onnode->get_nodedata()->facelist.push_back(std::move(f));
        }
    }
}

/*
============
MakeFaces
============
*/
void MakeFaces(node_t *node)
{
    logging::funcheader();

    makefaces_stats_t stats{};

    MakeFaces_r(node, stats);
}
