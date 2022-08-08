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

#include <cstring>
#include <list>

#include <qbsp/brush.hh>
#include <qbsp/csg.hh>
#include <qbsp/map.hh>
#include <qbsp/qbsp.hh>

bool bspbrush_t_less::operator()(const bspbrush_t *a, const bspbrush_t *b) const
{
    return a->file_order < b->file_order;
}

const maptexinfo_t &side_t::get_texinfo() const
{
    return map.mtexinfos[this->texinfo];
}

const qbsp_plane_t &side_t::get_plane() const
{
    return map.get_plane(planenum);
}

const qbsp_plane_t &side_t::get_positive_plane() const
{
    auto &p = map.get_plane(planenum & ~1);
    Q_assert(p.get_normal()[(int) p.get_type() % 3] > 0);
    return p;
}

std::unique_ptr<bspbrush_t> bspbrush_t::copy_unique() const
{
    return std::make_unique<bspbrush_t>(*this);
}

/*
=================
CheckFace

Note: this will not catch 0 area polygons
=================
*/
static void CheckFace(side_t *face, const mapface_t &sourceface)
{
    if (face->w.size() < 3) {
        if (face->w.size() == 2) {
            logging::print(
                "WARNING: {}: partially clipped into degenerate polygon @ ({}) - ({})\n", sourceface.line, face->w[0], face->w[1]);
        } else if (face->w.size() == 1) {
            logging::print("WARNING: {}: partially clipped into degenerate polygon @ ({})\n", sourceface.line, face->w[0]);
        } else {
            logging::print("WARNING: {}: completely clipped away\n", sourceface.line);
        }

        face->w.clear();
        return;
    }

    const qbsp_plane_t &plane = face->get_plane();
    qvec3d facenormal = plane.get_normal();

    for (size_t i = 0; i < face->w.size(); i++) {
        const qvec3d &p1 = face->w[i];
        const qvec3d &p2 = face->w[(i + 1) % face->w.size()];

        for (auto &v : p1) {
            if (fabs(v) > qbsp_options.worldextent.value()) {
                // this is fatal because a point should never lay outside the world
                FError("{}: coordinate out of range ({})\n", sourceface.line, v);
            }
        }

        /* check the point is on the face plane */
        {
            vec_t dist = face->get_plane().distance_to(p1);
            if (fabs(dist) > qbsp_options.epsilon.value()) {
                logging::print("WARNING: {}: Point ({:.3} {:.3} {:.3}) off plane by {:2.4}\n", sourceface.line,
                    p1[0], p1[1], p1[2], dist);
            }
        }

        /* check the edge isn't degenerate */
        qvec3d edgevec = p2 - p1;
        vec_t length = qv::length(edgevec);
        if (length < qbsp_options.epsilon.value()) {
            logging::print("WARNING: {}: Healing degenerate edge ({}) at ({:.3f} {:.3} {:.3})\n",
                sourceface.line, length, p1[0], p1[1], p1[2]);
            for (size_t j = i + 1; j < face->w.size(); j++)
                face->w[j - 1] = face->w[j];
            face->w.resize(face->w.size() - 1);
            CheckFace(face, sourceface);
            break;
        }

        qvec3d edgenormal = qv::normalize(qv::cross(facenormal, edgevec));
        vec_t edgedist = qv::dot(p1, edgenormal);
        edgedist += qbsp_options.epsilon.value();

        /* all other points must be on front side */
        for (size_t j = 0; j < face->w.size(); j++) {
            if (j == i)
                continue;
            vec_t dist = qv::dot(face->w[j], edgenormal);
            if (dist > edgedist) {
                logging::print("WARNING: {}: Found a non-convex face (error size {}, point: {})\n",
                    sourceface.line, dist - edgedist, face->w[j]);
                face->w.clear();
                return;
            }
        }
    }
}

/*
=============================================================================

                        TURN BRUSHES INTO GROUPS OF FACES

=============================================================================
*/

