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
// Child window hosting the shared editor, drawn with the software canvas
HWND createPhsWin32EditorView(HWND parent, PhsSynthEngine* engine, ParamChangeCallback onParamChange = nullptr);
void destroyPhsWin32EditorView(HWND hwnd);
#endif

} // namespace phs
