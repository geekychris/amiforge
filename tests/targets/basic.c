/* Debuggee: loops, nested calls, recursion, structs/pointers/arrays/strings, globals,
 * statics, floats, a NULL write behind the "crash" argument and an endless loop behind "spin".
 * dbgtest finds lines by the "@name" markers in comments. */
#include <stdio.h>
#include <string.h>

enum colour { RED, GREEN = 5, BLUE };

struct point { int x, y; };

struct node {
    int value;
    char name[8];
    struct point pos;
    enum colour col;
    struct node *next;
};

typedef struct node node_t;

int g_counter = 3;
static int s_hidden = 11;
const char *g_greeting = "hello";
double g_ratio = 2.5;
int g_table[4] = { 10, 20, 30, 40 };
struct flags { unsigned a : 3, b : 5; int c : 4; } g_flags = { 5, 17, -3 };
union num { int i; float f; } g_num = { 0x3f800000 };

static int add(int a, int b)
{
    int sum = a + b;            /* @add_body */
    return sum;
}

static int twice(int v)
{
    int r = add(v, v);          /* @twice_call */
    return r;                   /* @twice_ret */
}

int factorial(int n)
{
    if (n <= 1)                 /* @fact_test */
        return 1;
    return n * factorial(n - 1);
}

static float scale(float f, double d)
{
    float out = f * (float)d;   /* @scale_body */
    return out;
}

static void crash(int *where)
{
    *where = 42;                /* @crash_write */
}

static void spin(void)
{
    volatile int n = 0;
    for (;;)
        n++;                    /* @spin_loop */
}

int main(int argc, char **argv)
{
    static int calls = 0;
    node_t a, b;
    node_t *head = &a;
    int i, total = 0;
    char buf[16];
    float f;

    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    a.value = 1; strcpy(a.name, "first"); a.pos.x = 3; a.pos.y = 4; a.col = GREEN; a.next = &b;
    b.value = 2; strcpy(b.name, "second"); b.col = BLUE;
    strcpy(buf, "buffer");

    for (i = 0; i < 5; i++) {   /* @loop_head */
        total += i;             /* @loop_body */
        calls++;
    }
    total += twice(total);      /* @main_twice */
    total += factorial(5);      /* @main_fact */
    f = scale(1.5f, g_ratio);
    total += (int)f + g_counter + s_hidden + g_table[2] + g_flags.b + (g_num.i != 0);
    if (argc > 1 && strcmp(argv[1], "crash") == 0)
        crash(NULL);            /* @main_crash */
    if (argc > 1 && strcmp(argv[1], "spin") == 0)
        spin();
    printf("%s total=%d node=%s head=%d\n", g_greeting, total, head->next->name, head->value);
    return total & 0x3F;        /* @main_ret */
}
