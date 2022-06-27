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

#include <cstring>
#include <list>

#include <qbsp/brush.hh>
#include <qbsp/csg4.hh>
#include <qbsp/map.hh>
#include <qbsp/qbsp.hh>

/*
 * Beveled clipping hull can generate many extra faces
 */
constexpr size_t MAX_FACES = 128;
constexpr size_t MAX_HULL_POINTS = 512;
constexpr size_t MAX_HULL_EDGES = 1024;

struct hullbrush_t
{
    const mapbrush_t *srcbrush;
    contentflags_t contents;
    aabb3d bounds;

    std::vector<mapface_t> faces;
    std::vector<qvec3d> points;
    std::vector<qvec3d> corners;
    std::vector<std::tuple<int, int>> edges;

    int linenum;
};

/*
=================
Face_Plane
=================
*/
qplane3d Face_Plane(const face_t *face)
{
    const qplane3d &result = map.planes.at(face->planenum);

    if (face->planeside) {
        return -result;
    }

    return result;
}

qplane3d Face_Plane(const side_t *face)
{
    const qplane3d &result = map.planes.at(face->planenum);

    if (face->planeside) {
        return -result;
    }

    return result;
}

/*
=================
CheckFace

Note: this will not catch 0 area polygons
=================
*/
static void CheckFace(side_t *face, const mapface_t &sourceface)
{
    const qbsp_plane_t &plane = map.planes.at(face->planenum);

    if (face->w.size() < 3) {
        if (face->w.size() == 2) {
            FError("line {}: too few points (2): ({}) ({})\n", sourceface.linenum, face->w[0], face->w[1]);
        } else if (face->w.size() == 1) {
            FError("line {}: too few points (1): ({})\n", sourceface.linenum, face->w[0]);
        } else {
            FError("line {}: too few points ({})", sourceface.linenum, face->w.size());
        }
    }

    qvec3d facenormal = plane.normal;
    if (face->planeside)
        facenormal = -facenormal;

    for (size_t i = 0; i < face->w.size(); i++) {
        const qvec3d &p1 = face->w[i];
        const qvec3d &p2 = face->w[(i + 1) % face->w.size()];

        for (auto &v : p1)
            if (fabs(v) > options.worldextent.value())
                FError("line {}: coordinate out of range ({})", sourceface.linenum, v);

        /* check the point is on the face plane */
        // fixme check: plane's normal is not inverted by planeside check above,
        // is this a bug? should `Face_Plane` be used instead?
        vec_t dist = plane.distance_to(p1);
        if (dist < -options.epsilon.value() || dist > options.epsilon.value())
            logging::print("WARNING: Line {}: Point ({:.3} {:.3} {:.3}) off plane by {:2.4}\n", sourceface.linenum, p1[0],
                p1[1], p1[2], dist);

        /* check the edge isn't degenerate */
        qvec3d edgevec = p2 - p1;
        vec_t length = qv::length(edgevec);
        if (length < options.epsilon.value()) {
            logging::print("WARNING: Line {}: Healing degenerate edge ({}) at ({:.3f} {:.3} {:.3})\n", sourceface.linenum,
                length, p1[0], p1[1], p1[2]);
            for (size_t j = i + 1; j < face->w.size(); j++)
                face->w[j - 1] = face->w[j];
            face->w.resize(face->w.size() - 1);
            CheckFace(face, sourceface);
            break;
        }

        qvec3d edgenormal = qv::normalize(qv::cross(facenormal, edgevec));
        vec_t edgedist = qv::dot(p1, edgenormal);
        edgedist += options.epsilon.value();

        /* all other points must be on front side */
        for (size_t j = 0; j < face->w.size(); j++) {
            if (j == i)
                continue;
            dist = qv::dot(face->w[j], edgenormal);
            if (dist > edgedist)
                FError("line {}: Found a non-convex face (error size {}, point: {})\n", sourceface.linenum,
                    dist - edgedist, face->w[j]);
        }
    }
}

//===========================================================================

