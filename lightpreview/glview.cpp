/*  Copyright (C) 2017 Eric Wasylishen

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

#include "glview.h"

// #include <cstdio>
#include <cassert>
#include <tuple>

#include <QApplication>
#include <QImage>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QTime>
#include <fmt/core.h>
#include <QOpenGLFramebufferObject>
#include <QOpenGLDebugLogger>
#include <QStandardPaths>
#include <QDateTime>

#include <common/decompile.hh>
#include <common/bspfile.hh>
#include <common/bsputils.hh>
#include <common/bspinfo.hh>
#include <common/imglib.hh>
#include <common/prtfile.hh>
#include <light/light.hh>

// given a width and height, returns the number of mips required
// see https://registry.khronos.org/OpenGL/extensions/ARB/ARB_texture_non_power_of_two.txt
static int GetMipLevelsForDimensions(int w, int h)
{
    return 1 + static_cast<int>(std::floor(std::log2(std::max(w, h))));
}

GLView::GLView(QWidget *parent)
    : QOpenGLWidget(parent),
      m_keysPressed(0),
      m_moveSpeed(1000),
      m_displayAspect(1),
      m_cameraOrigin(0, 0, 0),
      m_cameraFwd(0, 1, 0),
      m_vao(),
      m_indexBuffer(QOpenGLBuffer::IndexBuffer),
      m_leakVao(),
      m_portalVao(),
      m_portalIndexBuffer(QOpenGLBuffer::IndexBuffer)
{
    for (auto &hullVao : m_hullVaos) {
        hullVao.indexBuffer = QOpenGLBuffer(QOpenGLBuffer::IndexBuffer);
    }

    setFocusPolicy(Qt::StrongFocus); // allow keyboard focus
}

GLView::~GLView()
{
    makeCurrent();

    delete m_program;
    delete m_program_wireframe;
    delete m_skybox_program;

    m_vbo.destroy();
    m_indexBuffer.destroy();
    m_vao.destroy();

    m_leakVao.destroy();
    m_leakVbo.destroy();

    m_portalVbo.destroy();
    m_portalIndexBuffer.destroy();
    m_portalVao.destroy();

    for (auto &hullVao : m_hullVaos) {
        hullVao.vbo.destroy();
        hullVao.indexBuffer.destroy();
        hullVao.vao.destroy();
    }

    placeholder_texture.reset();
    lightmap_texture.reset();
    face_visibility_texture.reset();
    m_drawcalls.clear();

    doneCurrent();
}

static const char *s_fragShader_Simple = R"(
#version 330 core
uniform vec4 drawcolor;

out vec4 color;

void main() {
    color = drawcolor;
}
)";

static const char *s_vertShader_Simple = R"(
#version 330 core

layout (location = 0) in vec3 position;

uniform mat4 MVP;

void main() {
    gl_Position = MVP * vec4(position, 1.0);
}
)";

static const char *s_fragShader_Wireframe = R"(
#version 330 core

out vec4 color;

void main() {
    color = vec4(1.0);
}
)";

static const char *s_vertShader_Wireframe = R"(
#version 330 core

layout (location = 0) in vec3 position;
layout (location = 6) in int face_index;

uniform mat4 MVP;
uniform usampler1D face_visibility_sampler;

bool is_culled() {
    int byte_index = face_index;

    uint sampled = texelFetch(face_visibility_sampler, byte_index, 0).r;

    return sampled != 16u;
}

void main() {
    if (is_culled()) {
        gl_Position = vec4(0.0);
        return;
    }
    gl_Position = MVP * vec4(position, 1.0);
}
)";

static const char *s_fragShader = R"(
#version 330 core

in vec2 uv;
in vec2 lightmap_uv;
in vec3 normal;
flat in vec3 flat_color;
flat in uint styles;

out vec4 color;

uniform sampler2D texture_sampler;
uniform sampler2DArray lightmap_sampler;
uniform float opacity;
uniform bool alpha_test;
uniform bool lightmap_only;
uniform bool fullbright;
uniform bool drawnormals;
uniform bool drawflat;
uniform float style_scalars[256];

void main() {
    if (drawnormals) {
        // remap -1..+1 to 0..1
        color = vec4((normal + vec3(1.0)) / vec3(2.0), opacity);
    } else if (drawflat) {
        color = vec4(flat_color, opacity);
    } else {
        vec3 texcolor = lightmap_only ? vec3(0.5) : texture(texture_sampler, uv).rgb;

        if (!lightmap_only && alpha_test && texture(texture_sampler, uv).a < 0.1) {
            discard;
        }

        vec3 lmcolor = fullbright ? vec3(0.5) : vec3(0);

        if (!fullbright)
        {
            for (uint i = 0u; i < 32u; i += 8u)
            {
                uint style = (styles >> i) & 0xFFu;

                if (style == 0xFFu)
                    break;

                lmcolor += texture(lightmap_sampler, vec3(lightmap_uv, float(style))).rgb * style_scalars[style];
            }
        }

        // 2.0 for overbright
        color = vec4(texcolor * lmcolor * 2.0, opacity);
    }
}
)";

static const char *s_vertShader = R"(
#version 330 core

layout (location = 0) in vec3 position;
layout (location = 1) in vec2 vertex_uv;
layout (location = 2) in vec2 vertex_lightmap_uv;
layout (location = 3) in vec3 vertex_normal;
layout (location = 4) in vec3 vertex_flat_color;
layout (location = 5) in uint vertex_styles;
layout (location = 6) in int face_index;

out vec2 uv;
out vec2 lightmap_uv;
out vec3 normal;
flat out vec3 flat_color;
flat out uint styles;

uniform mat4 MVP;
uniform usampler1D face_visibility_sampler;

bool is_culled() {
    int byte_index = face_index;

    uint sampled = texelFetch(face_visibility_sampler, byte_index, 0).r;

    return sampled != 16u;
}

void main() {
    if (is_culled()) {
        gl_Position = vec4(0.0);
        return;
    }

    gl_Position = MVP * vec4(position.x, position.y, position.z, 1.0);

    uv = vertex_uv;
    lightmap_uv = vertex_lightmap_uv;
    normal = vertex_normal;
    flat_color = vertex_flat_color;
    styles = vertex_styles;
}
)";

static const char *s_skyboxFragShader = R"(
#version 330 core

in vec3 fragment_world_pos;
in vec2 lightmap_uv;
in vec3 normal;
flat in vec3 flat_color;
flat in uint styles;

out vec4 color;

uniform samplerCube texture_sampler;
uniform sampler2DArray lightmap_sampler;
uniform bool lightmap_only;
uniform bool fullbright;
uniform bool drawnormals;
uniform bool drawflat;
uniform float style_scalars[256];

uniform vec3 eye_origin;

void main() {
    if (drawnormals) {
        // remap -1..+1 to 0..1
        color = vec4((normal + vec3(1.0)) / vec3(2.0), 1.0);
    } else if (drawflat) {
        color = vec4(flat_color, 1.0);
    } else {
        if (!fullbright && lightmap_only)
        {
            vec3 lmcolor = vec3(0.5);

            for (uint i = 0u; i < 32u; i += 8u)
            {
                uint style = (styles >> i) & 0xFFu;

                if (style == 0xFFu)
                    break;

                lmcolor += texture(lightmap_sampler, vec3(lightmap_uv, float(style))).rgb * style_scalars[style];
            }

            // 2.0 for overbright
            color = vec4(lmcolor * 2.0, 1.0);
        }
        else
        {
            // cubemap case
            vec3 dir = normalize(fragment_world_pos - eye_origin);
            color = vec4(texture(texture_sampler, dir).rgb, 1.0);
        }
    }
}
)";

static const char *s_skyboxVertShader = R"(
#version 330 core

layout (location = 0) in vec3 position;
layout (location = 1) in vec2 vertex_uv;
layout (location = 2) in vec2 vertex_lightmap_uv;
layout (location = 3) in vec3 vertex_normal;
layout (location = 4) in vec3 vertex_flat_color;
layout (location = 5) in uint vertex_styles;
layout (location = 6) in int face_index;

out vec3 fragment_world_pos;
out vec2 lightmap_uv;
out vec3 normal;
flat out vec3 flat_color;
flat out uint styles;

uniform mat4 MVP;
uniform vec3 eye_origin;
uniform usampler1D face_visibility_sampler;

bool is_culled() {
    int byte_index = face_index;

    uint sampled = texelFetch(face_visibility_sampler, byte_index, 0).r;

    return sampled != 16u;
}

void main() {
    if (is_culled()) {
        gl_Position = vec4(0.0);
        return;
    }

    gl_Position = MVP * vec4(position, 1.0);
    fragment_world_pos = position;

    lightmap_uv = vertex_lightmap_uv;
    normal = vertex_normal;
    flat_color = vertex_flat_color;
    styles = vertex_styles;
}
)";

GLView::face_visibility_key_t GLView::desiredFaceVisibility() const
{
    face_visibility_key_t result;
    result.show_bmodels = m_showBmodels;

    if (m_visCulling) {
        const mbsp_t &bsp = *m_bsp;
        const auto &world = bsp.dmodels.at(0);

        auto *leaf =
            BSP_FindLeafAtPoint(&bsp, &world, qvec3d{m_cameraOrigin.x(), m_cameraOrigin.y(), m_cameraOrigin.z()});

        int leafnum = leaf - bsp.dleafs.data();

        result.leafnum = leafnum;

        if (bsp.loadversion->game->id == GAME_QUAKE_II) {
            result.clusternum = leaf->cluster;
        } else {
            result.clusternum = leaf->visofs;
        }
    } else {
        result.leafnum = -1;
        result.clusternum = -1;
    }
    return result;
}

void GLView::updateFaceVisibility()
{
    if (!m_bsp)
        return;

    const mbsp_t &bsp = *m_bsp;
    const auto &world = bsp.dmodels.at(0);

    const face_visibility_key_t desired = desiredFaceVisibility();

    if (m_uploaded_face_visibility &&
        *m_uploaded_face_visibility == desired) {
        qDebug() << "reusing last frame visdata";
        return;
    }

    qDebug() << "looking up pvs for clusternum " << desired.clusternum;

    const int face_visibility_width = m_bsp->dfaces.size();

    std::vector<uint8_t> face_flags;
    face_flags.resize(face_visibility_width, 0);

    bool in_solid = false;

    if (desired.leafnum == -1)
        in_solid = true;
    if (desired.clusternum == -1)
        in_solid = true;
    if (desired.leafnum == 0 && bsp.loadversion->game->id != GAME_QUAKE_II)
        in_solid = true;

    bool found_visdata = false;

    if (!in_solid) {
        if (auto it = m_decompressedVis.find(desired.clusternum); it != m_decompressedVis.end()) {
            found_visdata = true;

            const auto &pvs = it->second;
            qDebug() << "found bitvec of size " << pvs.size();

            // visit all world leafs: if they're visible, mark the appropriate faces
            BSP_VisitAllLeafs(bsp, bsp.dmodels[0], [&](const mleaf_t &leaf) {
                if (Pvs_LeafVisible(&bsp, pvs, &leaf)) {
                    for (int ms = 0; ms < leaf.nummarksurfaces; ++ms) {
                        int fnum = bsp.dleaffaces[leaf.firstmarksurface + ms];
                        face_flags[fnum] = 16;
                    }
                }
            });
        }
    }

    if (!found_visdata) {
        // mark all world faces
        for (int fi = world.firstface; fi < (world.firstface + world.numfaces); ++fi) {
            face_flags[fi] = 16;
        }
    }

    // set all bmodel faces to visible
    if (m_showBmodels) {
        for (int mi = 1; mi < bsp.dmodels.size(); ++mi) {
            auto &model = bsp.dmodels[mi];
            for (int fi = model.firstface; fi < (model.firstface + model.numfaces); ++fi) {
                face_flags[fi] = 16;
            }
        }
    }

    setFaceVisibilityArray(face_flags.data());

    m_uploaded_face_visibility = desired;
}

bool GLView::shouldLiveUpdate() const
{
    if (m_keysPressed)
        return true;

    if (QApplication::mouseButtons())
        return true;

    return false;
}

void GLView::handleLoggedMessage(const QOpenGLDebugMessage &debugMessage)
{
#ifdef _DEBUG
    if (debugMessage.type() == QOpenGLDebugMessage::ErrorType)
        __debugbreak();
#endif

    qDebug() << debugMessage.message();
}

void GLView::initializeGL()
{
    initializeOpenGLFunctions();

    QOpenGLDebugLogger *logger = new QOpenGLDebugLogger(this);

    logger->initialize(); // initializes in the current context, i.e. ctx

    connect(logger, &QOpenGLDebugLogger::messageLogged, this, &GLView::handleLoggedMessage);
    logger->startLogging(QOpenGLDebugLogger::SynchronousLogging);

    // set up shader

    m_program = new QOpenGLShaderProgram();
    m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, s_vertShader);
    m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, s_fragShader);
    assert(m_program->link());

    m_skybox_program = new QOpenGLShaderProgram();
    m_skybox_program->addShaderFromSourceCode(QOpenGLShader::Vertex, s_skyboxVertShader);
    m_skybox_program->addShaderFromSourceCode(QOpenGLShader::Fragment, s_skyboxFragShader);
    assert(m_skybox_program->link());

    m_program_simple = new QOpenGLShaderProgram();
    m_program_simple->addShaderFromSourceCode(QOpenGLShader::Vertex, s_vertShader_Simple);
    m_program_simple->addShaderFromSourceCode(QOpenGLShader::Fragment, s_fragShader_Simple);
    assert(m_program_simple->link());

    m_program_wireframe = new QOpenGLShaderProgram();
    m_program_wireframe->addShaderFromSourceCode(QOpenGLShader::Vertex, s_vertShader_Wireframe);
    m_program_wireframe->addShaderFromSourceCode(QOpenGLShader::Fragment, s_fragShader_Wireframe);
    assert(m_program_wireframe->link());

    m_program->bind();
    m_program_mvp_location = m_program->uniformLocation("MVP");
    m_program_texture_sampler_location = m_program->uniformLocation("texture_sampler");
    m_program_lightmap_sampler_location = m_program->uniformLocation("lightmap_sampler");
    m_program_face_visibility_sampler_location = m_program->uniformLocation("face_visibility_sampler");
    m_program_opacity_location = m_program->uniformLocation("opacity");
    m_program_alpha_test_location = m_program->uniformLocation("alpha_test");
    m_program_lightmap_only_location = m_program->uniformLocation("lightmap_only");
    m_program_fullbright_location = m_program->uniformLocation("fullbright");
    m_program_drawnormals_location = m_program->uniformLocation("drawnormals");
    m_program_drawflat_location = m_program->uniformLocation("drawflat");
    m_program_style_scalars_location = m_program->uniformLocation("style_scalars");
    m_program->release();

    m_skybox_program->bind();
    m_skybox_program_mvp_location = m_skybox_program->uniformLocation("MVP");
    m_skybox_program_eye_direction_location = m_skybox_program->uniformLocation("eye_origin");
    m_skybox_program_texture_sampler_location = m_skybox_program->uniformLocation("texture_sampler");
    m_skybox_program_lightmap_sampler_location = m_skybox_program->uniformLocation("lightmap_sampler");
    m_skybox_program_face_visibility_sampler_location = m_skybox_program->uniformLocation("face_visibility_sampler");
    m_skybox_program_opacity_location = m_skybox_program->uniformLocation("opacity");
    m_skybox_program_lightmap_only_location = m_skybox_program->uniformLocation("lightmap_only");
    m_skybox_program_fullbright_location = m_skybox_program->uniformLocation("fullbright");
    m_skybox_program_drawnormals_location = m_skybox_program->uniformLocation("drawnormals");
    m_skybox_program_drawflat_location = m_skybox_program->uniformLocation("drawflat");
    m_skybox_program_style_scalars_location = m_skybox_program->uniformLocation("style_scalars");
    m_skybox_program->release();

    m_program_wireframe->bind();
    m_program_wireframe_mvp_location = m_program_wireframe->uniformLocation("MVP");
    m_program_wireframe_face_visibility_sampler_location =
        m_program_wireframe->uniformLocation("face_visibility_sampler");
    m_program_wireframe->release();

    m_program_simple->bind();
    m_program_simple_mvp_location = m_program_simple->uniformLocation("MVP");
    m_program_simple_color_location = m_program_simple->uniformLocation("drawcolor");
    m_program_simple->release();

    m_vao.create();
    m_leakVao.create();
    m_portalVao.create();
    for (auto &hullVao : m_hullVaos) {
        hullVao.vao.create();
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_LINE_SMOOTH);
    glFrontFace(GL_CW);
}

void GLView::paintGL()
{
    // calculate frame time + update m_lastFrame
    float duration_seconds;

    const auto now = I_FloatTime();
    if (m_lastFrame) {
        duration_seconds = (now - *m_lastFrame).count();
    } else {
        duration_seconds = 0;
    }
    m_lastFrame = now;

    // apply motion
    applyMouseMotion();
    applyFlyMovement(duration_seconds);

    // update vis culling if needed
    updateFaceVisibility();

    // draw
    glClearColor(0.1, 0.1, 0.1, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    QMatrix4x4 modelMatrix;
    QMatrix4x4 viewMatrix;
    QMatrix4x4 projectionMatrix;
    projectionMatrix.perspective(90, m_displayAspect, 1.0f, 1'000'000.0f);
    viewMatrix.lookAt(m_cameraOrigin, m_cameraOrigin + m_cameraFwd, QVector3D(0, 0, 1));

    QMatrix4x4 MVP = projectionMatrix * viewMatrix * modelMatrix;

    QOpenGLShaderProgram *active_program = nullptr;

    m_program->bind();
    m_program->setUniformValue(m_program_mvp_location, MVP);
    m_program->setUniformValue(m_program_texture_sampler_location, 0 /* texture unit */);
    m_program->setUniformValue(m_program_lightmap_sampler_location, 1 /* texture unit */);
    m_program->setUniformValue(m_program_face_visibility_sampler_location, 2 /* texture unit */);
    m_program->setUniformValue(m_program_opacity_location, 1.0f);
    m_program->setUniformValue(m_program_alpha_test_location, false);
    m_program->setUniformValue(m_program_lightmap_only_location, m_lighmapOnly);
    m_program->setUniformValue(m_program_fullbright_location, m_fullbright);
    m_program->setUniformValue(m_program_drawnormals_location, m_drawNormals);
    m_program->setUniformValue(m_program_drawflat_location, m_drawFlat);

    m_skybox_program->bind();
    m_skybox_program->setUniformValue(m_skybox_program_mvp_location, MVP);
    m_skybox_program->setUniformValue(m_skybox_program_eye_direction_location, m_cameraOrigin);
    m_skybox_program->setUniformValue(m_skybox_program_texture_sampler_location, 0 /* texture unit */);
    m_skybox_program->setUniformValue(m_skybox_program_lightmap_sampler_location, 1 /* texture unit */);
    m_skybox_program->setUniformValue(m_skybox_program_face_visibility_sampler_location, 2 /* texture unit */);
    m_skybox_program->setUniformValue(m_skybox_program_opacity_location, 1.0f);
    m_skybox_program->setUniformValue(m_skybox_program_lightmap_only_location, m_lighmapOnly);
    m_skybox_program->setUniformValue(m_skybox_program_fullbright_location, m_fullbright);
    m_skybox_program->setUniformValue(m_skybox_program_drawnormals_location, m_drawNormals);
    m_skybox_program->setUniformValue(m_skybox_program_drawflat_location, m_drawFlat);

    // resolves whether to render a particular drawcall as opaque
    auto draw_as_opaque = [&](const drawcall_t &draw) -> bool {
        if (m_drawTranslucencyAsOpaque)
            return true;

        return draw.key.opacity == 1.0f;
    };

    // opaque draws
    for (auto &draw : m_drawcalls) {
        if (m_drawLeafs)
            break;

        if (!draw_as_opaque(draw))
            continue;

        if (active_program != draw.key.program) {
            active_program = draw.key.program;
            active_program->bind();
        }

        if (draw.key.alpha_test) {
            m_program->setUniformValue(m_program_alpha_test_location, true);
        } else {
            m_program->setUniformValue(m_program_alpha_test_location, false);
        }

        draw.texture->bind(0 /* texture unit */);
        lightmap_texture->bind(1 /* texture unit */);
        if (face_visibility_texture) {
            face_visibility_texture->bind(2 /* texture unit */);
        }

        if (active_program == m_program) {
            m_program->setUniformValue(m_program_opacity_location, 1.0f);
        } else {
            m_skybox_program->setUniformValue(m_skybox_program_opacity_location, 1.0f);
        }

        QOpenGLVertexArrayObject::Binder vaoBinder(&m_vao);

        glDrawElements(GL_TRIANGLES, draw.index_count, GL_UNSIGNED_INT,
            reinterpret_cast<void *>(draw.first_index * sizeof(uint32_t)));
    }

    // translucent draws
    if (!m_drawLeafs) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        for (auto &draw : m_drawcalls) {
            if (draw_as_opaque(draw))
                continue;

            if (active_program != draw.key.program) {
                active_program = draw.key.program;
                active_program->bind();
            }

            if (draw.key.alpha_test) {
                m_program->setUniformValue(m_program_alpha_test_location, true);
            } else {
                m_program->setUniformValue(m_program_alpha_test_location, false);
            }

            draw.texture->bind(0 /* texture unit */);
            lightmap_texture->bind(1 /* texture unit */);
            if (face_visibility_texture) {
                face_visibility_texture->bind(2 /* texture unit */);
            }

            if (active_program == m_program) {
                m_program->setUniformValue(m_program_opacity_location, draw.key.opacity);
            } else {
                m_skybox_program->setUniformValue(m_skybox_program_opacity_location, draw.key.opacity);
            }

            QOpenGLVertexArrayObject::Binder vaoBinder(&m_vao);

            glDrawElements(GL_TRIANGLES, draw.index_count, GL_UNSIGNED_INT,
                reinterpret_cast<void *>(draw.first_index * sizeof(uint32_t)));
        }

        glDisable(GL_BLEND);
    }

    m_program->release();

    // wireframe
    if (m_showTris || m_showTrisSeeThrough) {
        m_program_wireframe->bind();
        m_program_wireframe->setUniformValue(m_program_wireframe_mvp_location, MVP);
        m_program_wireframe->setUniformValue(
            m_program_wireframe_face_visibility_sampler_location, 2 /* texture unit */);

        if (face_visibility_texture) {
            face_visibility_texture->bind(2 /* texture unit */);
        }

        if (m_showTrisSeeThrough)
            glDisable(GL_DEPTH_TEST);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glEnable(GL_POLYGON_OFFSET_LINE);
        glPolygonOffset(-0.8, 1.0);

        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        QOpenGLVertexArrayObject::Binder vaoBinder(&m_vao);

        for (auto &draw : m_drawcalls) {
            glDrawElements(GL_TRIANGLES, draw.index_count, GL_UNSIGNED_INT,
                reinterpret_cast<void *>(draw.first_index * sizeof(uint32_t)));
        }
        if (m_showTrisSeeThrough)
            glEnable(GL_DEPTH_TEST);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glDisable(GL_POLYGON_OFFSET_FILL);
        glDisable(GL_POLYGON_OFFSET_LINE);

        m_program_wireframe->release();
    }

    if (m_drawLeak && num_leak_points) {
        m_program_simple->bind();
        m_program_simple->setUniformValue(m_program_simple_mvp_location, MVP);

        QOpenGLVertexArrayObject::Binder vaoBinder(&m_leakVao);

        glDrawArrays(GL_LINE_STRIP, 0, num_leak_points);

        m_program_simple->release();
    }

    if (m_drawPortals && num_portal_indices) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);

        m_program_simple->bind();
        m_program_simple->setUniformValue(m_program_simple_mvp_location, MVP);

        QOpenGLVertexArrayObject::Binder vaoBinder(&m_portalVao);

        glDisable(GL_CULL_FACE);

        glEnable(GL_PRIMITIVE_RESTART);
        glPrimitiveRestartIndex((GLuint)-1);

        m_program_wireframe->setUniformValue(m_program_simple_color_location, 1.0f, 0.4f, 0.4f, 0.2f);
        glDrawElements(GL_TRIANGLE_FAN, num_portal_indices, GL_UNSIGNED_INT, 0);
        m_program_wireframe->setUniformValue(m_program_simple_color_location, 1.0f, 1.f, 1.f, 0.2f);
        glDrawElements(GL_LINE_LOOP, num_portal_indices, GL_UNSIGNED_INT, 0);

        glDisable(GL_PRIMITIVE_RESTART);

        glEnable(GL_CULL_FACE);

        m_program_simple->release();

        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    }

    if (m_drawLeafs) {
        int hull = *m_drawLeafs;
        leaf_vao_t &vaodata = m_hullVaos[hull];
        if (vaodata.num_indices) {
            m_program_simple->bind();
            m_program_simple->setUniformValue(m_program_simple_mvp_location, MVP);

            QOpenGLVertexArrayObject::Binder vaoBinder(&vaodata.vao);

            glEnable(GL_PRIMITIVE_RESTART);
            glPrimitiveRestartIndex((GLuint)-1);

            m_program_simple->setUniformValue(m_program_simple_color_location, 1.0f, 0.4f, 0.4f, 0.2f);
            glDrawElements(GL_TRIANGLE_FAN, vaodata.num_indices, GL_UNSIGNED_INT, 0);

            m_program_simple->setUniformValue(m_program_simple_color_location, 1.0f, 1.f, 1.f, 0.2f);
            glDrawElements(GL_LINE_LOOP, vaodata.num_indices, GL_UNSIGNED_INT, 0);
            
            glDisable(GL_PRIMITIVE_RESTART);

            m_program_simple->release();
        }
    }

    if (shouldLiveUpdate()) {
        update(); // schedule the next frame
    } else {
        qDebug() << "pausing anims..";
        m_lastFrame = std::nullopt;
        m_lastMouseDownPos = std::nullopt;
    }
}

