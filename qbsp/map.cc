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

#include <string>
#include <memory>

#include <ctype.h>
#include <string.h>
#include <regex>

#include "qbsp.hh"
#include "parser.hh"
#include "wad.hh"

#define info_player_start       1
#define info_player_deathmatch  2
#define info_player_coop        4

static int rgfStartSpots;

const mapface_t &mapbrush_t::face(int i) const {
    if (i < 0 || i >= this->numfaces)
        Error("mapbrush_t::face: %d out of bounds (numfaces %d)", i, this->numfaces);
    return map.faces.at(this->firstface + i);
}

const mapbrush_t &mapentity_t::mapbrush(int i) const {
    if (i < 0 || i >= this->nummapbrushes)
        Error("mapentity_t::mapbrush: %d out of bounds (nummapbrushes %d)", i, this->nummapbrushes);
    return map.brushes.at(this->firstmapbrush + i);
}

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
    q_snprintf(framename, sizeof(framename), "%s", name);
    for (i = 0; i < frame; i++) {
        framename[1] = basechar + i;
        for (j = 0; j < map.nummiptex(); j++) {
            if (!Q_strcasecmp(framename, map.miptex.at(j).c_str()))
                break;
        }
        if (j < map.nummiptex())
            continue;

        map.miptex.push_back(framename);
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

    for (i = 0; i < map.nummiptex(); i++) {
        if (!Q_strcasecmp(name, map.miptex.at(i).c_str()))
            return i;
    }

    /* Handle animating textures carefully */
    if (name[0] == '+') {
        AddAnimTex(name);
        i = map.nummiptex();
    }

    map.miptex.push_back(name);
    return i;
}

static bool
IsSkipName(const char *name)
{
    if (options.fNoskip)
        return false;
    if (!Q_strcasecmp(name, "skip"))
        return true;
    if (!Q_strcasecmp(name, "*waterskip"))
        return true;
    if (!Q_strcasecmp(name, "*slimeskip"))
        return true;
    if (!Q_strcasecmp(name, "*lavaskip"))
        return true;
    return false;
}

static bool
IsSpecialName(const char *name)
{
    if (options.fSplitspecial)
        return false;
    if (name[0] == '*' && !options.fSplitturb)
        return true;
    if (!Q_strncasecmp(name, "sky", 3) && !options.fSplitsky)
        return true;
    return false;
}

static bool
IsHintName(const char *name)
{
    if (!Q_strcasecmp(name, "hint"))
        return true;
    if (!Q_strcasecmp(name, "hintskip"))
        return true;
    return false;
}

/*
===============
FindTexinfo

Returns a global texinfo number
===============
*/
int
FindTexinfo(mtexinfo_t *texinfo, uint64_t flags)
{
    const size_t num_texinfo = map.mtexinfos.size();

    /* Set the texture flags */
    texinfo->flags = flags;

    for (size_t index = 0; index < num_texinfo; index++) {
        const mtexinfo_t *target = &map.mtexinfos.at(index);
        if (texinfo->miptex != target->miptex)
            continue;
        if (texinfo->flags != target->flags)
            continue;

        /* Don't worry about texture alignment on skip or hint surfaces */
        if (texinfo->flags & (TEX_SKIP | TEX_HINT))
            return index;

        int j;
        for (j = 0; j < 4; j++) {
            if (texinfo->vecs[0][j] != target->vecs[0][j])
                break;
            if (texinfo->vecs[1][j] != target->vecs[1][j])
                break;
        }
        if (j != 4)
            continue;

        return static_cast<int>(index);
    }

    /* Allocate a new texinfo at the end of the array */
    map.mtexinfos.push_back(*texinfo);
    return static_cast<int>(num_texinfo);
}

/* detect colors with components in 0-1 and scale them to 0-255 */
static void
normalize_color_format(vec3_t color)
{
    if (color[0] >= 0 && color[0] <= 1 &&
        color[1] >= 0 && color[1] <= 1 &&
        color[2] >= 0 && color[2] <= 1)
    {
        VectorScale(color, 255, color);
    }
}

