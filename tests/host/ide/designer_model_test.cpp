// The GUI designer's model: .afg parsing/writing, editing, validation and code generation.
#include "aftest.h"
#include "GuiModel.h"

using namespace af::gui;

static const char *SAMPLE =
    "# a sample\n"
    "window name=demo title=\"Demo \\\"GUI\\\"\" width=300\n"
    "vlayout id=root\n"
    "  hlayout id=opts title=Options\n"
    "    checkbox id=fast text=_Fast checked=1\n"
    "    chooser id=mode items=\"Low|Medium|High\" selected=1 label=Mode\n"
    "  listbrowser id=files items=\"a.c|b.c\" weight=100\n"
    "  hlayout id=buttons even=1\n"
    "    button id=ok text=_OK hint=\"Do it\"\n"
    "    space\n"
    "    button id=cancel text=_Cancel\n";

static bool contains(const std::string &s, const std::string &part) { return s.find(part) != std::string::npos; }

TEST(parse_sample)
{
    Model m;
    std::string err;
    CHECK(m.parse(SAMPLE, err));
    CHECK_EQ(err, std::string());
    CHECK_EQ(m.window.get("title"), std::string("Demo \"GUI\""));
    CHECK_EQ(m.window.getInt("width"), 300);
    CHECK_EQ(m.baseName(), std::string("demo"));
    Node *root = m.root();
    CHECK(root->kind == Kind::VLayout);
    CHECK_EQ((int)root->children.size(), 3);
    Node *mode = m.findById("mode");
    CHECK(mode && mode->kind == Kind::Chooser);
    CHECK(mode && mode->parent == m.findById("opts"));
    CHECK_EQ((int)splitItems(mode->get("items")).size(), 3);
    CHECK_EQ(m.findById("buttons")->children[1]->kind == Kind::Space, true);
    CHECK(m.validate().empty());
}

TEST(write_then_parse_round_trips)
{
    Model m, m2;
    std::string err;
    CHECK(m.parse(SAMPLE, err));
    std::string text = m.write();
    CHECK(m2.parse(text, err));
    CHECK_EQ(m2.write(), text);
    CHECK_EQ(m2.findById("ok")->get("hint"), std::string("Do it"));
}

TEST(defaults_and_set)
{
    Model m;
    Node *b = m.root()->add(Kind::Button);
    CHECK_EQ(b->get("text"), std::string("Button"));   // the kind's default
    b->set("text", "Button");                           // a default isn't stored
    CHECK(!b->props.count("text"));
    b->set("text", "Go");
    CHECK_EQ(b->get("text"), std::string("Go"));
    CHECK_EQ(m.uniqueId(Kind::Button), std::string("button1"));
    b->set("id", "button1");
    CHECK_EQ(m.uniqueId(Kind::Button), std::string("button2"));
}

TEST(parse_errors)
{
    Model m;
    std::string err;
    CHECK(!m.parse("window\nfrobnicate id=x\n", err));
    CHECK(contains(err, "line 2"));
    CHECK(!m.parse("window\nbutton id=x\n", err));             // root must be a layout
    CHECK(!m.parse("window\nvlayout\n  button id=a\n    button id=b\n", err));   // button holds nothing
    CHECK(!m.parse("window\nvlayout\nvlayout\n", err));          // two roots
    CHECK(!m.parse("window title=\"open\n", err));
}

TEST(move_and_detach)
{
    Model m;
    Node *r = m.root();
    Node *a = r->add(Kind::Button); a->set("id", "a");
    Node *b = r->add(Kind::Button); b->set("id", "b");
    Node *g = r->add(Kind::HLayout); g->set("id", "g");
    auto taken = b->detach();
    CHECK_EQ((int)r->children.size(), 2);
    taken->parent = g;
    g->children.push_back(std::move(taken));
    CHECK_EQ(m.findById("b")->parent == g, true);
    CHECK_EQ(a->indexInParent(), 0);
    CHECK_EQ(g->indexInParent(), 1);
}

TEST(validation)
{
    Model m;
    std::string err;
    CHECK(m.parse("window name=x\nvlayout\n  button id=ok\n  button id=OK\n  button id=\"two words\"\n", err));
    auto p = m.validate();
    CHECK_EQ((int)p.size(), 2);
}

TEST(identifiers)
{
    CHECK_EQ(identifier("My App!"), std::string("My_App_"));
    CHECK_EQ(identifier("3d"), std::string("_3d"));
    CHECK_EQ(identifier(""), std::string("_"));
    CHECK_EQ(cString("a\"b\\c\n"), std::string("\"a\\\"b\\\\c\\n\""));
}

