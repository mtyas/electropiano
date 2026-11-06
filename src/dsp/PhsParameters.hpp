#pragma once

#include "PhsVersion.hpp"
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

    // --- Stages and effects ---
    kParamResonator,           // 0 Rhodes tine (PyPHS beam + hammer), 1 Wurlitzer reed (OpenWurli)
    kParamPickupType,          // 0 Electromagnetic (Falaize-Helie), 1 Electrostatic (OpenWurli overlap law)
    kParamAmpType,             // 0 Suitcase/stage preamp, 1 Wurlitzer 200A circuit (OpenWurli)
    kParamCabinet,             // Speaker cabinet character (0 = flat DI)
    kParamWahAmount,           // Envelope filter (auto-wah) amount
    kParamWahSens,             // Envelope filter sensitivity
    kParamDelayMix,            // Tape/BBD echo mix
    kParamDelayTime,           // Echo time
    kParamDelayFeedback,       // Echo feedback

    // --- Per-note expression (MPE), modulation matrix, tempo sync, oversampling ---
    kParamMpeMode,             // 0 Off (channel-less), 1 MPE: one note per channel, per-note bend / pressure / slide
    kParamBendRange,           // Pitch-bend range in semitones (1..48): global wheel, or MPE member channels
    kParamPressureAmt,         // Per-note pressure: pulls the pickup closer (barkier, louder)
    kParamSlideAmt,            // Per-note slide (CC74): opens the pickup coil / brightness
    kParamQuality,             // Oversampling of the nonlinear amp stage: 0 Off, 1 2x, 2 4x
    kParamTremSync,            // Tempo sync of the tremolo / vibrato (0 = free Hz)
    kParamPhaserSync,          // Tempo sync of the phaser
    kParamEchoSync,            // Tempo sync of the echo
    kParamLfo1Rate, kParamLfo1Shape, kParamLfo1Sync,
    kParamLfo2Rate, kParamLfo2Shape, kParamLfo2Sync,
    kParamMod1Src, kParamMod1Dst, kParamMod1Amt,
    kParamMod2Src, kParamMod2Dst, kParamMod2Amt,
    kParamMod3Src, kParamMod3Dst, kParamMod3Amt,
    kParamMod4Src, kParamMod4Dst, kParamMod4Amt,
    kParamMod5Src, kParamMod5Dst, kParamMod5Amt,
    kParamMod6Src, kParamMod6Dst, kParamMod6Amt,

    // --- "Impossible instrument" macros, studio effects ---
    kParamMaterial,            // Tine material: steel -> glass -> carbon fibre (decay, bell stretch, chime)
    kParamPickupCount,         // Magnetic pickups per tine: 1..4
    kParamPickupSpread,        // Spacing between those pickups
    kParamGapFmDepth,          // Audio-rate modulation of the pickup gap / offset (physical FM)
    kParamGapFmRatio,          // ... at this multiple of the note's frequency
    kParamResMorph,            // Crossfade from the chosen resonator to the other one, within the note
    kParamReverbType,          // 0 Hall, 1 Spring
    kParamCabType,             // 0 Speaker, 1 Open back, 2 Closed 2x12, 3 Studio mic, 4 Radio
    kParamEqFreq,              // Post-EQ mid band frequency
    kParamEqGain,              // Post-EQ mid band gain (+-12 dB)
    kParamCompAmount,          // Bus compressor mix (parallel; 0 = off)

    // --- Mastering EQ, compressor + limiter, cabinet mic / room ---
    kParamEqB1Freq, kParamEqB1Gain, kParamEqB1Q, kParamEqB1Shelf,   // low band (shelf or bell)
    kParamEqB2Freq, kParamEqB2Gain, kParamEqB2Q,                     // low-mid bell
    kParamEqB3Q,                                                      // high-mid bell (freq / gain: kParamEqFreq / kParamEqGain)
    kParamEqB4Freq, kParamEqB4Gain, kParamEqB4Q, kParamEqB4Shelf,   // high band (shelf or bell)
    kParamCompThresh, kParamCompRatio, kParamCompAttack, kParamCompRelease, kParamCompKnee,
    kParamCompMakeup, kParamCompHpf, kParamCompDetect, kParamCompAuto,
    kParamLimOn, kParamLimCeil,
    kParamCabMic,              // Mic position: on the cone centre (bright) to the edge (dark, combed)
    kParamCabDistance,         // Mic distance: proximity bass boost fades, air and room come in
    kParamCabRoom,             // Room reflections

    // --- Tuning, playing, realism and space ---
    kParamMasterTune,          // Concert pitch: A4 = 415 .. 466 Hz
    kParamTranspose,           // -24 .. +24 semitones
    kParamPolyphony,           // Voices: 8, 12, 16, 24, 32, 48, 64
    kParamSympathetic,         // Sympathetic resonance of the undamped tines / reeds (pedal down)
    kParamCondition,           // Per-key variation: tuning, voicing, decay, hammer wear (0 = factory-perfect)
    kParamStereoSpread,        // Keyboard spread across the stereo field
    kParamReverbPreDelay,      // Reverb pre-delay (0 .. 120 ms)
    kParamChorusMode,          // 0 Dual (two BBD taps), 1 Tri-chorus (three taps, 120 degrees apart)
    kParamGapFmExact,          // Gap FM ratio: 0 free, 1 snapped to exact ratios (1:4 .. 8:1)

    // --- Effect switches (the knobs keep their settings while an effect is off) ---
    kParamWahOn, kParamPhaserOn, kParamTremOn, kParamChorusOn, kParamEchoOn, kParamReverbOn, kParamEqOn, kParamCompOn,

    // --- Envelope filter, phaser and chorus in depth ---
    kParamWahReso,             // Filter resonance (Q 0.7 .. 14)
    kParamWahMode,             // 0 Classic (low-pass + band-pass), 1 Low-pass, 2 Band-pass, 3 High-pass
    kParamWahDir,              // 0 sweeps up with the playing, 1 sweeps down
    kParamWahFreq,             // Resting frequency the sweep starts from
    kParamWahRelease,          // How fast the filter falls back after a note
    kParamPhaserDepth,         // Sweep width (octaves)
    kParamPhaserFeedback,      // Resonance: negative and positive feedback
    kParamPhaserStages,        // 2, 4, 6, 8 or 12 all-pass stages
    kParamPhaserCenter,        // Centre of the sweep
    kParamPhaserSpread,        // Stereo: phase offset between the left and right sweeps
    kParamChorusRate,          // Chorus LFO speed
    kParamChorusDepth,         // Chorus sweep depth
    kParamChorusDelay,         // Chorus base delay (short = flanger-like, long = doubling)
    kParamChorusWidth,         // Stereo width of the wet signal

    // --- Easy controls: one knob each that moves every related setting ---
    kParamEasyDecay,           // Shorter <-> longer ring
    kParamEasyTone,            // Darker / bassier <-> brighter
    kParamEasyDrive,           // Cleaner <-> dirtier
    kParamEasyMod,             // Scales the modulation matrix and the modulation effects (0 = none, centre = as set, max = double)

    kNumParams
};

