/*
    Copyright (C) 1996-1997  Id Software, Inc.
    Copyright (C) 1997       Greg Lewis

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

#include <string.h>

#include "qbsp.h"
#include "parser.h"

#define info_player_start		1
#define info_player_deathmatch	2
#define	info_player_coop		4

static int cAnimtex;
static int rgfStartSpots;


int
FindMiptex(char *szName)
{
    int i;

    for (i = 0; i < cMiptex; i++) {
	if (!strcmp(szName, rgszMiptex[i]))
	    return i;
    }
    strcpy(rgszMiptex[i], szName);
    cMiptex++;

    if (szName[0] == '+')
	cAnimtex++;

    return i;
}


/*
===============
FindTexinfo

Returns a global texinfo number
===============
*/
static int
FindTexinfo(texinfo_t *t)
{
    int i, j;
    texinfo_t *tex;

    // set the special flag
    if ((rgszMiptex[t->miptex][0] == '*'
	 || !strncasecmp(rgszMiptex[t->miptex], "sky", 3))
	&& !options.fSplitspecial)
	t->flags |= TEX_SPECIAL;

    tex = (texinfo_t *)pWorldEnt->lumps[BSPTEXINFO].data;
    for (i = 0; i < pWorldEnt->lumps[BSPTEXINFO].index; i++, tex++) {
	if (t->miptex != tex->miptex)
	    continue;
	if (t->flags != tex->flags)
	    continue;

	for (j = 0; j < 8; j++)
	    if (t->vecs[0][j] != tex->vecs[0][j])
		break;
	if (j != 8)
	    continue;

	return i;
    }

    // allocate a new texture
    *((texinfo_t *)pWorldEnt->lumps[BSPTEXINFO].data + i) = *t;
    pWorldEnt->lumps[BSPTEXINFO].index++;
    map.cTotal[BSPTEXINFO]++;

    return i;
}


static void
ParseEpair(void)
{
    epair_t *e;

    e = AllocMem(OTHER, sizeof(epair_t), true);
    e->next = map.rgEntities[map.iEntities].epairs;
    map.rgEntities[map.iEntities].epairs = e;

    if (strlen(token) >= MAX_KEY - 1)
	Message(msgError, errEpairTooLong, linenum);
    e->key = copystring(token);
    ParseToken(PARSE_SAMELINE);
    if (strlen(token) >= MAX_VALUE - 1)
	Message(msgError, errEpairTooLong, linenum);
    e->value = copystring(token);

    if (!strcasecmp(e->key, "origin"))
	GetVectorForKey(map.iEntities, e->key,
			map.rgEntities[map.iEntities].origin);
    else if (!strcasecmp(e->key, "classname")) {
	if (!strcasecmp(e->value, "info_player_start")) {
	    if (rgfStartSpots & info_player_start)
		Message(msgWarning, warnMultipleStarts);
	    rgfStartSpots |= info_player_start;
	} else if (!strcasecmp(e->value, "info_player_deathmatch"))
	    rgfStartSpots |= info_player_deathmatch;
	else if (!strcasecmp(e->value, "info_player_coop"))
	    rgfStartSpots |= info_player_coop;
    }
}


static void
TextureAxisFromPlane(plane_t *pln, vec3_t xv, vec3_t yv)
{
    vec3_t baseaxis[18] = {
	{0, 0, 1}, {1, 0, 0}, {0, -1, 0},	// floor
	{0, 0, -1}, {1, 0, 0}, {0, -1, 0},	// ceiling
	{1, 0, 0}, {0, 1, 0}, {0, 0, -1},	// west wall
	{-1, 0, 0}, {0, 1, 0}, {0, 0, -1},	// east wall
	{0, 1, 0}, {1, 0, 0}, {0, 0, -1},	// south wall
	{0, -1, 0}, {1, 0, 0}, {0, 0, -1}	// north wall
    };

    int bestaxis;
    vec_t dot, best;
    int i;

    best = 0;
    bestaxis = 0;

    for (i = 0; i < 6; i++) {
	dot = DotProduct(pln->normal, baseaxis[i * 3]);
	if (dot > best || (dot == best && !options.fOldaxis)) {
	    best = dot;
	    bestaxis = i;
	}
    }

    VectorCopy(baseaxis[bestaxis * 3 + 1], xv);
    VectorCopy(baseaxis[bestaxis * 3 + 2], yv);
}


