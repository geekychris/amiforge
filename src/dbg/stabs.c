/*
 * stabs.c — parses .stab/.stabstr (GCC -gstabs / -gstabs+) into dbg_info: compilation
 * units, source files, the line table, functions with parameters and scoped locals,
 * globals/statics, and the type graph (C types plus the parts of the C++ extensions we
 * need: classes with base classes, fields and static members; methods are skipped).
 *
 * ELF stabs conventions used by this toolchain: N_FUN values are absolute link addresses,
 * N_SLINE/N_LBRAC/N_RBRAC values are relative to the enclosing function (absolute outside
 * one, as in assembler files), N_PSYM/N_LSYM are frame offsets, N_RSYM register numbers.
 */
#include "dbg_int.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

enum {
    N_UNDF = 0x00, N_GSYM = 0x20, N_FNAME = 0x22, N_FUN = 0x24, N_STSYM = 0x26, N_LCSYM = 0x28,
    N_ROSYM = 0x2c, N_RSYM = 0x40, N_SLINE = 0x44, N_SO = 0x64, N_LSYM = 0x80, N_BINCL = 0x82,
    N_SOL = 0x84, N_PSYM = 0xa0, N_EINCL = 0xa2, N_LBRAC = 0xc0, N_EXCL = 0xc2, N_RBRAC = 0xe0
};

typedef struct { dtype **v; int n; } ttab;
typedef struct { const char *name; uint32_t value; int tab; } hdr;
typedef struct { int idx; uint32_t lo; } block;   /* open LBRAC: first local index, start */

typedef struct {
    dbg_info *di;
    arena *A;
    vec tabs;        /* ttab */
    vec hdrs;        /* hdr: N_BINCL headers, for N_EXCL */
    vec filemap;     /* int: type-file number -> tabs index, for the current unit */
    vec xrefs;       /* dtype*: cross-references to resolve at the end */
    int cu, cur_file;
    int func;        /* index of the open function, -1 if none */
    int func_closed; /* its N_FUN end marker has been seen */
    uint32_t func_lo;
    vec params, locals;   /* dvar for the open function */
    vec blocks;      /* block stack */
    int pending;     /* first local not yet assigned to a block */
    const char *defining;   /* name being defined by the current stab ('t'/'T') */
} pstate;

/* ---------------- type tables ---------------- */

static dtype *new_type(pstate *ps, dtype_kind k)
{
    dtype *t = arena_alloc(ps->A, sizeof *t);
    t->kind = k;
    return t;
}

static int new_tab(pstate *ps)
{
    ttab *t = vec_push(&ps->tabs);
    (void)t;
    return (int)ps->tabs.n - 1;
}

static dtype **slot(pstate *ps, int file, int num)
{
    if (file < 0 || num < 0 || num > 1000000) return NULL;
    while ((int)ps->filemap.n <= file) *(int *)vec_push(&ps->filemap) = new_tab(ps);
    ttab *t = &VEC_AT(&ps->tabs, ttab, VEC_AT(&ps->filemap, int, file));
    if (num >= t->n) {
        int n = t->n ? t->n : 64;
        while (n <= num) n *= 2;
        dtype **v = realloc(t->v, sizeof(dtype *) * n);
        if (!v) return NULL;
        memset(v + t->n, 0, sizeof(dtype *) * (n - t->n));
        t->v = v; t->n = n;
    }
    return &t->v[num];
}

static dtype *typeref(pstate *ps, int file, int num)
{
    dtype **s = slot(ps, file, num);
    if (!s) return new_type(ps, T_UNKNOWN);
    if (!*s) *s = new_type(ps, T_UNKNOWN);
    return *s;
}

/* ---------------- lexing helpers ---------------- */

static long read_num(const char **pp)
{
    const char *p = *pp;
    int neg = 0;
    long v = 0;
    if (*p == '-') { neg = 1; p++; }
    while (isdigit((unsigned char)*p)) v = v * 10 + (*p++ - '0');
    *pp = p;
    return neg ? -v : v;
}

/* Bounds of a range type can be decimal or (for big values) octal with a leading 0. */
static int read_bound(const char **pp, int64_t *v, uint64_t *u, int *is_octal)
{
    const char *p = *pp;
    int neg = 0;
    uint64_t x = 0;
    if (*p == '-') { neg = 1; p++; }
    *is_octal = (*p == '0' && isdigit((unsigned char)p[1]));
    if (!isdigit((unsigned char)*p)) return 0;
    while (isdigit((unsigned char)*p)) x = *is_octal ? x * 8 + (uint64_t)(*p++ - '0') : x * 10 + (uint64_t)(*p++ - '0');
    *u = neg ? (uint64_t)(-(int64_t)x) : x;
    *v = neg ? -(int64_t)x : (int64_t)x;
    *pp = p;
    return 1;
}

