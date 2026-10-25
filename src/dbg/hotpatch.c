/* hotpatch.c — Edit and Continue: links a recompiled object against the running program.
 * See hotpatch.h. */
#include "hotpatch.h"
#include "dbg_int.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- ELF reading */

#define SHT_SYMTAB 2
#define SHT_RELA   4
#define SHT_NOBITS 8
#define SHT_GROUP  17
#define SHF_WRITE  0x1
#define SHF_ALLOC  0x2
#define SHF_EXEC   0x4
#define SHN_UNDEF  0
#define SHN_ABS    0xfff1
#define SHN_COMMON 0xfff2
#define STT_NOTYPE  0
#define STT_OBJECT  1
#define STT_FUNC    2
#define STT_SECTION 3
#define STT_FILE    4
#define STB_LOCAL  0
#define STB_GLOBAL 1
#define STB_WEAK   2

typedef struct { uint32_t offset; uint32_t sym; uint32_t type; int32_t addend; } hp_rela;

typedef struct {
    const char *name;
    uint32_t type, flags, addr, size, align, link, info;
    const uint8_t *data;         /* NULL for NOBITS */
    int nrela;
    hp_rela *rela;               /* relocations that apply to this section */
} hp_sect;

typedef struct {
    const char *name;
    const char *file;            /* locals: the FILE symbol they follow (NULL for globals) */
    uint32_t value, size;
    uint8_t type, bind;
    uint16_t shndx;
} hp_sym;

struct hp_elf {
    uint8_t *buf;
    uint32_t size;
    int etype;
    int nsect;
    hp_sect *sect;
    int nsym;
    hp_sym *sym;                 /* in file order (relocations index it) */
    int *byname;                 /* indices of named symbols, sorted by name */
    int nbyname;
};

