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
    renderShowcaseDemos();

    if (ok) {
        std::printf("\nALL PORT-HAMILTONIAN DSP & PLUGIN FORMAT TESTS PASSED!\n");
        return 0;
    } else {
        std::fprintf(stderr, "\nONE OR MORE TESTS FAILED!\n");
        return 1;
    }
}
