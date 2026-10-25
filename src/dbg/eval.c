/*
 * eval.c — C expression evaluation over the debuggee's memory and registers, and
 * formatting of values into dbg_var records.
 *
 * Supported: identifiers (locals in scope, parameters, C++ members of `this`, file
 * statics, globals, enumerators, functions, .symtab data), . -> [] unary * & - ! ~,
 * casts to known types, sizeof, arithmetic, shifts, bitwise and logical operators,
 * comparisons, $registers ($r0-$r31, $f0-$f31, $pc, $sp, $lr, $ctr, $cr, $xer, $msr),
 * and decimal/hex/octal/char/float literals.
 */
#include "dbg_int.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ---------------- variable storage ---------------- */

typedef struct {
    dtype *type;
    int loc;                 /* LV_* */
    uint32_t addr;
    int reg;
    uint32_t bitpos, bitsize;
    uint8_t val[8];          /* LV_VAL: big-endian bytes */
    int level;
    unsigned gen;
} varpriv;

enum { LV_NONE, LV_MEM, LV_REG, LV_VAL };

struct varstore {
    void **set;              /* open-addressed set of live allocations */
    size_t cap, n;
    arena types;             /* types synthesised during evaluation */
    vec ptrcache;            /* pairs (target, ptr) */
};

varstore *varstore_new(void)
{
    varstore *vs = calloc(1, sizeof *vs);
    if (vs) vec_init(&vs->ptrcache, sizeof(dtype *) * 2);
    return vs;
}

static size_t ptr_hash(const void *p, size_t cap) { return ((uintptr_t)p >> 3) * 2654435761u & (cap - 1); }

static int set_add(varstore *vs, void *p)
{
    if ((vs->n + 1) * 2 > vs->cap) {
        size_t nc = vs->cap ? vs->cap * 2 : 64;
        void **ns = calloc(nc, sizeof(void *));
        if (!ns) return 0;
        for (size_t i = 0; i < vs->cap; i++) if (vs->set[i] && vs->set[i] != (void *)1) {
            size_t h = ptr_hash(vs->set[i], nc);
            while (ns[h]) h = (h + 1) & (nc - 1);
            ns[h] = vs->set[i];
        }
        free(vs->set);
        vs->set = ns; vs->cap = nc;
    }
    size_t h = ptr_hash(p, vs->cap);
    while (vs->set[h] && vs->set[h] != (void *)1) h = (h + 1) & (vs->cap - 1);
    vs->set[h] = p;
    vs->n++;
    return 1;
}

static int set_remove(varstore *vs, void *p)
{
    if (!vs->cap) return 0;
    size_t h = ptr_hash(p, vs->cap);
    for (size_t i = 0; i < vs->cap && vs->set[h]; i++, h = (h + 1) & (vs->cap - 1))
        if (vs->set[h] == p) { vs->set[h] = (void *)1; vs->n--; return 1; }
    return 0;
}

void varstore_free(varstore *vs)
{
    if (!vs) return;
    for (size_t i = 0; i < vs->cap; i++) if (vs->set[i] && vs->set[i] != (void *)1) free(vs->set[i]);
    free(vs->set);
    arena_free(&vs->types);
    vec_free(&vs->ptrcache);
    free(vs);
}

void varstore_release(varstore *vs, dbg_var **vars, int n)
{
    if (!vs || !vars) return;
    int own_array = set_remove(vs, vars);
    for (int i = 0; i < n; i++) if (vars[i] && set_remove(vs, vars[i])) free(vars[i]);
    if (own_array) free(vars);
}

static dbg_var **new_array(varstore *vs, int n)
{
    dbg_var **a = calloc(n ? n : 1, sizeof(dbg_var *));
    if (a && !set_add(vs, a)) { free(a); return NULL; }
    return a;
}

/* One allocation: dbg_var + varpriv + strings. */
static dbg_var *new_var(varstore *vs, const char *name, const char *type, const char *value)
{
    size_t ln = strlen(name) + 1, lt = strlen(type) + 1, lv = strlen(value) + 1;
    char *m = calloc(1, sizeof(dbg_var) + sizeof(varpriv) + ln + lt + lv);
    if (!m) return NULL;
    dbg_var *v = (dbg_var *)m;
    varpriv *p = (varpriv *)(m + sizeof(dbg_var));
    char *s = m + sizeof(dbg_var) + sizeof(varpriv);
    memcpy(s, name, ln); v->name = s; s += ln;
    memcpy(s, type, lt); v->type = s; s += lt;
    memcpy(s, value, lv); v->value = s;
    v->priv = p;
    if (!set_add(vs, v)) { free(m); return NULL; }
    return v;
}

/* ---------------- builtin types ---------------- */

static dtype t_int = { .kind = T_INT, .name = "int", .size = 4, .is_signed = 1 };
static dtype t_uint = { .kind = T_INT, .name = "unsigned int", .size = 4 };
static dtype t_ll = { .kind = T_INT, .name = "long long", .size = 8, .is_signed = 1 };
static dtype t_ull = { .kind = T_INT, .name = "unsigned long long", .size = 8 };
static dtype t_double = { .kind = T_FLOAT, .name = "double", .size = 8, .is_signed = 1 };
static dtype t_char = { .kind = T_INT, .name = "char", .size = 1, .is_char = 1 };
static dtype t_void = { .kind = T_VOID, .name = "void" };

static dtype *ptr_to(varstore *vs, dtype *t)
{
    for (size_t i = 0; i < vs->ptrcache.n; i++) {
        dtype **e = (dtype **)((char *)vs->ptrcache.data + i * vs->ptrcache.esz);
        if (e[0] == t) return e[1];
    }
    dtype *p = arena_alloc(&vs->types, sizeof *p);
    if (!p) return &t_uint;
    p->kind = T_PTR; p->size = 4; p->target = t;
    dtype **e = vec_push(&vs->ptrcache);
    if (e) { e[0] = t; e[1] = p; }
    return p;
}

/* ---------------- values ---------------- */

typedef struct {
    dtype *t;
    int loc;
    uint32_t addr;
    int reg;
    uint32_t bitpos, bitsize;
    uint8_t val[8];
} xval;

typedef struct {
    const eval_env *env;
    varstore *vs;
    const char *p;           /* parse position */
    char err[160];
    int noeval;              /* parsing only (sizeof / short-circuit) */
} ectx;

static int fail(ectx *c, const char *fmt, const char *arg)
{
    if (!c->err[0]) snprintf(c->err, sizeof c->err, fmt, arg ? arg : "");
    return 0;
}

static uint64_t get_be(const uint8_t *b, int n)
{
    uint64_t v = 0;
    for (int i = 0; i < n; i++) v = v << 8 | b[i];
    return v;
}

static void put_be(uint8_t *b, int n, uint64_t v)
{
    for (int i = n - 1; i >= 0; i--) { b[i] = (uint8_t)v; v >>= 8; }
}

static int reg_bytes(const eval_env *env, int reg, uint8_t out[8], int *n)
{
    const dbg_regs *r = env->regs;
    if (reg == REG_PC) { put_be(out, 4, env->pc); *n = 4; return 1; }
    if (reg == REG_GPR + 1 && !r) { put_be(out, 4, env->sp); *n = 4; return 1; }
    if (!r) return 0;
    if (reg >= REG_GPR && reg < REG_GPR + 32) { put_be(out, 4, r->gpr[reg]); *n = 4; return 1; }
    if (reg >= REG_FPR && reg < REG_FPR + 32) {
        uint64_t u; memcpy(&u, &r->fpr[reg - REG_FPR], 8);
        put_be(out, 8, u); *n = 8; return 1;
    }
    uint32_t v;
    switch (reg) {
    case REG_LR: v = r->lr; break;
    case REG_CTR: v = r->ctr; break;
    case REG_CR: v = r->cr; break;
    case REG_XER: v = r->xer; break;
    case REG_MSR: v = r->msr; break;
    default: return 0;
    }
    put_be(out, 4, v); *n = 4;
    return 1;
}

