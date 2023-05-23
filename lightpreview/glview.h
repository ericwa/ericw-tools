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

#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QElapsedTimer>
#include <QVector3D>
#include <QMatrix4x4>

#include <vector>

#include <common/qvec.hh>
#include <common/cmdlib.hh>
#include <common/entdata.h>

enum class keys_t : uint32_t
{
    none = 0,
    up = 1,
    right = 2,
    down = 4,
    left = 8,
    fly_down = 16,
    fly_up = 32
};

struct mbsp_t;

class GLView : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
private:
    uint32_t m_keysPressed;
    int m_keymoveUpdateTimer;
    time_point m_lastKeymoveFrame;
    QPointF m_lastMouseDownPos;
    /**
     * units / second
     */
    float m_moveSpeed;

    // camera stuff
    float m_displayAspect;
    QVector3D m_cameraOrigin;
    QVector3D m_cameraFwd; // unit vec
    QVector3D cameraRight() const
    {
        QVector3D v = QVector3D::crossProduct(m_cameraFwd, QVector3D(0, 0, 1));
        v.normalize();
        return v;
    }

    // render options
    bool m_lighmapOnly = false;
    bool m_fullbright = false;
    bool m_drawNormals = false;
    bool m_showTris = false;
    bool m_drawFlat = false;

    QOpenGLVertexArrayObject m_vao;
    QOpenGLBuffer m_vbo;
    QOpenGLBuffer m_indexBuffer;

    std::unique_ptr<QOpenGLTexture> lightmap_texture;
    struct drawcall_t
    {
        float opacity = 1.0f;
        std::unique_ptr<QOpenGLTexture> texture;
        size_t first_index = 0;
        size_t index_count = 0;
    };
    std::vector<drawcall_t> m_drawcalls;

    QOpenGLShaderProgram *m_program = nullptr;
    QOpenGLShaderProgram *m_program_wireframe = nullptr;

    // uniform locations
    int m_program_mvp_location = 0;
    int m_program_texture_sampler_location = 0;
    int m_program_lightmap_sampler_location = 0;
    int m_program_opacity_location = 0;
    int m_program_lightmap_only_location = 0;
    int m_program_fullbright_location = 0;
    int m_program_drawnormals_location = 0;
    int m_program_showtris_location = 0;
    int m_program_drawflat_location = 0;

    // uniform locations (wireframe program)
    int m_program_wireframe_mvp_location = 0;

public:
    GLView(QWidget *parent = nullptr);
    ~GLView();

    void renderBSP(const QString &file, const mbsp_t &bsp, const std::vector<entdict_t> &entities);
    void setCamera(const qvec3d &origin, const qvec3d &fwd);
    void setLighmapOnly(bool lighmapOnly);
    void setFullbright(bool fullbright);
    void setDrawNormals(bool drawnormals);
    void setShowTris(bool showtris);
    void setDrawFlat(bool drawflat);

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
    void wheelEvent(QWheelEvent *event) override;

protected:
    /** animation timer */
    void timerEvent(QTimerEvent *event) override;
};
