/* version 2: a new helper and a string; same data */
#include <string.h>
int counter = 7;
static int calls;

static int bonus(void)
{
    return (int)strlen("thousand") * 125;
}

int compute(int x)
{
    calls++;
    counter++;
    return x * 3 + bonus();
}
