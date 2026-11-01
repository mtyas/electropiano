#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#include "PhsCocoaView.h"
#include <cmath>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <algorithm>

#ifndef PHS_VIEW_CLASS_NAME
#define PHS_VIEW_CLASS_NAME PhsEditorCocoaView_Default
#endif

namespace {

struct KnobRect {
    uint32_t paramId;
    NSRect rect;
    const char* line1;
    const char* line2;
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

static NSImage* gKnobFilmstrip = nil;
static NSImage* gSliderCap = nil;
static NSImage* gRockerActive = nil;
static NSImage* gRockerInactive = nil;
static NSImage* gPanelCard = nil;

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

    NSString* kPath = findAsset(@"knob_filmstrip");
    if (kPath) gKnobFilmstrip = [[NSImage alloc] initWithContentsOfFile:kPath];

    NSString* sPath = findAsset(@"slider_cap_3d");
    if (sPath) gSliderCap = [[NSImage alloc] initWithContentsOfFile:sPath];

    NSString* raPath = findAsset(@"rocker_active_3d");
    if (raPath) gRockerActive = [[NSImage alloc] initWithContentsOfFile:raPath];

    NSString* riPath = findAsset(@"rocker_inactive_3d");
    if (riPath) gRockerInactive = [[NSImage alloc] initWithContentsOfFile:riPath];

    NSString* pcPath = findAsset(@"panel_card_3d");
    if (pcPath) gPanelCard = [[NSImage alloc] initWithContentsOfFile:pcPath];
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

    // 1. Top Header Bar (y: 624 .. 674)
    for (int i = 0; i < 4; ++i) {
        modelButtonRects_[i] = NSMakeRect(240.0 + i * 86.0, 634.0, 82.0, 28.0);
    }
    undoButtonRect_         = NSMakeRect(592.0, 634.0, 48.0, 28.0);
    redoButtonRect_         = NSMakeRect(644.0, 634.0, 48.0, 28.0);
    midiLearnButtonRect_    = NSMakeRect(698.0, 634.0, 76.0, 28.0);
    presetPrevRect_         = NSMakeRect(778.0, 634.0, 20.0, 28.0);
    presetBoxRect_          = NSMakeRect(800.0, 634.0, 154.0, 28.0);
    presetNextRect_         = NSMakeRect(956.0, 634.0, 20.0, 28.0);
    presetBrowseButtonRect_ = NSMakeRect(978.0, 634.0, 16.0, 28.0);

    // 2. Control Panels & Knobs (y: 114 .. 420)
    // Panel 1: Mechanics & Tine (x: 20 .. 330)
    knobs_.push_back({phs::kParamHammerHardness,   NSMakeRect(24.0,  324.0, 72.0, 78.0), "Hammer", "Hardness",   "MECHANICS"});
    knobs_.push_back({phs::kParamHammerHysteresis, NSMakeRect(100.0, 324.0, 72.0, 78.0), "Hammer", "Felt Loss",   "MECHANICS"});
    knobs_.push_back({phs::kParamStrikePosition,   NSMakeRect(176.0, 324.0, 72.0, 78.0), "Strike", "Position",    "MECHANICS"});
    knobs_.push_back({phs::kParamTineDecay,        NSMakeRect(252.0, 324.0, 72.0, 78.0), "Tine",   "Decay",       "MECHANICS"});

    knobs_.push_back({phs::kParamTineBell,         NSMakeRect(48.0,  228.0, 76.0, 78.0), "Bell",   "Modes",       "MECHANICS"});
    knobs_.push_back({phs::kParamToneBarCoupling,  NSMakeRect(136.0, 228.0, 76.0, 78.0), "ToneBar", "Coupling",   "MECHANICS"});
    knobs_.push_back({phs::kParamKeyNoise,         NSMakeRect(224.0, 228.0, 76.0, 78.0), "Action", "Thud",       "MECHANICS"});

    // Panel 2: Nonlinear Transducer & Preamp EQ (x: 338 .. 664)
    knobs_.push_back({phs::kParamPickupDistance,   NSMakeRect(352.0, 324.0, 74.0, 78.0), "Pickup", "Air Gap",     "PICKUP"});
    knobs_.push_back({phs::kParamPickupSymmetry,   NSMakeRect(440.0, 324.0, 74.0, 78.0), "Pickup", "Alignment",   "PICKUP"});
    knobs_.push_back({phs::kParamPickupResonance,  NSMakeRect(528.0, 324.0, 74.0, 78.0), "Coil",   "Resonance",   "PICKUP"});

    knobs_.push_back({phs::kParamPreampDrive,      NSMakeRect(352.0, 228.0, 74.0, 78.0), "Preamp", "Drive",       "PREAMP"});
    knobs_.push_back({phs::kParamBassBoost,        NSMakeRect(440.0, 228.0, 74.0, 78.0), "Bass",   "Tone",        "PREAMP"});
    knobs_.push_back({phs::kParamTrebleBoost,      NSMakeRect(528.0, 228.0, 74.0, 78.0), "Treble", "Tone",        "PREAMP"});

    // Panel 3: Modulation & Effects (x: 672 .. 982)
    knobs_.push_back({phs::kParamTremoloDepth,     NSMakeRect(684.0, 324.0, 68.0, 78.0), "Tremolo", "Depth",      "MODULATION"});
    knobs_.push_back({phs::kParamTremoloRate,      NSMakeRect(756.0, 324.0, 68.0, 78.0), "Tremolo", "Rate",       "MODULATION"});
    knobs_.push_back({phs::kParamPhaserMix,        NSMakeRect(828.0, 324.0, 68.0, 78.0), "Phaser",  "Mix",        "MODULATION"});
    knobs_.push_back({phs::kParamPhaserRate,       NSMakeRect(900.0, 324.0, 68.0, 78.0), "Phaser",  "Rate",       "MODULATION"});

    knobs_.push_back({phs::kParamChorusMix,        NSMakeRect(680.0, 228.0, 58.0, 78.0), "Chorus",  "Mix",        "EFFECTS"});
    knobs_.push_back({phs::kParamReverbMix,        NSMakeRect(740.0, 228.0, 58.0, 78.0), "Reverb",  "Mix",        "EFFECTS"});
    knobs_.push_back({phs::kParamReverbDecay,      NSMakeRect(800.0, 228.0, 58.0, 78.0), "Reverb",  "Decay",      "EFFECTS"});
    knobs_.push_back({phs::kParamVelocityCurve,    NSMakeRect(860.0, 228.0, 58.0, 78.0), "Velocity", "Curve",     "OUTPUT"});
    knobs_.push_back({phs::kParamMasterVolume,     NSMakeRect(920.0, 228.0, 58.0, 78.0), "Master",  "Volume",     "OUTPUT"});

    // 3. Performance Section (y: 8 .. 104)
    pedalSwitchRect_ = NSMakeRect(18.0, 64.0, 58.0, 40.0);
    pitchBendRect_   = NSMakeRect(80.0, 16.0, 26.0, 88.0);
    modWheelRect_    = NSMakeRect(110.0, 16.0, 26.0, 88.0);
    keyboardRect_    = NSMakeRect(144.0, 16.0, 838.0, 88.0);
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

        // Fine vertical woodgrain lines
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
        0.13f, 0.14f, 0.16f, 1.0f,
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

    // 4. Upper Section: Laboratory & Oscilloscope
    [self drawLaboratorySection:ctx];

    // 5. Control Panels & Knobs
    [self drawControlPanels:ctx];

    // 6. Performance Section: Red Felt Rail, 61-Key Keyboard & Wheels
    [self drawPerformanceSection:ctx];

    // 7. Modal Preset Browser (if active)
    if (browserOpen_) {
        [self drawPresetBrowserModal:ctx];
    }

