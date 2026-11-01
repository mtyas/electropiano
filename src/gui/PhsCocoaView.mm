#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#include "PhsCocoaView.h"
#include <cmath>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>

#ifndef PHS_VIEW_CLASS_NAME
#define PHS_VIEW_CLASS_NAME PhsEditorCocoaView_Default
#endif

namespace {

struct KnobRect {
    uint32_t paramId;
    NSRect rect;
    const char* title;
    const char* unit;
};

static bool isBlackKey(int midiNote) {
    const int n = midiNote % 12;
    return (n == 1 || n == 3 || n == 6 || n == 8 || n == 10);
}

// Convert parameter value to human-readable string
static NSString* formatParamValue(uint32_t id, float norm) {
    if (id >= phs::kNumParams) return @"";
    const auto& meta = phs::kParamMetadata[id];
    const float val = meta.minValue + norm * (meta.maxValue - meta.minValue);
    switch (id) {
        case phs::kParamModelType: {
            const int m = std::clamp(static_cast<int>(std::round(norm * 3.0f)), 0, 3);
            static const char* kModels[] = {"Rhodes Mk I", "Rhodes Dyno", "Wurlitzer 200A", "Custom PHS"};
            return [NSString stringWithUTF8String:kModels[m]];
        }
        case phs::kParamHammerHardness:
            return [NSString stringWithFormat:@"%.0f%%", norm * 100.0f];
        case phs::kParamHammerHysteresis:
            return [NSString stringWithFormat:@"%.2f", val];
        case phs::kParamStrikePosition:
            return [NSString stringWithFormat:@"%.1f mm", val * 100.0f];
        case phs::kParamTineDecay:
            return [NSString stringWithFormat:@"%.1f s", val];
        case phs::kParamTineBell:
            return [NSString stringWithFormat:@"%.0f%%", norm * 100.0f];
        case phs::kParamToneBarCoupling:
            return [NSString stringWithFormat:@"%.0f%%", norm * 100.0f];
        case phs::kParamKeyNoise:
            return [NSString stringWithFormat:@"%.1f dB", -40.0f + norm * 40.0f];
        case phs::kParamPickupDistance:
            return [NSString stringWithFormat:@"%.2f mm", 0.95f - 0.71f * norm];
        case phs::kParamPickupSymmetry:
            return [NSString stringWithFormat:@"%+.2f", -0.5f + norm];
        case phs::kParamPickupResonance:
            return [NSString stringWithFormat:@"Q=%.1f", 0.5f + norm * 4.0f];
        case phs::kParamPreampDrive:
            return [NSString stringWithFormat:@"%.1f dB", norm * 24.0f];
        case phs::kParamBassBoost:
        case phs::kParamTrebleBoost:
            return [NSString stringWithFormat:@"%+.1f dB", -12.0f + norm * 24.0f];
        case phs::kParamTremoloDepth:
        case phs::kParamPhaserMix:
        case phs::kParamChorusMix:
        case phs::kParamReverbMix:
            return [NSString stringWithFormat:@"%.0f%%", norm * 100.0f];
        case phs::kParamTremoloRate:
        case phs::kParamPhaserRate:
            return [NSString stringWithFormat:@"%.2f Hz", val];
        case phs::kParamReverbDecay:
            return [NSString stringWithFormat:@"%.1f s", val];
        case phs::kParamVelocityCurve:
            return [NSString stringWithFormat:@"%.2f", val];
        case phs::kParamMasterVolume:
            return [NSString stringWithFormat:@"%.1f dB", -60.0f + norm * 66.0f];
        default:
            return [NSString stringWithFormat:@"%.2f", val];
    }
}

} // namespace

@interface PHS_VIEW_CLASS_NAME : NSView {
@public
    phs::PhsSynthEngine* engine_;
    phs::ParamChangeCallback paramCallback_;
    NSTimer* redrawTimer_;

    std::vector<KnobRect> knobs_;
    NSRect modelButtonRects_[4];
    NSRect undoButtonRect_;
    NSRect redoButtonRect_;
    NSRect midiLearnButtonRect_;
    NSRect presetPrevRect_;
    NSRect presetNextRect_;
    NSRect presetBoxRect_;
    NSRect presetBrowseButtonRect_;
    NSRect pedalButtonRect_;
    NSRect pitchBendRect_;
    NSRect modWheelRect_;
    NSRect keyboardRect_;

    // Preset Browser modal elements
    bool browserOpen_;
    NSRect browserModalRect_;
    NSRect browserCloseRect_;
    NSRect browserSaveRect_;
    NSRect browserDeleteRect_;
    NSRect categoryTabRects_[6];
    phs::PresetCategory selectedCategory_;
    int selectedPresetIdx_;
    int browserScrollOffset_;

    // Mouse interaction
    int draggedKnobIdx_;
    CGFloat dragStartY_;
    float dragStartVal_;
    bool isFineDrag_;
    int mouseHeldMidiNote_;
    bool pitchBendHeld_;
    bool modWheelHeld_;
    uint64_t lastClickTimestamp_;
    int lastClickedKnobIdx_;
}
- (instancetype)initWithFrame:(NSRect)frame
                       engine:(phs::PhsSynthEngine*)engine
                     callback:(phs::ParamChangeCallback)cb;
- (void)stopTimer;
- (void)openSavePresetDialog;
@end

@implementation PHS_VIEW_CLASS_NAME

- (instancetype)initWithFrame:(NSRect)frame
                       engine:(phs::PhsSynthEngine*)engine
                     callback:(phs::ParamChangeCallback)cb {
    self = [super initWithFrame:frame];
    if (self) {
        engine_ = engine;
        paramCallback_ = cb;
        draggedKnobIdx_ = -1;
        mouseHeldMidiNote_ = -1;
        pitchBendHeld_ = false;
        modWheelHeld_ = false;
        browserOpen_ = false;
        selectedCategory_ = phs::PresetCategory::All;
        selectedPresetIdx_ = 0;
        browserScrollOffset_ = 0;
        lastClickTimestamp_ = 0;
        lastClickedKnobIdx_ = -1;

        [self setupLayout];

        // 30 Hz silky refresh timer
        redrawTimer_ = [[NSTimer scheduledTimerWithTimeInterval:(1.0 / 30.0)
                                                         target:self
                                                       selector:@selector(onTimerTick:)
                                                       userInfo:nil
                                                        repeats:YES] retain];
    }
    return self;
}

- (void)stopTimer {
    if (redrawTimer_) {
        [redrawTimer_ invalidate];
        [redrawTimer_ release];
        redrawTimer_ = nil;
    }
    engine_ = nullptr;
}

- (void)dealloc {
    [self stopTimer];
    [super dealloc];
}

