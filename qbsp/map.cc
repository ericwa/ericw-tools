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

#include <cassert>
#include <cctype>
#include <cstring>

#include <string>
#include <memory>
#include <list>
#include <utility>
#include <optional>
#include <fstream>
#include <fmt/ostream.h>

#include <qbsp/brush.hh>
#include <qbsp/map.hh>
#include <qbsp/qbsp.hh>

#include <common/parser.hh>
#include <common/fs.hh>
#include <common/imglib.hh>
#include <common/qvec.hh>

mapdata_t map;
std::shared_mutex map_planes_lock;

const std::optional<img::texture_meta> &mapdata_t::load_image_meta(const std::string_view &name)
{
    static std::optional<img::texture_meta> nullmeta = std::nullopt;
    auto it = meta_cache.find(name.data());

    if (it != meta_cache.end()) {
        return it->second;
    }

    // try a meta-only texture first; this is all we really need anyways
    if (auto [texture_meta, _0, _1] = img::load_texture_meta(name, qbsp_options.target_game, qbsp_options);
        texture_meta) {
        // slight special case: if the meta has no width/height defined,
        // pull it from the real texture.
        if (!texture_meta->width || !texture_meta->height) {
            auto [texture, _0, _1] = img::load_texture(name, true, qbsp_options.target_game, qbsp_options);

            if (texture) {
                texture_meta->width = texture->meta.width;
                texture_meta->height = texture->meta.height;
            }
        }

        if (!texture_meta->width || !texture_meta->height) {
            logging::print("WARNING: texture {} has empty width/height \n", name);
        }

        return meta_cache.emplace(name, texture_meta).first->second;
    }

    // couldn't find a meta texture, so pull it from the pixel image
    if (auto [texture, _0, _1] = img::load_texture(name, true, qbsp_options.target_game, qbsp_options); texture) {
        return meta_cache.emplace(name, texture->meta).first->second;
    }

    logging::print("WARNING: Couldn't locate texture for {}\n", name);
    meta_cache.emplace(name, std::nullopt);
    return nullmeta;
}

static std::shared_ptr<fs::archive_like> LoadTexturePath(const fs::path &path)
{
    if (qbsp_options.wadpaths.pathsValue().empty() || path.is_absolute()) {
        return fs::addArchive(path, false);
    }

    for (auto &wadpath : qbsp_options.wadpaths.pathsValue()) {
        return fs::addArchive(wadpath.path / path, wadpath.external);
    }

    return nullptr;
}

static void EnsureTexturesLoaded()
{
    // Q2 doesn't need this
    if (qbsp_options.target_game->id == GAME_QUAKE_II) {
        return;
    }

    if (map.textures_loaded)
        return;

    map.textures_loaded = true;

    const mapentity_t *entity = map.world_entity();

    std::string wadstring = entity->epairs.get("_wad");

    if (wadstring.empty()) {
        wadstring = entity->epairs.get("wad");
    }

    bool loaded_any_archive = false;

    if (wadstring.empty()) {
        logging::print("WARNING: No wad or _wad key exists in the worldmodel\n");
    } else {
        imemstream stream(wadstring.data(), wadstring.size());
        std::string wad;

        while (std::getline(stream, wad, ';')) {
            if (LoadTexturePath(wad)) {
                loaded_any_archive = true;
            }
        }
    }

    if (!loaded_any_archive) {
        if (!wadstring.empty()) {
            logging::print("WARNING: No valid WAD filenames in worldmodel\n");
        }

        /* Try the default wad name */
        fs::path defaultwad = qbsp_options.map_path;
        defaultwad.replace_extension("wad");

        if (fs::exists(defaultwad)) {
            logging::print("INFO: Using default WAD: {}\n", defaultwad);
            LoadTexturePath(defaultwad);
        }
    }
}

// Useful shortcuts
mapentity_t *mapdata_t::world_entity()
{
    return &entities.at(0);
}

void mapdata_t::reset()
{
    *this = mapdata_t{};
}

struct texdef_valve_t
{
    qmat<vec_t, 2, 3> axis{};
    qvec2d scale{};
    qvec2d shift{};
};

struct texdef_quake_ed_t
{
    vec_t rotate = 0;
    qvec2d scale{};
    qvec2d shift{};
};

struct texdef_quake_ed_noshift_t
{
    vec_t rotate = 0;
    qvec2d scale{};
};

struct texdef_etp_t
{
    std::array<qvec3d, 3> planepoints{};
    bool tx2 = false;
};

using texdef_brush_primitives_t = qmat<vec_t, 2, 3>;

static texdef_valve_t TexDef_BSPToValve(const texvecf &in_vecs);
static qvec2f projectToAxisPlane(const qvec3d &snapped_normal, const qvec3d &point);
static texdef_quake_ed_noshift_t Reverse_QuakeEd(qmat2x2f M, const qbsp_plane_t &plane, bool preserveX);
static void SetTexinfo_QuakeEd_New(
    const qbsp_plane_t &plane, const qvec2d &shift, vec_t rotate, const qvec2d &scale, texvecf &out_vecs);
static void TestExpandBrushes(const mapentity_t *src);

const mapface_t &mapbrush_t::face(int i) const
{
    if (i < 0 || i >= this->numfaces)
        FError("{} out of bounds (numfaces {})", i, this->numfaces);
    return map.faces.at(this->firstface + i);
}

const mapbrush_t &mapentity_t::mapbrush(int i) const
{
    if (i < 0 || i >= this->nummapbrushes)
        FError("{} out of bounds (nummapbrushes {})", i, this->nummapbrushes);
    return map.brushes.at(this->firstmapbrush + i);
}

static void AddAnimTex(const char *name)
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
        FError("Bad animating texture {}", name);

    /*
     * Always add the lower numbered animation frames first, otherwise
     * many Quake engines will exit with an error loading the bsp.
     */
    snprintf(framename, sizeof(framename), "%s", name);
    for (i = 0; i < frame; i++) {
        framename[1] = basechar + i;
        for (j = 0; j < map.miptex.size(); j++) {
            if (!Q_strcasecmp(framename, map.miptex.at(j).name.c_str()))
                break;
        }
        if (j < map.miptex.size())
            continue;

        map.miptex.push_back({framename});
    }
}

int FindMiptex(const char *name, std::optional<extended_texinfo_t> &extended_info, bool internal, bool recursive)
{
    const char *pathsep;
    int i;

    // FIXME: figure out a way that we can move this to gamedef
    if (qbsp_options.target_game->id != GAME_QUAKE_II) {
        /* Ignore leading path in texture names (Q2 map compatibility) */
        pathsep = strrchr(name, '/');
        if (pathsep)
            name = pathsep + 1;

        if (!extended_info.has_value()) {
            extended_info = extended_texinfo_t{};
        }

        for (i = 0; i < map.miptex.size(); i++) {
            const maptexdata_t &tex = map.miptex.at(i);

            if (!Q_strcasecmp(name, tex.name.c_str())) {
                return i;
            }
        }

        i = map.miptex.size();
        map.miptex.push_back({name});

        /* Handle animating textures carefully */
        if (name[0] == '+') {
            AddAnimTex(name);
        }
    } else {
        // load .wal first
        auto wal = map.load_image_meta(name);

        if (wal && !internal && !extended_info.has_value()) {
            extended_info = extended_texinfo_t{wal->contents, wal->flags, wal->value, wal->animation};
        }

        if (!extended_info.has_value()) {
            extended_info = extended_texinfo_t{};
        }

        for (i = 0; i < map.miptex.size(); i++) {
            const maptexdata_t &tex = map.miptex.at(i);

            if (!Q_strcasecmp(name, tex.name.c_str()) && tex.flags.native == extended_info->flags.native &&
                tex.value == extended_info->value && tex.animation == extended_info->animation) {

                return i;
            }
        }

        i = map.miptex.size();
        map.miptex.push_back({name, extended_info->flags, extended_info->value, extended_info->animation});

        /* Handle animating textures carefully */
        if (!extended_info->animation.empty() && recursive) {

            int last_i = i;

            // recursively load animated textures until we loop back to us
            while (true) {
                // wal for next chain
                wal = map.load_image_meta(wal->animation.c_str());

                // texinfo base for animated wal
                std::optional<extended_texinfo_t> animation_info = extended_info;
                animation_info->animation = wal->animation;

                // fetch animation chain
                int next_i = FindMiptex(wal->name.data(), animation_info, internal, false);
                map.miptex[last_i].animation_miptex = next_i;
                last_i = next_i;

                // looped back
                if (wal->animation == name)
                    break;
            }

            // link back to the start
            map.miptex[last_i].animation_miptex = i;
        }
    }

    return i;
}

static bool IsSkipName(const char *name)
{
    if (qbsp_options.noskip.value())
        return false;
    if (!Q_strcasecmp(name, "skip"))
        return true;
    if (!Q_strcasecmp(name, "*waterskip"))
        return true;
    if (!Q_strcasecmp(name, "*slimeskip"))
        return true;
    if (!Q_strcasecmp(name, "*lavaskip"))
        return true;
    if (!Q_strcasecmp(name, "bevel")) // zhlt compat
        return true;
    if (!Q_strcasecmp(name, "null")) // zhlt compat
        return true;
    return false;
}

static bool IsNoExpandName(const char *name)
{
    if (!Q_strcasecmp(name, "bevel")) // zhlt compat
        return true;
    return false;
}

static bool IsSpecialName(const char *name)
{
    if (name[0] == '*' && !qbsp_options.splitturb.value())
        return true;
    if (!Q_strncasecmp(name, "sky", 3) && !qbsp_options.splitsky.value())
        return true;
    return false;
}

static bool IsHintName(const char *name)
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
int FindTexinfo(const maptexinfo_t &texinfo)
{
    // NaN's will break mtexinfo_lookup, since they're being used as a std::map key and don't compare properly with <.
    // They should have been stripped out already in ValidateTextureProjection.
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 4; j++) {
            Q_assert(!std::isnan(texinfo.vecs.at(i, j)));
        }
    }

    // check for an exact match in the reverse lookup
    const auto it = map.mtexinfo_lookup.find(texinfo);
    if (it != map.mtexinfo_lookup.end()) {
        return it->second;
    }

    /* Allocate a new texinfo at the end of the array */
    const int num_texinfo = static_cast<int>(map.mtexinfos.size());
    map.mtexinfos.emplace_back(texinfo);
    map.mtexinfo_lookup[texinfo] = num_texinfo;

    // catch broken < implementations in maptexinfo_t
    assert(map.mtexinfo_lookup.find(texinfo) != map.mtexinfo_lookup.end());

    // create a copy of the miptex for animation chains
    if (map.miptex[texinfo.miptex].animation_miptex != -1) {
        maptexinfo_t anim_next = texinfo;

        anim_next.miptex = map.miptex[texinfo.miptex].animation_miptex;

        map.mtexinfos[num_texinfo].next = FindTexinfo(anim_next);
    }

    return num_texinfo;
}