static bool NormalizePlane(qbsp_plane_t &p, bool flip = true)
{
    for (size_t i = 0; i < 3; i++) {
        if (p.normal[i] == 1.0) {
            p.normal[(i + 1) % 3] = 0;
            p.normal[(i + 2) % 3] = 0;
            p.type = (i == 0 ? plane_type_t::PLANE_X : i == 1 ? plane_type_t::PLANE_Y : plane_type_t::PLANE_Z);
            return 0; /* no flip */
        }
        if (p.normal[i] == -1.0) {
            if (flip) {
                p.normal[i] = 1.0;
                p.dist = -p.dist;
            }
            p.normal[(i + 1) % 3] = 0;
            p.normal[(i + 2) % 3] = 0;
            p.type = (i == 0 ? plane_type_t::PLANE_X : i == 1 ? plane_type_t::PLANE_Y : plane_type_t::PLANE_Z);
            return 1; /* plane flipped */
        }
    }

    vec_t ax = fabs(p.normal[0]);
    vec_t ay = fabs(p.normal[1]);
    vec_t az = fabs(p.normal[2]);

    size_t nearest;

    if (ax >= ay && ax >= az)
    {
        nearest = 0;
        p.type = plane_type_t::PLANE_ANYX;
    }
    else if (ay >= ax && ay >= az)
    {
        nearest = 1;
        p.type = plane_type_t::PLANE_ANYY;
    }
    else
    {
        nearest = 2;
        p.type = plane_type_t::PLANE_ANYZ;
    }

    if (flip && p.normal[nearest] < 0) {
        p = -p;
        return true; /* plane flipped */
    }

    return false; /* no flip */
}

/* Plane Hashing */

inline int plane_hash_fn(const qplane3d &p)
{
    // FIXME: include normal..?
    return Q_rint(fabs(p.dist));
}

static void PlaneHash_Add(const qplane3d &p, int index)
{
    const int hash = plane_hash_fn(p);
    map.planehash[hash].push_back(index);
}

/*
 * NewPlane
 * - Returns a global plane number and the side that will be the front
 */
static int NewPlane(const qplane3d &plane, planeside_t *side)
{
    vec_t len = qv::length(plane.normal);

    if (len < 1 - options.epsilon.value() || len > 1 + options.epsilon.value())
        FError("invalid normal (vector length {:.4})", len);

    size_t index = map.planes.size();
    qbsp_plane_t &added_plane = map.planes.emplace_back(qbsp_plane_t{plane});

    bool out_flipped = NormalizePlane(added_plane, side != nullptr);

    if (side) {
        *side = out_flipped ? SIDE_BACK : SIDE_FRONT;
    }

    PlaneHash_Add(added_plane, index);

    // add the back side, too
    FindPlane(-plane, nullptr);

    return index;
}

/*
 * FindPlane
 * - Returns a global plane number and the side that will be the front
 * - if `side` is null, only an exact match will be fetched.
 */
int FindPlane(const qplane3d &plane, planeside_t *side)
{
    for (int i : map.planehash[plane_hash_fn(plane)]) {
        const qbsp_plane_t &p = map.planes.at(i);
        if (qv::epsilonEqual(p, plane)) {
            if (side) {
                *side = SIDE_FRONT;
            }
            return i;
        } else if (side && qv::epsilonEqual(-p, plane)) {
            *side = SIDE_BACK;
            return i;
        }
    }
    return NewPlane(plane, side);
}

/*
 * FindPositivePlane
 * - Only used for nodes; always finds a positive matching plane.
 */
int FindPositivePlane(int planenum)
{
    const auto &plane = map.planes[planenum];

    // already positive, or it's PLANE_ANY_x which doesn't matter
    if (plane.type >= plane_type_t::PLANE_ANYX || (plane.normal[0] + plane.normal[1] + plane.normal[2]) > 0) {
        return planenum;
    }

    return FindPlane(-plane, nullptr);
}

int FindPositivePlane(const qplane3d &plane, planeside_t *side)
{
    int planenum = FindPlane(plane, side);
    int positive_plane = FindPositivePlane(planenum);

    if (planenum == positive_plane) {
        return planenum;
    }

    // planenum itself isn't positive, so flip the planeside and return the positive version
    if (side) {
        *side = (*side == SIDE_FRONT) ? SIDE_BACK : SIDE_FRONT;
    }
    return positive_plane;
}

