#include "core/events.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <fstream>

#include "core/time.hpp"

namespace apron {

namespace {
constexpr std::array<std::pair<Kind, std::string_view>, 8> kKinds{{
    {Kind::RunStarted, "run.started"},
    {Kind::Step, "step"},
    {Kind::Ask, "ask"},
    {Kind::Blocked, "blocked"},
    {Kind::Note, "note"},
    {Kind::Check, "check"},
    {Kind::RunFinished, "run.finished"},
    {Kind::Review, "review"},
}};
}

std::string_view to_string(Kind kind) {
  for (auto [k, name] : kKinds)
    if (k == kind) return name;
  return "step";
}

std::optional<Kind> kind_from_string(std::string_view text) {
  for (auto [k, name] : kKinds)
    if (name == text) return k;
  return std::nullopt;
}

json::Value Event::to_json() const {
  json::Object o;
  o.emplace_back("ts", timeutil::iso8601(ts_ms));
  o.emplace_back("run", run);
  o.emplace_back("task", task);
  o.emplace_back("agent", agent);
  o.emplace_back("machine", machine);
  o.emplace_back("kind", std::string(to_string(kind)));
  if (!msg.empty()) o.emplace_back("msg", msg);
  for (const auto& kv : extra) o.push_back(kv);
  return json::Value(std::move(o));
}

std::optional<Event> Event::from_json(const json::Value& value, std::string* error) {
  if (!value.is_object()) {
    if (error) *error = "event is not an object";
    return std::nullopt;
  }
  Event e;
  auto ts = timeutil::parse_iso8601(value.get_str("ts"));
  auto kind = kind_from_string(value.get_str("kind"));
  if (!ts || !kind) {
    if (error) *error = "event without a valid ts or kind";
    return std::nullopt;
  }
  e.ts_ms = *ts;
  e.kind = *kind;
  e.run = value.get_str("run");
  e.task = value.get_str("task");
  e.agent = value.get_str("agent");
  e.machine = value.get_str("machine");
  e.msg = value.get_str("msg");
  static constexpr std::array<std::string_view, 7> kBase{"ts", "run", "task", "agent", "machine", "kind", "msg"};
  for (const auto& [k, v] : value.obj()) {
    bool base = false;
    for (auto b : kBase)
      if (b == k) base = true;
    if (!base) e.extra.emplace_back(k, v);
  }
  return e;
}

const json::Value* Event::extra_find(std::string_view key) const {
  for (const auto& [k, v] : extra)
    if (k == key) return &v;
  return nullptr;
}

std::string Event::extra_str(std::string_view key) const {
  const json::Value* v = extra_find(key);
  return v && v->is_string() ? v->str() : std::string();
}

bool append_event(const std::filesystem::path& file, const Event& event, std::string* error) {
  std::error_code ec;
  std::filesystem::create_directories(file.parent_path(), ec);
  std::string line = json::dump(event.to_json());
  line += '\n';
  int fd = ::open(file.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
  if (fd < 0) {
    if (error) *error = std::string("cannot open ") + file.string() + ": " + std::strerror(errno);
    return false;
  }
  ssize_t written = ::write(fd, line.data(), line.size());
  ::close(fd);
  if (written != static_cast<ssize_t>(line.size())) {
    if (error) *error = "short write to " + file.string();
    return false;
  }
  return true;
}

std::vector<Event> load_events(const std::filesystem::path& file, std::vector<std::string>* warnings) {
  std::vector<Event> out;
  std::ifstream in(file);
  std::string line;
  int number = 0;
  while (std::getline(in, line)) {
    ++number;
    if (line.empty()) continue;
    std::string err;
    auto value = json::parse(line, &err);
    std::optional<Event> event;
    if (value) event = Event::from_json(*value, &err);
    if (event) out.push_back(std::move(*event));
    else if (warnings) warnings->push_back(file.filename().string() + ":" + std::to_string(number) + ": " + err);
  }
  return out;
}

}  // namespace apron