static surfflags_t SurfFlagsForEntity(const maptexinfo_t &texinfo, const mapentity_t &entity)
{
    surfflags_t flags{};
    const char *texname = map.miptex.at(texinfo.miptex).name.c_str();
    const int shadow = entity.epairs.get_int("_shadow");

    // These flags are pulled from surf flags in Q2.
    // TODO: the Q1 version of this block can now be moved into texinfo
    // loading by shoving them inside of texinfo.flags like
    // Q2 does. Similarly, we can move the Q2 block out
    // into a special function, like.. I dunno,
    // game->surface_flags_from_name(surfflags_t &inout, const char *name)
    // which we can just call instead of this block.
    // the only annoyance is we can't access the various options (noskip,
    // splitturb, etc) from there.
    if (qbsp_options.target_game->id != GAME_QUAKE_II) {
        if (IsSkipName(texname))
            flags.is_skip = true;
        if (IsHintName(texname))
            flags.is_hint = true;
        if (IsSpecialName(texname))
            flags.native |= TEX_SPECIAL;
    } else {
        flags.native = texinfo.flags.native;

        if ((flags.native & Q2_SURF_NODRAW) || IsSkipName(texname))
            flags.is_skip = true;
        if ((flags.native & Q2_SURF_HINT) || IsHintName(texname))
            flags.is_hint = true;
    }
    if (IsNoExpandName(texname))
        flags.no_expand = true;
    if (entity.epairs.get_int("_dirt") == -1)
        flags.no_dirt = true;
    if (entity.epairs.get_int("_bounce") == -1)
        flags.no_bounce = true;
    if (entity.epairs.get_int("_minlight") == -1)
        flags.no_minlight = true;
    if (entity.epairs.get_int("_lightignore") == 1)
        flags.light_ignore = true;

    // "_minlight_exclude", "_minlight_exclude2", "_minlight_exclude3"...
    for (int i = 0; i <= 9; i++) {
        std::string key = "_minlight_exclude";
        if (i > 0) {
            key += std::to_string(i);
        }

        const std::string &excludeTex = entity.epairs.get(key.c_str());
        if (!excludeTex.empty() && !Q_strcasecmp(texname, excludeTex)) {
            flags.no_minlight = true;
        }
    }

    if (shadow == -1)
        flags.no_shadow = true;
    if (!Q_strcasecmp("func_detail_illusionary", entity.epairs.get("classname"))) {
        /* Mark these entities as TEX_NOSHADOW unless the mapper set "_shadow" "1" */
        if (shadow != 1) {
            flags.no_shadow = true;
        }
    }

    // handle "_phong" and "_phong_angle" and "_phong_angle_concave"
    vec_t phongangle = entity.epairs.get_float("_phong_angle");
    const int phong = entity.epairs.get_int("_phong");

    if (phong && (phongangle == 0.0)) {
        phongangle = 89.0; // default _phong_angle
    }

    if (phongangle) {
        flags.phong_angle = clamp(phongangle, 0.0, 360.0);
    }

    const vec_t phong_angle_concave = entity.epairs.get_float("_phong_angle_concave");
    flags.phong_angle_concave = clamp(phong_angle_concave, 0.0, 360.0);

    // handle "_minlight"
    const vec_t minlight = entity.epairs.get_float("_minlight");
    if (minlight > 0) {
        // CHECK: allow > 510 now that we're float? or is it not worth it since it will
        // be beyond max?
        flags.minlight = clamp(minlight, 0.0, 510.0);
    }

    // handle "_mincolor"
    {
        qvec3d mincolor{};

        entity.epairs.get_vector("_mincolor", mincolor);
        if (qv::epsilonEmpty(mincolor, EQUAL_EPSILON)) {
            entity.epairs.get_vector("_minlight_color", mincolor);
        }

        mincolor = qv::normalize_color_format(mincolor);
        if (!qv::epsilonEmpty(mincolor, EQUAL_EPSILON)) {
            for (int32_t i = 0; i < 3; i++) {
                flags.minlight_color[i] = clamp(mincolor[i], 0.0, 255.0);
            }
        }
    }

    // handle "_light_alpha"
    const vec_t lightalpha = entity.epairs.get_float("_light_alpha");
    if (lightalpha != 0.0) {
        flags.light_alpha = clamp(lightalpha, 0.0, 1.0);
    }

    return flags;
}

static void ParseEpair(parser_t &parser, mapentity_t *entity)
{
    std::string key = parser.token;

    // trim whitespace from start/end
    while (std::isspace(key.front())) {
        key.erase(key.begin());
    }
    while (std::isspace(key.back())) {
        key.erase(key.end() - 1);
    }

    parser.parse_token(PARSE_SAMELINE);

    entity->epairs.set(key, parser.token);

    if (string_iequals(key, "origin")) {
        entity->epairs.get_vector(key, entity->origin);
    }
}

static void TextureAxisFromPlane(const qplane3d &plane, qvec3d &xv, qvec3d &yv, qvec3d &snapped_normal)
{
    constexpr qvec3d baseaxis[18] = {
        {0, 0, 1}, {1, 0, 0}, {0, -1, 0}, // floor
        {0, 0, -1}, {1, 0, 0}, {0, -1, 0}, // ceiling
        {1, 0, 0}, {0, 1, 0}, {0, 0, -1}, // west wall
        {-1, 0, 0}, {0, 1, 0}, {0, 0, -1}, // east wall
        {0, 1, 0}, {1, 0, 0}, {0, 0, -1}, // south wall
        {0, -1, 0}, {1, 0, 0}, {0, 0, -1} // north wall
    };

    int bestaxis;
    vec_t dot, best;
    int i;

    best = 0;
    bestaxis = 0;

    for (i = 0; i < 6; i++) {
        dot = qv::dot(plane.normal, baseaxis[i * 3]);
        if (dot > best || (dot == best && !qbsp_options.oldaxis.value())) {
            best = dot;
            bestaxis = i;
        }
    }

    xv = baseaxis[bestaxis * 3 + 1];
    yv = baseaxis[bestaxis * 3 + 2];
    snapped_normal = baseaxis[bestaxis * 3];
}

static quark_tx_info_t ParseExtendedTX(parser_t &parser)
{
    quark_tx_info_t result;

    if (parser.parse_token(PARSE_COMMENT | PARSE_OPTIONAL)) {
        if (!strncmp(parser.token.c_str(), "//TX", 4)) {
            if (parser.token[4] == '1')
                result.quark_tx1 = true;
            else if (parser.token[4] == '2')
                result.quark_tx2 = true;
        }
    } else {
        // Parse extra Quake 2 surface info
        if (parser.parse_token(PARSE_OPTIONAL)) {
            result.info = extended_texinfo_t{std::stoi(parser.token)};

            if (parser.parse_token(PARSE_OPTIONAL)) {
                result.info->flags.native = std::stoi(parser.token);
            }
            if (parser.parse_token(PARSE_OPTIONAL)) {
                result.info->value = std::stoi(parser.token);
            }
        }
    }

    return result;
}

static qmat4x4f texVecsTo4x4Matrix(const qplane3d &faceplane, const texvecf &in_vecs)
{
    //           [s]
    // T * vec = [t]
    //           [distOffPlane]
    //           [?]

    qmat4x4f T{
        in_vecs.at(0, 0), in_vecs.at(1, 0), static_cast<float>(faceplane.normal[0]), 0, // col 0
        in_vecs.at(0, 1), in_vecs.at(1, 1), static_cast<float>(faceplane.normal[1]), 0, // col 1
        in_vecs.at(0, 2), in_vecs.at(1, 2), static_cast<float>(faceplane.normal[2]), 0, // col 2
        in_vecs.at(0, 3), in_vecs.at(1, 3), static_cast<float>(-faceplane.dist), 1 // col 3
    };
    return T;
}

static qmat2x2f scale2x2(float xscale, float yscale)
{
    qmat2x2f M{xscale, 0, // col 0
        0, yscale}; // col1
    return M;
}

static qmat2x2f rotation2x2_deg(float degrees)
{
    float r = degrees * (Q_PI / 180.0);
    float cosr = cos(r);
    float sinr = sin(r);

    // [ cosTh -sinTh ]
    // [ sinTh cosTh  ]

    qmat2x2f M{cosr, sinr, // col 0
        -sinr, cosr}; // col1

    return M;
}

static float extractRotation(qmat2x2f m)
{
    qvec2f point = m * qvec2f(1, 0); // choice of this matters if there's shearing
    float rotation = atan2(point[1], point[0]) * 180.0 / Q_PI;
    return rotation;
}

static qvec2f evalTexDefAtPoint(const texdef_quake_ed_t &texdef, const qbsp_plane_t &faceplane, const qvec3f &point)
{
    texvecf temp;
    SetTexinfo_QuakeEd_New(faceplane, texdef.shift, texdef.rotate, texdef.scale, temp);

    const qmat4x4f worldToTexSpace_res = texVecsTo4x4Matrix(faceplane, temp);
    const qvec2f uv = qvec2f(worldToTexSpace_res * qvec4f(point[0], point[1], point[2], 1.0f));
    return uv;
}

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
    for (int i = 0; i < 2; i++) {
        if (fabs(a[i] - b[i]) > epsilon) {
            printf("warning, checkEq failed\n");
        }
    }
}

qvec2f normalizeShift(const std::optional<img::texture_meta> &texture, const qvec2f &in)
{
    if (!texture) {
        return in; // can't do anything without knowing the texture size.
    }

    int fullWidthOffsets = static_cast<int>(in[0]) / texture->width;
    int fullHeightOffsets = static_cast<int>(in[1]) / texture->height;

    qvec2f result(in[0] - static_cast<float>(fullWidthOffsets * texture->width),
        in[1] - static_cast<float>(fullHeightOffsets * texture->height));
    return result;
}

