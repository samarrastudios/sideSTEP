// The view cube: a small cube that turns with the model. Click a face, an edge
// or a corner to look from there; the house below it returns to the home view.
#pragma once
#include <gp_Dir.hxx>

#include <QWidget>

class ViewCube : public QWidget {
    Q_OBJECT
public:
    explicit ViewCube(QWidget* parent);
    // The camera's attitude: direction from the model toward the eye, and up.
    void setCamera(const gp_Dir& towardEye, const gp_Dir& up);
    void setLight(bool light);

signals:
    void lookFrom(const gp_Dir& towardEye, const gp_Dir& up);
    void home();

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    struct Hit { bool valid = false; int x = 0, y = 0, z = 0; };  // which faces a region belongs to (-1, 0, +1 per axis)
    Hit hitTest(const QPointF& p) const;
    QPointF project(double x, double y, double z) const;
    QRectF homeRect() const;

    double right_[3] = {1, 0, 0}, up_[3] = {0, 0, 1}, back_[3] = {0, -1, 0};
    Hit hover_;
    bool hoverHome_ = false;
    bool light_ = true;
};
