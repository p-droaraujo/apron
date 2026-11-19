// Review through the real binary, in a throwaway git repository.
#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "test.hpp"

namespace fs = std::filesystem;

namespace {

struct Repo {
  fs::path dir;
  std::string env;
  std::string apron = std::string(APRON_BINARY_DIR) + "/apron";
  Repo() {
    dir = fs::temp_directory_path() / ("apron-review-" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir / ".apron");
    env = "cd " + dir.string() + " && APRON_SOCKET=" + (dir / "none.sock").string() +
          " APRON_LANG=en NO_COLOR=1 APRON_AGENT=claude APRON_MACHINE=box GIT_AUTHOR_NAME=t GIT_AUTHOR_EMAIL=t@t "
          "GIT_COMMITTER_NAME=t GIT_COMMITTER_EMAIL=t@t ";
    std::ofstream(dir / ".apron" / "tasks.md") << "- [ ] R1 Review me\n  scope: **\n  done-when: merged\n";
    std::ofstream(dir / ".gitignore") << ".apron/runs/\n";
    sh("git init -q -b main && git add -A && git commit -q -m base");
  }
  ~Repo() { fs::remove_all(dir); }
  std::string sh(const std::string& cmd) {
    std::string out;
    FILE* p = popen((env + cmd + " 2>&1").c_str(), "r");
    char buf[4096];
    size_t n;
    while (p && (n = fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
    if (p) pclose(p);
    return out;
  }
  std::string tasks() {
    std::ifstream in(dir / ".apron" / "tasks.md");
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
  }
  // An agent's whole run on its branch, committed with the Apron-Run trailer.
  std::string agent_run() {
    sh("git checkout -q -b apron/R1");
    std::string run = sh(apron + " run start R1 --intent work");
    run.erase(run.find_last_not_of("\n") + 1);
    std::ofstream(dir / "feature.txt") << "done\n";
    sh(apron + " run finish 'feature added'");
    sh("git add -A && git commit -q -m 'R1' -m 'Apron-Run: " + run + "'");
    sh("git checkout -q main");
    return run;
  }
};

}  // namespace

TEST("review: a finished run waits in the queue until it is merged into main") {
  Repo r;
  std::string run = r.agent_run();
  CHECK(r.sh(r.apron + " review").find("R1") != std::string::npos);
  CHECK(!fs::exists(r.dir / "feature.txt"));  // not in main yet
  CHECK_EQ(r.sh(r.apron + " review " + run + " merge"), std::string(""));
  CHECK(fs::exists(r.dir / "feature.txt"));   // main fast-forwarded
  CHECK(r.sh(r.apron + " review").find("nothing to review") != std::string::npos);
  CHECK(r.sh(r.apron + " log " + run).find("merged") != std::string::npos);
}

TEST("review: return reopens the task with the comment the next agent reads") {
  Repo r;
  std::string run = r.agent_run();
  CHECK(r.sh(r.apron + " review " + run + " return").find("needs a comment") != std::string::npos);
  r.sh(r.apron + " review " + run + " return 'keep the old file name'");
  std::string tasks = r.tasks();
  CHECK(tasks.find("- [ ] R1") != std::string::npos);
  CHECK(tasks.find("review: keep the old file name") != std::string::npos);
  CHECK(r.sh(r.apron + " review").find("nothing to review") != std::string::npos);
}

TEST("review: main that moved on is never merged over") {
  Repo r;
  std::string run = r.agent_run();
  std::ofstream(r.dir / "other.txt") << "x\n";
  r.sh("git add other.txt && git commit -q -m other");
  CHECK(r.sh(r.apron + " review " + run + " merge").find("rebase") != std::string::npos);
  CHECK(!fs::exists(r.dir / "feature.txt"));
}
