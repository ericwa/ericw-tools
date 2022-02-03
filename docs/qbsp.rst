====
qbsp
====

qbsp - Compile a Quake BSP file from a MAP source file

Synopsis
--------

**qbsp** [OPTION]... SOURCFILE [DESTFILE]

Description
-----------

:program:`qbsp` is a tool used in the creation of maps for the **id Software**
game **Quake**. qbsp takes a .map file as input and produces a .bsp file
playable in the Quake engine. If the ``DESTFILE`` argument is not
supplied, the output filename will be chosen by stripping the file
extension (if any) from ``SOURCEFILE`` and appending the .bsp extension.

Options
-------

.. program:: qbsp

.. option:: -nofill

   Doesn't perform outside filling

.. option:: -noclip

   Doesn't build clip hulls

.. option:: -noskip

   Doesn't remove faces using the 'skip' texture

.. option:: -onlyents

   Only updates .map entities

.. option:: -verbose

   Print out more .map information

.. option:: -noverbose

   Print out almost no information at all

.. option:: -splitspecial
   
   Doesn't combine sky and water faces into one large face. This allows
   for statically lit water.

.. option:: -transwater

   Computes portal information for transparent water (default)

.. option:: -notranswater

   Computes portal information for opaque water

.. option:: -transsky

   Computes portal information for transparent sky

.. option:: -nooldaxis

   Use alternate texture alignment algorithm. The default is to use the
   original QBSP texture alignment algorithm, which required the
   -oldaxis switch in tyrutils-ericw v0.15.1 and earlier.

.. option:: -forcegoodtree (experimental)

   Force use of expensive processing for SolidBSP stage. Often results
   in a more optimal BSP file in terms of file size, at the expense of
   extra processing time.

.. option:: -bspleak

   Creates a .por file, used in the BSP editor

.. option:: -oldleak

   Create an old-style QBSP .PTS file (default is new)

.. option:: -leaktest

   Makes it a compile error if a leak is detected.

.. option:: -nopercent

   Prevents output of percent completion information

.. option:: -hexen2

   Generate a hexen2 bsp. This can be used in addition to -bsp2 to avoid
   clipnode issues.

.. option:: -bsp2

   Create the output BSP file in BSP2 format. Allows the creation of
   much larger and more complex maps than the original BSP 29 format).

.. option:: -2psb

   Create the output BSP file in 2PSB format. This an earlier version of
   the BSP2 format, supported by the RMQ engine (and thus is also known
   as the BSP2rmq or RMQe bsp format).

.. option:: -hlbsp

   Create the output BSP file in Half-Life's format. Note that the hull
   size differences prevent this from being generally usable for the
   vanilla quake gamecode. This cannot be used in combination with the
   -bsp2 argument.

.. option:: -leakdist [n]

   Space between leakfile points (default 2)

.. option:: -subdivide [n]

   Use different texture subdivision (default 240). Lower values will
   harm framerates. Higher values may not be supported. DP+FTEQW+QSS
   support up to 4080 (unless lightmap scaling is in use), but such
   values will cause other engines to crash-to-console. Use zero to
   specify no subdivision.

.. option:: -wadpath <dir>

   Search this directory for wad files (default is cwd). Multiple
   -wadpath args may be used. This argument is ignored for wads
   specified using an absolute path.

.. option:: -xwadpath <dir>

   Like -wadpath, except textures found using the specified path will
   NOT be embedded into the bsp (equivelent to -notex, but for only
   textures from specific wads). You should use this for wads like
   halflife's standard wad files, but q1bsps require an engine extension
   and players are not nearly as likely to have the same wad version.

.. option:: -oldrottex

   Use old method of texturing rotate\_ brushes where the mapper aligns
   textures for the object at (0 0 0).

.. option:: -maxNodeSize [n]

   Switch to the cheap spatial subdivion bsp heuristic when splitting
   nodes of this size (in any dimension). This gives much faster qbsp
   processing times on large maps and should generate better bsp trees
   as well. From txqbsp-xt, thanks rebb. (default 1024, 0 to disable)

.. option:: -wrbrushes

   (bspx) Includes a list of brushes for brush-based collision. This
   allows for arbitrary collision sizes in engines that support it,
   currently only FTEQW.

.. option:: -wrbrushesonly

   "-wrbrushes" combined with "-noclip" argument. This is NOT backwards
   compatible.

.. option:: -notex

   Write only placeholder textures, to depend upon replacements. This
   avoids inclusion of third-party copyrighted images inside your maps,
   but is not backwards compatible but will work in FTEQW and QSS.

.. option:: -notjunc

   Don't attempt to fix T-junctions. This is only for engines or formats
   that prefer micro-cracks over degenerate triangles. If you don't know
   what that means, don't set this.

.. option:: -omitdetail

   Detail brushes are omitted from the compile.

.. option:: -convert <fmt>

   Convert a .MAP to a different .MAP format. fmt can be: quake, quake2,
   valve, bp (brush primitives). Conversions to "quake" or "quake2"
   format may not be able to match the texture alignment in the source
   map, other conversions are lossless. The converted map is saved to
   <source map name>-<fmt>.map.

