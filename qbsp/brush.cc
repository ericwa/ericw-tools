/*
    Copyright (C) 1996-1997  Id Software, Inc.
    Copyright (C) 1997       Greg Lewis
    Copyright (C) 1999-2005  Id Software, Inc.

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

#include <cstring>
#include <list>
#include <common/log.hh>
#include <qbsp/map.hh>
#include <qbsp/qbsp.hh>

side_t side_t::clone_non_winding_data() const
{
    side_t result;
    result.planenum = this->planenum;
    result.texinfo = this->texinfo;
    result.onnode = this->onnode;
    result.bevel = this->bevel;
    result.source = this->source;
    result.tested = this->tested;
    return result;
}

side_t side_t::clone() const
{
    side_t result = clone_non_winding_data();
    result.w = this->w.clone();
    return result;
}

bool side_t::is_visible() const
{
    // workaround for qbsp_q2_mist_clip.map - we want to treat nodraw faces as "!visible"
    // so they're used as splitters after mist
    if (get_texinfo().flags.is_nodraw) {
        if (get_texinfo().flags.is_hint) {
            return true;
        }

        return false;
    }

    return source && source->visible;
}

const maptexinfo_t &side_t::get_texinfo() const
{
    return map.mtexinfos[this->texinfo];
}

const qbsp_plane_t &side_t::get_plane() const
{
    return map.get_plane(planenum);
}

const qbsp_plane_t &side_t::get_positive_plane() const
{
    return map.get_plane(planenum & ~1);
}

bspbrush_t::ptr bspbrush_t::copy_unique() const
{
    return bspbrush_t::make_ptr(this->clone());
}

bspbrush_t bspbrush_t::clone() const
{
    bspbrush_t result;

    result.original_ptr = this->original_ptr;
    result.mapbrush = this->mapbrush;

    result.bounds = this->bounds;
    result.side = this->side;
    result.testside = this->testside;

    result.sides.reserve(this->sides.size());
    for (auto &side : this->sides) {
        result.sides.push_back(side.clone());
    }

    result.contents = this->contents;

    result.sphere_origin = this->sphere_origin;
    result.sphere_radius = this->sphere_radius;

    return result;
}

bool bspbrush_t::contains_point(const qvec3d &point, vec_t epsilon) const
{
    for (auto &side : sides) {
        if (side.get_plane().distance_to(point) > epsilon) {
            return false;
        }
    }
    return true;
}

/*
=================
CheckFace

Note: this will not catch 0 area polygons
=================
*/
static void CheckFace(
    side_t *face, const mapface_t &sourceface, std::optional<std::reference_wrapper<size_t>> num_clipped)
{
    if (face->w.size() < 3) {
        if (qbsp_options.verbose.value()) {
            if (face->w.size() == 2) {
                logging::print("WARNING: {}: partially clipped into degenerate polygon @ ({}) - ({})\n",
                    sourceface.line, face->w[0], face->w[1]);
            } else if (face->w.size() == 1) {
                logging::print(
                    "WARNING: {}: partially clipped into degenerate polygon @ ({})\n", sourceface.line, face->w[0]);
            } else {
                logging::print("WARNING: {}: completely clipped away\n", sourceface.line);
            }
        }

        if (num_clipped) {
            (*num_clipped)++;
        }

        face->w.clear();
        return;
    }

    const qbsp_plane_t &plane = face->get_plane();
    qvec3d facenormal = plane.get_normal();

    for (size_t i = 0; i < face->w.size(); i++) {
        const qvec3d &p1 = face->w[i];
        const qvec3d &p2 = face->w[(i + 1) % face->w.size()];

        for (auto &v : p1) {
            if (fabs(v) > qbsp_options.worldextent.value()) {
                // this is fatal because a point should never lay outside the world
                FError("{}: coordinate out of range ({})\n", sourceface.line, v);
            }
        }

        /* check the point is on the face plane */
        {
            vec_t dist = face->get_plane().distance_to(p1);
            if (fabs(dist) > qbsp_options.epsilon.value()) {
                logging::print("WARNING: {}: Point ({:.3} {:.3} {:.3}) off plane by {:2.4}\n", sourceface.line, p1[0],
                    p1[1], p1[2], dist);
            }
        }

        /* check the edge isn't degenerate */
        qvec3d edgevec = p2 - p1;
        vec_t length = qv::length(edgevec);
        if (length < qbsp_options.epsilon.value()) {
            logging::print("WARNING: {}: Healing degenerate edge ({}) at ({:.3f} {:.3} {:.3})\n", sourceface.line,
                length, p1[0], p1[1], p1[2]);
            for (size_t j = i + 1; j < face->w.size(); j++)
                face->w[j - 1] = face->w[j];
            face->w.resize(face->w.size() - 1);
            CheckFace(face, sourceface, num_clipped);
            break;
        }

        qvec3d edgenormal = qv::normalize(qv::cross(facenormal, edgevec));
        vec_t edgedist = qv::dot(p1, edgenormal);
        edgedist += qbsp_options.epsilon.value();

        /* all other points must be on front side */
        for (size_t j = 0; j < face->w.size(); j++) {
            if (j == i)
                continue;
            vec_t dist = qv::dot(face->w[j], edgenormal);
            if (dist > edgedist) {
                logging::print("WARNING: {}: Found a non-convex face (error size {}, point: {})\n", sourceface.line,
                    dist - edgedist, face->w[j]);
                face->w.clear();
                return;
            }
        }
    }
}

