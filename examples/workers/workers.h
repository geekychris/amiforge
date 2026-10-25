// workers.h - the messages the worker processes and the main program exchange, and the C++
// wrappers around the exec/dos resources.
#pragma once

#include <string>
#include <vector>

#include <exec/types.h>
#include <exec/ports.h>
#include <dos/dos.h>
#include <dos/dosextens.h>

// Everything sent between the processes starts with an exec Message.
enum { MSG_JOB = 1, MSG_PROGRESS };

struct WorkMsg {
    struct Message msg;
    uint32 kind;                  // MSG_JOB or MSG_PROGRESS
    uint32 worker;                // 1..n
};

// main -> worker (sent to the new process's own message port); the worker replies to it when
// it has finished, with the answer filled in.
struct JobMsg {
    WorkMsg head;
    uint32 from, to;              // count the primes in [from, to)
    struct MsgPort *progressPort; // where to send ProgressMsgs
    uint32 primes;                // the answer
    uint32 ticks;                 // how long it took (1/50 s)
    bool aborted;                 // stopped by Ctrl-C
};

// worker -> main, a few times during the job; main replies at once.
struct ProgressMsg {
    WorkMsg head;
    uint32 percent;
};

namespace os {

// A message port: AllocSysObject(ASOT_PORT) and FreeSysObject().
class Port {
public:
    Port();
    ~Port();
    Port(const Port &) = delete;
    Port &operator=(const Port &) = delete;
    bool ok() const { return port != nullptr; }
    struct MsgPort *get() const { return port; }
    uint32 signal() const { return 1UL << port->mp_SigBit; }
private:
    struct MsgPort *port = nullptr;
};

// One worker process. start() creates it with CreateNewProcTags() and hands it its job.
class Worker {
public:
    Worker(uint32 id, uint32 from, uint32 to, struct MsgPort *replies);
    Worker(const Worker &) = delete;
    Worker &operator=(const Worker &) = delete;
    bool start();
    bool running() const { return started && !finished; }
    void stop();                  // Ctrl-C to the process, if it's still running
    JobMsg job;
    bool started = false, finished = false;
    uint32 percent = 0;
private:
    std::string name;
    struct Process *proc = nullptr;
};

// All the workers and the port their messages come to. The destructor stops the ones still
// running and waits for each to reply: no process is left running code that's unloaded.
class Crew {
public:
    Crew(uint32 count, uint32 limit);
    ~Crew();
    Crew(const Crew &) = delete;
    Crew &operator=(const Crew &) = delete;
    bool ok() const { return port.ok(); }
    uint32 signal() const { return port.signal(); }
    bool startAll();
    bool allDone() const;
    // Handle the messages that arrived; true if anything changed.
    bool collect();
    void stopAll();
    std::vector<Worker *> workers;
private:
    Port port;
};

} // namespace os
