#pragma once

#include "PhsParameters.hpp"
#include <cmath>
#include <algorithm>
#include <array>
#include <vector>

namespace phs {

class PhsEffectsChain {
public:
    void prepare(float sampleRate) {
        fs_ = std::max(8000.0f, sampleRate);
        dt_ = 1.0f / fs_;

        // Initialize BBD Chorus delay buffer (~50 ms max)
        const size_t chorusLen = static_cast<size_t>(0.06f * fs_) + 16;
        chorusBufL_.assign(chorusLen, 0.0f);
        chorusBufR_.assign(chorusLen, 0.0f);
        chorusWriteIdx_ = 0;

        // Initialize Stereo Reverb delay lines (scaled to sample rate)
        const float srScale = fs_ / 44100.0f;
        const int combBase[4] = {1116, 1356, 1491, 1617};
        const int apBase[2] = {225, 556};
        for (int i = 0; i < 4; ++i) {
            combLenL_[i] = std::max(64, static_cast<int>(combBase[i] * srScale));
            combLenR_[i] = std::max(64, static_cast<int>((combBase[i] + 23) * srScale));
            combBufL_[i].assign(combLenL_[i], 0.0f);
            combBufR_[i].assign(combLenR_[i], 0.0f);
            combIdxL_[i] = 0;
            combIdxR_[i] = 0;
            combFilterL_[i] = 0.0f;
            combFilterR_[i] = 0.0f;
        }
        for (int i = 0; i < 2; ++i) {
            apLenL_[i] = std::max(32, static_cast<int>(apBase[i] * srScale));
            apLenR_[i] = std::max(32, static_cast<int>((apBase[i] + 19) * srScale));
            apBufL_[i].assign(apLenL_[i], 0.0f);
            apBufR_[i].assign(apLenR_[i], 0.0f);
            apIdxL_[i] = 0;
            apIdxR_[i] = 0;
        }

        reset();
    }

    void reset() {
        lowStateL_ = 0.0f;
        lowStateR_ = 0.0f;
        highStateL_ = 0.0f;
        highStateR_ = 0.0f;
        preampXPrev_ = 0.0f;
        revLpState_ = 0.0f;

        tremPhase_ = 0.0f;
        phaserPhase_ = 0.0f;
        chorusPhase_ = 0.0f;
        phaserFbL_ = 0.0f;
        phaserFbR_ = 0.0f;
        for (int s = 0; s < 4; ++s) {
            phaserStateL_[s] = 0.0f;
            phaserStateR_[s] = 0.0f;
        }

        std::fill(chorusBufL_.begin(), chorusBufL_.end(), 0.0f);
        std::fill(chorusBufR_.begin(), chorusBufR_.end(), 0.0f);
        for (int i = 0; i < 4; ++i) {
            std::fill(combBufL_[i].begin(), combBufL_[i].end(), 0.0f);
            std::fill(combBufR_[i].begin(), combBufR_[i].end(), 0.0f);
            combFilterL_[i] = 0.0f;
            combFilterR_[i] = 0.0f;
        }
        for (int i = 0; i < 2; ++i) {
            std::fill(apBufL_[i].begin(), apBufL_[i].end(), 0.0f);
            std::fill(apBufR_[i].begin(), apBufR_[i].end(), 0.0f);
        }
    }

