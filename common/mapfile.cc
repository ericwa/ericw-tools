#include <common/mapfile.hh>
#include <common/log.hh>
#include <common/ostream.hh>
#include <common/imglib.hh>
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
        {
            shift,
            rotate,
            scale
        },
        {
            axis
        }
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
    Q_assert(std::holds_alternative<texdef_quake_ed_t>(raw));

    if (parser.token[4] != '1' && parser.token[4] != '2') {
        return false;
    }

    raw = texdef_etp_t { std::get<texdef_quake_ed_t>(raw), parser.token[4] == '2' };
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


/*
ComputeAxisBase()
from q3map2

computes the base texture axis for brush primitive texturing
note: ComputeAxisBase here and in editor code must always BE THE SAME!
warning: special case behaviour of atan2( y, x ) <-> atan( y / x ) might not be the same everywhere when x == 0
rotation by (0,RotY,RotZ) assigns X to normal
*/
inline std::tuple<qvec3d, qvec3d> compute_axis_base(const qvec3d &normal_unsanitized)
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

    return {
    /* rotate (0,1,0) and (0,0,1) to compute texX and texY */
        {
          -sin(RotZ),
           cos(RotZ),
           0
        },
        {
    /* the texY vector is along -z (t texture coorinates axis) */
           -sin(RotY) * cos(RotZ),
           -sin(RotY) * sin(RotZ),
           -cos(RotY)
        }
    };
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

        parser.parse_token(PARSE_SAMELINE);
        texture = std::move(parser.token);
    } else if (base_format == texcoord_style_t::quaked) {
        parser.parse_token(PARSE_SAMELINE);
        texture = std::move(parser.token);

        parser.parse_token(PARSE_SAMELINE | PARSE_PEEK);

        if (parser.token == "[") {
            raw = parse_valve_220(parser);
        } else {
            raw = parse_quake_ed(parser);
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

namespace convert_to_quaked
{
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

        const float cosAngle = std::max(-1.0f, std::min(1.0f, qv::dot(start, end)));
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

    static texdef_quake_ed_t Reverse_QuakeEd(qmat2x2f M, const qplane3d &plane, bool preserveX)
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

        auto [ xv, yv, snapped_normal ] = texture_axis_t(plane);

        const qvec2f sAxis = projectToAxisPlane(snapped_normal, xv);
        const qvec2f tAxis = projectToAxisPlane(snapped_normal, yv);

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

                    texdef_quake_ed_t reversed;
                    reversed.rotate = angleGuess;
                    reversed.scale[0] = xScaleSgn / absXscale;
                    reversed.scale[1] = yScaleSgn / absYscale;
                    return reversed;
                }
            }
        }

        // TODO: detect when we expect this to fail, i.e.  invalid texture axes (0-length),
        // and throw an error if it fails unexpectedly.

        return {};
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

    static qvec2f evalTexDefAtPoint(const texdef_quake_ed_t &texdef, const qplane3d &faceplane, const qvec3f &point)
    {
        brush_side_t temp;
        temp.set_texinfo(texdef_quake_ed_t { texdef.shift, texdef.rotate, texdef.scale });

        const qmat4x4f worldToTexSpace_res = texVecsTo4x4Matrix(faceplane, temp.vecs);
        const qvec2f uv = qvec2f(worldToTexSpace_res * qvec4f(point[0], point[1], point[2], 1.0f));
        return uv;
    }

    static texdef_quake_ed_t addShift(const texdef_quake_ed_t &texdef, const qvec2f shift)
    {
        texdef_quake_ed_t res = texdef;
        res.shift = shift;
        return res;
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
    static texdef_quake_ed_t TexDef_BSPToQuakeEd(const qplane3d &faceplane,
        const std::optional<img::texture_meta> &texture, const texvecf &in_vecs, const std::array<qvec3d, 3> &facepoints)
    {
        // First get the un-rotated, un-scaled unit texture vecs (based on the face plane).
        texture_axis_t axis(faceplane);
        qvec3d &snapped_normal = axis.snapped_normal;

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

        const texdef_quake_ed_t res = Reverse_QuakeEd(texPlaneToUV, faceplane, false);

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
};

namespace convert_to_valve
{
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

        for (size_t i = 0; i < 2; i++) {
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
};

namespace convert_to_bp
{
    // From FaceToBrushPrimitFace in GtkRadiant
    static texdef_bp_t TexDef_BSPToBrushPrimitives(
        const qplane3d &plane, const img::texture_meta &texture, const texvecf &in_vecs)
    {
        auto [ texX, texY ] = compute_axis_base(plane.normal);

        // compute projection vector
        qvec3d proj = plane.normal * plane.dist;

        // (0,0) in plane axis base is (0,0,0) in world coordinates + projection on the affine plane
        // (1,0) in plane axis base is texX in world coordinates + projection on the affine plane
        // (0,1) in plane axis base is texY in world coordinates + projection on the affine plane
        // use old texture code to compute the ST coords of these points
        qvec2d st[] = {
            in_vecs.uvs(proj, texture.width, texture.height),
            in_vecs.uvs(texX + proj, texture.width, texture.height),
            in_vecs.uvs(texY + proj, texture.width, texture.height)
        };
        // compute texture matrix
        texdef_bp_t res;
        res.axis.set_col(2, st[0]);
        res.axis.set_col(0, st[1] - st[0]);
        res.axis.set_col(1, st[2] - st[0]);
        return res;
    }
};

void brush_side_t::convert_to(texcoord_style_t style, const gamedef_t *game, const settings::common_settings &options)
{
    // we're already this style
    switch (style) {
    case texcoord_style_t::quaked:
        if (std::holds_alternative<texdef_quake_ed_t>(raw)) {
            return;
        }
        break;
    case texcoord_style_t::etp:
        if (std::holds_alternative<texdef_etp_t>(raw)) {
            return;
        }
        break;
    case texcoord_style_t::brush_primitives:
        if (std::holds_alternative<texdef_bp_t>(raw)) {
            return;
        }
        break;
    case texcoord_style_t::valve_220:
        if (std::holds_alternative<texdef_valve_t>(raw)) {
            return;
        }
        break;
    }

    if (style == texcoord_style_t::quaked) {
        std::optional<img::texture_meta> meta = std::nullopt;

        if (game) {
            meta = std::get<0>(img::load_texture_meta(texture, game, options));
        }

        raw = convert_to_quaked::TexDef_BSPToQuakeEd(plane, meta, vecs, planepts);
    } else if (style == texcoord_style_t::valve_220) {
        raw = convert_to_valve::TexDef_BSPToValve(vecs);
    } else if (style == texcoord_style_t::brush_primitives) {
        if (!game) {
            FError("conversion to brush primitives requires a `--game` option to be set");
        }

        auto [ meta, result, data ] = img::load_texture_meta(texture, game, options);

        if (!meta) {
            FError("conversion to brush primitives requires texture to be loaded");
        }

        raw = convert_to_bp::TexDef_BSPToBrushPrimitives(plane, meta.value(), vecs);
    } else {
        FError("can't currently convert to this format!");
    }
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

void brush_t::convert_to(texcoord_style_t style, const gamedef_t *game, const settings::common_settings &options)
{
    for (auto &face : faces) {
        face.convert_to(style, game, options);
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

void map_file_t::convert_to(texcoord_style_t style, const gamedef_t *game, const settings::common_settings &options)
{
    for (auto &entity : entities) {
        for (auto &brush : entity.brushes) {
            brush.convert_to(style, game, options);
        }
    }
}
