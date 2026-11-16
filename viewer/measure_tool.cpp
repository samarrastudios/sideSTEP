#include "measure_tool.h"

#include "measure.h"
#include "occ_view.h"

#include <AIS_Shape.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS_Edge.hxx>

#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

Quantity_Color Orange() { return Quantity_Color(1.0, 0x5B / 255.0, 0x1F / 255.0, Quantity_TOC_sRGB); }

const char* ModeName(MeasureTool::Mode m) {
    switch (m) {
    case MeasureTool::Distance: return "Distance";
    case MeasureTool::PointToPoint: return "Point to point";
    case MeasureTool::Dimension: return "Length / diameter";
    case MeasureTool::Angle: return "Angle";
    default: return "Overall size";
    }
}

bool TwoPicks(MeasureTool::Mode m) {
    return m == MeasureTool::Distance || m == MeasureTool::PointToPoint || m == MeasureTool::Angle;
}

}  // namespace

MeasureTool::MeasureTool(OccView* view, QWidget* parent) : QWidget(parent), view_(view) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    auto* title = new QLabel(tr("MEASURE"), this);
    title->setStyleSheet(QStringLiteral("font-weight:600; letter-spacing:1px; color:#8A8C90;"));
    layout->addWidget(title);

    // What to measure.
    auto* grid = new QGridLayout;
    grid->setSpacing(4);
    modes_ = new QButtonGroup(this);
    const Mode order[] = {Distance, PointToPoint, Dimension, Angle, Size};
    const char* tips[] = {
        "Shortest distance between two corners, edges or faces. Also gives hole centre-to-centre.",
        "Straight-line distance between two corners.",
        "Click one edge or face: edge length, hole diameter, arc radius or face area.",
        "Angle between two straight edges or flat faces.",
        "Overall X, Y, Z size of a part you click, or of the whole model."};
    for (int i = 0; i < 5; ++i) {
        auto* b = new QToolButton(this);
        b->setText(tr(ModeName(order[i])));
        b->setToolTip(tr(tips[i]));
        b->setCheckable(true);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setChecked(order[i] == mode_);
        modes_->addButton(b, order[i]);
        grid->addWidget(b, i / 2, i % 2);
    }
    layout->addLayout(grid);
    connect(modes_, &QButtonGroup::idClicked, this, [this](int id) { setMode(Mode(id)); });

    hint_ = new QLabel(this);
    hint_->setWordWrap(true);
    hint_->setStyleSheet(QStringLiteral("color:#B9B7B0;"));
    layout->addWidget(hint_);

    auto* whole = new QPushButton(tr("Measure the whole model"), this);
    wholeModel_ = whole;
    connect(whole, &QPushButton::clicked, this, [this] {
        add(Size, TopoDS_Shape(), TopoDS_Shape(), bounds_, tr("Whole model"));
    });
    layout->addWidget(whole);

    // Kept measurements.
    list_ = new QListWidget(this);
    list_->setWordWrap(true);
    list_->setSpacing(2);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(list_, 1);

    auto* row = new QHBoxLayout;
    row->setSpacing(4);
    auto* remove = new QPushButton(tr("Remove"), this);
    auto* clearAll = new QPushButton(tr("Clear"), this);
    auto* copy = new QPushButton(tr("Copy"), this);
    remove->setToolTip(tr("Remove the measurement selected in the list (Delete)"));
    copy->setToolTip(tr("Copy all measurements as text"));
    for (QPushButton* b : {remove, clearAll, copy}) {
        b->setStyleSheet(QStringLiteral("padding:5px 8px;"));
        row->addWidget(b);
    }
    layout->addLayout(row);
    connect(remove, &QPushButton::clicked, this, [this] { removeAt(list_->currentRow()); });
    connect(clearAll, &QPushButton::clicked, this, &MeasureTool::clear);
    connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(text()); });

    auto* unitRow = new QHBoxLayout;
    unitRow->addWidget(new QLabel(tr("Units"), this));
    units_ = new QComboBox(this);
    units_->addItems({tr("mm"), tr("inches"), tr("mm and inches")});
    units_->setCurrentIndex(QSettings().value(QStringLiteral("measure/units"), 2).toInt());
    SetLengthUnits(LengthUnits(units_->currentIndex()));
    unitRow->addWidget(units_, 1);
    layout->addLayout(unitRow);
    connect(units_, &QComboBox::currentIndexChanged, this, [this](int index) {
        SetLengthUnits(LengthUnits(index));
        QSettings().setValue(QStringLiteral("measure/units"), index);
        for (Item& item : items_) {
            describe(item, nullptr, nullptr, nullptr);
            item.label->setText(item.summary);
            item.label->adjustSize();
        }
        refreshList();
        placeLabels();
    });

    updateHint();
}