int
FindTexinfoEnt(mtexinfo_t *texinfo, const mapentity_t *entity)
{
    uint64_t flags = 0;
    const char *texname = map.miptex.at(texinfo->miptex).c_str();
    if (IsSkipName(texname))
        flags |= TEX_SKIP;
    if (IsHintName(texname))
        flags |= TEX_HINT;
    if (IsSpecialName(texname))
        flags |= TEX_SPECIAL;
    if (atoi(ValueForKey(entity, "_dirt")) == -1)
        flags |= TEX_NODIRT;

    // handle "_phong" and "_phong_angle"
    vec_t phongangle = atof(ValueForKey(entity, "_phong_angle"));
    const int phong = atoi(ValueForKey(entity, "_phong"));
    
    if (phong && (phongangle == 0.0)) {
        phongangle = 89.0; // default _phong_angle
    }
    
    if (phongangle) {
        const uint8_t phongangle_byte = (uint8_t) qmax(0, qmin(255, (int)rint(phongangle)));
        flags |= (phongangle_byte << TEX_PHONG_ANGLE_SHIFT);
    }
    
    // handle "_minlight"
    const vec_t minlight = atof(ValueForKey(entity, "_minlight"));
    if (minlight > 0) {
        const uint8_t minlight_byte = (uint8_t) qmax(0, qmin(255, (int)rint(minlight)));
        flags |= (minlight_byte << TEX_MINLIGHT_SHIFT);
    }

    // handle "_mincolor"
    {
        vec3_t mincolor {0.0, 0.0, 0.0};
    
        GetVectorForKey(entity, "_mincolor", mincolor);
        if (VectorCompare(vec3_origin, mincolor)) {
            GetVectorForKey(entity, "_minlight_color", mincolor);
        }
    
        normalize_color_format(mincolor);
        if (!VectorCompare(vec3_origin, mincolor)) {
            const uint64_t r_byte = qmax(0, qmin(255, (int)rint(mincolor[0])));
            const uint64_t g_byte = qmax(0, qmin(255, (int)rint(mincolor[1])));
            const uint64_t b_byte = qmax(0, qmin(255, (int)rint(mincolor[2])));
            
            flags |= (r_byte << TEX_MINLIGHT_COLOR_R_SHIFT);
            flags |= (g_byte << TEX_MINLIGHT_COLOR_G_SHIFT);
            flags |= (b_byte << TEX_MINLIGHT_COLOR_B_SHIFT);
        }
    }
    
    return FindTexinfo(texinfo, flags);
}


static void
ParseEpair(parser_t *parser, mapentity_t *entity)
{
    epair_t *epair;

    epair = (epair_t *)AllocMem(OTHER, sizeof(epair_t), true);
    epair->next = entity->epairs;
    entity->epairs = epair;

    if (strlen(parser->token) >= MAX_KEY - 1)
        goto parse_error;
    epair->key = copystring(parser->token);
    ParseToken(parser, PARSE_SAMELINE);
    if (strlen(parser->token) >= MAX_VALUE - 1)
        goto parse_error;
    epair->value = copystring(parser->token);

    if (!Q_strcasecmp(epair->key, "origin")) {
        GetVectorForKey(entity, epair->key, entity->origin);
    } else if (!Q_strcasecmp(epair->key, "classname")) {
        if (!Q_strcasecmp(epair->value, "info_player_start")) {
            if (rgfStartSpots & info_player_start)
                Message(msgWarning, warnMultipleStarts);
            rgfStartSpots |= info_player_start;
        } else if (!Q_strcasecmp(epair->value, "info_player_deathmatch")) {
            rgfStartSpots |= info_player_deathmatch;
        } else if (!Q_strcasecmp(epair->value, "info_player_coop")) {
            rgfStartSpots |= info_player_coop;
        }
    }
    return;

 parse_error:
    Error("line %d: Entity key or value too long", parser->linenum);
}


