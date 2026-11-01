// Linux host for the shared editor: a child X11 window with its own display connection and
// event thread, rendering phs::gui::Editor through the portable software canvas.
#if defined(__linux__) && !defined(__ANDROID__)
#include "PhsX11View.h"
#include "PhsSoftwareCanvas.hpp"
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

extern char** environ;

namespace {

struct X11Editor {
    Display* dpy = nullptr;
    Window win = 0;
    GC gc = nullptr;
    XImage* image = nullptr;
    std::vector<uint32_t> converted; // used only when the visual is not 0x00RRGGBB
    phs::gui::SoftwareCanvas canvas;
    std::unique_ptr<phs::gui::Editor> editor;
    std::thread thread;
    std::atomic<bool> running{false};
};

std::mutex gEditorsMutex;
std::map<Window, X11Editor*> gEditors;

double nowSeconds() {
    using namespace std::chrono;
    return duration_cast<duration<double>>(steady_clock::now().time_since_epoch()).count();
}

void openUrl(const std::string& url) {
    pid_t pid;
    char* argv[] = {const_cast<char*>("xdg-open"), const_cast<char*>(url.c_str()), nullptr};
    if (posix_spawnp(&pid, "xdg-open", nullptr, nullptr, argv, environ) == 0) {
        std::thread([pid] { int status; waitpid(pid, &status, 0); }).detach();
    }
}

void present(X11Editor& e) {
    e.editor->draw(e.canvas);
    Visual* vis = DefaultVisual(e.dpy, DefaultScreen(e.dpy));
    if (vis->red_mask != 0xFF0000 || vis->green_mask != 0x00FF00 || vis->blue_mask != 0x0000FF) {
        // Unusual visual: repack each channel into the server's masks
        auto place = [](uint32_t v, unsigned long mask) {
            int shift = 0;
            while (mask && !(mask & 1)) { mask >>= 1; ++shift; }
            int bits = 0;
            while (mask & 1) { mask >>= 1; ++bits; }
            return static_cast<uint32_t>((v >> (8 - bits)) << shift);
        };
        const uint32_t* src = e.canvas.pixels();
        for (size_t i = 0; i < e.converted.size(); ++i) {
            const uint32_t p = src[i];
            e.converted[i] = place((p >> 16) & 0xFF, vis->red_mask) | place((p >> 8) & 0xFF, vis->green_mask) |
                             place(p & 0xFF, vis->blue_mask);
        }
        e.image->data = reinterpret_cast<char*>(e.converted.data());
    }
    XPutImage(e.dpy, e.win, e.gc, e.image, 0, 0, 0, 0, phs::kEditorWidth, phs::kEditorHeight);
    XFlush(e.dpy);
}

void eventLoop(X11Editor* e) {
    const int fd = ConnectionNumber(e->dpy);
    const float H = static_cast<float>(phs::kEditorHeight);
    bool needsPaint = true;
    while (e->running.load()) {
        while (XPending(e->dpy)) {
            XEvent ev;
            XNextEvent(e->dpy, &ev);
            switch (ev.type) {
                case Expose:
                    needsPaint = true;
                    break;
                case ButtonPress: {
                    const float x = static_cast<float>(ev.xbutton.x), y = H - ev.xbutton.y;
                    if (ev.xbutton.button == Button1) {
                        XSetInputFocus(e->dpy, e->win, RevertToParent, CurrentTime);
                        e->editor->mouseDown(x, y, (ev.xbutton.state & ShiftMask) != 0, nowSeconds());
                    } else if (ev.xbutton.button == Button3) {
                        e->editor->rightMouseDown(x, y);
                    } else if (ev.xbutton.button == Button4 || ev.xbutton.button == Button5) {
                        e->editor->scroll(x, y, ev.xbutton.button == Button4 ? 1.0f : -1.0f);
                    }
                    needsPaint = true;
                    break;
                }
                case MotionNotify:
                    if (ev.xmotion.state & Button1Mask) {
                        e->editor->mouseDrag(static_cast<float>(ev.xmotion.x), H - ev.xmotion.y);
                        needsPaint = true;
                    }
                    break;
                case ButtonRelease:
                    if (ev.xbutton.button == Button1) {
                        e->editor->mouseUp(static_cast<float>(ev.xbutton.x), H - ev.xbutton.y);
                        needsPaint = true;
                    }
                    break;
                case KeyPress: {
                    char buf[16];
                    KeySym ks = 0;
                    const int n = XLookupString(&ev.xkey, buf, sizeof(buf), &ks, nullptr);
                    if (ks == XK_Escape) e->editor->keyAction(phs::gui::Key::Escape);
                    else if (ks == XK_Return || ks == XK_KP_Enter) e->editor->keyAction(phs::gui::Key::Enter);
                    else if (ks == XK_BackSpace) e->editor->keyAction(phs::gui::Key::Backspace);
                    else for (int i = 0; i < n; ++i) e->editor->keyChar(static_cast<unsigned char>(buf[i]));
                    needsPaint = true;
                    break;
                }
                default:
                    break;
            }
        }
        if (needsPaint || e->editor->wantsRedraw()) {
            present(*e);
            needsPaint = false;
        }
        pollfd pfd{fd, POLLIN, 0};
        poll(&pfd, 1, 33); // wake on X events, otherwise ~30 fps
    }
}

} // namespace

