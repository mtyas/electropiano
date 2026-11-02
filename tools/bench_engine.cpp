// CPU benchmark and regression check for the synth engine.
//
//   build/bench_engine                 time every factory preset (dense 12-voice passage)
//   build/bench_engine --save DIR      also write each preset's output to DIR/<idx>.f32
//   build/bench_engine --compare DIR   report the largest difference against a saved run
//   ... --fail-above X                 exit with status 2 if any sample differs by more than X
//   ... --quick                        2 seconds per preset instead of 8 (CI)
//
// The figure reported is the real-time factor (audio seconds rendered per CPU second), so
// higher is better. The comparison lets DSP optimisations be checked for audible drift.

#include "../src/dsp/PhsSynthEngine.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr float kFs = 48000.0f;
constexpr int kBlock = 256;
int kSeconds = 8;

// A busy passage: overlapping chords, sustain pedal, fast repeated notes and a pitch bend
void play(phs::PhsSynthEngine& e, int blockIdx) {
    const int t = blockIdx * kBlock;
    auto at = [&](float sec) { return t == static_cast<int>(sec * kFs) / kBlock * kBlock; };
    static const int chordA[6] = {36, 48, 55, 59, 62, 64};
    static const int chordB[6] = {41, 53, 57, 60, 64, 67};
    if (at(0.0f)) for (int n : chordA) e.noteOn(n, 0.75f);
    if (at(0.5f)) e.handleMidiEvent(0xB0, 64, 127);
    if (at(1.0f)) for (int n : chordA) e.noteOff(n);
    if (at(1.2f)) for (int n : chordB) e.noteOn(n, 0.85f);
    for (int k = 0; k < 16; ++k)
        if (at(2.0f + 0.12f * k)) e.noteOn(72 + (k * 5) % 14, 0.5f + 0.03f * k);
    if (at(4.0f)) e.handleMidiEvent(0xE0, 0, 96);
    if (at(4.5f)) e.handleMidiEvent(0xB0, 64, 0);
    if (at(4.6f)) e.handleMidiEvent(0xE0, 0, 64);
    if (at(5.0f)) for (int n : chordA) e.noteOn(n + 12, 1.0f);
    if (at(6.5f)) for (int n : chordA) e.noteOff(n + 12);
}

} // namespace

int main(int argc, char** argv) {
    std::string saveDir, compareDir;
    double failAbove = -1.0;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--quick")) kSeconds = 2;
        else if (i + 1 < argc && !std::strcmp(argv[i], "--save")) saveDir = argv[++i];
        else if (i + 1 < argc && !std::strcmp(argv[i], "--compare")) compareDir = argv[++i];
        else if (i + 1 < argc && !std::strcmp(argv[i], "--fail-above")) failAbove = std::atof(argv[++i]);
    }

    const int frames = static_cast<int>(kFs) * kSeconds;
    const int presets = phs::PhsPresetManager().getPresetCount();
    double totalCpu = 0.0, worstDiff = 0.0;
    std::vector<float> l(frames), r(frames);

    std::printf("%-4s %-34s %9s %12s\n", "idx", "preset", "x realtime", "max |diff|");
    for (int p = 0; p < presets; ++p) {
        auto engine = std::make_unique<phs::PhsSynthEngine>();
        engine->prepare(kFs);
        engine->loadPreset(p, false);

        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b * kBlock < frames; ++b) {
            play(*engine, b);
            engine->processBlock(l.data() + b * kBlock, r.data() + b * kBlock, kBlock);
        }
        const double cpu = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        totalCpu += cpu;

        std::vector<float> both(l);
        both.insert(both.end(), r.begin(), r.end());
        double diff = -1.0;
        if (!compareDir.empty()) {
            std::ifstream in(compareDir + "/" + std::to_string(p) + ".f32", std::ios::binary);
            std::vector<float> ref(both.size());
            if (in.read(reinterpret_cast<char*>(ref.data()), static_cast<std::streamsize>(ref.size() * sizeof(float)))) {
                diff = 0.0;
                for (size_t i = 0; i < both.size(); ++i) diff = std::max(diff, static_cast<double>(std::fabs(both[i] - ref[i])));
                worstDiff = std::max(worstDiff, diff);
            }
        }
        if (!saveDir.empty()) {
            std::ofstream out(saveDir + "/" + std::to_string(p) + ".f32", std::ios::binary);
            out.write(reinterpret_cast<const char*>(both.data()), static_cast<std::streamsize>(both.size() * sizeof(float)));
        }
        const auto* preset = engine->getPresetManager().getPreset(p);
        std::printf("%-4d %-34s %9.1f %12s\n", p, preset ? preset->name.c_str() : "?", kSeconds / cpu,
                    diff < 0.0 ? "-" : std::to_string(diff).c_str());
    }
    std::printf("\nAll presets: %.1f x realtime (%.2f s CPU for %d s of audio)\n",
                presets * kSeconds / totalCpu, totalCpu, presets * kSeconds);
    if (!compareDir.empty()) std::printf("Largest sample difference against %s: %.3g\n", compareDir.c_str(), worstDiff);
    if (failAbove >= 0.0 && worstDiff > failAbove) {
        std::printf("FAIL: the sound changed by more than %g\n", failAbove);
        return 2;
    }
    return 0;
}
