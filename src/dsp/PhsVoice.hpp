#pragma once

// One polyphonic voice, built from swappable stages:
//
//   Resonator  Rhodes tine: port-Hamiltonian hammer + cantilever beam + tone bar,
//              after Falaize & Helie, "Passive simulation of the nonlinear
//              port-Hamiltonian modeling of a Rhodes piano", J. Sound Vib. 390
//              (2017), as implemented in PyPHS (github.com/pyphs/pyphs,
//              dictionary: mechanics.felt, beams.cantilever, transducers.pickup).
//              Wurlitzer reed: OpenWurli's modal reed + hammer (wurli/WurliVoice.hpp).
//   Pickup     Electromagnetic: the flux law of Falaize & Helie eq. (25) followed by
//              the coil's RLC resonance. Electrostatic: OpenWurli's field-solved
//              overlap law into the 200A plate network.
//
// Any resonator can drive any pickup. Voice output is in "voice units" (the level
// the suitcase chain expects); kVoiceUnitsPerVolt converts to the 200A pickup's
// volts so that reed + electrostatic + 200A amp reproduces OpenWurli exactly.

#include "PhsParameters.hpp"
#include "wurli/WurliVoice.hpp"
#include <cmath>
#include <algorithm>
#include <array>

namespace phs {

static constexpr float kPi = 3.14159265358979323846f;
static constexpr float kTwoPi = 6.28318530717958647692f;

// Modal degrees of freedom of the tine:
// Modes 0-2: Euler-Bernoulli cantilever modes (cosh(bL)cos(bL) = -1), as in PyPHS beams.cantilever
// Modes 3-4: 2nd/3rd harmonic mounting/tone-bar resonances (electropiano extension, not in PyPHS)
static constexpr int kNumBeamModes = 5;

static constexpr float kCantileverBetaL[3] = {1.87510407f, 4.69409113f, 7.85475744f};
static constexpr float kModalFreqRatios[kNumBeamModes] = {1.0000000f, 6.2668933f, 17.547467f, 2.0002000f, 3.0006000f};
// Extra pickup coupling of the bell modes (the tine tip's bend is close to the pickup)
static constexpr float kChimePickupBoost[kNumBeamModes] = {1.0f, 20.0f, 10.0f, 1.0f, 1.0f};

// Normalised cantilever eigenfunction phi_m(x/L) (|phi_m(1)| == 1 at the free tip)
inline float evalCantileverModeShape(int modeIdx, float xNorm) {
    xNorm = std::clamp(xNorm, 0.02f, 1.0f);
    if (modeIdx < 3) {
        const float bL = kCantileverBetaL[modeIdx];
        const float bx = bL * xNorm;
        const float sigma = (std::cosh(bL) + std::cos(bL)) / (std::sinh(bL) + std::sin(bL));
        return 0.5f * ((std::cosh(bx) - std::cos(bx)) - sigma * (std::sinh(bx) - std::sin(bx)));
    } else if (modeIdx == 3) {
        return std::sin(kPi * xNorm) * 0.55f;
    }
    return std::sin(1.5f * kPi * xNorm) * 0.38f;
}

// Falaize & Helie (JSV 2017) eq. (25): magnetic flux sensitivity of the Rhodes pickup to
// the tine tip position q (m). Rb: tine tip radius, Rp: pickup magnet radius,
// Lh / Lv: horizontal / vertical tip-to-pickup distances. (dmu set to 1, as in PyPHS.)
inline double falaizeHeliePickupSensitivity(double q, double Rb, double Rp, double Lh, double Lv) {
    const double lh2 = Lh * Lh;
    const double f1 = (q - Rp + Lv) * (q - Rp + Lv) + lh2;
    const double f2 = (q + Rp + Lv) * (q + Rp + Lv) + lh2;
    return 2.0 * Rb * Rb * Rp * ((f1 - 2.0 * lh2) / (f1 * f1) - (f2 - 2.0 * lh2) / (f2 * f2));
}

struct VoiceTelemetry {
    double totalHamiltonian = 0.0;   // Stored energy H >= 0 (hammer + felt + beam + tone bar)
    double dissipatedPower = 0.0;    // Instantaneous dissipated power P_diss >= 0
    double inputPower = 0.0;         // Power the hammer transfers into the beam
    float tipDisplacement = 0.0f;    // Tip displacement at the pickup (mm)
    float tipVelocity = 0.0f;        // Normalised tip velocity
    float hammerCompression = 0.0f;  // Felt compression (mm)
    float pickupOutput = 0.0f;       // Output audio signal
};

class PhsVoice {
public:
    enum class Resonator { Tine, Reed };
    enum class Pickup { Magnetic, Electrostatic };

    void prepare(float sampleRate) {
        fs_ = std::max(8000.0f, sampleRate);
        dt_ = 1.0f / fs_;
        damperStep_ = dt_ / 0.012f;
        dcBlockR_ = 1.0f - (kTwoPi * 20.0f / fs_);
        thumpLenSamples_ = static_cast<uint64_t>(0.028f * fs_);
        relTapLenSamples_ = static_cast<uint64_t>(0.016f * fs_);
        reset();
    }

    void reset() {
        active_ = false;
        releasing_ = false;
        sustained_ = false;
        damperAmount_ = 0.0f;
        releaseEnv_ = 1.0f;
        midiNote_ = key_ = -1;
        keyDown_ = sostenutoHeld_ = false;
        damperDepth_ = reedDamperDepth_ = 0.0f;
        ageSamples_ = 0;
        releaseAgeSamples_ = 0;
        qh_ = ph_ = 0.0;
        hammerDetached_ = true;
        qTb_ = pTb_ = 0.0;
        qBeam_.fill(0.0);
        pBeam_.fill(0.0);
        resetPickupPaths();
        dcBlockX1_ = dcBlockY1_ = 0.0f;
        coeffsDirty_ = true;
        telemetry_ = {};
    }

    // channel: MIDI channel the note arrived on (MPE gives each note its own); the expression
    // arguments are the per-note controller values already received for that channel.
    // True when a new strike of this note can hit the tine while it still rings, as on the
    // instrument (a tine-only voice that stays tine-only). Reeds are restarted instead.
    bool canRestrikeInPlace(int midiNote, const float* params) const {
        return active_ && midiNote == midiNote_ && hasTine_ && !hasReed_ && !tineDone_ &&
               stepIndex(params[kParamResonator], 1) == 0 && params[kParamResMorph] <= 0.001f;
    }