    CGColorSpaceRelease(cs);
    CGContextRestoreGState(ctx);
}

// =============================================================================
// 1. TOP HEADER & BOUTIQUE BADGES
// =============================================================================
- (void)drawHeaderBar:(CGContextRef)ctx {
    CGContextSaveGState(ctx);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();

    // Brushed Aluminum Anodized Top Rail (y: 624 .. 676)
    const CGFloat railColors[12] = {
        0.24f, 0.26f, 0.30f, 1.0f,
        0.15f, 0.16f, 0.19f, 1.0f,
        0.10f, 0.11f, 0.13f, 1.0f
    };
    const CGFloat railLocs[3] = {0.0f, 0.45f, 1.0f};
    CGGradientRef rGrad = CGGradientCreateWithColorComponents(cs, railColors, railLocs, 3);
    CGContextDrawLinearGradient(ctx, rGrad, CGPointMake(0, 676), CGPointMake(0, 624), 0);
    CGGradientRelease(rGrad);
    CGColorSpaceRelease(cs);

    // Bottom shadow bevel line
    CGContextSetRGBStrokeColor(ctx, 0.35f, 0.38f, 0.46f, 0.7f);
    CGContextSetLineWidth(ctx, 1.0f);
    CGContextStrokeLineSegments(ctx, (CGPoint[]){CGPointMake(16, 624), CGPointMake(984, 624)}, 2);

    // Engraved Corner Hex Screws
    auto drawScrew = [&](CGFloat sx, CGFloat sy) {
        CGContextSetRGBFillColor(ctx, 0.28f, 0.30f, 0.36f, 0.95f);
        CGContextSetRGBStrokeColor(ctx, 0.08f, 0.09f, 0.11f, 1.0f);
        CGContextSetLineWidth(ctx, 0.8f);
        CGContextFillEllipseInRect(ctx, CGRectMake(sx - 3.5, sy - 3.5, 7.0, 7.0));
        CGContextStrokeEllipseInRect(ctx, CGRectMake(sx - 3.5, sy - 3.5, 7.0, 7.0));
        CGContextSetRGBStrokeColor(ctx, 0.12f, 0.13f, 0.16f, 0.9f);
        CGContextStrokeLineSegments(ctx, (CGPoint[]){CGPointMake(sx - 2.5, sy), CGPointMake(sx + 2.5, sy)}, 2);
    };
    drawScrew(24.0, 650.0);
    drawScrew(976.0, 650.0);

    // Gold Foil Brand Plate: "PyPHS"
    NSDictionary* brandAttrs = @{
        NSFontAttributeName: [NSFont fontWithName:@"Optima-ExtraBlack" size:18.0] ?: [NSFont boldSystemFontOfSize:18.0],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.96f green:0.84f blue:0.42f alpha:1.0f]
    };
    [@"PyPHS" drawAtPoint:NSMakePoint(36.0, 645.0) withAttributes:brandAttrs];

    // Prominent Glowing Version Badge: [ • v1.1.0 REVAMP ]
    NSRect verBadgeRect = NSMakeRect(104.0, 646.0, 118.0, 18.0);
    CGContextSetRGBFillColor(ctx, 0.08f, 0.25f, 0.16f, 0.95f);
    CGContextSetRGBStrokeColor(ctx, 0.25f, 0.85f, 0.45f, 0.95f);
    CGContextSetLineWidth(ctx, 1.0f);
    CGPathRef vPath = CGPathCreateWithRoundedRect(NSRectToCGRect(verBadgeRect), 4.0, 4.0, nullptr);
    CGContextAddPath(ctx, vPath);
    CGContextDrawPath(ctx, kCGPathFillStroke);
    CGPathRelease(vPath);

    // Glowing LED dot
    CGContextSetRGBFillColor(ctx, 0.30f, 1.0f, 0.55f, 1.0f);
    CGContextFillEllipseInRect(ctx, CGRectMake(110.0, 652.0, 6.0, 6.0));

