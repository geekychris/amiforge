/* test_${LOWER} - opens ${LOWER}.device (the one next to it), writes to it and reads back.
 * Built and run by the run-test script: Build > Run (Ctrl-F5) in AmiForge.
 */
#include <stdio.h>
#include <string.h>

#include <proto/exec.h>
#include <devices/newstyle.h>

#include "${LOWER}.h"

int main(void)
{
    struct MsgPort *port = IExec->AllocSysObjectTags(ASOT_PORT, TAG_END);
    struct IOStdReq *io = IExec->AllocSysObjectTags(ASOT_IOREQUEST, ASOIOR_Size, sizeof(struct IOStdReq),
                                                    ASOIOR_ReplyPort, port, TAG_END);
    if (!io || IExec->OpenDevice("PROGDIR:${LOWER}.device", 0, (struct IORequest *)io, 0) != 0) {
        printf("Can't open ${LOWER}.device\n");
        return 20;
    }
    const char *msg = "Hello, device!";
    io->io_Command = CMD_WRITE;
    io->io_Data = (APTR)msg;
    io->io_Length = strlen(msg) + 1;
    io->io_Offset = 0;
    IExec->DoIO((struct IORequest *)io);
    printf("CMD_WRITE: error %ld, wrote %ld bytes\n", (long)io->io_Error, (long)io->io_Actual);

    char back[64] = "";
    io->io_Command = CMD_READ;
    io->io_Data = back;
    io->io_Length = sizeof back;
    io->io_Offset = 0;
    IExec->DoIO((struct IORequest *)io);
    printf("CMD_READ: error %ld, read \"%s\"\n", (long)io->io_Error, back);

    struct NSDeviceQueryResult q = { 0 };
    io->io_Command = NSCMD_DEVICEQUERY;
    io->io_Data = &q;
    io->io_Length = sizeof q;
    IExec->DoIO((struct IORequest *)io);
    int n = 0;
    if (io->io_Error == 0 && q.SupportedCommands)
        while (q.SupportedCommands[n]) n++;
    printf("NSCMD_DEVICEQUERY: %ld commands\n", (long)n);

    io->io_Command = 0x7777;   /* an unknown command */
    IExec->DoIO((struct IORequest *)io);
    printf("Unknown command: error %ld (IOERR_NOCMD is %ld)\n", (long)io->io_Error, (long)IOERR_NOCMD);

    int ok = strcmp(back, msg) == 0 && n == 4 && io->io_Error == IOERR_NOCMD;
    printf("%s\n", ok ? "All tests passed." : "A test FAILED.");
    IExec->CloseDevice((struct IORequest *)io);
    IExec->FreeSysObject(ASOT_IOREQUEST, io);
    IExec->FreeSysObject(ASOT_PORT, port);
    return ok ? 0 : 10;
}
