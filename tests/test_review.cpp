// Review through the real binary, in a throwaway git repository.
#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "core/i18n.hpp"
#include "core/project.hpp"
#include "core/review.hpp"
#include "core/runs.hpp"
#include "core/scope.hpp"
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
    run = run.substr(0, run.find('\n'));  // the run id; the task follows on stderr
    run.erase(run.find_last_not_of("\n") + 1);
    std::ofstream(dir / "feature.txt") << "done\n";
    sh(apron + " run finish 'feature added'");
    sh("git add -A && git commit -q -m 'R1' -m 'Apron-Run: " + run + "'");
    sh("git checkout -q main");
    return run;
  }
};

}  // namespace

// The screen calls these on a Gates card; the tests call them the same way.
bool queued(const apron::Project& p, const std::string& run) {
  for (const auto& r : apron::review_queue(p))
    if (r.id == run) return true;
  return false;
}

TEST("review: a finished run waits in the queue until it is merged into main") {
  apron::i18n::set(apron::i18n::Lang::En);
  Repo r;
  apron::Project p{r.dir};
  std::string run = r.agent_run();
  CHECK(queued(p, run));
  CHECK(!fs::exists(r.dir / "feature.txt"));  // not in main yet
  CHECK_EQ(apron::review_merge(p, run), std::string(""));
  CHECK(fs::exists(r.dir / "feature.txt"));   // main fast-forwarded
  CHECK(!queued(p, run));
  CHECK(r.sh(r.apron + " log " + run).find("merged") != std::string::npos);
}

TEST("review: return reopens the task with the comment the next agent reads") {
  apron::i18n::set(apron::i18n::Lang::En);
  Repo r;
  apron::Project p{r.dir};
  std::string run = r.agent_run();
  CHECK(apron::review_return(p, run, "").find("needs a comment") != std::string::npos);
  CHECK_EQ(apron::review_return(p, run, "keep the old file name"), std::string(""));
  std::string tasks = r.tasks();
  CHECK(tasks.find("- [ ] R1") != std::string::npos);
  CHECK(tasks.find("review: keep the old file name") != std::string::npos);
  CHECK(!queued(p, run));
}

TEST("review: main that moved on is never merged over") {
  apron::i18n::set(apron::i18n::Lang::En);
  Repo r;
  apron::Project p{r.dir};
  std::string run = r.agent_run();
  std::ofstream(r.dir / "other.txt") << "x\n";
  r.sh("git add other.txt && git commit -q -m other");
  CHECK(apron::review_merge(p, run).find("rebase") != std::string::npos);
  CHECK(!fs::exists(r.dir / "feature.txt"));
}

TEST("safety: values from .apron/ never reach a shell or pass for git options") {
  using namespace apron;
  CHECK(valid_run_id("r-T8-20261120-052741"));
  CHECK(!valid_run_id(""));
  CHECK(!valid_run_id("../../etc/passwd"));
  CHECK(!valid_run_id("-oops"));
  CHECK(!valid_run_id("x'; touch pwned; '"));
  CHECK(is_commit_id("9057948828"));
  CHECK(!is_commit_id("--output=/tmp/x"));

  fs::path root = fs::temp_directory_path() / ("apron-safe-" + std::to_string(::getpid()));
  fs::remove_all(root);
  fs::create_directories(root / ".apron" / "runs");
  [[maybe_unused]] int rc = std::system(("cd " + root.string() + " && git init -q && git -c user.email=t@t -c user.name=t commit -q --allow-empty -m base").c_str());
  fs::path pwned = root / "pwned";
  Project p{root};
  run_changes(p, "x'; touch " + pwned.string() + "; '");
  CHECK(git_changed_files(root, "--output=" + pwned.string()).empty());
  CHECK(!fs::exists(pwned));
  CHECK(!review_return(p, "../../x", "comment").empty());
  fs::remove_all(root);
}
