// Section tools. Each is a strip of controls that sits in the pill at the top
// of the view while the tool is on.
//
// SectionTool cuts the model with one plane across X, Y or Z.
// SectionBoxTool keeps only what is inside a box, with a low and a high cut on each axis.
// Cut faces are filled in the part's own colour.
#pragma once
#include <Bnd_Box.hxx>
#include <Graphic3d_ClipPlane.hxx>

#include <QWidget>

class OccView;
class QButtonGroup;
class QSlider;

class SectionTool : public QWidget {
    Q_OBJECT
public:
    explicit SectionTool(OccView* view);

    void setBounds(const Bnd_Box& bounds);  // a new file
    void setActive(bool on);
    bool isActive() const { return active_; }
    void setAxis(int axis);             // 0 = X, 1 = Y, 2 = Z
    void setPosition(double fraction);  // 0..1 across the model

private:
    void apply();

    OccView* view_;
    Handle(Graphic3d_ClipPlane) plane_;
    Bnd_Box bounds_;
    bool active_ = false;
    int axis_ = 0;
    bool flipped_ = false;
    QButtonGroup* axes_ = nullptr;
    QSlider* slider_ = nullptr;
};

class SectionBoxTool : public QWidget {
    Q_OBJECT
public:
    explicit SectionBoxTool(OccView* view);

    void setBounds(const Bnd_Box& bounds);
    void setActive(bool on);
    bool isActive() const { return active_; }
    void setRange(int axis, double low, double high);  // fractions 0..1

private:
    void apply();
    void showAxis();

    OccView* view_;
    Handle(Graphic3d_ClipPlane) planes_[6];  // low and high for X, Y, Z
    Bnd_Box bounds_;
    bool active_ = false;
    // Starts around the whole model with the top taken off: something useful
    // shows on any shape (a box around the middle can be empty on an L-shaped one).
    double low_[3] = {0, 0, 0}, high_[3] = {1, 1, 0.6};
    int axis_ = 2;
    QButtonGroup* axes_ = nullptr;
    QSlider* lowSlider_ = nullptr;
    QSlider* highSlider_ = nullptr;
};
