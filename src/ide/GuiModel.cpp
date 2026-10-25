// The GUI designer's model, its .afg file format and the C code generator (see GuiModel.h).
#include "GuiModel.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace af {
namespace gui {

// --- kinds -------------------------------------------------------------------------------

static const char *const FRAMES = "none|group|button|field";

static std::vector<PropInfo> withCommon(std::vector<PropInfo> own, bool gadget)
{
    std::vector<PropInfo> p = { { "id", "ID name", PropType::Ident, "", nullptr },
                                { "label", "Label (left)", PropType::Text, "", nullptr } };
    p.insert(p.end(), own.begin(), own.end());
    p.push_back({ "weight", "Weight (0 = minimum size)", PropType::Int, "", nullptr });
    p.push_back({ "minwidth", "Minimum width", PropType::Int, "", nullptr });
    p.push_back({ "minheight", "Minimum height", PropType::Int, "", nullptr });
    if (gadget) {
        p.push_back({ "disabled", "Disabled", PropType::Bool, "0", nullptr });
        p.push_back({ "hint", "Help hint", PropType::Text, "", nullptr });
    }
    return p;
}

static const std::vector<KindInfo> &kinds()
{
    static std::vector<KindInfo> k = {
        { Kind::Window, "window", "Window", true, false, "window",
          { { "name", "Code name", PropType::Ident, "main", nullptr },
            { "title", "Title", PropType::Text, "Untitled", nullptr },
            { "width", "Width (0 = automatic)", PropType::Int, "0", nullptr },
            { "height", "Height (0 = automatic)", PropType::Int, "0", nullptr },
            { "resizable", "Resizable", PropType::Bool, "1", nullptr } } },
        { Kind::VLayout, "vlayout", "Vertical Group", true, false, "vgroup",
          withCommon({ { "title", "Frame title", PropType::Text, "", nullptr },
                       { "frame", "Frame", PropType::Choice, "none", FRAMES },
                       { "even", "Even sizes", PropType::Bool, "0", nullptr } }, false) },
        { Kind::HLayout, "hlayout", "Horizontal Group", true, false, "hgroup",
          withCommon({ { "title", "Frame title", PropType::Text, "", nullptr },
                       { "frame", "Frame", PropType::Choice, "none", FRAMES },
                       { "even", "Even sizes", PropType::Bool, "0", nullptr } }, false) },
        { Kind::Button, "button", "Button", false, true, "button",
          withCommon({ { "text", "Text", PropType::Text, "Button", nullptr } }, true) },
        { Kind::String, "string", "String", false, true, "string",
          withCommon({ { "text", "Text", PropType::Text, "", nullptr },
                       { "maxchars", "Maximum length", PropType::Int, "128", nullptr } }, true) },
        { Kind::Integer, "integer", "Integer", false, true, "integer",
          withCommon({ { "value", "Value", PropType::Int, "0", nullptr },
                       { "min", "Minimum", PropType::Int, "0", nullptr },
                       { "max", "Maximum", PropType::Int, "100", nullptr } }, true) },
        { Kind::CheckBox, "checkbox", "Check Box", false, true, "checkbox",
          withCommon({ { "text", "Text", PropType::Text, "Check box", nullptr },
                       { "checked", "Checked", PropType::Bool, "0", nullptr } }, true) },
        { Kind::Chooser, "chooser", "Chooser (pop-up)", false, true, "chooser",
          withCommon({ { "items", "Items (separate with |)", PropType::Items, "One|Two|Three", nullptr },
                       { "selected", "Selected item (0 = first)", PropType::Int, "0", nullptr } }, true) },
        { Kind::Radio, "radio", "Radio Buttons", false, true, "radio",
          withCommon({ { "items", "Items (separate with |)", PropType::Items, "One|Two", nullptr },
                       { "selected", "Selected item (0 = first)", PropType::Int, "0", nullptr } }, true) },
        { Kind::Slider, "slider", "Slider", false, true, "slider",
          withCommon({ { "min", "Minimum", PropType::Int, "0", nullptr },
                       { "max", "Maximum", PropType::Int, "100", nullptr },
                       { "level", "Level", PropType::Int, "0", nullptr },
                       { "vertical", "Vertical", PropType::Bool, "0", nullptr } }, true) },
        { Kind::Scroller, "scroller", "Scroller", false, true, "scroller",
          withCommon({ { "total", "Total", PropType::Int, "100", nullptr },
                       { "visible", "Visible", PropType::Int, "10", nullptr },
                       { "top", "Top", PropType::Int, "0", nullptr },
                       { "vertical", "Vertical", PropType::Bool, "0", nullptr } }, true) },
        { Kind::ListBrowser, "listbrowser", "List", false, true, "list",
          withCommon({ { "items", "Items (separate with |)", PropType::Items, "", nullptr } }, true) },
        { Kind::Label, "label", "Text Label", false, false, "text",
          withCommon({ { "text", "Text", PropType::Text, "Text", nullptr } }, false) },
        { Kind::Space, "space", "Space", false, false, "space", withCommon({}, false) },
        { Kind::FuelGauge, "fuelgauge", "Fuel Gauge", false, false, "gauge",
          withCommon({ { "min", "Minimum", PropType::Int, "0", nullptr },
                       { "max", "Maximum", PropType::Int, "100", nullptr },
                       { "level", "Level", PropType::Int, "50", nullptr } }, false) },
        { Kind::GetFile, "getfile", "File Chooser", false, true, "file",
          withCommon({ { "title", "Requester title", PropType::Text, "Choose a file", nullptr },
                       { "pattern", "Pattern", PropType::Text, "", nullptr },
                       { "save", "Save mode", PropType::Bool, "0", nullptr } }, true) },
        { Kind::TextEditor, "texteditor", "Text Editor", false, true, "editor",
          withCommon({ { "text", "Text", PropType::Text, "", nullptr } }, true) },
    };
    return k;
}

const KindInfo &kindInfo(Kind k) { return kinds()[(int)k]; }

const KindInfo *kindByName(const std::string &name)
{
    for (auto &k : kinds())
        if (name == k.name) return &k;
    return nullptr;
}

std::vector<Kind> paletteKinds()
{
    std::vector<Kind> v;
    for (int i = (int)Kind::VLayout; i < (int)Kind::Count; i++) v.push_back((Kind)i);
    return v;
}

// --- nodes -------------------------------------------------------------------------------

std::string Node::get(const std::string &key) const
{
    auto it = props.find(key);
    if (it != props.end()) return it->second;
    for (auto &p : kindInfo(kind).props)
        if (key == p.key) return p.def;
    return "";
}

int Node::getInt(const std::string &key) const { return atoi(get(key).c_str()); }

bool Node::getBool(const std::string &key) const
{
    std::string v = get(key);
    return !v.empty() && v != "0" && v != "no" && v != "false";
}

void Node::set(const std::string &key, const std::string &value)
{
    // a value equal to the default isn't written to the file
    std::string def;
    for (auto &p : kindInfo(kind).props)
        if (key == p.key) def = p.def;
    if (value == def && key != "id" && key != "name") props.erase(key);
    else props[key] = value;
}

Node *Node::add(Kind k, int index)
{
    auto n = std::make_unique<Node>();
    n->kind = k;
    n->parent = this;
    Node *p = n.get();
    if (index < 0 || index > (int)children.size()) children.push_back(std::move(n));
    else children.insert(children.begin() + index, std::move(n));
    return p;
}

int Node::indexInParent() const
{
    if (!parent) return -1;
    for (size_t i = 0; i < parent->children.size(); i++)
        if (parent->children[i].get() == this) return (int)i;
    return -1;
}

std::unique_ptr<Node> Node::detach()
{
    int i = indexInParent();
    if (i < 0) return nullptr;
    std::unique_ptr<Node> me = std::move(parent->children[i]);
    parent->children.erase(parent->children.begin() + i);
    me->parent = nullptr;
    return me;
}

std::vector<std::string> splitItems(const std::string &items)
{
    std::vector<std::string> out;
    if (items.empty()) return out;
    size_t a = 0;
    for (;;) {
        size_t b = items.find('|', a);
        out.push_back(items.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return out;
}

// --- strings -----------------------------------------------------------------------------

std::string identifier(const std::string &s)
{
    std::string o;
    for (unsigned char c : s) o += (isalnum(c) && c < 128) || c == '_' ? (char)c : '_';
    if (o.empty() || isdigit((unsigned char)o[0])) o = "_" + o;
    return o;
}

std::string upper(const std::string &s)
{
    std::string o = s;
    for (auto &c : o) c = (char)toupper((unsigned char)c);
    return o;
}

std::string cString(const std::string &s)
{
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
        else if (c == '\n') o += "\\n";
        else if (c == '\t') o += "\\t";
        else if (c < 32) { char b[8]; snprintf(b, sizeof b, "\\%03o", c); o += b; }
        else o += (char)c;
    }
    return o + "\"";
}

static std::string quoteValue(const std::string &v)
{
    bool plain = !v.empty();
    for (unsigned char c : v)
        if (isspace(c) || c == '"' || c == '\\' || c == '=' || c == '#') plain = false;
    if (plain) return v;
    std::string o = "\"";
    for (char c : v) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (c == '\n') o += "\\n";
        else o += c;
    }
    return o + "\"";
}

// --- the file format ---------------------------------------------------------------------

Node *Model::root()
{
    window.kind = Kind::Window;
    if (window.children.empty()) {
        Node *r = window.add(Kind::VLayout);
        r->set("id", "root");
    }
    return window.children[0].get();
}

// One line: kind key=value ... ; false with err on a syntax error.
static bool parseLine(const std::string &line, std::string &kind, std::map<std::string, std::string> &props,
                      std::string &err)
{
    size_t i = 0, n = line.size();
    auto skip = [&] { while (i < n && isspace((unsigned char)line[i])) i++; };
    skip();
    size_t a = i;
    while (i < n && !isspace((unsigned char)line[i])) i++;
    kind = line.substr(a, i - a);
    for (;;) {
        skip();
        if (i >= n || line[i] == '#') break;
        size_t k = i;
        while (i < n && line[i] != '=' && !isspace((unsigned char)line[i])) i++;
        std::string key = line.substr(k, i - k);
        if (i >= n || line[i] != '=') { err = "expected key=value after \"" + key + "\""; return false; }
        i++;
        std::string val;
        if (i < n && line[i] == '"') {
            i++;
            bool closed = false;
            while (i < n) {
                char c = line[i++];
                if (c == '\\' && i < n) {
                    char e = line[i++];
                    val += e == 'n' ? '\n' : e;
                } else if (c == '"') { closed = true; break; }
                else val += c;
            }
            if (!closed) { err = "missing closing quote"; return false; }
        } else {
            size_t v = i;
            while (i < n && !isspace((unsigned char)line[i])) i++;
            val = line.substr(v, i - v);
        }
        props[key] = val;
    }
    return true;
}

bool Model::parse(const std::string &text, std::string &err)
{
    window = Node();
    window.kind = Kind::Window;
    std::vector<std::pair<Node *, int>> stack;   // node, indentation
    bool haveWindow = false;
    int lineNo = 0;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? text.size() + 1 : nl + 1;
        lineNo++;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t ind = 0;
        while (ind < line.size() && (line[ind] == ' ' || line[ind] == '\t')) ind++;
        if (ind == line.size() || line[ind] == '#') continue;
        std::string kind, e;
        std::map<std::string, std::string> props;
        if (!parseLine(line, kind, props, e)) {
            err = "line " + std::to_string(lineNo) + ": " + e;
            return false;
        }
        const KindInfo *ki = kindByName(kind);
        if (!ki) { err = "line " + std::to_string(lineNo) + ": unknown object \"" + kind + "\""; return false; }
        if (ki->kind == Kind::Window) {
            if (haveWindow) { err = "line " + std::to_string(lineNo) + ": a second window"; return false; }
            window.props = props;
            haveWindow = true;
            continue;
        }
        while (!stack.empty() && stack.back().second >= (int)ind) stack.pop_back();
        Node *parent = stack.empty() ? &window : stack.back().first;
        if (parent == &window && !window.children.empty()) {
            err = "line " + std::to_string(lineNo) + ": only one root layout is allowed";
            return false;
        }
        if (parent == &window && !ki->container) {
            err = "line " + std::to_string(lineNo) + ": the root must be a vlayout or hlayout";
            return false;
        }
        if (!kindInfo(parent->kind).container) {
            err = "line " + std::to_string(lineNo) + ": a " + kindInfo(parent->kind).name + " can't contain objects";
            return false;
        }
        Node *n = parent->add(ki->kind);
        n->props = props;
        stack.push_back({ n, (int)ind });
    }
    root();
    return true;
}

