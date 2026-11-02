// Wurlitzer 200A voice: modal reed, hammer, electrostatic pickup.
//
// C++ port of OpenWurli's voice-side DSP (crates/openwurli-dsp/src: tables.rs,
// variation.rs, hammer.rs, reed.rs, pickup.rs, voice.rs, filters.rs).
// Copyright (c) the OpenWurli contributors, GPL-3.0-or-later.
// Upstream: https://github.com/hal0zer0/openwurli
//
// The port keeps OpenWurli's numerics (double precision, same constants, same
// update order). Things deliberately left out: the experimental MLP per-note
// corrections (off by default upstream) and the test-only helpers.
#pragma once

#include "WurliPickupTables.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace phs::wurli {

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kTau = 2.0 * kPi;

// =============================================================================
// Biquad (Audio EQ Cookbook, Direct Form II Transposed) — as melange-primitives
// =============================================================================
class Biquad {
public:
    static Biquad bandpass(double fc, double q, double fs) { Biquad b; b.setBandpass(fc, q, fs); return b; }
    static Biquad lowpass(double fc, double q, double fs) { Biquad b; b.setLowpass(fc, q, fs); return b; }
    static Biquad highpass(double fc, double q, double fs) { Biquad b; b.setHighpass(fc, q, fs); return b; }