/*
=================
FindTargetEntity
=================
*/
static const mapentity_t *FindTargetEntity(const std::string &target)
{
    for (const auto &entity : map.entities) {
        const std::string &name = entity.epairs.get("targetname");
        if (!string_iequals(target, name))
            return &entity;
    }

    return nullptr;
}

/*
=================
FixRotateOrigin
=================
*/
qvec3d FixRotateOrigin(mapentity_t *entity)
{
    const std::string &search = entity->epairs.get("target");
    const mapentity_t *target = nullptr;

    if (!search.empty()) {
        target = FindTargetEntity(search);
    }

    qvec3d offset;

    if (target) {
        target->epairs.get_vector("origin", offset);
    } else {
        logging::print("WARNING: No target for rotation entity \"{}\"", entity->epairs.get("classname"));
        offset = {};
    }

    entity->epairs.set("origin", qv::to_string(offset));
    return offset;
}

static bool MapBrush_IsHint(const mapbrush_t &brush)
{
    for (auto &f : brush.faces) {
        if (f.flags.is_hint)
            return true;
    }

    return false;
}

#if 0
        if (hullnum <= 0 && Brush_IsHint(*hullbrush)) {
            /* Don't generate hintskip faces */
            const maptexinfo_t &texinfo = map.mtexinfos.at(mapface.texinfo);

            if (qbsp_options.target_game->texinfo_is_hintskip(texinfo.flags, map.miptexTextureName(texinfo.miptex)))
                continue;
        }
#endif

//============================================================================

/*
==================
CreateBrushWindings

Create all of the windings for the specified brush, and
calculate its bounds.
==================
*/
bool CreateBrushWindings(bspbrush_t *brush)
{
    std::optional<winding_t> w;

    for (int i = 0; i < brush->sides.size(); i++) {
        side_t *side = &brush->sides[i];
        w = BaseWindingForPlane(side->get_plane());
        for (int j = 0; j < brush->sides.size() && w; j++) {
            if (i == j)
                continue;
            if (brush->sides[j].bevel)
                continue;
            const qplane3d &plane = map.planes[brush->sides[j].planenum ^ 1];
            w = w->clip(plane, qbsp_options.epsilon.value(), false)[SIDE_FRONT]; // CLIP_EPSILON);
        }

        if (w) {
            for (auto &p : *w) {
                for (auto &v : p) {
                    if (fabs(v) > qbsp_options.worldextent.value()) {
                        logging::print("WARNING: {}: invalid winding point\n", brush->mapbrush ? brush->mapbrush->line : parser_source_location{});
                        w = std::nullopt;
                    }
                }
            }

            side->w = *w;
        } else {
            side->w.clear();
        }
    }

    return brush->update_bounds(true);
}

