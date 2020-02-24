/*
    Copyright (C) 1996-1997  Id Software, Inc.
    Copyright (C) 1997       Greg Lewis
    Copyright (C) 1999-2005  Id Software, Inc.

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
#include <list>
#include <utility>
#include <cassert>

#include <ctype.h>
#include <string.h>

#include <qbsp/qbsp.hh>
#include <qbsp/parser.hh>
#include <qbsp/wad.hh>

#include <common/qvec.hh>

#define info_player_start       1
#define info_player_deathmatch  2
#define info_player_coop        4

static int rgfStartSpots;

class texdef_valve_t {
public:
    vec3_t axis[2];
    vec_t scale[2];
    vec_t shift[2];
    
    texdef_valve_t() {
        for (int i=0;i<2;i++)
            for (int j=0;j<3;j++)
                axis[i][j] = 0;
        
        for (int i=0;i<2;i++)
            scale[i] = 0;
        
        for (int i=0;i<2;i++)
            shift[i] = 0;
    }
};

class texdef_quake_ed_t {
public:
    vec_t rotate;
    vec_t scale[2];
    vec_t shift[2];
    
    texdef_quake_ed_t() : rotate(0) {
        scale[0] = 0;
        scale[1] = 0;
        shift[0] = 0;
        shift[1] = 0;
    }
};

class texdef_quake_ed_noshift_t {
public:
    vec_t rotate;
    vec_t scale[2];
    
    texdef_quake_ed_noshift_t() : rotate(0) {
        scale[0] = 0;
        scale[1] = 0;
    }
};

class texdef_etp_t {
public:
    vec3_t planepoints[3];
    bool tx2;
    
    texdef_etp_t() : tx2(false) {
        for (int i=0;i<3;i++)
            for (int j=0;j<3;j++)
                planepoints[i][j] = 0;
    }
};

class texdef_brush_primitives_t {
public:
    vec3_t texMat[2];
    
    texdef_brush_primitives_t() {
        for (int i=0;i<2;i++)
            for (int j=0;j<3;j++)
                texMat[i][j] = 0;
    }
};

static texdef_valve_t TexDef_BSPToValve(const float in_vecs[2][4]);
static qvec2f projectToAxisPlane(const vec3_t snapped_normal, qvec3f point);
static texdef_quake_ed_noshift_t Reverse_QuakeEd(qmat2x2f M, const qbsp_plane_t *plane, bool preserveX);
static void SetTexinfo_QuakeEd_New(const qbsp_plane_t *plane, const vec_t shift[2], vec_t rotate, const vec_t scale[2], float out_vecs[2][4]);
static void TestExpandBrushes(const mapentity_t *src);

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
    if (!Q_strcasecmp(name, "bevel"))    //zhlt compat
        return true;
    if (!Q_strcasecmp(name, "null"))    //zhlt compat
        return true;
    return false;
}

static bool
IsNoExpandName(const char *name)
{
    if (!Q_strcasecmp(name, "bevel"))    //zhlt compat
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
    /* Set the texture flags */
    texinfo->flags = flags;
    texinfo->outputnum = -1;

    // NaN's will break mtexinfo_lookup, since they're being used as a std::map key and don't compare properly with <.
    // They should have been stripped out already in ValidateTextureProjection.
    for (int i=0;i<2;i++) {
        for (int j=0;j<4;j++) {
            Q_assert(!std::isnan(texinfo->vecs[i][j]));
        }
    }
    
    // check for an exact match in the reverse lookup
    const auto it = map.mtexinfo_lookup.find(*texinfo);
    if (it != map.mtexinfo_lookup.end()) {
        return it->second;
    }
    
    /* Allocate a new texinfo at the end of the array */
    const int num_texinfo = static_cast<int>(map.mtexinfos.size());
    map.mtexinfos.push_back(*texinfo);
    map.mtexinfo_lookup[*texinfo] = num_texinfo;
    
    // catch broken < implementations in mtexinfo_t
    assert(map.mtexinfo_lookup.find(*texinfo) != map.mtexinfo_lookup.end());
    
    return num_texinfo;
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
    const int shadow = atoi(ValueForKey(entity, "_shadow"));
    if (IsSkipName(texname))
        flags |= TEX_SKIP;
    if (IsHintName(texname))
        flags |= TEX_HINT;
    if (IsSpecialName(texname))
        flags |= TEX_SPECIAL;
    if (IsNoExpandName(texname))
        flags |= TEX_NOEXPAND;
    if (atoi(ValueForKey(entity, "_dirt")) == -1)
        flags |= TEX_NODIRT;
    if (atoi(ValueForKey(entity, "_bounce")) == -1)
        flags |= TEX_NOBOUNCE;
    if (atoi(ValueForKey(entity, "_minlight")) == -1)
        flags |= TEX_NOMINLIGHT;
    if (atoi(ValueForKey(entity, "_lightignore")) == 1)
        flags |= TEX_LIGHTIGNORE;

    // "_minlight_exclude", "_minlight_exclude2", "_minlight_exclude3"... 
    for (int i = 0; i <= 9; i++) {
        std::string key = "_minlight_exclude";
        if (i > 0) {
            key += std::to_string(i);
        }

        const char* excludeTex = ValueForKey(entity, key.c_str());
        if (strlen(excludeTex) > 0 && !Q_strcasecmp(texname, excludeTex)) {
            flags |= TEX_NOMINLIGHT;
        }
    }
   
    if (shadow == -1)
        flags |= TEX_NOSHADOW;
    if (!Q_strcasecmp("func_detail_illusionary", ValueForKey(entity, "classname"))) {
        /* Mark these entities as TEX_NOSHADOW unless the mapper set "_shadow" "1" */
        if (shadow != 1) {
            flags |= TEX_NOSHADOW;
        }
    }
    
    // handle "_phong" and "_phong_angle" and "_phong_angle_concave"
    vec_t phongangle = atof(ValueForKey(entity, "_phong_angle"));
    const int phong = atoi(ValueForKey(entity, "_phong"));
    
    if (phong && (phongangle == 0.0)) {
        phongangle = 89.0; // default _phong_angle
    }
    
    if (phongangle) {
        const uint64_t phongangle_byte = (uint64_t) qmax(0, qmin(255, (int)rint(phongangle)));
        flags |= (phongangle_byte << TEX_PHONG_ANGLE_SHIFT);
    }
    
    const vec_t phong_angle_concave = atof(ValueForKey(entity, "_phong_angle_concave"));
    {
        const uint64_t phong_angle_concave_byte = (uint64_t) qmax(0, qmin(255, (int)rint(phong_angle_concave)));
        flags |= (phong_angle_concave_byte << TEX_PHONG_ANGLE_CONCAVE_SHIFT);
    }
    
    // handle "_minlight"
    const vec_t minlight = atof(ValueForKey(entity, "_minlight"));
    if (minlight > 0) {
        const uint64_t minlight_byte = (uint64_t) qmax(0, qmin(255, (int)rint(minlight)));
        flags |= (minlight_byte << TEX_MINLIGHT_SHIFT);
    }

    // handle "_mincolor"
    {
        vec3_t mincolor {0.0, 0.0, 0.0};
    
        GetVectorForKey(entity, "_mincolor", mincolor);
        if (VectorCompare(vec3_origin, mincolor, EQUAL_EPSILON)) {
            GetVectorForKey(entity, "_minlight_color", mincolor);
        }
    
        normalize_color_format(mincolor);
        if (!VectorCompare(vec3_origin, mincolor, EQUAL_EPSILON)) {
            const uint64_t r_byte = qmax(0, qmin(255, (int)rint(mincolor[0])));
            const uint64_t g_byte = qmax(0, qmin(255, (int)rint(mincolor[1])));
            const uint64_t b_byte = qmax(0, qmin(255, (int)rint(mincolor[2])));
            
            flags |= (r_byte << TEX_MINLIGHT_COLOR_R_SHIFT);
            flags |= (g_byte << TEX_MINLIGHT_COLOR_G_SHIFT);
            flags |= (b_byte << TEX_MINLIGHT_COLOR_B_SHIFT);
        }
    }

    // handle "_light_alpha"
    const vec_t lightalpha = atof(ValueForKey(entity, "_light_alpha"));
    if (lightalpha != 0.0) {
        const uint64_t lightalpha_u7 = (uint64_t) qmax(0, qmin(127, (int)rint(lightalpha * 127.0)));
        Q_assert(lightalpha_u7 < 128u);
        flags |= (lightalpha_u7 << TEX_LIGHT_ALPHA_SHIFT);
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
TextureAxisFromPlane(const qbsp_plane_t *plane, vec3_t xv, vec3_t yv, vec3_t snapped_normal)
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

struct extended_tx_info_t {
    bool quark_tx1;
    bool quark_tx2;
    
    int contents;
    int flags;
    int value;
    
    extended_tx_info_t() :
    quark_tx1(false),
    quark_tx2(false),
    contents(0),
    flags(0),
    value(0) {}
};

static extended_tx_info_t
ParseExtendedTX(parser_t *parser)
{
    extended_tx_info_t result;

    if (ParseToken(parser, PARSE_COMMENT | PARSE_OPTIONAL)) {
        if (!strncmp(parser->token, "//TX", 4)) {
            if (parser->token[4] == '1')
                result.quark_tx1 = true;
            else if (parser->token[4] == '2')
                result.quark_tx2 = true;
        }
    } else {
        // Parse extra Quake 2 surface info
        if (ParseToken(parser, PARSE_OPTIONAL)) {
            result.contents = atoi(parser->token);
        }
        if (ParseToken(parser, PARSE_OPTIONAL)) {
            result.flags = atoi(parser->token);
        }
        if (ParseToken(parser, PARSE_OPTIONAL)) {
            result.value = atoi(parser->token);
        }
    }

    return result;
}

static qmat4x4f texVecsTo4x4Matrix(const qbsp_plane_t &faceplane, const float in_vecs[2][4])
{
    //           [s]
    // T * vec = [t]
    //           [distOffPlane]
    //           [?]
    
    qmat4x4f T {in_vecs[0][0], in_vecs[1][0], static_cast<float>(faceplane.normal[0]), 0, // col 0
                  in_vecs[0][1], in_vecs[1][1], static_cast<float>(faceplane.normal[1]), 0, // col 1
                  in_vecs[0][2], in_vecs[1][2], static_cast<float>(faceplane.normal[2]), 0, // col 2
                  in_vecs[0][3], in_vecs[1][3], static_cast<float>(-faceplane.dist), 1 // col 3
    };
    return T;
}

static qmat2x2f scale2x2(float xscale, float yscale)
{
    qmat2x2f M {
        xscale, 0,  // col 0
        0, yscale }; // col1
    return M;
}

static qmat2x2f rotation2x2_deg(float degrees)
{
    float r = degrees * (Q_PI / 180.0);
    float cosr = cos(r);
    float sinr = sin(r);
    
    // [ cosTh -sinTh ]
    // [ sinTh cosTh  ]
    
    qmat2x2f M {
        cosr, sinr, // col 0
        -sinr, cosr }; // col1
    
    return M;
}

static float extractRotation(qmat2x2f m) {
    qvec2f point = m * qvec2f(1, 0); // choice of this matters if there's shearing
    float rotation = atan2(point[1], point[0]) * 180.0 / Q_PI;
    return rotation;
}

static qvec2f evalTexDefAtPoint(const texdef_quake_ed_t &texdef, const qbsp_plane_t *faceplane, const qvec3f point)
{
    float temp[2][4];
    SetTexinfo_QuakeEd_New(faceplane, texdef.shift, texdef.rotate, texdef.scale, temp);
    
    const qmat4x4f worldToTexSpace_res = texVecsTo4x4Matrix(*faceplane, temp);
    const qvec2f uv = qvec2f(worldToTexSpace_res * qvec4f(point[0], point[1], point[2], 1.0f));
    return uv;
}

#if 0
static float
TexDefRMSE(const texdef_quake_ed_t &texdef, const qbsp_plane_t *faceplane, const qmat4x4f referenceXform, const vec3_t facepoints[3])
{
    float avgSquaredDist = 0;
    for (int i=0; i<3; i++) {
        qvec3f worldPoint = vec3_t_to_glm(facepoints[i]);
        qvec2f observed = evalTexDefAtPoint(texdef, faceplane, worldPoint);
        qvec2f expected = qvec2f(referenceXform * qvec4f(worldPoint, 1.0f));
        
        qvec2f distVec = observed - expected;
        float dist2 = qv::dot(distVec, distVec);
        avgSquaredDist += dist2;
    }
    avgSquaredDist /= 3.0;
    return sqrt(avgSquaredDist);
}
#endif

static texdef_quake_ed_t addShift(const texdef_quake_ed_noshift_t &texdef, const qvec2f shift)
{
    texdef_quake_ed_t res2;
    res2.rotate = texdef.rotate;
    res2.scale[0] = texdef.scale[0];
    res2.scale[1] = texdef.scale[1];
    
    res2.shift[0] = shift[0];
    res2.shift[1] = shift[1];
    return res2;
}

void checkEq(const qvec2f &a, const qvec2f &b, float epsilon)
{
    for (int i=0; i<2; i++) {
        if (fabs(a[i] - b[i]) > epsilon) {
            printf("warning, checkEq failed\n");
        }
    }
}

qvec2f normalizeShift(const texture_t *texture, const qvec2f &in)
{
    if (texture == nullptr)
        return in; // can't do anything without knowing the texture size.
    
    int fullWidthOffsets = static_cast<int>(in[0]) / texture->width;
    int fullHeightOffsets = static_cast<int>(in[1]) / texture->height;
    
    qvec2f result(in[0] - static_cast<float>(fullWidthOffsets * texture->width),
                     in[1] - static_cast<float>(fullHeightOffsets * texture->height));
    return result;
}

/// `texture` is optional. If given, the "shift" values can be normalized
static texdef_quake_ed_t
TexDef_BSPToQuakeEd(const qbsp_plane_t &faceplane, const texture_t *texture, const float in_vecs[2][4], const vec3_t facepoints[3])
{
    // First get the un-rotated, un-scaled unit texture vecs (based on the face plane).
    vec3_t snapped_normal;
    vec3_t unrotated_vecs[2];
    TextureAxisFromPlane(&faceplane, unrotated_vecs[0], unrotated_vecs[1], snapped_normal);

    const qmat4x4f worldToTexSpace = texVecsTo4x4Matrix(faceplane, in_vecs);
    
    // Grab the UVs of the 3 reference points
    qvec2f facepoints_uvs[3];
    for (int i=0; i<3; i++) {
        facepoints_uvs[i] = qvec2f(worldToTexSpace * qvec4f(facepoints[i][0], facepoints[i][1], facepoints[i][2], 1.0));
    }
    
    // Project the 3 reference points onto the axis plane. They are now 2d points.
    qvec2f facepoints_projected[3];
    for (int i=0; i<3; i++) {
        facepoints_projected[i] = projectToAxisPlane(snapped_normal, vec3_t_to_glm(facepoints[i]));
    }
    
    // Now make 2 vectors out of our 3 points (so we are ignoring translation for now)
    const qvec2f p0p1 = facepoints_projected[1] - facepoints_projected[0];
    const qvec2f p0p2 = facepoints_projected[2] - facepoints_projected[0];
    
    const qvec2f p0p1_uv = facepoints_uvs[1] - facepoints_uvs[0];
    const qvec2f p0p2_uv = facepoints_uvs[2] - facepoints_uvs[0];
    
    /*
    Find a 2x2 transformation matrix that maps p0p1 to p0p1_uv, and p0p2 to p0p2_uv
    
        [ a b ] [ p0p1.x ] = [ p0p1_uv.x ]
        [ c d ] [ p0p1.y ]   [ p0p1_uv.y ]
        
        [ a b ] [ p0p2.x ] = [ p0p1_uv.x ]
        [ c d ] [ p0p2.y ]   [ p0p2_uv.y ]
    
    writing as a system of equations:
    
        a * p0p1.x + b * p0p1.y = p0p1_uv.x
        c * p0p1.x + d * p0p1.y = p0p1_uv.y
        a * p0p2.x + b * p0p2.y = p0p2_uv.x
        c * p0p2.x + d * p0p2.y = p0p2_uv.y

    back to a matrix equation, with the unknowns in a column vector:
    
       [ p0p1_uv.x ]   [ p0p1.x p0p1.y 0       0      ] [ a ]
       [ p0p1_uv.y ] = [ 0       0     p0p1.x p0p1.y  ] [ b ]
       [ p0p2_uv.x ]   [ p0p2.x p0p2.y 0       0      ] [ c ]
       [ p0p2_uv.y ]   [ 0       0     p0p2.x p0p2.y  ] [ d ]
     
     */
       
    const qmat4x4f M {p0p1[0], 0,      p0p2[0], 0,      // col 0
                      p0p1[1], 0,      p0p2[1], 0,      // col 1
                      0,      p0p1[0], 0,      p0p2[0], // col 2
                      0,      p0p1[1], 0,      p0p2[1]  // col 3
    };
    
    const qmat4x4f Minv = qv::inverse(M);
    const qvec4f abcd = Minv * qvec4f(p0p1_uv[0],
                                            p0p1_uv[1],
                                            p0p2_uv[0],
                                            p0p2_uv[1]);
    
    const qmat2x2f texPlaneToUV{abcd[0], abcd[2], // col 0
                                abcd[1], abcd[3]};// col 1
    
    {
        // self check
//        qvec2f uv01_test = texPlaneToUV * p0p1;
//        qvec2f uv02_test = texPlaneToUV * p0p2;
        
        // these fail if one of the texture axes is 0 length.
//        checkEq(uv01_test, p0p1_uv, 0.01);
//        checkEq(uv02_test, p0p2_uv, 0.01);
    }
    
    const texdef_quake_ed_noshift_t res = Reverse_QuakeEd(texPlaneToUV, &faceplane, false);
    
    // figure out shift based on facepoints[0]
    qvec3f testpoint = vec3_t_to_glm(facepoints[0]);
    qvec2f uv0_actual = evalTexDefAtPoint(addShift(res, qvec2f(0,0)), &faceplane, testpoint);
    qvec2f uv0_desired = qvec2f(worldToTexSpace * qvec4f(testpoint[0], testpoint[1], testpoint[2], 1.0f));
    qvec2f shift = uv0_desired - uv0_actual;
    
    // sometime we have very large shift values, normalize them to be smaller
    shift = normalizeShift(texture, shift);
    
    const texdef_quake_ed_t res2 = addShift(res, shift);
    return res2;
}

float NormalizeDegrees(float degs)
{
    while (degs < 0)
        degs += 360;
    
    while (degs > 360)
        degs -= 360;
    
    if (fabs(degs - 360.0) < 0.001)
        degs = 0;
    
    return degs;
}

bool EqualDegrees(float a, float b) {
    return fabs(NormalizeDegrees(a) - NormalizeDegrees(b)) < 0.001;
}

static std::pair<int,int> getSTAxes(const vec3_t snapped_normal)
{
    if (snapped_normal[0]) {
        return std::make_pair(1,2);
    } else if (snapped_normal[1]) {
        return std::make_pair(0,2);
    } else {
        return std::make_pair(0,1);
    }
}

static qvec2f projectToAxisPlane(const vec3_t snapped_normal, qvec3f point)
{
    const std::pair<int,int> axes = getSTAxes(snapped_normal);
    
    const qvec2f proj(point[axes.first],
                         point[axes.second]);
    return proj;
}

float clockwiseDegreesBetween(qvec2f start, qvec2f end)
{
    start = qv::normalize(start);
    end = qv::normalize(end);
    
    const float cosAngle = qmax(-1.0f, qmin(1.0f, qv::dot(start, end)));
    const float unsigned_degrees = acos(cosAngle) * (360.0 / (2.0 * Q_PI));
    
    if (unsigned_degrees < ANGLEEPSILON)
        return 0;
    
    // get a normal for the rotation plane using the right-hand rule
    // if this is pointing up (qvec3f(0,0,1)), it's counterclockwise rotation.
    // if this is pointing down (qvec3f(0,0,-1)), it's clockwise rotation.
    qvec3f rotationNormal = qv::normalize(qv::cross(qvec3f(start[0], start[1], 0.0f), qvec3f(end[0], end[1], 0.0f)));
    
    const float normalsCosAngle = qv::dot(rotationNormal, qvec3f(0,0,1));
    if (normalsCosAngle >= 0) {
        // counterclockwise rotation
        return -unsigned_degrees;
    }
    // clockwise rotation
    return unsigned_degrees;
}

static texdef_quake_ed_noshift_t
Reverse_QuakeEd(qmat2x2f M, const qbsp_plane_t *plane, bool preserveX)
{
    // Check for shear, because we might tweak M to remove it
    {
        qvec2f Xvec = qvec2f(M[0][0], M[1][0]);
        qvec2f Yvec = qvec2f(M[0][1], M[1][1]);
        double cosAngle = qv::dot(qv::normalize(Xvec), qv::normalize(Yvec));
        
        //const double oldXscale = sqrt(pow(M[0][0], 2.0) + pow(M[1][0], 2.0));
        //const double oldYscale = sqrt(pow(M[0][1], 2.0) + pow(M[1][1], 2.0));
        
        if (fabs(cosAngle) > 0.001) {
            // Detected shear
            
            if (preserveX) {
                const float degreesToY = clockwiseDegreesBetween(Xvec, Yvec);
                const bool CW = (degreesToY > 0);
                
                // turn 90 degrees from Xvec
                const qvec2f newYdir = qv::normalize(
                     qvec2f(qv::cross(qvec3f(0, 0, CW ? -1.0f : 1.0f), qvec3f(Xvec[0], Xvec[1], 0.0))));
                
                // scalar projection of the old Yvec onto newYDir to get the new Yscale
                const float newYscale = qv::dot(Yvec, newYdir);
                Yvec = newYdir * static_cast<float>(newYscale);
            } else {
                // Preserve Y.
                
                const float degreesToX = clockwiseDegreesBetween(Yvec, Xvec);
                const bool CW = (degreesToX > 0);
                
                // turn 90 degrees from Yvec
                const qvec2f newXdir = qv::normalize(
                    qvec2f(qv::cross(qvec3f(0, 0, CW ? -1.0f : 1.0f), qvec3f(Yvec[0], Yvec[1], 0.0))));
                
                // scalar projection of the old Xvec onto newXDir to get the new Xscale
                const float newXscale = qv::dot(Xvec, newXdir);
                Xvec = newXdir * static_cast<float>(newXscale);
            }
            
            // recheck
            cosAngle = qv::dot(qv::normalize(Xvec), qv::normalize(Yvec));
            if (fabs(cosAngle) > 0.001) {
                Error("SHEAR correction failed\n");
            }
            
            // update M
            M[0][0] = Xvec[0];
            M[1][0] = Xvec[1];
            
            M[0][1] = Yvec[0];
            M[1][1] = Yvec[1];
        }
    }
    
    // extract abs(scale)
    const double absXscale = sqrt(pow(M[0][0], 2.0) + pow(M[1][0], 2.0));
    const double absYscale = sqrt(pow(M[0][1], 2.0) + pow(M[1][1], 2.0));
    const qmat2x2f applyAbsScaleM{static_cast<float>(absXscale), // col0
                                  0,
                                  0, // col1
                                  static_cast<float>(absYscale)};
    
    vec3_t vecs[2];
    vec3_t snapped_normal;
    TextureAxisFromPlane(plane, vecs[0], vecs[1], snapped_normal);

    const qvec2f sAxis = projectToAxisPlane(snapped_normal, vec3_t_to_glm(vecs[0]));
    const qvec2f tAxis = projectToAxisPlane(snapped_normal, vec3_t_to_glm(vecs[1]));

    // This is an identity matrix possibly with negative signs.
    const qmat2x2f axisFlipsM{sAxis[0], tAxis[0],  // col0
                              sAxis[1], tAxis[1]}; // col1

    // N.B. this is how M is built in SetTexinfo_QuakeEd_New and guides how we
    // strip off components of it later in this function.
    //
    //    qmat2x2f M = scaleM * rotateM * axisFlipsM;
    
    // strip off the magnitude component of the scale, and `axisFlipsM`.
    const qmat2x2f flipRotate = qv::inverse(applyAbsScaleM) * M * qv::inverse(axisFlipsM);
    
    // We don't know the signs on the scales, which will mess up figuring out the rotation, so try all 4 combinations
    for (float xScaleSgn : std::vector<float>{ -1.0, 1.0 }) {
        for (float yScaleSgn : std::vector<float>{ -1.0, 1.0 }) {
            
            // "apply" - matrix constructed to apply a guessed value
            // "guess" - this matrix might not be what we think
            
            const qmat2x2f applyGuessedFlipM{
                                     xScaleSgn, // col0
                                     0,
                                     0, // col1
                                     yScaleSgn};
            
            const qmat2x2f rotateMGuess = qv::inverse(applyGuessedFlipM) * flipRotate;
            const float angleGuess = extractRotation(rotateMGuess);
            
//            const qmat2x2f Mident = rotateMGuess * rotation2x2_deg(-angleGuess);

            const qmat2x2f applyAngleGuessM = rotation2x2_deg(angleGuess);
            const qmat2x2f Mguess = applyGuessedFlipM * applyAbsScaleM * applyAngleGuessM * axisFlipsM;
            
            if (fabs(M[0][0] - Mguess[0][0]) < 0.001
                && fabs(M[0][1] - Mguess[0][1]) < 0.001
                && fabs(M[1][0] - Mguess[1][0]) < 0.001
                && fabs(M[1][1] - Mguess[1][1]) < 0.001) {
                
                texdef_quake_ed_noshift_t reversed;
                reversed.rotate = angleGuess;
                reversed.scale[0] = xScaleSgn / absXscale;
                reversed.scale[1] = yScaleSgn / absYscale;
                return reversed;
            }
        }
    }

    // TODO: detect when we expect this to fail, i.e.  invalid texture axes (0-length),
    // and throw an error if it fails unexpectedly.
    
    //printf("Warning, Reverse_QuakeEd failed\n");
    
    texdef_quake_ed_noshift_t fail;
    return fail;
}

static void
SetTexinfo_QuakeEd_New(const qbsp_plane_t *plane, const vec_t shift[2], vec_t rotate, const vec_t scale[2], float out_vecs[2][4])
{
    vec_t sanitized_scale[2];
    for (int i=0; i<2; i++) {
        sanitized_scale[i] = (scale[i] != 0.0) ? scale[i] : 1.0;
    }
    
    vec3_t vecs[2];
    vec3_t snapped_normal;
    TextureAxisFromPlane(plane, vecs[0], vecs[1], snapped_normal);

    qvec2f sAxis = projectToAxisPlane(snapped_normal, vec3_t_to_glm(vecs[0]));
    qvec2f tAxis = projectToAxisPlane(snapped_normal, vec3_t_to_glm(vecs[1]));

    // This is an identity matrix possibly with negative signs.
    qmat2x2f axisFlipsM{sAxis[0], tAxis[0],  // col0
                        sAxis[1], tAxis[1]}; // col1
    
    qmat2x2f rotateM = rotation2x2_deg(rotate);
    qmat2x2f scaleM = scale2x2(1.0/sanitized_scale[0], 1.0/sanitized_scale[1]);
    
    qmat2x2f M = scaleM * rotateM * axisFlipsM;
    
    if (false) {
        // Self-test for Reverse_QuakeEd
        texdef_quake_ed_noshift_t reversed = Reverse_QuakeEd(M, plane, false);
        
        // normalize
        if (!EqualDegrees(reversed.rotate, rotate)) {
            reversed.rotate += 180;
            reversed.scale[0] *= -1;
            reversed.scale[1] *= -1;
        }
 
        if (!EqualDegrees(reversed.rotate, rotate)) {
            Error("wrong rotat got %f expected %f\n",
                reversed.rotate, rotate);
        }
        
        if (fabs(reversed.scale[0] - sanitized_scale[0]) > 0.001
            || fabs(reversed.scale[1] - sanitized_scale[1]) > 0.001) {
            Error("wrong scale, got %f %f exp %f %f\n",
                reversed.scale[0], reversed.scale[1],
                sanitized_scale[0], sanitized_scale[1]);
        }
    }
    
    // copy M into the output vectors
    
    for (int i=0; i<2; i++) {
        for (int j=0; j<4; j++) {
            out_vecs[i][j] = 0.0;
        }
    }

    const std::pair<int,int> axes = getSTAxes(snapped_normal);
    
    //                        M[col][row]
    // S
    out_vecs[0][axes.first] = M[0][0];
    out_vecs[0][axes.second] = M[1][0];
    out_vecs[0][3] = shift[0];
    
    // T
    out_vecs[1][axes.first] = M[0][1];
    out_vecs[1][axes.second] = M[1][1];
    out_vecs[1][3] = shift[1];
}

static void
SetTexinfo_QuakeEd(const qbsp_plane_t *plane, const vec3_t planepts[3], const vec_t shift[2], vec_t rotate,
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
    
    if (false) {
        // Self-test of SetTexinfo_QuakeEd_New
        float check[2][4];
        SetTexinfo_QuakeEd_New(plane, shift, rotate, scale, check);
        for (int i=0; i<2; i++) {
            for (int j=0; j<4; j++) {
                if (fabs(check[i][j] - out->vecs[i][j]) > 0.001) {
                    SetTexinfo_QuakeEd_New(plane, shift, rotate, scale, check);
                    Error("fail");
                }
            }
        }
    }

    if (false) {
        // Self-test of TexDef_BSPToQuakeEd
        texdef_quake_ed_t reversed = TexDef_BSPToQuakeEd(*plane, nullptr, out->vecs, planepts);
    
        if (!EqualDegrees(reversed.rotate, rotate)) {
            reversed.rotate += 180;
            reversed.scale[0] *= -1;
            reversed.scale[1] *= -1;
        }
 
        if (!EqualDegrees(reversed.rotate, rotate)) {
            printf("wrong rotat got %f expected %f\n",
                reversed.rotate, rotate);
        }
        
        if (fabs(reversed.scale[0] - scale[0]) > 0.001
            || fabs(reversed.scale[1] - scale[1]) > 0.001) {
            printf("wrong scale, got %f %f exp %f %f\n",
                reversed.scale[0], reversed.scale[1],
                scale[0], scale[1]);
        }
        
        if (fabs(reversed.shift[0] - shift[0]) > 0.1
            || fabs(reversed.shift[1] - shift[1]) > 0.1) {
            printf("wrong shift, got %f %f exp %f %f\n",
                reversed.shift[0], reversed.shift[1],
                shift[0], shift[1]);
        }
    }
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
TexDef_BSPToBrushPrimitives(const qbsp_plane_t plane, const int texSize[2], const float in_vecs[2][4])
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
                vec3_t planepts[3], const qbsp_plane_t *plane)
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
        
        // Read extra Q2 params
        const auto extinfo = ParseExtendedTX(parser);
        mapface.contents = extinfo.contents;
        mapface.flags = extinfo.flags;
        mapface.value = extinfo.value;
    } else if (brush->format == brushformat_t::NORMAL) {
        ParseToken(parser, PARSE_SAMELINE);
        tx->miptex = FindMiptex(parser->token);
        mapface.texname = std::string(parser->token);
        
        ParseToken(parser, PARSE_SAMELINE);
        if (!strcmp(parser->token, "[")) {
            parser->unget = true;
            ParseValve220TX(parser, axis, shift, &rotate, scale);
            tx_type = TX_VALVE_220;
            
            // Read extra Q2 params
            const auto extinfo = ParseExtendedTX(parser);
            mapface.contents = extinfo.contents;
            mapface.flags = extinfo.flags;
            mapface.value = extinfo.value;
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
            
            // Read extra Q2 params and/or QuArK subtype
            const auto extinfo = ParseExtendedTX(parser);
            if (extinfo.quark_tx1) {
                tx_type = TX_QUARK_TYPE1;
            } else if (extinfo.quark_tx2) {
                tx_type = TX_QUARK_TYPE2;
            } else {
                tx_type = TX_QUAKED;
            }
            mapface.contents = extinfo.contents;
            mapface.flags = extinfo.flags;
            mapface.value = extinfo.value;
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
        SetTexinfo_QuakeEd(plane, planepts, shift, rotate, scale, tx);
        break;
    }
}

