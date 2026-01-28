==========
BSPX Lumps
==========

Here are the BSPX lumps we support. As with other Quake formats, byte order is little endian.

.. bspx-lump:: FACENORMALS

   This adds per-face, per-vertex normals, tangents, and bitangents.

   For maps using :bmodel-key:`_phong`, this allows in-engine dynamic lights to render
   with the same smooth/sharp edges that the baked lighting uses.

   .. code-block:: c

      // first, a table of vectors which are referred to later (for compression)
      uint32_t num_unique_vecs;
      float3 vecs[num_unique_vecs];

      // next, a per-vertex, per-face struct
      struct {
         struct {
            // these all index into `vecs`
            uint32_t normal_index;
            uint32_t tangent_index;
            uint32_t bitangent_index;
         } pervertex[face->numedges];
      } perface[num_dfaces];

   Generated with :option:`light -wrnormals`.

   Supported engines: Quake II (Enhanced).

   See also, ``VERTEXNORMALS`` (which we don't support).

.. todo:: This list is incomplete...
