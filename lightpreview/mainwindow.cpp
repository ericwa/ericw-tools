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

#include <QCoreApplication>
#include <QDockWidget>
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
#include <QTimer>
#include <QScrollArea>
#include <QSpinBox>
#include <QScrollBar>
#include <QFrame>
#include <QLabel>
#include <QTextEdit>
#include <QStatusBar>
#include <QStringList>
#include <QThread>
#include <QApplication>

#include <common/bspfile.hh>
#include <qbsp/qbsp.hh>
#include <vis/vis.hh>
#include <light/light.hh>
#include <common/bspinfo.hh>
#include <fmt/chrono.h>

#include "glview.h"

// Recent files

static constexpr auto RECENT_SETTINGS_KEY = "recent_files";
static constexpr size_t MAX_RECENTS = 10;

static void ClearRecents()
{
    QSettings s;
    s.setValue(RECENT_SETTINGS_KEY, QStringList());
}

/**
 * Updates the recent files settings by pushing the given file to the front
 * and trimming the list to SETTINGS_MAX.
 *
 * @param file the file to push
 * @return the new recent files list
 */
static QStringList AddRecent(const QString &file)
{
    QSettings s;
    QStringList recents = s.value(RECENT_SETTINGS_KEY).toStringList();

    recents.removeOne(file); // no-op if not present
    recents.push_front(file);
    while (recents.size() > MAX_RECENTS) {
        recents.pop_back();
    }

    s.setValue(RECENT_SETTINGS_KEY, recents);

    return recents;
}

static QStringList GetRecents()
{
    QSettings s;
    QStringList recents = s.value(RECENT_SETTINGS_KEY).toStringList();
    return recents;
}

// ETLogWidget
ETLogWidget::ETLogWidget(QWidget *parent)
    : QTabWidget(parent)
{
    for (size_t i = 0; i < std::size(logTabNames); i++) {
        m_textEdits[i] = new QTextEdit();

        auto *formLayout = new QFormLayout();
        auto *form = new QWidget();
        formLayout->addRow(m_textEdits[i]);
        form->setLayout(formLayout);
        setTabText(i, logTabNames[i]);
        addTab(form, logTabNames[i]);
        formLayout->setContentsMargins(0, 0, 0, 0);
    }
}

// MainWindow

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    // create the menu first as it is used by other things (dock widgets)
    setupMenu();

    // gl view
    glView = new GLView(this);
    setCentralWidget(glView);

    setAcceptDrops(true);

    createPropertiesSidebar();
    createOutputLog();

    createStatusBar();

    resize(1024, 768);
}

