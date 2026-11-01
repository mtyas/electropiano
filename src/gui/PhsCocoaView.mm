#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#include "../dsp/PhsSynthEngine.hpp"
#include "../dsp/PhsParameters.hpp"
#include "PhsCocoaView.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <string>

#ifndef PHS_VIEW_CLASS_NAME
#define PHS_VIEW_CLASS_NAME PhsEditorView_CLAP
#endif

namespace {

enum class KnobStyle {
    Mechanics,   // Rhodes skirted bakelite with spun-aluminum cap
    Pickup,      // Precision knurled champagne/nickel metal cylinder
    Drive,       // Vintage chunky Tweed/Plexi gold pointer knob
    Modulation,  // Analog stompbox fluted rubberized knob with cyan cap
    Reverb       // Studio console aerospace billet aluminum with jewel pip
};

struct KnobRect {
    uint32_t paramId;
    NSRect rect;
    const char* line1;
    const char* line2;
    const char* sectionName;
    KnobStyle style;
};

// Formats parameter value for display
static NSString* formatParamValue(uint32_t id, float norm) {
    if (id >= phs::kNumParams) return @"";
    const auto& meta = phs::kParamMetadata[id];
    const float val = meta.minValue + norm * (meta.maxValue - meta.minValue);

    switch (id) {
        case phs::kParamModelType: {
            const int m = std::clamp(static_cast<int>(std::round(norm * 3.0f)), 0, 3);
            const char* names[] = {"Rhodes Mk I", "Rhodes Dyno", "Wurlitzer 200A", "PHS Hybrid"};
            return [NSString stringWithUTF8String:names[m]];
        }
        case phs::kParamHammerHardness:
        case phs::kParamHammerHysteresis:
        case phs::kParamTineBell:
        case phs::kParamToneBarCoupling:
        case phs::kParamKeyNoise:
        case phs::kParamPickupSymmetry:
        case phs::kParamPickupResonance:
        case phs::kParamPreampDrive:
        case phs::kParamTremoloDepth:
        case phs::kParamPhaserMix:
        case phs::kParamChorusMix:
        case phs::kParamReverbMix:
        case phs::kParamReverbDecay:
        case phs::kParamVelocityCurve:
            return [NSString stringWithFormat:@"%.0f%%", norm * 100.0f];

        case phs::kParamStrikePosition:
            return [NSString stringWithFormat:@"%.2f L", val];

        case phs::kParamTineDecay:
            return [NSString stringWithFormat:@"%.2f s", val];

        case phs::kParamPickupDistance:
            return [NSString stringWithFormat:@"%.2f mm", 0.5f + norm * 4.5f];

        case phs::kParamBassBoost:
        case phs::kParamTrebleBoost: {
            const float db = (norm - 0.5f) * 24.0f;
            return [NSString stringWithFormat:@"%+.1f dB", db];
        }

        case phs::kParamTremoloRate:
            return [NSString stringWithFormat:@"%.1f Hz", 0.5f + norm * 11.5f];

        case phs::kParamPhaserRate:
            return [NSString stringWithFormat:@"%.2f Hz", 0.1f + norm * 3.9f];

        case phs::kParamMasterVolume: {
            if (norm < 0.001f) return @"-inf dB";
            const float db = 20.0f * std::log10(norm);
            return [NSString stringWithFormat:@"%.1f dB", db];
        }

        default:
            return [NSString stringWithFormat:@"%.2f", val];
    }
}

} // namespace

static NSImage* gKnobMechanics = nil;
static NSImage* gKnobPickup = nil;
static NSImage* gKnobDrive = nil;
static NSImage* gKnobModulation = nil;
static NSImage* gKnobReverb = nil;
static NSImage* gSliderCap = nil;
static NSImage* gRockerActive = nil;
static NSImage* gRockerInactive = nil;

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

    // Deck Bounding Rectangles
    NSRect pianoDeckRect_;
    NSRect fxDeckRect_;

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

static void init3dAssets() {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

    auto findAsset = [](NSString* name) -> NSString* {
        NSString* p1 = [NSString stringWithFormat:@"resources/gui/%@.png", name];
        if ([[NSFileManager defaultManager] fileExistsAtPath:p1]) return p1;

        NSBundle* b = [NSBundle bundleForClass:[PHS_VIEW_CLASS_NAME class]];
        NSString* p2 = [b pathForResource:name ofType:@"png"];
        if (p2 && [[NSFileManager defaultManager] fileExistsAtPath:p2]) return p2;

        NSString* execPath = [[b executablePath] stringByDeletingLastPathComponent];
        NSString* p3 = [[execPath stringByAppendingPathComponent:@"../Resources"] stringByAppendingPathComponent:[NSString stringWithFormat:@"%@.png", name]];
        if ([[NSFileManager defaultManager] fileExistsAtPath:p3]) return p3;

        NSString* p4 = [NSString stringWithFormat:@"/Users/matthewtyas/Documents/antigravity/pianoelectro/resources/gui/%@.png", name];
        if ([[NSFileManager defaultManager] fileExistsAtPath:p4]) return p4;

        return nil;
    };

    NSString* mPath = findAsset(@"knob_mechanics_filmstrip");
    if (mPath) gKnobMechanics = [[NSImage alloc] initWithContentsOfFile:mPath];

    NSString* pkPath = findAsset(@"knob_pickup_filmstrip");
    if (pkPath) gKnobPickup = [[NSImage alloc] initWithContentsOfFile:pkPath];

    NSString* dPath = findAsset(@"knob_drive_filmstrip");
    if (dPath) gKnobDrive = [[NSImage alloc] initWithContentsOfFile:dPath];

    NSString* modPath = findAsset(@"knob_modulation_filmstrip");
    if (modPath) gKnobModulation = [[NSImage alloc] initWithContentsOfFile:modPath];

    NSString* revPath = findAsset(@"knob_reverb_filmstrip");
    if (revPath) gKnobReverb = [[NSImage alloc] initWithContentsOfFile:revPath];

    NSString* sPath = findAsset(@"slider_cap_3d");
    if (sPath) gSliderCap = [[NSImage alloc] initWithContentsOfFile:sPath];

    NSString* raPath = findAsset(@"rocker_active_3d");
    if (raPath) gRockerActive = [[NSImage alloc] initWithContentsOfFile:raPath];

    NSString* riPath = findAsset(@"rocker_inactive_3d");
    if (riPath) gRockerInactive = [[NSImage alloc] initWithContentsOfFile:riPath];
}

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

        init3dAssets();
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

    // 1. Top Header Bar (y: 624 .. 672)
    for (int i = 0; i < 4; ++i) {
        modelButtonRects_[i] = NSMakeRect(202.0 + i * 80.0, 632.0, 76.0, 28.0);
    }
    undoButtonRect_         = NSMakeRect(530.0, 632.0, 44.0, 28.0);
    redoButtonRect_         = NSMakeRect(578.0, 632.0, 44.0, 28.0);
    midiLearnButtonRect_    = NSMakeRect(628.0, 632.0, 64.0, 28.0);
    presetPrevRect_         = NSMakeRect(702.0, 632.0, 20.0, 28.0);
    presetBoxRect_          = NSMakeRect(724.0, 632.0, 168.0, 28.0);
    presetNextRect_         = NSMakeRect(894.0, 632.0, 20.0, 28.0);
    presetBrowseButtonRect_ = NSMakeRect(918.0, 632.0, 56.0, 28.0);

    // 2. UPPER DECK: PIANO CORE ENGINE (y: 368 .. 618)
    pianoDeckRect_ = NSMakeRect(24.0, 368.0, 952.0, 250.0);

    // Section 1: TINE & HAMMER MECHANICS (x: 32 .. 568, w: 536)
    // Row 1 (y: 494)
    knobs_.push_back({phs::kParamHammerHardness,   NSMakeRect(40.0,  494.0, 66.0, 88.0), "Hammer", "Hardness",  "MECHANICS", KnobStyle::Mechanics});
    knobs_.push_back({phs::kParamHammerHysteresis, NSMakeRect(174.0, 494.0, 66.0, 88.0), "Hammer", "Felt Loss",  "MECHANICS", KnobStyle::Mechanics});
    knobs_.push_back({phs::kParamStrikePosition,   NSMakeRect(308.0, 494.0, 66.0, 88.0), "Strike", "Position",   "MECHANICS", KnobStyle::Mechanics});
    knobs_.push_back({phs::kParamTineDecay,        NSMakeRect(442.0, 494.0, 66.0, 88.0), "Tine",   "Decay",      "MECHANICS", KnobStyle::Mechanics});

    // Row 2 (y: 386)
    knobs_.push_back({phs::kParamTineBell,         NSMakeRect(40.0,  386.0, 66.0, 88.0), "Bell",   "Modes",      "MECHANICS", KnobStyle::Mechanics});
    knobs_.push_back({phs::kParamToneBarCoupling,  NSMakeRect(174.0, 386.0, 66.0, 88.0), "ToneBar", "Coupling",  "MECHANICS", KnobStyle::Mechanics});
    knobs_.push_back({phs::kParamKeyNoise,         NSMakeRect(308.0, 386.0, 66.0, 88.0), "Key",    "Noise",     "MECHANICS", KnobStyle::Mechanics});
    knobs_.push_back({phs::kParamVelocityCurve,    NSMakeRect(442.0, 386.0, 66.0, 88.0), "Velocity", "Curve",    "MECHANICS", KnobStyle::Mechanics});

    // Section 2: TRANSDUCER PICKUP & TONE EQ (x: 580 .. 968, w: 388)
    // Row 1 (y: 494)
    knobs_.push_back({phs::kParamPickupDistance,   NSMakeRect(598.0, 494.0, 66.0, 88.0), "Pickup", "Air Gap",    "PICKUP", KnobStyle::Pickup});
    knobs_.push_back({phs::kParamPickupSymmetry,   NSMakeRect(726.0, 494.0, 66.0, 88.0), "Pickup", "Voicing",    "PICKUP", KnobStyle::Pickup});
    knobs_.push_back({phs::kParamPickupResonance,  NSMakeRect(854.0, 494.0, 66.0, 88.0), "Coil",   "Resonance",  "PICKUP", KnobStyle::Pickup});

    // Row 2 (y: 386)
    knobs_.push_back({phs::kParamBassBoost,        NSMakeRect(662.0, 386.0, 66.0, 88.0), "Bass",   "Tone",       "PICKUP", KnobStyle::Pickup});
    knobs_.push_back({phs::kParamTrebleBoost,      NSMakeRect(790.0, 386.0, 66.0, 88.0), "Treble", "Tone",       "PICKUP", KnobStyle::Pickup});

    // 3. LOWER DECK: VINTAGE EFFECTS RACK (y: 120 .. 356)
    fxDeckRect_ = NSMakeRect(24.0, 120.0, 952.0, 236.0);

    // Module 1: Preamp Overdrive (x: 32 .. 192, w: 160)
    knobs_.push_back({phs::kParamPreampDrive,      NSMakeRect(78.0,  166.0, 68.0, 92.0), "Preamp", "Drive",      "OVERDRIVE", KnobStyle::Drive});

    // Module 2: Suitcase Tremolo / Pan (x: 200 .. 384, w: 184)
    knobs_.push_back({phs::kParamTremoloRate,      NSMakeRect(216.0, 166.0, 66.0, 92.0), "Tremolo", "Rate",      "TREMOLO", KnobStyle::Modulation});
    knobs_.push_back({phs::kParamTremoloDepth,     NSMakeRect(300.0, 166.0, 66.0, 92.0), "Tremolo", "Depth",     "TREMOLO", KnobStyle::Modulation});

    // Module 3: Modulation (Phaser & Chorus) (x: 392 .. 668, w: 276)
    knobs_.push_back({phs::kParamPhaserRate,       NSMakeRect(408.0, 166.0, 66.0, 92.0), "Phaser",  "Rate",      "MODULATION", KnobStyle::Modulation});
    knobs_.push_back({phs::kParamPhaserMix,        NSMakeRect(496.0, 166.0, 66.0, 92.0), "Phaser",  "Mix",       "MODULATION", KnobStyle::Modulation});
    knobs_.push_back({phs::kParamChorusMix,        NSMakeRect(584.0, 166.0, 66.0, 92.0), "Stereo",  "Chorus",    "MODULATION", KnobStyle::Modulation});

    // Module 4: Studio Reverb (x: 676 .. 860, w: 184)
    knobs_.push_back({phs::kParamReverbDecay,      NSMakeRect(692.0, 166.0, 66.0, 92.0), "Reverb",  "Decay",     "REVERB", KnobStyle::Reverb});
    knobs_.push_back({phs::kParamReverbMix,        NSMakeRect(776.0, 166.0, 66.0, 92.0), "Reverb",  "Mix",       "REVERB", KnobStyle::Reverb});

    // Module 5: Master Output (x: 868 .. 968, w: 100)
    knobs_.push_back({phs::kParamMasterVolume,     NSMakeRect(884.0, 166.0, 68.0, 92.0), "Master",  "Volume",    "MASTER", KnobStyle::Reverb});

    // 4. Performance Section (y: 8 .. 108)
    pedalSwitchRect_ = NSMakeRect(18.0, 16.0, 58.0, 86.0);
    pitchBendRect_   = NSMakeRect(82.0, 16.0, 26.0, 86.0);
    modWheelRect_    = NSMakeRect(112.0, 16.0, 26.0, 86.0);
    keyboardRect_    = NSMakeRect(146.0, 16.0, 836.0, 86.0);
}

