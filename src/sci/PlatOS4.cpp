// Scintilla platform layer for AmigaOS 4.1: surfaces on RastPorts, fonts from diskfont,
// popup windows for the autocomplete list and calltips.

#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <chrono>
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <optional>
#include <algorithm>
#include <memory>
#include <functional>

#include "ScintillaTypes.h"
#include "Debugging.h"
#include "Geometry.h"
#include "Platform.h"
#include "UniConversion.h"
#include "sci/PlatOS4.h"

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <proto/intuition.h>
#include <proto/diskfont.h>
#include <graphics/rpattr.h>
#include <graphics/gfxmacros.h>
#include <intuition/pointerclass.h>

using namespace Scintilla;

namespace Scintilla::Internal {

namespace {

struct ::Screen *currentScreen = nullptr;

int Round(XYPOSITION v) noexcept { return static_cast<int>(std::lround(v)); }
int Floor(XYPOSITION v) noexcept { return static_cast<int>(std::floor(v)); }

uint32_t ARGB(ColourRGBA c) noexcept {
    return 0xFF000000u | (uint32_t(c.GetRed()) << 16) | (uint32_t(c.GetGreen()) << 8) | c.GetBlue();
}

// Palette pens for 8-bit screens, shared by all surfaces on that screen.
struct PenCache {
    struct ::Screen *screen = nullptr;
    std::map<uint32_t, LONG> pens;

    LONG Get(struct ::Screen *scr, ColourRGBA c) {
        if (!scr)
            return 1;
        if (scr != screen) {
            Release();
            screen = scr;
        }
        const uint32_t key = ARGB(c);
        auto it = pens.find(key);
        if (it != pens.end())
            return it->second;
        LONG pen = IGraphics->ObtainBestPen(scr->ViewPort.ColorMap,
            c.GetRed() * 0x01010101u, c.GetGreen() * 0x01010101u, c.GetBlue() * 0x01010101u,
            OBP_Precision, PRECISION_IMAGE, TAG_END);
        if (pen < 0)
            pen = 1;
        pens[key] = pen;
        return pen;
    }
    void Release() {
        if (screen) {
            for (auto &p : pens)
                IGraphics->ReleasePen(screen->ViewPort.ColorMap, p.second);
        }
        pens.clear();
        screen = nullptr;
    }
};
PenCache penCache;

// Latin-1 <-> UTF-8 for the *UTF8 drawing entry points.
std::string Latin1FromUTF8(std::string_view sv) {
    std::string out;
    out.reserve(sv.size());
    for (size_t i = 0; i < sv.size(); ) {
        const unsigned char c = sv[i];
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
            i++;
            continue;
        }
        const int len = UTF8BytesOfLead[c];
        unsigned int cp = '?';
        if (len == 2 && i + 1 < sv.size())
            cp = ((c & 0x1F) << 6) | (sv[i + 1] & 0x3F);
        out.push_back(cp < 0x100 ? static_cast<char>(cp) : '?');
        i += len;
    }
    return out;
}

} // namespace

void OS4SetCurrentScreen(struct ::Screen *screen) {
    currentScreen = screen;
}

void OS4ReleasePens(struct ::Screen *screen) {
    if (penCache.screen == screen)
        penCache.Release();
}