void MainWindow::createPropertiesSidebar()
{
    QDockWidget *dock = new QDockWidget(tr("Properties"), this);

    auto *formLayout = new QFormLayout();

    vis_checkbox = new QCheckBox(tr("vis"));

    common_options = new QLineEdit();
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
    auto *hull0 = new QRadioButton(tr("Leafs"));
    auto *hull1 = new QRadioButton(tr("Hull 1"));
    auto *hull2 = new QRadioButton(tr("Hull 2"));
    auto *hull3 = new QRadioButton(tr("Hull 3"));
    auto *hull4 = new QRadioButton(tr("Hull 4"));
    auto *hull5 = new QRadioButton(tr("Hull 5"));

    lightmapped->setShortcut(QKeySequence("Alt+1"));
    lightmap_only->setShortcut(QKeySequence("Alt+2"));
    fullbright->setShortcut(QKeySequence("Alt+3"));
    normals->setShortcut(QKeySequence("Alt+4"));
    drawflat->setShortcut(QKeySequence("Alt+5"));
    hull0->setShortcut(QKeySequence("Alt+6"));

    lightmapped->setToolTip("Lighmapped textures (Alt+1)");
    lightmap_only->setToolTip("Lightmap only (Alt+2)");
    fullbright->setToolTip("Textures without lightmap (Alt+3)");
    normals->setToolTip("Visualize normals (Alt+4)");
    drawflat->setToolTip("Flat-shaded polygons (Alt+5)");

    auto *rendermode_layout = new QVBoxLayout();
    rendermode_layout->addWidget(lightmapped);
    rendermode_layout->addWidget(lightmap_only);
    rendermode_layout->addWidget(fullbright);
    rendermode_layout->addWidget(normals);
    rendermode_layout->addWidget(drawflat);
    rendermode_layout->addWidget(hull0);
    rendermode_layout->addWidget(hull1);
    rendermode_layout->addWidget(hull2);
    rendermode_layout->addWidget(hull3);
    rendermode_layout->addWidget(hull4);
    rendermode_layout->addWidget(hull5);

    auto *rendermode_group = new QGroupBox(tr("Render mode"));
    rendermode_group->setLayout(rendermode_layout);

    auto *drawportals = new QCheckBox(tr("Draw Portals (PRT)"));
    auto *drawleak = new QCheckBox(tr("Draw Leak (PTS/LIN)"));

    auto *showtris = new QCheckBox(tr("Show Tris"));
    auto *showtris_seethrough = new QCheckBox(tr("Show Tris (See Through)"));
    auto *visculling = new QCheckBox(tr("Vis Culling"));
    visculling->setChecked(true);

    auto *keepposition = new QCheckBox(tr("Keep Camera Pos"));

    nearest = new QCheckBox(tr("Nearest Filter"));

    bspx_decoupled_lm = new QCheckBox(tr("BSPX: Decoupled Lightmap"));
    bspx_decoupled_lm->setChecked(true);

    bspx_normals = new QCheckBox(tr("BSPX: Face Normals"));
    bspx_normals->setChecked(true);

    auto *draw_opaque = new QCheckBox(tr("Draw Translucency as Opaque"));
    auto *show_bmodels = new QCheckBox(tr("Show Bmodels"));
    show_bmodels->setChecked(true);

    formLayout->addRow(tr("common"), common_options);
    formLayout->addRow(tr("qbsp"), qbsp_options);
    formLayout->addRow(vis_checkbox, vis_options);
    formLayout->addRow(tr("light"), light_options);
    formLayout->addRow(reload_button);
    formLayout->addRow(rendermode_group);
    formLayout->addRow(drawportals);
    formLayout->addRow(drawleak);
    formLayout->addRow(showtris);
    formLayout->addRow(showtris_seethrough);
    formLayout->addRow(visculling);
    formLayout->addRow(keepposition);
    formLayout->addRow(nearest);
    formLayout->addRow(bspx_decoupled_lm);
    formLayout->addRow(bspx_normals);
    formLayout->addRow(draw_opaque);
    formLayout->addRow(show_bmodels);

    lightstyles = new QVBoxLayout();

    auto *lightstyles_group = new QGroupBox(tr("Lightstyles"));
    lightstyles_group->setLayout(lightstyles);

    auto *scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setWidget(lightstyles_group);
    scrollArea->setBackgroundRole(QPalette::Window);
    scrollArea->setFrameShadow(QFrame::Plain);
    scrollArea->setFrameShape(QFrame::NoFrame);

    formLayout->addRow(scrollArea);

    auto *form = new QWidget();
    form->setLayout(formLayout);

    // finish dock setup
    dock->setWidget(form);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
    viewMenu->addAction(dock->toggleViewAction());

    // load state persisted in settings
    QSettings s;
    common_options->setText(s.value("common_options").toString());
    qbsp_options->setText(s.value("qbsp_options").toString());
    vis_checkbox->setChecked(s.value("vis_enabled").toBool());
    vis_options->setText(s.value("vis_options").toString());
    light_options->setText(s.value("light_options").toString());
    nearest->setChecked(s.value("nearest").toBool());
    if (nearest->isChecked()) {
        glView->setMagFilter(QOpenGLTexture::Nearest);
    }

    // setup event handlers

    connect(reload_button, &QAbstractButton::clicked, this, &MainWindow::reload);
    connect(lightmap_only, &QAbstractButton::toggled, this, [=](bool checked) { glView->setLighmapOnly(checked); });
    connect(fullbright, &QAbstractButton::toggled, this, [=](bool checked) { glView->setFullbright(checked); });
    connect(normals, &QAbstractButton::toggled, this, [=](bool checked) { glView->setDrawNormals(checked); });
    connect(showtris, &QAbstractButton::toggled, this, [=](bool checked) { glView->setShowTris(checked); });
    connect(showtris_seethrough, &QAbstractButton::toggled, this,
        [=](bool checked) { glView->setShowTrisSeeThrough(checked); });
    connect(visculling, &QAbstractButton::toggled, this, [=](bool checked) { glView->setVisCulling(checked); });
    connect(drawflat, &QAbstractButton::toggled, this, [=](bool checked) { glView->setDrawFlat(checked); });
    connect(hull0, &QAbstractButton::toggled, this, [=](bool checked) { glView->setDrawLeafs(checked ? std::optional<int>{0} : std::nullopt); });
    connect(hull1, &QAbstractButton::toggled, this, [=](bool checked) { glView->setDrawLeafs(checked ? std::optional<int>{1} : std::nullopt); });
    connect(hull2, &QAbstractButton::toggled, this, [=](bool checked) { glView->setDrawLeafs(checked ? std::optional<int>{2} : std::nullopt); });
    connect(hull3, &QAbstractButton::toggled, this, [=](bool checked) { glView->setDrawLeafs(checked ? std::optional<int>{3} : std::nullopt); });
    connect(hull4, &QAbstractButton::toggled, this, [=](bool checked) { glView->setDrawLeafs(checked ? std::optional<int>{4} : std::nullopt); });
    connect(hull5, &QAbstractButton::toggled, this, [=](bool checked) { glView->setDrawLeafs(checked ? std::optional<int>{5} : std::nullopt); });
    connect(drawportals, &QAbstractButton::toggled, this, [=](bool checked) { glView->setDrawPortals(checked); });
    connect(drawleak, &QAbstractButton::toggled, this, [=](bool checked) { glView->setDrawLeak(checked); });
    connect(keepposition, &QAbstractButton::toggled, this, [=](bool checked) { glView->setKeepOrigin(checked); });
    connect(nearest, &QAbstractButton::toggled, this,
        [=](bool checked) { glView->setMagFilter(checked ? QOpenGLTexture::Nearest : QOpenGLTexture::Linear); });
    connect(draw_opaque, &QAbstractButton::toggled, this,
        [=](bool checked) { glView->setDrawTranslucencyAsOpaque(checked); });
    connect(glView, &GLView::cameraMoved, this, &MainWindow::displayCameraPositionInfo);
    connect(show_bmodels, &QAbstractButton::toggled, this,
        [=](bool checked) { glView->setShowBmodels(checked); });

    // set up load timer
    m_fileReloadTimer = std::make_unique<QTimer>();

    m_fileReloadTimer->setSingleShot(true);
    m_fileReloadTimer->connect(m_fileReloadTimer.get(), &QTimer::timeout, this, &MainWindow::fileReloadTimerExpired);
}

