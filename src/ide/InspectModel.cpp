// The Intuition inspector's decoders and rows (see InspectModel.h). Flag values are those
// of the OS4 SDK's intuition/intuition.h; Inspector.cpp checks them against the headers.
#include "InspectModel.h"

#include <stdio.h>

namespace af {

namespace {

struct Bit { uint32_t mask; const char *name; };

std::string hex(uint32_t v)
{
    char b[16];
    snprintf(b, sizeof b, "0x%lx", (unsigned long)v);
    return b;
}

std::string join(const Bit *bits, size_t n, uint32_t v)
{
    std::string out;
    uint32_t known = 0;
    for (size_t i = 0; i < n; i++) {
        known |= bits[i].mask;
        if ((v & bits[i].mask) == bits[i].mask && bits[i].mask) {
            if (!out.empty()) out += "|";
            out += bits[i].name;
        }
    }
    if (v & ~known) {
        if (!out.empty()) out += "|";
        out += hex(v & ~known);
    }
    return out;
}

const Bit idcmpBits[] = {
    { 0x00000001, "SIZEVERIFY" }, { 0x00000002, "NEWSIZE" }, { 0x00000004, "REFRESHWINDOW" },
    { 0x00000008, "MOUSEBUTTONS" }, { 0x00000010, "MOUSEMOVE" }, { 0x00000020, "GADGETDOWN" },
    { 0x00000040, "GADGETUP" }, { 0x00000080, "REQSET" }, { 0x00000100, "MENUPICK" },
    { 0x00000200, "CLOSEWINDOW" }, { 0x00000400, "RAWKEY" }, { 0x00000800, "REQVERIFY" },
    { 0x00001000, "REQCLEAR" }, { 0x00002000, "MENUVERIFY" }, { 0x00004000, "NEWPREFS" },
    { 0x00008000, "DISKINSERTED" }, { 0x00010000, "DISKREMOVED" }, { 0x00020000, "WBENCHMESSAGE" },
    { 0x00040000, "ACTIVEWINDOW" }, { 0x00080000, "INACTIVEWINDOW" }, { 0x00100000, "DELTAMOVE" },
    { 0x00200000, "VANILLAKEY" }, { 0x00400000, "INTUITICKS" }, { 0x00800000, "IDCMPUPDATE" },
    { 0x01000000, "MENUHELP" }, { 0x02000000, "CHANGEWINDOW" }, { 0x04000000, "GADGETHELP" },
    { 0x08000000, "EXTENDEDMOUSE" }, { 0x10000000, "EXTENDEDKEYBOARD" }, { 0x80000000, "LONELYMESSAGE" },
};

// The refresh type is a 2-bit field (0 = SMART_REFRESH): handled separately.
const Bit wflgBits[] = {
    { 0x00000001, "SIZEGADGET" }, { 0x00000002, "DRAGBAR" }, { 0x00000004, "DEPTHGADGET" },
    { 0x00000008, "CLOSEGADGET" }, { 0x00000010, "SIZEBRIGHT" }, { 0x00000020, "SIZEBBOTTOM" },
    { 0x00000100, "BACKDROP" }, { 0x00000200, "REPORTMOUSE" }, { 0x00000400, "GIMMEZEROZERO" },
    { 0x00000800, "BORDERLESS" }, { 0x00001000, "ACTIVATE" }, { 0x00002000, "WINDOWACTIVE" },
    { 0x00004000, "INREQUEST" }, { 0x00008000, "MENUSTATE" }, { 0x00010000, "RMBTRAP" },
    { 0x00020000, "NOCAREREFRESH" }, { 0x00040000, "NW_EXTENDED" }, { 0x00200000, "NEWLOOKMENUS" },
    { 0x01000000, "WINDOWREFRESH" }, { 0x02000000, "WBENCHWINDOW" }, { 0x04000000, "WINDOWTICKED" },
    { 0x08000000, "VISITOR" }, { 0x10000000, "ZOOMED" }, { 0x20000000, "HASZOOM" },
    { 0x40000000, "HASICONIFY" },
};

const Bit gflgBits[] = {
    { 0x0004, "GADGIMAGE" }, { 0x0008, "RELBOTTOM" }, { 0x0010, "RELRIGHT" }, { 0x0020, "RELWIDTH" },
    { 0x0040, "RELHEIGHT" }, { 0x0080, "SELECTED" }, { 0x0100, "DISABLED" }, { 0x0200, "TABCYCLE" },
    { 0x0400, "STRINGEXTEND" }, { 0x0800, "IMAGEDISABLE" }, { 0x4000, "RELSPECIAL" }, { 0x8000, "EXTENDED" },
};

const Bit gactBits[] = {
    { 0x0001, "RELVERIFY" }, { 0x0002, "IMMEDIATE" }, { 0x0004, "ENDGADGET" }, { 0x0008, "FOLLOWMOUSE" },
    { 0x0010, "RIGHTBORDER" }, { 0x0020, "LEFTBORDER" }, { 0x0040, "TOPBORDER" }, { 0x0080, "BOTTOMBORDER" },
    { 0x0100, "TOGGLESELECT" }, { 0x0200, "STRINGCENTER" }, { 0x0400, "STRINGRIGHT" }, { 0x0800, "LONGINT" },
    { 0x1000, "ALTKEYMAP" }, { 0x2000, "BOOLEXTEND" }, { 0x4000, "ACTIVEGADGET" }, { 0x8000, "BORDERSNIFF" },
};

std::string dims(int l, int t, int w, int h)
{
    char b[64];
    snprintf(b, sizeof b, "%d,%d  %dx%d", l, t, w, h);
    return b;
}

std::string num(long v)
{
    char b[24];
    snprintf(b, sizeof b, "%ld", v);
    return b;
}

} // namespace

std::string decodeIdcmp(uint32_t v) { return join(idcmpBits, sizeof idcmpBits / sizeof *idcmpBits, v); }

std::string decodeWindowFlags(uint32_t v)
{
    static const char *const refresh[] = { "SMART_REFRESH", "SIMPLE_REFRESH", "SUPER_BITMAP", "OTHER_REFRESH" };
    std::string r = refresh[(v >> 6) & 3];
    std::string rest = join(wflgBits, sizeof wflgBits / sizeof *wflgBits, v & ~0xC0u);
    return rest.empty() ? r : r + "|" + rest;
}

std::string decodeGadgetType(uint16_t v)
{
    std::string out;
    if (v & 0x8000) {
        static const char *const sys[] = { "?", "SIZING", "WDRAGGING", "SDRAGGING", "WDEPTH", "SDEPTH",
                                           "WZOOM", "SUNUSED", "CLOSE", "ICONIFY" };
        unsigned t = (v & 0x00F0) >> 4;
        out = std::string("SYS:") + (t < sizeof sys / sizeof *sys ? sys[t] : hex(v & 0xF0).c_str());
    } else {
        static const char *const types[] = { "?", "BOOLGADGET", "GADGET0002", "PROPGADGET", "STRGADGET",
                                             "CUSTOMGADGET", "?", "?" };
        out = types[v & 7];
    }
    if (v & 0x4000) out += "|SCRGADGET";
    if (v & 0x2000) out += "|GZZGADGET";
    if (v & 0x1000) out += "|REQGADGET";
    if (v & 0x0200) out += "|TBARGADGET";
    return out;
}

std::string decodeGadgetFlags(uint16_t v)
{
    static const char *const hl[] = { "GADGHCOMP", "GADGHBOX", "GADGHIMAGE", "GADGHNONE" };
    std::string r = hl[v & 3];
    std::string rest = join(gflgBits, sizeof gflgBits / sizeof *gflgBits, v & ~0x3003u);
    unsigned label = (v >> 12) & 3;
    if (label == 1) rest += rest.empty() ? "LABELSTRING" : "|LABELSTRING";
    else if (label == 2) rest += rest.empty() ? "LABELIMAGE" : "|LABELIMAGE";
    return rest.empty() ? r : r + "|" + rest;
}

std::string decodeActivation(uint16_t v) { return join(gactBits, sizeof gactBits / sizeof *gactBits, v); }

std::string decodeTaskState(int state)
{
    static const char *const names[] = { "invalid", "added", "running", "ready", "waiting", "exception",
                                         "removed", "crashed", "suspended" };
    return state >= 0 && state < (int)(sizeof names / sizeof *names) ? names[state] : num(state);
}

std::string decodeSignals(uint32_t mask, const InspectSnapshot &snap)
{
    std::string out;
    for (int b = 0; b < 32; b++) {
        if (!(mask & (1u << b))) continue;
        std::string name;
        for (auto &w : snap.windows)
            if (w.sigBit == b) {
                if (!name.empty()) name += ", ";
                name += "IDCMP '" + w.title + "'";
            }
        if (name.empty()) {
            switch (b) {
            case 0: name = "ABORT"; break;
            case 1: name = "CHILD"; break;
            case 4: name = "SINGLE"; break;
            case 5: name = "INTUITION"; break;
            case 7: name = "NET"; break;
            case 8: name = "DOS"; break;
            case 12: name = "CTRL-C"; break;
            case 13: name = "CTRL-D"; break;
            case 14: name = "CTRL-E"; break;
            case 15: name = "CTRL-F"; break;
            default: name = "bit " + num(b); break;
            }
        }
        if (!out.empty()) out += " ";
        out += name;
    }
    return out.empty() ? "(none)" : out;
}

std::vector<InspRow> inspectRows(const InspectSnapshot &snap)
{
    std::vector<InspRow> rows;
    auto add = [&](int depth, const std::string &n, const std::string &v, InspKind k, int idx = -1, int win = -1,
                   bool kids = false) {
        InspRow r;
        r.depth = depth; r.name = n; r.value = v; r.kind = k; r.index = idx; r.window = win; r.hasChildren = kids;
        rows.push_back(r);
    };
    if (snap.task.valid) {
        add(0, "Task", snap.task.name + " (" + decodeTaskState(snap.task.state) + ")", InspKind::Info, -1, -1, true);
        add(1, "Waiting for", decodeSignals(snap.task.sigWait, snap), InspKind::Detail);
        add(1, "Received", decodeSignals(snap.task.sigRecvd, snap), InspKind::Detail);
        add(1, "Allocated", decodeSignals(snap.task.sigAlloc, snap), InspKind::Detail);
    } else {
        add(0, "Task", "(the program isn't running)", InspKind::Info);
    }
    if (snap.windows.empty() && snap.screens.empty()) {
        add(0, "Windows", "(the program has no windows open)", InspKind::Info);
        return rows;
    }
    auto addWindow = [&](int wi, int depth) {
        const WindowInfo &w = snap.windows[wi];
        add(depth, "Window '" + w.title + "'", dims(w.left, w.top, w.width, w.height) + (w.active ? "  (active)" : ""),
            InspKind::Window, wi, wi, true);
        add(depth + 1, "Address", hex(w.addr), InspKind::Detail, -1, wi);
        add(depth + 1, "IDCMP", decodeIdcmp(w.idcmp), InspKind::Detail, -1, wi);
        add(depth + 1, "Flags", decodeWindowFlags(w.flags), InspKind::Detail, -1, wi);
        add(depth + 1, "Signal bit", w.sigBit >= 0 ? num(w.sigBit) : "(no UserPort)", InspKind::Detail, -1, wi);
        add(depth + 1, "Gadgets", num((long)w.gadgets.size()), InspKind::Detail, -1, wi, !w.gadgets.empty());
        for (size_t g = 0; g < w.gadgets.size(); g++) {
            const GadgetInfo &gi = w.gadgets[g];
            std::string label = "Gadget " + num(gi.id);
            if (!gi.className.empty()) label += " (" + gi.className + ")";
            else label += " (" + decodeGadgetType(gi.type) + ")";
            if (!gi.text.empty()) label += " '" + gi.text + "'";
            add(depth + 2, label, dims(gi.left, gi.top, gi.width, gi.height), InspKind::Gadget, (int)g, wi, true);
            add(depth + 3, "Type", decodeGadgetType(gi.type), InspKind::Detail, (int)g, wi);
            add(depth + 3, "Flags", decodeGadgetFlags(gi.flags), InspKind::Detail, (int)g, wi);
            add(depth + 3, "Activation", decodeActivation(gi.activation), InspKind::Detail, (int)g, wi);
        }
    };
    std::vector<bool> shown(snap.windows.size(), false);
    for (size_t s = 0; s < snap.screens.size(); s++) {
        const ScreenInfo &sc = snap.screens[s];
        std::string title = sc.title.empty() ? sc.pubName : sc.title;
        std::string v = num(sc.width) + "x" + num(sc.height) + "x" + num(sc.depth);
        if (!sc.pubName.empty()) v += "  public '" + sc.pubName + "'";
        if (sc.newSinceStart) v += "  (opened while debugging)";
        if (sc.front) v += "  (front)";
        add(0, "Screen '" + title + "'", v, InspKind::Screen, (int)s, -1, true);
        for (size_t w = 0; w < snap.windows.size(); w++)
            if (snap.windows[w].screen == sc.addr) {
                addWindow((int)w, 1);
                shown[w] = true;
            }
    }
    for (size_t w = 0; w < snap.windows.size(); w++)
        if (!shown[w]) addWindow((int)w, 0);
    return rows;
}

} // namespace af
