=====
light
=====

light - Caclulate lightmap data for a Quake BSP file

Synopsis
========

**light** [OPTION]... BSPFILE

Description
===========

**light** reads a Quake .bsp file and calculates light and shadow
information based on the entity definitions contained in the .bsp. The
.bsp file is updated with the new light data upon completion,
overwriting any existing lighting data.

Options
=======

.. program:: light

Note, any of the Worldspawn Keys listed in the next section can be
supplied as command-line options, which will override any setting in
worldspawn.

Logging
-------

.. option:: -log

   Write log files. Enabled by default.

.. option:: -nolog

   Don't write log files.

.. option:: -verbose
            -v

   Verbose output.

.. option:: -nopercent

   Don't output percentage messages.

.. option:: -nostat

   Don't output statistic messages.

.. option:: -noprogress

   Don't output progress messages.

.. option:: -nocolor

   Don't output color codes (for TB, etc).

.. option:: -quiet
            -noverbose

   Suppress non-important messages (equivalent to :option:`-nopercent` :option:`-nostat`
   :option:`-noprogress`)


Game
----

.. option:: -gamedir "relative/path" or "C:/absolute/path"

   Override the default mod base directory. if this is not set, or if it is relative, it will be derived from
   the input file or the basedir if specified.

.. option:: -basedir "relative/path" or "C:/absolute/path"

   Override the default game base directory. if this is not set, or if it is relative, it will be derived
   from the input file or the gamedir if specified.

.. option:: -filepriority archive | loose

   Which types of archives (folders/loose files or packed archives) are higher priority and chosen first
   for path searching.

.. option:: -path "/path/to/folder" <multiple allowed>

   Additional paths or archives to add to the search path, mostly for loose files.

.. option:: -q2rtx

   Adjust settings to best support Q2RTX.

.. option:: -defaultpaths

   Whether the compiler should attempt to automatically derive game/base paths for
   games that support it. Enabled by default.

.. option:: -nodefaultpaths

   Opt out of :option:`-defaultpaths`.

Performance
-----------

.. option:: -lowpriority [0]

   Run in a lower priority, to free up headroom for other processes.

.. option:: -threads n

   Set number of threads explicitly. By default light will attempt to
   detect the number of CPUs/cores available.

.. option:: -extra

   Calculate extra samples (2x2) and average the results for smoother
   shadows.

.. option:: -extra4

   Calculate even more samples (4x4) and average the results for
   smoother shadows.

.. option:: -gate n

   Set a minimum light level, below which can be considered zero
   brightness. This can dramatically speed up processing when there are
   large numbers of lights with inverse or inverse square falloff. In
   most cases, values less than 1.0 will cause no discernible visual
   differences. Default 0.001.

.. option:: -sunsamples [n]

   Set the number of samples to use for :worldspawn-key:`_sunlight_penumbra` and
   :worldspawn-key:`_sunlight2` (sunlight2 may use more or less because of how the suns
   are set up in a sphere). Default 100.

.. option:: -surflight_subdivide [n]

   Configure spacing of all surface lights. Default 16 units. Value must be between 1
   and 8192. In the future I'd like to make this
   configurable per-surface-light.

.. option:: -emissivequality low | high

   For emissive surfaces (both direct light and bounced light), use a single
   point in the middle of the face (low) or subdivide the face into multiple
   points, which provides anti-aliased results and more shadows, at the cost
   of compile time. When using "high", you can use `surflight_subdivide`
   to control the point spacing for better anti-aliasing. Default is low.

Output format options
---------------------

.. option:: -lit

   Force generation of a .lit file, even if your map does not have any
   coloured lights. By default, light will automatically generate the
   .lit file when needed.

.. option:: -world_units_per_luxel n

   Enables output of DECOUPLED_LM BSPX lump.

.. option:: -onlyents

   Updates the entities lump in the bsp. You should run this after
   running qbsp with -onlyents, if your map uses any switchable lights.
   All this does is assign style numbers to each switchable light.

