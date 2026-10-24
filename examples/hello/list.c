#include <stdlib.h>
#include "list.h"

struct item *list_push(struct item *head, int value, const char *name)
{
    struct item *it = malloc(sizeof *it);
    if (!it)
        return head;
    it->value = value;
    it->name = name;
    it->next = head;
    return it;
}

int list_sum(const struct item *head)
{
    int sum = 0;
    while (head) {
        sum += head->value;
        head = head->next;
    }
    return sum;
}

void list_free(struct item *head)
{
    while (head) {
        struct item *next = head->next;
        free(head);
        head = next;
    }
}
