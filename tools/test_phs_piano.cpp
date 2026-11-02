#include "../src/dsp/PhsSynthEngine.hpp"
#include "../include/clap/clap.h"
#include "../include/vst3/vst3_c_api.h"
#if defined(__APPLE__)
#include <AudioUnit/AudioUnit.h>
#include <dlfcn.h>
#endif
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <fstream>
#include <iostream>
#include <cassert>
#include <filesystem>

namespace {

bool writeStereoWav(const char* filename, const std::vector<float>& L, const std::vector<float>& R, uint32_t sampleRate) {
    if (L.size() != R.size()) return false;
    std::ofstream out(filename, std::ios::binary);
    if (!out) return false;

    const uint32_t numFrames = static_cast<uint32_t>(L.size());
    const uint16_t numChannels = 2;
    const uint16_t bitsPerSample = 16;
    const uint32_t byteRate = sampleRate * numChannels * (bitsPerSample / 8);
    const uint16_t blockAlign = numChannels * (bitsPerSample / 8);
    const uint32_t dataSize = numFrames * blockAlign;
    const uint32_t chunkSize = 36 + dataSize;

    out.write("RIFF", 4);
    out.write(reinterpret_cast<const char*>(&chunkSize), 4);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    const uint32_t subchunk1Size = 16;
    const uint16_t audioFormat = 1; // PCM
    out.write(reinterpret_cast<const char*>(&subchunk1Size), 4);
    out.write(reinterpret_cast<const char*>(&audioFormat), 2);
    out.write(reinterpret_cast<const char*>(&numChannels), 2);
    out.write(reinterpret_cast<const char*>(&sampleRate), 4);
    out.write(reinterpret_cast<const char*>(&byteRate), 4);
    out.write(reinterpret_cast<const char*>(&blockAlign), 2);
    out.write(reinterpret_cast<const char*>(&bitsPerSample), 2);
    out.write("data", 4);
    out.write(reinterpret_cast<const char*>(&dataSize), 4);

    for (uint32_t i = 0; i < numFrames; ++i) {
        const float cL = std::clamp(L[i], -1.0f, 1.0f);
        const float cR = std::clamp(R[i], -1.0f, 1.0f);
        const int16_t sL = static_cast<int16_t>(std::lrint(cL * 32767.0f));
        const int16_t sR = static_cast<int16_t>(std::lrint(cR * 32767.0f));
        out.write(reinterpret_cast<const char*>(&sL), 2);
        out.write(reinterpret_cast<const char*>(&sR), 2);
    }
    return true;
}

// 1. Verify Port-Hamiltonian Passivity & Discrete Power Balance
bool verifyPhsPassivity() {
    std::printf("[TEST 1] Verifying Port-Hamiltonian Passivity & Discrete Energy Conservation...\n");
    phs::PhsVoice voice;
    const float fs = 48000.0f;
    const float dt = 1.0f / fs;
    voice.prepare(fs);

    float params[phs::kNumParams];
    for (uint32_t i = 0; i < phs::kNumParams; ++i) {
        params[i] = phs::kExpandedFactoryPresets[0].values[i];
    }

    // Strike middle C4 (MIDI 60) at fortissimo velocity (0.95)
    voice.noteOn(60, 0.95f, params);

    double prevH = 0.0;
    float maxEnergyResidual = 0.0f;
    int nonPassiveViolations = 0;
    float peakOut = 0.0f;
    int contactSamples = 0;
    std::vector<float> noteBuf(48000, 0.0f);

    for (int n = 0; n < 48000; ++n) {
        const float y = voice.processSample(params);
        noteBuf[n] = y;
        if (!std::isfinite(y)) {
            std::fprintf(stderr, "  FAIL: Non-finite output at sample %d\n", n);
            return false;
        }
        peakOut = std::max(peakOut, std::fabs(y));
        const auto& tel = voice.getTelemetry();
        if (tel.hammerCompression > 0.0f) {
            ++contactSamples;
        }

        // H includes the hammer and felt, so the closed system obeys the discrete power
        // balance dH/dt + P_diss == 0 on EVERY sample, through contact and separation
        // (discrete-gradient scheme). Residual is measured against the stored energy.
        if (n > 0) {
            const double dH = tel.totalHamiltonian - prevH;
            if (dH > 1e-9 * std::max(1e-30, prevH)) {
                ++nonPassiveViolations;
            }
            const double residual = std::fabs(dH + dt * tel.dissipatedPower) / std::max(1e-30, prevH);
            maxEnergyResidual = std::max(maxEnergyResidual, static_cast<float>(residual));
        }
        prevH = tel.totalHamiltonian;
    }

    // Measure DFT magnitude at sub-bass (40 Hz), fundamental f0 (261.63 Hz), 2*f0, 3*f0, and chime (6.267*f0)
    auto measureFreqDb = [&](float freqHz, int startSample, int len) {
        double re = 0.0, im = 0.0;
        for (int i = 0; i < len; ++i) {
            const double w = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / (len - 1)));
            const double s = noteBuf[startSample + i] * w;
            const double ang = 2.0 * M_PI * freqHz * i / fs;
            re += s * std::cos(ang);
            im -= s * std::sin(ang);
        }
        const double mag = std::sqrt(re * re + im * im) / (0.25 * len);
        return 20.0 * std::log10(std::max(1e-9, mag));
    };

    const double f0 = 261.6256;
    const double subBassDb = measureFreqDb(45.0, 240, 16384);
    const double fundDb    = measureFreqDb(f0, 240, 16384);
    const double harm2Db   = measureFreqDb(2.0 * f0, 240, 16384);
    const double harm3Db   = measureFreqDb(3.0 * f0, 240, 16384);
    const double chimeDb   = measureFreqDb(6.2669 * f0, 0, 8192);
    const double sustainDb = measureFreqDb(f0, 24000, 16384);

    std::printf("  -> Hammer Contact Duration: %.2f ms (%d samples)\n", contactSamples * 1000.0f / fs, contactSamples);
    std::printf("  -> Peak Output: %.4f | Sustain f0 @ 0.5s: %.1f dBFS\n", peakOut, sustainDb);
    std::printf("  -> Spectrum: f0=%.1f dB | 2*f0=%.1f dB | 3*f0=%.1f dB | Chime(6.27f0)=%.1f dB | SubBass(45Hz)=%.1f dB\n",
                fundDb, harm2Db, harm3Db, chimeDb, subBassDb);
    std::printf("  -> Post-impact non-passive violations: %d | Max relative power residual: %.2e\n",
                nonPassiveViolations, maxEnergyResidual);
    return (nonPassiveViolations == 0 && maxEnergyResidual < 1e-6f && peakOut > 0.15f && peakOut < 4.0f &&
            (fundDb - subBassDb) > 25.0);
}