    // restrike: the hammer hits the still-moving tine (see canRestrikeInPlace); the tine's motion,
    // the pickup and the output filters carry on, so a repeated note neither clicks nor loses
    // the ring it builds on
    void noteOn(int midiNote, float velocity, const float* params, int channel = 0,
                float bendSemis = 0.0f, float pressure = 0.0f, float slide = 0.5f, bool restrike = false) {
        const bool keepState = restrike && canRestrikeInPlace(midiNote, params);
        midiNote_ = midiNote;
        key_ = midiNote;
        keyDown_ = true;
        sostenutoHeld_ = false;
        damperDepth_ = 0.0f;
        reedDamperDepth_ = 0.0f;
        setCondition(midiNote, params[kParamCondition]);
        channel_ = channel;
        bendTarget_ = bendCur_ = bendSemis;
        bendApplied_ = 0.0f;
        pressure_ = pressure;
        slide_ = slide;
        velocity_ = std::clamp(velocity, 0.01f, 1.0f);
        active_ = true;
        releasing_ = false;
        sustained_ = false;
        damperAmount_ = 0.0f;
        releaseEnv_ = 1.0f;
        ageSamples_ = 0;
        releaseAgeSamples_ = 0;
        reedDamperStarted_ = false;
        if (!keepState) {
            resetPickupPaths();
            fmPhase_ = 0.0f;
            dcBlockX1_ = dcBlockY1_ = 0.0f;
        }
        tineDone_ = reedDone_ = false;
        xPrevMm_ = 0.0;
        geomKey_ = -1.0f;

        resonator_ = stepIndex(params[kParamResonator], 1) ? Resonator::Reed : Resonator::Tine;
        pickup_ = stepIndex(params[kParamPickupType], 1) ? Pickup::Electrostatic : Pickup::Magnetic;
        modelType_ = stepIndex(params[kParamModelType], 3);
        tuneHz_ = masterTuneHz(params[kParamMasterTune]);
        f0Base_ = tuneHz_ * std::pow(2.0f, (static_cast<float>(midiNote) - 69.0f) / 12.0f + (varCents_ + tuningCents_) / 1200.0f);
        f0_ = f0Base_ * std::pow(2.0f, bendSemis / 12.0f);
        bendApplied_ = bendSemis;
        keyScale_ = std::clamp((static_cast<float>(midiNote) - 21.0f) / 87.0f, 0.0f, 1.0f);

        // Morph: with the knob above zero the note runs both resonators (the chosen one is path 0,
        // the other path 1) and crossfades their pickup outputs, so it can sweep within the note
        dual_ = params[kParamResMorph] > 0.001f;
        hasReed_ = resonator_ == Resonator::Reed || dual_;
        hasTine_ = resonator_ == Resonator::Tine || dual_;
        tinePath_ = resonator_ == Resonator::Tine ? 0 : 1;
        reedPath_ = 1 - tinePath_;
        if (hasReed_) noteOnReed(params);
        if (hasTine_) noteOnTine(params, keepState); // last, so the tine's velocity shaping drives the action noise

        // Electrostatic pickup: the comb section and side of the band follow the note
        const int wMidi = std::clamp(midiNote, wurli::kMidiLo, wurli::kMidiHi);
        for (int path = 0; path < (dual_ ? 2 : 1) && !keepState; ++path) {
            const double swingMm = path == reedPath_ ? reed_.swingMm() : 1.0;
            pp_[path].es.init(fs_, wMidi, swingMm, electrostaticRestMm(params[kParamPickupSymmetry]),
                              wurli::kCTotal * std::pow(2.0, (0.45 - params[kParamPickupResonance]) * 2.0));
        }
        if (hasReed_) reed_.setPitchScale(reedPitchScale(bendSemis));
        updateExpressionTargets(params);
    }

    // The key that started the note (the engine matches note-offs with it; with Transpose
    // the voice sounds a different note)
    void setKey(int key) { key_ = key; }
    // Microtuning: this note's offset from 12-TET in cents (set before noteOn)
    void setTuningCents(float cents) { tuningCents_ = cents; }
    int getKey() const { return key_; }
    bool isKeyDown() const { return keyDown_; }
    bool isSostenutoHeld() const { return sostenutoHeld_; }
    void setSostenutoHeld(bool held) { sostenutoHeld_ = held; }

    // Per-note controllers, set from the audio thread as they arrive
    void setExpression(float bendSemis, float pressure, float slide) {
        bendTarget_ = bendSemis;
        pressure_ = pressure;
        slide_ = slide;
    }
    int getChannel() const { return channel_; }

    // Key released. damperDepth: how far the damper may close (0 = held off by the sustain or
    // sostenuto pedal, 1 = fully on; in between = half-pedalling). releaseVelocity: how fast the
    // key came up, which sets how quickly the damper lands (0.5 = the instrument's usual speed).
    void noteOff(float damperDepth, float releaseVelocity = 0.5f) {
        keyDown_ = false;
        releaseVelocity_ = std::clamp(releaseVelocity, 0.0f, 1.0f);
        // Damper landing time: 30 ms (slow release) .. 12 ms .. 5 ms (fast)
        damperStep_ = dt_ / (0.012f * std::pow(2.0f, (0.5f - releaseVelocity_) * 2.4f));
        applyDamper(damperDepth);
    }

    // Pedal movement while the key is up: the damper follows it (it can lift off again)
    void setDamper(float damperDepth) {
        if (keyDown_) return;
        applyDamper(damperDepth);
    }

    float processSample(const float* params) {
        if (!active_) return 0.0f;
        ++ageSamples_;
        if ((ageSamples_ & 7) == 0) {
            updateExpressionTargets(params);
            if ((ageSamples_ & 31) == 0) updateBend(params);
        }

        if (fmDepth_ > 0.001f) {
            fmPhase_ += fmInc_;
            if (fmPhase_ >= kTwoPi) fmPhase_ -= kTwoPi;
            fmS_ = std::sin(fmPhase_);
            fmC_ = std::cos(fmPhase_);
        } else {
            fmS_ = fmC_ = 0.0f;
        }

        const float yTine = (hasTine_ && !tineDone_) ? processTine(params) : 0.0f;
        const float yReed = (hasReed_ && !reedDone_) ? processReed(params) : 0.0f;
        float out;
        if (dual_) {
            const bool primaryReed = resonator_ == Resonator::Reed;
            out = primaryReed ? morphA_ * yReed + morphB_ * yTine : morphA_ * yTine + morphB_ * yReed;
        } else {
            out = hasReed_ ? yReed : yTine;
        }
        if ((!hasTine_ || tineDone_) && (!hasReed_ || reedDone_)) active_ = false;

        const float yDc = out - dcBlockX1_ + dcBlockR_ * dcBlockY1_;
        dcBlockX1_ = out;
        dcBlockY1_ = yDc;
        telemetry_.pickupOutput = yDc;
        return yDc;
    }

    bool isActive() const { return active_; }
    bool isReleasing() const { return releasing_; }
    int getMidiNote() const { return key_; }
    int getSoundingNote() const { return midiNote_; }
    // Fundamental frequency actually sounding (tuning, condition and bend included)
    float getFrequency() const { return f0_; }
    uint64_t getAge() const { return ageSamples_; }
    const VoiceTelemetry& getTelemetry() const { return telemetry_; }

private:
    // =========================================================================
    // Per-note expression
    // =========================================================================
    // Pressure pulls the pickup closer; slide opens the coil resonance. Both are offsets on
    // the panel knobs (so modulation and expression stack), refreshed every 8 samples.
    void updateExpressionTargets(const float* params) {
        const float pressureAmt = modAmount(params[kParamPressureAmt]);
        const float slideAmt = modAmount(params[kParamSlideAmt]);
        gapEff_ = std::clamp(params[kParamPickupDistance] + varGap_ + 0.40f * pressure_ * pressureAmt, 0.0f, 1.0f);
        qEff_ = std::clamp(params[kParamPickupResonance] + 0.55f * (slide_ - 0.5f) * 2.0f * slideAmt, 0.0f, 1.0f);
        fmDepth_ = params[kParamGapFmDepth];
        fmInc_ = std::min(3.0f, kTwoPi * f0_ * gapFmRatio(params[kParamGapFmRatio], stepIndex(params[kParamGapFmExact], 1) == 1) / fs_);
        pickupCount_ = pickupCountFromNorm(params[kParamPickupCount]);
        pickupSpreadMm_ = pickupSpreadMm(params[kParamPickupSpread]);
        const float morph = std::clamp(params[kParamResMorph], 0.0f, 1.0f);
        if (morph != morphCached_) {
            morphCached_ = morph;
            morphA_ = std::cos(0.5f * kPi * morph);
            morphB_ = std::sin(0.5f * kPi * morph);
        }
    }

