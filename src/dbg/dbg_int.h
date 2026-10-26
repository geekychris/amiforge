/*
 * dbg_int.h — internals of the debugger core shared by the portable parts (ELF, stabs,
 * symbol tables, PPC decoder, expression evaluator) and the OS4 runtime (os4_*.c).
 * Everything here is plain C99 and builds on the host too (tests/host).
 *
 * Addresses: dbg_info stores link-time addresses. Once the program is loaded, the
 * session installs a per-section relocation (info_set_reloc) and every info_* function
 * takes and returns run-time addresses.
 */
#ifndef AF_DBG_INT_H
#define AF_DBG_INT_H

#include <stddef.h>
#include <stdint.h>
#include "dbg.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- small utilities (util.c) ---------- */

/* Bump allocator: everything parsed from one ELF file lives in one arena. */
typedef struct arena_blk arena_blk;
typedef struct { arena_blk *head; size_t used, cap; } arena;
void *arena_alloc(arena *a, size_t n);           /* zeroed, 8-byte aligned */
char *arena_strndup(arena *a, const char *s, size_t n);
char *arena_strdup(arena *a, const char *s);
void arena_free(arena *a);

/* Growable array of fixed-size elements (malloc-backed). */
typedef struct { void *data; size_t n, cap, esz; } vec;
void vec_init(vec *v, size_t esz);
void *vec_push(vec *v);                          /* zeroed new element */
void vec_free(vec *v);
#define VEC_AT(v, T, i) (((T *)(v)->data)[i])

/* Simple string helpers. */
const char *path_base(const char *p);            /* after the last '/' or ':' */
int str_ieq(const char *a, const char *b);
uint32_t be32(const uint8_t *p);
uint16_t be16(const uint8_t *p);
void put_be32(uint8_t *p, uint32_t v);

/* ---------- ELF (elf.c) ---------- */

typedef struct {
    const char *name;
    uint32_t type, flags, addr, offset, size, link, info, entsize;
} elf_section;

typedef struct {
    const char *name;
    uint32_t value, size;
    uint8_t type, bind;      /* STT_*, STB_* */
    uint16_t shndx;
} elf_symbol;

#define ELF_SHF_WRITE 0x1
#define ELF_SHF_ALLOC 0x2
#define ELF_SHF_EXEC  0x4
#define ELF_SHT_NOBITS 8
#define ELF_STT_OBJECT 1
#define ELF_STT_FUNC   2

typedef struct {
    arena mem;
    uint32_t entry;
    int nsect;
    elf_section *sect;
    int nsym;
    elf_symbol *sym;          /* .symtab, sorted by value */
    uint8_t *stab;  uint32_t stab_size;
    char *stabstr;  uint32_t stabstr_size;
    char err[128];
} elf_file;

/* Reads the parts of an ELF32 big-endian PPC file the debugger needs. 0 on success. */
int elf_open(elf_file *ef, const char *path);
void elf_close(elf_file *ef);
int elf_section_index(const elf_file *ef, const char *name);

/* ---------- types (stabs.c) ---------- */

typedef enum {
    T_UNKNOWN, T_VOID, T_INT, T_BOOL, T_FLOAT, T_PTR, T_REF, T_ARRAY, T_STRUCT, T_UNION,
    T_ENUM, T_FUNC, T_TYPEDEF, T_CONST, T_VOLATILE, T_XREF, T_ALIAS, T_MEMBERPTR
} dtype_kind;

typedef struct dtype dtype;

typedef struct {
    const char *name;
    dtype *type;
    uint32_t bitpos, bitsize;
    uint8_t is_static, is_base, vis;  /* vis: '0' private '1' protected '2' public */
    const char *physname;             /* static members: mangled symbol */
} dfield;

typedef struct { const char *name; int32_t value; } denum;

