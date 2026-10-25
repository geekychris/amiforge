/* Opens the sample GUI, for check.sh (compiles and links it). */
#include <proto/exec.h>
#include <dos/dos.h>
#include "sample_gui.h"

int main(void)
{
    static struct sample_gui gui;
    if (sample_gui_create(&gui) && sample_gui_open(&gui)) {
        uint32 sig = sample_gui_signal(&gui);
        while (!(IExec->Wait(sig | SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C))
            if (!sample_gui_handle(&gui)) break;
    }
    sample_gui_dispose(&gui);
    return 0;
}