#if defined(__APPLE__)
// 2. Test compiled CLAP bundle via dlopen
bool testClapBundle(const char* dylibPath) {
    std::printf("[TEST 2] Testing CLAP Bundle (%s)...\n", dylibPath);
    void* handle = dlopen(dylibPath, RTLD_LOCAL | RTLD_LAZY);
    if (!handle) {
        std::fprintf(stderr, "  FAIL: dlopen failed: %s\n", dlerror());
        return false;
    }

    const auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(handle, "clap_entry"));
    if (!entry || !entry->init || !entry->get_factory) {
        std::fprintf(stderr, "  FAIL: clap_entry symbol missing\n");
        dlclose(handle);
        return false;
    }

    entry->init(dylibPath);
    const auto* factory = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!factory || factory->get_plugin_count(factory) < 1) {
        std::fprintf(stderr, "  FAIL: CLAP factory invalid\n");
        dlclose(handle);
        return false;
    }

    const clap_plugin_descriptor_t* desc = factory->get_plugin_descriptor(factory, 0);
    std::printf("  -> Found CLAP Plugin: %s (%s) v%s\n", desc->name, desc->id, desc->version);

    clap_host_t host{};
    host.clap_version = CLAP_VERSION;
    host.name = "PyPHS Test Host";
    host.vendor = "PyPHS";
    host.url = "https://github.com/pyphs/pyphs";
    host.version = "1.0.0";

    const clap_plugin_t* plug = factory->create_plugin(factory, &host, desc->id);
    if (!plug || !plug->init(plug) || !plug->activate(plug, 48000.0, 64, 1024)) {
        std::fprintf(stderr, "  FAIL: CLAP plugin init/activate failed\n");
        dlclose(handle);
        return false;
    }
    plug->start_processing(plug);

    // Send a CLAP Note-On event and process 1024 frames
    clap_event_note_t noteEv{};
    noteEv.header.size = sizeof(clap_event_note_t);
    noteEv.header.time = 0;
    noteEv.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    noteEv.header.type = CLAP_EVENT_NOTE_ON;
    noteEv.key = 64; // E4
    noteEv.velocity = 0.85;

    clap_input_events_t inEvents{};
    inEvents.ctx = &noteEv;
    inEvents.size = [](const clap_input_events_t*) -> uint32_t { return 1; };
    inEvents.get = [](const clap_input_events_t* list, uint32_t) -> const clap_event_header_t* {
        return static_cast<const clap_event_header_t*>(list->ctx);
    };

    std::vector<float> outL(1024, 0.0f);
    std::vector<float> outR(1024, 0.0f);
    float* chans[2] = {outL.data(), outR.data()};

    clap_audio_buffer_t outBuf{};
    outBuf.data32 = chans;
    outBuf.channel_count = 2;

    clap_process_t proc{};
    proc.frames_count = 1024;
    proc.audio_outputs = &outBuf;
    proc.audio_outputs_count = 1;
    proc.in_events = &inEvents;

    plug->process(plug, &proc);

    float rms = 0.0f;
    for (float s : outL) rms += s * s;
    rms = std::sqrt(rms / outL.size());
    std::printf("  -> CLAP Render 1024 frames RMS: %.5f\n", rms);

    plug->stop_processing(plug);
    plug->deactivate(plug);
    plug->destroy(plug);
    entry->deinit();
    dlclose(handle);
    return rms > 1e-4f;
}

// 3. Test compiled VST3 bundle via dlopen
bool testVst3Bundle(const char* dylibPath) {
    std::printf("[TEST 3] Testing VST3 Bundle (%s)...\n", dylibPath);
    void* handle = dlopen(dylibPath, RTLD_LOCAL | RTLD_LAZY);
    if (!handle) {
        std::fprintf(stderr, "  FAIL: dlopen failed: %s\n", dlerror());
        return false;
    }

    using GetFactoryProc = Steinberg::IPluginFactory* (*)();
    auto getFactory = reinterpret_cast<GetFactoryProc>(dlsym(handle, "GetPluginFactory"));
    if (!getFactory) {
        std::fprintf(stderr, "  FAIL: GetPluginFactory symbol missing\n");
        dlclose(handle);
        return false;
    }

    Steinberg::IPluginFactory* factory = getFactory();
    if (!factory || factory->countClasses() < 1) {
        std::fprintf(stderr, "  FAIL: VST3 factory invalid\n");
        dlclose(handle);
        return false;
    }

    Steinberg::PClassInfo ci{};
    factory->getClassInfo(0, &ci);
    std::printf("  -> Found VST3 Class: %s (%s)\n", ci.name, ci.category);

    Steinberg::Vst::IComponent* comp = nullptr;
    if (factory->createInstance(reinterpret_cast<const char*>(ci.cid),
                                reinterpret_cast<const char*>(Steinberg::kIComponentIID),
                                reinterpret_cast<void**>(&comp)) != Steinberg::kResultOk || !comp) {
        std::fprintf(stderr, "  FAIL: createInstance(IComponent) failed\n");
        dlclose(handle);
        return false;
    }
    comp->initialize(nullptr);

    Steinberg::Vst::IAudioProcessor* proc = nullptr;
    comp->queryInterface(Steinberg::kIAudioProcessorIID, reinterpret_cast<void**>(&proc));
    Steinberg::Vst::IEditController* ctrl = nullptr;
    comp->queryInterface(Steinberg::kIEditControllerIID, reinterpret_cast<void**>(&ctrl));

    if (!proc || !ctrl) {
        std::fprintf(stderr, "  FAIL: queryInterface(IAudioProcessor / IEditController) failed\n");
        comp->release();
        dlclose(handle);
        return false;
    }

    std::printf("  -> VST3 EditController Parameter Count: %d\n", ctrl->getParameterCount());

    Steinberg::Vst::ProcessSetup setup{0, Steinberg::Vst::kSample32, 1024, 48000.0};
    proc->setupProcessing(setup);
    comp->setActive(1);
    proc->setProcessing(1);

    // Prepare a single NoteOn event in a mock IEventList
    class SimpleEventList : public Steinberg::Vst::IEventList {
    public:
        Steinberg::tresult queryInterface(const Steinberg::TUID, void**) override { return Steinberg::kNoInterface; }
        Steinberg::uint32 addRef() override { return 1; }
        Steinberg::uint32 release() override { return 1; }
        Steinberg::int32 getEventCount() override { return 1; }
        Steinberg::tresult getEvent(Steinberg::int32, Steinberg::Vst::Event& e) override {
            std::memset(&e, 0, sizeof(e));
            e.type = Steinberg::Vst::Event::kNoteOnEvent;
            e.noteOn.pitch = 67; // G4
            e.noteOn.velocity = 0.85f;
            return Steinberg::kResultOk;
        }
        Steinberg::tresult addEvent(Steinberg::Vst::Event&) override { return Steinberg::kResultOk; }
    } evList;

    std::vector<float> outL(1024, 0.0f);
    std::vector<float> outR(1024, 0.0f);
    float* chans[2] = {outL.data(), outR.data()};

    Steinberg::Vst::AudioBusBuffers outBus{};
    outBus.numChannels = 2;
    outBus.channelBuffers32 = chans;

    Steinberg::Vst::ProcessData data{};
    data.numSamples = 1024;
    data.numOutputs = 1;
    data.outputs = &outBus;
    data.inputEvents = &evList;

    proc->process(data);

    float rms = 0.0f;
    for (float s : outL) rms += s * s;
    rms = std::sqrt(rms / outL.size());
    std::printf("  -> VST3 Render 1024 frames RMS: %.5f\n", rms);

    ctrl->release();
    proc->release();
    comp->terminate();
    comp->release();
    dlclose(handle);
    return rms > 1e-4f;
}