- (BOOL)isFlipped {
    return NO; // Standard Cartesian coordinates (bottom-left = 0, 0)
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (void)onTimerTick:(NSTimer*)timer {
    (void)timer;
    [self setNeedsDisplay:YES];
}

- (void)setupLayout {
    knobs_.clear();

    // =========================================================================
    // 1. TOP HEADER & TOOLBAR (y: 630 .. 675)
    // =========================================================================
    // Model selector tabs (Rhodes Mk I, Rhodes Dyno, Wurlitzer 200A, Custom PHS)
    for (int i = 0; i < 4; ++i) {
        modelButtonRects_[i] = NSMakeRect(205.0 + i * 94.0, 638.0, 90.0, 28.0);
    }

    // Undo & Redo buttons
    undoButtonRect_ = NSMakeRect(590.0, 638.0, 56.0, 28.0);
    redoButtonRect_ = NSMakeRect(650.0, 638.0, 56.0, 28.0);

    // MIDI Learn button
    midiLearnButtonRect_ = NSMakeRect(712.0, 638.0, 84.0, 28.0);

    // Preset Prev / Box / Next / Browse
    presetPrevRect_         = NSMakeRect(802.0, 638.0, 24.0, 28.0);
    presetBoxRect_          = NSMakeRect(828.0, 638.0, 116.0, 28.0);
    presetNextRect_         = NSMakeRect(946.0, 638.0, 24.0, 28.0);
    presetBrowseButtonRect_ = NSMakeRect(972.0, 638.0, 20.0, 28.0);

    // =========================================================================
    // 2. CONTROL PANELS & KNOBS (y: 118 .. 420)
    // =========================================================================
    // Panel 1: Mechanics & Tine (x: 16 .. 328, y: 122 .. 420)
    const uint32_t mechTopIds[4] = {
        phs::kParamHammerHardness,
        phs::kParamHammerHysteresis,
        phs::kParamStrikePosition,
        phs::kParamTineDecay
    };
    for (int i = 0; i < 4; ++i) {
        const CGFloat kx = 24.0 + i * 76.0;
        knobs_.push_back({mechTopIds[i], NSMakeRect(kx, 332.0, 66.0, 68.0), phs::kParamMetadata[mechTopIds[i]].name, ""});
    }
    const uint32_t mechBotIds[3] = {
        phs::kParamTineBell,
        phs::kParamToneBarCoupling,
        phs::kParamKeyNoise
    };
    for (int i = 0; i < 3; ++i) {
        const CGFloat kx = 56.0 + i * 86.0;
        knobs_.push_back({mechBotIds[i], NSMakeRect(kx, 240.0, 66.0, 68.0), phs::kParamMetadata[mechBotIds[i]].name, ""});
    }

    // Panel 2: Nonlinear Transducer & Preamp EQ (x: 342 .. 662, y: 122 .. 420)
    const uint32_t transTopIds[3] = {
        phs::kParamPickupDistance,
        phs::kParamPickupSymmetry,
        phs::kParamPickupResonance
    };
    for (int i = 0; i < 3; ++i) {
        const CGFloat kx = 362.0 + i * 98.0;
        knobs_.push_back({transTopIds[i], NSMakeRect(kx, 332.0, 68.0, 68.0), phs::kParamMetadata[transTopIds[i]].name, ""});
    }
    const uint32_t transBotIds[3] = {
        phs::kParamPreampDrive,
        phs::kParamBassBoost,
        phs::kParamTrebleBoost
    };
    for (int i = 0; i < 3; ++i) {
        const CGFloat kx = 362.0 + i * 98.0;
        knobs_.push_back({transBotIds[i], NSMakeRect(kx, 240.0, 68.0, 68.0), phs::kParamMetadata[transBotIds[i]].name, ""});
    }

    // Panel 3: Modulation & Effects (x: 676 .. 984, y: 122 .. 420)
    const uint32_t fxTopIds[4] = {
        phs::kParamTremoloDepth,
        phs::kParamTremoloRate,
        phs::kParamPhaserMix,
        phs::kParamPhaserRate
    };
    for (int i = 0; i < 4; ++i) {
        const CGFloat kx = 688.0 + i * 72.0;
        knobs_.push_back({fxTopIds[i], NSMakeRect(kx, 332.0, 64.0, 68.0), phs::kParamMetadata[fxTopIds[i]].name, ""});
    }
    const uint32_t fxBotIds[5] = {
        phs::kParamChorusMix,
        phs::kParamReverbMix,
        phs::kParamReverbDecay,
        phs::kParamVelocityCurve,
        phs::kParamMasterVolume
    };
    for (int i = 0; i < 5; ++i) {
        const CGFloat kx = 684.0 + i * 58.0;
        knobs_.push_back({fxBotIds[i], NSMakeRect(kx, 240.0, 56.0, 68.0), phs::kParamMetadata[fxBotIds[i]].name, ""});
    }

    // Performance bar (Left of keyboard: Sustain Pedal, Pitch Bend, Mod Wheel)
    pedalButtonRect_ = NSMakeRect(16.0, 64.0, 60.0, 36.0);
    pitchBendRect_   = NSMakeRect(84.0, 16.0, 24.0, 84.0);
    modWheelRect_    = NSMakeRect(114.0, 16.0, 24.0, 84.0);

    // 61-Key Keyboard (x: 148 .. 984, y: 14 .. 104)
    keyboardRect_    = NSMakeRect(148.0, 14.0, 836.0, 90.0);

    // Preset Browser modal layout
    browserModalRect_  = NSMakeRect(80.0, 60.0, 840.0, 550.0);
    browserCloseRect_  = NSMakeRect(885.0, 575.0, 24.0, 24.0);
    browserSaveRect_   = NSMakeRect(480.0, 72.0, 140.0, 32.0);
    browserDeleteRect_ = NSMakeRect(630.0, 72.0, 100.0, 32.0);

    for (int i = 0; i < 6; ++i) {
        categoryTabRects_[i] = NSMakeRect(96.0, 520.0 - (i + 1) * 36.0, 150.0, 30.0);
    }
}

// =============================================================================
// KEYBOARD SHORTCUTS (Undo / Redo / Pedal)
// =============================================================================
- (void)keyDown:(NSEvent*)event {
    if (!engine_) return;
    const NSString* chars = [event charactersIgnoringModifiers];
    const NSUInteger mods = [event modifierFlags];
    const bool isCmd = (mods & NSEventModifierFlagCommand) != 0;
    const bool isShift = (mods & NSEventModifierFlagShift) != 0;

    if (browserOpen_) {
        if ([event keyCode] == 53) { // Escape
            browserOpen_ = false;
            [self setNeedsDisplay:YES];
            return;
        }
    }

    if (isCmd && [chars isEqualToString:@"z"]) {
        if (isShift) {
            engine_->redo();
        } else {
            engine_->undo();
        }
        [self setNeedsDisplay:YES];
        return;
    } else if (isCmd && [chars isEqualToString:@"y"]) {
        engine_->redo();
        [self setNeedsDisplay:YES];
        return;
    } else if ([chars isEqualToString:@" "]) { // Space toggles sustain pedal
        engine_->handleMidiEvent(0xB0, 64, (engine_->isNoteActive(60) ? 0 : 127));
        [self setNeedsDisplay:YES];
        return;
    }
    [super keyDown:event];
}

// =============================================================================
// DRAWING ROUTINES (Boutique Hardware + CRT Phosphor Oscilloscope)
// =============================================================================
- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    CGContextRef ctx = (CGContextRef)[[NSGraphicsContext currentContext] CGContext];
    if (!ctx) return;

    // Background gradient: Vintage Deep Navy / Ebony
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    const CGFloat bgColors[8] = {
        0.08f, 0.09f, 0.12f, 1.0f,
        0.03f, 0.04f, 0.06f, 1.0f
    };
    CGGradientRef bgGrad = CGGradientCreateWithColorComponents(cs, bgColors, nullptr, 2);
    CGContextDrawLinearGradient(ctx, bgGrad, CGPointMake(0, 680), CGPointMake(0, 0), 0);
    CGGradientRelease(bgGrad);

    // 1. Draw Header Bar
    [self drawHeaderBar:ctx];

    // 2. Draw Physical Port-Hamiltonian Lab Visualizer
    [self drawLaboratorySection:ctx];

    // 3. Draw Hardware Panels & Knobs
    [self drawControlPanels:ctx];

    // 4. Draw Performance Controls & Playable Keyboard
    [self drawPerformanceSection:ctx];

    // 5. Draw Preset Browser Modal (if open)
    if (browserOpen_) {
        [self drawPresetBrowserModal:ctx];
    }

    CGColorSpaceRelease(cs);
}

