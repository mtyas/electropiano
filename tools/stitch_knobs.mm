#import <Cocoa/Cocoa.h>
#include <cstdio>

int main() {
    @autoreleasepool {
        const int numFrames = 64;
        const int frameW = 96;
        const int frameH = 96;
        const int totalH = frameH * numFrames;

        NSBitmapImageRep *rep = [[NSBitmapImageRep alloc]
            initWithBitmapDataPlanes:nil
                          pixelsWide:frameW
                          pixelsHigh:totalH
                       bitsPerSample:8
                     samplesPerPixel:4
                            hasAlpha:YES
                            isPlanar:NO
                      colorSpaceName:NSDeviceRGBColorSpace
                         bytesPerRow:0
                        bitsPerPixel:0];

        NSGraphicsContext *context = [NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
        [NSGraphicsContext setCurrentContext:context];
        CGContextRef ctx = (CGContextRef)[context CGContext];

        // Clear transparent
        CGContextClearRect(ctx, CGRectMake(0, 0, frameW, totalH));

        for (int i = 0; i < numFrames; ++i) {
            NSString *path = [NSString stringWithFormat:@"resources/gui/knob_frame_%02d.png", i];
            NSImage *img = [[NSImage alloc] initWithContentsOfFile:path];
            if (!img) {
                std::fprintf(stderr, "Missing frame %s\n", [path UTF8String]);
                continue;
            }
            // In standard Cocoa coordinates with isFlipped NO, frame 0 is at bottom, frame 63 at top,
            // or frame 0 at top if drawn from top. Let's draw frame i from y = totalH - (i + 1) * frameH:
            NSRect destRect = NSMakeRect(0, totalH - (i + 1) * frameH, frameW, frameH);
            [img drawInRect:destRect fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1.0];
            [img release];

            // Remove temporary frame
            [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
        }

        NSData *pngData = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
        [pngData writeToFile:@"resources/gui/knob_filmstrip.png" atomically:YES];
        std::printf("Stitched 64 frames to resources/gui/knob_filmstrip.png (%dx%d)\n", frameW, totalH);
        [rep release];
    }
    return 0;
}
