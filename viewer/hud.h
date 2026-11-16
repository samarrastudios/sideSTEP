// The controls that float over the 3D view: the tool bar at the bottom, the
// active tool's settings in a pill at the top, and the view cube.
//
// The 3D view is a native OpenGL window, which Qt cannot draw translucent
// widgets on top of. So the HUD is its own frameless, see-through window kept
// exactly over the view; its mask is the union of its controls, so clicks
// anywhere else go straight through to the model.
#pragma once
#include <QWidget>

class ViewCube;
class QFrame;
class QHBoxLayout;
class QLabel;
class QToolButton;

class Hud : public QWidget {
    Q_OBJECT
public:
    Hud(QWidget* window, QWidget* target);

    // Adds a button to the bar; `gap` starts a new group.
    QToolButton* addButton(const QIcon& icon, const QString& label, const QString& tip, bool checkable, bool gap = false);
    // Shows a tool's settings in the pill (nullptr hides it). The widget is not owned by the pill.
    void setPill(const QString& title, QWidget* content);
    ViewCube* cube() const { return cube_; }
    void setLight(bool light);
    // A short message that fades out by itself.
    void toast(const QString& text);
    void relayout();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QWidget* window_;
    QWidget* target_;
    QFrame* bar_ = nullptr;
    QHBoxLayout* barLayout_ = nullptr;
    QFrame* pill_ = nullptr;
    QHBoxLayout* pillLayout_ = nullptr;
    QLabel* pillTitle_ = nullptr;
    QLabel* pillHint_ = nullptr;
    QWidget* pillContent_ = nullptr;
    QLabel* toast_ = nullptr;
    ViewCube* cube_ = nullptr;
    bool light_ = true;
};