struct dtype {
    dtype_kind kind;
    const char *name;        /* base / typedef / struct tag / enum tag name, or NULL */
    uint32_t size;           /* bytes (0 = unknown / incomplete) */
    uint8_t is_signed, is_char;
    uint8_t plain_name;      /* struct/union/enum named without the C tag keyword (C++, typedef'd anon) */
    char xref_tag;           /* T_XREF: 's', 'u' or 'e' */
    dtype *target;           /* PTR/REF/ARRAY elem/TYPEDEF/CONST/VOLATILE/FUNC return/ALIAS */
    int32_t lo, hi;          /* ARRAY bounds */
    int nfields;
    dfield *fields;          /* STRUCT/UNION */
    int nenums;
    denum *enums;            /* ENUM */
};

/* Follows typedefs, const/volatile, aliases and resolved cross-references. */
dtype *type_strip(dtype *t);
/* Follows only aliases and resolved cross-references (keeps typedef names). */
dtype *type_unalias(dtype *t);
uint32_t type_size(dtype *t);
/* Writes a C declaration of the type ("struct node *", "char [16]") into buf. */
void type_name(dtype *t, char *buf, size_t n);
int type_is_integral(dtype *t);
int type_is_scalar(dtype *t);

/* ---------- debug info (stabs.c, info.c) ---------- */

typedef enum {
    V_LOCAL,     /* frame-relative (stack) */
    V_PARAM,     /* frame-relative parameter */
    V_REG,       /* register (gpr 0-31, fpr 32-63) */
    V_REGPARM,   /* parameter in a register */
    V_REFPARAM,  /* parameter passed by reference: the frame slot holds its address */
    V_STATIC,    /* absolute link address */
    V_GLOBAL,    /* address from .symtab (by name) */
    V_CONST      /* compile-time constant (value in `off`) */
} dvar_class;

typedef struct {
    const char *name;
    dtype *type;
    dvar_class cls;
    int32_t off;             /* frame offset, register number, constant */
    uint32_t addr;           /* V_STATIC / V_GLOBAL link address (0 if unknown) */
    uint32_t lo, hi;         /* scope, function-relative byte range (locals); 0,~0 = whole function */
    int line;
    int cu;                  /* globals: defining unit */
} dvar;

typedef struct {
    const char *name;        /* as in stabs (mangled for C++) */
    const char *display;     /* demangled, with parameters for C++ */
    const char *base;        /* display without the parameter list ("shapes::Rect::area") */
    uint32_t lo, hi;         /* link addresses [lo, hi) */
    uint32_t body;           /* first address after the prologue (2nd line entry), or lo */
    int file, line;          /* declaration */
    int cu;
    int nparams, nlocals;
    dvar *params, *locals;
    dtype *ret;
    uint8_t has_lines;
    uint8_t superseded;      /* --- hotpatch: replaced by a newer version (Edit and Continue) */
} dfunc;

typedef struct {
    uint32_t addr;           /* link address */
    int32_t line;
    uint16_t file;
    int32_t func;            /* index into funcs, -1 for lines outside any stabs function (asm) */
} dline;

typedef struct {
    const char *dir;         /* compilation directory (may be NULL) */
    const char *name;        /* main source file as given to the compiler */
    uint32_t lo, hi;
} dcu;

typedef struct {
    const char *name;        /* as in stabs */
    int cu;                  /* the unit that first mentioned it (for its comp dir) */
} dfile;

typedef struct {
    uint32_t link, size, run;   /* one allocated ELF section */
} dreloc;

typedef struct dbg_info {
    elf_file elf;
    arena mem;
    vec funcs;               /* dfunc, sorted by lo */
    vec lines;               /* dline, sorted by addr (then by stab order) */
    vec cus;                 /* dcu */
    vec files;               /* dfile */
    vec globals;             /* dvar: every N_GSYM / file-level N_STSYM / N_LCSYM */
    vec structs;             /* dtype*: named complete struct/union/enum types, for xrefs */
    vec typedefs;            /* dtype*: named typedefs and base types (for casts) */
    int nreloc;
    dreloc *reloc;           /* NULL = identity (host tests) */
    char err[128];
    /* --- hotpatch --- applied Edit and Continue patches (hotpatch.c) */
    int npatch_code;
    uint32_t (*patch_code)[2];   /* executable [lo, hi) of each patch: part of the program */
    vec patches;                 /* struct dbg_info *: their debug info (owns what merged entries point to) */
} dbg_info;

