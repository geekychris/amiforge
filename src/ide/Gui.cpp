// ListView: a listbrowser.gadget with its node list, columns and the detach/attach dance.
#include "App.h"

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/listbrowser.h>
#include <gadgets/listbrowser.h>
#include <intuition/gadgetclass.h>

namespace af {

ListView::ListView(int columns) : cols(columns)
{
    list = new struct List;
    IExec->NewList(list);
}

ListView::~ListView()
{
    IListBrowser->FreeListBrowserList(list);
    delete list;
    if (ci) IListBrowser->FreeLBColumnInfo(ci);
}

Object *ListView::create(uint32_t gid, const char *const *titles, const int *weights,
                         bool hier, bool checkboxes, struct TextAttr *font)
{
    hierarchical = hier;
    std::vector<struct TagItem> tags;
    for (int c = 0; c < cols; c++) {
        tags.push_back({LBCIA_Column, (uint32)c});
        if (titles) tags.push_back({LBCIA_Title, (uint32)titles[c]});
        tags.push_back({LBCIA_Weight, (uint32)(weights ? weights[c] : 100 / cols)});
        tags.push_back({LBCIA_DraggableSeparator, TRUE});
    }
    tags.push_back({TAG_DONE, 0});
    ci = IListBrowser->AllocLBColumnInfoA(cols, tags.data());
    obj = IIntuition->NewObject(IListBrowser->LISTBROWSER_GetClass(), nullptr,
        GA_ID, gid,
        GA_RelVerify, TRUE,
        font ? GA_TextAttr : TAG_IGNORE, (uint32)font,
        LISTBROWSER_Labels, (uint32)list,
        LISTBROWSER_ColumnInfo, (uint32)ci,
        LISTBROWSER_ColumnTitles, titles != nullptr,
        LISTBROWSER_Hierarchical, hier,
        LISTBROWSER_ShowSelected, TRUE,
        LISTBROWSER_AutoFit, TRUE,
        LISTBROWSER_HorizontalProp, cols > 1 || hier,
        LISTBROWSER_Striping, LBS_ROWS,
        TAG_DONE);
    (void)checkboxes;
    return obj;
}

void ListView::detach()
{
    if (detachDepth++ == 0 && obj) {
        if (win) IIntuition->SetGadgetAttrs((struct Gadget *)obj, win, nullptr, LISTBROWSER_Labels, ~0UL, TAG_DONE);
        else IIntuition->SetAttrs(obj, LISTBROWSER_Labels, ~0UL, TAG_DONE);
    }
}

void ListView::attach(int sel)
{
    if (detachDepth > 0 && --detachDepth == 0 && obj) {
        // RefreshSetGadgetAttrs: with plain SetGadgetAttrs the list showed the new rows but
        // ignored clicks until it was re-rendered (seen on the build log after a build).
        if (win)
            IIntuition->RefreshSetGadgetAttrs((struct Gadget *)obj, win, nullptr,
                LISTBROWSER_Labels, (uint32)list, LISTBROWSER_ColumnInfo, (uint32)ci,
                sel >= 0 ? LISTBROWSER_Selected : TAG_IGNORE, (uint32)sel,
                sel >= 0 ? LISTBROWSER_MakeVisible : TAG_IGNORE, (uint32)sel, TAG_DONE);
        else
            IIntuition->SetAttrs(obj, LISTBROWSER_Labels, (uint32)list, LISTBROWSER_ColumnInfo, (uint32)ci, TAG_DONE);
    }
}

void ListView::clear()
{
    detach();
    IListBrowser->FreeListBrowserList(list);
    IExec->NewList(list);
    attach();
}

struct Node *ListView::add(const std::vector<std::string> &c, void *ud, int generation,
                           uint32_t flags, bool checked, struct Node *after)
{
    std::vector<struct TagItem> tags;
    for (int i = 0; i < cols; i++) {
        tags.push_back({LBNA_Column, (uint32)i});
        tags.push_back({LBNCA_CopyText, TRUE});
        tags.push_back({LBNCA_Text, (uint32)(i < (int)c.size() ? c[i].c_str() : "")});
    }
    tags.push_back({LBNA_UserData, (uint32)ud});
    if (hierarchical) tags.push_back({LBNA_Generation, (uint32)(generation + 1)});
    if (flags) tags.push_back({LBNA_Flags, flags});
    if (flags & 0x80000000) {   // our own flag: checkbox row
        tags.back().ti_Data = flags & ~0x80000000;
        tags.push_back({LBNA_CheckBox, TRUE});
        tags.push_back({LBNA_Checked, checked});
    }
    tags.push_back({TAG_DONE, 0});
    struct Node *n = IListBrowser->AllocListBrowserNodeA(cols, tags.data());
    if (!n) return nullptr;
    if (after) IExec->Insert(list, n, after);
    else IExec->AddTail(list, n);
    return n;
}

void ListView::setPens(struct Node *n, int fg)
{
    if (!n || fg < 0) return;
    uint32 flags = 0;
    IListBrowser->GetListBrowserNodeAttrs(n, LBNA_Flags, &flags, TAG_DONE);
    for (int i = 0; i < cols; i++)
        IListBrowser->SetListBrowserNodeAttrs(n, LBNA_Column, i, LBNCA_FGPen, fg, TAG_DONE);
    IListBrowser->SetListBrowserNodeAttrs(n, LBNA_Flags, flags | LBFLG_CUSTOMPENS, TAG_DONE);
}

int ListView::count() const
{
    int n = 0;
    for (struct Node *p = list->lh_Head; p->ln_Succ; p = p->ln_Succ) n++;
    return n;
}

struct Node *ListView::nodeAt(int index) const
{
    int i = 0;
    for (struct Node *p = list->lh_Head; p->ln_Succ; p = p->ln_Succ, i++)
        if (i == index) return p;
    return nullptr;
}

int ListView::indexOf(struct Node *n) const
{
    int i = 0;
    for (struct Node *p = list->lh_Head; p->ln_Succ; p = p->ln_Succ, i++)
        if (p == n) return i;
    return -1;
}

void *ListView::userData(struct Node *n) const
{
    uint32 ud = 0;
    if (n) IListBrowser->GetListBrowserNodeAttrs(n, LBNA_UserData, &ud, TAG_DONE);
    return (void *)ud;
}

struct Node *ListView::selected() const
{
    uint32 n = 0;
    if (obj) IIntuition->GetAttr(LISTBROWSER_SelectedNode, obj, &n);
    return (struct Node *)n;
}

void ListView::makeVisible(int index)
{
    if (obj && win && index >= 0)
        IIntuition->SetGadgetAttrs((struct Gadget *)obj, win, nullptr, LISTBROWSER_MakeVisible, index, TAG_DONE);
}

void ListView::select(int index)
{
    if (obj && win)
        IIntuition->SetGadgetAttrs((struct Gadget *)obj, win, nullptr, LISTBROWSER_Selected, index,
                                   LISTBROWSER_MakeVisible, index < 0 ? 0 : index, TAG_DONE);
}

} // namespace af
