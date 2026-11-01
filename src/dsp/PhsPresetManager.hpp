#pragma once

#include "PhsParameters.hpp"
#include <string>
#include <vector>
#include <array>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <cstdlib>
#include <initializer_list>

namespace phs {

enum class PresetCategory : int {
    All = 0,
    RhodesVintage,
    Wurlitzer,
    DynoMod80s,
    AmbientFX,
    User
};

inline const char* getCategoryName(PresetCategory cat) {
    switch (cat) {
        case PresetCategory::All: return "All Categories";
        case PresetCategory::RhodesVintage: return "Rhodes Vintage";
        case PresetCategory::Wurlitzer: return "Wurlitzer Reeds";
        case PresetCategory::DynoMod80s: return "Dyno & Mod 80s";
        case PresetCategory::AmbientFX: return "Ambient & FX";
        case PresetCategory::User: return "User Presets";
    }
    return "General";
}

struct PresetItem {
    std::string name;
    PresetCategory category;
    std::string author;
    std::string description;
    bool isUserPreset = false;
    bool isFavorite = false;
    std::string filePath; // For user presets on disk
    std::array<float, kNumParams> values{};
};

// -----------------------------------------------------------------------------
// Factory presets: each starts from the parameter defaults, takes its instrument's
// stages (resonator / pickup / amp), then names only the controls it changes.
// -----------------------------------------------------------------------------
enum class Instrument { MkI, Dyno, Wurli200A, Hybrid };

struct PresetOverride {
    uint32_t id;
    float value;
};

inline PresetItem makeFactoryPreset(const char* name, PresetCategory category, const char* description,
                                    Instrument instrument, std::initializer_list<PresetOverride> overrides) {
    PresetItem item;
    item.name = name;
    item.category = category;
    item.author = "mtyas";
    item.description = description;
    for (uint32_t i = 0; i < kNumParams; ++i) item.values[i] = kParamMetadata[i].defaultValue;
    item.values[kParamModelType] = static_cast<float>(instrument) / 3.0f;
    const bool reed = instrument == Instrument::Wurli200A;
    const bool electrostatic = instrument == Instrument::Wurli200A || instrument == Instrument::Hybrid;
    item.values[kParamResonator] = reed ? 1.0f : 0.0f;
    item.values[kParamPickupType] = electrostatic ? 1.0f : 0.0f;
    item.values[kParamAmpType] = electrostatic ? 1.0f : 0.0f;
    if (instrument == Instrument::Wurli200A || instrument == Instrument::Hybrid) {
        item.values[kParamTremoloDepth] = 0.5f;   // OpenWurli default vibrato depth
        item.values[kParamTremoloRate] = 0.4819f; // the Twin-T's own 5.56 Hz
        item.values[kParamReverbMix] = 0.0f;
    }
    for (const auto& o : overrides) item.values[o.id] = o.value;
    return item;
}

inline std::vector<PresetItem> buildFactoryPresets() {
    using C = PresetCategory;
    using I = Instrument;
    return {
        // --- Rhodes ---
        makeFactoryPreset("1973 Suitcase Mk I", C::RhodesVintage,
            "Suitcase Mk I with stereo auto-pan, warm tone bars and a little bark on hard accents.", I::MkI,
            {{kParamTremoloDepth, 0.42f}, {kParamTremoloRate, 0.40f}, {kParamPreampDrive, 0.35f}, {kParamCabinet, 0.45f},
             {kParamBassBoost, 0.56f}, {kParamReverbMix, 0.20f}}),
        makeFactoryPreset("1977 Stage Mk I (DI)", C::RhodesVintage,
            "Passive Stage piano straight into the desk: bell up front, woody decay, audible felt.", I::MkI,
            {{kParamTremoloDepth, 0.0f}, {kParamPreampDrive, 0.25f}, {kParamTrebleBoost, 0.55f}, {kParamKeyNoise, 0.38f},
             {kParamReverbMix, 0.10f}}),
        makeFactoryPreset("Silver Top '71 Sparkle", C::RhodesVintage,
            "Bright early tines struck near the clamp; glassy chime over a round fundamental.", I::MkI,
            {{kParamTineBell, 0.62f}, {kParamStrikePosition, 0.20f}, {kParamPickupSymmetry, 0.30f}, {kParamTrebleBoost, 0.62f},
             {kParamTremoloDepth, 0.25f}, {kParamReverbMix, 0.22f}}),
        makeFactoryPreset("Mellow Ballad Mk I", C::RhodesVintage,
            "Soft felt, long sustain and a rolled-off top for slow ballads.", I::MkI,
            {{kParamHammerHardness, 0.30f}, {kParamHammerHysteresis, 0.55f}, {kParamTineDecay, 0.78f}, {kParamTrebleBoost, 0.42f},
             {kParamVelocityCurve, 0.45f}, {kParamTremoloDepth, 0.30f}, {kParamTremoloRate, 0.32f}, {kParamReverbMix, 0.32f}}),
        makeFactoryPreset("Barking Mk I Overdrive", C::RhodesVintage,
            "Tines close to the pickup into a pushed amp and speaker: growls when you dig in.", I::MkI,
            {{kParamPickupDistance, 0.80f}, {kParamPickupSymmetry, 0.55f}, {kParamPreampDrive, 0.70f}, {kParamHammerHardness, 0.70f},
             {kParamCabinet, 0.75f}, {kParamTremoloDepth, 0.0f}, {kParamReverbMix, 0.14f}}),
        makeFactoryPreset("Bell Octave Tine", C::RhodesVintage,
            "Tine centred on the pickup (Lv near 0): the Falaize-Helie flux law turns it into an octave bell.", I::MkI,
            {{kParamPickupSymmetry, 0.0f}, {kParamTineBell, 0.55f}, {kParamTremoloDepth, 0.2f}, {kParamReverbMix, 0.28f}, {kParamMasterVolume, 0.95f}}),
        makeFactoryPreset("Herbie Funk Wah-Pan", C::RhodesVintage,
            "Envelope filter into a gently driven suitcase; quacks with your dynamics.", I::MkI,
            {{kParamWahAmount, 0.65f}, {kParamWahSens, 0.55f}, {kParamTremoloDepth, 0.30f}, {kParamPreampDrive, 0.45f},
             {kParamHammerHardness, 0.60f}, {kParamReverbMix, 0.12f}}),
        makeFactoryPreset("Phase-90 Stage Funk", C::RhodesVintage,
            "Close pickups and a slow optical phaser.", I::MkI,
            {{kParamPhaserMix, 0.62f}, {kParamPhaserRate, 0.24f}, {kParamPickupDistance, 0.66f}, {kParamPreampDrive, 0.45f},
             {kParamTremoloDepth, 0.0f}, {kParamReverbMix, 0.16f}, {kParamMasterVolume, 0.66f}}),

        // --- Wurlitzer (OpenWurli) ---
        makeFactoryPreset("200A Factory (OpenWurli)", C::Wurlitzer,
            "OpenWurli's calibrated 200A: reed, electrostatic pickup, preamp, vibrato and power amp at their defaults.", I::Wurli200A, {}),
        makeFactoryPreset("200A Supertramp Stage", C::Wurlitzer,
            "Volume up, vibrato on, a touch of cabinet and room.", I::Wurli200A,
            {{kParamTremoloDepth, 0.38f}, {kParamPreampDrive, 0.55f}, {kParamCabinet, 0.45f}, {kParamReverbMix, 0.15f}}),
        makeFactoryPreset("200A Dry Session", C::Wurlitzer,
            "No vibrato, tight cabinet, small room: sits in a mix.", I::Wurli200A,
            {{kParamTremoloDepth, 0.0f}, {kParamCabinet, 0.25f}, {kParamReverbMix, 0.12f}}),
        makeFactoryPreset("200A Cranked Bark", C::Wurlitzer,
            "Volume pot wide open into the power amp rails, harder hammers, full cabinet.", I::Wurli200A,
            {{kParamPreampDrive, 0.62f}, {kParamHammerHardness, 0.65f}, {kParamCabinet, 0.85f}, {kParamTrebleBoost, 0.58f},
             {kParamTremoloDepth, 0.0f}}),
        makeFactoryPreset("200A Vibrato Lounge", C::Wurlitzer,
            "Deep LDR vibrato inside the preamp loop, longer sustain, plate reverb.", I::Wurli200A,
            {{kParamTremoloDepth, 0.85f}, {kParamTineDecay, 0.72f}, {kParamReverbMix, 0.30f}, {kParamReverbDecay, 0.55f}}),
        makeFactoryPreset("200A Tape Echo", C::Wurlitzer,
            "Reeds into a warm tape echo.", I::Wurli200A,
            {{kParamDelayMix, 0.30f}, {kParamDelayTime, 0.58f}, {kParamDelayFeedback, 0.42f}, {kParamReverbMix, 0.20f},
             {kParamTremoloDepth, 0.25f}}),
        makeFactoryPreset("200A Psychedelic Phase", C::Wurlitzer,
            "Vibrato plus a sweeping phaser.", I::Wurli200A,
            {{kParamPhaserMix, 0.70f}, {kParamPhaserRate, 0.20f}, {kParamTremoloDepth, 0.45f}, {kParamReverbMix, 0.18f}}),

        // --- Dyno / 80s ---
        makeFactoryPreset("Dyno-My-Piano LA Bell", C::DynoMod80s,
            "Hard tips near the clamp, centred voicing, Dyno pickups and lush chorus.", I::Dyno,
            {{kParamHammerHardness, 0.62f}, {kParamHammerHysteresis, 0.22f}, {kParamStrikePosition, 0.19f}, {kParamTineDecay, 0.75f},
             {kParamTineBell, 0.78f}, {kParamToneBarCoupling, 0.65f}, {kParamPickupDistance, 0.62f}, {kParamPickupSymmetry, 0.22f},
             {kParamPickupResonance, 0.65f}, {kParamBassBoost, 0.60f}, {kParamTrebleBoost, 0.70f}, {kParamTremoloDepth, 0.15f},
             {kParamChorusMix, 0.55f}, {kParamReverbMix, 0.32f}, {kParamReverbDecay, 0.58f}}),
        makeFactoryPreset("80s Ballad Chorus EP", C::DynoMod80s,
            "Chorus, long hall and a soft echo tail.", I::Dyno,
            {{kParamTineBell, 0.70f}, {kParamChorusMix, 0.70f}, {kParamReverbMix, 0.42f}, {kParamReverbDecay, 0.65f},
             {kParamDelayMix, 0.15f}, {kParamDelayTime, 0.62f}, {kParamDelayFeedback, 0.30f}, {kParamTremoloDepth, 0.0f}}),
        makeFactoryPreset("Dyno Tine Shimmer", C::DynoMod80s,
            "Strong tone-bar beating and chime with a slow phaser.", I::Dyno,
            {{kParamToneBarCoupling, 0.75f}, {kParamTineBell, 0.70f}, {kParamPhaserMix, 0.28f}, {kParamPhaserRate, 0.15f},
             {kParamReverbMix, 0.30f}, {kParamTremoloDepth, 0.0f}}),
        makeFactoryPreset("Glass Tine", C::DynoMod80s,
            "Very hard, undamped hammers and an octave-leaning pickup: almost FM-like.", I::Dyno,
            {{kParamTineBell, 0.90f}, {kParamHammerHardness, 0.82f}, {kParamHammerHysteresis, 0.15f}, {kParamPickupSymmetry, 0.10f},
             {kParamChorusMix, 0.40f}, {kParamReverbMix, 0.35f}, {kParamTremoloDepth, 0.0f}}),

        // --- Hybrid / FX ---
        makeFactoryPreset("Hybrid: Tine into 200A", C::AmbientFX,
            "A Rhodes tine on the Wurlitzer's electrostatic comb, through the 200A preamp and vibrato.", I::Hybrid,
            {{kParamTremoloDepth, 0.35f}, {kParamCabinet, 0.3f}, {kParamReverbMix, 0.15f}}),
        makeFactoryPreset("Hybrid: Reed into Suitcase", C::AmbientFX,
            "Wurlitzer reeds on a Rhodes magnetic pickup, panning through a suitcase.", I::MkI,
            {{kParamResonator, 1.0f}, {kParamTremoloDepth, 0.45f}, {kParamCabinet, 0.4f}, {kParamReverbMix, 0.18f}}),
        makeFactoryPreset("Reed on Magnetic, 200A Amp", C::AmbientFX,
            "Reeds through a magnetic pickup into the 200A circuit.", I::Wurli200A,
            {{kParamPickupType, 0.0f}, {kParamPickupDistance, 0.65f}, {kParamTremoloDepth, 0.3f}}),
        makeFactoryPreset("Tine on Comb, Suitcase", C::AmbientFX,
            "Rhodes tine on an electrostatic comb with suitcase auto-pan.", I::MkI,
            {{kParamPickupType, 1.0f}, {kParamTremoloDepth, 0.4f}, {kParamReverbMix, 0.2f}}),
        makeFactoryPreset("Mu-Tron Funk Machine", C::AmbientFX,
            "Deep envelope filter, punchy hammers, dry and tight.", I::MkI,
            {{kParamWahAmount, 0.92f}, {kParamWahSens, 0.62f}, {kParamPreampDrive, 0.42f}, {kParamHammerHardness, 0.66f},
             {kParamTremoloDepth, 0.0f}, {kParamReverbMix, 0.08f}}),
        makeFactoryPreset("Dub Echo Chamber", C::AmbientFX,
            "Long, darkening tape repeats and a big room.", I::MkI,
            {{kParamDelayMix, 0.45f}, {kParamDelayTime, 0.72f}, {kParamDelayFeedback, 0.68f}, {kParamReverbMix, 0.38f},
             {kParamReverbDecay, 0.70f}, {kParamTrebleBoost, 0.42f}, {kParamTremoloDepth, 0.0f}}),
        makeFactoryPreset("Ambient Tine Pad", C::AmbientFX,
            "Soft hammers, endless sustain, chorus, echo and a huge reverb.", I::Dyno,
            {{kParamHammerHardness, 0.22f}, {kParamTineDecay, 0.95f}, {kParamReverbMix, 0.65f}, {kParamReverbDecay, 0.88f},
             {kParamChorusMix, 0.50f}, {kParamDelayMix, 0.25f}, {kParamDelayTime, 0.80f}, {kParamDelayFeedback, 0.50f},
             {kParamTremoloDepth, 0.0f}}),
        makeFactoryPreset("Sub-Bark Bass EP", C::AmbientFX,
            "Heavy bass, close pickups and grit for left-hand bass lines.", I::MkI,
            {{kParamBassBoost, 0.78f}, {kParamPickupDistance, 0.75f}, {kParamPreampDrive, 0.55f}, {kParamHammerHardness, 0.70f},
             {kParamTremoloDepth, 0.0f}, {kParamReverbMix, 0.08f}, {kParamMasterVolume, 0.45f}}),
        makeFactoryPreset("Lo-Fi Cassette Keys", C::AmbientFX,
            "Dull top, small speaker, wobbly short echo and chorus.", I::Wurli200A,
            {{kParamTrebleBoost, 0.30f}, {kParamCabinet, 0.95f}, {kParamDelayMix, 0.22f}, {kParamDelayTime, 0.35f},
             {kParamDelayFeedback, 0.30f}, {kParamChorusMix, 0.35f}, {kParamPreampDrive, 0.52f}}),
    };
}

static const std::vector<PresetItem> kExpandedFactoryPresets = buildFactoryPresets();

class PhsPresetManager {
public:
    PhsPresetManager() {
        presets_ = kExpandedFactoryPresets;
        ensureUserPresetDirectory();
        scanUserPresets();
    }

