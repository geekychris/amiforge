// AmiForge - the application: main window, documents, project, build and debug.
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <set>
#include <stdint.h>

#include <exec/types.h>
#include <intuition/classusr.h>
#include <graphics/text.h>
#include <libraries/gadtools.h>

#include "sci/SciView.h"
#include "dbg/dbg.h"
#include "Snippets.h"
#include "Symbols.h"
#include <functional>
// --- debugtools ---
#include "InspectModel.h"
#include "LeakModel.h"
#include "ProfileModel.h"
// --- end debugtools ---

struct Window;
struct Screen;
struct DrawInfo;
struct Gadget;
struct List;
struct Node;
struct MsgPort;
struct IntuiMessage;
struct Hook;
struct ColumnInfo;
struct NewMenu;

namespace af {

// --- strings and files (Util.cpp) ---
std::string strprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
std::string trim(const std::string &s);
std::vector<std::string> splitWords(const std::string &s);   // shell-style, honours "quotes"
std::string fileName(const std::string &path);                // "dh1:src/main.c" -> "main.c"
std::string dirName(const std::string &path);                 // "dh1:src/main.c" -> "dh1:src"
std::string joinPath(const std::string &dir, const std::string &name);
std::string extension(const std::string &path);               // lower case, without the dot
std::string relativeTo(const std::string &dir, const std::string &path);
std::string absolutePath(const std::string &path);            // via Lock/NameFromLock; unchanged if it doesn't exist
bool readFile(const std::string &path, std::string &out);
bool writeFile(const std::string &path, const std::string &data);
bool fileExists(const std::string &path);
int64_t fileTime(const std::string &path);                    // modification time in ticks, -1 if missing
bool sameFile(const std::string &a, const std::string &b);
bool isSourceFile(const std::string &path);                   // compiles to an object: c cpp cc cxx s S asm
bool isCppFile(const std::string &path);
bool isAsmFile(const std::string &path);
bool sdkInstalled();                                          // SDK: exists (checked without a requester)

// --- project (Project.cpp) ---
struct Project {
    std::string path;           // the .afp file; empty = no project
    std::string dir;            // directory of the .afp; sources are relative to it
    std::string name = "untitled";
    std::string output = "program";
    std::string cc = "gcc", cxx = "g++";
    std::string cflags = "-O0 -gstabs -Wall";
    std::string cxxflags = "-O0 -gstabs+ -Wall";
    std::string asflags = "-gstabs";
    std::string ldflags = "";
    std::string libs = "-lauto";
    std::string objdir = "obj";
    std::string args;
    std::string console;        // empty = default CON: window
    bool stopAtMain = true;
    bool trackLeaks = false;    // --- debugtools: Debug > Track Leaks ---
    // --- designer ---
    std::string runcmd;         // Run runs this (in the project's drawer) instead of the program
    std::string author, version = "1.0", shortDesc, aminetType = "util/misc", description;
    std::string releaseFlags;   // empty = the debug flags without -g, -O0 -> -O2
    // --- end designer ---
    // --- integrations ---
    bool distcc = false;        // compile with distcc (Build > Distributed Builds)
    std::string distccHosts;    // empty: ENV:AmiForge/DistccHosts
    // --- end integrations ---
    std::vector<std::string> files;    // relative to dir
    std::vector<std::string> watches;
    struct Bp { std::string file; int line; bool enabled; std::string cond; };
    std::vector<Bp> breakpoints;       // file relative to dir