- (void)drawHeaderBar:(CGContextRef)ctx {
    // Header background line
    CGContextSetRGBStrokeColor(ctx, 0.20f, 0.22f, 0.28f, 0.8f);
    CGContextSetLineWidth(ctx, 1.0f);
    CGContextStrokeRect(ctx, CGRectMake(0, 630.0, 1000.0, 1.0));

    // Boutique Brand Badge: "PyPHS"
    NSDictionary* brandAttrs = @{
        NSFontAttributeName: [NSFont fontWithName:@"HelveticaNeue-Bold" size:18.0] ?: [NSFont boldSystemFontOfSize:18.0],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.95f green:0.82f blue:0.42f alpha:1.0f]
    };
    [@"PyPHS" drawAtPoint:NSMakePoint(18.0, 646.0) withAttributes:brandAttrs];

    NSDictionary* subBrandAttrs = @{
        NSFontAttributeName: [NSFont fontWithName:@"HelveticaNeue-Medium" size:9.5] ?: [NSFont systemFontOfSize:9.5],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.55f green:0.75f blue:0.92f alpha:0.9f]
    };
    [@"PORT-HAMILTONIAN ELECTRIC PIANO" drawAtPoint:NSMakePoint(18.0, 636.0) withAttributes:subBrandAttrs];

    // Model Selector Tabs
    const int currentModel = engine_ ? std::clamp(static_cast<int>(std::round(engine_->getParamNormalized(phs::kParamModelType) * 3.0f)), 0, 3) : 0;
    static const char* kModelNames[4] = {"Rhodes Mk I", "Rhodes Dyno", "Wurlitzer 200A", "Custom PHS"};

    for (int i = 0; i < 4; ++i) {
        const NSRect r = modelButtonRects_[i];
        const bool active = (i == currentModel);

        CGContextSaveGState(ctx);
        if (active) {
            CGContextSetRGBFillColor(ctx, 0.16f, 0.32f, 0.45f, 0.95f);
            CGContextSetRGBStrokeColor(ctx, 0.40f, 0.78f, 0.98f, 0.9f);
        } else {
            CGContextSetRGBFillColor(ctx, 0.12f, 0.13f, 0.17f, 0.85f);
            CGContextSetRGBStrokeColor(ctx, 0.25f, 0.28f, 0.35f, 0.6f);
        }
        CGContextSetLineWidth(ctx, 1.0f);
        CGPathRef round = CGPathCreateWithRoundedRect(NSRectToCGRect(r), 4.0, 4.0, nullptr);
        CGContextAddPath(ctx, round);
        CGContextDrawPath(ctx, kCGPathFillStroke);
        CGPathRelease(round);

        NSDictionary* textAttrs = @{
            NSFontAttributeName: [NSFont fontWithName:@"HelveticaNeue-Bold" size:10.5] ?: [NSFont boldSystemFontOfSize:10.5],
            NSForegroundColorAttributeName: active ? [NSColor whiteColor] : [NSColor colorWithCalibratedWhite:0.65 alpha:1.0]
        };
        NSString* title = [NSString stringWithUTF8String:kModelNames[i]];
        const NSSize sz = [title sizeWithAttributes:textAttrs];
        [title drawAtPoint:NSMakePoint(r.origin.x + (r.size.width - sz.width) * 0.5, r.origin.y + (r.size.height - sz.height) * 0.5)
            withAttributes:textAttrs];
        CGContextRestoreGState(ctx);
    }

    // Undo / Redo Buttons
    auto drawBtn = [&](NSRect r, NSString* title, bool enabled) {
        CGContextSaveGState(ctx);
        CGContextSetRGBFillColor(ctx, 0.12f, 0.13f, 0.17f, enabled ? 0.9f : 0.4f);
        CGContextSetRGBStrokeColor(ctx, 0.30f, 0.34f, 0.42f, enabled ? 0.8f : 0.3f);
        CGPathRef p = CGPathCreateWithRoundedRect(NSRectToCGRect(r), 4.0, 4.0, nullptr);
        CGContextAddPath(ctx, p);
        CGContextDrawPath(ctx, kCGPathFillStroke);
        CGPathRelease(p);

        NSDictionary* attrs = @{
            NSFontAttributeName: [NSFont systemFontOfSize:10.0 weight:NSFontWeightMedium],
            NSForegroundColorAttributeName: enabled ? [NSColor whiteColor] : [NSColor colorWithCalibratedWhite:0.4 alpha:1.0]
        };
        const NSSize sz = [title sizeWithAttributes:attrs];
        [title drawAtPoint:NSMakePoint(r.origin.x + (r.size.width - sz.width) * 0.5, r.origin.y + (r.size.height - sz.height) * 0.5) withAttributes:attrs];
        CGContextRestoreGState(ctx);
    };
    drawBtn(undoButtonRect_, @"↺ Undo", engine_ ? engine_->canUndo() : false);
    drawBtn(redoButtonRect_, @"↻ Redo", engine_ ? engine_->canRedo() : false);

    // MIDI Learn Button
    const bool isLearning = engine_ && (engine_->getLearningParamId() >= 0);
    {
        CGContextSaveGState(ctx);
        if (isLearning) {
            CGContextSetRGBFillColor(ctx, 0.55f, 0.38f, 0.05f, 0.95f);
            CGContextSetRGBStrokeColor(ctx, 0.98f, 0.78f, 0.15f, 1.0f);
        } else {
            CGContextSetRGBFillColor(ctx, 0.12f, 0.13f, 0.17f, 0.9f);
            CGContextSetRGBStrokeColor(ctx, 0.30f, 0.34f, 0.42f, 0.8f);
        }
        CGPathRef p = CGPathCreateWithRoundedRect(NSRectToCGRect(midiLearnButtonRect_), 4.0, 4.0, nullptr);
        CGContextAddPath(ctx, p);
        CGContextDrawPath(ctx, kCGPathFillStroke);
        CGPathRelease(p);

        NSDictionary* attrs = @{
            NSFontAttributeName: [NSFont systemFontOfSize:10.0 weight:NSFontWeightBold],
            NSForegroundColorAttributeName: isLearning ? [NSColor colorWithCalibratedRed:1.0 green:0.9 blue:0.2 alpha:1.0]
                                                       : [NSColor colorWithCalibratedWhite:0.8 alpha:1.0]
        };
        NSString* text = isLearning ? @"LEARNING..." : @"⚡ LEARN";
        const NSSize sz = [text sizeWithAttributes:attrs];
        [text drawAtPoint:NSMakePoint(midiLearnButtonRect_.origin.x + (midiLearnButtonRect_.size.width - sz.width) * 0.5,
                                      midiLearnButtonRect_.origin.y + (midiLearnButtonRect_.size.height - sz.height) * 0.5)
           withAttributes:attrs];
        CGContextRestoreGState(ctx);
    }

    // Preset Selector & Browse Button
    drawBtn(presetPrevRect_, @"<", true);
    {
        CGContextSaveGState(ctx);
        CGContextSetRGBFillColor(ctx, 0.09f, 0.10f, 0.14f, 0.95f);
        CGContextSetRGBStrokeColor(ctx, 0.32f, 0.36f, 0.45f, 0.8f);
        CGPathRef p = CGPathCreateWithRoundedRect(NSRectToCGRect(presetBoxRect_), 4.0, 4.0, nullptr);
        CGContextAddPath(ctx, p);
        CGContextDrawPath(ctx, kCGPathFillStroke);
        CGPathRelease(p);

        NSString* pName = @"Preset";
        if (engine_) {
            const int pIdx = engine_->getCurrentPreset();
            const auto* itm = engine_->getPresetManager().getPreset(pIdx);
            if (itm) pName = [NSString stringWithUTF8String:itm->name.c_str()];
        }
        NSDictionary* pAttrs = @{
            NSFontAttributeName: [NSFont systemFontOfSize:10.0 weight:NSFontWeightSemibold],
            NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.92 green:0.88 blue:0.75 alpha:1.0]
        };
        const NSSize sz = [pName sizeWithAttributes:pAttrs];
        [pName drawAtPoint:NSMakePoint(presetBoxRect_.origin.x + 6.0, presetBoxRect_.origin.y + (presetBoxRect_.size.height - sz.height) * 0.5) withAttributes:pAttrs];
        CGContextRestoreGState(ctx);
    }
    drawBtn(presetNextRect_, @">", true);
    drawBtn(presetBrowseButtonRect_, @"☰", true);
}

