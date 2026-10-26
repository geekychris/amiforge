/*
 * dbg_hosttest — host unit tests for the portable debugger core.
 *   dbg_hosttest <targets dir> <dumps dir> <target sources dir>
 * Checks the stabs line table and functions against objdump -G / readelf -s, the PPC
 * branch decoder against every branch in objdump -d, and the expression evaluator and
 * value formatting over a fake memory image (the ELF's sections + a synthetic stack).
 */
#include "dbg/dbg_int.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int g_pass, g_fail;
static const char *g_targets, *g_dumps, *g_srcdir;

#define CHECK(cond, ...) do { if (cond) g_pass++; else { g_fail++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static char *path(const char *dir, const char *name, const char *ext)
{
    static char buf[4][512];
    static int k;
    char *b = buf[k++ & 3];
    snprintf(b, 512, "%s/%s%s", dir, name, ext);
    return b;
}

/* Line number of the "@name" marker in a target source. */
static int marker(const char *src, const char *name)
{
    FILE *f = fopen(path(g_srcdir, src, ""), "r");
    char line[512], pat[64];
    int n = 0;
    snprintf(pat, sizeof pat, "@%s", name);
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        n++;
        char *m = strstr(line, pat);
        if (m && !isalnum((unsigned char)m[strlen(pat)]) && m[strlen(pat)] != '_') { fclose(f); return n; }
    }
    fclose(f);
    return 0;
}

/* ------------------------------------------------------------------ line table */

typedef struct { uint32_t addr; int line; char file[160]; } refline;

static int ends_with(const char *s, const char *suffix)
{
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && !strcmp(s + a - b, suffix);
}

static void test_lines(const char *target)
{
    char err[128];
    dbg_info *di = info_load(path(g_targets, target, ""), err, sizeof err);
    CHECK(di != NULL, "%s: info_load: %s", target, err);
    if (!di) return;
    FILE *f = fopen(path(g_dumps, target, ".G"), "r");
    CHECK(f != NULL, "%s: no objdump -G dump", target);
    if (!f) { info_free(di); return; }
    refline *ref = NULL;
    int nref = 0, cap = 0, in_func = 0;
    uint32_t func_lo = 0;
    char file[160] = "", line[2048];
    while (fgets(line, sizeof line, f)) {
        char type[16], str[1024] = "";
        int idx, othr, desc;
        unsigned value, strx;
        int n = sscanf(line, "%d %15s %d %d %x %u %1023[^\n]", &idx, type, &othr, &desc, &value, &strx, str);
        if (n < 6) continue;
        char *e = str + strlen(str);
        while (e > str && isspace((unsigned char)e[-1])) *--e = 0;
        if (!strcmp(type, "SO") || !strcmp(type, "SOL")) {
            if (*str && !ends_with(str, "/")) snprintf(file, sizeof file, "%s", str);
            if (!*str) in_func = 0;
        } else if (!strcmp(type, "FUN")) {
            if (*str) { in_func = 1; func_lo = value; } else in_func = 0;
        } else if (!strcmp(type, "SLINE")) {
            if (nref == cap) { cap = cap ? cap * 2 : 256; ref = realloc(ref, sizeof(refline) * cap); }
            ref[nref].addr = in_func ? func_lo + value : value;
            ref[nref].line = desc;
            snprintf(ref[nref].file, sizeof ref[nref].file, "%s", file);
            nref++;
        }
    }
    fclose(f);
    CHECK((int)di->lines.n == nref, "%s: %d line entries, objdump has %d", target, (int)di->lines.n, nref);
    int bad = 0;
    for (int i = 0; i < nref; i++) {
        int found = 0;
        for (size_t k = 0; k < di->lines.n && !found; k++) {
            const dline *l = &VEC_AT(&di->lines, dline, k);
            if (l->addr == ref[i].addr && l->line == ref[i].line && !strcmp(info_file_name(di, l->file), ref[i].file)) found = 1;
        }
        if (!found && bad++ < 5) printf("  %s: missing line %s:%d at %08x\n", target, ref[i].file, ref[i].line, ref[i].addr);
    }
    CHECK(bad == 0, "%s: %d line entries differ from objdump -G", target, bad);
    for (size_t k = 1; k < di->lines.n; k++)
        if (VEC_AT(&di->lines, dline, k - 1).addr > VEC_AT(&di->lines, dline, k).addr) { CHECK(0, "%s: line table not sorted", target); break; }
    free(ref);

    /* functions: start and size match .symtab */
    f = fopen(path(g_dumps, target, ".syms"), "r");
    int nfunc = 0, fbad = 0;
    while (f && fgets(line, sizeof line, f)) {
        unsigned num, value, size;
        char type[16], bind[16], vis[16], ndx[16], name[512];
        if (sscanf(line, " %u: %x %u %15s %15s %15s %15s %511s", &num, &value, &size, type, bind, vis, ndx, name) != 8) continue;
        if (strcmp(type, "FUNC")) continue;
        for (size_t k = 0; k < di->funcs.n; k++) {
            const dfunc *fn = &VEC_AT(&di->funcs, dfunc, k);
            if (strcmp(fn->name, name)) continue;
            nfunc++;
            if (fn->lo != value || fn->hi - fn->lo != size) {
                if (fbad++ < 5) printf("  %s: %s at %08x+%u, symtab %08x+%u\n", target, name, fn->lo, fn->hi - fn->lo, value, size);
            }
        }
    }
    if (f) fclose(f);
    CHECK(nfunc == (int)di->funcs.n, "%s: %d of %d stabs functions found in .symtab", target, nfunc, (int)di->funcs.n);
    CHECK(fbad == 0, "%s: %d functions differ from .symtab", target, fbad);
    info_free(di);
}

