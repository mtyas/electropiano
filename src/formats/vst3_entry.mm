#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#include "../gui/PhsCocoaView.h"
#elif defined(_WIN32)
#include "../gui/PhsWin32View.h"
#elif defined(__linux__)
#include "../gui/PhsX11View.h"
#endif

#include "../../include/vst3/vst3_c_api.h"
#include "../dsp/PhsSynthEngine.hpp"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <cstdlib>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

// Unique VST3 Component Class ID for PyPHS Electric Piano
static const TUID kPhsProcessorCID = INLINE_UID(0x50795048, 0x53456C65, 0x63506961, 0x6E6F3031);

class PhsPlugView : public IPlugView {
public:
    PhsPlugView(phs::PhsSynthEngine* engine, IComponentHandler* handler)
        : engine_(engine), handler_(handler) {}

    virtual ~PhsPlugView() {
#if defined(__APPLE__)
        if (nsView_) {
            phs::destroyPhsEditorView(nsView_);
            nsView_ = nil;
        }
#elif defined(_WIN32)
        if (hwnd_) {
            phs::destroyPhsWin32EditorView(hwnd_);
            hwnd_ = nullptr;
        }
#elif defined(__linux__)
        if (x11Win_) {
            phs::destroyPhsX11EditorView(x11Win_);
            x11Win_ = 0;
        }
#endif
    }