    NSDictionary* verAttrs = @{
        NSFontAttributeName: [NSFont boldSystemFontOfSize:9.0],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.65f green:1.0f blue:0.75f alpha:1.0f]
    };
    [@"v1.1.0 REVAMP" drawAtPoint:NSMakePoint(120.0, 649.0) withAttributes:verAttrs];

    NSDictionary* subBrandAttrs = @{
        NSFontAttributeName: [NSFont boldSystemFontOfSize:8.0],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.55f green:0.72f blue:0.88f alpha:0.9f]
    };
    [@"PORT-HAMILTONIAN PHYSICAL MODEL" drawAtPoint:NSMakePoint(36.0, 633.0) withAttributes:subBrandAttrs];

    // 4 Model Selector Buttons
    const int currentModel = engine_ ? std::clamp(static_cast<int>(std::round(engine_->getParamNormalized(phs::kParamModelType) * 3.0f)), 0, 3) : 0;
    static const char* kModelNames[4] = {"Rhodes Mk I", "Rhodes Dyno", "Wurlitzer 200A", "Custom PHS"};

    for (int i = 0; i < 4; ++i) {
        const NSRect r = modelButtonRects_[i];
        const bool active = (i == currentModel);

        CGContextSaveGState(ctx);
        if (active) {
            CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
            const CGFloat actColors[8] = {
                0.22f, 0.45f, 0.62f, 1.0f,
                0.12f, 0.25f, 0.38f, 1.0f
            };
            CGGradientRef aGrad = CGGradientCreateWithColorComponents(cs, actColors, nullptr, 2);
            CGPathRef round = CGPathCreateWithRoundedRect(NSRectToCGRect(r), 4.0, 4.0, nullptr);
            CGContextAddPath(ctx, round);
            CGContextClip(ctx);
            CGContextDrawLinearGradient(ctx, aGrad, CGPointMake(r.origin.x, r.origin.y + r.size.height), CGPointMake(r.origin.x, r.origin.y), 0);
            CGGradientRelease(aGrad);
            CGColorSpaceRelease(cs);
            CGContextResetClip(ctx);

            CGContextSetRGBStrokeColor(ctx, 0.45f, 0.88f, 1.0f, 0.95f);
            CGContextSetLineWidth(ctx, 1.2f);
            CGPathRef pBorder = CGPathCreateWithRoundedRect(NSRectToCGRect(r), 4.0, 4.0, nullptr);
            CGContextAddPath(ctx, pBorder);
            CGContextStrokePath(ctx);
            CGPathRelease(pBorder);

            // Active Glowing Cyan Jewel Lamp
            CGContextSetRGBFillColor(ctx, 0.40f, 0.95f, 1.0f, 1.0f);
            CGContextFillEllipseInRect(ctx, CGRectMake(r.origin.x + 8.0, r.origin.y + 11.0, 6.0, 6.0));
        } else {
            CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
            const CGFloat inactColors[8] = {
                0.16f, 0.17f, 0.21f, 1.0f,
                0.09f, 0.10f, 0.12f, 1.0f
            };
            CGGradientRef iGrad = CGGradientCreateWithColorComponents(cs, inactColors, nullptr, 2);
            CGPathRef round = CGPathCreateWithRoundedRect(NSRectToCGRect(r), 4.0, 4.0, nullptr);
            CGContextAddPath(ctx, round);
            CGContextClip(ctx);
            CGContextDrawLinearGradient(ctx, iGrad, CGPointMake(r.origin.x, r.origin.y + r.size.height), CGPointMake(r.origin.x, r.origin.y), 0);
            CGGradientRelease(iGrad);
            CGColorSpaceRelease(cs);
            CGContextResetClip(ctx);

            CGContextSetRGBStrokeColor(ctx, 0.25f, 0.28f, 0.34f, 0.8f);
            CGContextSetLineWidth(ctx, 1.0f);
            CGPathRef pBorder = CGPathCreateWithRoundedRect(NSRectToCGRect(r), 4.0, 4.0, nullptr);
            CGContextAddPath(ctx, pBorder);
            CGContextStrokePath(ctx);
            CGPathRelease(pBorder);
        }

        NSDictionary* textAttrs = @{
            NSFontAttributeName: [NSFont boldSystemFontOfSize:9.5],
            NSForegroundColorAttributeName: active ? [NSColor whiteColor] : [NSColor colorWithCalibratedWhite:0.65 alpha:1.0]
        };
        NSString* title = [NSString stringWithUTF8String:kModelNames[i]];
        const NSSize sz = [title sizeWithAttributes:textAttrs];
        const CGFloat tx = r.origin.x + (active ? 9.0 : 0.0) + (r.size.width - (active ? 9.0 : 0.0) - sz.width) * 0.5;
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
            NSFontAttributeName: [NSFont boldSystemFontOfSize:9.5],
            NSForegroundColorAttributeName: enabled ? [NSColor whiteColor] : [NSColor colorWithCalibratedWhite:0.35 alpha:1.0]
        };
        const NSSize sz = [title sizeWithAttributes:attrs];
        [title drawAtPoint:NSMakePoint(r.origin.x + (r.size.width - sz.width) * 0.5, r.origin.y + (r.size.height - sz.height) * 0.5) withAttributes:attrs];
        CGContextRestoreGState(ctx);
    };
    drawBtn(undoButtonRect_, @"↺ Undo", engine_ ? engine_->canUndo() : false);
    drawBtn(redoButtonRect_, @"↻ Redo", engine_ ? engine_->canRedo() : false);

    // MIDI Learn Button (Pulsing amber when active)
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
            NSFontAttributeName: [NSFont boldSystemFontOfSize:9.5],
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

    // Preset Steppers & VFD Screen
    drawBtn(presetPrevRect_, @"<", true);
    {
        CGContextSaveGState(ctx);
        // VFD Screen Glass
        CGContextSetRGBFillColor(ctx, 0.03f, 0.07f, 0.06f, 0.95f);
        CGContextSetRGBStrokeColor(ctx, 0.20f, 0.45f, 0.35f, 0.85f);
        CGPathRef p = CGPathCreateWithRoundedRect(NSRectToCGRect(presetBoxRect_), 4.0, 4.0, nullptr);
        CGContextAddPath(ctx, p);
        CGContextDrawPath(ctx, kCGPathFillStroke);
        CGPathRelease(p);

        NSString* pName = @"Preset";
        int pIdx = 0;
        if (engine_) {
            pIdx = engine_->getCurrentPreset();
            const auto* itm = engine_->getPresetManager().getPreset(pIdx);
            if (itm) pName = [NSString stringWithUTF8String:itm->name.c_str()];
        }
        NSString* displayStr = [NSString stringWithFormat:@"%02d: %@", pIdx + 1, pName];
        NSDictionary* pAttrs = @{
            NSFontAttributeName: [NSFont boldSystemFontOfSize:9.0],
            NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.40f green:0.95f blue:0.75f alpha:1.0f]
        };
        [displayStr drawAtPoint:NSMakePoint(presetBoxRect_.origin.x + 5.0, presetBoxRect_.origin.y + 7.0) withAttributes:pAttrs];
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
    const NSRect labRect = NSMakeRect(20.0, 432.0, 960.0, 184.0);

    CGContextSaveGState(ctx);
    // Dark Outer Bezel
    CGContextSetRGBFillColor(ctx, 0.05f, 0.06f, 0.08f, 0.95f);
    CGContextSetRGBStrokeColor(ctx, 0.20f, 0.22f, 0.28f, 0.9f);
    CGContextSetLineWidth(ctx, 1.2f);
    CGPathRef rPath = CGPathCreateWithRoundedRect(NSRectToCGRect(labRect), 6.0, 6.0, nullptr);
    CGContextAddPath(ctx, rPath);
    CGContextDrawPath(ctx, kCGPathFillStroke);
    CGPathRelease(rPath);

    // Left Subpanel: Physical Port-Hamiltonian Cantilever Transducer Lab (x: 28 .. 486)
    const NSRect physRect = NSMakeRect(28.0, 440.0, 458.0, 168.0);
    CGContextSetRGBFillColor(ctx, 0.02f, 0.03f, 0.04f, 0.95f);
    CGContextSetRGBStrokeColor(ctx, 0.14f, 0.18f, 0.24f, 0.85f);
    CGPathRef phPath = CGPathCreateWithRoundedRect(NSRectToCGRect(physRect), 4.0, 4.0, nullptr);
    CGContextAddPath(ctx, phPath);
    CGContextDrawPath(ctx, kCGPathFillStroke);
    CGPathRelease(phPath);

    NSDictionary* labTitleAttrs = @{
        NSFontAttributeName: [NSFont boldSystemFontOfSize:9.5],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.45f green:0.75f blue:0.95f alpha:0.95f]
    };
    [@"PORT-HAMILTONIAN TRANSDUCER & CANTILEVER TINE" drawAtPoint:NSMakePoint(38.0, 592.0) withAttributes:labTitleAttrs];

    phs::ScopeSnapshot scope{};
    if (engine_) {
        scope = engine_->getScopeSnapshot();
    }

    // Telemetry Line
    NSDictionary* telAttrs = @{
        NSFontAttributeName: [NSFont monospacedSystemFontOfSize:8.5 weight:NSFontWeightBold],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.35f green:0.92f blue:0.55f alpha:1.0f]
    };
    NSString* hStr = [NSString stringWithFormat:@"H(t): %.2e J | Pdiss: %.2e W | Poly: %d | PASSIVITY: STABLE",
                      scope.totalHamiltonian, scope.dissipatedPower, scope.activeVoices];
    [hStr drawAtPoint:NSMakePoint(38.0, 576.0) withAttributes:telAttrs];

    // Animated Mechanical Schematic
    const CGFloat baseY = 506.0;
    const CGFloat clampX = 60.0;
    const CGFloat tipX = 390.0;
    const CGFloat tipExcursion = std::clamp(scope.tipDisplacement * 18.0f, -24.0f, 24.0f);
    const CGFloat hammerComp = std::clamp(scope.hammerCompression * 24.0f, 0.0f, 18.0f);

    // Resonant Tone Bar (Zinc-Plated Resonator Arm above Tine)
    CGContextSetRGBFillColor(ctx, 0.45f, 0.48f, 0.52f, 0.85f);
    CGContextSetRGBStrokeColor(ctx, 0.20f, 0.22f, 0.26f, 0.95f);
    CGContextSetLineWidth(ctx, 1.0f);
    CGContextFillRect(ctx, CGRectMake(clampX - 16.0, baseY + 18.0, 240.0, 12.0));
    CGContextStrokeRect(ctx, CGRectMake(clampX - 16.0, baseY + 18.0, 240.0, 12.0));

    // Clamped Root Block (Machined Cast Steel Block)
    CGContextSetRGBFillColor(ctx, 0.38f, 0.42f, 0.48f, 0.95f);
    CGContextFillRect(ctx, CGRectMake(clampX - 18.0, baseY - 26.0, 18.0, 58.0));
    CGContextSetRGBStrokeColor(ctx, 0.15f, 0.17f, 0.22f, 1.0f);
    CGContextStrokeRect(ctx, CGRectMake(clampX - 18.0, baseY - 26.0, 18.0, 58.0));
    // Hex bolts in clamp
    CGContextSetRGBFillColor(ctx, 0.20f, 0.22f, 0.26f, 1.0f);
    CGContextFillEllipseInRect(ctx, CGRectMake(clampX - 13.0, baseY + 20.0, 8.0, 8.0));
    CGContextFillEllipseInRect(ctx, CGRectMake(clampX - 13.0, baseY - 16.0, 8.0, 8.0));

    // Spring-Steel Tine (Flexing Cantilever Beam with Steel Sheen)
    CGContextSetRGBStrokeColor(ctx, 0.82f, 0.86f, 0.92f, 0.98f);
    CGContextSetLineWidth(ctx, 3.5f);
    CGContextBeginPath(ctx);
    CGContextMoveToPoint(ctx, clampX, baseY);
    CGContextAddCurveToPoint(ctx,
                             clampX + (tipX - clampX) * 0.45f, baseY,
                             clampX + (tipX - clampX) * 0.75f, baseY + tipExcursion * 0.4f,
                             tipX, baseY + tipExcursion);
    CGContextStrokePath(ctx);

    // Hammer Assembly (Birch Shank + Felt Tip)
    const CGFloat strikeX = clampX + (tipX - clampX) * 0.26f;
    const CGFloat hammerRestY = baseY - 40.0;
    const CGFloat hammerY = hammerRestY + (scope.hammerCompression > 0.001f ? (26.0f + hammerComp) : 0.0f);

    // Wooden hammer shank
    CGContextSetRGBStrokeColor(ctx, 0.76f, 0.56f, 0.36f, 0.95f);
    CGContextSetLineWidth(ctx, 3.2f);
    CGContextStrokeLineSegments(ctx, (CGPoint[]){CGPointMake(strikeX, hammerY - 26.0), CGPointMake(strikeX, hammerY)}, 2);

    // Felt hammer head (Layered White/Grey Felt)
    CGContextSetRGBFillColor(ctx, 0.95f, 0.94f, 0.90f, 1.0f);
    CGContextSetRGBStrokeColor(ctx, 0.35f, 0.32f, 0.30f, 1.0f);
    CGContextSetLineWidth(ctx, 1.0f);
    CGContextFillEllipseInRect(ctx, CGRectMake(strikeX - 7.0, hammerY - 5.0, 14.0, 11.0));
    CGContextStrokeEllipseInRect(ctx, CGRectMake(strikeX - 7.0, hammerY - 5.0, 14.0, 11.0));

    // Pickup Transducer: Bobbin with Copper Coil Windings & Alnico Pole Piece
    const CGFloat pickupX = tipX + 16.0;
    // Copper coil bobbin
    CGContextSetRGBFillColor(ctx, 0.85f, 0.46f, 0.22f, 0.95f);
    CGContextFillRect(ctx, CGRectMake(pickupX + 3.0, baseY - 14.0, 14.0, 28.0));
    CGContextSetRGBStrokeColor(ctx, 0.35f, 0.16f, 0.06f, 1.0f);
    CGContextStrokeRect(ctx, CGRectMake(pickupX + 3.0, baseY - 14.0, 14.0, 28.0));
    // Threaded Pole Piece Screw
    CGContextSetRGBFillColor(ctx, 0.65f, 0.68f, 0.75f, 1.0f);
    CGContextFillRect(ctx, CGRectMake(pickupX - 2.0, baseY - 4.0, 6.0, 8.0));
    CGContextSetRGBStrokeColor(ctx, 0.20f, 0.22f, 0.26f, 1.0f);
    CGContextStrokeRect(ctx, CGRectMake(pickupX - 2.0, baseY - 4.0, 6.0, 8.0));

    // Magnetic Flux Lines
    CGContextSetRGBStrokeColor(ctx, 0.35f, 0.85f, 0.95f, 0.6f);
    CGContextSetLineWidth(ctx, 1.0f);
    for (int k = -2; k <= 2; ++k) {
        CGContextBeginPath(ctx);
        CGContextMoveToPoint(ctx, tipX, baseY + tipExcursion + k * 3.0f);
        CGContextAddQuadCurveToPoint(ctx, (tipX + pickupX) * 0.5f, baseY + k * 7.0f, pickupX, baseY + k * 4.0f);
        CGContextStrokePath(ctx);
    }

    // Right Subpanel: Phosphor CRT Stereo Oscilloscope & Harmonic Spectrum (x: 494 .. 972)
    const NSRect scopeRect = NSMakeRect(494.0, 440.0, 478.0, 168.0);
    CGContextSetRGBFillColor(ctx, 0.02f, 0.04f, 0.03f, 0.98f);
    CGContextSetRGBStrokeColor(ctx, 0.08f, 0.30f, 0.18f, 0.85f);
    CGPathRef scPath = CGPathCreateWithRoundedRect(NSRectToCGRect(scopeRect), 4.0, 4.0, nullptr);
    CGContextAddPath(ctx, scPath);
    CGContextDrawPath(ctx, kCGPathFillStroke);
    CGPathRelease(scPath);

    // Graticule Grid
    CGContextSetRGBStrokeColor(ctx, 0.06f, 0.22f, 0.12f, 0.45f);
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
        NSFontAttributeName: [NSFont boldSystemFontOfSize:9.5],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.25f green:0.92f blue:0.55f alpha:0.95f]
    };
    [@"DUAL-TRACE PHOSPHOR CRT OSCILLOSCOPE" drawAtPoint:NSMakePoint(504.0, 592.0) withAttributes:scopeTitleAttrs];

    // Stereo Oscilloscope Waveform Trace
    CGContextSetRGBStrokeColor(ctx, 0.35f, 1.0f, 0.65f, 0.9f);
    CGContextSetLineWidth(ctx, 1.4f);
    CGContextBeginPath(ctx);
    const CGFloat midY = scopeRect.origin.y + scopeRect.size.height * 0.5f;
    const CGFloat maxWaveHeight = scopeRect.size.height * 0.42f;
    const uint32_t wPos = scope.writePos;
    for (int i = 0; i < phs::kScopeRingSize; ++i) {
        const uint32_t idx = (wPos + i) % phs::kScopeRingSize;
        const CGFloat x = scopeRect.origin.x + 6.0 + (i / static_cast<CGFloat>(phs::kScopeRingSize - 1)) * (scopeRect.size.width - 126.0);
        const CGFloat y = midY + std::clamp(scope.waveL[idx], -1.0f, 1.0f) * maxWaveHeight;
        if (i == 0) CGContextMoveToPoint(ctx, x, y);
        else CGContextAddLineToPoint(ctx, x, y);
    }
    CGContextStrokePath(ctx);

    // 4-Band Harmonic Bar Graph (f0, 2f0, 3f0, 6.3f0)
    const CGFloat barBaseX = scopeRect.origin.x + scopeRect.size.width - 108.0;
    const CGFloat barBaseY = scopeRect.origin.y + 14.0;
    static const char* kHarmLabels[4] = {"f0", "2f0", "3f0", "6.3f0"};
    for (int h = 0; h < 4; ++h) {
        const CGFloat bx = barBaseX + h * 26.0;
        float hLevel = 0.05f;
        if (scope.activeVoices > 0) {
            const float eNorm = std::min(1.0f, std::abs(scope.tipVelocity) * (1.0f + h * 0.3f));
            hLevel = (h == 0) ? eNorm * 0.82f : (h == 1) ? eNorm * 0.50f : (h == 2) ? eNorm * 0.32f : eNorm * 0.22f;
        }
        const CGFloat bh = std::max(4.0f, hLevel * 50.0f);
        CGContextSetRGBFillColor(ctx, 0.22f, 0.88f, 0.58f, 0.85f);
        CGContextFillRect(ctx, CGRectMake(bx, barBaseY, 18.0, bh));

        NSDictionary* bAttrs = @{
            NSFontAttributeName: [NSFont boldSystemFontOfSize:8.0],
            NSForegroundColorAttributeName: [NSColor colorWithCalibratedWhite:0.75 alpha:1.0]
        };
        NSString* lbl = [NSString stringWithUTF8String:kHarmLabels[h]];
        [lbl drawAtPoint:NSMakePoint(bx + 1.0, barBaseY + bh + 3.0) withAttributes:bAttrs];
    }

    CGContextRestoreGState(ctx);
}

