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
Window createPhsX11EditorView(Window parent, PhsSynthEngine* engine, ParamChangeCallback onParamChange = nullptr);
void destroyPhsX11EditorView(Window win);
#endif

} // namespace phs
