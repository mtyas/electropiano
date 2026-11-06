// macOS host for the shared editor: an NSView that forwards input to phs::gui::Editor and
// draws it through a CoreGraphics canvas (native AppKit text, Retina resolution).
#import <Cocoa/Cocoa.h>
#include "PhsCocoaView.h"
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>

#ifndef PHS_VIEW_CLASS_NAME
#define PHS_VIEW_CLASS_NAME PhsEditorView_CLAP
#endif

namespace {

using phs::gui::Color;
using phs::gui::Rect;
using phs::gui::TextStyle;

// Gradients and blurred shadows cost far more to rasterise than to copy, and the editor draws
// the same ones every frame (backdrop, panels, knob bodies). Each is rendered once, at the
// device resolution, into a CGLayer and blitted after that.
class SpriteCache {
public:
    ~SpriteCache() { clear(); clearText(); }
    CGLayerRef find(uint64_t key) const {
        const auto it = layers_.find(key);
        return it == layers_.end() ? nullptr : it->second;
    }
    void insert(uint64_t key, CGLayerRef layer) {
        if (layers_.size() >= 1024) clear(); // a zoom change or a long session: start again
        layers_[key] = layer;
    }
    void clear() {
        for (auto& kv : layers_) CGLayerRelease(kv.second);
        layers_.clear();
    }

    // Text: attribute dictionaries per style and colour, and measured widths per string
    std::unordered_map<uint64_t, NSDictionary*> attrs;
    std::unordered_map<std::string, float> widths;
    std::unordered_map<uint64_t, float> lineHeights;
    void clearText() {
        for (auto& kv : attrs) [kv.second release];
        attrs.clear();
        widths.clear();
        lineHeights.clear();
    }

private:
    std::unordered_map<uint64_t, CGLayerRef> layers_;
};

class CGCanvas final : public phs::gui::Canvas {
public:
    CGCanvas(CGContextRef ctx, SpriteCache* sprites = nullptr) : ctx_(ctx), sprites_(sprites) {}

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
        sprite(1, r, 0.0f, {radius, 0, 0}, top, bottom, [&](const Rect& lr) {
            addRoundRect(lr, radius);
            CGContextClip(ctx_);
            gradient(top, bottom, lr.maxY(), lr.y);
        });
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
        sprite(2, {cx - radius, cy - radius, 2 * radius, 2 * radius}, 0.0f, {0, 0, 0}, top, bottom, [&](const Rect& lr) {
            CGContextAddEllipseInRect(ctx_, cg(lr));
            CGContextClip(ctx_);
            gradient(top, bottom, lr.maxY(), lr.y);
        });
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
        sprite(3, r, shadowPad(blur, dy), {radius, blur, dy}, c, c, [&](const Rect& lr) {
            withShadow(blur, dy, c, [&] { addRoundRect(lr, radius); CGContextFillPath(ctx_); });
        });
    }
    void shadowCircle(float cx, float cy, float radius, float blur, float dy, Color c) override {
        sprite(4, {cx - radius, cy - radius, 2 * radius, 2 * radius}, shadowPad(blur, dy), {0, blur, dy}, c, c, [&](const Rect& lr) {
            withShadow(blur, dy, c, [&] { CGContextFillEllipseInRect(ctx_, cg(lr)); });
        });
    }
    void glowCircle(float cx, float cy, float radius, float blur, Color c) override { shadowCircle(cx, cy, radius, blur, 0.0f, c); }

    float textWidth(const std::string& s, const TextStyle& st) override {
        std::string key;
        if (sprites_) {
            key.reserve(s.size() + 16);
            key.append(reinterpret_cast<const char*>(&st.size), sizeof(float));
            key.append(reinterpret_cast<const char*>(&st.kern), sizeof(float));
            key.push_back(static_cast<char>(static_cast<int>(st.weight) * 2 + (st.monoDigits ? 1 : 0)));
            key.append(s);
            const auto it = sprites_->widths.find(key);
            if (it != sprites_->widths.end()) return it->second;
        }
        NSString* str = [NSString stringWithUTF8String:s.c_str()];
        const float w = static_cast<float>([str sizeWithAttributes:attrs(st, {1, 1, 1, 1})].width) - st.kern;
        if (sprites_) {
            if (sprites_->widths.size() > 8192) sprites_->widths.clear();
            sprites_->widths.emplace(std::move(key), w);
        }
        return w;
    }
    float lineHeight(const TextStyle& st) override {
        const uint64_t key = styleKey(st, {1, 1, 1, 1});
        if (sprites_) {
            const auto it = sprites_->lineHeights.find(key);
            if (it != sprites_->lineHeights.end()) return it->second;
        }
        const float h = static_cast<float>([@"Ag" sizeWithAttributes:attrs(st, {1, 1, 1, 1})].height);
        if (sprites_) sprites_->lineHeights[key] = h;
        return h;
    }
    void drawText(const std::string& s, float x, float y, const TextStyle& st, Color c) override {
        [[NSString stringWithUTF8String:s.c_str()] drawAtPoint:NSMakePoint(x, y) withAttributes:attrs(st, c)];
    }

private:
    static CGRect cg(const Rect& r) { return CGRectMake(r.x, r.y, r.w, r.h); }

