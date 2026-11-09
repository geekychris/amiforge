// --- markdown --- View > Markdown Preview (Amiga-M): a window beside the editor that shows the
// current .md file rendered (Markdown.h parses and lays it out). It follows the editor as you
// type (300 ms after the last change), draws with graphics.library into a space.gadget with
// scrollers, loads pictures through picture.datatype (scaled to fit, cached per file), and
// opens links: a relative file in the editor, an #anchor here, a web address with OpenURL.
#include "App.h"
#include "Markdown.h"
#include "Scintilla.h"

#include <map>
#include <string.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <proto/diskfont.h>
#include <proto/datatypes.h>
#include <proto/utility.h>
#include <proto/intuition.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/space.h>
#include <proto/scroller.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/space.h>
#include <gadgets/scroller.h>
#include <datatypes/pictureclass.h>
#include <graphics/gfxbase.h>
#include <intuition/gadgetclass.h>
#include <intuition/icclass.h>
#include <diskfont/diskfonttag.h>

namespace af {

enum { MV_SPACE = 1, MV_VSCROLL, MV_HSCROLL };

class MdPreview {
public:
    explicit MdPreview(App &a) : app(a) {}
    ~MdPreview() { close(); }

    bool open();
    void close();
    uint32 signal();
    void handle();
    void show(const std::string &path, const std::string &text);   // a document's text
    void relayout();
    void redraw();
    void scrollTo(int x, int y);
    void idcmp(struct IntuiMessage *im);
    std::string links();          // MCP: the visible links with their screen boxes
    std::string state();
    bool clickLink(int index);    // as a click on it

    std::string path;
    md::Doc doc;
    bool wantsClose = false;

private:
    struct Metrics : md::Metrics {
        MdPreview *v;
        int textWidth(int font, const std::string &t) override;
        int lineHeight(int font) override;
        int baseline(int font) override;
        bool imageSize(const std::string &src, int &w, int &h) override;
    };
    struct Pic {
        Object *dt = nullptr;
        int w = 0, h = 0;           // natural size
        int sw = 0, sh = 0;         // the size it was scaled to
        struct BitMap *bm = nullptr;
        bool failed = false;
    };

    App &app;
    Object *wo = nullptr;
    struct Window *w = nullptr;
    Object *space = nullptr, *vsc = nullptr, *hsc = nullptr;
    struct Hook hook = {};
    md::Layout lay;
    int layWidth = -1, scrollX = 0, scrollY = 0;
    struct TextFont *fonts[md::FCount] = {};
    bool soft[md::FCount] = {};        // the font has no real bold: soft style
    std::vector<struct TextFont *> opened;
    struct RastPort mrp;               // for measuring
    LONG pens[8];
    enum { PPaper, PText, PLink, PMuted, PHeading, PCode, PHead, PN };
    std::map<std::string, Pic> pics;
    long long loadedPixels = 0;
    Metrics metrics;
    std::string title;

