#pragma once

// Procedural guitar-amp / microphone cabinet models for the electric piano. Each cabinet is a
// speaker voicing (high-pass, two low-pass poles, four resonant bells for the box and cone
// breakup) followed by the microphone: position on the cone, distance (proximity bass and air
// loss) and a few early room reflections. The same object is used by the DSP and by the editor,
// which plots its response, so what is drawn is what is heard.

#include "wurli/WurliVoice.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace phs {

struct CabPeak { float f, q, db; };
struct CabSpec {
    const char* name;
    float hp, lp;          // speaker band (Hz)
    CabPeak pk[4];         // box resonance, body, presence, cone breakup
    float drive;           // speaker compression / breakup (0 = linear)
    float proxDb;          // close-mic proximity bass boost
    float capDb;           // dust-cap presence when the mic sits on the centre
};

inline const CabSpec& cabSpec(int type) {
    static const CabSpec kSpecs[9] = {
        {"Suitcase 4x10",   65.0f,  5800.0f, {{110, 1.1f,  4.0f}, {450, 1.0f,  2.0f}, {2200, 1.3f, 3.0f}, {3600, 2.0f,  2.0f}}, 0.15f, 3.0f, 3.0f},
        {"Twin 2x12 Open",  75.0f,  6500.0f, {{ 90, 0.9f,  2.5f}, {700, 1.2f, -2.0f}, {2800, 1.0f, 3.0f}, {4800, 2.0f,  2.0f}}, 0.10f, 2.0f, 4.0f},
        {"Deluxe 1x12",     85.0f,  5200.0f, {{130, 1.2f,  3.5f}, {900, 1.0f,  2.0f}, {2200, 1.1f, 4.0f}, {3800, 2.0f, -2.0f}}, 0.30f, 4.0f, 3.0f},
        {"Closed 2x12",     70.0f,  5000.0f, {{100, 1.4f,  4.0f}, {400, 1.0f, -2.0f}, {1900, 1.0f, 3.5f}, {3200, 2.0f,  2.5f}}, 0.35f, 4.0f, 3.5f},
        {"Studio Condenser",30.0f, 15000.0f, {{180, 0.7f,  1.0f}, {900, 0.7f,  0.0f}, {5000, 0.7f, 3.0f}, {11000, 0.6f, 3.0f}}, 0.00f, 2.0f, 1.5f},
        {"Ribbon Mic",      40.0f,  9000.0f, {{ 90, 0.8f,  3.0f}, {250, 0.7f,  2.0f}, {3000, 1.0f,-2.0f}, {8000, 0.7f, -4.0f}}, 0.00f, 5.0f, 0.0f},
        {"Hi-Fi Monitor",  40.0f, 18000.0f, {{ 80, 0.8f,  1.5f}, {500, 0.8f,  0.0f}, {2000, 0.8f,-1.0f}, {9000, 0.8f,  1.0f}}, 0.00f, 1.0f, 0.0f},
        {"Radio",         450.0f,  3300.0f, {{800, 1.5f,  3.0f}, {1200, 1.0f, 0.0f}, {1800, 1.8f, 5.0f}, {3000, 2.0f,  2.0f}}, 0.50f, 0.0f, 0.0f},
        {"Lo-Fi",         300.0f,  2400.0f, {{600, 2.5f,  6.0f}, {1200, 3.0f, 4.0f}, {2000, 1.0f,-2.0f}, {2300, 2.0f,  0.0f}}, 0.80f, 0.0f, 0.0f}};
    return kSpecs[std::clamp(type - 1, 0, 8)];
}

class CabinetModel {
public:
    void prepare(float fs) {
        fs_ = fs;
        combBuf_.assign(256, 0.0f);
        roomBuf_.assign(static_cast<size_t>(0.060f * fs) + 8, 0.0f);
        for (int t = 0; t < kRoomTaps; ++t) roomTapDelay_[t] = static_cast<int>(kRoomTapSec[t] * fs_);
        reset();
        key_ = -1.0f;
    }
    void reset() {
        for (auto& b : bq_) b.reset();
        std::fill(combBuf_.begin(), combBuf_.end(), 0.0f);
        std::fill(roomBuf_.begin(), roomBuf_.end(), 0.0f);
        combIdx_ = roomIdx_ = 0;
        roomLp_ = 0.0f;
    }

