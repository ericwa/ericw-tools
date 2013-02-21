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

#define info_player_start	1
#define info_player_deathmatch	2
#define	info_player_coop	4

static int cAnimtex;
static int rgfStartSpots;


int
FindMiptex(const char *name)
{
    int i;

    for (i = 0; i < map.nummiptex; i++) {
	if (!strcmp(name, map.miptex[i]))
	    return i;
    }
    if (map.nummiptex == map.maxmiptex)
	Error(errLowMiptexCount);

    strcpy(map.miptex[i], name);
    map.nummiptex++;

    if (name[0] == '+')
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
    const char *name;

    // set the special flag
    name = map.miptex[t->miptex];
    if (name[0] == '*' || !strncasecmp(name, "sky", 3))
	if (!options.fSplitspecial)
	    t->flags |= TEX_SPECIAL;

    tex = pWorldEnt->lumps[BSPTEXINFO].data;
    for (i = 0; i < pWorldEnt->lumps[BSPTEXINFO].index; i++, tex++) {
	if (t->miptex != tex->miptex)
	    continue;
	if (t->flags != tex->flags)
	    continue;

	for (j = 0; j < 4; j++) {
	    if (t->vecs[0][j] != tex->vecs[0][j])
		break;
	    if (t->vecs[1][j] != tex->vecs[1][j])
		break;
	}
	if (j != 4)
	    continue;

	return i;
    }

    /* Allocate a new texinfo at the end of the array */
    *tex = *t;
    pWorldEnt->lumps[BSPTEXINFO].index++;
    map.cTotal[BSPTEXINFO]++;

    return i;
}


static void
ParseEpair(parser_t *parser, mapentity_t *ent)
{
    epair_t *epair;

    epair = AllocMem(OTHER, sizeof(epair_t), true);
    epair->next = ent->epairs;
    ent->epairs = epair;

    if (strlen(parser->token) >= MAX_KEY - 1)
	Error(errEpairTooLong, parser->linenum);
    epair->key = copystring(parser->token);
    ParseToken(parser, PARSE_SAMELINE);
    if (strlen(parser->token) >= MAX_VALUE - 1)
	Error(errEpairTooLong, parser->linenum);
    epair->value = copystring(parser->token);

    if (!strcasecmp(epair->key, "origin")) {
	GetVectorForKey(ent, epair->key, ent->origin);
    } else if (!strcasecmp(epair->key, "classname")) {
	if (!strcasecmp(epair->value, "info_player_start")) {
	    if (rgfStartSpots & info_player_start)
		Message(msgWarning, warnMultipleStarts);
	    rgfStartSpots |= info_player_start;
	} else if (!strcasecmp(epair->value, "info_player_deathmatch")) {
	    rgfStartSpots |= info_player_deathmatch;
	} else if (!strcasecmp(epair->value, "info_player_coop")) {
	    rgfStartSpots |= info_player_coop;
	}
    }
}


static void
TextureAxisFromPlane(const plane_t *plane, vec3_t xv, vec3_t yv)
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
	dot = DotProduct(plane->normal, baseaxis[i * 3]);
	if (dot > best || (dot == best && !options.fOldaxis)) {
	    best = dot;
	    bestaxis = i;
	}
    }

    VectorCopy(baseaxis[bestaxis * 3 + 1], xv);
    VectorCopy(baseaxis[bestaxis * 3 + 2], yv);
}


typedef enum {
    TX_ORIGINAL    = 0,
    TX_QUARK_TYPE1 = 1,
    TX_QUARK_TYPE2 = 2
} texcoord_style_t;

static texcoord_style_t
ParseExtendedTX(parser_t *parser)
{
    texcoord_style_t style = TX_ORIGINAL;

    if (ParseToken(parser, PARSE_COMMENT)) {
	if (!strncmp(parser->token, "//TX", 4)) {
	    if (parser->token[4] == '1')
		style = TX_QUARK_TYPE1;
	    else if (parser->token[4] == '2')
		style = TX_QUARK_TYPE2;
	}
    }

    return style;
}