/*
=============================================================================

                        TURN BRUSHES INTO GROUPS OF FACES

=============================================================================
*/

/*
=================
FindTargetEntity

Finds the entity whose `targetname` value is case-insensitve-equal to `target`.
=================
*/
static const mapentity_t *FindTargetEntity(const std::string &target)
{
    for (const auto &entity : map.entities) {
        const std::string &name = entity.epairs.get("targetname");
        if (string_iequals(target, name)) {
            return &entity;
        }
    }

    return nullptr;
}

/*
=================
FixRotateOrigin
=================
*/
qvec3d FixRotateOrigin(mapentity_t &entity)
{
    const std::string &search = entity.epairs.get("target");
    const mapentity_t *target = nullptr;

    if (!search.empty()) {
        target = FindTargetEntity(search);
    }

    qvec3d offset;

    if (target) {
        target->epairs.get_vector("origin", offset);
    } else {
        logging::print("WARNING: No target for rotation entity \"{}\"", entity.epairs.get("classname"));
        offset = {};
    }

    entity.epairs.set("origin", qv::to_string(offset));
    return offset;
}

//============================================================================

/*
==================
CreateBrushWindings

Create all of the windings for the specified brush, and
calculate its bounds.
==================
*/
bool CreateBrushWindings(bspbrush_t &brush)
{
    for (int i = 0; i < brush.sides.size(); i++) {
        side_t &side = brush.sides[i];
        std::optional<winding_t> w = BaseWindingForPlane<winding_t>(side.get_plane());

        for (int j = 0; j < brush.sides.size() && w; j++) {
            if (i == j) {
                continue;
            }
            if (brush.sides[j].bevel) {
                continue;
            }
            const qplane3d &plane = map.planes[brush.sides[j].planenum ^ 1];
            w = w->clip_front(plane, qbsp_options.epsilon.value(), false);
        }

        if (w) {
            for (auto &p : *w) {
                for (auto &v : p) {
                    if (fabs(v) > qbsp_options.worldextent.value()) {
                        logging::print("WARNING: {}: invalid winding point\n",
                            brush.mapbrush ? brush.mapbrush->line : parser_source_location{});
                        w = std::nullopt;
                        break;
                    }
                }

                if (!w) {
                    break;
                }
            }

            side.w = std::move(*w);
            if (side.source) {
                side.source->visible = true;
            }
        } else {
            side.w.clear();
            if (side.source) {
                side.source->visible = false;
            }
        }
    }

    return brush.update_bounds(true);
}

#define QBSP3

#ifndef QBSP3
/*
==============================================================================

BEVELED CLIPPING HULL GENERATION

This is done by brute force, and could easily get a lot faster if anyone cares.
==============================================================================
*/

