#include "measure.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <GProp_GProps.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS.hxx>
#include <gp_Circ.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>

#include <cmath>

namespace {

const double kPi = 3.14159265358979323846;
LengthUnits g_units = LengthUnits::Both;

// What a picked entity offers beyond its own extent: a direction (line,
// plane normal, axis) and, for round things, an axis through a centre.
struct Feature {
    bool hasDir = false, isNormal = false, hasAxis = false;
    gp_Dir dir;
    gp_Pnt centre;
};

Feature FeatureOf(const TopoDS_Shape& s) {
    Feature f;
    try {
        if (s.ShapeType() == TopAbs_EDGE) {
            BRepAdaptor_Curve c(TopoDS::Edge(s));
            if (c.GetType() == GeomAbs_Line) {
                f.hasDir = true;
                f.dir = c.Line().Direction();
            } else if (c.GetType() == GeomAbs_Circle) {
                f.hasDir = f.hasAxis = true;
                f.dir = c.Circle().Axis().Direction();
                f.centre = c.Circle().Location();
            }
        } else if (s.ShapeType() == TopAbs_FACE) {
            BRepAdaptor_Surface a(TopoDS::Face(s));
            if (a.GetType() == GeomAbs_Plane) {
                f.hasDir = f.isNormal = true;
                f.dir = a.Plane().Axis().Direction();
            } else if (a.GetType() == GeomAbs_Cylinder) {
                f.hasDir = f.hasAxis = true;
                f.dir = a.Cylinder().Axis().Direction();
                f.centre = a.Cylinder().Location();
            }
        }
    } catch (const Standard_Failure&) {
    }
    return f;
}

QString Mm(double mm) { return QStringLiteral("%1 mm").arg(mm, 0, 'f', 2); }
QString In(double mm) { return QStringLiteral("%1 in").arg(mm / 25.4, 0, 'f', 3); }

// Angle between two features, in degrees, folded into 0..90. A plane's normal
// against a line's direction is 90 degrees off the angle people mean.
double AngleBetween(const Feature& a, const Feature& b) {
    double angle = a.dir.Angle(b.dir) * 180.0 / kPi;
    if (angle > 90.0) angle = 180.0 - angle;
    const bool planeA = a.isNormal, planeB = b.isNormal;
    if (planeA != planeB) angle = 90.0 - angle;
    return angle;
}

gp_Pnt Centre(const TopoDS_Shape& s) {
    GProp_GProps props;
    if (s.ShapeType() == TopAbs_FACE) BRepGProp::SurfaceProperties(s, props);
    else BRepGProp::LinearProperties(s, props);
    return props.CentreOfMass();
}

}  // namespace

void SetLengthUnits(LengthUnits units) { g_units = units; }
LengthUnits GetLengthUnits() { return g_units; }

QString FormatLength(double mm) {
    switch (g_units) {
    case LengthUnits::Millimetres: return Mm(mm);
    case LengthUnits::Inches: return In(mm);
    default: return QStringLiteral("%1 (%2)").arg(Mm(mm), In(mm));
    }
}

QString FormatLengthShort(double mm) { return g_units == LengthUnits::Inches ? In(mm) : Mm(mm); }

QString FormatArea(double mm2) {
    const QString metric = QStringLiteral("%1 mm²").arg(mm2, 0, 'f', 1);
    const QString imperial = QStringLiteral("%1 in²").arg(mm2 / 645.16, 0, 'f', 2);
    switch (g_units) {
    case LengthUnits::Millimetres: return metric;
    case LengthUnits::Inches: return imperial;
    default: return QStringLiteral("%1 (%2)").arg(metric, imperial);
    }
}

