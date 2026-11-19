// apron log [execução] e apron runs
#include <cstdio>

#include "cli/common.hpp"
#include "core/runs.hpp"
#include "core/time.hpp"

namespace apron::cli {

namespace {

std::string state_label(const Style& st, RunState s) {
  std::string label(to_string(s));
  switch (s) {
    case RunState::Working: return st.blue("● " + label);
    case RunState::Waiting: return st.amber("◐ " + label);
    case RunState::Blocked: return st.dim("⊘ " + label);
    case RunState::Done: return "✓ " + label;
    case RunState::Failed: return st.red("✕ " + label);
    case RunState::Abandoned: return st.dim("– " + label);
  }
  return label;
}

std::string pad(std::string s, size_t width) {
  // Largura em colunas, contando caracteres UTF-8 e não bytes.
  // Sequências de cor (ESC [ ... m) não ocupam colunas.
  size_t cols = 0;
  bool esc = false;
  for (unsigned char c : s) {
    if (esc) {
      if (c == 'm') esc = false;
      continue;
    }
    if (c == 0x1b) {
      esc = true;
      continue;
    }
    if ((c & 0xC0) != 0x80) ++cols;
  }
  if (cols < width) s.append(width - cols, ' ');
  return s;
}

void print_event(const Style& st, const Event& e, int64_t start) {
  std::string when = st.dim(pad(timeutil::short_duration(e.ts_ms - start), 7));
  std::string glyph, label, msg = e.msg;
  switch (e.kind) {
    case Kind::RunStarted:
      glyph = st.blue("▶");
      label = "iniciou";
      msg = e.extra_str("intent");
      break;
    case Kind::Step: glyph = st.blue("·"); label = "passo"; break;
    case Kind::Ask: glyph = st.amber("◐"); label = st.amber("pergunta"); break;
    case Kind::Blocked:
      glyph = st.dim("⊘");
      label = "sem energia";
      msg = "(" + e.extra_str("cause") + ") " + msg;
      break;
    case Kind::Note: glyph = st.dim("›"); label = "nota"; break;
    case Kind::Check: {
      const json::Value* ok = e.extra_find("ok");
      bool passed = ok && ok->is_bool() && ok->boolean();
      glyph = passed ? "✓" : st.red("✕");
      label = "check";
      msg = e.extra_str("name");
      if (const json::Value* ms = e.extra_find("ms"); ms && ms->is_number())
        msg += st.dim(" " + timeutil::short_duration(static_cast<int64_t>(ms->num())));
      break;
    }
    case Kind::RunFinished: {
      std::string result = e.extra_str("result");
      glyph = result == "failed" ? st.red("■") : "■";
      label = "terminou";
      msg = st.bold(result) + (e.msg.empty() ? "" : " · " + e.msg);
      break;
    }
  }
  std::printf("  %s %s %s %s\n", when.c_str(), glyph.c_str(), pad(label, 12).c_str(), msg.c_str());
}

}  // namespace

int cmd_log(const Args& args) {
  auto project = require_project();
  if (!project) return 1;
  std::string run = args.pos.empty() ? "" : args.pos[0];
  if (run.empty()) {
    if (auto current = project->current_run()) run = *current;
    else {
      auto runs = list_runs(*project);
      if (runs.empty()) return fail("nenhuma execução neste projeto ainda");
      run = runs.front().id;
    }
  }
  std::vector<std::string> warnings;
  auto events = load_events(project->run_file(run), &warnings);
  if (events.empty()) return fail("execução não encontrada: " + run);
  RunSummary s = summarize(run, events);

  Style st;
  std::printf("%s %s %s %s %s %s %s\n", st.bold(s.task).c_str(), st.dim("·").c_str(),
              (s.agent + "@" + s.machine).c_str(), st.dim("·").c_str(),
              timeutil::short_duration(s.last_ms - s.started_ms).c_str(), st.dim("·").c_str(),
              state_label(st, s.state).c_str());
  std::printf("%s\n\n", st.dim(run).c_str());
  for (const auto& e : events) print_event(st, e, s.started_ms);
  for (const auto& w : warnings) std::fprintf(stderr, "aviso: linha ignorada em %s\n", w.c_str());
  return 0;
}

int cmd_runs(const Args&) {
  auto project = require_project();
  if (!project) return 1;
  auto runs = list_runs(*project);
  if (runs.empty()) {
    std::puts("nenhuma execução ainda");
    return 0;
  }
  Style st;
  auto current = project->current_run();
  for (const auto& s : runs) {
    std::string checks;
    if (s.checks_ok + s.checks_failed > 0)
      checks = "checks " + std::to_string(s.checks_ok) + "/" + std::to_string(s.checks_ok + s.checks_failed);
    std::printf("%s %s %s %s %s %s\n", pad(state_label(st, s.state), 18).c_str(), pad(s.task, 5).c_str(),
                pad(s.agent + "@" + s.machine, 18).c_str(),
                st.dim(pad(timeutil::short_duration(s.last_ms - s.started_ms), 7)).c_str(), pad(checks, 11).c_str(),
                (current && *current == s.id ? st.bold(s.last_msg) : s.last_msg).c_str());
  }
  return 0;
}

}  // namespace apron::cli
