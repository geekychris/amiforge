// todo - a to-do list with ReAction. A C++ example using the OS:
//   - window.class with a layout.gadget tree: a listbrowser.gadget with a checkbox per item,
//     a string.gadget and button.gadgets (one C++ class per group, in gadgets.cpp);
//   - WM_HANDLEINPUT event handling, keyboard shortcuts from the "_" in the labels;
//   - dos.library files: the list is kept in ENVARC:TodoExample (and ENV:), so it survives a
//     reboot, and saved after every change.
// Type an item and press Return (or Add). Click a checkbox to mark it done; Clear Done removes
// those. Esc or the close gadget quits.
#include <cstdio>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/listbrowser.h>

#include "todo.h"

const char *const TodoList::fileName = "TodoExample";

bool TodoList::load()
{
    items.clear();
    BPTR f = 0;
    for (const char *dir : { "ENV:", "ENVARC:" })
        if ((f = IDOS->Open((std::string(dir) + fileName).c_str(), MODE_OLDFILE))) break;
    if (!f) return false;
    char line[256];
    while (IDOS->FGets(f, line, sizeof line)) {
        std::string s = line;
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (s.size() < 5 || s[0] != '[' || s[2] != ']') continue;
        items.push_back({ s.substr(4), s[1] == 'x' });
    }
    IDOS->Close(f);
    return true;
}

bool TodoList::save() const
{
    bool ok = true;
    for (const char *dir : { "ENV:", "ENVARC:" }) {
        BPTR f = IDOS->Open((std::string(dir) + fileName).c_str(), MODE_NEWFILE);
        if (!f) { ok = false; continue; }
        for (const TodoItem &i : items) {
            IDOS->FPuts(f, i.done ? "[x] " : "[ ] ");
            IDOS->FPuts(f, i.text.c_str());
            IDOS->FPutC(f, '\n');
        }
        IDOS->Close(f);
    }
    return ok;
}

// The window and everything in it. The gadget groups are members, so they outlive the window
// object (destroyed in the destructor body): the listbrowser's nodes are freed after it.
class TodoWindow {
public:
    enum { GID_ITEMS = 1, GID_ENTRY, GID_ADD, GID_REMOVE, GID_CLEAR };

    TodoWindow(TodoList &todo) : todo(todo), list(GID_ITEMS), entry(GID_ENTRY, GID_ADD), buttons(GID_REMOVE, GID_CLEAR)
    {
        Object *root = IIntuition->NewObject(ILayout->LAYOUT_GetClass(), nullptr,
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT, LAYOUT_SpaceOuter, TRUE,
            LAYOUT_AddChild, list.gadget(),
            LAYOUT_AddChild, entry.layout(), CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, buttons.layout(), CHILD_WeightedHeight, 0,
            TAG_DONE);
        winObj = IIntuition->NewObject(IWindow->WINDOW_GetClass(), nullptr,
            WA_Title, "To Do", WA_ScreenTitle, "To Do - an AmiForge example",
            WA_DragBar, TRUE, WA_CloseGadget, TRUE, WA_DepthGadget, TRUE, WA_SizeGadget, TRUE,
            WA_Activate, TRUE, WA_InnerWidth, 360, WA_InnerHeight, 300, WA_IDCMP, IDCMP_GADGETUP | IDCMP_RAWKEY | IDCMP_CLOSEWINDOW,
            WINDOW_Position, WPOS_CENTERSCREEN,
            WINDOW_Layout, root,
            TAG_DONE);
        if (!winObj && root) IIntuition->DisposeObject(root);
    }

    ~TodoWindow()
    {
        if (winObj) IIntuition->DisposeObject(winObj);   // closes the window, disposes the gadgets
    }

    TodoWindow(const TodoWindow &) = delete;
    TodoWindow &operator=(const TodoWindow &) = delete;

    bool open()
    {
        if (!winObj) return false;
        win = (struct Window *)IIntuition->IDoMethod(winObj, WM_OPEN);
        if (!win) return false;
        list.show(todo, win, -1);
        entry.activate(win);
        return true;
    }

    uint32 signal() const
    {
        uint32 sig = 0;
        IIntuition->GetAttr(WINDOW_SigMask, winObj, &sig);
        return sig;
    }

    // Handle what arrived; false when the window should close.
    bool handle()
    {
        uint32 result;
        uint16 code;
        bool keepOpen = true;
        while ((result = IIntuition->IDoMethod(winObj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
            switch (result & WMHI_CLASSMASK) {
            case WMHI_CLOSEWINDOW:
                keepOpen = false;
                break;
            case WMHI_RAWKEY:
                if ((result & WMHI_KEYMASK) == 0x45) keepOpen = false;   // Esc
                break;
            case WMHI_GADGETUP:
                gadgetUp(result & WMHI_GADGETMASK);
                break;
            }
        }
        return keepOpen;
    }

private:
    void gadgetUp(uint32 id)
    {
        switch (id) {
        case GID_ENTRY:   // Return in the string gadget
        case GID_ADD: {
            std::string text = entry.take(win);
            if (!text.empty()) {
                todo.items.push_back({ text, false });
                changed((int)todo.items.size() - 1);
            }
            entry.activate(win);
            break;
        }
        case GID_REMOVE: {
            int sel = list.selected();
            if (sel >= 0 && sel < (int)todo.items.size()) {
                todo.items.erase(todo.items.begin() + sel);
                changed(sel);
            }
            break;
        }
        case GID_CLEAR: {
            std::vector<TodoItem> keep;
            for (auto &i : todo.items)
                if (!i.done) keep.push_back(i);
            todo.items.swap(keep);
            changed(-1);
            break;
        }
        case GID_ITEMS:
            if (list.readChecks(todo) && !todo.save()) std::printf("todo: can't save the list\n");
            break;
        }
    }

    void changed(int select)
    {
        list.show(todo, win, select);
        if (!todo.save()) std::printf("todo: can't save the list\n");
    }

    TodoList &todo;
    ItemList list;
    EntryRow entry;
    ButtonRow buttons;
    Object *winObj = nullptr;
    struct Window *win = nullptr;
};

int main()
{
    TodoList todo;
    if (!todo.load()) {
        todo.items.push_back({ "Build this example with F7", true });
        todo.items.push_back({ "Set a breakpoint in TodoWindow::gadgetUp", false });
        todo.items.push_back({ "Add an item of your own", false });
    }
    TodoWindow window(todo);
    if (!window.open()) {
        std::printf("todo: can't open the window\n");
        return 20;
    }
    uint32 sig = window.signal();
    for (;;) {
        uint32 got = IExec->Wait(sig | SIGBREAKF_CTRL_C);
        if (got & SIGBREAKF_CTRL_C) break;
        if (!window.handle()) break;
    }
    return todo.save() ? 0 : 5;
}
