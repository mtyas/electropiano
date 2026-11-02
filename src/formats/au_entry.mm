#import <Cocoa/Cocoa.h>
#import <AudioUnit/AudioUnit.h>
#import <AudioUnit/AUCocoaUIView.h>
#import <AudioToolbox/AudioToolbox.h>
#include "../dsp/PhsSynthEngine.hpp"
#include "../gui/PhsCocoaView.h"
#include <vector>
#include <cmath>
#include <cstring>

// Custom property ID to pass the C++ engine pointer to the Cocoa View Factory in the same process
static const AudioUnitPropertyID kPhsCustomProperty_EnginePtr = 65500;

@interface PhsAUCocoaViewFactory : NSObject <AUCocoaUIBase>
- (unsigned)interfaceVersion;
- (NSView *)uiViewForAudioUnit:(AudioUnit)inAudioUnit withSize:(NSSize)inPreferredSize;
@end

@implementation PhsAUCocoaViewFactory

- (unsigned)interfaceVersion {
    return 0;
}

- (NSView *)uiViewForAudioUnit:(AudioUnit)inAudioUnit withSize:(NSSize)inPreferredSize {
    (void)inPreferredSize;
    phs::PhsSynthEngine *enginePtr = nullptr;
    UInt32 dataSize = sizeof(enginePtr);
    OSStatus err = AudioUnitGetProperty(inAudioUnit,
                                        kPhsCustomProperty_EnginePtr,
                                        kAudioUnitScope_Global,
                                        0,
                                        &enginePtr,
                                        &dataSize);
    if (err != noErr || !enginePtr) {
        return [[[NSView alloc] initWithFrame:NSMakeRect(0, 0, phs::kEditorWidth, phs::kEditorHeight)] autorelease];
    }

    NSView *view = phs::createPhsEditorView(enginePtr, [inAudioUnit](uint32_t paramId, float normVal) {
        AudioUnitSetParameter(inAudioUnit,
                              static_cast<AudioUnitParameterID>(paramId),
                              kAudioUnitScope_Global,
                              0,
                              normVal,
                              0);
        AudioUnitParameter param{};
        param.mAudioUnit = inAudioUnit;
        param.mParameterID = static_cast<AudioUnitParameterID>(paramId);
        param.mScope = kAudioUnitScope_Global;
        param.mElement = 0;
        AUParameterListenerNotify(nullptr, nullptr, &param);
    });
    return [view autorelease];
}

@end

namespace {

static const UInt32 kAuType         = 'aumu';
static const UInt32 kAuSubType      = 'PhsE';
static const UInt32 kAuManufacturer = 'PyPH';
static const UInt32 kAuVersion      = (PHS_VERSION_MAJOR << 16) | (PHS_VERSION_MINOR << 8);

struct PropListener {
    AudioUnitPropertyID id;
    AudioUnitPropertyListenerProc proc;
    void *userData;
};

struct RenderNotify {
    AURenderCallback proc;
    void *userData;
};

struct PhsAudioUnit {
    AudioComponentPlugInInterface pluginInterface{};
    AudioComponentInstance instance = nullptr;

    phs::PhsSynthEngine engine{};
    bool initialized = false;
    Float64 sampleRate = 48000.0;
    UInt32 channels = 2;
    UInt32 maxFramesPerSlice = 4096;
    AUPreset currentPreset{0, CFSTR("1973 Suitcase Rhodes Mk I")};
    UInt32 nextNoteInstanceId = 100;

    std::vector<float> scratchL;
    std::vector<float> scratchR;

    // MIDI arrives before each render call with its offset into the coming slice; it is
    // applied at that sample instead of at the start of the slice
    struct QueuedMidi { UInt32 offset; uint8_t status, data1, data2; };
    static constexpr int kMidiQueueSize = 2048;
    QueuedMidi midiQueue[kMidiQueueSize]{};
    int midiCount = 0;
    std::vector<PropListener> propListeners;
    std::vector<RenderNotify> renderNotifies;

    PhsAudioUnit() {
        scratchL.resize(4096, 0.0f);
        scratchR.resize(4096, 0.0f);
    }

    void fillStreamDesc(AudioStreamBasicDescription &desc) const {
        std::memset(&desc, 0, sizeof(desc));
        desc.mSampleRate = sampleRate;
        desc.mFormatID = kAudioFormatLinearPCM;
        desc.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
        desc.mBytesPerPacket = sizeof(Float32);
        desc.mFramesPerPacket = 1;
        desc.mBytesPerFrame = sizeof(Float32);
        desc.mChannelsPerFrame = channels;
        desc.mBitsPerChannel = 32;
    }

