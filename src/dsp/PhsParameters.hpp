#pragma once

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <array>

namespace phs {

// Parameter IDs are host automation IDs: new parameters are only ever appended.
enum ParamID : uint32_t {
    kParamModelType = 0,       // Instrument flavour: 0 Rhodes Mk I, 1 Rhodes Dyno, 2 Wurlitzer 200A, 3 Hybrid
    kParamHammerHardness,      // Felt stiffness k_h and nonlinearity exponent alpha (reed: hammer dwell)
    kParamHammerHysteresis,    // Felt dissipation (PyPHS felt damper A)
    kParamStrikePosition,      // Relative strike point x_strike / L along the cantilever beam
    kParamTineDecay,           // Modal decay time
    kParamTineBell,            // Upper cantilever modes (tine chime / reed overtones)
    kParamToneBarCoupling,     // Tine tone-bar coupling / reed tip (solder) mass
    kParamKeyNoise,            // Mechanical thump / hammer attack noise

    kParamPickupDistance,      // Magnetic: horizontal gap Lh. Electrostatic: reed swing into the comb
    kParamPickupSymmetry,      // Magnetic: vertical offset Lv. Electrostatic: rest offset from the finger band
    kParamPickupResonance,     // Magnetic: coil RLC resonance. Electrostatic: plate-node capacitance (HPF corner)
    kParamPreampDrive,         // Suitcase: preamp saturation. 200A: volume pot into the power amp
    kParamBassBoost,           // Baxandall bass shelf (+-16 dB)
    kParamTrebleBoost,         // Baxandall treble shelf (+-16 dB)

    kParamTremoloDepth,        // Suitcase stereo auto-pan / 200A vibrato (LDR in the preamp loop)
    kParamTremoloRate,         // LFO speed (200A: time-scales the Twin-T oscillator)
    kParamPhaserMix,           // 4-stage optical all-pass phaser mix
    kParamPhaserRate,          // Phaser LFO rate
    kParamChorusMix,           // BBD stereo chorus mix
    kParamReverbMix,           // Reverb wet mix
    kParamReverbDecay,         // Reverb decay
    kParamVelocityCurve,       // Touch / velocity response
    kParamMasterVolume,        // Output gain

    // --- v1.3: swappable stages and new effects ---
    kParamResonator,           // 0 Rhodes tine (PyPHS beam + hammer), 1 Wurlitzer reed (OpenWurli)
    kParamPickupType,          // 0 Electromagnetic (Falaize-Helie), 1 Electrostatic (OpenWurli overlap law)
    kParamAmpType,             // 0 Suitcase/stage preamp, 1 Wurlitzer 200A circuit (OpenWurli)
    kParamCabinet,             // Speaker cabinet character (0 = flat DI)
    kParamWahAmount,           // Envelope filter (auto-wah) amount
    kParamWahSens,             // Envelope filter sensitivity
    kParamDelayMix,            // Tape/BBD echo mix
    kParamDelayTime,           // Echo time
    kParamDelayFeedback,       // Echo feedback

