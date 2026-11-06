#pragma once

// Portable software implementation of phs::gui::Canvas, used by the Windows and Linux
// editors. Shapes are rasterised analytically: each pixel's coverage comes from the
// signed distance to the shape (1 px anti-aliasing), so everything stays crisp without a
// graphics library. Text comes from the pre-rendered Inter glyph atlas (PhsFontAtlas.hpp).
//
// Pixels are 0x00RRGGBB, rows top-to-bottom: what Win32 DIBs and X11 TrueColor
// ZPixmaps expect.

#include "PhsCanvas.hpp"
#include "PhsFontAtlas.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace phs::gui {

class SoftwareCanvas final : public Canvas {
public:
    void resize(int w, int h) {
        w_ = w;
        h_ = h;
        pixels_.assign(static_cast<size_t>(w) * h, 0);
        stack_.clear();
        state_ = State{{0, 0, w, h}, 1.0f};
    }
    const uint32_t* pixels() const { return pixels_.data(); }
    uint32_t* pixels() { return pixels_.data(); }
    int width() const { return w_; }
    int height() const { return h_; }

    void save() override { stack_.push_back(state_); }
    void restore() override {
        if (!stack_.empty()) { state_ = stack_.back(); stack_.pop_back(); }
    }
    void clip(const Rect& r) override {
        const int x0 = std::max(state_.clip.x0, static_cast<int>(std::floor(r.x)));
        const int x1 = std::min(state_.clip.x1, static_cast<int>(std::ceil(r.maxX())));
        const int y0 = std::max(state_.clip.y0, static_cast<int>(std::floor(h_ - r.maxY())));
        const int y1 = std::min(state_.clip.y1, static_cast<int>(std::ceil(h_ - r.y)));
        state_.clip = {x0, y0, x1, y1};
    }
    void setAlpha(float a) override { state_.alpha = std::clamp(a, 0.0f, 1.0f); }

    void fillRect(const Rect& r, Color c) override { fillRoundRect(r, 0.0f, c); }

