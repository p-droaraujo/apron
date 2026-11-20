// apron run start|finish|current
#include <cstdio>

#include "cli/common.hpp"
#include "core/events.hpp"
#include "core/runs.hpp"
#include "core/scope.hpp"
#include "core/tasks.hpp"
#include "core/time.hpp"

namespace apron::cli {

namespace {

// Lanes: parallel runs are safe when their scopes do not meet. When this run's
// scope meets one still in progress, say so, on the terminal and in the run.
void warn_overlaps(const Project& project, const TaskFile& file, const Task& task, const Event& started) {
  auto mine = split_scope(task.field(kScope));
  for (const auto& r : list_runs(project)) {
    if (r.id == started.run || r.state == RunState::Done || r.state == RunState::Failed || r.state == RunState::Abandoned)
      continue;
    const Task* other = file.find(r.task);
    if (!other || other->id == task.id) continue;
    std::vector<std::string> shared;
    for (const auto& theirs : split_scope(other->field(kScope)))
      for (const auto& g : mine)
        if (globs_may_overlap(g, theirs)) {
          shared.push_back(theirs);
          break;
        }
    if (shared.empty()) continue;
    std::string list;
    for (const auto& g : shared) list += (list.empty() ? "" : ", ") + g;
    std::string msg = trf("scope meets {} ({}, {}@{}): {}", {r.task, r.id, r.agent, r.machine, list});
    std::fprintf(stderr, "%s\n", trf("warning: {}", {msg}).c_str());
    Event n;
    n.ts_ms = timeutil::now_ms();
    n.run = started.run;
    n.task = started.task;
    n.agent = started.agent;
    n.machine = started.machine;
    n.kind = Kind::Note;
    n.msg = msg;
    std::string err;
    record(project, n, &err);
  }
}

int run_start(const Args& args, const Project& project) {
  if (args.pos.size() < 2) return fail(tr("usage: apron run start <task> [--agent name] [--intent \"sentence\"]"));
  if (auto current = project.current_run()) {
    return fail(trf("a run is already in progress in this worktree ({}); close it with apron run finish first",
                    {*current}));
  }
  // The minimum contract: a task with scope and done-when, dependencies done.
  const std::string& id = args.pos[1];
  auto file = TaskFile::load(project.tasks_file());
  const Task* task = file ? file->find(id) : nullptr;
  if (task) {
    auto why = blockers_to_start(*file, *task);
    if (!why.empty() && !args.has("force")) {
      std::string msg = trf("task {} cannot be picked up:", {id});
      for (const auto& w : why) msg += "\n  - " + w;
      return fail(msg + "\n" + tr("(use --force to start anyway)"));
    }
  } else {
    std::fprintf(stderr, "%s\n", trf("warning: {} is not in tasks.md; the run stands alone", {id}).c_str());
  }

  int64_t now = timeutil::now_ms();
  Event e;
  e.ts_ms = now;
  e.task = id;
  e.run = new_run_id(e.task, now);
  e.agent = args.get("agent", default_agent());
  e.machine = machine_name();
  e.kind = Kind::RunStarted;
  std::string intent = args.get("intent", args.rest(2));
  if (!intent.empty()) e.extra.emplace_back("intent", intent);
  // The commit the run starts from: the scope meter diffs against it.
  if (auto head = git_head(project.root)) e.extra.emplace_back("base", *head);
  // Inside an apron session, the run remembers which one: the agent's terminal.
  if (const char* session = std::getenv("APRON_SESSION"); session && *session) e.extra.emplace_back("session", std::string(session));

  std::string err;
  if (!record(project, e, &err)) return fail(err);
  project.set_current_run(e.run);
  if (task) sync_task(project, id, '~', e.agent + "@" + e.machine);
  std::printf("%s\n", e.run.c_str());
  if (task && file) warn_overlaps(project, *file, *task, e);
  return 0;
}

}  // namespace

ScopeReport measure_scope(const Project& project, const std::vector<Event>& events) {
  if (events.empty()) return {};
  const Event& first = events.front();
  std::string base = first.extra_str("base");
  if (base.empty()) {
    // Runs started before the meter existed: use the commit HEAD had at the start.
    if (auto at = git_commit_at(project.root, first.ts_ms)) base = *at;
  }
  if (base.empty()) return {};
  std::vector<std::string> scope = {"**"};
  if (auto file = TaskFile::load(project.tasks_file()))
    if (const Task* t = file->find(first.task)) scope = split_scope(t->field(kScope));
  return check_scope(git_changed_files(project.root, base), scope);
}

namespace {

int run_finish(const Args& args, const Project& project) {
  std::string run = args.get("run");
  if (run.empty()) {
    auto current = project.current_run();
    if (!current) return fail(tr("no run in progress; use --run <id>"));
    run = *current;
  }
  auto events = load_events(project.run_file(run));
  if (events.empty()) return fail(trf("run not found: {}", {run}));
  std::string result = args.get("result", "done");
  if (result != "done" && result != "failed" && result != "abandoned")
    return fail(tr("--result must be done, failed or abandoned"));

  const Event& first = events.front();
  Event e;
  e.ts_ms = timeutil::now_ms();
  e.run = run;
  e.task = first.task;
  e.agent = first.agent;
  e.machine = machine_name();
  e.kind = Kind::RunFinished;
  e.msg = args.rest(1);
  e.extra.emplace_back("result", result);

  // Scope meter: what this run touched, and what left the task's scope.
  ScopeReport scope = measure_scope(project, events);
  json::Array files, outside;
  for (const auto& f : scope.files) files.emplace_back(f);
  for (const auto& f : scope.outside) outside.emplace_back(f);
  e.extra.emplace_back("files", std::move(files));
  e.extra.emplace_back("outside", std::move(outside));
  if (!scope.outside.empty()) {
    std::fprintf(stderr, "%s\n", trf("warning: {} of {} files left the scope:", {std::to_string(scope.outside.size()), std::to_string(scope.files.size())}).c_str());
    for (const auto& f : scope.outside) std::fprintf(stderr, "  %s\n", f.c_str());
  }
  std::string err;
  if (!record(project, e, &err)) return fail(err);
  if (project.current_run() == run) project.clear_current_run();
  sync_task(project, first.task, result == "done" ? 'x' : result == "abandoned" ? '-' : ' ', "");
  return 0;
}

}  // namespace

int cmd_run(const Args& args) {
  auto project = require_project();
  if (!project) return 1;
  std::string sub = args.pos.empty() ? "" : args.pos[0];
  if (sub == "start") return run_start(args, *project);
  if (sub == "finish") return run_finish(args, *project);

  return fail(tr("usage: apron run start|finish"));
}

}  // namespace apron::cli