    bool load(const std::string &afp, std::string &err);
    bool save(std::string &err);
    std::string abs(const std::string &rel) const { return joinPath(dir, rel); }
    std::string rel(const std::string &absPath) const { return relativeTo(dir, absPath); }
    std::string outputPath() const { return abs(output); }
    bool usesCpp() const;
    bool contains(const std::string &absPath) const;
};

// --- build (Builder.cpp) ---
enum class MsgKind { Info, Command, Output, Note, Warning, Error, Done };
struct BuildLine {
    MsgKind kind;
    std::string text;
    std::string file;   // absolute, if the line refers to a source location
    int line = 0, col = 0;
};
class Builder {
public:
    Builder();
    ~Builder();
    // Start building `targets` ("build", "rebuild", "clean", "compile:<file>"). Lines arrive via poll().
    bool start(const Project &p, const std::string &what);
    // Run arbitrary commands (label, command line) in dir, reporting like a build.
    bool startCommands(const std::string &dir, const std::vector<std::pair<std::string, std::string>> &cmds);
    bool busy() const { return running; }
    void stop();
    // --- hotpatch
    static std::string compileCommand(const Project &p, const std::string &src, const std::string &obj);
    static std::string objectPath(const Project &p, const std::string &src);
    uint32_t signalMask() const;
    bool poll(BuildLine &out);
    int errors = 0, warnings = 0;
    int stepFailures = 0;   // failed commands and other build-level errors
    bool lastOk = false;
    struct Job;
private:
    Job *job = nullptr;
    bool running = false;
    int8_t sigBit = -1;
};
bool parseDiagnostic(const std::string &text, const std::string &cwd, BuildLine &out);

// --- AutoDocs (AutoDocs.cpp) ---
struct DocEntry {
    std::string full;   // "exec.library/AllocVecTagList", "exec.library/debug/AddDebugHook"
    std::string func;   // "AllocVecTagList"
    std::string lib;    // "exec.library"
    std::string file;   // the .doc file
};

// --- documents ---
struct Document {
    std::string path;           // absolute; empty for untitled
    std::string title;          // tab label
    void *sciDoc = nullptr;     // Scintilla document (SCI_CREATEDOCUMENT)
    bool modified = false;
    int untitledNo = 0;
    struct Node *tabNode = nullptr;
    std::string tabText;        // the clicktab keeps a pointer to it
    // view state, saved when switching away
    intptr_t firstLine = 0, caret = 0, anchor = 0, xOffset = 0;
    // --- integrations ---
    std::string lexerName;      // a file name to pick the lexer by when path doesn't ("x.diff")
    bool hex = false;           // a hex editor view of `bytes` (HexEditor.cpp)
    std::string bytes;
    std::vector<std::pair<size_t, int>> hexUndo;   // offset, old byte (-1: was appended)
    int hexNibble = 0;
    bool readOnly = false;      // diffs and other generated text
    // --- end integrations ---
};

// --- listbrowser helper (Gui.cpp) ---
class ListView {
public:
    explicit ListView(int columns = 1);
    ~ListView();
    Object *create(uint32_t gid, const char *const *titles = nullptr, const int *weights = nullptr,
                   bool hierarchical = false, bool checkboxes = false, struct TextAttr *font = nullptr);
    void detach();                      // before modifying the list
    void attach(int selected = -1);     // after; refreshes the gadget
    void clear();                       // detach + free all nodes + attach
    struct Node *add(const std::vector<std::string> &cols, void *userData = nullptr,
                     int generation = 0, uint32_t flags = 0, bool checked = false, struct Node *after = nullptr);
    void setPens(struct Node *n, int fg);   // highlight one row
    int count() const;
    struct Node *nodeAt(int index) const;
    int indexOf(struct Node *n) const;
    void *userData(struct Node *n) const;
    struct Node *selected() const;
    void makeVisible(int index);
    void select(int index);
    Object *obj = nullptr;
    struct Window *win = nullptr;
    struct List *list;
    struct ColumnInfo *ci = nullptr;
    int cols;
    bool hierarchical = false;
private:
    int detachDepth = 0;
};

// --- the app ---
enum Gid {
    GID_ROOT = 1, GID_TOOLBAR,
    GID_TB_NEW, GID_TB_OPEN, GID_TB_SAVE, GID_TB_BUILD, GID_TB_RUN, GID_TB_DEBUG,
    GID_TB_CONTINUE, GID_TB_OVER, GID_TB_INTO, GID_TB_OUT, GID_TB_STOP, GID_TB_FIND, GID_TB_DOCS,
    GID_PROJECT, GID_TABS, GID_EDITOR, GID_VSCROLL, GID_HSCROLL,
    GID_SIDE_TABS, GID_SIDE_PAGES, GID_LOCALS, GID_WATCHES, GID_REGS, GID_WATCH_STR, GID_WATCH_ADD, GID_WATCH_DEL,
    GID_BOTTOM_TABS, GID_BOTTOM_PAGES, GID_BUILDLOG, GID_FINDLOG, GID_STACK, GID_BPS, GID_BP_DEL, GID_BP_DELALL,
    GID_MEM_ADDR, GID_MEM_VIEW, GID_DISASM, GID_DEBUGLOG, GID_STATUS, GID_STATUS2,
    // --- editor features ---
    GID_LEFT_TABS, GID_LEFT_PAGES, GID_SYMBOLS,
    // --- debugtools ---
    GID_INSPECT, GID_INSPECT_REFRESH, GID_INSPECT_HIGHLIGHT, GID_LEAKS, GID_PROFILE, GID_PROFILE_CLEAR,
    // --- end debugtools ---
    GID_LAST
};
// --- integrations --- gadget ids of the Git tab (kept in App::igad, outside gad[])
enum IntegGid {
    GID_INTEG_FIRST = 1000,
    GID_GIT_BRANCH = GID_INTEG_FIRST, GID_GIT_NEWBRANCH, GID_GIT_REFRESH, GID_GIT_FILES, GID_GIT_STAGE,
    GID_GIT_UNSTAGE, GID_GIT_DISCARD, GID_GIT_DIFF, GID_GIT_COMMIT, GID_GIT_BLAME, GID_GIT_STATUS,
    GID_INTEG_LAST
};

class App : public SciViewHost {
public:
    App();
    ~App();
    int run(int argc, char **argv);

