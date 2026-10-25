/* The code for the window's gadgets. The GUI Designer writes the rest of this file. */
/* USER-BEGIN includes */
#include <stdio.h>
#include <proto/dos.h>
#include <intuition/intuition.h>

static int greetings;
/* USER-END includes */
/* USER-BEGIN ${UPPER}_GREET */
        {
            STRPTR name = NULL;
            uint32 loud = 0, which = 0;
            static const char *const words[] = { "Hello", "Good morning", "Welcome" };
            char text[200];
            IIntuition->GetAttr(STRINGA_TextVal, gui->obj[${UPPER}_NAME], (uint32 *)&name);
            IIntuition->GetAttr(GA_Selected, gui->obj[${UPPER}_LOUD], &loud);
            IIntuition->GetAttr(CHOOSER_Selected, gui->obj[${UPPER}_GREETING], &which);
            snprintf(text, sizeof text, "%s, %s%s", words[which % 3], name && *name ? (char *)name : "stranger",
                     loud ? "!!!" : ".");
            greetings++;
            IIntuition->SetGadgetAttrs((struct Gadget *)gui->obj[${UPPER}_GAUGE], gui->win, NULL,
                                       FUELGAUGE_Level, (greetings * 10) % 110, TAG_DONE);
            struct EasyStruct es = { sizeof(struct EasyStruct), 0, "${NAME}", "%s", "_OK", NULL, NULL };
            IIntuition->EasyRequest(gui->win, &es, NULL, text);
        }
/* USER-END ${UPPER}_GREET */
/* USER-BEGIN ${UPPER}_QUIT */
        gui->quit = TRUE;
/* USER-END ${UPPER}_QUIT */
