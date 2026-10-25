// Debugger tools: the live Intuition inspector ("Windows" tab), the leak tracker ("Leaks")
// and the profiler ("Profile", plus end-of-line heat in the editor).
//
// The inspector reads Intuition's screen and window lists under LockIBase, copies what it
// needs and lets go at once; the program's windows are those whose UserPort signals its task.
// The leak tracker and the profiler are run by the debugger core (dbg_track_resources,
// dbg_profile); this file drains their data into LeakModel / ProfileModel while the program
// runs and turns it into the panels when it has ended.
#include "App.h"

#include <string.h>
#include <stdlib.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/listbrowser.h>
#include <intuition/intuitionbase.h>
#include <intuition/classes.h>
#include <intuition/screens.h>
#include <gadgets/listbrowser.h>
#include <devices/timer.h>

#include "Scintilla.h"
#include "InspectSnapshot.h"

namespace af {

static inline intptr_t S(SciView *v, unsigned m, uintptr_t w = 0, intptr_t l = 0) { return v ? v->send(m, w, l) : 0; }

enum { AF_STYLE_PROFILE = 250 };   // Scintilla style for the profile annotations

static void sleepMs(uint32_t ms)
{
    struct MsgPort *port = (struct MsgPort *)IExec->AllocSysObjectTags(ASOT_PORT, TAG_END);
    struct TimeRequest *tr = port ? (struct TimeRequest *)IExec->AllocSysObjectTags(ASOT_IOREQUEST,
        ASOIOR_Size, sizeof(struct TimeRequest), ASOIOR_ReplyPort, port, TAG_END) : nullptr;
    if (tr && !IExec->OpenDevice("timer.device", UNIT_MICROHZ, (struct IORequest *)tr, 0)) {
        tr->Request.io_Command = TR_ADDREQUEST;
        tr->Time.Seconds = ms / 1000;
        tr->Time.Microseconds = (ms % 1000) * 1000;
        IExec->DoIO((struct IORequest *)tr);
        IExec->CloseDevice((struct IORequest *)tr);
    }
    if (tr) IExec->FreeSysObject(ASOT_IOREQUEST, tr);
    if (port) IExec->FreeSysObject(ASOT_PORT, port);
}

// ===================================================================== session plumbing

void App::dtBeforeRun()
{
    trackRun = project.trackLeaks && !profileRun;
    leakModel.reset();
    if (profileRun) {
        profileModel.reset();
        clearProfile();
    }
    dbg_track_resources(dbg, trackRun);
    dbg_profile(dbg, profileRun, 1000);
    screensAtStart = currentScreens();   // screens that exist now aren't the program's
    inspectSnap = InspectSnapshot();
    inspectRowList.clear();
    inspectView.clear();
    if (trackRun) {
        leakTargets.clear();
        leaksView.clear();
        leaksView.detach();
        leaksView.add({"(tracking: the report appears when the program ends)", "", ""});
        leaksView.attach();
    }
}

void App::dtPoll()
{
    if (!trackRun && !profileRun) return;
    static dbg_res_event evs[512];
    static dbg_sample smp[512];
    int n;
    if (trackRun)
        while ((n = dbg_resource_events(dbg, evs, 512)) > 0) leakModel.addAll(evs, n);
    if (profileRun)
        while ((n = dbg_profile_samples(dbg, smp, 512)) > 0) profileModel.addAll(smp, n);
}

CodeLoc App::resolveCode(uint32_t a)
{
    CodeLoc c;
    if (dbg && dbg_addr_in_program(dbg, a)) {
        c.inProgram = true;
        const char *f = nullptr, *fn = nullptr;
        int l = 0;
        if (dbg_addr_to_line(dbg, a, &f, &l, &fn)) {
            if (f) {
                const char *src = dbg_resolve_source(dbg, f);
                c.file = src ? absolutePath(src) : f;
            }
            c.line = l;
            if (fn) c.function = fn;
        }
        // without a stabs range the debugger names code "symbol+0x1c": group it under the symbol
        size_t plus = c.function.rfind("+0x");
        if (plus != std::string::npos && plus > 0) c.function.erase(plus);
        if (c.function.empty()) c.function = strprintf("0x%08lx", (unsigned long)a);
    } else {
        char buf[160];
        if (dbg && dbg_addr_module(dbg, a, buf, sizeof buf)) c.function = buf;
        else c.function = "unknown code";
    }
    return c;
}

void App::dtSessionEnded()
{
    if (!dbg || (!trackRun && !profileRun)) return;
    dtPoll();
    CodeResolver resolve = [this](uint32_t a) { return resolveCode(a); };
    busyPointer(true);
    if (trackRun) {
        refreshLeaks();
        showBottomPage(BOTTOM_LEAKS);
    }
    if (profileRun) {
        profileModel.finish(resolve);
        profileNotes.clear();
        for (auto &f : profileModel.lines())
            for (auto &l : f.second) profileNotes[f.first][l.first] = profileModel.annotation(l.second);
        refreshProfile();
        applyProfileAnnotations();
        showBottomPage(BOTTOM_PROFILE);
        debugLog(strprintf("Profile: %lu samples over %.1f s, %.0f%% waiting", (unsigned long)profileModel.samples(),
                           profileModel.time() / 1000000.0,
                           profileModel.percent(profileModel.waiting())), penNote);
    }
    busyPointer(false);
    trackRun = profileRun = false;
}

void App::startProfiling()
{
    if (debugging()) {
        info("Stop debugging first: profiling starts the program on its own.");
        return;
    }
    profileNext = true;
    if (!buildBeforeRun(2)) profileNext = false;
}

void App::toggleTrackLeaks()
{
    project.trackLeaks = !project.trackLeaks;
    if (!project.path.empty()) saveProject();
    rebuildMenus();
    setStatus(project.trackLeaks ? "Leak tracking on: the next debug run reports what the program doesn't release"
                                 : "Leak tracking off");
}

// ===================================================================== inspector

void App::refreshInspector()
{
    struct Task *task = dbg && debugging() ? (struct Task *)dbg_task(dbg) : nullptr;
    takeInspectSnapshot(task, screensAtStart, inspectSnap);
    inspectRowList = inspectRows(inspectSnap);
    inspectView.clear();
    inspectView.detach();
    for (size_t i = 0; i < inspectRowList.size(); i++) {
        const InspRow &r = inspectRowList[i];
        struct Node *n = inspectView.add({r.name, r.value}, (void *)(intptr_t)(i + 1), r.depth,
                                         r.hasChildren ? (LBFLG_HASCHILDREN | LBFLG_SHOWCHILDREN) : 0);
        if (n && (r.kind == InspKind::Window || r.kind == InspKind::Screen)) inspectView.setPens(n, penNote);
    }
    inspectView.attach();
}

void App::inspectorClicked()
{
    struct Node *n = inspectView.selected();
    int i = n ? (int)(intptr_t)inspectView.userData(n) - 1 : -1;
    if (i < 0 || i >= (int)inspectRowList.size()) return;
    const InspRow &r = inspectRowList[i];
    if ((r.kind == InspKind::Window || r.kind == InspKind::Gadget) && r.window >= 0 &&
        r.window < (int)inspectSnap.windows.size())
        highlightWindow(inspectSnap.windows[r.window].addr, r.kind == InspKind::Gadget ? r.index : -1);
}

// Brings the window (and its screen) to the front and flashes a complemented frame around it
// (or around one of its gadgets) on the screen's bitmap: drawn twice per flash, so it
// leaves nothing behind. The window is looked up again first: it may have closed.
void App::highlightWindow(uint32_t addr, int gadget)
{
    struct Screen *scr = nullptr;
    struct Window *win = nullptr;
    int box[4];
    findWindowBox(addr, gadget, &scr, &win, box);
    int x0 = box[0], y0 = box[1], x1 = box[2], y1 = box[3];
    struct IntuitionBase *ib = (struct IntuitionBase *)IntuitionBase;
    if (!win) {
        setStatus("That window has closed: refresh the Windows panel");
        return;
    }
    if (ib->FirstScreen != scr) IIntuition->ScreenToFront(scr);
    IIntuition->WindowToFront(win);
    sleepMs(150);   // let Intuition bring it forward
    struct RastPort rp;
    IGraphics->InitRastPort(&rp);
    rp.BitMap = scr->RastPort.BitMap;
    IGraphics->SetDrMd(&rp, COMPLEMENT);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= scr->Width) x1 = scr->Width - 1;
    if (y1 >= scr->Height) y1 = scr->Height - 1;
    auto frame = [&]() {
        for (int k = 0; k < 3; k++) {
            int a = x0 + k, b = y0 + k, c = x1 - k, d = y1 - k;
            if (c <= a || d <= b) break;
            IGraphics->Move(&rp, a, b);
            IGraphics->Draw(&rp, c, b);
            IGraphics->Draw(&rp, c, d);
            IGraphics->Draw(&rp, a, d);
            IGraphics->Draw(&rp, a, b + 1);
        }
    };
    for (int i = 0; i < 3; i++) {
        frame();
        sleepMs(140);
        frame();
        sleepMs(110);
    }
    setStatus(gadget >= 0 ? "Gadget highlighted" : "Window brought to the front");
}

