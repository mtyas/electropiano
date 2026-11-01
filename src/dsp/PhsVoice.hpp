#pragma once

#include "PhsParameters.hpp"
#include <cmath>
#include <algorithm>
#include <array>

namespace phs {

static constexpr float kPi = 3.14159265358979323846f;
static constexpr float kTwoPi = 6.28318530717958647692f;

// Modal degrees of freedom in the Port-Hamiltonian resonator:
// Mode 0: Fundamental transverse bending mode (1.0000 * f0)
// Mode 1: 2nd Euler-Bernoulli cantilever mode (~6.2669 * f0 for Rhodes tine, ~5.50 * f0 for Wurli reed)
// Mode 2: 3rd Euler-Bernoulli cantilever mode (~17.547 * f0 crystalline strike ping)
// Mode 3: 2nd harmonic structural/bridge resonance (2.0002 * f0)
// Mode 4: 3rd harmonic structural resonance (3.0006 * f0)
static constexpr int kNumBeamModes = 5;

static constexpr float kCantileverBetaL[3] = {
    1.87510407f,
    4.69409113f,
    7.85475744f
};

static constexpr float kModalFreqRatios[kNumBeamModes] = {
    1.0000000f,  // Mode 0: Fundamental f0
    6.2668933f,  // Mode 1: Cantilever Tine Chime / Bell overtone
    17.547467f,  // Mode 2: Cantilever High Metallic Ping
    2.0002000f,  // Mode 3: 2nd Harmonic Warmth (mounting/bridge coupling)
    3.0006000f   // Mode 4: 3rd Harmonic Body
};

// Evaluate normalized spatial eigenfunction phi_m(x/L) at strike point xNorm = x/L in [0, 1]
inline float evalCantileverModeShape(int modeIdx, float xNorm) {
    xNorm = std::clamp(xNorm, 0.02f, 1.0f);
    if (modeIdx < 3) {
        const float bL = kCantileverBetaL[modeIdx];
        const float bx = bL * xNorm;
        const float sigma = (std::cosh(bL) + std::cos(bL)) / (std::sinh(bL) + std::sin(bL));
        const float val = (std::cosh(bx) - std::cos(bx)) - sigma * (std::sinh(bx) - std::sin(bx));
        return 0.5f * val; // Normalized so |phi_m(1.0)| == 1.0 at the free tip
    } else if (modeIdx == 3) {
        return std::sin(kPi * xNorm) * 0.55f;
    } else {
        return std::sin(1.5f * kPi * xNorm) * 0.38f;
    }
}

struct VoiceTelemetry {
    double totalHamiltonian = 0.0;   // Stored resonator Hamiltonian H_res(x) >= 0
    double dissipatedPower = 0.0;    // Instantaneous dissipated power P_diss >= 0
    double inputPower = 0.0;         // Instantaneous external power P_in from hammer impact
    float tipDisplacement = 0.0f;    // Tine/reed tip displacement w(x_pickup, t) in mm
    float tipVelocity = 0.0f;        // Normalized tip velocity
    float hammerCompression = 0.0f;  // Felt compression xi in mm
    float pickupOutput = 0.0f;       // Output audio signal
};

class PhsVoice {
public:
    void prepare(float sampleRate) {
        fs_ = std::max(8000.0f, sampleRate);
        dt_ = 1.0f / fs_;
        reset();
    }

    void reset() {
        active_ = false;
        releasing_ = false;
        sustained_ = false;
        damperAmount_ = 0.0f;
        releaseEnv_ = 1.0f;
        midiNote_ = -1;
        ageSamples_ = 0;
        releaseAgeSamples_ = 0;

        qh_ = 0.0f;
        ph_ = 0.0f;
        xiPrev_ = 0.0f;
        hammerDetached_ = true;

        qTb_ = 0.0f;
        pTb_ = 0.0f;

        for (int m = 0; m < kNumBeamModes; ++m) {
            qBeam_[m] = 0.0f;
            pBeam_[m] = 0.0f;
        }

        wBodyPrev_ = 0.0f;
        coilLPState_ = 0.0f;
        coilBPState_ = 0.0f;
        dcBlockX1_ = 0.0f;
        dcBlockY1_ = 0.0f;

        telemetry_ = {};
    }

