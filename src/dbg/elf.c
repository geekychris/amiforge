/* elf.c — reads section headers, .symtab and .stab/.stabstr of an ELF32 big-endian file. */
#include "dbg_int.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int read_at(FILE *f, uint32_t off, void *buf, uint32_t len)
{
    return fseek(f, (long)off, SEEK_SET) == 0 && fread(buf, 1, len, f) == len;
}

static void *load_section(elf_file *ef, FILE *f, const elf_section *s)
{
    void *p = arena_alloc(&ef->mem, s->size + 1);   /* +1: string tables get a terminator */
    if (p && s->size && !read_at(f, s->offset, p, s->size)) return NULL;
    return p;
}

static int sym_cmp(const void *a, const void *b)
{
    const elf_symbol *x = a, *y = b;
    if (x->value != y->value) return x->value < y->value ? -1 : 1;
    /* functions before other symbols at the same address, then globals */
    if (x->type != y->type) return x->type == ELF_STT_FUNC ? -1 : y->type == ELF_STT_FUNC ? 1 : 0;
    return (int)y->bind - (int)x->bind;
}

int elf_open(elf_file *ef, const char *path)
{
    memset(ef, 0, sizeof *ef);
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(ef->err, sizeof ef->err, "can't open %s", path); return -1; }
    uint8_t h[52];
    if (!read_at(f, 0, h, sizeof h) || memcmp(h, "\177ELF", 4) || h[4] != 1 || h[5] != 2) {
        snprintf(ef->err, sizeof ef->err, "%s is not an ELF32 big-endian file", path);
        fclose(f); return -1;
    }
    if (be16(h + 18) != 20) {   /* EM_PPC */
        snprintf(ef->err, sizeof ef->err, "%s is not a PowerPC executable", path);
        fclose(f); return -1;
    }
    ef->entry = be32(h + 24);
    uint32_t shoff = be32(h + 32);
    uint16_t shentsize = be16(h + 46), shnum = be16(h + 48), shstrndx = be16(h + 50);
    if (!shoff || shentsize < 40 || !shnum) {
        snprintf(ef->err, sizeof ef->err, "%s has no section headers", path);
        fclose(f); return -1;
    }
    uint8_t *sh = malloc((size_t)shnum * shentsize);
    if (!sh || !read_at(f, shoff, sh, (uint32_t)shnum * shentsize)) {
        free(sh); snprintf(ef->err, sizeof ef->err, "can't read section headers"); fclose(f); return -1;
    }
    ef->nsect = shnum;
    ef->sect = arena_alloc(&ef->mem, sizeof(elf_section) * shnum);
    for (int i = 0; i < shnum; i++) {
        const uint8_t *p = sh + (size_t)i * shentsize;
        elf_section *s = &ef->sect[i];
        s->name = (const char *)(uintptr_t)be32(p);   /* fixed up below */
        s->type = be32(p + 4); s->flags = be32(p + 8); s->addr = be32(p + 12);
        s->offset = be32(p + 16); s->size = be32(p + 20); s->link = be32(p + 24);
        s->info = be32(p + 28); s->entsize = be32(p + 36);
    }
    free(sh);
    const char *shstr = NULL;
    uint32_t shstr_size = 0;
    if (shstrndx < shnum) {
        shstr = load_section(ef, f, &ef->sect[shstrndx]);
        shstr_size = ef->sect[shstrndx].size;
    }
    for (int i = 0; i < shnum; i++) {
        uint32_t off = (uint32_t)(uintptr_t)ef->sect[i].name;
        ef->sect[i].name = shstr && off < shstr_size ? shstr + off : "";
    }

    /* .symtab + its string table */
    int si = elf_section_index(ef, ".symtab");
    if (si >= 0 && ef->sect[si].link < (uint32_t)shnum) {
        const elf_section *s = &ef->sect[si];
        uint8_t *raw = load_section(ef, f, s);
        char *str = load_section(ef, f, &ef->sect[s->link]);
        uint32_t strsz = ef->sect[s->link].size;
        uint32_t esz = s->entsize ? s->entsize : 16, n = s->size / esz;
        if (raw && str) {
            ef->sym = arena_alloc(&ef->mem, sizeof(elf_symbol) * (n ? n : 1));
            for (uint32_t i = 0; i < n; i++) {
                const uint8_t *p = raw + i * esz;
                uint32_t nm = be32(p);
                uint8_t info = p[12];
                uint8_t type = info & 15;
                uint16_t shndx = be16(p + 14);
                if (nm == 0 || nm >= strsz || shndx == 0 || shndx >= 0xff00) continue;
                if (type != ELF_STT_FUNC && type != ELF_STT_OBJECT && type != 0) continue;
                elf_symbol *e = &ef->sym[ef->nsym++];
                e->name = str + nm; e->value = be32(p + 4); e->size = be32(p + 8);
                e->type = type; e->bind = info >> 4; e->shndx = shndx;
            }
            qsort(ef->sym, ef->nsym, sizeof(elf_symbol), sym_cmp);
        }
    }

    /* .stab + .stabstr */
    int ti = elf_section_index(ef, ".stab"), tsi = elf_section_index(ef, ".stabstr");
    if (ti >= 0 && tsi >= 0) {
        ef->stab = load_section(ef, f, &ef->sect[ti]);
        ef->stab_size = ef->stab ? ef->sect[ti].size : 0;
        ef->stabstr = load_section(ef, f, &ef->sect[tsi]);
        ef->stabstr_size = ef->stabstr ? ef->sect[tsi].size : 0;
    }
    fclose(f);
    return 0;
}

void elf_close(elf_file *ef) { arena_free(&ef->mem); memset(ef, 0, sizeof *ef); }

int elf_section_index(const elf_file *ef, const char *name)
{
    for (int i = 0; i < ef->nsect; i++) if (!strcmp(ef->sect[i].name, name)) return i;
    return -1;
}