    void fillRoundRect(const Rect& r, float radius, Color c) override {
        convex(r.x - 1, r.y - 1, r.maxX() + 1, r.maxY() + 1, [&](float) { return c; },
               [&](float px, float py) { return cov(-sdRoundRect(px, py, r, radius)); });
    }
    void fillRoundRectGradient(const Rect& r, float radius, Color top, Color bottom) override {
        convex(r.x - 1, r.y - 1, r.maxX() + 1, r.maxY() + 1,
               [&](float py) { return mix(bottom, top, std::clamp((py - r.y) / std::max(1.0f, r.h), 0.0f, 1.0f)); },
               [&](float px, float py) { return cov(-sdRoundRect(px, py, r, radius)); });
    }
    void strokeRoundRect(const Rect& r, float radius, Color c, float width) override {
        const Rect mid = r.inset(width * 0.5f, width * 0.5f);
        const float rr = std::max(0.0f, radius - width * 0.5f);
        // Rows clear of the corners only touch the stroke near the left and right edges
        const float band = std::min(radius, std::min(r.w, r.h) * 0.5f) + width + 1.5f;
        shapeRows(r.x - 1, r.y - 1, r.maxX() + 1, r.maxY() + 1, [&](float py, float& a0, float& a1, float& b0, float& b1) {
            if (py > r.y + band && py < r.maxY() - band) {
                a0 = r.x - 1; a1 = r.x + width + 1; b0 = r.maxX() - width - 1; b1 = r.maxX() + 1;
            } else {
                a0 = r.x - 1; a1 = r.maxX() + 1; b0 = b1 = 0.0f;
            }
        }, [&](float px, float py, Color& col) {
            col = c;
            return cov(width * 0.5f - std::fabs(sdRoundRect(px, py, mid, rr)));
        });
    }
    void fillCircle(float cx, float cy, float radius, Color c) override {
        convex(cx - radius - 1, cy - radius - 1, cx + radius + 1, cy + radius + 1, [&](float) { return c; },
               [&](float px, float py) { return cov(radius - std::hypot(px - cx, py - cy)); });
    }
    void fillCircleGradient(float cx, float cy, float radius, Color top, Color bottom) override {
        convex(cx - radius - 1, cy - radius - 1, cx + radius + 1, cy + radius + 1,
               [&](float py) { return mix(bottom, top, std::clamp((py - (cy - radius)) / (2.0f * radius), 0.0f, 1.0f)); },
               [&](float px, float py) { return cov(radius - std::hypot(px - cx, py - cy)); });
    }
    void strokeCircle(float cx, float cy, float radius, Color c, float width) override {
        const float o = radius + width;
        ring(cx, cy, radius, width * 0.5f, [&](float px, float py, Color& col) {
            col = c;
            return cov(width * 0.5f - std::fabs(std::hypot(px - cx, py - cy) - radius));
        });
        (void)o;
    }
    void strokeArc(float cx, float cy, float radius, float a0, float a1, Color c, float width) override {
        const float lo = std::min(a0, a1), hi = std::max(a0, a1);
        const float hw = width * 0.5f;
        const float ex0 = cx + radius * std::cos(lo), ey0 = cy + radius * std::sin(lo);
        const float ex1 = cx + radius * std::cos(hi), ey1 = cy + radius * std::sin(hi);
        ring(cx, cy, radius, hw, [&](float px, float py, Color& col) {
            col = c;
            float a = std::atan2(py - cy, px - cx);
            while (a < lo) a += 6.28318531f;
            float d;
            if (a <= hi) {
                d = std::fabs(std::hypot(px - cx, py - cy) - radius) - hw;
            } else {
                d = std::min(std::hypot(px - ex0, py - ey0), std::hypot(px - ex1, py - ey1)) - hw; // round caps
            }
            return cov(-d);
        });
    }
    void strokeLine(float x0, float y0, float x1, float y1, Color c, float width) override {
        const float hw = width * 0.5f;
        shape(std::min(x0, x1) - hw - 1, std::min(y0, y1) - hw - 1, std::max(x0, x1) + hw + 1, std::max(y0, y1) + hw + 1,
              [&](float px, float py, Color& col) {
                  col = c;
                  const float dx = x1 - x0, dy = y1 - y0;
                  const float len2 = dx * dx + dy * dy;
                  const float t = len2 > 0.0f ? std::clamp(((px - x0) * dx + (py - y0) * dy) / len2, 0.0f, 1.0f) : 0.0f;
                  return cov(hw - std::hypot(px - (x0 + t * dx), py - (y0 + t * dy)));
              });
    }
    void shadowRoundRect(const Rect& r, float radius, float blur, float dy, Color c) override {
        const Rect s = r.offset(0, dy);
        convex(s.x - blur - 1, s.y - blur - 1, s.maxX() + blur + 1, s.maxY() + blur + 1, [&](float) { return c; },
               [&](float px, float py) { return soft(sdRoundRect(px, py, s, radius), blur); });
    }
    void shadowCircle(float cx, float cy, float radius, float blur, float dy, Color c) override {
        cy += dy;
        const float o = radius + blur;
        convex(cx - o - 1, cy - o - 1, cx + o + 1, cy + o + 1, [&](float) { return c; },
               [&](float px, float py) { return soft(std::hypot(px - cx, py - cy) - radius, blur); });
    }
    void glowCircle(float cx, float cy, float radius, float blur, Color c) override { shadowCircle(cx, cy, radius, blur, 0.0f, c); }