    // Constant skirt gain bandpass (peak gain = Q)
    void setBandpass(double fc, double q, double fs) {
        const double w0 = kTau * fc / fs, alpha = std::sin(w0) / (2.0 * q);
        setNormalised(q * alpha, 0.0, -q * alpha, 1.0 + alpha, -2.0 * std::cos(w0), 1.0 - alpha);
    }
    void setLowpass(double fc, double q, double fs) {
        const double w0 = kTau * fc / fs, alpha = std::sin(w0) / (2.0 * q), c = std::cos(w0);
        setNormalised((1.0 - c) * 0.5, 1.0 - c, (1.0 - c) * 0.5, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
    }
    void setHighpass(double fc, double q, double fs) {
        const double w0 = kTau * fc / fs, alpha = std::sin(w0) / (2.0 * q), c = std::cos(w0);
        setNormalised((1.0 + c) * 0.5, -(1.0 + c), (1.0 + c) * 0.5, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
    }
    void setPeaking(double fc, double q, double gainDb, double fs) {
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w0 = kTau * fc / fs, alpha = std::sin(w0) / (2.0 * q), c = std::cos(w0);
        setNormalised(1.0 + alpha * A, -2.0 * c, 1.0 - alpha * A, 1.0 + alpha / A, -2.0 * c, 1.0 - alpha / A);
    }
    void setLowShelf(double fc, double q, double gainDb, double fs) {
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w0 = kTau * fc / fs, alpha = std::sin(w0) / (2.0 * q), c = std::cos(w0), sa = 2.0 * std::sqrt(A) * alpha;
        setNormalised(A * ((A + 1.0) - (A - 1.0) * c + sa), 2.0 * A * ((A - 1.0) - (A + 1.0) * c), A * ((A + 1.0) - (A - 1.0) * c - sa),
                      (A + 1.0) + (A - 1.0) * c + sa, -2.0 * ((A - 1.0) + (A + 1.0) * c), (A + 1.0) + (A - 1.0) * c - sa);
    }
    void setHighShelf(double fc, double q, double gainDb, double fs) {
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w0 = kTau * fc / fs, alpha = std::sin(w0) / (2.0 * q), c = std::cos(w0), sa = 2.0 * std::sqrt(A) * alpha;
        setNormalised(A * ((A + 1.0) + (A - 1.0) * c + sa), -2.0 * A * ((A - 1.0) + (A + 1.0) * c), A * ((A + 1.0) + (A - 1.0) * c - sa),
                      (A + 1.0) - (A - 1.0) * c + sa, 2.0 * ((A - 1.0) - (A + 1.0) * c), (A + 1.0) - (A - 1.0) * c - sa);
    }
    // |H(e^jw)| at frequency f: used by the editor to draw the EQ and cabinet curves
    double magnitude(double f, double fs) const {
        const double w = kTau * f / fs;
        const double c1 = std::cos(w), s1 = std::sin(w), c2 = std::cos(2.0 * w), s2 = std::sin(2.0 * w);
        const double nr = b0_ + b1_ * c1 + b2_ * c2, ni = -(b1_ * s1 + b2_ * s2);
        const double dr = 1.0 + a1_ * c1 + a2_ * c2, di = -(a1_ * s1 + a2_ * s2);
        return std::sqrt((nr * nr + ni * ni) / (dr * dr + di * di + 1e-300));
    }
    double process(double x) {
        const double y = b0_ * x + z1_;
        z1_ = b1_ * x - a1_ * y + z2_;
        z2_ = b2_ * x - a2_ * y;
        return y;
    }
    void reset() { z1_ = z2_ = 0.0; }

private:
    void setNormalised(double b0, double b1, double b2, double a0, double a1, double a2) {
        b0_ = b0 / a0; b1_ = b1 / a0; b2_ = b2 / a0; a1_ = a1 / a0; a2_ = a2 / a0;
    }
    double b0_ = 1.0, b1_ = 0.0, b2_ = 0.0, a1_ = 0.0, a2_ = 0.0;
    double z1_ = 0.0, z2_ = 0.0;
};

// =============================================================================
// Per-note tables (tables.rs) — Euler-Bernoulli reed with tip mass, MIDI 33..96
// =============================================================================
inline constexpr int kNumModes = 7;
inline constexpr int kMidiLo = 33;
inline constexpr int kMidiHi = 96;
inline constexpr std::array<double, kNumModes> kBaseModeAmplitudes = {1.0, 0.005, 0.0035, 0.0018, 0.0011, 0.0007, 0.0005};
using ModeArray = std::array<double, kNumModes>;

inline double midiToFreq(int midi) { return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0); }

inline double interpAnchors(double m, const double (*anchors)[2], int n) {
    if (m <= anchors[0][0]) return anchors[0][1];
    if (m >= anchors[n - 1][0]) return anchors[n - 1][1];
    for (int i = 0; i < n - 1; ++i) {
        if (m <= anchors[i + 1][0]) {
            const double t = (m - anchors[i][0]) / (anchors[i + 1][0] - anchors[i][0]);
            return anchors[i][1] + t * (anchors[i + 1][1] - anchors[i][1]);
        }
    }
    return 0.0;
}

inline double tipMassRatio(int midi) {
    static constexpr double anchors[5][2] = {{33.0, 0.10}, {52.0, 0.00}, {62.0, 0.00}, {74.0, 0.02}, {96.0, 0.01}};
    return interpAnchors(midi, anchors, 5);
}

inline ModeArray eigenvalues(double mu) {
    struct Row { double mu; double betas[kNumModes]; };
    static constexpr Row table[8] = {
        {0.00, {1.8751, 4.6941, 7.8548, 10.9955, 14.1372, 17.2788, 20.4204}},
        {0.01, {1.8584, 4.6849, 7.8504, 10.9930, 14.1356, 17.2776, 20.4195}},
        {0.05, {1.7920, 4.6477, 7.8316, 10.9830, 14.1288, 17.2726, 20.4158}},
        {0.10, {1.7227, 4.6024, 7.8077, 10.9700, 14.1198, 17.2660, 20.4110}},
        {0.15, {1.6625, 4.5618, 7.7859, 10.9580, 14.1114, 17.2598, 20.4065}},
        {0.20, {1.6097, 4.5254, 7.7659, 10.9470, 14.1036, 17.2540, 20.4023}},
        {0.30, {1.5201, 4.4620, 7.7310, 10.9280, 14.0894, 17.2434, 20.3946}},
        {0.50, {1.3853, 4.3601, 7.6745, 10.8970, 14.0650, 17.2252, 20.3814}},
    };
    const double mc = std::clamp(mu, 0.0, 0.50);
    int lo = 0;
    for (int i = 0; i < 8; ++i) if (table[i].mu <= mc) lo = i;
    const int hi = std::min(lo + 1, 7);
    const double t = table[hi].mu > table[lo].mu ? (mc - table[lo].mu) / (table[hi].mu - table[lo].mu) : 0.0;
    ModeArray b{};
    for (int i = 0; i < kNumModes; ++i) b[i] = table[lo].betas[i] + t * (table[hi].betas[i] - table[lo].betas[i]);
    return b;
}

inline ModeArray modeRatios(double mu) {
    const ModeArray b = eigenvalues(mu);
    const double b1sq = b[0] * b[0];
    ModeArray r{};
    for (int i = 0; i < kNumModes; ++i) r[i] = (b[i] * b[i]) / b1sq;
    return r;
}

inline double reedLengthMm(int midi) {
    const double n = std::clamp(midi - 32.0, 1.0, 64.0);
    const double inches = n <= 20.0 ? 3.0 - n / 20.0 : 2.0 - (n - 20.0) / 44.0;
    return inches * 25.4;
}

// Provisional ff swing at the pickup's finger region (mm): (midi, central, low, high)
inline constexpr double kProvisionalFfSwingMm[24][2] = {
    {33, 2.46}, {36, 2.44}, {39, 2.40}, {42, 2.33}, {45, 2.23}, {48, 2.25}, {51, 2.05}, {52, 1.98},
    {53, 1.16}, {54, 1.16}, {57, 1.14}, {60, 1.11}, {63, 1.06}, {66, 0.99}, {69, 0.90}, {72, 0.77},
    {75, 0.67}, {78, 0.57}, {81, 0.48}, {84, 0.42}, {87, 0.35}, {90, 0.30}, {93, 0.25}, {96, 0.21},
};
inline constexpr double kProvisionalAirDragDbPerMm[24][2] = {
    {33, 0.259}, {36, 0.356}, {39, 0.485}, {42, 0.651}, {45, 0.863}, {48, 1.125}, {51, 1.417}, {52, 1.519},
    {53, 0.664}, {54, 0.756}, {57, 1.114}, {60, 1.633}, {63, 2.381}, {66, 3.446}, {69, 4.863}, {72, 5.921},
    {75, 7.116}, {78, 8.562}, {81, 10.32}, {84, 12.45}, {87, 15.05}, {90, 18.24}, {93, 22.16}, {96, 27.02},
};
inline constexpr int kBassTopMidi = 52;

// Log-domain interpolation that never crosses the bass/treble pickup flip at reed 20|21
inline double logInterpBySide(int midi, const double (*knots)[2], int n) {
    const bool bass = midi <= kBassTopMidi;
    int prev = -1;
    for (int i = 0; i < n; ++i) {
        const int km = static_cast<int>(knots[i][0]);
        if ((km <= kBassTopMidi) != bass) continue;
        if (km >= midi) {
            if (prev >= 0 && km > midi) {
                const double t = static_cast<double>(midi - knots[prev][0]) / (km - knots[prev][0]);
                return std::exp(std::log(knots[prev][1]) * (1.0 - t) + std::log(knots[i][1]) * t);
            }
            return knots[i][1];
        }
        prev = i;
    }
    return prev >= 0 ? knots[prev][1] : 0.0;
}

inline constexpr double kSwingMinMm = 0.02;
inline constexpr double kTargetDb = -29.55;
inline constexpr double kVoicingSlope = -0.04;

inline double pickupSwingMm(int midi) { return std::max(logInterpBySide(midi, kProvisionalFfSwingMm, 24), kSwingMinMm); }
inline double airDragDbPerMm(int midi) { return logInterpBySide(midi, kProvisionalAirDragDbPerMm, 24); }

inline double modeShape(double beta, double xi) {
    const double sigma = (std::cosh(beta) + std::cos(beta)) / (std::sinh(beta) + std::sin(beta));
    const double bx = beta * xi;
    return std::cosh(bx) - std::cos(bx) - sigma * (std::sinh(bx) - std::sin(bx));
}

inline ModeArray spatialCouplingCoefficients(double mu, double reedLenMm) {
    constexpr double kPlateActiveLengthMm = 6.0;
    constexpr int kSimpson = 32;
    const ModeArray betas = eigenvalues(mu);
    const double ell = std::clamp(kPlateActiveLengthMm / reedLenMm, 0.0, 1.0);
    const double xiStart = 1.0 - ell;
    ModeArray raw{};
    for (int m = 0; m < kNumModes; ++m) {
        const double tip = modeShape(betas[m], 1.0);
        if (std::fabs(tip) < 1e-30 || ell < 1e-12) { raw[m] = 1.0; continue; }
        const double h = ell / kSimpson;
        double sum = modeShape(betas[m], xiStart) + modeShape(betas[m], 1.0);
        for (int j = 1; j < kSimpson; ++j) sum += (j % 2 == 1 ? 4.0 : 2.0) * modeShape(betas[m], xiStart + j * h);
        raw[m] = std::clamp(std::fabs((sum * h / 3.0) / (ell * tip)), 0.0, 1.0);
    }
    ModeArray k{};
    for (int i = 0; i < kNumModes; ++i) k[i] = raw[0] > 1e-30 ? std::clamp(raw[i] / raw[0], 0.0, 1.0) : 1.0;
    return k;
}

inline double fundamentalDecayRate(int midi) { return std::max(0.005 * std::pow(midiToFreq(midi), 1.22), 3.0); }

inline double velocityExponent(int midi) {
    const double m = midi, center = 62.0, sigma = 15.0, maxExp = 1.7;
    const double t = std::exp(-0.5 * ((m - center) / sigma) * ((m - center) / sigma));
    const double minExp = m < center ? 0.55 : 1.3;
    return minExp + t * (maxExp - minExp);
}

inline double velocityScurve(double velocity) {
    const double k = 1.5;
    const double s = 1.0 / (1.0 + std::exp(-k * (velocity - 0.5)));
    const double s0 = 1.0 / (1.0 + std::exp(k * 0.5));
    const double s1 = 1.0 / (1.0 + std::exp(-k * 0.5));
    return (s - s0) / (s1 - s0);
}

inline double registerTrimDb(int midi) {
    static constexpr double anchors[13][2] = {
        {36, -2.6}, {40, -0.2}, {44, -1.6}, {48, 1.3}, {52, 0.5}, {56, -2.0}, {60, 0.0},
        {64, 0.7}, {68, 0.8}, {72, -0.6}, {76, 1.4}, {80, 2.1}, {84, 3.6},
    };
    return interpAnchors(midi, anchors, 13);
}

struct NoteParams {
    double fundamentalHz;
    ModeArray modeRatios;
    ModeArray modeAmplitudes;
    ModeArray modeDecayRates; // dB/s
};

// muOffset: extra tip (solder) mass ratio on top of the 200A's per-note value (0 = OpenWurli)
inline NoteParams noteParams(int midi, double muOffset = 0.0) {
    NoteParams p{};
    p.fundamentalHz = midiToFreq(midi);
    const double mu = std::max(0.0, tipMassRatio(midi) + muOffset);
    p.modeRatios = modeRatios(mu);
    const double base = fundamentalDecayRate(midi);
    const ModeArray k = spatialCouplingCoefficients(mu, reedLengthMm(midi));
    for (int i = 0; i < kNumModes; ++i) {
        p.modeDecayRates[i] = base * p.modeRatios[i] * p.modeRatios[i];
        p.modeAmplitudes[i] = kBaseModeAmplitudes[i] * k[i];
    }
    return p;
}

// =============================================================================
// Electrostatic pickup law and constants (pickup.rs)
// =============================================================================
inline constexpr double kCTotal = 240.0e-12;          // plate node capacitance (LOW confidence upstream)
inline constexpr double kC2Base = 220.0e-12;          // C-2 at TR-1's base
inline constexpr double kFcRefC2Ground = 880.5;
inline constexpr double kCTotalRef = 240.0e-12;
inline constexpr double kVPolarizing = 147.0;
inline constexpr double kCReed0 = 3.0e-12;
inline constexpr double kPickupRestOffsetMm = 0.085;

inline double pickupFcForCTotal(double cTotal) { return kFcRefC2Ground * (kCTotalRef + kC2Base) / (cTotal + kC2Base); }
inline const double kPickupFc = pickupFcForCTotal(kCTotal);
inline constexpr double kPickupSensitivity = kVPolarizing * kCReed0 / kCTotal;

// Natural cubic spline on a uniform grid; held at the end knots outside the solved range
inline double evalLaw(const PickupLawTable& t, double u) {
    const int last = t.n - 2;
    const double pos = std::clamp((u - t.u0) / t.du, 0.0, static_cast<double>(last + 1));
    const int i = std::min(static_cast<int>(pos), last);
    const double f = pos - i, a = 1.0 - f, h2 = t.du * t.du / 6.0;
    return a * t.y[i] + f * t.y[i + 1] + ((a * a * a - a) * t.m[i] + (f * f * f - f) * t.m[i + 1]) * h2;
}

inline const PickupLawTable& pickupLawFor(int midi) {
    const int reed = std::max(0, midi - 32);
    if (reed <= 14) return R01_14;
    if (reed <= 20) return R15_20;
    if (reed <= 42) return R21_42;
    return R43_64;
}

// +1 where the finger band sits above the reed (bass, reeds 1-20), -1 below (treble)
inline double pickupBandSign(int midi) { return std::max(0, midi - 32) <= 20 ? 1.0 : -1.0; }

// Multi-harmonic RMS of the pickup source through the one-pole high-pass, 64-point DFT
inline double pickupRmsProxyLaw(int midi, double swingMm, double f0, double fc) {
    const PickupLawTable& law = pickupLawFor(midi);
    const double a = pickupBandSign(midi) * swingMm;
    const double rest = evalLaw(law, -kPickupRestOffsetMm);
    constexpr int N = 64;
    // Twiddle factors, shared by every note-on (this runs on the audio thread)
    struct Twiddles {
        double c[N], s[N];
        Twiddles() { for (int k = 0; k < N; ++k) { c[k] = std::cos(kTau * k / N); s[k] = std::sin(kTau * k / N); } }
    };
    static const Twiddles tw;
    double s[N];
    for (int i = 0; i < N; ++i) s[i] = evalLaw(law, a * tw.s[i] - kPickupRestOffsetMm) - rest;
    double sumSq = 0.0;
    for (int n = 1; n <= 8; ++n) {
        double re = 0.0, im = 0.0;
        for (int i = 0; i < N; ++i) {
            const int k = (n * i) & (N - 1);
            re += s[i] * tw.c[k];
            im -= s[i] * tw.s[k];
        }
        const double cn = 2.0 * std::sqrt(re * re + im * im) / N;
        const double nf = n * f0;
        const double hpf = nf / std::sqrt(nf * nf + fc * fc);
        sumSq += (cn * hpf) * (cn * hpf);
    }
    return std::sqrt(sumSq);
}

// Post-pickup voicing gain: levels the keyboard against C4 (tables::output_scale)
inline double outputScale(int midi, double velocity) {
    const double swing = pickupSwingMm(midi);
    const double f0 = midiToFreq(midi);
    const double sv = velocityScurve(velocity);
    const double effSwing = std::max(swing * std::pow(sv, velocityExponent(midi)), 1e-6);
    const double effSwingRef = std::max(pickupSwingMm(60) * std::pow(sv, velocityExponent(60)), 1e-6);
    const double rms = pickupRmsProxyLaw(midi, effSwing, f0, kPickupFc);
    const double rmsRef = pickupRmsProxyLaw(60, effSwingRef, midiToFreq(60), kPickupFc);
    const double flatDb = -20.0 * std::log10(rms / rmsRef);
    const double voicingDb = kVoicingSlope * std::max(midi - 60.0, 0.0);
    const double trim = registerTrimDb(midi) * std::pow(velocity, 1.3);
    return std::pow(10.0, (kTargetDb + flatDb + voicingDb + trim) / 20.0);
}

// =============================================================================
// Per-note variation (variation.rs)
// =============================================================================
inline double hashUnit(int midi, uint32_t seed) {
    uint32_t h = 2166136261u;
    h ^= static_cast<uint32_t>(midi);
    h *= 16777619u;
    h ^= seed;
    h *= 16777619u;
    h ^= h >> 16;
    h *= 2654435769u;
    return static_cast<double>(h & 0x00FFFFFFu) / 16777216.0;
}
inline double freqDetune(int midi) { return 1.0 + (hashUnit(midi, 0xDEAD) * 2.0 - 1.0) * 0.00173; }
inline ModeArray modeAmplitudeOffsets(int midi) {
    ModeArray o{};
    for (int i = 0; i < kNumModes; ++i) o[i] = 1.0 + (hashUnit(midi, 0xBEEF + i) * 2.0 - 1.0) * 0.08;
    return o;
}

// =============================================================================
// Hammer (hammer.rs): Gaussian dwell filter, onset ramp, attack noise
// =============================================================================
inline double dwellTime(double velocity, double f0) {
    return std::clamp((0.75 + 0.25 * (1.0 - velocity)) / f0, 0.0003, 0.020);
}
inline double onsetRampTime(double velocity, double f0) {
    return std::max((1.0 + (1.0 - velocity)) / f0, 0.002);
}
inline ModeArray dwellAttenuation(double velocity, double f0, const ModeArray& ratios) {
    const double tDwell = dwellTime(velocity, f0);
    const double sigmaSq = 64.0;
    ModeArray a{};
    for (int i = 0; i < kNumModes; ++i) {
        const double ft = f0 * ratios[i] * tDwell;
        a[i] = std::exp(-ft * ft / (2.0 * sigmaSq));
    }
    if (a[0] > 1e-30) {
        const double a0 = a[0];
        for (auto& x : a) x /= a0;
    }
    return a;
}

class AttackNoise {
public:
    static constexpr uint32_t kFadeInSamples = 16;