// 4. Test compiled AudioUnit (.component) bundle via factory function
bool testAuBundle(const char* dylibPath) {
    std::printf("[TEST 4] Testing AudioUnit (.component) Bundle (%s)...\n", dylibPath);
    void* handle = dlopen(dylibPath, RTLD_LOCAL | RTLD_LAZY);
    if (!handle) {
        std::fprintf(stderr, "  FAIL: dlopen failed: %s\n", dlerror());
        return false;
    }

    using AUFactoryProc = AudioComponentPlugInInterface* (*)(const AudioComponentDescription*);
    auto factory = reinterpret_cast<AUFactoryProc>(dlsym(handle, "PhsElectricPianoAUFactory"));
    if (!factory) {
        std::fprintf(stderr, "  FAIL: PhsElectricPianoAUFactory symbol missing\n");
        dlclose(handle);
        return false;
    }

    AudioComponentDescription desc{'aumu', 'PhsE', 'PyPH', 0, 0};
    AudioComponentPlugInInterface* plug = factory(&desc);
    if (!plug || !plug->Open || !plug->Lookup) {
        std::fprintf(stderr, "  FAIL: AU Factory returned null\n");
        dlclose(handle);
        return false;
    }

    AudioComponentInstance dummyInstance = reinterpret_cast<AudioComponentInstance>(plug);
    plug->Open(plug, dummyInstance);

    using InitProc = OSStatus (*)(void*);
    using MidiProc = OSStatus (*)(void*, UInt32, UInt32, UInt32, UInt32);
    using RenderProc = OSStatus (*)(void*, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32, UInt32, AudioBufferList*);

    auto initFn = reinterpret_cast<InitProc>(plug->Lookup(kAudioUnitInitializeSelect));
    auto midiFn = reinterpret_cast<MidiProc>(plug->Lookup(kMusicDeviceMIDIEventSelect));
    auto renderFn = reinterpret_cast<RenderProc>(plug->Lookup(kAudioUnitRenderSelect));

    initFn(plug);
    midiFn(plug, 0x90, 60, 110, 0);

    std::vector<float> outL(1024, 0.0f);
    std::vector<float> outR(1024, 0.0f);

    AudioBufferList* abl = static_cast<AudioBufferList*>(std::calloc(1, sizeof(AudioBufferList) + sizeof(AudioBuffer)));
    abl->mNumberBuffers = 2;
    abl->mBuffers[0].mNumberChannels = 1;
    abl->mBuffers[0].mDataByteSize = 1024 * sizeof(float);
    abl->mBuffers[0].mData = outL.data();
    abl->mBuffers[1].mNumberChannels = 1;
    abl->mBuffers[1].mDataByteSize = 1024 * sizeof(float);
    abl->mBuffers[1].mData = outR.data();

    AudioTimeStamp ts{};
    AudioUnitRenderActionFlags flags = 0;
    renderFn(plug, &flags, &ts, 0, 1024, abl);

    float rms = 0.0f;
    for (float s : outL) rms += s * s;
    rms = std::sqrt(rms / outL.size());
    std::printf("  -> AudioUnit Render 1024 frames RMS: %.5f\n", rms);

    std::free(abl);
    plug->Close(plug);
    dlclose(handle);
    return rms > 1e-4f;
}

// 5. Render musical showcase WAV files for Rhodes Mk I, Wurlitzer 200A, and Dyno-My-Piano
#endif // __APPLE__

void renderShowcaseDemos() {
    std::printf("[TEST 5] Rendering Showcase Audio Demos (48 kHz Stereo WAV)...\n");
    std::error_code ec;
    std::filesystem::create_directories("renders", ec);
    const uint32_t sr = 48000;
    const int totalSeconds = 6;
    const size_t totalFrames = sr * totalSeconds;

    struct DemoSpec {
        int presetIdx;
        const char* filename;
    };

    const DemoSpec specs[] = {
        {0, "renders/rhodes_mk1_suitcase.wav"},
        {8, "renders/wurlitzer_200a_openwurli.wav"},
        {11, "renders/wurlitzer_200a_cranked.wav"},
        {15, "renders/dyno_la_bell.wav"},
        {19, "renders/hybrid_tine_into_200a.wav"},
        {20, "renders/hybrid_reed_into_suitcase.wav"},
        {23, "renders/mutron_funk.wav"},
        {25, "renders/ambient_tine_pad.wav"}
    };

    for (const auto& sp : specs) {
        phs::PhsSynthEngine engine;
        engine.prepare(static_cast<float>(sr));
        engine.loadPreset(sp.presetIdx);

        std::vector<float> bufL(totalFrames, 0.0f);
        std::vector<float> bufR(totalFrames, 0.0f);

        // Schedule a rich jazz progression:
        // t = 0.0s: Cmaj9 warm chord (C3, E3, G3, B3, D4) at mp/mf velocity
        // t = 1.8s: Fmaj9#11 chord (F2, A3, C4, E4, G4, B4) at mf velocity
        // t = 3.5s: Expressive fortissimo bark melody notes (D5, E5, G5, A4) at ff velocity (0.96)
        const int block = 256;
        for (size_t offset = 0; offset + block <= totalFrames; offset += block) {
            if (offset == 0) {
                engine.noteOn(48, 0.58f);
                engine.noteOn(55, 0.62f);
                engine.noteOn(59, 0.65f);
                engine.noteOn(62, 0.68f);
                engine.noteOn(64, 0.72f);
            } else if (offset == static_cast<size_t>(1.65 * sr) / block * block) {
                engine.noteOff(48);
                engine.noteOff(55);
                engine.noteOff(59);
                engine.noteOff(62);
                engine.noteOff(64);
            } else if (offset == static_cast<size_t>(1.80 * sr) / block * block) {
                engine.noteOn(41, 0.72f);
                engine.noteOn(57, 0.70f);
                engine.noteOn(60, 0.74f);
                engine.noteOn(64, 0.78f);
                engine.noteOn(67, 0.82f);
            } else if (offset == static_cast<size_t>(3.40 * sr) / block * block) {
                // Hard fortissimo strike to demonstrate nonlinear pickup bark!
                engine.noteOn(74, 0.96f);
            } else if (offset == static_cast<size_t>(3.75 * sr) / block * block) {
                engine.noteOff(74);
                engine.noteOn(76, 0.98f);
            } else if (offset == static_cast<size_t>(4.15 * sr) / block * block) {
                engine.noteOff(76);
                engine.noteOn(72, 0.92f);
            }
            engine.processBlock(bufL.data() + offset, bufR.data() + offset, block);
        }

        float peak = 0.0f;
        for (size_t i = 0; i < totalFrames; ++i) {
            peak = std::max(peak, std::max(std::fabs(bufL[i]), std::fabs(bufR[i])));
        }
        writeStereoWav(sp.filename, bufL, bufR, sr);
        std::printf("  -> Wrote %s (preset: \"%s\", peak: %.3f)\n",
                    sp.filename,
                    phs::kExpandedFactoryPresets[sp.presetIdx].name.c_str(),
                    peak);
    }
}

