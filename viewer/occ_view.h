// A Qt widget that hosts an OpenCASCADE 3D view in its own native window.
#pragma once
#include <AIS_InteractiveContext.hxx>
#include <AIS_ViewController.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>

#include <Bnd_Box.hxx>

#include <QWidget>

#include <functional>
#include <vector>

class QVariantAnimation;

class OccView : public QWidget, protected AIS_ViewController {
    Q_OBJECT
public:
    explicit OccView(QWidget* parent);

    const Handle(AIS_InteractiveContext)& context() const { return ctx_; }
    const Handle(V3d_View)& view() const { return view_; }

    void redraw();
    // Handles queued input and redraws now (normally done by the paint event).
    void flush();
    void fitAll();
    // Fits the view around points on the model's surface, as seen from the
    // current direction. `all` is the overall box, used for a first coarse fit.
    void fitPoints(const std::vector<gp_Pnt>& points, const Bnd_Box& all);
    // The same fit done with plain arithmetic on the camera, for use every
    // frame while parts are moving (the library's own fit re-measures every
    // object and takes tens of milliseconds on a big assembly).
    void fitPointsQuick(const std::vector<gp_Pnt>& points);
    void setOrientation(V3d_TypeOfOrientation o);
    void setPerspective(bool on);
    void setBackground(const QColor& c);
    bool dump(const QString& path);
    // Widget coordinates (logical pixels) of a 3D point.
    QPoint project(const gp_Pnt& p) const;

    // Where the camera looks from (a direction from the model toward the eye) and its up.
    void cameraFrame(gp_Dir& towardEye, gp_Dir& up) const;
    // Turns the camera smoothly to look from a direction; `done` runs when it gets there.
    void animateTo(const gp_Dir& towardEye, const gp_Dir& up, std::function<void()> done = {});
    // Mouse mappings familiar from CAD programs:
    // 0 sideSTEP, 1 SolidWorks, 2 Fusion / Inventor, 3 Onshape, 4 Creo / NX, 5 Rhino.
    void setMousePreset(int preset);
    // Which way the wheel zooms. Programs disagree, so it is the user's switch.
    void setReverseZoom(bool reverse) { reverseZoom_ = reverse; }
    // Soft shadows cast by the main light.
    void setShadows(bool on);
    // The pointer while no button is down: an open hand normally, a crosshair for measuring.
    void setIdleCursor(Qt::CursorShape shape);

    // Selects the part under a widget position unless it is already selected.
    // Returns false when there is nothing there.
    bool selectAt(const QPoint& pos);

signals:
    void selectionChanged();
    void viewRedrawn();
    // A right click that was not a drag (right drag pans).
    void contextRequested(const QPoint& globalPos);

protected:
    QPaintEngine* paintEngine() const override { return nullptr; }
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void showEvent(QShowEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void leaveEvent(QEvent*) override;

    void OnSelectionChanged(const Handle(AIS_InteractiveContext)&, const Handle(V3d_View)&) override;
    void handleViewRedraw(const Handle(AIS_InteractiveContext)&, const Handle(V3d_View)&) override;

private:
    void mouseButtons(QMouseEvent* e);
    void syncSize();

    QPoint rightPress_;
    bool reverseZoom_ = false;
    Qt::CursorShape idleCursor_ = Qt::OpenHandCursor;
    QVariantAnimation* turn_ = nullptr;
    Handle(V3d_Viewer) viewer_;
    Handle(V3d_View) view_;
    Handle(AIS_InteractiveContext) ctx_;
};
