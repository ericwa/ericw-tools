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
void
SurfaceBBox(dface_t *s, vec3_t mins, vec3_t maxs)
{
    int i, j;
    int e;
    int vi;
    float *v;

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
    int i, j, k, l;
    dleaf_t *leaf, *hit;
    byte *vis;
    dface_t *surf;
    vec3_t mins, maxs;
    float d, maxd;
    int ambient_type;
    texinfo_t *info;
    miptex_t *miptex;
    int ofs;
    float dists[NUM_AMBIENTS];
    float vol;

    for (i = 0; i < portalleafs; i++) {
	leaf = &dleafs[i + 1];

	//
	// clear ambients
	//
	for (j = 0; j < NUM_AMBIENTS; j++)
	    dists[j] = 1020;

	vis = &uncompressed[i * leafbytes];

	for (j = 0; j < portalleafs; j++) {
	    if (!(vis[j >> 3] & (1 << (j & 7))))
		continue;

	    //
	    // check this leaf for sound textures
	    //
	    hit = &dleafs[j + 1];

	    for (k = 0; k < hit->nummarksurfaces; k++) {
		surf = &dfaces[dmarksurfaces[hit->firstmarksurface + k]];
		info = &texinfo[surf->texinfo];
		ofs = ((dmiptexlump_t *)dtexdata)->dataofs[info->miptex];
		miptex = (miptex_t *)(&dtexdata[ofs]);

		if (!Q_strncasecmp(miptex->name, "*water", 6))
		    ambient_type = AMBIENT_WATER;
		else if (!Q_strncasecmp(miptex->name, "sky", 3))
		    ambient_type = AMBIENT_SKY;
		else if (!Q_strncasecmp(miptex->name, "*slime", 6))
		    ambient_type = AMBIENT_WATER;	// AMBIENT_SLIME;
		else if (!Q_strncasecmp(miptex->name, "*lava", 6))
		    ambient_type = AMBIENT_LAVA;
		else if (!Q_strncasecmp(miptex->name, "*04water", 8))
		    ambient_type = AMBIENT_WATER;
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