// =============================================================================
// MAIN DRAW RECT
// =============================================================================
- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    CGContextRef ctx = (CGContextRef)[[NSGraphicsContext currentContext] CGContext];
    if (!ctx) return;

    CGContextSaveGState(ctx);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();

    // 1. Walnut Wood End-Cheeks (Left: 0..16, Right: 984..1000)
    auto drawWoodCheek = [&](CGFloat x, CGFloat w) {
        const CGFloat woodColors[16] = {
            0.18f, 0.09f, 0.04f, 1.0f,
            0.32f, 0.16f, 0.08f, 1.0f,
            0.22f, 0.11f, 0.05f, 1.0f,
            0.12f, 0.06f, 0.03f, 1.0f
        };
        const CGFloat woodLocs[4] = {0.0f, 0.35f, 0.70f, 1.0f};
        CGGradientRef wGrad = CGGradientCreateWithColorComponents(cs, woodColors, woodLocs, 4);
        CGContextDrawLinearGradient(ctx, wGrad, CGPointMake(x, 0), CGPointMake(x + w, 0), 0);
        CGGradientRelease(wGrad);

        CGContextSetRGBStrokeColor(ctx, 0.40f, 0.22f, 0.11f, 0.35f);
        CGContextSetLineWidth(ctx, 0.8f);
        for (CGFloat gy = 8.0; gy < 670.0; gy += 14.0) {
            CGContextStrokeLineSegments(ctx, (CGPoint[]){CGPointMake(x + 2.0, gy), CGPointMake(x + w - 2.0, gy + 3.0)}, 2);
        }
    };
    drawWoodCheek(0.0, 16.0);
    drawWoodCheek(984.0, 16.0);

    // 2. Main Chassis: Vintage Tolex Textured Body (x: 16 .. 984)
    const CGFloat tolexColors[8] = {
        0.14f, 0.15f, 0.17f, 1.0f,
        0.06f, 0.07f, 0.08f, 1.0f
    };
    CGGradientRef tolexGrad = CGGradientCreateWithColorComponents(cs, tolexColors, nullptr, 2);
    CGContextDrawLinearGradient(ctx, tolexGrad, CGPointMake(0, 680), CGPointMake(0, 0), 0);
    CGGradientRelease(tolexGrad);

    // Outer border trim
    CGContextSetRGBStrokeColor(ctx, 0.25f, 0.27f, 0.32f, 0.8f);
    CGContextSetLineWidth(ctx, 1.0f);
    CGContextStrokeRect(ctx, CGRectMake(16.5, 0.5, 967.0, 679.0));

    // 3. Top Header Bar
    [self drawHeaderBar:ctx];

    // 4. UPPER DECK: PIANO CORE ENGINE (Physical Acoustics & Voicing)
    [self drawPianoDeck:ctx];

    // 5. LOWER DECK: VINTAGE EFFECTS RACK (Analog Stompbox & Studio Processing)
    [self drawEffectsDeck:ctx];

    // 6. Draw All Knobs with their distinct 3D pre-rendered filmstrips
    [self drawAllKnobs:ctx];

    // 7. Performance Section: Red Felt Rail, 61-Key Keyboard & Wheels
    [self drawPerformanceSection:ctx];

    // 8. Modal Preset Browser (if active)
    if (browserOpen_) {
        [self drawPresetBrowserModal:ctx];
    }

    CGColorSpaceRelease(cs);
    CGContextRestoreGState(ctx);
}

