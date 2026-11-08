// --- basic --- Boing BASIC line syntax and the IDE's code-behind model.
#include "aftest.h"

#include "sci/BasicSyntax.h"
#include "sci/Languages.h"
#include "BasicModel.h"
#include "GuiModel.h"
#include "TemplateEngine.h"
#include <fstream>
#include <sstream>

using namespace af;
using namespace af::basic;

TEST(basic_language_detection)
{
    CHECK(languageFor("DH1:x/Main.BAS") == Language::Basic);
    CHECK(languageFor("Counter.cls") == Language::Basic);
    CHECK(languageFor("Hello.bbp") == Language::Props);
    CHECK_EQ(std::string(commentStyle(Language::Basic).line), std::string("'"));
    CHECK(indentRule(Language::Basic) == IndentRule::Keywords);
    CHECK(foldable(Language::Basic));
}

TEST(basic_comments_and_strings)
{
    CHECK_EQ(stripComment("x = 1 ' set x"), std::string("x = 1"));
    CHECK_EQ(stripComment("Print \"it's\" ' c"), std::string("Print \"it's\""));
    CHECK_EQ(stripComment("Rem all comment"), std::string(""));
    CHECK_EQ(stripComment("x = 1: Rem c"), std::string("x = 1:"));
    CHECK_EQ(stripComment("Remark = 3"), std::string("Remark = 3"));
    CHECK_EQ(stripComment("s = \"a\"\"b ' no\" ' yes"), std::string("s = \"a\"\"b ' no\""));
    CHECK_EQ(maskStrings("s = \"If x Then\""), std::string("s = \"         \""));
    std::vector<std::string> w = codeWords("10 Print X");
    CHECK_EQ(w.size(), (size_t)2);
    CHECK_EQ(w[0], std::string("print"));
    w = codeWords("retry: If a Then");
    CHECK_EQ(w[0], std::string("if"));
}

TEST(basic_classify_blocks)
{
    CHECK(classify("Private Sub ok_Click()").opens == Block::Sub);
    CHECK(classify("Public Static Function F(x As Long) As Long").opens == Block::Function);
    CHECK(classify("Property Get Value() As Long").opens == Block::Property);
    CHECK(classify("Declare Function X Lib \"a.library\" ()").opens == Block::None);
    CHECK(classify("Exit Sub").opens == Block::None);
    CHECK(classify("End Sub").closes == Block::Sub);
    CHECK(classify("End").closes == Block::None);
    CHECK(classify("If a > 1 Then").opens == Block::If);
    CHECK(classify("If a > 1 Then ' comment").opens == Block::If);
    CHECK(classify("If a > 1 Then b = 2").opens == Block::None);
    CHECK(classify("If a Then b = \"Then\"").opens == Block::None);
    CHECK(classify("ElseIf a Then").middle == Block::If);
    CHECK(classify("Else").middle == Block::If);
    CHECK(classify("End If").closes == Block::If);
    CHECK(classify("For i = 1 To 10 Step 2").opens == Block::For);
    CHECK(classify("For Each x In c").opens == Block::For);
    CHECK(classify("Next i").closes == Block::For);
    CHECK(classify("Do While x < 3").opens == Block::Do);
    CHECK(classify("Loop Until x").closes == Block::Do);
    CHECK(classify("While x").opens == Block::While);
    CHECK(classify("Wend").closes == Block::While);
    CHECK(classify("Select Case k").opens == Block::Select);
    CHECK(classify("Case 1, 2").middle == Block::Select);
    CHECK(classify("End Select").closes == Block::Select);
    CHECK(classify("With main").opens == Block::With);
    CHECK(classify("Private Type Point").opens == Block::Type);
    CHECK(classify("Enum Colours").opens == Block::Enum);
    CHECK(classify("' Sub x()").opens == Block::None);
    CHECK(classify("s = \"Sub x()\"").opens == Block::None);
}

