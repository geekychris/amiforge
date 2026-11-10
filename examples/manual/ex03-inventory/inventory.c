/* inventory - the manual's exercise 3, fixed: an item that isn't in stock is skipped
 * (and it says so). inventory-crash.c is the version with the bug. */
#include <stdio.h>
#include <string.h>

struct item {
    const char *name;
    int count;
    struct item *next;
};

static struct item disks = { "disks", 12, NULL };
static struct item mice = { "mice", 3, &disks };
static struct item joysticks = { "joysticks", 5, &mice };

static struct item *find(struct item *list, const char *name)
{
    for (struct item *i = list; i; i = i->next)
        if (strcmp(i->name, name) == 0) return i;
    return NULL;
}

static int take(struct item *list, const char **names, int n)
{
    int taken = 0;
    for (int k = 0; k < n; k++) {
        struct item *it = find(list, names[k]);
        if (!it) {
            printf("no %s in stock\n", names[k]);
            continue;
        }
        it->count--;
        taken++;
    }
    return taken;
}

int main(void)
{
    const char *order[] = { "mice", "disks", "keyboards", "joysticks" };
    printf("items taken: %d\n", take(&joysticks, order, 4));
    printf("mice left: %d\n", mice.count);
    return 0;
}