// =============================================================================
// 1. TOP HEADER BAR
// =============================================================================
- (void)drawHeaderBar:(CGContextRef)ctx {
    CGContextSaveGState(ctx);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();

    // Dark brushed aluminum header background
    const CGFloat hColors[8] = {
        0.18f, 0.19f, 0.22f, 1.0f,
        0.09f, 0.10f, 0.12f, 1.0f
    };
    CGGradientRef hGrad = CGGradientCreateWithColorComponents(cs, hColors, nullptr, 2);
    CGContextDrawLinearGradient(ctx, hGrad, CGPointMake(20, 674), CGPointMake(20, 624), 0);
    CGGradientRelease(hGrad);

    // Bottom silver separator rule
    CGContextSetRGBStrokeColor(ctx, 0.40f, 0.44f, 0.52f, 0.8f);
    CGContextSetLineWidth(ctx, 1.0f);
    CGContextStrokeLineSegments(ctx, (CGPoint[]){CGPointMake(20, 624.5), CGPointMake(980, 624.5)}, 2);

    // Left Logo / Branding
    NSDictionary* brandAttrs = @{
        NSFontAttributeName: [NSFont fontWithName:@"Optima-Bold" size:18.0] ?: [NSFont boldSystemFontOfSize:18.0],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.95 green:0.86 blue:0.62 alpha:1.0]
    };
    [@"PyPHS" drawAtPoint:NSMakePoint(26.0, 642.0) withAttributes:brandAttrs];

    NSDictionary* subBrandAttrs = @{
        NSFontAttributeName: [NSFont boldSystemFontOfSize:7.5],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedWhite:0.65 alpha:1.0]
    };
    [@"PORT-HAMILTONIAN PHYSICAL MODEL" drawAtPoint:NSMakePoint(26.0, 630.0) withAttributes:subBrandAttrs];

    // Model Selector Buttons (4 Tactile Rocker Switches)
    const int currentModel = engine_ ? std::clamp(static_cast<int>(std::round(engine_->getParamNormalized(phs::kParamModelType) * 3.0f)), 0, 3) : 0;
    const char* modelNames[4] = {"RHODES I", "DYNO MK", "WURLI 200", "PHS HYBRID"};

    for (int i = 0; i < 4; ++i) {
        const NSRect r = modelButtonRects_[i];
        const bool active = (i == currentModel);

        if (active && gRockerActive) {
            [gRockerActive drawInRect:r fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1.0];
        } else if (!active && gRockerInactive) {
            [gRockerInactive drawInRect:r fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1.0];
        } else {
            CGPathRef bp = CGPathCreateWithRoundedRect(NSRectToCGRect(r), 4.0, 4.0, nullptr);
            CGContextAddPath(ctx, bp);
            if (active) CGContextSetRGBFillColor(ctx, 0.16f, 0.32f, 0.46f, 1.0f);
            else CGContextSetRGBFillColor(ctx, 0.12f, 0.13f, 0.16f, 1.0f);
            CGContextFillPath(ctx);
            CGPathRelease(bp);
        }

        // Active Cyan Status LED
        if (active) {
            CGContextSetRGBFillColor(ctx, 0.25f, 0.90f, 1.0f, 1.0f);
            CGContextFillEllipseInRect(ctx, CGRectMake(r.origin.x + 5.0, r.origin.y + 11.5, 5.0, 5.0));
        }

        NSDictionary* bAttrs = @{
            NSFontAttributeName: [NSFont boldSystemFontOfSize:8.0],
            NSForegroundColorAttributeName: active ? [NSColor colorWithCalibratedWhite:1.0 alpha:1.0] : [NSColor colorWithCalibratedWhite:0.60 alpha:1.0]
        };
        NSString* name = [NSString stringWithUTF8String:modelNames[i]];
        const NSSize sz = [name sizeWithAttributes:bAttrs];
        [name drawAtPoint:NSMakePoint(r.origin.x + (r.size.width - sz.width) * 0.5 + (active ? 4.0 : 0.0), r.origin.y + (r.size.height - sz.height) * 0.5) withAttributes:bAttrs];
    }

    // Host Action Buttons: Undo / Redo / MIDI Learn
    auto drawFlatButton = [&](NSRect r, NSString* label, bool active, bool highlight) {
        CGPathRef p = CGPathCreateWithRoundedRect(NSRectToCGRect(r), 3.0, 3.0, nullptr);
        CGContextAddPath(ctx, p);
        if (highlight) CGContextSetRGBFillColor(ctx, 0.85f, 0.65f, 0.10f, 0.95f);
        else if (active) CGContextSetRGBFillColor(ctx, 0.20f, 0.23f, 0.28f, 1.0f);
        else CGContextSetRGBFillColor(ctx, 0.12f, 0.13f, 0.16f, 0.7f);
        CGContextFillPath(ctx);
        CGContextSetRGBStrokeColor(ctx, 0.35f, 0.38f, 0.45f, 0.7f);
        CGContextSetLineWidth(ctx, 1.0f);
        CGContextAddPath(ctx, p);
        CGContextStrokePath(ctx);
        CGPathRelease(p);

        NSDictionary* a = @{
            NSFontAttributeName: [NSFont boldSystemFontOfSize:8.0],
            NSForegroundColorAttributeName: highlight ? [NSColor blackColor] : [NSColor colorWithCalibratedWhite:0.85 alpha:1.0]
        };
        const NSSize sz = [label sizeWithAttributes:a];
        [label drawAtPoint:NSMakePoint(r.origin.x + (r.size.width - sz.width) * 0.5, r.origin.y + (r.size.height - sz.height) * 0.5) withAttributes:a];
    };

    const bool canUndo = engine_ ? engine_->canUndo() : false;
    const bool canRedo = engine_ ? engine_->canRedo() : false;
    const bool isLearning = engine_ ? (engine_->getLearningParamId() >= 0) : false;

    drawFlatButton(undoButtonRect_, @"UNDO", canUndo, false);
    drawFlatButton(redoButtonRect_, @"REDO", canRedo, false);
    drawFlatButton(midiLearnButtonRect_, isLearning ? @"LEARNING" : @"LEARN", true, isLearning);

    // Preset Stepper & VFD Display
    drawFlatButton(presetPrevRect_, @"<", true, false);
    drawFlatButton(presetNextRect_, @" >", true, false);
    drawFlatButton(presetBrowseButtonRect_, @"BROWSE", true, false);

    // VFD Preset Display Window
    CGPathRef vp = CGPathCreateWithRoundedRect(NSRectToCGRect(presetBoxRect_), 3.0, 3.0, nullptr);
    CGContextAddPath(ctx, vp);
    CGContextSetRGBFillColor(ctx, 0.03f, 0.05f, 0.08f, 1.0f);
    CGContextFillPath(ctx);
    CGContextSetRGBStrokeColor(ctx, 0.15f, 0.40f, 0.55f, 0.8f);
    CGContextSetLineWidth(ctx, 1.2f);
    CGContextAddPath(ctx, vp);
    CGContextStrokePath(ctx);
    CGPathRelease(vp);

    NSString* presetName = @"1973 Suitcase Rhodes Mk I";
    if (engine_) {
        const auto* curPreset = engine_->getPresetManager().getPreset(engine_->getCurrentPreset());
        if (curPreset) {
            presetName = [NSString stringWithUTF8String:curPreset->name.c_str()];
        }
    }
    NSDictionary* vfdAttrs = @{
        NSFontAttributeName: [NSFont fontWithName:@"Courier-Bold" size:10.0] ?: [NSFont boldSystemFontOfSize:10.0],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.25 green:0.92 blue:1.0 alpha:0.95]
    };
    const NSSize psz = [presetName sizeWithAttributes:vfdAttrs];
    [presetName drawAtPoint:NSMakePoint(presetBoxRect_.origin.x + (presetBoxRect_.size.width - psz.width) * 0.5, presetBoxRect_.origin.y + (presetBoxRect_.size.height - psz.height) * 0.5) withAttributes:vfdAttrs];

    CGColorSpaceRelease(cs);
    CGContextRestoreGState(ctx);
}

// =============================================================================
// 2. UPPER DECK: PIANO CORE ENGINE (Physical Acoustics & Voicing)
// =============================================================================
- (void)drawPianoDeck:(CGContextRef)ctx {
    CGContextSaveGState(ctx);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();

    // 1. Deck Outer Plate: Fine Brushed Satin Aluminum
    const CGFloat deckColors[8] = {
        0.20f, 0.22f, 0.25f, 1.0f,
        0.13f, 0.14f, 0.16f, 1.0f
    };
    CGGradientRef dGrad = CGGradientCreateWithColorComponents(cs, deckColors, nullptr, 2);
    CGPathRef dp = CGPathCreateWithRoundedRect(NSRectToCGRect(pianoDeckRect_), 6.0, 6.0, nullptr);
    CGContextSaveGState(ctx);
    CGContextAddPath(ctx, dp);
    CGContextClip(ctx);
    CGContextDrawLinearGradient(ctx, dGrad, CGPointMake(pianoDeckRect_.origin.x, NSMaxY(pianoDeckRect_)), CGPointMake(pianoDeckRect_.origin.x, pianoDeckRect_.origin.y), 0);
    CGGradientRelease(dGrad);

    // Fine horizontal brushing lines
    CGContextSetRGBStrokeColor(ctx, 1.0f, 1.0f, 1.0f, 0.035f);
    CGContextSetLineWidth(ctx, 0.6f);
    for (CGFloat y = pianoDeckRect_.origin.y + 2.0; y < NSMaxY(pianoDeckRect_); y += 3.0) {
        CGContextStrokeLineSegments(ctx, (CGPoint[]){CGPointMake(pianoDeckRect_.origin.x + 4.0, y), CGPointMake(NSMaxX(pianoDeckRect_) - 4.0, y)}, 2);
    }
    CGContextRestoreGState(ctx);

    // Deck Bezel & Rim
    CGContextSetRGBStrokeColor(ctx, 0.45f, 0.48f, 0.55f, 0.85f);
    CGContextSetLineWidth(ctx, 1.5f);
    CGContextAddPath(ctx, dp);
    CGContextStrokePath(ctx);
    CGPathRelease(dp);

    // Corner Allen Screws
    auto drawScrew = [&](CGFloat sx, CGFloat sy) {
        CGContextSetRGBFillColor(ctx, 0.30f, 0.32f, 0.36f, 1.0f);
        CGContextFillEllipseInRect(ctx, CGRectMake(sx - 3.5, sy - 3.5, 7.0, 7.0));
        CGContextSetRGBStrokeColor(ctx, 0.10f, 0.10f, 0.12f, 0.9f);
        CGContextSetLineWidth(ctx, 1.0f);
        CGContextStrokeLineSegments(ctx, (CGPoint[]){CGPointMake(sx - 2.0, sy), CGPointMake(sx + 2.0, sy)}, 2);
    };
    drawScrew(pianoDeckRect_.origin.x + 8.0, pianoDeckRect_.origin.y + 8.0);
    drawScrew(NSMaxX(pianoDeckRect_) - 8.0, pianoDeckRect_.origin.y + 8.0);
    drawScrew(pianoDeckRect_.origin.x + 8.0, NSMaxY(pianoDeckRect_) - 8.0);
    drawScrew(NSMaxX(pianoDeckRect_) - 8.0, NSMaxY(pianoDeckRect_) - 8.0);

    // 2. Deck Header Title Plate
    NSDictionary* dTitleAttrs = @{
        NSFontAttributeName: [NSFont fontWithName:@"Optima-Bold" size:11.0] ?: [NSFont boldSystemFontOfSize:11.0],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.92 green:0.86 blue:0.72 alpha:0.95]
    };
    [@"PIANO CORE  //  PHYSICAL ACOUSTIC & TRANSDUCER ENGINE" drawAtPoint:NSMakePoint(pianoDeckRect_.origin.x + 20.0, NSMaxY(pianoDeckRect_) - 18.0) withAttributes:dTitleAttrs];

    // 3. Sub-Panel 1: TINE & HAMMER MECHANICS (x: 32 .. 568, w: 536)
    const NSRect mechRect = NSMakeRect(32.0, 376.0, 536.0, 218.0);
    CGPathRef mp = CGPathCreateWithRoundedRect(NSRectToCGRect(mechRect), 5.0, 5.0, nullptr);
    CGContextSaveGState(ctx);
    CGContextAddPath(ctx, mp);
    CGContextSetRGBFillColor(ctx, 0.10f, 0.11f, 0.13f, 0.85f);
    CGContextFillPath(ctx);
    CGContextSetRGBStrokeColor(ctx, 0.32f, 0.35f, 0.42f, 0.75f);
    CGContextSetLineWidth(ctx, 1.0f);
    CGContextAddPath(ctx, mp);
    CGContextStrokePath(ctx);
    CGPathRelease(mp);
    CGContextRestoreGState(ctx);

    // Sub-Panel 1 Header
    NSDictionary* sp1Attrs = @{
        NSFontAttributeName: [NSFont boldSystemFontOfSize:9.0],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.85 green:0.88 blue:0.95 alpha:0.95]
    };
    [@"TINE & HAMMER MECHANICS" drawAtPoint:NSMakePoint(mechRect.origin.x + 12.0, NSMaxY(mechRect) - 16.0) withAttributes:sp1Attrs];

    NSDictionary* sp1Sub = @{
        NSFontAttributeName: [NSFont systemFontOfSize:7.5],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedWhite:0.55 alpha:0.9]
    };
    NSString* s1 = @"PORT-HAMILTONIAN CANTILEVER DYNAMICS";
    const NSSize sz1 = [s1 sizeWithAttributes:sp1Sub];
    [s1 drawAtPoint:NSMakePoint(NSMaxX(mechRect) - sz1.width - 12.0, NSMaxY(mechRect) - 15.0) withAttributes:sp1Sub];

    // 4. Sub-Panel 2: TRANSDUCER & TONE EQ (x: 580 .. 968, w: 388)
    const NSRect pickRect = NSMakeRect(580.0, 376.0, 388.0, 218.0);
    CGPathRef pp = CGPathCreateWithRoundedRect(NSRectToCGRect(pickRect), 5.0, 5.0, nullptr);
    CGContextSaveGState(ctx);
    CGContextAddPath(ctx, pp);
    CGContextSetRGBFillColor(ctx, 0.12f, 0.11f, 0.10f, 0.85f);
    CGContextFillPath(ctx);
    CGContextSetRGBStrokeColor(ctx, 0.48f, 0.42f, 0.32f, 0.75f);
    CGContextSetLineWidth(ctx, 1.0f);
    CGContextAddPath(ctx, pp);
    CGContextStrokePath(ctx);
    CGPathRelease(pp);
    CGContextRestoreGState(ctx);

    // Sub-Panel 2 Header
    NSDictionary* sp2Attrs = @{
        NSFontAttributeName: [NSFont boldSystemFontOfSize:9.0],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.95 green:0.86 blue:0.70 alpha:0.95]
    };
    [@"TRANSDUCER & TONE EQ" drawAtPoint:NSMakePoint(pickRect.origin.x + 12.0, NSMaxY(pickRect) - 16.0) withAttributes:sp2Attrs];

    NSDictionary* sp2Sub = @{
        NSFontAttributeName: [NSFont systemFontOfSize:7.5],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedWhite:0.55 alpha:0.9]
    };
    NSString* s2 = @"MAGNETIC FLUX & RLC VOICING";
    const NSSize sz2 = [s2 sizeWithAttributes:sp2Sub];
    [s2 drawAtPoint:NSMakePoint(NSMaxX(pickRect) - sz2.width - 12.0, NSMaxY(pickRect) - 15.0) withAttributes:sp2Sub];

    CGColorSpaceRelease(cs);
    CGContextRestoreGState(ctx);
}