TEST(basic_auto_indent)
{
    CHECK_EQ(af::indentChangeAfter(Language::Basic, "Sub Main()"), 1);
    CHECK_EQ(af::indentChangeAfter(Language::Basic, "    x = 1"), 0);
    CHECK_EQ(af::indentChangeAfter(Language::Basic, "    If x Then"), 1);
    CHECK_EQ(af::indentChangeAfter(Language::Basic, "    Case 1"), 1);
    // closers line up with their opener
    std::vector<std::string> before = { "Sub Main()", "    If x Then", "        y = 1" };
    std::vector<int> ind = { 0, 4, 8 };
    CHECK_EQ(closerIndent("        End If", before, ind, 4), 4);
    CHECK_EQ(closerIndent("        Else", before, ind, 4), 4);
    CHECK_EQ(closerIndent("        y = 2", before, ind, 4), -1);
    before = { "Sub Main()", "    If x Then", "        If y Then", "            z", "        End If", "        q" };
    ind = { 0, 4, 8, 12, 8, 8 };
    CHECK_EQ(closerIndent("        End If", before, ind, 4), 4);
    CHECK_EQ(closerIndent("        End Sub", before, ind, 4), 0);
    before = { "    Select Case k" };
    ind = { 4 };
    CHECK_EQ(closerIndent("    Case 1", before, ind, 4), 8);
    before = { "    Select Case k", "        Case 1", "            a" };
    ind = { 4, 8, 12 };
    CHECK_EQ(closerIndent("            Case 2", before, ind, 4), 8);
    CHECK_EQ(closerIndent("            End Select", before, ind, 4), 4);
    // a Next outside any For in this Sub: not found
    before = { "Sub A()", "    x" };
    ind = { 0, 4 };
    CHECK_EQ(closerIndent("    Next", before, ind, 4), -1);
}

static const char *formCode =
    "Option Explicit\n"                                 // 1
    "\n"                                                // 2
    "Private Sub Form_Load()\n"                         // 3
    "    name.Text = \"Amiga\" ' greet\n"               // 4
    "End Sub\n"                                         // 5
    "\n"                                                // 6
    "Private Sub ok_Click()\n"                          // 7
    "End Sub\n"                                         // 8
    "\n"                                                // 9
    "Public Function Twice(ByVal n As Long, _\n"        // 10
    "                      Optional k As Long = 2) As Long\n"   // 11
    "    Twice = n * k\n"                               // 12
    "End Function\n"                                    // 13
    "Sub ok_DblClick()\n"                               // 14
    "    ' nothing yet\n"                               // 15
    "    Call ok_Click\n"                               // 16
    "End Sub\n";                                        // 17

TEST(basic_procedures)
{
    std::vector<Proc> ps = procedures(formCode);
    CHECK_EQ(ps.size(), (size_t)4);
    CHECK_EQ(ps[0].name, std::string("Form_Load"));
    CHECK_EQ(ps[0].line, 3);
    CHECK_EQ(ps[0].endLine, 5);
    CHECK(ps[0].hasCode);
    CHECK_EQ(ps[0].scope, std::string("Private"));
    CHECK_EQ(ps[1].name, std::string("ok_Click"));
    CHECK(!ps[1].hasCode);
    CHECK_EQ(ps[2].name, std::string("Twice"));
    CHECK(ps[2].kind == ProcKind::Function);
    CHECK_EQ(ps[2].line, 10);
    CHECK_EQ(ps[2].endLine, 13);
    CHECK_EQ(ps[2].params, std::string("ByVal n As Long,                        Optional k As Long = 2"));
    CHECK_EQ(ps[2].nameColumn, 16);
    CHECK_EQ(ps[3].name, std::string("ok_DblClick"));
    CHECK(ps[3].hasCode);
    CHECK_EQ(findProc(ps, "OK_CLICK"), 1);
    CHECK_EQ(findProc(ps, "nope"), -1);
}

