// Tools > GUI Designer: edit a .afg file (GuiModel.h) - a palette of ReAction objects, the
// window's layout as a tree, the selected object's properties - with a live preview window
// built from real ReAction objects, and "Generate Code" for <name>_gui.c/.h.
// --- basic --- In a Boing BASIC project it is a Visual Basic style form designer instead:
// there is no generated code, the form's code is the module beside it (main.afg -> main.bas);
// an Events list shows the selected object's events (with code, empty, none), double-clicking
// a control (in the tree or the preview) or choosing an event opens its sub in the editor,
// and "View Code" opens the module (BasicDesign.cpp does the editor side). The Menu Editor
// (BasicMenuEditor.cpp) makes the form's menus; the preview shows them as its menu strip, and
// choosing a menu item there opens its <menu>_Click sub. Toolbars, status bars and tabs are
// previewed with ReAction gadgets, grids and tree views as lists.
#include "App.h"
#include "GuiModel.h"
#include "BasicModel.h"   // --- basic ---
#include "Scintilla.h"

#include <deque>
#include <functional>
#include <string.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/string.h>
#include <proto/integer.h>
#include <proto/checkbox.h>
#include <proto/chooser.h>
#include <proto/radiobutton.h>
#include <proto/slider.h>
#include <proto/scroller.h>
#include <proto/listbrowser.h>
#include <proto/label.h>
#include <proto/space.h>
#include <proto/fuelgauge.h>
#include <proto/getfile.h>
#include <proto/texteditor.h>
#include <proto/clicktab.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/string.h>
#include <gadgets/integer.h>
#include <gadgets/checkbox.h>
#include <gadgets/chooser.h>
#include <gadgets/radiobutton.h>
#include <gadgets/slider.h>
#include <gadgets/scroller.h>
#include <gadgets/listbrowser.h>
#include <gadgets/fuelgauge.h>
#include <gadgets/getfile.h>
#include <gadgets/texteditor.h>
#include <gadgets/clicktab.h>
#include <libraries/gadtools.h>
#include <images/label.h>
#include <images/bevel.h>

namespace af {

using namespace gui;

bool basicMenuEditor(App &app, Model &model, std::string &selectId);   // BasicMenuEditor.cpp

enum {
    DG_PALETTE = 1, DG_ADD, DG_TREE, DG_REMOVE, DG_UP, DG_DOWN, DG_INDENT, DG_OUTDENT,
    DG_PROPS, DG_VALUE, DG_SAVE, DG_GENERATE, DG_PREVIEW, DG_STATUS,
    DG_EVENTS, DG_MENUS /* --- basic --- */, DG_N
};
static const uint32 PREVIEW_ID_BASE = 1000;

class Designer {
public:
    explicit Designer(App &a) : app(a) {}
    ~Designer() { close(); }

    bool open(const std::string &afg);
    void close();
    bool confirmClose();          // asks about unsaved changes; false = cancelled
    uint32 signals();
    void handle(uint32 got);
    bool wantsClose = false;
    // --- basic --- for App and the MCP commands
    const std::string &file() const { return path; }
    bool isBasic() const { return basicMode; }
    void toFront() { if (win) { IIntuition->WindowToFront(win); IIntuition->ActivateWindow(win); } }
    bool selectId(const std::string &id);          // "" or the window's name: the window
    std::string selectedId();                      // the window's name for the window
    std::string selectedKind();                    // "window", "button"...
    bool gotoEvent(const std::string &event);      // the selected object's ("" = its default event)
    std::string eventsReport();                    // name<TAB>state<TAB>params per event of the selection
    bool setProperty(const std::string &key, const std::string &value, std::string &err);   // as typed in the editor field
    bool saveFile() { return save(); }
    void addKind(Kind k);                          // as the palette's Add does
    void menuEditor();                             // --- basic --- Tools > Menu Editor

private:
    App &app;
    Model model;
    std::string path;
    bool modified = false;
    Object *winObj = nullptr;
    struct Window *win = nullptr;
    Object *g[DG_N] = {};
    ListView palette{1}, tree{2}, props{2};
    std::vector<Kind> paletteKinds;
    std::vector<Node *> rows;                  // tree rows (window = nullptr)
    Node *selected = nullptr;                  // nullptr = the window
    std::vector<const PropInfo *> propRows;
    int selectedProp = -1;
    // the preview
    Object *pvObj = nullptr;
    struct Window *pvWin = nullptr;
    int pvLeft = -1, pvTop = -1;
    std::deque<std::string> pvStrings;         // texts the preview's objects point at
    std::deque<std::vector<const char *>> pvArrays;
    std::vector<struct List *> pvLists;
    std::vector<Node *> pvNodes;               // by preview gadget ID - PREVIEW_ID_BASE
    // --- basic ---
    bool basicMode = false;
    ListView events{2};
    std::vector<std::string> eventRows;        // event names, per row of `events`
    Node *pvLastPick = nullptr;                // preview double-clicks
    uint32 pvLastSecs = 0, pvLastMicros = 0;
    std::vector<struct NewMenu> pvMenu;        // the preview's menu strip (nm_UserData = index + 1 in pvMenuNodes)
    std::vector<Node *> pvMenuNodes;
    std::vector<struct List *> pvTabLists;     // clicktab labels
    std::vector<struct ColumnInfo *> pvColumns;   // the grids' column titles
    uint32 pvId(Node *n);                      // a preview gadget ID that selects n
    void buildMenus();
    void refreshEvents();
    std::string idOf(Node *n);                 // its id (the window: its name); made if it has none

