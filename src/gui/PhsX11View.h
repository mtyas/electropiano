#pragma once

#include "PhsEditor.hpp"

#if defined(__linux__) && !defined(__ANDROID__)
#include <X11/Xlib.h>
#else
typedef unsigned long Window;
#endif

namespace phs {

#if defined(__linux__) && !defined(__ANDROID__)
// Child X11 window hosting the shared editor, drawn with the software canvas. The editor
// runs on its own thread with its own display connection.
// requestResize asks the plugin host for a new size (it is called from the editor's thread; return
// false where the host cannot be asked from there, and the zoom applies when the editor reopens)
using X11ResizeRequest = std::function<bool(int width, int height)>;
Window createPhsX11EditorView(Window parent, PhsSynthEngine* engine, ParamChangeCallback onParamChange = nullptr,
                              X11ResizeRequest requestResize = nullptr);
void destroyPhsX11EditorView(Window win);
// Host-driven resize (any thread; applied by the editor's thread)
void setPhsX11EditorSize(Window win, int width, int height);
#endif

} // namespace phs
