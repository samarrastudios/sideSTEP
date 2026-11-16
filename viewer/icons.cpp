#include "icons.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace {

// Everything is drawn on a 24 x 24 grid.
QPolygonF Poly(std::initializer_list<QPointF> pts) { return QPolygonF(QList<QPointF>(pts)); }

// An isometric cube: outline plus the three inner edges.
void Cube(QPainter& p, int filledFace = -1) {
    const QPointF top(12, 3), tr(20, 7.5), br(20, 16.5), bottom(12, 21), bl(4, 16.5), tl(4, 7.5), mid(12, 12);
    if (filledFace >= 0) {
        const QPolygonF faces[] = {Poly({top, tr, mid, tl}), Poly({tl, mid, bottom, bl}), Poly({mid, tr, br, bottom})};
        p.save();
        p.setBrush(p.pen().color());
        p.setOpacity(0.55);
        p.setPen(Qt::NoPen);
        p.drawPolygon(faces[filledFace]);
        p.restore();
    }
    p.drawPolygon(Poly({top, tr, br, bottom, bl, tl}));
    p.drawLine(mid, tl);
    p.drawLine(mid, tr);
    p.drawLine(mid, bottom);
}

void Eye(QPainter& p) {
    QPainterPath path;
    path.moveTo(3, 12);
    path.quadTo(12, 3, 21, 12);
    path.quadTo(12, 21, 3, 12);
    p.drawPath(path);
    p.drawEllipse(QPointF(12, 12), 2.6, 2.6);
}

void Arrow(QPainter& p, QPointF from, QPointF to, double head = 3.2) {
    p.drawLine(from, to);
    const QLineF line(to, from);
    QLineF a = line, b = line;
    a.setLength(head);
    b.setLength(head);
    a.setAngle(line.angle() + 32);
    b.setAngle(line.angle() - 32);
    p.drawLine(a);
    p.drawLine(b);
}