    // SciViewHost
    void sciNotify(SciView *view, const Scintilla::NotificationData &n) override;
    void sciScrollInfo(SciView *view, bool vertical, int pos, int visible, int total) override;
    void sciWantFocus(SciView *view) override;
    void sciUnhandledKey(SciView *view, uint16_t rawkey, uint16_t qualifier) override;
    void sciContextMenu(SciView *view, int x, int y) override;

    // --- window (MainWindow.cpp) ---
    bool openMainWindow();
    void closeMainWindow();
    void eventLoop();
    void handleGadget(uint32_t gid, uint16_t code);
    void handleMenu(uint32_t id);
    bool handleKey(uint16_t rawkey, uint16_t qual);
    uint32_t idcmpHook(struct IntuiMessage *im);
    void layoutEditor(bool reblit = true);   // place the editor on its placeholder's box
    void setStatus(const std::string &left);
    void updateTitle();
    void updateToolbar();
    void showBottomPage(int page);
    void showSidePage(int page);
    void setGadget(uint32_t gid, uint32_t tag, uint32_t value);
    std::string getString(uint32_t gid);
    void busyPointer(bool busy);

    // --- documents (Editor.cpp) ---
    Document *openFile(const std::string &path, bool activate = true);
    Document *newDocument();
    Document *findDocument(const std::string &path);
    bool saveDocument(Document *d, bool saveAs = false);
    bool saveAll();
    bool closeDocument(Document *d);   // asks about unsaved changes; false if cancelled
    bool closeAll();
    void activate(Document *d);
    void rebuildTabs();
    void gotoLocation(const std::string &file, int line, int col = 0, bool select = true);
    void updateCaretStatus();
    void markErrors();
    void clearMarkers(int marker);
    Document *current() { return cur; }
    void findNext(bool backwards);
    void replaceOne();
    int replaceAll();
    void gotoLineDialog();
    void toggleBreakpointAtCaret();
    void toggleBreakpoint(Document *d, int line);   // 1-based
    void syncBreakpointMarkers(Document *d);
    void autoIndent(int ch);
    void refreshMarkers();                   // breakpoints, execution line, diagnostics for the current document
    void harvestBreakpoints(Document *d);    // marker positions (which move with edits) back into the project
    void toggleComment();
    void setBreakpointEnabled(int index, bool on);
    void syncDebuggerBreakpoints();