void GLView::setCamera(const qvec3d &origin, const qvec3d &fwd)
{
    m_cameraOrigin = {(float)origin[0], (float)origin[1], (float)origin[2]};
    m_cameraFwd = {(float)fwd[0], (float)fwd[1], (float)fwd[2]};
}

void GLView::setLighmapOnly(bool lighmapOnly)
{
    m_lighmapOnly = lighmapOnly;
    update();
}

void GLView::setFullbright(bool fullbright)
{
    m_fullbright = fullbright;
    update();
}

void GLView::setDrawNormals(bool drawnormals)
{
    m_drawNormals = drawnormals;
    update();
}

void GLView::setDrawLeafs(std::optional<int> hullnum)
{
    m_drawLeafs = hullnum;
    update();
}

void GLView::setShowTris(bool showtris)
{
    m_showTris = showtris;
    update();
}

void GLView::setShowTrisSeeThrough(bool showtris)
{
    m_showTrisSeeThrough = showtris;
    update();
}

void GLView::setVisCulling(bool viscull)
{
    m_visCulling = viscull;
    update();
}

void GLView::setDrawFlat(bool drawflat)
{
    m_drawFlat = drawflat;
    update();
}

void GLView::setKeepOrigin(bool keeporigin)
{
    m_keepOrigin = keeporigin;
}

