/* info.c — loads a program's debug info and answers symbol, line and type queries. */
#include "dbg_int.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ---------------- types ---------------- */

dtype *type_unalias(dtype *t)
{
    for (int n = 0; t && n < 64; n++) {
        if (t->kind == T_ALIAS && t->target) t = t->target;
        else if (t->kind == T_XREF && t->target) t = t->target;
        else break;
    }
    return t;
}

dtype *type_strip(dtype *t)
{
    for (int n = 0; t && n < 64; n++) {
        switch (t->kind) {
        case T_ALIAS: case T_TYPEDEF: case T_CONST: case T_VOLATILE:
            if (!t->target) return t;
            t = t->target;
            break;
        case T_XREF:
            if (!t->target) return t;
            t = t->target;
            break;
        default:
            return t;
        }
    }
    return t;
}

uint32_t type_size(dtype *t)
{
    t = type_strip(t);
    if (!t) return 0;
    switch (t->kind) {
    case T_ARRAY: {
        if (t->hi < t->lo) return 0;
        return (uint32_t)(t->hi - t->lo + 1) * type_size(t->target);
    }
    case T_PTR: case T_REF: case T_MEMBERPTR: return 4;
    case T_VOID: case T_FUNC: case T_UNKNOWN: case T_XREF: return 0;
    default: return t->size;
    }
}

int type_is_integral(dtype *t)
{
    t = type_strip(t);
    return t && (t->kind == T_INT || t->kind == T_BOOL || t->kind == T_ENUM);
}

int type_is_scalar(dtype *t)
{
    t = type_strip(t);
    return t && (type_is_integral(t) || t->kind == T_FLOAT || t->kind == T_PTR || t->kind == T_REF || t->kind == T_MEMBERPTR);
}

typedef struct { char *buf; size_t n, len; } sbuf;

static void sb_put(sbuf *b, const char *s)
{
    size_t l = strlen(s);
    if (b->len + l >= b->n) l = b->n - b->len - 1;
    memcpy(b->buf + b->len, s, l);
    b->len += l;
    b->buf[b->len] = 0;
}

static const char *base_name(dtype *t, char *tmp, size_t n)
{
    const char *kw = t->kind == T_UNION ? "union" : t->kind == T_ENUM ? "enum" : "struct";
    switch (t->kind) {
    case T_STRUCT: case T_UNION: case T_ENUM:
        if (!t->name) { snprintf(tmp, n, "%s {...}", kw); return tmp; }
        if (t->plain_name) return t->name;
        snprintf(tmp, n, "%s %s", kw, t->name);
        return tmp;
    case T_XREF:
        if (t->target) return base_name(t->target, tmp, n);
        snprintf(tmp, n, "%s %s", t->xref_tag == 'u' ? "union" : t->xref_tag == 'e' ? "enum" : "struct", t->name ? t->name : "?");
        return tmp;
    case T_VOID: return t->name ? t->name : "void";
    case T_BOOL: return t->name ? t->name : "bool";
    case T_FLOAT: return t->name ? t->name : t->size == 4 ? "float" : "double";
    case T_INT:
        if (t->name) return t->name;
        snprintf(tmp, n, "%sint%u_t", t->is_signed ? "" : "u", (unsigned)t->size * 8);
        return tmp;
    case T_TYPEDEF: return t->name ? t->name : "?";
    default: return "?";
    }
}

static void decl(dtype *t, const char *inner, sbuf *out, int depth)
{
    char tmp[256], in2[512];
    t = type_unalias(t);
    if (!t || depth > 32) { sb_put(out, "?"); return; }
    switch (t->kind) {
    case T_PTR: case T_REF: {
        dtype *tg = type_unalias(t->target);
        const char *op = t->kind == T_PTR ? "*" : "&";
        if (tg && (tg->kind == T_ARRAY || tg->kind == T_FUNC)) snprintf(in2, sizeof in2, "(%s%s)", op, inner);
        else snprintf(in2, sizeof in2, "%s%s", op, inner);
        decl(t->target, in2, out, depth + 1);
        return;
    }
    case T_CONST: case T_VOLATILE: {
        const char *q = t->kind == T_CONST ? "const" : "volatile";
        dtype *tg = type_unalias(t->target);
        if (tg && (tg->kind == T_PTR || tg->kind == T_REF)) {
            snprintf(in2, sizeof in2, "%s%s%s", q, *inner ? " " : "", inner);
            decl(t->target, in2, out, depth + 1);
        } else {
            sb_put(out, q); sb_put(out, " ");
            decl(t->target, inner, out, depth + 1);
        }
        return;
    }
    case T_ARRAY:
        if (t->hi >= t->lo) snprintf(in2, sizeof in2, "%s[%ld]", inner, (long)(t->hi - t->lo + 1));
        else snprintf(in2, sizeof in2, "%s[]", inner);
        decl(t->target, in2, out, depth + 1);
        return;
    case T_FUNC:
        snprintf(in2, sizeof in2, "%s()", inner);
        decl(t->target, in2, out, depth + 1);
        return;
    case T_MEMBERPTR:
        snprintf(in2, sizeof in2, "::*%s", inner);
        decl(t->target, in2, out, depth + 1);
        return;
    case T_UNKNOWN:
        sb_put(out, "<unknown type>");
        if (*inner) { sb_put(out, " "); sb_put(out, inner); }
        return;
    default:
        sb_put(out, base_name(t, tmp, sizeof tmp));
        if (*inner) {
            if (*inner != '[') sb_put(out, " ");
            else sb_put(out, " ");
            sb_put(out, inner);
        }
        return;
    }
}

