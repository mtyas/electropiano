// Windows host for the shared editor: a child window that renders phs::gui::Editor through
// the portable software canvas and blits it as a top-down 32-bit DIB.
#if defined(_WIN32)
#include "PhsWin32View.h"
#include "PhsSoftwareCanvas.hpp"
#include <windowsx.h>
#include <shellapi.h>
#include <commdlg.h>
#include <algorithm>
#include <memory>

namespace {

struct Win32Editor {
    std::unique_ptr<phs::gui::Editor> editor;
    phs::gui::SoftwareCanvas canvas;
    float zoom = 1.0f;
    phs::Win32ResizeRequest requestResize;
    HWND hwnd = nullptr;
};

const wchar_t* kClassName = L"electropianoEditor";

HINSTANCE moduleInstance() {
    HMODULE m = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&kClassName), &m);
    return m;
}

// Window pixels to the editor's logical coordinates (origin bottom-left)
float logicalX(const Win32Editor* e, int px) { return static_cast<float>(px) / e->zoom; }
float logicalY(const Win32Editor* e, int py) { return static_cast<float>(phs::kEditorHeight) - static_cast<float>(py) / e->zoom; }
float logicalX(const Win32Editor* e, LPARAM lp) { return logicalX(e, GET_X_LPARAM(lp)); }
float logicalY(const Win32Editor* e, LPARAM lp) { return logicalY(e, GET_Y_LPARAM(lp)); }

void setZoomFromSize(Win32Editor* e, int w, int h) {
    e->zoom = std::max(0.25f, std::min(w / static_cast<float>(phs::kEditorWidth), h / static_cast<float>(phs::kEditorHeight)));
    if (e->editor) e->editor->setZoom(e->zoom);
}