enum texcoord_style {
    TX_ORIGINAL    = 0,
    TX_QUARK_TYPE1 = 1,
    TX_QUARK_TYPE2 = 2
};

static int
ParseExtendedTX(void)
{
    int style = TX_ORIGINAL;

    if (ParseToken(PARSE_COMMENT)) {
	if (!strncmp(token, "//TX", 4)) {
	    if (token[4] == '1')
		style = TX_QUARK_TYPE1;
	    else if (token[4] == '2')
		style = TX_QUARK_TYPE2;
	}
    }

    return style;
}


static void
SetTexinfo_QuakeEd(int shift[2], int rotate, vec_t scale[2], texinfo_t *tx)
{
    int i, j;
    vec3_t vecs[2];
    int sv, tv;
    vec_t ang, sinv, cosv;
    vec_t ns, nt;

    TextureAxisFromPlane(&(map.rgFaces[map.iFaces].plane), vecs[0], vecs[1]);

    if (!scale[0])
	scale[0] = 1;
    if (!scale[1])
	scale[1] = 1;

    // rotate axis
    if (rotate == 0) {
	sinv = 0;
	cosv = 1;
    } else if (rotate == 90) {
	sinv = 1;
	cosv = 0;
    } else if (rotate == 180) {
	sinv = 0;
	cosv = -1;
    } else if (rotate == 270) {
	sinv = -1;
	cosv = 0;
    } else {
	ang = (vec_t)rotate / 180 * Q_PI;
	sinv = sin(ang);
	cosv = cos(ang);
    }

    if (vecs[0][0])
	sv = 0;
    else if (vecs[0][1])
	sv = 1;
    else
	sv = 2;

    if (vecs[1][0])
	tv = 0;
    else if (vecs[1][1])
	tv = 1;
    else
	tv = 2;

    for (i = 0; i < 2; i++) {
	ns = cosv * vecs[i][sv] - sinv * vecs[i][tv];
	nt = sinv * vecs[i][sv] + cosv * vecs[i][tv];
	vecs[i][sv] = ns;
	vecs[i][tv] = nt;
    }

    for (i = 0; i < 2; i++)
	for (j = 0; j < 3; j++)
	    tx->vecs[i][j] = vecs[i][j] / scale[i];

    tx->vecs[0][3] = shift[0];
    tx->vecs[1][3] = shift[1];
}