/* Raw bytes of a value (size = type size, at most 8 for scalars). */
static int fetch(ectx *c, const xval *v, uint8_t *buf, uint32_t size)
{
    if (c->noeval && v->loc != LV_VAL) { memset(buf, 0, size > 8 ? 8 : size); return 1; }
    if (v->loc == LV_VAL) { memcpy(buf, v->val, size > 8 ? 8 : size); return 1; }
    if (v->loc == LV_REG) {
        uint8_t r[8]; int n;
        if (!reg_bytes(c->env, v->reg, r, &n)) return fail(c, "register value not available in this frame", NULL);
        dtype *st = type_strip(v->t);
        if (st && st->kind == T_FLOAT && n == 8 && size == 4) {   /* float in an FPR (held as double) */
            double d; uint64_t u = get_be(r, 8); memcpy(&d, &u, 8);
            float f = (float)d; uint32_t fu; memcpy(&fu, &f, 4);
            put_be(buf, 4, fu);
            return 1;
        }
        if ((int)size <= n) memcpy(buf, r + n - size, size);     /* low-order bytes */
        else return fail(c, "value doesn't fit the register", NULL);
        return 1;
    }
    if (v->loc == LV_MEM) {
        if (v->bitsize) {
            uint32_t first = v->bitpos / 8, last = (v->bitpos + v->bitsize + 7) / 8, n = last - first;
            uint8_t b[8];
            if (n > 8 || c->env->mem.read(c->env->mem.ud, v->addr + first, b, n) != (int)n)
                return fail(c, "cannot read memory at 0x%s", NULL);
            uint64_t w = get_be(b, (int)n);
            unsigned shift = n * 8 - (v->bitpos % 8) - v->bitsize;
            uint64_t x = (w >> shift) & ((v->bitsize >= 64) ? ~0ull : ((1ull << v->bitsize) - 1));
            dtype *st = type_strip(v->t);
            if (st && st->is_signed && v->bitsize < 64 && (x >> (v->bitsize - 1)) & 1) x |= ~0ull << v->bitsize;
            put_be(buf, (int)size, x);
            return 1;
        }
        if (c->env->mem.read(c->env->mem.ud, v->addr, buf, size) != (int)size) {
            char a[16]; snprintf(a, sizeof a, "%08lx", (unsigned long)v->addr);
            return fail(c, "cannot read memory at 0x%s", a);
        }
        return 1;
    }
    return fail(c, "no value", NULL);
}

/* Numeric value of a scalar: integer (i, with unsigned flag) or double. */
typedef struct { int is_float; int is_unsigned; int bits; int64_t i; double d; dtype *t; } num;

static int to_num(ectx *c, const xval *v, num *n)
{
    dtype *st = type_strip(v->t);
    memset(n, 0, sizeof *n);
    n->t = v->t;
    if (!st) return fail(c, "unknown type", NULL);
    if (st->kind == T_REF) {
        uint8_t b[4];
        if (!fetch(c, v, b, 4)) return 0;
        xval tv = { st->target, LV_MEM, (uint32_t)get_be(b, 4), 0, 0, 0, {0} };
        return to_num(c, &tv, n);
    }
    if (st->kind == T_ARRAY || st->kind == T_FUNC) {    /* decays to its address */
        if (v->loc != LV_MEM) return fail(c, "array is not in memory", NULL);
        n->i = v->addr; n->is_unsigned = 1; n->bits = 32;
        return 1;
    }
    if (!type_is_scalar(st)) return fail(c, "not a scalar value", NULL);
    uint32_t size = type_size(st);
    if (!size || size > 8) return fail(c, "unsupported scalar size", NULL);
    uint8_t b[8];
    if (!fetch(c, v, b, size)) return 0;
    uint64_t u = get_be(b, (int)size);
    if (st->kind == T_FLOAT) {
        n->is_float = 1;
        if (size == 4) { uint32_t w = (uint32_t)u; float f; memcpy(&f, &w, 4); n->d = f; }
        else { double d; memcpy(&d, &u, 8); n->d = d; }
        return 1;
    }
    n->bits = (int)size * 8;
    n->is_unsigned = !st->is_signed || st->kind == T_PTR || st->kind == T_BOOL;
    if (!n->is_unsigned && size < 8 && (u >> (size * 8 - 1)) & 1) u |= ~0ull << (size * 8);
    n->i = (int64_t)u;
    return 1;
}

static xval mk_int(int64_t i, dtype *t)
{
    xval v;
    memset(&v, 0, sizeof v);
    v.t = t; v.loc = LV_VAL;
    uint32_t sz = type_size(t);
    if (!sz || sz > 8) sz = 4;
    put_be(v.val, (int)sz, (uint64_t)i);
    return v;
}

static xval mk_double(double d)
{
    xval v;
    memset(&v, 0, sizeof v);
    v.t = &t_double; v.loc = LV_VAL;
    uint64_t u; memcpy(&u, &d, 8);
    put_be(v.val, 8, u);
    return v;
}

/* Converts a number to the representation of type t. */
static int convert(ectx *c, const num *n, dtype *t, xval *out)
{
    dtype *st = type_strip(t);
    if (!st) return fail(c, "unknown type", NULL);
    if (st->kind == T_FLOAT) {
        double d = n->is_float ? n->d : n->is_unsigned ? (double)(uint64_t)n->i : (double)n->i;
        memset(out, 0, sizeof *out);
        out->t = t; out->loc = LV_VAL;
        if (type_size(st) == 4) { float f = (float)d; uint32_t w; memcpy(&w, &f, 4); put_be(out->val, 4, w); }
        else { uint64_t u; memcpy(&u, &d, 8); put_be(out->val, 8, u); }
        return 1;
    }
    if (type_is_scalar(st)) {
        int64_t i = n->is_float ? (int64_t)n->d : n->i;
        if (st->kind == T_BOOL) i = n->is_float ? n->d != 0 : n->i != 0;
        *out = mk_int(i, t);
        return 1;
    }
    return fail(c, "cannot convert to a non-scalar type", NULL);
}

/* ---------------- lexer ---------------- */

static void skip_ws(ectx *c) { while (isspace((unsigned char)*c->p)) c->p++; }

static int accept(ectx *c, const char *tok)
{
    skip_ws(c);
    size_t l = strlen(tok);
    if (strncmp(c->p, tok, l)) return 0;
    /* don't split longer operators: "<" must not match "<=" or "<<" etc. */
    char n = c->p[l];
    if (l == 1 && strchr("<>=!&|", tok[0]) && (n == '=' || (n == tok[0] && tok[0] != '!' && tok[0] != '='))) return 0;
    if (l == 1 && tok[0] == '-' && (n == '>' || n == '-')) return 0;
    if (l == 1 && tok[0] == '+' && n == '+') return 0;
    if (l == 2 && (!strcmp(tok, "<<") || !strcmp(tok, ">>")) && n == '=') return 0;
    c->p += l;
    return 1;
}

static int is_ident_start(char ch) { return isalpha((unsigned char)ch) || ch == '_'; }

/* identifier, allowing C++ qualification ("ns::name", "~T") */
static int read_ident(ectx *c, char *buf, size_t n)
{
    skip_ws(c);
    const char *s = c->p;
    if (!is_ident_start(*s) && !(s[0] == ':' && s[1] == ':')) return 0;
    const char *p = s;
    for (;;) {
        if (p[0] == ':' && p[1] == ':' && is_ident_start(p[2])) { p += 2; continue; }
        if (isalnum((unsigned char)*p) || *p == '_') { p++; continue; }
        break;
    }
    size_t l = (size_t)(p - s);
    if (l >= n) l = n - 1;
    memcpy(buf, s, l);
    buf[l] = 0;
    c->p = p;
    return 1;
}