void MeasureTool::setModel(const QVector<Handle(AIS_InteractiveObject)>& parts, const Bnd_Box& bounds, PartInfo info) {
    clear();
    parts_ = parts;
    bounds_ = bounds;
    info_ = std::move(info);
}

void MeasureTool::setActive(bool on) {
    if (on == active_) return;
    active_ = on;
    dropPending();
    view_->context()->ClearSelected(false);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    applyModeToAll();
    QApplication::restoreOverrideCursor();
    view_->setIdleCursor(on ? Qt::CrossCursor : Qt::OpenHandCursor);
    view_->redraw();
}

void MeasureTool::setMode(Mode mode) {
    if (modes_->checkedId() != mode) modes_->button(mode)->setChecked(true);
    if (mode == mode_) return;
    mode_ = mode;
    dropPending();
    updateHint();
    if (!active_) return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    applyModeToAll();
    QApplication::restoreOverrideCursor();
    view_->redraw();
}

// Which kinds of thing can be picked depends on the mode.
void MeasureTool::applyMode(const Handle(AIS_InteractiveObject)& part) {
    const Handle(AIS_InteractiveContext)& ctx = view_->context();
    ctx->Deactivate(part);
    if (!active_ || mode_ == Size) {
        ctx->Activate(part, 0);  // whole parts
        return;
    }
    ctx->Activate(part, AIS_Shape::SelectionMode(TopAbs_VERTEX));
    if (mode_ == PointToPoint) return;
    ctx->Activate(part, AIS_Shape::SelectionMode(TopAbs_EDGE));
    ctx->Activate(part, AIS_Shape::SelectionMode(TopAbs_FACE));
}

void MeasureTool::applyModeToAll() {
    const Handle(AIS_InteractiveContext)& ctx = view_->context();
    for (const Handle(AIS_InteractiveObject)& part : parts_)
        if (ctx->IsDisplayed(part)) applyMode(part);
}

void MeasureTool::partShown(const Handle(AIS_InteractiveObject)& part) {
    if (active_) applyMode(part);
}

void MeasureTool::updateHint() {
    const char* text = "";
    switch (mode_) {
    case Distance: text = pending_.IsNull() ? "Click a corner, edge or face." : "Now click the second corner, edge or face."; break;
    case PointToPoint: text = pending_.IsNull() ? "Click a corner." : "Now click the second corner."; break;
    case Dimension: text = "Click an edge for its length, a hole for its diameter, or a face for its area."; break;
    case Angle: text = pending_.IsNull() ? "Click a straight edge or flat face." : "Now click the second edge or face."; break;
    case Size: text = "Click a part for its overall size."; break;
    }
    hint_->setText(tr(text));
    wholeModel_->setVisible(mode_ == Size);
}

Handle(AIS_InteractiveObject) MeasureTool::mark(const TopoDS_Shape& s, bool shaded) {
    const Handle(AIS_InteractiveContext)& ctx = view_->context();
    Handle(AIS_Shape) o = new AIS_Shape(s);
    o->SetColor(Orange());
    o->SetWidth(3.0);
    if (shaded) o->SetTransparency(0.6);  // a tint: the model stays readable underneath
    ctx->Display(o, shaded ? AIS_Shaded : AIS_WireFrame, -1, false);
    ctx->SetZLayer(o, Graphic3d_ZLayerId_Topmost);
    return o;
}

void MeasureTool::dropPending() {
    if (!pendingMark_.IsNull()) view_->context()->Remove(pendingMark_, false);
    pendingMark_.Nullify();
    pending_.Nullify();
    updateHint();
}

bool MeasureTool::cancelPending() {
    if (pending_.IsNull()) return false;
    dropPending();
    view_->redraw();
    return true;
}

void MeasureTool::pick() {
    if (!active_) return;
    const Handle(AIS_InteractiveContext)& ctx = view_->context();
    ctx->InitSelected();
    if (!ctx->MoreSelected()) return;

    if (mode_ == Size) {
        Handle(AIS_InteractiveObject) part = ctx->SelectedInteractive();
        ctx->ClearSelected(false);
        QString name;
        Bnd_Box box;
        if (!part.IsNull() && info_ && info_(part, name, box)) add(Size, TopoDS_Shape(), TopoDS_Shape(), box, name);
        view_->redraw();
        return;
    }

    Handle(StdSelect_BRepOwner) owner = Handle(StdSelect_BRepOwner)::DownCast(ctx->SelectedOwner());
    ctx->ClearSelected(false);
    if (owner.IsNull() || !owner->HasShape()) { view_->redraw(); return; }
    TopoDS_Shape shape = owner->Shape();
    if (owner->HasLocation()) shape = shape.Located(owner->Location() * shape.Location());

    if (!TwoPicks(mode_)) {
        add(mode_, shape, TopoDS_Shape(), Bnd_Box(), QString());
    } else if (pending_.IsNull()) {
        pending_ = shape;
        pendingMark_ = mark(shape, shape.ShapeType() == TopAbs_FACE);
        updateHint();
    } else {
        const TopoDS_Shape first = pending_;
        dropPending();
        add(mode_, first, shape, Bnd_Box(), QString());
    }
    view_->redraw();
}

