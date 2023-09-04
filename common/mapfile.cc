#include <common/mapfile.hh>
#include <common/log.hh>
#include <common/ostream.hh>
#include <utility>

/*static*/ bool brush_side_t::is_valid_texture_projection(const qvec3f &faceNormal, const qvec3f &s_vec, const qvec3f &t_vec)
{
    // TODO: This doesn't match how light does it (TexSpaceToWorld)

    const qvec3f tex_normal = qv::normalize(qv::cross(s_vec, t_vec));

    for (size_t i = 0; i < 3; i++) {
        if (std::isnan(tex_normal[i])) {
            return false;
        }
    }

    const float cosangle = qv::dot(tex_normal, faceNormal);

    if (std::isnan(cosangle)) {
        return false;
    } else if (fabs(cosangle) < ZERO_EPSILON) {
        return false;
    }

    return true;
}

void brush_side_t::validate_texture_projection()
{
    if (!is_valid_texture_projection()) {
        /*
        if (qbsp_options.verbose.value()) {
        logging::print("WARNING: {}: repairing invalid texture projection (\"{}\" near {} {} {})\n", mapface.line,
        mapface.texname, (int)mapface.planepts[0][0], (int)mapface.planepts[0][1], (int)mapface.planepts[0][2]);
        } else {
        issue_stats.num_repaired++;
        }
        */

        // Reset texturing to sensible defaults
        set_texinfo(texdef_quake_ed_t {
            { 0.0, 0.0 },
            0,
            { 1.0, 1.0 }
            });

        Q_assert(is_valid_texture_projection());
    }
}

/*static*/ texdef_bp_t brush_side_t::parse_bp(parser_t &parser)
{
    qmat<vec_t, 2, 3> texMat;

    parser.parse_token(PARSE_SAMELINE);

    if (parser.token != "(") {
        goto parse_error;
    }

    for (size_t i = 0; i < 2; i++) {
        parser.parse_token(PARSE_SAMELINE);
        if (parser.token != "(") {
            goto parse_error;
        }

        for (size_t j = 0; j < 3; j++) {
            parser.parse_token(PARSE_SAMELINE);
            texMat.at(i, j) = std::stod(parser.token);
        }

        parser.parse_token(PARSE_SAMELINE);

        if (parser.token != ")") {
            goto parse_error;
        }
    }

    parser.parse_token(PARSE_SAMELINE);

    if (parser.token != ")") {
        goto parse_error;
    }

    return { texMat };

parse_error:
    FError("{}: couldn't parse Brush Primitives texture info", parser.location);
}

/*static*/ texdef_valve_t brush_side_t::parse_valve_220(parser_t &parser)
{
    qmat<vec_t, 2, 3> axis;
    qvec2d shift, scale;
    vec_t rotate;

    for (size_t i = 0; i < 2; i++) {
        parser.parse_token(PARSE_SAMELINE);

        if (parser.token != "[") {
            goto parse_error;
        }

        for (size_t j = 0; j < 3; j++) {
            parser.parse_token(PARSE_SAMELINE);
            axis.at(i, j) = std::stod(parser.token);
        }

        parser.parse_token(PARSE_SAMELINE);
        shift[i] = std::stod(parser.token);
        parser.parse_token(PARSE_SAMELINE);

        if (parser.token != "]") {
            goto parse_error;
        }
    }
    parser.parse_token(PARSE_SAMELINE);
    rotate = std::stod(parser.token);
    parser.parse_token(PARSE_SAMELINE);
    scale[0] = std::stod(parser.token);
    parser.parse_token(PARSE_SAMELINE);
    scale[1] = std::stod(parser.token);

    return {
        shift,
        rotate,
        scale,
        axis
    };

parse_error:
    FError("{}: couldn't parse Valve220 texture info", parser.location);
}

/*static*/ texdef_quake_ed_t brush_side_t::parse_quake_ed(parser_t &parser)
{
    qvec2d shift, scale;
    vec_t rotate;

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

    return {
        shift,
        rotate,
        scale
    };
}