static void
SetTexinfo_QuakeEd(const plane_t *plane, const int shift[2], int rotate,
		   const vec_t scale[2], texinfo_t *out)
{
    int i, j;
    vec3_t vecs[2];
    int sv, tv;
    vec_t ang, sinv, cosv;
    vec_t ns, nt;

    TextureAxisFromPlane(plane, vecs[0], vecs[1]);

    /* Normalize the Texture rotation */
    rotate %= 360;
    while (rotate < 0)
	rotate += 360;

    // rotate axis
    switch (rotate) {
    case 0:
	sinv = 0;
	cosv = 1;
	break;
    case 90:
	sinv = 1;
	cosv = 0;
	break;
    case 180:
	sinv = 0;
	cosv = -1;
	break;
    case 270:
	sinv = -1;
	cosv = 0;
	break;
    default:
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
	    /* Interpret zero scale as no scaling */
	    out->vecs[i][j] = vecs[i][j] / (scale[i] ? scale[i] : 1);

    out->vecs[0][3] = shift[0];
    out->vecs[1][3] = shift[1];
}

static void
SetTexinfo_QuArK(parser_t *parser, vec3_t planepts[3], texcoord_style_t style,
		 texinfo_t *out)
{
    int i;
    vec3_t vecs[2];
    vec_t a, b, c, d;
    vec_t determinant;

    /*
     * Type 1 uses vecs[0] = (pt[2] - pt[0]) and vecs[1] = (pt[1] - pt[0])
     * Type 2 reverses the order of the vecs
     * 128 is the scaling factor assumed by QuArK.
     */
    switch (style) {
    case TX_QUARK_TYPE1:
	VectorSubtract(planepts[2], planepts[0], vecs[0]);
	VectorSubtract(planepts[1], planepts[0], vecs[1]);
	break;
    case TX_QUARK_TYPE2:
	VectorSubtract(planepts[1], planepts[0], vecs[0]);
	VectorSubtract(planepts[2], planepts[0], vecs[1]);
	break;
    default:
	Error(errBadTXStyle);
    }
    VectorScale(vecs[0], 1.0 / 128.0, vecs[0]);
    VectorScale(vecs[1], 1.0 / 128.0, vecs[1]);

    a = DotProduct(vecs[0], vecs[0]);
    b = DotProduct(vecs[0], vecs[1]);
    c = b; /* DotProduct(vecs[1], vecs[0]); */
    d = DotProduct(vecs[1], vecs[1]);

    /*
     * Want to solve for out->vecs:
     *
     *    | a b | | out->vecs[0] | = | vecs[0] |
     *    | c d | | out->vecs[1] |   | vecs[1] |
     *
     * => | out->vecs[0] | = __ 1.0__  | d  -b | | vecs[0] |
     *    | out->vecs[1] |   a*d - b*c | -c  a | | vecs[1] |
     */
    determinant = a * d - b * c;
    if (fabs(determinant) < ZERO_EPSILON) {
	Message(msgWarning, warnDegenerateQuArKTX, parser->linenum);
	for (i = 0; i < 3; i++)
	    out->vecs[0][i] = out->vecs[1][i] = 0;
    } else {
	for (i = 0; i < 3; i++) {
	    out->vecs[0][i] = (d * vecs[0][i] - b * vecs[1][i]) / determinant;
	    out->vecs[1][i] = (a * vecs[1][i] - c * vecs[0][i]) / determinant;
	}
    }

    /* Finally, the texture offset is indicated by planepts[0] */
    for (i = 0; i < 3; ++i) {
	vecs[0][i] = out->vecs[0][i];
	vecs[1][i] = out->vecs[1][i];
    }
    out->vecs[0][3] = -DotProduct(vecs[0], planepts[0]);
    out->vecs[1][3] = -DotProduct(vecs[1], planepts[0]);
}


static void
ParseBrush(parser_t *parser, mapbrush_t *brush)
{
    vec3_t planepts[3];
    vec3_t t1, t2, t3;
    int i, j;
    texinfo_t tx;
    vec_t d;
    int shift[2], rotate;
    vec_t scale[2];
    int tx_type;
    plane_t *plane;
    mapface_t *face, *checkface;

    brush->faces = face = map.faces + map.numfaces;
    while (ParseToken(parser, PARSE_NORMAL)) {
	if (!strcmp(parser->token, "}"))
	    break;

	// read the three point plane definition
	for (i = 0; i < 3; i++) {
	    if (i != 0)
		ParseToken(parser, PARSE_NORMAL);
	    if (strcmp(parser->token, "("))
		Error(errInvalidMapPlane, parser->linenum);

	    for (j = 0; j < 3; j++) {
		ParseToken(parser, PARSE_SAMELINE);
		planepts[i][j] = atof(parser->token);
	    }

	    ParseToken(parser, PARSE_SAMELINE);
	    if (strcmp(parser->token, ")"))
		Error(errInvalidMapPlane, parser->linenum);
	}

	// read the texturedef
	memset(&tx, 0, sizeof(tx));
	ParseToken(parser, PARSE_SAMELINE);
	tx.miptex = FindMiptex(parser->token);
	ParseToken(parser, PARSE_SAMELINE);
	shift[0] = atoi(parser->token);
	ParseToken(parser, PARSE_SAMELINE);
	shift[1] = atoi(parser->token);
	ParseToken(parser, PARSE_SAMELINE);
	rotate = atoi(parser->token);
	ParseToken(parser, PARSE_SAMELINE);
	scale[0] = atof(parser->token);
	ParseToken(parser, PARSE_SAMELINE);
	scale[1] = atof(parser->token);

	// if the three points are all on a previous plane, it is a
	// duplicate plane
	for (checkface = brush->faces; checkface < face; checkface++) {
	    plane = &checkface->plane;
	    for (i = 0; i < 3; i++) {
		d = DotProduct(planepts[i], plane->normal) - plane->dist;
		if (d < -ON_EPSILON || d > ON_EPSILON)
		    break;
	    }
	    if (i == 3)
		break;
	}
	if (checkface < face) {
	    Message(msgWarning, warnBrushDuplicatePlane, parser->linenum);
	    continue;
	}

	if (map.numfaces == map.maxfaces)
	    Error(errLowFaceCount);

	// convert to a vector / dist plane
	for (j = 0; j < 3; j++) {
	    t1[j] = planepts[0][j] - planepts[1][j];
	    t2[j] = planepts[2][j] - planepts[1][j];
	    t3[j] = planepts[1][j];
	}

	plane = &face->plane;
	CrossProduct(t1, t2, plane->normal);
	if (VectorCompare(plane->normal, vec3_origin)) {
	    Message(msgWarning, warnNoPlaneNormal, parser->linenum);
	    continue;
	}
	VectorNormalize(plane->normal);
	plane->dist = DotProduct(t3, plane->normal);

	tx_type = ParseExtendedTX(parser);
	switch (tx_type) {
	case TX_QUARK_TYPE1:
	case TX_QUARK_TYPE2:
	    SetTexinfo_QuArK(parser, &planepts[0], tx_type, &tx);
	    break;
	default:
	    SetTexinfo_QuakeEd(plane, shift, rotate, scale, &tx);
	    break;
	}
	face->texinfo = FindTexinfo(&tx);

	face++;
	map.numfaces++;
	Message(msgPercent, map.numfaces, map.maxfaces);
    }

    brush->numfaces = face - brush->faces;
    if (!brush->numfaces)
	brush->faces = NULL;
}

static bool
ParseEntity(parser_t *parser, mapentity_t *ent)
{
    mapbrush_t *brush;

    if (!ParseToken(parser, PARSE_NORMAL))
	return false;

    if (strcmp(parser->token, "{"))
	Error(errParseEntity, parser->linenum);

    if (map.numentities == map.maxentities)
	Error(errLowEntCount);

    ent->mapbrushes = brush = map.brushes + map.numbrushes;
    do {
	if (!ParseToken(parser, PARSE_NORMAL))
	    Error(errUnexpectedEOF);
	if (!strcmp(parser->token, "}"))
	    break;
	else if (!strcmp(parser->token, "{")) {
	    if (map.numbrushes == map.maxbrushes)
		Error(errLowMapbrushCount);
	    ParseBrush(parser, brush++);
	    map.numbrushes++;
	} else
	    ParseEpair(parser, ent);
    } while (1);

    ent->nummapbrushes = brush - ent->mapbrushes;
    if (!ent->nummapbrushes)
	ent->mapbrushes = NULL;

    return true;
}


static void
PreParseFile(const char *buf)
{
    int braces = 0;
    struct lumpdata *texinfo;

    map.maxentities = map.maxbrushes = map.maxfaces = 0;

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
		map.maxentities++;
	    else if (braces == 1)
		map.maxbrushes++;
	    braces++;
	} else if (*buf == '}')
	    braces--;
	else if (*buf == '(')
	    map.maxfaces++;
	buf++;
    }

    if (map.maxfaces % 3 != 0)
	Message(msgWarning, warnBadMapFaceCount);
    map.maxfaces /= 3;

    map.faces = AllocMem(MAPFACE, map.maxfaces, true);
    map.brushes = AllocMem(MAPBRUSH, map.maxbrushes, true);
    map.entities = AllocMem(MAPENTITY, map.maxentities, true);

    // While we're here...
    pWorldEnt = map.entities;

    // Allocate maximum memory here, copy over later
    // Maximum possible is one miptex/texinfo per face
    map.maxmiptex = map.maxfaces;
    map.miptex = AllocMem(MIPTEX, map.maxmiptex, true);
    texinfo = &pWorldEnt->lumps[BSPTEXINFO];
    texinfo->data = AllocMem(BSPTEXINFO, map.maxfaces, true);
    texinfo->count = map.maxfaces;
}