    // Glide toward the target bend (about 1.5 ms) and retune the resonator when it has moved
    void updateBend(const float* params) {
        bendCur_ += 0.30f * (bendTarget_ - bendCur_);
        const float tune = masterTuneHz(params[kParamMasterTune]);
        const bool retune = tune != tuneHz_;
        if (!retune && std::fabs(bendCur_ - bendApplied_) < 0.002f) return;
        if (retune) {
            f0Base_ *= tune / tuneHz_;
            tuneHz_ = tune;
        }
        bendApplied_ = bendCur_;
        const float ratio = std::pow(2.0f, bendCur_ / 12.0f);
        f0_ = f0Base_ * ratio;
        if (hasReed_) reed_.setPitchScale(reedPitchScale(bendCur_));
        if (hasTine_) updateTuning_ = true;
    }

    // The reed's tables are built at A4 = 440 Hz: concert pitch, the key's condition and the bend
    // are applied as one frequency ratio
    double reedPitchScale(float bendSemis) const {
        return (tuneHz_ / 440.0) * std::pow(2.0, (bendSemis + (varCents_ + tuningCents_) / 100.0) / 12.0);
    }

    // Per-key variation, deterministic for each key (the same piano every time): tuning a few
    // cents off, uneven voicing, decay and hammer wear. condition 0 = factory-perfect.
    void setCondition(int midiNote, float condition) {
        auto u = [midiNote](uint32_t seed) { return static_cast<float>(wurli::hashUnit(midiNote, seed) * 2.0 - 1.0); };
        // Curved: about as before low on the knob (presets sit at 20 %), much stronger at the top
        const float c0 = std::clamp(condition, 0.0f, 1.0f), c = c0 * (0.35f + 0.65f * c0);
        // Up to +/- 20 cents, decay x0.4 .. x1.6, and clearly uneven voicing at full condition
        varCents_ = c * 20.0f * u(0xC0FFEE);
        varDecay_ = 1.0f + 0.60f * c * u(0xDECA1);
        varBell_ = 0.40f * c * u(0xBE11);
        varHard_ = 0.30f * c * u(0x4A2D);
        varGap_ = 0.25f * c * u(0x6A9);
        varToneBar_ = 0.35f * c * u(0x70BA2);
        varStrike_ = 0.15f * c * u(0x57121);
    }

    // Damper position for a released key (see noteOff)
    void applyDamper(float depth) {
        depth = std::clamp(depth, 0.0f, 1.0f);
        if (depth <= 0.02f) {
            // Held off: the note keeps ringing (if the damper was down, it lifts again)
            sustained_ = true;
            releasing_ = false;
            damperDepth_ = 0.0f;
            return;
        }
        if (!releasing_) releaseAgeSamples_ = 0;
        sustained_ = false;
        releasing_ = true;
        damperDepth_ = depth;
        // A damper that only grazes the tine (half pedal) takes much longer to stop it
        const float speed = std::pow(2.0f, (releaseVelocity_ - 0.5f) * 1.2f);
        releaseDecayMulEff_ = std::exp(-dt_ * speed * depth * depth / 0.055f);
    }

    // =========================================================================
    // Rhodes tine
    // =========================================================================
    void noteOnTine(const float* params, bool keepState = false) {
        const float touchParam = params[kParamVelocityCurve];
        const float gamma = 2.70f - 2.25f * touchParam;
        shapedVel_ = std::pow(velocity_, gamma);

        updateModalParameters(params);
        lastStrikeParam_ = params[kParamStrikePosition];
        lastDecayParam_ = params[kParamTineDecay];
        lastBellParam_ = params[kParamTineBell];
        lastTbParam_ = params[kParamToneBarCoupling];
        lastMaterialParam_ = params[kParamMaterial];

        // --- Hammer: mass + nonlinear felt (PyPHS mechanics.felt) ---
        // H_felt(xi) = k/(alpha+1) * xi^(alpha+1); damper force = lambda * xi^(alpha-1) * dxi/dt
        const float hardness = std::clamp(params[kParamHammerHardness] + varHard_, 0.0f, 1.0f);
        const float effHardness = std::clamp(hardness + 0.35f * (shapedVel_ - 0.5f), 0.02f, 0.98f);
        alphaHammer_ = (modelType_ == 2) ? (2.10f + 0.90f * effHardness) : (1.85f + 0.85f * effHardness);
        mHammer_ = 1.0;

        // Contact duration: ~2.4 ms (soft) .. ~0.4 ms (hard, bright)
        const float velStiffening = std::pow(std::max(0.08f, shapedVel_), 0.28f);
        const float tauContact = ((0.00240f - 0.00198f * effHardness) / velStiffening) * std::pow(0.55f, keyScale_);
        const float targetTipMm = 0.035f + (0.35f + 0.45f * (1.0f - 0.35f * touchParam)) * shapedVel_;
        const float totalImpulse = (massMode_[0] * targetTipMm * omegaMode_[0]) / cStrike_[0];
        const double vHammer0 = totalImpulse / (1.85 * mHammer_);
        const double xiNom = std::max(1e-4, (tauContact * vHammer0) / 2.94);
        kHammer_ = ((alphaHammer_ + 1.0) * 0.5 * mHammer_ * vHammer0 * vHammer0) / std::pow(xiNom, alphaHammer_ + 1.0);
        // Felt loss: Cushion 0 = elastic snap, 1 = heavy felt absorption
        lambdaHammer_ = (kHammer_ / vHammer0) * (0.02 + 0.75 * params[kParamHammerHysteresis]) * xiNom;

        if (!keepState) {
            qBeam_.fill(0.0);
            pBeam_.fill(0.0);
            qTb_ = pTb_ = 0.0;
        }
        // The hammer starts in contact position (touching the strike point, wherever the tine is)
        // with its strike momentum
        double wStrike = 0.0;
        for (int m = 0; m < kNumBeamModes; ++m) if (modeActive_[m]) wStrike += cStrike_[m] * qBeam_[m];
        qh_ = wStrike;
        ph_ = mHammer_ * vHammer0;
        xiPrev_ = 0.0;
        hammerDetached_ = false;
    }

