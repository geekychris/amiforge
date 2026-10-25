#include "aftest.h"
#include "debugtools_fake.h"

using namespace af;
using dtfake::ev;

TEST(leaks_matched_pairs_leave_nothing)
{
    LeakModel m;
    m.add(ev(DBG_RES_VEC, 0, DBG_FN_ALLOCVECTAGS, 0x100000, 64, 0x1010));
    m.add(ev(DBG_RES_FILE, 0, DBG_FN_OPEN, 0x2000, 0, 0x1020));
    m.add(ev(DBG_RES_FILE, 1, DBG_FN_CLOSE, 0x2000, 0, 0x1030));
    m.add(ev(DBG_RES_VEC, 1, DBG_FN_FREEVEC, 0x100000, 0, 0x1040));
    CHECK_EQ(m.outstanding(), (size_t)0);
    CHECK_EQ(m.report(dtfake::resolve).size(), (size_t)0);
    CHECK_EQ(m.problems(dtfake::resolve).size(), (size_t)0);
    CHECK_EQ(m.events(), 4u);
}

TEST(leaks_grouped_by_site)
{
    LeakModel m;
    // three allocations from the same call in load_level, one from main
    for (uint32_t i = 0; i < 3; i++) m.add(ev(DBG_RES_MALLOC, 0, DBG_FN_MALLOC, 0x200000 + i * 0x100, 100, 0x1404));
    m.add(ev(DBG_RES_MALLOC, 0, DBG_FN_MALLOC, 0x300000, 1000, 0x1104));
    m.add(ev(DBG_RES_MALLOC, 1, DBG_FN_FREE, 0x200100, 0, 0x1500));
    auto r = m.report(dtfake::resolve);
    CHECK_EQ(r.size(), (size_t)2);
    // most bytes first
    CHECK_EQ(r[0].bytes, (uint64_t)1000);
    CHECK_EQ(r[0].loc.function, std::string("main"));
    CHECK_EQ(r[1].items.size(), (size_t)2);
    CHECK_EQ(r[1].bytes, (uint64_t)200);
    CHECK_EQ(r[1].loc.function, std::string("load_level"));
    CHECK_EQ(r[1].loc.line, 10 + (0x1400 - 0x1000) / 16);      // the call before the return address
    CHECK_EQ(LeakModel::describe(r[1]), std::string("2 x malloc, 200 bytes"));
    CHECK_EQ(m.outstandingBytes(), (uint64_t)1200);
}

TEST(leaks_site_is_first_program_frame)
{
    LeakModel m;
    // allocated inside a library (newlib) called from draw()
    m.add(ev(DBG_RES_VEC, 0, DBG_FN_ALLOCVEC, 0x400000, 32, 0x7000100, 0x1808, 0x1104));
    auto r = m.report(dtfake::resolve);
    CHECK_EQ(r.size(), (size_t)1);
    CHECK_EQ(r[0].site, 0x1808u);
    CHECK_EQ(r[0].loc.function, std::string("draw"));
}

TEST(leaks_without_program_frame_sort_last)
{
    LeakModel m;
    m.add(ev(DBG_RES_VEC, 0, DBG_FN_ALLOCVEC, 0x400000, 4096, 0x7000100));
    m.add(ev(DBG_RES_LOCK, 0, DBG_FN_LOCK, 0x1234, 0, 0x1104));
    auto r = m.report(dtfake::resolve);
    CHECK_EQ(r.size(), (size_t)2);
    CHECK_EQ(r[0].kind, (int)DBG_RES_LOCK);
    CHECK_EQ(r[1].site, 0u);
    CHECK_EQ(r[1].loc.function, std::string("newlib.library: malloc"));
}

TEST(leaks_library_open_counts)
{
    LeakModel m;
    m.add(ev(DBG_RES_LIBRARY, 0, DBG_FN_OPENLIBRARY, 0x5000, 50, 0x1010));
    m.add(ev(DBG_RES_LIBRARY, 0, DBG_FN_OPENLIBRARY, 0x5000, 50, 0x1020));
    m.add(ev(DBG_RES_LIBRARY, 1, DBG_FN_CLOSELIBRARY, 0x5000, 0, 0x1030));
    CHECK_EQ(m.outstanding(), (size_t)1);
    auto r = m.report(dtfake::resolve);
    CHECK_EQ(r.size(), (size_t)1);
    // the most recent open is released first: the first one leaks
    CHECK_EQ(r[0].site, 0x1010u);
    CHECK_EQ(LeakModel::describe(r[0]), std::string("1 x OpenLibrary"));
}