// ===================================================================== leaks

static std::string copyText(const char *s, size_t max = 80)
{
    if (!s) return "";
    size_t n = 0;
    while (n < max && s[n]) n++;
    return std::string(s, n);
}

static std::string resourceName(const Leak &l)
{
    char buf[256];
    switch (l.kind) {
    case DBG_RES_FILE:
        if (IDOS->NameFromFH((BPTR)l.addr, buf, sizeof buf)) return buf;
        break;
    case DBG_RES_LOCK:
        if (IDOS->NameFromLock((BPTR)l.addr, buf, sizeof buf)) return buf;
        break;
    case DBG_RES_LIBRARY: {
        struct Library *lib = (struct Library *)l.addr;
        return copyText(lib->lib_Node.ln_Name, 60) + strprintf(" (v%lu)", (unsigned long)l.size);
    }
    case DBG_RES_INTERFACE: {
        struct Interface *i = (struct Interface *)l.addr;
        std::string lib = i->Data.LibBase ? copyText(i->Data.LibBase->lib_Node.ln_Name, 60) : "?";
        return lib + " \"" + copyText(i->Data.Link.ln_Name, 30) + "\"";
    }
    case DBG_RES_SYSOBJ: {
        static_assert(ASOT_PORT == 6 && ASOT_MUTEX == 13, "ASOT_ values");
        static const char *const types[] = { "IOREQUEST", "HOOK", "INTERRUPT", "LIST", "DMAENTRY", "NODE", "PORT",
                                             "MESSAGE", "SEMAPHORE", "TAGLIST", "MEMPOOL", "ITEMPOOL", "RMAP",
                                             "MUTEX", "EXTMEM", "LOG" };
        return l.size < sizeof types / sizeof *types ? std::string("ASOT_") + types[l.size]
                                                     : strprintf("type %lu", (unsigned long)l.size);
    }
    default:
        break;
    }
    return "";
}

