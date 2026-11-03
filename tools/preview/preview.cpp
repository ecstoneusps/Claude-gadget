// Desktop preview of the face engine. Renders frames with a tiny software
// rasterizer and writes them as raw RGB so make_preview.py can build a GIF
// and a contact sheet. Build & run via tools/preview/make_preview.sh.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <vector>

#include "Face.h"

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

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s anim|sheet <out.rgb>\n", argv[0]);
        return 1;
    }
    const int W = 320, H = 240;
    FILE* out = fopen(argv[2], "wb");
    if (!out) return 1;

    const int N = (int)Expression::Count;
    if (strcmp(argv[1], "sheet") == 0) {
        // One settled, non-blinking frame per expression, eyes centered.
        for (int i = 0; i < N; ++i) {
            Face face;
            face.setAutoBlink(false);
            face.setLookAt(0, 0);
            face.setExpression((Expression)i);
            for (int t = 0; t < 60; ++t) face.update(33);
            SoftCanvas c(W, H);
            face.draw(c);
            c.writeRGB(out);
            printf("%s\n", expressionName((Expression)i));
        }
    } else {
        // Animated tour: each expression for ~2s, with a "talking" segment.
        Face face;
        face.setSeed(42);
        const int fps = 25, dt = 1000 / fps;
        int frames = 0;
        for (int i = 0; i < N; ++i) {
            face.setExpression((Expression)i);
            for (int f = 0; f < fps * 2; ++f) {
                bool talking = (Expression)i == Expression::Neutral && f > fps / 2;
                face.setTalkLevel(talking ? 0.5f + 0.5f * sinf(f * 1.7f) * sinf(f * 0.6f) : 0);
                face.update(dt);
                SoftCanvas c(W, H);
                face.draw(c);
                c.writeRGB(out);
                ++frames;
            }
        }
        printf("%d frames\n", frames);
    }
    fclose(out);
    return 0;
}