/* ---------------- type names (for casts and sizeof) ---------------- */

static dtype *base_type_named(ectx *c, const char *w)
{
    static const struct { const char *n; dtype *t; } b[] = {
        { "int", &t_int }, { "signed", &t_int }, { "unsigned", &t_uint }, { "char", &t_char },
        { "double", &t_double }, { "void", &t_void },
    };
    dtype *t = info_type_named(c->env->di, w, 0);
    if (t) return t;
    for (size_t i = 0; i < sizeof b / sizeof b[0]; i++) if (!strcmp(b[i].n, w)) return b[i].t;
    return NULL;
}

/* Tries to parse a type name at the current position. Restores the position on failure. */
static dtype *parse_typename(ectx *c)
{
    const char *save = c->p;
    char w[256], full[256] = "";
    int tag = 0;
    dtype *t = NULL;
    if (!read_ident(c, w, sizeof w)) return NULL;
    if (!strcmp(w, "const") || !strcmp(w, "volatile")) {
        if (!read_ident(c, w, sizeof w)) { c->p = save; return NULL; }
    }
    if (!strcmp(w, "struct") || !strcmp(w, "union") || !strcmp(w, "enum") || !strcmp(w, "class")) {
        tag = w[0] == 'u' ? 'u' : w[0] == 'e' ? 'e' : 's';
        if (!read_ident(c, w, sizeof w)) { c->p = save; return NULL; }
        t = info_type_named(c->env->di, w, tag);
        if (!t) { c->p = save; return NULL; }
    } else {
        /* multi-word base types: unsigned int, long long, short int ... */
        static const char *words[] = { "unsigned", "signed", "short", "long", "int", "char", "double", "float", NULL };
        int is_word = 0;
        for (int i = 0; words[i]; i++) if (!strcmp(w, words[i])) is_word = 1;
        if (is_word) {
            strcpy(full, w);
            for (;;) {
                const char *p2 = c->p;
                char w2[64];
                if (!read_ident(c, w2, sizeof w2)) break;
                int ok = 0;
                for (int i = 0; words[i]; i++) if (!strcmp(w2, words[i])) ok = 1;
                if (!ok) { c->p = p2; break; }
                if (strlen(full) + strlen(w2) + 2 < sizeof full) { strcat(full, " "); strcat(full, w2); }
            }
            t = info_type_named(c->env->di, full, 0);
            if (!t) {   /* GCC names: "long int", "short unsigned int", ... */
                if (!strcmp(full, "unsigned") || !strcmp(full, "unsigned int")) t = &t_uint;
                else if (!strcmp(full, "long long") || !strcmp(full, "long long int")) t = &t_ll;
                else if (!strcmp(full, "unsigned long long")) t = &t_ull;
                else if (strstr(full, "char")) t = &t_char;
                else if (!strcmp(full, "double")) t = &t_double;
                else if (!strncmp(full, "unsigned", 8)) t = &t_uint;
                else t = &t_int;
            }
        } else {
            t = base_type_named(c, w);
            if (!t) t = info_type_named(c->env->di, w, 's');   /* C++ class names */
            if (!t) { c->p = save; return NULL; }
        }
    }
    for (;;) {
        skip_ws(c);
        if (*c->p == '*') { c->p++; t = ptr_to(c->vs, t); continue; }
        const char *p2 = c->p;
        char w2[32];
        if (read_ident(c, w2, sizeof w2) && (!strcmp(w2, "const") || !strcmp(w2, "volatile"))) continue;
        c->p = p2;
        break;
    }
    return t;
}

/* ---------------- identifiers ---------------- */

static int var_value(ectx *c, const dvar *dv, xval *out)
{
    const eval_env *env = c->env;
    memset(out, 0, sizeof *out);
    out->t = dv->type;
    switch (dv->cls) {
    case V_LOCAL: case V_PARAM:
        out->loc = LV_MEM; out->addr = env->sp + (uint32_t)dv->off;
        return 1;
    case V_REFPARAM: {
        uint32_t a;
        if (!mem_read32(&env->mem, env->sp + (uint32_t)dv->off, &a)) return fail(c, "cannot read reference parameter", NULL);
        out->loc = LV_MEM; out->addr = a;
        return 1;
    }
    case V_REG: case V_REGPARM:
        if (!env->regs) return fail(c, "%s lives in a register, not available in this frame", dv->name);
        out->loc = LV_REG; out->reg = dv->off;
        return 1;
    case V_STATIC: case V_GLOBAL:
        out->addr = info_var_addr(env->di, dv);
        if (!out->addr) return fail(c, "no address for %s", dv->name);
        out->loc = LV_MEM;
        return 1;
    case V_CONST:
        *out = mk_int(dv->off, dv->type);
        return 1;
    }
    return 0;
}

static const dvar *find_local(const eval_env *env, const char *name)
{
    const dfunc *f = env->func;
    if (!f) return NULL;
    uint32_t off = env->pc - info_l2r(env->di, f->lo);
    for (int i = f->nlocals - 1; i >= 0; i--) {
        const dvar *v = &f->locals[i];
        if (!strcmp(v->name, name) && off >= v->lo && off < (v->hi ? v->hi : 0xffffffffu)) return v;
    }
    for (int i = 0; i < f->nparams; i++) if (!strcmp(f->params[i].name, name)) return &f->params[i];
    return NULL;
}

static int find_enumerator(ectx *c, const char *name, xval *out)
{
    const dbg_info *di = c->env->di;
    for (size_t i = 0; i < di->structs.n; i++) {
        dtype *t = VEC_AT(&di->structs, dtype *, i);
        if (t->kind != T_ENUM) continue;
        for (int k = 0; k < t->nenums; k++)
            if (!strcmp(t->enums[k].name, name)) { *out = mk_int(t->enums[k].value, t); return 1; }
    }
    return 0;
}

static int member(ectx *c, xval *obj, const char *name, xval *out);
static int deref(ectx *c, const xval *v, xval *out);

static int identifier(ectx *c, const char *name, xval *out)
{
    const eval_env *env = c->env;
    const dvar *dv = find_local(env, name);
    if (dv) return var_value(c, dv, out);
    /* C++: members of this */
    const dvar *self = find_local(env, "this");
    if (self) {
        xval th, obj;
        if (var_value(c, self, &th)) {
            num n;
            dtype *pt = type_strip(th.t);
            if (pt && pt->kind == T_PTR && to_num(c, &th, &n)) {
                memset(&obj, 0, sizeof obj);
                obj.t = pt->target; obj.loc = LV_MEM; obj.addr = (uint32_t)n.i;
                char save[sizeof c->err];
                memcpy(save, c->err, sizeof save);
                if (member(c, &obj, name, out)) return 1;
                memcpy(c->err, save, sizeof save);
            }
        }
    }
    int cu = env->func ? env->func->cu : info_cu_at(env->di, env->pc);
    dv = info_global_named(env->di, name, cu);
    if (dv) return var_value(c, dv, out);
    if (find_enumerator(c, name, out)) return 1;
    const dfunc *f = info_func_named(env->di, name);
    if (f) {
        static dtype fn = { .kind = T_FUNC, .target = &t_int };
        memset(out, 0, sizeof *out);
        out->t = &fn; out->loc = LV_MEM; out->addr = info_l2r(env->di, f->lo);
        return 1;
    }
    uint32_t a = info_symbol_addr(env->di, name);
    if (a) {   /* data symbol without debug info: treat as int */
        memset(out, 0, sizeof *out);
        out->t = &t_int; out->loc = LV_MEM; out->addr = a;
        return 1;
    }
    return fail(c, "no symbol \"%.100s\" in current context", name);
}

