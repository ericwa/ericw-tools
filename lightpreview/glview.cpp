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

#include <common/bspfile.hh>
#include <common/bsputils.hh>
#include <common/bspinfo.hh>
#include <common/imglib.hh>
#include <light/light.hh>

GLView::GLView(QWidget *parent)
    : QOpenGLWidget(parent),
      m_keysPressed(0),
      m_keymoveUpdateTimer(0),
      m_lastMouseDownPos(0, 0),
      m_moveSpeed(1000),
      m_displayAspect(1),
      m_cameraOrigin(0, 0, 0),
      m_cameraFwd(0, 1, 0),
      m_vao(),
      m_indexBuffer(QOpenGLBuffer::IndexBuffer)
{
    setFocusPolicy(Qt::StrongFocus); // allow keyboard focus
}

GLView::~GLView()
{
    makeCurrent();

    delete m_program;
    delete m_program_wireframe;

    m_vbo.destroy();
    m_indexBuffer.destroy();
    m_vao.destroy();

    lightmap_texture.reset();
    m_drawcalls.clear();

    doneCurrent();
}

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

uniform mat4 MVP;

void main() {
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
        vec3 lmcolor = fullbright ? vec3(0.5) : vec3(0);

        if (!fullbright)
        {
            for (uint i = 0u; i < 32u; i += 8u)
            {
                uint style = (styles >> i) & 0xFFu;

                if (style == 0xFFu)
                    break;

                lmcolor += texture(lightmap_sampler, vec3(lightmap_uv, (float) style)).rgb * style_scalars[style];
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

out vec2 uv;
out vec2 lightmap_uv;
out vec3 normal;
flat out vec3 flat_color;
flat out uint styles;

uniform mat4 MVP;

void main() {
    gl_Position = MVP * vec4(position.x, position.y, position.z, 1.0);

    uv = vertex_uv;
    lightmap_uv = vertex_lightmap_uv;
    normal = vertex_normal;
    flat_color = vertex_flat_color;
    styles = vertex_styles;
}
)";

void GLView::handleLoggedMessage(const QOpenGLDebugMessage &debugMessage)
{
    qDebug() << debugMessage.message();
}

void GLView::initializeGL()
{
    initializeOpenGLFunctions();

    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    QOpenGLDebugLogger *logger = new QOpenGLDebugLogger(this);

    logger->initialize(); // initializes in the current context, i.e. ctx

    connect(logger, &QOpenGLDebugLogger::messageLogged, this, &GLView::handleLoggedMessage);
    logger->startLogging();

    // set up shader

    m_program = new QOpenGLShaderProgram();
    m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, s_vertShader);
    m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, s_fragShader);
    assert(m_program->link());

    m_program_wireframe = new QOpenGLShaderProgram();
    m_program_wireframe->addShaderFromSourceCode(QOpenGLShader::Vertex, s_vertShader_Wireframe);
    m_program_wireframe->addShaderFromSourceCode(QOpenGLShader::Fragment, s_fragShader_Wireframe);
    assert(m_program_wireframe->link());

    m_program->bind();
    m_program_mvp_location = m_program->uniformLocation("MVP");
    m_program_texture_sampler_location = m_program->uniformLocation("texture_sampler");
    m_program_lightmap_sampler_location = m_program->uniformLocation("lightmap_sampler");
    m_program_opacity_location = m_program->uniformLocation("opacity");
    m_program_lightmap_only_location = m_program->uniformLocation("lightmap_only");
    m_program_fullbright_location = m_program->uniformLocation("fullbright");
    m_program_drawnormals_location = m_program->uniformLocation("drawnormals");
    m_program_drawflat_location = m_program->uniformLocation("drawflat");
    m_program_style_scalars_location = m_program->uniformLocation("style_scalars");
    m_program->release();

    m_program_wireframe->bind();
    m_program_wireframe_mvp_location = m_program_wireframe->uniformLocation("MVP");
    m_program_wireframe->release();

    m_vao.create();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glFrontFace(GL_CW);
}