// =============================================================================
// 3. HARDWARE CONTROL PANELS & FLUTED SKIRT KNOBS
// =============================================================================
- (void)drawControlPanels:(CGContextRef)ctx {
    const NSRect panelRects[3] = {
        NSMakeRect(20.0, 114.0, 310.0, 308.0),
        NSMakeRect(338.0, 114.0, 326.0, 308.0),
        NSMakeRect(672.0, 114.0, 308.0, 308.0)
    };
    static const char* kPanelTitles[3] = {
        "— TINE & HAMMER MECHANICS —",
        "— TRANSDUCER & PREAMP EQ —",
        "— VINTAGE MODULATION & STAGE —"
    };

    for (int p = 0; p < 3; ++p) {
        CGContextSaveGState(ctx);
        // Beveled Recessed Aluminum Plate
        CGContextSetRGBFillColor(ctx, 0.07f, 0.08f, 0.10f, 0.92f);
        CGContextSetRGBStrokeColor(ctx, 0.22f, 0.25f, 0.32f, 0.85f);
        CGContextSetLineWidth(ctx, 1.0f);
        CGPathRef pPath = CGPathCreateWithRoundedRect(NSRectToCGRect(panelRects[p]), 6.0, 6.0, nullptr);
        CGContextAddPath(ctx, pPath);
        CGContextDrawPath(ctx, kCGPathFillStroke);
        CGPathRelease(pPath);

        // Header Title Banner
        NSRect hRect = NSMakeRect(panelRects[p].origin.x + 8.0, panelRects[p].origin.y + panelRects[p].size.height - 24.0,
                                  panelRects[p].size.width - 16.0, 18.0);
        CGContextSetRGBFillColor(ctx, 0.11f, 0.13f, 0.17f, 0.9f);
        CGPathRef hp = CGPathCreateWithRoundedRect(NSRectToCGRect(hRect), 3.0, 3.0, nullptr);
        CGContextAddPath(ctx, hp);
        CGContextFillPath(ctx);
        CGPathRelease(hp);

        NSDictionary* tAttrs = @{
            NSFontAttributeName: [NSFont boldSystemFontOfSize:9.5],
            NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.90f green:0.92f blue:0.98f alpha:0.95f]
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
                   line1:[NSString stringWithUTF8String:k.line1]
                   line2:[NSString stringWithUTF8String:k.line2]
                   value:norm
                  midiCc:cc
                 isArmed:isArmed];
    }
}

