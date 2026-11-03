#include "PhsEditor.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <complex>
#include <fstream>
#include <vector>

namespace phs::gui {

namespace {

// -----------------------------------------------------------------------------
// Theme: one neutral graphite palette with a single warm amber accent
// -----------------------------------------------------------------------------
constexpr Color kBgTop       = {0.090f, 0.098f, 0.114f, 1.0f};
constexpr Color kBgBottom    = {0.055f, 0.060f, 0.071f, 1.0f};
constexpr Color kHeaderBg    = {0.043f, 0.047f, 0.055f, 1.0f};
constexpr Color kPanelBg     = {0.110f, 0.118f, 0.137f, 1.0f};
constexpr Color kPanelTop    = {0.122f, 0.130f, 0.151f, 1.0f};
constexpr Color kPanelBorder = {0.190f, 0.204f, 0.231f, 1.0f};
constexpr Color kInsetBg     = {0.035f, 0.039f, 0.047f, 1.0f};
constexpr Color kHairline    = {0.160f, 0.172f, 0.196f, 1.0f};
constexpr Color kTextPrimary = {0.900f, 0.910f, 0.925f, 1.0f};
constexpr Color kTextMuted   = {0.560f, 0.585f, 0.625f, 1.0f};
constexpr Color kTextDim     = {0.400f, 0.420f, 0.455f, 1.0f};
constexpr Color kAccent      = {0.965f, 0.659f, 0.271f, 1.0f};
constexpr Color kLearn       = {0.380f, 0.760f, 1.000f, 1.0f};
constexpr Color kArcTrack    = {0.070f, 0.075f, 0.090f, 1.0f};
constexpr Color kBlack       = {0.0f, 0.0f, 0.0f, 1.0f};

constexpr TextStyle kBrand{17.0f, Weight::Semibold, -0.2f};
constexpr TextStyle kBrandBy{10.0f, Weight::Medium};
constexpr TextStyle kModelName{8.5f, Weight::Medium, 1.0f};
constexpr TextStyle kSegment{11.0f, Weight::Medium};
constexpr TextStyle kSegmentOn{11.0f, Weight::Semibold};
constexpr TextStyle kPresetName{11.5f, Weight::Medium};
constexpr TextStyle kButton{10.0f, Weight::Semibold, 0.4f};
constexpr TextStyle kPanelTitle{9.0f, Weight::Semibold, 1.2f};
constexpr TextStyle kKnobLabel{10.5f, Weight::Medium};
constexpr TextStyle kKnobValue{10.0f, Weight::Medium, 0.0f, true};
constexpr TextStyle kLearning{8.5f, Weight::Semibold, 0.8f};
constexpr TextStyle kCcTag{7.5f, Weight::Semibold, 0.3f};
constexpr TextStyle kSustain{8.0f, Weight::Semibold, 0.8f};
constexpr TextStyle kWheelLabel{7.5f, Weight::Semibold, 0.6f};
constexpr TextStyle kStripOption{10.5f, Weight::Medium};
constexpr TextStyle kStripOptionOn{10.5f, Weight::Semibold};
constexpr TextStyle kModalTitle{15.0f, Weight::Semibold};
constexpr TextStyle kListItem{11.5f, Weight::Regular};
constexpr TextStyle kListItemOn{11.5f, Weight::Semibold};
constexpr TextStyle kModalButton{11.0f, Weight::Medium};
constexpr TextStyle kAboutBy{10.5f, Weight::Medium};
constexpr TextStyle kAboutHead{11.5f, Weight::Semibold};
constexpr TextStyle kAboutBody{10.5f, Weight::Regular};
constexpr TextStyle kLinkTitle{10.5f, Weight::Semibold};
constexpr TextStyle kLinkSub{9.0f, Weight::Regular};
constexpr TextStyle kTextField{12.0f, Weight::Regular};

constexpr float kArcInset = 5.5f; // value arc sits this far outside the knob body
constexpr Color kBandColor[4] = {{0.38f, 0.76f, 1.0f, 1.0f}, {0.45f, 0.85f, 0.55f, 1.0f}, kAccent, {0.95f, 0.45f, 0.62f, 1.0f}};

// Smoothed magnitude spectrum (dBFS) of the last `n` output samples at kPts log-spaced points, 20 Hz .. 20 kHz
void computeSpectrumDb(const float* ring, uint32_t pos, int n, float fs, float* outDb, int pts) {
    std::vector<std::complex<float>> a(n);
    for (int i = 0; i < n; ++i) {
        const float w = 0.5f - 0.5f * std::cos(6.2831853f * i / (n - 1));
        a[i] = ring[(pos + static_cast<uint32_t>(i)) & static_cast<uint32_t>(n - 1)] * w;
    }
    for (int i = 1, j = 0; i < n; ++i) { // bit reversal
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        const std::complex<float> wl(std::cos(6.2831853f / len), -std::sin(6.2831853f / len));
        for (int i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (int k = 0; k < len / 2; ++k) {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    for (int j = 0; j < pts; ++j) {
        const float f0 = 20.0f * std::pow(1000.0f, static_cast<float>(j) / pts), f1 = 20.0f * std::pow(1000.0f, static_cast<float>(j + 1) / pts);
        const int b0 = std::clamp(static_cast<int>(f0 * n / fs), 1, n / 2 - 1);
        const int b1 = std::clamp(static_cast<int>(std::ceil(f1 * n / fs)), b0, n / 2 - 1);
        float m = 0.0f;
        for (int b = b0; b <= b1; ++b) m = std::max(m, std::abs(a[b]));
        outDb[j] = 20.0f * std::log10(m * 4.0f / n + 1e-9f);
    }
}
constexpr float kPi = 3.14159265358979f;

// Keyboard geometry (61 keys, C2..C7)
constexpr int kKbStartNote = 36;
constexpr int kKbNumKeys = 61;
constexpr int kKbNumNaturals = 36;
constexpr bool kIsBlackKey[12] = {false, true, false, true, false, false, true, false, true, false, true, false};

enum MenuAction { kMenuLearn, kMenuForget, kMenuReset };
enum Icon { kIconNone, kIconUndo, kIconRedo, kIconInfo };

} // namespace

Editor::Editor(PhsSynthEngine* engine, EditorHost host) : engine_(engine), host_(std::move(host)) {
    std::fill(std::begin(eqSpec_), std::end(eqSpec_), -120.0f); // analyser starts from silence
    layout();
}

// =============================================================================
// LAYOUT
// Canvas 900 x 776, origin bottom-left. 16 px outer margin, 8 px panel gutters.
//   header 726..776 | signal chain 678..718 | page bar 640..670 | resonator + pickup 404..634
//   amp / filter / phaser 268..396 | chorus / echo / reverb / output 132..260 | keys 12..124
// =============================================================================
void Editor::layout() {
    knobs_.clear();
    panels_.clear();
    const float W = kEditorWidth, H = kEditorHeight;
    const float margin = 16.0f, gutter = 8.0f, contentW = W - 2.0f * margin;

    // 1. Header
    const float hy = H - 39.0f, hh = 28.0f;
    modelSelector_ = {196.0f, hy, 272.0f, hh};
    for (int i = 0; i < 4; ++i) modelButtons_[i] = {modelSelector_.x + i * 68.0f, hy, 68.0f, hh};
    presetPrev_ = {482.0f, hy, 26.0f, hh};
    presetBox_ = {508.0f, hy, 196.0f, hh};
    presetNext_ = {704.0f, hy, 26.0f, hh};
    undoBtn_ = {742.0f, hy, 28.0f, hh};
    redoBtn_ = {772.0f, hy, 28.0f, hh};
    midiBtn_ = {810.0f, hy, 42.0f, hh};
    aboutBtn_ = {856.0f, hy, 28.0f, hh};
    pageBarY_ = 640.0f; // between the signal chain and the instrument panels

    // 2. Signal chain: three two-way stage switches with arrows between
    strip_ = {margin, H - 98.0f, contentW, 40.0f};
    {
        const float labelW = 78.0f, segW = 92.0f, arrowW = 34.0f;
        const float groupW = labelW + 2.0f * segW;
        float x = strip_.x + (contentW - (3.0f * groupW + 2.0f * arrowW)) * 0.5f;
        for (int g = 0; g < 3; ++g) {
            stageLabelX_[g] = x;
            for (int k = 0; k < 2; ++k) stageRects_[g][k] = {x + labelW + k * segW, strip_.y + 7.0f, segW, 26.0f};
            x += groupW + arrowW;
        }
    }

    // 3. Resonator + pickup panels: 7 equal columns split 4 | 3
    auto cellHeight = [](float r) { return 32.0f + 2.0f * (r + kArcInset); };
    auto addKnob = [&](uint32_t pid, float cx, float y, float w, float h, float r, bool bipolar) {
        knobs_.push_back({pid, {cx - w * 0.5f, y, w, h}, r, bipolar});
    };
    const float pianoY = 404.0f, pianoH = 230.0f;
    const float colW = (contentW - gutter) / 7.0f;
    const Rect resonator{margin, pianoY, colW * 4.0f, pianoH};
    const Rect pickup{resonator.maxX() + gutter, pianoY, colW * 3.0f, pianoH};
    panels_.push_back({resonator, PanelKind::Resonator});
    panels_.push_back({pickup, PanelKind::Pickup});

    const float bigR = 23.0f, bigCellH = cellHeight(bigR), cellW = colW - 12.0f;
    const float row1Y = pianoY + pianoH - 30.0f - bigCellH;
    const float row2Y = row1Y - bigCellH - 6.0f;
    auto resCol = [&](int i) { return resonator.x + colW * (i + 0.5f); };
    auto pickCol = [&](float i) { return pickup.x + colW * (i + 0.5f); };
    addKnob(kParamHammerHardness, resCol(0), row1Y, cellW, bigCellH, bigR, false);
    addKnob(kParamHammerHysteresis, resCol(1), row1Y, cellW, bigCellH, bigR, false);
    addKnob(kParamStrikePosition, resCol(2), row1Y, cellW, bigCellH, bigR, false);
    addKnob(kParamTineDecay, resCol(3), row1Y, cellW, bigCellH, bigR, false);
    addKnob(kParamTineBell, resCol(0), row2Y, cellW, bigCellH, bigR, false);
    addKnob(kParamToneBarCoupling, resCol(1), row2Y, cellW, bigCellH, bigR, false);
    addKnob(kParamKeyNoise, resCol(2), row2Y, cellW, bigCellH, bigR, false);
    addKnob(kParamVelocityCurve, resCol(3), row2Y, cellW, bigCellH, bigR, false);
    addKnob(kParamPickupDistance, pickCol(0), row1Y, cellW, bigCellH, bigR, false);
    addKnob(kParamPickupSymmetry, pickCol(1), row1Y, cellW, bigCellH, bigR, false);
    addKnob(kParamPickupResonance, pickCol(2), row1Y, cellW, bigCellH, bigR, false);
    addKnob(kParamBassBoost, pickCol(0.5f), row2Y, cellW, bigCellH, bigR, true);
    addKnob(kParamTrebleBoost, pickCol(1.5f), row2Y, cellW, bigCellH, bigR, true);

    // 4. Two effects rows, equal columns per row
    const float smallR = 19.0f, smallCellH = cellHeight(smallR), fxH = 128.0f;
    auto buildRow = [&](float y, std::initializer_list<std::pair<PanelKind, std::vector<uint32_t>>> modules) {
        int cols = 0;
        for (const auto& m : modules) cols += static_cast<int>(m.second.size());
        const float cw = (contentW - gutter * (modules.size() - 1)) / cols;
        const float rowY = y + fxH - 30.0f - smallCellH;
        float x = margin;
        for (const auto& m : modules) {
            const Rect r{x, y, cw * m.second.size(), fxH};
            panels_.push_back({r, m.first});
            for (size_t i = 0; i < m.second.size(); ++i) addKnob(m.second[i], x + cw * (i + 0.5f), rowY, cw - 8.0f, smallCellH, smallR, false);
            x += r.w + gutter;
        }
    };
    buildRow(268.0f, {
        {PanelKind::Amp, {kParamPreampDrive, kParamTremoloDepth, kParamTremoloRate, kParamCabinet}},
        {PanelKind::EnvFilter, {kParamWahAmount, kParamWahSens}},
        {PanelKind::Phaser, {kParamPhaserMix, kParamPhaserRate}},
    });
    buildRow(132.0f, {
        {PanelKind::Chorus, {kParamChorusMix}},
        {PanelKind::Echo, {kParamDelayMix, kParamDelayTime, kParamDelayFeedback}},
        {PanelKind::Reverb, {kParamReverbMix, kParamReverbDecay}},
        {PanelKind::Output, {kParamMasterVolume}},
    });

    // 5. Performance strip
    performance_ = {margin, 12.0f, contentW, 112.0f};
    pedal_ = {margin + 10.0f, 22.0f, 56.0f, 92.0f};
    pitchWheel_ = {margin + 76.0f, 22.0f, 22.0f, 92.0f};
    modWheel_ = {margin + 104.0f, 22.0f, 22.0f, 92.0f};
    keyboard_ = {margin + 136.0f, 20.0f, contentW - 144.0f, 96.0f};
}

// =============================================================================
// Helpers
// =============================================================================
Editor::Stages Editor::stages() const {
    Stages s;
    if (engine_) {
        s.reed = stepIndex(engine_->getParamNormalized(kParamResonator), 1) == 1;
        s.electrostatic = stepIndex(engine_->getParamNormalized(kParamPickupType), 1) == 1;
        s.amp200A = stepIndex(engine_->getParamNormalized(kParamAmpType), 1) == 1;
    }
    return s;
}

void Editor::setParam(uint32_t id, float v) {
    if (engine_) engine_->setParamNormalized(id, v);
    if (host_.paramChanged) host_.paramChanged(id, v);
}

void Editor::sendMidi(uint8_t status, uint8_t d1, uint8_t d2) {
    if (engine_) engine_->pushGuiMidiEvent(status, d1, d2); // lock-free queue to the audio thread
}

const char* Editor::panelTitle(PanelKind kind, const Stages& c) const {
    switch (kind) {
        case PanelKind::Resonator: return c.reed ? "REED & HAMMER" : "TINE & HAMMER";
        case PanelKind::Pickup: return c.electrostatic ? "ELECTROSTATIC PICKUP & TONE" : "MAGNETIC PICKUP & TONE";
        case PanelKind::Amp: return c.amp200A ? "200A AMP" : "SUITCASE AMP";
        case PanelKind::EnvFilter: return "ENV FILTER";
        case PanelKind::Phaser: return "PHASER";
        case PanelKind::Chorus: return "CHORUS";
        case PanelKind::Echo: return "TAPE ECHO";
        case PanelKind::Reverb: return "REVERB";
        case PanelKind::Output: return "OUTPUT";
    }
    return "";
}

bool Editor::knobInactive(uint32_t id, const Stages& s) const {
    if (s.reed && id == kParamStrikePosition) return true;
    // A tempo-synced LFO follows the host, so its free rate knob is parked
    if (engine_ && id == kParamLfo1Rate) return stepIndex(engine_->getParamNormalized(kParamLfo1Sync), 7) != 0;
    if (engine_ && id == kParamLfo2Rate) return stepIndex(engine_->getParamNormalized(kParamLfo2Sync), 7) != 0;
    return false;
}

const char* Editor::knobLabel(uint32_t id, const Stages& c) const {
    switch (id) {
        case kParamLfo1Rate: case kParamLfo2Rate: return "Rate";
        case kParamEqB1Freq: case kParamEqB2Freq: case kParamEqFreq: case kParamEqB4Freq: return "Freq";
        case kParamEqB1Gain: case kParamEqB2Gain: case kParamEqGain: case kParamEqB4Gain: return "Gain";
        case kParamEqB1Q: case kParamEqB2Q: case kParamEqB3Q: case kParamEqB4Q: return "Q";
        case kParamCompThresh: return "Threshold";
        case kParamCompRatio: return "Ratio";
        case kParamCompAttack: return "Attack";
        case kParamCompRelease: return "Release";
        case kParamCompKnee: return "Knee";
        case kParamCompMakeup: return "Makeup";
        case kParamCompHpf: return "SC HPF";
        case kParamCompAmount: return "Mix";
        case kParamLimCeil: return "Ceiling";
        case kParamCabMic: return "Mic Pos";
        case kParamCabDistance: return "Distance";
        case kParamPressureAmt: return "Pressure";
        case kParamCabinet: return engine_ && stepIndex(engine_->getParamNormalized(kParamCabType), 9) != 0 ? "Cab Drive" : "Cabinet";
        case kParamHammerHysteresis: return c.reed ? "Damper" : "Cushion";
        case kParamTineBell: return c.reed ? "Overtones" : "Chime";
        case kParamToneBarCoupling: return c.reed ? "Tip Mass" : "Tone Bar";
        case kParamKeyNoise: return c.reed ? "Attack" : "Thump";
        case kParamPickupDistance: return c.electrostatic ? "Swing" : "Air Gap";
        case kParamPickupSymmetry: return c.electrostatic ? "Rest Gap" : "Voicing";
        case kParamPickupResonance: return c.electrostatic ? "Plate C" : "Coil Q";
        case kParamPreampDrive: return c.amp200A ? "Volume" : "Drive";
        case kParamTremoloDepth: return c.amp200A ? "Vibrato" : "Tremolo";
        case kParamTremoloRate: return "Speed";
        case kParamWahAmount: return "Amount";
        case kParamWahSens: return "Sens";
        case kParamPhaserMix: case kParamChorusMix: case kParamDelayMix: case kParamReverbMix: return "Mix";
        case kParamPhaserRate: return "Rate";
        case kParamDelayTime: return "Time";
        case kParamDelayFeedback: return "Repeats";
        case kParamReverbDecay: return "Decay";
        case kParamReverbPreDelay: return "Pre-Delay";
        case kParamMasterTune: return "Tune";
        default: return kParamMetadata[id].name;
    }
}

std::string Editor::valueText(uint32_t id, float norm, const Stages& c) const {
    char buf[64];
    if (knobInactive(id, c)) {
        if (id == kParamLfo1Rate || id == kParamLfo2Rate) {
            const float sync = engine_->getParamNormalized(id == kParamLfo1Rate ? kParamLfo1Sync : kParamLfo2Sync);
            return kSyncNames[stepIndex(sync, 7)];
        }
        return "\xE2\x80\x94"; // em dash
    }
    switch (id) {
        case kParamTineDecay:
            if (c.reed) { std::snprintf(buf, sizeof(buf), "x%.2f", std::pow(2.0f, (norm - 0.65f) * 4.0f)); return buf; }
            break;
        case kParamPickupDistance:
            if (c.electrostatic) { std::snprintf(buf, sizeof(buf), "x%.2f", electrostaticSwingScale(norm)); return buf; }
            break;
        case kParamPickupSymmetry:
            if (c.electrostatic) { std::snprintf(buf, sizeof(buf), "%.3f mm", electrostaticRestMm(norm)); return buf; }
            break;
        case kParamPickupResonance:
            if (c.electrostatic) { std::snprintf(buf, sizeof(buf), "%.0f pF", 240.0f * std::pow(2.0f, (0.45f - norm) * 2.0f)); return buf; }
            break;
        case kParamGapFmRatio:
            formatGapFmRatio(norm, stepIndex(engine_ ? engine_->getParamNormalized(kParamGapFmExact) : 0.0f, 1) == 1, buf, sizeof(buf));
            return buf;
        case kParamPreampDrive:
            if (c.amp200A) { std::snprintf(buf, sizeof(buf), "%.0f%%", wurliVolumePot(norm) * 100.0); return buf; }
            break;
        default: break;
    }
    formatParamDisplay(id, norm, buf, sizeof(buf));
    return buf;
}

void Editor::drawCentered(Canvas& c, const std::string& s, const Rect& r, const TextStyle& st, Color col) {
    const float w = c.textWidth(s, st), h = c.lineHeight(st);
    c.drawText(s, std::round(r.x + (r.w - w) * 0.5f), std::round(r.y + (r.h - h) * 0.5f), st, col);
}

std::vector<std::string> Editor::wrap(Canvas& c, const std::string& text, float width, const TextStyle& st) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t nl = text.find('\n', start);
        const std::string para = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        std::string line;
        size_t i = 0;
        while (i <= para.size()) {
            const size_t sp = para.find(' ', i);
            const std::string word = para.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
            const std::string trial = line.empty() ? word : line + " " + word;
            if (!line.empty() && c.textWidth(trial, st) > width) {
                lines.push_back(line);
                line = word;
            } else {
                line = trial;
            }
            if (sp == std::string::npos) break;
            i = sp + 1;
        }
        lines.push_back(line);
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return lines;
}

std::string Editor::ellipsize(Canvas& c, const std::string& s, float width, const TextStyle& st) {
    if (c.textWidth(s, st) <= width) return s;
    std::string t = s;
    while (!t.empty()) {
        do { t.pop_back(); } while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80);
        if (c.textWidth(t + "\xE2\x80\xA6", st) <= width) return t + "\xE2\x80\xA6";
    }
    return t;
}

// =============================================================================
// DRAW
// =============================================================================
void Editor::draw(Canvas& c) {
    lastHash_ = stateHash();
    forceRedraw_ = false;
    c.save();
    c.fillRoundRectGradient({0, 0, static_cast<float>(kEditorWidth), static_cast<float>(kEditorHeight)}, 0.0f, kBgTop, kBgBottom);
    drawHeader(c);
    drawStageStrip(c);
    drawPageBar(c);
    if (page_ == 0) {
        drawPanels(c);
        for (size_t i = 0; i < knobs_.size(); ++i) drawKnob(c, knobs_[i], draggedKnob_ == static_cast<int>(i));
    } else {
        drawPage(c);
    }
    drawPerformance(c);
    if (listOpen_) drawList(c);
    if (browserOpen_) drawPresetBrowser(c);
    if (saveOpen_) drawSaveDialog(c);
    if (aboutOpen_) drawAbout(c);
    if (menuOpen_) drawMenu(c);
    c.restore();
}

void Editor::headerButton(Canvas& c, const Rect& r, int icon, const char* label, bool enabled, bool active) {
    c.fillRoundRect(r, 5.0f, active ? kAccent : kPanelBg);
    c.strokeRoundRect(r, 5.0f, active ? kAccent : kPanelBorder, 1.0f);
    const Color tc = active ? kHeaderBg : (enabled ? kTextPrimary : kTextDim);
    const float cx = r.midX(), cy = r.midY();
    if (icon == kIconUndo || icon == kIconRedo) {
        // U-turn arrow: half circle plus an arrowhead on its top end
        const float s = icon == kIconUndo ? 1.0f : -1.0f; // undo points left, redo right
        const float ax = cx + s * 1.0f, ay = cy - 1.0f;
        if (s > 0) c.strokeArc(ax, ay, 4.5f, -kPi * 0.5f, kPi * 0.5f, tc, 1.6f);
        else c.strokeArc(ax, ay, 4.5f, kPi * 0.5f, kPi * 1.5f, tc, 1.6f);
        const float tipX = cx - s * 4.0f, tipY = ay + 4.5f;
        c.strokeLine(ax, tipY, tipX, tipY, tc, 1.6f);
        c.strokeLine(tipX, tipY, tipX + s * 2.6f, tipY + 2.6f, tc, 1.6f);
        c.strokeLine(tipX, tipY, tipX + s * 2.6f, tipY - 2.6f, tc, 1.6f);
    } else if (icon == kIconInfo) {
        c.fillCircle(cx, cy + 5.0f, 1.3f, tc);
        c.strokeLine(cx, cy - 5.0f, cx, cy + 1.5f, tc, 2.0f);
    } else {
        drawCentered(c, label, r, kButton, tc);
    }
}

void Editor::drawHeader(Canvas& c) {
    const float top = kEditorHeight;
    c.fillRect({0, top - 50.0f, static_cast<float>(kEditorWidth), 50.0f}, kHeaderBg);
    c.fillRect({0, top - 50.0f, static_cast<float>(kEditorWidth), 1.0f}, kHairline);

    const int currentModel = engine_ ? stepIndex(engine_->getParamNormalized(kParamModelType), 3) : 0;
    c.drawText("electropiano", 16.0f, top - 31.0f, kBrand, kTextPrimary);
    const float brandW = c.textWidth("electropiano", kBrand);
    c.drawText("by mtyas", 16.0f + brandW + 6.0f, top - 27.0f, kBrandBy, kAccent);
    char modelBuf[48];
    formatParamDisplay(kParamModelType, currentModel / 3.0f, modelBuf, sizeof(modelBuf));
    std::string modelName = modelBuf;
    for (auto& ch : modelName) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    c.drawText(modelName, 16.0f, top - 43.0f, kModelName, kTextMuted);

    // Instrument selector (sets all three stages)
    c.fillRoundRect(modelSelector_, 6.0f, kInsetBg);
    c.strokeRoundRect(modelSelector_, 6.0f, kPanelBorder, 1.0f);
    const char* modelNames[4] = {"Mk I", "Dyno", "200A", "Hybrid"};
    for (int i = 0; i < 4; ++i) {
        const Rect& r = modelButtons_[i];
        const bool active = i == currentModel;
        if (active) {
            c.fillRoundRect(r.inset(3.0f, 3.0f), 4.0f, kAccent);
        } else if (i > 0 && i != currentModel + 1) {
            c.fillRect({r.x, r.y + 8.0f, 1.0f, r.h - 16.0f}, kPanelBorder);
        }
        drawCentered(c, modelNames[i], r, active ? kSegmentOn : kSegment, active ? kHeaderBg : kTextMuted);
    }

    // Preset stepper: [<][ name v ][>]
    const Rect stepper{presetPrev_.x, presetPrev_.y, presetNext_.maxX() - presetPrev_.x, presetPrev_.h};
    c.fillRoundRect(stepper, 6.0f, kInsetBg);
    c.strokeRoundRect(stepper, 6.0f, kPanelBorder, 1.0f);
    c.fillRect({presetPrev_.maxX(), presetPrev_.y + 1.0f, 1.0f, presetPrev_.h - 2.0f}, kPanelBorder);
    c.fillRect({presetNext_.x - 1.0f, presetNext_.y + 1.0f, 1.0f, presetNext_.h - 2.0f}, kPanelBorder);
    auto chevron = [&](float cx, float cy, float dx, float dy) {
        c.strokeLine(cx - dx, cy + dy, cx, cy, kTextMuted, 1.5f);
        c.strokeLine(cx, cy, cx - dx, cy - dy, kTextMuted, 1.5f);
    };
    chevron(presetPrev_.midX() - 2.0f, presetPrev_.midY(), -3.5f, 4.0f);
    chevron(presetNext_.midX() + 2.0f, presetNext_.midY(), 3.5f, 4.0f);

    std::string presetName = "Init";
    if (engine_) {
        if (const auto* p = engine_->getPresetManager().getPreset(engine_->getCurrentPreset())) presetName = p->name;
    }
    const Rect nameRect{presetBox_.x + 8.0f, presetBox_.y, presetBox_.w - 28.0f, presetBox_.h};
    drawCentered(c, ellipsize(c, presetName, nameRect.w, kPresetName), nameRect, kPresetName, kTextPrimary);
    const float chx = presetBox_.maxX() - 13.0f, chy = presetBox_.midY();
    c.strokeLine(chx - 3.5f, chy + 1.5f, chx, chy - 2.0f, kTextMuted, 1.4f);
    c.strokeLine(chx, chy - 2.0f, chx + 3.5f, chy + 1.5f, kTextMuted, 1.4f);

    const bool canUndo = engine_ && engine_->canUndo();
    const bool canRedo = engine_ && engine_->canRedo();
    const bool learning = engine_ && engine_->getLearningParamId() >= 0;
    headerButton(c, undoBtn_, kIconUndo, "UNDO", canUndo, false);
    headerButton(c, redoBtn_, kIconRedo, "REDO", canRedo, false);
    headerButton(c, midiBtn_, kIconNone, learning ? "LEARN" : "MIDI", true, learning);
    headerButton(c, aboutBtn_, kIconInfo, "?", true, aboutOpen_);
}

void Editor::drawStageStrip(Canvas& c) {
    c.fillRoundRect(strip_, 8.0f, kPanelBg);
    c.strokeRoundRect(strip_, 8.0f, kPanelBorder, 1.0f);
    const Stages s = stages();
    const char* titles[3] = {"RESONATOR", "PICKUP", "AMP"};
    const char* options[3][2] = {{"Rhodes Tine", "Wurli Reed"}, {"Magnetic", "Electrostatic"}, {"Suitcase", "200A Circuit"}};
    const bool selected[3] = {s.reed, s.electrostatic, s.amp200A};
    for (int g = 0; g < 3; ++g) {
        const Rect r0 = stageRects_[g][0], r1 = stageRects_[g][1];
        c.drawText(titles[g], stageLabelX_[g], r0.midY() - 6.0f, kPanelTitle, kTextMuted);
        const Rect both{r0.x, r0.y, r1.maxX() - r0.x, r0.h};
        c.fillRoundRect(both, 6.0f, kInsetBg);
        c.strokeRoundRect(both, 6.0f, kPanelBorder, 1.0f);
        for (int k = 0; k < 2; ++k) {
            const bool on = (k == 1) == selected[g];
            if (on) c.fillRoundRect(stageRects_[g][k].inset(3.0f, 3.0f), 4.0f, kAccent);
            drawCentered(c, options[g][k], stageRects_[g][k], on ? kStripOptionOn : kStripOption, on ? kHeaderBg : kTextMuted);
        }
        if (g < 2) {
            const float ax = r1.maxX() + 10.0f, ay = r1.midY();
            c.strokeLine(ax, ay, ax + 14.0f, ay, kTextDim, 1.5f);
            c.strokeLine(ax + 9.0f, ay + 4.0f, ax + 14.0f, ay, kTextDim, 1.5f);
            c.strokeLine(ax + 14.0f, ay, ax + 9.0f, ay - 4.0f, kTextDim, 1.5f);
        }
    }
}

void Editor::drawPanels(Canvas& c) {
    const Stages s = stages();
    for (const auto& p : panels_) {
        c.fillRoundRectGradient(p.rect, 8.0f, kPanelTop, kPanelBg);
        c.strokeRoundRect(p.rect, 8.0f, kPanelBorder, 1.0f);
        const float ty = p.rect.maxY() - 22.0f;
        c.fillRect({p.rect.x + 12.0f, ty + 3.0f, 2.0f, 9.0f}, kAccent);
        c.drawText(panelTitle(p.kind, s), p.rect.x + 20.0f, ty + 0.5f, kPanelTitle, kTextMuted);
    }
}

void Editor::drawKnob(Canvas& c, const Knob& k, bool dragged) {
    const float norm = engine_ ? engine_->getParamNormalized(k.paramId) : kParamMetadata[k.paramId].defaultValue;
    const int8_t midiCc = engine_ ? engine_->getParamCc(k.paramId) : -1;
    const bool armed = engine_ && engine_->getLearningParamId() == static_cast<int32_t>(k.paramId);
    const Stages s = stages();

    c.save();
    if (knobInactive(k.paramId, s)) c.setAlpha(0.35f);
    const Rect& cell = k.rect;
    const float r = k.radius, arcR = r + kArcInset;
    const float cx = std::round(cell.midX()), cy = std::round(cell.maxY() - 20.0f - arcR);

    drawCentered(c, knobLabel(k.paramId, s), {cell.x, cell.maxY() - 14.0f, cell.w, 14.0f}, kKnobLabel, kTextPrimary);

    // Value arc: 270 degree sweep, 7:30 to 4:30 o'clock
    const float a0 = 225.0f * kPi / 180.0f, a1 = -45.0f * kPi / 180.0f;
    const float aCur = a0 - norm * (a0 - a1);
    c.strokeArc(cx, cy, arcR, a0, a1, kArcTrack, 3.0f);
    // Scale ticks outside the arc: the ones the value has passed light up
    if (r >= 16.0f) {
        for (int t = 0; t <= 10; ++t) {
            const float ta = a0 - (a0 - a1) * t / 10.0f, c0 = std::cos(ta), s0 = std::sin(ta);
            const bool passed = static_cast<float>(t) / 10.0f <= norm + 0.001f;
            c.strokeLine(cx + c0 * (arcR + 4.5f), cy + s0 * (arcR + 4.5f), cx + c0 * (arcR + (t % 5 == 0 ? 7.5f : 6.0f)),
                         cy + s0 * (arcR + (t % 5 == 0 ? 7.5f : 6.0f)), passed ? kAccent.withAlpha(0.55f) : kHairline, 1.0f);
        }
    }
    const float aFrom = k.bipolar ? kPi * 0.5f : a0;
    if (std::fabs(aFrom - aCur) > 0.01f) {
        c.strokeArc(cx, cy, arcR, aFrom, aCur, (armed ? kLearn : kAccent).withAlpha(0.20f), 7.0f); // soft glow
        c.strokeArc(cx, cy, arcR, aFrom, aCur, armed ? kLearn : kAccent, 3.0f);
    }

    // Knob body: soft drop shadow, machined skirt, darker cap
    c.shadowCircle(cx, cy, r, 3.0f, -2.0f, kBlack.withAlpha(0.6f));
    c.fillCircleGradient(cx, cy, r, {0.30f, 0.315f, 0.345f, 1.0f}, {0.13f, 0.14f, 0.16f, 1.0f});
    const float capR = r * 0.80f;
    c.fillCircleGradient(cx, cy, capR, {0.185f, 0.195f, 0.22f, 1.0f}, {0.115f, 0.122f, 0.14f, 1.0f});
    c.strokeCircle(cx, cy, capR - 0.5f, {1.0f, 1.0f, 1.0f, 0.06f}, 1.0f);

    // Pointer
    const float ca = std::cos(aCur), sa = std::sin(aCur);
    c.strokeLine(cx + ca * r * 0.30f, cy + sa * r * 0.30f, cx + ca * r * 0.74f, cy + sa * r * 0.74f, kTextPrimary, 2.2f);

    // Value readout (or LEARN prompt) under the arc's open bottom, MIDI CC tag beside it
    const Rect valRect{cell.x, cell.y, cell.w, 14.0f};
    if (armed) {
        drawCentered(c, "LEARNING", valRect, kLearning, kLearn);
    } else {
        const std::string val = valueText(k.paramId, norm, s);
        drawCentered(c, val, valRect, kKnobValue, dragged ? kAccent : kTextMuted);
        if (midiCc >= 0) {
            char cc[16];
            std::snprintf(cc, sizeof(cc), "CC%d", midiCc);
            const float ccW = c.textWidth(cc, kCcTag), valW = c.textWidth(val, kKnobValue);
            const Rect tag{std::round(cx + valW * 0.5f + 4.0f), cell.y + 1.5f, ccW + 6.0f, 11.0f};
            c.fillRoundRect(tag, 3.0f, kLearn.withAlpha(0.14f));
            drawCentered(c, cc, tag, kCcTag, kLearn);
        }
    }
    c.restore();
}

bool Editor::keyRect(int note, Rect& out) const {
    if (note < kKbStartNote || note >= kKbStartNote + kKbNumKeys) return false;
    const float naturalW = keyboard_.w / kKbNumNaturals;
    int naturalsBefore = 0;
    for (int n = kKbStartNote; n < note; ++n) if (!kIsBlackKey[n % 12]) ++naturalsBefore;
    const float x0 = keyboard_.x + naturalsBefore * naturalW;
    if (kIsBlackKey[note % 12]) {
        const float sw = naturalW * 0.60f, sh = keyboard_.h * 0.60f;
        out = {x0 - sw * 0.5f, keyboard_.maxY() - sh, sw, sh};
    } else {
        out = {x0, keyboard_.y, naturalW, keyboard_.h};
    }
    return true;
}

int Editor::noteAtPoint(float x, float y) const {
    if (!keyboard_.contains(x, y)) return -1;
    Rect r{};
    for (int pass = 0; pass < 2; ++pass) {
        for (int note = kKbStartNote; note < kKbStartNote + kKbNumKeys; ++note) {
            if (kIsBlackKey[note % 12] == (pass == 0) && keyRect(note, r) && r.contains(x, y)) return note;
        }
    }
    return -1;
}

void Editor::drawPerformance(Canvas& c) {
    c.fillRoundRect(performance_, 8.0f, kInsetBg);
    c.strokeRoundRect(performance_, 8.0f, kPanelBorder, 1.0f);

    const bool sustained = engine_ && engine_->isSustainActive();
    c.fillRoundRect(pedal_, 6.0f, sustained ? kAccent.withAlpha(0.16f) : kPanelBg);
    c.strokeRoundRect(pedal_, 6.0f, sustained ? kAccent : kPanelBorder, 1.0f);
    const float ledX = pedal_.midX(), ledY = pedal_.maxY() - 20.0f;
    if (sustained) c.glowCircle(ledX, ledY, 3.5f, 6.0f, kAccent.withAlpha(0.8f));
    c.fillCircle(ledX, ledY, 3.5f, sustained ? kAccent : kArcTrack);
    drawCentered(c, "SUSTAIN", {pedal_.x, pedal_.y + 10.0f, pedal_.w, 12.0f}, kSustain, sustained ? kAccent : kTextMuted);

    auto wheel = [&](const Rect& r, const char* label, float norm) {
        const Rect slot{r.x, r.y + 14.0f, r.w, r.h - 14.0f};
        c.fillRoundRect(slot, 5.0f, {0.02f, 0.022f, 0.027f, 1.0f});
        c.strokeRoundRect(slot, 5.0f, kPanelBorder, 1.0f);
        const Rect w = slot.inset(3.0f, 3.0f);
        c.fillRoundRectGradient(w, 3.0f, {0.13f, 0.14f, 0.16f, 1.0f}, {0.22f, 0.23f, 0.26f, 1.0f});
        for (float y = w.y + 4.0f; y < w.maxY() - 2.0f; y += 4.0f) c.fillRect({w.x + 2.0f, y - 0.5f, w.w - 4.0f, 1.0f}, kBlack.withAlpha(0.55f));
        const float my = w.y + 4.0f + norm * (w.h - 8.0f);
        c.fillRect({w.x + 1.0f, my - 1.0f, w.w - 2.0f, 2.0f}, kTextPrimary.withAlpha(0.85f));
        drawCentered(c, label, {r.x - 6.0f, r.y, r.w + 12.0f, 11.0f}, kWheelLabel, kTextMuted);
    };
    wheel(pitchWheel_, "PITCH", 0.5f);
    wheel(modWheel_, "MOD", 0.0f);

    auto held = [&](int note) { return (engine_ && engine_->isNoteActive(note)) || heldNote_ == note; };
    Rect kr{};
    for (int note = kKbStartNote; note < kKbStartNote + kKbNumKeys; ++note) {
        if (kIsBlackKey[note % 12] || !keyRect(note, kr)) continue;
        const bool h = held(note);
        c.fillRoundRectGradient(kr.inset(0.5f, 0.0f), 2.5f, h ? Color{0.98f, 0.80f, 0.55f, 1.0f} : Color{0.93f, 0.925f, 0.91f, 1.0f},
                                h ? Color{0.95f, 0.70f, 0.38f, 1.0f} : Color{0.99f, 0.985f, 0.975f, 1.0f});
        c.fillRect({kr.maxX() - 1.0f, kr.y + 1.0f, 1.0f, kr.h - 1.0f}, kBlack.withAlpha(0.35f));
    }
    for (int note = kKbStartNote; note < kKbStartNote + kKbNumKeys; ++note) {
        if (!kIsBlackKey[note % 12] || !keyRect(note, kr)) continue;
        const bool h = held(note);
        c.shadowRoundRect(kr.offset(0.5f, 0.0f), 2.0f, 1.5f, -1.5f, kBlack.withAlpha(0.5f));
        c.fillRoundRect(kr, 2.0f, h ? kAccent : Color{0.08f, 0.085f, 0.095f, 1.0f});
        if (!h) c.fillRoundRectGradient({kr.x + 1.5f, kr.y + 4.0f, kr.w - 3.0f, kr.h - 5.0f}, 1.5f, {0.17f, 0.18f, 0.20f, 1.0f}, {0.10f, 0.105f, 0.115f, 1.0f});
    }
    // Key-slip shadow across the top of the keys
    c.fillRoundRectGradient({keyboard_.x, keyboard_.maxY() - 5.0f, keyboard_.w, 5.0f}, 0.0f, kBlack.withAlpha(0.45f), kBlack.withAlpha(0.0f));
}

// =============================================================================
// MODALS
// =============================================================================
Rect Editor::modalFrame(Canvas& c, float w, float h, const char* title, Rect& closeRect) {
    c.fillRect({0, 0, static_cast<float>(kEditorWidth), static_cast<float>(kEditorHeight)}, kBlack.withAlpha(0.62f));
    const Rect r{std::round((kEditorWidth - w) * 0.5f), std::round((kEditorHeight - h) * 0.5f), w, h};
    c.shadowRoundRect(r, 10.0f, 16.0f, -8.0f, kBlack.withAlpha(0.6f));
    c.fillRoundRect(r, 10.0f, kPanelBg);
    c.strokeRoundRect(r, 10.0f, kPanelBorder, 1.0f);
    c.drawText(title, r.x + 20.0f, r.maxY() - 34.0f, kModalTitle, kTextPrimary);
    closeRect = {r.maxX() - 38.0f, r.maxY() - 38.0f, 26.0f, 26.0f};
    c.fillRoundRect(closeRect, 13.0f, kInsetBg);
    const float x = closeRect.midX(), y = closeRect.midY();
    c.strokeLine(x - 4.0f, y - 4.0f, x + 4.0f, y + 4.0f, kTextMuted, 1.4f);
    c.strokeLine(x - 4.0f, y + 4.0f, x + 4.0f, y - 4.0f, kTextMuted, 1.4f);
    c.fillRect({r.x, r.maxY() - 49.0f, r.w, 1.0f}, kHairline);
    return r;
}

void Editor::modalButton(Canvas& c, const Rect& r, const char* label, bool primary, bool enabled) {
    c.save();
    if (!enabled) c.setAlpha(0.4f);
    c.fillRoundRect(r, 6.0f, primary ? kAccent : kInsetBg);
    c.strokeRoundRect(r, 6.0f, primary ? kAccent : kPanelBorder, 1.0f);
    drawCentered(c, label, r, kModalButton, primary ? kHeaderBg : kTextPrimary);
    c.restore();
}

std::vector<int> Editor::visiblePresets() const {
    std::vector<int> out;
    if (!engine_) return out;
    const auto& presets = engine_->getPresetManager().getPresets();
    auto lower = [](std::string t) {
        for (auto& ch : t) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return t;
    };
    const std::string needle = lower(search_);
    for (size_t i = 0; i < presets.size(); ++i) {
        const auto& p = presets[i];
        if (favoritesOnly_ ? !p.isFavorite : !(category_ == PresetCategory::All || p.category == category_)) continue;
        if (!needle.empty() && lower(p.name + " " + p.description + " " + p.author).find(needle) == std::string::npos) continue;
        out.push_back(static_cast<int>(i));
    }
    return out;
}

Rect Editor::presetListRect() const { return {browserRect_.x + 20.0f, browserRect_.y + 92.0f, browserRect_.w - 40.0f, 254.0f}; }

void Editor::drawPresetBrowser(Canvas& c) {
    browserRect_ = modalFrame(c, 600.0f, 440.0f, "Presets", browserClose_);
    const Rect& m = browserRect_;

    // Search: typing while the browser is open filters by name, description and author
    searchRect_ = {m.x + 160.0f, m.maxY() - 40.0f, 340.0f, 26.0f};
    c.fillRoundRect(searchRect_, 6.0f, kInsetBg);
    c.strokeRoundRect(searchRect_, 6.0f, search_.empty() ? kPanelBorder : kAccent, 1.0f);
    {
        const float sx = searchRect_.x + 14.0f, sy = searchRect_.midY() + 1.0f;
        c.strokeCircle(sx, sy, 4.5f, kTextDim, 1.4f);
        c.strokeLine(sx + 3.2f, sy - 3.2f, sx + 6.5f, sy - 6.5f, kTextDim, 1.4f);
        const float th = c.lineHeight(kListItem);
        const float ty = std::round(searchRect_.y + (searchRect_.h - th) * 0.5f);
        if (search_.empty()) {
            c.drawText("Type to search", searchRect_.x + 28.0f, ty, kListItem, kTextDim);
        } else {
            const std::string shown = ellipsize(c, search_, searchRect_.w - 40.0f, kListItem);
            c.drawText(shown, searchRect_.x + 28.0f, ty, kListItem, kTextPrimary);
            c.fillRect({searchRect_.x + 29.0f + c.textWidth(shown, kListItem), searchRect_.y + 6.0f, 1.5f, searchRect_.h - 12.0f}, kAccent);
        }
    }

    const char* cats[7] = {"All", "Rhodes", "Wurlitzer", "Dyno/Bells", "Hybrid/Fx", "User", "Favourites"};
    const Rect tabs{m.x + 20.0f, m.maxY() - 86.0f, 560.0f, 28.0f};
    c.fillRoundRect(tabs, 6.0f, kInsetBg);
    const float tw = tabs.w / 7.0f;
    for (int i = 0; i < 7; ++i) {
        tabRects_[i] = {tabs.x + i * tw, tabs.y, tw, tabs.h};
        const bool on = i == 6 ? favoritesOnly_ : (!favoritesOnly_ && static_cast<int>(category_) == i);
        if (on) c.fillRoundRect(tabRects_[i].inset(3.0f, 3.0f), 4.0f, kAccent);
        drawCentered(c, cats[i], tabRects_[i], on ? kSegmentOn : kSegment, on ? kHeaderBg : kTextMuted);
    }

    const Rect list = presetListRect();
    c.fillRoundRect(list, 6.0f, kInsetBg);
    c.strokeRoundRect(list, 6.0f, kPanelBorder, 1.0f);
    const std::vector<int> items = visiblePresets();
    const int rows = static_cast<int>((list.h - 8.0f) / 24.0f);
    listScroll_ = std::clamp(listScroll_, 0, std::max(0, static_cast<int>(items.size()) - rows));
    if (engine_) {
        c.save();
        c.clip(list.inset(1.0f, 1.0f));
        const auto& presets = engine_->getPresetManager().getPresets();
        float rowY = list.maxY() - 28.0f;
        for (int k = listScroll_; k < static_cast<int>(items.size()) && rowY >= list.y; ++k) {
            const int p = items[k];
            const bool current = engine_->getCurrentPreset() == p;
            const bool selected = selectedPreset_ == p;
            if (current || selected) {
                c.fillRoundRect({list.x + 4.0f, rowY - 2.0f, list.w - 18.0f, 22.0f}, 4.0f, kAccent.withAlpha(current ? 0.16f : 0.08f));
            }
            // Favourite star (click it to toggle)
            {
                const float cx = list.x + 16.0f, cy = rowY + 8.0f, ro = 5.5f, ri = 2.4f;
                float px = 0, py = 0;
                const Color sc = presets[p].isFavorite ? kAccent : kTextDim.withAlpha(0.6f);
                for (int v = 0; v <= 10; ++v) {
                    const float a = 1.5707963f + v * 0.6283185f, rr = (v & 1) ? ri : ro;
                    const float x = cx + rr * std::cos(a), y = cy + rr * std::sin(a);
                    if (v > 0) c.strokeLine(px, py, x, y, sc, presets[p].isFavorite ? 1.6f : 1.0f);
                    px = x; py = y;
                }
                if (presets[p].isFavorite) c.fillCircle(cx, cy, 2.2f, kAccent);
            }
            c.drawText(presets[p].name, list.x + 32.0f, rowY + 2.0f, current ? kListItemOn : kListItem, current ? kAccent : kTextPrimary);
            if (presets[p].isUserPreset) {
                const float w = c.textWidth("USER", kCcTag);
                c.drawText("USER", list.maxX() - 24.0f - w, rowY + 4.0f, kCcTag, kTextDim);
            }
            rowY -= 24.0f;
        }
        c.restore();
        // Scroll bar
        if (static_cast<int>(items.size()) > rows) {
            const float track = list.h - 8.0f;
            const float thumbH = std::max(24.0f, track * rows / items.size());
            const float t = static_cast<float>(listScroll_) / (items.size() - rows);
            c.fillRoundRect({list.maxX() - 8.0f, list.maxY() - 4.0f - thumbH - t * (track - thumbH), 4.0f, thumbH}, 2.0f, kTextDim);
        }
    }

    // The selected (or loaded) preset's description
    if (engine_) {
        const int shownIdx = selectedPreset_ >= 0 ? selectedPreset_ : engine_->getCurrentPreset();
        if (const auto* p = engine_->getPresetManager().getPreset(shownIdx)) {
            float dy = list.y - 16.0f;
            int lines = 0;
            for (const auto& line : wrap(c, p->description.empty() ? p->name : p->description, m.w - 40.0f, kLinkSub)) {
                if (++lines > 2) break;
                c.drawText(line, m.x + 20.0f, dy, kLinkSub, kTextMuted);
                dy -= c.lineHeight(kLinkSub) + 1.0f;
            }
        }
    }

    browserSave_ = {m.x + 20.0f, m.y + 16.0f, 120.0f, 28.0f};
    browserDelete_ = {m.x + 148.0f, m.y + 16.0f, 120.0f, 28.0f};
    const bool canDelete = engine_ && selectedPreset_ >= 0 && engine_->getPresetManager().getPreset(selectedPreset_) &&
                           engine_->getPresetManager().getPreset(selectedPreset_)->isUserPreset;
    modalButton(c, browserSave_, "Save Preset\xE2\x80\xA6", true);
    modalButton(c, browserDelete_, "Delete", false, canDelete);
    char count[64];
    std::snprintf(count, sizeof(count), "%d presets \xC2\xB7 click to load \xC2\xB7 star to favourite", static_cast<int>(items.size()));
    c.drawText(count, m.x + 290.0f, m.y + 24.0f, kLinkSub, kTextDim);
}

void Editor::drawSaveDialog(Canvas& c) {
    saveRect_ = modalFrame(c, 420.0f, 200.0f, "Save Preset", saveClose_);
    const Rect& m = saveRect_;
    c.drawText("Name", m.x + 20.0f, m.maxY() - 78.0f, kPanelTitle, kTextMuted);
    const Rect field{m.x + 20.0f, m.maxY() - 116.0f, m.w - 40.0f, 30.0f};
    c.fillRoundRect(field, 6.0f, kInsetBg);
    c.strokeRoundRect(field, 6.0f, kAccent, 1.0f);
    const std::string shown = ellipsize(c, saveName_, field.w - 24.0f, kTextField);
    const float th = c.lineHeight(kTextField);
    const float ty = std::round(field.y + (field.h - th) * 0.5f);
    c.drawText(shown, field.x + 10.0f, ty, kTextField, kTextPrimary);
    const float caretX = field.x + 11.0f + c.textWidth(shown, kTextField);
    c.fillRect({caretX, field.y + 7.0f, 1.5f, field.h - 14.0f}, kAccent);

    saveOk_ = {m.maxX() - 140.0f, m.y + 16.0f, 120.0f, 28.0f};
    saveCancel_ = {m.maxX() - 268.0f, m.y + 16.0f, 120.0f, 28.0f};
    modalButton(c, saveCancel_, "Cancel", false);
    modalButton(c, saveOk_, "Save", true, !saveName_.empty());
}

void Editor::drawAbout(Canvas& c) {
    aboutRect_ = modalFrame(c, 560.0f, 440.0f, "electropiano", aboutClose_);
    const Rect& m = aboutRect_;
    c.drawText("by mtyas  \xC2\xB7  v" PHS_VERSION_STRING "  \xC2\xB7  GPL-3.0", m.x + 28.0f + c.textWidth("electropiano", kModalTitle), m.maxY() - 31.0f, kAboutBy, kAccent);

    const float x = m.x + 24.0f, w = m.w - 48.0f;
    float y = m.maxY() - 78.0f;
    auto heading = [&](const char* s) { c.drawText(s, x, y, kAboutHead, kTextPrimary); y -= 6.0f; };
    auto body = [&](const std::string& text) {
        const float lh = c.lineHeight(kAboutBody) + 1.0f;
        for (const auto& line : wrap(c, text, w, kAboutBody)) {
            y -= lh;
            c.drawText(line, x, y, kAboutBody, kTextMuted);
        }
        y -= 22.0f;
    };
    heading("Physically modelled electric pianos, stage by stage");
    body("Pick a resonator, a pickup and an amp, and mix them freely: a Rhodes tine through the Wurlitzer 200A circuit, "
         "a Wurlitzer reed on a Rhodes pickup, or either instrument as built.");
    heading("Built on the work of");
    body("\xE2\x80\xA2  Rhodes tine, felt hammer and magnetic pickup: Antoine Falaize & Thomas H\xC3\xA9lie, \"Passive simulation of the "
         "nonlinear port-Hamiltonian modeling of a Rhodes piano\", J. Sound Vib. 390 (2017), and the PyPHS project. The tine is "
         "solved with their energy-conserving discrete-gradient method and the pickup uses their flux law (eq. 25).\n"
         "\xE2\x80\xA2  Wurlitzer 200A reed, electrostatic pickup, preamp, LDR vibrato, power amp and speaker: a C++ port of "
         "OpenWurli by hal0zer0 and contributors, derived from the 200A schematic and SPICE.");
    heading("Created by Matthew Tyas");
    body("electropiano is free software under the GNU GPL v3. If it ends up in your music, please consider supporting it "
         "and the projects above.");

    const float bw = (w - 3 * 8.0f) / 4.0f;
    const char* titles[4] = {"Website", "Support on Ko-fi", "PyPHS", "OpenWurli"};
    const char* subs[4] = {"mtyas.com", "ko-fi.com/mtyas", "github.com/pyphs", "github.com/hal0zer0"};
    for (int i = 0; i < 4; ++i) {
        aboutLinks_[i] = {x + i * (bw + 8.0f), m.y + 20.0f, bw, 40.0f};
        const bool primary = i == 1;
        const Rect& r = aboutLinks_[i];
        c.fillRoundRect(r, 6.0f, primary ? kAccent : kInsetBg);
        c.strokeRoundRect(r, 6.0f, primary ? kAccent : kPanelBorder, 1.0f);
        drawCentered(c, titles[i], {r.x, r.y + 19.0f, r.w, 16.0f}, kLinkTitle, primary ? kHeaderBg : kTextPrimary);
        drawCentered(c, subs[i], {r.x, r.y + 5.0f, r.w, 14.0f}, kLinkSub, primary ? Color{0.15f, 0.10f, 0.04f, 1.0f} : kTextMuted);
    }
}

void Editor::drawMenu(Canvas& c) {
    c.shadowRoundRect(menuRect_, 6.0f, 8.0f, -3.0f, kBlack.withAlpha(0.6f));
    c.fillRoundRect(menuRect_, 6.0f, kPanelBg);
    c.strokeRoundRect(menuRect_, 6.0f, kPanelBorder, 1.0f);
    float y = menuRect_.maxY() - 6.0f - 24.0f;
    for (const auto& item : menuItems_) {
        c.drawText(item.label, menuRect_.x + 12.0f, y + 5.0f, kModalButton, kTextPrimary);
        y -= 24.0f;
    }
}

// =============================================================================
// INPUT
// =============================================================================
void Editor::mouseDown(float x, float y, bool fine, double timeSec) {
    forceRedraw_ = true;

    if (listOpen_) { mouseDownList(x, y); return; }

    if (menuOpen_) {
        menuOpen_ = false;
        if (menuRect_.contains(x, y)) {
            const int idx = static_cast<int>((menuRect_.maxY() - 6.0f - y) / 24.0f);
            if (idx >= 0 && idx < static_cast<int>(menuItems_.size())) runMenuAction(menuItems_[idx].action);
        }
        return;
    }

    if (saveOpen_) {
        if (saveClose_.contains(x, y) || saveCancel_.contains(x, y)) { saveOpen_ = false; return; }
        if (saveOk_.contains(x, y)) keyAction(Key::Enter);
        return;
    }

    if (aboutOpen_) {
        static const char* urls[4] = {"https://mtyas.com", "https://ko-fi.com/mtyas", "https://github.com/pyphs/pyphs",
                                      "https://github.com/hal0zer0/openwurli"};
        for (int i = 0; i < 4; ++i) {
            if (aboutLinks_[i].contains(x, y)) { if (host_.openUrl) host_.openUrl(urls[i]); return; }
        }
        if (aboutClose_.contains(x, y) || !aboutRect_.contains(x, y)) aboutOpen_ = false;
        return;
    }

    if (browserOpen_) {
        if (browserClose_.contains(x, y) || !browserRect_.contains(x, y)) { browserOpen_ = false; return; }
        for (int i = 0; i < 7; ++i) {
            if (!tabRects_[i].contains(x, y)) continue;
            favoritesOnly_ = i == 6;
            if (i < 6) category_ = static_cast<PresetCategory>(i);
            listScroll_ = 0;
            return;
        }
        if (browserSave_.contains(x, y)) { saveOpen_ = true; saveName_ = "My Piano"; return; }
        if (browserDelete_.contains(x, y)) {
            if (engine_ && selectedPreset_ >= 0) {
                const auto* p = engine_->getPresetManager().getPreset(selectedPreset_);
                if (p && p->isUserPreset) { engine_->getPresetManager().deleteUserPreset(selectedPreset_); selectedPreset_ = -1; }
            }
            return;
        }
        const Rect list = presetListRect();
        if (list.contains(x, y) && engine_) {
            const std::vector<int> items = visiblePresets();
            const int row = static_cast<int>((list.maxY() - 4.0f - y) / 24.0f);
            const int k = listScroll_ + row;
            if (row >= 0 && k < static_cast<int>(items.size()) && x < list.x + 28.0f) {
                const auto* fav = engine_->getPresetManager().getPreset(items[k]);
                if (fav) engine_->getPresetManager().setFavorite(items[k], !fav->isFavorite);
                return;
            }
            if (row >= 0 && k < static_cast<int>(items.size())) {
                const auto* p = engine_->getPresetManager().getPreset(items[k]);
                selectedPreset_ = items[k];
                engine_->loadPreset(items[k]);
                for (uint32_t i = 0; i < kNumParams; ++i) if (host_.paramChanged) host_.paramChanged(i, engine_->getParamNormalized(i));
                if (p && !p->isUserPreset) browserOpen_ = false; // keep open on user presets so they can be deleted
            }
        }
        return;
    }

    // Header: instrument buttons are macros that also set the three stages
    for (int i = 0; i < 4; ++i) {
        if (modelButtons_[i].contains(x, y)) {
            if (engine_) engine_->pushUndoState("Instrument");
            setParam(kParamModelType, i / 3.0f);
            const bool reed = i == 2, electrostatic = i == 2 || i == 3;
            setParam(kParamResonator, reed ? 1.0f : 0.0f);
            setParam(kParamPickupType, electrostatic ? 1.0f : 0.0f);
            setParam(kParamAmpType, electrostatic ? 1.0f : 0.0f);
            return;
        }
    }
    for (int g = 0; g < 3; ++g) {
        for (int k = 0; k < 2; ++k) {
            if (stageRects_[g][k].contains(x, y)) {
                const uint32_t ids[3] = {kParamResonator, kParamPickupType, kParamAmpType};
                if (engine_) engine_->pushUndoState("Stage");
                setParam(ids[g], static_cast<float>(k));
                return;
            }
        }
    }
    if (undoBtn_.contains(x, y)) { if (engine_) engine_->undo(); return; }
    if (redoBtn_.contains(x, y)) { if (engine_) engine_->redo(); return; }
    if (midiBtn_.contains(x, y)) {
        if (engine_) { if (engine_->getLearningParamId() >= 0) engine_->stopMidiLearn(); else engine_->startMidiLearn(0); }
        return;
    }
    auto stepPreset = [&](int dir) {
        if (!engine_) return;
        const int count = engine_->getPresetManager().getPresetCount();
        if (count <= 0) return;
        engine_->loadPreset((engine_->getCurrentPreset() + dir + count) % count);
        for (uint32_t i = 0; i < kNumParams; ++i) if (host_.paramChanged) host_.paramChanged(i, engine_->getParamNormalized(i));
    };
    if (presetPrev_.contains(x, y)) { stepPreset(-1); return; }
    if (presetNext_.contains(x, y)) { stepPreset(1); return; }
    if (presetBox_.contains(x, y)) { browserOpen_ = true; selectedPreset_ = -1; return; }
    if (aboutBtn_.contains(x, y)) { aboutOpen_ = true; return; }
    for (int i = 0; i < kNumPages; ++i) {
        if (pageTabs_[i].contains(x, y)) {
            page_ = i;
            if (i == 7) refreshTunings();
            return;
        }
    }
    if (page_ == 6 && mouseDownPlay(x, y)) return;

    if (pedal_.contains(x, y)) {
        sendMidi(0xB0, 64, (engine_ && engine_->isSustainActive()) ? 0 : 127);
        return;
    }

    if (page_ != 0 && mouseDownModPanel(x, y, fine, timeSec)) return;
    for (size_t i = 0; page_ == 0 && i < knobs_.size(); ++i) {
        if (!knobs_[i].rect.contains(x, y)) continue;
        const uint32_t pid = knobs_[i].paramId;
        if (engine_ && engine_->getLearningParamId() >= 0) { engine_->startMidiLearn(pid); return; }
        if (lastClickedKnob_ == static_cast<int>(i) && timeSec - lastClickTime_ < 0.35) {
            setParam(pid, kParamMetadata[pid].defaultValue); // double-click resets
            draggedKnob_ = -1;
            lastClickTime_ = -1.0;
            return;
        }
        lastClickTime_ = timeSec;
        lastClickedKnob_ = static_cast<int>(i);
        draggedKnob_ = static_cast<int>(i);
        beginKnobDrag(pid, y, fine);
        return;
    }

    const int note = noteAtPoint(x, y);
    if (note >= 0) {
        heldNote_ = note;
        sendMidi(0x90, static_cast<uint8_t>(note), 108);
    }
}

void Editor::beginKnobDrag(uint32_t pid, float y, bool fine) {
    dragStartY_ = y;
    dragStartVal_ = engine_ ? engine_->getParamNormalized(pid) : 0.5f;
    fineDrag_ = fine;
    if (engine_) engine_->pushUndoState(kParamMetadata[pid].name);
}

void Editor::mouseDrag(float x, float y) {
    if (eqDragBand_ >= 0) {
        setEqNode(eqDragBand_, x, y);
        return;
    }
    if (draggedSlider_ >= 0 && draggedSlider_ < static_cast<int>(modSliders_.size())) {
        setSliderFromX(draggedSlider_, x);
        return;
    }
    const float sens = fineDrag_ ? 0.0015f : 0.0065f;
    if (draggedModKnob_ >= 0 && draggedModKnob_ < static_cast<int>(modKnobs_.size())) {
        setParam(modKnobs_[draggedModKnob_].paramId, std::clamp(dragStartVal_ + (y - dragStartY_) * sens, 0.0f, 1.0f));
        return;
    }
    if (draggedKnob_ < 0 || draggedKnob_ >= static_cast<int>(knobs_.size())) return;
    setParam(knobs_[draggedKnob_].paramId, std::clamp(dragStartVal_ + (y - dragStartY_) * sens, 0.0f, 1.0f));
}

void Editor::mouseUp(float, float) {
    draggedKnob_ = -1;
    draggedModKnob_ = -1;
    draggedSlider_ = -1;
    eqDragBand_ = -1;
    if (heldNote_ >= 0) {
        sendMidi(0x80, static_cast<uint8_t>(heldNote_), 0);
        heldNote_ = -1;
    }
    forceRedraw_ = true;
}

void Editor::rightMouseDown(float x, float y) {
    forceRedraw_ = true;
    menuOpen_ = false;
    if (browserOpen_ || aboutOpen_ || saveOpen_) return;
    for (const auto& k : (page_ == 0 ? knobs_ : modKnobs_)) {
        if (!k.rect.contains(x, y)) continue;
        menuParam_ = k.paramId;
        menuItems_ = {{"MIDI Learn", kMenuLearn}};
        const int8_t cc = engine_ ? engine_->getParamCc(k.paramId) : -1;
        if (cc >= 0) {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "Forget MIDI CC %d", cc);
            menuItems_.push_back({buf, kMenuForget});
        }
        menuItems_.push_back({"Reset to Default", kMenuReset});
        const float h = 12.0f + 24.0f * menuItems_.size();
        menuRect_ = {std::min(x, kEditorWidth - 170.0f), std::max(y - h, 4.0f), 160.0f, h};
        menuOpen_ = true;
        return;
    }
}

void Editor::runMenuAction(int action) {
    if (!engine_) return;
    switch (action) {
        case kMenuLearn: engine_->startMidiLearn(menuParam_); break;
        case kMenuForget: engine_->clearMidiMapping(menuParam_); break;
        case kMenuReset:
            engine_->pushUndoState("Reset");
            setParam(menuParam_, kParamMetadata[menuParam_].defaultValue);
            break;
        default: break;
    }
}

void Editor::scroll(float x, float y, float deltaLines) {
    if (browserOpen_ && presetListRect().contains(x, y)) {
        listScroll_ -= static_cast<int>(std::round(deltaLines));
        forceRedraw_ = true;
        return;
    }
    if (page_ != 0 && !browserOpen_ && !aboutOpen_ && !saveOpen_) {
        forceRedraw_ = true;
        if (page_ == 4 && engine_) { // over a node (or the plot): scroll sets that band's Q
            int band = -1;
            for (int b = 0; b < 4; ++b) if (eqNodes_[b].contains(x, y)) band = b;
            if (band < 0 && eqPlot_.contains(x, y)) band = eqSel_;
            if (band >= 0) {
                eqSel_ = band;
                setParam(kEqQId[band], std::clamp(engine_->getParamNormalized(kEqQId[band]) + deltaLines * 0.02f, 0.0f, 1.0f));
                return;
            }
        }
        for (const auto& st : modSteppers_) {
            if (st.rect.contains(x, y)) { stepParam(st.paramId, deltaLines > 0 ? 1 : -1); return; }
        }
        for (const auto& sl : modSliders_) {
            if (!sl.rect.contains(x, y)) continue;
            const float v = engine_ ? engine_->getParamNormalized(sl.paramId) : 0.5f;
            setParam(sl.paramId, std::clamp(v + deltaLines * 0.02f, 0.0f, 1.0f));
            return;
        }
        for (const auto& k : modKnobs_) {
            if (!k.rect.contains(x, y)) continue;
            const float v = engine_ ? engine_->getParamNormalized(k.paramId) : 0.5f;
            setParam(k.paramId, std::clamp(v + deltaLines * 0.01f, 0.0f, 1.0f));
            return;
        }
        return;
    }
    // Wheel over a knob nudges it
    if (browserOpen_ || aboutOpen_ || saveOpen_) return;
    for (const auto& k : knobs_) {
        if (!k.rect.contains(x, y)) continue;
        const float v = engine_ ? engine_->getParamNormalized(k.paramId) : 0.5f;
        setParam(k.paramId, std::clamp(v + deltaLines * 0.01f, 0.0f, 1.0f));
        return;
    }
}

bool Editor::keyChar(uint32_t cp) {
    // Typing goes to the save dialog's name, or to the preset browser's search
    std::string* target = saveOpen_ ? &saveName_ : (browserOpen_ ? &search_ : nullptr);
    if (!target || cp < 32 || cp == 127) return false;
    if (target->size() >= 48) return true;
    if (cp < 0x80) {
        *target += static_cast<char>(cp);
    } else if (cp < 0x800) {
        *target += static_cast<char>(0xC0 | (cp >> 6));
        *target += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        *target += static_cast<char>(0xE0 | (cp >> 12));
        *target += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        *target += static_cast<char>(0x80 | (cp & 0x3F));
    }
    if (!saveOpen_) listScroll_ = 0;
    forceRedraw_ = true;
    return true;
}

bool Editor::keyAction(Key k) {
    forceRedraw_ = true;
    if (saveOpen_) {
        if (k == Key::Escape) { saveOpen_ = false; return true; }
        if (k == Key::Backspace) {
            while (!saveName_.empty() && (static_cast<unsigned char>(saveName_.back()) & 0xC0) == 0x80) saveName_.pop_back();
            if (!saveName_.empty()) saveName_.pop_back();
            return true;
        }
        if (k == Key::Enter && !saveName_.empty() && engine_) {
            float values[kNumParams];
            for (uint32_t i = 0; i < kNumParams; ++i) values[i] = engine_->getParamNormalized(i);
            engine_->getPresetManager().saveUserPreset(saveName_, PresetCategory::User, "mtyas", "", values);
            saveOpen_ = false;
            category_ = PresetCategory::User;
            listScroll_ = 0;
            return true;
        }
        return true;
    }
    if (browserOpen_ && !search_.empty() && (k == Key::Escape || k == Key::Backspace)) {
        // Escape clears the search first; Backspace edits it
        if (k == Key::Escape) search_.clear();
        else {
            while (!search_.empty() && (static_cast<unsigned char>(search_.back()) & 0xC0) == 0x80) search_.pop_back();
            if (!search_.empty()) search_.pop_back();
        }
        listScroll_ = 0;
        return true;
    }
    if (k == Key::Enter && browserOpen_ && engine_) {
        // Enter loads the first match
        const auto items = visiblePresets();
        if (!items.empty()) {
            engine_->loadPreset(items[0]);
            for (uint32_t i = 0; i < kNumParams; ++i) if (host_.paramChanged) host_.paramChanged(i, engine_->getParamNormalized(i));
            browserOpen_ = false;
        }
        return true;
    }
    if (k == Key::Escape) {
        const bool any = menuOpen_ || aboutOpen_ || browserOpen_ || listOpen_;
        menuOpen_ = aboutOpen_ = browserOpen_ = listOpen_ = false;
        return any;
    }
    return false;
}

// Everything the editor shows, folded into one number: redraw only when it changes
uint64_t Editor::stateHash() const {
    uint64_t h = 1469598103934665603ull;
    auto mixIn = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    if (engine_) {
        for (uint32_t i = 0; i < kNumParams; ++i) {
            float v = engine_->getParamNormalized(i);
            uint32_t bits;
            std::memcpy(&bits, &v, 4);
            mixIn(bits);
            mixIn(static_cast<uint64_t>(engine_->getParamCc(i) + 1));
        }
        for (int n = kKbStartNote; n < kKbStartNote + kKbNumKeys; ++n) mixIn(engine_->isNoteActive(n) ? n : 0);
        mixIn(engine_->isSustainActive());
        mixIn(static_cast<uint64_t>(engine_->getLearningParamId() + 2));
        mixIn(static_cast<uint64_t>(engine_->getCurrentPreset() + 1));
        mixIn(engine_->canUndo() * 2 + engine_->canRedo());
        mixIn(static_cast<uint64_t>(engine_->getPresetManager().getPresetCount()));
        mixIn(static_cast<uint64_t>(page_));
        mixIn(listOpen_);
        // Meters move in small steps; the EQ page animates every frame while notes ring
        const ScopeSnapshot& sc = engine_->getScopeSnapshot();
        mixIn(static_cast<uint64_t>(sc.meterL * 400.0f));
        mixIn(static_cast<uint64_t>(sc.meterR * 400.0f));
        mixIn(static_cast<uint64_t>(-sc.gainReductionDb * 4.0f));
        if (page_ == 4) mixIn(sc.writePos);
        mixIn(static_cast<uint64_t>(sc.cpuLoad * 100.0f));
        mixIn(static_cast<uint64_t>(sc.activeVoices * 1000 + sc.polyphony));
        mixIn(engine_->getTuning().generation());
        mixIn(std::hash<std::string>()(search_));
        mixIn(favoritesOnly_ * 2 + abSlot_);
        for (const auto& p : engine_->getPresetManager().getPresets()) mixIn(p.isFavorite);
        mixIn(static_cast<uint64_t>(zoom_ * 100.0f));
        if (page_ == 5) mixIn(sc.grPos);
    }
    return h;
}

bool Editor::wantsRedraw() { return forceRedraw_ || stateHash() != lastHash_; }

// =============================================================================
// MODULATION & EXPRESSION PANEL
// =============================================================================
void Editor::stepParam(uint32_t id, int dir) {
    const int steps = kParamMetadata[id].steps;
    if (steps <= 0) return;
    const int cur = stepIndex(engine_ ? engine_->getParamNormalized(id) : kParamMetadata[id].defaultValue, steps);
    const int next = (cur + dir + steps + 1) % (steps + 1);
    if (engine_) engine_->pushUndoState(kParamMetadata[id].name);
    setParam(id, static_cast<float>(next) / static_cast<float>(steps));
}

void Editor::setSliderFromX(int index, float x) {
    const Rect& r = modSliders_[index].rect;
    const float trackW = r.w - 56.0f;
    setParam(modSliders_[index].paramId, std::clamp((x - r.x) / trackW, 0.0f, 1.0f));
}

bool Editor::mouseDownModPanel(float x, float y, bool fine, double timeSec) {
    const bool learning = engine_ && engine_->getLearningParamId() >= 0;

    if (page_ == 4) {
        for (int b = 0; b < 4; ++b) {
            if (!eqNodes_[b].contains(x, y)) continue;
            eqSel_ = b;
            const int key = 5000 + b;
            if (lastClickedKnob_ == key && timeSec - lastClickTime_ < 0.35) {
                setParam(kEqGainId[b], 0.5f); // double-click flattens the band
                lastClickTime_ = -1.0;
                return true;
            }
            lastClickTime_ = timeSec;
            lastClickedKnob_ = key;
            if (engine_) engine_->pushUndoState("EQ");
            eqDragBand_ = b;
            return true;
        }
    }
    for (const auto& st : modSteppers_) {
        if (!st.rect.contains(x, y)) continue;
        if (learning) { engine_->startMidiLearn(st.paramId); return true; }
        // Choices with more than two entries open a list; the chevrons at either end step through them
        if (kParamMetadata[st.paramId].steps >= 2 && x >= st.rect.x + st.rect.w * 0.2f && x <= st.rect.maxX() - st.rect.w * 0.2f) {
            openList(st.paramId, st.rect);
        } else {
            stepParam(st.paramId, kParamMetadata[st.paramId].steps >= 2 && x > st.rect.midX() ? 1 : (x < st.rect.midX() ? -1 : 1));
        }
        return true;
    }
    for (size_t i = 0; i < modSliders_.size(); ++i) {
        if (!modSliders_[i].rect.contains(x, y)) continue;
        const uint32_t pid = modSliders_[i].paramId;
        if (learning) { engine_->startMidiLearn(pid); return true; }
        const int key = 3000 + static_cast<int>(i);
        if (lastClickedKnob_ == key && timeSec - lastClickTime_ < 0.35) {
            setParam(pid, kParamMetadata[pid].defaultValue);
            lastClickTime_ = -1.0;
            return true;
        }
        lastClickTime_ = timeSec;
        lastClickedKnob_ = key;
        if (engine_) engine_->pushUndoState(kParamMetadata[pid].name);
        draggedSlider_ = static_cast<int>(i);
        setSliderFromX(draggedSlider_, x);
        return true;
    }
    for (size_t i = 0; i < modKnobs_.size(); ++i) {
        if (!modKnobs_[i].rect.contains(x, y)) continue;
        const uint32_t pid = modKnobs_[i].paramId;
        if (learning) { engine_->startMidiLearn(pid); return true; }
        const int key = 1000 + static_cast<int>(i);
        if (lastClickedKnob_ == key && timeSec - lastClickTime_ < 0.35) {
            setParam(pid, kParamMetadata[pid].defaultValue);
            lastClickTime_ = -1.0;
            return true;
        }
        lastClickTime_ = timeSec;
        lastClickedKnob_ = key;
        draggedModKnob_ = static_cast<int>(i);
        beginKnobDrag(pid, y, fine);
        return true;
    }
    return false;
}

void Editor::drawCard(Canvas& c, const Rect& r, const char* title) {
    c.fillRoundRectGradient(r, 8.0f, kPanelTop, kPanelBg);
    c.strokeRoundRect(r, 8.0f, kPanelBorder, 1.0f);
    const float ty = r.maxY() - 22.0f;
    c.fillRect({r.x + 12.0f, ty + 3.0f, 2.0f, 9.0f}, kAccent);
    c.drawText(title, r.x + 20.0f, ty + 0.5f, kPanelTitle, kTextMuted);
}

void Editor::addStepper(Canvas& c, const Rect& r, uint32_t id, bool enabled) {
    modSteppers_.push_back({id, r});
    drawStepper(c, r, id, enabled);
}

void Editor::addSlider(Canvas& c, const Rect& r, uint32_t id, bool enabled) {
    modSliders_.push_back({id, r});
    drawSlider(c, r, id, enabled);
}

void Editor::drawStepper(Canvas& c, const Rect& r, uint32_t id, bool enabled) {
    const float norm = engine_ ? engine_->getParamNormalized(id) : kParamMetadata[id].defaultValue;
    const bool armed = engine_ && engine_->getLearningParamId() == static_cast<int32_t>(id);
    c.save();
    if (!enabled) c.setAlpha(0.45f);
    c.fillRoundRect(r, 5.0f, kInsetBg);
    c.strokeRoundRect(r, 5.0f, armed ? kLearn : kPanelBorder, 1.0f);
    auto chevron = [&](float cx, float cy, float dx) {
        c.strokeLine(cx - dx, cy + 3.5f, cx, cy, kTextDim, 1.4f);
        c.strokeLine(cx, cy, cx - dx, cy - 3.5f, kTextDim, 1.4f);
    };
    chevron(r.x + 11.0f, r.midY(), -3.0f);
    chevron(r.maxX() - 11.0f, r.midY(), 3.0f);
    char buf[64];
    formatParamDisplay(id, norm, buf, sizeof(buf));
    const bool dim = norm <= 0.0001f && kParamMetadata[id].defaultValue <= 0.0001f;
    drawCentered(c, ellipsize(c, armed ? "LEARNING" : buf, r.w - 34.0f, kStripOptionOn), r, kStripOptionOn,
                 armed ? kLearn : (dim ? kTextMuted : kTextPrimary));
    c.restore();
}

void Editor::drawSlider(Canvas& c, const Rect& r, uint32_t id, bool enabled) {
    const float norm = engine_ ? engine_->getParamNormalized(id) : kParamMetadata[id].defaultValue;
    const bool armed = engine_ && engine_->getLearningParamId() == static_cast<int32_t>(id);
    c.save();
    if (!enabled) c.setAlpha(0.45f);
    const Rect track{r.x, r.midY() - 3.0f, r.w - 56.0f, 6.0f};
    c.fillRoundRect(track, 3.0f, kArcTrack);
    const float cx = track.x + track.w * 0.5f, tx = track.x + norm * track.w;
    c.fillRect({cx - 0.5f, track.y - 3.0f, 1.0f, track.h + 6.0f}, kTextDim);
    const Rect fill{std::min(cx, tx), track.y, std::fabs(tx - cx), track.h};
    if (fill.w > 0.5f) c.fillRoundRect(fill, 3.0f, armed ? kLearn : kAccent);
    c.shadowCircle(tx, track.midY(), 6.0f, 3.0f, -1.0f, kBlack.withAlpha(0.6f));
    c.fillCircleGradient(tx, track.midY(), 6.0f, {0.36f, 0.375f, 0.405f, 1.0f}, {0.16f, 0.17f, 0.19f, 1.0f});
    char buf[48];
    formatParamDisplay(id, norm, buf, sizeof(buf));
    drawCentered(c, armed ? "LEARN" : buf, {r.maxX() - 52.0f, r.y, 52.0f, r.h}, kKnobValue, armed ? kLearn : kTextMuted);
    c.restore();
}

void Editor::drawScope(Canvas& c, const Rect& r) {
    const Rect portrait{r.x + 12.0f, r.y + 8.0f, 64.0f, 64.0f};
    const Rect wave{r.x + 88.0f, r.y + 8.0f, r.w - 100.0f, 64.0f};
    for (const Rect& box : {portrait, wave}) {
        c.fillRoundRect(box, 5.0f, kInsetBg);
        c.strokeRoundRect(box, 5.0f, kPanelBorder, 1.0f);
    }
    c.fillRect({portrait.midX() - 0.5f, portrait.y + 3.0f, 1.0f, portrait.h - 6.0f}, kHairline);
    c.fillRect({portrait.x + 3.0f, portrait.midY() - 0.5f, portrait.w - 6.0f, 1.0f}, kHairline);
    c.fillRect({wave.x + 3.0f, wave.midY() - 0.5f, wave.w - 6.0f, 1.0f}, kHairline);
    if (!engine_) return;

    const ScopeSnapshot& sc = engine_->getScopeSnapshot();
    constexpr int kPoints = 256;
    float maxW = 1e-6f, maxV = 1e-6f, maxA = 1e-6f;
    for (int i = 0; i < kPoints; ++i) {
        const int idx = static_cast<int>((sc.writePos + kScopeRingSize - kPoints + i) % kScopeRingSize);
        maxW = std::max(maxW, std::fabs(sc.phaseW[idx]));
        maxV = std::max(maxV, std::fabs(sc.phaseV[idx]));
        maxA = std::max(maxA, std::fabs(sc.waveL[idx]));
    }
    c.save();
    c.clip(portrait.inset(2.0f, 2.0f));
    float px = 0, py = 0;
    for (int i = 0; i < kPoints; ++i) {
        const int idx = static_cast<int>((sc.writePos + kScopeRingSize - kPoints + i) % kScopeRingSize);
        const float x = portrait.midX() + sc.phaseW[idx] / maxW * (portrait.w * 0.45f);
        const float y = portrait.midY() + sc.phaseV[idx] / maxV * (portrait.h * 0.45f);
        if (i > 0) c.strokeLine(px, py, x, y, kAccent.withAlpha(0.25f + 0.75f * i / kPoints), 1.2f);
        px = x; py = y;
    }
    c.restore();
    c.save();
    c.clip(wave.inset(2.0f, 2.0f));
    for (int i = 0; i < kPoints; ++i) {
        const int idx = static_cast<int>((sc.writePos + kScopeRingSize - kPoints + i) % kScopeRingSize);
        const float x = wave.x + wave.w * i / (kPoints - 1);
        const float y = wave.midY() + sc.waveL[idx] / maxA * (wave.h * 0.42f);
        if (i > 0) c.strokeLine(px, py, x, y, kTextPrimary.withAlpha(0.8f), 1.2f);
        px = x; py = y;
    }
    c.restore();
    c.drawText("tip x velocity", portrait.x + 2.0f, r.y - 1.0f, kLinkSub, kTextDim);
    c.drawText("output", wave.x + 2.0f, r.y - 1.0f, kLinkSub, kTextDim);
}

void Editor::drawMeter(Canvas& c, const Rect& r, float level, bool reverse) {
    c.fillRoundRect(r, 3.0f, kInsetBg);
    constexpr int kCells = 36;
    const float cw = (r.w - 4.0f) / kCells;
    for (int i = 0; i < kCells; ++i) {
        const float pos = static_cast<float>(i) / kCells;
        const bool lit = reverse ? pos < level : pos < level;
        Color col = pos < 0.72f ? Color{0.30f, 0.80f, 0.45f, 1.0f} : pos < 0.9f ? kAccent : Color{0.95f, 0.30f, 0.25f, 1.0f};
        if (reverse) col = Color{0.55f, 0.75f, 1.0f, 1.0f};
        c.fillRect({r.x + 2.0f + i * cw, r.y + 2.0f, cw - 1.0f, r.h - 4.0f}, lit ? col : col.withAlpha(0.14f));
    }
}

// =============================================================================
// PAGES: Sound (the instrument panels), Modulation, Macros, Space, EQ, Dynamics, Play.
// Every page fills the same 868 x 502 area under the page bar, so the header, signal
// chain, keyboard and output meters stay put and the plugin reads as one instrument.
// =============================================================================
void Editor::drawPageBar(Canvas& c) {
    static const char* names[kNumPages] = {"SOUND", "MOD", "MACROS", "SPACE", "EQ", "DYNAMICS", "PLAY"};
    static const float widths[kNumPages] = {72.0f, 56.0f, 76.0f, 66.0f, 46.0f, 88.0f, 56.0f};
    float x = 16.0f;
    const float y = pageBarY_, h = 30.0f;
    float total = 0.0f;
    for (float w : widths) total += w;
    const Rect all{x, y, total, h};
    c.fillRoundRect(all, 8.0f, kPanelBg);
    c.strokeRoundRect(all, 8.0f, kPanelBorder, 1.0f);
    for (int i = 0; i < kNumPages; ++i) {
        pageTabs_[i] = {x, y, widths[i], h};
        const bool on = page_ == i;
        if (on) c.fillRoundRect(pageTabs_[i].inset(3.0f, 3.0f), 5.0f, kAccent);
        drawCentered(c, names[i], pageTabs_[i], kButton, on ? kHeaderBg : kTextMuted);
        x += widths[i];
    }

    // Output meters, always visible: peak L / R and the compressor's gain reduction
    const ScopeSnapshot* sc = engine_ ? &engine_->getScopeSnapshot() : nullptr;
    auto toLevel = [](float v) { return std::clamp((20.0f * std::log10(v + 1e-6f) + 60.0f) / 60.0f, 0.0f, 1.0f); };
    const float mx = 16.0f + all.w + 16.0f + 64.0f, mw = kEditorWidth - 16.0f - mx - 112.0f;
    // CPU (share of the real-time budget) and voices in use
    if (sc) {
        char cpu[32];
        std::snprintf(cpu, sizeof(cpu), "CPU %.0f%%", std::min(999.0f, sc->cpuLoad * 100.0f));
        c.drawText(cpu, mx - 64.0f, y + 16.0f, kLinkSub, sc->cpuLoad > 0.7f ? kAccent : kTextMuted);
        std::snprintf(cpu, sizeof(cpu), "%d / %d", sc->activeVoices, sc->polyphony);
        c.drawText(cpu, mx - 64.0f, y + 4.0f, kLinkSub, kTextDim);
    }
    c.drawText("OUT", mx, y + 10.0f, kPanelTitle, kTextMuted);
    drawMeter(c, {mx + 34.0f, y + 16.0f, mw - 34.0f, 9.0f}, toLevel(sc ? sc->meterL : 0.0f));
    drawMeter(c, {mx + 34.0f, y + 5.0f, mw - 34.0f, 9.0f}, toLevel(sc ? sc->meterR : 0.0f));
    const float gx = mx + mw + 12.0f;
    c.drawText("COMP", gx, y + 10.0f, kPanelTitle, kTextMuted);
    drawMeter(c, {gx + 40.0f, y + 10.0f, 60.0f, 10.0f}, sc ? std::clamp(-sc->gainReductionDb / 18.0f, 0.0f, 1.0f) : 0.0f, true);
}

void Editor::drawPage(Canvas& c) {
    modSteppers_.clear();
    modSliders_.clear();
    modKnobs_.clear();
    const Rect area{16.0f, 132.0f, 868.0f, 502.0f};
    switch (page_) {
        case 1: drawLabMod(c, area); break;
        case 2: drawLabMacros(c, area); break;
        case 3: drawLabSpace(c, area); break;
        case 4: drawLabEq(c, area); break;
        case 5: drawLabDynamics(c, area); break;
        case 6: drawLabPlay(c, area); break;
        default: break;
    }
    for (size_t i = 0; i < modKnobs_.size(); ++i) drawKnob(c, modKnobs_[i], draggedModKnob_ == static_cast<int>(i));
}

void Editor::drawLabMod(Canvas& c, const Rect& area) {
    auto caption = [&](const char* s, float x, float y) { c.drawText(s, x, y, kPanelTitle, kTextDim); };
    const float topY = area.maxY();

    // --- Two LFOs -------------------------------------------------------------
    const uint32_t lfoParams[2][3] = {{kParamLfo1Rate, kParamLfo1Shape, kParamLfo1Sync},
                                      {kParamLfo2Rate, kParamLfo2Shape, kParamLfo2Sync}};
    for (int i = 0; i < 2; ++i) {
        const Rect card{area.x + i * 438.0f, topY - 106.0f, 430.0f, 106.0f};
        drawCard(c, card, i == 0 ? "LFO 1" : "LFO 2");
        modKnobs_.push_back({lfoParams[i][0], {card.x + 14.0f, card.y + 4.0f, 90.0f, 73.0f}, 15.0f, false});
        caption("SHAPE", card.x + 134.0f, card.y + 58.0f);
        addStepper(c, {card.x + 134.0f, card.y + 28.0f, 134.0f, 26.0f}, lfoParams[i][1]);
        caption("TEMPO SYNC", card.x + 282.0f, card.y + 58.0f);
        addStepper(c, {card.x + 282.0f, card.y + 28.0f, 134.0f, 26.0f}, lfoParams[i][2]);
    }
    const float colTop = topY - 106.0f - 8.0f;
    const float colH = colTop - area.y;

    // --- Modulation matrix ----------------------------------------------------
    const Rect matrix{area.x, area.y, 560.0f, colH};
    drawCard(c, matrix, "MODULATION MATRIX");
    caption("SOURCE", matrix.x + 36.0f, matrix.maxY() - 44.0f);
    caption("TARGET", matrix.x + 206.0f, matrix.maxY() - 44.0f);
    caption("AMOUNT", matrix.x + 398.0f, matrix.maxY() - 44.0f);
    for (int i = 0; i < kNumModSlots; ++i) {
        const uint32_t base = kFirstModSlotParam + static_cast<uint32_t>(i) * 3;
        const float rowY = matrix.maxY() - 54.0f - 44.0f * (i + 1) + 12.0f;
        const bool on = engine_ && stepIndex(engine_->getParamNormalized(base), kNumModSources - 1) != kSrcNone;
        char idx[4];
        std::snprintf(idx, sizeof(idx), "%d", i + 1);
        c.drawText(idx, matrix.x + 16.0f, rowY + 7.0f, kKnobLabel, on ? kAccent : kTextDim);
        addStepper(c, {matrix.x + 34.0f, rowY, 140.0f, 26.0f}, base);
        const float ax = matrix.x + 180.0f, ay = rowY + 13.0f;
        c.strokeLine(ax, ay, ax + 16.0f, ay, on ? kAccent : kTextDim, 1.5f);
        c.strokeLine(ax + 11.0f, ay + 4.0f, ax + 16.0f, ay, on ? kAccent : kTextDim, 1.5f);
        c.strokeLine(ax + 16.0f, ay, ax + 11.0f, ay - 4.0f, on ? kAccent : kTextDim, 1.5f);
        addStepper(c, {matrix.x + 204.0f, rowY, 180.0f, 26.0f}, base + 1, on);
        addSlider(c, {matrix.x + 396.0f, rowY + 2.0f, 150.0f, 22.0f}, base + 2, on);
    }
    {
        float y = matrix.y + 40.0f;
        for (const auto& line : wrap(c, "Each slot adds its source, scaled by Amount, to the target knob. LFOs swing both ways; "
                                        "the other sources run from 0 upward. Targets keep their panel setting as the centre.",
                                     matrix.w - 32.0f, kAboutBody)) {
            c.drawText(line, matrix.x + 16.0f, y, kAboutBody, kTextDim);
            y -= c.lineHeight(kAboutBody) + 1.0f;
        }
    }

    // --- Expression and tempo sync --------------------------------------------
    const float colX = area.x + 568.0f, colW = area.w - 568.0f;
    auto row = [&](const Rect& card, int k, const char* label) {
        const float cy = card.maxY() - 48.0f - 34.0f * k;
        c.drawText(label, card.x + 16.0f, cy - 6.0f, kKnobLabel, kTextMuted);
        return Rect{card.x + 104.0f, cy - 12.0f, card.w - 120.0f, 24.0f};
    };
    const Rect expr{colX, colTop - 190.0f, colW, 190.0f};
    drawCard(c, expr, "EXPRESSION (MPE)");
    addStepper(c, row(expr, 0, "MPE"), kParamMpeMode);
    addStepper(c, row(expr, 1, "Bend range"), kParamBendRange);
    addSlider(c, row(expr, 2, "Pressure"), kParamPressureAmt);
    addSlider(c, row(expr, 3, "Slide"), kParamSlideAmt);
    c.drawText("Pressure closes the pickup, slide opens the coil", expr.x + 16.0f, expr.y + 8.0f, kLinkSub, kTextDim);
    const Rect sync{colX, area.y, colW, colH - 190.0f - 8.0f};
    drawCard(c, sync, "TEMPO SYNC & QUALITY");
    addStepper(c, row(sync, 0, "Tremolo"), kParamTremSync);
    addStepper(c, row(sync, 1, "Phaser"), kParamPhaserSync);
    addStepper(c, row(sync, 2, "Echo"), kParamEchoSync);
    addStepper(c, row(sync, 3, "Oversampling"), kParamQuality);
}

void Editor::drawLabMacros(Canvas& c, const Rect& area) {
    const Rect card{area.x, area.maxY() - 150.0f, area.w, 150.0f};
    drawCard(c, card, "IMPOSSIBLE INSTRUMENT");
    const float cw = card.w / 7.0f;
    auto knobAt = [&](int col, uint32_t id) {
        modKnobs_.push_back({id, {card.x + cw * col + 9.0f, card.y + 10.0f, cw - 18.0f, 89.0f}, 23.0f, false});
    };
    knobAt(0, kParamMaterial);
    knobAt(2, kParamPickupSpread);
    knobAt(3, kParamGapFmDepth);
    knobAt(4, kParamGapFmRatio);
    knobAt(6, kParamResMorph);
    drawCentered(c, "Pickups", {card.x + cw, card.y + 76.0f, cw, 14.0f}, kKnobLabel, kTextPrimary);
    addStepper(c, {card.x + cw + 14.0f, card.y + 44.0f, cw - 28.0f, 26.0f}, kParamPickupCount);
    drawCentered(c, "Ratio Mode", {card.x + cw * 5, card.y + 76.0f, cw, 14.0f}, kKnobLabel, kTextPrimary);
    addStepper(c, {card.x + cw * 5 + 14.0f, card.y + 44.0f, cw - 28.0f, 26.0f}, kParamGapFmExact);

    const Rect help{area.x, area.y, 560.0f, area.h - 150.0f - 8.0f};
    drawPartialMap(c, {area.x + 568.0f, area.y, area.w - 568.0f, help.h});
    drawCard(c, help, "WHAT THEY DO");
    float y = help.maxY() - 46.0f;
    const struct { const char* head; const char* body; } items[] = {
        {"Material", "Steel to glass to carbon fibre. A longer ring, stretched, inharmonic bell overtones and a louder chime as the knob moves right, even with the Chime knob low. Applies to tines and reeds."},
        {"Pickups and Spread", "Put one to four magnetic pickups along the tine and sum them in phase; Spread sets how far apart they sit. The tine's bark moves from the single-pickup law to a comb-like blend. (Magnetic pickup only.)"},
        {"Gap FM and Ratio", "Swings the pickup gap and offset at an audio-rate multiple of every note's pitch. Small amounts add shimmer; large amounts turn into bell-like FM, and it stays tuned because it follows the note. Ratio Mode Exact snaps the ratio to 1:4, 1:2, 1:1, 2:1 .. 8:1 for harmonic tones."},
        {"Morph", "Crossfades from the selected resonator (tine or reed) to the other one while the note rings. Have it above zero when you play the note; automate or modulate it to sweep from Rhodes to Wurlitzer within a held chord."}};
    for (const auto& it : items) {
        c.drawText(it.head, help.x + 20.0f, y, kAboutHead, kTextPrimary);
        y -= 4.0f;
        for (const auto& line : wrap(c, it.body, help.w - 40.0f, kAboutBody)) {
            y -= c.lineHeight(kAboutBody) + 1.0f;
            c.drawText(line, help.x + 20.0f, y, kAboutBody, kTextMuted);
        }
        y -= 24.0f;
    }
}

// ---- shared curve grid: log frequency (20 Hz .. 20 kHz) against dB -------------------------
void Editor::drawCurveGrid(Canvas& c, const Rect& plot, float dbLo, float dbHi, float dbStep) {
    c.fillRoundRect(plot, 6.0f, kInsetBg);
    c.strokeRoundRect(plot, 6.0f, kPanelBorder, 1.0f);
    const float freqs[10] = {20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000};
    const char* names[10] = {"20", "50", "100", "200", "500", "1k", "2k", "5k", "10k", "20k"};
    for (int i = 0; i < 10; ++i) {
        const float x = plot.x + plot.w * std::log10(freqs[i] / 20.0f) / 3.0f;
        if (i > 0 && i < 9) c.fillRect({x - 0.5f, plot.y + 1.0f, 1.0f, plot.h - 2.0f}, kHairline.withAlpha(0.7f));
        const float tw = c.textWidth(names[i], kLinkSub);
        c.drawText(names[i], std::clamp(x - tw * 0.5f, plot.x + 3.0f, plot.maxX() - tw - 3.0f), plot.y + 3.0f, kLinkSub, kTextDim);
    }
    for (float db = std::ceil(dbLo / dbStep) * dbStep; db <= dbHi + 0.01f; db += dbStep) {
        const float y = plot.y + plot.h * (db - dbLo) / (dbHi - dbLo);
        c.fillRect({plot.x + 1.0f, y - 0.5f, plot.w - 2.0f, 1.0f}, std::fabs(db) < 0.01f ? kTextDim.withAlpha(0.7f) : kHairline.withAlpha(0.7f));
        if (db - dbLo < 0.5f * dbStep || dbHi - db < 0.5f * dbStep) continue; // edge labels would sit on the frequency scale
        char b[12];
        std::snprintf(b, sizeof(b), "%+.0f", db);
        c.drawText(b, plot.x + 4.0f, y + 2.0f, kLinkSub, kTextDim);
    }
}

// ---- Space: reverb, echo and the miked cabinet ------------------------------------------------
void Editor::drawLabSpace(Canvas& c, const Rect& area) {
    const Rect cab{area.x, area.y, 560.0f, area.h};
    drawCard(c, cab, "CABINET & MICROPHONE");
    const int type = engine_ ? stepIndex(engine_->getParamNormalized(kParamCabType), 9) : 0;
    c.drawText("Cabinet", cab.x + 18.0f, cab.maxY() - 56.0f, kKnobLabel, kTextMuted);
    addStepper(c, {cab.x + 80.0f, cab.maxY() - 62.0f, 250.0f, 26.0f}, kParamCabType);
    const Rect plot{cab.x + 14.0f, cab.y + 118.0f, cab.w - 28.0f, cab.h - 62.0f - 118.0f - 16.0f};
    drawCurveGrid(c, plot, -24.0f, 18.0f, 6.0f);
    if (type == 0) {
        drawCentered(c, "Speaker (200A): OpenWurli's speaker model. The Cabinet knob sets its character.", plot, kAboutBody, kTextDim);
    } else {
        const float mic = engine_->getParamNormalized(kParamCabMic), dist = engine_->getParamNormalized(kParamCabDistance);
        float px = 0, py = 0;
        constexpr int kPts = 240;
        for (int i = 0; i <= kPts; ++i) {
            const float f = 20.0f * std::pow(1000.0f, static_cast<float>(i) / kPts);
            const float db = std::clamp(static_cast<float>(CabinetModel::responseDb(type, mic, dist, f)), -24.0f, 18.0f);
            const float x = plot.x + plot.w * i / kPts, y = plot.y + plot.h * (db + 24.0f) / 42.0f;
            c.fillRect({x, plot.y + plot.h * 24.0f / 42.0f, 1.2f, y - (plot.y + plot.h * 24.0f / 42.0f)}, kAccent.withAlpha(0.10f));
            if (i > 0) c.strokeLine(px, py, x, y, kAccent, 2.2f);
            px = x; py = y;
        }
    }
    const float cw = cab.w / 4.0f;
    const uint32_t ids[4] = {kParamCabMic, kParamCabDistance, kParamCabRoom, kParamCabinet};
    for (int i = 0; i < 4; ++i) modKnobs_.push_back({ids[i], {cab.x + cw * i + 12.0f, cab.y + 10.0f, cw - 24.0f, 77.0f}, 17.0f, false});

    const float colX = area.x + 568.0f, colW = area.w - 568.0f;
    const Rect rev{colX, area.maxY() - 210.0f, colW, 210.0f};
    drawCard(c, rev, "REVERB");
    c.drawText("Type", rev.x + 16.0f, rev.maxY() - 56.0f, kKnobLabel, kTextMuted);
    addStepper(c, {rev.x + 70.0f, rev.maxY() - 62.0f, colW - 86.0f, 26.0f}, kParamReverbType);
    const float rw = colW / 3.0f;
    const uint32_t rids[3] = {kParamReverbMix, kParamReverbDecay, kParamReverbPreDelay};
    for (int i = 0; i < 3; ++i) modKnobs_.push_back({rids[i], {rev.x + rw * i + 6.0f, rev.y + 12.0f, rw - 12.0f, 89.0f}, 20.0f, false});
    const Rect echo{colX, area.y, colW, area.h - 210.0f - 8.0f};
    drawCard(c, echo, "TAPE ECHO & CHORUS");
    c.drawText("Sync", echo.x + 16.0f, echo.maxY() - 56.0f, kKnobLabel, kTextMuted);
    addStepper(c, {echo.x + 70.0f, echo.maxY() - 62.0f, colW - 86.0f, 26.0f}, kParamEchoSync);
    c.drawText("Chorus", echo.x + 16.0f, echo.maxY() - 92.0f, kKnobLabel, kTextMuted);
    addStepper(c, {echo.x + 70.0f, echo.maxY() - 98.0f, colW - 86.0f, 26.0f}, kParamChorusMode);
    const float ew = colW / 3.0f;
    const uint32_t eids[3] = {kParamDelayMix, kParamDelayTime, kParamDelayFeedback};
    for (int i = 0; i < 3; ++i) modKnobs_.push_back({eids[i], {echo.x + ew * i + 6.0f, echo.y + 12.0f, ew - 12.0f, 77.0f}, 17.0f, false});
}

// ---- EQ: four-band parametric mastering equaliser with live spectrum -------------------------
void Editor::setEqNode(int band, float x, float y) {
    if (!engine_) return;
    const float f = 20.0f * std::pow(1000.0f, std::clamp((x - eqPlot_.x) / eqPlot_.w, 0.0f, 1.0f));
    const float db = (std::clamp((y - eqPlot_.y) / eqPlot_.h, 0.0f, 1.0f) - 0.5f) * 30.0f; // plot spans +-15 dB
    setParam(kEqFreqId[band], eqBandFreqNorm(band, f));
    setParam(kEqGainId[band], std::clamp(db / 24.0f + 0.5f, 0.0f, 1.0f));
}

void Editor::drawLabEq(Canvas& c, const Rect& area) {
    const Rect card{area.x, area.maxY() - 304.0f, area.w, 304.0f};
    drawCard(c, card, "MASTERING EQ");
    eqPlot_ = {card.x + 12.0f, card.y + 12.0f, card.w - 24.0f, card.h - 42.0f};
    drawCurveGrid(c, eqPlot_, -15.0f, 15.0f, 5.0f);

    float pv[kNumParams];
    for (uint32_t i = 0; i < kNumParams; ++i) pv[i] = engine_ ? engine_->getParamNormalized(i) : kParamMetadata[i].defaultValue;
    auto xOf = [&](float f) { return eqPlot_.x + eqPlot_.w * std::log10(f / 20.0f) / 3.0f; };
    auto yOf = [&](float db) { return eqPlot_.y + eqPlot_.h * (db + 15.0f) / 30.0f; };

    // Live spectrum behind the curve (fast attack, slow fall)
    if (engine_) {
        float db[kSpecPts];
        computeSpectrumDb(engine_->analyzerData(), engine_->analyzerPos(), kAnalyzerSize, engine_->sampleRate(), db, kSpecPts);
        for (int j = 0; j < kSpecPts; ++j) {
            eqSpec_[j] = std::max(db[j], eqSpec_[j] - 1.8f);
            const float x0 = eqPlot_.x + eqPlot_.w * j / kSpecPts, x1 = eqPlot_.x + eqPlot_.w * (j + 1) / kSpecPts;
            const float h = std::clamp((eqSpec_[j] + 90.0f) / 84.0f, 0.0f, 1.0f) * (eqPlot_.h - 4.0f);
            c.fillRect({x0, eqPlot_.y + 2.0f, x1 - x0 + 0.5f, h}, {0.55f, 0.65f, 0.85f, 0.22f});
        }
    }

    // Band responses and the sum
    wurli::Biquad bq[4];
    bool active[4];
    for (int b = 0; b < 4; ++b) { setupEqBand(bq[b], b, pv, 48000.0); active[b] = std::fabs(eqGainDb(pv[kEqGainId[b]])) > 0.05f; }
    constexpr int kPts = 260;
    float px = 0, py = 0, bx[4] = {}, by[4] = {};
    const float y0 = yOf(0.0f);
    for (int i = 0; i <= kPts; ++i) {
        const float f = 20.0f * std::pow(1000.0f, static_cast<float>(i) / kPts);
        const float x = eqPlot_.x + eqPlot_.w * i / kPts;
        float total = 0.0f;
        for (int b = 0; b < 4; ++b) {
            const float db = active[b] ? 20.0f * std::log10(static_cast<float>(bq[b].magnitude(f, 48000.0)) + 1e-9f) : 0.0f;
            total += db;
            const float y = yOf(std::clamp(db, -15.0f, 15.0f));
            if (i > 0 && (active[b] || b == eqSel_)) c.strokeLine(bx[b], by[b], x, y, kBandColor[b].withAlpha(b == eqSel_ ? 0.55f : 0.28f), 1.2f);
            bx[b] = x; by[b] = y;
        }
        const float y = yOf(std::clamp(total, -15.0f, 15.0f));
        c.fillRect({x, std::min(y, y0), 1.2f, std::fabs(y - y0)}, kAccent.withAlpha(0.13f));
        if (i > 0) c.strokeLine(px, py, x, y, kTextPrimary, 2.4f);
        px = x; py = y;
    }

    // Draggable nodes
    for (int b = 0; b < 4; ++b) {
        const float cx = xOf(eqBandFreqHz(b, pv[kEqFreqId[b]])), cy = yOf(std::clamp(eqGainDb(pv[kEqGainId[b]]), -15.0f, 15.0f));
        eqNodes_[b] = {cx - 14.0f, cy - 14.0f, 28.0f, 28.0f};
        const bool sel = b == eqSel_;
        if (sel) c.glowCircle(cx, cy, 8.0f, 12.0f, kBandColor[b].withAlpha(0.7f));
        c.fillCircle(cx, cy, sel ? 8.0f : 6.5f, kBandColor[b]);
        c.strokeCircle(cx, cy, sel ? 8.0f : 6.5f, kBlack.withAlpha(0.5f), 1.0f);
        char n[4];
        std::snprintf(n, sizeof(n), "%d", b + 1);
        drawCentered(c, n, {cx - 8.0f, cy - 8.0f, 16.0f, 16.0f}, kCcTag, kBlack);
    }
    // Readout for the selected band
    {
        const int b = eqSel_;
        char buf[96], fb[24];
        formatParamDisplay(kEqFreqId[b], pv[kEqFreqId[b]], fb, sizeof(fb));
        const bool shelf = kEqShelfId[b] < kNumParams && stepIndex(pv[kEqShelfId[b]], 1) == 1;
        const char* nm[4] = {"LOW", "LOW MID", "HIGH MID", "HIGH"};
        std::snprintf(buf, sizeof(buf), "%s %s   %s   %+.1f dB   Q %.2f", nm[b], shelf ? "SHELF" : "BELL", fb, eqGainDb(pv[kEqGainId[b]]), eqQ(pv[kEqQId[b]]));
        const float tw = c.textWidth(buf, kKnobValue);
        c.drawText(buf, card.maxX() - 20.0f - tw, card.maxY() - 22.0f, kKnobValue, kBandColor[b]);
        c.drawText("Drag the points. Scroll over one for Q. Double-click to reset a band.", card.x + 150.0f, card.maxY() - 22.0f, kLinkSub, kTextDim);
    }

    // Band cards
    const float gap = 12.0f, w = (area.w - 3.0f * gap) / 4.0f;
    const char* titles[4] = {"BAND 1  LOW", "BAND 2  LOW MID", "BAND 3  HIGH MID", "BAND 4  HIGH"};
    for (int b = 0; b < 4; ++b) {
        const Rect bc{area.x + b * (w + gap), area.y, w, area.h - 304.0f - 8.0f};
        drawCard(c, bc, titles[b]);
        c.fillRect({bc.x + 12.0f, bc.maxY() - 19.0f, 2.0f, 9.0f}, kBandColor[b]);
        if (kEqShelfId[b] < kNumParams) addStepper(c, {bc.x + 12.0f, bc.maxY() - 56.0f, w - 24.0f, 24.0f}, kEqShelfId[b]);
        else drawCentered(c, "Bell", {bc.x + 12.0f, bc.maxY() - 56.0f, w - 24.0f, 24.0f}, kKnobLabel, kTextDim);
        const uint32_t ids[3] = {kEqFreqId[b], kEqGainId[b], kEqQId[b]};
        for (int k = 0; k < 3; ++k) modKnobs_.push_back({ids[k], {bc.x + 4.0f + (w - 8.0f) / 3.0f * k, bc.y + 10.0f, (w - 8.0f) / 3.0f, 77.0f}, 17.0f, k == 1});
    }
}

// ---- Dynamics: compressor and limiter ------------------------------------------------------------
void Editor::drawLabDynamics(Canvas& c, const Rect& area) {
    const ScopeSnapshot* sc = engine_ ? &engine_->getScopeSnapshot() : nullptr;
    auto P = [&](uint32_t id) { return engine_ ? engine_->getParamNormalized(id) : kParamMetadata[id].defaultValue; };
    const float thr = compThreshDb(P(kParamCompThresh)), ratio = compRatio(P(kParamCompRatio)), knee = compKneeDb(P(kParamCompKnee));
    const bool autoGain = stepIndex(P(kParamCompAuto), 1) == 1;
    const float makeup = compMakeupDb(P(kParamCompMakeup)) + (autoGain ? -thr * (1.0f - 1.0f / ratio) * 0.5f : 0.0f);
    const float mix = P(kParamCompAmount);

    const float topH = 232.0f;
    const Rect curve{area.x, area.maxY() - topH, 330.0f, topH};
    drawCard(c, curve, "TRANSFER CURVE");
    const float side = curve.h - 50.0f;
    const Rect plot{curve.x + 18.0f, curve.y + 14.0f, side, side};
    c.fillRoundRect(plot, 6.0f, kInsetBg);
    c.strokeRoundRect(plot, 6.0f, kPanelBorder, 1.0f);
    for (int g = -60; g <= 0; g += 12) {
        const float t = (g + 60.0f) / 60.0f;
        c.fillRect({plot.x + plot.w * t - 0.5f, plot.y + 1.0f, 1.0f, plot.h - 2.0f}, kHairline.withAlpha(0.7f));
        c.fillRect({plot.x + 1.0f, plot.y + plot.h * t - 0.5f, plot.w - 2.0f, 1.0f}, kHairline.withAlpha(0.7f));
    }
    auto X = [&](float db) { return plot.x + plot.w * std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f); };
    auto Y = [&](float db) { return plot.y + plot.h * std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f); };
    c.strokeLine(X(-60.0f), Y(-60.0f), X(0.0f), Y(0.0f), kTextDim.withAlpha(0.6f), 1.0f);
    c.strokeLine(X(thr), plot.y + 1.0f, X(thr), plot.maxY() - 1.0f, kLearn.withAlpha(0.5f), 1.0f); // threshold marker
    float px = 0, py = 0;
    for (int i = 0; i <= 90; ++i) {
        const float in = -60.0f + 60.0f * i / 90.0f;
        const float out = in + (mix > 0.001f ? mix * (compStaticGrDb(in - thr, ratio, knee) + makeup) : 0.0f);
        if (i > 0) c.strokeLine(px, py, X(in), Y(out), kAccent, 2.4f);
        px = X(in); py = Y(out);
    }
    if (sc && mix > 0.001f && sc->compLevelDb > -60.0f) {
        const float in = sc->compLevelDb;
        const float out = in + sc->gainReductionDb + makeup;
        c.glowCircle(X(in), Y(out), 4.0f, 6.0f, kLearn.withAlpha(0.8f));
        c.fillCircle(X(in), Y(out), 4.0f, kLearn);
    }
    c.drawText("input dB", plot.x + 4.0f, plot.y - 11.0f, kLinkSub, kTextDim);
    c.drawText("output", plot.x + 4.0f, plot.maxY() + 2.0f, kLinkSub, kTextDim);
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.1f dB", sc ? sc->gainReductionDb : 0.0f);
    c.drawText("GAIN REDUCTION", plot.maxX() + 18.0f, plot.maxY() - 18.0f, kPanelTitle, kTextDim);
    c.drawText(buf, plot.maxX() + 18.0f, plot.maxY() - 38.0f, kModalTitle, kAccent);
    std::snprintf(buf, sizeof(buf), "%.1f dB", sc ? sc->limiterReductionDb : 0.0f);
    c.drawText("LIMITER", plot.maxX() + 18.0f, plot.maxY() - 68.0f, kPanelTitle, kTextDim);
    c.drawText(buf, plot.maxX() + 18.0f, plot.maxY() - 86.0f, kKnobValue, kTextMuted);

    // Gain-reduction history
    const Rect hist{curve.maxX() + 12.0f, curve.y, area.w - curve.w - 12.0f, topH};
    drawCard(c, hist, "GAIN REDUCTION HISTORY  (about 2 s)");
    const Rect hp{hist.x + 14.0f, hist.y + 14.0f, hist.w - 28.0f, hist.h - 50.0f};
    c.fillRoundRect(hp, 6.0f, kInsetBg);
    c.strokeRoundRect(hp, 6.0f, kPanelBorder, 1.0f);
    for (int g = 0; g <= 18; g += 6) {
        const float y = hp.maxY() - hp.h * g / 24.0f;
        c.fillRect({hp.x + 1.0f, y - 0.5f, hp.w - 2.0f, 1.0f}, kHairline.withAlpha(0.7f));
        char b[8];
        std::snprintf(b, sizeof(b), "-%d", g);
        c.drawText(b, hp.x + 4.0f, y - 11.0f, kLinkSub, kTextDim);
    }
    if (sc) {
        float lx = 0, ly = 0;
        for (int i = 0; i < kGrHistory; ++i) {
            const float v = sc->grHistory[(sc->grPos + static_cast<uint32_t>(i)) % kGrHistory];
            const float x = hp.x + hp.w * i / (kGrHistory - 1), y = hp.maxY() - hp.h * std::clamp(-v / 24.0f, 0.0f, 1.0f);
            c.fillRect({x, y, hp.w / kGrHistory + 0.6f, hp.maxY() - y}, kAccent.withAlpha(0.20f));
            if (i > 0) c.strokeLine(lx, ly, x, y, kAccent, 1.8f);
            lx = x; ly = y;
        }
    }

    // Compressor controls
    const float rowH = 150.0f;
    const Rect comp{area.x, area.maxY() - topH - 8.0f - rowH, area.w, rowH};
    drawCard(c, comp, "COMPRESSOR");
    const uint32_t ids[8] = {kParamCompThresh, kParamCompRatio, kParamCompAttack, kParamCompRelease, kParamCompKnee, kParamCompMakeup, kParamCompHpf, kParamCompAmount};
    const float cw = 84.0f;
    for (int i = 0; i < 8; ++i) modKnobs_.push_back({ids[i], {comp.x + 10.0f + cw * i, comp.y + 8.0f, cw - 6.0f, 89.0f}, 23.0f, false});
    const float sx = comp.x + 10.0f + cw * 8.0f + 10.0f, sw = comp.maxX() - sx - 14.0f;
    c.drawText("DETECTOR", sx, comp.maxY() - 52.0f, kPanelTitle, kTextDim);
    addStepper(c, {sx, comp.maxY() - 80.0f, sw, 24.0f}, kParamCompDetect);
    c.drawText("AUTO GAIN", sx, comp.maxY() - 100.0f, kPanelTitle, kTextDim);
    addStepper(c, {sx, comp.maxY() - 128.0f, sw, 24.0f}, kParamCompAuto);

    // Limiter
    const Rect lim{area.x, area.y, area.w, area.h - topH - 8.0f - rowH - 8.0f};
    drawCard(c, lim, "OUTPUT LIMITER");
    c.drawText("State", lim.x + 20.0f, lim.maxY() - 60.0f, kKnobLabel, kTextMuted);
    addStepper(c, {lim.x + 70.0f, lim.maxY() - 66.0f, 120.0f, 26.0f}, kParamLimOn);
    modKnobs_.push_back({kParamLimCeil, {lim.x + 220.0f, lim.y + 8.0f, 90.0f, 73.0f}, 15.0f, false});
    c.drawText("A fast peak limiter after the master volume: nothing leaves above the ceiling.", lim.x + 340.0f, lim.y + 44.0f, kAboutBody, kTextDim);
    c.drawText("The compressor Mix knob blends compressed and dry signal (parallel compression).", lim.x + 340.0f, lim.y + 28.0f, kAboutBody, kTextDim);
}

