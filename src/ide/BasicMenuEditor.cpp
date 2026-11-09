// --- basic --- The GUI designer's Menu Editor for Boing BASIC forms, as Visual Basic's: the
// menus as one list (a title, its items indented under it, their sub-items indented again),
// and for the selected one its caption, name (for <name>_Click), shortcut, and the checkable,
// checked, radio, enabled and visible states. The arrows change the level and the order; Next
// goes to the next menu (a new one at the end), Insert puts a new one before the selected one.
// OK puts the list back into the form (Model::setMenuRows), Cancel leaves the form as it was.
#include "App.h"
#include "GuiModel.h"

#include <cctype>
#include <string.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/string.h>
#include <proto/checkbox.h>
#include <proto/label.h>
#include <proto/listbrowser.h>
#include <proto/space.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/string.h>
#include <gadgets/checkbox.h>
#include <gadgets/listbrowser.h>
#include <images/label.h>

namespace af {

using namespace gui;

namespace {

enum {
    ME_LIST = 1, ME_CAPTION, ME_NAME, ME_KEY, ME_CHECKABLE, ME_CHECKED, ME_RADIO, ME_ENABLED, ME_VISIBLE,
    ME_LEFT, ME_RIGHT, ME_UP, ME_DOWN, ME_NEXT, ME_INSERT, ME_DELETE, ME_OK, ME_CANCEL, ME_STATUS, ME_N
};

std::string prop(const MenuRow &r, const char *k)
{
    auto it = r.props.find(k);
    return it == r.props.end() ? "" : it->second;
}

bool flag(const MenuRow &r, const char *k)
{
    std::string v = prop(r, k);
    return !v.empty() && v != "0";
}

void setProp(MenuRow &r, const char *k, const std::string &v, const char *def = "")
{
    if (v == def) r.props.erase(k);
    else r.props[k] = v;
}

// "mnu" + the caption's words: "_Open..." -> mnuOpen, "Save as" -> mnuSaveAs
std::string nameFor(const std::string &caption)
{
    std::string n = "mnu";
    bool up = true;
    for (unsigned char c : caption) {
        if (isalnum(c) && c < 128) {
            n += up ? (char)toupper(c) : (char)c;
            up = false;
        } else if (c == ' ' || c == '-') up = true;
    }
    return n == "mnu" ? "" : n;
}

struct MenuEditor {
    App &app;
    Model &model;
    std::vector<MenuRow> rows;
    int cur = -1;
    Object *wo = nullptr;
    struct Window *w = nullptr;
    Object *g[ME_N] = {};
    ListView list{2};
    std::string status;

    MenuEditor(App &a, Model &m) : app(a), model(m), rows(m.menuRows()) {}

    std::string field(int id)
    {
        uint32 v = 0;
        IIntuition->GetAttr(STRINGA_TextVal, g[id], &v);
        return v ? (const char *)v : "";
    }
    bool checked(int id)
    {
        uint32 v = 0;
        IIntuition->GetAttr(GA_Selected, g[id], &v);
        return v != 0;
    }
    void set(int id, uint32 tag, uint32 value)
    {
        IIntuition->SetGadgetAttrs((struct Gadget *)g[id], w, nullptr, tag, value, TAG_DONE);
    }
    void say(const std::string &s)
    {
        status = s;
        set(ME_STATUS, GA_Text, (uint32)status.c_str());
    }

    // ids in use outside the menus (controls, the window) and in the other menu rows
    bool idTaken(const std::string &id, int except)
    {
        if (id.empty()) return false;
        std::vector<std::pair<Node *, int>> all;
        model.walk(all);
        for (auto &p : all)
            if (p.first->kind != Kind::Menu && upper(p.first->get("id")) == upper(id)) return true;
        for (int i = 0; i < (int)rows.size(); i++)
            if (i != except && upper(prop(rows[i], "id")) == upper(id)) return true;
        return false;
    }