TEST(basic_ensure_sub_existing_and_new)
{
    std::string t = formCode;
    SubPlace w;
    CHECK(!ensureSub(t, "Form_Load", "", w));
    CHECK_EQ(w.line, 4);
    CHECK_EQ(w.column, 4);
    CHECK(!w.created);
    CHECK(ensureSub(t, "ok_click", "", w));    // any case; an empty line is made in the empty body
    CHECK(!w.created);
    CHECK_EQ(w.line, 8);
    CHECK(t.find("Private Sub ok_Click()\n\nEnd Sub\n") != std::string::npos);
    // a missing sub is added at the end with a blank line before it
    CHECK(ensureSub(t, "name_Change", "", w));
    CHECK(w.created);
    CHECK_EQ(w.line, 21);
    CHECK_EQ(w.column, 4);
    CHECK(t.find("End Sub\n\nPrivate Sub name_Change()\n\nEnd Sub\n") != std::string::npos);
    std::vector<Proc> ps = procedures(t);
    CHECK_EQ(findProc(ps, "name_Change"), 4);
    CHECK_EQ(ps[4].line, 20);
    // with parameters, into an empty file
    std::string e;
    CHECK(ensureSub(e, "Form_Unload", "Cancel As Integer", w));
    CHECK_EQ(e, std::string("Private Sub Form_Unload(Cancel As Integer)\n\nEnd Sub\n"));
    CHECK_EQ(w.line, 2);
    // a file without a final newline
    std::string n = "Sub A()\nEnd Sub";
    CHECK(ensureSub(n, "B", "", w));
    CHECK_EQ(n, std::string("Sub A()\nEnd Sub\n\nPrivate Sub B()\n\nEnd Sub\n"));
    CHECK_EQ(w.line, 5);
}

TEST(basic_empty_sub_goes_to_its_blank_line)
{
    std::string t = "Private Sub ok_Click()\n\nEnd Sub\n";
    SubPlace w;
    CHECK(!ensureSub(t, "ok_Click", "", w));
    CHECK_EQ(w.line, 2);
    CHECK_EQ(w.column, 4);
}

TEST(basic_events_with_code)
{
    std::vector<Proc> ps = procedures(formCode);
    const ControlInfo *b = controlFor("button");
    CHECK(b != nullptr);
    if (!b) return;
    std::set<std::string> subs = eventsWithSubs(ps, "ok", *b, false);
    CHECK(subs.count("Click") == 1);
    std::set<std::string> code = eventsWithCode(ps, "ok", *b, false);
    CHECK(code.count("Click") == 0);   // empty
    const ControlInfo *f = controlFor("window");
    CHECK(f && f->defaultEvent == "Load");
    if (f) CHECK(eventsWithCode(ps, "main", *f, true).count("Load") == 1);
    CHECK_EQ(eventSubName("main", "Load", true), std::string("Form_Load"));
    CHECK_EQ(eventSubName("ok", "Click", false), std::string("ok_Click"));
}

TEST(basic_default_events)
{
    struct { const char *kind, *ev; } want[] = {
        { "button", "Click" }, { "string", "Change" }, { "checkbox", "Click" }, { "chooser", "Click" },
        { "slider", "Change" }, { "listbrowser", "Click" }, { "timer", "Timer" }, { "window", "Load" },
    };
    for (auto &w : want) {
        const ControlInfo *c = controlFor(w.kind);
        CHECK(c != nullptr);
        if (c) CHECK_EQ(c->defaultEvent, std::string(w.ev));
    }
    const ControlInfo *l = controlFor("label");
    CHECK(l && l->defaultEvent.empty());
    const ControlInfo *f = controlFor("window");
    CHECK(f && f->event("unload") && f->event("unload")->params == "Cancel As Integer");
}