.. option:: -includeskip

   Emit skip/nodraw faces. Mainly for Q2RTX.

.. option:: -threads n

   Set number of threads to use. By default, qbsp will attempt to
   use all available hardware threads.

Special Texture Names
---------------------

The contents inside a brush depend on the texture name(s) assigned to
it.

By default brush contents are solid unless they have a special name.
Names beginning with an asterisk are liquids. A prefix of *\*slime*
indicates slime, *\*lava* is for lava and anything else beginning with
*\** will have contents as water.

All faces of a brush must have textures which indicate the same
contents. Mixed content types will cause qbsp to print an error and
exit.

skip
^^^^

Any surfaces assigned a texture name of *skip* will be compiled into the
bsp as invisible surfaces. Solid surfaces will still be solid (e.g. the
play can't walk or shoot through them) but they will not be drawn.
Water, slime and lava surfaces can be made invisible using the texture
names *\*waterskip*, *\*slimeskip* and *\*lavaskip* respectively.

hint
^^^^

Hint surfaces cause a bsp split and portal to be generated the on the
surface plane, after which they are removed from the final bsp - they
are neither visible, nor structural. Strategic placement of hint
surfaces can be used by a map author to optimise the PVS calculations so
as to limit overdraw by the engine (see also: **vis**\ (1)).

Use a texture with the name *hintskip* on any surfaces of a hint brush
which you don't want to generate bsp splits or portals. All surfaces of
a hint brush must use either the *hint* or *hintskip* texture name.

origin
^^^^^^

An origin brush (all faces textured with "origin") can be added to a
brush entity (but not detail or compiler-internal entities like
func_group). Doing so causes all of the brushes in the brush entitiy to
be translated so the center of the origin brush lines up with 0 0 0. The
entity key "origin" is then automatically set on the brush entity to the
original cooridnates of the center of the "origin" brush before it was
translated to 0 0 0.

In Hexen 2, origin brushes are the native way of marking the center
point of the rotation axis for rotating entities.

In Quake, origin brushes can be used to make some map hacks easier to
set up that would otherwise require placing brushes at the world origin
and entering an "origin" value by hand.

Note that, unlike the Hipnotic rotation support in QBSP, using origin
brushes does not cause the model bounds to be expanded. (With Hipnotic
rotation this was to ensure that the model is not vis culled, regardless
of its rotated angle.) Origin brushes are useful for more than just
rotation, and doing this bounds expansion would break some use cases, so
if you're going to rotate a model with an origin brush you might need to
expand the bounds of it a bit using clip brushes so it doesn't get vis
culled.

External Map Prefab Support
---------------------------

This qbsp has a prefab system using a point entity named
"misc_external_map". The idea is, each "misc_external_map" imports
brushes from an external .map file, applies rotations specified by the
"_external_map_angles" key, then translates them to the "origin" key of
the "misc_external_map" entity. Finally, the classname of the
"misc_external_map" is switched to the one provided by the mapper in the
"_external_map_classname" key. (The "origin" key is also cleared to "0 0
0" before saving the .bsp).

The external .map file should consist of worldspawn brushes only,
although you can use func_group for editing convenience. Brush entities
are merged with the worldspawn brushes during import. All worldspawn
keys, and any point entities are ignored. Currently, this means that the
"wad" key is not handled, so you need to add any texture wads required
by the external .map file to your main map.

Note that you can set other entity keys on the "misc_external_map" to
configure the final entity type. e.g. if you set
"_external_map_classname" to "func_door", you can also set a
"targetname" key on the "misc_external_map", or any other keys for
"func_door".

\_external_map
   Specifies the filename of the .map to import.

\_external_map_classname
   What entity you want the external map to turn in to. You can use
   internal qbsp entity types such as "func_detail", or a regular bmodel
   classname like "func_wall" or "func_door".

\_external_map_angles
   Rotation for the prefab, "pitch yaw roll" format. Assuming the
   exernal map is facing the +X axis, positive pitch is down. Yaw of
   180, for example, would rotate it to face -X.

\_external_map_angle
   Short version of "_external_map_angles" for when you want to specify
   just a yaw rotation.

\_external_map_scale
   Scale factor for the prefab, defaults to 1. Either specify a single
   value or three scales, "x y z".

Detail Brush Support
--------------------

This version of qbsp supports detail brushes which are similar in
concept to Quake 2's detail brushes. They don't seal the map (previous
versions did).

To be compatible with existing Quake 1 mapping tools, detail brushes can
be added by creating an entity with classname "func_detail". When qbsp
reads the map file, it will add any brushes included in a func_detail
entity into the worldspawn as details and remove the func_detail entity.
Any number of func_detail entities can be used (useful for grouping) and
all included brushes will be added to the worldspawn.

Here is an example entity definition suitable to add the the .QC files
used by BSP Editor:

::

       /*QUAKED func_detail (0.5 0.5 0.9) ?
       Detail brushes add visual details to
       the world, but do not block visibility.
       func_detail entities are merged into
       the worldspawn entity by the qbsp compiler
       and do not appear as separate entities in
       the compiled bsp.
       */

For WorldCraft in .FGD format (untested):

::

       @SolidClass color(128 128 230) = func_detail: "Detail" []

For Radiant in .ENT format:

::

       <group name="func_detail" color="0 .5 .8">
       Detail brushes add visual details to the world, but do not
       block visibility. func_detail entities are merged into the
       worldspawn entity by the qbsp compiler and do not appear as
       separate entities in the compiled bsp.
       </group>

What should be written to the .map file is a simple entity with one or
more brushes. E.g.:

::

       {
       "classname" "func_detail"
       {
       ( -176  80  0 ) ( -208  80  0 ) ( -208  48  0 ) COP1_1 0 0 0 1.0 1.0
       ( -192 -80 64 ) ( -208 -80  0 ) ( -192 -64 64 ) COP1_1 0 0 0 1.0 1.0
       ( -176 -80  0 ) ( -192 -80 64 ) ( -176 -64  0 ) COP1_1 0 0 0 1.0 1.0
       ( -16   48  0 ) (  -16  64 64 ) (    0  48  0 ) COP1_1 0 0 0 1.0 1.0
       ( -16   64 64 ) (  -16  80  0 ) (    0  64 64 ) COP1_1 0 0 0 1.0 1.0
       }
       }

When qbsp detects detail brushes, it outputs a modified portal file
format with the header PRT2 (default is PRT1). This portal file contains
extra information needed by vis to compute the potentially visible set
(PVS) for the map/bsp. So you will also need a vis util capable of
processing the PRT2 file format.

Detail Variants
---------------

func_detail_illusionary
^^^^^^^^^^^^^^^^^^^^^^^

func_detail variant with no collision (players / monsters / gunfire) and
doesn't split world faces. Doesn't cast shadows unless enabled with
"_shadow" "1". Useful for hanging vines. Still creates BSP leafs, which
is unavoidable without a new .bsp file format.

Intersecting func_detail_illusionary brushes don't clip each other; this
is intended to make trees/shrubs/foliage easier with "_mirrorinside"
"1".

func_detail_wall
^^^^^^^^^^^^^^^^

func_detail variant that doesn't split world faces. Useful for when you
want a decoration touching a floor or wall to not split the floor/wall
faces (you'll get some overdraw instead.) If it completely covers up a
world face, that face will get clipped away, so it's not suitable for
fence textures; see func_detail_fence instead.

Intersecting func_detail_wall brushes don't clip each other.

func_detail_fence
^^^^^^^^^^^^^^^^^

Similar to func_detail_wall except it's suitable for fence textures,
never clips away world faces. Useful for fences, grates, etc., that are
solid and block gunfire.

Intersecting func_detail_fence brushes don't clip each other.

Model Entity Keys
-----------------

"_lmscale" "n"
   Generates an LMSHIFT bspx lump for use by a light util. Note that
   both scaled and unscaled lighting will normally be used.

"_mirrorinside" "n"
   Set to 1 to save mirrored inside faces for bmodels, so when the
   player view is inside the bmodel, they will still see the faces.
   (e.g. for func_water, or func_illusionary)

Other Special-Purpose Entities
------------------------------

func_illusionary_visblocker
^^^^^^^^^^^^^^^^^^^^^^^^^^^

For creating vis-blocking illusionary brushes (similar to
"func_detail_illusionary" or "func_illusionary". The player can walk
through them.) This gives the same effect as water brushes when the
"-notranswater" flag is used, except the interior of these brushes are
saved as CONTENTS_EMPTY. One thing to be aware of is, if the player's
view is very close to the faces of these brushes, they might be able to
see into the void (depending on the engine). Fitzquake family engines
have a workaround for this that is enabled if the brushes are textured
with a water texture ("*" prefix).

Map Compatibility
-----------------

In addition to standard Quake 1 .map files, ericw-tools QBSP is
compatible with:

-  Floating point brush coordinates and texture alignments

-  Valve's 220 map format as produced by the *Hammer* editor

-  Extended texture positioning as supported by the *QuArK* editor

-  Standard Quake 2 map format (leading paths in texture names are
   stripped and any extra surface properties are ignored)

-  Brush Primitives produce by Radiant editors (normally a Quake 3
   format)

Author
------

| Eric Wasylishen
| Kevin Shanahan (aka Tyrann) - http://disenchant.net
| Based on source provided by id Software and Greg Lewis

Reporting Bugs
--------------

| Please post bug reports at
  https://github.com/ericwa/ericw-tools/issues.
| Improvements to the documentation are welcome and encouraged.

Copyright
---------

| Copyright (C) 2017 Eric Wasylishen
| Copyright (C) 2013 Kevin Shanahan
| Copyright (C) 1997 Greg Lewis
| Copyright (C) 1997 id Software
| License GPLv2+: GNU GPL version 2 or later
| <http://gnu.org/licenses/gpl2.html>.

This is free software: you are free to change and redistribute it. There
is NO WARRANTY, to the extent permitted by law.

See Also
--------

**light**\ (1) **vis**\ (1) **bspinfo**\ (1) **bsputil**\ (1)
**quake**\ (6)
