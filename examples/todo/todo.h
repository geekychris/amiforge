// todo.h - the to-do list: the data (TodoList) and one C++ class per group of ReAction gadgets.
#pragma once

#include <string>
#include <vector>

#include <exec/types.h>
#include <exec/lists.h>
#include <intuition/classusr.h>

// The items, saved as text: one per line, "[x] text" or "[ ] text".
struct TodoItem {
    std::string text;
    bool done = false;
};

class TodoList {
public:
    std::vector<TodoItem> items;
    bool load();                  // ENV:TodoExample, else ENVARC:TodoExample
    bool save() const;            // both (ENVARC: keeps it over a reboot)
    static const char *const fileName;
};

// The listbrowser with a checkbox per item. Owns the Exec list of nodes it shows and the
// column info; the gadget itself belongs to the window's layout (disposed with the window).
class ItemList {
public:
    explicit ItemList(uint32 id);
    ~ItemList();
    ItemList(const ItemList &) = delete;
    ItemList &operator=(const ItemList &) = delete;

    Object *gadget() const { return obj; }
    void show(const TodoList &todo, struct Window *win, int select);   // rebuild the rows
    bool readChecks(TodoList &todo) const;                             // checkbox states back; true if one changed
    int selected() const;                                              // -1 if none

private:
    void freeNodes();
    Object *obj = nullptr;
    struct List nodes;
    struct ColumnInfo *columns = nullptr;
};

// The string gadget to type a new item in and its Add button.
class EntryRow {
public:
    EntryRow(uint32 stringId, uint32 addId);
    Object *layout() const { return group; }
    std::string take(struct Window *win);   // the text, and clears the gadget
    void activate(struct Window *win);      // put the cursor in it
private:
    Object *group = nullptr, *string = nullptr;
};

// Remove and Clear Done.
class ButtonRow {
public:
    ButtonRow(uint32 removeId, uint32 clearId);
    Object *layout() const { return group; }
private:
    Object *group = nullptr;
};
