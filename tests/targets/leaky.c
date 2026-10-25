/* leaky — a debuggee for the leak tracker test (tests/os4/toolstest.cpp): it leaks one of
 * each kind of resource on purpose, at the @markers, and releases the rest. */
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <stdlib.h>

static void *keep[4];

static void leak_memory(void)
{
    keep[0] = malloc(100);                                      /* @leak_malloc */
    void *t = malloc(50);
    free(t);
    keep[1] = AllocVecTags(256, AVT_Type, MEMF_PRIVATE, TAG_END);   /* @leak_vec */
    APTR v = AllocVecTags(64, TAG_END);
    FreeVec(v);
    char *r = malloc(10);
    r = realloc(r, 4000);                                       /* @leak_realloc */
    keep[2] = r;
}

static void leak_dos(void)
{
    BPTR f = Open("S:Startup-Sequence", MODE_OLDFILE);          /* @leak_file */
    (void)f;
    BPTR l = Lock("RAM:", SHARED_LOCK);                         /* @leak_lock */
    (void)l;
    BPTR l2 = Lock("SYS:", SHARED_LOCK);
    if (l2) UnLock(l2);
}

static void leak_exec(void)
{
    struct Library *lib = OpenLibrary("asl.library", 50);      /* @leak_lib */
    (void)lib;
    struct MsgPort *p = AllocSysObjectTags(ASOT_PORT, TAG_END);   /* @leak_port */
    (void)p;
    struct Library *lib2 = OpenLibrary("asl.library", 50);
    if (lib2) CloseLibrary(lib2);
}

int main(void)
{
    leak_memory();
    leak_dos();
    leak_exec();
    printf("leaky done\n");
    return 0;
}