namespace phs {

Window createPhsX11EditorView(Window parent, PhsSynthEngine* engine, ParamChangeCallback onParamChange) {
    XInitThreads();
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) return 0;

    auto* e = new X11Editor();
    e->dpy = dpy;
    const int screen = DefaultScreen(dpy);
    e->win = XCreateSimpleWindow(dpy, parent, 0, 0, kEditorWidth, kEditorHeight, 0, BlackPixel(dpy, screen), BlackPixel(dpy, screen));
    XSelectInput(dpy, e->win, ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | KeyPressMask | StructureNotifyMask);
    e->gc = XCreateGC(dpy, e->win, 0, nullptr);

    gui::EditorHost host;
    host.paramChanged = std::move(onParamChange);
    host.openUrl = openUrl;
    e->editor = std::make_unique<gui::Editor>(engine, std::move(host));
    e->canvas.resize(kEditorWidth, kEditorHeight);
    e->converted.resize(static_cast<size_t>(kEditorWidth) * kEditorHeight);

    e->image = XCreateImage(dpy, DefaultVisual(dpy, screen), DefaultDepth(dpy, screen), ZPixmap, 0,
                            reinterpret_cast<char*>(e->canvas.pixels()), kEditorWidth, kEditorHeight, 32, kEditorWidth * 4);
    if (!e->image) {
        XFreeGC(dpy, e->gc);
        XDestroyWindow(dpy, e->win);
        XCloseDisplay(dpy);
        delete e;
        return 0;
    }
    const uint32_t probe = 1;
    e->image->byte_order = (*reinterpret_cast<const uint8_t*>(&probe) == 1) ? LSBFirst : MSBFirst;

    XMapWindow(dpy, e->win);
    XFlush(dpy);
    const Window win = e->win;
    {
        std::lock_guard<std::mutex> lock(gEditorsMutex);
        gEditors[win] = e;
    }
    e->running = true;
    e->thread = std::thread(eventLoop, e); // from here on only the editor thread uses dpy
    return win;
}

void destroyPhsX11EditorView(Window win) {
    X11Editor* e = nullptr;
    {
        std::lock_guard<std::mutex> lock(gEditorsMutex);
        auto it = gEditors.find(win);
        if (it == gEditors.end()) return;
        e = it->second;
        gEditors.erase(it);
    }
    e->running = false;
    if (e->thread.joinable()) e->thread.join();
    e->editor->detachEngine();
    e->image->data = nullptr; // the pixels belong to the canvas
    XDestroyImage(e->image);
    XFreeGC(e->dpy, e->gc);
    XDestroyWindow(e->dpy, e->win);
    XCloseDisplay(e->dpy);
    delete e;
}

} // namespace phs

#endif // __linux__