void MainWindow::logWidgetSetText(ETLogTab tab, const std::string &str)
{
    m_outputLogWidget->setTabText((int32_t)tab, str.c_str());
}

void MainWindow::lightpreview_percent_callback(std::optional<uint32_t> percent, std::optional<duration> elapsed)
{
    int32_t tabIndex = (int32_t)m_activeLogTab;

    if (elapsed.has_value()) {
        lightpreview_log_callback(
            logging::flag::PROGRESS, fmt::format("finished in: {:.3}\n", elapsed.value()).c_str());
        QMetaObject::invokeMethod(
            this, std::bind(&MainWindow::logWidgetSetText, this, m_activeLogTab, ETLogWidget::logTabNames[tabIndex]));
    } else {
        if (percent.has_value()) {
            QMetaObject::invokeMethod(
                this, std::bind(&MainWindow::logWidgetSetText, this, m_activeLogTab,
                          fmt::format("{} [{:>3}%]", ETLogWidget::logTabNames[tabIndex], percent.value())));
        } else {
            QMetaObject::invokeMethod(this, std::bind(&MainWindow::logWidgetSetText, this, m_activeLogTab,
                                                fmt::format("{} (...)", ETLogWidget::logTabNames[tabIndex])));
        }
    }
}

void MainWindow::lightpreview_log_callback(logging::flag flags, const char *str)
{
    if (bitflags(flags) & logging::flag::PERCENT)
        return;

    if (QApplication::instance()->thread() != QThread::currentThread()) {
        QMetaObject::invokeMethod(this,
            std::bind([this, flags](const std::string &s) -> void { lightpreview_log_callback(flags, s.c_str()); },
                std::string(str)));
        return;
    }

    auto *textEdit = m_outputLogWidget->textEdit(m_activeLogTab);
    const bool atBottom = textEdit->verticalScrollBar()->value() == textEdit->verticalScrollBar()->maximum();
    QTextDocument *doc = textEdit->document();
    QTextCursor cursor(doc);
    cursor.movePosition(QTextCursor::End);
    cursor.beginEditBlock();
    cursor.insertBlock();
    cursor.insertHtml(QString::asprintf("%s\n", str));
    cursor.endEditBlock();

    // scroll scrollarea to bottom if it was at bottom when we started
    //(we don't want to force scrolling to bottom if user is looking at a
    // higher position)
    if (atBottom) {
        QScrollBar *bar = textEdit->verticalScrollBar();
        bar->setValue(bar->maximum());
    }
}

