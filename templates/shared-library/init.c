/* ${NAME}: the library's start-up code - its ROM tag, the library manager interface
 * (open, close, expunge) and the table of interfaces. The functions are in ${LOWER}.c.
 * ${DATE}, ${AUTHOR}
 */
#include <exec/exec.h>
#include <proto/exec.h>
#include <dos/dos.h>
#include <proto/${LOWER}.h>

#define LIBNAME  "${LOWER}.library"
#define VERSION  1
#define REVISION 0
#define VSTRING  "${LOWER}.library 1.0 (${DATE})"

static const char __attribute__((used)) verstag[] = "\0$VER: " VSTRING;

struct ${IDENT}LibBase {
    struct Library libNode;
    BPTR segList;
    /* the library's own data goes here */
};

/* Running the library from the Shell does nothing. */
int32 _start(void);
int32 _start(void)
{
    return RETURN_FAIL;
}

static struct Library *libOpen(struct LibraryManagerInterface *Self, uint32 version)
{
    struct ${IDENT}LibBase *base = (struct ${IDENT}LibBase *)Self->Data.LibBase;
    if (version > VERSION) return NULL;
    base->libNode.lib_OpenCnt++;
    base->libNode.lib_Flags &= ~LIBF_DELEXP;
    return (struct Library *)base;
}

static APTR libExpunge(struct LibraryManagerInterface *Self)
{
    struct ExecIFace *IExec = (struct ExecIFace *)(*(struct ExecBase **)4)->MainInterface;
    struct ${IDENT}LibBase *base = (struct ${IDENT}LibBase *)Self->Data.LibBase;
    if (base->libNode.lib_OpenCnt > 0) {
        base->libNode.lib_Flags |= LIBF_DELEXP;   /* expunge when the last user closes it */
        return NULL;
    }
    APTR seg = (APTR)base->segList;
    IExec->Remove((struct Node *)base);
    IExec->DeleteLibrary((struct Library *)base);
    return seg;
}

static APTR libClose(struct LibraryManagerInterface *Self)
{
    struct ${IDENT}LibBase *base = (struct ${IDENT}LibBase *)Self->Data.LibBase;
    base->libNode.lib_OpenCnt--;
    if (base->libNode.lib_OpenCnt == 0 && (base->libNode.lib_Flags & LIBF_DELEXP))
        return libExpunge(Self);
    return NULL;
}

static struct Library *libInit(struct Library *lib, BPTR seglist, struct ExecIFace *IExec)
{
    struct ${IDENT}LibBase *base = (struct ${IDENT}LibBase *)lib;
    (void)IExec;
    base->libNode.lib_Node.ln_Type = NT_LIBRARY;
    base->libNode.lib_Node.ln_Pri = 0;
    base->libNode.lib_Node.ln_Name = (STRPTR)LIBNAME;
    base->libNode.lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
    base->libNode.lib_Version = VERSION;
    base->libNode.lib_Revision = REVISION;
    base->libNode.lib_IdString = (STRPTR)VSTRING;
    base->segList = seglist;
    return lib;
}

static uint32 libObtain(struct LibraryManagerInterface *Self) { return ++Self->Data.RefCount; }
static uint32 libRelease(struct LibraryManagerInterface *Self) { return --Self->Data.RefCount; }

static const APTR libManagerVectors[] = {
    (APTR)libObtain, (APTR)libRelease, NULL, NULL,
    (APTR)libOpen, (APTR)libClose, (APTR)libExpunge, NULL,
    (APTR)-1
};

static const struct TagItem libManagerTags[] = {
    { MIT_Name, (Tag)"__library" },
    { MIT_VectorTable, (Tag)libManagerVectors },
    { MIT_Version, 1 },
    { TAG_DONE, 0 }
};

/* the "main" interface's function table, made by idltool from ${LOWER}.xml */
#include "${LOWER}_vectors.c"

static const struct TagItem mainTags[] = {
    { MIT_Name, (Tag)"main" },
    { MIT_VectorTable, (Tag)main_v1_vectors },
    { MIT_Version, 1 },
    { TAG_DONE, 0 }
};

static const CONST_APTR libInterfaces[] = { libManagerTags, mainTags, NULL };

static const struct TagItem libCreateTags[] = {
    { CLT_DataSize, sizeof(struct ${IDENT}LibBase) },
    { CLT_InitFunc, (Tag)libInit },
    { CLT_Interfaces, (Tag)libInterfaces },
    { TAG_DONE, 0 }
};

static const struct Resident __attribute__((used)) libResident = {
    RTC_MATCHWORD,
    (struct Resident *)&libResident,
    (APTR)(&libResident + 1),
    RTF_NATIVE | RTF_AUTOINIT,
    VERSION,
    NT_LIBRARY,
    0,
    (STRPTR)LIBNAME,
    (STRPTR)VSTRING,
    (APTR)libCreateTags
};
