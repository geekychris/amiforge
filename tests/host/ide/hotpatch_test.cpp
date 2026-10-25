// Edit and Continue: links fixture objects (tests/host/ide/hotpatch, built by the OS4
// cross-compiler) against fixture programs and checks the result instruction by instruction.
#include "aftest.h"
#include "dbg/hotpatch.h"
#include "dbg/dbg_int.h"
#include <cstdlib>
#include <cstring>

static std::string fx(const char *rel)
{
    const char *d = getenv("HP_FIXTURES");
    return std::string(d ? d : "../../build/host/hp") + "/" + rel;
}

// Programs are linked at 0x01000000; pretend they were loaded 0x40000000 higher (a constant
// offset is enough to tell link from run addresses).
static const uint32_t LOAD = 0x40000000u;
static uint32_t l2r(void *, uint32_t a) { return a + LOAD; }

struct Elf {
    hp_elf *e = nullptr;
    Elf(const std::string &path, int rela)
    {
        char err[200];
        e = hp_elf_load(path.c_str(), rela, err, sizeof err);
        if (!e) std::printf("  can't load %s: %s\n", path.c_str(), err);
    }
    ~Elf() { hp_elf_free(e); }
};

// run-time address of a program symbol (via the debugger's own reader)
static uint32_t prog_sym(const std::string &prog, const char *name)
{
    char err[128];
    dbg_info *di = info_load(prog.c_str(), err, sizeof err);
    if (!di) return 0;
    uint32_t a = 0;
    for (int i = 0; i < di->elf.nsym; i++)
        if (!strcmp(di->elf.sym[i].name, name)) { a = di->elf.sym[i].value + LOAD; break; }
    info_free(di);
    return a;
}

static uint32_t w32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

// Addresses built by lis/addi(ori)/lwz pairs in the image: (rD,hi)+(lo) -> full address.
static std::vector<uint32_t> addresses_in(const hp_patch &p, uint32_t lo, uint32_t hi)
{
    std::vector<uint32_t> out;
    for (uint32_t a = lo; a + 4 <= hi; a += 4) {
        uint32_t w = w32(p.image + (a - p.base));
        if ((w >> 26) != 15 || ((w >> 16) & 31) != 0) continue;   // lis rD,X
        uint32_t rd = (w >> 21) & 31, high = w << 16;
        for (uint32_t b = a + 4; b < a + 24 && b + 4 <= hi; b += 4) {
            uint32_t v = w32(p.image + (b - p.base)), op = v >> 26;
            if (((v >> 16) & 31) != rd) continue;
            if (op == 14 || op == 32 || op == 36 || op == 34 || op == 38 || op == 44 || op == 40 || op == 48 || op == 52) {
                out.push_back(high + (uint32_t)(int32_t)(int16_t)(v & 0xffff));
                break;
            }
        }
    }
    return out;
}

static bool contains(const std::vector<uint32_t> &v, uint32_t x)
{
    for (auto a : v) if (a == x) return true;
    return false;
}

TEST(branch_near_and_far)
{
    uint32_t w[4];
    CHECK_EQ(hp_branch(0x1000, 0x1100, w), 1);
    CHECK_EQ(w[0], 0x48000100u);
    CHECK_EQ(hp_branch(0x1100, 0x1000, w), 1);
    CHECK_EQ(w[0], 0x4BFFFF00u);
    CHECK_EQ(hp_branch(0x01000000, 0x5A345678, w), 4);
    CHECK_EQ(w[0], 0x3C005A34u);   // lis r0,0x5a34
    CHECK_EQ(w[1], 0x60005678u);   // ori r0,r0,0x5678
    CHECK_EQ(w[2], 0x7C0903A6u);   // mtctr r0
    CHECK_EQ(w[3], 0x4E800420u);   // bctr
    // just out of reach forward
    CHECK_EQ(hp_branch(0x10000000, 0x12000000, w), 4);
    CHECK_EQ(hp_branch(0x10000000, 0x11FFFFFC, w), 1);
}