    void openFonts();
    void closeFonts();
    void setFont(struct RastPort *rp, int f);
    Pic *pic(const std::string &src);
    void freePics();
    bool box(struct IBox &b);
    void updateScrollers();
    std::string resolve(const std::string &href) { return md::resolvePath(path, href); }
    void follow(int link);
};

static uint32 mdHookFunc(struct Hook *h, APTR, struct IntuiMessage *im)
{
    ((MdPreview *)h->h_Data)->idcmp(im);
    return 0;
}

// --- fonts ------------------------------------------------------------------------------------

void MdPreview::openFonts()
{
    struct TextAttr *sa = w->WScreen->Font;
    int size = sa ? sa->ta_YSize : 13;
    std::string name = sa && sa->ta_Name ? (const char *)sa->ta_Name : "topaz.font";
    auto openAt = [&](const std::string &n, int ysize, int style) -> struct TextFont * {
        struct TextAttr ta = { (STRPTR)n.c_str(), (UWORD)ysize, (UBYTE)style, 0 };
        struct TextFont *f = IDiskfont->OpenDiskFont(&ta);
        if (f) opened.push_back(f);
        return f;
    };
    struct TextFont *body = openAt(name, size, FS_NORMAL);
    if (!body) body = openAt("topaz.font", 8, FS_NORMAL);
    auto styled = [&](int ysize, int style, int slot) {
        struct TextFont *f = openAt(name, ysize, style);
        if (!f) f = body;
        fonts[slot] = f;
        // a bitmap font has no bold of its own: soft bold (and soft italic) when drawing
        soft[slot] = f && style && (f->tf_Style & style) != style;
    };
    fonts[md::FBody] = body;
    styled(size, FSF_BOLD, md::FBold);
    styled(size, FSF_ITALIC, md::FItalic);
    styled(size, FSF_BOLD | FSF_ITALIC, md::FBoldItalic);
    static const int pct[6] = { 200, 160, 130, 115, 100, 90 };
    for (int i = 0; i < 6; i++) styled(size * pct[i] / 100, FSF_BOLD, md::FH1 + i);
    struct TextFont *mono = openAt("DejaVu Sans Mono.font", size, FS_NORMAL);
    if (!mono) mono = ((struct GfxBase *)GfxBase)->DefaultFont;   // the system's fixed-width font
    fonts[md::FMono] = mono ? mono : body;
}

void MdPreview::closeFonts()
{
    for (auto *f : opened) IGraphics->CloseFont(f);
    opened.clear();
    memset(fonts, 0, sizeof fonts);
}

void MdPreview::setFont(struct RastPort *rp, int f)
{
    IGraphics->SetFont(rp, fonts[f]);
    uint32 style = 0;
    if (soft[f]) {
        if (f == md::FBold || f == md::FBoldItalic || f >= md::FH1) style |= FSF_BOLD;
        if (f == md::FItalic || f == md::FBoldItalic) style |= FSF_ITALIC;
    }
    IGraphics->SetSoftStyle(rp, style, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
}

int MdPreview::Metrics::textWidth(int font, const std::string &t)
{
    if (t.empty() || !v->fonts[font]) return 0;
    v->setFont(&v->mrp, font);
    int n = (int)IGraphics->TextLength(&v->mrp, t.c_str(), t.size());
    if (v->soft[font] && (font == md::FBold || font == md::FBoldItalic || font >= md::FH1)) n += (int)t.size();
    return n;
}

int MdPreview::Metrics::lineHeight(int font)
{
    struct TextFont *f = v->fonts[font];
    return f ? f->tf_YSize + 2 : 10;
}

int MdPreview::Metrics::baseline(int font)
{
    struct TextFont *f = v->fonts[font];
    return f ? f->tf_Baseline + 1 : 8;
}

bool MdPreview::Metrics::imageSize(const std::string &src, int &w, int &h)
{
    Pic *p = v->pic(src);
    if (!p || p->failed) return false;
    w = p->w;
    h = p->h;
    return true;
}

// --- pictures ---------------------------------------------------------------------------------

MdPreview::Pic *MdPreview::pic(const std::string &src)
{
    if (md::linkKind(src) != md::LinkKind::File) return nullptr;   // no downloads
    std::string file = resolve(src);
    auto it = pics.find(file);
    if (it != pics.end()) return &it->second;
    // memory stays bounded: past 96 pictures or 24 million pixels (datatypes keep the decoded
    // original as well as the scaled copy) the rest show their alt text
    if (pics.size() >= 96 || loadedPixels > 24 * 1000 * 1000) return nullptr;
    Pic &p = pics[file];
    // no "insert volume" requester for a picture on a volume that isn't there
    APTR oldWin = IDOS->SetProcWindow((APTR)-1);
    p.dt = IDataTypes->NewDTObject(file.c_str(), DTA_GroupID, GID_PICTURE, PDTA_Remap, TRUE,
                                   PDTA_Screen, (uint32)w->WScreen, PDTA_DestMode, PMODE_V43,
                                   PDTA_UseFriendBitMap, TRUE, OBP_Precision, PRECISION_IMAGE, TAG_END);
    IDOS->SetProcWindow(oldWin);
    struct BitMapHeader *bmh = nullptr;
    if (p.dt) IDataTypes->GetDTAttrs(p.dt, PDTA_BitMapHeader, (uint32)&bmh, TAG_END);
    if (!p.dt || !bmh || !bmh->bmh_Width || !bmh->bmh_Height) {
        if (p.dt) IDataTypes->DisposeDTObject(p.dt);
        p.dt = nullptr;
        p.failed = true;
        return &p;
    }
    p.w = bmh->bmh_Width;
    p.h = bmh->bmh_Height;
    loadedPixels += (long long)p.w * p.h;
    return &p;
}

void MdPreview::freePics()
{
    for (auto &kv : pics)
        if (kv.second.dt) IDataTypes->DisposeDTObject(kv.second.dt);
    pics.clear();
    loadedPixels = 0;
}

// --- the window -------------------------------------------------------------------------------

bool MdPreview::open()
{
    if (wo) return true;
    Class *layout = ILayout->LAYOUT_GetClass();
    hook.h_Entry = (HOOKFUNC)mdHookFunc;
    hook.h_Data = this;
    struct Screen *scr = app.win->WScreen;
    int ww = std::min(600, scr->Width / 2), wh = app.win->Height;
    wo = IIntuition->NewObject(IWindow->WINDOW_GetClass(), nullptr,
        WA_Title, (uint32)"Markdown Preview", WA_DragBar, TRUE, WA_CloseGadget, TRUE, WA_DepthGadget, TRUE,
        WA_SizeGadget, TRUE, WA_Activate, FALSE, WA_PubScreen, (uint32)scr, WA_SmartRefresh, TRUE,
        WA_Left, std::max(0, scr->Width - ww), WA_Top, app.win->TopEdge, WA_Width, ww, WA_Height, wh,
        WA_IDCMP, IDCMP_GADGETUP | IDCMP_CLOSEWINDOW | IDCMP_NEWSIZE | IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS |
                  IDCMP_EXTENDEDMOUSE | IDCMP_IDCMPUPDATE | IDCMP_REFRESHWINDOW,
        WINDOW_IDCMPHook, (uint32)&hook,
        WINDOW_IDCMPHookBits, IDCMP_IDCMPUPDATE | IDCMP_EXTENDEDMOUSE | IDCMP_MOUSEBUTTONS | IDCMP_RAWKEY,
        WINDOW_Layout, IIntuition->NewObject(layout, nullptr, LAYOUT_Orientation, LAYOUT_ORIENT_VERT, LAYOUT_SpaceInner, FALSE,
            LAYOUT_AddChild, IIntuition->NewObject(layout, nullptr, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ, LAYOUT_SpaceInner, FALSE,
                LAYOUT_AddChild, space = IIntuition->NewObject(ISpace->SPACE_GetClass(), nullptr, GA_ID, MV_SPACE,
                    SPACE_MinWidth, 120, SPACE_MinHeight, 80, TAG_DONE),
                LAYOUT_AddChild, vsc = IIntuition->NewObject(IScroller->SCROLLER_GetClass(), nullptr, GA_ID, MV_VSCROLL,
                    GA_RelVerify, TRUE, SCROLLER_Orientation, SORIENT_VERT, SCROLLER_Arrows, TRUE,
                    ICA_TARGET, ICTARGET_IDCMP, TAG_DONE),
                CHILD_WeightedWidth, 0,
                TAG_DONE),
            LAYOUT_AddChild, hsc = IIntuition->NewObject(IScroller->SCROLLER_GetClass(), nullptr, GA_ID, MV_HSCROLL,
                GA_RelVerify, TRUE, SCROLLER_Orientation, SORIENT_HORIZ, SCROLLER_Arrows, TRUE,
                ICA_TARGET, ICTARGET_IDCMP, TAG_DONE),
            CHILD_WeightedHeight, 0,
            TAG_DONE),
        TAG_DONE);
    if (!wo) return false;
    w = (struct Window *)IIntuition->IDoMethod(wo, WM_OPEN);
    if (!w) {
        IIntuition->DisposeObject(wo);
        wo = nullptr;
        return false;
    }
    IGraphics->InitRastPort(&mrp);
    openFonts();
    metrics.v = this;
    struct ColorMap *cm = w->WScreen->ViewPort.ColorMap;
    static const uint32 rgb[PN] = { 0xFFFFFF, 0x202020, 0x1A55C0, 0x707070, 0x102860, 0xEEEEEE, 0xDDE4EE };
    for (int i = 0; i < PN; i++)
        pens[i] = IGraphics->ObtainBestPen(cm, (rgb[i] >> 16 & 0xFF) * 0x01010101u, (rgb[i] >> 8 & 0xFF) * 0x01010101u,
                                           (rgb[i] & 0xFF) * 0x01010101u, OBP_Precision, PRECISION_GUI, TAG_END);
    return true;
}

void MdPreview::close()
{
    if (!wo) return;
    freePics();
    if (w) {
        struct ColorMap *cm = w->WScreen->ViewPort.ColorMap;
        for (int i = 0; i < PN; i++)
            if (pens[i] >= 0) IGraphics->ReleasePen(cm, pens[i]);
    }
    closeFonts();
    IIntuition->DisposeObject(wo);
    wo = nullptr;
    w = nullptr;
    space = vsc = hsc = nullptr;
}

uint32 MdPreview::signal()
{
    uint32 s = 0;
    if (wo) IIntuition->GetAttr(WINDOW_SigMask, wo, &s);
    return s;
}

bool MdPreview::box(struct IBox &b)
{
    struct IBox *ib = nullptr;
    if (!space || !IIntuition->GetAttr(SPACE_AreaBox, space, (uint32 *)&ib) || !ib) return false;
    b = *ib;
    return b.Width > 0 && b.Height > 0;
}

void MdPreview::show(const std::string &p, const std::string &text)
{
    bool another = p != path;
    path = p;
    md::parse(text, doc);
    if (another) {
        freePics();
        scrollX = scrollY = 0;
        static std::string t;
        size_t slash = path.find_last_of("/:");
        title = "Markdown Preview - " + (slash == std::string::npos ? path : path.substr(slash + 1));
        if (wo) IIntuition->SetAttrs(wo, WA_Title, (uint32)title.c_str(), TAG_DONE);
    }
    layWidth = -1;
    relayout();
}

void MdPreview::relayout()
{
    struct IBox b;
    if (!w || !box(b)) return;
    lay = md::layout(doc, b.Width, metrics);
    layWidth = b.Width;
    scrollTo(scrollX, scrollY);   // clamps, sets the scrollers, redraws
}

void MdPreview::updateScrollers()
{
    struct IBox b;
    if (!box(b)) return;
    IIntuition->SetGadgetAttrs((struct Gadget *)vsc, w, nullptr, SCROLLER_Total, std::max(lay.height, 1),
                               SCROLLER_Visible, b.Height, SCROLLER_Top, scrollY, TAG_DONE);
    IIntuition->SetGadgetAttrs((struct Gadget *)hsc, w, nullptr, SCROLLER_Total, std::max(lay.width, 1),
                               SCROLLER_Visible, b.Width, SCROLLER_Top, scrollX, TAG_DONE);
}

void MdPreview::scrollTo(int x, int y)
{
    struct IBox b;
    if (!box(b)) return;
    scrollX = std::max(0, std::min(x, lay.width - b.Width));
    scrollY = std::max(0, std::min(y, lay.height - b.Height));
    updateScrollers();
    redraw();
}

void MdPreview::redraw()
{
    struct IBox b;
    if (!w || !box(b)) return;
    if (layWidth != b.Width) { relayout(); return; }
    struct RastPort *rp = w->RPort;
    struct Region *r = IGraphics->NewRegion();
    struct Region *old = nullptr;
    if (r) {
        struct Rectangle rc = { b.Left, b.Top, (WORD)(b.Left + b.Width - 1), (WORD)(b.Top + b.Height - 1) };
        IGraphics->OrRectRegion(r, &rc);
        old = ILayers->InstallClipRegion(w->WLayer, r);
    }
    IGraphics->SetAPen(rp, pens[PPaper]);
    IGraphics->RectFill(rp, b.Left, b.Top, b.Left + b.Width - 1, b.Top + b.Height - 1);
    IGraphics->SetDrMd(rp, JAM1);
    int ox = b.Left - scrollX, oy = b.Top - scrollY;
    for (const md::Item &it : lay.items) {
        if (it.y + it.h < scrollY || it.y > scrollY + b.Height) continue;   // not in view
        if (it.x > scrollX + b.Width || it.x + it.w < scrollX) continue;
        int x = ox + it.x, y = oy + it.y;
        switch (it.kind) {
        case md::ItemKind::CodeBg:
        case md::ItemKind::TableHead:
            IGraphics->SetAPen(rp, pens[it.kind == md::ItemKind::CodeBg ? PCode : PHead]);
            if (it.w > 0 && it.h > 0) IGraphics->RectFill(rp, x, y, x + it.w - 1, y + it.h - 1);
            break;
        case md::ItemKind::QuoteBar:
        case md::ItemKind::Rule:
            IGraphics->SetAPen(rp, pens[PMuted]);
            if (it.w > 0 && it.h > 0) IGraphics->RectFill(rp, x, y, x + it.w - 1, y + it.h - 1);
            break;
        case md::ItemKind::Text: {
            int pen = it.color == md::CLink ? PLink : it.color == md::CMuted ? PMuted : it.color == md::CHeading ? PHeading : PText;
            IGraphics->SetAPen(rp, pens[pen]);
            setFont(rp, it.font);
            int base = fonts[it.font] ? fonts[it.font]->tf_Baseline + 1 : 8;
            IGraphics->Move(rp, x, y + base);
            IGraphics->Text(rp, it.text.c_str(), it.text.size());
            if (it.link >= 0 || (it.style & md::Underline)) {
                IGraphics->Move(rp, x, y + base + 1);
                IGraphics->Draw(rp, x + it.w - 1, y + base + 1);
            }
            if (it.style & md::Strike) {
                int my = y + base - (fonts[it.font] ? fonts[it.font]->tf_Baseline / 3 : 3);
                IGraphics->Move(rp, x, my);
                IGraphics->Draw(rp, x + it.w - 1, my);
            }
            break;
        }
        case md::ItemKind::Picture: {
            Pic *p = it.image >= 0 && it.image < (int)doc.images.size() ? pic(doc.images[it.image].src) : nullptr;
            if (p && p->dt) {
                if (p->sw != it.w || p->sh != it.h || !p->bm) {
                    // picture.datatype scales it (PDTM_SCALE) when it lays it out
                    if (it.w != p->w || it.h != p->h)
                        IDataTypes->DoDTMethod(p->dt, nullptr, nullptr, PDTM_SCALE, it.w, it.h, 0);
                    IDataTypes->DoDTMethod(p->dt, nullptr, nullptr, DTM_PROCLAYOUT, 0, 1);
                    p->bm = nullptr;
                    IDataTypes->GetDTAttrs(p->dt, PDTA_DestBitMap, (uint32)&p->bm, TAG_END);
                    if (!p->bm) IDataTypes->GetDTAttrs(p->dt, PDTA_BitMap, (uint32)&p->bm, TAG_END);
                    p->sw = it.w;
                    p->sh = it.h;
                }
                if (p->bm) IGraphics->BltBitMapRastPort(p->bm, 0, 0, rp, x, y, it.w, it.h, 0xC0);
            }
            break;
        }
        case md::ItemKind::AltBox: {
            IGraphics->SetAPen(rp, pens[PMuted]);
            IGraphics->Move(rp, x, y);
            IGraphics->Draw(rp, x + it.w - 1, y);
            IGraphics->Draw(rp, x + it.w - 1, y + it.h - 1);
            IGraphics->Draw(rp, x, y + it.h - 1);
            IGraphics->Draw(rp, x, y);
            setFont(rp, md::FItalic);
            IGraphics->Move(rp, x + 4, y + 2 + (fonts[md::FItalic] ? fonts[md::FItalic]->tf_Baseline + 1 : 8));
            IGraphics->Text(rp, it.text.c_str(), it.text.size());
            break;
        }
        }
    }
    IGraphics->SetSoftStyle(rp, 0, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
    if (r) {
        ILayers->InstallClipRegion(w->WLayer, old);
        IGraphics->DisposeRegion(r);
    }
}

// --- input ------------------------------------------------------------------------------------

void MdPreview::idcmp(struct IntuiMessage *im)
{
    struct IBox b;
    if (!box(b)) return;
    int line = std::max(8, metrics.lineHeight(md::FBody));
    switch (im->Class) {
    case IDCMP_IDCMPUPDATE: {   // a scroller moved (while dragging, too)
        uint32 id = IUtility->GetTagData(GA_ID, 0, (struct TagItem *)im->IAddress);
        uint32 top = IUtility->GetTagData(SCROLLER_Top, 0, (struct TagItem *)im->IAddress);
        if (id == MV_VSCROLL && (int)top != scrollY) { scrollY = top; redraw(); }
        if (id == MV_HSCROLL && (int)top != scrollX) { scrollX = top; redraw(); }
        break;
    }
    case IDCMP_EXTENDEDMOUSE:
        if (im->Code == IMSGCODE_INTUIWHEELDATA && im->IAddress)
            scrollTo(scrollX, scrollY + 3 * line * ((struct IntuiWheelData *)im->IAddress)->WheelY);
        break;
    case IDCMP_RAWKEY:
        switch (im->Code) {
        case 0x4C: scrollTo(scrollX, scrollY - line); break;                 // up
        case 0x4D: scrollTo(scrollX, scrollY + line); break;                 // down
        case 0x48: case 0x3F: scrollTo(scrollX, scrollY - b.Height + line); break;   // page up
        case 0x49: case 0x1F: case 0x40: scrollTo(scrollX, scrollY + b.Height - line); break;   // page down, space
        case 0x70: scrollTo(0, 0); break;                                    // home
        case 0x71: scrollTo(scrollX, lay.height); break;                     // end
        }
        break;
    case IDCMP_MOUSEBUTTONS:
        if (im->Code == SELECTUP && im->MouseX >= b.Left && im->MouseX < b.Left + b.Width && im->MouseY >= b.Top &&
            im->MouseY < b.Top + b.Height) {
            int link = md::linkAt(lay, im->MouseX - b.Left + scrollX, im->MouseY - b.Top + scrollY);
            if (link >= 0) app.deferredMd.push_back(link);   // after WM_HANDLEINPUT: it may open a file
        }
        break;
    }
}

void MdPreview::handle()
{
    uint32 result;
    uint16 code;
    while (wo && (result = IIntuition->IDoMethod(wo, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
        switch (result & WMHI_CLASSMASK) {
        case WMHI_CLOSEWINDOW: wantsClose = true; break;
        case WMHI_NEWSIZE: relayout(); break;
        case WMHI_GADGETUP:
            if ((result & WMHI_GADGETMASK) == MV_VSCROLL || (result & WMHI_GADGETMASK) == MV_HSCROLL) {
                uint32 top = 0;
                IIntuition->GetAttr(SCROLLER_Top, (result & WMHI_GADGETMASK) == MV_VSCROLL ? vsc : hsc, &top);
                if ((result & WMHI_GADGETMASK) == MV_VSCROLL) scrollY = top;
                else scrollX = top;
                redraw();
            }
            break;
        }
    }
}

// a heading's GitHub-style anchor: "Getting started!" -> "getting-started"
static std::string slug(const std::string &s)
{
    std::string o;
    for (unsigned char c : s) {
        if (isalnum(c) || c >= 0xC0) o += (char)tolower(c);
        else if (c == ' ' || c == '-') o += '-';
    }
    return o;
}

void MdPreview::follow(int link)
{
    if (link < 0 || link >= (int)doc.links.size()) return;
    const std::string &href = doc.links[link];
    switch (md::linkKind(href)) {
    case md::LinkKind::File: {
        std::string file = resolve(href);
        APTR oldWin = IDOS->SetProcWindow((APTR)-1);
        bool ok = fileExists(file);
        IDOS->SetProcWindow(oldWin);
        if (!ok) { app.setStatus("No file " + file); return; }
        app.openFile(file);
        app.setStatus("Opened " + file);
        break;
    }
    case md::LinkKind::Anchor: {
        std::string want = href.substr(1);
        // the heading with that anchor, found in the items: the first text item of the heading font at its y
        std::vector<std::pair<std::string, const md::Block *>> heads;
        std::function<void(const md::Block &)> walk = [&](const md::Block &b) {
            if (b.type == md::BlockType::Heading) heads.push_back({ slug(md::plainText(b)), &b });
            for (auto &c : b.children) walk(*c);
        };
        walk(doc.root);
        for (auto &h : heads)
            if (h.first == want)
                for (auto &it : lay.items)
                    if (it.kind == md::ItemKind::Text && it.font >= md::FH1 && md::plainText(*h.second).compare(0, it.text.size(), it.text) == 0) {
                        scrollTo(scrollX, it.y - 4);
                        return;
                    }
        app.setStatus("No heading for " + href);
        break;
    }
    case md::LinkKind::Web: {
        // openurl.library opens it in the user's browser; its OpenURL command is the way in
        struct Library *ou = IExec->OpenLibrary("openurl.library", 0);
        if (!ou) { app.setStatus(href + " (no openurl.library to open it)"); return; }
        IExec->CloseLibrary(ou);
        std::string url = href;
        if (url.compare(0, 4, "www.") == 0) url = "http://" + url;
        std::string cmd = "OpenURL \"" + url + "\"";
        BPTR nil = IDOS->Open("NIL:", MODE_OLDFILE);
        int32 rc = IDOS->SystemTags(cmd.c_str(), SYS_Input, nil, SYS_Output, 0, SYS_Asynch, TRUE,
                                    NP_Name, (uint32)"AmiForge OpenURL", TAG_END);
        if (rc == -1) {
            if (nil) IDOS->Close(nil);
            app.setStatus(href + " (OpenURL didn't start)");
        } else app.setStatus("Opening " + url);
        break;
    }
    case md::LinkKind::Other:
        app.setStatus(href);
        break;
    }
}

bool MdPreview::clickLink(int index)
{
    if (index < 0 || index >= (int)doc.links.size()) return false;
    follow(index);
    return true;
}

std::string MdPreview::links()
{
    struct IBox b;
    std::string s;
    if (!w || !box(b)) return s;
    std::map<int, bool> seen;
    for (auto &it : lay.items) {
        if (it.link < 0 || seen[it.link]) continue;
        if (it.y + it.h < scrollY || it.y > scrollY + b.Height) continue;
        seen[it.link] = true;
        int sx = w->LeftEdge + b.Left + it.x - scrollX, sy = w->TopEdge + b.Top + it.y - scrollY;
        s += std::to_string(it.link) + "\t" + doc.links[it.link] + "\t" + std::to_string(sx) + "\t" + std::to_string(sy) +
             "\t" + std::to_string(it.w) + "\t" + std::to_string(it.h) + "\t" + it.text + "\n";
    }
    return s;
}

std::string MdPreview::state()
{
    int ok = 0, failed = 0;
    for (auto &kv : pics) (kv.second.failed ? failed : ok)++;
    int pictures = 0, alts = 0;
    for (auto &it : lay.items) {
        if (it.kind == md::ItemKind::Picture) pictures++;
        if (it.kind == md::ItemKind::AltBox) alts++;
    }
    struct IBox b = {};
    box(b);
    return "file=" + path + "\nblocks=" + std::to_string(doc.root.children.size()) + "\nlinks=" +
           std::to_string(doc.links.size()) + "\nimages=" + std::to_string(doc.images.size()) +
           "\npictures_drawn=" + std::to_string(pictures) + "\nalt_boxes=" + std::to_string(alts) +
           "\npictures_loaded=" + std::to_string(ok) + "\npictures_failed=" + std::to_string(failed) +
           "\nwidth=" + std::to_string(lay.width) + "\nheight=" + std::to_string(lay.height) +
           "\nview=" + std::to_string(b.Width) + "x" + std::to_string(b.Height) + "\nscroll=" + std::to_string(scrollX) +
           "," + std::to_string(scrollY) + "\ntruncated=" + (doc.truncated ? "1" : "0") + "\n";
}

// --- App --------------------------------------------------------------------------------------

static bool isMarkdown(const std::string &path)
{
    size_t dot = path.rfind('.');
    if (dot == std::string::npos) return false;
    std::string e = path.substr(dot + 1);
    for (auto &c : e) c = (char)tolower((unsigned char)c);
    return e == "md" || e == "markdown" || e == "mdown" || e == "mkd";
}

bool App::isMarkdownFile(const std::string &path) { return isMarkdown(path); }

// the text of an open document (what's typed, not what's saved), else the file's
static std::string docText(App &a, const std::string &path)
{
    for (auto &d : a.docs)
        if (d->path == path && d->sciDoc && a.editor) {
            if (d.get() == a.cur) {
                intptr_t n = a.editor->send(SCI_GETLENGTH);
                std::string s(n, '\0');
                struct Sci_TextRangeFull tr = { { 0, n }, &s[0] };
                a.editor->send(SCI_GETTEXTRANGEFULL, 0, (intptr_t)&tr);
                return s;
            }
        }
    std::string text;
    readFile(path, text);
    return text;
}

bool App::mdShow(const std::string &path)
{
    if (!mdPreview) mdPreview = new MdPreview(*this);
    if (!mdPreview->open()) {
        delete mdPreview;
        mdPreview = nullptr;
        info("The Markdown preview window couldn't open.");
        return false;
    }
    mdPreview->show(absolutePath(path), docText(*this, absolutePath(path)));
    return true;
}

void App::mdClose()
{
    delete mdPreview;
    mdPreview = nullptr;
    mdDelay = 0;
}

void App::mdToggle()
{
    if (mdPreview) { mdClose(); return; }
    if (cur && isMarkdown(cur->path)) mdShow(cur->path);
    else setStatus("The Markdown preview is for .md files: open one first");
}

uint32_t App::mdSignals() { return mdPreview ? mdPreview->signal() : 0; }

void App::mdHandle()
{
    if (!mdPreview) return;
    mdPreview->handle();
    if (mdPreview->wantsClose) { mdClose(); return; }
    std::vector<int> links;
    links.swap(deferredMd);
    for (int l : links)
        if (mdPreview) mdPreview->clickLink(l);
}

// SCN_MODIFIED: the preview follows the document it shows, 300 ms after the last change
void App::mdEdited()
{
    if (mdPreview && cur && cur->path == mdPreview->path) mdDelay = 3;   // INTUITICKS (0.1 s each)
}

void App::mdTick()
{
    if (mdDelay > 0 && --mdDelay == 0) mdRefresh();
}

void App::mdRefresh()
{
    if (mdPreview) mdPreview->show(mdPreview->path, docText(*this, mdPreview->path));
}

// a document became the current one: a Markdown file shows in the open preview
void App::mdActivated()
{
    if (mdPreview && cur && isMarkdown(cur->path) && cur->path != mdPreview->path) mdShow(cur->path);
}

// MCP/ARexx: PREVIEWMARKDOWN [FILE], MDBLOCKS [FILE], MDLINKS, MDSTATE, MDCLICK N, MDSCROLL Y
bool App::mdCommand(const std::string &what, const std::string &arg, std::string &out, std::string &err)
{
    if (what == "PREVIEW") {
        std::string p = arg.empty() ? (cur ? cur->path : "") : absolutePath(arg);
        if (p.empty()) { err = "no file"; return false; }
        if (!mdShow(p)) { err = "the preview window didn't open"; return false; }
        out = mdPreview->state();
        return true;
    }
    if (what == "BLOCKS") {
        std::string p = arg.empty() ? (mdPreview ? mdPreview->path : cur ? cur->path : "") : absolutePath(arg);
        if (p.empty()) { err = "no file"; return false; }
        md::Doc d;
        md::parse(docText(*this, p), d);
        out = md::dump(d);
        for (size_t i = 0; i < d.links.size(); i++) out += "link " + std::to_string(i) + " " + d.links[i] + "\n";
        for (size_t i = 0; i < d.images.size(); i++) out += "image " + std::to_string(i) + " " + d.images[i].src + "\n";
        return true;
    }
    if (!mdPreview) { err = "the Markdown preview isn't open"; return false; }
    if (what == "LINKS") { out = mdPreview->links(); return true; }
    if (what == "STATE") { out = mdPreview->state(); return true; }
    if (what == "CLICK") {
        if (!mdPreview->clickLink(atoi(arg.c_str()))) { err = "no link " + arg; return false; }
        out = cur ? cur->path : "";
        return true;
    }
    if (what == "SCROLL") { mdPreview->scrollTo(0, atoi(arg.c_str())); out = mdPreview->state(); return true; }
    err = "unknown Markdown command " + what;
    return false;
}

} // namespace af
