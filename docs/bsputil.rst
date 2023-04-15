=======
bsputil
=======

bsputil - utiltiy for working with Quake BSP files

Synopsis
========

**bsputil** [OPTION]... BSPFILE

Description
===========

**bsputil is a small utility for basic manipulation of Quake BSP
files.**

Options
=======

.. program:: bsputil

.. option:: --extract-textures

   Extract the texture data from *BSPFILE*\ **and create a Quake WAD**
   file. The output filename is generated from *BSPFILE*\ **by**
   stripping the .bsp extension and adding the .wad extension.

.. option:: --extract-entities

   Extract the entity data from *BSPFILE*\ **and create a plain** text
   .ent file. The output filename is generated from *BSPFILE* by
   stripping the .bsp extension and adding the .ent extension.

.. option:: --check
   
   Load *BSPFILE*\ **into memory and run a set of tests to check that**
   all internal data structures are self-consistent. Currently the tests
   are very basic and not all warnings will result in errors from all
   versions of the Quake engine. This option is not targeted at level
   designers, but is intended to assist with development of the **qbsp
   tool and check that a "clean" bsp file is generated.**

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