    Node *nodeOf() { return selected ? selected : &model.window; }
    void setStatus(const std::string &s);
    void refreshTree();
    void refreshProps();
    void select(Node *n);
    void changed(bool structure);
    void addObject();
    void removeObject();
    void move(int how);
    void applyValue();
    void toggleOrCycle();
    bool save();
    void generate();
    void openPreview();
    void closePreview();
    Object *build(Node *n);
    const char *keep(const std::string &s) { pvStrings.push_back(s); return pvStrings.back().c_str(); }
};

// --- the window ----------------------------------------------------------------------------

bool Designer::open(const std::string &afg)
{
    path = afg;
    std::string text, err;
    if (readFile(afg, text)) {
        if (!model.parse(text, err)) {
            app.info("Can't read " + afg + ":\n" + err);
            return false;
        }
    } else {
        // a new file: a window named after it, with a vertical group and a button
        model = Model();
        std::string base = fileName(afg);
        size_t dot = base.rfind('.');
        if (dot != std::string::npos) base = base.substr(0, dot);
        model.window.set("name", identifier(base));
        model.window.set("title", base);
        Node *b = model.root()->add(Kind::Button);
        b->set("id", "ok");
        b->set("text", "_OK");
        modified = true;
    }
    model.root();

    static const char *treeTitles[] = { "Object", "ID name" };
    static const int treeWeights[] = { 55, 45 };
    static const char *propTitles[] = { "Property", "Value" };
    static const int propWeights[] = { 45, 55 };
    Class *layout = ILayout->LAYOUT_GetClass();
    auto btn = [&](uint32 id, const char *t, const char *hint) {
        return g[id] = IIntuition->NewObject(IButton->BUTTON_GetClass(), nullptr, GA_ID, id, GA_RelVerify, TRUE,
                                             GA_Text, (uint32)t, GA_HintInfo, (uint32)hint, TAG_DONE);
    };
    auto group = [&](const char *title) {
        return IIntuition->NewObject(layout, nullptr, LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
                                     LAYOUT_BevelStyle, BVS_GROUP, LAYOUT_Label, (uint32)title, TAG_DONE);
    };
    Object *pal = group("Palette"), *trg = group("Layout"), *prg = group("Properties");
    IIntuition->SetAttrs(pal, LAYOUT_AddChild, (uint32)palette.create(DG_PALETTE),
                         LAYOUT_AddChild, (uint32)btn(DG_ADD, "_Add", "Add the chosen object after the selected one (or into the selected group)"),
                         CHILD_WeightedHeight, 0, TAG_DONE);
    Object *treeButtons = IIntuition->NewObject(layout, nullptr, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ, LAYOUT_EvenSize, TRUE,
        LAYOUT_AddChild, btn(DG_REMOVE, "Remo_ve", "Delete the selected object"),
        LAYOUT_AddChild, btn(DG_UP, "_Up", "Move it before the previous object"),
        LAYOUT_AddChild, btn(DG_DOWN, "_Down", "Move it after the next object"),
        LAYOUT_AddChild, btn(DG_INDENT, "_In", "Move it into the group above it"),
        LAYOUT_AddChild, btn(DG_OUTDENT, "_Out", "Move it out of its group"),
        TAG_DONE);
    IIntuition->SetAttrs(trg, LAYOUT_AddChild, (uint32)tree.create(DG_TREE, treeTitles, treeWeights, true),
                         LAYOUT_AddChild, (uint32)treeButtons, CHILD_WeightedHeight, 0, TAG_DONE);
    g[DG_VALUE] = IIntuition->NewObject(IString->STRING_GetClass(), nullptr, GA_ID, DG_VALUE, GA_RelVerify, TRUE,
                                        STRINGA_MaxChars, 1000, GA_HintInfo,
                                        (uint32)"Type the value and press Return. Double-click a yes/no or choice property to change it.",
                                        TAG_DONE);
    IIntuition->SetAttrs(prg, LAYOUT_AddChild, (uint32)props.create(DG_PROPS, propTitles, propWeights),
                         LAYOUT_AddChild, (uint32)g[DG_VALUE], CHILD_WeightedHeight, 0, TAG_DONE);
    // --- basic --- the events of the selected object, under its properties
    basicMode = app.basicFormMode(afg);
    if (basicMode) {
        static const char *evTitles[] = { "Event", "Code" };
        static const int evWeights[] = { 60, 40 };
        Object *evg = group("Events (click one to write its code)");
        IIntuition->SetAttrs(evg, LAYOUT_AddChild, (uint32)events.create(DG_EVENTS, evTitles, evWeights), TAG_DONE);
        IIntuition->SetAttrs(prg, LAYOUT_AddChild, (uint32)evg, CHILD_WeightedHeight, 90, TAG_DONE);
    }

    static std::string title;
    title = "GUI Designer - " + path;
    winObj = IIntuition->NewObject(IWindow->WINDOW_GetClass(), nullptr,
        WA_Title, (uint32)title.c_str(), WA_DragBar, TRUE, WA_CloseGadget, TRUE, WA_DepthGadget, TRUE,
        WA_SizeGadget, TRUE, WA_Activate, TRUE, WA_PubScreen, (uint32)app.win->WScreen,
        WA_Left, app.win->LeftEdge + 40, WA_Top, app.win->TopEdge + 30, WA_InnerWidth, 820, WA_InnerHeight, basicMode ? 520 : 460,
        WA_IDCMP, IDCMP_GADGETUP | IDCMP_CLOSEWINDOW,
        WINDOW_GadgetHelp, TRUE,
        WINDOW_Layout, IIntuition->NewObject(layout, nullptr,
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT, LAYOUT_SpaceOuter, TRUE,
            LAYOUT_AddChild, IIntuition->NewObject(layout, nullptr, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_AddChild, pal, CHILD_WeightedWidth, 22,
                LAYOUT_AddChild, trg, CHILD_WeightedWidth, 40,
                LAYOUT_AddChild, prg, CHILD_WeightedWidth, 38,
                TAG_DONE),
            LAYOUT_AddChild, IIntuition->NewObject(layout, nullptr, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_AddChild, g[DG_STATUS] = IIntuition->NewObject(IButton->BUTTON_GetClass(), nullptr,
                    GA_ID, DG_STATUS, GA_ReadOnly, TRUE, GA_Text, (uint32)"", BUTTON_Justification, BCJ_LEFT, TAG_DONE),
                LAYOUT_AddChild, btn(DG_PREVIEW, "_Preview", "Show the window as it will look"),
                CHILD_WeightedWidth, 0,
                basicMode ? LAYOUT_AddChild : TAG_IGNORE,   // --- basic ---
                basicMode ? (uint32)btn(DG_MENUS, "_Menu Editor...", "Make the form's menus (titles, items, shortcuts)") : 0,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, btn(DG_SAVE, "_Save", "Save the .afg file"),
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, basicMode
                    ? btn(DG_GENERATE, "View _Code", "Open the form's code (its Boing BASIC module) in the editor")   // --- basic ---
                    : btn(DG_GENERATE, "_Generate Code", "Save, and write the C code for this window into the project"),
                CHILD_WeightedWidth, 0,
                TAG_DONE),
            CHILD_WeightedHeight, 0,
            TAG_DONE),
        TAG_DONE);
    if (!winObj) return false;
    win = (struct Window *)IIntuition->IDoMethod(winObj, WM_OPEN);
    if (!win) {
        IIntuition->DisposeObject(winObj);
        winObj = nullptr;
        return false;
    }
    palette.win = tree.win = props.win = win;
    events.win = win;   // --- basic ---
    palette.detach();
    for (Kind k : paletteKinds = gui::paletteKinds(basicMode)) palette.add({ gui::paletteTitle(k, basicMode) });   // --- basic --- VB's Toolbox
    palette.attach(0);
    select(model.root());
    openPreview();
    setStatus(basicMode ? (modified ? "A new form: add controls from the palette, double-click one to write its code."
                                    : "Double-click a control to write its code; choose an event below its properties.")
                        : modified ? "A new GUI: add objects from the palette, then Generate Code."
                                   : "Select an object to change it; the preview follows every change.");
    return true;
}

void Designer::close()
{
    closePreview();
    palette.win = tree.win = props.win = events.win = nullptr;
    if (winObj) IIntuition->DisposeObject(winObj);
    winObj = nullptr;
    win = nullptr;
    palette.obj = tree.obj = props.obj = events.obj = nullptr;
}

bool Designer::confirmClose()
{
    if (!modified) return true;
    int r = app.ask("GUI Designer", fileName(path) + " has changes that aren't saved.", "_Save|_Don't Save|_Cancel");
    if (r == 1) return save();
    return r == 2;
}

uint32 Designer::signals()
{
    uint32 s = 0, p = 0;
    if (winObj) IIntuition->GetAttr(WINDOW_SigMask, winObj, &s);
    if (pvObj) IIntuition->GetAttr(WINDOW_SigMask, pvObj, &p);
    return s | p;
}

void Designer::setStatus(const std::string &s)
{
    static std::string text;
    text = s;
    if (win) IIntuition->SetGadgetAttrs((struct Gadget *)g[DG_STATUS], win, nullptr, GA_Text, (uint32)text.c_str(), TAG_DONE);
}

// --- the tree and the properties ------------------------------------------------------------

static std::string rowText(Node *n)
{
    const KindInfo &k = kindInfo(n->kind);
    std::string t = k.title;
    // --- basic ---
    if ((n->kind == Kind::Menu && n->get("text") == "-") || (n->kind == Kind::Tool && n->getBool("separator")))
        return t + ": (separator)";
    if (n->kind == Kind::Menu)
        return std::string(n->parent && n->parent->kind == Kind::Window ? (n->getBool("hidden") ? "Context menu" : "Menu title")
                                                                        : "Menu item") + ": " + n->get("text") +
               (n->has("key") ? "  (Amiga-" + n->get("key") + ")" : "");
    if (n->parent && n->parent->kind == Kind::Tabs) return "Page: " + n->get("title");
    if (n->has("text")) t += ": " + n->get("text");
    else if (n->has("title")) t += ": " + n->get("title");
    else if (n->has("label")) t += ": " + n->get("label");
    return t;
}

void Designer::refreshTree()
{
    tree.detach();
    IListBrowser->FreeListBrowserList(tree.list);
    IExec->NewList(tree.list);
    rows.clear();
    int sel = 0;
    tree.add({ "Window: " + model.window.get("title"), model.window.get("name") }, nullptr, 0,
             LBFLG_HASCHILDREN | LBFLG_SHOWCHILDREN);
    rows.push_back(nullptr);
    std::vector<std::pair<Node *, int>> all;
    model.walk(all);
    for (auto &p : all) {
        uint32 flags = kindInfo(p.first->kind).container ? LBFLG_HASCHILDREN | LBFLG_SHOWCHILDREN : 0;
        if (p.first == selected) sel = (int)rows.size();
        tree.add({ rowText(p.first), p.first->get("id") }, p.first, p.second + 1, flags);
        rows.push_back(p.first);
    }
    tree.attach(sel);
}

void Designer::refreshProps()
{
    Node *n = nodeOf();
    props.detach();
    IListBrowser->FreeListBrowserList(props.list);
    IExec->NewList(props.list);
    propRows.clear();
    for (auto &p : kindInfo(n->kind).props) {
        if (p.basic && !basicMode) continue;   // --- basic --- the C designer has no use for it
        std::string v = n->get(p.key);
        if (p.type == PropType::Bool) v = n->getBool(p.key) ? "Yes" : "No";
        props.add({ p.title, v });
        propRows.push_back(&p);
    }
    if (selectedProp >= (int)propRows.size()) selectedProp = -1;
    props.attach(selectedProp);
    props.select(selectedProp);   // -1 clears the old row's highlight
    std::string v = selectedProp >= 0 ? n->get(propRows[selectedProp]->key) : "";
    IIntuition->SetGadgetAttrs((struct Gadget *)g[DG_VALUE], win, nullptr, STRINGA_TextVal, (uint32)v.c_str(),
                               GA_Disabled, selectedProp < 0, TAG_DONE);
    bool isRoot = selected == model.root() || !selected;
    for (int id : { DG_REMOVE, DG_UP, DG_DOWN, DG_INDENT, DG_OUTDENT })
        IIntuition->SetGadgetAttrs((struct Gadget *)g[id], win, nullptr, GA_Disabled, isRoot, TAG_DONE);
    refreshEvents();   // --- basic ---
}

// --- basic --- the Events list: every event of the selected object's kind, and whether the
// code-behind has a sub for it ("code", or "empty" for a sub with nothing in it yet)
void Designer::refreshEvents()
{
    if (!basicMode || !events.obj) return;
    Node *n = nodeOf();
    std::string kind = kindInfo(n->kind).name;
    const basic::ControlInfo *ci = basic::controlFor(kind);
    events.detach();
    IListBrowser->FreeListBrowserList(events.list);
    IExec->NewList(events.list);
    eventRows.clear();
    if (ci) {
        std::string id = n == &model.window ? model.window.get("name") : n->get("id");
        std::vector<int> st = app.basicEventStates(path, id, kind);
        for (size_t i = 0; i < ci->events.size(); i++) {
            const basic::Member &e = ci->events[i];
            int state = i < st.size() ? st[i] : 0;
            std::string label = e.name + (e.params.empty() ? "" : "(...)");
            if (e.name == ci->defaultEvent) label += "  (default)";
            struct ::Node *row = events.add({ label, state == 2 ? "code" : state == 1 ? "empty" : "" });
            if (row && state == 2 && app.penNote >= 0) events.setPens(row, app.penNote);
            eventRows.push_back(e.name);
        }
    }
    events.attach(-1);
}

std::string Designer::idOf(Node *n)
{
    if (!n || n == &model.window) return model.window.get("name");
    if (!n->has("id")) {
        n->set("id", model.uniqueId(n->kind));
        changed(false);
    }
    return n->get("id");
}

bool Designer::selectId(const std::string &id)
{
    if (id.empty() || basic::equalsNoCase(id, model.window.get("name")) || basic::equalsNoCase(id, "form")) {
        select(nullptr);
        return true;
    }
    std::vector<std::pair<Node *, int>> all;
    model.walk(all);
    for (auto &p : all)
        if (basic::equalsNoCase(p.first->get("id"), id)) {
            select(p.first);
            return true;
        }
    return false;
}

std::string Designer::selectedId() { return selected ? selected->get("id") : model.window.get("name"); }
std::string Designer::selectedKind() { return kindInfo(nodeOf()->kind).name; }

bool Designer::gotoEvent(const std::string &event)
{
    Node *n = nodeOf();
    std::string kind = kindInfo(n->kind).name;
    const basic::ControlInfo *ci = basic::controlFor(kind);
    std::string ev = event.empty() && ci ? ci->defaultEvent : event;
    if (ci && ev.empty() && event.empty() && n->kind != Kind::VLayout && n->kind != Kind::HLayout && n->kind != Kind::Space) {
        // --- basic --- a control without events (Label, Canvas, ProgressBar): its code lives in
        // the form's events, so double-click opens Form_Load (a Canvas: Form_KeyDown if there's
        // code there already)
        idOf(n);
        if (modified) save();
        std::string fe = "Load";
        if (n->kind == Kind::Canvas) {
            std::vector<int> st = app.basicEventStates(path, model.window.get("name"), "window");
            const basic::ControlInfo *wi = basic::controlFor("window");
            for (size_t i = 0; wi && i < wi->events.size() && i < st.size(); i++)
                if (wi->events[i].name == "KeyDown" && st[i] == 2) fe = "KeyDown";
        }
        bool ok = app.basicGotoEvent(path, model.window.get("name"), "window", fe);
        app.setStatus(std::string("A ") + gui::paletteTitle(n->kind, true) + " has no events of its own: Form_" + fe +
                      (n->kind == Kind::Canvas ? " (draw from a Timer or the form's KeyDown)" : ""));
        refreshEvents();
        return ok;
    }
    if (!ci || ev.empty()) {
        setStatus(std::string("A ") + kindInfo(n->kind).title + " has no events.");
        return false;
    }
    std::string id = idOf(n);
    if (modified) save();   // the program reads the .afg: keep it in step with the code
    bool ok = app.basicGotoEvent(path, id, kind, ev);
    refreshEvents();
    return ok;
}

std::string Designer::eventsReport()
{
    Node *n = nodeOf();
    std::string kind = kindInfo(n->kind).name;
    const basic::ControlInfo *ci = basic::controlFor(kind);
    std::string out;
    if (!ci) return out;
    std::string id = n == &model.window ? model.window.get("name") : n->get("id");
    std::vector<int> st = app.basicEventStates(path, id, kind);
    for (size_t i = 0; i < ci->events.size(); i++) {
        int state = i < st.size() ? st[i] : 0;
        out += ci->events[i].name + "\t" + (state == 2 ? "code" : state == 1 ? "empty" : "none") + "\t" +
               ci->events[i].params + (ci->events[i].name == ci->defaultEvent ? "\tdefault" : "") + "\n";
    }
    return out;
}

void Designer::select(Node *n)
{
    selected = n;
    selectedProp = -1;
    refreshTree();
    refreshProps();
}

void Designer::changed(bool structure)
{
    modified = true;
    if (structure) refreshTree();
    else {
        // just the selected row's texts
        refreshTree();
    }
    refreshProps();
    openPreview();
    auto problems = model.validate();
    setStatus(problems.empty() ? "Changed - Save or Generate Code when it's ready." : "Problem: " + problems.front());
}

void Designer::addObject()
{
    ::Node *pn = palette.selected();
    int pi = palette.indexOf(pn);
    if (pi < 0 || pi >= (int)paletteKinds.size()) { setStatus("Choose what to add in the palette."); return; }
    addKind(paletteKinds[pi]);
}

void Designer::addKind(Kind k)
{
    Node *at = nodeOf();
    Node *n = nullptr;
    // --- basic --- where it may go (GuiModel's canHold): into the selection if it's a group
    // that takes it, else after the selection or after the nearest group around it that can
    // be followed by it; a menu after the selected menu or as a new title; a control into the
    // first page of selected tabs; else at the end of the root layout
    if (k == Kind::Menu) {
        if (selected && selected->kind == Kind::Menu) n = selected->parent->add(k, selected->indexInParent() + 1);
        else n = model.window.add(k);
        n->set("text", "Menu");
    } else if (selected && kindInfo(at->kind).container && canHold(at, k)) {
        n = at->add(k);
    } else if (selected && at->kind == Kind::Tabs && k != Kind::Tool) {
        if (at->children.empty()) at->add(Kind::VLayout)->set("title", "Page 1");
        n = at->children[0]->add(k);
    } else {
        for (Node *c = selected; c && !n; c = c->parent)
            if (c->parent && c->parent != &model.window && canHold(c->parent, k))
                n = c->parent->add(k, c->indexInParent() + 1);
        if (!n && k == Kind::Tool) {
            setStatus("A Toolbar button goes in a Toolbar: select the toolbar (or one of its buttons) first.");
            return;
        }
        if (!n) n = model.root()->add(k);
    }
    n->set("id", model.uniqueId(k));
    // --- basic --- new objects that hold others start with something in them
    if (k == Kind::Tabs)
        for (int i = 1; i <= 2; i++) {
            Node *pg = n->add(Kind::VLayout);
            pg->set("title", "Page " + std::to_string(i));
        }
    if (k == Kind::Toolbar) {
        Node *t = n->add(Kind::Tool);
        t->set("id", model.uniqueId(Kind::Tool));
        t->set("text", "Tool");
    }
    if (k == Kind::Tool) n->set("text", "Tool");
    if (n->parent && n->parent->kind == Kind::Tabs && n->get("title").empty())
        n->set("title", "Page " + std::to_string(n->indexInParent() + 1));
    selected = n;
    selectedProp = -1;
    changed(true);
    setStatus(std::string("Added a ") + paletteTitle(k, basicMode) + " - set its properties on the right.");
}

// --- basic --- the Menu Editor (a modal dialog); afterwards the form has the new menus
void Designer::menuEditor()
{
    if (!basicMode) return;
    if (selected && selected->kind == Kind::Menu) selected = nullptr;   // the menus are made again
    pvLastPick = nullptr;
    std::string sel;
    bool changedMenus = basicMenuEditor(app, model, sel);
    Node *pick = sel.empty() ? nullptr : model.findById(sel);
    if (changedMenus) {
        selected = pick ? pick : selected;
        selectedProp = -1;
        changed(true);
        setStatus("The menus changed. Double-click a menu in the tree (or choose it in the preview) to write its code.");
    } else {
        select(pick ? pick : selected);
        openPreview();   // its menu strip pointed at the old menus
    }
    toFront();
}

void Designer::removeObject()
{
    if (!selected || selected == model.root()) return;
    Node *parent = selected->parent;
    int i = selected->indexInParent();
    selected->detach();   // the unique_ptr frees it
    selectedProp = -1;
    selected = parent->children.empty() ? parent : parent->children[std::min<int>(i, (int)parent->children.size() - 1)].get();
    changed(true);
}

// 0 up, 1 down, 2 into the group above, 3 out of its group
void Designer::move(int how)
{
    Node *n = selected;
    if (!n || n == model.root()) return;
    Node *parent = n->parent;
    int i = n->indexInParent();
    if (how == 0 || how == 1) {
        int j = how == 0 ? i - 1 : i + 1;
        if (j < 0 || j >= (int)parent->children.size()) return;
        if (parent == &model.window && j == 0) return;   // --- basic --- menus stay after the root layout
        std::swap(parent->children[i], parent->children[j]);
    } else if (how == 2) {
        if (i == 0) { setStatus("There's no group above it to move it into."); return; }
        Node *target = parent->children[i - 1].get();
        if (!kindInfo(target->kind).container) { setStatus("The object above isn't a group."); return; }
        std::string why;   // --- basic --- menus, toolbars, tabs
        if (!canHold(target, n->kind, &why)) { setStatus("It can't go there: " + why + "."); return; }
        auto mine = n->detach();
        mine->parent = target;
        target->children.push_back(std::move(mine));
    } else {
        Node *grand = parent->parent;
        if (!grand || (grand->kind == Kind::Window && n->kind != Kind::Menu)) { setStatus("It's already in the outermost group."); return; }
        std::string why;   // --- basic ---
        if (!canHold(grand, n->kind, &why)) { setStatus("It can't go there: " + why + "."); return; }
        int pi = parent->indexInParent();
        auto mine = n->detach();
        mine->parent = grand;
        grand->children.insert(grand->children.begin() + pi + 1, std::move(mine));
    }
    changed(true);
}

void Designer::applyValue()
{
    if (selectedProp < 0) return;
    uint32 v = 0;
    IIntuition->GetAttr(STRINGA_TextVal, g[DG_VALUE], &v);
    std::string value = v ? (const char *)v : "";
    std::string err;
    if (!setProperty(propRows[selectedProp]->key, value, err)) setStatus(err);
}

bool Designer::setProperty(const std::string &key, const std::string &valueIn, std::string &err)
{
    Node *n = nodeOf();
    const PropInfo *p = nullptr;
    for (auto &pi : kindInfo(n->kind).props)
        if (key == pi.key && (basicMode || !pi.basic)) p = &pi;
    if (!p) { err = "A " + std::string(kindInfo(n->kind).title) + " has no property " + key; return false; }
    std::string value = valueIn;
    if (p->type == PropType::Ident && !value.empty() && identifier(value) != value) {
        err = "\"" + value + "\" isn't a valid " + (basicMode ? "name" : "C name") + ": use letters, digits and _.";
        return false;
    }
    if (p->type == PropType::Bool) value = (value == "1" || value == "Yes" || value == "yes" || value == "y") ? "1" : "0";
    // --- basic --- a control's new name: its event subs follow (if the user wants)
    std::string oldId = p->type == PropType::Ident ? n->get(p->key) : "";
    n->set(p->key, value);
    changed(false);
    if (basicMode && p->type == PropType::Ident && !oldId.empty() && oldId != value) {
        app.basicRenameControl(path, oldId, value, kindInfo(n->kind).name, !app.mcpQuiet);
        refreshEvents();
    }
    return true;
}

void Designer::toggleOrCycle()
{
    if (selectedProp < 0) return;
    const PropInfo *p = propRows[selectedProp];
    Node *n = nodeOf();
    if (p->type == PropType::Bool) n->set(p->key, n->getBool(p->key) ? "0" : "1");
    else if (p->type == PropType::Choice) {
        auto choices = splitItems(p->choices);
        std::string cur = n->get(p->key);
        size_t i = 0;
        while (i < choices.size() && choices[i] != cur) i++;
        n->set(p->key, choices[(i + 1) % choices.size()]);
    } else return;
    changed(false);
}

bool Designer::save()
{
    if (!writeFile(path, model.write())) {
        app.info("Can't write " + path);
        return false;
    }
    modified = false;
    setStatus("Saved " + path);
    return true;
}

void Designer::generate()
{
    if (basicMode) {   // --- basic --- "View Code"
        if (modified) save();
        if (app.basicOpenCode(path, true) && app.win) {
            IIntuition->WindowToFront(app.win);
            IIntuition->ActivateWindow(app.win);
        }
        return;
    }
    auto problems = model.validate();
    if (!problems.empty()) {
        app.info("The code can't be generated yet:\n" + problems.front());
        return;
    }
    if (!save()) return;
    std::string c, h, err;
    if (!app.generateGuiFiles(path, c, h, err)) {
        app.info(err);
        return;
    }
    // add the files to the project, if they're in its drawer
    if (!app.project.path.empty()) {
        bool added = false;
        for (const std::string &f : { c, h, path }) {
            if (app.project.contains(f)) continue;
            std::string rel = app.project.rel(f);
            if (rel == f) continue;   // not under the project's drawer
            app.project.files.push_back(rel);
            added = true;
        }
        if (added) {
            app.saveProject();
            app.refreshProjectList();
        }
    }
    setStatus("Wrote " + fileName(c) + " and " + fileName(h) + ". Your code goes between the USER-BEGIN/END lines in " +
              fileName(c) + ".");
}

// --- the preview ---------------------------------------------------------------------------

Object *Designer::build(Node *n)
{
    std::vector<struct TagItem> t;
    auto tag = [&](uint32 k, uint32 v) { t.push_back({ k, v }); };
    const KindInfo &ki = kindInfo(n->kind);
    if (ki.gadget) {
        pvNodes.push_back(n);
        tag(GA_ID, PREVIEW_ID_BASE + (uint32)pvNodes.size() - 1);
        tag(GA_RelVerify, TRUE);
        if (n->getBool("disabled")) tag(GA_Disabled, TRUE);
        if (n->has("hint")) tag(GA_HintInfo, (uint32)keep(n->get("hint")));
    }
    Class *cls = nullptr;
    auto labels = [&]() {
        pvArrays.emplace_back();
        for (auto &s : splitItems(n->get("items"))) pvArrays.back().push_back(keep(s));
        pvArrays.back().push_back(nullptr);
        return (uint32)pvArrays.back().data();
    };
    switch (n->kind) {
    case Kind::VLayout:
    case Kind::HLayout: {
        cls = ILayout->LAYOUT_GetClass();
        tag(LAYOUT_Orientation, n->kind == Kind::VLayout ? LAYOUT_ORIENT_VERT : LAYOUT_ORIENT_HORIZ);
        tag(LAYOUT_SpaceOuter, n == model.root());
        bool page = n->parent && n->parent->kind == Kind::Tabs;   // --- basic --- its title is the tab's
        std::string frame = n->get("frame");
        if (n->has("title") && frame == "none" && !page) frame = "group";
        if (frame == "group") tag(LAYOUT_BevelStyle, BVS_GROUP);
        else if (frame == "button") tag(LAYOUT_BevelStyle, BVS_BUTTON);
        else if (frame == "field") tag(LAYOUT_BevelStyle, BVS_FIELD);
        if (n->has("title") && !page) tag(LAYOUT_Label, (uint32)keep(n->get("title")));
        if (n->getBool("even")) tag(LAYOUT_EvenSize, TRUE);
        bool bars = false;
        for (auto &c : n->children) {
            Object *o = build(c.get());
            if (!o) continue;
            if (bars && n->getBool("splitter")) tag(LAYOUT_WeightBar, TRUE);   // --- basic --- between the children
            bars = true;
            tag(c->kind == Kind::Label ? LAYOUT_AddImage : LAYOUT_AddChild, (uint32)o);
            if (c->has("label"))
                tag(CHILD_Label, (uint32)IIntuition->NewObject(ILabel->LABEL_GetClass(), nullptr,
                                                               LABEL_Text, (uint32)keep(c->get("label")), TAG_DONE));
            if (c->has("weight"))
                tag(n->kind == Kind::HLayout ? CHILD_WeightedWidth : CHILD_WeightedHeight, (uint32)c->getInt("weight"));
            if (c->has("minwidth")) tag(CHILD_MinWidth, (uint32)c->getInt("minwidth"));
            if (c->has("minheight")) tag(CHILD_MinHeight, (uint32)c->getInt("minheight"));
            if (c->kind == Kind::Canvas) {   // --- basic --- at least width x height
                tag(CHILD_MinWidth, (uint32)std::max(16, c->getInt("width")));
                tag(CHILD_MinHeight, (uint32)std::max(16, c->getInt("height")));
            }
            // --- basic --- bars take their own height only
            if (!c->has("weight") && (c->kind == Kind::Toolbar || c->kind == Kind::StatusBar))
                tag(n->kind == Kind::HLayout ? CHILD_WeightedWidth : CHILD_WeightedHeight, 0);
            if ((c->kind == Kind::Grid || c->kind == Kind::TreeView) && !c->has("minheight")) tag(CHILD_MinHeight, 60);
        }
        break;
    }
    case Kind::Button:
        cls = IButton->BUTTON_GetClass();
        tag(GA_Text, (uint32)keep(n->get("text")));
        break;
    case Kind::String:
        cls = IString->STRING_GetClass();
        tag(STRINGA_MaxChars, (uint32)std::max(1, n->getInt("maxchars")));
        tag(STRINGA_TextVal, (uint32)keep(n->get("text")));
        break;
    case Kind::Integer:
        cls = IInteger->INTEGER_GetClass();
        tag(INTEGER_Minimum, (uint32)n->getInt("min"));
        tag(INTEGER_Maximum, (uint32)n->getInt("max"));
        tag(INTEGER_Number, (uint32)n->getInt("value"));
        break;
    case Kind::CheckBox:
        cls = ICheckBox->CHECKBOX_GetClass();
        tag(GA_Text, (uint32)keep(n->get("text")));
        tag(CHECKBOX_Checked, n->getBool("checked"));
        break;
    case Kind::Chooser:
        cls = IChooser->CHOOSER_GetClass();
        tag(CHOOSER_PopUp, TRUE);
        tag(CHOOSER_LabelArray, labels());
        tag(CHOOSER_Selected, (uint32)n->getInt("selected"));
        break;
    case Kind::Radio:
        cls = IRadioButton->RADIOBUTTON_GetClass();
        tag(RADIOBUTTON_LabelArray, labels());
        tag(RADIOBUTTON_Selected, (uint32)n->getInt("selected"));
        break;
    case Kind::Slider:
        cls = ISlider->SLIDER_GetClass();
        tag(SLIDER_Min, (uint32)n->getInt("min"));
        tag(SLIDER_Max, (uint32)n->getInt("max"));
        tag(SLIDER_Level, (uint32)n->getInt("level"));
        tag(SLIDER_Orientation, n->getBool("vertical") ? SLIDER_VERTICAL : SLIDER_HORIZONTAL);
        break;
    case Kind::Scroller:
        cls = IScroller->SCROLLER_GetClass();
        tag(SCROLLER_Total, (uint32)n->getInt("total"));
        tag(SCROLLER_Visible, (uint32)n->getInt("visible"));
        tag(SCROLLER_Top, (uint32)n->getInt("top"));
        tag(SCROLLER_Orientation, n->getBool("vertical") ? SORIENT_VERT : SORIENT_HORIZ);
        break;
    case Kind::ListBrowser: {
        cls = IListBrowser->LISTBROWSER_GetClass();
        struct List *l = new struct List;
        IExec->NewList(l);
        for (auto &s : splitItems(n->get("items"))) {
            ::Node *ln = IListBrowser->AllocListBrowserNode(1, LBNCA_CopyText, TRUE, LBNCA_Text, (uint32)s.c_str(), TAG_DONE);
            if (ln) IExec->AddTail(l, ln);
        }
        pvLists.push_back(l);
        tag(LISTBROWSER_Labels, (uint32)l);
        tag(LISTBROWSER_ShowSelected, TRUE);
        break;
    }
    case Kind::Label:
        cls = ILabel->LABEL_GetClass();
        tag(LABEL_Text, (uint32)keep(n->get("text")));
        break;
    case Kind::Space:
        cls = ISpace->SPACE_GetClass();
        break;
    case Kind::FuelGauge:
        cls = IFuelGauge->FUELGAUGE_GetClass();
        tag(FUELGAUGE_Min, (uint32)n->getInt("min"));
        tag(FUELGAUGE_Max, (uint32)n->getInt("max"));
        tag(FUELGAUGE_Level, (uint32)n->getInt("level"));
        tag(FUELGAUGE_Percent, TRUE);
        tag(FUELGAUGE_Justification, FGJ_CENTER);
        break;
    case Kind::GetFile:
        cls = IGetFile->GETFILE_GetClass();
        tag(GETFILE_TitleText, (uint32)keep(n->get("title")));
        if (n->has("pattern")) {
            tag(GETFILE_Pattern, (uint32)keep(n->get("pattern")));
            tag(GETFILE_DoPatterns, TRUE);
        }
        break;
    case Kind::TextEditor:
        cls = ITextEditor->TEXTEDITOR_GetClass();
        tag(GA_TEXTEDITOR_Contents, (uint32)keep(n->get("text")));
        break;
    case Kind::Canvas:   // --- basic --- a box of its size, labelled with its id
        cls = IButton->BUTTON_GetClass();
        tag(GA_ReadOnly, TRUE);
        tag(GA_Text, (uint32)keep("Canvas " + n->get("id") + " (" + n->get("width") + " x " + n->get("height") + ")"));
        break;
    // --- basic --- docs/ui.md's controls, as near as ReAction shows them
    case Kind::Toolbar: {   // a row of buttons (the runtime uses speedbar.gadget)
        cls = ILayout->LAYOUT_GetClass();
        tag(LAYOUT_Orientation, n->getBool("vertical") ? LAYOUT_ORIENT_VERT : LAYOUT_ORIENT_HORIZ);
        tag(LAYOUT_SpaceInner, FALSE);
        tag(LAYOUT_BevelStyle, BVS_BUTTON);
        for (auto &c : n->children) {
            Node *tl = c.get();
            if (tl->getBool("separator")) {
                tag(LAYOUT_AddChild, (uint32)IIntuition->NewObject(ISpace->SPACE_GetClass(), nullptr, TAG_DONE));
                tag(CHILD_MinWidth, 8);
                tag(CHILD_WeightedWidth, 0);
                continue;
            }
            // the runtime shows the text when a picture is missing: the preview shows it always
            std::string text = tl->get("text");
            if (text.empty()) {
                std::string img = tl->get("image");
                size_t sl = img.find_last_of(":/");
                text = img.empty() ? (text.empty() ? std::string("Tool") : text) : "[" + img.substr(sl == std::string::npos ? 0 : sl + 1) + "]";
            }
            Object *b = IIntuition->NewObject(IButton->BUTTON_GetClass(), nullptr,
                GA_ID, pvId(tl), GA_RelVerify, TRUE, GA_Text, (uint32)keep(text),
                GA_Disabled, tl->getBool("disabled") || n->getBool("disabled"),
                tl->has("hint") ? GA_HintInfo : TAG_IGNORE, (uint32)keep(tl->get("hint")),
                BUTTON_PushButton, tl->getBool("toggle"), GA_Selected, tl->getBool("toggle") && tl->getBool("checked"),
                TAG_DONE);
            if (!b) continue;
            tag(LAYOUT_AddChild, (uint32)b);
            tag(CHILD_WeightedWidth, 0);
        }
        if (!n->getBool("vertical")) tag(LAYOUT_AddChild, (uint32)IIntuition->NewObject(ISpace->SPACE_GetClass(), nullptr, TAG_DONE));
        break;
    }
    case Kind::StatusBar: {   // recessed panels
        cls = ILayout->LAYOUT_GetClass();
        tag(LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ);
        tag(LAYOUT_SpaceInner, FALSE);
        auto widths = splitItems(n->get("widths"));
        auto panels = splitItems(n->get("panels"));
        if (panels.empty()) panels.push_back("");
        for (size_t i = 0; i < panels.size(); i++) {
            tag(LAYOUT_AddChild, (uint32)IIntuition->NewObject(IButton->BUTTON_GetClass(), nullptr,
                GA_ReadOnly, TRUE, GA_Text, (uint32)keep(panels[i]), BUTTON_Justification, BCJ_LEFT,
                BUTTON_BevelStyle, BVS_FIELD, TAG_DONE));
            int wpx = i < widths.size() ? atoi(widths[i].c_str()) : 0;
            if (wpx > 0) {
                tag(CHILD_MinWidth, (uint32)wpx);
                tag(CHILD_WeightedWidth, 0);
            }
        }
        break;
    }
    case Kind::Image: {   // a box with the picture's name
        std::string f = n->get("file");
        size_t sl = f.find_last_of(":/");
        cls = IButton->BUTTON_GetClass();
        tag(GA_ReadOnly, TRUE);
        tag(GA_Text, (uint32)keep("Image " + (f.empty() ? std::string("(no file)") : f.substr(sl == std::string::npos ? 0 : sl + 1))));
        if (n->getBool("frame")) tag(BUTTON_BevelStyle, BVS_FIELD);
        break;
    }
    case Kind::Tabs: {   // clicktab.gadget with a page per child layout
        struct List *labels = new struct List;
        IExec->NewList(labels);
        pvTabLists.push_back(labels);
        Object *pg = IIntuition->NewObject(ILayout->PAGE_GetClass(), nullptr, TAG_DONE);
        int i = 0;
        for (auto &c : n->children) {
            Object *o = build(c.get());
            if (!o) continue;
            ::Node *tn = IClickTab->AllocClickTabNode(TNA_Text, (uint32)keep(c->get("title").empty() ? "Page" : c->get("title")),
                                                      TNA_Number, i++, TAG_DONE);
            if (tn) IExec->AddTail(labels, tn);
            if (pg) IIntuition->SetAttrs(pg, PAGE_Add, (uint32)o, TAG_DONE);
        }
        cls = IClickTab->CLICKTAB_GetClass();
        tag(CLICKTAB_Labels, (uint32)labels);
        if (pg) tag(CLICKTAB_PageGroup, (uint32)pg);
        tag(CLICKTAB_Current, (uint32)std::max(0, std::min(n->getInt("selected"), i - 1)));
        break;
    }
    case Kind::TreeView: {   // a hierarchical list with sample nodes
        cls = IListBrowser->LISTBROWSER_GetClass();
        struct List *l = new struct List;
        IExec->NewList(l);
        struct { const char *t; int gen; bool kids; } sample[] = { { "Node 1", 1, true }, { "Child 1", 2, false },
                                                                  { "Child 2", 2, false }, { "Node 2", 1, false } };
        for (auto &sm : sample) {
            ::Node *ln = IListBrowser->AllocListBrowserNode(1, LBNA_Generation, sm.gen,
                LBNA_Flags, sm.kids ? LBFLG_HASCHILDREN | LBFLG_SHOWCHILDREN : 0, LBNCA_CopyText, TRUE,
                LBNCA_Text, (uint32)sm.t, TAG_DONE);
            if (ln) IExec->AddTail(l, ln);
        }
        pvLists.push_back(l);
        tag(LISTBROWSER_Labels, (uint32)l);
        tag(LISTBROWSER_Hierarchical, TRUE);
        tag(LISTBROWSER_ShowSelected, TRUE);
        break;
    }
    case Kind::Grid: {   // a list with the grid's columns (up to 8) and a few rows
        cls = IListBrowser->LISTBROWSER_GetClass();
        int cols = std::max(1, std::min(n->getInt("cols"), 8));
        int fixedCols = std::max(0, std::min(n->getInt("fixedcols"), cols));
        bool headers = n->getBool("headers");
        std::vector<struct TagItem> ct;
        for (int c = 0; c < cols; c++) {
            int dataCol = c - fixedCols;
            std::string title = c < fixedCols ? "" : headers ? std::string(1, (char)('A' + dataCol % 26)) : "";
            ct.push_back({ LBCIA_Column, (uint32)c });
            ct.push_back({ LBCIA_Title, (uint32)keep(title) });
            ct.push_back({ LBCIA_Width, (uint32)(c < fixedCols ? 24 : std::max(16, n->getInt("colwidth"))) });
        }
        ct.push_back({ TAG_DONE, 0 });
        struct ColumnInfo *ci = IListBrowser->AllocLBColumnInfoA(cols, ct.data());
        if (ci) pvColumns.push_back(ci);
        struct List *l = new struct List;
        IExec->NewList(l);
        int rows = std::max(0, std::min(n->getInt("rows") - n->getInt("fixedrows"), 5));
        for (int r = 0; r < rows; r++) {
            std::vector<struct TagItem> nt;
            for (int c = 0; c < cols; c++) {
                nt.push_back({ LBNA_Column, (uint32)c });
                nt.push_back({ LBNCA_CopyText, TRUE });
                nt.push_back({ LBNCA_Text, (uint32)keep(c < fixedCols && headers ? std::to_string(r + 1) : "") });
            }
            nt.push_back({ TAG_DONE, 0 });
            ::Node *ln = IListBrowser->AllocListBrowserNodeA(cols, nt.data());
            if (ln) IExec->AddTail(l, ln);
        }
        pvLists.push_back(l);
        tag(LISTBROWSER_Labels, (uint32)l);
        if (ci) tag(LISTBROWSER_ColumnInfo, (uint32)ci);
        tag(LISTBROWSER_ColumnTitles, n->getInt("fixedrows") > 0);
        tag(LISTBROWSER_HorizontalProp, TRUE);
        break;
    }
    default:
        return nullptr;
    }
    tag(TAG_DONE, 0);
    return cls ? IIntuition->NewObjectA(cls, nullptr, t.data()) : nullptr;
}

uint32 Designer::pvId(Node *n)
{
    pvNodes.push_back(n);
    return PREVIEW_ID_BASE + (uint32)pvNodes.size() - 1;
}

// --- basic --- the form's menus as the preview's menu strip (gadtools NewMenu): titles,
// items and sub-items; hidden ones (context menus) aren't in it
void Designer::buildMenus()
{
    pvMenu.clear();
    pvMenuNodes.clear();
    std::function<void(Node *, int)> add = [&](Node *m, int level) {
        if (m->getBool("hidden") || level > 2) return;
        struct NewMenu nm = {};
        nm.nm_Type = level == 0 ? NM_TITLE : level == 1 ? NM_ITEM : NM_SUB;
        bool bar = m->get("text") == "-";
        nm.nm_Label = bar && level > 0 ? NM_BARLABEL : (STRPTR)keep(m->get("text"));
        std::string key = m->get("key");
        if (key.size() == 1) nm.nm_CommKey = (STRPTR)keep(key);
        if (m->getBool("disabled")) nm.nm_Flags |= level == 0 ? NM_MENUDISABLED : NM_ITEMDISABLED;
        if (level > 0 && (m->getBool("checkable") || m->getBool("radio") || m->getBool("checked"))) {
            nm.nm_Flags |= CHECKIT | (m->getBool("radio") ? 0 : MENUTOGGLE);
            if (m->getBool("checked")) nm.nm_Flags |= CHECKED;
        }
        if (level > 0 && m->getBool("radio") && m->parent) {   // the other radio items of its panel
            int i = 0;
            for (auto &sib : m->parent->children) {
                if (sib->getBool("hidden")) continue;
                if (sib.get() != m && sib->getBool("radio") && i < 32) nm.nm_MutualExclude |= 1 << i;
                i++;
            }
        }
        if (!bar) {
            pvMenuNodes.push_back(m);
            nm.nm_UserData = (APTR)(uint32)pvMenuNodes.size();
        }
        pvMenu.push_back(nm);
        for (auto &c : m->children) add(c.get(), level + 1);
    };
    for (Node *m : model.menus()) add(m, 0);
    if (!pvMenu.empty()) {
        struct NewMenu end = {};
        end.nm_Type = NM_END;
        pvMenu.push_back(end);
    }
}

void Designer::openPreview()
{
    closePreview();
    pvNodes.clear();
    Object *root = build(model.root());
    if (!root) return;
    if (basicMode) buildMenus();   // --- basic ---
    int w = model.window.getInt("width"), h = model.window.getInt("height");
    if (pvLeft < 0) {
        pvLeft = win ? win->LeftEdge + win->Width + 10 : 100;
        pvTop = win ? win->TopEdge : 60;
        if (app.win && pvLeft + 200 > app.win->WScreen->Width) pvLeft = app.win->WScreen->Width - 360;
    }
    static std::string title;
    title = "Preview: " + model.window.get("title");
    pvObj = IIntuition->NewObject(IWindow->WINDOW_GetClass(), nullptr,
        WA_Title, (uint32)title.c_str(), WA_DragBar, TRUE, WA_CloseGadget, TRUE, WA_DepthGadget, TRUE,
        WA_SizeGadget, model.window.getBool("resizable") && !model.window.getBool("toolwindow"), WA_Activate, FALSE,
        WA_DepthGadget, !model.window.getBool("toolwindow"),   // --- basic ---
        WA_PubScreen, (uint32)app.win->WScreen, WA_Left, pvLeft, WA_Top, pvTop,
        w > 0 ? WA_InnerWidth : TAG_IGNORE, w, h > 0 ? WA_InnerHeight : TAG_IGNORE, h,
        WA_IDCMP, IDCMP_GADGETUP | IDCMP_CLOSEWINDOW | IDCMP_MENUPICK,
        WINDOW_GadgetHelp, TRUE,
        pvMenu.empty() ? TAG_IGNORE : WINDOW_NewMenu, (uint32)pvMenu.data(),   // --- basic ---
        WINDOW_Layout, root,
        TAG_DONE);
    if (!pvObj) return;
    pvWin = (struct Window *)IIntuition->IDoMethod(pvObj, WM_OPEN);
    if (!pvWin) {
        IIntuition->DisposeObject(pvObj);
        pvObj = nullptr;
        setStatus("The preview window doesn't fit: make it smaller or give it a size.");
    }
}

void Designer::closePreview()
{
    if (pvWin) { pvLeft = pvWin->LeftEdge; pvTop = pvWin->TopEdge; }
    if (pvObj) IIntuition->DisposeObject(pvObj);
    pvObj = nullptr;
    pvWin = nullptr;
    for (auto *l : pvLists) {
        IListBrowser->FreeListBrowserList(l);
        delete l;
    }
    pvLists.clear();
    for (auto *l : pvTabLists) {   // --- basic ---
        ::Node *tn;
        while ((tn = IExec->RemHead(l))) IClickTab->FreeClickTabNode(tn);
        delete l;
    }
    pvTabLists.clear();
    for (auto *ci : pvColumns) IListBrowser->FreeLBColumnInfo(ci);
    pvColumns.clear();
    pvMenuNodes.clear();
    pvStrings.clear();
    pvArrays.clear();
}

// --- events --------------------------------------------------------------------------------

void Designer::handle(uint32 got)
{
    uint32 ws = 0, ps = 0;
    if (winObj) IIntuition->GetAttr(WINDOW_SigMask, winObj, &ws);
    if (pvObj) IIntuition->GetAttr(WINDOW_SigMask, pvObj, &ps);
    if (pvObj && (got & ps)) {
        uint32 result;
        uint16 code;
        bool closePv = false;
        Node *pick = nullptr, *menuPick = nullptr;
        while ((result = IIntuition->IDoMethod(pvObj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
            if ((result & WMHI_CLASSMASK) == WMHI_CLOSEWINDOW) closePv = true;
            else if ((result & WMHI_CLASSMASK) == WMHI_MENUPICK) {   // --- basic --- a menu item: its Click sub
                struct Menu *strip = nullptr;
                IIntuition->GetAttr(WINDOW_MenuStrip, pvObj, (uint32 *)&strip);
                struct MenuItem *item = strip && code != MENUNULL ? IIntuition->ItemAddress(strip, code) : nullptr;
                uint32 ud = item ? (uint32)GTMENUITEM_USERDATA(item) : 0;
                if (ud >= 1 && ud <= pvMenuNodes.size()) menuPick = pvMenuNodes[ud - 1];
            }
            else if ((result & WMHI_CLASSMASK) == WMHI_GADGETUP) {
                uint32 id = (result & WMHI_GADGETMASK) - PREVIEW_ID_BASE;
                if (id < pvNodes.size()) pick = pvNodes[id];
            }
        }
        if (closePv) closePreview();
        else if (menuPick) {
            select(menuPick);
            gotoEvent("");
        } else if (pick) {
            // --- basic --- a second click on the same control in time is a double-click
            uint32 secs = 0, micros = 0;
            IIntuition->CurrentTime(&secs, &micros);
            bool dbl = basicMode && pick == pvLastPick && IIntuition->DoubleClick(pvLastSecs, pvLastMicros, secs, micros);
            pvLastPick = dbl ? nullptr : pick;
            pvLastSecs = secs;
            pvLastMicros = micros;
            select(pick);
            if (dbl) gotoEvent("");
            else setStatus("Selected " + pick->get("id") + (basicMode ? " (clicked in the preview; click again to write its code)."
                                                                       : " (clicked in the preview)."));
        }
    }
    if (!winObj || !(got & ws)) return;
    uint32 result;
    uint16 code;
    while (winObj && (result = IIntuition->IDoMethod(winObj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
        if ((result & WMHI_CLASSMASK) == WMHI_CLOSEWINDOW) { wantsClose = true; continue; }
        if ((result & WMHI_CLASSMASK) != WMHI_GADGETUP) continue;
        switch (result & WMHI_GADGETMASK) {
        case DG_PALETTE: {
            uint32 ev = 0;   // a list's double-click is its RelEvent, not the code
            IIntuition->GetAttr(LISTBROWSER_RelEvent, palette.obj, &ev);
            if (ev == LBRE_DOUBLECLICK) addObject();
            break;
        }
        case DG_ADD: addObject(); break;
        case DG_TREE: {
            int i = tree.indexOf(tree.selected());
            if (i >= 0 && i < (int)rows.size() && rows[i] != selected) {
                selected = rows[i];
                selectedProp = -1;
                refreshProps();
            }
            // --- basic --- double-click: the default event's code (Form_Load for the window)
            uint32 ev = 0;
            IIntuition->GetAttr(LISTBROWSER_RelEvent, tree.obj, &ev);
            if (basicMode && ev == LBRE_DOUBLECLICK) gotoEvent("");
            break;
        }
        case DG_EVENTS: {   // --- basic ---
            int i = events.indexOf(events.selected());
            if (i >= 0 && i < (int)eventRows.size()) gotoEvent(eventRows[i]);
            break;
        }
        case DG_REMOVE: removeObject(); break;
        case DG_UP: move(0); break;
        case DG_DOWN: move(1); break;
        case DG_INDENT: move(2); break;
        case DG_OUTDENT: move(3); break;
        case DG_PROPS: {
            int i = props.indexOf(props.selected());
            if (i < 0) break;
            bool again = i == selectedProp;
            selectedProp = i;
            Node *n = nodeOf();
            std::string v = n->get(propRows[i]->key);
            IIntuition->SetGadgetAttrs((struct Gadget *)g[DG_VALUE], win, nullptr, STRINGA_TextVal, (uint32)v.c_str(),
                                       GA_Disabled, FALSE, TAG_DONE);
            uint32 ev = 0;
            IIntuition->GetAttr(LISTBROWSER_RelEvent, props.obj, &ev);
            if (ev == LBRE_DOUBLECLICK || (again && (propRows[i]->type == PropType::Bool || propRows[i]->type == PropType::Choice)))
                toggleOrCycle();
            else if (propRows[i]->type != PropType::Bool && propRows[i]->type != PropType::Choice)
                IIntuition->IDoMethod(winObj, WM_ACTIVATEGADGET, g[DG_VALUE]);
            const PropInfo *p = propRows[i];
            if (p->type == PropType::Items) setStatus("Separate the items with |, e.g. One|Two|Three");
            else if (p->type == PropType::Choice) setStatus(std::string("Click again to choose: ") + p->choices);
            else if (p->type == PropType::Bool) setStatus("Click again to switch it.");
            else setStatus("Type the new value below and press Return.");
            break;
        }
        case DG_VALUE: applyValue(); break;
        case DG_SAVE: save(); break;
        case DG_MENUS: menuEditor(); break;   // --- basic ---
        case DG_GENERATE: generate(); break;
        case DG_PREVIEW:
            openPreview();
            if (pvWin) IIntuition->WindowToFront(pvWin);
            break;
        }
    }
}

// --- App -----------------------------------------------------------------------------------

void App::openDesigner(const std::string &afgIn)
{
    if (designer) {
        if (afgIn.empty()) return;   // already open: the menu just brings it up
        if (!designer->confirmClose()) return;
        closeDesigner();
    }
    std::string afg = afgIn;
    if (afg.empty()) {
        // the project's .afg if it has exactly one, otherwise ask (a new name makes a new GUI)
        std::vector<std::string> mine;
        for (auto &f : project.files)
            if (extension(f) == "afg") mine.push_back(project.abs(f));
        if (mine.size() == 1) afg = mine[0];
        else {
            std::string start = project.path.empty() ? joinPath(lastDir, "window.afg")
                                                     : (mine.empty() ? project.abs("window.afg") : mine[0]);
            afg = requestFile("GUI Designer - open a .afg file, or type a new name", true, start, "#?.afg");
            if (afg.empty()) return;
            if (extension(afg) != "afg") afg += ".afg";
        }
    }
    designer = new Designer(*this);
    if (!designer->open(absolutePath(afg))) {
        delete designer;
        designer = nullptr;
    }
}

void App::closeDesigner()
{
    delete designer;
    designer = nullptr;
}

uint32_t App::designerSignals() { return designer ? designer->signals() : 0; }

// --- basic --- another project was opened (or the project closed): the designer's form belongs
// to the old one, so it closes (asking about unsaved changes; under MCP they're saved)
void App::designerProjectChanged()
{
    if (!designer) return;
    if (!project.path.empty() && project.contains(designer->file())) return;
    if (mcpQuiet) designer->saveFile();
    else if (!designer->confirmClose()) return;
    closeDesigner();
}

std::string App::designerPath() { return designer ? designer->file() : ""; }
void App::designerToFront() { if (designer) designer->toFront(); }

// MCP/ARexx: DESIGNER commands on the open designer (McpPort.cpp). `what`: SELECT id,
// SELECTED, EVENTS, GOTOEVENT [event], SET key value, SAVE. Returns false with err.
bool App::designerCommand(const std::string &what, const std::string &arg1, const std::string &arg2,
                          std::string &out, std::string &err)
{
    if (!designer) { err = "the GUI designer isn't open"; return false; }
    if (what == "SELECT") {
        if (!designer->selectId(arg1)) { err = "no object with the id " + arg1; return false; }
        out = designer->selectedKind() + "\t" + designer->selectedId();
        return true;
    }
    if (what == "SELECTED") { out = designer->selectedKind() + "\t" + designer->selectedId(); return true; }
    if (what == "EVENTS") {
        if (!designer->isBasic()) { err = "events are for Boing BASIC forms"; return false; }
        out = designer->eventsReport();
        return true;
    }
    if (what == "GOTOEVENT") {
        if (!designer->isBasic()) { err = "events are for Boing BASIC forms"; return false; }
        if (!designer->gotoEvent(arg1)) { err = trim(statusLeft); return false; }
        int line = editor ? (int)editor->send(SCI_LINEFROMPOSITION, editor->send(SCI_GETCURRENTPOS)) + 1 : 0;
        out = cur ? cur->path + "\t" + std::to_string(line) + "\t" + mcpLastSub : "";
        return true;
    }
    if (what == "SET") return designer->setProperty(arg1, arg2, err);
    if (what == "ADD") {   // --- basic --- the palette's Add: after the selection, or into the selected group
        const gui::KindInfo *ki = gui::kindByName(arg1);
        if (!ki || ki->kind == gui::Kind::Window) { err = "no object kind " + arg1; return false; }
        designer->addKind(ki->kind);
        out = designer->selectedKind() + "\t" + designer->selectedId();
        return true;
    }
    if (what == "MENUEDITOR") {   // --- basic --- the dialog is modal: it opens after the reply
        if (!designer->isBasic()) { err = "menus are for Boing BASIC forms"; return false; }
        deferredWork.push_back([this]() { if (designer) designer->menuEditor(); });
        return true;
    }
    if (what == "SAVE") { if (!designer->saveFile()) { err = "can't save"; return false; } return true; }
    if (what == "FILE") { out = designer->file(); return true; }
    err = "unknown DESIGNER command " + what;
    return false;
}

void App::handleDesigner(uint32_t got)
{
    if (!designer) return;
    designer->handle(got);
    if (designer->wantsClose) {
        designer->wantsClose = false;
        if (designer->confirmClose()) closeDesigner();
    }
}

} // namespace af
