/* Debuggee for dbgtest's "oscall" section: a program that spends nearly all its time inside
 * AmigaOS calls, where pausing it must not stop it (the debugger waits for the call to
 * return). Without arguments it prints to its console window and sleeps with Delay(), in a
 * loop; "wait" makes it Wait() for signals: CTRL-F wakes it (it goes round again), CTRL-E
 * ends both modes with exit code 7. dbgtest finds lines by the "@name" markers in comments. */
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <string.h>

volatile int g_lines, g_wakes;

static int work(int n)
{
    int sum = 0;
    for (int i = 0; i < n; i++) sum += i * 3;   /* @work */
    return sum;
}

static int print_loop(void)
{
    for (;;) {
        printf("line %d, sum %d\n", g_lines, work(50));   /* @print_line */
        g_lines++;
        Delay(2);                                          /* @delay */
        if (SetSignal(0, SIGBREAKF_CTRL_E) & SIGBREAKF_CTRL_E) return 7;
    }
}

static int wait_loop(void)
{
    printf("waiting: CTRL-F wakes, CTRL-E ends\n");
    fflush(stdout);
    for (;;) {
        uint32 sigs = Wait(SIGBREAKF_CTRL_E | SIGBREAKF_CTRL_F);   /* @wait */
        if (sigs & SIGBREAKF_CTRL_F) g_wakes++;                           /* @woken */
        if (sigs & SIGBREAKF_CTRL_E) return 7;
    }
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "wait")) return wait_loop();
    return print_loop();
}
