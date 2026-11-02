#pragma once

// Drawing interface the shared editor (PhsEditor) renders through. Coordinates are
// logical pixels with the origin at the BOTTOM-left and y pointing up (AppKit
// convention); backends convert as needed.
//
// Backends: PhsCocoaView.mm (CoreGraphics + AppKit text, macOS) and
// PhsSoftwareCanvas.hpp (portable anti-aliased rasteriser, Windows and Linux).

#include <string>

namespace phs::gui {

struct Color {
    float r, g, b, a;
    constexpr Color withAlpha(float m) const { return {r, g, b, a * m}; }
};

struct Rect {
    float x, y, w, h;
    float maxX() const { return x + w; }
    float maxY() const { return y + h; }
    float midX() const { return x + w * 0.5f; }
    float midY() const { return y + h * 0.5f; }
    bool contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
    Rect inset(float dx, float dy) const { return {x + dx, y + dy, w - 2 * dx, h - 2 * dy}; }
    Rect offset(float dx, float dy) const { return {x + dx, y + dy, w, h}; }
};

enum class Weight { Regular = 0, Medium = 1, Semibold = 2 };

struct TextStyle {
    float size;
    Weight weight;
    float kern = 0.0f;     // extra spacing between characters
    bool monoDigits = false;
};

class Canvas {
public:
    virtual ~Canvas() = default;

    virtual void save() = 0;
    virtual void restore() = 0;
    virtual void clip(const Rect& r) = 0;
    virtual void setAlpha(float a) = 0; // global alpha for subsequent drawing (reset by restore)

    virtual void fillRect(const Rect& r, Color c) = 0;
    virtual void fillRoundRect(const Rect& r, float radius, Color c) = 0;
    virtual void fillRoundRectGradient(const Rect& r, float radius, Color top, Color bottom) = 0;
    // Stroke inside the rect edge (the stroke is inset by width / 2)
    virtual void strokeRoundRect(const Rect& r, float radius, Color c, float width) = 0;
    virtual void fillCircle(float cx, float cy, float radius, Color c) = 0;
    virtual void fillCircleGradient(float cx, float cy, float radius, Color top, Color bottom) = 0;
    virtual void strokeCircle(float cx, float cy, float radius, Color c, float width) = 0;
    // Arc from angle a0 to a1 (radians, counter-clockwise positive); sweeps clockwise when a1 < a0
    virtual void strokeArc(float cx, float cy, float radius, float a0, float a1, Color c, float width) = 0;
    virtual void strokeLine(float x0, float y0, float x1, float y1, Color c, float width) = 0; // round caps
    // Soft drop shadow of a rounded rect / circle (blur = shadow radius, dy = offset)
    virtual void shadowRoundRect(const Rect& r, float radius, float blur, float dy, Color c) = 0;
    virtual void shadowCircle(float cx, float cy, float radius, float blur, float dy, Color c) = 0;
    virtual void glowCircle(float cx, float cy, float radius, float blur, Color c) = 0;

    virtual float textWidth(const std::string& utf8, const TextStyle& s) = 0;
    virtual float lineHeight(const TextStyle& s) = 0;
    // (x, y) is the bottom-left of the text's line box
    virtual void drawText(const std::string& utf8, float x, float y, const TextStyle& s, Color c) = 0;
};

// Draws the editor's logical coordinates at a zoom factor onto another canvas (used by the
// software renderer; CoreGraphics scales its context instead)
class ScaledCanvas final : public Canvas {
public:
    ScaledCanvas(Canvas& inner, float scale) : in_(inner), s_(scale) {}
    void save() override { in_.save(); }
    void restore() override { in_.restore(); }
    void clip(const Rect& r) override { in_.clip(S(r)); }
    void setAlpha(float a) override { in_.setAlpha(a); }
    void fillRect(const Rect& r, Color c) override { in_.fillRect(S(r), c); }
    void fillRoundRect(const Rect& r, float radius, Color c) override { in_.fillRoundRect(S(r), radius * s_, c); }
    void fillRoundRectGradient(const Rect& r, float radius, Color top, Color bottom) override {
        in_.fillRoundRectGradient(S(r), radius * s_, top, bottom);
    }
    void strokeRoundRect(const Rect& r, float radius, Color c, float width) override {
        in_.strokeRoundRect(S(r), radius * s_, c, width * s_);
    }
    void fillCircle(float cx, float cy, float radius, Color c) override { in_.fillCircle(cx * s_, cy * s_, radius * s_, c); }
    void fillCircleGradient(float cx, float cy, float radius, Color top, Color bottom) override {
        in_.fillCircleGradient(cx * s_, cy * s_, radius * s_, top, bottom);
    }
    void strokeCircle(float cx, float cy, float radius, Color c, float width) override {
        in_.strokeCircle(cx * s_, cy * s_, radius * s_, c, width * s_);
    }
    void strokeArc(float cx, float cy, float radius, float a0, float a1, Color c, float width) override {
        in_.strokeArc(cx * s_, cy * s_, radius * s_, a0, a1, c, width * s_);
    }
    void strokeLine(float x0, float y0, float x1, float y1, Color c, float width) override {
        in_.strokeLine(x0 * s_, y0 * s_, x1 * s_, y1 * s_, c, width * s_);
    }
    void shadowRoundRect(const Rect& r, float radius, float blur, float dy, Color c) override {
        in_.shadowRoundRect(S(r), radius * s_, blur * s_, dy * s_, c);
    }
    void shadowCircle(float cx, float cy, float radius, float blur, float dy, Color c) override {
        in_.shadowCircle(cx * s_, cy * s_, radius * s_, blur * s_, dy * s_, c);
    }
    void glowCircle(float cx, float cy, float radius, float blur, Color c) override {
        in_.glowCircle(cx * s_, cy * s_, radius * s_, blur * s_, c);
    }
    float textWidth(const std::string& t, const TextStyle& st) override { return in_.textWidth(t, T(st)) / s_; }
    float lineHeight(const TextStyle& st) override { return in_.lineHeight(T(st)) / s_; }
    void drawText(const std::string& t, float x, float y, const TextStyle& st, Color c) override {
        in_.drawText(t, x * s_, y * s_, T(st), c);
    }

private:
    Rect S(const Rect& r) const { return {r.x * s_, r.y * s_, r.w * s_, r.h * s_}; }
    TextStyle T(const TextStyle& st) const { TextStyle o = st; o.size *= s_; o.kern *= s_; return o; }
    Canvas& in_;
    float s_;
};

// Editor zoom steps offered in the editor (the host can also resize to any size in between)
static constexpr float kZoomSteps[] = {0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f};

} // namespace phs::gui