    void updateModalParameters(const float* params) {
        const float xStrike = strikePositionXL(std::clamp(params[kParamStrikePosition] + varStrike_, 0.0f, 1.0f));
        const float decayParam = params[kParamTineDecay];
        const float material = params[kParamMaterial];
        const float bellParam = std::clamp(params[kParamTineBell] + varBell_ + materialBellLift(material), 0.0f, 1.0f);
        const float toneBarParam = std::clamp(params[kParamToneBarCoupling] + varToneBar_, 0.0f, 1.0f);
        const float hardness = std::clamp(params[kParamHammerHardness] + varHard_, 0.0f, 1.0f);

        float baseT60 = tineBaseT60(decayParam) * std::pow(0.50f, keyScale_) * materialDecayMul(material) * varDecay_;
        if (modelType_ == 2) baseT60 *= 0.68f; // steel-reed voiced tine

        for (int m = 0; m < kNumBeamModes; ++m) {
            float ratio = kModalFreqRatios[m];
            if (modelType_ == 2 && m == 1) ratio = 5.45f - 0.30f * toneBarParam;
            else if (modelType_ == 1 && m == 1) ratio = 6.268f;
            if (m == 1 || m == 2) ratio *= materialBellStretch(material);

            const float fm = f0_ * ratio;
            if (fm >= 0.44f * fs_) {
                modeActive_[m] = false;
                qBeam_[m] = pBeam_[m] = 0.0;
                cStrike_[m] = cPickup_[m] = 0.0f;
                omegaMode_[m] = 100.0f;
                dampMode_[m] = 1000.0f;
                massMode_[m] = 1.0f;
                stiffMode_[m] = 10000.0f;
                continue;
            }
            modeActive_[m] = true;
            // Pre-warped so the midpoint rule oscillates at exactly fm
            omegaMode_[m] = 2.0f * fs_ * std::tan(kPi * fm * dt_);
            const float mEff = 25.0f;
            massMode_[m] = mEff;
            stiffMode_[m] = mEff * omegaMode_[m] * omegaMode_[m];

            float modeT60 = baseT60;
            if (m == 1) modeT60 = (0.025f + 0.60f * bellParam * bellParam) * (0.35f + 0.65f * keyScale_) * materialBellDecayMul(material);
            else if (m == 2) modeT60 = (0.010f + 0.065f * bellParam) * (0.4f + 0.6f * (1.0f - keyScale_)) * materialBellDecayMul(material);
            else if (m == 3) modeT60 = baseT60 * (0.45f + 0.45f * toneBarParam);
            else if (m == 4) modeT60 = baseT60 * (0.30f + 0.35f * toneBarParam);
            dampMode_[m] = 13.81551f / std::max(0.012f, modeT60);

            const float rawShape = evalCantileverModeShape(m, xStrike);
            if (m == 0) {
                cStrike_[m] = 0.50f;
            } else if (m == 1) {
                const float chimeExcite = bellParam * (0.15f + 0.45f * hardness) * (0.30f + 0.70f * shapedVel_);
                cStrike_[m] = -chimeExcite * (0.45f + 0.55f * std::fabs(rawShape));
            } else if (m == 2) {
                cStrike_[m] = bellParam * (0.08f + 0.25f * hardness) * (0.25f + 0.75f * shapedVel_) * std::fabs(rawShape);
            } else if (m == 3) {
                cStrike_[m] = rawShape * (0.02f + 0.38f * toneBarParam) * (0.35f + 0.65f * shapedVel_);
            } else {
                cStrike_[m] = rawShape * (0.02f + 0.28f * toneBarParam) * (0.30f + 0.70f * shapedVel_);
            }

            if (m == 0) {
                cPickup_[m] = 1.0f;
            } else if (m == 1) {
                const float bellAmt = bellParam * ((modelType_ == 2) ? (0.12f + 0.18f * bellParam)
                                                 : (modelType_ == 1) ? (0.18f + 0.24f * bellParam)
                                                                     : (0.15f + 0.22f * bellParam));
                cPickup_[m] = bellAmt * (0.45f + 0.55f * keyScale_);
            } else if (m == 2) {
                cPickup_[m] = bellParam * ((modelType_ == 2) ? 0.08f : 0.12f) * (0.40f + 0.60f * keyScale_);
            } else if (m == 3) {
                cPickup_[m] = (0.02f + 0.35f * toneBarParam) * (0.3f + 0.7f * shapedVel_);
            } else {
                const float h3 = (modelType_ == 2) ? (0.04f + 0.30f * toneBarParam) : (0.02f + 0.25f * toneBarParam);
                cPickup_[m] = h3 * (0.25f + 0.75f * shapedVel_);
            }
        }

        // Tone bar: detuned coupled resonator (0.35 - 1.55 Hz beating)
        const float detuneHz = 0.35f + 1.20f * toneBarParam;
        omegaTb_ = 2.0f * fs_ * std::tan(kPi * (f0_ + detuneHz) * dt_);
        massTb_ = massMode_[0] * 8.0f;
        stiffTb_ = massTb_ * omegaTb_ * omegaTb_;
        dampTb_ = dampMode_[0] * (0.70f - 0.30f * toneBarParam);
        // Coupling stiffness from the wanted shimmer: two oscillators coupled by k_c split by
        // about k_c / (sqrt(m0 * mt) * omega0) rad/s, so k_c is set from a target beat rate
        // (0.15 .. 1.05 Hz) rather than as a fixed fraction of the tine stiffness, which split
        // the fundamental by tens of Hz on every key.
        const float beatHz = 0.15f + 0.90f * toneBarParam;
        kCoupleTb_ = kTwoPi * beatHz * std::sqrt(massMode_[0] * massTb_) * omegaMode_[0];

        // An electromagnetic pickup responds to tip velocity (PyPHS: output = f(q) * dq/dt), so every
        // mode is referred to the fundamental's omega: upper modes keep their natural omega_m/omega_0
        // emphasis rather than being flattened to displacement.
        for (int m = 0; m < kNumBeamModes; ++m) {
            const float chime = (m == 1 || m == 2) ? materialChimeMul(material) : 1.0f;
            vTipWeight_[m] = modeActive_[m] ? chime * cPickup_[m] * kChimePickupBoost[m] / (massMode_[m] * omegaMode_[0]) : 0.0f;
        }
        coeffsDirty_ = true;
    }

    // Felt storage function and its discrete gradient (PyPHS: grad = 'discret')
    double feltEnergy(double xi) const {
        return xi > 0.0 ? kHammer_ / (alphaHammer_ + 1.0) * std::pow(xi, alphaHammer_ + 1.0) : 0.0;
    }
    // eOld: feltEnergy(xiOld), passed in because the contact solve evaluates many xiNew per sample
    double feltStorageForce(double xiOld, double xiNew, double eOld) const {
        const double d = xiNew - xiOld;
        if (std::fabs(d) < 1e-12) return kHammer_ * std::pow(std::max(0.0, 0.5 * (xiOld + xiNew)), alphaHammer_);
        return (feltEnergy(xiNew) - eOld) / d;
    }
    // Storage (discrete gradient) + felt damper r(xi) * dxi/dt. A felt cannot pull the
    // tine back, so the total is held at >= 0; the clamped part is extra dissipation.
    double feltForce(double xiOld, double xiNew, double eOld) const {
        const double d = xiNew - xiOld;
        const double xm = 0.5 * (xiOld + xiNew);
        const double r = xm > 0.0 ? lambdaHammer_ * std::pow(xm, alphaHammer_ - 1.0) : 0.0;
        return std::max(0.0, feltStorageForce(xiOld, xiNew, eOld) + r * d / dt_);
    }

