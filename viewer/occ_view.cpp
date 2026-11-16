#include "occ_view.h"

#include <Aspect_DisplayConnection.hxx>
#include <Graphic3d_Camera.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Prs3d_Drawer.hxx>
#include <V3d_AmbientLight.hxx>
#include <V3d_DirectionalLight.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Quantity_Color.hxx>
#include <WNT_Window.hxx>

#include <SelectMgr_ViewerSelector.hxx>
#include <gp_Mat.hxx>
#include <gp_Quaternion.hxx>
#include <gp_QuaternionSLerp.hxx>

#include <QMouseEvent>
#include <QVariantAnimation>

#include <algorithm>
#include <QWheelEvent>

namespace {

Quantity_Color Srgb(int r, int g, int b) {
    return Quantity_Color(r / 255.0, g / 255.0, b / 255.0, Quantity_TOC_sRGB);
}

Aspect_VKeyMouse Buttons(Qt::MouseButtons b) {
    Aspect_VKeyMouse r = Aspect_VKeyMouse_NONE;
    if (b & Qt::LeftButton) r |= Aspect_VKeyMouse_LeftButton;
    if (b & Qt::MiddleButton) r |= Aspect_VKeyMouse_MiddleButton;
    if (b & Qt::RightButton) r |= Aspect_VKeyMouse_RightButton;
    return r;
}

Aspect_VKeyFlags Modifiers(Qt::KeyboardModifiers m) {
    Aspect_VKeyFlags r = Aspect_VKeyFlags_NONE;
    if (m & Qt::ShiftModifier) r |= Aspect_VKeyFlags_SHIFT;
    if (m & Qt::ControlModifier) r |= Aspect_VKeyFlags_CTRL;
    if (m & Qt::AltModifier) r |= Aspect_VKeyFlags_ALT;
    return r;
}

}  // namespace

OccView::OccView(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_NativeWindow);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(200, 150);

    Handle(Aspect_DisplayConnection) display = new Aspect_DisplayConnection();
    Handle(OpenGl_GraphicDriver) driver = new OpenGl_GraphicDriver(display, false);
    viewer_ = new V3d_Viewer(driver);
    // Three lights. The key light is fixed to the model (above, from the
    // front left) and casts soft shadows, so parts sit on one another instead
    // of floating. A weaker light rides with the camera so no side of the
    // model is ever black, and an ambient fill lifts the shadows.
    Handle(V3d_DirectionalLight) key = new V3d_DirectionalLight(gp_Dir(0.45, 0.6, -1.0), Quantity_NOC_WHITE, false);
    key->SetIntensity(1.75f);
    key->SetCastShadows(true);
    Handle(V3d_DirectionalLight) camera = new V3d_DirectionalLight(gp_Dir(0.25, -0.2, -1.0), Quantity_NOC_WHITE, true);
    camera->SetIntensity(0.7f);
    Handle(V3d_AmbientLight) fill = new V3d_AmbientLight(Quantity_NOC_WHITE);
    fill->SetIntensity(0.72f);
    viewer_->AddLight(key);
    viewer_->AddLight(camera);
    viewer_->AddLight(fill);
    viewer_->SetLightOn();

    ctx_ = new AIS_InteractiveContext(viewer_);
    ctx_->SetDisplayMode(AIS_Shaded, false);
    const Handle(Prs3d_Drawer)& drawer = ctx_->DefaultDrawer();
    drawer->SetAutoTriangulation(false);  // parts are meshed by the loader, off the UI thread
    drawer->SetFaceBoundaryDraw(true);
    drawer->SetFaceBoundaryAspect(new Prs3d_LineAspect(Srgb(0x4A, 0x4E, 0x56), Aspect_TOL_SOLID, 1.0));

    // Brand accent for selection; a paler tone while hovering.
    const Quantity_Color orange = Srgb(0xFF, 0x5B, 0x1F), hover = Srgb(0xFF, 0xB0, 0x8C);
    for (Prs3d_TypeOfHighlight t : {Prs3d_TypeOfHighlight_Selected, Prs3d_TypeOfHighlight_LocalSelected})
        ctx_->HighlightStyle(t)->SetColor(orange);
    for (Prs3d_TypeOfHighlight t : {Prs3d_TypeOfHighlight_Dynamic, Prs3d_TypeOfHighlight_LocalDynamic})
        ctx_->HighlightStyle(t)->SetColor(hover);

    view_ = viewer_->CreateView();
    Handle(WNT_Window) window = new WNT_Window((Aspect_Handle)winId());
    view_->SetWindow(window);
    view_->SetBackgroundColor(Srgb(0xF3, 0xF1, 0xEA));
    view_->ChangeRenderingParams().NbMsaaSamples = 4;
    view_->ChangeRenderingParams().ShadowMapResolution = 2048;
    view_->ChangeRenderingParams().IsShadowEnabled = true;
    view_->SetShadingModel(Graphic3d_TypeOfShadingModel_Phong);
    view_->Camera()->SetFOVy(32.0);  // a calm perspective, when it is switched on
    view_->ChangeRenderingParams().Resolution = (unsigned int)(96.0 * devicePixelRatioF() + 0.5);
    view_->TriedronDisplay(Aspect_TOTP_LEFT_LOWER, Quantity_NOC_GRAY40, 0.09, V3d_ZBUFFER);
    view_->SetProj(V3d_XposYnegZpos);

    setMousePreset(0);
    SetAllowRotation(true);
    setCursor(idleCursor_);
}