bool mapface_t::set_planepts(const vec3_t *pts)
{
    for (int i=0; i<3; i++)
        VectorCopy(pts[i], this->planepts[i]);
    
    return SetPlanePts(pts, this->plane.normal, &this->plane.dist);
}

std::array<qvec4f, 2> mapface_t::get_texvecs(void) const
{
    const mtexinfo_t &texinfo = map.mtexinfos.at(this->texinfo);
    
    qvec4f res[2];
    for (int i=0; i<2; i++) {
        for (int j=0; j<4; j++) {
            res[i][j] = texinfo.vecs[i][j];
        }
    }
    
    return std::array<qvec4f, 2>{{ res[0], res[1] }};
}

void mapface_t::set_texvecs(const std::array<qvec4f, 2> &vecs)
{
    // start with a copy of the current texinfo structure
    mtexinfo_t texInfoNew = map.mtexinfos.at(this->texinfo);
    
    // update vecs
    for (int i=0; i<2; i++) {
        for (int j=0; j<4; j++) {
            texInfoNew.vecs[i][j] = vecs.at(i)[j];
        }
    }
    
    this->texinfo = FindTexinfo( &texInfoNew, texInfoNew.flags );
}

bool
IsValidTextureProjection(const qvec3f &faceNormal, const qvec3f &s_vec, const qvec3f &t_vec)
{
    // TODO: This doesn't match how light does it (TexSpaceToWorld)
    
    const qvec3f tex_normal = qv::normalize(qv::cross(s_vec, t_vec));
    
    for (int i=0;i<3;i++)
    	if (std::isnan(tex_normal[i]))
            return false;
    
    const float cosangle = qv::dot(tex_normal, faceNormal);
    if (std::isnan(cosangle))
        return false;
    if (fabs(cosangle) < ZERO_EPSILON)
        return false;
    
    return true;
}