- (void)draw3dKnob:(CGContextRef)ctx
              rect:(NSRect)rect
             param:(uint32_t)paramId
             line1:(NSString*)line1
             line2:(NSString*)line2
             value:(float)norm
            midiCc:(int8_t)midiCc
           isArmed:(bool)isArmed {
    const CGFloat cx = rect.origin.x + rect.size.width * 0.5;
    const CGFloat cy = rect.origin.y + 34.0;
    const CGFloat radius = 16.5;

    CGContextSaveGState(ctx);

    // 1. Two-Line Centered Parameter Label (Above Knob, y: rect.origin.y + 54 .. 76)
    NSMutableParagraphStyle* pStyle = [[[NSMutableParagraphStyle alloc] init] autorelease];
    [pStyle setAlignment:NSTextAlignmentCenter];
    [pStyle setLineSpacing:1.0];
    NSDictionary* titleAttrs = @{
        NSFontAttributeName: [NSFont boldSystemFontOfSize:8.0],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedWhite:0.82 alpha:1.0],
        NSParagraphStyleAttributeName: pStyle
    };
    NSString* fullTitle = [NSString stringWithFormat:@"%@\n%@", line1, line2];
    NSRect labelRect = NSMakeRect(rect.origin.x - 2.0, rect.origin.y + 54.0, rect.size.width + 4.0, 22.0);
    [fullTitle drawInRect:labelRect withAttributes:titleAttrs];

    // 2. Halo Arc Indicator (270° sweep, from 225° to -45°)
    const CGFloat startAngle = 225.0f * (M_PI / 180.0f);
    const CGFloat endAngle   = -45.0f * (M_PI / 180.0f);
    const CGFloat curAngle   = startAngle - norm * (startAngle - endAngle);

    // Dark Track Arc
    CGContextSetRGBStrokeColor(ctx, 0.16f, 0.18f, 0.23f, 0.9f);
    CGContextSetLineWidth(ctx, 2.8f);
    CGContextAddArc(ctx, cx, cy, radius + 4.0, endAngle, startAngle, 0);
    CGContextStrokePath(ctx);

    // Active Glowing Arc
    if (isArmed) {
        CGContextSetRGBStrokeColor(ctx, 1.0f, 0.85f, 0.20f, 1.0f);
    } else {
        CGContextSetRGBStrokeColor(ctx, 0.38f, 0.82f, 0.98f, 0.95f);
    }
    CGContextSetLineWidth(ctx, 3.0f);
    CGContextAddArc(ctx, cx, cy, radius + 4.0, curAngle, startAngle, 0);
    CGContextStrokePath(ctx);

    // 3. 3D Blender Raytraced Knob Body (y: 15 .. 53)
    if (gKnobFilmstrip) {
        const int numFrames = 64;
        const int frameW = 96;
        const int frameH = 96;
        const int totalH = frameH * numFrames;
        const int frameIdx = std::clamp(static_cast<int>(std::round(norm * 63.0f)), 0, 63);
        const CGFloat srcY = totalH - (frameIdx + 1) * frameH;
        const NSRect srcRect = NSMakeRect(0, srcY, frameW, frameH);
        const CGFloat knobSize = 38.0;
        const NSRect knobDest = NSMakeRect(cx - knobSize * 0.5, cy - knobSize * 0.5, knobSize, knobSize);
        [gKnobFilmstrip drawInRect:knobDest fromRect:srcRect operation:NSCompositingOperationSourceOver fraction:1.0];
    } else {
        CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
        const CGFloat capColors[8] = {
            0.34f, 0.37f, 0.44f, 1.0f,
            0.11f, 0.12f, 0.14f, 1.0f
        };
        CGGradientRef capGrad = CGGradientCreateWithColorComponents(cs, capColors, nullptr, 2);
        CGContextSaveGState(ctx);
        CGContextAddArc(ctx, cx, cy, radius, 0, 2.0 * M_PI, 0);
        CGContextClip(ctx);
        CGContextDrawLinearGradient(ctx, capGrad, CGPointMake(cx - radius, cy + radius), CGPointMake(cx + radius, cy - radius), 0);
        CGGradientRelease(capGrad);
        CGColorSpaceRelease(cs);
        CGContextRestoreGState(ctx);

        // Outer Silver Rim
        CGContextSetRGBStrokeColor(ctx, 0.48f, 0.52f, 0.60f, 0.95f);
        CGContextSetLineWidth(ctx, 1.2f);
        CGContextAddArc(ctx, cx, cy, radius, 0, 2.0 * M_PI, 0);
        CGContextStrokePath(ctx);

        // High-Contrast Indicator Pointer Line
        CGContextSetRGBStrokeColor(ctx, isArmed ? 1.0f : 0.95f, isArmed ? 0.85f : 0.95f, isArmed ? 0.2f : 0.95f, 1.0f);
        CGContextSetLineWidth(ctx, 2.2f);
        CGContextBeginPath(ctx);
        const CGFloat indLen1 = radius * 0.25f;
        const CGFloat indLen2 = radius * 0.90f;
        CGContextMoveToPoint(ctx, cx + indLen1 * std::cos(curAngle), cy + indLen1 * std::sin(curAngle));
        CGContextAddLineToPoint(ctx, cx + indLen2 * std::cos(curAngle), cy + indLen2 * std::sin(curAngle));
        CGContextStrokePath(ctx);
    }

    // 4. Parameter Value Pill (Below Knob, y: 3 .. 16)
    NSString* valStr = formatParamValue(paramId, norm);
    NSDictionary* valAttrs = @{
        NSFontAttributeName: [NSFont boldSystemFontOfSize:8.0],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.40 green:0.85 blue:1.0 alpha:1.0]
    };
    const NSSize vSz = [valStr sizeWithAttributes:valAttrs];
    const NSRect pillRect = NSMakeRect(cx - (vSz.width + 10.0) * 0.5, rect.origin.y + 3.0, vSz.width + 10.0, 13.0);
    CGContextSetRGBFillColor(ctx, 0.04f, 0.05f, 0.07f, 0.9f);
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

        NSDictionary* ccAttrs = @{
            NSFontAttributeName: [NSFont boldSystemFontOfSize:7.5],
            NSForegroundColorAttributeName: [NSColor blackColor]
        };
        [@"LEARN" drawAtPoint:NSMakePoint(bRect.origin.x + 3.0, bRect.origin.y + 1.0) withAttributes:ccAttrs];
    } else if (midiCc >= 0) {
        NSString* ccStr = [NSString stringWithFormat:@"CC %d", midiCc];
        NSDictionary* ccAttrs = @{
            NSFontAttributeName: [NSFont boldSystemFontOfSize:7.5],
            NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:1.0 green:0.85 blue:0.3 alpha:1.0]
        };
        const NSSize cSz = [ccStr sizeWithAttributes:ccAttrs];
        NSRect bRect = NSMakeRect(cx - (cSz.width + 8.0) * 0.5, cy - 7.0, cSz.width + 8.0, 14.0);
        CGContextSetRGBFillColor(ctx, 0.08f, 0.10f, 0.14f, 0.9f);
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
// 4. PERFORMANCE SECTION: RED FELT RAIL, 61-KEY KEYBOARD & WHEELS
// =============================================================================
- (void)drawPerformanceSection:(CGContextRef)ctx {
    CGContextSaveGState(ctx);

    // Left Performance Recess Well (x: 16 .. 140, y: 14 .. 112)
    const NSRect leftWell = NSMakeRect(18.0, 14.0, 120.0, 98.0);
    CGContextSetRGBFillColor(ctx, 0.09f, 0.10f, 0.12f, 0.95f);
    CGContextSetRGBStrokeColor(ctx, 0.22f, 0.25f, 0.30f, 0.85f);
    CGContextSetLineWidth(ctx, 1.0f);
    CGPathRef lwPath = CGPathCreateWithRoundedRect(NSRectToCGRect(leftWell), 5.0, 5.0, nullptr);
    CGContextAddPath(ctx, lwPath);
    CGContextDrawPath(ctx, kCGPathFillStroke);
    CGPathRelease(lwPath);

    // 1. Sustain Footswitch
    const bool sustainActive = engine_ && engine_->isNoteActive(60);
    CGContextSetRGBFillColor(ctx, 0.14f, 0.16f, 0.20f, 0.95f);
    CGContextSetRGBStrokeColor(ctx, sustainActive ? 0.35f : 0.28f, sustainActive ? 0.90f : 0.30f, sustainActive ? 0.45f : 0.36f, 0.95f);
    CGContextSetLineWidth(ctx, 1.2f);
    CGPathRef pedP = CGPathCreateWithRoundedRect(NSRectToCGRect(pedalSwitchRect_), 5.0, 5.0, nullptr);
    CGContextAddPath(ctx, pedP);
    CGContextDrawPath(ctx, kCGPathFillStroke);
    CGPathRelease(pedP);

    // LED ring on pedal
    CGContextSetRGBFillColor(ctx, sustainActive ? 0.35f : 0.20f, sustainActive ? 1.0f : 0.25f, sustainActive ? 0.45f : 0.28f, 1.0f);
    CGContextFillEllipseInRect(ctx, CGRectMake(pedalSwitchRect_.origin.x + 8.0, pedalSwitchRect_.origin.y + 24.0, 6.0, 6.0));

    NSDictionary* pedAttrs = @{
        NSFontAttributeName: [NSFont boldSystemFontOfSize:8.0],
        NSForegroundColorAttributeName: sustainActive ? [NSColor colorWithCalibratedRed:0.4 green:1.0 blue:0.5 alpha:1.0] : [NSColor colorWithCalibratedWhite:0.7 alpha:1.0]
    };
    [@"SUSTAIN" drawAtPoint:NSMakePoint(pedalSwitchRect_.origin.x + 18.0, pedalSwitchRect_.origin.y + 22.0) withAttributes:pedAttrs];
    NSDictionary* subPedAttrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:7.0 weight:NSFontWeightMedium],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedWhite:0.5 alpha:1.0]
    };
    [@"(Spacebar)" drawAtPoint:NSMakePoint(pedalSwitchRect_.origin.x + 8.0, pedalSwitchRect_.origin.y + 8.0) withAttributes:subPedAttrs];

    // 2. Pitch & Mod Wheels (Milled Ribbed Aluminum)
    auto drawWheel = [&](NSRect r, float val, NSString* name) {
        // Wheel Well
        CGContextSetRGBFillColor(ctx, 0.05f, 0.06f, 0.07f, 0.98f);
        CGContextSetRGBStrokeColor(ctx, 0.25f, 0.28f, 0.35f, 0.9f);
        CGPathRef wp = CGPathCreateWithRoundedRect(NSRectToCGRect(r), 4.0, 4.0, nullptr);
        CGContextAddPath(ctx, wp);
        CGContextDrawPath(ctx, kCGPathFillStroke);
        CGPathRelease(wp);

        // Ribbed 3D Slider Cap / Wheel Body
        const CGFloat wy = r.origin.y + 4.0 + val * (r.size.height - 24.0);
        if (gSliderCap) {
            const NSRect capDest = NSMakeRect(r.origin.x - 3.0, wy - 3.0, r.size.width + 6.0, 20.0);
            [gSliderCap drawInRect:capDest fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1.0];
        } else {
            CGContextSetRGBFillColor(ctx, 0.42f, 0.46f, 0.54f, 0.95f);
            CGContextFillRect(ctx, CGRectMake(r.origin.x + 2.0, wy, r.size.width - 4.0, 14.0));
            CGContextSetRGBStrokeColor(ctx, 0.85f, 0.88f, 0.95f, 0.9f);
            CGContextSetLineWidth(ctx, 1.2f);
            CGContextStrokeLineSegments(ctx, (CGPoint[]){CGPointMake(r.origin.x + 3.0, wy + 7.0), CGPointMake(r.origin.x + r.size.width - 3.0, wy + 7.0)}, 2);
        }

        NSDictionary* wAttrs = @{
            NSFontAttributeName: [NSFont boldSystemFontOfSize:7.5],
            NSForegroundColorAttributeName: [NSColor colorWithCalibratedWhite:0.65 alpha:1.0]
        };
        const NSSize wsz = [name sizeWithAttributes:wAttrs];
        [name drawAtPoint:NSMakePoint(r.origin.x + (r.size.width - wsz.width) * 0.5, r.origin.y - 12.0) withAttributes:wAttrs];
    };
    drawWheel(pitchBendRect_, 0.5f, @"PITCH");
    drawWheel(modWheelRect_, 0.0f, @"MOD");

    // 3. Iconic Fender Rhodes RED DAMPER FELT RAIL above Keys (y: 104 .. 112)
    const NSRect feltRail = NSMakeRect(keyboardRect_.origin.x, keyboardRect_.origin.y + keyboardRect_.size.height,
                                       keyboardRect_.size.width, 8.0);
    CGContextSetRGBFillColor(ctx, 0.72f, 0.12f, 0.14f, 0.95f);
    CGContextFillRect(ctx, NSRectToCGRect(feltRail));
    CGContextSetRGBStrokeColor(ctx, 0.40f, 0.06f, 0.08f, 0.95f);
    CGContextSetLineWidth(ctx, 1.0f);
    CGContextStrokeRect(ctx, NSRectToCGRect(feltRail));

    // 4. Playable 61-Key Keyboard (C2 to C7)
    const int startNote = 36;
    const int numKeys = 61;
    int numWhiteKeys = 0;
    for (int i = 0; i < numKeys; ++i) {
        if (!isBlackKey(startNote + i)) ++numWhiteKeys;
    }

    const CGFloat whiteKeyWidth = keyboardRect_.size.width / static_cast<CGFloat>(numWhiteKeys);
    const CGFloat whiteKeyHeight = keyboardRect_.size.height;
    const CGFloat blackKeyWidth = whiteKeyWidth * 0.62f;
    const CGFloat blackKeyHeight = whiteKeyHeight * 0.62f;

    // Draw White Keys First
    int whiteIdx = 0;
    for (int i = 0; i < numKeys; ++i) {
        const int note = startNote + i;
        if (isBlackKey(note)) continue;

        const CGFloat x = keyboardRect_.origin.x + whiteIdx * whiteKeyWidth;
        const NSRect wkRect = NSMakeRect(x, keyboardRect_.origin.y, whiteKeyWidth, whiteKeyHeight);
        const bool isHeld = (mouseHeldMidiNote_ == note) || (engine_ && engine_->isNoteActive(note));

        CGContextSaveGState(ctx);
        CGContextClipToRect(ctx, NSRectToCGRect(wkRect));
        if (isHeld) {
            CGContextSetRGBFillColor(ctx, 0.85f, 0.92f, 1.0f, 1.0f);
            CGContextFillRect(ctx, NSRectToCGRect(wkRect));
        } else {
            // Warm Ivory gradient
            CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
            const CGFloat ivColors[8] = {
                0.96f, 0.95f, 0.92f, 1.0f,
                0.86f, 0.85f, 0.82f, 1.0f
            };
            CGGradientRef ivGrad = CGGradientCreateWithColorComponents(cs, ivColors, nullptr, 2);
            CGContextDrawLinearGradient(ctx, ivGrad, CGPointMake(x, keyboardRect_.origin.y + whiteKeyHeight), CGPointMake(x, keyboardRect_.origin.y), 0);
            CGGradientRelease(ivGrad);
            CGColorSpaceRelease(cs);
        }
        CGContextRestoreGState(ctx);

        // Key Border
        CGContextSetRGBStrokeColor(ctx, 0.18f, 0.19f, 0.22f, 0.7f);
        CGContextSetLineWidth(ctx, 0.8f);
        CGContextStrokeRect(ctx, NSRectToCGRect(wkRect));

        // C-Note Octave Labels
        if ((note % 12) == 0) {
            const int oct = (note / 12) - 1;
            NSString* octStr = [NSString stringWithFormat:@"C%d", oct];
            NSDictionary* octAttrs = @{
                NSFontAttributeName: [NSFont boldSystemFontOfSize:8.0],
                NSForegroundColorAttributeName: [NSColor colorWithCalibratedWhite:0.4 alpha:1.0]
            };
            [octStr drawAtPoint:NSMakePoint(x + 2.0, keyboardRect_.origin.y + 4.0) withAttributes:octAttrs];
        }

        CGContextRestoreGState(ctx);
        ++whiteIdx;
    }

    // Draw Black Keys (Top Layer)
    whiteIdx = 0;
    for (int i = 0; i < numKeys; ++i) {
        const int note = startNote + i;
        if (isBlackKey(note)) {
            const CGFloat prevWhiteX = keyboardRect_.origin.x + (whiteIdx - 1) * whiteKeyWidth;
            const CGFloat bx = prevWhiteX + whiteKeyWidth - (blackKeyWidth * 0.5f);
            const NSRect bkRect = NSMakeRect(bx, keyboardRect_.origin.y + (whiteKeyHeight - blackKeyHeight), blackKeyWidth, blackKeyHeight);
            const bool isHeld = (mouseHeldMidiNote_ == note) || (engine_ && engine_->isNoteActive(note));

            CGContextSaveGState(ctx);
            if (isHeld) {
                CGContextSetRGBFillColor(ctx, 0.25f, 0.45f, 0.65f, 1.0f);
            } else {
                CGContextSetRGBFillColor(ctx, 0.10f, 0.10f, 0.12f, 1.0f);
            }
            CGPathRef bkP = CGPathCreateWithRoundedRect(NSRectToCGRect(bkRect), 2.0, 2.0, nullptr);
            CGContextAddPath(ctx, bkP);
            CGContextFillPath(ctx);
            CGPathRelease(bkP);

            // Bevel highlight on black key
            CGContextSetRGBStrokeColor(ctx, 0.28f, 0.30f, 0.35f, 0.8f);
            CGContextSetLineWidth(ctx, 0.8f);
            CGContextStrokeRect(ctx, NSRectToCGRect(bkRect));

            CGContextRestoreGState(ctx);
        } else {
            ++whiteIdx;
        }
    }

    CGContextRestoreGState(ctx);
}