TEST(basic_rename_control)
{
    int subs = 0, refs = 0;
    std::string r = renameControl(formCode, "ok", "go", false, &subs, &refs);
    CHECK_EQ(subs, 2);
    CHECK_EQ(refs, 0);
    CHECK(r.find("Private Sub go_Click()") != std::string::npos);
    CHECK(r.find("Sub go_DblClick()") != std::string::npos);
    CHECK(r.find("Call go_Click") != std::string::npos);
    CHECK(r.find("ok_") == std::string::npos);
    // the control's own name in the code
    r = renameControl(formCode, "name", "who", true, &subs, &refs);
    CHECK_EQ(subs, 0);
    CHECK_EQ(refs, 1);
    CHECK(r.find("who.Text = \"Amiga\"") != std::string::npos);
    // strings and comments are left alone
    std::string t = "Sub OK_Click()\n    s = \"ok_Click\" ' ok_Click\nEnd Sub\n";
    r = renameControl(t, "ok", "go", true, &subs, &refs);
    CHECK_EQ(r, std::string("Sub go_Click()\n    s = \"ok_Click\" ' ok_Click\nEnd Sub\n"));
    // only events of the control kind count as its subs (when the kind is given)
    std::vector<Proc> ps = procedures("Sub ok_Click()\nEnd Sub\nSub ok_Helper()\nEnd Sub\n");
    CHECK_EQ(controlSubs(ps, "ok", controlFor("button")).size(), (size_t)1);
    CHECK_EQ(controlSubs(ps, "ok", nullptr).size(), (size_t)2);
}

TEST(basic_outline)
{
    std::string t =
        "Option Explicit\n"
        "Private Type Point\n"
        "    x As Long\n"
        "End Type\n"
        "Public Enum Colour\n"
        "    Red\n"
        "End Enum\n"
        "Const Max = 10\n"
        "Dim count As Long\n"
        "Public total As Long\n"
        "Declare Function DisplayBeep Lib \"intuition.library\" (ByVal s As Long) As Long\n"
        "Event Changed(ByVal v As Long)\n"
        "Sub Main()\n"
        "    Dim local As Long\n"
        "End Sub\n"
        "Property Get Value() As Long\n"
        "End Property\n";
    std::vector<Symbol> s = outline(t);
    CHECK_EQ(s.size(), (size_t)9);
    if (s.size() < 9) return;
    CHECK(s[0].kind == SymKind::Struct && s[0].name == "Point" && s[0].line == 2);
    CHECK(s[1].kind == SymKind::Enum && s[1].name == "Colour");
    CHECK(s[2].kind == SymKind::Define && s[2].name == "Max");
    CHECK(s[3].kind == SymKind::Global && s[3].name == "count");
    CHECK(s[4].kind == SymKind::Global && s[4].name == "total");
    CHECK(s[5].kind == SymKind::Function && s[5].name == "DisplayBeep" && s[5].detail == "Declare");
    CHECK(s[6].kind == SymKind::Method && s[6].name == "Changed");
    CHECK(s[7].kind == SymKind::Function && s[7].name == "Main" && s[7].line == 13 && s[7].detail == "()");
    CHECK(s[8].kind == SymKind::Method && s[8].name == "Value" && s[8].detail == "Property Get");
    // through the language dispatch
    CHECK(hasOutline(Language::Basic));
    CHECK_EQ(outlineSymbols(t, Language::Basic).size(), (size_t)9);
}

TEST(basic_identifiers_any_case)
{
    std::string t = "Dim Count As Long\ncount = COUNT + 1 ' count\ns = \"count\"\n";
    CHECK_EQ(af::findIdentifier(t, "count", Language::Basic).size(), (size_t)3);
    CHECK(isReservedWord("dim", Language::Basic));
    CHECK(isReservedWord("Integer", Language::Basic));
    CHECK(!isReservedWord("counter", Language::Basic));
    int n = 0;
    CHECK_EQ(af::replaceIdentifier(t, "count", "total", Language::Basic, &n),
             std::string("Dim total As Long\ntotal = total + 1 ' count\ns = \"count\"\n"));
    CHECK_EQ(n, 3);
}

