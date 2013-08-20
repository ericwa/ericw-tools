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
SurfaceBBox(const bsp29_dface_t *s, vec3_t mins, vec3_t maxs)
{
    int i, j;
    int e;
    int vi;
    const float *v;

    mins[0] = mins[1] = 999999;
    maxs[0] = maxs[1] = -99999;

    for (i = 0; i < s->numedges; i++) {
	e = dsurfedges[s->firstedge + i];
	if (e >= 0)
	    vi = dedges[e].v[0];
	else
	    vi = dedges[-e].v[1];
	v = dvertexes[vi].point;

	for (j = 0; j < 3; j++) {
	    if (v[j] < mins[j])
		mins[j] = v[j];
	    if (v[j] > maxs[j])
		maxs[j] = v[j];
	}
    }
}


/*
  ====================
  CalcAmbientSounds
  ====================
*/
void
CalcAmbientSounds(void)
{
    const bsp29_dface_t *surf;
    const texinfo_t *info;
    const miptex_t *miptex;
    int i, j, k, l;
    bsp29_dleaf_t *leaf, *hit;
    byte *vis;
    vec3_t mins, maxs;
    float d, maxd;
    int ambient_type;
    int ofs;
    float dists[NUM_AMBIENTS];
    float vol;

    for (i = 0; i < portalleafs_real; i++) {
	leaf = &dleafs[i + 1];

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
	    hit = &dleafs[j + 1];

	    for (k = 0; k < hit->nummarksurfaces; k++) {
		surf = &dfaces[dmarksurfaces[hit->firstmarksurface + k]];
		info = &texinfo[surf->texinfo];
		ofs = dtexdata.header->dataofs[info->miptex];
		miptex = (const miptex_t *)(dtexdata.base + ofs);

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
		SurfaceBBox(surf, mins, maxs);
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