// ---------------------------------------------------------------------------
// v1.4 features: MPE expression, modulation matrix, oversampling, tempo sync, state compatibility
// ---------------------------------------------------------------------------
float autocorrFreq(const std::vector<float>& x, size_t start, size_t len, float fs) {
    // Normalised autocorrelation; the pitch is the shortest lag that is a strong local peak
    const size_t minLag = static_cast<size_t>(fs / 2500.0f), maxLag = static_cast<size_t>(fs / 60.0f);
    std::vector<float> v(maxLag + 2, 0.0f);
    float best = -1e30f;
    for (size_t lag = minLag; lag <= maxLag; ++lag) {
        double acc = 0.0, e0 = 0.0, e1 = 0.0;
        for (size_t i = 0; i + lag < len; ++i) {
            const double a = x[start + i], b = x[start + i + lag];
            acc += a * b; e0 += a * a; e1 += b * b;
        }
        v[lag] = static_cast<float>(acc / std::sqrt(e0 * e1 + 1e-30));
        best = std::max(best, v[lag]);
    }
    for (size_t lag = minLag + 1; lag < maxLag; ++lag) {
        if (v[lag] >= 0.85f * best && v[lag] >= v[lag - 1] && v[lag] >= v[lag + 1]) return fs / static_cast<float>(lag);
    }
    return 0.0f;
}

std::vector<float> renderMono(phs::PhsSynthEngine& e, int frames) {
    std::vector<float> l(frames), r(frames);
    for (int done = 0; done < frames; done += 256) e.processBlock(l.data() + done, r.data() + done, std::min(256, frames - done));
    return l;
}

float rmsOf(const std::vector<float>& x, size_t a, size_t n) {
    double acc = 0.0;
    for (size_t i = a; i < a + n && i < x.size(); ++i) acc += static_cast<double>(x[i]) * x[i];
    return static_cast<float>(std::sqrt(acc / std::max<size_t>(1, n)));
}

void bendMidi(phs::PhsSynthEngine& e, int channel, float norm) {
    const int v14 = 8192 + static_cast<int>(std::lround(norm * 8192.0f));
    e.handleMidiEvent(static_cast<uint8_t>(0xE0 | channel), v14 & 0x7F, (v14 >> 7) & 0x7F);
}

bool testExpression() {
    std::printf("[TEST 6] Per-note expression (MPE pitch bend, pressure, channel handling)...\n");
    bool ok = true;
    for (int reed = 0; reed < 2; ++reed) {
        phs::PhsSynthEngine e;
        e.prepare(48000.0f);
        e.setParamNormalized(phs::kParamResonator, reed ? 1.0f : 0.0f);
        e.setParamNormalized(phs::kParamMpeMode, 1.0f);
        e.setParamNormalized(phs::kParamBendRange, 1.0f); // 48 st
        e.setParamNormalized(phs::kParamReverbMix, 0.0f);
        e.setParamNormalized(phs::kParamTremoloDepth, 0.0f);

        renderMono(e, 1024);
        bendMidi(e, 1, 0.25f); // +12 st on channel 2 (index 1)
        e.handleMidiEvent(0x91, 60, 100);
        auto bent = renderMono(e, 24000);
        const float fBent = autocorrFreq(bent, 6000, 4096, 48000.0f);
        e.handleMidiEvent(0x81, 60, 0);
        renderMono(e, 24000);

        // A different member channel is unaffected by channel 2's bend
        e.handleMidiEvent(0x92, 60, 100);
        auto plain = renderMono(e, 24000);
        const float fPlain = autocorrFreq(plain, 6000, 4096, 48000.0f);

        // Bending mid-note retunes the sounding voice
        e.handleMidiEvent(0x82, 60, 0);
        renderMono(e, 24000);
        bendMidi(e, 3, 0.0f);
        e.handleMidiEvent(0x93, 60, 100);
        renderMono(e, 12000);
        bendMidi(e, 3, 0.25f);
        renderMono(e, 4800);
        auto glided = renderMono(e, 12000);
        const float fGlide = autocorrFreq(glided, 2000, 4096, 48000.0f);

        std::printf("  -> %s: bent %.1f Hz | other channel %.1f Hz | mid-note bend %.1f Hz (note 60 = 261.6 Hz)\n",
                    reed ? "reed" : "tine", fBent, fPlain, fGlide);
        if (std::fabs(fBent / 523.25f - 1.0f) > 0.03f) { std::fprintf(stderr, "  !! bent pitch off\n"); ok = false; }
        if (std::fabs(fPlain / 261.63f - 1.0f) > 0.03f) { std::fprintf(stderr, "  !! bend leaked to another channel\n"); ok = false; }
        if (std::fabs(fGlide / 523.25f - 1.0f) > 0.03f) { std::fprintf(stderr, "  !! mid-note bend did not retune\n"); ok = false; }
    }

    // Pressure pulls the magnetic pickup closer, which changes the output level
    {
        phs::PhsSynthEngine e;
        e.prepare(48000.0f);
        e.setParamNormalized(phs::kParamMpeMode, 1.0f);
        e.setParamNormalized(phs::kParamPressureAmt, 1.0f);
        e.setParamNormalized(phs::kParamReverbMix, 0.0f);
        float level[2];
        for (int p = 0; p < 2; ++p) {
            e.handleMidiEvent(0xD1, p ? 127 : 0, 0);
            e.handleMidiEvent(0x91, 64, 100);
            level[p] = rmsOf(renderMono(e, 12000), 2000, 8000);
            e.handleMidiEvent(0x81, 64, 0);
            renderMono(e, 24000);
        }
        std::printf("  -> pressure 0: rms %.4f | pressure max: rms %.4f\n", level[0], level[1]);
        if (std::fabs(level[1] - level[0]) < 0.05f * level[0]) { std::fprintf(stderr, "  !! pressure had no effect\n"); ok = false; }
    }

    // With MPE off the channel is ignored: a note-on on channel 6 is released by a note-off on channel 1
    {
        phs::PhsSynthEngine e;
        e.prepare(48000.0f);
        e.handleMidiEvent(0x95, 60, 100);
        renderMono(e, 1024);
        e.handleMidiEvent(0x80, 60, 0);
        renderMono(e, 48000);
        if (e.isNoteActive(60)) { std::fprintf(stderr, "  !! note stuck with MPE off\n"); ok = false; }
    }
    return ok;
}

