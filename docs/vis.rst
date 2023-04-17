===
vis
===

.. program:: vis

vis calculates the visibility (and hearability) sets for
.BSP files.



Command-line options
====================

Logging
-------

.. option:: -log [0]
            -nolog [0]

   whether log files are written or not

.. option:: -verbose
            -v

   verbose output

.. option:: -nopercent

   don't output percentage messages

.. option:: -nostat

   don't output statistic messages

.. option:: -noprogress

   don't output progress messages

.. option:: -nocolor

   don't output color codes (for TB, etc)

.. option:: -quiet
            -noverbose

   suppress non-important messages (equivalent to -nopercent -nostat -noprogress)

Performance
-----------

.. option:: -threads n

   number of threads to use, maximum; leave 0 for automatic

.. option:: -lowpriority [0]

   run in a lower priority, to free up headroom for other processes

.. option:: -fast

   run very simple & fast vis procedure

Game
----

.. option:: -gamedir "relative/path" or "C:/absolute/path"

   override the default mod base directory. if this is not set, or if it is relative, it will be derived from the input file or the basedir if specified.

.. option:: -basedir "relative/path" or "C:/absolute/path"

   override the default game base directory. if this is not set, or if it is relative, it will be derived from the input file or the gamedir if specified.

.. option:: -filepriority archive | loose

   which types of archives (folders/loose files or packed archives) are higher priority and chosen first for path searching

.. option:: -path "/path/to/folder" <multiple allowed>

   additional paths or archives to add to the search path, mostly for loose files

.. option:: -q2rtx

   adjust settings to best support Q2RTX

.. option:: -defaultpaths [0]
            -nodefaultpaths [0]

   whether the compiler should attempt to automatically derive game/base paths for games that support it

Output
------

.. option:: -noambientsky

   don't output ambient sky sounds

.. option:: -noambientwater

   don't output ambient water sounds

.. option:: -noambientslime

   don't output ambient slime sounds

.. option:: -noambientlava

   don't output ambient lava sounds

.. option:: -noambient

   don't output ambient sounds at all

.. option:: -autoclean [0]
            -noautoclean [0]

   remove any extra files on successful completion

Advanced
--------

.. option:: -level n

   number of iterations for tests

.. option:: -visdist n

   control the distance required for a portal to be considered seen

.. option:: -nostate

   ignore saved state files, for forced re-runs

.. option:: -phsonly

   re-calculate the PHS of a Quake II BSP without touching the PVS

Worldspawn keys
===============

===
vis
===

vis - Compute visibility (PVS) for a Quake BSP file

Synopsis
========

**vis** [OPTION]... BSPFILE

Description
===========

**vis** is a tool used in the creation of maps for the game Quake. vis
looks for a .prt file by stripping the file extension from BSPFILE (if
any) and appending ".prt". vis then calculates the potentially visible
set (PVS) information before updating the .bsp file, overwriting any
existing PVS data.

This vis tool supports the PRT2 format for Quake maps with detail
brushes. See the qbsp documentation for details.

Compiling a map (without the -fast parameter) can take a long time, even
days or weeks in extreme cases. Vis will attempt to write a state file
every five minutes so that progress will not be lost in case the
computer needs to be rebooted or an unexpected power outage occurs.

Options
=======

.. program:: vis

.. option:: -threads n

   Set number of threads explicitly. By default vis will attempt to
   detect the number of CPUs/cores available.

.. option:: -fast

   Skip detailed calculations and calculate a very loose set of PVS
   data. Sometimes useful for a quick test while developing a map.

.. option:: -level n

   Select a test level from 0 to 4 for detailed visibility calculations.
   Lower levels are not necessarily faster in in all cases. It is not
   recommended that you change the default level unless you are
   experiencing problems. Default 4.

.. option:: -v

   Verbose output.

.. option:: -vv

   Very verbose output.

.. option:: -noambientsky

   Disable ambient sound generation for textures with names beginning
   with 'SKY'.

.. option:: -noambientwater

   Disable ambient sound generation for textures with names beginning
   with '*WATER' or '*04WATER'.

.. option:: -noambientslime

   Disable ambient sound generation for textures with names beginning
   with '*SLIME'.

.. option:: -noambientlava

   Disable ambient sound generation for textures with names beginning
   with '*LAVA'.

.. option:: -noambient

   Disable all ambient sound generation.

.. option:: -visdist n
   
   Allow culling of areas further than n units.

Author
======

| Kevin Shanahan (aka Tyrann) - http://disenchant.net
| Eric Wasylishen
| Based on source provided by id Software

Reporting Bugs
==============

| Please post bug reports at
  https://github.com/ericwa/tyrutils-ericw/issues.
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