// ---- Macros: partial map of the resonator ------------------------------------------------------
void Editor::drawPartialMap(Canvas& c, const Rect& card) {
    drawCard(c, card, "PARTIAL MAP  (illustrative)");
    const Rect plot{card.x + 14.0f, card.y + 28.0f, card.w - 28.0f, card.h - 62.0f};
    c.fillRoundRect(plot, 6.0f, kInsetBg);
    c.strokeRoundRect(plot, 6.0f, kPanelBorder, 1.0f);
    if (!engine_) return;
    auto P = [&](uint32_t id) { return engine_->getParamNormalized(id); };
    const float mat = P(kParamMaterial), bell = std::min(1.0f, P(kParamTineBell) + materialBellLift(mat)), tb = P(kParamToneBarCoupling);
    const float stretch = materialBellStretch(mat), chime = materialChimeMul(mat);
    // ratio, relative level, ring time (s)
    const float base = tineBaseT60(P(kParamTineDecay)) * materialDecayMul(mat);
    const float parts[5][3] = {
        {1.0f, 1.0f, base},
        {2.0f, 0.02f + 0.35f * tb, base * (0.45f + 0.45f * tb)},
        {3.0f, 0.02f + 0.25f * tb, base * (0.30f + 0.35f * tb)},
        {6.27f * stretch, std::min(1.0f, bell * (0.15f + 0.22f * bell) * 20.0f * chime), (0.025f + 0.6f * bell * bell) * materialBellDecayMul(mat)},
        {17.55f * stretch, std::min(1.0f, bell * 0.12f * 10.0f * chime), (0.01f + 0.065f * bell) * materialBellDecayMul(mat)}};
    float topLevel = 1e-4f;
    for (const auto& pt : parts) topLevel = std::max(topLevel, pt[1]);
    for (int i = 0; i < 5; ++i) {
        const float x = plot.x + plot.w * (0.04f + 0.92f * std::log10(parts[i][0] * 1.0f) / 1.3f);
        const float db = 20.0f * std::log10((parts[i][1] + 1e-4f) / topLevel);
        const float h = std::clamp((db + 36.0f) / 36.0f, 0.02f, 1.0f) * (plot.h - 22.0f);
        const float ring = std::clamp(std::log10(parts[i][2] * 20.0f + 1.0f) / 2.2f, 0.12f, 1.0f); // brighter = rings longer
        c.fillRoundRect({x - 7.0f, plot.y + 14.0f, 14.0f, h}, 3.0f, kAccent.withAlpha(0.25f + 0.75f * ring));
        char b[16];
        std::snprintf(b, sizeof(b), "x%.1f", parts[i][0]);
        c.drawText(b, x - c.textWidth(b, kLinkSub) * 0.5f, plot.y + 2.0f, kLinkSub, kTextMuted);
    }
    c.drawText("height = level (36 dB range), brightness = ring time", card.x + 16.0f, card.y + 10.0f, kLinkSub, kTextDim);
}


