#pragma once

#include "../dsp/PhsSynthEngine.hpp"
#include <functional>

#ifdef __OBJC__
#import <Cocoa/Cocoa.h>
#else
typedef void NSView;
#endif

namespace phs {

// Callback invoked when the user turns a knob or changes preset in the GUI,
// allowing the plugin wrapper (VST3 / CLAP / AU) to notify the host for automation.
using ParamChangeCallback = std::function<void(uint32_t paramId, float normalizedValue)>;

static constexpr int kEditorWidth = 1000;
static constexpr int kEditorHeight = 680;

// Creates a retained NSView* instance backed by our CoreGraphics editor.
// Caller owns the returned NSView* (call [view release] or CFRelease when destroying).
NSView* createPhsEditorView(PhsSynthEngine* engine, ParamChangeCallback onParamChange = nullptr);

// Explicitly stop timer and detach engine before releasing view
void destroyPhsEditorView(NSView* view);

} // namespace phs