LRESULT CALLBACK EditorWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<Win32Editor*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            SetTimer(hwnd, 1, 16, nullptr); // ~60 fps; repaints only what changed
            return 0;
        }
        case WM_TIMER:
            if (self) {
                // Meters, keys and analysers repaint their own regions; anything else, the window
                for (const phs::gui::Rect& r : self->editor->dirtyRects()) {
                    if (r.w >= phs::kEditorWidth && r.h >= phs::kEditorHeight) { InvalidateRect(hwnd, nullptr, FALSE); break; }
                    const float z = self->zoom;
                    RECT rc{static_cast<LONG>(r.x * z) - 1, static_cast<LONG>((phs::kEditorHeight - r.maxY()) * z) - 1,
                            static_cast<LONG>(r.maxX() * z) + 2, static_cast<LONG>((phs::kEditorHeight - r.y) * z) + 2};
                    InvalidateRect(hwnd, &rc, FALSE);
                }
            }
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE:
            if (self) {
                setZoomFromSize(self, LOWORD(lp), HIWORD(lp));
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (self) {
                auto& c = self->canvas;
                RECT rc;
                GetClientRect(hwnd, &rc);
                const int w = std::max(1, static_cast<int>(rc.right - rc.left)), h = std::max(1, static_cast<int>(rc.bottom - rc.top));
                const bool resized = c.width() != w || c.height() != h;
                if (resized) c.resize(w, h);
                phs::gui::ScaledCanvas scaled(c, self->zoom);
                // The canvas keeps the last frame, so a partial update repaints only its area
                // (GDI clips the blit below to the same area)
                const RECT& u = ps.rcPaint;
                const bool full = resized || (u.left <= 0 && u.top <= 0 && u.right >= w && u.bottom >= h);
                if (full) {
                    self->editor->draw(scaled);
                } else {
                    const float z = self->zoom;
                    const phs::gui::Rect area{u.left / z - 1.0f, phs::kEditorHeight - u.bottom / z - 1.0f,
                                              (u.right - u.left) / z + 2.0f, (u.bottom - u.top) / z + 2.0f};
                    c.save();
                    scaled.clip(area);
                    self->editor->draw(scaled, area);
                    c.restore();
                }
                BITMAPINFO bmi{};
                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = c.width();
                bmi.bmiHeader.biHeight = -c.height(); // top-down
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 32;
                bmi.bmiHeader.biCompression = BI_RGB;
                SetDIBitsToDevice(hdc, 0, 0, c.width(), c.height(), 0, 0, 0, c.height(), c.pixels(), &bmi, DIB_RGB_COLORS);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
            if (self) {
                SetFocus(hwnd);
                SetCapture(hwnd);
                self->editor->mouseDown(logicalX(self, lp), logicalY(self, lp), (wp & MK_SHIFT) != 0,
                                        GetMessageTime() / 1000.0);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_MOUSEMOVE:
            if (self && (wp & MK_LBUTTON)) {
                self->editor->mouseDrag(logicalX(self, lp), logicalY(self, lp));
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONUP:
            if (self) {
                ReleaseCapture();
                self->editor->mouseUp(logicalX(self, lp), logicalY(self, lp));
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_RBUTTONDOWN:
            if (self) {
                self->editor->rightMouseDown(logicalX(self, lp), logicalY(self, lp));
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_MOUSEWHEEL:
            if (self) {
                POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                ScreenToClient(hwnd, &pt);
                self->editor->scroll(logicalX(self, static_cast<int>(pt.x)), logicalY(self, static_cast<int>(pt.y)),
                                     GET_WHEEL_DELTA_WPARAM(wp) / static_cast<float>(WHEEL_DELTA));
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_GETDLGCODE:
            return DLGC_WANTALLKEYS | DLGC_WANTCHARS;
        case WM_KEYDOWN:
            if (self) {
                bool handled = false;
                if (wp == VK_ESCAPE) handled = self->editor->keyAction(phs::gui::Key::Escape);
                else if (wp == VK_RETURN) handled = self->editor->keyAction(phs::gui::Key::Enter);
                else if (wp == VK_BACK) handled = self->editor->keyAction(phs::gui::Key::Backspace);
                if (handled) { InvalidateRect(hwnd, nullptr, FALSE); return 0; }
            }
            break;
        case WM_CHAR:
            if (self && wp >= 32 && self->editor->keyChar(static_cast<uint32_t>(wp))) {
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            break;
        case WM_DESTROY:
            KillTimer(hwnd, 1);
            if (self) {
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                delete self;
            }
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

namespace phs {

HWND createPhsWin32EditorView(HWND parent, PhsSynthEngine* engine, ParamChangeCallback onParamChange,
                              Win32ResizeRequest requestResize) {
    const HINSTANCE inst = moduleInstance();
    WNDCLASSEXW wc{};
    if (!GetClassInfoExW(inst, kClassName, &wc)) {
        wc.cbSize = sizeof(wc);
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = EditorWndProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        RegisterClassExW(&wc);
    }
    auto* self = new Win32Editor();
    self->zoom = loadEditorZoom();
    self->requestResize = std::move(requestResize);
    gui::EditorHost host;
    host.paramChanged = std::move(onParamChange);
    host.openUrl = [](const std::string& url) { ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL); };
    host.requestZoom = [self](float z) {
        const int w = scaledEditorWidth(z), h = scaledEditorHeight(z);
        if (self->requestResize && !self->requestResize(w, h)) return;
        if (self->hwnd) SetWindowPos(self->hwnd, nullptr, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    };
    host.chooseTuningFile = [self]() -> std::string {
        wchar_t file[MAX_PATH] = L"";
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = self->hwnd;
        ofn.lpstrFilter = L"Scala tuning (*.scl)\0*.scl\0All files\0*.*\0";
        ofn.lpstrFile = file;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameW(&ofn)) return {};
        char utf8[MAX_PATH * 4];
        const int n = WideCharToMultiByte(CP_UTF8, 0, file, -1, utf8, sizeof(utf8), nullptr, nullptr);
        return n > 0 ? std::string(utf8) : std::string();
    };
    self->editor = std::make_unique<gui::Editor>(engine, std::move(host));
    self->editor->setZoom(self->zoom);
    const int w = scaledEditorWidth(self->zoom), h = scaledEditorHeight(self->zoom);
    self->canvas.resize(w, h);
    HWND hwnd = CreateWindowExW(0, kClassName, L"electropiano", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0,
                                w, h, parent, nullptr, inst, self);
    if (!hwnd) delete self;
    else self->hwnd = hwnd;
    return hwnd;
}

void setPhsWin32EditorSize(HWND hwnd, int width, int height) {
    if (hwnd) SetWindowPos(hwnd, nullptr, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void destroyPhsWin32EditorView(HWND hwnd) {
    if (!hwnd) return;
    if (auto* self = reinterpret_cast<Win32Editor*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) self->editor->detachEngine();
    DestroyWindow(hwnd); // WM_DESTROY frees the editor
}

} // namespace phs

#endif // _WIN32
