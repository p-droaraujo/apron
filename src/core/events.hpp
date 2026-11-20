// Run events: the language an agent uses to narrate its own work.
// See docs/agent-protocol.md, section 4.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/json.hpp"

namespace apron {

enum class Kind { RunStarted, Step, Ask, Blocked, Note, Check, RunFinished, Review };

std::string_view to_string(Kind kind);
std::optional<Kind> kind_from_string(std::string_view text);

struct Event {
  int64_t ts_ms = 0;
  std::string run;
  std::string task;
  std::string agent;
  std::string machine;
  Kind kind = Kind::Step;
  std::string msg;
  json::Object extra;  // intent, name, ok, ms, result...

  json::Value to_json() const;
  static std::optional<Event> from_json(const json::Value& value, std::string* error = nullptr);

  // Shortcut for text extra fields.
  std::string extra_str(std::string_view key) const;
  const json::Value* extra_find(std::string_view key) const;
};

// One JSON line per event. Each write is a single append, safe across processes
// for short lines (POSIX makes O_APPEND writes up to PIPE_BUF atomic).
bool append_event(const std::filesystem::path& file, const Event& event, std::string* error = nullptr);
std::vector<Event> load_events(const std::filesystem::path& file, std::vector<std::string>* warnings = nullptr);

}  // namespace apron
