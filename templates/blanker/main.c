/* ${NAME} - a screen blanker commodity for AmigaOS 4.1.
 * ${DATE}, ${AUTHOR}
 *
 * Blanks the display after TIMEOUT seconds without keyboard or mouse input, or straight away
 * on the hot key; any input brings the display back. While blanked it draws a starfield.
 * Exchange can disable it or remove it; Ctrl-C also quits.
 *
 *   ${LOWER} TIMEOUT=60 HOTKEY="ctrl alt b"     run as a commodity
 *   ${LOWER} TEST                               blank for three seconds and quit (for trying it)
 */
#include <stdlib.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/commodities.h>
#include <libraries/commodities.h>
#include <intuition/intuition.h>
#include <intuition/pointerclass.h>
#include <devices/timer.h>
#include <devices/inputevent.h>
#include <graphics/rpattr.h>

static const char version[] __attribute__((used)) = "$VER: ${NAME} 1.0 (${DATE})";

enum { EVT_HOTKEY = 1, EVT_INPUT };
#define STARS 120

static struct Screen *scr;
static struct Window *bwin;
static struct { int x, y, z; } stars[STARS];

static void blank(void)
{
    if (scr) return;
    scr = IIntuition->OpenScreenTags(NULL, SA_LikeWorkbench, TRUE, SA_Quiet, TRUE, SA_ShowTitle, FALSE,
                                     SA_Title, "${NAME}", TAG_END);
    if (!scr) return;
    bwin = IIntuition->OpenWindowTags(NULL, WA_CustomScreen, scr, WA_Backdrop, TRUE, WA_Borderless, TRUE,
                                      WA_Activate, TRUE, WA_RMBTrap, TRUE, WA_PointerType, POINTERTYPE_NONE,
                                      WA_Width, scr->Width, WA_Height, scr->Height, TAG_END);
    struct RastPort *rp = bwin ? bwin->RPort : &scr->RastPort;
    IGraphics->SetRPAttrs(rp, RPTAG_APenColor, 0xFF000000, TAG_END);
    IGraphics->RectFill(rp, 0, 0, scr->Width - 1, scr->Height - 1);
    for (int i = 0; i < STARS; i++) {
        stars[i].x = rand() % 2000 - 1000;
        stars[i].y = rand() % 2000 - 1000;
        stars[i].z = rand() % 1000 + 1;
    }
}

static void unblank(void)
{
    if (bwin) IIntuition->CloseWindow(bwin);
    if (scr) IIntuition->CloseScreen(scr);
    bwin = NULL;
    scr = NULL;
}

/* One frame of the starfield. */
static void animate(void)
{
    if (!scr) return;
    struct RastPort *rp = bwin ? bwin->RPort : &scr->RastPort;
    int cx = scr->Width / 2, cy = scr->Height / 2;
    for (int i = 0; i < STARS; i++) {
        int px = cx + stars[i].x * 200 / stars[i].z, py = cy + stars[i].y * 200 / stars[i].z;
        IGraphics->SetRPAttrs(rp, RPTAG_APenColor, 0xFF000000, TAG_END);
        IGraphics->RectFill(rp, px, py, px + 1, py + 1);
        stars[i].z -= 8;
        if (stars[i].z < 1) stars[i].z += 1000;
        px = cx + stars[i].x * 200 / stars[i].z;
        py = cy + stars[i].y * 200 / stars[i].z;
        if (px < 0 || py < 0 || px >= scr->Width - 1 || py >= scr->Height - 1) continue;
        uint32 b = 255 - stars[i].z / 4;
        IGraphics->SetRPAttrs(rp, RPTAG_APenColor, 0xFF000000 | b << 16 | b << 8 | b, TAG_END);
        IGraphics->RectFill(rp, px, py, px + 1, py + 1);
    }
}