    static std::string getUserPresetsDirectory() {
#if defined(_WIN32)
        const char* appData = std::getenv("APPDATA");
        if (appData) {
            return std::string(appData) + "\\mtyas\\electropiano\\Presets";
        }
        return "presets";
#elif defined(__APPLE__)
        const char* home = std::getenv("HOME");
        if (home) {
            return std::string(home) + "/Library/Application Support/mtyas/electropiano/Presets";
        }
        return "presets";
#else
        const char* xdgData = std::getenv("XDG_DATA_HOME");
        if (xdgData) {
            return std::string(xdgData) + "/mtyas/electropiano/Presets";
        }
        const char* home = std::getenv("HOME");
        if (home) {
            return std::string(home) + "/.local/share/mtyas/electropiano/Presets";
        }
        return "presets";
#endif
    }

    void ensureUserPresetDirectory() {
        try {
            const std::string dir = getUserPresetsDirectory();
            std::filesystem::create_directories(dir);
        } catch (...) {}
    }

    void scanUserPresets() {
        // Remove existing user presets
        presets_.erase(
            std::remove_if(presets_.begin(), presets_.end(), [](const PresetItem& item) {
                return item.isUserPreset;
            }),
            presets_.end()
        );

        const std::string dirPath = getUserPresetsDirectory();
        try {
            if (std::filesystem::exists(dirPath)) {
                for (const auto& entry : std::filesystem::directory_iterator(dirPath)) {
                    if (entry.is_regular_file() && entry.path().extension() == ".phspreset") {
                        PresetItem item;
                        if (loadPresetFile(entry.path().string(), item)) {
                            item.isUserPreset = true;
                            item.filePath = entry.path().string();
                            presets_.push_back(item);
                        }
                    }
                }
            }
        } catch (...) {}
    }

