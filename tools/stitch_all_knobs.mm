#import <Cocoa/Cocoa.h>
#include <cstdio>
#include <vector>
#include <string>

void stitchKnob(const std::string& prefix, const std::string& outName) {
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

    CGContextClearRect(ctx, CGRectMake(0, 0, frameW, totalH));

    for (int i = 0; i < numFrames; ++i) {
        NSString *path = [NSString stringWithFormat:@"resources/gui/frames/%s_%02d.png", prefix.c_str(), i];
        NSImage *img = [[NSImage alloc] initWithContentsOfFile:path];
        if (!img) {
            std::fprintf(stderr, "Missing frame %s\n", [path UTF8String]);
            continue;
        }
        // In standard Cocoa coordinates with isFlipped NO, frame 0 is at bottom, frame 63 at top:
        // We want frame 0 (minimum value) at the top of the filmstrip so that in y-slice lookups,
        // frameIndex = floor(val * 63.99) * frameH can be drawn predictably.
        NSRect destRect = NSMakeRect(0, totalH - (i + 1) * frameH, frameW, frameH);
        [img drawInRect:destRect fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1.0];
        [img release];

        // Clean up individual frame file
        [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
    }

    NSString* outPath = [NSString stringWithFormat:@"resources/gui/%s", outName.c_str()];
    NSData *pngData = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    [pngData writeToFile:outPath atomically:YES];
    std::printf("Saved %s (%dx%d)\n", [outPath UTF8String], frameW, totalH);
    [rep release];
}

int main() {
    @autoreleasepool {
        std::vector<std::pair<std::string, std::string>> knobs = {
            {"knob_mechanics", "knob_mechanics_filmstrip.png"},
            {"knob_pickup", "knob_pickup_filmstrip.png"},
            {"knob_drive", "knob_drive_filmstrip.png"},
            {"knob_modulation", "knob_modulation_filmstrip.png"},
            {"knob_reverb", "knob_reverb_filmstrip.png"}
        };

        for (const auto& k : knobs) {
            stitchKnob(k.first, k.second);
        }
        std::printf("ALL 5 FILMSTRIPS STITCHED SUCCESSFULLY!\n");
    }
    return 0;
}