    void processStereoSample(float monoIn, const float* params, float modWheel, float& outL, float& outR) {
        const int modelType = std::clamp(static_cast<int>(std::round(params[kParamModelType] * 3.0f)), 0, 3);

        // =========================================================================
        // 1. Preamp Saturation (Passive Discrete-Gradient Antiderivative Waveshaper)
        // =========================================================================
        const float driveParam = params[kParamPreampDrive];
        const float driveGain = 1.0f + 5.5f * driveParam * driveParam;
        const float xDriven = monoIn * driveGain;
        const float dx = xDriven - preampXPrev_;
        float preampOut = 0.0f;
        // Asymmetrical tube/JFET potential phi(x) = log(cosh(x)) + 0.12 * x^2 / (1 + x^2)
        if (std::fabs(dx) > 1e-5f) {
            preampOut = (evalPreampAntideriv(xDriven) - evalPreampAntideriv(preampXPrev_)) / dx;
        } else {
            const float xMid = 0.5f * (xDriven + preampXPrev_);
            preampOut = std::tanh(xMid) + 0.12f * xMid / ((1.0f + xMid * xMid) * (1.0f + xMid * xMid));
        }
        preampXPrev_ = xDriven;
        // Compensate gain gently so higher drive adds harmonic bark without blowing up volume
        preampOut *= 1.0f / std::sqrt(1.0f + 0.65f * (driveGain - 1.0f));

        // =========================================================================
        // 2. Passive Baxandall Bass & Treble Tone Stack
        // =========================================================================
        const float bassDb = (params[kParamBassBoost] - 0.5f) * 20.0f;
        const float trebleDb = (params[kParamTrebleBoost] - 0.5f) * 20.0f;
        const float bassGain = std::pow(10.0f, bassDb / 20.0f) - 1.0f;
        const float trebleGain = std::pow(10.0f, trebleDb / 20.0f) - 1.0f;

        // One-pole crossover filters at 180 Hz (bass shelf) and 2200 Hz (treble shelf)
        const float alphaLow = 1.0f - std::exp(-2.0f * 3.14159265f * 180.0f * dt_);
        const float alphaHigh = 1.0f - std::exp(-2.0f * 3.14159265f * 2200.0f * dt_);

        lowStateL_ += alphaLow * (preampOut - lowStateL_);
        highStateL_ += alphaHigh * (preampOut - highStateL_);
        const float highBand = preampOut - highStateL_;

        float sigL = preampOut + bassGain * lowStateL_ + trebleGain * highBand;
        float sigR = sigL;

        // =========================================================================
        // 3. 4-Stage Optical All-Pass Phaser
        // =========================================================================
        const float phaserMix = params[kParamPhaserMix];
        if (phaserMix > 0.001f) {
            const float phaserRate = 0.08f + params[kParamPhaserRate] * 3.5f;
            phaserPhase_ += phaserRate * dt_;
            if (phaserPhase_ >= 1.0f) phaserPhase_ -= 1.0f;

            const float lfoL = 0.5f + 0.5f * std::sin(6.2831853f * phaserPhase_);
            const float lfoR = 0.5f + 0.5f * std::sin(6.2831853f * (phaserPhase_ + 0.18f));

            const float fMin = 240.0f;
            const float fMax = 2100.0f;
            const float fL = fMin * std::pow(fMax / fMin, lfoL);
            const float fR = fMin * std::pow(fMax / fMin, lfoR);

            const float tL = std::tan(3.14159265f * std::min(fL, 0.4f * fs_) * dt_);
            const float tR = std::tan(3.14159265f * std::min(fR, 0.4f * fs_) * dt_);
            const float aL = (1.0f - tL) / (1.0f + tL);
            const float aR = (1.0f - tR) / (1.0f + tR);

            float yL = sigL + 0.42f * phaserFbL_;
            float yR = sigR + 0.42f * phaserFbR_;

            for (int s = 0; s < 4; ++s) {
                const float nextL = aL * yL + phaserStateL_[s];
                phaserStateL_[s] = yL - aL * nextL;
                yL = nextL;

                const float nextR = aR * yR + phaserStateR_[s];
                phaserStateR_[s] = yR - aR * nextR;
                yR = nextR;
            }
            phaserFbL_ = std::tanh(yL);
            phaserFbR_ = std::tanh(yR);

            sigL = sigL * (1.0f - 0.5f * phaserMix) + yL * (0.5f * phaserMix);
            sigR = sigR * (1.0f - 0.5f * phaserMix) + yR * (0.5f * phaserMix);
        }

        // =========================================================================
        // 4. Suitcase Stereo Opto-Tremolo / Wurlitzer 200A Vibrato
        // =========================================================================
        const float tremDepth = std::clamp(params[kParamTremoloDepth] + 0.75f * modWheel, 0.0f, 1.0f);
        if (tremDepth > 0.001f) {
            const float tremRate = 0.5f + params[kParamTremoloRate] * 10.5f;
            tremPhase_ += tremRate * dt_;
            if (tremPhase_ >= 1.0f) tremPhase_ -= 1.0f;

            if (modelType == 2) {
                // Wurlitzer 200A Opto/Twin-Diode Vibrato (primarily mono amplitude modulation + subtle stereo width)
                const float s = std::sin(6.2831853f * tremPhase_);
                // Asymmetric optical lamp curve
                const float modL = 1.0f - tremDepth * 0.68f * (0.5f + 0.5f * std::tanh(1.5f * s));
                const float modR = 1.0f - tremDepth * 0.68f * (0.5f + 0.5f * std::tanh(1.5f * std::sin(6.2831853f * (tremPhase_ + 0.08f))));
                sigL *= modL;
                sigR *= modR;
            } else {
                // Rhodes Suitcase Stereo Auto-Pan with incandescent bulb thermal filament lag
                const float sL = std::sin(6.2831853f * tremPhase_);
                const float sR = -sL;
                const float panL = 1.0f - tremDepth * 0.82f * (0.5f + 0.5f * std::tanh(1.8f * sL));
                const float panR = 1.0f - tremDepth * 0.82f * (0.5f + 0.5f * std::tanh(1.8f * sR));
                sigL *= panL;
                sigR *= panR;
            }
        }

        // =========================================================================
        // 5. Vintage BBD Stereo Tri-Chorus (Dyno-My-Piano style)
        // =========================================================================
        const float chorusMix = params[kParamChorusMix];
        if (chorusMix > 0.001f && !chorusBufL_.empty()) {
            const size_t bufSize = chorusBufL_.size();
            chorusBufL_[chorusWriteIdx_] = sigL;
            chorusBufR_[chorusWriteIdx_] = sigR;

            chorusPhase_ += 0.65f * dt_;
            if (chorusPhase_ >= 1.0f) chorusPhase_ -= 1.0f;

            const float modL = 0.5f + 0.5f * std::sin(6.2831853f * chorusPhase_);
            const float modR = 0.5f + 0.5f * std::cos(6.2831853f * chorusPhase_);

            const float delayMsL = 7.5f + 4.5f * modL;
            const float delayMsR = 8.5f + 4.5f * modR;

            const float wetL = readFractionalDelay(chorusBufL_, chorusWriteIdx_, delayMsL * 0.001f * fs_);
            const float wetR = readFractionalDelay(chorusBufR_, chorusWriteIdx_, delayMsR * 0.001f * fs_);

            chorusWriteIdx_ = (chorusWriteIdx_ + 1) % bufSize;

            sigL = sigL * (1.0f - 0.45f * chorusMix) + wetL * (0.55f * chorusMix);
            sigR = sigR * (1.0f - 0.45f * chorusMix) + wetR * (0.55f * chorusMix);
        }

        // =========================================================================
        // 6. Stereo Spring / Hall Reverb
        // =========================================================================
        const float revMix = params[kParamReverbMix];
        if (revMix > 0.001f) {
            const float revDecay = 0.65f + 0.26f * params[kParamReverbDecay];
            const float damp = 0.45f; // Absorbs high-frequency hiss on each reflection
            const float alphaRev = 1.0f - std::exp(-2.0f * 3.14159265f * 3000.0f * dt_);
            const float revInRaw = (sigL + sigR) * 0.25f;
            revLpState_ += alphaRev * (revInRaw - revLpState_);
            const float revInL = revLpState_;
            const float revInR = revLpState_;

            float sumL = 0.0f;
            float sumR = 0.0f;
            for (int i = 0; i < 4; ++i) {
                // Left Comb
                const float outCombL = combBufL_[i][combIdxL_[i]];
                combFilterL_[i] = outCombL * (1.0f - damp) + combFilterL_[i] * damp;
                combBufL_[i][combIdxL_[i]] = revInL + combFilterL_[i] * revDecay;
                combIdxL_[i] = (combIdxL_[i] + 1) % combLenL_[i];
                sumL += outCombL;

                // Right Comb
                const float outCombR = combBufR_[i][combIdxR_[i]];
                combFilterR_[i] = outCombR * (1.0f - damp) + combFilterR_[i] * damp;
                combBufR_[i][combIdxR_[i]] = revInR + combFilterR_[i] * revDecay;
                combIdxR_[i] = (combIdxR_[i] + 1) % combLenR_[i];
                sumR += outCombR;
            }

            // 2 Series All-pass diffusers per channel
            for (int i = 0; i < 2; ++i) {
                const float bufOutL = apBufL_[i][apIdxL_[i]];
                const float newL = -sumL + bufOutL;
                apBufL_[i][apIdxL_[i]] = sumL + bufOutL * 0.5f;
                apIdxL_[i] = (apIdxL_[i] + 1) % apLenL_[i];
                sumL = newL;

                const float bufOutR = apBufR_[i][apIdxR_[i]];
                const float newR = -sumR + bufOutR;
                apBufR_[i][apIdxR_[i]] = sumR + bufOutR * 0.5f;
                apIdxR_[i] = (apIdxR_[i] + 1) % apLenR_[i];
                sumR = newR;
            }

            sigL = sigL * (1.0f - 0.35f * revMix) + sumL * (0.35f * revMix);
            sigR = sigR * (1.0f - 0.35f * revMix) + sumR * (0.35f * revMix);
        }

        // Master output gain + gentle safety soft-clipper
        const float masterGain = params[kParamMasterVolume] * 1.25f;
        outL = softLimit(sigL * masterGain);
        outR = softLimit(sigR * masterGain);
    }

private:
    static inline float evalPreampAntideriv(float x) {
        // Antiderivative of tanh(x) + 0.12 * x / (1 + x^2)^2
        // int tanh(x) dx = log(cosh(x)), which is |x| + log(1 + exp(-2|x|)) - log(2)
        const float ax = std::fabs(x);
        const float logCosh = ax + std::log(1.0f + std::exp(-2.0f * ax)) - 0.69314718f;
        const float asymTerm = -0.06f / (1.0f + x * x);
        return logCosh + asymTerm;
    }

