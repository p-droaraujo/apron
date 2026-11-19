#include "core/tasks.hpp"

#include <array>
#include <fstream>
#include <sstream>

#include "core/i18n.hpp"

namespace apron {

namespace {

constexpr std::array<std::pair<std::string_view, std::string_view>, 6> kAliases{{
    {"scope", "escopo"},
    {"done-when", "pronto"},
    {"depends", "depende"},
    {"checks", "checks"},
    {"decision", "decisão"},
    {"by", "por"},
}};

std::string trim(std::string_view s) {
  size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
  return std::string(s.substr(a, b - a));
}

size_t indent_of(std::string_view s) {
  size_t n = 0;
  while (n < s.size() && (s[n] == ' ' || s[n] == '\t')) ++n;
  return n;
}

// "- [x] T1 Title" -> state, id, title.
bool parse_task_line(std::string_view line, char& state, std::string& id, std::string& title) {
  size_t i = indent_of(line);
  if (line.substr(i, 3) != "- [" || line.size() < i + 6 || line[i + 4] != ']' || line[i + 5] != ' ') return false;
  state = line[i + 3];
  std::string rest = trim(line.substr(i + 6));
  size_t sp = rest.find(' ');
  id = rest.substr(0, sp);
  title = sp == std::string::npos ? "" : trim(std::string_view(rest).substr(sp + 1));
  return !id.empty();
}

// "  key: value", indented deeper than the task.
bool parse_field_line(std::string_view line, size_t task_indent, std::string& key, std::string& value) {
  size_t i = indent_of(line);
  if (i <= task_indent || i >= line.size()) return false;
  std::string_view body = line.substr(i);
  if (body.rfind("- ", 0) == 0) return false;
  size_t colon = body.find(':');
  if (colon == std::string::npos || colon == 0) return false;
  std::string_view k = body.substr(0, colon);
  for (char c : k)
    if (c == ' ') return false;  // keys are a single word
  key = std::string(k);
  value = trim(body.substr(colon + 1));
  return true;
}

}  // namespace

std::string_view canonical_key(std::string_view key) {
  for (auto [en, pt] : kAliases)
    if (key == en || key == pt) return en;
  return key;
}

std::string_view portuguese_key(std::string_view canonical) {
  for (auto [en, pt] : kAliases)
    if (canonical == en) return pt;
  return canonical;
}

std::string Task::field(std::string_view canonical) const {
  for (const auto& [k, v] : fields)
    if (canonical_key(k) == canonical) return v;
  return "";
}

bool Task::written_in_portuguese() const {
  for (const auto& [k, v] : fields)
    if (k != "checks" && canonical_key(k) != k) return true;
  return false;
}

std::vector<std::string> Task::depends() const {
  std::vector<std::string> out;
  std::stringstream ss(field(kDepends));
  std::string item;
  while (std::getline(ss, item, ',')) {
    item = trim(item);
    if (!item.empty() && item != "—" && item != "-") out.push_back(item);
  }
  return out;
}

TaskFile TaskFile::from_text(const std::string& text) {
  TaskFile f;
  std::string line;
  std::stringstream ss(text);
  while (std::getline(ss, line)) f.lines_.push_back(line);
  f.trailing_newline_ = !text.empty() && text.back() == '\n';
  f.reparse();
  return f;
}

std::optional<TaskFile> TaskFile::load(const std::filesystem::path& file, std::string* error) {
  std::ifstream in(file);
  if (!in) {
    if (error) *error = trf("cannot read {}", {file.string()});
    return std::nullopt;
  }
  std::stringstream ss;
  ss << in.rdbuf();
  return from_text(ss.str());
}

void TaskFile::reparse() {
  tasks_.clear();
  std::string section;
  for (size_t i = 0; i < lines_.size(); ++i) {
    const std::string& line = lines_[i];
    if (line.rfind("## ", 0) == 0) {
      section = trim(std::string_view(line).substr(3));
      continue;
    }
    Task t;
    if (!parse_task_line(line, t.state, t.id, t.title)) continue;
    t.section = section;
    t.line = t.last_line = i;
    size_t base = indent_of(line);
    for (size_t j = i + 1; j < lines_.size(); ++j) {
      std::string key, value;
      if (!parse_field_line(lines_[j], base, key, value)) break;
      t.fields.emplace_back(key, value);
      t.last_line = j;
    }
    tasks_.push_back(std::move(t));
  }
}

const Task* TaskFile::find(std::string_view id) const {
  for (const auto& t : tasks_)
    if (t.id == id) return &t;
  return nullptr;
}

bool TaskFile::set_state(std::string_view id, char state) {
  const Task* t = find(id);
  if (!t) return false;
  std::string& line = lines_[t->line];
  line[indent_of(line) + 3] = state;
  reparse();
  return true;
}

bool TaskFile::set_field(std::string_view id, std::string_view canonical, std::string_view value) {
  const Task* t = find(id);
  if (!t) return false;
  size_t task_indent = indent_of(lines_[t->line]);
  for (size_t j = t->line + 1; j <= t->last_line; ++j) {
    std::string k, v;
    if (parse_field_line(lines_[j], task_indent, k, v) && canonical_key(k) == canonical) {
      if (value.empty()) lines_.erase(lines_.begin() + static_cast<long>(j));
      else lines_[j] = lines_[j].substr(0, indent_of(lines_[j])) + k + ": " + std::string(value);
      reparse();
      return true;
    }
  }
  if (value.empty()) return true;
  std::string_view key = t->written_in_portuguese() ? portuguese_key(canonical) : canonical;
  std::string indent(task_indent + 2, ' ');
  lines_.insert(lines_.begin() + static_cast<long>(t->last_line + 1),
                indent + std::string(key) + ": " + std::string(value));
  reparse();
  return true;
}

std::string TaskFile::text() const {
  std::string out;
  for (size_t i = 0; i < lines_.size(); ++i) {
    out += lines_[i];
    if (i + 1 < lines_.size() || trailing_newline_) out += '\n';
  }
  return out;
}

bool TaskFile::save(const std::filesystem::path& file, std::string* error) const {
  // Write next to it and rename: readers never see a half-written file.
  std::filesystem::path tmp = file;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::trunc | std::ios::binary);
    out << text();
    if (!out) {
      if (error) *error = trf("cannot write {}", {tmp.string()});
      return false;
    }
  }
  std::error_code ec;
  std::filesystem::rename(tmp, file, ec);
  if (ec && error) *error = trf("cannot replace {}: {}", {file.string(), ec.message()});
  return !ec;
}

std::vector<std::string> blockers_to_start(const TaskFile& file, const Task& task) {
  std::vector<std::string> why;
  if (task.field(kScope).empty()) why.push_back(tr("missing the scope field"));
  if (task.field(kDoneWhen).empty()) why.push_back(tr("missing the done-when field"));
  if (task.state == '~') why.push_back(trf("already in progress ({})", {task.field(kBy)}));
  if (task.state == 'x') why.push_back(tr("already done"));
  if (task.state == '-') why.push_back(tr("was abandoned"));
  if (task.state == '?') why.push_back(tr("waiting for a decision from a person"));
  for (const auto& dep : task.depends()) {
    const Task* d = file.find(dep);
    if (!d) why.push_back(trf("depends on {}, which does not exist", {dep}));
    else if (d->state != 'x') why.push_back(trf("depends on {}, which is not done yet", {dep}));
  }
  return why;
}

}  // namespace apron