    void init(double velocity, double f0, double fs, uint32_t seed, double levelScale = 1.0) {
        amplitude_ = 0.025 * velocity * velocity * levelScale;
        decay_ = std::exp(-1.0 / (0.003 * fs));
        remaining_ = static_cast<uint32_t>(0.015 * fs);
        fadeIn_ = kFadeInSamples;
        bpf_ = Biquad::bandpass(std::clamp(f0 * 5.0, 200.0, 2000.0), 0.7, fs);
        rng_ = seed;
    }
    bool done() const { return remaining_ == 0; }
    double next() {
        if (remaining_ == 0) return 0.0;
        double env = 1.0;
        if (fadeIn_ > 0) {
            const double t = static_cast<double>(kFadeInSamples - fadeIn_) / kFadeInSamples;
            --fadeIn_;
            env = 0.5 * (1.0 - std::cos(kPi * t));
        }
        rng_ = rng_ * 1664525u + 1013904223u;
        const double noise = static_cast<double>(static_cast<int32_t>(rng_)) / 2147483647.0;
        const double out = amplitude_ * env * bpf_.process(noise);
        amplitude_ *= decay_;
        --remaining_;
        return out;
    }

private:
    double amplitude_ = 0.0, decay_ = 0.0;
    uint32_t remaining_ = 0, fadeIn_ = 0, rng_ = 0;
    Biquad bpf_;
};

// =============================================================================
// Modal reed (reed.rs): 7 quadrature oscillators, onset ramp, damper, air drag
// =============================================================================
class ModalReed {
public:
    void init(double f0, const ModeArray& ratios, const ModeArray& amps, const ModeArray& decayDb,
              double onsetTime, double velocity, double fs) {
        baseF0_ = f0;
        fs_ = fs;
        ratios_ = ratios;
        for (int i = 0; i < kNumModes; ++i) {
            Mode& m = modes_[i];
            const double inc = kTau * f0 * ratios[i] / fs;
            m.s = 0.0;
            m.c = 1.0;
            m.cosInc = std::cos(inc);
            m.sinInc = std::sin(inc);
            m.amplitude = amps[i];
            m.decayMult = std::exp(-(decayDb[i] / 8.686) / fs);
            m.envelope = 1.0;
            m.damperRate = 0.0;
            m.damperMult = 1.0;
        }
        sample_ = 0;
        onsetRampSamples_ = static_cast<uint64_t>(std::llround(onsetTime * fs));
        onsetRampInc_ = onsetRampSamples_ > 0 ? kPi / static_cast<double>(onsetRampSamples_) : 0.0;
        onsetShapeExp_ = 1.0 + (1.0 - velocity);
        damperActive_ = false;
        damperRampSamples_ = 0.0;
        damperReleaseCount_ = 0.0;
        damperRampDone_ = false;
        dragK_ = 0.0;
        pitchScale_ = 1.0;
    }

