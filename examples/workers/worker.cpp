// worker.cpp - the code that runs in each worker process, and the wrappers that start and
// stop them.
//
// The worker is a separate exec process running a function of this program. It must not use
// the C or C++ library (newlib keeps its state per process, set up by the program's startup
// code, which the worker never runs): only exec and dos calls, and its own stack.
#include "workers.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dostags.h>

namespace {

bool isPrime(uint32 n)
{
    if (n < 2) return false;
    if (n % 2 == 0) return n == 2;
    for (uint32 d = 3; d * d <= n; d += 2)
        if (n % d == 0) return false;
    return true;
}

// The entry point of a worker process.
int32 workerEntry(STRPTR, int32, struct ExecBase *)
{
    struct Process *me = (struct Process *)IExec->FindTask(nullptr);
    // the job arrives on the process's own port
    IExec->WaitPort(&me->pr_MsgPort);
    JobMsg *job = (JobMsg *)IExec->GetMsg(&me->pr_MsgPort);

    // our own port for the replies to our progress messages
    struct MsgPort *replyPort = (struct MsgPort *)IExec->AllocSysObjectTags(ASOT_PORT, TAG_DONE);
    ProgressMsg progress = {};
    progress.head.msg.mn_Length = sizeof progress;
    progress.head.msg.mn_ReplyPort = replyPort;
    progress.head.kind = MSG_PROGRESS;
    progress.head.worker = job->head.worker;

    struct DateStamp start, end;
    IDOS->DateStamp(&start);
    uint32 count = 0, span = job->to - job->from, nextReport = 10;
    for (uint32 n = job->from; n < job->to; n++) {
        if (isPrime(n)) count++;
        uint32 done = (uint32)((uint64)(n - job->from + 1) * 100 / span);
        if (done >= nextReport && replyPort) {
            // stop on Ctrl-C (from the main program, or from Status/Break)
            if (IExec->SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) {
                job->aborted = true;
                break;
            }
            progress.percent = done;
            IExec->PutMsg(job->progressPort, &progress.head.msg);
            IExec->WaitPort(replyPort);   // main replies at once; the message is ours again
            IExec->GetMsg(replyPort);
            nextReport = done + 10;
        }
    }
    IDOS->DateStamp(&end);
    job->primes = count;
    job->ticks = (uint32)((end.ds_Days - start.ds_Days) * 24 * 60 * 60 * TICKS_PER_SECOND +
                          (end.ds_Minute - start.ds_Minute) * 60 * TICKS_PER_SECOND + end.ds_Tick - start.ds_Tick);
    if (replyPort) IExec->FreeSysObject(ASOT_PORT, replyPort);

    // Reply under Forbid(): the main program may unload this code as soon as it has the
    // reply, and Forbid() keeps it from running until this process has ended.
    IExec->Forbid();
    IExec->ReplyMsg(&job->head.msg);
    return 0;
}

} // namespace

namespace os {

Port::Port()
{
    port = (struct MsgPort *)IExec->AllocSysObjectTags(ASOT_PORT, TAG_DONE);
}

Port::~Port()
{
    if (port) IExec->FreeSysObject(ASOT_PORT, port);
}

Worker::Worker(uint32 id, uint32 from, uint32 to, struct MsgPort *replies) : job()
{
    job.head.msg.mn_Length = sizeof job;
    job.head.msg.mn_ReplyPort = replies;
    job.head.kind = MSG_JOB;
    job.head.worker = id;
    job.from = from;
    job.to = to;
    job.progressPort = replies;
    name = "workers example #" + std::to_string((unsigned long)id);
}

bool Worker::start()
{
    proc = IDOS->CreateNewProcTags(
        NP_Entry, workerEntry,
        NP_Name, name.c_str(),
        NP_StackSize, 32768,
        NP_Priority, -1,          // below the Shell's: the machine stays responsive
        NP_Child, TRUE,
        TAG_DONE);
    if (!proc) return false;
    started = true;
    IExec->PutMsg(&proc->pr_MsgPort, &job.head.msg);
    return true;
}

void Worker::stop()
{
    // Under Forbid() the process can't reply (and end) while we look: if the job hasn't come
    // back yet, the process is still there to signal.
    IExec->Forbid();
    if (running() && job.head.msg.mn_Node.ln_Type != NT_REPLYMSG)
        IExec->Signal(&proc->pr_Task, SIGBREAKF_CTRL_C);
    IExec->Permit();
}

Crew::Crew(uint32 count, uint32 limit)
{
    if (!port.ok()) return;
    uint32 slice = limit / count;
    for (uint32 i = 0; i < count; i++) {
        uint32 from = i * slice, to = i + 1 == count ? limit : from + slice;
        workers.push_back(new Worker(i + 1, from, to, port.get()));
    }
}

Crew::~Crew()
{
    stopAll();
    while (!allDone()) {
        IExec->WaitPort(port.get());
        collect();
    }
    for (Worker *w : workers) delete w;
}

bool Crew::startAll()
{
    for (Worker *w : workers)
        if (!w->start()) return false;
    return true;
}

bool Crew::allDone() const
{
    for (const Worker *w : workers)
        if (w->running()) return false;
    return true;
}

bool Crew::collect()
{
    bool changed = false;
    while (struct Message *m = IExec->GetMsg(port.get())) {
        WorkMsg *wm = (WorkMsg *)m;
        if (wm->worker < 1 || wm->worker > workers.size()) continue;
        Worker *w = workers[wm->worker - 1];
        if (wm->kind == MSG_PROGRESS) {
            w->percent = ((ProgressMsg *)wm)->percent;
            IExec->ReplyMsg(m);
        } else {
            w->finished = true;   // our JobMsg, back from the worker
            if (!w->job.aborted) w->percent = 100;
        }
        changed = true;
    }
    return changed;
}

void Crew::stopAll()
{
    for (Worker *w : workers) w->stop();
}

} // namespace os