bool brush_side_t::parse_quark_comment(parser_t &parser)
{
    if (!parser.parse_token(PARSE_COMMENT | PARSE_OPTIONAL)) {
        return false;
    }

    if (parser.token.length() < 5 || strncmp(parser.token.c_str(), "//TX", 4)) {
        return false;
    }

    // QuArK TX modes can only exist on Quaked-style maps
    Q_assert(style == texcoord_style_t::quaked);
    style = texcoord_style_t::etp;

    if (parser.token[4] == '1') {
        raw = texdef_etp_t { std::get<texdef_quake_ed_t>(raw), false };
    } else if (parser.token[4] == '2') {
        raw = texdef_etp_t { std::get<texdef_quake_ed_t>(raw), true };
    } else {
        return false;
    }

    return true;
}

void brush_side_t::parse_extended_texinfo(parser_t &parser)
{
    if (!parse_quark_comment(parser)) {
        // Parse extra Quake 2 surface info
        if (parser.parse_token(PARSE_OPTIONAL)) {
            texinfo_quake2_t q2_info;

            q2_info.contents = {std::stoi(parser.token)};

            if (parser.parse_token(PARSE_OPTIONAL)) {
                q2_info.flags.native = std::stoi(parser.token);
            }
            if (parser.parse_token(PARSE_OPTIONAL)) {
                q2_info.value = std::stoi(parser.token);
            }

            extended_info = q2_info;

            parse_quark_comment(parser);
        }
    }
}

void brush_side_t::set_texinfo(const texdef_quake_ed_t &texdef)
{
    texture_axis_t axis(plane);
    qvec3d vectors[2] = {
        axis.xv,
        axis.yv
    };

    /* Rotate axis */
    vec_t ang = texdef.rotate / 180.0 * Q_PI;
    vec_t sinv = sin(ang);
    vec_t cosv = cos(ang);

    size_t sv, tv;

    if (vectors[0][0]) {
        sv = 0;
    } else if (vectors[0][1]) {
        sv = 1;
    } else {
        sv = 2; // unreachable, due to TextureAxisFromPlane lookup table
    }

    if (vectors[1][0]) {
        tv = 0; // unreachable, due to TextureAxisFromPlane lookup table
    } else if (vectors[1][1]) {
        tv = 1;
    } else {
        tv = 2;
    }

    for (size_t i = 0; i < 2; i++) {
        vec_t ns = cosv * vectors[i][sv] - sinv * vectors[i][tv];
        vec_t nt = sinv * vectors[i][sv] + cosv * vectors[i][tv];
        vectors[i][sv] = ns;
        vectors[i][tv] = nt;
    }

    for (size_t i = 0; i < 2; i++) {
        for (size_t j = 0; j < 3; j++) {
            /* Interpret zero scale as no scaling */
            vecs.at(i, j) = vectors[i][j] / (texdef.scale[i] ? texdef.scale[i] : 1);
        }
    }

    vecs.at(0, 3) = texdef.shift[0];
    vecs.at(1, 3) = texdef.shift[1];

    // TODO: move these self-tests somewhere else, do them for all types
#if 0
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
            ewt::print("wrong rotate got {} expected {}\n", reversed.rotate, rotate);
        }

        if (fabs(reversed.scale[0] - scale[0]) > 0.001 || fabs(reversed.scale[1] - scale[1]) > 0.001) {
            ewt::print("wrong scale, got {} {} exp {} {}\n", reversed.scale[0], reversed.scale[1], scale[0], scale[1]);
        }

        if (fabs(reversed.shift[0] - shift[0]) > 0.1 || fabs(reversed.shift[1] - shift[1]) > 0.1) {
            ewt::print("wrong shift, got {} {} exp {} {}\n", reversed.shift[0], reversed.shift[1], shift[0], shift[1]);
        }
    }
#endif
}

void brush_side_t::set_texinfo(const texdef_valve_t &texdef)
{
    for (size_t i = 0; i < 3; i++) {
        vecs.at(0, i) = texdef.axis.at(0, i) / texdef.scale[0];
        vecs.at(1, i) = texdef.axis.at(1, i) / texdef.scale[1];
    }

    vecs.at(0, 3) = texdef.shift[0];
    vecs.at(1, 3) = texdef.shift[1];
}

