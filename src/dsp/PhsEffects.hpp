#pragma once

#include "PhsParameters.hpp"
#include "wurli/WurliAmp.hpp"
#include <cmath>
#include <algorithm>
#include <array>
#include <vector>

namespace phs {

// Level match: the 200A chain's plugin-level output against the suitcase chain
static constexpr float kAmp200AOutputTrim = 1.5f;

// Signal chain: envelope filter -> amp (Suitcase preamp + auto-pan, or the Wurlitzer
// 200A circuit with its LDR vibrato) -> cabinet -> tone stack -> phaser -> chorus ->
// echo -> reverb -> output.
class PhsEffectsChain {
public:
    void prepare(float sampleRate) {
        fs_ = std::max(8000.0f, sampleRate);
        dt_ = 1.0f / fs_;
        alphaLow_ = 1.0f - std::exp(-2.0f * 3.14159265f * 140.0f * dt_);
        alphaHigh_ = 1.0f - std::exp(-2.0f * 3.14159265f * 2800.0f * dt_);
        alphaRev_ = 1.0f - std::exp(-2.0f * 3.14159265f * 2000.0f * dt_);

        amp200A_.prepare(fs_);
        cabinet_.prepare(fs_);

        // Echo: up to 1 s plus modulation headroom
        echoLen_ = static_cast<int>(1.1f * fs_) + 8;
        echoBufL_.assign(echoLen_, 0.0f);
        echoBufR_.assign(echoLen_, 0.0f);
        echoLpCoef_ = 1.0f - std::exp(-2.0f * 3.14159265f * 3200.0f * dt_);
        echoHpCoef_ = 1.0f - std::exp(-2.0f * 3.14159265f * 90.0f * dt_);
        echoTimeSlew_ = 1.0f - std::exp(-dt_ / 0.08f);
        wahAttack_ = 1.0f - std::exp(-dt_ / 0.002f);
        wahRelease_ = 1.0f - std::exp(-dt_ / 0.150f);

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
        ctrlCountdown_ = 0;
        lfoPrimed_ = false;
        toneBassParam_ = -1.0f;
        toneTrebleParam_ = -1.0f;
        for (int s = 0; s < 4; ++s) {
            phaserStateL_[s] = 0.0f;
            phaserStateR_[s] = 0.0f;
        }

        amp200A_.reset();
        cabinet_.reset();
        std::fill(echoBufL_.begin(), echoBufL_.end(), 0.0f);
        std::fill(echoBufR_.begin(), echoBufR_.end(), 0.0f);
        echoWrite_ = 0;
        echoTime_ = -1.0f;
        echoLpL_ = echoLpR_ = echoHpL_ = echoHpR_ = 0.0f;
        echoWowPhase_ = 0.0f;
        wahEnv_ = 0.0f;
        wahLow_ = wahBand_ = 0.0f;
        wahCounter_ = 0;
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

        const bool amp200A = stepIndex(params[kParamAmpType], 1) == 1;

        // =========================================================================
        // 1. Envelope filter (auto-wah): resonant state-variable filter swept by the
        //    playing dynamics, in front of the amp like a pedal
        // =========================================================================
        float x = monoIn;
        const float wahAmount = params[kParamWahAmount];
        if (wahAmount > 0.001f) {
            const float level = std::fabs(x);
            wahEnv_ += (level > wahEnv_ ? wahAttack_ : wahRelease_) * (level - wahEnv_);
            if ((wahCounter_++ & 7) == 0) {
                const float drive = wahEnv_ * 4.0f * std::pow(2.0f, params[kParamWahSens] * 6.0f - 1.0f);
                const float sweep = std::clamp(drive / (1.0f + drive), 0.0f, 1.0f);
                const float fc = std::min(220.0f * std::pow(2.0f, sweep * (2.0f + 2.8f * wahAmount)), 0.40f * fs_);
                wahG_ = std::tan(3.14159265f * fc * dt_);
            }
            const float k = 1.0f / 4.0f; // Q = 4
            const float a1 = 1.0f / (1.0f + wahG_ * (wahG_ + k));
            const float v3 = x - wahLow_;
            const float v1 = a1 * wahBand_ + wahG_ * a1 * v3;
            const float v2 = wahLow_ + wahG_ * v1;
            wahBand_ = 2.0f * v1 - wahBand_;
            wahLow_ = 2.0f * v2 - wahLow_;
            const float wet = 0.7f * v2 + 1.1f * v1;
            x = x + wahAmount * (wet - x);
        }

        // =========================================================================
        // 2. Amp stage
        // =========================================================================
        float ampOut;
        if (amp200A) {
            // Wurlitzer 200A: pickup volts into the DK preamp, LDR vibrato inside its
            // feedback loop, volume pot, power amp (OpenWurli). Drive = the volume pot.
            amp200A_.setVolume(wurliVolumePot(params[kParamPreampDrive]));
            amp200A_.setTremoloDepth(std::clamp(params[kParamTremoloDepth] + 0.75f * modWheel, 0.0f, 1.0f));
            amp200A_.setTremoloRateScale(tremoloRateHz(params[kParamTremoloRate]) / wurli::Tremolo::kNaturalRateHz);
            ampOut = static_cast<float>(amp200A_.process(x / (kVoiceUnitsPerVolt * kVoiceMixGain))) * kAmp200AOutputTrim;
        } else {
            // Suitcase / stage preamp: smooth tube-JFET saturation with 2nd-harmonic bloom
            const float driveParam = params[kParamPreampDrive];
            const float driveGain = 1.0f + 6.5f * driveParam * driveParam;
            const float xDriven = x * driveGain;
            const float x2 = xDriven * xDriven;
            const float asym = 0.12f * driveParam * (x2 / (1.0f + 0.6f * x2));
            ampOut = (xDriven + asym) / std::sqrt(1.0f + 0.75f * x2);
            ampOut *= 1.0f / std::sqrt(1.0f + 0.45f * (driveGain - 1.0f));
        }

        // Speaker cabinet (OpenWurli speaker model; 0 = flat DI)
        cabinet_.setCharacter(params[kParamCabinet]);
        ampOut = static_cast<float>(cabinet_.process(ampOut));

        // =========================================================================
        // 3. Passive Baxandall Bass & Treble Tone Stack (+/- 16 dB at 140 Hz / 2.8 kHz)
        // =========================================================================
        if (params[kParamBassBoost] != toneBassParam_) {
            toneBassParam_ = params[kParamBassBoost];
            bassGain_ = std::pow(10.0f, eqDb(toneBassParam_) / 20.0f) - 1.0f;
        }
        if (params[kParamTrebleBoost] != toneTrebleParam_) {
            toneTrebleParam_ = params[kParamTrebleBoost];
            trebleGain_ = std::pow(10.0f, eqDb(toneTrebleParam_) / 20.0f) - 1.0f;
        }
        lowStateL_ += alphaLow_ * (ampOut - lowStateL_);
        highStateL_ += alphaHigh_ * (ampOut - highStateL_);
        const float highBand = ampOut - highStateL_;

        float sigL = ampOut + bassGain_ * lowStateL_ + trebleGain_ * highBand;
        float sigR = sigL;

        // Control-rate LFOs: the transcendental maths behind every modulator is evaluated
        // once per kCtrlRate samples and linearly ramped, which is inaudible at <12 Hz rates.
        const float tremDepth = std::clamp(params[kParamTremoloDepth] + 0.75f * modWheel, 0.0f, 1.0f);
        if (ctrlCountdown_ == 0) {
            updateControlRateLfos(params, modelType, tremDepth);
        }
        --ctrlCountdown_;
        for (int i = 0; i < kNumLfoOutputs; ++i) {
            lfoCur_[i] += lfoInc_[i];
        }

        // =========================================================================
        // 3. 4-Stage Optical All-Pass Phaser
        // =========================================================================
        const float phaserMix = params[kParamPhaserMix];
        if (phaserMix > 0.001f) {
            const float aL = lfoCur_[kLfoPhaserAL];
            const float aR = lfoCur_[kLfoPhaserAR];

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
        if (!amp200A && tremDepth > 0.001f) {
            sigL *= lfoCur_[kLfoTremL];
            sigR *= lfoCur_[kLfoTremR];
        }

        // =========================================================================
        // 5. Vintage BBD Stereo Tri-Chorus (Dyno-My-Piano style)
        // =========================================================================
        const float chorusMix = params[kParamChorusMix];
        if (chorusMix > 0.001f && !chorusBufL_.empty()) {
            const size_t bufSize = chorusBufL_.size();
            chorusBufL_[chorusWriteIdx_] = sigL;
            chorusBufR_[chorusWriteIdx_] = sigR;

            const float wetL = readFractionalDelay(chorusBufL_, chorusWriteIdx_, lfoCur_[kLfoChorusDL]);
            const float wetR = readFractionalDelay(chorusBufR_, chorusWriteIdx_, lfoCur_[kLfoChorusDR]);

            if (++chorusWriteIdx_ >= bufSize) chorusWriteIdx_ = 0;

            sigL = sigL * (1.0f - 0.45f * chorusMix) + wetL * (0.55f * chorusMix);
            sigR = sigR * (1.0f - 0.45f * chorusMix) + wetR * (0.55f * chorusMix);
        }

        // =========================================================================
        // 6. Tape / BBD echo: filtered, saturating feedback with ping-pong spread
        // =========================================================================
        const float echoMix = params[kParamDelayMix];
        if (echoMix > 0.001f) {
            const float target = delayTimeSec(params[kParamDelayTime]) * fs_;
            if (echoTime_ < 0.0f) echoTime_ = target;
            echoTime_ += echoTimeSlew_ * (target - echoTime_); // glides like a tape speed change
            echoWowPhase_ += 0.55f * dt_;
            if (echoWowPhase_ >= 1.0f) echoWowPhase_ -= 1.0f;
            const float wow = 0.0006f * fs_ * std::sin(6.2831853f * echoWowPhase_);
            const float dl = std::clamp(echoTime_ + wow, 4.0f, static_cast<float>(echoLen_ - 4));
            const float dr = std::clamp(echoTime_ * 1.012f - wow, 4.0f, static_cast<float>(echoLen_ - 4));
            const float tapL = readDelay(echoBufL_, echoWrite_, dl);
            const float tapR = readDelay(echoBufR_, echoWrite_, dr);
            const float fb = params[kParamDelayFeedback] * 0.95f;
            // tone-shaped, gently saturated repeats with a little cross-feed
            echoLpL_ += echoLpCoef_ * (tapL - echoLpL_);
            echoLpR_ += echoLpCoef_ * (tapR - echoLpR_);
            echoHpL_ += echoHpCoef_ * (echoLpL_ - echoHpL_);
            echoHpR_ += echoHpCoef_ * (echoLpR_ - echoHpR_);
            const float repL = echoLpL_ - echoHpL_, repR = echoLpR_ - echoHpR_;
            echoBufL_[echoWrite_] = std::tanh(sigL + fb * (0.8f * repL + 0.2f * repR));
            echoBufR_[echoWrite_] = std::tanh(sigR + fb * (0.8f * repR + 0.2f * repL));
            if (++echoWrite_ >= echoLen_) echoWrite_ = 0;
            sigL += echoMix * tapL;
            sigR += echoMix * tapR;
        } else {
            echoTime_ = -1.0f;
        }

        // =========================================================================
        // 7. Stereo Spring / Hall Reverb
        // =========================================================================
        const float revMix = params[kParamReverbMix];
        if (revMix > 0.001f) {
            const float revDecay = 0.35f + 0.58f * params[kParamReverbDecay];
            const float damp = 0.58f; // Warm high-frequency acoustic damping
            const float revInRaw = (sigL + sigR) * 0.15f;
            revLpState_ += alphaRev_ * (revInRaw - revLpState_);
            const float revInL = revLpState_;
            const float revInR = revLpState_;

            float sumL = 0.0f;
            float sumR = 0.0f;
            for (int i = 0; i < 4; ++i) {
                // Left Comb
                const float outCombL = combBufL_[i][combIdxL_[i]];
                combFilterL_[i] = outCombL * (1.0f - damp) + combFilterL_[i] * damp;
                combBufL_[i][combIdxL_[i]] = revInL + combFilterL_[i] * revDecay;
                if (++combIdxL_[i] >= combLenL_[i]) combIdxL_[i] = 0;
                sumL += outCombL;

                // Right Comb
                const float outCombR = combBufR_[i][combIdxR_[i]];
                combFilterR_[i] = outCombR * (1.0f - damp) + combFilterR_[i] * damp;
                combBufR_[i][combIdxR_[i]] = revInR + combFilterR_[i] * revDecay;
                if (++combIdxR_[i] >= combLenR_[i]) combIdxR_[i] = 0;
                sumR += outCombR;
            }

            sumL *= 0.25f;
            sumR *= 0.25f;

            // 2 Series All-pass diffusers per channel
            for (int i = 0; i < 2; ++i) {
                const float bufOutL = apBufL_[i][apIdxL_[i]];
                const float newL = -sumL + bufOutL;
                apBufL_[i][apIdxL_[i]] = sumL + bufOutL * 0.5f;
                if (++apIdxL_[i] >= apLenL_[i]) apIdxL_[i] = 0;
                sumL = newL;

                const float bufOutR = apBufR_[i][apIdxR_[i]];
                const float newR = -sumR + bufOutR;
                apBufR_[i][apIdxR_[i]] = sumR + bufOutR * 0.5f;
                if (++apIdxR_[i] >= apLenR_[i]) apIdxR_[i] = 0;
                sumR = newR;
            }

            sigL = sigL * (1.0f - 0.50f * revMix) + sumL * (0.65f * revMix);
            sigR = sigR * (1.0f - 0.50f * revMix) + sumR * (0.65f * revMix);
        }

        // Master output gain + gentle safety soft-clipper
        const float masterGain = params[kParamMasterVolume] * 1.25f;
        outL = softLimit(sigL * masterGain);
        outR = softLimit(sigR * masterGain);
    }

private:
    static constexpr int kCtrlRate = 16;
    enum LfoOutput { kLfoPhaserAL, kLfoPhaserAR, kLfoTremL, kLfoTremR, kLfoChorusDL, kLfoChorusDR, kNumLfoOutputs };

    // Advances every modulator by one control period and sets per-sample ramps toward
    // the values at the end of that period.
    void updateControlRateLfos(const float* params, int modelType, float tremDepth) {
        if (!lfoPrimed_) {
            evalLfoOutputs(modelType, tremDepth, lfoCur_.data());
            lfoPrimed_ = true;
        }

        const float period = static_cast<float>(kCtrlRate) * dt_;
        phaserPhase_ += (0.08f + params[kParamPhaserRate] * 3.5f) * period;
        if (phaserPhase_ >= 1.0f) phaserPhase_ -= 1.0f;
        tremPhase_ += (0.5f + params[kParamTremoloRate] * 10.5f) * period;
        if (tremPhase_ >= 1.0f) tremPhase_ -= 1.0f;
        chorusPhase_ += 0.65f * period;
        if (chorusPhase_ >= 1.0f) chorusPhase_ -= 1.0f;

        float target[kNumLfoOutputs];
        evalLfoOutputs(modelType, tremDepth, target);

        const float invCtrl = 1.0f / static_cast<float>(kCtrlRate);
        for (int i = 0; i < kNumLfoOutputs; ++i) {
            lfoInc_[i] = (target[i] - lfoCur_[i]) * invCtrl;
        }
        ctrlCountdown_ = kCtrlRate;
    }

    void evalLfoOutputs(int modelType, float tremDepth, float* out) const {
        // Phaser all-pass coefficients
        {
            const float lfoL = 0.5f + 0.5f * std::sin(6.2831853f * phaserPhase_);
            const float lfoR = 0.5f + 0.5f * std::sin(6.2831853f * (phaserPhase_ + 0.18f));
            const float fMin = 240.0f;
            const float fMax = 2100.0f;
            const float fL = fMin * std::pow(fMax / fMin, lfoL);
            const float fR = fMin * std::pow(fMax / fMin, lfoR);
            const float tL = std::tan(3.14159265f * std::min(fL, 0.4f * fs_) * dt_);
            const float tR = std::tan(3.14159265f * std::min(fR, 0.4f * fs_) * dt_);
            out[kLfoPhaserAL] = (1.0f - tL) / (1.0f + tL);
            out[kLfoPhaserAR] = (1.0f - tR) / (1.0f + tR);
        }

        // Tremolo / auto-pan gains
        if (modelType == 2) {
            // Wurlitzer 200A Opto/Twin-Diode Vibrato (primarily mono amplitude modulation + subtle stereo width)
            const float sL = std::sin(6.2831853f * tremPhase_);
            const float sR = std::sin(6.2831853f * (tremPhase_ + 0.08f));
            // Asymmetric optical lamp curve
            out[kLfoTremL] = 1.0f - tremDepth * 0.68f * (0.5f + 0.5f * std::tanh(1.5f * sL));
            out[kLfoTremR] = 1.0f - tremDepth * 0.68f * (0.5f + 0.5f * std::tanh(1.5f * sR));
        } else {
            // Rhodes Suitcase Stereo Auto-Pan with incandescent bulb thermal filament lag
            const float sL = std::sin(6.2831853f * tremPhase_);
            out[kLfoTremL] = 1.0f - tremDepth * 0.82f * (0.5f + 0.5f * std::tanh(1.8f * sL));
            out[kLfoTremR] = 1.0f - tremDepth * 0.82f * (0.5f + 0.5f * std::tanh(-1.8f * sL));
        }

        // BBD chorus delay times (in samples)
        const float modL = 0.5f + 0.5f * std::sin(6.2831853f * chorusPhase_);
        const float modR = 0.5f + 0.5f * std::cos(6.2831853f * chorusPhase_);
        out[kLfoChorusDL] = (7.5f + 4.5f * modL) * 0.001f * fs_;
        out[kLfoChorusDR] = (8.5f + 4.5f * modR) * 0.001f * fs_;
    }

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

    static inline float readDelay(const std::vector<float>& buf, int writeIdx, float delaySamples) {
        const int n = static_cast<int>(buf.size());
        float pos = static_cast<float>(writeIdx) - delaySamples;
        if (pos < 0.0f) pos += static_cast<float>(n);
        int i0 = static_cast<int>(pos);
        const float frac = pos - static_cast<float>(i0);
        if (i0 >= n) i0 -= n;
        const int i1 = (i0 + 1 < n) ? i0 + 1 : 0;
        return buf[i0] + frac * (buf[i1] - buf[i0]);
    }

    static inline float readFractionalDelay(const std::vector<float>& buf, size_t writeIdx, float delaySamples) {
        const size_t N = buf.size();
        float readPos = static_cast<float>(writeIdx) - delaySamples;
        if (readPos < 0.0f) readPos += static_cast<float>(N);
        size_t idx0 = static_cast<size_t>(readPos);
        const float frac = readPos - static_cast<float>(idx0);
        if (idx0 >= N) idx0 -= N;
        const size_t idx1 = (idx0 + 1 < N) ? idx0 + 1 : 0;
        return buf[idx0] * (1.0f - frac) + buf[idx1] * frac;
    }

    float fs_ = 48000.0f;
    float dt_ = 1.0f / 48000.0f;
    float alphaLow_ = 0.0f;
    float alphaHigh_ = 0.0f;
    float alphaRev_ = 0.0f;

    float toneBassParam_ = -1.0f;
    float toneTrebleParam_ = -1.0f;
    float bassGain_ = 0.0f;
    float trebleGain_ = 0.0f;

    int ctrlCountdown_ = 0;
    bool lfoPrimed_ = false;
    std::array<float, kNumLfoOutputs> lfoCur_{};
    std::array<float, kNumLfoOutputs> lfoInc_{};

    wurli::Amp200A amp200A_;
    wurli::Speaker cabinet_;

    std::vector<float> echoBufL_, echoBufR_;
    int echoLen_ = 0, echoWrite_ = 0;
    float echoTime_ = -1.0f, echoTimeSlew_ = 0.0f, echoWowPhase_ = 0.0f;
    float echoLpCoef_ = 0.0f, echoHpCoef_ = 0.0f;
    float echoLpL_ = 0.0f, echoLpR_ = 0.0f, echoHpL_ = 0.0f, echoHpR_ = 0.0f;

    float wahEnv_ = 0.0f, wahAttack_ = 0.0f, wahRelease_ = 0.0f, wahG_ = 0.1f;
    float wahLow_ = 0.0f, wahBand_ = 0.0f;
    uint32_t wahCounter_ = 0;

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
