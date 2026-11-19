// Onde as coisas de um projeto do Apron moram no disco.
#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace apron {

struct Project {
  std::filesystem::path root;  // diretório que contém .apron/

  std::filesystem::path apron_dir() const { return root / ".apron"; }
  std::filesystem::path runs_dir() const { return apron_dir() / "runs"; }
  std::filesystem::path tasks_file() const { return apron_dir() / "tasks.md"; }
  std::filesystem::path checks_file() const { return apron_dir() / "checks.toml"; }
  std::filesystem::path run_file(const std::string& run) const { return runs_dir() / (run + ".jsonl"); }
  // Aponta para a execução em curso neste worktree.
  std::filesystem::path current_file() const { return runs_dir() / "current"; }

  std::optional<std::string> current_run() const;
  bool set_current_run(const std::string& run) const;
  void clear_current_run() const;
};

// Sobe a partir de `start` até achar um diretório com .apron/.
std::optional<Project> find_project(std::filesystem::path start);

// Nome curto da máquina (hostname até o primeiro ponto), ou APRON_MACHINE.
std::string machine_name();

}  // namespace apron