TEST(leaks_mismatched_free_is_a_problem)
{
    LeakModel m;
    m.add(ev(DBG_RES_MALLOC, 0, DBG_FN_MALLOC, 0x600000, 16, 0x1010));
    m.add(ev(DBG_RES_VEC, 1, DBG_FN_FREEVEC, 0x600000, 0, 0x1810));
    CHECK_EQ(m.outstanding(), (size_t)0);
    auto p = m.problems(dtfake::resolve);
    CHECK_EQ(p.size(), (size_t)1);
    CHECK_EQ(p[0].text, std::string("FreeVec() of memory from malloc()"));
    CHECK_EQ(p[0].loc.function, std::string("draw"));
}

TEST(leaks_double_free_is_a_problem)
{
    LeakModel m;
    m.add(ev(DBG_RES_VEC, 0, DBG_FN_ALLOCVEC, 0x600000, 16, 0x1010));
    m.add(ev(DBG_RES_VEC, 1, DBG_FN_FREEVEC, 0x600000, 0, 0x1020));
    m.add(ev(DBG_RES_VEC, 1, DBG_FN_FREEVEC, 0x600000, 0, 0x1030));
    auto p = m.problems(dtfake::resolve);
    CHECK_EQ(p.size(), (size_t)1);
    CHECK_EQ(p[0].text, std::string("FreeVec() of memory that was already freed"));
    // memory reused after the free is fine to free again
    m.add(ev(DBG_RES_MALLOC, 0, DBG_FN_MALLOC, 0x600000, 16, 0x1040));
    m.add(ev(DBG_RES_MALLOC, 1, DBG_FN_FREE, 0x600000, 0, 0x1050));
    CHECK_EQ(m.problems(dtfake::resolve).size(), (size_t)1);
}

TEST(leaks_freemem_size_mismatch)
{
    LeakModel m;
    m.add(ev(DBG_RES_MEM, 0, DBG_FN_ALLOCMEM, 0x700000, 256, 0x1010));
    m.add(ev(DBG_RES_MEM, 1, DBG_FN_FREEMEM, 0x700000, 128, 0x1020));
    auto p = m.problems(dtfake::resolve);
    CHECK_EQ(p.size(), (size_t)1);
    CHECK_EQ(p[0].text, std::string("FreeMem() of 128 bytes, but AllocMem() gave 256"));
    CHECK_EQ(m.outstanding(), (size_t)0);
}

TEST(leaks_unknown_release_is_counted_not_reported)
{
    LeakModel m;
    m.add(ev(DBG_RES_LOCK, 1, DBG_FN_UNLOCK, 0x999, 0, 0x1010));
    m.add(ev(DBG_RES_MALLOC, 1, DBG_FN_FREE, 0x888, 0, 0x1010));
    CHECK_EQ(m.unknownReleases(), 2u);
    CHECK_EQ(m.problems(dtfake::resolve).size(), (size_t)0);
}

TEST(leaks_open_from_lock_transfers_ownership)
{
    LeakModel m;
    m.add(ev(DBG_RES_LOCK, 0, DBG_FN_LOCK, 0x111, 0, 0x1010));
    // OpenFromLock records the lock's release and the file's acquisition
    m.add(ev(DBG_RES_LOCK, 1, DBG_FN_OPENFROMLOCK, 0x111, 0, 0x1020));
    m.add(ev(DBG_RES_FILE, 0, DBG_FN_OPENFROMLOCK, 0x222, 0, 0x1020));
    auto r = m.report(dtfake::resolve);
    CHECK_EQ(r.size(), (size_t)1);
    CHECK_EQ(r[0].kind, (int)DBG_RES_FILE);
}

TEST(leaks_format_bytes)
{
    CHECK_EQ(LeakModel::formatBytes(12), std::string("12 bytes"));
    CHECK_EQ(LeakModel::formatBytes(2048), std::string("2.0 KB"));
    CHECK_EQ(LeakModel::formatBytes(3 * 1024 * 1024), std::string("3.0 MB"));
    CHECK_EQ(std::string(dbg_res_function_name(DBG_FN_ALLOCSYSOBJECTTAGS)), std::string("AllocSysObjectTags"));
    CHECK_EQ(std::string(dbg_res_function_name(DBG_FN_FREE)), std::string("free"));
    CHECK_EQ(std::string(dbg_res_kind_name(DBG_RES_LOCK)), std::string("Lock"));
}