    const std::vector<PresetItem>& getPresets() const { return presets_; }

    int getPresetCount() const { return static_cast<int>(presets_.size()); }

    const PresetItem* getPreset(int index) const {
        if (index >= 0 && index < static_cast<int>(presets_.size())) {
            return &presets_[index];
        }
        return nullptr;
    }

    bool saveUserPreset(const std::string& name,
                        PresetCategory category,
                        const std::string& author,
                        const std::string& description,
                        const float* params) {
        ensureUserPresetDirectory();
        if (name.empty() || !params) return false;

        // Sanitize filename
        std::string safeName = name;
        for (char& c : safeName) {
            if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
                c = '_';
            }
        }

        const std::string filePath = getUserPresetsDirectory() + "/" + safeName + ".phspreset";
        std::ofstream out(filePath);
        if (!out) return false;

        out << "# PyPHS Electric Piano Preset\n";
        out << "Version=1\n";
        out << "Name=" << name << "\n";
        out << "Category=" << static_cast<int>(category) << "\n";
        out << "Author=" << (author.empty() ? "User" : author) << "\n";
        out << "Description=" << description << "\n";
        out << "Values=";
        for (uint32_t i = 0; i < kNumParams; ++i) {
            out << params[i] << (i + 1 < kNumParams ? "," : "\n");
        }
        out.close();