unsigned int OS4Milliseconds() {
    using namespace std::chrono;
    return static_cast<unsigned int>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

// --- off-screen buffers ---

bool OffscreenBuffer::Allocate(int w, int h, struct BitMap *friendBitMap) {
    Free();
    w = std::max(w, 1);
    h = std::max(h, 1);
    const uint32 depth = friendBitMap ? IGraphics->GetBitMapAttr(friendBitMap, BMA_DEPTH) : 8;
    bm = IGraphics->AllocBitMapTags(w, h, depth,
        BMATags_Friend, friendBitMap,
        BMATags_Clear, TRUE,
        TAG_END);
    if (!bm)
        return false;
    li = ILayers->NewLayerInfo();
    if (li)
        layer = ILayers->CreateUpfrontLayer(li, bm, 0, 0, w - 1, h - 1, LAYERSIMPLE, nullptr);
    if (!layer) {
        Free();
        return false;
    }
    width = w;
    height = h;
    return true;
}

void OffscreenBuffer::Free() {
    if (layer) {
        ILayers->InstallClipRegion(layer, nullptr);
        ILayers->DeleteLayer(0, layer);
        layer = nullptr;
    }
    if (li) {
        ILayers->DisposeLayerInfo(li);
        li = nullptr;
    }
    if (bm) {
        IGraphics->WaitBlit();
        IGraphics->FreeBitMap(bm);
        bm = nullptr;
    }
    width = height = 0;
}

struct RastPort *OffscreenBuffer::RP() const {
    return layer ? layer->rp : nullptr;
}

// --- fonts ---

class FontOS4 : public Font {
public:
    struct TextFont *tf = nullptr;
    ULONG style = FS_NORMAL;      // soft style applied when drawing
    int widths[256] = {};
    int ascent = 0, descent = 0, height = 0, aveWidth = 0;

    explicit FontOS4(const FontParameters &fp) {
        std::string name = fp.faceName ? fp.faceName : Platform::DefaultFont();
        if (name.size() < 5 || name.compare(name.size() - 5, 5, ".font") != 0)
            name += ".font";
        const UWORD ysize = static_cast<UWORD>(std::max(4, Round(fp.size)));
        UBYTE wanted = FS_NORMAL;
        if (static_cast<int>(fp.weight) >= static_cast<int>(FontWeight::SemiBold))
            wanted |= FSF_BOLD;
        if (fp.italic)
            wanted |= FSF_ITALIC;
        struct TextAttr ta = { (STRPTR)name.c_str(), ysize, wanted, 0 };
        tf = IDiskfont->OpenDiskFont(&ta);
        if (!tf) {
            struct TextAttr dejavu = { (STRPTR)"DejaVu Sans Mono.font", ysize, wanted, 0 };
            tf = IDiskfont->OpenDiskFont(&dejavu);
        }
        if (!tf) {
            struct TextAttr topaz = { (STRPTR)"topaz.font", 8, wanted, 0 };
            tf = IGraphics->OpenFont(&topaz);
        }
        if (!tf)
            return;
        // Styles the font lacks are applied algorithmically.
        struct RastPort rp;
        IGraphics->InitRastPort(&rp);
        IGraphics->SetFont(&rp, tf);
        style = IGraphics->SetSoftStyle(&rp, wanted, IGraphics->AskSoftStyle(&rp)) & ~tf->tf_Style;
        for (int c = 0; c < 256; c++) {
            const char ch = static_cast<char>(c);
            widths[c] = IGraphics->TextLength(&rp, &ch, 1);
        }
        // Outline fonts can draw outside tf_YSize (the underscore below it): use the real
        // extent so the line below does not paint over those pixels.
        struct TextExtent te;
        IGraphics->FontExtent(tf, &te);
        ascent = std::max(tf->tf_Baseline + 1, -te.te_Extent.MinY);
        descent = std::max(tf->tf_YSize - tf->tf_Baseline - 1, te.te_Extent.MaxY + 1);
        height = ascent + descent;
        aveWidth = widths['n'] > 0 ? widths['n'] : tf->tf_XSize;
    }
    ~FontOS4() noexcept override {
        if (tf)
            IGraphics->CloseFont(tf);
    }
    void SetOn(struct RastPort *rp) const {
        if (rp->Font != tf)
            IGraphics->SetFont(rp, tf);
        IGraphics->SetSoftStyle(rp, style, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
    }
};

std::shared_ptr<Font> Font::Allocate(const FontParameters &fp) {
    return std::make_shared<FontOS4>(fp);
}

namespace {
const FontOS4 *AsOS4(const Font *f) noexcept {
    const FontOS4 *font = static_cast<const FontOS4 *>(f);
    return (font && font->tf) ? font : nullptr;
}
}

// --- surfaces ---

class SurfaceOS4 : public Surface {
    struct RastPort *rp = nullptr;
    std::unique_ptr<OffscreenBuffer> owned;    // for pixmaps
    struct ::Screen *screen = nullptr;
    bool trueColour = true;
    bool readable = false;                     // off-screen: pixels can be read back
    struct RastPort raw;                       // layerless RastPort on the same bitmap
    int originX = 0, originY = 0;              // layer position in its bitmap
    int width = 0, height = 0;
    SurfaceMode mode;

    struct Clip { int x0, y0, x1, y1; };
    std::vector<Clip> clips;
    struct Region *region = nullptr;
    struct Region *oldRegion = nullptr;
    bool regionInstalled = false;

    void Attach(struct RastPort *rp_) {
        rp = rp_;
        screen = currentScreen;
        if (!rp)
            return;
        trueColour = IGraphics->GetBitMapAttr(rp->BitMap, BMA_DEPTH) > 8;
        if (rp->Layer) {
            const struct ::Layer *l = rp->Layer;
            width = l->bounds.MaxX - l->bounds.MinX + 1;
            height = l->bounds.MaxY - l->bounds.MinY + 1;
            readable = l->Window == nullptr;
            originX = l->bounds.MinX;
            originY = l->bounds.MinY;
        } else {
            width = IGraphics->GetBitMapAttr(rp->BitMap, BMA_WIDTH);
            height = IGraphics->GetBitMapAttr(rp->BitMap, BMA_HEIGHT);
            readable = true;
        }
        IGraphics->InitRastPort(&raw);
        raw.BitMap = rp->BitMap;
        IGraphics->SetDrMd(rp, JAM1);
    }

    void Pen(ColourRGBA c) {
        if (trueColour)
            IGraphics->SetRPAttrs(rp, RPTAG_APenColor, ARGB(c), TAG_END);
        else
            IGraphics->SetAPen(rp, penCache.Get(screen, c));
    }

    Clip CurrentClip() const noexcept {
        Clip c = { 0, 0, width, height };
        if (!clips.empty()) {
            const Clip &t = clips.back();
            c = { std::max(c.x0, t.x0), std::max(c.y0, t.y0), std::min(c.x1, t.x1), std::min(c.y1, t.y1) };
        }
        return c;
    }

    void ApplyClip() {
        if (!rp || !rp->Layer)
            return;
        if (clips.empty()) {
            if (regionInstalled) {
                ILayers->InstallClipRegion(rp->Layer, oldRegion);
                regionInstalled = false;
            }
            return;
        }
        if (!region)
            region = IGraphics->NewRegion();
        if (!region)
            return;
        // Layers only rebuilds its clip rects when a different region is installed, so take
        // ours out before changing it.
        if (regionInstalled)
            ILayers->InstallClipRegion(rp->Layer, nullptr);
        IGraphics->ClearRegion(region);
        const Clip &c = clips.back();
        if (c.x1 > c.x0 && c.y1 > c.y0) {
            struct Rectangle r = { (WORD)c.x0, (WORD)c.y0, (WORD)(c.x1 - 1), (WORD)(c.y1 - 1) };
            IGraphics->OrRectRegion(region, &r);
        }
        struct Region *prev = ILayers->InstallClipRegion(rp->Layer, region);
        if (!regionInstalled) {
            oldRegion = prev;
            regionInstalled = true;
        }
    }

    // Opaque fill of [x0,x1) x [y0,y1).
    void Fill(int x0, int y0, int x1, int y1, ColourRGBA c) {
        if (x1 <= x0 || y1 <= y0)
            return;
        Pen(c);
        IGraphics->RectFill(rp, x0, y0, x1 - 1, y1 - 1);
    }

    // Translucent fill. Pixels are read back on off-screen true-colour surfaces; elsewhere
    // the colour is approximated by mixing with white (the editor's backgrounds are light).
    void Blend(int x0, int y0, int x1, int y1, ColourRGBA c) {
        const unsigned alpha = c.GetAlpha();
        if (alpha == 0)
            return;
        if (alpha == 255) {
            Fill(x0, y0, x1, y1, c);
            return;
        }
        if (!readable || !trueColour) {
            Fill(x0, y0, x1, y1, ColourRGBA(0xFF, 0xFF, 0xFF).MixedWith(c.Opaque(), alpha / 255.0));
            return;
        }
        const Clip clip = CurrentClip();
        x0 = std::max(x0, clip.x0); y0 = std::max(y0, clip.y0);
        x1 = std::min(x1, clip.x1); y1 = std::min(y1, clip.y1);
        if (x1 <= x0 || y1 <= y0)
            return;
        const int w = x1 - x0, h = y1 - y0;
        std::vector<uint32_t> buf(size_t(w) * h);
        ReadPixels(buf.data(), x0, y0, w, h);
        const unsigned ia = 255 - alpha;
        const unsigned r = c.GetRed() * alpha, g = c.GetGreen() * alpha, b = c.GetBlue() * alpha;
        for (uint32_t &p : buf) {
            const unsigned pr = (p >> 16) & 0xFF, pg = (p >> 8) & 0xFF, pb = p & 0xFF;
            p = 0xFF000000u | (((pr * ia + r) / 255) << 16) | (((pg * ia + g) / 255) << 8) | ((pb * ia + b) / 255);
        }
        WritePixels(buf.data(), x0, y0, w, h);
    }

    void ReadPixels(uint32_t *buf, int x, int y, int w, int h) {
        IGraphics->ReadPixelArray(&raw, x + originX, y + originY, reinterpret_cast<uint8 *>(buf),
            0, 0, w * 4, PIXF_A8R8G8B8, w, h);
    }
    void WritePixels(uint32_t *buf, int x, int y, int w, int h) {
        IGraphics->WritePixelArray(reinterpret_cast<uint8 *>(buf), 0, 0, w * 4, PIXF_A8R8G8B8,
            &raw, x + originX, y + originY, w, h);
    }

    void FillPolygon(const Point *pts, size_t npts, ColourRGBA c);
    void StrokePolyline(const Point *pts, size_t npts, bool closed, Stroke stroke);
    void SmoothEllipse(PRectangle rc, FillStroke fs);
    void DrawText(PRectangle rc, const Font *font_, XYPOSITION ybase, std::string_view text, ColourRGBA fore);

public:
    SurfaceOS4() noexcept {
        IGraphics->InitRastPort(&raw);
    }
    ~SurfaceOS4() noexcept override {
        Release();
    }

    void Init(WindowID) override {
        Release();
    }
    void Init(SurfaceID sid, WindowID) override {
        Release();
        Attach(static_cast<struct RastPort *>(sid));
    }
    std::unique_ptr<Surface> AllocatePixMap(int w, int h) override {
        auto s = std::make_unique<SurfaceOS4>();
        auto buffer = std::make_unique<OffscreenBuffer>();
        struct BitMap *friendBm = (rp && rp->BitMap) ? rp->BitMap
            : (currentScreen ? currentScreen->RastPort.BitMap : nullptr);
        if (buffer->Allocate(w, h, friendBm)) {
            s->Attach(buffer->RP());
            s->owned = std::move(buffer);
        }
        s->mode = mode;
        return s;
    }

    void SetMode(SurfaceMode mode_) override { mode = mode_; }

    void Release() noexcept override {
        if (rp && rp->Layer && regionInstalled)
            ILayers->InstallClipRegion(rp->Layer, oldRegion);
        regionInstalled = false;
        oldRegion = nullptr;
        if (region) {
            IGraphics->DisposeRegion(region);
            region = nullptr;
        }
        clips.clear();
        rp = nullptr;
        owned.reset();
    }
    int SupportsFeature(Supports feature) noexcept override {
        return feature == Supports::LineDrawsFinal ? 1 : 0;
    }
    bool Initialised() override { return rp != nullptr; }
    int LogPixelsY() override { return 72; }
    int PixelDivisions() override { return 1; }
    int DeviceHeightFont(int points) override { return points; }

    void LineDraw(Point start, Point end, Stroke stroke) override {
        const Point pts[2] = { start, end };
        StrokePolyline(pts, 2, false, stroke);
    }
    void PolyLine(const Point *pts, size_t npts, Stroke stroke) override {
        StrokePolyline(pts, npts, false, stroke);
    }
    void Polygon(const Point *pts, size_t npts, FillStroke fs) override {
        FillPolygon(pts, npts, fs.fill.colour);
        StrokePolyline(pts, npts, true, fs.stroke);
    }
    void RectangleDraw(PRectangle rc, FillStroke fs) override {
        const int w = std::max(1, Round(fs.stroke.width));
        const int x0 = Round(rc.left), y0 = Round(rc.top), x1 = Round(rc.right), y1 = Round(rc.bottom);
        Blend(x0 + w, y0 + w, x1 - w, y1 - w, fs.fill.colour);
        RectangleFrame(rc, fs.stroke);
    }
    void RectangleFrame(PRectangle rc, Stroke stroke) override {
        const int w = std::max(1, Round(stroke.width));
        const int x0 = Round(rc.left), y0 = Round(rc.top), x1 = Round(rc.right), y1 = Round(rc.bottom);
        if (x1 - x0 <= 2 * w || y1 - y0 <= 2 * w) {
            Blend(x0, y0, x1, y1, stroke.colour);
            return;
        }
        Blend(x0, y0, x1, y0 + w, stroke.colour);
        Blend(x0, y1 - w, x1, y1, stroke.colour);
        Blend(x0, y0 + w, x0 + w, y1 - w, stroke.colour);
        Blend(x1 - w, y0 + w, x1, y1 - w, stroke.colour);
    }
    void FillRectangle(PRectangle rc, Scintilla::Internal::Fill fill) override {
        Blend(Round(rc.left), Round(rc.top), Round(rc.right), Round(rc.bottom), fill.colour);
    }
    void FillRectangleAligned(PRectangle rc, Scintilla::Internal::Fill fill) override {
        FillRectangle(rc, fill);
    }
    void FillRectangle(PRectangle rc, Surface &surfacePattern) override {
        SurfaceOS4 &pattern = static_cast<SurfaceOS4 &>(surfacePattern);
        const int x0 = Round(rc.left), y0 = Round(rc.top), x1 = Round(rc.right), y1 = Round(rc.bottom);
        if (!pattern.rp || pattern.width <= 0 || pattern.height <= 0) {
            Fill(x0, y0, x1, y1, ColourRGBA(0xC0, 0xC0, 0xC0));
            return;
        }
        // Tile from multiples of the pattern size so adjacent fills line up.
        const int pw = pattern.width, ph = pattern.height;
        for (int y = y0 - (y0 % ph + ph) % ph; y < y1; y += ph) {
            for (int x = x0 - (x0 % pw + pw) % pw; x < x1; x += pw) {
                const int sx = std::max(x, x0), sy = std::max(y, y0);
                const int ex = std::min(x + pw, x1), ey = std::min(y + ph, y1);
                if (ex > sx && ey > sy)
                    IGraphics->BltBitMapRastPort(pattern.rp->BitMap, sx - x, sy - y, rp, sx, sy, ex - sx, ey - sy, 0xC0);
            }
        }
    }
    void RoundedRectangle(PRectangle rc, FillStroke fs) override {
        const int x0 = Round(rc.left), y0 = Round(rc.top), x1 = Round(rc.right), y1 = Round(rc.bottom);
        if (x1 - x0 < 4 || y1 - y0 < 4) {
            RectangleDraw(rc, fs);
            return;
        }
        Blend(x0 + 1, y0 + 1, x1 - 1, y1 - 1, fs.fill.colour);
        Blend(x0 + 2, y0, x1 - 2, y0 + 1, fs.stroke.colour);
        Blend(x0 + 2, y1 - 1, x1 - 2, y1, fs.stroke.colour);
        Blend(x0, y0 + 2, x0 + 1, y1 - 2, fs.stroke.colour);
        Blend(x1 - 1, y0 + 2, x1, y1 - 2, fs.stroke.colour);
        Blend(x0 + 1, y0 + 1, x0 + 2, y0 + 2, fs.stroke.colour);
        Blend(x1 - 2, y0 + 1, x1 - 1, y0 + 2, fs.stroke.colour);
        Blend(x0 + 1, y1 - 2, x0 + 2, y1 - 1, fs.stroke.colour);
        Blend(x1 - 2, y1 - 2, x1 - 1, y1 - 1, fs.stroke.colour);
    }
    void AlphaRectangle(PRectangle rc, XYPOSITION cornerSize, FillStroke fs) override {
        const int x0 = Round(rc.left), y0 = Round(rc.top), x1 = Round(rc.right), y1 = Round(rc.bottom);
        const int sw = std::max(1, Round(fs.stroke.width));
        if (cornerSize <= 0 || x1 - x0 < 2 * sw + 2 || y1 - y0 < 2 * sw + 2) {
            RectangleDraw(rc, fs);
            return;
        }
        // Corners cut by one pixel.
        Blend(x0 + sw, y0 + sw, x1 - sw, y1 - sw, fs.fill.colour);
        Blend(x0 + 1, y0, x1 - 1, y0 + sw, fs.stroke.colour);
        Blend(x0 + 1, y1 - sw, x1 - 1, y1, fs.stroke.colour);
        Blend(x0, y0 + 1, x0 + sw, y1 - 1, fs.stroke.colour);
        Blend(x1 - sw, y0 + 1, x1, y1 - 1, fs.stroke.colour);
    }
    void GradientRectangle(PRectangle rc, const std::vector<ColourStop> &stops, GradientOptions options) override {
        if (stops.empty())
            return;
        const int x0 = Round(rc.left), y0 = Round(rc.top), x1 = Round(rc.right), y1 = Round(rc.bottom);
        const bool horizontal = options == GradientOptions::leftToRight;
        const int n = horizontal ? x1 - x0 : y1 - y0;
        for (int i = 0; i < n; i++) {
            const XYPOSITION pos = n > 1 ? XYPOSITION(i) / (n - 1) : 0;
            ColourRGBA c = stops.front().colour;
            for (size_t s = 1; s < stops.size(); s++) {
                if (pos <= stops[s].position) {
                    const XYPOSITION span = stops[s].position - stops[s - 1].position;
                    const XYPOSITION t = span > 0 ? (pos - stops[s - 1].position) / span : 0;
                    c = stops[s - 1].colour.MixedWith(stops[s].colour, t);
                    break;
                }
                c = stops[s].colour;
            }
            if (horizontal)
                Blend(x0 + i, y0, x0 + i + 1, y1, c);
            else
                Blend(x0, y0 + i, x1, y0 + i + 1, c);
        }
    }
    void DrawRGBAImage(PRectangle rc, int w, int h, const unsigned char *pixels) override {
        const int x0 = Round(rc.left), y0 = Round(rc.top);
        const Clip clip = CurrentClip();
        const int cx0 = std::max(x0, std::max(clip.x0, Round(rc.left)));
        const int cy0 = std::max(y0, std::max(clip.y0, Round(rc.top)));
        const int cx1 = std::min(x0 + w, std::min(clip.x1, Round(rc.right)));
        const int cy1 = std::min(y0 + h, std::min(clip.y1, Round(rc.bottom)));
        if (cx1 <= cx0 || cy1 <= cy0)
            return;
        const int bw = cx1 - cx0, bh = cy1 - cy0;
        if (readable && trueColour) {
            std::vector<uint32_t> buf(size_t(bw) * bh);
            ReadPixels(buf.data(), cx0, cy0, bw, bh);
            for (int y = 0; y < bh; y++) {
                for (int x = 0; x < bw; x++) {
                    const unsigned char *s = pixels + ((y + cy0 - y0) * w + (x + cx0 - x0)) * 4;
                    const unsigned a = s[3], ia = 255 - a;
                    uint32_t &p = buf[size_t(y) * bw + x];
                    const unsigned pr = (p >> 16) & 0xFF, pg = (p >> 8) & 0xFF, pb = p & 0xFF;
                    p = 0xFF000000u | (((pr * ia + s[0] * a) / 255) << 16) |
                        (((pg * ia + s[1] * a) / 255) << 8) | ((pb * ia + s[2] * a) / 255);
                }
            }
            WritePixels(buf.data(), cx0, cy0, bw, bh);
            return;
        }
        for (int y = cy0; y < cy1; y++) {
            for (int x = cx0; x < cx1; x++) {
                const unsigned char *s = pixels + ((y - y0) * w + (x - x0)) * 4;
                if (s[3] >= 128) {
                    Pen(ColourRGBA(s[0], s[1], s[2]));
                    IGraphics->WritePixel(rp, x, y);
                }
            }
        }
    }
    void Ellipse(PRectangle rc, FillStroke fs) override {
        SmoothEllipse(rc, fs);
    }
    void Stadium(PRectangle rc, FillStroke fs, Ends) override {
        AlphaRectangle(rc, rc.Height() / 2, fs);
    }
    void Copy(PRectangle rc, Point from, Surface &surfaceSource) override {
        SurfaceOS4 &src = static_cast<SurfaceOS4 &>(surfaceSource);
        if (!src.rp)
            return;
        const int w = Round(rc.Width()), h = Round(rc.Height());
        if (w > 0 && h > 0)
            IGraphics->BltBitMapRastPort(src.rp->BitMap, Round(from.x) + src.originX, Round(from.y) + src.originY,
                rp, Round(rc.left), Round(rc.top), w, h, 0xC0);
    }

    std::unique_ptr<IScreenLineLayout> Layout(const IScreenLine *) override { return {}; }

    void DrawTextNoClip(PRectangle rc, const Font *font_, XYPOSITION ybase, std::string_view text, ColourRGBA fore, ColourRGBA back) override {
        FillRectangle(rc, back);
        DrawTextTransparent(rc, font_, ybase, text, fore);
    }
    void DrawTextClipped(PRectangle rc, const Font *font_, XYPOSITION ybase, std::string_view text, ColourRGBA fore, ColourRGBA back) override {
        SetClip(rc);
        DrawTextNoClip(rc, font_, ybase, text, fore, back);
        PopClip();
    }
    void DrawTextTransparent(PRectangle rc, const Font *font_, XYPOSITION ybase, std::string_view text, ColourRGBA fore) override {
        if (mode.codePage == CpUtf8)
            DrawText(rc, font_, ybase, Latin1FromUTF8(text), fore);
        else
            DrawText(rc, font_, ybase, text, fore);
    }
    void MeasureWidths(const Font *font_, std::string_view text, XYPOSITION *positions) override {
        if (mode.codePage == CpUtf8) {
            MeasureWidthsUTF8(font_, text, positions);
            return;
        }
        const FontOS4 *font = AsOS4(font_);
        int x = 0;
        for (size_t i = 0; i < text.size(); i++) {
            x += font ? font->widths[static_cast<unsigned char>(text[i])] : 8;
            positions[i] = x;
        }
    }
    XYPOSITION WidthText(const Font *font_, std::string_view text) override {
        if (mode.codePage == CpUtf8)
            return WidthTextUTF8(font_, text);
        const FontOS4 *font = AsOS4(font_);
        int x = 0;
        for (unsigned char c : text)
            x += font ? font->widths[c] : 8;
        return x;
    }

    void DrawTextNoClipUTF8(PRectangle rc, const Font *font_, XYPOSITION ybase, std::string_view text, ColourRGBA fore, ColourRGBA back) override {
        FillRectangle(rc, back);
        DrawText(rc, font_, ybase, Latin1FromUTF8(text), fore);
    }
    void DrawTextClippedUTF8(PRectangle rc, const Font *font_, XYPOSITION ybase, std::string_view text, ColourRGBA fore, ColourRGBA back) override {
        SetClip(rc);
        DrawTextNoClipUTF8(rc, font_, ybase, text, fore, back);
        PopClip();
    }
    void DrawTextTransparentUTF8(PRectangle rc, const Font *font_, XYPOSITION ybase, std::string_view text, ColourRGBA fore) override {
        DrawText(rc, font_, ybase, Latin1FromUTF8(text), fore);
    }
    void MeasureWidthsUTF8(const Font *font_, std::string_view text, XYPOSITION *positions) override {
        // Every byte of a character gets the position after that character.
        const FontOS4 *font = AsOS4(font_);
        int x = 0;
        for (size_t i = 0; i < text.size(); ) {
            const unsigned char c = text[i];
            size_t len = c < 0x80 ? 1 : std::max(1, static_cast<int>(UTF8BytesOfLead[c]));
            len = std::min(len, text.size() - i);
            const std::string ch = Latin1FromUTF8(text.substr(i, len));
            x += font ? font->widths[static_cast<unsigned char>(ch.empty() ? '?' : ch[0])] : 8;
            for (size_t k = 0; k < len; k++)
                positions[i + k] = x;
            i += len;
        }
    }
    XYPOSITION WidthTextUTF8(const Font *font_, std::string_view text) override {
        const FontOS4 *font = AsOS4(font_);
        int x = 0;
        for (unsigned char c : Latin1FromUTF8(text))
            x += font ? font->widths[c] : 8;
        return x;
    }

    XYPOSITION Ascent(const Font *font_) override {
        const FontOS4 *font = AsOS4(font_);
        return font ? font->ascent : 7;
    }
    XYPOSITION Descent(const Font *font_) override {
        const FontOS4 *font = AsOS4(font_);
        return font ? font->descent : 1;
    }
    XYPOSITION InternalLeading(const Font *) override { return 0; }
    XYPOSITION Height(const Font *font_) override {
        const FontOS4 *font = AsOS4(font_);
        return font ? font->height : 8;
    }
    XYPOSITION AverageCharWidth(const Font *font_) override {
        const FontOS4 *font = AsOS4(font_);
        return font ? font->aveWidth : 8;
    }

    void SetClip(PRectangle rc) override {
        Clip c = { Floor(rc.left), Floor(rc.top), static_cast<int>(std::ceil(rc.right)), static_cast<int>(std::ceil(rc.bottom)) };
        if (!clips.empty()) {
            const Clip &t = clips.back();
            c = { std::max(c.x0, t.x0), std::max(c.y0, t.y0), std::min(c.x1, t.x1), std::min(c.y1, t.y1) };
        }
        clips.push_back(c);
        ApplyClip();
    }
    void PopClip() override {
        if (!clips.empty())
            clips.pop_back();
        ApplyClip();
    }
    void FlushCachedState() override {}
    void FlushDrawing() override {}
};

void SurfaceOS4::DrawText(PRectangle rc, const Font *font_, XYPOSITION ybase, std::string_view text, ColourRGBA fore) {
    const FontOS4 *font = AsOS4(font_);
    if (!rp || !font || text.empty())
        return;
    font->SetOn(rp);
    IGraphics->SetDrMd(rp, JAM1);
    Pen(fore);
    IGraphics->Move(rp, Round(rc.left), Round(ybase));
    while (!text.empty()) {
        const size_t n = std::min<size_t>(text.size(), 1024);
        IGraphics->Text(rp, text.data(), n);
        text.remove_prefix(n);
    }
}

// Even-odd scanline fill, sampling at pixel centres.
void SurfaceOS4::FillPolygon(const Point *pts, size_t npts, ColourRGBA c) {
    if (npts < 3 || c.GetAlpha() == 0)
        return;
    XYPOSITION ymin = pts[0].y, ymax = pts[0].y;
    for (size_t i = 1; i < npts; i++) {
        ymin = std::min(ymin, pts[i].y);
        ymax = std::max(ymax, pts[i].y);
    }
    std::vector<XYPOSITION> xs;
    for (int y = Floor(ymin); y < static_cast<int>(std::ceil(ymax)); y++) {
        const XYPOSITION yc = y + 0.5;
        xs.clear();
        for (size_t i = 0; i < npts; i++) {
            const Point &a = pts[i], &b = pts[(i + 1) % npts];
            if ((a.y <= yc && yc < b.y) || (b.y <= yc && yc < a.y))
                xs.push_back(a.x + (yc - a.y) * (b.x - a.x) / (b.y - a.y));
        }
        std::sort(xs.begin(), xs.end());
        for (size_t i = 0; i + 1 < xs.size(); i += 2)
            Blend(Round(xs[i]), y, Round(xs[i + 1]), y + 1, c);
    }
}

void SurfaceOS4::StrokePolyline(const Point *pts, size_t npts, bool closed, Stroke stroke) {
    if (npts < 2 || stroke.colour.GetAlpha() == 0 || stroke.width <= 0)
        return;
    Pen(stroke.colour.Opaque());
    const int w = std::max(1, Round(stroke.width));
    for (int k = 0; k < w; k++) {
        // Thick lines: offset copies perpendicular-ish (good enough for markers).
        const int d = k - (w - 1) / 2;
        IGraphics->Move(rp, Floor(pts[0].x) + d, Floor(pts[0].y));
        for (size_t i = 1; i < npts; i++)
            IGraphics->Draw(rp, Floor(pts[i].x) + d, Floor(pts[i].y));
        if (closed)
            IGraphics->Draw(rp, Floor(pts[0].x) + d, Floor(pts[0].y));
    }
}

// Ellipses (breakpoint markers) are drawn antialiased where pixels can be read back.
void SurfaceOS4::SmoothEllipse(PRectangle rc, FillStroke fs) {
    const int x0 = Floor(rc.left), y0 = Floor(rc.top);
    const int x1 = static_cast<int>(std::ceil(rc.right)), y1 = static_cast<int>(std::ceil(rc.bottom));
    const XYPOSITION cx = (rc.left + rc.right) / 2, cy = (rc.top + rc.bottom) / 2;
    const XYPOSITION rx = rc.Width() / 2, ry = rc.Height() / 2;
    const XYPOSITION sw = fs.stroke.width;
    if (rx <= 0 || ry <= 0)
        return;
    if (!readable || !trueColour) {
        for (int y = y0; y < y1; y++) {
            const XYPOSITION dy = (y + 0.5 - cy) / ry;
            if (std::fabs(dy) >= 1)
                continue;
            const XYPOSITION half = rx * std::sqrt(1 - dy * dy);
            const XYPOSITION ihalf = std::max<XYPOSITION>(0, half - sw);
            Fill(Round(cx - half), y, Round(cx + half), y + 1, fs.stroke.colour.Opaque());
            if (std::fabs(y + 0.5 - cy) < ry - sw)
                Fill(Round(cx - ihalf), y, Round(cx + ihalf), y + 1, fs.fill.colour.Opaque());
        }
        return;
    }
    const Clip clip = CurrentClip();
    const int bx0 = std::max(x0, clip.x0), by0 = std::max(y0, clip.y0);
    const int bx1 = std::min(x1, clip.x1), by1 = std::min(y1, clip.y1);
    if (bx1 <= bx0 || by1 <= by0)
        return;
    const int bw = bx1 - bx0, bh = by1 - by0;
    std::vector<uint32_t> buf(size_t(bw) * bh);
    ReadPixels(buf.data(), bx0, by0, bw, bh);
    const XYPOSITION irx = std::max<XYPOSITION>(rx - sw, 0), iry = std::max<XYPOSITION>(ry - sw, 0);
    constexpr int S = 4;    // 4x4 supersampling
    auto mix = [](uint32_t p, ColourRGBA c, unsigned a) {
        const unsigned ia = 255 - a;
        const unsigned pr = (p >> 16) & 0xFF, pg = (p >> 8) & 0xFF, pb = p & 0xFF;
        return 0xFF000000u | (((pr * ia + c.GetRed() * a) / 255) << 16) |
            (((pg * ia + c.GetGreen() * a) / 255) << 8) | ((pb * ia + c.GetBlue() * a) / 255);
    };
    for (int y = 0; y < bh; y++) {
        for (int x = 0; x < bw; x++) {
            int inner = 0, outer = 0;
            for (int sy = 0; sy < S; sy++) {
                for (int sx = 0; sx < S; sx++) {
                    const XYPOSITION px = bx0 + x + (sx + 0.5) / S - cx;
                    const XYPOSITION py = by0 + y + (sy + 0.5) / S - cy;
                    if ((px * px) / (rx * rx) + (py * py) / (ry * ry) <= 1) {
                        outer++;
                        if (irx > 0 && iry > 0 && (px * px) / (irx * irx) + (py * py) / (iry * iry) <= 1)
                            inner++;
                    }
                }
            }
            if (!outer)
                continue;
            uint32_t &p = buf[size_t(y) * bw + x];
            if (inner)
                p = mix(p, fs.fill.colour, fs.fill.colour.GetAlpha() * inner / (S * S));
            if (outer > inner)
                p = mix(p, fs.stroke.colour, fs.stroke.colour.GetAlpha() * (outer - inner) / (S * S));
        }
    }
    WritePixels(buf.data(), bx0, by0, bw, bh);
}

std::unique_ptr<Surface> Surface::Allocate(Technology) {
    return std::make_unique<SurfaceOS4>();
}

// --- windows ---

namespace {
OS4Window *Win(WindowID wid) noexcept { return static_cast<OS4Window *>(wid); }
}

Window::~Window() noexcept {}

void Window::Destroy() noexcept {
    if (wid)
        Win(wid)->DestroyWindow();
    wid = nullptr;
}

PRectangle Window::GetPosition() const {
    return wid ? Win(wid)->ScreenRect() : PRectangle();
}

void Window::SetPosition(PRectangle rc) {
    if (wid)
        Win(wid)->SetScreenRect(rc);
}

void Window::SetPositionRelative(PRectangle rc, const Window *relativeTo) {
    if (!wid)
        return;
    const PRectangle origin = relativeTo ? relativeTo->GetPosition() : PRectangle();
    rc.Move(origin.left, origin.top);
    // Keep popups on the screen.
    if (const struct ::Screen *scr = Win(wid)->GetScreen()) {
        if (rc.right > scr->Width)
            rc.Move(scr->Width - rc.right, 0);
        if (rc.bottom > scr->Height)
            rc.Move(0, scr->Height - rc.bottom);
        if (rc.left < 0)
            rc.Move(-rc.left, 0);
        if (rc.top < 0)
            rc.Move(0, -rc.top);
    }
    Win(wid)->SetScreenRect(rc);
}

PRectangle Window::GetClientPosition() const {
    const PRectangle rc = GetPosition();
    return PRectangle(0, 0, rc.Width(), rc.Height());
}

void Window::Show(bool show) {
    if (wid)
        Win(wid)->Show(show);
}

void Window::InvalidateAll() {
    if (wid)
        Win(wid)->Invalidate(GetClientPosition());
}

void Window::InvalidateRectangle(PRectangle rc) {
    if (wid)
        Win(wid)->Invalidate(rc);
}

void Window::SetCursor(Cursor curs) {
    if (wid && curs != cursorLast) {
        Win(wid)->SetPointer(curs);
        cursorLast = curs;
    }
}

PRectangle Window::GetMonitorRect(Point) {
    if (!wid)
        return PRectangle();
    const PRectangle rc = GetPosition();
    const struct ::Screen *scr = Win(wid)->GetScreen();
    if (!scr)
        return PRectangle();
    return PRectangle(-rc.left, -rc.top, scr->Width - rc.left, scr->Height - rc.top);
}

// --- popup windows ---

// A borderless ToolBox window (never activated, so the editor keeps the keyboard) that paints
// through a callback into an off-screen buffer. Clicks arrive through the editor's window
// (OS4PopupClick); OS4PollPopups() handles refreshes and repaints.
class PopupWindow : public OS4Window {
public:
    using PaintFn = std::function<void(Surface &, PRectangle)>;
    using ClickFn = std::function<void(Point, unsigned int)>;

    PopupWindow(struct ::Screen *screen_, PaintFn paint_, ClickFn click_)
        : screen(screen_), paint(std::move(paint_)), click(std::move(click_)) {
        All().push_back(this);
    }
    ~PopupWindow() override {
        Close();
        auto &all = All();
        all.erase(std::remove(all.begin(), all.end(), this), all.end());
    }

    PRectangle ScreenRect() const override { return rc; }
    void SetScreenRect(PRectangle r) override {
        r = PRectangle(std::round(r.left), std::round(r.top), std::round(r.right), std::round(r.bottom));
        if (r == rc)
            return;
        rc = r;
        if (win) {
            Close();
            if (visible)
                Open();
        }
    }
    void Show(bool show) override {
        visible = show;
        if (show && !win)
            Open();
        else if (!show)
            Close();
    }
    void Invalidate(PRectangle) override { dirty = true; }
    struct ::Screen *GetScreen() const override { return screen; }
    void DestroyWindow() override { delete this; }

    bool Contains(int screenX, int screenY) const {
        return win && rc.Contains(Point(screenX, screenY));
    }
    bool ClickAt(int screenX, int screenY, unsigned int when) {
        if (!click || !Contains(screenX, screenY))
            return false;
        click(Point(screenX - rc.left, screenY - rc.top), when);
        return true;
    }

    static std::vector<PopupWindow *> &All() {
        static std::vector<PopupWindow *> all;
        return all;
    }

    void Poll() {
        if (!win)
            return;
        struct IntuiMessage *im;
        while ((im = reinterpret_cast<struct IntuiMessage *>(IExec->GetMsg(win->UserPort)))) {
            if (im->Class == IDCMP_REFRESHWINDOW) {
                IIntuition->BeginRefresh(win);
                IIntuition->EndRefresh(win, TRUE);
                dirty = true;
            }
            IExec->ReplyMsg(&im->ExecMessage);
        }
        if (dirty)
            Repaint();
    }

private:
    struct ::Screen *screen;
    struct ::Window *win = nullptr;
    PaintFn paint;
    ClickFn click;
    PRectangle rc;
    bool visible = false;
    bool dirty = false;
    OffscreenBuffer buffer;

    void Open() {
        const int w = std::max(1, Round(rc.Width())), h = std::max(1, Round(rc.Height()));
        win = IIntuition->OpenWindowTags(nullptr,
            WA_CustomScreen, screen,
            WA_Left, Round(rc.left), WA_Top, Round(rc.top),
            WA_Width, w, WA_Height, h,
            WA_Borderless, TRUE,
            WA_ToolBox, TRUE,
            WA_Activate, FALSE,
            WA_StayTop, TRUE,
            WA_SmartRefresh, TRUE,
            WA_RMBTrap, TRUE,
            WA_IDCMP, IDCMP_REFRESHWINDOW,
            TAG_END);
        if (win) {
            buffer.Allocate(w, h, screen->RastPort.BitMap);
            dirty = true;
        }
    }
    void Close() {
        if (win) {
            IIntuition->CloseWindow(win);
            win = nullptr;
        }
        buffer.Free();
    }
    void Repaint() {
        dirty = false;
        if (!win || !buffer.RP() || !paint)
            return;
        SurfaceOS4 surface;
        surface.Init(buffer.RP(), this);
        paint(surface, PRectangle(0, 0, buffer.Width(), buffer.Height()));
        surface.Release();
        IGraphics->BltBitMapRastPort(buffer.BM(), 0, 0, win->RPort, 0, 0, buffer.Width(), buffer.Height(), 0xC0);
    }
};

void OS4PollPopups() {
    for (PopupWindow *p : PopupWindow::All())
        p->Poll();
}

bool OS4PopupAt(int screenX, int screenY) {
    for (const PopupWindow *p : PopupWindow::All())
        if (p->Contains(screenX, screenY))
            return true;
    return false;
}

bool OS4PopupClick(int screenX, int screenY, unsigned int milliseconds) {
    // Copy: the click may close (delete) popups.
    std::vector<PopupWindow *> all = PopupWindow::All();
    for (PopupWindow *p : all)
        if (p->ClickAt(screenX, screenY, milliseconds))
            return true;
    return false;
}

// Calltips: the view creates one through this (ScintillaOS4::CreateCallTipWindow).
OS4Window *OS4CreatePopup(struct ::Screen *screen,
                          std::function<void(Surface &, PRectangle)> paint,
                          std::function<void(Point, unsigned int)> click) {
    return new PopupWindow(screen, std::move(paint), std::move(click));
}

// --- list box (autocomplete) ---

class ListBoxOS4 : public ListBox {
    const Font *font = nullptr;
    int lineHeight = 10;
    int aveCharWidth = 8;
    int visibleRows = 9;
    int selected = -1;
    int top = 0;
    std::vector<std::string> items;
    IListBoxDelegate *delegate = nullptr;
    ListOptions options;
    unsigned int lastClickTime = 0;
    int lastClickRow = -1;
    static constexpr int border = 1;
    static constexpr int inset = 3;

    void Paint(Surface &s, PRectangle rc) {
        const ColourRGBA fore = options.fore.value_or(ColourRGBA(0, 0, 0));
        const ColourRGBA back = options.back.value_or(ColourRGBA(0xFF, 0xFF, 0xFF));
        const ColourRGBA foreSel = options.foreSelected.value_or(ColourRGBA(0xFF, 0xFF, 0xFF));
        const ColourRGBA backSel = options.backSelected.value_or(ColourRGBA(0x33, 0x66, 0xCC));
        s.FillRectangle(rc, back);
        s.RectangleFrame(rc, Stroke(ColourRGBA(0x80, 0x80, 0x80)));
        const XYPOSITION ascent = s.Ascent(font);
        const int rows = std::min<int>(visibleRows, static_cast<int>(items.size()) - top);
        for (int i = 0; i < rows; i++) {
            const int item = top + i;
            const PRectangle rcRow(border, border + i * lineHeight, rc.right - border, border + (i + 1) * lineHeight);
            const bool sel = item == selected;
            if (sel)
                s.FillRectangle(rcRow, backSel);
            PRectangle rcText = rcRow;
            rcText.left += inset;
            s.SetClip(rcRow);
            s.DrawTextTransparent(rcText, font, rcRow.top + ascent, items[item], sel ? foreSel : fore);
            s.PopClip();
        }
    }
    void Click(Point pt, unsigned int when) {
        const int row = top + static_cast<int>(pt.y - border) / std::max(1, lineHeight);
        if (row < 0 || row >= Length())
            return;
        const bool dbl = row == lastClickRow && when - lastClickTime < Platform::DoubleClickTime();
        lastClickRow = row;
        lastClickTime = when;
        Select(row);
        if (dbl && delegate) {
            ListBoxEvent event(ListBoxEvent::EventType::doubleClick);
            delegate->ListNotify(&event);
        }
    }

public:
    ListBoxOS4() noexcept = default;
    ~ListBoxOS4() noexcept override { Destroy(); }

    void SetFont(const Font *font_) override { font = font_; }
    void Create(Window &parent, int, Point, int lineHeight_, bool, Technology) override {
        Destroy();
        lineHeight = std::max(1, lineHeight_);
        struct ::Screen *scr = parent.GetID() ? Win(parent.GetID())->GetScreen() : currentScreen;
        wid = new PopupWindow(scr,
            [this](Surface &s, PRectangle rc) { Paint(s, rc); },
            [this](Point pt, unsigned int when) { Click(pt, when); });
    }
    void SetAverageCharWidth(int width) override { aveCharWidth = std::max(1, width); }
    void SetVisibleRows(int rows) override { visibleRows = std::max(1, rows); }
    int GetVisibleRows() const override { return visibleRows; }
    PRectangle GetDesiredRect() override {
        SurfaceOS4 measure;
        measure.Init(nullptr);
        int width = 12 * aveCharWidth;
        for (const std::string &s : items)
            width = std::max(width, static_cast<int>(measure.WidthText(font, s)));
        const int rows = std::max(1, std::min<int>(visibleRows, static_cast<int>(items.size())));
        return PRectangle(0, 0, width + 2 * (border + inset) + aveCharWidth, rows * lineHeight + 2 * border);
    }
    int CaretFromEdge() override { return border + inset; }
    void Clear() noexcept override {
        items.clear();
        selected = -1;
        top = 0;
    }
    void Append(char *s, int) override { items.emplace_back(s ? s : ""); }
    int Length() override { return static_cast<int>(items.size()); }
    void Select(int n) override {
        if (n < -1 || n >= Length())
            return;
        selected = n;
        if (n >= 0) {
            if (n < top)
                top = n;
            else if (n >= top + visibleRows)
                top = n - visibleRows + 1;
        }
        InvalidateAll();
        if (delegate) {
            ListBoxEvent event(ListBoxEvent::EventType::selectionChange);
            delegate->ListNotify(&event);
        }
    }
    int GetSelection() override { return selected; }
    int Find(const char *prefix) override {
        const size_t len = strlen(prefix);
        for (size_t i = 0; i < items.size(); i++)
            if (items[i].compare(0, len, prefix) == 0)
                return static_cast<int>(i);
        return -1;
    }
    std::string GetValue(int n) override {
        return (n >= 0 && n < Length()) ? items[n] : std::string();
    }
    void RegisterImage(int, const char *) override {}
    void RegisterRGBAImage(int, int, int, const unsigned char *) override {}
    void ClearRegisteredImages() override {}
    void SetDelegate(IListBoxDelegate *lbDelegate) override { delegate = lbDelegate; }
    void SetList(const char *list, char separator, char typesep) override {
        Clear();
        std::string item;
        for (const char *p = list; ; p++) {
            if (*p == separator || *p == '\0') {
                const size_t t = item.find(typesep);
                if (t != std::string::npos)
                    item.resize(t);
                items.push_back(item);
                item.clear();
                if (*p == '\0')
                    break;
            } else {
                item.push_back(*p);
            }
        }
    }
    void SetOptions(ListOptions options_) override { options = options_; }
};

ListBox::ListBox() noexcept {}
ListBox::~ListBox() noexcept {}

std::unique_ptr<ListBox> ListBox::Allocate() {
    return std::make_unique<ListBoxOS4>();
}

// --- menus: context menus are reported to the host (SciViewHost::sciContextMenu) ---

Menu::Menu() noexcept : mid(nullptr) {}
void Menu::CreatePopUp() {}
void Menu::Destroy() noexcept {}
void Menu::Show(Point, const Window &) {}

// --- platform ---

ColourRGBA Platform::Chrome() {
    return ColourRGBA(0xE0, 0xE0, 0xE0);
}

ColourRGBA Platform::ChromeHighlight() {
    return ColourRGBA(0xFF, 0xFF, 0xFF);
}

const char *Platform::DefaultFont() {
    return "DejaVu Sans Mono";
}

int Platform::DefaultFontSize() {
    return 13;
}

unsigned int Platform::DoubleClickTime() {
    // Intuition's double-click interval from the user's input preferences.
    struct Preferences prefs;
    if (IIntuition->GetPrefs(&prefs, sizeof(prefs))) {
        const uint32 *tv = reinterpret_cast<const uint32 *>(&prefs.DoubleClick);
        const unsigned int ms = tv[0] * 1000 + tv[1] / 1000;
        if (ms >= 100 && ms <= 5000)
            return ms;
    }
    return 500;
}

void Platform::DebugDisplay(const char *s) noexcept {
    IExec->DebugPrintF("%s", s);
}

void Platform::DebugPrintf(const char *format, ...) noexcept {
    char buffer[2000];
    va_list ap;
    va_start(ap, format);
    vsnprintf(buffer, sizeof(buffer), format, ap);
    va_end(ap);
    IExec->DebugPrintF("%s", buffer);
}

namespace {
bool assertionPopUps = false;
}

bool Platform::ShowAssertionPopUps(bool assertionPopUps_) noexcept {
    const bool old = assertionPopUps;
    assertionPopUps = assertionPopUps_;
    return old;
}

void Platform::Assert(const char *c, const char *file, int line) noexcept {
    // Logged, not fatal: an editor assertion must not take the IDE down.
    IExec->DebugPrintF("Scintilla assertion failed: %s, %s:%ld\n", c, file, (long)line);
}

}
