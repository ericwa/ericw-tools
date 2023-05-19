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

#include <QImage>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QTime>
#include <fmt/core.h>

#include <common/bspfile.hh>
#include <common/bsputils.hh>
#include <common/imglib.hh>
#include <light/light.hh>

GLView::GLView(QWidget *parent)
    : QOpenGLWidget(parent),
      m_keysPressed(0),
      m_keymoveUpdateTimer(0),
      m_lastMouseDownPos(0, 0),
      m_displayAspect(1),
      m_cameraOrigin(0, 0, 0),
      m_cameraFwd(0, 1, 0),
      m_vao(),
      m_indexBuffer(QOpenGLBuffer::IndexBuffer),
      m_program(nullptr),
      m_program_mvp_location(0),
      m_program_texture_sampler_location(0)
{
    setFocusPolicy(Qt::StrongFocus); // allow keyboard focus
}

GLView::~GLView()
{
    makeCurrent();

    delete m_program;

    m_vbo.destroy();
    m_indexBuffer.destroy();
    m_vao.destroy();

    doneCurrent();
}

static const char *s_fragShader = R"(
#version 330 core

in vec2 uv;

out vec4 color;

uniform sampler2D texture_sampler;

void main() {
    color = vec4(texture(texture_sampler, uv).rgb, 1.0);
}
)";

static const char *s_vertShader = R"(
#version 330 core

layout (location = 0) in vec3 position;
layout (location = 1) in vec2 vertex_uv;

out vec2 uv;

uniform mat4 MVP;

void main() {
    gl_Position = MVP * vec4(position.x, position.y, position.z, 1.0);

    uv = vertex_uv;
}
)";

void GLView::initializeGL()
{
    initializeOpenGLFunctions();

    // set up shader

    m_program = new QOpenGLShaderProgram();
    m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, s_vertShader);
    m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, s_fragShader);
    assert(m_program->link());

    m_program->bind();
    m_program_mvp_location = m_program->uniformLocation("MVP");
    m_program_texture_sampler_location = m_program->uniformLocation("texture_sampler");
    m_vao.create();
}

void GLView::paintGL()
{
    // draw
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    m_program->bind();

    QMatrix4x4 modelMatrix;
    QMatrix4x4 viewMatrix;
    QMatrix4x4 projectionMatrix;
    projectionMatrix.perspective(90, m_displayAspect, 0.01f, 1'000'000.0f);
    viewMatrix.lookAt(m_cameraOrigin, m_cameraOrigin + m_cameraFwd, QVector3D(0, 0, 1));

    QMatrix4x4 MVP = projectionMatrix * viewMatrix * modelMatrix;

    m_program->setUniformValue(m_program_mvp_location, MVP);
    m_program->setUniformValue(m_program_texture_sampler_location, 0 /* texture unit */);

    for (auto &draw : m_drawcalls) {
        draw.texture->bind(0 /* texture unit */);

        QOpenGLVertexArrayObject::Binder vaoBinder(&m_vao);

        // glEnable(GL_BLEND);
        // glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glDrawElements(GL_TRIANGLES, draw.index_count, GL_UNSIGNED_INT,
            reinterpret_cast<void *>(draw.first_index * sizeof(uint32_t)));

        // glDisable(GL_BLEND);
    }

    m_program->release();
}

void GLView::renderBSP(const mbsp_t &bsp)
{
    // FIXME: move to a lightpreview_settings
    settings::common_settings settings;

    bsp.loadversion->game->init_filesystem("placeholder.map", settings);
    img::load_textures(&bsp, settings);

    // NOTE: according to https://doc.qt.io/qt-6/qopenglwidget.html#resource-initialization-and-cleanup
    // we can only do this after `initializeGL()` has run once.
    makeCurrent();

    auto &m = bsp.dmodels[0];

    // collect faces grouped by texture name
    std::map<std::string, std::vector<const mface_t *>> faces_by_texname;

    for (int i = m.firstface; i < m.firstface + m.numfaces; ++i) {
        auto &f = bsp.dfaces[i];
        std::string t = Face_TextureName(&bsp, &f);
        // FIXME: keep empty texture names?
        if (t.empty())
            continue;
        if (f.numedges < 3)
            continue;
        faces_by_texname[t].push_back(&f);
    }

    // populate the vertex/index buffers
    struct vertex_t
    {
        qvec3f pos;
        qvec2f uv;
    };
    std::vector<vertex_t> verts;
    std::vector<uint32_t> indexBuffer;

    for (const auto &[texname, faces] : faces_by_texname) {
        // upload texture
        // FIXME: we should have a separate lightpreview_options
        auto *texture = img::find(texname);

        if (!texture) {
            logging::print("warning, couldn't locate {}", texname);
            continue;
        }

        std::unique_ptr<QOpenGLTexture> qtexture =
            std::make_unique<QOpenGLTexture>(QImage(reinterpret_cast<const uint8_t *>(texture->pixels.data()),
                texture->width, texture->height, QImage::Format_RGBA8888));

        const size_t dc_first_index = indexBuffer.size();

        for (const mface_t *f : faces) {
            const size_t first_vertex_of_face = verts.size();

            // output a vertex for each vertex of the face
            for (int j = 0; j < f->numedges; ++j) {
                qvec3f pos = Face_PointAtIndex(&bsp, f, j);
                qvec2f uv = Face_WorldToTexCoord(&bsp, f, pos);

                uv[0] *= (1.0 / texture->width);
                uv[1] *= (1.0 / texture->height);

                verts.push_back({.pos = pos, .uv = uv});
            }

            // output the vertex indices for this face
            for (int j = 2; j < f->numedges; ++j) {
                indexBuffer.push_back(first_vertex_of_face);
                indexBuffer.push_back(first_vertex_of_face + j - 1);
                indexBuffer.push_back(first_vertex_of_face + j);
            }
        }

        const size_t dc_index_count = indexBuffer.size() - dc_first_index;

        drawcall_t dc = {.texture = std::move(qtexture), .first_index = dc_first_index, .index_count = dc_index_count};
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
    glVertexAttribPointer(0 /* attrib */, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)0);

    // normals
    glEnableVertexAttribArray(1 /* attrib */);
    glVertexAttribPointer(1 /* attrib */, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)(3 * sizeof(float)));

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

    repaint();
}

static keys_t Qt_Key_To_keys_t(int key)
{
    switch (key) {
        case Qt::Key_W: return keys_t::up;
        case Qt::Key_A: return keys_t::left;
        case Qt::Key_S: return keys_t::down;
        case Qt::Key_D: return keys_t::right;
    }
    return keys_t::none;
}

void GLView::startMovementTimer()
{
    if (m_keymoveUpdateTimer)
        return;

    m_keymoveUpdateTimer = startTimer(10); // repaint timer, calls timerEvent()
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
    static float speed = 0.02;
    m_cameraOrigin += m_cameraFwd * event->angleDelta().y() * speed;
    update();
}

void GLView::timerEvent(QTimerEvent *event)
{
    const float speed = 10;

    if (m_keysPressed & static_cast<uint32_t>(keys_t::up))
        m_cameraOrigin += m_cameraFwd * speed;
    if (m_keysPressed & static_cast<uint32_t>(keys_t::down))
        m_cameraOrigin -= m_cameraFwd * speed;
    if (m_keysPressed & static_cast<uint32_t>(keys_t::left))
        m_cameraOrigin -= cameraRight() * speed;
    if (m_keysPressed & static_cast<uint32_t>(keys_t::right))
        m_cameraOrigin += cameraRight() * speed;

    update(); // schedule a repaint
}