    // Retunes every mode by a frequency ratio; the phasors keep their state, so a bend is seamless
    void setPitchScale(double scale) {
        if (std::fabs(scale - pitchScale_) < 1e-7) return;
        pitchScale_ = scale;
        for (int i = 0; i < kNumModes; ++i) {
            const double inc = kTau * baseF0_ * ratios_[i] * scale / fs_;
            modes_[i].cosInc = std::cos(inc);
            modes_[i].sinInc = std::sin(inc);
        }
    }

    void setAirDrag(double betaDbPerMm, double swingMm, double fs) {
        dragK_ = (betaDbPerMm / 8.686) * std::fabs(modes_[0].amplitude) * swingMm / fs;
    }

    void startDamper(int midi, double fs, double rateScale = 1.0) {
        if (midi >= 92) return; // top 5 keys have no damper
        const double baseRate = std::max(55.0 * std::pow(2.0, (midi - 60.0) / 24.0), 0.5) * rateScale;
        for (int m = 0; m < kNumModes; ++m) {
            const double factor = std::min(baseRate * std::pow(3.0, m), 2000.0);
            modes_[m].damperRate = factor / fs;
            modes_[m].damperMult = std::exp(-modes_[m].damperRate);
        }
        const double rampTime = midi < 48 ? 0.050 : (midi < 72 ? 0.025 : 0.008);
        damperRampSamples_ = rampTime * fs;
        damperActive_ = true;
        damperReleaseCount_ = 0.0;
        damperRampDone_ = false;
    }