static int fail(char *err, size_t n, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static int fail(char *err, size_t n, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (err && n) vsnprintf(err, n, fmt, ap);
    va_end(ap);
    return 0;
}

static const hp_elf *g_sort_elf;
static int byname_cmp(const void *a, const void *b)
{
    const hp_sym *x = &g_sort_elf->sym[*(const int *)a], *y = &g_sort_elf->sym[*(const int *)b];
    return strcmp(x->name, y->name);
}

hp_elf *hp_elf_from_buffer(uint8_t *buf, uint32_t size, int with_rela, char *err, size_t errlen)
{
    hp_elf *e = calloc(1, sizeof *e);
    if (!e) { free(buf); fail(err, errlen, "out of memory"); return NULL; }
    e->buf = buf; e->size = size;
    if (size < 52 || memcmp(buf, "\177ELF", 4) || buf[4] != 1 || buf[5] != 2 || be16(buf + 18) != 20) {
        fail(err, errlen, "not an ELF32 big-endian PowerPC file");
        hp_elf_free(e); return NULL;
    }
    e->etype = be16(buf + 16);
    uint32_t shoff = be32(buf + 32);
    uint16_t shentsize = be16(buf + 46), shnum = be16(buf + 48), shstrndx = be16(buf + 50);
    if (!shoff || shentsize < 40 || !shnum || shoff + (uint32_t)shnum * shentsize > size) {
        fail(err, errlen, "bad section headers");
        hp_elf_free(e); return NULL;
    }
    e->nsect = shnum;
    e->sect = calloc(shnum, sizeof(hp_sect));
    if (!e->sect) { fail(err, errlen, "out of memory"); hp_elf_free(e); return NULL; }
    uint32_t *nameoff = calloc(shnum, sizeof(uint32_t)), *offs = calloc(shnum, sizeof(uint32_t)),
             *entsz = calloc(shnum, sizeof(uint32_t));
    if (!nameoff || !offs || !entsz) { free(nameoff); free(offs); free(entsz); fail(err, errlen, "out of memory"); hp_elf_free(e); return NULL; }
    for (int i = 0; i < shnum; i++) {
        const uint8_t *p = buf + shoff + (uint32_t)i * shentsize;
        hp_sect *s = &e->sect[i];
        nameoff[i] = be32(p);
        s->type = be32(p + 4); s->flags = be32(p + 8); s->addr = be32(p + 12);
        offs[i] = be32(p + 16); s->size = be32(p + 20); s->link = be32(p + 24);
        s->info = be32(p + 28); s->align = be32(p + 32); entsz[i] = be32(p + 36);
        if (s->type != SHT_NOBITS && s->size) {
            if (offs[i] + s->size > size || offs[i] + s->size < offs[i]) {
                free(nameoff); free(offs); free(entsz);
                fail(err, errlen, "section %d runs past the end of the file", i);
                hp_elf_free(e); return NULL;
            }
            s->data = buf + offs[i];
        }
    }
    const char *shstr = shstrndx < shnum ? (const char *)e->sect[shstrndx].data : NULL;
    uint32_t shstrsz = shstrndx < shnum ? e->sect[shstrndx].size : 0;
    for (int i = 0; i < shnum; i++)
        e->sect[i].name = shstr && nameoff[i] < shstrsz ? shstr + nameoff[i] : "";

    /* symbols, in file order */
    for (int i = 0; i < shnum; i++) {
        hp_sect *s = &e->sect[i];
        if (s->type != SHT_SYMTAB || !s->data || s->link >= (uint32_t)shnum) continue;
        const char *str = (const char *)e->sect[s->link].data;
        uint32_t strsz = e->sect[s->link].size, es = entsz[i] ? entsz[i] : 16, n = s->size / es;
        e->sym = calloc(n ? n : 1, sizeof(hp_sym));
        if (!e->sym) break;
        const char *file = NULL;
        for (uint32_t k = 0; k < n; k++) {
            const uint8_t *p = s->data + k * es;
            hp_sym *y = &e->sym[k];
            uint32_t nm = be32(p);
            y->name = str && nm < strsz ? str + nm : "";
            y->value = be32(p + 4); y->size = be32(p + 8);
            y->type = p[12] & 15; y->bind = p[12] >> 4;
            y->shndx = be16(p + 14);
            if (y->type == STT_FILE) { file = y->name; continue; }
            if (y->bind == STB_LOCAL) y->file = file;
        }
        e->nsym = (int)n;
        break;
    }
    e->byname = malloc(sizeof(int) * (e->nsym ? e->nsym : 1));
    if (e->byname) {
        for (int i = 0; i < e->nsym; i++)
            if (e->sym[i].name[0] && e->sym[i].type != STT_FILE && e->sym[i].type != STT_SECTION) e->byname[e->nbyname++] = i;
        g_sort_elf = e;
        qsort(e->byname, e->nbyname, sizeof(int), byname_cmp);
    }

    /* relocations */
    if (with_rela) {
        for (int i = 0; i < shnum; i++) {
            hp_sect *r = &e->sect[i];
            if (r->type != SHT_RELA || !r->data || r->info >= (uint32_t)shnum) continue;
            hp_sect *t = &e->sect[r->info];
            uint32_t es = entsz[i] ? entsz[i] : 12, n = r->size / es;
            t->rela = calloc(n ? n : 1, sizeof(hp_rela));
            if (!t->rela) continue;
            for (uint32_t k = 0; k < n; k++) {
                const uint8_t *p = r->data + k * es;
                uint32_t info = be32(p + 4);
                t->rela[k].offset = be32(p);
                t->rela[k].sym = info >> 8;
                t->rela[k].type = info & 0xff;
                t->rela[k].addend = (int32_t)be32(p + 8);
            }
            t->nrela = (int)n;
        }
    }
    free(nameoff); free(offs); free(entsz);
    return e;
}

hp_elf *hp_elf_load(const char *path, int with_rela, char *err, size_t errlen)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fail(err, errlen, "can't open %s", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = sz > 0 ? malloc((size_t)sz) : NULL;
    if (!buf || fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        fclose(f); free(buf);
        fail(err, errlen, "can't read %s", path);
        return NULL;
    }
    fclose(f);
    hp_elf *e = hp_elf_from_buffer(buf, (uint32_t)sz, with_rela, err, errlen);
    if (!e && err && errlen) {
        char tmp[200];
        snprintf(tmp, sizeof tmp, "%s: %s", path, err);
        snprintf(err, errlen, "%s", tmp);
    }
    return e;
}

void hp_elf_free(hp_elf *e)
{
    if (!e) return;
    for (int i = 0; i < e->nsect; i++) free(e->sect[i].rela);
    free(e->sect); free(e->sym); free(e->byname); free(e->buf);
    free(e);
}

/* first index in byname with that name, or -1 */
static int first_named(const hp_elf *e, const char *name)
{
    int lo = 0, hi = e->nbyname;
    while (lo < hi) {
        int m = (lo + hi) / 2;
        if (strcmp(e->sym[e->byname[m]].name, name) < 0) lo = m + 1; else hi = m;
    }
    return lo < e->nbyname && !strcmp(e->sym[e->byname[lo]].name, name) ? lo : -1;
}

/* ---------------------------------------------------------------- program lookups */

typedef struct {
    const hp_elf *exe;
    hp_l2r_fn l2r;
    void *ud;
} prog;

static const char *base_of(const char *p)
{
    const char *b = p;
    for (const char *q = p; *q; q++) if (*q == '/' || *q == ':') b = q + 1;
    return b;
}

static int same_file(const char *a, const char *b)
{
    return a && b && str_ieq(base_of(a), base_of(b));
}

/* Run-time address of a program symbol (relative to its section, so symbols just past a
 * section's end, like _SDA_BASE_, map too). */
static uint32_t prog_run(const prog *pg, const hp_sym *y)
{
    if (y->shndx == SHN_ABS) return y->value;
    if (y->shndx == SHN_UNDEF || y->shndx >= pg->exe->nsect) return 0;
    const hp_sect *s = &pg->exe->sect[y->shndx];
    return pg->l2r(pg->ud, s->addr) + (y->value - s->addr);
}

/* A defined program symbol: a global/weak one by name, or a local one from the given source
 * file (file != NULL). */
static const hp_sym *prog_find(const prog *pg, const char *name, const char *file, int allow_any_local)
{
    const hp_elf *e = pg->exe;
    int i = first_named(e, name);
    if (i < 0) return NULL;
    const hp_sym *local_any = NULL;
    for (; i < e->nbyname && !strcmp(e->sym[e->byname[i]].name, name); i++) {
        const hp_sym *y = &e->sym[e->byname[i]];
        if (y->shndx == SHN_UNDEF) continue;
        if (file) {
            if (y->bind == STB_LOCAL && same_file(y->file, file)) return y;
        } else {
            if (y->bind != STB_LOCAL) return y;
            if (!local_any) local_any = y;
        }
    }
    return allow_any_local ? local_any : NULL;
}

/* ---------------------------------------------------------------- the patch */

void hp_patch_free(hp_patch *p);

/* fail() + free the half-prepared patch (after formatting: the message may use its data) */
static int refuse(hp_patch *p, char *err, size_t n, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
static int refuse(hp_patch *p, char *err, size_t n, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (err && n) vsnprintf(err, n, fmt, ap);
    va_end(ap);
    hp_patch_free(p);
    return 0;
}

enum { PL_SKIP, PL_NEW, PL_OLD };   /* section placement */

typedef struct { uint32_t target; uint32_t addr; } stub;

struct hp_priv {
    const hp_elf *obj;
    prog pg;
    char source[128];
    int *place;                  /* per object section: PL_* */
    uint32_t *off;               /* PL_NEW: offset in the patch memory; PL_OLD: run address */
    uint32_t code_size;          /* executable sections, then stubs, then read-only data */
    uint32_t stub_off;
    int max_stubs;
    stub *stubs;
    uint32_t sda_base;           /* the program's _SDA_BASE_ (0 if none) */
};

static int is_skipped_section(const hp_sect *s)
{
    static const char *const skip[] = { ".eh_frame", ".eh_frame_hdr", ".gcc_except_table", ".ctors",
                                        ".dtors", ".init_array", ".fini_array", ".comment", ".note.GNU-stack", NULL };
    for (int i = 0; skip[i]; i++) {
        size_t l = strlen(skip[i]);
        if (!strncmp(s->name, skip[i], l) && (!s->name[l] || s->name[l] == '.')) return 1;
    }
    return 0;
}

/* vtables, typeinfo and VTTs stay the program's: comparing typeinfo addresses must keep
 * working, and the old vtables' entries lead to the new functions anyway. */
static int keep_program_copy(const char *name)
{
    return !strncmp(name, "_ZTV", 4) || !strncmp(name, "_ZTI", 4) || !strncmp(name, "_ZTS", 4) || !strncmp(name, "_ZTT", 4);
}

static uint32_t align_up(uint32_t v, uint32_t a) { if (a < 4) a = 4; return (v + a - 1) & ~(a - 1); }

static int is_rel24(uint32_t t) { return t == 10 || t == 18 || t == 23; }   /* REL24, PLTREL24, LOCAL24PC */

void hp_patch_free(hp_patch *p)
{
    if (!p) return;
    if (p->priv) {
        free(p->priv->place); free(p->priv->off); free(p->priv->stubs);
        free(p->priv);
    }
    for (int i = 0; i < p->nfuncs; i++) free((char *)p->funcs[i].name);
    free(p->funcs); free(p->image); free(p->stab); free(p->stabstr);
    memset(p, 0, sizeof *p);
}

int hp_prepare(hp_patch *p, const hp_elf *obj, const hp_elf *exe, const char *source,
               hp_l2r_fn l2r, void *ud, char *err, size_t errlen)
{
    memset(p, 0, sizeof *p);
    if (obj->etype != 1) return fail(err, errlen, "not an object file");
    if (exe->etype != 2) return fail(err, errlen, "the program file isn't an executable");
    hp_priv *v = calloc(1, sizeof *v);
    if (!v) return fail(err, errlen, "out of memory");
    p->priv = v;
    v->obj = obj;
    v->pg.exe = exe; v->pg.l2r = l2r; v->pg.ud = ud;
    snprintf(v->source, sizeof v->source, "%s", base_of(source));
    v->place = calloc(obj->nsect, sizeof(int));
    v->off = calloc(obj->nsect, sizeof(uint32_t));
    if (!v->place || !v->off) { return refuse(p, err, errlen, "out of memory"); }
    const hp_sym *sda = prog_find(&v->pg, "_SDA_BASE_", NULL, 1);
    v->sda_base = sda ? prog_run(&v->pg, sda) : 0;

    /* the program must have been built from this file */
    int known = 0;
    for (int i = 0; i < exe->nsym && !known; i++)
        if (exe->sym[i].type == STT_FILE && same_file(exe->sym[i].name, v->source)) known = 1;
    if (!known) { return refuse(p, err, errlen, "the program wasn't built from %s", v->source); }

    /* 1. writable data: over the program's copy */
    for (int i = 1; i < obj->nsect; i++) {
        const hp_sect *s = &obj->sect[i];
        if (!(s->flags & SHF_ALLOC) || !s->size || is_skipped_section(s)) continue;
        if (!(s->flags & SHF_WRITE)) continue;
        int64_t base = -1;
        const char *first = NULL;
        for (int k = 0; k < obj->nsym; k++) {
            const hp_sym *y = &obj->sym[k];
            if (y->shndx != i || y->type == STT_SECTION || !y->name[0]) continue;
            const hp_sym *old = prog_find(&v->pg, y->name, y->bind == STB_LOCAL ? v->source : NULL, 0);
            if (!old) {
                
                return refuse(p, err, errlen, "%s is a new %s variable: restart the program to use it",
                            y->name, y->bind == STB_LOCAL ? "static" : "global");
            }
            if (old->size && y->size && old->size != y->size) {
                
                return refuse(p, err, errlen, "the size of %s changed (%lu to %lu bytes): restart the program",
                            y->name, (unsigned long)old->size, (unsigned long)y->size);
            }
            int64_t b = (int64_t)prog_run(&v->pg, old) - y->value;
            if (base < 0) { base = b; first = y->name; }
            else if (base != b) {
                
                return refuse(p, err, errlen, "the layout of %s's variables changed (%s, %s): restart the program",
                            v->source, first, y->name);
            }
        }
        if (base < 0) { return refuse(p, err, errlen, "%s has data in %s that can't be matched with the program", v->source, s->name); }
        /* nothing of another file may lie inside: that would mean the data grew */
        for (int k = 0; k < exe->nsym; k++) {
            const hp_sym *y = &exe->sym[k];
            if (y->type != STT_OBJECT || !y->size || !y->name[0] || y->shndx == SHN_UNDEF || y->shndx >= exe->nsect) continue;
            if (!(exe->sect[y->shndx].flags & SHF_WRITE)) continue;
            uint32_t a = prog_run(&v->pg, y);
            if (a < (uint32_t)base || a >= (uint32_t)base + s->size) continue;
            if (y->bind == STB_LOCAL && same_file(y->file, v->source)) continue;
            int ours = 0;
            for (int m = 0; m < obj->nsym && !ours; m++)
                if (obj->sym[m].shndx == i && !strcmp(obj->sym[m].name, y->name)) ours = 1;
            if (!ours) {
                
                return refuse(p, err, errlen, "the data of %s grew (it would overlap %s): restart the program", v->source, y->name);
            }
        }
        v->place[i] = PL_OLD;
        v->off[i] = (uint32_t)base;
    }

    /* 2. code, then stubs, then read-only data: in new memory */
    uint32_t at = 0;
    int nrel24 = 0;
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 1; i < obj->nsect; i++) {
            const hp_sect *s = &obj->sect[i];
            if (!(s->flags & SHF_ALLOC) || (s->flags & SHF_WRITE) || !s->size || is_skipped_section(s)) continue;
            int exec = (s->flags & SHF_EXEC) != 0;
            if (exec != (pass == 0)) continue;
            at = align_up(at, s->align);
            v->place[i] = PL_NEW;
            v->off[i] = at;
            at += s->size;
            for (int r = 0; r < s->nrela; r++) if (is_rel24(s->rela[r].type)) nrel24++;
        }
        if (pass == 0) {
            v->code_size = at;
            v->stub_off = at = align_up(at, 4);
            v->max_stubs = nrel24;
            at += 16 * (uint32_t)nrel24;
            v->code_size = at;
        }
    }
    v->stubs = calloc(nrel24 ? nrel24 : 1, sizeof(stub));
    if (!v->stubs) { return refuse(p, err, errlen, "out of memory"); }
    p->mem_size = align_up(at ? at : 4, 16);

    /* 3. the functions it defines, with the program's versions */
    int nf = 0;
    for (int k = 0; k < obj->nsym; k++) {
        const hp_sym *y = &obj->sym[k];
        if (y->type == STT_FUNC && y->shndx && y->shndx < obj->nsect && v->place[y->shndx] == PL_NEW) nf++;
    }
    p->funcs = calloc(nf ? nf : 1, sizeof(hp_func));
    if (!p->funcs) { return refuse(p, err, errlen, "out of memory"); }
    for (int k = 0; k < obj->nsym; k++) {
        const hp_sym *y = &obj->sym[k];
        if (y->type != STT_FUNC || !y->shndx || y->shndx >= obj->nsect || v->place[y->shndx] != PL_NEW) continue;
        hp_func *f = &p->funcs[p->nfuncs++];
        size_t l = strlen(y->name);
        char *nm = malloc(l + 1);
        if (nm) memcpy(nm, y->name, l + 1);
        f->name = nm ? nm : "";
        f->new_size = y->size;
        const hp_sym *old = prog_find(&v->pg, y->name, y->bind == STB_LOCAL ? v->source : NULL, 0);
        if (old && old->type == STT_FUNC) { f->old_addr = prog_run(&v->pg, old); f->old_size = old->size; }
    }

    int has_eh = 0;
    for (int i = 1; i < obj->nsect; i++)
        if (!strncmp(obj->sect[i].name, ".gcc_except_table", 17) && obj->sect[i].size) has_eh = 1;
    if (has_eh)
        snprintf(p->warn, sizeof p->warn, "C++ exceptions thrown through the changed functions of %s won't be caught", v->source);
    return 1;
}