void GLView::setDrawPortals(bool drawportals)
{
    m_drawPortals = drawportals;
    update();
}

void GLView::setDrawLeak(bool drawleak)
{
    m_drawLeak = drawleak;
    update();
}

void GLView::setLightStyleIntensity(int style_id, int intensity)
{
    makeCurrent();
    m_program->bind();
    m_program->setUniformValue(m_program_style_scalars_location + style_id, intensity / 100.f);
    m_program->release();
    doneCurrent();

    update();
}

void GLView::setMagFilter(QOpenGLTexture::Filter filter)
{
    m_filter = filter;

    if (placeholder_texture)
        placeholder_texture->setMagnificationFilter(m_filter);

    for (auto &dc : m_drawcalls) {
        dc.texture->setMagnificationFilter(m_filter);
    }

    update();
}

void GLView::setDrawTranslucencyAsOpaque(bool drawopaque)
{
    m_drawTranslucencyAsOpaque = drawopaque;
    update();
}

void GLView::setShowBmodels(bool bmodels)
{
    // force re-upload of face visibility
    m_uploaded_face_visibility = std::nullopt;
    m_showBmodels = bmodels;
    update();
}

void GLView::takeScreenshot(QString destPath, int w, int h)
{
    // update aspect ratio
    float backupDisplayAspect = m_displayAspect;
    m_displayAspect = static_cast<float>(w) / static_cast<float>(h);

    makeCurrent();
    {
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        format.setSamples(4);

        QOpenGLFramebufferObject fbo(w, h, format);
        assert(fbo.bind());

        glViewport(0, 0, w, h);
        paintGL();

        QImage image = fbo.toImage();
        image.save(destPath);

        assert(fbo.release());
    }
    doneCurrent();

    // restore aspect ratio
    m_displayAspect = backupDisplayAspect;
    update();
}