    // Sizes the atlas does not hold (a scaled editor) are drawn from the nearest face of the same
    // weight, resampled
    float textWidth(const std::string& s, const TextStyle& st) override {
        float ratio;
        const auto* face = faceFor(st, ratio);
        float w = 0.0f;
        int n = 0;
        for (size_t i = 0; i < s.size();) {
            const uint32_t cp = nextCodepoint(s, i);
            if (const auto* g = glyphFor(face, cp)) w += g->advance * ratio;
            ++n;
        }
        return w + st.kern * std::max(0, n - 1);
    }
    float lineHeight(const TextStyle& st) override {
        float ratio;
        const auto* f = faceFor(st, ratio);
        return std::ceil((f->ascent + f->descent) * ratio);
    }
    void drawText(const std::string& s, float x, float y, const TextStyle& st, Color c) override {
        float ratio;
        const auto* face = faceFor(st, ratio);
        const float baseline = static_cast<float>(h_) - (y + face->descent * ratio); // device y of the baseline
        float pen = x;
        for (size_t i = 0; i < s.size();) {
            const uint32_t cp = nextCodepoint(s, i);
            const auto* g = glyphFor(face, cp);
            if (!g) continue;
            if (g->w > 0) {
                if (ratio == 1.0f) {
                    blitGlyph(face, *g, static_cast<int>(std::lround(pen)) + g->left,
                              static_cast<int>(std::lround(baseline)) - g->top, c);
                } else {
                    blitGlyphScaled(face, *g, pen + g->left * ratio, baseline - g->top * ratio, ratio, c);
                }
            }
            pen += g->advance * ratio + st.kern;
        }
    }

private:
    struct Clip { int x0, y0, x1, y1; };
    struct State { Clip clip; float alpha; };

