// sideSTEP Explorer thumbnail provider, plus the DLL's entry points and
// registration for both shell handlers (thumbnails here, the preview pane in
// preview_handler.cpp).
//
// Windows loads this DLL (in an isolated dllhost.exe, not in Explorer itself),
// hands it the file as a stream, and asks for a bitmap. All the real work is
// in the portable core (core/thumb_core.h).
#include "shared.h"

#include <windows.h>
#include <new>
#include <objbase.h>
#include <propsys.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <thumbcache.h>

#include <cstring>
#include <string>
#include <vector>

#include "thumb_core.h"

#ifndef APP_DISPLAY_NAME_W
#define APP_DISPLAY_NAME_W L"sideSTEP"
#endif

// {62BA6660-F1B2-4D31-8270-6235FD499579}
static const CLSID CLSID_SideStepThumbs = {0x62BA6660, 0xF1B2, 0x4D31, {0x82, 0x70, 0x62, 0x35, 0xFD, 0x49, 0x95, 0x79}};
static const wchar_t kClsidStr[] = L"{62BA6660-F1B2-4D31-8270-6235FD499579}";
// Shell category id for IThumbnailProvider handlers (fixed by Windows).
static const wchar_t kThumbHandlerKey[] = L"ShellEx\\{E357FCCD-A995-4576-B01F-234630154E96}";
static const wchar_t* const kExtensions[] = {L".step", L".stp", L".p21", L".stpnc"};
// Files larger than this are skipped (Explorer shows the normal icon).
static const ULONGLONG kMaxFileBytes = 400ull * 1024 * 1024;

HINSTANCE g_module = nullptr;
LONG g_refs = 0;

// Opt-in diagnostics: set HKCU\Software\sideSTEP "DebugLog" (REG_SZ) to a file
// path and each step is appended there. Off by default; never shows UI.
void DebugLog(const char* what, HRESULT hr) {
    wchar_t path[MAX_PATH] = {};
    DWORD cb = sizeof(path) - sizeof(wchar_t);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\sideSTEP", L"DebugLog", RRF_RT_REG_SZ, nullptr, path, &cb) != ERROR_SUCCESS || !path[0])
        return;
    HANDLE f = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    char line[512];
    const int n = wsprintfA(line, "pid=%lu tid=%lu %s hr=0x%08lX\r\n", GetCurrentProcessId(), GetCurrentThreadId(), what, (unsigned long)hr);
    DWORD w = 0;
    WriteFile(f, line, (DWORD)n, &w, nullptr);
    CloseHandle(f);
}