        scanUserPresets();
        return true;
    }

    bool deleteUserPreset(int index) {
        if (index < 0 || index >= static_cast<int>(presets_.size())) return false;
        const auto& item = presets_[index];
        if (!item.isUserPreset || item.filePath.empty()) return false;

        try {
            if (std::filesystem::exists(item.filePath)) {
                std::filesystem::remove(item.filePath);
            }
        } catch (...) {
            return false;
        }

        scanUserPresets();
        return true;
    }

private:
    std::vector<PresetItem> presets_;

    bool loadPresetFile(const std::string& path, PresetItem& outItem) {
        std::ifstream in(path);
        if (!in) return false;

        outItem = {};
        outItem.isUserPreset = true;
        outItem.filePath = path;
        outItem.category = PresetCategory::User;
        for (uint32_t i = 0; i < kNumParams; ++i) outItem.values[i] = kParamMetadata[i].defaultValue;
        uint32_t valuesRead = 0;

        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;

            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);

            if (key == "Name") {
                outItem.name = val;
            } else if (key == "Category") {
                int catId = std::atoi(val.c_str());
                outItem.category = (catId >= 0 && catId <= 5) ? static_cast<PresetCategory>(catId) : PresetCategory::User;
            } else if (key == "Author") {
                outItem.author = val;
            } else if (key == "Description") {
                outItem.description = val;
            } else if (key == "Values") {
                std::stringstream ss(val);
                std::string token;
                uint32_t pIdx = 0;
                while (std::getline(ss, token, ',') && pIdx < kNumParams) {
                    outItem.values[pIdx++] = std::stof(token);
                }
                valuesRead = pIdx;
            }
        }
        if (valuesRead > 0 && valuesRead <= kNumParamsV2) {
            // Saved before the stage selectors existed: the Model choice implied them
            const float stage = stepIndex(outItem.values[kParamModelType], 3) == 2 ? 1.0f : 0.0f;
            outItem.values[kParamResonator] = outItem.values[kParamPickupType] = outItem.values[kParamAmpType] = stage;
        }
        if (outItem.name.empty()) {
            outItem.name = std::filesystem::path(path).stem().string();
        }
        return true;
    }
};

} // namespace phs
