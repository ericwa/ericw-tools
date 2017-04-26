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
    int numfaces;
    vec3_t mins;
    vec3_t maxs;
    mapface_t faces[MAX_FACES];

    int numpoints;
    int numedges;
    vec3_t points[MAX_HULL_POINTS];
    vec3_t corners[MAX_HULL_POINTS * 8];
    int edges[MAX_HULL_EDGES][2];
} hullbrush_t;

/*
=================
CheckFace

Note: this will not catch 0 area polygons
=================
*/
void
CheckFace(face_t *face)
{
    const qbsp_plane_t *plane = &map.planes[face->planenum];
    const vec_t *p1, *p2;
    vec_t length, dist, edgedist;
    vec3_t edgevec, edgenormal, facenormal;
    int i, j;

    if (face->w.numpoints < 3) {
        if (face->w.numpoints == 2) {
            Error("%s: too few points (2): (%f %f %f) (%f %f %f)\n", __func__,
                  face->w.points[0][0], face->w.points[0][1], face->w.points[0][2],
                  face->w.points[1][0], face->w.points[1][1], face->w.points[1][2]);
        } else if (face->w.numpoints == 1) {
            Error("%s: too few points (1): (%f %f %f)\n", __func__, 
                  face->w.points[0][0], face->w.points[0][1], face->w.points[0][2]);
        } else {
            Error("%s: too few points (%d)", __func__, face->w.numpoints);
        }
    }

    VectorCopy(plane->normal, facenormal);
    if (face->planeside)
        VectorSubtract(vec3_origin, facenormal, facenormal);

    for (i = 0; i < face->w.numpoints; i++) {
        p1 = face->w.points[i];
        p2 = face->w.points[(i + 1) % face->w.numpoints];

        for (j = 0; j < 3; j++)
            if (p1[j] > BOGUS_RANGE || p1[j] < -BOGUS_RANGE)
                Error("%s: coordinate out of range (%f)", __func__, p1[j]);

        /* check the point is on the face plane */
        dist = DotProduct(p1, plane->normal) - plane->dist;
        if (dist < -ON_EPSILON || dist > ON_EPSILON)
            Message(msgWarning, warnPointOffPlane, p1[0], p1[1], p1[2], dist);

        /* check the edge isn't degenerate */
        VectorSubtract(p2, p1, edgevec);
        length = VectorLength(edgevec);
        if (length < ON_EPSILON) {
            Message(msgWarning, warnDegenerateEdge, length, p1[0], p1[1], p1[2]);
            for (j = i + 1; j < face->w.numpoints; j++)
                VectorCopy(face->w.points[j], face->w.points[j - 1]);
            face->w.numpoints--;
            CheckFace(face);
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
                Error("%s: Found a non-convex face (error size %f, point: %f %f %f)\n",
                      __func__, dist - edgedist, face->w.points[j][0], face->w.points[j][1], face->w.points[j][2]);
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


int
PlaneEqual(const qbsp_plane_t *p1, const qbsp_plane_t *p2)
{
    return (fabs(p1->normal[0] - p2->normal[0]) < NORMAL_EPSILON &&
            fabs(p1->normal[1] - p2->normal[1]) < NORMAL_EPSILON &&
            fabs(p1->normal[2] - p2->normal[2]) < NORMAL_EPSILON &&
            fabs(p1->dist - p2->dist) < DIST_EPSILON);
}

int
PlaneInvEqual(const qbsp_plane_t *p1, const qbsp_plane_t *p2)
{
    return (fabs(p1->normal[0] + p2->normal[0]) < NORMAL_EPSILON &&
            fabs(p1->normal[1] + p2->normal[1]) < NORMAL_EPSILON &&
            fabs(p1->normal[2] + p2->normal[2]) < NORMAL_EPSILON &&
            fabs(p1->dist + p2->dist) < DIST_EPSILON);
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
FindPlane(const qbsp_plane_t *plane, int *side)
{
    for (int i : map.planehash[plane_hash_fn(plane)]) {
        const qbsp_plane_t &p = map.planes.at(i);
        if (PlaneEqual(&p, plane)) {
            *side = SIDE_FRONT;
            return i;
        } else if (PlaneInvEqual(&p, plane)) {
            *side = SIDE_BACK;
            return i;
        }
    }
    return NewPlane(plane->normal, plane->dist, side);
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
CreateBrushFaces(hullbrush_t *hullbrush, const vec3_t rotate_offset,
                 const int hullnum)
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
        if (!hullnum) {
            /* Don't generate hintskip faces */
            const mtexinfo_t &texinfo = map.mtexinfos.at(mapface->texinfo);
            const char *texname = map.miptex.at(texinfo.miptex).c_str();
            if (!Q_strcasecmp(texname, "hintskip"))
                continue;
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
        f->planenum = FindPlane(&plane, &f->planeside);
        f->next = facelist;
        facelist = f;
        CheckFace(f);
        UpdateFaceSphere(f);
    }

    // Rotatable objects must have a bounding box big enough to
    // account for all its rotations
    if (rotate_offset[0] || rotate_offset[1] || rotate_offset[2]) {
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
FreeBrushes(brush_t *brushlist)
{
    brush_t *brush, *next;

    for (brush = brushlist; brush; brush = next) {
        next = brush->next;
        FreeBrushFaces(brush->faces);
        FreeMem(brush, BRUSH, 1);
    }
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

static int
Brush_GetContents(const mapbrush_t *mapbrush)
{
    const char *texname;
    const mapface_t &mapface = mapbrush->face(0);
    const mtexinfo_t &texinfo = map.mtexinfos.at(mapface.texinfo);
    texname = map.miptex.at(texinfo.miptex).c_str();

    if (!Q_strcasecmp(texname, "origin"))
        return CONTENTS_ORIGIN;
    if (!Q_strcasecmp(texname, "hint") || !Q_strcasecmp(texname, "hintskip"))
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

    return CONTENTS_SOLID;
}


/*
===============
LoadBrush

Converts a mapbrush to a bsp brush
===============
*/
brush_t *LoadBrush(const mapbrush_t *mapbrush, const vec3_t rotate_offset, const int hullnum)
{
    hullbrush_t hullbrush;
    brush_t *brush;
    face_t *facelist;

    // create the faces
    if (mapbrush->numfaces > MAX_FACES)
        Error("brush->faces >= MAX_FACES (%d), source brush on line %d",
              MAX_FACES, mapbrush->face(0).linenum);

    hullbrush.srcbrush = mapbrush;
    hullbrush.numfaces = mapbrush->numfaces;
    for (int i=0; i<mapbrush->numfaces; i++)
        hullbrush.faces[i] = mapbrush->face(i);

    if (hullnum == 0) {
        facelist = CreateBrushFaces(&hullbrush, rotate_offset, hullnum);
    } else {
        // for clipping hulls, don't apply rotation offset yet..
        // it will be applied below
        facelist = CreateBrushFaces(&hullbrush, vec3_origin, hullnum);
    }
    
    if (!facelist) {
        Message(msgWarning, warnNoBrushFaces);
        return NULL;
    }

    if (options.hexen2)
    {
        if (hullnum == 1) {
            vec3_t size[2] = { {-16, -16, -32}, {16, 16, 24} };
            ExpandBrush(&hullbrush, size, facelist);
            FreeBrushFaces(facelist);
            facelist = CreateBrushFaces(&hullbrush, rotate_offset, hullnum);
        }
        else    if (hullnum == 2) {
            vec3_t size[2] = { {-24, -24, -20}, {24, 24, 20} };
            ExpandBrush(&hullbrush, size, facelist);
            FreeBrushFaces(facelist);
            facelist = CreateBrushFaces(&hullbrush, rotate_offset, hullnum);
        }
        else    if (hullnum == 3) {
            vec3_t size[2] = { {-16, -16, -12}, {16, 16, 16} };
            ExpandBrush(&hullbrush, size, facelist);
            FreeBrushFaces(facelist);
            facelist = CreateBrushFaces(&hullbrush, rotate_offset, hullnum);
        }
        else    if (hullnum == 4) {
#if 0
            if (options.hexen2 == 1) { /*original game*/
                vec3_t size[2] = { {-40, -40, -42}, {40, 40, 42} };
                ExpandBrush(&hullbrush, size, facelist);
                FreeBrushFaces(facelist);
                facelist = CreateBrushFaces(&hullbrush, rotate_offset, hullnum);
            } else
#endif
            {   /*mission pack*/
                    vec3_t size[2] = { {-8, -8, -8}, {8, 8, 8} };
                    ExpandBrush(&hullbrush, size, facelist);
                    FreeBrushFaces(facelist);
                    facelist = CreateBrushFaces(&hullbrush, rotate_offset, hullnum);
            }
        }
        else    if (hullnum == 5) {
            vec3_t size[2] = { {-48, -48, -50}, {48, 48, 50} };
            ExpandBrush(&hullbrush, size, facelist);
            FreeBrushFaces(facelist);
            facelist = CreateBrushFaces(&hullbrush, rotate_offset, hullnum);
        }
    }
    else
    {
        if (hullnum == 1) {
            vec3_t size[2] = { {-16, -16, -32}, {16, 16, 24} };

            ExpandBrush(&hullbrush, size, facelist);
            FreeBrushFaces(facelist);
            facelist = CreateBrushFaces(&hullbrush, rotate_offset, hullnum);
        } else if (hullnum == 2) {
            vec3_t size[2] = { {-32, -32, -64}, {32, 32, 24} };

            ExpandBrush(&hullbrush, size, facelist);
            FreeBrushFaces(facelist);
            facelist = CreateBrushFaces(&hullbrush, rotate_offset, hullnum);
        }
    }

    // create the brush
    brush = (brush_t *)AllocMem(BRUSH, 1, true);

    brush->faces = facelist;
    VectorCopy(hullbrush.mins, brush->mins);
    VectorCopy(hullbrush.maxs, brush->maxs);

    return brush;
}

//=============================================================================


/*
============
Brush_LoadEntity

hullnum -1 should contain ALL brushes.
hullnum 0 does not contain clip brushes.
============
*/
void
Brush_LoadEntity(mapentity_t *dst, const mapentity_t *src, const int hullnum)
{
    const char *classname;
    brush_t *brush, *next, *nonsolid, *solid;
    const mapbrush_t *mapbrush;
    vec3_t rotate_offset;
    int i, contents, cflags = 0;
    int lmshift;

    /*
     * The brush list needs to be ordered:
     * 1. detail nonsolid
     * 2. nonsolid
     * 3. detail solid
     * 4. solid
     *
     * We will add func_group brushes first and detail brushes last, so we can
     * always just put nonsolid on the head of the list, but will need to insert
     * solid brushes between any existing nonsolid and solids on the list.
     */

    solid = NULL;
    nonsolid = dst->brushes;
    classname = ValueForKey(src, "classname");

    /* Origin brush support */
    bool usesOriginBrush = false;
    VectorCopy(vec3_origin, rotate_offset);
    
    for (int i = 0; i < src->nummapbrushes; i++) {
        const mapbrush_t *mapbrush = &src->mapbrush(i);
        const int contents = Brush_GetContents(mapbrush);
        if (contents == CONTENTS_ORIGIN) {
            if (dst == pWorldEnt()) {
                Message(msgWarning, warnOriginBrushInWorld);
                continue;
            }
            
            brush_t *brush = LoadBrush(mapbrush, vec3_origin, 0);
            if (brush) {
                vec3_t origin;
                VectorAdd(brush->mins, brush->maxs, origin);
                VectorScale(origin, 0.5, origin);
                
                char value[1024];
                q_snprintf(value, sizeof(value), "%.2f %.2f %.2f", origin[0], origin[1], origin[2]);
                SetKeyValue(dst, "origin", value);
                
                VectorCopy(origin, rotate_offset);
                usesOriginBrush = true;
                
                FreeMem(brush, BRUSH, 1);
            }
        }
    }
    
    /* Hipnotic rotation */
    if (!usesOriginBrush) {
        if (!strncmp(classname, "rotate_", 7)) {
            FixRotateOrigin(dst);
            GetVectorForKey(dst, "origin", rotate_offset);
        }
    }

    /* If the source entity is func_detail, set the content flag */
    if (!Q_strcasecmp(classname, "func_detail"))
        cflags |= CFLAGS_DETAIL;

    /* entities with custom lmscales are important for the qbsp to know about */
    i = 16 * atof(ValueForKey(src, "_lmscale"));
    if (!i) i = 16;     //if 0, pick a suitable default
    lmshift = 0;
    while (i > 1)
    {
        lmshift++;      //only allow power-of-two scales
        i /= 2;
    }
    
    for (i = 0; i < src->nummapbrushes; i++, mapbrush++) {
        mapbrush = &src->mapbrush(i);
        contents = Brush_GetContents(mapbrush);

        /* "origin" brushes always discarded */
        if (contents == CONTENTS_ORIGIN)
            continue;
        
        /* -omitdetail option */
        if (options.fOmitDetail
            && ((cflags & CFLAGS_DETAIL) == CFLAGS_DETAIL))
            continue;
        
        /*
         * "clip" brushes don't show up in the draw hull, but we still want to
         * include them in the model bounds so collision detection works
         * correctly.
         */
        if (contents == CONTENTS_CLIP) {
            if (hullnum <= 0) {
                brush = LoadBrush(mapbrush, rotate_offset, hullnum);
                if (brush) {
                    AddToBounds(dst, brush->mins);
                    AddToBounds(dst, brush->maxs);
                    FreeBrushFaces(brush->faces);
                    FreeMem(brush, BRUSH, 1);
                }
                continue;
            }
            contents = CONTENTS_SOLID;
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

        /* nonsolid brushes don't show up in clipping hulls */
        if (hullnum && contents != CONTENTS_SOLID && contents != CONTENTS_SKY)
            continue;

        /* sky brushes are solid in the collision hulls */
        if (hullnum && contents == CONTENTS_SKY)
            contents = CONTENTS_SOLID;

        brush = LoadBrush(mapbrush, rotate_offset, hullnum);
        if (!brush)
            continue;

        dst->numbrushes++;
        brush->contents = contents;
        brush->lmshift = lmshift;
        brush->cflags = cflags;
        if (brush->contents != CONTENTS_SOLID) {
            brush->next = nonsolid;
            nonsolid = brush;
        } else {
            brush->next = solid;
            solid = brush;
        }

        AddToBounds(dst, brush->mins);
        AddToBounds(dst, brush->maxs);

        Message(msgPercent, i + 1, src->nummapbrushes);
    }

    if (!nonsolid) {
        /* No non-solids and no dst brushes */
        dst->brushes = solid;
        return;
    }
    if (nonsolid->contents == CONTENTS_SOLID) {
        /* No non-solids added */
        if (!solid)
            return;

        /* Add the new solids to the head of the dst list */
        brush = dst->brushes;
        dst->brushes = solid;
        next = solid->next;
        while (next) {
            solid = next;
            next = next->next;
        }
        solid->next = brush;
        return;
    }

    /* Insert the non-solids at the dst head */
    dst->brushes = nonsolid;
    next = nonsolid->next;
    while (next && next->contents != CONTENTS_SOLID) {
        nonsolid = next;
        next = next->next;
    }
    /* If no new solids to add, we are done */
    if (!solid)
        return;

    /* Insert new solids and re-attach the existing solids list (next) */
    nonsolid->next = solid;
    if (next) {
        while (solid->next)
            solid = solid->next;
        solid->next = next;
    }
}
