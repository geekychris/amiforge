// The Git tab, change markers and blame.
//
// Everything that only reads the repository goes through GitCore (in-process, fast); what
// changes it (add, reset, checkout, branch) runs sgit through App::gitRun like the Git menu.
//
//  - Change markers: a thin margin next to the text: green = added, blue = changed lines
//    compared with HEAD; a red line where lines were deleted. Updated when a file is opened
//    or saved, and a moment after editing stops.
//  - The Git tab: the branch (a chooser that switches branches), the changed files with
//    Stage / Unstage / Discard / Diff, New Branch, Commit, Blame.
//  - Blame: a margin with the commit, author and date of each line; the line's commit is
//    shown in the status bar, and clicking the margin shows the whole message.
#include "App.h"
#include "Commands.h"
#include "GitCore.h"
#include "LineDiff.h"
#include "Integrations.h"

#include <string.h>
#include <time.h>
#include <algorithm>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/label.h>
#include <proto/chooser.h>
#include <proto/listbrowser.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/chooser.h>
#include <gadgets/listbrowser.h>
#include <images/label.h>

#include "Scintilla.h"

namespace af {

static inline intptr_t S(SciView *v, unsigned m, uintptr_t w = 0, intptr_t l = 0) { return v ? v->send(m, w, l) : 0; }
static intptr_t bgr(uint32_t rgb) { return ((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF); }

enum { MARK_GIT_ADDED = 21, MARK_GIT_CHANGED = 22, MARK_GIT_DELETED = 23, MARK_GIT_DELETED_TOP = 24 };
enum { MARGIN_GIT = 3, MARGIN_BLAME = 4 };
const int STYLE_BLAME = 200;

struct App::GitState {
    std::string repoDir;
    git::Repo *repo = nullptr;
    std::string headId;
    std::map<std::string, std::string> headFiles;      // of headId
    std::map<std::string, std::vector<std::string>> headText;
    bool markersDirty = false;
    int64_t dirtyAt = 0;
    // blame of the current document
    std::string blamePath, blameHead;
    std::vector<git::Repo::FileVersion> versions;
    std::vector<std::vector<std::string>> versionLines;
    std::vector<int> owner;
    // the Git tab
    std::string panelRepo;
    std::vector<git::FileStatus> rows;
    ~GitState() { delete repo; }
};

// --- OS helpers ---

static int64_t nowTicks()
{
    struct DateStamp ds;
    IDOS->DateStamp(&ds);
    return ((int64_t)ds.ds_Days * 1440 + ds.ds_Minute) * 3000 + ds.ds_Tick;
}

static bool osListDir(const std::string &dir, std::vector<std::string> &names)
{
    BPTR lock = IDOS->Lock(dir.c_str(), SHARED_LOCK);
    if (!lock) return false;
    APTR ctx = IDOS->ObtainDirContextTags(EX_FileLockInput, lock, EX_DoCurrentDir, TRUE,
                                          EX_DataFields, EXF_NAME | EXF_TYPE, TAG_END);
    if (ctx) {
        struct ExamineData *ed;
        while ((ed = IDOS->ExamineDir(ctx))) {
            std::string n = (const char *)ed->Name;
            if (EXD_IS_DIRECTORY(ed)) n += "/";
            names.push_back(n);
        }
        IDOS->ReleaseDirContext(ctx);
    }
    IDOS->UnLock(lock);
    return true;
}

// The repository a file is in (the nearest drawer above it with a .git).
static std::string repoOf(const std::string &path)
{
    std::string d = dirName(path);
    for (int i = 0; i < 16 && !d.empty(); i++) {
        if (fileExists(joinPath(d, ".git/HEAD"))) return d;
        std::string up = dirName(d);
        if (up == d || up.empty()) break;
        d = up;
    }
    return "";
}

static git::Repo *repoFor(App::GitState *gs, const std::string &dir)
{
    if (dir.empty()) return nullptr;
    if (gs->repo && gs->repoDir == dir) return gs->repo;
    delete gs->repo;
    gs->repo = new git::Repo;
    gs->repoDir = dir;
    gs->headId.clear();
    gs->headFiles.clear();
    gs->headText.clear();
    gs->blamePath.clear();
    if (!gs->repo->open(dir, osListDir)) {
        delete gs->repo;
        gs->repo = nullptr;
        gs->repoDir.clear();
    }
    return gs->repo;
}

// HEAD moved (a commit, a checkout): drop what was cached for the old one.
static void checkHead(App::GitState *gs)
{
    if (!gs->repo) return;
    std::string h = gs->repo->resolve("HEAD");
    if (h == gs->headId) return;
    gs->headId = h;
    gs->headFiles.clear();
    gs->headText.clear();
    if (!h.empty()) gs->repo->treeFiles(h, gs->headFiles);
}

static std::string relIn(const std::string &repo, const std::string &abs)
{
    std::string r = relativeTo(repo, abs);
    return r == abs ? "" : r;
}

static std::string currentText(SciView *ed)
{
    intptr_t len = S(ed, SCI_GETLENGTH);
    const char *p = (const char *)S(ed, SCI_GETCHARACTERPOINTER);
    return p ? std::string(p, len) : std::string();
}

static std::string q(const std::string &s)
{
    return s.find_first_of(" \t\"") == std::string::npos ? s : "\"" + s + "\"";
}

// "2024-01-02" from a commit time and its zone ("+0100")
static std::string commitDate(int64_t t, const std::string &tz)
{
    if (tz.size() == 5) {
        int off = atoi(tz.substr(1, 2).c_str()) * 3600 + atoi(tz.substr(3, 2).c_str()) * 60;
        t += tz[0] == '-' ? -off : off;
    }
    int64_t days = t / 86400;
    if (t < 0 && t % 86400) days--;
    // civil from days (H. Hinnant)
    int64_t z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097, yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = yoe + era * 400, doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    int64_t d = doy - (153 * mp + 2) / 5 + 1, m = mp + (mp < 10 ? 3 : -9);
    y += m <= 2;
    return strprintf("%04ld-%02ld-%02ld", (long)y, (long)m, (long)d);
}

// --- margins and markers ---

void App::gitApplyMargins()
{
    if (!editor || !cur) return;
    if (S(editor, SCI_GETMARGINS) < 5) S(editor, SCI_SETMARGINS, 5);
    S(editor, SCI_SETMARGINTYPEN, MARGIN_GIT, SC_MARGIN_SYMBOL);
    S(editor, SCI_SETMARGINMASKN, MARGIN_GIT,
      (1 << MARK_GIT_ADDED) | (1 << MARK_GIT_CHANGED) | (1 << MARK_GIT_DELETED_TOP));
    S(editor, SCI_SETMARGINWIDTHN, MARGIN_GIT, cur->hex || cur->readOnly ? 0 : 5);
    S(editor, SCI_SETMARGINSENSITIVEN, MARGIN_GIT, 1);
    S(editor, SCI_MARKERDEFINE, MARK_GIT_ADDED, SC_MARK_FULLRECT);
    S(editor, SCI_MARKERSETBACK, MARK_GIT_ADDED, bgr(0x2DA44E));
    S(editor, SCI_MARKERDEFINE, MARK_GIT_CHANGED, SC_MARK_FULLRECT);
    S(editor, SCI_MARKERSETBACK, MARK_GIT_CHANGED, bgr(0x3B82E8));
    S(editor, SCI_MARKERDEFINE, MARK_GIT_DELETED, SC_MARK_UNDERLINE);   // a red line under the line before
    S(editor, SCI_MARKERSETBACK, MARK_GIT_DELETED, bgr(0xE5534B));
    S(editor, SCI_MARKERDEFINE, MARK_GIT_DELETED_TOP, SC_MARK_FULLRECT);
    S(editor, SCI_MARKERSETBACK, MARK_GIT_DELETED_TOP, bgr(0xE5534B));
    // the symbol margin mustn't draw ours too
    intptr_t m1 = S(editor, SCI_GETMARGINMASKN, AF_MARGIN_SYMBOLS);
    S(editor, SCI_SETMARGINMASKN, AF_MARGIN_SYMBOLS,
      m1 & ~((1 << MARK_GIT_ADDED) | (1 << MARK_GIT_CHANGED) | (1 << MARK_GIT_DELETED) | (1 << MARK_GIT_DELETED_TOP)));
    // blame
    S(editor, SCI_SETMARGINTYPEN, MARGIN_BLAME, SC_MARGIN_TEXT);
    S(editor, SCI_SETMARGINMASKN, MARGIN_BLAME, 0);
    S(editor, SCI_SETMARGINSENSITIVEN, MARGIN_BLAME, 1);
    S(editor, SCI_STYLESETFONT, STYLE_BLAME, (intptr_t)"DejaVu Sans Mono");
    S(editor, SCI_STYLESETSIZE, STYLE_BLAME, std::max(8, fontSize - 2));
    S(editor, SCI_STYLESETFORE, STYLE_BLAME, bgr(0x505A66));
    S(editor, SCI_STYLESETBACK, STYLE_BLAME, bgr(0xEEF1F5));
    int w = blameOn && !cur->hex && !cur->readOnly ? (int)S(editor, SCI_TEXTWIDTH, STYLE_BLAME, (intptr_t)"a1b2c3d Annabelle Co 2024-01-02 ") : 0;
    S(editor, SCI_SETMARGINWIDTHN, MARGIN_BLAME, w);
}

void App::gitUpdateMarkers()
{
    if (!editor || !cur) return;
    for (int m : { MARK_GIT_ADDED, MARK_GIT_CHANGED, MARK_GIT_DELETED, MARK_GIT_DELETED_TOP })
        S(editor, SCI_MARKERDELETEALL, m);
    if (cur->path.empty() || cur->hex || cur->readOnly) return;
    if (!gitState) gitState = new GitState;
    std::string dir = repoOf(cur->path);
    git::Repo *r = repoFor(gitState, dir);
    if (!r) return;
    checkHead(gitState);
    std::string rel = relIn(dir, cur->path);
    if (rel.empty()) return;
    std::vector<std::string> old;
    auto it = gitState->headText.find(rel);
    if (it != gitState->headText.end()) old = it->second;
    else {
        auto f = gitState->headFiles.find(rel);
        std::string blob;
        if (f != gitState->headFiles.end() && r->readBlob(f->second, blob)) old = splitLines(blob);
        else if (f == gitState->headFiles.end() && !fileTrackedOrHeadless(gitState, rel)) return;   // untracked: no markers
        gitState->headText[rel] = old;
    }
    std::vector<std::string> now = splitLines(currentText(editor));
    std::vector<LineChange> ch = changeMarkers(old, now);
    for (size_t l = 0; l < ch.size(); l++) {
        switch (ch[l]) {
        case LineChange::Added: S(editor, SCI_MARKERADD, l, MARK_GIT_ADDED); break;
        case LineChange::Modified: S(editor, SCI_MARKERADD, l, MARK_GIT_CHANGED); break;
        case LineChange::DeletedAbove:
            if (l == 0) S(editor, SCI_MARKERADD, 0, MARK_GIT_DELETED_TOP);
            else S(editor, SCI_MARKERADD, l - 1, MARK_GIT_DELETED);
            break;
        default: break;
        }
    }
    // removed at the very end
    if (!old.empty() && now.size() < old.size() && ch.size() && ch.back() == LineChange::None) {
        std::vector<Hunk> hs = diffLines(old, now);
        if (!hs.empty() && hs.back().bStart >= (int)now.size() && hs.back().bCount == 0)
            S(editor, SCI_MARKERADD, now.size() - 1, MARK_GIT_DELETED);
    }
}

// --- blame ---

void App::gitUpdateBlame()
{
    if (!editor || !cur) return;
    S(editor, SCI_MARGINTEXTCLEARALL);
    if (gitState) gitState->owner.clear();
    if (!blameOn || cur->path.empty() || cur->hex || cur->readOnly) return;
    if (!gitState) gitState = new GitState;
    std::string dir = repoOf(cur->path);
    git::Repo *r = repoFor(gitState, dir);
    if (!r) {
        S(editor, SCI_SETMARGINWIDTHN, MARGIN_BLAME, 0);
        setStatus("Blame: this file isn't in a git repository");
        return;
    }
    checkHead(gitState);
    std::string rel = relIn(dir, cur->path);
    GitState *gs = gitState;
    if (gs->blamePath != cur->path || gs->blameHead != gs->headId) {
        busyPointer(true);
        setStatus("Reading the history of " + rel + "...");
        gs->versions = r->fileHistory("HEAD", rel, 60);
        gs->versionLines.clear();
        for (auto &v : gs->versions) {
            std::string b;
            r->readBlob(v.blob, b);
            gs->versionLines.push_back(splitLines(b));
        }
        gs->blamePath = cur->path;
        gs->blameHead = gs->headId;
        busyPointer(false);
        setStatus(strprintf("Blame: %ld commit%s changed %s", (long)gs->versions.size(), gs->versions.size() == 1 ? "" : "s", rel.c_str()));
    }
    std::vector<std::string> now = splitLines(currentText(editor));
    gs->owner = blameLines(now, gs->versionLines);
    int prev = -2;
    for (size_t l = 0; l < gs->owner.size(); l++) {
        int o = gs->owner[l];
        std::string text;
        if (o != prev) {
            if (o < 0) text = "(not committed yet)";
            else {
                const git::Commit &c = gs->versions[o].commit;
                std::string who = c.author.size() > 12 ? c.author.substr(0, 12) : c.author;
                text = c.id.substr(0, 7) + " " + who + std::string(13 - std::min<size_t>(who.size(), 12), ' ') + commitDate(c.time, c.tz);
            }
        }
        prev = o;
        if (!text.empty()) {
            S(editor, SCI_MARGINSETTEXT, l, (intptr_t)text.c_str());
            S(editor, SCI_MARGINSETSTYLE, l, STYLE_BLAME);
        }
    }
}

void App::gitBlameStatus(int line)
{
    if (!blameOn || !gitState || line < 0 || line >= (int)gitState->owner.size()) return;
    int o = gitState->owner[line];
    if (o < 0) { setStatus("Line " + std::to_string(line + 1) + ": not committed yet"); return; }
    const git::Commit &c = gitState->versions[o].commit;
    setStatus(c.id.substr(0, 7) + " " + c.author + ", " + commitDate(c.time, c.tz) + ": " + c.summary());
}

// --- the Git tab ---

static const char *statusWord(const git::FileStatus &s)
{
    if (s.staged == '?') return "untracked";
    if (s.staged == 'A') return s.work == 'M' ? "added, changed" : s.work == 'D' ? "added, deleted" : "added";
    if (s.staged == 'D') return "deleted (staged)";
    if (s.staged == 'M') return s.work == 'M' ? "staged, changed" : s.work == 'D' ? "staged, deleted" : "staged";
    if (s.work == 'M') return "changed";
    if (s.work == 'D') return "deleted";
    return "";
}

// .gitignore patterns: "*.o", "obj/", "/build", "dir/file"
static bool globMatch(const char *p, const char *s)
{
    for (; *p; p++, s++) {
        if (*p == '*') {
            while (p[1] == '*') p++;
            for (const char *t = s; ; t++) {
                if (globMatch(p + 1, t)) return true;
                if (!*t || *t == '/') return false;
            }
        }
        if (!*s) return false;
        if (*p != '?' && *p != *s) return false;
    }
    return !*s;
}

static bool ignored(const std::vector<std::string> &pats, const std::string &rel, bool isDir)
{
    std::string base = rel.substr(rel.rfind('/') == std::string::npos ? 0 : rel.rfind('/') + 1);
    bool ign = false;
    for (auto p : pats) {
        bool neg = !p.empty() && p[0] == '!';
        if (neg) p = p.substr(1);
        bool dirOnly = !p.empty() && p.back() == '/';
        if (dirOnly) { if (!isDir) continue; p.pop_back(); }
        bool anchored = p.find('/') != std::string::npos;
        if (!p.empty() && p[0] == '/') p = p.substr(1);
        bool m = anchored ? globMatch(p.c_str(), rel.c_str()) : globMatch(p.c_str(), base.c_str());
        if (m) ign = !neg;
    }
    return ign;
}

static void readIgnores(const std::string &repo, std::vector<std::string> &pats)
{
    for (const char *f : { ".gitignore", ".git/info/exclude" }) {
        std::string t;
        if (!readFile(joinPath(repo, f), t)) continue;
        for (auto &l : splitLines(t)) {
            std::string s = trim(l);
            if (!s.empty() && s[0] != '#') pats.push_back(s);
        }
    }
}

// The work tree's files (not .git, not ignored), at most `limit`.
static void scanWork(const std::string &repo, const std::string &rel, const std::vector<std::string> &pats,
                     std::vector<std::string> &out, size_t limit)
{
    std::vector<std::string> names;
    if (!osListDir(rel.empty() ? repo : joinPath(repo, rel), names)) return;
    for (auto &n : names) {
        if (out.size() >= limit) return;
        bool dir = !n.empty() && n.back() == '/';
        std::string name = dir ? n.substr(0, n.size() - 1) : n;
        if (name == ".git") continue;
        std::string r = rel.empty() ? name : rel + "/" + name;
        if (ignored(pats, r, dir)) continue;
        if (dir) scanWork(repo, r, pats, out, limit);
        else out.push_back(r);
    }
}

// size and modification time (seconds since 1970, from the Amiga's local time)
static bool fileInfo(const std::string &path, uint32_t &size, uint32_t &mtime)
{
    struct ExamineData *ed = IDOS->ExamineObjectTags(EX_StringNameInput, path.c_str(), TAG_END);
    if (!ed) return false;
    size = (uint32_t)ed->FileSize;
    mtime = (uint32_t)((ed->Date.ds_Days * 86400 + ed->Date.ds_Minute * 60 + ed->Date.ds_Tick / 50) + 252460800);
    IDOS->FreeDosObject(DOS_EXAMINEDATA, ed);
    return true;
}

bool fileTrackedOrHeadless(App::GitState *gs, const std::string &rel)
{
    // a file that isn't in HEAD: show it all as added only if the index has it (a new file
    // that was staged) or there is no commit yet
    if (gs->headId.empty()) return true;
    std::vector<git::IndexEntry> idx;
    if (!gs->repo || !gs->repo->readIndex(idx)) return false;
    for (auto &e : idx) if (e.path == rel) return true;
    return false;
}

static std::vector<git::FileStatus> repoStatus(App::GitState *gs, const std::string &dir)
{
    std::vector<git::FileStatus> out;
    git::Repo *r = repoFor(gs, dir);
    if (!r) return out;
    checkHead(gs);
    std::vector<git::IndexEntry> idx;
    r->readIndex(idx);
    std::vector<std::string> pats;
    readIgnores(dir, pats);
    std::vector<std::string> files;
    scanWork(dir, "", pats, files, 3000);
    std::map<std::string, const git::IndexEntry *> byPath;
    for (auto &e : idx) byPath[e.path] = &e;
    std::map<std::string, std::string> work;
    for (auto &e : idx) {
        uint32_t size, mtime;
        std::string p = joinPath(dir, e.path);
        if (!fileInfo(p, size, mtime)) continue;    // deleted
        if (size != e.size) { work[e.path] = "size"; continue; }   // anything but e.id
        if (mtime == e.mtime) { work[e.path] = e.id; continue; }
        std::string data;
        work[e.path] = readFile(p, data) ? git::blobId(data) : "";
    }
    for (auto &f : files)
        if (!byPath.count(f)) work[f] = "untracked";
    return git::computeStatus(gs->headFiles, idx, work);
}

Object *App::createGitPage()
{
    Class *layout = ILayout->LAYOUT_GetClass();
    auto btn = [&](uint32_t gid, const char *text, const char *hint) {
        return igad[gid - GID_INTEG_FIRST] = IIntuition->NewObject(IButton->BUTTON_GetClass(), nullptr,
            GA_ID, gid, GA_RelVerify, TRUE, GA_Text, (uint32)text, GA_HintInfo, (uint32)hint, TAG_DONE);
    };
    gitBranchLabels = new struct List;
    IExec->NewList(gitBranchLabels);
    static const char *titles[] = { "Status", "File" };
    static const int weights[] = { 22, 78 };
    return IIntuition->NewObject(layout, nullptr, LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_AddChild, IIntuition->NewObject(layout, nullptr, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
            LAYOUT_AddChild, igad[GID_GIT_BRANCH - GID_INTEG_FIRST] = IIntuition->NewObject(IChooser->CHOOSER_GetClass(), nullptr,
                GA_ID, GID_GIT_BRANCH, GA_RelVerify, TRUE, CHOOSER_Labels, (uint32)gitBranchLabels,
                GA_HintInfo, (uint32)"The current branch: choose another to switch to it", TAG_DONE),
            CHILD_Label, IIntuition->NewObject(ILabel->LABEL_GetClass(), nullptr, LABEL_Text, (uint32)"Branch", TAG_DONE),
            CHILD_WeightedWidth, 30,
            LAYOUT_AddChild, btn(GID_GIT_NEWBRANCH, "New Branch...", "Create a branch here and switch to it"),
            CHILD_WeightedWidth, 0,
            LAYOUT_AddChild, igad[GID_GIT_STATUS - GID_INTEG_FIRST] = IIntuition->NewObject(IButton->BUTTON_GetClass(), nullptr,
                GA_ID, GID_GIT_STATUS, GA_ReadOnly, TRUE, GA_Underscore, 0, GA_Text, (uint32)"", BUTTON_Justification, BCJ_LEFT, TAG_DONE),
            CHILD_WeightedWidth, 70,
            LAYOUT_AddChild, btn(GID_GIT_REFRESH, "Refresh", "Read the repository's status again"),
            CHILD_WeightedWidth, 0,
            TAG_DONE),
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, igad[GID_GIT_FILES - GID_INTEG_FIRST] = gitFiles.create(GID_GIT_FILES, titles, weights),
        LAYOUT_AddChild, IIntuition->NewObject(layout, nullptr, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
            LAYOUT_EvenSize, TRUE,
            LAYOUT_AddChild, btn(GID_GIT_STAGE, "Stage", "Add the file's changes to the next commit (git add)"),
            LAYOUT_AddChild, btn(GID_GIT_UNSTAGE, "Unstage", "Take the file out of the next commit (git reset)"),
            LAYOUT_AddChild, btn(GID_GIT_DISCARD, "Discard...", "Throw away the file's changes (git checkout)"),
            LAYOUT_AddChild, btn(GID_GIT_DIFF, "Diff", "Show the file's changes (or double-click it)"),
            LAYOUT_AddChild, btn(GID_GIT_BLAME, "Blame", "Show who last changed each line of the current file"),
            LAYOUT_AddChild, btn(GID_GIT_COMMIT, "Commit...", "Commit (Git > Commit)"),
            TAG_DONE),
        CHILD_WeightedHeight, 0,
        TAG_DONE);
}

static void setIG(App *a, uint32_t gid, uint32_t tag, uint32_t val)
{
    Object *o = a->igad[gid - GID_INTEG_FIRST];
    if (o && a->win) IIntuition->SetGadgetAttrs((struct Gadget *)o, a->win, nullptr, tag, val, TAG_DONE);
}

void App::gitRefreshPanel()
{
    if (!win) return;
    if (!gitState) gitState = new GitState;
    std::string dir = gitRepoDir();
    if (dir.empty() && cur && !cur->path.empty()) dir = repoOf(cur->path);
    static std::string statusText;
    gitFiles.clear();
    gitState->rows.clear();
    // branches
    Object *ch = igad[GID_GIT_BRANCH - GID_INTEG_FIRST];
    if (ch) IIntuition->SetGadgetAttrs((struct Gadget *)ch, win, nullptr, CHOOSER_Labels, ~0UL, TAG_DONE);
    struct Node *n;
    while ((n = IExec->RemHead(gitBranchLabels))) IChooser->FreeChooserNode(n);
    gitBranchNames.clear();
    git::Repo *r = repoFor(gitState, dir);
    if (!r) {
        statusText = "Not a git repository: Git > Clone, or 'sgit init' in the project's drawer";
        setIG(this, GID_GIT_STATUS, GA_Text, (uint32)statusText.c_str());
        if (ch) IIntuition->RefreshSetGadgetAttrs((struct Gadget *)ch, win, nullptr, CHOOSER_Labels, (uint32)gitBranchLabels, TAG_DONE);
        for (int g : { GID_GIT_STAGE, GID_GIT_UNSTAGE, GID_GIT_DISCARD, GID_GIT_DIFF, GID_GIT_NEWBRANCH, GID_GIT_COMMIT })
            setIG(this, g, GA_Disabled, TRUE);
        return;
    }
    for (int g : { GID_GIT_STAGE, GID_GIT_UNSTAGE, GID_GIT_DISCARD, GID_GIT_DIFF, GID_GIT_NEWBRANCH, GID_GIT_COMMIT })
        setIG(this, g, GA_Disabled, FALSE);
    busyPointer(true);
    std::string curBranch = r->headBranch();
    int sel = 0;
    gitBranchNames = r->branches();
    if (curBranch.empty()) gitBranchNames.insert(gitBranchNames.begin(), "(detached HEAD)");
    for (size_t i = 0; i < gitBranchNames.size(); i++) {
        if (gitBranchNames[i] == curBranch) sel = (int)i;
        struct Node *cn = IChooser->AllocChooserNode(CNA_Text, (uint32)gitBranchNames[i].c_str(), CNA_CopyText, TRUE, TAG_DONE);
        if (cn) IExec->AddTail(gitBranchLabels, cn);
    }
    if (ch) IIntuition->RefreshSetGadgetAttrs((struct Gadget *)ch, win, nullptr, CHOOSER_Labels, (uint32)gitBranchLabels,
                                              CHOOSER_Selected, sel, TAG_DONE);
    // files
    gitState->rows = repoStatus(gitState, dir);
    gitState->panelRepo = dir;
    gitFiles.detach();
    for (size_t i = 0; i < gitState->rows.size(); i++) {
        auto &s = gitState->rows[i];
        struct Node *row = gitFiles.add({ statusWord(s), s.path }, (void *)(intptr_t)(i + 1));
        if (row) gitFiles.setPens(row, s.staged == '?' ? penDim : s.staged != ' ' ? penNote : s.work == 'D' ? penError : penWarning);
    }
    gitFiles.attach();
    git::Commit c;
    std::string headInfo = r->readCommit(gitState->headId, c) ? c.id.substr(0, 7) + " " + c.summary() : "no commits yet";
    statusText = strprintf("%ld changed  -  HEAD %s", (long)gitState->rows.size(), headInfo.c_str());
    setIG(this, GID_GIT_STATUS, GA_Text, (uint32)statusText.c_str());
    busyPointer(false);
}

static const git::FileStatus *selectedRow(App *a)
{
    struct Node *n = a->gitFiles.selected();
    intptr_t i = (intptr_t)a->gitFiles.userData(n) - 1;
    if (!a->gitState || i < 0 || i >= (intptr_t)a->gitState->rows.size()) return nullptr;
    return &a->gitState->rows[i];
}

void App::gitPanelAction(int what)
{
    const git::FileStatus *s = selectedRow(this);
    std::string dir = gitState ? gitState->panelRepo : "";
    if (what == GID_GIT_REFRESH) { gitRefreshPanel(); return; }
    if (what == GID_GIT_NEWBRANCH) { gitNewBranch(); return; }
    if (what == GID_GIT_COMMIT) { handleMenu(CMD_GIT_COMMIT); return; }
    if (what == GID_GIT_BLAME) { handleMenu(CMD_GIT_BLAME); return; }
    if (!s || dir.empty()) { setStatus("Select a file in the Git tab first"); return; }
    std::string path = s->path;
    switch (what) {
    case GID_GIT_STAGE:
        if (!saveAll()) return;
        gitPendingRefresh = "panel";   // come back to the Git tab when it worked
        if (s->work == 'D') gitRun(dir, { { "Staging the deletion of " + path, "rm --cached " + q(path) } }, "git rm");
        else gitRun(dir, { { "Staging " + path, "add " + q(path) } }, "git add");
        break;
    case GID_GIT_UNSTAGE:
        if (s->staged == ' ' || s->staged == '?') { setStatus(path + " isn't staged"); return; }
        gitPendingRefresh = "panel";
        gitRun(dir, { { "Unstaging " + path, "reset HEAD -- " + q(path) } }, "git reset");
        break;
    case GID_GIT_DISCARD: {
        if (s->staged == '?') {
            if (ask("Discard", "Delete the untracked file " + path + "?", "_Delete|_Cancel") != 1) return;
            IDOS->Delete(joinPath(dir, path).c_str());
            gitRefreshPanel();
            return;
        }
        if (s->work == ' ') { setStatus(path + " has no unstaged changes to discard (Unstage it first)"); return; }
        if (ask("Discard", "Throw away the changes to " + path + "?\nThis can't be undone.", "_Discard|_Cancel") != 1) return;
        Document *d = findDocument(joinPath(dir, path));
        if (d && d->modified) {
            // it's about to be replaced from the index: don't ask to save it later
            d->modified = false;
        }
        gitPendingRefresh = "panel";
        gitRun(dir, { { "Discarding the changes to " + path, "checkout -- " + q(path) } }, "git checkout");
        break;
    }
    case GID_GIT_DIFF:
        gitShowFileDiff(path, s->work == ' ' && s->staged != ' ');
        break;
    }
}

void App::gitShowFileDiff(const std::string &rel, bool staged)
{
    if (!gitState || !gitState->repo) return;
    git::Repo *r = gitState->repo;
    std::string dir = gitState->panelRepo;
    // staged: HEAD -> index; otherwise index (or HEAD) -> the file on disk
    std::string a, b;
    std::vector<git::IndexEntry> idx;
    r->readIndex(idx);
    std::string indexId;
    for (auto &e : idx) if (e.path == rel) indexId = e.id;
    auto h = gitState->headFiles.find(rel);
    std::string headId = h == gitState->headFiles.end() ? "" : h->second;
    if (staged) {
        if (!headId.empty()) r->readBlob(headId, a);
        if (!indexId.empty()) r->readBlob(indexId, b);
    } else {
        if (!indexId.empty()) r->readBlob(indexId, a);
        else if (!headId.empty()) r->readBlob(headId, a);
        readFile(joinPath(dir, rel), b);
    }
    std::string d = unifiedDiff(splitLines(a), splitLines(b), rel);
    if (d.empty()) d = "No differences in " + rel + (staged ? " between HEAD and the index.\n" : ".\n");
    Document *doc = newDocument();
    doc->title = fileName(rel) + (staged ? " (staged diff)" : " (diff)");
    doc->lexerName = "changes.diff";
    doc->readOnly = true;
    activate(doc);
    S(editor, SCI_SETREADONLY, 0);
    S(editor, SCI_SETTEXT, 0, (intptr_t)d.c_str());
    S(editor, SCI_EMPTYUNDOBUFFER);
    S(editor, SCI_SETSAVEPOINT);
    S(editor, SCI_SETREADONLY, 1);
    doc->modified = false;
    integrationsActivated(doc);
    rebuildTabs();
    updateTitle();
}

void App::gitSwitchBranch(int index)
{
    if (!gitState || index < 0 || index >= (int)gitBranchNames.size()) return;
    std::string dir = gitState->panelRepo;
    git::Repo *r = gitState->repo;
    if (!r || dir.empty()) return;
    std::string to = gitBranchNames[index];
    if (to == r->headBranch() || to[0] == '(') return;
    bool dirty = false;
    for (auto &s : gitState->rows) if (s.staged != '?') dirty = true;
    if (dirty && ask("Switch Branch", "There are uncommitted changes. Switching to " + to + " may overwrite them.\nSwitch anyway?",
                     "_Switch|_Cancel") != 1) {
        gitRefreshPanel();   // put the chooser back
        return;
    }
    if (!saveAll()) return;
    gitPendingRefresh = "panel";
    gitRun(dir, { { "Switching to " + to, "checkout " + q(to) } }, "git checkout");
}

void App::gitNewBranch()
{
    std::string dir = gitState && !gitState->panelRepo.empty() ? gitState->panelRepo : gitRepoDir();
    if (dir.empty()) { info("This project isn't in a git repository."); return; }
    std::string name;
    if (!askString("New Branch", "Name of the new branch (it starts at the current commit):", name)) return;
    name = trim(name);
    if (name.empty() || name.find_first_of(" \t~^:?*[\\") != std::string::npos) {
        if (!name.empty()) info("A branch name can't contain spaces or any of ~ ^ : ? * [ \\");
        return;
    }
    // sgit's "branch" returns 1 even when it worked, so the ref is written here: a branch is
    // just a file holding the commit it points at
    if (!gitState) gitState = new GitState;
    git::Repo *r = repoFor(gitState, dir);
    std::string head = r ? r->resolve("HEAD") : "";
    if (head.empty()) { info("There is no commit to start a branch from yet."); return; }
    for (auto &b : r->branches())
        if (b == name) { info("There is already a branch called " + name + "."); return; }
    std::string refPath = joinPath(dir, ".git/refs/heads/" + name);
    for (size_t p = 0; (p = name.find('/', p)) != std::string::npos; p++) {   // "feature/x": make the drawers
        BPTR l = IDOS->CreateDir(joinPath(dir, ".git/refs/heads/" + name.substr(0, p)).c_str());
        if (l) IDOS->UnLock(l);
    }
    if (!writeFile(refPath, head + "\n")) { info("Couldn't create the branch (" + refPath + ")."); return; }
    gitPendingRefresh = "panel";
    gitRun(dir, { { "Switching to the new branch " + name, "checkout " + q(name) } }, "git checkout");
}

// After a checkout or discard: files that changed on disk are read again (unless edited).
void App::gitReloadOpenFiles()
{
    Document *was = cur;
    for (auto &dp : docs) {
        Document *d = dp.get();
        if (d->path.empty() || d->hex || d->readOnly || d->modified) continue;
        std::string text;
        if (!readFile(d->path, text)) continue;
        if (d != cur) activate(d);
        if (currentText(editor) == text) continue;
        intptr_t pos = S(editor, SCI_GETCURRENTPOS), first = S(editor, SCI_GETFIRSTVISIBLELINE);
        S(editor, SCI_SETTEXT, 0, (intptr_t)text.c_str());
        S(editor, SCI_EMPTYUNDOBUFFER);
        S(editor, SCI_SETSAVEPOINT);
        S(editor, SCI_GOTOPOS, std::min<intptr_t>(pos, S(editor, SCI_GETLENGTH)));
        S(editor, SCI_SETFIRSTVISIBLELINE, first);
        d->modified = false;
    }
    if (was && was != cur) activate(was);
    rebuildTabs();
    updateTitle();
}

// --- the shared hooks that belong to git ---

void App::integrationsGitDone(bool ok)
{
    bool fromPanel = gitPendingRefresh == "panel";
    gitPendingRefresh.clear();
    gitReloadOpenFiles();
    if (gitState) {
        gitState->headId.clear();          // a commit or checkout moves HEAD
        gitState->blamePath.clear();
        if (gitState->repo) { delete gitState->repo; gitState->repo = nullptr; gitState->repoDir.clear(); }   // new packs/refs
    }
    gitRefreshPanel();
    gitUpdateMarkers();
    if (blameOn) gitUpdateBlame();
    if (fromPanel && ok) {
        int p = bottomPageIndex("Git");
        if (p >= 0) showBottomPage(p);
    }
}

void App::integrationsTextChanged()
{
    if (!gitState) gitState = new GitState;
    gitState->markersDirty = true;
    gitState->dirtyAt = nowTicks();
}

bool App::integrationsMarginClick(int margin, int line)
{
    if (margin == MARGIN_BLAME) {
        if (!gitState || line < 0 || line >= (int)gitState->owner.size()) return true;
        int o = gitState->owner[line];
        std::string tip;
        if (o < 0) tip = "Not committed yet";
        else {
            const git::Commit &c = gitState->versions[o].commit;
            tip = "commit " + c.id + "\nAuthor: " + c.author + " <" + c.email + ">\nDate:   " + commitDate(c.time, c.tz) +
                  "\n\n" + c.message;
        }
        S(editor, SCI_CALLTIPSHOW, S(editor, SCI_POSITIONFROMLINE, line), (intptr_t)tip.c_str());
        return true;
    }
    if (margin == MARGIN_GIT) {
        // what the lines were in HEAD
        if (!gitState || !cur) return true;
        std::string dir = repoOf(cur->path), rel = relIn(dir, cur->path);
        auto it = gitState->headText.find(rel);
        if (it == gitState->headText.end()) return true;
        std::vector<std::string> now = splitLines(currentText(editor));
        for (auto &h : diffLines(it->second, now)) {
            if (line < h.bStart - 1 || line > h.bStart + std::max(h.bCount, 1) - 1) continue;
            std::string tip;
            if (h.aCount == 0) tip = "Added (not in HEAD)";
            else {
                tip = h.bCount == 0 ? "Deleted here; HEAD had:\n" : "Changed; HEAD had:\n";
                for (int k = 0; k < h.aCount && k < 15; k++) tip += it->second[h.aStart + k] + "\n";
                if (h.aCount > 15) tip += "...";
            }
            S(editor, SCI_CALLTIPSHOW, S(editor, SCI_POSITIONFROMLINE, line), (intptr_t)tip.c_str());
            break;
        }
        return true;
    }
    return false;
}

// Called from integrationsIdle(): markers (and blame) a second after editing stops.
void gitIdle(App *a)
{
    App::GitState *gs = a->gitState;
    if (gs && gs->markersDirty && nowTicks() - gs->dirtyAt > 40) {
        gs->markersDirty = false;
        a->gitUpdateMarkers();
        if (a->blameOn) a->gitUpdateBlame();
    }
    if (a->gitPanelWanted) {
        a->gitPanelWanted = false;
        a->gitRefreshPanel();
    }
}

void gitCleanup(App *a)
{
    delete a->gitState;
    a->gitState = nullptr;
    if (a->gitBranchLabels) {
        Object *ch = a->igad[GID_GIT_BRANCH - GID_INTEG_FIRST];
        if (ch && a->win) IIntuition->SetGadgetAttrs((struct Gadget *)ch, a->win, nullptr, CHOOSER_Labels, ~0UL, TAG_DONE);
        struct Node *n;
        while ((n = IExec->RemHead(a->gitBranchLabels))) IChooser->FreeChooserNode(n);
        delete a->gitBranchLabels;
        a->gitBranchLabels = nullptr;
    }
}

} // namespace af
