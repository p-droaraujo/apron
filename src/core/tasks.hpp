// tasks.md: reading and writing that keep everything that did not change.
// Format in docs/agent-protocol.md, section 2.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace apron {

// Field keys are English; each one also has a Portuguese alias, so a team can
// write the file in either language. Lookups and writes go through these.
inline constexpr std::string_view kScope = "scope";
inline constexpr std::string_view kDoneWhen = "done-when";
inline constexpr std::string_view kDepends = "depends";
inline constexpr std::string_view kChecks = "checks";
inline constexpr std::string_view kDecision = "decision";
inline constexpr std::string_view kBy = "by";

// "escopo" -> "scope"; an English or unknown key comes back unchanged.
std::string_view canonical_key(std::string_view key);
std::string_view portuguese_key(std::string_view canonical);

struct Task {
  std::string id;       // T2
  std::string title;    // the rest of the line
  char state = ' ';     // ' ', '~', '?', 'x', '-'
  std::string section;  // the last ## heading before the task
  std::vector<std::pair<std::string, std::string>> fields;  // keys as written
  size_t line = 0;       // index of the task line
  size_t last_line = 0;  // last line that belongs to the task (a field or the task itself)

  // Value of a field by canonical key, whatever language it was written in.
  std::string field(std::string_view canonical) const;
  std::vector<std::string> depends() const;  // ids in `depends`, without "—"
  bool written_in_portuguese() const;
};

class TaskFile {
 public:
  static std::optional<TaskFile> load(const std::filesystem::path& file, std::string* error = nullptr);
  static TaskFile from_text(const std::string& text);

  const std::vector<Task>& tasks() const { return tasks_; }
  const Task* find(std::string_view id) const;

  bool set_state(std::string_view id, char state);
  // Sets a field by canonical key, keeping the language the task already uses.
  // An empty value removes the field.
  bool set_field(std::string_view id, std::string_view canonical, std::string_view value);

  std::string text() const;
  bool save(const std::filesystem::path& file, std::string* error = nullptr) const;

 private:
  std::vector<std::string> lines_;
  bool trailing_newline_ = true;
  std::vector<Task> tasks_;
  void reparse();
};

// Why a task cannot be picked up now (translated); empty if it can.
std::vector<std::string> blockers_to_start(const TaskFile& file, const Task& task);

}  // namespace apron
