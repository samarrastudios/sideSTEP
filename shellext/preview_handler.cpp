// sideSTEP preview handler: the model in File Explorer's preview pane, where
// it can be dragged around to look at it from any side.
//
// Windows hosts this in prevhost.exe. The file arrives as a stream; it is
// loaded on a worker thread (big assemblies take seconds) and drawn with plain
// OpenGL 1.1, which every Windows machine has, so the DLL needs no Qt and no
// extra runtime.
#include "shared.h"

#include <windowsx.h>
#include <new>
#include <objbase.h>
#include <ocidl.h>
#include <shlwapi.h>
#include <shobjidl.h>

#include <GL/gl.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

const CLSID CLSID_SideStepPreview = {0x8F1C0C46, 0x5E0B, 0x4D3A, {0x9B, 0x56, 0x6C, 0x7F, 0x3F, 0x2A, 0x51, 0xD4}};

namespace {

const wchar_t kWindowClass[] = L"sideSTEPPreview";
const UINT WM_MESH_READY = WM_APP + 1;
const ULONGLONG kMaxFileBytes = 400ull * 1024 * 1024;

#ifndef GL_MULTISAMPLE
#define GL_MULTISAMPLE 0x809D
#endif
#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT 0x80E1
#endif

// Result of the background load, shared between the worker and the window.
struct LoadState {
    std::mutex lock;
    HWND notify = nullptr;  // cleared when the window goes away
    std::vector<char> data;
    stepcore::Mesh mesh;
    std::string error;
    bool done = false, ok = false;
};

DWORD WINAPI LoadThread(LPVOID param) {
    std::shared_ptr<LoadState>* holder = static_cast<std::shared_ptr<LoadState>*>(param);
    std::shared_ptr<LoadState> state = *holder;
    delete holder;
    {
        stepcore::LoadOptions opt;
        opt.parallel = false;  // see RenderStepThumbnail: OCCT's thread pool hangs a DLL host at exit
        stepcore::Mesh mesh;
        std::string error;
        const bool ok = SafeLoadStep(state->data.data(), state->data.size(), opt, mesh, error);
        std::lock_guard<std::mutex> g(state->lock);
        state->data = std::vector<char>();
        state->mesh = std::move(mesh);
        state->error = error;
        state->ok = ok;
        state->done = true;
        if (state->notify) PostMessageW(state->notify, WM_MESH_READY, 0, 0);
    }
    state.reset();
    InterlockedDecrement(&g_refs);
    // Keep the DLL mapped until this thread has really left its code.
    FreeLibraryAndExitThread(g_module, 0);
}

// ---- the preview window ------------------------------------------------------

class View {
public:
    static HWND Create(HWND parent, const RECT& rc, COLORREF background, COLORREF text) {
        static ATOM atom = 0;
        if (!atom) {
            WNDCLASSEXW wc = {sizeof(wc)};
            wc.style = CS_OWNDC | CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
            wc.lpfnWndProc = &View::WndProc;
            wc.hInstance = g_module;
            wc.hCursor = LoadCursorW(nullptr, IDC_SIZEALL);
            wc.lpszClassName = kWindowClass;
            atom = RegisterClassExW(&wc);
            if (!atom) return nullptr;
        }
        View* v = new (std::nothrow) View();
        if (!v) return nullptr;
        v->background_ = background;
        v->textColor_ = text;
        HWND h = CreateWindowExW(0, kWindowClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                 rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, parent, nullptr, g_module, v);
        if (!h) delete v;  // WM_NCCREATE never attached it
        return h;
    }

    static View* From(HWND h) { return reinterpret_cast<View*>(GetWindowLongPtrW(h, GWLP_USERDATA)); }