/* ------------------------------------------------------------------ decoder */

static void test_decoder_dump(const char *target)
{
    FILE *f = fopen(path(g_dumps, target, ".d"), "r");
    CHECK(f != NULL, "%s: no objdump -d dump", target);
    if (!f) return;
    char line[512];
    int nbr = 0, bad = 0;
    while (fgets(line, sizeof line, f)) {
        unsigned addr, b0, b1, b2, b3;
        char mn[32] = "", ops[256] = "";
        if (sscanf(line, " %x:\t%x %x %x %x \t%31s %255[^\n]", &addr, &b0, &b1, &b2, &b3, mn, ops) < 6) continue;
        uint32_t insn = b0 << 24 | b1 << 16 | b2 << 8 | b3;
        if (mn[0] != 'b') continue;
        ppc_branch b;
        ppc_decode(insn, addr, &b);
        size_t ml = strlen(mn);
        int link = 0;
        char base[32];
        snprintf(base, sizeof base, "%s", mn);
        if (base[ml - 1] == '+' || base[ml - 1] == '-') base[--ml] = 0;   /* static prediction hint */
        if (!strcmp(base, "blr") || !strcmp(base, "blrl") || ends_with(base, "lr") || ends_with(base, "lrl")) {
            nbr++;
            if (b.kind != PPC_BCLR) { bad++; printf("  %08x %s: kind %d, want bclr\n", addr, mn, b.kind); }
            link = ends_with(base, "lrl");
            if (b.link != link) { bad++; printf("  %08x %s: link %d\n", addr, mn, b.link); }
            if (!strcmp(base, "blr") && !b.always) { bad++; printf("  %08x blr not unconditional\n", addr); }
            continue;
        }
        if (ends_with(base, "ctr") || ends_with(base, "ctrl")) {
            nbr++;
            if (b.kind != PPC_BCCTR) { bad++; printf("  %08x %s: kind %d, want bcctr\n", addr, mn, b.kind); }
            if (b.link != ends_with(base, "ctrl")) { bad++; printf("  %08x %s: link %d\n", addr, mn, b.link); }
            continue;
        }
        /* relative branches print their target: "... 1000554 <add>" or "cr7,10006f0 <...>" */
        char *lt = strstr(ops, " <");
        if (!lt) continue;
        char *s = lt;
        while (s > ops && isxdigit((unsigned char)s[-1])) s--;
        unsigned target_addr;
        if (sscanf(s, "%x", &target_addr) != 1) continue;
        nbr++;
        if ((b.kind != PPC_B && b.kind != PPC_BC) || b.target != target_addr) {
            bad++;
            printf("  %08x %s %s: decoded kind %d target %08x\n", addr, mn, ops, b.kind, b.target);
        }
        int want_link = ends_with(base, "l") && strcmp(base, "bdnzl") && strcmp(base, "bl") ? 1 : !strcmp(base, "bl");
        if (!strcmp(base, "b") || !strcmp(base, "ble") || !strcmp(base, "bnl")) want_link = 0;
        if (!strcmp(base, "bl") && !b.link) { bad++; printf("  %08x bl without link\n", addr); }
        (void)want_link;
        if (!strcmp(base, "b") && (!b.always || b.link)) { bad++; printf("  %08x b: always %d link %d\n", addr, b.always, b.link); }
    }
    fclose(f);
    CHECK(nbr > 20, "%s: only %d branches checked", target, nbr);
    CHECK(bad == 0, "%s: %d of %d branches decoded wrongly", target, bad, nbr);
}

static void test_decoder_fixed(void)
{
    ppc_branch b;
    uint32_t out[2];
    ppc_decode(0x4800001d, 0x1000538, &b);                  /* bl +0x1c */
    CHECK(b.kind == PPC_B && b.link && b.always && b.target == 0x1000554, "bl");
    ppc_decode(0x4bfffff0, 0x1000100, &b);                  /* b -0x10 */
    CHECK(b.kind == PPC_B && !b.link && b.target == 0x10000f0, "b backwards");
    ppc_decode(0x48000102, 0x2000000, &b);                  /* ba 0x100 */
    CHECK(b.kind == PPC_B && b.target == 0x100, "ba");
    ppc_decode(0x4e800020, 0, &b);                          /* blr */
    CHECK(b.kind == PPC_BCLR && b.always && !b.link, "blr");
    ppc_decode(0x4e800421, 0, &b);                          /* bctrl */
    CHECK(b.kind == PPC_BCCTR && b.always && b.link, "bctrl");
    ppc_decode(0x4200fff8, 0x1000, &b);                     /* bdnz -8 */
    CHECK(b.kind == PPC_BC && !b.always && b.uses_ctr && b.target == 0xff8, "bdnz");
    ppc_decode(0x419e0010, 0x1000, &b);                     /* beq cr7,+16 */
    CHECK(b.kind == PPC_BC && !b.always && !b.uses_ctr && b.target == 0x1010, "beq");
    ppc_decode(0x429f0005, 0x1000, &b);                     /* bcl 20,31,+4 (get PC) */
    CHECK(b.kind == PPC_BC && b.always && b.link && b.target == 0x1004, "bcl 20,31");
    ppc_decode(0x4d9e0020, 0, &b);                          /* beqlr cr7 */
    CHECK(b.kind == PPC_BCLR && !b.always, "beqlr");
    ppc_decode(0x44000002, 0, &b);
    CHECK(b.kind == PPC_SC, "sc");
    ppc_decode(0x7fe00008, 0, &b);
    CHECK(b.kind == PPC_TRAP, "trap");
    ppc_decode(0x38600000, 0, &b);
    CHECK(b.kind == PPC_OTHER, "li");

    CHECK(ppc_successors(0x38600000, 0x100, 0, 0, out) == 1 && out[0] == 0x104, "succ li");
    CHECK(ppc_successors(0x4800001d, 0x100, 0, 0, out) == 1 && out[0] == 0x11c, "succ bl");
    CHECK(ppc_successors(0x4e800020, 0x100, 0x5550, 0, out) == 1 && out[0] == 0x5550, "succ blr");
    CHECK(ppc_successors(0x4e800420, 0x100, 0, 0x7770, out) == 1 && out[0] == 0x7770, "succ bctr");
    CHECK(ppc_successors(0x419e0010, 0x100, 0, 0, out) == 2 && out[0] == 0x104 && out[1] == 0x110, "succ beq");
    CHECK(ppc_successors(0x4d9e0020, 0x100, 0x900, 0, out) == 2 && out[1] == 0x900, "succ beqlr");
    int32_t d;
    CHECK(ppc_is_stwu_r1(0x9421ff80, &d) && d == -128, "stwu r1,-128(r1)");
    CHECK(!ppc_is_stwu_r1(0x9401ff80, &d), "stwu r0 isn't a frame");
}

