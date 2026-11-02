#pragma once

#include "PhsEditor.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
typedef void* HWND;
#endif

namespace phs {

#if defined(_WIN32)
// Asks the plugin host for a new editor size (pixels); returns true if the host agreed
using Win32ResizeRequest = std::function<bool(int width, int height)>;

// Child window hosting the shared editor, drawn with the software canvas at the saved zoom;
// it scales to any size it is given (see setPhsWin32EditorSize)
HWND createPhsWin32EditorView(HWND parent, PhsSynthEngine* engine, ParamChangeCallback onParamChange = nullptr,
                              Win32ResizeRequest requestResize = nullptr);
void destroyPhsWin32EditorView(HWND hwnd);
// Host-driven resize
void setPhsWin32EditorSize(HWND hwnd, int width, int height);
#endif

} // namespace phs
