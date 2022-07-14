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
#include <qbsp/portals.hh>
#include <qbsp/csg.hh>
#include <qbsp/map.hh>
#include <qbsp/merge.hh>
#include <qbsp/brushbsp.hh>
#include <qbsp/qbsp.hh>
#include <qbsp/writebsp.hh>

#include <map>
#include <list>

static bool ShouldOmitFace(face_t *f)
{
    if (!qbsp_options.includeskip.value() && map.mtexinfos.at(f->texinfo).flags.is_skip)
        return true;
    if (map.mtexinfos.at(f->texinfo).flags.is_hint)
        return true;

    // HACK: to save a few faces, don't output the interior faces of sky brushes
    if (f->contents.is_sky(qbsp_options.target_game)) {
        return true;
    }

    return false;
}

static void MergeNodeFaces(node_t *node)
{
    node->facelist = MergeFaceList(std::move(node->facelist));
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

    f->output_vertices.resize(f->w.size());

    for (size_t i = 0; i < f->w.size(); i++) {
        EmitVertex(f->w[i], f->output_vertices[i]);
    }

    f->original_vertices = f->output_vertices;
}

static void EmitVertices_R(node_t *node)
{
    if (node->planenum == PLANENUM_LEAF) {
        return;
    }

    for (auto &f : node->facelist) {
        EmitFaceVertices(f.get());
    }

    EmitVertices_R(node->children[0].get());
    EmitVertices_R(node->children[1].get());
}

void EmitVertices(node_t *headnode)
{
    EmitVertices_R(headnode);
}

//===========================================================================

/*
==================
GetEdge

Returns a global edge number, possibly negative to indicate a backwards edge.
==================
*/
inline int64_t GetEdge(const size_t &v1, const size_t &v2, const face_t *face)
{
    if (!face->contents.is_valid(qbsp_options.target_game, false))
        FError("Face with invalid contents");

    // search for existing edges
    if (auto it = map.hashedges.find(std::make_pair(v1, v2)); it != map.hashedges.end()) {
        return it->second;
    } else if (auto it = map.hashedges.find(std::make_pair(v2, v1)); it != map.hashedges.end()) {
        return -it->second;
    }

    /* emit an edge */
    int64_t i = map.bsp.dedges.size();

    map.bsp.dedges.emplace_back(bsp2_dedge_t{static_cast<uint32_t>(v1), static_cast<uint32_t>(v2)});

    map.add_hash_edge(v1, v2, i);

    return i;
}

static void FindFaceFragmentEdges(face_t *face, face_fragment_t *fragment)
{
    Q_assert(fragment->outputnumber == std::nullopt);

    if (qbsp_options.maxedges.value() && fragment->output_vertices.size() > qbsp_options.maxedges.value()) {
        FError("Internal error: face->numpoints > max edges ({})", qbsp_options.maxedges.value());
    }

    fragment->edges.resize(fragment->output_vertices.size());

    for (size_t i = 0; i < fragment->output_vertices.size(); i++) {
        auto &p1 = fragment->output_vertices[i];
        auto &p2 = fragment->output_vertices[(i + 1) % fragment->output_vertices.size()];
        fragment->edges[i] = GetEdge(p1, p2, face);
    }
}

/*
==================
FindFaceEdges
==================
*/
static void FindFaceEdges(face_t *face)
{
    if (ShouldOmitFace(face))
        return;

    FindFaceFragmentEdges(face, face);

    for (auto &fragment : face->fragments) {
        FindFaceFragmentEdges(face, &fragment);
    }
}

/*
================
MakeFaceEdges_r
================
*/
static void MakeFaceEdges_r(node_t *node)
{
    if (node->planenum == PLANENUM_LEAF)
        return;

    for (auto &f : node->facelist) {
        FindFaceEdges(f.get());
    }

    MakeFaceEdges_r(node->children[0].get());
    MakeFaceEdges_r(node->children[1].get());
}

/*
==============
EmitFaceFragment
==============
*/
static void EmitFaceFragment(face_t *face, face_fragment_t *fragment)
{
    // this can't really happen, but just in case it ever does..
    // (I use this in testing to find faces of interest)
    if (!fragment->output_vertices.size()) {
        logging::print("WARNING: zero-point triangle attempted to be emitted\n");
        return;
    }

    int i;

    // emit a region
    Q_assert(!fragment->outputnumber.has_value());
    fragment->outputnumber = map.bsp.dfaces.size();

    mface_t &out = map.bsp.dfaces.emplace_back();

    // emit lmshift
    map.exported_lmshifts.push_back(face->lmshift);
    Q_assert(map.bsp.dfaces.size() == map.exported_lmshifts.size());

    out.planenum = ExportMapPlane(face->planenum);
    out.side = face->planeside;
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
}