/* ------------------------------------------------------------------ fake memory */

typedef struct { uint32_t addr, size; uint8_t *data; int writable; } region;
typedef struct { region r[32]; int n; dbg_regs regs; int reg_writes; } fakemem;

static region *find_region(fakemem *m, uint32_t a, uint32_t len)
{
    for (int i = 0; i < m->n; i++)
        if (a >= m->r[i].addr && a + len <= m->r[i].addr + m->r[i].size && a + len >= a) return &m->r[i];
    return NULL;
}

static int fm_read(void *ud, uint32_t a, void *buf, uint32_t len)
{
    fakemem *m = ud;
    uint32_t done = 0;
    while (done < len) {
        region *r = find_region(m, a + done, 1);
        if (!r) break;
        uint32_t n = r->addr + r->size - (a + done);
        if (n > len - done) n = len - done;
        memcpy((uint8_t *)buf + done, r->data + (a + done - r->addr), n);
        done += n;
    }
    return (int)done;
}

static int fm_write(void *ud, uint32_t a, const void *buf, uint32_t len)
{
    fakemem *m = ud;
    region *r = find_region(m, a, len);
    if (!r) return 0;
    memcpy(r->data + (a - r->addr), buf, len);
    return (int)len;
}

static int fm_set_reg(void *ud, int reg, const void *val, int size)
{
    fakemem *m = ud;
    const uint8_t *b = val;
    uint32_t v = 0;
    for (int i = 0; i < size && i < 4; i++) v = v << 8 | b[i];
    if (reg < 32) { m->regs.gpr[reg] = v; m->reg_writes++; return 1; }
    return 0;
}

static region *add_region(fakemem *m, uint32_t addr, uint32_t size)
{
    region *r = &m->r[m->n++];
    r->addr = addr; r->size = size; r->data = calloc(1, size); r->writable = 1;
    return r;
}

static void load_sections(fakemem *m, const dbg_info *di, const char *file)
{
    FILE *f = fopen(file, "rb");
    for (int i = 0; i < di->elf.nsect; i++) {
        const elf_section *s = &di->elf.sect[i];
        if (!(s->flags & ELF_SHF_ALLOC) || !s->size || m->n >= 30) continue;
        region *r = add_region(m, s->addr, s->size);
        if (s->type != ELF_SHT_NOBITS && f) { fseek(f, s->offset, SEEK_SET); if (fread(r->data, 1, s->size, f) != s->size) {} }
    }
    if (f) fclose(f);
}

static void w32(fakemem *m, uint32_t a, uint32_t v) { uint8_t b[4]; put_be32(b, v); fm_write(m, a, b, 4); }
static void wstr(fakemem *m, uint32_t a, const char *s) { fm_write(m, a, s, (uint32_t)strlen(s) + 1); }

/* ------------------------------------------------------------------ evaluator */

static varstore *g_vs;
static eval_env g_env;

static dbg_var *ev(const char *e) { return eval_expr(g_vs, &g_env, e); }

#define EXPECT_VAL(expr, want) do { dbg_var *v_ = ev(expr); \
    CHECK(!strcmp(v_->value, want), "eval %s = %s, want %s", expr, v_->value, want); } while (0)
#define EXPECT_PREFIX(expr, want) do { dbg_var *v_ = ev(expr); \
    CHECK(!strncmp(v_->value, want, strlen(want)), "eval %s = %s, want prefix %s", expr, v_->value, want); } while (0)
#define EXPECT_SUFFIX(expr, want) do { dbg_var *v_ = ev(expr); \
    CHECK(ends_with(v_->value, want), "eval %s = %s, want suffix %s", expr, v_->value, want); } while (0)
#define EXPECT_TYPE(expr, want) do { dbg_var *v_ = ev(expr); \
    CHECK(!strcmp(v_->type, want), "type of %s = %s, want %s", expr, v_->type, want); } while (0)