    double next() {
        if (damperActive_) {
            damperReleaseCount_ += 1.0;
            const double t = damperReleaseCount_;
            if (!damperRampDone_) {
                if (t > damperRampSamples_) {
                    damperRampDone_ = true;
                } else {
                    for (auto& m : modes_) m.envelope *= std::exp(-m.damperRate * t / damperRampSamples_);
                }
            }
            if (damperRampDone_) {
                for (auto& m : modes_) m.envelope *= m.damperMult;
            }
        }

        double onset = 1.0;
        if (sample_ < onsetRampSamples_) {
            const double cosine = 0.5 * (1.0 - std::cos(static_cast<double>(sample_) * onsetRampInc_));
            onset = onsetShapeExp_ <= 1.001 ? cosine
                  : onsetShapeExp_ >= 1.999 ? cosine * cosine
                                            : std::pow(cosine, onsetShapeExp_);
        }

        double sum = 0.0;
        for (auto& m : modes_) {
            sum += m.amplitude * m.s * onset * m.envelope;
            const double sNew = m.s * m.cosInc + m.c * m.sinInc;
            const double cNew = m.c * m.cosInc - m.s * m.sinInc;
            m.s = sNew;
            m.c = cNew;
            m.envelope *= m.decayMult;
        }

        if (dragK_ > 0.0) {
            const double env = modes_[0].envelope;
            modes_[0].envelope = env * (1.0 - dragK_ * env);
        }

        if ((sample_ & 1023) == 0 && sample_ > 0) {
            for (auto& m : modes_) {
                const double rInv = 1.0 / std::sqrt(m.s * m.s + m.c * m.c);
                m.s *= rInv;
                m.c *= rInv;
            }
        }
        ++sample_;
        return sum;
    }