struct hullbrush_t
{
    bspbrush_t &brush;

    std::vector<qvec3d> points;
    std::vector<qvec3d> corners;
    std::vector<std::array<size_t, 2>> edges;
};

/*
============
AddBrushPlane
=============
*/
static bool AddBrushPlane(hullbrush_t &hullbrush, const qbsp_plane_t &plane)
{
    for (auto &s : hullbrush.brush.sides) {
        if (qv::epsilonEqual(s.get_plane(), plane)) {
            return false;
        }
    }

    auto &s = hullbrush.brush.sides.emplace_back();
    s.planenum = map.add_or_find_plane(plane);
    s.texinfo = 0;
    return true;
}

/*
============
TestAddPlane

Adds the given plane to the brush description if all of the original brush
vertexes can be put on the front side
=============
*/
static bool TestAddPlane(hullbrush_t &hullbrush, const qbsp_plane_t &plane)
{
    /* see if the plane has already been added */
    for (auto &s : hullbrush.brush.sides) {
        if (qv::epsilonEqual(plane, s.get_plane()) || qv::epsilonEqual(plane, s.get_positive_plane())) {
            return false;
        }
    }

    /* check all the corner points */
    bool points_front = false;
    bool points_back = false;

    for (size_t i = 0; i < hullbrush.corners.size(); i++) {
        vec_t d = qv::dot(hullbrush.corners[i], plane.get_normal()) - plane.get_dist();

        if (d < -qbsp_options.epsilon.value()) {
            if (points_front) {
                return false;
            }
            points_back = true;
        } else if (d > qbsp_options.epsilon.value()) {
            if (points_back) {
                return false;
            }
            points_front = true;
        }
    }

    // the plane is a seperator
    if (points_front) {
        return AddBrushPlane(hullbrush, -plane);
    } else {
        return AddBrushPlane(hullbrush, plane);
    }
}

/*
============
AddHullPoint

Doesn't add if duplicated
=============
*/
static size_t AddHullPoint(hullbrush_t &hullbrush, const qvec3d &p, const aabb3d &hull_size)
{
    for (auto &pt : hullbrush.points) {
        if (qv::epsilonEqual(p, pt, QBSP_EQUAL_EPSILON)) {
            return &pt - hullbrush.points.data();
        }
    }

    hullbrush.points.emplace_back(p);

    for (size_t x = 0; x < 2; x++) {
        for (size_t y = 0; y < 2; y++) {
            for (size_t z = 0; z < 2; z++) {
                hullbrush.corners.emplace_back(p + qvec3d{hull_size[x][0], hull_size[y][1], hull_size[z][2]});
            }
        }
    }

    return hullbrush.points.size() - 1;
}

/*
============
AddHullEdge

Creates all of the hull planes around the given edge, if not done already
=============
*/
static bool AddHullEdge(hullbrush_t &hullbrush, const qvec3d &p1, const qvec3d &p2, const aabb3d &hull_size)
{
    std::array<size_t, 2> edge = {AddHullPoint(hullbrush, p1, hull_size), AddHullPoint(hullbrush, p2, hull_size)};

    for (auto &e : hullbrush.edges) {
        if (e == edge || e == decltype(edge){edge[1], edge[0]}) {
            return false;
        }
    }

    hullbrush.edges.emplace_back(edge);

    qvec3d edgevec = qv::normalize(p1 - p2);
    bool added = false;

    for (size_t a = 0; a < 3; a++) {
        qvec3d planevec{};
        planevec[a] = 1;

        qplane3d plane;
        plane.normal = qv::cross(planevec, edgevec);

        vec_t length = qv::normalizeInPlace(plane.normal);

        /* If this edge is almost parallel to the hull edge, skip it. */
        if (length < ANGLEEPSILON) {
            continue;
        }

        size_t b = (a + 1) % 3;
        size_t c = (a + 2) % 3;

        for (size_t d = 0; d < 2; d++) {
            for (size_t e = 0; e < 2; e++) {
                qvec3d planeorg = p1;
                planeorg[b] += hull_size[d][b];
                planeorg[c] += hull_size[e][c];
                plane.dist = qv::dot(planeorg, plane.normal);
                added = TestAddPlane(hullbrush, plane) || added;
            }
        }
    }

    return added;
}

