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

#include <string.h>

#include <qbsp/qbsp.hh>

/*
 * Beveled clipping hull can generate many extra faces
 */
#define MAX_FACES 128
#define MAX_HULL_POINTS 512
#define MAX_HULL_EDGES 1024

typedef struct hullbrush_s {
    const mapbrush_t *srcbrush;
    int contents;
    int numfaces;
    vec3_t mins;
    vec3_t maxs;
    mapface_t faces[MAX_FACES];

    int numpoints;
    int numedges;
    vec3_t points[MAX_HULL_POINTS];
    vec3_t corners[MAX_HULL_POINTS * 8];
    int edges[MAX_HULL_EDGES][2];

    int linenum;
} hullbrush_t;

/*
=================
Face_Plane
=================
*/
plane_t
Face_Plane(const face_t *face)
{
    const qbsp_plane_t *plane = &map.planes.at(face->planenum);
    plane_t result;
    
    result.dist = plane->dist;
    VectorCopy(plane->normal, result.normal);
    
    if (face->planeside) {
        VectorScale(result.normal, -1.0, result.normal);
        result.dist = -result.dist;
    }
    
    return result;
}

/*
=================
CheckFace

Note: this will not catch 0 area polygons
=================
*/
void
CheckFace(face_t *face, const mapface_t *sourceface)
{
    const qbsp_plane_t *plane = &map.planes[face->planenum];
    const vec_t *p1, *p2;
    vec_t length, dist, edgedist;
    vec3_t edgevec, edgenormal, facenormal;
    int i, j;

    if (face->w.numpoints < 3) {
        if (face->w.numpoints == 2) {
            Error("%s: line %d: too few points (2): (%f %f %f) (%f %f %f)\n", __func__, sourceface->linenum,
                  face->w.points[0][0], face->w.points[0][1], face->w.points[0][2],
                  face->w.points[1][0], face->w.points[1][1], face->w.points[1][2]);
        } else if (face->w.numpoints == 1) {
            Error("%s: line %d: too few points (1): (%f %f %f)\n", __func__, sourceface->linenum,
                  face->w.points[0][0], face->w.points[0][1], face->w.points[0][2]);
        } else {
            Error("%s: line %d: too few points (%d)", __func__, sourceface->linenum, face->w.numpoints);
        }
    }

    VectorCopy(plane->normal, facenormal);
    if (face->planeside)
        VectorSubtract(vec3_origin, facenormal, facenormal);

    for (i = 0; i < face->w.numpoints; i++) {
        p1 = face->w.points[i];
        p2 = face->w.points[(i + 1) % face->w.numpoints];

        for (j = 0; j < 3; j++)
            if (p1[j] > options.worldExtent || p1[j] < -options.worldExtent)
                Error("%s: line %d: coordinate out of range (%f)", __func__, sourceface->linenum, p1[j]);

        /* check the point is on the face plane */
        dist = DotProduct(p1, plane->normal) - plane->dist;
        if (dist < -ON_EPSILON || dist > ON_EPSILON)
            Message(msgWarning, warnPointOffPlane, sourceface->linenum, p1[0], p1[1], p1[2], dist);

        /* check the edge isn't degenerate */
        VectorSubtract(p2, p1, edgevec);
        length = VectorLength(edgevec);
        if (length < ON_EPSILON) {
            Message(msgWarning, warnDegenerateEdge, sourceface->linenum, length, p1[0], p1[1], p1[2]);
            for (j = i + 1; j < face->w.numpoints; j++)
                VectorCopy(face->w.points[j], face->w.points[j - 1]);
            face->w.numpoints--;
            CheckFace(face, sourceface);
            break;
        }

        CrossProduct(facenormal, edgevec, edgenormal);
        VectorNormalize(edgenormal);
        edgedist = DotProduct(p1, edgenormal);
        edgedist += ON_EPSILON;

        /* all other points must be on front side */
        for (j = 0; j < face->w.numpoints; j++) {
            if (j == i)
                continue;
            dist = DotProduct(face->w.points[j], edgenormal);
            if (dist > edgedist)
                Error("%s: line %d: Found a non-convex face (error size %f, point: %f %f %f)\n",
                      __func__, sourceface->linenum, dist - edgedist, face->w.points[j][0], face->w.points[j][1], face->w.points[j][2]);
        }
    }
}


//===========================================================================

/*
=================
AddToBounds
=================
*/
static void
AddToBounds(mapentity_t *entity, const vec3_t point)
{
    int i;

    for (i = 0; i < 3; i++) {
        if (point[i] < entity->mins[i])
            entity->mins[i] = point[i];
        if (point[i] > entity->maxs[i])
            entity->maxs[i] = point[i];
    }
}

//===========================================================================

static int
NormalizePlane(qbsp_plane_t *p)
{
    int i;
    vec_t ax, ay, az;

    p->outputplanenum = -1;
    
    for (i = 0; i < 3; i++) {
        if (p->normal[i] == 1.0) {
            p->normal[(i + 1) % 3] = 0;
            p->normal[(i + 2) % 3] = 0;
            p->type = PLANE_X + i;
            return 0; /* no flip */
        }
        if (p->normal[i] == -1.0) {
            p->normal[i] = 1.0;
            p->normal[(i + 1) % 3] = 0;
            p->normal[(i + 2) % 3] = 0;
            p->dist = -p->dist;
            p->type = PLANE_X + i;
            return 1; /* plane flipped */
        }
    }

    ax = fabs(p->normal[0]);
    ay = fabs(p->normal[1]);
    az = fabs(p->normal[2]);

    if (ax >= ay && ax >= az)
        p->type = PLANE_ANYX;
    else if (ay >= ax && ay >= az)
        p->type = PLANE_ANYY;
    else
        p->type = PLANE_ANYZ;

    if (p->normal[p->type - PLANE_ANYX] < 0) {
        VectorSubtract(vec3_origin, p->normal, p->normal);
        p->dist = -p->dist;
        return 1; /* plane flipped */
    }
    return 0; /* no flip */
}


bool
PlaneEqual(const qbsp_plane_t *p1, const qbsp_plane_t *p2)
{
    return (fabs(p1->normal[0] - p2->normal[0]) < NORMAL_EPSILON &&
            fabs(p1->normal[1] - p2->normal[1]) < NORMAL_EPSILON &&
            fabs(p1->normal[2] - p2->normal[2]) < NORMAL_EPSILON &&
            fabs(p1->dist - p2->dist) < DIST_EPSILON);
}

bool
PlaneInvEqual(const qbsp_plane_t *p1, const qbsp_plane_t *p2)
{
    qbsp_plane_t temp = {0};
    VectorScale(p1->normal, -1.0, temp.normal);
    temp.dist = -p1->dist;
    return PlaneEqual(&temp, p2);
}