QString DescribeEntity(const TopoDS_Shape& s) {
    try {
        switch (s.ShapeType()) {
        case TopAbs_VERTEX: {
            const gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(s));
            return QStringLiteral("Point  X %1  Y %2  Z %3").arg(FormatLengthShort(p.X()), FormatLengthShort(p.Y()), FormatLengthShort(p.Z()));
        }
        case TopAbs_EDGE: {
            BRepAdaptor_Curve c(TopoDS::Edge(s));
            const double len = GCPnts_AbscissaPoint::Length(c);
            if (c.GetType() == GeomAbs_Line) return QStringLiteral("Straight edge, length %1").arg(FormatLength(len));
            if (c.GetType() == GeomAbs_Circle) {
                const double r = c.Circle().Radius();
                const bool full = std::abs(std::abs(c.LastParameter() - c.FirstParameter()) - 2 * kPi) < 1e-6;
                return full ? QStringLiteral("Circle, diameter %1").arg(FormatLength(2 * r))
                            : QStringLiteral("Arc, radius %1").arg(FormatLength(r));
            }
            return QStringLiteral("Curved edge, length %1").arg(FormatLength(len));
        }
        case TopAbs_FACE: {
            BRepAdaptor_Surface a(TopoDS::Face(s));
            if (a.GetType() == GeomAbs_Cylinder)
                return QStringLiteral("Cylindrical face, diameter %1").arg(FormatLength(2 * a.Cylinder().Radius()));
            GProp_GProps props;
            BRepGProp::SurfaceProperties(s, props);
            return QStringLiteral("%1 face, area %2").arg(a.GetType() == GeomAbs_Plane ? "Flat" : "Curved", FormatArea(props.Mass()));
        }
        default:
            break;
        }
    } catch (const Standard_Failure&) {
    }
    return QStringLiteral("Selected");
}

MeasureResult MeasureDistance(const TopoDS_Shape& first, const TopoDS_Shape& second) {
    MeasureResult r;
    r.details = QStringLiteral("These two could not be measured. Try other picks.");
    try {
        BRepExtrema_DistShapeShape ext(first, second);
        if (!ext.IsDone() || ext.NbSolution() < 1) return r;
        r.ok = true;
        r.a = ext.PointOnShape1(1);
        r.b = ext.PointOnShape2(1);
        r.hasLine = r.a.Distance(r.b) > 1e-7;
        r.anchor = gp_Pnt((r.a.XYZ() + r.b.XYZ()) / 2.0);
        const double dist = ext.Value();
        r.summary = FormatLengthShort(dist);
        QStringList lines;
        lines << QStringLiteral("Distance %1").arg(FormatLength(dist));
        lines << QStringLiteral("ΔX %1   ΔY %2   ΔZ %3")
                     .arg(FormatLengthShort(std::abs(r.b.X() - r.a.X())), FormatLengthShort(std::abs(r.b.Y() - r.a.Y())),
                          FormatLengthShort(std::abs(r.b.Z() - r.a.Z())));

        const Feature fa = FeatureOf(first), fb = FeatureOf(second);
        if (fa.hasDir && fb.hasDir) {
            const double angle = AngleBetween(fa, fb);
            const bool parallelAxes = fa.hasAxis && fb.hasAxis && fa.dir.IsParallel(fb.dir, 1e-4);
            if (parallelAxes) {
                // Hole to hole: distance between the two axes.
                const double centres = gp_Lin(fa.centre, fa.dir).Distance(fb.centre);
                lines.prepend(QStringLiteral("Centre to centre %1").arg(FormatLength(centres)));
                r.summary = QStringLiteral("%1 c-c").arg(FormatLengthShort(centres));
            } else if (angle > 0.01) {
                lines << QStringLiteral("Angle %1°").arg(angle, 0, 'f', 2);
            }
        }
        lines << QStringLiteral("From: %1").arg(DescribeEntity(first)) << QStringLiteral("To: %1").arg(DescribeEntity(second));
        r.details = lines.join(QChar(u'\n'));
    } catch (const Standard_Failure&) {
        r.ok = false;
    }
    return r;
}

