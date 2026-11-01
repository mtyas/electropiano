#pragma once

#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <array>

namespace phs {

enum ParamID : uint32_t {
    kParamModelType = 0,       // 0: Rhodes MkI, 1: Rhodes Dyno, 2: Wurlitzer 200A, 3: PHS Hybrid
    kParamHammerHardness,      // Felt stiffness k_h and nonlinearity exponent alpha
    kParamHammerHysteresis,    // Hunt-Crossley dissipative felt loss lambda_h
    kParamStrikePosition,      // Relative strike point x_strike / L along cantilever beam
    kParamTineDecay,           // Primary fluid/viscous modal decay time (seconds)
    kParamTineBell,            // High cantilever mode (6.27x, 17.55x) chime amplitude
    kParamToneBarCoupling,     // Asymmetric tuning fork tone-bar coupling / reed solder mass
    kParamKeyNoise,            // Mechanical hammer/damper felt thump transient

    kParamPickupDistance,      // Air gap d_gap (closer = stronger nonlinear flux & bark)
    kParamPickupSymmetry,      // Vertical offset y_offset (0 = symmetric 2nd harm, >0 = asymmetric)
    kParamPickupResonance,     // Coil RLC resonance (Rhodes) / Capacitive load RC (Wurlitzer)
    kParamPreampDrive,         // Passive JFET/Tube stage saturation
    kParamBassBoost,           // Baxandall passive bass control (-12 to +12 dB)
    kParamTrebleBoost,         // Baxandall passive treble control (-12 to +12 dB)

    kParamTremoloDepth,        // Suitcase stereo auto-pan / Wurlitzer vibrato depth
    kParamTremoloRate,         // LFO speed in Hz (0.5 to 12 Hz)
    kParamPhaserMix,           // 4-stage optical all-pass phaser mix
    kParamPhaserRate,          // Phaser LFO rate in Hz (0.1 to 4.0 Hz)
    kParamChorusMix,           // Vintage BBD stereo chorus mix
    kParamReverbMix,           // Spring / Studio plate reverb wet mix
    kParamReverbDecay,         // Reverb decay time (0..1)
    kParamVelocityCurve,       // Velocity sensitivity (0..1)
    kParamMasterVolume,        // Output gain (0..1)