void OccView::fitAll() {
    view_->FitAll(0.02, false);
    view_->ZFitAll();
    view_->Invalidate();
    update();
}

void OccView::redraw() {
    view_->Invalidate();
    update();
}

void OccView::fitPoints(const std::vector<gp_Pnt>& points, const Bnd_Box& all) {
    if (all.IsVoid() || points.empty()) { fitAll(); return; }
    // First a coarse fit on the overall box, which also sets a sane depth.
    view_->FitAll(all, 0.04, false);
    // Then tighten to the model's outline on the screen: a box around an
    // L-shaped or lopsided model would leave it sitting off to one side.
    double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
    for (const gp_Pnt& p : points) {
        double px, py;
        view_->Project(p.X(), p.Y(), p.Z(), px, py);
        minX = std::min(minX, px); maxX = std::max(maxX, px);
        minY = std::min(minY, py); maxY = std::max(maxY, py);
    }
    // Room around the model, with more at the bottom and top where the
    // floating bar and the tool pill sit.
    const double span = std::max(maxX - minX, maxY - minY);
    if (maxX > minX || maxY > minY)
        view_->FitAll(minX - 0.05 * span, minY - 0.17 * span, maxX + 0.05 * span, maxY + 0.09 * span);
    view_->ZFitAll();
    view_->Invalidate();
    update();
}

void OccView::fitPointsQuick(const std::vector<gp_Pnt>& points) {
    if (points.empty()) return;
    const Handle(Graphic3d_Camera) cam = view_->Camera();
    const gp_Vec sight(cam->Direction()), up(cam->Up());
    const gp_Vec side = sight.Crossed(up);
    const gp_Pnt centre = cam->Center();
    double minX = 1e300, minY = 1e300, minZ = 1e300, maxX = -1e300, maxY = -1e300, maxZ = -1e300;
    for (const gp_Pnt& p : points) {
        const gp_Vec v(centre, p);
        const double x = v.Dot(side), y = v.Dot(up), z = v.Dot(sight);
        minX = std::min(minX, x); maxX = std::max(maxX, x);
        minY = std::min(minY, y); maxY = std::max(maxY, y);
        minZ = std::min(minZ, z); maxZ = std::max(maxZ, z);
    }
    // The same margins as fitPoints: more at the bottom and top, for the floating controls.
    const double span = std::max(maxX - minX, maxY - minY);
    minX -= 0.05 * span; maxX += 0.05 * span;
    minY -= 0.17 * span; maxY += 0.09 * span;
    const double aspect = std::max(cam->Aspect(), 1e-6);
    const double height = std::max(maxY - minY, (maxX - minX) / aspect);
    if (height <= 0) return;
    const gp_Vec shift = side * ((minX + maxX) / 2) + up * ((minY + maxY) / 2);
    cam->SetEyeAndCenter(cam->Eye().Translated(shift), centre.Translated(shift));
    cam->SetScale(height);
    // Keep everything between the near and far planes.
    const double distance = cam->Distance(), depth = std::max(maxZ - minZ, span);
    cam->SetZRange(distance + minZ - depth, distance + maxZ + depth);
    view_->Invalidate();
    update();
}