TEST(links_changed_function_keeping_data)
{
    Elf prog(fx("v1/prog"), 0), obj(fx("v2/compute.o"), 1);
    CHECK(prog.e && obj.e);
    if (!prog.e || !obj.e) return;
    hp_patch p;
    char err[256] = "";
    int ok = hp_prepare(&p, obj.e, prog.e, "src/compute.c", l2r, nullptr, err, sizeof err);
    CHECK(ok);
    if (!ok) { std::printf("  %s\n", err); return; }
    CHECK(p.mem_size > 0);
    const uint32_t base = 0x7F000000u;
    ok = hp_link(&p, base, err, sizeof err);
    CHECK(ok);
    if (!ok) { std::printf("  %s\n", err); hp_patch_free(&p); return; }

    // compute is redirected from the program's copy; twice() is new
    const hp_func *compute = nullptr, *twice = nullptr;
    for (int i = 0; i < p.nfuncs; i++) {
        if (!strcmp(p.funcs[i].name, "compute")) compute = &p.funcs[i];
        if (!strcmp(p.funcs[i].name, "twice")) twice = &p.funcs[i];
    }
    CHECK(compute && twice);
    if (!compute || !twice) { hp_patch_free(&p); return; }
    std::string pp = fx("v1/prog");
    CHECK_EQ(compute->old_addr, prog_sym(pp, "compute"));
    CHECK(compute->old_size >= 16);
    CHECK_EQ(twice->old_addr, 0u);
    CHECK(compute->new_addr >= p.code_lo && compute->new_addr < p.code_hi);

    // its data references are the program's variables
    auto addrs = addresses_in(p, compute->new_addr, compute->new_addr + compute->new_size);
    CHECK(contains(addrs, prog_sym(pp, "counter")));
    CHECK(contains(addrs, prog_sym(pp, "calls")));
    CHECK(contains(addrs, prog_sym(pp, "factor")));
    CHECK(contains(addrs, prog_sym(pp, "local_static.0")));
    CHECK(contains(addrs, prog_sym(pp, "buf")));

    // strlen is ~1 GB away: the call goes through a stub that loads its address
    CHECK(p.nstubs >= 1);
    uint32_t strlen_addr = prog_sym(pp, "strlen");
    CHECK(strlen_addr != 0);
    bool stub_ok = false;
    for (uint32_t a = compute->new_addr; a < compute->new_addr + compute->new_size; a += 4) {
        uint32_t w = w32(p.image + (a - base));
        if ((w & 0xFC000003u) != 0x48000001u) continue;          // bl
        int32_t d = (int32_t)(w & 0x03FFFFFCu);
        if (d & 0x02000000) d -= 0x04000000;
        uint32_t t = a + (uint32_t)d;
        if (t == twice->new_addr) continue;
        CHECK(t >= p.code_lo && t < p.code_hi);
        const uint8_t *s = p.image + (t - base);
        uint32_t target = (w32(s) & 0xffff) << 16 | (w32(s + 4) & 0xffff);
        if (target == strlen_addr) stub_ok = true;
        CHECK_EQ(w32(s + 8), 0x7D8903A6u);
        CHECK_EQ(w32(s + 12), 0x4E800420u);
    }
    CHECK(stub_ok);

    // the new string literal is in the patch, not the program
    bool found = false;
    for (uint32_t i = 0; i + 7 <= p.mem_size; i++)
        if (!memcmp(p.image + i, "abcdef", 7)) found = true;
    CHECK(found);
    hp_patch_free(&p);
}

TEST(refuses_new_static)
{
    Elf prog(fx("v1/prog"), 0), obj(fx("newvar/compute.o"), 1);
    if (!prog.e || !obj.e) { CHECK(false); return; }
    hp_patch p;
    char err[256] = "";
    CHECK(!hp_prepare(&p, obj.e, prog.e, "compute.c", l2r, nullptr, err, sizeof err));
    CHECK(strstr(err, "extra") != nullptr);
    CHECK(strstr(err, "new static") != nullptr);
}

TEST(refuses_size_change)
{
    Elf prog(fx("v1/prog"), 0), obj(fx("grow/compute.o"), 1);
    if (!prog.e || !obj.e) { CHECK(false); return; }
    hp_patch p;
    char err[256] = "";
    CHECK(!hp_prepare(&p, obj.e, prog.e, "compute.c", l2r, nullptr, err, sizeof err));
    CHECK(strstr(err, "size of buf") != nullptr);
}

TEST(refuses_function_not_in_program)
{
    Elf prog(fx("v1/prog"), 0), obj(fx("newlib/compute.o"), 1);
    if (!prog.e || !obj.e) { CHECK(false); return; }
    hp_patch p;
    char err[256] = "";
    int ok = hp_prepare(&p, obj.e, prog.e, "compute.c", l2r, nullptr, err, sizeof err);
    CHECK(ok);
    if (ok) {
        CHECK(!hp_link(&p, 0x7F000000u, err, sizeof err));
        CHECK(strstr(err, "atoi") != nullptr);
        hp_patch_free(&p);
    }
}

