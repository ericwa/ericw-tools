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

#include "qbsp.h"
#include "wad.h"

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
    const plane_t *plane = &map.planes[face->planenum];
    const vec_t *p1, *p2;
    vec_t length, dist, edgedist;
    vec3_t edgevec, edgenormal, facenormal;
    int i, j;

    if (face->w.numpoints < 3)
	Error("%s: too few points (%d)", __func__, face->w.numpoints);

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
		Error("%s: Found a non-convex face (error size %f)\n",
		      __func__, dist - edgedist);
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
NormalizePlane(plane_t *p)
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
PlaneEqual(const plane_t *p1, const plane_t *p2)
{
    return (fabs(p1->normal[0] - p2->normal[0]) < NORMAL_EPSILON &&
	    fabs(p1->normal[1] - p2->normal[1]) < NORMAL_EPSILON &&
	    fabs(p1->normal[2] - p2->normal[2]) < NORMAL_EPSILON &&
	    fabs(p1->dist - p2->dist) < DIST_EPSILON);
}

int
PlaneInvEqual(const plane_t *p1, const plane_t *p2)
{
    return (fabs(p1->normal[0] + p2->normal[0]) < NORMAL_EPSILON &&
	    fabs(p1->normal[1] + p2->normal[1]) < NORMAL_EPSILON &&
	    fabs(p1->normal[2] + p2->normal[2]) < NORMAL_EPSILON &&
	    fabs(p1->dist + p2->dist) < DIST_EPSILON);
}

/* Plane Hashing */
#define	PLANE_HASHES (1<<10)
static struct plane *plane_hash[PLANE_HASHES];

/*
 * Choice of hash function:
 * - Begin with abs(dist), very rarely > 4096
 * - Many maps probably won't go beyond 2048 units
 * - Low 3 bits also very commonly zero (axial planes on multiples of 8 units)
 */
static inline int
plane_hash_fn(const struct plane *p)
{
    const int dist = floor(fabs(p->dist) + 0.5);

    return (dist ^ (dist >> 3)) & (PLANE_HASHES - 1);
}

static void
PlaneHash_Add(struct plane *p)
{
    const int hash = plane_hash_fn(p);

    p->hash_chain = plane_hash[hash];
    plane_hash[hash] = p;
}

void
PlaneHash_Init(void)
{
    int i;

    for (i = 0; i < PLANE_HASHES; ++i)
	plane_hash[i] = NULL;
}

/*
 * NewPlane
 * - Returns a global plane number and the side that will be the front
 */
static int
NewPlane(const vec3_t normal, const vec_t dist, int *side)
{
    plane_t *plane;
    vec_t len;

    len = VectorLength(normal);
    if (len < 1 - ON_EPSILON || len > 1 + ON_EPSILON)
	Error("%s: invalid normal (vector length %.4f)", __func__, len);
    if (map.numplanes == map.maxplanes)
	Error("Internal error: didn't allocate enough planes? (%s)", __func__);

    plane = &map.planes[map.numplanes];
    VectorCopy(normal, plane->normal);
    plane->dist = dist;
    *side = NormalizePlane(plane) ? SIDE_BACK : SIDE_FRONT;
    PlaneHash_Add(plane);

    return map.numplanes++;
}

/*
 * FindPlane
 * - Returns a global plane number and the side that will be the front
 */
