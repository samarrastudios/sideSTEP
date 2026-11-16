// sideSTEP viewer: opens a STEP file in a 3D view with its model tree.
//   sideSTEP.exe [file.step]
// Testing aid, runs off-screen and quits:
//   sideSTEP.exe file.step --dump out.png [--explode 0-100] [--display 0-3] [--section x,0.5 | box]
//                [--measure-mode 0-4] [--clicks "x,y;x,y"] [--view front|back|left|right|top|bottom|iso]
//   sideSTEP.exe --register | --unregister   (file type registration, no window)
//   sideSTEP.exe --helper-on | --helper-off  (the user's switch for the background helper, see unblock.h)
//   sideSTEP.exe --watch                     (the helper itself: keeps STEP files previewable in Explorer)
#include "file_assoc.h"
#include "main_window.h"
#include "unblock.h"

#include <QApplication>
#include <QIcon>
#include <QPalette>
#include <QStyleFactory>

#include <cstring>

namespace {

// Brand palette (assets/brand/BRAND.md): Graphite, Paper, Signal orange, Ink.
void ApplyTheme(QApplication& app) {
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    const QColor graphite(0x17, 0x19, 0x1E), panel(0x1E, 0x21, 0x27), raised(0x2A, 0x2E, 0x36);
    const QColor paper(0xF3, 0xF1, 0xEA), orange(0xFF, 0x5B, 0x1F), ink(0x11, 0x13, 0x16), dim(0x8A, 0x8C, 0x90);
    QPalette p;
    p.setColor(QPalette::Window, graphite);
    p.setColor(QPalette::WindowText, paper);
    p.setColor(QPalette::Base, panel);
    p.setColor(QPalette::AlternateBase, graphite);
    p.setColor(QPalette::Text, paper);
    p.setColor(QPalette::Button, raised);
    p.setColor(QPalette::ButtonText, paper);
    p.setColor(QPalette::ToolTipBase, raised);
    p.setColor(QPalette::ToolTipText, paper);
    p.setColor(QPalette::PlaceholderText, dim);
    p.setColor(QPalette::Highlight, orange);
    p.setColor(QPalette::HighlightedText, ink);
    p.setColor(QPalette::Link, orange);
    p.setColor(QPalette::Light, raised);
    p.setColor(QPalette::Mid, raised);
    p.setColor(QPalette::Dark, ink);
    for (QPalette::ColorRole r : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, r, dim);
    app.setPalette(p);
    app.setStyleSheet(QStringLiteral(
        "QToolBar { border: 0; padding: 4px 6px; spacing: 1px; }"
        "QToolBar::separator { background: #2A2E36; width: 1px; margin: 6px 6px; }"
        "QToolButton { padding: 5px 9px; border: 0; border-radius: 4px; }"
        "QToolBar QToolButton { padding: 4px 7px; font-size: 11px; min-width: 44px; }"
        "QToolButton:hover { background: #2A2E36; }"
        "QToolButton:checked { background: #FF5B1F; color: #111316; }"
        "QToolButton::menu-button { border: 0; width: 14px; }"
        "QLineEdit { border: 0; border-bottom: 1px solid #2A2E36; padding: 6px 8px; background: #1E2127; }"
        "QListWidget { border: 0; }"
        "QDockWidget::title { padding: 6px 8px; }"
        "#readout { background: #1E2127; border-top: 1px solid #2A2E36; }"
        "#mainBar QToolButton { font-size: 12px; min-width: 0; padding: 4px 9px; }"
        "QTreeWidget::item { padding: 2px 0; }"
        "QTreeWidget { border: 0; }"
        "QStatusBar::item { border: 0; }"
        "QPushButton { padding: 7px 18px; }"));
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--register")) return RegisterFileAssociation() ? 0 : 1;
        if (!strcmp(argv[i], "--unregister")) { SetWatcherEnabled(false); return UnregisterFileAssociation() ? 0 : 1; }
        if (!strcmp(argv[i], "--helper-on")) return SetWatcherEnabled(true) ? 0 : 1;
        if (!strcmp(argv[i], "--helper-off")) return SetWatcherEnabled(false) ? 0 : 1;
        if (!strcmp(argv[i], "--watch")) return RunUnblockWatcher();
    }
    QCoreApplication::setOrganizationName(QStringLiteral("sideSTEP"));
    QCoreApplication::setApplicationName(QStringLiteral("sideSTEP"));
    QCoreApplication::setApplicationVersion(QStringLiteral(APP_VERSION));
    QApplication app(argc, argv);
    ApplyTheme(app);
    QIcon icon;
    for (int size : {16, 24, 32, 48, 64, 256})
        icon.addFile(QStringLiteral(":/brand/icon/png/sidestep-%1.png").arg(size), QSize(size, size));
    app.setWindowIcon(icon);

    MainWindow window;
    QString file;
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args[i] == QLatin1String("--dump") && i + 1 < args.size()) window.setDumpPath(args[++i]);
        else if (args[i] == QLatin1String("--explode") && i + 1 < args.size()) window.setTestExplode(args[++i].toInt());
        else if (args[i] == QLatin1String("--clicks") && i + 1 < args.size()) window.setTestClicks(args[++i]);
        else if (args[i] == QLatin1String("--measure-mode") && i + 1 < args.size()) window.setTestMeasureMode(args[++i].toInt());
        else if (args[i] == QLatin1String("--section") && i + 1 < args.size()) window.setTestSection(args[++i]);
        else if (args[i] == QLatin1String("--display") && i + 1 < args.size()) window.setTestDisplay(args[++i].toInt());
        else if (args[i] == QLatin1String("--view") && i + 1 < args.size()) window.setTestView(args[++i]);
        else if (args[i] == QLatin1String("--perspective")) window.setTestPerspective(true);
        else if (file.isEmpty()) file = args[i];
    }
    if (window.isDumping()) {
        // Test runs stay out of the way: off-screen, and without taking focus.
        window.setAttribute(Qt::WA_ShowWithoutActivating);
        window.move(-30000, -30000);
    }
    window.show();
    if (!file.isEmpty()) window.openFile(file);
    return app.exec();
}
