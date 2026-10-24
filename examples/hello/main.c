/* hello — AmiForge's first example.
 *
 * Try: F9 on a line to set a breakpoint, F5 to debug, F10/F11 to step,
 * and watch `total` and `list` in the Locals and Watches panels. */
#include <stdio.h>
#include "list.h"

static int square(int n)
{
    return n * n;
}

int main(void)
{
    static const char *names[] = { "one", "two", "three", "four", "five" };
    struct item *list = NULL;
    int total = 0;
    int i;

    printf("Hello from AmiForge!\n");
    for (i = 1; i <= 5; i++) {
        list = list_push(list, square(i), names[i - 1]);
        total += i;
    }
    printf("1 + 2 + ... + 5 = %d\n", total);
    printf("sum of the squares = %d\n", list_sum(list));
    list_free(list);
    return 0;
}