/* A type number: "N" or "(F,N)". Returns 0 if *pp doesn't start one. */
static int read_typenum(const char **pp, int *file, int *num)
{
    const char *p = *pp;
    if (*p == '(') {
        p++;
        *file = (int)read_num(&p);
        if (*p != ',') return 0;
        p++;
        *num = (int)read_num(&p);
        if (*p != ')') return 0;
        p++;
    } else if (isdigit((unsigned char)*p) || (*p == '-' && isdigit((unsigned char)p[1]))) {
        *file = 0;
        *num = (int)read_num(&p);
    } else return 0;
    *pp = p;
    return 1;
}

/* Name up to ':' at template depth 0. */
static const char *read_name(const char **pp, size_t *len)
{
    const char *s = *pp, *p = s;
    int depth = 0;
    for (; *p; p++) {
        if (*p == '<') depth++;
        else if (*p == '>') depth--;
        else if (*p == ':' && depth <= 0) break;
    }
    *len = (size_t)(p - s);
    *pp = p;
    return s;
}

static void skip_past(const char **pp, char c)
{
    const char *p = *pp;
    while (*p && *p != c) p++;
    if (*p) p++;
    *pp = p;
}

/* ---------------- type parsing ---------------- */

static dtype *parse_type(pstate *ps, const char **pp);
static void parse_body(pstate *ps, const char **pp, dtype *t);

static void builtin(dtype *t, int num)
{
    static const struct { int num; const char *name; int size, sgn, flt; } b[] = {
        { -1, "int", 4, 1, 0 }, { -2, "char", 1, 1, 0 }, { -3, "short", 2, 1, 0 }, { -4, "long", 4, 1, 0 },
        { -5, "unsigned char", 1, 0, 0 }, { -6, "signed char", 1, 1, 0 }, { -7, "unsigned short", 2, 0, 0 },
        { -8, "unsigned int", 4, 0, 0 }, { -9, "unsigned", 4, 0, 0 }, { -10, "unsigned long", 4, 0, 0 },
        { -11, "void", 0, 0, 0 }, { -12, "float", 4, 1, 1 }, { -13, "double", 8, 1, 1 },
        { -14, "long double", 8, 1, 1 }, { -15, "int", 4, 1, 0 }, { -16, "bool", 4, 0, 0 },
        { -20, "char", 1, 0, 0 }, { -21, "byte", 1, 1, 0 }, { -27, "char", 1, 1, 0 },
        { -31, "long long", 8, 1, 0 }, { -32, "unsigned long long", 8, 0, 0 },
    };
    for (size_t i = 0; i < sizeof b / sizeof b[0]; i++) if (b[i].num == num) {
        t->name = b[i].name; t->size = (uint32_t)b[i].size; t->is_signed = (uint8_t)b[i].sgn;
        t->kind = num == -11 ? T_VOID : num == -16 ? T_BOOL : b[i].flt ? T_FLOAT : T_INT;
        t->is_char = (b[i].size == 1 && num != -21);
        return;
    }
    t->kind = T_INT; t->size = 4; t->is_signed = 1; t->name = "int";
}

/* r<base>;<lo>;<hi>; */
static void parse_range(pstate *ps, const char **pp, dtype *t)
{
    const char *p = *pp;
    dtype *base = parse_type(ps, &p);
    int64_t lo = 0, hi = 0;
    uint64_t ulo = 0, uhi = 0;
    int olo = 0, ohi = 0;
    if (*p == ';') p++;
    int ok = read_bound(&p, &lo, &ulo, &olo);
    if (*p == ';') p++;
    ok = ok && read_bound(&p, &hi, &uhi, &ohi);
    if (*p == ';') p++;
    *pp = p;
    t->lo = (int32_t)lo; t->hi = (int32_t)hi;
    if (!ok) { t->kind = T_INT; t->size = 4; t->is_signed = 1; return; }
    if (hi == 0 && lo > 0 && !olo) {           /* r(0,1);4;0; = 4-byte float */
        t->kind = T_FLOAT; t->size = (uint32_t)lo; t->is_signed = 1;
        return;
    }
    if (base == t && lo == 0 && hi == 0) { t->kind = T_VOID; return; }
    t->kind = T_INT;
    if (olo || ohi) {                          /* octal bounds: 64-bit (or full 32-bit unsigned) */
        if (ulo == 0) {                        /* unsigned */
            t->is_signed = 0;
            t->size = uhi > 0xffffffffull ? 8 : uhi > 0xffff ? 4 : uhi > 0xff ? 2 : 1;
        } else {                               /* lo = 0100..0 (two's complement min) */
            t->is_signed = 1;
            t->size = ulo > 0xffffffffull || uhi > 0x7fffffffull ? 8 : 4;
        }
        return;
    }
    if (lo < 0) {
        t->is_signed = 1;
        t->size = hi <= 127 ? 1 : hi <= 32767 ? 2 : hi <= 2147483647LL ? 4 : 8;
    } else {
        t->is_signed = 0;
        t->size = hi <= 255 ? 1 : hi <= 65535 ? 2 : (uint64_t)hi <= 0xffffffffull ? 4 : 8;
    }
    if (t->size == 1) t->is_char = 1;          /* refined by name: "char", "unsigned char" */
}

