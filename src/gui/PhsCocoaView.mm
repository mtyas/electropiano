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
    const char* group;
};

static bool isBlackKey(int midiNote) {
    const int n = midiNote % 12;
    return (n == 1 || n == 3 || n == 6 || n == 8 || n == 10);
}

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
            return [NSString stringWithFormat:@"%.0f %%", norm * 100.0f];
        case phs::kParamHammerHysteresis:
            return [NSString stringWithFormat:@"%.2f", val];
        case phs::kParamStrikePosition:
            return [NSString stringWithFormat:@"%.1f mm", val * 100.0f];
        case phs::kParamTineDecay:
            return [NSString stringWithFormat:@"%.1f s", val];
        case phs::kParamTineBell:
            return [NSString stringWithFormat:@"%.0f %%", norm * 100.0f];
        case phs::kParamToneBarCoupling:
            return [NSString stringWithFormat:@"%.0f %%", norm * 100.0f];
        case phs::kParamKeyNoise:
            return [NSString stringWithFormat:@"%.1f dB", -40.0f + norm * 40.0f];
        case phs::kParamPickupDistance:
            return [NSString stringWithFormat:@"%.2f mm", 0.95f - 0.71f * norm];
        case phs::kParamPickupSymmetry:
            return [NSString stringWithFormat:@"%+.2f", -0.5f + norm];
        case phs::kParamPickupResonance:
            return [NSString stringWithFormat:@"Q = %.1f", 0.5f + norm * 4.0f];
        case phs::kParamPreampDrive:
            return [NSString stringWithFormat:@"%.1f dB", norm * 24.0f];
        case phs::kParamBassBoost:
        case phs::kParamTrebleBoost:
            return [NSString stringWithFormat:@"%+.1f dB", -12.0f + norm * 24.0f];
        case phs::kParamTremoloDepth:
        case phs::kParamPhaserMix:
        case phs::kParamChorusMix:
        case phs::kParamReverbMix:
            return [NSString stringWithFormat:@"%.0f %%", norm * 100.0f];
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
    NSRect pedalSwitchRect_;
    NSRect pitchBendRect_;
    NSRect modWheelRect_;
    NSRect keyboardRect_;

    // Preset Browser modal
    bool browserOpen_;
    NSRect browserModalRect_;
    NSRect browserCloseRect_;
    NSRect browserSaveRect_;
    NSRect browserDeleteRect_;
    NSRect categoryTabRects_[6];
    phs::PresetCategory selectedCategory_;
    int selectedPresetIdx_;

    // Mouse Interaction
    int draggedKnobIdx_;
    CGFloat dragStartY_;
    float dragStartVal_;
    bool isFineDrag_;
    int mouseHeldMidiNote_;
    uint64_t lastClickTimestamp_;
    int lastClickedKnobIdx_;
    uint64_t animFrameCounter_;
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
        browserOpen_ = false;
        selectedCategory_ = phs::PresetCategory::All;
        selectedPresetIdx_ = 0;
        lastClickTimestamp_ = 0;
        lastClickedKnobIdx_ = -1;
        animFrameCounter_ = 0;

        [self setupLayout];

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
    return NO;
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (void)onTimerTick:(NSTimer*)timer {
    (void)timer;
    ++animFrameCounter_;
    [self setNeedsDisplay:YES];
}

- (void)setupLayout {
    knobs_.clear();

    // 1. Top Header Bar (y: 622 .. 672)
    for (int i = 0; i < 4; ++i) {
        modelButtonRects_[i] = NSMakeRect(208.0 + i * 92.0, 634.0, 88.0, 28.0);
    }
    undoButtonRect_         = NSMakeRect(588.0, 634.0, 54.0, 28.0);
    redoButtonRect_         = NSMakeRect(646.0, 634.0, 54.0, 28.0);
    midiLearnButtonRect_    = NSMakeRect(708.0, 634.0, 86.0, 28.0);
    presetPrevRect_         = NSMakeRect(802.0, 634.0, 24.0, 28.0);
    presetBoxRect_          = NSMakeRect(828.0, 634.0, 118.0, 28.0);
    presetNextRect_         = NSMakeRect(948.0, 634.0, 24.0, 28.0);
    presetBrowseButtonRect_ = NSMakeRect(974.0, 634.0, 18.0, 28.0);

    // 2. Control Panels & Knobs (y: 114 .. 420)
    // Panel 1: Mechanics & Tine (x: 18 .. 328)
    const uint32_t mTop[4] = {
        phs::kParamHammerHardness,
        phs::kParamHammerHysteresis,
        phs::kParamStrikePosition,
        phs::kParamTineDecay
    };
    for (int i = 0; i < 4; ++i) {
        knobs_.push_back({mTop[i], NSMakeRect(26.0 + i * 74.0, 328.0, 66.0, 72.0), phs::kParamMetadata[mTop[i]].name, "MECHANICS"});
    }
    const uint32_t mBot[3] = {
        phs::kParamTineBell,
        phs::kParamToneBarCoupling,
        phs::kParamKeyNoise
    };
    for (int i = 0; i < 3; ++i) {
        knobs_.push_back({mBot[i], NSMakeRect(58.0 + i * 86.0, 234.0, 68.0, 72.0), phs::kParamMetadata[mBot[i]].name, "MECHANICS"});
    }

    // Panel 2: Nonlinear Transducer & Preamp EQ (x: 340 .. 666)
    const uint32_t tTop[3] = {
        phs::kParamPickupDistance,
        phs::kParamPickupSymmetry,
        phs::kParamPickupResonance
    };
    for (int i = 0; i < 3; ++i) {
        knobs_.push_back({tTop[i], NSMakeRect(358.0 + i * 98.0, 328.0, 72.0, 72.0), phs::kParamMetadata[tTop[i]].name, "PICKUP"});
    }
    const uint32_t tBot[3] = {
        phs::kParamPreampDrive,
        phs::kParamBassBoost,
        phs::kParamTrebleBoost
    };
    for (int i = 0; i < 3; ++i) {
        knobs_.push_back({tBot[i], NSMakeRect(358.0 + i * 98.0, 234.0, 72.0, 72.0), phs::kParamMetadata[tBot[i]].name, "PREAMP"});
    }

    // Panel 3: Modulation & Effects (x: 678 .. 982)
    const uint32_t fTop[4] = {
        phs::kParamTremoloDepth,
        phs::kParamTremoloRate,
        phs::kParamPhaserMix,
        phs::kParamPhaserRate
    };
    for (int i = 0; i < 4; ++i) {
        knobs_.push_back({fTop[i], NSMakeRect(690.0 + i * 72.0, 328.0, 64.0, 72.0), phs::kParamMetadata[fTop[i]].name, "MODULATION"});
    }
    const uint32_t fBot[5] = {
        phs::kParamChorusMix,
        phs::kParamReverbMix,
        phs::kParamReverbDecay,
        phs::kParamVelocityCurve,
        phs::kParamMasterVolume
    };
    for (int i = 0; i < 5; ++i) {
        knobs_.push_back({fBot[i], NSMakeRect(686.0 + i * 58.0, 234.0, 56.0, 72.0), phs::kParamMetadata[fBot[i]].name, "SPACE"});
    }

    // 3. Performance & Keyboard (y: 12 .. 104)
    pedalSwitchRect_ = NSMakeRect(18.0, 58.0, 54.0, 42.0);
    pitchBendRect_   = NSMakeRect(78.0, 14.0, 24.0, 86.0);
    modWheelRect_    = NSMakeRect(108.0, 14.0, 24.0, 86.0);
    keyboardRect_    = NSMakeRect(142.0, 12.0, 842.0, 92.0);

    // 4. Preset Browser Modal
    browserModalRect_  = NSMakeRect(80.0, 55.0, 840.0, 560.0);
    browserCloseRect_  = NSMakeRect(885.0, 582.0, 24.0, 24.0);
    browserSaveRect_   = NSMakeRect(480.0, 68.0, 140.0, 32.0);
    browserDeleteRect_ = NSMakeRect(632.0, 68.0, 96.0, 32.0);
    for (int i = 0; i < 6; ++i) {
        categoryTabRects_[i] = NSMakeRect(96.0, 524.0 - (i + 1) * 36.0, 150.0, 30.0);
    }
}