// =============================================================================
// 5. PRESET BROWSER MODAL DRAWER
// =============================================================================
- (void)drawPresetBrowserModal:(CGContextRef)ctx {
    if (!browserOpen_) return;

    CGContextSaveGState(ctx);
    // Dark Backdrop
    CGContextSetRGBFillColor(ctx, 0.0f, 0.0f, 0.0f, 0.65f);
    CGContextFillRect(ctx, [self bounds]);

    // Modal Card
    browserModalRect_ = NSMakeRect(180.0, 90.0, 640.0, 500.0);
    CGContextSetRGBFillColor(ctx, 0.08f, 0.09f, 0.11f, 0.98f);
    CGContextSetRGBStrokeColor(ctx, 0.35f, 0.55f, 0.75f, 0.95f);
    CGContextSetLineWidth(ctx, 1.5f);
    CGPathRef mp = CGPathCreateWithRoundedRect(NSRectToCGRect(browserModalRect_), 8.0, 8.0, nullptr);
    CGContextAddPath(ctx, mp);
    CGContextDrawPath(ctx, kCGPathFillStroke);
    CGPathRelease(mp);

    // Modal Header
    NSDictionary* mTitleAttrs = @{
        NSFontAttributeName: [NSFont boldSystemFontOfSize:14.0],
        NSForegroundColorAttributeName: [NSColor whiteColor]
    };
    [@"PRESET LIBRARIAN" drawAtPoint:NSMakePoint(browserModalRect_.origin.x + 20.0, browserModalRect_.origin.y + browserModalRect_.size.height - 34.0) withAttributes:mTitleAttrs];

    // Close Button
    browserCloseRect_ = NSMakeRect(browserModalRect_.origin.x + browserModalRect_.size.width - 36.0,
                                   browserModalRect_.origin.y + browserModalRect_.size.height - 34.0, 24.0, 24.0);
    CGContextSetRGBFillColor(ctx, 0.20f, 0.22f, 0.26f, 0.9f);
    CGPathRef cp = CGPathCreateWithRoundedRect(NSRectToCGRect(browserCloseRect_), 4.0, 4.0, nullptr);
    CGContextAddPath(ctx, cp);
    CGContextFillPath(ctx);
    CGPathRelease(cp);
    NSDictionary* cAttrs = @{
        NSFontAttributeName: [NSFont boldSystemFontOfSize:11.0],
        NSForegroundColorAttributeName: [NSColor whiteColor]
    };
    [@"✕" drawAtPoint:NSMakePoint(browserCloseRect_.origin.x + 7.0, browserCloseRect_.origin.y + 4.0) withAttributes:cAttrs];

    // Category Tabs (Left column)
    static const char* kCategories[6] = {"All Presets", "Rhodes", "Wurlitzer", "Dyno / LA", "Atmospheric", "FX / Experimental"};
    for (int c = 0; c < 6; ++c) {
        const NSRect cr = NSMakeRect(browserModalRect_.origin.x + 20.0, browserModalRect_.origin.y + 360.0 - c * 38.0, 160.0, 32.0);
        categoryTabRects_[c] = cr;
        const bool active = (static_cast<int>(selectedCategory_) == c);

        CGContextSetRGBFillColor(ctx, active ? 0.18f : 0.11f, active ? 0.32f : 0.13f, active ? 0.45f : 0.16f, 0.9f);
        CGPathRef catP = CGPathCreateWithRoundedRect(NSRectToCGRect(cr), 4.0, 4.0, nullptr);
        CGContextAddPath(ctx, catP);
        CGContextFillPath(ctx);
        CGPathRelease(catP);

        NSDictionary* catAttrs = @{
            NSFontAttributeName: [NSFont boldSystemFontOfSize:10.0],
            NSForegroundColorAttributeName: active ? [NSColor whiteColor] : [NSColor colorWithCalibratedWhite:0.65 alpha:1.0]
        };
        NSString* cTitle = [NSString stringWithUTF8String:kCategories[c]];
        [cTitle drawAtPoint:NSMakePoint(cr.origin.x + 10.0, cr.origin.y + 9.0) withAttributes:catAttrs];
    }

    // Preset List (Right column)
    if (engine_) {
        const auto& presets = engine_->getPresetManager().getPresets();
        int row = 0;
        for (size_t i = 0; i < presets.size(); ++i) {
            const auto& p = presets[i];
            if (selectedCategory_ != phs::PresetCategory::All && p.category != selectedCategory_) continue;

            const NSRect rowRect = NSMakeRect(browserModalRect_.origin.x + 196.0, browserModalRect_.origin.y + 410.0 - row * 34.0, 420.0, 28.0);
            const bool isSelected = (static_cast<int>(i) == selectedPresetIdx_);

            CGContextSetRGBFillColor(ctx, isSelected ? 0.22f : 0.10f, isSelected ? 0.40f : 0.12f, isSelected ? 0.58f : 0.14f, 0.9f);
            CGPathRef rp = CGPathCreateWithRoundedRect(NSRectToCGRect(rowRect), 4.0, 4.0, nullptr);
            CGContextAddPath(ctx, rp);
            CGContextFillPath(ctx);
            CGPathRelease(rp);

            NSDictionary* pAttrs = @{
                NSFontAttributeName: [NSFont boldSystemFontOfSize:10.5],
                NSForegroundColorAttributeName: isSelected ? [NSColor whiteColor] : [NSColor colorWithCalibratedWhite:0.8 alpha:1.0]
            };
            NSString* pName = [NSString stringWithFormat:@"%02zu.  %@", i + 1, [NSString stringWithUTF8String:p.name.c_str()]];
            [pName drawAtPoint:NSMakePoint(rowRect.origin.x + 12.0, rowRect.origin.y + 7.0) withAttributes:pAttrs];
            ++row;
            if (row >= 11) break;
        }
    }

    // Save & Delete Buttons
    browserSaveRect_ = NSMakeRect(browserModalRect_.origin.x + 20.0, browserModalRect_.origin.y + 20.0, 110.0, 28.0);
    browserDeleteRect_ = NSMakeRect(browserModalRect_.origin.x + 140.0, browserModalRect_.origin.y + 20.0, 110.0, 28.0);

    auto drawModalBtn = [&](NSRect r, NSString* txt) {
        CGContextSetRGBFillColor(ctx, 0.15f, 0.18f, 0.24f, 0.95f);
        CGContextSetRGBStrokeColor(ctx, 0.35f, 0.42f, 0.55f, 0.9f);
        CGPathRef p = CGPathCreateWithRoundedRect(NSRectToCGRect(r), 4.0, 4.0, nullptr);
        CGContextAddPath(ctx, p);
        CGContextDrawPath(ctx, kCGPathFillStroke);
        CGPathRelease(p);
        NSDictionary* bAttrs = @{
            NSFontAttributeName: [NSFont boldSystemFontOfSize:9.5],
            NSForegroundColorAttributeName: [NSColor whiteColor]
        };
        const NSSize sz = [txt sizeWithAttributes:bAttrs];
        [txt drawAtPoint:NSMakePoint(r.origin.x + (r.size.width - sz.width) * 0.5, r.origin.y + (r.size.height - sz.height) * 0.5) withAttributes:bAttrs];
    };
    drawModalBtn(browserSaveRect_, @"Save Preset...");
    drawModalBtn(browserDeleteRect_, @"Delete Preset");

    CGContextRestoreGState(ctx);
}

