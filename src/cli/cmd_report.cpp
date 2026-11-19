// apron report step|ask|blocked|note "frase"
#include "cli/common.hpp"
#include "core/events.hpp"
#include "core/tasks.hpp"
#include "core/time.hpp"

namespace apron::cli {

int cmd_report(const Args& args) {
  auto project = require_project();
  if (!project) return 1;
  if (args.pos.empty()) return fail("uso: apron report step|ask|blocked|note \"frase\"");

  auto kind = kind_from_string(args.pos[0]);
  if (!kind || *kind == Kind::RunStarted || *kind == Kind::RunFinished || *kind == Kind::Check)
    return fail("tipo de relato inválido: " + args.pos[0] + " (use step, ask, blocked ou note)");

  std::string msg = args.rest(1);
  if (msg.empty()) return fail("o relato precisa de uma frase");

  std::string run = args.get("run");
  if (run.empty()) {
    auto current = project->current_run();
    if (!current) return fail("nenhuma execução em curso; comece com apron run start <tarefa>");
    run = *current;
  }
  auto events = load_events(project->run_file(run));
  if (events.empty()) return fail("execução não encontrada: " + run);

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
      return fail("--cause precisa ser gpu, budget, lock, machine ou other");
    e.extra.emplace_back("cause", cause);
  }
  std::string err;
  if (!append_event(project->run_file(run), e, &err)) return fail(err);
  // Uma pergunta põe a tarefa em [?]; o próximo passo a devolve para [~].
  std::string por = e.agent + "@" + e.machine;
  if (*kind == Kind::Ask) sync_task(*project, e.task, '?', por);
  else if (*kind == Kind::Step) {
    if (auto file = TaskFile::load(project->tasks_file()); file && file->find(e.task) && file->find(e.task)->state == '?')
      sync_task(*project, e.task, '~', por);
  }
  return 0;
}

}  // namespace apron::cli
