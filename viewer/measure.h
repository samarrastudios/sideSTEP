// Measurements on picked vertices, edges and faces. Geometry is in millimetres
// (OpenCASCADE converts STEP units on import); results are formatted in the
// units the user chose.
#pragma once
#include <Bnd_Box.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>

#include <QString>
#include <QStringList>

enum class LengthUnits { Millimetres, Inches, Both };
void SetLengthUnits(LengthUnits units);
LengthUnits GetLengthUnits();

// "123.45 mm", "4.860 in" or "123.45 mm (4.860 in)", following the setting.
QString FormatLength(double mm);
// One unit only (millimetres unless the setting is inches); for labels in the view.
QString FormatLengthShort(double mm);
QString FormatArea(double mm2);

// One line about a single picked entity: type and its key dimension.
QString DescribeEntity(const TopoDS_Shape& s);

struct MeasureResult {
    bool ok = false;
    QString summary;       // short, for the label in the view
    QString details;       // full readout, or the reason it failed
    gp_Pnt anchor;         // where the label goes
    bool hasLine = false;  // draw a line from a to b
    gp_Pnt a, b;
};

// Shortest distance between two entities, with deltas, angle and hole
// centre-to-centre where they apply.
MeasureResult MeasureDistance(const TopoDS_Shape& first, const TopoDS_Shape& second);
// The entity's own size: edge length, circle or cylinder diameter, arc radius, face area.
MeasureResult MeasureDimension(const TopoDS_Shape& s);
// Angle between two straight edges and/or flat faces (or the axes of round ones).
MeasureResult MeasureAngle(const TopoDS_Shape& first, const TopoDS_Shape& second);
// Overall size of a box along X, Y and Z.
MeasureResult MeasureBox(const Bnd_Box& box);