void Editor::openList(uint32_t id, const Rect& anchor) {
    listOpen_ = true;
    listParam_ = id;
    listAnchor_ = anchor;
    const int n = kParamMetadata[id].steps + 1;
    listRows_ = std::min(n, 12);
    listCols_ = (n + listRows_ - 1) / listRows_;
}

void Editor::drawList(Canvas& c) {
    const int steps = kParamMetadata[listParam_].steps, n = steps + 1;
    const float cur = engine_ ? engine_->getParamNormalized(listParam_) : 0.0f;
    const int curIdx = stepIndex(cur, steps);
    std::vector<std::string> items(n);
    float colW = std::max(listAnchor_.w, 110.0f);
    for (int i = 0; i < n; ++i) {
        char b[64];
        formatParamDisplay(listParam_, static_cast<float>(i) / steps, b, sizeof(b));
        items[i] = b;
        colW = std::max(colW, c.textWidth(items[i], kListItem) + 34.0f);
    }
    listColW_ = colW;
    const float itemH = 24.0f, w = listCols_ * colW + 8.0f, h = listRows_ * itemH + 8.0f;
    float x = std::min(listAnchor_.x, kEditorWidth - w - 8.0f);
    float y = listAnchor_.y - h - 3.0f;
    if (y < 6.0f) y = std::min(listAnchor_.maxY() + 3.0f, kEditorHeight - h - 6.0f);
    listRect_ = {std::max(8.0f, x), y, w, h};
    c.shadowRoundRect(listRect_, 8.0f, 14.0f, -6.0f, kBlack.withAlpha(0.7f));
    c.fillRoundRect(listRect_, 8.0f, kPanelBg);
    c.strokeRoundRect(listRect_, 8.0f, kAccent.withAlpha(0.6f), 1.0f);
    for (int i = 0; i < n; ++i) {
        const int col = i / listRows_, row = i % listRows_;
        const Rect r{listRect_.x + 4.0f + col * colW, listRect_.maxY() - 4.0f - (row + 1) * itemH, colW, itemH};
        const bool on = i == curIdx;
        if (on) c.fillRoundRect(r.inset(2.0f, 1.0f), 4.0f, kAccent.withAlpha(0.20f));
        c.drawText(items[i], r.x + 14.0f, r.y + 6.0f, on ? kListItemOn : kListItem, on ? kAccent : kTextPrimary);
        if (col > 0 && row == 0) c.fillRect({r.x - 1.0f, listRect_.y + 6.0f, 1.0f, listRect_.h - 12.0f}, kHairline);
    }
}