// Keyboard shortcuts
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
        if (isShift) engine_->redo();
        else engine_->undo();
        [self setNeedsDisplay:YES];
        return;
    } else if (isCmd && [chars isEqualToString:@"y"]) {
        engine_->redo();
        [self setNeedsDisplay:YES];
        return;
    } else if ([chars isEqualToString:@" "]) {
        engine_->handleMidiEvent(0xB0, 64, (engine_->isNoteActive(60) ? 0 : 127));
        [self setNeedsDisplay:YES];
        return;
    }
    [super keyDown:event];
}

// =============================================================================
// MAIN DRAW ROUTINE
// =============================================================================
- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    CGContextRef ctx = (CGContextRef)[[NSGraphicsContext currentContext] CGContext];
    if (!ctx) return;

    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();

    // 1. Ultra-luxurious Brushed Obsidian / Charcoal Chassis with Ambient Lighting
    const CGFloat bgColors[12] = {
        0.14f, 0.15f, 0.18f, 1.0f, // Top edge subtle highlight
        0.08f, 0.09f, 0.11f, 1.0f, // Mid chassis
        0.04f, 0.04f, 0.06f, 1.0f  // Bottom deep shadow
    };
    const CGFloat bgLocations[3] = {0.0f, 0.45f, 1.0f};
    CGGradientRef bgGrad = CGGradientCreateWithColorComponents(cs, bgColors, bgLocations, 3);
    CGContextDrawLinearGradient(ctx, bgGrad, CGPointMake(0, 680), CGPointMake(0, 0), 0);
    CGGradientRelease(bgGrad);

    // Fine chassis grain & border rim
    CGContextSetRGBStrokeColor(ctx, 0.28f, 0.30f, 0.36f, 0.6f);
    CGContextSetLineWidth(ctx, 1.0f);
    CGContextStrokeRect(ctx, CGRectMake(0.5, 0.5, 999.0, 679.0));

    // 2. Draw Top Header Bar
    [self drawHeaderBar:ctx];

    // 3. Draw Physical Laboratory & Oscilloscope
    [self drawLaboratorySection:ctx];

    // 4. Draw Boutique Hardware Control Panels & Knobs
    [self drawControlPanels:ctx];

    // 5. Draw Performance Controls & Playable 61-Key Keyboard
    [self drawPerformanceSection:ctx];

    // 6. Draw Preset Browser Modal (if opened)
    if (browserOpen_) {
        [self drawPresetBrowserModal:ctx];
    }

    CGColorSpaceRelease(cs);
}