/*
=============================================================================

                        TURN BRUSHES INTO GROUPS OF FACES

=============================================================================
*/

/*
=================
FindTargetEntity
=================
*/
static const mapentity_t *FindTargetEntity(const std::string &target)
{
    for (const auto &entity : map.entities) {
        const std::string &name = entity.epairs.get("targetname");
        if (!string_iequals(target, name))
            return &entity;
    }

    return nullptr;
}

/*
=================
FixRotateOrigin
=================
*/
qvec3d FixRotateOrigin(mapentity_t *entity)
{
    const std::string &search = entity->epairs.get("target");
    const mapentity_t *target = nullptr;

    if (!search.empty()) {
        target = FindTargetEntity(search);
    }

    qvec3d offset;

    if (target) {
        target->epairs.get_vector("origin", offset);
    } else {
        logging::print("WARNING: No target for rotation entity \"{}\"", entity->epairs.get("classname"));
        offset = {};
    }

    entity->epairs.set("origin", qv::to_string(offset));
    return offset;
}

static bool Brush_IsHint(const hullbrush_t &brush)
{
    for (auto &f : brush.faces)
        if (f.flags.is_hint)
            return true;

    return false;
}

static bool MapBrush_IsHint(const mapbrush_t &brush)
{
    for (size_t i = 0; i < brush.numfaces; i++)
        if (brush.face(i).flags.is_hint)
            return true;

    return false;
}