    float processTine(const float* params) {
        if ((ageSamples_ & 63) == 0) {
            const int newModel = stepIndex(params[kParamModelType], 3);
            if (updateTuning_) {
                updateTuning_ = false;
                updateModalParameters(params);
            }
            if (newModel != modelType_ || std::fabs(params[kParamStrikePosition] - lastStrikeParam_) > 1e-4f ||
                std::fabs(params[kParamTineDecay] - lastDecayParam_) > 1e-4f ||
                std::fabs(params[kParamTineBell] - lastBellParam_) > 1e-4f ||
                std::fabs(params[kParamToneBarCoupling] - lastTbParam_) > 1e-4f ||
                std::fabs(params[kParamMaterial] - lastMaterialParam_) > 1e-4f) {
                modelType_ = newModel;
                updateModalParameters(params);
                lastStrikeParam_ = params[kParamStrikePosition];
                lastDecayParam_ = params[kParamTineDecay];
                lastBellParam_ = params[kParamTineBell];
                lastTbParam_ = params[kParamToneBarCoupling];
                lastMaterialParam_ = params[kParamMaterial];
            }
        }

        // Damper felt lands over 12 ms (no note-off clicks)
        if (releasing_) {
            damperAmount_ = damperAmount_ < damperDepth_ ? std::min(damperDepth_, damperAmount_ + damperStep_)
                                                         : std::max(damperDepth_, damperAmount_ - damperStep_);
            releaseEnv_ *= releaseDecayMulEff_;
        } else {
            // Lifting (or never down): the felt leaves the tine; what is left keeps ringing
            damperAmount_ = std::max(0.0f, damperAmount_ - damperStep_);
        }
        // A damper that only grazes the tine (half pedal) presses with less force as well
        const float damperExtraDamp = damperAmount_ * std::max(damperDepth_, damperAmount_) * ((modelType_ == 2) ? 75.0f : 55.0f);
        if (coeffsDirty_ || damperExtraDamp != cachedDamperExtra_) rebuildIntegratorCoefficients(damperExtraDamp);

        const double dtD = static_cast<double>(dt_);
        const double h = 0.5 * dtD;
        const CoupledCoeffs& c0 = coupled_;

        // Free (zero-force) predictions of every mode, used by the hammer contact solve
        const double rhs1Free = -c0.hK00 * qBeam_[0] + c0.hKc * qTb_ - c0.eps11 * pBeam_[0] - c0.a12 * pTb_;
        const double rhs2 = c0.hKc * qBeam_[0] - c0.hKtt * qTb_ - c0.a21 * pBeam_[0] - c0.eps22 * pTb_;

        // =====================================================================
        // STEP 1: hammer-beam contact, discrete-gradient implicit solve
        // The strike point displacement w = sum_m c_m q_m couples to every mode
        // (PyPHS beams.cantilever transformer chain), so the scheme is passive.
        // =====================================================================
        double contactForce = 0.0;
        double feltDissPower = 0.0;
        if (!hammerDetached_) {
            double wNow = 0.0, wFree = 0.0, sens = dtD * dtD / (2.0 * mHammer_);
            for (int m = 0; m < kNumBeamModes; ++m) {
                if (!modeActive_[m]) continue;
                const double cm = cStrike_[m];
                double qFree, dqdF;
                if (m == 0) {
                    const double dpMid = (c0.a22 * rhs1Free - c0.a12 * rhs2) * c0.invDet;
                    qFree = qBeam_[0] + c0.dtOverM0 * (pBeam_[0] + dpMid);
                    dqdF = c0.dtOverM0 * c0.a22 * h * cm * c0.invDet;
                } else {
                    const int u = m - 1;
                    const double dpMid = (-up_.hk[u] * qBeam_[m] - up_.eps[u] * pBeam_[m]) * up_.invDenom[u];
                    qFree = qBeam_[m] + up_.dtOverM[u] * (pBeam_[m] + dpMid);
                    dqdF = up_.dtOverM[u] * h * cm * up_.invDenom[u];
                }
                wNow += cm * qBeam_[m];
                wFree += cm * qFree;
                sens += cm * dqdF;
            }
            const double xiOld = qh_ - wNow;
            const double xiFree = qh_ + dtD * ph_ / mHammer_ - wFree;

            double xiNew = xiFree;
            const double eOld = feltEnergy(xiOld);
            if (xiFree > 0.0 || xiOld > 0.0) {
                // g(xi) = xi - xiFree + sens * F(xi) is increasing; bracket and solve (Illinois)
                auto g = [&](double xi) { return xi - xiFree + sens * feltForce(xiOld, xi, eOld); };
                double hi = xiFree, ghi = g(hi);
                double lo = xiFree - ghi - 1e-12, glo = g(lo); // ghi == sens * F(xiFree)
                for (int k = 0; k < 8 && glo > 0.0; ++k) { lo -= std::fabs(lo - hi) + 1e-9; glo = g(lo); }
                xiNew = hi;
                if (ghi > 0.0 && glo < 0.0) {
                    int side = 0;
                    for (int it = 0; it < 60; ++it) {
                        xiNew = (lo * ghi - hi * glo) / (ghi - glo);
                        const double gm = g(xiNew);
                        if (std::fabs(gm) < 1e-15 || std::fabs(hi - lo) < 1e-14) break;
                        if ((gm > 0.0) == (ghi > 0.0)) { hi = xiNew; ghi = gm; if (side == 1) glo *= 0.5; side = 1; }
                        else { lo = xiNew; glo = gm; if (side == -1) ghi *= 0.5; side = -1; }
                    }
                }
            }
            contactForce = feltForce(xiOld, xiNew, eOld);
            // P_diss = (F - F_storage) * dxi/dt >= 0 (felt damper, plus any clamped pull)
            feltDissPower = (contactForce - feltStorageForce(xiOld, xiNew, eOld)) * (xiNew - xiOld) / dtD;
            const double phOld = ph_;
            ph_ -= dtD * contactForce;
            qh_ += dtD * 0.5 * (phOld + ph_) / mHammer_;
            xiPrev_ = xiNew;
            if (contactForce <= 0.0 && xiNew <= 0.0 && ph_ <= 0.0 && ageSamples_ > 4) hammerDetached_ = true;
        }

        // =====================================================================
        // STEP 2: implicit midpoint step of mode 0 + tone bar and modes 1..4
        // =====================================================================
        double beamEnergy = 0.0, beamDissPower = 0.0, inputPower = 0.0;
        {
            const double fExt0 = static_cast<double>(cStrike_[0]) * contactForce;
            const double rhs1 = rhs1Free + h * fExt0;
            const double dp0Mid = (c0.a22 * rhs1 - c0.a12 * rhs2) * c0.invDet;
            const double dptMid = (-c0.a21 * rhs1 + c0.a11 * rhs2) * c0.invDet;
            const double p0Mid = pBeam_[0] + dp0Mid, ptMid = pTb_ + dptMid;
            const double p0New = pBeam_[0] + 2.0 * dp0Mid, ptNew = pTb_ + 2.0 * dptMid;
            const double q0New = qBeam_[0] + c0.dtOverM0 * p0Mid, qtNew = qTb_ + c0.dtOverMt * ptMid;
            qBeam_[0] = q0New; pBeam_[0] = p0New; qTb_ = qtNew; pTb_ = ptNew;

            const double qDiff = q0New - qtNew;
            beamEnergy += 0.5 * (c0.k0 * q0New * q0New + p0New * p0New * c0.invM0 + c0.kt * qtNew * qtNew +
                                 ptNew * ptNew * c0.invMt + c0.kc * qDiff * qDiff);
            const double v0Mid = p0Mid * c0.invM0, vtMid = ptMid * c0.invMt;
            beamDissPower += c0.R0 * v0Mid * v0Mid + c0.Rt * vtMid * vtMid;
            inputPower += fExt0 * v0Mid;
        }
        // Modes 1..4 as one branch-free loop over flat coefficient arrays (vectorises); an inactive
        // mode has all-zero coefficients and stays at rest
        {
            double* q = qBeam_.data() + 1;
            double* p = pBeam_.data() + 1;
            double e = 0.0, diss = 0.0, inP = 0.0;
            for (int u = 0; u < kUpperModes; ++u) {
                const double fExt = up_.cStrike[u] * contactForce;
                const double dpMid = (-up_.hk[u] * q[u] - up_.eps[u] * p[u] + h * fExt) * up_.invDenom[u];
                const double pMid = p[u] + dpMid;
                p[u] += 2.0 * dpMid;
                q[u] += up_.dtOverM[u] * pMid;
                e += up_.stiff[u] * q[u] * q[u] + p[u] * p[u] * up_.invM[u];
                const double vMid = pMid * up_.invM[u];
                diss += up_.R[u] * vMid * vMid;
                inP += fExt * vMid;
            }
            beamEnergy += 0.5 * e;
            beamDissPower += diss;
            inputPower += inP;
        }

        // =====================================================================
        // STEP 3: tip excursion and velocity at the pickup
        // =====================================================================
        const float tbWeight = 0.02f + 0.65f * std::clamp(params[kParamToneBarCoupling] + varToneBar_, 0.0f, 1.0f);
        const float wBody = static_cast<float>(qBeam_[0] + tbWeight * qTb_)
                          + (modeActive_[3] ? cPickup_[3] * static_cast<float>(qBeam_[3]) : 0.0f)
                          + (modeActive_[4] ? cPickup_[4] * static_cast<float>(qBeam_[4]) : 0.0f);
        float vTipNorm = static_cast<float>((pBeam_[0] / massMode_[0]) + tbWeight * (pTb_ / massTb_)) / omegaMode_[0];
        for (int m = 1; m < kNumBeamModes; ++m) vTipNorm += vTipWeight_[m] * static_cast<float>(pBeam_[m]);

        float out = runPickup(params, wBody, vTipNorm, tinePath_, false);

        // Mechanical action sounds: 110 Hz keybed thud + 920 Hz escapement click, damper tap on release
        const float thumpParam = params[kParamKeyNoise];
        if (thumpParam > 0.001f && ageSamples_ < thumpLenSamples_) {
            const float tSec = static_cast<float>(ageSamples_) * dt_;
            const float thud = std::exp(-tSec / 0.0095f) * std::sin(kTwoPi * 110.0f * tSec);
            const float click = std::exp(-tSec / 0.0032f) * std::sin(kTwoPi * 920.0f * tSec);
            out += thumpParam * (0.06f + 0.20f * shapedVel_) * (0.75f * thud + 0.35f * click);
        }
        if (releasing_ && thumpParam > 0.001f && releaseAgeSamples_ < relTapLenSamples_) {
            const float tRel = static_cast<float>(releaseAgeSamples_) * dt_;
            // The damper lands harder when the key is let go quickly
            const float tapLevel = 0.035f * damperDepth_ * (0.4f + 1.2f * releaseVelocity_);
            out += thumpParam * tapLevel * std::exp(-tRel / 0.0050f) * std::sin(kTwoPi * 380.0f * tRel);
            ++releaseAgeSamples_;
        }
        out *= releaseEnv_;

        // The free hammer keeps its (constant) kinetic energy after it leaves the tine
        const double hammerEnergy = 0.5 * ph_ * ph_ / mHammer_ + (hammerDetached_ ? 0.0 : feltEnergy(xiPrev_));
        telemetry_.totalHamiltonian = beamEnergy + hammerEnergy;
        telemetry_.dissipatedPower = beamDissPower + feltDissPower;
        telemetry_.inputPower = inputPower;
        telemetry_.tipDisplacement = wBody;
        telemetry_.tipVelocity = vTipNorm;
        telemetry_.hammerCompression = static_cast<float>(xiPrev_);

        if (releasing_ && releaseEnv_ < 1e-4f) {
            tineDone_ = true;
        } else if (hammerDetached_ && ageSamples_ > 256 && beamEnergy < 1e-9 && std::fabs(out) < 1e-6f) {
            tineDone_ = true;
        }
        return out;
    }

