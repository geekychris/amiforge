/* ${NAME}: an exec device for AmigaOS 4.1 - a skeleton to build a driver on.
 * ${DATE}, ${AUTHOR}
 *
 * Programs open it with OpenDevice("${LOWER}.device", 0, ioreq, 0) (or with a path) and send it
 * IOStdReqs. This one keeps a small buffer: CMD_WRITE stores into it and CMD_READ reads it back.
 * Every command finishes at once (it's a "quick" device); a real driver would start the
 * hardware in BeginIO, reply when it's done, and cancel a pending request in AbortIO.
 *
 * The device is linked without the C library's start-up code (-nostartfiles), so it can't use
 * newlib functions such as memcpy(): use exec's (CopyMem) or plain loops.
 */
#include <exec/exec.h>
#include <exec/errors.h>
#include <proto/exec.h>
#include <dos/dos.h>
#include <devices/newstyle.h>

#include "${LOWER}.h"

#define DEVNAME  ${UPPER}_NAME
#define VERSION  1
#define REVISION 0
#define VSTRING  "${LOWER}.device 1.0 (${DATE})"

static const char __attribute__((used)) verstag[] = "\0$VER: " VSTRING;

struct ${IDENT}Base {
    struct Device dev;
    BPTR segList;
    struct SignalSemaphore lock;    /* BeginIO can be called by several tasks at once */
    uint8 buffer[${UPPER}_BUFSIZE];
};

int32 _start(void);
int32 _start(void)
{
    return RETURN_FAIL;
}

static void devOpen(struct DeviceManagerInterface *Self, struct IORequest *ior, uint32 unit, uint32 flags)
{
    struct ${IDENT}Base *base = (struct ${IDENT}Base *)Self->Data.LibBase;
    (void)flags;
    if (unit != 0 || ior->io_Message.mn_Length < sizeof(struct IOStdReq)) {
        ior->io_Error = IOERR_OPENFAIL;
        return;
    }
    base->dev.dd_Library.lib_OpenCnt++;
    base->dev.dd_Library.lib_Flags &= ~LIBF_DELEXP;
    ior->io_Device = (struct Device *)base;
    ior->io_Unit = NULL;
    ior->io_Error = 0;
}

static APTR devExpunge(struct DeviceManagerInterface *Self)
{
    struct ExecIFace *IExec = (struct ExecIFace *)(*(struct ExecBase **)4)->MainInterface;
    struct ${IDENT}Base *base = (struct ${IDENT}Base *)Self->Data.LibBase;
    if (base->dev.dd_Library.lib_OpenCnt > 0) {
        base->dev.dd_Library.lib_Flags |= LIBF_DELEXP;
        return NULL;
    }
    APTR seg = (APTR)base->segList;
    IExec->Remove((struct Node *)base);
    IExec->DeleteLibrary((struct Library *)base);
    return seg;
}

static APTR devClose(struct DeviceManagerInterface *Self, struct IORequest *ior)
{
    struct ${IDENT}Base *base = (struct ${IDENT}Base *)Self->Data.LibBase;
    ior->io_Device = (struct Device *)-1;
    ior->io_Unit = (struct Unit *)-1;
    base->dev.dd_Library.lib_OpenCnt--;
    if (base->dev.dd_Library.lib_OpenCnt == 0 && (base->dev.dd_Library.lib_Flags & LIBF_DELEXP))
        return devExpunge(Self);
    return NULL;
}

static const uint16 supported[] = { CMD_READ, CMD_WRITE, CMD_CLEAR, NSCMD_DEVICEQUERY, 0 };