/*
============
ExpandBrush
=============
*/
static void ExpandBrush(hullbrush_t &hullbrush, const aabb3d &hull_size)
{
    // create all the hull points
    for (auto &f : hullbrush.brush.sides) {
        for (auto &pt : f.w) {
            AddHullPoint(hullbrush, pt, hull_size);
        }
    }

    // expand all of the planes
    for (auto &f : hullbrush.brush.sides) {
        if (f.get_texinfo().flags.no_expand) {
            continue;
        }
        qvec3d corner = {};
        qplane3d plane = f.get_plane();
        for (size_t x = 0; x < 3; x++) {
            if (plane.normal[x] > 0) {
                corner[x] = hull_size[1][x];
            } else if (plane.normal[x] < 0) {
                corner[x] = hull_size[0][x];
            }
        }
        plane.dist += qv::dot(corner, plane.normal);
        f.planenum = map.add_or_find_plane(plane);
    }

    // add any axis planes not contained in the brush to bevel off corners
    for (size_t x = 0; x < 3; x++) {
        for (int32_t s = -1; s <= 1; s += 2) {
            // add the plane
            qplane3d plane;
            plane.normal = {};
            plane.normal[x] = (vec_t)s;
            if (s == -1) {
                plane.dist = -hullbrush.brush.bounds.mins()[x] + -hull_size[0][x];
            } else {
                plane.dist = hullbrush.brush.bounds.maxs()[x] + hull_size[1][x];
            }
            AddBrushPlane(hullbrush, plane);
        }
    }

    // add all of the edge bevels
    for (size_t f = 0; f < hullbrush.brush.sides.size(); f++) {
        auto *side = &hullbrush.brush.sides[f];
        auto *w = &side->w;

        for (size_t i = 0; i < w->size(); i++) {
            if (AddHullEdge(hullbrush, (*w)[i], (*w)[(i + 1) % w->size()], hull_size)) {
                // re-fetch ptrs
                side = &hullbrush.brush.sides[f];
                w = &side->w;
            }
        }
    }
}
#endif