TEST(basic_bbp)
{
    Bbp b;
    CHECK(parseBbp("# Boing BASIC project\nName=Hello\nStartup=Form main            ' or: Startup=Sub Main\n"
                   "Module=Main.bas\nForm=main.afg                ' code-behind\nClass=Counter.cls\nStack=262144\n", b));
    CHECK_EQ(b.name, std::string("Hello"));
    CHECK_EQ(b.startup, std::string("Form main"));
    CHECK_EQ(b.modules.size(), (size_t)1);
    CHECK_EQ(b.forms.size(), (size_t)1);
    CHECK_EQ(b.forms[0], std::string("main.afg"));
    CHECK(b.isFormApp());
    CHECK_EQ(b.other.size(), (size_t)1);
    std::vector<std::string> all = b.allFiles();
    CHECK_EQ(all.size(), (size_t)4);
    CHECK_EQ(all[2], std::string("main.bas"));
    Bbp c;
    CHECK(parseBbp(writeBbp(b), c));
    CHECK_EQ(c.name, b.name);
    CHECK_EQ(c.classes.size(), (size_t)1);
    CHECK_EQ(c.other[0].first, std::string("Stack"));
    CHECK_EQ(codeBehindOf("forms/main.afg"), std::string("forms/main.bas"));
    CHECK_EQ(codeBehindOf("DH1:x/main.afg"), std::string("DH1:x/main.bas"));
    CHECK_EQ(formOf("main.bas", { "Main.bas", "main.afg" }), std::string("main.afg"));
    CHECK_EQ(formOf("Main2.bas", { "main.afg" }), std::string(""));
    CHECK(isBasicFile("x.BAS") && isBasicFile("c.cls") && !isBasicFile("x.c"));
}

TEST(basic_commands)
{
    CHECK_EQ(checkCommand("boing", "Hello.bbp"), std::string("boing check Hello.bbp"));
    CHECK_EQ(buildCommand("SDK:C/boing", "My App.bbp", "myapp", false), std::string("SDK:C/boing build \"My App.bbp\" -o myapp --debug"));
    CHECK_EQ(buildCommand("boing", "a.bbp", "a", true), std::string("boing build a.bbp -o a"));
    CHECK_EQ(runCommand("boing", "a.bbp", "x y"), std::string("boing run a.bbp x y"));
    CHECK_EQ(runCommand("boing", "a.bbp", "", "T:e.1"), std::string("boing run --error-file T:e.1 a.bbp"));
    std::string f, m;
    int l = 0;
    CHECK(parseRuntimeError("DH1:proj/main.bas:12: runtime error 9: Subscript out of range", f, l, m));
    CHECK_EQ(f, std::string("DH1:proj/main.bas"));
    CHECK_EQ(l, 12);
    CHECK_EQ(m, std::string("runtime error 9: Subscript out of range"));
    CHECK(!parseRuntimeError("main.bas:12:3: error: x", f, l, m));
}

TEST(basic_symbols_output)
{
    std::vector<Symbol> s = parseSymbols("sub\tok_Click\t12\t15\tSub ok_Click()\n"
                                         "function\tTwice\t20\t23\tFunction Twice(ByVal n As Long) As Long\n"
                                         "property-get\tValue\t30\n"
                                         "garbage line\n"
                                         "type\tPoint\t2\t5\t\n");
    CHECK_EQ(s.size(), (size_t)4);
    if (s.size() < 4) return;
    CHECK(s[0].name == "ok_Click" && s[0].line == 12 && s[0].detail == "()");
    CHECK(s[1].detail == "(ByVal n As Long)");
    CHECK(s[2].kind == SymKind::Method);
    CHECK(s[3].kind == SymKind::Struct);
    // the format boing prints: kind, name, line, signature[, scope]; locals and labels are skipped
    s = parseSymbols("sub\tMain\t1\tSub Main(ByVal n As Long)\nlocal\tx\t2\tDim x As Long\tMain\n"
                     "const\tMax\t3\tConst Max = 3\n");
    CHECK_EQ(s.size(), (size_t)2);
    if (s.size() == 2) {
        CHECK(s[0].name == "Main" && s[0].detail == "(ByVal n As Long)");
        CHECK(s[1].kind == SymKind::Define);
    }
}

