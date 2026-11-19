#include <cstdio>
#include <cstring>

#include "cli/common.hpp"

namespace apron::cli {

Args Args::parse(int argc, char** argv, int from, const std::vector<std::string>& flags) {
  Args a;
  for (int i = from; i < argc; ++i) {
    std::string s = argv[i];
    if (s.rfind("--", 0) == 0 && s.size() > 2) {
      std::string key = s.substr(2);
      if (auto eq = key.find('='); eq != std::string::npos) {
        a.opt[key.substr(0, eq)] = key.substr(eq + 1);
        continue;
      }
      bool is_flag = false;
      for (const auto& f : flags)
        if (f == key) is_flag = true;
      if (is_flag || i + 1 >= argc) a.opt[key] = "1";
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

Runs (docs/agent-protocol.md):
  apron run start <task> [--agent name] [--intent "sentence"] [--force]
  apron run finish [--result done|failed|abandoned] "summary"
  apron run current
  apron report step|ask|note "sentence"
  apron report blocked --cause gpu|budget|lock|machine|other "sentence"
  apron log [run]               timeline of a run (default: the current or the latest)
  apron check [names...]        run checks.toml and record the results in the current run
  apron scope [run]             files the run touched, and which left the task's scope
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