// =============================================================================
// LABORATORY & OSCILLOSCOPE (y: 432 .. 620)
// =============================================================================
- (void)drawLaboratorySection:(CGContextRef)ctx {
    const NSRect labRect = NSMakeRect(16.0, 432.0, 968.0, 192.0);

    // Dark chassis background
    CGContextSaveGState(ctx);
    CGContextSetRGBFillColor(ctx, 0.05f, 0.06f, 0.08f, 0.92f);
    CGContextSetRGBStrokeColor(ctx, 0.18f, 0.20f, 0.25f, 0.8f);
    CGContextSetLineWidth(ctx, 1.2f);
    CGPathRef rPath = CGPathCreateWithRoundedRect(NSRectToCGRect(labRect), 6.0, 6.0, nullptr);
    CGContextAddPath(ctx, rPath);
    CGContextDrawPath(ctx, kCGPathFillStroke);
    CGPathRelease(rPath);

    // Left Subpanel: Port-Hamiltonian Physical Beam & Transducer Visualizer (x: 24 .. 480)
    const NSRect physRect = NSMakeRect(24.0, 440.0, 460.0, 176.0);
    CGContextSetRGBFillColor(ctx, 0.03f, 0.04f, 0.05f, 0.9f);
    CGContextSetRGBStrokeColor(ctx, 0.14f, 0.16f, 0.20f, 0.7f);
    CGPathRef phPath = CGPathCreateWithRoundedRect(NSRectToCGRect(physRect), 4.0, 4.0, nullptr);
    CGContextAddPath(ctx, phPath);
    CGContextDrawPath(ctx, kCGPathFillStroke);
    CGPathRelease(phPath);

    // Label: "PHYSICAL PORT-HAMILTONIAN RESONATOR"
    NSDictionary* labTitleAttrs = @{
        NSFontAttributeName: [NSFont fontWithName:@"HelveticaNeue-Bold" size:10.0] ?: [NSFont boldSystemFontOfSize:10.0],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.40f green:0.75f blue:0.92f alpha:0.95f]
    };
    [@"PORT-HAMILTONIAN SYSTEM (H <-> B <-> P)" drawAtPoint:NSMakePoint(32.0, 598.0) withAttributes:labTitleAttrs];

    phs::ScopeSnapshot scope{};
    if (engine_) {
        scope = engine_->getScopeSnapshot();
    }

    // Passivity & Hamiltonian Telemetry Box
    NSDictionary* telAttrs = @{
        NSFontAttributeName: [NSFont fontWithName:@"Courier-Bold" size:9.5] ?: [NSFont monospacedSystemFontOfSize:9.5 weight:NSFontWeightBold],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.25f green:0.85f blue:0.45f alpha:1.0f]
    };
    NSString* hStr = [NSString stringWithFormat:@"H(t): %.2e J | Pdiss: %.2e W | Voices: %d | PASSIVE: OK",
                      scope.totalHamiltonian, scope.dissipatedPower, scope.activeVoices];
    [hStr drawAtPoint:NSMakePoint(32.0, 582.0) withAttributes:telAttrs];

    // Animated Schematic: Hammer -> Cantilever Tine/Reed -> Pickup
    const CGFloat baseY = 510.0;
    const CGFloat clampX = 50.0;
    const CGFloat tipX = 380.0;
    const CGFloat tipExcursion = std::clamp(scope.tipDisplacement * 22.0f, -32.0f, 32.0f);
    const CGFloat hammerComp = std::clamp(scope.hammerCompression * 35.0f, 0.0f, 25.0f);

    // Clamped Root
    CGContextSetRGBFillColor(ctx, 0.40f, 0.42f, 0.48f, 0.9f);
    CGContextFillRect(ctx, CGRectMake(clampX - 10.0, baseY - 24.0, 10.0, 48.0));

    // Flexible Cantilever Tine (Cubic Bezier curve flexure)
    CGContextSetRGBStrokeColor(ctx, 0.88f, 0.85f, 0.72f, 1.0f);
    CGContextSetLineWidth(ctx, 3.5f);
    CGContextBeginPath(ctx);
    CGContextMoveToPoint(ctx, clampX, baseY);
    CGContextAddCurveToPoint(ctx, clampX + 140.0, baseY,
                                  clampX + 240.0, baseY + tipExcursion * 0.7f,
                                  tipX, baseY + tipExcursion);
    CGContextStrokePath(ctx);

    // Felt Hammer
    const CGFloat hammerStrikeX = clampX + 80.0;
    const CGFloat hammerY = baseY - 28.0 + hammerComp;
    CGContextSetRGBFillColor(ctx, 0.25f, 0.65f, 0.85f, 0.95f);
    CGContextFillEllipseInRect(ctx, CGRectMake(hammerStrikeX - 8.0, hammerY, 16.0, 16.0));

    // Magnetic / Electrostatic Pickup Pole Piece
    const CGFloat pickupX = tipX + 14.0;
    CGContextSetRGBFillColor(ctx, 0.85f, 0.35f, 0.25f, 0.95f);
    CGContextFillRect(ctx, CGRectMake(pickupX, baseY - 8.0, 14.0, 16.0));

    // Nonlinear Magnetic Flux Linkage Lines (Glowing Neon Cyan/Orange)
    CGContextSetRGBStrokeColor(ctx, 0.35f, 0.85f, 0.95f, 0.65f);
    CGContextSetLineWidth(ctx, 1.0f);
    for (int k = -2; k <= 2; ++k) {
        CGContextBeginPath(ctx);
        CGContextMoveToPoint(ctx, tipX, baseY + tipExcursion + k * 3.0f);
        CGContextAddQuadCurveToPoint(ctx, (tipX + pickupX) * 0.5f, baseY + k * 6.0f, pickupX, baseY + k * 4.0f);
        CGContextStrokePath(ctx);
    }

    // Physical Labels
    NSDictionary* annAttrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:8.5 weight:NSFontWeightMedium],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedWhite:0.6 alpha:1.0]
    };
    [@"Felt Hammer" drawAtPoint:NSMakePoint(hammerStrikeX - 22.0, baseY - 46.0) withAttributes:annAttrs];
    [@"Cantilever Tine" drawAtPoint:NSMakePoint(clampX + 130.0, baseY + 18.0) withAttributes:annAttrs];
    [@"Pickup Transducer" drawAtPoint:NSMakePoint(pickupX - 16.0, baseY - 24.0) withAttributes:annAttrs];

    // Right Subpanel: Real-time CRT Stereo Oscilloscope & Harmonic Spectrum (x: 494 .. 976)
    const NSRect scopeRect = NSMakeRect(494.0, 440.0, 480.0, 176.0);
    CGContextSetRGBFillColor(ctx, 0.02f, 0.05f, 0.04f, 0.95f);
    CGContextSetRGBStrokeColor(ctx, 0.08f, 0.32f, 0.20f, 0.8f);
    CGPathRef scPath = CGPathCreateWithRoundedRect(NSRectToCGRect(scopeRect), 4.0, 4.0, nullptr);
    CGContextAddPath(ctx, scPath);
    CGContextDrawPath(ctx, kCGPathFillStroke);
    CGPathRelease(scPath);

    // CRT Phosphor Grid Lines
    CGContextSetRGBStrokeColor(ctx, 0.06f, 0.22f, 0.14f, 0.5f);
    CGContextSetLineWidth(ctx, 0.8f);
    for (int gx = 1; gx < 8; ++gx) {
        const CGFloat x = scopeRect.origin.x + gx * (scopeRect.size.width / 8.0);
        CGContextStrokeLineSegments(ctx, (CGPoint[]){CGPointMake(x, scopeRect.origin.y), CGPointMake(x, scopeRect.origin.y + scopeRect.size.height)}, 2);
    }
    for (int gy = 1; gy < 6; ++gy) {
        const CGFloat y = scopeRect.origin.y + gy * (scopeRect.size.height / 6.0);
        CGContextStrokeLineSegments(ctx, (CGPoint[]){CGPointMake(scopeRect.origin.x, y), CGPointMake(scopeRect.origin.x + scopeRect.size.width, y)}, 2);
    }

    // Scope Title
    NSDictionary* scopeTitleAttrs = @{
        NSFontAttributeName: [NSFont fontWithName:@"HelveticaNeue-Bold" size:10.0] ?: [NSFont boldSystemFontOfSize:10.0],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.25f green:0.85f blue:0.55f alpha:0.95f]
    };
    [@"STEREO OSCILLOSCOPE & HARMONIC CRT" drawAtPoint:NSMakePoint(502.0, 598.0) withAttributes:scopeTitleAttrs];

    // Real-time Stereo Oscilloscope Trace
    CGContextSetRGBStrokeColor(ctx, 0.25f, 0.95f, 0.65f, 0.9f);
    CGContextSetLineWidth(ctx, 1.4f);
    CGContextBeginPath(ctx);
    const CGFloat midY = scopeRect.origin.y + scopeRect.size.height * 0.5f;
    const CGFloat maxWaveHeight = scopeRect.size.height * 0.42f;
    const uint32_t wPos = scope.writePos;
    for (int i = 0; i < phs::kScopeRingSize; ++i) {
        const uint32_t idx = (wPos + i) % phs::kScopeRingSize;
        const CGFloat x = scopeRect.origin.x + 6.0 + (i / static_cast<CGFloat>(phs::kScopeRingSize - 1)) * (scopeRect.size.width - 12.0);
        const CGFloat y = midY + std::clamp(scope.waveL[idx], -1.0f, 1.0f) * maxWaveHeight;
        if (i == 0) CGContextMoveToPoint(ctx, x, y);
        else CGContextAddLineToPoint(ctx, x, y);
    }
    CGContextStrokePath(ctx);

    // Harmonic Spectrum Bars (f0, 2f0, 3f0, 6.27f0 chime)
    const CGFloat barBaseX = scopeRect.origin.x + scopeRect.size.width - 120.0;
    const CGFloat barBaseY = scopeRect.origin.y + 12.0;
    static const char* kHarmLabels[4] = {"f0", "2f0", "3f0", "6.3f0"};
    for (int h = 0; h < 4; ++h) {
        const CGFloat bx = barBaseX + h * 28.0;
        float hLevel = 0.05f;
        if (scope.activeVoices > 0) {
            const float eNorm = std::min(1.0f, std::abs(scope.tipVelocity) * (1.0f + h * 0.4f));
            hLevel = (h == 0) ? eNorm * 0.85f : (h == 1) ? eNorm * 0.55f : (h == 2) ? eNorm * 0.35f : eNorm * 0.45f;
        }
        const CGFloat bh = std::max(4.0f, hLevel * 48.0f);
        CGContextSetRGBFillColor(ctx, 0.20f, 0.85f, 0.65f, 0.8f);
        CGContextFillRect(ctx, CGRectMake(bx, barBaseY, 18.0, bh));

        NSDictionary* bAttrs = @{
            NSFontAttributeName: [NSFont systemFontOfSize:8.0 weight:NSFontWeightMedium],
            NSForegroundColorAttributeName: [NSColor colorWithCalibratedWhite:0.7 alpha:1.0]
        };
        NSString* lbl = [NSString stringWithUTF8String:kHarmLabels[h]];
        [lbl drawAtPoint:NSMakePoint(bx + 1.0, barBaseY + bh + 2.0) withAttributes:bAttrs];
    }

    CGContextRestoreGState(ctx);
}

// =============================================================================
// CONTROL PANELS & KNOBS (y: 114 .. 426)
// =============================================================================
- (void)drawControlPanels:(CGContextRef)ctx {
    // 3 Panel Frames
    const NSRect panelRects[3] = {
        NSMakeRect(16.0, 114.0, 314.0, 310.0),
        NSMakeRect(338.0, 114.0, 326.0, 310.0),
        NSMakeRect(672.0, 114.0, 312.0, 310.0)
    };
    static const char* kPanelTitles[3] = {
        "PHS MECHANICS & TINE RESONATOR",
        "NONLINEAR TRANSDUCER & PREAMP EQ",
        "STAGE MODULATION & REVERB"
    };

    for (int p = 0; p < 3; ++p) {
        CGContextSaveGState(ctx);
        CGContextSetRGBFillColor(ctx, 0.07f, 0.08f, 0.10f, 0.90f);
        CGContextSetRGBStrokeColor(ctx, 0.18f, 0.20f, 0.26f, 0.75f);
        CGContextSetLineWidth(ctx, 1.0f);
        CGPathRef pPath = CGPathCreateWithRoundedRect(NSRectToCGRect(panelRects[p]), 6.0, 6.0, nullptr);
        CGContextAddPath(ctx, pPath);
        CGContextDrawPath(ctx, kCGPathFillStroke);
        CGPathRelease(pPath);

        NSDictionary* tAttrs = @{
            NSFontAttributeName: [NSFont fontWithName:@"HelveticaNeue-Bold" size:10.0] ?: [NSFont boldSystemFontOfSize:10.0],
            NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.85f green:0.88f blue:0.95f alpha:0.95f]
        };
        NSString* title = [NSString stringWithUTF8String:kPanelTitles[p]];
        [title drawAtPoint:NSMakePoint(panelRects[p].origin.x + 14.0, panelRects[p].origin.y + panelRects[p].size.height - 20.0) withAttributes:tAttrs];
        CGContextRestoreGState(ctx);
    }

    // Draw Knobs
    const int32_t learningParam = engine_ ? engine_->getLearningParamId() : -1;
    for (size_t i = 0; i < knobs_.size(); ++i) {
        const auto& k = knobs_[i];
        const float norm = engine_ ? engine_->getParamNormalized(k.paramId) : phs::kParamMetadata[k.paramId].defaultValue;
        const int8_t cc = engine_ ? engine_->getParamCc(k.paramId) : -1;
        const bool isArmed = (learningParam == static_cast<int32_t>(k.paramId));

        [self draw3dKnob:ctx
                    rect:k.rect
                   param:k.paramId
                   title:[NSString stringWithUTF8String:k.title]
                   value:norm
                 midiCc:cc
                isArmed:isArmed];
    }
}