    kNumParams
};

// Voice units (the suitcase chain's level) per pickup volt. Reed + electrostatic pickup
// voices are scaled by this, and the 200A amp divides it back out, so that chain runs
// on exactly the voltages OpenWurli's circuit expects.
static constexpr double kVoiceUnitsPerVolt = 52.5;
// Gain applied to the summed voices before the effects chain
static constexpr float kVoiceMixGain = 0.32f;

// Parameters that existed in state version 2 (before the v1.3 additions)
static constexpr uint32_t kNumParamsV2 = 23;

struct ParamInfo {
    uint32_t id;
    const char* name;
    const char* shortName;
    const char* unit;
    const char* category;
    float defaultValue;
    int steps;               // 0 = continuous, otherwise number of steps (choices - 1)
};

static constexpr std::array<ParamInfo, kNumParams> kParamMetadata = {{
    {kParamModelType,        "Model",          "Model",    "",    "Instrument", 0.0f,  3},
    {kParamHammerHardness,   "Hardness",       "Hardness", "%",   "Resonator",  0.48f, 0},
    {kParamHammerHysteresis, "Cushion",        "Cushion",  "%",   "Resonator",  0.35f, 0},
    {kParamStrikePosition,   "Strike",         "Strike",   "x/L", "Resonator",  0.30f, 0},
    {kParamTineDecay,        "Decay",          "Decay",    "s",   "Resonator",  0.65f, 0},
    {kParamTineBell,         "Chime",          "Chime",    "%",   "Resonator",  0.55f, 0},
    {kParamToneBarCoupling,  "Tone Bar",       "ToneBar",  "%",   "Resonator",  0.55f, 0},
    {kParamKeyNoise,         "Thump",          "Thump",    "%",   "Resonator",  0.30f, 0},
    {kParamPickupDistance,   "Air Gap",        "AirGap",   "",    "Pickup",     0.50f, 0},
    {kParamPickupSymmetry,   "Voicing",        "Voicing",  "",    "Pickup",     0.40f, 0},
    {kParamPickupResonance,  "Coil Q",         "CoilQ",    "%",   "Pickup",     0.45f, 0},
    {kParamPreampDrive,      "Drive",          "Drive",    "%",   "Amp",        0.40f, 0},
    {kParamBassBoost,        "Bass",           "Bass",     "dB",  "Tone",       0.50f, 0},
    {kParamTrebleBoost,      "Treble",         "Treble",   "dB",  "Tone",       0.50f, 0},
    {kParamTremoloDepth,     "Trem Depth",     "Depth",    "%",   "Amp",        0.35f, 0},
    {kParamTremoloRate,      "Trem Speed",     "Speed",    "Hz",  "Amp",        0.48f, 0},
    {kParamPhaserMix,        "Phaser",         "Phaser",   "%",   "Effects",    0.00f, 0},
    {kParamPhaserRate,       "Phaser Rate",    "Rate",     "Hz",  "Effects",    0.28f, 0},
    {kParamChorusMix,        "Chorus",         "Chorus",   "%",   "Effects",    0.00f, 0},
    {kParamReverbMix,        "Reverb Mix",     "Mix",      "%",   "Effects",    0.20f, 0},
    {kParamReverbDecay,      "Reverb Decay",   "Decay",    "%",   "Effects",    0.45f, 0},
    {kParamVelocityCurve,    "Touch",          "Touch",    "%",   "Resonator",  0.50f, 0},
    {kParamMasterVolume,     "Volume",         "Volume",   "dB",  "Output",     0.78f, 0},
    {kParamResonator,        "Resonator",      "Reson",    "",    "Instrument", 0.0f,  1},
    {kParamPickupType,       "Pickup",         "Pickup",   "",    "Instrument", 0.0f,  1},
    {kParamAmpType,          "Amp",            "Amp",      "",    "Instrument", 0.0f,  1},
    {kParamCabinet,          "Cabinet",        "Cab",      "%",   "Amp",        0.00f, 0},
    {kParamWahAmount,        "Env Filter",     "Wah",      "%",   "Effects",    0.00f, 0},
    {kParamWahSens,          "Env Sens",       "Sens",     "%",   "Effects",    0.50f, 0},
    {kParamDelayMix,         "Echo Mix",       "Echo",     "%",   "Effects",    0.00f, 0},
    {kParamDelayTime,        "Echo Time",      "Time",     "ms",  "Effects",    0.55f, 0},
    {kParamDelayFeedback,    "Echo Feedback",  "Repeats",  "%",   "Effects",    0.35f, 0},
}};

// -----------------------------------------------------------------------------
// Value mappings shared by the DSP and every display, so they cannot disagree
// -----------------------------------------------------------------------------
inline int stepIndex(float norm, int steps) { return std::clamp(static_cast<int>(std::lround(norm * steps)), 0, steps); }
inline float eqDb(float norm) { return (norm - 0.5f) * 32.0f; }
inline float tremoloRateHz(float norm) { return 0.5f + norm * 10.5f; }
inline float phaserRateHz(float norm) { return 0.08f + norm * 3.5f; }
inline float delayTimeSec(float norm) { return 0.04f * std::pow(25.0f, norm); }        // 40 ms .. 1 s
inline float tineBaseT60(float norm) { return 0.35f + 27.65f * norm * norm; }        // at A0; shortens up the keys
inline float strikePositionXL(float norm) { return 0.06f + norm * 0.42f; }
inline float magneticGapMm(float norm) { return 3.0f - norm * 2.1f; }                // knob up = closer
inline float magneticOffsetMm(float norm) { return 0.05f + norm * 1.15f; }
inline float electrostaticRestMm(float norm) { return 0.085f * std::pow(2.0f, (norm - 0.40f) * 3.0f); }
inline float electrostaticSwingScale(float norm) { return std::pow(2.0f, (norm - 0.50f) * 2.0f); }
inline double wurliVolumePot(float driveNorm) { return std::clamp(0.4 + driveNorm, 0.0, 1.0); } // 0.40 -> upstream 0.8

inline void formatParamDisplay(uint32_t id, float n, char* out, size_t size) {
    switch (id) {
        case kParamModelType: {
            const char* names[4] = {"Rhodes Mk I", "Rhodes Dyno", "Wurlitzer 200A", "Hybrid"};
            std::snprintf(out, size, "%s", names[stepIndex(n, 3)]);
            return;
        }
        case kParamResonator: std::snprintf(out, size, "%s", stepIndex(n, 1) ? "Reed" : "Tine"); return;
        case kParamPickupType: std::snprintf(out, size, "%s", stepIndex(n, 1) ? "Electrostatic" : "Magnetic"); return;
        case kParamAmpType: std::snprintf(out, size, "%s", stepIndex(n, 1) ? "200A" : "Suitcase"); return;
        case kParamStrikePosition: std::snprintf(out, size, "%.2f L", strikePositionXL(n)); return;
        case kParamTineDecay: std::snprintf(out, size, "%.1f s", tineBaseT60(n) * 0.732f); return; // T60 at middle C
        case kParamPickupDistance: std::snprintf(out, size, "%.2f mm", magneticGapMm(n)); return;
        case kParamPickupSymmetry: std::snprintf(out, size, "%.2f mm", magneticOffsetMm(n)); return;
        case kParamBassBoost:
        case kParamTrebleBoost: std::snprintf(out, size, "%+.1f dB", eqDb(n)); return;
        case kParamTremoloRate: std::snprintf(out, size, "%.1f Hz", tremoloRateHz(n)); return;
        case kParamPhaserRate: std::snprintf(out, size, "%.2f Hz", phaserRateHz(n)); return;
        case kParamDelayTime: std::snprintf(out, size, "%.0f ms", delayTimeSec(n) * 1000.0f); return;
        case kParamMasterVolume:
            if (n <= 0.001f) std::snprintf(out, size, "-inf dB");
            else std::snprintf(out, size, "%+.1f dB", 20.0f * std::log10(n / 0.78f));
            return;
        default: std::snprintf(out, size, "%.0f%%", n * 100.0f); return;
    }
}

} // namespace phs