static void
SetTexinfo_QuArK(vec3_t planepts[3], int tx_type, texinfo_t *tx)
{
    vec3_t vecs[2];
    int i, point;
    vec_t a, b, c, d;
    vec_t determinant;

    /*
     * Type 1 uses vecs[0] = (pt[2] - pt[0]) and vecs[1] = (pt[1] - pt[0])
     * Type 2 reverses the order of the vecs
     * 128 is the scaling factor assumed by QuArK.
     */
    for (i = 0; i < 2; i++) {
	point = (tx_type == TX_QUARK_TYPE1) ? 2 - i : i + 1;
	VectorSubtract(planepts[point], planepts[0], vecs[i]);
	VectorScale(vecs[i], 1.0 / 128.0, vecs[i]);
    }

    a = DotProduct(vecs[0], vecs[0]);
    b = DotProduct(vecs[0], vecs[1]);
    c = b; /* DotProduct(vecs[1], vecs[0]); */
    d = DotProduct(vecs[1], vecs[1]);

    /*
     * Want to solve for tx->vecs:
     *
     *    | a b | | tx->vecs[0] | = | vecs[0] |
     *    | c d | | tx->vecs[1] |   | vecs[1] |
     *
     * => | tx->vecs[0] | = __ 1.0__  | d  -b | | vecs[0] |
     *    | tx->vecs[1] |   a*d - b*c | -c  a | | vecs[1] |
     */
    determinant = a * d - b * c;
    if (fabs(determinant) < ZERO_EPSILON) {
	Message(msgWarning, warnDegenerateQuArKTX, linenum);
	for (i = 0; i < 3; i++)
	    tx->vecs[0][i] = tx->vecs[1][i] = 0;
    } else {
	for (i = 0; i < 3; i++) {
	    tx->vecs[0][i] = (d * vecs[0][i] - b * vecs[1][i]) / determinant;
	    tx->vecs[1][i] = (a * vecs[1][i] - c * vecs[0][i]) / determinant;
	}
    }

    /* Finally, the texture offset is indicated by planepts[0] */
    for (i = 0; i < 3; ++i) {
	vecs[0][i] = tx->vecs[0][i];
	vecs[1][i] = tx->vecs[1][i];
    }
    tx->vecs[0][3] = -DotProduct(vecs[0], planepts[0]);
    tx->vecs[1][3] = -DotProduct(vecs[1], planepts[0]);
}


static void
ParseBrush(void)
{
    vec3_t planepts[3];
    vec3_t t1, t2, t3;
    int i, j;
    texinfo_t tx;
    vec_t d;
    int shift[2], rotate;
    vec_t scale[2];
    int iFace;
    int tx_type;

    map.rgBrushes[map.iBrushes].iFaceEnd = map.iFaces + 1;

    while (ParseToken(PARSE_NORMAL)) {
	if (!strcmp(token, "}"))
	    break;

	// read the three point plane definition
	for (i = 0; i < 3; i++) {
	    if (i != 0)
		ParseToken(PARSE_NORMAL);
	    if (strcmp(token, "("))
		Message(msgError, errInvalidMapPlane, linenum);

	    for (j = 0; j < 3; j++) {
		ParseToken(PARSE_SAMELINE);
		planepts[i][j] = atof(token);
	    }

	    ParseToken(PARSE_SAMELINE);
	    if (strcmp(token, ")"))
		Message(msgError, errInvalidMapPlane, linenum);
	}

	// read the texturedef
	memset(&tx, 0, sizeof(tx));
	ParseToken(PARSE_SAMELINE);
	tx.miptex = FindMiptex(token);
	ParseToken(PARSE_SAMELINE);
	shift[0] = atoi(token);
	ParseToken(PARSE_SAMELINE);
	shift[1] = atoi(token);
	ParseToken(PARSE_SAMELINE);
	rotate = atoi(token);
	ParseToken(PARSE_SAMELINE);
	scale[0] = atof(token);
	ParseToken(PARSE_SAMELINE);
	scale[1] = atof(token);

	// if the three points are all on a previous plane, it is a
	// duplicate plane
	for (iFace = map.rgBrushes[map.iBrushes].iFaceEnd - 1;
	     iFace > map.iFaces; iFace--) {
	    for (i = 0; i < 3; i++) {
		d = DotProduct(planepts[i], map.rgFaces[iFace].plane.normal) -
		    map.rgFaces[iFace].plane.dist;
		if (d < -ON_EPSILON || d > ON_EPSILON)
		    break;
	    }
	    if (i == 3)
		break;
	}
	if (iFace > map.iFaces) {
	    Message(msgWarning, warnBrushDuplicatePlane, linenum);
	    continue;
	}

	if (map.iFaces < 0)
	    Message(msgError, errLowFaceCount);

	// convert to a vector / dist plane
	for (j = 0; j < 3; j++) {
	    t1[j] = planepts[0][j] - planepts[1][j];
	    t2[j] = planepts[2][j] - planepts[1][j];
	    t3[j] = planepts[1][j];
	}

	CrossProduct(t1, t2, map.rgFaces[map.iFaces].plane.normal);
	if (VectorCompare(map.rgFaces[map.iFaces].plane.normal, vec3_origin)) {
	    Message(msgWarning, warnNoPlaneNormal, linenum);
	    break;
	}
	VectorNormalize(map.rgFaces[map.iFaces].plane.normal);
	map.rgFaces[map.iFaces].plane.dist =
	    DotProduct(t3, map.rgFaces[map.iFaces].plane.normal);

	tx_type = ParseExtendedTX();
	switch (tx_type) {
	case TX_QUARK_TYPE1:
	case TX_QUARK_TYPE2:
	    SetTexinfo_QuArK(planepts, tx_type, &tx);
	    break;
	default:
	    SetTexinfo_QuakeEd(shift, rotate, scale, &tx);
	    break;
	}

	// unique the texinfo
	map.rgFaces[map.iFaces].texinfo = FindTexinfo(&tx);
	map.iFaces--;
	Message(msgPercent, map.cFaces - map.iFaces - 1, map.cFaces);
    }

    map.rgBrushes[map.iBrushes].iFaceStart = map.iFaces + 1;
    map.iBrushes--;
}