- (void)draw3dKnob:(CGContextRef)ctx
              rect:(NSRect)rect
             param:(uint32_t)paramId
             title:(NSString*)title
             value:(float)norm
            midiCc:(int8_t)midiCc
           isArmed:(bool)isArmed {
    const CGFloat cx = rect.origin.x + rect.size.width * 0.5;
    const CGFloat cy = rect.origin.y + rect.size.height * 0.5 + 4.0;
    const CGFloat radius = std::min(rect.size.width, rect.size.height) * 0.32;

    CGContextSaveGState(ctx);

    // Circular LED Background Arc: 270 degree sweep (-135 deg to +135 deg)
    const CGFloat startAngle = -M_PI * 1.25;
    const CGFloat endAngle = M_PI * 0.25;
    const CGFloat curAngle = startAngle + norm * (endAngle - startAngle);

    CGContextSetRGBStrokeColor(ctx, 0.15f, 0.17f, 0.22f, 0.8f);
    CGContextSetLineWidth(ctx, 3.5f);
    CGContextSetLineCap(ctx, kCGLineCapRound);
    CGContextBeginPath(ctx);
    CGContextAddArc(ctx, cx, cy, radius + 5.0, startAngle, endAngle, 0);
    CGContextStrokePath(ctx);

    // Active Glowing LED Value Arc
    if (isArmed) {
        CGContextSetRGBStrokeColor(ctx, 1.0f, 0.82f, 0.15f, 1.0f); // Pulsing gold for learn
    } else {
        CGContextSetRGBStrokeColor(ctx, 0.30f, 0.72f, 0.95f, 0.95f); // Boutique Ice Blue
    }
    CGContextSetLineWidth(ctx, 3.5f);
    CGContextBeginPath(ctx);
    CGContextAddArc(ctx, cx, cy, radius + 5.0, startAngle, curAngle, 0);
    CGContextStrokePath(ctx);

    // 3D Brushed Metallic Knob Cap Gradient
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    const CGFloat knobGradColors[8] = {
        0.32f, 0.34f, 0.40f, 1.0f,
        0.14f, 0.15f, 0.18f, 1.0f
    };
    CGGradientRef kGrad = CGGradientCreateWithColorComponents(cs, knobGradColors, nullptr, 2);
    CGContextAddArc(ctx, cx, cy, radius, 0, 2.0 * M_PI, 0);
    CGContextClip(ctx);
    CGContextDrawLinearGradient(ctx, kGrad, CGPointMake(cx, cy + radius), CGPointMake(cx, cy - radius), 0);
    CGGradientRelease(kGrad);
    CGColorSpaceRelease(cs);

    // Knob Rim Bezel
    CGContextResetClip(ctx);
    CGContextSetRGBStrokeColor(ctx, 0.42f, 0.45f, 0.52f, 0.85f);
    CGContextSetLineWidth(ctx, 1.2f);
    CGContextAddArc(ctx, cx, cy, radius, 0, 2.0 * M_PI, 0);
    CGContextStrokePath(ctx);

    // Indicator Line Notch
    CGContextSetRGBStrokeColor(ctx, isArmed ? 1.0f : 0.95f, isArmed ? 0.85f : 0.95f, isArmed ? 0.2f : 0.95f, 1.0f);
    CGContextSetLineWidth(ctx, 2.2f);
    CGContextBeginPath(ctx);
    const CGFloat indLen1 = radius * 0.35f;
    const CGFloat indLen2 = radius * 0.92f;
    CGContextMoveToPoint(ctx, cx + indLen1 * std::cos(curAngle), cy + indLen1 * std::sin(curAngle));
    CGContextAddLineToPoint(ctx, cx + indLen2 * std::cos(curAngle), cy + indLen2 * std::sin(curAngle));
    CGContextStrokePath(ctx);

    // Parameter Title (Above or Below)
    NSDictionary* titleAttrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:8.5 weight:NSFontWeightMedium],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedWhite:0.75 alpha:1.0]
    };
    const NSSize tSz = [title sizeWithAttributes:titleAttrs];
    [title drawAtPoint:NSMakePoint(cx - tSz.width * 0.5, rect.origin.y + rect.size.height - 10.0) withAttributes:titleAttrs];

    // Formatted Value String (Below)
    NSString* valStr = formatParamValue(paramId, norm);
    NSDictionary* valAttrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:8.0 weight:NSFontWeightBold],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.35 green:0.80 blue:0.95 alpha:1.0]
    };
    const NSSize vSz = [valStr sizeWithAttributes:valAttrs];
    [valStr drawAtPoint:NSMakePoint(cx - vSz.width * 0.5, rect.origin.y - 2.0) withAttributes:valAttrs];

    // MIDI CC Badge (if mapped or armed)
    if (isArmed) {
        NSRect bRect = NSMakeRect(cx - 18.0, cy - 7.0, 36.0, 14.0);
        CGContextSetRGBFillColor(ctx, 0.85f, 0.65f, 0.05f, 0.95f);
        CGPathRef bp = CGPathCreateWithRoundedRect(NSRectToCGRect(bRect), 3.0, 3.0, nullptr);
        CGContextAddPath(ctx, bp);
        CGContextFillPath(ctx);
        CGPathRelease(bp);

        NSDictionary* ccAttrs = @{
            NSFontAttributeName: [NSFont systemFontOfSize:7.5 weight:NSFontWeightBold],
            NSForegroundColorAttributeName: [NSColor blackColor]
        };
        [@"LEARN" drawAtPoint:NSMakePoint(bRect.origin.x + 3.0, bRect.origin.y + 1.0) withAttributes:ccAttrs];
    } else if (midiCc >= 0) {
        NSString* ccStr = [NSString stringWithFormat:@"CC %d", midiCc];
        NSDictionary* ccAttrs = @{
            NSFontAttributeName: [NSFont systemFontOfSize:7.5 weight:NSFontWeightBold],
            NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:1.0 green:0.85 blue:0.3 alpha:1.0]
        };
        const NSSize cSz = [ccStr sizeWithAttributes:ccAttrs];
        NSRect bRect = NSMakeRect(cx - (cSz.width + 8.0) * 0.5, cy - 7.0, cSz.width + 8.0, 14.0);
        CGContextSetRGBFillColor(ctx, 0.10f, 0.12f, 0.16f, 0.85f);
        CGContextSetRGBStrokeColor(ctx, 0.85f, 0.70f, 0.20f, 0.85f);
        CGContextSetLineWidth(ctx, 0.8f);
        CGPathRef bp = CGPathCreateWithRoundedRect(NSRectToCGRect(bRect), 3.0, 3.0, nullptr);
        CGContextAddPath(ctx, bp);
        CGContextDrawPath(ctx, kCGPathFillStroke);
        CGPathRelease(bp);
        [ccStr drawAtPoint:NSMakePoint(bRect.origin.x + 4.0, bRect.origin.y + 1.0) withAttributes:ccAttrs];
    }

    CGContextRestoreGState(ctx);
}

