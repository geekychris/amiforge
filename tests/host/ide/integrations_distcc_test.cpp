// distcc command lines.
#include "aftest.h"
#include "ide/DistccCmd.h"

using namespace af;

TEST(distcc_hosts)
{
    auto h = distcc::parseHosts("192.168.101.2, 10.0.2.2:3633/4  localhost;mac.local");
    CHECK_EQ(h.size(), (size_t)4);
    CHECK_EQ(h[1], std::string("10.0.2.2:3633/4"));
    CHECK_EQ(distcc::hostsValue(h), std::string("192.168.101.2 10.0.2.2:3633/4 localhost mac.local"));
    std::string err;
    CHECK(distcc::validHost("192.168.101.2", err));
    CHECK(distcc::validHost("mac.local:3632/8", err));
    CHECK(distcc::validHost("localhost", err));
    CHECK(!distcc::validHost("host:abc", err));
    CHECK(!distcc::validHost("host:99999", err));
    CHECK(!distcc::validHost("host/0", err));
    CHECK(!distcc::validHost("bad host", err));
    CHECK(!distcc::validHost(":3632", err));
    CHECK(distcc::parseHosts("   ").empty());
}

TEST(distcc_wrap)
{
    CHECK_EQ(distcc::wrapCompile("SDK:C/distcc", "SDK:gcc/bin/g++ -O0 -gstabs+ -Wall -c bouncer.cpp -o obj/bouncer.o"),
             std::string("SDK:C/distcc g++ -O0 -gstabs+ -Wall -c bouncer.cpp -o obj/bouncer.o"));
    CHECK_EQ(distcc::wrapCompile("distcc", "gcc -c a.c -o a.o"), std::string("distcc gcc -c a.c -o a.o"));
    CHECK_EQ(distcc::wrapCompile("distcc", "Work:tools/ppc-amigaos-gcc -c a.c"), std::string("distcc ppc-amigaos-gcc -c a.c"));
}

TEST(distcc_which_files)
{
    CHECK(distcc::distributable("a.c"));
    CHECK(distcc::distributable("dir/b.CPP"));
    CHECK(distcc::distributable("x.cc"));
    CHECK(!distcc::distributable("fastsum.s"));
    CHECK(!distcc::distributable("start.S"));
    CHECK(!distcc::distributable("header.h"));
    CHECK(!distcc::distributable("Makefile"));
}