    static inline float softLimit(float x) {
        if (x > 2.5f) return 1.0f;
        if (x < -2.5f) return -1.0f;
        return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
    }

    static inline float readFractionalDelay(const std::vector<float>& buf, size_t writeIdx, float delaySamples) {
        const size_t N = buf.size();
        float readPos = static_cast<float>(writeIdx) - delaySamples;
        while (readPos < 0.0f) readPos += static_cast<float>(N);
        const size_t idx0 = static_cast<size_t>(readPos) % N;
        const size_t idx1 = (idx0 + 1) % N;
        const float frac = readPos - std::floor(readPos);
        return buf[idx0] * (1.0f - frac) + buf[idx1] * frac;
    }

    float fs_ = 48000.0f;
    float dt_ = 1.0f / 48000.0f;

    float lowStateL_ = 0.0f;
    float lowStateR_ = 0.0f;
    float highStateL_ = 0.0f;
    float highStateR_ = 0.0f;
    float preampXPrev_ = 0.0f;
    float revLpState_ = 0.0f;

    float tremPhase_ = 0.0f;
    float phaserPhase_ = 0.0f;
    float chorusPhase_ = 0.0f;
    float phaserFbL_ = 0.0f;
    float phaserFbR_ = 0.0f;
    std::array<float, 4> phaserStateL_{};
    std::array<float, 4> phaserStateR_{};

    std::vector<float> chorusBufL_;
    std::vector<float> chorusBufR_;
    size_t chorusWriteIdx_ = 0;

    std::array<std::vector<float>, 4> combBufL_;
    std::array<std::vector<float>, 4> combBufR_;
    std::array<int, 4> combLenL_{};
    std::array<int, 4> combLenR_{};
    std::array<int, 4> combIdxL_{};
    std::array<int, 4> combIdxR_{};
    std::array<float, 4> combFilterL_{};
    std::array<float, 4> combFilterR_{};

    std::array<std::vector<float>, 2> apBufL_;
    std::array<std::vector<float>, 2> apBufR_;
    std::array<int, 2> apLenL_{};
    std::array<int, 2> apLenR_{};
    std::array<int, 2> apIdxL_{};
    std::array<int, 2> apIdxR_{};
};

} // namespace phs