// =============================================================================
// 1. TOP HEADER & BOUTIQUE BADGE
// =============================================================================
- (void)drawHeaderBar:(CGContextRef)ctx {
    // Brushed Titanium Faceplate (y: 624 .. 676)
    CGContextSaveGState(ctx);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    const CGFloat barColors[8] = {
        0.16f, 0.17f, 0.21f, 1.0f,
        0.09f, 0.10f, 0.12f, 1.0f
    };
    CGGradientRef barGrad = CGGradientCreateWithColorComponents(cs, barColors, nullptr, 2);
    CGContextDrawLinearGradient(ctx, barGrad, CGPointMake(0, 674), CGPointMake(0, 624), 0);
    CGGradientRelease(barGrad);
    CGColorSpaceRelease(cs);

    // Bottom bevel line
    CGContextSetRGBStrokeColor(ctx, 0.26f, 0.28f, 0.34f, 0.8f);
    CGContextSetLineWidth(ctx, 1.0f);
    CGContextStrokeLineSegments(ctx, (CGPoint[]){CGPointMake(0, 624), CGPointMake(1000, 624)}, 2);

    // Vintage Engraved Screws in Faceplate
    auto drawScrew = [&](CGFloat sx, CGFloat sy) {
        CGContextSetRGBFillColor(ctx, 0.25f, 0.27f, 0.32f, 0.9f);
        CGContextSetRGBStrokeColor(ctx, 0.08f, 0.09f, 0.11f, 1.0f);
        CGContextSetLineWidth(ctx, 0.8f);
        CGContextFillEllipseInRect(ctx, CGRectMake(sx - 3.5, sy - 3.5, 7.0, 7.0));
        CGContextStrokeEllipseInRect(ctx, CGRectMake(sx - 3.5, sy - 3.5, 7.0, 7.0));
        CGContextSetRGBStrokeColor(ctx, 0.12f, 0.13f, 0.16f, 0.9f);
        CGContextStrokeLineSegments(ctx, (CGPoint[]){CGPointMake(sx - 2.5, sy), CGPointMake(sx + 2.5, sy)}, 2);
    };
    drawScrew(8.0, 650.0);
    drawScrew(992.0, 650.0);

    // Gold Foil Brand Plate: "PyPHS"
    NSDictionary* brandAttrs = @{
        NSFontAttributeName: [NSFont fontWithName:@"Optima-ExtraBlack" size:18.0] ?: [NSFont boldSystemFontOfSize:18.0],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.96f green:0.84f blue:0.45f alpha:1.0f]
    };
    [@"PyPHS" drawAtPoint:NSMakePoint(24.0, 644.0) withAttributes:brandAttrs];

    NSDictionary* subBrandAttrs = @{
        NSFontAttributeName: [NSFont fontWithName:@"HelveticaNeue-Bold" size:8.5] ?: [NSFont boldSystemFontOfSize:8.5],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.55f green:0.75f blue:0.92f alpha:0.95f]
    };
    [@"PORT-HAMILTONIAN PHYSICAL MODEL" drawAtPoint:NSMakePoint(24.0, 634.0) withAttributes:subBrandAttrs];

    // Model Selector Tabs
    const int currentModel = engine_ ? std::clamp(static_cast<int>(std::round(engine_->getParamNormalized(phs::kParamModelType) * 3.0f)), 0, 3) : 0;
    static const char* kModelNames[4] = {"Rhodes Mk I", "Rhodes Dyno", "Wurlitzer 200A", "Custom PHS"};

    for (int i = 0; i < 4; ++i) {
        const NSRect r = modelButtonRects_[i];
        const bool active = (i == currentModel);

        CGContextSaveGState(ctx);
        if (active) {
            CGContextSetRGBFillColor(ctx, 0.18f, 0.35f, 0.48f, 0.95f);
            CGContextSetRGBStrokeColor(ctx, 0.42f, 0.82f, 0.98f, 0.95f);
            CGContextSetShadowWithColor(ctx, CGSizeMake(0, 0), 6.0, [[NSColor colorWithCalibratedRed:0.3 green:0.75 blue:0.95 alpha:0.45] CGColor]);
        } else {
            CGContextSetRGBFillColor(ctx, 0.11f, 0.12f, 0.15f, 0.85f);
            CGContextSetRGBStrokeColor(ctx, 0.22f, 0.25f, 0.30f, 0.7f);
        }
        CGPathRef round = CGPathCreateWithRoundedRect(NSRectToCGRect(r), 4.0, 4.0, nullptr);
        CGContextAddPath(ctx, round);
        CGContextDrawPath(ctx, kCGPathFillStroke);
        CGPathRelease(round);

        // Little jewel LED on the left of active tab
        if (active) {
            CGContextSetRGBFillColor(ctx, 0.45f, 0.95f, 1.0f, 1.0f);
            CGContextFillEllipseInRect(ctx, CGRectMake(r.origin.x + 8.0, r.origin.y + 11.0, 6.0, 6.0));
        }

        NSDictionary* textAttrs = @{
            NSFontAttributeName: [NSFont fontWithName:@"HelveticaNeue-Bold" size:10.0] ?: [NSFont boldSystemFontOfSize:10.0],
            NSForegroundColorAttributeName: active ? [NSColor whiteColor] : [NSColor colorWithCalibratedWhite:0.65 alpha:1.0]
        };
        NSString* title = [NSString stringWithUTF8String:kModelNames[i]];
        const NSSize sz = [title sizeWithAttributes:textAttrs];
        const CGFloat tx = r.origin.x + (active ? 10.0 : 0.0) + (r.size.width - (active ? 10.0 : 0.0) - sz.width) * 0.5;
        [title drawAtPoint:NSMakePoint(tx, r.origin.y + (r.size.height - sz.height) * 0.5) withAttributes:textAttrs];
        CGContextRestoreGState(ctx);
    }

    // Undo / Redo Buttons
    auto drawBtn = [&](NSRect r, NSString* title, bool enabled) {
        CGContextSaveGState(ctx);
        CGContextSetRGBFillColor(ctx, 0.11f, 0.12f, 0.15f, enabled ? 0.9f : 0.4f);
        CGContextSetRGBStrokeColor(ctx, 0.25f, 0.28f, 0.35f, enabled ? 0.8f : 0.3f);
        CGPathRef p = CGPathCreateWithRoundedRect(NSRectToCGRect(r), 4.0, 4.0, nullptr);
        CGContextAddPath(ctx, p);
        CGContextDrawPath(ctx, kCGPathFillStroke);
        CGPathRelease(p);

        NSDictionary* attrs = @{
            NSFontAttributeName: [NSFont systemFontOfSize:10.0 weight:NSFontWeightMedium],
            NSForegroundColorAttributeName: enabled ? [NSColor whiteColor] : [NSColor colorWithCalibratedWhite:0.35 alpha:1.0]
        };
        const NSSize sz = [title sizeWithAttributes:attrs];
        [title drawAtPoint:NSMakePoint(r.origin.x + (r.size.width - sz.width) * 0.5, r.origin.y + (r.size.height - sz.height) * 0.5) withAttributes:attrs];
        CGContextRestoreGState(ctx);
    };
    drawBtn(undoButtonRect_, @"↺ Undo", engine_ ? engine_->canUndo() : false);
    drawBtn(redoButtonRect_, @"↻ Redo", engine_ ? engine_->canRedo() : false);

    // MIDI Learn Button (Blinking amber LED when active)
    const bool isLearning = engine_ && (engine_->getLearningParamId() >= 0);
    {
        CGContextSaveGState(ctx);
        if (isLearning) {
            CGContextSetRGBFillColor(ctx, 0.45f, 0.30f, 0.05f, 0.95f);
            CGContextSetRGBStrokeColor(ctx, 1.0f, 0.82f, 0.15f, 1.0f);
            CGContextSetShadowWithColor(ctx, CGSizeMake(0, 0), 8.0, [[NSColor colorWithCalibratedRed:1.0 green:0.8 blue:0.1 alpha:0.6] CGColor]);
        } else {
            CGContextSetRGBFillColor(ctx, 0.11f, 0.12f, 0.15f, 0.9f);
            CGContextSetRGBStrokeColor(ctx, 0.25f, 0.28f, 0.35f, 0.8f);
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

    // Preset Prev / Display / Next / Browse
    drawBtn(presetPrevRect_, @"<", true);
    {
        CGContextSaveGState(ctx);
        CGContextSetRGBFillColor(ctx, 0.07f, 0.08f, 0.10f, 0.95f);
        CGContextSetRGBStrokeColor(ctx, 0.26f, 0.30f, 0.38f, 0.8f);
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
            NSFontAttributeName: [NSFont fontWithName:@"HelveticaNeue-Medium" size:10.0] ?: [NSFont systemFontOfSize:10.0],
            NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.95 green:0.88 blue:0.75 alpha:1.0]
        };
        const NSSize sz = [pName sizeWithAttributes:pAttrs];
        [pName drawAtPoint:NSMakePoint(presetBoxRect_.origin.x + 6.0, presetBoxRect_.origin.y + (presetBoxRect_.size.height - sz.height) * 0.5) withAttributes:pAttrs];
        CGContextRestoreGState(ctx);
    }
    drawBtn(presetNextRect_, @">", true);
    drawBtn(presetBrowseButtonRect_, @"☰", true);

    CGContextRestoreGState(ctx);
}

