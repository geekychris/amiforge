/* ${NAME} - a Shell program for AmigaOS 4.1.
 * ${DATE}, ${AUTHOR}
 *
 * Try: ${LOWER} WORLD   or   ${LOWER} Amiga TIMES=3 SHOUT
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <proto/exec.h>
#include <proto/dos.h>

static const char version[] __attribute__((used)) = "$VER: ${NAME} 1.0 (${DATE})";

#define TEMPLATE "NAME/A,TIMES/N,SHOUT/S"
enum { ARG_NAME, ARG_TIMES, ARG_SHOUT, ARG_COUNT };

int main(void)
{
    int32 args[ARG_COUNT] = { 0 };
    struct RDArgs *rda = IDOS->ReadArgs(TEMPLATE, args, NULL);
    if (!rda) {
        IDOS->PrintFault(IDOS->IoErr(), "${NAME}");
        return RETURN_FAIL;
    }

    char name[64];
    strncpy(name, (const char *)args[ARG_NAME], sizeof name - 1);
    name[sizeof name - 1] = 0;
    if (args[ARG_SHOUT])
        for (char *p = name; *p; p++) *p = toupper((unsigned char)*p);

    long times = args[ARG_TIMES] ? *(int32 *)args[ARG_TIMES] : 1;
    for (long i = 1; i <= times; i++)
        printf("%ld: Hello, %s!\n", i, name);

    IDOS->FreeArgs(rda);
    return RETURN_OK;
}