/* Skips a C++ method list entry group: name:: already consumed. */
static void skip_methods(pstate *ps, const char **pp)
{
    const char *p = *pp;
    while (*p) {
        parse_type(ps, &p);                    /* method type (#...) */
        if (*p == ':') p++;
        skip_past(&p, ';');                    /* physname */
        if (*p) p++;                           /* visibility */
        if (*p) p++;                           /* const/volatile qualifier */
        if (*p == '*') {                       /* virtual: index ; [type ;] */
            p++;
            skip_past(&p, ';');
            if (*p == '(' || isdigit((unsigned char)*p)) { parse_type(ps, &p); if (*p == ';') p++; }
        } else if (*p == '?' || *p == '.') p++;
        if (*p == ';') { p++; break; }         /* end of this method's overloads */
    }
    *pp = p;
}

/* s<size>[!<nbases>,{<virt><vis><offset>,<type>;}]{<field>}; [~%<type>;] */
static void parse_struct(pstate *ps, const char **pp, dtype *t, int is_union)
{
    const char *p = *pp;
    vec fields;
    vec_init(&fields, sizeof(dfield));
    t->kind = is_union ? T_UNION : T_STRUCT;
    t->size = (uint32_t)read_num(&p);
    if (*p == '!') {                           /* C++ base classes */
        p++;
        int nb = (int)read_num(&p);
        if (*p == ',') p++;
        for (int i = 0; i < nb && *p; i++) {
            dfield *f = vec_push(&fields);
            if (*p) p++;                       /* virtual flag */
            if (*p) f->vis = (uint8_t)*p++;
            f->bitpos = (uint32_t)read_num(&p);
            if (*p == ',') p++;
            f->type = parse_type(ps, &p);
            f->is_base = 1;
            if (*p == ';') p++;
        }
    }
    while (*p && *p != ';') {
        size_t nl;
        const char *nm = read_name(&p, &nl);
        if (*p != ':') break;
        p++;
        if (*p == ':') {                       /* method */
            p++;
            skip_methods(ps, &p);
            continue;
        }
        dfield *f = vec_push(&fields);
        f->name = arena_strndup(ps->A, nm, nl);
        if (*p == '/') { p++; if (*p) f->vis = (uint8_t)*p++; }
        f->type = parse_type(ps, &p);
        if (*p == ':') {                       /* static member: :physname; */
            p++;
            const char *ph = p;
            skip_past(&p, ';');
            f->is_static = 1;
            f->physname = arena_strndup(ps->A, ph, (size_t)(p - ph - (p[-1] == ';')));
            continue;
        }
        if (*p == ',') p++;
        f->bitpos = (uint32_t)read_num(&p);
        if (*p == ',') p++;
        f->bitsize = (uint32_t)read_num(&p);
        if (*p == ';') p++;
    }
    if (*p == ';') p++;
    if (*p == '~') {                           /* ~%<vptr type>; */
        p++;
        if (*p == '%') { p++; parse_type(ps, &p); }
        if (*p == ';') p++;
    }
    t->nfields = (int)fields.n;
    if (fields.n) {
        t->fields = arena_alloc(ps->A, sizeof(dfield) * fields.n);
        memcpy(t->fields, fields.data, sizeof(dfield) * fields.n);
    }
    vec_free(&fields);
    *pp = p;
}

static void parse_enum(pstate *ps, const char **pp, dtype *t)
{
    const char *p = *pp;
    vec ev;
    vec_init(&ev, sizeof(denum));
    t->kind = T_ENUM; t->size = 4; t->is_signed = 1;
    while (*p && *p != ';') {
        size_t nl;
        const char *nm = read_name(&p, &nl);
        if (*p != ':') break;
        p++;
        denum *e = vec_push(&ev);
        e->name = arena_strndup(ps->A, nm, nl);
        e->value = (int32_t)read_num(&p);
        if (*p == ',') p++;
    }
    if (*p == ';') p++;
    t->nenums = (int)ev.n;
    if (ev.n) {
        t->enums = arena_alloc(ps->A, sizeof(denum) * ev.n);
        memcpy(t->enums, ev.data, sizeof(denum) * ev.n);
    }
    vec_free(&ev);
    *pp = p;
}

