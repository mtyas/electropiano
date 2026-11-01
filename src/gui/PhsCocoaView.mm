// macOS host for the shared editor: an NSView that forwards input to phs::gui::Editor and
// draws it through a CoreGraphics canvas (native AppKit text, Retina resolution).
#import <Cocoa/Cocoa.h>
#include "PhsCocoaView.h"
#include <cmath>
#include <memory>

#ifndef PHS_VIEW_CLASS_NAME
#define PHS_VIEW_CLASS_NAME PhsEditorView_CLAP
#endif

namespace {

using phs::gui::Color;
using phs::gui::Rect;
using phs::gui::TextStyle;

class CGCanvas final : public phs::gui::Canvas {
public:
    explicit CGCanvas(CGContextRef ctx) : ctx_(ctx) {}

    void save() override { CGContextSaveGState(ctx_); }
    void restore() override { CGContextRestoreGState(ctx_); }
    void clip(const Rect& r) override { CGContextClipToRect(ctx_, cg(r)); }
    void setAlpha(float a) override { CGContextSetAlpha(ctx_, a); }

    void fillRect(const Rect& r, Color c) override { fill(c); CGContextFillRect(ctx_, cg(r)); }
    void fillRoundRect(const Rect& r, float radius, Color c) override {
        addRoundRect(r, radius);
        fill(c);
        CGContextFillPath(ctx_);
    }
    void fillRoundRectGradient(const Rect& r, float radius, Color top, Color bottom) override {
        CGContextSaveGState(ctx_);
        addRoundRect(r, radius);
        CGContextClip(ctx_);
        gradient(top, bottom, r.maxY(), r.y);
        CGContextRestoreGState(ctx_);
    }
    void strokeRoundRect(const Rect& r, float radius, Color c, float width) override {
        addRoundRect(r.inset(width * 0.5f, width * 0.5f), radius);
        stroke(c, width);
        CGContextStrokePath(ctx_);
    }
    void fillCircle(float cx, float cy, float radius, Color c) override {
        fill(c);
        CGContextFillEllipseInRect(ctx_, CGRectMake(cx - radius, cy - radius, 2 * radius, 2 * radius));
    }
    void fillCircleGradient(float cx, float cy, float radius, Color top, Color bottom) override {
        CGContextSaveGState(ctx_);
        CGContextAddEllipseInRect(ctx_, CGRectMake(cx - radius, cy - radius, 2 * radius, 2 * radius));
        CGContextClip(ctx_);
        gradient(top, bottom, cy + radius, cy - radius);
        CGContextRestoreGState(ctx_);
    }
    void strokeCircle(float cx, float cy, float radius, Color c, float width) override {
        stroke(c, width);
        CGContextStrokeEllipseInRect(ctx_, CGRectMake(cx - radius, cy - radius, 2 * radius, 2 * radius));
    }
    void strokeArc(float cx, float cy, float radius, float a0, float a1, Color c, float width) override {
        stroke(c, width);
        CGContextSetLineCap(ctx_, kCGLineCapRound);
        CGContextAddArc(ctx_, cx, cy, radius, a0, a1, a1 < a0 ? 1 : 0);
        CGContextStrokePath(ctx_);
    }
    void strokeLine(float x0, float y0, float x1, float y1, Color c, float width) override {
        stroke(c, width);
        CGContextSetLineCap(ctx_, kCGLineCapRound);
        CGContextMoveToPoint(ctx_, x0, y0);
        CGContextAddLineToPoint(ctx_, x1, y1);
        CGContextStrokePath(ctx_);
    }
    // Shadows: the editor always draws the casting shape over its shadow straight after,
    // so the shape is filled in place (in the shadow colour) with a CG shadow attached
    void shadowRoundRect(const Rect& r, float radius, float blur, float dy, Color c) override {
        withShadow(blur, dy, c, [&] { addRoundRect(r, radius); CGContextFillPath(ctx_); });
    }
    void shadowCircle(float cx, float cy, float radius, float blur, float dy, Color c) override {
        withShadow(blur, dy, c, [&] { CGContextFillEllipseInRect(ctx_, CGRectMake(cx - radius, cy - radius, 2 * radius, 2 * radius)); });
    }
    void glowCircle(float cx, float cy, float radius, float blur, Color c) override { shadowCircle(cx, cy, radius, blur, 0.0f, c); }