    // =========================================================================
    // Wurlitzer reed (OpenWurli)
    // =========================================================================
    void noteOnReed(const float* params) {
        // Touch at its centre (0.5) leaves OpenWurli's velocity response untouched
        const double gamma = std::pow(2.0, (0.5 - params[kParamVelocityCurve]) * 2.0);
        const double vel = std::pow(static_cast<double>(velocity_), gamma);
        shapedVel_ = static_cast<float>(vel);
        wurli::ReedVoicing v;
        // Every control sits at OpenWurli's calibration when at its default position
        v.decayScale = std::pow(2.0, -(params[kParamTineDecay] - 0.65) * 4.0) / materialDecayMul(params[kParamMaterial]);
        v.overtoneScale = std::pow(2.0, (params[kParamTineBell] - 0.55) * 5.0) * materialChimeMul(params[kParamMaterial]);
        v.noiseScale = std::clamp(params[kParamKeyNoise] / 0.30, 0.0, 3.5);
        v.damperScale = std::pow(2.0, (params[kParamHammerHysteresis] - 0.35) * 2.0);
        v.dwellVelocityOffset = (params[kParamHammerHardness] - 0.48) * 0.8;
        v.tipMassOffset = (params[kParamToneBarCoupling] - 0.55) * 0.12;
        noiseSeed_ = noiseSeed_ * 1664525u + 1013904223u + static_cast<uint32_t>(midiNote_);
        reed_.noteOn(midiNote_, vel, fs_, noiseSeed_, v);
    }