static void reset_type(dtype *t) { memset(t, 0, sizeof *t); }

/* Parses a type definition body into t (in place, so earlier references see it). */
static void parse_body(pstate *ps, const char **pp, dtype *t)
{
    const char *p = *pp;
    uint32_t attr_size = 0;
    while (*p == '@' && isalpha((unsigned char)p[1])) {   /* attributes: @s<bits>; */
        if (p[1] == 's') { const char *q = p + 2; attr_size = (uint32_t)read_num(&q); }
        skip_past(&p, ';');
    }
    int file, num;
    reset_type(t);
    char c = *p;
    if (c == '(' || isdigit((unsigned char)c) || (c == '-' && isdigit((unsigned char)p[1]))) {
        const char *q = p;
        if (read_typenum(&q, &file, &num) && num < 0 && *q != '=') {   /* builtin type */
            builtin(t, num);
            p = q;
        } else {
            dtype *target = parse_type(ps, &p);
            if (target == t) t->kind = T_VOID;         /* (0,9)=(0,9): void */
            else { t->kind = T_ALIAS; t->target = target; }
        }
    } else switch (*p++) {
    case 'r': parse_range(ps, &p, t); break;
    case '*': t->kind = T_PTR; t->size = 4; t->target = parse_type(ps, &p); break;
    case '&': t->kind = T_REF; t->size = 4; t->target = parse_type(ps, &p); break;
    case 'k': t->kind = T_CONST; t->target = parse_type(ps, &p); break;
    case 'B': t->kind = T_VOLATILE; t->target = parse_type(ps, &p); break;
    case 'f': case 'F':
        t->kind = T_FUNC; t->target = parse_type(ps, &p);
        while (*p == ',') { p++; parse_type(ps, &p); }  /* prototyped args (rare) */
        if (*p == ';' && c == 'F') p++;
        break;
    case '#':                                           /* method type */
        t->kind = T_FUNC;
        if (*p == '#') { p++; t->target = parse_type(ps, &p); }
        else {
            parse_type(ps, &p);                         /* class */
            if (*p == ',') { p++; t->target = parse_type(ps, &p); }
            while (*p == ',') { p++; parse_type(ps, &p); }
        }
        if (*p == ';') p++;
        break;
    case '@':                                           /* member pointer @class,type */
        t->kind = T_MEMBERPTR; t->size = 4;
        parse_type(ps, &p);
        if (*p == ',') p++;
        t->target = parse_type(ps, &p);
        break;
    case 'a': {                                         /* a<index range>;<elem> */
        dtype idx;
        memset(&idx, 0, sizeof idx);
        if (*p == 'r') { p++; parse_range(ps, &p, &idx); }
        else parse_type(ps, &p);
        t->kind = T_ARRAY;
        t->lo = idx.lo; t->hi = idx.hi;
        t->target = parse_type(ps, &p);
        break;
    }
    case 's': parse_struct(ps, &p, t, 0); break;
    case 'u': parse_struct(ps, &p, t, 1); break;
    case 'e': parse_enum(ps, &p, t); break;
    case 'x': {
        size_t nl;
        t->kind = T_XREF;
        t->xref_tag = *p ? *p++ : 's';
        const char *nm = read_name(&p, &nl);
        t->name = arena_strndup(ps->A, nm, nl);
        if (*p == ':') p++;
        *(dtype **)vec_push(&ps->xrefs) = t;
        break;
    }
    case 'R':                                           /* R<kind>;<bytes>;0; */
        read_num(&p); if (*p == ';') p++;
        t->kind = T_FLOAT; t->size = (uint32_t)read_num(&p); t->is_signed = 1;
        skip_past(&p, ';'); skip_past(&p, ';');
        break;
    default:
        t->kind = T_UNKNOWN;
        p += strlen(p);                                 /* can't tell where it ends */
        break;
    }
    if (attr_size && (t->kind == T_INT || t->kind == T_BOOL)) t->size = attr_size / 8;
    *pp = p;
}

static dtype *parse_type(pstate *ps, const char **pp)
{
    int file, num;
    const char *p = *pp;
    if (read_typenum(&p, &file, &num)) {
        if (num < 0 && *p != '=') {                     /* reference to a builtin */
            dtype *t = new_type(ps, T_INT);
            builtin(t, num);
            *pp = p;
            return t;
        }
        dtype *t = typeref(ps, file, num);
        if (*p == '=') {
            p++;
            parse_body(ps, &p, t);
        }
        *pp = p;
        return t;
    }
    dtype *t = new_type(ps, T_UNKNOWN);
    parse_body(ps, &p, t);
    *pp = p;
    return t;
}

/* ---------------- symbols ---------------- */

