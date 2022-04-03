/*  Copyright (C) 1996-1997  Id Software, Inc.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

    See file, 'COPYING', for details.
*/

#include <cfloat>

#include <vis/vis.hh>
#include <common/bsputils.hh>

/*

Some textures (sky, water, slime, lava) are considered ambien sound emiters.
Find an aproximate distance to the nearest emiter of each class for each leaf.

*/

/*
  ====================
  SurfaceBBox
  ====================
*/
static aabb3d SurfaceBBox(const mbsp_t *bsp, const mface_t *surf)
{
    aabb3d bounds;

    for (int32_t i = 0; i < surf->numedges; i++) {
        int32_t edgenum = bsp->dsurfedges[surf->firstedge + i], vertnum;

        if (edgenum >= 0)
            vertnum = bsp->dedges[edgenum][0];
        else
            vertnum = bsp->dedges[-edgenum][1];

        bounds += bsp->dvertexes[vertnum];
    }

    return bounds;
}

/*
  ====================
  CalcAmbientSounds
  ====================
*/
void CalcAmbientSounds(mbsp_t *bsp)
{
    const mface_t *surf;
    const gtexinfo_t *info;
    int i, j, k, l;
    mleaf_t *leaf, *hit;
    uint8_t *vis;
    float d, maxd;
    int ambient_type;
    float dists[NUM_AMBIENTS];
    float vol;

    for (i = 0; i < portalleafs_real; i++) {
        leaf = &bsp->dleafs[i + 1];

        //
        // clear ambients
        //
        for (j = 0; j < NUM_AMBIENTS; j++)
            dists[j] = 1020;

        if (portalleafs != portalleafs_real) {
            vis = &uncompressed[leaf->cluster * leafbytes_real];
        } else {
            vis = &uncompressed[i * leafbytes_real];
        }

        for (j = 0; j < portalleafs_real; j++) {
            if (!(vis[j >> 3] & (1 << (j & 7))))
                continue;

            //
            // check this leaf for sound textures
            //
            hit = &bsp->dleafs[j + 1];

            for (k = 0; k < hit->nummarksurfaces; k++) {
                surf = BSP_GetFace(bsp, bsp->dleaffaces[hit->firstmarksurface + k]);
                info = &bsp->texinfo[surf->texinfo];
                const auto &miptex = bsp->dtex.textures[info->miptex];

                if (!Q_strncasecmp(miptex.name.data(), "sky", 3) && !options.noambientsky.value())
                    ambient_type = AMBIENT_SKY;
                else if (!Q_strncasecmp(miptex.name.data(), "*water", 6) && !options.noambientwater.value())
                    ambient_type = AMBIENT_WATER;
                else if (!Q_strncasecmp(miptex.name.data(), "*04water", 8) && !options.noambientwater.value())
                    ambient_type = AMBIENT_WATER;
                else if (!Q_strncasecmp(miptex.name.data(), "*slime", 6) && !options.noambientslime.value())
                    ambient_type = AMBIENT_WATER; // AMBIENT_SLIME;
                else if (!Q_strncasecmp(miptex.name.data(), "*lava", 5) && !options.noambientlava.value())
                    ambient_type = AMBIENT_LAVA;
                else
                    continue;

                // find distance from source leaf to polygon
                aabb3d bounds = SurfaceBBox(bsp, surf);
                maxd = 0;
                for (l = 0; l < 3; l++) {
                    if (bounds.mins()[l] > leaf->maxs[l])
                        d = bounds.mins()[l] - leaf->maxs[l];
                    else if (bounds.maxs()[l] < leaf->mins[l])
                        d = leaf->mins[l] - bounds.mins()[l];
                    else
                        d = 0;
                    if (d > maxd)
                        maxd = d;
                }

                maxd = 0.25;
                if (maxd < dists[ambient_type])
                    dists[ambient_type] = maxd;
            }
        }

        for (j = 0; j < NUM_AMBIENTS; j++) {
            if (dists[j] < 100)
                vol = 1.0;
            else {
                vol = (vec_t)(1.0 - dists[2] * 0.002);
                if (vol < 0)
                    vol = 0;
            }
            leaf->ambient_level[j] = (uint8_t)(vol * 255);
        }
    }
}

/*
================
CalcPHS

Calculate the PHS (Potentially Hearable Set)
by ORing together all the PVS visible from a leaf
================
*/
void CalcPHS(mbsp_t *bsp)
{
    const int32_t leafbytes = (portalleafs + 7) >> 3;
    const int32_t leaflongs = leafbytes / sizeof(long);

    // increase the bits size with approximately how much space we'll need
    bsp->dvis.bits.reserve(bsp->dvis.bits.size() * 2);

    // FIXME: should this use alloca?
    uint8_t *uncompressed = new uint8_t[leafbytes];
    uint8_t *uncompressed_2 = new uint8_t[leafbytes];
    uint8_t *compressed = new uint8_t[leafbytes * 2];
    uint8_t *uncompressed_orig = new uint8_t[leafbytes];

    int32_t count = 0;
    for (int32_t i = 0; i < portalleafs; i++) {
        const uint8_t *scan = bsp->dvis.bits.data() + bsp->dvis.get_bit_offset(VIS_PVS, i);

        DecompressRow(scan, leafbytes, uncompressed);
        memset(uncompressed_orig, 0, leafbytes);
        memcpy(uncompressed_orig, uncompressed, leafbytes);

        scan = uncompressed_orig;

        for (int32_t j = 0; j < leafbytes; j++) {
            uint8_t bitbyte = scan[j];
            if (!bitbyte)
                continue;
            for (int32_t k = 0; k < 8; k++) {
                if (!(bitbyte & (1 << k)))
                    continue;
                // OR this pvs row into the phs
                int32_t index = ((j << 3) + k);
                if (index >= portalleafs)
                    FError("Bad bit in PVS"); // pad bits should be 0
                const uint8_t *src_compressed = bsp->dvis.bits.data() + bsp->dvis.get_bit_offset(VIS_PVS, index);
                DecompressRow(src_compressed, leafbytes, uncompressed_2);
                const long *src = (long *)uncompressed_2;
                long *dest = (long *)uncompressed;
                for (int32_t l = 0; l < leaflongs; l++)
                    dest[l] |= src[l];
            }
        }
        for (int32_t j = 0; j < portalleafs; j++)
            if (uncompressed[j >> 3] & (1 << (j & 7)))
                count++;

        //
        // compress the bit string
        //
        int32_t j = CompressRow(uncompressed, leafbytes, compressed);

        bsp->dvis.set_bit_offset(VIS_PHS, i, bsp->dvis.bits.size());

        std::copy(compressed, compressed + j, std::back_inserter(bsp->dvis.bits));
    }

    delete[] uncompressed;
    delete[] uncompressed_2;
    delete[] compressed;
    delete[] uncompressed_orig;

    fmt::print("Average clusters hearable: {}\n", count / portalleafs);

    bsp->dvis.bits.shrink_to_fit();
}