void brush_side_t::set_texinfo(const texdef_etp_t &texdef)
{
    qvec3d vectors[2];

    /*
    * Type 1 uses vecs[0] = (pt[2] - pt[0]) and vecs[1] = (pt[1] - pt[0])
    * Type 2 reverses the order of the vecs
    * 128 is the scaling factor assumed by QuArK.
    */
    if (!texdef.tx2) {
        vectors[0] = planepts[2] - planepts[0];
        vectors[1] = planepts[1] - planepts[0];
    } else {
        vectors[0] = planepts[1] - planepts[0];
        vectors[1] = planepts[2] - planepts[0];
    }

    vectors[0] *= 1.0 / 128.0;
    vectors[1] *= 1.0 / 128.0;

    vec_t a = qv::dot(vectors[0], vectors[0]);
    vec_t b = qv::dot(vectors[0], vectors[1]);
    vec_t c = b; /* qv::dot(vectors[1], vectors[0]); */
    vec_t d = qv::dot(vectors[1], vectors[1]);

    /*
    * Want to solve for out->vecs:
    *
    *    | a b | | out->vecs[0] | = | vecs[0] |
    *    | c d | | out->vecs[1] |   | vecs[1] |
    *
    * => | out->vecs[0] | = __ 1.0__  | d  -b | | vecs[0] |
    *    | out->vecs[1] |   a*d - b*c | -c  a | | vecs[1] |
    */
    vec_t determinant = a * d - b * c;
    if (fabs(determinant) < ZERO_EPSILON) {
        logging::print("WARNING: {}: Face with degenerate QuArK-style texture axes\n", location);
        for (size_t i = 0; i < 3; i++) {
            vecs.at(0, i) = vecs.at(1, i) = 0;
        }
    } else {
        for (size_t i = 0; i < 3; i++) {
            vecs.at(0, i) = (d * vectors[0][i] - b * vectors[1][i]) / determinant;
            vecs.at(1, i) = -(a * vectors[1][i] - c * vectors[0][i]) / determinant;
        }
    }

    /* Finally, the texture offset is indicated by planepts[0] */
    for (size_t i = 0; i < 3; ++i) {
        vectors[0][i] = vecs.at(0, i);
        vectors[1][i] = vecs.at(1, i);
    }

    vecs.at(0, 3) = -qv::dot(vectors[0], planepts[0]);
    vecs.at(1, 3) = -qv::dot(vectors[1], planepts[0]);
}

void brush_side_t::set_texinfo(const texdef_bp_t &texdef)
{
#if 0
    const auto &texture = map.load_image_meta(mapface.texname.c_str());
    const int32_t width = texture ? texture->width : 64;
    const int32_t height = texture ? texture->height : 64;

    SetTexinfo_BrushPrimitives(texMat, plane.normal, width, height, tx->vecs);
#endif
    FError("todo BP");
}

void brush_side_t::parse_texture_def(parser_t &parser, texcoord_style_t base_format)
{
    if (base_format == texcoord_style_t::brush_primitives) {
        raw = parse_bp(parser);
        style = texcoord_style_t::brush_primitives;

        parser.parse_token(PARSE_SAMELINE);
        texture = std::move(parser.token);
    } else if (base_format == texcoord_style_t::quaked) {
        parser.parse_token(PARSE_SAMELINE);
        texture = std::move(parser.token);

        parser.parse_token(PARSE_SAMELINE | PARSE_PEEK);

        if (parser.token == "[") {
            raw = parse_valve_220(parser);
            style = texcoord_style_t::valve_220;
        } else {
            raw = parse_quake_ed(parser);
            style = texcoord_style_t::quaked;
        }
    } else {
        FError("{}: Bad brush format", parser.location);
    }

    // Read extra Q2 params and/or QuArK subtype
    parse_extended_texinfo(parser);

    std::visit([this](auto &&x) { set_texinfo(x); }, raw);
}

void brush_side_t::parse_plane_def(parser_t &parser)
{
    for (size_t i = 0; i < 3; i++) {
        if (i != 0) {
            parser.parse_token();
        }

        if (parser.token != "(") {
            goto parse_error;
        }

        for (size_t j = 0; j < 3; j++) {
            parser.parse_token(PARSE_SAMELINE);
            planepts[i][j] = std::stod(parser.token);
        }

        parser.parse_token(PARSE_SAMELINE);

        if (parser.token != ")") {
            goto parse_error;
        }
    }

    return;

parse_error:
    FError("{}: Invalid brush plane format", parser.location);
}

