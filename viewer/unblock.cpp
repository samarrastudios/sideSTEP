#include "unblock.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <exdisp.h>
#include <servprov.h>
#include <shlguid.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <deque>
#include <map>
#include <mutex>
#include <vector>

namespace {

const wchar_t kRunKey[] = LR"(Software\Microsoft\Windows\CurrentVersion\Run)";
const wchar_t kRunValue[] = L"sideSTEP";
const wchar_t kMutexName[] = LR"(Local\sideSTEP.UnblockWatcher)";
const wchar_t kStopEvent[] = LR"(Local\sideSTEP.UnblockWatcher.Stop)";
const wchar_t kMarkStream[] = L":Zone.Identifier";

bool HasStepExtension(const std::wstring& path) {
    const size_t dot = path.find_last_of(L'.');
    const size_t slash = path.find_last_of(L"\\/");
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) return false;
    const wchar_t* ext = path.c_str() + dot;
    return !_wcsicmp(ext, L".step") || !_wcsicmp(ext, L".stp") || !_wcsicmp(ext, L".p21");
}

// A change notification for the mark itself names the stream
// ("part.step:Zone.Identifier"); reduce it to the file.
std::wstring WithoutStreamName(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    const size_t colon = path.find(L':', slash == std::wstring::npos ? 0 : slash);
    // A drive letter's colon comes before any slash, so it is never found here
    // except for a bare "C:name", which Windows does not report.
    return colon == std::wstring::npos ? path : path.substr(0, colon);
}

// Files whose mark may only be written a moment after they appear (browsers
// add it when the download completes), so each is tried again a little later.
struct Retry { std::wstring path; ULONGLONG due; };
std::mutex g_retryLock;
std::deque<Retry> g_retries;

void QueueRetries(const std::wstring& path) {
    const ULONGLONG now = GetTickCount64();
    std::lock_guard<std::mutex> g(g_retryLock);
    if (g_retries.size() > 4000) return;  // a flood of changes; the next event will catch up
    g_retries.push_back({path, now + 1000});
    g_retries.push_back({path, now + 6000});
}

DWORD WINAPI RetryThread(LPVOID) {
    for (;;) {
        Sleep(500);
        std::vector<std::wstring> due;
        {
            const ULONGLONG now = GetTickCount64();
            std::lock_guard<std::mutex> g(g_retryLock);
            for (auto it = g_retries.begin(); it != g_retries.end();) {
                if (it->due <= now) { due.push_back(it->path); it = g_retries.erase(it); }
                else ++it;
            }
        }
        for (const std::wstring& p : due) UnblockStepFile(p);
    }
}

DWORD WINAPI WatchThread(LPVOID param) {
    std::wstring* rootArg = static_cast<std::wstring*>(param);
    const std::wstring root = *rootArg;
    delete rootArg;
    HANDLE dir = CreateFileW(root.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (dir == INVALID_HANDLE_VALUE) return 1;
    std::vector<BYTE> buffer(256 * 1024);
    const DWORD filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE |
                         0x00000200 /* stream name */ | 0x00000800 /* stream write */;
    for (;;) {
        DWORD bytes = 0;
        if (!ReadDirectoryChangesW(dir, buffer.data(), (DWORD)buffer.size(), TRUE, filter, &bytes, nullptr, nullptr)) {
            Sleep(2000);  // the drive went away, or something transient; try again
            continue;
        }
        if (bytes == 0) continue;  // too many changes at once to report; later events still arrive
        const BYTE* p = buffer.data();
        for (;;) {
            const FILE_NOTIFY_INFORMATION* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(p);
            if (info->Action != FILE_ACTION_REMOVED && info->Action != FILE_ACTION_RENAMED_OLD_NAME) {
                const std::wstring name(info->FileName, info->FileNameLength / sizeof(wchar_t));
                const std::wstring path = WithoutStreamName(root + name);
                if (HasStepExtension(path)) {
                    UnblockStepFile(path);
                    QueueRetries(path);
                }
            }
            if (!info->NextEntryOffset) break;
            p += info->NextEntryOffset;
        }
    }
}

// Folders that cannot hold the user's STEP files and are slow or pointless to walk.
bool SkipFolder(const std::wstring& parent, const wchar_t* name, DWORD attributes) {
    if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) return true;  // junctions, cloud folders
    if (!wcscmp(name, L".") || !wcscmp(name, L"..")) return true;
    static const wchar_t* const anywhere[] = {L"$Recycle.Bin", L"System Volume Information", L"node_modules", L".git"};
    for (const wchar_t* n : anywhere)
        if (!_wcsicmp(name, n)) return true;
    if (parent.size() == 3) {  // directly under a drive root
        static const wchar_t* const atRoot[] = {L"Windows", L"Program Files", L"Program Files (x86)", L"ProgramData"};
        for (const wchar_t* n : atRoot)
            if (!_wcsicmp(name, n)) return true;
    }
    return !_wcsicmp(name, L"AppData");
}

