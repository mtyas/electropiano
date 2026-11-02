#import <Cocoa/Cocoa.h>
#import <AudioUnit/AudioUnit.h>
#import <AudioToolbox/AudioToolbox.h>
#import <CoreMIDI/CoreMIDI.h>
#include "../dsp/PhsSynthEngine.hpp"
#include "../gui/PhsCocoaView.h"
#include <vector>

namespace {

struct StandaloneContext {
    phs::PhsSynthEngine engine{};
    AudioComponentInstance outputUnit = nullptr;
    MIDIClientRef midiClient = 0;
    MIDIPortRef midiInPort = 0;
};

static OSStatus coreAudioRenderCallback(void *inRefCon,
                                        AudioUnitRenderActionFlags *,
                                        const AudioTimeStamp *,
                                        UInt32,
                                        UInt32 inNumberFrames,
                                        AudioBufferList *ioData) {
    auto *ctx = static_cast<StandaloneContext *>(inRefCon);
    if (!ctx || !ioData || ioData->mNumberBuffers < 1) return noErr;

    float *outL = static_cast<float *>(ioData->mBuffers[0].mData);
    float *outR = (ioData->mNumberBuffers > 1)
                ? static_cast<float *>(ioData->mBuffers[1].mData)
                : outL;
    ctx->engine.processBlock(outL, outR, static_cast<int>(inNumberFrames));
    return noErr;
}

static void coreMidiReadProc(const MIDIPacketList *pktlist, void *readProcRefCon, void *) {
    auto *ctx = static_cast<StandaloneContext *>(readProcRefCon);
    if (!ctx || !pktlist) return;
    const MIDIPacket *packet = &pktlist->packet[0];
    for (UInt32 i = 0; i < pktlist->numPackets; ++i) {
        const UInt16 len = packet->length;
        UInt16 idx = 0;
        while (idx < len) {
            const uint8_t status = packet->data[idx];
            const uint8_t kind = status & 0xF0;
            if ((status & 0x80) == 0 || kind == 0xF0) {
                ++idx; // data byte without a status, or a system message
                continue;
            }
            // Program change and channel pressure carry one data byte, everything else two
            const UInt16 dataBytes = (kind == 0xC0 || kind == 0xD0) ? 1 : 2;
            if (idx + dataBytes >= len) break;
            const uint8_t d1 = packet->data[idx + 1] & 0x7F;
            const uint8_t d2 = dataBytes == 2 ? (packet->data[idx + 2] & 0x7F) : 0;
            ctx->engine.pushGuiMidiEvent(status, d1, d2);
            idx += 1 + dataBytes;
        }
        packet = MIDIPacketNext(packet);
    }
}

} // namespace

@interface PhsAppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate> {
    NSWindow *window_;
    NSView *editorView_;
    StandaloneContext ctx_;
}
@end

@implementation PhsAppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    (void)notification;

    // 1. Setup CoreAudio Default Output AudioUnit
    AudioComponentDescription desc{};
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_DefaultOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;

    AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
    if (comp && AudioComponentInstanceNew(comp, &ctx_.outputUnit) == noErr) {
        Float64 hwSampleRate = 48000.0;
        UInt32 srSize = sizeof(hwSampleRate);
        AudioUnitGetProperty(ctx_.outputUnit,
                             kAudioUnitProperty_SampleRate,
                             kAudioUnitScope_Output,
                             0,
                             &hwSampleRate,
                             &srSize);
        if (hwSampleRate <= 0.0) hwSampleRate = 48000.0;
        ctx_.engine.prepare(static_cast<float>(hwSampleRate));

        AudioStreamBasicDescription streamDesc{};
        streamDesc.mSampleRate = hwSampleRate;
        streamDesc.mFormatID = kAudioFormatLinearPCM;
        streamDesc.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
        streamDesc.mBytesPerPacket = sizeof(float);
        streamDesc.mFramesPerPacket = 1;
        streamDesc.mBytesPerFrame = sizeof(float);
        streamDesc.mChannelsPerFrame = 2;
        streamDesc.mBitsPerChannel = 32;

        AudioUnitSetProperty(ctx_.outputUnit,
                             kAudioUnitProperty_StreamFormat,
                             kAudioUnitScope_Input,
                             0,
                             &streamDesc,
                             sizeof(streamDesc));

        AURenderCallbackStruct cb{};
        cb.inputProc = coreAudioRenderCallback;
        cb.inputProcRefCon = &ctx_;
        AudioUnitSetProperty(ctx_.outputUnit,
                             kAudioUnitProperty_SetRenderCallback,
                             kAudioUnitScope_Input,
                             0,
                             &cb,
                             sizeof(cb));

        AudioUnitInitialize(ctx_.outputUnit);
        AudioOutputUnitStart(ctx_.outputUnit);
    }

    // 2. Setup CoreMIDI Input (connect to all available hardware MIDI sources)
    if (MIDIClientCreate(CFSTR("electropiano MIDI"), nullptr, nullptr, &ctx_.midiClient) == noErr) {
        if (MIDIInputPortCreate(ctx_.midiClient, CFSTR("Input"), coreMidiReadProc, &ctx_, &ctx_.midiInPort) == noErr) {
            const ItemCount numSources = MIDIGetNumberOfSources();
            for (ItemCount i = 0; i < numSources; ++i) {
                MIDIEndpointRef src = MIDIGetSource(i);
                if (src) MIDIPortConnectSource(ctx_.midiInPort, src, nullptr);
            }
        }
    }

    // 3. Create Main Window & Embed Cocoa Editor View
    NSRect winRect = NSMakeRect(0, 0, phs::kEditorWidth, phs::kEditorHeight);
    NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable;
    window_ = [[NSWindow alloc] initWithContentRect:winRect
                                          styleMask:style
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    [window_ setTitle:@"electropiano by mtyas — Port-Hamiltonian Rhodes & Wurlitzer"];
    [window_ center];
    [window_ setDelegate:self];

    editorView_ = phs::createPhsEditorView(&ctx_.engine, nullptr);
    [[window_ contentView] addSubview:editorView_];
    [window_ makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    (void)sender;
    return YES;
}

- (void)applicationWillTerminate:(NSNotification *)notification {
    (void)notification;
    if (ctx_.outputUnit) {
        AudioOutputUnitStop(ctx_.outputUnit);
        AudioUnitUninitialize(ctx_.outputUnit);
        AudioComponentInstanceDispose(ctx_.outputUnit);
        ctx_.outputUnit = nullptr;
    }
    if (editorView_) {
        phs::destroyPhsEditorView(editorView_);
        editorView_ = nil;
    }
}

@end

int main(int argc, const char *argv[]) {
    (void)argc;
    (void)argv;
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    NSApplication *app = [NSApplication sharedApplication];
    [app setActivationPolicy:NSApplicationActivationPolicyRegular];

    PhsAppDelegate *delegate = [[PhsAppDelegate alloc] init];
    [app setDelegate:delegate];
    [app run];

    [delegate release];
    [pool drain];
    return 0;
}