void App::refreshLeaks()
{
    CodeResolver resolve = [this](uint32_t a) { return resolveCode(a); };
    auto groups = leakModel.report(resolve);
    auto problems = leakModel.problems(resolve);
    leakTargets.clear();
    leaksView.clear();
    leaksView.detach();
    auto add = [&](const std::vector<std::string> &cols, int gen, bool kids, const NavTarget &t, int pen = -1) {
        leakTargets.push_back(t);
        struct Node *n = leaksView.add(cols, (void *)(intptr_t)leakTargets.size(), gen,
                                       kids ? (LBFLG_HASCHILDREN | LBFLG_SHOWCHILDREN) : 0);
        if (n && pen >= 0) leaksView.setPens(n, pen);
    };
    // what the program's own calls left behind, and what the OS did on its task (C library
    // and DOS housekeeping: listed last, dimmed)
    size_t held = 0, osHeld = 0;
    uint64_t bytes = 0;
    for (auto &g : groups) {
        if (g.site) { held += g.items.size(); bytes += g.bytes; }
        else osHeld += g.items.size();
    }
    std::string summary = held ? strprintf("%lu not released by the program (%s of memory)", (unsigned long)held,
                                           LeakModel::formatBytes(bytes).c_str())
                               : std::string("Everything the program acquired was released");
    add({summary, strprintf("%lu calls tracked", (unsigned long)leakModel.events()), ""}, 0, false, {},
        held ? penError : penNote);
    if (uint32_t lost = dbg_resources_lost(dbg))
        add({strprintf("%lu calls weren't recorded (buffer full): results may be off", (unsigned long)lost), "", ""}, 1,
            false, {}, penWarning);
    const char *note = dbg_resources_note(dbg);
    if (note && *note) add({note, "", ""}, 0, false, {}, penDim);
    for (auto &p : problems) {
        std::string where = p.loc.file.empty() ? p.loc.function : strprintf("%s:%d  %s", fileName(p.loc.file).c_str(),
                                                                            p.loc.line, p.loc.function.c_str());
        add({"Problem: " + p.text, "", where}, 0, false, { p.loc.file, p.loc.line }, penWarning);
    }
    for (auto &g : groups) {
        std::string where = g.loc.file.empty() ? (g.site ? g.loc.function : "(not from the program's code: " + g.loc.function + ")")
                                               : strprintf("%s:%d  %s", fileName(g.loc.file).c_str(), g.loc.line,
                                                           g.loc.function.c_str());
        if (!g.site && osHeld) {
            add({strprintf("Not from the program's own calls: %lu (usually the C library's or DOS's housekeeping)",
                           (unsigned long)osHeld), "", ""}, 0, false, {}, penDim);
            osHeld = 0;
        }
        add({LeakModel::describe(g), dbg_res_kind_name(g.kind), where}, 0, true, { g.loc.file, g.loc.line },
            g.site ? -1 : penDim);
        size_t shown = 0;
        for (auto &l : g.items) {
            if (shown++ >= 50) {
                add({strprintf("... and %lu more", (unsigned long)(g.items.size() - 50)), "", ""}, 1, false,
                    { g.loc.file, g.loc.line });
                break;
            }
            std::string details = strprintf("0x%08lx", (unsigned long)l.addr);
            if (LeakModel::isMemory(l.kind)) details += strprintf(", %lu bytes", (unsigned long)l.size);
            std::string name = resourceName(l);
            if (!name.empty()) details += "  " + name;
            add({dbg_res_function_name(l.fn), details, strprintf("call #%lu", (unsigned long)l.seq)}, 1, false,
                { g.loc.file, g.loc.line });
        }
    }
    leaksView.attach();
    debugLog(summary + (problems.empty() ? "" : strprintf(", %lu problem%s", (unsigned long)problems.size(),
                                                          problems.size() == 1 ? "" : "s")),
             held || !problems.empty() ? penWarning : penNote);
}

