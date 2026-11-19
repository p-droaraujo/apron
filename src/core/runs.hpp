// A run summarized from its events: what the interface shows.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/events.hpp"
#include "core/project.hpp"

namespace apron {

// Agent state, in the interface's vocabulary (see docs/agent-protocol.md).
enum class RunState { Working, Waiting, Blocked, Done, Failed, Abandoned };

std::string_view to_string(RunState state);  // English, stable
std::string label(RunState state);           // translated, for display

struct RunSummary {
  std::string id;
  std::string task;
  std::string agent;
  std::string machine;
  std::string intent;
  int64_t started_ms = 0;
  int64_t last_ms = 0;
  RunState state = RunState::Working;
  std::string last_msg;  // the most recent sentence
  int steps = 0;
  int checks_ok = 0;
  int checks_failed = 0;
  int files = -1;  // -1 = not measured
  std::vector<std::string> outside;
};

RunSummary summarize(const std::string& id, const std::vector<Event>& events);

// Every run in the project, newest first.
std::vector<RunSummary> list_runs(const Project& project);

std::string new_run_id(const std::string& task, int64_t now_ms);

}  // namespace apron