TEST(basic_member_completion)
{
    std::map<std::string, std::string> ctl = { { "ok", "button" }, { "names", "listbrowser" }, { "main", "window" } };
    std::vector<std::string> m = memberCompletions("OK", ctl);
    CHECK(std::find(m.begin(), m.end(), "Caption") != m.end());
    CHECK(std::find(m.begin(), m.end(), "Text") != m.end());
    CHECK(std::find(m.begin(), m.end(), "SetFocus") != m.end());
    m = memberCompletions("names", ctl);
    CHECK(std::find(m.begin(), m.end(), "AddItem") != m.end());
    m = memberCompletions("main", ctl);
    CHECK(std::find(m.begin(), m.end(), "Caption") != m.end());
    CHECK(memberCompletions("x", ctl).empty());
}

TEST(basic_forms_events_md)
{
    const char *md =
        "# Forms: properties, methods, events\n"
        "Intro text.\n"
        "## Button (`button`) - VB `Button`\n"
        "### Properties\n"
        "- `Text` As String - the label\n"
        "- `Enabled` As Boolean\n"
        "### Methods\n"
        "- `SetFocus()`\n"
        "### Events\n"
        "- `Click()` (default) - pressed\n"
        "## String (`string`) - VB `TextBox`\n"
        "**Events:**\n"
        "- Enter()\n"
        "- Change() (default)\n"
        "## form\n"
        "Events:\n"
        "| Event | Notes |\n"
        "|---|---|\n"
        "| Form_Load() | before it opens |\n"
        "| Form_Unload(Cancel As Integer) | |\n"
        "## Notes\n"
        "- not a control\n";
    std::vector<ControlInfo> t;
    CHECK(parseFormsEvents(md, t));
    CHECK_EQ(t.size(), (size_t)3);
    if (t.size() < 3) return;
    CHECK_EQ(t[0].kind, std::string("button"));
    CHECK_EQ(t[0].basicType, std::string("Button"));
    CHECK_EQ(t[0].properties.size(), (size_t)2);
    if (t[0].properties.size() < 2) return;
    CHECK_EQ(t[0].properties[0].type, std::string("String"));
    CHECK_EQ(t[0].properties[0].doc, std::string("the label"));
    CHECK_EQ(t[0].methods.size(), (size_t)2);   // SetFocus, and Refresh (every gadget has it)
    CHECK_EQ(t[0].defaultEvent, std::string("Click"));
    CHECK_EQ(t[1].basicType, std::string("TextBox"));
    CHECK_EQ(t[1].events.size(), (size_t)2);
    CHECK_EQ(t[1].defaultEvent, std::string("Change"));
    CHECK_EQ(t[2].kind, std::string("window"));
    CHECK_EQ(t[2].events.size(), (size_t)2);
    CHECK_EQ(t[2].events[0].name, std::string("Load"));
    CHECK_EQ(t[2].events[1].params, std::string("Cancel As Integer"));
    CHECK_EQ(t[2].defaultEvent, std::string("Load"));
    // the table replaces the built-in one; kinds it doesn't describe stay
    setControls(t);
    CHECK(controlFor("string")->event("Enter") != nullptr);
    CHECK(controlFor("slider") != nullptr);
    resetControls();
    CHECK(controlFor("string")->event("Enter") == nullptr);
}

TEST(basic_completion_words_and_form_controls)
{
    CHECK_EQ(casedWord("elseif"), std::string("ElseIf"));
    CHECK_EQ(casedWord("MSGBOX"), std::string("MsgBox"));
    CHECK_EQ(casedWord("myVar"), std::string("myVar"));
    // every keyword the lexer knows has a cased form for completion
    for (int set = 0; set < 3; set++) {
        std::string list = keywords(set), w;
        for (size_t i = 0; i <= list.size(); i++) {
            if (i < list.size() && list[i] != ' ') { w.push_back(list[i]); continue; }
            if (!w.empty() && casedWord(w) == w && w != "") {
                bool found = false;
                for (auto &c : completionWords()) found = found || equalsNoCase(c, w);
                if (!found) aftest::fail(__FILE__, __LINE__, "no cased word for " + w);
            }
            w.clear();
        }
    }
    std::map<std::string, std::string> c = formControls(
        "# AmiForge GUI\nwindow name=main title=x\nvlayout id=root\n  button id=ok text=_OK\n  string id=who\n  space\n");
    CHECK_EQ(c["main"], std::string("window"));
    CHECK_EQ(c["ok"], std::string("button"));
    CHECK_EQ(c["who"], std::string("string"));
    CHECK_EQ(c["root"], std::string("vlayout"));
    CHECK_EQ(c.size(), (size_t)4);
}

