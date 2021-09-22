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
// globals.c

#include <qbsp/qbsp.hh>

/* ------------------------------------------------------------------------ */

mapdata_t map;

// Useful shortcuts
mapentity_t *pWorldEnt() {
    return &map.entities.at(0);
}

// util.c
FILE *logfile;

const char *rgszWarnings[cWarnings] = {
    "No wad or _wad key exists in the worldmodel",
    "No valid WAD filenames in worldmodel",
    "Multiple info_player_start entities",
    "line %d: Brush with duplicate plane",
    "line %d: Brush plane with no normal",
    "No info_player_start entity in level",
    "No info_player_deathmatch entities in level",
    "No info_player_coop entities in level",
    "Line %d: Point (%.3f %.3f %.3f) off plane by %2.4f",
    "Couldn't create brush faces",

    "Reached occupant \"%s\" at (%.0f %.0f %.0f), no filling performed.",
    "Portal siding direction is wrong",
    "New portal was clipped away in CutNodePortals_r near (%.3f %.3f %.3f)",
    "Winding outside node",
    "Winding with area %f",
    "%s isn't a wadfile",
    "Texture %s not found",
    "%s is an invalid option",
    "Unable to open qbsp.log",
    "No entities in empty space -- no filling performed (hull %d)",

    "Strange map face count",
    "Too many edges in TryMerge",
    "Line %d: Healing degenerate edge (%f) at (%.3f %.3f %.3f)",
    "No target for rotation entity \"%s\"",
    "line %d: Face with degenerate QuArK-style texture axes",
    "Mixed face contents (%s, %s) near (%.2f %.2f %.2f)",
    "Ignoring origin brush in worldspawn"
};
