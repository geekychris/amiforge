/* wordcount - counts the lines, words and characters of a file (AmiForge manual, exercise 1).
 *
 * Try: wordcount S:Startup-Sequence   or   wordcount S:Startup-Sequence LONGEST
 */
#include <stdio.h>
#include <ctype.h>

#include <proto/exec.h>
#include <proto/dos.h>

static const char version[] __attribute__((used)) = "$VER: wordcount 1.0 (9.11.2026)";

#define TEMPLATE "FILE/A,LONGEST/S"
enum { ARG_FILE, ARG_LONGEST, ARG_COUNT };

int main(void)
{
    int32 args[ARG_COUNT] = { 0 };
    struct RDArgs *rda = IDOS->ReadArgs(TEMPLATE, args, NULL);
    if (!rda) {
        IDOS->PrintFault(IDOS->IoErr(), "wordcount");
        return RETURN_FAIL;
    }

    const char *name = (const char *)args[ARG_FILE];
    BPTR fh = IDOS->Open(name, MODE_OLDFILE);
    if (!fh) {
        IDOS->PrintFault(IDOS->IoErr(), name);
        IDOS->FreeArgs(rda);
        return RETURN_FAIL;
    }

    long lines = 0, words = 0, chars = 0, longest = 0, length = 0;
    int inword = 0, c;
    while ((c = IDOS->FGetC(fh)) >= 0) {
        chars++;
        if (c == '\n') {
            lines++;
            if (length > longest) longest = length;
            length = 0;
        } else {
            length++;
        }
        if (isspace(c)) inword = 0;
        else if (!inword) {
            inword = 1;
            words++;
        }
    }
    if (length > longest) longest = length;
    IDOS->Close(fh);

    printf("%ld lines, %ld words, %ld characters in %s\n", lines, words, chars, name);
    if (args[ARG_LONGEST])
        printf("the longest line has %ld characters\n", longest);

    IDOS->FreeArgs(rda);
    return RETURN_OK;
}