void Sweep(const std::wstring& folder) {
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileExW((folder + L"*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
                                   FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!SkipFolder(folder, fd.cFileName, fd.dwFileAttributes)) Sweep(folder + fd.cFileName + L"\\");
        } else {
            const std::wstring path = folder + fd.cFileName;
            if (HasStepExtension(path)) UnblockStepFile(path);
        }
    } while (FindNextFileW(find, &fd));
    FindClose(find);
}

DWORD WINAPI SweepThread(LPVOID param) {
    std::vector<std::wstring>* roots = static_cast<std::vector<std::wstring>*>(param);
    SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);  // stay out of the way
    for (const std::wstring& r : *roots) Sweep(r);
    delete roots;
    return 0;
}

// Follows File Explorer: whatever folder (or Home / search list) a window is
// showing, the STEP files in it are unblocked before the user clicks one.
// This is what covers cloud and network drives, which are too big to sweep
// and do not always report changes: Google Drive, for one, marks its files
// as coming from the internet.
template <class T>
struct Com {
    T* p = nullptr;
    ~Com() { if (p) p->Release(); }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
};

void UnblockItemsIn(IFolderView* view) {
    Com<IShellItemArray> items;
    if (FAILED(view->Items(SVGIO_ALLVIEW | SVGIO_FLAG_VIEWORDER, IID_PPV_ARGS(&items)))) return;
    DWORD count = 0;
    items->GetCount(&count);
    for (DWORD i = 0; i < count && i < 20000; ++i) {
        Com<IShellItem> item;
        if (FAILED(items->GetItemAt(i, &item))) continue;
        LPWSTR path = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
            UnblockStepFile(path);
            CoTaskMemFree(path);
        }
    }
}

DWORD WINAPI ExplorerThread(LPVOID) {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
    struct Seen { std::wstring folder; int count = -1; ULONGLONG when = 0; };
    std::map<HWND, Seen> seen;
    for (;;) {
        Com<IShellWindows> windows;
        long total = 0;
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&windows)))) windows->get_Count(&total);
        for (long i = 0; i < total; ++i) {
            VARIANT index;
            VariantInit(&index);
            index.vt = VT_I4;
            index.lVal = i;
            Com<IDispatch> window;
            if (windows->Item(index, &window) != S_OK || !window.p) continue;
            Com<IServiceProvider> provider;
            Com<IShellBrowser> browser;
            Com<IShellView> shellView;
            Com<IFolderView> view;
            Com<IPersistFolder2> folder;
            if (FAILED(window->QueryInterface(IID_PPV_ARGS(&provider))) ||
                FAILED(provider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser))) ||
                FAILED(browser->QueryActiveShellView(&shellView)) || FAILED(shellView->QueryInterface(IID_PPV_ARGS(&view))) ||
                FAILED(view->GetFolder(IID_PPV_ARGS(&folder))))
                continue;
            HWND hwnd = nullptr;
            browser->GetWindow(&hwnd);
            std::wstring name;
            PIDLIST_ABSOLUTE pidl = nullptr;
            if (SUCCEEDED(folder->GetCurFolder(&pidl)) && pidl) {
                LPWSTR parsing = nullptr;
                if (SUCCEEDED(SHGetNameFromIDList(pidl, SIGDN_DESKTOPABSOLUTEPARSING, &parsing)) && parsing) {
                    name = parsing;
                    CoTaskMemFree(parsing);
                }
                CoTaskMemFree(pidl);
            }
            int count = 0;
            view->ItemCount(SVGIO_ALLVIEW, &count);
            // Look again when the window shows a different place or a different
            // number of items, and every so often regardless.
            Seen& s = seen[hwnd];
            const ULONGLONG now = GetTickCount64();
            if (s.folder == name && s.count == count && now - s.when < 15000) continue;
            s.folder = name;
            s.count = count;
            s.when = now;
            UnblockItemsIn(view.p);
        }
        if (seen.size() > 200) seen.clear();  // windows come and go
        Sleep(1200);
    }
}