// Structured exception handling cannot share a function with C++ objects that
// need unwinding, hence these thin wrappers.
static bool GuardedLoad(const char* data, size_t size, const stepcore::LoadOptions* opt, stepcore::Mesh* out, std::string* error) {
    __try {
        return stepcore::LoadStep(data, size, *opt, *out, *error);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
static bool GuardedThumbnail(const char* data, size_t size, int px, stepcore::Image* out, std::string* error) {
    __try {
        return stepcore::RenderStepThumbnail(data, size, px, *out, *error);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool SafeLoadStep(const char* data, size_t size, const stepcore::LoadOptions& opt, stepcore::Mesh& out, std::string& error) {
    const bool ok = GuardedLoad(data, size, &opt, &out, &error);
    if (!ok && error.empty()) { error = "crashed while reading the file"; DebugLog("load: hardware exception", E_FAIL); }
    return ok;
}
bool SafeRenderThumbnail(const char* data, size_t size, int px, stepcore::Image& out, std::string& error) {
    const bool ok = GuardedThumbnail(data, size, px, &out, &error);
    if (!ok && error.empty()) { error = "crashed while reading the file"; DebugLog("thumbnail: hardware exception", E_FAIL); }
    return ok;
}

class ThumbProvider final : public IInitializeWithStream, public IThumbnailProvider {
public:
    ThumbProvider() { InterlockedIncrement(&g_refs); }

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == __uuidof(IInitializeWithStream))
            *ppv = static_cast<IInitializeWithStream*>(this);
        else if (riid == __uuidof(IThumbnailProvider))
            *ppv = static_cast<IThumbnailProvider*>(this);
        else {
            wchar_t g[64] = {};
            char a[96] = "QI unsupported ";
            StringFromGUID2(riid, g, 64);
            WideCharToMultiByte(CP_ACP, 0, g, -1, a + 15, 64, nullptr, nullptr);
            DebugLog(a, E_NOINTERFACE);
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_ref); }
    IFACEMETHODIMP_(ULONG) Release() override {
        const ULONG r = InterlockedDecrement(&m_ref);
        if (r == 0) delete this;
        return r;
    }

    // IInitializeWithStream
    IFACEMETHODIMP Initialize(IStream* stream, DWORD) override {
        DebugLog("Initialize");
        if (m_stream) return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
        if (!stream) return E_INVALIDARG;
        m_stream = stream;
        m_stream->AddRef();
        return S_OK;
    }

    // IThumbnailProvider
    IFACEMETHODIMP GetThumbnail(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* alpha) override {
        const HRESULT hr = GetThumbnailImpl(cx, phbmp, alpha);
        DebugLog("GetThumbnail", hr);
        return hr;
    }

private:
    HRESULT GetThumbnailImpl(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* alpha) {
        if (!phbmp || !alpha) return E_POINTER;
        *phbmp = nullptr;
        if (!m_stream) return E_UNEXPECTED;
        if (cx < 16) cx = 16;
        if (cx > 1024) cx = 1024;

        STATSTG st = {};
        HRESULT hr = m_stream->Stat(&st, STATFLAG_NONAME);
        if (FAILED(hr)) return hr;
        if (st.cbSize.QuadPart == 0 || st.cbSize.QuadPart > kMaxFileBytes) return E_FAIL;

        std::vector<char> data;
        try {
            data.resize((size_t)st.cbSize.QuadPart);
        } catch (...) {
            return E_OUTOFMEMORY;
        }
        size_t got = 0;
        while (got < data.size()) {
            ULONG n = 0;
            const ULONG want = (ULONG)((data.size() - got) > (1u << 20) ? (1u << 20) : (data.size() - got));
            hr = m_stream->Read(data.data() + got, want, &n);
            if (FAILED(hr)) return hr;
            if (n == 0) break;
            got += n;
        }
        if (got == 0) return E_FAIL;

        stepcore::Image img;
        std::string err;
        bool ok = false;
        try {
            ok = SafeRenderThumbnail(data.data(), got, (int)cx, img, err);
        } catch (...) {
            ok = false;
        }
        if (!ok || img.width <= 0) return E_FAIL;

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = img.width;
        bmi.bmiHeader.biHeight = -img.height;  // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP hbmp = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!hbmp || !bits) return E_OUTOFMEMORY;
        std::memcpy(bits, img.bgra.data(), img.bgra.size());
        *phbmp = hbmp;
        *alpha = WTSAT_ARGB;
        return S_OK;
    }

private:
    ~ThumbProvider() {
        if (m_stream) m_stream->Release();
        InterlockedDecrement(&g_refs);
    }
    LONG m_ref = 1;
    IStream* m_stream = nullptr;
};

class ClassFactory final : public IClassFactory {
public:
    explicit ClassFactory(bool preview) : m_preview(preview) {}
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_ref); }
    IFACEMETHODIMP_(ULONG) Release() override {
        const ULONG r = InterlockedDecrement(&m_ref);
        if (r == 0) delete this;
        return r;
    }
    IFACEMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        if (m_preview) return CreatePreviewHandler(riid, ppv);
        ThumbProvider* p = new (std::nothrow) ThumbProvider();
        if (!p) return E_OUTOFMEMORY;
        const HRESULT hr = p->QueryInterface(riid, ppv);
        p->Release();
        return hr;
    }
    IFACEMETHODIMP LockServer(BOOL lock) override {
        if (lock) InterlockedIncrement(&g_refs);
        else InterlockedDecrement(&g_refs);
        return S_OK;
    }

private:
    ~ClassFactory() = default;
    LONG m_ref = 1;
    const bool m_preview;
};

// ---- registration (per-user: HKCU, so no admin rights are needed) ----

