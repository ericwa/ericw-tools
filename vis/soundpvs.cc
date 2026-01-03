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

#include <common/log.hh>
#include <vis/vis.hh>
#include <common/bsputils.hh>
#include <common/parallel.hh>
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
    logging::funcheader();

    // fast path for -noambient
    if (vis_options.noambientsky.value() && vis_options.noambientwater.value() && vis_options.noambientslime.value() &&
        vis_options.noambientlava.value()) {
        for (int i = 0; i < portalleafs_real; i++) {
            mleaf_t *leaf = &bsp->dleafs[i + 1];
            for (int j = 0; j < NUM_AMBIENTS; j++) {
                leaf->ambient_level[j] = 0;
            }
        }
        return;
    }

    logging::parallel_for(0, portalleafs_real, [&bsp](int i) {
        mleaf_t *leaf = &bsp->dleafs[i + 1];

        float dists[NUM_AMBIENTS];

        //
        // clear ambients
        //
        for (int j = 0; j < NUM_AMBIENTS; j++)
            dists[j] = 1020;

        uint8_t *vis;
        if (portalleafs != portalleafs_real) {
            vis = &uncompressed[leaf->cluster * leafbytes_real];
        } else {
            vis = &uncompressed[i * leafbytes_real];
        }

        for (int j = 0; j < portalleafs_real; j++) {
            if (!(vis[j >> 3] & nth_bit(j & 7)))
                continue;

            //
            // check this leaf for sound textures
            //
            mleaf_t *hit = &bsp->dleafs[j + 1];

            for (int k = 0; k < hit->nummarksurfaces; k++) {
                const mface_t *surf = BSP_GetFace(bsp, bsp->dleaffaces[hit->firstmarksurface + k]);
                const mtexinfo_t *info = &bsp->texinfo[surf->texinfo];
                const auto &miptex = bsp->dtex.textures[info->miptex];

                ambient_type_t ambient_type;
                if (!Q_strncasecmp(miptex.name.data(), "sky", 3) && !vis_options.noambientsky.value()) {
                    ambient_type = AMBIENT_SKY;
                } else if (!Q_strncasecmp(miptex.name.data(), "*water", 6) ||
                           !Q_strncasecmp(miptex.name.data(), "!water", 6)) {
                    if (!vis_options.noambientwater.value()) {
                        ambient_type = AMBIENT_WATER;
                    }
                } else if (!Q_strncasecmp(miptex.name.data(), "*04water", 6) ||
                           !Q_strncasecmp(miptex.name.data(), "!04water", 6)) {
                    if (!vis_options.noambientwater.value()) {
                        ambient_type = AMBIENT_WATER;
                    }
                } else if (!Q_strncasecmp(miptex.name.data(), "*slime", 6) ||
                           !Q_strncasecmp(miptex.name.data(), "!slime", 6)) {
                    if (!vis_options.noambientslime.value()) {
                        ambient_type =
                            AMBIENT_WATER; // AMBIENT_SLIME; // there should probably be a VIS arg to use the acutal
                                           // AMBIENT_SLIME, for games on custom engines that can parse it
                    }
                } else if (!Q_strncasecmp(miptex.name.data(), "*lava", 5) ||
                           !Q_strncasecmp(miptex.name.data(), "!lava", 5)) {
                    if (!vis_options.noambientslime.value()) {
                        ambient_type = AMBIENT_LAVA;
                    }
                } else {
                    continue;
                }

                // noambient surfflag
                if (vis::extended_texinfo_flags[surf->texinfo].noambient)
                    continue;

                // find distance from source leaf to polygon
                aabb3d bounds = SurfaceBBox(bsp, surf);
                float maxd = 0;
                for (int l = 0; l < 3; l++) {
                    float d;
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

        for (int j = 0; j < NUM_AMBIENTS; j++) {
            float vol;
            if (dists[j] < 100)
                vol = 1.0;
            else {
                vol = (double)(1.0 - dists[2] * 0.002);
                if (vol < 0)
                    vol = 0;
            }
            leaf->ambient_level[j] = (uint8_t)(vol * 255);
        }
    });
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
    logging::funcheader();

    const int32_t leafbytes = (portalleafs + 7) >> 3;
    const int32_t leaflongs = leafbytes / sizeof(long);

    // increase the bits size with approximately how much space we'll need
    bsp->dvis.bits.reserve(bsp->dvis.bits.size() * 2);

    std::vector<uint8_t> uncompressed(leafbytes);
    std::vector<uint8_t> uncompressed_2(leafbytes);
    std::vector<uint8_t> compressed(leafbytes * 2);
    std::vector<uint8_t> uncompressed_orig(leafbytes);

    int32_t count = 0;
    for (int32_t i = 0; i < portalleafs; i++) {
        const uint8_t *scan = bsp->dvis.bits.data() + bsp->dvis.get_bit_offset(VIS_PVS, i);

        DecompressVis(scan, bsp->dvis.bits.data() + bsp->dvis.bits.size(), uncompressed.data(),
            uncompressed.data() + uncompressed.size());
        std::copy(uncompressed.begin(), uncompressed.end(), uncompressed_orig.begin());

        scan = uncompressed_orig.data();

        for (int32_t j = 0; j < leafbytes; j++) {
            uint8_t bitbyte = scan[j];
            if (!bitbyte)
                continue;
            for (int32_t k = 0; k < 8; k++) {
                if (!(bitbyte & nth_bit(k)))
                    continue;
                // OR this pvs row into the phs
                int32_t index = ((j << 3) + k);
                if (index >= portalleafs)
                    FError("Bad bit in PVS"); // pad bits should be 0
                const uint8_t *src_compressed = bsp->dvis.bits.data() + bsp->dvis.get_bit_offset(VIS_PVS, index);
                DecompressVis(src_compressed, bsp->dvis.bits.data() + bsp->dvis.bits.size(), uncompressed_2.data(),
                    uncompressed_2.data() + uncompressed_2.size());
                const long *src = reinterpret_cast<long *>(uncompressed_2.data());
                long *dest = reinterpret_cast<long *>(uncompressed.data());
                for (int32_t l = 0; l < leaflongs; l++)
                    dest[l] |= src[l];
            }
        }
        for (int32_t j = 0; j < portalleafs; j++)
            if (uncompressed[j >> 3] & nth_bit(j & 7))
                count++;

        //
        // compress the bit string
        //
        compressed.clear();
        CompressRow(uncompressed.data(), leafbytes, std::back_inserter(compressed));

        bsp->dvis.set_bit_offset(VIS_PHS, i, bsp->dvis.bits.size());

        std::copy(compressed.begin(), compressed.end(), std::back_inserter(bsp->dvis.bits));
    }

    fmt::print("Average clusters hearable: {}\n", count / portalleafs);

    bsp->dvis.bits.shrink_to_fit();
}