static int reg_by_name(const char *n)
{
    if ((n[0] == 'r' || n[0] == 'f') && isdigit((unsigned char)n[1])) {
        int i = atoi(n + 1);
        if (i < 0 || i > 31) return -1;
        return (n[0] == 'r' ? REG_GPR : REG_FPR) + i;
    }
    if (!strcmp(n, "sp")) return REG_GPR + 1;
    if (!strcmp(n, "pc") || !strcmp(n, "ip")) return REG_PC;
    if (!strcmp(n, "lr")) return REG_LR;
    if (!strcmp(n, "ctr")) return REG_CTR;
    if (!strcmp(n, "cr")) return REG_CR;
    if (!strcmp(n, "xer")) return REG_XER;
    if (!strcmp(n, "msr")) return REG_MSR;
    return -1;
}

/* ---------------- member access ---------------- */

static int find_field(dtype *st, const char *name, uint32_t base_bits, const dfield **out, uint32_t *bitoff, int depth)
{
    if (!st || depth > 16) return 0;
    for (int i = 0; i < st->nfields; i++) {
        const dfield *f = &st->fields[i];
        if (!f->is_base && f->name && !strcmp(f->name, name)) { *out = f; *bitoff = base_bits; return 1; }
    }
    for (int i = 0; i < st->nfields; i++) {
        const dfield *f = &st->fields[i];
        if (f->is_base && find_field(type_strip(f->type), name, base_bits + f->bitpos, out, bitoff, depth + 1)) return 1;
    }
    return 0;
}

static int field_value(ectx *c, const xval *obj, const dfield *f, uint32_t base_bits, xval *out)
{
    memset(out, 0, sizeof *out);
    out->t = f->type;
    if (f->is_static) {
        uint32_t a = f->physname ? info_symbol_addr(c->env->di, f->physname) : 0;
        if (!a) return fail(c, "static member %s has no address", f->name);
        out->loc = LV_MEM; out->addr = a;
        return 1;
    }
    if (obj->loc != LV_MEM) return fail(c, "structure is not in memory", NULL);
    uint32_t bits = base_bits + f->bitpos;
    uint32_t tsz = type_size(f->type);
    out->loc = LV_MEM;
    if (f->is_base || (bits % 8 == 0 && (f->bitsize == 0 || f->bitsize == tsz * 8 || !type_is_integral(f->type)))) {
        out->addr = obj->addr + bits / 8;
    } else {                                           /* bitfield */
        out->addr = obj->addr;
        out->bitpos = bits; out->bitsize = f->bitsize;
    }
    return 1;
}

static int member(ectx *c, xval *obj, const char *name, xval *out)
{
    dtype *st = type_strip(obj->t);
    if (st && st->kind == T_REF) {
        xval o2;
        if (!deref(c, obj, &o2)) return 0;
        return member(c, &o2, name, out);
    }
    if (!st || (st->kind != T_STRUCT && st->kind != T_UNION)) return fail(c, "request for member %s in something that is not a structure", name);
    const dfield *f;
    uint32_t base;
    if (!find_field(st, name, 0, &f, &base, 0)) {
        char tn[128]; type_name(obj->t, tn, sizeof tn);
        char m[160]; snprintf(m, sizeof m, "there is no member named %.60s in %.60s", name, tn);
        return fail(c, "%s", m);
    }
    return field_value(c, obj, f, base, out);
}

static int deref(ectx *c, const xval *v, xval *out)
{
    dtype *st = type_strip(v->t);
    if (!st) return fail(c, "unknown type", NULL);
    if (st->kind == T_ARRAY) {
        if (v->loc != LV_MEM) return fail(c, "array is not in memory", NULL);
        memset(out, 0, sizeof *out);
        out->t = st->target; out->loc = LV_MEM; out->addr = v->addr;
        return 1;
    }
    if (st->kind != T_PTR && st->kind != T_REF) return fail(c, "attempt to take contents of a non-pointer value", NULL);
    num n;
    xval pv = *v;                      /* read the pointer itself (a reference would read its target) */
    pv.t = ptr_to(c->vs, st->target);
    if (!to_num(c, &pv, &n)) return 0;
    dtype *tg = type_strip(st->target);
    if (tg && tg->kind == T_VOID) return fail(c, "attempt to take contents of a void pointer", NULL);
    memset(out, 0, sizeof *out);
    out->t = st->target; out->loc = LV_MEM; out->addr = (uint32_t)n.i;
    return 1;
}

/* ---------------- parser / evaluator ---------------- */

static int expr(ectx *c, xval *v);
static int unary(ectx *c, xval *v);

static int number(ectx *c, xval *v)
{
    const char *s = c->p;
    char *end;
    int is_float = 0;
    const char *q = s;
    if (q[0] == '0' && (q[1] == 'x' || q[1] == 'X')) q += 2;
    else {
        while (isdigit((unsigned char)*q)) q++;
        if (*q == '.' || *q == 'e' || *q == 'E') is_float = 1;
    }
    if (is_float) {
        double d = strtod(s, &end);
        c->p = end;
        if (*c->p == 'f' || *c->p == 'F') c->p++;
        *v = mk_double(d);
        return 1;
    }
    unsigned long long u = strtoull(s, &end, 0);
    c->p = end;
    int is_u = 0, longs = 0;
    while (*c->p == 'u' || *c->p == 'U' || *c->p == 'l' || *c->p == 'L') {
        if (*c->p == 'u' || *c->p == 'U') is_u = 1; else longs++;
        c->p++;
    }
    dtype *t;
    if (longs >= 2 || u > 0xffffffffull) t = is_u || u > 0x7fffffffffffffffull ? &t_ull : &t_ll;
    else if (is_u || u > 0x7fffffffull) t = &t_uint;
    else t = &t_int;
    *v = mk_int((int64_t)u, t);
    return 1;
}

static int char_literal(ectx *c, xval *v)
{
    const char *p = c->p + 1;
    int ch;
    if (*p == '\\') {
        p++;
        switch (*p) {
        case 'n': ch = '\n'; p++; break;
        case 't': ch = '\t'; p++; break;
        case 'r': ch = '\r'; p++; break;
        case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7':
            ch = (int)strtol(p, (char **)&p, 8); break;
        case 'x': ch = (int)strtol(p + 1, (char **)&p, 16); break;
        default: ch = (unsigned char)*p++; break;
        }
    } else ch = (unsigned char)*p++;
    if (*p != '\'') return fail(c, "unterminated character literal", NULL);
    c->p = p + 1;
    *v = mk_int(ch, &t_char);
    return 1;
}

static int primary(ectx *c, xval *v)
{
    skip_ws(c);
    char ch = *c->p;
    if (ch == '(') {
        c->p++;
        if (!expr(c, v)) return 0;
        if (!accept(c, ")")) return fail(c, "missing ')'", NULL);
        return 1;
    }
    if (isdigit((unsigned char)ch) || (ch == '.' && isdigit((unsigned char)c->p[1]))) return number(c, v);
    if (ch == '\'') return char_literal(c, v);
    if (ch == '$') {
        char n[32];
        c->p++;
        if (!read_ident(c, n, sizeof n)) return fail(c, "bad register name", NULL);
        int r = reg_by_name(n);
        if (r < 0) return fail(c, "unknown register $%s", n);
        memset(v, 0, sizeof *v);
        v->loc = LV_REG; v->reg = r;
        v->t = r >= REG_FPR && r < REG_FPR + 32 ? &t_double : &t_uint;
        if (r == REG_GPR + 1 || r == REG_PC) v->t = ptr_to(c->vs, &t_void);
        if (c->env->level > 0 && r != REG_PC && r != REG_GPR + 1) {   /* only pc/sp known for outer frames */
            if (!c->env->regs) return fail(c, "register $%s not available in this frame", n);
        }
        return 1;
    }
    char name[256];
    if (read_ident(c, name, sizeof name)) {
        if (!strcmp(name, "sizeof")) {
            skip_ws(c);
            uint32_t size = 0;
            const char *save = c->p;
            if (accept(c, "(")) {
                dtype *t = parse_typename(c);
                if (t && accept(c, ")")) { *v = mk_int(type_size(t), &t_uint); return 1; }
                c->p = save;
            }
            xval x;
            c->noeval++;
            int ok = unary(c, &x);
            c->noeval--;
            if (!ok) return 0;
            size = type_size(x.t);
            *v = mk_int(size, &t_uint);
            return 1;
        }
        if (!strcmp(name, "true") || !strcmp(name, "false")) { *v = mk_int(name[0] == 't', &t_int); return 1; }
        if (!strcmp(name, "NULL")) { *v = mk_int(0, ptr_to(c->vs, &t_void)); return 1; }
        return identifier(c, name, v);
    }
    if (!*c->p) return fail(c, "expression expected", NULL);
    char t[2] = { *c->p, 0 };
    return fail(c, "syntax error near '%s'", t);
}

