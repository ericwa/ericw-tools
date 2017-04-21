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

typedef struct brush_s {
    struct brush_s *next;
    vec3_t mins, maxs;
    face_t *faces;
    short contents;             /* BSP contents */
    short cflags;               /* Compiler internal contents flags */
    short lmshift;              /* lightmap scaling (qu/lightmap pixel), passed to the light util */
} brush_t;

class mapbrush_t;

brush_t *LoadBrush(const mapbrush_t *mapbrush, const vec3_t rotate_offset, const int hullnum);
void FreeBrushes(brush_t *brushlist);

int FindPlane(const plane_t *plane, int *side);
int PlaneEqual(const plane_t *p1, const plane_t *p2);
int PlaneInvEqual(const plane_t *p1, const plane_t *p2);

#endif