/*
=================
CreateBrushFaces
=================
*/
static std::vector<side_t> CreateBrushFaces(const mapentity_t *src, hullbrush_t *hullbrush, const int hullnum,
    const rotation_t rottype = rotation_t::none, const qvec3d &rotate_offset = {})
{
    vec_t r;
    std::optional<winding_t> w;
    qbsp_plane_t plane;
    std::vector<side_t> facelist;
    qvec3d point;
    vec_t max, min;

    min = VECT_MAX;
    max = -VECT_MAX;

    hullbrush->bounds = {};

    for (auto &mapface : hullbrush->faces) {
        if (hullnum <= 0 && Brush_IsHint(*hullbrush)) {
            /* Don't generate hintskip faces */
            const maptexinfo_t &texinfo = map.mtexinfos.at(mapface.texinfo);

            if (options.target_game->texinfo_is_hintskip(texinfo.flags, map.miptexTextureName(texinfo.miptex)))
                continue;
        }

        w = BaseWindingForPlane(mapface.plane);

        for (auto &mapface2 : hullbrush->faces) {
            if (&mapface == &mapface2)
                continue;
            if (!w)
                break;

            // flip the plane, because we want to keep the back side
            plane = -mapface2.plane;

            w = w->clip(plane, options.epsilon.value(), false)[SIDE_FRONT];
        }

        if (!w) {
            continue; // overconstrained plane
        }

        if (w->size() > MAXEDGES) {
            FError("face->numpoints > MAXEDGES ({}), source face on line {}", MAXEDGES, mapface.linenum);
        }

        // this face is a keeper
        side_t &f = facelist.emplace_back();
        f.planenum = PLANENUM_LEAF;

        f.w.resize(w->size());

        for (size_t j = 0; j < w->size(); j++) {
            for (size_t k = 0; k < 3; k++) {
                point[k] = w->at(j)[k] - rotate_offset[k];
                r = Q_rint(point[k]);
                if (fabs(point[k] - r) < ZERO_EPSILON)
                    f.w[j][k] = r;
                else
                    f.w[j][k] = point[k];

                if (f.w[j][k] < min)
                    min = f.w[j][k];
                if (f.w[j][k] > max)
                    max = f.w[j][k];
            }

            hullbrush->bounds += f.w[j];
        }

        // account for texture offset, from txqbsp-xt
        if (!options.oldrottex.value()) {
            maptexinfo_t texInfoNew = map.mtexinfos.at(mapface.texinfo);
            texInfoNew.outputnum = std::nullopt;

            texInfoNew.vecs.at(0, 3) += qv::dot(rotate_offset, texInfoNew.vecs.row(0).xyz());
            texInfoNew.vecs.at(1, 3) += qv::dot(rotate_offset, texInfoNew.vecs.row(1).xyz());

            mapface.texinfo = FindTexinfo(texInfoNew);
        }

        plane.normal = mapface.plane.normal;
        point = mapface.plane.normal * mapface.plane.dist;
        point -= rotate_offset;
        plane.dist = qv::dot(plane.normal, point);

        f.texinfo = hullnum > 0 ? 0 : mapface.texinfo;
        f.planenum = FindPositivePlane(plane, &f.planeside);

        CheckFace(&f, mapface);
        UpdateFaceSphere(&f);
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
    const bool shouldExpand = (rotate_offset[0] != 0.0 || rotate_offset[1] != 0.0 || rotate_offset[2] != 0.0) &&
                              rottype == rotation_t::hipnotic &&
                              (hullnum >= 0) // hullnum < 0 corresponds to -wrbrushes clipping hulls
                              && options.target_game->id != GAME_HEXEN_II; // never do this in Hexen 2

    if (shouldExpand) {
        vec_t delta = std::max(fabs(max), fabs(min));
        hullbrush->bounds = {-delta, delta};
    }

    return facelist;
}

/*
=====================
FreeBrushes
=====================
*/
void FreeBrushes(mapentity_t *ent)
{
    ent->brushes.clear();
}

/*
==============================================================================

BEVELED CLIPPING HULL GENERATION

This is done by brute force, and could easily get a lot faster if anyone cares.
==============================================================================
*/

/*
============
AddBrushPlane
=============
*/
static void AddBrushPlane(hullbrush_t *hullbrush, const qplane3d &plane)
{
    vec_t len = qv::length(plane.normal);

    if (len < 1.0 - NORMAL_EPSILON || len > 1.0 + NORMAL_EPSILON)
        FError("invalid normal (vector length {:.4})", len);

    for (auto &mapface : hullbrush->faces) {
        if (qv::epsilonEqual(mapface.plane.normal, plane.normal, EQUAL_EPSILON) &&
            fabs(mapface.plane.dist - plane.dist) < options.epsilon.value())
            return;
    }

    if (hullbrush->faces.size() == MAX_FACES)
        FError(
            "brush->faces >= MAX_FACES ({}), source brush on line {}", MAX_FACES, hullbrush->srcbrush->face(0).linenum);

    mapface_t &mapface = hullbrush->faces.emplace_back();
    mapface.plane = {plane};
    mapface.texinfo = 0;
}

/*
============
TestAddPlane

Adds the given plane to the brush description if all of the original brush
vertexes can be put on the front side
=============
*/
static void TestAddPlane(hullbrush_t *hullbrush, qplane3d &plane)
{
    vec_t d;
    int points_front, points_back;

    /* see if the plane has already been added */
    for (auto &mapface : hullbrush->faces) {
        if (qv::epsilonEqual(plane, mapface.plane))
            return;
        if (qv::epsilonEqual(-plane, mapface.plane))
            return;
    }

    /* check all the corner points */
    points_front = 0;
    points_back = 0;

    for (auto &corner : hullbrush->corners) {
        d = plane.distance_to(corner);
        if (d < -options.epsilon.value()) {
            if (points_front)
                return;
            points_back = 1;
        } else if (d > options.epsilon.value()) {
            if (points_back)
                return;
            points_front = 1;
        }
    }

    // the plane is a seperator
    if (points_front) {
        plane = -plane;
    }

    AddBrushPlane(hullbrush, plane);
}

/*
============
AddHullPoint

Doesn't add if duplicated
=============
*/
static int AddHullPoint(hullbrush_t *hullbrush, const qvec3d &p, const aabb3d &hull_size)
{
    for (auto &pt : hullbrush->points)
        if (qv::epsilonEqual(p, pt, EQUAL_EPSILON))
            return &pt - hullbrush->points.data();

    if (hullbrush->points.size() == MAX_HULL_POINTS)
        FError("hullbrush->numpoints == MAX_HULL_POINTS ({}), "
               "source brush on line {}",
            MAX_HULL_POINTS, hullbrush->srcbrush->face(0).linenum);

    int i = hullbrush->points.size();
    hullbrush->points.emplace_back(p);

    for (size_t x = 0; x < 2; x++)
        for (size_t y = 0; y < 2; y++)
            for (size_t z = 0; z < 2; z++) {
                hullbrush->corners.emplace_back(p[0] + hull_size[x][0], p[1] + hull_size[y][1], p[2] + hull_size[z][2]);
            }

    return i;
}

/*
============
AddHullEdge

Creates all of the hull planes around the given edge, if not done allready
=============
*/
static void AddHullEdge(hullbrush_t *hullbrush, const qvec3d &p1, const qvec3d &p2, const aabb3d &hull_size)
{
    int pt1, pt2;
    int a, b, c, d, e;
    qplane3d plane;
    vec_t length;

    pt1 = AddHullPoint(hullbrush, p1, hull_size);
    pt2 = AddHullPoint(hullbrush, p2, hull_size);

    for (auto &edge : hullbrush->edges)
        if ((edge == std::make_tuple(pt1, pt2)) || (edge == std::make_tuple(pt2, pt1)))
            return;

    if (hullbrush->edges.size() == MAX_HULL_EDGES)
        FError("hullbrush->numedges == MAX_HULL_EDGES ({}), "
               "source brush on line {}",
            MAX_HULL_EDGES, hullbrush->srcbrush->face(0).linenum);

    hullbrush->edges.emplace_back(pt1, pt2);

    qvec3d edgevec = qv::normalize(p1 - p2);

    for (a = 0; a < 3; a++) {
        b = (a + 1) % 3;
        c = (a + 2) % 3;

        qvec3d planevec;
        planevec[a] = 1;
        planevec[b] = 0;
        planevec[c] = 0;
        plane.normal = qv::cross(planevec, edgevec);
        length = qv::normalizeInPlace(plane.normal);

        /* If this edge is almost parallel to the hull edge, skip it. */
        if (length < ANGLEEPSILON)
            continue;

        for (d = 0; d <= 1; d++) {
            for (e = 0; e <= 1; e++) {
                qvec3d planeorg = p1;
                planeorg[b] += hull_size[d][b];
                planeorg[c] += hull_size[e][c];
                plane.dist = qv::dot(planeorg, plane.normal);
                TestAddPlane(hullbrush, plane);
            }
        }
    }
}

/*
============
ExpandBrush
=============
*/
static void ExpandBrush(hullbrush_t *hullbrush, const aabb3d &hull_size, std::vector<side_t> &facelist)
{
    int x, s;
    qbsp_plane_t plane;
    int cBevEdge = 0;

    hullbrush->points.clear();
    hullbrush->corners.clear();
    hullbrush->edges.clear();

    // create all the hull points
    for (auto &f : facelist)
        for (size_t i = 0; i < f.w.size(); i++) {
            AddHullPoint(hullbrush, f.w[i], hull_size);
            cBevEdge++;
        }

    // expand all of the planes
    for (auto &mapface : hullbrush->faces) {
        if (mapface.flags.no_expand)
            continue;
        qvec3d corner{};
        for (x = 0; x < 3; x++) {
            if (mapface.plane.normal[x] > 0)
                corner[x] = hull_size[1][x];
            else if (mapface.plane.normal[x] < 0)
                corner[x] = hull_size[0][x];
        }
        mapface.plane.dist += qv::dot(corner, mapface.plane.normal);
    }

    // add any axis planes not contained in the brush to bevel off corners
    for (x = 0; x < 3; x++)
        for (s = -1; s <= 1; s += 2) {
            // add the plane
            plane.normal = {};
            plane.normal[x] = (vec_t)s;
            if (s == -1)
                plane.dist = -hullbrush->bounds.mins()[x] + -hull_size[0][x];
            else
                plane.dist = hullbrush->bounds.maxs()[x] + hull_size[1][x];
            AddBrushPlane(hullbrush, plane);
        }

    // add all of the edge bevels
    for (auto &f : facelist)
        for (size_t i = 0; i < f.w.size(); i++)
            AddHullEdge(hullbrush, f.w[i], f.w[(i + 1) % f.w.size()], hull_size);
}

//============================================================================

static contentflags_t Brush_GetContents(const mapbrush_t *mapbrush)
{
    bool base_contents_set = false;
    contentflags_t base_contents = options.target_game->create_empty_contents();

    // validate that all of the sides have valid contents
    for (int i = 0; i < mapbrush->numfaces; i++) {
        const mapface_t &mapface = mapbrush->face(i);
        const maptexinfo_t &texinfo = map.mtexinfos.at(mapface.texinfo);

        contentflags_t contents =
            options.target_game->face_get_contents(mapface.texname.data(), texinfo.flags, mapface.contents);

        if (contents.is_empty(options.target_game)) {
            continue;
        }

        // use the first non-empty as the base contents value
        if (!base_contents_set) {
            base_contents_set = true;
            base_contents = contents;
        }

        if (!contents.types_equal(base_contents, options.target_game)) {
            logging::print("mixed face contents ({} != {}) at line {}\n", base_contents.to_string(options.target_game),
                contents.to_string(options.target_game), mapface.linenum);
            break;
        }
    }

    // make sure we found a valid type
    Q_assert(base_contents.is_valid(options.target_game, false));

    return base_contents;
}

/*
===============
LoadBrush

Converts a mapbrush to a bsp brush
===============
*/
std::optional<bspbrush_t> LoadBrush(const mapentity_t *src, const mapbrush_t *mapbrush, const contentflags_t &contents,
    const qvec3d &rotate_offset, const rotation_t rottype, const int hullnum)
{
    hullbrush_t hullbrush;
    std::vector<side_t> facelist;

    // create the faces

    hullbrush.linenum = mapbrush->face(0).linenum;
    if (mapbrush->numfaces > MAX_FACES)
        FError("brush->faces >= MAX_FACES ({}), source brush on line {}", MAX_FACES, hullbrush.linenum);

    hullbrush.contents = contents;
    hullbrush.srcbrush = mapbrush;
    hullbrush.faces.reserve(mapbrush->numfaces);
    for (int i = 0; i < mapbrush->numfaces; i++)
        hullbrush.faces.emplace_back(mapbrush->face(i));

    if (hullnum <= 0) {
        // for hull 0 or BSPX -wrbrushes collision, apply the rotation offset now
        facelist = CreateBrushFaces(src, &hullbrush, hullnum, rottype, rotate_offset);
    } else {
        // for Quake-style clipping hulls, don't apply rotation offset yet..
        // it will be applied below
        facelist = CreateBrushFaces(src, &hullbrush, hullnum);
    }

    if (facelist.empty()) {
        logging::print("WARNING: Couldn't create brush faces\n");
        logging::print("^ brush at line {} of .map file\n", hullbrush.linenum);
        return std::nullopt;
    }

    if (hullnum > 0) {
        auto &hulls = options.target_game->get_hull_sizes();
        Q_assert(hullnum < hulls.size());
        ExpandBrush(&hullbrush, *(hulls.begin() + hullnum), facelist);
        facelist = CreateBrushFaces(src, &hullbrush, hullnum, rottype, rotate_offset);
    }

    // create the brush
    bspbrush_t brush{};
    brush.contents = contents;
    brush.sides = std::move(facelist);
    brush.bounds = hullbrush.bounds;
    return brush;
}

//=============================================================================

static void Brush_LoadEntity(mapentity_t *dst, const mapentity_t *src, const int hullnum, std::any &stats)
{
    const mapbrush_t *mapbrush;
    qvec3d rotate_offset{};
    int i;
    int lmshift;
    bool all_detail, all_detail_fence, all_detail_illusionary;

    const std::string &classname = src->epairs.get("classname");
    /* Origin brush support */
    rotation_t rottype = rotation_t::none;

    for (int i = 0; i < src->nummapbrushes; i++) {
        const mapbrush_t *mapbrush = &src->mapbrush(i);
        const contentflags_t contents = Brush_GetContents(mapbrush);
        if (contents.is_origin(options.target_game)) {
            if (dst == map.world_entity()) {
                logging::print("WARNING: Ignoring origin brush in worldspawn\n");
                continue;
            }

            std::optional<bspbrush_t> brush = LoadBrush(src, mapbrush, contents, {}, rotation_t::none, 0);

            if (brush) {
                rotate_offset = brush->bounds.centroid();

                dst->epairs.set("origin", qv::to_string(rotate_offset));

                rottype = rotation_t::origin_brush;
            }
        }
    }

    /* Hipnotic rotation */
    if (rottype == rotation_t::none) {
        if (!Q_strncasecmp(classname, "rotate_", 7)) {
            rotate_offset = FixRotateOrigin(dst);
            rottype = rotation_t::hipnotic;
        }
    }

    /* If the source entity is func_detail, set the content flag */
    if (!options.nodetail.value()) {
        all_detail = false;
        if (!Q_strcasecmp(classname, "func_detail")) {
            all_detail = true;
        }

        all_detail_fence = false;
        if (!Q_strcasecmp(classname, "func_detail_fence") || !Q_strcasecmp(classname, "func_detail_wall")) {
            all_detail_fence = true;
        }

        all_detail_illusionary = false;
        if (!Q_strcasecmp(classname, "func_detail_illusionary")) {
            all_detail_illusionary = true;
        }
    }

    /* entities with custom lmscales are important for the qbsp to know about */
    i = 16 * src->epairs.get_int("_lmscale");
    if (!i)
        i = 16; // if 0, pick a suitable default
    lmshift = 0;
    while (i > 1) {
        lmshift++; // only allow power-of-two scales
        i /= 2;
    }

    /* _mirrorinside key (for func_water etc.) */
    std::optional<bool> mirrorinside;

    if (src->epairs.has("_mirrorinside")) {
        mirrorinside = src->epairs.get_int("_mirrorinside") ? true : false;
    }

    /* _noclipfaces */
    std::optional<bool> clipsametype;

    if (src->epairs.has("_noclipfaces")) {
        clipsametype = static_cast<bool>(src->epairs.get_int("_noclipfaces"));
    }

    const bool func_illusionary_visblocker = (0 == Q_strcasecmp(classname, "func_illusionary_visblocker"));

    // _omitbrushes 1 just discards all brushes in the entity.
    // could be useful for geometry guides, selective compilation, etc.
    if (src->epairs.get_int("_omitbrushes"))
        return;

    for (i = 0; i < src->nummapbrushes; i++, mapbrush++) {
        logging::percent(i, src->nummapbrushes);
        mapbrush = &src->mapbrush(i);
        contentflags_t contents = Brush_GetContents(mapbrush);

        // per-brush settings
        bool detail = false;
        bool detail_illusionary = false;
        bool detail_fence = false;

        // inherit the per-entity settings
        detail |= all_detail;
        detail_illusionary |= all_detail_illusionary;
        detail_fence |= all_detail_fence;

        /* "origin" brushes always discarded */
        if (contents.is_origin(options.target_game))
            continue;

        /* -omitdetail option omits all types of detail */
        if (options.omitdetail.value() && detail)
            continue;
        if ((options.omitdetail.value() || options.omitdetailillusionary.value()) && detail_illusionary)
            continue;
        if ((options.omitdetail.value() || options.omitdetailfence.value()) && detail_fence)
            continue;

        /* turn solid brushes into detail, if we're in hull0 */
        if (hullnum <= 0 && contents.is_solid(options.target_game)) {
            if (detail_illusionary) {
                contents = options.target_game->create_detail_illusionary_contents(contents);
            } else if (detail_fence) {
                contents = options.target_game->create_detail_fence_contents(contents);
            } else if (detail) {
                contents = options.target_game->create_detail_solid_contents(contents);
            }
        }

        /* func_detail_illusionary don't exist in the collision hull
         * (or bspx export) except for Q2, who needs them in there */
        if (hullnum > 0 && detail_illusionary) {
            continue;
        }

        /*
         * "clip" brushes don't show up in the draw hull, but we still want to
         * include them in the model bounds so collision detection works
         * correctly.
         */
        if (hullnum != HULL_COLLISION && contents.is_clip(options.target_game)) {
            if (hullnum == 0) {
                std::optional<bspbrush_t> brush = LoadBrush(src, mapbrush, contents, rotate_offset, rottype, hullnum);

                if (brush) {
                    dst->bounds += brush->bounds;
                }

                continue;
                // for hull1, 2, etc., convert clip to CONTENTS_SOLID
            } else {
                contents = options.target_game->create_solid_contents();
            }
        }

        /* "hint" brushes don't affect the collision hulls */
        if (MapBrush_IsHint(*mapbrush)) {
            if (hullnum > 0)
                continue;
            contents = options.target_game->create_empty_contents();
        }

        /* entities in some games never use water merging */
        if (dst != map.world_entity() && !options.target_game->allow_contented_bmodels) {
            contents = options.target_game->create_solid_contents();

            /* Hack to turn bmodels with "_mirrorinside" into func_detail_fence in hull 0.
                this is to allow "_mirrorinside" to work on func_illusionary, func_wall, etc.
                Otherwise they would be CONTENTS_SOLID and the inside faces would be deleted.

                It's CONTENTS_DETAIL_FENCE because this gets mapped to CONTENTS_SOLID just
                before writing the bsp, and bmodels normally have CONTENTS_SOLID as their
                contents type.
                */
            if (hullnum <= 0 && mirrorinside.value_or(false)) {
                contents = options.target_game->create_detail_fence_contents(contents);
            }
        }

        /* nonsolid brushes don't show up in clipping hulls */
        if (hullnum > 0 && !contents.is_solid(options.target_game) && !contents.is_sky(options.target_game))
            continue;

        /* sky brushes are solid in the collision hulls */
        if (hullnum > 0 && contents.is_sky(options.target_game))
            contents = options.target_game->create_solid_contents();

        // apply extended flags
        contents.set_mirrored(mirrorinside);
        contents.set_clips_same_type(clipsametype);
        contents.illusionary_visblocker = func_illusionary_visblocker;

        std::optional<bspbrush_t> brush = LoadBrush(src, mapbrush, contents, rotate_offset, rottype, hullnum);
        if (!brush)
            continue;

        brush->lmshift = lmshift;

        for (auto &face : brush->sides)
            face.lmshift = lmshift;

        if (classname == std::string_view("func_areaportal")) {
            brush->func_areaportal = const_cast<mapentity_t *>(src); // FIXME: get rid of consts on src in the callers?
        }

        options.target_game->count_contents_in_stats(brush->contents, stats);
        dst->brushes.push_back(std::make_unique<bspbrush_t>(brush.value()));
        dst->bounds += brush->bounds;
    }

    logging::percent(src->nummapbrushes, src->nummapbrushes, src == map.world_entity());
}

/*
============
Brush_LoadEntity

hullnum HULL_COLLISION should contain ALL brushes. (used by BSPX_CreateBrushList())
hullnum 0 does not contain clip brushes.
============
*/
void Brush_LoadEntity(mapentity_t *entity, const int hullnum)
{
    std::any stats = options.target_game->create_content_stats();

    Brush_LoadEntity(entity, entity, hullnum, stats);

    /*
     * If this is the world entity, find all func_group and func_detail
     * entities and add their brushes with the appropriate contents flag set.
     */
    if (entity == map.world_entity()) {
        /*
         * We no longer care about the order of adding func_detail and func_group,
         * Entity_SortBrushes will sort the brushes
         */
        for (int i = 1; i < map.entities.size(); i++) {
            mapentity_t *source = &map.entities.at(i);

            /* Load external .map and change the classname, if needed */
            ProcessExternalMapEntity(source);

            ProcessAreaPortal(source);

            if (IsWorldBrushEntity(source) || IsNonRemoveWorldBrushEntity(source)) {
                Brush_LoadEntity(entity, source, hullnum, stats);
            }
        }
    }

    options.target_game->print_content_stats(stats, "brushes");
}

void bspbrush_t::update_bounds()
{
    this->bounds = {};
    for (const auto &face : sides) {
        this->bounds = this->bounds.unionWith(face.w.bounds());
    }
}