TEST(generate_code)
{
    Model m;
    std::string err;
    CHECK(m.parse(SAMPLE, err));
    Generated g = generate(m);
    CHECK_EQ(g.headerName, std::string("demo_gui.h"));
    CHECK_EQ(g.sourceName, std::string("demo_gui.c"));
    // the header: ids in order, the struct, the functions
    CHECK(contains(g.header, "DEMO_NONE = 0,\n    DEMO_ROOT,\n    DEMO_OPTS,\n    DEMO_FAST,\n    DEMO_MODE,"));
    CHECK(contains(g.header, "struct List files_list;"));
    CHECK(contains(g.header, "BOOL demo_gui_create(struct demo_gui *gui);"));
    CHECK(contains(g.header, "BOOL demo_gui_handle(struct demo_gui *gui);"));
    // the source: classes, tags, labels, weights, list nodes, events
    CHECK(contains(g.source, "#include <proto/chooser.h>"));
    CHECK(contains(g.source, "#include <proto/listbrowser.h>"));
    CHECK(!contains(g.source, "#include <proto/slider.h>"));
    CHECK(contains(g.source, "static CONST_STRPTR demo_mode_items[] = { \"Low\", \"Medium\", \"High\", NULL };"));
    CHECK(contains(g.source, "WA_Title, \"Demo \\\"GUI\\\"\","));
    CHECK(contains(g.source, "WA_InnerWidth, 300,"));
    CHECK(!contains(g.source, "WA_InnerHeight"));
    CHECK(contains(g.source, "gui->obj[DEMO_OK] = IIntuition->NewObject(IButton->BUTTON_GetClass(), NULL,"));
    CHECK(contains(g.source, "GA_HintInfo, \"Do it\","));
    CHECK(contains(g.source, "WINDOW_GadgetHelp, TRUE,"));
    CHECK(contains(g.source, "CHILD_Label, IIntuition->NewObject(ILabel->LABEL_GetClass(), NULL, LABEL_Text, \"Mode\", TAG_DONE),"));
    CHECK(contains(g.source, "CHILD_WeightedHeight, 100,"));
    CHECK(contains(g.source, "LAYOUT_Label, \"Options\","));
    CHECK(contains(g.source, "LAYOUT_BevelStyle, BVS_GROUP,"));
    CHECK(contains(g.source, "LAYOUT_EvenSize, TRUE,"));
    CHECK(contains(g.source, "CHECKBOX_Checked, TRUE,"));
    CHECK(contains(g.source, "LBNCA_Text, \"b.c\""));
    CHECK(contains(g.source, "IListBrowser->FreeListBrowserList(&gui->files_list);"));
    CHECK(contains(g.source, "case DEMO_CANCEL:\n        /* USER-BEGIN DEMO_CANCEL */\n"));
    CHECK(!contains(g.source, "case DEMO_ROOT:"));      // layouts don't report events
    CHECK(contains(g.source, "IIntuition->IDoMethod(gui->window, WM_OPEN);"));
    // balanced parentheses: every NewObject( closes
    int depth = 0;
    for (char c : g.source) { if (c == '(') depth++; else if (c == ')') depth--; }
    CHECK_EQ(depth, 0);
}

TEST(user_sections_survive_regeneration)
{
    Model m;
    std::string err;
    CHECK(m.parse(SAMPLE, err));
    std::string src = generate(m).source;
    // the user writes code for the OK button and a global
    std::string mark = "/* USER-BEGIN DEMO_OK */\n";
    size_t at = src.find(mark);
    CHECK(at != std::string::npos);
    size_t endAt = src.find("        /* USER-END DEMO_OK */", at);
    src.replace(at + mark.size(), endAt - at - mark.size(), "        gui->quit = TRUE;   /* my code */\n");
    std::string inc = "/* USER-BEGIN includes */\n";
    src.insert(src.find(inc) + inc.size(), "static int clicks;\n");
    auto secs = userSections(src);
    CHECK_EQ(secs["DEMO_OK"], std::string("        gui->quit = TRUE;   /* my code */\n"));

    // regenerate: kept
    std::string again = generate(m, src).source;
    CHECK(contains(again, "/* USER-BEGIN DEMO_OK */\n        gui->quit = TRUE;   /* my code */\n        /* USER-END DEMO_OK */"));
    CHECK(contains(again, "/* USER-BEGIN includes */\nstatic int clicks;\n/* USER-END includes */"));
    CHECK(!contains(again, "#if 0"));

    // the OK button is removed: its code is parked in #if 0, not lost
    m.findById("ok")->detach();
    std::string parked = generate(m, again).source;
    CHECK(!contains(parked, "case DEMO_OK:"));
    CHECK(contains(parked, "#if 0"));
    CHECK(contains(parked, "gui->quit = TRUE;   /* my code */"));
    // and comes back if the button does
    Node *ok = m.findById("buttons")->add(Kind::Button, 0);
    ok->set("id", "ok");
    std::string back = generate(m, parked).source;
    CHECK(contains(back, "/* USER-BEGIN DEMO_OK */\n        gui->quit = TRUE;   /* my code */\n"));
}

