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
#include <QOpenGLDebugMessage>
#include <QElapsedTimer>
#include <QVector3D>
#include <QMatrix4x4>

#include <optional>
#include <vector>

#include <common/qvec.hh>
#include <common/cmdlib.hh>
#include <common/entdata.h>
#include <common/bspfile.hh>
#include <common/bspinfo.hh>

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
    Q_OBJECT

private:
    std::optional<mbsp_t> m_bsp;
    std::unordered_map<int, std::vector<uint8_t>> m_decompressedVis;

    uint32_t m_keysPressed;
    std::optional<time_point> m_lastFrame;
    std::optional<QPoint> m_lastMouseDownPos;
    /**
     * units / second
     */
    float m_moveSpeed;

    // vis culling stuff
    struct face_visibility_key_t
    {
        bool show_bmodels;
        // -1 indicates solid leaf or no visdata (render all world faces)
        int leafnum;
        // Q2 bsp: cluster num
        // other: visofs
        int clusternum;

        bool operator==(const face_visibility_key_t &other) const
        {
            return show_bmodels == other.show_bmodels && leafnum == other.leafnum && clusternum == other.clusternum;
        }
    };
    face_visibility_key_t desiredFaceVisibility() const;

    std::optional<face_visibility_key_t> m_uploaded_face_visibility;
    bool m_visCulling = true;

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
    std::optional<int> m_drawLeafs;
    bool m_showTris = false;
    bool m_showTrisSeeThrough = false;
    bool m_drawFlat = false;
    bool m_keepOrigin = false;
    bool m_drawPortals = false;
    bool m_drawLeak = false;
    QOpenGLTexture::Filter m_filter = QOpenGLTexture::Linear;
    bool m_drawTranslucencyAsOpaque = false;
    bool m_showBmodels = true;

    QOpenGLVertexArrayObject m_vao;
    QOpenGLBuffer m_vbo;
    QOpenGLBuffer m_indexBuffer;

    QOpenGLVertexArrayObject m_leakVao;
    QOpenGLBuffer m_leakVbo;

    QOpenGLVertexArrayObject m_portalVao;
    QOpenGLBuffer m_portalVbo;
    QOpenGLBuffer m_portalIndexBuffer;

    struct leaf_vao_t {
        QOpenGLVertexArrayObject vao;
        QOpenGLBuffer vbo;
        QOpenGLBuffer indexBuffer;
        int num_indices = 0;
    };
    std::array<leaf_vao_t, MAX_MAP_HULLS_H2> m_hullVaos;

    // this determines what can be batched together in a draw call
    struct material_key
    {
        QOpenGLShaderProgram *program;
        std::string texname;
        float opacity = 1.f;
        bool alpha_test = false;

        auto as_tuple() const { return std::make_tuple(program, texname, opacity, alpha_test); }

        bool operator<(const material_key &other) const { return as_tuple() < other.as_tuple(); }
    };

    std::shared_ptr<QOpenGLTexture> placeholder_texture;
    std::shared_ptr<QOpenGLTexture> lightmap_texture;
    /**
     * 1D texture, one uint8 per face.
     *
     * 0 = render normally
     * 1 = skip face
     */
    std::shared_ptr<QOpenGLTexture> face_visibility_texture;

    struct drawcall_t
    {
        material_key key;
        std::shared_ptr<QOpenGLTexture> texture;
        size_t first_index = 0;
        size_t index_count = 0;
    };
    std::vector<drawcall_t> m_drawcalls;
    size_t num_leak_points = 0;
    size_t num_portal_indices = 0;

    QOpenGLShaderProgram *m_program = nullptr, *m_skybox_program = nullptr;
    QOpenGLShaderProgram *m_program_wireframe = nullptr;
    QOpenGLShaderProgram *m_program_simple = nullptr;

    // uniform locations (default program)
    int m_program_mvp_location = 0;
    int m_program_texture_sampler_location = 0;
    int m_program_lightmap_sampler_location = 0;
    int m_program_face_visibility_sampler_location = 0;
    int m_program_opacity_location = 0;
    int m_program_alpha_test_location = 0;
    int m_program_lightmap_only_location = 0;
    int m_program_fullbright_location = 0;
    int m_program_drawnormals_location = 0;
    int m_program_drawflat_location = 0;
    int m_program_style_scalars_location = 0;

    // uniform locations (skybox program)
    int m_skybox_program_mvp_location = 0;
    int m_skybox_program_eye_direction_location = 0;
    int m_skybox_program_texture_sampler_location = 0;
    int m_skybox_program_lightmap_sampler_location = 0;
    int m_skybox_program_face_visibility_sampler_location = 0;
    int m_skybox_program_opacity_location = 0;
    int m_skybox_program_lightmap_only_location = 0;
    int m_skybox_program_fullbright_location = 0;
    int m_skybox_program_drawnormals_location = 0;
    int m_skybox_program_drawflat_location = 0;
    int m_skybox_program_style_scalars_location = 0;

    // uniform locations (wireframe program)
    int m_program_wireframe_mvp_location = 0;
    int m_program_wireframe_face_visibility_sampler_location = 0;

    // uniform locations (simple program)
    int m_program_simple_mvp_location = 0;
    int m_program_simple_color_location = 0;

public:
    GLView(QWidget *parent = nullptr);
    ~GLView();

private:
    void setFaceVisibilityArray(uint8_t *data);

public:
    void renderBSP(const QString &file, const mbsp_t &bsp, const bspxentries_t &bspx,
        const std::vector<entdict_t> &entities, const full_atlas_t &lightmap, const settings::common_settings &settings,
        bool use_bspx_normals);
    void setCamera(const qvec3d &origin, const qvec3d &fwd);
    // FIXME: distinguish render modes from render options
    void setLighmapOnly(bool lighmapOnly);
    void setFullbright(bool fullbright);
    void setDrawNormals(bool drawnormals);
    void setDrawLeafs(std::optional<int> hullnum);
    void setShowTris(bool showtris);
    void setShowTrisSeeThrough(bool showtris);
    void setVisCulling(bool viscull);
    void setDrawFlat(bool drawflat);
    void setKeepOrigin(bool keeporigin);
    void setDrawPortals(bool drawportals);
    void setDrawLeak(bool drawleak);
    // intensity = 0 to 200
    void setLightStyleIntensity(int style_id, int intensity);
    void setMagFilter(QOpenGLTexture::Filter filter);
    const bool &getKeepOrigin() const { return m_keepOrigin; }
    void setDrawTranslucencyAsOpaque(bool drawopaque);
    void setShowBmodels(bool bmodels);

    void takeScreenshot(QString destPath, int w, int h);

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int width, int height) override;

private:
    void updateFaceVisibility();
    bool shouldLiveUpdate() const;
    void handleLoggedMessage(const QOpenGLDebugMessage &debugMessage);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    void applyMouseMotion();
    void applyFlyMovement(float duration);

signals:
    void cameraMoved();

public:
    qvec3f cameraPosition() const;
};