.. option:: -litonly

   Generate a .lit file that is compatible with the .bsp without
   modifying the .bsp. This is meant for tweaking lighting or adding
   colored lights when you can't modify an existing .bsp (e.g. for
   multiplayer maps.) Typically you would make a temporary copy of the
   .bsp, update the lights in the entity lump (e.g. with "qbsp
   -onlyents"), then re-light it with "light -litonly". Engines may
   enforce a restriction that you can't make areas brighter than they
   originally were (cheat protection). Also, styled lights
   (flickering/switchable) can't be added in new areas or have their
   styles changed.

.. option:: -nolighting

   Do all of the stuff required for lighting to work without actually
   performing any lighting calculations. This is mainly for engines that
   don't use the light data, but still need switchable lights, etc.

.. option:: -nolights

   Ignore light entities (only sunlight/minlight).

.. option:: -facestyles n

   Max amount of styles per face; requires BSPX lump if > 4.

.. option:: -exportobj

   Export an .OBJ for inspection.

.. option:: -lmshift n

   Force a specified lmshift to be applied to the entire map; this is useful if you want to re-light a map with
   higher quality BSPX lighting without the sources. Will add the LMSHIFT lump to the BSP.

Postprocessing options
----------------------

.. option:: -soft [n]

   Perform post-processing on the lightmap which averages adjacent
   samples to smooth shadow edges. If n is specified, the algorithm
   will take 'n' samples on each side of the sample point and replace
   the original value with the average. e.g. a value of 1 results in
   averaging a 3x3 square centred on the original sample. 2 implies a
   5x5 square and so on. If -soft is specified, but n is omitted, a
   value will be the level of oversampling requested. If no
   oversampling, then the implied value is 1. :option:`-extra` implies a value
   of 2 and :option:`-extra4` implies 3. Default 0 (off).

Debug modes
-----------

.. option:: -dirtdebug

   Implies :worldspawn-key:`_dirt` "1", and renders just the dirtmap against a fullbright
   background, ignoring all lights in the map. Useful for previewing and
   turning the dirt settings.

.. option:: -phongdebug

   Write normals to lit file for debugging phong shading.

.. option:: -bouncedebug

   Write bounced lighting only to the lightmap for debugging /
   previewing -bounce.

.. option:: -bouncelightsdebug

   Only save bounced emitters lighting to the lightmap.

.. option:: -surflight_dump

   Saves the lights generated by surfacelights to a
   "mapname-surflights.map" file.

.. option:: -visapprox auto | none | rays | vis

   Change approximate visibility algorithm.

   auto
      choose default based on format

   vis
      use BSP vis data (slow but precise).

   rays
      use sphere culling with fired rays (fast but may miss faces).

   none
      Disable approximate visibility culling of lights, which has a small
      chance of introducing artifacts where lights cut off too soon.

.. option:: -novisapprox

   Alias for :option:`-visapprox none`

.. option:: -phongdebug_obj

   Save map as .obj with phonged normals.

.. option:: -debugoccluded

   Save luxel occlusion data to lightmap.

.. option:: -debugneighbours

   Save neighboring faces data to lightmap (requires :option:`-debugface`).

.. option:: -debugmottle

   Save mottle pattern (used by Q2 minlight, when opted in with :bmodel-key:`_minlight_mottle`)
   to lightmap.

.. option:: -debugface x y z

.. option:: -debugvert x y z

.. option:: -highlightseams

Experimental options
--------------------

.. option:: -addmin

   Changes the behaviour of *minlight*. Instead of increasing low light
   levels to the global minimum, add the global minimum light level to
   all style 0 lightmaps. This may help reducing the sometimes uniform
   minlight effect.

.. option:: -lit2

   Force generation of a .lit2 file, even if your map does not have any
   coloured lights.

.. option:: -lux

   Generate a .lux file storing average incoming light directions for
   surfaces. Usable by FTEQW with "r_deluxemapping 1".

.. option:: -bspxlux

   Writes lux data into the bsp itself.

.. option:: -lmscale n

   Equivalent to "_lightmap_scale" worldspawn key.

.. option:: -bspxlit

   Writes rgb data into the bsp itself.

.. option:: -bspx

   Writes both rgb and directions data into the bsp itself.

.. option:: -bspxonly

   Writes both rgb and directions data *only* into the bsp itself.

.. option:: -novanilla

   Fallback scaled lighting will be omitted. Standard grey lighting will
   be omitted if there are coloured lights. Implies :option:`-bspxlit`. :option:`-lit`
   will no longer be implied by the presence of coloured lights.

.. option:: -wrnormals
   
   Writes normal data into the bsp itself.

.. option:: -arghradcompat

   Enable compatibility for Arghrad-specific keys.

.. option:: -radlights "filename.rad"

   Loads a <surfacename> <r> <g> <b> <intensity> file.

.. option:: -lightgrid

   Generates a lightgrid and writes it to a bspx lump (LIGHTGRID_OCTREE).

.. option:: -lightgrid_dist x y z

   Distance between lightgrid sample points, in world units. Controls lightgrid size.

.. option:: -lightgrid_format octree

   Lightgrid BSPX lump to use. Currently there is only one supported format, octree.

Model Entity Keys
=================

Worldspawn Keys
---------------

The following keys can be added to the *worldspawn* entity:

.. worldspawn-key:: "light" "n"
                    "_minlight" "n"

   Set a global minimum light level of "n" across the whole map. This is
   an easy way to eliminate completely dark areas of the level, however
   you may lose some contrast as a result, so use with care. Default 0.

   .. note:: In Q2 mode, minlight uses a 0..1 range.

.. worldspawn-key:: "_minlight_color" "r g b"
                    "_mincolor" "r g b"

   Specify red(r), green(g) and blue(b) components for the colour of the
   minlight. RGB component values are between 0 and 255 (between 0 and 1
   is also accepted). Default is white light ("255 255 255").

.. worldspawn-key:: "_maxlight" "n"

.. worldspawn-key:: "_dist" "n"

   Scales the fade distance of all lights by a factor of n. If n > 1
   lights fade more quickly with distance and if n < 1, lights fade more
   slowly with distance and light reaches further.

.. worldspawn-key:: "_range" "n"

   Scales the brightness range of all lights without affecting their
   fade discance. Values of n > 0.5 makes lights brighter and n < 0.5
   makes lights less bright. The same effect can be achieved on
   individual lights by adjusting both the "light" and "wait"
   attributes.

.. worldspawn-key:: "_sunlight" "n"
                    "_sun_light" "n"

   Set the brightness of the sunlight coming from an unseen sun in the
   sky. Sky brushes (or more accurately bsp leafs with sky contents)
   will emit sunlight at an angle specified by the "_sun_mangle" key.
   Default 0.

.. worldspawn-key:: "_anglescale" "n"
                    "_anglesense" "n"

   Set the scaling of sunlight brightness due to the angle of incidence
   with a surface (more detailed explanation in the "_anglescale" light
   entity key below).

.. worldspawn-key:: "_sunlight_mangle" "yaw pitch roll"
                    "_sun_mangle" "yaw pitch roll"
                    "_sun_angle" "yaw pitch roll"

   Specifies the direction of sunlight using yaw, pitch and roll in
   degrees. Yaw specifies the angle around the Z-axis from 0 to 359
   degrees and pitch specifies the angle from 90 (shining straight up)
   to -90 (shining straight down from above). Roll has no effect, so use
   any value (e.g. 0). Default is straight down ("0 -90 0").

.. worldspawn-key:: "_sun2" "n"

.. worldspawn-key:: "_sun2_color" "x y z"

.. worldspawn-key:: "_sun2_mangle" "x y z"

.. worldspawn-key:: "_sunlight_penumbra" "n"

   Specifies the penumbra width, in degrees, of sunlight. Useful values
   are 3-4 for a gentle soft edge, or 10-20+ for more diffuse sunlight.
   Default is 0.

.. worldspawn-key:: "_sunlight_color" "r g b"
                    "_sun_color" "r g b"

   Specify red(r), green(g) and blue(b) components for the colour of the
   sunlight. RGB component values are between 0 and 255 (between 0 and 1
   is also accepted). Default is white light ("255 255 255").

.. worldspawn-key:: "_sunlight2" "n"

   Set the brightness of a dome of lights arranged around the upper
   hemisphere. (i.e. ambient light, coming from above the horizon).
   Default 0.

.. worldspawn-key:: "_sunlight_color2" "r g b"
                    "_sunlight2_color" "r g b"

   Specifies the colour of \_sunlight2, same format as
   "_sunlight_color". Default is white light ("255 255 255").

.. worldspawn-key:: "_sunlight3" "n"

   Same as "_sunlight2", but for the bottom hemisphere (i.e. ambient
   light, coming from below the horizon). Combine "_sunlight2" and
   "_sunlight3" to have light coming equally from all directions, e.g.
   for levels floating in the clouds. Default 0.

.. worldspawn-key:: "_sunlight_color3" "r g b"
                    "_sunlight3_color" "r g b"

   Specifies the colour of "_sunlight3". Default is white light ("255
   255 255").

.. worldspawn-key:: "_dirt" "n"
                    "_dirty" "n"

   1 enables dirtmapping (ambient occlusion) on all lights, borrowed
   from q3map2. This adds shadows to corners and crevices. You can
   override the global setting for specific lights with the "_dirt"
   light entity key or "_sunlight_dirt", "_sunlight2_dirt", and
   "_minlight_dirt" worldspawn keys. Default is no dirtmapping (-1).

.. worldspawn-key:: "_sunlight_dirt" "n"

   1 enables dirtmapping (ambient occlusion) on sunlight, -1 to disable
   (making it illuminate the dirtmapping shadows). Default is to use the
   value of "_dirt".

.. worldspawn-key:: "_sunlight2_dirt" "n"
   
   1 enables dirtmapping (ambient occlusion) on sunlight2/3, -1 to
   disable. Default is to use the value of "_dirt".

.. worldspawn-key:: "_minlight_dirt" "n"
   
   1 enables dirtmapping (ambient occlusion) on minlight, -1 to disable.
   Default is to use the value of "_dirt".

.. worldspawn-key:: "_dirtmode" "n"

   Choose between ordered (0, default) and randomized (1) dirtmapping.

.. worldspawn-key:: "_dirtdepth" "n"

   Maximum depth of occlusion checking for dirtmapping, default 128.

.. worldspawn-key:: "_dirtscale" "n"

   Scale factor used in dirt calculations, default 1. Lower values (e.g.
   0.5) make the dirt fainter, 2.0 would create much darker shadows.

.. worldspawn-key:: "_dirtgain" "n"

   Exponent used in dirt calculation, default 1. Lower values (e.g. 0.5)
   make the shadows darker and stretch further away from corners.

.. worldspawn-key:: "_dirtangle" "n"

   Cone angle in degrees for occlusion testing, default 88. Allowed
   range 1-90. Lower values can avoid unwanted dirt on arches, pipe
   interiors, etc.

.. worldspawn-key:: "_gamma" "n"

   Adjust brightness of final lightmap. Default 1, >1 is brighter, <1 is
   darker.

.. worldspawn-key:: "_lightmap_scale" "n"

   Forces all surfaces+submodels to use this specific lightmap scale.
   Removes "LMSHIFT" field.

.. worldspawn-key:: "_bounce" "n"

   Non-zero enables bounce lighting, disabled by default. The value is
   the maximum number of bounces to perform.

.. worldspawn-key:: "_bouncescale" "n"

   Scales brightness of bounce lighting, default 1.

.. worldspawn-key:: "_bouncecolorscale" "n"

   Weight for bounce lighting to use texture colors from the map:
   0=ignore map textures (default), 1=multiply bounce light color by
   texture color.

.. worldspawn-key:: "_bouncelightsubdivision" "n"

.. worldspawn-key:: "_surflightscale" "n"

   Scales the surface light emission from Q2 surface lights (excluding sky faces) by this amount.

.. worldspawn-key:: "_surflightskyscale" "n"

   Scales the surface light emission from Q2 sky faces by this amount.

.. worldspawn-key:: "_surflightsubdivision" "n"
                    "_choplight" "n"

.. worldspawn-key:: "_bouncestyled" "n"

   1 makes styled lights bounce (e.g. flickering or switchable lights),
   default is 0, they do not bounce.

.. worldspawn-key:: "_spotlightautofalloff" "n"

   When set to 1, spotlight falloff is calculated from the distance to
   the targeted info_null. Ignored when "_falloff" is not 0. Default 0.

.. worldspawn-key:: "_surflight_radiosity" "n"

   Whether to use Q1-style surface subdivision (0) or Q2-style surface radiosity.

.. worldspawn-key:: "_sky_surface" "x y z"
                    "_sun_surface" "x y z"

.. worldspawn-key:: "_compilerstyle_start" "n"

.. worldspawn-key:: "_compilerstyle_max" "n"

Model Entity Keys
-----------------

The following keys can be used on any entity with a brush model.
"_minlight", "_mincolor", "_dirt", "_phong", "_phong_angle",
"_phong_angle_concave", "_shadow", "_bounce" are supported on
func_detail/func_group as well, if qbsp from these tools is used.

.. bmodel-key:: "_minlight" "n"

   Set the minimum light level for any surface of the brush model.
   Default 0.

   .. note:: Q2 uses a 0..1 scale for this key

.. bmodel-key:: "_minlight_mottle" "n"
                "_minlightMottle" "n"

   Whether minlight should have a mottled pattern. Defaults
   to 0.

.. bmodel-key:: "_minlight_exclude" "texname"

   Faces with the given texture are excluded from receiving minlight on
   this brush model.

.. bmodel-key:: "_minlight_color" "r g b"
                "_mincolor" "r g b"

   Specify red(r), green(g) and blue(b) components for the colour of the
   minlight. RGB component values are between 0 and 255 (between 0 and 1
   is also accepted). Default is white light ("255 255 255").

.. bmodel-key:: "_shadow" "n"

   If n is 1, this model will cast shadows on other models and itself
   (i.e. "_shadow" implies "_shadowself"). Note that this doesn't
   magically give Quake dynamic lighting powers, so the shadows will not
   move if the model moves. Set to -1 on func_detail/func_group to
   prevent them from casting shadows. Default 0.

.. bmodel-key:: "_shadowself" "n"
                "_selfshadow" "n"

   If n is 1, this model will cast shadows on itself if one part of the
   model blocks the light from another model surface. This can be a
   better compromise for moving models than full shadowing. Default 0.

.. bmodel-key:: "_shadowworldonly" "n"

   If n is 1, this model will cast shadows on the world only (not other
   bmodels).

.. bmodel-key:: "_switchableshadow" "n"

   If n is 1, this model casts a shadow that can be switched on/off
   using QuakeC. To make this work, a lightstyle is automatically
   assigned and stored in a key called "switchshadstyle", which the
   QuakeC will need to read and call the "lightstyle()" builtin with "a"
   or "m" to switch the shadow on or off. Entities sharing the same
   targetname, and with "_switchableshadow" set to 1, will share the
   same lightstyle.

   These models are only able to block style 0 light (i.e., non-flickering
   or switchable lights). Flickering or switchable lights will shine
   through the switchable shadow casters, regardless of whether the shadow
   is off or on.

.. bmodel-key:: "_dirt" "n"

   For brush models, -1 prevents dirtmapping on the brush model. Useful
   if the bmodel touches or sticks into the world, and you want to
   prevent those areas from turning black. Default 0.

.. bmodel-key:: "_phong" "n"

   1 enables phong shading on this model with a default \_phong_angle of
   89 (softens columns etc).

.. bmodel-key:: "_phong_angle" "n"

   Enables phong shading on faces of this model with a custom angle.
   Adjacent faces with normals this many degrees apart (or less) will be
   smoothed. Consider setting "_anglescale" to "1" on lights or
   worldspawn to make the effect of phong shading more visible. Use the
   "-phongdebug" command-line flag to save the interpolated normals to
   the lightmap for previewing (use "r_lightmap 1" or "gl_lightmaps 1"
   in your engine to preview.)

.. bmodel-key:: "_phong_angle_concave" "n"

   Optional key for setting a different angle threshold for concave
   joints. A pair of faces will either use "_phong_angle" or
   "_phong_angle_concave" as the smoothing threshold, depending on
   whether the joint between the faces is concave or not.
   "_phong_angle(_concave)" is the maximum angle (in degrees) between
   the face normals that will still cause the pair of faces to be
   smoothed. The minimum setting for "_phong_angle_concave" is 1, this
   should make all concave joints non-smoothed (unless they're less than
   1 degree apart, almost a flat plane.) If it's 0 or unset, the same
   value as "_phong_angle" is used.

.. bmodel-key:: "_lightignore" "n"

   1 makes a model receive minlight only, ignoring all lights /
   sunlight. Could be useful on rotators / trains.

   .. seealso:: `Lighting Channels`_ for a more powerful version of this

.. bmodel-key:: "_bounce" "n"
   
   Set to -1 to prevent this model from bouncing light (i.e. prevents
   its brushes from emitting bounced light they receive from elsewhere.)
   Only has an effect if "_bounce" is enabled in worldspawn.

.. bmodel-key:: "_autominlight" "n"

   "Autominlight" is a feature for automatically choosing a suitable
   minlight color for a bmodel entity (e.g. a func_door), by averaging
   incoming light at the center of the bmodel bounding box.

   Default behaviour is to apply autominlight on occluded luxels only (e.g., for a 
   door that opens vertically upwards, it would apply to the bottom face of the
   door, which is initially pressed against the ground).

   A value of "-1" disables the feature (occluded luxels will be solid black),
   and "1" enables it as a minlight color even on non-occluded luxels.

.. bmodel-key:: "_autominlight_target" "name"

   For autominlight, instead of using the center of the model bounds as the sample point,
   searches for an entity with its "targetname" key set to "name", 
   and use that entity's origin (typically you'd use an "info_null" for this).


Light Entity Keys
=================

Light entity keys can be used in any entity with a classname starting
with the first five letters "light". E.g. "light", "light_globe",
"light_flame_small_yellow", etc.

.. light-key:: "light" "n"

   Set the light intensity. Negative values are also allowed and will
   cause the entity to subtract light cast by other entities. Default
   300.

.. light-key:: "wait" "n"

   Scale the fade distance of the light by "n". Values of n > 1 make the
   light fade more quickly with distance, and values < 1 make the light
   fade more slowly (and thus reach further). Default 1.

.. light-key:: "delay" "n"

   Select an attenuation formaula for the light:

   ::

      0 => Linear attenuation (default)
      1 => 1/x attenuation
      2 => 1/(x^2) attenuation
      3 => No attenuation (same brightness at any distance)
      4 => "local minlight" - No attenuation and like minlight,
            it won't raise the lighting above it's light value.
            Unlike minlight, it will only affect surfaces within
            line of sight of the entity.
      5 => 1/(x^2) attenuation, but slightly more attenuated and
            without the extra bright effect that "delay 2" has
            near the source.

.. light-key:: "_falloff" "n"

   Sets the distance at which the light drops to 0, in map units.

   In this mode, "wait" is ignored and "light" only controls the brightness
   at the center of the light, and no longer affects the falloff distance.

   Only supported on linear attenuation (delay 0) lights currently.

.. light-key:: "_color" "r g b"

   Specify red(r), green(g) and blue(b) components for the colour of the
   light. RGB component values are between 0 and 255 (between 0 and 1 is
   also accepted). Default is white light ("255 255 255").

.. light-key:: "target" "name"

   Turns the light into a spotlight, with the direction of light being
   towards another entity with it's "targetname" key set to "name".

.. light-key:: "mangle" "yaw pitch roll"

   Turns the light into a spotlight and specifies the direction of light
   using yaw, pitch and roll in degrees. Yaw specifies the angle around
   the Z-axis from 0 to 359 degrees and pitch specifies the angle from
   90 (straight up) to -90 (straight down). Roll has no effect, so use
   any value (e.g. 0). Often easier than the "target" method.

.. light-key:: "angle" "n"

   Specifies the angle in degrees for a spotlight cone. Default 40.

.. light-key:: "_softangle" "n"

   Specifies the angle in degrees for an inner spotlight cone (must be
   less than the "angle" cone. Creates a softer transition between the
   full brightness of the inner cone to the edge of the outer cone.
   Default 0 (disabled).

.. light-key:: "targetname" "name"

   Turns the light into a switchable light, toggled by another entity
   targeting it's name.

.. light-key:: "style" "n"

   Set the animated light style. Default 0.

.. light-key:: "_anglescale" "n"
               "_anglesense" "n"

   Sets a scaling factor for how much influence the angle of incidence
   of light on a surface has on the brightness of the surface. *n* must
   be between 0.0 and 1.0. Smaller values mean less attenuation, with
   zero meaning that angle of incidence has no effect at all on the
   brightness. Default 0.5.

.. light-key:: "_dirtscale" "n"
               "_dirtgain" "n"

   Override the global "_dirtscale" or "_dirtgain" settings to change
   how this light is affected by dirtmapping (ambient occlusion). See
   descriptions of these keys in the worldspawn section.

.. light-key:: "_dirt" "n"

   Overrides the worldspawn setting of "_dirt" for this particular
   light. -1 to disable dirtmapping (ambient occlusion) for this light,
   making it illuminate the dirtmapping shadows. 1 to enable ambient
   occlusion for this light. Default is to defer to the worldspawn
   setting.

.. light-key:: "_deviance" "n"

   Split up the light into a sphere of randomly positioned lights within
   radius "n" (in world units). Useful to give shadows a wider penumbra.
   "_samples" specifies the number of lights in the sphere. The "light"
   value is automatically scaled down for most lighting formulas (except
   linear and non-additive minlight) to attempt to keep the brightness
   equal. Default is 0, do not split up lights.

.. light-key:: "_samples" "n"

   Number of lights to use for "_deviance". Default 16 (only used if
   "_deviance" is set).

.. light-key:: "_surface" "texturename"

   Makes surfaces with the given texture name emit light, by using this
   light as a template which is copied across those surfaces. Lights are
   spaced about 128 units (though possibly closer due to bsp splitting)
   apart and positioned 2 units above the surfaces.

.. light-key:: "_surface_offset" "n"

   Controls the offset lights are placed above surfaces for "_surface".
   Default 2.

.. light-key:: "_surface_spotlight" "n"

   For a surface light template (i.e. a light with "_surface" set),
   setting this to "1" makes each instance into a spotlight, with the
   direction of light pointing along the surface normal. In other words,
   it automatically sets "mangle" on each of the generated lights.

.. light-key:: "_project_texture" "texture"

   Specifies that a light should project this texture. The texture must
   be used in the map somewhere.

.. light-key:: "_project_mangle" "yaw pitch roll"

   Specifies the yaw/pitch/roll angles for a texture projection
   (overriding mangle).

.. light-key:: "_project_fov" "n"

   Specifies the fov angle for a texture projection. Default 90.

.. light-key:: "_bouncescale" "n"

   Scales the amount of light that is contributed by bounces. Default is
   1.0, 0.0 disables bounce lighting for this light.

.. light-key:: "_sun" "n"

   Set to 1 to make this entity a sun, as an alternative to using the
   sunlight worldspawn keys. If the light targets an info_null entity,
   the direction towards that entity sets sun direction. The light
   itself is disabled, so it can be placed anywhere in the map.

   The following light properties correspond to these sunlight settings:

   ::

      light       => _sunlight
      mangle      => _sunlight_mangle
      deviance    => _sunlight_penumbra
      _color      => _sunlight_color
      _dirt       => _sunlight_dirt
      _anglescale => _anglescale
      style       => flicker style for styled sunlight
      targetname  => targetname for switchable sunlight
      _suntexture => this sunlight is only emitted from faces with this texture name

.. light-key:: "_sunlight2" "n"

   Set to 1 to make this entity control the upper dome lighting emitted
   from sky faces, as an alternative to the worldspawn key :worldspawn-key:`_sunlight2`.
   The light entity itself is disabled, so it can be placed anywhere in
   the map.

   The following light properties correspond to these sunlight settings:

   light
      _sunlight2

   _color
      _sunlight2_color

   _dirt
      _sunlight2_dirt

   _anglescale
      _anglescale

   style
      flicker style for styled dome light

   targetname
      targetname for switchable sunlight

   _suntexture
      this sunlight is only emitted from faces with this texture name

.. light-key:: "_sunlight3" "n"

   Same as :light-key:`_sunlight2`, but for the lower hemisphere.

.. light-key:: "_nostaticlight" "n"

   Set to 1 to make the light compiler ignore this entity (prevents it
   from casting any light). e.g. could be useful with rtlights.

Lighting Channels
=================

Lighting channels allow custom lighting setups where certain light entities only affect certain bmodels. Useful
for lighting rotators, doors, etc.

.. note:: Currently, bounced light, surface lights, and sunlight are always on channel 1.

Light Keys
----------

.. light-key:: "_light_channel_mask" "n"

   Mask of lighting channels that the light casts on.

   In order for this light to cast light on a bmodel, there needs to be a least 1 bit in common between
   :light-key:`_light_channel_mask` and the receiving bmodel's :bmodel-key:`_object_channel_mask` (i.e. the bitwise AND must be nonzero).

   Default 1.

.. light-key:: "_shadow_channel_mask" "n"

   This is the mask of lighting channels that will block this entity's light rays. If the the bitwise AND of this
   and another bmodel's :bmodel-key:`_object_channel_mask` is nonzero, the light ray is stopped.

   This is an advanced option, for making bmodels only cast shadows for specific lights (but not others).

   Defaults to :light-key:`_light_channel_mask`

Model Keys
----------

.. bmodel-key:: "_object_channel_mask" "n"

   Mask of lighting channels that this bmodel receives light on, blocks light on, and tests for AO on.

   Default 1.

   .. note:: Changing this from 1 will disable bouncing light off of this bmodel.

   .. note:: Changing this from 1 implicitly enables :bmodel-key:`_shadow`.

   .. note::

      Changing to 2, for example, will cause the bmodel to initially be solid black. You'll need to add minlight or lights
      with :light-key:`_light_channel_mask` ``2``.

Other Information
=================

The ``\b`` escape sequence toggles red text on/off, you can use this in
any strings in the map file. e.g. ``"message" "Here is \bsome red
text\b..."``

Author
======

| Eric Wasylishen
| Kevin Shanahan (aka Tyrann) - http://disenchant.net
| David Walton (aka spike)
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
