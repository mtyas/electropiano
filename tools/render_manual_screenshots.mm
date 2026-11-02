// Renders the screenshots used by docs/MANUAL.md and README.md (docs/images/).
//
// clang++ -std=c++17 -ObjC++ -fno-objc-arc -O2 -I. -DPHS_VIEW_CLASS_NAME=PhsEditorView_Docs \
//     tools/render_manual_screenshots.mm src/gui/PhsCocoaView.mm src/gui/PhsEditor.cpp \
//     -framework Cocoa -framework QuartzCore -o build/render_manual_screenshots
// build/render_manual_screenshots docs/images
#import <Cocoa/Cocoa.h>
#include "src/dsp/PhsSynthEngine.hpp"
#include "src/gui/PhsCocoaView.h"
#include "src/gui/PhsSoftwareCanvas.hpp"
#include <string>
#include <vector>

namespace {

std::string gDir;

NSBitmapImageRep* renderView(NSView* view, int scale) {
    const NSRect frame = NSMakeRect(0, 0, phs::kEditorWidth, phs::kEditorHeight);
    NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nil pixelsWide:phs::kEditorWidth * scale
                                                                    pixelsHigh:phs::kEditorHeight * scale bitsPerSample:8
                                                               samplesPerPixel:4 hasAlpha:YES isPlanar:NO
                                                                colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
    [rep setSize:frame.size];
    [view displayRectIgnoringOpacity:frame inContext:[NSGraphicsContext graphicsContextWithBitmapImageRep:rep]];
    return rep;
}

void writePng(CGImageRef img, const std::string& name) {
    NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithCGImage:img];
    [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}]
        writeToFile:[NSString stringWithUTF8String:(gDir + "/" + name).c_str()] atomically:YES];
    [rep release];
    std::printf("wrote %s/%s\n", gDir.c_str(), name.c_str());
}

// Full editor, plus optional crops given in editor coordinates (y up)
struct Crop { const char* name; float x, y, w, h; };
void shoot(NSView* view, const char* fullName, std::vector<Crop> crops = {}) {
    const int scale = 2;
    NSBitmapImageRep* rep = renderView(view, scale);
    CGImageRef full = [rep CGImage];
    if (fullName) writePng(full, fullName);
    for (const auto& c : crops) {
        const CGRect r = CGRectMake(c.x * scale, (phs::kEditorHeight - c.y - c.h) * scale, c.w * scale, c.h * scale);
        CGImageRef part = CGImageCreateWithImageInRect(full, r);
        writePng(part, c.name);
        CGImageRelease(part);
    }
    [rep release];
}

void playChord(phs::PhsSynthEngine& e, std::initializer_list<int> notes) {
    for (int n : notes) e.noteOn(n, 0.8f);
    float l[64], r[64];
    e.processBlock(l, r, 64); // publishes the active-note mask the keyboard draws
}

} // namespace

