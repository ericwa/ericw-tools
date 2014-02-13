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

#include <ctype.h>
#include <string.h>

#include "qbsp.h"
#include "parser.h"

#define info_player_start	1
#define info_player_deathmatch	2
#define	info_player_coop	4

static int rgfStartSpots;

static void
AddAnimTex(const char *name)
{
    int i, j, frame;
    char framename[16], basechar = '0';

    frame = name[1];
    if (frame >= 'a' && frame <= 'j')
	frame -= 'a' - 'A';

    if (frame >= '0' && frame <= '9') {
	frame -= '0';
	basechar = '0';
    } else if (frame >= 'A' && frame <= 'J') {
	frame -= 'A';
	basechar = 'A';
    }

    if (frame < 0 || frame > 9)
	Error("Bad animating texture %s", name);

    /*
     * Always add the lower numbered animation frames first, otherwise
     * many Quake engines will exit with an error loading the bsp.
     */
    snprintf(framename, sizeof(framename), "%s", name);
    for (i = 0; i < frame; i++) {
	framename[1] = basechar + i;
	for (j = 0; j < map.nummiptex; j++) {
	    if (!strcasecmp(framename, map.miptex[j]))
		break;
	}
	if (j < map.nummiptex)
	    continue;
	if (map.nummiptex == map.maxmiptex)
	    Error("Internal error: map.nummiptex > map.maxmiptex");

	snprintf(map.miptex[j], sizeof(map.miptex[j]), "%s", framename);
	map.nummiptex++;
    }
}

int
FindMiptex(const char *name)
{
    const char *pathsep;
    int i;

    /* Ignore leading path in texture names (Q2 map compatibility) */
    pathsep = strrchr(name, '/');
    if (pathsep)
	name = pathsep + 1;

    for (i = 0; i < map.nummiptex; i++) {
	if (!strcasecmp(name, map.miptex[i]))
	    return i;
    }
    if (map.nummiptex == map.maxmiptex)
	Error("Internal error: map.nummiptex > map.maxmiptex");

    /* Handle animating textures carefully */
    if (name[0] == '+') {
	AddAnimTex(name);
	i = map.nummiptex;
    }

    snprintf(map.miptex[i], sizeof(map.miptex[i]), "%s", name);
    map.nummiptex++;

    return i;
}

static bool
IsSkipName(const char *name)
{
    if (options.fNoskip)
	return false;
    if (!strcasecmp(name, "skip"))
	return true;
    if (!strcasecmp(name, "*waterskip"))
	return true;
    if (!strcasecmp(name, "*slimeskip"))
	return true;
    if (!strcasecmp(name, "*lavaskip"))
	return true;
    return false;
}

static bool
IsSplitName(const char *name)
{
    if (options.fSplitspecial)
	return false;
    if (name[0] == '*' || !strncasecmp(name, "sky", 3))
	return true;
    return false;
}

static bool
IsHintName(const char *name)
{
    if (!strcasecmp(name, "hint"))
	return true;
    if (!strcasecmp(name, "hintskip"))
	return true;
    return false;
}

/*
===============
FindTexinfo

Returns a global texinfo number
===============
*/
static int
FindTexinfo(texinfo_t *texinfo)
{
    int index, j;
    texinfo_t *target;
    const char *texname;
    const int num_texinfo = pWorldEnt->lumps[LUMP_TEXINFO].index;

    /* Set the texture flags */
    texinfo->flags = 0;
    texname = map.miptex[texinfo->miptex];
    if (IsSkipName(texname))
	texinfo->flags |= TEX_SKIP;
    if (IsHintName(texname))
	texinfo->flags |= TEX_HINT;
    if (IsSplitName(texname))
	texinfo->flags |= TEX_SPECIAL;

    target = pWorldEnt->lumps[LUMP_TEXINFO].data;
    for (index = 0; index < num_texinfo; index++, target++) {
	if (texinfo->miptex != target->miptex)
	    continue;
	if (texinfo->flags != target->flags)
	    continue;

	/* Don't worry about texture alignment on skip or hint surfaces */
	if (texinfo->flags & (TEX_SKIP | TEX_HINT))
	    return index;

	for (j = 0; j < 4; j++) {
	    if (texinfo->vecs[0][j] != target->vecs[0][j])
		break;
	    if (texinfo->vecs[1][j] != target->vecs[1][j])
		break;
	}
	if (j != 4)
	    continue;

	return index;
    }

    /* Allocate a new texinfo at the end of the array */
    *target = *texinfo;
    pWorldEnt->lumps[LUMP_TEXINFO].index++;
    map.cTotal[LUMP_TEXINFO]++;

    return index;
}