// =============================================================================
// 3. LOWER DECK: VINTAGE EFFECTS RACK (Analog Stompbox & Studio Processing)
// =============================================================================
- (void)drawEffectsDeck:(CGContextRef)ctx {
    CGContextSaveGState(ctx);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();

    // 1. Deck Outer Chassis: Dark Matte Textured Steel
    const CGFloat fxColors[8] = {
        0.11f, 0.12f, 0.14f, 1.0f,
        0.05f, 0.06f, 0.07f, 1.0f
    };
    CGGradientRef fGrad = CGGradientCreateWithColorComponents(cs, fxColors, nullptr, 2);
    CGPathRef fp = CGPathCreateWithRoundedRect(NSRectToCGRect(fxDeckRect_), 6.0, 6.0, nullptr);
    CGContextSaveGState(ctx);
    CGContextAddPath(ctx, fp);
    CGContextClip(ctx);
    CGContextDrawLinearGradient(ctx, fGrad, CGPointMake(fxDeckRect_.origin.x, NSMaxY(fxDeckRect_)), CGPointMake(fxDeckRect_.origin.x, fxDeckRect_.origin.y), 0);
    CGGradientRelease(fGrad);
    CGContextRestoreGState(ctx);

    // Chassis Bezel
    CGContextSetRGBStrokeColor(ctx, 0.28f, 0.30f, 0.35f, 0.9f);
    CGContextSetLineWidth(ctx, 1.5f);
    CGContextAddPath(ctx, fp);
    CGContextStrokePath(ctx);
    CGPathRelease(fp);

    // Header Title Plate
    NSDictionary* fxTitleAttrs = @{
        NSFontAttributeName: [NSFont fontWithName:@"Optima-Bold" size:11.0] ?: [NSFont boldSystemFontOfSize:11.0],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.75 green:0.85 blue:0.95 alpha:0.95]
    };
    [@"VINTAGE EFFECTS RACK  //  BOUTIQUE ANALOG STOMPBOX & STUDIO PROCESSING" drawAtPoint:NSMakePoint(fxDeckRect_.origin.x + 20.0, NSMaxY(fxDeckRect_) - 18.0) withAttributes:fxTitleAttrs];

    // Helper: Draw FX Module Enclosure
    auto drawModuleBox = [&](NSRect r, const char* title, const char* sub, NSColor* accent, NSColor* ledColor) {
        CGPathRef bp = CGPathCreateWithRoundedRect(NSRectToCGRect(r), 4.0, 4.0, nullptr);
        CGContextSaveGState(ctx);
        CGContextAddPath(ctx, bp);
        CGContextSetRGBFillColor(ctx, 0.08f, 0.09f, 0.11f, 0.92f);
        CGContextFillPath(ctx);

        // Accent top border line
        CGContextSetStrokeColorWithColor(ctx, [accent CGColor]);
        CGContextSetLineWidth(ctx, 1.8f);
        CGContextStrokeLineSegments(ctx, (CGPoint[]){CGPointMake(r.origin.x + 4.0, NSMaxY(r) - 1.0), CGPointMake(NSMaxX(r) - 4.0, NSMaxY(r) - 1.0)}, 2);

        // Subtle outer border
        CGContextSetRGBStrokeColor(ctx, 0.24f, 0.26f, 0.30f, 0.75f);
        CGContextSetLineWidth(ctx, 1.0f);
        CGContextAddPath(ctx, bp);
        CGContextStrokePath(ctx);
        CGPathRelease(bp);
        CGContextRestoreGState(ctx);

        // Status Jewel LED
        if (ledColor) {
            CGContextSetFillColorWithColor(ctx, [ledColor CGColor]);
            CGContextFillEllipseInRect(ctx, CGRectMake(r.origin.x + 8.0, NSMaxY(r) - 16.0, 6.0, 6.0));
        }

        // Header Title
        NSDictionary* tAttrs = @{
            NSFontAttributeName: [NSFont boldSystemFontOfSize:8.5],
            NSForegroundColorAttributeName: accent
        };
        NSString* tStr = [NSString stringWithUTF8String:title];
        [tStr drawAtPoint:NSMakePoint(r.origin.x + 18.0, NSMaxY(r) - 18.0) withAttributes:tAttrs];

        // Subtitle
        NSDictionary* sAttrs = @{
            NSFontAttributeName: [NSFont systemFontOfSize:7.0],
            NSForegroundColorAttributeName: [NSColor colorWithCalibratedWhite:0.50 alpha:0.9]
        };
        NSString* sStr = [NSString stringWithUTF8String:sub];
        [sStr drawAtPoint:NSMakePoint(r.origin.x + 18.0, NSMaxY(r) - 27.0) withAttributes:sAttrs];
    };

    // 1. Preamp Overdrive
    drawModuleBox(NSMakeRect(32.0, 126.0, 160.0, 204.0), "OVERDRIVE", "TUBE / JFET",
                  [NSColor colorWithCalibratedRed:0.95 green:0.72 blue:0.25 alpha:1.0],
                  [NSColor colorWithCalibratedRed:1.00 green:0.60 blue:0.10 alpha:1.0]);

    // 2. Suitcase Tremolo
    drawModuleBox(NSMakeRect(200.0, 126.0, 184.0, 204.0), "TREMOLO", "SUITCASE PAN",
                  [NSColor colorWithCalibratedRed:0.25 green:0.85 blue:0.65 alpha:1.0],
                  [NSColor colorWithCalibratedRed:0.20 green:0.95 blue:0.60 alpha:1.0]);

    // 3. Analog Modulation (Phaser & Chorus)
    drawModuleBox(NSMakeRect(392.0, 126.0, 276.0, 204.0), "MODULATION", "PHASER & CHORUS",
                  [NSColor colorWithCalibratedRed:0.65 green:0.55 blue:0.95 alpha:1.0],
                  [NSColor colorWithCalibratedRed:0.70 green:0.50 blue:1.00 alpha:1.0]);

    // 4. Studio Reverb
    drawModuleBox(NSMakeRect(676.0, 126.0, 184.0, 204.0), "REVERB", "SPRING & PLATE",
                  [NSColor colorWithCalibratedRed:0.35 green:0.75 blue:1.00 alpha:1.0],
                  [NSColor colorWithCalibratedRed:0.25 green:0.70 blue:1.00 alpha:1.0]);

    // 5. Master Output
    drawModuleBox(NSMakeRect(868.0, 126.0, 100.0, 204.0), "MASTER", "MAIN OUT",
                  [NSColor colorWithCalibratedWhite:0.90 alpha:1.0],
                  [NSColor colorWithCalibratedRed:0.30 green:0.85 blue:0.40 alpha:1.0]);

    CGColorSpaceRelease(cs);
    CGContextRestoreGState(ctx);
}