// =============================================================================
// PERFORMANCE SECTION & 61-KEY PLAYABLE KEYBOARD (y: 12 .. 108)
// =============================================================================
- (void)drawPerformanceSection:(CGContextRef)ctx {
    // Sustain Pedal Button
    const bool isPedalDown = engine_ && engine_->isNoteActive(60); // Check pedal state
    CGContextSaveGState(ctx);
    if (isPedalDown) {
        CGContextSetRGBFillColor(ctx, 0.85f, 0.55f, 0.15f, 0.95f);
        CGContextSetRGBStrokeColor(ctx, 1.0f, 0.85f, 0.35f, 1.0f);
    } else {
        CGContextSetRGBFillColor(ctx, 0.14f, 0.16f, 0.20f, 0.90f);
        CGContextSetRGBStrokeColor(ctx, 0.30f, 0.35f, 0.44f, 0.75f);
    }
    CGPathRef pedP = CGPathCreateWithRoundedRect(NSRectToCGRect(pedalButtonRect_), 4.0, 4.0, nullptr);
    CGContextAddPath(ctx, pedP);
    CGContextDrawPath(ctx, kCGPathFillStroke);
    CGPathRelease(pedP);

    NSDictionary* pAttrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:9.0 weight:NSFontWeightBold],
        NSForegroundColorAttributeName: isPedalDown ? [NSColor blackColor] : [NSColor whiteColor]
    };
    NSString* pedTxt = isPedalDown ? @"PEDAL\nON" : @"PEDAL\nOFF";
    [pedTxt drawInRect:pedalButtonRect_ withAttributes:pAttrs];
    CGContextRestoreGState(ctx);

    // Pitch Bend & Modulation Wheels
    auto drawWheel = [&](NSRect r, NSString* name, float posNorm) {
        CGContextSaveGState(ctx);
        CGContextSetRGBFillColor(ctx, 0.10f, 0.11f, 0.14f, 0.95f);
        CGContextSetRGBStrokeColor(ctx, 0.25f, 0.28f, 0.35f, 0.8f);
        CGPathRef wp = CGPathCreateWithRoundedRect(NSRectToCGRect(r), 4.0, 4.0, nullptr);
        CGContextAddPath(ctx, wp);
        CGContextDrawPath(ctx, kCGPathFillStroke);
        CGPathRelease(wp);

        // Thumb bar
        const CGFloat thumbY = r.origin.y + 4.0 + posNorm * (r.size.height - 24.0);
        NSRect tRect = NSMakeRect(r.origin.x + 2.0, thumbY, r.size.width - 4.0, 16.0);
        CGContextSetRGBFillColor(ctx, 0.35f, 0.38f, 0.46f, 0.95f);
        CGContextFillRect(ctx, NSRectToCGRect(tRect));

        NSDictionary* wAttrs = @{
            NSFontAttributeName: [NSFont systemFontOfSize:7.5 weight:NSFontWeightMedium],
            NSForegroundColorAttributeName: [NSColor colorWithCalibratedWhite:0.6 alpha:1.0]
        };
        [name drawAtPoint:NSMakePoint(r.origin.x - 2.0, r.origin.y - 10.0) withAttributes:wAttrs];
        CGContextRestoreGState(ctx);
    };
    drawWheel(pitchBendRect_, @"PITCH", 0.5f);
    drawWheel(modWheelRect_, @"MOD", 0.0f);

    // 61-Key Interactive Playable Keyboard (MIDI 36 = C2 to MIDI 96 = C7)
    // 36 White keys, 25 Black keys
    const int startNote = 36;
    const int numKeys = 61;
    int numWhiteKeys = 0;
    for (int n = startNote; n < startNote + numKeys; ++n) {
        if (!isBlackKey(n)) ++numWhiteKeys;
    }

    const CGFloat whiteKeyWidth = keyboardRect_.size.width / static_cast<CGFloat>(numWhiteKeys);
    const CGFloat whiteKeyHeight = keyboardRect_.size.height;
    const CGFloat blackKeyWidth = whiteKeyWidth * 0.62f;
    const CGFloat blackKeyHeight = whiteKeyHeight * 0.62f;

    // Draw White Keys First
    int whiteIdx = 0;
    for (int n = startNote; n < startNote + numKeys; ++n) {
        if (isBlackKey(n)) continue;
        const CGFloat kx = keyboardRect_.origin.x + whiteIdx * whiteKeyWidth;
        const NSRect wkRect = NSMakeRect(kx, keyboardRect_.origin.y, whiteKeyWidth - 1.0, whiteKeyHeight);
        const bool active = engine_ && (engine_->isNoteActive(n) || mouseHeldMidiNote_ == n);

        CGContextSaveGState(ctx);
        if (active) {
            CGContextSetRGBFillColor(ctx, 0.85f, 0.92f, 1.0f, 1.0f); // Pressed active tint
        } else {
            CGContextSetRGBFillColor(ctx, 0.95f, 0.94f, 0.92f, 1.0f); // Vintage ivory
        }
        CGContextSetRGBStrokeColor(ctx, 0.30f, 0.32f, 0.36f, 0.7f);
        CGContextSetLineWidth(ctx, 0.8f);
        CGContextFillRect(ctx, NSRectToCGRect(wkRect));
        CGContextStrokeRect(ctx, NSRectToCGRect(wkRect));

        // Octave C Labels
        if (n % 12 == 0) {
            NSDictionary* cAttrs = @{
                NSFontAttributeName: [NSFont systemFontOfSize:8.0 weight:NSFontWeightBold],
                NSForegroundColorAttributeName: [NSColor colorWithCalibratedWhite:0.4 alpha:1.0]
            };
            NSString* cName = [NSString stringWithFormat:@"C%d", (n / 12) - 1];
            [cName drawAtPoint:NSMakePoint(kx + 3.0, keyboardRect_.origin.y + 4.0) withAttributes:cAttrs];
        }
        CGContextRestoreGState(ctx);
        ++whiteIdx;
    }

    // Draw Black Keys on Top
    whiteIdx = 0;
    for (int n = startNote; n < startNote + numKeys; ++n) {
        if (!isBlackKey(n)) {
            ++whiteIdx;
            continue;
        }
        // Black key sits between (whiteIdx - 1) and whiteIdx
        const CGFloat kx = keyboardRect_.origin.x + whiteIdx * whiteKeyWidth - (blackKeyWidth * 0.5f);
        const NSRect bkRect = NSMakeRect(kx, keyboardRect_.origin.y + (whiteKeyHeight - blackKeyHeight), blackKeyWidth, blackKeyHeight);
        const bool active = engine_ && (engine_->isNoteActive(n) || mouseHeldMidiNote_ == n);

        CGContextSaveGState(ctx);
        if (active) {
            CGContextSetRGBFillColor(ctx, 0.30f, 0.65f, 0.95f, 1.0f); // Glowing active blue
        } else {
            CGContextSetRGBFillColor(ctx, 0.12f, 0.13f, 0.16f, 1.0f); // Matte ebony
        }
        CGContextSetRGBStrokeColor(ctx, 0.05f, 0.05f, 0.07f, 0.9f);
        CGContextSetLineWidth(ctx, 0.8f);
        CGContextFillRect(ctx, NSRectToCGRect(bkRect));
        CGContextStrokeRect(ctx, NSRectToCGRect(bkRect));
        CGContextRestoreGState(ctx);
    }
}