    float processReed(const float* params) {
        if (releasing_ && std::fabs(damperDepth_ - reedDamperDepth_) > 0.05f) {
            // Half pedal: a lightly touching felt damps the reed more slowly
            const double speed = std::pow(2.0, (releaseVelocity_ - 0.5) * 1.2);
            reed_.setDamper(damperDepth_ * damperDepth_ * speed);
            reedDamperDepth_ = damperDepth_;
            reedDamperStarted_ = true;
        } else if (!releasing_ && reedDamperStarted_) {
            reed_.setDamper(0.0);
            reedDamperDepth_ = 0.0f;
            reedDamperStarted_ = false;
        }
        const double disp = reed_.next();       // 1.0 = the note's ff swing at the pickup
        const double xMm = disp * reed_.swingMm();
        const double vNorm = (xMm - xPrevMm_) * fs_ / (wurli::kTau * reed_.fundamentalHz());
        xPrevMm_ = xMm;

        float out = runPickup(params, static_cast<float>(xMm), static_cast<float>(vNorm), reedPath_, true);

        const double env = reed_.envelope();
        telemetry_.totalHamiltonian = env * env;
        telemetry_.dissipatedPower = 0.0;
        telemetry_.inputPower = 0.0;
        telemetry_.tipDisplacement = static_cast<float>(xMm);
        telemetry_.tipVelocity = static_cast<float>(vNorm);
        telemetry_.hammerCompression = 0.0f;
        if (reed_.isSilent()) reedDone_ = true;
        return out;
    }

    // =========================================================================
    // Pickups. tipMm: displacement at the pickup (mm); vNorm: velocity / omega0 (mm)
    // =========================================================================
    float runPickup(const float* params, float tipMm, float vNorm, int path, bool asReed) {
        PickupPath& pp = pp_[path];
        if (pickup_ == Pickup::Electrostatic) {
            const double swing = electrostaticSwingScale(gapEff_) * (1.0 + 0.6 * fmDepth_ * fmS_);
            double volts;
            if (asReed) {
                volts = pp.es.process(tipMm / reed_.swingMm() * swing) * reed_.outputScale();
            } else {
                // A tine's few tenths of a mm against the comb: bandSign orients it like a reed
                volts = pp.es.processPenetration(pp.es.bandSign() * tipMm * swing - pp.es.restOffsetMm()) * kTineElectrostaticGain;
            }
            return static_cast<float>(volts * kVoiceUnitsPerVolt);
        }

        // --- Electromagnetic: Falaize & Helie eq. (25) ---
        const float gapKey = gapEff_ * 7.0f + params[kParamPickupSymmetry] * 131.0f + modelType_;
        if (gapKey != geomKey_) {
            geomKey_ = gapKey;
            const bool dyno = (modelType_ == 1 || modelType_ == 3);
            emLh_ = magneticGapMm(gapEff_) * 1e-3 * (dyno ? 0.85 : 1.0);
            emLv_ = magneticOffsetMm(params[kParamPickupSymmetry]) * 1e-3;
            emRp_ = (dyno ? 1.2 : 1.0) * 1e-3;
            // Level reference: small-signal fundamental and octave sensitivities. Half of the
            // geometry's level change is kept (it is physical), half is normalised away.
            static const double refDefault = magneticLevelRef(magneticGapMm(0.5f) * 1e-3, magneticOffsetMm(0.4f) * 1e-3, 1e-3);
            emNorm_ = -1.0 / std::sqrt(magneticLevelRef(emLh_, emLv_, emRp_) * refDefault);
        }
        // Physical FM: the gap and offset are swung at audio rate; several pickups along the tine
        // (fanned out from the base offset, so their contributions add rather than cancel) are summed in phase
        const double lhNow = emLh_ * (1.0 + 0.6 * fmDepth_ * fmS_);
        const double lvBase = emLv_ + 0.5e-3 * fmDepth_ * fmC_;
        const int n = pickupCount_;
        double sens = 0.0;
        for (int k = 0; k < n; ++k) {
            const double lv = lvBase + k * pickupSpreadMm_ * 1e-3; // fanned out from the base offset
            sens += falaizeHeliePickupSensitivity(static_cast<double>(tipMm) * 1e-3, kRb, emRp_, lhNow, lv);
        }
        sens /= n;
        const float transducerSignal = static_cast<float>(sens * emNorm_) * vNorm * kMagneticGain
                                     * (asReed ? kReedMagneticGain : 1.0f);

        // Coil RLC resonance (2-pole SVF): low-pass with a resonant presence peak
        const float pickupQParam = qEff_;
        if (pickupQParam != pp.coilQ || modelType_ != pp.coilModel) {
            const float resFreq = (modelType_ == 1) ? (1900.0f + 3300.0f * pickupQParam)
                                : (modelType_ == 2) ? (1700.0f + 2900.0f * pickupQParam)
                                                    : (1800.0f + 3100.0f * pickupQParam);
            const float g = std::tan(kPi * std::min(resFreq, 0.44f * fs_) * dt_);
            const float kDamp = 1.60f - 1.22f * pickupQParam;
            pp.a1 = 1.0f / (1.0f + g * (g + kDamp));
            pp.a2 = g * pp.a1;
            pp.a3 = g * pp.a2;
            pp.coilQ = pickupQParam;
            pp.coilModel = modelType_;
        }
        const float v3 = transducerSignal - pp.lp;
        const float v1 = pp.a1 * pp.bp + pp.a2 * v3;
        const float v2 = pp.lp + pp.a2 * pp.bp + pp.a3 * v3;
        pp.bp = 2.0f * v1 - pp.bp;
        pp.lp = 2.0f * v2 - pp.lp;
        return v2 + (0.05f + 0.95f * pickupQParam) * v1;
    }

    void resetPickupPaths() {
        for (auto& pp : pp_) {
            pp.lp = pp.bp = 0.0f;
            pp.coilQ = -1.0f;
            pp.coilModel = -1;
        }
        geomKey_ = -1.0f;
    }

    static double magneticLevelRef(double lh, double lv, double rp) {
        const double f0 = falaizeHeliePickupSensitivity(0.0, kRb, rp, lh, lv);
        const double d = 1e-6;
        const double f1 = (falaizeHeliePickupSensitivity(d, kRb, rp, lh, lv) -
                           falaizeHeliePickupSensitivity(-d, kRb, rp, lh, lv)) / (2.0 * d) * 0.3e-3;
        return std::sqrt(f0 * f0 + f1 * f1);
    }

    // =========================================================================
    // Cached implicit-midpoint coefficients (rebuilt only when parameters or the damper move)
    // =========================================================================
    struct CoupledCoeffs {
        double k0, kc, kt, hK00, hKc, hKtt;
        double eps11, eps22, a11, a12, a21, a22, invDet;
        double invM0, invMt, dtOverM0, dtOverMt, R0, Rt;
    };
    // Implicit-midpoint coefficients of modes 1..4, one array per coefficient
    static constexpr int kUpperModes = kNumBeamModes - 1;
    struct UpperModeCoeffs {
        alignas(32) double stiff[kUpperModes], hk[kUpperModes], eps[kUpperModes], invDenom[kUpperModes];
        alignas(32) double invM[kUpperModes], dtOverM[kUpperModes], R[kUpperModes], cStrike[kUpperModes];
    };

