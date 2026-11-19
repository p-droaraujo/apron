// apron report step|ask|blocked|note "sentence"
#include "cli/common.hpp"
#include "core/events.hpp"
#include "core/tasks.hpp"
#include "core/time.hpp"

namespace apron::cli {

int cmd_report(const Args& args) {
  auto project = require_project();
  if (!project) return 1;
  if (args.pos.empty()) return fail(tr("usage: apron report step|ask|blocked|note \"sentence\""));

  auto kind = kind_from_string(args.pos[0]);
  if (!kind || *kind == Kind::RunStarted || *kind == Kind::RunFinished || *kind == Kind::Check)
    return fail(trf("invalid report kind: {} (use step, ask, blocked or note)", {args.pos[0]}));

  std::string msg = args.rest(1);
  if (msg.empty()) return fail(tr("a report needs a sentence"));

  std::string run = args.get("run");
  if (run.empty()) {
    auto current = project->current_run();
    if (!current) return fail(tr("no run in progress; start one with apron run start <task>"));
    run = *current;
  }
  auto events = load_events(project->run_file(run));
  if (events.empty()) return fail(trf("run not found: {}", {run}));

  Event e;
  e.ts_ms = timeutil::now_ms();
  e.run = run;
  e.task = events.front().task;
  e.agent = events.front().agent;
  e.machine = machine_name();
  e.kind = *kind;
  e.msg = msg;
  if (*kind == Kind::Blocked) {
    std::string cause = args.get("cause", "other");
    if (cause != "gpu" && cause != "budget" && cause != "lock" && cause != "machine" && cause != "other")
      return fail(tr("--cause must be gpu, budget, lock, machine or other"));
    e.extra.emplace_back("cause", cause);
  }
  std::string err;
  if (!record(*project, e, &err)) return fail(err);
  // An ask moves the task to [?]; the next step moves it back to [~].
  std::string by = e.agent + "@" + e.machine;
  if (*kind == Kind::Ask) sync_task(*project, e.task, '?', by);
  else if (*kind == Kind::Step) {
    if (auto file = TaskFile::load(project->tasks_file()); file && file->find(e.task) && file->find(e.task)->state == '?')
      sync_task(*project, e.task, '~', by);
  }
  return 0;
}

}  // namespace apron::cli
