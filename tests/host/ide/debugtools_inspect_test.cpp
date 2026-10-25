#include "aftest.h"
#include "InspectModel.h"

using namespace af;

TEST(inspect_idcmp)
{
    CHECK_EQ(decodeIdcmp(0), std::string(""));
    CHECK_EQ(decodeIdcmp(0x00000200 | 0x00000040), std::string("GADGETUP|CLOSEWINDOW"));
    CHECK_EQ(decodeIdcmp(0x00400000), std::string("INTUITICKS"));
    CHECK_EQ(decodeIdcmp(0x20000000), std::string("0x20000000"));   // reserved: shown in hex
}

TEST(inspect_window_flags)
{
    // SMART_REFRESH is the zero value of the refresh field
    CHECK_EQ(decodeWindowFlags(0x0000000F), std::string("SMART_REFRESH|SIZEGADGET|DRAGBAR|DEPTHGADGET|CLOSEGADGET"));
    CHECK_EQ(decodeWindowFlags(0x00000040 | 0x00001000), std::string("SIMPLE_REFRESH|ACTIVATE"));
    CHECK_EQ(decodeWindowFlags(0x000000C0), std::string("OTHER_REFRESH"));
}

TEST(inspect_gadget_type)
{
    CHECK_EQ(decodeGadgetType(0x0001), std::string("BOOLGADGET"));
    CHECK_EQ(decodeGadgetType(0x0005 | 0x2000), std::string("CUSTOMGADGET|GZZGADGET"));
    CHECK_EQ(decodeGadgetType(0x8000 | 0x0080), std::string("SYS:CLOSE"));
    CHECK_EQ(decodeGadgetType(0x8000 | 0x0020), std::string("SYS:WDRAGGING"));
}

TEST(inspect_gadget_flags_and_activation)
{
    CHECK_EQ(decodeGadgetFlags(0x0000), std::string("GADGHCOMP"));
    CHECK_EQ(decodeGadgetFlags(0x0003 | 0x0100), std::string("GADGHNONE|DISABLED"));
    CHECK_EQ(decodeGadgetFlags(0x0001 | 0x1000), std::string("GADGHBOX|LABELSTRING"));
    CHECK_EQ(decodeActivation(0x0001 | 0x0002), std::string("RELVERIFY|IMMEDIATE"));
}

TEST(inspect_signals)
{
    InspectSnapshot s;
    WindowInfo w;
    w.title = "Bouncer";
    w.sigBit = 31;
    s.windows.push_back(w);
    CHECK_EQ(decodeSignals((1u << 31) | (1u << 12), s), std::string("CTRL-C IDCMP 'Bouncer'"));
    CHECK_EQ(decodeSignals(0, s), std::string("(none)"));
    CHECK_EQ(decodeSignals(1u << 20, s), std::string("bit 20"));
    CHECK_EQ(decodeTaskState(4), std::string("waiting"));
}

TEST(inspect_rows_tree)
{
    InspectSnapshot s;
    s.task.valid = true;
    s.task.name = "bouncer";
    s.task.state = 4;
    s.task.sigWait = 1u << 30;
    ScreenInfo sc;
    sc.addr = 0x100; sc.title = "Workbench Screen"; sc.pubName = "Workbench"; sc.width = 1024; sc.height = 768; sc.depth = 32;
    s.screens.push_back(sc);
    WindowInfo w;
    w.addr = 0x200; w.screen = 0x100; w.title = "Bouncer"; w.sigBit = 30; w.idcmp = 0x200;
    w.width = 320; w.height = 200;
    GadgetInfo g;
    g.id = 7; g.type = 0x0005; g.className = "button.gadget"; g.text = "Quit";
    w.gadgets.push_back(g);
    s.windows.push_back(w);
    WindowInfo orphan;
    orphan.title = "Elsewhere"; orphan.screen = 0x999;
    s.windows.push_back(orphan);

    auto rows = inspectRows(s);
    CHECK_EQ(rows[0].name, std::string("Task"));
    CHECK_EQ(rows[0].value, std::string("bouncer (waiting)"));
    CHECK_EQ(rows[1].value, std::string("IDCMP 'Bouncer'"));
    int screenRow = -1, winRow = -1, gadRow = -1, orphanRow = -1;
    for (size_t i = 0; i < rows.size(); i++) {
        if (rows[i].kind == InspKind::Screen) screenRow = (int)i;
        if (rows[i].kind == InspKind::Window && rows[i].index == 0) winRow = (int)i;
        if (rows[i].kind == InspKind::Window && rows[i].index == 1) orphanRow = (int)i;
        if (rows[i].kind == InspKind::Gadget) gadRow = (int)i;
    }
    CHECK(screenRow >= 0 && winRow > screenRow && gadRow > winRow && orphanRow > gadRow);
    if (winRow < 0 || gadRow < 0 || orphanRow < 0) return;
    CHECK_EQ(rows[winRow].depth, 1);
    CHECK_EQ(rows[winRow].name, std::string("Window 'Bouncer'"));
    CHECK_EQ(rows[gadRow].name, std::string("Gadget 7 (button.gadget) 'Quit'"));
    CHECK_EQ(rows[gadRow].window, 0);
    CHECK_EQ(rows[orphanRow].depth, 0);     // its screen isn't listed: top level
    CHECK_EQ(rows[screenRow].value, std::string("1024x768x32  public 'Workbench'"));
}

TEST(inspect_rows_no_task)
{
    InspectSnapshot s;
    auto rows = inspectRows(s);
    CHECK_EQ(rows.size(), (size_t)2);
    CHECK_EQ(rows[1].value, std::string("(the program has no windows open)"));
}
