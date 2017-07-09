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

#ifndef QBSP_PORTALS_HH
#define QBSP_PORTALS_HH

typedef struct portal_s {
    int planenum;
    node_t *nodes[2];           // [0] = front side of planenum
    struct portal_s *next[2];   // [0] = next portal in nodes[0]'s list of portals
    winding_t *winding;
} portal_t;

extern node_t outside_node;     // portals outside the world face this

void FreeAllPortals(node_t *node);

#endif