static void writeNode(const Node &n, int depth, std::string &out)
{
    out += std::string(depth * 2, ' ') + kindInfo(n.kind).name;
    // id first, then the kind's properties in their order, then anything unknown
    std::set<std::string> done;
    auto emit = [&](const std::string &k) {
        auto it = n.props.find(k);
        if (it == n.props.end() || done.count(k)) return;
        done.insert(k);
        out += " " + k + "=" + quoteValue(it->second);
    };
    emit("id");
    for (auto &p : kindInfo(n.kind).props) emit(p.key);
    for (auto &kv : n.props) emit(kv.first);
    out += "\n";
    for (auto &c : n.children) writeNode(*c, depth + 1, out);
}

std::string Model::write() const
{
    std::string out = "# AmiForge GUI (edit with Tools > GUI Designer)\n";
    Node w;
    w.kind = Kind::Window;
    w.props = window.props;
    writeNode(w, 0, out);
    for (auto &c : window.children) writeNode(*c, 0, out);
    return out;
}

static void walkNode(Node *n, int depth, std::vector<std::pair<Node *, int>> &out)
{
    out.push_back({ n, depth });
    for (auto &c : n->children) walkNode(c.get(), depth + 1, out);
}

void Model::walk(std::vector<std::pair<Node *, int>> &out)
{
    out.clear();
    walkNode(root(), 0, out);
}