// Fixed drives. The mark is a named stream; drives that say they have none
// (Google Drive presents itself as FAT32 yet keeps the mark) are still
// watched, but only those that say they do are swept from end to end.
std::vector<std::wstring> WatchRoots(bool sweepable) {
    std::vector<std::wstring> roots;
    wchar_t drives[512] = {};
    GetLogicalDriveStringsW(511, drives);
    for (const wchar_t* d = drives; *d; d += wcslen(d) + 1) {
        if (GetDriveTypeW(d) != DRIVE_FIXED) continue;
        DWORD flags = 0;
        if (!GetVolumeInformationW(d, nullptr, 0, nullptr, nullptr, &flags, nullptr, 0)) continue;
        if (!sweepable || (flags & FILE_NAMED_STREAMS)) roots.push_back(d);
    }
    return roots;
}

}  // namespace

void UnblockStepFile(const std::wstring& path) {
    if (!HasStepExtension(path)) return;
    // Leave cloud placeholders alone: touching them would download the file.
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) return;
    if (attributes & (FILE_ATTRIBUTE_OFFLINE | 0x00400000 /* recall on data access */ | 0x00040000 /* recall on open */)) return;
    const std::wstring mark = path + kMarkStream;
    if (DeleteFileW(mark.c_str())) return;
    // No mark at all is the usual case. A read-only file refuses the change:
    // lift the flag for a moment, remove the mark, and put the flag back.
    if (GetLastError() == ERROR_ACCESS_DENIED && (attributes & FILE_ATTRIBUTE_READONLY) &&
        SetFileAttributesW(path.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY)) {
        DeleteFileW(mark.c_str());
        SetFileAttributesW(path.c_str(), attributes);
    }
}

int RunUnblockWatcher() {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) return 0;  // already watching

    const std::vector<std::wstring> roots = WatchRoots(false);
    CloseHandle(CreateThread(nullptr, 0, &RetryThread, nullptr, 0, nullptr));
    CloseHandle(CreateThread(nullptr, 0, &ExplorerThread, nullptr, 0, nullptr));
    for (const std::wstring& r : roots) CloseHandle(CreateThread(nullptr, 0, &WatchThread, new std::wstring(r), 0, nullptr));
    // Existing files: the user's own folders first (that is where downloads
    // are), then the rest of each drive, which can take a long while.
    auto* sweep = new std::vector<std::wstring>();
    wchar_t profile[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH)) sweep->push_back(std::wstring(profile) + LR"(\)");
    const std::vector<std::wstring> sweepRoots = WatchRoots(true);
    sweep->insert(sweep->end(), sweepRoots.begin(), sweepRoots.end());
    CloseHandle(CreateThread(nullptr, 0, &SweepThread, sweep, 0, nullptr));
    // Runs until the user switches it off (SetWatcherEnabled(false) signals this event).
    HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, kStopEvent);
    if (stop) WaitForSingleObject(stop, INFINITE);
    else Sleep(INFINITE);
    return 0;
}

bool IsWatcherEnabled() {
    wchar_t value[MAX_PATH * 2];
    DWORD size = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, value, &size) == ERROR_SUCCESS;
}

bool SetWatcherEnabled(bool enabled) {
    if (!enabled) {
        RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kRunValue);
        if (HANDLE stop = OpenEventW(EVENT_MODIFY_STATE, FALSE, kStopEvent)) {
            SetEvent(stop);
            CloseHandle(stop);
        }
        return true;
    }
    wchar_t exe[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) return false;
    std::wstring command = L'"' + std::wstring(exe) + LR"(" --watch)";
    if (RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, REG_SZ, command.c_str(),
                        (DWORD)((command.size() + 1) * sizeof(wchar_t))) != ERROR_SUCCESS)
        return false;
    // Start it now as well; a second copy exits by itself if one is already watching.
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi = {};
    if (CreateProcessW(exe, &command[0], nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    return true;
}
