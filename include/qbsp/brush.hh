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

#ifndef QBSP_BRUSH_HH
#define QBSP_BRUSH_HH

#include <qbsp/winding.hh>

typedef struct brush_s {
    struct brush_s *next;
    vec3_t mins, maxs;
    face_t *faces;
    short contents;             /* BSP contents */
    short cflags;               /* Compiler internal contents flags */
    short lmshift;              /* lightmap scaling (qu/lightmap pixel), passed to the light util */
} brush_t;

class mapbrush_t;

plane_t Face_Plane(const face_t *face);

int Brush_ListCountWithCFlags(const brush_t *brush, int cflags);
int Brush_ListCount(const brush_t *brush);
int Brush_NumFaces(const brush_t *brush);

enum class rotation_t {
    none, hipnotic, origin_brush
};

brush_t *LoadBrush(const mapentity_t *src, const mapbrush_t *mapbrush, int contents, const vec3_t rotate_offset, const rotation_t rottype, const int hullnum);
void FreeBrushes(mapentity_t *ent);

int FindPlane(const vec3_t normal, const vec_t dist, int *side);
bool PlaneEqual(const qbsp_plane_t *p1, const qbsp_plane_t *p2);
bool PlaneInvEqual(const qbsp_plane_t *p1, const qbsp_plane_t *p2);

bool BoundBrush (brush_t *brush);
vec_t BrushVolume (const brush_t *brush);
int BrushMostlyOnSide (const brush_t *brush, const vec3_t normal, vec_t dist);
void SplitBrush (const brush_t *brush, int planenum, int planeside, brush_t **front, brush_t **back);

void FilterStructuralBrushesIntoTree( const mapentity_t *e, node_t *headnode );

void FreeBrush(brush_t *brush);

#endif
