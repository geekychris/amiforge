/* Deliberate mistakes: open errors.afp in AmiForge and build it - each one gets an
   "Explain:" line in the Build tab. */
#include <stdio.h>
#include <exec/types.h>
#include <proto/exec.h>

int main(void)
{
    int32 count = 3;
    printf("%d windows\n", count);              /* int32 is long: %ld */
    struct Window *w = OpenWindowTags(NULL, TAG_END);   /* no IIntuition-> */
    ULONG flags = MEMF_ANY;                      /* <exec/memory.h> */
    (void)w; (void)flags;
    return 0;
}