    // type 1..9, mic / dist / room 0..1, driveExtra 0..1 (the Cabinet knob)
    void configure(int type, float mic, float dist, float room, float driveExtra) {
        const float key = type * 1000.0f + mic * 97.0f + dist * 31.0f + room * 7.0f;
        driveExtra_ = driveExtra;
        roomAmount_ = room;
        if (key == key_) return;
        key_ = key;
        design(type, mic, dist, bq_, fs_, comb_);
        const CabSpec& s = cabSpec(type);
        drive_ = s.drive;
        roomGain_ = room * (0.25f + 0.75f * dist);
        combDelay_ = std::clamp(static_cast<int>(comb_.delayMs * 0.001f * fs_), 1, 250);
        combGain_ = comb_.gain;
    }

    float process(float x) {
        const float k = 1.0f + 3.0f * (drive_ + 1.5f * driveExtra_);
        double v = std::tanh(k * x) / k;
        for (auto& b : bq_) v = b.process(v);
        float y = static_cast<float>(v);
        // Mic off the cone axis: a short comb from the path difference between cone centre and edge
        const int cn = static_cast<int>(combBuf_.size());
        combBuf_[combIdx_] = y;
        int ci = combIdx_ - combDelay_;
        if (ci < 0) ci += cn;
        y += combGain_ * combBuf_[ci];
        if (++combIdx_ >= cn) combIdx_ = 0;
        // Room: five early reflections through a one-pole low-pass
        if (roomGain_ > 0.002f) {
            roomBuf_[roomIdx_] = y;
            static const float gains[kRoomTaps] = {0.50f, 0.40f, 0.33f, 0.25f, 0.18f};
            float acc = 0.0f;
            const int n = static_cast<int>(roomBuf_.size());
            for (int t = 0; t < kRoomTaps; ++t) {
                int ri = roomIdx_ - roomTapDelay_[t];
                if (ri < 0) ri += n;
                acc += gains[t] * roomBuf_[ri];
            }
            roomLp_ += 0.35f * (acc - roomLp_);
            y += roomGain_ * roomLp_;
            if (++roomIdx_ >= n) roomIdx_ = 0;
        }
        return y;
    }

    // Magnitude response of the speaker + mic chain (without the room), for the editor's curve
    static double responseDb(int type, float mic, float dist, double f, double fs = 48000.0) {
        wurli::Biquad bq[kBiquads];
        Comb comb;
        design(type, mic, dist, bq, static_cast<float>(fs), comb);
        double mag = 1.0;
        for (const auto& b : bq) mag *= b.magnitude(f, fs);
        const double w = wurli::kTau * f / fs, d = comb.delayMs * 0.001 * fs;
        const double cr = 1.0 + comb.gain * std::cos(w * d), ci = -comb.gain * std::sin(w * d);
        mag *= std::sqrt(cr * cr + ci * ci);
        return 20.0 * std::log10(mag + 1e-12);
    }

private:
    static constexpr int kBiquads = 9;
    static constexpr int kRoomTaps = 5;
    static constexpr float kRoomTapSec[kRoomTaps] = {0.0063f, 0.0111f, 0.0179f, 0.0260f, 0.0377f};
    struct Comb { float delayMs = 0.1f, gain = 0.0f; };

    static void design(int type, float mic, float dist, wurli::Biquad* bq, float fs, Comb& comb) {
        const CabSpec& s = cabSpec(type);
        const double lpHz = std::min<double>(s.lp * std::pow(0.5, 0.8 * mic) * std::pow(0.78, dist), 0.45 * fs);
        bq[0].setHighpass(s.hp, 0.7, fs);
        bq[1].setLowpass(lpHz, 0.707, fs);
        bq[2].setLowpass(lpHz, 0.6, fs);
        for (int i = 0; i < 4; ++i) {
            const double db = s.pk[i].db * (i == 3 ? (1.0 - 0.7 * mic) : 1.0);
            bq[3 + i].setPeaking(std::min<double>(s.pk[i].f, 0.45 * fs), s.pk[i].q, db, fs);
        }
        bq[7].setLowShelf(160.0, 0.7, s.proxDb * (1.0 - dist), fs);   // proximity effect
        bq[8].setPeaking(4200.0, 0.8, s.capDb * (1.0 - mic), fs);      // dust-cap presence
        comb.delayMs = 0.04f + 0.30f * mic;
        comb.gain = 0.35f * mic;
    }

    float fs_ = 48000.0f, key_ = -1.0f, drive_ = 0.0f, driveExtra_ = 0.0f, roomGain_ = 0.0f, roomAmount_ = 0.0f;
    wurli::Biquad bq_[kBiquads];
    Comb comb_;
    int combDelay_ = 1, combIdx_ = 0, roomIdx_ = 0, roomTapDelay_[kRoomTaps]{};
    float combGain_ = 0.0f, roomLp_ = 0.0f;
    std::vector<float> combBuf_, roomBuf_;
};

} // namespace phs