void brush_side_t::write_extended_info(std::ostream &stream)
{
    if (extended_info) {
        ewt::print(stream, " {} {} {}", extended_info->contents.native, extended_info->flags.native, extended_info->value);
    }
}

void brush_side_t::write_texinfo(std::ostream &stream, const texdef_quake_ed_t &texdef)
{
    ewt::print(stream, "{} {} {} {} {}", texdef.shift[0], texdef.shift[1], texdef.rotate, texdef.scale[0], texdef.scale[1]);
    write_extended_info(stream);
}

void brush_side_t::write_texinfo(std::ostream &stream, const texdef_valve_t &texdef)
{
    ewt::print(stream, "[ {} {} {} {} ] [ {} {} {} {} ] {} {} {}",
        texdef.axis.at(0, 0), texdef.axis.at(0, 1), texdef.axis.at(0, 2), texdef.shift[0],
        texdef.axis.at(1, 0), texdef.axis.at(1, 1), texdef.axis.at(1, 2), texdef.shift[1],
        texdef.rotate, texdef.scale[0], texdef.scale[1]);
    write_extended_info(stream);
}

void brush_side_t::write_texinfo(std::ostream &stream, const texdef_etp_t &texdef)
{
    write_texinfo(stream, (const texdef_quake_ed_t &) texdef);
    ewt::print(stream, "//TX{}", texdef.tx2 ? '2' : '1');
}

void brush_side_t::write_texinfo(std::ostream &stream, const texdef_bp_t &texdef)
{
    FError("todo bp");
}

void brush_side_t::write(std::ostream &stream)
{
    ewt::print(stream, "( {} {} {} ) ( {} {} {} ) ( {} {} {} ) {} ", planepts[0][0], planepts[0][1], planepts[0][2],
        planepts[1][0], planepts[1][1], planepts[1][2], planepts[2][0], planepts[2][1], planepts[2][2],
        texture);

    std::visit([this, &stream](auto &&x) { write_texinfo(stream, x); }, raw);
}

void brush_side_t::convert_to(texcoord_style_t style)
{
    // we're already this style
    if (this->style == style) {
        return;
    }

    this->style = style;
}

void brush_t::parse_brush_face(parser_t &parser, texcoord_style_t base_format)
{
    brush_side_t side;

    side.location = parser.location;

    side.parse_plane_def(parser);

    /* calculate the normal/dist plane equation */
    qvec3d ab = side.planepts[0] - side.planepts[1];
    qvec3d cb = side.planepts[2] - side.planepts[1];

    vec_t length;
    qvec3d normal = qv::normalize(qv::cross(ab, cb), length);
    vec_t dist = qv::dot(side.planepts[1], normal);

    side.plane = { normal, dist };

    side.parse_texture_def(parser, base_format);

    if (length < NORMAL_EPSILON) {
        logging::print("WARNING: {}: Brush plane with no normal\n", parser.location);
        return;
    }

    /* Check for duplicate planes */
    for (auto &check : faces) {
        if (qv::epsilonEqual(check.plane, side.plane) ||
            qv::epsilonEqual(-check.plane, side.plane)) {
            logging::print("{}: Brush with duplicate plane\n", parser.location);
            return;
        }
    }

    // ericw -- round texture vector values that are within ZERO_EPSILON of integers,
    // to attempt to attempt to work around corrupted lightmap sizes in DarkPlaces
    // (it uses 32 bit precision in CalcSurfaceExtents)
    for (size_t i = 0; i < 2; i++) {
        for (size_t j = 0; j < 4; j++) {
            vec_t r = Q_rint(side.vecs.at(i, j));
            if (fabs(side.vecs.at(i, j) - r) < ZERO_EPSILON) {
                side.vecs.at(i, j) = r;
            }
        }
    }

    side.validate_texture_projection();

    faces.emplace_back(std::move(side));
}

