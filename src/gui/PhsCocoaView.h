#pragma once

#include "PhsEditor.hpp"

#ifdef __OBJC__
#import <Cocoa/Cocoa.h>
#else
typedef void NSView;
#endif

namespace phs {

// Creates a retained NSView* hosting the shared editor (CoreGraphics canvas).
// Caller owns the returned NSView* (call [view release] or CFRelease when destroying).
NSView* createPhsEditorView(PhsSynthEngine* engine, ParamChangeCallback onParamChange = nullptr);

// Explicitly stop timer and detach engine before releasing view
void destroyPhsEditorView(NSView* view);

// The editor inside a view created above (for tools that script it, e.g. screenshots)
gui::Editor* editorOfPhsView(NSView* view);

} // namespace phs