/*
===============
LoadBrush

Converts a mapbrush to a bsp brush
===============
*/
std::optional<bspbrush_t> LoadBrush(const mapentity_t &src, mapbrush_t &mapbrush, const contentflags_t &contents,
    hull_index_t hullnum, std::optional<std::reference_wrapper<size_t>> num_clipped)
{
    // create the brush
    bspbrush_t brush{};
    brush.contents = contents;
    brush.sides.reserve(mapbrush.faces.size());
    brush.mapbrush = &mapbrush;

    for (size_t i = 0; i < mapbrush.faces.size(); i++) {
        auto &src = mapbrush.faces[i];

        // fixme-brushbsp: should this happen for all hulls?
        // fixme-brushbsp: this causes a hint side to expand
        // to the world extents (winding & bounds) which throws
        // a lot of warnings. is this how this should be working?
#if 0
        if (!hullnum.value_or(0) && mapbrush.is_hint) {
            /* Don't generate hintskip faces */
            const maptexinfo_t &texinfo = src.get_texinfo();

            // any face that isn't a hint is assumed to be hintskip
            if (!texinfo.flags.is_hint) {
                continue;
            }
        }
#endif

#ifdef QBSP3
        // don't add bevels for the point hull
        if (!hullnum.value_or(0) && src.bevel) {
            continue;
        }
#else
        // don't add bevels
        if (src.bevel) {
            continue;
        }
#endif

        auto &dst = brush.sides.emplace_back();
        dst.texinfo = src.texinfo;
        dst.planenum = src.planenum;
        dst.bevel = src.bevel;
        dst.source = &src;
    }

    // expand the brushes for the hull
    if (hullnum.value_or(0)) {
        auto &hulls = qbsp_options.target_game->get_hull_sizes();
        Q_assert(hullnum < hulls.size());
        auto &hull = *(hulls.begin() + hullnum.value());

#ifdef QBSP3
        for (auto &mapface : brush.sides) {
            if (mapface.get_texinfo().flags.no_expand) {
                continue;
            }
            qvec3d corner{};
            for (int32_t x = 0; x < 3; x++) {
                if (mapface.get_plane().get_normal()[x] > 0) {
                    corner[x] = hull[1][x];
                } else if (mapface.get_plane().get_normal()[x] < 0) {
                    corner[x] = hull[0][x];
                }
            }
            qplane3d plane = mapface.get_plane();
            plane.dist += qv::dot(corner, plane.normal);
            mapface.planenum = map.add_or_find_plane(plane);
            mapface.bevel = false;
        }
#else

        if (!CreateBrushWindings(brush)) {
            return std::nullopt;
        }

        hullbrush_t hullbrush{brush};
        ExpandBrush(hullbrush, hull);
#endif
    }

    if (!CreateBrushWindings(brush)) {
        return std::nullopt;
    }

    for (auto &face : brush.sides) {
        CheckFace(&face, *face.source, num_clipped);
    }

    // Rotatable objects must have a bounding box big enough to
    // account for all its rotations

    // if -wrbrushes is in use, don't do this for the clipping hulls because it depends on having
    // the actual non-hacked bbox (it doesn't write axial planes).

    // Hexen2 also doesn't want the bbox expansion, it's handled in engine (see: SV_LinkEdict)

    // Only do this for hipnotic rotation. For origin brushes in Quake, it breaks some of their
    // uses (e.g. func_train). This means it's up to the mapper to expand the model bounds with
    // clip brushes if they're going to rotate a model in vanilla Quake and not use hipnotic rotation.
    // The idea behind the bounds expansion was to avoid incorrect vis culling (AFAIK).
    const bool shouldExpand = !qv::emptyExact(src.origin) && src.rotation == rotation_t::hipnotic &&
                              hullnum.has_value() &&
                              qbsp_options.target_game->id != GAME_HEXEN_II; // never do this in Hexen 2

    if (shouldExpand) {
        vec_t max = -std::numeric_limits<vec_t>::infinity(), min = std::numeric_limits<vec_t>::infinity();

        for (auto &v : brush.bounds.mins()) {
            min = std::min(min, v);
            max = std::max(max, v);
        }
        for (auto &v : brush.bounds.maxs()) {
            min = std::min(min, v);
            max = std::max(max, v);
        }

        vec_t delta = std::max(fabs(max), fabs(min));
        brush.bounds = {-delta, delta};
    }

    return brush;
}

//=============================================================================

