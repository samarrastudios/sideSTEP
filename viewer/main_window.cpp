#include "main_window.h"

#include "hud.h"
#include "icons.h"
#include "measure.h"
#include "measure_tool.h"
#include "occ_view.h"
#include "open_with.h"
#include "section_tool.h"
#include "unblock.h"
#include "view_cube.h"

#include <TopLoc_Location.hxx>

#include <QtConcurrent/QtConcurrentRun>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QCloseEvent>
#include <QCollator>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace {

const QStringList kPatterns = {"*.step", "*.stp", "*.p21"};
const QColor kInk(0x11, 0x13, 0x16), kPaper(0xF3, 0xF1, 0xEA), kDim(0x8A, 0x8C, 0x90);

bool IsStepFile(const QString& path) {
    const QString s = QFileInfo(path).suffix().toLower();
    return s == "step" || s == "stp" || s == "p21";
}

QString Count(long long n) {
    if (n >= 1000000) return QStringLiteral("%1M").arg(n / 1e6, 0, 'f', 1);
    if (n >= 1000) return QStringLiteral("%1k").arg(n / 1e3, 0, 'f', 0);
    return QString::number(n);
}

// Standard views: where the eye is, and which way is up.
struct StandardView { const char* name; const char* menu; double eye[3]; double up[3]; };
const StandardView kViews[] = {
    {"front", "F&ront", {0, -1, 0}, {0, 0, 1}},  {"back", "&Back", {0, 1, 0}, {0, 0, 1}},
    {"left", "&Left", {-1, 0, 0}, {0, 0, 1}},    {"right", "Ri&ght", {1, 0, 0}, {0, 0, 1}},
    {"top", "&Top", {0, 0, 1}, {0, 1, 0}},       {"bottom", "B&ottom", {0, 0, -1}, {0, -1, 0}},
    {"iso", "&Isometric", {1, -1, 1}, {0, 0, 1}}};

QString Number(double v, int decimals) { return QString::number(v, 'f', decimals); }

// Mouse styles, in the order OccView::setMousePreset numbers them.
struct MouseStyle { const char* name; const char* summary; bool reverseZoom; };
const MouseStyle kMouseStyles[] = {
    {"sideSTEP", "left drag turns, right or middle drag pans, scroll zooms", false},
    {"SolidWorks", "middle drag turns, Ctrl+middle pans, Shift+middle zooms", true},
    {"Fusion / Inventor", "middle drag pans, Shift+middle turns, scroll zooms", false},
    {"Onshape", "right drag turns, middle drag pans, scroll zooms", false},
    {"Creo / NX", "middle drag turns, Shift+middle pans, Ctrl+middle zooms", false},
    {"Rhino", "right drag turns, Shift+right pans, Ctrl+right zooms", false}};
const int kMouseStyleCount = int(sizeof(kMouseStyles) / sizeof(kMouseStyles[0]));

}  // namespace

MainWindow::MainWindow() {
    setWindowTitle(QStringLiteral("sideSTEP"));
    setAcceptDrops(true);
    resize(1400, 880);
    buildChrome();
    buildActions();

    connect(view_, &OccView::selectionChanged, this, &MainWindow::onViewSelection);
    connect(view_, &OccView::contextRequested, this, &MainWindow::onContextMenu);
    connect(view_, &OccView::viewRedrawn, this, [this] {
        measure_->placeLabels();
        gp_Dir eye, up;
        view_->cameraFrame(eye, up);
        hud_->cube()->setCamera(eye, up);
    });
    connect(hud_->cube(), &ViewCube::lookFrom, this, [this](const gp_Dir& eye, const gp_Dir& up) { lookFrom(eye, up, false); });
    connect(hud_->cube(), &ViewCube::home, this, [this] { standardView(6); });
    connect(tree_, &QTreeWidget::itemSelectionChanged, this, &MainWindow::onTreeSelection);
    connect(tree_, &QTreeWidget::itemClicked, this, &MainWindow::onTreeClicked);
    connect(filter_, &QLineEdit::textChanged, this, &MainWindow::filterTree);

    QSettings settings;
    setLightBackground(settings.value(QStringLiteral("view/lightBackground"), true).toBool());
    reverseZoom_->setChecked(settings.value(QStringLiteral("view/reverseZoom"), false).toBool());
    view_->setReverseZoom(reverseZoom_->isChecked());
    setMouseStyle(settings.value(QStringLiteral("view/mouse"), 0).toInt(), false);
    setBusy(false);
}

MainWindow::~MainWindow() {
    if (loading_) loading_->cancel = true;
    clearModel();
}