void MainWindow::createOutputLog()
{
    QDockWidget *dock = new QDockWidget(tr("Tool Logs"), this);

    m_outputLogWidget = new ETLogWidget();

    // finish dock widget setup
    dock->setWidget(m_outputLogWidget);

    addDockWidget(Qt::BottomDockWidgetArea, dock);
    viewMenu->addAction(dock->toggleViewAction());

    logging::set_print_callback(
        std::bind(&MainWindow::lightpreview_log_callback, this, std::placeholders::_1, std::placeholders::_2));
    logging::set_percent_callback(
        std::bind(&MainWindow::lightpreview_percent_callback, this, std::placeholders::_1, std::placeholders::_2));
}

void MainWindow::createStatusBar()
{
    statusBar();
}

/**
 * Precondition: openRecentMenu is created.
 *
 * Clears and rebuilds the menu given the list of files that should be displayed in it.
 */
void MainWindow::updateRecentsSubmenu(const QStringList &recents)
{
    openRecentMenu->clear();

    for (const QString &recent : recents) {
        auto *action = openRecentMenu->addAction(recent);
        connect(action, &QAction::triggered, this, [this, recent]() { loadFile(recent); });
    }

    openRecentMenu->addSeparator();
    openRecentMenu->addAction(tr("Clear Recents"), this, [this]() {
        ClearRecents();
        this->updateRecentsSubmenu(GetRecents());
    });
}

MainWindow::~MainWindow() { }

void MainWindow::setupMenu()
{
    auto *menu = menuBar()->addMenu(tr("&File"));

    auto *open = menu->addAction(tr("&Open"), this, &MainWindow::fileOpen);
    open->setShortcut(QKeySequence::Open);

    openRecentMenu = menu->addMenu(tr("Open &Recent"));
    updateRecentsSubmenu(GetRecents());

    menu->addSeparator();

    menu->addAction(tr("Save Screenshot..."), this, &MainWindow::takeScreenshot);

    menu->addSeparator();

    auto *exit = menu->addAction(tr("E&xit"), this, &QWidget::close);
    exit->setShortcut(QKeySequence::Quit);

    // view menu

    viewMenu = menuBar()->addMenu(tr("&View"));
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

void MainWindow::showEvent(QShowEvent *event)
{
    // FIXME: move command-line parsing somewhere else?
    // FIXME: support more command-line options?
    auto args = QCoreApplication::arguments();
    if (args.size() == 2) {
        QTimer::singleShot(0, this, [=] { loadFile(args.at(1)); });
    }
}

void MainWindow::fileOpen()
{
    // open the file browser in the directory containing the currently open file, if there is one
    QString currentDir;
    if (!m_mapFile.isEmpty()) {
        currentDir = QFileInfo(m_mapFile).absolutePath();
    }

    QString fileName = QFileDialog::getOpenFileName(this, tr("Open File"), currentDir, tr("Map (*.map);; BSP (*.bsp)"));

    if (!fileName.isEmpty())
        loadFile(fileName);
}

void MainWindow::takeScreenshot()
{
    QString fileName = QFileDialog::getSaveFileName(this, tr("Save Screenshot"), "", tr("PNG (*.png)"));

    if (!fileName.isEmpty())
        glView->takeScreenshot(fileName, 3840, 2160);
}

void MainWindow::fileReloadTimerExpired()
{
    qint64 currentSize = QFileInfo(m_mapFile).size();

    // it was rewritten...
    if (currentSize != m_fileSize) {
        qDebug() << "size changed since last write, restarting timer";
        m_fileReloadTimer->start(150);
        return;
    }

    // good to go? maybe?
    qDebug() << "size not changed, good to go";
    loadFileInternal(m_mapFile, true);

    m_fileSize = -1;
}

void MainWindow::loadFile(const QString &file)
{
    qDebug() << "load " << file;

    // update recents
    updateRecentsSubmenu(AddRecent(file));

    m_mapFile = file;

    if (m_watcher) {
        delete m_watcher;
    }
    m_watcher = new QFileSystemWatcher(this);
    m_fileSize = -1;

    // start watching it
    qDebug() << "adding path: " << m_watcher->addPath(file);

    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [&](const QString &path) {
        qDebug() << "got change notif for " << m_mapFile;

        // check current files' size
        m_fileSize = QFileInfo(m_mapFile).size();

        // start timer
        m_fileReloadTimer->start(150);
    });

    loadFileInternal(file, false);
}

