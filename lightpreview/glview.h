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

#ifndef GLVIEW_H
#define GLVIEW_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>

#include <QElapsedTimer>
#include <QVector3D>
#include <QMatrix4x4>

enum class keys_t : uint32_t {
    none = 0,
    up = 1,
    right = 2,
    down = 4,
    left = 8
};

class GLView : public QOpenGLWidget,
               protected QOpenGLFunctions
{
private:
    uint32_t m_keysPressed;
    int m_keymoveUpdateTimer;
    QPointF m_lastMouseDownPos;
    
    // camera stuff
    float m_displayAspect;
    QVector3D m_cameraOrigin;
    QVector3D m_cameraFwd; // unit vec
    QVector3D cameraRight() const {
        QVector3D v = QVector3D::crossProduct(m_cameraFwd, QVector3D(0,0,1));
        v.normalize();
        return v;
    }
    
    QOpenGLVertexArrayObject m_vao;
    QOpenGLBuffer m_vbo;
    QOpenGLShaderProgram *m_program;
    
    // uniform locations
    int m_program_mvp_location;
    
public:
    GLView(QWidget *parent = nullptr);
    ~GLView();

    
protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int width, int height) override;

private:
    void startMovementTimer();
    void stopMovementTimer();
    
protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void wheelEvent (QWheelEvent *event) override;
    
protected:
    /** animation timer */
    void timerEvent(QTimerEvent *event) override;
};

#endif // GLVIEW_H