dbg_info *info_load(const char *path, char *err, size_t errlen);
void info_free(dbg_info *di);
void info_set_reloc(dbg_info *di, const dreloc *r, int n);   /* n = 0 clears */
uint32_t info_l2r(const dbg_info *di, uint32_t link);
uint32_t info_r2l(const dbg_info *di, uint32_t run);
int info_in_program(const dbg_info *di, uint32_t run);    /* inside an executable section */
int info_in_sections(const dbg_info *di, uint32_t run, uint32_t len);   /* inside any loaded section */

/* All of these take and return run-time addresses. The dfunc/dline structures they return
 * hold link addresses: convert with info_l2r. */
int stabs_parse(dbg_info *di);
const dfunc *info_func_at(const dbg_info *di, uint32_t addr);
const dfunc *info_func_named(const dbg_info *di, const char *name);
const dline *info_line_at(const dbg_info *di, uint32_t addr);     /* entry covering addr */
int info_is_line_start(const dbg_info *di, uint32_t addr);
/* Resolves file:line to the lowest address of the first line >= line that has code.
 * Lines in a function's prologue move to the function body. Returns 0 if none. */
uint32_t info_line_addr(const dbg_info *di, const char *file, int line, int *actual);
int info_file_matches(const dbg_info *di, int file, const char *user_path);
const char *info_file_name(const dbg_info *di, int file);
/* Run-time address range [lo,hi) of the function containing addr: stabs, else .symtab. */
int info_func_range(const dbg_info *di, uint32_t addr, uint32_t *lo, uint32_t *hi, uint32_t *body);
/* Nearest .symtab symbol at or below addr ("name+off" written to buf). 0 if none. */
int info_symbolize(const dbg_info *di, uint32_t addr, char *buf, size_t n);
uint32_t info_symbol_addr(const dbg_info *di, const char *name);   /* run-time, 0 if missing */
const dvar *info_global_named(const dbg_info *di, const char *name, int prefer_cu);
dtype *info_type_named(const dbg_info *di, const char *name, int tag);  /* tag: 0, 's', 'u', 'e' */
int info_cu_at(const dbg_info *di, uint32_t addr);
/* Run-time address of a static/global (0 if unknown). */
uint32_t info_var_addr(const dbg_info *di, const dvar *v);

/* Demangling (demangle.cpp). Returns buf, or NULL if `name` isn't a mangled C++ name. */
char *dbg_demangle(const char *name, char *buf, size_t n);

/* ---------- PPC decoding (ppc.c) ---------- */

typedef enum { PPC_OTHER, PPC_B, PPC_BC, PPC_BCLR, PPC_BCCTR, PPC_SC, PPC_TRAP } ppc_kind;

typedef struct {
    ppc_kind kind;
    int link;                /* LK: a call */
    int always;              /* unconditional (b, or BO "branch always") */
    int uses_ctr;            /* decrements CTR (bc/bclr with BO[2] clear) */
    uint32_t target;         /* static target for b/bc */
} ppc_branch;

void ppc_decode(uint32_t insn, uint32_t pc, ppc_branch *b);
/* Possible next PCs after executing insn at pc (1 or 2 entries). */
int ppc_successors(uint32_t insn, uint32_t pc, uint32_t lr, uint32_t ctr, uint32_t out[2]);
int ppc_is_stwu_r1(uint32_t insn, int32_t *disp);   /* stwu r1,-N(r1) */
#define PPC_TRAP_INSN 0x7FE00008u

/* ---------- frames ---------- */

