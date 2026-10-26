// --- robust ---
// Build > Run's programs and Build > Stop Program (Shift-Ctrl-F5, or the toolbar's Stop button
// when nothing is being debugged).
//
// Run starts the program through SystemTags(SYS_Asynch), which makes a shell process that runs
// it. NP_NotifyOnDeathMessage (dos 51.55) gives each such process a DeathMessage that DOS
// replies to our port as the process ends, so the IDE knows when it is gone. Stop Program
// finds the process by that DeathMessage (ProcessScan, which keeps the process list from
// changing while we look) and signals it SIGBREAKF_CTRL_C, which reaches the program because
// the shell runs commands on its own process. A program that doesn't check for Ctrl-C keeps
// running: after a few seconds the IDE offers Ctrl-C again or Ctrl-D/E/F too, and explains that
// AmigaOS has no safe way to force a program to quit. The states are robust::RunModel.
#include "App.h"
#include "Robust.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dostags.h>
#include <dos/dosextens.h>
#include <devices/timer.h>

namespace af {

struct RunCtl {
    struct MsgPort *port = nullptr;             // death messages and the timer
    struct TimeRequest *timer = nullptr;
    bool timerOpen = false, timerBusy = false;
    std::vector<struct DeathMessage *> procs;   // one per running program
    robust::RunModel model;
    bool offerPending = false;                  // show the "it won't stop" requester when idle
    // --- lang --- Python runs: Stop Program creates the stop file (the runner turns it into a
    // KeyboardInterrupt); when the program ends its traceback file goes into the build log
    struct PyRun { struct DeathMessage *dm; std::string stopFile, tracebackFile; };
    std::vector<PyRun> py;
};

// Milliseconds, for the model (it only uses differences, which survive wrapping)
static uint32_t nowMs()
{
    struct DateStamp ds;
    IDOS->DateStamp(&ds);
    return (uint32_t)ds.ds_Days * 86400000u + (uint32_t)ds.ds_Minute * 60000u + (uint32_t)ds.ds_Tick * (1000u / TICKS_PER_SECOND);
}

static RunCtl *ensureRunCtl(App *app)
{
    if (app->runCtl) return app->runCtl;
    RunCtl *rc = new RunCtl;
    rc->port = (struct MsgPort *)IExec->AllocSysObjectTags(ASOT_PORT, TAG_END);
    if (!rc->port) {
        delete rc;
        return nullptr;
    }
    rc->timer = (struct TimeRequest *)IExec->AllocSysObjectTags(ASOT_IOREQUEST,
        ASOIOR_Size, sizeof(struct TimeRequest), ASOIOR_ReplyPort, rc->port, TAG_END);
    if (rc->timer && !IExec->OpenDevice("timer.device", UNIT_VBLANK, (struct IORequest *)rc->timer, 0))
        rc->timerOpen = true;
    app->runCtl = rc;
    return rc;
}

static void cancelTimer(RunCtl *rc)
{
    if (!rc->timerBusy) return;
    if (!IExec->CheckIO((struct IORequest *)rc->timer)) IExec->AbortIO((struct IORequest *)rc->timer);
    IExec->WaitIO((struct IORequest *)rc->timer);   // also takes the reply off the port
    rc->timerBusy = false;
}

static void armTimer(RunCtl *rc, uint32_t ms)
{
    if (!rc->timerOpen) return;
    cancelTimer(rc);
    rc->timer->Request.io_Command = TR_ADDREQUEST;
    rc->timer->Time.Seconds = ms / 1000;
    rc->timer->Time.Microseconds = (ms % 1000) * 1000;
    IExec->SendIO((struct IORequest *)rc->timer);
    rc->timerBusy = true;
}

struct SignalJob {
    RunCtl *rc;
    uint32 sigs;
    int found;
};

static int32 signalHook(struct Hook *hook, APTR, struct Process *proc)
{
    SignalJob *job = (SignalJob *)hook->h_Data;
    for (struct DeathMessage *dm : job->rc->procs)
        if (proc->pr_DeathMessage == dm) {
            IExec->Signal((struct Task *)proc, job->sigs);
            job->found++;
        }
    return 0;   // carry on: there may be several
}

// Signal every running program (under ProcessScan's lock, so none can vanish meanwhile)
static int signalPrograms(RunCtl *rc, uint32 sigs)
{
    SignalJob job = { rc, sigs, 0 };
    struct Hook hook = {};
    hook.h_Entry = (HOOKFUNC)signalHook;
    hook.h_Data = &job;
    IDOS->ProcessScan(&hook, nullptr, 0);
    return job.found;
}

static void act(App *app, RunCtl *rc, int bits)
{
    if (bits & robust::RA_SEND_CTRL_C) {
        uint32 sigs = SIGBREAKF_CTRL_C;
        if (rc->model.alsoDEF()) sigs |= SIGBREAKF_CTRL_D | SIGBREAKF_CTRL_E | SIGBREAKF_CTRL_F;
        signalPrograms(rc, sigs);
        for (auto &p : rc->py) writeFile(p.stopFile, "stop\n");   // --- lang ---
    }
    if (bits & robust::RA_ARM_TIMER) armTimer(rc, 3100);
    if (bits & robust::RA_OFFER_MORE) rc->offerPending = true;   // runIdle shows it (not under a dialog)
    if (rc->model.running()) app->setStatus(rc->model.status());
}

int32_t App::runSystem(const std::string &cmd, int32_t input, const std::string &name)
{
    RunCtl *rc = ensureRunCtl(this);
    struct DeathMessage *dm = rc ? (struct DeathMessage *)IExec->AllocVecTags(sizeof(struct DeathMessage),
        AVT_Type, MEMF_SHARED, AVT_ClearWithValue, 0, TAG_END) : nullptr;
    if (dm) {
        dm->dm_Msg.mn_ReplyPort = rc->port;
        dm->dm_Msg.mn_Length = sizeof(struct DeathMessage);
    }
    int32 r = IDOS->SystemTags(cmd.c_str(), SYS_Input, input, SYS_Output, 0, SYS_Asynch, TRUE,
                               NP_Name, (uint32)name.c_str(),
                               runStack ? SYS_StackSize : TAG_IGNORE, runStack,   // --- lang ---
                               dm ? NP_NotifyOnDeathMessage : TAG_IGNORE, (uint32)dm, TAG_END);
    if (r == -1 || !dm) {
        if (dm) IExec->FreeVec(dm);
        if (r != -1) setStatus("Started " + name);   // untracked (out of memory)
        return r;
    }
    rc->procs.push_back(dm);
    rc->model.started(name);
    setStatus(rc->model.status());
    updateToolbar();
    return r;
}

uint32_t App::runSignals()
{
    return runCtl ? 1UL << runCtl->port->mp_SigBit : 0;
}

void App::runHandle()
{
    RunCtl *rc = runCtl;
    if (!rc) return;
    struct Message *m;
    bool changed = false;
    while ((m = IExec->GetMsg(rc->port))) {
        if (rc->timer && m == &rc->timer->Request.io_Message) {
            rc->timerBusy = false;
            act(this, rc, rc->model.tick(nowMs()));
            continue;
        }
        struct DeathMessage *dm = (struct DeathMessage *)m;
        for (size_t i = 0; i < rc->procs.size(); i++)
            if (rc->procs[i] == dm) {
                rc->procs.erase(rc->procs.begin() + i);
                break;
            }
        // --- lang ---
        std::string pyTraceback;
        for (size_t i = 0; i < rc->py.size(); i++)
            if (rc->py[i].dm == dm) {
                IDOS->Delete(rc->py[i].stopFile.c_str());
                pyTraceback = rc->py[i].tracebackFile;
                rc->py.erase(rc->py.begin() + i);
                break;
            }
        // --- end lang ---
        int left = rc->model.exited(dm->dm_ReturnCode);
        IExec->FreeVec(dm);
        changed = true;
        if (left == 0) {
            cancelTimer(rc);
            rc->offerPending = false;
            setStatus(rc->model.endedStatus());
        } else {
            setStatus(rc->model.status());
        }
        if (!pyTraceback.empty()) pythonRunEnded(pyTraceback);   // --- lang ---
    }
    if (changed) updateToolbar();
}

// --- lang ---
void App::runTrackPython(const std::string &stopFile, const std::string &tracebackFile)
{
    if (runCtl && !runCtl->procs.empty()) runCtl->py.push_back({runCtl->procs.back(), stopFile, tracebackFile});
}
// --- end lang ---

void App::runIdle()
{
    RunCtl *rc = runCtl;
    if (!rc || !rc->offerPending || !modalStack.empty()) return;
    rc->offerPending = false;
    if (!rc->model.running()) return;
    int choice = ask("Stop Program", rc->model.stubbornText(nowMs()), robust::RunModel::stubbornButtons());
    runHandle();   // it may have ended while the requester was up
    act(this, rc, rc->model.choose((robust::StubbornChoice)choice, nowMs()));
}

void App::stopProgram()
{
    if (!programRunning()) {
        setStatus("No program started with Run is running");
        return;
    }
    act(this, runCtl, runCtl->model.stop(nowMs()));
    runIdle();
}

bool App::programRunning()
{
    return runCtl && runCtl->model.running();
}

bool App::runQuitCheck()
{
    if (!programRunning()) return true;
    int c = ask("Quit", runCtl->model.name() + " (started with Run) is still running.\n\n"
                "AmiForge can quit and leave it running, or send it Ctrl-C first.",
                "_Quit|_Stop It First|_Cancel");
    if (c == 2) stopProgram();
    return c == 1;
}

void App::runCleanup()
{
    RunCtl *rc = runCtl;
    if (!rc) return;
    cancelTimer(rc);
    if (rc->timerOpen) IExec->CloseDevice((struct IORequest *)rc->timer);
    if (rc->timer) IExec->FreeSysObject(ASOT_IOREQUEST, rc->timer);
    runHandle();
    if (rc->procs.empty()) {
        IExec->FreeSysObject(ASOT_PORT, rc->port);
    } else {
        // Programs are still running and DOS will reply their DeathMessages here when they
        // end, after we are gone: leave the port and the messages allocated and stop the port
        // from signalling our (dead) task. A few dozen bytes, on purpose.
        rc->port->mp_Flags = PA_IGNORE;
    }
    delete rc;
    runCtl = nullptr;
}

} // namespace af
// --- end robust ---