// =============================================================================
// 4. DRAW ALL 3D KNOBS
// =============================================================================
- (void)drawAllKnobs:(CGContextRef)ctx {
    const int32_t learningParam = engine_ ? engine_->getLearningParamId() : -1;

    for (size_t i = 0; i < knobs_.size(); ++i) {
        const auto& k = knobs_[i];
        const float norm = engine_ ? engine_->getParamNormalized(k.paramId) : phs::kParamMetadata[k.paramId].defaultValue;
        const int8_t cc = engine_ ? engine_->getParamCc(k.paramId) : -1;
        const bool isArmed = (learningParam == static_cast<int32_t>(k.paramId));

        [self drawSingle3dKnob:ctx
                          rect:k.rect
                         param:k.paramId
                         line1:[NSString stringWithUTF8String:k.line1]
                         line2:[NSString stringWithUTF8String:k.line2]
                         value:norm
                        midiCc:cc
                       isArmed:isArmed
                         style:k.style];
    }
}

- (void)drawSingle3dKnob:(CGContextRef)ctx
                    rect:(NSRect)rect
                   param:(uint32_t)paramId
                   line1:(NSString*)line1
                   line2:(NSString*)line2
                   value:(float)norm
                  midiCc:(int8_t)midiCc
                 isArmed:(bool)isArmed
                   style:(KnobStyle)style {
    const CGFloat cx = rect.origin.x + rect.size.width * 0.5;
    const CGFloat cy = rect.origin.y + 40.0;
    const CGFloat radius = 20.0;

    CGContextSaveGState(ctx);

    // 1. Two-Line Centered Parameter Label (Above Knob, y: rect.origin.y + 64 .. 86)
    NSMutableParagraphStyle* pStyle = [[[NSMutableParagraphStyle alloc] init] autorelease];
    [pStyle setAlignment:NSTextAlignmentCenter];
    [pStyle setLineSpacing:1.0];
    NSDictionary* titleAttrs = @{
        NSFontAttributeName: [NSFont boldSystemFontOfSize:8.2],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedWhite:0.86 alpha:1.0],
        NSParagraphStyleAttributeName: pStyle
    };
    NSString* fullTitle = [NSString stringWithFormat:@"%@\n%@", line1, line2];
    NSRect labelRect = NSMakeRect(rect.origin.x - 4.0, rect.origin.y + 64.0, rect.size.width + 8.0, 22.0);
    [fullTitle drawInRect:labelRect withAttributes:titleAttrs];

    // 2. Glowing Halo Arc Indicator (270° sweep, from 225° to -45°)
    const CGFloat startAngle = 225.0f * (M_PI / 180.0f);
    const CGFloat endAngle   = -45.0f * (M_PI / 180.0f);
    const CGFloat curAngle   = startAngle - norm * (startAngle - endAngle);

    // Dark Track Arc
    CGContextSetRGBStrokeColor(ctx, 0.15f, 0.17f, 0.20f, 0.9f);
    CGContextSetLineWidth(ctx, 3.0f);
    CGContextAddArc(ctx, cx, cy, radius + 4.5, endAngle, startAngle, 0);
    CGContextStrokePath(ctx);

    // Section-specific glowing arc color
    if (isArmed) {
        CGContextSetRGBStrokeColor(ctx, 1.0f, 0.85f, 0.20f, 1.0f);
    } else {
        switch (style) {
            case KnobStyle::Mechanics:   CGContextSetRGBStrokeColor(ctx, 0.95f, 0.76f, 0.32f, 0.95f); break; // Warm amber gold
            case KnobStyle::Pickup:      CGContextSetRGBStrokeColor(ctx, 0.82f, 0.88f, 0.98f, 0.95f); break; // Champagne silver
            case KnobStyle::Drive:       CGContextSetRGBStrokeColor(ctx, 1.00f, 0.55f, 0.15f, 1.00f); break; // Fiery ember drive
            case KnobStyle::Modulation:  CGContextSetRGBStrokeColor(ctx, 0.25f, 0.90f, 0.95f, 0.95f); break; // Electric cyan
            case KnobStyle::Reverb:      CGContextSetRGBStrokeColor(ctx, 0.40f, 0.80f, 1.00f, 0.95f); break; // Sky blue ambience
        }
    }
    CGContextSetLineWidth(ctx, 3.2f);
    CGContextAddArc(ctx, cx, cy, radius + 4.5, curAngle, startAngle, 0);
    CGContextStrokePath(ctx);

    // 3. Render 3D Blender Raytraced Knob Body
    NSImage* knobImg = nil;
    switch (style) {
        case KnobStyle::Mechanics:   knobImg = gKnobMechanics; break;
        case KnobStyle::Pickup:      knobImg = gKnobPickup; break;
        case KnobStyle::Drive:       knobImg = gKnobDrive; break;
        case KnobStyle::Modulation:  knobImg = gKnobModulation; break;
        case KnobStyle::Reverb:      knobImg = gKnobReverb; break;
    }

    if (knobImg) {
        const int numFrames = 64;
        const int frameW = 96;
        const int frameH = 96;
        const int totalH = frameH * numFrames;
        const int frameIdx = std::clamp(static_cast<int>(std::round(norm * 63.0f)), 0, 63);
        const CGFloat srcY = totalH - (frameIdx + 1) * frameH;
        const NSRect srcRect = NSMakeRect(0, srcY, frameW, frameH);
        const CGFloat knobSize = 44.0;
        const NSRect knobDest = NSMakeRect(cx - knobSize * 0.5, cy - knobSize * 0.5, knobSize, knobSize);
        [knobImg drawInRect:knobDest fromRect:srcRect operation:NSCompositingOperationSourceOver fraction:1.0];
    } else {
        // Fallback procedural knob
        CGContextSetRGBFillColor(ctx, 0.18f, 0.20f, 0.24f, 1.0f);
        CGContextFillEllipseInRect(ctx, CGRectMake(cx - radius, cy - radius, radius * 2.0, radius * 2.0));
        CGContextSetRGBStrokeColor(ctx, 0.60f, 0.65f, 0.75f, 0.9f);
        CGContextSetLineWidth(ctx, 1.5f);
        CGContextStrokeEllipseInRect(ctx, CGRectMake(cx - radius, cy - radius, radius * 2.0, radius * 2.0));
    }

    // 4. Parameter Value Display Pill (Below Knob, y: rect.origin.y + 3 .. 17)
    NSString* valStr = formatParamValue(paramId, norm);
    NSDictionary* valAttrs = @{
        NSFontAttributeName: [NSFont boldSystemFontOfSize:8.0],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.35 green:0.90 blue:1.0 alpha:1.0]
    };
    const NSSize vSz = [valStr sizeWithAttributes:valAttrs];
    const NSRect pillRect = NSMakeRect(cx - (vSz.width + 10.0) * 0.5, rect.origin.y + 3.0, vSz.width + 10.0, 14.0);
    CGContextSetRGBFillColor(ctx, 0.04f, 0.05f, 0.07f, 0.92f);
    CGPathRef pillP = CGPathCreateWithRoundedRect(NSRectToCGRect(pillRect), 3.0, 3.0, nullptr);
    CGContextAddPath(ctx, pillP);
    CGContextFillPath(ctx);
    CGPathRelease(pillP);

    [valStr drawAtPoint:NSMakePoint(cx - vSz.width * 0.5, rect.origin.y + 3.0) withAttributes:valAttrs];

    // 5. MIDI CC Badge (if learned or armed)
    if (isArmed) {
        NSRect bRect = NSMakeRect(cx - 18.0, cy - 7.0, 36.0, 14.0);
        CGContextSetRGBFillColor(ctx, 0.95f, 0.75f, 0.10f, 0.95f);
        CGPathRef bp = CGPathCreateWithRoundedRect(NSRectToCGRect(bRect), 3.0, 3.0, nullptr);
        CGContextAddPath(ctx, bp);
        CGContextFillPath(ctx);
        CGPathRelease(bp);

        NSDictionary* bAttrs = @{
            NSFontAttributeName: [NSFont boldSystemFontOfSize:8.0],
            NSForegroundColorAttributeName: [NSColor blackColor]
        };
        [@"LEARN" drawAtPoint:NSMakePoint(cx - 14.0, cy - 6.0) withAttributes:bAttrs];
    } else if (midiCc >= 0) {
        NSRect bRect = NSMakeRect(rect.origin.x + 2.0, rect.origin.y + 50.0, 26.0, 11.0);
        CGContextSetRGBFillColor(ctx, 0.10f, 0.18f, 0.28f, 0.9f);
        CGPathRef bp = CGPathCreateWithRoundedRect(NSRectToCGRect(bRect), 2.0, 2.0, nullptr);
        CGContextAddPath(ctx, bp);
        CGContextFillPath(ctx);
        CGPathRelease(bp);

        NSDictionary* ccAttrs = @{
            NSFontAttributeName: [NSFont boldSystemFontOfSize:6.5],
            NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.40 green:0.80 blue:1.0 alpha:0.9]
        };
        NSString* ccStr = [NSString stringWithFormat:@"CC%d", midiCc];
        [ccStr drawAtPoint:NSMakePoint(bRect.origin.x + 2.0, bRect.origin.y) withAttributes:ccAttrs];
    }

    CGContextRestoreGState(ctx);
}

