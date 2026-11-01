#pragma once

#include <cstdint>
#include <cstring>

namespace Steinberg {

using int8 = int8_t;
using uint8 = uint8_t;
using int16 = int16_t;
using uint16 = uint16_t;
using int32 = int32_t;
using uint32 = uint32_t;
using int64 = int64_t;
using uint64 = uint64_t;
using tresult = int32;
using TBool = uint8;
using char8 = char;
using char16 = char16_t;
using TUID = uint8[16];

// Windows VST3 is COM-compatible: __stdcall, COM HRESULT codes and COM GUID byte order
// (as COM_COMPATIBLE in the Steinberg SDK). Other platforms use the plain layout.
#if defined(_WIN32)
#define SMTG_API __stdcall
static constexpr tresult kNoInterface = static_cast<tresult>(0x80004002L);
static constexpr tresult kResultOk = 0;
static constexpr tresult kResultTrue = kResultOk;
static constexpr tresult kResultFalse = 1;
static constexpr tresult kInvalidArgument = static_cast<tresult>(0x80070057L);
static constexpr tresult kNotImplemented = static_cast<tresult>(0x80004001L);
static constexpr tresult kInternalError = static_cast<tresult>(0x80004005L);
static constexpr tresult kNotInitialized = static_cast<tresult>(0x8000FFFFL);
static constexpr tresult kOutOfMemory = static_cast<tresult>(0x8007000EL);
#define INLINE_UID(l1, l2, l3, l4) \
{ \
    (uint8)(((uint32)(l1) & 0x000000FF)      ), (uint8)(((uint32)(l1) & 0x0000FF00) >>  8), \
    (uint8)(((uint32)(l1) & 0x00FF0000) >> 16), (uint8)(((uint32)(l1) & 0xFF000000) >> 24), \
    (uint8)(((uint32)(l2) & 0x00FF0000) >> 16), (uint8)(((uint32)(l2) & 0xFF000000) >> 24), \
    (uint8)(((uint32)(l2) & 0x000000FF)      ), (uint8)(((uint32)(l2) & 0x0000FF00) >>  8), \
    (uint8)(((uint32)(l3) & 0xFF000000) >> 24), (uint8)(((uint32)(l3) & 0x00FF0000) >> 16), \
    (uint8)(((uint32)(l3) & 0x0000FF00) >>  8), (uint8)(((uint32)(l3) & 0x000000FF)      ), \
    (uint8)(((uint32)(l4) & 0xFF000000) >> 24), (uint8)(((uint32)(l4) & 0x00FF0000) >> 16), \
    (uint8)(((uint32)(l4) & 0x0000FF00) >>  8), (uint8)(((uint32)(l4) & 0x000000FF)      )  \
}
#else
#define SMTG_API

static constexpr tresult kNoInterface = -1;
static constexpr tresult kResultOk = 0;
static constexpr tresult kResultTrue = kResultOk;
static constexpr tresult kResultFalse = 1;
static constexpr tresult kInvalidArgument = 2;
static constexpr tresult kNotImplemented = 3;
static constexpr tresult kInternalError = 4;
static constexpr tresult kNotInitialized = 5;
static constexpr tresult kOutOfMemory = 6;

#define INLINE_UID(l1, l2, l3, l4) \
{ \
    (uint8)(((uint32)(l1) & 0xFF000000) >> 24), (uint8)(((uint32)(l1) & 0x00FF0000) >> 16), \
    (uint8)(((uint32)(l1) & 0x0000FF00) >>  8), (uint8)(((uint32)(l1) & 0x000000FF)      ), \
    (uint8)(((uint32)(l2) & 0xFF000000) >> 24), (uint8)(((uint32)(l2) & 0x00FF0000) >> 16), \
    (uint8)(((uint32)(l2) & 0x0000FF00) >>  8), (uint8)(((uint32)(l2) & 0x000000FF)      ), \
    (uint8)(((uint32)(l3) & 0xFF000000) >> 24), (uint8)(((uint32)(l3) & 0x00FF0000) >> 16), \
    (uint8)(((uint32)(l3) & 0x0000FF00) >>  8), (uint8)(((uint32)(l3) & 0x000000FF)      ), \
    (uint8)(((uint32)(l4) & 0xFF000000) >> 24), (uint8)(((uint32)(l4) & 0x00FF0000) >> 16), \
    (uint8)(((uint32)(l4) & 0x0000FF00) >>  8), (uint8)(((uint32)(l4) & 0x000000FF)      )  \
}
#endif

inline bool iidEqual(const TUID a, const TUID b) {
    return std::memcmp(a, b, 16) == 0;
}

inline void copyAsciiToUtf16(char16* dst, const char* src, size_t maxChars) {
    size_t i = 0;
    while (i + 1 < maxChars && src[i] != '\0') {
        dst[i] = static_cast<char16>(static_cast<unsigned char>(src[i]));
        ++i;
    }
    dst[i] = 0;
}

class FUnknown {
public:
    virtual tresult SMTG_API queryInterface(const TUID _iid, void** obj) = 0;
    virtual uint32 SMTG_API addRef() = 0;
    virtual uint32 SMTG_API release() = 0;
};

static const TUID kFUnknownIID        = INLINE_UID(0x00000000, 0x00000000, 0xC0000000, 0x00000046);
static const TUID kIPluginBaseIID     = INLINE_UID(0x22888DDB, 0x156E45AE, 0x8358B348, 0x08190625);
static const TUID kIPluginFactoryIID  = INLINE_UID(0x7A4D811C, 0x52114A1F, 0xAED9D2EE, 0x0B43BF9F);
static const TUID kIPluginFactory2IID = INLINE_UID(0x0007B650, 0xF24B4C0B, 0xA464EDB9, 0xF00B2ABB);
static const TUID kIPluginFactory3IID = INLINE_UID(0x4555A2AB, 0xC1234E57, 0x9B122910, 0x36878931);
static const TUID kIComponentIID      = INLINE_UID(0xE831FF31, 0xF2D54301, 0x928EBBEE, 0x25697802);
static const TUID kIAudioProcessorIID = INLINE_UID(0x42043F99, 0xB7DA453C, 0xA569E79D, 0x9AAEC33D);
static const TUID kIEditControllerIID = INLINE_UID(0xDCD7BBE3, 0x7742448D, 0xA874AACC, 0x979C759E);
static const TUID kIPlugViewIID       = INLINE_UID(0x5BC32507, 0xD06049EA, 0xA6151B52, 0x2B755B29);

class IBStream : public FUnknown {
public:
    enum IStreamSeekMode { kIBSeekSet = 0, kIBSeekCur, kIBSeekEnd };
    virtual tresult SMTG_API read(void* buffer, int32 numBytes, int32* numBytesRead) = 0;
    virtual tresult SMTG_API write(void* buffer, int32 numBytes, int32* numBytesWritten) = 0;
    virtual tresult SMTG_API seek(int64 pos, int32 mode, int64* result) = 0;
    virtual tresult SMTG_API tell(int64* pos) = 0;
};

class IPluginBase : public FUnknown {
public:
    virtual tresult SMTG_API initialize(FUnknown* context) = 0;
    virtual tresult SMTG_API terminate() = 0;
};

struct PFactoryInfo {
    enum FactoryFlags { kNoFlags = 0, kClassesDiscardable = 1 << 0, kLicenseCheck = 1 << 1, kComponentNonDiscardable = 1 << 3, kUnicode = 1 << 4 };
    char8 vendor[64];
    char8 url[256];
    char8 email[128];
    int32 flags;
};

struct PClassInfo {
    enum ClassCardinality { kManyInstances = 0x7FFFFFFF };
    TUID cid;
    int32 cardinality;
    char8 category[32];
    char8 name[64];
};

struct PClassInfo2 {
    TUID cid;
    int32 cardinality;
    char8 category[32];
    char8 name[64];
    uint32 classFlags;
    char8 subCategories[128];
    char8 vendor[64];
    char8 version[64];
    char8 sdkVersion[64];
};

struct PClassInfoW {
    TUID cid;
    int32 cardinality;
    char8 category[32];
    char16 name[64];
    uint32 classFlags;
    char8 subCategories[128];
    char16 vendor[64];
    char16 version[64];
    char16 sdkVersion[64];
};

class IPluginFactory : public FUnknown {
public:
    virtual tresult SMTG_API getFactoryInfo(PFactoryInfo* info) = 0;
    virtual int32 SMTG_API countClasses() = 0;
    virtual tresult SMTG_API getClassInfo(int32 index, PClassInfo* info) = 0;
    virtual tresult SMTG_API createInstance(const char* cid, const char* _iid, void** obj) = 0;
};

class IPluginFactory2 : public IPluginFactory {
public:
    virtual tresult SMTG_API getClassInfo2(int32 index, PClassInfo2* info) = 0;
};

class IPluginFactory3 : public IPluginFactory2 {
public:
    virtual tresult SMTG_API getClassInfoUnicode(int32 index, PClassInfoW* info) = 0;
    virtual tresult SMTG_API setHostContext(FUnknown* context) = 0;
};

struct ViewRect {
    int32 left = 0;
    int32 top = 0;
    int32 right = 0;
    int32 bottom = 0;
};

class IPlugFrame : public FUnknown {
public:
    virtual tresult SMTG_API resizeView(class IPlugView* view, ViewRect* newSize) = 0;
};

class IPlugView : public FUnknown {
public:
    virtual tresult SMTG_API isPlatformTypeSupported(const char* type) = 0;
    virtual tresult SMTG_API attached(void* parent, const char* type) = 0;
    virtual tresult SMTG_API removed() = 0;
    virtual tresult SMTG_API onWheel(float distance) = 0;
    virtual tresult SMTG_API onKeyDown(char16 key, int16 keyCode, int16 modifiers) = 0;
    virtual tresult SMTG_API onKeyUp(char16 key, int16 keyCode, int16 modifiers) = 0;
    virtual tresult SMTG_API getSize(ViewRect* size) = 0;
    virtual tresult SMTG_API onSize(ViewRect* newSize) = 0;
    virtual tresult SMTG_API onFocus(TBool state) = 0;
    virtual tresult SMTG_API setFrame(IPlugFrame* frame) = 0;
    virtual tresult SMTG_API canResize() = 0;
    virtual tresult SMTG_API checkSizeConstraint(ViewRect* rect) = 0;
};

namespace Vst {

using ParamID = uint32;
using ParamValue = double;
using SampleRate = double;
using MediaType = int32;
using BusDirection = int32;
using BusType = int32;
using IoMode = int32;
using SpeakerArrangement = uint64;
using String128 = char16[128];

enum MediaTypes { kAudio = 0, kEvent = 1 };
enum BusDirections { kInput = 0, kOutput = 1 };
enum BusTypes { kMain = 0, kAux = 1 };
enum SymbolicSampleSizes { kSample32 = 0, kSample64 = 1 };

static constexpr SpeakerArrangement kSpeakerArrEmpty = 0;
static constexpr SpeakerArrangement kSpeakerArrMono = (1ULL << 19);
static constexpr SpeakerArrangement kSpeakerArrStereo = (1ULL << 0) | (1ULL << 1);

struct BusInfo {
    MediaType mediaType;
    BusDirection direction;
    int32 channelCount;
    String128 name;
    BusType busType;
    uint32 flags;
    enum BusFlags { kDefaultActive = 1 << 0 };
};

struct RoutingInfo {
    MediaType mediaType;
    int32 busIndex;
    int32 channel;
};

class IComponent : public IPluginBase {
public:
    virtual tresult SMTG_API getControllerClassId(TUID classId) = 0;
    virtual tresult SMTG_API setIoMode(IoMode mode) = 0;
    virtual int32 SMTG_API getBusCount(MediaType type, BusDirection dir) = 0;
    virtual tresult SMTG_API getBusInfo(MediaType type, BusDirection dir, int32 index, BusInfo& bus) = 0;
    virtual tresult SMTG_API getRoutingInfo(RoutingInfo& inInfo, RoutingInfo& outInfo) = 0;
    virtual tresult SMTG_API activateBus(MediaType type, BusDirection dir, int32 index, TBool state) = 0;
    virtual tresult SMTG_API setActive(TBool state) = 0;
    virtual tresult SMTG_API setState(IBStream* state) = 0;
    virtual tresult SMTG_API getState(IBStream* state) = 0;
};

struct ProcessSetup {
    int32 processMode;
    int32 symbolicSampleSize;
    int32 maxSamplesPerBlock;
    SampleRate sampleRate;
};

struct AudioBusBuffers {
    int32 numChannels;
    uint64 silenceFlags;
    union {
        float** channelBuffers32;
        double** channelBuffers64;
    };
};

class IParamValueQueue : public FUnknown {
public:
    virtual ParamID SMTG_API getParameterId() = 0;
    virtual int32 SMTG_API getPointCount() = 0;
    virtual tresult SMTG_API getPoint(int32 index, int32& sampleOffset, ParamValue& value) = 0;
    virtual tresult SMTG_API addPoint(int32 sampleOffset, ParamValue value, int32& index) = 0;
};

class IParameterChanges : public FUnknown {
public:
    virtual int32 SMTG_API getParameterCount() = 0;
    virtual IParamValueQueue* SMTG_API getParameterData(int32 index) = 0;
    virtual IParamValueQueue* SMTG_API addParameterData(const ParamID& id, int32& index) = 0;
};

struct NoteOnEvent {
    int16 channel;
    int16 pitch;
    float tuning;
    float velocity;
    int32 length;
    int32 noteId;
};

struct NoteOffEvent {
    int16 channel;
    int16 pitch;
    float velocity;
    int32 noteId;
    float tuning;
};

struct Event {
    int32 busIndex;
    int32 sampleOffset;
    double ppqPosition;
    uint16 flags;
    uint16 type;
    enum EventTypes {
        kNoteOnEvent = 0,
        kNoteOffEvent = 1,
        kDataEvent = 2,
        kPolyPressureEvent = 3,
        kNoteExpressionValueEvent = 4
    };
    union {
        NoteOnEvent noteOn;
        NoteOffEvent noteOff;
        uint8 rawPad[32];
    };
};

class IEventList : public FUnknown {
public:
    virtual int32 SMTG_API getEventCount() = 0;
    virtual tresult SMTG_API getEvent(int32 index, Event& e) = 0;
    virtual tresult SMTG_API addEvent(Event& e) = 0;
};

struct ProcessData {
    int32 processMode;
    int32 symbolicSampleSize;
    int32 numSamples;
    int32 numInputs;
    int32 numOutputs;
    AudioBusBuffers* inputs;
    AudioBusBuffers* outputs;
    IParameterChanges* inputParameterChanges;
    IParameterChanges* outputParameterChanges;
    IEventList* inputEvents;
    IEventList* outputEvents;
    void* processContext;
};

class IAudioProcessor : public FUnknown {
public:
    virtual tresult SMTG_API setBusArrangements(SpeakerArrangement* inputs, int32 numIns,
                                                SpeakerArrangement* outputs, int32 numOuts) = 0;
    virtual tresult SMTG_API getBusArrangement(BusDirection dir, int32 index, SpeakerArrangement& arr) = 0;
    virtual tresult SMTG_API canProcessSampleSize(int32 symbolicSampleSize) = 0;
    virtual uint32 SMTG_API getLatencySamples() = 0;
    virtual tresult SMTG_API setupProcessing(ProcessSetup& setup) = 0;
    virtual tresult SMTG_API setProcessing(TBool state) = 0;
    virtual tresult SMTG_API process(ProcessData& data) = 0;
    virtual uint32 SMTG_API getTailSamples() = 0;
};

struct ParameterInfo {
    ParamID id;
    String128 title;
    String128 shortTitle;
    String128 units;
    int32 stepCount;
    ParamValue defaultNormalizedValue;
    int32 unitId;
    int32 flags;
    enum ParameterFlags {
        kNoFlags = 0,
        kCanAutomate = 1 << 0,
        kIsReadOnly = 1 << 1,
        kIsWrapAround = 1 << 2,
        kIsList = 1 << 3
    };
};

class IComponentHandler : public FUnknown {
public:
    virtual tresult SMTG_API beginEdit(ParamID id) = 0;
    virtual tresult SMTG_API performEdit(ParamID id, ParamValue valueNormalized) = 0;
    virtual tresult SMTG_API endEdit(ParamID id) = 0;
    virtual tresult SMTG_API restartComponent(int32 flags) = 0;
};

class IEditController : public IPluginBase {
public:
    virtual tresult SMTG_API setComponentState(IBStream* state) = 0;
    virtual tresult SMTG_API setState(IBStream* state) = 0;
    virtual tresult SMTG_API getState(IBStream* state) = 0;
    virtual int32 SMTG_API getParameterCount() = 0;
    virtual tresult SMTG_API getParameterInfo(int32 paramIndex, ParameterInfo& info) = 0;
    virtual tresult SMTG_API getParamStringByValue(ParamID id, ParamValue valueNormalized, String128 string) = 0;
    virtual tresult SMTG_API getParamValueByString(ParamID id, char16* string, ParamValue& valueNormalized) = 0;
    virtual ParamValue SMTG_API normalizedParamToPlain(ParamID id, ParamValue valueNormalized) = 0;
    virtual ParamValue SMTG_API plainParamToNormalized(ParamID id, ParamValue plainValue) = 0;
    virtual ParamValue SMTG_API getParamNormalized(ParamID id) = 0;
    virtual tresult SMTG_API setParamNormalized(ParamID id, ParamValue value) = 0;
    virtual tresult SMTG_API setComponentHandler(IComponentHandler* handler) = 0;
    virtual IPlugView* SMTG_API createView(const char* name) = 0;
};

} // namespace Vst
} // namespace Steinberg