    void noteOn(int midiNote, float velocity, const float* params) {
        midiNote_ = midiNote;
        velocity_ = std::clamp(velocity, 0.01f, 1.0f);
        active_ = true;
        releasing_ = false;
        sustained_ = false;
        damperAmount_ = 0.0f;
        releaseEnv_ = 1.0f;
        ageSamples_ = 0;

        const int modelType = std::clamp(static_cast<int>(std::round(params[kParamModelType] * 3.0f)), 0, 3);
        modelType_ = modelType;

        // Fundamental frequency in Hz
        f0_ = 440.0f * std::pow(2.0f, (static_cast<float>(midiNote) - 69.0f) / 12.0f);

        // Key-scaling factor across 88-key range (0.0 at A0=21, 1.0 at C8=108)
        keyScale_ = std::clamp((static_cast<float>(midiNote) - 21.0f) / 87.0f, 0.0f, 1.0f);

        // Dynamic touch & velocity curve shaping:
        // touch = 0.0 -> gamma = 2.70 (Heavy / Expressive Touch: soft playing is very gentle, only hard hits bark)
        // touch = 0.5 -> gamma = 1.45 (Standard vintage dynamic touch)
        // touch = 1.0 -> gamma = 0.45 (Light / Stage Touch: punchy, immediate bark and fullness even on gentle touches)
        const float touchParam = params[kParamVelocityCurve];
        const float gamma = 2.70f - 2.25f * touchParam;
        const float shapedVel = std::pow(velocity_, gamma);
        shapedVel_ = shapedVel;

        // --- 1. Update Modal Beam & Tone-Bar Parameters ---
        updateModalParameters(params);
        lastStrikeParam_   = params[kParamStrikePosition];
        lastDecayParam_    = params[kParamTineDecay];
        lastBellParam_     = params[kParamTineBell];
        lastTbParam_       = params[kParamToneBarCoupling];
        lastKeyNoiseParam_ = params[kParamKeyNoise];

        // --- 2. Port-Hamiltonian Hammer (H) Initialization ---
        const float hardness = params[kParamHammerHardness];
        // Dynamic felt stiffening with velocity:
        const float effHardness = std::clamp(hardness + 0.35f * (shapedVel - 0.5f), 0.02f, 0.98f);
        alphaHammer_ = (modelType == 2) ? (2.10f + 0.90f * effHardness) : (1.85f + 0.85f * effHardness);

        mHammer_ = 1.0f;

        // Contact duration tau_c (seconds):
        // Soft strike / soft hammer: ~2.40 ms (warm, round, dark attack)
        // Hard strike / hard hammer: ~0.42 ms (sharp, snappy, bright attack transient)
        const float velStiffening = std::pow(std::max(0.08f, shapedVel), 0.28f);
        const float tauContact = ((0.00240f - 0.00198f * effHardness) / velStiffening) * std::pow(0.55f, keyScale_);

        // Target peak tip displacement w_max (in mm):
        // Soft strikes stay in linear zone (0.04 mm), hard strikes enter heavy nonlinear saturation (0.85 mm)
        const float targetTipMm = 0.035f + (0.35f + 0.45f * (1.0f - 0.35f * touchParam)) * shapedVel;

        // Required impulse J_total so Mode 0 reaches targetTipMm:
        const float totalImpulse = (massMode_[0] * targetTipMm * omegaMode_[0]) / cStrike_[0];

        // A rebounding hammer of mass m_h delivers total impulse J_total ~ 1.85 * m_h * v_h0:
        const float vHammer0 = totalImpulse / (1.85f * mHammer_);
        const float xiNom = std::max(1e-4f, (tauContact * vHammer0) / 2.94f);

        // Exact Hertzian stiffness k_h so peak compression equals xiNom over duration tauContact:
        kHammer_ = ((alphaHammer_ + 1.0f) * 0.5f * mHammer_ * vHammer0 * vHammer0)
                 / std::pow(xiNom, alphaHammer_ + 1.0f);

        // Hunt-Crossley dissipative felt loss parameter lambda_h >= 0:
        // Cushion = 0: high restitution (elastic, snappy bounce, maximum energy transfer)
        // Cushion = 1: heavy felt compression (absorbs impact velocity, mellow spongey damping)
        const float feltLoss = params[kParamHammerHysteresis];
        lambdaHammer_ = (kHammer_ / vHammer0) * (0.02f + 0.75f * feltLoss);

        qh_ = 0.0f;
        ph_ = mHammer_ * vHammer0;
        xiPrev_ = 0.0f;
        hammerDetached_ = false;

        // Clean modal state and filter memories on strike
        for (int m = 0; m < kNumBeamModes; ++m) {
            qBeam_[m] = 0.0;
            pBeam_[m] = 0.0;
        }
        qTb_ = 0.0;
        pTb_ = 0.0;
        coilLPState_ = 0.0f;
        coilBPState_ = 0.0f;
        dcBlockX1_ = 0.0f;
        dcBlockY1_ = 0.0f;

        wBodyPrev_ = 0.0f;
        releaseAgeSamples_ = 0;
    }