static int file_index(pstate *ps, const char *name)
{
    vec *fv = &ps->di->files;
    for (size_t i = 0; i < fv->n; i++)
        if (!strcmp(VEC_AT(fv, dfile, i).name, name)) return (int)i;
    dfile *f = vec_push(fv);
    f->name = arena_strdup(ps->A, name);
    f->cu = ps->cu;
    return (int)fv->n - 1;
}

static void close_func(pstate *ps)
{
    if (ps->func < 0) return;
    dfunc *f = &VEC_AT(&ps->di->funcs, dfunc, ps->func);
    for (size_t i = (size_t)ps->pending; i < ps->locals.n; i++) {   /* no block: whole function */
        dvar *v = &VEC_AT(&ps->locals, dvar, i);
        v->lo = 0; v->hi = 0xffffffffu;
    }
    /* blocks still open (malformed): close at the end */
    for (size_t b = 0; b < ps->blocks.n; b++) {
        block *bl = &VEC_AT(&ps->blocks, block, b);
        for (size_t i = (size_t)bl->idx; i < ps->locals.n; i++)
            if (VEC_AT(&ps->locals, dvar, i).hi == 0) VEC_AT(&ps->locals, dvar, i).hi = 0xffffffffu;
    }
    f->nparams = (int)ps->params.n;
    if (f->nparams) {
        f->params = arena_alloc(ps->A, sizeof(dvar) * ps->params.n);
        memcpy(f->params, ps->params.data, sizeof(dvar) * ps->params.n);
    }
    f->nlocals = (int)ps->locals.n;
    if (f->nlocals) {
        f->locals = arena_alloc(ps->A, sizeof(dvar) * ps->locals.n);
        memcpy(f->locals, ps->locals.data, sizeof(dvar) * ps->locals.n);
    }
    if (!f->hi) f->hi = f->lo;
    ps->params.n = ps->locals.n = ps->blocks.n = 0;
    ps->pending = 0;
    ps->func = -1;
    ps->func_closed = 0;
}

static void add_global(pstate *ps, const dvar *v)
{
    dvar *g = vec_push(&ps->di->globals);
    *g = *v;
    g->cu = ps->cu;
}

static void name_type(pstate *ps, dtype *t, const char *name, int is_tag, int also_typedef)
{
    dtype *u = t;
    if (u->kind == T_ALIAS && !is_tag) {                /* typedef: name:t(0,a)=(0,b) */
        u->kind = T_TYPEDEF;
        u->name = name;
        *(dtype **)vec_push(&ps->di->typedefs) = u;
        return;
    }
    if (u->kind == T_STRUCT || u->kind == T_UNION || u->kind == T_ENUM) {
        if (!u->name || is_tag) {
            int fresh = !u->name;
            u->name = name;
            if (!is_tag || also_typedef) u->plain_name = 1;
            if (is_tag || fresh) *(dtype **)vec_push(&ps->di->structs) = u;
        }
        if (also_typedef || !is_tag) *(dtype **)vec_push(&ps->di->typedefs) = u;
        return;
    }
    if (!is_tag && (u->kind == T_INT || u->kind == T_FLOAT || u->kind == T_BOOL || u->kind == T_VOID)) {
        if (!u->name) u->name = name;
        if (u->kind == T_INT && u->size == 1) u->is_char = strstr(name, "char") != NULL;
        if (!strcmp(name, "bool") || !strcmp(name, "_Bool")) u->kind = T_BOOL;
        *(dtype **)vec_push(&ps->di->typedefs) = u;
        return;
    }
    if (!is_tag && !u->name) {                          /* typedef of a pointer etc.: wrap */
        dtype *td = new_type(ps, T_TYPEDEF);
        *td = *u;                                       /* keep the definition reachable */
        u->kind = T_TYPEDEF; u->name = name; u->target = td;
        u->nfields = 0; u->fields = NULL; u->nenums = 0; u->enums = NULL;
        *(dtype **)vec_push(&ps->di->typedefs) = u;
    }
}