    tresult SMTG_API queryInterface(const TUID _iid, void** obj) override {
        if (!obj) return kInvalidArgument;
        if (iidEqual(_iid, kFUnknownIID) || iidEqual(_iid, kIPlugViewIID)) {
            addRef();
            *obj = static_cast<IPlugView*>(this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }

    uint32 SMTG_API addRef() override {
        return ++refCount_;
    }

    uint32 SMTG_API release() override {
        const uint32 r = --refCount_;
        if (r == 0) delete this;
        return r;
    }

    tresult SMTG_API isPlatformTypeSupported(const char* type) override {
        if (!type) return kInvalidArgument;
#if defined(__APPLE__)
        if (std::strcmp(type, "NSView") == 0) return kResultTrue;
#elif defined(_WIN32)
        if (std::strcmp(type, "HWND") == 0) return kResultTrue;
#elif defined(__linux__)
        if (std::strcmp(type, "X11EmbedWindowID") == 0) return kResultTrue;
#endif
        return kResultFalse;
    }

    tresult SMTG_API attached(void* parent, const char* type) override {
        if (!parent || isPlatformTypeSupported(type) != kResultTrue) {
            return kInvalidArgument;
        }
#if defined(__APPLE__)
        if (!nsView_) {
            IComponentHandler* h = handler_;
            nsView_ = phs::createPhsEditorView(engine_, [h](uint32_t paramId, float normVal) {
                if (h) {
                    h->beginEdit(paramId);
                    h->performEdit(paramId, static_cast<ParamValue>(normVal));
                    h->endEdit(paramId);
                }
            }, [this](int w, int ht) { return requestResize(w, ht); });
        }
        NSView* parentView = static_cast<NSView*>(parent);
        if (parentView && nsView_) {
            [parentView addSubview:nsView_];
            return kResultOk;
        }
#elif defined(_WIN32)
        IComponentHandler* h = handler_;
        hwnd_ = phs::createPhsWin32EditorView(static_cast<HWND>(parent), engine_, [h](uint32_t paramId, float normVal) {
            if (h) {
                h->beginEdit(paramId);
                h->performEdit(paramId, static_cast<ParamValue>(normVal));
                h->endEdit(paramId);
            }
        }, [this](int w, int ht) { return requestResize(w, ht); });
        if (hwnd_) return kResultOk;
#elif defined(__linux__)
        IComponentHandler* h = handler_;
        // IPlugFrame must be called from the host's UI thread, which the X11 editor thread is not:
        // the zoom control's new size applies when the editor is next opened (host resizes still work)
        x11Win_ = phs::createPhsX11EditorView(static_cast<Window>(reinterpret_cast<uintptr_t>(parent)), engine_, [h](uint32_t paramId, float normVal) {
            if (h) {
                h->beginEdit(paramId);
                h->performEdit(paramId, static_cast<ParamValue>(normVal));
                h->endEdit(paramId);
            }
        }, [](int, int) { return false; });
        if (x11Win_) return kResultOk;
#endif
        return kInternalError;
    }

    tresult SMTG_API removed() override {
#if defined(__APPLE__)
        if (nsView_) {
            phs::destroyPhsEditorView(nsView_);
            nsView_ = nil;
        }
#elif defined(_WIN32)
        if (hwnd_) {
            phs::destroyPhsWin32EditorView(hwnd_);
            hwnd_ = nullptr;
        }
#elif defined(__linux__)
        if (x11Win_) {
            phs::destroyPhsX11EditorView(x11Win_);
            x11Win_ = 0;
        }
#endif
        return kResultOk;
    }

    tresult SMTG_API onWheel(float) override { return kResultFalse; }
    tresult SMTG_API onKeyDown(char16, int16, int16) override { return kResultFalse; }
    tresult SMTG_API onKeyUp(char16, int16, int16) override { return kResultFalse; }

    tresult SMTG_API getSize(ViewRect* size) override {
        if (!size) return kInvalidArgument;
        size->left = 0;
        size->top = 0;
        size->right = width_;
        size->bottom = height_;
        return kResultOk;
    }

    tresult SMTG_API onSize(ViewRect* r) override {
        if (!r) return kInvalidArgument;
        width_ = r->right - r->left;
        height_ = r->bottom - r->top;
#if defined(__APPLE__)
        if (nsView_) [nsView_ setFrameSize:NSMakeSize(width_, height_)];
#elif defined(_WIN32)
        if (hwnd_) phs::setPhsWin32EditorSize(hwnd_, width_, height_);
#elif defined(__linux__)
        if (x11Win_) phs::setPhsX11EditorSize(x11Win_, width_, height_);
#endif
        return kResultOk;
    }

    // The editor's zoom control: ask the host to resize (it answers with onSize)
    bool requestResize(int w, int h) {
        if (!plugFrame_) return false;
        ViewRect r;
        r.right = w;
        r.bottom = h;
        if (plugFrame_->resizeView(this, &r) != kResultOk) return false;
        width_ = w;
        height_ = h;
        return true;
    }
    tresult SMTG_API onFocus(TBool) override { return kResultOk; }
    tresult SMTG_API setFrame(IPlugFrame* frame) override {
        plugFrame_ = frame;
        return kResultOk;
    }
    // Resizable at the layout's aspect ratio, from half to three times its size
    tresult SMTG_API canResize() override { return kResultTrue; }
    tresult SMTG_API checkSizeConstraint(ViewRect* rect) override {
        if (!rect) return kInvalidArgument;
        const float z = std::clamp(std::min((rect->right - rect->left) / static_cast<float>(phs::kEditorWidth),
                                            (rect->bottom - rect->top) / static_cast<float>(phs::kEditorHeight)), 0.5f, 3.0f);
        rect->right = rect->left + phs::scaledEditorWidth(z);
        rect->bottom = rect->top + phs::scaledEditorHeight(z);
        return kResultOk;
    }

private:
    std::atomic<uint32> refCount_{1};
    phs::PhsSynthEngine* engine_ = nullptr;
    IComponentHandler* handler_ = nullptr;
    IPlugFrame* plugFrame_ = nullptr;
    int32 width_ = phs::scaledEditorWidth(phs::loadEditorZoom());
    int32 height_ = phs::scaledEditorHeight(phs::loadEditorZoom());
#if defined(__APPLE__)
    NSView* nsView_ = nil;
#elif defined(_WIN32)
    HWND hwnd_ = nullptr;
#elif defined(__linux__)
    Window x11Win_ = 0;
#endif
};

// Combined Single-Component VST3 Instrument implementing IComponent, IAudioProcessor, and IEditController
class PhsVst3Component : public IComponent, public IAudioProcessor, public IEditController, public INoteExpressionController {
public:
    PhsVst3Component() = default;
    virtual ~PhsVst3Component() {
        if (componentHandler_) {
            componentHandler_->release();
            componentHandler_ = nullptr;
        }
    }

    // --- FUnknown ---
    tresult SMTG_API queryInterface(const TUID _iid, void** obj) override {
        if (!obj) return kInvalidArgument;
        if (iidEqual(_iid, kFUnknownIID) || iidEqual(_iid, kIPluginBaseIID) || iidEqual(_iid, kIComponentIID)) {
            addRef();
            *obj = static_cast<IComponent*>(this);
            return kResultOk;
        }
        if (iidEqual(_iid, kIAudioProcessorIID)) {
            addRef();
            *obj = static_cast<IAudioProcessor*>(this);
            return kResultOk;
        }
        if (iidEqual(_iid, kIEditControllerIID)) {
            addRef();
            *obj = static_cast<IEditController*>(this);
            return kResultOk;
        }
        if (iidEqual(_iid, kINoteExpressionControllerIID)) {
            addRef();
            *obj = static_cast<INoteExpressionController*>(this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }

    uint32 SMTG_API addRef() override {
        return ++refCount_;
    }

    uint32 SMTG_API release() override {
        const uint32 r = --refCount_;
        if (r == 0) delete this;
        return r;
    }

    // --- IPluginBase ---
    tresult SMTG_API initialize(FUnknown*) override {
        return kResultOk;
    }

    tresult SMTG_API terminate() override {
        if (componentHandler_) {
            componentHandler_->release();
            componentHandler_ = nullptr;
        }
        return kResultOk;
    }

    // --- IComponent ---
    tresult SMTG_API getControllerClassId(TUID classId) override {
        // Single-component architecture: return kNoInterface (or zero UID) so host queries IEditController directly on this object
        std::memset(classId, 0, sizeof(TUID));
        return kNoInterface;
    }

    tresult SMTG_API setIoMode(IoMode) override { return kResultOk; }

    int32 SMTG_API getBusCount(MediaType type, BusDirection dir) override {
        if (type == kAudio && dir == kOutput) return 1;
        if (type == kEvent && dir == kInput) return 1;
        return 0;
    }

    tresult SMTG_API getBusInfo(MediaType type, BusDirection dir, int32 index, BusInfo& bus) override {
        if (index != 0) return kInvalidArgument;
        std::memset(&bus, 0, sizeof(bus));
        if (type == kAudio && dir == kOutput) {
            bus.mediaType = kAudio;
            bus.direction = kOutput;
            bus.channelCount = 2;
            copyAsciiToUtf16(bus.name, "Stereo Out", 128);
            bus.busType = kMain;
            bus.flags = BusInfo::kDefaultActive;
            return kResultOk;
        }
        if (type == kEvent && dir == kInput) {
            bus.mediaType = kEvent;
            bus.direction = kInput;
            bus.channelCount = 16;
            copyAsciiToUtf16(bus.name, "MIDI Event In", 128);
            bus.busType = kMain;
            bus.flags = BusInfo::kDefaultActive;
            return kResultOk;
        }
        return kInvalidArgument;
    }

    tresult SMTG_API getRoutingInfo(RoutingInfo&, RoutingInfo&) override {
        return kNotImplemented;
    }

    tresult SMTG_API activateBus(MediaType, BusDirection, int32 index, TBool) override {
        return (index == 0) ? kResultOk : kInvalidArgument;
    }

    tresult SMTG_API setActive(TBool state) override {
        if (!state) engine_.reset();
        return kResultOk;
    }

    tresult SMTG_API setState(IBStream* state) override {
        if (!state) return kInvalidArgument;
        // The whole stream: the fixed block and the tuning chunk after it
        std::vector<uint8_t> st;
        uint8_t buf[4096];
        for (;;) {
            int32 n = 0;
            if (state->read(buf, sizeof(buf), &n) != kResultOk || n <= 0 || st.size() > (8u << 20)) break;
            st.insert(st.end(), buf, buf + n);
        }
        if (st.size() < 12) return kInternalError;
        return engine_.setState(st.data(), st.size()) ? kResultOk : kInternalError;
    }

    tresult SMTG_API getState(IBStream* state) override {
        if (!state) return kInvalidArgument;
        const std::vector<uint8_t> st = engine_.saveState();
        int32 bytesWritten = 0;
        state->write(const_cast<uint8_t*>(st.data()), static_cast<int32>(st.size()), &bytesWritten);
        return (bytesWritten == static_cast<int32>(st.size())) ? kResultOk : kInternalError;
    }

    // --- IAudioProcessor ---
    tresult SMTG_API setBusArrangements(SpeakerArrangement* inputs, int32 numIns,
                                        SpeakerArrangement* outputs, int32 numOuts) override {
        (void)inputs;
        if (numIns != 0 || numOuts != 1 || !outputs) return kResultFalse;
        if (outputs[0] != kSpeakerArrStereo && outputs[0] != kSpeakerArrMono) return kResultFalse;
        outArr_ = outputs[0];
        return kResultTrue;
    }

    tresult SMTG_API getBusArrangement(BusDirection dir, int32 index, SpeakerArrangement& arr) override {
        if (dir == kOutput && index == 0) {
            arr = outArr_;
            return kResultOk;
        }
        return kInvalidArgument;
    }

    tresult SMTG_API canProcessSampleSize(int32 symbolicSampleSize) override {
        return (symbolicSampleSize == kSample32) ? kResultTrue : kResultFalse;
    }

    uint32 SMTG_API getLatencySamples() override { return 0; }

    // Note expression events address a note by id, so remember which key each id started
    void rememberNoteId(int32 id, int16 pitch) {
        if (id < 0) return;
        noteIds_[noteIdWrite_ & 63] = {id, pitch};
        ++noteIdWrite_;
    }
    int keyForNoteId(int32 id) const {
        for (const auto& n : noteIds_) if (n.id == id) return n.key;
        return -1;
    }
    struct NoteIdSlot { int32 id = -1; int key = -1; };
    NoteIdSlot noteIds_[64]{};
    uint32_t noteIdWrite_ = 0;

    tresult SMTG_API setupProcessing(ProcessSetup& setup) override {
        engine_.prepare(static_cast<float>(setup.sampleRate));
        return kResultOk;
    }

    tresult SMTG_API setProcessing(TBool state) override {
        if (!state) engine_.reset();
        return kResultOk;
    }

    // One note, expression or automation point, at its position in the block
    struct TimedEvent {
        int32 offset;
        uint32 seq;           // arrival order, so simultaneous events keep their order
        enum Kind : uint8_t { Param, NoteOn, NoteOff, Expression, PolyPressure } kind;
        int16 channel, pitch;
        uint32 id;            // parameter id or note-expression type
        int32 noteId;
        double value;         // parameter value, velocity, pressure or expression value
    };
    static constexpr int kMaxTimedEvents = 4096;

    void addTimedEvent(const TimedEvent& e) {
        if (numTimed_ < kMaxTimedEvents) timed_[numTimed_++] = e;
    }

    void applyTimedEvent(const TimedEvent& e) {
        switch (e.kind) {
            case TimedEvent::Param:
                engine_.setParamNormalized(e.id, static_cast<float>(e.value));
                break;
            case TimedEvent::NoteOn:
                rememberNoteId(e.noteId, e.pitch);
                engine_.noteOn(e.pitch, static_cast<float>(e.value), e.channel);
                break;
            case TimedEvent::NoteOff:
                engine_.noteOff(e.pitch, e.channel, static_cast<float>(e.value));
                break;
            case TimedEvent::Expression: {
                const int key = keyForNoteId(e.noteId);
                if (key < 0) break;
                if (e.id == kTuningTypeID) engine_.setNoteTuning(key, static_cast<float>((e.value - 0.5) * 240.0));
                else if (e.id == kBrightnessTypeID) engine_.setNoteBrightness(key, static_cast<float>(e.value));
                else if (e.id == kExpressionTypeID) engine_.setNotePressure(key, static_cast<float>(e.value));
                break;
            }
            case TimedEvent::PolyPressure:
                engine_.setNotePressure(e.pitch, static_cast<float>(e.value));
                break;
        }
    }

    tresult SMTG_API process(ProcessData& data) override {
        // Notes, expressions and every automation point are applied at their sample position:
        // the block is rendered in pieces between them
        numTimed_ = 0;
        uint32 seq = 0;
        const int32 numSamples = std::max<int32>(0, data.numSamples);
        auto clampOffset = [&](int32 o) { return std::clamp<int32>(o, 0, std::max<int32>(0, numSamples - 1)); };

        // 1. Parameter changes: every point of every queue
        if (data.inputParameterChanges) {
            const int32 numParamsChanged = data.inputParameterChanges->getParameterCount();
            for (int32 i = 0; i < numParamsChanged; ++i) {
                IParamValueQueue* queue = data.inputParameterChanges->getParameterData(i);
                if (!queue) continue;
                const int32 numPoints = queue->getPointCount();
                for (int32 k = 0; k < numPoints; ++k) {
                    int32 sampleOffset = 0;
                    ParamValue value = 0.0;
                    if (queue->getPoint(k, sampleOffset, value) != kResultOk) continue;
                    TimedEvent e{};
                    e.offset = clampOffset(sampleOffset);
                    e.seq = seq++;
                    e.kind = TimedEvent::Param;
                    e.id = queue->getParameterId();
                    e.value = value;
                    addTimedEvent(e);
                }
            }
        }

        // Host tempo (ProcessContext: the leading fields of the VST3 struct)
        if (data.processContext) {
            struct ProcessContextHead {
                uint32 state;
                double sampleRate;
                int64 projectTimeSamples, systemTime, continousTimeSamples;
                double projectTimeMusic, barPositionMusic, cycleStartMusic, cycleEndMusic;
                double tempo;
            };
            const auto* pc = static_cast<const ProcessContextHead*>(data.processContext);
            if (pc->state & (1u << 10)) engine_.setTempo(static_cast<float>(pc->tempo)); // kTempoValid
        }
        // kOffline = 2: the host is bouncing, so the engine may use its high-quality path
        engine_.setOfflineRendering(data.processMode == 2);

        // 2. Note events
        if (data.inputEvents) {
            const int32 numEvents = data.inputEvents->getEventCount();
            for (int32 i = 0; i < numEvents; ++i) {
                Event ev{};
                if (data.inputEvents->getEvent(i, ev) != kResultOk) continue;
                TimedEvent e{};
                e.offset = clampOffset(ev.sampleOffset);
                e.seq = seq++;
                if (ev.type == Event::kNoteOnEvent) {
                    e.channel = ev.noteOn.channel;
                    e.pitch = ev.noteOn.pitch;
                    e.noteId = ev.noteOn.noteId;
                    e.value = ev.noteOn.velocity;
                    e.kind = ev.noteOn.velocity > 0.0f ? TimedEvent::NoteOn : TimedEvent::NoteOff;
                    if (e.kind == TimedEvent::NoteOff) e.value = 0.5;
                } else if (ev.type == Event::kNoteOffEvent) {
                    e.kind = TimedEvent::NoteOff;
                    e.channel = ev.noteOff.channel;
                    e.pitch = ev.noteOff.pitch;
                    e.value = ev.noteOff.velocity;
                } else if (ev.type == Event::kNoteExpressionValueEvent) {
                    e.kind = TimedEvent::Expression;
                    e.id = ev.noteExpressionValue.typeId;
                    e.noteId = ev.noteExpressionValue.noteId;
                    e.value = ev.noteExpressionValue.value;
                } else if (ev.type == Event::kPolyPressureEvent) {
                    e.kind = TimedEvent::PolyPressure;
                    e.pitch = ev.polyPressure.pitch;
                    e.value = ev.polyPressure.pressure;
                } else {
                    continue;
                }
                addTimedEvent(e);
            }
        }
        std::sort(timed_, timed_ + numTimed_, [](const TimedEvent& a, const TimedEvent& b) {
            return a.offset != b.offset ? a.offset < b.offset : a.seq < b.seq;
        });

        // 3. Render between events
        float* outL = nullptr;
        float* outR = nullptr;
        if (data.numOutputs > 0 && data.outputs && data.outputs[0].channelBuffers32) {
            outL = data.outputs[0].channelBuffers32[0];
            outR = (data.outputs[0].numChannels > 1) ? data.outputs[0].channelBuffers32[1] : outL;
        }
        int idx = 0;
        int32 cur = 0;
        while (cur < numSamples) {
            while (idx < numTimed_ && timed_[idx].offset <= cur) applyTimedEvent(timed_[idx++]);
            const int32 next = idx < numTimed_ ? timed_[idx].offset : numSamples;
            if (outL) engine_.processBlock(outL + cur, outR + cur, next - cur);
            else engine_.processBlock(nullptr, nullptr, next - cur);
            cur = next;
        }
        while (idx < numTimed_) applyTimedEvent(timed_[idx++]);

        if (outL) {
            // While idle the engine writes exact zeros, so downstream processing can be skipped
            data.outputs[0].silenceFlags = engine_.isIdle() ? ((1ull << data.outputs[0].numChannels) - 1) : 0;
        }
        return kResultOk;
    }

    // --- INoteExpressionController: tuning (pitch), brightness (slide) and expression (pressure) ---
    int32 SMTG_API getNoteExpressionCount(int32, int16) override { return 3; }

    tresult SMTG_API getNoteExpressionInfo(int32, int16, int32 index, NoteExpressionTypeInfo& info) override {
        if (index < 0 || index > 2) return kInvalidArgument;
        std::memset(&info, 0, sizeof(info));
        const uint32 ids[3] = {kTuningTypeID, kBrightnessTypeID, kExpressionTypeID};
        const char* names[3] = {"Tuning", "Brightness", "Pressure"};
        info.typeId = ids[index];
        copyAsciiToUtf16(info.title, names[index], 128);
        copyAsciiToUtf16(info.shortTitle, names[index], 128);
        copyAsciiToUtf16(info.units, index == 0 ? "semitones" : "", 128);
        info.valueDesc = {index == 0 ? 0.5 : 0.0, 0.0, 1.0, 0};
        info.associatedParameterId = 0;
        info.flags = index == 0 ? 1 : 0; // kIsBipolar
        return kResultOk;
    }

    tresult SMTG_API getNoteExpressionStringByValue(int32, int16, uint32 id, ParamValue v, String128 string) override {
        char buf[32];
        if (id == kTuningTypeID) std::snprintf(buf, sizeof(buf), "%+.2f st", (v - 0.5) * 240.0);
        else std::snprintf(buf, sizeof(buf), "%.0f%%", v * 100.0);
        copyAsciiToUtf16(string, buf, 128);
        return kResultOk;
    }

    tresult SMTG_API getNoteExpressionValueByString(int32, int16, uint32, const char16*, ParamValue&) override {
        return kNotImplemented;
    }

    uint32 SMTG_API getTailSamples() override {
        return 48000;
    }

    // --- IEditController ---
    tresult SMTG_API setComponentState(IBStream* state) override {
        return setState(state);
    }

    int32 SMTG_API getParameterCount() override {
        return static_cast<int32>(phs::kNumParams);
    }

    tresult SMTG_API getParameterInfo(int32 paramIndex, ParameterInfo& info) override {
        if (paramIndex < 0 || paramIndex >= static_cast<int32>(phs::kNumParams)) {
            return kInvalidArgument;
        }
        const auto& meta = phs::kParamMetadata[paramIndex];
        std::memset(&info, 0, sizeof(info));
        info.id = meta.id;
        copyAsciiToUtf16(info.title, meta.name, 128);
        copyAsciiToUtf16(info.shortTitle, meta.shortName, 128);
        copyAsciiToUtf16(info.units, meta.unit, 128);
        info.stepCount = meta.steps;
        info.defaultNormalizedValue = static_cast<ParamValue>(meta.defaultValue);
        info.unitId = 0;
        info.flags = ParameterInfo::kCanAutomate | (meta.steps > 0 ? ParameterInfo::kIsList : 0);
        return kResultOk;
    }

    tresult SMTG_API getParamStringByValue(ParamID id, ParamValue valueNormalized, String128 string) override {
        if (id >= phs::kNumParams) return kInvalidArgument;
        char buf[64];
        phs::formatParamDisplay(id, static_cast<float>(valueNormalized), buf, sizeof(buf));
        copyAsciiToUtf16(string, buf, 128);
        return kResultOk;
    }

    tresult SMTG_API getParamValueByString(ParamID id, char16* string, ParamValue& valueNormalized) override {
        if (id >= phs::kNumParams || !string) return kInvalidArgument;
        char ascii[64];
        size_t i = 0;
        while (i + 1 < sizeof(ascii) && string[i] != 0) {
            ascii[i] = static_cast<char>(string[i] & 0x7F);
            ++i;
        }
        ascii[i] = '\0';
        valueNormalized = std::clamp(std::atof(ascii) / 100.0, 0.0, 1.0);
        return kResultOk;
    }

    ParamValue SMTG_API normalizedParamToPlain(ParamID, ParamValue valueNormalized) override {
        return valueNormalized;
    }

    ParamValue SMTG_API plainParamToNormalized(ParamID, ParamValue plainValue) override {
        return plainValue;
    }

    ParamValue SMTG_API getParamNormalized(ParamID id) override {
        return static_cast<ParamValue>(engine_.getParamNormalized(id));
    }

    tresult SMTG_API setParamNormalized(ParamID id, ParamValue value) override {
        if (id >= phs::kNumParams) return kInvalidArgument;
        engine_.setParamNormalized(id, static_cast<float>(value));
        return kResultOk;
    }

    tresult SMTG_API setComponentHandler(IComponentHandler* handler) override {
        if (componentHandler_ == handler) return kResultOk;
        if (componentHandler_) componentHandler_->release();
        componentHandler_ = handler;
        if (componentHandler_) componentHandler_->addRef();
        return kResultOk;
    }

    IPlugView* SMTG_API createView(const char* name) override {
        if (name && std::strcmp(name, "editor") == 0) {
            return new PhsPlugView(&engine_, componentHandler_);
        }
        return nullptr;
    }

private:
    std::atomic<uint32> refCount_{1};
    phs::PhsSynthEngine engine_{};
    TimedEvent timed_[kMaxTimedEvents];
    int numTimed_ = 0;
    SpeakerArrangement outArr_ = kSpeakerArrStereo;
    IComponentHandler* componentHandler_ = nullptr;
};

class PhsPluginFactory : public IPluginFactory3 {
public:
    tresult SMTG_API queryInterface(const TUID _iid, void** obj) override {
        if (!obj) return kInvalidArgument;
        if (iidEqual(_iid, kFUnknownIID) ||
            iidEqual(_iid, kIPluginFactoryIID) ||
            iidEqual(_iid, kIPluginFactory2IID) ||
            iidEqual(_iid, kIPluginFactory3IID)) {
            addRef();
            *obj = static_cast<IPluginFactory3*>(this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }

    uint32 SMTG_API addRef() override { return 1; }
    uint32 SMTG_API release() override { return 1; }

    tresult SMTG_API getFactoryInfo(PFactoryInfo* info) override {
        if (!info) return kInvalidArgument;
        std::memset(info, 0, sizeof(*info));
        std::snprintf(info->vendor, sizeof(info->vendor), "mtyas");
        std::snprintf(info->url, sizeof(info->url), "https://mtyas.com");
        std::snprintf(info->email, sizeof(info->email), "contact@mtyas.com");
        info->flags = PFactoryInfo::kUnicode;
        return kResultOk;
    }

    int32 SMTG_API countClasses() override {
        return 1;
    }

    tresult SMTG_API getClassInfo(int32 index, PClassInfo* info) override {
        if (index != 0 || !info) return kInvalidArgument;
        std::memset(info, 0, sizeof(*info));
        std::memcpy(info->cid, kPhsProcessorCID, sizeof(TUID));
        info->cardinality = PClassInfo::kManyInstances;
        std::snprintf(info->category, sizeof(info->category), "Audio Module Class");
        std::snprintf(info->name, sizeof(info->name), "electropiano");
        return kResultOk;
    }

    tresult SMTG_API getClassInfo2(int32 index, PClassInfo2* info) override {
        if (index != 0 || !info) return kInvalidArgument;
        std::memset(info, 0, sizeof(*info));
        std::memcpy(info->cid, kPhsProcessorCID, sizeof(TUID));
        info->cardinality = PClassInfo::kManyInstances;
        std::snprintf(info->category, sizeof(info->category), "Audio Module Class");
        std::snprintf(info->name, sizeof(info->name), "electropiano");
        info->classFlags = 0;
        std::snprintf(info->subCategories, sizeof(info->subCategories), "Instrument|Synth|Piano");
        std::snprintf(info->vendor, sizeof(info->vendor), "mtyas");
        std::snprintf(info->version, sizeof(info->version), "%s", PHS_VERSION_STRING);
        std::snprintf(info->sdkVersion, sizeof(info->sdkVersion), "VST 3.7.8");
        return kResultOk;
    }

    tresult SMTG_API getClassInfoUnicode(int32 index, PClassInfoW* info) override {
        if (index != 0 || !info) return kInvalidArgument;
        std::memset(info, 0, sizeof(*info));
        std::memcpy(info->cid, kPhsProcessorCID, sizeof(TUID));
        info->cardinality = PClassInfo::kManyInstances;
        std::snprintf(info->category, sizeof(info->category), "Audio Module Class");
        copyAsciiToUtf16(info->name, "electropiano", 64);
        info->classFlags = 0;
        std::snprintf(info->subCategories, sizeof(info->subCategories), "Instrument|Synth|Piano");
        copyAsciiToUtf16(info->vendor, "mtyas", 64);
        copyAsciiToUtf16(info->version, PHS_VERSION_STRING, 64);
        copyAsciiToUtf16(info->sdkVersion, "VST 3.7.8", 64);
        return kResultOk;
    }

    tresult SMTG_API createInstance(const char* cid, const char* _iid, void** obj) override {
        if (!cid || !_iid || !obj) return kInvalidArgument;
        if (iidEqual(reinterpret_cast<const uint8*>(cid), kPhsProcessorCID)) {
            auto* comp = new PhsVst3Component();
            const tresult res = comp->queryInterface(reinterpret_cast<const uint8*>(_iid), obj);
            comp->release();
            return res;
        }
        *obj = nullptr;
        return kNoInterface;
    }

    tresult SMTG_API setHostContext(FUnknown*) override {
        return kResultOk;
    }
};

static PhsPluginFactory gPluginFactory;

} // namespace

#if defined(_WIN32)
#define PHS_VST3_EXPORT __declspec(dllexport)
#else
#define PHS_VST3_EXPORT __attribute__((visibility("default")))
#endif

extern "C" {

PHS_VST3_EXPORT IPluginFactory* SMTG_API GetPluginFactory() {
    return &gPluginFactory;
}

// Module entry points each platform's hosts look for
#if defined(__APPLE__)
PHS_VST3_EXPORT bool bundleEntry(CFBundleRef) { return true; }
PHS_VST3_EXPORT bool bundleExit(void) { return true; }
#elif defined(_WIN32)
PHS_VST3_EXPORT bool InitDll() { return true; }
PHS_VST3_EXPORT bool ExitDll() { return true; }
#else
PHS_VST3_EXPORT bool ModuleEntry(void*) { return true; }
PHS_VST3_EXPORT bool ModuleExit(void) { return true; }
#endif

} // extern "C"