static bool
IsValidTextureProjection(const mapface_t &mapface, const mtexinfo_t *tx)
{
    const qvec3f faceNormal = vec3_t_to_glm(mapface.plane.normal);
    const qvec3f s_vec = qvec3f(tx->vecs[0][0], tx->vecs[0][1], tx->vecs[0][2]);
    const qvec3f t_vec = qvec3f(tx->vecs[1][0], tx->vecs[1][1], tx->vecs[1][2]);
    const bool valid = IsValidTextureProjection(faceNormal, s_vec, t_vec);
    
    return valid;
}

static void
ValidateTextureProjection(mapface_t &mapface, mtexinfo_t *tx)
{
    if (!IsValidTextureProjection(mapface, tx)) {
        logprint("WARNING: repairing invalid texture projection on line %d (\"%s\" near %d %d %d)\n",
                 mapface.linenum,
                 mapface.texname.c_str(),
                 (int)mapface.planepts[0][0],
                 (int)mapface.planepts[0][1],
                 (int)mapface.planepts[0][2]);
      
        // Reset texturing to sensible defaults
        const double shift[2] = {0,0};
        const double rotate = 0;
        const double scale[2] = {1,1};
        SetTexinfo_QuakeEd(&mapface.plane, mapface.planepts, shift, rotate, scale, tx);
        
        Q_assert(IsValidTextureProjection(mapface, tx));
    }
}

