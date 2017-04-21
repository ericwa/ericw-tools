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

#ifndef QBSP_TJUNC_HH
#define QBSP_TJUNC_HH

typedef struct wvert_s {
    vec_t t;                    /* t-value for parametric equation of edge */
    struct wvert_s *prev, *next; /* t-ordered list of vertices on same edge */
} wvert_t;

typedef struct wedge_s {
    struct wedge_s *next;       /* pointer for hash bucket chain */
    vec3_t dir;                 /* direction vector for the edge */
    vec3_t origin;              /* origin (t = 0) in parametric form */
    wvert_t head;               /* linked list of verticies on this edge */
} wedge_t;

#endif