// Window layout: tree on the left, the model with its floating controls in
// the middle, a readout strip under the model.
void MainWindow::buildChrome() {
    auto* centre = new QWidget(this);
    auto* cl = new QVBoxLayout(centre);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(0);

    stack_ = new QStackedWidget(centre);
    auto* welcome = new QWidget(stack_);
    auto* wl = new QVBoxLayout(welcome);
    auto* lockup = new QLabel(welcome);
    lockup->setPixmap(QPixmap(QStringLiteral(":/brand/lockup/sidestep-lockup-dark.png")).scaledToWidth(360, Qt::SmoothTransformation));
    lockup->setAlignment(Qt::AlignCenter);
    welcomeText_ = new QLabel(welcome);
    welcomeText_->setAlignment(Qt::AlignCenter);
    welcomeText_->setWordWrap(true);
    auto* openButton = new QPushButton(tr("Open a STEP file"), welcome);
    connect(openButton, &QPushButton::clicked, this, &MainWindow::chooseFile);
    wl->addStretch(2);
    wl->addWidget(lockup);
    wl->addSpacing(24);
    wl->addWidget(welcomeText_);
    wl->addSpacing(12);
    wl->addWidget(openButton, 0, Qt::AlignCenter);
    wl->addStretch(3);
    stack_->addWidget(welcome);
    view_ = new OccView(stack_);
    stack_->addWidget(view_);
    cl->addWidget(stack_, 1);

    // Readout strip: the whole model's size, or the selected part's.
    readout_ = new QFrame(centre);
    readout_->setObjectName(QStringLiteral("readout"));
    auto* rl = new QHBoxLayout(readout_);
    rl->setContentsMargins(14, 7, 10, 7);
    rl->setSpacing(14);
    readoutName_ = new QLabel(readout_);
    readoutName_->setStyleSheet(QStringLiteral("font-weight:600;"));
    readoutPath_ = new QLabel(readout_);
    readoutPath_->setStyleSheet(QStringLiteral("color:#8A8C90;"));
    readoutSize_ = new QLabel(readout_);
    readoutSize_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    readoutShowOnly_ = new QPushButton(tr("Show only"), readout_);
    readoutHide_ = new QPushButton(tr("Hide"), readout_);
    for (QPushButton* b : {readoutShowOnly_, readoutHide_}) b->setStyleSheet(QStringLiteral("padding:3px 12px;"));
    readoutShowOnly_->setToolTip(tr("Fade everything else (I)"));
    readoutHide_->setToolTip(tr("Hide the selected parts (H)"));
    rl->addWidget(readoutName_);
    rl->addWidget(readoutPath_);
    rl->addWidget(readoutSize_);
    rl->addStretch(1);
    rl->addWidget(readoutShowOnly_);
    rl->addWidget(readoutHide_);
    // Mouse style switcher: always in sight, one click to change.
    mouseButton_ = new QToolButton(readout_);
    mouseButton_->setPopupMode(QToolButton::InstantPopup);
    mouseButton_->setToolTip(tr("How the mouse turns, pans and zooms the model. Pick the CAD program your hand already knows."));
    mouseButton_->setStyleSheet(QStringLiteral("QToolButton { padding:3px 20px 3px 10px; color:#B9B7B0; } QToolButton::menu-indicator { subcontrol-position: right center; right: 6px; }"));
    rl->addWidget(mouseButton_);
    connect(readoutShowOnly_, &QPushButton::clicked, this, &MainWindow::showOnlySelected);
    connect(readoutHide_, &QPushButton::clicked, this, &MainWindow::hideSelected);
    cl->addWidget(readout_);
    setCentralWidget(centre);

    hud_ = new Hud(this, view_);

    // --- left: the part tree, cascading as the file has it ---
    auto* dock = new QDockWidget(tr("Parts"), this);
    dock->setObjectName(QStringLiteral("modelDock"));
    dock->setFeatures(QDockWidget::DockWidgetMovable);
    auto* side = new QWidget(dock);
    auto* sl = new QVBoxLayout(side);
    sl->setContentsMargins(0, 0, 0, 0);
    sl->setSpacing(0);
    filter_ = new QLineEdit(side);
    filter_->setPlaceholderText(tr("Search parts"));
    filter_->setClearButtonEnabled(true);
    tree_ = new QTreeWidget(side);
    tree_->setColumnCount(3);
    tree_->setHeaderHidden(true);
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree_->setUniformRowHeights(true);
    tree_->setIndentation(16);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    tree_->header()->setSectionResizeMode(2, QHeaderView::Fixed);
    tree_->setColumnWidth(1, 44);
    tree_->setColumnWidth(2, 30);
    sl->addWidget(filter_);
    sl->addWidget(tree_, 1);
    dock->setWidget(side);
    addDockWidget(Qt::LeftDockWidgetArea, dock);

    // --- right: measurements, while the measure tool is on ---
    measureDock_ = new QDockWidget(tr("Measurements"), this);
    measureDock_->setObjectName(QStringLiteral("measureDock"));
    measureDock_->setFeatures(QDockWidget::DockWidgetMovable);
    measure_ = new MeasureTool(view_, measureDock_);
    measureDock_->setWidget(measure_);
    addDockWidget(Qt::RightDockWidgetArea, measureDock_);
    measureDock_->hide();
    resizeDocks({dock, measureDock_}, {310, 290}, Qt::Horizontal);

    // --- tool settings that sit in the pill ---
    section_ = new SectionTool(view_);
    box_ = new SectionBoxTool(view_);
    explodeStrip_ = new QWidget;
    auto* el = new QHBoxLayout(explodeStrip_);
    el->setContentsMargins(0, 0, 0, 0);
    explode_ = new QSlider(Qt::Horizontal, explodeStrip_);
    explode_->setRange(0, 100);
    explode_->setFixedWidth(220);
    explode_->setFocusPolicy(Qt::NoFocus);
    explode_->setToolTip(tr("How far the parts are spread apart"));
    el->setSpacing(4);
    auto* directions = new QButtonGroup(explodeStrip_);
    const char* directionNames[] = {"All", "X", "Y", "Z"};
    const char* directionTips[] = {"Spread in every direction", "Spread along X only", "Spread along Y only", "Spread along Z only (up and down)"};
    for (int i = 0; i < 4; ++i) {
        auto* b = new QToolButton(explodeStrip_);
        b->setText(tr(directionNames[i]));
        b->setToolTip(tr(directionTips[i]));
        b->setCheckable(true);
        b->setChecked(i == 0);
        b->setFocusPolicy(Qt::NoFocus);
        directions->addButton(b, i);
        el->addWidget(b);
    }
    el->addSpacing(6);
    el->addWidget(explode_);
    connect(explode_, &QSlider::valueChanged, this, &MainWindow::setExplode);
    connect(directions, &QButtonGroup::idClicked, this, [this](int id) {
        explodeAxis_ = id - 1;
        applyExplode(explodeNow_, true);
    });
    explodeTimer_ = new QTimer(this);
    connect(explodeTimer_, &QTimer::timeout, this, &MainWindow::explodeTick);
    measureHint_ = new QLabel(tr("Pick what to measure in the panel on the right, then click the model."));

    // --- status bar ---
    status_ = new QLabel(this);
    info_ = new QLabel(this);
    progress_ = new QProgressBar(this);
    progress_->setFixedWidth(180);
    progress_->setTextVisible(false);
    progress_->hide();
    cancelButton_ = new QToolButton(this);
    cancelButton_->setText(tr("Cancel"));
    cancelButton_->hide();
    connect(cancelButton_, &QToolButton::clicked, this, [this] {
        if (loading_) loading_->cancel = true;
        loading_.reset();
        setBusy(false);
    });
    statusBar()->addWidget(status_, 1);
    statusBar()->addPermanentWidget(progress_);
    statusBar()->addPermanentWidget(cancelButton_);
    statusBar()->addPermanentWidget(info_);
}