static void
ParseEpair(parser_t *parser, mapentity_t *entity)
{
    epair_t *epair;

    epair = AllocMem(OTHER, sizeof(epair_t), true);
    epair->next = entity->epairs;
    entity->epairs = epair;

    if (strlen(parser->token) >= MAX_KEY - 1)
	goto parse_error;
    epair->key = copystring(parser->token);
    ParseToken(parser, PARSE_SAMELINE);
    if (strlen(parser->token) >= MAX_VALUE - 1)
	goto parse_error;
    epair->value = copystring(parser->token);

    if (!strcasecmp(epair->key, "origin")) {
	GetVectorForKey(entity, epair->key, entity->origin);
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
    return;

 parse_error:
    Error("line %d: Entity key or value too long", parser->linenum);
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
    TX_QUAKED      = 0,
    TX_QUARK_TYPE1 = 1,
    TX_QUARK_TYPE2 = 2,
    TX_VALVE_220   = 3,
} texcoord_style_t;

static texcoord_style_t
ParseExtendedTX(parser_t *parser)
{
    texcoord_style_t style = TX_QUAKED;

    if (ParseToken(parser, PARSE_COMMENT | PARSE_OPTIONAL)) {
	if (!strncmp(parser->token, "//TX", 4)) {
	    if (parser->token[4] == '1')
		style = TX_QUARK_TYPE1;
	    else if (parser->token[4] == '2')
		style = TX_QUARK_TYPE2;
	}
    } else {
	/* Throw away extra Quake 2 surface info */
	ParseToken(parser, PARSE_OPTIONAL); /* contents */
	ParseToken(parser, PARSE_OPTIONAL); /* flags */
	ParseToken(parser, PARSE_OPTIONAL); /* value */
    }

    return style;
}

static void
SetTexinfo_QuakeEd(const plane_t *plane, const vec_t shift[2], vec_t rotate,
		   const vec_t scale[2], texinfo_t *out)
{
    int i, j;
    vec3_t vecs[2];
    int sv, tv;
    vec_t ang, sinv, cosv;
    vec_t ns, nt;

    TextureAxisFromPlane(plane, vecs[0], vecs[1]);

    /* Rotate axis */
    ang = rotate / 180.0 * Q_PI;
    sinv = sin(ang);
    cosv = cos(ang);

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
SetTexinfo_QuArK(parser_t *parser, vec3_t planepts[3],
		 texcoord_style_t style, texinfo_t *out)
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
	Error("Internal error: bad texture coordinate style");
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
SetTexinfo_Valve220(vec3_t axis[2], const vec_t shift[2], const vec_t scale[2],
		    texinfo_t *out)
{
    int i;

    for (i = 0; i < 3; i++) {
	out->vecs[0][i] = axis[0][i] / scale[0];
	out->vecs[1][i] = axis[1][i] / scale[1];
    }
    out->vecs[0][3] = shift[0];
    out->vecs[1][3] = shift[1];
}

static void
ParsePlaneDef(parser_t *parser, vec3_t planepts[3])
{
    int i, j;

    for (i = 0; i < 3; i++) {
	if (i != 0)
	    ParseToken(parser, PARSE_NORMAL);
	if (strcmp(parser->token, "("))
	    goto parse_error;

	for (j = 0; j < 3; j++) {
	    ParseToken(parser, PARSE_SAMELINE);
	    planepts[i][j] = atof(parser->token);
	}

	ParseToken(parser, PARSE_SAMELINE);
	if (strcmp(parser->token, ")"))
	    goto parse_error;
    }
    return;

 parse_error:
    Error("line %d: Invalid brush plane format", parser->linenum);
}

static void
ParseValve220TX(parser_t *parser, vec3_t axis[2], vec_t shift[2],
		vec_t *rotate, vec_t scale[2])
{
    int i, j;

    for (i = 0; i < 2; i++) {
	ParseToken(parser, PARSE_SAMELINE);
	if (strcmp(parser->token, "["))
	    goto parse_error;
	for (j = 0; j < 3; j++) {
	    ParseToken(parser, PARSE_SAMELINE);
	    axis[i][j] = atof(parser->token);
	}
	ParseToken(parser, PARSE_SAMELINE);
	shift[i] = atof(parser->token);
	ParseToken(parser, PARSE_SAMELINE);
	if (strcmp(parser->token, "]"))
	    goto parse_error;
    }
    ParseToken(parser, PARSE_SAMELINE);
    rotate[0] = atof(parser->token);
    ParseToken(parser, PARSE_SAMELINE);
    scale[0] = atof(parser->token);
    ParseToken(parser, PARSE_SAMELINE);
    scale[1] = atof(parser->token);
    return;

 parse_error:
    Error("line %d: couldn't parse Valve220 texture info", parser->linenum);
}

static void
ParseTextureDef(parser_t *parser, texinfo_t *tx,
		vec3_t planepts[3], const plane_t *plane)
{
    vec3_t axis[2];
    vec_t shift[2], rotate, scale[2];
    texcoord_style_t tx_type;

    memset(tx, 0, sizeof(*tx));
    ParseToken(parser, PARSE_SAMELINE);
    tx->miptex = FindMiptex(parser->token);
    ParseToken(parser, PARSE_SAMELINE);
    if (!strcmp(parser->token, "[")) {
	parser->unget = true;
	ParseValve220TX(parser, axis, shift, &rotate, scale);
	tx_type = TX_VALVE_220;
    } else {
	shift[0] = atof(parser->token);
	ParseToken(parser, PARSE_SAMELINE);
	shift[1] = atof(parser->token);
	ParseToken(parser, PARSE_SAMELINE);
	rotate = atof(parser->token);
	ParseToken(parser, PARSE_SAMELINE);
	scale[0] = atof(parser->token);
	ParseToken(parser, PARSE_SAMELINE);
	scale[1] = atof(parser->token);
	tx_type = ParseExtendedTX(parser);
    }

    if (!planepts || !plane)
	return;

    switch (tx_type) {
    case TX_QUARK_TYPE1:
    case TX_QUARK_TYPE2:
	SetTexinfo_QuArK(parser, &planepts[0], tx_type, tx);
	break;
    case TX_VALVE_220:
	SetTexinfo_Valve220(axis, shift, scale, tx);
	break;
    case TX_QUAKED:
    default:
	SetTexinfo_QuakeEd(plane, shift, rotate, scale, tx);
	break;
    }
}

static bool
ParseBrushFace(parser_t *parser, mapface_t *face)
{
    vec3_t planepts[3], planevecs[2];
    vec_t length;
    plane_t *plane;
    texinfo_t tx;

    face->linenum = parser->linenum;
    ParsePlaneDef(parser, planepts);

    /* calculate the normal/dist plane equation */
    VectorSubtract(planepts[0], planepts[1], planevecs[0]);
    VectorSubtract(planepts[2], planepts[1], planevecs[1]);
    plane = &face->plane;
    CrossProduct(planevecs[0], planevecs[1], plane->normal);
    length = VectorNormalize(plane->normal);
    plane->dist = DotProduct(planepts[1], plane->normal);

    ParseTextureDef(parser, &tx, planepts, plane);

    if (length < NORMAL_EPSILON) {
	Message(msgWarning, warnNoPlaneNormal, parser->linenum);
	return false;
    }

    face->texinfo = FindTexinfo(&tx);

    return true;
}

static void
ParseBrush(parser_t *parser, mapbrush_t *brush)
{
    const mapface_t *check;
    mapface_t *face;
    bool faceok;

    brush->faces = face = map.faces + map.numfaces;
    while (ParseToken(parser, PARSE_NORMAL)) {
	if (!strcmp(parser->token, "}"))
	    break;

	if (map.numfaces == map.maxfaces)
	    Error("Internal error: didn't allocate enough faces?");

	faceok = ParseBrushFace(parser, face);
	if (!faceok)
	    continue;

	/* Check for duplicate planes */
	for (check = brush->faces; check < face; check++) {
	    if (PlaneEqual(&check->plane, &face->plane)) {
		Message(msgWarning, warnBrushDuplicatePlane, parser->linenum);
		continue;
	    }
	    if (PlaneInvEqual(&check->plane, &face->plane)) {
		/* FIXME - this is actually an invalid brush */
		Message(msgWarning, warnBrushDuplicatePlane, parser->linenum);
		continue;
	    }
	}

	/* Save the face, update progress */
	map.numfaces++;
	Message(msgPercent, map.numfaces, map.maxfaces);
	face++;
    }

    brush->numfaces = face - brush->faces;
    if (!brush->numfaces)
	brush->faces = NULL;
}

static bool
ParseEntity(parser_t *parser, mapentity_t *entity)
{
    mapbrush_t *brush;

    if (!ParseToken(parser, PARSE_NORMAL))
	return false;

    if (strcmp(parser->token, "{"))
	Error("line %d: Invalid entity format, { not found", parser->linenum);

    if (map.numentities == map.maxentities)
	Error("Internal error: didn't allocate enough entities?");

    entity->mapbrushes = brush = map.brushes + map.numbrushes;
    do {
	if (!ParseToken(parser, PARSE_NORMAL))
	    Error("Unexpected EOF (no closing brace)");
	if (!strcmp(parser->token, "}"))
	    break;
	else if (!strcmp(parser->token, "{")) {
	    if (map.numbrushes == map.maxbrushes)
		Error("Internal error: didn't allocate enough brushes?");
	    ParseBrush(parser, brush++);
	    map.numbrushes++;
	} else
	    ParseEpair(parser, entity);
    } while (1);

    entity->nummapbrushes = brush - entity->mapbrushes;
    if (!entity->nummapbrushes)
	entity->mapbrushes = NULL;

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
	} else if (*buf == '{' && (isspace(buf[1]) || !buf[1])) {
	    if (braces == 0)
		map.maxentities++;
	    else if (braces == 1)
		map.maxbrushes++;
	    braces++;
	} else if (*buf == '}' && (isspace(buf[1]) || !buf[1])) {
	    braces--;
	} else if (*buf == '(') {
	    map.maxfaces++;
	}
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

    /*
     * Allocate maximum memory here, copy over later
     * Maximum possible is one miptex/texinfo per face
     * Plus a few extra for animations
     */
    map.maxmiptex = map.maxfaces + 100;
    map.miptex = AllocMem(MIPTEX, map.maxmiptex, true);
    texinfo = &pWorldEnt->lumps[LUMP_TEXINFO];
    texinfo->data = AllocMem(BSP_TEXINFO, map.maxfaces, true);
    texinfo->count = map.maxfaces;
}

/*
 * Special world entities are entities which have their brushes added to the
 * world before being removed from the map. Currently func_detail and
 * func_group.
 */
static bool
IsWorldBrushEntity(const mapentity_t *entity)
{
    const char *classname = ValueForKey(entity, "classname");

    if (!strcasecmp(classname, "func_detail"))
	return true;
    if (!strcasecmp(classname, "func_group"))
	return true;
    return false;
}


void
LoadMapFile(void)
{
    parser_t parser;
    char *buf;
    int i, j, length, cAxis;
    void *pTemp;
    struct lumpdata *texinfo;
    mapentity_t *entity;
    mapbrush_t *brush;
    mapface_t *face, *face2;

    Message(msgProgress, "LoadMapFile");

    length = LoadFile(options.szMapName, &buf, true);
    PreParseFile(buf);
    ParserInit(&parser, buf);

    map.numfaces = map.numbrushes = map.numentities = 0;
    entity = map.entities;
    while (ParseEntity(&parser, entity)) {
	/* Allocate memory for the bmodel, if needed. */
	if (!IsWorldBrushEntity(entity) && entity->nummapbrushes) {
	    entity->lumps[LUMP_MODELS].data = AllocMem(BSP_MODEL, 1, true);
	    entity->lumps[LUMP_MODELS].count = 1;
	}
	map.numentities++;
	entity++;
    }

    /* Double check the entity count matches our pre-parse count */
    if (map.numentities != map.maxentities)
	Error("Internal error: mismatched entity count?");

    FreeMem(buf, OTHER, length + 1);

    // Print out warnings for entities
    if (!(rgfStartSpots & info_player_start))
	Message(msgWarning, warnNoPlayerStart);
    if (!(rgfStartSpots & info_player_deathmatch))
	Message(msgWarning, warnNoPlayerDeathmatch);
//      if (!(rgfStartSpots & info_player_coop))
//              Message(msgWarning, warnNoPlayerCoop);

    texinfo = &pWorldEnt->lumps[LUMP_TEXINFO];
    if (texinfo->index > texinfo->count)
	Error("Internal error: didn't allocate enough texinfos?");
    else if (texinfo->index < texinfo->count) {
	pTemp = texinfo->data;
	texinfo->data = AllocMem(BSP_TEXINFO, texinfo->index, true);
	memcpy(texinfo->data, pTemp, texinfo->index * MemSize[BSP_TEXINFO]);
	FreeMem(pTemp, BSP_TEXINFO, texinfo->count);
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

    Message(msgStat, "%8d faces", map.numfaces);
    Message(msgStat, "%8d brushes", map.numbrushes);
    Message(msgStat, "%8d entities", map.numentities);
    Message(msgStat, "%8d unique texnames", map.nummiptex);
    Message(msgStat, "%8d texinfo", texinfo->count);
    Message(msgLiteral, "\n");
}


void
PrintEntity(const mapentity_t *entity)
{
    epair_t *epair;

    for (epair = entity->epairs; epair; epair = epair->next)
	Message(msgStat, "%20s : %s", epair->key, epair->value);
}


const char *
ValueForKey(const mapentity_t *entity, const char *key)
{
    const epair_t *ep;

    for (ep = entity->epairs; ep; ep = ep->next)
	if (!strcasecmp(ep->key, key))
	    return ep->value;

    return "";
}


void
SetKeyValue(mapentity_t *entity, const char *key, const char *value)
{
    epair_t *ep;

    for (ep = entity->epairs; ep; ep = ep->next)
	if (!strcasecmp(ep->key, key)) {
	    free(ep->value); /* FIXME */
	    ep->value = copystring(value);
	    return;
	}
    ep = AllocMem(OTHER, sizeof(epair_t), true);
    ep->next = entity->epairs;
    entity->epairs = ep;
    ep->key = copystring(key);
    ep->value = copystring(value);
}


void
GetVectorForKey(const mapentity_t *entity, const char *szKey, vec3_t vec)
{
    const char *value;
    double v1, v2, v3;

    value = ValueForKey(entity, szKey);
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
    const mapentity_t *entity;

    map.cTotal[LUMP_ENTITIES] = 0;

    for (i = 0, entity = map.entities; i < map.numentities; i++, entity++) {
	entities = &map.entities[i].lumps[LUMP_ENTITIES];

	/* Check if entity needs to be removed */
	if (!entity->epairs || IsWorldBrushEntity(entity)) {
	    entities->count = 0;
	    entities->data = NULL;
	    continue;
	}

	cLen = 0;
	for (ep = entity->epairs; ep; ep = ep->next) {
	    int i = strlen(ep->key) + strlen(ep->value) + 6;
	    if (i <= 128)
		cLen += i;
	    else
		cLen += 128;
	}
	// Add 4 for {\n and }\n
	cLen += 4;

	entities->count = cLen;
	map.cTotal[LUMP_ENTITIES] += cLen;
	entities->data = pCur = AllocMem(BSP_ENT, cLen, true);
	*pCur = 0;

	strcat(pCur, "{\n");
	pCur += 2;

	for (ep = entity->epairs; ep; ep = ep->next) {
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
