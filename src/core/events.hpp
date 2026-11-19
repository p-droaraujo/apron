// Eventos de execução: a linguagem com que um agente conta o próprio trabalho.
// Ver docs/agent-protocol.md, seção 4.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/json.hpp"

namespace apron {

enum class Kind { RunStarted, Step, Ask, Blocked, Note, Check, RunFinished };

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
  json::Object extra;  // intent, cause, name, ok, ms, result...

  json::Value to_json() const;
  static std::optional<Event> from_json(const json::Value& value, std::string* error = nullptr);

  // Atalho para campos extras em texto.
  std::string extra_str(std::string_view key) const;
  const json::Value* extra_find(std::string_view key) const;
};

// Uma linha JSON por evento. A escrita é um append único, seguro entre processos
// para linhas curtas (POSIX garante atomicidade de O_APPEND até PIPE_BUF).
bool append_event(const std::filesystem::path& file, const Event& event, std::string* error = nullptr);
std::vector<Event> load_events(const std::filesystem::path& file, std::vector<std::string>* warnings = nullptr);

}  // namespace apron