    // thr: linear amplitude threshold
    bool isSilent(double thr) const {
        for (const auto& m : modes_) {
            if (std::fabs(m.amplitude * m.envelope) > thr) return false;
        }
        return true;
    }
    bool isDamping() const { return damperActive_; }
    void liftDamper() { damperActive_ = false; }
    double releaseSeconds(double fs) const { return damperActive_ ? damperReleaseCount_ / fs : 0.0; }
    double fundamentalEnvelope() const { return modes_[0].amplitude * modes_[0].envelope; }

private:
    struct Mode {
        double s, c, cosInc, sinInc, amplitude, decayMult, envelope, damperRate, damperMult;
    };
    std::array<Mode, kNumModes> modes_{};
    uint64_t sample_ = 0;
    uint64_t onsetRampSamples_ = 0;
    double onsetRampInc_ = 0.0;
    double onsetShapeExp_ = 1.0;
    bool damperActive_ = false;
    double damperRampSamples_ = 0.0;
    double damperReleaseCount_ = 0.0;
    bool damperRampDone_ = false;
    double dragK_ = 0.0;
    double baseF0_ = 261.6, fs_ = 48000.0, pitchScale_ = 1.0;
    ModeArray ratios_{};
};

// =============================================================================
// Electrostatic pickup network (pickup.rs)
// Source = the moving reed's field-solved capacitance (the nonlinearity);
// network = exact trapezoidal discretisation of the plate node, ~one-pole HPF.
// =============================================================================
class ElectrostaticPickup {
public:
    // restOffsetMm and cTotal default to OpenWurli's values; electropiano exposes them
    // as voicing controls (rest offset = how far the reed sits outside the finger band,
    // cTotal = plate-node capacitance, which sets the high-pass corner).
    void init(double fs, int midi, double amplitudeMm,
              double restOffsetMm = kPickupRestOffsetMm, double cTotal = kCTotal) {
        law_ = &pickupLawFor(midi);
        bandSign_ = pickupBandSign(midi);
        amplitudeMm_ = amplitudeMm;
        restOffsetMm_ = restOffsetMm;
        cReedRatio_ = kCReed0 / cTotal;
        sensitivity_ = kVPolarizing * kCReed0 / cTotal;
        beta_ = (1.0 / fs) / (2.0 * (1.0 / (kTau * pickupFcForCTotal(cTotal))));
        cRest_ = evalLaw(*law_, -restOffsetMm_);
        reset();
    }
    void reset() { w_ = 0.0; sPrev_ = 0.0; mPrev_ = 1.0; }