void
LoadMapFile(void)
{
    parser_t parser;
    char *buf;
    int i, j, length, cAxis;
    void *pTemp;
    struct lumpdata *texinfo;
    mapentity_t *ent;
    mapbrush_t *brush;
    mapface_t *face, *face2;

    Message(msgProgress, "LoadMapFile");

    length = LoadFile(options.szMapName, &buf, true);
    PreParseFile(buf);
    ParserInit(&parser, buf);

    map.numfaces = map.numbrushes = map.numentities = 0;
    ent = map.entities;
    while (ParseEntity(&parser, ent)) {
	/* Allocate memory for the bmodel, if needed. */
	const char *classname = ValueForKey(ent, "classname");
	if (strcmp(classname, "func_detail") && ent->nummapbrushes) {
	    ent->lumps[BSPMODEL].data = AllocMem(BSPMODEL, 1, true);
	    ent->lumps[BSPMODEL].count = 1;
	}
	map.numentities++;
	ent++;
    }

    /* Double check the entity count matches our pre-parse count */
    if (map.numentities != map.maxentities)
	Error(errLowEntCount);

    FreeMem(buf, OTHER, length + 1);

    // Print out warnings for entities
    if (!(rgfStartSpots & info_player_start))
	Message(msgWarning, warnNoPlayerStart);
    if (!(rgfStartSpots & info_player_deathmatch))
	Message(msgWarning, warnNoPlayerDeathmatch);
//      if (!(rgfStartSpots & info_player_coop))
//              Message(msgWarning, warnNoPlayerCoop);

    // Clean up texture memory
    if (map.nummiptex > map.maxfaces)
	Error(errLowMiptexCount);
    else if (map.nummiptex < map.maxfaces) {
	// For stuff in AddAnimatingTex, make room available
	pTemp = map.miptex;
	map.maxmiptex = map.nummiptex + cAnimtex * 20;
	map.miptex = AllocMem(MIPTEX, map.maxmiptex, true);
	memcpy(map.miptex, pTemp, map.nummiptex * rgcMemSize[MIPTEX]);
	FreeMem(pTemp, MIPTEX, map.maxfaces);
    }

    texinfo = &pWorldEnt->lumps[BSPTEXINFO];
    if (texinfo->index > texinfo->count)
	Error(errLowTexinfoCount);
    else if (texinfo->index < texinfo->count) {
	pTemp = texinfo->data;
	texinfo->data = AllocMem(BSPTEXINFO, texinfo->index, true);
	memcpy(texinfo->data, pTemp, texinfo->index * rgcMemSize[BSPTEXINFO]);
	FreeMem(pTemp, BSPTEXINFO, texinfo->count);
	texinfo->count = texinfo->index;
    }
    // One plane per face + 6 for portals
    map.maxplanes = map.numfaces + 6;

    // Count # of unique planes in all of the faces
    for (i = 0, face = map.faces; i < map.numfaces; i++, face++) {
	face->fUnique = true;
	for (j = 0, face2 = map.faces; j < i; j++, face2++) {
	    if (face2->fUnique &&
		VectorCompare(face->plane.normal, face2->plane.normal) &&
		fabs(face->plane.dist - face2->plane.dist) < EQUAL_EPSILON) {
		face->fUnique = false;
		map.maxplanes--;
		break;
	    }
	}
    }

    /*
     * Now iterate through brushes, add one plane for each face below 6 axis
     * aligned faces. This compensates for planes added in ExpandBrush.
     */
    for (i = 0, brush = map.brushes; i < map.numbrushes; i++, brush++) {
	cAxis = 0;
	for (j = 0, face = brush->faces; j < brush->numfaces; j++, face++) {
	    if (fabs(face->plane.normal[0]) > 1 - NORMAL_EPSILON
		|| fabs(face->plane.normal[1]) > 1 - NORMAL_EPSILON
		|| fabs(face->plane.normal[2]) > 1 - NORMAL_EPSILON)
		cAxis++;
	}
	if (6 - cAxis > 0)
	    map.maxplanes += 6 - cAxis;
    }

    /*
     * map.maxplanes*3 because of 3 hulls, then add 20% as a fudge factor for
     * hull edge bevel planes
     */
    map.maxplanes = 3 * map.maxplanes + map.maxplanes / 5;
    map.planes = AllocMem(PLANE, map.maxplanes, true);

    Message(msgStat, "%5i faces", map.numfaces);
    Message(msgStat, "%5i brushes", map.numbrushes);
    Message(msgStat, "%5i entities", map.numentities);
    Message(msgStat, "%5i unique texnames", map.nummiptex);
    Message(msgStat, "%5i texinfo", texinfo->count);
    Message(msgLiteral, "\n");
}


