// Tools > GUI Designer: edit a .afg file (GuiModel.h) - a palette of ReAction objects, the
// window's layout as a tree, the selected object's properties - with a live preview window
// built from real ReAction objects, and "Generate Code" for <name>_gui.c/.h.
#include "App.h"
#include "GuiModel.h"

#include <deque>
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
#include <images/label.h>
#include <images/bevel.h>

namespace af {

using namespace gui;

enum {
    DG_PALETTE = 1, DG_ADD, DG_TREE, DG_REMOVE, DG_UP, DG_DOWN, DG_INDENT, DG_OUTDENT,
    DG_PROPS, DG_VALUE, DG_SAVE, DG_GENERATE, DG_PREVIEW, DG_STATUS, DG_N
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

    static std::string title;
    title = "GUI Designer - " + path;
    winObj = IIntuition->NewObject(IWindow->WINDOW_GetClass(), nullptr,
        WA_Title, (uint32)title.c_str(), WA_DragBar, TRUE, WA_CloseGadget, TRUE, WA_DepthGadget, TRUE,
        WA_SizeGadget, TRUE, WA_Activate, TRUE, WA_PubScreen, (uint32)app.win->WScreen,
        WA_Left, app.win->LeftEdge + 40, WA_Top, app.win->TopEdge + 30, WA_InnerWidth, 820, WA_InnerHeight, 460,
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
                LAYOUT_AddChild, btn(DG_SAVE, "_Save", "Save the .afg file"),
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, btn(DG_GENERATE, "_Generate Code", "Save, and write the C code for this window into the project"),
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
    palette.detach();
    for (Kind k : paletteKinds = gui::paletteKinds()) palette.add({ kindInfo(k).title });
    palette.attach(0);
    select(model.root());
    openPreview();
    setStatus(modified ? "A new GUI: add objects from the palette, then Generate Code."
                       : "Select an object to change it; the preview follows every change.");
    return true;
}

void Designer::close()
{
    closePreview();
    palette.win = tree.win = props.win = nullptr;
    if (winObj) IIntuition->DisposeObject(winObj);
    winObj = nullptr;
    win = nullptr;
    palette.obj = tree.obj = props.obj = nullptr;
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
    Kind k = paletteKinds[pi];
    Node *at = nodeOf();
    Node *n;
    if (!selected || kindInfo(at->kind).container) {
        Node *parent = selected ? at : model.root();
        n = parent->add(k);
    } else {
        n = at->parent->add(k, at->indexInParent() + 1);
    }
    n->set("id", model.uniqueId(k));
    selected = n;
    selectedProp = -1;
    changed(true);
    setStatus(std::string("Added a ") + kindInfo(k).title + " - set its properties on the right.");
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
        std::swap(parent->children[i], parent->children[j]);
    } else if (how == 2) {
        if (i == 0) { setStatus("There's no group above it to move it into."); return; }
        Node *target = parent->children[i - 1].get();
        if (!kindInfo(target->kind).container) { setStatus("The object above isn't a group."); return; }
        auto mine = n->detach();
        mine->parent = target;
        target->children.push_back(std::move(mine));
    } else {
        Node *grand = parent->parent;
        if (!grand || grand->kind == Kind::Window) { setStatus("It's already in the outermost group."); return; }
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
    const PropInfo *p = propRows[selectedProp];
    if (p->type == PropType::Ident && !value.empty() && identifier(value) != value) {
        setStatus("\"" + value + "\" isn't a valid C name: use letters, digits and _.");
        return;
    }
    if (p->type == PropType::Bool) value = (value == "1" || value == "Yes" || value == "yes" || value == "y") ? "1" : "0";
    nodeOf()->set(p->key, value);
    changed(false);
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
        std::string frame = n->get("frame");
        if (n->has("title") && frame == "none") frame = "group";
        if (frame == "group") tag(LAYOUT_BevelStyle, BVS_GROUP);
        else if (frame == "button") tag(LAYOUT_BevelStyle, BVS_BUTTON);
        else if (frame == "field") tag(LAYOUT_BevelStyle, BVS_FIELD);
        if (n->has("title")) tag(LAYOUT_Label, (uint32)keep(n->get("title")));
        if (n->getBool("even")) tag(LAYOUT_EvenSize, TRUE);
        for (auto &c : n->children) {
            Object *o = build(c.get());
            if (!o) continue;
            tag(c->kind == Kind::Label ? LAYOUT_AddImage : LAYOUT_AddChild, (uint32)o);
            if (c->has("label"))
                tag(CHILD_Label, (uint32)IIntuition->NewObject(ILabel->LABEL_GetClass(), nullptr,
                                                               LABEL_Text, (uint32)keep(c->get("label")), TAG_DONE));
            if (c->has("weight"))
                tag(n->kind == Kind::HLayout ? CHILD_WeightedWidth : CHILD_WeightedHeight, (uint32)c->getInt("weight"));
            if (c->has("minwidth")) tag(CHILD_MinWidth, (uint32)c->getInt("minwidth"));
            if (c->has("minheight")) tag(CHILD_MinHeight, (uint32)c->getInt("minheight"));
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
    default:
        return nullptr;
    }
    tag(TAG_DONE, 0);
    return cls ? IIntuition->NewObjectA(cls, nullptr, t.data()) : nullptr;
}

void Designer::openPreview()
{
    closePreview();
    pvNodes.clear();
    Object *root = build(model.root());
    if (!root) return;
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
        WA_SizeGadget, model.window.getBool("resizable"), WA_Activate, FALSE,
        WA_PubScreen, (uint32)app.win->WScreen, WA_Left, pvLeft, WA_Top, pvTop,
        w > 0 ? WA_InnerWidth : TAG_IGNORE, w, h > 0 ? WA_InnerHeight : TAG_IGNORE, h,
        WA_IDCMP, IDCMP_GADGETUP | IDCMP_CLOSEWINDOW,
        WINDOW_GadgetHelp, TRUE,
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
        Node *pick = nullptr;
        while ((result = IIntuition->IDoMethod(pvObj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
            if ((result & WMHI_CLASSMASK) == WMHI_CLOSEWINDOW) closePv = true;
            else if ((result & WMHI_CLASSMASK) == WMHI_GADGETUP) {
                uint32 id = (result & WMHI_GADGETMASK) - PREVIEW_ID_BASE;
                if (id < pvNodes.size()) pick = pvNodes[id];
            }
        }
        if (closePv) closePreview();
        else if (pick) {
            select(pick);
            setStatus("Selected " + pick->get("id") + " (clicked in the preview).");
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
