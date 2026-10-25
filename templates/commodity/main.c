/* ${NAME} - a commodity for AmigaOS 4.1.
 * ${DATE}, ${AUTHOR}
 *
 * Registers with commodities.library and listens for a hot key (default "ctrl alt h").
 * Pressing it - or Exchange's Show Interface - shows a small window that counts the presses.
 * Exchange's Disable/Enable switch the hot key off and on; Remove (or Ctrl-C) quits.
 *
 *   ${LOWER} HOTKEY="ctrl alt h" PRI=0
 */
#include <stdio.h>
#include <string.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/commodities.h>
#include <libraries/commodities.h>
#include <intuition/intuition.h>

static const char version[] __attribute__((used)) = "$VER: ${NAME} 1.0 (${DATE})";

#define EVT_HOTKEY 1L

static struct Window *win;
static long presses;

static void showWindow(void)
{
    if (!win)
        win = IIntuition->OpenWindowTags(NULL, WA_Title, "${NAME}", WA_InnerWidth, 260, WA_InnerHeight, 40,
                                         WA_DragBar, TRUE, WA_CloseGadget, TRUE, WA_DepthGadget, TRUE,
                                         WA_Activate, TRUE, WA_IDCMP, IDCMP_CLOSEWINDOW, TAG_END);
    if (!win) return;
    IIntuition->WindowToFront(win);
    char text[64];
    snprintf(text, sizeof text, "Hot key pressed %ld time%s.", presses, presses == 1 ? "" : "s");
    struct RastPort *rp = win->RPort;
    IGraphics->EraseRect(rp, win->BorderLeft, win->BorderTop, win->Width - win->BorderRight - 1,
                         win->Height - win->BorderBottom - 1);
    IGraphics->SetAPen(rp, 1);
    IGraphics->Move(rp, win->BorderLeft + 10, win->BorderTop + 20);
    IGraphics->Text(rp, text, strlen(text));
}

static void hideWindow(void)
{
    if (win) IIntuition->CloseWindow(win);
    win = NULL;
}

int main(void)
{
    int32 args[2] = { 0, 0 };
    struct RDArgs *rda = IDOS->ReadArgs("HOTKEY/K,PRI/N/K", args, NULL);
    if (!rda) {
        IDOS->PrintFault(IDOS->IoErr(), "${NAME}");
        return RETURN_FAIL;
    }
    const char *hotkey = args[0] ? (const char *)args[0] : "ctrl alt h";
    int pri = args[1] ? *(int32 *)args[1] : 0;

    struct MsgPort *port = IExec->AllocSysObjectTags(ASOT_PORT, TAG_END);
    struct NewBroker nb = { NB_VERSION, "${NAME}", "${NAME} 1.0", "Shows a window on a hot key",
                            NBU_UNIQUE | NBU_NOTIFY, COF_SHOW_HIDE, (BYTE)pri, port, 0 };
    LONG err = 0;
    CxObj *broker = port ? ICommodities->CxBroker(&nb, &err) : NULL;
    if (!broker) {
        IDOS->Printf("${NAME}: can't start (is it running already?)\n");
        if (port) IExec->FreeSysObject(ASOT_PORT, port);
        IDOS->FreeArgs(rda);
        return RETURN_FAIL;
    }
    CxObj *filter = CxFilter(hotkey);
    if (filter) {
        ICommodities->AttachCxObj(filter, CxSender(port, EVT_HOTKEY));
        ICommodities->AttachCxObj(filter, CxTranslate(NULL));
        ICommodities->AttachCxObj(broker, filter);
    }
    if (!filter || ICommodities->CxObjError(filter)) {
        IDOS->Printf("${NAME}: \"%s\" isn't a valid hot key\n", hotkey);
        ICommodities->DeleteCxObjAll(broker);
        IExec->FreeSysObject(ASOT_PORT, port);
        IDOS->FreeArgs(rda);
        return RETURN_FAIL;
    }
    ICommodities->ActivateCxObj(broker, TRUE);
    IDOS->Printf("${NAME}: press %s (Ctrl-C or Exchange's Remove to quit)\n", hotkey);

    BOOL running = TRUE;
    while (running) {
        uint32 winSig = win ? 1UL << win->UserPort->mp_SigBit : 0;
        uint32 got = IExec->Wait((1UL << port->mp_SigBit) | winSig | SIGBREAKF_CTRL_C);
        if (got & SIGBREAKF_CTRL_C) running = FALSE;
        CxMsg *msg;
        while ((msg = (CxMsg *)IExec->GetMsg(port))) {
            uint32 type = ICommodities->CxMsgType(msg), id = ICommodities->CxMsgID(msg);
            IExec->ReplyMsg((struct Message *)msg);
            if (type == CXM_IEVENT && id == EVT_HOTKEY) {
                presses++;
                showWindow();
            } else if (type == CXM_COMMAND) {
                switch (id) {
                case CXCMD_DISABLE: ICommodities->ActivateCxObj(broker, FALSE); break;
                case CXCMD_ENABLE: ICommodities->ActivateCxObj(broker, TRUE); break;
                case CXCMD_APPEAR: showWindow(); break;
                case CXCMD_DISAPPEAR: hideWindow(); break;
                case CXCMD_KILL: running = FALSE; break;
                case CXCMD_UNIQUE: showWindow(); break;   /* started a second time */
                }
            }
        }
        struct IntuiMessage *im;
        while (win && (im = (struct IntuiMessage *)IExec->GetMsg(win->UserPort))) {
            uint32 cls = im->Class;
            IExec->ReplyMsg((struct Message *)im);
            if (cls == IDCMP_CLOSEWINDOW) { hideWindow(); break; }
        }
    }
    hideWindow();
    ICommodities->DeleteCxObjAll(broker);
    struct Message *m;
    while ((m = IExec->GetMsg(port))) IExec->ReplyMsg(m);
    IExec->FreeSysObject(ASOT_PORT, port);
    IDOS->FreeArgs(rda);
    return RETURN_OK;
}