static int postfix(ectx *c, xval *v)
{
    if (!primary(c, v)) return 0;
    for (;;) {
        if (accept(c, "[")) {
            xval idx;
            if (!expr(c, &idx)) return 0;
            if (!accept(c, "]")) return fail(c, "missing ']'", NULL);
            num n;
            if (!to_num(c, &idx, &n) || n.is_float) return fail(c, "array index must be an integer", NULL);
            dtype *st = type_strip(v->t);
            if (!st || (st->kind != T_ARRAY && st->kind != T_PTR)) return fail(c, "cannot subscript something that is not an array or pointer", NULL);
            xval base;
            if (!deref(c, v, &base)) return 0;
            uint32_t es = type_size(base.t);
            if (!es && type_strip(base.t)->kind != T_VOID) es = 1;
            base.addr += (uint32_t)(n.i * (int64_t)es);
            *v = base;
            continue;
        }
        if (accept(c, ".")) {
            char n[256];
            if (!read_ident(c, n, sizeof n)) return fail(c, "member name expected after '.'", NULL);
            xval m;
            if (!member(c, v, n, &m)) return 0;
            *v = m;
            continue;
        }
        if (accept(c, "->")) {
            char n[256];
            if (!read_ident(c, n, sizeof n)) return fail(c, "member name expected after '->'", NULL);
            xval o, m;
            if (!deref(c, v, &o) || !member(c, &o, n, &m)) return 0;
            *v = m;
            continue;
        }
        return 1;
    }
}

static int unary(ectx *c, xval *v)
{
    skip_ws(c);
    if (accept(c, "-")) {
        if (!unary(c, v)) return 0;
        num n;
        if (!to_num(c, v, &n)) return 0;
        if (n.is_float) *v = mk_double(-n.d);
        else *v = mk_int(-n.i, n.bits > 32 ? &t_ll : n.is_unsigned ? &t_uint : &t_int);
        return 1;
    }
    if (accept(c, "+")) return unary(c, v);
    if (accept(c, "!")) {
        if (!unary(c, v)) return 0;
        num n;
        if (!to_num(c, v, &n)) return 0;
        *v = mk_int(n.is_float ? n.d == 0 : n.i == 0, &t_int);
        return 1;
    }
    if (accept(c, "~")) {
        if (!unary(c, v)) return 0;
        num n;
        if (!to_num(c, v, &n) || n.is_float) return fail(c, "~ needs an integer", NULL);
        *v = mk_int(~n.i, n.bits > 32 ? &t_ll : n.is_unsigned ? &t_uint : &t_int);
        return 1;
    }
    if (accept(c, "*")) {
        xval p;
        if (!unary(c, &p)) return 0;
        dtype *st = type_strip(p.t);
        if (st && st->kind == T_FUNC) { *v = p; return 1; }
        return deref(c, &p, v);
    }
    if (accept(c, "&")) {
        xval o;
        if (!unary(c, &o)) return 0;
        if (o.loc != LV_MEM || o.bitsize) return fail(c, "cannot take the address of this value", NULL);
        *v = mk_int(o.addr, ptr_to(c->vs, o.t));
        return 1;
    }
    /* cast: '(' typename ')' */
    skip_ws(c);
    if (*c->p == '(') {
        const char *save = c->p;
        c->p++;
        dtype *t = parse_typename(c);
        if (t && accept(c, ")")) {
            xval x;
            if (!unary(c, &x)) return 0;
            dtype *st = type_strip(t);
            if (st && (st->kind == T_STRUCT || st->kind == T_UNION)) {   /* reinterpret memory */
                if (x.loc != LV_MEM) return fail(c, "cannot cast a value to a structure", NULL);
                x.t = t; *v = x;
                return 1;
            }
            num n;
            if (!to_num(c, &x, &n)) return 0;
            return convert(c, &n, t, v);
        }
        c->p = save;
    }
    return postfix(c, v);
}

/* binary arithmetic with C-like promotion */
static int arith(ectx *c, const char *op, xval *a, xval *b, xval *out)
{
    num x, y;
    if (!to_num(c, a, &x) || !to_num(c, b, &y)) return 0;
    dtype *sa = type_strip(a->t), *sb = type_strip(b->t);
    int pa = sa && (sa->kind == T_PTR || sa->kind == T_ARRAY), pb = sb && (sb->kind == T_PTR || sb->kind == T_ARRAY);
    if ((pa || pb) && (!strcmp(op, "+") || !strcmp(op, "-"))) {
        if (pa && pb && !strcmp(op, "-")) {
            uint32_t es = type_size(sa->target);
            *out = mk_int(((int64_t)(uint32_t)x.i - (int64_t)(uint32_t)y.i) / (es ? es : 1), &t_int);
            return 1;
        }
        if (pa && pb) return fail(c, "cannot add two pointers", NULL);
        dtype *pt = pa ? sa : sb;
        int64_t off = pa ? y.i : x.i;
        uint32_t base = (uint32_t)(pa ? x.i : y.i);
        uint32_t es = type_size(pt->target);
        if (!es) es = 1;
        dtype *rt = pt->kind == T_ARRAY ? ptr_to(c->vs, pt->target) : (pa ? a->t : b->t);
        *out = mk_int((uint32_t)(base + (uint32_t)((op[0] == '-' ? -off : off) * (int64_t)es)), rt);
        return 1;
    }
    int cmp = !strcmp(op, "==") || !strcmp(op, "!=") || !strcmp(op, "<") || !strcmp(op, ">") || !strcmp(op, "<=") || !strcmp(op, ">=");
    if (x.is_float || y.is_float) {
        double p = x.is_float ? x.d : x.is_unsigned ? (double)(uint64_t)x.i : (double)x.i;
        double q = y.is_float ? y.d : y.is_unsigned ? (double)(uint64_t)y.i : (double)y.i;
        double r;
        if (cmp) {
            int t = op[0] == '=' ? p == q : op[0] == '!' ? p != q : op[0] == '<' ? (op[1] ? p <= q : p < q) : (op[1] ? p >= q : p > q);
            *out = mk_int(t, &t_int);
            return 1;
        }
        switch (op[0]) {
        case '+': r = p + q; break;
        case '-': r = p - q; break;
        case '*': r = p * q; break;
        case '/': if (q == 0 && !c->noeval) return fail(c, "division by zero", NULL); r = q != 0 ? p / q : 0; break;
        default: return fail(c, "invalid operands to %s", op);
        }
        *out = mk_double(r);
        return 1;
    }
    int bits = x.bits > y.bits ? x.bits : y.bits;
    if (bits < 32) bits = 32;
    int uns = (x.is_unsigned && x.bits >= bits) || (y.is_unsigned && y.bits >= bits);
    dtype *rt = bits > 32 ? (uns ? &t_ull : &t_ll) : (uns ? &t_uint : &t_int);
    uint64_t ux = (uint64_t)x.i, uy = (uint64_t)y.i;
    int64_t sx = x.i, sy = y.i;
    if (bits == 32) {
        ux &= 0xffffffffu; uy &= 0xffffffffu;
        sx = (int32_t)sx; sy = (int32_t)sy;
    }
    if (cmp) {
        int t;
        if (op[0] == '=') t = ux == uy;
        else if (op[0] == '!') t = ux != uy;
        else if (uns) t = op[0] == '<' ? (op[1] ? ux <= uy : ux < uy) : (op[1] ? ux >= uy : ux > uy);
        else t = op[0] == '<' ? (op[1] ? sx <= sy : sx < sy) : (op[1] ? sx >= sy : sx > sy);
        *out = mk_int(t, &t_int);
        return 1;
    }
    int64_t r;
    if (!strcmp(op, "+")) r = (int64_t)(ux + uy);
    else if (!strcmp(op, "-")) r = (int64_t)(ux - uy);
    else if (!strcmp(op, "*")) r = (int64_t)(ux * uy);
    else if (!strcmp(op, "/") || !strcmp(op, "%")) {
        if (!uy && c->noeval) { *out = mk_int(0, rt); return 1; }
        if (!uy) return fail(c, "division by zero", NULL);
        if (uns) r = op[0] == '/' ? (int64_t)(ux / uy) : (int64_t)(ux % uy);
        else r = op[0] == '/' ? sx / sy : sx % sy;
    }
    else if (!strcmp(op, "<<")) r = (int64_t)(ux << (uy & 63));
    else if (!strcmp(op, ">>")) r = uns ? (int64_t)(ux >> (uy & 63)) : sx >> (uy & 63);
    else if (!strcmp(op, "&")) r = (int64_t)(ux & uy);
    else if (!strcmp(op, "|")) r = (int64_t)(ux | uy);
    else if (!strcmp(op, "^")) r = (int64_t)(ux ^ uy);
    else return fail(c, "unknown operator %s", op);
    *out = mk_int(r, rt);
    return 1;
}

