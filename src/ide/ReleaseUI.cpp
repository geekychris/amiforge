// Build > Package Release: the release details dialog, a release build (optimised, no debug
// info, into obj-release/), then a drawer with the program, icons, readme and install script,
// archived as release/<Name><version>.lha with an Aminet readme beside it.
//
// OS4 has no LhA that makes archives, but it has xad2lha, which turns anything XAD reads into
// an LhA archive: the drawer is packed with the SDK's tar and converted.
#include "App.h"
#include "ReleaseGen.h"

#include <string.h>
#include <time.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/icon.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/string.h>
#include <proto/label.h>
#include <proto/chooser.h>
#include <proto/texteditor.h>
#include <workbench/workbench.h>
#include <dos/dostags.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/string.h>
#include <gadgets/chooser.h>
#include <gadgets/texteditor.h>
#include <images/label.h>

namespace af {

struct ReleaseJob {
    int phase = 0;              // 1 building, 2 packing
    rel::Info info;
    std::string stage;          // release/stage: what goes into the archive
    std::string archive;        // release/<base>.lha
    std::string readme;         // release/<base>.readme
    std::string programPath;    // the release build's program
};

static std::string today()
{
    time_t now = time(nullptr);
    struct tm *t = localtime(&now);
    return std::to_string(t->tm_mday) + "." + std::to_string(t->tm_mon + 1) + "." + std::to_string(t->tm_year + 1900);
}

static rel::Info infoFor(const Project &p)
{
    rel::Info i;
    i.name = p.name;
    i.program = fileName(p.output);
    i.version = p.version;
    i.date = today();
    i.author = p.author;
    i.shortDesc = p.shortDesc;
    i.type = p.aminetType;
    i.description = p.description;
    return i;
}

bool App::releaseDialog()
{
    enum { RD_VERSION = 1, RD_AUTHOR, RD_SHORT, RD_TYPE, RD_DESC, RD_FLAGS, RD_OK, RD_CANCEL, RD_N };
    Object *g[RD_N] = {};
    Class *layout = ILayout->LAYOUT_GetClass();
    auto label = [](const char *t) {
        return IIntuition->NewObject(ILabel->LABEL_GetClass(), nullptr, LABEL_Text, (uint32)t, TAG_DONE);
    };
    auto field = [&](int id, const std::string &v, int max) {
        return g[id] = IIntuition->NewObject(IString->STRING_GetClass(), nullptr, GA_ID, id, GA_TabCycle, TRUE,
                                             STRINGA_MaxChars, max, STRINGA_TextVal, (uint32)v.c_str(), TAG_DONE);
    };
    const auto &types = rel::aminetTypes();
    std::vector<const char *> typeLabels;
    int typeIndex = 0;
    for (size_t i = 0; i < types.size(); i++) {
        typeLabels.push_back(types[i].c_str());
        if (types[i] == project.aminetType) typeIndex = (int)i;
    }
    typeLabels.push_back(nullptr);
    std::string flags = project.releaseFlags.empty() ? rel::releaseFlags(project.cflags) : project.releaseFlags;
    std::string author = project.author;
    if (author.empty()) {
        char n[200] = "", e[200] = "";
        IDOS->GetVar("AmiForge/Author", n, sizeof n, GVF_GLOBAL_ONLY);
        author = n;
        if (author.empty()) {
            IDOS->GetVar("AmiForge/GitName", n, sizeof n, GVF_GLOBAL_ONLY);
            IDOS->GetVar("AmiForge/GitEmail", e, sizeof e, GVF_GLOBAL_ONLY);
            author = n[0] ? (e[0] ? std::string(n) + " <" + e + ">" : std::string(n)) : "";
        }
    }
    static std::string title;
    title = "Package Release - " + project.name;
    Object *wo = IIntuition->NewObject(IWindow->WINDOW_GetClass(), nullptr,
        WA_Title, (uint32)title.c_str(), WA_DragBar, TRUE, WA_CloseGadget, TRUE, WA_DepthGadget, TRUE,
        WA_SizeGadget, TRUE, WA_Activate, TRUE, WA_PubScreen, (uint32)win->WScreen,
        WINDOW_Position, WPOS_CENTERWINDOW, WINDOW_RefWindow, (uint32)win,
        WINDOW_Layout, IIntuition->NewObject(layout, nullptr,
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT, LAYOUT_SpaceOuter, TRUE,
            LAYOUT_AddChild, field(RD_VERSION, project.version, 20), CHILD_Label, label("_Version"), CHILD_MinWidth, 460,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, field(RD_AUTHOR, author, 200), CHILD_Label, label("_Author (Name <email>)"),
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, field(RD_SHORT, project.shortDesc, 60), CHILD_Label, label("_Short description (40)"),
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, g[RD_TYPE] = IIntuition->NewObject(IChooser->CHOOSER_GetClass(), nullptr,
                GA_ID, RD_TYPE, CHOOSER_PopUp, TRUE, CHOOSER_LabelArray, (uint32)typeLabels.data(),
                CHOOSER_Selected, typeIndex, CHOOSER_MaxLabels, 50, TAG_DONE),
            CHILD_Label, label("Aminet _type"), CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, g[RD_DESC] = IIntuition->NewObject(ITextEditor->TEXTEDITOR_GetClass(), nullptr,
                GA_ID, RD_DESC, GA_TEXTEDITOR_Contents, (uint32)project.description.c_str(), TAG_DONE),
            CHILD_Label, label("_Description"), CHILD_MinHeight, 110,
            LAYOUT_AddChild, field(RD_FLAGS, flags, 400), CHILD_Label, label("Release _C/C++ flags"),
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, IIntuition->NewObject(layout, nullptr, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_EvenSize, TRUE,
                LAYOUT_AddChild, g[RD_OK] = IIntuition->NewObject(IButton->BUTTON_GetClass(), nullptr,
                    GA_ID, RD_OK, GA_RelVerify, TRUE, GA_Text, (uint32)"_Build and Package", TAG_DONE),
                LAYOUT_AddChild, g[RD_CANCEL] = IIntuition->NewObject(IButton->BUTTON_GetClass(), nullptr,
                    GA_ID, RD_CANCEL, GA_RelVerify, TRUE, GA_Text, (uint32)"_Cancel", TAG_DONE),
                TAG_DONE),
            CHILD_WeightedHeight, 0,
            TAG_DONE),
        TAG_DONE);
    if (!wo) return false;
    struct Window *w = (struct Window *)IIntuition->IDoMethod(wo, WM_OPEN);
    if (!w) {
        IIntuition->DisposeObject(wo);
        return false;
    }
    busyPointer(true);
    uint32 sig = 0;
    IIntuition->GetAttr(WINDOW_SigMask, wo, &sig);
    auto get = [&](int id) {
        uint32 v = 0;
        IIntuition->GetAttr(STRINGA_TextVal, g[id], &v);
        return trim(v ? (const char *)v : "");
    };
    int done = 0;
    while (!done) {
        IExec->Wait(sig);
        uint32 result;
        uint16 code;
        while ((result = IIntuition->IDoMethod(wo, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
            if ((result & WMHI_CLASSMASK) == WMHI_CLOSEWINDOW) done = -1;
            if ((result & WMHI_CLASSMASK) != WMHI_GADGETUP) continue;
            if ((result & WMHI_GADGETMASK) == RD_CANCEL) done = -1;
            if ((result & WMHI_GADGETMASK) != RD_OK) continue;
            // take the values, check them
            project.version = get(RD_VERSION);
            project.author = get(RD_AUTHOR);
            project.shortDesc = get(RD_SHORT);
            uint32 t = 0;
            IIntuition->GetAttr(CHOOSER_Selected, g[RD_TYPE], &t);
            if (t < types.size()) project.aminetType = types[t];
            STRPTR text = (STRPTR)IIntuition->DoGadgetMethod((struct Gadget *)g[RD_DESC], w, nullptr,
                                                             GM_TEXTEDITOR_ExportText, nullptr);
            project.description = text ? trim((const char *)text) : "";
            if (text) IExec->FreeVec(text);
            std::string f = get(RD_FLAGS);
            project.releaseFlags = f == rel::releaseFlags(project.cflags) ? "" : f;
            auto problems = rel::validate(infoFor(project));
            if (!problems.empty()) {
                std::string msg;
                for (auto &p : problems) msg += "- " + p + "\n";
                ask("Package Release", "Please fix these first:\n\n" + msg, "_OK");
                continue;
            }
            done = 1;
        }
    }
    IIntuition->DisposeObject(wo);
    busyPointer(false);
    if (done == 1) saveProject();
    return done == 1;
}

void App::packageRelease()
{
    if (builder.busy()) { setStatus("A build is already running"); return; }
    if (debugging()) { info("Stop debugging before packaging."); return; }
    if (!ensureProject() || project.path.empty()) {
        info("Packaging needs a project (Project > New Project).");
        return;
    }
    if (!sdkInstalled()) { build("build"); return; }   // explains what's missing
    if (!releaseDialog()) return;
    if (!saveAll()) return;

    delete releaseJob;
    releaseJob = new ReleaseJob;
    releaseJob->info = infoFor(project);
    releaseJob->phase = 1;
    // the release build: its own objects, optimised, without debug info
    Project rp = project;
    std::string flags = project.releaseFlags.empty() ? rel::releaseFlags(project.cflags) : project.releaseFlags;
    rp.cflags = flags;
    rp.cxxflags = project.releaseFlags.empty() ? rel::releaseFlags(project.cxxflags) : project.releaseFlags;
    rp.asflags = rel::releaseFlags(project.asflags);
    if (rp.asflags == "-O2") rp.asflags = "";
    rp.objdir = "obj-release";
    rp.output = "obj-release/" + fileName(project.output);
    releaseJob->programPath = project.abs(rp.output);

    // a clean release build: remove the last one's objects here (the builder's "rebuild"
    // runs Delete, which fails when there's nothing to delete yet)
    {
        BPTR nil = IDOS->Open("NIL:", MODE_NEWFILE), nilIn = IDOS->Open("NIL:", MODE_OLDFILE);
        IDOS->SystemTags(("Delete \"" + project.abs(rp.objdir) + "\" ALL QUIET FORCE").c_str(),
                         SYS_Input, nilIn, SYS_Output, nil, TAG_END);
        if (nil) IDOS->Close(nil);
        if (nilIn) IDOS->Close(nilIn);
    }
    diagnostics.clear();
    buildLog.clear();
    refreshMarkers();
    showBottomPage(0);
    pendingAfterBuild = 3;
    if (!builder.start(rp, "build")) {
        pendingAfterBuild = 0;
        info("There is nothing to build: the project has no C, C++ or assembler files.");
        return;
    }
    setStatus("Building the release of " + project.name + "...");
    updateToolbar();
}

static bool mkdirs(const std::string &path)
{
    BPTR l = IDOS->Lock(path.c_str(), SHARED_LOCK);
    if (!l) l = IDOS->CreateDir(path.c_str());
    if (!l) return false;
    IDOS->UnLock(l);
    return true;
}

static bool putIcon(const std::string &path, LONG type, const char *defaultTool)
{
    struct DiskObject *icon = IIcon->GetDefDiskObject(type);
    if (!icon) return false;
    icon->do_CurrentX = NO_ICON_POSITION;
    icon->do_CurrentY = NO_ICON_POSITION;
    if (defaultTool) icon->do_DefaultTool = (STRPTR)defaultTool;
    BOOL ok = IIcon->PutDiskObject(path.c_str(), icon);
    IIcon->FreeDiskObject(icon);
    return ok;
}

void App::continuePackaging(bool ok)
{
    ReleaseJob *j = releaseJob;
    if (!j) return;
    auto logLine = [&](MsgKind k, const std::string &text) {
        BuildLine l;
        l.kind = k;
        l.text = text;
        diagnostics.push_back(l);
        buildLog.detach();
        struct Node *n = buildLog.add({ (k == MsgKind::Info ? "== " : "") + text }, (void *)(intptr_t)diagnostics.size());
        if (n) buildLog.setPens(n, k == MsgKind::Error ? penError : penNote);
        buildLog.attach(buildLog.count() - 1);
    };
    auto fail = [&](const std::string &why) {
        logLine(MsgKind::Error, why);
        setStatus("Packaging failed");
        delete releaseJob;
        releaseJob = nullptr;
    };
    if (!ok) { fail(j->phase == 1 ? "The release build failed: nothing was packaged." : "Packaging failed."); return; }

    if (j->phase == 1) {
        // stage the drawer
        const rel::Info &i = j->info;
        std::string base = rel::archiveBase(i);
        std::string relDir = project.abs("release");
        j->stage = joinPath(relDir, "stage");
        j->archive = joinPath(relDir, base + ".lha");
        j->readme = joinPath(relDir, base + ".readme");
        // a fresh stage
        std::vector<std::pair<std::string, std::string>> cmds;
        {
            BPTR nil = IDOS->Open("NIL:", MODE_NEWFILE), nilIn = IDOS->Open("NIL:", MODE_OLDFILE);
            IDOS->SystemTags(("Delete \"" + j->stage + "\" \"" + j->archive + "\" ALL QUIET FORCE").c_str(),
                             SYS_Input, nilIn, SYS_Output, nil, TAG_END);
            if (nil) IDOS->Close(nil);
            if (nilIn) IDOS->Close(nilIn);
        }
        std::string drawer = joinPath(j->stage, i.name);
        if (!mkdirs(relDir) || !mkdirs(j->stage) || !mkdirs(drawer)) { fail("Can't make " + drawer); return; }
        std::string readme = rel::aminetReadme(i);
        if (!writeFile(j->readme, readme) || !writeFile(joinPath(drawer, i.name + ".readme"), readme) ||
            !writeFile(joinPath(j->stage, "Install"), rel::installScript(i))) {
            fail("Can't write the readme and install script in " + relDir);
            return;
        }
        IDOS->SetProtection(joinPath(j->stage, "Install").c_str(), EXDF_SCRIPT);
        // icons: the drawer, the program, the readme, the installer
        bool icons = putIcon(drawer, WBDRAWER, nullptr) &&
                     putIcon(joinPath(drawer, i.program), WBTOOL, nullptr) &&
                     putIcon(joinPath(drawer, i.name + ".readme"), WBPROJECT, "SYS:Utilities/MultiView") &&
                     putIcon(joinPath(j->stage, "Install"), WBPROJECT, "C:IconX");
        if (!icons) logLine(MsgKind::Warning, "Some icons couldn't be made (icon.library's default icons are missing?).");
        logLine(MsgKind::Info, "Packing " + fileName(j->archive));
        std::string tar = joinPath(relDir, base + ".tar");
        std::string prog = "\"" + joinPath(drawer, i.program) + "\"";
        cmds.push_back({ "Copying the program", "Copy \"" + j->programPath + "\" " + prog + " CLONE" });
        if (extension(i.program) != "library" && extension(i.program) != "device")
            cmds.push_back({ "Stripping it", "SDK:gcc/bin/strip " + prog });
        cmds.push_back({ "Packing the drawer", "SDK:C/tar --force-local -cf \"" + tar + "\" \"" + i.name + "\" \"" + i.name + ".info\" Install Install.info" });
        cmds.push_back({ "Making the LhA archive", "xad2lha \"" + tar + "\" \"" + j->archive + "\" QUIET" });
        cmds.push_back({ "Tidying up", "Delete \"" + tar + "\" QUIET" });
        cmds.push_back({ "The archive holds", "xadList \"" + j->archive + "\"" });
        j->phase = 2;
        pendingAfterBuild = 3;
        if (!builder.startCommands(j->stage, cmds)) { pendingAfterBuild = 0; fail("Couldn't start packing."); return; }
        updateToolbar();
        return;
    }
    // phase 2 done
    if (!fileExists(j->archive)) { fail("The archive wasn't made."); return; }
    logLine(MsgKind::Info, "Release ready: " + j->archive + " and " + fileName(j->readme) + " (for Aminet)");
    setStatus("Packaged " + fileName(j->archive));
    delete releaseJob;
    releaseJob = nullptr;
}

} // namespace af