static void parse_symbol(pstate *ps, int type, int desc, uint32_t value, const char *str)
{
    /* split at the first ':' that isn't part of "::" */
    const char *p = str;
    for (;;) {
        p = strchr(p, ':');
        if (!p) return;
        if (p[1] == ':') { p += 2; continue; }
        break;
    }
    size_t nl = (size_t)(p - str);
    p++;
    char d = *p;
    dvar v;
    memset(&v, 0, sizeof v);
    v.line = desc;
    if (d == 't' || d == 'T') {
        int is_tag = d == 'T', also = 0;
        p++;
        if (is_tag && *p == 't') { also = 1; p++; }
        const char *name = arena_strndup(ps->A, str, nl);
        dtype *t = parse_type(ps, &p);
        if (nl && strcmp(name, " ")) name_type(ps, t, name, is_tag, also);
        return;
    }
    if (d == 'c') {                                     /* constant: c=i<n>; or c=e<type>,<n>; */
        p++;
        if (*p == '=') p++;
        v.name = arena_strndup(ps->A, str, nl);
        v.cls = V_CONST;
        if (*p == 'i') { p++; v.off = (int32_t)read_num(&p); v.type = typeref(ps, -1, 0); }
        else if (*p == 'e') { p++; v.type = parse_type(ps, &p); if (*p == ',') p++; v.off = (int32_t)read_num(&p); }
        else return;
        if (!v.type || v.type->kind == T_UNKNOWN) { v.type = new_type(ps, T_INT); builtin(v.type, -1); }
        if (ps->func >= 0) { v.hi = 0xffffffffu; *(dvar *)vec_push(&ps->locals) = v; }
        return;
    }
    if (d == 'F' || d == 'f') {
        if (!nl) return;
        close_func(ps);
        p++;
        dfunc *f = vec_push(&ps->di->funcs);
        f->name = arena_strndup(ps->A, str, nl);
        f->lo = value;
        f->line = desc;
        f->file = ps->cur_file;
        f->cu = ps->cu;
        f->ret = parse_type(ps, &p);
        ps->func = (int)ps->di->funcs.n - 1;
        ps->func_lo = value;
        ps->func_closed = 0;
        return;
    }
    v.name = arena_strndup(ps->A, str, nl);
    if (isdigit((unsigned char)d) || d == '(' || d == '-') {
        v.cls = V_LOCAL;
    } else {
        p++;
        switch (d) {
        case 'p': v.cls = V_PARAM; break;
        case 'P': case 'R': v.cls = V_REGPARM; break;
        case 'r': v.cls = V_REG; break;
        case 'v': v.cls = V_REFPARAM; break;
        case 'a': v.cls = V_REGPARM; break;             /* reference in a register: treat as reg */
        case 'G': v.cls = V_GLOBAL; break;
        case 'S': case 'V': v.cls = V_STATIC; break;
        default: return;
        }
    }
    v.type = parse_type(ps, &p);
    switch (v.cls) {
    case V_LOCAL: case V_REG:
        v.off = (int32_t)value;
        if (ps->func < 0) return;
        *(dvar *)vec_push(&ps->locals) = v;
        break;
    case V_PARAM: case V_REGPARM: case V_REFPARAM:
        v.off = (int32_t)value;
        if (ps->func < 0) return;
        if (type == N_RSYM && v.cls == V_REGPARM) {     /* register copy of a stack param: replace */
            for (size_t i = 0; i < ps->params.n; i++) {
                dvar *o = &VEC_AT(&ps->params, dvar, i);
                if (!strcmp(o->name, v.name)) { *o = v; return; }
            }
        }
        *(dvar *)vec_push(&ps->params) = v;
        break;
    case V_STATIC:
        v.addr = value;
        if (d == 'V' && ps->func >= 0) {               /* function-local static */
            for (size_t i = 0; i < ps->locals.n; i++)
                if (!strcmp(VEC_AT(&ps->locals, dvar, i).name, v.name) && VEC_AT(&ps->locals, dvar, i).cls == V_STATIC) return;
            *(dvar *)vec_push(&ps->locals) = v;
            if (ps->func_closed) VEC_AT(&ps->locals, dvar, ps->locals.n - 1).hi = 0xffffffffu;
            break;
        }
        if (d == 'V') return;
        add_global(ps, &v);
        break;
    case V_GLOBAL:
        v.addr = 0;                                     /* resolved from .symtab later */
        add_global(ps, &v);
        break;
    default:
        break;
    }
}

/* ---------------- main loop ---------------- */

static int dfunc_cmp(const void *a, const void *b)
{
    const dfunc *x = a, *y = b;
    return x->lo < y->lo ? -1 : x->lo > y->lo;
}

/* Strips the parameter list from a demangled name. */
static const char *func_base(arena *A, const char *d)
{
    int depth = 0;
    for (const char *p = d; *p; p++) {
        if (!strncmp(p, "operator()", 10)) { p += 9; continue; }
        if (*p == '<') depth++;
        else if (*p == '>') depth--;
        else if (*p == '(' && depth <= 0) return arena_strndup(A, d, (size_t)(p - d));
    }
    return d;
}

void stabs_finish_funcs(dbg_info *di);

