// A one-file test framework: TEST registers, CHECK fails without aborting.
#pragma once

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace apron::test {

struct Case {
  const char* name;
  std::function<void()> fn;
};

inline std::vector<Case>& registry() {
  static std::vector<Case> cases;
  return cases;
}
inline int& failures() {
  static int n = 0;
  return n;
}

struct Register {
  Register(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

}  // namespace apron::test

#define APRON_CAT2(a, b) a##b
#define APRON_CAT(a, b) APRON_CAT2(a, b)
#define TEST(name)                                                                     \
  static void APRON_CAT(test_fn_, __LINE__)();                                         \
  static ::apron::test::Register APRON_CAT(test_reg_, __LINE__)(name, &APRON_CAT(test_fn_, __LINE__)); \
  static void APRON_CAT(test_fn_, __LINE__)()

#define CHECK(cond)                                                              \
  do {                                                                           \
    if (!(cond)) {                                                               \
      ++::apron::test::failures();                                               \
      std::fprintf(stderr, "  failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
    }                                                                            \
  } while (0)

#define CHECK_EQ(a, b)                                                                                    \
  do {                                                                                                    \
    auto&& _a = (a);                                                                                      \
    auto&& _b = (b);                                                                                      \
    if (!(_a == _b)) {                                                                                    \
      ++::apron::test::failures();                                                                        \
      std::fprintf(stderr, "  failed: %s == %s (%s:%d)\n", #a, #b, __FILE__, __LINE__);                   \
    }                                                                                                     \
  } while (0)