// --- basic --- Boing BASIC's docs/ui.md kinds and keys

static const char *UI_SAMPLE =
    "# AmiForge GUI\n"
    "window name=paint title=Paint width=400 height=300 remember=1 iconify=1 toolwindow=1 dropfiles=1 contextmenu=mnuCtx\n"
    "vlayout id=root splitter=1\n"
    "  toolbar id=tb weight=0 textonly=1 imagesonly=1 vertical=1 hint=Tools\n"
    "    tool id=tbOpen text=Open image=TBImages:open selimage=TBImages:open_s hint=\"Open a file\" menu=mnuOpen\n"
    "    tool separator=1\n"
    "    tool id=tbGrid text=Grid toggle=1 checked=1 disabled=1\n"
    "  hlayout id=row splitter=1\n"
    "    canvas id=view width=320 height=200 scale=2 mousemove=1 rmb=1 fill=1 dropfiles=1 contextmenu=mnuCtx\n"
    "    button id=save text=Save image=PROGDIR:save.png selimage=PROGDIR:save2.png contextmenu=mnuCtx\n"
    "  listbrowser id=files columns=\"Name|Size\" widths=\"60|40\" multiselect=1 sortable=1\n"
    "  texteditor id=notes fixedfont=1\n"
    "  tabs id=pages selected=1\n"
    "    vlayout title=General\n"
    "      treeview id=tree\n"
    "    vlayout title=Sheet\n"
    "      grid id=sheet rows=100 cols=27 fixedrows=2 fixedcols=0 colwidth=80 editable=0 headers=1\n"
    "  image id=logo file=PROGDIR:logo.png frame=1\n"
    "  commondialog id=dlg title=\"Open a picture\" filter=\"Pictures|#?.png\" initdir=RAM: flags=4096\n"
    "  statusbar id=sb panels=\"Ready|Ln 1|100%\" widths=\"0|60|50\"\n"
    "menu id=mnuFile text=Project\n"
    "  menu id=mnuOpen text=Open... key=O\n"
    "  menu text=-\n"
    "  menu id=mnuRecent text=\"Open recent\"\n"
    "    menu id=mnuRecent0 text=(empty) disabled=1\n"
    "  menu id=mnuQuit text=Quit key=Q\n"
    "menu id=mnuView text=View\n"
    "  menu id=mnuGridOn text=Grid checkable=1 checked=1\n"
    "  menu id=mnuZoom1 text=\"Zoom 100%\" radio=1 checked=1\n"
    "menu id=mnuCtx text=Canvas hidden=1\n"
    "  menu id=mnuClear text=Clear\n";

