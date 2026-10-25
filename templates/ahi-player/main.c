/* ${NAME} - plays generated tones through AHI on AmigaOS 4.1.
 * ${DATE}, ${AUTHOR}
 *
 * Synthesises a short note for each frequency given (a C major arpeggio by default) and plays
 * them one after another with ahi.device's CMD_WRITE, double-buffered so the notes join up.
 * The sound goes to AHI's default unit: set its mode in Prefs/AHI.
 *
 *   ${LOWER} NOTES=262,330,392,523 MS=250 VOLUME=70
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <devices/ahi.h>

static const char version[] __attribute__((used)) = "$VER: ${NAME} 1.0 (${DATE})";

#define RATE 44100

/* sin(2*pi*i/1024) * 32767, built with a parabola approximation (no maths library needed) */
static int16 sine[1024];

static void makeSine(void)
{
    for (int i = 0; i < 1024; i++) {
        int x = i % 512;                          /* half a period: 0..511 */
        int32 y = (int32)x * (512 - x);            /* parabola, peak 65536 at x = 256 */
        int32 v = (int32)((y * 32767LL) / 65536);
        sine[i] = (int16)(i < 512 ? v : -v);
    }
}

/* One note: `ms` milliseconds of `hz`, with a short fade in and out against clicks. */
static int fillNote(int16 *buf, int hz, int ms, int volume)
{
    int n = RATE * ms / 1000, fade = RATE / 200;
    uint32 phase = 0, step = (uint32)(((uint64)hz << 32) / RATE);
    for (int i = 0; i < n; i++) {
        int32 s = sine[phase >> 22] * volume / 100;
        int env = i < fade ? i : n - i < fade ? n - i : fade;
        buf[i] = (int16)(s * env / fade);
        phase += step;
    }
    return n;
}

int main(void)
{
    int32 args[3] = { 0, 0, 0 };
    struct RDArgs *rda = IDOS->ReadArgs("NOTES/K,MS/N/K,VOLUME/N/K", args, NULL);
    if (!rda) {
        IDOS->PrintFault(IDOS->IoErr(), "${NAME}");
        return RETURN_FAIL;
    }
    const char *notes = args[0] ? (const char *)args[0] : "262,330,392,523";
    int ms = args[1] ? *(int32 *)args[1] : 250;
    int volume = args[2] ? *(int32 *)args[2] : 70;
    if (ms < 20) ms = 20;
    if (ms > 4000) ms = 4000;

    struct MsgPort *port = IExec->AllocSysObjectTags(ASOT_PORT, TAG_END);
    struct AHIRequest *req[2] = { NULL, NULL };
    req[0] = port ? IExec->AllocSysObjectTags(ASOT_IOREQUEST, ASOIOR_Size, sizeof(struct AHIRequest),
                                              ASOIOR_ReplyPort, port, TAG_END) : NULL;
    if (req[0]) req[0]->ahir_Version = 4;
    if (!req[0] || IExec->OpenDevice(AHINAME, AHI_DEFAULT_UNIT, (struct IORequest *)req[0], 0) != 0) {
        IDOS->Printf("${NAME}: can't open ahi.device - is AHI installed and set up in Prefs/AHI?\n");
        if (req[0]) IExec->FreeSysObject(ASOT_IOREQUEST, req[0]);
        if (port) IExec->FreeSysObject(ASOT_PORT, port);
        IDOS->FreeArgs(rda);
        return RETURN_FAIL;
    }
    req[1] = IExec->AllocSysObjectTags(ASOT_IOREQUEST, ASOIOR_Duplicate, req[0], TAG_END);
    int16 *buf[2];
    int maxSamples = RATE * ms / 1000 + 1;
    buf[0] = IExec->AllocVecTags(maxSamples * 2, AVT_Type, MEMF_SHARED, TAG_END);
    buf[1] = IExec->AllocVecTags(maxSamples * 2, AVT_Type, MEMF_SHARED, TAG_END);
    makeSine();

    struct AHIRequest *prev = NULL;
    int which = 0, played = 0;
    const char *p = notes;
    while (*p && req[1] && buf[0] && buf[1]) {
        int hz = atoi(p);
        while (*p && *p != ',') p++;
        if (*p == ',') p++;
        if (hz < 20 || hz > 20000) continue;
        struct AHIRequest *r = req[which];
        int n = fillNote(buf[which], hz, ms, volume);
        r->ahir_Std.io_Command = CMD_WRITE;
        r->ahir_Std.io_Data = buf[which];
        r->ahir_Std.io_Length = n * 2;
        r->ahir_Std.io_Offset = 0;
        r->ahir_Type = AHIST_M16S;
        r->ahir_Frequency = RATE;
        r->ahir_Volume = 0x10000;
        r->ahir_Position = 0x8000;
        r->ahir_Link = prev;            /* starts as soon as the previous note ends */
        IExec->SendIO((struct IORequest *)r);
        IDOS->Printf("%ld Hz\n", (long)hz);
        if (prev) IExec->WaitIO((struct IORequest *)prev);
        prev = r;
        which ^= 1;
        played++;
        if (IExec->SetSignal(0, 0) & SIGBREAKF_CTRL_C) break;
    }
    if (prev) IExec->WaitIO((struct IORequest *)prev);
    IDOS->Printf("Played %ld note%s.\n", (long)played, played == 1 ? "" : "s");

    IExec->FreeVec(buf[0]);
    IExec->FreeVec(buf[1]);
    if (req[1]) IExec->FreeSysObject(ASOT_IOREQUEST, req[1]);
    IExec->CloseDevice((struct IORequest *)req[0]);
    IExec->FreeSysObject(ASOT_IOREQUEST, req[0]);
    IExec->FreeSysObject(ASOT_PORT, port);
    IDOS->FreeArgs(rda);
    return played ? RETURN_OK : RETURN_WARN;
}