void GLView::setFaceVisibilityArray(uint8_t *data)
{
    // one byte per face
    int face_visibility_width = m_bsp->dfaces.size();

    face_visibility_texture.reset();

    face_visibility_texture = std::make_shared<QOpenGLTexture>(QOpenGLTexture::Target1D);
    face_visibility_texture->setSize(face_visibility_width);
    face_visibility_texture->setFormat(QOpenGLTexture::R8U);
    face_visibility_texture->setMagnificationFilter(QOpenGLTexture::Nearest);
    face_visibility_texture->setMinificationFilter(QOpenGLTexture::Nearest);
    face_visibility_texture->allocateStorage();

    face_visibility_texture->setData(
        0, QOpenGLTexture::Red_Integer, QOpenGLTexture::UInt8, reinterpret_cast<const void *>(data));

    logging::print("uploaded {} bytes face visibility texture", face_visibility_width);
}

void GLView::renderBSP(const QString &file, const mbsp_t &bsp, const bspxentries_t &bspx,
    const std::vector<entdict_t> &entities, const full_atlas_t &lightmap, const settings::common_settings &settings,
    bool use_bspx_normals)
{
    // copy the bsp for later use (FIXME: just store a pointer to MainWindow's?)
    m_bsp = bsp;
    if (bsp.dvis.bits.empty()) {
        logging::print("no visdata\n");
        m_decompressedVis.clear();
    } else {
        logging::print("decompressing visdata...\n");
        m_decompressedVis = DecompressAllVis(&bsp, true);
    }

    img::load_textures(&bsp, settings);

    std::optional<bspxfacenormals> facenormals;

    if (use_bspx_normals)
        facenormals = BSPX_FaceNormals(bsp, bspx);

    // NOTE: according to https://doc.qt.io/qt-6/qopenglwidget.html#resource-initialization-and-cleanup
    // we can only do this after `initializeGL()` has run once.
    makeCurrent();

    // clear old data
    placeholder_texture.reset();
    lightmap_texture.reset();
    face_visibility_texture.reset();
    m_drawcalls.clear();
    m_vbo.bind();
    m_vbo.allocate(0);
    m_leakVbo.bind();
    m_leakVbo.allocate(0);
    m_indexBuffer.bind();
    m_indexBuffer.allocate(0);
    m_portalVbo.bind();
    m_portalVbo.allocate(0);
    m_portalIndexBuffer.bind();
    m_portalIndexBuffer.allocate(0);
    for (auto &hullVao : m_hullVaos) {
        hullVao.vbo.bind();
        hullVao.vbo.allocate(0);
        hullVao.indexBuffer.bind();
        hullVao.indexBuffer.allocate(0);
    }

    num_leak_points = 0;
    num_portal_indices = 0;
    m_uploaded_face_visibility = std::nullopt;

    int32_t highest_depth = 0;

    for (auto &style : lightmap.style_to_lightmap_atlas) {
        highest_depth = std::max(highest_depth, style.first);
    }

    // upload lightmap atlases
    {
        // FIXME: empty map access if there are no lightmaps
        const auto &lm_tex = lightmap.style_to_lightmap_atlas.begin()->second;

        lightmap_texture = std::make_shared<QOpenGLTexture>(QOpenGLTexture::Target2DArray);
        lightmap_texture->setSize(lm_tex.width, lm_tex.height);
        lightmap_texture->setLayers(highest_depth + 1);
        lightmap_texture->setFormat(QOpenGLTexture::TextureFormat::RGBA8_UNorm);
        lightmap_texture->setAutoMipMapGenerationEnabled(false);
        lightmap_texture->setMagnificationFilter(QOpenGLTexture::Linear);
        lightmap_texture->setMinificationFilter(QOpenGLTexture::Linear);
        lightmap_texture->allocateStorage();

        for (auto &style : lightmap.style_to_lightmap_atlas) {
            lightmap_texture->setData(0, style.first, QOpenGLTexture::RGBA, QOpenGLTexture::UInt8,
                reinterpret_cast<const void *>(style.second.pixels.data()));
        }
    }

    // upload placeholder texture
    {
        placeholder_texture = std::make_shared<QOpenGLTexture>(QOpenGLTexture::Target2D);
        placeholder_texture->setSize(64, 64);
        placeholder_texture->setFormat(QOpenGLTexture::TextureFormat::RGBA8_UNorm);
        placeholder_texture->setAutoMipMapGenerationEnabled(true);
        placeholder_texture->setMagnificationFilter(m_filter);
        placeholder_texture->setMinificationFilter(QOpenGLTexture::Linear);
        placeholder_texture->allocateStorage();

        uint8_t *data = new uint8_t[64 * 64 * 4];
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                int i = ((y * 64) + x) * 4;

                int v;
                if ((x > 32) == (y > 32)) {
                    v = 64;
                } else {
                    v = 32;
                }

                data[i] = v; // R
                data[i + 1] = v; // G
                data[i + 2] = v; // B
                data[i + 3] = 0xff; // A
            }
        }
        placeholder_texture->setData(
            0, QOpenGLTexture::RGBA, QOpenGLTexture::UInt8, reinterpret_cast<const void *>(data));
        delete[] data;
    }

    struct face_payload
    {
        const mface_t *face;
        qvec3d model_offset;
    };

    // collect faces grouped by material_key
    std::map<material_key, std::vector<face_payload>> faces_by_material_key;

    bool needs_skybox = false;

    // collect entity bmodels
    for (int mi = 0; mi < bsp.dmodels.size(); mi++) {
        qvec3d origin{};

        if (mi != 0) {
            // find matching entity
            std::string modelStr = fmt::format("*{}", mi);
            bool found = false;

            for (auto &ent : entities) {
                if (ent.get("model") == modelStr) {
                    found = true;
                    ent.get_vector("origin", origin);
                    break;
                }
            }

            if (!found)
                continue;
        }

        auto &m = bsp.dmodels[mi];

        for (int i = m.firstface; i < m.firstface + m.numfaces; ++i) {
            auto &f = bsp.dfaces[i];
            std::string t = Face_TextureName(&bsp, &f);
            if (f.numedges < 3)
                continue;

            const mtexinfo_t *texinfo = Face_Texinfo(&bsp, &f);
            if (!texinfo)
                continue; // FIXME: render as checkerboard?

            QOpenGLShaderProgram *program = m_program;

            // determine opacity
            float opacity = 1.0f;
            bool alpha_test = false;

            if (bsp.loadversion->game->id == GAME_QUAKE_II) {

                if (texinfo->flags.native & Q2_SURF_NODRAW) {
                    continue;
                }

                if (texinfo->flags.native & Q2_SURF_SKY) {
                    program = m_skybox_program;
                    needs_skybox = true;
                } else {
                    if (texinfo->flags.native & Q2_SURF_TRANS33) {
                        opacity = 0.33f;
                    }
                    if (texinfo->flags.native & Q2_SURF_TRANS66) {
                        opacity = 0.66f;
                    }

                    if (texinfo->flags.native & Q2_SURF_ALPHATEST) {
                        alpha_test = true;
                    }
                }
            }

            material_key k = {.program = program, .texname = t, .opacity = opacity, .alpha_test = alpha_test};
            faces_by_material_key[k].push_back({.face = &f, .model_offset = origin});
        }
    }

    std::shared_ptr<QOpenGLTexture> skybox_texture;

    if (needs_skybox) {
        // load skybox
        std::string skybox = "unit1_"; // TODO: game-specific defaults

        if (entities[0].has("sky")) {
            skybox = entities[0].get("sky");
        }

        skybox_texture = std::make_shared<QOpenGLTexture>(QOpenGLTexture::TargetCubeMap);

        {
            QImage up_img;
            {
                auto up =
                    img::load_texture(fmt::format("env/{}up", skybox), false, bsp.loadversion->game, settings, true);
                up_img = QImage((const uchar *)std::get<0>(up)->pixels.data(), std::get<0>(up)->width,
                    std::get<0>(up)->height, QImage::Format_RGB32);
                up_img = std::move(up_img.transformed(QTransform().rotate(-90.0)).mirrored(false, true));
            }

            skybox_texture->setSize(up_img.width(), up_img.height());
            skybox_texture->setFormat(QOpenGLTexture::TextureFormat::RGBA8_UNorm);
            skybox_texture->setAutoMipMapGenerationEnabled(true);
            skybox_texture->setMagnificationFilter(m_filter);
            skybox_texture->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
            skybox_texture->setMaximumAnisotropy(16);
            skybox_texture->allocateStorage();

            skybox_texture->setWrapMode(QOpenGLTexture::ClampToEdge);

            skybox_texture->setData(0, 0, QOpenGLTexture::CubeMapPositiveZ, QOpenGLTexture::RGBA, QOpenGLTexture::UInt8,
                up_img.constBits(), nullptr);
        }
        {
            QImage down_img;
            {
                auto down =
                    img::load_texture(fmt::format("env/{}dn", skybox), false, bsp.loadversion->game, settings, true);
                down_img = QImage((const uchar *)std::get<0>(down)->pixels.data(), std::get<0>(down)->width,
                    std::get<0>(down)->height, QImage::Format_RGB32);
                down_img = std::move(down_img.transformed(QTransform().rotate(90.0)).mirrored(true, false));
            }

            skybox_texture->setData(0, 0, QOpenGLTexture::CubeMapNegativeZ, QOpenGLTexture::RGBA, QOpenGLTexture::UInt8,
                down_img.constBits(), nullptr);
        }
        {
            QImage left_img;
            {
                auto left =
                    img::load_texture(fmt::format("env/{}lf", skybox), false, bsp.loadversion->game, settings, true);
                left_img = QImage((const uchar *)std::get<0>(left)->pixels.data(), std::get<0>(left)->width,
                    std::get<0>(left)->height, QImage::Format_RGB32);
                left_img = std::move(left_img.transformed(QTransform().rotate(-90.0)).mirrored(true, false));
            }
            skybox_texture->setData(0, 0, QOpenGLTexture::CubeMapNegativeX, QOpenGLTexture::RGBA, QOpenGLTexture::UInt8,
                left_img.constBits(), nullptr);
        }
        {
            QImage right_img;
            {
                auto right =
                    img::load_texture(fmt::format("env/{}rt", skybox), false, bsp.loadversion->game, settings, true);
                right_img = QImage((const uchar *)std::get<0>(right)->pixels.data(), std::get<0>(right)->width,
                    std::get<0>(right)->height, QImage::Format_RGB32);
                right_img = std::move(right_img.transformed(QTransform().rotate(90.0)).mirrored(true, false));
            }
            skybox_texture->setData(0, 0, QOpenGLTexture::CubeMapPositiveX, QOpenGLTexture::RGBA, QOpenGLTexture::UInt8,
                right_img.constBits(), nullptr);
        }
        {
            QImage front_img;
            {
                auto front =
                    img::load_texture(fmt::format("env/{}ft", skybox), false, bsp.loadversion->game, settings, true);
                front_img = QImage((const uchar *)std::get<0>(front)->pixels.data(), std::get<0>(front)->width,
                    std::get<0>(front)->height, QImage::Format_RGB32);
                front_img = front_img.mirrored(true, false);
            }
            skybox_texture->setData(0, 0, QOpenGLTexture::CubeMapNegativeY, QOpenGLTexture::RGBA, QOpenGLTexture::UInt8,
                front_img.constBits(), nullptr);
        }
        {
            QImage back_img;
            {
                auto back =
                    img::load_texture(fmt::format("env/{}bk", skybox), false, bsp.loadversion->game, settings, true);
                back_img = QImage((const uchar *)std::get<0>(back)->pixels.data(), std::get<0>(back)->width,
                    std::get<0>(back)->height, QImage::Format_RGB32);
                back_img = std::move(back_img.transformed(QTransform().rotate(-180.0)).mirrored(true, false));
            }
            skybox_texture->setData(0, 0, QOpenGLTexture::CubeMapPositiveY, QOpenGLTexture::RGBA, QOpenGLTexture::UInt8,
                back_img.constBits(), nullptr);
        }
    }

    // populate the vertex/index buffers
    struct vertex_t
    {
        qvec3f pos;
        qvec2f uv;
        qvec2f lightmap_uv;
        qvec3f normal;
        qvec3f flat_color;
        uint32_t styles;
        int32_t face_index;
    };
    std::vector<vertex_t> verts;
    std::vector<uint32_t> indexBuffer;

    for (const auto &[k, faces] : faces_by_material_key) {
        // upload texture
        // FIXME: we should have a separate lightpreview_options
        auto *texture = img::find(k.texname);
        std::shared_ptr<QOpenGLTexture> qtexture;

        if (!texture) {
            logging::print("warning, couldn't locate {}", k.texname);
            qtexture = placeholder_texture;
        }

        if (!texture->width || !texture->height) {
            logging::print("warning, empty texture {}", k.texname);
            qtexture = placeholder_texture;
        }

        if (texture->pixels.empty()) {
            logging::print("warning, empty texture pixels {}", k.texname);
            qtexture = placeholder_texture;
        }

        const size_t dc_first_index = indexBuffer.size();

        if (k.program == m_skybox_program) {
            qtexture = skybox_texture;
        }

        for (const auto &[f, model_offset] : faces) {
            const int fnum = Face_GetNum(&bsp, f);
            const auto plane_normal = Face_Normal(&bsp, f);
            const qvec3f flat_color = qvec3f{Random(), Random(), Random()};

            const size_t first_vertex_of_face = verts.size();

            const auto lm_uvs = lightmap.facenum_to_lightmap_uvs.at(fnum);

            // output a vertex for each vertex of the face
            for (int j = 0; j < f->numedges; ++j) {
                qvec3f pos = Face_PointAtIndex(&bsp, f, j);
                qvec2f uv = Face_WorldToTexCoord(&bsp, f, pos);

                if (qtexture) {
                    uv[0] *= (1.0 / qtexture->width());
                    uv[1] *= (1.0 / qtexture->height());
                } else {
                    uv[0] *= (1.0 / texture->width);
                    uv[1] *= (1.0 / texture->height);
                }

                qvec2f lightmap_uv = lm_uvs.at(j);

                qvec3f vertex_normal;
                if (facenormals) {
                    auto normal_index = facenormals->per_face[fnum].per_vert[j].normal;
                    vertex_normal = facenormals->normals[normal_index];
                } else {
                    vertex_normal = plane_normal;
                }

                verts.push_back({.pos = pos + model_offset,
                    .uv = uv,
                    .lightmap_uv = lightmap_uv,
                    .normal = vertex_normal,
                    .flat_color = flat_color,
                    .styles = (uint32_t)(f->styles[0]) | (uint32_t)(f->styles[1] << 8) |
                              (uint32_t)(f->styles[2] << 16) | (uint32_t)(f->styles[3] << 24),
                    .face_index = fnum});
            }

            // output the vertex indices for this face
            for (int j = 2; j < f->numedges; ++j) {
                indexBuffer.push_back(first_vertex_of_face);
                indexBuffer.push_back(first_vertex_of_face + j - 1);
                indexBuffer.push_back(first_vertex_of_face + j);
            }
        }

        if (!qtexture) {
            qtexture = std::make_shared<QOpenGLTexture>(QOpenGLTexture::Target2D);

            int mipLevels = GetMipLevelsForDimensions(texture->width, texture->height);

            qtexture->setFormat(QOpenGLTexture::TextureFormat::RGBA8_UNorm);
            qtexture->setSize(texture->width, texture->height);
            qtexture->setMipLevels(mipLevels);

            qtexture->allocateStorage(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8);

            qtexture->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
            qtexture->setMagnificationFilter(m_filter);
            qtexture->setMaximumAnisotropy(16);

            qtexture->setData(
                QOpenGLTexture::RGBA, QOpenGLTexture::UInt8, reinterpret_cast<const void *>(texture->pixels.data()));
        }

        const size_t dc_index_count = indexBuffer.size() - dc_first_index;

        drawcall_t dc = {
            .key = k, .texture = std::move(qtexture), .first_index = dc_first_index, .index_count = dc_index_count};
        m_drawcalls.push_back(std::move(dc));
    }

    {
        QOpenGLVertexArrayObject::Binder vaoBinder(&m_vao);

        // upload index buffer
        m_indexBuffer.create();
        m_indexBuffer.bind();
        m_indexBuffer.allocate(indexBuffer.data(), indexBuffer.size() * sizeof(indexBuffer[0]));

        // upload vertex buffer
        m_vbo.create();
        m_vbo.bind();
        m_vbo.allocate(verts.data(), verts.size() * sizeof(verts[0]));

        // positions
        glEnableVertexAttribArray(0 /* attrib */);
        glVertexAttribPointer(0 /* attrib */, 3, GL_FLOAT, GL_FALSE, sizeof(vertex_t), (void *)offsetof(vertex_t, pos));

        // texture uvs
        glEnableVertexAttribArray(1 /* attrib */);
        glVertexAttribPointer(1 /* attrib */, 2, GL_FLOAT, GL_FALSE, sizeof(vertex_t), (void *)offsetof(vertex_t, uv));

        // lightmap uvs
        glEnableVertexAttribArray(2 /* attrib */);
        glVertexAttribPointer(
            2 /* attrib */, 2, GL_FLOAT, GL_FALSE, sizeof(vertex_t), (void *)offsetof(vertex_t, lightmap_uv));

        // normals
        glEnableVertexAttribArray(3 /* attrib */);
        glVertexAttribPointer(
            3 /* attrib */, 3, GL_FLOAT, GL_FALSE, sizeof(vertex_t), (void *)offsetof(vertex_t, normal));

        // flat shading color
        glEnableVertexAttribArray(4 /* attrib */);
        glVertexAttribPointer(
            4 /* attrib */, 3, GL_FLOAT, GL_FALSE, sizeof(vertex_t), (void *)offsetof(vertex_t, flat_color));

        // styles
        glEnableVertexAttribArray(5 /* attrib */);
        glVertexAttribIPointer(
            5 /* attrib */, 1, GL_UNSIGNED_INT, sizeof(vertex_t), (void *)offsetof(vertex_t, styles));

        // face indices
        glEnableVertexAttribArray(6 /* attrib */);
        glVertexAttribIPointer(6 /* attrib */, 1, GL_INT, sizeof(vertex_t), (void *)offsetof(vertex_t, face_index));
    }

    // initialize style values
    m_program->bind();
    for (int i = 0; i < 256; i++) {
        m_program->setUniformValue(m_program_style_scalars_location + i, 1.f);
    }
    m_program->release();

    // load leak file
    fs::path leakFile = fs::path(file.toStdString()).replace_extension(".pts");

    if (!fs::exists(leakFile)) {
        leakFile = fs::path(file.toStdString()).replace_extension(".lin");
    }

    // populate the vertex/index buffers
    struct simple_vertex_t
    {
        qvec3f pos;
    };

    if (fs::exists(leakFile)) {
        QOpenGLVertexArrayObject::Binder leakVaoBinder(&m_leakVao);

        std::ifstream f(leakFile);
        std::vector<simple_vertex_t> points;

        while (!f.eof()) {
            std::string line;
            std::getline(f, line);

            if (line.empty()) {
                break;
            }

            auto s = QString::fromStdString(line);
            auto split = s.split(' ');

            double x = split[0].toDouble();
            double y = split[1].toDouble();
            double z = split[2].toDouble();

            points.push_back(simple_vertex_t{qvec3f{(float)x, (float)y, (float)z}});

            num_leak_points++;
        }

        // upload vertex buffer
        m_leakVbo.create();
        m_leakVbo.bind();
        m_leakVbo.allocate(points.data(), points.size() * sizeof(points[0]));

        // positions
        glEnableVertexAttribArray(0 /* attrib */);
        glVertexAttribPointer(
            0 /* attrib */, 3, GL_FLOAT, GL_FALSE, sizeof(simple_vertex_t), (void *)offsetof(simple_vertex_t, pos));
    }

    // load portal file
    fs::path portalFile = fs::path(file.toStdString()).replace_extension(".prt");

    if (fs::exists(portalFile)) {
        QOpenGLVertexArrayObject::Binder portalVaoBinder(&m_portalVao);

        auto prt = LoadPrtFile(portalFile, bsp.loadversion);
        std::vector<GLuint> indices;
        std::vector<simple_vertex_t> points;

        [[maybe_unused]] size_t total_points = 0;
        [[maybe_unused]] size_t total_indices = 0;
        size_t current_index = 0;

        for (auto &portal : prt.portals) {
            total_points += portal.winding.size();
            total_indices += portal.winding.size() + 1;

            for (auto &pt : portal.winding) {
                indices.push_back(current_index++);
                points.push_back(simple_vertex_t{qvec3f{pt}});
            }

            indices.push_back((GLuint)-1);
        }

        // upload index buffer
        m_portalIndexBuffer.create();
        m_portalIndexBuffer.bind();
        m_portalIndexBuffer.allocate(indices.data(), indices.size() * sizeof(indices[0]));

        num_portal_indices = indices.size();

        // upload vertex buffer
        m_portalVbo.create();
        m_portalVbo.bind();
        m_portalVbo.allocate(points.data(), points.size() * sizeof(points[0]));

        // positions
        glEnableVertexAttribArray(0 /* attrib */);
        glVertexAttribPointer(
            0 /* attrib */, 3, GL_FLOAT, GL_FALSE, sizeof(simple_vertex_t), (void *)offsetof(simple_vertex_t, pos));
    }

    // load decompiled hulls
    // TODO: support decompiling bmodels other than the world

    for (int hullnum = 0; ; ++hullnum) {
        if (hullnum >= 1) {
            // check if hullnum 1 or higher is valid for this bsp (hull0 is always present); it's slightly involved
            if (bsp.loadversion->game->id == GAME_QUAKE_II) {
                break;
            }
            if (hullnum >= bsp.dmodels[0].headnode.size())
                break;
            // 0 is valid for hull 0, and hull 1 (where it refers to clipnode 0)
            if (hullnum >= 2 && bsp.dmodels[0].headnode[hullnum] == 0)
                break;
            // must be valid...
        }

        // decompile the hull
        auto leaf_visuals = VisualizeLeafs(bsp, 0, hullnum);

        auto &vao = m_hullVaos[hullnum];

        QOpenGLVertexArrayObject::Binder hullVaoBinder(&vao.vao);

        std::vector<GLuint> indices;
        std::vector<simple_vertex_t> points;

        for (const auto &leaf : leaf_visuals) {
            if (leaf.contents.is_empty(bsp.loadversion->game))
                continue;

            for (const auto &winding : leaf.windings) {
                // output a vertex + index for each vertex of the face
                for (int j = 0; j < winding.size(); ++j) {
                    indices.push_back(points.size());
                    points.push_back({.pos = winding[j]});
                }

                // use primitive restarts so we can draw the same
                // vertex/index buffer as either line loop or triangle fans
                indices.push_back((GLuint)-1);
            }
        }

        // upload index buffer
        vao.indexBuffer.create();
        vao.indexBuffer.bind();
        vao.indexBuffer.allocate(indices.data(), indices.size() * sizeof(indices[0]));

        vao.num_indices = indices.size();

        logging::print("set up leaf vao for {} with {} indices vao indices {}", hullnum, vao.num_indices, indices.size());

        // upload vertex buffer
        vao.vbo.create();
        vao.vbo.bind();
        vao.vbo.allocate(points.data(), points.size() * sizeof(points[0]));

        // positions
        glEnableVertexAttribArray(0 /* attrib */);
        glVertexAttribPointer(
            0 /* attrib */, 3, GL_FLOAT, GL_FALSE, sizeof(simple_vertex_t), (void *)offsetof(simple_vertex_t, pos));
    }

    doneCurrent();

    // schedule repaint
    update();
}