/* Address a symbol of the object resolves to (0 + error message if it can't). */
static int resolve(hp_patch *p, uint32_t symi, uint32_t *out, char *err, size_t errlen)
{
    hp_priv *v = p->priv;
    const hp_elf *obj = v->obj;
    if (symi >= (uint32_t)obj->nsym) return fail(err, errlen, "bad symbol index %lu", (unsigned long)symi);
    const hp_sym *y = &obj->sym[symi];
    if (symi == 0) { *out = 0; return 1; }
    if (y->shndx == SHN_ABS) { *out = y->value; return 1; }
    if (y->shndx != SHN_UNDEF && y->shndx != SHN_COMMON && y->shndx < obj->nsect) {
        int pl = v->place[y->shndx];
        if (y->type != STT_SECTION && y->name[0] && pl == PL_NEW && keep_program_copy(y->name)) {
            const hp_sym *old = prog_find(&v->pg, y->name, y->bind == STB_LOCAL ? v->source : NULL, 0);
            if (old) { *out = prog_run(&v->pg, old); return 1; }
        }
        if (pl == PL_NEW) { *out = p->base + v->off[y->shndx] + (y->type == STT_SECTION ? 0 : y->value); return 1; }
        if (pl == PL_OLD) { *out = v->off[y->shndx] + (y->type == STT_SECTION ? 0 : y->value); return 1; }
        /* a section we don't load (exception tables...): only a problem if it's used */
        return fail(err, errlen, "%s refers to %s, which a patch can't contain",
                    v->source, y->type == STT_SECTION ? obj->sect[y->shndx].name : y->name);
    }
    /* external, or common: the program's */
    const hp_sym *old = prog_find(&v->pg, y->name, NULL, !strcmp(y->name, "_SDA_BASE_") || !strcmp(y->name, "_SDA2_BASE_"));
    if (!old) return fail(err, errlen, "%s uses %s, which isn't in the running program: rebuild and restart", v->source, y->name);
    *out = prog_run(&v->pg, old);
    return 1;
}

