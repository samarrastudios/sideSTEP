// Test harness for the preview-pane handler: loads the shell DLL directly (no
// registration needed), hosts the preview in an off-screen window the way
// Explorer's preview pane would, optionally drags it, and saves what it drew.
//   sidestep-previewtest <dll | registered> <file.step> <out.png> [seconds=4] [dragPixels=0]
// "registered" goes through prevhost.exe, as Explorer does, using the installed DLL.
#include <windows.h>
#include <objbase.h>
#include <shlwapi.h>
#include <shobjidl.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "thumb_core.h"

static const CLSID kPreviewClsid = {0x8F1C0C46, 0x5E0B, 0x4D3A, {0x9B, 0x56, 0x6C, 0x7F, 0x3F, 0x2A, 0x51, 0xD4}};

static void Pump(DWORD ms) {
    const DWORD end = GetTickCount() + ms;
    while (GetTickCount() < end) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(10);
    }
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 4) { fwprintf(stderr, L"usage: sidestep-previewtest <dll> <file.step> <out.png> [seconds] [dragPixels]\n"); return 2; }
    const int seconds = argc > 4 ? _wtoi(argv[4]) : 4;
    const int drag = argc > 5 ? _wtoi(argv[5]) : 0;
    const int w = 700, h = 900;
    OleInitialize(nullptr);

    IClassFactory* factory = nullptr;
    IPreviewHandler* handler = nullptr;
    IInitializeWithStream* init = nullptr;
    IPreviewHandlerVisuals* visuals = nullptr;
    IStream* stream = nullptr;
    HRESULT hr = E_FAIL;
    if (!wcscmp(argv[1], L"registered")) {
        // The real route: Windows starts prevhost.exe and loads the registered DLL there.
        hr = CoCreateInstance(kPreviewClsid, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&handler));
    } else {
        HMODULE dll = LoadLibraryW(argv[1]);
        if (!dll) { fwprintf(stderr, L"cannot load %s (error %lu)\n", argv[1], GetLastError()); return 1; }
        typedef HRESULT(STDAPICALLTYPE * GetClassObjectFn)(REFCLSID, REFIID, void**);
        GetClassObjectFn getClassObject = (GetClassObjectFn)GetProcAddress(dll, "DllGetClassObject");
        hr = getClassObject ? getClassObject(kPreviewClsid, IID_PPV_ARGS(&factory)) : E_FAIL;
        if (SUCCEEDED(hr)) hr = factory->CreateInstance(nullptr, IID_PPV_ARGS(&handler));
    }
    if (SUCCEEDED(hr)) hr = handler->QueryInterface(IID_PPV_ARGS(&init));
    if (SUCCEEDED(hr)) hr = SHCreateStreamOnFileEx(argv[2], STGM_READ | STGM_SHARE_DENY_NONE, 0, FALSE, nullptr, &stream);
    if (SUCCEEDED(hr)) hr = init->Initialize(stream, STGM_READ);
    if (FAILED(hr)) { fwprintf(stderr, L"setup failed: 0x%08lX\n", (unsigned long)hr); return 1; }

    HWND host = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC", L"", WS_POPUP | WS_CLIPCHILDREN,
                                -30000, -30000, w, h, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    ShowWindow(host, SW_SHOWNOACTIVATE);
    RECT rc = {0, 0, w, h};
    if (SUCCEEDED(handler->QueryInterface(IID_PPV_ARGS(&visuals)))) visuals->SetBackgroundColor(RGB(255, 255, 255));
    hr = handler->SetWindow(host, &rc);
    if (SUCCEEDED(hr)) hr = handler->DoPreview();
    wprintf(L"DoPreview: 0x%08lX\n", (unsigned long)hr);
    Pump(seconds * 1000);

    HWND child = GetWindow(host, GW_CHILD);
    if (child && drag) {
        const LPARAM from = MAKELPARAM(w / 2, h / 2), to = MAKELPARAM(w / 2 + drag, h / 2 + drag / 3);
        SendMessageW(child, WM_LBUTTONDOWN, MK_LBUTTON, from);
        SendMessageW(child, WM_MOUSEMOVE, MK_LBUTTON, to);
        SendMessageW(child, WM_LBUTTONUP, 0, to);
        Pump(300);
    }

    // Capture what the preview window shows.
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC mem = CreateCompatibleDC(nullptr);
    HBITMAP bmp = CreateDIBSection(mem, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    SelectObject(mem, bmp);
    const BOOL printed = PrintWindow(child ? child : host, mem, 2 /* PW_RENDERFULLCONTENT */);
    GdiFlush();
    stepcore::Image img;
    img.width = w;
    img.height = h;
    img.bgra.assign((const uint8_t*)bits, (const uint8_t*)bits + (size_t)w * h * 4);
    for (size_t i = 3; i < img.bgra.size(); i += 4) img.bgra[i] = 255;
    char path[MAX_PATH * 3];
    WideCharToMultiByte(CP_UTF8, 0, argv[3], -1, path, sizeof(path), nullptr, nullptr);
    const bool saved = stepcore::WritePng(path, img);
    wprintf(L"child=%p printed=%d saved=%d\n", (void*)child, printed, (int)saved);

    handler->Unload();
    if (visuals) visuals->Release();
    init->Release();
    stream->Release();
    handler->Release();
    if (factory) factory->Release();
    Pump(200);
    return saved ? 0 : 1;
}