// =============================================================================
// 2. LABORATORY & PHOSPHOR CRT SECTION
// =============================================================================
- (void)drawLaboratorySection:(CGContextRef)ctx {
    const NSRect labRect = NSMakeRect(18.0, 432.0, 964.0, 184.0);

    // Bezel Frame (Brushed Gunmetal)
    CGContextSaveGState(ctx);
    CGContextSetRGBFillColor(ctx, 0.06f, 0.07f, 0.09f, 0.95f);
    CGContextSetRGBStrokeColor(ctx, 0.20f, 0.22f, 0.28f, 0.85f);
    CGContextSetLineWidth(ctx, 1.2f);
    CGPathRef rPath = CGPathCreateWithRoundedRect(NSRectToCGRect(labRect), 6.0, 6.0, nullptr);
    CGContextAddPath(ctx, rPath);
    CGContextDrawPath(ctx, kCGPathFillStroke);
    CGPathRelease(rPath);

    // Left Subpanel: Physical Port-Hamiltonian Cantilever Transducer Lab (x: 26 .. 484)
    const NSRect physRect = NSMakeRect(26.0, 440.0, 460.0, 168.0);
    CGContextSetRGBFillColor(ctx, 0.03f, 0.04f, 0.05f, 0.92f);
    CGContextSetRGBStrokeColor(ctx, 0.15f, 0.17f, 0.22f, 0.8f);
    CGPathRef phPath = CGPathCreateWithRoundedRect(NSRectToCGRect(physRect), 4.0, 4.0, nullptr);
    CGContextAddPath(ctx, phPath);
    CGContextDrawPath(ctx, kCGPathFillStroke);
    CGPathRelease(phPath);

    NSDictionary* labTitleAttrs = @{
        NSFontAttributeName: [NSFont fontWithName:@"HelveticaNeue-Bold" size:9.5] ?: [NSFont boldSystemFontOfSize:9.5],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.40f green:0.75f blue:0.95f alpha:0.95f]
    };
    [@"PORT-HAMILTONIAN PHYSICAL TRANSDUCER (H <-> B <-> P)" drawAtPoint:NSMakePoint(36.0, 592.0) withAttributes:labTitleAttrs];

    phs::ScopeSnapshot scope{};
    if (engine_) {
        scope = engine_->getScopeSnapshot();
    }

    // Live Conservation Telemetry
    NSDictionary* telAttrs = @{
        NSFontAttributeName: [NSFont fontWithName:@"Courier-Bold" size:9.0] ?: [NSFont monospacedSystemFontOfSize:9.0 weight:NSFontWeightBold],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.30f green:0.90f blue:0.50f alpha:1.0f]
    };
    NSString* hStr = [NSString stringWithFormat:@"H(t): %.2e J | Pdiss: %.2e W | Polyphony: %d | PASSIVITY: UNCONDITIONAL",
                      scope.totalHamiltonian, scope.dissipatedPower, scope.activeVoices];
    [hStr drawAtPoint:NSMakePoint(36.0, 576.0) withAttributes:telAttrs];

    // Animated Cantilever Tine, Rebound Hammer, and Pickup Transducer
    const CGFloat baseY = 506.0;
    const CGFloat clampX = 54.0;
    const CGFloat tipX = 390.0;
    const CGFloat tipExcursion = std::clamp(scope.tipDisplacement * 20.0f, -28.0f, 28.0f);
    const CGFloat hammerComp = std::clamp(scope.hammerCompression * 30.0f, 0.0f, 22.0f);

    // Clamped Root Block
    CGContextSetRGBFillColor(ctx, 0.35f, 0.38f, 0.44f, 0.95f);
    CGContextFillRect(ctx, CGRectMake(clampX - 12.0, baseY - 24.0, 12.0, 48.0));

    // Vibrating Cantilever Tine (Cubic Bezier curve flexure)
    CGContextSetRGBStrokeColor(ctx, 0.92f, 0.88f, 0.74f, 1.0f);
    CGContextSetLineWidth(ctx, 3.2f);
    CGContextBeginPath(ctx);
    CGContextMoveToPoint(ctx, clampX, baseY);
    CGContextAddCurveToPoint(ctx, clampX + 130.0, baseY,
                                  clampX + 240.0, baseY + tipExcursion * 0.7f,
                                  tipX, baseY + tipExcursion);
    CGContextStrokePath(ctx);

    // Felt Hammer Shank & Head
    const CGFloat hammerStrikeX = clampX + 76.0;
    const CGFloat hammerY = baseY - 26.0 + hammerComp;
    CGContextSetRGBStrokeColor(ctx, 0.65f, 0.50f, 0.35f, 0.9f);
    CGContextSetLineWidth(ctx, 2.0f);
    CGContextStrokeLineSegments(ctx, (CGPoint[]){CGPointMake(hammerStrikeX, hammerY - 24.0), CGPointMake(hammerStrikeX, hammerY)}, 2);

    CGContextSetRGBFillColor(ctx, 0.25f, 0.65f, 0.88f, 0.95f);
    CGContextFillEllipseInRect(ctx, CGRectMake(hammerStrikeX - 8.0, hammerY, 16.0, 16.0));

    // Magnetic / Electrostatic Pickup Pole Piece
    const CGFloat pickupX = tipX + 16.0;
    CGContextSetRGBFillColor(ctx, 0.88f, 0.38f, 0.25f, 0.95f);
    CGContextFillRect(ctx, CGRectMake(pickupX, baseY - 10.0, 14.0, 20.0));

    // Glowing Flux Linkage Lines
    CGContextSetRGBStrokeColor(ctx, 0.35f, 0.85f, 0.95f, 0.65f);
    CGContextSetLineWidth(ctx, 1.0f);
    for (int k = -2; k <= 2; ++k) {
        CGContextBeginPath(ctx);
        CGContextMoveToPoint(ctx, tipX, baseY + tipExcursion + k * 3.0f);
        CGContextAddQuadCurveToPoint(ctx, (tipX + pickupX) * 0.5f, baseY + k * 6.0f, pickupX, baseY + k * 4.0f);
        CGContextStrokePath(ctx);
    }

    // Right Subpanel: Phosphor CRT Stereo Oscilloscope & 4-Band Harmonics (x: 498 .. 974)
    const NSRect scopeRect = NSMakeRect(498.0, 440.0, 476.0, 168.0);
    CGContextSetRGBFillColor(ctx, 0.02f, 0.04f, 0.03f, 0.98f);
    CGContextSetRGBStrokeColor(ctx, 0.08f, 0.30f, 0.18f, 0.8f);
    CGPathRef scPath = CGPathCreateWithRoundedRect(NSRectToCGRect(scopeRect), 4.0, 4.0, nullptr);
    CGContextAddPath(ctx, scPath);
    CGContextDrawPath(ctx, kCGPathFillStroke);
    CGPathRelease(scPath);

    // CRT Graticule Grid
    CGContextSetRGBStrokeColor(ctx, 0.06f, 0.20f, 0.12f, 0.45f);
    CGContextSetLineWidth(ctx, 0.8f);
    for (int gx = 1; gx < 8; ++gx) {
        const CGFloat x = scopeRect.origin.x + gx * (scopeRect.size.width / 8.0);
        CGContextStrokeLineSegments(ctx, (CGPoint[]){CGPointMake(x, scopeRect.origin.y), CGPointMake(x, scopeRect.origin.y + scopeRect.size.height)}, 2);
    }
    for (int gy = 1; gy < 6; ++gy) {
        const CGFloat y = scopeRect.origin.y + gy * (scopeRect.size.height / 6.0);
        CGContextStrokeLineSegments(ctx, (CGPoint[]){CGPointMake(scopeRect.origin.x, y), CGPointMake(scopeRect.origin.x + scopeRect.size.width, y)}, 2);
    }

    NSDictionary* scopeTitleAttrs = @{
        NSFontAttributeName: [NSFont fontWithName:@"HelveticaNeue-Bold" size:9.5] ?: [NSFont boldSystemFontOfSize:9.5],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.25f green:0.88f blue:0.55f alpha:0.95f]
    };
    [@"REAL-TIME PHOSPHOR CRT OSCILLOSCOPE" drawAtPoint:NSMakePoint(508.0, 592.0) withAttributes:scopeTitleAttrs];

    // Stereo Oscilloscope Waveform Trace
    CGContextSetRGBStrokeColor(ctx, 0.30f, 0.98f, 0.65f, 0.9f);
    CGContextSetLineWidth(ctx, 1.4f);
    CGContextBeginPath(ctx);
    const CGFloat midY = scopeRect.origin.y + scopeRect.size.height * 0.5f;
    const CGFloat maxWaveHeight = scopeRect.size.height * 0.42f;
    const uint32_t wPos = scope.writePos;
    for (int i = 0; i < phs::kScopeRingSize; ++i) {
        const uint32_t idx = (wPos + i) % phs::kScopeRingSize;
        const CGFloat x = scopeRect.origin.x + 6.0 + (i / static_cast<CGFloat>(phs::kScopeRingSize - 1)) * (scopeRect.size.width - 130.0);
        const CGFloat y = midY + std::clamp(scope.waveL[idx], -1.0f, 1.0f) * maxWaveHeight;
        if (i == 0) CGContextMoveToPoint(ctx, x, y);
        else CGContextAddLineToPoint(ctx, x, y);
    }
    CGContextStrokePath(ctx);

    // 4-Band Harmonic Bar Graph (f0, 2f0, 3f0, 6.27f0 chime)
    const CGFloat barBaseX = scopeRect.origin.x + scopeRect.size.width - 110.0;
    const CGFloat barBaseY = scopeRect.origin.y + 14.0;
    static const char* kHarmLabels[4] = {"f0", "2f0", "3f0", "6.3f0"};
    for (int h = 0; h < 4; ++h) {
        const CGFloat bx = barBaseX + h * 26.0;
        float hLevel = 0.05f;
        if (scope.activeVoices > 0) {
            const float eNorm = std::min(1.0f, std::abs(scope.tipVelocity) * (1.0f + h * 0.3f));
            hLevel = (h == 0) ? eNorm * 0.82f : (h == 1) ? eNorm * 0.50f : (h == 2) ? eNorm * 0.32f : eNorm * 0.25f;
        }
        const CGFloat bh = std::max(4.0f, hLevel * 48.0f);
        CGContextSetRGBFillColor(ctx, 0.20f, 0.85f, 0.60f, 0.85f);
        CGContextFillRect(ctx, CGRectMake(bx, barBaseY, 18.0, bh));

        NSDictionary* bAttrs = @{
            NSFontAttributeName: [NSFont systemFontOfSize:8.0 weight:NSFontWeightMedium],
            NSForegroundColorAttributeName: [NSColor colorWithCalibratedWhite:0.75 alpha:1.0]
        };
        NSString* lbl = [NSString stringWithUTF8String:kHarmLabels[h]];
        [lbl drawAtPoint:NSMakePoint(bx + 1.0, barBaseY + bh + 3.0) withAttributes:bAttrs];
    }

    CGContextRestoreGState(ctx);
}

