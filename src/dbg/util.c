/* util.c — arena allocator, growable arrays and small helpers for the debugger core. */
#include "dbg_int.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

struct arena_blk { arena_blk *next; size_t size; };   /* data follows (8-byte aligned) */

#define ARENA_CHUNK (64 * 1024)

void *arena_alloc(arena *a, size_t n)
{
    n = (n + 7) & ~(size_t)7;
    if (!a->head || a->used + n > a->cap) {
        size_t cap = n > ARENA_CHUNK ? n : ARENA_CHUNK;
        arena_blk *b = malloc(sizeof(arena_blk) + 8 + cap);
        if (!b) return NULL;
        b->next = a->head; b->size = cap;
        a->head = b; a->used = 0; a->cap = cap;
    }
    char *base = (char *)(((uintptr_t)(a->head + 1) + 7) & ~(uintptr_t)7);
    void *p = base + a->used;
    a->used += n;
    memset(p, 0, n);
    return p;
}

char *arena_strndup(arena *a, const char *s, size_t n)
{
    char *d = arena_alloc(a, n + 1);
    if (d) { memcpy(d, s, n); d[n] = 0; }
    return d;
}

char *arena_strdup(arena *a, const char *s) { return s ? arena_strndup(a, s, strlen(s)) : NULL; }

void arena_free(arena *a)
{
    arena_blk *b = a->head;
    while (b) { arena_blk *n = b->next; free(b); b = n; }
    a->head = NULL; a->used = a->cap = 0;
}

void vec_init(vec *v, size_t esz) { v->data = NULL; v->n = v->cap = 0; v->esz = esz; }

void *vec_push(vec *v)
{
    if (v->n == v->cap) {
        size_t cap = v->cap ? v->cap * 2 : 16;
        void *d = realloc(v->data, cap * v->esz);
        if (!d) return NULL;
        v->data = d; v->cap = cap;
    }
    void *e = (char *)v->data + v->n++ * v->esz;
    memset(e, 0, v->esz);
    return e;
}

void vec_free(vec *v) { free(v->data); v->data = NULL; v->n = v->cap = 0; }

const char *path_base(const char *p)
{
    const char *b = p;
    for (; *p; p++) if (*p == '/' || *p == ':') b = p + 1;
    return b;
}

int str_ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return *a == *b;
}

uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
void put_be32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

int mem_read32(const dbg_mem *m, uint32_t addr, uint32_t *v)
{
    uint8_t b[4];
    if ((addr & 3) || m->read(m->ud, addr, b, 4) != 4) return 0;
    *v = be32(b);
    return 1;
}
