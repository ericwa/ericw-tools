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

#include "mainwindow.h"

#include <QString>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QFileSystemWatcher>
#include <QFileInfo>
#include <QFormLayout>
#include <QLineEdit>
#include <QSplitter>
#include <QCheckBox>
#include <QPushButton>
#include <QSettings>
#include <QMenu>
#include <QMenuBar>
#include <QFileDialog>
#include <QGroupBox>
#include <QRadioButton>

#include <common/bspfile.hh>
#include <qbsp/qbsp.hh>
#include <vis/vis.hh>
#include <light/light.hh>

#include "glview.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    resize(640, 480);

    // gl view
    glView = new GLView();

    // properties form
    auto *formLayout = new QFormLayout();

    vis_checkbox = new QCheckBox(tr("vis"));

    qbsp_options = new QLineEdit();
    vis_options = new QLineEdit();
    light_options = new QLineEdit();
    auto *reload_button = new QPushButton(tr("Reload"));

    auto *lightmapped = new QRadioButton(tr("Lightmapped"));
    lightmapped->setChecked(true);
    auto *lightmap_only = new QRadioButton(tr("Lightmap Only"));
    auto *fullbright = new QRadioButton(tr("Fullbright"));
    auto *normals = new QRadioButton(tr("Normals"));
    auto *drawflat = new QRadioButton(tr("Flat shading"));

    auto *rendermode_layout = new QVBoxLayout();
    rendermode_layout->addWidget(lightmapped);
    rendermode_layout->addWidget(lightmap_only);
    rendermode_layout->addWidget(fullbright);
    rendermode_layout->addWidget(normals);
    rendermode_layout->addWidget(drawflat);

    auto *rendermode_group = new QGroupBox(tr("Render mode"));
    rendermode_group->setLayout(rendermode_layout);

    auto *showtris = new QCheckBox(tr("Show Tris"));

    auto *keepposition = new QCheckBox(tr("Keep Camera Pos"));

    formLayout->addRow(tr("qbsp"), qbsp_options);
    formLayout->addRow(vis_checkbox, vis_options);
    formLayout->addRow(tr("light"), light_options);
    formLayout->addRow(reload_button);
    formLayout->addRow(rendermode_group);
    formLayout->addRow(showtris);
    formLayout->addRow(keepposition);

    auto *form = new QWidget();
    form->setLayout(formLayout);

    // splitter

    auto *splitter = new QSplitter();
    splitter->addWidget(form);
    splitter->addWidget(glView);

    setCentralWidget(splitter);
    setAcceptDrops(true);

    // load state persisted in settings
    QSettings s;
    qbsp_options->setText(s.value("qbsp_options").toString());
    vis_checkbox->setChecked(s.value("vis_enabled").toBool());
    vis_options->setText(s.value("vis_options").toString());
    light_options->setText(s.value("light_options").toString());

    // setup event handlers

    connect(reload_button, &QAbstractButton::clicked, this, &MainWindow::reload);
    connect(lightmap_only, &QAbstractButton::toggled, this, [=](bool checked) { glView->setLighmapOnly(checked); });
    connect(fullbright, &QAbstractButton::toggled, this, [=](bool checked) { glView->setFullbright(checked); });
    connect(normals, &QAbstractButton::toggled, this, [=](bool checked) { glView->setDrawNormals(checked); });
    connect(showtris, &QAbstractButton::toggled, this, [=](bool checked) { glView->setShowTris(checked); });
    connect(drawflat, &QAbstractButton::toggled, this, [=](bool checked) { glView->setDrawFlat(checked); });
    connect(keepposition, &QAbstractButton::toggled, this, [=](bool checked) { glView->setKeepOrigin(checked); });

    setupMenu();
}

MainWindow::~MainWindow() { }

void MainWindow::setupMenu()
{
    auto *menu = menuBar()->addMenu(tr("&File"));

    auto *open = menu->addAction(tr("&Open"), this, &MainWindow::fileOpen);
    open->setShortcut(QKeySequence::Open);

    auto *openRecent = menu->addAction(tr("Open &Recent"));

    auto *takeScreenshot = menu->addAction(tr("Take Screenshot"), this, [=]() { glView->takeScreenshot(3840, 2160); });

    auto *exit = menu->addAction(tr("E&xit"), this, &QWidget::close);
    exit->setShortcut(QKeySequence::Quit);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    auto urls = event->mimeData()->urls();
    if (!urls.empty()) {
        const QUrl &url = urls[0];
        if (url.isLocalFile()) {
            loadFile(url.toLocalFile());

            event->acceptProposedAction();
        }
    }
}

void MainWindow::fileOpen()
{
    QString fileName = QFileDialog::getOpenFileName(this, tr("Open File"), "", tr("Map (*.map);; BSP (*.bsp)"));

    if (!fileName.isEmpty())
        loadFile(fileName);
}