/*
==============
EmitFace
==============
*/
static void EmitFace(face_t *face)
{
    if (ShouldOmitFace(face))
        return;

    EmitFaceFragment(face, face);

    for (auto &fragment : face->fragments) {
        EmitFaceFragment(face, &fragment);
    }
}

/*
==============
GrowNodeRegion
==============
*/
static void GrowNodeRegion(node_t *node)
{
    if (node->planenum == PLANENUM_LEAF)
        return;

    node->firstface = static_cast<int>(map.bsp.dfaces.size());

    for (auto &face : node->facelist) {
        //Q_assert(face->planenum == node->planenum);

        // emit a region
        EmitFace(face.get());
    }

    node->numfaces = static_cast<int>(map.bsp.dfaces.size()) - node->firstface;

    GrowNodeRegion(node->children[0].get());
    GrowNodeRegion(node->children[1].get());
}

/*
================
MakeFaceEdges
================
*/
int MakeFaceEdges(node_t *headnode)
{
    int firstface;

    logging::print(logging::flag::PROGRESS, "---- {} ----\n", __func__);

    firstface = static_cast<int>(map.bsp.dfaces.size());
    MakeFaceEdges_r(headnode);

    logging::print(logging::flag::PROGRESS, "---- GrowRegions ----\n");
    GrowNodeRegion(headnode);

    return firstface;
}

//===========================================================================

static int c_nodefaces;

/*
================
AddMarksurfaces_r

Adds the given face to the markfaces lists of all descendant leafs of `node`.

fixme-brushbsp: all leafs in a cluster can share the same marksurfaces, right?
================
*/
static void AddMarksurfaces_r(face_t *face, std::unique_ptr<face_t> face_copy, node_t *node)
{
    if (node->planenum == PLANENUM_LEAF) {
        node->markfaces.push_back(face);
        return;
    }

    const auto lock = std::lock_guard(map_planes_lock);
    const qbsp_plane_t &splitplane = map.planes.at(node->planenum);

    auto [frontFragment, backFragment] = SplitFace(std::move(face_copy), splitplane);
    if (frontFragment) {
        AddMarksurfaces_r(face, std::move(frontFragment), node->children[0].get());
    }
    if (backFragment) {
        AddMarksurfaces_r(face, std::move(backFragment), node->children[1].get());
    }
}

/*
================
MakeMarkFaces

Populates the `markfaces` vectors of all leafs
================
*/
void MakeMarkFaces(node_t* node)
{
    if (node->planenum == PLANENUM_LEAF) {
        return;
    }

    // for the faces on this splitting node..
    for (auto &face : node->facelist) {
        // add this face to all descendant leafs it touches
        
        // make a copy we can clip
        auto face_copy = CopyFace(face.get());

        if (face->planeside == 0) {
            AddMarksurfaces_r(face.get(), std::move(face_copy), node->children[0].get());
        } else {
            AddMarksurfaces_r(face.get(), std::move(face_copy), node->children[1].get());
        }
    }

    // process child nodes recursively
    MakeMarkFaces(node->children[0].get());
    MakeMarkFaces(node->children[1].get());
}

struct makefaces_stats_t {
    int	c_nodefaces;
    int c_merge;
    int c_subdivide;
};

/*
===============
SubdivideFace

If the face is >256 in either texture direction, carve a valid sized
piece off and insert the remainder in the next link
===============
*/
static std::list<std::unique_ptr<face_t>> SubdivideFace(std::unique_ptr<face_t> f)
{
    vec_t mins, maxs;
    vec_t v;
    int axis;
    qbsp_plane_t plane;
    const maptexinfo_t *tex;
    vec_t subdiv;
    vec_t extent;
    int lmshift;

    /* special (non-surface cached) faces don't need subdivision */
    tex = &map.mtexinfos.at(f->texinfo);

    if (tex->flags.is_skip || tex->flags.is_hint || !qbsp_options.target_game->surf_is_subdivided(tex->flags)) {
        std::list<std::unique_ptr<face_t>> result;
        result.push_back(std::move(f));
        return result;
    }
    // subdivision is pretty much pointless other than because of lightmap block limits
    // one lightmap block will always be added at the end, for smooth interpolation

    // engines that do support scaling will support 256*256 blocks (at whatever scale).
    lmshift = f->lmshift;
    if (lmshift > 4)
        lmshift = 4; // no bugging out with legacy lighting

    // legacy engines support 18*18 max blocks (at 1:16 scale).
    // the 18*18 limit can be relaxed in certain engines, and doing so will generally give a performance boost.
    subdiv = min(qbsp_options.subdivide.value(), 255 << lmshift);

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

            qvec3d tmp = tex->vecs.row(axis).xyz();

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
                //logging::print("didn't split\n");
                // FError("Didn't split the polygon");
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

    return surfaces;
}