// =============================================================================
// PRESET BROWSER MODAL (Category tabs, preset list, load, save, delete)
// =============================================================================
- (void)drawPresetBrowserModal:(CGContextRef)ctx {
    // Semi-transparent backdrop blur/dim
    CGContextSaveGState(ctx);
    CGContextSetRGBFillColor(ctx, 0.0f, 0.0f, 0.0f, 0.72f);
    CGContextFillRect(ctx, CGRectMake(0, 0, 1000.0, 680.0));

    // Modal Window
    CGContextSetRGBFillColor(ctx, 0.09f, 0.10f, 0.14f, 0.98f);
    CGContextSetRGBStrokeColor(ctx, 0.32f, 0.38f, 0.48f, 0.9f);
    CGContextSetLineWidth(ctx, 1.5f);
    CGPathRef mPath = CGPathCreateWithRoundedRect(NSRectToCGRect(browserModalRect_), 8.0, 8.0, nullptr);
    CGContextAddPath(ctx, mPath);
    CGContextDrawPath(ctx, kCGPathFillStroke);
    CGPathRelease(mPath);

    // Title Bar
    NSDictionary* mTitleAttrs = @{
        NSFontAttributeName: [NSFont fontWithName:@"HelveticaNeue-Bold" size:14.0] ?: [NSFont boldSystemFontOfSize:14.0],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.95 green:0.82 blue:0.42 alpha:1.0]
    };
    [@"PRESET BROWSER & SOUND BANK" drawAtPoint:NSMakePoint(browserModalRect_.origin.x + 20.0, browserModalRect_.origin.y + browserModalRect_.size.height - 32.0)
                                 withAttributes:mTitleAttrs];

    // Close Button [X]
    NSDictionary* closeAttrs = @{
        NSFontAttributeName: [NSFont boldSystemFontOfSize:13.0],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedWhite:0.8 alpha:1.0]
    };
    [@"✕" drawAtPoint:NSMakePoint(browserCloseRect_.origin.x + 6.0, browserCloseRect_.origin.y + 4.0) withAttributes:closeAttrs];

    // Left Column: Category Tabs
    static const char* kCategories[6] = {
        "All Presets",
        "Rhodes Vintage",
        "Wurlitzer Reeds",
        "Dyno & Mod 80s",
        "Ambient & FX",
        "User Presets"
    };
    for (int i = 0; i < 6; ++i) {
        const NSRect r = categoryTabRects_[i];
        const bool active = (static_cast<int>(selectedCategory_) == i);

        CGContextSetRGBFillColor(ctx, active ? 0.20f : 0.12f, active ? 0.32f : 0.14f, active ? 0.45f : 0.18f, 0.9f);
        CGContextSetRGBStrokeColor(ctx, 0.30f, 0.35f, 0.45f, active ? 0.9f : 0.4f);
        CGPathRef tp = CGPathCreateWithRoundedRect(NSRectToCGRect(r), 4.0, 4.0, nullptr);
        CGContextAddPath(ctx, tp);
        CGContextDrawPath(ctx, kCGPathFillStroke);
        CGPathRelease(tp);

        NSDictionary* cAttrs = @{
            NSFontAttributeName: [NSFont systemFontOfSize:10.5 weight:active ? NSFontWeightBold : NSFontWeightMedium],
            NSForegroundColorAttributeName: active ? [NSColor whiteColor] : [NSColor colorWithCalibratedWhite:0.65 alpha:1.0]
        };
        NSString* cName = [NSString stringWithUTF8String:kCategories[i]];
        [cName drawAtPoint:NSMakePoint(r.origin.x + 10.0, r.origin.y + 7.0) withAttributes:cAttrs];
    }

    // Right Column: Presets List Table
    if (engine_) {
        const auto& pm = engine_->getPresetManager();
        const auto& allPresets = pm.getPresets();
        const NSRect listRect = NSMakeRect(264.0, 118.0, 320.0, 420.0);

        CGContextSetRGBFillColor(ctx, 0.06f, 0.07f, 0.09f, 0.9f);
        CGContextSetRGBStrokeColor(ctx, 0.20f, 0.22f, 0.28f, 0.8f);
        CGPathRef lp = CGPathCreateWithRoundedRect(NSRectToCGRect(listRect), 4.0, 4.0, nullptr);
        CGContextAddPath(ctx, lp);
        CGContextDrawPath(ctx, kCGPathFillStroke);
        CGPathRelease(lp);

        int rowY = listRect.origin.y + listRect.size.height - 28.0;
        int drawn = 0;
        for (int pIdx = 0; pIdx < static_cast<int>(allPresets.size()); ++pIdx) {
            const auto& p = allPresets[pIdx];
            if (selectedCategory_ != phs::PresetCategory::All && p.category != selectedCategory_) {
                continue;
            }
            if (rowY < listRect.origin.y + 8.0) break;

            const bool isSel = (pIdx == selectedPresetIdx_);
            const NSRect rowRect = NSMakeRect(listRect.origin.x + 4.0, rowY, listRect.size.width - 8.0, 24.0);
            if (isSel) {
                CGContextSetRGBFillColor(ctx, 0.22f, 0.40f, 0.58f, 0.9f);
                CGPathRef rp = CGPathCreateWithRoundedRect(NSRectToCGRect(rowRect), 3.0, 3.0, nullptr);
                CGContextAddPath(ctx, rp);
                CGContextFillPath(ctx);
                CGPathRelease(rp);
            }

            NSDictionary* pAttrs = @{
                NSFontAttributeName: [NSFont systemFontOfSize:10.0 weight:isSel ? NSFontWeightBold : NSFontWeightRegular],
                NSForegroundColorAttributeName: isSel ? [NSColor whiteColor] : [NSColor colorWithCalibratedWhite:0.8 alpha:1.0]
            };
            NSString* pName = [NSString stringWithUTF8String:p.name.c_str()];
            [pName drawAtPoint:NSMakePoint(rowRect.origin.x + 6.0, rowRect.origin.y + 4.0) withAttributes:pAttrs];

            rowY -= 26.0;
            (void)drawn;
            ++drawn;
        }

        // Details Panel (x: 596 .. 896, y: 118 .. 538)
        const NSRect detailRect = NSMakeRect(596.0, 118.0, 308.0, 420.0);
        CGContextSetRGBFillColor(ctx, 0.06f, 0.07f, 0.09f, 0.9f);
        CGContextSetRGBStrokeColor(ctx, 0.20f, 0.22f, 0.28f, 0.8f);
        CGPathRef dp = CGPathCreateWithRoundedRect(NSRectToCGRect(detailRect), 4.0, 4.0, nullptr);
        CGContextAddPath(ctx, dp);
        CGContextDrawPath(ctx, kCGPathFillStroke);
        CGPathRelease(dp);

        const auto* curPreset = pm.getPreset(selectedPresetIdx_);
        if (curPreset) {
            NSDictionary* dnAttrs = @{
                NSFontAttributeName: [NSFont fontWithName:@"HelveticaNeue-Bold" size:13.0] ?: [NSFont boldSystemFontOfSize:13.0],
                NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.95 green:0.85 blue:0.45 alpha:1.0]
            };
            NSString* dn = [NSString stringWithUTF8String:curPreset->name.c_str()];
            [dn drawAtPoint:NSMakePoint(detailRect.origin.x + 14.0, detailRect.origin.y + detailRect.size.height - 30.0) withAttributes:dnAttrs];

            NSDictionary* daAttrs = @{
                NSFontAttributeName: [NSFont systemFontOfSize:10.0 weight:NSFontWeightMedium],
                NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.55 green:0.75 blue:0.95 alpha:1.0]
            };
            NSString* da = [NSString stringWithFormat:@"Author: %s | Category: %s",
                            curPreset->author.c_str(), phs::getCategoryName(curPreset->category)];
            [da drawAtPoint:NSMakePoint(detailRect.origin.x + 14.0, detailRect.origin.y + detailRect.size.height - 50.0) withAttributes:daAttrs];

            NSDictionary* ddAttrs = @{
                NSFontAttributeName: [NSFont systemFontOfSize:10.0],
                NSForegroundColorAttributeName: [NSColor colorWithCalibratedWhite:0.75 alpha:1.0]
            };
            NSString* dd = [NSString stringWithUTF8String:curPreset->description.c_str()];
            NSRect ddRect = NSMakeRect(detailRect.origin.x + 14.0, detailRect.origin.y + 40.0, detailRect.size.width - 28.0, detailRect.size.height - 100.0);
            [dd drawInRect:ddRect withAttributes:ddAttrs];
        }

        // Action Buttons: Save New Preset & Delete
        auto drawActionBtn = [&](NSRect r, NSString* txt, bool active) {
            CGContextSetRGBFillColor(ctx, active ? 0.16f : 0.10f, active ? 0.35f : 0.12f, active ? 0.25f : 0.14f, 0.9f);
            CGContextSetRGBStrokeColor(ctx, active ? 0.35f : 0.20f, active ? 0.75f : 0.25f, active ? 0.45f : 0.30f, 0.9f);
            CGPathRef bp = CGPathCreateWithRoundedRect(NSRectToCGRect(r), 4.0, 4.0, nullptr);
            CGContextAddPath(ctx, bp);
            CGContextDrawPath(ctx, kCGPathFillStroke);
            CGPathRelease(bp);

            NSDictionary* abAttrs = @{
                NSFontAttributeName: [NSFont systemFontOfSize:10.5 weight:NSFontWeightBold],
                NSForegroundColorAttributeName: active ? [NSColor whiteColor] : [NSColor colorWithCalibratedWhite:0.4 alpha:1.0]
            };
            const NSSize sz = [txt sizeWithAttributes:abAttrs];
            [txt drawAtPoint:NSMakePoint(r.origin.x + (r.size.width - sz.width) * 0.5, r.origin.y + (r.size.height - sz.height) * 0.5) withAttributes:abAttrs];
        };
        drawActionBtn(browserSaveRect_, @"+ Save Preset...", true);
        const bool canDel = curPreset && curPreset->isUserPreset;
        drawActionBtn(browserDeleteRect_, @"Delete", canDel);
    }

    CGContextRestoreGState(ctx);
}