bool testModulationMatrix() {
    std::printf("[TEST 7] Modulation matrix (LFO -> Volume, Mod Wheel -> Env Filter)...\n");
    bool ok = true;
    auto destNorm = [](uint32_t param) {
        for (int i = 1; i < phs::kNumModDests; ++i) if (phs::kModDestParam[i] == param) return i / float(phs::kNumModDests - 1);
        return 0.0f;
    };
    float modulatedSpread = 0.0f, plainSpread = 0.0f;
    for (int pass = 0; pass < 2; ++pass) {
        phs::PhsSynthEngine e;
        e.prepare(48000.0f);
        e.setParamNormalized(phs::kParamReverbMix, 0.0f);
        e.setParamNormalized(phs::kParamTremoloDepth, 0.0f);
        e.setParamNormalized(phs::kParamTineDecay, 1.0f);
        if (pass == 1) {
            e.setParamNormalized(phs::kParamLfo1Rate, std::log(5.0f / 0.05f) / std::log(800.0f)); // 5 Hz
            e.setParamNormalized(phs::kParamMod1Src, phs::kSrcLfo1 / float(phs::kNumModSources - 1));
            e.setParamNormalized(phs::kParamMod1Dst, destNorm(phs::kParamMasterVolume));
            e.setParamNormalized(phs::kParamMod1Amt, 0.75f); // +50 %
        }
        e.handleMidiEvent(0x90, 57, 110);
        auto out = renderMono(e, 48000);
        float lo = 1e9f, hi = 0.0f;
        for (size_t a = 6000; a + 960 < out.size(); a += 960) {
            const float r = rmsOf(out, a, 960);
            lo = std::min(lo, r); hi = std::max(hi, r);
        }
        (pass ? modulatedSpread : plainSpread) = hi / std::max(lo, 1e-9f);
    }
    std::printf("  -> level max/min over 1 s: unmodulated %.2fx | LFO on Volume %.2fx\n", plainSpread, modulatedSpread);
    if (modulatedSpread < 1.25f * plainSpread) { std::fprintf(stderr, "  !! LFO modulation not audible\n"); ok = false; }

    // Mod wheel as a source, destination checked through the 'effective' output: Volume -50 % halves the level
    {
        float lvl[2];
        for (int pass = 0; pass < 2; ++pass) {
            phs::PhsSynthEngine e;
            e.prepare(48000.0f);
            e.setParamNormalized(phs::kParamReverbMix, 0.0f);
            e.setParamNormalized(phs::kParamTremoloDepth, 0.0f);
            e.setParamNormalized(phs::kParamMod1Src, phs::kSrcModWheel / float(phs::kNumModSources - 1));
            e.setParamNormalized(phs::kParamMod1Dst, destNorm(phs::kParamMasterVolume));
            e.setParamNormalized(phs::kParamMod1Amt, 0.0f); // -100 %
            e.handleMidiEvent(0xB0, 1, pass ? 127 : 0);
            e.handleMidiEvent(0x90, 60, 100);
            lvl[pass] = rmsOf(renderMono(e, 14000), 4000, 8000);
        }
        std::printf("  -> mod wheel 0: rms %.4f | mod wheel max (-100%% volume): rms %.5f\n", lvl[0], lvl[1]);
        if (!(lvl[1] < 0.05f * lvl[0])) { std::fprintf(stderr, "  !! mod wheel routing failed\n"); ok = false; }
    }
    return ok;
}

bool testOversamplingAndSync() {
    std::printf("[TEST 8] Oversampled preamp and tempo-synced echo...\n");
    bool ok = true;
    const float fs = 48000.0f;

    // Aliasing: a 9 kHz sine driven hard produces a 3rd harmonic at 27 kHz which folds to 21 kHz
    float alias[3];
    for (int q = 0; q < 3; ++q) {
        phs::PhsEffectsChain fx;
        fx.prepare(fs);
        float p[phs::kNumParams];
        for (uint32_t i = 0; i < phs::kNumParams; ++i) p[i] = phs::kParamMetadata[i].defaultValue;
        p[phs::kParamPreampDrive] = 1.0f;
        p[phs::kParamReverbMix] = 0.0f;
        p[phs::kParamTremoloDepth] = 0.0f;
        p[phs::kParamBassBoost] = p[phs::kParamTrebleBoost] = 0.5f;
        p[phs::kParamQuality] = q / 2.0f;
        const int n = 16384;
        double re = 0.0, im = 0.0;
        for (int i = 0; i < n; ++i) {
            float l, r;
            fx.processStereoSample(0.35f * std::sin(2.0f * 3.14159265f * 9000.0f * i / fs), p, 0.0f, l, r);
            if (i >= 4096) { // Goertzel-style single bin at 21 kHz
                const double ph = 2.0 * 3.14159265358979 * 21000.0 * i / fs;
                re += l * std::cos(ph); im += l * std::sin(ph);
            }
        }
        alias[q] = static_cast<float>(10.0 * std::log10((re * re + im * im) / ((n - 4096.0) * (n - 4096.0)) + 1e-30));
    }
    std::printf("  -> alias bin level: off %.1f dB | 2x %.1f dB | 4x %.1f dB\n", alias[0], alias[1], alias[2]);
    if (!(alias[1] < alias[0] - 12.0f)) { std::fprintf(stderr, "  !! 2x oversampling does not reduce aliasing\n"); ok = false; }
    if (!(alias[2] <= alias[1] + 3.0f)) { std::fprintf(stderr, "  !! 4x worse than 2x\n"); ok = false; }

    // Echo locked to 1/4 notes: 120 BPM = 500 ms, 90 BPM = 667 ms
    for (float bpm : {120.0f, 90.0f}) {
        phs::PhsEffectsChain fx;
        fx.prepare(fs);
        fx.setTempo(bpm);
        float p[phs::kNumParams];
        for (uint32_t i = 0; i < phs::kNumParams; ++i) p[i] = phs::kParamMetadata[i].defaultValue;
        p[phs::kParamReverbMix] = 0.0f;
        p[phs::kParamTremoloDepth] = 0.0f;
        p[phs::kParamDelayMix] = 1.0f;
        p[phs::kParamDelayFeedback] = 0.0f;
        p[phs::kParamEchoSync] = 3.0f / 7.0f; // 1/4
        std::vector<float> out(48000);
        for (int i = 0; i < 48000; ++i) {
            float l, r;
            fx.processStereoSample(i == 0 ? 0.3f : 0.0f, p, 0.0f, l, r);
            out[i] = l;
        }
        size_t peak = 0;
        for (size_t i = 9000; i < out.size(); ++i) if (std::fabs(out[i]) > std::fabs(out[peak < 9000 ? 9000 : peak])) peak = i;
        const float expected = 60.0f / bpm * fs;
        std::printf("  -> echo at %.0f BPM: repeat at %zu samples (expected %.0f)\n", bpm, peak, expected);
        if (std::fabs(static_cast<float>(peak) - expected) > 300.0f) { std::fprintf(stderr, "  !! echo not locked to tempo\n"); ok = false; }
    }
    return ok;
}

