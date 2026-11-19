// O tasks.md: leitura e escrita que preservam tudo que não foi alterado.
// Formato em docs/agent-protocol.md, seção 2.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace apron {

struct Task {
  std::string id;       // T2
  std::string title;    // o resto da linha
  char state = ' ';     // ' ', '~', '?', 'x', '-'
  std::string section;  // o último cabeçalho ## antes da tarefa
  std::vector<std::pair<std::string, std::string>> fields;
  size_t line = 0;       // índice da linha da tarefa
  size_t last_line = 0;  // última linha que pertence à tarefa (campo ou a própria)

  std::string field(std::string_view key) const;
  std::vector<std::string> depends() const;  // ids em `depende`, sem "—"
};

class TaskFile {
 public:
  static std::optional<TaskFile> load(const std::filesystem::path& file, std::string* error = nullptr);
  static TaskFile from_text(const std::string& text);

  const std::vector<Task>& tasks() const { return tasks_; }
  const Task* find(std::string_view id) const;

  bool set_state(std::string_view id, char state);
  // Troca o valor de um campo, cria o campo se não existir, ou remove com valor vazio.
  bool set_field(std::string_view id, std::string_view key, std::string_view value);

  std::string text() const;
  bool save(const std::filesystem::path& file, std::string* error = nullptr) const;

 private:
  std::vector<std::string> lines_;
  bool trailing_newline_ = true;
  std::vector<Task> tasks_;
  void reparse();
};

// Por que uma tarefa não pode ser pega agora; vazio se pode.
std::vector<std::string> blockers_to_start(const TaskFile& file, const Task& task);

}  // namespace apron
