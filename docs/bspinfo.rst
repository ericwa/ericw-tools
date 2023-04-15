=======
bspinfo
=======

bspinfo - print basic information about a Quake BSP file

Synopsis
========

**bspinfo** BSPFILE

Description
===========

**bspinfo** will print a very basic summary of the internal data in
*BSPFILE*. The BSP version number is printed, followed by one line for
each of the data types inside, giving the count and data size in bytes
of each data type.

If the filename *BSPFILE* does not have a .bsp extension, **bsputil**
will look for a .bsp file by stripping the file extension from BSPFILE
(if any) and appending ".bsp".

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