    // --- project (ProjectUI.cpp) ---
    void newProject();
    void openProject(const std::string &path = "");
    bool saveProject();
    void closeProject();
    void addFilesToProject();
    void removeFileFromProject();
    void refreshProjectList();
    void projectSettings();
    void findInFiles();
    // AutoDocs (AutoDocs.cpp)
    bool loadDocIndex();
    const DocEntry *findDoc(const std::string &word);
    void openDocWindow();
    void closeDocWindow();
    void layoutDocView();
    void showDocMatches(const std::string &filter);
    void showDoc(const DocEntry *e, bool remember);
    void lookupDoc(const std::string &word);
    void handleDocWindow();
    void docForWordAtCaret();
    void docLinkClicked(intptr_t position);
    void openSdkDocs();
    // navigation (Navigate.cpp)
    std::string resolveInclude(const std::string &name, bool quoted, const std::string &fromFile);
    std::string wordAt(intptr_t pos);
    bool openIncludeAt(intptr_t pos);
    bool gotoDefinition(const std::string &name);
    void ctrlClick(intptr_t pos);
    void showContextMenu();
    void showShortcuts();
    void queueCommand(uint32_t cmd);
    intptr_t pendingClickPos = -1;
    bool swallowSelectUp = false;
    std::string docEntryText(const DocEntry &e);
    // completion (Completion.cpp)
    void refreshCompletionWords();
    void showCompletion(bool explicitRequest);
    void showCallTip();
    std::string docPrototype(const DocEntry &e, const std::string &name);
    void completionCharAdded(int ch);
    std::map<std::string, std::pair<int64_t, std::vector<std::string>>> wordCache;
    // scopes (Scope.cpp)
    const std::string &codeText(const std::string &path);
    std::vector<std::string> scopeSources();
    void scopeMembers(const std::string &scope, std::set<std::string> &out);
    std::string typeOfVariable(const std::string &var, intptr_t line);
    void typeMembers(const std::string &type, std::set<std::string> &out);
    int scopeAt(intptr_t start, std::string &name);
    void signatures(const std::string &name, const std::string &className, bool constructor, std::vector<std::string> &out);
    void updateCallTipHighlight();
    intptr_t callTipParen = -1;       // position of the "(" the calltip is for
    std::string callTipFirst;         // its first signature (for highlighting the argument)
    std::map<std::string, std::pair<int64_t, std::string>> codeCache;
    const std::vector<std::string> &includesOf(const std::string &path);
    std::map<std::string, std::pair<int64_t, std::vector<std::string>>> includeCache;
    void setGadgetIn(struct Window *w, Object *g, uint32_t tag, uint32_t value);
    std::vector<DocEntry> docIndex;
    Object *docWinObj = nullptr;
    struct Window *docWin = nullptr;
    Object *docGad[8] = {};
    ListView docList{2};
    SciView *docView = nullptr;
    std::vector<std::string> docHistory;
    std::string docCurrent;
    intptr_t pendingDocLink = -1;
    // git (Git.cpp)
    std::string gitRepoDir();
    bool gitRun(const std::string &dir, const std::vector<std::pair<std::string, std::string>> &cmds, const std::string &title);
    void gitStatus();
    void gitDiff();
    void gitLog();
    void gitCommit();
    void gitPushPull(bool push);
    void gitClone();
    void gitAfterClone(bool ok);
    void gitSetup();
    bool gitBusy = false;
    std::string pendingCloneDir;
    // recent projects (ENVARC:AmiForge/RecentProjects, newest first)
    void loadRecent();
    void saveRecent();
    void addRecent(const std::string &afp);
    void rebuildMenus();
    std::vector<std::string> recentProjects;
    std::vector<struct NewMenu> menuData;     // window.class keeps a pointer to it
    std::vector<std::string> menuLabels;      // and to these

    // --- build & run (BuildUI.cpp) ---
    void build(const std::string &what);
    void pollBuild();
    void runProgram();
    bool buildBeforeRun(int afterAction);
    int pendingAfterBuild = 0;   // 1 run, 2 debug

    // --- debug (DebugUI.cpp) ---
    void debugStart();
    void debugContinue();
    void debugStep(int kind);    // 0 over, 1 into, 2 out, 3 instruction
    void debugStop();
    void debugPause();
    void debugRunToCursor();
    void pollDebugger();
    void onStopped(const dbg_event &ev);
    void refreshDebugPanels();
    void refreshLocals();
    void refreshWatches();
    void refreshRegisters();
    void refreshStack();
    void refreshBreakpointList();
    void refreshDisassembly();
    void refreshMemory();
    void expandVar(ListView &lv, struct Node *n);
    void addWatch(const std::string &expr);
    void removeWatch();
    void selectFrame(int level);
    void clearExecutionMarkers();
    void debugLog(const std::string &s, int pen = -1);
    bool debugging() const { return dbg && dbg_get_state(dbg) != DBG_NONE && dbg_get_state(dbg) != DBG_EXITED; }
    bool stopped() const { return dbg && dbg_get_state(dbg) == DBG_STOPPED; }