    void noteOff(bool sustainPedalDown) {
        if (sustainPedalDown) {
            sustained_ = true;
        } else {
            releasing_ = true;
            sustained_ = false;
            releaseAgeSamples_ = 0;
        }
    }

    void setSustainPedal(bool down) {
        if (!down && sustained_) {
            sustained_ = false;
            releasing_ = true;
            releaseAgeSamples_ = 0;
        }
    }

    void updateModalParameters(const float* params) {
        const int modelType = std::clamp(static_cast<int>(std::round(params[kParamModelType] * 3.0f)), 0, 3);
        modelType_ = modelType;

        const float strikeNorm = params[kParamStrikePosition];
        // Spatial strike location xStrike in [0.06 .. 0.48]:
        // At 0.06: near clamp root -> rich high harmonics, biting edge
        // At 0.48: near nodal center -> deep round fundamental, soft warmth
        const float xStrike = 0.06f + strikeNorm * 0.42f;
        const float decayParam = params[kParamTineDecay];
        const float bellParam = params[kParamTineBell];
        const float toneBarParam = params[kParamToneBarCoupling];
        const float hardness = params[kParamHammerHardness];

        // Primary fundamental decay time T60 (seconds):
        // decayParam in [0..1]:
        // at decayParam = 0.0 -> 0.35s (super-tight funk/clavinet staccato pluck!)
        // at decayParam = 0.5 -> 6.8s (vintage classic singing Rhodes)
        // at decayParam = 1.0 -> 28.0s (infinite lush ballad sustain)
        const float decayCurve = decayParam * decayParam;
        float baseT60 = (0.35f + 27.65f * decayCurve) * std::pow(0.50f, keyScale_);
        if (modelType == 2) {
            baseT60 *= 0.68f; // Wurlitzer 200A steel reed
        }

        for (int m = 0; m < kNumBeamModes; ++m) {
            float ratio = kModalFreqRatios[m];
            if (modelType == 2 && m == 1) {
                ratio = 5.45f - 0.30f * toneBarParam;
            } else if (modelType == 1 && m == 1) {
                ratio = 6.268f;
            }

            const float fm = f0_ * ratio;
            if (fm >= 0.44f * fs_) {
                modeActive_[m] = false;
                cStrike_[m] = 0.0f;
                cPickup_[m] = 0.0f;
                omegaMode_[m] = 100.0f;
                dampMode_[m] = 1000.0f;
                massMode_[m] = 1.0f;
                stiffMode_[m] = 10000.0f;
                continue;
            }
            modeActive_[m] = true;
            // Exact bilinear/midpoint frequency pre-warping so every mode oscillates at exact pitch fm!
            omegaMode_[m] = 2.0f * fs_ * std::tan(kPi * fm * dt_);

            const float mEff = 25.0f;
            massMode_[m] = mEff;
            stiffMode_[m] = mEff * omegaMode_[m] * omegaMode_[m];

            float modeT60 = baseT60;
            if (m == 1) {
                // Iconic Rhodes/Wurli "Tine Chime / Bell" overtone (~6.27 * f0):
                // Decay time scales directly with chime knob:
                // At chime = 0: decays in 25ms (completely dark, bell-free)
                // At chime = 1: rings out for 0.65s (singing crystalline luster)
                modeT60 = (0.025f + 0.60f * bellParam * bellParam) * (0.35f + 0.65f * keyScale_);
            } else if (m == 2) {
                // High crystalline strike ping (~17.55 * f0): decays rapidly in 15 - 50 ms
                modeT60 = (0.010f + 0.065f * bellParam) * (0.4f + 0.6f * (1.0f - keyScale_));
            } else if (m == 3) {
                // Tone bar 2nd harmonic structural warmth
                modeT60 = baseT60 * (0.45f + 0.45f * toneBarParam);
            } else {
                // Tone bar 3rd harmonic body
                modeT60 = baseT60 * (0.30f + 0.35f * toneBarParam);
            }
            modeT60 = std::max(0.012f, modeT60);
            dampMode_[m] = 13.81551f / modeT60; // Exact 2 * ln(1000) / T60

            // Spatial coupling at hammer strike point c_m(x_strike)
            const float rawShape = evalCantileverModeShape(m, xStrike);
            if (m == 0) {
                cStrike_[m] = 0.50f;
            } else if (m == 1) {
                // Chime excitation: directly gated and scaled by bellParam
                const float chimeExcite = bellParam * (0.15f + 0.45f * hardness) * (0.30f + 0.70f * shapedVel_);
                cStrike_[m] = -chimeExcite * (0.45f + 0.55f * std::fabs(rawShape));
            } else if (m == 2) {
                // Strike ping (decays in <40ms)
                const float pingExcite = bellParam * (0.08f + 0.25f * hardness) * (0.25f + 0.75f * shapedVel_);
                cStrike_[m] = pingExcite * std::fabs(rawShape);
            } else if (m == 3) {
                // Tone bar 2nd harmonic octave warmth
                const float tbExcite = (0.02f + 0.38f * toneBarParam) * (0.35f + 0.65f * shapedVel_);
                cStrike_[m] = rawShape * tbExcite;
            } else {
                // Tone bar 3rd harmonic body
                const float tbExcite = (0.02f + 0.28f * toneBarParam) * (0.30f + 0.70f * shapedVel_);
                cStrike_[m] = rawShape * tbExcite;
            }

            // Spatial coupling at pickup tip c_m(x_pickup)
            if (m == 0) {
                cPickup_[m] = 1.0f;
            } else if (m == 1) {
                // Tine bell mode (~6.27 * f0):
                // At bellParam = 0 -> 0.0 (completely pure fundamental!)
                // At bellParam = 1 -> 0.42 (radiant, cutting, iconic Rhodes chime!)
                const float bellAmt = bellParam * ((modelType == 2) ? (0.12f + 0.18f * bellParam)
                                                  : (modelType == 1) ? (0.18f + 0.24f * bellParam)
                                                                     : (0.15f + 0.22f * bellParam));
                cPickup_[m] = bellAmt * (0.45f + 0.55f * keyScale_);
            } else if (m == 2) {
                const float pingAmt = bellParam * ((modelType == 2) ? 0.08f : 0.12f);
                cPickup_[m] = pingAmt * (0.40f + 0.60f * keyScale_);
            } else if (m == 3) {
                // Tone bar 2nd harmonic octave warmth
                cPickup_[m] = (0.02f + 0.35f * toneBarParam) * (0.3f + 0.7f * shapedVel_);
            } else {
                const float h3 = (modelType == 2) ? (0.04f + 0.30f * toneBarParam) : (0.02f + 0.25f * toneBarParam);
                cPickup_[m] = h3 * (0.25f + 0.75f * shapedVel_);
            }
        }

        // --- Tone-Bar Coupled Resonator (T) Parameters ---
        // Acoustic detuning creates iconic 0.4 - 1.8 Hz acoustic beating shimmer:
        const float detuneHz = 0.35f + 1.20f * toneBarParam;
        omegaTb_ = 2.0f * fs_ * std::tan(kPi * (f0_ + detuneHz) * dt_);
        massTb_ = massMode_[0] * 8.0f;
        stiffTb_ = massTb_ * omegaTb_ * omegaTb_;
        dampTb_ = dampMode_[0] * (0.70f - 0.30f * toneBarParam);

        // Strong physical coupling stiffness between tine clamp and tone bar:
        // Scaled directly by toneBarParam from 0.005 (uncoupled dry pin) to 0.42 * stiffMode0 (deep resonance)
        kCoupleTb_ = stiffMode_[0] * (0.005f + 0.42f * toneBarParam);
    }