Node *Model::findById(const std::string &id)
{
    std::vector<std::pair<Node *, int>> all;
    walk(all);
    for (auto &p : all)
        if (p.first->get("id") == id) return p.first;
    return nullptr;
}

std::string Model::uniqueId(Kind k)
{
    std::string prefix = kindInfo(k).idPrefix;
    for (int i = 1;; i++) {
        std::string id = prefix + std::to_string(i);
        if (!findById(id)) return id;
    }
}

std::string Model::baseName() const { return identifier(window.get("name")); }

std::vector<std::string> Model::validate()
{
    std::vector<std::string> problems;
    std::vector<std::pair<Node *, int>> all;
    walk(all);
    std::set<std::string> seen;
    for (auto &p : all) {
        std::string id = p.first->get("id");
        if (id.empty()) continue;
        if (identifier(id) != id)
            problems.push_back("\"" + id + "\" isn't a valid C name (use letters, digits and _)");
        else if (!seen.insert(upper(id)).second)
            problems.push_back("the ID name \"" + id + "\" is used more than once");
    }
    std::string name = window.get("name");
    if (identifier(name) != name) problems.push_back("the window's code name \"" + name + "\" isn't a valid C name");
    return problems;
}

// --- code generation ---------------------------------------------------------------------

std::map<std::string, std::string> userSections(const std::string &src)
{
    // "/* USER-BEGIN key */\n ...body... <indent>/* USER-END key */"
    std::map<std::string, std::string> out;
    size_t pos = 0;
    const std::string B = "/* USER-BEGIN ", E = "/* USER-END ";
    while ((pos = src.find(B, pos)) != std::string::npos) {
        size_t ks = pos + B.size();
        size_t ke = src.find(" */", ks);
        size_t bodyStart = src.find('\n', ks);
        pos = ks;
        if (ke == std::string::npos || bodyStart == std::string::npos || ke > bodyStart) continue;
        std::string key = src.substr(ks, ke - ks);
        bodyStart++;
        size_t end = src.find(E + key + " */", bodyStart);
        if (end == std::string::npos) continue;
        size_t endLine = src.rfind('\n', end);
        size_t bodyEnd = endLine == std::string::npos || endLine < bodyStart ? bodyStart : endLine + 1;
        if (!out.count(key)) out[key] = src.substr(bodyStart, bodyEnd - bodyStart);
        pos = end + E.size();
    }
    return out;
}

