/* Crashes for the debugger's explanations: crashes [null|iface|freed|recurse]
   Debug it in AmiForge (set the program arguments in Project > Settings). */
#include <stdio.h>
#include <string.h>
#include <proto/exec.h>
#include <proto/graphics.h>

struct Ball { int x, y, dx, dy, size; };

static int depth(int n) { volatile char pad[256]; pad[0] = (char)n; return depth(n + 1) + pad[0]; }

int main(int argc, char **argv)
{
    const char *what = argc > 1 ? argv[1] : "null";
    if (strcmp(what, "null") == 0) {
        struct Ball *b = NULL;
        printf("size %d\n", b->size);           /* field at offset 16 of NULL */
    } else if (strcmp(what, "iface") == 0) {
        struct GraphicsIFace *saved = IGraphics;
        IGraphics = NULL;                        /* as if graphics.library never opened */
        IGraphics->WaitTOF();
        IGraphics = saved;
    } else if (strcmp(what, "freed") == 0) {
        struct Ball *b = (struct Ball *)0xDEADBEEF;   /* what MungWall leaves in freed memory */
        b->x = 1;
    } else if (strcmp(what, "recurse") == 0) {
        printf("%d\n", depth(0));
    }
    return 0;
}
