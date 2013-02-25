Updated 2013-02-25

------------------
 Tyr-Utils (v0.5)
------------------
  Website: http://disenchant.net
  Author:  Kevin Shanahan (AKA Tyrann)
  Email:   tyrann@disenchant.net

A collection of command line utilities for building Quake levels and working
with various Quake file formats. I need to work on the documentation a bit
more, but below are some brief descriptions of the tools.

Included utilities:

  qbsp    - Used for turning a .map file into a playable .bsp file.
            See qbsp.txt for more detailed information.

  light   - Used for lighting a level after the bsp stage. This util is also
            known as TyrLite; see light.txt for detailed usage instructions.

  vis     - Creates the potentially visible set (PVS) for a bsp.
            See vis.txt for more detailed information.

  bspinfo - Simple util to print out some stats about the data contained in a
            bsp file.

  bsputil - Tool for working with a bsp file. Minimal features currently, but
            it can extract the entities lump from a bsp file into a text file
            and extract the textures to a .wad file.

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
