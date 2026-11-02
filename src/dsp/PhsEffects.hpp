#pragma once

#include "PhsParameters.hpp"
#include "wurli/WurliAmp.hpp"
#include "PhsCabinet.hpp"
#include <cmath>
#include <algorithm>
#include <array>
#include <vector>

namespace phs {

// Configures one band of the mastering EQ from the parameter array. Bands 0 and 3 can be shelves.
// Shared by the DSP and the editor's response curve.
inline void setupEqBand(wurli::Biquad& b, int band, const float* p, double fs) {
    const double f = std::min<double>(eqBandFreqHz(band, p[kEqFreqId[band]]), 0.45 * fs);
    const double gain = eqGainDb(p[kEqGainId[band]]);
    const double q = eqQ(p[kEqQId[band]]);
    const bool shelf = kEqShelfId[band] < kNumParams && stepIndex(p[kEqShelfId[band]], 1) == 1;
    if (shelf && band == 0) b.setLowShelf(f, q, gain, fs);
    else if (shelf) b.setHighShelf(f, q, gain, fs);
    else b.setPeaking(f, q, gain, fs);
}

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
        cabModel_.prepare(fs_);
        meterDecay_ = std::exp(-1.0f / (0.30f * fs_));
        // Spring tank: two springs of different length, 32-stage dispersive allpass each
        const int springBase[2] = {static_cast<int>(0.0331f * fs_), static_cast<int>(0.0413f * fs_)};
        for (int k = 0; k < 2; ++k) {
            springLen_[k] = std::max(64, springBase[k]);
            springBuf_[k].assign(springLen_[k], 0.0f);
        }

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
        for (int k = 0; k < 2; ++k) {
            std::fill(springBuf_[k].begin(), springBuf_[k].end(), 0.0f);
            springIdx_[k] = 0;
            springLp_[k] = springHp_[k] = 0.0f;
            for (auto& st : springAp_[k]) st = {0.0f, 0.0f};
        }
        meterL_ = meterR_ = satIn_ = satOut_ = 0.0f;
        compGrDb_ = 0.0f;
        cabModel_.reset();
        eqKey_ = -1.0f;
        for (auto& ch : eqBq_) for (auto& b : ch) b.reset();
        scLpL_ = scLpR_ = rmsState_ = compGr_ = 0.0f;
        limGain_ = 1.0f;
        os1_.reset();
        os2_.reset();
        osStep_ = -1;
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

    // Output meters (peak with a ~300 ms fall), compressor gain reduction in dB (<= 0) and the
    // amp stage's recent input / output peaks, for the editor
    float meterL() const { return meterL_; }
    float meterR() const { return meterR_; }
    float gainReductionDb() const { return compGrDb_; }
    float compLevelDb() const { return compLevelDb_; }
    float limiterReductionDb() const { return limGrDb_; }
    float ampInLevel() const { return satIn_; }
    float ampOutLevel() const { return satOut_; }
    // The suitcase preamp's static transfer curve (what the editor plots)
    static float suitcaseTransfer(float x, float driveParam) {
        const float g = 1.0f + 6.5f * driveParam * driveParam;
        return suitcaseShape(x, driveParam, g) / std::sqrt(1.0f + 0.45f * (g - 1.0f));
    }