static void test_basic(void)
{
    char err[128], file[512];
    snprintf(file, sizeof file, "%s", path(g_targets, "basic", ""));
    dbg_info *di = info_load(file, err, sizeof err);
    CHECK(di != NULL, "info_load basic: %s", err);
    if (!di) return;

    /* functions, lines, symbols */
    const dfunc *fmain = info_func_named(di, "main");
    CHECK(fmain && fmain->nparams == 2, "main has 2 params");
    if (!fmain) { info_free(di); return; }
    CHECK(!strcmp(fmain->params[0].name, "argc") && fmain->params[0].cls == V_PARAM && fmain->params[0].off == 0x68, "argc at 0x68");
    CHECK(!strcmp(fmain->params[1].name, "argv") && fmain->params[1].off == 0x6c, "argv at 0x6c");
    const char *want_locals[] = { "calls", "a", "b", "head", "i", "total", "buf", "f" };
    for (size_t k = 0; k < sizeof want_locals / sizeof *want_locals; k++) {
        int found = 0;
        for (int i = 0; i < fmain->nlocals; i++) if (!strcmp(fmain->locals[i].name, want_locals[k])) found = 1;
        CHECK(found, "main has local %s", want_locals[k]);
    }
    const dfunc *fadd = info_func_named(di, "add");
    CHECK(fadd && fadd->body > fadd->lo, "add has a prologue");
    int la = 0;
    uint32_t a = info_line_addr(di, "basic.c", marker("basic.c", "add_body"), &la);
    CHECK(fadd && a == fadd->body && la == marker("basic.c", "add_body"), "bp at add_body resolves to add's body (%08x %d)", a, la);
    a = info_line_addr(di, "tests/targets/basic.c", fmain->line, &la);
    CHECK(a == fmain->body && la > fmain->line, "bp on main's opening line moves past the prologue (%d)", la);
    a = info_line_addr(di, "DH1:AmiForge/src/BASIC.C", marker("basic.c", "loop_body"), &la);
    CHECK(a && la == marker("basic.c", "loop_body"), "Amiga path, other case resolves");
    a = info_line_addr(di, "other.c", 10, &la);
    CHECK(a == 0, "unknown file doesn't resolve");
    a = info_line_addr(di, "basic.c", marker("basic.c", "main_twice") - 1, &la);   /* blank/brace line before */
    CHECK(la >= marker("basic.c", "main_twice") - 1 && la <= marker("basic.c", "main_twice"), "line without code moves forward (%d)", la);
    const dline *dl = info_line_at(di, fadd->body);
    CHECK(dl && dl->line == marker("basic.c", "add_body") && !strcmp(path_base(info_file_name(di, dl->file)), "basic.c"), "addr -> line");
    CHECK(info_func_at(di, fadd->body + 4) == fadd, "addr -> function");
    char sym[128];
    CHECK(info_symbolize(di, fadd->lo + 8, sym, sizeof sym) && !strcmp(sym, "add+0x8"), "symbolize %s", sym);
    CHECK(info_symbol_addr(di, "g_counter") != 0, "symbol address of g_counter");
    CHECK(info_is_line_start(di, fadd->body) && !info_is_line_start(di, fadd->body + 4), "line starts");

    /* relocation round trip */
    dreloc rel[8];
    int nr = 0;
    for (int i = 0; i < di->elf.nsect && nr < 8; i++) {
        const elf_section *s = &di->elf.sect[i];
        if (s->flags & ELF_SHF_ALLOC) { rel[nr].link = s->addr; rel[nr].size = s->size; rel[nr].run = 0x40000000u + s->addr; nr++; }
    }
    info_set_reloc(di, rel, nr);
    CHECK(info_l2r(di, fmain->lo) == 0x40000000u + fmain->lo, "l2r");
    CHECK(info_func_at(di, 0x40000000u + fmain->body) == fmain, "func_at with relocation");
    CHECK(info_in_program(di, 0x40000000u + fmain->body) && !info_in_program(di, fmain->body), "in_program");
    a = info_line_addr(di, "basic.c", marker("basic.c", "add_body"), &la);
    CHECK(a == 0x40000000u + fadd->body, "line_addr returns run-time addresses");
    info_set_reloc(di, NULL, 0);

    /* types */
    dtype *node = info_type_named(di, "node", 's');
    char tn[128];
    CHECK(node && node->nfields == 5 && type_size(node) == 28, "struct node: 5 fields, 28 bytes");
    if (node) {
        type_name(node->fields[1].type, tn, sizeof tn);
        CHECK(!strcmp(tn, "char [8]"), "node.name is %s", tn);
        type_name(node->fields[4].type, tn, sizeof tn);
        CHECK(!strcmp(tn, "struct node *"), "node.next is %s", tn);
    }
    dtype *col = info_type_named(di, "colour", 'e');
    CHECK(col && col->nenums == 3 && col->enums[1].value == 5, "enum colour");

    /* fake memory: sections at link addresses + main's frame */
    fakemem m;
    memset(&m, 0, sizeof m);
    load_sections(&m, di, file);
    const uint32_t SP = 0x60000000u;
    add_region(&m, SP, 0x400);
    uint32_t A = SP + 0x18, B = SP + 0x34, ARGV = SP + 0x100;
    w32(&m, A + 0, 1); wstr(&m, A + 4, "first"); w32(&m, A + 12, 3); w32(&m, A + 16, 4); w32(&m, A + 20, 5); w32(&m, A + 24, B);
    w32(&m, B + 0, 2); wstr(&m, B + 4, "second"); w32(&m, B + 20, 6); w32(&m, B + 24, 0);
    w32(&m, SP + 0x10, A);                       /* head */
    w32(&m, SP + 0x08, 5);                       /* i */
    w32(&m, SP + 0x0c, 30);                      /* total */
    wstr(&m, SP + 0x50, "buffer");               /* buf */
    w32(&m, SP + 0x14, 0x40700000);              /* f = 3.75f */
    w32(&m, SP + 0x68, 2);                       /* argc */
    w32(&m, SP + 0x6c, ARGV);                    /* argv */
    w32(&m, ARGV, ARGV + 0x10); w32(&m, ARGV + 4, ARGV + 0x20); w32(&m, ARGV + 8, 0);
    wstr(&m, ARGV + 0x10, "basic"); wstr(&m, ARGV + 0x20, "crash");
    m.regs.gpr[1] = SP; m.regs.gpr[3] = 0x1234; m.regs.pc = fmain->body + 0x40; m.regs.lr = 0x01b17d14;

    memset(&g_env, 0, sizeof g_env);
    g_env.di = di;
    g_env.mem.ud = &m; g_env.mem.read = fm_read; g_env.mem.write = fm_write; g_env.mem.set_reg = fm_set_reg;
    g_env.func = fmain; g_env.pc = fmain->body + 0x40; g_env.sp = SP; g_env.regs = &m.regs;
    g_vs = varstore_new();

    EXPECT_VAL("a.value", "1");
    EXPECT_VAL("a.name", "\"first\"");
    EXPECT_VAL("head->next->name", "\"second\"");
    EXPECT_VAL("a.pos.x + a.pos.y", "7");
    EXPECT_VAL("a.col", "GREEN");
    EXPECT_VAL("b.col", "BLUE");
    EXPECT_VAL("a.col == GREEN", "1");
    EXPECT_VAL("BLUE", "BLUE");
    EXPECT_VAL("buf", "\"buffer\"");
    EXPECT_VAL("buf[1]", "117 'u'");
    EXPECT_VAL("f", "3.75");
    EXPECT_VAL("f * 2", "7.5");
    EXPECT_VAL("i", "5");
    EXPECT_VAL("total / 4", "7");
    EXPECT_VAL("total % 4", "2");
    EXPECT_VAL("-total", "-30");
    EXPECT_VAL("g_counter", "3");
    EXPECT_VAL("s_hidden", "11");
    EXPECT_SUFFIX("g_greeting", " \"hello\"");
    EXPECT_VAL("g_greeting[1]", "101 'e'");
    EXPECT_VAL("*g_greeting", "104 'h'");
    EXPECT_VAL("g_ratio", "2.5");
    EXPECT_VAL("g_table", "{10, 20, 30, 40}");
    EXPECT_VAL("g_table[2] * 2", "60");
    EXPECT_VAL("*(g_table + 3)", "40");
    EXPECT_VAL("g_flags.a", "5");
    EXPECT_VAL("g_flags.b", "17");
    EXPECT_VAL("g_flags.c", "-3");
    EXPECT_VAL("g_num.i", "1065353216");
    EXPECT_VAL("g_num.f", "1");
    EXPECT_VAL("*head", "{...}");
    EXPECT_VAL("&a", "0x60000018");
    EXPECT_VAL("head == &a", "1");
    EXPECT_VAL("head->next == &b", "1");
    EXPECT_VAL("b.next", "0x00000000");
    EXPECT_VAL("sizeof(struct node)", "28");
    EXPECT_VAL("sizeof a", "28");
    EXPECT_VAL("sizeof(node_t *)", "4");
    EXPECT_VAL("(char)65", "65 'A'");
    EXPECT_VAL("(unsigned char)-1", "255 '\\377'");
    EXPECT_VAL("(struct node *)head", "0x60000018");
    EXPECT_VAL("((struct node *)head)->pos.y", "4");
    EXPECT_VAL("((node_t *)0x60000034)->value", "2");
    EXPECT_SUFFIX("argv[1]", " \"crash\"");
    EXPECT_VAL("argc", "2");
    EXPECT_VAL("$r3", "4660");
    EXPECT_VAL("$r3 + 1 == 0x1235", "1");
    EXPECT_VAL("0x10 + 3", "19");
    EXPECT_VAL("10 > 3 && 2 < 1", "0");
    EXPECT_VAL("10 > 3 || 1/0", "1");
    EXPECT_VAL("1 << 4 | 1", "17");
    EXPECT_VAL("~0", "-1");
    EXPECT_VAL("0xffffffff", "4294967295");
    EXPECT_VAL("(float)1 / 4", "0.25");
    EXPECT_VAL("'\\n'", "10 '\\n'");
    EXPECT_VAL("calls", "0");
    EXPECT_PREFIX("1/0", "<error: division by zero");
    EXPECT_PREFIX("nosuchvar", "<error: no symbol \"nosuchvar\"");
    EXPECT_PREFIX("a.nosuch", "<error: there is no member named nosuch");
    EXPECT_PREFIX("a +", "<error:");
    EXPECT_PREFIX("i i", "<error: unexpected text");
    EXPECT_PREFIX("add", "0x");
    EXPECT_SUFFIX("add", " <add>");

    EXPECT_TYPE("head", "node_t *");
    EXPECT_TYPE("buf", "char [16]");
    EXPECT_TYPE("argv", "char **");
    EXPECT_TYPE("g_greeting", "const char *");
    EXPECT_TYPE("a.pos", "struct point");
    EXPECT_TYPE("g_table", "int [4]");
    EXPECT_TYPE("&g_table", "int (*)[4]");
    EXPECT_TYPE("f", "float");
    EXPECT_TYPE("a.col", "enum colour");

    /* children */
    dbg_var *va = ev("a"), **kids;
    CHECK(va->has_children && va->addr == A && va->size == 28, "a has children, addr, size");
    int nk = eval_children(g_vs, &g_env, va, &kids);
    CHECK(nk == 5 && !strcmp(kids[0]->name, "value") && !strcmp(kids[1]->value, "\"first\"") && !strcmp(kids[3]->value, "GREEN"),
          "children of a (%d)", nk);
    if (nk == 5) {
        dbg_var **gk;
        int ng = eval_children(g_vs, &g_env, kids[2], &gk);
        CHECK(ng == 2 && !strcmp(gk[1]->name, "y") && !strcmp(gk[1]->value, "4"), "a.pos.y via children");
        CHECK(kids[4]->has_children, "a.next expands");
        ng = eval_children(g_vs, &g_env, kids[4], &gk);
        CHECK(ng == 5 && !strcmp(gk[1]->value, "\"second\""), "a.next children are b's fields");
        varstore_release(g_vs, gk, ng);
    }
    varstore_release(g_vs, kids, nk);
    dbg_var *vt = ev("g_table");
    nk = eval_children(g_vs, &g_env, vt, &kids);
    CHECK(nk == 4 && !strcmp(kids[3]->name, "[3]") && !strcmp(kids[3]->value, "40"), "array children");
    dbg_var *vp = ev("argv");
    nk = eval_children(g_vs, &g_env, vp, &kids);
    CHECK(nk == 1 && !strcmp(kids[0]->name, "*argv") && ends_with(kids[0]->value, "\"basic\""), "pointer child");
    dbg_var *vs = ev("g_greeting");
    CHECK(!vs->has_children, "char * doesn't expand");

    /* locals and globals */
    dbg_var **loc;
    int nl = eval_locals(g_vs, &g_env, &loc);
    CHECK(nl == 10 && !strcmp(loc[0]->name, "argc") && !strcmp(loc[1]->name, "argv"), "locals: params first (%d)", nl);
    int seen_total = 0;
    for (int i = 0; i < nl; i++) if (!strcmp(loc[i]->name, "total") && !strcmp(loc[i]->value, "30")) seen_total = 1;
    CHECK(seen_total, "locals has total = 30");
    varstore_release(g_vs, loc, nl);
    dbg_var **gl;
    int ngl = eval_globals(g_vs, &g_env, fmain->cu, &gl);
    CHECK(ngl == 7, "7 file-scope variables in basic.c (%d)", ngl);
    varstore_release(g_vs, gl, ngl);

    /* assignment */
    char aerr[128] = "";
    dbg_var *vtot = ev("total");
    CHECK(vtot->editable && eval_assign(g_vs, &g_env, vtot, "42", aerr, sizeof aerr), "set total: %s", aerr);
    EXPECT_VAL("total", "42");
    dbg_var *vfl = ev("g_flags.b");
    CHECK(eval_assign(g_vs, &g_env, vfl, "9", aerr, sizeof aerr), "set bitfield: %s", aerr);
    EXPECT_VAL("g_flags.b", "9");
    EXPECT_VAL("g_flags.a", "5");
    EXPECT_VAL("g_flags.c", "-3");
    dbg_var *vf = ev("f");
    CHECK(eval_assign(g_vs, &g_env, vf, "0.5", aerr, sizeof aerr), "set float");
    EXPECT_VAL("f", "0.5");
    dbg_var *vr = ev("$r3");
    CHECK(eval_assign(g_vs, &g_env, vr, "77", aerr, sizeof aerr) && m.regs.gpr[3] == 77, "set $r3");
    dbg_var *vbuf = ev("buf");
    CHECK(!vbuf->editable && !eval_assign(g_vs, &g_env, vbuf, "1", aerr, sizeof aerr), "arrays aren't editable");

    /* conditions */
    char cerr[128];
    CHECK(eval_condition(&g_env, "i == 5", cerr, sizeof cerr) == 1, "condition true");
    CHECK(eval_condition(&g_env, "i == 4", cerr, sizeof cerr) == 0, "condition false");
    CHECK(eval_condition(&g_env, "head->value > 0 && b.next == 0", cerr, sizeof cerr) == 1, "pointer condition");
    CHECK(eval_condition(&g_env, "nosuch > 1", cerr, sizeof cerr) == -1 && strstr(cerr, "nosuch"), "condition error: %s", cerr);

    /* outer frames have no registers */
    g_env.regs = NULL; g_env.level = 1;
    EXPECT_PREFIX("$r3", "<error:");
    EXPECT_VAL("total", "42");
    g_env.regs = &m.regs; g_env.level = 0;

    /* release tolerates double frees and foreign arrays */
    dbg_var *x = ev("i");
    varstore_release(g_vs, &x, 1);
    varstore_release(g_vs, &x, 1);
    CHECK(1, "double release");

    /* frames: main at entry (no prologue yet) and in its body */
    frame_info fi;
    frame_analyse0(di, &g_env.mem, fmain->lo, SP + 0x80, 0x01b17d14, &fi);
    CHECK(fi.cfa == SP + 0x80 && fi.sp == SP && fi.ret == 0x01b17d14, "frame at entry: cfa %08x sp %08x ret %08x", fi.cfa, fi.sp, fi.ret);
    w32(&m, SP, SP + 0x80);                     /* back chain */
    add_region(&m, SP + 0x400, 0x10);
    w32(&m, SP + 0x84, 0x7f001234);             /* LR save word in the caller's frame */
    frame_analyse0(di, &g_env.mem, fmain->body + 0x40, SP, 0x01000600, &fi);
    CHECK(fi.cfa == SP + 0x80 && fi.sp == SP && fi.ret == 0x7f001234, "frame in body: cfa %08x ret %08x", fi.cfa, fi.ret);
    frame_analyse0(di, &g_env.mem, fadd->body, SP, 0x01000600, &fi);     /* leaf: LR never saved */
    CHECK(fi.ret == 0x01000600, "leaf function returns through LR");

    varstore_free(g_vs);
    for (int i = 0; i < m.n; i++) free(m.r[i].data);
    info_free(di);
}

