#include "view_cube.h"

#include <gp_Vec.hxx>

#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>

#include <algorithm>
#include <cmath>

namespace {

const double kHalf = 33.0;   // half the cube's side, in pixels
const double kBand = 0.30;   // how much of a face's half-width belongs to its edges and corners
const QPointF kCentre(80, 78);

struct Face { int axis; int sign; const char* label; };
const Face kFaces[] = {{1, -1, "FRONT"}, {1, 1, "BACK"}, {0, -1, "LEFT"}, {0, 1, "RIGHT"}, {2, 1, "TOP"}, {2, -1, "BOTTOM"}};

// The two in-plane axes of a face, chosen so that text reads upright from outside.
void FaceAxes(const Face& f, double u[3], double v[3]) {
    const double n[3] = {f.axis == 0 ? (double)f.sign : 0, f.axis == 1 ? (double)f.sign : 0, f.axis == 2 ? (double)f.sign : 0};
    double up[3] = {0, 0, 1};
    if (f.axis == 2) { up[0] = 0; up[1] = f.sign > 0 ? 1 : -1; up[2] = 0; }  // top reads with the back edge up
    // u = up x n (text runs left to right seen from outside), v = up
    u[0] = up[1] * n[2] - up[2] * n[1];
    u[1] = up[2] * n[0] - up[0] * n[2];
    u[2] = up[0] * n[1] - up[1] * n[0];
    for (int i = 0; i < 3; ++i) v[i] = up[i];
}

}  // namespace

ViewCube::ViewCube(QWidget* parent) : QWidget(parent) {
    setFixedSize(150, 150);
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
    setToolTip(tr("Click a face, edge or corner to look from there. The house returns to the home view."));
}

void ViewCube::setLight(bool light) {
    light_ = light;
    update();
}

void ViewCube::setCamera(const gp_Dir& towardEye, const gp_Dir& up) {
    gp_Vec r = gp_Vec(up).Crossed(gp_Vec(towardEye));
    if (r.Magnitude() < 1e-9) return;
    r.Normalize();
    const gp_Vec u = gp_Vec(towardEye).Crossed(r);
    const double nr[3] = {r.X(), r.Y(), r.Z()}, nu[3] = {u.X(), u.Y(), u.Z()}, nb[3] = {towardEye.X(), towardEye.Y(), towardEye.Z()};
    bool changed = false;
    for (int i = 0; i < 3; ++i) changed |= std::abs(nr[i] - right_[i]) > 1e-4 || std::abs(nu[i] - up_[i]) > 1e-4;
    if (!changed) return;
    for (int i = 0; i < 3; ++i) { right_[i] = nr[i]; up_[i] = nu[i]; back_[i] = nb[i]; }
    update();
}

QPointF ViewCube::project(double x, double y, double z) const {
    return kCentre + QPointF(x * right_[0] + y * right_[1] + z * right_[2], -(x * up_[0] + y * up_[1] + z * up_[2]));
}

// The home button: a round chip at the cube's upper left.
QRectF ViewCube::homeRect() const { return QRectF(4, 4, 30, 30); }

// Each visible face is a 3 x 3 grid: the middle is the face, the rim cells
// are its edges and corners (shared with the neighbouring faces).
ViewCube::Hit ViewCube::hitTest(const QPointF& p) const {
    Hit best;
    for (const Face& f : kFaces) {
        const double facing = back_[f.axis] * f.sign;
        if (facing <= 0.02) continue;
        double u[3], v[3];
        FaceAxes(f, u, v);
        double n[3] = {0, 0, 0};
        n[f.axis] = f.sign;
        const QPointF o = project(n[0] * kHalf, n[1] * kHalf, n[2] * kHalf);
        const QPointF du = project(n[0] * kHalf + u[0] * kHalf, n[1] * kHalf + u[1] * kHalf, n[2] * kHalf + u[2] * kHalf) - o;
        const QPointF dv = project(n[0] * kHalf + v[0] * kHalf, n[1] * kHalf + v[1] * kHalf, n[2] * kHalf + v[2] * kHalf) - o;
        const double det = du.x() * dv.y() - du.y() * dv.x();
        if (std::abs(det) < 1e-6) continue;
        const QPointF d = p - o;
        const double a = (d.x() * dv.y() - d.y() * dv.x()) / det, b = (du.x() * d.y() - du.y() * d.x()) / det;
        if (std::abs(a) > 1 || std::abs(b) > 1) continue;
        const int ia = a > 1 - kBand ? 1 : a < -(1 - kBand) ? -1 : 0, ib = b > 1 - kBand ? 1 : b < -(1 - kBand) ? -1 : 0;
        double dir[3];
        for (int i = 0; i < 3; ++i) dir[i] = n[i] + ia * u[i] + ib * v[i];
        best.valid = true;
        best.x = (int)std::lround(dir[0]);
        best.y = (int)std::lround(dir[1]);
        best.z = (int)std::lround(dir[2]);
        return best;
    }
    return best;
}

