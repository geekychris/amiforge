// --- basic --- Boing BASIC projects in AmiForge: the parts without OS calls, so the host tests
// check them.
//
//  - Controls: per designer object kind (GuiModel.h), the BASIC properties, methods and events,
//    and the default event a double-click opens. Built in, and replaced by Boing BASIC's
//    docs/forms-events.md when the IDE finds it (parseFormsEvents).
//  - Code-behind: a form main.afg has its code in main.bas. Finding the procedures in it,
//    the event subs (<control>_<Event>, Form_<Event> for the window), adding an empty one,
//    renaming a control's subs.
//  - The outline (Subs, Functions, Properties, Types, Enums, Declares...).
//  - Project files (.bbp) and the boing command lines.
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "Symbols.h"

namespace af {
namespace basic {

// --- controls --------------------------------------------------------------------------------

struct Member {
    std::string name;        // "Text", "AddItem", "Click"
    std::string params;      // events and methods: "Cancel As Integer" (without parentheses)
    std::string type;        // properties: "String"; functions: the result type
    std::string doc;         // one line
};

struct ControlInfo {
    std::string kind;        // the .afg kind: "button", "string"... "window" for the form
    std::string basicType;   // what BASIC calls it: "Button", "TextBox", "Form"
    std::vector<Member> properties, methods, events;
    std::string defaultEvent;   // "Click"; "" if the kind has no events
    const Member *event(const std::string &name) const;   // any case
};

// The control table (the built-in one, or what setControls() put there).
const std::vector<ControlInfo> &controls();
const ControlInfo *controlFor(const std::string &kind);   // "" / unknown -> nullptr
// Read docs/forms-events.md (see parseFormsEvents); true if it had at least one control.
bool parseFormsEvents(const std::string &markdown, std::vector<ControlInfo> &out, std::string *err = nullptr);
void setControls(const std::vector<ControlInfo> &table);   // replaces the built-in one
void resetControls();                                      // back to the built-in one

// --- procedures ------------------------------------------------------------------------------

enum class ProcKind { Sub, Function, PropertyGet, PropertyLet, PropertySet };

struct Proc {
    ProcKind kind = ProcKind::Sub;
    std::string name;
    std::string params;      // as written, without the parentheses
    std::string scope;       // "Public", "Private", "Friend" or ""
    bool isStatic = false;
    int line = 0;            // 1-based: the header
    int endLine = 0;         // the End Sub line (0 if it's missing)
    int nameColumn = 0;      // 0-based column of the name on the header line
    bool hasCode = false;    // anything but blanks and comments between header and end
};

std::vector<Proc> procedures(const std::string &text);
int findProc(const std::vector<Proc> &procs, const std::string &name);   // any case; -1

// The event sub's name: ("ok", "Click") -> "ok_Click"; the window (isForm) -> "Form_Load".
std::string eventSubName(const std::string &controlId, const std::string &event, bool isForm);

// Where an event sub is, or where it was put: `text` gets an empty one added if it's missing
// ("Private Sub ok_Click()\n    \nEnd Sub", after the last procedure, with a blank line
// before it). `line` is 1-based, the line to put the cursor on (the first line of the body),
// `column` its indentation. Returns true if the text was changed.
struct SubPlace { int line = 0; int column = 0; bool created = false; };
bool ensureSub(std::string &text, const std::string &name, const std::string &params, SubPlace &where,
               int indentWidth = 4);

// The events of `controlId` that have a sub in `procs` ("Click" for ok_Click), any case.
std::set<std::string> eventsWithSubs(const std::vector<Proc> &procs, const std::string &controlId,
                                     const ControlInfo &ci, bool isForm);
// The same, but only subs with code in them (the designer shows these in bold, like VB).
std::set<std::string> eventsWithCode(const std::vector<Proc> &procs, const std::string &controlId,
                                     const ControlInfo &ci, bool isForm);

// Renaming a control: its event subs (oldId_Click -> newId_Click, every use of the names) and,
// with `references`, the control's own name where the code uses it (oldId.Text). Outside
// comments and strings, any case. Counts in *subs and *refs.
std::string renameControl(const std::string &text, const std::string &oldId, const std::string &newId,
                          bool references, int *subs = nullptr, int *refs = nullptr);
// Which subs renameControl would rename (oldId_<something> where <something> is an event of the
// control kind; any oldId_X if ci is null).
std::vector<std::string> controlSubs(const std::vector<Proc> &procs, const std::string &oldId, const ControlInfo *ci);

// --- the language --------------------------------------------------------------------------

std::vector<Symbol> outline(const std::string &text);
// Comments and strings blanked out (same length, newlines kept).
std::string maskCode(const std::string &text);
// Whole-word uses of `name` outside comments and strings, any case.
std::vector<size_t> findIdentifier(const std::string &text, const std::string &name);
bool isBasicFile(const std::string &path);   // .bas .cls .bi
bool equalsNoCase(const std::string &a, const std::string &b);

// `boing symbols` output, one symbol per line: kind<TAB>name<TAB>line[<TAB>endline<TAB>signature].
// Unknown lines are skipped.
std::vector<Symbol> parseSymbols(const std::string &output);

// --- projects (.bbp) -------------------------------------------------------------------------

struct Bbp {
    std::string name;
    std::string startup;                   // "Form main" or "Sub Main"
    std::vector<std::string> modules, forms, classes;   // relative paths
    std::vector<std::pair<std::string, std::string>> other;   // everything else, in order (Stack=, Icon=)
    bool isFormApp() const { return !forms.empty(); }
    std::vector<std::string> allFiles() const;   // modules, forms, their code-behind modules, classes
};
bool parseBbp(const std::string &text, Bbp &out);
std::string writeBbp(const Bbp &b);
// The code-behind module of a form ("forms/main.afg" -> "forms/main.bas").
std::string codeBehindOf(const std::string &afg);
// The form a code-behind module belongs to, if `files` has it ("main.bas" -> "main.afg"), else "".
std::string formOf(const std::string &bas, const std::vector<std::string> &files);

// --- the boing command ------------------------------------------------------------------------

std::string quoteArg(const std::string &s);       // AmigaDOS quoting when needed
std::string checkCommand(const std::string &boing, const std::string &target);
std::string buildCommand(const std::string &boing, const std::string &target, const std::string &output,
                         bool release, const std::string &extra = "");
std::string runCommand(const std::string &boing, const std::string &target, const std::string &args);
std::string symbolsCommand(const std::string &boing, const std::string &file);

// Runtime errors from `boing run`: "file:line: runtime error 9: Subscript out of range".
// True with file, line and message for such a line.
bool parseRuntimeError(const std::string &line, std::string &file, int &lineNo, std::string &msg);

// Completion after "name.": the members of the control or form called `name` (any case)
// in `controls` (id -> kind; the form's own name maps to "window"; "Me" too in a code-behind).
std::vector<std::string> memberCompletions(const std::string &name, const std::map<std::string, std::string> &controls);

} // namespace basic
} // namespace af
