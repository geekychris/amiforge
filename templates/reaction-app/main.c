/* ${NAME} - a ReAction program for AmigaOS 4.1.
 * ${DATE}, ${AUTHOR}
 *
 * The window is designed in ${LOWER}.afg: open it with Tools > GUI Designer, change it,
 * then click "Generate Code" to rewrite ${LOWER}_gui.c/.h. The code for each gadget lives in
 * ${LOWER}_gui.c between the USER-BEGIN and USER-END lines, and survives regeneration.
 */
#include <proto/exec.h>
#include <proto/dos.h>

#include "${LOWER}_gui.h"

static const char version[] __attribute__((used)) = "$VER: ${NAME} 1.0 (${DATE})";

int main(void)
{
    static struct ${LOWER}_gui gui;
    if (!${LOWER}_gui_create(&gui) || !${LOWER}_gui_open(&gui)) {
        IDOS->Printf("${NAME}: can't open the window\n");
        ${LOWER}_gui_dispose(&gui);
        return RETURN_FAIL;
    }
    uint32 sig = ${LOWER}_gui_signal(&gui);
    for (;;) {
        uint32 got = IExec->Wait(sig | SIGBREAKF_CTRL_C);
        if (got & SIGBREAKF_CTRL_C) break;
        if (!${LOWER}_gui_handle(&gui)) break;
    }
    ${LOWER}_gui_dispose(&gui);
    return RETURN_OK;
}