int main(int argc, char** argv) {
    @autoreleasepool {
        gDir = argc > 1 ? argv[1] : "docs/images";
        phs::PhsSynthEngine engine;
        engine.prepare(48000.0f);
        NSView* view = phs::createPhsEditorView(&engine, nullptr);
        [view setFrame:NSMakeRect(0, 0, phs::kEditorWidth, phs::kEditorHeight)];
        phs::gui::Editor* ed = phs::editorOfPhsView(view);

        // 1. Rhodes Mk I with a chord held, plus section crops
        engine.loadPreset(0, false);
        playChord(engine, {48, 55, 60, 64, 71});
        shoot(view, "editor_rhodes.png", {
            {"section_header.png", 0, 726, 900, 50},
            {"section_signal_chain.png", 0, 674, 900, 48},
            {"section_resonator_pickup.png", 8, 398, 884, 240},
            {"section_effects.png", 8, 126, 884, 274},
            {"section_keyboard.png", 8, 6, 884, 122},
        });
        for (int n : {48, 55, 60, 64, 71}) engine.noteOff(n);
        float l[4096], r[4096];
        engine.processBlock(l, r, 4096);

        // 2. Wurlitzer 200A (OpenWurli) and the stage-specific panels
        engine.loadPreset(8, false);
        shoot(view, "editor_wurlitzer.png", {
            {"section_reed_electrostatic.png", 8, 398, 884, 240},
            {"section_200a_amp.png", 8, 262, 440, 140},
        });

        // 3. A hybrid: Rhodes tine through the 200A circuit
        engine.loadPreset(19, false);
        shoot(view, "editor_hybrid.png", {{"section_signal_chain_hybrid.png", 0, 674, 900, 48}});


        // 3b. The pages: a held chord, a few slots filled in, a mastering curve, a compressed mix
        {
            using namespace phs;
            engine.loadPreset(0, false);
            engine.setParamNormalized(kParamMpeMode, 1.0f);
            engine.setParamNormalized(kParamMod1Src, 1 / 8.0f);   engine.setParamNormalized(kParamMod1Dst, 8 / 39.0f);  engine.setParamNormalized(kParamMod1Amt, 0.68f);
            engine.setParamNormalized(kParamMod2Src, 3 / 8.0f);   engine.setParamNormalized(kParamMod2Dst, 33 / 39.0f); engine.setParamNormalized(kParamMod2Amt, 0.78f);
            engine.setParamNormalized(kParamMod3Src, 5 / 8.0f);   engine.setParamNormalized(kParamMod3Dst, 7 / 39.0f);  engine.setParamNormalized(kParamMod3Amt, 0.66f);
            engine.setParamNormalized(kParamLfo1Rate, 0.42f);
            engine.setParamNormalized(kParamLfo2Sync, 3 / 7.0f);
            engine.setParamNormalized(kParamMaterial, 0.5f);
            engine.setParamNormalized(kParamPickupCount, 2 / 3.0f);
            engine.setParamNormalized(kParamPickupSpread, 0.4f);
            engine.setParamNormalized(kParamGapFmDepth, 0.25f);
            engine.setParamNormalized(kParamEqB1Gain, 0.5f + 4 / 24.0f);
            engine.setParamNormalized(kParamEqB2Gain, 0.5f - 3 / 24.0f);
            engine.setParamNormalized(kParamEqGain, 0.5f + 5 / 24.0f);
            engine.setParamNormalized(kParamEqB4Gain, 0.5f + 3 / 24.0f);
            engine.setParamNormalized(kParamCompAmount, 1.0f);
            engine.setParamNormalized(kParamCompThresh, 0.45f);
            engine.setParamNormalized(kParamCompKnee, 0.5f);
            engine.setParamNormalized(kParamLimOn, 1.0f);
            engine.setParamNormalized(kParamCabType, 4 / 9.0f);
            playChord(engine, {40, 47, 52, 56, 59, 64});
            auto settle = [&]() { for (int i = 0; i < 40; ++i) engine.processBlock(l, r, 512); };
            settle();
            const float tabX[8] = {52, 116, 182, 253, 309, 376, 453, 514};
            auto page = [&](int i, const char* name) {
                ed->mouseDown(tabX[i], 655, false, 20.0 + i);
                settle();
                shoot(view, name);
            };
            page(1, "editor_mod.png");
            ed->mouseDown(310, 447, false, 40.0);   // the first slot's target: opens the list
            shoot(view, "editor_mod_list.png");
            ed->keyAction(phs::gui::Key::Escape);
            page(2, "editor_macros.png");
            page(3, "editor_space.png");
            page(4, "editor_eq.png");
            page(5, "editor_dynamics.png");
            page(6, "editor_scope.png");
            // Play: just intonation loaded (the tuning arrow), a played-in instrument, a little spread
            engine.setParamNormalized(kParamCondition, 0.35f);
            engine.setParamNormalized(kParamStereoSpread, 0.4f);
            page(7, "editor_play.png");
            ed->mouseDown(416, 566, false, 45.0);
            settle();
            shoot(view, "editor_play.png");
            engine.getTuning().resetToEqual();
            ed->mouseDown(tabX[0], 655, false, 50.0);
            shoot(view, "editor_sound.png");
            engine.setParamNormalized(kParamMpeMode, 0.0f);
            for (int n : {40, 47, 52, 56, 59, 64}) engine.noteOff(n);
            for (int i = 0; i < 20; ++i) engine.processBlock(l, r, 512);
            engine.loadPreset(0, false);
        }

        // 4. Dialogs
        engine.loadPreset(0, false);
        ed->mouseDown(600, 751, false, 10.0); // preset name box
        shoot(view, "dialog_presets.png");
        ed->mouseDown(200, 198, false, 11.0); // "Save Preset..."
        for (int i = 0; i < 16; ++i) ed->keyAction(phs::gui::Key::Backspace);
        for (char ch : std::string("Late Night Mk I")) ed->keyChar(static_cast<uint32_t>(ch));
        shoot(view, "dialog_save.png");
        ed->keyAction(phs::gui::Key::Escape);
        ed->keyAction(phs::gui::Key::Escape);
        engine.setParamNormalized(phs::kParamTineBell, 0.7f);
        ed->rightMouseDown(80, 545);
        shoot(view, nullptr, {{"menu_knob.png", 16, 404, 330, 230}});
        ed->keyAction(phs::gui::Key::Escape);
        ed->mouseDown(870, 741, false, 12.0);
        shoot(view, "dialog_about.png");
        ed->keyAction(phs::gui::Key::Escape);

        // 5. The Windows / Linux renderer (same editor, software canvas)
        {
            phs::PhsSynthEngine e2;
            e2.prepare(48000.0f);
            e2.loadPreset(8, false);
            phs::gui::Editor editor(&e2, {});
            phs::gui::SoftwareCanvas canvas;
            canvas.resize(phs::kEditorWidth, phs::kEditorHeight);
            editor.draw(canvas);
            CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
            CGContextRef bc = CGBitmapContextCreate(canvas.pixels(), canvas.width(), canvas.height(), 8, canvas.width() * 4, cs,
                                                    kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little);
            CGImageRef img = CGBitmapContextCreateImage(bc);
            writePng(img, "editor_windows_linux.png");
            CGImageRelease(img);
            CGContextRelease(bc);
            CGColorSpaceRelease(cs);
        }
        phs::destroyPhsEditorView(view);
    }
    return 0;
}
