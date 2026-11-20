#include "core/review.hpp"

#include <algorithm>
#include <sstream>

#include "core/events.hpp"
#include "core/i18n.hpp"
#include "core/scope.hpp"
#include "core/tasks.hpp"
#include "core/time.hpp"

namespace apron {

namespace {

std::string trim(std::string s) {
  size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

std::string git(const Project& p, std::vector<std::string> args) {
  args.insert(args.begin(), "git");
  return trim(capture(args, p.root).value_or(""));
}
bool git_ok(const Project& p, std::vector<std::string> args) {
  args.insert(args.begin(), "git");
  return capture(args, p.root).has_value();
}

bool record_review(const Project& project, const std::string& run, const std::string& action, const std::string& msg) {
  if (!valid_run_id(run)) return false;
  auto events = load_events(project.run_file(run));
  if (events.empty()) return false;
  Event e;
  e.ts_ms = timeutil::now_ms();
  e.run = run;
  e.task = events.front().task;
  e.agent = events.front().agent;
  e.machine = machine_name();
  e.kind = Kind::Review;
  e.msg = msg;
  e.extra.emplace_back("action", action);
  return append_event(project.run_file(run), e);
}

void reopen(const Project& project, const std::string& task, const std::string& comment) {
  auto file = TaskFile::load(project.tasks_file());
  if (!file || !file->find(task)) return;
  file->set_state(task, ' ');
  file->set_field(task, kBy, "");
  file->set_field(task, kReview, comment);
  file->save(project.tasks_file());
}

}  // namespace

std::vector<RunSummary> review_queue(const Project& project) {
  std::vector<RunSummary> out;
  for (auto& r : list_runs(project))
    if (r.needs_review()) out.push_back(std::move(r));
  std::stable_sort(out.begin(), out.end(), [](const RunSummary& a, const RunSummary& b) {
    auto rank = [](const RunSummary& r) { return r.checks_failed > 0 || r.state == RunState::Failed ? 0 : !r.outside.empty() ? 1 : 2; };
    return rank(a) < rank(b);
  });
  return out;
}

RunChanges run_changes(const Project& project, const std::string& run) {
  RunChanges c;
  // The run's commit carries its id in the Apron-Run trailer (see the protocol).
  if (!valid_run_id(run)) return c;
  c.commit = git(project, {"log", "--all", "-1", "--format=%H", "--grep=^Apron-Run: " + run + "$"});
  if (!is_commit_id(c.commit)) {
    c.commit.clear();
    auto events = load_events(project.run_file(run));
    std::string base = events.empty() ? "" : events.front().extra_str("base");
    if (is_commit_id(base)) c.stat = git(project, {"diff", "--stat", base});
    return c;
  }
  c.stat = git(project, {"show", "--stat", "--format=", c.commit});
  std::istringstream branches(git(project, {"branch", "--contains", c.commit, "--format=%(refname:short)"}));
  std::string b;
  while (std::getline(branches, b)) {
    b = trim(b);
    if (b.rfind("apron/", 0) == 0) c.branch = b;
    if (c.branch.empty() && b != "main") c.branch = b;
  }
  return c;
}

std::string review_merge(const Project& project, const std::string& run) {
  RunChanges c = run_changes(project, run);
  if (c.commit.empty()) return tr("the run has no commit with its Apron-Run trailer yet");
  // Already in main? Then there is nothing to move, only to record.
  bool in_main = git_ok(project, {"merge-base", "--is-ancestor", c.commit, "main"});
  if (!in_main) {
    if (!git_ok(project, {"merge-base", "--is-ancestor", "main", c.commit}))
      return tr("main has moved on since this run; rebase its branch onto main first");
    if (git(project, {"rev-parse", "--abbrev-ref", "HEAD"}) == "main") {
      if (!git(project, {"status", "--porcelain", "--untracked-files=no"}).empty())
        return tr("main has uncommitted changes; commit or stash them first");
      if (!git_ok(project, {"merge", "--ff-only", c.commit})) return tr("git could not fast-forward main");
    } else if (!git_ok(project, {"fetch", ".", c.commit + ":main"})) {
      return tr("git could not fast-forward main");
    }
  }
  record_review(project, run, "merged", "");
  return "";
}

std::string review_return(const Project& project, const std::string& run, const std::string& comment) {
  if (!valid_run_id(run)) return trf("run not found: {}", {run});
  if (trim(comment).empty()) return tr("returning a run needs a comment for the agent");
  auto events = load_events(project.run_file(run));
  if (events.empty()) return trf("run not found: {}", {run});
  record_review(project, run, "returned", comment);
  reopen(project, events.front().task, trim(comment));
  return "";
}

std::string review_discard(const Project& project, const std::string& run) {
  if (!valid_run_id(run)) return trf("run not found: {}", {run});
  auto events = load_events(project.run_file(run));
  if (events.empty()) return trf("run not found: {}", {run});
  record_review(project, run, "discarded", "");
  reopen(project, events.front().task, "");
  return "";
}

}  // namespace apron
