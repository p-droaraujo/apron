#include "daemon/registry.hpp"

#include <cstdlib>
#include <fstream>

#include "core/runs.hpp"
#include "core/time.hpp"

namespace apron::daemon {

namespace fs = std::filesystem;

fs::path Registry::default_file() {
  if (const char* state = std::getenv("XDG_STATE_HOME"); state && *state) return fs::path(state) / "apron" / "projects";
  const char* home = std::getenv("HOME");
  return fs::path(home && *home ? home : "/tmp") / ".local" / "state" / "apron" / "projects";
}

Registry::Registry(fs::path file) : file_(std::move(file)) {
  std::ifstream in(file_);
  std::string line;
  while (std::getline(in, line))
    if (!line.empty()) roots_.insert(line);
}

bool Registry::add(const std::string& root) {
  if (!roots_.insert(root).second) return false;
  save();
  return true;
}

void Registry::save() const {
  std::error_code ec;
  fs::create_directories(file_.parent_path(), ec);
  std::ofstream out(file_, std::ios::trunc);
  for (const auto& r : roots_) out << r << '\n';
}

json::Value Registry::list() const {
  json::Array out;
  for (const auto& root : roots_) {
    std::error_code ec;
    if (!fs::is_directory(fs::path(root) / ".apron", ec)) continue;  // moved or deleted
    Project p{root};
    int working = 0, waiting = 0, blocked = 0, done = 0;
    int64_t last = 0;
    std::string last_msg, last_task;
    for (const auto& r : list_runs(p)) {
      switch (r.state) {
        case RunState::Working: ++working; break;
        case RunState::Waiting: ++waiting; break;
        case RunState::Blocked: ++blocked; break;
        default: ++done;
      }
      if (r.last_ms > last) {
        last = r.last_ms;
        last_msg = r.last_msg;
        last_task = r.task;
      }
    }
    json::Object o;
    o.emplace_back("name", fs::path(root).filename().string());
    o.emplace_back("root", root);
    o.emplace_back("working", working);
    o.emplace_back("waiting", waiting);
    o.emplace_back("blocked", blocked);
    o.emplace_back("done", done);
    if (last) {
      o.emplace_back("last", timeutil::iso8601(last));
      o.emplace_back("last_task", last_task);
      o.emplace_back("last_msg", last_msg);
    }
    out.push_back(json::Value(std::move(o)));
  }
  return json::Value(std::move(out));
}

}  // namespace apron::daemon