void type_name(dtype *t, char *buf, size_t n)
{
    sbuf b = { buf, n, 0 };
    if (!n) return;
    buf[0] = 0;
    decl(t, "", &b, 0);
}

/* ---------------- loading ---------------- */

static int symname_cmp(const void *a, const void *b)
{
    return strcmp((*(const elf_symbol * const *)a)->name, (*(const elf_symbol * const *)b)->name);
}

static const elf_symbol *sym_by_name(const elf_symbol **sorted, int n, const char *name)
{
    int lo = 0, hi = n - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2, c = strcmp(sorted[m]->name, name);
        if (!c) return sorted[m];
        if (c < 0) lo = m + 1; else hi = m - 1;
    }
    return NULL;
}

/* C++ globals: stabs has "x" while .symtab has "_ZN2ns1xE". */
static const elf_symbol *sym_by_cxx_name(const elf_file *ef, const char *name)
{
    char pat[300], buf[1024];
    snprintf(pat, sizeof pat, "%u%s", (unsigned)strlen(name), name);
    for (int i = 0; i < ef->nsym; i++) {
        const elf_symbol *s = &ef->sym[i];
        if (s->type != ELF_STT_OBJECT || strncmp(s->name, "_Z", 2) || !strstr(s->name, pat)) continue;
        const char *d = dbg_demangle(s->name, buf, sizeof buf);
        size_t dl = d ? strlen(d) : 0, nl = strlen(name);
        if (d && dl > nl + 1 && !strcmp(d + dl - nl, name) && d[dl - nl - 1] == ':') return s;
    }
    return NULL;
}

dbg_info *info_load(const char *path, char *err, size_t errlen)
{
    dbg_info *di = calloc(1, sizeof *di);
    if (!di) { snprintf(err, errlen, "out of memory"); return NULL; }
    vec_init(&di->funcs, sizeof(dfunc)); vec_init(&di->lines, sizeof(dline));
    vec_init(&di->cus, sizeof(dcu)); vec_init(&di->files, sizeof(dfile));
    vec_init(&di->globals, sizeof(dvar)); vec_init(&di->structs, sizeof(dtype *));
    vec_init(&di->typedefs, sizeof(dtype *));
    vec_init(&di->patches, sizeof(dbg_info *));   /* --- hotpatch */
    if (elf_open(&di->elf, path)) {
        snprintf(err, errlen, "%s", di->elf.err);
        info_free(di);
        return NULL;
    }
    stabs_parse(di);

    /* globals: addresses from .symtab */
    elf_file *ef = &di->elf;
    const elf_symbol **sorted = malloc(sizeof(*sorted) * (ef->nsym ? ef->nsym : 1));
    if (sorted) {
        for (int i = 0; i < ef->nsym; i++) sorted[i] = &ef->sym[i];
        qsort(sorted, ef->nsym, sizeof(*sorted), symname_cmp);
        for (size_t i = 0; i < di->globals.n; i++) {
            dvar *v = &VEC_AT(&di->globals, dvar, i);
            if (v->cls != V_GLOBAL || v->addr) continue;
            const elf_symbol *s = sym_by_name(sorted, ef->nsym, v->name);
            if (!s) s = sym_by_cxx_name(ef, v->name);
            if (s) v->addr = s->value;
        }
        free(sorted);
    }
    if (!di->elf.stab) snprintf(di->err, sizeof di->err, "no stabs debug info (build with -gstabs)");
    return di;
}

