/*
 * os4_tools.h — the debugtools runtime (resource tracking, profiler) as seen by the session
 * (os4_session.c). Internal to src/dbg.
 */
#ifndef AF_OS4_TOOLS_H
#define AF_OS4_TOOLS_H

#include "dbg_int.h"

struct Task;
struct DebugIFace;

/* session accessors (os4_session.c) */
struct Task *dbg__proc(dbg_session *s);
struct DebugIFace *dbg__idebug(dbg_session *s);
dbg_info *dbg__info(dbg_session *s);
void **dbg__dt(dbg_session *s);
volatile uint32_t *dbg__exc_count(dbg_session *s);

/* hooks the session calls (os4_tools.c) */
int dt_wants_main(dbg_session *s);
int dt_sampling(dbg_session *s);            /* the profiler's sampler is running (it keeps the task dispatched) */          /* plant the main() trap even without stop_at_main */
void dt_before_go(dbg_session *s);          /* IDE task: the launcher waits for the go signal */
void dt_program_starting(dbg_session *s);   /* debuggee task, just before RunCommand */
void dt_program_returned(dbg_session *s);   /* debuggee task, just after RunCommand */
void dt_at_main(dbg_session *s);            /* IDE task, the program is stopped at main() */
void dt_after_exit(dbg_session *s);         /* IDE task, the process has ended */
void dt_stop(dbg_session *s);               /* IDE task: undo everything (unload, kill, destroy) */
void dt_free(dbg_session *s);               /* dbg_destroy */

#endif