void GLView::paintGL()
{
    // draw
    glClearColor(0.1, 0.1, 0.1, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    QMatrix4x4 modelMatrix;
    QMatrix4x4 viewMatrix;
    QMatrix4x4 projectionMatrix;
    projectionMatrix.perspective(90, m_displayAspect, 1.0f, 1'000'000.0f);
    viewMatrix.lookAt(m_cameraOrigin, m_cameraOrigin + m_cameraFwd, QVector3D(0, 0, 1));

    QMatrix4x4 MVP = projectionMatrix * viewMatrix * modelMatrix;

    // wireframe
    if (m_showTris) {
        m_program_wireframe->bind();
        m_program_wireframe->setUniformValue(m_program_wireframe_mvp_location, MVP);

        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        QOpenGLVertexArrayObject::Binder vaoBinder(&m_vao);

        for (auto &draw : m_drawcalls) {
            glDrawElements(GL_TRIANGLES, draw.index_count, GL_UNSIGNED_INT,
                reinterpret_cast<void *>(draw.first_index * sizeof(uint32_t)));
        }
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        m_program_wireframe->release();
    }

    m_program->bind();
    m_program->setUniformValue(m_program_mvp_location, MVP);
    m_program->setUniformValue(m_program_texture_sampler_location, 0 /* texture unit */);
    m_program->setUniformValue(m_program_lightmap_sampler_location, 1 /* texture unit */);
    m_program->setUniformValue(m_program_opacity_location, 1.0f);
    m_program->setUniformValue(m_program_lightmap_only_location, m_lighmapOnly);
    m_program->setUniformValue(m_program_fullbright_location, m_fullbright);
    m_program->setUniformValue(m_program_drawnormals_location, m_drawNormals);
    m_program->setUniformValue(m_program_drawflat_location, m_drawFlat);

    for (int i = 0; i < 256; i++) {
        m_program->setUniformValue(m_program_style_scalars_location + i, 1.f);
    }

    // opaque draws
    for (auto &draw : m_drawcalls) {
        if (draw.opacity != 1.0f)
            continue;

        draw.texture->bind(0 /* texture unit */);
        lightmap_texture->bind(1 /* texture unit */);

        QOpenGLVertexArrayObject::Binder vaoBinder(&m_vao);

        glDrawElements(GL_TRIANGLES, draw.index_count, GL_UNSIGNED_INT,
            reinterpret_cast<void *>(draw.first_index * sizeof(uint32_t)));
    }

    // translucent draws
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        for (auto &draw : m_drawcalls) {
            if (draw.opacity == 1.0f)
                continue;

            draw.texture->bind(0 /* texture unit */);
            lightmap_texture->bind(1 /* texture unit */);

            m_program->setUniformValue(m_program_opacity_location, draw.opacity);

            QOpenGLVertexArrayObject::Binder vaoBinder(&m_vao);

            glDrawElements(GL_TRIANGLES, draw.index_count, GL_UNSIGNED_INT,
                reinterpret_cast<void *>(draw.first_index * sizeof(uint32_t)));
        }

        glDisable(GL_BLEND);
    }

    m_program->release();
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

void GLView::setShowTris(bool showtris)
{
    m_showTris = showtris;
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

void GLView::setLightStyleIntensity(int style_id, int intensity)
{
    makeCurrent();
    m_program->bind();
    m_program->setUniformValue(m_program_style_scalars_location + style_id, intensity / 200.f);
    m_program->release();
    doneCurrent();

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

void GLView::renderBSP(const QString &file, const mbsp_t &bsp, const bspxentries_t &bspx,
    const std::vector<entdict_t> &entities, const full_atlas_t &lightmap, const settings::common_settings &settings)
{
    img::load_textures(&bsp, settings);

    auto facenormals = BSPX_FaceNormals(bsp, bspx);

    // NOTE: according to https://doc.qt.io/qt-6/qopenglwidget.html#resource-initialization-and-cleanup
    // we can only do this after `initializeGL()` has run once.
    makeCurrent();

    // clear old data
    lightmap_texture.reset();
    m_drawcalls.clear();
    m_vbo.allocate(0);
    m_indexBuffer.allocate(0);

    int32_t highest_depth = 0;

    for (auto &style : lightmap.style_to_lightmap_atlas) {
        highest_depth = max(highest_depth, style.first);
    }

    // upload lightmap atlases
    {
        const auto &lm_tex = lightmap.style_to_lightmap_atlas.begin()->second;

        lightmap_texture =
            std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2DArray);
        lightmap_texture->setSize(lm_tex.width, lm_tex.height);
        lightmap_texture->setLayers(highest_depth + 1);

        lightmap_texture->setAutoMipMapGenerationEnabled(false);
        lightmap_texture->setMagnificationFilter(QOpenGLTexture::Linear);
        lightmap_texture->setMinificationFilter(QOpenGLTexture::Linear);

        lightmap_texture->setFormat(QOpenGLTexture::TextureFormat::RGBAFormat);

        lightmap_texture->allocateStorage();

        for (auto &style : lightmap.style_to_lightmap_atlas) {
            lightmap_texture->setData(0, style.first, QOpenGLTexture::RGBA, QOpenGLTexture::UInt8, reinterpret_cast<const void *>(style.second.pixels.data()));
        }
    }

    // this determines what can be batched together in a draw call
    struct material_key
    {
        std::string texname;
        float opacity;

        auto as_tuple() const { return std::make_tuple(texname, opacity); }

        bool operator<(const material_key &other) const { return as_tuple() < other.as_tuple(); }
    };

    struct face_payload
    {
        const mface_t *face;
        qvec3d model_offset;
    };

    // collect faces grouped by material_key
    std::map<material_key, std::vector<face_payload>> faces_by_material_key;

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
            // FIXME: keep empty texture names?
            if (t.empty())
                continue;
            if (f.numedges < 3)
                continue;

            const mtexinfo_t *texinfo = Face_Texinfo(&bsp, &f);
            if (!texinfo)
                continue; // FIXME: render as checkerboard?

            // determine opacity
            float opacity = 1.0f;
            if (bsp.loadversion->game->id == GAME_QUAKE_II) {

                if (texinfo->flags.native & (Q2_SURF_NODRAW | Q2_SURF_SKY)) {
                    continue;
                }

                if (texinfo->flags.native & Q2_SURF_TRANS33) {
                    opacity = 0.33f;
                }
                if (texinfo->flags.native & Q2_SURF_TRANS66) {
                    opacity = 0.66f;
                }
            }

            material_key k = {.texname = t, .opacity = opacity};
            faces_by_material_key[k].push_back({.face = &f, .model_offset = origin});
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
    };
    std::vector<vertex_t> verts;
    std::vector<uint32_t> indexBuffer;

    for (const auto &[k, faces] : faces_by_material_key) {
        // upload texture
        // FIXME: we should have a separate lightpreview_options
        auto *texture = img::find(k.texname);

        if (!texture) {
            logging::print("warning, couldn't locate {}", k.texname);
            continue;
        }

        std::unique_ptr<QOpenGLTexture> qtexture =
            std::make_unique<QOpenGLTexture>(QImage(reinterpret_cast<const uint8_t *>(texture->pixels.data()),
                texture->width, texture->height, QImage::Format_RGBA8888));

        qtexture->setMaximumAnisotropy(16);
        qtexture->setAutoMipMapGenerationEnabled(true);

        const size_t dc_first_index = indexBuffer.size();

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

                uv[0] *= (1.0 / texture->width);
                uv[1] *= (1.0 / texture->height);

                qvec2f lightmap_uv = lm_uvs.at(j);

                qvec3f vertex_normal;
                if (facenormals) {
                    auto normal_index = facenormals->per_face[fnum].per_vert[j].normal;
                    vertex_normal = facenormals->normals[normal_index];
                } else {
                    vertex_normal = plane_normal;
                }

                verts.push_back({
                    .pos = pos + model_offset,
                    .uv = uv,
                    .lightmap_uv = lightmap_uv,
                    .normal = vertex_normal,
                    .flat_color = flat_color,
                    .styles = (uint32_t) (f->styles[0]) | (uint32_t) (f->styles[1] << 8) | (uint32_t) (f->styles[2] << 16) | (uint32_t) (f->styles[3] << 24)
                });
            }

            // output the vertex indices for this face
            for (int j = 2; j < f->numedges; ++j) {
                indexBuffer.push_back(first_vertex_of_face);
                indexBuffer.push_back(first_vertex_of_face + j - 1);
                indexBuffer.push_back(first_vertex_of_face + j);
            }
        }

        const size_t dc_index_count = indexBuffer.size() - dc_first_index;

        drawcall_t dc = {.opacity = k.opacity,
            .texture = std::move(qtexture),
            .first_index = dc_first_index,
            .index_count = dc_index_count};
        m_drawcalls.push_back(std::move(dc));
    }

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
    glVertexAttribPointer(3 /* attrib */, 3, GL_FLOAT, GL_FALSE, sizeof(vertex_t), (void *)offsetof(vertex_t, normal));

    // flat shading color
    glEnableVertexAttribArray(4 /* attrib */);
    glVertexAttribPointer(
        4 /* attrib */, 3, GL_FLOAT, GL_FALSE, sizeof(vertex_t), (void *)offsetof(vertex_t, flat_color));

    // styles
    glEnableVertexAttribArray(5 /* attrib */);
    glVertexAttribIPointer(
        5 /* attrib */, 1, GL_UNSIGNED_INT, sizeof(vertex_t), (void *)offsetof(vertex_t, styles));

    doneCurrent();

    // schedule repaint
    update();
}

