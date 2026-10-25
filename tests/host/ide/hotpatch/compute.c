#include <string.h>
int counter = 7;
static int calls;
static char buf[64];
int factor = 3;
int compute(int x)
{
    static int local_static = 100;
    calls++;
    counter++;
    local_static++;
    strcpy(buf, "abc");
    return x * factor + (int)strlen(buf) + local_static - calls;
}
