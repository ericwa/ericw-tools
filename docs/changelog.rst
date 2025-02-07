=========
Changelog
=========

2.0.0-alpha9
============

Changes
-------

- qbsp: never merge across liquids, deprecate ``-nomergeacrossliquids``
- qbsp: remove treating ``__TB_empty`` as skip
- qbsp: deprecate :bmodel-key:`_chop` and replace with :bmodel-key:`_chop_order`
- macOS builds now compiled on macOS 14

Features
--------

- light: add :worldspawn-key:`_surflight_atten` key, supported on worldspawn/func_group/func_detail/etc.
- light: add :light-key:`_switchableshadow_target`
- qbsp: add :bmodel-key:`_hulls` bmodel key for omitting specific collision hulls
- lightpreview: add "view -> move camera to" menu item, show Q2 area in statusbar

Bug fixes
---------

- qbsp: fix bmodel bounds for bmodels that mix ``clip`` and non-``clip`` brushes
- qbsp: fix software renderer compatibility (only reuse edges once)
- qbsp: add support for the two missing content flags from re-release (``Q2_CONTENTS_NO_WATERJUMP``,
  ``Q2_CONTENTS_PROJECTILECLIP``)
- qbsp: fix :option:`qbsp -notriggermodels` using incorrect bounds
- qbsp: :classname:`func_illusionary_visblocker` fixes
- qbsp: :option:`qbsp -notex` fixes
- common: fix ``std::filesystem::equivalence`` exception on macOS
- bspinfo: fix lightmap dump
- bsputil: fix :option:`bsputil --extract-entities` and :option:`bsputil --extract-textures` command line parsing
- light: fix :bmodel-key:`_surflight_group`

2.0.0-alpha8
============

Changes
-------