void ViewCube::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor ink(0x11, 0x13, 0x16), paper(0xF3, 0xF1, 0xEA), orange(0xFF, 0x5B, 0x1F);
    const QColor text = light_ ? ink : paper;
    const QColor outline = light_ ? QColor(17, 19, 22, 120) : QColor(243, 241, 234, 150);
    const QColor seam = light_ ? QColor(17, 19, 22, 30) : QColor(243, 241, 234, 38);

    // A soft shadow on the "floor" under the cube, so it reads as an object sitting in the view.
    {
        const QPointF c = kCentre + QPointF(0, kHalf * 1.72);
        QRadialGradient g(c, 46);
        g.setColorAt(0, QColor(0, 0, 0, light_ ? 46 : 110));
        g.setColorAt(1, QColor(0, 0, 0, 0));
        p.save();
        p.translate(c);
        p.scale(1.0, 0.26);
        p.translate(-c);
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawEllipse(c, 46, 46);
        p.restore();
    }

    QFont font = p.font();
    font.setPixelSize(11);
    font.setWeight(QFont::DemiBold);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);

    for (const Face& f : kFaces) {
        const double facing = back_[f.axis] * f.sign;
        if (facing <= 0.02) continue;
        double u[3], v[3], n[3] = {0, 0, 0};
        FaceAxes(f, u, v);
        n[f.axis] = f.sign;
        auto corner = [&](double a, double b) {
            return project((n[0] + a * u[0] + b * v[0]) * kHalf, (n[1] + a * u[1] + b * v[1]) * kHalf, (n[2] + a * u[2] + b * v[2]) * kHalf);
        };
        QPolygonF quad;
        quad << corner(-1, 1) << corner(1, 1) << corner(1, -1) << corner(-1, -1);

        // Lit from above and from the front left, like the model: the top is
        // brightest, the sides step down in tone.
        const double lightDir[3] = {-0.38, -0.50, 0.78};
        const double lit = std::max(0.0, n[0] * lightDir[0] + n[1] * lightDir[1] + n[2] * lightDir[2]);
        const int tone = light_ ? (int)(226 + 29 * lit) : (int)(46 + 44 * lit);
        const QColor fill = light_ ? QColor(tone, tone, tone - 2) : QColor(tone, tone + 3, tone + 9);
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawPolygon(quad);

        // The region under the pointer.
        if (hover_.valid) {
            const int h[3] = {hover_.x, hover_.y, hover_.z};
            if (h[f.axis] == f.sign) {
                double a0 = -1, a1 = 1, b0 = -1, b1 = 1;
                const double edge = 1 - kBand;
                const double ha = h[0] * u[0] + h[1] * u[1] + h[2] * u[2], hb = h[0] * v[0] + h[1] * v[1] + h[2] * v[2];
                if (ha > 0.5) a0 = edge; else if (ha < -0.5) a1 = -edge; else { a0 = -edge; a1 = edge; }
                if (hb > 0.5) b0 = edge; else if (hb < -0.5) b1 = -edge; else { b0 = -edge; b1 = edge; }
                QPolygonF cell;
                cell << corner(a0, b1) << corner(a1, b1) << corner(a1, b0) << corner(a0, b0);
                p.setBrush(orange);
                p.drawPolygon(cell);
            }
        }

        // Faint seams mark where the edge and corner zones are; then the outline.
        const double e = 1 - kBand;
        p.setPen(QPen(seam, 1.0));
        p.drawLine(corner(-e, 1), corner(-e, -1));
        p.drawLine(corner(e, 1), corner(e, -1));
        p.drawLine(corner(-1, e), corner(1, e));
        p.drawLine(corner(-1, -e), corner(1, -e));
        p.setPen(QPen(outline, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPolygon(quad);

        // The label, mapped onto the face.
        if (facing > 0.28) {
            QPolygonF unit;
            unit << QPointF(-33, -33) << QPointF(33, -33) << QPointF(33, 33) << QPointF(-33, 33);
            QTransform t;
            if (QTransform::quadToQuad(unit, quad, t)) {
                p.save();
                p.setTransform(t, true);
                p.setFont(font);
                const bool hot = hover_.valid && std::abs(hover_.x) + std::abs(hover_.y) + std::abs(hover_.z) == 1 &&
                                 (f.axis == 0 ? hover_.x : f.axis == 1 ? hover_.y : hover_.z) == f.sign;
                QColor c = hot ? ink : text;
                c.setAlphaF(std::min(1.0, 0.30 + facing));
                // As an outline, not as text: text drawn through a skewed
                // transform picks up coloured fringes from sub-pixel smoothing.
                const QString label = QString::fromLatin1(f.label);
                const QFontMetricsF metrics(font);
                QPainterPath glyphs;
                glyphs.addText(-metrics.horizontalAdvance(label) / 2.0, metrics.capHeight() / 2.0, font, label);
                p.setPen(Qt::NoPen);
                p.setBrush(c);
                p.drawPath(glyphs);
                p.restore();
            }
        }
    }

    // Home: a round chip with a solid little house.
    const QRectF h = homeRect();
    const QPointF c = h.center();
    p.setPen(QPen(hoverHome_ ? orange : (light_ ? QColor(17, 19, 22, 46) : QColor(243, 241, 234, 50)), 1.0));
    p.setBrush(hoverHome_ ? orange : (light_ ? QColor(255, 255, 255, 240) : QColor(30, 33, 39, 240)));
    p.drawEllipse(h.adjusted(0.5, 0.5, -0.5, -0.5));
    QPainterPath house;
    house.setFillRule(Qt::OddEvenFill);
    house.moveTo(c + QPointF(-8.5, -0.5));   // roof, with a little overhang
    house.lineTo(c + QPointF(0, -8));
    house.lineTo(c + QPointF(8.5, -0.5));
    house.lineTo(c + QPointF(6, -0.5));
    house.lineTo(c + QPointF(6, 7));         // walls
    house.lineTo(c + QPointF(-6, 7));
    house.lineTo(c + QPointF(-6, -0.5));
    house.closeSubpath();
    house.addRect(QRectF(c + QPointF(-1.8, 2), QSizeF(3.6, 5)));  // the door, cut out
    p.setPen(Qt::NoPen);
    p.setBrush(hoverHome_ ? ink : text);
    p.drawPath(house);
}

void ViewCube::mouseMoveEvent(QMouseEvent* e) {
    const bool home = homeRect().contains(e->position());
    const Hit h = home ? Hit() : hitTest(e->position());
    if (h.valid != hover_.valid || h.x != hover_.x || h.y != hover_.y || h.z != hover_.z || home != hoverHome_) {
        hover_ = h;
        hoverHome_ = home;
        update();
    }
}

void ViewCube::leaveEvent(QEvent*) {
    hover_ = Hit();
    hoverHome_ = false;
    update();
}

void ViewCube::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    if (homeRect().contains(e->position())) { emit home(); return; }
    const Hit h = hitTest(e->position());
    if (!h.valid) return;
    const gp_Dir dir(h.x, h.y, h.z);
    // Looking straight down or up: keep the front of the model toward the bottom of the screen.
    const bool vertical = h.x == 0 && h.y == 0;
    emit lookFrom(dir, vertical ? gp_Dir(0, h.z > 0 ? 1 : -1, 0) : gp_Dir(0, 0, 1));
}
