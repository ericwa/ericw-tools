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

#ifndef QBSP_WINDING_HH
#define QBSP_WINDING_HH

typedef struct plane {
    vec3_t normal;
    vec_t dist;
    int type;
    int outputplanenum;         // -1=unassigned, only valid after ExportNodePlanes
} qbsp_plane_t;

typedef struct {
    int numpoints;
    vec3_t points[MAXEDGES];            // variable sized
} winding_t;

winding_t *BaseWindingForPlane(const qbsp_plane_t *p);
void CheckWinding(const winding_t *w);
winding_t *NewWinding(int points);
winding_t *CopyWinding(const winding_t *w);
void CopyWindingInto(winding_t *dest, const winding_t *src); // FIXME: get rid of this
winding_t *FlipWinding(const winding_t *w);
winding_t *ClipWinding(winding_t *in, const qbsp_plane_t *split, bool keepon);
void DivideWinding(const winding_t *in, const qbsp_plane_t *split, winding_t **front,
                   winding_t **back);
void MidpointWinding(const winding_t *w, vec3_t v);

/* Helper function for ClipWinding and it's variants */
void CalcSides(const winding_t *in, const qbsp_plane_t *split, int *sides,
               vec_t *dists, int counts[3]);

vec_t WindingArea(const winding_t * w);

void ChopWindingInPlace (winding_t **w, const vec3_t normal, vec_t dist, vec_t epsilon);

#endif
