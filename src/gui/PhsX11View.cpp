#if defined(__linux__) && !defined(__ANDROID__)
#include "PhsX11View.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cstring>

namespace phs {

Window createPhsX11EditorView(Window parent, PhsSynthEngine* /*engine*/, ParamChangeCallback /*onParamChange*/) {
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) return 0;

    int screen = DefaultScreen(dpy);
    Window win = XCreateSimpleWindow(
        dpy, parent,
        0, 0, kEditorWidth, kEditorHeight, 0,
        BlackPixel(dpy, screen),
        WhitePixel(dpy, screen)
    );

    XSelectInput(dpy, win, ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask);
    XMapWindow(dpy, win);
    XFlush(dpy);
    return win;
}

void destroyPhsX11EditorView(Window win) {
    if (!win) return;
    Display* dpy = XOpenDisplay(nullptr);
    if (dpy) {
        XDestroyWindow(dpy, win);
        XCloseDisplay(dpy);
    }
}

} // namespace phs
#endif // __linux__
