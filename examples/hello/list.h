/* A tiny linked list, to have something to look at in the debugger. */
#ifndef LIST_H
#define LIST_H

struct item {
    int value;
    const char *name;
    struct item *next;
};

struct item *list_push(struct item *head, int value, const char *name);
int list_sum(const struct item *head);
void list_free(struct item *head);

#endif
