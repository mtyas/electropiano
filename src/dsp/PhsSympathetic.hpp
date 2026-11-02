#pragma once

// Sympathetic resonance: with the dampers lifted, every undamped tine (or reed) on the harp is
// set ringing by the notes being played, each at its own pitch. The coupling path is the harp
// rail the tines are mounted on, so the bank is driven by the summed voices.
//
// One damped resonator per key (its fundamental, from the same tuning as the voices). Keys that
// are sounding as voices are skipped, since their own tine is already the voice. With the
// dampers down the bank is damped like the tines are and soon falls silent, after which it
// costs nothing.

#include <array>
#include <cmath>
#include <cstdint>

namespace phs {

class SympatheticBank {
public:
    static constexpr int kLowKey = 21, kHighKey = 108, kKeys = kHighKey - kLowKey + 1;

    void prepare(double fs) {
        fs_ = fs;
        reset();
        freq_.fill(0.0);
        coeffsDirty_ = true;
    }
    void reset() {
        y1_.fill(0.0);
        y2_.fill(0.0);
        x1_ = x2_ = 0.0;
        energy_ = 0.0;
    }

    // Tuning of key k (Hz); call when the tuning or master tune changes
    void setKeyFrequency(int key, double hz) {
        const int i = key - kLowKey;
        if (i < 0 || i >= kKeys || hz == freq_[i]) return;
        freq_[i] = hz;
        coeffsDirty_ = true;
    }

    // undamped: dampers off (pedal down). Rebuilds the resonators when it or the tuning changed.
    void setDampers(bool undamped) {
        if (undamped != undamped_) { undamped_ = undamped; coeffsDirty_ = true; }
        if (!coeffsDirty_) return;
        coeffsDirty_ = false;
        for (int i = 0; i < kKeys; ++i) {
            const double f = freq_[i];
            if (!(f > 0.0) || f >= 0.45 * fs_) { a1_[i] = a2_[i] = g_[i] = 0.0; continue; }
            // Decay as a tine of that register would ring (shorter up the keyboard); damped: ~60 ms
            const double keyScale = static_cast<double>(i) / (kKeys - 1);
            const double t60 = undamped_ ? 6.0 * std::pow(0.25, keyScale) : 0.06;
            const double rad = std::exp(-6.907755 / (t60 * fs_));
            a1_[i] = 2.0 * rad * std::cos(6.283185307179586 * f / fs_);
            a2_[i] = rad * rad;
            g_[i] = 0.5 * (1.0 - rad * rad); // with the (x - x[n-2]) input: unity gain, zero phase at resonance
        }
    }

    // Per block: which keys are sounding as voices (their resonator is not driven) and each
    // key's stereo position
    void setBlockRouting(const bool* sounding, const float* panByNote) {
        for (int i = 0; i < kKeys; ++i) {
            drive_[i] = sounding[kLowKey + i] ? 0.0 : g_[i];
            pan_[i] = panByNote[kLowKey + i];
        }
    }

    // True when the bank has anything to do (dampers off, or still ringing out)
    bool isActive() const { return undamped_ || energy_ > 1e-12; }
    // True while it still makes sound
    bool isRinging() const { return energy_ > 1e-12; }

    // x: the summed voices. Returns the mono resonance and, through side, its stereo side part.
    float process(float x, float& side) {
        double sum = 0.0, sumSide = 0.0, e = 0.0;
        // Constant-peak-gain resonators share the input difference x[n] - x[n-2]
        const double xd = static_cast<double>(x) - x2_;
        x2_ = x1_;
        x1_ = x;
        for (int i = 0; i < kKeys; ++i) {
            const double y = a1_[i] * y1_[i] - a2_[i] * y2_[i] + drive_[i] * xd;
            y2_[i] = y1_[i];
            y1_[i] = y;
            sum += y;
            sumSide += pan_[i] * y;
            e += y * y;
        }
        energy_ = e;
        side = static_cast<float>(sumSide);
        return static_cast<float>(sum);
    }

private:
    // Structure of arrays, so the per-sample loop vectorises
    alignas(32) std::array<double, kKeys> a1_{}, a2_{}, g_{}, drive_{}, pan_{}, y1_{}, y2_{};
    std::array<double, kKeys> freq_{};
    double fs_ = 48000.0, energy_ = 0.0, x1_ = 0.0, x2_ = 0.0;
    bool undamped_ = false, coeffsDirty_ = true;
};

} // namespace phs