static bool
ParseEntity(mapentity_t *e)
{
    if (!ParseToken(PARSE_NORMAL))
	return false;

    if (strcmp(token, "{"))
	Message(msgError, errParseEntity, linenum);

    if (map.iEntities >= map.cEntities)
	Message(msgError, errLowEntCount);

    e->iBrushEnd = map.iBrushes + 1;

    do {
	if (!ParseToken(PARSE_NORMAL))
	    Message(msgError, errUnexpectedEOF);
	if (!strcmp(token, "}"))
	    break;
	else if (!strcmp(token, "{"))
	    ParseBrush();
	else
	    ParseEpair();
    } while (1);

    // Allocate some model memory while we're here
    e->iBrushStart = map.iBrushes + 1;
    if (e->iBrushStart != e->iBrushEnd) {
	e->lumps[BSPMODEL].data = AllocMem(BSPMODEL, 1, true);
	e->lumps[BSPMODEL].count = 1;
    }

    return true;
}


static void
PreParseFile(char *buf)
{
    int braces = 0;
    struct lumpdata *texinfo;

    map.cEntities = map.cBrushes = map.cFaces = 0;

    // Very simple... we just want numbers here.  Invalid formats are
    // detected later.  Problems with deviant .MAP formats.
    while (*buf != 0) {
	if (*buf == '\"') {
	    buf++;
	    // Quoted string... skip to end of quote
	    while (*buf != '\"' && *buf)
		buf++;
	    if (!*buf)
		break;
	} else if (*buf == '/' && *(buf + 1) == '/') {
	    // Comment... skip to end of line
	    while (*buf != '\n' && *buf)
		buf++;
	    if (!*buf)
		break;
	} else if (*buf == '{') {
	    if (braces == 0)
		map.cEntities++;
	    else if (braces == 1)
		map.cBrushes++;
	    braces++;
	} else if (*buf == '}')
	    braces--;
	else if (*buf == '(')
	    map.cFaces++;
	buf++;
    }

    if (map.cFaces % 3 != 0)
	Message(msgWarning, warnBadMapFaceCount);
    map.cFaces /= 3;

    map.rgFaces = AllocMem(MAPFACE, map.cFaces, true);
    map.rgBrushes = AllocMem(MAPBRUSH, map.cBrushes, true);
    map.rgEntities = AllocMem(MAPENTITY, map.cEntities, true);

    // While we're here...
    pWorldEnt = map.rgEntities;

    // Allocate maximum memory here, copy over later
    // Maximum possible is one miptex/texinfo per face
    rgszMiptex = AllocMem(MIPTEX, map.cFaces, true);
    texinfo = &pWorldEnt->lumps[BSPTEXINFO];
    texinfo->data = AllocMem(BSPTEXINFO, map.cFaces, true);
    texinfo->count = map.cFaces;
}