    // the fields into rows[cur]
    void commit()
    {
        if (cur < 0 || cur >= (int)rows.size()) return;
        MenuRow &r = rows[cur];
        std::string caption = field(ME_CAPTION), name = field(ME_NAME), key = field(ME_KEY);
        setProp(r, "text", caption, "Menu");
        if (name.empty() && caption != "-" && !caption.empty()) {   // VB makes you name it; we do it for you
            std::string base = nameFor(caption), n = base;
            for (int i = 2; !base.empty() && idTaken(n, cur); i++) n = base + std::to_string(i);
            name = n;
        }
        if (!name.empty() && identifier(name) != name) {
            say("\"" + name + "\" isn't a valid name: use letters, digits and _.");
            name = prop(r, "id");
        } else if (idTaken(name, cur)) {
            say("The name \"" + name + "\" is already used.");
            name = prop(r, "id");
        }
        setProp(r, "id", name);
        setProp(r, "key", key);
        setProp(r, "checkable", checked(ME_CHECKABLE) ? "1" : "0", "0");
        setProp(r, "checked", checked(ME_CHECKED) ? "1" : "0", "0");
        setProp(r, "radio", checked(ME_RADIO) ? "1" : "0", "0");
        setProp(r, "disabled", checked(ME_ENABLED) ? "0" : "1", "0");
        setProp(r, "hidden", checked(ME_VISIBLE) ? "0" : "1", "0");
    }

    // rows[cur] into the fields
    void load()
    {
        bool any = cur >= 0 && cur < (int)rows.size();
        static MenuRow none;
        const MenuRow &r = any ? rows[cur] : none;
        static std::string caption, name, key;
        caption = any ? (r.props.count("text") ? prop(r, "text") : "Menu") : "";
        name = prop(r, "id");
        key = prop(r, "key");
        set(ME_CAPTION, STRINGA_TextVal, (uint32)caption.c_str());
        set(ME_NAME, STRINGA_TextVal, (uint32)name.c_str());
        set(ME_KEY, STRINGA_TextVal, (uint32)key.c_str());
        set(ME_CHECKABLE, GA_Selected, flag(r, "checkable"));
        set(ME_CHECKED, GA_Selected, flag(r, "checked"));
        set(ME_RADIO, GA_Selected, flag(r, "radio"));
        set(ME_ENABLED, GA_Selected, !flag(r, "disabled"));
        set(ME_VISIBLE, GA_Selected, !flag(r, "hidden"));
        for (int id : { ME_CAPTION, ME_NAME, ME_KEY, ME_CHECKABLE, ME_CHECKED, ME_RADIO, ME_ENABLED, ME_VISIBLE,
                        ME_LEFT, ME_RIGHT, ME_UP, ME_DOWN, ME_DELETE })
            set(id, GA_Disabled, !any);
    }

    void refreshList()
    {
        list.detach();
        IListBrowser->FreeListBrowserList(list.list);
        IExec->NewList(list.list);
        for (auto &r : rows) {
            std::string t(r.level * 4, '.');
            std::string c = r.props.count("text") ? prop(r, "text") : "Menu";
            t += c == "-" ? "--------" : c;
            std::string extra = prop(r, "key").empty() ? "" : "Amiga-" + prop(r, "key");
            if (flag(r, "hidden")) extra += extra.empty() ? "hidden" : ", hidden";
            if (flag(r, "disabled")) extra += extra.empty() ? "disabled" : ", disabled";
            list.add({ t, extra });
        }
        list.attach(cur);
        if (cur >= 0) list.makeVisible(cur);
    }

    void go(int i)
    {
        cur = rows.empty() ? -1 : std::max(0, std::min(i, (int)rows.size() - 1));
        refreshList();
        load();
    }

    MenuRow fresh(int level)
    {
        MenuRow r;
        r.level = std::max(0, level);
        r.props["text"] = "";
        return r;
    }

    void action(int id)
    {
        commit();
        int n = (int)rows.size();
        switch (id) {
        case ME_LEFT:
            if (cur >= 0 && rows[cur].level > 0) rows[cur].level--;
            break;
        case ME_RIGHT:
            if (cur > 0 && rows[cur].level <= rows[cur - 1].level) rows[cur].level++;
            else if (cur >= 0) say(cur == 0 ? "The first menu is a title: it can't be indented."
                                             : "A menu can be one level below the one above it, not more.");
            break;
        case ME_UP:
            if (cur > 0) { std::swap(rows[cur], rows[cur - 1]); cur--; }
            break;
        case ME_DOWN:
            if (cur >= 0 && cur + 1 < n) { std::swap(rows[cur], rows[cur + 1]); cur++; }
            break;
        case ME_NEXT:
            if (cur + 1 < n) { go(cur + 1); return; }
            rows.push_back(fresh(cur < 0 ? 0 : rows[cur].level == 0 ? 1 : rows[cur].level));
            go((int)rows.size() - 1);
            IIntuition->IDoMethod(wo, WM_ACTIVATEGADGET, g[ME_CAPTION]);
            return;
        case ME_INSERT: {
            int at = cur < 0 ? 0 : cur;
            rows.insert(rows.begin() + at, fresh(cur < 0 ? 0 : rows[cur].level));
            go(at);
            IIntuition->IDoMethod(wo, WM_ACTIVATEGADGET, g[ME_CAPTION]);
            return;
        }
        case ME_DELETE:
            if (cur >= 0) rows.erase(rows.begin() + cur);
            go(cur >= (int)rows.size() ? (int)rows.size() - 1 : cur);
            return;
        default:
            break;
        }
        refreshList();
        load();
    }