    float textWidth(const std::string& s, const TextStyle& st) override {
        NSString* str = [NSString stringWithUTF8String:s.c_str()];
        return static_cast<float>([str sizeWithAttributes:attrs(st, {1, 1, 1, 1})].width) - st.kern;
    }
    float lineHeight(const TextStyle& st) override {
        return static_cast<float>([@"Ag" sizeWithAttributes:attrs(st, {1, 1, 1, 1})].height);
    }
    void drawText(const std::string& s, float x, float y, const TextStyle& st, Color c) override {
        [[NSString stringWithUTF8String:s.c_str()] drawAtPoint:NSMakePoint(x, y) withAttributes:attrs(st, c)];
    }

private:
    static CGRect cg(const Rect& r) { return CGRectMake(r.x, r.y, r.w, r.h); }
    void fill(Color c) { CGContextSetRGBFillColor(ctx_, c.r, c.g, c.b, c.a); }
    void stroke(Color c, float w) {
        CGContextSetRGBStrokeColor(ctx_, c.r, c.g, c.b, c.a);
        CGContextSetLineWidth(ctx_, w);
    }
    void addRoundRect(const Rect& r, float radius) {
        radius = std::min(radius, std::min(r.w, r.h) * 0.5f);
        CGPathRef p = CGPathCreateWithRoundedRect(cg(r), radius, radius, nullptr);
        CGContextAddPath(ctx_, p);
        CGPathRelease(p);
    }
    void gradient(Color top, Color bottom, float yTop, float yBottom) {
        CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
        const CGFloat comps[8] = {top.r, top.g, top.b, top.a, bottom.r, bottom.g, bottom.b, bottom.a};
        CGGradientRef g = CGGradientCreateWithColorComponents(cs, comps, nullptr, 2);
        CGContextDrawLinearGradient(ctx_, g, CGPointMake(0, yTop), CGPointMake(0, yBottom), 0);
        CGGradientRelease(g);
        CGColorSpaceRelease(cs);
    }
    template <class F>
    void withShadow(float blur, float dy, Color c, F&& drawShape) {
        CGContextSaveGState(ctx_);
        CGColorRef col = CGColorCreateGenericRGB(c.r, c.g, c.b, c.a);
        CGContextSetShadowWithColor(ctx_, CGSizeMake(0, dy), blur, col);
        CGColorRelease(col);
        CGContextSetRGBFillColor(ctx_, c.r, c.g, c.b, c.a);
        drawShape();
        CGContextRestoreGState(ctx_);
    }
    static NSDictionary* attrs(const TextStyle& st, Color c) {
        const NSFontWeight w = st.weight == phs::gui::Weight::Semibold ? NSFontWeightSemibold
                             : st.weight == phs::gui::Weight::Medium ? NSFontWeightMedium : NSFontWeightRegular;
        NSFont* font = st.monoDigits ? [NSFont monospacedDigitSystemFontOfSize:st.size weight:w]
                                     : [NSFont systemFontOfSize:st.size weight:w];
        return @{
            NSFontAttributeName: font,
            NSForegroundColorAttributeName: [NSColor colorWithDeviceRed:c.r green:c.g blue:c.b alpha:c.a],
            NSKernAttributeName: @(st.kern)
        };
    }
    CGContextRef ctx_;
};

} // namespace

@interface PHS_VIEW_CLASS_NAME : NSView {
@public
    std::unique_ptr<phs::gui::Editor> editor_;
    NSTimer* redrawTimer_;
}
- (instancetype)initWithFrame:(NSRect)frame engine:(phs::PhsSynthEngine*)engine callback:(phs::ParamChangeCallback)cb;
- (void)stopTimer;
@end

@implementation PHS_VIEW_CLASS_NAME

