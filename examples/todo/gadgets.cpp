// gadgets.cpp - the ReAction side: listbrowser.gadget, string.gadget, button.gadget, layout.gadget.
#include "todo.h"

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/listbrowser.h>
#include <proto/layout.h>
#include <proto/string.h>
#include <proto/button.h>
#include <gadgets/listbrowser.h>
#include <gadgets/layout.h>
#include <gadgets/string.h>
#include <gadgets/button.h>

// --- ItemList ---

ItemList::ItemList(uint32 id)
{
    IExec->NewList(&nodes);
    columns = IListBrowser->AllocLBColumnInfo(1, LBCIA_Column, 0, LBCIA_Title, "To do", TAG_DONE);
    obj = IIntuition->NewObject(IListBrowser->LISTBROWSER_GetClass(), nullptr,
        GA_ID, id, GA_RelVerify, TRUE,
        LISTBROWSER_Labels, &nodes,
        LISTBROWSER_ColumnInfo, columns,
        LISTBROWSER_ColumnTitles, TRUE,
        LISTBROWSER_ShowSelected, TRUE,
        LISTBROWSER_Striping, LBS_ROWS,
        TAG_DONE);
}

ItemList::~ItemList()
{
    // the gadget has gone with the window by now: its list and columns can go
    freeNodes();
    if (columns) IListBrowser->FreeLBColumnInfo(columns);
}

void ItemList::freeNodes()
{
    IListBrowser->FreeListBrowserList(&nodes);   // frees every node and empties the list
}

void ItemList::show(const TodoList &todo, struct Window *win, int select)
{
    // detach the list while it changes, then give it back
    IIntuition->SetGadgetAttrs((struct Gadget *)obj, win, nullptr, LISTBROWSER_Labels, ~0U, TAG_DONE);
    freeNodes();
    for (const TodoItem &item : todo.items) {
        struct Node *n = IListBrowser->AllocListBrowserNode(1,
            LBNA_CheckBox, TRUE, LBNA_Checked, item.done,
            LBNA_Column, 0, LBNCA_CopyText, TRUE, LBNCA_Text, item.text.c_str(),
            TAG_DONE);
        if (n) IExec->AddTail(&nodes, n);
    }
    if (select >= (int)todo.items.size()) select = (int)todo.items.size() - 1;
    IIntuition->SetGadgetAttrs((struct Gadget *)obj, win, nullptr,
        LISTBROWSER_Labels, &nodes, LISTBROWSER_Selected, select, LISTBROWSER_MakeVisible, select < 0 ? 0 : select,
        TAG_DONE);
}

bool ItemList::readChecks(TodoList &todo) const
{
    bool changed = false;
    size_t i = 0;
    for (struct Node *n = nodes.lh_Head; n->ln_Succ && i < todo.items.size(); n = n->ln_Succ, i++) {
        uint32 checked = 0;
        IListBrowser->GetListBrowserNodeAttrs(n, LBNA_Checked, &checked, TAG_DONE);
        if ((checked != 0) != todo.items[i].done) {
            todo.items[i].done = checked != 0;
            changed = true;
        }
    }
    return changed;
}

int ItemList::selected() const
{
    int32 sel = -1;
    IIntuition->GetAttr(LISTBROWSER_Selected, obj, (uint32 *)&sel);
    return sel;
}

// --- EntryRow ---

EntryRow::EntryRow(uint32 stringId, uint32 addId)
{
    group = IIntuition->NewObject(ILayout->LAYOUT_GetClass(), nullptr,
        LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
        LAYOUT_AddChild, string = IIntuition->NewObject(IString->STRING_GetClass(), nullptr,
            GA_ID, stringId, GA_RelVerify, TRUE, GA_TabCycle, TRUE, STRINGA_MaxChars, 200, TAG_DONE),
        LAYOUT_AddChild, IIntuition->NewObject(IButton->BUTTON_GetClass(), nullptr,
            GA_ID, addId, GA_RelVerify, TRUE, GA_Text, "_Add", TAG_DONE),
        CHILD_WeightedWidth, 0,
        TAG_DONE);
}

std::string EntryRow::take(struct Window *win)
{
    uint32 v = 0;
    IIntuition->GetAttr(STRINGA_TextVal, string, &v);
    std::string text = v ? (const char *)v : "";
    IIntuition->SetGadgetAttrs((struct Gadget *)string, win, nullptr, STRINGA_TextVal, "", TAG_DONE);
    // no leading or trailing blanks
    size_t a = text.find_first_not_of(" \t"), b = text.find_last_not_of(" \t");
    return a == std::string::npos ? "" : text.substr(a, b - a + 1);
}

void EntryRow::activate(struct Window *win)
{
    if (win) ILayout->ActivateLayoutGadget((struct Gadget *)group, win, nullptr, (uint32)string);
}

// --- ButtonRow ---

ButtonRow::ButtonRow(uint32 removeId, uint32 clearId)
{
    group = IIntuition->NewObject(ILayout->LAYOUT_GetClass(), nullptr,
        LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ, LAYOUT_EvenSize, TRUE,
        LAYOUT_AddChild, IIntuition->NewObject(IButton->BUTTON_GetClass(), nullptr,
            GA_ID, removeId, GA_RelVerify, TRUE, GA_Text, "_Remove", TAG_DONE),
        LAYOUT_AddChild, IIntuition->NewObject(IButton->BUTTON_GetClass(), nullptr,
            GA_ID, clearId, GA_RelVerify, TRUE, GA_Text, "_Clear Done", TAG_DONE),
        TAG_DONE);
}
