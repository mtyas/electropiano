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
static constexpr int kAnalyzerSize = 4096;  // output ring for the spectrum analyser
static constexpr int kGrHistory = 192;       // compressor gain-reduction history (about 2 s)
static constexpr uint32_t kGuiMidiRing = 512; // power of two: bursts of MPE pitch bends must not be dropped

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
    float hammerHold = 0.0f;                 // felt compression, held briefly so the contact is visible
    float ampIn = 0.0f, ampOut = 0.0f;       // amp stage peaks (for the saturation view)
    float meterL = 0.0f, meterR = 0.0f;      // output peak meters
    float gainReductionDb = 0.0f;            // bus compressor (<= 0)
    float compLevelDb = -90.0f;              // compressor detector level
    float limiterReductionDb = 0.0f;
    std::array<float, kGrHistory> grHistory{};
    uint32_t grPos = 0;
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
        resetExpression();
        envFollow_ = 0.0f;
        envAttack_ = 1.0f - std::exp(-1.0f / (0.005f * fs_));
        envRelease_ = 1.0f - std::exp(-1.0f / (0.120f * fs_));
    }

    void reset() {
        for (auto& v : voices_) {
            v.reset();
        }
        effects_.reset();
        sustainPedal_ = false;
        resetExpression();
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

    // Host-side modulation (CLAP parameter modulation): a non-destructive offset added to the knob
    void setHostModulation(uint32_t id, float amount) {
        if (id >= kNumParams) return;
        hostMod_[id] = amount;
        bool any = false;
        for (float m : hostMod_) any = any || m != 0.0f;
        hostModActive_ = any;
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
        const uint32_t nextW = (w + 1) & (kGuiMidiRing - 1);
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

    // Host tempo for the synced LFOs, tremolo, phaser and echo
    void setTempo(float bpm) {
        if (std::isfinite(bpm) && bpm > 0.0f) {
            bpm_ = std::clamp(bpm, 20.0f, 400.0f);
            effects_.setTempo(bpm_);
        }
    }

    // status carries the MIDI channel in its low nibble. For channel pressure (0xD0) data1 is the pressure.
    void handleMidiEvent(uint8_t status, uint8_t data1, uint8_t data2) {
        const uint8_t msg = status & 0xF0;
        const int channel = status & 0x0F;
        if (msg == 0x90 && data2 > 0) {
            const float vel = static_cast<float>(data2) / 127.0f;
            noteOn(static_cast<int>(data1), vel, channel);
        } else if (msg == 0x80 || (msg == 0x90 && data2 == 0)) {
            noteOff(static_cast<int>(data1), channel);
        } else if (msg == 0xE0) {
            const int v14 = (data1 & 0x7F) | ((data2 & 0x7F) << 7);
            setChannelBend(channel, static_cast<float>(v14 - 8192) / 8192.0f);
        } else if (msg == 0xD0) {
            setChannelPressure(channel, static_cast<float>(data1 & 0x7F) / 127.0f);
        } else if (msg == 0xA0) {
            setPolyPressure(static_cast<int>(data1 & 0x7F), static_cast<float>(data2 & 0x7F) / 127.0f);
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

            // MPE: CC74 on a member channel is that note's slide, not the global brightness mapping
            if (data1 == 74 && mpeEnabled() && channel != 0) {
                setChannelSlide(channel, static_cast<float>(data2) / 127.0f);
                return;
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

    // ---- Per-note expression ------------------------------------------------
    bool mpeEnabled() const { return stepIndex(targetParams_[kParamMpeMode].load(std::memory_order_relaxed), 1) == 1; }

    // Pitch bend. MPE: channel 0 (the master) bends every note by +-2 semitones and each member
    // channel bends only its own note by the Bend Range. Otherwise any channel is a global wheel.
    void setChannelBend(int channel, float norm) {
        if (mpeEnabled()) chBend_[channel & 15] = norm;
        else globalBend_ = norm;
        refreshExpression();
    }
    void setChannelPressure(int channel, float norm) {
        if (mpeEnabled()) chPressure_[channel & 15] = norm;
        else globalPressure_ = norm;
        refreshExpression();
    }
    void setChannelSlide(int channel, float norm) {
        chSlide_[channel & 15] = norm;
        refreshExpression();
    }
    void setPolyPressure(int note, float norm) {
        polyPressure_[note & 127] = norm;
        refreshExpression();
    }
    // CLAP / host note expressions, addressed by key: tuning in semitones, pressure and brightness 0..1
    void setNoteTuning(int note, float semitones) { noteTuning_[note & 127] = semitones; refreshExpression(); }
    void setNotePressure(int note, float norm) { polyPressure_[note & 127] = norm; refreshExpression(); }
    void setNoteBrightness(int note, float norm) { noteBrightness_[note & 127] = norm; refreshExpression(); }

    void noteOn(int midiNote, float velocity, int channel = 0) {
        if (midiNote < 0 || midiNote > 127) return;
        const int ch = mpeEnabled() ? (channel & 15) : 0;

        // Modulation sources that depend on the note: refresh them before the voice reads its parameters
        lastVelocity_ = velocity;
        lastKey_ = static_cast<float>(midiNote - 21) / 87.0f;
        randomValue_ = nextRandom();
        noteTuning_[midiNote] = 0.0f;
        noteBrightness_[midiNote] = -1.0f;
        polyPressure_[midiNote] = 0.0f;

        for (uint32_t i = 0; i < kNumParams; ++i) {
            smoothedParams_[i] = targetParams_[i].load(std::memory_order_relaxed);
        }
        float noteParams[kNumParams];
        std::memcpy(noteParams, smoothedParams_.data(), sizeof(noteParams));
        applyModulation(noteParams);
        if (hostModActive_) {
            for (uint32_t i = 0; i < kNumParams; ++i) {
                if (hostMod_[i] != 0.0f) noteParams[i] = std::clamp(noteParams[i] + hostMod_[i], 0.0f, 1.0f);
            }
        }

        const ExprValues ex = expressionFor(ch, midiNote);
        auto start = [&](PhsVoice& v) { v.noteOn(midiNote, velocity, noteParams, ch, ex.bend, ex.pressure, ex.slide); };

        // 1. Check if this MIDI note is already playing on this channel -> re-strike
        for (auto& v : voices_) {
            if (v.isActive() && v.getMidiNote() == midiNote && v.getChannel() == ch) {
                start(v);
                return;
            }
        }

        // 2. Find an idle voice
        for (auto& v : voices_) {
            if (!v.isActive()) {
                start(v);
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
        start(voices_[stealIdx]);
    }

    void noteOff(int midiNote, int channel = 0) {
        const int ch = mpeEnabled() ? (channel & 15) : 0;
        for (auto& v : voices_) {
            if (v.isActive() && v.getMidiNote() == midiNote && v.getChannel() == ch) {
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
            r = (r + 1) & (kGuiMidiRing - 1);
        }
        guiMidiRead_.store(r, std::memory_order_release);

        // Fetch target parameters
        float targets[kNumParams];
        for (uint32_t p = 0; p < kNumParams; ++p) {
            targets[p] = targetParams_[p].load(std::memory_order_relaxed);
        }
        smoothedParams_[kParamModelType] = targets[kParamModelType];
        prepareModulationBlock(targets);
        // Pitch-bend range or MPE mode may have changed: keep the sounding notes in step
        refreshExpression();

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

            const float* liveParams = smoothedParams_.data();
            if (modActive_ || hostModActive_) {
                advanceModulation();
                std::memcpy(modParams_, smoothedParams_.data(), sizeof(modParams_));
                applyModulation(modParams_);
                if (hostModActive_) {
                    for (uint32_t i = 0; i < kNumParams; ++i) {
                        if (hostMod_[i] != 0.0f) modParams_[i] = std::clamp(modParams_[i] + hostMod_[i], 0.0f, 1.0f);
                    }
                }
                liveParams = modParams_;
            }

            for (int vIdx = 0; vIdx < kMaxVoices; ++vIdx) {
                auto& v = voices_[vIdx];
                if (!v.isActive()) continue;

                voiceSum += v.processSample(liveParams);
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
            effects_.processStereoSample(voiceSum * kVoiceMixGain, liveParams, modWheel_, left, right);
            {
                const float level = std::fabs(left) + std::fabs(right);
                envFollow_ += (level > envFollow_ ? envAttack_ : envRelease_) * (level - envFollow_);
            }

            if (outL) outL[s] = left;
            if (outR) outR[s] = right;

            analyzer_[analyzerPos_ & (kAnalyzerSize - 1)] = 0.5f * (left + right);
            ++analyzerPos_;
            if ((++grCounter_ & 511) == 0) {
                scope_.grHistory[scope_.grPos % kGrHistory] = effects_.gainReductionDb() + effects_.limiterReductionDb();
                scope_.grPos = (scope_.grPos + 1) % kGrHistory;
            }

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
        hammerHold_ = std::max(leadXi, hammerHold_ * 0.85f);
        scope_.hammerHold = hammerHold_;
        scope_.activeVoices = activeCount;
        scope_.ampIn = effects_.ampInLevel();
        scope_.ampOut = effects_.ampOutLevel();
        scope_.meterL = effects_.meterL();
        scope_.meterR = effects_.meterR();
        scope_.gainReductionDb = effects_.gainReductionDb();
        scope_.compLevelDb = effects_.compLevelDb();
        scope_.limiterReductionDb = effects_.limiterReductionDb();
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
    // Recent output (mono) for the spectrum analyser: kAnalyzerSize samples ending at analyzerPos()
    const float* analyzerData() const { return analyzer_.data(); }
    uint32_t analyzerPos() const { return analyzerPos_; }
    float sampleRate() const { return fs_; }

    // Binary state serialization for VST3 / CLAP / AU
    struct SerializedState {
        uint32_t magic = 0x50485345; // 'PHSE'
        uint32_t version = 6;
        int32_t presetIdx = 0;
        float params[kNumParams]{};
        int8_t midiCc[kNumParams]{};
        uint8_t reserved[64]{};
    };

    void getState(SerializedState& out) const {
        out.magic = 0x50485345;
        out.version = 6;
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

        // Version 2 states carried 23 parameters, version 3 carried 32; later ones keep their defaults
        const uint32_t storedParams = ver >= 6 ? kNumParams : ver == 5 ? kNumParamsV5 : ver == 4 ? kNumParamsV4 : ver == 3 ? kNumParamsV3 : kNumParamsV2;
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

        // The MIDI CC table follows the parameter block, so its offset depends on the version
        const size_t ccOffset = sizeof(uint32_t) * 3 + sizeof(float) * storedParams;
        if (ver >= 3 && size >= ccOffset + storedParams) {
            const auto* cc = static_cast<const int8_t*>(data) + ccOffset;
            for (int i = 0; i < 128; ++i) {
                ccToParam_[i].store(-1, std::memory_order_relaxed);
            }
            for (uint32_t i = 0; i < kNumParams; ++i) {
                const int8_t c = i < storedParams ? cc[i] : static_cast<int8_t>(-1);
                paramToCc_[i].store(c, std::memory_order_relaxed);
                if (c >= 0) {
                    ccToParam_[static_cast<uint8_t>(c)].store(static_cast<int8_t>(i), std::memory_order_relaxed);
                }
            }
        }
        return true;
    }

private:
    // ---- Expression bookkeeping -------------------------------------------
    struct ExprValues { float bend = 0.0f, pressure = 0.0f, slide = 0.5f; };

    void resetExpression() {
        chBend_.fill(0.0f);
        chPressure_.fill(0.0f);
        chSlide_.fill(0.5f);
        polyPressure_.fill(0.0f);
        noteTuning_.fill(0.0f);
        noteBrightness_.fill(-1.0f);
        globalBend_ = globalPressure_ = 0.0f;
    }

    ExprValues expressionFor(int channel, int note) const {
        ExprValues e;
        const float range = static_cast<float>(bendRangeSemis(targetParams_[kParamBendRange].load(std::memory_order_relaxed)));
        if (mpeEnabled()) {
            e.bend = chBend_[0] * 2.0f + (channel != 0 ? chBend_[channel] * range : 0.0f);
            e.pressure = std::max(chPressure_[0], channel != 0 ? chPressure_[channel] : 0.0f);
            e.slide = channel != 0 ? chSlide_[channel] : 0.5f;
        } else {
            e.bend = globalBend_ * range;
            e.pressure = globalPressure_;
        }
        e.bend += noteTuning_[note];
        e.pressure = std::max(e.pressure, polyPressure_[note]);
        if (noteBrightness_[note] >= 0.0f) e.slide = noteBrightness_[note];
        return e;
    }

    void refreshExpression() {
        for (auto& v : voices_) {
            if (!v.isActive() || v.isReleasing()) continue;
            const ExprValues e = expressionFor(v.getChannel(), v.getMidiNote());
            v.setExpression(e.bend, e.pressure, e.slide);
        }
    }

    // ---- Modulation matrix ------------------------------------------------
    float nextRandom() {
        rng_ = rng_ * 1664525u + 1013904223u;
        return static_cast<float>(rng_ >> 8) * (1.0f / 16777216.0f);
    }

    // Reads the slot and LFO settings once per block
    void prepareModulationBlock(const float* targets) {
        numSlots_ = 0;
        for (int i = 0; i < kNumModSlots; ++i) {
            const uint32_t base = kFirstModSlotParam + static_cast<uint32_t>(i) * 3;
            const int src = stepIndex(targets[base], kNumModSources - 1);
            const uint32_t dst = modDestParam(targets[base + 1]);
            const float amt = modAmount(targets[base + 2]);
            if (src == kSrcNone || dst >= kNumParams || std::fabs(amt) < 0.002f) continue;
            slotSrc_[numSlots_] = src;
            slotDst_[numSlots_] = dst;
            slotAmt_[numSlots_] = amt;
            ++numSlots_;
        }
        modActive_ = numSlots_ > 0;
        lfoShape_[0] = stepIndex(targets[kParamLfo1Shape], 4);
        lfoShape_[1] = stepIndex(targets[kParamLfo2Shape], 4);
        lfoInc_[0] = syncedHz(lfoRateHz(targets[kParamLfo1Rate]), targets[kParamLfo1Sync], bpm_) / fs_;
        lfoInc_[1] = syncedHz(lfoRateHz(targets[kParamLfo2Rate]), targets[kParamLfo2Sync], bpm_) / fs_;
    }

    void advanceModulation() {
        for (int i = 0; i < 2; ++i) {
            lfoPhase_[i] += lfoInc_[i];
            if (lfoPhase_[i] >= 1.0f) {
                lfoPhase_[i] -= std::floor(lfoPhase_[i]);
                lfoHold_[i] = nextRandom() * 2.0f - 1.0f;
            }
            const float ph = lfoPhase_[i];
            switch (lfoShape_[i]) {
                case 0: lfoValue_[i] = std::sin(kTwoPi * ph); break;
                case 1: lfoValue_[i] = 1.0f - 4.0f * std::fabs(ph - 0.5f); break;
                case 2: lfoValue_[i] = 2.0f * ph - 1.0f; break;
                case 3: lfoValue_[i] = ph < 0.5f ? 1.0f : -1.0f; break;
                default: lfoValue_[i] = lfoHold_[i]; break;
            }
        }
    }

    float sourceValue(int src) const {
        switch (src) {
            case kSrcLfo1: return lfoValue_[0];
            case kSrcLfo2: return lfoValue_[1];
            case kSrcModWheel: return modWheel_;
            case kSrcPressure: return aftertouchLevel();
            case kSrcVelocity: return lastVelocity_;
            case kSrcKey: return std::clamp(lastKey_, 0.0f, 1.0f);
            case kSrcEnvelope: return std::clamp(envFollow_ * 2.0f, 0.0f, 1.0f);
            case kSrcRandom: return randomValue_;
            default: return 0.0f;
        }
    }

    float aftertouchLevel() const {
        float p = globalPressure_;
        for (float c : chPressure_) p = std::max(p, c);
        return p;
    }

    // Adds every active slot (source x amount) to its destination, then clamps what it touched
    void applyModulation(float* p) const {
        if (!modActive_) return;
        float delta[kNumParams] = {};
        for (int i = 0; i < numSlots_; ++i) delta[slotDst_[i]] += slotAmt_[i] * sourceValue(slotSrc_[i]);
        for (int i = 0; i < numSlots_; ++i) {
            const uint32_t d = slotDst_[i];
            p[d] = std::clamp(p[d] + delta[d], 0.0f, 1.0f);
            delta[d] = 0.0f; // a target shared by several slots is applied once
        }
    }

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
    float bpm_ = 120.0f;

    // Per-note expression
    std::array<float, 16> chBend_{}, chPressure_{}, chSlide_{};
    std::array<float, 128> polyPressure_{}, noteTuning_{}, noteBrightness_{};
    float globalBend_ = 0.0f, globalPressure_ = 0.0f;

    // Modulation matrix
    bool modActive_ = false;
    bool hostModActive_ = false;
    std::array<float, kNumParams> hostMod_{};
    int numSlots_ = 0;
    int slotSrc_[kNumModSlots]{};
    uint32_t slotDst_[kNumModSlots]{};
    float slotAmt_[kNumModSlots]{};
    int lfoShape_[2]{};
    float lfoInc_[2]{}, lfoPhase_[2]{}, lfoValue_[2]{}, lfoHold_[2]{};
    float lastVelocity_ = 0.0f, lastKey_ = 0.5f, randomValue_ = 0.0f;
    float envFollow_ = 0.0f, envAttack_ = 0.01f, envRelease_ = 0.0001f;
    uint32_t rng_ = 0x2545F491u;
    float modParams_[kNumParams]{};

    std::array<GuiMidiEvent, kGuiMidiRing> guiMidiRing_{};
    std::atomic<uint32_t> guiMidiWrite_{0};
    std::atomic<uint32_t> guiMidiRead_{0};

    std::atomic<uint64_t> activeNoteMaskLow_{0};
    std::atomic<uint64_t> activeNoteMaskHigh_{0};
    uint32_t scopeSampleCounter_ = 0;
    float hammerHold_ = 0.0f;
    std::array<float, kAnalyzerSize> analyzer_{};
    uint32_t analyzerPos_ = 0, grCounter_ = 0;
    ScopeSnapshot scope_{};
};

} // namespace phs
