/* windows - a debuggee for the Intuition inspector test (tests/os4/toolstest.cpp): opens a
 * window with two boolean gadgets; the test sets a breakpoint in ready(). */
#include <proto/exec.h>
#include <proto/intuition.h>
#include <intuition/intuition.h>
#include <stdio.h>

static struct IntuiText okText = { 1, 0, JAM1, 8, 3, NULL, (STRPTR)"OK", NULL };
static struct IntuiText cancelText = { 1, 0, JAM1, 8, 3, NULL, (STRPTR)"Cancel", NULL };
static struct Gadget cancelGad = { NULL, 100, 40, 70, 16, GFLG_GADGHCOMP, GACT_RELVERIFY, GTYP_BOOLGADGET,
                                   NULL, NULL, &cancelText, 0, NULL, 2, NULL };
static struct Gadget okGad = { &cancelGad, 20, 40, 60, 16, GFLG_GADGHCOMP, GACT_RELVERIFY, GTYP_BOOLGADGET,
                               NULL, NULL, &okText, 0, NULL, 1, NULL };

static void ready(struct Window *w)
{
    printf("window %p open\n", (void *)w);                      /* @ready */
}

int main(void)
{
    struct Window *w = OpenWindowTags(NULL,
        WA_Title, (ULONG)"AmiForge inspector test",
        WA_Left, 60, WA_Top, 80, WA_Width, 260, WA_Height, 90,
        WA_DragBar, TRUE, WA_CloseGadget, TRUE, WA_DepthGadget, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP,
        WA_Gadgets, (ULONG)&okGad,
        TAG_END);
    if (!w) { printf("no window\n"); return 20; }
    ready(w);
    CloseWindow(w);
    return 0;
}
