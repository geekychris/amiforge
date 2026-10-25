// workers - exec multitasking from C++. The program splits a job (counting the primes below
// LIMIT) between several worker processes and shows how they get on:
//   - dos.library's CreateNewProcTags() starts each worker, running a function of this program
//     (worker.cpp) as a new process;
//   - exec message ports: the job goes to the new process's own port; the worker sends
//     progress messages back to the main program's port and replies to the job when it's done;
//   - signals: Wait() on the port and on Ctrl-C; Ctrl-C is passed on to every worker with
//     Signal(), and each one stops at its next progress report;
//   - C++ wrappers (os::Port, os::Worker, os::Crew) own the resources; Crew's destructor
//     waits until every worker has finished, so none outlives the program.
//
//   workers [WORKERS n] [LIMIT n]       (default 4 workers, LIMIT 400000)
// Try: change the number of workers in Project > Settings > Program arguments, and list the
// processes while it runs with the Shell's Status command (they're "workers example #n").
#include <cstdio>

#include <proto/exec.h>
#include <proto/dos.h>

#include "workers.h"

static const char version[] __attribute__((used)) = "$VER: workers 1.0 (25.10.2026)";

static void showProgress(const os::Crew &crew)
{
    std::printf("\r");
    for (const os::Worker *w : crew.workers)
        std::printf("#%lu %3lu%%  ", (unsigned long)w->job.head.worker, (unsigned long)w->percent);
    std::fflush(stdout);
}

int main()
{
    enum { ARG_WORKERS, ARG_LIMIT, ARG_COUNT };
    int32 array[ARG_COUNT] = {};
    struct RDArgs *rda = IDOS->ReadArgs("WORKERS/K/N,LIMIT/K/N", array, nullptr);
    if (!rda) {
        IDOS->PrintFault(IDOS->IoErr(), "workers");
        return RETURN_FAIL;
    }
    uint32 count = array[ARG_WORKERS] ? (uint32)*(int32 *)array[ARG_WORKERS] : 4;
    uint32 limit = array[ARG_LIMIT] ? (uint32)*(int32 *)array[ARG_LIMIT] : 400000;
    IDOS->FreeArgs(rda);
    if (count < 1) count = 1;
    if (count > 16) count = 16;
    if (limit < count) limit = count;

    std::printf("Counting the primes below %lu with %lu worker process%s:\n",
                (unsigned long)limit, (unsigned long)count, count == 1 ? "" : "es");
    struct DateStamp start, end;
    IDOS->DateStamp(&start);
    bool stopped = false;
    {
        os::Crew crew(count, limit);
        if (!crew.ok() || !crew.startAll()) {
            std::printf("workers: can't start the processes\n");
            return RETURN_FAIL;   // ~Crew stops and waits for any that did start
        }
        showProgress(crew);
        while (!crew.allDone()) {
            uint32 got = IExec->Wait(crew.signal() | SIGBREAKF_CTRL_C);
            if (got & SIGBREAKF_CTRL_C) {
                stopped = true;
                crew.stopAll();
            }
            if (crew.collect()) showProgress(crew);
        }
        std::printf("\n\n%-8s %-20s %10s %8s\n", "Worker", "Range", "Primes", "Seconds");
        uint32 total = 0;
        for (const os::Worker *w : crew.workers) {
            char range[32];
            std::snprintf(range, sizeof range, "%lu-%lu", (unsigned long)w->job.from, (unsigned long)w->job.to - 1);
            std::printf("#%-7lu %-20s %10lu %5lu.%02lu%s\n", (unsigned long)w->job.head.worker, range,
                        (unsigned long)w->job.primes, (unsigned long)(w->job.ticks / TICKS_PER_SECOND),
                        (unsigned long)(w->job.ticks % TICKS_PER_SECOND * 2), w->job.aborted ? "  (stopped)" : "");
            total += w->job.primes;
        }
        IDOS->DateStamp(&end);
        uint32 ticks = (uint32)((end.ds_Days - start.ds_Days) * 24 * 60 * 60 * TICKS_PER_SECOND +
                                (end.ds_Minute - start.ds_Minute) * 60 * TICKS_PER_SECOND + end.ds_Tick - start.ds_Tick);
        std::printf("%-29s %10lu %5lu.%02lu\n", "Total", (unsigned long)total,
                    (unsigned long)(ticks / TICKS_PER_SECOND), (unsigned long)(ticks % TICKS_PER_SECOND * 2));
    }
    if (stopped) {
        std::printf("***Break\n");
        return RETURN_WARN;
    }
    return RETURN_OK;
}