// =============================================================================
// 5. PERFORMANCE SECTION: RED FELT RAIL, 61-KEY PLAYABLE KEYBOARD & WHEELS
// =============================================================================
- (void)drawPerformanceSection:(CGContextRef)ctx {
    CGContextSaveGState(ctx);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();

    // 1. Iconic Crimson Red Felt Rail (Rhodes Harp Rail, y: 104..110)
    const CGFloat feltColors[8] = {
        0.78f, 0.08f, 0.12f, 1.0f,
        0.42f, 0.04f, 0.06f, 1.0f
    };
    CGGradientRef fGrad = CGGradientCreateWithColorComponents(cs, feltColors, nullptr, 2);
    CGContextDrawLinearGradient(ctx, fGrad, CGPointMake(0, 110), CGPointMake(0, 104), 0);
    CGGradientRelease(fGrad);

    // Felt trim line
    CGContextSetRGBStrokeColor(ctx, 0.90f, 0.20f, 0.25f, 0.8f);
    CGContextSetLineWidth(ctx, 0.8f);
    CGContextStrokeLineSegments(ctx, (CGPoint[]){CGPointMake(18, 110.5), CGPointMake(982, 110.5)}, 2);

    // 2. Sustain Pedal Stomp Switch
    const bool isSustained = engine_ ? engine_->isSustainActive() : false;
    CGPathRef pedP = CGPathCreateWithRoundedRect(NSRectToCGRect(pedalSwitchRect_), 4.0, 4.0, nullptr);
    CGContextAddPath(ctx, pedP);
    if (isSustained) CGContextSetRGBFillColor(ctx, 0.22f, 0.40f, 0.28f, 1.0f);
    else CGContextSetRGBFillColor(ctx, 0.12f, 0.13f, 0.16f, 1.0f);
    CGContextFillPath(ctx);
    CGContextSetRGBStrokeColor(ctx, isSustained ? 0.35f : 0.28f, isSustained ? 0.85f : 0.30f, isSustained ? 0.45f : 0.35f, 0.9f);
    CGContextSetLineWidth(ctx, 1.2f);
    CGContextAddPath(ctx, pedP);
    CGContextStrokePath(ctx);
    CGPathRelease(pedP);

    NSDictionary* pedAttrs = @{
        NSFontAttributeName: [NSFont boldSystemFontOfSize:8.0],
        NSForegroundColorAttributeName: isSustained ? [NSColor colorWithCalibratedRed:0.4 green:1.0 blue:0.5 alpha:1.0] : [NSColor colorWithCalibratedWhite:0.65 alpha:1.0]
    };
    [@"SUSTAIN" drawAtPoint:NSMakePoint(pedalSwitchRect_.origin.x + 8.0, pedalSwitchRect_.origin.y + 50.0) withAttributes:pedAttrs];
    [@"PEDAL" drawAtPoint:NSMakePoint(pedalSwitchRect_.origin.x + 12.0, pedalSwitchRect_.origin.y + 36.0) withAttributes:pedAttrs];

    // Jewel status on pedal
    CGContextSetRGBFillColor(ctx, isSustained ? 0.3f : 0.2f, isSustained ? 1.0f : 0.3f, isSustained ? 0.4f : 0.3f, 1.0f);
    CGContextFillEllipseInRect(ctx, CGRectMake(pedalSwitchRect_.origin.x + 25.0, pedalSwitchRect_.origin.y + 14.0, 8.0, 8.0));

    // 3. Pitch Bend and Modulation Wheels
    auto drawWheel = [&](NSRect r, const char* label, float norm) {
        // Well recess
        CGPathRef wp = CGPathCreateWithRoundedRect(NSRectToCGRect(r), 3.0, 3.0, nullptr);
        CGContextAddPath(ctx, wp);
        CGContextSetRGBFillColor(ctx, 0.05f, 0.06f, 0.07f, 1.0f);
        CGContextFillPath(ctx);
        CGContextSetRGBStrokeColor(ctx, 0.24f, 0.26f, 0.30f, 0.8f);
        CGContextSetLineWidth(ctx, 1.0f);
        CGContextAddPath(ctx, wp);
        CGContextStrokePath(ctx);
        CGPathRelease(wp);

        // Center track
        const CGFloat midX = r.origin.x + r.size.width * 0.5;
        CGContextSetRGBStrokeColor(ctx, 0.12f, 0.14f, 0.18f, 1.0f);
        CGContextSetLineWidth(ctx, 3.0f);
        CGContextStrokeLineSegments(ctx, (CGPoint[]){CGPointMake(midX, r.origin.y + 8.0), CGPointMake(midX, NSMaxY(r) - 8.0)}, 2);

        // 3D Slider Cap
        const CGFloat capH = 24.0;
        const CGFloat capY = r.origin.y + 4.0 + norm * (r.size.height - 8.0 - capH);
        const NSRect capDest = NSMakeRect(r.origin.x + 2.0, capY, r.size.width - 4.0, capH);

        if (gSliderCap) {
            [gSliderCap drawInRect:capDest fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1.0];
        } else {
            CGContextSetRGBFillColor(ctx, 0.35f, 0.38f, 0.44f, 1.0f);
            CGContextFillRect(ctx, capDest);
        }

        NSDictionary* la = @{
            NSFontAttributeName: [NSFont boldSystemFontOfSize:6.5],
            NSForegroundColorAttributeName: [NSColor colorWithCalibratedWhite:0.60 alpha:1.0]
        };
        NSString* lStr = [NSString stringWithUTF8String:label];
        const NSSize lsz = [lStr sizeWithAttributes:la];
        [lStr drawAtPoint:NSMakePoint(midX - lsz.width * 0.5, r.origin.y + 2.0) withAttributes:la];
    };
    drawWheel(pitchBendRect_, "PITCH", 0.5f);
    drawWheel(modWheelRect_,  "MOD",   0.0f);

    // 4. Playable 61-Key Keyboard (C2 to C7, 36 Naturals, 25 Sharps)
    const int startNote = 36; // C2
    const int numNaturals = 36;
    const CGFloat naturalW = keyboardRect_.size.width / static_cast<CGFloat>(numNaturals);
    const CGFloat naturalH = keyboardRect_.size.height;
    const CGFloat sharpW = naturalW * 0.62f;
    const CGFloat sharpH = naturalH * 0.62f;

    const bool isBlackPattern[12] = {false, true, false, true, false, false, true, false, true, false, true, false};

    // Draw White Natural Keys First
    int natIdx = 0;
    for (int note = startNote; note <= startNote + 60; ++note) {
        const int pitchClass = note % 12;
        if (!isBlackPattern[pitchClass]) {
            const CGFloat kx = keyboardRect_.origin.x + natIdx * naturalW;
            const NSRect kRect = NSMakeRect(kx, keyboardRect_.origin.y, naturalW, naturalH);

            const bool isHeld = (engine_ && engine_->isNoteActive(note)) || (mouseHeldMidiNote_ == note);

            // Ivory Key Gradient
            const CGFloat ivoryColors[8] = {
                isHeld ? 0.75f : 0.98f, isHeld ? 0.85f : 0.97f, isHeld ? 0.95f : 0.93f, 1.0f,
                isHeld ? 0.65f : 0.88f, isHeld ? 0.75f : 0.87f, isHeld ? 0.85f : 0.82f, 1.0f
            };
            CGGradientRef kGrad = CGGradientCreateWithColorComponents(cs, ivoryColors, nullptr, 2);
            CGContextSaveGState(ctx);
            CGPathRef kp = CGPathCreateWithRoundedRect(NSRectToCGRect(kRect), 1.5, 1.5, nullptr);
            CGContextAddPath(ctx, kp);
            CGContextClip(ctx);
            CGContextDrawLinearGradient(ctx, kGrad, CGPointMake(kx, NSMaxY(kRect)), CGPointMake(kx, kRect.origin.y), 0);
            CGGradientRelease(kGrad);
            CGContextRestoreGState(ctx);

            // Key border
            CGContextSetRGBStrokeColor(ctx, 0.20f, 0.22f, 0.25f, 0.85f);
            CGContextSetLineWidth(ctx, 0.8f);
            CGContextAddPath(ctx, kp);
            CGContextStrokePath(ctx);
            CGPathRelease(kp);

            ++natIdx;
        }
    }

    // Draw Black Sharp Keys On Top
    natIdx = 0;
    for (int note = startNote; note < startNote + 60; ++note) {
        const int pitchClass = note % 12;
        if (isBlackPattern[pitchClass]) {
            const CGFloat kx = keyboardRect_.origin.x + natIdx * naturalW - sharpW * 0.5f;
            const NSRect sRect = NSMakeRect(kx, keyboardRect_.origin.y + (naturalH - sharpH), sharpW, sharpH);

            const bool isHeld = (engine_ && engine_->isNoteActive(note)) || (mouseHeldMidiNote_ == note);

            const CGFloat blackColors[8] = {
                isHeld ? 0.25f : 0.16f, isHeld ? 0.45f : 0.17f, isHeld ? 0.65f : 0.19f, 1.0f,
                isHeld ? 0.15f : 0.05f, isHeld ? 0.25f : 0.06f, isHeld ? 0.35f : 0.07f, 1.0f
            };
            CGGradientRef sGrad = CGGradientCreateWithColorComponents(cs, blackColors, nullptr, 2);
            CGContextSaveGState(ctx);
            CGPathRef sp = CGPathCreateWithRoundedRect(NSRectToCGRect(sRect), 1.0, 1.0, nullptr);
            CGContextAddPath(ctx, sp);
            CGContextClip(ctx);
            CGContextDrawLinearGradient(ctx, sGrad, CGPointMake(kx, NSMaxY(sRect)), CGPointMake(kx, sRect.origin.y), 0);
            CGGradientRelease(sGrad);
            CGContextRestoreGState(ctx);

            CGContextSetRGBStrokeColor(ctx, 0.0f, 0.0f, 0.0f, 0.95f);
            CGContextSetLineWidth(ctx, 0.8f);
            CGContextAddPath(ctx, sp);
            CGContextStrokePath(ctx);
            CGPathRelease(sp);
        } else {
            ++natIdx;
        }
    }

    CGColorSpaceRelease(cs);
    CGContextRestoreGState(ctx);
}

