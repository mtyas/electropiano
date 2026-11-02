// Renders the shared editor through the portable software canvas (the Windows / Linux
// renderer) into PNG files, so their look can be checked on any machine.
//
// clang++ -std=c++17 -ObjC++ -O2 tools/render_gui_software.mm src/gui/PhsEditor.cpp \
//     -framework Cocoa -o build/render_gui_software && build/render_gui_software <outdir>
#import <Cocoa/Cocoa.h>
#include "../src/dsp/PhsSynthEngine.hpp"
#include "../src/gui/PhsEditor.hpp"
#include "../src/gui/PhsSoftwareCanvas.hpp"
#include <string>

static void savePng(const phs::gui::SoftwareCanvas& c, const std::string& path) {
    NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nil pixelsWide:c.width() pixelsHigh:c.height()
                                                                bitsPerSample:8 samplesPerPixel:3 hasAlpha:NO isPlanar:NO
                                                               colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:c.width() * 4 bitsPerPixel:32];
    unsigned char* dst = [rep bitmapData];
    for (int i = 0; i < c.width() * c.height(); ++i) {
        const uint32_t p = c.pixels()[i];
        dst[i * 4 + 0] = (p >> 16) & 0xFF;
        dst[i * 4 + 1] = (p >> 8) & 0xFF;
        dst[i * 4 + 2] = p & 0xFF;
        dst[i * 4 + 3] = 255;
    }
    [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:[NSString stringWithUTF8String:path.c_str()] atomically:YES];
    [rep release];
}

int main(int argc, char** argv) {
    @autoreleasepool {
        const std::string dir = argc > 1 ? argv[1] : "renders";
        phs::PhsSynthEngine engine;
        engine.prepare(48000.0f);
        phs::gui::Editor editor(&engine, {});
        phs::gui::SoftwareCanvas canvas;
        canvas.resize(phs::kEditorWidth, phs::kEditorHeight);

        auto shot = [&](const char* name) {
            canvas.resize(phs::kEditorWidth, phs::kEditorHeight);
            editor.draw(canvas);
            savePng(canvas, dir + "/" + name);
            std::printf("wrote %s/%s\n", dir.c_str(), name);
        };
        shot("software_main.png");
        engine.loadPreset(8, false);
        shot("software_200a.png");
        editor.mouseDown(600, 715, false, 0.0); // open the preset browser
        shot("software_browser.png");
        editor.keyAction(phs::gui::Key::Escape);
        editor.rightMouseDown(80, 560);         // knob context menu
        shot("software_menu.png");
        editor.keyAction(phs::gui::Key::Escape);
        editor.mouseDown(870, 705, false, 1.0); // about
        shot("software_about.png");
    }
    return 0;
}
