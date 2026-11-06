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
#include <algorithm>
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
    int width = phs::kEditorWidth, height = phs::kEditorHeight;
    float zoom = 1.0f;
    std::atomic<int> pendingW{0}, pendingH{0};   // a size asked for by the host or the zoom control
    phs::X11ResizeRequest requestResize;
};

// (Re)creates the canvas and the XImage over it for a window size; editor thread only
bool rebuildImage(X11Editor& e, int w, int h) {
    w = std::max(1, w);
    h = std::max(1, h);
    const int screen = DefaultScreen(e.dpy);
    e.canvas.resize(w, h);
    e.converted.assign(static_cast<size_t>(w) * h, 0);
    if (e.image) {
        e.image->data = nullptr; // the pixels belong to the canvas
        XDestroyImage(e.image);
    }
    e.image = XCreateImage(e.dpy, DefaultVisual(e.dpy, screen), DefaultDepth(e.dpy, screen), ZPixmap, 0,
                           reinterpret_cast<char*>(e.canvas.pixels()), w, h, 32, w * 4);
    if (!e.image) return false;
    const uint32_t probe = 1;
    e.image->byte_order = (*reinterpret_cast<const uint8_t*>(&probe) == 1) ? LSBFirst : MSBFirst;
    e.width = w;
    e.height = h;
    e.zoom = std::max(0.25f, std::min(w / static_cast<float>(phs::kEditorWidth), h / static_cast<float>(phs::kEditorHeight)));
    if (e.editor) e.editor->setZoom(e.zoom);
    return true;
}

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

// full: repaint everything; otherwise only the regions the editor reports as changed
void present(X11Editor& e, bool full) {
    if (!e.image) return;
    int px0 = 0, py0 = 0, px1 = e.width, py1 = e.height; // pixels to send, top-down
    {
        phs::gui::ScaledCanvas scaled(e.canvas, e.zoom);
        const std::vector<phs::gui::Rect> dirty = full ? std::vector<phs::gui::Rect>{} : e.editor->dirtyRects();
        const bool whole = full || (dirty.size() == 1 && dirty[0].w >= phs::kEditorWidth && dirty[0].h >= phs::kEditorHeight);
        if (whole) {
            e.editor->draw(scaled);
        } else {
            if (dirty.empty()) return;
            px0 = e.width; py0 = e.height; px1 = py1 = 0;
            for (const auto& r : dirty) {
                e.canvas.save();
                scaled.clip(r);
                e.editor->draw(scaled, r);
                e.canvas.restore();
                px0 = std::min(px0, static_cast<int>(r.x * e.zoom) - 1);
                px1 = std::max(px1, static_cast<int>(r.maxX() * e.zoom) + 2);
                py0 = std::min(py0, static_cast<int>((phs::kEditorHeight - r.maxY()) * e.zoom) - 1);
                py1 = std::max(py1, static_cast<int>((phs::kEditorHeight - r.y) * e.zoom) + 2);
            }
            px0 = std::max(0, px0); py0 = std::max(0, py0);
            px1 = std::min(e.width, px1); py1 = std::min(e.height, py1);
            if (px0 >= px1 || py0 >= py1) return;
        }
    }
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
    XPutImage(e.dpy, e.win, e.gc, e.image, px0, py0, px0, py0, px1 - px0, py1 - py0);
    XFlush(e.dpy);
}

void eventLoop(X11Editor* e) {
    const int fd = ConnectionNumber(e->dpy);
    const float H = static_cast<float>(phs::kEditorHeight);
    auto lx = [e](int px) { return static_cast<float>(px) / e->zoom; };
    auto ly = [e, H](int py) { return H - static_cast<float>(py) / e->zoom; };
    bool needsPaint = true;
    while (e->running.load()) {
        const int pw = e->pendingW.exchange(0), ph = e->pendingH.exchange(0);
        if (pw > 0 && ph > 0) {
            XResizeWindow(e->dpy, e->win, static_cast<unsigned>(pw), static_cast<unsigned>(ph));
            rebuildImage(*e, pw, ph);
            needsPaint = true;
        }
        while (XPending(e->dpy)) {
            XEvent ev;
            XNextEvent(e->dpy, &ev);
            switch (ev.type) {
                case Expose:
                    needsPaint = true;
                    break;
                case ConfigureNotify:
                    if (ev.xconfigure.width != e->width || ev.xconfigure.height != e->height) {
                        rebuildImage(*e, ev.xconfigure.width, ev.xconfigure.height);
                        needsPaint = true;
                    }
                    break;
                case ButtonPress: {
                    const float x = lx(ev.xbutton.x), y = ly(ev.xbutton.y);
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
                        e->editor->mouseDrag(lx(ev.xmotion.x), ly(ev.xmotion.y));
                        needsPaint = true;
                    }
                    break;
                case ButtonRelease:
                    if (ev.xbutton.button == Button1) {
                        e->editor->mouseUp(lx(ev.xbutton.x), ly(ev.xbutton.y));
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
        present(*e, needsPaint);
        needsPaint = false;
        pollfd pfd{fd, POLLIN, 0};
        poll(&pfd, 1, 16); // wake on X events, otherwise ~60 fps (most ticks repaint little or nothing)
    }
}

} // namespace

namespace phs {

Window createPhsX11EditorView(Window parent, PhsSynthEngine* engine, ParamChangeCallback onParamChange,
                              X11ResizeRequest requestResize) {
    XInitThreads();
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) return 0;

    auto* e = new X11Editor();
    e->dpy = dpy;
    e->requestResize = std::move(requestResize);
    const float zoom = loadEditorZoom();
    const int screen = DefaultScreen(dpy);
    e->win = XCreateSimpleWindow(dpy, parent, 0, 0, scaledEditorWidth(zoom), scaledEditorHeight(zoom), 0,
                                 BlackPixel(dpy, screen), BlackPixel(dpy, screen));
    XSelectInput(dpy, e->win, ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | KeyPressMask | StructureNotifyMask);
    e->gc = XCreateGC(dpy, e->win, 0, nullptr);

    gui::EditorHost host;
    host.paramChanged = std::move(onParamChange);
    host.openUrl = openUrl;
    // The zoom control runs on the editor thread: ask the host, then resize on this thread
    host.requestZoom = [e](float z) {
        const int w = scaledEditorWidth(z), h = scaledEditorHeight(z);
        if (e->requestResize && !e->requestResize(w, h)) return;
        e->pendingW = w;
        e->pendingH = h;
    };
    e->editor = std::make_unique<gui::Editor>(engine, std::move(host));
    if (!rebuildImage(*e, scaledEditorWidth(zoom), scaledEditorHeight(zoom))) {
        XFreeGC(dpy, e->gc);
        XDestroyWindow(dpy, e->win);
        XCloseDisplay(dpy);
        delete e;
        return 0;
    }
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

void setPhsX11EditorSize(Window win, int width, int height) {
    std::lock_guard<std::mutex> lock(gEditorsMutex);
    auto it = gEditors.find(win);
    if (it == gEditors.end()) return;
    it->second->pendingW = width;
    it->second->pendingH = height;
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
