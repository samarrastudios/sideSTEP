#include "open_with.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QProcess>
#include <QUrl>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>

namespace {

template <class T>
struct ComPtr {
    T* p = nullptr;
    ~ComPtr() { if (p) p->Release(); }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

QString TakeString(LPWSTR s) {
    QString r = s ? QString::fromWCharArray(s) : QString();
    if (s) CoTaskMemFree(s);
    return r;
}

std::wstring Ext(const QString& filePath) {
    return (QStringLiteral(".") + QFileInfo(filePath).suffix()).toStdWString();
}

bool IsSelf(const QString& id) {
    return QFileInfo(id).fileName().compare(QFileInfo(QCoreApplication::applicationFilePath()).fileName(),
                                            Qt::CaseInsensitive) == 0;
}

// CAD and 3D programs worth offering even when Windows does not list them as
// recommended for STEP files (it stops recommending the previous default once
// sideSTEP takes over, for one).
bool IsKnownCadApp(const QString& id) {
    static const char* const names[] = {
        "freecad", "fusion360", "fusionlauncher", "sldworks", "edrawings", "rhino", "inventor", "acad", "edge",
        "cnext", "xtop", "ugraf", "plasticity", "moi", "shapr3d", "alibre", "ironcad", "zw3d", "bricscad",
        "designspark", "cadassistant", "keyshot", "blender", "f3d", "prusa-slicer", "bambu-studio", "orca-slicer",
        "orcaslicer", "ultimaker-cura", "cura"};
    const QString base = QFileInfo(id).completeBaseName().toLower();
    for (const char* n : names)
        if (base == QLatin1String(n)) return true;
    return false;
}

void AddApps(QList<ExternalApp>& apps, const QString& filePath, ASSOC_FILTER filter, bool knownOnly) {
    ComPtr<IEnumAssocHandlers> en;
    if (FAILED(SHAssocEnumHandlers(Ext(filePath).c_str(), filter, &en))) return;
    QFileIconProvider icons;
    for (;;) {
        ComPtr<IAssocHandler> h;
        ULONG got = 0;
        if (en->Next(1, &h, &got) != S_OK || got == 0) break;
        LPWSTR s = nullptr;
        ExternalApp app;
        if (SUCCEEDED(h->GetName(&s))) app.id = TakeString(s);
        s = nullptr;
        if (SUCCEEDED(h->GetUIName(&s))) app.name = TakeString(s);
        if (app.id.isEmpty() || IsSelf(app.id) || (knownOnly && !IsKnownCadApp(app.id))) continue;
        bool seen = false;
        for (const ExternalApp& a : apps) seen |= a.id.compare(app.id, Qt::CaseInsensitive) == 0;
        if (seen) continue;
        // Some programs register no display name and Windows hands back the path.
        if (app.name.isEmpty() || app.name.contains(QLatin1Char(':')) || app.name.contains(QDir::separator()))
            app.name = ExternalAppNameFromExe(app.id);
        if (QFileInfo::exists(app.id)) app.icon = icons.icon(QFileInfo(app.id));
        apps.push_back(app);
    }
}

}  // namespace

QList<ExternalApp> ListExternalApps(const QString& filePath) {
    QList<ExternalApp> apps;
    AddApps(apps, filePath, ASSOC_FILTER_RECOMMENDED, false);
    AddApps(apps, filePath, ASSOC_FILTER_NONE, true);
    return apps;
}

bool OpenInExternalApp(const QString& id, const QString& filePath) {
    const std::wstring native = QDir::toNativeSeparators(filePath).toStdWString();
    ComPtr<IEnumAssocHandlers> en;
    if (SUCCEEDED(SHAssocEnumHandlers(Ext(filePath).c_str(), ASSOC_FILTER_NONE, &en))) {
        for (;;) {
            ComPtr<IAssocHandler> h;
            ULONG got = 0;
            if (en->Next(1, &h, &got) != S_OK || got == 0) break;
            LPWSTR s = nullptr;
            if (FAILED(h->GetName(&s))) continue;
            if (TakeString(s).compare(id, Qt::CaseInsensitive) != 0) continue;
            ComPtr<IShellItem> item;
            ComPtr<IDataObject> data;
            if (SUCCEEDED(SHCreateItemFromParsingName(native.c_str(), nullptr, IID_PPV_ARGS(&item))) &&
                SUCCEEDED(item->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(&data))) &&
                SUCCEEDED(h->Invoke(data.p)))
                return true;
            break;
        }
    }
    if (QFileInfo(id).isExecutable())
        return QProcess::startDetached(id, {QDir::toNativeSeparators(filePath)});
    return false;
}

QString ExternalAppNameFromExe(const QString& exePath) {
    const std::wstring path = QDir::toNativeSeparators(exePath).toStdWString();
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (size) {
        QByteArray buf((int)size, 0);
        struct Lang { WORD language, codePage; }* langs = nullptr;
        UINT len = 0;
        if (GetFileVersionInfoW(path.c_str(), 0, size, buf.data()) &&
            VerQueryValueW(buf.data(), L"\\VarFileInfo\\Translation", (LPVOID*)&langs, &len) && len >= sizeof(Lang)) {
            wchar_t key[64];
            swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\FileDescription", langs[0].language, langs[0].codePage);
            wchar_t* text = nullptr;
            if (VerQueryValueW(buf.data(), key, (LPVOID*)&text, &len) && len > 1) {
                const QString name = QString::fromWCharArray(text).trimmed();
                if (!name.isEmpty()) return name;
            }
        }
    }
    return QFileInfo(exePath).completeBaseName();
}

void OpenDefaultAppsSettings() {
    QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:defaultapps?registeredAppUser=sideSTEP")));
}

void RevealInExplorer(const QString& filePath) {
    QProcess::startDetached(QStringLiteral("explorer.exe"),
                            {QStringLiteral("/select,"), QDir::toNativeSeparators(filePath)});
}
