#include "file_assoc.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>

#include <string>

namespace {

// Registry paths are raw string literals so no backslash can be misread as an
// escape. All of them are relative to HKEY_CURRENT_USER.
const wchar_t* const kExtensions[] = {L".step", L".stp", L".p21"};
const wchar_t kProgId[] = L"sideSTEP.step";
const std::wstring kClasses = LR"(Software\Classes\)";
const std::wstring kProgIdKey = kClasses + kProgId;
const std::wstring kAppKey = kClasses + LR"(Applications\sideSTEP.exe)";
const std::wstring kCapabilities = LR"(Software\sideSTEP\Capabilities)";
const wchar_t kRegisteredApps[] = LR"(Software\RegisteredApplications)";
const wchar_t kOpenCommand[] = LR"(\shell\open\command)";

bool Set(const std::wstring& key, const wchar_t* name, const std::wstring& value) {
    return RegSetKeyValueW(HKEY_CURRENT_USER, key.c_str(), name, REG_SZ, value.c_str(),
                           (DWORD)((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

void DeleteTree(const std::wstring& key) {
    RegDeleteTreeW(HKEY_CURRENT_USER, key.c_str());
    RegDeleteKeyW(HKEY_CURRENT_USER, key.c_str());
}

}  // namespace

bool RegisterFileAssociation() {
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return false;
    const std::wstring exe = path;
    const std::wstring quoted = L'"' + exe + L'"';
    const std::wstring command = quoted + LR"( "%1")";
    bool ok = true;

    // The file type sideSTEP offers.
    ok &= Set(kProgIdKey, nullptr, L"STEP 3D model");
    ok &= Set(kProgIdKey + LR"(\DefaultIcon)", nullptr, quoted + L",0");
    ok &= Set(kProgIdKey + LR"(\shell\open)", L"FriendlyAppName", L"sideSTEP");
    ok &= Set(kProgIdKey + kOpenCommand, nullptr, command);

    // The program, for the "Open with" list.
    ok &= Set(kAppKey, L"FriendlyAppName", L"sideSTEP");
    ok &= Set(kAppKey + kOpenCommand, nullptr, command);

    // What it can open, for Settings > Default apps.
    ok &= Set(kCapabilities, L"ApplicationName", L"sideSTEP");
    ok &= Set(kCapabilities, L"ApplicationDescription", L"STEP files, seen without the CAD package.");
    ok &= Set(kRegisteredApps, L"sideSTEP", kCapabilities);

    for (const wchar_t* ext : kExtensions) {
        ok &= Set(kClasses + ext + LR"(\OpenWithProgids)", kProgId, L"");
        ok &= Set(kAppKey + LR"(\SupportedTypes)", ext, L"");
        ok &= Set(kCapabilities + LR"(\FileAssociations)", ext, kProgId);
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return ok;
}

bool UnregisterFileAssociation() {
    DeleteTree(kProgIdKey);
    DeleteTree(kAppKey);
    DeleteTree(kCapabilities);
    RegDeleteKeyValueW(HKEY_CURRENT_USER, kRegisteredApps, L"sideSTEP");
    for (const wchar_t* ext : kExtensions)
        RegDeleteKeyValueW(HKEY_CURRENT_USER, (kClasses + ext + LR"(\OpenWithProgids)").c_str(), kProgId);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}
