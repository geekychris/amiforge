// The GUI designer's model: a window with a tree of ReAction layouts and gadgets, its text
// file format (.afg) and the C code generator. Portable C++ (no OS calls): the host unit tests
// build it as is.
//
// A .afg file:
//
//   # AmiForge GUI
//   window name=main title="My Program" width=320 height=160
//   vlayout id=root
//     hlayout title="Options" frame=group
//       checkbox id=fast text="_Fast"
//     button id=ok text="_OK" weight=0
//
// One object per line: its kind and key=value properties (values with spaces in "quotes",
// with \" \\ and \n escapes). Two spaces of indentation per level of nesting. The first line
// after the comments is the window, the second the root layout.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace af {
namespace gui {

enum class Kind {
    Window, VLayout, HLayout, Button, String, Integer, CheckBox, Chooser, Radio, Slider, Scroller,
    ListBrowser, Label, Space, FuelGauge, GetFile, TextEditor, Count
};

// A property of an object kind, as the property editor shows it.
enum class PropType { Text, Ident, Int, Bool, Items, Choice };
struct PropInfo {
    const char *key;
    const char *title;
    PropType type;
    const char *def;        // default value ("" = unset)
    const char *choices;    // PropType::Choice: "a|b|c"
};

struct KindInfo {
    Kind kind;
    const char *name;           // in the .afg file: "button"
    const char *title;          // in the palette: "Button"
    bool container;             // holds children (layouts)
    bool gadget;                // has a GA_ID and reports events
    const char *idPrefix;       // for new objects' ids: "button" -> button1
    std::vector<PropInfo> props;
};

const KindInfo &kindInfo(Kind k);
const KindInfo *kindByName(const std::string &name);
std::vector<Kind> paletteKinds();    // the kinds the designer's palette offers, in order

struct Node {
    Kind kind = Kind::Button;
    std::map<std::string, std::string> props;
    std::vector<std::unique_ptr<Node>> children;
    Node *parent = nullptr;

    std::string get(const std::string &key) const;        // the value, or the kind's default
    int getInt(const std::string &key) const;
    bool getBool(const std::string &key) const;
    bool has(const std::string &key) const { return props.count(key) && !props.at(key).empty(); }
    void set(const std::string &key, const std::string &value);
    Node *add(Kind k, int index = -1);                    // a new child (at index, -1 = end)
    std::unique_ptr<Node> detach();                       // out of its parent
    int indexInParent() const;
};

std::vector<std::string> splitItems(const std::string &items);   // "a|b|c" -> {a, b, c}

struct Model {
    Node window;       // kind Window: name, title, width, height
    Node *root();      // the window's one child, a layout (created if missing)

    bool parse(const std::string &text, std::string &err);
    std::string write() const;

    // Every node in display order (depth first), with its depth (root = 0).
    void walk(std::vector<std::pair<Node *, int>> &out);
    Node *findById(const std::string &id);
    std::string uniqueId(Kind k);       // "button3": the first free id for a new object
    std::string baseName() const;       // the window's name as a C identifier ("main")
    std::vector<std::string> validate();   // problems that would stop the code from compiling
};

// C identifier from free text: "My App!" -> "My_App_"; never empty, never starts with a digit.
std::string identifier(const std::string &s);
std::string upper(const std::string &s);
std::string cString(const std::string &s);    // a C string literal, quotes included

// Generated code. Text between "USER-BEGIN <key>" and "USER-END <key>" comment lines in
// `oldSource` is kept in the new source; sections whose key no longer exists are kept in an
// #if 0 block at the end, so nothing typed there is lost.
struct Generated {
    std::string headerName, sourceName;   // "main_gui.h", "main_gui.c"
    std::string header, source;
};
Generated generate(Model &m, const std::string &oldSource = "");
std::map<std::string, std::string> userSections(const std::string &source);

} // namespace gui
} // namespace af
