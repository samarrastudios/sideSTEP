#include "section_tool.h"

#include "occ_view.h"

#include <gp_Pln.hxx>

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>

#include <algorithm>

namespace {

// The X / Y / Z selector both tools share.
QButtonGroup* AxisButtons(QWidget* parent, QHBoxLayout* layout, int current) {
    auto* group = new QButtonGroup(parent);
    const char* names[] = {"X", "Y", "Z"};
    for (int i = 0; i < 3; ++i) {
        auto* b = new QToolButton(parent);
        b->setText(QString::fromLatin1(names[i]));
        b->setToolTip(QObject::tr("Cut across the %1 axis").arg(QString::fromLatin1(names[i])));
        b->setCheckable(true);
        b->setChecked(i == current);
        b->setFocusPolicy(Qt::NoFocus);
        b->setFixedWidth(30);
        group->addButton(b, i);
        layout->addWidget(b);
    }
    return group;
}

QSlider* Slider(QWidget* parent, int value, const QString& tip) {
    auto* s = new QSlider(Qt::Horizontal, parent);
    s->setRange(0, 1000);
    s->setValue(value);
    s->setFixedWidth(150);
    s->setToolTip(tip);
    s->setFocusPolicy(Qt::NoFocus);
    return s;
}

Handle(Graphic3d_ClipPlane) CappedPlane() {
    Handle(Graphic3d_ClipPlane) p = new Graphic3d_ClipPlane();
    p->SetCapping(true);
    p->SetUseObjectMaterial(true);
    return p;
}

}  // namespace

// ------------------------------------------------------------------ section ---

SectionTool::SectionTool(OccView* view) : view_(view) {
    plane_ = CappedPlane();
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    axes_ = AxisButtons(this, layout, axis_);
    layout->addSpacing(6);
    slider_ = Slider(this, 500, tr("Move the cut through the model"));
    layout->addWidget(slider_);
    layout->addSpacing(6);
    auto* flip = new QToolButton(this);
    flip->setText(tr("Flip"));
    flip->setToolTip(tr("Keep the other side of the cut"));
    flip->setCheckable(true);
    flip->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(flip);

    connect(axes_, &QButtonGroup::idClicked, this, [this](int id) { axis_ = id; apply(); });
    connect(flip, &QToolButton::toggled, this, [this](bool on) { flipped_ = on; apply(); });
    connect(slider_, &QSlider::valueChanged, this, [this] { apply(); });
}

void SectionTool::setBounds(const Bnd_Box& bounds) {
    bounds_ = bounds;
    apply();
}

void SectionTool::setActive(bool on) {
    if (on == active_) return;
    active_ = on;
    if (on) {
        apply();
        view_->view()->AddClipPlane(plane_);
    } else {
        view_->view()->RemoveClipPlane(plane_);
    }
    view_->redraw();
}

void SectionTool::setAxis(int axis) {
    axis_ = axis;
    axes_->button(axis)->setChecked(true);
    apply();
}

void SectionTool::setPosition(double fraction) { slider_->setValue(int(fraction * 1000)); }

void SectionTool::apply() {
    if (bounds_.IsVoid()) return;
    double lo[3], hi[3];
    bounds_.Get(lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]);
    gp_Pnt point((lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2);
    point.SetCoord(axis_ + 1, lo[axis_] + (hi[axis_] - lo[axis_]) * slider_->value() / 1000.0);
    // The plane keeps the side its normal points to. By default that is the
    // side away from the usual viewpoint (front-right-top), so the cut face
    // looks at you.
    const double away[3] = {-1.0, 1.0, -1.0};
    double n[3] = {0, 0, 0};
    n[axis_] = flipped_ ? -away[axis_] : away[axis_];
    plane_->SetEquation(gp_Pln(point, gp_Dir(n[0], n[1], n[2])));
    if (active_) view_->redraw();
}

// -------------------------------------------------------------- section box ---

SectionBoxTool::SectionBoxTool(OccView* view) : view_(view) {
    for (Handle(Graphic3d_ClipPlane)& p : planes_) p = CappedPlane();
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    axes_ = AxisButtons(this, layout, axis_);
    layout->addSpacing(6);
    lowSlider_ = Slider(this, int(low_[axis_] * 1000), tr("Low side of the box on this axis"));
    highSlider_ = Slider(this, int(high_[axis_] * 1000), tr("High side of the box on this axis"));
    lowSlider_->setFixedWidth(110);
    highSlider_->setFixedWidth(110);
    layout->addWidget(lowSlider_);
    layout->addWidget(highSlider_);
    layout->addSpacing(6);
    auto* reset = new QToolButton(this);
    reset->setText(tr("Reset"));
    reset->setToolTip(tr("Put the box back around the whole model, with the top off"));
    reset->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(reset);

    connect(axes_, &QButtonGroup::idClicked, this, [this](int id) { axis_ = id; showAxis(); });
    connect(lowSlider_, &QSlider::valueChanged, this, [this](int v) {
        low_[axis_] = std::min(v / 1000.0, high_[axis_] - 0.01);
        apply();
    });
    connect(highSlider_, &QSlider::valueChanged, this, [this](int v) {
        high_[axis_] = std::max(v / 1000.0, low_[axis_] + 0.01);
        apply();
    });
    connect(reset, &QToolButton::clicked, this, [this] {
        for (int i = 0; i < 3; ++i) { low_[i] = 0; high_[i] = i == 2 ? 0.6 : 1; }
        showAxis();
        apply();
    });
}

void SectionBoxTool::showAxis() {
    const QSignalBlocker a(lowSlider_), b(highSlider_);
    lowSlider_->setValue(int(low_[axis_] * 1000));
    highSlider_->setValue(int(high_[axis_] * 1000));
}

void SectionBoxTool::setBounds(const Bnd_Box& bounds) {
    bounds_ = bounds;
    apply();
}

void SectionBoxTool::setRange(int axis, double low, double high) {
    low_[axis] = low;
    high_[axis] = high;
    if (axis == axis_) showAxis();
    apply();
}

void SectionBoxTool::setActive(bool on) {
    if (on == active_) return;
    active_ = on;
    if (on) apply();
    for (const Handle(Graphic3d_ClipPlane)& p : planes_) {
        if (on) view_->view()->AddClipPlane(p);
        else view_->view()->RemoveClipPlane(p);
    }
    view_->redraw();
}

void SectionBoxTool::apply() {
    if (bounds_.IsVoid()) return;
    double lo[3], hi[3];
    bounds_.Get(lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]);
    const gp_Pnt centre((lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2);
    for (int axis = 0; axis < 3; ++axis) {
        const double span = hi[axis] - lo[axis];
        double n[3] = {0, 0, 0};
        gp_Pnt at = centre;
        // Low side: keep everything above it. High side: keep everything below it.
        at.SetCoord(axis + 1, lo[axis] + span * low_[axis]);
        n[axis] = 1;
        planes_[axis * 2]->SetEquation(gp_Pln(at, gp_Dir(n[0], n[1], n[2])));
        at.SetCoord(axis + 1, lo[axis] + span * high_[axis]);
        n[axis] = -1;
        planes_[axis * 2 + 1]->SetEquation(gp_Pln(at, gp_Dir(n[0], n[1], n[2])));
    }
    if (active_) view_->redraw();
}
