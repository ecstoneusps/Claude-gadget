#pragma once
#include "Canvas.h"

// Wraps the canvas the face draws into and keeps track of where it drew, so
// only that part needs clearing and sending to the display.
//
// The face clears the whole screen to the background color every frame and
// then draws in the middle. Outside the area drawn last frame the screen
// already is background, so fillScreen() only clears that area; afterwards
// dirty() is what changed on screen (last frame's area plus this frame's).
//
// Per frame: beginFrame(), draw, then send dirty() to the display.
// invalidateAll(): something else covered the screen (a photo, the camera
// preview); the next frame clears and sends everything.
class DirtyCanvas : public Canvas {
public:
    struct Rect {
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // half-open: [x0, x1) x [y0, y1)
        bool empty() const { return x1 <= x0 || y1 <= y0; }
        int width() const { return x1 - x0; }
        int height() const { return y1 - y0; }
    };

    explicit DirtyCanvas(Canvas& inner) : c_(inner) { invalidateAll(); }

    void beginFrame() {
        prev_ = cur_;
        cur_ = Rect{};
    }
    void invalidateAll() { cur_ = full(); }
    Rect dirty() const { return clip(unite(prev_, cur_)); }

    int width() const override { return c_.width(); }
    int height() const override { return c_.height(); }

    void fillScreen(uint16_t col) override {
        if (!haveBg_ || col != bg_) {
            // A new background color: everything changes.
            haveBg_ = true;
            bg_ = col;
            prev_ = full();
        }
        const Rect r = clip(prev_);
        if (!r.empty()) c_.fillRect(r.x0, r.y0, r.width(), r.height(), col);
    }
    void fillRect(int x, int y, int w, int h, uint16_t col) override {
        grow(x, y, x + w, y + h);
        c_.fillRect(x, y, w, h, col);
    }
    void fillRoundRect(int x, int y, int w, int h, int r, uint16_t col) override {
        grow(x, y, x + w, y + h);
        c_.fillRoundRect(x, y, w, h, r, col);
    }
    void fillCircle(int x, int y, int r, uint16_t col) override {
        grow(x - r, y - r, x + r + 1, y + r + 1);
        c_.fillCircle(x, y, r, col);
    }
    void fillEllipse(int x, int y, int rx, int ry, uint16_t col) override {
        grow(x - rx, y - ry, x + rx + 1, y + ry + 1);
        c_.fillEllipse(x, y, rx, ry, col);
    }
    void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t col) override {
        grow(min3(x0, x1, x2), min3(y0, y1, y2), max3(x0, x1, x2) + 1, max3(y0, y1, y2) + 1);
        c_.fillTriangle(x0, y0, x1, y1, x2, y2, col);
    }

private:
    // A pixel of slack around every shape, in case a renderer rounds a
    // curve's edge differently.
    static constexpr int kSlack = 2;

    static int min3(int a, int b, int c) { return a < b ? (a < c ? a : c) : (b < c ? b : c); }
    static int max3(int a, int b, int c) { return a > b ? (a > c ? a : c) : (b > c ? b : c); }
    Rect full() const { return Rect{0, 0, c_.width(), c_.height()}; }
    Rect clip(Rect r) const {
        if (r.x0 < 0) r.x0 = 0;
        if (r.y0 < 0) r.y0 = 0;
        if (r.x1 > c_.width()) r.x1 = c_.width();
        if (r.y1 > c_.height()) r.y1 = c_.height();
        return r;
    }
    static Rect unite(const Rect& a, const Rect& b) {
        if (a.empty()) return b;
        if (b.empty()) return a;
        return Rect{a.x0 < b.x0 ? a.x0 : b.x0, a.y0 < b.y0 ? a.y0 : b.y0, a.x1 > b.x1 ? a.x1 : b.x1,
                    a.y1 > b.y1 ? a.y1 : b.y1};
    }
    void grow(int x0, int y0, int x1, int y1) {
        if (x1 <= x0 || y1 <= y0) return;
        cur_ = unite(cur_, Rect{x0 - kSlack, y0 - kSlack, x1 + kSlack, y1 + kSlack});
    }

    Canvas& c_;
    Rect prev_, cur_;
    bool haveBg_ = false;
    uint16_t bg_ = 0;
};