/// `texture` is optional. If given, the "shift" values can be normalized
static texdef_quake_ed_t TexDef_BSPToQuakeEd(const qbsp_plane_t &faceplane,
    const std::optional<img::texture_meta> &texture, const texvecf &in_vecs, const std::array<qvec3d, 3> &facepoints)
{
    // First get the un-rotated, un-scaled unit texture vecs (based on the face plane).
    qvec3d snapped_normal;
    qvec3d unrotated_vecs[2];
    TextureAxisFromPlane(faceplane, unrotated_vecs[0], unrotated_vecs[1], snapped_normal);

    const qmat4x4f worldToTexSpace = texVecsTo4x4Matrix(faceplane, in_vecs);

    // Grab the UVs of the 3 reference points
    qvec2f facepoints_uvs[3];
    for (int i = 0; i < 3; i++) {
        facepoints_uvs[i] = qvec2f(worldToTexSpace * qvec4f(facepoints[i][0], facepoints[i][1], facepoints[i][2], 1.0));
    }

    // Project the 3 reference points onto the axis plane. They are now 2d points.
    qvec2f facepoints_projected[3];
    for (int i = 0; i < 3; i++) {
        facepoints_projected[i] = projectToAxisPlane(snapped_normal, facepoints[i]);
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

    const qmat4x4f M{
        p0p1[0], 0, p0p2[0], 0, // col 0
        p0p1[1], 0, p0p2[1], 0, // col 1
        0, p0p1[0], 0, p0p2[0], // col 2
        0, p0p1[1], 0, p0p2[1] // col 3
    };

    const qmat4x4f Minv = qv::inverse(M);
    const qvec4f abcd = Minv * qvec4f(p0p1_uv[0], p0p1_uv[1], p0p2_uv[0], p0p2_uv[1]);

    const qmat2x2f texPlaneToUV{abcd[0], abcd[2], // col 0
        abcd[1], abcd[3]}; // col 1

    {
        // self check
        //        qvec2f uv01_test = texPlaneToUV * p0p1;
        //        qvec2f uv02_test = texPlaneToUV * p0p2;

        // these fail if one of the texture axes is 0 length.
        //        checkEq(uv01_test, p0p1_uv, 0.01);
        //        checkEq(uv02_test, p0p2_uv, 0.01);
    }

    const texdef_quake_ed_noshift_t res = Reverse_QuakeEd(texPlaneToUV, faceplane, false);

    // figure out shift based on facepoints[0]
    const qvec3f testpoint = facepoints[0];
    qvec2f uv0_actual = evalTexDefAtPoint(addShift(res, qvec2f(0, 0)), faceplane, testpoint);
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

bool EqualDegrees(float a, float b)
{
    return fabs(NormalizeDegrees(a) - NormalizeDegrees(b)) < 0.001;
}

static std::pair<int, int> getSTAxes(const qvec3d &snapped_normal)
{
    if (snapped_normal[0]) {
        return std::make_pair(1, 2);
    } else if (snapped_normal[1]) {
        return std::make_pair(0, 2);
    } else {
        return std::make_pair(0, 1);
    }
}

static qvec2f projectToAxisPlane(const qvec3d &snapped_normal, const qvec3d &point)
{
    const std::pair<int, int> axes = getSTAxes(snapped_normal);
    const qvec2f proj(point[axes.first], point[axes.second]);
    return proj;
}

float clockwiseDegreesBetween(qvec2f start, qvec2f end)
{
    start = qv::normalize(start);
    end = qv::normalize(end);

    const float cosAngle = max(-1.0f, min(1.0f, qv::dot(start, end)));
    const float unsigned_degrees = acos(cosAngle) * (360.0 / (2.0 * Q_PI));

    if (unsigned_degrees < ANGLEEPSILON)
        return 0;

    // get a normal for the rotation plane using the right-hand rule
    // if this is pointing up (qvec3f(0,0,1)), it's counterclockwise rotation.
    // if this is pointing down (qvec3f(0,0,-1)), it's clockwise rotation.
    qvec3f rotationNormal = qv::normalize(qv::cross(qvec3f(start[0], start[1], 0.0f), qvec3f(end[0], end[1], 0.0f)));

    const float normalsCosAngle = qv::dot(rotationNormal, qvec3f(0, 0, 1));
    if (normalsCosAngle >= 0) {
        // counterclockwise rotation
        return -unsigned_degrees;
    }
    // clockwise rotation
    return unsigned_degrees;
}

static texdef_quake_ed_noshift_t Reverse_QuakeEd(qmat2x2f M, const qbsp_plane_t &plane, bool preserveX)
{
    // Check for shear, because we might tweak M to remove it
    {
        qvec2f Xvec = M.row(0);
        qvec2f Yvec = M.row(1);
        double cosAngle = qv::dot(qv::normalize(Xvec), qv::normalize(Yvec));

        // const double oldXscale = sqrt(pow(M[0][0], 2.0) + pow(M[1][0], 2.0));
        // const double oldYscale = sqrt(pow(M[0][1], 2.0) + pow(M[1][1], 2.0));

        if (fabs(cosAngle) > 0.001) {
            // Detected shear

            if (preserveX) {
                const float degreesToY = clockwiseDegreesBetween(Xvec, Yvec);
                const bool CW = (degreesToY > 0);

                // turn 90 degrees from Xvec
                const qvec2f newYdir =
                    qv::normalize(qvec2f(qv::cross(qvec3f(0, 0, CW ? -1.0f : 1.0f), qvec3f(Xvec[0], Xvec[1], 0.0))));

                // scalar projection of the old Yvec onto newYDir to get the new Yscale
                const float newYscale = qv::dot(Yvec, newYdir);
                Yvec = newYdir * static_cast<float>(newYscale);
            } else {
                // Preserve Y.

                const float degreesToX = clockwiseDegreesBetween(Yvec, Xvec);
                const bool CW = (degreesToX > 0);

                // turn 90 degrees from Yvec
                const qvec2f newXdir =
                    qv::normalize(qvec2f(qv::cross(qvec3f(0, 0, CW ? -1.0f : 1.0f), qvec3f(Yvec[0], Yvec[1], 0.0))));

                // scalar projection of the old Xvec onto newXDir to get the new Xscale
                const float newXscale = qv::dot(Xvec, newXdir);
                Xvec = newXdir * static_cast<float>(newXscale);
            }

            // recheck
            cosAngle = qv::dot(qv::normalize(Xvec), qv::normalize(Yvec));
            if (fabs(cosAngle) > 0.001) {
                FError("SHEAR correction failed\n");
            }

            // update M
            M.at(0, 0) = Xvec[0];
            M.at(0, 1) = Xvec[1];

            M.at(1, 0) = Yvec[0];
            M.at(1, 1) = Yvec[1];
        }
    }

    // extract abs(scale)
    const double absXscale = sqrt(pow(M.at(0, 0), 2.0) + pow(M.at(0, 1), 2.0));
    const double absYscale = sqrt(pow(M.at(1, 0), 2.0) + pow(M.at(1, 1), 2.0));
    const qmat2x2f applyAbsScaleM{static_cast<float>(absXscale), // col0
        0,
        0, // col1
        static_cast<float>(absYscale)};

    qvec3d vecs[2];
    qvec3d snapped_normal;
    TextureAxisFromPlane(plane, vecs[0], vecs[1], snapped_normal);

    const qvec2f sAxis = projectToAxisPlane(snapped_normal, vecs[0]);
    const qvec2f tAxis = projectToAxisPlane(snapped_normal, vecs[1]);

    // This is an identity matrix possibly with negative signs.
    const qmat2x2f axisFlipsM{sAxis[0], tAxis[0], // col0
        sAxis[1], tAxis[1]}; // col1

    // N.B. this is how M is built in SetTexinfo_QuakeEd_New and guides how we
    // strip off components of it later in this function.
    //
    //    qmat2x2f M = scaleM * rotateM * axisFlipsM;

    // strip off the magnitude component of the scale, and `axisFlipsM`.
    const qmat2x2f flipRotate = qv::inverse(applyAbsScaleM) * M * qv::inverse(axisFlipsM);

    // We don't know the signs on the scales, which will mess up figuring out the rotation, so try all 4 combinations
    for (float xScaleSgn : std::vector<float>{-1.0, 1.0}) {
        for (float yScaleSgn : std::vector<float>{-1.0, 1.0}) {

            // "apply" - matrix constructed to apply a guessed value
            // "guess" - this matrix might not be what we think

            const qmat2x2f applyGuessedFlipM{xScaleSgn, // col0
                0,
                0, // col1
                yScaleSgn};

            const qmat2x2f rotateMGuess = qv::inverse(applyGuessedFlipM) * flipRotate;
            const float angleGuess = extractRotation(rotateMGuess);

            //            const qmat2x2f Mident = rotateMGuess * rotation2x2_deg(-angleGuess);

            const qmat2x2f applyAngleGuessM = rotation2x2_deg(angleGuess);
            const qmat2x2f Mguess = applyGuessedFlipM * applyAbsScaleM * applyAngleGuessM * axisFlipsM;

            if (fabs(M.at(0, 0) - Mguess.at(0, 0)) < 0.001 && fabs(M.at(1, 0) - Mguess.at(1, 0)) < 0.001 &&
                fabs(M.at(0, 1) - Mguess.at(0, 1)) < 0.001 && fabs(M.at(1, 1) - Mguess.at(1, 1)) < 0.001) {

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

    // printf("Warning, Reverse_QuakeEd failed\n");

    texdef_quake_ed_noshift_t fail;
    return fail;
}

static void SetTexinfo_QuakeEd_New(
    const qbsp_plane_t &plane, const qvec2d &shift, vec_t rotate, const qvec2d &scale, texvecf &out_vecs)
{
    vec_t sanitized_scale[2];
    for (int i = 0; i < 2; i++) {
        sanitized_scale[i] = (scale[i] != 0.0) ? scale[i] : 1.0;
    }

    qvec3d vecs[2];
    qvec3d snapped_normal;
    TextureAxisFromPlane(plane, vecs[0], vecs[1], snapped_normal);

    qvec2f sAxis = projectToAxisPlane(snapped_normal, vecs[0]);
    qvec2f tAxis = projectToAxisPlane(snapped_normal, vecs[1]);

    // This is an identity matrix possibly with negative signs.
    qmat2x2f axisFlipsM{sAxis[0], tAxis[0], // col0
        sAxis[1], tAxis[1]}; // col1

    qmat2x2f rotateM = rotation2x2_deg(rotate);
    qmat2x2f scaleM = scale2x2(1.0 / sanitized_scale[0], 1.0 / sanitized_scale[1]);

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
            FError("wrong rotate got {} expected {}\n", reversed.rotate, rotate);
        }

        if (fabs(reversed.scale[0] - sanitized_scale[0]) > 0.001 ||
            fabs(reversed.scale[1] - sanitized_scale[1]) > 0.001) {
            FError("wrong scale, got {} {} exp {} {}\n", reversed.scale[0], reversed.scale[1], sanitized_scale[0],
                sanitized_scale[1]);
        }
    }

    // copy M into the output vectors
    out_vecs = {};

    const std::pair<int, int> axes = getSTAxes(snapped_normal);

    //                        M[col][row]
    // S
    out_vecs.at(0, axes.first) = M.at(0, 0);
    out_vecs.at(0, axes.second) = M.at(0, 1);
    out_vecs.at(0, 3) = shift[0];

    // T
    out_vecs.at(1, axes.first) = M.at(1, 0);
    out_vecs.at(1, axes.second) = M.at(1, 1);
    out_vecs.at(1, 3) = shift[1];
}

static void SetTexinfo_QuakeEd(const qbsp_plane_t &plane, const std::array<qvec3d, 3> &planepts, const qvec2d &shift,
    const vec_t &rotate, const qvec2d &scale, maptexinfo_t *out)
{
    int i, j;
    qvec3d vecs[2];
    int sv, tv;
    vec_t ang, sinv, cosv;
    vec_t ns, nt;
    qvec3d unused;

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
            out->vecs.at(i, j) = vecs[i][j] / (scale[i] ? scale[i] : 1);

    out->vecs.at(0, 3) = shift[0];
    out->vecs.at(1, 3) = shift[1];

    if (false) {
        // Self-test of SetTexinfo_QuakeEd_New
        texvecf check;
        SetTexinfo_QuakeEd_New(plane, shift, rotate, scale, check);
        for (int i = 0; i < 2; i++) {
            for (int j = 0; j < 4; j++) {
                if (fabs(check.at(i, j) - out->vecs.at(i, j)) > 0.001) {
                    SetTexinfo_QuakeEd_New(plane, shift, rotate, scale, check);
                    FError("fail");
                }
            }
        }
    }

    if (false) {
        // Self-test of TexDef_BSPToQuakeEd
        texdef_quake_ed_t reversed = TexDef_BSPToQuakeEd(plane, std::nullopt, out->vecs, planepts);

        if (!EqualDegrees(reversed.rotate, rotate)) {
            reversed.rotate += 180;
            reversed.scale[0] *= -1;
            reversed.scale[1] *= -1;
        }

        if (!EqualDegrees(reversed.rotate, rotate)) {
            fmt::print("wrong rotate got {} expected {}\n", reversed.rotate, rotate);
        }

        if (fabs(reversed.scale[0] - scale[0]) > 0.001 || fabs(reversed.scale[1] - scale[1]) > 0.001) {
            fmt::print("wrong scale, got {} {} exp {} {}\n", reversed.scale[0], reversed.scale[1], scale[0], scale[1]);
        }

        if (fabs(reversed.shift[0] - shift[0]) > 0.1 || fabs(reversed.shift[1] - shift[1]) > 0.1) {
            fmt::print("wrong shift, got {} {} exp {} {}\n", reversed.shift[0], reversed.shift[1], shift[0], shift[1]);
        }
    }
}

static void SetTexinfo_QuArK(
    parser_t &parser, const std::array<qvec3d, 3> &planepts, texcoord_style_t style, maptexinfo_t *out)
{
    int i;
    qvec3d vecs[2];
    vec_t a, b, c, d;
    vec_t determinant;

    /*
     * Type 1 uses vecs[0] = (pt[2] - pt[0]) and vecs[1] = (pt[1] - pt[0])
     * Type 2 reverses the order of the vecs
     * 128 is the scaling factor assumed by QuArK.
     */
    switch (style) {
        case TX_QUARK_TYPE1:
            vecs[0] = planepts[2] - planepts[0];
            vecs[1] = planepts[1] - planepts[0];
            break;
        case TX_QUARK_TYPE2:
            vecs[0] = planepts[1] - planepts[0];
            vecs[1] = planepts[2] - planepts[0];
            break;
        default: FError("Internal error: bad texture coordinate style");
    }

    vecs[0] *= 1.0 / 128.0;
    vecs[1] *= 1.0 / 128.0;

    a = qv::dot(vecs[0], vecs[0]);
    b = qv::dot(vecs[0], vecs[1]);
    c = b; /* qv::dot(vecs[1], vecs[0]); */
    d = qv::dot(vecs[1], vecs[1]);

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
        logging::print("WARNING: line {}: Face with degenerate QuArK-style texture axes\n", parser.linenum);
        for (i = 0; i < 3; i++)
            out->vecs.at(0, i) = out->vecs.at(1, i) = 0;
    } else {
        for (i = 0; i < 3; i++) {
            out->vecs.at(0, i) = (d * vecs[0][i] - b * vecs[1][i]) / determinant;
            out->vecs.at(1, i) = -(a * vecs[1][i] - c * vecs[0][i]) / determinant;
        }
    }

    /* Finally, the texture offset is indicated by planepts[0] */
    for (i = 0; i < 3; ++i) {
        vecs[0][i] = out->vecs.at(0, i);
        vecs[1][i] = out->vecs.at(1, i);
    }
    out->vecs.at(0, 3) = -qv::dot(vecs[0], planepts[0]);
    out->vecs.at(1, 3) = -qv::dot(vecs[1], planepts[0]);
}

static void SetTexinfo_Valve220(qmat<vec_t, 2, 3> &axis, const qvec2d &shift, const qvec2d &scale, maptexinfo_t *out)
{
    int i;

    for (i = 0; i < 3; i++) {
        out->vecs.at(0, i) = axis.at(0, i) / scale[0];
        out->vecs.at(1, i) = axis.at(1, i) / scale[1];
    }
    out->vecs.at(0, 3) = shift[0];
    out->vecs.at(1, 3) = shift[1];
}

/*
 ComputeAxisBase()
 from q3map2

 computes the base texture axis for brush primitive texturing
 note: ComputeAxisBase here and in editor code must always BE THE SAME!
 warning: special case behaviour of atan2( y, x ) <-> atan( y / x ) might not be the same everywhere when x == 0
 rotation by (0,RotY,RotZ) assigns X to normal
 */
static void ComputeAxisBase(const qvec3d &normal_unsanitized, qvec3d &texX, qvec3d &texY)
{
    vec_t RotY, RotZ;

    qvec3d normal = normal_unsanitized;

    /* do some cleaning */
    if (fabs(normal[0]) < 1e-6) {
        normal[0] = 0.0f;
    }
    if (fabs(normal[1]) < 1e-6) {
        normal[1] = 0.0f;
    }
    if (fabs(normal[2]) < 1e-6) {
        normal[2] = 0.0f;
    }

    /* compute the two rotations around y and z to rotate x to normal */
    RotY = -atan2(normal[2], sqrt(normal[1] * normal[1] + normal[0] * normal[0]));
    RotZ = atan2(normal[1], normal[0]);

    /* rotate (0,1,0) and (0,0,1) to compute texX and texY */
    texX[0] = -sin(RotZ);
    texX[1] = cos(RotZ);
    texX[2] = 0;

    /* the texY vector is along -z (t texture coorinates axis) */
    texY[0] = -sin(RotY) * cos(RotZ);
    texY[1] = -sin(RotY) * sin(RotZ);
    texY[2] = -cos(RotY);
}

static void SetTexinfo_BrushPrimitives(
    const qmat<vec_t, 2, 3> &texMat, const qvec3d &faceNormal, int texWidth, int texHeight, texvecf &vecs)
{
    qvec3d texX, texY;

    ComputeAxisBase(faceNormal, texX, texY);

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

    vecs.at(0, 0) = texWidth * ((texX[0] * texMat.at(0, 0)) + (texY[0] * texMat.at(0, 1)));
    vecs.at(0, 1) = texWidth * ((texX[1] * texMat.at(0, 0)) + (texY[1] * texMat.at(0, 1)));
    vecs.at(0, 2) = texWidth * ((texX[2] * texMat.at(0, 0)) + (texY[2] * texMat.at(0, 1)));
    vecs.at(0, 3) = texWidth * texMat.at(0, 2);

    vecs.at(1, 0) = texHeight * ((texX[0] * texMat.at(1, 0)) + (texY[0] * texMat.at(1, 1)));
    vecs.at(1, 1) = texHeight * ((texX[1] * texMat.at(1, 0)) + (texY[1] * texMat.at(1, 1)));
    vecs.at(1, 2) = texHeight * ((texX[2] * texMat.at(1, 0)) + (texY[2] * texMat.at(1, 1)));
    vecs.at(1, 3) = texHeight * texMat.at(1, 2);
}

// From FaceToBrushPrimitFace in GtkRadiant
static texdef_brush_primitives_t TexDef_BSPToBrushPrimitives(
    const qplane3d &plane, const int texSize[2], const texvecf &in_vecs)
{
    qvec3d texX, texY;
    ComputeAxisBase(plane.normal, texX, texY);

    // compute projection vector
    qvec3d proj = plane.normal * plane.dist;

    // (0,0) in plane axis base is (0,0,0) in world coordinates + projection on the affine plane
    // (1,0) in plane axis base is texX in world coordinates + projection on the affine plane
    // (0,1) in plane axis base is texY in world coordinates + projection on the affine plane
    // use old texture code to compute the ST coords of these points
    qvec2d st[] = {in_vecs.uvs(proj, texSize[0], texSize[1]), in_vecs.uvs(texX + proj, texSize[0], texSize[1]),
        in_vecs.uvs(texY + proj, texSize[0], texSize[1])};
    // compute texture matrix
    texdef_brush_primitives_t res;
    res.set_col(2, st[0]);
    res.set_col(0, st[1] - st[0]);
    res.set_col(1, st[2] - st[0]);
    return res;
}

static void ParsePlaneDef(parser_t &parser, std::array<qvec3d, 3> &planepts)
{
    int i, j;

    for (i = 0; i < 3; i++) {
        if (i != 0)
            parser.parse_token();
        if (parser.token != "(")
            goto parse_error;

        for (j = 0; j < 3; j++) {
            parser.parse_token(PARSE_SAMELINE);
            planepts[i][j] = std::stod(parser.token);
        }

        parser.parse_token(PARSE_SAMELINE);
        if (parser.token != ")")
            goto parse_error;
    }
    return;

parse_error:
    FError("line {}: Invalid brush plane format", parser.linenum);
}

static void ParseValve220TX(parser_t &parser, qmat<vec_t, 2, 3> &axis, qvec2d &shift, vec_t &rotate, qvec2d &scale)
{
    int i, j;

    for (i = 0; i < 2; i++) {
        parser.parse_token(PARSE_SAMELINE);
        if (parser.token != "[")
            goto parse_error;
        for (j = 0; j < 3; j++) {
            parser.parse_token(PARSE_SAMELINE);
            axis.at(i, j) = std::stod(parser.token);
        }
        parser.parse_token(PARSE_SAMELINE);
        shift[i] = std::stod(parser.token);
        parser.parse_token(PARSE_SAMELINE);
        if (parser.token != "]")
            goto parse_error;
    }
    parser.parse_token(PARSE_SAMELINE);
    rotate = std::stod(parser.token);
    parser.parse_token(PARSE_SAMELINE);
    scale[0] = std::stod(parser.token);
    parser.parse_token(PARSE_SAMELINE);
    scale[1] = std::stod(parser.token);
    return;

parse_error:
    FError("line {}: couldn't parse Valve220 texture info", parser.linenum);
}

static void ParseBrushPrimTX(parser_t &parser, qmat<vec_t, 2, 3> &texMat)
{
    parser.parse_token(PARSE_SAMELINE);
    if (parser.token != "(")
        goto parse_error;

    for (int i = 0; i < 2; i++) {
        parser.parse_token(PARSE_SAMELINE);
        if (parser.token != "(")
            goto parse_error;

        for (int j = 0; j < 3; j++) {
            parser.parse_token(PARSE_SAMELINE);
            texMat.at(i, j) = std::stod(parser.token);
        }

        parser.parse_token(PARSE_SAMELINE);
        if (parser.token != ")")
            goto parse_error;
    }

    parser.parse_token(PARSE_SAMELINE);
    if (parser.token != ")")
        goto parse_error;

    return;

parse_error:
    FError("line {}: couldn't parse Brush Primitives texture info", parser.linenum);
}

static void ParseTextureDef(parser_t &parser, mapface_t &mapface, const mapbrush_t &brush, maptexinfo_t *tx,
    std::array<qvec3d, 3> &planepts, const qplane3d &plane)
{
    vec_t rotate;
    qmat<vec_t, 2, 3> texMat, axis;
    qvec2d shift, scale;
    texcoord_style_t tx_type;

    quark_tx_info_t extinfo;

    if (brush.format == brushformat_t::BRUSH_PRIMITIVES) {
        ParseBrushPrimTX(parser, texMat);
        tx_type = TX_BRUSHPRIM;

        parser.parse_token(PARSE_SAMELINE);
        mapface.texname = parser.token;

        // Read extra Q2 params
        extinfo = ParseExtendedTX(parser);

        mapface.raw_info = extinfo.info;
    } else if (brush.format == brushformat_t::NORMAL) {
        parser.parse_token(PARSE_SAMELINE);
        mapface.texname = parser.token;

        parser.parse_token(PARSE_SAMELINE | PARSE_PEEK);
        if (parser.token == "[") {
            ParseValve220TX(parser, axis, shift, rotate, scale);
            tx_type = TX_VALVE_220;

            // Read extra Q2 params
            extinfo = ParseExtendedTX(parser);
        } else {
            parser.parse_token(PARSE_SAMELINE);
            shift[0] = std::stod(parser.token);
            parser.parse_token(PARSE_SAMELINE);
            shift[1] = std::stod(parser.token);
            parser.parse_token(PARSE_SAMELINE);
            rotate = std::stod(parser.token);
            parser.parse_token(PARSE_SAMELINE);
            scale[0] = std::stod(parser.token);
            parser.parse_token(PARSE_SAMELINE);
            scale[1] = std::stod(parser.token);

            // Read extra Q2 params and/or QuArK subtype
            extinfo = ParseExtendedTX(parser);
            if (extinfo.quark_tx1) {
                tx_type = TX_QUARK_TYPE1;
            } else if (extinfo.quark_tx2) {
                tx_type = TX_QUARK_TYPE2;
            } else {
                tx_type = TX_QUAKED;
            }
        }

        mapface.raw_info = extinfo.info;
    } else {
        FError("Bad brush format");
    }

    // if we have texture defs, see if we should remap this one
    if (auto it = qbsp_options.loaded_texture_defs.find(mapface.texname);
        it != qbsp_options.loaded_texture_defs.end()) {
        mapface.texname = std::get<0>(it->second);

        if (std::get<1>(it->second).has_value()) {
            mapface.raw_info = extinfo.info = std::get<1>(it->second).value();
        }
    }

    // If we're not Q2 but we're loading a Q2 map, just remove the extra
    // info so it can at least compile.
    if (qbsp_options.target_game->id != GAME_QUAKE_II) {
        extinfo.info = std::nullopt;
    } else {
        // assign animation to extinfo, so that we load the animated
        // first one first
        if (auto &wal = map.load_image_meta(mapface.texname.c_str())) {
            if (!extinfo.info) {
                extinfo.info = extended_texinfo_t{wal->contents, wal->flags, wal->value};
            }
            extinfo.info->animation = wal->animation;
        } else if (!extinfo.info) {
            extinfo.info = extended_texinfo_t{};
        }

        if (extinfo.info->contents.native & Q2_CONTENTS_TRANSLUCENT) {
            // remove TRANSLUCENT; it's only meant to be set by the compiler
            extinfo.info->contents.native &= ~Q2_CONTENTS_TRANSLUCENT;

            // but give us detail if we lack trans. this is likely what they intended
            if (!(extinfo.info->flags.native & (Q2_SURF_TRANS33 | Q2_SURF_TRANS66))) {
                extinfo.info->contents.native |= Q2_CONTENTS_DETAIL;

                logging::print("WARNING: face at line {}: swapped TRANSLUCENT for DETAIL\n", mapface.linenum);
            }
        }

        // This fixes a bug in some old maps.
        if ((extinfo.info->flags.native & (Q2_SURF_SKY | Q2_SURF_NODRAW)) == (Q2_SURF_SKY | Q2_SURF_NODRAW)) {
            extinfo.info->flags.native &= ~Q2_SURF_NODRAW;
            logging::print("WARNING: face at line {}: SKY | NODRAW mixed. Removing NODRAW.\n", mapface.linenum);
        }
    }

    tx->miptex = FindMiptex(mapface.texname.c_str(), extinfo.info);

    mapface.contents = {extinfo.info->contents};
    tx->flags = mapface.flags = {extinfo.info->flags};
    tx->value = mapface.value = extinfo.info->value;

    contentflags_t contents{mapface.contents};

    if (!contents.is_valid(qbsp_options.target_game, false)) {
        auto old_contents = contents;
        qbsp_options.target_game->contents_make_valid(contents);
        logging::print("WARNING: line {}: face has invalid contents {}, remapped to {}\n", mapface.linenum,
            old_contents.to_string(qbsp_options.target_game), contents.to_string(qbsp_options.target_game));
    }

    switch (tx_type) {
        case TX_QUARK_TYPE1:
        case TX_QUARK_TYPE2: SetTexinfo_QuArK(parser, planepts, tx_type, tx); break;
        case TX_VALVE_220: SetTexinfo_Valve220(axis, shift, scale, tx); break;
        case TX_BRUSHPRIM: {
            const auto &texture = map.load_image_meta(mapface.texname.c_str());
            const int32_t width = texture ? texture->width : 64;
            const int32_t height = texture ? texture->height : 64;

            SetTexinfo_BrushPrimitives(texMat, plane.normal, width, height, tx->vecs);
            break;
        }
        case TX_QUAKED:
        default: SetTexinfo_QuakeEd(plane, planepts, shift, rotate, scale, tx); break;
    }
}

bool mapface_t::set_planepts(const std::array<qvec3d, 3> &pts)
{
    planepts = pts;

    /* calculate the normal/dist plane equation */
    qvec3d ab = planepts[0] - planepts[1];
    qvec3d cb = planepts[2] - planepts[1];

    vec_t length;
    qvec3d normal = qv::normalize(qv::cross(ab, cb), length);
    vec_t dist = qv::dot(planepts[1], normal);
    
    planenum = map.add_or_find_plane({ normal, dist });

    return length >= NORMAL_EPSILON;
}

const texvecf &mapface_t::get_texvecs() const
{
    return map.mtexinfos.at(this->texinfo).vecs;
}

void mapface_t::set_texvecs(const texvecf &vecs)
{
    // start with a copy of the current texinfo structure
    maptexinfo_t texInfoNew = map.mtexinfos.at(this->texinfo);
    texInfoNew.outputnum = std::nullopt;
    texInfoNew.vecs = vecs;
    this->texinfo = FindTexinfo(texInfoNew);
}

const qbsp_plane_t &mapface_t::get_plane() const
{
    return map.get_plane(planenum);
}

bool IsValidTextureProjection(const qvec3f &faceNormal, const qvec3f &s_vec, const qvec3f &t_vec)
{
    // TODO: This doesn't match how light does it (TexSpaceToWorld)

    const qvec3f tex_normal = qv::normalize(qv::cross(s_vec, t_vec));

    for (int i = 0; i < 3; i++)
        if (std::isnan(tex_normal[i]))
            return false;

    const float cosangle = qv::dot(tex_normal, faceNormal);
    if (std::isnan(cosangle))
        return false;
    if (fabs(cosangle) < ZERO_EPSILON)
        return false;

    return true;
}

inline bool IsValidTextureProjection(const mapface_t &mapface, const maptexinfo_t *tx)
{
    return IsValidTextureProjection(mapface.get_plane().get_normal(), tx->vecs.row(0).xyz(), tx->vecs.row(1).xyz());
}

static void ValidateTextureProjection(mapface_t &mapface, maptexinfo_t *tx)
{
    if (!IsValidTextureProjection(mapface, tx)) {
        logging::print("WARNING: repairing invalid texture projection on line {} (\"{}\" near {} {} {})\n",
            mapface.linenum, mapface.texname, (int)mapface.planepts[0][0], (int)mapface.planepts[0][1],
            (int)mapface.planepts[0][2]);

        // Reset texturing to sensible defaults
        const std::array<vec_t, 2> shift{0, 0};
        const vec_t rotate = 0;
        const std::array<vec_t, 2> scale = {1, 1};
        SetTexinfo_QuakeEd(mapface.get_plane(), mapface.planepts, shift, rotate, scale, tx);

        Q_assert(IsValidTextureProjection(mapface, tx));
    }
}

static std::optional<mapface_t> ParseBrushFace(parser_t &parser, const mapbrush_t &brush, const mapentity_t &entity)
{
    std::array<qvec3d, 3> planepts;
    bool normal_ok;
    maptexinfo_t tx;
    int i, j;
    mapface_t face;

    face.linenum = parser.linenum;
    ParsePlaneDef(parser, planepts);

    normal_ok = face.set_planepts(planepts);

    ParseTextureDef(parser, face, brush, &tx, face.planepts, face.get_plane());

    if (!normal_ok) {
        logging::print("WARNING: line {}: Brush plane with no normal\n", parser.linenum);
        return std::nullopt;
    }

    // ericw -- round texture vector values that are within ZERO_EPSILON of integers,
    // to attempt to attempt to work around corrupted lightmap sizes in DarkPlaces
    // (it uses 32 bit precision in CalcSurfaceExtents)
    for (i = 0; i < 2; i++) {
        for (j = 0; j < 4; j++) {
            vec_t r = Q_rint(tx.vecs.at(i, j));
            if (fabs(tx.vecs.at(i, j) - r) < ZERO_EPSILON)
                tx.vecs.at(i, j) = r;
        }
    }

    ValidateTextureProjection(face, &tx);

    tx.flags = SurfFlagsForEntity(tx, entity);
    face.texinfo = FindTexinfo(tx);

    return face;
}


/*
================
CalculateBrushBounds
================
*/
inline void CalculateBrushBounds(mapbrush_t &ob)
{
    ob.bounds = {};

	for (size_t i = 0; i < ob.numfaces; i++) {
		const auto &plane = ob.face(i).get_plane();
		std::optional<winding_t> w = BaseWindingForPlane(plane);
		
        for (size_t j = 0; j < ob.numfaces && w; j++) {
			if (i == j) {
				continue;
            }
			if (ob.face(j).bevel) {
				continue;
            }
			const auto &plane = map.get_plane(ob.face(j).planenum ^ 1);
            w = w->clip(plane, 0)[SIDE_FRONT]; //CLIP_EPSILON);
		}

		if (w) {
            const_cast<mapface_t &>(ob.face(i)).winding = w.value();
			//side->visible = true;
			for (auto &p : w.value()) {
                ob.bounds += p;
            }
		}
	}

	for (size_t i = 0; i < 3; i++) {
		if (ob.bounds.mins()[0] < -qbsp_options.worldextent.value() || ob.bounds.maxs()[0] > qbsp_options.worldextent.value()) {
			logging::print("WARNING: entity xxx, brush yyy: bounds out of range\n");
        }
		if (ob.bounds.mins()[0] > qbsp_options.worldextent.value() || ob.bounds.maxs()[0] < -qbsp_options.worldextent.value()) {
			logging::print("WARNING: entity xxx, brush yyy: no visible sides on brush\n");
        }
	}
}


/*
=================
AddBrushBevels

Adds any additional planes necessary to allow the brush to be expanded
against axial bounding boxes
=================
*/
inline void AddBrushBevels(mapentity_t &e, mapbrush_t &b)
{
	//
	// add the axial planes
	//
	int32_t order = 0;
	for (int32_t axis = 0; axis < 3; axis++) {
		for (int32_t dir = -1; dir <= 1; dir += 2, order++) {
			// see if the plane is already present
            int32_t i;

			for (i = 0; i < b.numfaces; i++) {
                auto &s = b.face(i);

				if (map.get_plane(s.planenum).get_normal()[axis] == dir) {
					break;
                }
			}

			if (i == b.numfaces) {
                // add a new side
                b.numfaces++;
                mapface_t &s = map.faces.emplace_back();
                qplane3d plane{};
				plane.normal[axis] = dir;
				if (dir == 1) {
					plane.dist = b.bounds.maxs()[axis];
                } else {
					plane.dist = -b.bounds.mins()[axis];
                }
				s.planenum = map.add_or_find_plane(plane);
				s.texinfo = b.face(0).texinfo;
				s.contents = b.face(0).contents;
                // fixme: why did we need to store all this stuff again, isn't
                // it in texinfo?
                s.raw_info = b.face(0).raw_info;
                s.flags = b.face(0).flags;
                s.texname = b.face(0).texname;
                s.value = b.face(0).value;

				s.bevel = true;
				e.numboxbevels++;
			}

			// if the plane is not in it canonical order, swap it
			if (i != order) {
                std::swap(const_cast<mapface_t &>(b.face(order)), const_cast<mapface_t &>(b.face(i)));
			}
		}
	}

	//
	// add the edge bevels
	//
	if (b.numfaces == 6) {
		return;		// pure axial
    }

	// test the non-axial plane edges
	for (size_t i = 6; i < b.numfaces; i++) {
		auto &s = b.face(i);
		auto &w = s.winding;

        if (!w) {
			continue;
        }

		for (size_t j = 0; j < w.size(); j++) {
			size_t k = (j + 1) % w.size();
			qvec3d vec = w[j] - w[k];

            if (qv::normalizeInPlace(vec) < 0.5) {
				continue;
            }

			vec = qv::Snap(vec);

			for (k = 0 ; k < 3; k++) {
				if (vec[k] == -1 || vec[k] == 1) {
					break;	// axial
                }
            }

			if (k != 3) {
				continue;	// only test non-axial edges
            }

			// try the six possible slanted axials from this edge
			for (size_t axis = 0; axis < 3; axis++) {
				for (size_t dir = -1; dir <= 1; dir += 2) {
					// construct a plane
                    qplane3d plane {};
                    plane.normal[axis] = dir;
					plane.normal = qv::cross(vec, plane.normal);
					if (qv::normalizeInPlace(plane.normal) < 0.5) {
						continue;
                    }
					plane.dist = qv::dot(w[j], plane.normal);

					// if all the points on all the sides are
					// behind this plane, it is a proper edge bevel
					for (k = 0; k < b.numfaces; k++) {
						// if this plane has allready been used, skip it
						if (qv::epsilonEqual(b.face(k).get_plane(), plane)) {
							break;
                        }

						auto &w2 = b.face(k).winding;

                        if (!w2) {
							continue;
                        }

                        size_t l = 0;
						for (; l < w2.size(); l++) {
							vec_t d = qv::dot(w2[l], plane.normal) - plane.dist;
							if (d > 0.1) {
								break;	// point in front
                            }
						}

						if (l != w2.size()) {
							break;
                        }
					}

					if (k != b.numfaces) {
						continue;	// wasn't part of the outer hull
                    }

					// add this plane
                    b.numfaces++;
                    mapface_t &s = map.faces.emplace_back();
				    s.planenum = map.add_or_find_plane(plane);
				    s.texinfo = b.face(0).texinfo;
				    s.contents = b.face(0).contents;
                    // fixme: why did we need to store all this stuff again, isn't
                    // it in texinfo?
                    s.raw_info = b.face(0).raw_info;
                    s.flags = b.face(0).flags;
                    s.texname = b.face(0).texname;
                    s.value = b.face(0).value;
					s.bevel = true;
					e.numedgebevels++;
				}
			}
		}
	}
}

mapbrush_t ParseBrush(parser_t &parser, mapentity_t &entity)
{
    mapbrush_t brush;

    // ericw -- brush primitives
    if (!parser.parse_token(PARSE_PEEK))
        FError("Unexpected EOF after { beginning brush");

    if (parser.token == "(") {
        brush.format = brushformat_t::NORMAL;
    } else {
        parser.parse_token();
        brush.format = brushformat_t::BRUSH_PRIMITIVES;

        // optional
        if (parser.token == "brushDef") {
            if (!parser.parse_token())
                FError("Brush primitives: unexpected EOF (nothing after brushDef)");
        }

        // mandatory
        if (parser.token != "{")
            FError("Brush primitives: expected second { at beginning of brush, got \"{}\"", parser.token);
    }
    // ericw -- end brush primitives

    while (parser.parse_token()) {
        if (parser.token == "}")
            break;

        std::optional<mapface_t> face = ParseBrushFace(parser, brush, entity);

        if (!face) {
            continue;
        }

        /* Check for duplicate planes */
        bool discardFace = false;
        for (int i = 0; i < brush.numfaces; i++) {
            const mapface_t &check = brush.face(i);
            if (qv::epsilonEqual(check.get_plane(), face->get_plane())) {
                logging::print("line {}: Brush with duplicate plane\n", parser.linenum);
                discardFace = true;
                continue;
            }
            if (qv::epsilonEqual(-check.get_plane(), face->get_plane())) {
                /* FIXME - this is actually an invalid brush */
                logging::print("line {}: Brush with duplicate plane\n", parser.linenum);
                continue;
            }
        }

        if (discardFace) {
            continue;
        }

        /* Save the face, update progress */
        if (!brush.numfaces) {
            brush.firstface = map.faces.size();
        }

        brush.numfaces++;
        map.faces.emplace_back(std::move(face.value()));
    }

    // ericw -- brush primitives - there should be another closing }
    if (brush.format == brushformat_t::BRUSH_PRIMITIVES) {
        if (!parser.parse_token())
            FError("Brush primitives: unexpected EOF (no closing brace)");
        if (parser.token != "}")
            FError("Brush primitives: Expected }, got: {}", parser.token);
    }
    // ericw -- end brush primitives

    // calculate brush bounds
    CalculateBrushBounds(brush);

    // add the brush bevels
    AddBrushBevels(entity, brush);

    return brush;
}

bool ParseEntity(parser_t &parser, mapentity_t *entity)
{
    if (!parser.parse_token())
        return false;

    if (parser.token != "{")
        FError("line {}: Invalid entity format, { not found", parser.linenum);

    entity->nummapbrushes = 0;
    do {
        if (!parser.parse_token())
            FError("Unexpected EOF (no closing brace)");
        if (parser.token == "}")
            break;
        else if (parser.token == "{") {
            // once we run into the first brush, set up textures state.
            EnsureTexturesLoaded();

            mapbrush_t brush = ParseBrush(parser, *entity);

            if (!entity->nummapbrushes)
                entity->firstmapbrush = map.brushes.size();
            entity->nummapbrushes++;

            map.brushes.push_back(brush);
        } else
            ParseEpair(parser, entity);
    } while (1);

    // replace aliases
    auto alias_it = qbsp_options.loaded_entity_defs.find(entity->epairs.get("classname"));

    if (alias_it != qbsp_options.loaded_entity_defs.end()) {
        for (auto &pair : alias_it->second) {
            if (pair.first == "classname" || !entity->epairs.has(pair.first)) {
                entity->epairs.set(pair.first, pair.second);
            }
        }
    }

    return true;
}

static void ScaleMapFace(mapface_t *face, const qvec3d &scale)
{
    const qmat3x3d scaleM{// column-major...
        scale[0], 0.0, 0.0, 0.0, scale[1], 0.0, 0.0, 0.0, scale[2]};

    std::array<qvec3d, 3> new_planepts;
    for (int i = 0; i < 3; i++) {
        new_planepts[i] = scaleM * face->planepts[i];
    }

    face->set_planepts(new_planepts);

    // update texinfo

    const qmat3x3d inversescaleM{// column-major...
        1 / scale[0], 0.0, 0.0, 0.0, 1 / scale[1], 0.0, 0.0, 0.0, 1 / scale[2]};

    const auto &texvecs = face->get_texvecs();
    texvecf newtexvecs;

    for (int i = 0; i < 2; i++) {
        const qvec4f in = texvecs.row(i);
        const qvec3f in_first3(in);

        const qvec3f out_first3 = inversescaleM * in_first3;
        newtexvecs.set_row(i, {out_first3[0], out_first3[1], out_first3[2], in[3]});
    }

    face->set_texvecs(newtexvecs);
}

static void RotateMapFace(mapface_t *face, const qvec3d &angles)
{
    const double pitch = DEG2RAD(angles[0]);
    const double yaw = DEG2RAD(angles[1]);
    const double roll = DEG2RAD(angles[2]);

    qmat3x3d rotation = RotateAboutZ(yaw) * RotateAboutY(pitch) * RotateAboutX(roll);

    std::array<qvec3d, 3> new_planepts;
    for (int i = 0; i < 3; i++) {
        new_planepts[i] = rotation * face->planepts[i];
    }

    face->set_planepts(new_planepts);

    // update texinfo

    const auto &texvecs = face->get_texvecs();
    texvecf newtexvecs;

    for (int i = 0; i < 2; i++) {
        const qvec4f in = texvecs.row(i);
        const qvec3f in_first3(in);

        const qvec3f out_first3 = rotation * in_first3;
        newtexvecs.set_row(i, {out_first3[0], out_first3[1], out_first3[2], in[3]});
    }

    face->set_texvecs(newtexvecs);
}

static void TranslateMapFace(mapface_t *face, const qvec3d &offset)
{
    std::array<qvec3d, 3> new_planepts;
    for (int i = 0; i < 3; i++) {
        new_planepts[i] = face->planepts[i] + offset;
    }

    face->set_planepts(new_planepts);

    // update texinfo

    const auto &texvecs = face->get_texvecs();
    texvecf newtexvecs;

    for (int i = 0; i < 2; i++) {
        qvec4f out = texvecs.row(i);
        // CHECK: precision loss here?
        out[3] += qv::dot(qvec3f(out), qvec3f(offset) * -1.0f);
        newtexvecs.set_row(i, {out[0], out[1], out[2], out[3]});
    }

    face->set_texvecs(newtexvecs);
}

/**
 * Loads an external .map file.
 *
 * The loaded brushes/planes/etc. will be stored in the global mapdata_t.
 */
static mapentity_t LoadExternalMap(const std::string &filename)
{
    mapentity_t dest{};

    auto file = fs::load(filename);

    if (!file) {
        FError("Couldn't load external map file \"{}\".\n", filename);
    }

    parser_t parser(file->data(), file->size());

    // parse the worldspawn
    if (!ParseEntity(parser, &dest)) {
        FError("'{}': Couldn't parse worldspawn entity\n", filename);
    }
    const std::string &classname = dest.epairs.get("classname");
    if (Q_strcasecmp("worldspawn", classname)) {
        FError("'{}': Expected first entity to be worldspawn, got: '{}'\n", filename, classname);
    }

    // parse any subsequent entities, move any brushes to worldspawn
    mapentity_t dummy{};
    while (ParseEntity(parser, &dummy)) {
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
        FError("Expected at least one brush for external map {}\n", filename);
    }

    logging::print(
        logging::flag::STAT, "     {}: '{}': Loaded {} mapbrushes.\n", __func__, filename, dest.nummapbrushes);

    return dest;
}

void ProcessExternalMapEntity(mapentity_t *entity)
{
    Q_assert(!qbsp_options.onlyents.value());

    const std::string &classname = entity->epairs.get("classname");
    if (Q_strcasecmp(classname, "misc_external_map"))
        return;

    const std::string &file = entity->epairs.get("_external_map");
    const std::string &new_classname = entity->epairs.get("_external_map_classname");

    // FIXME: throw specific error message instead? this might be confusing for mappers
    Q_assert(!file.empty());
    Q_assert(!new_classname.empty());

    Q_assert(0 == entity->nummapbrushes); // misc_external_map must be a point entity

    const mapentity_t external_worldspawn = LoadExternalMap(file);

    // copy the brushes into the target
    entity->firstmapbrush = external_worldspawn.firstmapbrush;
    entity->nummapbrushes = external_worldspawn.nummapbrushes;

    qvec3d origin;
    entity->epairs.get_vector("origin", origin);

    qvec3d angles;
    entity->epairs.get_vector("_external_map_angles", angles);
    if (qv::epsilonEmpty(angles, EQUAL_EPSILON)) {
        angles[1] = entity->epairs.get_float("_external_map_angle");
    }

    qvec3d scale;
    int ncomps = entity->epairs.get_vector("_external_map_scale", scale);
    if (ncomps < 3) {
        if (scale[0] == 0.0) {
            scale = 1;
        } else {
            scale = scale[0];
        }
    }

    for (int i = 0; i < entity->nummapbrushes; i++) {
        mapbrush_t *brush = const_cast<mapbrush_t *>(&entity->mapbrush(i));

        for (int j = 0; j < brush->numfaces; j++) {
            mapface_t *face = const_cast<mapface_t *>(&brush->face(j));

            ScaleMapFace(face, scale);
            RotateMapFace(face, angles);
            TranslateMapFace(face, origin);
        }
    }

    entity->epairs.set("classname", new_classname);
    // FIXME: Should really just delete the origin key?
    entity->epairs.set("origin", "0 0 0");
}

void ProcessAreaPortal(mapentity_t *entity)
{
    Q_assert(!qbsp_options.onlyents.value());

    const std::string &classname = entity->epairs.get("classname");

    if (Q_strcasecmp(classname, "func_areaportal"))
        return;

    // areaportal entities move their brushes, but don't eliminate
    // the entity
    // FIXME: print entity ID/line number
    if (entity->nummapbrushes != 1)
        FError("func_areaportal can only be a single brush");

    for (size_t i = entity->firstmapbrush; i < entity->firstmapbrush + entity->nummapbrushes; i++) {
        map.brushes[i].contents = Q2_CONTENTS_AREAPORTAL;

        for (size_t f = map.brushes[i].firstface; f < map.brushes[i].firstface + map.brushes[i].numfaces; f++) {
            map.faces[f].contents.native = Q2_CONTENTS_AREAPORTAL;
            map.faces[f].texinfo = map.skip_texinfo;
        }
    }
    entity->areaportalnum = ++map.numareaportals;
    // set the portal number as "style"
    entity->epairs.set("style", std::to_string(map.numareaportals));
}

/*
 * Special world entities are entities which have their brushes added to the
 * world before being removed from the map.
 */
bool IsWorldBrushEntity(const mapentity_t *entity)
{
    const std::string &classname = entity->epairs.get("classname");

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
 * Some games need special entities that are merged into the world, but not
 * removed from the map entirely.
 */
bool IsNonRemoveWorldBrushEntity(const mapentity_t *entity)
{
    const std::string &classname = entity->epairs.get("classname");

    if (!Q_strcasecmp(classname, "func_areaportal"))
        return true;

    return false;
}

void LoadMapFile(void)
{
    logging::funcheader();

    {
        auto file = fs::load(qbsp_options.map_path);

        if (!file) {
            FError("Couldn't load map file \"{}\".\n", qbsp_options.map_path);
            return;
        }

        parser_t parser(file->data(), file->size());

        for (int i = 0;; i++) {
            mapentity_t &entity = map.entities.emplace_back();

            if (!ParseEntity(parser, &entity)) {
                break;
            }
        }
        // Remove dummy entity inserted above
        assert(!map.entities.back().epairs.size());
        assert(map.entities.back().brushes.empty());
        map.entities.pop_back();
    }

    // -add function
    if (!qbsp_options.add.value().empty()) {
        auto file = fs::load(qbsp_options.add.value());

        if (!file) {
            FError("Couldn't load map file \"{}\".\n", qbsp_options.add.value());
            return;
        }

        parser_t parser(file->data(), file->size());

        for (int i = 0;; i++) {
            mapentity_t &entity = map.entities.emplace_back();

            if (!ParseEntity(parser, &entity)) {
                break;
            }

            if (entity.epairs.get("classname") == "worldspawn") {
                // The easiest way to get the additional map's worldspawn brushes
                // into the base map's is to rename the additional map's worldspawn classname to func_group
                entity.epairs.set("classname", "func_group");
            }
        }
        // Remove dummy entity inserted above
        assert(!map.entities.back().epairs.size());
        assert(map.entities.back().brushes.empty());
        map.entities.pop_back();
    }

    logging::print(logging::flag::STAT, "     {:8} faces\n", map.faces.size());
    logging::print(logging::flag::STAT, "     {:8} brushes\n", map.brushes.size());
    logging::print(logging::flag::STAT, "     {:8} entities\n", map.entities.size());
    logging::print(logging::flag::STAT, "     {:8} unique texnames\n", map.miptex.size());
    logging::print(logging::flag::STAT, "     {:8} texinfo\n", map.mtexinfos.size());
    logging::print(logging::flag::STAT, "     {:8} unique planes\n", map.planes.size());
    logging::print(logging::flag::STAT, "\n");

    if (qbsp_options.expand.value()) {
        TestExpandBrushes(map.world_entity());
    }
}

static texdef_valve_t TexDef_BSPToValve(const texvecf &in_vecs)
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

    for (int i = 0; i < 2; i++) {
        qvec3d axis = in_vecs.row(i).xyz();
        const vec_t length = qv::normalizeInPlace(axis);
        // avoid division by 0
        if (length != 0.0) {
            res.scale[i] = 1.0 / length;
        } else {
            res.scale[i] = 0.0;
        }
        res.shift[i] = in_vecs.at(i, 3);
        res.axis.set_row(i, axis);
    }

    return res;
}

static void fprintDoubleAndSpc(std::ofstream &f, double v)
{
    int rounded = rint(v);
    if (static_cast<double>(rounded) == v) {
        fmt::print(f, "{} ", rounded);
    } else if (std::isfinite(v)) {
        fmt::print(f, "{:0.17} ", v);
    } else {
        printf("WARNING: suppressing nan or infinity\n");
        f << "0 ";
    }
}

static void ConvertMapFace(std::ofstream &f, const mapface_t &mapface, const conversion_t format)
{
    const auto &texture = map.load_image_meta(mapface.texname.c_str());

    const maptexinfo_t &texinfo = map.mtexinfos.at(mapface.texinfo);

    // Write plane points
    for (int i = 0; i < 3; i++) {
        f << " ( ";
        for (int j = 0; j < 3; j++) {
            fprintDoubleAndSpc(f, mapface.planepts[i][j]);
        }
        f << ") ";
    }

    switch (format) {
        case conversion_t::quake:
        case conversion_t::quake2: {
            const texdef_quake_ed_t quakeed =
                TexDef_BSPToQuakeEd(mapface.get_plane(), texture, texinfo.vecs, mapface.planepts);

            fmt::print(f, "{} ", mapface.texname);
            fprintDoubleAndSpc(f, quakeed.shift[0]);
            fprintDoubleAndSpc(f, quakeed.shift[1]);
            fprintDoubleAndSpc(f, quakeed.rotate);
            fprintDoubleAndSpc(f, quakeed.scale[0]);
            fprintDoubleAndSpc(f, quakeed.scale[1]);

            if (mapface.raw_info.has_value()) {
                f << mapface.raw_info->contents.native << " " << mapface.raw_info->flags.native << " "
                  << mapface.raw_info->value;
            }

            break;
        }
        case conversion_t::valve: {
            const texdef_valve_t valve = TexDef_BSPToValve(texinfo.vecs);

            fmt::print(f, "{} [ ", mapface.texname);
            fprintDoubleAndSpc(f, valve.axis.at(0, 0));
            fprintDoubleAndSpc(f, valve.axis.at(0, 1));
            fprintDoubleAndSpc(f, valve.axis.at(0, 2));
            fprintDoubleAndSpc(f, valve.shift[0]);
            f << "] [ ";
            fprintDoubleAndSpc(f, valve.axis.at(1, 0));
            fprintDoubleAndSpc(f, valve.axis.at(1, 1));
            fprintDoubleAndSpc(f, valve.axis.at(1, 2));
            fprintDoubleAndSpc(f, valve.shift[1]);
            f << "] 0 ";
            fprintDoubleAndSpc(f, valve.scale[0]);
            fprintDoubleAndSpc(f, valve.scale[1]);

            if (mapface.raw_info.has_value()) {
                f << mapface.raw_info->contents.native << " " << mapface.raw_info->flags.native << " "
                  << mapface.raw_info->value;
            }

            break;
        }
        case conversion_t::bp: {
            int texSize[2];
            texSize[0] = texture ? texture->width : 64;
            texSize[1] = texture ? texture->height : 64;

            const texdef_brush_primitives_t bp = TexDef_BSPToBrushPrimitives(mapface.get_plane(), texSize, texinfo.vecs);
            f << "( ( ";
            fprintDoubleAndSpc(f, bp.at(0, 0));
            fprintDoubleAndSpc(f, bp.at(0, 1));
            fprintDoubleAndSpc(f, bp.at(0, 2));
            f << ") ( ";
            fprintDoubleAndSpc(f, bp.at(1, 0));
            fprintDoubleAndSpc(f, bp.at(1, 1));
            fprintDoubleAndSpc(f, bp.at(1, 2));

            // N.B.: always print the Q2/Q3 flags
            fmt::print(f, ") ) {} ", mapface.texname);

            if (mapface.raw_info.has_value()) {
                f << mapface.raw_info->contents.native << " " << mapface.raw_info->flags.native << " "
                  << mapface.raw_info->value;
            } else {
                f << "0 0 0";
            }

            break;
        }
        default: FError("Internal error: unknown texcoord_style_t\n");
    }

    f << '\n';
}

static void ConvertMapBrush(std::ofstream &f, const mapbrush_t &mapbrush, const conversion_t format)
{
    f << "{\n";
    if (format == conversion_t::bp) {
        f << "brushDef\n";
        f << "{\n";
    }
    for (int i = 0; i < mapbrush.numfaces; i++) {
        ConvertMapFace(f, mapbrush.face(i), format);
    }
    if (format == conversion_t::bp) {
        f << "}\n";
    }
    f << "}\n";
}

static void ConvertEntity(std::ofstream &f, const mapentity_t *entity, const conversion_t format)
{
    f << "{\n";

    for (const auto &[key, value] : entity->epairs) {
        fmt::print(f, "\"{}\" \"{}\"\n", key, value);
    }

    for (int i = 0; i < entity->nummapbrushes; i++) {
        ConvertMapBrush(f, entity->mapbrush(i), format);
    }
    f << "}\n";
}

void ConvertMapFile(void)
{
    logging::funcheader();

    std::string append;

    switch (qbsp_options.convertmapformat.value()) {
        case conversion_t::quake: append = "-quake"; break;
        case conversion_t::quake2: append = "-quake2"; break;
        case conversion_t::valve: append = "-valve"; break;
        case conversion_t::bp: append = "-bp"; break;
        default: FError("Internal error: unknown conversion_t\n");
    }

    fs::path filename = qbsp_options.bsp_path;
    filename.replace_filename(qbsp_options.bsp_path.stem().string() + append).replace_extension(".map");

    std::ofstream f(filename);

    if (!f)
        FError("Couldn't open file\n");

    for (const mapentity_t &entity : map.entities) {
        ConvertEntity(f, &entity, qbsp_options.convertmapformat.value());
    }

    logging::print("Conversion saved to {}\n", filename);

    qbsp_options.fVerbose = false;
}

void PrintEntity(const mapentity_t *entity)
{
    for (auto &epair : entity->epairs)
        logging::print(logging::flag::STAT, "     {:20} : {}\n", epair.first, epair.second);
}

void WriteEntitiesToString()
{
    for (auto &entity : map.entities) {
        /* Check if entity needs to be removed */
        if (!entity.epairs.size() || IsWorldBrushEntity(&entity)) {
            continue;
        }

        map.bsp.dentdata += "{\n";

        for (auto &ep : entity.epairs) {

            if (ep.first.size() >= qbsp_options.target_game->max_entity_key - 1) {
                logging::print("WARNING: {} at {} has long key {} (length {} >= {})\n", entity.epairs.get("classname"),
                    entity.origin, ep.first, ep.first.size(), qbsp_options.target_game->max_entity_key - 1);
            }

            if (ep.second.size() >= qbsp_options.target_game->max_entity_value - 1) {
                logging::print("WARNING: {} at {} has long value for key {} (length {} >= {})\n",
                    entity.epairs.get("classname"), entity.origin, ep.first, ep.second.size(),
                    qbsp_options.target_game->max_entity_value - 1);
            }

            fmt::format_to(std::back_inserter(map.bsp.dentdata), "\"{}\" \"{}\"\n", ep.first, ep.second);
        }

        map.bsp.dentdata += "}\n";
    }
}

//====================================================================

inline std::optional<qvec3d> GetIntersection(const qplane3d &p1, const qplane3d &p2, const qplane3d &p3)
{
    const vec_t denom = qv::dot(p1.normal, qv::cross(p2.normal, p3.normal));

    if (denom == 0.f) {
        return std::nullopt;
    }

    return (qv::cross(p2.normal, p3.normal) * p1.dist - qv::cross(p3.normal, p1.normal) * -p2.dist -
               qv::cross(p1.normal, p2.normal) * -p3.dist) /
           denom;
}

/*
=================
GetBrushExtents
=================
*/
inline vec_t GetBrushExtents(const mapbrush_t &hullbrush)
{
    vec_t extents = -std::numeric_limits<vec_t>::infinity();

    for (int32_t i = 0; i < hullbrush.numfaces - 2; i++) {
        for (int32_t j = i; j < hullbrush.numfaces - 1; j++) {
            for (int32_t k = j; k < hullbrush.numfaces; k++) {
                if (i == j || j == k || k == i) {
                    continue;
                }

                auto &fi = hullbrush.face(i);
                auto &fj = hullbrush.face(j);
                auto &fk = hullbrush.face(k);

                bool legal = true;
                auto vertex = GetIntersection(fi.get_plane(), fj.get_plane(), fk.get_plane());

                if (!vertex) {
                    continue;
                }

                for (int32_t m = 0; m < hullbrush.numfaces; m++) {
                    if (hullbrush.face(m).get_plane().distance_to(*vertex) > NORMAL_EPSILON) {
                        legal = false;
                        break;
                    }
                }

                if (legal) {

                    for (auto &p : *vertex) {
                        extents = max(extents, fabs(p));
                    }
                }
            }
        }
    }

    return extents;
}

#include "tbb/parallel_for_each.h"
#include <atomic>

void CalculateWorldExtent(void)
{
    std::atomic<vec_t> extents = -std::numeric_limits<vec_t>::infinity();

    tbb::parallel_for_each(map.brushes, [&](const mapbrush_t &brush) {
        const vec_t brushExtents = max(extents.load(), GetBrushExtents(brush));
        vec_t currentExtents = extents;
        while (currentExtents < brushExtents && !extents.compare_exchange_weak(currentExtents, brushExtents))
            ;
    });

    vec_t hull_extents = 0;

    for (auto &hull : qbsp_options.target_game->get_hull_sizes()) {
        for (auto &v : hull.size()) {
            hull_extents = max(hull_extents, fabs(v));
        }
    }

    qbsp_options.worldextent.setValue((extents + hull_extents) * 2, settings::source::GAME_TARGET);
}

/*
==================
WriteBspBrushMap

from q3map
==================
*/
void WriteBspBrushMap(const fs::path &name, const std::vector<std::unique_ptr<bspbrush_t>> &list)
{
    std::shared_lock lock(map_planes_lock);

    logging::print("writing {}\n", name);
    std::ofstream f(name);

    if (!f)
        FError("Can't write {}", name);

    fmt::print(f, "{{\n\"classname\" \"worldspawn\"\n");

    for (auto &brush : list) {
        fmt::print(f, "{{\n");
        for (auto &face : brush->sides) {
            // FIXME: Factor out this mess
            winding_t w = BaseWindingForPlane(face.get_plane());

            fmt::print(f, "( {} ) ", w[0]);
            fmt::print(f, "( {} ) ", w[1]);
            fmt::print(f, "( {} ) ", w[2]);

            if (face.visible) {
                fmt::print(f, "skip 0 0 0 1 1\n");
            } else {
                fmt::print(f, "nonvisible 0 0 0 1 1\n");
            }
        }

        fmt::print(f, "}}\n");
    }

    fmt::print(f, "}}\n");
}

/*
================
TestExpandBrushes

Expands all the brush planes and saves a new map out to
allow visual inspection of the clipping bevels

from q3map
================
*/
static void TestExpandBrushes(const mapentity_t *src)
{
    std::vector<std::unique_ptr<bspbrush_t>> hull1brushes;

    for (int i = 0; i < src->nummapbrushes; i++) {
        const mapbrush_t *mapbrush = &src->mapbrush(i);
        std::optional<bspbrush_t> hull1brush = LoadBrush(src, mapbrush, {CONTENTS_SOLID}, {}, rotation_t::none,
            qbsp_options.target_game->id == GAME_QUAKE_II ? HULL_COLLISION : 1);

        if (hull1brush) {
            hull1brushes.emplace_back(std::make_unique<bspbrush_t>(std::move(*hull1brush)));
        }
    }

    WriteBspBrushMap("expanded.map", hull1brushes);
}