TEST(refuses_other_file)
{
    Elf prog(fx("v1/prog"), 0), obj(fx("v2/compute.o"), 1);
    if (!prog.e || !obj.e) { CHECK(false); return; }
    hp_patch p;
    char err[256] = "";
    CHECK(!hp_prepare(&p, obj.e, prog.e, "other.c", l2r, nullptr, err, sizeof err));
    CHECK(strstr(err, "wasn't built from other.c") != nullptr);
}

TEST(debug_info_merges)
{
    std::string pp = fx("v1/prog");
    Elf prog(pp, 0), obj(fx("v2/compute.o"), 1);
    if (!prog.e || !obj.e) { CHECK(false); return; }
    hp_patch p;
    char err[256] = "";
    if (!hp_prepare(&p, obj.e, prog.e, "compute.c", l2r, nullptr, err, sizeof err) || !hp_link(&p, 0x7F000000u, err, sizeof err)) {
        std::printf("  %s\n", err); CHECK(false); return;
    }
    dbg_info *di = info_load(pp.c_str(), err, sizeof err);
    CHECK(di != nullptr);
    if (!di) { hp_patch_free(&p); return; }
    // the program as loaded: one section mapping per allocated section
    std::vector<dreloc> rel;
    for (int i = 0; i < di->elf.nsect; i++) {
        const elf_section *s = &di->elf.sect[i];
        if ((s->flags & ELF_SHF_ALLOC) && s->size) rel.push_back({ s->addr, s->size, s->addr + LOAD });
    }
    info_set_reloc(di, rel.data(), (int)rel.size());
    int old_line = 0;
    uint32_t old_addr = info_line_addr(di, "compute.c", 13, &old_line);
    const dfunc *oldf = info_func_named(di, "compute");
    CHECK(oldf != nullptr);
    uint32_t old_lo = oldf ? info_l2r(di, oldf->lo) : 0;
    CHECK(info_in_program(di, old_lo));

    dbg_info *pi = hp_debug_info(&p, err, sizeof err);
    CHECK(pi != nullptr);
    if (!pi) { info_free(di); hp_patch_free(&p); return; }
    const hp_func *nc = nullptr;
    for (int i = 0; i < p.nfuncs; i++) if (!strcmp(p.funcs[i].name, "compute")) nc = &p.funcs[i];
    CHECK(info_func_named(pi, "compute") && info_func_named(pi, "compute")->lo == nc->new_addr);
    CHECK(hp_merge_info(di, pi, "compute.c", p.base, p.mem_size, p.code_lo, p.code_hi));

    // name and line lookups find the new code; the old code is still known by address
    const dfunc *f = info_func_named(di, "compute");
    CHECK(f && info_l2r(di, f->lo) == nc->new_addr);
    CHECK(info_func_named(di, "twice") != nullptr);
    int line = 0;
    uint32_t a = info_line_addr(di, "compute.c", 13, &line);   // calls++ in v2 (line 13)
    CHECK(a >= p.code_lo && a < p.code_hi);
    CHECK_EQ(line, 13);
    CHECK(old_addr != 0 && old_addr != a);
    const dfunc *of = info_func_at(di, old_lo);
    CHECK(of && of->superseded);
    CHECK(info_in_program(di, nc->new_addr));
    CHECK(info_in_sections(di, p.base, 4));
    const dline *l = info_line_at(di, a);
    CHECK(l && l->line == 13);
    CHECK(l && !strcmp(info_file_name(di, l->file) + strlen(info_file_name(di, l->file)) - 9, "compute.c"));
    // the static local inside compute is the program's
    const dfunc *nf = info_func_at(di, nc->new_addr);
    bool found = false;
    for (int i = 0; nf && i < nf->nlocals; i++)
        if (!strcmp(nf->locals[i].name, "local_static")) {
            found = true;
            CHECK_EQ(nf->locals[i].addr, prog_sym(pp, "local_static.0"));
        }
    CHECK(found);
    info_free(di);
    hp_patch_free(&p);
}