namespace {

struct Gen {
    Model &m;
    std::string base, BASE;
    std::map<std::string, std::string> keep;   // old user sections
    std::set<std::string> usedKeys;
    std::set<Kind> kindsUsed;
    bool hints = false;
    std::vector<Node *> nodes;                 // with an id, in order
    std::string statics;                       // label arrays

    explicit Gen(Model &model) : m(model) {}

    std::string idName(Node *n) { return BASE + "_" + upper(identifier(n->get("id"))); }

    std::string user(const std::string &key, const std::string &indent, const std::string &def)
    {
        usedKeys.insert(key);
        std::string body = keep.count(key) ? keep[key] : def;
        return indent + "/* USER-BEGIN " + key + " */\n" + body + indent + "/* USER-END " + key + " */\n";
    }

    // The NewObject() call for n, indented by `ind` spaces.
    std::string object(Node *n, int ind)
    {
        std::string I(ind + 4, ' ');
        std::string s;
        bool hasId = !n->get("id").empty();
        const KindInfo &ki = kindInfo(n->kind);
        std::string assign = hasId ? "gui->obj[" + idName(n) + "] = " : "";
        auto cls = [&](const char *c) { return assign + "IIntuition->NewObject(" + std::string(c) + ", NULL,\n"; };
        auto tag = [&](const std::string &t, const std::string &v) { s += I + t + ", " + v + ",\n"; };
        auto gadgetTags = [&] {
            if (ki.gadget && hasId) {
                tag("GA_ID", idName(n));
                tag("GA_RelVerify", "TRUE");
            }
            if (ki.gadget && n->getBool("disabled")) tag("GA_Disabled", "TRUE");
            if (ki.gadget && n->has("hint")) { tag("GA_HintInfo", cString(n->get("hint"))); hints = true; }
        };
        std::string lists = n->has("id") ? base + "_" + identifier(n->get("id")) : "";
        auto labelArray = [&] {
            std::string arr = base + "_" + identifier(n->get("id")) + "_items";
            statics += "static CONST_STRPTR " + arr + "[] = {";
            for (auto &it : splitItems(n->get("items"))) statics += " " + cString(it) + ",";
            statics += " NULL };\n";
            return arr;
        };
        switch (n->kind) {
        case Kind::VLayout:
        case Kind::HLayout: {
            s += cls("ILayout->LAYOUT_GetClass()");
            tag("LAYOUT_Orientation", n->kind == Kind::VLayout ? "LAYOUT_ORIENT_VERT" : "LAYOUT_ORIENT_HORIZ");
            tag("LAYOUT_SpaceOuter", n->parent && n->parent->kind == Kind::Window ? "TRUE" : "FALSE");
            std::string frame = n->get("frame");
            if (n->has("title") && frame == "none") frame = "group";
            if (frame == "group") tag("LAYOUT_BevelStyle", "BVS_GROUP");
            else if (frame == "button") tag("LAYOUT_BevelStyle", "BVS_BUTTON");
            else if (frame == "field") tag("LAYOUT_BevelStyle", "BVS_FIELD");
            if (n->has("title")) tag("LAYOUT_Label", cString(n->get("title")));
            if (n->getBool("even")) tag("LAYOUT_EvenSize", "TRUE");
            for (auto &c : n->children) {
                Node *ch = c.get();
                bool image = ch->kind == Kind::Label;
                s += I + (image ? "LAYOUT_AddImage, " : "LAYOUT_AddChild, ") + object(ch, ind + 4);
                if (ch->has("label"))
                    s += I + "CHILD_Label, IIntuition->NewObject(ILabel->LABEL_GetClass(), NULL, LABEL_Text, " +
                         cString(ch->get("label")) + ", TAG_DONE),\n";
                if (ch->has("weight"))
                    s += I + (n->kind == Kind::HLayout ? "CHILD_WeightedWidth, " : "CHILD_WeightedHeight, ") +
                         std::to_string(ch->getInt("weight")) + ",\n";
                if (ch->has("minwidth")) s += I + "CHILD_MinWidth, " + std::to_string(ch->getInt("minwidth")) + ",\n";
                if (ch->has("minheight")) s += I + "CHILD_MinHeight, " + std::to_string(ch->getInt("minheight")) + ",\n";
            }
            break;
        }
        case Kind::Button:
            s += cls("IButton->BUTTON_GetClass()");
            gadgetTags();
            tag("GA_Text", cString(n->get("text")));
            break;
        case Kind::String:
            s += cls("IString->STRING_GetClass()");
            gadgetTags();
            tag("STRINGA_MaxChars", std::to_string(n->getInt("maxchars")));
            if (n->has("text")) tag("STRINGA_TextVal", cString(n->get("text")));
            break;
        case Kind::Integer:
            s += cls("IInteger->INTEGER_GetClass()");
            gadgetTags();
            tag("INTEGER_Minimum", std::to_string(n->getInt("min")));
            tag("INTEGER_Maximum", std::to_string(n->getInt("max")));
            tag("INTEGER_Number", std::to_string(n->getInt("value")));
            break;
        case Kind::CheckBox:
            s += cls("ICheckBox->CHECKBOX_GetClass()");
            gadgetTags();
            tag("GA_Text", cString(n->get("text")));
            if (n->getBool("checked")) tag("CHECKBOX_Checked", "TRUE");
            break;
        case Kind::Chooser:
            s += cls("IChooser->CHOOSER_GetClass()");
            gadgetTags();
            tag("CHOOSER_PopUp", "TRUE");
            tag("CHOOSER_LabelArray", labelArray());
            tag("CHOOSER_Selected", std::to_string(n->getInt("selected")));
            break;
        case Kind::Radio:
            s += cls("IRadioButton->RADIOBUTTON_GetClass()");
            gadgetTags();
            tag("RADIOBUTTON_LabelArray", labelArray());
            tag("RADIOBUTTON_Selected", std::to_string(n->getInt("selected")));
            break;
        case Kind::Slider:
            s += cls("ISlider->SLIDER_GetClass()");
            gadgetTags();
            tag("SLIDER_Min", std::to_string(n->getInt("min")));
            tag("SLIDER_Max", std::to_string(n->getInt("max")));
            tag("SLIDER_Level", std::to_string(n->getInt("level")));
            tag("SLIDER_Orientation", n->getBool("vertical") ? "SLIDER_VERTICAL" : "SLIDER_HORIZONTAL");
            break;
        case Kind::Scroller:
            s += cls("IScroller->SCROLLER_GetClass()");
            gadgetTags();
            tag("SCROLLER_Total", std::to_string(n->getInt("total")));
            tag("SCROLLER_Visible", std::to_string(n->getInt("visible")));
            tag("SCROLLER_Top", std::to_string(n->getInt("top")));
            tag("SCROLLER_Orientation", n->getBool("vertical") ? "SORIENT_VERT" : "SORIENT_HORIZ");
            break;
        case Kind::ListBrowser:
            s += cls("IListBrowser->LISTBROWSER_GetClass()");
            gadgetTags();
            if (hasId) tag("LISTBROWSER_Labels", "&gui->" + identifier(n->get("id")) + "_list");
            tag("LISTBROWSER_ShowSelected", "TRUE");
            break;
        case Kind::Label:
            s += cls("ILabel->LABEL_GetClass()");
            tag("LABEL_Text", cString(n->get("text")));
            break;
        case Kind::Space:
            s += cls("ISpace->SPACE_GetClass()");
            break;
        case Kind::FuelGauge:
            s += cls("IFuelGauge->FUELGAUGE_GetClass()");
            tag("FUELGAUGE_Min", std::to_string(n->getInt("min")));
            tag("FUELGAUGE_Max", std::to_string(n->getInt("max")));
            tag("FUELGAUGE_Level", std::to_string(n->getInt("level")));
            tag("FUELGAUGE_Percent", "TRUE");
            tag("FUELGAUGE_Justification", "FGJ_CENTER");
            break;
        case Kind::GetFile:
            s += cls("IGetFile->GETFILE_GetClass()");
            gadgetTags();
            tag("GETFILE_TitleText", cString(n->get("title")));
            if (n->has("pattern")) {
                tag("GETFILE_Pattern", cString(n->get("pattern")));
                tag("GETFILE_DoPatterns", "TRUE");
            }
            if (n->getBool("save")) tag("GETFILE_DoSaveMode", "TRUE");
            break;
        case Kind::TextEditor:
            s += cls("ITextEditor->TEXTEDITOR_GetClass()");
            gadgetTags();
            if (n->has("text")) tag("GA_TEXTEDITOR_Contents", cString(n->get("text")));
            break;
        default:
            break;
        }
        (void)lists;
        s += I + "TAG_DONE),\n";
        return s;
    }

