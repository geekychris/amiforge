/* ${NAME} - an Intuition window with a GadTools menu, for AmigaOS 4.1.
 * ${DATE}, ${AUTHOR}
 *
 * The Draw menu fills the window with lines or boxes; Project > Quit (or the close
 * gadget, or Ctrl-C in the Shell) ends the program. The window can be resized: it's
 * redrawn to fit.
 */
#include <stdlib.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>
#include <graphics/rpattr.h>

static const char version[] __attribute__((used)) = "$VER: ${NAME} 1.0 (${DATE})";

enum { MENU_ABOUT = 1, MENU_QUIT, MENU_LINES, MENU_BOXES, MENU_CLEAR };

static struct NewMenu menus[] = {
    { NM_TITLE, (STRPTR)"Project", NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"About...", (STRPTR)"?", 0, 0, (APTR)MENU_ABOUT },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Quit", (STRPTR)"Q", 0, 0, (APTR)MENU_QUIT },
    { NM_TITLE, (STRPTR)"Draw", NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Lines", (STRPTR)"L", 0, 0, (APTR)MENU_LINES },
    { NM_ITEM, (STRPTR)"Boxes", (STRPTR)"B", 0, 0, (APTR)MENU_BOXES },
    { NM_ITEM, (STRPTR)"Clear", (STRPTR)"C", 0, 0, (APTR)MENU_CLEAR },
    { NM_END, NULL, NULL, 0, 0, NULL }
};

static int mode = MENU_LINES;

static void draw(struct Window *w)
{
    struct RastPort *rp = w->RPort;
    int x0 = w->BorderLeft, y0 = w->BorderTop;
    int x1 = w->Width - w->BorderRight - 1, y1 = w->Height - w->BorderBottom - 1;
    IGraphics->SetRPAttrs(rp, RPTAG_APenColor, 0xFF102040, TAG_END);
    IGraphics->RectFill(rp, x0, y0, x1, y1);
    if (mode == MENU_CLEAR) return;
    for (int i = 0; i < 60; i++) {
        uint32 colour = 0xFF000000 | (uint32)(rand() & 0xFFFFFF);
        IGraphics->SetRPAttrs(rp, RPTAG_APenColor, colour, TAG_END);
        int ax = x0 + rand() % (x1 - x0 + 1), ay = y0 + rand() % (y1 - y0 + 1);
        int bx = x0 + rand() % (x1 - x0 + 1), by = y0 + rand() % (y1 - y0 + 1);
        if (mode == MENU_LINES) {
            IGraphics->Move(rp, ax, ay);
            IGraphics->Draw(rp, bx, by);
        } else {
            IGraphics->RectFill(rp, ax < bx ? ax : bx, ay < by ? ay : by, ax < bx ? bx : ax, ay < by ? by : ay);
        }
    }
}

int main(void)
{
    struct Window *win = IIntuition->OpenWindowTags(NULL,
        WA_Title, "${NAME}",
        WA_InnerWidth, 400, WA_InnerHeight, 260,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_SizeGadget, TRUE,
        WA_MinWidth, 120, WA_MinHeight, 80, WA_MaxWidth, ~0, WA_MaxHeight, ~0,
        WA_Activate, TRUE, WA_SmartRefresh, TRUE, WA_NewLookMenus, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_MENUPICK | IDCMP_NEWSIZE,
        TAG_END);
    if (!win) {
        IDOS->Printf("${NAME}: can't open the window\n");
        return RETURN_FAIL;
    }
    APTR vi = IGadTools->GetVisualInfoA(win->WScreen, NULL);
    struct Menu *strip = IGadTools->CreateMenusA(menus, NULL);
    if (vi && strip && IGadTools->LayoutMenus(strip, vi, GTMN_NewLookMenus, TRUE, TAG_END))
        IIntuition->SetMenuStrip(win, strip);
    draw(win);

    BOOL running = TRUE;
    uint32 winSig = 1UL << win->UserPort->mp_SigBit;
    while (running) {
        uint32 got = IExec->Wait(winSig | SIGBREAKF_CTRL_C);
        if (got & SIGBREAKF_CTRL_C) running = FALSE;
        struct IntuiMessage *im;
        while ((im = (struct IntuiMessage *)IExec->GetMsg(win->UserPort))) {
            uint32 cls = im->Class;
            uint16 code = im->Code;
            IExec->ReplyMsg((struct Message *)im);
            if (cls == IDCMP_CLOSEWINDOW) running = FALSE;
            else if (cls == IDCMP_NEWSIZE) draw(win);
            else if (cls == IDCMP_MENUPICK) {
                while (code != MENUNULL) {
                    struct MenuItem *item = IIntuition->ItemAddress(strip, code);
                    if (!item) break;
                    int id = (int)(uint32)GTMENUITEM_USERDATA(item);
                    if (id == MENU_QUIT) running = FALSE;
                    else if (id == MENU_ABOUT) {
                        struct EasyStruct es = { sizeof(struct EasyStruct), 0, "About ${NAME}",
                                                 "${NAME} 1.0\nAn Intuition and GadTools example.", "_OK", NULL, NULL };
                        IIntuition->EasyRequest(win, &es, NULL);
                    } else {
                        mode = id;
                        draw(win);
                    }
                    code = item->NextSelect;
                }
            }
        }
    }
    IIntuition->ClearMenuStrip(win);
    if (strip) IGadTools->FreeMenus(strip);
    if (vi) IGadTools->FreeVisualInfo(vi);
    IIntuition->CloseWindow(win);
    return RETURN_OK;
}