static void devBeginIO(struct DeviceManagerInterface *Self, struct IORequest *ior)
{
    struct ExecIFace *IExec = (struct ExecIFace *)(*(struct ExecBase **)4)->MainInterface;
    struct ${IDENT}Base *base = (struct ${IDENT}Base *)Self->Data.LibBase;
    struct IOStdReq *io = (struct IOStdReq *)ior;
    io->io_Error = 0;
    io->io_Actual = 0;
    IExec->ObtainSemaphore(&base->lock);
    switch (io->io_Command) {
    case CMD_READ:
    case CMD_WRITE: {
        uint32 off = io->io_Offset, len = io->io_Length;
        if (off > ${UPPER}_BUFSIZE) { io->io_Error = IOERR_BADADDRESS; break; }
        if (len > ${UPPER}_BUFSIZE - off) len = ${UPPER}_BUFSIZE - off;
        if (io->io_Command == CMD_READ) IExec->CopyMem(base->buffer + off, io->io_Data, len);
        else IExec->CopyMem(io->io_Data, base->buffer + off, len);
        io->io_Actual = len;
        break;
    }
    case CMD_CLEAR:
        for (int i = 0; i < ${UPPER}_BUFSIZE; i++) base->buffer[i] = 0;
        break;
    case NSCMD_DEVICEQUERY: {
        struct NSDeviceQueryResult *q = (struct NSDeviceQueryResult *)io->io_Data;
        if (!q || io->io_Length < sizeof *q) { io->io_Error = IOERR_BADLENGTH; break; }
        q->DevQueryFormat = 0;
        q->SizeAvailable = sizeof *q;
        q->DeviceType = NSDEVTYPE_UNKNOWN;
        q->DeviceSubType = 0;
        q->SupportedCommands = (UWORD *)supported;
        io->io_Actual = sizeof *q;
        break;
    }
    default:
        io->io_Error = IOERR_NOCMD;
        break;
    }
    IExec->ReleaseSemaphore(&base->lock);
    /* finished: reply unless the caller asked for quick I/O (DoIO, or IOF_QUICK) */
    if (!(io->io_Flags & IOF_QUICK)) IExec->ReplyMsg(&io->io_Message);
}

static void devAbortIO(struct DeviceManagerInterface *Self, struct IORequest *ior)
{
    /* every request finishes inside BeginIO, so there's never one to abort */
    (void)Self;
    (void)ior;
}

static struct Library *devInit(struct Library *lib, BPTR seglist, struct ExecIFace *IExec)
{
    struct ${IDENT}Base *base = (struct ${IDENT}Base *)lib;
    base->dev.dd_Library.lib_Node.ln_Type = NT_DEVICE;
    base->dev.dd_Library.lib_Node.ln_Name = (STRPTR)DEVNAME;
    base->dev.dd_Library.lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
    base->dev.dd_Library.lib_Version = VERSION;
    base->dev.dd_Library.lib_Revision = REVISION;
    base->dev.dd_Library.lib_IdString = (STRPTR)VSTRING;
    base->segList = seglist;
    IExec->InitSemaphore(&base->lock);
    for (int i = 0; i < ${UPPER}_BUFSIZE; i++) base->buffer[i] = 0;
    return lib;
}

static uint32 devObtain(struct DeviceManagerInterface *Self) { return ++Self->Data.RefCount; }
static uint32 devRelease(struct DeviceManagerInterface *Self) { return --Self->Data.RefCount; }

static const APTR devManagerVectors[] = {
    (APTR)devObtain, (APTR)devRelease, NULL, NULL,
    (APTR)devOpen, (APTR)devClose, (APTR)devExpunge, NULL,
    (APTR)devBeginIO, (APTR)devAbortIO,
    (APTR)-1
};

static const struct TagItem devManagerTags[] = {
    { MIT_Name, (Tag)"__device" },
    { MIT_VectorTable, (Tag)devManagerVectors },
    { MIT_Version, 1 },
    { TAG_DONE, 0 }
};

static const CONST_APTR devInterfaces[] = { devManagerTags, NULL };

static const struct TagItem devCreateTags[] = {
    { CLT_DataSize, sizeof(struct ${IDENT}Base) },
    { CLT_InitFunc, (Tag)devInit },
    { CLT_Interfaces, (Tag)devInterfaces },
    { TAG_DONE, 0 }
};

static const struct Resident __attribute__((used)) devResident = {
    RTC_MATCHWORD,
    (struct Resident *)&devResident,
    (APTR)(&devResident + 1),
    RTF_NATIVE | RTF_AUTOINIT,
    VERSION,
    NT_DEVICE,
    0,
    (STRPTR)DEVNAME,
    (STRPTR)VSTRING,
    (APTR)devCreateTags
};