// =============================================================================
// MOUSE & USER INTERACTION
// =============================================================================
- (void)mouseDown:(NSEvent*)event {
    if (!engine_) return;
    const NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    const uint64_t nowMs = static_cast<uint64_t>([event timestamp] * 1000.0);
    const bool isDoubleClick = (nowMs - lastClickTimestamp_ < 350);
    lastClickTimestamp_ = nowMs;

    // 1. Preset Browser Modal Interactions
    if (browserOpen_) {
        if (NSPointInRect(pt, browserCloseRect_) || !NSPointInRect(pt, browserModalRect_)) {
            browserOpen_ = false;
            [self setNeedsDisplay:YES];
            return;
        }
        for (int c = 0; c < 6; ++c) {
            if (NSPointInRect(pt, categoryTabRects_[c])) {
                selectedCategory_ = static_cast<phs::PresetCategory>(c);
                [self setNeedsDisplay:YES];
                return;
            }
        }
        const auto& presets = engine_->getPresetManager().getPresets();
        int row = 0;
        for (size_t i = 0; i < presets.size(); ++i) {
            if (selectedCategory_ != phs::PresetCategory::All && presets[i].category != selectedCategory_) continue;
            const NSRect rowRect = NSMakeRect(browserModalRect_.origin.x + 196.0, browserModalRect_.origin.y + 410.0 - row * 34.0, 420.0, 28.0);
            if (NSPointInRect(pt, rowRect)) {
                selectedPresetIdx_ = static_cast<int>(i);
                engine_->loadPreset(selectedPresetIdx_);
                browserOpen_ = false;
                [self setNeedsDisplay:YES];
                return;
            }
            ++row;
            if (row >= 11) break;
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

    // 2. Top Header Controls
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
        engine_->loadPreset(prev);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(pt, presetNextRect_)) {
        const int cur = engine_->getCurrentPreset();
        const int total = engine_->getPresetManager().getPresetCount();
        const int next = (cur + 1) % total;
        engine_->loadPreset(next);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(pt, presetBrowseButtonRect_) || NSPointInRect(pt, presetBoxRect_)) {
        browserOpen_ = true;
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
            isFineDrag_ = ([event modifierFlags] & NSEventModifierFlagShift) != 0;
            return;
        }
    }

    // 4. Playable Keyboard
    if (NSPointInRect(pt, keyboardRect_)) {
        const int startNote = 36;
        const int numKeys = 61;
        int numWhiteKeys = 0;
        for (int i = 0; i < numKeys; ++i) {
            if (!isBlackKey(startNote + i)) ++numWhiteKeys;
        }
        const CGFloat whiteKeyWidth = keyboardRect_.size.width / static_cast<CGFloat>(numWhiteKeys);
        const CGFloat whiteKeyHeight = keyboardRect_.size.height;
        const CGFloat blackKeyWidth = whiteKeyWidth * 0.62f;
        const CGFloat blackKeyHeight = whiteKeyHeight * 0.62f;

        int hitNote = -1;
        // Check Black Keys First
        if (pt.y >= keyboardRect_.origin.y + (whiteKeyHeight - blackKeyHeight)) {
            int wIdx = 0;
            for (int i = 0; i < numKeys; ++i) {
                const int note = startNote + i;
                if (isBlackKey(note)) {
                    const CGFloat prevWhiteX = keyboardRect_.origin.x + (wIdx - 1) * whiteKeyWidth;
                    const CGFloat bx = prevWhiteX + whiteKeyWidth - (blackKeyWidth * 0.5f);
                    const NSRect bkRect = NSMakeRect(bx, keyboardRect_.origin.y + (whiteKeyHeight - blackKeyHeight), blackKeyWidth, blackKeyHeight);
                    if (NSPointInRect(pt, bkRect)) {
                        hitNote = note;
                        break;
                    }
                } else {
                    ++wIdx;
                }
            }
        }
        // Check White Keys
        if (hitNote < 0) {
            const int wIdx = std::clamp(static_cast<int>((pt.x - keyboardRect_.origin.x) / whiteKeyWidth), 0, numWhiteKeys - 1);
            int curW = 0;
            for (int i = 0; i < numKeys; ++i) {
                const int note = startNote + i;
                if (!isBlackKey(note)) {
                    if (curW == wIdx) {
                        hitNote = note;
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
            return;
        }
    }
}

- (void)mouseDragged:(NSEvent*)event {
    if (!engine_) return;
    const NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];

    if (draggedKnobIdx_ >= 0 && draggedKnobIdx_ < static_cast<int>(knobs_.size())) {
        const auto& k = knobs_[draggedKnobIdx_];
        const CGFloat dy = pt.y - dragStartY_;
        const float sensitivity = isFineDrag_ ? 0.0015f : 0.0055f;
        const float newVal = std::clamp(static_cast<float>(dragStartVal_ + dy * sensitivity), 0.0f, 1.0f);
        engine_->setParamNormalized(k.paramId, newVal);
        if (paramCallback_) paramCallback_(k.paramId, newVal);
        [self setNeedsDisplay:YES];
        return;
    }
}

- (void)mouseUp:(NSEvent*)event {
    (void)event;
    if (draggedKnobIdx_ >= 0) {
        draggedKnobIdx_ = -1;
    }
    if (mouseHeldMidiNote_ >= 0) {
        if (engine_) {
            engine_->pushGuiMidiEvent(0x80, mouseHeldMidiNote_, 0);
        }
        mouseHeldMidiNote_ = -1;
    }
    [self setNeedsDisplay:YES];
}

- (void)rightMouseDown:(NSEvent*)event {
    if (!engine_) return;
    const NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    for (size_t i = 0; i < knobs_.size(); ++i) {
        if (NSPointInRect(pt, knobs_[i].rect)) {
            const uint32_t pId = knobs_[i].paramId;
            NSMenu* menu = [[[NSMenu alloc] initWithTitle:@"MIDI Control"] autorelease];
            NSMenuItem* itemLearn = [menu addItemWithTitle:@"MIDI Learn"
                                                    action:@selector(onMenuLearn:)
                                             keyEquivalent:@""];
            [itemLearn setTarget:self];
            [itemLearn setTag:static_cast<NSInteger>(pId)];

            if (engine_->getParamCc(pId) >= 0) {
                NSMenuItem* itemUnlearn = [menu addItemWithTitle:@"Remove MIDI Assignment"
                                                          action:@selector(onMenuUnlearn:)
                                                   keyEquivalent:@""];
                [itemUnlearn setTarget:self];
                [itemUnlearn setTag:static_cast<NSInteger>(pId)];
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
    if (!engine_) return;
    if ([[event characters] isEqualToString:@" "]) {
        const bool active = engine_->isNoteActive(60);
        engine_->handleMidiEvent(0xB0, 64, active ? 0 : 127);
        [self setNeedsDisplay:YES];
        return;
    }
    [super keyDown:event];
}

- (void)openSavePresetDialog {
    NSTextField* input = [[[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 240, 24)] autorelease];
    [input setStringValue:@"My Custom Electric Piano"];

    NSAlert* alert = [[[NSAlert alloc] init] autorelease];
    [alert setMessageText:@"Save User Preset"];
    [alert setInformativeText:@"Enter a name for the new Port-Hamiltonian preset:"];
    [alert addButtonWithTitle:@"Save"];
    [alert addButtonWithTitle:@"Cancel"];
    [alert setAccessoryView:input];

    if ([alert runModal] == NSAlertFirstButtonReturn) {
        NSString* name = [input stringValue];
        if (name && [name length] > 0 && engine_) {
            float curVals[phs::kNumParams];
            for (uint32_t i = 0; i < phs::kNumParams; ++i) {
                curVals[i] = engine_->getParamNormalized(i);
            }
            engine_->getPresetManager().saveUserPreset([name UTF8String], phs::PresetCategory::User, "User", "", curVals);
            [self setNeedsDisplay:YES];
        }
    }
}

@end

namespace phs {

NSView* createPhsEditorView(PhsSynthEngine* engine, ParamChangeCallback onParamChange) {
    NSRect frame = NSMakeRect(0, 0, kEditorWidth, kEditorHeight);
    auto* view = [[PHS_VIEW_CLASS_NAME alloc] initWithFrame:frame
                                                     engine:engine
                                                   callback:onParamChange];
    return view;
}

void destroyPhsEditorView(NSView* view) {
    if (!view) return;
    if ([view respondsToSelector:@selector(stopTimer)]) {
        [view performSelector:@selector(stopTimer)];
    }
    [view release];
}

} // namespace phs
