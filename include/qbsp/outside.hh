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

#pragma once

#include <fstream>
#include <vector>
#include <qbsp/brush.hh>
#include <common/qvec.hh>

struct node_t;
struct tree_t;

void WriteLeakTrail(std::ofstream &leakfile, qvec3d point1, const qvec3d &point2);

bool FillOutside(tree_t &tree, hull_index_t hullnum, bspbrush_t::container &brushes);
void MarkBrushSidesInvisible(bspbrush_t::container &brushes);

void FillBrushEntity(tree_t &tree, hull_index_t hullnum, bspbrush_t::container &brushes);

void FillDetail(tree_t &tree, hull_index_t hullnum, bspbrush_t::container &brushes);