// =============================================================================
// 6. PRESET BROWSER MODAL
// =============================================================================
- (void)drawPresetBrowserModal:(CGContextRef)ctx {
    CGContextSaveGState(ctx);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();

    // Dim background
    CGContextSetRGBFillColor(ctx, 0.0f, 0.0f, 0.0f, 0.70f);
    CGContextFillRect(ctx, CGRectMake(0, 0, 1000, 680));

    browserModalRect_ = NSMakeRect(200.0, 140.0, 600.0, 420.0);
    CGPathRef mp = CGPathCreateWithRoundedRect(NSRectToCGRect(browserModalRect_), 6.0, 6.0, nullptr);
    CGContextAddPath(ctx, mp);
    CGContextSetRGBFillColor(ctx, 0.10f, 0.11f, 0.13f, 0.98f);
    CGContextFillPath(ctx);
    CGContextSetRGBStrokeColor(ctx, 0.35f, 0.38f, 0.45f, 0.95f);
    CGContextSetLineWidth(ctx, 1.5f);
    CGContextAddPath(ctx, mp);
    CGContextStrokePath(ctx);
    CGPathRelease(mp);

    // Modal Title Bar
    NSDictionary* tAttrs = @{
        NSFontAttributeName: [NSFont fontWithName:@"Optima-Bold" size:14.0] ?: [NSFont boldSystemFontOfSize:14.0],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.95 green:0.86 blue:0.62 alpha:1.0]
    };
    [@"PyPHS Preset Library" drawAtPoint:NSMakePoint(browserModalRect_.origin.x + 20.0, NSMaxY(browserModalRect_) - 28.0) withAttributes:tAttrs];

    // Close Button [X]
    browserCloseRect_ = NSMakeRect(NSMaxX(browserModalRect_) - 32.0, NSMaxY(browserModalRect_) - 30.0, 22.0, 22.0);
    CGContextSetRGBFillColor(ctx, 0.22f, 0.24f, 0.28f, 1.0f);
    CGContextFillRect(ctx, browserCloseRect_);
    NSDictionary* xAttrs = @{
        NSFontAttributeName: [NSFont boldSystemFontOfSize:11.0],
        NSForegroundColorAttributeName: [NSColor whiteColor]
    };
    [@"X" drawAtPoint:NSMakePoint(browserCloseRect_.origin.x + 6.0, browserCloseRect_.origin.y + 4.0) withAttributes:xAttrs];

    // Category Tabs
    const char* catNames[6] = {"All", "Rhodes", "Wurlitzer", "Dyno/Bells", "Hybrid/Fx", "User"};
    const CGFloat tabW = 88.0;
    for (int c = 0; c < 6; ++c) {
        categoryTabRects_[c] = NSMakeRect(browserModalRect_.origin.x + 20.0 + c * (tabW + 6.0), NSMaxY(browserModalRect_) - 62.0, tabW, 24.0);
        const bool isSelected = (static_cast<int>(selectedCategory_) == c);
        CGPathRef tp = CGPathCreateWithRoundedRect(NSRectToCGRect(categoryTabRects_[c]), 3.0, 3.0, nullptr);
        CGContextAddPath(ctx, tp);
        if (isSelected) CGContextSetRGBFillColor(ctx, 0.25f, 0.38f, 0.55f, 1.0f);
        else CGContextSetRGBFillColor(ctx, 0.14f, 0.15f, 0.18f, 1.0f);
        CGContextFillPath(ctx);
        CGPathRelease(tp);

        NSDictionary* ca = @{
            NSFontAttributeName: [NSFont boldSystemFontOfSize:9.0],
            NSForegroundColorAttributeName: isSelected ? [NSColor whiteColor] : [NSColor colorWithCalibratedWhite:0.65 alpha:1.0]
        };
        NSString* cStr = [NSString stringWithUTF8String:catNames[c]];
        const NSSize csz = [cStr sizeWithAttributes:ca];
        [cStr drawAtPoint:NSMakePoint(categoryTabRects_[c].origin.x + (tabW - csz.width) * 0.5, categoryTabRects_[c].origin.y + 5.0) withAttributes:ca];
    }

    // Preset List Box
    const NSRect listRect = NSMakeRect(browserModalRect_.origin.x + 20.0, browserModalRect_.origin.y + 48.0, browserModalRect_.size.width - 40.0, 270.0);
    CGContextSetRGBFillColor(ctx, 0.05f, 0.06f, 0.08f, 1.0f);
    CGContextFillRect(ctx, listRect);
    CGContextSetRGBStrokeColor(ctx, 0.20f, 0.22f, 0.26f, 1.0f);
    CGContextStrokeRect(ctx, listRect);

    if (engine_) {
        const auto& presets = engine_->getPresetManager().getPresets();
        CGFloat rowY = NSMaxY(listRect) - 24.0;
        for (size_t p = 0; p < presets.size(); ++p) {
            if (selectedCategory_ != phs::PresetCategory::All && presets[p].category != selectedCategory_) {
                continue;
            }
            if (rowY < listRect.origin.y + 4.0) break;

            const bool isCurrent = (engine_->getCurrentPreset() == static_cast<int>(p));
            if (isCurrent) {
                CGContextSetRGBFillColor(ctx, 0.16f, 0.28f, 0.42f, 0.85f);
                CGContextFillRect(ctx, CGRectMake(listRect.origin.x + 2.0, rowY - 2.0, listRect.size.width - 4.0, 22.0));
            }

            NSDictionary* pAttrs = @{
                NSFontAttributeName: isCurrent ? [NSFont boldSystemFontOfSize:10.0] : [NSFont systemFontOfSize:10.0],
                NSForegroundColorAttributeName: isCurrent ? [NSColor colorWithCalibratedRed:0.3 green:0.9 blue:1.0 alpha:1.0] : [NSColor colorWithCalibratedWhite:0.85 alpha:1.0]
            };
            NSString* pName = [NSString stringWithUTF8String:presets[p].name.c_str()];
            [pName drawAtPoint:NSMakePoint(listRect.origin.x + 12.0, rowY + 2.0) withAttributes:pAttrs];

            rowY -= 24.0;
        }
    }

    // Save and Delete Buttons
    browserSaveRect_   = NSMakeRect(browserModalRect_.origin.x + 20.0, browserModalRect_.origin.y + 14.0, 110.0, 26.0);
    browserDeleteRect_ = NSMakeRect(browserModalRect_.origin.x + 140.0, browserModalRect_.origin.y + 14.0, 110.0, 26.0);

    auto drawModalActionBtn = [&](NSRect r, NSString* lbl) {
        CGPathRef p = CGPathCreateWithRoundedRect(NSRectToCGRect(r), 3.0, 3.0, nullptr);
        CGContextAddPath(ctx, p);
        CGContextSetRGBFillColor(ctx, 0.18f, 0.20f, 0.24f, 1.0f);
        CGContextFillPath(ctx);
        CGContextSetRGBStrokeColor(ctx, 0.35f, 0.38f, 0.45f, 1.0f);
        CGContextSetLineWidth(ctx, 1.0f);
        CGContextAddPath(ctx, p);
        CGContextStrokePath(ctx);
        CGPathRelease(p);

        NSDictionary* a = @{
            NSFontAttributeName: [NSFont boldSystemFontOfSize:9.0],
            NSForegroundColorAttributeName: [NSColor whiteColor]
        };
        const NSSize sz = [lbl sizeWithAttributes:a];
        [lbl drawAtPoint:NSMakePoint(r.origin.x + (r.size.width - sz.width) * 0.5, r.origin.y + (r.size.height - sz.height) * 0.5) withAttributes:a];
    };
    drawModalActionBtn(browserSaveRect_, @"Save Preset...");
    drawModalActionBtn(browserDeleteRect_, @"Delete Preset");

    CGColorSpaceRelease(cs);
    CGContextRestoreGState(ctx);
}