static uint32_t get32(const uint8_t *p) { return be32(p); }
static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

static uint32_t stub_for(hp_patch *p, uint32_t target)
{
    hp_priv *v = p->priv;
    for (int i = 0; i < p->nstubs; i++) if (v->stubs[i].target == target) return v->stubs[i].addr;
    if (p->nstubs >= v->max_stubs) return 0;
    uint32_t off = v->stub_off + 16 * (uint32_t)p->nstubs;
    uint8_t *w = p->image + off;
    put_be32(w, 0x3D800000u | (target >> 16));          /* lis   r12,target@h */
    put_be32(w + 4, 0x618C0000u | (target & 0xffff));  /* ori   r12,r12,target@l */
    put_be32(w + 8, 0x7D8903A6u);                      /* mtctr r12 */
    put_be32(w + 12, 0x4E800420u);                     /* bctr */
    v->stubs[p->nstubs].target = target;
    v->stubs[p->nstubs].addr = p->base + off;
    p->nstubs++;
    return p->base + off;
}

/* Applies one relocation at buf (run address P). */
static int apply(hp_patch *p, uint8_t *at, uint32_t P, uint32_t type, uint32_t S, int32_t A, const char *what,
                 int allow_stub, char *err, size_t errlen)
{
    uint32_t V = S + (uint32_t)A;
    switch (type) {
    case 0: return 1;                                    /* NONE */
    case 1: put_be32(at, V); return 1;                   /* ADDR32 */
    case 3: put16(at, V); return 1;                      /* ADDR16 */
    case 4: put16(at, V & 0xffff); return 1;             /* ADDR16_LO */
    case 5: put16(at, V >> 16); return 1;                /* ADDR16_HI */
    case 6: put16(at, (V + 0x8000) >> 16); return 1;     /* ADDR16_HA */
    case 2: {                                            /* ADDR24 */
        if (V & 0xfc000003) return fail(err, errlen, "absolute branch to %s out of range", what);
        put_be32(at, (get32(at) & ~0x03fffffcu) | V);
        return 1;
    }
    case 10: case 18: case 23: {                         /* REL24, PLTREL24, LOCAL24PC */
        int32_t d = (int32_t)(V - P);
        int outside = V < p->base || V >= p->base + p->mem_size;
        if (d < -0x2000000 || d > 0x1fffffc || (d & 3) || (p->force_far && allow_stub && outside)) {
            uint32_t s = allow_stub ? stub_for(p, V) : 0;
            if (!s) return fail(err, errlen, "branch to %s out of range", what);
            d = (int32_t)(s - P);
            if (d < -0x2000000 || d > 0x1fffffc) return fail(err, errlen, "branch to %s's stub out of range", what);
        }
        put_be32(at, (get32(at) & ~0x03fffffcu) | ((uint32_t)d & 0x03fffffcu));
        return 1;
    }
    case 11: case 12: case 13: {                         /* REL14 (+ branch-prediction variants) */
        int32_t d = (int32_t)(V - P);
        if (d < -0x8000 || d > 0x7ffc || (d & 3)) return fail(err, errlen, "conditional branch to %s out of range", what);
        put_be32(at, (get32(at) & ~0x0000fffcu) | ((uint32_t)d & 0xfffcu));
        return 1;
    }
    case 26: put_be32(at, V - P); return 1;              /* REL32 */
    case 249: put16(at, V - P); return 1;                /* REL16 */
    case 250: put16(at, (V - P) & 0xffff); return 1;     /* REL16_LO */
    case 251: put16(at, (V - P) >> 16); return 1;        /* REL16_HI */
    case 252: put16(at, ((V - P) + 0x8000) >> 16); return 1;   /* REL16_HA */
    case 32:                                             /* SDAREL16 */
        if (!p->priv->sda_base) return fail(err, errlen, "small-data reference to %s, but the program has no _SDA_BASE_", what);
        put16(at, V - p->priv->sda_base);
        return 1;
    default:
        return fail(err, errlen, "relocation type %lu (for %s) isn't supported by Edit and Continue", (unsigned long)type, what);
    }
}

