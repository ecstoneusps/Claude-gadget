#pragma once
#include <stdint.h>

// Minimal drawing surface the face engine renders into.
// On the device this wraps an M5Canvas (LovyanGFX sprite); in the desktop
// preview it wraps a plain software framebuffer. Colors are RGB565.
class Canvas {
public:
    virtual ~Canvas() = default;
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual void fillScreen(uint16_t c) = 0;
    virtual void fillRect(int x, int y, int w, int h, uint16_t c) = 0;
    virtual void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) = 0;
    virtual void fillCircle(int x, int y, int r, uint16_t c) = 0;
    virtual void fillEllipse(int x, int y, int rx, int ry, uint16_t c) = 0;
    virtual void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) = 0;
};

inline uint16_t rgb565(int r, int g, int b) {
    r = r < 0 ? 0 : (r > 255 ? 255 : r);
    g = g < 0 ? 0 : (g > 255 ? 255 : g);
    b = b < 0 ? 0 : (b > 255 ? 255 : b);
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