int stabs_parse(dbg_info *di)
{
    elf_file *ef = &di->elf;
    if (!ef->stab || ef->stab_size < 12) return 0;
    pstate ps;
    memset(&ps, 0, sizeof ps);
    ps.di = di; ps.A = &di->mem;
    vec_init(&ps.tabs, sizeof(ttab)); vec_init(&ps.hdrs, sizeof(hdr)); vec_init(&ps.filemap, sizeof(int));
    vec_init(&ps.xrefs, sizeof(dtype *)); vec_init(&ps.params, sizeof(dvar)); vec_init(&ps.locals, sizeof(dvar));
    vec_init(&ps.blocks, sizeof(block));
    ps.func = -1; ps.cu = -1; ps.cur_file = -1;

    uint32_t nstab = ef->stab_size / 12, strbase = 0, next_strbase = 0;
    const char *dir = NULL;
    char *cont = NULL;                                  /* continued ("\\"-terminated) string */
    size_t cont_len = 0;
    for (uint32_t i = 0; i < nstab; i++) {
        const uint8_t *e = ef->stab + i * 12;
        uint32_t strx = be32(e);
        int type = e[4];
        int desc = (int16_t)be16(e + 6);
        uint32_t value = be32(e + 8);
        if (type == N_UNDF) {                           /* unit header: string table base moves */
            strbase = next_strbase;
            next_strbase += value;
            continue;
        }
        if (type < 0x20) continue;                      /* not a stab */
        const char *str = "";
        if (strx && strbase + strx < ef->stabstr_size) str = ef->stabstr + strbase + strx;
        size_t sl = strlen(str);
        if (sl && str[sl - 1] == '\\') {                /* continuation */
            char *n = realloc(cont, cont_len + sl);
            if (n) { cont = n; memcpy(cont + cont_len, str, sl - 1); cont_len += sl - 1; cont[cont_len] = 0; }
            continue;
        }
        if (cont_len) {
            char *n = realloc(cont, cont_len + sl + 1);
            if (n) { cont = n; memcpy(cont + cont_len, str, sl + 1); str = cont; }
            cont_len = 0;
        }

        switch (type) {
        case N_SO:
            if (!*str) {                                /* end of unit */
                close_func(&ps);
                if (ps.cu >= 0) VEC_AT(&di->cus, dcu, ps.cu).hi = value;
                dir = NULL;
                break;
            }
            if (str[strlen(str) - 1] == '/') { dir = arena_strdup(ps.A, str); break; }
            close_func(&ps);
            {
                dcu *c = vec_push(&di->cus);
                c->dir = dir; c->name = arena_strdup(ps.A, str); c->lo = value;
                ps.cu = (int)di->cus.n - 1;
                ps.filemap.n = 0;                        /* fresh type numbering */
                ps.cur_file = file_index(&ps, str);
            }
            break;
        case N_SOL:
            if (*str) ps.cur_file = file_index(&ps, str);
            break;
        case N_BINCL: {
            hdr *h = vec_push(&ps.hdrs);
            h->name = arena_strdup(ps.A, str); h->value = value; h->tab = new_tab(&ps);
            *(int *)vec_push(&ps.filemap) = h->tab;
            break;
        }
        case N_EXCL: {
            int tab = -1;
            for (size_t k = 0; k < ps.hdrs.n; k++) {
                hdr *h = &VEC_AT(&ps.hdrs, hdr, k);
                if (h->value == value && !strcmp(h->name, str)) { tab = h->tab; break; }
            }
            *(int *)vec_push(&ps.filemap) = tab >= 0 ? tab : new_tab(&ps);
            break;
        }
        case N_FUN:
            if (!*str) {                                /* end of function: value = size */
                if (ps.func >= 0) {
                    VEC_AT(&di->funcs, dfunc, ps.func).hi = ps.func_lo + value;
                    ps.func_closed = 1;
                }
                break;
            }
            parse_symbol(&ps, type, desc, value, str);
            break;
        case N_SLINE: {
            if (ps.cur_file < 0) break;
            dline *l = vec_push(&di->lines);
            int in_func = ps.func >= 0 && !ps.func_closed;
            l->addr = in_func ? ps.func_lo + value : value;
            l->line = desc < 0 ? desc & 0xffff : desc;
            l->file = (uint16_t)ps.cur_file;
            l->func = in_func ? ps.func : -1;
            break;
        }
        case N_LBRAC:
            if (ps.func >= 0) {
                block *b = vec_push(&ps.blocks);
                b->idx = ps.pending; b->lo = value;
                for (size_t k = (size_t)ps.pending; k < ps.locals.n; k++) VEC_AT(&ps.locals, dvar, k).lo = value;
                ps.pending = (int)ps.locals.n;
            }
            break;
        case N_RBRAC:
            if (ps.func >= 0 && ps.blocks.n) {
                block *b = &VEC_AT(&ps.blocks, block, ps.blocks.n - 1);
                for (size_t k = (size_t)b->idx; k < (size_t)ps.pending && k < ps.locals.n; k++) {
                    dvar *v = &VEC_AT(&ps.locals, dvar, k);
                    if (!v->hi) v->hi = value;
                }
                ps.blocks.n--;
            }
            break;
        case N_GSYM: case N_STSYM: case N_LCSYM: case N_ROSYM:
        case N_LSYM: case N_PSYM: case N_RSYM:
            if (*str) parse_symbol(&ps, type, desc, value, str);
            break;
        default:
            break;
        }
    }
    close_func(&ps);
    free(cont);

    /* resolve cross-references to complete types by name */
    for (size_t i = 0; i < ps.xrefs.n; i++) {
        dtype *x = VEC_AT(&ps.xrefs, dtype *, i);
        if (x->kind != T_XREF || x->target) continue;
        dtype_kind want = x->xref_tag == 'u' ? T_UNION : x->xref_tag == 'e' ? T_ENUM : T_STRUCT;
        for (size_t k = 0; k < di->structs.n; k++) {
            dtype *s = VEC_AT(&di->structs, dtype *, k);
            if (s->kind == want && s->name && !strcmp(s->name, x->name) && (s->nfields || s->nenums || s->size)) {
                x->target = s;
                break;
            }
        }
    }

    for (size_t i = 0; i < ps.tabs.n; i++) free(VEC_AT(&ps.tabs, ttab, i).v);
    vec_free(&ps.tabs); vec_free(&ps.hdrs); vec_free(&ps.filemap); vec_free(&ps.xrefs);
    vec_free(&ps.params); vec_free(&ps.locals); vec_free(&ps.blocks);

    if (di->lines.n) {
        /* stable sort by address (stab order is kept for equal addresses; the later entry
         * wins on lookup). The table is almost sorted already, so insertion sort is cheap. */
        dline *L = di->lines.data;
        size_t n = di->lines.n;
        for (size_t i = 1; i < n; i++) {
            if (L[i - 1].addr <= L[i].addr) continue;
            dline t = L[i];
            size_t j = i;
            while (j > 0 && L[j - 1].addr > t.addr) { L[j] = L[j - 1]; j--; }
            L[j] = t;
        }
    }

    /* sort functions, remapping the indices kept in the line table */
    size_t nf = di->funcs.n;
    if (nf) {
        dfunc *F = di->funcs.data;
        int *order = malloc(sizeof(int) * nf), *where = malloc(sizeof(int) * nf);
        dfunc *copy = malloc(sizeof(dfunc) * nf);
        if (order && where && copy) {
            memcpy(copy, F, sizeof(dfunc) * nf);
            for (size_t i = 0; i < nf; i++) order[i] = (int)i;
            /* sort indices by lo (insertion sort on a mostly-sorted list) */
            for (size_t i = 1; i < nf; i++) {
                int k = order[i];
                size_t j = i;
                while (j > 0 && copy[order[j - 1]].lo > copy[k].lo) { order[j] = order[j - 1]; j--; }
                order[j] = k;
            }
            for (size_t i = 0; i < nf; i++) { F[i] = copy[order[i]]; where[order[i]] = (int)i; }
            dline *L = di->lines.data;
            for (size_t i = 0; i < di->lines.n; i++) if (L[i].func >= 0) L[i].func = where[L[i].func];
        } else qsort(F, nf, sizeof(dfunc), dfunc_cmp);   /* out of memory: lines keep stale func indices */
        free(order); free(where); free(copy);
    }
    stabs_finish_funcs(di);
    return 0;
}

