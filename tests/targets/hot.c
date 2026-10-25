/* hot — a debuggee for the profiler test (tests/os4/toolstest.cpp): most of its time goes
 * to hot_loop(), a little to cold_loop(), and about 0.4 s to waiting in snooze(). */
#include <proto/exec.h>
#include <devices/timer.h>
#include <stdio.h>

volatile unsigned long sink;

static void hot_loop(unsigned long n)
{
    for (unsigned long i = 0; i < n; i++)
        sink += i * 7 + (i >> 3);                               /* @hot */
}

static void cold_loop(unsigned long n)
{
    for (unsigned long i = 0; i < n; i++) sink ^= i;            /* @cold */
}

static void snooze(unsigned long us)
{
    struct MsgPort *port = AllocSysObjectTags(ASOT_PORT, TAG_END);
    struct TimeRequest *tr = AllocSysObjectTags(ASOT_IOREQUEST, ASOIOR_Size, sizeof(struct TimeRequest),
                                                ASOIOR_ReplyPort, port, TAG_END);
    if (tr && !OpenDevice("timer.device", UNIT_MICROHZ, (struct IORequest *)tr, 0)) {
        tr->Request.io_Command = TR_ADDREQUEST;
        tr->Time.Seconds = us / 1000000;
        tr->Time.Microseconds = us % 1000000;
        DoIO((struct IORequest *)tr);                           /* @wait */
        CloseDevice((struct IORequest *)tr);
    }
    FreeSysObject(ASOT_IOREQUEST, tr);
    FreeSysObject(ASOT_PORT, port);
}

int main(void)
{
    for (int round = 0; round < 6; round++) {
        hot_loop(1500000);
        cold_loop(150000);
    }
    snooze(400000);
    printf("hot done %lu\n", sink);
    return 0;
}