    // displacement: reed displacement in model units (1.0 = this note's ff swing)
    double process(double displacement) {
        const double u = bandSign_ * amplitudeMm_ * displacement - restOffsetMm_;
        return processPenetration(u);
    }
    // u: penetration past the near finger face (mm)
    double processPenetration(double u) {
        const double s = evalLaw(*law_, u) - cRest_;
        const double m = 1.0 + cReedRatio_ * s;
        const double mAvg = 0.5 * (m + mPrev_);
        w_ = (w_ * (mAvg - beta_) - (s - sPrev_)) / (mAvg + beta_);
        sPrev_ = s;
        mPrev_ = m;
        return w_ * sensitivity_;
    }
    double bandSign() const { return bandSign_; }
    double restOffsetMm() const { return restOffsetMm_; }

private:
    const PickupLawTable* law_ = &R21_42;
    double bandSign_ = -1.0, amplitudeMm_ = 1.0, restOffsetMm_ = kPickupRestOffsetMm;
    double cReedRatio_ = kCReed0 / kCTotal, sensitivity_ = kPickupSensitivity;
    double beta_ = 0.0, cRest_ = 0.0;
    double w_ = 0.0, sPrev_ = 0.0, mPrev_ = 1.0;
};

// =============================================================================
// Reed exciter (voice.rs build_reed): reed + attack noise, at a note and velocity.
// Produces reed displacement in model units (1.0 = the note's ff swing at the pickup).
// =============================================================================
struct ReedVoicing {
    double decayScale = 1.0;     // multiplies every mode's decay rate (1 = OpenWurli)
    double overtoneScale = 1.0;  // multiplies modes 2..7 amplitudes (1 = OpenWurli)
    double noiseScale = 1.0;     // attack-noise level (1 = OpenWurli)
    double damperScale = 1.0;    // damper felt rate (1 = OpenWurli)
    double dwellVelocityOffset = 0.0; // shifts the hammer dwell/onset as if struck harder (0 = OpenWurli)
    double tipMassOffset = 0.0;  // extra solder tip mass ratio: moves the overtone ratios (0 = OpenWurli)
};

class ReedExciter {
public:
    // midi is clamped to the 200A's 64 reeds for the tables; pitch follows the real note
    void noteOn(int midi, double velocity, double fs, uint32_t noiseSeed, const ReedVoicing& v) {
        midi_ = midi;
        tableMidi_ = std::clamp(midi, kMidiLo, kMidiHi);
        fs_ = fs;
        damperScale_ = v.damperScale;
        const NoteParams p = noteParams(tableMidi_, v.tipMassOffset);
        f0_ = midiToFreq(midi) * freqDetune(tableMidi_);

        const double dwellVel = std::clamp(velocity + v.dwellVelocityOffset, 0.0, 1.0);
        const ModeArray dwell = dwellAttenuation(dwellVel, f0_, p.modeRatios);
        const double onset = onsetRampTime(dwellVel, f0_);
        const ModeArray offs = modeAmplitudeOffsets(tableMidi_);
        const double velScale = std::pow(velocityScurve(velocity), velocityExponent(tableMidi_));

        ModeArray amps{}, decay{};
        for (int i = 0; i < kNumModes; ++i) {
            amps[i] = p.modeAmplitudes[i] * dwell[i] * offs[i] * velScale * (i > 0 ? v.overtoneScale : 1.0);
            decay[i] = p.modeDecayRates[i] * v.decayScale;
        }
        swingMm_ = pickupSwingMm(tableMidi_);
        reed_.init(f0_, p.modeRatios, amps, decay, onset, velocity, fs);
        reed_.setAirDrag(airDragDbPerMm(tableMidi_), swingMm_, fs);
        noise_.init(velocity, f0_, fs, noiseSeed, v.noiseScale);
        outputScale_ = wurli::outputScale(tableMidi_, velocity);
    }