/* Fills display names, body addresses and has_lines. */
void stabs_finish_funcs(dbg_info *di)
{
    dfunc *F = di->funcs.data;
    dline *L = di->lines.data;
    char buf[1024];
    for (size_t i = 0; i < di->funcs.n; i++) {
        dfunc *f = &F[i];
        if (f->hi <= f->lo) f->hi = i + 1 < di->funcs.n ? F[i + 1].lo : f->lo + 4;
        const char *dm = dbg_demangle(f->name, buf, sizeof buf);
        f->display = dm ? arena_strdup(&di->mem, dm) : f->name;
        f->base = dm ? func_base(&di->mem, f->display) : f->name;
        f->body = f->lo;
        /* first line entry at lo, the next one at a higher address starts the body */
        size_t lo = 0, hi = di->lines.n;
        while (lo < hi) { size_t m = (lo + hi) / 2; if (L[m].addr < f->lo) lo = m + 1; else hi = m; }
        int seen_first = 0;
        for (size_t k = lo; k < di->lines.n && L[k].addr < f->hi; k++) {
            f->has_lines = 1;
            if (!seen_first) { seen_first = 1; if (L[k].addr != f->lo) { f->body = f->lo; break; } continue; }
            if (L[k].addr > f->lo) { f->body = L[k].addr; break; }
        }
    }
}