void MainWindow::loadFile(const QString &file)
{
    qDebug() << "load " << file;

    m_mapFile = file;

    if (m_watcher) {
        delete m_watcher;
    }
    m_watcher = new QFileSystemWatcher(this);

    // start watching it
    qDebug() << "adding path: " << m_watcher->addPath(file);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [&](const QString &path) {
        if (QFileInfo(path).size() == 0) {
            // saving a map in TB produces 2 change notifications on Windows; the
            // first truncates the file to 0 bytes, so ignore that.
            return;
        }
        qDebug() << "got change notif for " << path;
        loadFileInternal(path, true);
    });

    loadFileInternal(file, false);
}

std::filesystem::path MakeFSPath(const QString &string)
{
    return std::filesystem::path{string.toStdU16String()};
}

static bspdata_t QbspVisLight_Common(const std::filesystem::path &name, std::vector<std::string> extra_qbsp_args,
    std::vector<std::string> extra_vis_args, std::vector<std::string> extra_light_args, bool run_vis)
{
    auto bsp_path = name;
    bsp_path.replace_extension(".bsp");

    std::vector<std::string> args{
        "", // the exe path, which we're ignoring in this case
    };
    for (auto &extra : extra_qbsp_args) {
        args.push_back(extra);
    }
    args.push_back(name.string());

    // run qbsp

    InitQBSP(args);
    ProcessFile();

    // run vis
    if (run_vis) {
        std::vector<std::string> vis_args{
            "", // the exe path, which we're ignoring in this case
        };
        for (auto &extra : extra_vis_args) {
            vis_args.push_back(extra);
        }
        vis_args.push_back(name.string());
        vis_main(vis_args);
    }

    // run light
    {
        std::vector<std::string> light_args{
            "", // the exe path, which we're ignoring in this case
        };
        for (auto &arg : extra_light_args) {
            light_args.push_back(arg);
        }
        light_args.push_back(name.string());

        light_main(light_args);
    }

    // serialize obj
    {
        bspdata_t bspdata;
        LoadBSPFile(bsp_path, &bspdata);

        ConvertBSPFormat(&bspdata, &bspver_generic);

        return bspdata;
    }
}

static std::vector<std::string> ParseArgs(const QLineEdit *line_edit)
{
    std::vector<std::string> result;

    QString text = line_edit->text().trimmed();
    if (text.isEmpty())
        return result;

    bool inside_quotes = false;
    for (const auto &str : text.split('"')) {
        qDebug() << "got token " << str << " inside quote? " << inside_quotes;

        if (inside_quotes) {
            result.push_back(str.toStdString());
        } else {
            // split by spaces
            for (const auto &str2 : str.split(' ', QString::SkipEmptyParts)) {
                qDebug() << "got sub token " << str2;
                result.push_back(str2.toStdString());
            }
        }

        inside_quotes = !inside_quotes;
    }

    return result;
}

void MainWindow::reload()
{
    if (m_mapFile.isEmpty())
        return;

    loadFileInternal(m_mapFile, true);
}

void MainWindow::loadFileInternal(const QString &file, bool is_reload)
{
    qDebug() << "loadFileInternal " << file;

    // persist settings
    QSettings s;
    s.setValue("qbsp_options", qbsp_options->text());
    s.setValue("vis_enabled", vis_checkbox->isChecked());
    s.setValue("vis_options", vis_options->text());
    s.setValue("light_options", light_options->text());

    // update title bar
    setWindowFilePath(file);
    setWindowTitle(QFileInfo(file).fileName() + " - lightpreview");

    fs::path fs_path = MakeFSPath(file);
    
    bspdata_t d;

    if (fs_path.extension().compare(".bsp") == 0) {

        LoadBSPFile(fs_path, &d);

        auto opts = ParseArgs(light_options);

        std::vector<const char *> argPtrs;

        for (const std::string &arg : opts) {
            argPtrs.push_back(arg.data());
        }

        settings::common_settings settings;

        settings.preinitialize(argPtrs.size(), argPtrs.data());
        settings.initialize(argPtrs.size(), argPtrs.data());
        settings.postinitialize(argPtrs.size(), argPtrs.data());

        d.version->game->init_filesystem(fs_path, settings);

        ConvertBSPFormat(&d, &bspver_generic);

    } else {
        d = QbspVisLight_Common(
            fs_path, ParseArgs(qbsp_options), ParseArgs(vis_options), ParseArgs(light_options), vis_checkbox->isChecked());
    }

    const auto &bsp = std::get<mbsp_t>(d.bsp);

    auto ents = EntData_Parse(bsp);

    glView->renderBSP(file, bsp, d.bspx.entries, ents);

    if (!is_reload && !glView->getKeepOrigin()) {
        for (auto &ent : ents) {
            if (ent.get("classname") == "info_player_start") {
                qvec3d origin;
                ent.get_vector("origin", origin);

                qvec3d angles{};

                if (ent.has("angles")) {
                    ent.get_vector("angles", angles);
                    angles = {angles[1], -angles[0], angles[2]}; // -pitch yaw roll -> yaw pitch roll
                } else if (ent.has("angle"))
                    angles = {ent.get_float("angle"), 0, 0};
                else if (ent.has("mangle"))
                    ent.get_vector("mangle", angles);

                glView->setCamera(origin, qv::vec_from_mangle(angles));
                break;
            }
        }
    }
}