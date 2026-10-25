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