MeasureResult MeasureDimension(const TopoDS_Shape& s) {
    MeasureResult r;
    r.details = QStringLiteral("Nothing to measure on that pick.");
    try {
        switch (s.ShapeType()) {
        case TopAbs_VERTEX: {
            r.anchor = BRep_Tool::Pnt(TopoDS::Vertex(s));
            r.summary = QStringLiteral("%1, %2, %3").arg(r.anchor.X(), 0, 'f', 1).arg(r.anchor.Y(), 0, 'f', 1).arg(r.anchor.Z(), 0, 'f', 1);
            r.details = DescribeEntity(s);
            r.ok = true;
            break;
        }
        case TopAbs_EDGE: {
            BRepAdaptor_Curve c(TopoDS::Edge(s));
            const double len = GCPnts_AbscissaPoint::Length(c);
            r.anchor = c.Value(0.5 * (c.FirstParameter() + c.LastParameter()));
            if (c.GetType() == GeomAbs_Circle) {
                const double radius = c.Circle().Radius();
                const bool full = std::abs(std::abs(c.LastParameter() - c.FirstParameter()) - 2 * kPi) < 1e-6;
                r.summary = full ? QStringLiteral("Ø %1").arg(FormatLengthShort(2 * radius))
                                 : QStringLiteral("R %1").arg(FormatLengthShort(radius));
                r.details = QStringLiteral("%1\nDiameter %2\nRadius %3\n%4 %5")
                                .arg(full ? "Circle" : "Arc", FormatLength(2 * radius), FormatLength(radius),
                                     full ? "Circumference" : "Arc length", FormatLength(len));
            } else {
                r.summary = FormatLengthShort(len);
                r.details = QStringLiteral("%1\nLength %2").arg(c.GetType() == GeomAbs_Line ? "Straight edge" : "Curved edge", FormatLength(len));
            }
            r.ok = true;
            break;
        }
        case TopAbs_FACE: {
            BRepAdaptor_Surface a(TopoDS::Face(s));
            GProp_GProps props;
            BRepGProp::SurfaceProperties(s, props);
            r.anchor = props.CentreOfMass();
            if (a.GetType() == GeomAbs_Cylinder) {
                const double radius = a.Cylinder().Radius();
                r.summary = QStringLiteral("Ø %1").arg(FormatLengthShort(2 * radius));
                r.details = QStringLiteral("Cylindrical face\nDiameter %1\nRadius %2\nArea %3")
                                .arg(FormatLength(2 * radius), FormatLength(radius), FormatArea(props.Mass()));
            } else {
                r.summary = FormatArea(props.Mass()).section(QStringLiteral(" ("), 0, 0);
                r.details = QStringLiteral("%1 face\nArea %2").arg(a.GetType() == GeomAbs_Plane ? "Flat" : "Curved", FormatArea(props.Mass()));
            }
            r.ok = true;
            break;
        }
        default:
            break;
        }
    } catch (const Standard_Failure&) {
        r.ok = false;
    }
    return r;
}

MeasureResult MeasureAngle(const TopoDS_Shape& first, const TopoDS_Shape& second) {
    MeasureResult r;
    const Feature fa = FeatureOf(first), fb = FeatureOf(second);
    if (!fa.hasDir || !fb.hasDir) {
        r.details = QStringLiteral("An angle needs straight edges, flat faces or round holes. Try other picks.");
        return r;
    }
    try {
        const double angle = AngleBetween(fa, fb);
        r.ok = true;
        r.anchor = gp_Pnt((Centre(first).XYZ() + Centre(second).XYZ()) / 2.0);
        r.a = Centre(first);
        r.b = Centre(second);
        r.hasLine = r.a.Distance(r.b) > 1e-7;
        r.summary = QStringLiteral("%1°").arg(angle, 0, 'f', 2);
        r.details = QStringLiteral("Angle %1°  (supplement %2°)\nFrom: %3\nTo: %4")
                        .arg(angle, 0, 'f', 2).arg(180.0 - angle, 0, 'f', 2).arg(DescribeEntity(first), DescribeEntity(second));
    } catch (const Standard_Failure&) {
        r.ok = false;
        r.details = QStringLiteral("These two could not be measured. Try other picks.");
    }
    return r;
}

MeasureResult MeasureBox(const Bnd_Box& box) {
    MeasureResult r;
    if (box.IsVoid()) { r.details = QStringLiteral("Nothing to measure."); return r; }
    double x0, y0, z0, x1, y1, z1;
    box.Get(x0, y0, z0, x1, y1, z1);
    const double dx = x1 - x0, dy = y1 - y0, dz = z1 - z0;
    r.ok = true;
    r.anchor = gp_Pnt(x1, y0, z1);
    auto bare = [](double mm) { return GetLengthUnits() == LengthUnits::Inches ? QString::number(mm / 25.4, 'f', 2) : QString::number(mm, 'f', 1); };
    r.summary = QStringLiteral("%1 x %2 x %3 %4").arg(bare(dx), bare(dy), bare(dz), GetLengthUnits() == LengthUnits::Inches ? "in" : "mm");
    r.details = QStringLiteral("X %1\nY %2\nZ %3").arg(FormatLength(dx), FormatLength(dy), FormatLength(dz));
    return r;
}
