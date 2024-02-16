=======
bsputil
=======

bsputil - utiltiy for working with Quake BSP files

Synopsis
========

**bsputil** [OPTION]... BSPFILE

Description
===========

bsputil is a small utility for in-place manipulation of Quake BSP
files.

Options
=======

.. program:: bsputil

.. option:: --scale x y z

   Scale the .bsp by the given scale factors.

   This is experimental, only a few entity properties are edited:

   - ``origin``
   - ``lip``
   - ``height``

.. option:: --replace-entities ENTFILE

   Replaces the .bsp's entity lump with the contents of ENTFILE.

   .. todo::

      Apparently this can handle being BSPFILE being a path to a .MAP file,
      and update one map file given the entities in a second, ENTFILE.

      Move this functionality to :doc:`maputil`?

.. option:: --convert FORMAT

   Convert the input .bsp to one of the given formats:

   - bsp29
   - bsp2
   - bsp2rmq
   - hexen2
   - hexen2bsp2
   - hexen2bsp2rmq
   - hl
   - q2bsp
   - qbism

   This is a "container" conversion only, so most conversions will not be
   usable.

.. option:: --extract-entities

   Extract the entity data from *BSPFILE* and create a plain text
   .ent file. The output filename is generated from *BSPFILE* by
   stripping the .bsp extension and adding the .ent extension.

.. option:: --extract-textures

   Extract the texture data from *BSPFILE* and create a Quake WAD
   file. The output filename is generated from *BSPFILE* by
   stripping the .bsp extension and adding the .wad extension.

.. option:: --replace-textures WADFILE

   Replace any textures in *BSPFILE* with updated versions from *WADFILE*.

   .. note::

      A warning will be issued if any texture sizes don't match between
      *BSPFILE* and *WADFILE*.

.. option:: --check
   
   Load *BSPFILE* into memory and run a set of tests to check that
   all internal data structures are self-consistent. Currently the tests
   are very basic and not all warnings will result in errors from all
   versions of the Quake engine. This option is not targeted at level
   designers, but is intended to assist with development of the qbsp
   tool and check that a "clean" bsp file is generated.

.. option:: --modelinfo

   Print some information on all models in the .bsp.

   .. todo:: Deprecated in favour of the .json export of :doc:`bspinfo`.

.. option:: --findfaces x y z nx ny nz

   Find faces with the given x, y, z coordinates inside the face, and
   the face having the given normal nx, ny, nz.

.. option:: --findleaf x y z

   Prints the leaf at the given coordinates.

.. option:: --settexinfo facenum texinfonum

   Change the texinfo of a given face.

.. option:: --decompile

   Decompile *BSPFILE* to ``BSPFILE.decompile.map``.

.. option:: --decompile-geomonly

   Decompile *BSPFILE* to ``BSPFILE.decompile.map`` without texturing.

.. option:: --decompile-ignore-brushes

   Decompile *BSPFILE* to ``BSPFILE.decompile.map`` without using the Q2 brushes lump.

.. option:: --decompile-hull N

   Decompile only the given hull number to ``BSPFILE.decompile.hullN.map``

.. option:: --extract-bspx-lump LUMPNAME OUTFILENAME

   Write the BSPX lump *LUMPNAME* to *OUTFILENAME*.

.. option:: --insert-bspx-lump LUMPNAME INFILENAME

   Read *INFILENAME* and insert it as a BSPX lump *LUMPNAME* in *BSPFILE*.

.. option:: --remove-bspx-lump LUMPNAME

   Removes *LUMPNAME* from *BSPFILE*.

.. option:: --svg

   Writes a top-down SVG rendering of *BSPFILE*.

Author
======

| Kevin Shanahan (aka Tyrann) - http://disenchant.net
| Eric Wasylishen
| Based on source provided by id Software

Reporting Bugs
==============

| Please post bug reports at
  https://github.com/ericwa/ericw-tools/issues.
| Improvements to the documentation are welcome and encouraged.

Copyright
=========

| Copyright (C) 2017 Eric Wasylishen
| Copyright (C) 2013 Kevin Shanahan
| Copyright (C) 1997 id Software
| License GPLv2+: GNU GPL version 2 or later
| <http://gnu.org/licenses/gpl2.html>.

This is free software: you are free to change and redistribute it. There
is NO WARRANTY, to the extent permitted by law.
