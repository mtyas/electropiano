#if defined(_WIN32)
#include "PhsWin32View.h"
#include <windows.h>
#include <windowsx.h>
#include <cmath>
#include <string>

namespace {

const wchar_t* kWin32ClassName = L"electropianoEditor";

struct Win32EditorState {
    phs::PhsSynthEngine* engine = nullptr;
    phs::ParamChangeCallback paramCallback;
    UINT_PTR timerId = 0;
};

LRESULT CALLBACK EditorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<Win32EditorState*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
            state = reinterpret_cast<Win32EditorState*>(cs->lpCreateParams);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            state->timerId = SetTimer(hwnd, 1, 33, nullptr); // ~30 fps
            return 0;
        }
        case WM_TIMER: {
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            RECT rc;
            GetClientRect(hwnd, &rc);

            // Double buffering
            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
            HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(memDC, memBmp));

            // Background fill (Navy / Dark Slate)
            HBRUSH bgBrush = CreateSolidBrush(RGB(18, 22, 30));
            FillRect(memDC, &rc, bgBrush);
            DeleteObject(bgBrush);

            // Title text
            SetBkMode(memDC, TRANSPARENT);
            SetTextColor(memDC, RGB(240, 210, 110));
            HFONT titleFont = CreateFontW(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                          CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            HFONT oldFont = static_cast<HFONT>(SelectObject(memDC, titleFont));
            TextOutW(memDC, 20, 15, L"electropiano by mtyas", 20);

            SetTextColor(memDC, RGB(110, 190, 240));
            HFONT subFont = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            SelectObject(memDC, subFont);
            TextOutW(memDC, 22, 40, L"Port-Hamiltonian Rhodes & Wurlitzer Physical Modeling", 53);

            // Blit buffer to screen
            BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);

            SelectObject(memDC, oldFont);
            DeleteObject(titleFont);
            DeleteObject(subFont);
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);

            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DESTROY: {
            if (state) {
                if (state->timerId) KillTimer(hwnd, state->timerId);
                delete state;
                SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
            }
            return 0;
        }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

} // namespace

namespace phs {

HWND createPhsWin32EditorView(HWND parent, PhsSynthEngine* engine, ParamChangeCallback onParamChange) {
    HINSTANCE hInst = GetModuleHandle(nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = EditorWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = kWin32ClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    auto* state = new Win32EditorState();
    state->engine = engine;
    state->paramCallback = onParamChange;

    HWND hwnd = CreateWindowExW(
        0,
        kWin32ClassName,
        L"PyPHS Electric Piano",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, kEditorWidth, kEditorHeight,
        parent,
        nullptr,
        hInst,
        state
    );

    return hwnd;
}

void destroyPhsWin32EditorView(HWND hwnd) {
    if (hwnd && IsWindow(hwnd)) {
        DestroyWindow(hwnd);
    }
}

} // namespace phs
#endif // _WIN32
