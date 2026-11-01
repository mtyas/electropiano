#pragma once

#include "PhsParameters.hpp"
#include "PhsVoice.hpp"
#include "PhsEffects.hpp"
#include "PhsPresetManager.hpp"
#include <atomic>
#include <array>
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace phs {

static constexpr int kMaxVoices = 32;
static constexpr int kScopeRingSize = 512;

struct GuiMidiEvent {
    uint8_t status;
    uint8_t data1;
    uint8_t data2;
};

struct ScopeSnapshot {
    double totalHamiltonian = 0.0;
    double dissipatedPower = 0.0;
    float tipDisplacement = 0.0f;
    float tipVelocity = 0.0f;
    float hammerCompression = 0.0f;
    int activeVoices = 0;
    std::array<float, kScopeRingSize> waveL{};
    std::array<float, kScopeRingSize> waveR{};
    std::array<float, kScopeRingSize> phaseW{};
    std::array<float, kScopeRingSize> phaseV{};
    uint32_t writePos = 0;
};

struct UndoStep {
    std::string label;
    int presetIdx = 0;
    std::array<float, kNumParams> params{};
};

class PhsSynthEngine {
public:
    PhsSynthEngine() {
        for (int i = 0; i < 128; ++i) {
            ccToParam_[i].store(-1, std::memory_order_relaxed);
        }
        for (uint32_t i = 0; i < kNumParams; ++i) {
            paramToCc_[i].store(-1, std::memory_order_relaxed);
        }
        // Useful factory MIDI CC defaults
        paramToCc_[kParamPreampDrive].store(71, std::memory_order_relaxed); // Resonance/Drive
        ccToParam_[71].store(static_cast<int8_t>(kParamPreampDrive), std::memory_order_relaxed);
        paramToCc_[kParamPickupDistance].store(74, std::memory_order_relaxed); // Brightness/Distance
        ccToParam_[74].store(static_cast<int8_t>(kParamPickupDistance), std::memory_order_relaxed);
        paramToCc_[kParamTremoloDepth].store(91, std::memory_order_relaxed); // Effects 1
        ccToParam_[91].store(static_cast<int8_t>(kParamTremoloDepth), std::memory_order_relaxed);
        paramToCc_[kParamChorusMix].store(93, std::memory_order_relaxed); // Chorus
        ccToParam_[93].store(static_cast<int8_t>(kParamChorusMix), std::memory_order_relaxed);
        paramToCc_[kParamReverbMix].store(92, std::memory_order_relaxed); // Reverb
        ccToParam_[92].store(static_cast<int8_t>(kParamReverbMix), std::memory_order_relaxed);

        loadPreset(0, false);
        for (uint32_t i = 0; i < kNumParams; ++i) {
            smoothedParams_[i] = targetParams_[i].load(std::memory_order_relaxed);
        }
        prepare(48000.0f);
    }

    void prepare(float sampleRate) {
        fs_ = std::max(8000.0f, sampleRate);
        smoothAlpha_ = 1.0f - std::exp(-1.0f / (0.008f * fs_)); // 8ms parameter smoothing
        for (auto& v : voices_) {
            v.prepare(fs_);
        }
        effects_.prepare(fs_);
        sustainPedal_ = false;
        modWheel_ = 0.0f;
        pitchBendSemis_ = 0.0f;
    }

    void reset() {
        for (auto& v : voices_) {
            v.reset();
        }
        effects_.reset();
        sustainPedal_ = false;
        activeNoteMaskLow_.store(0, std::memory_order_relaxed);
        activeNoteMaskHigh_.store(0, std::memory_order_relaxed);
    }

    PhsPresetManager& getPresetManager() { return presetManager_; }
    const PhsPresetManager& getPresetManager() const { return presetManager_; }

    void loadPreset(int presetIdx, bool pushUndo = true) {
        const auto* preset = presetManager_.getPreset(presetIdx);
        if (!preset) return;

        if (pushUndo) {
            pushUndoState("Preset: " + preset->name);
        }

        currentPreset_.store(presetIdx, std::memory_order_relaxed);
        for (uint32_t i = 0; i < kNumParams; ++i) {
            targetParams_[i].store(preset->values[i], std::memory_order_relaxed);
        }
    }

    int getCurrentPreset() const {
        return currentPreset_.load(std::memory_order_relaxed);
    }

    void setParamNormalized(uint32_t id, float normVal) {
        if (id < kNumParams) {
            normVal = std::clamp(normVal, 0.0f, 1.0f);
            targetParams_[id].store(normVal, std::memory_order_relaxed);
        }
    }

    float getParamNormalized(uint32_t id) const {
        if (id < kNumParams) {
            return targetParams_[id].load(std::memory_order_relaxed);
        }
        return 0.0f;
    }

    // =========================================================================
    // UNDO / REDO HISTORY SYSTEM
    // =========================================================================
    void pushUndoState(const std::string& label = "Edit") {
        UndoStep step;
        step.label = label;
        step.presetIdx = currentPreset_.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < kNumParams; ++i) {
            step.params[i] = targetParams_[i].load(std::memory_order_relaxed);
        }

        if (!undoStack_.empty()) {
            bool identical = true;
            for (uint32_t i = 0; i < kNumParams; ++i) {
                if (std::fabs(undoStack_.back().params[i] - step.params[i]) > 1e-4f) {
                    identical = false;
                    break;
                }
            }
            if (identical) return;
        }

        undoStack_.push_back(step);
        if (undoStack_.size() > 64) {
            undoStack_.erase(undoStack_.begin());
        }
        redoStack_.clear();
    }

    bool canUndo() const { return !undoStack_.empty(); }
    bool canRedo() const { return !redoStack_.empty(); }

    bool undo() {
        if (undoStack_.empty()) return false;

        UndoStep current;
        current.label = "Current";
        current.presetIdx = currentPreset_.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < kNumParams; ++i) {
            current.params[i] = targetParams_[i].load(std::memory_order_relaxed);
        }
        redoStack_.push_back(current);

        UndoStep prev = undoStack_.back();
        undoStack_.pop_back();

        currentPreset_.store(prev.presetIdx, std::memory_order_relaxed);
        for (uint32_t i = 0; i < kNumParams; ++i) {
            targetParams_[i].store(prev.params[i], std::memory_order_relaxed);
            smoothedParams_[i] = prev.params[i];
        }
        return true;
    }

    bool redo() {
        if (redoStack_.empty()) return false;

        UndoStep current;
        current.label = "Current";
        current.presetIdx = currentPreset_.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < kNumParams; ++i) {
            current.params[i] = targetParams_[i].load(std::memory_order_relaxed);
        }
        undoStack_.push_back(current);

        UndoStep next = redoStack_.back();
        redoStack_.pop_back();

        currentPreset_.store(next.presetIdx, std::memory_order_relaxed);
        for (uint32_t i = 0; i < kNumParams; ++i) {
            targetParams_[i].store(next.params[i], std::memory_order_relaxed);
            smoothedParams_[i] = next.params[i];
        }
        return true;
    }

    // =========================================================================
    // MIDI LEARN SYSTEM
    // =========================================================================
    void startMidiLearn(int32_t paramId) {
        if (paramId >= 0 && paramId < static_cast<int32_t>(kNumParams)) {
            learningParamId_.store(paramId, std::memory_order_relaxed);
        }
    }

    void stopMidiLearn() {
        learningParamId_.store(-1, std::memory_order_relaxed);
    }

    int32_t getLearningParamId() const {
        return learningParamId_.load(std::memory_order_relaxed);
    }

    void clearMidiMapping(uint32_t paramId) {
        if (paramId < kNumParams) {
            int8_t cc = paramToCc_[paramId].load(std::memory_order_relaxed);
            if (cc >= 0) {
                ccToParam_[static_cast<uint8_t>(cc)].store(-1, std::memory_order_relaxed);
            }
            paramToCc_[paramId].store(-1, std::memory_order_relaxed);
        }
    }

    int8_t getParamCc(uint32_t paramId) const {
        if (paramId < kNumParams) {
            return paramToCc_[paramId].load(std::memory_order_relaxed);
        }
        return -1;
    }

    // Queue a MIDI event from GUI (lock-free SPSC ring buffer)
    void pushGuiMidiEvent(uint8_t status, uint8_t data1, uint8_t data2) {
        const uint32_t w = guiMidiWrite_.load(std::memory_order_relaxed);
        const uint32_t nextW = (w + 1) & 63;
        if (nextW != guiMidiRead_.load(std::memory_order_acquire)) {
            guiMidiRing_[w] = {status, data1, data2};
            guiMidiWrite_.store(nextW, std::memory_order_release);
        }
    }

    void setSustain(bool active) {
        sustainPedal_ = active;
        for (auto& v : voices_) {
            if (v.isActive()) {
                v.setSustainPedal(sustainPedal_);
            }
        }
    }

    bool isSustainActive() const {
        return sustainPedal_;
    }

    void handleMidiEvent(uint8_t status, uint8_t data1, uint8_t data2) {
        const uint8_t msg = status & 0xF0;
        if (msg == 0x90 && data2 > 0) {
            const float vel = static_cast<float>(data2) / 127.0f;
            noteOn(static_cast<int>(data1), vel);
        } else if (msg == 0x80 || (msg == 0x90 && data2 == 0)) {
            noteOff(static_cast<int>(data1));
        } else if (msg == 0xB0) {
            if (data1 == 64) {
                sustainPedal_ = (data2 >= 64);
                for (auto& v : voices_) {
                    if (v.isActive()) {
                        v.setSustainPedal(sustainPedal_);
                    }
                }
            } else if (data1 == 1) {
                modWheel_ = static_cast<float>(data2) / 127.0f;
            } else if (data1 == 120 || data1 == 123) {
                for (auto& v : voices_) {
                    v.reset();
                }
            }

            // Handle MIDI Learn or Mapped Controller
            const int32_t learning = learningParamId_.load(std::memory_order_relaxed);
            if (learning >= 0 && learning < static_cast<int32_t>(kNumParams)) {
                if (data1 != 64 && data1 != 120 && data1 != 123) {
                    const int8_t prevParam = ccToParam_[data1].load(std::memory_order_relaxed);
                    if (prevParam >= 0 && prevParam < static_cast<int8_t>(kNumParams)) {
                        paramToCc_[prevParam].store(-1, std::memory_order_relaxed);
                    }
                    const int8_t prevCc = paramToCc_[learning].load(std::memory_order_relaxed);
                    if (prevCc >= 0) {
                        ccToParam_[static_cast<uint8_t>(prevCc)].store(-1, std::memory_order_relaxed);
                    }
                    paramToCc_[learning].store(static_cast<int8_t>(data1), std::memory_order_relaxed);
                    ccToParam_[data1].store(static_cast<int8_t>(learning), std::memory_order_relaxed);
                    learningParamId_.store(-1, std::memory_order_relaxed);
                }
            } else {
                const int8_t mappedParam = ccToParam_[data1].load(std::memory_order_relaxed);
                if (mappedParam >= 0 && mappedParam < static_cast<int8_t>(kNumParams)) {
                    setParamNormalized(mappedParam, static_cast<float>(data2) / 127.0f);
                }
            }
        }
    }

    void noteOn(int midiNote, float velocity) {
        if (midiNote < 0 || midiNote > 127) return;

        for (uint32_t i = 0; i < kNumParams; ++i) {
            smoothedParams_[i] = targetParams_[i].load(std::memory_order_relaxed);
        }

        // 1. Check if this MIDI note is already playing -> re-strike
        for (auto& v : voices_) {
            if (v.isActive() && v.getMidiNote() == midiNote) {
                v.noteOn(midiNote, velocity, smoothedParams_.data());
                return;
            }
        }

        // 2. Find an idle voice
        for (auto& v : voices_) {
            if (!v.isActive()) {
                v.noteOn(midiNote, velocity, smoothedParams_.data());
                return;
            }
        }

        // 3. Steal lowest-energy voice
        int stealIdx = 0;
        double minScore = 1e30;
        for (int i = 0; i < kMaxVoices; ++i) {
            const double energy = voices_[i].getTelemetry().totalHamiltonian;
            const double score = (voices_[i].isReleasing() ? 0.1 : 1.0) * energy;
            if (score < minScore) {
                minScore = score;
                stealIdx = i;
            }
        }
        voices_[stealIdx].noteOn(midiNote, velocity, smoothedParams_.data());
    }

    void noteOff(int midiNote) {
        for (auto& v : voices_) {
            if (v.isActive() && v.getMidiNote() == midiNote) {
                v.noteOff(sustainPedal_);
            }
        }
    }

    void processBlock(float* outL, float* outR, int numSamples) {
        // Drain any GUI MIDI events first
        uint32_t r = guiMidiRead_.load(std::memory_order_relaxed);
        const uint32_t w = guiMidiWrite_.load(std::memory_order_acquire);
        while (r != w) {
            const auto ev = guiMidiRing_[r];
            handleMidiEvent(ev.status, ev.data1, ev.data2);
            r = (r + 1) & 63;
        }
        guiMidiRead_.store(r, std::memory_order_release);

        // Fetch target parameters
        float targets[kNumParams];
        for (uint32_t p = 0; p < kNumParams; ++p) {
            targets[p] = targetParams_[p].load(std::memory_order_relaxed);
        }
        smoothedParams_[kParamModelType] = targets[kParamModelType];

        double blockH = 0.0;
        double blockDiss = 0.0;
        float leadW = 0.0f;
        float leadV = 0.0f;
        float leadXi = 0.0f;
        int activeCount = 0;
        uint64_t maskLow = 0;
        uint64_t maskHigh = 0;

        for (int s = 0; s < numSamples; ++s) {
            for (uint32_t p = 1; p < kNumParams; ++p) {
                smoothedParams_[p] += smoothAlpha_ * (targets[p] - smoothedParams_[p]);
            }

            float voiceSum = 0.0f;
            double sampleH = 0.0;
            double sampleDiss = 0.0;
            float newestW = 0.0f;
            float newestV = 0.0f;
            float maxXi = 0.0f;
            uint64_t minAge = UINT64_MAX;
            int countThisSample = 0;

            for (int vIdx = 0; vIdx < kMaxVoices; ++vIdx) {
                auto& v = voices_[vIdx];
                if (!v.isActive()) continue;

                voiceSum += v.processSample(smoothedParams_.data());
                if (!v.isActive()) continue;

                ++countThisSample;
                const int note = v.getMidiNote();
                if (note >= 0 && note < 64) {
                    maskLow |= (1ULL << note);
                } else if (note >= 64 && note < 128) {
                    maskHigh |= (1ULL << (note - 64));
                }

                const auto& tel = v.getTelemetry();
                sampleH += tel.totalHamiltonian;
                sampleDiss += tel.dissipatedPower;
                if (tel.hammerCompression > maxXi) {
                    maxXi = tel.hammerCompression;
                }
                if (v.getAge() < minAge) {
                    minAge = v.getAge();
                    newestW = tel.tipDisplacement;
                    newestV = tel.tipVelocity;
                }
            }

            float left = 0.0f;
            float right = 0.0f;
            effects_.processStereoSample(voiceSum * kVoiceMixGain, smoothedParams_.data(), modWheel_, left, right);

            if (outL) outL[s] = left;
            if (outR) outR[s] = right;

            if ((scopeSampleCounter_++ & 1) == 0) {
                const uint32_t pos = scope_.writePos % kScopeRingSize;
                scope_.waveL[pos] = left;
                scope_.waveR[pos] = right;
                scope_.phaseW[pos] = newestW;
                scope_.phaseV[pos] = newestV;
                scope_.writePos = (pos + 1) % kScopeRingSize;
            }

            blockH = sampleH;
            blockDiss = sampleDiss;
            leadW = newestW;
            leadV = newestV;
            leadXi = maxXi;
            activeCount = countThisSample;
        }

        scope_.totalHamiltonian = blockH;
        scope_.dissipatedPower = blockDiss;
        scope_.tipDisplacement = leadW;
        scope_.tipVelocity = leadV;
        scope_.hammerCompression = leadXi;
        scope_.activeVoices = activeCount;
        activeNoteMaskLow_.store(maskLow, std::memory_order_relaxed);
        activeNoteMaskHigh_.store(maskHigh, std::memory_order_relaxed);
    }

    bool isNoteActive(int midiNote) const {
        if (midiNote >= 0 && midiNote < 64) {
            return (activeNoteMaskLow_.load(std::memory_order_relaxed) & (1ULL << midiNote)) != 0;
        } else if (midiNote >= 64 && midiNote < 128) {
            return (activeNoteMaskHigh_.load(std::memory_order_relaxed) & (1ULL << (midiNote - 64))) != 0;
        }
        return false;
    }

    const ScopeSnapshot& getScopeSnapshot() const { return scope_; }

    // Binary state serialization for VST3 / CLAP / AU
    struct SerializedState {
        uint32_t magic = 0x50485345; // 'PHSE'
        uint32_t version = 3;
        int32_t presetIdx = 0;
        float params[kNumParams]{};
        int8_t midiCc[kNumParams]{};
        uint8_t reserved[64]{};
    };

    void getState(SerializedState& out) const {
        out.magic = 0x50485345;
        out.version = 3;
        out.presetIdx = currentPreset_.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < kNumParams; ++i) {
            out.params[i] = targetParams_[i].load(std::memory_order_relaxed);
            out.midiCc[i] = paramToCc_[i].load(std::memory_order_relaxed);
        }
        std::memset(out.reserved, 0, sizeof(out.reserved));
    }

    bool setState(const void* data, size_t size) {
        if (!data || size < sizeof(uint32_t) * 3) return false;
        const auto* header = static_cast<const uint32_t*>(data);
        if (header[0] != 0x50485345) return false;

        const uint32_t ver = header[1];
        const int32_t pIdx = static_cast<const int32_t*>(static_cast<const void*>(header + 2))[0];
        currentPreset_.store(std::clamp(pIdx, 0, presetManager_.getPresetCount() - 1), std::memory_order_relaxed);

        // Version 2 states carried 23 parameters; later ones keep their defaults
        const uint32_t storedParams = ver >= 3 ? kNumParams : kNumParamsV2;
        if (size < sizeof(uint32_t) * 3 + sizeof(float) * storedParams) return false;
        const float* pData = reinterpret_cast<const float*>(header + 3);
        for (uint32_t i = 0; i < kNumParams; ++i) {
            float v = kParamMetadata[i].defaultValue;
            if (i < storedParams && std::isfinite(pData[i])) v = std::clamp(pData[i], 0.0f, 1.0f);
            targetParams_[i].store(v, std::memory_order_relaxed);
            smoothedParams_[i] = v;
        }
        if (ver < 3) {
            // A v2 state's Model choice implied its stages
            const int model = stepIndex(targetParams_[kParamModelType].load(std::memory_order_relaxed), 3);
            targetParams_[kParamResonator].store(model == 2 ? 1.0f : 0.0f, std::memory_order_relaxed);
            targetParams_[kParamPickupType].store(model == 2 ? 1.0f : 0.0f, std::memory_order_relaxed);
            targetParams_[kParamAmpType].store(model == 2 ? 1.0f : 0.0f, std::memory_order_relaxed);
        }

        if (ver >= 3 && size >= sizeof(SerializedState)) {
            const auto* st = static_cast<const SerializedState*>(data);
            for (int i = 0; i < 128; ++i) {
                ccToParam_[i].store(-1, std::memory_order_relaxed);
            }
            for (uint32_t i = 0; i < kNumParams; ++i) {
                const int8_t cc = st->midiCc[i];
                paramToCc_[i].store(cc, std::memory_order_relaxed);
                if (cc >= 0) {
                    ccToParam_[static_cast<uint8_t>(cc)].store(static_cast<int8_t>(i), std::memory_order_relaxed);
                }
            }
        }
        return true;
    }