static std::unique_ptr<mapface_t>
ParseBrushFace(parser_t *parser, const mapbrush_t *brush, const mapentity_t *entity)
{
    vec3_t planepts[3];
    bool normal_ok;
    const qbsp_plane_t *plane;
    mtexinfo_t tx;
    int i, j;
    std::unique_ptr<mapface_t> face { new mapface_t };

    face->linenum = parser->linenum;
    ParsePlaneDef(parser, planepts);

    normal_ok = face->set_planepts(planepts);
    plane = &face->plane;

    ParseTextureDef(parser, *face, brush, &tx, face->planepts, plane);

    if (!normal_ok) {
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
    
    ValidateTextureProjection(*face, &tx);

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
        bool discardFace = false;
        for (int i = 0; i<brush.numfaces; i++) {
            const mapface_t &check = brush.face(i);
            if (PlaneEqual(&check.plane, &face->plane)) {
                Message(msgWarning, warnBrushDuplicatePlane, parser->linenum);
                discardFace = true;
                continue;
            }
            if (PlaneInvEqual(&check.plane, &face->plane)) {
                /* FIXME - this is actually an invalid brush */
                Message(msgWarning, warnBrushDuplicatePlane, parser->linenum);
                continue;
            }
        }
        if (discardFace)
            continue;

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

bool
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

static void ScaleMapFace(mapface_t *face, const vec3_t scale)
{
    const qmat3x3d scaleM {
        // column-major...
        static_cast<double>(scale[0]), 0.0, 0.0,
        0.0, static_cast<double>(scale[1]), 0.0,
        0.0, 0.0, static_cast<double>(scale[2])
    };
    
    vec3_t new_planepts[3];
    for (int i=0; i<3; i++) {
        qvec3d oldpt = qvec3d_from_vec3(face->planepts[i]);
        qvec3d newpt = scaleM * oldpt;
        
        glm_to_vec3_t(newpt, new_planepts[i]);
    }
    
    face->set_planepts(new_planepts);
    
    // update texinfo
    
    const qmat3x3d inversescaleM {
        // column-major...
        static_cast<double>(1/scale[0]), 0.0, 0.0,
        0.0, static_cast<double>(1/scale[1]), 0.0,
        0.0, 0.0, static_cast<double>(1/scale[2])
    };
    
    const std::array<qvec4f, 2> texvecs = face->get_texvecs();
    std::array<qvec4f, 2> newtexvecs;
    
    for (int i=0; i<2; i++) {
        const qvec4f in = texvecs.at(i);
        const qvec3f in_first3(in);
        
        const qvec3f out_first3 = inversescaleM * in_first3;
        const qvec4f out(out_first3[0], out_first3[1], out_first3[2], in[3]);
        newtexvecs.at(i) = out;
    }
    
    face->set_texvecs(newtexvecs);
}

static void RotateMapFace(mapface_t *face, const vec3_t angles)
{
    const double pitch = DEG2RAD(angles[0]);
    const double yaw = DEG2RAD(angles[1]);
    const double roll = DEG2RAD(angles[2]);
    
    qmat3x3d rotation = RotateAboutZ(yaw) * RotateAboutY(pitch) * RotateAboutX(roll);
    
    vec3_t new_planepts[3];
    for (int i=0; i<3; i++) {
        qvec3d oldpt = qvec3d_from_vec3(face->planepts[i]);
        qvec3d newpt = rotation * oldpt;
        
        glm_to_vec3_t(newpt, new_planepts[i]);
    }
    
    face->set_planepts(new_planepts);
    
    // update texinfo
    
    const std::array<qvec4f, 2> texvecs = face->get_texvecs();
    std::array<qvec4f, 2> newtexvecs;
    
    for (int i=0; i<2; i++) {
        const qvec4f in = texvecs.at(i);
        const qvec3f in_first3(in);
        
        const qvec3f out_first3 = rotation * in_first3;
        const qvec4f out(out_first3[0], out_first3[1], out_first3[2], in[3]);
        newtexvecs.at(i) = out;
    }
    
    face->set_texvecs(newtexvecs);
}

static void TranslateMapFace(mapface_t *face, const vec3_t offset)
{
    vec3_t new_planepts[3];
    for (int i=0; i<3; i++) {
        VectorAdd(face->planepts[i], offset, new_planepts[i]);
    }
    
    face->set_planepts(new_planepts);
    
    // update texinfo
    
    const std::array<qvec4f, 2> texvecs = face->get_texvecs();
    std::array<qvec4f, 2> newtexvecs;
    
    for (int i=0; i<2; i++) {
        qvec4f out = texvecs.at(i);
        out[3] += qv::dot(qvec3f(out), vec3_t_to_glm(offset) * -1.0f);
        newtexvecs.at(i) = out;
    }
    
    face->set_texvecs(newtexvecs);
}

void
ProcessExternalMapEntity(mapentity_t *entity)
{
    Q_assert(!options.fOnlyents);
    
    const char *classname = ValueForKey(entity, "classname");
    if (Q_strcasecmp(classname, "misc_external_map"))
        return;
    
    const char *file = ValueForKey(entity, "_external_map");
    const char *new_classname = ValueForKey(entity, "_external_map_classname");
    
    Q_assert(file && file[0]);
    Q_assert(new_classname && new_classname[0]);
    
    Q_assert(0 == entity->nummapbrushes); // misc_external_map must be a point entity
    
    const mapentity_t external_worldspawn = LoadExternalMap(file);
    
    // copy the brushes into the target
    entity->firstmapbrush = external_worldspawn.firstmapbrush;
    entity->nummapbrushes = external_worldspawn.nummapbrushes;
    
    vec3_t origin;
    GetVectorForKey(entity, "origin", origin);
    
    vec3_t angles;
    GetVectorForKey(entity, "_external_map_angles", angles);
    if (VectorCompare(angles, vec3_origin, EQUAL_EPSILON)) {
        angles[1] = atof(ValueForKey(entity, "_external_map_angle"));
    }
    
    vec3_t scale;
    int ncomps = GetVectorForKey(entity, "_external_map_scale", scale);
    if (ncomps < 3) {
        if (scale[0] == 0.0) {
            VectorSet(scale, 1, 1, 1);
        } else {
        	scale[1] = scale[0];
        	scale[2] = scale[0];
        }
    }
    
    for (int i=0; i<entity->nummapbrushes; i++) {
        mapbrush_t *brush = const_cast<mapbrush_t *>(&entity->mapbrush(i));
        
        for (int j=0; j<brush->numfaces; j++) {
            mapface_t *face = const_cast<mapface_t *>(&brush->face(j));
            
            ScaleMapFace(face, scale);
            RotateMapFace(face, angles);
            TranslateMapFace(face, origin);
        }
    }
    
    SetKeyValue(entity, "classname", new_classname);
    // FIXME: Should really just delete the origin key?
    SetKeyValue(entity, "origin", "0 0 0");
}

/*
 * Special world entities are entities which have their brushes added to the
 * world before being removed from the map. Currently func_detail and
 * func_group.
 */
bool
IsWorldBrushEntity(const mapentity_t *entity)
{
    const char *classname = ValueForKey(entity, "classname");

    /* 
     These entities should have their classname remapped to the value of
     _external_map_classname before ever calling IsWorldBrushEntity
     */
    Q_assert(Q_strcasecmp(classname, "misc_external_map"));
    
    if (!Q_strcasecmp(classname, "func_detail"))
        return true;
    if (!Q_strcasecmp(classname, "func_group"))
        return true;
    if (!Q_strcasecmp(classname, "func_detail_illusionary"))
        return true;
    if (!Q_strcasecmp(classname, "func_detail_wall"))
        return true;
    if (!Q_strcasecmp(classname, "func_detail_fence"))
        return true;
    if (!Q_strcasecmp(classname, "func_illusionary_visblocker"))
        return true;
    return false;
}

/**
 * Loads an external .map file.
 *
 * The loaded brushes/planes/etc. will be stored in the global mapdata_t.
 */
mapentity_t LoadExternalMap(const char *filename)
{
    parser_t parser;
    char *buf;
    int length;
    mapentity_t dest {};
    
    length = LoadFile(filename, &buf, true);
    ParserInit(&parser, buf);
    
    // parse the worldspawn
    if (!ParseEntity(&parser, &dest)) {
        Error("LoadExternalMap: '%s': Couldn't parse worldspawn entity\n", filename);
    }
    const char *classname = ValueForKey(&dest, "classname");
    if (Q_strcasecmp("worldspawn", classname)) {
        Error("LoadExternalMap: '%s': Expected first entity to be worldspawn, got: '%s'\n", filename, classname);
    }
    
    // parse any subsequent entities, move any brushes to worldspawn
    mapentity_t dummy {};
    while (ParseEntity(&parser, &dummy)) {
        // this is kind of fragile, but move the brushes to the worldspawn.
        if (dummy.nummapbrushes) {
            // special case for when the external map's worldspawn has no brushes
            if (!dest.firstmapbrush) {
                dest.firstmapbrush = dummy.firstmapbrush;
            }
            dest.nummapbrushes += dummy.nummapbrushes;
        }

        // clear for the next loop iteration
        dummy = mapentity_t();
    }
    
    if (!dest.nummapbrushes) {
        Error("Expected at least one brush for external map %s\n", filename);
    }
    
    Message(msgStat, "LoadExternalMap: '%s': Loaded %d mapbrushes.\n", filename, dest.nummapbrushes);
    
    FreeMem(buf, OTHER, length + 1);
    
    return dest;
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
    
    if (options.fTestExpand) {
        TestExpandBrushes (pWorldEnt());
    }
}

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

static void fprintDoubleAndSpc(FILE *f, double v)
{
    int rounded = rint(v);
    if (static_cast<double>(rounded) == v) {
        fprintf(f, "%d ", rounded);
    } else if (std::isfinite(v)) {
        fprintf(f, "%0.17g ", v);
    } else {
        printf("WARNING: suppressing nan or infinity\n");
        fprintf(f, "0 ");
    }
}

static void
ConvertMapFace(FILE *f, const mapface_t &mapface, const conversion_t format)
{
    EnsureTexturesLoaded();
    const texture_t *texture = WADList_GetTexture(mapface.texname.c_str());
    
    const mtexinfo_t &texinfo = map.mtexinfos.at(mapface.texinfo);
    
    // Write plane points
    for (int i=0; i<3; i++) {
        fprintf(f, " ( ");
        for (int j=0; j<3; j++) {
            fprintDoubleAndSpc(f, mapface.planepts[i][j]);
        }
        fprintf(f, ") ");
    }
    
    switch(format) {
        case conversion_t::quake:
        case conversion_t::quake2: {
            const texdef_quake_ed_t quakeed = TexDef_BSPToQuakeEd(mapface.plane, texture, texinfo.vecs, mapface.planepts);
            
            fprintf(f, "%s ", mapface.texname.c_str());
            fprintDoubleAndSpc(f, quakeed.shift[0]);
            fprintDoubleAndSpc(f, quakeed.shift[1]);
            fprintDoubleAndSpc(f, quakeed.rotate);
            fprintDoubleAndSpc(f, quakeed.scale[0]);
            fprintDoubleAndSpc(f, quakeed.scale[1]);
            
            if (format == conversion_t::quake2) {
                fprintf(f, "0 0 0");
            }
            
            break;
        }
        case conversion_t::valve: {
            const texdef_valve_t valve = TexDef_BSPToValve(texinfo.vecs);
            
            fprintf(f, "%s [ ", mapface.texname.c_str());
            fprintDoubleAndSpc(f, valve.axis[0][0]);
            fprintDoubleAndSpc(f, valve.axis[0][1]);
            fprintDoubleAndSpc(f, valve.axis[0][2]);
            fprintDoubleAndSpc(f, valve.shift[0]);
            fprintf(f, "] [ ");
            fprintDoubleAndSpc(f, valve.axis[1][0]);
            fprintDoubleAndSpc(f, valve.axis[1][1]);
            fprintDoubleAndSpc(f, valve.axis[1][2]);
            fprintDoubleAndSpc(f, valve.shift[1]);
            fprintf(f, "] 0 ");
            fprintDoubleAndSpc(f, valve.scale[0]);
            fprintDoubleAndSpc(f, valve.scale[1]);
            break;
        }
        case conversion_t::bp: {
            int texSize[2];
            texSize[0] = texture ? texture->width : 64;
            texSize[1] = texture ? texture->height : 64;
            
            const texdef_brush_primitives_t bp = TexDef_BSPToBrushPrimitives(mapface.plane, texSize, texinfo.vecs);
            fprintf(f, "( ( ");
            fprintDoubleAndSpc(f, bp.texMat[0][0]);
            fprintDoubleAndSpc(f, bp.texMat[0][1]);
            fprintDoubleAndSpc(f, bp.texMat[0][2]);
            fprintf(f, ") ( ");
            fprintDoubleAndSpc(f, bp.texMat[1][0]);
            fprintDoubleAndSpc(f, bp.texMat[1][1]);
            fprintDoubleAndSpc(f, bp.texMat[1][2]);
            
            // N.B.: always print the Q2/Q3 flags
            fprintf(f, ") ) %s 0 0 0", mapface.texname.c_str());
            break;
        }
        default:
            Error("Internal error: unknown texcoord_style_t\n");
    }
    
    fprintf(f, "\n");
}

static void
ConvertMapBrush(FILE *f, const mapbrush_t &mapbrush, const conversion_t format)
{
    fprintf(f, "{\n");
    if (format == conversion_t::bp) {
        fprintf(f, "brushDef\n");
        fprintf(f, "{\n");
    }
    for (int i=0; i<mapbrush.numfaces; i++) {
        ConvertMapFace(f, mapbrush.face(i), format);
    }
    if (format == conversion_t::bp) {
        fprintf(f, "}\n");
    }
    fprintf(f, "}\n");
}

static void
ConvertEntity(FILE *f, const mapentity_t *entity, const conversion_t format)
{
    fprintf(f, "{\n");
    
    // put the epairs in a temporary list to reverse the order, so we can print them in the same order as the .MAP file
    std::list<std::pair<std::string, std::string>> epairs;
    for (const epair_t *epair = entity->epairs; epair; epair = epair->next) {
        epairs.push_front(std::make_pair(std::string(epair->key), std::string(epair->value)));
    }
    for (const auto &epair : epairs) {
        fprintf(f, "\"%s\" \"%s\"\n", epair.first.c_str(), epair.second.c_str());
    }
    
    for (int i=0; i<entity->nummapbrushes; i++) {
        ConvertMapBrush(f, entity->mapbrush(i), format);
    }
    fprintf(f, "}\n");
}

static std::string stripExt(const std::string &filename) {
    char filenameCstr[1024];
    strncpy(filenameCstr, filename.c_str(), sizeof(filenameCstr));
    filenameCstr[1023] = '\0';
    
    StripExtension(filenameCstr);
    return std::string(filenameCstr);
}

void ConvertMapFile(void)
{
    Message(msgProgress, "ConvertMapFile");
    
    std::string filename = stripExt(options.szBSPName);
    
    switch(options.convertMapFormat) {
        case conversion_t::quake:
            filename += "-quake.map";
            break;
        case conversion_t::quake2:
            filename += "-quake2.map";
            break;
        case conversion_t::valve:
            filename += "-valve.map";
            break;
        case conversion_t::bp:
            filename += "-bp.map";
            break;
        default:
            Error("Internal error: unknown conversion_t\n");
    }
    
    FILE *f = fopen(filename.c_str(), "wb");
    if (f == nullptr)
        Error("Couldn't open file\n");
    
    for (const mapentity_t &entity : map.entities) {
        ConvertEntity(f, &entity, options.convertMapFormat);
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

/**
 * returnts number of vector components read
 */
int
GetVectorForKey(const mapentity_t *entity, const char *szKey, vec3_t vec)
{
    const char *value;
    double v1, v2, v3;

    value = ValueForKey(entity, szKey);
    v1 = v2 = v3 = 0;
    // scanf into doubles, then assign, so it is vec_t size independent
    const int numComps = sscanf(value, "%lf %lf %lf", &v1, &v2, &v3);
    vec[0] = v1;
    vec[1] = v2;
    vec[2] = v3;
    
    return numComps;
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

//====================================================================

/*
==================
WriteBspBrushMap

from q3map
==================
*/
void
WriteBspBrushMap(const char *name, const std::vector<const brush_t *> &list)
{
    FILE	*f;
    
    logprint ("writing %s\n", name);
    f = fopen (name, "wb");
    if (!f)
        Error ("Can't write %s\b", name);
    
    fprintf (f, "{\n\"classname\" \"worldspawn\"\n");
    
    for (const brush_t *brush : list)
    {
        fprintf (f, "{\n");
        for (const face_t *face = brush->faces; face; face = face->next)
        {
            // FIXME: Factor out this mess
            qbsp_plane_t plane = map.planes.at(face->planenum);
            if (face->planeside) {
                VectorScale(plane.normal, -1, plane.normal);
                plane.dist = -plane.dist;
            }
            
            winding_t	*w = BaseWindingForPlane(&plane);
            
            fprintf (f,"( %g %g %g ) ", w->points[0][0], w->points[0][1], w->points[0][2]);
            fprintf (f,"( %g %g %g ) ", w->points[1][0], w->points[1][1], w->points[1][2]);
            fprintf (f,"( %g %g %g ) ", w->points[2][0], w->points[2][1], w->points[2][2]);
            
            fprintf (f, "notexture 0 0 0 1 1\n" );
            
            FreeMem(w, WINDING, 1);
        }
        fprintf (f, "}\n");
    }
    fprintf (f, "}\n");
    
    fclose (f);
}

/*
================
TestExpandBrushes

Expands all the brush planes and saves a new map out to
allow visual inspection of the clipping bevels

from q3map
================
*/
static void
TestExpandBrushes(const mapentity_t *src)
{
    std::vector<const brush_t *> hull1brushes;
    
    for (int i = 0; i < src->nummapbrushes; i++) {
        const mapbrush_t *mapbrush = &src->mapbrush(i);
        brush_t *hull1brush = LoadBrush(src, mapbrush, CONTENTS_SOLID, vec3_origin, rotation_t::none, 1);
        
        if (hull1brush != nullptr)
            hull1brushes.push_back(hull1brush);
    }
    
    WriteBspBrushMap("expanded.map", hull1brushes);
}
