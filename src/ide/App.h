// AmiForge - the application: main window, documents, project, build and debug.
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <stdint.h>

#include <exec/types.h>
#include <intuition/classusr.h>
#include <graphics/text.h>

#include "sci/SciView.h"
#include "dbg/dbg.h"

struct Window;
struct Gadget;
struct List;
struct Node;
struct MsgPort;
struct IntuiMessage;
struct Hook;
struct ColumnInfo;

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
    bool busy() const { return running; }
    void stop();
    uint32_t signalMask() const;
    bool poll(BuildLine &out);
    int errors = 0, warnings = 0;
    bool lastOk = false;
    struct Job;
private:
    Job *job = nullptr;
    bool running = false;
    int8_t sigBit = -1;
};
bool parseDiagnostic(const std::string &text, const std::string &cwd, BuildLine &out);

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
    GID_TB_CONTINUE, GID_TB_OVER, GID_TB_INTO, GID_TB_OUT, GID_TB_STOP, GID_TB_FIND,
    GID_PROJECT, GID_TABS, GID_EDITOR, GID_VSCROLL, GID_HSCROLL,
    GID_SIDE_TABS, GID_SIDE_PAGES, GID_LOCALS, GID_WATCHES, GID_REGS, GID_WATCH_STR, GID_WATCH_ADD, GID_WATCH_DEL,
    GID_BOTTOM_TABS, GID_BOTTOM_PAGES, GID_BUILDLOG, GID_FINDLOG, GID_STACK, GID_BPS, GID_BP_DEL, GID_BP_DELALL,
    GID_MEM_ADDR, GID_MEM_VIEW, GID_DISASM, GID_DEBUGLOG, GID_STATUS, GID_STATUS2,
    GID_LAST
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
    void layoutEditor();
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

    // --- dialogs (Dialogs.cpp) ---
    std::string requestFile(const char *title, bool save, const std::string &initial = "", const char *pattern = nullptr);
    std::vector<std::string> requestFiles(const char *title, const std::string &drawer);
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
    struct List *tabLabels = nullptr, *sideLabels = nullptr, *bottomLabels = nullptr;
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
    bool autoIndentEnabled = false;   // SciView auto-indents (AF_SCI_SETAUTOINDENT)
    bool wrapLines = false, showWhitespace = false;
};

extern App *gApp;

} // namespace af
