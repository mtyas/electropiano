#pragma once

// Stereo reverbs for the effects chain:
//   StudioHall  8-line feedback delay network with a Hadamard mixing matrix, frequency-dependent
//               decay (a one-pole low-pass in every line), slowly modulated lines against
//               metallic ringing, and input diffusion. True stereo: left and right feed
//               different lines and are read from different lines.
//   PlateReverb The plate topology of J. Dattorro, "Effect Design, Part 1: Reverberator and
//               Other Filters", J. Audio Eng. Soc. 45(9), 1997: input diffusers into a
//               figure-of-eight tank of modulated all-passes and delays, with the published
//               output taps. Delay lengths are scaled from the paper's 29761 Hz.
// Both take a decay (0..1) and a damping amount and run at the host rate.

#include <array>
#include <cmath>
#include <vector>

namespace phs {

namespace rvb {
// Delay line with a fractional (linearly interpolated) read
struct Delay {
    std::vector<float> buf;
    int pos = 0;
    void init(int maxLen) { buf.assign(static_cast<size_t>(std::max(4, maxLen + 2)), 0.0f); pos = 0; }
    void clear() { std::fill(buf.begin(), buf.end(), 0.0f); pos = 0; }
    void push(float x) {
        buf[static_cast<size_t>(pos)] = x;
        if (++pos >= static_cast<int>(buf.size())) pos = 0;
    }
    // Sample written `d` pushes ago (d >= 1)
    float tap(int d) const {
        int i = pos - d;
        if (i < 0) i += static_cast<int>(buf.size());
        return buf[static_cast<size_t>(i)];
    }
    float tapFrac(float d) const {
        const int di = static_cast<int>(d);
        const float f = d - static_cast<float>(di);
        return tap(di) + f * (tap(di + 1) - tap(di));
    }
};

// Schroeder all-pass: y = -g x + d, d <- x + g y
struct Allpass {
    Delay d;
    int len = 1;
    void init(int l) { len = std::max(1, l); d.init(len + 32); }
    void clear() { d.clear(); }
    float process(float x, float g, float modSamples = 0.0f) {
        const float delayed = modSamples != 0.0f ? d.tapFrac(static_cast<float>(len) + modSamples) : d.tap(len);
        const float v = x + g * delayed;
        d.push(v);
        return delayed - g * v;
    }
};
} // namespace rvb

class StudioHall {
public:
    void prepare(float fs) {
        fs_ = fs;
        static const float lineMs[kLines] = {29.7f, 37.1f, 41.1f, 43.7f, 53.0f, 59.9f, 67.3f, 73.1f};
        for (int i = 0; i < kLines; ++i) {
            len_[i] = static_cast<float>(static_cast<int>(lineMs[i] * 0.001f * fs));
            line_[i].init(static_cast<int>(len_[i]) + 64);
        }
        static const float diffMs[4] = {4.7f, 3.6f, 12.7f, 9.3f};
        for (int i = 0; i < 4; ++i) {
            diffL_[i].init(static_cast<int>(diffMs[i] * 0.001f * fs));
            diffR_[i].init(static_cast<int>(diffMs[i] * 1.07f * 0.001f * fs));
        }
        modInc_ = 0.37f / fs;
        decayKey_ = -1.0f;
        reset();
    }
    void reset() {
        for (auto& l : line_) l.clear();
        for (auto& a : diffL_) a.clear();
        for (auto& a : diffR_) a.clear();
        lp_.fill(0.0f);
        modPhase_ = 0.0f;
    }