static void test_cplus(void)
{
    char err[128];
    dbg_info *di = info_load(path(g_targets, "cplus", ""), err, sizeof err);
    CHECK(di != NULL, "info_load cplus: %s", err);
    if (!di) return;
    const dfunc *f = info_func_named(di, "shapes::Rect::area");
    CHECK(f && !strcmp(f->display, "shapes::Rect::area() const"), "demangled method: %s", f ? f->display : "-");
    CHECK(f && f->nparams == 1 && !strcmp(f->params[0].name, "this"), "method has this");
    f = info_func_named(di, "area");
    CHECK(f && !strcmp(f->base, "shapes::Rect::area"), "unqualified method name");
    f = info_func_named(di, "shapes::measure");
    CHECK(f && !strncmp(f->display, "shapes::measure(", 16), "overload by qualified name");
    f = info_func_named(di, "_ZN6shapes7measureEii");
    CHECK(f && !strcmp(f->display, "shapes::measure(int, int)"), "mangled name lookup");
    dtype *rect = info_type_named(di, "Rect", 's');
    CHECK(rect && rect->nfields == 3 && type_size(rect) == 32, "class Rect: 3 fields, 32 bytes");
    char tn[128];
    if (rect) {
        type_name(rect, tn, sizeof tn);
        CHECK(!strcmp(tn, "Rect"), "C++ class prints as %s", tn);
        CHECK(!strcmp(rect->fields[2].name, "label_"), "Rect::label_");
        type_name(rect->fields[2].type, tn, sizeof tn);
        CHECK(!strcmp(tn, "string"), "label_ is %s", tn);
    }
    dtype *bs = info_type_named(di, "basic_string", 's');
    CHECK(bs && bs->nfields >= 3, "std::string layout parsed");
    int la;
    uint32_t a = info_line_addr(di, "cplus.cpp", marker("cplus.cpp", "area_body"), &la);
    const dfunc *af = info_func_named(di, "shapes::Rect::area");
    CHECK(af && a == af->body, "bp in a method body");
    const dline *dl = info_line_at(di, af ? af->lo : 0);
    CHECK(dl && !strcmp(path_base(info_file_name(di, dl->file)), "cplus.cpp"), "method lines are in cplus.cpp");
    /* code from headers is attributed to the header (N_SOL) */
    const dfunc *len = info_func_named(di, "std::char_traits<char>::length");
    dl = len ? info_line_at(di, len->body) : NULL;
    CHECK(dl && !strcmp(path_base(info_file_name(di, dl->file)), "char_traits.h"), "inline header function lines in char_traits.h");
    info_free(di);
}

