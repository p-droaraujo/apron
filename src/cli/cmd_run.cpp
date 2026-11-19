// apron run start|finish|current
#include <cstdio>

#include "cli/common.hpp"
#include "core/events.hpp"
#include "core/runs.hpp"
#include "core/tasks.hpp"
#include "core/time.hpp"

namespace apron::cli {

namespace {

int run_start(const Args& args, const Project& project) {
  if (args.pos.size() < 2) return fail("uso: apron run start <tarefa> [--agent nome] [--intent \"frase\"]");
  if (auto current = project.current_run()) {
    return fail("já existe uma execução em curso neste worktree (" + *current +
                "); feche com apron run finish antes de começar outra");
  }
  // O contrato mínimo: tarefa com escopo e pronto, dependências feitas.
  const std::string& id = args.pos[1];
  auto file = TaskFile::load(project.tasks_file());
  const Task* task = file ? file->find(id) : nullptr;
  if (task) {
    auto why = blockers_to_start(*file, *task);
    if (!why.empty() && !args.has("force")) {
      std::string msg = "a tarefa " + id + " não pode ser pega:";
      for (const auto& w : why) msg += "\n  - " + w;
      return fail(msg + "\n(use --force para começar mesmo assim)");
    }
  } else {
    std::fprintf(stderr, "aviso: %s não está no tasks.md; a execução fica avulsa\n", id.c_str());
  }

  int64_t now = timeutil::now_ms();
  Event e;
  e.ts_ms = now;
  e.task = args.pos[1];
  e.run = new_run_id(e.task, now);
  e.agent = args.get("agent", default_agent());
  e.machine = machine_name();
  e.kind = Kind::RunStarted;
  std::string intent = args.get("intent", args.rest(2));
  if (!intent.empty()) e.extra.emplace_back("intent", intent);

  std::string err;
  if (!append_event(project.run_file(e.run), e, &err)) return fail(err);
  project.set_current_run(e.run);
  if (task) sync_task(project, id, '~', e.agent + "@" + e.machine);
  std::printf("%s\n", e.run.c_str());
  return 0;
}

int run_finish(const Args& args, const Project& project) {
  std::string run = args.get("run");
  if (run.empty()) {
    auto current = project.current_run();
    if (!current) return fail("nenhuma execução em curso; use --run <id>");
    run = *current;
  }
  auto events = load_events(project.run_file(run));
  if (events.empty()) return fail("execução não encontrada: " + run);
  std::string result = args.get("result", "done");
  if (result != "done" && result != "failed" && result != "abandoned")
    return fail("--result precisa ser done, failed ou abandoned");

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
  std::string err;
  if (!append_event(project.run_file(run), e, &err)) return fail(err);
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
  if (sub == "current") {
    auto current = project->current_run();
    if (!current) return 1;
    std::printf("%s\n", current->c_str());
    return 0;
  }
  return fail("uso: apron run start|finish|current");
}

}  // namespace apron::cli