// =============================================================================
// 3. HARDWARE CONTROL PANELS & 3D METALLIC FLUTED KNOBS
// =============================================================================
- (void)drawControlPanels:(CGContextRef)ctx {
    const NSRect panelRects[3] = {
        NSMakeRect(18.0, 114.0, 314.0, 308.0),
        NSMakeRect(340.0, 114.0, 326.0, 308.0),
        NSMakeRect(674.0, 114.0, 308.0, 308.0)
    };
    static const char* kPanelTitles[3] = {
        "MECHANICS & TINE RESONATOR",
        "NONLINEAR TRANSDUCER & EQ",
        "STAGE MODULATION & REVERB"
    };

    for (int p = 0; p < 3; ++p) {
        CGContextSaveGState(ctx);
        // Beveled plate
        CGContextSetRGBFillColor(ctx, 0.07f, 0.08f, 0.10f, 0.92f);
        CGContextSetRGBStrokeColor(ctx, 0.20f, 0.22f, 0.28f, 0.8f);
        CGContextSetLineWidth(ctx, 1.0f);
        CGPathRef pPath = CGPathCreateWithRoundedRect(NSRectToCGRect(panelRects[p]), 6.0, 6.0, nullptr);
        CGContextAddPath(ctx, pPath);
        CGContextDrawPath(ctx, kCGPathFillStroke);
        CGPathRelease(pPath);

        // Header plate banner
        NSRect hRect = NSMakeRect(panelRects[p].origin.x + 8.0, panelRects[p].origin.y + panelRects[p].size.height - 24.0,
                                  panelRects[p].size.width - 16.0, 18.0);
        CGContextSetRGBFillColor(ctx, 0.11f, 0.13f, 0.16f, 0.9f);
        CGPathRef hp = CGPathCreateWithRoundedRect(NSRectToCGRect(hRect), 3.0, 3.0, nullptr);
        CGContextAddPath(ctx, hp);
        CGContextFillPath(ctx);
        CGPathRelease(hp);

        NSDictionary* tAttrs = @{
            NSFontAttributeName: [NSFont fontWithName:@"HelveticaNeue-Bold" size:9.5] ?: [NSFont boldSystemFontOfSize:9.5],
            NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.88f green:0.90f blue:0.96f alpha:0.95f]
        };
        NSString* title = [NSString stringWithUTF8String:kPanelTitles[p]];
        const NSSize sz = [title sizeWithAttributes:tAttrs];
        [title drawAtPoint:NSMakePoint(hRect.origin.x + (hRect.size.width - sz.width) * 0.5, hRect.origin.y + (hRect.size.height - sz.height) * 0.5) withAttributes:tAttrs];
        CGContextRestoreGState(ctx);
    }

    // Draw All Knobs
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
    const CGFloat radius = std::min(rect.size.width, rect.size.height) * 0.30;

    CGContextSaveGState(ctx);

    // Circular LED Background Track (270 degrees sweep: -135 to +135 deg)
    const CGFloat startAngle = -M_PI * 1.25;
    const CGFloat endAngle   = M_PI * 0.25;
    const CGFloat curAngle   = startAngle + norm * (endAngle - startAngle);

    CGContextSetRGBStrokeColor(ctx, 0.14f, 0.16f, 0.20f, 0.85f);
    CGContextSetLineWidth(ctx, 3.2f);
    CGContextSetLineCap(ctx, kCGLineCapRound);
    CGContextBeginPath(ctx);
    CGContextAddArc(ctx, cx, cy, radius + 5.5, startAngle, endAngle, 0);
    CGContextStrokePath(ctx);

    // Glowing LED Value Arc
    if (isArmed) {
        CGContextSetRGBStrokeColor(ctx, 1.0f, 0.82f, 0.15f, 1.0f);
        CGContextSetShadowWithColor(ctx, CGSizeMake(0, 0), 6.0, [[NSColor colorWithCalibratedRed:1.0 green:0.8 blue:0.1 alpha:0.6] CGColor]);
    } else {
        CGContextSetRGBStrokeColor(ctx, 0.35f, 0.78f, 0.98f, 0.95f);
        CGContextSetShadowWithColor(ctx, CGSizeMake(0, 0), 4.0, [[NSColor colorWithCalibratedRed:0.3 green:0.75 blue:0.95 alpha:0.4] CGColor]);
    }
    CGContextSetLineWidth(ctx, 3.2f);
    CGContextBeginPath(ctx);
    CGContextAddArc(ctx, cx, cy, radius + 5.5, startAngle, curAngle, 0);
    CGContextStrokePath(ctx);

    CGContextSetShadowWithColor(ctx, CGSizeZero, 0, nullptr); // clear shadow

    // 3D Brushed Metal Cap
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    const CGFloat knobGradColors[8] = {
        0.30f, 0.32f, 0.38f, 1.0f,
        0.13f, 0.14f, 0.17f, 1.0f
    };
    CGGradientRef kGrad = CGGradientCreateWithColorComponents(cs, knobGradColors, nullptr, 2);
    CGContextAddArc(ctx, cx, cy, radius, 0, 2.0 * M_PI, 0);
    CGContextClip(ctx);
    CGContextDrawLinearGradient(ctx, kGrad, CGPointMake(cx, cy + radius), CGPointMake(cx, cy - radius), 0);
    CGGradientRelease(kGrad);
    CGColorSpaceRelease(cs);

    // Bezel Rim
    CGContextResetClip(ctx);
    CGContextSetRGBStrokeColor(ctx, 0.40f, 0.43f, 0.50f, 0.9f);
    CGContextSetLineWidth(ctx, 1.2f);
    CGContextAddArc(ctx, cx, cy, radius, 0, 2.0 * M_PI, 0);
    CGContextStrokePath(ctx);

    // Indicator Line Notch
    CGContextSetRGBStrokeColor(ctx, isArmed ? 1.0f : 0.95f, isArmed ? 0.85f : 0.95f, isArmed ? 0.2f : 0.95f, 1.0f);
    CGContextSetLineWidth(ctx, 2.2f);
    CGContextBeginPath(ctx);
    const CGFloat indLen1 = radius * 0.30f;
    const CGFloat indLen2 = radius * 0.92f;
    CGContextMoveToPoint(ctx, cx + indLen1 * std::cos(curAngle), cy + indLen1 * std::sin(curAngle));
    CGContextAddLineToPoint(ctx, cx + indLen2 * std::cos(curAngle), cy + indLen2 * std::sin(curAngle));
    CGContextStrokePath(ctx);

    // Parameter Name (Above)
    NSDictionary* titleAttrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:8.0 weight:NSFontWeightMedium],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedWhite:0.75 alpha:1.0]
    };
    const NSSize tSz = [title sizeWithAttributes:titleAttrs];
    [title drawAtPoint:NSMakePoint(cx - tSz.width * 0.5, rect.origin.y + rect.size.height - 11.0) withAttributes:titleAttrs];

    // Value Readout (Below)
    NSString* valStr = formatParamValue(paramId, norm);
    NSDictionary* valAttrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:8.0 weight:NSFontWeightBold],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.40 green:0.82 blue:0.98 alpha:1.0]
    };
    const NSSize vSz = [valStr sizeWithAttributes:valAttrs];
    [valStr drawAtPoint:NSMakePoint(cx - vSz.width * 0.5, rect.origin.y - 3.0) withAttributes:valAttrs];

    // MIDI CC Badge
    if (isArmed) {
        NSRect bRect = NSMakeRect(cx - 18.0, cy - 7.0, 36.0, 14.0);
        CGContextSetRGBFillColor(ctx, 0.92f, 0.70f, 0.05f, 0.95f);
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
// 4. PERFORMANCE SECTION & 61-KEY PLAYABLE KEYBOARD
// =============================================================================
- (void)drawPerformanceSection:(CGContextRef)ctx {
    // Sustain Pedal Switch
    const bool isPedalDown = engine_ && engine_->isNoteActive(60); // sustain pedal state
    CGContextSaveGState(ctx);
    if (isPedalDown) {
        CGContextSetRGBFillColor(ctx, 0.85f, 0.55f, 0.15f, 0.95f);
        CGContextSetRGBStrokeColor(ctx, 1.0f, 0.85f, 0.35f, 1.0f);
    } else {
        CGContextSetRGBFillColor(ctx, 0.13f, 0.15f, 0.18f, 0.90f);
        CGContextSetRGBStrokeColor(ctx, 0.28f, 0.32f, 0.40f, 0.75f);
    }
    CGPathRef pedP = CGPathCreateWithRoundedRect(NSRectToCGRect(pedalSwitchRect_), 4.0, 4.0, nullptr);
    CGContextAddPath(ctx, pedP);
    CGContextDrawPath(ctx, kCGPathFillStroke);
    CGPathRelease(pedP);

    NSDictionary* pAttrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:8.5 weight:NSFontWeightBold],
        NSForegroundColorAttributeName: isPedalDown ? [NSColor blackColor] : [NSColor whiteColor]
    };
    NSString* pedTxt = isPedalDown ? @"SUSTAIN\n[ON]" : @"SUSTAIN\n[OFF]";
    [pedTxt drawInRect:pedalSwitchRect_ withAttributes:pAttrs];
    CGContextRestoreGState(ctx);

    // Pitch & Mod Wheels
    auto drawWheel = [&](NSRect r, NSString* name, float posNorm) {
        CGContextSaveGState(ctx);
        CGContextSetRGBFillColor(ctx, 0.09f, 0.10f, 0.13f, 0.95f);
        CGContextSetRGBStrokeColor(ctx, 0.24f, 0.27f, 0.34f, 0.8f);
        CGPathRef wp = CGPathCreateWithRoundedRect(NSRectToCGRect(r), 4.0, 4.0, nullptr);
        CGContextAddPath(ctx, wp);
        CGContextDrawPath(ctx, kCGPathFillStroke);
        CGPathRelease(wp);

        const CGFloat thumbY = r.origin.y + 4.0 + posNorm * (r.size.height - 24.0);
        NSRect tRect = NSMakeRect(r.origin.x + 2.0, thumbY, r.size.width - 4.0, 16.0);
        CGContextSetRGBFillColor(ctx, 0.32f, 0.36f, 0.44f, 0.95f);
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

    // Draw White Keys
    int whiteIdx = 0;
    for (int n = startNote; n < startNote + numKeys; ++n) {
        if (isBlackKey(n)) continue;
        const CGFloat kx = keyboardRect_.origin.x + whiteIdx * whiteKeyWidth;
        const NSRect wkRect = NSMakeRect(kx, keyboardRect_.origin.y, whiteKeyWidth - 1.0, whiteKeyHeight);
        const bool active = engine_ && (engine_->isNoteActive(n) || mouseHeldMidiNote_ == n);

        CGContextSaveGState(ctx);
        if (active) {
            CGContextSetRGBFillColor(ctx, 0.85f, 0.93f, 1.0f, 1.0f);
        } else {
            CGContextSetRGBFillColor(ctx, 0.96f, 0.95f, 0.93f, 1.0f);
        }
        CGContextSetRGBStrokeColor(ctx, 0.32f, 0.34f, 0.38f, 0.7f);
        CGContextSetLineWidth(ctx, 0.8f);
        CGContextFillRect(ctx, NSRectToCGRect(wkRect));
        CGContextStrokeRect(ctx, NSRectToCGRect(wkRect));

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

    // Draw Black Keys
    whiteIdx = 0;
    for (int n = startNote; n < startNote + numKeys; ++n) {
        if (!isBlackKey(n)) {
            ++whiteIdx;
            continue;
        }
        const CGFloat kx = keyboardRect_.origin.x + whiteIdx * whiteKeyWidth - (blackKeyWidth * 0.5f);
        const NSRect bkRect = NSMakeRect(kx, keyboardRect_.origin.y + (whiteKeyHeight - blackKeyHeight), blackKeyWidth, blackKeyHeight);
        const bool active = engine_ && (engine_->isNoteActive(n) || mouseHeldMidiNote_ == n);

        CGContextSaveGState(ctx);
        if (active) {
            CGContextSetRGBFillColor(ctx, 0.30f, 0.70f, 0.98f, 1.0f);
        } else {
            CGContextSetRGBFillColor(ctx, 0.11f, 0.12f, 0.15f, 1.0f);
        }
        CGContextSetRGBStrokeColor(ctx, 0.04f, 0.04f, 0.06f, 0.9f);
        CGContextSetLineWidth(ctx, 0.8f);
        CGContextFillRect(ctx, NSRectToCGRect(bkRect));
        CGContextStrokeRect(ctx, NSRectToCGRect(bkRect));
        CGContextRestoreGState(ctx);
    }
}

// =============================================================================
// 5. PRESET BROWSER MODAL OVERLAY
// =============================================================================
- (void)drawPresetBrowserModal:(CGContextRef)ctx {
    CGContextSaveGState(ctx);
    CGContextSetRGBFillColor(ctx, 0.0f, 0.0f, 0.0f, 0.72f);
    CGContextFillRect(ctx, CGRectMake(0, 0, 1000.0, 680.0));

    CGContextSetRGBFillColor(ctx, 0.09f, 0.10f, 0.14f, 0.98f);
    CGContextSetRGBStrokeColor(ctx, 0.32f, 0.38f, 0.48f, 0.9f);
    CGContextSetLineWidth(ctx, 1.5f);
    CGPathRef mPath = CGPathCreateWithRoundedRect(NSRectToCGRect(browserModalRect_), 8.0, 8.0, nullptr);
    CGContextAddPath(ctx, mPath);
    CGContextDrawPath(ctx, kCGPathFillStroke);
    CGPathRelease(mPath);

    NSDictionary* mTitleAttrs = @{
        NSFontAttributeName: [NSFont fontWithName:@"HelveticaNeue-Bold" size:14.0] ?: [NSFont boldSystemFontOfSize:14.0],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.95 green:0.82 blue:0.42 alpha:1.0]
    };
    [@"PRESET BROWSER & SOUND BANK" drawAtPoint:NSMakePoint(browserModalRect_.origin.x + 20.0, browserModalRect_.origin.y + browserModalRect_.size.height - 32.0)
                                 withAttributes:mTitleAttrs];

    NSDictionary* closeAttrs = @{
        NSFontAttributeName: [NSFont boldSystemFontOfSize:13.0],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedWhite:0.8 alpha:1.0]
    };
    [@"✕" drawAtPoint:NSMakePoint(browserCloseRect_.origin.x + 6.0, browserCloseRect_.origin.y + 4.0) withAttributes:closeAttrs];

    // Category Tabs
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
        for (int pIdx = 0; pIdx < static_cast<int>(allPresets.size()); ++pIdx) {
            const auto& p = allPresets[pIdx];
            if (selectedCategory_ != phs::PresetCategory::All && p.category != selectedCategory_) continue;
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
        }

        // Details Panel
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

        // Action Buttons
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
// MOUSE INTERACTION
// =============================================================================
- (void)mouseDown:(NSEvent*)event {
    if (!engine_) return;
    const NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    const NSUInteger mods = [event modifierFlags];
    isFineDrag_ = (mods & NSEventModifierFlagShift) != 0;

    const uint64_t now = static_cast<uint64_t>([[NSDate date] timeIntervalSince1970] * 1000.0);
    const bool isDoubleClick = (now - lastClickTimestamp_ < 350);
    lastClickTimestamp_ = now;

    // 1. Preset Browser Modal Handling
    if (browserOpen_) {
        if (NSPointInRect(pt, browserCloseRect_) || !NSPointInRect(pt, browserModalRect_)) {
            browserOpen_ = false;
            [self setNeedsDisplay:YES];
            return;
        }

        for (int i = 0; i < 6; ++i) {
            if (NSPointInRect(pt, categoryTabRects_[i])) {
                selectedCategory_ = static_cast<phs::PresetCategory>(i);
                [self setNeedsDisplay:YES];
                return;
            }
        }

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

        if (NSPointInRect(pt, browserSaveRect_)) {
            [self openSavePresetDialog];
            return;
        }

        if (NSPointInRect(pt, browserDeleteRect_)) {
            engine_->getPresetManager().deleteUserPreset(selectedPresetIdx_);
            [self setNeedsDisplay:YES];
            return;
        }
        return;
    }

    // 2. Header Bar Controls
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

    if (NSPointInRect(pt, midiLearnButtonRect_)) {
        if (engine_->getLearningParamId() >= 0) {
            engine_->stopMidiLearn();
        } else {
            engine_->startMidiLearn(phs::kParamPickupDistance);
        }
        [self setNeedsDisplay:YES];
        return;
    }

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

    if (NSPointInRect(pt, pedalSwitchRect_)) {
        const bool active = engine_->isNoteActive(60);
        engine_->handleMidiEvent(0xB0, 64, active ? 0 : 127);
        [self setNeedsDisplay:YES];
        return;
    }

    // 3. Knobs
    for (size_t i = 0; i < knobs_.size(); ++i) {
        if (NSPointInRect(pt, knobs_[i].rect)) {
            const uint32_t pId = knobs_[i].paramId;

            if (engine_->getLearningParamId() >= 0) {
                engine_->startMidiLearn(pId);
                [self setNeedsDisplay:YES];
                return;
            }

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

    // 4. Playable Keyboard
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