typedef struct { const char *ops[6]; } level_ops;

static const level_ops levels[] = {
    { { "||" } }, { { "&&" } }, { { "|" } }, { { "^" } }, { { "&" } },
    { { "==", "!=" } }, { { "<=", ">=", "<", ">" } }, { { "<<", ">>" } },
    { { "+", "-" } }, { { "*", "/", "%" } },
};
#define NLEVELS (int)(sizeof levels / sizeof levels[0])

static int binary(ectx *c, xval *v, int lvl)
{
    if (lvl >= NLEVELS) return unary(c, v);
    if (!binary(c, v, lvl + 1)) return 0;
    for (;;) {
        const char *op = NULL;
        for (int i = 0; i < 6 && levels[lvl].ops[i]; i++)
            if (accept(c, levels[lvl].ops[i])) { op = levels[lvl].ops[i]; break; }
        if (!op) return 1;
        if (lvl <= 1) {                                    /* && || short-circuit */
            num a;
            if (!to_num(c, v, &a)) return 0;
            int av = a.is_float ? a.d != 0 : a.i != 0;
            int skip = (op[0] == '|' && av) || (op[0] == '&' && !av);
            xval b;
            if (skip) c->noeval++;
            int ok = binary(c, &b, lvl + 1);
            if (skip) { c->noeval--; if (!ok) return 0; *v = mk_int(av, &t_int); continue; }
            if (!ok) return 0;
            num bn;
            if (!to_num(c, &b, &bn)) return 0;
            *v = mk_int(bn.is_float ? bn.d != 0 : bn.i != 0, &t_int);
            continue;
        }
        xval b, r;
        if (!binary(c, &b, lvl + 1) || !arith(c, op, v, &b, &r)) return 0;
        *v = r;
    }
}

static int expr(ectx *c, xval *v) { return binary(c, v, 0); }

static int parse_full(ectx *c, const char *text, xval *v)
{
    c->p = text;
    c->err[0] = 0;
    if (!expr(c, v)) return 0;
    skip_ws(c);
    if (*c->p) {
        char t[32]; snprintf(t, sizeof t, "%.20s", c->p);
        return fail(c, "unexpected text: %s", t);
    }
    return 1;
}

/* ---------------- formatting ---------------- */

static void escape_char(int ch, char *out)
{
    switch (ch) {
    case '\n': strcpy(out, "\\n"); return;
    case '\t': strcpy(out, "\\t"); return;
    case '\r': strcpy(out, "\\r"); return;
    case '\0': strcpy(out, "\\0"); return;
    case '"': strcpy(out, "\\\""); return;
    case '\\': strcpy(out, "\\\\"); return;
    case '\'': strcpy(out, "\\'"); return;
    }
    if (ch < 32 || ch >= 127) sprintf(out, "\\%03o", ch & 0xff);
    else { out[0] = (char)ch; out[1] = 0; }
}

/* Quoted string from target memory (stops at NUL or max). 0 if unreadable. */
static int read_cstring(const eval_env *env, uint32_t addr, uint32_t max, int stop_at_nul, char *out, size_t n)
{
    size_t o = 0;
    out[o++] = '"';
    uint8_t buf[64];
    uint32_t done = 0;
    int truncated = 0;
    while (done < max) {
        uint32_t chunk = max - done < sizeof buf ? max - done : (uint32_t)sizeof buf;
        /* don't cross a page boundary in one read: the rest may be unmapped */
        uint32_t to_page = 4096 - ((addr + done) & 4095);
        if (chunk > to_page) chunk = to_page;
        int got = env->mem.read(env->mem.ud, addr + done, buf, chunk);
        if (got <= 0) { if (!done) return 0; break; }
        int end = 0;
        for (int i = 0; i < got; i++) {
            if (stop_at_nul && !buf[i]) { end = 1; break; }
            char e[8];
            escape_char(buf[i], e);
            size_t el = strlen(e);
            if (o + el + 6 >= n) { truncated = 1; end = 1; break; }
            memcpy(out + o, e, el); o += el;
        }
        done += (uint32_t)got;
        if (end || got < (int)chunk) break;
    }
    if (done >= max && stop_at_nul && max >= 200) truncated = 1;
    out[o++] = '"';
    if (truncated) { memcpy(out + o, "...", 3); o += 3; }
    out[o] = 0;
    return 1;
}

static int is_std_string(dtype *st, const dfield **dp, const dfield **len)
{
    if (!st || st->kind != T_STRUCT || !st->name || !strstr(st->name, "basic_string")) return 0;
    uint32_t off;
    if (!find_field(st, "_M_dataplus", 0, dp, &off, 0)) return 0;
    if (!find_field(st, "_M_string_length", 0, len, &off, 0)) return 0;
    return 1;
}

static void format_value(ectx *c, const xval *v, char *out, size_t n, int depth);

static void format_array_summary(ectx *c, const xval *v, dtype *st, char *out, size_t n, int depth)
{
    dtype *et = type_strip(st->target);
    int count = st->hi >= st->lo ? st->hi - st->lo + 1 : 0;
    if (et && et->kind == T_INT && et->is_char && type_size(et) == 1) {
        if (v->loc != LV_MEM || !read_cstring(c->env, v->addr, (uint32_t)count, 1, out, n)) snprintf(out, n, "<unreadable>");
        return;
    }
    if (depth > 0 || !et || !type_is_scalar(et) || count > 16) { snprintf(out, n, "[%d]", count); return; }
    size_t o = 0;
    out[o++] = '{';
    uint32_t es = type_size(et);
    for (int i = 0; i < count && o + 32 < n; i++) {
        xval e = *v;
        e.t = st->target; e.addr = v->addr + (uint32_t)i * es;
        char tmp[64];
        format_value(c, &e, tmp, sizeof tmp, depth + 1);
        o += (size_t)snprintf(out + o, n - o, "%s%s", i ? ", " : "", tmp);
    }
    if (o + 2 < n) { out[o++] = '}'; out[o] = 0; }
}