static void
TextureAxisFromPlane(const plane_t *plane, vec3_t xv, vec3_t yv, vec3_t snapped_normal)
{
    vec3_t baseaxis[18] = {
        {0, 0, 1}, {1, 0, 0}, {0, -1, 0},       // floor
        {0, 0, -1}, {1, 0, 0}, {0, -1, 0},      // ceiling
        {1, 0, 0}, {0, 1, 0}, {0, 0, -1},       // west wall
        {-1, 0, 0}, {0, 1, 0}, {0, 0, -1},      // east wall
        {0, 1, 0}, {1, 0, 0}, {0, 0, -1},       // south wall
        {0, -1, 0}, {1, 0, 0}, {0, 0, -1}       // north wall
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
    VectorCopy(baseaxis[bestaxis * 3], snapped_normal);
}

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

class texdef_quake_ed_t {
public:
    vec_t rotate;
    vec_t scale[2];
    vec_t shift[2];
};

static texdef_quake_ed_t
TexDef_BSPToQuakeEd(const plane_t &faceplane, const float in_vecs[2][4])
{
    // First get the un-rotated, un-scaled texture vecs (based on the face plane).
    vec3_t snapped_normal;
    vec3_t unrotated_vecs[2];
    TextureAxisFromPlane(&faceplane, unrotated_vecs[0], unrotated_vecs[1], snapped_normal);
    
    // These axes rotate counterclockwise
    const vec3_t posX { 1,0,0};
    const vec3_t negY { 0,-1,0};
    const vec3_t posZ { 0,0,1};
    
    const bool ccw = (VectorCompare(posX, snapped_normal)
                      || VectorCompare(negY, snapped_normal)
                      || VectorCompare(posZ, snapped_normal));
    
    // Extract the final scale (magnitude only, we don't know the sign yet) and shift values
    // Also normalize the BSP texture axes and store in rotated_vec.
    vec_t scale[2], shift[2];
    vec3_t rotated_vec[2];
    for (int i=0; i<2; i++) {
        for (int j=0; j<3; j++) {
            rotated_vec[i][j] = in_vecs[i][j];
        }
        const vec_t length = VectorNormalize(rotated_vec[i]);
        if (length == 0.0) {
            // Hack around bad input
            scale[i] = 0.0;
        } else {
            scale[i] = 1.0 / length;
        }
        
        shift[i] = in_vecs[i][3];
    }
    
    // We know that both unrotated_vecs were rotated by the same amount,
    // then each possibly was multiplied by -1, to get rotated_vecs.
    //
    // To figure out the signs on the scales, try all 4 possibilities.
    float angles[4][2];
    for (int sign_index=0; sign_index<4; sign_index++) {
        float sign0 = (sign_index / 2) == 1 ? -1.0 : 1.0;
        float sign1 = (sign_index % 2) == 1 ? -1.0 : 1.0;
        
        vec3_t rotated_flipped_vec[2];
        VectorScale(rotated_vec[0], sign0, rotated_flipped_vec[0]);
        VectorScale(rotated_vec[1], sign1, rotated_flipped_vec[1]);
        
        // Try to reproduce the rotation
        for (int i=0; i<2; i++)
        {
            float angle = SignedDegreesBetweenUnitVectors(unrotated_vecs[i], rotated_flipped_vec[i], snapped_normal);
            
            if (ccw)
                angle *= -1;
            
            angles[sign_index][i] = angle;
        }
    }
    
    float bestangle = 0;
    
    float closest_angle_dist = FLT_MAX;
    int best_signindex = -1;
    
    // There should be a combination of signs that give both vectors the same amount of rotation
    for (int i=0; i<4; i++) {
        const float dist = fabs(angles[i][0] - angles[i][1]);
        if (dist < closest_angle_dist) {
            closest_angle_dist = dist;
            bestangle = angles[i][0];
            best_signindex = i;
        }
    }
    
    if (closest_angle_dist > 0.01) {
//        printf("unequal rotation detected\n");
    }
    
    float sign0 = (best_signindex / 2) == 1 ? -1.0 : 1.0;
    float sign1 = (best_signindex % 2) == 1 ? -1.0 : 1.0;
    
    scale[0] *= sign0;
    scale[1] *= sign1;
    
    texdef_quake_ed_t res;
    res.rotate = bestangle;
    for (int i=0; i<2; i++) {
        res.scale[i] = scale[i];
        res.shift[i] = shift[i];
    }
    
    return res;
}

static void
SetTexinfo_QuakeEd(const plane_t *plane, const vec_t shift[2], vec_t rotate,
                   const vec_t scale[2], mtexinfo_t *out)
{
    int i, j;
    vec3_t vecs[2];
    int sv, tv;
    vec_t ang, sinv, cosv;
    vec_t ns, nt;
    vec3_t unused;
    
    TextureAxisFromPlane(plane, vecs[0], vecs[1], unused);


    /* Rotate axis */
    ang = rotate / 180.0 * Q_PI;
    sinv = sin(ang);
    cosv = cos(ang);

    if (vecs[0][0])
        sv = 0;
    else if (vecs[0][1])
        sv = 1;
    else
        sv = 2; // unreachable, due to TextureAxisFromPlane lookup table

    if (vecs[1][0])
        tv = 0; // unreachable, due to TextureAxisFromPlane lookup table
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
                 texcoord_style_t style, mtexinfo_t *out)
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
            out->vecs[1][i] = -(a * vecs[1][i] - c * vecs[0][i]) / determinant;
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
                    mtexinfo_t *out)
{
    int i;

    for (i = 0; i < 3; i++) {
        out->vecs[0][i] = axis[0][i] / scale[0];
        out->vecs[1][i] = axis[1][i] / scale[1];
    }
    out->vecs[0][3] = shift[0];
    out->vecs[1][3] = shift[1];
}

/*
 ComputeAxisBase() 
 from q3map2
 
 computes the base texture axis for brush primitive texturing
 note: ComputeAxisBase here and in editor code must always BE THE SAME!
 warning: special case behaviour of atan2( y, x ) <-> atan( y / x ) might not be the same everywhere when x == 0
 rotation by (0,RotY,RotZ) assigns X to normal
 */
