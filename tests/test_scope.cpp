#include <unistd.h>

#include <filesystem>
#include <fstream>

#include "core/scope.hpp"
#include "test.hpp"

using namespace apron;
namespace fs = std::filesystem;

TEST("scope: globs with ** span directories, * stays in one") {
  CHECK(glob_match("src/**", "src/core/tasks.cpp"));
  CHECK(glob_match("src/**", "src/main.cpp"));
  CHECK(!glob_match("src/**", "tests/a.cpp"));
  CHECK(glob_match("**", "anything/at/all.md"));
  CHECK(glob_match("src/*.cpp", "src/main.cpp"));
  CHECK(!glob_match("src/*.cpp", "src/core/tasks.cpp"));
  CHECK(glob_match("**/*.md", "docs/agent-protocol.md"));
  CHECK(glob_match("**/*.md", "README.md"));
  CHECK(glob_match("CMakeLists.txt", "CMakeLists.txt"));
  CHECK(!glob_match("CMakeLists.txt", "src/CMakeLists.txt"));
  CHECK(glob_match("src/tui/**", "src/tui/progress.cpp"));
}

TEST("scope: what the T2 run should have flagged") {
  // T2's scope was src/**, tests/**; it also touched AGENTS.md and the protocol.
  std::vector<std::string> touched{".apron/tasks.md", "AGENTS.md", "docs/agent-protocol.md", "src/core/tasks.cpp",
                                   "tests/test_tasks.cpp"};
  ScopeReport r = check_scope(touched, split_scope("src/**, tests/**"));
  CHECK_EQ(r.files.size(), size_t(5));
  CHECK_EQ(r.outside.size(), size_t(2));  // tasks.md is Apron's own write
  CHECK_EQ(r.outside[0], std::string("AGENTS.md"));
}

TEST("scope: changed files come from git, including untracked ones") {
  fs::path root = fs::temp_directory_path() / ("apron-scope-" + std::to_string(::getpid()));
  fs::remove_all(root);
  fs::create_directories(root / "src");
  auto sh = [&](const std::string& cmd) { return std::system(("cd " + root.string() + " && " + cmd + " >/dev/null 2>&1").c_str()) == 0; };
  CHECK(sh("git init -q && git -c user.email=t@t -c user.name=t commit -q --allow-empty -m base"));
  auto base = git_head(root);
  CHECK(base.has_value());
  std::ofstream(root / "src" / "a.cpp") << "x";
  CHECK(sh("git add src/a.cpp && git -c user.email=t@t -c user.name=t commit -q -m a"));
  std::ofstream(root / "README.md") << "y";  // untracked
  auto files = git_changed_files(root, base.value_or(""));
  CHECK_EQ(files.size(), size_t(2));
  ScopeReport r = check_scope(files, {"src/**"});
  CHECK_EQ(r.outside.size(), size_t(1));
  CHECK_EQ(r.outside.empty() ? "" : r.outside[0], std::string("README.md"));
  fs::remove_all(root);
}