static const char *sym_label(const hp_elf *obj, uint32_t symi)
{
    if (symi >= (uint32_t)obj->nsym) return "?";
    const hp_sym *y = &obj->sym[symi];
    if (y->type == STT_SECTION && y->shndx < obj->nsect) return obj->sect[y->shndx].name;
    return y->name[0] ? y->name : "?";
}

/* Is offset `off` of section `sect` inside an object the patch doesn't use (a vtable or
 * typeinfo that stays the program's)? Its relocations are left alone. */
static int in_unused_copy(const hp_priv *v, int sect, uint32_t off)
{
    const hp_elf *obj = v->obj;
    for (int k = 0; k < obj->nsym; k++) {
        const hp_sym *y = &obj->sym[k];
        if (y->shndx != sect || y->type == STT_SECTION || !y->name[0] || !keep_program_copy(y->name)) continue;
        if (off < y->value || off >= y->value + (y->size ? y->size : 4)) continue;
        if (prog_find(&v->pg, y->name, y->bind == STB_LOCAL ? v->source : NULL, 0)) return 1;
    }
    return 0;
}

int hp_link(hp_patch *p, uint32_t base, char *err, size_t errlen)
{
    hp_priv *v = p->priv;
    if (!v) return fail(err, errlen, "not prepared");
    const hp_elf *obj = v->obj;
    p->base = base;
    free(p->image);
    p->image = calloc(1, p->mem_size);
    if (!p->image) return fail(err, errlen, "out of memory");
    p->nstubs = 0;
    p->code_lo = base;
    p->code_hi = base + v->code_size;

    for (int i = 1; i < obj->nsect; i++)
        if (v->place[i] == PL_NEW && obj->sect[i].data) memcpy(p->image + v->off[i], obj->sect[i].data, obj->sect[i].size);
    for (int i = 1; i < obj->nsect; i++) {
        const hp_sect *s = &obj->sect[i];
        if (v->place[i] != PL_NEW) continue;
        int exec = (s->flags & SHF_EXEC) != 0;
        for (int r = 0; r < s->nrela; r++) {
            const hp_rela *rl = &s->rela[r];
            if (rl->offset + 4 > s->size && rl->type != 3 && rl->type != 4 && rl->type != 5 && rl->type != 6)
                return fail(err, errlen, "relocation outside %s", s->name);
            if (!exec && in_unused_copy(v, i, rl->offset)) continue;
            uint32_t S;
            if (!resolve(p, rl->sym, &S, err, errlen)) return 0;
            uint32_t P = base + v->off[i] + rl->offset;
            if (!apply(p, p->image + v->off[i] + rl->offset, P, rl->type, S, rl->addend,
                       sym_label(obj, rl->sym), exec, err, errlen)) return 0;
        }
    }
    for (int k = 0; k < p->nfuncs; k++) {
        hp_func *f = &p->funcs[k];
        for (int m = 0; m < obj->nsym; m++) {
            const hp_sym *y = &obj->sym[m];
            if (y->type == STT_FUNC && !strcmp(y->name, f->name) && y->shndx < obj->nsect && v->place[y->shndx] == PL_NEW) {
                f->new_addr = base + v->off[y->shndx] + y->value;
                break;
            }
        }
    }

    /* debug info: .stab with its relocations applied (addresses become run-time ones) */
    free(p->stab); free(p->stabstr);
    p->stab = NULL; p->stabstr = NULL; p->stab_size = p->stabstr_size = 0;
    for (int i = 1; i < obj->nsect; i++) {
        const hp_sect *s = &obj->sect[i];
        if (strcmp(s->name, ".stab") || !s->data) continue;
        p->stab = malloc(s->size ? s->size : 1);
        if (!p->stab) return fail(err, errlen, "out of memory");
        memcpy(p->stab, s->data, s->size);
        p->stab_size = s->size;
        for (int r = 0; r < s->nrela; r++) {
            const hp_rela *rl = &s->rela[r];
            if (rl->offset + 4 > s->size) continue;
            uint32_t S;
            char e2[160];
            if (!resolve(p, rl->sym, &S, e2, sizeof e2)) S = 0;    /* debug info only: carry on */
            if (rl->type == 1) put_be32(p->stab + rl->offset, S + (uint32_t)rl->addend);
        }
        if (s->link < (uint32_t)obj->nsect && obj->sect[s->link].data) {
            const hp_sect *ss = &obj->sect[s->link];
            p->stabstr = malloc(ss->size + 1);
            if (!p->stabstr) return fail(err, errlen, "out of memory");
            memcpy(p->stabstr, ss->data, ss->size);
            p->stabstr[ss->size] = 0;
            p->stabstr_size = ss->size;
        }
    }
    return 1;
}

