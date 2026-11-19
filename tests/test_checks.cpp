#include <filesystem>

#include "core/checks.hpp"
#include "test.hpp"

using namespace apron;

TEST("checks: reads [[check]] tables and ignores the rest") {
  auto checks = parse_checks(R"(# comment
[[check]]
name = "build"   # trailing comment
run  = "make -j 'a#b'"
timeout = 30

[other]
name = "not a check"

[[check]]
name = 'test'
run = "ctest"

[[check]]
name = "no command"
)");
  CHECK_EQ(checks.size(), size_t(2));
  CHECK_EQ(checks[0].name, std::string("build"));
  CHECK_EQ(checks[0].run, std::string("make -j 'a#b'"));
  CHECK_EQ(checks[0].timeout_s, 30);
  CHECK_EQ(checks[1].name, std::string("test"));
}

TEST("checks: exit code, output tail and timeout") {
  auto root = std::filesystem::temp_directory_path();
  CheckResult ok = run_check({"ok", "true", 5}, root);
  CHECK(ok.ok);
  CHECK(ok.tail.empty());
  CheckResult bad = run_check({"bad", "echo boom; exit 3", 5}, root);
  CHECK(!bad.ok);
  CHECK_EQ(bad.exit_code, 3);
  CHECK(bad.tail.find("boom") != std::string::npos);
  CheckResult slow = run_check({"slow", "sleep 5", 1}, root);
  CHECK(!slow.ok);
  CHECK(slow.timed_out);
  CHECK(slow.ms < 3000);
}