    void collect(Node *n)
    {
        kindsUsed.insert(n->kind);
        if (n->has("label")) kindsUsed.insert(Kind::Label);
        if (!n->get("id").empty()) nodes.push_back(n);
        for (auto &c : n->children) collect(c.get());
    }

    std::string defaultHandler(Node *n)
    {
        std::string I = "        ";
        std::string idx = "gui->obj[" + idName(n) + "]";
        switch (n->kind) {
        case Kind::Button: return I + "/* the button was clicked */\n";
        case Kind::String:
            return I + "/* Return was pressed. The text:\n" + I + "   STRPTR text = NULL;\n" + I +
                   "   IIntuition->GetAttr(STRINGA_TextVal, " + idx + ", (uint32 *)&text); */\n";
        case Kind::Integer:
            return I + "/* the number changed. Read it with:\n" + I + "   uint32 value = 0;\n" + I +
                   "   IIntuition->GetAttr(INTEGER_Number, " + idx + ", &value); */\n";
        case Kind::CheckBox: return I + "/* code is TRUE when it was checked, FALSE when unchecked */\n";
        case Kind::Chooser:
        case Kind::Radio: return I + "/* code is the number of the chosen item (0 = first) */\n";
        case Kind::Slider: return I + "/* code is the slider's new level */\n";
        case Kind::Scroller: return I + "/* code is the scroller's new top */\n";
        case Kind::ListBrowser:
            return I + "/* a row was clicked: code is its number. The node:\n" + I +
                   "   struct Node *node = NULL;\n" + I + "   IIntuition->GetAttr(LISTBROWSER_SelectedNode, " + idx +
                   ", (uint32 *)&node); */\n";
        case Kind::GetFile:
            return I + "if (IIntuition->IDoMethod(" + idx + ", GFILE_REQUEST, gui->win)) {\n" + I +
                   "    STRPTR file = NULL;\n" + I + "    IIntuition->GetAttr(GETFILE_FullFile, " + idx +
                   ", (uint32 *)&file);\n" + I + "    /* file is the chosen file */\n" + I + "}\n";
        case Kind::TextEditor: return I + "/* the text changed */\n";
        default: return "";
        }
    }
};

const char *protoFor(Kind k)
{
    switch (k) {
    case Kind::Button: return "button";
    case Kind::String: return "string";
    case Kind::Integer: return "integer";
    case Kind::CheckBox: return "checkbox";
    case Kind::Chooser: return "chooser";
    case Kind::Radio: return "radiobutton";
    case Kind::Slider: return "slider";
    case Kind::Scroller: return "scroller";
    case Kind::ListBrowser: return "listbrowser";
    case Kind::Label: return "label";
    case Kind::Space: return "space";
    case Kind::FuelGauge: return "fuelgauge";
    case Kind::GetFile: return "getfile";
    case Kind::TextEditor: return "texteditor";
    default: return nullptr;
    }
}

const char *headerFor(Kind k)
{
    switch (k) {
    case Kind::Label: return "images/label.h";
    case Kind::Button: return "gadgets/button.h";
    case Kind::String: return "gadgets/string.h";
    case Kind::Integer: return "gadgets/integer.h";
    case Kind::CheckBox: return "gadgets/checkbox.h";
    case Kind::Chooser: return "gadgets/chooser.h";
    case Kind::Radio: return "gadgets/radiobutton.h";
    case Kind::Slider: return "gadgets/slider.h";
    case Kind::Scroller: return "gadgets/scroller.h";
    case Kind::ListBrowser: return "gadgets/listbrowser.h";
    case Kind::Space: return "gadgets/space.h";
    case Kind::FuelGauge: return "gadgets/fuelgauge.h";
    case Kind::GetFile: return "gadgets/getfile.h";
    case Kind::TextEditor: return "gadgets/texteditor.h";
    default: return nullptr;
    }
}

} // namespace

