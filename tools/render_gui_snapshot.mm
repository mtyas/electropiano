#import <Cocoa/Cocoa.h>
#include "src/dsp/PhsSynthEngine.hpp"
#include "src/gui/PhsCocoaView.h"

int main() {
    @autoreleasepool {
        phs::PhsSynthEngine engine;
        engine.prepare(48000.0f);

        NSView *view = phs::createPhsEditorView(&engine, nullptr);
        NSRect frame = NSMakeRect(0, 0, phs::kEditorWidth, phs::kEditorHeight);
        [view setFrame:frame];

        auto saveImage = [&](NSString* filename) {
            NSBitmapImageRep *rep = [[NSBitmapImageRep alloc]
                initWithBitmapDataPlanes:nil
                              pixelsWide:static_cast<NSInteger>(frame.size.width)
                              pixelsHigh:static_cast<NSInteger>(frame.size.height)
                           bitsPerSample:8
                         samplesPerPixel:4
                                hasAlpha:YES
                                isPlanar:NO
                          colorSpaceName:NSDeviceRGBColorSpace
                             bytesPerRow:0
                            bitsPerPixel:0];

            NSGraphicsContext *context = [NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
            [NSGraphicsContext setCurrentContext:context];

            [view displayRectIgnoringOpacity:frame inContext:context];

            NSData *pngData = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
            [pngData writeToFile:filename atomically:YES];
            std::printf("Saved %s (%dx%d)\n", [filename UTF8String], (int)frame.size.width, (int)frame.size.height);
            [rep release];
        };

        // 1. Main View Snapshot
        saveImage(@"renders/gui_snapshot.png");

        // 2. Open About dialog via Key/Value or direct call
        // In PhsCocoaView, aboutButtonRect_ is at (818, 538)
        // Let's create an event or use setValue:forKey:
        phs::editorOfPhsView(view)->mouseDown(870, 705, false, 1.0); // About button
        saveImage(@"renders/gui_about_snapshot.png");

        phs::destroyPhsEditorView(view);
    }
    return 0;
}
