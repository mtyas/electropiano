// Windows host for the shared editor: a child window that renders phs::gui::Editor through
// the portable software canvas and blits it as a top-down 32-bit DIB.
#if defined(_WIN32)
#include "PhsWin32View.h"
#include "PhsSoftwareCanvas.hpp"
#include <windowsx.h>
#include <shellapi.h>
#include <memory>

namespace {

struct Win32Editor {
    std::unique_ptr<phs::gui::Editor> editor;
    phs::gui::SoftwareCanvas canvas;
};

const wchar_t* kClassName = L"electropianoEditor";

HINSTANCE moduleInstance() {
    HMODULE m = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&kClassName), &m);
    return m;
}

float logicalY(LPARAM lp) { return static_cast<float>(phs::kEditorHeight - GET_Y_LPARAM(lp)); }

LRESULT CALLBACK EditorWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<Win32Editor*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            SetTimer(hwnd, 1, 33, nullptr); // ~30 fps; repaints only when something changed
            return 0;
        }
        case WM_TIMER:
            if (self && self->editor->wantsRedraw()) InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (self) {
                auto& c = self->canvas;
                if (c.width() != phs::kEditorWidth || c.height() != phs::kEditorHeight) c.resize(phs::kEditorWidth, phs::kEditorHeight);
                self->editor->draw(c);
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
                self->editor->mouseDown(static_cast<float>(GET_X_LPARAM(lp)), logicalY(lp), (wp & MK_SHIFT) != 0,
                                        GetMessageTime() / 1000.0);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_MOUSEMOVE:
            if (self && (wp & MK_LBUTTON)) {
                self->editor->mouseDrag(static_cast<float>(GET_X_LPARAM(lp)), logicalY(lp));
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONUP:
            if (self) {
                ReleaseCapture();
                self->editor->mouseUp(static_cast<float>(GET_X_LPARAM(lp)), logicalY(lp));
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_RBUTTONDOWN:
            if (self) {
                self->editor->rightMouseDown(static_cast<float>(GET_X_LPARAM(lp)), logicalY(lp));
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_MOUSEWHEEL:
            if (self) {
                POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                ScreenToClient(hwnd, &pt);
                self->editor->scroll(static_cast<float>(pt.x), static_cast<float>(phs::kEditorHeight - pt.y),
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

HWND createPhsWin32EditorView(HWND parent, PhsSynthEngine* engine, ParamChangeCallback onParamChange) {
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
    gui::EditorHost host;
    host.paramChanged = std::move(onParamChange);
    host.openUrl = [](const std::string& url) { ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL); };
    self->editor = std::make_unique<gui::Editor>(engine, std::move(host));
    self->canvas.resize(kEditorWidth, kEditorHeight);
    HWND hwnd = CreateWindowExW(0, kClassName, L"electropiano", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0,
                                kEditorWidth, kEditorHeight, parent, nullptr, inst, self);
    if (!hwnd) delete self;
    return hwnd;
}

void destroyPhsWin32EditorView(HWND hwnd) {
    if (!hwnd) return;
    if (auto* self = reinterpret_cast<Win32Editor*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) self->editor->detachEngine();
    DestroyWindow(hwnd); // WM_DESTROY frees the editor
}

} // namespace phs

#endif // _WIN32