void
PrintEntity(const mapentity_t *ent)
{
    epair_t *epair;

    for (epair = ent->epairs; epair; epair = epair->next)
	Message(msgStat, "%20s : %s", epair->key, epair->value);
}


const char *
ValueForKey(const mapentity_t *ent, const char *key)
{
    const epair_t *ep;

    for (ep = ent->epairs; ep; ep = ep->next)
	if (!strcmp(ep->key, key))
	    return ep->value;

    return "";
}


void
SetKeyValue(mapentity_t *ent, const char *key, const char *value)
{
    epair_t *ep;

    for (ep = ent->epairs; ep; ep = ep->next)
	if (!strcmp(ep->key, key)) {
	    free(ep->value); /* FIXME */
	    ep->value = copystring(value);
	    return;
	}
    ep = AllocMem(OTHER, sizeof(epair_t), true);
    ep->next = ent->epairs;
    ent->epairs = ep;
    ep->key = copystring(key);
    ep->value = copystring(value);
}


void
GetVectorForKey(const mapentity_t *ent, const char *szKey, vec3_t vec)
{
    const char *value;
    double v1, v2, v3;

    value = ValueForKey(ent, szKey);
    v1 = v2 = v3 = 0;
    // scanf into doubles, then assign, so it is vec_t size independent
    sscanf(value, "%lf %lf %lf", &v1, &v2, &v3);
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
    int i;
    int cLen;
    struct lumpdata *entities;
    const mapentity_t *ent;
    const char *classname;

    map.cTotal[BSPENT] = 0;

    for (i = 0, ent = map.entities; i < map.numentities; i++, ent++) {
	entities = &map.entities[i].lumps[BSPENT];

	/* Check if entity needs to be removed */
	classname = ValueForKey(ent, "classname");
	if (!ent->epairs || !strcmp(classname, "func_detail")) {
	    entities->count = 0;
	    entities->data = NULL;
	    continue;
	}

	cLen = 0;
	for (ep = ent->epairs; ep; ep = ep->next) {
	    int i = strlen(ep->key) + strlen(ep->value) + 6;
	    if (i <= 128)
		cLen += i;
	    else
		cLen += 128;
	}
	// Add 4 for {\n and }\n
	cLen += 4;

	entities->count = cLen;
	map.cTotal[BSPENT] += cLen;
	entities->data = pCur = AllocMem(BSPENT, cLen, true);
	*pCur = 0;

	strcat(pCur, "{\n");
	pCur += 2;

	for (ep = ent->epairs; ep; ep = ep->next) {
	    // Limit on Quake's strings of 128 bytes
	    sprintf(szLine, "\"%.*s\" \"%.*s\"\n", MAX_KEY, ep->key,
		    122 - (int)strlen(ep->key), ep->value);
	    strcat(pCur, szLine);
	    pCur += strlen(szLine);
	}

	// No terminating null on this string
	pCur[0] = '}';
	pCur[1] = '\n';
    }
}