    // --- debugtools (DebugTools.cpp): Intuition inspector, leak tracker, profiler ---
    enum { BOTTOM_WINDOWS = 7, BOTTOM_LEAKS = 8, BOTTOM_PROFILE = 9 };   // bottom panel pages
    void dtBeforeRun();                 // debugStart, before dbg_run: switch tracking/profiling on
    void dtPoll();                      // drain the debugger's leak events and profile samples
    void dtSessionEnded();              // the program has ended (before dbg_unload): build the reports
    void startProfiling();              // Debug > Profile
    void toggleTrackLeaks();
    void refreshInspector();
    void inspectorClicked();
    void highlightWindow(uint32_t window, int gadget);
    void refreshLeaks();
    void leakClicked();
    void refreshProfile();
    void profileClicked();
    void applyProfileAnnotations();     // end-of-line heat on the current document
    void clearProfile();
    CodeLoc resolveCode(uint32_t addr);
    ListView inspectView{2}, leaksView{3}, profileView{5};
    LeakModel leakModel;
    ProfileModel profileModel;
    bool profileNext = false, profileRun = false, trackRun = false;
    InspectSnapshot inspectSnap;
    std::vector<InspRow> inspectRowList;
    std::set<uint32_t> screensAtStart;
    struct NavTarget { std::string file; int line = 0; };
    std::vector<NavTarget> leakTargets, profileTargets;      // per row (userData - 1)
    std::map<std::string, std::map<int, std::string>> profileNotes;   // absolute file -> line -> note
    std::set<void *> annotatedDocs;     // Scintilla documents with our annotations
    // --- end debugtools ---

    // --- dialogs (Dialogs.cpp) ---
    std::string requestFile(const char *title, bool save, const std::string &initial = "", const char *pattern = nullptr);
    std::vector<std::string> requestFiles(const char *title, const std::string &drawer);
    std::string requestDrawer(const char *title, const std::string &initial);
    int ask(const char *title, const std::string &body, const char *buttons);   // 1..n left to right, 0 = last
    bool askString(const char *title, const std::string &body, std::string &value);
    bool askInteger(const char *title, const std::string &body, int &value, int min, int max);
    void info(const std::string &body) { ask("AmiForge", body, "_OK"); }
    void openFindWindow(bool replace);
    void closeFindWindow();
    void handleFindWindow();
    void about();
    bool editProjectSettings();
    void preferences();

    // state
    struct Window *win = nullptr;
    Object *winObj = nullptr;
    Object *gad[GID_LAST] = {};
    struct MsgPort *appPort = nullptr;
    struct MsgPort *idcmpPort = nullptr;
    SciView *editor = nullptr;
    std::vector<std::unique_ptr<Document>> docs;
    Document *cur = nullptr;
    int untitledCounter = 0;
    Project project;
    Builder builder;
    std::vector<BuildLine> diagnostics;      // the last build's output, one per build log row
    std::vector<BuildLine> findResults;      // Find in Files hits, one per find log row
    int penError = -1, penWarning = -1, penNote = -1, penDim = -1, penChanged = -1;
    std::vector<std::pair<dbg_var **, int>> varAllocs;   // locals/watches/children to free on the next refresh
    std::vector<std::pair<std::string, std::string>> lastValues;   // "panel/name" -> value, to highlight changes
    bool ensureProject();               // a project, or an implicit one for the current file
    dbg_session *dbg = nullptr;
    ListView projectList{1}, buildLog{1}, findLog{2}, locals{3}, watches{3}, regs{2}, stack{3},
             bpList{3}, memView{1}, disasm{1}, debugLogView{1};
    struct List *tabLabels = nullptr, *sideLabels = nullptr, *bottomLabels = nullptr, *leftLabels = nullptr;
    Object *tabCloseImage = nullptr;    // close gadget on the active file tab
    struct DrawInfo *drawInfo = nullptr;
    struct Screen *drawInfoScreen = nullptr;
    struct TextAttr monoAttr;
    struct TextFont *monoFont = nullptr;
    bool quit = false;
    bool editorFocus = true;
    int fontSize = 13;
    std::string lastDir = "RAM:";
    std::string findText, replaceText;
    bool findCase = false, findWord = false, findRegex = false;
    Object *findWinObj = nullptr;
    struct Window *findWin = nullptr;
    Object *findGad[16] = {};
    std::string statusLeft;
    int selectedFrame = 0;
    uint32_t memAddr = 0;
    dbg_regs lastRegs = {};
    bool haveLastRegs = false;
    std::vector<dbg_var *> localVars, watchVars;
    std::string runCmdOutput;
    std::string execFile;           // where the program is stopped (top frame), absolute
    int execLine = 0;
    std::string frameFile;          // the selected stack frame, if not the top one
    int frameLine = 0;
    std::vector<int> dbgBpIds;      // dbg ids, parallel to project.breakpoints (-1 = not set)
    // --- hotpatch: Edit and Continue (HotPatch.cpp)
    void applyChanges();
    void hotAfterCompile(bool ok);
    bool hotBusy = false;
    std::vector<std::string> hotFiles;            // sources being recompiled (project-relative)
    std::map<std::string, int64_t> hotApplied;    // source (absolute) -> its time when last applied
    void hotReset() { hotApplied.clear(); }
    bool autoIndentEnabled = false;   // SciView auto-indents (AF_SCI_SETAUTOINDENT)
    bool wrapLines = false, showWhitespace = false;