    float processSample(const float* params) {
        if (!active_) {
            return 0.0f;
        }
        ++ageSamples_;

        // Refresh modal parameters only when user moves a knob
        if ((ageSamples_ & 63) == 0) {
            const float strikeChange = std::fabs(params[kParamStrikePosition] - lastStrikeParam_);
            const float decayChange  = std::fabs(params[kParamTineDecay] - lastDecayParam_);
            const float bellChange   = std::fabs(params[kParamTineBell] - lastBellParam_);
            const float tbChange     = std::fabs(params[kParamToneBarCoupling] - lastTbParam_);
            const int newModel       = std::clamp(static_cast<int>(std::round(params[kParamModelType] * 3.0f)), 0, 3);
            if (newModel != modelType_ || strikeChange > 1e-4f || decayChange > 1e-4f || bellChange > 1e-4f || tbChange > 1e-4f) {
                updateModalParameters(params);
                lastStrikeParam_ = params[kParamStrikePosition];
                lastDecayParam_  = params[kParamTineDecay];
                lastBellParam_   = params[kParamTineBell];
                lastTbParam_     = params[kParamToneBarCoupling];
            }
        }

        // Damper felt engagement: smooth continuous landing over 12 ms (eliminates note-off clicks & state acceleration spikes!)
        if (releasing_) {
            damperAmount_ = std::min(1.0f, damperAmount_ + (dt_ / 0.012f));
            releaseEnv_ *= std::exp(-dt_ / 0.055f);
        } else {
            damperAmount_ = 0.0f;
            releaseEnv_ = 1.0f;
        }
        const float damperExtraDamp = damperAmount_ * ((modelType_ == 2) ? 75.0f : 55.0f);

        // =========================================================================
        // STEP 1: Port-Hamiltonian Hammer-Beam Impact (H <-> B)
        //         Solved via Structure-Preserving Implicit Midpoint Rule
        // =========================================================================
        float contactForce = 0.0f;
        float feltDissPower = 0.0f;

        if (!hammerDetached_) {
            const float wStrike = cStrike_[0] * qBeam_[0];
            const float vStrike = cStrike_[0] * (pBeam_[0] / massMode_[0]);
            const float vh = ph_ / mHammer_;
            const float xiRelVel = vh - vStrike;

            // Advance hammer position by half-step (Störmer-Verlet / Midpoint position)
            const float qhMid = qh_ + 0.5f * dt_ * vh;
            const float wStrikeMid = wStrike + 0.5f * dt_ * vStrike;
            const float xiMid = qhMid - wStrikeMid;

            if (xiMid > 0.0f) {
                const float xiPow = std::pow(xiMid, alphaHammer_);
                const float fStorage = kHammer_ * xiPow;
                const float fDiss = lambdaHammer_ * xiPow * xiRelVel;
                contactForce = std::max(0.0f, fStorage + fDiss);
                feltDissPower = std::max(0.0f, fDiss * xiRelVel);

                ph_ -= dt_ * contactForce;
                qh_ = qhMid + 0.5f * dt_ * (ph_ / mHammer_);
                xiPrev_ = xiMid;
            } else {
                qh_ += dt_ * vh;
                xiPrev_ = 0.0f;
                if (ph_ <= 0.0f && ageSamples_ > 4) {
                    hammerDetached_ = true;
                }
            }
        }

        // =========================================================================
        // STEP 2: Exact Implicit Midpoint Integration of Coupled Mode 0 + Tone-Bar
        //         and Higher Cantilever Modes m = 1..4 (64-bit Double Precision)
        // =========================================================================
        double beamEnergy = 0.0;
        double beamDissPower = 0.0;
        double inputPower = 0.0;
        const double dtD = static_cast<double>(dt_);
        const double h = 0.5 * dtD;
        const double h2 = h * h;

        // 2A. Coupled 2x2 Implicit Midpoint Solve for Fundamental Mode 0 and Tone-Bar (tb)
        {
            const double m0 = massMode_[0];
            const double kc = kCoupleTb_;
            const double k0 = std::max(0.5f * stiffMode_[0], stiffMode_[0] - kCoupleTb_);
            const double d0 = dampMode_[0] + damperExtraDamp;
            const double R0 = m0 * d0;

            const double mt = massTb_;
            const double kt = stiffTb_;
            const double dtb = dampTb_ + damperExtraDamp * 0.9f;
            const double Rt = mt * dtb;

            const double fExt0 = static_cast<double>(cStrike_[0]) * contactForce;

            const double eps11 = h * d0 + h2 * (k0 + kc) / m0;
            const double a11 = 1.0 + eps11;
            const double a12 = -h2 * kc / mt;
            const double a21 = -h2 * kc / m0;
            const double eps22 = h * dtb + h2 * (kt + kc) / mt;
            const double a22 = 1.0 + eps22;

            const double q0Old = qBeam_[0];
            const double p0Old = pBeam_[0];
            const double qtOld = qTb_;
            const double ptOld = pTb_;

            const double rhs1 = -h * ((k0 + kc) * q0Old - kc * qtOld) - eps11 * p0Old - a12 * ptOld + h * fExt0;
            const double rhs2 = -h * (-kc * q0Old + (kt + kc) * qtOld) - a21 * p0Old - eps22 * ptOld;

            const double det = a11 * a22 - a12 * a21;
            const double invDet = 1.0 / det;
            const double dp0Mid = (a22 * rhs1 - a12 * rhs2) * invDet;
            const double dptMid = (-a21 * rhs1 + a11 * rhs2) * invDet;

            const double p0Mid = p0Old + dp0Mid;
            const double ptMid = ptOld + dptMid;
            const double p0New = p0Old + 2.0 * dp0Mid;
            const double q0New = q0Old + (dtD / m0) * p0Mid;
            const double ptNew = ptOld + 2.0 * dptMid;
            const double qtNew = qtOld + (dtD / mt) * ptMid;

            qBeam_[0] = q0New;
            pBeam_[0] = p0New;
            qTb_ = qtNew;
            pTb_ = ptNew;

            const double qDiff = q0New - qtNew;
            beamEnergy += 0.5 * k0 * q0New * q0New + 0.5 * (p0New * p0New) / m0
                        + 0.5 * kt * qtNew * qtNew + 0.5 * (ptNew * ptNew) / mt
                        + 0.5 * kc * qDiff * qDiff;

            const double v0Mid = p0Mid / m0;
            const double vtMid = ptMid / mt;
            beamDissPower += R0 * v0Mid * v0Mid + Rt * vtMid * vtMid;
            inputPower += fExt0 * v0Mid;
        }

        // 2B. Implicit Midpoint Solve for Higher Beam Modes m = 1..4
        for (int m = 1; m < kNumBeamModes; ++m) {
            if (!modeActive_[m]) continue;

            const double mass = massMode_[m];
            const double stiff = stiffMode_[m];
            const double dampRate = dampMode_[m] + damperExtraDamp * 1.6f;
            const double Rm = mass * dampRate;
            const double fExt = static_cast<double>(cStrike_[m]) * contactForce;

            const double hk = h * stiff;
            const double hOverM = h / mass;
            const double hROverM = h * dampRate;
            const double eps = hROverM + hk * hOverM;

            const double denom = 1.0 + eps;
            const double qOld = qBeam_[m];
            const double pOld = pBeam_[m];

            const double dpMid = (-hk * qOld - eps * pOld + h * fExt) / denom;
            const double pMid = pOld + dpMid;
            const double pNew = pOld + 2.0 * dpMid;
            const double qNew = qOld + (dtD / mass) * pMid;

            qBeam_[m] = qNew;
            pBeam_[m] = pNew;

            beamEnergy += 0.5 * stiff * qNew * qNew + 0.5 * (pNew * pNew) / mass;
            const double vMid = pMid / mass;
            beamDissPower += Rm * vMid * vMid;
            inputPower += fExt * vMid;
        }

        // =========================================================================
        // STEP 3: Integrated Tine & Tone-Bar Tip Excursion and Modal Velocity
        // =========================================================================
        const float tbWeight = 0.02f + 0.65f * params[kParamToneBarCoupling];
        const float wBody = static_cast<float>(qBeam_[0] + tbWeight * qTb_)
                          + (modeActive_[3] ? cPickup_[3] * static_cast<float>(qBeam_[3]) : 0.0f)
                          + (modeActive_[4] ? cPickup_[4] * static_cast<float>(qBeam_[4]) : 0.0f);

        // Normalize each mode by its own physical modal frequency omega_m so higher modes
        // don't blow up by 17.5x in velocity!
        float vTipNorm = static_cast<float>((pBeam_[0] / massMode_[0]) + tbWeight * (pTb_ / massTb_)) / omegaMode_[0];
        if (modeActive_[1]) {
            vTipNorm += cPickup_[1] * static_cast<float>(pBeam_[1] / massMode_[1]) / omegaMode_[1];
        }
        if (modeActive_[2]) {
            vTipNorm += cPickup_[2] * static_cast<float>(pBeam_[2] / massMode_[2]) / omegaMode_[2];
        }
        if (modeActive_[3]) {
            vTipNorm += cPickup_[3] * static_cast<float>(pBeam_[3] / massMode_[3]) / omegaMode_[3];
        }
        if (modeActive_[4]) {
            vTipNorm += cPickup_[4] * static_cast<float>(pBeam_[4] / massMode_[4]) / omegaMode_[4];
        }

        // =========================================================================
        // STEP 4: Nonlinear Transducer Pickup (P) via Discrete Gradient
        // =========================================================================
        // Air gap d_gap in [0.20 .. 1.25] mm: controls nonlinear bark threshold
        // Voicing symParam in [0..1]: controls asymmetric 2nd-harmonic bloom vs 3rd-harmonic growl
        const float distParam = params[kParamPickupDistance];
        const float dGap = 1.25f - 1.05f * distParam; // [0.20 .. 1.25] mm
        const float symParam = params[kParamPickupSymmetry];
        const float asymFactor = 0.05f + 1.45f * symParam;

        float dPhiDw = 1.0f;
        if (modelType_ == 2) {
            dPhiDw = evalWurlitzerCapacitanceSensitivity(wBody, asymFactor, dGap);
        } else {
            const bool dyno = (modelType_ == 1 || modelType_ == 3);
            dPhiDw = evalRhodesMagneticFluxSensitivity(wBody, asymFactor, dGap, dyno);
        }

        // Transduced signal: dPhi/dt = (dPhi/dw) * v_tip with proximity gain
        const float proximityGain = 0.45f + 0.85f * distParam;
        const float transducerSignal = dPhiDw * vTipNorm * proximityGain;

        // =========================================================================
        // STEP 5: Pickup RLC Coil / Capacitive Load Filter (Passive 2-Pole SVF)
        // =========================================================================
        // Fully processes the transducer signal through the coil's inductive resonance
        // and rolls off high-frequency hiss at 12 dB/octave above resonance!
        const float pickupQParam = params[kParamPickupResonance];
        const float resFreq = (modelType_ == 1) ? (1900.0f + 3300.0f * pickupQParam)
                            : (modelType_ == 2) ? (1700.0f + 2900.0f * pickupQParam)
                                                : (1800.0f + 3100.0f * pickupQParam);
        const float g = std::tan(kPi * std::min(resFreq, 0.44f * fs_) * dt_);
        const float kDamp = 1.60f - 1.22f * pickupQParam; // 1.60 (warm vintage roll) to 0.38 (singing Dyno bell resonance)
        const float a1 = 1.0f / (1.0f + g * (g + kDamp));
        const float a2 = g * a1;
        const float a3 = g * a2;

        const float v3 = transducerSignal - coilLPState_;
        const float v1 = a1 * coilBPState_ + a2 * v3;
        const float v2 = coilLPState_ + a2 * coilBPState_ + a3 * v3;
        coilBPState_ = 2.0f * v1 - coilBPState_;
        coilLPState_ = 2.0f * v2 - coilLPState_;

        // Low-pass + presence bandpass combination
        float pickupOut = v2 + (0.05f + 0.95f * pickupQParam) * v1;

        // =========================================================================
        // STEP 6: Mechanical Action Sounds (Thump, Felt Escapement & Damper Tap)
        // =========================================================================
        // Authentic dual-component acoustic action sound:
        // - Deep 110 Hz wood keybed thud (10 ms decay)
        // - Crisp 920 Hz felt escapement click (3.5 ms decay)
        // Gated and scaled by thump parameter and strike velocity:
        const float thumpParam = params[kParamKeyNoise];
        if (thumpParam > 0.001f && ageSamples_ < static_cast<uint64_t>(0.028f * fs_)) {
            const float tSec = static_cast<float>(ageSamples_) * dt_;
            const float thudEnv = std::exp(-tSec / 0.0095f) * std::sin(kTwoPi * 110.0f * tSec);
            const float clickEnv = std::exp(-tSec / 0.0032f) * std::sin(kTwoPi * 920.0f * tSec);
            const float thumpAmp = thumpParam * (0.06f + 0.20f * shapedVel_);
            pickupOut += thumpAmp * (0.75f * thudEnv + 0.35f * clickEnv);
        }

        // Physical felt damper landing tap on key release:
        if (releasing_ && thumpParam > 0.001f && releaseAgeSamples_ < static_cast<uint64_t>(0.016f * fs_)) {
            const float tRel = static_cast<float>(releaseAgeSamples_) * dt_;
            const float relTap = std::exp(-tRel / 0.0050f) * std::sin(kTwoPi * 380.0f * tRel);
            pickupOut += thumpParam * 0.035f * relTap;
            releaseAgeSamples_++;
        }

        // Apply smooth release envelope and DC blocker
        pickupOut *= releaseEnv_;

        const float Rdc = 1.0f - (kTwoPi * 20.0f / fs_);
        const float yDc = pickupOut - dcBlockX1_ + Rdc * dcBlockY1_;
        dcBlockX1_ = pickupOut;
        dcBlockY1_ = yDc;

        telemetry_.totalHamiltonian = beamEnergy;
        telemetry_.dissipatedPower = beamDissPower + feltDissPower;
        telemetry_.inputPower = inputPower;
        telemetry_.tipDisplacement = wBody;
        telemetry_.tipVelocity = vTipNorm;
        telemetry_.hammerCompression = xiPrev_;
        telemetry_.pickupOutput = yDc;

        if (releasing_ && releaseEnv_ < 1e-4f) {
            active_ = false;
        } else if (hammerDetached_ && ageSamples_ > 256 && beamEnergy < 1e-9f && std::fabs(yDc) < 1e-6f) {
            active_ = false;
        }

        return yDc;
    }