bool testStateCompatibility() {
    std::printf("[TEST 9] State round trip and loading a v3 (pre-expression) state...\n");
    bool ok = true;
    // Hand-built version 3 layout: header, 32 floats, 32 CC bytes, 64 reserved
    std::vector<uint8_t> v3(12 + 4 * 32 + 32 + 64, 0);
    const uint32_t magic = 0x50485345, ver = 3;
    const int32_t preset = 2;
    std::memcpy(&v3[0], &magic, 4); std::memcpy(&v3[4], &ver, 4); std::memcpy(&v3[8], &preset, 4);
    for (int i = 0; i < 32; ++i) {
        const float f = 0.25f + 0.01f * i;
        std::memcpy(&v3[12 + 4 * i], &f, 4);
    }
    for (int i = 0; i < 32; ++i) v3[12 + 128 + i] = static_cast<uint8_t>(-1);
    v3[12 + 128 + phs::kParamReverbMix] = 20; // CC20 -> reverb mix
    phs::PhsSynthEngine e;
    if (!e.setState(v3.data(), v3.size())) { std::fprintf(stderr, "  !! v3 state rejected\n"); return false; }
    if (std::fabs(e.getParamNormalized(phs::kParamDelayFeedback) - (0.25f + 0.01f * phs::kParamDelayFeedback)) > 1e-5f) ok = false;
    if (e.getParamCc(phs::kParamReverbMix) != 20) { std::fprintf(stderr, "  !! v3 CC map lost\n"); ok = false; }
    if (e.getParamNormalized(phs::kParamMpeMode) != 0.0f || e.getParamNormalized(phs::kParamQuality) != 0.5f) { std::fprintf(stderr, "  !! new params not defaulted\n"); ok = false; }

    e.setParamNormalized(phs::kParamMod3Dst, 0.5f);
    e.setParamNormalized(phs::kParamLfo2Shape, 0.75f);
    phs::PhsSynthEngine::SerializedState st{};
    e.getState(st);
    phs::PhsSynthEngine f;
    if (!f.setState(&st, sizeof(st))) ok = false;
    if (f.getParamNormalized(phs::kParamMod3Dst) != 0.5f || f.getParamNormalized(phs::kParamLfo2Shape) != 0.75f || f.getParamCc(phs::kParamReverbMix) != 20) {
        std::fprintf(stderr, "  !! v4 round trip mismatch\n");
        ok = false;
    }
    std::printf("  -> v3 load %s, v4 round trip %s\n", ok ? "ok" : "FAILED", ok ? "ok" : "FAILED");
    return ok;
}

// ---------------------------------------------------------------------------
// v1.5: macros, studio effects, host modulation
// ---------------------------------------------------------------------------
std::vector<float> renderNote(const std::vector<std::pair<uint32_t, float>>& setup, int note, int frames) {
    phs::PhsSynthEngine e;
    e.prepare(48000.0f);
    e.setParamNormalized(phs::kParamReverbMix, 0.0f);
    e.setParamNormalized(phs::kParamTremoloDepth, 0.0f);
    for (const auto& kv : setup) e.setParamNormalized(kv.first, kv.second);
    e.handleMidiEvent(0x90, static_cast<uint8_t>(note), 100);
    return renderMono(e, frames);
}

double correlation(const std::vector<float>& a, const std::vector<float>& b) {
    double ab = 0, aa = 0, bb = 0;
    for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) { ab += a[i] * double(b[i]); aa += a[i] * double(a[i]); bb += b[i] * double(b[i]); }
    return ab / std::sqrt(aa * bb + 1e-30);
}

bool allFinite(const std::vector<float>& x) {
    for (float v : x) if (!std::isfinite(v)) return false;
    return true;
}

bool testMacros() {
    std::printf("[TEST 10] Impossible-instrument macros (material, pickups, gap FM, morph)...\n");
    bool ok = true;
    using namespace phs;

    // Material: carbon rings longer than steel
    const auto steel = renderNote({{kParamMaterial, 0.0f}}, 60, 72000);
    const auto carbon = renderNote({{kParamMaterial, 1.0f}}, 60, 72000);
    const float lateSteel = rmsOf(steel, 48000, 12000), lateCarbon = rmsOf(carbon, 48000, 12000);
    std::printf("  -> late-tail rms: steel %.5f | carbon %.5f\n", lateSteel, lateCarbon);
    if (!(lateCarbon > 1.5f * lateSteel)) { std::fprintf(stderr, "  !! material did not lengthen the ring\n"); ok = false; }

    // Pickups: summing several pickups changes the tone, and stays finite and bounded
    const auto one = renderNote({{kParamPickupCount, 0.0f}}, 48, 24000);
    const auto four = renderNote({{kParamPickupCount, 1.0f}, {kParamPickupSpread, 1.0f}}, 48, 24000);
    const double cPick = correlation(one, four);
    std::printf("  -> 1 vs 4 pickups: correlation %.3f, rms %.4f vs %.4f\n", cPick, rmsOf(one, 2000, 20000), rmsOf(four, 2000, 20000));
    if (!(cPick < 0.995) || !allFinite(four) || rmsOf(four, 2000, 20000) < 0.2f * rmsOf(one, 2000, 20000)) { std::fprintf(stderr, "  !! multi-pickup had no effect or blew up\n"); ok = false; }

    // Gap FM: adds sidebands (differs from the plain note) but remains stable and in tune
    const auto plain = renderNote({}, 60, 24000);
    const auto fmod = renderNote({{kParamGapFmDepth, 0.8f}, {kParamGapFmRatio, 0.6f}}, 60, 24000);
    const double cFm = correlation(plain, fmod);
    const float f = autocorrFreq(fmod, 6000, 4096, 48000.0f);
    std::printf("  -> gap FM: correlation %.3f, pitch %.1f Hz\n", cFm, f);
    if (!(cFm < 0.98) || !allFinite(fmod) || std::fabs(f / 261.63f - 1.0f) > 0.05f) { std::fprintf(stderr, "  !! gap FM had no effect\n"); ok = false; }

    // Morph: fully morphed tine == the reed played directly; half-way is a different, finite blend
    const auto reed = renderNote({{kParamResonator, 1.0f}}, 60, 24000);
    const auto morphed = renderNote({{kParamResonator, 0.0f}, {kParamResMorph, 1.0f}}, 60, 24000);
    const auto half = renderNote({{kParamResonator, 0.0f}, {kParamResMorph, 0.5f}}, 60, 24000);
    const double cMorph = correlation(reed, morphed);
    std::printf("  -> morph 100%% vs reed: correlation %.4f | morph 50%% rms %.4f\n", cMorph, rmsOf(half, 2000, 20000));
    if (!(cMorph > 0.99)) { std::fprintf(stderr, "  !! full morph is not the reed\n"); ok = false; }
    if (!allFinite(half) || rmsOf(half, 2000, 20000) < 0.001f) { std::fprintf(stderr, "  !! half morph silent or unstable\n"); ok = false; }
    // And back: reed morphing to tine
    const auto tine = renderNote({{kParamResonator, 0.0f}}, 60, 24000);
    const auto morphedBack = renderNote({{kParamResonator, 1.0f}, {kParamResMorph, 1.0f}}, 60, 24000);
    if (!(correlation(tine, morphedBack) > 0.99)) { std::fprintf(stderr, "  !! reed to tine morph failed\n"); ok = false; }
    return ok;
}