static void test_asm(void)
{
    char err[128];
    dbg_info *di = info_load(path(g_targets, "asmtest", ""), err, sizeof err);
    CHECK(di != NULL, "info_load asmtest: %s", err);
    if (!di) return;
    uint32_t add3 = info_symbol_addr(di, "add3"), lo, hi, body;
    CHECK(add3 != 0, "add3 in .symtab");
    CHECK(info_func_at(di, add3) == NULL, "add3 has no stabs function");
    CHECK(info_func_range(di, add3 + 4, &lo, &hi, &body) && lo == add3 && hi == add3 + 12 && body == add3, "add3 range from .symtab");
    const dline *dl = info_line_at(di, add3);
    CHECK(dl && dl->line == marker("asmtest_add3.S", "add3_first") && ends_with(info_file_name(di, dl->file), "asmtest_add3.S"), "asm line info");
    dl = info_line_at(di, add3 + 8);
    CHECK(dl && dl->line == marker("asmtest_add3.S", "add3_first") + 2, "asm line +2");
    int la;
    uint32_t a = info_line_addr(di, "asmtest_add3.S", marker("asmtest_add3.S", "add3_first") + 1, &la);
    CHECK(a == add3 + 4, "bp in assembler source");
    char sym[64];
    CHECK(info_symbolize(di, add3 + 4, sym, sizeof sym) && !strcmp(sym, "add3+0x4"), "symbolize asm: %s", sym);
    info_free(di);
}

