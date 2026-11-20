// apron hook: Claude Code's hooks, turned into run events.
//
// Claude Code runs `apron hook` on its hook events with the event's JSON on
// stdin (integrations/claude-code/). It writes only inside an Apron project
// with a run open, and it never gets in Claude Code's way: every failure exits
// 0 in silence. What each event does:
//
//   PreToolUse        a step when the work changes phase (read, plan, edit, build, test)
//   PostToolUse       a step when a tool ran after an ask: the person approved it
//   Notification      an ask when Claude Code waits for permission or for input
//   UserPromptSubmit  a step when the person answers an ask
//   Stop              asks Claude to close the run or report an ask; the second
//                     time, an ask on its behalf (APRON_HOOK_STOP=0 turns it off)
//   SessionEnd        blocked, when the session ends with the run still open
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>

#include "cli/common.hpp"
#include "core/runs.hpp"
#include "core/time.hpp"

namespace apron::cli {

namespace {

bool env_off(const char* name) {
  const char* v = std::getenv(name);
  return v && std::string(v) == "0";
}

std::vector<std::string> words(const std::string& s) {
  std::vector<std::string> out;
  std::istringstream in(s);
  std::string w;
  while (in >> w) out.push_back(w);
  return out;
}

std::string base(const std::string& w) {
  auto slash = w.rfind('/');
  return slash == std::string::npos ? w : w.substr(slash + 1);
}

// The agent talking to Apron itself is not a phase of the work.
bool is_apron_command(const std::string& command) {
  auto w = words(command);
  for (size_t i = 0; i + 1 < w.size(); ++i)
    if (base(w[i]) == "apron" && (w[i + 1] == "report" || w[i + 1] == "run" || w[i + 1] == "check")) return true;
  return false;
}

std::string bash_phase(const std::string& command) {
  auto w = words(command);
  for (size_t i = 0; i < w.size(); ++i) {
    std::string b = base(w[i]);
    std::string next = i + 1 < w.size() ? w[i + 1] : "";
    if (b == "ctest" || b == "pytest" || b == "vitest" || b == "jest") return "test";
    if ((b == "cargo" || b == "go" || b == "npm" || b == "pnpm" || b == "yarn") && next == "test") return "test";
    if (b == "make" && next == "test") return "test";
    if (b == "ninja" || b == "make" || b == "tsc") return "build";
    if (b == "cmake" && next == "--build") return "build";
    if ((b == "cargo" || b == "go") && next == "build") return "build";
  }
  return "";
}

std::string tool_phase(const std::string& tool, const json::Value& input) {
  if (tool == "Read" || tool == "Grep" || tool == "Glob" || tool == "LS" || tool == "WebFetch" || tool == "WebSearch")
    return "read";
  if (tool == "TodoWrite" || tool == "ExitPlanMode") return "plan";
  if (tool == "Edit" || tool == "MultiEdit" || tool == "Write" || tool == "NotebookEdit") return "edit";
  if (tool == "Bash") {
    const json::Value* ti = input.find("tool_input");
    std::string cmd = ti ? ti->get_str("command") : "";
    if (is_apron_command(cmd)) return "";
    return bash_phase(cmd);
  }
  return "";
}

}  // namespace

int cmd_hook(const Args& args) {
  std::string text((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
  auto input = json::parse(text);
  if (!input || !input->is_object()) return 0;
  std::string event = !args.pos.empty() ? args.pos[0] : input->get_str("hook_event_name");

  std::string dir = input->get_str("cwd");
  if (dir.empty())
    if (const char* p = std::getenv("CLAUDE_PROJECT_DIR")) dir = p;
  auto project = find_project(dir.empty() ? std::filesystem::current_path() : std::filesystem::path(dir));
  if (!project) return 0;
  auto run = project->current_run();
  if (!run) return 0;
  auto events = load_events(project->run_file(*run));
  if (events.empty()) return 0;
  Kind last = events.back().kind;
  bool pt = i18n::current() != i18n::Lang::En;  // the machine's language, like the interface
  auto say = [&](const char* en, const char* ptbr) { return std::string(pt ? ptbr : en); };

  auto report = [&](Kind kind, const std::string& msg) {
    Event e;
    e.ts_ms = timeutil::now_ms();
    e.run = *run;
    e.task = events.front().task;
    e.agent = events.front().agent;
    e.machine = machine_name();
    e.kind = kind;
    e.msg = msg.size() > 200 ? msg.substr(0, 200) : msg;
    std::string err;
    record(*project, e, &err);
    std::string by = e.agent + "@" + e.machine;
    if (kind == Kind::Ask) sync_task(*project, e.task, '?', by);
    else if (kind == Kind::Step && last == Kind::Ask) sync_task(*project, e.task, '~', by);
  };

  if (event == "PreToolUse") {
    if (env_off("APRON_HOOK_STEPS")) return 0;
    std::string phase = tool_phase(input->get_str("tool_name"), *input);
    if (phase.empty()) return 0;
    auto phase_file = project->runs_dir() / (*run + ".claude-code-phase");
    std::string previous;
    std::ifstream(phase_file) >> previous;
    if (previous == phase) return 0;
    std::ofstream(phase_file, std::ios::trunc) << phase << '\n';
    if (phase == "read") report(Kind::Step, say("Claude Code reads the code.", "O Claude Code lê o código."));
    else if (phase == "plan") report(Kind::Step, say("Claude Code plans the work.", "O Claude Code planeja o trabalho."));
    else if (phase == "edit") report(Kind::Step, say("Claude Code edits files.", "O Claude Code edita arquivos."));
    else if (phase == "build") report(Kind::Step, say("Claude Code builds the project.", "O Claude Code compila o projeto."));
    else report(Kind::Step, say("Claude Code runs the tests.", "O Claude Code roda os testes."));
  } else if (event == "PostToolUse") {
    // A tool ran while an ask was open: the person approved the permission
    // prompt. The agent's own `apron report ask` must not undo itself.
    if (last != Kind::Ask) return 0;
    if (const json::Value* ti = input->find("tool_input"); ti && is_apron_command(ti->get_str("command"))) return 0;
    report(Kind::Step, say("The person approves and Claude Code resumes.", "A pessoa aprova e o Claude Code retoma."));
  } else if (event == "Notification") {
    if (last == Kind::Ask) return 0;
    std::string type = input->get_str("notification_type");
    std::string message = input->get_str("message");
    if (type.empty()) {
      if (message.find("permission") != std::string::npos) type = "permission_prompt";
      else if (message.find("waiting") != std::string::npos) type = "idle_prompt";
    }
    if (type == "permission_prompt")
      report(Kind::Ask, say("Claude Code waits for permission: ", "O Claude Code espera permissão: ") + message);
    else if (type == "idle_prompt")
      report(Kind::Ask, say("Claude Code waits for the person's input.", "O Claude Code espera uma resposta da pessoa."));
  } else if (event == "UserPromptSubmit") {
    if (last == Kind::Ask)
      report(Kind::Step, say("The person answers and Claude Code resumes.", "A pessoa responde e o Claude Code retoma."));
  } else if (event == "Stop") {
    if (env_off("APRON_HOOK_STOP")) return 0;
    if (last == Kind::Ask || last == Kind::Blocked || last == Kind::RunFinished) return 0;
    const json::Value* active = input->find("stop_hook_active");
    if (active && active->is_bool() && active->boolean()) {
      report(Kind::Ask, say("Claude Code stops with the run open and waits for the person.",
                            "O Claude Code para com a execução aberta e espera a pessoa."));
      return 0;
    }
    std::string reason = "The Apron run " + *run +
                         " is still open. If the task is done, run `apron check` and then `apron run finish --result done "
                         "\"<what was done; what was left out; what to look at first>\"`. If you need a decision from the "
                         "person, run `apron report ask \"<the question>\"`. If something outside stops you, run `apron "
                         "report blocked \"<the reason>\"`.";
    json::Object out;
    out.emplace_back("decision", "block");
    out.emplace_back("reason", reason);
    std::puts(json::dump(json::Value(std::move(out))).c_str());
  } else if (event == "SessionEnd") {
    if (last == Kind::RunFinished || last == Kind::Blocked) return 0;
    report(Kind::Blocked, say("The Claude Code session ends with the run still open.",
                              "A sessão do Claude Code termina com a execução aberta."));
  }
  return 0;
}

}  // namespace apron::cli