bool testStudioEffects() {
    std::printf("[TEST 11] Studio effects (spring reverb, cabinets, post-EQ, compressor, meters, host modulation)...\n");
    bool ok = true;
    using namespace phs;
    const float fs = 48000.0f;
    auto defaults = [](float* p) { for (uint32_t i = 0; i < kNumParams; ++i) p[i] = kParamMetadata[i].defaultValue; p[kParamReverbMix] = 0; p[kParamTremoloDepth] = 0; };

    // Spring reverb: an impulse leaves a decaying tail, different from the hall
    float tailEnergy[2];
    for (int type = 0; type < 2; ++type) {
        PhsEffectsChain fx; fx.prepare(fs);
        float p[kNumParams]; defaults(p);
        p[kParamReverbMix] = 0.8f; p[kParamReverbDecay] = 0.8f; p[kParamReverbType] = static_cast<float>(type);
        double e = 0; bool finite = true;
        for (int i = 0; i < 48000; ++i) {
            float l, r; fx.processStereoSample(i == 0 ? 0.4f : 0.0f, p, 0.0f, l, r);
            if (i > 6000) e += l * double(l);
            finite = finite && std::isfinite(l) && std::isfinite(r);
        }
        tailEnergy[type] = static_cast<float>(e);
        if (!finite) { std::fprintf(stderr, "  !! reverb produced non-finite samples\n"); ok = false; }
    }
    std::printf("  -> reverb tail energy: hall %.4g | spring %.4g\n", tailEnergy[0], tailEnergy[1]);
    if (!(tailEnergy[1] > 1e-6f)) { std::fprintf(stderr, "  !! spring reverb has no tail\n"); ok = false; }

    // Cabinet types: the radio voicing removes the low end that a flat DI keeps
    float lowRms[2];
    for (int type = 0; type < 2; ++type) {
        PhsEffectsChain fx; fx.prepare(fs);
        float p[kNumParams]; defaults(p);
        p[kParamCabType] = type ? 4.0f / 4.0f : 0.0f; p[kParamCabinet] = 1.0f;
        if (type == 0) p[kParamCabinet] = 0.0f;
        double acc = 0;
        for (int i = 0; i < 24000; ++i) {
            float l, r; fx.processStereoSample(0.1f * std::sin(2.0f * 3.14159265f * 110.0f * i / fs), p, 0.0f, l, r);
            if (i > 8000) acc += l * double(l);
        }
        lowRms[type] = static_cast<float>(std::sqrt(acc / 16000.0));
    }
    std::printf("  -> 110 Hz through cabinet: DI %.4f | radio %.4f\n", lowRms[0], lowRms[1]);
    if (!(lowRms[1] < 0.35f * lowRms[0])) { std::fprintf(stderr, "  !! radio cabinet did not roll off the lows\n"); ok = false; }

    // Post-EQ: +12 dB bell at 1 kHz
    float eqGain;
    {
        double rms[2];
        for (int on = 0; on < 2; ++on) {
            PhsEffectsChain fx; fx.prepare(fs);
            float p[kNumParams]; defaults(p);
            p[kParamEqFreq] = std::log(1000.0f / 200.0f) / std::log(25.0f);
            p[kParamEqGain] = on ? 1.0f : 0.5f;
            double acc = 0;
            for (int i = 0; i < 24000; ++i) {
                float l, r; fx.processStereoSample(0.02f * std::sin(2.0f * 3.14159265f * 1000.0f * i / fs), p, 0.0f, l, r);
                if (i > 8000) acc += l * double(l);
            }
            rms[on] = std::sqrt(acc / 16000.0);
        }
        eqGain = static_cast<float>(20.0 * std::log10(rms[1] / rms[0]));
    }
    std::printf("  -> post-EQ +12 dB at 1 kHz measured %+.1f dB\n", eqGain);
    if (std::fabs(eqGain - 12.0f) > 1.5f) { std::fprintf(stderr, "  !! post-EQ gain off\n"); ok = false; }

    // Compressor: gain reduction reported, and loud/quiet ratio narrowed
    {
        double level[2][2];
        float gr = 0, meter = 0;
        for (int on = 0; on < 2; ++on) {
            PhsEffectsChain fx; fx.prepare(fs);
            float p[kNumParams]; defaults(p);
            p[kParamCompAmount] = on ? 1.0f : 0.0f;
            for (int lv = 0; lv < 2; ++lv) {
                double acc = 0;
                const float amp = lv ? 0.5f : 0.05f;
                for (int i = 0; i < 48000; ++i) {
                    float l, r; fx.processStereoSample(amp * std::sin(2.0f * 3.14159265f * 440.0f * i / fs), p, 0.0f, l, r);
                    if (i > 24000) acc += l * double(l);
                }
                level[on][lv] = std::sqrt(acc / 24000.0);
            }
            if (on) { gr = fx.gainReductionDb(); meter = fx.meterL(); }
        }
        const double spreadOff = level[0][1] / level[0][0], spreadOn = level[1][1] / level[1][0];
        std::printf("  -> compressor: loud/quiet %.2fx -> %.2fx, gain reduction %.1f dB, meter %.3f\n", spreadOff, spreadOn, gr, meter);
        if (!(spreadOn < 0.8 * spreadOff) || !(gr < -1.0f) || !(meter > 0.01f)) { std::fprintf(stderr, "  !! compressor or meters failed\n"); ok = false; }
    }

    // Host modulation (CLAP PARAM_MOD): -50 % on Volume lowers the level without moving the knob
    {
        float lvl[2];
        for (int m = 0; m < 2; ++m) {
            PhsSynthEngine e; e.prepare(fs);
            e.setParamNormalized(kParamReverbMix, 0.0f);
            if (m) e.setHostModulation(kParamMasterVolume, -0.5f);
            e.handleMidiEvent(0x90, 60, 100);
            lvl[m] = rmsOf(renderMono(e, 14000), 4000, 8000);
            if (m && std::fabs(e.getParamNormalized(kParamMasterVolume) - 0.78f) > 1e-5f) { std::fprintf(stderr, "  !! host mod moved the knob\n"); ok = false; }
        }
        std::printf("  -> host modulation on Volume: rms %.4f -> %.4f\n", lvl[0], lvl[1]);
        if (!(lvl[1] < 0.8f * lvl[0])) { std::fprintf(stderr, "  !! host modulation had no effect\n"); ok = false; }
    }
    return ok;
}

// ---------------------------------------------------------------------------
// v1.6: mastering EQ, compressor / limiter, cabinets
// ---------------------------------------------------------------------------
double sineGainDb(float* p, float freq, float amp, float fs = 48000.0f) {
    phs::PhsEffectsChain fx; fx.prepare(fs);
    double in = 0, out = 0;
    for (int i = 0; i < 48000; ++i) {
        const float x = amp * std::sin(2.0f * 3.14159265f * freq * i / fs);
        float l, r; fx.processStereoSample(x, p, 0.0f, l, r);
        if (i > 24000) { in += x * double(x); out += l * double(l); }
    }
    return 10.0 * std::log10((out + 1e-30) / in);
}

