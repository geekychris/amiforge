#include "aftest.h"
#include "debugtools_fake.h"
#include "ProfileModel.h"

using namespace af;

static dbg_sample smp(uint32_t ip, int waiting, uint32_t c0 = 0, uint32_t c1 = 0, uint32_t c2 = 0)
{
    dbg_sample s;
    std::memset(&s, 0, sizeof s);
    s.ip = ip;
    s.state = waiting ? DBG_SAMPLE_WAITING : DBG_SAMPLE_RUNNING;
    uint32_t c[3] = { c0, c1, c2 };
    for (int i = 0; i < 3 && c[i]; i++) s.callers[s.ncallers++] = c[i];
    return s;
}

TEST(profile_self_and_total)
{
    ProfileModel p;
    // 6 samples in draw (called from main), 2 in main itself
    for (int i = 0; i < 6; i++) p.add(smp(0x1810, 0, 0x1108));
    p.add(smp(0x1100, 0, 0x9000000));
    p.add(smp(0x1104, 0, 0x9000000));
    p.finish(dtfake::resolve);
    CHECK_EQ(p.samples(), (uint64_t)8);
    auto &f = p.functions();
    CHECK(f.size() >= 2);
    CHECK_EQ(f[0].name, std::string("draw"));
    CHECK_EQ(f[0].self, (uint64_t)6);
    CHECK_EQ(f[0].total, (uint64_t)6);
    CHECK_EQ(f[1].name, std::string("main"));
    CHECK_EQ(f[1].self, (uint64_t)2);
    CHECK_EQ(f[1].total, (uint64_t)8);     // in the chain of draw's samples too
    CHECK_EQ(f[0].line, 10 + (0x1810 - 0x1000) / 16);
    CHECK(p.percent(6) > 74.9 && p.percent(6) < 75.1);
}

TEST(profile_os_time_goes_to_the_caller)
{
    ProfileModel p;
    // waiting in the kernel, called from load_level (0x1408 returns to it), called from main
    for (int i = 0; i < 4; i++) p.add(smp(0x9100000, 1, 0x9200000, 0x1408, 0x1108));
    p.add(smp(0x7000010, 0, 0x1404));   // in newlib, from load_level
    p.finish(dtfake::resolve);
    CHECK_EQ(p.waiting(), (uint64_t)4);
    auto &f = p.functions();
    const ProfFunction *ll = nullptr, *os = nullptr, *mn = nullptr;
    for (auto &x : f) {
        if (x.name == "load_level") ll = &x;
        if (x.name == "[OS] kernel") os = &x;
        if (x.name == "main") mn = &x;
    }
    CHECK(ll && os && mn);
    if (!ll || !os || !mn) return;
    CHECK_EQ(ll->self, (uint64_t)0);
    CHECK_EQ(ll->inOS, (uint64_t)5);
    CHECK_EQ(ll->waiting, (uint64_t)4);
    CHECK_EQ(ll->total, (uint64_t)5);
    CHECK_EQ(mn->inOS, (uint64_t)0);      // load_level is nearer
    CHECK_EQ(mn->total, (uint64_t)4);
    CHECK_EQ(os->self, (uint64_t)4);
    CHECK(!os->program);
    // program rows come before OS rows
    CHECK(f.front().program);
    CHECK(!f.back().program);
    // the call lines get the OS time
    auto &lines = p.lines().at("main.c");
    CHECK_EQ(lines.at(10 + (0x1404 - 0x1000) / 16).inOS, (uint64_t)5);
}

TEST(profile_annotation_text)
{
    ProfileModel p;
    for (int i = 0; i < 3; i++) p.add(smp(0x1010, 0, 0));
    p.add(smp(0x9000000, 0, 0x1014));
    p.finish(dtfake::resolve);
    ProfLine l = p.lines().at("main.c").at(11);
    CHECK_EQ(p.annotation(l), std::string("\xAB 75.0% (3) + 25.0% in OS"));
    ProfLine only; only.self = 1;
    CHECK_EQ(p.annotation(only), std::string("\xAB 25.0% (1)"));
}

TEST(profile_reset)
{
    ProfileModel p;
    p.add(smp(0x1010, 0));
    p.reset();
    p.finish(dtfake::resolve);
    CHECK_EQ(p.samples(), (uint64_t)0);
    CHECK(p.functions().empty());
}