static void SubdivideNodeFaces(node_t *node)
{
    std::list<std::unique_ptr<face_t>> result;

    // subdivide each face and push the results onto subdivided
    for (auto &face : node->facelist) {
        result.splice(result.end(), SubdivideFace(std::move(face)));
    }

    node->facelist = std::move(result);
}

/*
============
FaceFromPortal

pside is which side of portal (equivalently, which side of the node) we're in.
Typically, we're in an empty leaf and the other side of the portal is a solid wall.

see also FindPortalSide which populates p->side
============
*/
static std::unique_ptr<face_t> FaceFromPortal(portal_t *p, int pside)
{
    side_t *side = p->side;
    if (!side)
        return nullptr;	// portal does not bridge different visible contents

    auto f = std::unique_ptr<face_t>(new face_t{});

    f->texinfo = side->texinfo;
    f->planenum = side->planenum;
    f->planeside = static_cast<planeside_t>(pside);
    f->portal = p;
    f->lmshift = side->lmshift;

    bool make_face =
        qbsp_options.target_game->directional_visible_contents(p->nodes[pside]->contents, p->nodes[!pside]->contents);
    if (!make_face) {
        // content type / game rules requested to skip generating a face on this side
        // todo-brushbsp: remove when appropriate
        logging::print("skipped face for {} -> {} portal\n",
            p->nodes[pside]->contents.to_string(qbsp_options.target_game),
            p->nodes[!pside]->contents.to_string(qbsp_options.target_game));
        return nullptr;
    }

    if (!p->nodes[pside]->contents.is_empty(qbsp_options.target_game)) {
        bool our_contents_mirrorinside = qbsp_options.target_game->contents_are_mirrored(p->nodes[pside]->contents);
        if (!our_contents_mirrorinside) {
            if (side->planeside != pside) {

                return nullptr;
            }
        }
    }


    if (pside)
    {
        f->w = p->winding->flip();
        f->contents = p->nodes[1]->contents;
    }
    else
    {
        f->w = *p->winding;
        f->contents = p->nodes[0]->contents;
    }

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
static void MakeFaces_r(node_t *node, makefaces_stats_t& stats)
{
    // recurse down to leafs
    if (node->planenum != PLANENUM_LEAF)
    {
        MakeFaces_r(node->children[0].get(), stats);
        MakeFaces_r(node->children[1].get(), stats);

        // merge together all visible faces on the node
        if (!qbsp_options.nomerge.value())
            MergeNodeFaces(node);
        if (qbsp_options.subdivide.boolValue())
            SubdivideNodeFaces(node);

        return;
    }

    // solid leafs never have visible faces
    if (node->contents.is_any_solid(qbsp_options.target_game))
        return;

    // see which portals are valid

    // (Note, this is happening per leaf, so we can potentially generate faces
    // for the same portal once from one leaf, and once from the neighbouring one)
    int s;
    for (portal_t *p = node->portals; p; p = p->next[s])
    {
        // 1 means node is on the back side of planenum
        s = (p->nodes[1] == node);

        std::unique_ptr<face_t> f = FaceFromPortal(p, s);
        if (f)
        {
            stats.c_nodefaces++;
            p->face[s] = f.get();
            p->onnode->facelist.push_back(std::move(f));
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
    logging::print(logging::flag::PROGRESS, "--- {} ---\n", __func__);

    makefaces_stats_t stats{};

    MakeFaces_r(node, stats);

    logging::print(logging::flag::STAT, "{} makefaces\n", stats.c_nodefaces);
    logging::print(logging::flag::STAT, "{} merged\n", stats.c_merge);
    logging::print(logging::flag::STAT, "{} subdivided\n", stats.c_subdivide);
}
