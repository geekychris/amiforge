// Command dispatch for menus, toolbar buttons and keys.
#include "App.h"
#include "Commands.h"

#include "Scintilla.h"

namespace af {

static inline intptr_t S(SciView *v, unsigned m, uintptr_t w = 0, intptr_t l = 0) { return v ? v->send(m, w, l) : 0; }

void App::handleMenu(uint32_t cmd)
{
    switch (cmd) {
    // project
    case CMD_NEW_PROJECT: newProject(); break;
    case CMD_OPEN_PROJECT: openProject(); break;
    case CMD_SAVE_PROJECT: saveProject(); break;
    case CMD_CLOSE_PROJECT: closeProject(); break;
    case CMD_ADD_FILES: addFilesToProject(); break;
    case CMD_REMOVE_FILE: removeFileFromProject(); break;
    case CMD_PROJECT_SETTINGS: projectSettings(); break;
    case CMD_ABOUT: about(); break;
    case CMD_CLEAR_RECENT:
        recentProjects.clear();
        saveRecent();
        rebuildMenus();
        break;
    case CMD_QUIT:
        if (debugging() && ask("Quit", "A program is being debugged. Stop it and quit?", "_Quit|_Cancel") != 1) break;
        if (debugging()) debugStop();
        if (!project.path.empty()) saveProject();
        if (closeAll()) quit = true;
        break;

    // file
    case CMD_NEW_FILE: newDocument(); break;
    case CMD_OPEN_FILE: {
        for (auto &f : requestFiles("Open File", project.path.empty() ? lastDir : project.dir)) openFile(f);
        break;
    }
    case CMD_SAVE: saveDocument(cur); break;
    case CMD_SAVE_AS: saveDocument(cur, true); break;
    case CMD_SAVE_ALL: saveAll(); break;
    case CMD_CLOSE: closeDocument(cur); break;

    // edit
    case CMD_UNDO: S(editor, SCI_UNDO); break;
    case CMD_REDO: S(editor, SCI_REDO); break;
    case CMD_CUT: S(editor, SCI_CUT); break;
    case CMD_COPY: S(editor, SCI_COPY); break;
    case CMD_PASTE: S(editor, SCI_PASTE); break;
    case CMD_SELECT_ALL: S(editor, SCI_SELECTALL); break;
    case CMD_FIND: openFindWindow(false); break;
    case CMD_REPLACE: openFindWindow(true); break;
    case CMD_FIND_NEXT: findNext(false); break;
    case CMD_FIND_PREV: findNext(true); break;
    case CMD_FIND_IN_FILES: findInFiles(); break;
    case CMD_GOTO_LINE: gotoLineDialog(); break;
    case CMD_COMMENT: toggleComment(); break;
    case CMD_INDENT: S(editor, SCI_TAB); break;
    case CMD_UNINDENT: S(editor, SCI_BACKTAB); break;

    // view
    case CMD_ZOOM_IN: S(editor, SCI_ZOOMIN); break;
    case CMD_ZOOM_OUT: S(editor, SCI_ZOOMOUT); break;
    case CMD_ZOOM_RESET: S(editor, SCI_SETZOOM, 0); break;
    case CMD_WRAP:
        wrapLines = !wrapLines;
        S(editor, SCI_SETWRAPMODE, wrapLines ? SC_WRAP_WORD : SC_WRAP_NONE);
        break;
    case CMD_WHITESPACE:
        showWhitespace = !showWhitespace;
        S(editor, SCI_SETVIEWWS, showWhitespace ? SCWS_VISIBLEALWAYS : SCWS_INVISIBLE);
        S(editor, SCI_SETVIEWEOL, showWhitespace);
        break;
    case CMD_FOLD_ALL: S(editor, SCI_FOLDALL, SC_FOLDACTION_CONTRACT); break;
    case CMD_UNFOLD_ALL: S(editor, SCI_FOLDALL, SC_FOLDACTION_EXPAND); break;

    // build
    case CMD_COMPILE:
        if (cur && !cur->path.empty()) build("compile:" + cur->path);
        break;
    case CMD_BUILD: build("build"); break;
    case CMD_REBUILD: build("rebuild"); break;
    case CMD_CLEAN: build("clean"); break;
    case CMD_STOP_BUILD: builder.stop(); break;
    case CMD_RUN: buildBeforeRun(1); break;
    case CMD_NEXT_ERROR:
    case CMD_PREV_ERROR: {
        // walk the diagnostics that point at a file, wrapping around
        static int at = -1;
        int n = (int)diagnostics.size();
        for (int k = 0; k < n; k++) {
            at = cmd == CMD_NEXT_ERROR ? (at + 1) % n : (at - 1 + n) % n;
            auto &d = diagnostics[at];
            if (!d.file.empty() && d.line > 0 && (d.kind == MsgKind::Error || d.kind == MsgKind::Warning)) {
                gotoLocation(d.file, d.line, d.col);
                buildLog.select(at);
                setStatus(d.text);
                break;
            }
        }
        break;
    }

    // debug
    case CMD_DEBUG_START:
        if (stopped()) debugContinue();
        else if (!debugging()) buildBeforeRun(2);
        break;
    case CMD_DEBUG_STOP: debugStop(); break;
    case CMD_DEBUG_PAUSE: debugPause(); break;
    case CMD_STEP_OVER: debugStep(0); break;
    case CMD_STEP_INTO: debugStep(1); break;
    case CMD_STEP_OUT: debugStep(2); break;
    case CMD_STEP_INSN: debugStep(3); break;
    case CMD_RUN_TO_CURSOR: debugRunToCursor(); break;
    case CMD_TOGGLE_BP: toggleBreakpointAtCaret(); break;
    case CMD_CLEAR_BPS:
        if (!project.breakpoints.empty() && ask("Breakpoints", "Delete all breakpoints?", "_Delete|_Cancel") == 1) {
            project.breakpoints.clear();
            syncDebuggerBreakpoints();
            refreshMarkers();
            refreshBreakpointList();
        }
        break;
    case CMD_ADD_WATCH: {
        std::string e;
        if (cur) {
            intptr_t a = S(editor, SCI_GETSELECTIONSTART), b = S(editor, SCI_GETSELECTIONEND);
            if (b > a && b - a < 200) {
                std::vector<char> buf(b - a + 1);
                S(editor, SCI_GETSELTEXT, 0, (intptr_t)buf.data());
                e = buf.data();
            }
        }
        if (askString("Add Watch", "Expression to watch:", e) && !trim(e).empty()) addWatch(trim(e));
        break;
    }
    // git
    case CMD_GIT_STATUS: gitStatus(); break;
    case CMD_GIT_DIFF: gitDiff(); break;
    case CMD_GIT_LOG: gitLog(); break;
    case CMD_GIT_COMMIT: gitCommit(); break;
    case CMD_GIT_PULL: gitPushPull(false); break;
    case CMD_GIT_PUSH: gitPushPull(true); break;
    case CMD_GIT_CLONE: gitClone(); break;
    case CMD_GIT_SETUP: gitSetup(); break;

    // help
    case CMD_DOC_LOOKUP: docForWordAtCaret(); break;
    case CMD_DOC_BROWSER: openDocWindow(); break;
    case CMD_SDK_DOCS: openSdkDocs(); break;

    default:
        if (cmd >= CMD_RECENT_FIRST && cmd <= CMD_RECENT_LAST) {
            size_t i = cmd - CMD_RECENT_FIRST;
            if (i < recentProjects.size()) {
                std::string p = recentProjects[i];
                if (fileExists(p)) openProject(p);
                else {
                    info("The project " + p + " isn't there any more; it's been taken off the list.");
                    recentProjects.erase(recentProjects.begin() + i);
                    saveRecent();
                    rebuildMenus();
                }
            }
        }
        break;
    }
    updateToolbar();
}

} // namespace af