bool Editor::mouseDownList(float x, float y) {
    if (listRect_.contains(x, y)) {
        const int col = static_cast<int>((x - listRect_.x - 4.0f) / listColW_);
        const int row = static_cast<int>((listRect_.maxY() - 4.0f - y) / 24.0f);
        const int idx = col * listRows_ + row;
        const int steps = kParamMetadata[listParam_].steps;
        if (col >= 0 && row >= 0 && row < listRows_ && idx >= 0 && idx <= steps) {
            if (engine_) engine_->pushUndoState(kParamMetadata[listParam_].name);
            setParam(listParam_, static_cast<float>(idx) / static_cast<float>(steps));
        }
    }
    listOpen_ = false;
    return true;
}

// ---- Play: tuning (Scala microtuning), playing realism, polyphony and the editor ------------
void Editor::refreshTunings() {
    tunings_ = availableTunings();
    forceRedraw_ = true;
}

void Editor::selectTuning(int index) {
    if (!engine_ || tunings_.empty()) return;
    const int n = static_cast<int>(tunings_.size());
    index = ((index % n) + n) % n;
    tuningError_.clear();
    if (!applyTuning(engine_->getTuning(), tunings_[static_cast<size_t>(index)], &tuningError_)) {
        tuningError_ = tunings_[static_cast<size_t>(index)].name + ": " + tuningError_;
    }
    forceRedraw_ = true;
}

