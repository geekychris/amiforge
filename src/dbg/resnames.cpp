// Names for the resource tracker's events (dbg.h). Portable: the host tests use them too.
#include "dbg.h"

extern "C" const char *dbg_res_function_name(int fn)
{
    static const char *const names[DBG_FN_COUNT] = {
        "AllocVec", "AllocVecTagList", "AllocVecTags", "FreeVec", "AllocMem", "FreeMem",
        "AllocSysObject", "AllocSysObjectTags", "FreeSysObject", "CreatePool", "DeletePool",
        "OpenLibrary", "CloseLibrary", "GetInterface", "DropInterface",
        "Open", "FOpen", "OpenFromLock", "Close", "Lock", "DupLock", "ParentDir", "CreateDir",
        "DupLockFromFH", "ParentOfFH", "UnLock", "malloc", "calloc", "realloc", "free"
    };
    return fn >= 0 && fn < DBG_FN_COUNT ? names[fn] : "?";
}

extern "C" const char *dbg_res_kind_name(int kind)
{
    static const char *const names[DBG_RES_KINDS] = { "?", "AllocVec", "AllocMem", "System object", "Memory pool",
                                                      "Library", "Interface", "File", "Lock", "malloc" };
    return kind > 0 && kind < DBG_RES_KINDS ? names[kind] : "?";
}