void brush_t::write(std::ostream &stream)
{
    stream << "{\n";

    if (base_format == texcoord_style_t::brush_primitives) {
        stream << "brushDef\n{\n";
    }

    for (auto &face : faces) {
        face.write(stream);
        stream << "\n";
    }

    if (base_format == texcoord_style_t::brush_primitives) {
        stream << "}\n";
    }

    stream << "}\n";
}

void brush_t::convert_to(texcoord_style_t style)
{
    for (auto &face : faces) {
        face.convert_to(style);
    }

    if (style == texcoord_style_t::brush_primitives) {
        base_format = style;
    } else {
        base_format = texcoord_style_t::quaked;
    }
}

// map file stuff

void map_entity_t::parse_entity_dict(parser_t &parser)
{
    std::string key = std::move(parser.token);

    // trim whitespace from start/end
    while (std::isspace(key.front())) {
        key.erase(key.begin());
    }
    while (std::isspace(key.back())) {
        key.erase(key.end() - 1);
    }

    parser.parse_token(PARSE_SAMELINE);
    epairs.set(key, parser.token);
}

void map_entity_t::parse_brush(parser_t &parser)
{
    // ericw -- brush primitives
    if (!parser.parse_token(PARSE_PEEK)) {
        FError("{}: unexpected EOF after {{ beginning brush", parser.location);
    }

    brush_t brush;

    if (parser.token == "(") {
        brush.base_format = texcoord_style_t::quaked;
    } else {
        parser.parse_token();
        brush.base_format = texcoord_style_t::brush_primitives;

        // optional
        if (parser.token == "brushDef") {
            if (!parser.parse_token()) {
                FError("Brush primitives: unexpected EOF (nothing after brushDef)");
            }
        }

        // mandatory
        if (parser.token != "{") {
            FError("Brush primitives: expected second {{ at beginning of brush, got \"{}\"", parser.token);
        }
    }
    // ericw -- end brush primitives

    while (parser.parse_token()) {

        // set linenum after first parsed token
        if (!brush.location) {
            brush.location = parser.location;
        }

        if (parser.token == "}") {
            break;
        }

        brush.parse_brush_face(parser, brush.base_format);
    }

    // ericw -- brush primitives - there should be another closing }
    if (brush.base_format == texcoord_style_t::brush_primitives) {
        if (!parser.parse_token()) {
            FError("Brush primitives: unexpected EOF (no closing brace)");
        } else if (parser.token != "}") {
            FError("Brush primitives: Expected }}, got: {}", parser.token);
        }
    }
    // ericw -- end brush primitives

    if (brush.faces.size()) {
        brushes.push_back(std::move(brush));
    }
}

bool map_entity_t::parse(parser_t &parser)
{
    location = parser.location;

    if (!parser.parse_token()) {
        return false;
    }

    if (parser.token != "{") {
        FError("{}: Invalid entity format, {{ not found", parser.location);
    }

    do {
        if (!parser.parse_token()) {
            FError("Unexpected EOF (no closing brace)");
        }

        if (parser.token == "}") {
            break;
        } else if (parser.token == "{") {
            parse_brush(parser);
        } else {
            parse_entity_dict(parser);
        }
    } while (1);

    return true;
}

void map_entity_t::write(std::ostream &stream)
{
    stream << "{\n";

    for (auto &kvp : epairs) {
        ewt::print(stream, "\"{}\" \"{}\"\n", kvp.first, kvp.second);
    }

    size_t brush_id = 0;

    for (auto &brush : brushes) {
        ewt::print(stream, "// brush {}\n", brush_id++);
        brush.write(stream);
    }

    stream << "}\n";
}

void map_file_t::parse(parser_t &parser)
{
    while (true) {
        map_entity_t &entity = entities.emplace_back();

        if (!entity.parse(parser)) {
            break;
        }
    }

    // Remove dummy entity inserted above
    assert(!entities.back().epairs.size());
    entities.pop_back();
}

void map_file_t::write(std::ostream &stream)
{
    size_t ent_id = 0;

    for (auto &entity : entities) {
        ewt::print(stream, "// entity {}\n", ent_id++);
        entity.write(stream);
    }
}

void map_file_t::convert_to(texcoord_style_t style)
{
    for (auto &entity : entities) {
        for (auto &brush : entity.brushes) {
            brush.convert_to(style);
        }
    }
}