    // --- editor features (EditorFeatures.cpp): themes, split view, minimap, multiple cursors,
    // snippets, symbol outline, Find All References, Rename Symbol ---
    void initEditorFeatures();                 // after the main window opened
    void closeEditorFeatures();                // before it closes
    // views: `editor` is the active one (mainView or splitView); mainView owns the scrollers
    SciView *mainView = nullptr, *splitView = nullptr, *miniView = nullptr;
    bool splitOn = false, minimapOn = false;
    void toggleSplit();
    void toggleMinimap();
    void syncViews();                          // the other views follow the current document
    void styleView(SciView *v);                // theme + view settings for the current document
    bool layoutViews(int left, int top, int width, int height, bool reblit);   // true if a view moved
    SciView *viewAt(int x, int y);             // an edit view (or the minimap) under a window point
    void setActiveView(SciView *v);
    void tickViews();
    void updateMinimap();
    void minimapMouse(const struct IntuiMessage *im);
    void minimapWheel(int lines);
    bool miniDragging = false;
    bool editorIdcmp(struct IntuiMessage *im, uint32_t &result);   // view routing in the IDCMP hook
    // themes (ENVARC:AmiForge/Theme)
    void setTheme(const std::string &name);
    void styleDocView();                       // AutoDocs.cpp
    std::string themeName = "Light";
    // multiple cursors
    void addNextOccurrence(bool all);
    bool altClick = false;
    // snippets (built in + ENVARC:AmiForge/Snippets)
    std::vector<Snippet> snippets;
    void loadSnippets();
    void editSnippets();
    void insertSnippetMenu();
    bool snippetTab(bool backwards);           // Tab in the editor: true when a snippet used it
    void insertSnippet(const Snippet &s, intptr_t from, intptr_t to);
    void snippetGoto(int step);
    void endSnippet();
    void paintSnippetFields();
    void addSnippetWords(const std::string &prefix, std::set<std::string> &words);
    bool snippetCompletion(const char *text, intptr_t wordStart);   // SCN_AUTOCSELECTION
    struct SnippetSession {
        bool active = false;
        Document *doc = nullptr;
        std::vector<SnippetField> fields;      // document positions
        std::vector<size_t> order;             // tab order (indices into fields)
        int at = 0;
    } snip;
    void editorModified(const Scintilla::NotificationData &n);   // SCN_MODIFIED of the current document
    // the symbol outline (left panel, Symbols tab)
    ListView symbolList{2};
    std::vector<Symbol> outline;
    int outlineDelay = 0;                      // INTUITICKS until the outline is re-read after an edit
    void refreshOutline();
    void outlineClicked();
    // references and renaming
    void findReferences();
    void renameSymbol();
    void clearOccurrences();
    bool occurrencesShown = false;
    std::string documentText(Document *d);
    void withDocument(Document *d, const std::function<void()> &fn);   // edit/read another open document
    bool batchEditing = false;
    std::vector<std::string> sourceFilesForSearch();   // project files + other open files
    // --- designer ---
    // project templates (TemplatesUI.cpp)
    void newProjectFromTemplate();
    bool createFromTemplate(const std::string &templateDir, const std::string &name, const std::string &drawer,
                            std::string &afpOut, std::string &err, std::vector<std::string> *openOut = nullptr);
    bool generateGuiFiles(const std::string &afg, std::string &cFile, std::string &hFile, std::string &err);
    // GUI designer (Designer.cpp)
    void openDesigner(const std::string &afg = "");
    void closeDesigner();
    uint32_t designerSignals();
    void handleDesigner(uint32_t got);
    class Designer *designer = nullptr;
    // release packaging (ReleaseUI.cpp)
    void packageRelease();
    void continuePackaging(bool buildOk);
    bool releaseDialog();
    struct ReleaseJob *releaseJob = nullptr;
    // --- end designer ---
    // --- integrations (Git panel, hex/icon/image editors, ARexx, distcc, explanations) ---
    // hooks called from the shared code
    void integrationsInit();                     // after the main window opened
    void integrationsCleanup();                  // before it closes
    uint32_t integrationsSignals();              // extra signals for the event loop
    void integrationsHandle(uint32_t got);       // after Wait()
    void integrationsIdle();                     // each pass of the event loop
    void integrationsActivated(Document *d);     // a document became the current one
    void integrationsTextChanged();              // the current document's text changed
    void integrationsCaretMoved();
    void integrationsSaved(Document *d);
    bool integrationsGadget(uint32_t gid, uint16_t code);   // true if it was one of ours
    bool integrationsCommand(uint32_t cmd);      // true if handled (menu commands, overrides for hex docs)
    bool integrationsKey(struct IntuiMessage *im);          // RAWKEY for special documents
    bool integrationsOpen(const std::string &path, const std::string &data, Document *&doc);   // non-text files
    bool integrationsMarginClick(int margin, int line);
    void integrationsBuildLine(const BuildLine &l, int index);   // a new build log row
    void integrationsGitDone(bool ok);           // a git command started by the panel finished
    void integrationsCrash(const dbg_event &ev); // the debugger stopped on an exception
    int bottomPageIndex(const char *name);       // index of a bottom tab by its label
    // git panel (GitPanel.cpp)
    struct GitState;
    GitState *gitState = nullptr;
    ListView gitFiles{2};
    Object *igad[24] = {};                       // our gadgets, by (gid - GID_INTEG_FIRST)
    std::vector<std::string> gitBranchNames;
    struct List *gitBranchLabels = nullptr;
    bool blameOn = false;
    bool gitPanelWanted = false;                 // refresh the Git tab when idle
    Object *createGitPage();
    void gitRefreshPanel();
    void gitUpdateMarkers();
    void gitUpdateBlame();
    void gitApplyMargins();
    void gitPanelAction(int what);
    void gitSwitchBranch(int index);
    void gitNewBranch();
    void gitShowFileDiff(const std::string &repoRel, bool staged);
    void gitReloadOpenFiles();
    void gitBlameStatus(int line);
    std::string gitPendingRefresh;
    // hex editor (HexEditor.cpp)
    Document *openHex(const std::string &path, const std::string &data);
    bool hexKey(struct IntuiMessage *im);
    void hexRender(Document *d, intptr_t caretOffset, int area, int nibble);
    void hexGoto();
    void hexFind(bool again);
    void hexUndoLast();
    void hexHighlightTwin();
    std::string hexLastFind;
    // icon editor and image viewer (ResourceEditors.cpp)
    struct ResourceWindow;
    std::vector<ResourceWindow *> resourceWindows;
    void openIconEditor(const std::string &path);
    void openImageViewer(const std::string &path);
    void closeResourceWindow(ResourceWindow *w);
    // ARexx (RexxPort.cpp)
    Object *rexxObj = nullptr;
    std::string rexxResult;                      // RESULT of the last command (arexx.class keeps the pointer)
    std::vector<std::string> macroFiles;         // Macros menu entries
    bool openRexxPort();
    void closeRexxPort();
    std::string rexxCommand(const std::string &line, int &rc);
    void runMacro(const std::string &path);
    void scanMacros();
    // distcc (Build > Distributed Builds...)
    void distccSettings();
    // explanations (ExplainUI.cpp)
    std::vector<std::string> explanations;       // per build log row index (empty: none)
    void explainRow(int index);
    std::string lastCrashExplanation;
    // --- end integrations ---
};

extern App *gApp;

} // namespace af