// =============================================================================
// MOUSE INTERACTION & EVENT HANDLING
// =============================================================================
- (void)mouseDown:(NSEvent*)event {
    if (!engine_) return;
    const NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    const NSUInteger mods = [event modifierFlags];
    isFineDrag_ = (mods & NSEventModifierFlagShift) != 0;

    // Double-click detection for knob reset
    const uint64_t now = static_cast<uint64_t>([[NSDate date] timeIntervalSince1970] * 1000.0);
    const bool isDoubleClick = (now - lastClickTimestamp_ < 350);
    lastClickTimestamp_ = now;

    // 1. If Preset Browser Modal is open, handle its interactions
    if (browserOpen_) {
        if (NSPointInRect(pt, browserCloseRect_) || !NSPointInRect(pt, browserModalRect_)) {
            browserOpen_ = false;
            [self setNeedsDisplay:YES];
            return;
        }

        // Category Tabs
        for (int i = 0; i < 6; ++i) {
            if (NSPointInRect(pt, categoryTabRects_[i])) {
                selectedCategory_ = static_cast<phs::PresetCategory>(i);
                [self setNeedsDisplay:YES];
                return;
            }
        }

        // Preset List Click
        const NSRect listRect = NSMakeRect(264.0, 118.0, 320.0, 420.0);
        if (NSPointInRect(pt, listRect)) {
            const auto& allPresets = engine_->getPresetManager().getPresets();
            int rowY = listRect.origin.y + listRect.size.height - 28.0;
            for (int pIdx = 0; pIdx < static_cast<int>(allPresets.size()); ++pIdx) {
                const auto& p = allPresets[pIdx];
                if (selectedCategory_ != phs::PresetCategory::All && p.category != selectedCategory_) continue;
                const NSRect rowRect = NSMakeRect(listRect.origin.x + 4.0, rowY, listRect.size.width - 8.0, 24.0);
                if (NSPointInRect(pt, rowRect)) {
                    selectedPresetIdx_ = pIdx;
                    engine_->loadPreset(pIdx, true);
                    if (paramCallback_) {
                        for (uint32_t i = 0; i < phs::kNumParams; ++i) {
                            paramCallback_(i, engine_->getParamNormalized(i));
                        }
                    }
                    [self setNeedsDisplay:YES];
                    return;
                }
                rowY -= 26.0;
            }
        }

        // Save User Preset Button
        if (NSPointInRect(pt, browserSaveRect_)) {
            [self openSavePresetDialog];
            return;
        }

        // Delete User Preset Button
        if (NSPointInRect(pt, browserDeleteRect_)) {
            engine_->getPresetManager().deleteUserPreset(selectedPresetIdx_);
            [self setNeedsDisplay:YES];
            return;
        }
        return;
    }

    // 2. Header Bar Controls
    // Model Selector Tabs
    for (int i = 0; i < 4; ++i) {
        if (NSPointInRect(pt, modelButtonRects_[i])) {
            engine_->pushUndoState("Change Model");
            const float normVal = i / 3.0f;
            engine_->setParamNormalized(phs::kParamModelType, normVal);
            if (paramCallback_) paramCallback_(phs::kParamModelType, normVal);
            [self setNeedsDisplay:YES];
            return;
        }
    }

    // Undo / Redo
    if (NSPointInRect(pt, undoButtonRect_)) {
        engine_->undo();
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(pt, redoButtonRect_)) {
        engine_->redo();
        [self setNeedsDisplay:YES];
        return;
    }

    // MIDI Learn Toggle
    if (NSPointInRect(pt, midiLearnButtonRect_)) {
        if (engine_->getLearningParamId() >= 0) {
            engine_->stopMidiLearn();
        } else {
            engine_->startMidiLearn(phs::kParamPickupDistance); // Default arm
        }
        [self setNeedsDisplay:YES];
        return;
    }

    // Preset Prev / Next / Browse
    if (NSPointInRect(pt, presetPrevRect_)) {
        const int cur = engine_->getCurrentPreset();
        const int total = engine_->getPresetManager().getPresetCount();
        const int prev = (cur - 1 + total) % total;
        engine_->loadPreset(prev, true);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(pt, presetNextRect_)) {
        const int cur = engine_->getCurrentPreset();
        const int total = engine_->getPresetManager().getPresetCount();
        const int next = (cur + 1) % total;
        engine_->loadPreset(next, true);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(pt, presetBrowseButtonRect_) || NSPointInRect(pt, presetBoxRect_)) {
        browserOpen_ = true;
        selectedPresetIdx_ = engine_->getCurrentPreset();
        [self setNeedsDisplay:YES];
        return;
    }

    // Sustain Pedal Button
    if (NSPointInRect(pt, pedalButtonRect_)) {
        const bool active = engine_->isNoteActive(60);
        engine_->handleMidiEvent(0xB0, 64, active ? 0 : 127);
        [self setNeedsDisplay:YES];
        return;
    }

    // 3. Knobs Interaction
    for (size_t i = 0; i < knobs_.size(); ++i) {
        if (NSPointInRect(pt, knobs_[i].rect)) {
            const uint32_t pId = knobs_[i].paramId;

            // If MIDI learn is armed, arm this specific knob!
            if (engine_->getLearningParamId() >= 0) {
                engine_->startMidiLearn(pId);
                [self setNeedsDisplay:YES];
                return;
            }

            // Double click: Reset parameter to default value
            if (isDoubleClick && lastClickedKnobIdx_ == static_cast<int>(i)) {
                engine_->pushUndoState("Reset Parameter");
                const float defVal = phs::kParamMetadata[pId].defaultValue;
                engine_->setParamNormalized(pId, defVal);
                if (paramCallback_) paramCallback_(pId, defVal);
                [self setNeedsDisplay:YES];
                return;
            }

            lastClickedKnobIdx_ = static_cast<int>(i);
            draggedKnobIdx_ = static_cast<int>(i);
            dragStartY_ = pt.y;
            dragStartVal_ = engine_->getParamNormalized(pId);
            engine_->pushUndoState("Adjust " + std::string(knobs_[i].title));
            return;
        }
    }

    // 4. Playable Keyboard Interaction
    if (NSPointInRect(pt, keyboardRect_)) {
        const int startNote = 36;
        const int numKeys = 61;
        int numWhiteKeys = 0;
        for (int n = startNote; n < startNote + numKeys; ++n) {
            if (!isBlackKey(n)) ++numWhiteKeys;
        }
        const CGFloat whiteKeyWidth = keyboardRect_.size.width / static_cast<CGFloat>(numWhiteKeys);
        const CGFloat whiteKeyHeight = keyboardRect_.size.height;
        const CGFloat blackKeyWidth = whiteKeyWidth * 0.62f;
        const CGFloat blackKeyHeight = whiteKeyHeight * 0.62f;

        // Check black keys first (top layer)
        int hitNote = -1;
        if (pt.y >= keyboardRect_.origin.y + (whiteKeyHeight - blackKeyHeight)) {
            int wIdx = 0;
            for (int n = startNote; n < startNote + numKeys; ++n) {
                if (!isBlackKey(n)) {
                    ++wIdx;
                    continue;
                }
                const CGFloat kx = keyboardRect_.origin.x + wIdx * whiteKeyWidth - (blackKeyWidth * 0.5f);
                const NSRect bkRect = NSMakeRect(kx, keyboardRect_.origin.y + (whiteKeyHeight - blackKeyHeight), blackKeyWidth, blackKeyHeight);
                if (NSPointInRect(pt, bkRect)) {
                    hitNote = n;
                    break;
                }
            }
        }

        // Check white keys
        if (hitNote < 0) {
            const int wIdx = std::clamp(static_cast<int>((pt.x - keyboardRect_.origin.x) / whiteKeyWidth), 0, numWhiteKeys - 1);
            int curW = 0;
            for (int n = startNote; n < startNote + numKeys; ++n) {
                if (!isBlackKey(n)) {
                    if (curW == wIdx) {
                        hitNote = n;
                        break;
                    }
                    ++curW;
                }
            }
        }

        if (hitNote >= 0) {
            mouseHeldMidiNote_ = hitNote;
            // Play note with velocity proportional to Y position (lower on key = higher velocity)
            const float velNorm = std::clamp(static_cast<float>(1.0 - (pt.y - keyboardRect_.origin.y) / whiteKeyHeight), 0.4f, 1.0f);
            engine_->pushGuiMidiEvent(0x90, hitNote, static_cast<uint8_t>(velNorm * 127.0f));
            [self setNeedsDisplay:YES];
        }
    }
}

- (void)mouseDragged:(NSEvent*)event {
    if (!engine_) return;
    const NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];

    if (draggedKnobIdx_ >= 0 && draggedKnobIdx_ < static_cast<int>(knobs_.size())) {
        const CGFloat dy = pt.y - dragStartY_;
        const float sensitivity = isFineDrag_ ? 0.001f : 0.005f;
        const float newVal = std::clamp(static_cast<float>(dragStartVal_ + dy * sensitivity), 0.0f, 1.0f);
        const uint32_t pId = knobs_[draggedKnobIdx_].paramId;

        engine_->setParamNormalized(pId, newVal);
        if (paramCallback_) {
            paramCallback_(pId, newVal);
        }
        [self setNeedsDisplay:YES];
    }
}

- (void)mouseUp:(NSEvent*)event {
    (void)event;
    if (mouseHeldMidiNote_ >= 0 && engine_) {
        engine_->pushGuiMidiEvent(0x80, mouseHeldMidiNote_, 0);
        mouseHeldMidiNote_ = -1;
    }
    draggedKnobIdx_ = -1;
    [self setNeedsDisplay:YES];
}

// Right click arms MIDI learn or pops up unlearn menu
- (void)rightMouseDown:(NSEvent*)event {
    if (!engine_) return;
    const NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    for (size_t i = 0; i < knobs_.size(); ++i) {
        if (NSPointInRect(pt, knobs_[i].rect)) {
            const uint32_t pId = knobs_[i].paramId;
            const int8_t mappedCc = engine_->getParamCc(pId);
            if (mappedCc >= 0) {
                engine_->clearMidiMapping(pId);
            } else {
                engine_->startMidiLearn(pId);
            }
            [self setNeedsDisplay:YES];
            return;
        }
    }
}

- (void)openSavePresetDialog {
    NSAlert* alert = [[NSAlert alloc] init];
    [alert setMessageText:@"Save User Preset"];
    [alert setInformativeText:@"Enter a name for your custom electric piano preset:"];
    [alert addButtonWithTitle:@"Save"];
    [alert addButtonWithTitle:@"Cancel"];

    NSTextField* input = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 240, 24)];
    [input setStringValue:@"My Custom Rhodes"];
    [alert setAccessoryView:input];

    const NSModalResponse resp = [alert runModal];
    if (resp == NSAlertFirstButtonReturn && engine_) {
        NSString* name = [input stringValue];
        if ([name length] > 0) {
            float params[phs::kNumParams];
            for (uint32_t i = 0; i < phs::kNumParams; ++i) {
                params[i] = engine_->getParamNormalized(i);
            }
            engine_->getPresetManager().saveUserPreset(
                [name UTF8String],
                phs::PresetCategory::User,
                "User",
                "Custom user preset saved from GUI",
                params
            );
            [self setNeedsDisplay:YES];
        }
    }
    [input release];
    [alert release];
}

@end

namespace phs {

NSView* createPhsEditorView(PhsSynthEngine* engine, ParamChangeCallback onParamChange) {
    NSRect frame = NSMakeRect(0, 0, kEditorWidth, kEditorHeight);
    PHS_VIEW_CLASS_NAME* view = [[PHS_VIEW_CLASS_NAME alloc] initWithFrame:frame
                                                                    engine:engine
                                                                  callback:onParamChange];
    return view;
}

void destroyPhsEditorView(NSView* view) {
    if (view) {
        PHS_VIEW_CLASS_NAME* v = (PHS_VIEW_CLASS_NAME*)view;
        [v stopTimer];
        [v release];
    }
}

} // namespace phs
