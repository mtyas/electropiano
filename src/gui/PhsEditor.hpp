#pragma once

// The electropiano editor: layout, drawing and interaction, shared by every platform.
// It draws through phs::gui::Canvas and receives input in logical coordinates with the
// origin at the bottom-left (y up). Platform hosts (Cocoa, Win32, X11) only forward
// events, provide a Canvas and repaint when wantsRedraw() says so.

#include "../dsp/PhsSynthEngine.hpp"
#include "../dsp/PhsTuningLibrary.hpp"
#include "PhsCanvas.hpp"
#include <functional>
#include <string>
#include <vector>

namespace phs {

// Callback invoked when the user changes a parameter in the editor, so the plugin wrapper
// (VST3 / CLAP / AU) can notify the host for automation.
using ParamChangeCallback = std::function<void(uint32_t paramId, float normalizedValue)>;

static constexpr int kEditorWidth = 900;
static constexpr int kEditorHeight = 776;

// Editor zoom (1 = 900 x 776), remembered between sessions in the user data folder
float loadEditorZoom();
void saveEditorZoom(float zoom);
inline int scaledEditorWidth(float zoom) { return static_cast<int>(kEditorWidth * zoom + 0.5f); }
inline int scaledEditorHeight(float zoom) { return static_cast<int>(kEditorHeight * zoom + 0.5f); }

namespace gui {

struct EditorHost {
    ParamChangeCallback paramChanged;
    std::function<void(const std::string& url)> openUrl;
    // Asks the platform view to change the editor's zoom (it resizes itself and tells the host)
    std::function<void(float zoom)> requestZoom;
    // Shows a file chooser for a Scala file; returns the path, or "" (absent where unsupported)
    std::function<std::string()> chooseTuningFile;
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
    // The zoom the platform view is drawing at (shown on the Play page)
    void setZoom(float zoom) { zoom_ = zoom; forceRedraw_ = true; }
    float zoom() const { return zoom_; }

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
    bool knobInactive(uint32_t id, const Stages& s) const;

    void drawHeader(Canvas& c);
    void drawStageStrip(Canvas& c);
    void drawPanels(Canvas& c);
    void drawKnob(Canvas& c, const Knob& k, bool dragged);
    void drawModPanel(Canvas& c);
    void drawPageBar(Canvas& c);
    void drawPage(Canvas& c);
    void drawLabMod(Canvas& c, const Rect& area);
    void drawLabMacros(Canvas& c, const Rect& area);
    void drawLabSpace(Canvas& c, const Rect& area);
    void drawLabEq(Canvas& c, const Rect& area);
    void drawLabDynamics(Canvas& c, const Rect& area);
    void drawPartialMap(Canvas& c, const Rect& card);
    void drawCurveGrid(Canvas& c, const Rect& plot, float dbLo, float dbHi, float dbStep);
    void openList(uint32_t id, const Rect& anchor);
    void drawList(Canvas& c);
    bool mouseDownList(float x, float y);
    void setEqNode(int band, float x, float y);
    void drawLabScope(Canvas& c, const Rect& area);
    void drawLabPlay(Canvas& c, const Rect& area);
    bool mouseDownPlay(float x, float y);
    void refreshTunings();
    void selectTuning(int index);
    void drawTineView(Canvas& c, const Rect& card);
    void drawPickupView(Canvas& c, const Rect& card);
    void drawAmpView(Canvas& c, const Rect& card);
    void drawMeter(Canvas& c, const Rect& r, float level01, bool reverse = false);
    void drawCard(Canvas& c, const Rect& r, const char* title);
    void drawStepper(Canvas& c, const Rect& r, uint32_t id, bool enabled = true);
    void drawSlider(Canvas& c, const Rect& r, uint32_t id, bool enabled = true);
    void drawScope(Canvas& c, const Rect& r);
    void addStepper(Canvas& c, const Rect& r, uint32_t id, bool enabled = true);
    void addSlider(Canvas& c, const Rect& r, uint32_t id, bool enabled = true);
    void stepParam(uint32_t id, int dir);
    bool mouseDownModPanel(float x, float y, bool fine, double timeSec);
    void beginKnobDrag(uint32_t id, float y, bool fine);
    void setSliderFromX(int index, float x);
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

    // Preset browser (tab 6 = favourites; typing filters the list)
    bool browserOpen_ = false;
    Rect browserRect_{}, browserClose_{}, browserSave_{}, browserDelete_{}, tabRects_[7]{}, searchRect_{};
    bool favoritesOnly_ = false;
    std::string search_;
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

    // Modulation & expression panel
    struct Stepper { uint32_t paramId; Rect rect; };
    struct Slider { uint32_t paramId; Rect rect; };
    static constexpr int kNumPages = 8;
    int page_ = 0;                     // 0 Sound, 1 Modulation, 2 Macros, 3 Space, 4 EQ, 5 Dynamics, 6 Scope, 7 Play
    Rect pageTabs_[kNumPages]{};
    float pageBarY_ = 640.0f;
    std::vector<Knob> modKnobs_;
    std::vector<Stepper> modSteppers_;
    std::vector<Slider> modSliders_;
    float tipMax_ = 0.0f, holdMax_ = 0.0f;

    // Dropdown list for stepped parameters (modulation sources and targets, cabinets, sync divisions...)
    bool listOpen_ = false;
    uint32_t listParam_ = 0;
    Rect listRect_{}, listAnchor_{};
    int listCols_ = 1, listRows_ = 1;
    float listColW_ = 150.0f;

    // Mastering EQ: selected band, the node being dragged, the plot area and the analyser's smoothed spectrum
    int eqSel_ = 0, eqDragBand_ = -1;
    Rect eqPlot_{};
    Rect eqNodes_[4]{};
    static constexpr int kSpecPts = 220;
    float eqSpec_[kSpecPts]{};
    int draggedModKnob_ = -1;
    int draggedSlider_ = -1;

    // Knob context menu
    bool menuOpen_ = false;
    Rect menuRect_{};
    uint32_t menuParam_ = 0;
    std::vector<MenuItem> menuItems_;

    // Play page: tuning list and zoom
    std::vector<TuningEntry> tunings_;
    std::string tuningError_;
    Rect tunePrev_{}, tuneNext_{}, tuneBox_{}, tuneLoad_{}, tuneFolder_{}, zoomPrev_{}, zoomNext_{};
    float zoom_ = 1.0f;

    // A/B compare: two snapshots of every parameter, one of them live
    Rect abRects_[3]{};                 // A, B, copy to the other
    std::array<std::array<float, kNumParams>, 2> abStore_{};
    bool abValid_[2] = {false, false};
    int abSlot_ = 0;
    void switchAb(int slot);

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