// Voice units (the suitcase chain's level) per pickup volt. Reed + electrostatic pickup
// voices are scaled by this, and the 200A amp divides it back out, so that chain runs
// on exactly the voltages OpenWurli's circuit expects.
static constexpr double kVoiceUnitsPerVolt = 52.5;
// Gain applied to the summed voices before the effects chain
static constexpr float kVoiceMixGain = 0.32f;

static constexpr int kNumModSlots = 6;
static constexpr uint32_t kFirstModSlotParam = kParamMod1Src;

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
    {kParamMpeMode,          "MPE",            "MPE",      "",    "Expression", 0.00f, 1},
    {kParamBendRange,        "Bend Range",     "Bend",     "st",  "Expression", 1.0f / 47.0f, 47},
    {kParamPressureAmt,      "Pressure",       "Press",    "%",   "Expression", 0.50f, 0},
    {kParamSlideAmt,         "Slide",          "Slide",    "%",   "Expression", 0.50f, 0},
    {kParamQuality,          "Quality",        "Quality",  "",    "Output",     1.00f / 2.0f, 2},
    {kParamTremSync,         "Trem Sync",      "TrSync",   "",    "Sync",       0.0f,  7},
    {kParamPhaserSync,       "Phaser Sync",    "PhSync",   "",    "Sync",       0.0f,  7},
    {kParamEchoSync,         "Echo Sync",      "EcSync",   "",    "Sync",       0.0f,  7},
    {kParamLfo1Rate,         "LFO 1 Rate",     "L1Rate",   "Hz",  "Modulation", 0.45f, 0},
    {kParamLfo1Shape,        "LFO 1 Shape",    "L1Shape",  "",    "Modulation", 0.0f,  4},
    {kParamLfo1Sync,         "LFO 1 Sync",     "L1Sync",   "",    "Modulation", 0.0f,  7},
    {kParamLfo2Rate,         "LFO 2 Rate",     "L2Rate",   "Hz",  "Modulation", 0.30f, 0},
    {kParamLfo2Shape,        "LFO 2 Shape",    "L2Shape",  "",    "Modulation", 0.0f,  4},
    {kParamLfo2Sync,         "LFO 2 Sync",     "L2Sync",   "",    "Modulation", 0.0f,  7},
    {kParamMod1Src,          "Mod 1 Source",   "M1Src",    "",    "Modulation", 0.0f,  8},
    {kParamMod1Dst,          "Mod 1 Target",   "M1Dst",    "",    "Modulation", 0.0f,  39},
    {kParamMod1Amt,          "Mod 1 Amount",   "M1Amt",    "%",   "Modulation", 0.5f,  0},
    {kParamMod2Src,          "Mod 2 Source",   "M2Src",    "",    "Modulation", 0.0f,  8},
    {kParamMod2Dst,          "Mod 2 Target",   "M2Dst",    "",    "Modulation", 0.0f,  39},
    {kParamMod2Amt,          "Mod 2 Amount",   "M2Amt",    "%",   "Modulation", 0.5f,  0},
    {kParamMod3Src,          "Mod 3 Source",   "M3Src",    "",    "Modulation", 0.0f,  8},
    {kParamMod3Dst,          "Mod 3 Target",   "M3Dst",    "",    "Modulation", 0.0f,  39},
    {kParamMod3Amt,          "Mod 3 Amount",   "M3Amt",    "%",   "Modulation", 0.5f,  0},
    {kParamMod4Src,          "Mod 4 Source",   "M4Src",    "",    "Modulation", 0.0f,  8},
    {kParamMod4Dst,          "Mod 4 Target",   "M4Dst",    "",    "Modulation", 0.0f,  39},
    {kParamMod4Amt,          "Mod 4 Amount",   "M4Amt",    "%",   "Modulation", 0.5f,  0},
    {kParamMod5Src,          "Mod 5 Source",   "M5Src",    "",    "Modulation", 0.0f,  8},
    {kParamMod5Dst,          "Mod 5 Target",   "M5Dst",    "",    "Modulation", 0.0f,  39},
    {kParamMod5Amt,          "Mod 5 Amount",   "M5Amt",    "%",   "Modulation", 0.5f,  0},
    {kParamMod6Src,          "Mod 6 Source",   "M6Src",    "",    "Modulation", 0.0f,  8},
    {kParamMod6Dst,          "Mod 6 Target",   "M6Dst",    "",    "Modulation", 0.0f,  39},
    {kParamMod6Amt,          "Mod 6 Amount",   "M6Amt",    "%",   "Modulation", 0.5f,  0},
    {kParamMaterial,         "Material",       "Material", "",    "Macros",     0.00f, 0},
    {kParamPickupCount,      "Pickup Count",   "Pickups",  "",    "Macros",     0.00f, 3},
    {kParamPickupSpread,     "Pickup Spread",  "Spread",   "mm",  "Macros",     0.50f, 0},
    {kParamGapFmDepth,       "Gap FM",         "GapFM",    "%",   "Macros",     0.00f, 0},
    {kParamGapFmRatio,       "Gap FM Ratio",   "FMRatio",  "x",   "Macros",     0.35f, 0},
    {kParamResMorph,         "Morph",          "Morph",    "%",   "Macros",     0.00f, 0},
    {kParamReverbType,       "Reverb Type",    "RevType",  "",    "Effects",    2.0f / 3.0f, 3},
    {kParamCabType,          "Cabinet Type",   "CabType",  "",    "Amp",        0.00f, 9},
    {kParamEqFreq,           "EQ HiMid Freq",  "EQ3Freq",  "Hz",  "Mastering EQ", 0.50f, 0},
    {kParamEqGain,           "EQ HiMid Gain",  "EQ3Gain",  "dB",  "Mastering EQ", 0.50f, 0},
    {kParamCompAmount,       "Comp Mix",       "CompMix",  "%",   "Dynamics",   0.00f, 0},
    {kParamEqB1Freq,         "EQ Low Freq",    "EQ1Freq",  "Hz",  "Mastering EQ", 0.341f, 0},
    {kParamEqB1Gain,         "EQ Low Gain",    "EQ1Gain",  "dB",  "Mastering EQ", 0.50f, 0},
    {kParamEqB1Q,            "EQ Low Q",       "EQ1Q",     "",    "Mastering EQ", 0.245f, 0},
    {kParamEqB1Shelf,        "EQ Low Shelf",   "EQ1Shelf", "",    "Mastering EQ", 1.00f, 1},
    {kParamEqB2Freq,         "EQ LoMid Freq",  "EQ2Freq",  "Hz",  "Mastering EQ", 0.354f, 0},
    {kParamEqB2Gain,         "EQ LoMid Gain",  "EQ2Gain",  "dB",  "Mastering EQ", 0.50f, 0},
    {kParamEqB2Q,            "EQ LoMid Q",     "EQ2Q",     "",    "Mastering EQ", 0.313f, 0},
    {kParamEqB3Q,            "EQ HiMid Q",     "EQ3Q",     "",    "Mastering EQ", 0.313f, 0},
    {kParamEqB4Freq,         "EQ High Freq",   "EQ4Freq",  "Hz",  "Mastering EQ", 0.646f, 0},
    {kParamEqB4Gain,         "EQ High Gain",   "EQ4Gain",  "dB",  "Mastering EQ", 0.50f, 0},
    {kParamEqB4Q,            "EQ High Q",      "EQ4Q",     "",    "Mastering EQ", 0.245f, 0},
    {kParamEqB4Shelf,        "EQ High Shelf",  "EQ4Shelf", "",    "Mastering EQ", 1.00f, 1},
    {kParamCompThresh,       "Comp Threshold", "Thresh",   "dB",  "Dynamics",   0.55f, 0},
    {kParamCompRatio,        "Comp Ratio",     "Ratio",    ":1",  "Dynamics",   0.463f, 0},
    {kParamCompAttack,       "Comp Attack",    "Attack",   "ms",  "Dynamics",   0.667f, 0},
    {kParamCompRelease,      "Comp Release",   "Release",  "ms",  "Dynamics",   0.587f, 0},
    {kParamCompKnee,         "Comp Knee",      "Knee",     "dB",  "Dynamics",   0.33f, 0},
    {kParamCompMakeup,       "Comp Makeup",    "Makeup",   "dB",  "Dynamics",   0.00f, 0},
    {kParamCompHpf,          "Comp Sidechain", "SC HPF",   "Hz",  "Dynamics",   0.00f, 0},
    {kParamCompDetect,       "Comp Detector",  "Detect",   "",    "Dynamics",   0.00f, 1},
    {kParamCompAuto,         "Comp Auto Gain", "AutoGain", "",    "Dynamics",   1.00f, 1},
    {kParamLimOn,            "Limiter",        "Limiter",  "",    "Dynamics",   0.00f, 1},
    {kParamLimCeil,          "Limiter Ceiling","Ceiling",  "dB",  "Dynamics",   0.975f, 0},
    {kParamCabMic,           "Mic Position",   "MicPos",   "%",   "Amp",        0.25f, 0},
    {kParamCabDistance,      "Mic Distance",   "MicDist",  "%",   "Amp",        0.30f, 0},
    {kParamCabRoom,          "Room",           "Room",     "%",   "Amp",        0.20f, 0},
    {kParamMasterTune,       "Master Tune",    "Tune",     "Hz",  "Output",     25.0f / 51.0f, 0},
    {kParamTranspose,        "Transpose",      "Transp",   "st",  "Output",     0.5f,  48},
    {kParamPolyphony,        "Polyphony",      "Voices",   "",    "Output",     4.0f / 6.0f, 6},
    {kParamSympathetic,      "Sympathetic",    "Sympath",  "%",   "Resonator",  0.20f, 0},
    {kParamCondition,        "Condition",      "Cond",     "%",   "Resonator",  0.20f, 0},
    {kParamStereoSpread,     "Key Spread",     "KSpread",  "%",   "Output",     0.00f, 0},
    {kParamReverbPreDelay,   "Pre-Delay",      "PreDly",   "ms",  "Effects",    0.10f, 0},
    {kParamChorusMode,       "Chorus Mode",    "ChMode",   "",    "Effects",    1.00f, 1},
    {kParamGapFmExact,       "Gap FM Exact",   "FMExact",  "",    "Macros",     0.00f, 1},
    {kParamWahOn,            "Env Filter On",  "WahOn",    "",    "Effects",    1.00f, 1},
    {kParamPhaserOn,         "Phaser On",      "PhsOn",    "",    "Effects",    1.00f, 1},
    {kParamTremOn,           "Tremolo On",     "TremOn",   "",    "Amp",        1.00f, 1},
    {kParamChorusOn,         "Chorus On",      "ChOn",     "",    "Effects",    1.00f, 1},
    {kParamEchoOn,           "Echo On",        "EchoOn",   "",    "Effects",    1.00f, 1},
    {kParamReverbOn,         "Reverb On",      "RevOn",    "",    "Effects",    1.00f, 1},
    {kParamEqOn,             "EQ On",          "EqOn",     "",    "Mastering EQ", 1.00f, 1},
    {kParamCompOn,           "Comp On",        "CompOn",   "",    "Dynamics",   1.00f, 1},
    {kParamWahReso,          "Env Resonance",  "WahRes",   "",    "Effects",    0.582f, 0},
    {kParamWahMode,          "Env Mode",       "WahMode",  "",    "Effects",    0.00f, 3},
    {kParamWahDir,           "Env Direction",  "WahDir",   "",    "Effects",    0.00f, 1},
    {kParamWahFreq,          "Env Frequency",  "WahFreq",  "Hz",  "Effects",    0.375f, 0},
    {kParamWahRelease,       "Env Release",    "WahRel",   "ms",  "Effects",    0.515f, 0},
    {kParamPhaserDepth,      "Phaser Depth",   "PhsDep",   "oct", "Effects",    0.521f, 0},
    {kParamPhaserFeedback,   "Phaser Feedback","PhsFb",    "%",   "Effects",    0.721f, 0},
    {kParamPhaserStages,     "Phaser Stages",  "PhsStg",   "",    "Effects",    0.25f, 4},
    {kParamPhaserCenter,     "Phaser Center",  "PhsCtr",   "Hz",  "Effects",    0.471f, 0},
    {kParamPhaserSpread,     "Phaser Stereo",  "PhsSpr",   "deg", "Effects",    0.36f, 0},
    {kParamChorusRate,       "Chorus Rate",    "ChRate",   "Hz",  "Effects",    0.5056f, 0},
    {kParamChorusDepth,      "Chorus Depth",   "ChDepth",  "%",   "Effects",    0.50f, 0},
    {kParamChorusDelay,      "Chorus Delay",   "ChDelay",  "ms",  "Effects",    0.3056f, 0},
    {kParamChorusWidth,      "Chorus Width",   "ChWidth",  "%",   "Effects",    0.50f, 0},
    {kParamEasyDecay,        "Easy Decay",     "EzDecay",  "",    "Easy",       0.50f, 0},
    {kParamEasyTone,         "Easy Tone",      "EzTone",   "",    "Easy",       0.50f, 0},
    {kParamEasyDrive,        "Easy Drive",     "EzDrive",  "",    "Easy",       0.50f, 0},
    {kParamEasyMod,          "Easy Mod",       "EzMod",    "",    "Easy",       0.50f, 0},
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

