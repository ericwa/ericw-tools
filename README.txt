Updated 2007-09-25

------------------
 Tyr-Utils (v0.4)
------------------
  Website: http://disenchant.net
  Author:  Kevin Shanahan (AKA Tyrann)
  Email:   tyrann@disenchant.net

A collection of command line utilities for building Quake levels and working
with various Quake file formats. I need to work on the documentation a bit
more, but below are some brief descriptions of the tools.

Included utilities:

  light   - A modified version of id's original light tool. Used for lighting a
            level after the bsp stage. This util is also known as TyrLite; see
            tyrlite.txt for detailed usage instructions.

  vis     - Standard vis util; creates the potentially visible set (PVS) for a
            bsp. This version had been slightly modified from id's version to
            reduce the compile time a little.

  bspinfo - Simple util to print out some stats about the data contained in a
            bsp file.

  bsputil - Tool for working with a bsp file. Not sure where this one is going,
            but for now all it can do is extract the entities lump from a bsp
            file into a text file. Might add a bsp -> wad extractor at some
            stage too.

  qbsp    - A modified version of Greg Lewis' TreeQBSP, which is in turn based
            on id's original qbsp tool. Used for turning a .map file into a
	    playable .bsp file.

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