    // Host tempo for the synced tremolo / phaser / echo (the engine updates it every block)
    void setTempo(float bpm) { bpm_ = std::clamp(bpm, 20.0f, 400.0f); }

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
            amp200A_.setTremoloRateScale(syncedHz(tremoloRateHz(params[kParamTremoloRate]), params[kParamTremSync], bpm_) / wurli::Tremolo::kNaturalRateHz);
            ampOut = static_cast<float>(amp200A_.process(x / (kVoiceUnitsPerVolt * kVoiceMixGain))) * kAmp200AOutputTrim;
        } else {
            // Suitcase / stage preamp: smooth tube-JFET saturation with 2nd-harmonic bloom,
            // run 2x / 4x oversampled (Quality) so the saturation does not fold back as aliasing
            ampOut = processSuitcasePreamp(x, params[kParamPreampDrive], stepIndex(params[kParamQuality], 2));
        }

        // Speaker cabinet (OpenWurli speaker model; 0 = flat DI)
        const int cabType = stepIndex(params[kParamCabType], 9);
        if (cabType == 0) {
            cabinet_.setCharacter(params[kParamCabinet]);
            ampOut = static_cast<float>(cabinet_.process(ampOut));
        } else {
            // A miked cabinet is always in the signal; the Cabinet knob becomes its drive
            cabModel_.configure(cabType, params[kParamCabMic], params[kParamCabDistance], params[kParamCabRoom], params[kParamCabinet]);
            ampOut = cabModel_.process(ampOut);
        }

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
            const float target = syncedSeconds(delayTimeSec(params[kParamDelayTime]), params[kParamEchoSync], bpm_) * fs_;
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
        if (revMix > 0.001f && stepIndex(params[kParamReverbType], 1) == 1) {
            // Spring tank: dispersive allpass chains in feedback delay loops give the "boing" chirp
            const float g = 0.55f + 0.38f * params[kParamReverbDecay];
            const float in = (sigL + sigR) * 0.5f * 0.5f;
            float wet[2];
            for (int k = 0; k < 2; ++k) {
                float y = springBuf_[k][springIdx_[k]];
                for (auto& st : springAp_[k]) {
                    const float o = kSpringAllpass * y + st.x1 - kSpringAllpass * st.y1;
                    st.x1 = y;
                    st.y1 = o;
                    y = o;
                }
                springLp_[k] += 0.42f * (y - springLp_[k]);
                float w = in + g * springLp_[k];
                springHp_[k] += 0.003f * (w - springHp_[k]);
                w -= springHp_[k];
                springBuf_[k][springIdx_[k]] = std::tanh(w);
                if (++springIdx_[k] >= springLen_[k]) springIdx_[k] = 0;
                wet[k] = springLp_[k];
            }
            sigL = sigL * (1.0f - 0.30f * revMix) + wet[0] * (0.90f * revMix) + wet[1] * (0.25f * revMix);
            sigR = sigR * (1.0f - 0.30f * revMix) + wet[1] * (0.90f * revMix) + wet[0] * (0.25f * revMix);
        } else if (revMix > 0.001f) {
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

        // Mastering EQ: four bands (low shelf / bell, two bells, high shelf / bell), stereo-linked
        {
            float key = 0.0f;
            for (int b = 0; b < 4; ++b) key += params[kEqFreqId[b]] * (3.1f + b) + params[kEqGainId[b]] * (7.3f + b) + params[kEqQId[b]] * (1.7f + b);
            key += params[kParamEqB1Shelf] * 11.0f + params[kParamEqB4Shelf] * 13.0f;
            const bool changed = key != eqKey_;
            eqKey_ = key;
            for (int b = 0; b < 4; ++b) {
                const bool active = std::fabs(eqGainDb(params[kEqGainId[b]])) > 0.05f;
                if (changed) {
                    setupEqBand(eqBq_[0][b], b, params, fs_);
                    eqBq_[1][b] = eqBq_[0][b]; // same coefficients, own state (copying is cheap)
                }
                if (active) {
                    sigL = static_cast<float>(eqBq_[0][b].process(sigL));
                    sigR = static_cast<float>(eqBq_[1][b].process(sigR));
                }
            }
        }

        // Bus compressor: stereo-linked, peak or RMS detector with a sidechain high-pass, soft knee,
        // attack / release, manual and automatic make-up, and a parallel mix
        const float compMix = params[kParamCompAmount];
        if (compMix > 0.001f) {
            const float thr = compThreshDb(params[kParamCompThresh]);
            const float ratio = compRatio(params[kParamCompRatio]);
            const float knee = compKneeDb(params[kParamCompKnee]);
            const float attack = 1.0f - std::exp(-1.0f / (0.001f * compAttackMs(params[kParamCompAttack]) * fs_));
            const float release = 1.0f - std::exp(-1.0f / (0.001f * compReleaseMs(params[kParamCompRelease]) * fs_));
            // Sidechain: high-pass the detector so bass does not pump the whole mix
            float dL = sigL, dR = sigR;
            if (params[kParamCompHpf] > 0.01f) {
                const float a = std::exp(-6.2831853f * compHpfHz(params[kParamCompHpf]) / fs_);
                scLpL_ = (1.0f - a) * dL + a * scLpL_;
                scLpR_ = (1.0f - a) * dR + a * scLpR_;
                dL -= scLpL_;
                dR -= scLpR_;
            }
            float level;
            if (stepIndex(params[kParamCompDetect], 1) == 1) {
                const float e = 0.5f * (dL * dL + dR * dR);
                rmsState_ += (1.0f - std::exp(-1.0f / (0.010f * fs_))) * (e - rmsState_);
                level = std::sqrt(rmsState_);
            } else {
                level = std::max(std::fabs(dL), std::fabs(dR));
            }
            compLevelDb_ = 20.0f * std::log10(level + 1e-9f);
            const float over = compLevelDb_ - thr;
            const float grTarget = compStaticGrDb(over, ratio, knee);
            compGr_ += (grTarget < compGr_ ? attack : release) * (grTarget - compGr_);
            const float autoGain = stepIndex(params[kParamCompAuto], 1) ? -thr * (1.0f - 1.0f / ratio) * 0.5f : 0.0f;
            const float gain = std::pow(10.0f, (compGr_ + autoGain + compMakeupDb(params[kParamCompMakeup])) / 20.0f);
            sigL += compMix * (sigL * gain - sigL);
            sigR += compMix * (sigR * gain - sigR);
            compGrDb_ = compGr_;
        } else {
            compGr_ += 0.002f * (0.0f - compGr_);
            compGrDb_ = compGr_;
            compLevelDb_ = -90.0f;
            rmsState_ = 0.0f;
        }

        // Master output gain, then the limiter (instant attack, smooth release) and a gentle safety clipper
        const float masterGain = params[kParamMasterVolume] * 1.25f;
        sigL *= masterGain;
        sigR *= masterGain;
        if (stepIndex(params[kParamLimOn], 1) == 1) {
            const float ceilLin = std::pow(10.0f, limCeilDb(params[kParamLimCeil]) / 20.0f);
            const float peak = std::max(std::fabs(sigL), std::fabs(sigR));
            const float want = peak > ceilLin ? ceilLin / peak : 1.0f;
            limGain_ = want < limGain_ ? want : limGain_ + (1.0f - std::exp(-1.0f / (0.08f * fs_))) * (1.0f - limGain_);
            sigL *= limGain_;
            sigR *= limGain_;
        } else {
            limGain_ += 0.01f * (1.0f - limGain_);
        }
        limGrDb_ = 20.0f * std::log10(limGain_ + 1e-9f);
        outL = softLimit(sigL);
        outR = softLimit(sigR);
        meterL_ = std::max(std::fabs(outL), meterL_ * meterDecay_);
        meterR_ = std::max(std::fabs(outR), meterR_ * meterDecay_);
    }