int main(void)
{
    int32 args[3] = { 0, 0, 0 };
    struct RDArgs *rda = IDOS->ReadArgs("TIMEOUT/N/K,HOTKEY/K,TEST/S", args, NULL);
    if (!rda) {
        IDOS->PrintFault(IDOS->IoErr(), "${NAME}");
        return RETURN_FAIL;
    }
    int timeout = args[0] ? *(int32 *)args[0] : 30;
    const char *hotkey = args[1] ? (const char *)args[1] : "ctrl alt b";
    BOOL test = args[2] != 0;

    /* a 1/20 s tick from timer.device: the idle count and the animation */
    struct MsgPort *tport = IExec->AllocSysObjectTags(ASOT_PORT, TAG_END);
    struct TimeRequest *tr = tport ? IExec->AllocSysObjectTags(ASOT_IOREQUEST, ASOIOR_Size, sizeof(struct TimeRequest),
                                                               ASOIOR_ReplyPort, tport, TAG_END) : NULL;
    if (!tr || IExec->OpenDevice(TIMERNAME, UNIT_MICROHZ, (struct IORequest *)tr, 0) != 0) {
        IDOS->Printf("${NAME}: can't open timer.device\n");
        return RETURN_FAIL;
    }
    struct MsgPort *port = IExec->AllocSysObjectTags(ASOT_PORT, TAG_END);
    CxObj *broker = NULL;
    if (!test) {
        struct NewBroker nb = { NB_VERSION, "${NAME}", "${NAME} 1.0", "Blanks the screen",
                                NBU_UNIQUE | NBU_NOTIFY, 0, 0, port, 0 };
        broker = ICommodities->CxBroker(&nb, NULL);
        if (!broker) {
            IDOS->Printf("${NAME}: can't start (is it running already?)\n");
            return RETURN_FAIL;
        }
        CxObj *hk = CxFilter(hotkey);
        ICommodities->AttachCxObj(hk, CxSender(port, EVT_HOTKEY));
        ICommodities->AttachCxObj(hk, CxTranslate(NULL));
        ICommodities->AttachCxObj(broker, hk);
        /* every key and mouse event, to measure idle time and to wake up */
        static const uint8 classes[] = { IECLASS_RAWKEY, IECLASS_RAWMOUSE };
        for (int i = 0; i < 2; i++) {
            IX ix = { IX_VERSION, classes[i], 0, 0, 0, 0, 0 };
            CxObj *f = CxFilter(NULL);
            ICommodities->SetFilterIX(f, &ix);
            ICommodities->AttachCxObj(f, CxSender(port, EVT_INPUT));
            ICommodities->AttachCxObj(broker, f);
        }
        ICommodities->ActivateCxObj(broker, TRUE);
        IDOS->Printf("${NAME}: blanks after %ld seconds, or on %s\n", (long)timeout, hotkey);
    } else {
        blank();
    }

    int ticks = 0, idle = 0, blankedAt = 0;
    BOOL running = TRUE, enabled = TRUE;
    tr->Request.io_Command = TR_ADDREQUEST;
    tr->Time.Seconds = 0;
    tr->Time.Microseconds = 50000;
    IExec->SendIO((struct IORequest *)tr);
    while (running) {
        uint32 got = IExec->Wait((1UL << port->mp_SigBit) | (1UL << tport->mp_SigBit) | SIGBREAKF_CTRL_C);
        if (got & SIGBREAKF_CTRL_C) running = FALSE;
        if (IExec->CheckIO((struct IORequest *)tr)) {
            IExec->WaitIO((struct IORequest *)tr);
            ticks++;
            idle++;
            if (scr) animate();
            else if (enabled && idle >= timeout * 20) { blank(); blankedAt = ticks; }
            if (test && ticks >= 60) running = FALSE;
            tr->Time.Seconds = 0;
            tr->Time.Microseconds = 50000;
            IExec->SendIO((struct IORequest *)tr);
        }
        CxMsg *msg;
        while ((msg = (CxMsg *)IExec->GetMsg(port))) {
            uint32 type = ICommodities->CxMsgType(msg), id = ICommodities->CxMsgID(msg);
            IExec->ReplyMsg((struct Message *)msg);
            if (type == CXM_IEVENT && id == EVT_HOTKEY) { if (enabled && !scr) { blank(); blankedAt = ticks; } idle = 0; }
            else if (type == CXM_IEVENT && id == EVT_INPUT) {
                idle = 0;
                if (scr && ticks - blankedAt > 10) unblank();   /* not the key that blanked it */
            } else if (type == CXM_COMMAND) {
                if (id == CXCMD_DISABLE) { enabled = FALSE; ICommodities->ActivateCxObj(broker, FALSE); }
                else if (id == CXCMD_ENABLE) { enabled = TRUE; ICommodities->ActivateCxObj(broker, TRUE); }
                else if (id == CXCMD_KILL) running = FALSE;
            }
        }
    }
    unblank();
    if (!IExec->CheckIO((struct IORequest *)tr)) IExec->AbortIO((struct IORequest *)tr);
    IExec->WaitIO((struct IORequest *)tr);
    IExec->CloseDevice((struct IORequest *)tr);
    IExec->FreeSysObject(ASOT_IOREQUEST, tr);
    IExec->FreeSysObject(ASOT_PORT, tport);
    if (broker) ICommodities->DeleteCxObjAll(broker);
    struct Message *m;
    while ((m = IExec->GetMsg(port))) IExec->ReplyMsg(m);
    IExec->FreeSysObject(ASOT_PORT, port);
    IDOS->FreeArgs(rda);
    return RETURN_OK;
}