    bool run(std::string &selectId)
    {
        Class *layout = ILayout->LAYOUT_GetClass(), *str = IString->STRING_GetClass(), *cb = ICheckBox->CHECKBOX_GetClass();
        auto label = [](const char *t) {
            return IIntuition->NewObject(ILabel->LABEL_GetClass(), nullptr, LABEL_Text, (uint32)t, TAG_DONE);
        };
        auto field = [&](int id, int max, const char *hint) {
            return g[id] = IIntuition->NewObject(str, nullptr, GA_ID, id, GA_RelVerify, TRUE, GA_TabCycle, TRUE,
                                                 STRINGA_MaxChars, max, GA_HintInfo, (uint32)hint, TAG_DONE);
        };
        auto check = [&](int id, const char *t, const char *hint) {
            return g[id] = IIntuition->NewObject(cb, nullptr, GA_ID, id, GA_RelVerify, TRUE, GA_Text, (uint32)t,
                                                 GA_HintInfo, (uint32)hint, TAG_DONE);
        };
        auto btn = [&](int id, const char *t, const char *hint) {
            return g[id] = IIntuition->NewObject(IButton->BUTTON_GetClass(), nullptr, GA_ID, id, GA_RelVerify, TRUE,
                                                 GA_Text, (uint32)t, GA_HintInfo, (uint32)hint, TAG_DONE);
        };
        static const char *titles[] = { "Menu", "Shortcut, state" };
        static const int weights[] = { 65, 35 };
        wo = IIntuition->NewObject(IWindow->WINDOW_GetClass(), nullptr,
            WA_Title, (uint32)"Menu Editor", WA_DragBar, TRUE, WA_CloseGadget, TRUE, WA_DepthGadget, TRUE,
            WA_SizeGadget, TRUE, WA_Activate, TRUE, WA_PubScreen, (uint32)app.win->WScreen,
            WA_InnerWidth, 460, WA_InnerHeight, 400,
            WA_IDCMP, IDCMP_GADGETUP | IDCMP_CLOSEWINDOW,
            WINDOW_Position, WPOS_CENTERSCREEN, WINDOW_GadgetHelp, TRUE,
            WINDOW_Layout, IIntuition->NewObject(layout, nullptr,
                LAYOUT_Orientation, LAYOUT_ORIENT_VERT, LAYOUT_SpaceOuter, TRUE,
                LAYOUT_AddChild, IIntuition->NewObject(layout, nullptr, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                    LAYOUT_AddChild, field(ME_CAPTION, 200, "The text of the menu; - is a separator bar. _ marks no key here: use Shortcut"),
                    CHILD_Label, label("_Caption"),
                    LAYOUT_AddChild, field(ME_KEY, 20, "One character: Amiga + that key chooses it. A longer text is only shown"),
                    CHILD_Label, label("Short_cut"), CHILD_WeightedWidth, 30,
                    TAG_DONE),
                CHILD_WeightedHeight, 0,
                LAYOUT_AddChild, field(ME_NAME, 64, "The name for code: its event sub is <name>_Click. Made from the caption if empty"),
                CHILD_Label, label("_Name"), CHILD_WeightedHeight, 0,
                LAYOUT_AddChild, IIntuition->NewObject(layout, nullptr, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                    LAYOUT_AddChild, check(ME_CHECKABLE, "Chec_kable", "A toggle item with a checkmark"),
                    LAYOUT_AddChild, check(ME_CHECKED, "C_hecked", "Its checkmark is on at the start"),
                    LAYOUT_AddChild, check(ME_RADIO, "_Radio", "One of the radio items of its panel"),
                    LAYOUT_AddChild, check(ME_ENABLED, "_Enabled", "Off: greyed out (a title: its whole menu)"),
                    LAYOUT_AddChild, check(ME_VISIBLE, "_Visible", "Off: not shown; a hidden title is a context menu"),
                    TAG_DONE),
                CHILD_WeightedHeight, 0,
                LAYOUT_AddChild, IIntuition->NewObject(layout, nullptr, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                    LAYOUT_EvenSize, TRUE,
                    LAYOUT_AddChild, btn(ME_LEFT, "<-", "One level out"),
                    LAYOUT_AddChild, btn(ME_RIGHT, "->", "One level in: an item of the menu above"),
                    LAYOUT_AddChild, btn(ME_UP, "Up", "Before the previous menu"),
                    LAYOUT_AddChild, btn(ME_DOWN, "Down", "After the next menu"),
                    LAYOUT_AddChild, btn(ME_NEXT, "Ne_xt", "The next menu; a new one at the end"),
                    LAYOUT_AddChild, btn(ME_INSERT, "_Insert", "A new menu before the selected one"),
                    LAYOUT_AddChild, btn(ME_DELETE, "_Delete", "Remove the selected menu"),
                    TAG_DONE),
                CHILD_WeightedHeight, 0,
                LAYOUT_AddChild, list.create(ME_LIST, titles, weights),
                LAYOUT_AddChild, g[ME_STATUS] = IIntuition->NewObject(IButton->BUTTON_GetClass(), nullptr,
                    GA_ID, ME_STATUS, GA_ReadOnly, TRUE, GA_Text, (uint32)"", BUTTON_Justification, BCJ_LEFT, TAG_DONE),
                CHILD_WeightedHeight, 0,
                LAYOUT_AddChild, IIntuition->NewObject(layout, nullptr, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                    LAYOUT_EvenSize, TRUE,
                    LAYOUT_AddChild, btn(ME_OK, "_OK", "Put these menus into the form"),
                    LAYOUT_AddChild, IIntuition->NewObject(ISpace->SPACE_GetClass(), nullptr, TAG_DONE),
                    LAYOUT_AddChild, btn(ME_CANCEL, "Cancel", "Leave the form's menus as they were"),
                    TAG_DONE),
                CHILD_WeightedHeight, 0,
                TAG_DONE),
            TAG_DONE);
        if (!wo) return false;
        w = (struct Window *)IIntuition->IDoMethod(wo, WM_OPEN);
        if (!w) {
            IIntuition->DisposeObject(wo);
            return false;
        }
        list.win = w;
        app.beginModal(wo);
        go(0);
        say(rows.empty() ? "No menus yet: type a title's caption (Project), then Next for its first item."
                         : "Choose a menu to change it. -> makes it an item of the menu above.");
        if (rows.empty()) action(ME_NEXT);
        int done = 0;   // 1 OK, -1 cancel
        while (!done) {
            app.waitModal(wo);
            uint32 result;
            uint16 code;
            while ((result = IIntuition->IDoMethod(wo, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
                if ((result & WMHI_CLASSMASK) == WMHI_CLOSEWINDOW) done = -1;
                if ((result & WMHI_CLASSMASK) != WMHI_GADGETUP) continue;
                uint32 id = result & WMHI_GADGETMASK;
                switch (id) {
                case ME_LIST: {
                    int i = list.indexOf(list.selected());
                    if (i >= 0 && i != cur) {
                        commit();
                        go(i);
                    }
                    break;
                }
                case ME_CAPTION:
                case ME_NAME:
                case ME_KEY:
                case ME_CHECKABLE:
                case ME_CHECKED:
                case ME_RADIO:
                case ME_ENABLED:
                case ME_VISIBLE:
                    commit();
                    refreshList();
                    if (id == ME_CAPTION) load();   // a name may have been made
                    break;
                case ME_OK: {
                    commit();
                    // rows nobody filled in (a Next too many) are dropped, as VB does
                    std::vector<MenuRow> keep;
                    for (auto &r : rows)
                        if (!prop(r, "text").empty() || !prop(r, "id").empty() || !r.props.count("text")) keep.push_back(r);
                    std::string err;
                    std::vector<MenuRow> before = model.menuRows();
                    if (!model.setMenuRows(keep, err)) { say(err); break; }
                    if (cur >= 0 && cur < (int)rows.size()) selectId = prop(rows[cur], "id");
                    done = 1;
                    if (model.menuRows().size() == before.size()) {
                        bool same = true;
                        auto after = model.menuRows();
                        for (size_t i = 0; i < before.size() && same; i++)
                            same = before[i].level == after[i].level && before[i].props == after[i].props;
                        if (same) done = 2;   // nothing changed
                    }
                    break;
                }
                case ME_CANCEL: done = -1; break;
                default: action((int)id); break;
                }
            }
        }
        app.endModal();
        list.win = nullptr;
        IIntuition->DisposeObject(wo);
        list.obj = nullptr;
        return done == 1;
    }
};

} // namespace

// true if the menus changed; selectId: the menu that was selected when OK was clicked
bool basicMenuEditor(App &app, Model &model, std::string &selectId)
{
    MenuEditor ed(app, model);
    return ed.run(selectId);
}

} // namespace af
