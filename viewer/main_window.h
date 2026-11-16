#pragma once
#include "model.h"

#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <QHash>
#include <QMainWindow>
#include <QSet>
#include <QVector>

#include <atomic>
#include <memory>

class Hud;
class MeasureTool;
class OccView;
class SectionBoxTool;
class SectionTool;
class QAction;
class QActionGroup;
class QDialog;
class QDockWidget;
class QLabel;
class QLineEdit;
class QMenu;
class QProgressBar;
class QPushButton;
class QSlider;
class QStackedWidget;
class QTimer;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    enum DisplayMode { ShadedWithEdges, Shaded, Wireframe, SeeThrough };
    // One tool at a time; picking another clears the first one's effect.
    enum Tool { NoTool, MeasureTool_, SectionTool_, SectionBoxTool_, ExplodeTool_ };

    MainWindow();
    ~MainWindow() override;
    void openFile(const QString& path);

    // Testing aids (see main.cpp): save the view to a PNG once the file has
    // loaded, after optionally driving the tools, then quit.
    void setDumpPath(const QString& path) { dumpPath_ = path; }
    bool isDumping() const { return !dumpPath_.isEmpty(); }
    void setTestExplode(int value) { testExplode_ = value; }
    void setTestClicks(const QString& clicks) { testClicks_ = clicks; }       // "0.4,0.5;0.6,0.5", fractions of the view
    void setTestMeasureMode(int mode) { testMeasureMode_ = mode; }
    void setTestSection(const QString& spec) { testSection_ = spec; }         // "x,0.45", or "box" for the section box
    void setTestDisplay(int mode) { testDisplay_ = mode; }
    void setTestView(const QString& name) { testView_ = name; }
    void setTestPerspective(bool on) { testPerspective_ = on; }               // front, top, ...

protected:
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;
    void closeEvent(QCloseEvent*) override;

private:
    struct Load {
        QString path;
        std::atomic<bool> cancel{false};
        std::shared_ptr<viewer::Model> model;
        std::string error;
        bool ok = false;
    };

    void buildChrome();
    void buildActions();
    void chooseFile();
    void openSibling(int step);
    void loadFinished(const std::shared_ptr<Load>& load);
    void showModel(const std::shared_ptr<viewer::Model>& model, const QString& path);
    void clearModel();
    void setBusy(bool busy, const QString& text = {});

    void onViewSelection();
    void onTreeSelection();
    void onTreeClicked(QTreeWidgetItem* item, int column);
    void onContextMenu(const QPoint& globalPos);
    void filterTree(const QString& text);
    void updateReadout();
    QVector<int> selectedNodes() const;
    QVector<int> leavesUnder(QTreeWidgetItem* item) const;
    void selectNodes(const QVector<int>& nodes);

    void setNodeVisible(int node, bool visible);
    void refreshTreeLooks();
    void hideSelected();
    void showOnlySelected();
    void showAll();
    void applyLook(int node);
    void setDisplayMode(DisplayMode mode);
    void setLightBackground(bool on);

    void setExplode(int value);
    void explodeTick();
    void applyExplode(double amount, bool settle);
    void setMouseStyle(int preset, bool announce);
    void fitVisible(bool selectionOnly = false);
    void lookFrom(const gp_Dir& towardEye, const gp_Dir& up, bool thenFit);
    void standardView(int index);  // 0 front .. 5 bottom, 6 isometric
    void setTool(Tool tool);
    void escape();
    void showParts();
    void snapshot();
    void toggleFullScreen();

    void rebuildOpenWithMenu();
    void openInExternalApp();
    void chooseExternalApp();
    void setExternalApp(const QString& id, const QString& name);
    void showAbout();
    void runTestSteps();
    void finishDump();

    OccView* view_ = nullptr;
    Hud* hud_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QLabel* welcomeText_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    QLineEdit* filter_ = nullptr;
    QWidget* readout_ = nullptr;
    QLabel* readoutName_ = nullptr;
    QLabel* readoutPath_ = nullptr;
    QLabel* readoutSize_ = nullptr;
    QPushButton* readoutShowOnly_ = nullptr;
    QPushButton* readoutHide_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* info_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QToolButton* cancelButton_ = nullptr;
    QDockWidget* measureDock_ = nullptr;
    MeasureTool* measure_ = nullptr;
    SectionTool* section_ = nullptr;
    SectionBoxTool* box_ = nullptr;
    QWidget* explodeStrip_ = nullptr;
    QSlider* explode_ = nullptr;
    QLabel* measureHint_ = nullptr;
    QDialog* partsDialog_ = nullptr;
    QMenu* openWithMenu_ = nullptr;
    QMenu* displayMenu_ = nullptr;
    QAction* openExternal_ = nullptr;
    QAction* fullScreenAction_ = nullptr;
    QActionGroup* displayActions_ = nullptr;
    QHash<int, QAction*> toolActions_;
    QHash<int, QToolButton*> toolButtons_;
    QToolButton* fullScreenButton_ = nullptr;
    QVector<QAction*> modelActions_;
    QVector<QWidget*> modelWidgets_;

    std::shared_ptr<viewer::Model> model_;
    std::shared_ptr<Load> loading_;
    QString path_;
    QVector<QTreeWidgetItem*> items_;                       // by node index
    QVector<gp_Vec> offsets_;                                // explode offset, by node index
    QVector<gp_Pnt> centres_;                                // middle of each node (assemblies included)
    double explodeNow_ = 0, explodeTarget_ = 0;              // 0 = assembled
    int explodeAxis_ = -1;                                   // -1 every direction, else X, Y or Z only
    QTimer* explodeTimer_ = nullptr;
    QToolButton* mouseButton_ = nullptr;
    QMenu* mouseMenu_ = nullptr;
    QAction* reverseZoom_ = nullptr;
    QVector<bool> visible_;                                  // by node index (parts only)
    QSet<int> solo_;                                         // "show only": the rest is faded
    QHash<const Standard_Transient*, int> nodeByObject_;    // displayed object -> node index
    bool syncing_ = false;
    DisplayMode displayMode_ = ShadedWithEdges;
    Tool tool_ = NoTool;
    bool lightBackground_ = true;
    bool wasMaximized_ = false;

    QString dumpPath_;
    int testExplode_ = 0;
    QString testClicks_;
    int testMeasureMode_ = -1;
    QString testSection_;
    int testDisplay_ = -1;
    QString testView_;
    bool testPerspective_ = false;
};