private:
    static float suitcaseShape(float x, float driveParam, float driveGain) {
        const float xDriven = x * driveGain;
        const float x2 = xDriven * xDriven;
        const float asym = 0.12f * driveParam * (x2 / (1.0f + 0.6f * x2));
        return (xDriven + asym) / std::sqrt(1.0f + 0.75f * x2);
    }

    float processSuitcasePreamp(float x, float driveParam, int qualityStep) {
        const float driveGain = 1.0f + 6.5f * driveParam * driveParam;
        const float norm = 1.0f / std::sqrt(1.0f + 0.45f * (driveGain - 1.0f));
        satIn_ = std::max(std::fabs(x), satIn_ * meterDecay_);
        if (qualityStep != osStep_) {
            osStep_ = qualityStep;
            os1_.reset();
            os2_.reset();
        }
        if (qualityStep == 0) {
            const float y0 = suitcaseShape(x, driveParam, driveGain) * norm;
            satOut_ = std::max(std::fabs(y0), satOut_ * meterDecay_);
            return y0;
        }

        double u0, u1;
        os1_.upsample(x, u0, u1);
        double y;
        if (qualityStep == 1) {
            y = os1_.downsample(suitcaseShape(static_cast<float>(u0), driveParam, driveGain),
                                suitcaseShape(static_cast<float>(u1), driveParam, driveGain));
        } else {
            double a0, a1, b0, b1;
            os2_.upsample(u0, a0, a1);
            const double ya = os2_.downsample(suitcaseShape(static_cast<float>(a0), driveParam, driveGain),
                                              suitcaseShape(static_cast<float>(a1), driveParam, driveGain));
            os2_.upsample(u1, b0, b1);
            const double yb = os2_.downsample(suitcaseShape(static_cast<float>(b0), driveParam, driveGain),
                                              suitcaseShape(static_cast<float>(b1), driveParam, driveGain));
            y = os1_.downsample(ya, yb);
        }
        satOut_ = std::max(std::fabs(static_cast<float>(y) * norm), satOut_ * meterDecay_);
        return static_cast<float>(y) * norm;
    }

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
        phaserPhase_ += syncedHz(phaserRateHz(params[kParamPhaserRate]), params[kParamPhaserSync], bpm_) * period;
        if (phaserPhase_ >= 1.0f) phaserPhase_ -= 1.0f;
        tremPhase_ += syncedHz(tremoloRateHz(params[kParamTremoloRate]), params[kParamTremSync], bpm_) * period;
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

    float meterL_ = 0.0f, meterR_ = 0.0f, meterDecay_ = 0.9999f;
    float satIn_ = 0.0f, satOut_ = 0.0f;
    float compGrDb_ = 0.0f;
    CabinetModel cabModel_;
    wurli::Biquad eqBq_[2][4];
    float eqKey_ = -1.0f;
    float scLpL_ = 0.0f, scLpR_ = 0.0f, rmsState_ = 0.0f, compGr_ = 0.0f, compLevelDb_ = -90.0f;
    float limGain_ = 1.0f, limGrDb_ = 0.0f;

    static constexpr int kSpringStages = 32;
    static constexpr float kSpringAllpass = 0.70f;
    struct ApState { float x1, y1; };
    std::vector<float> springBuf_[2];
    int springLen_[2]{64, 64}, springIdx_[2]{};
    std::array<ApState, kSpringStages> springAp_[2]{};
    float springLp_[2]{}, springHp_[2]{};

    float bpm_ = 120.0f;
    wurli::Oversampler os1_, os2_;
    int osStep_ = -1;

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
