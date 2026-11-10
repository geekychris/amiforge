/* converter - Celsius to Fahrenheit in a ReAction window (AmiForge manual, exercise 2).
 * The window is converter.afg, made in Tools > GUI Designer; converter_gui.c/.h are generated
 * from it, and the conversion is in converter_gui.c's USER sections. */
#include <proto/exec.h>
#include <dos/dos.h>

#include "converter_gui.h"

static const char version[] __attribute__((used)) = "$VER: converter 1.0 (9.11.2026)";

int main(void)
{
    static struct converter_gui gui;
    if (converter_gui_create(&gui) && converter_gui_open(&gui)) {
        uint32 sig = converter_gui_signal(&gui);
        while (!(IExec->Wait(sig | SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C))
            if (!converter_gui_handle(&gui)) break;
    }
    converter_gui_dispose(&gui);
    return 0;
}