    void rebuildIntegratorCoefficients(float damperExtraDamp) {
        const double dtD = static_cast<double>(dt_);
        const double h = 0.5 * dtD;
        const double h2 = h * h;

        CoupledCoeffs& c = coupled_;
        const double m0 = massMode_[0];
        const double mt = massTb_;
        c.kc = kCoupleTb_;
        c.k0 = std::max(0.5f * stiffMode_[0], stiffMode_[0] - kCoupleTb_);
        c.kt = stiffTb_;
        const double d0 = dampMode_[0] + damperExtraDamp;
        const double dtb = dampTb_ + damperExtraDamp * 0.9f;
        c.R0 = m0 * d0;
        c.Rt = mt * dtb;
        c.hK00 = h * (c.k0 + c.kc);
        c.hKc = h * c.kc;
        c.hKtt = h * (c.kt + c.kc);
        c.eps11 = h * d0 + h2 * (c.k0 + c.kc) / m0;
        c.eps22 = h * dtb + h2 * (c.kt + c.kc) / mt;
        c.a11 = 1.0 + c.eps11;
        c.a12 = -h2 * c.kc / mt;
        c.a21 = -h2 * c.kc / m0;
        c.a22 = 1.0 + c.eps22;
        c.invDet = 1.0 / (c.a11 * c.a22 - c.a12 * c.a21);
        c.invM0 = 1.0 / m0;
        c.invMt = 1.0 / mt;
        c.dtOverM0 = dtD / m0;
        c.dtOverMt = dtD / mt;

        for (int m = 1; m < kNumBeamModes; ++m) {
            const int u = m - 1;
            if (!modeActive_[m]) {
                up_.stiff[u] = up_.hk[u] = up_.eps[u] = up_.invM[u] = up_.dtOverM[u] = up_.R[u] = up_.cStrike[u] = 0.0;
                up_.invDenom[u] = 1.0;
                continue;
            }
            const double mass = massMode_[m];
            const double dampRate = dampMode_[m] + damperExtraDamp * 1.6f;
            up_.stiff[u] = stiffMode_[m];
            up_.hk[u] = h * up_.stiff[u];
            up_.eps[u] = h * dampRate + up_.hk[u] * (h / mass);
            up_.invDenom[u] = 1.0 / (1.0 + up_.eps[u]);
            up_.invM[u] = 1.0 / mass;
            up_.dtOverM[u] = dtD / mass;
            up_.R[u] = mass * dampRate;
            up_.cStrike[u] = cStrike_[m];
        }
        cachedDamperExtra_ = damperExtraDamp;
        coeffsDirty_ = false;
    }

    // Pickup geometry (PyPHS transducers.pickup defaults: Rb = Rp = 1 mm)
    static constexpr double kRb = 1.0e-3;
    static constexpr float kMagneticGain = 1.0f;
    static constexpr double kTineElectrostaticGain = 0.043;
    static constexpr float kReedMagneticGain = 0.43f;

    float fs_ = 48000.0f;
    float dt_ = 1.0f / 48000.0f;
    float damperStep_ = 0.0f;
    float dcBlockR_ = 0.997f;
    uint64_t thumpLenSamples_ = 0;
    uint64_t relTapLenSamples_ = 0;

    Resonator resonator_ = Resonator::Tine;
    Pickup pickup_ = Pickup::Magnetic;

    bool active_ = false;
    bool releasing_ = false;
    bool sustained_ = false;
    float damperAmount_ = 0.0f;
    float releaseEnv_ = 1.0f;
    int midiNote_ = -1;
    int modelType_ = 0;
    float velocity_ = 0.0f;
    float shapedVel_ = 0.0f;
    float f0_ = 440.0f;
    float f0Base_ = 440.0f;
    float keyScale_ = 0.5f;
    int channel_ = 0;
    float bendTarget_ = 0.0f, bendCur_ = 0.0f, bendApplied_ = 0.0f;
    float pressure_ = 0.0f, slide_ = 0.5f;
    float releaseVelocity_ = 0.5f;
    int key_ = -1;
    bool keyDown_ = false, sostenutoHeld_ = false;
    float damperDepth_ = 0.0f, reedDamperDepth_ = 0.0f, releaseDecayMulEff_ = 1.0f;
    float tuneHz_ = 440.0f, tuningCents_ = 0.0f;
    float varCents_ = 0.0f, varDecay_ = 1.0f, varBell_ = 0.0f, varHard_ = 0.0f, varGap_ = 0.0f;
    float varToneBar_ = 0.0f, varStrike_ = 0.0f;
    float gapEff_ = 0.5f, qEff_ = 0.45f;
    bool updateTuning_ = false;
    uint64_t ageSamples_ = 0;
    uint64_t releaseAgeSamples_ = 0;

    float lastStrikeParam_ = -1.0f;
    float lastDecayParam_ = -1.0f;
    float lastBellParam_ = -1.0f;
    float lastTbParam_ = -1.0f;
    float lastMaterialParam_ = -1.0f;

    // Hammer (H): mass + felt
    double mHammer_ = 1.0;
    double kHammer_ = 1.0e7;
    double alphaHammer_ = 2.1;
    double lambdaHammer_ = 100.0;
    double qh_ = 0.0;
    double ph_ = 0.0;
    double xiPrev_ = 0.0;
    bool hammerDetached_ = true;

    // Beam (B) modes
    std::array<bool, kNumBeamModes> modeActive_{};
    std::array<double, kNumBeamModes> qBeam_{};
    std::array<double, kNumBeamModes> pBeam_{};
    std::array<float, kNumBeamModes> omegaMode_{};
    std::array<float, kNumBeamModes> dampMode_{};
    std::array<float, kNumBeamModes> massMode_{};
    std::array<float, kNumBeamModes> stiffMode_{};
    std::array<float, kNumBeamModes> cStrike_{};
    std::array<float, kNumBeamModes> cPickup_{};

    // Tone bar (T)
    double qTb_ = 0.0;
    double pTb_ = 0.0;
    float omegaTb_ = 440.0f;
    float massTb_ = 300.0f;
    float stiffTb_ = 1000.0f;
    float dampTb_ = 1.0f;
    float kCoupleTb_ = 10.0f;

    bool coeffsDirty_ = true;
    float cachedDamperExtra_ = -1.0f;
    CoupledCoeffs coupled_{};
    UpperModeCoeffs up_{};
    std::array<float, kNumBeamModes> vTipWeight_{};

    // Reed
    wurli::ReedExciter reed_;
    bool reedDamperStarted_ = false;
    double xPrevMm_ = 0.0;
    uint32_t noiseSeed_ = 0x9E3779B9u;

    // Pickups
    // One pickup chain per running resonator (path 0 is the chosen resonator, path 1 the morph target)
    struct PickupPath {
        wurli::ElectrostaticPickup es;
        float coilQ = -1.0f;
        int coilModel = -1;
        float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, lp = 0.0f, bp = 0.0f;
    };
    std::array<PickupPath, 2> pp_{};
    bool dual_ = false, hasTine_ = true, hasReed_ = false, tineDone_ = false, reedDone_ = false;
    int tinePath_ = 0, reedPath_ = 1;
    float morphA_ = 1.0f, morphB_ = 0.0f, morphCached_ = -1.0f;
    float fmDepth_ = 0.0f, fmInc_ = 0.0f, fmPhase_ = 0.0f, fmS_ = 0.0f, fmC_ = 0.0f;
    int pickupCount_ = 1;
    float pickupSpreadMm_ = 0.0f;
    float geomKey_ = -1.0f;
    double emLh_ = 1e-3, emLv_ = 0.5e-3, emRp_ = 1e-3, emNorm_ = 1.0;
    float dcBlockX1_ = 0.0f;
    float dcBlockY1_ = 0.0f;

    VoiceTelemetry telemetry_{};
};

} // namespace phs
