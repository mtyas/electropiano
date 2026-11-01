#pragma once

// The electropiano editor: layout, drawing and interaction, shared by every platform.
// It draws through phs::gui::Canvas and receives input in logical coordinates with the
// origin at the bottom-left (y up). Platform hosts (Cocoa, Win32, X11) only forward
// events, provide a Canvas and repaint when wantsRedraw() says so.

#include "../dsp/PhsSynthEngine.hpp"
#include "PhsCanvas.hpp"
#include <functional>
#include <string>
#include <vector>

namespace phs {

// Callback invoked when the user changes a parameter in the editor, so the plugin wrapper
// (VST3 / CLAP / AU) can notify the host for automation.
using ParamChangeCallback = std::function<void(uint32_t paramId, float normalizedValue)>;

static constexpr int kEditorWidth = 900;
static constexpr int kEditorHeight = 740;

namespace gui {

struct EditorHost {
    ParamChangeCallback paramChanged;
    std::function<void(const std::string& url)> openUrl;
};

enum class Key { Escape, Enter, Backspace };

class Editor {
public:
    Editor(PhsSynthEngine* engine, EditorHost host);

    void draw(Canvas& c);

    // Input in logical coordinates (origin bottom-left, y up). timeSec is any monotonic clock.
    void mouseDown(float x, float y, bool fine, double timeSec);
    void mouseDrag(float x, float y);
    void mouseUp(float x, float y);
    void rightMouseDown(float x, float y);
    void scroll(float x, float y, float deltaLines);
    bool keyChar(uint32_t codepoint);
    bool keyAction(Key k);

    // True when anything visible has changed since the last draw()
    bool wantsRedraw();
    void detachEngine() { engine_ = nullptr; }

private:
    enum class PanelKind { Resonator, Pickup, Amp, EnvFilter, Phaser, Chorus, Echo, Reverb, Output };
    struct Knob { uint32_t paramId; Rect rect; float radius; bool bipolar; };
    struct Panel { Rect rect; PanelKind kind; };
    struct Stages { bool reed = false, electrostatic = false, amp200A = false; };
    struct MenuItem { std::string label; int action; };

    void layout();
    Stages stages() const;
    void setParam(uint32_t id, float v);
    std::string valueText(uint32_t id, float norm, const Stages& s) const;
    const char* knobLabel(uint32_t id, const Stages& s) const;
    const char* panelTitle(PanelKind k, const Stages& s) const;
    static bool knobInactive(uint32_t id, const Stages& s) { return s.reed && id == kParamStrikePosition; }

    void drawHeader(Canvas& c);
    void drawStageStrip(Canvas& c);
    void drawPanels(Canvas& c);
    void drawKnob(Canvas& c, const Knob& k, size_t index);
    void drawPerformance(Canvas& c);
    void drawPresetBrowser(Canvas& c);
    void drawSaveDialog(Canvas& c);
    void drawAbout(Canvas& c);
    void drawMenu(Canvas& c);
    Rect modalFrame(Canvas& c, float w, float h, const char* title, Rect& closeRect);
    void modalButton(Canvas& c, const Rect& r, const char* label, bool primary, bool enabled = true);
    void headerButton(Canvas& c, const Rect& r, int icon, const char* label, bool enabled, bool active);
    void drawCentered(Canvas& c, const std::string& s, const Rect& r, const TextStyle& st, Color col);
    std::vector<std::string> wrap(Canvas& c, const std::string& text, float width, const TextStyle& st);
    std::string ellipsize(Canvas& c, const std::string& s, float width, const TextStyle& st);

    bool keyRect(int note, Rect& out) const;
    int noteAtPoint(float x, float y) const;
    void sendMidi(uint8_t status, uint8_t d1, uint8_t d2);
    std::vector<int> visiblePresets() const;
    Rect presetListRect() const;
    void runMenuAction(int action);
    uint64_t stateHash() const;

    PhsSynthEngine* engine_;
    EditorHost host_;

    std::vector<Knob> knobs_;
    std::vector<Panel> panels_;
    Rect modelButtons_[4]{}, modelSelector_{};
    Rect presetPrev_{}, presetBox_{}, presetNext_{}, undoBtn_{}, redoBtn_{}, midiBtn_{}, aboutBtn_{};
    Rect strip_{};
    Rect stageRects_[3][2]{};
    float stageLabelX_[3]{};
    Rect performance_{}, pedal_{}, pitchWheel_{}, modWheel_{}, keyboard_{};

    // Preset browser
    bool browserOpen_ = false;
    Rect browserRect_{}, browserClose_{}, browserSave_{}, browserDelete_{}, tabRects_[6]{};
    PresetCategory category_ = PresetCategory::All;
    int listScroll_ = 0;
    int selectedPreset_ = -1;

    // Save-name dialog
    bool saveOpen_ = false;
    std::string saveName_;
    Rect saveRect_{}, saveOk_{}, saveCancel_{}, saveClose_{};

    // About
    bool aboutOpen_ = false;
    Rect aboutRect_{}, aboutClose_{}, aboutLinks_[4]{};

    // Knob context menu
    bool menuOpen_ = false;
    Rect menuRect_{};
    uint32_t menuParam_ = 0;
    std::vector<MenuItem> menuItems_;

    // Interaction
    int draggedKnob_ = -1;
    float dragStartY_ = 0.0f, dragStartVal_ = 0.0f;
    bool fineDrag_ = false;
    int heldNote_ = -1;
    int lastClickedKnob_ = -1;
    double lastClickTime_ = -1.0;
    uint64_t lastHash_ = 0;
    bool forceRedraw_ = true;
};

} // namespace gui
} // namespace phs