static void ComputeAxisBase( const vec3_t normal_unsanitized, vec3_t texX, vec3_t texY ){
    vec_t RotY, RotZ;
    
    vec3_t normal;
    VectorCopy(normal_unsanitized, normal);
    
    /* do some cleaning */
    if ( fabs( normal[ 0 ] ) < 1e-6 ) {
        normal[ 0 ] = 0.0f;
    }
    if ( fabs( normal[ 1 ] ) < 1e-6 ) {
        normal[ 1 ] = 0.0f;
    }
    if ( fabs( normal[ 2 ] ) < 1e-6 ) {
        normal[ 2 ] = 0.0f;
    }
    
    /* compute the two rotations around y and z to rotate x to normal */
    RotY = -atan2( normal[ 2 ], sqrt( normal[ 1 ] * normal[ 1 ] + normal[ 0 ] * normal[ 0 ] ) );
    RotZ = atan2( normal[ 1 ], normal[ 0 ] );
    
    /* rotate (0,1,0) and (0,0,1) to compute texX and texY */
    texX[ 0 ] = -sin( RotZ );
    texX[ 1 ] = cos( RotZ );
    texX[ 2 ] = 0;
    
    /* the texY vector is along -z (t texture coorinates axis) */
    texY[ 0 ] = -sin( RotY ) * cos( RotZ );
    texY[ 1 ] = -sin( RotY ) * sin( RotZ );
    texY[ 2 ] = -cos( RotY );
}

static void
SetTexinfo_BrushPrimitives(const vec3_t texMat[2], const vec3_t faceNormal, int texWidth, int texHeight, float vecs[2][4])
{
    vec3_t texX, texY;
    
    ComputeAxisBase( faceNormal, texX, texY );
    
/*
 derivation of the conversion below:
 
 classic BSP texture vecs to texture coordinates:
 
   u = (dot(vert, out->vecs[0]) + out->vecs[3]) / texWidth
 
 brush primitives: (starting with q3map2 code, then rearranging it to look like the classic formula)
   
   u = (texMat[0][0] * dot(vert, texX)) + (texMat[0][1] * dot(vert, texY)) + texMat[0][2]
 
 factor out vert:
  
   u = (vert[0] * (texX[0] * texMat[0][0] + texY[0] * texMat[0][1])) 
      + (vert[1] * (texX[1] * texMat[0][0] + texY[1] * texMat[0][1]))
      + (vert[2] * (texX[2] * texMat[0][0] + texY[2] * texMat[0][1]))
      + texMat[0][2];
 
 multiplying that by 1 = (texWidth / texWidth) gives us something in the same shape as the classic formula,
 so we can get out->vecs.
 
 */
    
    vecs[0][0] = texWidth * ((texX[0] * texMat[0][0]) + (texY[0] * texMat[0][1]));
    vecs[0][1] = texWidth * ((texX[1] * texMat[0][0]) + (texY[1] * texMat[0][1]));
    vecs[0][2] = texWidth * ((texX[2] * texMat[0][0]) + (texY[2] * texMat[0][1]));
    vecs[0][3] = texWidth * texMat[0][2];
    
    vecs[1][0] = texHeight * ((texX[0] * texMat[1][0]) + (texY[0] * texMat[1][1]));
    vecs[1][1] = texHeight * ((texX[1] * texMat[1][0]) + (texY[1] * texMat[1][1]));
    vecs[1][2] = texHeight * ((texX[2] * texMat[1][0]) + (texY[2] * texMat[1][1]));
    vecs[1][3] = texHeight * texMat[1][2];
}

class texdef_brush_primitives_t {
public:
    vec3_t texMat[2];
};

static void BSP_GetSTCoordsForPoint(const vec_t *point, const int texSize[2], const float in_vecs[2][4], vec_t *st_out)
{
    for (int i=0; i<2; i++) {
        st_out[i] = (point[0] * in_vecs[i][0]
                     + point[1] * in_vecs[i][1]
                     + point[2] * in_vecs[i][2]
                     +            in_vecs[i][3]) / static_cast<vec_t>(texSize[i]);
    }
}