std::filesystem::path MakeFSPath(const QString &string)
{
    return std::filesystem::path{string.toStdU16String()};
}

bspdata_t MainWindow::QbspVisLight_Common(const std::filesystem::path &name, std::vector<std::string> extra_common_args,
    std::vector<std::string> extra_qbsp_args, std::vector<std::string> extra_vis_args,
    std::vector<std::string> extra_light_args, bool run_vis)
{
    auto resetActiveTabText = [&]() {
        QMetaObject::invokeMethod(this, std::bind(&MainWindow::logWidgetSetText, this, m_activeLogTab,
                                            ETLogWidget::logTabNames[(int32_t)m_activeLogTab]));
    };

    auto bsp_path = name;
    bsp_path.replace_extension(".bsp");

    std::vector<std::string> args{
        "", // the exe path, which we're ignoring in this case
    };
    for (auto &extra : extra_common_args) {
        args.push_back(extra);
    }
    for (auto &extra : extra_qbsp_args) {
        args.push_back(extra);
    }
    args.push_back(name.string());

    // run qbsp
    m_activeLogTab = ETLogTab::TAB_BSP;

    InitQBSP(args);
    ProcessFile();

    resetActiveTabText();

    // run vis
    if (run_vis) {
        m_activeLogTab = ETLogTab::TAB_VIS;
        std::vector<std::string> vis_args{
            "", // the exe path, which we're ignoring in this case
        };
        for (auto &extra : extra_common_args) {
            vis_args.push_back(extra);
        }
        for (auto &extra : extra_vis_args) {
            vis_args.push_back(extra);
        }
        vis_args.push_back(name.string());
        vis_main(vis_args);
    }

    resetActiveTabText();

    // run light
    {
        m_activeLogTab = ETLogTab::TAB_LIGHT;
        std::vector<std::string> light_args{
            "", // the exe path, which we're ignoring in this case
        };
        for (auto &extra : extra_common_args) {
            light_args.push_back(extra);
        }
        for (auto &arg : extra_light_args) {
            light_args.push_back(arg);
        }
        light_args.push_back(name.string());

        light_main(light_args);
    }

    resetActiveTabText();

    m_activeLogTab = ETLogTab::TAB_LIGHTPREVIEW;

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
            for (const auto &str2 : str.split(' ', Qt::SkipEmptyParts)) {
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

class QLightStyleSlider : public QFrame
{
public:
    int32_t style_id;

    QLightStyleSlider(int32_t style_id, GLView *glView)
        : QFrame(),
          style_id(style_id),
          glView(glView)
    {
        auto *style_layout = new QHBoxLayout();

        auto *style = new QSpinBox();
        style->setRange(0, 200);
        style->setValue(100);
        style->setSingleStep(10);
        // style->setTickPosition(QSlider::TicksBothSides);
        // style->setTickInterval(50);

        connect(style, QOverload<int>::of(&QSpinBox::valueChanged), this, &QLightStyleSlider::setValue);

        auto *style_label = new QLabel();
        style_label->setText(QString::asprintf("%i", style_id));

        style_layout->addWidget(style_label);
        style_layout->addWidget(style);

        setLayout(style_layout);
        setFrameShadow(QFrame::Plain);
        setFrameShape(QFrame::NoFrame);
    }

private:
    void setValue(int value) { glView->setLightStyleIntensity(style_id, value); }

    GLView *glView;
};

int MainWindow::compileMap(const QString &file, bool is_reload)
{
    fs::path fs_path = MakeFSPath(file);

    m_bspdata = {};
    render_settings.reset();

    try {
        if (fs_path.extension().compare(".bsp") == 0) {

            LoadBSPFile(fs_path, &m_bspdata);

            auto opts = ParseArgs(common_options);

            std::vector<const char *> argPtrs;

            argPtrs.push_back("");

            for (const std::string &arg : opts) {
                argPtrs.push_back(arg.data());
            }

            render_settings.preinitialize(argPtrs.size(), argPtrs.data());
            render_settings.initialize(argPtrs.size() - 1, argPtrs.data() + 1);
            render_settings.postinitialize(argPtrs.size(), argPtrs.data());

            m_bspdata.version->game->init_filesystem(fs_path, render_settings);

            ConvertBSPFormat(&m_bspdata, &bspver_generic);

        } else {
            m_bspdata = QbspVisLight_Common(fs_path, ParseArgs(common_options), ParseArgs(qbsp_options),
                ParseArgs(vis_options), ParseArgs(light_options), vis_checkbox->isChecked());

            // FIXME: move to a lightpreview_settings
            settings::common_settings settings;

            // FIXME: copy the -path args from light
            settings.paths.copy_from(::light_options.paths);

            m_bspdata.loadversion->game->init_filesystem(file.toStdString(), settings);
        }
    } catch (const settings::parse_exception &p) {
        // FIXME: threading error: don't call Qt widgets code from background thread
        auto *textEdit = m_outputLogWidget->textEdit(m_activeLogTab);
        textEdit->append(QString::fromUtf8(p.what()) + QString::fromLatin1("\n"));
        m_activeLogTab = ETLogTab::TAB_LIGHTPREVIEW;
        return 1;
    } catch (const settings::quit_after_help_exception &p) {
        // FIXME: threading error: don't call Qt widgets code from background thread
        auto *textEdit = m_outputLogWidget->textEdit(m_activeLogTab);
        textEdit->append(QString::fromUtf8(p.what()) + QString::fromLatin1("\n"));
        m_activeLogTab = ETLogTab::TAB_LIGHTPREVIEW;
        return 1;
    } catch (const std::exception &other) {
        // FIXME: threading error: don't call Qt widgets code from background thread
        auto *textEdit = m_outputLogWidget->textEdit(m_activeLogTab);
        textEdit->append(QString::fromUtf8(other.what()) + QString::fromLatin1("\n"));
        m_activeLogTab = ETLogTab::TAB_LIGHTPREVIEW;
        return 1;
    }

    // try to load .lit
    auto lit_path = fs_path;
    lit_path.replace_extension(".lit");

    try {
        m_litdata = LoadLitFile(lit_path);
    } catch (const std::runtime_error &error) {
        logging::print("error loading lit: {}", error.what());
        m_litdata = {};
    }

    return 0;
}

void MainWindow::compileThreadExited()
{
    // clear lightstyle widgets
    while (QWidget *w = lightstyles->parentWidget()->findChild<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
        delete w;
    }

    delete m_compileThread;
    m_compileThread = nullptr;

    if (!std::holds_alternative<mbsp_t>(m_bspdata.bsp)) {
        return;
    }
    const auto &bsp = std::get<mbsp_t>(m_bspdata.bsp);

    auto ents = EntData_Parse(bsp);

    // build lightmap atlas
    auto atlas = build_lightmap_atlas(bsp, m_bspdata.bspx.entries, m_litdata, false, bspx_decoupled_lm->isChecked());

    glView->renderBSP(m_mapFile, bsp, m_bspdata.bspx.entries, ents, atlas, render_settings, bspx_normals->isChecked());

    if (!m_fileWasReload && !glView->getKeepOrigin()) {
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

    // set lightstyle data
    for (auto &style_entry : atlas.style_to_lightmap_atlas) {

        auto *style = new QLightStyleSlider(style_entry.first, glView);
        lightstyles->addWidget(style);
    }
}

void MainWindow::loadFileInternal(const QString &file, bool is_reload)
{
    // TODO
    if (m_compileThread)
        return;

    qDebug() << "loadFileInternal " << file;

    // just in case
    m_fileReloadTimer->stop();
    m_fileWasReload = is_reload;

    // persist settings
    QSettings s;
    s.setValue("common_options", common_options->text());
    s.setValue("qbsp_options", qbsp_options->text());
    s.setValue("vis_enabled", vis_checkbox->isChecked());
    s.setValue("vis_options", vis_options->text());
    s.setValue("light_options", light_options->text());
    s.setValue("nearest", nearest->isChecked());

    // update title bar
    setWindowFilePath(file);
    setWindowTitle(QFileInfo(file).fileName() + " - lightpreview");

    for (auto &edit : m_outputLogWidget->textEdits()) {
        edit->clear();
    }

    m_compileThread = QThread::create(std::bind(&MainWindow::compileMap, this, file, is_reload));
    connect(m_compileThread, &QThread::finished, this, &MainWindow::compileThreadExited);
    m_compileThread->start();
}

void MainWindow::displayCameraPositionInfo()
{
    const auto *bsp = std::get_if<mbsp_t>(&m_bspdata.bsp);
    if (!bsp)
        return;

    const qvec3f point = glView->cameraPosition();
    [[maybe_unused]] const mleaf_t *leaf = BSP_FindLeafAtPoint(bsp, &bsp->dmodels[0], point);

    // TODO: display leaf info
}