    void Start(std::shared_ptr<LoadState> state) {
        state_ = std::move(state);
        std::lock_guard<std::mutex> g(state_->lock);
        state_->notify = hwnd_;
    }
    void SetColors(COLORREF background, COLORREF text) {
        background_ = background;
        textColor_ = text;
        logoDirty_ = true;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

private:
    View() { InterlockedIncrement(&g_refs); }
    ~View() { InterlockedDecrement(&g_refs); }

    static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_NCCREATE) {
            View* v = static_cast<View*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            v->hwnd_ = h;
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(v));
            return DefWindowProcW(h, msg, wp, lp);
        }
        View* v = From(h);
        if (!v) return DefWindowProcW(h, msg, wp, lp);
        switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(h, &ps);
            v->Paint();
            EndPaint(h, &ps);
            return 0;
        }
        case WM_MESH_READY:
            v->MeshReady();
            return 0;
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
            SetFocus(h);
            SetCapture(h);
            v->last_ = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            v->touched_ = true;
            return 0;
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
            if (!(wp & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON))) ReleaseCapture();
            return 0;
        case WM_MOUSEMOVE: {
            const POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            const int dx = p.x - v->last_.x, dy = p.y - v->last_.y;
            if (GetCapture() == h && (dx || dy)) {
                if (wp & MK_LBUTTON) {
                    v->azimuth_ -= dx * 0.5f;
                    v->elevation_ = std::max(-89.0f, std::min(89.0f, v->elevation_ + dy * 0.5f));
                } else if (wp & (MK_RBUTTON | MK_MBUTTON)) {
                    v->panX_ += dx;
                    v->panY_ -= dy;
                }
                v->last_ = p;
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        }
        case WM_MOUSEWHEEL: {
            const float steps = GET_WHEEL_DELTA_WPARAM(wp) / 120.0f;
            v->zoom_ = std::max(0.2f, std::min(40.0f, v->zoom_ * std::pow(1.15f, steps)));
            v->touched_ = true;
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case WM_LBUTTONDBLCLK:
            v->ResetCamera();
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_DESTROY:
            v->Destroy();
            return 0;
        case WM_NCDESTROY:
            SetWindowLongPtrW(h, GWLP_USERDATA, 0);
            delete v;
            return DefWindowProcW(h, msg, wp, lp);
        }
        return DefWindowProcW(h, msg, wp, lp);
    }

    void ResetCamera() {
        // Same direction as the thumbnail: Z up, seen from front-right-top.
        azimuth_ = -45.0f;
        elevation_ = 30.1f;
        zoom_ = 1.0f;
        panX_ = panY_ = 0;
    }

    void Destroy() {
        if (state_) {
            std::lock_guard<std::mutex> g(state_->lock);
            state_->notify = nullptr;
        }
        state_.reset();
        if (glrc_) {
            wglMakeCurrent(dc_, glrc_);
            if (list_) glDeleteLists(list_, 1);
            if (font_) glDeleteLists(font_, 96);
            if (logo_) glDeleteTextures(1, &logo_);
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(glrc_);
            glrc_ = nullptr;
        }
        if (dc_) { ReleaseDC(hwnd_, dc_); dc_ = nullptr; }
    }

    // Asks the driver for a multisampled pixel format. That needs an OpenGL
    // context to exist first, hence the throwaway window.
    int MultisampleFormat() {
        int format = 0;
        HWND dummy = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 8, 8, nullptr, nullptr, g_module, nullptr);
        if (!dummy) return 0;
        HDC ddc = GetDC(dummy);
        PIXELFORMATDESCRIPTOR pfd = BasicFormat();
        const int basic = ChoosePixelFormat(ddc, &pfd);
        if (basic && SetPixelFormat(ddc, basic, &pfd)) {
            if (HGLRC rc = wglCreateContext(ddc)) {
                wglMakeCurrent(ddc, rc);
                typedef BOOL(WINAPI * ChooseFn)(HDC, const int*, const FLOAT*, UINT, int*, UINT*);
                ChooseFn choose = reinterpret_cast<ChooseFn>(wglGetProcAddress("wglChoosePixelFormatARB"));
                if (choose) {
                    const int attribs[] = {0x2001 /*DRAW_TO_WINDOW*/, 1, 0x2010 /*SUPPORT_OPENGL*/, 1,
                                           0x2011 /*DOUBLE_BUFFER*/, 1, 0x2013 /*PIXEL_TYPE*/, 0x202B /*RGBA*/,
                                           0x2014 /*COLOR_BITS*/, 32, 0x2022 /*DEPTH_BITS*/, 24,
                                           0x2041 /*SAMPLE_BUFFERS*/, 1, 0x2042 /*SAMPLES*/, 4, 0};
                    UINT count = 0;
                    int f = 0;
                    if (choose(dc_, attribs, nullptr, 1, &f, &count) && count > 0) format = f;
                }
                wglMakeCurrent(nullptr, nullptr);
                wglDeleteContext(rc);
            }
        }
        ReleaseDC(dummy, ddc);
        DestroyWindow(dummy);
        return format;
    }

    static PIXELFORMATDESCRIPTOR BasicFormat() {
        PIXELFORMATDESCRIPTOR pfd = {sizeof(pfd), 1};
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 32;
        pfd.cDepthBits = 24;
        return pfd;
    }

    bool EnsureGl() {
        if (glrc_) return true;
        if (glFailed_) return false;
        dc_ = GetDC(hwnd_);
        PIXELFORMATDESCRIPTOR pfd = BasicFormat();
        int format = MultisampleFormat();
        DebugLog(format ? "preview: multisampled format" : "preview: no multisampling", (HRESULT)format);
        if (!format) format = ChoosePixelFormat(dc_, &pfd);
        if (format) DescribePixelFormat(dc_, format, sizeof(pfd), &pfd);
        if (!format || !SetPixelFormat(dc_, format, &pfd) || !(glrc_ = wglCreateContext(dc_))) {
            DebugLog("preview: OpenGL unavailable", HRESULT_FROM_WIN32(GetLastError()));
            glFailed_ = true;
            return false;
        }
        wglMakeCurrent(dc_, glrc_);
        const int px = MulDiv(15, GetDpiForWindow(hwnd_), 96);
        HFONT f = CreateFontW(-px, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        if (f) {
            HGDIOBJ old = SelectObject(dc_, f);
            font_ = glGenLists(96);
            if (!wglUseFontBitmapsW(dc_, 32, 96, font_)) { glDeleteLists(font_, 96); font_ = 0; }
            SelectObject(dc_, old);
            DeleteObject(f);
        }
        return true;
    }

    void MeshReady() {
        if (!state_ || !EnsureGl()) { InvalidateRect(hwnd_, nullptr, FALSE); return; }
        stepcore::Mesh mesh;
        {
            std::lock_guard<std::mutex> g(state_->lock);
            if (!state_->done) return;
            loaded_ = true;
            if (!state_->ok) { failed_ = true; }
            else mesh = std::move(state_->mesh);
        }
        if (!failed_ && !mesh.tris.empty()) {
            for (int a = 0; a < 3; ++a) centre_[a] = 0.5f * (mesh.bmin[a] + mesh.bmax[a]);
            const float dx = mesh.bmax[0] - mesh.bmin[0], dy = mesh.bmax[1] - mesh.bmin[1], dz = mesh.bmax[2] - mesh.bmin[2];
            radius_ = std::max(0.5f * std::sqrt(dx * dx + dy * dy + dz * dz), 1e-6f);

            // Flatten into per-vertex arrays and compile once; the driver keeps
            // the geometry, so drawing a frame is a single call.
            const size_t n = mesh.tris.size() * 3;
            std::vector<float> pos(n * 3), nrm(n * 3);
            std::vector<unsigned char> col(n * 3);
            size_t i = 0;
            for (const stepcore::Triangle& t : mesh.tris) {
                const float* p0 = t.p[0]; const float* p1 = t.p[1]; const float* p2 = t.p[2];
                const float e1[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
                const float e2[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
                const float fn[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
                for (int k = 0; k < 3; ++k, ++i) {
                    const bool hasN = t.n[k][0] != 0 || t.n[k][1] != 0 || t.n[k][2] != 0;
                    // Stored normals do not always agree with the triangle's
                    // winding (reversed faces); make them, or the face is lit
                    // from behind and comes out dark.
                    const float side = t.n[k][0] * fn[0] + t.n[k][1] * fn[1] + t.n[k][2] * fn[2];
                    const float sign = side < 0 ? -1.0f : 1.0f;
                    for (int a = 0; a < 3; ++a) {
                        pos[i * 3 + a] = t.p[k][a] - centre_[a];
                        nrm[i * 3 + a] = hasN ? sign * t.n[k][a] : fn[a];
                        col[i * 3 + a] = t.rgb[a];
                    }
                }
            }
            // A sample of the vertices, used every frame to keep the model
            // fitted to the pane from whatever side it is seen.
            const size_t stride = std::max<size_t>(1, n / 20000);
            for (size_t v = 0; v < n; v += stride) {
                outline_.push_back(pos[v * 3]);
                outline_.push_back(pos[v * 3 + 1]);
                outline_.push_back(pos[v * 3 + 2]);
            }
            mesh = stepcore::Mesh();
            wglMakeCurrent(dc_, glrc_);
            glEnableClientState(GL_VERTEX_ARRAY);
            glEnableClientState(GL_NORMAL_ARRAY);
            glEnableClientState(GL_COLOR_ARRAY);
            glVertexPointer(3, GL_FLOAT, 0, pos.data());
            glNormalPointer(GL_FLOAT, 0, nrm.data());
            glColorPointer(3, GL_UNSIGNED_BYTE, 0, col.data());
            list_ = glGenLists(1);
            glNewList(list_, GL_COMPILE);
            glDrawArrays(GL_TRIANGLES, 0, (GLsizei)n);
            glEndList();
            glDisableClientState(GL_VERTEX_ARRAY);
            glDisableClientState(GL_NORMAL_ARRAY);
            glDisableClientState(GL_COLOR_ARRAY);
        } else {
            failed_ = true;
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    // The app icon, flattened onto the pane colour so it needs no blending.
    void BuildLogo(int size) {
        if (logo_) { glDeleteTextures(1, &logo_); logo_ = 0; }
        logoDirty_ = false;
        const int tex = 64;
        HICON icon = (HICON)LoadImageW(g_module, L"IDI_APP", IMAGE_ICON, tex, tex, LR_DEFAULTCOLOR);
        if (!icon) return;
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = tex;
        bmi.bmiHeader.biHeight = tex;  // bottom-up, as OpenGL wants
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        void* bits = nullptr;
        HDC mem = CreateCompatibleDC(nullptr);
        HBITMAP bmp = CreateDIBSection(mem, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (bmp && bits) {
            HGDIOBJ old = SelectObject(mem, bmp);
            RECT r = {0, 0, tex, tex};
            HBRUSH b = CreateSolidBrush(background_);
            FillRect(mem, &r, b);
            DeleteObject(b);
            DrawIconEx(mem, 0, 0, icon, tex, tex, 0, nullptr, DI_NORMAL);
            GdiFlush();
            glGenTextures(1, &logo_);
            glBindTexture(GL_TEXTURE_2D, logo_);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, tex, tex, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, bits);
            SelectObject(mem, old);
        }
        if (bmp) DeleteObject(bmp);
        DeleteDC(mem);
        DestroyIcon(icon);
        (void)size;
    }

    void Text(const char* s, float x, float y) {
        if (!font_) return;
        glColor3ub(GetRValue(textColor_), GetGValue(textColor_), GetBValue(textColor_));
        glRasterPos2f(x, y);
        glListBase(font_ - 32);
        glCallLists((GLsizei)strlen(s), GL_UNSIGNED_BYTE, s);
    }

    void Paint() {
        RECT rc;
        GetClientRect(hwnd_, &rc);
        const int w = rc.right, h = rc.bottom;
        if (w <= 0 || h <= 0) return;
        if (!EnsureGl()) {
            HBRUSH b = CreateSolidBrush(background_);
            FillRect(dc_ ? dc_ : GetDC(hwnd_), &rc, b);
            DeleteObject(b);
            return;
        }
        wglMakeCurrent(dc_, glrc_);
        glViewport(0, 0, w, h);
        glClearColor(GetRValue(background_) / 255.0f, GetGValue(background_) / 255.0f, GetBValue(background_) / 255.0f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        const float dpi = GetDpiForWindow(hwnd_) / 96.0f;

        if (list_) {
            // Orthographic, fitted to the model's outline as seen from here,
            // so it stays centred and as large as the pane allows while it spins.
            const float az = azimuth_ * 3.14159265f / 180.0f, el = elevation_ * 3.14159265f / 180.0f;
            const float right[3] = {-std::sin(az), std::cos(az), 0};
            const float up[3] = {-std::sin(el) * std::cos(az), -std::sin(el) * std::sin(az), std::cos(el)};
            float minX = 1e30f, maxX = -1e30f, minY = 1e30f, maxY = -1e30f;
            for (size_t i = 0; i + 2 < outline_.size(); i += 3) {
                const float x = outline_[i] * right[0] + outline_[i + 1] * right[1];
                const float y = outline_[i] * up[0] + outline_[i + 1] * up[1] + outline_[i + 2] * up[2];
                minX = std::min(minX, x); maxX = std::max(maxX, x);
                minY = std::min(minY, y); maxY = std::max(maxY, y);
            }
            if (minX > maxX) { minX = minY = -radius_; maxX = maxY = radius_; }
            const float usable = 0.86f;  // leave a margin, and room for the mark
            const float perPixel = std::max({(maxX - minX) / (w * usable), (maxY - minY) / (h * usable), radius_ * 1e-6f}) / zoom_;
            const float hw = 0.5f * w * perPixel, hh = 0.5f * h * perPixel;
            const float cx = 0.5f * (minX + maxX) - panX_ * perPixel, cy = 0.5f * (minY + maxY) - panY_ * perPixel;
            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glOrtho(cx - hw, cx + hw, cy - hh, cy + hh, -radius_ * 4, radius_ * 4);
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();
            const float light[4] = {0.35f, 0.45f, 1.0f, 0.0f};  // fixed to the camera, like the thumbnail
            const float diffuse[4] = {0.78f, 0.78f, 0.78f, 1}, ambient[4] = {0.38f, 0.38f, 0.38f, 1}, none[4] = {0, 0, 0, 1};
            glLightfv(GL_LIGHT0, GL_POSITION, light);
            glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);
            glLightfv(GL_LIGHT0, GL_SPECULAR, none);
            glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);
            glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, 1);
            glRotatef(elevation_ - 90.0f, 1, 0, 0);
            glRotatef(-azimuth_ - 90.0f, 0, 0, 1);
            glEnable(GL_DEPTH_TEST);
            glEnable(GL_LIGHTING);
            glEnable(GL_LIGHT0);
            glEnable(GL_COLOR_MATERIAL);
            glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
            glEnable(GL_NORMALIZE);
            glEnable(GL_MULTISAMPLE);
            glShadeModel(GL_SMOOTH);
            glCallList(list_);
            glDisable(GL_LIGHTING);
            glDisable(GL_DEPTH_TEST);
        }

        // Overlay in pixel coordinates, origin bottom-left.
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, w, 0, h, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        const float margin = 14 * dpi, size = 36 * dpi;
        if (logoDirty_) BuildLogo((int)size);
        if (logo_ && w > size * 3 && h > size * 3) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, logo_);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
            const float x0 = w - margin - size, y0 = margin;
            glBegin(GL_QUADS);
            glTexCoord2f(0, 0); glVertex2f(x0, y0);
            glTexCoord2f(1, 0); glVertex2f(x0 + size, y0);
            glTexCoord2f(1, 1); glVertex2f(x0 + size, y0 + size);
            glTexCoord2f(0, 1); glVertex2f(x0, y0 + size);
            glEnd();
            glDisable(GL_TEXTURE_2D);
        }
        if (!loaded_) Text("Loading preview...", margin, margin + 4 * dpi);
        else if (failed_) Text("No preview for this file.", margin, margin + 4 * dpi);
        else if (!touched_) Text("Drag to spin. Scroll to zoom.", margin, margin + 4 * dpi);
        SwapBuffers(dc_);
    }

    HWND hwnd_ = nullptr;
    HDC dc_ = nullptr;
    HGLRC glrc_ = nullptr;
    bool glFailed_ = false;
    GLuint list_ = 0, font_ = 0, logo_ = 0;
    bool logoDirty_ = true;
    std::shared_ptr<LoadState> state_;
    bool loaded_ = false, failed_ = false, touched_ = false;
    float centre_[3] = {0, 0, 0};
    std::vector<float> outline_;  // xyz triples, relative to centre_
    float radius_ = 1;
    float azimuth_ = -45.0f, elevation_ = 30.1f, zoom_ = 1.0f;
    float panX_ = 0, panY_ = 0;
    POINT last_ = {0, 0};
    COLORREF background_ = RGB(255, 255, 255), textColor_ = RGB(96, 96, 96);
};

// ---- the COM object ------------------------------------------------------------

class PreviewHandler final : public IPreviewHandler,
                             public IPreviewHandlerVisuals,
                             public IOleWindow,
                             public IObjectWithSite,
                             public IInitializeWithStream {
public:
    PreviewHandler() { InterlockedIncrement(&g_refs); }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == __uuidof(IPreviewHandler)) *ppv = static_cast<IPreviewHandler*>(this);
        else if (riid == __uuidof(IPreviewHandlerVisuals)) *ppv = static_cast<IPreviewHandlerVisuals*>(this);
        else if (riid == IID_IOleWindow) *ppv = static_cast<IOleWindow*>(this);
        else if (riid == IID_IObjectWithSite) *ppv = static_cast<IObjectWithSite*>(this);
        else if (riid == __uuidof(IInitializeWithStream)) *ppv = static_cast<IInitializeWithStream*>(this);
        else { *ppv = nullptr; return E_NOINTERFACE; }
        AddRef();
        return S_OK;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&ref_); }
    IFACEMETHODIMP_(ULONG) Release() override {
        const ULONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return r;
    }

    // IInitializeWithStream
    IFACEMETHODIMP Initialize(IStream* stream, DWORD) override {
        if (stream_) return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
        if (!stream) return E_INVALIDARG;
        stream_ = stream;
        stream_->AddRef();
        return S_OK;
    }

    // IPreviewHandler
    IFACEMETHODIMP SetWindow(HWND parent, const RECT* rc) override {
        if (!parent || !rc) return E_INVALIDARG;
        parent_ = parent;
        rect_ = *rc;
        if (view_) {
            SetParent(view_, parent_);
            MoveWindow(view_, rect_.left, rect_.top, rect_.right - rect_.left, rect_.bottom - rect_.top, TRUE);
        }
        return S_OK;
    }
    IFACEMETHODIMP SetRect(const RECT* rc) override {
        if (!rc) return E_INVALIDARG;
        rect_ = *rc;
        if (view_) MoveWindow(view_, rect_.left, rect_.top, rect_.right - rect_.left, rect_.bottom - rect_.top, TRUE);
        return S_OK;
    }
    IFACEMETHODIMP DoPreview() override {
        const HRESULT hr = DoPreviewImpl();
        DebugLog("DoPreview", hr);
        return hr;
    }
    IFACEMETHODIMP Unload() override {
        if (view_) { DestroyWindow(view_); view_ = nullptr; }
        if (stream_) { stream_->Release(); stream_ = nullptr; }
        return S_OK;
    }
    IFACEMETHODIMP SetFocus() override {
        if (!view_) return S_FALSE;
        ::SetFocus(view_);
        return S_OK;
    }
    IFACEMETHODIMP QueryFocus(HWND* phwnd) override {
        if (!phwnd) return E_POINTER;
        *phwnd = ::GetFocus();
        return *phwnd ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    }
    IFACEMETHODIMP TranslateAccelerator(MSG* msg) override {
        return frame_ ? frame_->TranslateAccelerator(msg) : S_FALSE;
    }

    // IPreviewHandlerVisuals
    IFACEMETHODIMP SetBackgroundColor(COLORREF color) override {
        background_ = color;
        if (view_) View::From(view_)->SetColors(background_, textColor_);
        return S_OK;
    }
    IFACEMETHODIMP SetFont(const LOGFONTW*) override { return S_OK; }
    IFACEMETHODIMP SetTextColor(COLORREF color) override {
        textColor_ = color;
        if (view_) View::From(view_)->SetColors(background_, textColor_);
        return S_OK;
    }

    // IOleWindow
    IFACEMETHODIMP GetWindow(HWND* phwnd) override {
        if (!phwnd) return E_POINTER;
        *phwnd = parent_;
        return S_OK;
    }
    IFACEMETHODIMP ContextSensitiveHelp(BOOL) override { return E_NOTIMPL; }

    // IObjectWithSite
    IFACEMETHODIMP SetSite(IUnknown* site) override {
        if (frame_) { frame_->Release(); frame_ = nullptr; }
        if (site_) { site_->Release(); site_ = nullptr; }
        site_ = site;
        if (site_) {
            site_->AddRef();
            site_->QueryInterface(__uuidof(IPreviewHandlerFrame), reinterpret_cast<void**>(&frame_));
        }
        return S_OK;
    }
    IFACEMETHODIMP GetSite(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        return site_ ? site_->QueryInterface(riid, ppv) : E_FAIL;
    }