void info_free(dbg_info *di)
{
    if (!di) return;
    vec_free(&di->funcs); vec_free(&di->lines); vec_free(&di->cus); vec_free(&di->files);
    vec_free(&di->globals); vec_free(&di->structs); vec_free(&di->typedefs);
    /* --- hotpatch */
    for (size_t i = 0; i < di->patches.n; i++) info_free(VEC_AT(&di->patches, dbg_info *, i));
    vec_free(&di->patches);
    free(di->patch_code);
    /* --- end hotpatch */
    free(di->reloc);
    arena_free(&di->mem);
    elf_close(&di->elf);
    free(di);
}

/* ---------------- relocation ---------------- */

void info_set_reloc(dbg_info *di, const dreloc *r, int n)
{
    free(di->reloc);
    di->reloc = NULL; di->nreloc = 0;
    if (n <= 0) return;
    di->reloc = malloc(sizeof(dreloc) * n);
    if (!di->reloc) return;
    memcpy(di->reloc, r, sizeof(dreloc) * n);
    di->nreloc = n;
}

uint32_t info_l2r(const dbg_info *di, uint32_t a)
{
    for (int i = 0; i < di->nreloc; i++) {
        const dreloc *r = &di->reloc[i];
        if (a >= r->link && a < r->link + (r->size ? r->size : 1)) return r->run + (a - r->link);
    }
    /* one past the end of a section (function ends) */
    for (int i = 0; i < di->nreloc; i++) {
        const dreloc *r = &di->reloc[i];
        if (a == r->link + r->size) return r->run + r->size;
    }
    return a;
}

uint32_t info_r2l(const dbg_info *di, uint32_t a)
{
    for (int i = 0; i < di->nreloc; i++) {
        const dreloc *r = &di->reloc[i];
        if (a >= r->run && a < r->run + (r->size ? r->size : 1)) return r->link + (a - r->run);
    }
    for (int i = 0; i < di->nreloc; i++) {
        const dreloc *r = &di->reloc[i];
        if (a == r->run + r->size) return r->link + r->size;
    }
    return a;
}

int info_in_program(const dbg_info *di, uint32_t run)
{
    for (int i = 0; i < di->npatch_code; i++)          /* --- hotpatch: patch code */
        if (run >= di->patch_code[i][0] && run < di->patch_code[i][1]) return 1;
    uint32_t link = info_r2l(di, run);
    if (di->nreloc) {
        int hit = 0;
        for (int i = 0; i < di->nreloc; i++)
            if (run >= di->reloc[i].run && run < di->reloc[i].run + di->reloc[i].size) { hit = 1; break; }
        if (!hit) return 0;
    }
    for (int i = 0; i < di->elf.nsect; i++) {
        const elf_section *s = &di->elf.sect[i];
        if ((s->flags & ELF_SHF_EXEC) && (s->flags & ELF_SHF_ALLOC) && link >= s->addr && link < s->addr + s->size) return 1;
    }
    return 0;
}

int info_in_sections(const dbg_info *di, uint32_t run, uint32_t len)
{
    for (int i = 0; i < di->nreloc; i++) {
        const dreloc *r = &di->reloc[i];
        if (run >= r->run && run + len <= r->run + r->size && run + len >= run) return 1;
    }
    return 0;
}

/* ---------------- functions and lines ---------------- */

static const dfunc *func_at_link(const dbg_info *di, uint32_t a)
{
    const dfunc *F = di->funcs.data;
    size_t lo = 0, hi = di->funcs.n;
    while (lo < hi) { size_t m = (lo + hi) / 2; if (F[m].lo <= a) lo = m + 1; else hi = m; }
    if (!lo) return NULL;
    const dfunc *f = &F[lo - 1];
    return a < f->hi ? f : NULL;
}

const dfunc *info_func_at(const dbg_info *di, uint32_t addr) { return func_at_link(di, info_r2l(di, addr)); }

const dfunc *info_func_named(const dbg_info *di, const char *name)
{
    const dfunc *F = di->funcs.data, *best = NULL;
    int best_score = 0;
    size_t nl = strlen(name);
    for (size_t i = 0; i < di->funcs.n; i++) {
        const dfunc *f = &F[i];
        int score = 0;
        if (!strcmp(f->name, name) || !strcmp(f->display, name)) score = 4;
        else if (!strcmp(f->base, name)) score = 3;
        else {
            size_t bl = strlen(f->base);
            if (bl > nl + 1 && !strcmp(f->base + bl - nl, name) && f->base[bl - nl - 1] == ':') score = 2;
        }
        if (score && f->superseded) score = 1;          /* --- hotpatch: the newest version wins */
        if (score > best_score) { best = f; best_score = score; }
    }
    return best;
}

