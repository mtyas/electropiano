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

// 16 Authentic Factory Presets across all categories
static const std::vector<PresetItem> kExpandedFactoryPresets = {
    // --- Rhodes Vintage ---
    {
        "1973 Suitcase Rhodes Mk I",
        PresetCategory::RhodesVintage,
        "PyPHS Laboratory",
        "Classic 1973 Suitcase with stereo auto-pan vibrato, warm tone-bar sustain, and gentle pickup bark on hard accents.",
        false, false, "",
        {{
            0.0f,  // Model: Rhodes Mk I
            0.48f, // Hammer Hardness
            0.35f, // Hammer Hysteresis
            0.18f, // Strike Position
            0.68f, // Tine Decay
            0.45f, // Tine Bell
            0.58f, // ToneBar Coupling
            0.28f, // Key Noise
            0.54f, // Pickup Distance
            0.38f, // Pickup Symmetry
            0.45f, // Coil Resonance
            0.32f, // Preamp Drive
            0.58f, // Bass Boost
            0.52f, // Treble Boost
            0.42f, // Tremolo Depth
            0.40f, // Tremolo Rate
            0.00f, // Phaser Mix
            0.25f, // Phaser Rate
            0.00f, // Chorus Mix
            0.22f, // Reverb Mix
            0.45f, // Reverb Decay
            0.50f, // Velocity Curve
            0.82f  // Master Volume
        }}
    },
    {
        "1977 Stage Piano (Clean DI)",
        PresetCategory::RhodesVintage,
        "PyPHS Laboratory",
        "Pure direct injection passive Stage Mk I. Bell tone up front with natural woody decay and acoustic hammer felt.",
        false, false, "",
        {{
            0.0f,  0.42f, 0.40f, 0.20f, 0.65f, 0.40f, 0.52f, 0.35f,
            0.48f, 0.35f, 0.40f, 0.18f, 0.50f, 0.50f, 0.00f, 0.25f,
            0.00f, 0.25f, 0.00f, 0.12f, 0.32f, 0.50f, 0.85f
        }}
    },
    {
        "1971 Silver Top Sparkle",
        PresetCategory::RhodesVintage,
        "PyPHS Laboratory",
        "Early teardrop felt hammer with distinctive percussive chime and slightly loose tone-bar resonance.",
        false, false, "",
        {{
            0.0f,  0.55f, 0.32f, 0.16f, 0.70f, 0.56f, 0.62f, 0.32f,
            0.58f, 0.42f, 0.50f, 0.38f, 0.55f, 0.60f, 0.28f, 0.45f,
            0.00f, 0.25f, 0.00f, 0.26f, 0.50f, 0.52f, 0.80f
        }}
    },
    {
        "Herbie Funk 1974 Wah-Pan",
        PresetCategory::RhodesVintage,
        "PyPHS Laboratory",
        "Hot preamp driving a sweeping 4-stage optical phaser with wide stereo tremolo for Head Hunters funk grooves.",
        false, false, "",
        {{
            0.0f,  0.64f, 0.26f, 0.17f, 0.60f, 0.48f, 0.55f, 0.30f,
            0.62f, 0.45f, 0.55f, 0.52f, 0.62f, 0.58f, 0.45f, 0.60f,
            0.55f, 0.38f, 0.00f, 0.20f, 0.40f, 0.55f, 0.80f
        }}
    },

    // --- Wurlitzer Reeds ---
    {
        "Wurlitzer 200A Reedy Bark",
        PresetCategory::Wurlitzer,
        "PyPHS Laboratory",
        "Signature 1974 200A electric reed piano. Punchy steel reed envelope with deep tube bark on fortissimo accents.",
        false, false, "",
        {{
            2.0f,  0.68f, 0.28f, 0.24f, 0.42f, 0.22f, 0.35f, 0.35f,
            0.66f, 0.52f, 0.55f, 0.48f, 0.46f, 0.58f, 0.45f, 0.52f,
            0.00f, 0.25f, 0.00f, 0.18f, 0.35f, 0.55f, 0.80f
        }}
    },
    {
        "Super-tramp 200A Stage",
        PresetCategory::Wurlitzer,
        "PyPHS Laboratory",
        "Tight electrostatic pickup gap, pushed preamp saturation, and built-in optical tremolo for iconic rock intros.",
        false, false, "",
        {{
            2.0f,  0.72f, 0.24f, 0.22f, 0.45f, 0.25f, 0.38f, 0.30f,
            0.70f, 0.55f, 0.58f, 0.56f, 0.44f, 0.64f, 0.60f, 0.55f,
            0.00f, 0.25f, 0.00f, 0.15f, 0.30f, 0.58f, 0.78f
        }}
    },
    {
        "Wurlitzer 140B Tube Velvet",
        PresetCategory::Wurlitzer,
        "PyPHS Laboratory",
        "Early 1960s all-tube 140B model. Softer felt hammers, smoother pickup response, and warm tube compression.",
        false, false, "",
        {{
            2.0f,  0.46f, 0.36f, 0.26f, 0.48f, 0.18f, 0.40f, 0.32f,
            0.52f, 0.40f, 0.42f, 0.40f, 0.56f, 0.48f, 0.35f, 0.42f,
            0.00f, 0.25f, 0.00f, 0.25f, 0.48f, 0.48f, 0.82f
        }}
    },
    {
        "Queen 'You're My Best Friend'",
        PresetCategory::Wurlitzer,
        "PyPHS Laboratory",
        "Cranky 200A driven through a vintage stereo chorus pedal with warm tube bass push.",
        false, false, "",
        {{
            2.0f,  0.65f, 0.28f, 0.23f, 0.44f, 0.24f, 0.36f, 0.32f,
            0.64f, 0.50f, 0.52f, 0.50f, 0.54f, 0.60f, 0.15f, 0.45f,
            0.00f, 0.25f, 0.45f, 0.22f, 0.40f, 0.52f, 0.80f
        }}
    },

    // --- Dyno & Mod 80s ---
    {
        "Dyno-My-Piano LA 80s Bell",
        PresetCategory::DynoMod80s,
        "PyPHS Laboratory",
        "Hot-rodded Chuck Monte Dyno preamp mod. Tight pickup proximity, high treble boost, and lush bucket-brigade chorus.",
        false, false, "",
        {{
            1.0f,  0.64f, 0.22f, 0.14f, 0.76f, 0.80f, 0.65f, 0.24f,
            0.62f, 0.22f, 0.65f, 0.38f, 0.64f, 0.75f, 0.22f, 0.35f,
            0.00f, 0.25f, 0.58f, 0.32f, 0.58f, 0.48f, 0.80f
        }}
    },
    {
        "Crystal Ballad Ball",
        PresetCategory::DynoMod80s,
        "PyPHS Laboratory",
        "The quintessential David Foster / 1980s power ballad electric piano with extended crystalline chime overtones.",
        false, false, "",
        {{
            1.0f,  0.60f, 0.24f, 0.13f, 0.82f, 0.86f, 0.70f, 0.20f,
            0.58f, 0.20f, 0.68f, 0.32f, 0.60f, 0.80f, 0.18f, 0.30f,
            0.00f, 0.25f, 0.65f, 0.42f, 0.68f, 0.45f, 0.78f
        }}
    },
    {
        "LA Studio Session Mk II",
        PresetCategory::DynoMod80s,
        "PyPHS Laboratory",
        "High-definition 1980 flat-top Mk II with active EQ modification and fast transient attack for dense West Coast mixes.",
        false, false, "",
        {{
            1.0f,  0.68f, 0.20f, 0.15f, 0.72f, 0.72f, 0.62f, 0.26f,
            0.65f, 0.25f, 0.60f, 0.42f, 0.58f, 0.70f, 0.28f, 0.42f,
            0.00f, 0.25f, 0.40f, 0.25f, 0.45f, 0.50f, 0.82f
        }}
    },
    {
        "R&B Neo Soul Butter",
        PresetCategory::DynoMod80s,
        "PyPHS Laboratory",
        "Warm, scooped mid response with gentle pickup tape-saturation and silky stereo modulation for slow jams.",
        false, false, "",
        {{
            1.0f,  0.46f, 0.34f, 0.18f, 0.75f, 0.58f, 0.56f, 0.30f,
            0.52f, 0.32f, 0.52f, 0.36f, 0.68f, 0.62f, 0.32f, 0.38f,
            0.00f, 0.25f, 0.35f, 0.30f, 0.52f, 0.48f, 0.82f
        }}
    },

    // --- Ambient & FX ---
    {
        "Deep Space Nebula Rhodes",
        PresetCategory::AmbientFX,
        "PyPHS Laboratory",
        "Cavernous hall plate reverb paired with a slow 4-stage optical phaser and soaring singing tines.",
        false, false, "",
        {{
            0.0f,  0.48f, 0.32f, 0.18f, 0.85f, 0.64f, 0.72f, 0.20f,
            0.55f, 0.36f, 0.52f, 0.25f, 0.56f, 0.62f, 0.25f, 0.28f,
            0.48f, 0.16f, 0.42f, 0.55f, 0.85f, 0.48f, 0.75f
        }}
    },
    {
        "Vaporwave Tape Cassette",
        PresetCategory::AmbientFX,
        "PyPHS Laboratory",
        "Warbled vibrato mod, warm tube drive, and lo-fi Baxandall filtering replicating worn cassette tapes.",
        false, false, "",
        {{
            2.0f,  0.55f, 0.32f, 0.22f, 0.50f, 0.30f, 0.42f, 0.45f,
            0.60f, 0.45f, 0.50f, 0.62f, 0.60f, 0.42f, 0.58f, 0.65f,
            0.35f, 0.45f, 0.48f, 0.32f, 0.52f, 0.52f, 0.78f
        }}
    },
    {
        "Port-Hamiltonian Strumming",
        PresetCategory::AmbientFX,
        "PyPHS Laboratory",
        "Maximum tone-bar coupled energy transfer creating an acoustic 12-string resonance harp texture.",
        false, false, "",
        {{
            3.0f,  0.58f, 0.26f, 0.16f, 0.88f, 0.75f, 0.88f, 0.22f,
            0.56f, 0.32f, 0.60f, 0.30f, 0.54f, 0.68f, 0.20f, 0.32f,
            0.25f, 0.30f, 0.50f, 0.40f, 0.72f, 0.50f, 0.80f
        }}
    },
    {
        "Sub-Bark Bass Electric",
        PresetCategory::AmbientFX,
        "PyPHS Laboratory",
        "Heavy bass boost with hard hammer strike position for funky electric piano bass lines.",
        false, false, "",
        {{
            0.0f,  0.72f, 0.22f, 0.22f, 0.62f, 0.35f, 0.45f, 0.35f,
            0.68f, 0.48f, 0.52f, 0.52f, 0.85f, 0.45f, 0.20f, 0.45f,
            0.00f, 0.25f, 0.00f, 0.16f, 0.35f, 0.60f, 0.82f
        }}
    }
};

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
            return std::string(appData) + "\\PyPHSElectricPiano\\Presets";
        }
        return "presets";
#elif defined(__APPLE__)
        const char* home = std::getenv("HOME");
        if (home) {
            return std::string(home) + "/Library/Application Support/PyPHSElectricPiano/Presets";
        }
        return "presets";
#else
        const char* xdgData = std::getenv("XDG_DATA_HOME");
        if (xdgData) {
            return std::string(xdgData) + "/PyPHSElectricPiano/Presets";
        }
        const char* home = std::getenv("HOME");
        if (home) {
            return std::string(home) + "/.local/share/PyPHSElectricPiano/Presets";
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
            }
        }
        if (outItem.name.empty()) {
            outItem.name = std::filesystem::path(path).stem().string();
        }
        return true;
    }
};

} // namespace phs