/*
===============
LoadBrush

Converts a mapbrush to a bsp brush
===============
*/
std::optional<bspbrush_t> LoadBrush(const mapentity_t *src, const mapbrush_t *mapbrush, const contentflags_t &contents,
    const int hullnum)
{
    // create the brush
    bspbrush_t brush{};
    brush.contents = contents;
    brush.sides.reserve(mapbrush->faces.size());
    brush.mapbrush = mapbrush;

    for (size_t i = 0; i < mapbrush->faces.size(); i++) {
        auto &src = mapbrush->faces[i];

        // don't add bevels for the point hull
        if (hullnum <= 0 && src.bevel) {
            continue;
        }

        auto &dst = brush.sides.emplace_back();

        dst.texinfo = hullnum > 0 ? 0 : src.texinfo;
        dst.planenum = src.planenum;
        dst.bevel = src.bevel;
        dst.source = &src;
    }

    // expand the brushes for the hull
    if (hullnum > 0) {
        auto &hulls = qbsp_options.target_game->get_hull_sizes();
        Q_assert(hullnum < hulls.size());
        auto &hull = *(hulls.begin() + hullnum);

        for (auto &mapface : brush.sides) {
            if (mapface.get_texinfo().flags.no_expand) {
                continue;
            }
            qvec3d corner{};
            for (int32_t x = 0; x < 3; x++) {
                if (mapface.get_plane().get_normal()[x] > 0) {
                    corner[x] = hull[1][x];
                } else if (mapface.get_plane().get_normal()[x] < 0) {
                    corner[x] = hull[0][x];
                }
            }
            qplane3d plane = mapface.get_plane();
            plane.dist += qv::dot(corner, plane.normal);
            mapface.planenum = map.add_or_find_plane(plane);
            mapface.bevel = false;
        }
    }

    if (!CreateBrushWindings(&brush)) {
        return std::nullopt;
    }

    for (auto &face : brush.sides) {
        CheckFace(&face, *face.source);
    }

    // Rotatable objects must have a bounding box big enough to
    // account for all its rotations

    // if -wrbrushes is in use, don't do this for the clipping hulls because it depends on having
    // the actual non-hacked bbox (it doesn't write axial planes).

    // Hexen2 also doesn't want the bbox expansion, it's handled in engine (see: SV_LinkEdict)

    // Only do this for hipnotic rotation. For origin brushes in Quake, it breaks some of their
    // uses (e.g. func_train). This means it's up to the mapper to expand the model bounds with
    // clip brushes if they're going to rotate a model in vanilla Quake and not use hipnotic rotation.
    // The idea behind the bounds expansion was to avoid incorrect vis culling (AFAIK).
    const bool shouldExpand = (src->origin[0] != 0.0 || src->origin[1] != 0.0 || src->origin[2] != 0.0) &&
                                src->rotation == rotation_t::hipnotic &&
                                (hullnum >= 0) // hullnum < 0 corresponds to -wrbrushes clipping hulls
                                && qbsp_options.target_game->id != GAME_HEXEN_II; // never do this in Hexen 2

    if (shouldExpand) {
        vec_t max = -std::numeric_limits<vec_t>::infinity(), min = std::numeric_limits<vec_t>::infinity();
            
        for (auto &v : brush.bounds.mins()) {
            min = ::min(min, v);
            max = ::max(max, v);
        }
        for (auto &v : brush.bounds.maxs()) {
            min = ::min(min, v);
            max = ::max(max, v);
        }

        vec_t delta = std::max(fabs(max), fabs(min));
        brush.bounds = {-delta, delta};
    }

    return brush;
}

//=============================================================================

