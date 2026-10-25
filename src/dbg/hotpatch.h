/*
 * hotpatch.h — the portable half of Edit and Continue: a small linker that turns a freshly
 * compiled object file into a patch for the running program.
 *
 * The new object's code and read-only data go into memory the debugger allocates. Its
 * writable data (globals and statics) is NOT copied: every writable section is placed over
 * the running program's copy of that file's data, found through the program's symbol table,
 * so the program keeps its state. Everything else the object refers to (other files'
 * functions and globals, the C library linked into the program, interface pointers) is bound
 * to the program's run-time addresses. Calls that can't reach with a relative branch go
 * through stubs at the end of the patch's code.
 *
 * The caller then copies `image` to `base`, redirects each function in `funcs` (a branch at
 * the old entry, see hp_branch) and merges the patch's debug info (hp_debug_info +
 * hp_merge_info) so stepping, breakpoints and locals work in the new code.
 *
 * Refused (with a message saying why): data layout changes — a new global or static, a
 * variable whose size changed, data the linker can't match to the program — references to
 * symbols the program doesn't contain, and relocation types this linker doesn't handle.
 *
 * Plain C99, no OS calls except stdio for hp_elf_load: built on the host for tests.
 */
#ifndef AF_HOTPATCH_H
#define AF_HOTPATCH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct dbg_info;

typedef struct hp_elf hp_elf;

/* Reads an ELF32 big-endian PPC file: an object (ET_REL) or the program (ET_EXEC).
 * with_rela: also read the relocation sections (needed for the object). */
hp_elf *hp_elf_load(const char *path, int with_rela, char *err, size_t errlen);
/* Same from a malloc'd buffer (ownership passes to the hp_elf). */
hp_elf *hp_elf_from_buffer(uint8_t *buf, uint32_t size, int with_rela, char *err, size_t errlen);
void hp_elf_free(hp_elf *e);

/* Maps a link-time address of the program to its run-time address. */
typedef uint32_t (*hp_l2r_fn)(void *ud, uint32_t link);

typedef struct {
    const char *name;            /* symbol name (mangled for C++) */
    uint32_t old_addr, old_size; /* the program's version (old_addr 0: a new function) */
    uint32_t new_addr, new_size;
} hp_func;

typedef struct hp_priv hp_priv;

typedef struct {
    uint32_t mem_size;           /* bytes of memory the patch needs (after hp_prepare) */
    uint32_t base;               /* where it goes (hp_link) */
    uint8_t *image;              /* mem_size bytes to copy to base (after hp_link) */
    int nfuncs;
    hp_func *funcs;              /* every function the object defines */
    uint32_t code_lo, code_hi;   /* executable part of [base, base + mem_size) */
    int nstubs;                  /* long-branch stubs made */
    uint8_t *stab;               /* relocated debug info (stabs) */
    uint32_t stab_size;
    char *stabstr;
    uint32_t stabstr_size;
    char warn[256];              /* things that work but the user should know */
    hp_priv *priv;
} hp_patch;

/* Checks the object against the program and works out the layout. 1 = ok, 0 = refused
 * (err says why). obj and exe must stay alive until hp_link has run. source is the source
 * file's name; only its last path component is compared with the program's FILE symbols. */
int hp_prepare(hp_patch *p, const hp_elf *obj, const hp_elf *exe, const char *source,
               hp_l2r_fn l2r, void *ud, char *err, size_t errlen);
/* Places the patch at base and applies the relocations. 1 = ok. */
int hp_link(hp_patch *p, uint32_t base, char *err, size_t errlen);
void hp_patch_free(hp_patch *p);

/* Instruction(s) that jump from `from` to `to`: a relative `b` (returns 1) or
 * lis r0 / ori r0 / mtctr r0 / bctr (returns 4). */
int hp_branch(uint32_t from, uint32_t to, uint32_t out[4]);

/* Debug info for the patch (from its relocated stabs); run-time addresses = link addresses. */
struct dbg_info *hp_debug_info(const hp_patch *p, char *err, size_t errlen);
/* Adds a patch's debug info to the program's: the patch's functions replace the program's
 * functions of the same name in that source file for line and name lookups (the old code
 * stays known for frames still running it), and [base, base+size) becomes part of the
 * program (code in [code_lo, code_hi)). di takes ownership of pi. 1 = ok. */
int hp_merge_info(struct dbg_info *di, struct dbg_info *pi, const char *source,
                  uint32_t base, uint32_t size, uint32_t code_lo, uint32_t code_hi);

#ifdef __cplusplus
}
#endif
#endif
