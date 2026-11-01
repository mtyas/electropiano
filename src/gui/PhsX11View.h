#pragma once

#include "../dsp/PhsSynthEngine.hpp"
#include <functional>

#if defined(__linux__) && !defined(__ANDROID__)
#include <X11/Xlib.h>
#else
typedef unsigned long Window;
#endif

namespace phs {

using ParamChangeCallback = std::function<void(uint32_t paramId, float normalizedValue)>;

static constexpr int kEditorWidth = 1000;
static constexpr int kEditorHeight = 680;

#if defined(__linux__) && !defined(__ANDROID__)
Window createPhsX11EditorView(Window parent, PhsSynthEngine* engine, ParamChangeCallback onParamChange = nullptr);
void destroyPhsX11EditorView(Window win);
#endif

} // namespace phs