/* Pause in OS code (dbg_pause): where the program can be stopped instead, and when a hit
 * there counts. The OS is a fake address range outside the program; the stack is built by
 * hand: OS frame -> user frame (returns into main) -> OS frame (newlib calling main) ->
 * user frame (returns into the program's startup code) -> end of the chain. */
static void test_pause(void)
{
    char err[128], file[512];
    snprintf(file, sizeof file, "%s", path(g_targets, "basic", ""));
    dbg_info *di = info_load(file, err, sizeof err);
    CHECK(di != NULL, "pause: info_load basic: %s", err);
    if (!di) return;
    const dfunc *fmain = info_func_named(di, "main");
    uint32_t start = info_symbol_addr(di, "_start");
    CHECK(fmain && start, "pause: main and _start");
    if (!fmain || !start) { info_free(di); return; }
    const uint32_t OS_PC = 0x01812804u, OS_RET = 0x01900040u, OS_LR = 0x01812700u;
    const uint32_t R_MAIN = fmain->body + 8, R_START = start + 8, R_LEAF = fmain->body + 16;

    /* is this pc in the program's code? */
    CHECK(info_in_program(di, fmain->lo) && info_in_program(di, R_MAIN), "pause: main is user code");
    CHECK(!info_in_program(di, OS_PC) && !info_in_program(di, OS_RET), "pause: kernel addresses are not user code");
    const dvar *gv = info_global_named(di, "g_counter", -1);
    if (gv) CHECK(!info_in_program(di, info_var_addr(di, gv)), "pause: data is not user code");

    fakemem m;
    memset(&m, 0, sizeof m);
    load_sections(&m, di, file);
    const uint32_t S0 = 0x60000000u, S1 = S0 + 0x40, S2 = S1 + 0x80, S3 = S2 + 0x40;
    add_region(&m, S0, 0x400);
    w32(&m, S0, S1);                              /* OS function's frame */
    w32(&m, S1, S2); w32(&m, S1 + 4, R_MAIN);     /* its return address, saved in the caller's frame */
    w32(&m, S2, S3); w32(&m, S2 + 4, OS_RET);     /* main was called from the OS (newlib) */
    w32(&m, S3, 0);  w32(&m, S3 + 4, R_START);    /* ... from the startup code; end of the chain */
    dbg_mem mem = { &m, fm_read, fm_write, fm_set_reg };

    uint32_t t[8];
    int n = frame_pause_targets(di, &mem, OS_PC, S0, OS_LR, t, 8);
    CHECK(n == 2 && t[0] == R_MAIN && t[1] == R_START, "pause: return addresses from the back chain (%d: %08lx %08lx)",
          n, n > 0 ? (unsigned long)t[0] : 0UL, n > 1 ? (unsigned long)t[1] : 0UL);
    /* a leaf OS function (or one that hasn't saved LR yet) returns through LR, which comes first */
    n = frame_pause_targets(di, &mem, OS_PC, S0, R_LEAF, t, 8);
    CHECK(n == 3 && t[0] == R_LEAF && t[1] == R_MAIN && t[2] == R_START, "pause: LR first when it is in the program (%d)", n);
    /* LR equal to a back chain address: listed once */
    n = frame_pause_targets(di, &mem, OS_PC, S0, R_MAIN, t, 8);
    CHECK(n == 2 && t[0] == R_MAIN && t[1] == R_START, "pause: no duplicates (%d)", n);
    /* the room given is respected */
    n = frame_pause_targets(di, &mem, OS_PC, S0, R_LEAF, t, 1);
    CHECK(n == 1 && t[0] == R_LEAF, "pause: max respected (%d)", n);
    /* in the program already: nothing to defer */
    n = frame_pause_targets(di, &mem, R_MAIN, S1, OS_LR, t, 8);
    CHECK(n == 0, "pause: no targets for a pc in user code (%d)", n);
    /* broken back chain (unreadable stack): only LR, if usable */
    n = frame_pause_targets(di, &mem, OS_PC, 0x70000000u, OS_LR, t, 8);
    CHECK(n == 0, "pause: unreadable stack, no targets (%d)", n);
    n = frame_pause_targets(di, &mem, OS_PC, 0x70000000u, R_LEAF, t, 8);
    CHECK(n == 1 && t[0] == R_LEAF, "pause: unreadable stack, LR only (%d)", n);
    /* a misaligned LR is never a target */
    n = frame_pause_targets(di, &mem, OS_PC, 0x70000000u, R_LEAF + 2, t, 8);
    CHECK(n == 0, "pause: misaligned LR ignored (%d)", n);

    /* does a hit count? returns leave r1 at or above the pause's; callbacks run below it */
    CHECK(frame_pause_hit_ok(S0, S0 - 0x1000, S0 + 0x1000, S1), "pause: return to an outer frame counts");
    CHECK(frame_pause_hit_ok(S0, S0 - 0x1000, S0 + 0x1000, S0), "pause: return through LR (same r1) counts");
    CHECK(!frame_pause_hit_ok(S0, S0 - 0x1000, S0 + 0x1000, S0 - 0x60), "pause: a callback from the OS doesn't count");
    CHECK(frame_pause_hit_ok(S0, 0x50000000u, 0x50010000u, S0 - 0x60), "pause: OS on another stack: any hit counts");
    CHECK(frame_pause_hit_ok(0, S0 - 0x1000, S0 + 0x1000, 0x1000), "pause: no r1 recorded: any hit counts");

    /* kill: rewrite the context only where that is known to work */
    CHECK(frame_kill_plan(1, 0, 0, 0) == KILL_NOW, "kill: stopped by the hook (even in OS code, e.g. a crash): at once");
    CHECK(frame_kill_plan(0, 1, 0, 0) == KILL_NOW, "kill: interrupted in the program's code: at once");
    CHECK(frame_kill_plan(0, 1, 1, 2) == KILL_NOW, "kill: in the program's code wins over waiting");
    CHECK(frame_kill_plan(0, 0, 1, 2) == KILL_WAKE_AT_RETURN, "kill: in Wait(): wake it and trap the return");
    CHECK(frame_kill_plan(0, 0, 0, 2) == KILL_AT_RETURN, "kill: busy in an OS call: trap the return");
    CHECK(frame_kill_plan(0, 0, 1, 0) == KILL_GIVE_UP, "kill: in the OS with nowhere to trap: give up");
    CHECK(frame_kill_plan(0, 0, 0, 0) == KILL_GIVE_UP, "kill: busy in the OS, no targets: give up");
    CHECK(frame_kill_wake_mask(0x00013000u) == 0x00013000u, "kill wake: an allocated signal and CTRL-C/D");
    CHECK(frame_kill_wake_mask(0x00000110u) == 0, "kill wake: never SIGF_SINGLE or SIGF_DOS");
    CHECK(frame_kill_wake_mask(0x80000110u) == 0x80000000u, "kill wake: only the program's own of a mixed mask");
    CHECK(frame_kill_wake_mask(0x0000F000u) == 0x0000F000u, "kill wake: CTRL-C..CTRL-F");
    /* the targets a kill traps are the pause's: the waiting program's stack from above */
    n = frame_pause_targets(di, &mem, OS_PC, S0, OS_LR, t, 8);
    CHECK(frame_kill_plan(0, info_in_program(di, OS_PC), 1, n) == KILL_WAKE_AT_RETURN, "kill: Wait() under main has targets (%d)", n);

    for (int i = 0; i < m.n; i++) free(m.r[i].data);
    info_free(di);
}

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: dbg_hosttest TARGETS DUMPS SOURCES\n"); return 2; }
    g_targets = argv[1]; g_dumps = argv[2]; g_srcdir = argv[3];
    test_decoder_fixed();
    const char *targets[] = { "basic", "cplus", "asmtest" };
    for (int i = 0; i < 3; i++) { test_lines(targets[i]); test_decoder_dump(targets[i]); }
    test_basic();
    test_cplus();
    test_asm();
    test_pause();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