- light: invalid "delay" settings are now a warning rather than a fatal error
- qbsp: q2: write out true leaf contents even if CONTENTS_SOLID is set. Previous
  behaviour (including original qbsp3 compiler) was that CONTENTS_SOLID would
  clear any other set contents bits in leafs (but not in brushes.) (#420)

Features
--------

- lightpreview: show leaf contents in status bar
- light: LIGHTING_E5BGR9 + HDR .lit support (from @dsvensson and @Shpoike)

Bug fixes
---------

- light: fix "mangle" on _sun 1 entities (#266)
- light: fix sunlight artifacts (21b3b696)
- qbsp: q2: fix areaportals which were broken in 2.0.0-alpha7 (70a08013)

2.0.0-alpha7
============

Changes
-------

- light: q2: opaque lit liquids receive light from both sides by default (specifically if they have the "warp" surf
  flag)
- qbsp: remove broken ``-transsky`` feature

Features
--------

- bsputil: gained ``--svg`` export
- lightpreview: add camera bookmarks, stats panel with BSP lump sizes

Bug fixes
---------

- qbsp: fix "sides not found" warning spam on Q1 maps with sky
- light: fix ``-dirtdebug`` causing a .lit file to be generated in Q2 mode
- lightpreview: fix ``-dirtdebug`` broken in Q2 mode (due to the above bug)
- light: reduce excess memory use

Enhancements
------------

- qbsp: liquids are automatically detail (according to ``-transwater``).


2.0.0-alpha6
============

This is the sixth alpha release of our 2.0.0 "dev builds".

The old stable v0.18.2-rc1 release still has more optimized output for Q1 and faster/less memory use for
qbsp/light, but we're working on regressions and welcome feedback as we work towards a stable 2.0.0 build.

Changes since alpha5:

- light: fix "-emissivequality high" having incorrect brightness due to a bad gate setting.
  This will cause different output from 2.0.0-alpha1 through alpha5 when emissives / bounce are in use.
- qbsp: change so liquids don't cause splits in perpendicular walls, by default.
  You can opt back into the old behaviour with "-nomergeacrossliquids" (or setting "_nomergeacrossliquids" "1"
  as a worldspawn key/value). This will be necessary for maps targetting water caustics (e.g. ezQuake),
  otherwise the water caustics effect will extend out of the water.
- packaging: get rid of the "bin" subfolder in the releases

Fixes since alpha5:

- fix macOS builds which were broken since alpha1
- lightpreview: package in macOS + Linux builds (@jonathanlinat)
- light: fix -bounce not being recognized as a shortcut for -bounce 1
- qbsp: make Q2_SURF_ALPHATEST imply TRANSLUCENT + DETAIL
- qbsp: never write 0, 1, or 2-vertex faces
- qbsp: improve tjunc logic to avoid excessive welding (Previously in the alpha builds, and a regression from
  0.18.1, func_detail_wall was welding to structural.)

  The new idea is, translucent things (e.g. translucent water, func_detail_fence, etc.) weld to other translucent
  things, and opaque things (func_detail, worldpsawn, etc.) weld to each other. func_detail_wall is special and
  only welds to itself.
- qbsp: fix .tga/.jpg/.png files with the same names as .wad textures causing corrupt .bsp textures

Enhancements since alpha5:

- docs: fill in a lot of missing documentation

Other notes:

- VS runtime for the Windows builds: https://aka.ms/vs/17/release/vc_redist.x64.exe
- Documentation is now at: https://ericw-tools.readthedocs.io

Upcoming
========

Note
----

- Windows builds are 64-bit only for this release. I'm planning to restore 32-bit support but dropping it for this release made a dependency update easier. Not sure how many users this affects - let me know if it is a problem for you.

Bug fixes
---------

- qbsp: make origin brushes not use hiprotate bounds expansion (c30a0a4)
- qbsp: fix external maps with no worldspawn brushes (5e74b4d)
- qbsp: fixes for ``-wrbrushes`` (cefd20c, 1577981, 209d481)
- qbsp: fix relative paths for .wad files (79d3aa9)
- light: fix ``_shadow`` ``-1`` being interpreted as ``_shadow`` ``1`` for bmodels (b04c06a)
- light: fix "unexpected geomID" error with bounce (dca61f8)

Enhancements
------------

- qbsp: initial multithreading support
- qbsp: experimental Half-Life support (8936594)
- qbsp: more logging of face line numbers for errors/warnings (4ec22ee)
- qbsp: Add support for ``_minlight_excludeN`` where N is in 2..9 (0460165)
- light: support ``_minlight_exclude``, ``_lightignore`` on func_group, detail, etc. (b343b95, a4c1ce6)
- light: add _nostaticlight key (2d3aa22)
- light: add _light_alpha func_group key for making faces translucent for light (d6136c1)
- light: bounce: also make shadow-casting bmodels bounce (unless they opt-out with ``_bounce`` ``-1``) (5010dc9)
- light: support "_suntexture" key on "_sun" "1" entities to limit them to being cast from a specific sky texture. (f220b2f)
- light: support "style" / "targetname" on "_sun" "1" entities. (d2ecc73)
- light: remove 65536 ray limit, fixes #276
- light: support sky faces up to 10^6 units away (ba7bdf8)
- light: allow starting assigning switchable styles before default of 32 (b132152)
- light: add ``_sunlight2``/``_sunlight3`` to be configured with a light entity, similar to "_sun" "1" (fa62b20, d4cc19a)
- light: print key name for ``Key length > MAX_ENT_KEY-1`` error (af4deba)
- vis: temporary hack of raising MAX_PORTALS_ON_LEAF to 512 (e2a5f62)

Removed
-------

- qbsp: -oldleak, -bspleak, -contenthack flags

2019-03-25 ericw-tools 0.18.2-rc1
=================================

Bug fixes
---------

- qbsp: fix "_mirrorinside" on bmodels
- qbsp: fix t-junctions on bmodels (fixes sparkles, lightmap seams, phong shading)
- qbsp: fix a case where func_detail faces were incorrectly deleted during outside-filling
- light: fix seams on "_mirrorinside" geometry (#236)
- light: fix black faces with sunlight pointing straight down
- vis: fix for microleafs blocking vis (e.g. 0.01 units thick). 
  This fixes HOMs appearing when a map was vised. (#261)

Features
--------

- qbsp: support a hybrid Valve 220 texturing + q2/q3 surface flags .map format
- qbsp: added -worldextent option for large maps (deault=65536, meaning +/-65536 units):
- light: Add "_bounce" "-1" model entity key to prevent light from bouncing off those brushes
- light: experimental support for lighting Quake 2 .bsp's

2018-04-05 ericw-tools 0.18.1
=============================

- qbsp: fix crash when worldspawn has 0 brushes
- qbsp: support reading Q2/Q3 detail flag
- qbsp: experimental "_noclipfaces" key
- qbsp: fix "_mirrorinside" on bmodels
- qbsp: improve an error message for when BSP2 is needed
- vis: fix "average leafs visible" message overflowing
- light: fix crash with surface lights

2018-02-18 ericw-tools 0.18
===========================

- light: tweak phong shading to use area and angle weighting
- light: add "_phong_angle_concave" key
- light: fix -bspx option

2018-01-29 ericw-tools 0.17
===========================

- qbsp: fix hint/skip having corrupt texturing with -convert option
- qbsp: warn and heal invalid texture projections
- qbsp: fix -omitdetail to affect all types of detail
- light: warn and ignore invalid texture projections instead of aborting
- light: make more robust against degenerate tris

2017-12-28 ericw-tools 0.16
===========================

- light: add flood-filling to fix black seams in detailwall.map when no -extra/-extra4 used
- light: fix color->greyscale conversion to be compatible with MarkV and some QuakeWorld engines
- light: don't mark sample points inside semi-transparent shadow casters as occluded.
- qbsp: add "_external_map_scale" key for misc_external_map
- qbsp: experimental func_illusionary_visblocker entity
- qbsp: better detection of when a map exceeds BSP29 limits. Previously, a corrupt BSP would be written when
  nodes/leafs exceeded BSP29 limits.
- bsputil: add --convert option. Not very useful, but can convert between BSP2 and 2PSB, for example.
- bsputil: "--check" option logs world mins/maxs
- bsputil, bspinfo: can now read Q2 BSP files

2017-09-17 TyrUtils-ericw 0.15.11
=================================

- light: add "_sun" entity key to configure sunlight in an entity instead of worldspawn.
  More than one "_sun" entity is supported.
- light: add "_falloff" light entity key to configure light falloff in map units.
  Only supported on linear (delay 0) lights.
- light: add "_spotlightautofalloff".
- light: fix light cutoff on curved surfaces (https://github.com/ericwa/tyrutils-ericw/issues/172)
- light: adjust -soft to fix regression in 0.15.10 (https://github.com/ericwa/tyrutils-ericw/issues/171)
- qbsp: add "_mirrorinside" key for mirroring the outside faces of bmodels so they are visible from inside.
  for func_water, or func_illusionary fences, etc.
- qbsp: fix CSG issue with overlapping off grid brushes (https://github.com/ericwa/tyrutils-ericw/issues/174)
- qbsp: fix HOMs introduced in 0.15.10, which were caused by an attempt to fix leaks-through-solids in 0.15.10.
  To re-enable the buggy code that may fix leaks through solids but add HOMs, use "-contenthack"
  (https://github.com/ericwa/tyrutils-ericw/issues/175).

2017-07-30 TyrUtils-ericw 0.15.10
=================================

- light: add "_shadowworldonly" bmodel key - only cast shadows on world, not other bmodels.
- light: switchable bmodel shadows (requires QuakeC support, see light manual).
- light: accept "_minlight" in worldspawn as an alias for "light"
- light: handle degenerate faces, print out the vertex coordinates
- qbsp: misc_external_map prefab system (see qbsp manual)
- qbsp: don't write unused texinfo
- qbsp: rewrite outside filling similar to q3map
- qbsp: revert change to SubdivideFace which was increasing faces a bit (see 53743dd)
- qbsp: add -expand option to dump the hull expansion to a "expanded.map", from q3map
- qbsp: add -leaktest option to abort compilation when a leak is found, from qbsp3
- qbsp: fix handling of duplicate planes, which was causing id1 maps to leak
- qbsp: try to get more reliable leaf content assignment (see a910dd8)
- bsputil: --check: print BSP tree heights at the first few levels of the tree
- bsputil: --check: check for unreferenced texinfo, vertices, planes
- bsputil: --check: print number of used lightstyles
- misc: travis-ci now runs qbsp on all id1 maps, the build fails if any maps leak

2017-06-10 TyrUtils-ericw 0.15.10-beta2
=======================================

- light: styled lights no longer bounce by default, set "_bouncestyled" "1" to enable.
- qbsp: map format conversion: fix reversing of epairs in converted maps
- qbsp: func_detail rewrite to fix vis issues with previous version.
  func_detail no longer seals maps.
- qbsp: add -omitdetail to omit all func_detail entities from the compile
- qbsp: new func_detail_illusionary entity. func_detail variant with no collision
  (players / monsters / gunfire) and doesn't split world faces.
  Doesn't cast shadows unless enabled with "_shadow" "1".
  Useful for hanging vines. Still creates BSP leafs. (Possible
  enhancement: avoid creating new leafs and just insert marksurfaces into
  existing leafs?)
- qbsp: new func_detail_wall entity. func_detail variant that doesn't split
  world faces. Useful for when you want a decoration touching a floor or wall
  to not split the floor/wall faces (you'll get some overdraw instead.)
  If it completely covers up a world face, that face will get clipped away, so
  it's not suitable for fence textures; see func_detail_fence instead.
- qbsp: new func_detail_fence entity. Similar to func_detail_wall except 
  it's suitable for fence textures, never clips away world faces.
  Useful for fences, grates, etc., that are solid and block gunfire.
- qbsp: add -forceprt1 option to generate a .prt file that GTKRadiant's prtview
  plugin can load (but will be unusable by vis).
  When func_detail is in use you normally get a PRT2 file that ptrview
  can't load.
- qbsp, light: allow _shadow -1 to stop a func_detail from casting shadows

2017-03-26 TyrUtils-ericw 0.15.10-beta1
=======================================

- light: fix Linux binary
- light: lights with a lightstyle now bounce
- light: new sample point positioning code
- light: per-light "_bouncescale" key
- qbsp: origin brush support
- qbsp: add -omitdetail option, strips out all func_detail brushes
- qbsp: add -convert option for converting between .MAP formats

2016-11-20 TyrUtils-ericw 0.15.9 release
========================================

- light: fix black fringes on bmodels that are touching against the world
- light: light passing through glass lights up the back side
- light: bmodels with "_alpha" < 1 and "_shadow" "1" set cast tinted shadows
- qbsp: support Quake 3 "Brush Primitives" .MAP format
- qbsp: save "_mincolor" for func_detail/group to the .texinfo file, now used by light 
- qbsp: performance improvements

2016-10-03 TyrUtils-ericw 0.15.8 release
========================================

- light: fix black noise in some cases when using -bounce. (reported by Pritchard)
- light: try to limit artifacts caused by "too many lightstyles on a face", 
  by saving the 4 brightest lightmaps. The previous behaviour was random,
  so you would likely get bad artifacts when that warning occurred.
- light: restore and expand the "unmatched target" warnings.
  Now checks "target", "killtarget", "target2", "angrytarget", "deathtarget".
  Also checks for any "targetname" that is never targetted.
- light: restore support for skip-textured bmodels with "_shadow" "1".
  This is only supported on bmodels where all faces are textured with "skip".
- light: add "_lightignore" model key, makes a model receive minlight only.
- qbsp:  accept absolute path to map (reported by lurq)

2016-09-09 TyrUtils-ericw 0.15.7 release
========================================

Bugfixes
--------

- light: fix shadow-casting bmodels that touch the world from messing up
  sample points on world faces, and prevent the world from messing up
  bmodel sample points (regression in 0.15.5)
- light: clamp lightmap samples to 255 before smoothing, downscaling.
  reduces jaggies in cases with very bright lights casting hard shadows.
- light: fix order of "_project_mangle" value to be consistent with "mangle"
- light: various crash fixes
- light: minlight no longer bounces

Performance
-----------

- light: new, faster raytracing backend (Embree)
- light: estimate visible bounding box each light by shooting rays in a sphere. 
  this gives a speedup similar to vised maps in 0.15.5, without requiring
  the map to be vised. As a downside, there is a small chance of
  introducing artifacts where lights cut off too soon.
  Disable with "-novisapprox".
- light: bounce lighting code redesigned to use less memory

Other
-----

- all: windows builds now require MSVC 2013 runtime:
  https://www.microsoft.com/en-ca/download/details.aspx?id=40784
- all: restore Windows XP support

2016-06-17 TyrUtils-ericw 0.15.6 release
========================================

- rebuild OS X binary as it was built in debug mode by accident

2016-06-10 TyrUtils-ericw 0.15.5 release
========================================

New features
------------

- light: added a better options summary with the -help flag
- light: added -bounce option, "_phong", "_project_texture" key
- light: use vis data to accelerate lighting
- light: "_minlight_exclude" key to exclude a texture from receiving minlight
- light: add "_sun2" "_sun2_color" "_sun2_mangle" which creates a second sun
  (unrelated to "_sunlight2" which is the sky dome light)
- vis: support .prt files written by bjptools-xt
- qbsp: add -objexport flag

Bugfixes
--------

- vis: fix ambient sounds when using func_detail, broken in tyrutils-ericw-v0.15.3

2015-12-10 TyrUtils-ericw 0.15.4 release
========================================

New features
------------

* light: new "-parse_escape_sequences" command-line flag. 
  The "\b" escape sequence toggles red text on/off, you can use this
  in any strings in the map file.
  e.g. "message" "Here is \bsome red text\b..."
* light: new "-surflight_dump" command-line flag. Saves the lights generated
  by surfacelights to a "mapname-surflights.map" file.
* light: new "_sunlight3" and "_sunlight3_color" keys. Same as "_sunlight2", 
  except creates suns on the bottom hemispere ("_sunlight2" creates
  suns on the top hemisphere.)
* build: support compiling with Visual Studio

Bugfixes
--------

* light: fix antilights (broken in last release)
* light: fix _mincolor to accept 0-1 float colors
* light: fix surface lights on rotating bmodels from incorrectly spawning
  lights near the origin
* qbsp: log coordinates for CheckFace errors
* qbsp: round texture coordinates that are close to integers, for Darkplaces
  compatibility
* qbsp: remove 128 char limit on entity key/value values 

2015-10-26 TyrUtils-ericw 0.15.3 release
========================================

* hexen2 support, patch from Spike
* light: add "_surface_spotlight" key for making surface lights into
  spotlights based on the surface normal
* vis: Reuse each cluster's visdata for all leafs in the cluster
* light: add "-sunsamples" flag to control number of samples for
  _sunlight_penumbra and _sunlight2
* qbsp: add "-epsilon" option to control ON_EPSILON, from txqbsp-xt
* light: silence "no model has face" warning generated by "skip" faces.
* light: fix "-gate" (was calculating too-large bounding spheres for delay 2
  lights.)
* qbsp: updates to the "-maxNodeSize" feature added in 0.15.2 to be closer to
  the txqbsp-xt version.
* light: Adjust the trace algorithm to match that in q3map. 
* qbsp: print coordinates for "New portal was clipped away" warning

2015-08-09 TyrUtils-ericw 0.15.2 release
========================================

* qbsp: add "-maxNodeSize" option, from txqbsp-xt. Defaults to 1024. Makes large
  maps process much faster and should generate better bsp trees.
  If it causes a problem disable with "-maxNodeSize 0"
* qbsp: make "mixed face contents" and "degenerate edge" non-fatal, from txqbsp-xt
* qbsp: make "-oldaxis" the default. new "-nooldaxis" flag to get the previous behaviour.
* light: add "-surflight_subdivide" flag to control amount of surface lights created
* light, vis: use below normal process priority on Windows
* light: allow negative surface light offset
* light: average the lit file color components to generate the bsp lightmap value.
  TODO: use a perceptually weighted average.
* light: fix lighting of hipnotic rotating entities.
* light: fix crash in "Bad texture axes on face:"
* light: fix surface lights being mistakenly duplicated
* light: add "-onlyents"
* light: add "-dirtangle" setting to control dirtmapping cone angle, default 88 degrees.

2015-07-09 TyrUtils-ericw 0.15.1 release
========================================

* light: .lux file support from Spike, for deluxemapping
* light: add gamma control with -gamma flag and "_gamma" key
* light: various optimizations
* light: rename -dirty flag to -dirt for consistency
* light: make fence texture tracing opt-in with the "-fence" flag.
  fix an issue with fence texture coords.
* light: support switchable lights with any light* classname, not just "light"
* light: fix debugging spam output from last build

2015-05-01 TyrUtils-ericw snapshot
==================================

* light: fix hang when using _deviance, make _samples default to 16 when
  _deviance is set.
* light: fix for always generating a .lit file when surface lights are used

2015-04-29 TyrUtils-ericw snapshot
==================================

* qbsp: fix broken -onlyents flag
* qbsp: fix texture offset on rotate_object, so they match in the
  editor. Added "-oldrottex" flag to revert to old behaviour. From txqbsp-xt.

2015-04-27 TyrUtils-ericw snapshot
==================================

new features
------------

* light: fence texture tracing, for bmodels with "_shadow" "1"
* light: surface light support via "_surface" "texturename" light key

convenience
-----------

* light: respect "_dirt" "-1" bmodel key in -dirtdebug mode
* light: allow setting "-dist" and "-range" command-line flags in worldspawn
  ("_dist", "_range")
* light: accept "_sunlight_mangle" as an alternative for "_sun_mangle"

other
-----

* all: increase stack size to 8MB. Fixes qbsp crash with bbin1.map on Windows,
  light crashes.
* qbsp: switch to hardcoded MAX_MAP_PLANES (262K), speeds up map file loading
  phase.
* qbsp: MakeFaceEdges: accelerate with a hash table to avoid slow O(n^2) search
  for edges
* qbsp: ChooseMidPlaneFromList: fix off-by-one error in axial plane test. On
  the first SolidBSP pass, gives fewer split nodes on bbin1.map (128k vs 199k)
* light: MatchTargets: disable copying "style" key/value from a light to the
  entity that targets it. Don't see any point, and causes problems if "style"
  is meaningful for the targetting entity (e.g. a monster).

2015-03-05 TyrUtils-ericw shapshot
==================================

* light: support "_dirt" "-1" on bmodels to disable dirtmapping

2015-02-24 TyrUtils-ericw snapshot
==================================

* light: _sunlight2 (sky light/light dome) support from q3map2
* light: _sunlight_penumbra (deviance) from q3map2

2015-01-31 TyrUtils-ericw snapshot
==================================

* light: per-light dirtmapping control

2015-01-21 TyrUtils-ericw snapshot
==================================

* light: revert trace change in TyrUtils 0.7 that was causing artifacts.
  fix bug in determining trace hitpoint

2015-01-19 TyrUtils-ericw snapshot
==================================

* light: handle colours in the range 0-1
* light: ambient occlusion / dirtmapping from q3map2 support
* qbsp: account for miptex struct in wad3 lump disksize
* light: Increase precision of lightmap extents calculations
* qbsp: fix coordinates in degenerate edge error in tjunc.c
* build: bump the fallback version number in Makefile
* bsputil: fix wad export from bsp with missing textures

2014-02-16 TyrUtils v0.15
=========================

* qbsp: Cope with textures names containing '{' or '}' (e.g. for alpha mask)
* qbsp: Increase MAXEDGES limit from 32 to 64
* qbsp: Make transparent water the default (same as txqbsp)
* qbsp: Improve some clip hull errors with map source line numbers
* qbsp: Ignore func_detail entities on -onlyents compiles
* light: Fix bug with minglight clamping with coloured lighting

2013-10-03 TyrUtils v0.14
=========================

* qbsp: Added Quake 2 map compatibility (extra surface attributes ignored)
* qbsp: Add -2psb option to output in RMQ compatible BSP2 format

2013-09-30 TyrUtils v0.13
=========================

* Fix handling of func_group/detail entities with no solid brushes
* Fix automatic adding of animated texture frames

2013-09-29 TyrUtils v0.12
=========================

* Implement the Darkplaces (LordHavoc) style BSP2 format and use as default
* Still support the RMQ style BSP2 format (but don't create any new ones)
* qbsp: Fix bug causing sky brushes to be non-solid

2013-09-24 TyrUtils v0.11
=========================

* Support BSP2 format (qbsp requires the "-bsp2" command line option)
* qbsp: Fix animating texture bug when brushes are textured with alt-animations
* qbsp: Fix a crash in tjunc calculations
* qbsp: Exit with error if verticies exceed 65535 (BSP29 limit)
* qbsp: Add experimental "-forcegoodtree" command line option (thanks Rebb)
* vis: reduce "leaf recursion" error to a warning and continue processing

2013-04-25 TyrUtils v0.10
=========================

* Documentation added for bspinfo and bsputil
* Fix vis bug due to missing vertex copy in v0.9 portal clip changes

2013-04-24 TyrUtils v0.9
========================

* qbsp: fixed bad pointfile generation

2013-04-23 TyrUtils v0.8
========================

* qbsp: fixed surface edge corruption when using skip surfaces
* qbsp: fixed portal generation for transparent water and detail nodes
* qbsp: added "-noskip" option for troubleshooting skip related problems
* light: reduce "no model has face ###" to a warning
* vis: fix portal stack corruption in ClipStackWinding
* bsputil: added a "--check" option (beta!) to check internal data consistency

2013-04-10 TyrUtils v0.7
========================

* Unix man page documentation for the main tools (qbsp, light, vis)
* HTML and text documentation is generated from the man page sources
* qbsp: added support for using WAD3 texture wads used by Hammer
* qbsp: include clip brushes when calculating bmodel bounding box
* qbsp: enable creation of clip-only bmodels
* qbsp: recognise and remove ``*waterskip``, ``*slimeskip`` and ``*lavaskip`` surfaces
* qbsp: added ``hintskip`` texture support
* qbsp: fixed some bugs parsing empty func_group/func_detail entities
* light: implemented self shadowing and full shadows for brush models
* light: implemented the "-soft" command line option
* light: implemented the "-addmin" command line option
* light: implemented the "_anglescale" (aka "_anglesense") key and cmdline
* light: remove support for negative color components (never worked properly)
* light: removed the "-nominlimit" option (now the default behaviour)
* light: removed the "-compress" option (a bad idea from long ago)
* light: make -gate command line affect linear falloff lights as well
* vis: changed the default testlevel to 4
* vis: added the '-noambient*' options to disable auto ambient sounds.

2013-03-07 TyrUtils v0.6
========================

* qbsp: respect floating point texture rotation and shift in map files
* qbsp: support for Valve's 220 map format used in later Worldcraft/Hammer
* qbsp: support func_group entities used by Radiant and similar editors
* qbsp: surfaces with the skip texture are now removed from the compiled bsp
* qbsp: hint brush support similar to Quake 2 for hand-tweaking the PVS
* qbsp: fixed a problem where leak files were not written for hull0 or hull1
* light: fixed a race condition in multithreaded coloured light processing
* light: fixed bug preventing use of all 4 light styles in a common case
* light: implemented attenutation formulae "delay" 4+5, ala Bengt's tools
* light: removed old bsp30 support
* light: lit files now automatically generated when coloured lights detected
* light: implemented 4x4 oversampling with -extra4 command line
* light: implemented the -gate option to help speed processing (default 0.001)
* light: implemented the "_softangle" key for spotlights
* light: implemented minlighting for brush models

2013-02-25 TyrUtils v0.5
========================

* New changelog to summarise changes going forward
* light and vis both now multithreaded on Unix and Windows platforms
* vis now writes a state file every 5 minutes so it can resume if needed
* qbsp and vis now support a form of detail brushes, similar to Quake 2. See
  qbsp.txt for further details.
* added a small optimisation to vis for a minor speedup (usually only 1-2%)
* build system re-written and lots of cleanups all over the code
