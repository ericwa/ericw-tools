=======
bspinfo
=======

bspinfo - print basic information about a Quake BSP file

Synopsis
========

**bspinfo** BSPFILE [BSPFILES]

Description
===========

**bspinfo** will print various info about each .bsp file:

- BSP type
- list of BSP lumps, the number of objects, and the total size in bytes
- list of BSPX lumps and their sizes in bytes

For the purpose of previewing lightmaps in Blender, it will also pack the lightmaps into an atlas and output
the following:

- ``mapname.bsp.geometry.obj`` containing the BSP faces plus the lightmap UV's
- ``mapname.bsp.lm_0.png``, ``mapname.bsp.lm_1.png``, etc., containing a lightmap atlas per used style number

For debugging, the bsp is also converted into a JSON representation and written to ``mapname.bsp.json``.

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