static void Brush_LoadEntity(mapentity_t *dst, const mapentity_t *src, const int hullnum, content_stats_base_t &stats, bspbrush_vector_t &brushes)
{
    // _omitbrushes 1 just discards all brushes in the entity.
    // could be useful for geometry guides, selective compilation, etc.
    if (src->epairs.get_int("_omitbrushes")) {
        return;
    }

    int i;
    int lmshift;
    bool all_detail, all_detail_fence, all_detail_illusionary;

    const std::string &classname = src->epairs.get("classname");

    /* If the source entity is func_detail, set the content flag */
    if (!qbsp_options.nodetail.value()) {
        all_detail = false;
        if (!Q_strcasecmp(classname, "func_detail")) {
            all_detail = true;
        }

        all_detail_fence = false;
        if (!Q_strcasecmp(classname, "func_detail_fence") || !Q_strcasecmp(classname, "func_detail_wall")) {
            all_detail_fence = true;
        }

        all_detail_illusionary = false;
        if (!Q_strcasecmp(classname, "func_detail_illusionary")) {
            all_detail_illusionary = true;
        }
    }

    /* entities with custom lmscales are important for the qbsp to know about */
    i = 16 * src->epairs.get_float("_lmscale");
    if (!i)
        i = 16; // if 0, pick a suitable default
    lmshift = 0;
    while (i > 1) {
        lmshift++; // only allow power-of-two scales
        i /= 2;
    }

    /* _mirrorinside key (for func_water etc.) */
    std::optional<bool> mirrorinside;

    if (src->epairs.has("_mirrorinside")) {
        mirrorinside = src->epairs.get_int("_mirrorinside") ? true : false;
    }

    /* _noclipfaces */
    std::optional<bool> clipsametype;

    if (src->epairs.has("_noclipfaces")) {
        clipsametype = src->epairs.get_int("_noclipfaces") ? false : true;
    }

    const bool func_illusionary_visblocker = (0 == Q_strcasecmp(classname, "func_illusionary_visblocker"));

    auto it = src->mapbrushes.begin();
    for (i = 0; i < src->mapbrushes.size(); i++, it++) {
        logging::percent(i, src->mapbrushes.size());
        auto &mapbrush = *it;
        contentflags_t contents = mapbrush.contents;

        // per-brush settings
        bool detail = false;
        bool detail_illusionary = false;
        bool detail_fence = false;

        // inherit the per-entity settings
        detail |= all_detail;
        detail_illusionary |= all_detail_illusionary;
        detail_fence |= all_detail_fence;

        /* "origin" brushes always discarded */
        if (contents.is_origin(qbsp_options.target_game))
            continue;

        /* -omitdetail option omits all types of detail */
        if (qbsp_options.omitdetail.value() && detail)
            continue;
        if ((qbsp_options.omitdetail.value() || qbsp_options.omitdetailillusionary.value()) && detail_illusionary)
            continue;
        if ((qbsp_options.omitdetail.value() || qbsp_options.omitdetailfence.value()) && detail_fence)
            continue;

        /* turn solid brushes into detail, if we're in hull0 */
        if (hullnum <= 0 && contents.is_solid(qbsp_options.target_game)) {
            if (detail_illusionary) {
                contents = qbsp_options.target_game->create_detail_illusionary_contents(contents);
            } else if (detail_fence) {
                contents = qbsp_options.target_game->create_detail_fence_contents(contents);
            } else if (detail) {
                contents = qbsp_options.target_game->create_detail_solid_contents(contents);
            }
        }

        /* func_detail_illusionary don't exist in the collision hull
         * (or bspx export) except for Q2, who needs them in there */
        if (hullnum > 0 && detail_illusionary) {
            continue;
        }

        /*
         * "clip" brushes don't show up in the draw hull, but we still want to
         * include them in the model bounds so collision detection works
         * correctly.
         */
        if (hullnum != HULL_COLLISION && contents.is_clip(qbsp_options.target_game)) {
            if (hullnum == 0) {
                if (auto brush = LoadBrush(src, &mapbrush, contents, hullnum)) {
                    dst->bounds += brush->bounds;
                }
                continue;
                // for hull1, 2, etc., convert clip to CONTENTS_SOLID
            } else {
                contents = qbsp_options.target_game->create_solid_contents();
            }
        }

        /* "hint" brushes don't affect the collision hulls */
        if (MapBrush_IsHint(mapbrush)) {
            if (hullnum > 0)
                continue;
            contents = qbsp_options.target_game->create_empty_contents();
        }

        /* entities in some games never use water merging */
        if (dst != map.world_entity() && !qbsp_options.target_game->allow_contented_bmodels) {
            contents = qbsp_options.target_game->create_solid_contents();

            /* Hack to turn bmodels with "_mirrorinside" into func_detail_fence in hull 0.
                this is to allow "_mirrorinside" to work on func_illusionary, func_wall, etc.
                Otherwise they would be CONTENTS_SOLID and the inside faces would be deleted.

                It's CONTENTS_DETAIL_FENCE because this gets mapped to CONTENTS_SOLID just
                before writing the bsp, and bmodels normally have CONTENTS_SOLID as their
                contents type.
                */
            if (hullnum <= 0 && mirrorinside.value_or(false)) {
                contents = qbsp_options.target_game->create_detail_fence_contents(contents);
            }
        }

        /* nonsolid brushes don't show up in clipping hulls */
        if (hullnum > 0 && !contents.is_solid(qbsp_options.target_game) && !contents.is_sky(qbsp_options.target_game))
            continue;

        /* sky brushes are solid in the collision hulls */
        if (hullnum > 0 && contents.is_sky(qbsp_options.target_game))
            contents = qbsp_options.target_game->create_solid_contents();

        // apply extended flags
        contents.set_mirrored(mirrorinside);
        contents.set_clips_same_type(clipsametype);
        contents.illusionary_visblocker = func_illusionary_visblocker;

        auto brush = LoadBrush(src, &mapbrush, contents, hullnum);

        if (!brush) {
            continue;
        }

        brush->lmshift = lmshift;

        for (auto &face : brush->sides) {
            face.lmshift = lmshift;
        }

        if (classname == std::string_view("func_areaportal")) {
            brush->func_areaportal = const_cast<mapentity_t *>(src); // FIXME: get rid of consts on src in the callers?
        }

        qbsp_options.target_game->count_contents_in_stats(brush->contents, stats);

        dst->bounds += brush->bounds;
        brushes.push_back(std::make_unique<bspbrush_t>(std::move(*brush)));
    }

    logging::percent(src->mapbrushes.size(), src->mapbrushes.size(), src == map.world_entity());
}

