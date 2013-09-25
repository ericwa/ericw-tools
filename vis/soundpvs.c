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

#include <float.h>

#include <vis/vis.h>

/*

Some textures (sky, water, slime, lava) are considered ambien sound emiters.
Find an aproximate distance to the nearest emiter of each class for each leaf.

*/


/*
  ====================
  SurfaceBBox
  ====================
*/
static void
SurfaceBBox(const bsp2_t *bsp, const bsp2_dface_t *surf,
	    vec3_t mins, vec3_t maxs)
{
    int i, j;
    int edgenum;
    int vertnum;
    const float *vert;

    mins[0] = mins[1] = FLT_MAX;
    maxs[0] = maxs[1] = -FLT_MAX;

    for (i = 0; i < surf->numedges; i++) {
	edgenum = bsp->dsurfedges[surf->firstedge + i];
	if (edgenum >= 0)
	    vertnum = bsp->dedges[edgenum].v[0];
	else
	    vertnum = bsp->dedges[-edgenum].v[1];
	vert = bsp->dvertexes[vertnum].point;

	for (j = 0; j < 3; j++) {
	    if (vert[j] < mins[j])
		mins[j] = vert[j];
	    if (vert[j] > maxs[j])
		maxs[j] = vert[j];
	}
    }
}


/*
  ====================
  CalcAmbientSounds
  ====================
*/
void
CalcAmbientSounds(bsp2_t *bsp)
{
    const bsp2_dface_t *surf;
    const texinfo_t *info;
    const miptex_t *miptex;
    int i, j, k, l;
    bsp2_dleaf_t *leaf, *hit;
    byte *vis;
    vec3_t mins, maxs;
    float d, maxd;
    int ambient_type;
    int ofs;
    float dists[NUM_AMBIENTS];
    float vol;

    for (i = 0; i < portalleafs_real; i++) {
	leaf = &bsp->dleafs[i + 1];

	//
	// clear ambients
	//
	for (j = 0; j < NUM_AMBIENTS; j++)
	    dists[j] = 1020;

	vis = &uncompressed[i * leafbytes_real];

	for (j = 0; j < portalleafs_real; j++) {
	    if (!(vis[j >> 3] & (1 << (j & 7))))
		continue;

	    //
	    // check this leaf for sound textures
	    //
	    hit = &bsp->dleafs[j + 1];

	    for (k = 0; k < hit->nummarksurfaces; k++) {
		surf = &bsp->dfaces[bsp->dmarksurfaces[hit->firstmarksurface + k]];
		info = &bsp->texinfo[surf->texinfo];
		ofs = bsp->dtexdata.header->dataofs[info->miptex];
		miptex = (const miptex_t *)(bsp->dtexdata.base + ofs);

		if (!strncasecmp(miptex->name, "sky", 3) && ambientsky)
		    ambient_type = AMBIENT_SKY;
		else if (!strncasecmp(miptex->name, "*water", 6) && ambientwater)
		    ambient_type = AMBIENT_WATER;
		else if (!strncasecmp(miptex->name, "*04water", 8) && ambientwater)
		    ambient_type = AMBIENT_WATER;
		else if (!strncasecmp(miptex->name, "*slime", 6) && ambientslime)
		    ambient_type = AMBIENT_WATER;	// AMBIENT_SLIME;
		else if (!strncasecmp(miptex->name, "*lava", 5) && ambientlava)
		    ambient_type = AMBIENT_LAVA;
		else
		    continue;

		// find distance from source leaf to polygon
		SurfaceBBox(bsp, surf, mins, maxs);
		maxd = 0;
		for (l = 0; l < 3; l++) {
		    if (mins[l] > leaf->maxs[l])
			d = mins[l] - leaf->maxs[l];
		    else if (maxs[l] < leaf->mins[l])
			d = leaf->mins[l] - mins[l];
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
	    leaf->ambient_level[j] = (byte)(vol * 255);
	}
    }
}