// =============================================================================
// MOUSE & INTERACTION HANDLING
// =============================================================================
- (void)mouseDown:(NSEvent*)event {
    NSPoint p = [self convertPoint:[event locationInWindow] fromView:nil];

    // If Preset Browser is open
    if (browserOpen_) {
        if (NSPointInRect(p, browserCloseRect_)) {
            browserOpen_ = false;
            [self setNeedsDisplay:YES];
            return;
        }
        for (int c = 0; c < 6; ++c) {
            if (NSPointInRect(p, categoryTabRects_[c])) {
                selectedCategory_ = static_cast<phs::PresetCategory>(c);
                [self setNeedsDisplay:YES];
                return;
            }
        }
        if (NSPointInRect(p, browserSaveRect_)) {
            [self openSavePresetDialog];
            return;
        }
        if (NSPointInRect(p, browserDeleteRect_)) {
            if (engine_) engine_->getPresetManager().deleteUserPreset(selectedPresetIdx_);
            [self setNeedsDisplay:YES];
            return;
        }

        const NSRect listRect = NSMakeRect(browserModalRect_.origin.x + 20.0, browserModalRect_.origin.y + 48.0, browserModalRect_.size.width - 40.0, 270.0);
        if (NSPointInRect(p, listRect) && engine_) {
            const auto& presets = engine_->getPresetManager().getPresets();
            CGFloat rowY = NSMaxY(listRect) - 24.0;
            for (size_t i = 0; i < presets.size(); ++i) {
                if (selectedCategory_ != phs::PresetCategory::All && presets[i].category != selectedCategory_) {
                    continue;
                }
                const NSRect rRow = NSMakeRect(listRect.origin.x, rowY - 2.0, listRect.size.width, 22.0);
                if (NSPointInRect(p, rRow)) {
                    engine_->loadPreset(static_cast<int>(i));
                    selectedPresetIdx_ = static_cast<int>(i);
                    browserOpen_ = false;
                    [self setNeedsDisplay:YES];
                    return;
                }
                rowY -= 24.0;
            }
        }
        return;
    }

    // Top Header Interactions
    for (int i = 0; i < 4; ++i) {
        if (NSPointInRect(p, modelButtonRects_[i])) {
            const float norm = static_cast<float>(i) / 3.0f;
            if (engine_) engine_->setParamNormalized(phs::kParamModelType, norm);
            if (paramCallback_) paramCallback_(phs::kParamModelType, norm);
            [self setNeedsDisplay:YES];
            return;
        }
    }

    if (NSPointInRect(p, undoButtonRect_)) {
        if (engine_) engine_->undo();
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(p, redoButtonRect_)) {
        if (engine_) engine_->redo();
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(p, midiLearnButtonRect_)) {
        if (engine_) {
            if (engine_->getLearningParamId() >= 0) engine_->stopMidiLearn();
            else engine_->startMidiLearn(0);
        }
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(p, presetPrevRect_)) {
        if (engine_) {
            const int count = engine_->getPresetManager().getPresetCount();
            if (count > 0) {
                const int cur = engine_->getCurrentPreset();
                engine_->loadPreset((cur - 1 + count) % count);
            }
        }
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(p, presetNextRect_)) {
        if (engine_) {
            const int count = engine_->getPresetManager().getPresetCount();
            if (count > 0) {
                const int cur = engine_->getCurrentPreset();
                engine_->loadPreset((cur + 1) % count);
            }
        }
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(p, presetBrowseButtonRect_) || NSPointInRect(p, presetBoxRect_)) {
        browserOpen_ = true;
        [self setNeedsDisplay:YES];
        return;
    }

    // Sustain Pedal Switch
    if (NSPointInRect(p, pedalSwitchRect_)) {
        if (engine_) {
            const bool s = !engine_->isSustainActive();
            engine_->setSustain(s);
        }
        [self setNeedsDisplay:YES];
        return;
    }

    // Knobs Check (with double click reset and fine drag Shift detection)
    const uint64_t nowMs = static_cast<uint64_t>([event timestamp] * 1000.0);
    const bool isShift = ([event modifierFlags] & NSEventModifierFlagShift) != 0;

    for (size_t i = 0; i < knobs_.size(); ++i) {
        if (NSPointInRect(p, knobs_[i].rect)) {
            const uint32_t pId = knobs_[i].paramId;

            if (engine_ && engine_->getLearningParamId() >= 0) {
                engine_->startMidiLearn(pId);
                [self setNeedsDisplay:YES];
                return;
            }

            if (lastClickedKnobIdx_ == static_cast<int>(i) && (nowMs - lastClickTimestamp_) < 350) {
                // Double click: Reset to default value
                const float defVal = phs::kParamMetadata[pId].defaultValue;
                if (engine_) engine_->setParamNormalized(pId, defVal);
                if (paramCallback_) paramCallback_(pId, defVal);
                draggedKnobIdx_ = -1;
                [self setNeedsDisplay:YES];
                return;
            }

            lastClickTimestamp_ = nowMs;
            lastClickedKnobIdx_ = static_cast<int>(i);
            draggedKnobIdx_ = static_cast<int>(i);
            dragStartY_ = p.y;
            dragStartVal_ = engine_ ? engine_->getParamNormalized(pId) : 0.5f;
            isFineDrag_ = isShift;
            return;
        }
    }

    // Playable Keyboard Check
    if (NSPointInRect(p, keyboardRect_)) {
        const int startNote = 36;
        const int numNaturals = 36;
        const CGFloat naturalW = keyboardRect_.size.width / static_cast<CGFloat>(numNaturals);
        const CGFloat naturalH = keyboardRect_.size.height;
        const CGFloat sharpW = naturalW * 0.62f;
        const CGFloat sharpH = naturalH * 0.62f;
        const bool isBlackPattern[12] = {false, true, false, true, false, false, true, false, true, false, true, false};

        int hitNote = -1;

        // Check black keys first
        int natIdx = 0;
        for (int note = startNote; note < startNote + 60; ++note) {
            const int pitchClass = note % 12;
            if (isBlackPattern[pitchClass]) {
                const CGFloat kx = keyboardRect_.origin.x + natIdx * naturalW - sharpW * 0.5f;
                const NSRect sRect = NSMakeRect(kx, keyboardRect_.origin.y + (naturalH - sharpH), sharpW, sharpH);
                if (NSPointInRect(p, sRect)) {
                    hitNote = note;
                    break;
                }
            } else {
                ++natIdx;
            }
        }

        // If not black key, check white key
        if (hitNote < 0) {
            const int clickedNatIdx = std::clamp(static_cast<int>((p.x - keyboardRect_.origin.x) / naturalW), 0, numNaturals - 1);
            natIdx = 0;
            for (int note = startNote; note <= startNote + 60; ++note) {
                const int pitchClass = note % 12;
                if (!isBlackPattern[pitchClass]) {
                    if (natIdx == clickedNatIdx) {
                        hitNote = note;
                        break;
                    }
                    ++natIdx;
                }
            }
        }

        if (hitNote >= 0) {
            mouseHeldMidiNote_ = hitNote;
            if (engine_) engine_->noteOn(hitNote, 0.85f);
            [self setNeedsDisplay:YES];
        }
    }
}

- (void)mouseDragged:(NSEvent*)event {
    if (draggedKnobIdx_ >= 0 && draggedKnobIdx_ < static_cast<int>(knobs_.size())) {
        NSPoint p = [self convertPoint:[event locationInWindow] fromView:nil];
        const CGFloat dy = p.y - dragStartY_;
        const float sensitivity = isFineDrag_ ? 0.0015f : 0.0065f;
        const float newVal = std::clamp(dragStartVal_ + static_cast<float>(dy) * sensitivity, 0.0f, 1.0f);

        const uint32_t pId = knobs_[draggedKnobIdx_].paramId;
        if (engine_) engine_->setParamNormalized(pId, newVal);
        if (paramCallback_) paramCallback_(pId, newVal);
        [self setNeedsDisplay:YES];
    }
}

- (void)mouseUp:(NSEvent*)event {
    (void)event;
    draggedKnobIdx_ = -1;
    if (mouseHeldMidiNote_ >= 0) {
        if (engine_) engine_->noteOff(mouseHeldMidiNote_);
        mouseHeldMidiNote_ = -1;
        [self setNeedsDisplay:YES];
    }
}

- (void)rightMouseDown:(NSEvent*)event {
    NSPoint p = [self convertPoint:[event locationInWindow] fromView:nil];
    for (size_t i = 0; i < knobs_.size(); ++i) {
        if (NSPointInRect(p, knobs_[i].rect)) {
            const uint32_t pId = knobs_[i].paramId;
            NSMenu* menu = [[[NSMenu alloc] initWithTitle:@"MIDI Learn"] autorelease];

            NSMenuItem* item1 = [menu addItemWithTitle:@"MIDI Learn" action:@selector(onMenuLearn:) keyEquivalent:@""];
            [item1 setTarget:self];
            [item1 setTag:pId];

            const int8_t cc = engine_ ? engine_->getParamCc(pId) : -1;
            if (cc >= 0) {
                NSMenuItem* item2 = [menu addItemWithTitle:[NSString stringWithFormat:@"Forget MIDI CC (%d)", cc] action:@selector(onMenuUnlearn:) keyEquivalent:@""];
                [item2 setTarget:self];
                [item2 setTag:pId];
            }

            [NSMenu popUpContextMenu:menu withEvent:event forView:self];
            return;
        }
    }
}

- (void)onMenuLearn:(NSMenuItem*)sender {
    if (engine_) engine_->startMidiLearn(static_cast<uint32_t>([sender tag]));
    [self setNeedsDisplay:YES];
}

- (void)onMenuUnlearn:(NSMenuItem*)sender {
    if (engine_) engine_->clearMidiMapping(static_cast<uint32_t>([sender tag]));
    [self setNeedsDisplay:YES];
}

- (void)keyDown:(NSEvent*)event {
    if (browserOpen_ && [event keyCode] == 53) { // Escape key closes browser
        browserOpen_ = false;
        [self setNeedsDisplay:YES];
        return;
    }
    [super keyDown:event];
}

- (void)openSavePresetDialog {
    NSTextField* input = [[[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 220, 24)] autorelease];
    [input setStringValue:@"My Custom Piano"];

    NSAlert* alert = [[[NSAlert alloc] init] autorelease];
    [alert setMessageText:@"Save User Preset"];
    [alert setInformativeText:@"Enter a name for your preset:"];
    [alert setAccessoryView:input];
    [alert addButtonWithTitle:@"Save"];
    [alert addButtonWithTitle:@"Cancel"];

    if ([alert runModal] == NSAlertFirstButtonReturn) {
        NSString* name = [input stringValue];
        if ([name length] > 0 && engine_) {
            float curParams[phs::kNumParams];
            for (uint32_t i = 0; i < phs::kNumParams; ++i) {
                curParams[i] = engine_->getParamNormalized(i);
            }
            engine_->getPresetManager().saveUserPreset([name UTF8String], phs::PresetCategory::User, "User", "", curParams);
            [self setNeedsDisplay:YES];
        }
    }
}

@end

// =============================================================================
// FACTORY CREATION FUNCTIONS
// =============================================================================
namespace phs {

NSView* createPhsEditorView(PhsSynthEngine* engine, ParamChangeCallback onParamChange) {
    PHS_VIEW_CLASS_NAME* view = [[PHS_VIEW_CLASS_NAME alloc] initWithFrame:NSMakeRect(0, 0, kEditorWidth, kEditorHeight)
                                                                     engine:engine
                                                                   callback:onParamChange];
    return view;
}

void destroyPhsEditorView(NSView* view) {
    if (view && [view isKindOfClass:[PHS_VIEW_CLASS_NAME class]]) {
        PHS_VIEW_CLASS_NAME* phsView = (PHS_VIEW_CLASS_NAME*)view;
        [phsView stopTimer];
    }
}

} // namespace phs
