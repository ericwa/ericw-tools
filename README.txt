tyrutils-ericw-spike

Added features:
qbsp:
	parser: string escape verification.
	arg: -wrbrushes		(bspx) Includes a list of brushes for brush-based collision
	arg: -wrbrushesonly	-wrbrushes combined with -noclip argument
	arg: -notex		Write only placeholder textures, to depend upon replacements
	field: _smooth		Names a texture within the entity for which lighting should be smoothed (softens columns etc).
	field: _lmscale		Generates an LMSHIFT bspx lump for use by a light util. Note that both scaled and unscaled lighting will normally be used.

light:
	worldfield: _lightmap_scale	Forces all surfaces+submodels to use this specific lightmap scale. Removes LMSHIFT field.
	arg: -lmscale				Equivelent to _lightmap_scale worldspawn key.
	field: _project_texture		Specifies that a light should project this texture.
	field: _project_mangle		Specifies the pitch+yaw+roll angles for a texture projection (overriding mangle).
	field: _project_fov			Specifies the fov angle for a texture projection.
	arg:	-bspxlit				Writes rgb data into the bsp itself.
	arg: -bspx					Writes both rgb and directions data into the bsp itself.
	arg: -novanilla				Fallback scaled lighting will be omitted. Standard grey lighting will be ommitted if there are coloured lights. Implies -bspxlit. -lit will no longer be implied by the presence of coloured lights.

bspinfo:
	displays bspx lumps.

vis:
	no changes


Updated 2015-07-13

--------------------------
 tyrutils-ericw (v0.15.1)
--------------------------
  Website:         http://ericwa.github.io/tyrutils-ericw
  Maintainer:      Eric Wasylishen (AKA ericw)
  Email:           ewasylishen@gmail.com

Original tyurtils:

  Website: http://disenchant.net
  Author:  Kevin Shanahan (AKA Tyrann)
  Email:   tyrann@disenchant.net


tyrutils-ericw is a branch of Tyrann's quake 1 tools, focused on
adding lighting features, mostly borrowed from q3map2. There are a few
bugfixes for qbsp as well. Original readme follows:

A collection of command line utilities for building Quake levels and working
with various Quake file formats. I need to work on the documentation a bit
more, but below are some brief descriptions of the tools.

Included utilities:

  qbsp    - Used for turning a .map file into a playable .bsp file.

  light   - Used for lighting a level after the bsp stage.
            This util was previously known as TyrLite

  vis     - Creates the potentially visible set (PVS) for a bsp.

  bspinfo - Print stats about the data contained in a bsp file.

  bsputil - Simple tool for manipulation of bsp file data

See the doc/ directory for more detailed descriptions of the various
tools capabilities.  See changelog.txt for a brief overview of recent
changes or git://disenchant.net/tyrutils for the full changelog and
source code.

---------
 License
---------

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
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