void Draw(QPainter& p, Icon icon) {
    switch (icon) {
    case Icon::Open: {
        QPainterPath path;
        path.moveTo(3, 19);
        path.lineTo(3, 5.5);
        path.lineTo(9, 5.5);
        path.lineTo(11, 8);
        path.lineTo(21, 8);
        path.lineTo(21, 19);
        path.closeSubpath();
        p.drawPath(path);
        break;
    }
    case Icon::Previous:
        p.drawPolyline(Poly({{14.5, 5.5}, {8, 12}, {14.5, 18.5}}));
        break;
    case Icon::Next:
        p.drawPolyline(Poly({{9.5, 5.5}, {16, 12}, {9.5, 18.5}}));
        break;
    case Icon::Fit:
        p.drawPolyline(Poly({{3.5, 8}, {3.5, 3.5}, {8, 3.5}}));
        p.drawPolyline(Poly({{16, 3.5}, {20.5, 3.5}, {20.5, 8}}));
        p.drawPolyline(Poly({{20.5, 16}, {20.5, 20.5}, {16, 20.5}}));
        p.drawPolyline(Poly({{8, 20.5}, {3.5, 20.5}, {3.5, 16}}));
        p.drawRect(QRectF(8.5, 8.5, 7, 7));
        break;
    case Icon::Views:
        Cube(p, 0);
        break;
    case Icon::Display:
        Cube(p, 2);
        break;
    case Icon::Measure: {
        // A ruler lying at an angle.
        p.save();
        p.translate(12, 12);
        p.rotate(-35);
        p.drawRect(QRectF(-10.5, -3.5, 21, 7));
        for (int i = -2; i <= 2; ++i) p.drawLine(QPointF(i * 3.5, -3.5), QPointF(i * 3.5, i % 2 ? -1.2 : 0.3));
        p.restore();
        break;
    }
    case Icon::Section: {
        // Half a cube with its cut face filled.
        const QPointF top(12, 3), bottom(12, 21), bl(4, 16.5), tl(4, 7.5);
        p.drawPolyline(Poly({top, tl, bl, bottom}));
        const QPolygonF cut = Poly({top, {17, 7.2}, {17, 20}, bottom});
        p.save();
        p.setBrush(p.pen().color());
        p.setOpacity(0.55);
        p.setPen(Qt::NoPen);
        p.drawPolygon(cut);
        p.restore();
        p.drawPolygon(cut);
        p.drawLine(QPointF(4, 7.5), QPointF(9, 10.3));
        break;
    }
    case Icon::SectionBox: {
        // A cube with a dashed box cut out of its near corner.
        Cube(p);
        p.save();
        QPen dashed = p.pen();
        dashed.setDashPattern({2.0, 1.6});
        p.setPen(dashed);
        p.drawPolyline(Poly({{12, 12}, {12, 16.5}}));
        p.drawPolyline(Poly({{8, 9.8}, {8, 14.3}, {12, 16.5}, {16, 14.3}, {16, 9.8}}));
        p.restore();
        break;
    }
    case Icon::Parts:
        // A list: bullets and lines.
        for (int i = 0; i < 3; ++i) {
            const double y = 6.5 + i * 5.5;
            p.drawEllipse(QPointF(5, y), 1.1, 1.1);
            p.drawLine(QPointF(9, y), QPointF(20, y));
        }
        break;
    case Icon::Snapshot: {
        QPainterPath body;
        body.addRoundedRect(QRectF(3, 7, 18, 12.5), 2, 2);
        p.drawPath(body);
        p.drawPolyline(Poly({{8.5, 7}, {10, 4.5}, {14, 4.5}, {15.5, 7}}));
        p.drawEllipse(QPointF(12, 13.2), 3.4, 3.4);
        break;
    }
    case Icon::FullScreen:
        p.drawPolyline(Poly({{4, 9}, {4, 4}, {9, 4}}));
        p.drawPolyline(Poly({{15, 4}, {20, 4}, {20, 9}}));
        p.drawPolyline(Poly({{20, 15}, {20, 20}, {15, 20}}));
        p.drawPolyline(Poly({{9, 20}, {4, 20}, {4, 15}}));
        break;
    case Icon::Explode:
        p.drawRect(QRectF(9.5, 9.5, 5, 5));
        Arrow(p, {8, 8}, {3.5, 3.5});
        Arrow(p, {16, 8}, {20.5, 3.5});
        Arrow(p, {16, 16}, {20.5, 20.5});
        Arrow(p, {8, 16}, {3.5, 20.5});
        break;
    case Icon::Hide:
        Eye(p);
        p.drawLine(QPointF(5, 19.5), QPointF(19, 4.5));
        break;
    case Icon::ShowAll:
        Eye(p);
        break;
    case Icon::Isolate:
        p.drawEllipse(QPointF(12, 12), 6.5, 6.5);
        p.save();
        p.setBrush(p.pen().color());
        p.drawEllipse(QPointF(12, 12), 2.2, 2.2);
        p.restore();
        p.drawLine(QPointF(12, 2.5), QPointF(12, 5.5));
        p.drawLine(QPointF(12, 18.5), QPointF(12, 21.5));
        p.drawLine(QPointF(2.5, 12), QPointF(5.5, 12));
        p.drawLine(QPointF(18.5, 12), QPointF(21.5, 12));
        break;
    case Icon::OpenIn:
        p.drawPolyline(Poly({{10, 5}, {4.5, 5}, {4.5, 19.5}, {19, 19.5}, {19, 14}}));
        Arrow(p, {11, 13}, {20, 4}, 5.5);
        break;
    }
}

QPixmap Render(Icon icon, const QColor& color, int scale) {
    QPixmap pm(24 * scale, 24 * scale);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(scale, scale);
    p.setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    Draw(p, icon);
    p.end();
    pm.setDevicePixelRatio(scale);
    return pm;
}

}  // namespace

QIcon MakeIcon(Icon icon, const QColor& normal, const QColor& checked) {
    const QColor dim(0x8A, 0x8C, 0x90);
    QIcon out;
    for (int scale : {1, 2, 3}) {
        out.addPixmap(Render(icon, normal, scale), QIcon::Normal, QIcon::Off);
        out.addPixmap(Render(icon, checked, scale), QIcon::Normal, QIcon::On);
        out.addPixmap(Render(icon, dim, scale), QIcon::Disabled, QIcon::Off);
        out.addPixmap(Render(icon, dim, scale), QIcon::Disabled, QIcon::On);
    }
    return out;
}

QIcon MakeIcon(Icon icon) { return MakeIcon(icon, QColor(0xF3, 0xF1, 0xEA), QColor(0x11, 0x13, 0x16)); }