void
LoadMapFile(void)
{
    char *buf;
    int i, j, length;
    void *pTemp;
    struct lumpdata *texinfo;

    Message(msgProgress, "LoadMapFile");

    length = LoadFile(options.szMapName, (void *)&buf, true);
    PreParseFile(buf);
    ParserInit(buf);

    // Faces are loaded in reverse order, to be compatible with origqbsp.
    // Brushes too.
    map.iFaces = map.cFaces - 1;
    map.iBrushes = map.cBrushes - 1;
    map.iEntities = 0;
    pCurEnt = &map.rgEntities[0];

    while (ParseEntity(pCurEnt)) {
	map.iEntities++;
	pCurEnt++;
    }

    FreeMem(buf, OTHER, length + 1);

    // Print out warnings for entities
    if (!(rgfStartSpots & info_player_start))
	Message(msgWarning, warnNoPlayerStart);
    if (!(rgfStartSpots & info_player_deathmatch))
	Message(msgWarning, warnNoPlayerDeathmatch);
//      if (!(rgfStartSpots & info_player_coop))
//              Message(msgWarning, warnNoPlayerCoop);

    // Clean up texture memory
    if (cMiptex > map.cFaces)
	Message(msgError, errLowMiptexCount);
    else if (cMiptex < map.cFaces) {
	// For stuff in AddAnimatingTex, make room available
	pTemp = (void *)rgszMiptex;
	rgszMiptex = AllocMem(MIPTEX, cMiptex + cAnimtex * 20, true);
	memcpy(rgszMiptex, pTemp, cMiptex * rgcMemSize[MIPTEX]);
	FreeMem(pTemp, MIPTEX, map.cFaces);
    }

    texinfo = &pWorldEnt->lumps[BSPTEXINFO];
    if (texinfo->index > texinfo->count)
	Message(msgError, errLowTexinfoCount);
    else if (texinfo->index < texinfo->count) {
	pTemp = texinfo->data;
	texinfo->data = AllocMem(BSPTEXINFO, texinfo->index, true);
	memcpy(texinfo->data, pTemp, texinfo->index * rgcMemSize[BSPTEXINFO]);
	FreeMem(pTemp, BSPTEXINFO, texinfo->count);
	texinfo->count = texinfo->index;
    }
    // One plane per face + 6 for portals
    cPlanes = map.cFaces + 6;

    // Count # of unique planes
    for (i = 0; i < map.cFaces; i++) {
	map.rgFaces[i].fUnique = true;
	for (j = 0; j < i; j++)
	    if (map.rgFaces[j].fUnique &&
		VectorCompare(map.rgFaces[i].plane.normal,
			      map.rgFaces[j].plane.normal)
		&& fabs(map.rgFaces[i].plane.dist -
			map.rgFaces[j].plane.dist) < EQUAL_EPSILON) {
		map.rgFaces[i].fUnique = false;
		cPlanes--;
		break;
	    }
    }

    // Now iterate through brushes, add one plane for each face below 6 axis aligned faces.
    // This compensates for planes added in ExpandBrush.
    int cAxis;

    for (i = 0; i < map.cBrushes; i++) {
	cAxis = 0;
	for (j = map.rgBrushes[i].iFaceStart; j < map.rgBrushes[i].iFaceEnd;
	     j++) {
	    if (fabs(map.rgFaces[j].plane.normal[0]) > 1 - NORMAL_EPSILON
		|| fabs(map.rgFaces[j].plane.normal[1]) > 1 - NORMAL_EPSILON
		|| fabs(map.rgFaces[j].plane.normal[2]) > 1 - NORMAL_EPSILON)
		cAxis++;
	}
	if (6 - cAxis > 0)
	    cPlanes += 6 - cAxis;
    }

    // cPlanes*3 because of 3 hulls, then add 20% as a fudge factor for hull edge bevel planes
    cPlanes = 3 * cPlanes + cPlanes / 5;
    pPlanes = AllocMem(PLANE, cPlanes, true);

    Message(msgStat, "%5i faces", map.cFaces);
    Message(msgStat, "%5i brushes", map.cBrushes);
    Message(msgStat, "%5i entities", map.cEntities);
    Message(msgStat, "%5i unique texnames", cMiptex);
    Message(msgStat, "%5i texinfo", texinfo->count);
    Message(msgLiteral, "\n");
}