int hp_branch(uint32_t from, uint32_t to, uint32_t out[4])
{
    return hp_branch_ex(from, to, 0, out);
}

int hp_branch_ex(uint32_t from, uint32_t to, int force_far, uint32_t out[4])
{
    int32_t d = (int32_t)(to - from);
    if (!force_far && d >= -0x2000000 && d <= 0x1fffffc && !(d & 3)) {
        out[0] = 0x48000000u | ((uint32_t)d & 0x03fffffcu);
        return 1;
    }
    out[0] = 0x3C000000u | (to >> 16);       /* lis   r0,to@h */
    out[1] = 0x60000000u | (to & 0xffff);    /* ori   r0,r0,to@l */
    out[2] = 0x7C0903A6u;                    /* mtctr r0 */
    out[3] = 0x4E800420u;                    /* bctr */
    return 4;
}

/* ---------------------------------------------------------------- debug info */

int stabs_parse(dbg_info *di);

dbg_info *hp_debug_info(const hp_patch *p, char *err, size_t errlen)
{
    if (!p->stab || !p->stabstr) { fail(err, errlen, "the object has no stabs debug info (compile with -gstabs)"); return NULL; }
    dbg_info *di = calloc(1, sizeof *di);
    if (!di) { fail(err, errlen, "out of memory"); return NULL; }
    vec_init(&di->funcs, sizeof(dfunc)); vec_init(&di->lines, sizeof(dline));
    vec_init(&di->cus, sizeof(dcu)); vec_init(&di->files, sizeof(dfile));
    vec_init(&di->globals, sizeof(dvar)); vec_init(&di->structs, sizeof(dtype *));
    vec_init(&di->typedefs, sizeof(dtype *)); vec_init(&di->patches, sizeof(dbg_info *));
    /* the parser reads elf.stab / elf.stabstr; copy them into the info's own arena */
    di->elf.stab = arena_alloc(&di->elf.mem, p->stab_size + 1);
    di->elf.stabstr = arena_alloc(&di->elf.mem, p->stabstr_size + 1);
    if (!di->elf.stab || !di->elf.stabstr) { info_free(di); fail(err, errlen, "out of memory"); return NULL; }
    memcpy(di->elf.stab, p->stab, p->stab_size);
    memcpy(di->elf.stabstr, p->stabstr, p->stabstr_size);
    di->elf.stab_size = p->stab_size;
    di->elf.stabstr_size = p->stabstr_size;
    stabs_parse(di);
    return di;
}