TEST(basic_ui_kinds_parse_and_round_trip)
{
    Model m;
    std::string err;
    CHECK(m.parse(UI_SAMPLE, err));
    CHECK_EQ(err, std::string());
    // every new kind, where it belongs
    CHECK(m.findById("tb")->kind == Kind::Toolbar);
    CHECK_EQ((int)m.findById("tb")->children.size(), 3);
    CHECK(m.findById("tbOpen")->kind == Kind::Tool && m.findById("tbOpen")->parent == m.findById("tb"));
    CHECK(m.findById("pages")->kind == Kind::Tabs);
    CHECK_EQ(m.findById("pages")->children[1]->get("title"), std::string("Sheet"));
    CHECK(m.findById("tree")->kind == Kind::TreeView);
    CHECK(m.findById("sheet")->kind == Kind::Grid);
    CHECK(m.findById("logo")->kind == Kind::Image);
    CHECK(m.findById("dlg")->kind == Kind::CommonDialog);
    CHECK(m.findById("sb")->kind == Kind::StatusBar);
    CHECK(m.findById("mnuOpen")->kind == Kind::Menu);
    CHECK(m.findById("mnuRecent0")->parent == m.findById("mnuRecent"));
    CHECK_EQ((int)m.menus().size(), 3);
    CHECK(m.menus()[2]->getBool("hidden"));
    CHECK(m.root()->kind == Kind::VLayout);
    CHECK(m.validate().empty());
    // every new key is kept
    CHECK(m.window.getBool("remember") && m.window.getBool("iconify") && m.window.getBool("toolwindow"));
    CHECK(m.window.getBool("dropfiles") && m.window.get("contextmenu") == "mnuCtx");
    CHECK(m.root()->getBool("splitter") && m.findById("row")->getBool("splitter"));
    CHECK_EQ(m.findById("save")->get("image"), std::string("PROGDIR:save.png"));
    CHECK_EQ(m.findById("save")->get("selimage"), std::string("PROGDIR:save2.png"));
    CHECK_EQ(m.findById("files")->get("columns"), std::string("Name|Size"));
    CHECK_EQ(m.findById("files")->get("widths"), std::string("60|40"));
    CHECK(m.findById("files")->getBool("multiselect") && m.findById("files")->getBool("sortable"));
    CHECK(m.findById("notes")->getBool("fixedfont"));
    CHECK(m.findById("view")->getBool("mousemove") && m.findById("view")->getBool("dropfiles"));
    CHECK(m.findById("view")->getBool("rmb") && m.findById("view")->getBool("fill"));
    // bars: weight 0 is the default (not written), a weight is kept
    m.findById("sb")->set("weight", "0");
    CHECK(!m.findById("sb")->props.count("weight"));
    CHECK_EQ(m.findById("tb")->getInt("weight"), 0);
    CHECK_EQ(m.findById("sheet")->getInt("rows"), 100);
    CHECK_EQ(m.findById("sheet")->getInt("fixedcols"), 0);
    CHECK(!m.findById("sheet")->getBool("editable") && m.findById("sheet")->getBool("headers"));
    CHECK_EQ(m.findById("sb")->get("widths"), std::string("0|60|50"));
    CHECK_EQ(m.findById("dlg")->get("filter"), std::string("Pictures|#?.png"));
    CHECK_EQ(m.findById("dlg")->get("initdir"), std::string("RAM:"));
    CHECK_EQ(m.findById("dlg")->getInt("flags"), 4096);
    CHECK_EQ(m.findById("mnuOpen")->get("key"), std::string("O"));
    // written and read again: the same file
    std::string out = m.write();
    Model m2;
    CHECK(m2.parse(out, err));
    CHECK_EQ(m2.write(), out);
    CHECK(contains(out, "\nmenu id=mnuFile text=Project\n  menu id=mnuOpen text=Open... key=O\n  menu text=-\n"));
    CHECK(contains(out, "    tool id=tbOpen text=Open image=TBImages:open selimage=TBImages:open_s hint=\"Open a file\" menu=mnuOpen\n"));
    CHECK(contains(out, "grid id=sheet rows=100 cols=27 fixedrows=2 fixedcols=0 headers=1 colwidth=80 editable=0\n"));
    CHECK(contains(out, "window name=paint title=Paint width=400 height=300 remember=1 iconify=1 toolwindow=1 dropfiles=1 contextmenu=mnuCtx\n"));
    // the menus come after the root layout in the file
    CHECK(out.find("\nmenu ") > out.find("statusbar"));
    // defaults (the runtime's) are not written
    Node *g = m.root()->add(Kind::Grid);
    g->set("rows", "10");
    g->set("editable", "1");
    CHECK(g->props.empty());
    CHECK_EQ(g->getInt("cols"), 5);
    CHECK_EQ(g->getInt("colwidth"), 64);
    CHECK_EQ(m.root()->add(Kind::StatusBar)->get("panels"), std::string("Ready"));
    CHECK_EQ(m.findById("mnuClear")->get("checked"), std::string("0"));
}