void GLView::resizeGL(int width, int height)
{
    m_displayAspect = static_cast<float>(width) / static_cast<float>(height);
}

void GLView::mousePressEvent(QMouseEvent *event)
{
    m_lastMouseDownPos = event->screenPos();
}

void GLView::mouseMoveEvent(QMouseEvent *event)
{
    if (!(event->buttons() & Qt::RightButton))
        return;

    QPointF delta = event->screenPos() - m_lastMouseDownPos;
    m_lastMouseDownPos = event->screenPos();

    // handle mouse movement
    float pitchDegrees = delta.y() * -0.2;
    float yawDegrees = delta.x() * -0.2;

    QMatrix4x4 mouseRotation;
    mouseRotation.rotate(pitchDegrees, cameraRight());
    mouseRotation.rotate(yawDegrees, QVector3D(0, 0, 1));

    // now rotate m_cameraFwd and m_cameraUp by mouseRotation
    m_cameraFwd = mouseRotation * m_cameraFwd;

    update();
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

void GLView::startMovementTimer()
{
    if (m_keymoveUpdateTimer)
        return;

    m_lastKeymoveFrame = I_FloatTime();
    m_keymoveUpdateTimer = startTimer(1, Qt::PreciseTimer); // repaint timer, calls timerEvent()
}

void GLView::stopMovementTimer()
{
    if (m_keymoveUpdateTimer != 0) {
        killTimer(m_keymoveUpdateTimer);
        m_keymoveUpdateTimer = 0;
    }
}

void GLView::keyPressEvent(QKeyEvent *event)
{
    keys_t key = Qt_Key_To_keys_t(event->key());

    m_keysPressed |= static_cast<uint32_t>(key);

    startMovementTimer();
}

void GLView::keyReleaseEvent(QKeyEvent *event)
{
    keys_t key = Qt_Key_To_keys_t(event->key());

    m_keysPressed &= ~static_cast<uint32_t>(key);

    if (!m_keysPressed)
        stopMovementTimer();
}

void GLView::wheelEvent(QWheelEvent *event)
{
    if (!(event->buttons() & Qt::RightButton))
        return;

    double delta = event->angleDelta().y();

    m_moveSpeed += delta;
    m_moveSpeed = clamp(m_moveSpeed, 10.0f, 5000.0f);
}

void GLView::timerEvent(QTimerEvent *event)
{
    // update frame time
    auto current_time = I_FloatTime();
    auto duration = current_time - m_lastKeymoveFrame;
    m_lastKeymoveFrame = current_time;

    // qDebug() << "timer event: duration: " << duration.count();

    const float distance = m_moveSpeed * duration.count();

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

    update(); // schedule a repaint
}