    // CG shadows are specified in device pixels, so the margin a shadow needs shrinks with zoom;
    // this is generous at any scale >= 1
    static float shadowPad(float blur, float dy) { return std::ceil(2.0f * blur + std::fabs(dy)) + 2.0f; }

    // Draws `draw` (which fills the shape at the rect it is given) through the sprite cache:
    // the shape is rendered once into a layer at the device scale, then copied into place
    template <class F>
    void sprite(int kind, const Rect& r, float pad, std::array<float, 3> extra, Color c0, Color c1, F&& draw) {
        const CGAffineTransform ctm = CGContextGetCTM(ctx_);
        const float scale = static_cast<float>(std::sqrt(std::fabs(ctm.a * ctm.d - ctm.b * ctm.c)));
        const float lw = r.w + 2.0f * pad, lh = r.h + 2.0f * pad;
        // The sprite is drawn on whole device pixels (a copy, not a resample); the shape's
        // sub-pixel offset is rendered into it, so it lands exactly where it would have
        const CGPoint dev = CGPointApplyAffineTransform(CGPointMake(r.x - pad, r.y - pad), ctm);
        const float fx = std::round(8.0f * static_cast<float>(dev.x - std::floor(dev.x))) / 8.0f;
        const float fy = std::round(8.0f * static_cast<float>(dev.y - std::floor(dev.y))) / 8.0f;
        const size_t pw = static_cast<size_t>(std::ceil(lw * scale)) + 1, ph = static_cast<size_t>(std::ceil(lh * scale)) + 1;
        const bool axisAligned = ctm.b == 0 && ctm.c == 0 && ctm.a > 0 && ctm.d > 0;
        if (!sprites_ || !axisAligned || pw * ph > 4096u * 4096u) {
            CGContextSaveGState(ctx_);
            draw(r);
            CGContextRestoreGState(ctx_);
            return;
        }
        uint64_t key = 1469598103934665603ull;
        auto mix = [&](float v) { uint32_t b; std::memcpy(&b, &v, 4); key = (key ^ b) * 1099511628211ull; };
        mix(static_cast<float>(kind)); mix(r.w); mix(r.h); mix(scale); mix(fx); mix(fy);
        for (float v : extra) mix(v);
        for (float v : {c0.r, c0.g, c0.b, c0.a, c1.r, c1.g, c1.b, c1.a}) mix(v);
        CGLayerRef layer = sprites_->find(key);
        if (!layer) {
            layer = CGLayerCreateWithContext(ctx_, CGSizeMake(pw, ph), nullptr);
            if (!layer) return;
            CGContextRef saved = ctx_;
            ctx_ = CGLayerGetContext(layer);
            CGContextTranslateCTM(ctx_, fx, fy);
            CGContextScaleCTM(ctx_, scale, scale);
            CGContextSaveGState(ctx_);
            draw(Rect{pad, pad, r.w, r.h});
            CGContextRestoreGState(ctx_);
            ctx_ = saved;
            sprites_->insert(key, layer);
        }
        const CGRect devRect = CGRectMake(std::floor(dev.x), std::floor(dev.y), pw, ph);
        CGContextDrawLayerInRect(ctx_, CGRectApplyAffineTransform(devRect, CGAffineTransformInvert(ctm)), layer);
    }
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
    static uint64_t styleKey(const TextStyle& st, Color c) {
        uint64_t key = 1469598103934665603ull;
        auto mix = [&](float v) { uint32_t b; std::memcpy(&b, &v, 4); key = (key ^ b) * 1099511628211ull; };
        for (float v : {st.size, st.kern, static_cast<float>(st.weight), st.monoDigits ? 1.0f : 0.0f, c.r, c.g, c.b, c.a}) mix(v);
        return key;
    }
    NSDictionary* attrs(const TextStyle& st, Color c) {
        if (!sprites_) return makeAttrs(st, c);
        const uint64_t key = styleKey(st, c);
        const auto it = sprites_->attrs.find(key);
        if (it != sprites_->attrs.end()) return it->second;
        if (sprites_->attrs.size() > 2048) sprites_->clearText();
        NSDictionary* d = [makeAttrs(st, c) retain];
        sprites_->attrs[key] = d;
        return d;
    }
    static NSDictionary* makeAttrs(const TextStyle& st, Color c) {
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
    SpriteCache* sprites_;
};

} // namespace

