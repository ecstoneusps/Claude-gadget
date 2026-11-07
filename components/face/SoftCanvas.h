#pragma once
// A tiny software rasterizer implementing the face's Canvas, for the desktop
// preview and the unit tests.
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <algorithm>
#include <vector>

#include "Canvas.h"

class SoftCanvas : public Canvas {
public:
    SoftCanvas(int w, int h) : w_(w), h_(h), px_(w * h, 0) {}
    int width() const override { return w_; }
    int height() const override { return h_; }

    void fillScreen(uint16_t c) override { std::fill(px_.begin(), px_.end(), c); }

    void fillRect(int x, int y, int w, int h, uint16_t c) override {
        for (int j = std::max(0, y); j < std::min(h_, y + h); ++j)
            for (int i = std::max(0, x); i < std::min(w_, x + w); ++i) px_[j * w_ + i] = c;
    }

    void fillCircle(int x, int y, int r, uint16_t c) override { fillEllipse(x, y, r, r, c); }

    void fillEllipse(int x, int y, int rx, int ry, uint16_t c) override {
        if (rx <= 0 || ry <= 0) return;
        for (int dy = -ry; dy <= ry; ++dy) {
            float f = 1.0f - (float)(dy * dy) / (float)(ry * ry);
            int dx = (int)(rx * sqrtf(f > 0 ? f : 0) + 0.5f);
            hline(x - dx, x + dx, y + dy, c);
        }
    }

    void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) override {
        if (w <= 0 || h <= 0) return;
        r = std::min(r, std::min(w, h) / 2);
        for (int j = 0; j < h; ++j) {
            int inset = 0;
            int dy = (j < r) ? (r - j) : (j >= h - r ? j - (h - r - 1) : 0);
            if (dy > 0) inset = r - (int)(sqrtf((float)(r * r - dy * dy)) + 0.5f);
            hline(x + inset, x + w - 1 - inset, y + j, c);
        }
    }

    void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) override {
        int minx = std::max(0, std::min({x0, x1, x2})), maxx = std::min(w_ - 1, std::max({x0, x1, x2}));
        int miny = std::max(0, std::min({y0, y1, y2})), maxy = std::min(h_ - 1, std::max({y0, y1, y2}));
        auto edge = [](int ax, int ay, int bx, int by, int px, int py) {
            return (long)(bx - ax) * (py - ay) - (long)(by - ay) * (px - ax);
        };
        for (int y = miny; y <= maxy; ++y)
            for (int x = minx; x <= maxx; ++x) {
                long a = edge(x0, y0, x1, y1, x, y), b = edge(x1, y1, x2, y2, x, y), d = edge(x2, y2, x0, y0, x, y);
                if ((a >= 0 && b >= 0 && d >= 0) || (a <= 0 && b <= 0 && d <= 0)) px_[y * w_ + x] = c;
            }
    }

    uint16_t at(int x, int y) const { return px_[y * w_ + x]; }
    void set(int x, int y, uint16_t c) { px_[y * w_ + x] = c; }

    void writeRGB(FILE* f) const {
        for (uint16_t p : px_) {
            uint8_t rgb[3] = {(uint8_t)(((p >> 11) & 0x1F) * 255 / 31), (uint8_t)(((p >> 5) & 0x3F) * 255 / 63),
                              (uint8_t)((p & 0x1F) * 255 / 31)};
            fwrite(rgb, 1, 3, f);
        }
    }

private:
    void hline(int x0, int x1, int y, uint16_t c) {
        if (y < 0 || y >= h_) return;
        for (int x = std::max(0, x0); x <= std::min(w_ - 1, x1); ++x) px_[y * w_ + x] = c;
    }
    int w_, h_;
    std::vector<uint16_t> px_;
};
