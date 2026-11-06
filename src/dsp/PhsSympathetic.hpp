#pragma once

// Sympathetic resonance: with the dampers lifted, every undamped tine (or reed) on the harp is
// set ringing by the notes being played, each at its own pitch. The coupling path is the harp
// rail the tines are mounted on, so the bank is driven by the summed voices.
//
// One resonator per key (its fundamental, from the same tuning as the voices). Keys that are
// sounding as voices are skipped, since their own tine is already the voice. Each resonator is a
// complex one-pole z <- r e^{jw} z + g x: its output is unity-gain and in phase at resonance, and
// changing the radius r (the damping) only changes how fast it decays, never where it is, so the
// dampers can land or lift without a click. They move over ~40 ms, as felt does.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace phs {

class SympatheticBank {
public:
    static constexpr int kLowKey = 21, kHighKey = 108, kKeys = kHighKey - kLowKey + 1;

    void prepare(double fs) {
        fs_ = fs;
        dampStep_ = 1.0 / (0.040 * fs);
        reset();
        freq_.fill(0.0);
        coeffsDirty_ = true;
    }
    void reset() {
        zr_.fill(0.0);
        zi_.fill(0.0);
        energy_ = 0.0;
        lift_ = liftTarget_;
    }

    // Tuning of key k (Hz); call when the tuning or master tune changes
    void setKeyFrequency(int key, double hz) {
        const int i = key - kLowKey;
        if (i < 0 || i >= kKeys || hz == freq_[i]) return;
        freq_[i] = hz;
        coeffsDirty_ = true;
    }

    // lift: 1 = dampers off (pedal down), 0 = dampers down; in between they only lightly touch.
    // They glide there over ~40 ms in process().
    void setDampers(double lift) {
        liftTarget_ = std::clamp(lift, 0.0, 1.0);
        if (!coeffsDirty_) return;
        coeffsDirty_ = false;
        for (int i = 0; i < kKeys; ++i) {
            const double f = freq_[i];
            if (!(f > 0.0) || f >= 0.45 * fs_) { cos_[i] = sin_[i] = rUp_[i] = rDown_[i] = 0.0; continue; }
            const double w = 6.283185307179586 * f / fs_;
            cos_[i] = std::cos(w);
            sin_[i] = std::sin(w);
            // Undamped: decays as a tine of that register rings (shorter up the keyboard); damped: ~60 ms
            const double keyScale = static_cast<double>(i) / (kKeys - 1);
            rUp_[i] = std::exp(-6.907755 / (6.0 * std::pow(0.25, keyScale) * fs_));
            rDown_[i] = std::exp(-6.907755 / (0.06 * fs_));
        }
    }

    // Per block: which keys are sounding as voices (their resonator is not driven) and each
    // key's stereo position
    void setBlockRouting(const bool* sounding, const float* panByNote) {
        for (int i = 0; i < kKeys; ++i) {
            drive_[i] = sounding[kLowKey + i] ? 0.0 : 1.0;
            pan_[i] = panByNote[kLowKey + i];
        }
    }

    // True when the bank has anything to do (dampers off or moving, or still ringing out)
    bool isActive() const { return liftTarget_ > 0.0 || lift_ > 0.0 || energy_ > 1e-12; }
    // True while it still makes sound
    bool isRinging() const { return energy_ > 1e-12; }

    // x: the summed voices. Returns the mono resonance and, through side, its stereo side part.
    float process(float x, float& side) {
        // Dampers glide between down (0) and lifted (1)
        if (lift_ < liftTarget_) lift_ = std::min(liftTarget_, lift_ + dampStep_);
        else if (lift_ > liftTarget_) lift_ = std::max(liftTarget_, lift_ - dampStep_);
        const double m = lift_, xd = x;
        double sum = 0.0, sumSide = 0.0, e = 0.0;
        for (int i = 0; i < kKeys; ++i) {
            const double r = rDown_[i] + m * (rUp_[i] - rDown_[i]);
            // Re{z} of the complex pole: unity at resonance, scaled by how far the damper is
            // lifted (a damper resting on the tine stops it being set ringing)
            const double g = 2.0 * (1.0 - r) * drive_[i] * m;
            const double re = r * (cos_[i] * zr_[i] - sin_[i] * zi_[i]) + g * xd;
            const double im = r * (sin_[i] * zr_[i] + cos_[i] * zi_[i]);
            zr_[i] = re;
            zi_[i] = im;
            sum += re;
            sumSide += pan_[i] * re;
            e += re * re + im * im;
        }
        energy_ = e;
        side = static_cast<float>(sumSide);
        return static_cast<float>(sum);
    }

private:
    // Structure of arrays, so the per-sample loop vectorises
    alignas(32) std::array<double, kKeys> cos_{}, sin_{}, rUp_{}, rDown_{}, drive_{}, pan_{}, zr_{}, zi_{};
    std::array<double, kKeys> freq_{};
    double fs_ = 48000.0, energy_ = 0.0, lift_ = 0.0, liftTarget_ = 0.0, dampStep_ = 0.0005;
    bool coeffsDirty_ = true;
};

} // namespace phs