    void noteOff() { reed_.startDamper(tableMidi_, fs_, damperScale_); }
    // scale 0 lifts the damper off the reed again; otherwise multiplies the felt's damping rate
    void setDamper(double scale) {
        if (scale <= 0.0) reed_.liftDamper();
        else reed_.startDamper(tableMidi_, fs_, damperScale_ * scale);
    }
    void setPitchScale(double scale) { reed_.setPitchScale(scale); }

    // Reed displacement plus the attack-noise burst (which upstream also feeds the pickup)
    double next() { return reed_.next() + (noise_.done() ? 0.0 : noise_.next()); }

    bool isSilent() const {
        if (reed_.isDamping() && reed_.releaseSeconds(fs_) > 10.0) return true;
        return reed_.isSilent(1e-4); // -80 dB
    }
    double fundamentalHz() const { return f0_; }
    double swingMm() const { return swingMm_; }
    double outputScale() const { return outputScale_; }
    int tableMidi() const { return tableMidi_; }
    double envelope() const { return reed_.fundamentalEnvelope(); }

private:
    ModalReed reed_;
    AttackNoise noise_;
    int midi_ = 60, tableMidi_ = 60;
    double fs_ = 48000.0, f0_ = 261.6, swingMm_ = 1.0, outputScale_ = 1.0, damperScale_ = 1.0;
};

} // namespace phs::wurli