TEST(second_patch_supersedes_first)
{
    std::string pp = fx("v1/prog");
    Elf prog(pp, 0), obj(fx("v2/compute.o"), 1);
    if (!prog.e || !obj.e) { CHECK(false); return; }
    char err[256];
    dbg_info *di = info_load(pp.c_str(), err, sizeof err);
    if (!di) { CHECK(false); return; }
    uint32_t bases[2] = { 0x7F000000u, 0x7E000000u };
    uint32_t news[2] = { 0, 0 };
    for (int k = 0; k < 2; k++) {
        hp_patch p;
        CHECK(hp_prepare(&p, obj.e, prog.e, "compute.c", l2r, nullptr, err, sizeof err));
        CHECK(hp_link(&p, bases[k], err, sizeof err));
        for (int i = 0; i < p.nfuncs; i++) if (!strcmp(p.funcs[i].name, "compute")) news[k] = p.funcs[i].new_addr;
        dbg_info *pi = hp_debug_info(&p, err, sizeof err);
        CHECK(pi && hp_merge_info(di, pi, "compute.c", p.base, p.mem_size, p.code_lo, p.code_hi));
        hp_patch_free(&p);
    }
    const dfunc *f = info_func_named(di, "compute");
    CHECK(f && f->lo == news[1]);
    int line = 0;
    uint32_t a = info_line_addr(di, "compute.c", 13, &line);
    CHECK(a >= news[1] && a < news[1] + 0x200);
    const dfunc *first = info_func_at(di, news[0]);
    CHECK(first && first->superseded);
    CHECK(info_in_program(di, news[0]) && info_in_program(di, news[1]));
    info_free(di);
}

TEST(cplusplus_method_and_vtable)
{
    std::string pp = fx("cpp1/prog");
    Elf prog(pp, 0), obj(fx("cpp2/shape.o"), 1);
    if (!prog.e || !obj.e) { CHECK(false); return; }
    hp_patch p;
    char err[256] = "";
    int ok = hp_prepare(&p, obj.e, prog.e, "shape.cpp", l2r, nullptr, err, sizeof err) && hp_link(&p, 0x7F000000u, err, sizeof err);
    CHECK(ok);
    if (!ok) { std::printf("  %s\n", err); return; }
    const hp_func *area = nullptr, *dtor = nullptr;
    for (int i = 0; i < p.nfuncs; i++) {
        if (!strcmp(p.funcs[i].name, "_ZNK6Square4areaEv")) area = &p.funcs[i];
        if (!strcmp(p.funcs[i].name, "_ZN5ShapeD2Ev")) dtor = &p.funcs[i];
    }
    CHECK(area && area->old_addr == prog_sym(pp, "_ZNK6Square4areaEv"));
    CHECK(dtor && dtor->old_addr == prog_sym(pp, "_ZN5ShapeD2Ev"));
    // the destructor stores the program's vtable pointer, not a copy in the patch
    uint32_t vt = prog_sym(pp, "_ZTV5Shape");
    CHECK(vt != 0);
    if (dtor) {
        auto addrs = addresses_in(p, dtor->new_addr, dtor->new_addr + dtor->new_size);
        bool uses_prog_vtable = false;
        for (auto a : addrs) if (a >= vt && a < vt + 32) uses_prog_vtable = true;
        CHECK(uses_prog_vtable);
    }
    hp_patch_free(&p);
}

TEST(plain_gstabs_function_ranges)
{
    // C built with -gstabs has no end-of-function stab: the last function of the patch
    // must still cover its whole body (it ends where the unit ends)
    Elf prog(fx("v1/prog"), 0), obj(fx("gs/compute.o"), 1);
    if (!prog.e || !obj.e) { CHECK(false); return; }
    hp_patch p;
    char err[256] = "";
    if (!hp_prepare(&p, obj.e, prog.e, "compute.c", l2r, nullptr, err, sizeof err) || !hp_link(&p, 0x7F000000u, err, sizeof err)) {
        std::printf("  %s\n", err); CHECK(false); return;
    }
    dbg_info *pi = hp_debug_info(&p, err, sizeof err);
    CHECK(pi != nullptr);
    const hp_func *nc = nullptr;
    for (int i = 0; i < p.nfuncs; i++) if (!strcmp(p.funcs[i].name, "compute")) nc = &p.funcs[i];
    if (pi && nc) {
        uint32_t last = nc->new_addr + nc->new_size - 4;   // its blr
        const dfunc *f = info_func_at(pi, last);
        CHECK(f && !strcmp(f->name, "compute"));
        const dline *l = info_line_at(pi, nc->new_addr + nc->new_size / 2);
        CHECK(l != nullptr);
        CHECK(info_func_at(pi, nc->new_addr + nc->new_size) == nullptr);
    }
    if (pi) info_free(pi);
    hp_patch_free(&p);
}