void GLView::resizeGL(int width, int height)
{
    m_displayAspect = static_cast<float>(width) / static_cast<float>(height);
}

void GLView::applyMouseMotion()
{
    if (!(QApplication::mouseButtons() & Qt::RightButton)) {
        m_lastMouseDownPos = std::nullopt;
        return;
    }

    QPoint current_pos = QCursor::pos();
    QPointF delta;
    if (m_lastMouseDownPos) {
        delta = current_pos - *m_lastMouseDownPos;
    } else {
        delta = QPointF(0, 0);
    }
    m_lastMouseDownPos = current_pos;

    // handle mouse movement
    float pitchDegrees = delta.y() * -0.2;
    float yawDegrees = delta.x() * -0.2;

    QMatrix4x4 mouseRotation;
    mouseRotation.rotate(pitchDegrees, cameraRight());
    mouseRotation.rotate(yawDegrees, QVector3D(0, 0, 1));

    // now rotate m_cameraFwd and m_cameraUp by mouseRotation
    m_cameraFwd = mouseRotation * m_cameraFwd;
}

static keys_t Qt_Key_To_keys_t(int key)
{
    switch (key) {
        case Qt::Key_W: return keys_t::up;
        case Qt::Key_A: return keys_t::left;
        case Qt::Key_S: return keys_t::down;
        case Qt::Key_D: return keys_t::right;
        case Qt::Key_Q: return keys_t::fly_down;
        case Qt::Key_E: return keys_t::fly_up;
    }
    return keys_t::none;
}