void OccView::setOrientation(V3d_TypeOfOrientation o) {
    view_->SetProj(o);
    view_->Invalidate();
    update();
}

void OccView::setPerspective(bool on) {
    view_->Camera()->SetProjectionType(on ? Graphic3d_Camera::Projection_Perspective
                                          : Graphic3d_Camera::Projection_Orthographic);
    view_->Invalidate();
    update();
}

void OccView::setBackground(const QColor& c) {
    view_->SetBackgroundColor(Srgb(c.red(), c.green(), c.blue()));
    view_->Invalidate();
    update();
}

bool OccView::dump(const QString& path) {
    view_->Redraw();
    return view_->Dump(path.toUtf8().constData());
}

QPoint OccView::project(const gp_Pnt& p) const {
    int x = 0, y = 0;
    view_->Convert(p.X(), p.Y(), p.Z(), x, y);
    const double dpr = devicePixelRatioF();
    return QPoint(int(x / dpr), int(y / dpr));
}

// Qt can deliver a resize event before the native window has its new size
// (typically while the widget is hidden, or as it is first shown). OpenCASCADE
// then keeps drawing into the old, small rectangle until the next resize. So
// before every frame, compare what OpenCASCADE believes with the real window.
void OccView::syncSize() {
    const Handle(Aspect_Window) window = view_->Window();
    if (window.IsNull()) return;
    int before[2] = {0, 0}, after[2] = {0, 0};
    window->Size(before[0], before[1]);
    window->DoResize();  // re-reads the real size of the native window
    window->Size(after[0], after[1]);
    if (before[0] != after[0] || before[1] != after[1]) {
        view_->MustBeResized();
        view_->Invalidate();
    }
}

void OccView::flush() {
    syncSize();
    view_->InvalidateImmediate();
    FlushViewEvents(ctx_, view_, true);
}

void OccView::showEvent(QShowEvent*) {
    syncSize();
    update();
}

void OccView::paintEvent(QPaintEvent*) { flush(); }

void OccView::resizeEvent(QResizeEvent*) {
    view_->MustBeResized();
    view_->Invalidate();
    update();
}

void OccView::mouseButtons(QMouseEvent* e) {
    const QPointF p = e->position() * devicePixelRatioF();
    if (UpdateMouseButtons(NCollection_Vec2<int>((int)p.x(), (int)p.y()), Buttons(e->buttons()),
                           Modifiers(e->modifiers()), false))
        update();
}