    void notifyPropertyChanged(AudioUnitPropertyID propId, AudioUnitScope scope, AudioUnitElement elem) {
        for (const auto &l : propListeners) {
            if (l.id == propId && l.proc) {
                l.proc(l.userData, instance, propId, scope, elem);
            }
        }
    }
};

// --- AudioUnit Method Implementations ---

static OSStatus auInitialize(void *selfPtr) {
    auto *self = static_cast<PhsAudioUnit *>(selfPtr);
    self->engine.prepare(static_cast<float>(self->sampleRate));
    self->scratchL.assign(self->maxFramesPerSlice, 0.0f);
    self->scratchR.assign(self->maxFramesPerSlice, 0.0f);
    self->initialized = true;
    return noErr;
}

static OSStatus auUninitialize(void *selfPtr) {
    auto *self = static_cast<PhsAudioUnit *>(selfPtr);
    self->engine.reset();
    self->initialized = false;
    return noErr;
}

static OSStatus auGetPropertyInfo(void *selfPtr,
                                  AudioUnitPropertyID inID,
                                  AudioUnitScope inScope,
                                  AudioUnitElement inElement,
                                  UInt32 *outDataSize,
                                  Boolean *outWritable) {
    (void)selfPtr;
    UInt32 size = 0;
    Boolean writable = false;

    switch (inID) {
        case kAudioUnitProperty_ElementCount:
            size = sizeof(UInt32);
            writable = false;
            break;

        case kAudioUnitProperty_StreamFormat:
            if (inScope != kAudioUnitScope_Output || inElement != 0) return kAudioUnitErr_InvalidProperty;
            size = sizeof(AudioStreamBasicDescription);
            writable = true;
            break;

        case kAudioUnitProperty_SampleRate:
            if (inScope != kAudioUnitScope_Output && inScope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidProperty;
            size = sizeof(Float64);
            writable = true;
            break;

        case kAudioUnitProperty_MaximumFramesPerSlice:
            if (inScope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidProperty;
            size = sizeof(UInt32);
            writable = true;
            break;

        case kAudioUnitProperty_SupportedNumChannels:
            if (inScope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidProperty;
            size = sizeof(AUChannelInfo) * 2;
            writable = false;
            break;

        case kAudioUnitProperty_ParameterList:
            if (inScope == kAudioUnitScope_Global) {
                size = sizeof(AudioUnitParameterID) * phs::kNumParams;
            } else {
                size = 0;
            }
            writable = false;
            break;

        case kAudioUnitProperty_ParameterInfo:
            if (inScope != kAudioUnitScope_Global || inElement >= phs::kNumParams) return kAudioUnitErr_InvalidParameter;
            size = sizeof(AudioUnitParameterInfo);
            writable = false;
            break;

        case kAudioUnitProperty_Latency:
        case kAudioUnitProperty_TailTime:
            if (inScope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidProperty;
            size = sizeof(Float64);
            writable = false;
            break;

        case kAudioUnitProperty_CPULoad:
            if (inScope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidProperty;
            size = sizeof(Float32);
            writable = false;
            break;

        case kAudioUnitProperty_FactoryPresets:
            if (inScope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidProperty;
            size = sizeof(CFArrayRef);
            writable = false;
            break;

        case kAudioUnitProperty_PresentPreset:
        case kAudioUnitProperty_CurrentPreset:
            if (inScope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidProperty;
            size = sizeof(AUPreset);
            writable = true;
            break;

        case kAudioUnitProperty_ClassInfo:
            if (inScope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidProperty;
            size = sizeof(CFPropertyListRef);
            writable = true;
            break;

        case kAudioUnitProperty_CocoaUI:
            if (inScope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidProperty;
            size = sizeof(AudioUnitCocoaViewInfo);
            writable = false;
            break;

        case kPhsCustomProperty_EnginePtr:
            size = sizeof(phs::PhsSynthEngine *);
            writable = false;
            break;

        default:
            return kAudioUnitErr_InvalidProperty;
    }

    if (outDataSize) *outDataSize = size;
    if (outWritable) *outWritable = writable;
    return noErr;
}

static OSStatus auGetProperty(void *selfPtr,
                              AudioUnitPropertyID inID,
                              AudioUnitScope inScope,
                              AudioUnitElement inElement,
                              void *outData,
                              UInt32 *ioDataSize) {
    auto *self = static_cast<PhsAudioUnit *>(selfPtr);
    if (!outData || !ioDataSize) return kAudio_ParamError;

    switch (inID) {
        case kAudioUnitProperty_ElementCount: {
            if (*ioDataSize < sizeof(UInt32)) return kAudio_ParamError;
            UInt32 count = 0;
            if (inScope == kAudioUnitScope_Global) count = 1;
            else if (inScope == kAudioUnitScope_Input) count = 0;
            else if (inScope == kAudioUnitScope_Output) count = 1;
            else if (inScope == kAudioUnitScope_Group) count = 16;
            else if (inScope == kAudioUnitScope_Note) count = 0;
            else return kAudioUnitErr_InvalidScope;
            *static_cast<UInt32 *>(outData) = count;
            *ioDataSize = sizeof(UInt32);
            return noErr;
        }

        case kAudioUnitProperty_StreamFormat: {
            if (inScope != kAudioUnitScope_Output || inElement != 0) return kAudioUnitErr_InvalidProperty;
            if (*ioDataSize < sizeof(AudioStreamBasicDescription)) return kAudio_ParamError;
            self->fillStreamDesc(*static_cast<AudioStreamBasicDescription *>(outData));
            *ioDataSize = sizeof(AudioStreamBasicDescription);
            return noErr;
        }

        case kAudioUnitProperty_SampleRate: {
            if (inScope != kAudioUnitScope_Output && inScope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidProperty;
            if (inScope == kAudioUnitScope_Output && inElement != 0) return kAudioUnitErr_InvalidElement;
            if (*ioDataSize < sizeof(Float64)) return kAudio_ParamError;
            *static_cast<Float64 *>(outData) = self->sampleRate;
            *ioDataSize = sizeof(Float64);
            return noErr;
        }

        case kAudioUnitProperty_MaximumFramesPerSlice: {
            if (inScope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidProperty;
            if (*ioDataSize < sizeof(UInt32)) return kAudio_ParamError;
            *static_cast<UInt32 *>(outData) = self->maxFramesPerSlice;
            *ioDataSize = sizeof(UInt32);
            return noErr;
        }

        case kAudioUnitProperty_SupportedNumChannels: {
            if (inScope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidProperty;
            if (*ioDataSize < sizeof(AUChannelInfo) * 2) return kAudio_ParamError;
            auto *info = static_cast<AUChannelInfo *>(outData);
            info[0].inChannels = 0;
            info[0].outChannels = 2;
            info[1].inChannels = 0;
            info[1].outChannels = 1;
            *ioDataSize = sizeof(AUChannelInfo) * 2;
            return noErr;
        }

        case kAudioUnitProperty_ParameterList: {
            if (inScope != kAudioUnitScope_Global) {
                *ioDataSize = 0;
                return noErr;
            }
            const UInt32 needed = sizeof(AudioUnitParameterID) * phs::kNumParams;
            if (*ioDataSize < needed) return kAudio_ParamError;
            auto *ids = static_cast<AudioUnitParameterID *>(outData);
            for (UInt32 i = 0; i < phs::kNumParams; ++i) {
                ids[i] = i;
            }
            *ioDataSize = needed;
            return noErr;
        }

        case kAudioUnitProperty_ParameterInfo: {
            if (inScope != kAudioUnitScope_Global || inElement >= phs::kNumParams) return kAudioUnitErr_InvalidParameter;
            if (*ioDataSize < sizeof(AudioUnitParameterInfo)) return kAudio_ParamError;
            const auto &meta = phs::kParamMetadata[inElement];
            auto *pInfo = static_cast<AudioUnitParameterInfo *>(outData);
            std::memset(pInfo, 0, sizeof(*pInfo));
            std::snprintf(pInfo->name, sizeof(pInfo->name), "%s", meta.name);
            pInfo->cfNameString = CFStringCreateWithCString(kCFAllocatorDefault, meta.name, kCFStringEncodingUTF8);
            pInfo->unit = kAudioUnitParameterUnit_Generic; // normalised 0..1, choices included
            pInfo->minValue = 0.0f;
            pInfo->maxValue = 1.0f;
            pInfo->defaultValue = meta.defaultValue;
            pInfo->flags = kAudioUnitParameterFlag_IsWritable
                         | kAudioUnitParameterFlag_IsReadable
                         | kAudioUnitParameterFlag_HasCFNameString;
            *ioDataSize = sizeof(AudioUnitParameterInfo);
            return noErr;
        }

        case kAudioUnitProperty_Latency: {
            if (inScope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidProperty;
            *static_cast<Float64 *>(outData) = 0.0;
            *ioDataSize = sizeof(Float64);
            return noErr;
        }

        case kAudioUnitProperty_TailTime: {
            if (inScope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidProperty;
            *static_cast<Float64 *>(outData) = 1.5;
            *ioDataSize = sizeof(Float64);
            return noErr;
        }

        case kAudioUnitProperty_CPULoad: {
            if (inScope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidProperty;
            *static_cast<Float32 *>(outData) = 0.0f;
            *ioDataSize = sizeof(Float32);
            return noErr;
        }

        case kAudioUnitProperty_FactoryPresets: {
            if (inScope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidProperty;
            static const int sCount = static_cast<int>(phs::kExpandedFactoryPresets.size());
            static std::vector<AUPreset> sPresets(sCount);
            static CFArrayRef sPresetArray = nullptr;
            if (!sPresetArray) {
                std::vector<const void *> ptrs(sCount);
                for (int i = 0; i < sCount; ++i) {
                    sPresets[i].presetNumber = i;
                    sPresets[i].presetName = CFStringCreateWithCString(kCFAllocatorDefault,
                                                                       phs::kExpandedFactoryPresets[i].name.c_str(),
                                                                       kCFStringEncodingUTF8);
                    ptrs[i] = &sPresets[i];
                }
                sPresetArray = CFArrayCreate(kCFAllocatorDefault, ptrs.data(), sCount, nullptr);
            }
            CFRetain(sPresetArray);
            *static_cast<CFArrayRef *>(outData) = sPresetArray;
            *ioDataSize = sizeof(CFArrayRef);
            return noErr;
        }

        case kAudioUnitProperty_PresentPreset:
        case kAudioUnitProperty_CurrentPreset: {
            if (inScope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidProperty;
            const int idx = self->engine.getCurrentPreset();
            self->currentPreset.presetNumber = idx;
            const auto *preset = self->engine.getPresetManager().getPreset(idx);
            self->currentPreset.presetName = CFStringCreateWithCString(kCFAllocatorDefault,
                                                                       preset ? preset->name.c_str() : "Custom",
                                                                       kCFStringEncodingUTF8);
            if (inID == kAudioUnitProperty_PresentPreset && self->currentPreset.presetName) {
                CFRetain(self->currentPreset.presetName);
            }
            *static_cast<AUPreset *>(outData) = self->currentPreset;
            *ioDataSize = sizeof(AUPreset);
            return noErr;
        }

        case kAudioUnitProperty_ClassInfo: {
            if (inScope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidProperty;
            CFMutableDictionaryRef dict = CFDictionaryCreateMutable(kCFAllocatorDefault,
                                                                    0,
                                                                    &kCFTypeDictionaryKeyCallBacks,
                                                                    &kCFTypeDictionaryValueCallBacks);
            SInt32 ver = kAuVersion;
            SInt32 type = kAuType;
            SInt32 sub = kAuSubType;
            SInt32 manu = kAuManufacturer;
            CFNumberRef nVer = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &ver);
            CFNumberRef nType = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &type);
            CFNumberRef nSub = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &sub);
            CFNumberRef nManu = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &manu);

            const std::vector<uint8_t> st = self->engine.saveState();
            CFDataRef stateData = CFDataCreate(kCFAllocatorDefault, st.data(), static_cast<CFIndex>(st.size()));

            CFDictionarySetValue(dict, CFSTR("version"), nVer);
            CFDictionarySetValue(dict, CFSTR("type"), nType);
            CFDictionarySetValue(dict, CFSTR("subtype"), nSub);
            CFDictionarySetValue(dict, CFSTR("manufacturer"), nManu);
            CFDictionarySetValue(dict, CFSTR("name"), CFSTR("electropiano"));
            CFDictionarySetValue(dict, CFSTR("data"), stateData);

            CFRelease(nVer);
            CFRelease(nType);
            CFRelease(nSub);
            CFRelease(nManu);
            CFRelease(stateData);

            *static_cast<CFPropertyListRef *>(outData) = dict;
            *ioDataSize = sizeof(CFPropertyListRef);
            return noErr;
        }

        case kAudioUnitProperty_CocoaUI: {
            if (inScope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidProperty;
            if (*ioDataSize < sizeof(AudioUnitCocoaViewInfo)) return kAudio_ParamError;
            auto *viewInfo = static_cast<AudioUnitCocoaViewInfo *>(outData);
            NSBundle *bundle = [NSBundle bundleForClass:[PhsAUCocoaViewFactory class]];
            NSURL *bundleURL = [bundle bundleURL];
            viewInfo->mCocoaAUViewBundleLocation = (CFURLRef)[bundleURL retain];
            viewInfo->mCocoaAUViewClass[0] = CFStringCreateWithCString(kCFAllocatorDefault,
                                                                       "PhsAUCocoaViewFactory",
                                                                       kCFStringEncodingUTF8);
            *ioDataSize = sizeof(AudioUnitCocoaViewInfo);
            return noErr;
        }

        case kPhsCustomProperty_EnginePtr: {
            *static_cast<phs::PhsSynthEngine **>(outData) = &self->engine;
            *ioDataSize = sizeof(phs::PhsSynthEngine *);
            return noErr;
        }

        default:
            return kAudioUnitErr_InvalidProperty;
    }
}

static OSStatus auSetProperty(void *selfPtr,
                              AudioUnitPropertyID inID,
                              AudioUnitScope inScope,
                              AudioUnitElement inElement,
                              const void *inData,
                              UInt32 inDataSize) {
    auto *self = static_cast<PhsAudioUnit *>(selfPtr);
    if (!inData) return noErr;

    switch (inID) {
        case kAudioUnitProperty_StreamFormat: {
            if (inScope != kAudioUnitScope_Output || inElement != 0) return kAudioUnitErr_InvalidProperty;
            if (self->initialized) return kAudioUnitErr_Initialized;
            if (inDataSize < sizeof(AudioStreamBasicDescription)) return kAudio_ParamError;
            const auto &desc = *static_cast<const AudioStreamBasicDescription *>(inData);

            if (desc.mFormatID != kAudioFormatLinearPCM) return kAudioUnitErr_FormatNotSupported;
            if (desc.mChannelsPerFrame != 1 && desc.mChannelsPerFrame != 2) return kAudioUnitErr_FormatNotSupported;
            if (desc.mSampleRate <= 0.0) return kAudioUnitErr_FormatNotSupported;
            const UInt32 reqFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
            if ((desc.mFormatFlags & reqFlags) != reqFlags || desc.mBitsPerChannel != 32) {
                return kAudioUnitErr_FormatNotSupported;
            }

            self->sampleRate = desc.mSampleRate;
            self->channels = desc.mChannelsPerFrame;
            self->notifyPropertyChanged(kAudioUnitProperty_StreamFormat, inScope, inElement);
            return noErr;
        }

        case kAudioUnitProperty_SampleRate: {
            if (self->initialized) return kAudioUnitErr_Initialized;
            if (inScope != kAudioUnitScope_Output && inScope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidProperty;
            if (inDataSize < sizeof(Float64)) return kAudio_ParamError;
            const Float64 sr = *static_cast<const Float64 *>(inData);
            if (sr <= 0.0) return kAudioUnitErr_FormatNotSupported;
            self->sampleRate = sr;
            self->notifyPropertyChanged(kAudioUnitProperty_SampleRate, inScope, inElement);
            return noErr;
        }

        case kAudioUnitProperty_MaximumFramesPerSlice: {
            if (inScope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidProperty;
            if (inDataSize < sizeof(UInt32)) return kAudio_ParamError;
            self->maxFramesPerSlice = std::max<UInt32>(64, *static_cast<const UInt32 *>(inData));
            self->scratchL.resize(self->maxFramesPerSlice, 0.0f);
            self->scratchR.resize(self->maxFramesPerSlice, 0.0f);
            self->notifyPropertyChanged(kAudioUnitProperty_MaximumFramesPerSlice, inScope, inElement);
            return noErr;
        }

        case kAudioUnitProperty_PresentPreset:
        case kAudioUnitProperty_CurrentPreset: {
            if (inScope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidProperty;
            if (inDataSize < sizeof(AUPreset)) return kAudio_ParamError;
            const auto &pr = *static_cast<const AUPreset *>(inData);
            if (pr.presetNumber >= 0 && pr.presetNumber < self->engine.getPresetManager().getPresetCount()) {
                self->engine.loadPreset(pr.presetNumber);
            }
            self->notifyPropertyChanged(kAudioUnitProperty_PresentPreset, inScope, inElement);
            self->notifyPropertyChanged(kAudioUnitProperty_ParameterList, inScope, inElement);
            return noErr;
        }

        case kAudioUnitProperty_ClassInfo: {
            if (inScope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidProperty;
            if (inDataSize < sizeof(CFPropertyListRef)) return kAudio_ParamError;
            CFPropertyListRef plist = *static_cast<const CFPropertyListRef *>(inData);
            if (!plist || CFGetTypeID(plist) != CFDictionaryGetTypeID()) return kAudio_ParamError;
            CFDictionaryRef dict = static_cast<CFDictionaryRef>(plist);
            CFDataRef dataRef = static_cast<CFDataRef>(CFDictionaryGetValue(dict, CFSTR("data")));
            if (dataRef && CFGetTypeID(dataRef) == CFDataGetTypeID()) {
                self->engine.setState(CFDataGetBytePtr(dataRef), static_cast<size_t>(CFDataGetLength(dataRef)));
            }
            return noErr;
        }

        default:
            return kAudioUnitErr_InvalidProperty;
    }
}

static OSStatus auAddPropertyListener(void *selfPtr,
                                      AudioUnitPropertyID inID,
                                      AudioUnitPropertyListenerProc inProc,
                                      void *inUserData) {
    auto *self = static_cast<PhsAudioUnit *>(selfPtr);
    self->propListeners.push_back({inID, inProc, inUserData});
    return noErr;
}

static OSStatus auRemovePropertyListener(void *selfPtr,
                                         AudioUnitPropertyID inID,
                                         AudioUnitPropertyListenerProc inProc) {
    auto *self = static_cast<PhsAudioUnit *>(selfPtr);
    auto &v = self->propListeners;
    v.erase(std::remove_if(v.begin(), v.end(), [&](const PropListener &l) {
        return l.id == inID && l.proc == inProc;
    }), v.end());
    return noErr;
}

static OSStatus auRemovePropertyListenerWithUserData(void *selfPtr,
                                                     AudioUnitPropertyID inID,
                                                     AudioUnitPropertyListenerProc inProc,
                                                     void *inUserData) {
    auto *self = static_cast<PhsAudioUnit *>(selfPtr);
    auto &v = self->propListeners;
    v.erase(std::remove_if(v.begin(), v.end(), [&](const PropListener &l) {
        return l.id == inID && l.proc == inProc && l.userData == inUserData;
    }), v.end());
    return noErr;
}

static OSStatus auAddRenderNotify(void *selfPtr, AURenderCallback inProc, void *inUserData) {
    auto *self = static_cast<PhsAudioUnit *>(selfPtr);
    self->renderNotifies.push_back({inProc, inUserData});
    return noErr;
}

static OSStatus auRemoveRenderNotify(void *selfPtr, AURenderCallback inProc, void *inUserData) {
    auto *self = static_cast<PhsAudioUnit *>(selfPtr);
    auto &v = self->renderNotifies;
    v.erase(std::remove_if(v.begin(), v.end(), [&](const RenderNotify &r) {
        return r.proc == inProc && r.userData == inUserData;
    }), v.end());
    return noErr;
}

static OSStatus auGetParameter(void *selfPtr,
                               AudioUnitParameterID inID,
                               AudioUnitScope inScope,
                               AudioUnitElement inElement,
                               AudioUnitParameterValue *outValue) {
    (void)inElement;
    if (inScope != kAudioUnitScope_Global || inID >= phs::kNumParams || !outValue) {
        return kAudioUnitErr_InvalidParameter;
    }
    auto *self = static_cast<PhsAudioUnit *>(selfPtr);
    *outValue = self->engine.getParamNormalized(inID);
    return noErr;
}

static OSStatus auSetParameter(void *selfPtr,
                               AudioUnitParameterID inID,
                               AudioUnitScope inScope,
                               AudioUnitElement inElement,
                               AudioUnitParameterValue inValue,
                               UInt32 inBufferOffsetInFrames) {
    (void)inElement;
    (void)inBufferOffsetInFrames;
    if (inScope != kAudioUnitScope_Global || inID >= phs::kNumParams) {
        return kAudioUnitErr_InvalidParameter;
    }
    auto *self = static_cast<PhsAudioUnit *>(selfPtr);
    self->engine.setParamNormalized(inID, inValue);
    return noErr;
}

static OSStatus auScheduleParameters(void *selfPtr,
                                     const AudioUnitParameterEvent *inParameterEvent,
                                     UInt32 inNumParamEvents) {
    if (!inParameterEvent) return kAudio_ParamError;
    auto *self = static_cast<PhsAudioUnit *>(selfPtr);
    for (UInt32 i = 0; i < inNumParamEvents; ++i) {
        const auto &ev = inParameterEvent[i];
        if (ev.scope == kAudioUnitScope_Global && ev.parameter < phs::kNumParams) {
            const float val = (ev.eventType == kParameterEvent_Immediate)
                ? ev.eventValues.immediate.value
                : ev.eventValues.ramp.endValue;
            self->engine.setParamNormalized(ev.parameter, val);
        }
    }
    return noErr;
}

static OSStatus auReset(void *selfPtr, AudioUnitScope, AudioUnitElement) {
    auto *self = static_cast<PhsAudioUnit *>(selfPtr);
    self->engine.reset();
    self->midiCount = 0;
    return noErr;
}

static OSStatus auRender(void *selfPtr,
                         AudioUnitRenderActionFlags *ioActionFlags,
                         const AudioTimeStamp *inTimeStamp,
                         UInt32 inOutputBusNumber,
                         UInt32 inNumberFrames,
                         AudioBufferList *ioData) {
    auto *self = static_cast<PhsAudioUnit *>(selfPtr);
    if (!self->initialized) return kAudioUnitErr_Uninitialized;
    if (inOutputBusNumber != 0) return kAudioUnitErr_InvalidElement;
    if (inNumberFrames > self->maxFramesPerSlice) return kAudioUnitErr_TooManyFramesToProcess;
    if (!ioData || ioData->mNumberBuffers < 1) return kAudio_ParamError;

    AudioUnitRenderActionFlags localFlags = ioActionFlags ? *ioActionFlags : 0;

    // Pre-render notify
    AudioUnitRenderActionFlags preFlags = localFlags | kAudioUnitRenderAction_PreRender;
    for (const auto &rn : self->renderNotifies) {
        if (rn.proc) rn.proc(rn.userData, &preFlags, inTimeStamp, inOutputBusNumber, inNumberFrames, ioData);
    }

    float *bufL = static_cast<float *>(ioData->mBuffers[0].mData);
    float *bufR = (ioData->mNumberBuffers > 1 && ioData->mBuffers[1].mData)
                ? static_cast<float *>(ioData->mBuffers[1].mData)
                : nullptr;

    if (!bufL) {
        bufL = self->scratchL.data();
        ioData->mBuffers[0].mData = bufL;
    }
    if (ioData->mNumberBuffers > 1 && !bufR) {
        bufR = self->scratchR.data();
        ioData->mBuffers[1].mData = bufR;
    }

    float *outR = bufR ? bufR : self->scratchR.data();
    {
        int idx = 0;
        UInt32 cur = 0;
        while (cur < inNumberFrames) {
            while (idx < self->midiCount && self->midiQueue[idx].offset <= cur) {
                const auto &m = self->midiQueue[idx++];
                self->engine.handleMidiEvent(m.status, m.data1, m.data2);
            }
            const UInt32 next = idx < self->midiCount ? std::min(self->midiQueue[idx].offset, inNumberFrames) : inNumberFrames;
            self->engine.processBlock(bufL + cur, outR + cur, static_cast<int>(next - cur));
            cur = next;
        }
        // Events beyond the slice (should not happen) are applied at its end
        while (idx < self->midiCount) {
            const auto &m = self->midiQueue[idx++];
            self->engine.handleMidiEvent(m.status, m.data1, m.data2);
        }
        self->midiCount = 0;
    }

    // Post-render notify
    AudioUnitRenderActionFlags postFlags = localFlags | kAudioUnitRenderAction_PostRender;
    for (const auto &rn : self->renderNotifies) {
        if (rn.proc) rn.proc(rn.userData, &postFlags, inTimeStamp, inOutputBusNumber, inNumberFrames, ioData);
    }

    return noErr;
}

// --- MusicDevice Selectors ---
static OSStatus auMIDIEvent(void *selfPtr,
                            UInt32 inStatus,
                            UInt32 inData1,
                            UInt32 inData2,
                            UInt32 inOffsetSampleFrame) {
    auto *self = static_cast<PhsAudioUnit *>(selfPtr);
    const uint8_t st = static_cast<uint8_t>(inStatus & 0xFF), d1 = static_cast<uint8_t>(inData1 & 0x7F),
                  d2 = static_cast<uint8_t>(inData2 & 0x7F);
    if (self->midiCount < PhsAudioUnit::kMidiQueueSize) {
        // Insertion keeps the queue ordered by offset (hosts send it nearly sorted)
        int i = self->midiCount++;
        while (i > 0 && self->midiQueue[i - 1].offset > inOffsetSampleFrame) {
            self->midiQueue[i] = self->midiQueue[i - 1];
            --i;
        }
        self->midiQueue[i] = {inOffsetSampleFrame, st, d1, d2};
    } else {
        self->engine.handleMidiEvent(st, d1, d2);
    }
    return noErr;
}

static OSStatus auSysEx(void *selfPtr, const UInt8 *data, UInt32 length) {
    if (data && length > 0) static_cast<PhsAudioUnit *>(selfPtr)->engine.handleSysEx(data, length);
    return noErr;
}

static OSStatus auPrepareInstrument(void *, MusicDeviceInstrumentID) {
    return noErr;
}

static OSStatus auReleaseInstrument(void *, MusicDeviceInstrumentID) {
    return noErr;
}

static OSStatus auStartNote(void *selfPtr,
                            MusicDeviceInstrumentID,
                            MusicDeviceGroupID,
                            NoteInstanceID *outNoteInstanceID,
                            UInt32,
                            const MusicDeviceNoteParams *inParams) {
    if (!inParams) return kAudio_ParamError;
    auto *self = static_cast<PhsAudioUnit *>(selfPtr);
    const int pitch = std::clamp(static_cast<int>( std::round(inParams->mPitch) ), 0, 127);
    const float vel = std::clamp(inParams->mVelocity / 127.0f, 0.01f, 1.0f);
    self->engine.noteOn(pitch, vel);
    if (outNoteInstanceID) {
        *outNoteInstanceID = static_cast<NoteInstanceID>(pitch);
    }
    return noErr;
}

static OSStatus auStopNote(void *selfPtr,
                           MusicDeviceGroupID,
                           NoteInstanceID inNoteInstanceID,
                           UInt32) {
    auto *self = static_cast<PhsAudioUnit *>(selfPtr);
    const int pitch = std::clamp(static_cast<int>(inNoteInstanceID), 0, 127);
    self->engine.noteOff(pitch);
    return noErr;
}

// --- AudioComponentPlugInInterface Open / Close / Lookup ---
static OSStatus auOpen(void *selfPtr, AudioComponentInstance inInstance) {
    auto *self = static_cast<PhsAudioUnit *>(selfPtr);
    self->instance = inInstance;
    return noErr;
}

static OSStatus auClose(void *selfPtr) {
    auto *self = static_cast<PhsAudioUnit *>(selfPtr);
    delete self;
    return noErr;
}

static AudioComponentMethod auLookup(SInt16 selector) {
    switch (selector) {
        case kAudioUnitInitializeSelect:                        return (AudioComponentMethod)auInitialize;
        case kAudioUnitUninitializeSelect:                      return (AudioComponentMethod)auUninitialize;
        case kAudioUnitGetPropertyInfoSelect:                   return (AudioComponentMethod)auGetPropertyInfo;
        case kAudioUnitGetPropertySelect:                       return (AudioComponentMethod)auGetProperty;
        case kAudioUnitSetPropertySelect:                       return (AudioComponentMethod)auSetProperty;
        case kAudioUnitAddPropertyListenerSelect:               return (AudioComponentMethod)auAddPropertyListener;
        case kAudioUnitRemovePropertyListenerSelect:            return (AudioComponentMethod)auRemovePropertyListener;
        case kAudioUnitRemovePropertyListenerWithUserDataSelect:return (AudioComponentMethod)auRemovePropertyListenerWithUserData;
        case kAudioUnitAddRenderNotifySelect:                   return (AudioComponentMethod)auAddRenderNotify;
        case kAudioUnitRemoveRenderNotifySelect:                return (AudioComponentMethod)auRemoveRenderNotify;
        case kAudioUnitGetParameterSelect:                      return (AudioComponentMethod)auGetParameter;
        case kAudioUnitSetParameterSelect:                      return (AudioComponentMethod)auSetParameter;
        case kAudioUnitScheduleParametersSelect:                return (AudioComponentMethod)auScheduleParameters;
        case kAudioUnitRenderSelect:                            return (AudioComponentMethod)auRender;
        case kAudioUnitResetSelect:                             return (AudioComponentMethod)auReset;

        case kMusicDeviceMIDIEventSelect:                       return (AudioComponentMethod)auMIDIEvent;
        case kMusicDeviceSysExSelect:                           return (AudioComponentMethod)auSysEx;
        case kMusicDevicePrepareInstrumentSelect:               return (AudioComponentMethod)auPrepareInstrument;
        case kMusicDeviceReleaseInstrumentSelect:               return (AudioComponentMethod)auReleaseInstrument;
        case kMusicDeviceStartNoteSelect:                       return (AudioComponentMethod)auStartNote;
        case kMusicDeviceStopNoteSelect:                        return (AudioComponentMethod)auStopNote;
        default:                                                return nullptr;
    }
}

} // namespace

extern "C" __attribute__((visibility("default")))
void *PhsElectricPianoAUFactory(const AudioComponentDescription *inDesc) {
    if (!inDesc) return nullptr;
    auto *unit = new PhsAudioUnit();
    unit->pluginInterface.Open = auOpen;
    unit->pluginInterface.Close = auClose;
    unit->pluginInterface.Lookup = auLookup;
    unit->pluginInterface.reserved = nullptr;
    return &unit->pluginInterface;
}