- (instancetype)initWithFrame:(NSRect)frame engine:(phs::PhsSynthEngine*)engine callback:(phs::ParamChangeCallback)cb {
    self = [super initWithFrame:frame];
    if (self) {
        phs::gui::EditorHost host;
        host.paramChanged = std::move(cb);
        host.openUrl = [](const std::string& url) {
            [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]]];
        };
        editor_ = std::make_unique<phs::gui::Editor>(engine, std::move(host));
        redrawTimer_ = [[NSTimer scheduledTimerWithTimeInterval:(1.0 / 30.0) target:self selector:@selector(onTimerTick:)
                                                       userInfo:nil repeats:YES] retain];
    }
    return self;
}

- (void)stopTimer {
    if (redrawTimer_) {
        [redrawTimer_ invalidate];
        [redrawTimer_ release];
        redrawTimer_ = nil;
    }
    if (editor_) editor_->detachEngine();
}

- (void)dealloc {
    [self stopTimer];
    editor_.reset();
    [super dealloc];
}

- (BOOL)isFlipped { return NO; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { (void)event; return YES; }

- (void)onTimerTick:(NSTimer*)timer {
    (void)timer;
    if (editor_ && editor_->wantsRedraw()) [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    CGContextRef ctx = (CGContextRef)[[NSGraphicsContext currentContext] CGContext];
    if (!ctx || !editor_) return;
    CGCanvas canvas(ctx);
    editor_->draw(canvas);
}

- (NSPoint)localPoint:(NSEvent*)event { return [self convertPoint:[event locationInWindow] fromView:nil]; }

- (void)mouseDown:(NSEvent*)event {
    [[self window] makeFirstResponder:self];
    const NSPoint p = [self localPoint:event];
    const bool fine = ([event modifierFlags] & NSEventModifierFlagShift) != 0;
    editor_->mouseDown(p.x, p.y, fine, [event timestamp]);
    [self setNeedsDisplay:YES];
}
- (void)mouseDragged:(NSEvent*)event {
    const NSPoint p = [self localPoint:event];
    editor_->mouseDrag(p.x, p.y);
    [self setNeedsDisplay:YES];
}
- (void)mouseUp:(NSEvent*)event {
    const NSPoint p = [self localPoint:event];
    editor_->mouseUp(p.x, p.y);
    [self setNeedsDisplay:YES];
}
- (void)rightMouseDown:(NSEvent*)event {
    const NSPoint p = [self localPoint:event];
    editor_->rightMouseDown(p.x, p.y);
    [self setNeedsDisplay:YES];
}
- (void)scrollWheel:(NSEvent*)event {
    const NSPoint p = [self localPoint:event];
    const CGFloat d = [event hasPreciseScrollingDeltas] ? [event scrollingDeltaY] / 12.0 : [event scrollingDeltaY];
    editor_->scroll(p.x, p.y, static_cast<float>(d));
    [self setNeedsDisplay:YES];
}
- (void)keyDown:(NSEvent*)event {
    bool handled = false;
    switch ([event keyCode]) {
        case 53: handled = editor_->keyAction(phs::gui::Key::Escape); break;
        case 36: case 76: handled = editor_->keyAction(phs::gui::Key::Enter); break;
        case 51: handled = editor_->keyAction(phs::gui::Key::Backspace); break;
        default: {
            NSString* chars = [event characters];
            for (NSUInteger i = 0; i < [chars length]; ++i) handled |= editor_->keyChar([chars characterAtIndex:i]);
        }
    }
    if (handled) [self setNeedsDisplay:YES];
    else [super keyDown:event];
}

@end

namespace phs {

NSView* createPhsEditorView(PhsSynthEngine* engine, ParamChangeCallback onParamChange) {
    return [[PHS_VIEW_CLASS_NAME alloc] initWithFrame:NSMakeRect(0, 0, kEditorWidth, kEditorHeight)
                                               engine:engine
                                             callback:onParamChange];
}

void destroyPhsEditorView(NSView* view) {
    if (view && [view isKindOfClass:[PHS_VIEW_CLASS_NAME class]]) [(PHS_VIEW_CLASS_NAME*)view stopTimer];
}

gui::Editor* editorOfPhsView(NSView* view) {
    if (view && [view isKindOfClass:[PHS_VIEW_CLASS_NAME class]]) return ((PHS_VIEW_CLASS_NAME*)view)->editor_.get();
    return nullptr;
}

} // namespace phs
