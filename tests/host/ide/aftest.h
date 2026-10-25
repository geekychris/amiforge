// Minimal unit-test harness for the host-side tests of AmiForge's portable IDE code.
//   TEST(name) { CHECK(a == b); CHECK_EQ(str, "x"); }
// Each tests/host/ide/<area>.mk adds its test sources and the src files they need; `make -C
// tests/host ide` builds one runner per area and runs them all.
#pragma once
#include <cstdio>
#include <string>
#include <vector>
#include <functional>

namespace aftest {
struct Case { const char *name; std::function<void()> fn; };
inline std::vector<Case> &cases() { static std::vector<Case> v; return v; }
inline int &failures() { static int n = 0; return n; }
struct Reg { Reg(const char *n, std::function<void()> f) { cases().push_back({n, f}); } };
inline void fail(const char *file, int line, const std::string &msg)
{
    std::printf("  FAIL %s:%d: %s\n", file, line, msg.c_str());
    failures()++;
}
}

#define AF_CAT2(a, b) a##b
#define AF_CAT(a, b) AF_CAT2(a, b)
#define TEST(name) \
    static void AF_CAT(test_, name)(); \
    static aftest::Reg AF_CAT(reg_, name)(#name, AF_CAT(test_, name)); \
    static void AF_CAT(test_, name)()
#define CHECK(c) do { if (!(c)) aftest::fail(__FILE__, __LINE__, #c); } while (0)
#define CHECK_EQ(a, b) do { auto _a = (a); auto _b = (b); if (!(_a == _b)) \
    aftest::fail(__FILE__, __LINE__, std::string(#a " == " #b "  (got: ") + aftest::show(_a) + ")"); } while (0)

namespace aftest {
inline std::string show(const std::string &s) { return "\"" + s + "\""; }
inline std::string show(const char *s) { return s ? "\"" + std::string(s) + "\"" : "null"; }
template <class T> std::string show(const T &v) { return std::to_string(v); }
}
