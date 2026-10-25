// canvas.h - the picture (8 colours, one byte per pixel) and saving it as an IFF ILBM.
#pragma once

#include <string>
#include <vector>
#include <exec/types.h>

class Canvas {
public:
    enum { COLOURS = 8, DEPTH = 3 };
    static const uint32 palette[COLOURS];   // 0xAARRGGBB; colour 0 is the paper

    Canvas(int width, int height) : w(width), h(height), pixels(width * height, 0) {}
    int width() const { return w; }
    int height() const { return h; }
    uint8 at(int x, int y) const { return pixels[y * w + x]; }
    void clear() { pixels.assign(pixels.size(), 0); }
    // A round dot of `radius` in colour `pen`, clipped to the canvas; calls span(x0, x1, y)
    // for each row it changed, so the window can draw the same thing.
    template <class Span> void dot(int cx, int cy, int radius, uint8 pen, Span span);

    // Write an IFF ILBM picture with iffparse.library; false (and why) if it failed.
    bool saveILBM(const std::string &path, std::string &error) const;

private:
    int w, h;
    std::vector<uint8> pixels;
};

template <class Span> void Canvas::dot(int cx, int cy, int radius, uint8 pen, Span span)
{
    for (int dy = -radius; dy <= radius; dy++) {
        int y = cy + dy;
        if (y < 0 || y >= h) continue;
        int dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= radius * radius) dx++;
        int x0 = cx - dx < 0 ? 0 : cx - dx, x1 = cx + dx >= w ? w - 1 : cx + dx;
        if (x0 > x1) continue;
        for (int x = x0; x <= x1; x++) pixels[y * w + x] = pen;
        span(x0, x1, y);
    }
}
