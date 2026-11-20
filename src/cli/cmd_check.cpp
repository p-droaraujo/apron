// apron check [names...]: run checks.toml and record the results.
#include <cstdio>
#include <sstream>

#include "cli/common.hpp"
#include "core/checks.hpp"
#include "core/events.hpp"
#include "core/scope.hpp"
#include "core/tasks.hpp"
#include "core/time.hpp"

namespace apron::cli {

namespace {

std::vector<std::string> split_names(const std::string& list) {
  std::vector<std::string> out;
  std::stringstream ss(list);
  std::string item;
  while (std::getline(ss, item, ',')) {
    size_t a = item.find_first_not_of(" \t"), b = item.find_last_not_of(" \t");
    if (a != std::string::npos) out.push_back(item.substr(a, b - a + 1));
  }
  return out;
}

}  // namespace

int cmd_check(const Args& args) {
  auto project = require_project();
  if (!project) return 1;
  std::string err;
  auto checks = load_checks(project->checks_file(), &err);
  if (!checks) return fail(err);
  if (!err.empty()) std::fprintf(stderr, "%s\n", trf("warning: {}", {err}).c_str());
  if (checks->empty()) return fail(tr("checks.toml has no [[check]] with name and run"));

  // Which checks: the ones named on the command line, else the task's `checks`
  // field, else all of them.
  auto current = project->current_run();
  std::vector<Event> run_events;
  if (current) run_events = load_events(project->run_file(*current));
  std::vector<std::string> wanted = args.pos;
  if (wanted.empty() && !run_events.empty()) {
    if (auto file = TaskFile::load(project->tasks_file()))
      if (const Task* t = file->find(run_events.front().task)) wanted = split_names(t->field(kChecks));
  }
  std::vector<Check> selected;
  for (const auto& c : *checks) {
    bool take = wanted.empty();
    for (const auto& w : wanted) take = take || w == c.name;
    if (take) selected.push_back(c);
  }
  for (const auto& w : wanted) {
    bool known = false;
    for (const auto& c : *checks) known = known || c.name == w;
    if (!known) std::fprintf(stderr, "%s\n", trf("warning: no check named {} in checks.toml", {w}).c_str());
  }

  // The run's context, for checks that judge the run itself (a verifier).
  Env env;
  if (current && !run_events.empty()) {
    const Event& start = run_events.front();
    env.emplace_back("APRON_RUN", *current);
    env.emplace_back("APRON_TASK", start.task);
    if (std::string base = start.extra_str("base"); is_commit_id(base)) env.emplace_back("APRON_BASE", base);
    if (auto file = TaskFile::load(project->tasks_file()))
      if (const Task* t = file->find(start.task)) {
        std::string text = t->id + " " + t->title;
        for (const auto& [k, v] : t->fields) text += "\n" + k + ": " + v;
        env.emplace_back("APRON_TASK_TEXT", text);
      }
  }

  Style st;
  int failed = 0;
  for (const auto& c : selected) {
    // The "running" line is only drawn on a terminal, where \r can replace it.
    if (st.on) {
      std::printf("%s %s", st.dim("…").c_str(), c.name.c_str());
      std::fflush(stdout);
    }
    CheckResult r = run_check(c, project->root, env);
    std::string when = timeutil::short_duration(r.ms);
    std::string mark = r.ok ? "✓" : st.red("✕");
    std::string why = r.timed_out ? " " + st.red(trf("timed out after {}s", {std::to_string(c.timeout_s)})) : "";
    std::printf("%s%s %s %s%s\n", st.on ? "\r" : "", mark.c_str(), c.name.c_str(), st.dim(when).c_str(), why.c_str());
    if (!r.ok) {
      ++failed;
      std::fputs(st.dim(r.tail).c_str(), stdout);
    }
    if (current && !run_events.empty()) {
      Event e;
      e.ts_ms = timeutil::now_ms();
      e.run = *current;
      e.task = run_events.front().task;
      e.agent = run_events.front().agent;
      e.machine = machine_name();
      e.kind = Kind::Check;
      e.extra.emplace_back("name", c.name);
      e.extra.emplace_back("ok", r.ok);
      e.extra.emplace_back("ms", static_cast<long long>(r.ms));
      if (!r.ok) e.extra.emplace_back("exit", r.exit_code);
      std::string ignored;
      record(*project, e, &ignored);
    }
  }
  if (!current) std::printf("%s\n", st.dim(tr("no run in progress: results were not recorded")).c_str());
  return failed == 0 ? 0 : 1;
}

}  // namespace apron::cli