// From FaceToBrushPrimitFace in GtkRadiant
static texdef_brush_primitives_t
TexDef_BSPToBrushPrimitives(const plane_t plane, const int texSize[2], const float in_vecs[2][4])
{
    vec3_t texX, texY;
    ComputeAxisBase( plane.normal, texX, texY );
    
    // ST of (0,0) (1,0) (0,1)
    vec_t ST[3][5]; // [ point index ] [ xyz ST ]
    
    // compute projection vector
    vec3_t proj;
    VectorCopy( plane.normal,proj );
    VectorScale( proj,plane.dist,proj );
    
    // (0,0) in plane axis base is (0,0,0) in world coordinates + projection on the affine plane
    // (1,0) in plane axis base is texX in world coordinates + projection on the affine plane
    // (0,1) in plane axis base is texY in world coordinates + projection on the affine plane
    // use old texture code to compute the ST coords of these points
    VectorCopy( proj,ST[0] );
    BSP_GetSTCoordsForPoint(&ST[0][0], texSize, in_vecs, &ST[0][3]);
    VectorCopy( texX,ST[1] );
    VectorAdd( ST[1],proj,ST[1] );
    BSP_GetSTCoordsForPoint(&ST[1][0], texSize, in_vecs, &ST[1][3]);
    VectorCopy( texY,ST[2] );
    VectorAdd( ST[2],proj,ST[2] );
    BSP_GetSTCoordsForPoint(&ST[2][0], texSize, in_vecs, &ST[2][3]);
    // compute texture matrix
    texdef_brush_primitives_t res;
    res.texMat[0][2] = ST[0][3];
    res.texMat[1][2] = ST[0][4];
    res.texMat[0][0] = ST[1][3] - res.texMat[0][2];
    res.texMat[1][0] = ST[1][4] - res.texMat[1][2];
    res.texMat[0][1] = ST[2][3] - res.texMat[0][2];
    res.texMat[1][1] = ST[2][4] - res.texMat[1][2];
    return res;
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
ParseBrushPrimTX(parser_t *parser, vec3_t texMat[2])
{
    ParseToken(parser, PARSE_SAMELINE);
    if (strcmp(parser->token, "("))
        goto parse_error;
    
    for (int i = 0; i < 2; i++) {
        ParseToken(parser, PARSE_SAMELINE);
        if (strcmp(parser->token, "("))
            goto parse_error;
        
        for (int j = 0; j < 3; j++) {
            ParseToken(parser, PARSE_SAMELINE);
            texMat[i][j] = atof(parser->token);
        }
        
        ParseToken(parser, PARSE_SAMELINE);
        if (strcmp(parser->token, ")"))
            goto parse_error;
    }
    
    ParseToken(parser, PARSE_SAMELINE);
    if (strcmp(parser->token, ")"))
        goto parse_error;
    
    return;
    
parse_error:
    Error("line %d: couldn't parse Brush Primitives texture info", parser->linenum);
}

static void
ParseTextureDef(parser_t *parser, mapface_t &mapface, const mapbrush_t *brush, mtexinfo_t *tx,
                vec3_t planepts[3], const plane_t *plane)
{
    vec3_t texMat[2];
    vec3_t axis[2];
    vec_t shift[2], rotate, scale[2];
    texcoord_style_t tx_type;
    int width, height;
    
    memset(tx, 0, sizeof(*tx));

    if (brush->format == brushformat_t::BRUSH_PRIMITIVES) {
        ParseBrushPrimTX(parser, texMat);
        tx_type = TX_BRUSHPRIM;
        
        ParseToken(parser, PARSE_SAMELINE);
        tx->miptex = FindMiptex(parser->token);
        mapface.texname = std::string(parser->token);
        
        EnsureTexturesLoaded();
        const texture_t *texture = WADList_GetTexture(parser->token);
        width = texture ? texture->width : 64;
        height = texture ? texture->height : 64;
        
        // throw away 3 extra values at end of line
        ParseExtendedTX(parser);
    } else if (brush->format == brushformat_t::NORMAL) {
        ParseToken(parser, PARSE_SAMELINE);
        tx->miptex = FindMiptex(parser->token);
        mapface.texname = std::string(parser->token);
        
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
    case TX_BRUSHPRIM:
        SetTexinfo_BrushPrimitives(texMat, plane->normal, width, height, tx->vecs);
        break;
    case TX_QUAKED:
    default:
        SetTexinfo_QuakeEd(plane, shift, rotate, scale, tx);
        break;
    }
}

static std::unique_ptr<mapface_t>
ParseBrushFace(parser_t *parser, const mapbrush_t *brush, const mapentity_t *entity)
{
    vec3_t planevecs[2];
    vec_t length;
    plane_t *plane;
    mtexinfo_t tx;
    int i, j;
    std::unique_ptr<mapface_t> face { new mapface_t };

    face->linenum = parser->linenum;
    ParsePlaneDef(parser, face->planepts);

    /* calculate the normal/dist plane equation */
    VectorSubtract(face->planepts[0], face->planepts[1], planevecs[0]);
    VectorSubtract(face->planepts[2], face->planepts[1], planevecs[1]);
    plane = &face->plane;
    CrossProduct(planevecs[0], planevecs[1], plane->normal);
    length = VectorNormalize(plane->normal);
    plane->dist = DotProduct(face->planepts[1], plane->normal);

    ParseTextureDef(parser, *face, brush, &tx, face->planepts, plane);

    if (length < NORMAL_EPSILON) {
        Message(msgWarning, warnNoPlaneNormal, parser->linenum);
        return nullptr;
    }

    // ericw -- round texture vector values that are within ZERO_EPSILON of integers,
    // to attempt to attempt to work around corrupted lightmap sizes in DarkPlaces
    // (it uses 32 bit precision in CalcSurfaceExtents)
    for (i = 0; i < 2; i++) {
        for (j = 0; j < 4; j++) {
            vec_t r = Q_rint(tx.vecs[i][j]);
            if (fabs(tx.vecs[i][j] - r) < ZERO_EPSILON)
                tx.vecs[i][j] = r;
        }
    }

    face->texinfo = FindTexinfoEnt(&tx, entity);

    return face;
}

mapbrush_t
ParseBrush(parser_t *parser, const mapentity_t *entity)
{
    mapbrush_t brush;
    
    // ericw -- brush primitives
    if (!ParseToken(parser, PARSE_NORMAL))
        Error("Unexpected EOF after { beginning brush");
    
    if (!strcmp(parser->token, "(")) {
        brush.format = brushformat_t::NORMAL;
        parser->unget = true;
    } else {
        brush.format = brushformat_t::BRUSH_PRIMITIVES;
        
        // optional
        if (!strcmp(parser->token, "brushDef")) {
            if (!ParseToken(parser, PARSE_NORMAL))
                Error("Brush primitives: unexpected EOF (nothing after brushDef)");
        }
        
        // mandatory
        if (strcmp(parser->token, "{"))
            Error("Brush primitives: expected second { at beginning of brush, got \"%s\"", parser->token);
    }
    // ericw -- end brush primitives
    
    while (ParseToken(parser, PARSE_NORMAL)) {
        if (!strcmp(parser->token, "}"))
            break;
        
        std::unique_ptr<mapface_t> face = ParseBrushFace(parser, &brush, entity);
        if (face.get() == nullptr)
            continue;

        /* Check for duplicate planes */
        for (int i = 0; i<brush.numfaces; i++) {
            const mapface_t &check = brush.face(i);
            if (PlaneEqual(&check.plane, &face->plane)) {
                Message(msgWarning, warnBrushDuplicatePlane, parser->linenum);
                continue;
            }
            if (PlaneInvEqual(&check.plane, &face->plane)) {
                /* FIXME - this is actually an invalid brush */
                Message(msgWarning, warnBrushDuplicatePlane, parser->linenum);
                continue;
            }
        }

        /* Save the face, update progress */
        
        if (0 == brush.numfaces)
            brush.firstface = map.faces.size();
        brush.numfaces++;
        map.faces.push_back(*face);
    }
    
    // ericw -- brush primitives - there should be another closing }
    if (brush.format == brushformat_t::BRUSH_PRIMITIVES) {
        if (!ParseToken(parser, PARSE_NORMAL))
            Error("Brush primitives: unexpected EOF (no closing brace)");
        if (strcmp(parser->token, "}"))
            Error("Brush primitives: Expected }, got: %s", parser->token);
    }
    // ericw -- end brush primitives
    
    return brush;
}

static bool
ParseEntity(parser_t *parser, mapentity_t *entity)
{
    if (!ParseToken(parser, PARSE_NORMAL))
        return false;

    if (strcmp(parser->token, "{"))
        Error("line %d: Invalid entity format, { not found", parser->linenum);

    entity->nummapbrushes = 0;
    do {
        if (!ParseToken(parser, PARSE_NORMAL))
            Error("Unexpected EOF (no closing brace)");
        if (!strcmp(parser->token, "}"))
            break;
        else if (!strcmp(parser->token, "{")) {
            mapbrush_t brush = ParseBrush(parser, entity);
            
            if (0 == entity->nummapbrushes)
                entity->firstmapbrush = map.brushes.size();
            entity->nummapbrushes++;
            
            map.brushes.push_back(brush);
        } else
            ParseEpair(parser, entity);
    } while (1);

    return true;
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

    if (!Q_strcasecmp(classname, "func_detail"))
        return true;
    if (!Q_strcasecmp(classname, "func_group"))
        return true;
    return false;
}


void
LoadMapFile(void)
{
    parser_t parser;
    char *buf;
    int length;

    Message(msgProgress, "LoadMapFile");

    length = LoadFile(options.szMapName, &buf, true);
    ParserInit(&parser, buf);

    for (int i=0; ; i++) {
        map.entities.push_back(mapentity_t {});
        mapentity_t *entity = &map.entities.at(i);
        
        if (!ParseEntity(&parser, entity)) {
            break;
        }
    }
    // Remove dummy entity inserted above 
    assert(map.entities.back().epairs == nullptr);
    assert(map.entities.back().numbrushes == 0);
    map.entities.pop_back();

    FreeMem(buf, OTHER, length + 1);

    // Print out warnings for entities
    if (!(rgfStartSpots & info_player_start))
        Message(msgWarning, warnNoPlayerStart);
    if (!(rgfStartSpots & info_player_deathmatch))
        Message(msgWarning, warnNoPlayerDeathmatch);
//      if (!(rgfStartSpots & info_player_coop))
//              Message(msgWarning, warnNoPlayerCoop);

    Message(msgStat, "%8d faces", map.numfaces());
    Message(msgStat, "%8d brushes", map.numbrushes());
    Message(msgStat, "%8d entities", map.numentities());
    Message(msgStat, "%8d unique texnames", map.nummiptex());
    Message(msgStat, "%8d texinfo", map.numtexinfo());
    Message(msgLiteral, "\n");
}

static std::string
TexDefToString_QuarkType1(const mapface_t &mapface, const mtexinfo_t &texinfo)
{
    Error("Unimplemented\n");
    return "";
}

class texdef_valve_t {
public:
    vec3_t axis[2];
    vec_t scale[2];
    vec_t shift[2];
};

static texdef_valve_t
TexDef_BSPToValve(const float in_vecs[2][4])
{
    texdef_valve_t res;
    
// From the valve -> bsp code,
//
//    for (i = 0; i < 3; i++) {
//        out->vecs[0][i] = axis[0][i] / scale[0];
//        out->vecs[1][i] = axis[1][i] / scale[1];
//    }
//
// We'll generate axis vectors of length 1 and pick the necessary scale
    
    for (int i=0; i<2; i++) {
        vec3_t axis;
        for (int j=0; j<3; j++) {
            axis[j] = in_vecs[i][j];
        }
        const vec_t length = VectorNormalize(axis);
        // avoid division by 0
        if (length != 0.0) {
            res.scale[i] = 1.0 / length;
        } else {
            res.scale[i] = 0.0;
        }
        res.shift[i] = in_vecs[i][3];
        VectorCopy(axis, res.axis[i]);
    }
    
    return res;
}

static void
ConvertMapFace(FILE *f, const mapface_t &mapface, const texcoord_style_t format)
{
    // All formats write the plane points the same way
    // FIXME: Not QaArK
    for (int i=0; i<3; i++) {
        fprintf(f, " ( ");
        for (int j=0; j<3; j++) {
            fprintf(f, "%0.17f ", mapface.planepts[i][j]);
        }
        fprintf(f, ") ");
    }
    
    const mtexinfo_t &texinfo = map.mtexinfos.at(mapface.texinfo);
    
    switch(format) {
        case texcoord_style_t::TX_QUAKED: {
            const texdef_quake_ed_t quakeed = TexDef_BSPToQuakeEd(mapface.plane, texinfo.vecs);
            
            fprintf(f, "%s %0.17f %0.17f %0.17f %0.17f %0.17f",
                    mapface.texname.c_str(),
                    quakeed.shift[0],
                    quakeed.shift[1],
                    quakeed.rotate,
                    quakeed.scale[0],
                    quakeed.scale[1]);
            break;
        }
        case texcoord_style_t::TX_QUARK_TYPE1:
            Error("Unimplemented");
            break;
        case texcoord_style_t::TX_VALVE_220: {
            const texdef_valve_t valve = TexDef_BSPToValve(texinfo.vecs);
            
            fprintf(f, "%s [ %0.17f %0.17f %0.17f %0.17f ] [ %0.17f %0.17f %0.17f %0.17f ] 0 %0.17f %0.17f",
                    mapface.texname.c_str(),
                    valve.axis[0][0],
                    valve.axis[0][1],
                    valve.axis[0][2],
                    valve.shift[0],
                    valve.axis[1][0],
                    valve.axis[1][1],
                    valve.axis[1][2],
                    valve.shift[1],
                    valve.scale[0],
                    valve.scale[1]);
            break;
        }
        case texcoord_style_t::TX_BRUSHPRIM: {
            EnsureTexturesLoaded();
            
            const texture_t *texture = WADList_GetTexture(mapface.texname.c_str());
            int texSize[2];
            texSize[0] = texture ? texture->width : 64;
            texSize[1] = texture ? texture->height : 64;
            
            const texdef_brush_primitives_t bp = TexDef_BSPToBrushPrimitives(mapface.plane, texSize, texinfo.vecs);
            fprintf(f, "( ( %0.17f %0.17f %0.17f ) ( %0.17f %0.17f %0.17f ) ) %s",
                    bp.texMat[0][0],
                    bp.texMat[0][1],
                    bp.texMat[0][2],
                    bp.texMat[1][0],
                    bp.texMat[1][1],
                    bp.texMat[1][2],
                    mapface.texname.c_str());
            break;
        }
        default:
            Error("Internal error: unknown texcoord_style_t\n");
    }
    
    fprintf(f, "\n");
}

static void
ConvertMapBrush(FILE *f, const mapbrush_t &mapbrush, const texcoord_style_t format)
{
    fprintf(f, "{\n");
    if (format == texcoord_style_t::TX_BRUSHPRIM) {
        fprintf(f, "brushDef\n");
        fprintf(f, "{\n");
    }
    for (int i=0; i<mapbrush.numfaces; i++) {
        ConvertMapFace(f, mapbrush.face(i), format);
    }
    if (format == texcoord_style_t::TX_BRUSHPRIM) {
        fprintf(f, "}\n");
    }
    fprintf(f, "}\n");
}

static void
ConvertEntity(FILE *f, const mapentity_t *entity, const texcoord_style_t format)
{
    fprintf(f, "{\n");
    for (const epair_t *epair = entity->epairs; epair; epair = epair->next) {
        fprintf(f, "\"%s\" \"%s\"\n", epair->key, epair->value);
    }
    for (int i=0; i<entity->nummapbrushes; i++) {
        ConvertMapBrush(f, entity->mapbrush(i), format);
    }
    fprintf(f, "}\n");
}

static std::string stripExt(const std::string &filename) {
    const std::regex extension_regex(R"(\.[^.\/]+$)");
    const std::string result = std::regex_replace(filename, extension_regex, "");
    return result;
}

void ConvertMapFile(void)
{
    Message(msgProgress, "ConvertMapFile");
    
    std::string filename = stripExt(options.szBSPName);
    
    switch(options.convertMapTexFormat) {
        case texcoord_style_t::TX_QUAKED:
            filename += "-quake.map";
            break;
        case texcoord_style_t::TX_VALVE_220:
            filename += "-valve220.map";
            break;
        case texcoord_style_t::TX_BRUSHPRIM:
            filename += "-bp.map";
            break;
        default:
            Error("Internal error: unknown texcoord_style_t\n");
    }
    
    FILE *f = fopen(filename.c_str(), "wb");
    if (f == nullptr)
        Error("Couldn't open file\n");
    
    for (const mapentity_t &entity : map.entities) {
        ConvertEntity(f, &entity, options.convertMapTexFormat);
    }
    
    fclose(f);
    
    std::string msg("Conversion saved to " + filename + "\n");
    Message(msgLiteral, msg.c_str());
    
    options.fVerbose = false;
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
        if (!Q_strcasecmp(ep->key, key))
            return ep->value;

    return "";
}


void
SetKeyValue(mapentity_t *entity, const char *key, const char *value)
{
    epair_t *ep;

    for (ep = entity->epairs; ep; ep = ep->next)
        if (!Q_strcasecmp(ep->key, key)) {
            free(ep->value); /* FIXME */
            ep->value = copystring(value);
            return;
        }
    ep = (epair_t *)AllocMem(OTHER, sizeof(epair_t), true);
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
    int i;
    int cLen;
    struct lumpdata *entities;
    mapentity_t *entity;

    map.cTotal[LUMP_ENTITIES] = 0;

    for (i = 0; i < map.numentities(); i++) {
        entity = &map.entities.at(i);
        entities = &entity->lumps[LUMP_ENTITIES];

        /* Check if entity needs to be removed */
        if (!entity->epairs || IsWorldBrushEntity(entity)) {
            entities->count = 0;
            entities->data = NULL;
            continue;
        }

        cLen = 0;
        for (ep = entity->epairs; ep; ep = ep->next) {
            int i = strlen(ep->key) + strlen(ep->value) + 6;
            cLen += i;
        }
        // Add 4 for {\n and }\n
        cLen += 4;

        entities->count = cLen;
        map.cTotal[LUMP_ENTITIES] += cLen;
        entities->data = pCur = (char *)AllocMem(BSP_ENT, cLen, true);
        *pCur = 0;

        strcat(pCur, "{\n");
        pCur += 2;

        for (ep = entity->epairs; ep; ep = ep->next) {
            // Limit on Quake's strings of 128 bytes
            // TODO: Warn when limit is exceeded
            int chars_printed = sprintf(pCur, "\"%s\" \"%s\"\n", ep->key, ep->value);
            pCur += chars_printed;
        }

        // No terminating null on this string
        pCur[0] = '}';
        pCur[1] = '\n';
    }
}
