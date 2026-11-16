// The measure tool: its side panel, the picking modes, and the measurements
// it keeps on screen until they are removed.
#pragma once
#include <AIS_InteractiveObject.hxx>
#include <Bnd_Box.hxx>
#include <TopoDS_Shape.hxx>

#include <QVector>
#include <QWidget>

#include <functional>

class OccView;
class QButtonGroup;
class QComboBox;
class QLabel;
class QListWidget;

class MeasureTool : public QWidget {
    Q_OBJECT
public:
    enum Mode { Distance, PointToPoint, Dimension, Angle, Size };
    // Looks up a displayed part: its name and its box in the assembly.
    using PartInfo = std::function<bool(const Handle(AIS_InteractiveObject)&, QString&, Bnd_Box&)>;

    MeasureTool(OccView* view, QWidget* parent);

    // A new file: the parts that can be picked, the model's overall box.
    void setModel(const QVector<Handle(AIS_InteractiveObject)>& parts, const Bnd_Box& bounds, PartInfo info);
    void setActive(bool on);
    bool isActive() const { return active_; }
    void setMode(Mode mode);
    // A part was just displayed again: give it the picking behaviour of the current mode.
    void partShown(const Handle(AIS_InteractiveObject)& part);
    // The view's selection changed while the tool is active.
    void pick();
    // Removes every measurement (the geometry moved, or a new file).
    void clear();
    // Drops a half-done two-pick measurement. Returns false if there was none.
    bool cancelPending();
    void placeLabels();
    QString text() const;
    QVector<QLabel*> labels() const;

private:
    struct Item {
        Mode mode;
        TopoDS_Shape a, b;
        Bnd_Box box;
        QString name;  // part name, for Size
        QVector<Handle(AIS_InteractiveObject)> objects;
        gp_Pnt anchor;
        QString summary, details;
        QLabel* label = nullptr;
    };

    void applyMode(const Handle(AIS_InteractiveObject)& part);
    void applyModeToAll();
    Handle(AIS_InteractiveObject) mark(const TopoDS_Shape& s, bool shaded);
    void add(Mode mode, const TopoDS_Shape& a, const TopoDS_Shape& b, const Bnd_Box& box, const QString& name);
    bool describe(Item& item, bool* hasLine, gp_Pnt* from, gp_Pnt* to) const;
    void removeAt(int index);
    void dropPending();
    void refreshList();
    void updateHint();

    OccView* view_;
    bool active_ = false;
    Mode mode_ = Distance;
    QVector<Handle(AIS_InteractiveObject)> parts_;
    Bnd_Box bounds_;
    PartInfo info_;

    QVector<Item> items_;
    TopoDS_Shape pending_;                      // first pick of a two-pick measurement
    Handle(AIS_InteractiveObject) pendingMark_;

    QButtonGroup* modes_ = nullptr;
    QLabel* hint_ = nullptr;
    QComboBox* units_ = nullptr;
    QListWidget* list_ = nullptr;
    QWidget* wholeModel_ = nullptr;
};