void Editor::drawLabPlay(Canvas& c, const Rect& area) {
    if (tunings_.empty()) refreshTunings();
    auto caption = [&](const char* s, float x, float y) { c.drawText(s, x, y, kPanelTitle, kTextDim); };
    auto chevron = [&](const Rect& r, float dx) {
        c.strokeLine(r.midX() - dx, r.midY() + 3.5f, r.midX(), r.midY(), kTextMuted, 1.4f);
        c.strokeLine(r.midX(), r.midY(), r.midX() - dx, r.midY() - 3.5f, kTextMuted, 1.4f);
    };

    // --- Tuning -------------------------------------------------------------------------------
    const Rect tune{area.x, area.y, 430.0f, area.h};
    drawCard(c, tune, "TUNING");
    const std::string current = engine_ ? engine_->getTuning().name() : "12-TET";
    caption("SCALE (SCALA .SCL)", tune.x + 18.0f, tune.maxY() - 48.0f);
    tunePrev_ = {tune.x + 16.0f, tune.maxY() - 82.0f, 28.0f, 28.0f};
    tuneNext_ = {tune.maxX() - 44.0f, tunePrev_.y, 28.0f, 28.0f};
    tuneBox_ = {tunePrev_.maxX() + 4.0f, tunePrev_.y, tuneNext_.x - tunePrev_.maxX() - 8.0f, 28.0f};
    for (const Rect* r : {&tunePrev_, &tuneNext_, &tuneBox_}) {
        c.fillRoundRect(*r, 5.0f, kInsetBg);
        c.strokeRoundRect(*r, 5.0f, kPanelBorder, 1.0f);
    }
    chevron(tunePrev_, -3.0f);
    chevron(tuneNext_, 3.0f);
    const std::string shown = current == "12-TET" ? "12-TET (equal temperament)" : current;
    drawCentered(c, ellipsize(c, shown, tuneBox_.w - 16.0f, kStripOptionOn), tuneBox_, kStripOptionOn, kTextPrimary);

    tuneLoad_ = {tune.x + 16.0f, tune.maxY() - 120.0f, 128.0f, 28.0f};
    tuneFolder_ = {tuneLoad_.maxX() + 8.0f, tuneLoad_.y, 128.0f, 28.0f};
    modalButton(c, tuneLoad_, "Load .scl...", false, static_cast<bool>(host_.chooseTuningFile));
    modalButton(c, tuneFolder_, "Tunings Folder", false, static_cast<bool>(host_.openUrl));
    if (!tuningError_.empty()) {
        c.drawText(ellipsize(c, tuningError_, tune.w - 36.0f, kLinkSub), tune.x + 18.0f, tuneLoad_.y - 16.0f, kLinkSub, kAccent);
    }

    // Deviation of one octave (C4..B4) from equal temperament, in cents
    const Rect plot{tune.x + 16.0f, tune.y + 118.0f, tune.w - 32.0f, tuneLoad_.y - 30.0f - (tune.y + 118.0f)};
    c.fillRoundRect(plot, 6.0f, kInsetBg);
    c.strokeRoundRect(plot, 6.0f, kPanelBorder, 1.0f);
    const float mid = plot.midY(), span = plot.h * 0.5f - 18.0f;
    c.fillRect({plot.x + 1.0f, mid - 0.5f, plot.w - 2.0f, 1.0f}, kTextDim.withAlpha(0.7f));
    for (float ct : {-50.0f, 50.0f}) {
        const float yy = mid + span * ct / 50.0f;
        c.fillRect({plot.x + 1.0f, yy - 0.5f, plot.w - 2.0f, 1.0f}, kHairline.withAlpha(0.7f));
        c.drawText(ct > 0 ? "+50 c" : "-50 c", plot.x + 4.0f, ct > 0 ? yy - 12.0f : yy + 2.0f, kLinkSub, kTextDim);
    }
    static const char* noteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const float bw = (plot.w - 50.0f) / 12.0f;
    for (int k = 0; k < 12; ++k) {
        const float bx = plot.x + 44.0f + bw * k;
        float cents = 0.0f;
        const bool mapped = !engine_ || engine_->getTuning().centsFor(60 + k, cents);
        drawCentered(c, noteNames[k], {bx, plot.y + 4.0f, bw, 12.0f}, kLinkSub, kTextDim);
        if (!mapped) {
            drawCentered(c, "x", {bx, mid - 6.0f, bw, 12.0f}, kLinkSub, kTextDim);
            continue;
        }
        const float h = span * std::clamp(cents, -60.0f, 60.0f) / 50.0f;
        c.fillRoundRect({bx + bw * 0.22f, std::min(mid, mid + h), bw * 0.56f, std::max(1.5f, std::fabs(h))}, 2.0f, kAccent);
        char b[12];
        std::snprintf(b, sizeof(b), "%+.0f", cents);
        drawCentered(c, b, {bx, plot.maxY() - 16.0f, bw, 12.0f}, kLinkSub, kTextMuted);
    }
    caption("CENTS FROM EQUAL TEMPERAMENT, C4 - B4", plot.x + 4.0f, plot.maxY() + 6.0f);

    // Concert pitch and transposition
    modKnobs_.push_back({kParamMasterTune, {tune.x + 16.0f, tune.y + 12.0f, 120.0f, 89.0f}, 22.0f, false});
    caption("TRANSPOSE", tune.x + 176.0f, tune.y + 76.0f);
    addStepper(c, {tune.x + 176.0f, tune.y + 44.0f, 120.0f, 26.0f}, kParamTranspose);

    // --- Playing & realism ----------------------------------------------------------------------
    const float rx = area.x + 438.0f, rw = area.w - 438.0f;
    const Rect inst{rx, area.maxY() - 250.0f, rw, 250.0f};
    drawCard(c, inst, "PLAYING & REALISM");
    caption("POLYPHONY", inst.x + 18.0f, inst.maxY() - 48.0f);
    addStepper(c, {inst.x + 110.0f, inst.maxY() - 56.0f, 110.0f, 26.0f}, kParamPolyphony);
    const float kw = rw / 3.0f;
    const uint32_t ids[3] = {kParamCondition, kParamSympathetic, kParamStereoSpread};
    for (int i = 0; i < 3; ++i) modKnobs_.push_back({ids[i], {inst.x + kw * i + 8.0f, inst.y + 66.0f, kw - 16.0f, 89.0f}, 23.0f, false});
    float ty = inst.y + 46.0f;
    for (const auto& line : wrap(c, "Condition: per-key tuning, voicing and wear. Sympathetic: undamped tines ring along with the pedal down. "
                                    "Key Spread: low keys left, high keys right.", rw - 36.0f, kLinkSub)) {
        c.drawText(line, inst.x + 18.0f, ty, kLinkSub, kTextDim);
        ty -= c.lineHeight(kLinkSub) + 1.0f;
    }

    // --- Editor and performance ------------------------------------------------------------------
    const Rect ed{rx, area.y, rw, area.h - 250.0f - 8.0f};
    drawCard(c, ed, "EDITOR & PERFORMANCE");
    caption("ZOOM", ed.x + 18.0f, ed.maxY() - 48.0f);
    zoomPrev_ = {ed.x + 110.0f, ed.maxY() - 56.0f, 26.0f, 26.0f};
    zoomNext_ = {ed.x + 194.0f, zoomPrev_.y, 26.0f, 26.0f};
    for (const Rect* r : {&zoomPrev_, &zoomNext_}) {
        c.fillRoundRect(*r, 5.0f, kInsetBg);
        c.strokeRoundRect(*r, 5.0f, kPanelBorder, 1.0f);
    }
    chevron(zoomPrev_, -3.0f);
    chevron(zoomNext_, 3.0f);
    char zb[16];
    std::snprintf(zb, sizeof(zb), "%.0f%%", zoom_ * 100.0f);
    drawCentered(c, zb, {zoomPrev_.maxX(), zoomPrev_.y, zoomNext_.x - zoomPrev_.maxX(), 26.0f}, kStripOptionOn,
                 host_.requestZoom ? kTextPrimary : kTextDim);
    if (engine_) {
        const ScopeSnapshot& sc = engine_->getScopeSnapshot();
        char b[64];
        std::snprintf(b, sizeof(b), "CPU %.1f%%   Voices %d / %d", sc.cpuLoad * 100.0f, sc.activeVoices, sc.polyphony);
        c.drawText(b, ed.x + 240.0f, ed.maxY() - 48.0f, kKnobLabel, kTextMuted);
    }
    // A/B compare
    caption("COMPARE", ed.x + 18.0f, ed.maxY() - 84.0f);
    abRects_[0] = {ed.x + 110.0f, ed.maxY() - 92.0f, 40.0f, 26.0f};
    abRects_[1] = {ed.x + 154.0f, abRects_[0].y, 40.0f, 26.0f};
    abRects_[2] = {ed.x + 204.0f, abRects_[0].y, 120.0f, 26.0f};
    for (int i = 0; i < 2; ++i) {
        const bool on = abSlot_ == i;
        c.fillRoundRect(abRects_[i], 5.0f, on ? kAccent : kInsetBg);
        c.strokeRoundRect(abRects_[i], 5.0f, on ? kAccent : kPanelBorder, 1.0f);
        drawCentered(c, i == 0 ? "A" : "B", abRects_[i], kSegmentOn, on ? kHeaderBg : kTextMuted);
    }
    modalButton(c, abRects_[2], abSlot_ == 0 ? "Copy A to B" : "Copy B to A", false);
    ty = ed.maxY() - 120.0f;
    for (const auto& line : wrap(c, "Pedals: the sustain pedal is continuous (half-pedalling lets the dampers graze), CC 66 is sostenuto, "
                                    "CC 67 the soft pedal. Release velocity sets how fast the dampers land. Microtuning also follows "
                                    "MIDI Tuning Standard SysEx; the tuning is saved with the project.", rw - 36.0f, kAboutBody)) {
        c.drawText(line, ed.x + 18.0f, ty, kAboutBody, kTextMuted);
        ty -= c.lineHeight(kAboutBody) + 2.0f;
    }
}