static size_t line_index_le(const dbg_info *di, uint32_t link)
{
    const dline *L = di->lines.data;
    size_t lo = 0, hi = di->lines.n;
    while (lo < hi) { size_t m = (lo + hi) / 2; if (L[m].addr <= link) lo = m + 1; else hi = m; }
    return lo;   /* number of entries with addr <= link */
}

static const elf_symbol *func_sym_at(const dbg_info *di, uint32_t link)
{
    const elf_file *ef = &di->elf;
    int lo = 0, hi = ef->nsym;
    while (lo < hi) { int m = (lo + hi) / 2; if (ef->sym[m].value <= link) lo = m + 1; else hi = m; }
    for (int i = lo - 1; i >= 0 && i >= lo - 8; i--) {
        const elf_symbol *s = &ef->sym[i];
        if (s->type == ELF_STT_FUNC && link >= s->value && link < s->value + (s->size ? s->size : 4)) return s;
    }
    return NULL;
}

const dline *info_line_at(const dbg_info *di, uint32_t addr)
{
    uint32_t link = info_r2l(di, addr);
    size_t n = line_index_le(di, link);
    if (!n) return NULL;
    const dline *l = &VEC_AT(&di->lines, dline, n - 1);
    if (l->func >= 0) {
        const dfunc *f = &VEC_AT(&di->funcs, dfunc, l->func);
        if (link >= f->hi) return NULL;
        return l;
    }
    /* outside stabs functions (assembler): stay within one .symtab function */
    const elf_symbol *s = func_sym_at(di, link);
    if (s && l->addr >= s->value) return l;
    if (!s && link - l->addr < 64) return l;
    return NULL;
}

int info_is_line_start(const dbg_info *di, uint32_t addr)
{
    uint32_t link = info_r2l(di, addr);
    size_t n = line_index_le(di, link);
    return n && VEC_AT(&di->lines, dline, n - 1).addr == link;
}

const char *info_file_name(const dbg_info *di, int file)
{
    return file >= 0 && (size_t)file < di->files.n ? VEC_AT(&di->files, dfile, file).name : NULL;
}

/* Number of trailing path components that match (0 = no match). Case-insensitive. */
int info_file_matches(const dbg_info *di, int file, const char *user)
{
    const char *a = info_file_name(di, file);
    if (!a || !user) return 0;
    const char *ae = a + strlen(a), *be = user + strlen(user);
    int score = 0;
    for (;;) {
        const char *as = ae, *bs = be;
        while (as > a && as[-1] != '/' && as[-1] != ':') as--;
        while (bs > user && bs[-1] != '/' && bs[-1] != ':') bs--;
        size_t al = (size_t)(ae - as), bl = (size_t)(be - bs);
        if (al != bl || !al) break;
        for (size_t i = 0; i < al; i++)
            if (tolower((unsigned char)as[i]) != tolower((unsigned char)bs[i])) return score;
        score++;
        if (as == a || bs == user) break;
        ae = as - 1; be = bs - 1;
        if (*ae == ':' || *be == ':') break;
    }
    return score;
}

uint32_t info_line_addr(const dbg_info *di, const char *file, int line, int *actual)
{
    const dline *L = di->lines.data;
    int nfiles = (int)di->files.n, best = 0;
    int *score = calloc(nfiles ? nfiles : 1, sizeof(int));
    if (!score) return 0;
    for (int i = 0; i < nfiles; i++) {
        score[i] = info_file_matches(di, i, file);
        if (score[i] > best) best = score[i];
    }
    const dfunc *FF = di->funcs.data;   /* --- hotpatch: lines of replaced functions don't count */
#define LIVE(i) (L[i].func < 0 || !FF[L[i].func].superseded)
    uint32_t addr = 0;
    int found = 0;
    if (best) {
        int target = 0;
        for (size_t i = 0; i < di->lines.n; i++)   /* smallest line >= requested */
            if (score[L[i].file] == best && LIVE(i) && L[i].line >= line && (!target || L[i].line < target)) target = L[i].line;
        if (target) {
            for (size_t i = 0; i < di->lines.n; i++)
                if (score[L[i].file] == best && LIVE(i) && L[i].line == target) { addr = L[i].addr; found = target; break; }
            /* a function's opening line: move past the prologue */
            const dfunc *f = func_at_link(di, addr);
            if (f && addr == f->lo && f->body != f->lo) {
                addr = f->body;
                size_t n = line_index_le(di, addr);
                if (n) found = L[n - 1].line;
            }
        }
    }
#undef LIVE
    free(score);
    if (actual) *actual = found;
    return found ? info_l2r(di, addr) : 0;
}

