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

constexpr const char* kUsage = R"(apron: one screen for your coding agents and projects

  apron                  open the screen
  apron new [command]    start a session here and attach to it; -d leaves it in the background

For agents (docs/agent-protocol.md):
  apron run start <task> [--agent name] [--intent "sentence"]
  apron run finish [--result done|failed|abandoned] "summary"
  apron report step|ask|blocked|note "sentence"
  apron check [names...]

The language follows the system; APRON_LANG=en or pt-BR chooses.
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
  if (cmd == "check") return cmd_check(Args::parse(argc, argv, 2));
  if (cmd == "new" || cmd == "daemon") return cmd_sessions(cmd, Args::parse(argc, argv, 2, {"d", "detach"}));
  // Plumbing, out of the help: Claude Code's hooks, ssh, the daemon, the
  // timeline that goes into a commit, and the machine list for scripts.
  if (cmd == "hook") return cmd_hook(Args::parse(argc, argv, 2));
  if (cmd == "proxy") return cmd_proxy(Args::parse(argc, argv, 2));
  if (cmd == "log") return cmd_log(Args::parse(argc, argv, 2));
  if (cmd == "machines") return cmd_machines(Args::parse(argc, argv, 2));
  if (cmd == "--version" || cmd == "version") {
    std::puts("apron 0.0.1");
    return 0;
  }
  return fail(apron::trf("unknown command: {} (see apron --help)", {cmd}));
}