static void Brush_LoadEntity(mapentity_t &dst, mapentity_t &src, hull_index_t hullnum, content_stats_base_t &stats,
    bspbrush_t::container &brushes, logging::percent_clock &clock, size_t &num_clipped)
{
    clock.max += src.mapbrushes.size();

    bool all_detail = false;
    bool all_detail_wall = false;
    bool all_detail_fence = false;
    bool all_detail_illusionary = false;

    const std::string &classname = src.epairs.get("classname");

    /* If the source entity is func_detail, set the content flag */
    if (!qbsp_options.nodetail.value()) {
        if (!Q_strcasecmp(classname, "func_detail")) {
            all_detail = true;
        }

        if (!Q_strcasecmp(classname, "func_detail_wall")) {
            all_detail_wall = true;
        }

        if (!Q_strcasecmp(classname, "func_detail_fence")) {
            all_detail_fence = true;
        }

        if (!Q_strcasecmp(classname, "func_detail_illusionary")) {
            all_detail_illusionary = true;
        }
    }

    for (auto &mapbrush : src.mapbrushes) {
        clock();

        if (map.is_world_entity(src) || IsWorldBrushEntity(src) || IsNonRemoveWorldBrushEntity(src)) {
            if (map.region) {
                if (map.region->bounds.disjoint(mapbrush.bounds)) {
                    // stats.regioned_brushes++;
                    // it = entity.mapbrushes.erase(it);
                    // logging::print("removed broosh\n");
                    continue;
                }
            }

            for (auto &region : map.antiregions) {
                if (!region.bounds.disjoint(mapbrush.bounds)) {
                    // stats.regioned_brushes++;
                    // it = entity.mapbrushes.erase(it);
                    // logging::print("removed broosh\n");
                    continue;
                }
            }
        }

        if (!hullnum.value_or(0)) {
            if (src.epairs.get_int("_super_detail")) {
                continue;
            }
        }

        contentflags_t contents = mapbrush.contents;

        if (qbsp_options.nodetail.value()) {
            contents = qbsp_options.target_game->clear_detail(contents);
        }

        /* "origin" brushes always discarded beforehand */
        Q_assert(!contents.is_origin(qbsp_options.target_game));

        // per-brush settings
        bool detail = false;
        bool detail_illusionary = false;
        bool detail_fence = false;
        bool detail_wall = false;

        // inherit the per-entity settings
        detail |= all_detail;
        detail_illusionary |= all_detail_illusionary;
        detail_fence |= all_detail_fence;
        detail_wall |= all_detail_wall;

        /* -omitdetail option omits all types of detail */
        if (qbsp_options.omitdetail.value() && detail)
            continue;
        if ((qbsp_options.omitdetail.value() || qbsp_options.omitdetailillusionary.value()) && detail_illusionary)
            continue;
        if ((qbsp_options.omitdetail.value() || qbsp_options.omitdetailfence.value()) && detail_fence)
            continue;
        if ((qbsp_options.omitdetail.value() || qbsp_options.omitdetailwall.value()) && detail_wall)
            continue;
        if (qbsp_options.omitdetail.value() && contents.is_any_detail(qbsp_options.target_game))
            continue;

        /* turn solid brushes into detail, if we're in hull0 */
        if (!hullnum.value_or(0) && contents.is_any_solid(qbsp_options.target_game)) {
            if (detail_illusionary) {
                contents = qbsp_options.target_game->create_detail_illusionary_contents(contents);
            } else if (detail_fence) {
                contents = qbsp_options.target_game->create_detail_fence_contents(contents);
            } else if (detail_wall) {
                contents = qbsp_options.target_game->create_detail_wall_contents(contents);
            } else if (detail) {
                contents = qbsp_options.target_game->create_detail_solid_contents(contents);
            }
        }

        /* func_detail_illusionary don't exist in the collision hull
         * (or bspx export) except for Q2, who needs them in there */
        if (hullnum.value_or(0) && detail_illusionary) {
            continue;
        }

        /*
         * "clip" brushes don't show up in the draw hull, but we still want to
         * include them in the model bounds so collision detection works
         * correctly.
         */
        if (hullnum.has_value() && contents.is_clip(qbsp_options.target_game)) {
            if (hullnum.value() == 0) {
                if (auto brush = LoadBrush(src, mapbrush, contents, hullnum, num_clipped)) {
                    dst.bounds += brush->bounds;
                }
                continue;
                // for hull1, 2, etc., convert clip to CONTENTS_SOLID
            } else {
                contents = qbsp_options.target_game->create_solid_contents();
            }
        }

        /* "hint" brushes don't affect the collision hulls */
        if (mapbrush.is_hint) {
            if (hullnum.value_or(0)) {
                continue;
            }
            contents = qbsp_options.target_game->create_empty_contents();
        }

        /* entities in some games never use water merging */
        if (!map.is_world_entity(dst) &&
            !(qbsp_options.target_game->allow_contented_bmodels || qbsp_options.bmodelcontents.value())) {
            // bmodels become solid in Q1

            // to allow use of _mirrorinside, we'll set it to detail fence, which will get remapped back
            // to CONTENTS_SOLID at export. (we wouldn't generate inside faces if the content was CONTENTS_SOLID
            // from the start.)
            contents = qbsp_options.target_game->create_detail_fence_contents(
                qbsp_options.target_game->create_solid_contents());
        }

        if (hullnum.value_or(0)) {
            /* nonsolid brushes don't show up in clipping hulls */
            if (!contents.is_any_solid(qbsp_options.target_game) && !contents.is_sky(qbsp_options.target_game) &&
                !contents.is_fence(qbsp_options.target_game)) {
                continue;
            }

            /* all used brushes are solid in the collision hulls */
            contents = qbsp_options.target_game->create_solid_contents();
        }

        // fixme-brushbsp: function calls above can override the values below
        // so we have to re-set them to be sure they stay what the mapper intended..
        contents.set_mirrored(mapbrush.contents.mirror_inside);
        contents.set_clips_same_type(mapbrush.contents.clips_same_type);

        auto brush = LoadBrush(src, mapbrush, contents, hullnum, num_clipped);

        if (!brush) {
            continue;
        }

        qbsp_options.target_game->count_contents_in_stats(brush->contents, stats);

        dst.bounds += brush->bounds;
        brushes.push_back(bspbrush_t::make_ptr(std::move(*brush)));
    }
}