int info_func_range(const dbg_info *di, uint32_t addr, uint32_t *lo, uint32_t *hi, uint32_t *body)
{
    uint32_t link = info_r2l(di, addr);
    const dfunc *f = func_at_link(di, link);
    if (f) {
        *lo = info_l2r(di, f->lo); *hi = info_l2r(di, f->hi);
        if (body) *body = info_l2r(di, f->body);
        return 1;
    }
    const elf_symbol *s = func_sym_at(di, link);
    if (!s) return 0;
    *lo = info_l2r(di, s->value);
    *hi = info_l2r(di, s->value + (s->size ? s->size : 4));
    if (body) *body = *lo;
    return 1;
}

int info_symbolize(const dbg_info *di, uint32_t addr, char *buf, size_t n)
{
    uint32_t link = info_r2l(di, addr);
    const dfunc *f = func_at_link(di, link);
    if (f) {
        if (link == f->lo) snprintf(buf, n, "%s", f->base);
        else snprintf(buf, n, "%s+0x%lx", f->base, (unsigned long)(link - f->lo));
        return 1;
    }
    const elf_file *ef = &di->elf;
    int lo = 0, hi = ef->nsym;
    while (lo < hi) { int m = (lo + hi) / 2; if (ef->sym[m].value <= link) lo = m + 1; else hi = m; }
    if (!lo) return 0;
    const elf_symbol *s = &ef->sym[lo - 1];
    /* prefer a sized symbol that covers the address */
    for (int i = lo - 1; i >= 0 && i >= lo - 8; i--)
        if (ef->sym[i].size && link < ef->sym[i].value + ef->sym[i].size) { s = &ef->sym[i]; break; }
    uint32_t off = link - s->value;
    if (s->size ? off >= s->size : off >= 0x1000) return 0;
    char dm[512];
    const char *nm = dbg_demangle(s->name, dm, sizeof dm);
    if (nm) {   /* drop the parameter list */
        char *paren = strchr(dm, '(');
        if (paren && paren != dm) *paren = 0;
    } else nm = s->name;
    if (off) snprintf(buf, n, "%s+0x%lx", nm, (unsigned long)off);
    else snprintf(buf, n, "%s", nm);
    return 1;
}

uint32_t info_symbol_addr(const dbg_info *di, const char *name)
{
    const elf_file *ef = &di->elf;
    for (int i = 0; i < ef->nsym; i++)
        if (!strcmp(ef->sym[i].name, name)) return info_l2r(di, ef->sym[i].value);
    const dfunc *f = info_func_named(di, name);
    if (f) return info_l2r(di, f->lo);
    return 0;
}

const dvar *info_global_named(const dbg_info *di, const char *name, int prefer_cu)
{
    const dvar *any = NULL;
    for (size_t i = 0; i < di->globals.n; i++) {
        const dvar *v = &VEC_AT(&di->globals, dvar, i);
        if (strcmp(v->name, name)) continue;
        if (v->cu == prefer_cu) return v;
        if (!any || (v->cls == V_GLOBAL && any->cls != V_GLOBAL)) any = v;
    }
    return any;
}

dtype *info_type_named(const dbg_info *di, const char *name, int tag)
{
    if (tag) {
        dtype_kind want = tag == 'u' ? T_UNION : tag == 'e' ? T_ENUM : T_STRUCT;
        dtype *partial = NULL;
        for (size_t i = 0; i < di->structs.n; i++) {
            dtype *t = VEC_AT(&di->structs, dtype *, i);
            if (t->kind == want && t->name && !strcmp(t->name, name)) {
                if (t->nfields || t->nenums) return t;
                partial = t;
            }
        }
        return partial;
    }
    for (size_t i = 0; i < di->typedefs.n; i++) {
        dtype *t = VEC_AT(&di->typedefs, dtype *, i);
        if (t->name && !strcmp(t->name, name)) return t;
    }
    return NULL;
}

int info_cu_at(const dbg_info *di, uint32_t addr)
{
    uint32_t link = info_r2l(di, addr);
    const dfunc *f = func_at_link(di, link);
    if (f) return f->cu;
    for (size_t i = 0; i < di->cus.n; i++) {
        const dcu *c = &VEC_AT(&di->cus, dcu, i);
        if (link >= c->lo && link < c->hi) return (int)i;
    }
    return -1;
}

uint32_t info_var_addr(const dbg_info *di, const dvar *v)
{
    if ((v->cls == V_STATIC || v->cls == V_GLOBAL) && v->addr) return info_l2r(di, v->addr);
    return 0;
}