void OccView::setMousePreset(int preset) {
    const unsigned int L = Aspect_VKeyMouse_LeftButton, M = Aspect_VKeyMouse_MiddleButton, R = Aspect_VKeyMouse_RightButton;
    const unsigned int ctrl = Aspect_VKeyFlags_CTRL, shift = Aspect_VKeyFlags_SHIFT;
    myMouseGestureMap.Clear();
    switch (preset) {
    case 1:  // SolidWorks: middle drag orbits, Ctrl+middle pans, Shift+middle zooms
        myMouseGestureMap.Bind(M, AIS_MouseGesture_RotateOrbit);
        myMouseGestureMap.Bind(M | ctrl, AIS_MouseGesture_Pan);
        myMouseGestureMap.Bind(M | shift, AIS_MouseGesture_Zoom);
        myMouseGestureMap.Bind(L, AIS_MouseGesture_SelectRectangle);
        break;
    case 2:  // Fusion and Inventor: middle drag pans, Shift+middle orbits
        myMouseGestureMap.Bind(M, AIS_MouseGesture_Pan);
        myMouseGestureMap.Bind(M | shift, AIS_MouseGesture_RotateOrbit);
        myMouseGestureMap.Bind(L, AIS_MouseGesture_SelectRectangle);
        break;
    case 3:  // Onshape: right drag orbits, middle or Ctrl+right pans
        myMouseGestureMap.Bind(R, AIS_MouseGesture_RotateOrbit);
        myMouseGestureMap.Bind(R | ctrl, AIS_MouseGesture_Pan);
        myMouseGestureMap.Bind(M, AIS_MouseGesture_Pan);
        myMouseGestureMap.Bind(L, AIS_MouseGesture_SelectRectangle);
        break;
    case 4:  // Creo and NX: middle drag orbits, Shift+middle pans, Ctrl+middle zooms
        myMouseGestureMap.Bind(M, AIS_MouseGesture_RotateOrbit);
        myMouseGestureMap.Bind(M | shift, AIS_MouseGesture_Pan);
        myMouseGestureMap.Bind(M | ctrl, AIS_MouseGesture_Zoom);
        myMouseGestureMap.Bind(L, AIS_MouseGesture_SelectRectangle);
        break;
    case 5:  // Rhino: right drag orbits, Shift+right pans, Ctrl+right zooms
        myMouseGestureMap.Bind(R, AIS_MouseGesture_RotateOrbit);
        myMouseGestureMap.Bind(R | shift, AIS_MouseGesture_Pan);
        myMouseGestureMap.Bind(R | ctrl, AIS_MouseGesture_Zoom);
        myMouseGestureMap.Bind(M, AIS_MouseGesture_Pan);
        myMouseGestureMap.Bind(L, AIS_MouseGesture_SelectRectangle);
        break;
    default:  // sideSTEP: left drag orbits, middle or right drag pans
        myMouseGestureMap.Bind(L, AIS_MouseGesture_RotateOrbit);
        myMouseGestureMap.Bind(M, AIS_MouseGesture_Pan);
        myMouseGestureMap.Bind(R, AIS_MouseGesture_Pan);
        myMouseGestureMap.Bind(L | ctrl, AIS_MouseGesture_Zoom);
        break;
    }
}

void OccView::setShadows(bool on) {
    view_->ChangeRenderingParams().IsShadowEnabled = on;
    view_->Invalidate();
    update();
}

void OccView::setIdleCursor(Qt::CursorShape shape) {
    idleCursor_ = shape;
    setCursor(shape);
}

void OccView::cameraFrame(gp_Dir& towardEye, gp_Dir& up) const {
    towardEye = view_->Camera()->Direction().Reversed();
    up = view_->Camera()->Up();
}

void OccView::animateTo(const gp_Dir& towardEye, const gp_Dir& up, std::function<void()> done) {
    // A camera attitude as a rotation: its right, up and toward-the-eye axes.
    auto attitude = [](const gp_Dir& back, const gp_Dir& upHint) {
        gp_Vec right = gp_Vec(upHint).Crossed(gp_Vec(back));
        if (right.Magnitude() < 1e-6) right = gp_Vec(1, 0, 0);
        right.Normalize();
        const gp_Vec trueUp = gp_Vec(back).Crossed(right);
        gp_Mat m;
        m.SetCols(right.XYZ(), trueUp.XYZ(), back.XYZ());
        return gp_Quaternion(m);
    };
    const Handle(Graphic3d_Camera) cam = view_->Camera();
    const gp_Quaternion from = attitude(cam->Direction().Reversed(), cam->Up());
    const gp_Quaternion to = attitude(towardEye, up);
    if (turn_) { turn_->stop(); turn_->deleteLater(); }
    turn_ = new QVariantAnimation(this);
    turn_->setDuration(280);
    turn_->setStartValue(0.0);
    turn_->setEndValue(1.0);
    turn_->setEasingCurve(QEasingCurve::InOutCubic);
    connect(turn_, &QVariantAnimation::valueChanged, this, [this, from, to](const QVariant& v) {
        gp_QuaternionSLerp slerp(from, to);
        gp_Quaternion q;
        slerp.Interpolate(v.toDouble(), q);
        const gp_Mat m = q.GetMatrix();
        const Handle(Graphic3d_Camera) c = view_->Camera();
        c->SetUp(gp_Dir(m.Column(2)));
        c->SetDirection(gp_Dir(m.Column(3)).Reversed());
        view_->Invalidate();
        update();
    });
    connect(turn_, &QVariantAnimation::finished, this, [this, done] {
        turn_->deleteLater();
        turn_ = nullptr;
        if (done) done();
    });
    turn_->start();
}