// Fills in the texts and the label position; the same call re-does them when
// the units change.
bool MeasureTool::describe(Item& item, bool* hasLine, gp_Pnt* from, gp_Pnt* to) const {
    MeasureResult r;
    switch (item.mode) {
    case Distance:
    case PointToPoint: r = MeasureDistance(item.a, item.b); break;
    case Dimension: r = MeasureDimension(item.a); break;
    case Angle: r = MeasureAngle(item.a, item.b); break;
    case Size: r = MeasureBox(item.box); break;
    }
    item.summary = r.summary;
    item.details = r.details;
    item.anchor = r.anchor;
    if (item.mode == Size && !item.name.isEmpty()) item.details = item.name + QChar(u'\n') + item.details;
    if (hasLine) *hasLine = r.hasLine;
    if (from) *from = r.a;
    if (to) *to = r.b;
    return r.ok;
}

void MeasureTool::add(Mode mode, const TopoDS_Shape& a, const TopoDS_Shape& b, const Bnd_Box& box, const QString& name) {
    Item item;
    item.mode = mode;
    item.a = a;
    item.b = b;
    item.box = box;
    item.name = name;
    bool hasLine = false;
    gp_Pnt from, to;
    if (!describe(item, &hasLine, &from, &to)) {
        hint_->setText(item.details);  // says what went wrong
        view_->redraw();
        return;
    }
    if (mode == Size) {
        double x0, y0, z0, x1, y1, z1;
        box.Get(x0, y0, z0, x1, y1, z1);
        if (x1 - x0 > 1e-7 && y1 - y0 > 1e-7 && z1 - z0 > 1e-7)
            item.objects.push_back(mark(BRepPrimAPI_MakeBox(gp_Pnt(x0, y0, z0), gp_Pnt(x1, y1, z1)).Shape(), false));
    } else {
        item.objects.push_back(mark(a, a.ShapeType() == TopAbs_FACE));
        if (!b.IsNull()) item.objects.push_back(mark(b, b.ShapeType() == TopAbs_FACE));
        if (hasLine) item.objects.push_back(mark(BRepBuilderAPI_MakeEdge(from, to).Edge(), false));
    }
    item.label = new QLabel(item.summary, view_);
    item.label->setAttribute(Qt::WA_NativeWindow);
    item.label->setStyleSheet(QStringLiteral("background:#FF5B1F; color:#111316; padding:3px 7px; font-weight:600;"));
    item.label->adjustSize();
    items_.push_back(item);
    refreshList();
    list_->setCurrentRow(list_->count() - 1);
    updateHint();
    placeLabels();
    view_->redraw();
}

void MeasureTool::removeAt(int index) {
    if (index < 0 || index >= items_.size()) return;
    for (const Handle(AIS_InteractiveObject)& o : items_[index].objects) view_->context()->Remove(o, false);
    delete items_[index].label;
    items_.remove(index);
    refreshList();
    view_->redraw();
}

void MeasureTool::clear() {
    dropPending();
    for (const Item& item : items_) {
        for (const Handle(AIS_InteractiveObject)& o : item.objects) view_->context()->Remove(o, false);
        delete item.label;
    }
    items_.clear();
    refreshList();
    view_->redraw();
}

void MeasureTool::refreshList() {
    list_->clear();
    for (int i = 0; i < items_.size(); ++i)
        list_->addItem(QStringLiteral("%1.  %2\n%3").arg(i + 1).arg(tr(ModeName(items_[i].mode)), items_[i].details));
}

void MeasureTool::placeLabels() {
    for (const Item& item : items_) {
        const QPoint p = view_->project(item.anchor) + QPoint(10, -item.label->height() - 6);
        if (item.label->pos() != p) item.label->move(p);
        if (!item.label->isVisible()) {
            item.label->show();
            item.label->raise();
        }
    }
}

QString MeasureTool::text() const {
    QStringList out;
    for (int i = 0; i < items_.size(); ++i)
        out << QStringLiteral("%1. %2\n%3").arg(i + 1).arg(tr(ModeName(items_[i].mode)), items_[i].details);
    return out.join(QStringLiteral("\n\n"));
}

QVector<QLabel*> MeasureTool::labels() const {
    QVector<QLabel*> out;
    for (const Item& item : items_) out.push_back(item.label);
    return out;
}
