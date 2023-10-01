=======
maputil
=======

maputil - utiltiy for working with Quake MAP files

Synopsis
========

**bsputil** [OPTION]... MAPFILE

Options
=======

.. program:: maputil

.. option:: --script <path to Lua script file>

   execute the given Lua script.

.. option:: --query \"<Lua expression>\"

   perform a query on entities and print out matching results.
   see docs for more details on globals.
   note that query has the same access as script
   but is more suitable for small read-only operations.

.. option:: --strip_extended_info

   removes extended Quake II/III information on faces.

.. option:: --convert <quake | valve | etp | bp>

   convert the current map to the given format.

.. option:: --save \"<output path>\"

   save the current map to the given output path.

.. option:: --game <quake | quake2 | hexen2 | halflife>

   set the current game; used for certain conversions
   or operations.

Lua layout
==========

::

   entities = table[]
    [E].dict = array
        [D] = [ key, value ]
    [E].brushes = table[]
     [S].texture = string
     [S].plane_points = [ [ x, y, z ] [ x, y, z ] [ x, y, z ] ]
     [S].raw = table (can only contain ONE member:)
         .quaked = table
          .shift = [ x, y ]
          .rotate = number
          .scale = [ x, y ]
         .valve = table
          .axis = [ [ x, y, z ] [ x, y, z ] ]
          .shift = [ x, y ]
          .rotate = number
          .scale = [ x, y ]
         .bp = table
          .axis = [ [ x, y, z ] [ x, y, z ] ]
         .etp = table
          .shift = [ x, y ]
          .rotate = number
          .scale = [ x, y ]
          .tx2 = boolean
     [S].info = table or nil
         .contents = number
         .value = number
         .flags = number
     [S].plane = [ x, y, z, d ] (read-only)
     [S].vecs = [ [ x, y, z, d ] [ x, y, z, d ] ] (read-only)
