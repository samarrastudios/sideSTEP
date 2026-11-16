// "Open in another app": lists the programs Windows knows for a file type and
// hands a file to one of them. Windows-only; kept out of the main window code.
#pragma once
#include <QIcon>
#include <QList>
#include <QString>

struct ExternalApp {
    QString name;  // shown to the user, e.g. "Autodesk Fusion"
    QString id;    // Windows' handler name: usually the full path of the exe
    QIcon icon;
};

// Programs registered for this file's extension, without sideSTEP itself.
QList<ExternalApp> ListExternalApps(const QString& filePath);

// Opens the file in the app with this id. Falls back to starting `id` as a
// program when Windows no longer lists it. Returns false if nothing started.
bool OpenInExternalApp(const QString& id, const QString& filePath);

// A display name for an exe the user browsed to.
QString ExternalAppNameFromExe(const QString& exePath);

// Opens Windows Settings on the page where the user picks default apps.
void OpenDefaultAppsSettings();

// Selects the file in a File Explorer window.
void RevealInExplorer(const QString& filePath);
