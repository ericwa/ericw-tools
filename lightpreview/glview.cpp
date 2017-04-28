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

#include <cstdio>
#include <cassert>

#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QTime>

GLView::GLView(QWidget *parent)
    : QOpenGLWidget(parent),
    m_keysPressed(0),
    m_keymoveUpdateTimer(0),
    m_lastMouseDownPos(0,0),
    m_displayAspect(1),
    m_cameraOrigin(0, 0, 0),
    m_cameraFwd(0, 1, 0),
    m_vao(),
    m_program(nullptr),
    m_program_mvp_location(0)
{
    setFocusPolicy(Qt::StrongFocus); // allow keyboard focus
}

GLView::~GLView()
{
    delete m_program;
}

static const char *s_fragShader = R"(
#version 330 core

out vec4 color;

void main() {
    color = vec4(1.0, 0.5, 0.2, 1.0);
}
)";

static const char *s_vertShader = R"(
#version 330 core

layout (location = 0) in vec3 position;

uniform mat4 MVP;

void main() {
    gl_Position = MVP * vec4(position.x, position.y, position.z, 1.0);
}
)";

void GLView::initializeGL()
{
    initializeOpenGLFunctions();
    
    // set up shader
    
    m_program = new QOpenGLShaderProgram();
    m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, s_vertShader);
    m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, s_fragShader);
    m_program->bindAttributeLocation("vertex", 0);
    assert(m_program->link());

    m_program->bind();
    m_program_mvp_location = m_program->uniformLocation("MVP");
    
    m_vao.create();
    QOpenGLVertexArrayObject::Binder vaoBinder(&m_vao);
    
    m_vbo.create();
    m_vbo.bind();
    
    static const GLfloat g_vertex_buffer_data[] = {
        -1.0f, 0, -1.0f,
        1.0f, 0, -1.0f,
        0.0f,  0, 1.0f,
    };
    assert(sizeof(g_vertex_buffer_data) == 36);

    m_vbo.allocate(g_vertex_buffer_data, sizeof(g_vertex_buffer_data));
}

void GLView::paintGL()
{
    // draw
    glClearColor(1, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    m_program->bind();
    
    QMatrix4x4 modelMatrix;
    QMatrix4x4 viewMatrix;
    QMatrix4x4 projectionMatrix;
    projectionMatrix.perspective(90, m_displayAspect, 0.1f, 100.0f);
    viewMatrix.lookAt(m_cameraOrigin, m_cameraOrigin + m_cameraFwd, QVector3D(0,0,1));

    QMatrix4x4 MVP = projectionMatrix * viewMatrix * modelMatrix;
    
    m_program->setUniformValue(m_program_mvp_location, MVP);
    
    QOpenGLVertexArrayObject::Binder vaoBinder(&m_vao);
    
    glEnableVertexAttribArray(0);
    assert(m_vbo.bind());
    
    glVertexAttribPointer(0 /* attrib */, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisableVertexAttribArray(0);
    
    m_program->release();
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
    mouseRotation.rotate(yawDegrees, QVector3D(0,0,1));
    
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
    printf("key movement %s\n", QTime::currentTime().toString().toStdString().c_str());
    
    const float speed = 0.1;
    
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