/* Plane Hashing */

static inline int
plane_hash_fn(const qbsp_plane_t *p)
{
    return Q_rint(fabs(p->dist));
}

static void
PlaneHash_Add(const qbsp_plane_t *p, int index)
{
    const int hash = plane_hash_fn(p);
    map.planehash[hash].push_back(index);
}

/*
 * NewPlane
 * - Returns a global plane number and the side that will be the front
 */
static int
NewPlane(const vec3_t normal, const vec_t dist, int *side)
{
    vec_t len;

    len = VectorLength(normal);
    if (len < 1 - ON_EPSILON || len > 1 + ON_EPSILON)
        Error("%s: invalid normal (vector length %.4f)", __func__, len);

    qbsp_plane_t plane;
    VectorCopy(normal, plane.normal);
    plane.dist = dist;
    *side = NormalizePlane(&plane) ? SIDE_BACK : SIDE_FRONT;
    
    int index = map.planes.size();
    map.planes.push_back(plane);
    PlaneHash_Add(&plane, index);
    return index;
}

/*
 * FindPlane
 * - Returns a global plane number and the side that will be the front
 */
int
FindPlane(const vec3_t normal, const vec_t dist, int *side)
{
    qbsp_plane_t plane = {0};
    VectorCopy(normal, plane.normal);
    plane.dist = dist;
    
    for (int i : map.planehash[plane_hash_fn(&plane)]) {
        const qbsp_plane_t &p = map.planes.at(i);
        if (PlaneEqual(&p, &plane)) {
            *side = SIDE_FRONT;
            return i;
        } else if (PlaneInvEqual(&p, &plane)) {
            *side = SIDE_BACK;
            return i;
        }
    }
    return NewPlane(plane.normal, plane.dist, side);
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
static const mapentity_t *
FindTargetEntity(const char *target)
{
    int i;
    const char *name;
    const mapentity_t *entity;

    for (i = 0; i < map.numentities(); i++) {
        entity = &map.entities.at(i);
        name = ValueForKey(entity, "targetname");
        if (!Q_strcasecmp(target, name))
            return entity;
    }

    return NULL;
}


/*
=================
FixRotateOrigin
=================
*/
void
FixRotateOrigin(mapentity_t *entity)
{
    const mapentity_t *target = NULL;
    const char *search;
    vec3_t offset;
    char value[20];

    search = ValueForKey(entity, "target");
    if (search[0])
        target = FindTargetEntity(search);

    if (target) {
        GetVectorForKey(target, "origin", offset);
    } else {
        search = ValueForKey(entity, "classname");
        Message(msgWarning, warnNoRotateTarget, search);
        VectorCopy(vec3_origin, offset);
    }

    q_snprintf(value, sizeof(value), "%d %d %d", (int)offset[0],
             (int)offset[1], (int)offset[2]);
    SetKeyValue(entity, "origin", value);
}


/*
=================
CreateBrushFaces
=================
*/
static face_t *
CreateBrushFaces(const mapentity_t *src, hullbrush_t *hullbrush, 
                 const vec3_t rotate_offset, const rotation_t rottype, const int hullnum)
{
    int i, j, k;
    vec_t r;
    face_t *f;
    winding_t *w;
    qbsp_plane_t plane;
    face_t *facelist = NULL;
    mapface_t *mapface, *mapface2;
    vec3_t point;
    vec_t max, min;

    min = VECT_MAX;
    max = -VECT_MAX;
    for (i = 0; i < 3; i++) {
        hullbrush->mins[i] = VECT_MAX;
        hullbrush->maxs[i] = -VECT_MAX;
    }

    mapface = hullbrush->faces;
    for (i = 0; i < hullbrush->numfaces; i++, mapface++) {
        if (!hullnum && hullbrush->contents == CONTENTS_HINT) {
            /* Don't generate hintskip faces */
            const mtexinfo_t &texinfo = map.mtexinfos.at(mapface->texinfo);
            const char *texname = map.miptex.at(texinfo.miptex).c_str();

            if (Q_strcasecmp(texname, "hint"))
                continue; // anything texname other than "hint" in a hint brush is treated as "hintskip", and discarded
        }

        w = BaseWindingForPlane(&mapface->plane);
        mapface2 = hullbrush->faces;
        for (j = 0; j < hullbrush->numfaces && w; j++, mapface2++) {
            if (j == i)
                continue;
            // flip the plane, because we want to keep the back side
            VectorSubtract(vec3_origin, mapface2->plane.normal, plane.normal);
            plane.dist = -mapface2->plane.dist;

            w = ClipWinding(w, &plane, false);
        }
        if (!w)
            continue;           // overconstrained plane

        // this face is a keeper
        f = (face_t *)AllocMem(FACE, 1, true);
        f->w.numpoints = w->numpoints;
        if (f->w.numpoints > MAXEDGES)
            Error("face->numpoints > MAXEDGES (%d), source face on line %d",
                  MAXEDGES, mapface->linenum);

        for (j = 0; j < w->numpoints; j++) {
            for (k = 0; k < 3; k++) {
                point[k] = w->points[j][k] - rotate_offset[k];
                r = Q_rint(point[k]);
                if (fabs(point[k] - r) < ZERO_EPSILON)
                    f->w.points[j][k] = r;
                else
                    f->w.points[j][k] = point[k];

                if (f->w.points[j][k] < hullbrush->mins[k])
                    hullbrush->mins[k] = f->w.points[j][k];
                if (f->w.points[j][k] > hullbrush->maxs[k])
                    hullbrush->maxs[k] = f->w.points[j][k];
                if (f->w.points[j][k] < min)
                    min = f->w.points[j][k];
                if (f->w.points[j][k] > max)
                    max = f->w.points[j][k];
            }
        }

        // account for texture offset, from txqbsp-xt
        if (options.fixRotateObjTexture) {
            const mtexinfo_t &texinfo = map.mtexinfos.at(mapface->texinfo);
            mtexinfo_t texInfoNew;
            vec3_t vecs[2];
            int k, l;

            memcpy(&texInfoNew, &texinfo, sizeof(texInfoNew));
            for (k=0; k<2; k++) {
                for (l=0; l<3; l++) {
                    vecs[k][l] = texinfo.vecs[k][l];
                }
            }

            texInfoNew.vecs[0][3] += DotProduct( rotate_offset, vecs[0] );
            texInfoNew.vecs[1][3] += DotProduct( rotate_offset, vecs[1] );

            mapface->texinfo = FindTexinfo( &texInfoNew, texInfoNew.flags );
        }

        VectorCopy(mapface->plane.normal, plane.normal);
        VectorScale(mapface->plane.normal, mapface->plane.dist, point);
        VectorSubtract(point, rotate_offset, point);
        plane.dist = DotProduct(plane.normal, point);

        FreeMem(w, WINDING, 1);

        f->texinfo = hullnum ? 0 : mapface->texinfo;
        f->planenum = FindPlane(plane.normal, plane.dist, &f->planeside);
        f->next = facelist;
        facelist = f;
        CheckFace(f, mapface);
        UpdateFaceSphere(f);
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
    const bool shouldExpand = 
           (rotate_offset[0] != 0.0 || rotate_offset[1] != 0.0 || rotate_offset[2] != 0.0)
        && rottype == rotation_t::hipnotic
        && (hullnum >= 0) // hullnum < 0 corresponds to -wrbrushes clipping hulls
        && !options.hexen2; // never do this in Hexen 2

    if (shouldExpand) {
        vec_t delta;

        delta = fabs(max);
        if (fabs(min) > delta)
            delta = fabs(min);

        for (k = 0; k < 3; k++) {
            hullbrush->mins[k] = -delta;
            hullbrush->maxs[k] = delta;
        }
    }

    return facelist;
}


/*
=================
FreeBrushFaces
=================
*/
static void
FreeBrushFaces(face_t *facelist)
{
    face_t *face, *next;

    for (face = facelist; face; face = next) {
        next = face->next;
        FreeMem(face, FACE, 1);
    }
}


/*
=====================
FreeBrushes
=====================
*/
void
FreeBrushes(mapentity_t *ent)
{
    brush_t *brush, *next;

    for (brush = ent->brushes; brush; brush = next) {
        next = brush->next;
        FreeBrush(brush);
    }
    ent->brushes = nullptr;
}

/*
=====================
FreeBrush
=====================
*/
void
FreeBrush(brush_t *brush)
{
    FreeBrushFaces(brush->faces);
    FreeMem(brush, BRUSH, 1);
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
static void
AddBrushPlane(hullbrush_t *hullbrush, qbsp_plane_t *plane)
{
    int i;
    mapface_t *mapface;
    vec_t len;

    len = VectorLength(plane->normal);
    if (len < 1.0 - NORMAL_EPSILON || len > 1.0 + NORMAL_EPSILON)
        Error("%s: invalid normal (vector length %.4f)", __func__, len);

    mapface = hullbrush->faces;
    for (i = 0; i < hullbrush->numfaces; i++, mapface++) {
        if (VectorCompare(mapface->plane.normal, plane->normal, EQUAL_EPSILON) &&
            fabs(mapface->plane.dist - plane->dist) < ON_EPSILON)
            return;
    }
    if (hullbrush->numfaces == MAX_FACES)
        Error("brush->faces >= MAX_FACES (%d), source brush on line %d",
              MAX_FACES, hullbrush->srcbrush->face(0).linenum);

    mapface->plane = *plane;
    mapface->texinfo = 0;
    hullbrush->numfaces++;
}


/*
============
TestAddPlane

Adds the given plane to the brush description if all of the original brush
vertexes can be put on the front side
=============
*/
static void
TestAddPlane(hullbrush_t *hullbrush, qbsp_plane_t *plane)
{
    int i, c;
    vec_t d;
    mapface_t *mapface;
    vec_t *corner;
    qbsp_plane_t flip;
    int points_front, points_back;

    /* see if the plane has already been added */
    mapface = hullbrush->faces;
    for (i = 0; i < hullbrush->numfaces; i++, mapface++) {
        if (PlaneEqual(plane, &mapface->plane))
            return;
        if (PlaneInvEqual(plane, &mapface->plane))
            return;
    }

    /* check all the corner points */
    points_front = 0;
    points_back = 0;

    corner = hullbrush->corners[0];
    c = hullbrush->numpoints * 8;

    for (i = 0; i < c; i++, corner += 3) {
        d = DotProduct(corner, plane->normal) - plane->dist;
        if (d < -ON_EPSILON) {
            if (points_front)
                return;
            points_back = 1;
        } else if (d > ON_EPSILON) {
            if (points_back)
                return;
            points_front = 1;
        }
    }

    // the plane is a seperator
    if (points_front) {
        VectorSubtract(vec3_origin, plane->normal, flip.normal);
        flip.dist = -plane->dist;
        plane = &flip;
    }

    AddBrushPlane(hullbrush, plane);
}

/*
============
AddHullPoint

Doesn't add if duplicated
=============
*/
static int
AddHullPoint(hullbrush_t *hullbrush, vec3_t p, vec3_t hull_size[2])
{
    int i;
    vec_t *c;
    int x, y, z;

    for (i = 0; i < hullbrush->numpoints; i++)
        if (VectorCompare(p, hullbrush->points[i], EQUAL_EPSILON))
            return i;

    if (hullbrush->numpoints == MAX_HULL_POINTS)
        Error("hullbrush->numpoints == MAX_HULL_POINTS (%d), "
              "source brush on line %d",
              MAX_HULL_POINTS, hullbrush->srcbrush->face(0).linenum);

    VectorCopy(p, hullbrush->points[hullbrush->numpoints]);

    c = hullbrush->corners[i * 8];

    for (x = 0; x < 2; x++)
        for (y = 0; y < 2; y++)
            for (z = 0; z < 2; z++) {
                c[0] = p[0] + hull_size[x][0];
                c[1] = p[1] + hull_size[y][1];
                c[2] = p[2] + hull_size[z][2];
                c += 3;
            }

    hullbrush->numpoints++;

    return i;
}


/*
============
AddHullEdge

Creates all of the hull planes around the given edge, if not done allready
=============
*/
static void
AddHullEdge(hullbrush_t *hullbrush, vec3_t p1, vec3_t p2, vec3_t hull_size[2])
{
    int pt1, pt2;
    int i;
    int a, b, c, d, e;
    vec3_t edgevec, planeorg, planevec;
    qbsp_plane_t plane;
    vec_t length;

    pt1 = AddHullPoint(hullbrush, p1, hull_size);
    pt2 = AddHullPoint(hullbrush, p2, hull_size);

    for (i = 0; i < hullbrush->numedges; i++)
        if ((hullbrush->edges[i][0] == pt1 && hullbrush->edges[i][1] == pt2)
            || (hullbrush->edges[i][0] == pt2 && hullbrush->edges[i][1] == pt1))
            return;

    if (hullbrush->numedges == MAX_HULL_EDGES)
        Error("hullbrush->numedges == MAX_HULL_EDGES (%d), "
              "source brush on line %d",
              MAX_HULL_EDGES, hullbrush->srcbrush->face(0).linenum);

    hullbrush->edges[i][0] = pt1;
    hullbrush->edges[i][1] = pt2;
    hullbrush->numedges++;

    VectorSubtract(p1, p2, edgevec);
    VectorNormalize(edgevec);

    for (a = 0; a < 3; a++) {
        b = (a + 1) % 3;
        c = (a + 2) % 3;

        planevec[a] = 1;
        planevec[b] = 0;
        planevec[c] = 0;
        CrossProduct(planevec, edgevec, plane.normal);
        length = VectorLength(plane.normal);

        /* If this edge is almost parallel to the hull edge, skip it. */
        if (length < ANGLEEPSILON)
            continue;

        VectorScale(plane.normal, 1.0 / length, plane.normal);
        for (d = 0; d <= 1; d++) {
            for (e = 0; e <= 1; e++) {
                VectorCopy(p1, planeorg);
                planeorg[b] += hull_size[d][b];
                planeorg[c] += hull_size[e][c];
                plane.dist = DotProduct(planeorg, plane.normal);
                TestAddPlane(hullbrush, &plane);
            }
        }
    }
}


/*
============
ExpandBrush
=============
*/
static void
ExpandBrush(hullbrush_t *hullbrush, vec3_t hull_size[2], face_t *facelist)
{
    int i, x, s;
    vec3_t corner;
    face_t *f;
    qbsp_plane_t plane;
    mapface_t *mapface;
    int cBevEdge = 0;

    hullbrush->numpoints = 0;
    hullbrush->numedges = 0;

    // create all the hull points
    for (f = facelist; f; f = f->next)
        for (i = 0; i < f->w.numpoints; i++) {
            AddHullPoint(hullbrush, f->w.points[i], hull_size);
            cBevEdge++;
        }

    // expand all of the planes
    mapface = hullbrush->faces;
    for (i = 0; i < hullbrush->numfaces; i++, mapface++) {
        if (mapface->flags & TEX_NOEXPAND)
            continue;
        VectorCopy(vec3_origin, corner);
        for (x = 0; x < 3; x++) {
            if (mapface->plane.normal[x] > 0)
                corner[x] = hull_size[1][x];
            else if (mapface->plane.normal[x] < 0)
                corner[x] = hull_size[0][x];
        }
        mapface->plane.dist += DotProduct(corner, mapface->plane.normal);
    }

    // add any axis planes not contained in the brush to bevel off corners
    for (x = 0; x < 3; x++)
        for (s = -1; s <= 1; s += 2) {
            // add the plane
            VectorCopy(vec3_origin, plane.normal);
            plane.normal[x] = (vec_t)s;
            if (s == -1)
                plane.dist = -hullbrush->mins[x] + -hull_size[0][x];
            else
                plane.dist = hullbrush->maxs[x] + hull_size[1][x];
            AddBrushPlane(hullbrush, &plane);
        }

    // add all of the edge bevels
    for (f = facelist; f; f = f->next)
        for (i = 0; i < f->w.numpoints; i++)
            AddHullEdge(hullbrush, f->w.points[i],
                        f->w.points[(i + 1) % f->w.numpoints], hull_size);
}

//============================================================================

static const int DetailFlag = (1 << 27);

static bool
Brush_IsDetail(const mapbrush_t *mapbrush)
{
    const mapface_t &mapface = mapbrush->face(0);
    
    if ((mapface.contents & DetailFlag) == DetailFlag) {
        return true;
    }
    return false;
}


static int
Brush_GetContents(const mapbrush_t *mapbrush)
{
    const char *texname;

    //check for strong content indicators
    for (int i = 0; i < mapbrush->numfaces; i++)
    {
        const mapface_t &mapface = mapbrush->face(i);
        const mtexinfo_t &texinfo = map.mtexinfos.at(mapface.texinfo);
        texname = map.miptex.at(texinfo.miptex).c_str();

        if (!Q_strcasecmp(texname, "origin"))
            return CONTENTS_ORIGIN;
        if (!Q_strcasecmp(texname, "hint"))
            return CONTENTS_HINT;
        if (!Q_strcasecmp(texname, "clip"))
            return CONTENTS_CLIP;

        if (texname[0] == '*') {
            if (!Q_strncasecmp(texname + 1, "lava", 4))
                return CONTENTS_LAVA;
            if (!Q_strncasecmp(texname + 1, "slime", 5))
                return CONTENTS_SLIME;
            return CONTENTS_WATER;
        }

        if (!Q_strncasecmp(texname, "sky", 3))
            return CONTENTS_SKY;
    }
    //and anything else is assumed to be a regular solid.
    return CONTENTS_SOLID;
}


/*
===============
LoadBrush

Converts a mapbrush to a bsp brush
===============
*/
brush_t *LoadBrush(const mapentity_t *src, const mapbrush_t *mapbrush, int contents, const vec3_t rotate_offset, const rotation_t rottype, const int hullnum)
{
    hullbrush_t hullbrush;
    brush_t *brush;
    face_t *facelist;

    // create the faces

    hullbrush.linenum =  mapbrush->face(0).linenum;
    if (mapbrush->numfaces > MAX_FACES)
        Error("brush->faces >= MAX_FACES (%d), source brush on line %d",
              MAX_FACES, hullbrush.linenum);
    
    hullbrush.contents = contents;
    hullbrush.srcbrush = mapbrush;
    hullbrush.numfaces = mapbrush->numfaces;
    for (int i=0; i<mapbrush->numfaces; i++)
        hullbrush.faces[i] = mapbrush->face(i);

    if (hullnum <= 0) {
        // for hull 0 or BSPX -wrbrushes collision, apply the rotation offset now
        facelist = CreateBrushFaces(src, &hullbrush, rotate_offset, rottype, hullnum);
    } else {
        // for Quake-style clipping hulls, don't apply rotation offset yet..
        // it will be applied below
        facelist = CreateBrushFaces(src, &hullbrush, vec3_origin, rottype, hullnum);
    }
    
    if (!facelist) {
        Message(msgWarning, warnNoBrushFaces);
        logprint("^ brush at line %d of .map file\n", hullbrush.linenum);
        return NULL;
    }

    if (options.BSPVersion == BSPHLVERSION)
    {
         if (hullnum == 1) {
            vec3_t size[2] = { {-16, -16, -36}, {16, 16, 36} };
            ExpandBrush(&hullbrush, size, facelist);
            FreeBrushFaces(facelist);
            facelist = CreateBrushFaces(src, &hullbrush, rotate_offset, rottype, hullnum);
        }
        else    if (hullnum == 2) {
            vec3_t size[2] = { {-32, -32, -32}, {32, 32, 32} };
            ExpandBrush(&hullbrush, size, facelist);
            FreeBrushFaces(facelist);
            facelist = CreateBrushFaces(src, &hullbrush, rotate_offset, rottype,  hullnum);
        }
        else    if (hullnum == 3) {
            vec3_t size[2] = { {-16, -16, -18}, {16, 16, 18} };
            ExpandBrush(&hullbrush, size, facelist);
            FreeBrushFaces(facelist);
            facelist = CreateBrushFaces(src, &hullbrush, rotate_offset, rottype, hullnum);
        }
    }
    else if (options.hexen2)
    {
        if (hullnum == 1) {
            vec3_t size[2] = { {-16, -16, -32}, {16, 16, 24} };
            ExpandBrush(&hullbrush, size, facelist);
            FreeBrushFaces(facelist);
            facelist = CreateBrushFaces(src, &hullbrush, rotate_offset, rottype, hullnum);
        }
        else    if (hullnum == 2) {
            vec3_t size[2] = { {-24, -24, -20}, {24, 24, 20} };
            ExpandBrush(&hullbrush, size, facelist);
            FreeBrushFaces(facelist);
            facelist = CreateBrushFaces(src, &hullbrush, rotate_offset, rottype, hullnum);
        }
        else    if (hullnum == 3) {
            vec3_t size[2] = { {-16, -16, -12}, {16, 16, 16} };
            ExpandBrush(&hullbrush, size, facelist);
            FreeBrushFaces(facelist);
            facelist = CreateBrushFaces(src, &hullbrush, rotate_offset, rottype, hullnum);
        }
        else    if (hullnum == 4) {
#if 0
            if (options.hexen2 == 1) { /*original game*/
                vec3_t size[2] = { {-40, -40, -42}, {40, 40, 42} };
                ExpandBrush(&hullbrush, size, facelist);
                FreeBrushFaces(facelist);
                facelist = CreateBrushFaces(src, &hullbrush, rotate_offset, rottype,  hullnum);
            } else
#endif
            {   /*mission pack*/
                    vec3_t size[2] = { {-8, -8, -8}, {8, 8, 8} };
                    ExpandBrush(&hullbrush, size, facelist);
                    FreeBrushFaces(facelist);
                    facelist = CreateBrushFaces(src, &hullbrush, rotate_offset, rottype, hullnum);
            }
        }
        else    if (hullnum == 5) {
            vec3_t size[2] = { {-48, -48, -50}, {48, 48, 50} };
            ExpandBrush(&hullbrush, size, facelist);
            FreeBrushFaces(facelist);
            facelist = CreateBrushFaces(src, &hullbrush, rotate_offset, rottype, hullnum);
        }
    }
    else
    {
        if (hullnum == 1) {
            vec3_t size[2] = { {-16, -16, -32}, {16, 16, 24} };

            ExpandBrush(&hullbrush, size, facelist);
            FreeBrushFaces(facelist);
            facelist = CreateBrushFaces(src, &hullbrush, rotate_offset, rottype, hullnum);
        } else if (hullnum == 2) {
            vec3_t size[2] = { {-32, -32, -64}, {32, 32, 24} };

            ExpandBrush(&hullbrush, size, facelist);
            FreeBrushFaces(facelist);
            facelist = CreateBrushFaces(src, &hullbrush, rotate_offset, rottype, hullnum);
        }
    }

    // create the brush
    brush = (brush_t *)AllocMem(BRUSH, 1, true);

    brush->contents = contents;
    brush->faces = facelist;
    VectorCopy(hullbrush.mins, brush->mins);
    VectorCopy(hullbrush.maxs, brush->maxs);

    return brush;
}

//=============================================================================

static brush_t *
Brush_ListTail(brush_t *brush)
{
    if (brush == nullptr) {
        return nullptr;
    }
 
    while (brush->next != nullptr) {
        brush = brush->next;
    }
    
    Q_assert(brush->next == nullptr);
    return brush;
}

int
Brush_ListCountWithCFlags(const brush_t *brush, int cflags)
{
    int cnt = 0;
    for (const brush_t *b = brush; b; b = b->next) {
        if (cflags == (b->cflags & cflags))
            cnt++;
    }
    return cnt;
}

int
Brush_ListCount(const brush_t *brush)
{
    return Brush_ListCountWithCFlags(brush, 0);
}

static int FaceListCount(const face_t *facelist)
{
    if (facelist)
        return 1 + FaceListCount(facelist->next);
    else
        return 0;
}

int Brush_NumFaces(const brush_t *brush)
{
    return FaceListCount(brush->faces);
}

void
Entity_SortBrushes(mapentity_t *dst)
{
    Q_assert(dst->brushes == nullptr);
    
    brush_t **nextLink = &dst->brushes;
    
    if (dst->detail_illusionary) {
        brush_t *last = Brush_ListTail(dst->detail_illusionary);
        *nextLink = dst->detail_illusionary;
        nextLink = &last->next;
    }
    if (dst->liquid) {
        brush_t *last = Brush_ListTail(dst->liquid);
        *nextLink = dst->liquid;
        nextLink = &last->next;
    }
    if (dst->detail_fence) {
        brush_t *last = Brush_ListTail(dst->detail_fence);
        *nextLink = dst->detail_fence;
        nextLink = &last->next;
    }
    if (dst->detail) {
        brush_t *last = Brush_ListTail(dst->detail);
        *nextLink = dst->detail;
        nextLink = &last->next;
    }
    if (dst->sky) {
        brush_t *last = Brush_ListTail(dst->sky);
        *nextLink = dst->sky;
        nextLink = &last->next;
    }
    if (dst->solid) {
        brush_t *last = Brush_ListTail(dst->solid);
        *nextLink = dst->solid;
        nextLink = &last->next;
    }    
}

/*
============
Brush_LoadEntity

hullnum -1 should contain ALL brushes. (used by BSPX_CreateBrushList())
hullnum 0 does not contain clip brushes.
============
*/
void
Brush_LoadEntity(mapentity_t *dst, const mapentity_t *src, const int hullnum)
{
    const char *classname;
    const mapbrush_t *mapbrush;
    vec3_t rotate_offset;
    int i, contents, cflags = 0;
    int lmshift;
    bool all_detail, all_detail_fence, all_detail_illusionary;

    /*
     * The brush list needs to be ordered (lowest to highest priority):
     * - detail_illusionary (which is saved as empty)
     * - liquid
     * - detail_fence
     * - detail (which is solid)
     * - sky
     * - solid
     */

    classname = ValueForKey(src, "classname");

    /* Origin brush support */
    rotation_t rottype = rotation_t::none;
    VectorCopy(vec3_origin, rotate_offset);
    
    for (int i = 0; i < src->nummapbrushes; i++) {
        const mapbrush_t *mapbrush = &src->mapbrush(i);
        const int contents = Brush_GetContents(mapbrush);
        if (contents == CONTENTS_ORIGIN) {
            if (dst == pWorldEnt()) {
                Message(msgWarning, warnOriginBrushInWorld);
                continue;
            }
            
            brush_t *brush = LoadBrush(src, mapbrush, contents, vec3_origin, rotation_t::none, 0);
            if (brush) {
                vec3_t origin;
                VectorAdd(brush->mins, brush->maxs, origin);
                VectorScale(origin, 0.5, origin);
                
                char value[1024];
                q_snprintf(value, sizeof(value), "%.2f %.2f %.2f", origin[0], origin[1], origin[2]);
                SetKeyValue(dst, "origin", value);
                
                VectorCopy(origin, rotate_offset);
                rottype = rotation_t::origin_brush;
                
                FreeBrush(brush);
            }
        }
    }
    
    /* Hipnotic rotation */
    if (rottype == rotation_t::none) {
        if (!strncmp(classname, "rotate_", 7)) {
            FixRotateOrigin(dst);
            GetVectorForKey(dst, "origin", rotate_offset);
            rottype = rotation_t::hipnotic;
        }
    }

    /* If the source entity is func_detail, set the content flag */
    all_detail = false;
    if (!Q_strcasecmp(classname, "func_detail") && !options.fNodetail) {
        all_detail = true;
    }
    if (!Q_strcasecmp(classname, "func_detail_wall") && !options.fNodetail) {
        all_detail = true;
        cflags |= CFLAGS_DETAIL_WALL;
    }
    
    all_detail_fence = false;
    if (!Q_strcasecmp(classname, "func_detail_fence") && !options.fNodetail) {
        all_detail_fence = true;
    }
    
    all_detail_illusionary = false;
    if (!Q_strcasecmp(classname, "func_detail_illusionary") && !options.fNodetail) {
        all_detail_illusionary = true;
    }

    /* entities with custom lmscales are important for the qbsp to know about */
    i = 16 * atof(ValueForKey(src, "_lmscale"));
    if (!i) i = 16;     //if 0, pick a suitable default
    lmshift = 0;
    while (i > 1)
    {
        lmshift++;      //only allow power-of-two scales
        i /= 2;
    }
    
    /* _mirrorinside key (for func_water etc.) */
    if (atoi(ValueForKey(src, "_mirrorinside"))) {
        cflags |= CFLAGS_BMODEL_MIRROR_INSIDE;
    }
    
    /* _noclipfaces */
    if (atoi(ValueForKey(src, "_noclipfaces"))) {
        cflags |= CFLAGS_NO_CLIPPING_SAME_TYPE;
    }

    const bool func_illusionary_visblocker =
        (0 == Q_strcasecmp(classname, "func_illusionary_visblocker"));

    for (i = 0; i < src->nummapbrushes; i++, mapbrush++) {
        mapbrush = &src->mapbrush(i);
        contents = Brush_GetContents(mapbrush);

        // per-brush settings
        bool detail = Brush_IsDetail(mapbrush);
        bool detail_illusionary = false;
        bool detail_fence = false;
        
        // inherit the per-entity settings
        detail |= all_detail;
        detail_illusionary |= all_detail_illusionary;
        detail_fence |= all_detail_fence;
        
        /* FIXME: move into Brush_GetContents? */
        if (func_illusionary_visblocker)
            contents = CONTENTS_ILLUSIONARY_VISBLOCKER;

        /* "origin" brushes always discarded */
        if (contents == CONTENTS_ORIGIN)
            continue;
        
        /* -omitdetail option omits all types of detail */
        if (options.fOmitDetail && detail && !(cflags & CFLAGS_DETAIL_WALL))
            continue;
        if ((options.fOmitDetail || options.fOmitDetailWall) && detail && (cflags & CFLAGS_DETAIL_WALL))
            continue;
        if ((options.fOmitDetail || options.fOmitDetailIllusionary) && detail_illusionary)
            continue;
        if ((options.fOmitDetail || options.fOmitDetailFence) && detail_fence)
            continue;
        
        /* turn solid brushes into detail, if we're in hull0 */
        if (hullnum == 0 && contents == CONTENTS_SOLID) {
            if (detail) {
                contents = CONTENTS_DETAIL;
            } else if (detail_illusionary) {
                contents = CONTENTS_DETAIL_ILLUSIONARY;
            } else if (detail_fence) {
                contents = CONTENTS_DETAIL_FENCE;
            }
        }
        
        /* func_detail_illusionary don't exist in the collision hull
         * (or bspx export) */
        if (hullnum && detail_illusionary) {
            continue;
        }
        
        /*
         * "clip" brushes don't show up in the draw hull, but we still want to
         * include them in the model bounds so collision detection works
         * correctly.
         */
        if (contents == CONTENTS_CLIP) {
            if (hullnum == 0) {
                brush_t *brush = LoadBrush(src, mapbrush, contents, rotate_offset, rottype, hullnum);
                if (brush) {
                    AddToBounds(dst, brush->mins);
                    AddToBounds(dst, brush->maxs);
                    FreeBrush(brush);
                }
                continue;
            }
            // for hull1, 2, etc., convert clip to CONTENTS_SOLID
            if (hullnum > 0) {
                contents = CONTENTS_SOLID;
            }
            // if hullnum is -1 (bspx brush export), leave it as CONTENTS_CLIP
        }

        /* "hint" brushes don't affect the collision hulls */
        if (contents == CONTENTS_HINT) {
            if (hullnum)
                continue;
            contents = CONTENTS_EMPTY;
        }

        /* entities never use water merging */
        if (dst != pWorldEnt())
            contents = CONTENTS_SOLID;

        /* Hack to turn bmodels with "_mirrorinside" into func_detail_fence in hull 0.
           this is to allow "_mirrorinside" to work on func_illusionary, func_wall, etc.
           Otherwise they would be CONTENTS_SOLID and the inside faces would be deleted.
         
           It's CONTENTS_DETAIL_FENCE because this gets mapped to CONTENTS_SOLID just
           before writing the bsp, and bmodels normally have CONTENTS_SOLID as their
           contents type.
         */
        if (dst != pWorldEnt() && hullnum == 0 && (cflags & CFLAGS_BMODEL_MIRROR_INSIDE)) {
            contents = CONTENTS_DETAIL_FENCE;
        }
        
        /* nonsolid brushes don't show up in clipping hulls */
        if (hullnum > 0 && contents != CONTENTS_SOLID && contents != CONTENTS_SKY)
            continue;

        /* sky brushes are solid in the collision hulls */
        if (hullnum > 0 && contents == CONTENTS_SKY)
            contents = CONTENTS_SOLID;
        
        brush_t *brush = LoadBrush(src, mapbrush, contents, rotate_offset, rottype, hullnum);
        if (!brush)
            continue;

        dst->numbrushes++;
        brush->lmshift = lmshift;
        brush->cflags = cflags;
        
        if (brush->contents == CONTENTS_SOLID) {
            brush->next = dst->solid;
            dst->solid = brush;
        } else if (brush->contents == CONTENTS_SKY) {
            brush->next = dst->sky;
            dst->sky = brush;
        } else if (brush->contents == CONTENTS_DETAIL) {
            brush->next = dst->detail;
            dst->detail = brush;
        } else if (brush->contents == CONTENTS_DETAIL_ILLUSIONARY) {
            brush->next = dst->detail_illusionary;
            dst->detail_illusionary = brush;
        } else if (brush->contents == CONTENTS_DETAIL_FENCE) {
            brush->next = dst->detail_fence;
            dst->detail_fence = brush;
        } else {
            brush->next = dst->liquid;
            dst->liquid = brush;
        }

        AddToBounds(dst, brush->mins);
        AddToBounds(dst, brush->maxs);

        Message(msgPercent, i + 1, src->nummapbrushes);
    }
}

//============================================================

/*
==================
BoundBrush

Sets the mins/maxs based on the windings
returns false if the brush doesn't enclose a valid volume

from q3map
==================
*/
bool BoundBrush (brush_t *brush)
{
    ClearBounds (brush->mins, brush->maxs);
    
    for (face_t *face = brush->faces; face; face = face->next) {
        const winding_t *w = &face->w;
        for (int j=0 ; j<w->numpoints ; j++)
            AddPointToBounds (w->points[j], brush->mins, brush->maxs);
    }
    
    for (int i=0 ; i<3 ; i++) {
        if (brush->mins[i] < MIN_WORLD_COORD || brush->maxs[i] > MAX_WORLD_COORD
            || brush->mins[i] >= brush->maxs[i] ) {
            return false;
        }
    }
    
    return true;
}

/*
==================
BrushVolume

from q3map
modified to follow https://en.wikipedia.org/wiki/Polyhedron#Volume
==================
*/
vec_t BrushVolume (const brush_t *brush)
{
    if (!brush)
        return 0;
    
    vec_t volume = 0;
    for (const face_t *face = brush->faces; face; face = face->next) {
        if (!face->w.numpoints)
            continue;
        
        const vec_t area = WindingArea(&face->w);
        const plane_t faceplane = Face_Plane(face);
        
        volume += DotProduct(faceplane.normal, face->w.points[0]) * area;
    }
    
    volume /= 3.0;
    
    return volume;
}

/*
==================
BrushMostlyOnSide

from q3map
==================
*/
int BrushMostlyOnSide (const brush_t *brush, const vec3_t planenormal, vec_t planedist)
{
    vec_t max;
    int side;
    
    max = 0;
    side = SIDE_FRONT;
    
    for (const face_t *face = brush->faces; face; face = face->next) {
        const winding_t *w = &face->w;
        if (!w->numpoints)
            continue;
        
        for (int j=0 ; j<w->numpoints ; j++) {
            const vec_t d = DotProduct (w->points[j], planenormal) - planedist;
            if (d > max) {
                max = d;
                side = SIDE_FRONT;
            }
            if (-d > max) {
                max = -d;
                side = SIDE_BACK;
            }
        }
    }
    
    return side;
}

face_t *CopyFace(const face_t *face)
{
    face_t *newface = (face_t *)AllocMem(FACE, 1, true);
    
    memcpy(newface, face, sizeof(face_t));
    
    // clear stuff that shouldn't be copied.
    newface->original = nullptr;
    newface->outputnumber = -1;
    newface->edges = nullptr;
    newface->next = nullptr;
    
    return newface;
}

/*
==================
CopyBrush

from q3map

Duplicates the brush, the sides, and the windings
==================
*/
brush_t *CopyBrush (const brush_t *brush)
{
    brush_t *newbrush = (brush_t *)AllocMem(BRUSH, 1, true);
    
    memcpy(newbrush, brush, sizeof(brush_t));
    
    newbrush->next = nullptr;
    newbrush->faces = nullptr;
    
    for (const face_t *face = brush->faces; face; face = face->next) {
        
        face_t *newface = CopyFace(face);
        
        // link into newbrush
        newface->next = newbrush->faces;
        newbrush->faces = newface;
    }
    
    return newbrush;
}

/*
================
WindingIsTiny

Returns true if the winding would be crunched out of
existance by the vertex snapping.

from q3map
================
*/
#define	EDGE_LENGTH	0.2
static qboolean
WindingIsTiny (const winding_t *w)
{
    /*
     if (WindingArea (w) < 1)
     return qtrue;
     return qfalse;
     */
    int		i, j;
    vec_t	len;
    vec3_t	delta;
    int		edges;
    
    edges = 0;
    for (i=0 ; i<w->numpoints ; i++)
    {
        j = i == w->numpoints - 1 ? 0 : i+1;
        VectorSubtract (w->points[j], w->points[i], delta);
        len = VectorLength (delta);
        if (len > EDGE_LENGTH)
        {
            if (++edges == 3)
                return false;
        }
    }
    return true;
}

/*
================
WindingIsHuge

Returns true if the winding still has one of the points
from basewinding for plane
 
from q3map
================
*/
qboolean WindingIsHuge (winding_t *w)
{
    int		i, j;
    
    for (i=0 ; i<w->numpoints ; i++) {
        for (j=0 ; j<3 ; j++)
            if (w->points[i][j] <= MIN_WORLD_COORD || w->points[i][j] >= MAX_WORLD_COORD)
                return true;
    }
    return false;
}

/*
================
SplitBrush

Generates two new brushes, leaving the original
unchanged
 
from q3map
================
*/
void SplitBrush (const brush_t *brush,
                 int planenum,
                 int planeside,
                 brush_t **front, brush_t **back)
{
    *front = nullptr;
    *back = nullptr;
    
    qbsp_plane_t plane;
    {
        const qbsp_plane_t *globalplane = &map.planes.at(planenum);
        VectorCopy(globalplane->normal, plane.normal);
        plane.dist = globalplane->dist;
        if (planeside) {
            VectorScale(plane.normal, -1, plane.normal);
            plane.dist = -plane.dist;
        }
        // FIXME: dangerous..
        plane.type = -1000;
        plane.outputplanenum = -1;
    }
    
    // check all points
    vec_t d_front = 0;
    vec_t d_back = 0;
    for (const face_t *face = brush->faces; face; face = face->next) {
        const winding_t *w = &face->w;
        if (!w->numpoints)
            continue;
        
        for (int j=0 ; j<w->numpoints ; j++) {
            const vec_t d = DotProduct (w->points[j], plane.normal) - plane.dist;
            if (d > 0 && d > d_front)
                d_front = d;
            if (d < 0 && d < d_back)
                d_back = d;
        }
    }
    
    if (d_front < 0.1) // PLANESIDE_EPSILON)
    {	// only on back
        *back = CopyBrush (brush);
        return;
    }
    if (d_back > -0.1) // PLANESIDE_EPSILON)
    {	// only on front
        *front = CopyBrush (brush);
        return;
    }
    
    // create a new winding from the split plane    
    winding_t *w = BaseWindingForPlane (&plane);
    for (const face_t *face = brush->faces; face; face = face->next) {
        if (!w)
            break;
        const plane_t plane2 = FlipPlane(Face_Plane(face));
        ChopWindingInPlace (&w, plane2.normal, plane2.dist, 0); // PLANESIDE_EPSILON);
    }

    if (!w || WindingIsTiny (w) )
    {	// the brush isn't really split
        int		side;
        
        if (w)
            FreeMem(w, WINDING, 1);
        
        side = BrushMostlyOnSide (brush, plane.normal, plane.dist);
        if (side == SIDE_FRONT)
            *front = CopyBrush (brush);
        if (side == SIDE_BACK)
            *back = CopyBrush (brush);
        return;
    }
    
    if (WindingIsHuge (w))
    {
        logprint ("WARNING: huge winding\n");
    }
    
    winding_t *midwinding = w;
    brush_t	*b[2];
    
    // split it for real
    
    // first, make two empty brushes (for the front and back side of the plane)
    
    for (int i=0 ; i<2 ; i++)
    {
        b[i] = (brush_t *) AllocMem (BRUSH, 1, true);
        //memcpy( b[i], brush, sizeof( brush_t ) );

        // NOTE: brush copying
        b[i]->contents = brush->contents;
        b[i]->cflags = brush->cflags;
        b[i]->lmshift = brush->lmshift;
        b[i]->faces = nullptr;
        b[i]->next = nullptr;

        // FIXME:
        //b[i]->original = brush->original;
    }
    
    // split all the current windings
    
    for (const face_t *face = brush->faces; face; face = face->next) {
        const winding_t *w = &face->w;
        if (!w->numpoints)
            continue;
        
        winding_t *cw[2];
        DivideWinding(w, &plane, &cw[0], &cw[1]);
        
        for (int j=0 ; j<2 ; j++)
        {
            if (!cw[j])
                continue;
            /*
             if (WindingIsTiny (cw[j]))
             {
             FreeWinding (cw[j]);
             continue;
             }
             */
            
            face_t *newface = CopyFace(face);
            CopyWindingInto(&newface->w, cw[j]);
            UpdateFaceSphere(newface);
            
            // link it into the front or back brush we are building
            newface->next = b[j]->faces;
            b[j]->faces = newface;
        }
        
        if (cw[0])
            FreeMem(cw[0], WINDING, 1);
        if (cw[1])
            FreeMem(cw[1], WINDING, 1);
    }
    
    
    // see if we have valid polygons on both sides
    
    for (int i=0 ; i<2 ; i++)
    {
        BoundBrush (b[i]);
        
        int j;
        for (j=0 ; j<3 ; j++)
        {
            if (b[i]->mins[j] < MIN_WORLD_COORD || b[i]->maxs[j] > MAX_WORLD_COORD)
            {
                logprint ("bogus brush after clip\n");
                break;
            }
        }
        
        // 3 faces is ok because we add a 4th face below
        if (Brush_NumFaces(b[i]) < 3 || j < 3)
        {
            FreeBrush (b[i]);
            b[i] = nullptr;
        }
    }
    
    if ( !(b[0] && b[1]) )
    {
        if (!b[0] && !b[1])
            logprint ("split removed brush\n");
        else
            logprint ("split not on both sides\n");
        if (b[0])
        {
            FreeBrush (b[0]);
            *front = CopyBrush (brush);
        }
        if (b[1])
        {
            FreeBrush (b[1]);
            *back = CopyBrush (brush);
        }
        return;
    }
    
    // add the midwinding to both sides
    for (int i=0 ; i<2 ; i++)
    {
        // clone the first face (arbitrarily)
        face_t *newface = CopyFace(b[i]->faces);
        
        if (i == 0) {
            winding_t *newwinding = FlipWinding(midwinding);
            CopyWindingInto(&newface->w, newwinding);
            newface->planenum = planenum;
            newface->planeside = !planeside;
            FreeMem(newwinding, WINDING, 1);
        } else {
            CopyWindingInto(&newface->w, midwinding);
            newface->planenum = planenum;
            newface->planeside = planeside;
        }
        
        UpdateFaceSphere(newface);
        
        // link it into the front or back brush
        newface->next = b[i]->faces;
        b[i]->faces = newface;
    }
    
    {
        vec_t	v1;
        int		i;
        
        for (i=0 ; i<2 ; i++)
        {
            v1 = BrushVolume (b[i]);
            if (v1 < 1.0)
            {
                FreeBrush(b[i]);
                b[i] = nullptr;
                logprint ("tiny volume after clip\n");
            }
        }
    }
    
    *front = b[0];
    *back = b[1];
    
    FreeMem(midwinding, WINDING, 1);
}

#if 0
/*
====================
FilterBrushIntoTree_r

from q3map
 
returns the number of fragments the brush was split into
frees brush
====================
*/
int FilterBrushIntoTree_r( brush_t *b, node_t *node )
{
    if ( !b ) {
        return 0;
    }
    
    // add it to the leaf list
    if ( node->planenum == PLANENUM_LEAF ) {
        
        Q_assert(b->next == nullptr);
        b->next = node->q3map_brushlist;
        node->q3map_brushlist = b;
        
        // FIXME: set node->q3map_contents
        
        return 1;
    }
    
    // split it by the node plane
    brush_t		*front, *back;
    SplitBrush ( b, node->planenum, 0, &front, &back );
    FreeBrush( b );
    
    int c = 0;
    c += FilterBrushIntoTree_r( front, node->children[0] );
    c += FilterBrushIntoTree_r( back, node->children[1] );
    
    return c;
}

/*
=====================
FilterStructuralBrushesIntoTree

Mark the leafs as opaque and areaportals
 
from q3map
=====================
*/
void FilterStructuralBrushesIntoTree( const mapentity_t *e, node_t *headnode )
{
    logprint( "----- FilterStructuralBrushesIntoTree -----\n");
    
    double st = I_FloatTime();
    
    int c_unique = 0;
    int c_clusters = 0;
    for ( const brush_t *b = e->brushes ; b ; b = b->next ) {
        c_unique++;
        brush_t *newb = CopyBrush( b );
        
        int r = FilterBrushIntoTree_r( newb, headnode );
        c_clusters += r;
        
        // mark all sides as visible so drawsurfs are created
    }
    
    logprint( "%5i structural brushes\n", c_unique );
    logprint( "%5i cluster references\n", c_clusters );
    logprint( "took %f seconds\n", I_FloatTime() - st );
}
#endif
