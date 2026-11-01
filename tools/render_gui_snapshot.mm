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
        [pngData writeToFile:@"renders/gui_snapshot.png" atomically:YES];
        std::printf("Saved renders/gui_snapshot.png (%dx%d)\n", (int)frame.size.width, (int)frame.size.height);

        phs::destroyPhsEditorView(view);
        [rep release];
    }
    return 0;
}
