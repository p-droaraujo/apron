// Utilidades da CLI: argumentos, saída colorida e o contexto do projeto.
#pragma once

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/project.hpp"

namespace apron::cli {

struct Args {
  std::vector<std::string> pos;
  std::map<std::string, std::string> opt;  // --chave valor, ou --flag => "1"

  static Args parse(int argc, char** argv, int from, const std::vector<std::string>& flags = {});
  bool has(const std::string& key) const { return opt.count(key) > 0; }
  std::string get(const std::string& key, const std::string& fallback = "") const {
    auto it = opt.find(key);
    return it == opt.end() ? fallback : it->second;
  }
  // Junta os posicionais a partir de `from` numa frase só.
  std::string rest(size_t from) const;
};

// Cores só quando a saída é um terminal e NO_COLOR não está definido.
struct Style {
  bool on;
  Style() : on(isatty(STDOUT_FILENO) && !std::getenv("NO_COLOR")) {}
  std::string c(const char* code, const std::string& text) const {
    return on ? std::string("\x1b[") + code + "m" + text + "\x1b[0m" : text;
  }
  std::string blue(const std::string& t) const { return c("38;2;122;162;247", t); }
  std::string amber(const std::string& t) const { return c("38;2;224;175;104", t); }
  std::string red(const std::string& t) const { return c("38;2;247;118;142", t); }
  std::string dim(const std::string& t) const { return c("38;2;125;133;146", t); }
  std::string bold(const std::string& t) const { return c("1", t); }
};

int fail(const std::string& message);
// Encontra o projeto a partir do diretório atual, ou explica como criar um.
std::optional<Project> require_project();
std::string default_agent();

int cmd_run(const Args& args);
int cmd_report(const Args& args);
int cmd_log(const Args& args);
int cmd_runs(const Args& args);
int cmd_tasks(const Args& args);
int cmd_ui(const Args& args);

// Atualiza o estado de uma tarefa no tasks.md; false se a tarefa não estiver lá.
bool sync_task(const Project& project, const std::string& id, char state, const std::string& por);

}  // namespace apron::cli