void MainWindow::buildActions() {
    auto add = [this](QMenu* menu, const QString& text, const QList<QKeySequence>& keys, auto slot, bool needsModel = true) {
        QAction* a = menu->addAction(text);
        if (!keys.isEmpty()) a->setShortcuts(keys);
        connect(a, &QAction::triggered, this, slot);
        if (needsModel) modelActions_.push_back(a);
        return a;
    };

    // ---- File ----
    QMenu* file = menuBar()->addMenu(tr("&File"));
    QAction* open = add(file, tr("&Open..."), {QKeySequence::Open}, [this] { chooseFile(); }, false);
    QAction* prev = add(file, tr("&Previous file in folder"), {Qt::Key_PageUp}, [this] { openSibling(-1); });
    QAction* next = add(file, tr("&Next file in folder"), {Qt::Key_PageDown}, [this] { openSibling(+1); });
    file->addSeparator();
    openExternal_ = add(file, QString(), {Qt::CTRL | Qt::Key_E}, [this] { openInExternalApp(); });
    openWithMenu_ = file->addMenu(tr("Open &with"));
    connect(openWithMenu_, &QMenu::aboutToShow, this, &MainWindow::rebuildOpenWithMenu);
    add(file, tr("Show in &folder"), {}, [this] { RevealInExplorer(path_); });
    file->addSeparator();
    add(file, tr("Make sideSTEP the &default for STEP files..."), {}, [] { OpenDefaultAppsSettings(); }, false);
    // The background helper that keeps Explorer's preview pane from refusing downloaded STEP files.
    QAction* helper = add(file, tr("Preview downloaded STEP files in Explorer without a &warning"), {}, [this](bool on) {
        SetWatcherEnabled(on);
        QMessageBox::information(this, tr("sideSTEP"),
            on ? tr("On. Windows marks downloaded files and then refuses to preview them. sideSTEP now removes that "
                    "mark from STEP files (.step, .stp, .p21), and from no other file type, using a small helper that "
                    "starts when you sign in.")
               : tr("Off. sideSTEP no longer removes Windows' \"downloaded from the internet\" mark from STEP files. "
                    "Files that are already cleared stay cleared; newly downloaded ones will show Windows' warning in "
                    "the preview pane until you choose Preview anyway."));
    }, false);
    helper->setCheckable(true);
    helper->setChecked(IsWatcherEnabled());
    connect(file, &QMenu::aboutToShow, helper, [helper] { helper->setChecked(IsWatcherEnabled()); });
    file->addSeparator();
    add(file, tr("E&xit"), {}, [this] { close(); }, false);

    // ---- View ----
    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
    QAction* fit = add(viewMenu, tr("&Fit"), {Qt::Key_F}, [this] { fitVisible(); });
    viewMenu->addSeparator();
    // Standard views answer to Ctrl+number (as in SolidWorks) and Shift+number (as in Onshape).
    const int shifted[] = {Qt::Key_Exclam, Qt::Key_At, Qt::Key_NumberSign, Qt::Key_Dollar, Qt::Key_Percent, Qt::Key_AsciiCircum, Qt::Key_Ampersand};
    for (int i = 0; i < 7; ++i) {
        QList<QKeySequence> keys = {QKeySequence(Qt::CTRL | (Qt::Key_1 + i)), QKeySequence(Qt::SHIFT | (Qt::Key_1 + i)),
                                    QKeySequence(Qt::SHIFT | shifted[i])};
        if (i == 6) keys << QKeySequence(Qt::Key_Home);
        add(viewMenu, tr(kViews[i].menu), keys, [this, i] { standardView(i); });
    }
    viewMenu->addSeparator();
    QSettings settings;
    QAction* persp = add(viewMenu, tr("&Perspective"), {Qt::Key_O}, [this](bool on) {
        view_->setPerspective(on);
        QSettings().setValue(QStringLiteral("view/perspective"), on);
    }, false);
    persp->setCheckable(true);
    persp->setChecked(settings.value(QStringLiteral("view/perspective"), false).toBool());
    view_->setPerspective(persp->isChecked());

    // Display: one list, one style at a time.
    displayMenu_ = viewMenu->addMenu(tr("&Display"));
    displayActions_ = new QActionGroup(this);
    struct Style { const char* text; int key; DisplayMode mode; };
    const Style styles[] = {{"Shaded with &edges", Qt::Key_E, ShadedWithEdges}, {"&Shaded", 0, Shaded},
                            {"See-&through", Qt::Key_T, SeeThrough},           {"&Wireframe", Qt::Key_W, Wireframe}};
    for (const Style& s : styles) {
        const DisplayMode mode = s.mode;
        QAction* a = add(displayMenu_, tr(s.text), s.key ? QList<QKeySequence>{QKeySequence(s.key)} : QList<QKeySequence>{},
                         [this, mode] { setDisplayMode(mode); }, false);
        a->setCheckable(true);
        a->setChecked(mode == displayMode_);
        a->setData(int(mode));
        displayActions_->addAction(a);
    }
    displayMenu_->addSeparator();
    displayMenu_->addAction(persp);
    QAction* shadows = add(displayMenu_, tr("Shado&ws"), {}, [this](bool on) {
        view_->setShadows(on);
        QSettings().setValue(QStringLiteral("view/shadows"), on);
    }, false);
    shadows->setCheckable(true);
    shadows->setChecked(settings.value(QStringLiteral("view/shadows"), true).toBool());
    view_->setShadows(shadows->isChecked());
    QAction* light = add(viewMenu, tr("Light back&ground"), {}, [this](bool on) {
        setLightBackground(on);
        QSettings().setValue(QStringLiteral("view/lightBackground"), on);
    }, false);
    light->setCheckable(true);
    light->setChecked(settings.value(QStringLiteral("view/lightBackground"), true).toBool());

    // Mouse: the mappings CAD users already have in their hands.
    mouseMenu_ = viewMenu->addMenu(tr("&Mouse"));
    auto* mouseGroup = new QActionGroup(this);
    for (int i = 0; i < kMouseStyleCount; ++i) {
        QAction* a = add(mouseMenu_, QStringLiteral("%1   -   %2").arg(tr(kMouseStyles[i].name), tr(kMouseStyles[i].summary)), {},
                         [this, i] { setMouseStyle(i, true); }, false);
        a->setCheckable(true);
        mouseGroup->addAction(a);
    }
    mouseMenu_->addSeparator();
    reverseZoom_ = add(mouseMenu_, tr("&Reverse the scroll wheel's zoom direction"), {}, [this](bool on) {
        view_->setReverseZoom(on);
        QSettings().setValue(QStringLiteral("view/reverseZoom"), on);
    }, false);
    reverseZoom_->setCheckable(true);
    viewMenu->addSeparator();
    fullScreenAction_ = add(viewMenu, tr("F&ull screen"), {Qt::SHIFT | Qt::Key_F, Qt::Key_F11}, [this] { toggleFullScreen(); }, false);

    // ---- Tools ----
    QMenu* tools = menuBar()->addMenu(tr("&Tools"));
    struct ToolDef { Tool tool; const char* text; int key; };
    const ToolDef defs[] = {{MeasureTool_, "&Measure", Qt::Key_M}, {SectionTool_, "&Section", Qt::Key_S},
                            {SectionBoxTool_, "Section &box", Qt::Key_B}, {ExplodeTool_, "E&xplode", Qt::Key_X}};
    for (const ToolDef& d : defs) {
        const Tool tool = d.tool;
        QAction* a = add(tools, tr(d.text), {QKeySequence(d.key)}, [this, tool] { setTool(tool); });
        a->setCheckable(true);
        toolActions_.insert(tool, a);
    }
    tools->addSeparator();
    QAction* parts = add(tools, tr("&Parts list"), {Qt::Key_P}, [this] { showParts(); });
    QAction* snap = add(tools, tr("S&napshot"), {Qt::Key_C}, [this] { snapshot(); });
    tools->addSeparator();
    add(tools, tr("&Hide selected"), {Qt::Key_H}, [this] { hideSelected(); });
    add(tools, tr("Show &only selected"), {Qt::Key_I}, [this] { showOnlySelected(); });
    add(tools, tr("Show &all"), {Qt::SHIFT | Qt::Key_H}, [this] { showAll(); });

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    add(help, tr("&About sideSTEP"), {}, [this] { showAbout(); }, false);

    auto* esc = new QAction(this);
    esc->setShortcut(Qt::Key_Escape);
    connect(esc, &QAction::triggered, this, &MainWindow::escape);
    addAction(esc);

    // ---- top bar: the file, and the hand-off ----
    QToolBar* bar = addToolBar(tr("File"));
    bar->setObjectName(QStringLiteral("mainBar"));
    bar->setMovable(false);
    bar->setIconSize(QSize(18, 18));
    bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    bar->setContextMenuPolicy(Qt::PreventContextMenu);
    open->setIcon(MakeIcon(Icon::Open));
    open->setIconText(tr("Open"));
    open->setToolTip(tr("Open a STEP file (Ctrl+O)"));
    prev->setIcon(MakeIcon(Icon::Previous));
    prev->setIconText(tr("Previous"));
    prev->setToolTip(tr("Previous STEP file in this folder (Page Up)"));
    next->setIcon(MakeIcon(Icon::Next));
    next->setIconText(tr("Next"));
    next->setToolTip(tr("Next STEP file in this folder (Page Down)"));
    bar->addActions({open, prev, next});
    auto* spacer = new QWidget(bar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    bar->addWidget(spacer);
    openExternal_->setIcon(MakeIcon(Icon::OpenIn));
    bar->addAction(openExternal_);

    // ---- the floating bar over the model ----
    auto hudButton = [this](Icon icon, const QString& label, const QString& tip, bool checkable, bool gap) {
        QToolButton* b = hud_->addButton(QIcon(), label, tip, checkable, gap);
        b->setProperty("hudIcon", int(icon));
        modelWidgets_.push_back(b);
        return b;
    };
    QToolButton* fitButton = hudButton(Icon::Fit, tr("Fit"), tr("Fit the model to the view  (F)"), false, false);
    connect(fitButton, &QToolButton::clicked, fit, &QAction::trigger);
    QToolButton* displayButton = hudButton(Icon::Display, tr("Display"), tr("Shaded with edges (E), Shaded, See-through (T), Wireframe (W)"), false, false);
    connect(displayButton, &QToolButton::clicked, this, [this, displayButton] {
        displayMenu_->exec(displayButton->mapToGlobal(QPoint(0, -displayMenu_->sizeHint().height() - 6)));
    });
    const struct { Tool tool; Icon icon; const char* label; const char* tip; } toolButtons[] = {
        {MeasureTool_, Icon::Measure, "Measure", "Measure distances, lengths, diameters, angles and sizes  (M)"},
        {SectionTool_, Icon::Section, "Section", "Cut the model with a plane to see inside  (S)"},
        {SectionBoxTool_, Icon::SectionBox, "Section box", "Keep only what is inside a box  (B)"},
        {ExplodeTool_, Icon::Explode, "Explode", "Spread the parts apart  (X)"}};
    bool first = true;
    for (const auto& t : toolButtons) {
        const Tool tool = t.tool;
        QToolButton* b = hudButton(t.icon, tr(t.label), tr(t.tip), true, first);
        first = false;
        connect(b, &QToolButton::clicked, this, [this, tool] { setTool(tool); });
        toolButtons_.insert(tool, b);
    }
    QToolButton* partsButton = hudButton(Icon::Parts, tr("Parts"), tr("Every part with how many there are  (P)"), false, true);
    connect(partsButton, &QToolButton::clicked, parts, &QAction::trigger);
    QToolButton* snapButton = hudButton(Icon::Snapshot, tr("Snapshot"), tr("Save a picture of this view and copy it  (C)"), false, false);
    connect(snapButton, &QToolButton::clicked, snap, &QAction::trigger);
    fullScreenButton_ = hudButton(Icon::FullScreen, tr("Full screen"), tr("Fill the screen  (Shift+F)"), true, false);
    connect(fullScreenButton_, &QToolButton::clicked, this, &MainWindow::toggleFullScreen);

    setExternalApp(settings.value(QStringLiteral("externalApp/id")).toString(),
                   settings.value(QStringLiteral("externalApp/name")).toString());
}

// ---------------------------------------------------------------- loading ---

void MainWindow::chooseFile() {
    const QString dir = path_.isEmpty() ? QSettings().value(QStringLiteral("lastDir")).toString()
                                        : QFileInfo(path_).absolutePath();
    const QString path = QFileDialog::getOpenFileName(this, tr("Open a STEP file"), dir,
                                                      tr("STEP files (*.step *.stp *.p21);;All files (*)"));
    if (!path.isEmpty()) openFile(path);
}

void MainWindow::openSibling(int step) {
    if (path_.isEmpty()) return;
    const QFileInfo current(path_);
    QStringList names = current.absoluteDir().entryList(kPatterns, QDir::Files);
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(names.begin(), names.end(), collator);
    if (names.size() < 2) return;
    int i = (int)names.indexOf(current.fileName());
    if (i < 0) i = 0;
    i = (i + step + (int)names.size()) % (int)names.size();
    openFile(current.absoluteDir().filePath(names[i]));
}

void MainWindow::openFile(const QString& path) {
    if (loading_) loading_->cancel = true;
    auto load = std::make_shared<Load>();
    load->path = QFileInfo(path).absoluteFilePath();
    loading_ = load;
    // Opening a file here is a statement of trust; let Explorer preview it too.
    UnblockStepFile(QDir::toNativeSeparators(load->path).toStdWString());
    setBusy(true, tr("Loading %1...").arg(QFileInfo(path).fileName()));
    QSettings().setValue(QStringLiteral("lastDir"), QFileInfo(path).absolutePath());

    QPointer<MainWindow> self(this);
    (void)QtConcurrent::run([self, load] {
        load->model = std::make_shared<viewer::Model>();
        QElapsedTimer tick;
        tick.start();
        qint64 last = -1000;
        const viewer::ProgressFn progress = [&](const char* stage, double fraction) {
            if (tick.elapsed() - last < 60) return;  // do not flood the UI thread
            last = tick.elapsed();
            const QString text = QString::fromLatin1(stage);
            QMetaObject::invokeMethod(qApp, [self, load, text, fraction] {
                if (!self || self->loading_ != load) return;
                self->progress_->setRange(0, fraction < 0 ? 0 : 1000);
                if (fraction >= 0) self->progress_->setValue(int(fraction * 1000));
                self->status_->setText(QStringLiteral("%1: %2").arg(QFileInfo(load->path).fileName(), text));
            }, Qt::QueuedConnection);
        };
        load->ok = viewer::LoadModel(load->path.toStdWString(), load->cancel, progress, *load->model, load->error);
        QMetaObject::invokeMethod(qApp, [self, load] {
            if (self) self->loadFinished(load);
        }, Qt::QueuedConnection);
    });
}

void MainWindow::loadFinished(const std::shared_ptr<Load>& load) {
    if (load != loading_) return;  // superseded or cancelled
    loading_.reset();
    setBusy(false);
    if (!load->ok) {
        if (load->error != "cancelled")
            QMessageBox::warning(this, tr("sideSTEP"),
                                 tr("%1 could not be opened.\n\n%2")
                                     .arg(QFileInfo(load->path).fileName(), QString::fromStdString(load->error)));
        return;
    }
    showModel(load->model, load->path);
}

void MainWindow::setBusy(bool busy, const QString& text) {
    progress_->setVisible(busy);
    cancelButton_->setVisible(busy);
    if (busy) progress_->setRange(0, 0);
    status_->setText(text);
    const bool hasModel = model_ != nullptr;
    for (QAction* a : modelActions_) a->setEnabled(hasModel);
    for (QWidget* w : modelWidgets_) w->setEnabled(hasModel);
    filter_->setEnabled(hasModel);
    readout_->setVisible(hasModel);
    if (!hasModel)
        welcomeText_->setText(busy ? text
                                   : tr("Drop a STEP file here, or press Ctrl+O.\n"
                                        "Drag to turn the model, right-drag to pan, scroll to zoom."));
}

void MainWindow::clearModel() {
    setTool(NoTool);
    explodeTimer_->stop();
    explodeNow_ = explodeTarget_ = 0;
    measure_->setModel({}, Bnd_Box(), nullptr);
    if (partsDialog_) partsDialog_->close();
    view_->context()->RemoveAll(false);
    syncing_ = true;
    tree_->clear();
    filter_->clear();
    syncing_ = false;
    items_.clear();
    visible_.clear();
    solo_.clear();
    nodeByObject_.clear();
    model_.reset();
}

void MainWindow::showModel(const std::shared_ptr<viewer::Model>& model, const QString& path) {
    QElapsedTimer timer;
    timer.start();
    clearModel();
    model_ = model;
    path_ = path;

    const Handle(AIS_InteractiveContext)& ctx = view_->context();
    const int n = (int)model->nodes.size();
    QVector<Handle(AIS_InteractiveObject)> parts;
    QVector<int> leafCount(n, 0);
    for (int i = n - 1; i >= 0; --i) {
        if (!model->nodes[i].ais.IsNull()) leafCount[i] = 1;
        if (model->nodes[i].parent >= 0) leafCount[model->nodes[i].parent] += leafCount[i];
    }
    syncing_ = true;
    items_.resize(n);
    visible_.fill(true, n);
    offsets_.fill(gp_Vec(), n);
    for (int i = 0; i < n; ++i) {
        const viewer::ModelNode& node = model->nodes[i];
        QTreeWidgetItem* item = node.parent >= 0 ? new QTreeWidgetItem(items_[node.parent]) : new QTreeWidgetItem(tree_);
        item->setText(0, QString::fromStdU16String(node.name));
        item->setToolTip(0, item->text(0));
        item->setData(0, Qt::UserRole, i);
        if (node.isAssembly) {
            item->setText(1, QString::number(leafCount[i]));
            item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
            item->setForeground(1, kDim);
        }
        items_[i] = item;
        if (!node.ais.IsNull()) {
            applyLook(i);
            ctx->Display(node.ais, displayMode_ == Wireframe ? AIS_WireFrame : AIS_Shaded, 0, false);
            nodeByObject_.insert(node.ais.get(), i);
            parts.push_back(node.ais);
        }
    }
    tree_->expandToDepth(0);
    syncing_ = false;
    refreshTreeLooks();

    // The middle of every item, assemblies included, for the explode tool.
    {
        QVector<Bnd_Box> boxes(n);
        for (int i = n - 1; i >= 0; --i) {
            if (!model->nodes[i].ais.IsNull()) boxes[i] = model->nodes[i].bounds;
            if (model->nodes[i].parent >= 0 && !boxes[i].IsVoid()) boxes[model->nodes[i].parent].Add(boxes[i]);
        }
        double x0, y0, z0, x1, y1, z1;
        model->bounds.Get(x0, y0, z0, x1, y1, z1);
        centres_.fill(gp_Pnt((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2), n);
        for (int i = 0; i < n; ++i) {
            if (boxes[i].IsVoid()) continue;
            boxes[i].Get(x0, y0, z0, x1, y1, z1);
            centres_[i] = gp_Pnt((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2);
        }
    }

    section_->setBounds(model->bounds);
    box_->setBounds(model->bounds);
    measure_->setModel(parts, model->bounds, [this](const Handle(AIS_InteractiveObject)& part, QString& name, Bnd_Box& box) {
        auto it = nodeByObject_.constFind(part.get());
        if (it == nodeByObject_.constEnd() || !model_) return false;
        const viewer::ModelNode& node = model_->nodes[*it];
        gp_Trsf move;
        move.SetTranslation(offsets_[*it]);
        name = QString::fromStdU16String(node.name);
        box = node.bounds.Transformed(move);
        return !box.IsVoid();
    });

    stack_->setCurrentWidget(view_);
    view_->setOrientation(V3d_XposYnegZpos);
    fitVisible();
    // Fit again once the view has its real size (it may just have been shown).
    QTimer::singleShot(0, view_, [this] { fitVisible(); hud_->relayout(); });

    setWindowTitle(QStringLiteral("%1 - sideSTEP").arg(QFileInfo(path).fileName()));
    const double seconds = (model->parseMs + model->transferMs + model->meshMs + timer.elapsed()) / 1000.0;
    info_->setText(tr("%1 triangles  |  loaded in %2 s").arg(Count(model->triangles)).arg(seconds, 0, 'f', 1));
    setBusy(false);
    updateReadout();

    if (!dumpPath_.isEmpty()) QTimer::singleShot(400, this, &MainWindow::runTestSteps);
}

// ------------------------------------------------------------- selection ---

QVector<int> MainWindow::leavesUnder(QTreeWidgetItem* item) const {
    QVector<int> out;
    QVector<QTreeWidgetItem*> todo{item};
    while (!todo.isEmpty()) {
        QTreeWidgetItem* it = todo.takeLast();
        if (it->childCount() == 0) out.push_back(it->data(0, Qt::UserRole).toInt());
        for (int c = 0; c < it->childCount(); ++c) todo.push_back(it->child(c));
    }
    return out;
}

QVector<int> MainWindow::selectedNodes() const {
    QVector<int> nodes;
    const Handle(AIS_InteractiveContext)& ctx = view_->context();
    for (ctx->InitSelected(); ctx->MoreSelected(); ctx->NextSelected()) {
        auto it = nodeByObject_.constFind(ctx->SelectedInteractive().get());
        if (it != nodeByObject_.constEnd() && !nodes.contains(*it)) nodes.push_back(*it);
    }
    return nodes;
}

void MainWindow::selectNodes(const QVector<int>& nodes) {
    const Handle(AIS_InteractiveContext)& ctx = view_->context();
    ctx->ClearSelected(false);
    for (int i : nodes) {
        const Handle(AIS_ColoredShape)& ais = model_->nodes[i].ais;
        if (!ais.IsNull() && ctx->IsDisplayed(ais)) ctx->AddOrRemoveSelected(ais, false);
    }
    view_->redraw();
    onViewSelection();
}

void MainWindow::onViewSelection() {
    if (!model_) return;
    if (measure_->isActive()) { measure_->pick(); return; }
    syncing_ = true;
    tree_->clearSelection();
    QTreeWidgetItem* first = nullptr;
    for (int node : selectedNodes()) {
        items_[node]->setSelected(true);
        if (!first) first = items_[node];
    }
    if (first) tree_->scrollToItem(first);
    syncing_ = false;
    updateReadout();
}

void MainWindow::onTreeSelection() {
    if (syncing_ || measure_->isActive() || !model_) return;
    const Handle(AIS_InteractiveContext)& ctx = view_->context();
    QSet<int> leaves;
    for (QTreeWidgetItem* item : tree_->selectedItems())
        for (int i : leavesUnder(item)) leaves.insert(i);
    ctx->ClearSelected(false);
    for (int i : leaves) {
        const Handle(AIS_ColoredShape)& ais = model_->nodes[i].ais;
        if (!ais.IsNull() && ctx->IsDisplayed(ais)) ctx->AddOrRemoveSelected(ais, false);
    }
    view_->redraw();
    updateReadout();
}

// The eye at the end of a row hides or shows the part (or everything under an assembly).
void MainWindow::onTreeClicked(QTreeWidgetItem* item, int column) {
    if (column != 2 || !model_) return;
    const QVector<int> leaves = leavesUnder(item);
    bool anyVisible = false;
    for (int i : leaves) anyVisible |= visible_[i];
    for (int i : leaves) setNodeVisible(i, !anyVisible);
    refreshTreeLooks();
    view_->redraw();
}

// The strip under the model: the whole model's size, or the selected part's.
void MainWindow::updateReadout() {
    if (!model_) return;
    const QVector<int> nodes = selectedNodes();
    const bool inch = model_->inches;
    const QString unit = inch ? QStringLiteral("in") : QStringLiteral("mm");
    auto primary = [inch](double mm) { return inch ? Number(mm / 25.4, 1) : Number(mm, 0); };

    Bnd_Box box;
    if (nodes.isEmpty()) box = model_->bounds;
    for (int i : nodes) box.Add(model_->nodes[i].bounds);
    double d[3] = {0, 0, 0};
    if (!box.IsVoid()) {
        double x0, y0, z0, x1, y1, z1;
        box.Get(x0, y0, z0, x1, y1, z1);
        d[0] = x1 - x0; d[1] = y1 - y0; d[2] = z1 - z0;
    }
    const QString note = model_->unitsStated ? QString() : tr("   (the file does not state its units; shown as mm)");

    readoutShowOnly_->setVisible(!nodes.isEmpty());
    readoutHide_->setVisible(!nodes.isEmpty());
    if (nodes.isEmpty()) {
        readoutName_->setText(tr("Whole model"));
        readoutPath_->setText(model_->partCount == 1 ? tr("1 part") : tr("%1 parts").arg(model_->partCount));
        readoutSize_->setText(tr("%1 W \u00D7 %2 D \u00D7 %3 H %4").arg(primary(d[0]), primary(d[1]), primary(d[2]), unit) + note);
        return;
    }
    if (nodes.size() == 1) {
        const viewer::ModelNode& node = model_->nodes[nodes.first()];
        readoutName_->setText(QString::fromStdU16String(node.name));
        // Where it sits in the tree, below the top level.
        QStringList path;
        for (int p = node.parent; p >= 0 && model_->nodes[p].parent >= 0; p = model_->nodes[p].parent)
            path.prepend(QString::fromStdU16String(model_->nodes[p].name));
        readoutPath_->setText(path.isEmpty() ? QString() : tr("in %1").arg(path.join(QStringLiteral(" \u203A "))));
    } else {
        readoutName_->setText(tr("%1 parts selected").arg(nodes.size()));
        readoutPath_->clear();
    }
    // Longest side first, in the file's units and then the other system.
    std::sort(d, d + 3, std::greater<double>());
    const QString inches = QStringLiteral("%1 \u00D7 %2 \u00D7 %3 in").arg(Number(d[0] / 25.4, 1), Number(d[1] / 25.4, 1), Number(d[2] / 25.4, 1));
    const QString mm = QStringLiteral("%1 \u00D7 %2 \u00D7 %3 mm").arg(Number(d[0], 0), Number(d[1], 0), Number(d[2], 0));
    readoutSize_->setText((inch ? inches + QStringLiteral("  \u00B7  ") + mm : mm + QStringLiteral("  \u00B7  ") + inches) + note);
}

void MainWindow::onContextMenu(const QPoint& globalPos) {
    if (!model_ || measure_->isActive()) return;
    view_->selectAt(view_->mapFromGlobal(globalPos));
    const bool any = !selectedNodes().isEmpty();
    QMenu menu(this);
    QAction* hide = menu.addAction(tr("Hide"));
    QAction* only = menu.addAction(tr("Show only"));
    QAction* fitSel = menu.addAction(tr("Fit to this"));
    for (QAction* a : {hide, only, fitSel}) a->setEnabled(any);
    connect(hide, &QAction::triggered, this, &MainWindow::hideSelected);
    connect(only, &QAction::triggered, this, &MainWindow::showOnlySelected);
    connect(fitSel, &QAction::triggered, this, [this] { fitVisible(true); });
    menu.addSeparator();
    connect(menu.addAction(tr("Show all")), &QAction::triggered, this, &MainWindow::showAll);
    connect(menu.addAction(tr("Fit")), &QAction::triggered, this, [this] { fitVisible(); });
    menu.exec(globalPos);
}

// Shows the rows whose name contains the text, with the assemblies above them.
void MainWindow::filterTree(const QString& text) {
    const QString needle = text.trimmed();
    std::function<bool(QTreeWidgetItem*)> visit = [&](QTreeWidgetItem* item) {
        bool childMatch = false;
        for (int c = 0; c < item->childCount(); ++c) childMatch |= visit(item->child(c));
        const bool match = needle.isEmpty() || childMatch || item->text(0).contains(needle, Qt::CaseInsensitive);
        item->setHidden(!match);
        if (!needle.isEmpty() && childMatch) item->setExpanded(true);
        return match;
    };
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) visit(tree_->topLevelItem(i));
}

// ------------------------------------------------- visibility and display ---

void MainWindow::setNodeVisible(int node, bool visible) {
    const viewer::ModelNode& n = model_->nodes[node];
    if (n.ais.IsNull() || visible_[node] == visible) return;
    visible_[node] = visible;
    const Handle(AIS_InteractiveContext)& ctx = view_->context();
    if (visible) {
        ctx->Display(n.ais, displayMode_ == Wireframe ? AIS_WireFrame : AIS_Shaded, measure_->isActive() ? -1 : 0, false);
        measure_->partShown(n.ais);
    } else {
        ctx->Erase(n.ais, false);
    }
}

// Eye icons and dimmed names follow what is shown. An assembly's eye is open
// while anything under it is shown.
void MainWindow::refreshTreeLooks() {
    if (!model_) return;
    static const QIcon eye = MakeIcon(Icon::ShowAll, kPaper, kPaper), eyeOff = MakeIcon(Icon::Hide, kDim, kDim);
    const int n = (int)model_->nodes.size();
    QVector<bool> shown(n, false);
    for (int i = n - 1; i >= 0; --i) {
        const viewer::ModelNode& node = model_->nodes[i];
        if (!node.ais.IsNull()) shown[i] = visible_[i];
        if (node.parent >= 0 && shown[i]) shown[node.parent] = true;
    }
    for (int i = 0; i < n; ++i) {
        const bool faded = !shown[i] || (!solo_.isEmpty() && !model_->nodes[i].ais.IsNull() && !solo_.contains(i));
        items_[i]->setIcon(2, shown[i] ? eye : eyeOff);
        items_[i]->setForeground(0, faded ? kDim : kPaper);
    }
}

// A part's transparency: faded when "show only" picked other parts, glassy in
// the see-through style, solid otherwise.
void MainWindow::applyLook(int node) {
    const viewer::ModelNode& n = model_->nodes[node];
    if (n.ais.IsNull()) return;
    if (!solo_.isEmpty() && !solo_.contains(node)) n.ais->SetTransparency(0.9);
    else if (displayMode_ == SeeThrough) n.ais->SetTransparency(0.72);
    else n.ais->UnsetTransparency();
}

void MainWindow::hideSelected() {
    if (!model_) return;
    const QVector<int> nodes = selectedNodes();
    view_->context()->ClearSelected(false);
    for (int i : nodes) setNodeVisible(i, false);
    syncing_ = true;
    tree_->clearSelection();
    syncing_ = false;
    refreshTreeLooks();
    view_->redraw();
    updateReadout();
}

// Show only: the picked parts stay solid and everything else fades back.
void MainWindow::showOnlySelected() {
    if (!model_) return;
    const QVector<int> keep = selectedNodes();
    if (keep.isEmpty()) return;
    solo_ = QSet<int>(keep.begin(), keep.end());
    const Handle(AIS_InteractiveContext)& ctx = view_->context();
    for (int i = 0; i < (int)model_->nodes.size(); ++i) {
        if (model_->nodes[i].ais.IsNull()) continue;
        applyLook(i);
        if (ctx->IsDisplayed(model_->nodes[i].ais)) ctx->Redisplay(model_->nodes[i].ais, false);
    }
    refreshTreeLooks();
    hud_->toast(tr("Showing only the selected parts. Shift+H shows everything again."));
    fitVisible(true);
}

void MainWindow::showAll() {
    if (!model_) return;
    solo_.clear();
    const Handle(AIS_InteractiveContext)& ctx = view_->context();
    for (int i = 0; i < (int)model_->nodes.size(); ++i) {
        if (model_->nodes[i].ais.IsNull()) continue;
        applyLook(i);
        if (visible_[i]) ctx->Redisplay(model_->nodes[i].ais, false);
        else setNodeVisible(i, true);
    }
    refreshTreeLooks();
    view_->redraw();
}

void MainWindow::setDisplayMode(DisplayMode mode) {
    displayMode_ = mode;
    for (QAction* a : displayActions_->actions())
        if (a->data().toInt() == int(mode)) a->setChecked(true);
    const Handle(AIS_InteractiveContext)& ctx = view_->context();
    ctx->DefaultDrawer()->SetFaceBoundaryDraw(mode == ShadedWithEdges || mode == SeeThrough);
    if (model_) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        for (int i = 0; i < (int)model_->nodes.size(); ++i) {
            const viewer::ModelNode& n = model_->nodes[i];
            if (n.ais.IsNull()) continue;
            applyLook(i);
            if (!ctx->IsDisplayed(n.ais)) continue;
            ctx->SetDisplayMode(n.ais, mode == Wireframe ? AIS_WireFrame : AIS_Shaded, false);
            ctx->Redisplay(n.ais, false);
        }
        QApplication::restoreOverrideCursor();
    }
    view_->redraw();
}

void MainWindow::setLightBackground(bool on) {
    lightBackground_ = on;
    view_->setBackground(on ? QColor(0xF3, 0xF1, 0xEA) : QColor(0x17, 0x19, 0x1E));
    hud_->setLight(on);
    // The floating bar's icons: Ink on the light glass, Paper on the dark; Ink on the orange "on" state.
    for (QWidget* w : modelWidgets_)
        if (auto* b = qobject_cast<QToolButton*>(w))
            if (b->property("hudIcon").isValid()) b->setIcon(MakeIcon(Icon(b->property("hudIcon").toInt()), on ? kInk : kPaper, kInk));
}

// ------------------------------------------------------------ camera, tools ---

// Fits the parts that are shown (or just the selected ones), where the
// explode slider has put them.
void MainWindow::fitVisible(bool selectionOnly) {
    std::vector<gp_Pnt> points;
    Bnd_Box all;
    if (model_) {
        const Handle(AIS_InteractiveContext)& ctx = view_->context();
        const QVector<int> selected = selectionOnly ? selectedNodes() : QVector<int>();
        for (int i = 0; i < (int)model_->nodes.size(); ++i) {
            const viewer::ModelNode& n = model_->nodes[i];
            if (n.ais.IsNull() || n.bounds.IsVoid() || !ctx->IsDisplayed(n.ais)) continue;
            if (!selected.isEmpty() && !selected.contains(i)) continue;
            gp_Trsf move;
            move.SetTranslation(offsets_[i]);
            all.Add(n.bounds.Transformed(move));
            if (!n.outline) continue;
            const gp_Trsf placed = move * n.trsf;
            for (const gp_Pnt& p : *n.outline) points.push_back(p.Transformed(placed));
        }
    }
    view_->fitPoints(points, all);
}

void MainWindow::lookFrom(const gp_Dir& towardEye, const gp_Dir& up, bool thenFit) {
    view_->animateTo(towardEye, up, thenFit ? std::function<void()>([this] { fitVisible(); }) : std::function<void()>());
}

void MainWindow::standardView(int index) {
    if (!model_ || index < 0 || index > 6) return;
    const StandardView& v = kViews[index];
    lookFrom(gp_Dir(v.eye[0], v.eye[1], v.eye[2]), gp_Dir(v.up[0], v.up[1], v.up[2]), true);
}

// The slider only sets where the explosion should end up; a timer eases the
// parts there, so dragging stays smooth however many parts there are.
void MainWindow::setExplode(int value) {
    if (syncing_ || !model_) return;
    explodeTarget_ = value / 100.0 * 1.2;
    if (!explodeTimer_->isActive()) explodeTimer_->start(16);
}

void MainWindow::explodeTick() {
    if (!model_) { explodeTimer_->stop(); return; }
    const double gap = explodeTarget_ - explodeNow_;
    if (std::abs(gap) < 0.002) {
        explodeNow_ = explodeTarget_;
        explodeTimer_->stop();
        applyExplode(explodeNow_, true);
    } else {
        explodeNow_ += gap * 0.3;
        applyExplode(explodeNow_, false);
    }
}

// Each item moves away from the middle of the assembly it belongs to, and
// carries its own parts with it. So sub-assemblies separate as groups, and the
// parts inside each group spread around the group's own middle, instead of
// everything flying straight out from one point.
// While the slider is moving (`settle` false) only what is drawn is updated;
// picking is brought up to date once the parts come to rest.
void MainWindow::applyExplode(double amount, bool settle) {
    if (!model_) return;
    double x0, y0, z0, x1, y1, z1;
    model_->bounds.Get(x0, y0, z0, x1, y1, z1);
    const gp_Pnt modelCentre((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2);
    const Handle(AIS_InteractiveContext)& ctx = view_->context();
    std::vector<gp_Pnt> corners;
    for (int i = 0; i < (int)model_->nodes.size(); ++i) {  // parents come before their children
        const viewer::ModelNode& n = model_->nodes[i];
        gp_Vec away(n.parent >= 0 ? centres_[n.parent] : modelCentre, centres_[i]);
        if (explodeAxis_ >= 0) {
            const double along = away.Coord(explodeAxis_ + 1);
            away = gp_Vec(0, 0, 0);
            away.SetCoord(explodeAxis_ + 1, along);
        }
        offsets_[i] = (n.parent >= 0 ? offsets_[n.parent] : gp_Vec()) + away * amount;
        if (n.ais.IsNull()) continue;
        gp_Trsf move;
        move.SetTranslation(offsets_[i]);
        if (settle) ctx->SetLocation(n.ais, TopLoc_Location(move * n.trsf));
        else n.ais->SetLocalTransformation(move * n.trsf);
        if (n.bounds.IsVoid() || !visible_[i]) continue;
        n.bounds.Get(x0, y0, z0, x1, y1, z1);
        for (int c = 0; c < 8; ++c)
            corners.push_back(gp_Pnt(c & 1 ? x1 : x0, c & 2 ? y1 : y0, c & 4 ? z1 : z0).Translated(offsets_[i]));
    }
    // The camera follows with the same fit on every frame and at rest, so the
    // view does not jump when the parts stop.
    view_->fitPointsQuick(corners);
}

// One tool at a time. Turning a tool off clears what it did to the model.
void MainWindow::setTool(Tool tool) {
    if (tool != NoTool && !model_) tool = NoTool;
    if (tool == tool_) tool = NoTool;  // pressing the active tool again leaves it

    switch (tool_) {
    case MeasureTool_:
        measure_->setActive(false);
        measure_->clear();
        measureDock_->hide();
        break;
    case SectionTool_: section_->setActive(false); break;
    case SectionBoxTool_: box_->setActive(false); break;
    case ExplodeTool_: explode_->setValue(0); break;
    default: break;
    }
    tool_ = tool;
    switch (tool_) {
    case MeasureTool_:
        syncing_ = true;
        tree_->clearSelection();
        syncing_ = false;
        measureDock_->show();
        measure_->setActive(true);
        hud_->setPill(tr("Measure"), measureHint_);
        break;
    case SectionTool_:
        section_->setActive(true);
        hud_->setPill(tr("Section"), section_);
        break;
    case SectionBoxTool_:
        box_->setActive(true);
        hud_->setPill(tr("Section box"), box_);
        break;
    case ExplodeTool_:
        hud_->setPill(tr("Explode"), explodeStrip_);
        explode_->setValue(35);
        break;
    default:
        hud_->setPill(QString(), nullptr);
        break;
    }
    for (auto it = toolActions_.begin(); it != toolActions_.end(); ++it) it.value()->setChecked(it.key() == tool_);
    for (auto it = toolButtons_.begin(); it != toolButtons_.end(); ++it) it.value()->setChecked(it.key() == tool_);
    QTimer::singleShot(0, hud_, &Hud::relayout);
    if (model_) updateReadout();
}

// Esc backs out one step at a time: a half-done measurement, then the tool,
// then the selection, then full screen. It never skips a step, and it leaves
// the display style alone.
void MainWindow::escape() {
    if (tool_ == MeasureTool_ && measure_->cancelPending()) return;
    if (tool_ != NoTool) { setTool(NoTool); return; }
    if (model_ && !selectedNodes().isEmpty()) {
        view_->context()->ClearSelected(false);
        view_->redraw();
        onViewSelection();
        return;
    }
    if (isFullScreen()) toggleFullScreen();
}

// Every part by name with how many there are; clicking a row selects them all.
void MainWindow::showParts() {
    if (!model_) return;
    if (partsDialog_) { partsDialog_->raise(); partsDialog_->activateWindow(); return; }
    QMap<QString, QVector<int>> byName;
    for (int i = 0; i < (int)model_->nodes.size(); ++i)
        if (!model_->nodes[i].ais.IsNull()) byName[QString::fromStdU16String(model_->nodes[i].name)].push_back(i);

    auto* dialog = new QDialog(this);
    partsDialog_ = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("Parts"));
    dialog->resize(520, 560);
    auto* layout = new QVBoxLayout(dialog);
    auto* summary = new QLabel(tr("%1 parts, %2 different").arg(model_->partCount).arg(byName.size()), dialog);
    auto* list = new QTreeWidget(dialog);
    list->setColumnCount(2);
    list->setHeaderLabels({tr("Part"), tr("Count")});
    list->setRootIsDecorated(false);
    list->setUniformRowHeights(true);
    list->header()->setStretchLastSection(false);
    list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    list->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    QStringList lines;
    QList<QPair<int, QString>> order;
    for (auto it = byName.begin(); it != byName.end(); ++it) order.push_back({(int)it.value().size(), it.key()});
    std::sort(order.begin(), order.end(), [](const QPair<int, QString>& a, const QPair<int, QString>& b) {
        return a.first != b.first ? a.first > b.first : a.second.compare(b.second, Qt::CaseInsensitive) < 0;
    });
    for (const auto& entry : order) {
        auto* row = new QTreeWidgetItem(list, {entry.second, QString::number(entry.first)});
        row->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        lines << QStringLiteral("%1\t%2").arg(entry.first).arg(entry.second);
    }
    auto* copy = new QPushButton(tr("Copy as a list"), dialog);
    copy->setToolTip(tr("Count and name on each line, ready to paste into a spreadsheet"));
    layout->addWidget(summary);
    layout->addWidget(list, 1);
    layout->addWidget(copy, 0, Qt::AlignRight);
    connect(copy, &QPushButton::clicked, this, [this, lines] {
        QApplication::clipboard()->setText(lines.join(QChar(u'\n')));
        hud_->toast(tr("Parts list copied."));
    });
    connect(list, &QTreeWidget::itemClicked, this, [this, byName](QTreeWidgetItem* row) {
        if (model_ && !measure_->isActive()) selectNodes(byName.value(row->text(0)));
    });
    connect(dialog, &QObject::destroyed, this, [this] { partsDialog_ = nullptr; });
    dialog->show();
}

// Saves a picture of the view to Pictures\sideSTEP and puts it on the clipboard.
void MainWindow::snapshot() {
    if (!model_) return;
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + QStringLiteral("/sideSTEP");
    QDir().mkpath(folder);
    const QString file = QStringLiteral("%1/%2 %3.png").arg(folder, QFileInfo(path_).completeBaseName(),
                                                           QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HHmmss")));
    if (!view_->dump(file)) {
        hud_->toast(tr("The snapshot could not be saved. Check that the Pictures folder can be written to."));
        return;
    }
    QApplication::clipboard()->setImage(QImage(file));
    hud_->toast(tr("Snapshot copied, and saved in Pictures \u203A sideSTEP."));
}

void MainWindow::toggleFullScreen() {
    const bool enter = !isFullScreen();
    if (enter) {
        wasMaximized_ = isMaximized();
        showFullScreen();
    } else if (wasMaximized_) {
        showMaximized();
    } else {
        showNormal();
    }
    menuBar()->setVisible(!enter);
    statusBar()->setVisible(!enter);
    for (QToolBar* bar : findChildren<QToolBar*>()) bar->setVisible(!enter);
    fullScreenButton_->setChecked(enter);
    if (enter) hud_->toast(tr("Esc leaves full screen."));
    QTimer::singleShot(50, hud_, &Hud::relayout);
}

void MainWindow::setMouseStyle(int preset, bool announce) {
    if (preset < 0 || preset >= kMouseStyleCount) preset = 0;
    view_->setMousePreset(preset);
    mouseMenu_->actions().at(preset)->setChecked(true);
    mouseButton_->setMenu(mouseMenu_);
    mouseButton_->setText(tr("Mouse: %1").arg(tr(kMouseStyles[preset].name)));
    if (!announce) return;
    // Choosing a program also sets the wheel direction that program is known for; the box under the list overrides it.
    reverseZoom_->setChecked(kMouseStyles[preset].reverseZoom);
    view_->setReverseZoom(kMouseStyles[preset].reverseZoom);
    QSettings settings;
    settings.setValue(QStringLiteral("view/mouse"), preset);
    settings.setValue(QStringLiteral("view/reverseZoom"), kMouseStyles[preset].reverseZoom);
    hud_->toast(QStringLiteral("%1: %2.").arg(tr(kMouseStyles[preset].name), tr(kMouseStyles[preset].summary)));
}

// ------------------------------------------------------ open in another app ---

void MainWindow::setExternalApp(const QString& id, const QString& name) {
    QSettings s;
    s.setValue(QStringLiteral("externalApp/id"), id);
    s.setValue(QStringLiteral("externalApp/name"), name);
    openExternal_->setText(id.isEmpty() ? tr("Open in another app...") : tr("Open in %1").arg(name));
    openExternal_->setIconText(id.isEmpty() ? tr("Open in...") : tr("Open in %1").arg(name));
    openExternal_->setToolTip(id.isEmpty() ? tr("Choose the CAD app that sideSTEP hands files to")
                                           : tr("Open this file in %1 (Ctrl+E). Change the app under File > Open with.").arg(name));
}

void MainWindow::rebuildOpenWithMenu() {
    openWithMenu_->clear();
    const QString current = QSettings().value(QStringLiteral("externalApp/id")).toString();
    for (const ExternalApp& app : ListExternalApps(path_.isEmpty() ? QStringLiteral("x.step") : path_)) {
        QAction* a = openWithMenu_->addAction(app.icon, app.name);
        a->setCheckable(true);
        a->setChecked(app.id.compare(current, Qt::CaseInsensitive) == 0);
        connect(a, &QAction::triggered, this, [this, app] {
            setExternalApp(app.id, app.name);
            openInExternalApp();
        });
    }
    if (!openWithMenu_->isEmpty()) openWithMenu_->addSeparator();
    connect(openWithMenu_->addAction(tr("Choose another app...")), &QAction::triggered, this, &MainWindow::chooseExternalApp);
}

void MainWindow::chooseExternalApp() {
    const QString start = qEnvironmentVariable("ProgramFiles", QStringLiteral("C:/Program Files"));
    const QString exe = QFileDialog::getOpenFileName(this, tr("Choose the app to open STEP files in"), start,
                                                     tr("Programs (*.exe)"));
    if (exe.isEmpty()) return;
    setExternalApp(QDir::toNativeSeparators(exe), ExternalAppNameFromExe(exe));
    openInExternalApp();
}

void MainWindow::openInExternalApp() {
    const QString id = QSettings().value(QStringLiteral("externalApp/id")).toString();
    if (id.isEmpty()) {
        // Nothing chosen yet: offer the list.
        rebuildOpenWithMenu();
        openWithMenu_->exec(QCursor::pos());
        return;
    }
    if (path_.isEmpty()) return;
    if (!OpenInExternalApp(id, path_))
        QMessageBox::warning(this, tr("sideSTEP"),
                             tr("%1 could not be started. Pick the app again under File > Open with.")
                                 .arg(QSettings().value(QStringLiteral("externalApp/name")).toString()));
}

void MainWindow::showAbout() {
    QMessageBox box(this);
    box.setWindowTitle(tr("About sideSTEP"));
    box.setIconPixmap(QPixmap(QStringLiteral(":/brand/icon/png/sidestep-256.png")).scaledToWidth(96, Qt::SmoothTransformation));
    box.setTextFormat(Qt::RichText);
    box.setText(tr("<b>sideSTEP %1</b><br>STEP files, seen without the CAD package.<br><br>"
                   "Created by Adrian Samarra. Open source, for Windows, released on GitHub.<br>"
                   "<a href=\"https://github.com/samarrastudios/sideSTEP\" style=\"color:#FF5B1F\">github.com/samarrastudios/sideSTEP</a><br><br>"
                   "<small>MIT License. Built with OpenCASCADE Technology (LGPL 2.1 with exception) and Qt (LGPL 3).</small>")
                    .arg(QStringLiteral(APP_VERSION)));
    box.exec();
}

// ------------------------------------------------------------ window events ---

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (!e->mimeData()->hasUrls()) return;
    for (const QUrl& u : e->mimeData()->urls())
        if (u.isLocalFile() && IsStepFile(u.toLocalFile())) { e->acceptProposedAction(); return; }
}

void MainWindow::dropEvent(QDropEvent* e) {
    for (const QUrl& u : e->mimeData()->urls())
        if (u.isLocalFile() && IsStepFile(u.toLocalFile())) { openFile(u.toLocalFile()); return; }
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (loading_) loading_->cancel = true;
    e->accept();
}

// -------------------------------------------------------------- testing aids ---

void MainWindow::runTestSteps() {
    int delay = 0;
    if (testDisplay_ >= 0) setDisplayMode(DisplayMode(testDisplay_));
    if (testPerspective_) { view_->setPerspective(true); fitVisible(); }
    if (testExplode_ > 0) {
        setTool(ExplodeTool_);
        // How long one frame of a moving explosion takes (what the slider feels like).
        QElapsedTimer frames;
        frames.start();
        for (int i = 0; i < 20; ++i) { applyExplode(0.02 * i, false); view_->flush(); }
        const double moving = frames.elapsed() / 20.0;
        // For comparison: just redrawing the model, with and without shadows.
        frames.restart();
        for (int i = 0; i < 20; ++i) { view_->redraw(); view_->flush(); }
        const double drawing = frames.elapsed() / 20.0;
        view_->setShadows(false);
        frames.restart();
        for (int i = 0; i < 20; ++i) { view_->redraw(); view_->flush(); }
        const double plain = frames.elapsed() / 20.0;
        view_->setShadows(true);
        status_->setText(QStringLiteral("explode frame: %1 ms (redraw alone %2 ms, without shadows %3 ms)")
                             .arg(moving, 0, 'f', 1).arg(drawing, 0, 'f', 1).arg(plain, 0, 'f', 1));
        explode_->setValue(testExplode_);
        delay += 900;  // let the parts come to rest
    }
    if (testSection_ == QLatin1String("box")) {
        setTool(SectionBoxTool_);
    } else if (!testSection_.isEmpty()) {
        const QStringList spec = testSection_.split(',');
        setTool(SectionTool_);
        section_->setAxis(spec.value(0).toLower() == "y" ? 1 : spec.value(0).toLower() == "z" ? 2 : 0);
        if (spec.size() > 1) section_->setPosition(spec[1].toDouble());
    }
    if (testMeasureMode_ >= 0 || !testClicks_.isEmpty()) {
        const bool measuring = testMeasureMode_ >= 0;
        if (measuring) {
            setTool(MeasureTool_);
            measure_->setMode(MeasureTool::Mode(testMeasureMode_));
            if (testMeasureMode_ == MeasureTool::Size && testClicks_.isEmpty())
                if (auto* whole = measure_->findChild<QPushButton*>()) whole->click();
        }
        for (const QString& click : testClicks_.split(';', Qt::SkipEmptyParts)) {
            const QStringList xy = click.split(',');
            if (xy.size() != 2) continue;
            // Move, press, release; the view handles input when it redraws,
            // and an off-screen test window gets no paint events of its own.
            const QEvent::Type types[] = {QEvent::MouseMove, QEvent::MouseButtonPress, QEvent::MouseButtonRelease};
            for (QEvent::Type type : types) {
                delay += 200;
                QTimer::singleShot(delay, this, [this, xy, type] {
                    const QPointF p(xy[0].toDouble() * view_->width(), xy[1].toDouble() * view_->height());
                    const bool press = type == QEvent::MouseButtonPress;
                    QMouseEvent e(type, p, view_->mapToGlobal(p), type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                                  press ? Qt::LeftButton : Qt::NoButton, Qt::NoModifier);
                    QCoreApplication::sendEvent(view_, &e);
                    view_->flush();
                });
            }
        }
    }
    if (!testView_.isEmpty()) {
        delay += 100;
        QTimer::singleShot(delay, this, [this] {
            for (int i = 0; i < 7; ++i)
                if (testView_ == QLatin1String(kViews[i].name)) standardView(i);
        });
        delay += 500;  // let the camera finish turning
    }
    QTimer::singleShot(delay + 600, this, &MainWindow::finishDump);
}

void MainWindow::finishDump() {
    view_->flush();
    measure_->placeLabels();
    hud_->relayout();
    view_->dump(dumpPath_);
    // The whole window: Qt's own widgets, with the 3D image laid in where the
    // view sits (it paints outside Qt, so grab() misses it), then the floating
    // controls, which are a window of their own.
    QPixmap shot = grab();
    QPainter painter(&shot);
    const QPoint origin = view_->mapTo(this, QPoint(0, 0));
    painter.drawImage(QRect(origin, view_->size()), QImage(dumpPath_));
    for (QLabel* label : measure_->labels())
        if (label->isVisible()) painter.drawPixmap(label->mapTo(this, QPoint(0, 0)), label->grab());
    painter.drawPixmap(origin, hud_->grab());
    painter.end();
    shot.save(dumpPath_ + QStringLiteral(".window.png"));

    // The measurements, the readout, and the programs "Open with" would offer.
    QFile notes(dumpPath_ + QStringLiteral(".txt"));
    if (notes.open(QIODevice::WriteOnly)) {
        notes.write(measure_->text().toUtf8());
        notes.write(QStringLiteral("\n--- readout ---\n%1 | %2 | %3\n%4\n").arg(readoutName_->text(), readoutPath_->text(), readoutSize_->text(), status_->text()).toUtf8());
        notes.write("--- open with ---\n");
        for (const ExternalApp& app : ListExternalApps(path_))
            notes.write((app.name + QStringLiteral(" | ") + app.id + QChar(u'\n')).toUtf8());
    }
    qApp->quit();
}