bool testMasteringChain() {
    std::printf("[TEST 12] Mastering EQ, compressor, limiter and cabinets...\n");
    bool ok = true;
    using namespace phs;
    auto defaults = [](float* p) { for (uint32_t i = 0; i < kNumParams; ++i) p[i] = kParamMetadata[i].defaultValue; p[kParamReverbMix] = 0; p[kParamTremoloDepth] = 0; p[kParamPreampDrive] = 0.05f; p[kParamMasterVolume] = 0.8f; };

    // EQ: the measured level must follow the analytic curve the editor draws (all four bands active)
    float p[kNumParams]; defaults(p);
    p[kParamEqB1Gain] = 0.5f + 6.0f / 24.0f;  // +6 dB low shelf
    p[kParamEqB2Gain] = 0.5f - 5.0f / 24.0f;  // -5 dB bell
    p[kParamEqGain] = 0.5f + 8.0f / 24.0f;    // +8 dB bell
    p[kParamEqB4Gain] = 0.5f + 4.0f / 24.0f;  // +4 dB high shelf
    wurli::Biquad bands[4];
    for (int b = 0; b < 4; ++b) setupEqBand(bands[b], b, p, 48000.0);
    double worst = 0;
    for (float f : {50.0f, 250.0f, 1000.0f, 9000.0f}) {
        double analytic = 0;
        for (auto& bq : bands) analytic += 20.0 * std::log10(bq.magnitude(f, 48000.0));
        float q[kNumParams]; defaults(q); std::memcpy(q, p, sizeof(q));
        float base[kNumParams]; defaults(base);
        const double measured = sineGainDb(q, f, 0.02f) - sineGainDb(base, f, 0.02f);
        worst = std::max(worst, std::fabs(measured - analytic));
        std::printf("  -> EQ at %5.0f Hz: analytic %+5.2f dB | measured %+5.2f dB\n", f, analytic, measured);
    }
    if (worst > 0.5) { std::fprintf(stderr, "  !! EQ curve disagrees with the audio\n"); ok = false; }

    // Compressor: static gain computer, -6 dB in, threshold -18, 4:1, hard knee, no make-up: -9 dB of reduction
    {
        float c[kNumParams]; defaults(c);
        c[kParamCompAmount] = 1.0f; c[kParamCompThresh] = 22.0f / 40.0f; c[kParamCompRatio] = std::log(4.0f) / std::log(20.0f);
        c[kParamMasterVolume] = 0.15f; c[kParamCompKnee] = 0.0f; c[kParamCompAuto] = 0.0f; c[kParamCompAttack] = 0.2f; c[kParamCompRelease] = 0.3f;
        float off[kNumParams]; defaults(off); off[kParamMasterVolume] = 0.15f;
        // Slope of the gain computer: every 6 dB more input is 6 * (1 - 1/4) = 4.5 dB more reduction
        const double r1 = sineGainDb(c, 440.0f, 0.25f) - sineGainDb(off, 440.0f, 0.25f);
        const double r2 = sineGainDb(c, 440.0f, 0.5f) - sineGainDb(off, 440.0f, 0.5f);
        std::printf("  -> compressor 4:1: reduction %.2f dB at -12 dB in, %.2f dB at -6 dB in (step %.2f, expect -4.5)\n", r1, r2, r2 - r1);
        if (std::fabs((r2 - r1) + 4.5) > 1.0) { std::fprintf(stderr, "  !! compressor ratio off\n"); ok = false; }
        // Makeup restores level; the RMS detector and sidechain filter stay stable
        c[kParamCompMakeup] = 9.0f / 18.0f; c[kParamCompDetect] = 1.0f; c[kParamCompHpf] = 0.3f;
        const double made = sineGainDb(c, 440.0f, 0.5f) - sineGainDb(off, 440.0f, 0.5f);
        std::printf("  -> with +9 dB make-up, RMS detector, sidechain HPF: %.2f dB\n", made);
        if (!std::isfinite(made) || made < -6.0 || made > 6.0) { std::fprintf(stderr, "  !! make-up / RMS path wrong\n"); ok = false; }
    }

    // Limiter: nothing above the ceiling
    {
        float l[kNumParams]; defaults(l);
        l[kParamLimOn] = 1.0f; l[kParamLimCeil] = (-6.0f + 12.0f) / 12.0f; // -6 dB
        PhsEffectsChain fx; fx.prepare(48000.0f);
        float peak = 0;
        for (int i = 0; i < 48000; ++i) {
            float a, b; fx.processStereoSample(0.9f * std::sin(2.0f * 3.14159265f * 300.0f * i / 48000.0f), l, 0.0f, a, b);
            if (i > 2000) peak = std::max(peak, std::fabs(a));
        }
        std::printf("  -> limiter ceiling -6 dB (0.501): peak %.3f\n", peak);
        if (peak > 0.55f) { std::fprintf(stderr, "  !! limiter exceeded its ceiling\n"); ok = false; }
    }

    // Cabinets: every type is stable, and the plotted response matches the audio (linear types, room off)
    for (int type = 1; type <= 9; ++type) {
        CabinetModel cm; cm.prepare(48000.0f);
        cm.configure(type, 0.4f, 0.3f, 0.0f, 0.0f);
        bool finite = true; float peak = 0;
        for (int i = 0; i < 48000; ++i) { const float y = cm.process(0.1f * std::sin(2.0f * 3.14159265f * 440.0f * i / 48000.0f)); finite = finite && std::isfinite(y); peak = std::max(peak, std::fabs(y)); }
        if (!finite || peak > 4.0f) { std::fprintf(stderr, "  !! cabinet %d unstable\n", type); ok = false; }
    }
    {
        CabinetModel cm; cm.prepare(48000.0f);
        cm.configure(5, 0.4f, 0.3f, 0.0f, 0.0f); // studio condenser: linear
        double in = 0, out = 0;
        for (int i = 0; i < 48000; ++i) {
            const float x = 0.01f * std::sin(2.0f * 3.14159265f * 3000.0f * i / 48000.0f);
            const float y = cm.process(x);
            if (i > 24000) { in += x * double(x); out += y * double(y); }
        }
        const double measured = 10.0 * std::log10(out / in), analytic = CabinetModel::responseDb(5, 0.4f, 0.3f, 3000.0);
        std::printf("  -> studio condenser at 3 kHz: plotted %+.2f dB | measured %+.2f dB\n", analytic, measured);
        if (std::fabs(measured - analytic) > 0.6) { std::fprintf(stderr, "  !! cabinet curve disagrees with the audio\n"); ok = false; }
    }
    return ok;
}

} // namespace

int main() {
    bool ok = true;
    ok &= verifyPhsPassivity();
#if defined(__APPLE__)
    // Plugin-bundle load tests use the macOS bundle layout
    ok &= testClapBundle("build/electropiano.clap/Contents/MacOS/electropiano");
    ok &= testVst3Bundle("build/electropiano.vst3/Contents/MacOS/electropiano");
    ok &= testAuBundle("build/electropiano.component/Contents/MacOS/electropiano");
#endif
    ok &= testExpression();
    ok &= testModulationMatrix();
    ok &= testOversamplingAndSync();
    ok &= testStateCompatibility();
    ok &= testMacros();
    ok &= testStudioEffects();
    ok &= testMasteringChain();
    renderShowcaseDemos();

    if (ok) {
        std::printf("\nALL PORT-HAMILTONIAN DSP & PLUGIN FORMAT TESTS PASSED!\n");
        return 0;
    } else {
        std::fprintf(stderr, "\nONE OR MORE TESTS FAILED!\n");
        return 1;
    }
}