Generated generate(Model &m, const std::string &oldSource)
{
    Gen g(m);
    g.base = m.baseName();
    g.BASE = upper(g.base);
    g.keep = userSections(oldSource);
    Node *root = m.root();
    g.collect(root);

    Generated out;
    out.headerName = g.base + "_gui.h";
    out.sourceName = g.base + "_gui.c";
    std::string afg = g.base + ".afg";
    std::string guard = g.BASE + "_GUI_H";
    std::string S = "struct " + g.base + "_gui";

    // --- header ---
    std::string h;
    h += "/* " + out.headerName + " - generated by AmiForge's GUI designer from " + afg + ".\n"
         " * Don't edit: change the GUI in the designer and generate the code again. */\n";
    h += "#ifndef " + guard + "\n#define " + guard + "\n\n";
    h += "#include <exec/types.h>\n#include <exec/lists.h>\n#include <intuition/classusr.h>\n\n";
    h += "/* the objects' IDs (the gadget IDs of the ones that report events) */\nenum {\n";
    h += "    " + g.BASE + "_NONE = 0,\n";
    for (Node *n : g.nodes) h += "    " + g.idName(n) + ",\n";
    h += "    " + g.BASE + "_NUM_IDS\n};\n\n";
    h += S + " {\n    Object *window;            /* the window.class object */\n"
         "    struct Window *win;        /* the open window, or NULL */\n"
         "    Object *obj[" + g.BASE + "_NUM_IDS];   /* the objects, by ID */\n";
    for (Node *n : g.nodes)
        if (n->kind == Kind::ListBrowser) h += "    struct List " + identifier(n->get("id")) + "_list;\n";
    h += "    BOOL quit;                 /* set by the event code to end the program */\n};\n\n";
    h += "/* Create the window object and its gadgets (FALSE if that failed). */\n";
    h += "BOOL " + g.base + "_gui_create(" + S + " *gui);\n";
    h += "/* Open and close the window; the objects stay. */\n";
    h += "BOOL " + g.base + "_gui_open(" + S + " *gui);\n";
    h += "void " + g.base + "_gui_close(" + S + " *gui);\n";
    h += "/* Free everything create() made (closes the window first). */\n";
    h += "void " + g.base + "_gui_dispose(" + S + " *gui);\n";
    h += "/* The signal to Wait() for. */\n";
    h += "uint32 " + g.base + "_gui_signal(" + S + " *gui);\n";
    h += "/* Handle the window's input; returns FALSE when the program should end. */\n";
    h += "BOOL " + g.base + "_gui_handle(" + S + " *gui);\n\n";
    h += "#endif\n";

    // --- source ---
    std::string c;
    c += "/* " + out.sourceName + " - generated by AmiForge's GUI designer from " + afg + ".\n"
         " * Generate the code again after changing the GUI: what you write between the\n"
         " * USER-BEGIN and USER-END lines is kept, everything else is replaced. */\n";
    c += "#include <string.h>\n\n#include <proto/exec.h>\n#include <proto/intuition.h>\n"
         "#include <proto/window.h>\n#include <proto/layout.h>\n";
    std::set<std::string> protos, headers;
    for (Kind k : g.kindsUsed) {
        if (protoFor(k)) protos.insert(protoFor(k));
        if (headerFor(k)) headers.insert(headerFor(k));
    }
    for (auto &p : protos) c += "#include <proto/" + p + ".h>\n";
    c += "#include <classes/window.h>\n#include <gadgets/layout.h>\n#include <images/bevel.h>\n";
    for (auto &hd : headers) c += "#include <" + hd + ">\n";
    c += "\n#include \"" + out.headerName + "\"\n\n";
    c += g.user("includes", "", "");
    c += "\n";

    std::string body = g.object(root, 8);   // fills statics and hints
    if (!g.statics.empty()) c += g.statics + "\n";

    c += "BOOL " + g.base + "_gui_create(" + S + " *gui)\n{\n";
    c += "    memset(gui, 0, sizeof *gui);\n";
    for (Node *n : g.nodes) {
        if (n->kind != Kind::ListBrowser) continue;
        std::string L = "gui->" + identifier(n->get("id")) + "_list";
        c += "    IExec->NewList(&" + L + ");\n";
        for (auto &it : splitItems(n->get("items"))) {
            c += "    {\n        struct Node *node = IListBrowser->AllocListBrowserNode(1, LBNCA_CopyText, TRUE, "
                 "LBNCA_Text, " + cString(it) + ", TAG_DONE);\n";
            c += "        if (node) IExec->AddTail(&" + L + ", node);\n    }\n";
        }
    }
    int w = m.window.getInt("width"), ht = m.window.getInt("height");
    c += "    gui->window = IIntuition->NewObject(IWindow->WINDOW_GetClass(), NULL,\n";
    c += "        WA_Title, " + cString(m.window.get("title")) + ",\n";
    c += "        WA_DragBar, TRUE, WA_CloseGadget, TRUE, WA_DepthGadget, TRUE, WA_Activate, TRUE,\n";
    c += std::string("        WA_SizeGadget, ") + (m.window.getBool("resizable") ? "TRUE" : "FALSE") + ",\n";
    if (w > 0) c += "        WA_InnerWidth, " + std::to_string(w) + ",\n";
    if (ht > 0) c += "        WA_InnerHeight, " + std::to_string(ht) + ",\n";
    c += "        WA_IDCMP, IDCMP_GADGETUP | IDCMP_CLOSEWINDOW,\n";
    c += "        WINDOW_Position, WPOS_CENTERSCREEN,\n";
    if (g.hints) c += "        WINDOW_GadgetHelp, TRUE,\n";
    c += "        WINDOW_Layout, " + body;
    c += "        TAG_DONE);\n";
    c += "    return gui->window != NULL;\n}\n\n";

    c += "BOOL " + g.base + "_gui_open(" + S + " *gui)\n{\n";
    c += "    if (!gui->win) gui->win = (struct Window *)IIntuition->IDoMethod(gui->window, WM_OPEN);\n";
    c += "    return gui->win != NULL;\n}\n\n";
    c += "void " + g.base + "_gui_close(" + S + " *gui)\n{\n";
    c += "    if (gui->win) IIntuition->IDoMethod(gui->window, WM_CLOSE);\n    gui->win = NULL;\n}\n\n";
    c += "void " + g.base + "_gui_dispose(" + S + " *gui)\n{\n";
    c += "    " + g.base + "_gui_close(gui);\n";
    c += "    if (gui->window) IIntuition->DisposeObject(gui->window);\n    gui->window = NULL;\n";
    for (Node *n : g.nodes)
        if (n->kind == Kind::ListBrowser)
            c += "    IListBrowser->FreeListBrowserList(&gui->" + identifier(n->get("id")) + "_list);\n";
    c += "}\n\n";
    c += "uint32 " + g.base + "_gui_signal(" + S + " *gui)\n{\n";
    c += "    uint32 sig = 0;\n    if (gui->window) IIntuition->GetAttr(WINDOW_SigMask, gui->window, &sig);\n";
    c += "    return sig;\n}\n\n";

    // the events
    c += "/* A gadget reported an event: id is which (" + g.BASE + "_...), code depends on the gadget. */\n";
    c += "static void " + g.base + "_gadget_event(" + S + " *gui, uint32 id, uint16 code)\n{\n";
    c += "    (void)code;\n    switch (id) {\n";
    for (Node *n : g.nodes) {
        if (!kindInfo(n->kind).gadget) continue;
        c += "    case " + g.idName(n) + ":\n";
        c += g.user(g.idName(n), "        ", g.defaultHandler(n));
        c += "        break;\n";
    }
    c += "    default:\n        break;\n    }\n}\n\n";
    c += "BOOL " + g.base + "_gui_handle(" + S + " *gui)\n{\n";
    c += "    uint32 result;\n    uint16 code = 0;\n";
    c += "    while ((result = IIntuition->IDoMethod(gui->window, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {\n";
    c += "        switch (result & WMHI_CLASSMASK) {\n";
    c += "        case WMHI_CLOSEWINDOW:\n";
    c += g.user("close", "            ", "            gui->quit = TRUE;\n");
    c += "            break;\n";
    c += "        case WMHI_GADGETUP:\n";
    c += "            " + g.base + "_gadget_event(gui, result & WMHI_GADGETMASK, code);\n";
    c += "            break;\n        }\n    }\n";
    c += "    return !gui->quit;\n}\n";

    // sections that no longer have a place
    std::string orphans;
    for (auto &kv : g.keep)
        if (!g.usedKeys.count(kv.first) && kv.second.find_first_not_of(" \t\n") != std::string::npos)
            orphans += "/* USER-BEGIN " + kv.first + " */\n" + kv.second + "/* USER-END " + kv.first + " */\n";
    if (!orphans.empty())
        c += "\n#if 0\n/* Code for objects that are no longer in the GUI, kept so it isn't lost: */\n" + orphans + "#endif\n";

    out.header = h;
    out.source = c;
    return out;
}

} // namespace gui
} // namespace af