// Double-click: bring that spot to the middle and move in.
void OccView::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    const QPointF p = e->position() * devicePixelRatioF();
    ctx_->MoveTo((int)p.x(), (int)p.y(), view_, false);
    if (!ctx_->HasDetected()) return;
    const gp_Pnt spot = ctx_->MainSelector()->PickedPoint(1);
    const Handle(Graphic3d_Camera) cam = view_->Camera();
    // Slide sideways (not along the line of sight) so the spot is centred.
    gp_Vec shift(cam->Center(), spot);
    const gp_Vec sight(cam->Direction());
    shift -= sight * shift.Dot(sight);
    cam->SetEyeAndCenter(cam->Eye().Translated(shift), cam->Center().Translated(shift));
    cam->SetScale(cam->Scale() / 2.0);
    view_->ZFitAll();
    view_->Invalidate();
    update();
}

void OccView::mousePressEvent(QMouseEvent* e) {
    setFocus();
    if (e->button() == Qt::LeftButton && idleCursor_ == Qt::OpenHandCursor) setCursor(Qt::ClosedHandCursor);
    if (e->button() == Qt::RightButton) rightPress_ = e->pos();
    mouseButtons(e);
}

void OccView::mouseReleaseEvent(QMouseEvent* e) {
    mouseButtons(e);
    if (e->buttons() == Qt::NoButton) setCursor(idleCursor_);
    if (e->button() == Qt::RightButton && (e->pos() - rightPress_).manhattanLength() < 4)
        emit contextRequested(e->globalPosition().toPoint());
}

bool OccView::selectAt(const QPoint& pos) {
    const double dpr = devicePixelRatioF();
    ctx_->MoveTo(int(pos.x() * dpr), int(pos.y() * dpr), view_, false);
    if (!ctx_->HasDetected()) return false;
    if (!ctx_->IsSelected(ctx_->DetectedOwner())) {
        ctx_->SelectDetected();
        emit selectionChanged();
    }
    redraw();
    return true;
}

void OccView::mouseMoveEvent(QMouseEvent* e) {
    const QPointF p = e->position() * devicePixelRatioF();
    if (UpdateMousePosition(NCollection_Vec2<int>((int)p.x(), (int)p.y()), Buttons(e->buttons()),
                            Modifiers(e->modifiers()), false))
        update();
}

void OccView::wheelEvent(QWheelEvent* e) {
    const QPointF p = e->position() * devicePixelRatioF();
    if (UpdateZoom(Aspect_ScrollDelta(NCollection_Vec2<int>((int)p.x(), (int)p.y()),
                                      double(e->angleDelta().y()) / 8.0 * (reverseZoom_ ? -1.0 : 1.0))))
        update();
}

void OccView::leaveEvent(QEvent*) {
    // Drop the hover highlight when the cursor leaves the view.
    if (UpdateMousePosition(NCollection_Vec2<int>(-1, -1), Aspect_VKeyMouse_NONE, Aspect_VKeyFlags_NONE, false))
        update();
}

void OccView::OnSelectionChanged(const Handle(AIS_InteractiveContext)&, const Handle(V3d_View)&) {
    emit selectionChanged();
}

void OccView::handleViewRedraw(const Handle(AIS_InteractiveContext)& ctx, const Handle(V3d_View)& view) {
    AIS_ViewController::handleViewRedraw(ctx, view);
    emit viewRedrawn();
    if (myToAskNextFrame) update();  // an animation or inertia is still running
}