void App::leakClicked()
{
    struct Node *n = leaksView.selected();
    int i = n ? (int)(intptr_t)leaksView.userData(n) - 1 : -1;
    if (i >= 0 && i < (int)leakTargets.size() && !leakTargets[i].file.empty() && leakTargets[i].line > 0)
        gotoLocation(leakTargets[i].file, leakTargets[i].line);
}

// ===================================================================== profile

void App::refreshProfile()
{
    profileTargets.clear();
    profileView.clear();
    profileView.detach();
    auto pct = [&](uint64_t n) {
        return n ? strprintf("%.1f%% (%.0f ms)", profileModel.percent(n), n / 1000.0) : std::string("");
    };
    auto add = [&](const std::vector<std::string> &cols, const NavTarget &t, int pen = -1) {
        profileTargets.push_back(t);
        struct Node *n = profileView.add(cols, (void *)(intptr_t)profileTargets.size());
        if (n && pen >= 0) profileView.setPens(n, pen);
    };
    std::string head = strprintf("%lu samples over %.1f s", (unsigned long)profileModel.samples(),
                                 profileModel.time() / 1000000.0);
    if (profileModel.samples())
        head += strprintf(", %.0f%% waiting", profileModel.percent(profileModel.waiting()));
    if (uint32_t lost = dbg ? dbg_profile_lost(dbg) : 0) head += strprintf(", %lu lost", (unsigned long)lost);
    add({head, "", "", "", ""}, {}, penNote);
    int rows = 0;
    for (auto &f : profileModel.functions()) {
        if (rows++ >= 300) break;
        std::string where = f.file.empty() ? "" : strprintf("%s:%d", fileName(f.file).c_str(), f.line);
        std::string os = pct(f.inOS);
        if (f.waiting) os += strprintf(" %.0f%%w", 100.0 * (double)f.waiting / (double)(f.inOS ? f.inOS : f.self));
        add({f.name, pct(f.self), f.program ? os : "", f.program ? strprintf("%.1f%%", profileModel.percent(f.total)) : "",
             where}, { f.file, f.line }, f.program ? -1 : penDim);
    }
    profileView.attach();
}