@interface PHS_VIEW_CLASS_NAME : NSView {
@public
    std::unique_ptr<phs::gui::Editor> editor_;
    NSTimer* redrawTimer_;
    SpriteCache sprites_;
    float zoom_;
    phs::EditorResizeRequest requestResize_;
}
- (instancetype)initWithFrame:(NSRect)frame engine:(phs::PhsSynthEngine*)engine callback:(phs::ParamChangeCallback)cb;
- (void)applyZoom:(float)zoom;
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
        host.requestZoom = [self](float z) { [self applyZoom:z]; };
        host.chooseTuningFile = []() -> std::string {
            NSOpenPanel* panel = [NSOpenPanel openPanel];
            [panel setCanChooseFiles:YES];
            [panel setCanChooseDirectories:NO];
            [panel setAllowsMultipleSelection:NO];
            [panel setMessage:@"Choose a Scala tuning (.scl). A .kbm of the same name is used as its keyboard mapping."];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            [panel setAllowedFileTypes:@[@"scl"]];
#pragma clang diagnostic pop
            if ([panel runModal] != NSModalResponseOK || [[panel URLs] count] == 0) return {};
            return std::string([[[[panel URLs] objectAtIndex:0] path] UTF8String]);
        };
        zoom_ = static_cast<float>(frame.size.width / phs::kEditorWidth);
        editor_ = std::make_unique<phs::gui::Editor>(engine, std::move(host));
        editor_->setZoom(zoom_);
        // 60 Hz, and in the common modes so meters keep moving while a knob is being dragged.
        // Most ticks repaint nothing or only the small regions that changed.
        redrawTimer_ = [[NSTimer timerWithTimeInterval:(1.0 / 60.0) target:self selector:@selector(onTimerTick:)
                                              userInfo:nil repeats:YES] retain];
        [[NSRunLoop currentRunLoop] addTimer:redrawTimer_ forMode:NSRunLoopCommonModes];
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

// The editor's own zoom control: resize, then let the plugin host know
- (void)applyZoom:(float)zoom {
    const int w = phs::scaledEditorWidth(zoom), h = phs::scaledEditorHeight(zoom);
    if (requestResize_ && !requestResize_(w, h)) return;
    [self setFrameSize:NSMakeSize(w, h)];
}

// Any size the view is given (by the host or by applyZoom) sets the zoom
- (void)setFrameSize:(NSSize)size {
    [super setFrameSize:size];
    zoom_ = std::max(0.25f, static_cast<float>(std::min(size.width / phs::kEditorWidth, size.height / phs::kEditorHeight)));
    if (editor_) editor_->setZoom(zoom_);
    [self setNeedsDisplay:YES];
}
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { (void)event; return YES; }

- (void)onTimerTick:(NSTimer*)timer {
    (void)timer;
    if (!editor_) return;
    for (const phs::gui::Rect& r : editor_->dirtyRects()) {
        if (r.w >= phs::kEditorWidth && r.h >= phs::kEditorHeight) { [self setNeedsDisplay:YES]; return; }
        [self setNeedsDisplayInRect:NSInsetRect(NSMakeRect(r.x * zoom_, r.y * zoom_, r.w * zoom_, r.h * zoom_), -2, -2)];
    }
}

- (void)drawRect:(NSRect)dirtyRect {
    CGContextRef ctx = (CGContextRef)[[NSGraphicsContext currentContext] CGContext];
    if (!ctx || !editor_) return;
    CGContextSaveGState(ctx);
    CGContextScaleCTM(ctx, zoom_, zoom_);
    CGCanvas canvas(ctx, &sprites_);
    // AppKit has clipped the context to the invalidated area; the editor skips what lies outside
    const NSRect b = [self bounds];
    if (NSContainsRect(dirtyRect, b)) editor_->draw(canvas);
    else editor_->draw(canvas, {static_cast<float>(dirtyRect.origin.x / zoom_), static_cast<float>(dirtyRect.origin.y / zoom_),
                                static_cast<float>(dirtyRect.size.width / zoom_), static_cast<float>(dirtyRect.size.height / zoom_)});
    CGContextRestoreGState(ctx);
}

- (NSPoint)localPoint:(NSEvent*)event {
    const NSPoint p = [self convertPoint:[event locationInWindow] fromView:nil];
    return NSMakePoint(p.x / zoom_, p.y / zoom_);
}

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

NSView* createPhsEditorView(PhsSynthEngine* engine, ParamChangeCallback onParamChange, EditorResizeRequest requestResize) {
    const float zoom = loadEditorZoom();
    PHS_VIEW_CLASS_NAME* view = [[PHS_VIEW_CLASS_NAME alloc] initWithFrame:NSMakeRect(0, 0, scaledEditorWidth(zoom), scaledEditorHeight(zoom))
                                                                    engine:engine
                                                                  callback:onParamChange];
    view->requestResize_ = std::move(requestResize);
    return view;
}

void destroyPhsEditorView(NSView* view) {
    if (view && [view isKindOfClass:[PHS_VIEW_CLASS_NAME class]]) [(PHS_VIEW_CLASS_NAME*)view stopTimer];
}

void addPhsEditorViewToParent(NSView* view, void* parentNSView) {
    if (view && parentNSView) [static_cast<NSView*>(parentNSView) addSubview:view];
}

void releasePhsEditorView(NSView* view) {
    if (!view) return;
    [view removeFromSuperview];
    [view release];
}

gui::Editor* editorOfPhsView(NSView* view) {
    if (view && [view isKindOfClass:[PHS_VIEW_CLASS_NAME class]]) return ((PHS_VIEW_CLASS_NAME*)view)->editor_.get();
    return nullptr;
}

} // namespace phs