    bool isActive() const { return active_; }
    bool isReleasing() const { return releasing_; }
    int getMidiNote() const { return midiNote_; }
    uint64_t getAge() const { return ageSamples_; }
    const VoiceTelemetry& getTelemetry() const { return telemetry_; }

    // Monotonic + asymmetric Port-Hamiltonian magnetic flux sensitivity dPhi/dw:
    // C-infinity analytic evaluation eliminating finite-difference noise at zero crossings!
    // - Linear baseline (1.0) preserves clean, crystal-clear fundamental tone f0
    // - Odd asymmetric term produces warm 2nd-harmonic octave bloom
    // - Even quadratic term produces punchy 3rd-harmonic Rhodes pickup bark
    static inline float evalRhodesMagneticFluxSensitivity(float w, float asym, float dGap, bool dynoMode) {
        const float gap = std::max(0.18f, dGap);
        const float u = w / gap;
        const float u2 = u * u;
        const float a1 = asym * (dynoMode ? 1.65f : 1.25f);
        const float a2 = (dynoMode ? 1.40f : 0.95f) * (0.35f / gap);
        return 1.0f + a1 * (u / std::sqrt(1.0f + 0.5f * u2)) + a2 * (u2 / (1.0f + 0.40f * u2));
    }

    // Wurlitzer 200A polarized electrostatic reed pickup sensitivity dC/dw:
    // Smooth asymmetric capacitive sensitivity for iconic reedy bite without white noise
    static inline float evalWurlitzerCapacitanceSensitivity(float w, float asym, float g0) {
        const float gap = std::max(0.18f, g0);
        const float u = w / gap;
        const float u2 = u * u;
        const float a1 = 0.25f + 1.15f * asym;
        const float a2 = 0.95f * (0.35f / gap);
        return 1.0f + a1 * (u / std::sqrt(1.0f + 0.65f * u2)) + a2 * (u2 / (1.0f + 0.50f * u2));
    }

private:
    float fs_ = 48000.0f;
    float dt_ = 1.0f / 48000.0f;

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
    float keyScale_ = 0.5f;
    uint64_t ageSamples_ = 0;
    uint64_t releaseAgeSamples_ = 0;