void
PrintEntity(int iEntity)
{
    epair_t *ep;

    for (ep = map.rgEntities[iEntity].epairs; ep; ep = ep->next)
	Message(msgStat, "%20s : %s", ep->key, ep->value);
}


char *
ValueForKey(int iEntity, char *key)
{
    epair_t *ep;

    for (ep = map.rgEntities[iEntity].epairs; ep; ep = ep->next)
	if (!strcmp(ep->key, key))
	    return ep->value;

    return NULL;
}


void
SetKeyValue(int iEntity, char *key, char *value)
{
    epair_t *ep;

    for (ep = map.rgEntities[iEntity].epairs; ep; ep = ep->next)
	if (!strcmp(ep->key, key)) {
	    free(ep->value); /* FIXME */
	    ep->value = copystring(value);
	    return;
	}
    ep = AllocMem(OTHER, sizeof(epair_t), true);
    ep->next = map.rgEntities[iEntity].epairs;
    map.rgEntities[iEntity].epairs = ep;
    ep->key = copystring(key);
    ep->value = copystring(value);
}


void
GetVectorForKey(int iEntity, char *szKey, vec3_t vec)
{
    char *k;
    double v1, v2, v3;

    k = ValueForKey(iEntity, szKey);
    v1 = v2 = v3 = 0;
    // scanf into doubles, then assign, so it is vec_t size independent
    sscanf(k, "%lf %lf %lf", &v1, &v2, &v3);
    vec[0] = v1;
    vec[1] = v2;
    vec[2] = v3;
}


void
WriteEntitiesToString(void)
{
    char *pCur;
    epair_t *ep;
    char szLine[129];
    int iEntity;
    int cLen;
    struct lumpdata *entities;

    map.cTotal[BSPENT] = 0;

    for (iEntity = 0; iEntity < map.cEntities; iEntity++) {
	ep = map.rgEntities[iEntity].epairs;
	entities = &map.rgEntities[iEntity].lumps[BSPENT];

	// ent got removed
	if (!ep) {
	    entities->count = 0;
	    entities->data = NULL;
	    continue;
	}

	cLen = 0;
	while (ep) {
	    int i = strlen(ep->key) + strlen(ep->value) + 6;

	    if (i <= 128)
		cLen += i;
	    else
		cLen += 128;
	    ep = ep->next;
	}

	// Add 4 for {\n and }\n
	cLen += 4;

	entities->count = cLen;
	map.cTotal[BSPENT] += cLen;
	entities->data = pCur = AllocMem(BSPENT, cLen, true);
	*pCur = 0;

	strcat(pCur, "{\n");
	pCur += 2;

	for (ep = map.rgEntities[iEntity].epairs; ep; ep = ep->next) {
	    // Limit on Quake's strings of 128 bytes
	    sprintf(szLine, "\"%.*s\" \"%.*s\"\n", MAX_KEY, ep->key,
		    122 - strlen(ep->key), ep->value);
	    strcat(pCur, szLine);
	    pCur += strlen(szLine);
	}

	// No terminating null on this string
	pCur[0] = '}';
	pCur[1] = '\n';
    }
}