/* Memory/register access used by frame walking and evaluation. */
typedef struct {
    void *ud;
    int (*read)(void *ud, uint32_t addr, void *buf, uint32_t len);   /* bytes read */
    int (*write)(void *ud, uint32_t addr, const void *buf, uint32_t len);
    int (*set_reg)(void *ud, int reg, const void *val, int size);   /* frame 0 only */
} dbg_mem;

int mem_read32(const dbg_mem *m, uint32_t addr, uint32_t *v);

typedef struct {
    uint32_t pc;
    uint32_t sp;             /* frame base: r1 of this frame once its prologue has run */
    uint32_t cfa;            /* caller's r1 */
    uint32_t ret;            /* return address (0 = unknown) */
} frame_info;

/* Analyses frame 0 from the registers: before the prologue the return address is LR,
 * afterwards it is *(cfa+4). */
void frame_analyse0(const dbg_info *di, const dbg_mem *m, uint32_t pc, uint32_t r1, uint32_t lr, frame_info *f);
/* Walks the back chain; returns the number of frames filled. */
int frame_walk(const dbg_info *di, const dbg_mem *m, uint32_t pc, uint32_t r1, uint32_t lr,
               frame_info *out, int max);
/* Pause (dbg_pause) when the program is in OS code: the program addresses it can be stopped
 * at instead, when it returns to them (LR, then the back chain's return addresses), and
 * whether a hit at one of them with stack pointer r1 counts. See frame.c. */
int frame_pause_targets(const dbg_info *di, const dbg_mem *m, uint32_t pc, uint32_t r1, uint32_t lr,
                        uint32_t *out, int max);
int frame_pause_hit_ok(uint32_t pause_r1, uint32_t sp_lo, uint32_t sp_hi, uint32_t r1);
/* Kill (dbg_kill): unwind at once, first trap the program where it returns from its OS call
 * (waking it if it waits), or give up and leave it suspended. See frame.c. */
typedef enum { KILL_NOW, KILL_AT_RETURN, KILL_WAKE_AT_RETURN, KILL_GIVE_UP } kill_plan;
kill_plan frame_kill_plan(int from_hook, int pc_in_program, int waiting, int ntargets);
uint32_t frame_kill_wake_mask(uint32_t sigwait);   /* signals to wake a waiting program with */

/* ---------- expressions and values (eval.c) ---------- */

/* Register numbers used by the evaluator. */
enum { REG_GPR = 0, REG_FPR = 32, REG_PC = 64, REG_LR, REG_CTR, REG_CR, REG_XER, REG_MSR, REG_COUNT };

typedef struct {
    const dbg_info *di;
    dbg_mem mem;
    const dfunc *func;       /* function of the selected frame (NULL if none) */
    uint32_t pc, sp;         /* selected frame */
    int level;
    const dbg_regs *regs;    /* NULL when registers of this frame aren't known */
    unsigned generation;     /* bumped by the session on each resume */
} eval_env;

/* Variable storage owned by the session (eval.c). */
typedef struct varstore varstore;
varstore *varstore_new(void);
void varstore_free(varstore *vs);
void varstore_release(varstore *vs, dbg_var **vars, int n);

dbg_var *eval_expr(varstore *vs, const eval_env *env, const char *expr);
dbg_var *eval_error(varstore *vs, const char *name, const char *msg);   /* value "<error: msg>" */
int eval_locals(varstore *vs, const eval_env *env, dbg_var ***out);
int eval_globals(varstore *vs, const eval_env *env, int cu, dbg_var ***out);
int eval_children(varstore *vs, const eval_env *env, dbg_var *v, dbg_var ***out);
int eval_assign(varstore *vs, const eval_env *env, dbg_var *v, const char *text, char *err, size_t errlen);
/* Evaluates a condition: 1 true, 0 false, -1 error (message in err). */
int eval_condition(const eval_env *env, const char *expr, char *err, size_t errlen);

#ifdef __cplusplus
}
#endif
#endif