// Boing BASIC's own docs/forms-events.md (the copy built in, BasicFormsEvents.h)
TEST(basic_forms_events_contract)
{
    std::string md = builtinFormsEvents();
    std::vector<ControlInfo> t;
    std::string err;
    CHECK(parseFormsEvents(md, t, &err));
    auto find = [&](const char *k) -> const ControlInfo * {
        for (auto &c : t) if (c.kind == k) return &c;
        return nullptr;
    };
    for (const char *k : { "window", "button", "string", "integer", "checkbox", "chooser", "radio", "slider",
                           "scroller", "listbrowser", "label", "space", "fuelgauge", "getfile", "texteditor", "timer" })
        if (!find(k)) aftest::fail(__FILE__, __LINE__, std::string("no section for ") + k);
    const ControlInfo *w = find("window");
    if (w) {
        CHECK_EQ(w->basicType, std::string("Form"));
        CHECK_EQ(w->defaultEvent, std::string("Load"));
        CHECK(w->event("Unload") && w->event("Unload")->params == "Cancel As Integer");
        CHECK(w->event("QueryUnload") && w->event("QueryUnload")->params == "Cancel As Integer, UnloadMode As Integer");
        CHECK(w->event("KeyDown") && w->event("KeyDown")->params == "KeyCode As Integer, Shift As Integer");
        bool caption = false, show = false;
        for (auto &p : w->properties) caption = caption || p.name == "Caption";
        for (auto &m : w->methods) show = show || m.name == "Show";
        CHECK(caption && show);
    }
    const ControlInfo *b = find("button");
    if (b) {
        CHECK_EQ(b->basicType, std::string("CommandButton"));
        CHECK_EQ(b->defaultEvent, std::string("Click"));
        CHECK(b->event("Click") && b->event("Click")->params.empty());
        bool enabled = false, focus = false;
        for (auto &p : b->properties) enabled = enabled || p.name == "Enabled";
        for (auto &m : b->methods) focus = focus || m.name == "SetFocus";
        CHECK(enabled && focus);
    }
    const ControlInfo *s = find("string");
    if (s) {
        CHECK_EQ(s->defaultEvent, std::string("Change"));
        CHECK(s->event("KeyPress") && s->event("KeyPress")->params == "KeyAscii As Integer");
    }
    const ControlInfo *c = find("checkbox");
    if (c) {
        bool caption = false, text = false;
        for (auto &p : c->properties) { caption = caption || p.name == "Caption"; text = text || p.name == "Text"; }
        CHECK(caption && text);
    }
    const ControlInfo *ch = find("chooser");
    if (ch) {
        bool add = false, list = false;
        for (auto &m : ch->methods) add = add || m.name == "AddItem";
        for (auto &p : ch->properties) list = list || p.name == "List";
        CHECK(add && list);
    }
    const ControlInfo *lb = find("listbrowser");
    if (lb) CHECK(lb->event("DblClick") != nullptr);
    const ControlInfo *lab = find("label");
    if (lab) {
        CHECK(lab->events.empty() && lab->defaultEvent.empty());
        bool enabled = false;
        for (auto &p : lab->properties) enabled = enabled || p.name == "Enabled";
        CHECK(!enabled);
    }
    const ControlInfo *tm = find("timer");
    if (tm) {
        CHECK_EQ(tm->defaultEvent, std::string("Timer"));
        bool interval = false;
        for (auto &p : tm->properties) interval = interval || p.name == "Interval";
        CHECK(interval);
    }
    const ControlInfo *sl = find("slider");
    if (sl) CHECK(sl->event("Scroll") != nullptr && sl->defaultEvent == "Change");
}