int
FindPlane(const plane_t *plane, int *side)
{
    const int bins[] = { 0, 1, -1 };
    const plane_t *p;
    int hash, h;
    int i;

    /* search the border bins as well */
    hash = plane_hash_fn(plane);
    for (i = 0; i < 3; ++i) {
	h = (hash + bins[i]) & (PLANE_HASHES - 1);
	for (p = plane_hash[h]; p; p = p->hash_chain) {
	    if (PlaneEqual(p, plane)) {
		*side = SIDE_FRONT;
		return p - map.planes;
	    } else if (PlaneInvEqual(p, plane)) {
		*side = SIDE_BACK;
		return p - map.planes;
	    }
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

    for (i = 0, entity = map.entities; i < map.numentities; i++, entity++) {
	name = ValueForKey(entity, "targetname");
	if (!strcasecmp(target, name))
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

    snprintf(value, sizeof(value), "%d %d %d", (int)offset[0],
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
    plane_t plane;
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
	    const texinfo_t *texinfo = pWorldEnt->lumps[LUMP_TEXINFO].data;
	    const char *texname = map.miptex[texinfo[mapface->texinfo].miptex];
	    if (!strcasecmp(texname, "hintskip"))
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
	    continue;		// overconstrained plane

	// this face is a keeper
	f = AllocMem(FACE, 1, true);
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
            const texinfo_t *texinfo = pWorldEnt->lumps[LUMP_TEXINFO].data;
            texinfo_t texInfoNew;
            vec3_t vecs[2];
            int k, l;

            memcpy(&texInfoNew, &texinfo[ mapface->texinfo ], sizeof(texInfoNew));
            for (k=0; k<2; k++) {
                for (l=0; l<3; l++) {
                    vecs[k][l] = texinfo[ mapface->texinfo ].vecs[k][l];
                }
            }

            texInfoNew.vecs[0][3] += DotProduct( rotate_offset, vecs[0] );
            texInfoNew.vecs[1][3] += DotProduct( rotate_offset, vecs[1] );

            mapface->texinfo = FindTexinfo( &texInfoNew );
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

// rotation

// up / down
#define	PITCH		0

// left / right
#define	YAW		1

// fall over
#define	ROLL		2

void AngleVectors (const vec3_t angles, vec3_t forward, vec3_t right, vec3_t up)
{
	float		angle;
	float		sr, sp, sy, cr, cp, cy;

	angle = angles[YAW] * (M_PI*2 / 360);
	sy = sin(angle);
	cy = cos(angle);
	angle = angles[PITCH] * (M_PI*2 / 360);
	sp = sin(angle);
	cp = cos(angle);
	angle = angles[ROLL] * (M_PI*2 / 360);
	sr = sin(angle);
	cr = cos(angle);

	forward[0] = cp*cy;
	forward[1] = cp*sy;
	forward[2] = -sp;
	right[0] = (-1*sr*sp*cy+-1*cr*-sy);
	right[1] = (-1*sr*sp*sy+-1*cr*cy);
	right[2] = -1*sr*cp;
	up[0] = (cr*sp*cy+-sr*-sy);
	up[1] = (cr*sp*sy+-sr*cy);
	up[2] = cr*cp;
}


static void RotatePoint (vec3_t point, const vec3_t angles)
{
	vec3_t	temp;
	vec3_t	forward, right, up;

	VectorCopy (point, temp);
	AngleVectors (angles, forward, right, up);
	point[0] = DotProduct (temp, forward);
	point[1] = -DotProduct (temp, right);
	point[2] = DotProduct (temp, up);
}


// .obj loader

#define MAX_OBJ_ELEMS 65536
#define MAX_OBJ_FACE_VERTS 64

/* indices start at 1, 0=not available */
typedef struct obj_face_s {
	int vert[MAX_OBJ_FACE_VERTS];
	int tex[MAX_OBJ_FACE_VERTS];
        int numverts;
	char texture[16];
} obj_face_t;

typedef struct obj_uv_s {
	double u, v;
} obj_uv_t;

typedef struct obj_model_s {
	vec3_t verts[MAX_OBJ_ELEMS];
	int numverts;
	obj_uv_t uvs[MAX_OBJ_ELEMS];
	int numuvs;
	obj_face_t faces[MAX_OBJ_ELEMS];
	int numfaces;
} obj_model_t;

obj_model_t *LoadObjModel(const char *filename)
{
	char currenttexture[16];
	obj_model_t *m = calloc(1, sizeof(obj_model_t));
	FILE *f = fopen(filename, "r");
	if (!f) 
	{
		printf("OBJ: ERROR: Couldn't locate file '%s'\n", filename);
		goto error;
	}

	currenttexture[0] = '\0';

	while (1)
	{
		char line[256];
		char identifier[256];
		char *value;

		if (!fgets(line, sizeof(line), f)) break;
		
		if (1 != sscanf(line, "%s", identifier))
			continue;
		
		if (m->numverts == MAX_OBJ_ELEMS || m->numuvs == MAX_OBJ_ELEMS) {
			printf("OBJ: ERROR: obj file too big\n");
			goto error;
		}

		value = line + strlen(identifier);
		if (!strcmp("v", identifier))
		{
			if (3 != sscanf(value, "%lf %lf %lf", &m->verts[m->numverts][0], &m->verts[m->numverts][1], &m->verts[m->numverts][2])) goto error;
			m->numverts++;
		}
		else if (!strcmp("vt", identifier))
		{
			if (2 != sscanf(value, "%lf %lf", &m->uvs[m->numuvs].u, &m->uvs[m->numuvs].v)) goto error;
			//printf("scanned %f %f\n", m->uvs[m->numuvs].u, m->uvs[m->numuvs].v);
			m->numuvs++;

		}
		else if (!strcmp("f", identifier))
		{
                    int junk;
                    int numverts = 0;
                    int usedchars = 0;
                    
                    while (1) {
                        m->faces[m->numfaces].vert[numverts] = 0;
                        m->faces[m->numfaces].tex[numverts] = 0;
                        
                        if (3 == sscanf(value, "%d/%d/%d %n", &m->faces[m->numfaces].vert[numverts], &m->faces[m->numfaces].tex[numverts], &junk, &usedchars)
                            || 2 == sscanf(value, "%d//%d %n", &m->faces[m->numfaces].vert[numverts], &junk, &usedchars)
                            || 2 == sscanf(value, "%d/%d %n", &m->faces[m->numfaces].vert[numverts], &m->faces[m->numfaces].tex[numverts], &usedchars)
                            || 1 == sscanf(value, "%d %n", &m->faces[m->numfaces].vert[numverts], &usedchars)
                            ) {
                            value += usedchars;
                            numverts++;
                        } else break;
                    }
                    
		    if (numverts == 0) {
			printf("OBJ: ERROR: parsed zero vertices for line '%s'\n", line);
			goto error;
		    }
                    
                    m->faces[m->numfaces].numverts = numverts;
                    strcpy(m->faces[m->numfaces].texture, currenttexture);
                    //printf("face tex %s current tex %s\n", m->faces[m->numfaces].texture, currenttexture); 
                    
                    m->numfaces++;
		}
		else if (!strcmp("usemtl", identifier))
		{
			char *found;
			int i;
			while (*value == ' ') // strip leading spaces
			{
				value += 1;
			}
			if ((found = strrchr(value, '\\'))) // strip path prefix
			{
				value = found + 1;
			}
			if ((found = strrchr(value, '/'))) // strip path prefix
			{
				value = found + 1;
			}
			for (i=0; i<15; i++)
			{
				if (value[i] == '\r') break;
				if (value[i] == '\n') break;
				if (value[i] == '\0') break;
				if (value[i] == '.') break;
				currenttexture[i] = value[i];
			}
			currenttexture[i] = '\0';
			//printf("Parsed texture name '%s'\n", currenttexture);
		}
	}
	fclose(f);
	return m;
error:
	free(m);
	if (f) fclose(f);
	printf("OBJ: ERROR: error parsing obj file\n");
	return NULL;
}

static plane_t
PlaneForPoints(vec3_t p0, vec3_t p1, vec3_t p2)
{
    plane_t plane;
    vec_t length;
    vec3_t planevecs[2];

    // from map.c
    VectorSubtract(p0, p1, planevecs[0]);
    VectorSubtract(p2, p1, planevecs[1]);
    CrossProduct(planevecs[0], planevecs[1], plane.normal);
    length = VectorNormalize(plane.normal);
    
    plane.dist = DotProduct(p1, plane.normal);
    
    return plane;
}

static plane_t
PlaneForObjFace(const obj_face_t *face, const obj_model_t *model)
{
    plane_t plane;
    vec_t length;
    vec3_t planevecs[2];
    vec3_t planepts[3];

    // search for 3 consecutive vertices that are not colinear
    int i;
    for (i=0; i<face->numverts; i++) {
	VectorCopy(model->verts[face->vert[i] - 1], planepts[0]);
	VectorCopy(model->verts[face->vert[(i+1)%face->numverts] - 1], planepts[1]);
	VectorCopy(model->verts[face->vert[(i+2)%face->numverts] - 1], planepts[2]);

    // from map.c
    VectorSubtract(planepts[0], planepts[2], planevecs[0]);
    VectorSubtract(planepts[1], planepts[2], planevecs[1]);
    CrossProduct(planevecs[0], planevecs[1], plane.normal);
    length = VectorNormalize(plane.normal);

    plane.dist = DotProduct(planepts[1], plane.normal);

	if (fabs(length - 1) < ZERO_EPSILON)
	    break; // found a good set of verts
    }
    
    return plane;
}

/* ======================================================================== */

// From GtkRadiant

#define M4X4_INDEX( m,row,col ) ( m[( col << 2 ) + row] )
#define M3X3_INDEX( m,row,col ) ( m[( col * 3 ) + row] )
typedef vec_t m4x4_t[16];
typedef vec_t m3x3_t[9];

float m3_det( m3x3_t mat ){
    float det;

    det = mat[0] * ( mat[4] * mat[8] - mat[7] * mat[5] )
        - mat[1] * ( mat[3] * mat[8] - mat[6] * mat[5] )
        + mat[2] * ( mat[3] * mat[7] - mat[6] * mat[4] );

    return( det );
}

int m3x3_invert( m3x3_t matrix )
{
    float det = m3_det( matrix );

    if ( fabs( det ) < 0.0005 )
    {
        return 1;
    }

    m3x3_t ma;
    memcpy( ma, matrix, sizeof( m3x3_t ) );

    matrix[0] =   ( ma[4]*ma[8] - ma[5]*ma[7] )  / det;
    matrix[1] = -( ma[1]*ma[8] - ma[7]*ma[2] ) / det;
    matrix[2] =   ( ma[1]*ma[5] - ma[4]*ma[2] )  / det;

    matrix[3] = -( ma[3]*ma[8] - ma[5]*ma[6] ) / det;
    matrix[4] =  (  ma[0]*ma[8] - ma[6]*ma[2] )  / det;
    matrix[5] = -( ma[0]*ma[5] - ma[3]*ma[2] ) / det;

    matrix[6] =   ( ma[3]*ma[7] - ma[6]*ma[4] )  / det;
    matrix[7] = -( ma[0]*ma[7] - ma[6]*ma[1] ) / det;
    matrix[8] =   ( ma[0]*ma[4] - ma[1]*ma[3] )  / det;

    return 0;
}

typedef vec_t vec4_t[4];

/* ======================================================================== */

static int
STToQuakeTexVec(const vec3_t s, const vec3_t x, const vec3_t y, const vec3_t z, float *texvec) /* 4-vector out: x/y/z-scale, offset */
{
    m3x3_t m;

    M3X3_INDEX(m, 0, 0) = x[0];
    M3X3_INDEX(m, 1, 0) = x[1];
    M3X3_INDEX(m, 2, 0) = x[2];

    M3X3_INDEX(m, 0, 1) = y[0];
    M3X3_INDEX(m, 1, 1) = y[1];
    M3X3_INDEX(m, 2, 1) = y[2];

    M3X3_INDEX(m, 0, 2) = z[0];
    M3X3_INDEX(m, 1, 2) = z[1];
    M3X3_INDEX(m, 2, 2) = z[2];

    if (0 != m3x3_invert(m))
    {
        printf("OBJ: ERROR: Couldn't invert matrix\n");
        return 1;
    }

    /* solution of Ax = b is x=(Ainv)b */

    vec3_t soln = {0,0,0};
    VectorMA(soln, s[0], &m[0], soln);
    VectorMA(soln, s[1], &m[3], soln);
    VectorMA(soln, s[2], &m[6], soln);

    texvec[0] = soln[0];
    texvec[1] = soln[1];
    texvec[2] = soln[2];
    texvec[3] = 0;

    return 0;
}

static texinfo_t
TexinfoForObjFace(const obj_face_t *face, const obj_model_t *model)
{
    texinfo_t tx;
    memset(&tx, 0, sizeof(tx));
    tx.miptex = FindMiptex(face->texture);
    tx.flags = 0;

    /*
    [ u1 ]   [ x1 y1 z1 ] [ s0 ]
    [ u2 ] = [ x2 y2 z2 ] [ s1 ]
    [ u3 ]   [ x3 y3 z3 ] [ s2 ]

    [ v1 ]   [ x1 y1 z1 ] [ t0 ]
    [ v2 ] = [ x2 y2 z2 ] [ t1 ]
    [ v3 ]   [ x3 y3 z3 ] [ t2 ]

    known      known      unknown (quake texture vetors)

    assume s3 and t3 are 0.
    */

    int width, height;
    width = 0;
    height = 0;
    const texture_t *tex = WADList_GetTexture(face->texture);
    if (tex)
    {
        width = tex->width;
        height = tex->height;
        //printf("Got %s has w %d %d\n", face->texture, width, height);
    }
    else
    {
        printf("OBJ: WARNING: Couldn't load texture '%s'\n", face->texture);
    }

    const vec3_t u = {
        model->uvs[face->tex[0] - 1].u * width,
        model->uvs[face->tex[1] - 1].u * width,
        model->uvs[face->tex[2] - 1].u * width
    };

    const vec3_t v = {
        model->uvs[face->tex[0] - 1].v * height,
        model->uvs[face->tex[1] - 1].v * height,
        model->uvs[face->tex[2] - 1].v * height
    };

    const vec3_t x = {
        model->verts[face->vert[0] - 1][0],
        model->verts[face->vert[1] - 1][0],
        model->verts[face->vert[2] - 1][0]
    };

    const vec3_t y = {
        model->verts[face->vert[0] - 1][1],
        model->verts[face->vert[1] - 1][1],
        model->verts[face->vert[2] - 1][1]
    };

    const vec3_t z = {
        model->verts[face->vert[0] - 1][2],
        model->verts[face->vert[1] - 1][2],
        model->verts[face->vert[2] - 1][2]
    };

    if (STToQuakeTexVec(u, x, y, z, tx.vecs[0])
        || STToQuakeTexVec(v, x, y, z, tx.vecs[1])
        || width == 0
        || height == 0)
    {
        printf("OBJ: ERROR: Failed to texture face\n");

        printf("texture: %s\n", face->texture);
        printf("in u: %f %f %f\n", u[0], u[1], u[2]);
        printf("in v: %f %f %f\n", v[0], v[1], v[2]);

        printf("out s: %f %f %f %f\n", tx.vecs[0][0], tx.vecs[0][1], tx.vecs[0][2], tx.vecs[0][3]);
        printf("out t: %f %f %f %f\n", tx.vecs[1][0], tx.vecs[1][1], tx.vecs[1][2], tx.vecs[1][3]);

        tx.vecs[0][0] = 1;
        tx.vecs[0][1] = 0;
        tx.vecs[0][2] = 0;
        tx.vecs[0][3] = 0;

        tx.vecs[1][0] = 0;
        tx.vecs[1][1] = 0;
        tx.vecs[1][2] = -1;
        tx.vecs[1][3] = 0;
    }

    return tx;
}

/*
=================
CreateFacesFromModel
=================
*/
static face_t *
CreateFacesFromModel(const mapentity_t *entity, const obj_model_t *model, vec3_t mins, vec3_t maxs)
{
    int i, j, k;
    vec_t r;
    face_t *f;
    plane_t plane;
    texinfo_t tx;
    face_t *facelist = NULL;    
    vec3_t point;

    for (i = 0; i < 3; i++) {
	mins[i] = VECT_MAX;
	maxs[i] = -VECT_MAX;
    }

    //mapface = hullbrush->faces;
    for (i = 0; i < model->numfaces; i++) {
    	const obj_face_t *face = &model->faces[i];
	// if (!hullnum) {
	//     /* Don't generate hintskip faces */
	//     const texinfo_t *texinfo = pWorldEnt->lumps[LUMP_TEXINFO].data;
	//     const char *texname = map.miptex[texinfo[mapface->texinfo].miptex];
	//     if (!strcasecmp(texname, "hintskip"))
	// 	continue;
	// }

	// w = BaseWindingForPlane(&mapface->plane);
	// mapface2 = hullbrush->faces;
	// for (j = 0; j < hullbrush->numfaces && w; j++, mapface2++) {
	//     if (j == i)
	// 	continue;
	//     // flip the plane, because we want to keep the back side
	//     VectorSubtract(vec3_origin, mapface2->plane.normal, plane.normal);
	//     plane.dist = -mapface2->plane.dist;

	//     w = ClipWinding(w, &plane, false);
	// }
	// if (!w)
	//     continue;		// overconstrained plane

	// this face is a keeper
	f = AllocMem(FACE, 1, true);
	f->w.numpoints = face->numverts;
	// if (f->w.numpoints > MAXEDGES)
	//     Error("face->numpoints > MAXEDGES (%d), source face on line %d",
	// 	  MAXEDGES, mapface->linenum);

	for (j = 0; j < face->numverts; j++) { /* which vertex */
	    for (k = 0; k < 3; k++) { /* which axis */
		point[k] = model->verts[face->vert[face->numverts - 1 - j] - 1][k];
		r = Q_rint(point[k]);
		if (fabs(point[k] - r) < ZERO_EPSILON)
		    f->w.points[j][k] = r;
		else
		    f->w.points[j][k] = point[k];

		if (f->w.points[j][k] < mins[k])
		    mins[k] = f->w.points[j][k];
		if (f->w.points[j][k] > maxs[k])
		    maxs[k] = f->w.points[j][k];
	    }
	}

	// VectorCopy(mapface->plane.normal, plane.normal);
	// VectorScale(mapface->plane.normal, mapface->plane.dist, point);
	// VectorSubtract(point, rotate_offset, point);
	// plane.dist = DotProduct(plane.normal, point);

	// FreeMem(w, WINDING, 1);


    	plane = PlaneForObjFace(face, model);
    	tx = TexinfoForObjFace(face, model);

        if (VectorLength(plane.normal) <= ZERO_EPSILON)
        {
            printf("OBJ: WARNING: Skipping bad face\n");
            FreeMem(f, FACE, 1);
            continue;
        }
        
	f->texinfo = FindTexinfo(&tx);
	f->planenum = FindPlane(&plane, &f->planeside);
	//f->planeside = 1;
	f->next = facelist;
	facelist = f;
	CheckFace(f);
	UpdateFaceSphere(f);
    }

    return facelist;
}

/*
===============
LoadObj

Converts a mapbrush to a bsp brush
===============
*/
static brush_t *
LoadObj(const mapentity_t *entity, const char *filename)
{
    //hullbrush_t hullbrush;
    brush_t *brush;
    face_t *facelist;
    vec3_t mins, maxs;
    obj_model_t *model;
    int i;

    // FIXME: Is it ok to ignore this for OBJ?
    // create the faces
 //    if (mapbrush->numfaces > MAX_FACES)
	// Error("brush->faces >= MAX_FACES (%d), source brush on line %d",
	//       MAX_FACES, mapbrush->faces[0].linenum);

    // hullbrush.srcbrush = mapbrush;
    // hullbrush.numfaces = mapbrush->numfaces;
    // memcpy(hullbrush.faces, mapbrush->faces,
	   // mapbrush->numfaces * sizeof(mapface_t));

    model = LoadObjModel(filename);
    if (model == NULL)
    	return NULL;

    printf("LoadObj: Loaded '%s' with %d faces, %d verts, %d UVs\n", filename,
    	model->numfaces, model->numverts, model->numuvs);

    vec3_t origin;
    VectorCopy(entity->origin, origin);
    // We reset the "origin" key to 0, but the in-memory-copy is still valid
//    GetVectorForKey(entity, "origin", origin);

    vec3_t angles;
    GetVectorForKey(entity, "angles", angles);    

    vec_t angle;
    angle = atof(ValueForKey(entity, "angle"));

    vec_t modelscale = atof(ValueForKey(entity, "_modelscale"));

    // transform the model verts (ugly)
    for (i=0; i<model->numverts; i++)
    {
    	// scale
    	if (modelscale != 0)
    	{
    		VectorScale(model->verts[i], modelscale, model->verts[i]);
    	}

    	// rotate
    	if (angles[0] != 0 || angles[1] != 0 || angles[2] != 0)
    	{
    		RotatePoint(model->verts[i], angles);
    	}
    	else if (angle != 0)
    	{
    		vec3_t angles2 = {0, -angle, 0};
    		RotatePoint(model->verts[i], angles2);
    	}

    	// translate
    	VectorAdd(model->verts[i], origin, model->verts[i]);    	
    }


    facelist = CreateFacesFromModel(entity, model, mins, maxs);
    free(model);

    if (!facelist) {
	Message(msgWarning, warnNoBrushFaces);
	return NULL;
    }

    // create the brush
    brush = AllocMem(BRUSH, 1, true);

    brush->faces = facelist;
    VectorCopy(mins, brush->mins);
    VectorCopy(maxs, brush->maxs);

    return brush;
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
AddBrushPlane(hullbrush_t *hullbrush, plane_t *plane)
{
    int i;
    mapface_t *mapface;
    vec_t len;

    len = VectorLength(plane->normal);
    if (len < 1.0 - NORMAL_EPSILON || len > 1.0 + NORMAL_EPSILON)
	Error("%s: invalid normal (vector length %.4f)", __func__, len);

    mapface = hullbrush->faces;
    for (i = 0; i < hullbrush->numfaces; i++, mapface++) {
	if (VectorCompare(mapface->plane.normal, plane->normal) &&
	    fabs(mapface->plane.dist - plane->dist) < ON_EPSILON)
	    return;
    }
    if (hullbrush->numfaces == MAX_FACES)
	Error("brush->faces >= MAX_FACES (%d), source brush on line %d",
	      MAX_FACES, hullbrush->srcbrush->faces[0].linenum);

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
TestAddPlane(hullbrush_t *hullbrush, plane_t *plane)
{
    int i, c;
    vec_t d;
    mapface_t *mapface;
    vec_t *corner;
    plane_t flip;
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
	if (VectorCompare(p, hullbrush->points[i]))
	    return i;

    if (hullbrush->numpoints == MAX_HULL_POINTS)
	Error("hullbrush->numpoints == MAX_HULL_POINTS (%d), "
	      "source brush on line %d",
	      MAX_HULL_POINTS, hullbrush->srcbrush->faces[0].linenum);

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
    plane_t plane;
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
	      MAX_HULL_EDGES, hullbrush->srcbrush->faces[0].linenum);

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
    plane_t plane;
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
    const mapface_t *mapface;
    const char *texname;
    const texinfo_t *texinfo = pWorldEnt->lumps[LUMP_TEXINFO].data;

    mapface = mapbrush->faces;
    texname = map.miptex[texinfo[mapface->texinfo].miptex];

    if (!strcasecmp(texname, "hint") || !strcasecmp(texname, "hintskip"))
	return CONTENTS_HINT;
    if (!strcasecmp(texname, "clip"))
	return CONTENTS_CLIP;

    if (texname[0] == '*') {
	if (!strncasecmp(texname + 1, "lava", 4))
	    return CONTENTS_LAVA;
	if (!strncasecmp(texname + 1, "slime", 5))
	    return CONTENTS_SLIME;
	return CONTENTS_WATER;
    }

    if (!strncasecmp(texname, "sky", 3))
	return CONTENTS_SKY;

    return CONTENTS_SOLID;
}


/*
===============
LoadBrush

Converts a mapbrush to a bsp brush
===============
*/
static brush_t *
LoadBrush(const mapbrush_t *mapbrush, const vec3_t rotate_offset,
	  const int hullnum)
{
    hullbrush_t hullbrush;
    brush_t *brush;
    face_t *facelist;

    // create the faces
    if (mapbrush->numfaces > MAX_FACES)
	Error("brush->faces >= MAX_FACES (%d), source brush on line %d",
	      MAX_FACES, mapbrush->faces[0].linenum);

    hullbrush.srcbrush = mapbrush;
    hullbrush.numfaces = mapbrush->numfaces;
    memcpy(hullbrush.faces, mapbrush->faces,
	   mapbrush->numfaces * sizeof(mapface_t));

    facelist = CreateBrushFaces(&hullbrush, rotate_offset, hullnum);
    if (!facelist) {
	Message(msgWarning, warnNoBrushFaces);
	return NULL;
    }

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

    // create the brush
    brush = AllocMem(BRUSH, 1, true);

    brush->faces = facelist;
    VectorCopy(hullbrush.mins, brush->mins);
    VectorCopy(hullbrush.maxs, brush->maxs);

    return brush;
}

//=============================================================================


/*
============
Brush_LoadEntity
============
*/
void
Brush_LoadEntity(mapentity_t *dst, const mapentity_t *src, const int hullnum)
{
    const char *classname;
    brush_t *brush, *next, *nonsolid, *solid;
    mapbrush_t *mapbrush;
    vec3_t rotate_offset;
    int i, contents, cflags = 0;

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

    /* Hipnotic rotation */
    VectorCopy(vec3_origin, rotate_offset);
    if (!strncmp(classname, "rotate_", 7)) {
	FixRotateOrigin(dst);
	GetVectorForKey(dst, "origin", rotate_offset);
    }

    /* If the source entity is func_detail, set the content flag */
    if (!strcasecmp(classname, "func_detail"))
	cflags |= CFLAGS_DETAIL;

    mapbrush = src->mapbrushes;
    for (i = 0; i < src->nummapbrushes; i++, mapbrush++) {
	contents = Brush_GetContents(mapbrush);

	/*
	 * "clip" brushes don't show up in the draw hull, but we still want to
	 * include them in the model bounds so collision detection works
	 * correctly.
	 */
	if (contents == CONTENTS_CLIP) {
	    if (!hullnum) {
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
	if (dst != pWorldEnt)
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

//static void PrintPoint(const vec3_t vec)
//{
//    printf("( %f %f %f ) ", vec[0], vec[1], vec[2]);
//}
//
//static void PrintPlane(const vec3_t vec0, const vec3_t vec1, const vec3_t vec2)
//{
//    PrintPoint(vec0);
//    PrintPoint(vec1);
//    PrintPoint(vec2);
//    printf("wbrick1_5 0 0 0 1 1\n");
//}

mapbrush_t *
CreateMapClipBrushForObjFace(face_t *face)
{
    mapbrush_t *result;
    result = AllocMem(MAPBRUSH, 1, true);
    result->numfaces = 4; /* always create a tetrahedron */
    result->faces = AllocMem(MAPFACE, result->numfaces, true);

    if (face->w.numpoints != 3) {
	Error("CreateMapClipBrushForObjFace: currentlly only triangulated .obj files supported.");
    }
    
    // Grab the plane info
    vec3_t normal;
    vec_t dist;
    VectorCopy(map.planes[face->planenum].normal, normal);
    dist = map.planes[face->planenum].dist;
    if (face->planeside) {
	VectorSubtract(vec3_origin, normal, normal);
	dist = -dist;
    }

    // Grab the points
    vec3_t points[3];
    VectorCopy(face->w.points[0], points[0]);
    VectorCopy(face->w.points[1], points[1]);
    VectorCopy(face->w.points[2], points[2]);
    
    /* make back pyramid point */
    vec3_t nadir;
    VectorCopy( points[ 0 ], nadir );
    VectorAdd( nadir, points[ 1 ], nadir );
    VectorAdd( nadir, points[ 2 ], nadir );
    VectorScale( nadir, (1.0f/3.0f), nadir );
    VectorMA( nadir, -1.0f, normal, nadir );
    
    result->faces[0].plane = PlaneForPoints(points[ 2 ], points[ 1 ], nadir );
    result->faces[1].plane = PlaneForPoints(points[ 1 ], points[ 0 ], nadir );
    result->faces[2].plane = PlaneForPoints(points[ 0 ], points[ 2 ], nadir );
    result->faces[3].plane = PlaneForPoints(points[ 0 ], points[ 1 ], points[ 2 ] );

//    printf("{\n");
//    PrintPlane(points[ 2 ], points[ 1 ], nadir );
//    PrintPlane(points[ 1 ], points[ 0 ], nadir );
//    PrintPlane(points[ 0 ], points[ 2 ], nadir );
//    PrintPlane(points[ 0 ], points[ 1 ], points[ 2 ] );
//    printf("}\n");
    
//    FlipPlane(&result->faces[0].plane);
//    FlipPlane(&result->faces[1].plane);
//    FlipPlane(&result->faces[2].plane);
//    FlipPlane(&result->faces[3].plane);
    
    return result;
}


void
Brush_LoadObj(mapentity_t *dst, const mapentity_t *src, const int hullnum, bool bmodel)
{
    brush_t *brush, *next, *nonsolid, *solid;
    vec3_t rotate_offset;
    const char *filename;

    VectorCopy(vec3_origin, rotate_offset);
    
//    if (hullnum != 0) return;

    solid = NULL;
    nonsolid = dst->brushes;

    filename = ValueForKey(src, "_objmodel");

	brush = LoadObj(src, filename);
	if (!brush)
	{
	    printf("WARNING: Failed to load .obj file: '%s'\n", filename);
	    return;
	}
    
    if (hullnum > 0)
    {
//        return;
	printf("doing clip brushes for obj model...\n");
	
	int clipfaces = 0;
	face_t *face;
	for (face = brush->faces; face; face = face->next) {
	    mapbrush_t *mapbrush = CreateMapClipBrushForObjFace(face);

	    // UGLY: overwrites the original brush (but we don't need it anymore)
	    brush = LoadBrush(mapbrush, rotate_offset, hullnum);
	    
	    dst->numbrushes++;
	    brush->contents = CONTENTS_SOLID;
	    brush->cflags = CFLAGS_DETAIL;
	    brush->next = solid;
	    solid = brush;
	    
	    AddToBounds(dst, brush->mins);
	    AddToBounds(dst, brush->maxs);
	    
	    clipfaces++;
	}
	
	printf("Created %d clip faces!\n", clipfaces);
    }
    else
    {
	if (bmodel)
	{
	    // ericw -- reset the origin to 0, so the engine loads it in the right place
	     SetKeyValue(dst, "origin", "0 0 0");
	     SetKeyValue(dst, "classname", "func_wall");
	    // ericw --
	}
	
	// Disable clipping against other brushes in the CSG phase
	// FIXME: This doesn't do anything?
	//if (atoi(ValueForKey(src, "_noclip")))
	{
	    //brush->cflags |= CFLAGS_NOCLIP;
	}
	
	dst->numbrushes++;
	brush->contents = CONTENTS_SOLID;
	brush->cflags = CFLAGS_DETAIL;
	brush->next = solid;
	solid = brush;

	AddToBounds(dst, brush->mins);
	AddToBounds(dst, brush->maxs);
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
