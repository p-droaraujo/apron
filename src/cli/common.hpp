// CLI helpers: arguments, colored output and the project context.
#pragma once

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/i18n.hpp"
#include "core/events.hpp"
#include "core/project.hpp"
#include "core/scope.hpp"

namespace apron::cli {

struct Args {
  std::vector<std::string> pos;
  std::map<std::string, std::string> opt;  // --key value, or --flag => "1"

  static Args parse(int argc, char** argv, int from, const std::vector<std::string>& flags = {});
  bool has(const std::string& key) const { return opt.count(key) > 0; }
  std::string get(const std::string& key, const std::string& fallback = "") const {
    auto it = opt.find(key);
    return it == opt.end() ? fallback : it->second;
  }
  // Joins the positionals from `from` into one sentence.
  std::string rest(size_t from) const;
};

// Colors only when output is a terminal and NO_COLOR is not set.
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
// Finds the project from the current directory, or explains how to make one.
std::optional<Project> require_project();
std::string default_agent();

int cmd_run(const Args& args);
int cmd_report(const Args& args);
int cmd_log(const Args& args);
int cmd_runs(const Args& args);
int cmd_tasks(const Args& args);
int cmd_ui(const Args& args);
int cmd_check(const Args& args);
int cmd_scope(const Args& args);
int cmd_sessions(const std::string& cmd, const Args& args);
int cmd_projects(const Args& args);
int cmd_hook(const Args& args);
int cmd_review(const Args& args);

// Files a run touched since its base commit, checked against the task's scope.
ScopeReport measure_scope(const Project& project, const std::vector<Event>& events);

// Records an event: appends it to the run's file (the source of truth), then
// tells the local aprond, if it is running. Never waits for the daemon.
bool record(const Project& project, const Event& event, std::string* error);

// Updates a task's state in tasks.md; false if the task is not there.
bool sync_task(const Project& project, const std::string& id, char state, const std::string& by);

}  // namespace apron::cli