void GLView::keyPressEvent(QKeyEvent *event)
{
    keys_t key = Qt_Key_To_keys_t(event->key());

    m_keysPressed |= static_cast<uint32_t>(key);
}

void GLView::keyReleaseEvent(QKeyEvent *event)
{
    keys_t key = Qt_Key_To_keys_t(event->key());

    m_keysPressed &= ~static_cast<uint32_t>(key);
}

void GLView::wheelEvent(QWheelEvent *event)
{
    if (!(event->buttons() & Qt::RightButton))
        return;

    double delta = event->angleDelta().y();

    m_moveSpeed += delta;
    m_moveSpeed = std::clamp(m_moveSpeed, 10.0f, 5000.0f);
}

void GLView::mousePressEvent(QMouseEvent *event)
{
    update();
}

void GLView::applyFlyMovement(float duration_seconds)
{
    // qDebug() << "timer event: duration: " << duration_seconds;

    const float distance = m_moveSpeed * duration_seconds;

    const auto prevOrigin = m_cameraOrigin;

    if (m_keysPressed & static_cast<uint32_t>(keys_t::up))
        m_cameraOrigin += m_cameraFwd * distance;
    if (m_keysPressed & static_cast<uint32_t>(keys_t::down))
        m_cameraOrigin -= m_cameraFwd * distance;
    if (m_keysPressed & static_cast<uint32_t>(keys_t::left))
        m_cameraOrigin -= cameraRight() * distance;
    if (m_keysPressed & static_cast<uint32_t>(keys_t::right))
        m_cameraOrigin += cameraRight() * distance;
    if (m_keysPressed & static_cast<uint32_t>(keys_t::fly_down))
        m_cameraOrigin -= QVector3D(0, 0, 1) * distance;
    if (m_keysPressed & static_cast<uint32_t>(keys_t::fly_up))
        m_cameraOrigin += QVector3D(0, 0, 1) * distance;

    if (prevOrigin != m_cameraOrigin) {
        emit cameraMoved();
    }
}

qvec3f GLView::cameraPosition() const
{
    return qvec3f{m_cameraOrigin[0], m_cameraOrigin[1], m_cameraOrigin[2]};
}