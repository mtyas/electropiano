#pragma once

#include "PhsEditor.hpp"

#ifdef __OBJC__
#import <Cocoa/Cocoa.h>
#else
typedef void NSView;
#endif

namespace phs {

// Asks the plugin host for a new editor size (pixels); returns true if the host agreed
using EditorResizeRequest = std::function<bool(int width, int height)>;

// Creates a retained NSView* hosting the shared editor (CoreGraphics canvas), at the zoom saved
// in the user's settings. The view scales its content to whatever size it is given (a host
// resizing it), keeping the 900 x 776 layout's aspect.
// Caller owns the returned NSView* (call [view release] or CFRelease when destroying).
NSView* createPhsEditorView(PhsSynthEngine* engine, ParamChangeCallback onParamChange = nullptr,
                            EditorResizeRequest requestResize = nullptr);

// Explicitly stop timer and detach engine before releasing view
void destroyPhsEditorView(NSView* view);

// For hosts written in plain C++ (LV2): add the view to a parent NSView, and release it
void addPhsEditorViewToParent(NSView* view, void* parentNSView);
void releasePhsEditorView(NSView* view);

// The editor inside a view created above (for tools that script it, e.g. screenshots)
gui::Editor* editorOfPhsView(NSView* view);

} // namespace phs
