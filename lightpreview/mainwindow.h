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

#include <QMainWindow>
#include <QVBoxLayout>

#include <common/bspfile.hh>
#include <common/settings.hh>

class GLView;
class QFileSystemWatcher;
class QLineEdit;
class QCheckBox;
class QStringList;
class QTextEdit;

enum class ETLogTab
{
    TAB_LIGHTPREVIEW,
    TAB_BSP,
    TAB_VIS,
    TAB_LIGHT,

    TAB_TOTAL
};

class ETLogWidget : public QTabWidget
{
    Q_OBJECT

public:
    static constexpr const char *logTabNames[(size_t)ETLogTab::TAB_TOTAL] = {"lightpreview", "bsp", "vis", "light"};

    explicit ETLogWidget(QWidget *parent = nullptr);
    ~ETLogWidget() { }

    QTextEdit *textEdit(ETLogTab i) { return m_textEdits[(size_t)i]; }
    const QTextEdit *textEdit(ETLogTab i) const { return m_textEdits[(size_t)i]; }

    auto &textEdits() { return m_textEdits; }

private:
    std::array<QTextEdit *, std::size(logTabNames)> m_textEdits;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

private:
    QFileSystemWatcher *m_watcher = nullptr;
    std::unique_ptr<QTimer> m_fileReloadTimer;
    bool m_fileWasReload = false;
    QString m_mapFile;
    bspdata_t m_bspdata;
    std::vector<uint8_t> m_litdata;
    settings::common_settings render_settings;
    qint64 m_fileSize = -1;
    ETLogTab m_activeLogTab = ETLogTab::TAB_LIGHTPREVIEW;
    QThread *m_compileThread = nullptr;

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private:
    void createPropertiesSidebar();
    void createOutputLog();
    void lightpreview_log_callback(logging::flag flags, const char *str);
    void lightpreview_percent_callback(std::optional<uint32_t> percent, std::optional<duration> elapsed);
    void logWidgetSetText(ETLogTab tab, const std::string &str);
    void createStatusBar();
    void updateRecentsSubmenu(const QStringList &recents);
    void setupMenu();
    void fileOpen();
    void takeScreenshot();
    void fileReloadTimerExpired();
    int compileMap(const QString &file, bool is_reload);
    void compileThreadExited();
    bspdata_t QbspVisLight_Common(const fs::path &name, std::vector<std::string> extra_common_args,
        std::vector<std::string> extra_qbsp_args, std::vector<std::string> extra_vis_args, std::vector<std::string> extra_light_args, bool run_vis);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void reload();
    void loadFile(const QString &file);
    void loadFileInternal(const QString &file, bool is_reload);
    void displayCameraPositionInfo();

private:
    GLView *glView = nullptr;

    QCheckBox *vis_checkbox = nullptr;
    QCheckBox *nearest = nullptr;
    QCheckBox *bspx_decoupled_lm = nullptr;
    QCheckBox *bspx_normals = nullptr;

    QLineEdit *common_options = nullptr;
    QLineEdit *qbsp_options = nullptr;
    QLineEdit *vis_options = nullptr;
    QLineEdit *light_options = nullptr;
    QVBoxLayout *lightstyles = nullptr;

    QMenu *viewMenu = nullptr;
    QMenu *openRecentMenu = nullptr;

    ETLogWidget *m_outputLogWidget = nullptr;
};
