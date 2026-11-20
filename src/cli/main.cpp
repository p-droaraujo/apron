#include <cstdio>
#include <cstring>

#include "cli/common.hpp"

namespace apron::cli {

Args Args::parse(int argc, char** argv, int from, const std::vector<std::string>& flags) {
  Args a;
  auto is_flag = [&](const std::string& key) {
    for (const auto& f : flags)
      if (f == key) return true;
    return false;
  };
  for (int i = from; i < argc; ++i) {
    std::string s = argv[i];
    if (s == "--") {  // everything after is positional, e.g. a command to run
      for (++i; i < argc; ++i) a.pos.emplace_back(argv[i]);
      break;
    }
    if (s.size() == 2 && s[0] == '-' && s[1] != '-' && is_flag(s.substr(1))) {  // -d
      a.opt[s.substr(1)] = "1";
      continue;
    }
    if (s.rfind("--", 0) == 0 && s.size() > 2) {
      std::string key = s.substr(2);
      if (auto eq = key.find('='); eq != std::string::npos) {
        a.opt[key.substr(0, eq)] = key.substr(eq + 1);
        continue;
      }
      if (is_flag(key) || i + 1 >= argc) a.opt[key] = "1";
      else a.opt[key] = argv[++i];
    } else {
      a.pos.push_back(std::move(s));
    }
  }
  return a;
}

std::string Args::rest(size_t from) const {
  std::string out;
  for (size_t i = from; i < pos.size(); ++i) {
    if (!out.empty()) out += ' ';
    out += pos[i];
  }
  return out;
}

int fail(const std::string& message) {
  std::fprintf(stderr, "apron: %s\n", message.c_str());
  return 1;
}

std::optional<Project> require_project() {
  auto project = find_project(std::filesystem::current_path());
  if (!project) fail(tr("no .apron/ found from here to the root; create one with mkdir .apron and a tasks.md"));
  return project;
}

std::string default_agent() {
  if (const char* env = std::getenv("APRON_AGENT"); env && *env) return env;
  return "agent";
}

}  // namespace apron::cli

namespace {

constexpr const char* kUsage = R"(apron — operate AI agents and follow projects

  apron                         open the progress view in the terminal

Sessions (kept by aprond, started on demand):
  apron new [-d] [command...]   new session in this directory, attached unless -d
  apron ls                      sessions and the program in the foreground of each
  apron attach <session>        attach this terminal; Ctrl-] detaches
  apron kill <session>          end a session
  apron daemon [stop]           is aprond running? stop it
  apron projects                every project on this machine, with its runs

Runs (docs/agent-protocol.md):
  apron run start <task> [--agent name] [--intent "sentence"] [--force]
  apron run finish [--result done|failed|abandoned] "summary"
  apron run current
  apron report step|ask|note "sentence"
  apron report blocked --cause gpu|budget|lock|machine|other "sentence"
  apron log [run]               timeline of a run (default: the current or the latest)
  apron check [names...]        run checks.toml and record the results in the current run
  apron scope [run]             files the run touched, and which left the task's scope
  apron review                  finished runs waiting for a person
  apron review <run> merge|return "comment"|discard
  apron runs                    every run in the project
  apron tasks [--ready]         the plan in tasks.md, with each task's state

Environment: APRON_AGENT (agent name), APRON_MACHINE (machine name),
APRON_LANG (en or pt-BR; default: system locale, then English), NO_COLOR.
)";

}  // namespace

int main(int argc, char** argv) {
  using namespace apron::cli;
  if (argc < 2) return cmd_ui(Args{});
  if (!std::strcmp(argv[1], "-h") || !std::strcmp(argv[1], "--help") || !std::strcmp(argv[1], "help")) {
    std::fputs(apron::tr(kUsage).c_str(), stdout);
    return 0;
  }
  std::string cmd = argv[1];
  if (cmd == "run") return cmd_run(Args::parse(argc, argv, 2, {"force"}));
  if (cmd == "report") return cmd_report(Args::parse(argc, argv, 2));
  if (cmd == "log") return cmd_log(Args::parse(argc, argv, 2));
  if (cmd == "new" || cmd == "ls" || cmd == "attach" || cmd == "kill" || cmd == "daemon")
    return cmd_sessions(cmd, Args::parse(argc, argv, 2, {"d", "detach"}));
  if (cmd == "machines") return cmd_machines(Args::parse(argc, argv, 2));
  if (cmd == "proxy") return cmd_proxy(Args::parse(argc, argv, 2));
  if (cmd == "review") return cmd_review(Args::parse(argc, argv, 2));
  if (cmd == "hook") return cmd_hook(Args::parse(argc, argv, 2));
  if (cmd == "projects") return cmd_projects(Args::parse(argc, argv, 2));
  if (cmd == "scope") return cmd_scope(Args::parse(argc, argv, 2));
  if (cmd == "check") return cmd_check(Args::parse(argc, argv, 2));
  if (cmd == "runs") return cmd_runs(Args::parse(argc, argv, 2));
  if (cmd == "tasks") return cmd_tasks(Args::parse(argc, argv, 2, {"ready", "prontas"}));
  if (cmd == "--version" || cmd == "version") {
    std::puts("apron 0.0.1");
    return 0;
  }
  return fail(apron::trf("unknown command: {} (see apron --help)", {cmd}));
}