private:
    ~PreviewHandler() {
        Unload();
        SetSite(nullptr);
        InterlockedDecrement(&g_refs);
    }

    HRESULT DoPreviewImpl() {
        if (view_) return S_OK;
        if (!stream_ || !parent_) return E_UNEXPECTED;
        view_ = View::Create(parent_, rect_, background_, textColor_);
        if (!view_) return HRESULT_FROM_WIN32(GetLastError());

        // The stream belongs to this thread's apartment, so read it here; the
        // slow part (parsing and meshing) happens on the worker.
        auto state = std::make_shared<LoadState>();
        STATSTG st = {};
        HRESULT hr = stream_->Stat(&st, STATFLAG_NONAME);
        if (SUCCEEDED(hr) && (st.cbSize.QuadPart == 0 || st.cbSize.QuadPart > kMaxFileBytes)) hr = E_FAIL;
        if (SUCCEEDED(hr)) {
            try { state->data.resize((size_t)st.cbSize.QuadPart); } catch (...) { hr = E_OUTOFMEMORY; }
        }
        size_t got = 0;
        while (SUCCEEDED(hr) && got < state->data.size()) {
            ULONG n = 0;
            const ULONG want = (ULONG)std::min<size_t>(state->data.size() - got, 1u << 20);
            hr = stream_->Read(state->data.data() + got, want, &n);
            if (n == 0) break;
            got += n;
        }
        View* v = View::From(view_);
        v->Start(state);
        if (FAILED(hr) || got == 0) {
            state->done = true;
            state->ok = false;
            PostMessageW(view_, WM_MESH_READY, 0, 0);
            return S_OK;  // the pane shows "No preview" rather than an error box
        }
        state->data.resize(got);

        // The worker holds its own reference on the DLL until it has exited.
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&LoadThread), &self);
        InterlockedIncrement(&g_refs);
        auto* holder = new std::shared_ptr<LoadState>(state);
        HANDLE thread = CreateThread(nullptr, 0, &LoadThread, holder, 0, nullptr);
        if (!thread) {
            delete holder;
            InterlockedDecrement(&g_refs);
            if (self) FreeLibrary(self);
            state->done = true;
            PostMessageW(view_, WM_MESH_READY, 0, 0);
            return S_OK;
        }
        CloseHandle(thread);
        return S_OK;
    }

    LONG ref_ = 1;
    IStream* stream_ = nullptr;
    IUnknown* site_ = nullptr;
    IPreviewHandlerFrame* frame_ = nullptr;
    HWND parent_ = nullptr, view_ = nullptr;
    RECT rect_ = {0, 0, 0, 0};
    COLORREF background_ = RGB(255, 255, 255), textColor_ = RGB(96, 96, 96);
};

}  // namespace

HRESULT CreatePreviewHandler(REFIID riid, void** ppv) {
    PreviewHandler* p = new (std::nothrow) PreviewHandler();
    if (!p) return E_OUTOFMEMORY;
    const HRESULT hr = p->QueryInterface(riid, ppv);
    p->Release();
    return hr;
}