/*
============
Brush_LoadEntity

hullnum HULL_COLLISION should contain ALL brushes. (used by BSPX_CreateBrushList())
hullnum 0 does not contain clip brushes.
============
*/
void Brush_LoadEntity(mapentity_t *entity, const int hullnum, bspbrush_vector_t &brushes)
{
    logging::funcheader();

    auto stats = qbsp_options.target_game->create_content_stats();

    Brush_LoadEntity(entity, entity, hullnum, *stats, brushes);

    /*
     * If this is the world entity, find all func_group and func_detail
     * entities and add their brushes with the appropriate contents flag set.
     */
    if (entity == map.world_entity()) {
        /*
         * We no longer care about the order of adding func_detail and func_group,
         * Entity_SortBrushes will sort the brushes
         */
        for (int i = 1; i < map.entities.size(); i++) {
            mapentity_t *source = &map.entities.at(i);

            /* Load external .map and change the classname, if needed */
            ProcessExternalMapEntity(source);

            ProcessAreaPortal(source);

            if (IsWorldBrushEntity(source) || IsNonRemoveWorldBrushEntity(source)) {
                Brush_LoadEntity(entity, source, hullnum, *stats, brushes);
            }
        }
    }

    logging::header("CountBrushes");

    qbsp_options.target_game->print_content_stats(*stats, "brushes");
}

bool bspbrush_t::update_bounds(bool warn_on_failures)
{
    this->bounds = {};

    for (const auto &face : sides) {
        if (face.w) {
            this->bounds = this->bounds.unionWith(face.w.bounds());
        }
    }

	for (size_t i = 0; i < 3; i++) {
        // todo: map_source_location in bspbrush_t
		if (this->bounds.mins()[0] <= -qbsp_options.worldextent.value() || this->bounds.maxs()[0] >= qbsp_options.worldextent.value()) {
            if (warn_on_failures) {
    			logging::print("WARNING: {}: brush bounds out of range\n", mapbrush ? mapbrush->line : parser_source_location());
            }
            return false;
        }
		if (this->bounds.mins()[0] >= qbsp_options.worldextent.value() || this->bounds.maxs()[0] <= -qbsp_options.worldextent.value()) {
            if (warn_on_failures) {
    			logging::print("WARNING: {}: no visible sides on brush\n", mapbrush ? mapbrush->line : parser_source_location());
            }
            return false;
        }
	}

    this->sphere_origin = (bounds.mins() + bounds.maxs()) / 2.0;
    this->sphere_radius = qv::length((bounds.maxs() - bounds.mins()) / 2.0);

    return true;
}