static void format_value(ectx *c, const xval *v, char *out, size_t n, int depth)
{
    dtype *st = type_strip(v->t);
    c->err[0] = 0;
    if (!st || st->kind == T_UNKNOWN) { snprintf(out, n, "<unknown type>"); return; }
    if (st->kind == T_XREF) { snprintf(out, n, "<incomplete type>"); return; }
    switch (st->kind) {
    case T_VOID: snprintf(out, n, "void"); return;
    case T_STRUCT: case T_UNION: {
        const dfield *dp, *len;
        if (is_std_string(st, &dp, &len) && v->loc == LV_MEM) {
            xval dpv, pv, lv;
            num pn, ln;
            const dfield *pf;
            uint32_t off;
            if (field_value(c, v, dp, 0, &dpv) && find_field(type_strip(dp->type), "_M_p", 0, &pf, &off, 0) &&
                field_value(c, &dpv, pf, off, &pv) && to_num(c, &pv, &pn) && field_value(c, v, len, 0, &lv) && to_num(c, &lv, &ln)) {
                uint32_t l = (uint32_t)ln.i > 256 ? 256 : (uint32_t)ln.i;
                if (read_cstring(c->env, (uint32_t)pn.i, l, 0, out, n)) return;
            }
        }
        snprintf(out, n, "{...}");
        return;
    }
    case T_ARRAY: format_array_summary(c, v, st, out, n, depth); return;
    case T_FUNC: {
        char sym[128] = "";
        info_symbolize(c->env->di, v->addr, sym, sizeof sym);
        snprintf(out, n, "0x%08lx <%s>", (unsigned long)v->addr, sym[0] ? sym : "?");
        return;
    }
    case T_REF: {
        xval tv;
        if (!deref(c, v, &tv)) { snprintf(out, n, "<unreadable>"); return; }
        char inner[200];
        format_value(c, &tv, inner, sizeof inner, depth + 1);
        snprintf(out, n, "@0x%08lx: %s", (unsigned long)tv.addr, inner);
        return;
    }
    default: break;
    }
    num x;
    if (!to_num(c, v, &x)) { snprintf(out, n, v->loc == LV_REG ? "<unavailable>" : "<unreadable>"); return; }
    if (x.is_float) {
        snprintf(out, n, type_size(st) == 4 ? "%.9g" : "%.17g", x.d);
        return;
    }
    switch (st->kind) {
    case T_BOOL: snprintf(out, n, "%s", x.i ? "true" : "false"); return;
    case T_ENUM:
        for (int i = 0; i < st->nenums; i++)
            if (st->enums[i].value == (int32_t)x.i) { snprintf(out, n, "%s", st->enums[i].name); return; }
        snprintf(out, n, "%ld", (long)x.i);
        return;
    case T_PTR: {
        uint32_t p = (uint32_t)x.i;
        dtype *tg = type_strip(st->target);
        if (tg && tg->kind == T_INT && tg->is_char && type_size(tg) == 1 && p) {
            char s[300];
            if (read_cstring(c->env, p, 200, 1, s, sizeof s)) snprintf(out, n, "0x%08lx %s", (unsigned long)p, s);
            else snprintf(out, n, "0x%08lx <unreadable>", (unsigned long)p);
            return;
        }
        if (tg && tg->kind == T_FUNC && p) {
            char sym[128] = "";
            if (info_symbolize(c->env->di, p, sym, sizeof sym)) { snprintf(out, n, "0x%08lx <%s>", (unsigned long)p, sym); return; }
        }
        snprintf(out, n, "0x%08lx", (unsigned long)p);
        return;
    }
    case T_MEMBERPTR: snprintf(out, n, "0x%08lx", (unsigned long)(uint32_t)x.i); return;
    default: break;
    }
    if (st->is_char && type_size(st) == 1) {
        char e[8];
        escape_char((int)(x.i & 0xff), e);
        snprintf(out, n, "%ld '%s'", (long)(st->is_signed ? (int8_t)x.i : (uint8_t)x.i), e);
        return;
    }
    if (x.is_unsigned) snprintf(out, n, "%llu", (unsigned long long)(x.bits >= 64 ? (uint64_t)x.i : (uint64_t)x.i & ((1ull << x.bits) - 1)));
    else snprintf(out, n, "%lld", (long long)x.i);
}

static int has_children(ectx *c, const xval *v)
{
    dtype *st = type_strip(v->t);
    if (!st) return 0;
    switch (st->kind) {
    case T_STRUCT: case T_UNION: return st->nfields > 0;
    case T_ARRAY: return st->hi >= st->lo;
    case T_REF: {
        dtype *tg = type_strip(st->target);
        return tg && (tg->kind == T_STRUCT || tg->kind == T_UNION || tg->kind == T_ARRAY);
    }
    case T_PTR: {
        dtype *tg = type_strip(st->target);
        if (!tg || tg->kind == T_VOID || tg->kind == T_FUNC || tg->kind == T_UNKNOWN) return 0;
        if (tg->kind == T_INT && tg->is_char && type_size(tg) == 1) return 0;
        num n;
        char save[sizeof c->err];
        memcpy(save, c->err, sizeof save);
        int ok = to_num(c, v, &n) && n.i != 0;
        memcpy(c->err, save, sizeof save);
        return ok;
    }
    default: return 0;
    }
}

static dbg_var *make_var(ectx *c, const char *name, const xval *v)
{
    char tn[256], val[512];
    type_name(v->t, tn, sizeof tn);
    format_value(c, v, val, sizeof val, 0);
    dbg_var *dv = new_var(c->vs, name, tn, val);
    if (!dv) return NULL;
    varpriv *p = dv->priv;
    p->type = v->t; p->loc = v->loc; p->addr = v->addr; p->reg = v->reg;
    p->bitpos = v->bitpos; p->bitsize = v->bitsize;
    memcpy(p->val, v->val, 8);
    p->level = c->env->level; p->gen = c->env->generation;
    dv->addr = v->loc == LV_MEM ? v->addr + (v->bitsize ? v->bitpos / 8 : 0) : 0;
    dv->size = type_size(v->t);
    dv->has_children = has_children(c, v);
    dv->editable = (v->loc == LV_MEM || v->loc == LV_REG) && type_is_scalar(v->t) && type_strip(v->t)->kind != T_REF;
    return dv;
}

dbg_var *eval_error(varstore *vs, const char *name, const char *msg)
{
    char val[200];
    snprintf(val, sizeof val, "<error: %s>", msg);
    return new_var(vs, name, "", val);
}

static void xval_from_var(const dbg_var *dv, xval *v)
{
    const varpriv *p = dv->priv;
    memset(v, 0, sizeof *v);
    v->t = p->type; v->loc = p->loc; v->addr = p->addr; v->reg = p->reg;
    v->bitpos = p->bitpos; v->bitsize = p->bitsize;
    memcpy(v->val, p->val, 8);
}

/* ---------------- public ---------------- */

dbg_var *eval_expr(varstore *vs, const eval_env *env, const char *text)
{
    ectx c;
    memset(&c, 0, sizeof c);
    c.env = env; c.vs = vs;
    xval v;
    if (!text || !*text) return eval_error(vs, "", "empty expression");
    if (!parse_full(&c, text, &v)) return eval_error(vs, text, c.err[0] ? c.err : "syntax error");
    dbg_var *dv = make_var(&c, text, &v);
    return dv ? dv : eval_error(vs, text, "out of memory");
}

int eval_condition(const eval_env *env, const char *text, char *err, size_t errlen)
{
    ectx c;
    memset(&c, 0, sizeof c);
    c.env = env;
    varstore *vs = varstore_new();
    if (!vs) { snprintf(err, errlen, "out of memory"); return -1; }
    c.vs = vs;
    xval v;
    num n;
    int r = -1;
    if (parse_full(&c, text, &v) && to_num(&c, &v, &n)) r = n.is_float ? n.d != 0 : n.i != 0;
    else snprintf(err, errlen, "%s", c.err[0] ? c.err : "bad condition");
    varstore_free(vs);
    return r;
}