private:
    float fs_ = 48000.0f;
    float smoothAlpha_ = 0.005f;

    std::array<std::atomic<float>, kNumParams> targetParams_{};
    std::array<float, kNumParams> smoothedParams_{};
    std::atomic<int> currentPreset_{0};
    PhsPresetManager presetManager_;

    // Undo / Redo history
    std::vector<UndoStep> undoStack_;
    std::vector<UndoStep> redoStack_;

    // MIDI Learn
    std::atomic<int32_t> learningParamId_{-1};
    std::array<std::atomic<int8_t>, kNumParams> paramToCc_{};
    std::array<std::atomic<int8_t>, 128> ccToParam_{};

    std::array<PhsVoice, kMaxVoices> voices_{};
    PhsEffectsChain effects_;

    bool sustainPedal_ = false;
    float modWheel_ = 0.0f;
    float pitchBendSemis_ = 0.0f;

    std::array<GuiMidiEvent, 64> guiMidiRing_{};
    std::atomic<uint32_t> guiMidiWrite_{0};
    std::atomic<uint32_t> guiMidiRead_{0};

    std::atomic<uint64_t> activeNoteMaskLow_{0};
    std::atomic<uint64_t> activeNoteMaskHigh_{0};
    uint32_t scopeSampleCounter_ = 0;
    ScopeSnapshot scope_{};
};

} // namespace phs
