#include <string.h>
int counter = 7;
static int calls;
static char buf[64];
int factor = 3;
static int twice(int v)
{
    return v + v;
}
int compute(int x)
{
    static int local_static = 100;
    calls++;
    counter++;
    local_static++;
    strcpy(buf, "abcdef");
    return twice(x * factor) + (int)strlen(buf) + local_static - calls;
}