// Keeps the live settings in the current slot and brings in the other one (a slot never used
// starts as a copy of the current sound)
void Editor::switchAb(int slot) {
    if (!engine_ || slot == abSlot_) return;
    for (uint32_t p = 0; p < kNumParams; ++p) abStore_[abSlot_][p] = engine_->getParamNormalized(p);
    abValid_[abSlot_] = true;
    if (abValid_[slot]) {
        engine_->pushUndoState(slot == 0 ? "Compare A" : "Compare B");
        for (uint32_t p = 0; p < kNumParams; ++p) setParam(p, abStore_[slot][p]);
    } else {
        abStore_[slot] = abStore_[abSlot_];
        abValid_[slot] = true;
    }
    abSlot_ = slot;
    forceRedraw_ = true;
}

bool Editor::mouseDownPlay(float x, float y) {
    if (!engine_) return false;
    auto currentIndex = [&]() {
        const std::string name = engine_->getTuning().name();
        for (size_t i = 0; i < tunings_.size(); ++i) {
            if (tunings_[i].name == name || (name == "12-TET" && tunings_[i].scl.empty())) return static_cast<int>(i);
        }
        return -1;
    };
    if (tunePrev_.contains(x, y) || tuneNext_.contains(x, y) || tuneBox_.contains(x, y)) {
        refreshTunings();
        const int cur = currentIndex();
        const int dir = tunePrev_.contains(x, y) ? -1 : 1;
        selectTuning(cur < 0 ? (dir > 0 ? 0 : static_cast<int>(tunings_.size()) - 1) : cur + dir);
        return true;
    }
    if (tuneLoad_.contains(x, y) && host_.chooseTuningFile) {
        const std::string path = host_.chooseTuningFile();
        if (path.empty()) return true;
        TuningEntry e;
        tuningError_.clear();
        if (!loadTuningFile(path, e)) { tuningError_ = "Could not read " + path; return true; }
        if (!applyTuning(engine_->getTuning(), e, &tuningError_)) { tuningError_ = e.name + ": " + tuningError_; return true; }
        // Keep a copy in the Tunings folder so it stays in the list
        try {
            namespace fs = std::filesystem;
            const fs::path dir = userTuningsDirectory();
            fs::create_directories(dir);
            const fs::path src(path);
            if (src.parent_path() != dir) {
                fs::copy_file(src, dir / src.filename(), fs::copy_options::overwrite_existing);
                fs::path kbm = src;
                kbm.replace_extension(".kbm");
                if (fs::exists(kbm)) fs::copy_file(kbm, dir / kbm.filename(), fs::copy_options::overwrite_existing);
            }
        } catch (...) {
        }
        refreshTunings();
        return true;
    }
    if (tuneFolder_.contains(x, y) && host_.openUrl) {
        const std::string dir = userTuningsDirectory();
        try { std::filesystem::create_directories(dir); } catch (...) {}
        std::string url = "file://";
        for (char ch : dir) {
            if (ch == ' ') url += "%20";
            else if (ch == '\\') url += '/';
            else url += ch;
        }
        host_.openUrl(url);
        return true;
    }
    for (int i = 0; i < 2; ++i) {
        if (abRects_[i].contains(x, y)) { switchAb(i); return true; }
    }
    if (abRects_[2].contains(x, y)) {
        const int other = 1 - abSlot_;
        for (uint32_t p = 0; p < kNumParams; ++p) abStore_[other][p] = engine_->getParamNormalized(p);
        abValid_[other] = true;
        return true;
    }
    if ((zoomPrev_.contains(x, y) || zoomNext_.contains(x, y)) && host_.requestZoom) {
        const int n = static_cast<int>(sizeof(kZoomSteps) / sizeof(kZoomSteps[0]));
        int cur = 0;
        for (int i = 0; i < n; ++i) if (std::fabs(kZoomSteps[i] - zoom_) < std::fabs(kZoomSteps[cur] - zoom_)) cur = i;
        const int next = std::clamp(cur + (zoomPrev_.contains(x, y) ? -1 : 1), 0, n - 1);
        saveEditorZoom(kZoomSteps[next]);
        host_.requestZoom(kZoomSteps[next]);
        return true;
    }
    return false;
}

} // namespace phs::gui

namespace phs {

namespace {
std::string editorSettingsPath() { return (std::filesystem::path(userDataDirectory()) / "Editor.cfg").string(); }
} // namespace

float loadEditorZoom() {
    std::ifstream in(editorSettingsPath());
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("Zoom=", 0) == 0) {
            const float z = std::strtof(line.c_str() + 5, nullptr);
            if (z >= 0.5f && z <= 3.0f) return z;
        }
    }
    return 1.0f;
}

void saveEditorZoom(float zoom) {
    try {
        std::filesystem::create_directories(userDataDirectory());
        std::ofstream out(editorSettingsPath());
        out << "Zoom=" << zoom << "\n";
    } catch (...) {
    }
}

} // namespace phs
