// Toolbar icons, drawn in code so they stay crisp at any display scale and
// follow the brand palette (Paper lines; Ink on the orange "on" state).
#pragma once
#include <QIcon>

enum class Icon {
    Open, Previous, Next, Fit, Views, Display, Measure, Section, SectionBox, Explode, Hide, Isolate, ShowAll, OpenIn,
    Parts, Snapshot, FullScreen
};

// For the dark window chrome: Paper lines.
QIcon MakeIcon(Icon icon);
// For any surface: the line colour, and the colour on a checked (orange) button.
QIcon MakeIcon(Icon icon, const QColor& normal, const QColor& checked);