TEST(basic_ui_kinds_rules)
{
    Model m;
    std::string err;
    CHECK(!m.parse("window\nmenu id=m text=File\nvlayout id=root\n", err));
    CHECK(contains(err, "menus come after the root layout"));
    CHECK(!m.parse("window\nvlayout id=root\n  menu id=m\n", err));
    CHECK(contains(err, "menus hold menus only"));
    CHECK(!m.parse("window\nvlayout id=root\nmenu id=m\n  button id=b\n", err));
    CHECK(contains(err, "menus hold menus only"));
    CHECK(!m.parse("window\nvlayout id=root\n  tool id=t\n", err));
    CHECK(contains(err, "tools go in a toolbar"));
    CHECK(!m.parse("window\nvlayout id=root\n  toolbar id=tb\n    button id=b\n", err));
    CHECK(contains(err, "a toolbar holds tools only"));
    CHECK(!m.parse("window\nvlayout id=root\n  tabs id=t\n    button id=b\n", err));
    CHECK(contains(err, "every page of tabs"));
    CHECK(!m.parse("window\ntabs id=t\n", err));
    CHECK(contains(err, "the root must be a vlayout or hlayout"));
    CHECK(!m.parse("window\nvlayout id=root\nhlayout id=two\n", err));
    CHECK(contains(err, "only one root layout"));
    CHECK(!m.parse("window\nvlayout id=root\n  grid id=g\n    button id=b\n", err));
    // canHold, as the designer uses it
    CHECK(m.parse(UI_SAMPLE, err));
    CHECK(canHold(m.findById("tb"), Kind::Tool));
    CHECK(!canHold(m.findById("tb"), Kind::Button));
    CHECK(canHold(m.findById("pages"), Kind::HLayout));
    CHECK(!canHold(m.findById("pages"), Kind::Grid));
    CHECK(canHold(&m.window, Kind::Menu));
    CHECK(!canHold(&m.window, Kind::VLayout));
    CHECK(canHold(m.findById("mnuFile"), Kind::Menu));
    CHECK(!canHold(m.root(), Kind::Menu));
    CHECK(!canHold(m.root(), Kind::Tool));
    CHECK(canHold(m.root(), Kind::Tabs));
    // the C designer: none of the new kinds, and no BASIC-only property
    for (Kind k : paletteKinds(false)) CHECK(!basicOnly(k));
    for (int k = (int)Kind::Menu; k < (int)Kind::Count; k++) CHECK(basicOnly((Kind)k));
    bool basicProp = false;
    for (auto &p : kindInfo(Kind::Button).props) basicProp = basicProp || (std::string(p.key) == "image" && p.basic);
    CHECK(basicProp);
    for (auto &p : kindInfo(Kind::Button).props)
        if (std::string(p.key) == "text") CHECK(!p.basic);
    // C code ignores the BASIC objects
    Generated gc = generate(m, "");
    CHECK(!contains(gc.source, "SHEET") && !contains(gc.source, "MNUOPEN") && !contains(gc.source, "TBOPEN"));
    CHECK(contains(gc.source, "PAINT_FILES"));
}

TEST(basic_menu_rows)
{
    Model m;
    std::string err;
    CHECK(m.parse(UI_SAMPLE, err));
    std::vector<MenuRow> rows = m.menuRows();
    CHECK_EQ((int)rows.size(), 11);
    CHECK_EQ(rows[0].level, 0);
    CHECK_EQ(rows[0].props["id"], std::string("mnuFile"));
    CHECK_EQ(rows[2].props["text"], std::string("-"));
    CHECK_EQ(rows[4].level, 2);
    CHECK_EQ(rows[4].props["id"], std::string("mnuRecent0"));
    CHECK_EQ(rows[9].props["hidden"], std::string("1"));
    // unchanged rows give the same file
    std::string before = m.write();
    CHECK(m.setMenuRows(rows, err));
    CHECK_EQ(m.write(), before);
    // the editor's operations: a new item under Quit, indent Quit into Open recent, delete View
    MenuRow about;
    about.level = 1;
    about.props["id"] = "mnuAbout";
    about.props["text"] = "About...";
    rows.insert(rows.begin() + 6, about);
    rows[5].level = 2;                       // Quit -> under Open recent
    rows.erase(rows.begin() + 7, rows.begin() + 10);   // View and its two items
    CHECK(m.setMenuRows(rows, err));
    CHECK_EQ((int)m.menus().size(), 2);
    CHECK(m.findById("mnuQuit")->parent == m.findById("mnuRecent"));
    CHECK(m.findById("mnuAbout")->parent == m.findById("mnuFile"));
    CHECK(!m.findById("mnuView") && !m.findById("mnuGridOn"));
    CHECK(m.findById("sheet") != nullptr);   // the layout is untouched
    Model m2;
    CHECK(m2.parse(m.write(), err));
    CHECK_EQ(m2.write(), m.write());
    // levels that don't make a tree
    std::vector<MenuRow> bad(2);
    bad[0].level = 1;
    CHECK(!m.setMenuRows(bad, err));
    CHECK(contains(err, "title"));
    bad[0].level = 0;
    bad[1].level = 2;
    CHECK(!m.setMenuRows(bad, err));
    CHECK(contains(err, "one level below"));
    CHECK(m.findById("mnuAbout") != nullptr);   // a refused change leaves the menus alone
    // no menus at all
    CHECK(m.setMenuRows({}, err));
    CHECK(m.menus().empty());
    CHECK(!contains(m.write(), "\nmenu "));
}