    kNumParams
};

struct ParamInfo {
    uint32_t id;
    const char* name;
    const char* shortName;
    const char* unit;
    const char* category;
    float minValue;
    float maxValue;
    float defaultValue;
    bool isStepped;
};

static constexpr std::array<ParamInfo, kNumParams> kParamMetadata = {{
    {kParamModelType,        "Model",           "Model",    "",    "Model",     0.0f, 3.0f, 0.0f, true},
    {kParamHammerHardness,   "Hardness",        "Hardness", "%",   "Mechanics", 0.0f, 1.0f, 0.48f, false},
    {kParamHammerHysteresis, "Cushion",         "Cushion",  "%",   "Mechanics", 0.0f, 1.0f, 0.35f, false},
    {kParamStrikePosition,   "Strike",          "Strike",   "x/L", "Mechanics", 0.08f, 0.42f, 0.18f, false},
    {kParamTineDecay,        "Decay",           "Decay",    "s",   "Mechanics", 0.15f, 1.0f, 0.65f, false},
    {kParamTineBell,         "Chime",           "Chime",    "%",   "Mechanics", 0.0f, 1.0f, 0.45f, false},
    {kParamToneBarCoupling,  "Tonebar",         "Tonebar",  "%",   "Mechanics", 0.0f, 1.0f, 0.55f, false},
    {kParamKeyNoise,         "Thump",           "Thump",    "%",   "Mechanics", 0.0f, 1.0f, 0.30f, false},

    {kParamPickupDistance,   "Air Gap",         "AirGap",   "mm",  "Pickup",    0.0f, 1.0f, 0.52f, false},
    {kParamPickupSymmetry,   "Voicing",         "Voicing",  "%",   "Pickup",    0.0f, 1.0f, 0.38f, false},
    {kParamPickupResonance,  "Coil Q",          "CoilQ",    "%",   "Pickup",    0.0f, 1.0f, 0.45f, false},
    {kParamPreampDrive,      "Drive",           "Drive",    "%",   "Pickup",    0.0f, 1.0f, 0.32f, false},
    {kParamBassBoost,        "Bass",            "Bass",     "dB",  "Pickup",    0.0f, 1.0f, 0.55f, false},
    {kParamTrebleBoost,      "Treble",          "Treble",   "dB",  "Pickup",    0.0f, 1.0f, 0.52f, false},

    {kParamTremoloDepth,     "Depth",           "Depth",    "%",   "Effects",   0.0f, 1.0f, 0.35f, false},
    {kParamTremoloRate,      "Speed",           "Speed",    "Hz",  "Effects",   0.0f, 1.0f, 0.42f, false},
    {kParamPhaserMix,        "Phaser",          "Phaser",   "%",   "Effects",   0.0f, 1.0f, 0.00f, false},
    {kParamPhaserRate,       "Rate",            "Rate",     "Hz",  "Effects",   0.0f, 1.0f, 0.28f, false},
    {kParamChorusMix,        "Chorus",          "Chorus",   "%",   "Effects",   0.0f, 1.0f, 0.00f, false},
    {kParamReverbMix,        "Mix",             "Mix",      "%",   "Effects",   0.0f, 1.0f, 0.22f, false},
    {kParamReverbDecay,      "Decay",           "Decay",    "%",   "Effects",   0.0f, 1.0f, 0.45f, false},
    {kParamVelocityCurve,    "Touch",           "Touch",    "%",   "Effects",   0.0f, 1.0f, 0.50f, false},
    {kParamMasterVolume,     "Volume",          "Volume",   "dB",  "Effects",   0.0f, 1.0f, 0.78f, false}
}};

struct Preset {
    const char* name;
    float values[kNumParams];
};

static constexpr int kNumPresets = 8;

static constexpr std::array<Preset, kNumPresets> kFactoryPresets = {{
    {
        "1973 Suitcase Rhodes Mk I",
        {
            0.0f,  // Model: Rhodes Mk I
            0.48f, // Hammer Hardness
            0.35f, // Hammer Hysteresis
            0.18f, // Strike Position
            0.68f, // Tine Decay
            0.45f, // Tine Bell
            0.58f, // ToneBar Coupling
            0.30f, // Key Noise
            0.54f, // Pickup Distance (Closer = more bark)
            0.38f, // Pickup Symmetry (Factory offset)
            0.45f, // Coil Resonance
            0.32f, // Preamp Drive
            0.58f, // Bass Boost
            0.52f, // Treble Boost
            0.40f, // Tremolo Depth (Suitcase auto-pan)
            0.42f, // Tremolo Rate
            0.00f, // Phaser Mix
            0.25f, // Phaser Rate
            0.00f, // Chorus Mix
            0.22f, // Reverb Mix
            0.45f, // Reverb Decay
            0.50f, // Velocity Curve
            0.78f  // Master Volume
        }
    },
    {
        "Wurlitzer 200A Reedy Bark",
        {
            2.0f,  // Model: Wurlitzer 200A
            0.68f, // Hammer Hardness (harder felt)
            0.28f, // Hammer Hysteresis
            0.24f, // Strike Position
            0.42f, // Reed Decay (shorter, punchier)
            0.22f, // Reed Bell (less chime, more fundamental/bark)
            0.35f, // Solder Mass Coupling
            0.35f, // Key Noise
            0.66f, // Pickup Distance (tight electrostatic gap -> aggressive bark)
            0.52f, // Pickup Symmetry
            0.55f, // Capacitive Load Q
            0.48f, // Preamp Drive
            0.46f, // Bass
            0.58f, // Treble
            0.45f, // Vibrato Depth (200A Twin-Diode Vibrato)
            0.52f, // Vibrato Rate (~5.5 Hz)
            0.00f, // Phaser Mix
            0.25f, // Phaser Rate
            0.00f, // Chorus Mix
            0.18f, // Reverb Mix
            0.35f, // Reverb Decay
            0.55f, // Velocity Curve
            0.78f  // Master Volume
        }
    },
    {
        "Dyno-My-Piano LA 80s Bell",
        {
            1.0f,  // Model: Rhodes Dyno
            0.62f, // Harder hammer tips
            0.22f, // Lower felt loss -> brighter transient
            0.14f, // Closer to clamped root -> strong overtones
            0.75f, // Long sustain
            0.78f, // Prominent crystalline tine bell
            0.65f, // Strong tonebar shimmer
            0.25f, // Key Noise
            0.62f, // Close pickup gap (Dyno mod)
            0.22f, // Centered voicing for strong 2nd harmonic octave chime
            0.65f, // High pickup Q
            0.38f, // Preamp Drive
            0.64f, // Bass Boost
            0.74f, // Treble Boost
            0.20f, // Subtle stereo pan
            0.35f, // Tremolo Rate
            0.00f, // Phaser Mix
            0.25f, // Phaser Rate
            0.55f, // Lush Stereo Tri-Chorus
            0.32f, // Reverb Mix
            0.58f, // Reverb Decay
            0.48f, // Velocity Curve
            0.78f  // Master Volume
        }
    },
    {
        "1975 Phase-90 Funk Stage",
        {
            0.0f,  // Model: Rhodes Mk I
            0.58f, // Punchy hammer
            0.30f, // Felt loss
            0.19f, // Strike Position
            0.62f, // Decay
            0.42f, // Bell
            0.50f, // ToneBar
            0.32f, // Key Noise
            0.68f, // Very close pickup for funky growl
            0.45f, // Asymmetric voicing
            0.52f, // Coil Q
            0.45f, // Warm preamp grit
            0.55f, // Bass
            0.56f, // Treble
            0.15f, // Tremolo Depth
            0.40f, // Tremolo Rate
            0.68f, // Classic Optical Phaser engaged
            0.24f, // Slow sweep
            0.00f, // Chorus Mix
            0.20f, // Reverb Mix
            0.40f, // Reverb Decay
            0.52f, // Velocity Curve
            0.78f  // Master Volume
        }
    },
    {
        "Wurlitzer 140B Mellow Soul",
        {
            2.0f,  // Model: Wurlitzer
            0.42f, // Softer worn felt hammer
            0.45f, // Higher felt damping
            0.28f, // Strike position closer to center
            0.50f, // Medium reed decay
            0.18f, // Warm minimal high modes
            0.42f, // Reed mass coupling
            0.38f, // Vintage mechanical action noise
            0.45f, // Wider gap for sweeter, cleaner dynamics
            0.35f, // Symmetry
            0.40f, // Pickup Q
            0.30f, // Tube preamp warmth
            0.58f, // Warm bass
            0.44f, // Rolled-off treble
            0.50f, // Classic tube vibrato
            0.48f, // Vibrato Rate
            0.00f, // Phaser Mix
            0.25f, // Phaser Rate
            0.00f, // Chorus Mix
            0.25f, // Spring Reverb
            0.42f, // Reverb Decay
            0.45f, // Velocity Curve
            0.80f  // Master Volume
        }
    },
    {
        "Warm Jazz Ballad Tines",
        {
            0.0f,  // Model: Rhodes Mk I
            0.34f, // Soft felt hammers
            0.48f, // Gentle hysteretic cushion
            0.22f, // Warm strike point
            0.80f, // Long singing sustain
            0.35f, // Delicate bell
            0.70f, // Deep 3D tone-bar coupling
            0.20f, // Low key noise
            0.40f, // Backed-off pickup distance for pure fundamental
            0.18f, // Near-symmetric octave overtone bloom
            0.38f, // Smooth coil response
            0.18f, // Clean preamp
            0.60f, // Deep warm low end
            0.46f, // Silky highs
            0.55f, // Deep slow Suitcase stereo vibrato
            0.26f, // Slow rate
            0.00f, // Phaser
            0.20f, // Phaser Rate
            0.18f, // Subtle spatial chorus
            0.35f, // Lush hall reverb
            0.62f, // Long reverb tail
            0.42f, // Soft velocity curve
            0.80f  // Master Volume
        }
    },
    {
        "PHS Hybrid Electro-Acoustic",
        {
            3.0f,  // Model: PHS Hybrid (Dual Magnetic + Electrostatic Pickup)
            0.55f, // Balanced hammer
            0.30f, // Felt loss
            0.16f, // Rich modal excitation
            0.72f, // Long sustain
            0.60f, // Clear modal chime
            0.65f, // Strong coupled resonator
            0.28f, // Key Noise
            0.58f, // Expressive nonlinear pickup
            0.42f, // Balanced symmetry
            0.55f, // Resonant transducer
            0.35f, // Preamp Drive
            0.56f, // Bass
            0.60f, // Treble
            0.30f, // Stereo Tremolo
            0.38f, // Tremolo Rate
            0.25f, // Subtle Phaser
            0.22f, // Phaser Rate
            0.30f, // Stereo Chorus
            0.30f, // Reverb Mix
            0.55f, // Reverb Decay
            0.50f, // Velocity Curve
            0.78f  // Master Volume
        }
    },
    {
        "PHS Nonlinear Physics Lab",
        {
            3.0f,  // Model: PHS Hybrid
            0.78f, // Very hard hammer -> intense modal excitation
            0.15f, // Low loss -> maximum energy transfer
            0.12f, // Near root -> strong higher beam modes
            0.88f, // Extended cantilever resonance
            0.85f, // High beam overtones
            0.82f, // Strong tonebar coupling beats
            0.45f, // Audible mechanical impact
            0.80f, // Extreme pickup flux nonlinearity (foldover bark!)
            0.60f, // Strong asymmetry
            0.75f, // High coil Q
            0.55f, // Driven preamp
            0.55f, // Bass
            0.65f, // Treble
            0.25f, // Tremolo
            0.45f, // Tremolo Rate
            0.35f, // Phaser
            0.30f, // Phaser Rate
            0.25f, // Chorus
            0.40f, // Reverb Mix
            0.70f, // Reverb Decay
            0.60f, // Velocity Curve
            0.75f  // Master Volume
        }
    }
}};

inline void formatParamDisplay(uint32_t id, float normVal, char* outBuf, size_t bufSize) {
    switch (id) {
        case kParamModelType: {
            int m = std::clamp(static_cast<int>(std::round(normVal * 3.0f)), 0, 3);
            const char* names[4] = {"Rhodes Mk I", "Rhodes Dyno", "Wurli 200A", "PHS Hybrid"};
            std::snprintf(outBuf, bufSize, "%s", names[m]);
            break;
        }
        case kParamStrikePosition: {
            float xL = 0.08f + normVal * (0.42f - 0.08f);
            std::snprintf(outBuf, bufSize, "%.2f L", xL);
            break;
        }
        case kParamTineDecay: {
            float sec = 0.4f + normVal * 11.6f;
            std::snprintf(outBuf, bufSize, "%.1f s", sec);
            break;
        }
        case kParamPickupDistance: {
            float mm = 2.8f - normVal * 2.3f; // higher knob = closer gap (more bark)
            std::snprintf(outBuf, bufSize, "%.2f mm", mm);
            break;
        }
        case kParamBassBoost:
        case kParamTrebleBoost: {
            float db = (normVal - 0.5f) * 24.0f;
            std::snprintf(outBuf, bufSize, "%+.1f dB", db);
            break;
        }
        case kParamTremoloRate: {
            float hz = 0.5f + normVal * 10.5f;
            std::snprintf(outBuf, bufSize, "%.1f Hz", hz);
            break;
        }
        case kParamPhaserRate: {
            float hz = 0.08f + normVal * 3.5f;
            std::snprintf(outBuf, bufSize, "%.2f Hz", hz);
            break;
        }
        case kParamMasterVolume: {
            if (normVal <= 0.001f) {
                std::snprintf(outBuf, bufSize, "-inf dB");
            } else {
                float db = 20.0f * std::log10(normVal / 0.78f);
                std::snprintf(outBuf, bufSize, "%+.1f dB", db);
            }
            break;
        }
        default: {
            std::snprintf(outBuf, bufSize, "%.0f%%", normVal * 100.0f);
            break;
        }
    }
}

} // namespace phs