static int scope_contains(const dvar *v, uint32_t off) { return off >= v->lo && off < (v->hi ? v->hi : 0xffffffffu); }

int eval_locals(varstore *vs, const eval_env *env, dbg_var ***out)
{
    *out = NULL;
    const dfunc *f = env->func;
    if (!f) return 0;
    ectx c;
    memset(&c, 0, sizeof c);
    c.env = env; c.vs = vs;
    uint32_t off = env->pc - info_l2r(env->di, f->lo);
    int n = 0;
    dbg_var **arr = new_array(vs, f->nparams + f->nlocals);
    if (!arr) return 0;
    for (int i = 0; i < f->nparams; i++) {
        xval v;
        c.err[0] = 0;
        arr[n] = var_value(&c, &f->params[i], &v) ? make_var(&c, f->params[i].name, &v) : eval_error(vs, f->params[i].name, c.err);
        if (arr[n]) n++;
    }
    for (int i = 0; i < f->nlocals; i++) {
        const dvar *lv = &f->locals[i];
        if (!scope_contains(lv, off)) continue;
        xval v;
        c.err[0] = 0;
        arr[n] = var_value(&c, lv, &v) ? make_var(&c, lv->name, &v) : eval_error(vs, lv->name, c.err);
        if (arr[n]) n++;
    }
    *out = arr;
    return n;
}

int eval_globals(varstore *vs, const eval_env *env, int cu, dbg_var ***out)
{
    *out = NULL;
    const dbg_info *di = env->di;
    ectx c;
    memset(&c, 0, sizeof c);
    c.env = env; c.vs = vs;
    int cnt = 0;
    for (size_t i = 0; i < di->globals.n; i++) if (cu < 0 || VEC_AT(&di->globals, dvar, i).cu == cu) cnt++;
    dbg_var **arr = new_array(vs, cnt);
    if (!arr) return 0;
    int n = 0;
    for (size_t i = 0; i < di->globals.n; i++) {
        const dvar *g = &VEC_AT(&di->globals, dvar, i);
        if (cu >= 0 && g->cu != cu) continue;
        xval v;
        c.err[0] = 0;
        arr[n] = var_value(&c, g, &v) ? make_var(&c, g->name, &v) : eval_error(vs, g->name, c.err);
        if (arr[n]) n++;
    }
    *out = arr;
    return n;
}

#define MAX_ARRAY_CHILDREN 1000

static int struct_children(ectx *c, const xval *obj, dtype *st, dbg_var **arr, int max)
{
    int n = 0;
    for (int i = 0; i < st->nfields && n < max; i++) {
        const dfield *f = &st->fields[i];
        xval fv;
        c->err[0] = 0;
        char nm[160];
        if (f->is_base) {
            type_name(f->type, nm, sizeof nm);
        } else snprintf(nm, sizeof nm, "%s", f->name ? f->name : "?");
        if (f->name && !strncmp(f->name, "_vptr", 5)) continue;
        if (f->is_static && !f->physname) continue;
        arr[n] = field_value(c, obj, f, 0, &fv) ? make_var(c, nm, &fv) : eval_error(c->vs, nm, c->err);
        if (arr[n]) n++;
    }
    return n;
}

int eval_children(varstore *vs, const eval_env *env, dbg_var *dv, dbg_var ***out)
{
    *out = NULL;
    if (!dv || !dv->priv || !dv->has_children) return 0;
    ectx c;
    memset(&c, 0, sizeof c);
    c.env = env; c.vs = vs;
    xval v;
    xval_from_var(dv, &v);
    const varpriv *p = dv->priv;
    if (v.loc == LV_REG && p->gen != env->generation) return 0;   /* stale register value */
    dtype *st = type_strip(v.t);
    if (st->kind == T_REF || st->kind == T_PTR) {
        xval tv;
        if (!deref(&c, &v, &tv)) return 0;
        dtype *tg = type_strip(tv.t);
        if (tg && (tg->kind == T_STRUCT || tg->kind == T_UNION)) {
            dbg_var **arr = new_array(vs, tg->nfields);
            if (!arr) return 0;
            int n = struct_children(&c, &tv, tg, arr, tg->nfields);
            *out = arr;
            return n;
        }
        if (st->kind == T_REF && tg && tg->kind == T_ARRAY) { v = tv; st = tg; }
        else {
            dbg_var **arr = new_array(vs, 1);
            if (!arr) return 0;
            char nm[200];
            snprintf(nm, sizeof nm, "*%s", dv->name);
            arr[0] = make_var(&c, nm, &tv);
            *out = arr;
            return arr[0] ? 1 : 0;
        }
    }
    if (st->kind == T_STRUCT || st->kind == T_UNION) {
        dbg_var **arr = new_array(vs, st->nfields);
        if (!arr) return 0;
        int n = struct_children(&c, &v, st, arr, st->nfields);
        *out = arr;
        return n;
    }
    if (st->kind == T_ARRAY) {
        int count = st->hi >= st->lo ? st->hi - st->lo + 1 : 0;
        if (count > MAX_ARRAY_CHILDREN) count = MAX_ARRAY_CHILDREN;
        dbg_var **arr = new_array(vs, count);
        if (!arr) return 0;
        uint32_t es = type_size(st->target);
        int n = 0;
        for (int i = 0; i < count; i++) {
            xval e = v;
            e.t = st->target; e.addr = v.addr + (uint32_t)i * es;
            char nm[24];
            snprintf(nm, sizeof nm, "[%d]", i + st->lo);
            arr[n] = make_var(&c, nm, &e);
            if (arr[n]) n++;
        }
        *out = arr;
        return n;
    }
    return 0;
}

int eval_assign(varstore *vs, const eval_env *env, dbg_var *dv, const char *text, char *err, size_t errlen)
{
    if (!dv || !dv->priv || !dv->editable) { snprintf(err, errlen, "value is not editable"); return 0; }
    ectx c;
    memset(&c, 0, sizeof c);
    c.env = env; c.vs = vs;
    xval target, src, conv;
    xval_from_var(dv, &target);
    const varpriv *p = dv->priv;
    if (target.loc == LV_REG && (p->gen != env->generation || p->level != 0)) {
        snprintf(err, errlen, "register variables can only be set in the innermost frame");
        return 0;
    }
    num n;
    if (!parse_full(&c, text, &src) || !to_num(&c, &src, &n) || !convert(&c, &n, target.t, &conv)) {
        snprintf(err, errlen, "%s", c.err[0] ? c.err : "bad value");
        return 0;
    }
    uint32_t size = type_size(target.t);
    if (target.loc == LV_REG) {
        if (!env->mem.set_reg || !env->mem.set_reg(env->mem.ud, target.reg, conv.val, (int)size)) {
            snprintf(err, errlen, "cannot write register");
            return 0;
        }
    } else if (target.bitsize) {
        uint32_t first = target.bitpos / 8, last = (target.bitpos + target.bitsize + 7) / 8, nb = last - first;
        uint8_t b[8];
        if (nb > 8 || env->mem.read(env->mem.ud, target.addr + first, b, nb) != (int)nb) { snprintf(err, errlen, "cannot read memory"); return 0; }
        uint64_t w = get_be(b, (int)nb), mask = ((1ull << target.bitsize) - 1);
        unsigned shift = nb * 8 - (target.bitpos % 8) - target.bitsize;
        w = (w & ~(mask << shift)) | (((uint64_t)n.i & mask) << shift);
        put_be(b, (int)nb, w);
        if (env->mem.write(env->mem.ud, target.addr + first, b, nb) != (int)nb) { snprintf(err, errlen, "cannot write memory"); return 0; }
    } else if (env->mem.write(env->mem.ud, target.addr, conv.val, size) != (int)size) {
        snprintf(err, errlen, "cannot write memory at 0x%08lx", (unsigned long)target.addr);
        return 0;
    }
    return 1;
}
