/* test_${LOWER} - opens ${LOWER}.library (the one next to it) and calls its functions.
 * Built and run by the run-test script: Build > Run (Ctrl-F5) in AmiForge.
 */
#include <stdio.h>

#include <proto/exec.h>
#include <proto/${LOWER}.h>

struct Library *${IDENT}Base;
struct ${IDENT}IFace *I${IDENT};

int main(void)
{
    ${IDENT}Base = IExec->OpenLibrary("PROGDIR:${LOWER}.library", 1);
    if (!${IDENT}Base) {
        printf("Can't open ${LOWER}.library\n");
        return 20;
    }
    I${IDENT} = (struct ${IDENT}IFace *)IExec->GetInterface(${IDENT}Base, "main", 1, NULL);
    if (!I${IDENT}) {
        printf("Can't get the library's interface\n");
        IExec->CloseLibrary(${IDENT}Base);
        return 20;
    }
    printf("%s\n", I${IDENT}->Hello());
    printf("Add(2, 3) = %ld\n", (long)I${IDENT}->Add(2, 3));
    printf("Multiply(6, 7) = %ld\n", (long)I${IDENT}->Multiply(6, 7));
    int ok = I${IDENT}->Add(2, 3) == 5 && I${IDENT}->Multiply(6, 7) == 42;
    printf("%s\n", ok ? "All tests passed." : "A test FAILED.");
    IExec->DropInterface((struct Interface *)I${IDENT});
    IExec->CloseLibrary(${IDENT}Base);
    return ok ? 0 : 10;
}