/*
============
Brush_LoadEntity

hullnum nullopt should contain ALL brushes; BSPX and Quake II, etc.
hullnum 0 does not contain clip brushes.
============
*/
void Brush_LoadEntity(mapentity_t &entity, hull_index_t hullnum, bspbrush_t::container &brushes, size_t &num_clipped)
{
    logging::funcheader();

    bool is_world_entity = map.is_world_entity(entity);

    auto stats = qbsp_options.target_game->create_content_stats();
    logging::percent_clock clock(0);
    clock.displayElapsed = is_world_entity;

    Brush_LoadEntity(entity, entity, hullnum, *stats, brushes, clock, num_clipped);

    /*
     * If this is the world entity, find all func_group and func_detail
     * entities and add their brushes with the appropriate contents flag set.
     */
    if (is_world_entity) {
        /*
         * We no longer care about the order of adding func_detail and func_group,
         * Entity_SortBrushes will sort the brushes
         */
        for (int i = 1; i < map.entities.size(); i++) {
            mapentity_t &source = map.entities.at(i);

            ProcessAreaPortal(source);

            if (IsWorldBrushEntity(source) || IsNonRemoveWorldBrushEntity(source)) {
                Brush_LoadEntity(entity, source, hullnum, *stats, brushes, clock, num_clipped);
            }
        }
    }

    clock.print();

    logging::header("CountBrushes");

    qbsp_options.target_game->print_content_stats(*stats, "brushes");

    logging::stat_tracker_t stat_print;
    auto &visible_sides_stat = stat_print.register_stat("visible sides");
    auto &invisible_sides_stat = stat_print.register_stat("invisible sides");
    auto &sourceless_sides_stat = stat_print.register_stat("sourceless sides");
    for (auto &brush : brushes) {
        for (auto &side : brush->sides) {
            if (!side.source) {
                sourceless_sides_stat.count++;
            } else if (side.source->visible) {
                visible_sides_stat.count++;
            } else {
                invisible_sides_stat.count++;
            }
        }
    }
}

bool bspbrush_t::update_bounds(bool warn_on_failures)
{
    this->bounds = {};

    for (const auto &face : sides) {
        if (face.w) {
            this->bounds.unionWith_in_place(face.w.bounds());
        }
    }

    for (size_t i = 0; i < 3; i++) {
        // todo: map_source_location in bspbrush_t
        if (this->bounds.mins()[i] <= -qbsp_options.worldextent.value() ||
            this->bounds.maxs()[i] >= qbsp_options.worldextent.value()) {
            if (warn_on_failures) {
                logging::print(
                    "WARNING: {}: brush bounds out of range\n", mapbrush ? mapbrush->line : parser_source_location());
            }
            return false;
        }
        if (this->bounds.mins()[i] >= qbsp_options.worldextent.value() ||
            this->bounds.maxs()[i] <= -qbsp_options.worldextent.value()) {
            if (warn_on_failures) {
                logging::print(
                    "WARNING: {}: no visible sides on brush\n", mapbrush ? mapbrush->line : parser_source_location());
            }
            return false;
        }
    }

    this->sphere_origin = (bounds.mins() + bounds.maxs()) / 2.0;
    this->sphere_radius = qv::length((bounds.maxs() - bounds.mins()) / 2.0);

    return true;
}
