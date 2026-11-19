#include <cstdio>
#include <cstring>
#include <iostream>

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
  if (!project) fail("nenhum .apron/ encontrado daqui até a raiz; crie um com mkdir .apron e um tasks.md");
  return project;
}

std::string default_agent() {
  if (const char* env = std::getenv("APRON_AGENT"); env && *env) return env;
  return "agente";
}

}  // namespace apron::cli

namespace {

constexpr const char* kUsage = R"(apron — operar agentes e acompanhar projetos

Execuções (docs/agent-protocol.md):
  apron run start <tarefa> [--agent nome] [--intent "frase"]
  apron run finish [--result done|failed|abandoned] "resumo"
  apron run current
  apron report step|ask|note "frase"
  apron report blocked --cause gpu|budget|lock|machine|other "frase"
  apron log [execução]          linha do tempo de uma execução (padrão: a atual ou a última)
  apron runs                    todas as execuções do projeto

Variáveis: APRON_AGENT (nome do agente), APRON_MACHINE (nome da máquina), NO_COLOR.
)";

}  // namespace

int main(int argc, char** argv) {
  using namespace apron::cli;
  if (argc < 2 || !std::strcmp(argv[1], "-h") || !std::strcmp(argv[1], "--help") || !std::strcmp(argv[1], "help")) {
    std::fputs(kUsage, stdout);
    return argc < 2 ? 1 : 0;
  }
  std::string cmd = argv[1];
  if (cmd == "run") return cmd_run(Args::parse(argc, argv, 2));
  if (cmd == "report") return cmd_report(Args::parse(argc, argv, 2));
  if (cmd == "log") return cmd_log(Args::parse(argc, argv, 2));
  if (cmd == "runs") return cmd_runs(Args::parse(argc, argv, 2));
  if (cmd == "--version" || cmd == "version") {
    std::puts("apron 0.0.1");
    return 0;
  }
  return fail("comando desconhecido: " + cmd + " (veja apron --help)");
}