    // decay 0..1 (0.6 .. 7 s), damping 0..1 (bright .. dark)
    void process(float inL, float inR, float decay, float damping, float& outL, float& outR) {
        if (decay != decayKey_ || damping != dampKey_) {
            decayKey_ = decay;
            dampKey_ = damping;
            const float t60 = 0.6f * std::pow(11.67f, decay);
            for (int i = 0; i < kLines; ++i) gain_[i] = std::pow(10.0f, -3.0f * len_[i] / (t60 * fs_));
            const float fc = 9000.0f * std::pow(0.15f, damping);
            dampCoef_ = 1.0f - std::exp(-6.2831853f * fc / fs_);
        }
        // Input diffusion
        float l = inL, r = inR;
        for (int i = 0; i < 4; ++i) { l = diffL_[i].process(l, 0.62f); r = diffR_[i].process(r, 0.62f); }

        // Read the lines (two of them slowly modulated, +-0.6 ms)
        modPhase_ += modInc_;
        if (modPhase_ >= 1.0f) modPhase_ -= 1.0f;
        const float m = 0.0006f * fs_ * std::sin(6.2831853f * modPhase_);
        float y[kLines];
        for (int i = 0; i < kLines; ++i) {
            const float mod = i == 2 ? m : (i == 5 ? -m : 0.0f);
            float v = line_[i].tapFrac(len_[i] + mod + 2.0f) * gain_[i];
            lp_[i] += dampCoef_ * (v - lp_[i]);
            y[i] = lp_[i];
        }
        outL = 0.35f * (y[0] + y[2] + y[4] + y[6]);
        outR = 0.35f * (y[1] + y[3] + y[5] + y[7]);

        // Hadamard feedback (fast Walsh-Hadamard transform, energy-preserving with 1/sqrt(8))
        for (int h = 1; h < kLines; h <<= 1)
            for (int i = 0; i < kLines; i += h << 1)
                for (int j = i; j < i + h; ++j) {
                    const float a = y[j], b = y[j + h];
                    y[j] = a + b;
                    y[j + h] = a - b;
                }
        for (int i = 0; i < kLines; ++i) line_[i].push(y[i] * 0.35355339f + ((i & 1) ? r : l));
    }

private:
    static constexpr int kLines = 8;
    float fs_ = 48000.0f;
    std::array<rvb::Delay, kLines> line_;
    std::array<float, kLines> len_{}, gain_{}, lp_{};
    std::array<rvb::Allpass, 4> diffL_, diffR_;
    float decayKey_ = -1.0f, dampKey_ = -1.0f, dampCoef_ = 0.5f;
    float modPhase_ = 0.0f, modInc_ = 0.0f;
};

class PlateReverb {
public:
    void prepare(float fs) {
        fs_ = fs;
        const float k = fs / 29761.0f;
        auto L = [k](int n) { return std::max(1, static_cast<int>(std::lround(n * k))); };
        inDiff_[0].init(L(142)); inDiff_[1].init(L(107)); inDiff_[2].init(L(379)); inDiff_[3].init(L(277));
        modAp_[0].init(L(672)); modAp_[1].init(L(908));
        dly1_[0].init(L(4453)); dly1_[1].init(L(4217));
        decAp_[0].init(L(1800)); decAp_[1].init(L(2656));
        dly2_[0].init(L(3720)); dly2_[1].init(L(3163));
        len1_[0] = L(4453); len1_[1] = L(4217); len2_[0] = L(3720); len2_[1] = L(3163);
        // Output taps (Dattorro table 2)
        tapL_ = {L(266), L(2974), L(1913), L(1996), L(1990), L(187), L(1066)};
        tapR_ = {L(353), L(3627), L(1228), L(2673), L(2111), L(335), L(121)};
        excursion_ = 16.0f * k;
        modInc_ = 1.0f / fs;
        reset();
    }
    void reset() {
        for (auto& a : inDiff_) a.clear();
        for (int i = 0; i < 2; ++i) { modAp_[i].clear(); dly1_[i].clear(); decAp_[i].clear(); dly2_[i].clear(); }
        bw_ = 0.0f;
        damp_.fill(0.0f);
        modPhase_ = 0.0f;
    }

    void process(float inL, float inR, float decay, float damping, float& outL, float& outR) {
        const float dec = 0.30f + 0.66f * decay;              // tank decay
        const float d = 0.0005f + 0.75f * damping * damping;  // tank damping
        // Input: band limit, then four diffusers
        bw_ += 0.9995f * (0.5f * (inL + inR) - bw_);
        float x = bw_;
        x = inDiff_[0].process(x, 0.75f);
        x = inDiff_[1].process(x, 0.75f);
        x = inDiff_[2].process(x, 0.625f);
        x = inDiff_[3].process(x, 0.625f);

        modPhase_ += modInc_;
        if (modPhase_ >= 1.0f) modPhase_ -= 1.0f;
        const float mod0 = excursion_ * std::sin(6.2831853f * modPhase_);
        const float mod1 = excursion_ * std::cos(6.2831853f * modPhase_);

        // Figure-of-eight tank: each half is fed the input plus the other half's end
        const float fb0 = dly2_[1].tap(len2_[1]) * dec;
        const float fb1 = dly2_[0].tap(len2_[0]) * dec;
        for (int h = 0; h < 2; ++h) {
            float t = x + (h == 0 ? fb0 : fb1);
            t = modAp_[h].process(t, -0.70f, h == 0 ? mod0 : mod1); // decay diffusion 1 (sign per paper)
            dly1_[h].push(t);
            float u = dly1_[h].tap(len1_[h]);
            damp_[h] += (1.0f - d) * (u - damp_[h]);
            u = damp_[h] * dec;
            u = decAp_[h].process(u, std::clamp(dec + 0.15f, 0.25f, 0.50f));
            dly2_[h].push(u);
        }

        // Output taps
        outL = 0.6f * (dly1_[1].tap(tapL_[0]) + dly1_[1].tap(tapL_[1]) - decAp_[1].d.tap(tapL_[2]) + dly2_[1].tap(tapL_[3])
                       - dly1_[0].tap(tapL_[4]) - decAp_[0].d.tap(tapL_[5]) - dly2_[0].tap(tapL_[6]));
        outR = 0.6f * (dly1_[0].tap(tapR_[0]) + dly1_[0].tap(tapR_[1]) - decAp_[0].d.tap(tapR_[2]) + dly2_[0].tap(tapR_[3])
                       - dly1_[1].tap(tapR_[4]) - decAp_[1].d.tap(tapR_[5]) - dly2_[1].tap(tapR_[6]));
    }

private:
    float fs_ = 48000.0f;
    std::array<rvb::Allpass, 4> inDiff_;
    std::array<rvb::Allpass, 2> modAp_, decAp_;
    std::array<rvb::Delay, 2> dly1_, dly2_;
    int len1_[2]{1, 1}, len2_[2]{1, 1};
    std::array<int, 7> tapL_{}, tapR_{};
    float bw_ = 0.0f, excursion_ = 16.0f, modPhase_ = 0.0f, modInc_ = 0.0f;
    std::array<float, 2> damp_{};
};

} // namespace phs
