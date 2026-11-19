// The projects this machine's daemon has heard about, kept across restarts.
#pragma once

#include <filesystem>
#include <set>
#include <string>

#include "core/json.hpp"

namespace apron::daemon {

class Registry {
 public:
  // $XDG_STATE_HOME/apron/projects, else ~/.local/state/apron/projects.
  static std::filesystem::path default_file();

  explicit Registry(std::filesystem::path file);
  // True if the project was new.
  bool add(const std::string& root);
  // Each project with its runs summarized from the files on disk.
  json::Value list() const;

 private:
  std::filesystem::path file_;
  std::set<std::string> roots_;
  void save() const;
};

}  // namespace apron::daemon