void App::profileClicked()
{
    struct Node *n = profileView.selected();
    int i = n ? (int)(intptr_t)profileView.userData(n) - 1 : -1;
    if (i >= 0 && i < (int)profileTargets.size() && !profileTargets[i].file.empty() && profileTargets[i].line > 0)
        gotoLocation(profileTargets[i].file, profileTargets[i].line);
}

void App::applyProfileAnnotations()
{
    if (!editor || !cur) return;
    void *doc = cur->sciDoc;
    bool had = annotatedDocs.count(doc) != 0;
    if (profileNotes.empty() && !had) return;
    if (had) S(editor, SCI_EOLANNOTATIONCLEARALL);
    annotatedDocs.erase(doc);
    if (profileNotes.empty() || cur->path.empty()) return;
    S(editor, SCI_STYLESETFORE, AF_STYLE_PROFILE, 0x1010B0);   // BGR: dark red
    S(editor, SCI_STYLESETBACK, AF_STYLE_PROFILE, 0xD8F0FF);   // pale amber
    S(editor, SCI_STYLESETITALIC, AF_STYLE_PROFILE, 1);
    S(editor, SCI_EOLANNOTATIONSETVISIBLE, EOLANNOTATION_BOXED);
    for (auto &f : profileNotes) {
        if (!sameFile(f.first, cur->path)) continue;
        intptr_t lines = S(editor, SCI_GETLINECOUNT);
        for (auto &l : f.second) {
            if (l.first < 1 || l.first > lines) continue;
            S(editor, SCI_EOLANNOTATIONSETTEXT, l.first - 1, (intptr_t)l.second.c_str());
            S(editor, SCI_EOLANNOTATIONSETSTYLE, l.first - 1, AF_STYLE_PROFILE);
        }
        annotatedDocs.insert(doc);
    }
}

void App::clearProfile()
{
    profileNotes.clear();
    applyProfileAnnotations();
    // other documents keep theirs until they're shown next (annotatedDocs remembers them)
    if (!profileRun) {
        profileModel.reset();
        profileTargets.clear();
        profileView.clear();
    }
}

} // namespace af