    float lastStrikeParam_ = -1.0f;
    float lastDecayParam_ = -1.0f;
    float lastBellParam_ = -1.0f;
    float lastTbParam_ = -1.0f;
    float lastKeyNoiseParam_ = 0.25f;

    // Port-Hamiltonian Hammer (H) states & parameters
    float mHammer_ = 1.0f;
    float kHammer_ = 1.0e7f;
    float alphaHammer_ = 2.1f;
    float lambdaHammer_ = 100.0f;
    float qh_ = 0.0f;
    float ph_ = 0.0f;
    float xiPrev_ = 0.0f;
    bool hammerDetached_ = true;

    // Port-Hamiltonian Cantilever Beam (B) modal states & parameters
    std::array<bool, kNumBeamModes> modeActive_{};
    std::array<double, kNumBeamModes> qBeam_{};
    std::array<double, kNumBeamModes> pBeam_{};
    std::array<float, kNumBeamModes> omegaMode_{};
    std::array<float, kNumBeamModes> dampMode_{};
    std::array<float, kNumBeamModes> massMode_{};
    std::array<float, kNumBeamModes> stiffMode_{};
    std::array<float, kNumBeamModes> cStrike_{};
    std::array<float, kNumBeamModes> cPickup_{};

    // Coupled Tone-Bar (T) state & parameters
    double qTb_ = 0.0;
    double pTb_ = 0.0;
    float omegaTb_ = 440.0f;
    float massTb_ = 300.0f;
    float stiffTb_ = 1000.0f;
    float dampTb_ = 1.0f;
    float kCoupleTb_ = 10.0f;

    // Pickup (P) state
    float wBodyPrev_ = 0.0f;
    float coilLPState_ = 0.0f;
    float coilBPState_ = 0.0f;
    float dcBlockX1_ = 0.0f;
    float dcBlockY1_ = 0.0f;

    VoiceTelemetry telemetry_{};
};

} // namespace phs
