#include "PhsEditor.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

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
constexpr float kPi = 3.14159265358979f;

// Keyboard geometry (61 keys, C2..C7)
constexpr int kKbStartNote = 36;
constexpr int kKbNumKeys = 61;
constexpr int kKbNumNaturals = 36;
constexpr bool kIsBlackKey[12] = {false, true, false, true, false, false, true, false, true, false, true, false};

enum MenuAction { kMenuLearn, kMenuForget, kMenuReset };
enum Icon { kIconNone, kIconUndo, kIconRedo, kIconInfo };

} // namespace

Editor::Editor(PhsSynthEngine* engine, EditorHost host) : engine_(engine), host_(std::move(host)) { layout(); }

// =============================================================================
// LAYOUT
// Canvas 900 x 740, origin bottom-left. 16 px outer margin, 8 px panel gutters.
//   header 690..740 | signal chain 642..682 | resonator + pickup 404..634
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

const char* Editor::knobLabel(uint32_t id, const Stages& c) const {
    switch (id) {
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
        case kParamCabinet: return "Cabinet";
        case kParamWahAmount: return "Amount";
        case kParamWahSens: return "Sens";
        case kParamPhaserMix: case kParamChorusMix: case kParamDelayMix: case kParamReverbMix: return "Mix";
        case kParamPhaserRate: return "Rate";
        case kParamDelayTime: return "Time";
        case kParamDelayFeedback: return "Repeats";
        case kParamReverbDecay: return "Decay";
        default: return kParamMetadata[id].name;
    }
}

std::string Editor::valueText(uint32_t id, float norm, const Stages& c) const {
    char buf[64];
    if (knobInactive(id, c)) return "\xE2\x80\x94"; // em dash
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
    drawPanels(c);
    for (size_t i = 0; i < knobs_.size(); ++i) drawKnob(c, knobs_[i], i);
    drawPerformance(c);
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

void Editor::drawKnob(Canvas& c, const Knob& k, size_t index) {
    const float norm = engine_ ? engine_->getParamNormalized(k.paramId) : kParamMetadata[k.paramId].defaultValue;
    const int8_t midiCc = engine_ ? engine_->getParamCc(k.paramId) : -1;
    const bool armed = engine_ && engine_->getLearningParamId() == static_cast<int32_t>(k.paramId);
    const bool dragged = draggedKnob_ == static_cast<int>(index);
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
    const float aFrom = k.bipolar ? kPi * 0.5f : a0;
    if (std::fabs(aFrom - aCur) > 0.01f) c.strokeArc(cx, cy, arcR, aFrom, aCur, armed ? kLearn : kAccent, 3.0f);

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
    for (size_t i = 0; i < presets.size(); ++i) {
        if (category_ == PresetCategory::All || presets[i].category == category_) out.push_back(static_cast<int>(i));
    }
    return out;
}

Rect Editor::presetListRect() const { return {browserRect_.x + 20.0f, browserRect_.y + 56.0f, browserRect_.w - 40.0f, 290.0f}; }

void Editor::drawPresetBrowser(Canvas& c) {
    browserRect_ = modalFrame(c, 600.0f, 440.0f, "Presets", browserClose_);
    const Rect& m = browserRect_;

    const char* cats[6] = {"All", "Rhodes", "Wurlitzer", "Dyno/Bells", "Hybrid/Fx", "User"};
    const Rect tabs{m.x + 20.0f, m.maxY() - 86.0f, 560.0f, 28.0f};
    c.fillRoundRect(tabs, 6.0f, kInsetBg);
    const float tw = tabs.w / 6.0f;
    for (int i = 0; i < 6; ++i) {
        tabRects_[i] = {tabs.x + i * tw, tabs.y, tw, tabs.h};
        const bool on = static_cast<int>(category_) == i;
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
            c.drawText(presets[p].name, list.x + 14.0f, rowY + 2.0f, current ? kListItemOn : kListItem, current ? kAccent : kTextPrimary);
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

    browserSave_ = {m.x + 20.0f, m.y + 16.0f, 120.0f, 28.0f};
    browserDelete_ = {m.x + 148.0f, m.y + 16.0f, 120.0f, 28.0f};
    const bool canDelete = engine_ && selectedPreset_ >= 0 && engine_->getPresetManager().getPreset(selectedPreset_) &&
                           engine_->getPresetManager().getPreset(selectedPreset_)->isUserPreset;
    modalButton(c, browserSave_, "Save Preset\xE2\x80\xA6", true);
    modalButton(c, browserDelete_, "Delete", false, canDelete);
    c.drawText("Scroll for more \xC2\xB7 click to load", m.x + 290.0f, m.y + 24.0f, kLinkSub, kTextDim);
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
    c.drawText("by mtyas  \xC2\xB7  v0.77  \xC2\xB7  GPL-3.0", m.x + 28.0f + c.textWidth("electropiano", kModalTitle), m.maxY() - 31.0f, kAboutBy, kAccent);

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
        for (int i = 0; i < 6; ++i) {
            if (tabRects_[i].contains(x, y)) { category_ = static_cast<PresetCategory>(i); listScroll_ = 0; return; }
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

    if (pedal_.contains(x, y)) {
        sendMidi(0xB0, 64, (engine_ && engine_->isSustainActive()) ? 0 : 127);
        return;
    }

    for (size_t i = 0; i < knobs_.size(); ++i) {
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
        dragStartY_ = y;
        dragStartVal_ = engine_ ? engine_->getParamNormalized(pid) : 0.5f;
        fineDrag_ = fine;
        if (engine_) engine_->pushUndoState(kParamMetadata[pid].name);
        return;
    }

    const int note = noteAtPoint(x, y);
    if (note >= 0) {
        heldNote_ = note;
        sendMidi(0x90, static_cast<uint8_t>(note), 108);
    }
}

void Editor::mouseDrag(float /*x*/, float y) {
    if (draggedKnob_ < 0 || draggedKnob_ >= static_cast<int>(knobs_.size())) return;
    const float sens = fineDrag_ ? 0.0015f : 0.0065f;
    setParam(knobs_[draggedKnob_].paramId, std::clamp(dragStartVal_ + (y - dragStartY_) * sens, 0.0f, 1.0f));
}

void Editor::mouseUp(float, float) {
    draggedKnob_ = -1;
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
    for (const auto& k : knobs_) {
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
    if (!saveOpen_ || cp < 32 || cp == 127) return false;
    if (saveName_.size() >= 48) return true;
    if (cp < 0x80) {
        saveName_ += static_cast<char>(cp);
    } else if (cp < 0x800) {
        saveName_ += static_cast<char>(0xC0 | (cp >> 6));
        saveName_ += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        saveName_ += static_cast<char>(0xE0 | (cp >> 12));
        saveName_ += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        saveName_ += static_cast<char>(0x80 | (cp & 0x3F));
    }
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
    if (k == Key::Escape) {
        const bool any = menuOpen_ || aboutOpen_ || browserOpen_;
        menuOpen_ = aboutOpen_ = browserOpen_ = false;
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
    }
    return h;
}

bool Editor::wantsRedraw() { return forceRedraw_ || stateHash() != lastHash_; }

} // namespace phs::gui