static int cu_is(const dbg_info *di, int cu, const char *source)
{
    if (cu < 0 || (size_t)cu >= di->cus.n) return 0;
    return same_file(VEC_AT(&di->cus, dcu, cu).name, source);
}

int hp_merge_info(dbg_info *di, dbg_info *pi, const char *source,
                  uint32_t base, uint32_t size, uint32_t code_lo, uint32_t code_hi)
{
    if (!di->patches.esz) vec_init(&di->patches, sizeof(dbg_info *));
    dbg_info **slot = vec_push(&di->patches);
    uint32_t (*pc)[2] = realloc(di->patch_code, sizeof(*pc) * (size_t)(di->npatch_code + 1));
    dreloc *rl = realloc(di->reloc, sizeof(dreloc) * (size_t)(di->nreloc + 1));
    if (!slot || !pc || !rl) { if (pc) di->patch_code = pc; if (rl) di->reloc = rl; return 0; }
    *slot = pi;
    di->patch_code = pc;
    di->patch_code[di->npatch_code][0] = code_lo;
    di->patch_code[di->npatch_code][1] = code_hi;
    di->npatch_code++;
    di->reloc = rl;
    di->reloc[di->nreloc].link = base;          /* identity: the patch was linked where it runs */
    di->reloc[di->nreloc].run = base;
    di->reloc[di->nreloc].size = size;
    di->nreloc++;

    /* the program's (and earlier patches') versions of the patch's functions step aside */
    const dfunc *PF = pi->funcs.data;
    for (size_t i = 0; i < di->funcs.n; i++) {
        dfunc *f = &VEC_AT(&di->funcs, dfunc, i);
        if (f->superseded || !cu_is(di, f->cu, source)) continue;
        for (size_t k = 0; k < pi->funcs.n; k++)
            if (!strcmp(PF[k].name, f->name)) { f->superseded = 1; break; }
    }

    int fileoff = (int)di->files.n, cuoff = (int)di->cus.n, funcoff = (int)di->funcs.n;
    for (size_t i = 0; i < pi->files.n; i++) {
        dfile *d = vec_push(&di->files);
        if (!d) return 0;
        *d = VEC_AT(&pi->files, dfile, i);
        d->cu += cuoff;
    }
    for (size_t i = 0; i < pi->cus.n; i++) {
        dcu *d = vec_push(&di->cus);
        if (!d) return 0;
        *d = VEC_AT(&pi->cus, dcu, i);
    }
    for (size_t i = 0; i < pi->funcs.n; i++) {
        dfunc *d = vec_push(&di->funcs);
        if (!d) return 0;
        *d = VEC_AT(&pi->funcs, dfunc, i);
        d->file += fileoff;
        d->cu += cuoff;
    }
    size_t nl0 = di->lines.n;
    for (size_t i = 0; i < pi->lines.n; i++) {
        dline *d = vec_push(&di->lines);
        if (!d) return 0;
        *d = VEC_AT(&pi->lines, dline, i);
        d->file = (uint16_t)(d->file + fileoff);
        if (d->func >= 0) d->func += funcoff;
    }

    /* functions sorted by address again, with the line table's indices remapped */
    size_t nf = di->funcs.n;
    dfunc *F = di->funcs.data;
    int *order = malloc(sizeof(int) * nf), *where = malloc(sizeof(int) * nf);
    dfunc *copy = malloc(sizeof(dfunc) * nf);
    if (!order || !where || !copy) { free(order); free(where); free(copy); return 0; }
    memcpy(copy, F, sizeof(dfunc) * nf);
    for (size_t i = 0; i < nf; i++) order[i] = (int)i;
    for (size_t i = 1; i < nf; i++) {
        int k = order[i];
        size_t j = i;
        while (j > 0 && copy[order[j - 1]].lo > copy[k].lo) { order[j] = order[j - 1]; j--; }
        order[j] = k;
    }
    for (size_t i = 0; i < nf; i++) { F[i] = copy[order[i]]; where[order[i]] = (int)i; }
    dline *L = di->lines.data;
    for (size_t i = 0; i < di->lines.n; i++) if (L[i].func >= 0) L[i].func = where[L[i].func];
    free(order); free(where); free(copy);

    /* lines: merge the (sorted) new ones into place, keeping order for equal addresses */
    size_t n = di->lines.n;
    for (size_t i = nl0 > 0 ? nl0 : 1; i < n; i++) {
        if (L[i - 1].addr <= L[i].addr) continue;
        dline t = L[i];
        size_t j = i;
        while (j > 0 && L[j - 1].addr > t.addr) { L[j] = L[j - 1]; j--; }
        L[j] = t;
    }
    return 1;
}