// The steps of New Project > Boing BASIC Form App and the designer on its form, without the GUI:
// the template's plan, the .afg the designer parses, the events of every object (what the
// Events list shows), double-click targets, a new event sub, a rename.
TEST(basic_form_template_to_designer_flow)
{
    auto slurp = [](const std::string &p, std::string &out) {
        std::ifstream f(p, std::ios::binary);
        if (!f) return false;
        std::stringstream ss;
        ss << f.rdbuf();
        out = ss.str();
        return true;
    };
    const std::string dir = "../../templates/basic-form";
    std::string defText, err;
    CHECK(slurp(dir + "/template.def", defText));
    tmpl::Def def;
    CHECK(tmpl::parseDef(defText, def, err));
    tmpl::Vars v = tmpl::makeVars("FormTest", "", 8, 11, 2026);
    tmpl::Plan plan = tmpl::plan(tmpl::instantiate(def, v), v, [&](const std::string &src, std::string &out) { return slurp(dir + "/" + src, out); });
    CHECK(plan.errors.empty());
    std::string afg, bas, bbp;
    for (auto &f : plan.files) {
        if (f.path == "main.afg") afg = f.content;
        if (f.path == "main.bas") bas = f.content;
        if (f.path == "FormTest.bbp") bbp = f.content;
    }
    CHECK(!afg.empty() && !bas.empty() && !bbp.empty());
    Bbp b;
    CHECK(parseBbp(bbp, b) && b.forms.size() == 1 && b.startup == "Form main");
    gui::Model m;
    CHECK(m.parse(afg, err));
    std::vector<std::pair<gui::Node *, int>> all;
    m.walk(all);
    std::vector<Proc> procs = procedures(bas);
    int withEvents = 0;
    // the window and every object, as refreshEvents does it
    std::vector<gui::Node *> nodes = { &m.window };
    for (auto &p : all) nodes.push_back(p.first);
    for (gui::Node *n : nodes) {
        std::string kind = gui::kindInfo(n->kind).name;
        const ControlInfo *ci = controlFor(kind);
        if (!ci) continue;
        bool isForm = n == &m.window;
        std::string id = isForm ? n->get("name") : n->get("id");
        std::set<std::string> subs = eventsWithSubs(procs, id, *ci, isForm);
        std::set<std::string> code = eventsWithCode(procs, id, *ci, isForm);
        for (auto &c : code) CHECK(subs.count(c) == 1);
        if (!ci->events.empty()) withEvents++;
        if (!ci->defaultEvent.empty()) CHECK(ci->event(ci->defaultEvent) != nullptr);
    }
    CHECK(withEvents >= 4);   // the form, who, greet, quit
    // double-click on who, then Form_Unload from the Events list
    SubPlace w;
    std::string text = bas;
    CHECK(!ensureSub(text, eventSubName("who", controlFor("string")->defaultEvent, false), "", w));
    const Member *un = controlFor("window")->event("Unload");
    CHECK(un != nullptr);
    if (un) CHECK(ensureSub(text, eventSubName("main", un->name, true), un->params, w));
    CHECK(text.find("Private Sub Form_Unload(Cancel As Integer)") != std::string::npos);
    // rename greet -> hello
    int subs = 0, refs = 0;
    text = renameControl(text, "greet", "hello", true, &subs, &refs);
    CHECK_EQ(subs, 1);
    CHECK(text.find("hello_Click") != std::string::npos && text.find("greet_Click") == std::string::npos);
    CHECK(text.find("hello.Enabled") != std::string::npos && text.find("greeting.Text") != std::string::npos);
    // a Timer added in the designer stays out of the C code
    gui::Node *t = m.root()->add(gui::Kind::Timer);
    t->set("id", "tick");
    t->set("interval", "500");
    gui::Generated g = gui::generate(m, "");
    CHECK(g.source.find("tick") == std::string::npos || g.source.find("TIMER") == std::string::npos);
    std::string again = m.write();
    gui::Model m2;
    CHECK(m2.parse(again, err));
    CHECK(m2.findById("tick") && m2.findById("tick")->kind == gui::Kind::Timer);
}