// ---- Expression, modulation and macro mappings -------------------------------
inline int bendRangeSemis(float norm) { return 1 + stepIndex(norm, 47); }
// 0.05 .. 7.5 Hz over the first three quarters (as before), then up to 400 Hz: audio-rate wobble
inline float lfoRateHz(float norm) {
    return norm <= 0.75f ? 0.05f * std::pow(800.0f, norm) : 7.5212f * std::pow(53.18f, (norm - 0.75f) * 4.0f);
}
// Material: steel (0) -> glass (0.5) -> carbon fibre (1). g covers the first half, h the second.
inline float materialDecayMul(float m) { const float g = std::min(1.0f, 2.0f * m), h = std::max(0.0f, 2.0f * m - 1.0f); return 1.0f + 0.8f * g + 1.4f * h; }
inline float materialBellDecayMul(float m) { const float g = std::min(1.0f, 2.0f * m), h = std::max(0.0f, 2.0f * m - 1.0f); return 1.0f + 1.5f * g + 1.5f * h; }
inline float materialBellStretch(float m) { const float g = std::min(1.0f, 2.0f * m), h = std::max(0.0f, 2.0f * m - 1.0f); return 1.0f + 0.18f * g + 0.22f * h; }
inline float materialChimeMul(float m) { const float g = std::min(1.0f, 2.0f * m), h = std::max(0.0f, 2.0f * m - 1.0f); return 1.0f + 0.9f * g + 1.3f * h; }
// Added to the Chime knob so the bell overtones the material reshapes are there to hear
inline float materialBellLift(float m) { const float g = std::min(1.0f, 2.0f * m), h = std::max(0.0f, 2.0f * m - 1.0f); return 0.30f * g + 0.20f * h; }
inline int pickupCountFromNorm(float n) { return 1 + stepIndex(n, 3); }
inline float pickupSpreadMm(float n) { return 1.5f * n; }
inline float gapFmRatio(float n) { return 0.25f * std::pow(32.0f, n); }                  // 0.25x .. 8x the note
// Exact mode: the nearest (in pitch) of 1:4, 1:2, 1:1, 2:1 .. 8:1
inline float gapFmRatioExact(float n) {
    static constexpr float kRatios[] = {0.25f, 0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    const float r = gapFmRatio(n);
    float best = kRatios[0];
    for (float k : kRatios) if (std::fabs(std::log(r / k)) < std::fabs(std::log(r / best))) best = k;
    return best;
}
inline float gapFmRatio(float n, bool exact) { return exact ? gapFmRatioExact(n) : gapFmRatio(n); }
inline void formatGapFmRatio(float n, bool exact, char* out, size_t size) {
    const float r = gapFmRatio(n, exact);
    if (!exact) std::snprintf(out, size, "x%.2f", r);
    else if (r < 1.0f) std::snprintf(out, size, "1:%.0f", 1.0f / r);
    else std::snprintf(out, size, "%.0f:1", r);
}
inline float eqFreqHz(float n) { return 200.0f * std::pow(25.0f, n); }                   // 200 Hz .. 5 kHz
inline float eqGainDb(float n) { return (n - 0.5f) * 24.0f; }
inline float eqQ(float n) { return 0.3f * std::pow(33.33f, n); }                         // 0.3 .. 10
inline float eqBandFreqHz(int band, float n) {
    switch (band) {
        case 0: return 20.0f * std::pow(25.0f, n);       // 20 Hz .. 500 Hz
        case 1: return 80.0f * std::pow(25.0f, n);       // 80 Hz .. 2 kHz
        case 2: return 200.0f * std::pow(25.0f, n);      // 200 Hz .. 5 kHz
        default: return 1500.0f * std::pow(13.333f, n);  // 1.5 kHz .. 20 kHz
    }
}
inline float compThreshDb(float n) { return -40.0f + 40.0f * n; }
inline float compRatio(float n) { return std::pow(20.0f, n); }                           // 1 .. 20
inline float compAttackMs(float n) { return 0.1f * std::pow(1000.0f, n); }               // 0.1 .. 100 ms
inline float compReleaseMs(float n) { return 10.0f * std::pow(100.0f, n); }              // 10 .. 1000 ms
inline float compKneeDb(float n) { return 12.0f * n; }
inline float compMakeupDb(float n) { return 18.0f * n; }
inline float compHpfHz(float n) { return 20.0f * std::pow(20.0f, n); }                   // 20 .. 400 Hz
inline float limCeilDb(float n) { return -12.0f + 12.0f * n; }

// ---- Tuning, playing and space mappings ---------------------------------------
inline float masterTuneHz(float n) { return 415.0f + 51.0f * n; }                       // A4
inline int transposeSemis(float n) { return stepIndex(n, 48) - 24; }
inline int polyphonyVoices(float n) { static const int v[7] = {8, 12, 16, 24, 32, 48, 64}; return v[stepIndex(n, 6)]; }
inline float preDelayMs(float n) { return 120.0f * n; }
enum ReverbAlgo { kRevClassicHall = 0, kRevSpring, kRevStudioHall, kRevPlate };
inline int reverbAlgo(float n) { return stepIndex(n, 3); }

// Inverse of eqBandFreqHz: the normalised value that puts band `band` at f Hz (the editor drags nodes with it)
inline float eqBandFreqNorm(int band, float f) {
    static const float lo[4] = {20.0f, 80.0f, 200.0f, 1500.0f}, span[4] = {25.0f, 25.0f, 25.0f, 13.333f};
    return std::clamp(std::log(f / lo[band]) / std::log(span[band]), 0.0f, 1.0f);
}
// Compressor gain computer (shared by the DSP and the editor's transfer curve): the gain change in dB for a
// detector level `over` dB above the threshold, with a quadratic soft knee of width `knee` dB
inline float compStaticGrDb(float over, float ratio, float knee) {
    if (2.0f * over > knee) return (1.0f / ratio - 1.0f) * over;
    if (knee > 0.0f && 2.0f * over > -knee) {
        const float t = over + 0.5f * knee;
        return (1.0f / ratio - 1.0f) * t * t / (2.0f * knee);
    }
    return 0.0f;
}

// ---- Envelope filter, phaser, chorus ------------------------------------------
inline bool switchOn(float norm) { return norm >= 0.5f; }
inline float wahQ(float n) { return 0.7f * std::pow(20.0f, n); }                     // 0.7 .. 14
inline float wahBaseHz(float n) { return 60.0f * std::pow(2.0f, n * 5.0f); }         // 60 Hz .. 1.9 kHz
inline float wahReleaseMs(float n) { return 20.0f * std::pow(50.0f, n); }            // 20 ms .. 1 s
inline float phaserDepthOct(float n) { return 6.0f * n; }
inline float phaserFeedback(float n) { return 0.95f * (2.0f * n - 1.0f); }           // -95 .. +95 %
inline int phaserStages(float n) { static constexpr int k[5] = {2, 4, 6, 8, 12}; return k[stepIndex(n, 4)]; }
inline float phaserCenterHz(float n) { return 100.0f * std::pow(2.0f, n * 6.0f); }   // 100 Hz .. 6.4 kHz
inline float chorusRateHz(float n) { return 0.05f * std::pow(160.0f, n); }           // 0.05 .. 8 Hz
inline float chorusDelayMs(float n) { return 2.0f + 18.0f * n; }                     // 2 .. 20 ms
// Easy controls: -1 .. +1 around the centre; Mod scales 0 .. 2x
inline float easyOffset(float n) { return 2.0f * n - 1.0f; }
inline float easyModScale(float n) { return 2.0f * n; }

// The Easy knobs: each moves every setting behind one idea, as an offset on the panel knobs (so
// the panel stays as set). Mod scales the modulation effects. The editor runs the same function
// to show each knob where the Easy knobs have taken it.
inline void applyEasyControls(float* p) {
    auto add = [p](uint32_t id, float v) { p[id] = std::clamp(p[id] + v, 0.0f, 1.0f); };
    const float d = easyOffset(p[kParamEasyDecay]);
    if (d != 0.0f) {
        add(kParamTineDecay, 0.55f * d);         // the tine or reed's ring
        add(kParamToneBarCoupling, 0.20f * d);   // the tone bar's sustain
        add(kParamReverbDecay, 0.40f * d);
        add(kParamReverbMix, 0.15f * d);
        add(kParamDelayFeedback, 0.30f * d);     // echo repeats
        add(kParamSympathetic, 0.50f * std::max(0.0f, d));
    }
    const float t = easyOffset(p[kParamEasyTone]);
    if (t != 0.0f) {
        add(kParamBassBoost, -0.40f * t);
        add(kParamTrebleBoost, 0.40f * t);
        add(kParamTineBell, 0.35f * t);          // chime
        add(kParamHammerHardness, 0.30f * t);
        add(kParamEqB1Gain, -0.25f * t);         // low shelf, up to -/+ 6 dB
        add(kParamEqB4Gain, 0.25f * t);          // high shelf
        add(kParamCabMic, -0.35f * t);           // a miked cabinet: towards the cone centre
    }
    const float r = easyOffset(p[kParamEasyDrive]);
    if (r != 0.0f) {
        add(kParamPreampDrive, 0.60f * r);
        add(kParamCabinet, 0.45f * r);           // cabinet drive
        add(kParamPickupDistance, 0.25f * r);    // closer pickup: hotter, barkier
        add(kParamHammerHardness, 0.15f * r);
        add(kParamMasterVolume, -0.12f * r);     // keeps the level roughly where it was
    }
    const float m = easyModScale(p[kParamEasyMod]);
    if (m != 1.0f) {
        for (uint32_t id : {kParamTremoloDepth, kParamPhaserMix, kParamChorusMix, kParamWahAmount, kParamGapFmDepth}) {
            p[id] = std::clamp(p[id] * m, 0.0f, 1.0f);
        }
        for (uint32_t id : {kParamPressureAmt, kParamSlideAmt}) p[id] = std::clamp(0.5f + (p[id] - 0.5f) * m, 0.0f, 1.0f);
    }
}

// Parameter ids of the four EQ bands, shared by the DSP and the editor's curve
static constexpr uint32_t kEqFreqId[4]  = {kParamEqB1Freq, kParamEqB2Freq, kParamEqFreq, kParamEqB4Freq};
static constexpr uint32_t kEqGainId[4]  = {kParamEqB1Gain, kParamEqB2Gain, kParamEqGain, kParamEqB4Gain};
static constexpr uint32_t kEqQId[4]     = {kParamEqB1Q, kParamEqB2Q, kParamEqB3Q, kParamEqB4Q};
static constexpr uint32_t kEqShelfId[4] = {kParamEqB1Shelf, 0xFFFFFFFFu, 0xFFFFFFFFu, kParamEqB4Shelf};
inline int qualityOversample(float norm) { return 1 << stepIndex(norm, 2); }          // 1x, 2x, 4x

// Tempo sync divisions: step 0 = free running, otherwise the length in quarter-note beats
static constexpr float kSyncBeats[8] = {0.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 2.0f / 3.0f, 1.0f / 3.0f};
static constexpr const char* kSyncNames[8] = {"Free", "1/1", "1/2", "1/4", "1/8", "1/16", "1/4T", "1/8T"};
inline float syncBeats(float norm) { return kSyncBeats[stepIndex(norm, 7)]; }
// Frequency of a free rate (Hz) or, when a division is chosen, of that division at the host tempo
inline float syncedHz(float freeHz, float syncNorm, float bpm) {
    const float beats = syncBeats(syncNorm);
    return beats > 0.0f ? (bpm / 60.0f) / beats : freeHz;
}

// Echo length in seconds: free time, or the chosen division at the host tempo (folded to fit the delay line)
inline float syncedSeconds(float freeSec, float syncNorm, float bpm) {
    const float beats = syncBeats(syncNorm);
    if (beats <= 0.0f) return freeSec;
    float t = beats * 60.0f / std::max(20.0f, bpm);
    while (t > 1.0f) t *= 0.5f;
    return t;
}

enum ModSource : int { kSrcNone = 0, kSrcLfo1, kSrcLfo2, kSrcModWheel, kSrcPressure, kSrcVelocity, kSrcKey, kSrcEnvelope, kSrcRandom, kNumModSources };
static constexpr const char* kModSourceNames[kNumModSources] = {
    "Off", "LFO 1", "LFO 2", "Mod Wheel", "Pressure", "Velocity", "Key", "Output Env", "Random"};

// Parameters a modulation slot can steer (the stage selectors are deliberately absent)
static constexpr int kNumModDests = 40;
static constexpr uint32_t kModDestParam[kNumModDests] = {
    0xFFFFFFFFu,
    kParamHammerHardness, kParamHammerHysteresis, kParamStrikePosition, kParamTineDecay, kParamTineBell,
    kParamToneBarCoupling, kParamKeyNoise, kParamPickupDistance, kParamPickupSymmetry, kParamPickupResonance,
    kParamPreampDrive, kParamBassBoost, kParamTrebleBoost, kParamTremoloDepth, kParamTremoloRate,
    kParamPhaserMix, kParamPhaserRate, kParamChorusMix, kParamReverbMix, kParamReverbDecay,
    kParamVelocityCurve, kParamMasterVolume, kParamCabinet, kParamWahAmount, kParamWahSens,
    kParamDelayMix, kParamDelayTime, kParamDelayFeedback,
    kParamMaterial, kParamGapFmDepth, kParamGapFmRatio, kParamResMorph, kParamEqFreq, kParamEqGain,
    kParamEqB1Gain, kParamEqB2Gain, kParamEqB4Gain, kParamCabMic, kParamCabDistance};

inline uint32_t modDestParam(float norm) { return kModDestParam[stepIndex(norm, kNumModDests - 1)]; }
inline float modAmount(float norm) { return (norm - 0.5f) * 2.0f; }                    // -1 .. +1

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
        case kParamPickupCount: std::snprintf(out, size, "%d", pickupCountFromNorm(n)); return;
        case kParamPickupSpread: std::snprintf(out, size, "%.2f mm", pickupSpreadMm(n)); return;
        case kParamGapFmRatio: std::snprintf(out, size, "x%.2f", gapFmRatio(n)); return;
        case kParamEqFreq: std::snprintf(out, size, "%.0f Hz", eqFreqHz(n)); return;
        case kParamEqB1Freq: case kParamEqB2Freq: case kParamEqB4Freq: {
            const int b = id == kParamEqB1Freq ? 0 : id == kParamEqB2Freq ? 1 : 3;
            const float f = eqBandFreqHz(b, n);
            if (f >= 1000.0f) std::snprintf(out, size, "%.2f kHz", f / 1000.0f); else std::snprintf(out, size, "%.0f Hz", f);
            return;
        }
        case kParamEqGain: case kParamEqB1Gain: case kParamEqB2Gain: case kParamEqB4Gain:
            std::snprintf(out, size, "%+.1f dB", eqGainDb(n)); return;
        case kParamEqB1Q: case kParamEqB2Q: case kParamEqB3Q: case kParamEqB4Q: std::snprintf(out, size, "Q %.2f", eqQ(n)); return;
        case kParamEqB1Shelf: case kParamEqB4Shelf: std::snprintf(out, size, "%s", stepIndex(n, 1) ? "Shelf" : "Bell"); return;
        case kParamCompThresh: std::snprintf(out, size, "%.1f dB", compThreshDb(n)); return;
        case kParamCompRatio: std::snprintf(out, size, "%.1f:1", compRatio(n)); return;
        case kParamCompAttack: std::snprintf(out, size, "%.1f ms", compAttackMs(n)); return;
        case kParamCompRelease: std::snprintf(out, size, "%.0f ms", compReleaseMs(n)); return;
        case kParamCompKnee: std::snprintf(out, size, "%.1f dB", compKneeDb(n)); return;
        case kParamCompMakeup: std::snprintf(out, size, "%+.1f dB", compMakeupDb(n)); return;
        case kParamCompHpf: if (n < 0.01f) std::snprintf(out, size, "Off"); else std::snprintf(out, size, "%.0f Hz", compHpfHz(n)); return;
        case kParamCompDetect: std::snprintf(out, size, "%s", stepIndex(n, 1) ? "RMS" : "Peak"); return;
        case kParamCompAuto: case kParamLimOn: std::snprintf(out, size, "%s", stepIndex(n, 1) ? "On" : "Off"); return;
        case kParamLimCeil: std::snprintf(out, size, "%.1f dB", limCeilDb(n)); return;
        case kParamReverbType: {
            static const char* r[4] = {"Classic Hall", "Spring", "Studio Hall", "Plate"};
            std::snprintf(out, size, "%s", r[reverbAlgo(n)]);
            return;
        }
        case kParamMasterTune: std::snprintf(out, size, "%.1f Hz", masterTuneHz(n)); return;
        case kParamTranspose: std::snprintf(out, size, "%+d st", transposeSemis(n)); return;
        case kParamPolyphony: std::snprintf(out, size, "%d", polyphonyVoices(n)); return;
        case kParamReverbPreDelay: std::snprintf(out, size, "%.0f ms", preDelayMs(n)); return;
        case kParamChorusMode: std::snprintf(out, size, "%s", stepIndex(n, 1) ? "Tri" : "Dual"); return;
        case kParamGapFmExact: std::snprintf(out, size, "%s", stepIndex(n, 1) ? "Exact" : "Free"); return;
        case kParamWahOn: case kParamPhaserOn: case kParamTremOn: case kParamChorusOn:
        case kParamEchoOn: case kParamReverbOn: case kParamEqOn: case kParamCompOn:
            std::snprintf(out, size, "%s", switchOn(n) ? "On" : "Off"); return;
        case kParamWahReso: std::snprintf(out, size, "Q %.1f", wahQ(n)); return;
        case kParamWahMode: { const char* m[4] = {"Classic", "Low-pass", "Band-pass", "High-pass"}; std::snprintf(out, size, "%s", m[stepIndex(n, 3)]); return; }
        case kParamWahDir: std::snprintf(out, size, "%s", stepIndex(n, 1) ? "Down" : "Up"); return;
        case kParamWahFreq: case kParamPhaserCenter: {
            const float hz = id == kParamWahFreq ? wahBaseHz(n) : phaserCenterHz(n);
            if (hz >= 1000.0f) std::snprintf(out, size, "%.1f kHz", hz / 1000.0f);
            else std::snprintf(out, size, "%.0f Hz", hz);
            return;
        }
        case kParamWahRelease: std::snprintf(out, size, "%.0f ms", wahReleaseMs(n)); return;
        case kParamPhaserDepth: std::snprintf(out, size, "%.1f oct", phaserDepthOct(n)); return;
        case kParamPhaserFeedback: std::snprintf(out, size, "%+.0f%%", phaserFeedback(n) * 100.0f); return;
        case kParamPhaserStages: std::snprintf(out, size, "%d stages", phaserStages(n)); return;
        case kParamPhaserSpread: std::snprintf(out, size, "%.0f deg", n * 180.0f); return;
        case kParamChorusRate: std::snprintf(out, size, "%.2f Hz", chorusRateHz(n)); return;
        case kParamChorusDepth: std::snprintf(out, size, "%.0f%%", n * 200.0f); return;
        case kParamChorusDelay: std::snprintf(out, size, "%.1f ms", chorusDelayMs(n)); return;
        case kParamChorusWidth: std::snprintf(out, size, "%.0f%%", n * 200.0f); return;
        case kParamEasyDecay: case kParamEasyTone: case kParamEasyDrive: {
            const float v = easyOffset(n);
            if (std::fabs(v) < 0.01f) std::snprintf(out, size, "0");
            else std::snprintf(out, size, "%+.0f", v * 100.0f);
            return;
        }
        case kParamEasyMod: std::snprintf(out, size, "%.0f%%", easyModScale(n) * 100.0f); return;
        case kParamCabType: {
            static const char* c[10] = {"Speaker (200A)", "Suitcase 4x10", "Twin 2x12 Open", "Deluxe 1x12", "Closed 2x12",
                                        "Studio Condenser", "Ribbon Mic", "Hi-Fi Monitor", "Radio", "Lo-Fi"};
            std::snprintf(out, size, "%s", c[stepIndex(n, 9)]);
            return;
        }
        case kParamMaterial: {
            const char* nm = n < 0.17f ? "Steel" : n < 0.4f ? "Steel/Glass" : n < 0.6f ? "Glass" : n < 0.83f ? "Glass/Carbon" : "Carbon";
            std::snprintf(out, size, "%s", nm);
            return;
        }
        case kParamMpeMode: std::snprintf(out, size, "%s", stepIndex(n, 1) ? "On" : "Off"); return;
        case kParamBendRange: std::snprintf(out, size, "%d st", bendRangeSemis(n)); return;
        case kParamQuality: { const char* q[3] = {"Off", "2x", "4x"}; std::snprintf(out, size, "%s", q[stepIndex(n, 2)]); return; }
        case kParamTremSync: case kParamPhaserSync: case kParamEchoSync:
        case kParamLfo1Sync: case kParamLfo2Sync:
            std::snprintf(out, size, "%s", kSyncNames[stepIndex(n, 7)]); return;
        case kParamLfo1Rate: case kParamLfo2Rate: {
            const float hz = lfoRateHz(n);
            std::snprintf(out, size, hz >= 100.0f ? "%.0f Hz" : hz >= 10.0f ? "%.1f Hz" : "%.2f Hz", hz);
            return;
        }
        case kParamLfo1Shape: case kParamLfo2Shape: {
            const char* sh[5] = {"Sine", "Triangle", "Saw", "Square", "S&H"};
            std::snprintf(out, size, "%s", sh[stepIndex(n, 4)]);
            return;
        }
        case kParamMod1Src: case kParamMod2Src: case kParamMod3Src:
        case kParamMod4Src: case kParamMod5Src: case kParamMod6Src:
            std::snprintf(out, size, "%s", kModSourceNames[stepIndex(n, kNumModSources - 1)]); return;
        case kParamMod1Dst: case kParamMod2Dst: case kParamMod3Dst:
        case kParamMod4Dst: case kParamMod5Dst: case kParamMod6Dst: {
            const uint32_t d = modDestParam(n);
            std::snprintf(out, size, "%s", d < kNumParams ? kParamMetadata[d].name : "Off");
            return;
        }
        case kParamPressureAmt: case kParamSlideAmt:
        case kParamMod1Amt: case kParamMod2Amt: case kParamMod3Amt:
        case kParamMod4Amt: case kParamMod5Amt: case kParamMod6Amt:
            std::snprintf(out, size, "%+.0f%%", modAmount(n) * 100.0f); return;
        case kParamMasterVolume:
            if (n <= 0.001f) std::snprintf(out, size, "-inf dB");
            else std::snprintf(out, size, "%+.1f dB", 20.0f * std::log10(n / 0.78f));
            return;
        default: std::snprintf(out, size, "%.0f%%", n * 100.0f); return;
    }
}

} // namespace phs