    static float cov(float d) { return std::clamp(d + 0.5f, 0.0f, 1.0f); }
    static float soft(float d, float blur) {
        const float t = std::clamp(0.5f - d / std::max(0.5f, 2.0f * blur), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }
    static Color mix(Color a, Color b, float t) {
        return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
    }
    static float sdRoundRect(float px, float py, const Rect& r, float radius) {
        radius = std::min(radius, std::min(r.w, r.h) * 0.5f);
        const float qx = std::fabs(px - r.midX()) - (r.w * 0.5f - radius);
        const float qy = std::fabs(py - r.midY()) - (r.h * 0.5f - radius);
        return std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - radius;
    }

    // Evaluates f at every pixel centre of the logical bounds (y-up) and blends the result
    template <class F>
    void shape(float lx0, float ly0, float lx1, float ly1, F&& f) {
        shapeRows(lx0, ly0, lx1, ly1, [&](float, float& a0, float& a1, float& b0, float& b1) {
            a0 = lx0; a1 = lx1; b0 = b1 = 0.0f;
        }, f);
    }

    // As shape(), but each row only visits up to two spans [a0, a1) and [b0, b1) (logical x)
    // that spans(py, ...) picks: the rest of the row is known to be untouched
    template <class S, class F>
    void shapeRows(float lx0, float ly0, float lx1, float ly1, S&& spans, F&& f) {
        const int x0 = std::max(state_.clip.x0, static_cast<int>(std::floor(lx0)));
        const int x1 = std::min(state_.clip.x1, static_cast<int>(std::ceil(lx1)));
        const int y0 = std::max(state_.clip.y0, static_cast<int>(std::floor(h_ - ly1)));
        const int y1 = std::min(state_.clip.y1, static_cast<int>(std::ceil(h_ - ly0)));
        if (x0 >= x1) return;
        Color col{};
        for (int dy = y0; dy < y1; ++dy) {
            const float py = static_cast<float>(h_) - (dy + 0.5f);
            uint32_t* row = pixels_.data() + static_cast<size_t>(dy) * w_;
            float s[4];
            spans(py, s[0], s[1], s[2], s[3]);
            int prevEnd = x0;
            for (int k = 0; k < 2; ++k) {
                if (!(s[2 * k + 1] > s[2 * k])) continue;
                const int sx0 = std::max(prevEnd, std::max(x0, static_cast<int>(std::floor(s[2 * k]))));
                const int sx1 = std::min(x1, static_cast<int>(std::ceil(s[2 * k + 1])));
                for (int dx = sx0; dx < sx1; ++dx) {
                    const float a = f(dx + 0.5f, py, col);
                    if (a > 0.0f) blend(row[dx], col, a * col.a * state_.alpha);
                }
                prevEnd = std::max(prevEnd, sx1);
            }
        }
    }

    // A convex shape: coverage(px, py) rises from the edges to 1 inside, so each row is
    // anti-aliased from both ends inward and the run between is a plain fill. rowColor(py)
    // gives the colour of a row (vertical gradients are constant along it).
    template <class RC, class CV>
    void convex(float lx0, float ly0, float lx1, float ly1, RC&& rowColor, CV&& coverage) {
        const int x0 = std::max(state_.clip.x0, static_cast<int>(std::floor(lx0)));
        const int x1 = std::min(state_.clip.x1, static_cast<int>(std::ceil(lx1)));
        const int y0 = std::max(state_.clip.y0, static_cast<int>(std::floor(h_ - ly1)));
        const int y1 = std::min(state_.clip.y1, static_cast<int>(std::ceil(h_ - ly0)));
        if (x0 >= x1) return;
        for (int dy = y0; dy < y1; ++dy) {
            const float py = static_cast<float>(h_) - (dy + 0.5f);
            uint32_t* row = pixels_.data() + static_cast<size_t>(dy) * w_;
            const Color col = rowColor(py);
            const float alpha = col.a * state_.alpha;
            if (alpha <= 0.0f) continue;
            int xl = x0;
            for (; xl < x1; ++xl) {
                const float a = coverage(xl + 0.5f, py);
                if (a >= 1.0f) break;
                if (a > 0.0f) blend(row[xl], col, a * alpha);
            }
            int xr = x1 - 1;
            for (; xr > xl; --xr) {
                const float a = coverage(xr + 0.5f, py);
                if (a >= 1.0f) break;
                if (a > 0.0f) blend(row[xr], col, a * alpha);
            }
            if (xl <= xr && xl < x1) fillSpan(row + xl, xr - xl + 1, col, alpha);
        }
    }

    // A ring around (cx, cy) at `radius`, `hw` either side: each row only visits the pixels
    // whose distance from the centre is within the ring
    template <class F>
    void ring(float cx, float cy, float radius, float hw, F&& f) {
        const float ro = radius + hw + 1.5f, ri = std::max(0.0f, radius - hw - 1.5f);
        shapeRows(cx - ro, cy - ro, cx + ro, cy + ro, [&](float py, float& a0, float& a1, float& b0, float& b1) {
            const float d = py - cy;
            const float xo = std::sqrt(std::max(0.0f, ro * ro - d * d));
            const float xi = std::fabs(d) < ri ? std::sqrt(ri * ri - d * d) : 0.0f;
            a0 = cx - xo; a1 = cx - xi + 1.0f;
            b0 = cx + xi - 1.0f; b1 = cx + xo;
        }, f);
    }

    static uint32_t pack(Color c) {
        auto ch = [](float v) { return static_cast<uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
        return (ch(c.r) << 16) | (ch(c.g) << 8) | ch(c.b);
    }
    static void fillSpan(uint32_t* dst, int n, Color c, float alpha) {
        const uint32_t src = pack(c);
        if (alpha >= 0.999f) { std::fill(dst, dst + n, src); return; }
        const uint32_t a = static_cast<uint32_t>(alpha * 256.0f + 0.5f);
        for (int i = 0; i < n; ++i) dst[i] = blendPacked(dst[i], src, a);
    }
    // a: 0 .. 256
    static uint32_t blendPacked(uint32_t d, uint32_t s, uint32_t a) {
        const uint32_t ia = 256 - a;
        const uint32_t rb = (((s & 0xFF00FF) * a + (d & 0xFF00FF) * ia) >> 8) & 0xFF00FF;
        const uint32_t g = (((s & 0x00FF00) * a + (d & 0x00FF00) * ia) >> 8) & 0x00FF00;
        return rb | g;
    }
    static void blend(uint32_t& dst, Color c, float a) {
        if (a <= 0.0f) return;
        dst = blendPacked(dst, pack(c), static_cast<uint32_t>(std::min(a, 1.0f) * 256.0f + 0.5f));
    }

    void blitGlyph(const fontatlas::Face* face, const fontatlas::Glyph& g, int ox, int oy, Color c) {
        const uint8_t* src = face->pixels + g.offset;
        for (int j = 0; j < g.h; ++j) {
            const int dy = oy + j;
            if (dy < state_.clip.y0 || dy >= state_.clip.y1) continue;
            uint32_t* row = pixels_.data() + static_cast<size_t>(dy) * w_;
            for (int i = 0; i < g.w; ++i) {
                const int dx = ox + i;
                if (dx < state_.clip.x0 || dx >= state_.clip.x1) continue;
                const uint8_t a = src[j * g.w + i];
                if (a) blend(row[dx], c, (a / 255.0f) * c.a * state_.alpha);
            }
        }
    }

    // The face to draw a style with, and the scale from that face to the style's size
    static const fontatlas::Face* faceFor(const TextStyle& st, float& ratio) {
        const fontatlas::Face* best = &fontatlas::kFaces[0];
        float bestScore = 1e9f;
        for (int i = 0; i < fontatlas::kNumFaces; ++i) {
            const auto& f = fontatlas::kFaces[i];
            const float score = std::fabs(f.size - st.size) * 4.0f + std::abs(f.weight - static_cast<int>(st.weight));
            if (score < bestScore) { bestScore = score; best = &f; }
        }
        if (std::fabs(best->size - st.size) < 0.26f) { ratio = 1.0f; return best; }
        // Not in the atlas: the largest face of the right weight at or below the size (or the
        // nearest one), magnified; downsampling a larger face keeps strokes even
        const fontatlas::Face* pick = nullptr;
        for (int i = 0; i < fontatlas::kNumFaces; ++i) {
            const auto& f = fontatlas::kFaces[i];
            if (f.weight != static_cast<int>(st.weight)) continue;
            if (!pick || std::fabs(f.size - st.size) < std::fabs(pick->size - st.size)) pick = &f;
        }
        if (!pick) pick = best;
        ratio = st.size / pick->size;
        return pick;
    }

    void blitGlyphScaled(const fontatlas::Face* face, const fontatlas::Glyph& g, float ox, float oy, float ratio, Color c) {
        const uint8_t* src = face->pixels + g.offset;
        const int w = static_cast<int>(std::ceil(g.w * ratio)) + 1, h = static_cast<int>(std::ceil(g.h * ratio)) + 1;
        const int x0 = static_cast<int>(std::floor(ox)), y0 = static_cast<int>(std::floor(oy));
        auto at = [&](int i, int j) -> float {
            if (i < 0 || j < 0 || i >= g.w || j >= g.h) return 0.0f;
            return src[j * g.w + i] / 255.0f;
        };
        for (int j = 0; j < h; ++j) {
            const int dy = y0 + j;
            if (dy < state_.clip.y0 || dy >= state_.clip.y1) continue;
            uint32_t* row = pixels_.data() + static_cast<size_t>(dy) * w_;
            const float sy = (dy + 0.5f - oy) / ratio - 0.5f;
            const int iy = static_cast<int>(std::floor(sy));
            const float fy = sy - iy;
            for (int i = 0; i < w; ++i) {
                const int dx = x0 + i;
                if (dx < state_.clip.x0 || dx >= state_.clip.x1) continue;
                const float sx = (dx + 0.5f - ox) / ratio - 0.5f;
                const int ix = static_cast<int>(std::floor(sx));
                const float fx = sx - ix;
                const float a = (at(ix, iy) * (1 - fx) + at(ix + 1, iy) * fx) * (1 - fy) +
                                (at(ix, iy + 1) * (1 - fx) + at(ix + 1, iy + 1) * fx) * fy;
                if (a > 0.003f) blend(row[dx], c, a * c.a * state_.alpha);
            }
        }
    }
    static const fontatlas::Glyph* glyphFor(const fontatlas::Face* f, uint32_t cp) {
        for (int i = 0; i < f->numGlyphs; ++i) if (f->glyphs[i].cp == cp) return &f->glyphs[i];
        for (int i = 0; i < f->numGlyphs; ++i) if (f->glyphs[i].cp == '?') return &f->glyphs[i];
        return nullptr;
    }
    static uint32_t nextCodepoint(const std::string& s, size_t& i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) { ++i; return c; }
        int n = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : 1;
        uint32_t cp = c & (0x3F >> n);
        ++i;
        while (n-- > 0 && i < s.size()) cp = (cp << 6) | (static_cast<unsigned char>(s[i++]) & 0x3F);
        return cp;
    }

    int w_ = 0, h_ = 0;
    std::vector<uint32_t> pixels_;
    State state_{{0, 0, 0, 0}, 1.0f};
    std::vector<State> stack_;
};

} // namespace phs::gui
