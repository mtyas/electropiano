#pragma once

#include "../dsp/PhsSynthEngine.hpp"
#include <functional>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
typedef void* HWND;
#endif

namespace phs {

using ParamChangeCallback = std::function<void(uint32_t paramId, float normalizedValue)>;

static constexpr int kEditorWidth = 1000;
static constexpr int kEditorHeight = 680;

#if defined(_WIN32)
HWND createPhsWin32EditorView(HWND parent, PhsSynthEngine* engine, ParamChangeCallback onParamChange = nullptr);
void destroyPhsWin32EditorView(HWND hwnd);
#endif

} // namespace phs
