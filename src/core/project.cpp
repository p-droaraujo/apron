#include "core/project.hpp"

#include <unistd.h>

#include <cstdlib>
#include <fstream>

namespace apron {

namespace fs = std::filesystem;

std::optional<Project> find_project(fs::path start) {
  // The real path, with symlinks resolved: on macOS /var is /private/var, and
  // one project must not show up under two names.
  std::error_code ec;
  fs::path dir = fs::weakly_canonical(fs::absolute(start, ec), ec);
  if (ec) return std::nullopt;
  while (true) {
    if (fs::is_directory(dir / ".apron", ec)) return Project{dir};
    if (!dir.has_parent_path() || dir.parent_path() == dir) return std::nullopt;
    dir = dir.parent_path();
  }
}

std::optional<std::string> Project::current_run() const {
  std::ifstream in(current_file());
  std::string run;
  if (in >> run && !run.empty()) return run;
  return std::nullopt;
}

bool Project::set_current_run(const std::string& run) const {
  std::error_code ec;
  fs::create_directories(runs_dir(), ec);
  std::ofstream out(current_file(), std::ios::trunc);
  out << run << '\n';
  return static_cast<bool>(out);
}

void Project::clear_current_run() const {
  std::error_code ec;
  fs::remove(current_file(), ec);
}

std::string machine_name() {
  if (const char* env = std::getenv("APRON_MACHINE"); env && *env) return env;
  char buf[256] = {0};
  if (gethostname(buf, sizeof buf - 1) != 0) return "local";
  std::string name(buf);
  if (auto dot = name.find('.'); dot != std::string::npos) name.resize(dot);
  return name.empty() ? "local" : name;
}

}  // namespace apron
