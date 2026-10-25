/* ptr X Y [click|right|double|none|rdown|rup] - put the mouse pointer at screen pixel (X,Y) of the
   front screen and click there: input.device IECLASS_NEWPOINTERPOS is absolute, unlike
   QEMU's relative mouse, which OS4 accelerates. A test tool for driving the IDE.
   rdown/rup press/release the right button only: rdown on the title bar, ptr X Y none over a
   menu item, rup there selects it. */
#include <stdlib.h>
#include <string.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <intuition/intuitionbase.h>

static void send(struct IOStdReq *io, struct InputEvent *ie)
{
    io->io_Command = IND_WRITEEVENT;
    io->io_Data = ie;
    io->io_Length = sizeof *ie;
    IExec->DoIO((struct IORequest *)io);
}

static void button(struct IOStdReq *io, UWORD code)
{
    struct InputEvent ie = {0};
    ie.ie_Class = IECLASS_RAWMOUSE;
    ie.ie_Code = code;
    ie.ie_Qualifier = IEQUALIFIER_RELATIVEMOUSE;
    send(io, &ie);
}

int main(int argc, char **argv)
{
    if (argc < 3) { IDOS->PutStr("usage: ptr X Y [click|right|double|none|rdown|rup]\n"); return 10; }
    const char *what = argc > 3 ? argv[3] : "click";
    struct MsgPort *port = IExec->AllocSysObjectTags(ASOT_PORT, TAG_END);
    struct IOStdReq *io = IExec->AllocSysObjectTags(ASOT_IOREQUEST, ASOIOR_Size, sizeof(struct IOStdReq),
                                                     ASOIOR_ReplyPort, port, TAG_END);
    if (!port || !io || IExec->OpenDevice("input.device", 0, (struct IORequest *)io, 0)) return 20;
    struct IEPointerPixel pp;
    struct IntuitionBase *ib = (struct IntuitionBase *)IntuitionBase;
    pp.iepp_Screen = ib->FirstScreen;
    pp.iepp_Position.X = (WORD)atoi(argv[1]);
    pp.iepp_Position.Y = (WORD)atoi(argv[2]);
    struct InputEvent ie = {0};
    ie.ie_Class = IECLASS_NEWPOINTERPOS;
    ie.ie_SubClass = IESUBCLASS_PIXEL;
    ie.ie_Code = IECODE_NOBUTTON;
    ie.ie_EventAddress = &pp;
    send(io, &ie);
    IDOS->Delay(5);
    if (!strcmp(what, "click") || !strcmp(what, "double")) {
        button(io, IECODE_LBUTTON); button(io, IECODE_LBUTTON | IECODE_UP_PREFIX);
        if (!strcmp(what, "double")) { IDOS->Delay(5); button(io, IECODE_LBUTTON); button(io, IECODE_LBUTTON | IECODE_UP_PREFIX); }
    } else if (!strcmp(what, "right")) {
        button(io, IECODE_RBUTTON); IDOS->Delay(10); button(io, IECODE_RBUTTON | IECODE_UP_PREFIX);
    } else if (!strcmp(what, "rdown")) {
        button(io, IECODE_RBUTTON);
    } else if (!strcmp(what, "rup")) {
        button(io, IECODE_RBUTTON | IECODE_UP_PREFIX);
    }
    IExec->CloseDevice((struct IORequest *)io);
    IExec->FreeSysObject(ASOT_IOREQUEST, io);
    IExec->FreeSysObject(ASOT_PORT, port);
    return 0;
}