static HRESULT SetStr(const std::wstring& key, const wchar_t* name, const std::wstring& value) {
    HKEY h;
    LONG r = RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &h, nullptr);
    if (r != ERROR_SUCCESS) return HRESULT_FROM_WIN32(r);
    r = RegSetValueExW(h, name, 0, REG_SZ, (const BYTE*)value.c_str(), (DWORD)((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(h);
    return HRESULT_FROM_WIN32(r);
}

static std::wstring ExtKey(const wchar_t* ext, bool systemAssoc, const wchar_t* handlerKey = kThumbHandlerKey) {
    std::wstring k = L"Software\\Classes\\";
    if (systemAssoc) k += L"SystemFileAssociations\\";
    k += ext;
    k += L"\\";
    k += handlerKey;
    return k;
}

// Preview pane: shell category id for IPreviewHandler, the list Windows keeps
// of installed preview handlers, and the 64-bit prevhost.exe surrogate.
static const wchar_t kPreviewClsidStr[] = L"{8F1C0C46-5E0B-4D3A-9B56-6C7F3F2A51D4}";
static const wchar_t kPreviewHandlerKey[] = L"ShellEx\\{8895b1c6-b41f-4c1c-a562-0d564250836f}";
static const wchar_t kPreviewHandlersList[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\PreviewHandlers";
static const wchar_t kPrevhostAppId[] = L"{6d2b5079-2f0b-48dd-ab7f-97cec514d30b}";

STDAPI DllRegisterServer() {
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(g_module, path, MAX_PATH)) return HRESULT_FROM_WIN32(GetLastError());
    const std::wstring clsKey = std::wstring(L"Software\\Classes\\CLSID\\") + kClsidStr;
    HRESULT hr = SetStr(clsKey, nullptr, APP_DISPLAY_NAME_W L" Thumbnail Provider");
    if (SUCCEEDED(hr)) hr = SetStr(clsKey + L"\\InprocServer32", nullptr, path);
    if (SUCCEEDED(hr)) hr = SetStr(clsKey + L"\\InprocServer32", L"ThreadingModel", L"Apartment");
    for (const wchar_t* ext : kExtensions) {
        // Both locations: the plain extension key, and SystemFileAssociations,
        // which still applies when another CAD app owns the file type.
        if (SUCCEEDED(hr)) hr = SetStr(ExtKey(ext, false), nullptr, kClsidStr);
        if (SUCCEEDED(hr)) hr = SetStr(ExtKey(ext, true), nullptr, kClsidStr);
    }
    const std::wstring prevKey = std::wstring(L"Software\\Classes\\CLSID\\") + kPreviewClsidStr;
    if (SUCCEEDED(hr)) hr = SetStr(prevKey, nullptr, APP_DISPLAY_NAME_W L" Preview Handler");
    if (SUCCEEDED(hr)) hr = SetStr(prevKey, L"DisplayName", APP_DISPLAY_NAME_W L" preview");
    if (SUCCEEDED(hr)) hr = SetStr(prevKey, L"AppID", kPrevhostAppId);
    if (SUCCEEDED(hr)) hr = SetStr(prevKey + L"\\InprocServer32", nullptr, path);
    if (SUCCEEDED(hr)) hr = SetStr(prevKey + L"\\InprocServer32", L"ThreadingModel", L"Apartment");
    if (SUCCEEDED(hr)) hr = SetStr(kPreviewHandlersList, kPreviewClsidStr, APP_DISPLAY_NAME_W L" preview");
    for (const wchar_t* ext : kExtensions) {
        if (SUCCEEDED(hr)) hr = SetStr(ExtKey(ext, false, kPreviewHandlerKey), nullptr, kPreviewClsidStr);
        if (SUCCEEDED(hr)) hr = SetStr(ExtKey(ext, true, kPreviewHandlerKey), nullptr, kPreviewClsidStr);
    }
    if (SUCCEEDED(hr)) SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return hr;
}

STDAPI DllUnregisterServer() {
    const std::wstring clsKey = std::wstring(L"Software\\Classes\\CLSID\\") + kClsidStr;
    RegDeleteTreeW(HKEY_CURRENT_USER, clsKey.c_str());
    RegDeleteKeyW(HKEY_CURRENT_USER, clsKey.c_str());
    for (const wchar_t* ext : kExtensions) {
        for (int sys = 0; sys < 2; ++sys) {
            const std::wstring k = ExtKey(ext, sys != 0);
            // Only remove the handler entry if it is still ours.
            wchar_t cur[64] = {};
            DWORD cb = sizeof(cur);
            if (RegGetValueW(HKEY_CURRENT_USER, k.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, cur, &cb) == ERROR_SUCCESS &&
                _wcsicmp(cur, kClsidStr) == 0) {
                RegDeleteKeyW(HKEY_CURRENT_USER, k.c_str());
            }
        }
    }
    const std::wstring prevKey = std::wstring(L"Software\\Classes\\CLSID\\") + kPreviewClsidStr;
    RegDeleteTreeW(HKEY_CURRENT_USER, prevKey.c_str());
    RegDeleteKeyW(HKEY_CURRENT_USER, prevKey.c_str());
    RegDeleteKeyValueW(HKEY_CURRENT_USER, kPreviewHandlersList, kPreviewClsidStr);
    for (const wchar_t* ext : kExtensions) {
        for (int sys = 0; sys < 2; ++sys) {
            const std::wstring k = ExtKey(ext, sys != 0, kPreviewHandlerKey);
            wchar_t cur[64] = {};
            DWORD cb = sizeof(cur);
            if (RegGetValueW(HKEY_CURRENT_USER, k.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, cur, &cb) == ERROR_SUCCESS &&
                _wcsicmp(cur, kPreviewClsidStr) == 0) {
                RegDeleteKeyW(HKEY_CURRENT_USER, k.c_str());
            }
        }
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    DebugLog("DllGetClassObject");
    const bool preview = clsid == CLSID_SideStepPreview;
    if (!preview && clsid != CLSID_SideStepThumbs) return CLASS_E_CLASSNOTAVAILABLE;
    ClassFactory* f = new (std::nothrow) ClassFactory(preview);
    if (!f) return E_OUTOFMEMORY;
    const HRESULT hr = f->QueryInterface(riid, ppv);
    f->Release();
    return hr;
}

STDAPI DllCanUnloadNow() { return g_refs == 0 ? S_OK : S_FALSE; }

BOOL APIENTRY DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = inst;
        DisableThreadLibraryCalls(inst);
        DebugLog("DLL_PROCESS_ATTACH");
    }
    return TRUE;
}
